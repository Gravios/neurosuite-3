/***************************************************************************
 * reorder_similarity_cuda.cu
 *
 * CUDA (NVIDIA) backend for single-linkage agglomerative clustering on the
 * cluster similarity matrix.  Mirrors the CPU loop in
 * KlustersApp::slotReorderClustersBySimilarity but folds all N-1 merge
 * steps into a single kernel launch so per-step overhead is amortised:
 * for N=500 the CPU path is ~125 ms, this kernel runs in single-digit ms.
 *
 * Supported architectures (set via CUDA_ARCHITECTURES in CMakeLists):
 *   75  — Turing   (RTX 2000 series)
 *   86  — Ampere   (RTX 3000 series)
 *   89  — Ada      (RTX 4000 series)
 *   120 — Blackwell (RTX 5000 series, e.g. RTX 5070 Ti)
 *
 * Why a single block
 * ------------------
 * The state machine — alive[], merge bookkeeping — is shared across N-1
 * iterations and small (a few kB).  A single CUDA block of BLOCK_SIZE
 * threads can keep alive[] in shared memory, __syncthreads() between
 * phases, and write the merge log to global memory at the end of each
 * step.  Multi-block reductions would require a global barrier per step
 * (cooperative groups) which costs more than it saves at this scale.
 ***************************************************************************/

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <vector>

#include "reorder_similarity_gpu.h"

#define BLOCK_SIZE 256

// ---------------------------------------------------------------------------
// Kernel — single block does the entire merge loop.
//
// Per step:
//   Phase 1: each thread scans a stride of upper-triangular (i, j) pairs,
//            keeping a local (sim, i, j) best.  Block reduction collapses
//            to the global best (bi, bj).
//   Phase 2: each thread updates S[bi, k] for k in its stride.  bj is
//            marked dead.  The merge (bi, bj) is recorded for the host.
// ---------------------------------------------------------------------------
__global__ void single_linkage_kernel(double* __restrict__ S,
                                      int*    __restrict__ alive,
                                      int*    __restrict__ mergeBi,
                                      int*    __restrict__ mergeBj,
                                      int N)
{
    __shared__ double s_bestSim[BLOCK_SIZE];
    __shared__ int    s_bestI  [BLOCK_SIZE];
    __shared__ int    s_bestJ  [BLOCK_SIZE];
    __shared__ int    s_bi;
    __shared__ int    s_bj;
    __shared__ int    s_exitFlag;

    const int tid = threadIdx.x;

    // Total upper-triangular pair count.  Threads stride over this range
    // by BLOCK_SIZE.  Using i < j enforces upper-triangle and avoids
    // double-counting symmetric pairs.
    const long long totalPairs = (long long)N * (N - 1) / 2;

    for (int step = 0; step < N - 1; ++step) {
        // ── Phase 1: argmax over alive pairs ────────────────────────────
        double localBest = -INFINITY;
        int    localI    = -1;
        int    localJ    = -1;

        for (long long idx = tid; idx < totalPairs; idx += BLOCK_SIZE) {
            // Decode (i, j) from linear upper-triangle index.  The closed
            // form below is the standard inverse of i*(N-1) - i*(i+1)/2 + (j-i-1).
            //   i = N - 2 - floor( sqrt(8*(T - idx - 1) + 1) / 2 - 0.5 )
            //   j = idx + i + 1 - i*(2*N - i - 1) / 2
            // Implemented via doubles so it works for the largest expected N.
            double T = (double)totalPairs;
            double k = (double)idx;
            int i = (int)((double)(N - 2)
                          - floor((sqrt(8.0 * (T - k - 1.0) + 1.0) - 1.0) / 2.0));
            // Clamp against rounding (rare, only at extreme indices).
            if (i < 0) i = 0;
            if (i > N - 2) i = N - 2;
            int j = (int)(idx + i + 1 - (long long)i * (2 * N - i - 1) / 2);
            if (j <= i || j >= N) continue;

            if (!alive[i] || !alive[j]) continue;
            const double s = S[(long long)i * N + j];
            if (s > localBest) {
                localBest = s;
                localI    = i;
                localJ    = j;
            }
        }

        s_bestSim[tid] = localBest;
        s_bestI  [tid] = localI;
        s_bestJ  [tid] = localJ;
        __syncthreads();

        // Block reduction (BLOCK_SIZE assumed power of two).
        for (int stride = BLOCK_SIZE / 2; stride > 0; stride >>= 1) {
            if (tid < stride) {
                if (s_bestSim[tid + stride] > s_bestSim[tid]) {
                    s_bestSim[tid] = s_bestSim[tid + stride];
                    s_bestI  [tid] = s_bestI  [tid + stride];
                    s_bestJ  [tid] = s_bestJ  [tid + stride];
                }
            }
            __syncthreads();
        }

        if (tid == 0) {
            s_bi = s_bestI[0];
            s_bj = s_bestJ[0];
            s_exitFlag = (s_bi < 0 || s_bj < 0) ? 1 : 0;
            mergeBi[step] = s_bi;
            mergeBj[step] = s_bj;
        }
        __syncthreads();

        // Disconnected matrix — no alive pair found.  Mark remaining
        // steps as sentinels (-1) and exit.  Mirrors the CPU loop's
        // `if (bi < 0 || bj < 0) break;`.
        if (s_exitFlag) {
            if (tid == 0) {
                for (int rem = step + 1; rem < N - 1; ++rem) {
                    mergeBi[rem] = -1;
                    mergeBj[rem] = -1;
                }
            }
            return;
        }

        // ── Phase 2: single-linkage update of S row/column bi ───────────
        const int bi = s_bi;
        const int bj = s_bj;
        for (int k = tid; k < N; k += BLOCK_SIZE) {
            if (!alive[k] || k == bi || k == bj) continue;
            const long long bi_k = (long long)bi * N + k;
            const long long bj_k = (long long)bj * N + k;
            const long long k_bi = (long long)k  * N + bi;
            const double a = S[bi_k];
            const double b = S[bj_k];
            const double m = (a > b) ? a : b;
            S[bi_k] = m;
            S[k_bi] = m;
        }
        __syncthreads();

        if (tid == 0) alive[bj] = 0;
        __syncthreads();
    }
}

