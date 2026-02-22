/***************************************************************************
 * realign_xcorr_cuda.cu
 *
 * CUDA implementation of normalised cross-correlation spike alignment.
 *
 * Thread organisation
 * -------------------
 * Grid:  (nSpikes,  2*maxShift+1)    — one block per (spike, lag) pair
 * Block: (nChannels × nSamples_clamped, 1, 1) — but since waveforms are
 *        short (typically 4ch × 32samp = 128 elements), we use a 1-D block
 *        of BLOCK_SIZE threads and let each thread accumulate one partial sum.
 *
 * Because nSamples is small (16–64 typically), we use a simpler layout:
 *   Grid:  (nSpikes)           — one block per spike
 *   Block: (2*maxShift+1)      — one thread per candidate lag
 *
 * Each thread iterates over all (ch, s) pairs and computes the numerator
 * and spike-energy for its lag, then writes score and lag to shared memory.
 * A reduction finds the best lag within the block.
 *
 * For large nSpikes or large maxShift the grid/block dims are adjusted to
 * respect hardware limits.
 ***************************************************************************/

#ifdef USE_CUDA

#include "realign_xcorr.h"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// Kernel: one block per spike, one thread per lag candidate
// Shared memory: 2 × (2*maxShift+1) floats for score and lag bookkeeping.
// ---------------------------------------------------------------------------

__global__ void xcorr_kernel(
    const int16_t* __restrict__ waveforms,   // [nSpikes × nChan × nSamp]
    const int16_t* __restrict__ tmpl,        // [nChan × nSamp]
    float* __restrict__ tmplEnergyBuf,       // [1] — precomputed sqrt(tmplEnergy)
    int nChan, int nSamp,
    int maxShift, float minScore,
    int*   __restrict__ shifts_out,
    float* __restrict__ scores_out)
{
    const int sp  = blockIdx.x;
    // Thread index maps to lag: lag = threadIdx.x - maxShift
    const int lag = static_cast<int>(threadIdx.x) - maxShift;

    extern __shared__ float shmem[];   // [nLags] scores, then [nLags] lags-as-float
    const int nLags = 2 * maxShift + 1;
    float* sh_score = shmem;
    float* sh_lag   = shmem + nLags;

    sh_score[threadIdx.x] = -FLT_MAX;
    sh_lag  [threadIdx.x] = 0.0f;
    __syncthreads();

    const int16_t* spk = waveforms + static_cast<long long>(sp) * nChan * nSamp;
    const float sqrtTmpl = tmplEnergyBuf[0];

    // Zero-padded xcorr: template index s pairs with spike index s+lag.
    // Spike samples outside [0,nSamp) contribute 0 to both num and spkEnergy.
    {
        double num       = 0.0;
        double spkEnergy = 0.0;

        for (int ch = 0; ch < nChan; ++ch) {
            const int16_t* tch = tmpl + ch * nSamp;
            const int16_t* sch = spk  + ch * nSamp;
            for (int s = 0; s < nSamp; ++s) {
                int sLag = s + lag;
                if (sLag < 0 || sLag >= nSamp) continue;
                double tv = static_cast<double>(tch[s]);
                double sv = static_cast<double>(sch[sLag]);
                num       += tv * sv;
                spkEnergy += sv * sv;
            }
        }

        double denom = static_cast<double>(sqrtTmpl) * sqrt(spkEnergy);
        float score  = (denom > 1e-12) ? static_cast<float>(num / denom) : 0.0f;

        sh_score[threadIdx.x] = score;
        sh_lag  [threadIdx.x] = static_cast<float>(lag);
    }
    __syncthreads();

    // Parallel reduction: find max score within block
    for (int stride = nLags / 2; stride >= 1; stride >>= 1) {
        if (static_cast<int>(threadIdx.x) < stride) {
            if (sh_score[threadIdx.x + stride] > sh_score[threadIdx.x]) {
                sh_score[threadIdx.x] = sh_score[threadIdx.x + stride];
                sh_lag  [threadIdx.x] = sh_lag  [threadIdx.x + stride];
            }
        }
        __syncthreads();
    }

    if (threadIdx.x == 0) {
        float best = sh_score[0];
        int   blag = static_cast<int>(sh_lag[0]);
        // Negate: bestLag>0 means spike is late, caller adds shift to timestamp
        // so shift must be negative to move the spike earlier.
        shifts_out[sp] = (best >= minScore) ? -blag : 0;
        scores_out[sp] = best;
    }
}

