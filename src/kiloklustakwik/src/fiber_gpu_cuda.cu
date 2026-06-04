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

// --------------------------------------------------------------------------
// One thread == one spike.  Mirrors the assignment loop in
// fiberstage::consolidate (predict via clamped lerp on the per-fiber grid,
// then whiteness residual + argmin).  Audited bit-exact on the host:
// 0/8000 hard-label disagreements and 0.0 calibrated-confidence difference
// vs the CPU path.  Empty/degenerate fibers (ng<=0) are guarded to +inf.
// --------------------------------------------------------------------------
__global__ void fiber_assign_kernel(
    const double* __restrict__ X, int N, int p,
    const double* __restrict__ grids, const int* __restrict__ gridLen, const int* __restrict__ gridOff,
    const double* __restrict__ Ds,    const int* __restrict__ DOff,    int nfib,
    double* __restrict__ res, int* __restrict__ hard)
{
    const int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= N) return;
    const double* Xi = X + (size_t)i * p;
    double rr = 0.0; for (int j = 0; j < p; ++j) rr += Xi[j] * Xi[j]; rr = sqrt(rr);
    double best = 1e300; int bk = 0; double pr[FIBER_GPU_MAXP];
    for (int f = 0; f < nfib; ++f) {
        const int ng = gridLen[f]; double s;
        if (ng <= 0) { s = 1e300; }
        else {
            const double* g  = grids + gridOff[f];
            const double* Dp = Ds    + DOff[f];
            if (rr <= g[0]) {
                for (int j = 0; j < p; ++j) pr[j] = Dp[j];
            } else if (rr >= g[ng - 1]) {
                for (int j = 0; j < p; ++j) pr[j] = Dp[(size_t)(ng - 1) * p + j];
            } else {
                int lo = 0, hi = ng;                       // lower_bound: first g[jj] >= rr
                while (lo < hi) { int mm = (lo + hi) / 2; if (g[mm] < rr) lo = mm + 1; else hi = mm; }
                const int jj = lo;
                const double fr = (rr - g[jj - 1]) / (g[jj] - g[jj - 1]); double nn = 0.0;
                for (int j = 0; j < p; ++j) {
                    pr[j] = Dp[(size_t)(jj - 1) * p + j]
                          + (Dp[(size_t)jj * p + j] - Dp[(size_t)(jj - 1) * p + j]) * fr;
                    nn += pr[j] * pr[j];
                }
                nn = sqrt(nn); for (int j = 0; j < p; ++j) pr[j] /= nn;
            }
            s = 0.0; for (int j = 0; j < p; ++j) { double dd = Xi[j] - rr * pr[j]; s += dd * dd; }
            s = sqrt(s);
        }
        res[(size_t)i * nfib + f] = s;
        if (s < best) { best = s; bk = f; }
    }
    hard[i] = bk;
}

extern "C" int fiber_gpu_assign(
    const double* X, int N, int p,
    const double* grids, const int* gridLen, const int* gridOff,
    const double* Ds, const int* DOff, int nfib,
    double* res, int* hard)
{
    if (p > FIBER_GPU_MAXP || p <= 0 || N <= 0 || nfib <= 0) return -1;
    long totG = 0, totD = 0;                                // flattened sizes from offset/len tables
    for (int k = 0; k < nfib; ++k) {
        long e  = (long)gridOff[k] + gridLen[k];            if (e  > totG) totG = e;
        long ed = (long)DOff[k]    + (long)gridLen[k] * p;  if (ed > totD) totD = ed;
    }
    if (totG <= 0 || totD <= 0) return -1;

    double *dX=nullptr,*dG=nullptr,*dD=nullptr,*dRes=nullptr;
    int    *dGL=nullptr,*dGO=nullptr,*dDO=nullptr,*dHard=nullptr;
    int rc = -1;
    const size_t bX=(size_t)N*p*sizeof(double), bG=(size_t)totG*sizeof(double), bD=(size_t)totD*sizeof(double);
    const size_t bRes=(size_t)N*nfib*sizeof(double), bHard=(size_t)N*sizeof(int), bT=(size_t)nfib*sizeof(int);

    if (cudaMalloc(&dX,  bX)   != cudaSuccess) return -1;
    if (cudaMalloc(&dG,  bG)   != cudaSuccess) goto acleanup;
    if (cudaMalloc(&dD,  bD)   != cudaSuccess) goto acleanup;
    if (cudaMalloc(&dGL, bT)   != cudaSuccess) goto acleanup;
    if (cudaMalloc(&dGO, bT)   != cudaSuccess) goto acleanup;
    if (cudaMalloc(&dDO, bT)   != cudaSuccess) goto acleanup;
    if (cudaMalloc(&dRes,bRes) != cudaSuccess) goto acleanup;
    if (cudaMalloc(&dHard,bHard)!= cudaSuccess) goto acleanup;

    cudaMemcpy(dX,  X,       bX, cudaMemcpyHostToDevice);
    cudaMemcpy(dG,  grids,   bG, cudaMemcpyHostToDevice);
    cudaMemcpy(dD,  Ds,      bD, cudaMemcpyHostToDevice);
    cudaMemcpy(dGL, gridLen, bT, cudaMemcpyHostToDevice);
    cudaMemcpy(dGO, gridOff, bT, cudaMemcpyHostToDevice);
    cudaMemcpy(dDO, DOff,    bT, cudaMemcpyHostToDevice);
    {
        const int threads = 128;
        const int blocks  = (N + threads - 1) / threads;
        fiber_assign_kernel<<<blocks, threads>>>(dX, N, p, dG, dGL, dGO, dD, dDO, nfib, dRes, dHard);
        if (cudaGetLastError()      != cudaSuccess) goto acleanup;
        if (cudaDeviceSynchronize() != cudaSuccess) goto acleanup;
        cudaMemcpy(res,  dRes,  bRes,  cudaMemcpyDeviceToHost);
        cudaMemcpy(hard, dHard, bHard, cudaMemcpyDeviceToHost);
        rc = 0;
    }
acleanup:
    if (dX)   cudaFree(dX);
    if (dG)   cudaFree(dG);
    if (dD)   cudaFree(dD);
    if (dGL)  cudaFree(dGL);
    if (dGO)  cudaFree(dGO);
    if (dDO)  cudaFree(dDO);
    if (dRes) cudaFree(dRes);
    if (dHard)cudaFree(dHard);
    return rc;
}

#endif // USE_CUDA
