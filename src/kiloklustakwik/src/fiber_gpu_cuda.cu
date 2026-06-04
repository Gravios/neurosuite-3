/***************************************************************************
 * fiber_gpu_cuda.cu
 *
 * CUDA backend for the standalone fiber clusterer.
 *
 * Kernel: in-band directional mean-shift.  One thread per seed; because the
 * support (dsup,rsup) is FIXED across iterations and seeds never read each
 * other, a thread runs all `iters` locally with no inter-thread sync.  The
 * per-thread arithmetic is identical (same order) to the CPU
 * fiberstage::meanshift_inband, audited bit-exact on the host
 * (max|ds|=max|rs|=0 vs CPU); CUDA's exp/sqrt may differ at the last ULP.
 *
 * Occupancy note: S is typically ~800, so one-thread-per-seed under-fills a
 * large GPU.  For higher occupancy switch to one block per seed with the
 * support loop strided across threads and a shared-memory reduction of
 * (acc[p], sw, sr); that changes the summation order (FP non-associative), so
 * re-audit at ~1e-12 rather than bit-exact.  Kept simple/correct here first.
 ***************************************************************************/
#ifdef USE_CUDA

#include "fiber_gpu.h"

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cmath>
#include <cstddef>

// --------------------------------------------------------------------------
// One thread == one seed.  Mirrors fiberstage::meanshift_inband exactly.
// --------------------------------------------------------------------------
__global__ void fiber_meanshift_kernel(
    const double* __restrict__ dsup, const double* __restrict__ rsup, int nsup, int p,
    double* __restrict__ ds, double* __restrict__ rs, int S,
    double kappa, double dr, int iters)
{
    const int s = blockIdx.x * blockDim.x + threadIdx.x;
    if (s >= S) return;

    double acc[FIBER_GPU_MAXP];
    double dloc[FIBER_GPU_MAXP];
    for (int k = 0; k < p; ++k) dloc[k] = ds[(size_t)s * p + k];
    double rloc = rs[s];

    for (int it = 0; it < iters; ++it) {
        for (int k = 0; k < p; ++k) acc[k] = 0.0;
        double sw = 0.0, sr = 0.0;
        for (int j = 0; j < nsup; ++j) {
            const double rj = rsup[j];
            if (fabs(rj - rloc) >= dr) continue;
            const double* dj = dsup + (size_t)j * p;
            double cs = 0.0;
            for (int k = 0; k < p; ++k) cs += dloc[k] * dj[k];
            const double w = exp(kappa * (cs - 1.0));
            sw += w; sr += w * rj;
            for (int k = 0; k < p; ++k) acc[k] += w * dj[k];
        }
        if (sw < 1e-9) sw = 1e-9;
        double nn = 0.0; for (int k = 0; k < p; ++k) nn += acc[k] * acc[k];
        nn = sqrt(nn) + 1e-12;
        for (int k = 0; k < p; ++k) dloc[k] = acc[k] / nn;
        rloc = sr / sw;
    }
    for (int k = 0; k < p; ++k) ds[(size_t)s * p + k] = dloc[k];
    rs[s] = rloc;
}

extern "C" int fiber_gpu_available(void)
{
    int n = 0;
    cudaError_t e = cudaGetDeviceCount(&n);
    return (e == cudaSuccess && n > 0) ? 1 : 0;
}

extern "C" int fiber_gpu_meanshift(
    const double* dsup, const double* rsup, int nsup, int p,
    double* ds, double* rs, int S,
    double kappa, double dr, int iters)
{
    if (p > FIBER_GPU_MAXP || p <= 0 || nsup <= 0 || S <= 0) return -1;

    double *d_dsup = nullptr, *d_rsup = nullptr, *d_ds = nullptr, *d_rs = nullptr;
    int rc = -1;
    const size_t bSup  = (size_t)nsup * p * sizeof(double);
    const size_t bRsup = (size_t)nsup     * sizeof(double);
    const size_t bDs   = (size_t)S * p    * sizeof(double);
    const size_t bRs   = (size_t)S        * sizeof(double);

    if (cudaMalloc(&d_dsup, bSup)  != cudaSuccess) return -1;
    if (cudaMalloc(&d_rsup, bRsup) != cudaSuccess) goto cleanup;
    if (cudaMalloc(&d_ds,   bDs)   != cudaSuccess) goto cleanup;
    if (cudaMalloc(&d_rs,   bRs)   != cudaSuccess) goto cleanup;

    cudaMemcpy(d_dsup, dsup, bSup,  cudaMemcpyHostToDevice);
    cudaMemcpy(d_rsup, rsup, bRsup, cudaMemcpyHostToDevice);
    cudaMemcpy(d_ds,   ds,   bDs,   cudaMemcpyHostToDevice);
    cudaMemcpy(d_rs,   rs,   bRs,   cudaMemcpyHostToDevice);

    {
        const int threads = 64;
        const int blocks  = (S + threads - 1) / threads;
        fiber_meanshift_kernel<<<blocks, threads>>>(
            d_dsup, d_rsup, nsup, p, d_ds, d_rs, S, kappa, dr, iters);
        if (cudaGetLastError()     != cudaSuccess) goto cleanup;
        if (cudaDeviceSynchronize()!= cudaSuccess) goto cleanup;
        cudaMemcpy(ds, d_ds, bDs, cudaMemcpyDeviceToHost);
        cudaMemcpy(rs, d_rs, bRs, cudaMemcpyDeviceToHost);
        rc = 0;
    }
cleanup:
    if (d_dsup) cudaFree(d_dsup);
    if (d_rsup) cudaFree(d_rsup);
    if (d_ds)   cudaFree(d_ds);
    if (d_rs)   cudaFree(d_rs);
    return rc;
}

#endif // USE_CUDA