// ---------------------------------------------------------------------------
// Template energy precomputation kernel (single block, single thread)
// ---------------------------------------------------------------------------
__global__ void tmpl_energy_kernel(
    const int16_t* __restrict__ tmpl,
    int nChan, int nSamp,
    float* __restrict__ out)
{
    double e = 0.0;
    for (int ch = 0; ch < nChan; ++ch)
        for (int s = 0; s < nSamp; ++s) {
            double v = static_cast<double>(tmpl[ch * nSamp + s]);
            e += v * v;
        }
    out[0] = static_cast<float>(sqrt(e));
}

// ---------------------------------------------------------------------------
// Host-side entry points
// ---------------------------------------------------------------------------

extern "C" {

int xcorr_cuda_available()
{
    int n = 0;
    cudaError_t err = cudaGetDeviceCount(&n);
    return (err == cudaSuccess && n > 0) ? 1 : 0;
}

int xcorr_cuda_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out)
{
    const size_t waveBytes = static_cast<size_t>(nSpikes) * nChannels * nSamples * sizeof(int16_t);
    const size_t tmplBytes = static_cast<size_t>(nChannels) * nSamples * sizeof(int16_t);
    const size_t shiftBytes = static_cast<size_t>(nSpikes) * sizeof(int);
    const size_t scoreBytes = static_cast<size_t>(nSpikes) * sizeof(float);

    int16_t *d_wave = nullptr, *d_tmpl = nullptr;
    float   *d_tmplE = nullptr, *d_scores = nullptr;
    int     *d_shifts = nullptr;

    if (cudaMalloc(&d_wave,   waveBytes)  != cudaSuccess) return -1;
    if (cudaMalloc(&d_tmpl,   tmplBytes)  != cudaSuccess) { cudaFree(d_wave); return -1; }
    if (cudaMalloc(&d_tmplE,  sizeof(float)) != cudaSuccess) goto cleanup;
    if (cudaMalloc(&d_shifts, shiftBytes) != cudaSuccess) goto cleanup;
    if (cudaMalloc(&d_scores, scoreBytes) != cudaSuccess) goto cleanup;

    cudaMemcpy(d_wave, waveforms, waveBytes,  cudaMemcpyHostToDevice);
    cudaMemcpy(d_tmpl, tmpl,      tmplBytes,  cudaMemcpyHostToDevice);

    // Precompute template energy
    tmpl_energy_kernel<<<1, 1>>>(d_tmpl, nChannels, nSamples, d_tmplE);

    {
        const int nLags = 2 * maxShift + 1;
        // Block size must be a power of 2 >= nLags for the reduction.
        int blockSz = 1;
        while (blockSz < nLags) blockSz <<= 1;
        if (blockSz > 1024) {
            fprintf(stderr, "[xcorr_cuda] maxShift too large for single-block reduction\n");
            goto cleanup;
        }
        size_t shmBytes = 2 * static_cast<size_t>(blockSz) * sizeof(float);
        xcorr_kernel<<<nSpikes, blockSz, shmBytes>>>(
            d_wave, d_tmpl, d_tmplE,
            nChannels, nSamples,
            maxShift, minScore,
            d_shifts, d_scores);
    }

    if (cudaGetLastError() != cudaSuccess) goto cleanup;
    cudaDeviceSynchronize();

    cudaMemcpy(shifts_out, d_shifts, shiftBytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(scores_out, d_scores, scoreBytes, cudaMemcpyDeviceToHost);

    cudaFree(d_wave); cudaFree(d_tmpl); cudaFree(d_tmplE);
    cudaFree(d_shifts); cudaFree(d_scores);
    return 0;

cleanup:
    cudaFree(d_wave); cudaFree(d_tmpl); cudaFree(d_tmplE);
    cudaFree(d_shifts); cudaFree(d_scores);
    return -1;
}

} // extern "C"

#endif // USE_CUDA
