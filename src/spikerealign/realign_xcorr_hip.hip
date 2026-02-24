/***************************************************************************
 * realign_xcorr_hip.hip
 *
 * AMD ROCm/HIP port of the CUDA xcorr alignment kernel.
 * Logic is identical; only the API calls differ (hipMalloc etc.).
 ***************************************************************************/

#ifdef USE_HIP

#include "realign_xcorr.h"

#include <hip/hip_runtime.h>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>

// ---------------------------------------------------------------------------
// Kernels  (identical algorithm to the CUDA version)
// ---------------------------------------------------------------------------

__global__ void xcorr_hip_kernel(
    const int16_t* __restrict__ waveforms,
    const int16_t* __restrict__ tmpl,
    float* __restrict__ tmplEnergyBuf,
    int nChan, int nSamp,
    int maxShift, float minScore,
    int*   __restrict__ shifts_out,
    float* __restrict__ scores_out)
{
    const int sp  = blockIdx.x;
    const int lag = static_cast<int>(threadIdx.x) - maxShift;

    extern __shared__ float shmem[];
    const int nLags = 2 * maxShift + 1;
    float* sh_score = shmem;
    float* sh_lag   = shmem + nLags;

    sh_score[threadIdx.x] = -FLT_MAX;
    sh_lag  [threadIdx.x] = 0.0f;
    __syncthreads();

    const int16_t* spk = waveforms + static_cast<long long>(sp) * nChan * nSamp;
    const float sqrtTmpl = tmplEnergyBuf[0];
    const float sqrtSpk  = tmplEnergyBuf[1 + sp];

    // Circular xcorr: spike index wraps modulo nSamp.
    {
        double num = 0.0;
        for (int ch = 0; ch < nChan; ++ch) {
            const int16_t* tch = tmpl + ch * nSamp;
            const int16_t* sch = spk  + ch * nSamp;
            for (int s = 0; s < nSamp; ++s) {
                int sLag = ((s + lag) % nSamp + nSamp) % nSamp;
                num += static_cast<double>(tch[s]) *
                       static_cast<double>(sch[sLag]);
            }
        }
        double denom = static_cast<double>(sqrtTmpl) *
                       static_cast<double>(sqrtSpk);
        float score  = (denom > 1e-12) ? static_cast<float>(num / denom) : 0.0f;
        sh_score[threadIdx.x] = score;
        sh_lag  [threadIdx.x] = static_cast<float>(lag);
    }
    __syncthreads();

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
        shifts_out[sp] = (best >= minScore) ? -blag : 0;
        scores_out[sp] = best;
    }
}

__global__ void tmpl_energy_hip_kernel(
    const int16_t* __restrict__ tmpl,
    const int16_t* __restrict__ waveforms,
    int nChan, int nSamp, int nSpikes,
    float* __restrict__ out)
{
    int sp = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);

    if (sp == 0) {
        double e = 0.0;
        for (int ch = 0; ch < nChan; ++ch)
            for (int s = 0; s < nSamp; ++s) {
                double v = static_cast<double>(tmpl[ch * nSamp + s]);
                e += v * v;
            }
        out[0] = static_cast<float>(sqrt(e));
    }

    if (sp < nSpikes) {
        const int16_t* spk = waveforms + static_cast<long long>(sp) * nChan * nSamp;
        double e = 0.0;
        for (int ch = 0; ch < nChan; ++ch)
            for (int s = 0; s < nSamp; ++s) {
                double v = static_cast<double>(spk[ch * nSamp + s]);
                e += v * v;
            }
        out[1 + sp] = static_cast<float>(sqrt(e));
    }
}

// ---------------------------------------------------------------------------
// Host entry points
// ---------------------------------------------------------------------------

extern "C" {

int xcorr_hip_available()
{
    int n = 0;
    hipError_t err = hipGetDeviceCount(&n);
    return (err == hipSuccess && n > 0) ? 1 : 0;
}

int xcorr_hip_compute(
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

    int16_t *d_wave = nullptr, *d_tmpl = nullptr;
    float   *d_tmplE = nullptr, *d_scores = nullptr;
    int     *d_shifts = nullptr;

    if (hipMalloc(&d_wave,   waveBytes)    != hipSuccess) return -1;
    if (hipMalloc(&d_tmpl,   tmplBytes)    != hipSuccess) { hipFree(d_wave); return -1; }
    if (hipMalloc(&d_tmplE,  (1 + static_cast<size_t>(nSpikes)) * sizeof(float)) != hipSuccess) goto cleanup;
    if (hipMalloc(&d_shifts, shiftBytes)   != hipSuccess) goto cleanup;
    if (hipMalloc(&d_scores, scoreBytes)   != hipSuccess) goto cleanup;

    hipMemcpy(d_wave, waveforms, waveBytes,  hipMemcpyHostToDevice);
    hipMemcpy(d_tmpl, tmpl,      tmplBytes,  hipMemcpyHostToDevice);

    {
        int blk = 256;
        int grd = (nSpikes + blk - 1) / blk;
        hipLaunchKernelGGL(tmpl_energy_hip_kernel,
                           dim3(grd), dim3(blk), 0, 0,
                           d_tmpl, d_wave, nChannels, nSamples, nSpikes, d_tmplE);
    }

    {
        const int nLags = 2 * maxShift + 1;
        int blockSz = 1;
        while (blockSz < nLags) blockSz <<= 1;
        if (blockSz > 1024) {
            fprintf(stderr, "[xcorr_hip] maxShift too large\n"); goto cleanup;
        }
        size_t shmBytes = 2 * static_cast<size_t>(blockSz) * sizeof(float);
        hipLaunchKernelGGL(xcorr_hip_kernel,
                           dim3(nSpikes), dim3(blockSz), shmBytes, 0,
                           d_wave, d_tmpl, d_tmplE,
                           nChannels, nSamples,
                           maxShift, minScore,
                           d_shifts, d_scores);
    }

    if (hipGetLastError() != hipSuccess) goto cleanup;
    hipDeviceSynchronize();

    hipMemcpy(shifts_out, d_shifts, shiftBytes, hipMemcpyDeviceToHost);
    hipMemcpy(scores_out, d_scores, scoreBytes, hipMemcpyDeviceToHost);

    hipFree(d_wave); hipFree(d_tmpl); hipFree(d_tmplE);
    hipFree(d_shifts); hipFree(d_scores);
    return 0;

cleanup:
    hipFree(d_wave); hipFree(d_tmpl); hipFree(d_tmplE);
    hipFree(d_shifts); hipFree(d_scores);
    return -1;
}

} // extern "C"

#endif // USE_HIP
