/***************************************************************************
 * realign_xcorr_cuda.cu
 *
 * CUDA implementation of normalised cross-correlation spike alignment.
 *
 * Normalization: both tmplEnergy and spkEnergy use the full waveform
 * (all samples, all channels) so the denominator is constant across lags.
 * This prevents the bias toward non-zero lags that occurs when spkEnergy
 * is computed only over the overlapping window at each lag.
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
// Kernel: precompute sqrt(energy) for template (1 thread) and each spike.
// One block, one thread per spike (up to 1024 spikes per call chunk).
// For simplicity we use a separate 1-thread kernel for the template.
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

__global__ void spike_energy_kernel(
    const int16_t* __restrict__ waveforms,
    int nChan, int nSamp, int nSpikes,
    float* __restrict__ spkSqrtEnergy)  // [nSpikes]
{
    const int sp = blockIdx.x * blockDim.x + threadIdx.x;
    if (sp >= nSpikes) return;
    const int16_t* spk = waveforms + static_cast<long long>(sp) * nChan * nSamp;
    double e = 0.0;
    for (int ch = 0; ch < nChan; ++ch)
        for (int s = 0; s < nSamp; ++s) {
            double v = static_cast<double>(spk[ch * nSamp + s]);
            e += v * v;
        }
    spkSqrtEnergy[sp] = static_cast<float>(sqrt(e));
}

// ---------------------------------------------------------------------------
// Kernel: one block per spike, one thread per lag candidate.
// spkSqrtEnergy[sp] is precomputed and constant across lags.
// ---------------------------------------------------------------------------

__global__ void xcorr_kernel(
    const int16_t* __restrict__ waveforms,
    const int16_t* __restrict__ tmpl,
    const float*   __restrict__ tmplEnergyBuf,   // [1]: sqrt(tmplEnergy)
    const float*   __restrict__ spkSqrtEnergy,   // [nSpikes]
    int nChan, int nSamp,
    int maxShift, float minScore,
    int*   __restrict__ shifts_out,
    float* __restrict__ scores_out)
{
    const int sp  = blockIdx.x;
    const int lag = static_cast<int>(threadIdx.x) - maxShift;

    extern __shared__ float shmem[];
    const int nLags = 2 * maxShift + 1;
    // The launch rounds the block up to the next power of two (blockDim.x) and
    // allocates 2*blockDim.x floats.  sh_lag MUST be based at blockDim.x, not
    // nLags: nLags is always odd so nLags < blockDim.x, and basing sh_lag at
    // nLags makes its head alias the tail of sh_score (cells [nLags,blockDim.x)
    // are written as both a score and a lag), which corrupts the recovered lag
    // for the low-index (negative) candidates.
    float* sh_score = shmem;
    float* sh_lag   = shmem + blockDim.x;

    sh_score[threadIdx.x] = -FLT_MAX;
    sh_lag  [threadIdx.x] = 0.0f;
    __syncthreads();

    // Only the nLags real lag candidates compute and store; padding lanes keep
    // their -FLT_MAX init so they never win the reduction (and do no work).
    if (static_cast<int>(threadIdx.x) < nLags) {
        const int16_t* spk = waveforms + static_cast<long long>(sp) * nChan * nSamp;
        const double denom = static_cast<double>(tmplEnergyBuf[0])
                           * static_cast<double>(spkSqrtEnergy[sp]);
        double num = 0.0;
        // Circular shift — all samples contribute at every lag.
        for (int ch = 0; ch < nChan; ++ch) {
            const int16_t* tch = tmpl + ch * nSamp;
            const int16_t* sch = spk  + ch * nSamp;
            for (int s = 0; s < nSamp; ++s) {
                int sLag = (s + lag + nSamp) % nSamp;
                num += static_cast<double>(tch[s])
                     * static_cast<double>(sch[sLag]);
            }
        }
        float score = (denom > 1e-12) ? static_cast<float>(num / denom) : 0.0f;
        sh_score[threadIdx.x] = score;
        sh_lag  [threadIdx.x] = static_cast<float>(lag);
    }
    __syncthreads();

    // Parallel reduction over the full power-of-two block.  Starting the stride
    // at nLags/2 (with nLags odd) silently drops the top candidate, lag
    // +maxShift; reducing over blockDim.x with the padding lanes pinned at
    // -FLT_MAX covers every real candidate.  A strict '>' keeps the lower index
    // on ties, i.e. the more-negative lag, matching the OMP/SYCL backends.
    for (int stride = blockDim.x / 2; stride >= 1; stride >>= 1) {
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
        // +bestLag: spike peak is at peakSamp0+bestLag, so ts is bestLag too early;
        // caller does newTs = ts + shifts_out to correct.
        shifts_out[sp] = (best >= minScore) ? blag : 0;
        scores_out[sp] = best;
    }
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
    const size_t waveBytes  = static_cast<size_t>(nSpikes) * nChannels * nSamples * sizeof(int16_t);
    const size_t tmplBytes  = static_cast<size_t>(nChannels) * nSamples * sizeof(int16_t);
    const size_t shiftBytes = static_cast<size_t>(nSpikes) * sizeof(int);
    const size_t scoreBytes = static_cast<size_t>(nSpikes) * sizeof(float);
    const size_t spkEBytes  = static_cast<size_t>(nSpikes) * sizeof(float);

    int16_t *d_wave = nullptr, *d_tmpl = nullptr;
    float   *d_tmplE = nullptr, *d_spkE = nullptr;
    float   *d_scores = nullptr;
    int     *d_shifts = nullptr;

    if (cudaMalloc(&d_wave,   waveBytes)    != cudaSuccess) return -1;
    if (cudaMalloc(&d_tmpl,   tmplBytes)    != cudaSuccess) { cudaFree(d_wave); return -1; }
    if (cudaMalloc(&d_tmplE,  sizeof(float)) != cudaSuccess) goto cleanup;
    if (cudaMalloc(&d_spkE,   spkEBytes)    != cudaSuccess) goto cleanup;
    if (cudaMalloc(&d_shifts, shiftBytes)   != cudaSuccess) goto cleanup;
    if (cudaMalloc(&d_scores, scoreBytes)   != cudaSuccess) goto cleanup;

    cudaMemcpy(d_wave, waveforms, waveBytes, cudaMemcpyHostToDevice);
    cudaMemcpy(d_tmpl, tmpl,      tmplBytes, cudaMemcpyHostToDevice);

    // Precompute energies
    tmpl_energy_kernel<<<1, 1>>>(d_tmpl, nChannels, nSamples, d_tmplE);
    {
        const int blk = 128;
        spike_energy_kernel<<<(nSpikes + blk - 1) / blk, blk>>>(
            d_wave, nChannels, nSamples, nSpikes, d_spkE);
    }

    {
        const int nLags = 2 * maxShift + 1;
        int blockSz = 1;
        while (blockSz < nLags) blockSz <<= 1;
        if (blockSz > 1024) {
            fprintf(stderr, "[xcorr_cuda] maxShift too large for single-block reduction\n");
            goto cleanup;
        }
        size_t shmBytes = 2 * static_cast<size_t>(blockSz) * sizeof(float);
        xcorr_kernel<<<nSpikes, blockSz, shmBytes>>>(
            d_wave, d_tmpl, d_tmplE, d_spkE,
            nChannels, nSamples,
            maxShift, minScore,
            d_shifts, d_scores);
    }

    if (cudaGetLastError() != cudaSuccess) goto cleanup;
    cudaDeviceSynchronize();

    cudaMemcpy(shifts_out, d_shifts, shiftBytes, cudaMemcpyDeviceToHost);
    cudaMemcpy(scores_out, d_scores, scoreBytes, cudaMemcpyDeviceToHost);

    cudaFree(d_wave); cudaFree(d_tmpl); cudaFree(d_tmplE); cudaFree(d_spkE);
    cudaFree(d_shifts); cudaFree(d_scores);
    return 0;

cleanup:
    cudaFree(d_wave); cudaFree(d_tmpl); cudaFree(d_tmplE); cudaFree(d_spkE);
    cudaFree(d_shifts); cudaFree(d_scores);
    return -1;
}

} // extern "C"

#endif // USE_CUDA