// ---------------------------------------------------------------------------
// Public C entry points
// ---------------------------------------------------------------------------

extern "C" int cuda_reorder_similarity_available()
{
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    if (err != cudaSuccess) {
        // Some installs leave a stale cuda* error in flight; clear it.
        cudaGetLastError();
        return 0;
    }
    return (count > 0) ? 1 : 0;
}

extern "C" int cuda_reorder_similarity(const double* S_host, int N,
                                       int* mergeBi_host, int* mergeBj_host)
{
    if (N < 2 || !S_host || !mergeBi_host || !mergeBj_host) return -1;

    const size_t S_bytes     = (size_t)N * N * sizeof(double);
    const size_t alive_bytes = (size_t)N * sizeof(int);
    const size_t merge_bytes = (size_t)(N - 1) * sizeof(int);

    double* d_S       = nullptr;
    int*    d_alive   = nullptr;
    int*    d_mergeBi = nullptr;
    int*    d_mergeBj = nullptr;

    auto cleanup = [&]() {
        if (d_S)       cudaFree(d_S);
        if (d_alive)   cudaFree(d_alive);
        if (d_mergeBi) cudaFree(d_mergeBi);
        if (d_mergeBj) cudaFree(d_mergeBj);
    };

    cudaError_t err;
    err = cudaMalloc(&d_S,       S_bytes);     if (err != cudaSuccess) { cleanup(); return -2; }
    err = cudaMalloc(&d_alive,   alive_bytes); if (err != cudaSuccess) { cleanup(); return -2; }
    err = cudaMalloc(&d_mergeBi, merge_bytes); if (err != cudaSuccess) { cleanup(); return -2; }
    err = cudaMalloc(&d_mergeBj, merge_bytes); if (err != cudaSuccess) { cleanup(); return -2; }

    err = cudaMemcpy(d_S, S_host, S_bytes, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cleanup(); return -3; }

    // alive[] initialised to 1 via a host buffer — cudaMemset would set
    // each byte to 1 (yielding 0x01010101), not each int to 1.
    std::vector<int> alive_init(N, 1);
    err = cudaMemcpy(d_alive, alive_init.data(), alive_bytes, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cleanup(); return -3; }

    single_linkage_kernel<<<1, BLOCK_SIZE>>>(d_S, d_alive, d_mergeBi, d_mergeBj, N);
    err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "[klusters] reorder_similarity_cuda launch: %s\n",
                cudaGetErrorString(err));
        cleanup();
        return -4;
    }
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "[klusters] reorder_similarity_cuda sync: %s\n",
                cudaGetErrorString(err));
        cleanup();
        return -4;
    }

    err = cudaMemcpy(mergeBi_host, d_mergeBi, merge_bytes, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) { cleanup(); return -5; }
    err = cudaMemcpy(mergeBj_host, d_mergeBj, merge_bytes, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) { cleanup(); return -5; }

    cleanup();
    return 0;
}
