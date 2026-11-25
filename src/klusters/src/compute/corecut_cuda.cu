/***************************************************************************
 * corecut_cuda.cu
 *
 * CUDA (NVIDIA) backend for the ordering loop of the core cut (corecut.h,
 * step 4; interface in corecut_gpu.h).
 *
 * On the device: the spikes x (column-major, so that neighbouring threads read
 * neighbouring addresses), |x_i|^2, the running products x_i . m in double,
 * and a taken flag per spike.
 *
 * One launch per spike taken.  Thread i:
 *     dot    = x_i . x_j                       (j: the spike taken last)
 *     xm_i  += (dot - xm_i) / k                (m moved by (x_j - m) / k)
 *     key_i  = |x_i|^2 - 2 xm_i                (|x_i - m|^2 less the common m.m)
 * and each block reduces its keys to the smallest among the spikes left (ties
 * to the lower index).  The per-block minima -- a few hundred pairs -- are
 * copied back and the host takes their minimum; that copy is the launch's
 * synchronisation.  The host keeps m itself (d doubles) for the step size.
 *
 * Cost: n launches of n * d multiply-adds.  40,000 spikes of 152 values is
 * 2.4e11 on the device and 40,000 round trips; the round trips dominate.
 ***************************************************************************/

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <vector>

#include "corecut_gpu.h"

#define CC_BLOCK 256

__global__ void corecut_step_kernel(
    const float* __restrict__ x,          // [d x n] column-major: x[u * n + i]
    const double* __restrict__ x2,        // [n]
    double* __restrict__ xm,              // [n]
    unsigned char* __restrict__ taken,    // [n]
    double* __restrict__ outKey,          // [blocks]
    int* __restrict__ outIdx,             // [blocks]
    int n, int d, int j, double inv)
{
    __shared__ double s_key[CC_BLOCK];
    __shared__ int    s_idx[CC_BLOCK];

    const int tid = threadIdx.x;
    const int i   = blockIdx.x * CC_BLOCK + tid;

    double key = INFINITY;
    int    idx = -1;
    if (i < n) {
        float dot = 0.0f;
        for (int u = 0; u < d; ++u)
            dot += x[(size_t)u * n + i] * x[(size_t)u * n + j];
        double v = xm[i];
        v += ((double)dot - v) * inv;
        xm[i] = v;
        if (i == j) taken[i] = 1;
        if (!taken[i]) { key = x2[i] - 2.0 * v; idx = i; }
    }
    s_key[tid] = key;
    s_idx[tid] = idx;
    __syncthreads();
    for (int stride = CC_BLOCK / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            const double ko = s_key[tid + stride];
            const int    io = s_idx[tid + stride];
            // smaller key wins; equal keys: the lower index (io >= 0 guards the empty slot)
            if (io >= 0 && (s_idx[tid] < 0 || ko < s_key[tid] || (ko == s_key[tid] && io < s_idx[tid]))) {
                s_key[tid] = ko;
                s_idx[tid] = io;
            }
        }
        __syncthreads();
    }
    if (tid == 0) {
        outKey[blockIdx.x] = s_key[0];
        outIdx[blockIdx.x] = s_idx[0];
    }
}

extern "C" int cuda_corecut_available()
{
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    if (err != cudaSuccess) { cudaGetLastError(); return 0; }
    return (count > 0) ? 1 : 0;
}

extern "C" int cuda_corecut_order(long n, int d, const float* x, long seed, long* order, double* step)
{
    if (n <= 1 || d <= 0 || !x || !order || !step || seed < 0 || seed >= n) return -1;
    if (n > 0x7fffffffL / 2) return -2;                    // the kernel indexes spikes with int

    const int    nI     = (int)n;
    const int    blocks = (nI + CC_BLOCK - 1) / CC_BLOCK;
    const size_t xBytes = (size_t)n * d * sizeof(float);

    // Host: |x_i|^2 and the column-major copy the device reads.
    std::vector<double> hx2((size_t)n);
    std::vector<float>  hxt((size_t)n * d);
    for (long i = 0; i < n; ++i) {
        const float* xi = x + (size_t)i * d;
        double s = 0.0;
        for (int u = 0; u < d; ++u) {
            s += (double)xi[u] * xi[u];
            hxt[(size_t)u * n + i] = xi[u];
        }
        hx2[(size_t)i] = s;
    }

    float*         d_x     = nullptr;
    double*        d_x2    = nullptr;
    double*        d_xm    = nullptr;
    unsigned char* d_taken = nullptr;
    double*        d_bkey  = nullptr;
    int*           d_bidx  = nullptr;
    auto cleanup = [&]() {
        if (d_x)     cudaFree(d_x);
        if (d_x2)    cudaFree(d_x2);
        if (d_xm)    cudaFree(d_xm);
        if (d_taken) cudaFree(d_taken);
        if (d_bkey)  cudaFree(d_bkey);
        if (d_bidx)  cudaFree(d_bidx);
    };

    cudaError_t err;
    err = cudaMalloc(&d_x,     xBytes);                           if (err != cudaSuccess) { cleanup(); return -3; }
    err = cudaMalloc(&d_x2,    (size_t)n * sizeof(double));       if (err != cudaSuccess) { cleanup(); return -3; }
    err = cudaMalloc(&d_xm,    (size_t)n * sizeof(double));       if (err != cudaSuccess) { cleanup(); return -3; }
    err = cudaMalloc(&d_taken, (size_t)n);                        if (err != cudaSuccess) { cleanup(); return -3; }
    err = cudaMalloc(&d_bkey,  (size_t)blocks * sizeof(double));  if (err != cudaSuccess) { cleanup(); return -3; }
    err = cudaMalloc(&d_bidx,  (size_t)blocks * sizeof(int));     if (err != cudaSuccess) { cleanup(); return -3; }

    err = cudaMemcpy(d_x,  hxt.data(), xBytes, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cleanup(); return -4; }
    err = cudaMemcpy(d_x2, hx2.data(), (size_t)n * sizeof(double), cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cleanup(); return -4; }
    err = cudaMemset(d_xm, 0, (size_t)n * sizeof(double));
    if (err != cudaSuccess) { cleanup(); return -4; }
    err = cudaMemset(d_taken, 0, (size_t)n);
    if (err != cudaSuccess) { cleanup(); return -4; }
    hxt.clear();
    hxt.shrink_to_fit();

    std::vector<double> m((size_t)d);
    for (int u = 0; u < d; ++u) m[(size_t)u] = x[(size_t)seed * d + u];
    std::vector<double> hkey((size_t)blocks);
    std::vector<int>    hidx((size_t)blocks);

    order[0] = seed;
    step[0]  = 0.0;
    long j = seed;
    for (long k = 1; k < n; ++k) {
        const double inv = 1.0 / (double)k;
        corecut_step_kernel<<<blocks, CC_BLOCK>>>(d_x, d_x2, d_xm, d_taken, d_bkey, d_bidx, nI, d, (int)j, inv);
        err = cudaGetLastError();
        if (err != cudaSuccess) {
            fprintf(stderr, "[klusters] corecut_cuda launch: %s\n", cudaGetErrorString(err));
            cleanup();
            return -5;
        }
        // The copies wait for the kernel: no separate synchronise.
        err = cudaMemcpy(hkey.data(), d_bkey, (size_t)blocks * sizeof(double), cudaMemcpyDeviceToHost);
        if (err != cudaSuccess) { fprintf(stderr, "[klusters] corecut_cuda step: %s\n", cudaGetErrorString(err)); cleanup(); return -6; }
        err = cudaMemcpy(hidx.data(), d_bidx, (size_t)blocks * sizeof(int), cudaMemcpyDeviceToHost);
        if (err != cudaSuccess) { cleanup(); return -6; }

        long   best = -1;
        double bestKey = 0.0;
        for (int b = 0; b < blocks; ++b) {
            const int ib = hidx[(size_t)b];
            if (ib < 0) continue;
            if (best < 0 || hkey[(size_t)b] < bestKey) { best = ib; bestKey = hkey[(size_t)b]; }   // blocks ascend: ties keep the lower index
        }
        if (best < 0) { cleanup(); return -7; }

        // m with k spikes in it (j, taken last, now added), for the step size.
        if (k > 1) {
            const float* xj = x + (size_t)j * d;
            for (int u = 0; u < d; ++u) m[(size_t)u] += ((double)xj[u] - m[(size_t)u]) * inv;
        }
        double mm = 0.0;
        for (int u = 0; u < d; ++u) mm += m[(size_t)u] * m[(size_t)u];
        const double d2 = bestKey + mm;
        order[k] = best;
        step[k]  = sqrt(d2 > 0.0 ? d2 : 0.0);
        j = best;
    }

    cleanup();
    return 0;
}
