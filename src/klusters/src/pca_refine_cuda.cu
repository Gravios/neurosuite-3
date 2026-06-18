/***************************************************************************
 * pca_refine_cuda.cu
 *
 * CUDA (NVIDIA) backend for the PCA-projection-energy refine pass.  Mirrors
 * the inner loop in KlustersDoc::realignSpikes (under `if (pcaRefine &&
 * pca.valid())`) but evaluates all M candidate shifts in parallel and folds
 * the per-spike search into a single block.
 *
 * Supported architectures (set via CUDA_ARCHITECTURES in CMakeLists):
 *   75  — Turing   (RTX 2000 series)
 *   86  — Ampere   (RTX 3000 series)
 *   89  — Ada      (RTX 4000 series)
 *   120 — Blackwell (RTX 5000 series, e.g. RTX 5070 Ti)
 *
 * Per-spike block layout
 * ----------------------
 *   One block of BLOCK_SIZE threads per spike.  Threads cooperate across
 *   candidates and channels:
 *
 *     for each candidate c in [0, M):
 *         phase 1 — stderiv pass (only if useStder)
 *                   For t = 0..rShift+d2u-1:
 *                     - block-wide sum across nChan channels at time t
 *                     - each of chForPca threads writes its stderived
 *                       sample into shared memory
 *         phase 2 — PCA projection energy
 *                   Threads parallelise across (chForPca * kComp).
 *                   For each (ch, k) pair compute the d2u-element dot
 *                   product, square it, atomicAdd into energy[c].
 *
 *   After all candidates: argmax over energy[0..M-1] via shared-memory
 *   reduction.  Block writes bestShifts[spike] = bestC - maxShift.
 *
 * One spike per block keeps inter-block synchronisation off the critical
 * path; for K spikes the kernel launches K blocks and each block is
 * independent.
 ***************************************************************************/

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <vector>

#include "pca_refine_gpu.h"

#define BLOCK_SIZE 256

// ---------------------------------------------------------------------------
// Block-wide sum reduction across nChan integer values.
// Assumes BLOCK_SIZE is a power of two and nChan ≤ BLOCK_SIZE.
// Returns the sum to all threads in s_red[0].
// ---------------------------------------------------------------------------
__device__ inline int blockSumInt(int* __restrict__ s_red, int v, int N_active)
{
    const int tid = threadIdx.x;
    s_red[tid] = (tid < N_active) ? v : 0;
    __syncthreads();
    for (int stride = BLOCK_SIZE / 2; stride > 0; stride >>= 1) {
        if (tid < stride) s_red[tid] += s_red[tid + stride];
        __syncthreads();
    }
    return s_red[0];
}

// ---------------------------------------------------------------------------
// Main kernel — one block per spike.
//
// Dynamic shared memory layout (allocated at launch):
//   [0]                                 float    energy[M]
//   [M*4]                               int16    stderivedRow[chForPca * (rShift+d2u)]
//   [M*4 + chFP*(rShift+d2u)*2]         int16    sdPrev[chForPca]                (only useStder)
//   [..]                                int      s_red[BLOCK_SIZE]               (for sums + reductions)
// All offsets aligned to 16 bytes via padding lambdas — see launcher.
// ---------------------------------------------------------------------------
__global__ void pca_refine_kernel(
    const int16_t* __restrict__ rawWindowsCM,    // [K, nChan, wideLen]
    const float*   __restrict__ pcaEvec,         // [chForPca * kComp * d2u]
    const float*   __restrict__ pcaMeans,        // [chForPca * d2u]
    int*           __restrict__ bestShifts,      // [K]
    int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
    int kComp, int d2u, int rShift, int maxShift,
    int useStder, int centered)
{
    const int spike = blockIdx.x;
    if (spike >= K) return;
    const int tid = threadIdx.x;

    extern __shared__ unsigned char shared_buf[];
    float*   energyPerCand = reinterpret_cast<float*>(shared_buf);                                // [M]
    int16_t* stderivedRow  = reinterpret_cast<int16_t*>(energyPerCand + M);                       // [chForPca * (rShift+d2u)]
    int16_t* sdPrev        = stderivedRow + (long long)chForPca * (rShift + d2u);                 // [chForPca]
    int*     s_red         = reinterpret_cast<int*>(sdPrev + chForPca);                           // [BLOCK_SIZE]

    const long long spikeBase = (long long)spike * nChan * wideLen;
    const int neededSamples   = rShift + d2u;

    // Initialise energy[c] = 0.
    for (int c = tid; c < M; c += BLOCK_SIZE) energyPerCand[c] = 0.0f;
    __syncthreads();

    // ─── For each candidate, compute the projection energy ────────────
    for (int c = 0; c < M; ++c) {
        // Reset sdPrev for this candidate (stderiv state is per-window).
        if (useStder) {
            if (tid < chForPca) sdPrev[tid] = 0;
            __syncthreads();
        }

        // Phase 1 — assemble the stderived (or raw) samples used by PCA
        // into shared memory at stderivedRow[ch * neededSamples + t].
        for (int t = 0; t < neededSamples; ++t) {
            // Each thread loads its channel's raw sample at (c + t).
            // tid < nChan covers all channels for the sum across nChan;
            // tid < chForPca covers the channels stored for PCA.
            int rawV = 0;
            if (tid < nChan) {
                const long long off = spikeBase
                                    + (long long)tid * wideLen
                                    + c + t;
                rawV = (int)rawWindowsCM[off];
            }

            if (useStder) {
                // Block-wide sum across all nChan channels.
                const int totalSum = blockSumInt(s_red, rawV, nChan);
                if (tid < chForPca) {
                    const int sd = nChan * rawV - totalSum;
                    const int sdCl = sd > 32767 ? 32767 : (sd < -32768 ? -32768 : sd);
                    const int prev = (int)sdPrev[tid];
                    int diff = sdCl - prev;
                    if (diff > 32767)  diff = 32767;
                    if (diff < -32768) diff = -32768;
                    sdPrev[tid] = (int16_t)sdCl;
                    stderivedRow[(long long)tid * neededSamples + t] = (int16_t)diff;
                }
            } else {
                if (tid < chForPca) {
                    stderivedRow[(long long)tid * neededSamples + t] = (int16_t)rawV;
                }
            }
            __syncthreads();
        }

        // Phase 2 — PCA projection energy.  Each (ch, k) thread computes
        // a single d2u-element dot product; chForPca * kComp pairs total.
        // For chForPca=8 / kComp=3 that's 24 threads doing real work and
        // BLOCK_SIZE-24 idle, which is fine — block latency is dominated
        // by Phase 1 above.  Output goes via shared atomicAdd into
        // energyPerCand[c].
        const int totalPairs = chForPca * kComp;
        for (int idx = tid; idx < totalPairs; idx += BLOCK_SIZE) {
            const int ch = idx / kComp;
            const int k  = idx % kComp;

            const int16_t* row = stderivedRow + (long long)ch * neededSamples + rShift;
            const float* ev    = pcaEvec  + (long long)ch * kComp * d2u + (long long)k * d2u;
            const float* mu    = pcaMeans + (long long)ch * d2u;

            float score = 0.0f;
            for (int u = 0; u < d2u; ++u) {
                float x = (float)row[u];
                if (centered) x -= mu[u];
                score += ev[u] * x;
            }
            atomicAdd(&energyPerCand[c], score * score);
        }
        __syncthreads();
    }

    // ─── Argmax over candidates ───────────────────────────────────────
    // s_red is float-sized below — we re-use the same buffer as a float
    // reduction by carrying parallel (value, index) tuples.
    float* s_bestSim = reinterpret_cast<float*>(s_red);
    int*   s_bestC   = reinterpret_cast<int*>(s_bestSim + BLOCK_SIZE);

    float lb = -INFINITY;
    int   li = -1;
    for (int c = tid; c < M; c += BLOCK_SIZE) {
        const float e = energyPerCand[c];
        if (e > lb) { lb = e; li = c; }
    }
    s_bestSim[tid] = lb;
    s_bestC  [tid] = li;
    __syncthreads();
    for (int stride = BLOCK_SIZE / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            if (s_bestSim[tid + stride] > s_bestSim[tid]) {
                s_bestSim[tid] = s_bestSim[tid + stride];
                s_bestC  [tid] = s_bestC  [tid + stride];
            }
        }
        __syncthreads();
    }

    if (tid == 0) {
        const int bestC = s_bestC[0];
        // bestC may be -1 only if M == 0 (impossible — caller asserts).
        bestShifts[spike] = bestC - maxShift;
    }
}

// ---------------------------------------------------------------------------
// Public C entry points
// ---------------------------------------------------------------------------

extern "C" int cuda_pca_refine_available()
{
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    if (err != cudaSuccess) { cudaGetLastError(); return 0; }
    return (count > 0) ? 1 : 0;
}

extern "C" int cuda_pca_refine(
    int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
    int kComp, int d2u, int rShift, int maxShift,
    int centered, int useStder,
    const int16_t* rawWindowsCM,
    const float*   pcaEvec,
    const float*   pcaMeans,
    int*           bestShifts)
{
    if (K <= 0 || M <= 0 || nChan <= 0 || chForPca <= 0 || kComp <= 0 || d2u <= 0)
        return -1;
    if (!rawWindowsCM || !pcaEvec || !bestShifts) return -1;
    if (centered && !pcaMeans) return -1;
    if (chForPca > nChan) chForPca = nChan;
    if (nChan > BLOCK_SIZE) return -2;   // single-block sum can't handle more

    const size_t raw_bytes   = (size_t)K * nChan * wideLen * sizeof(int16_t);
    const size_t evec_bytes  = (size_t)chForPca * kComp * d2u * sizeof(float);
    const size_t means_bytes = (size_t)chForPca * d2u * sizeof(float);
    const size_t best_bytes  = (size_t)K * sizeof(int);

    int16_t* d_raw   = nullptr;
    float*   d_evec  = nullptr;
    float*   d_means = nullptr;
    int*     d_best  = nullptr;

    auto cleanup = [&]() {
        if (d_raw)   cudaFree(d_raw);
        if (d_evec)  cudaFree(d_evec);
        if (d_means) cudaFree(d_means);
        if (d_best)  cudaFree(d_best);
    };

    cudaError_t err;
    err = cudaMalloc(&d_raw,   raw_bytes);     if (err != cudaSuccess) { cleanup(); return -3; }
    err = cudaMalloc(&d_evec,  evec_bytes);    if (err != cudaSuccess) { cleanup(); return -3; }
    if (centered) {
        err = cudaMalloc(&d_means, means_bytes); if (err != cudaSuccess) { cleanup(); return -3; }
    }
    err = cudaMalloc(&d_best,  best_bytes);    if (err != cudaSuccess) { cleanup(); return -3; }

    err = cudaMemcpy(d_raw,  rawWindowsCM, raw_bytes,  cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cleanup(); return -4; }
    err = cudaMemcpy(d_evec, pcaEvec,      evec_bytes, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { cleanup(); return -4; }
    if (centered) {
        err = cudaMemcpy(d_means, pcaMeans, means_bytes, cudaMemcpyHostToDevice);
        if (err != cudaSuccess) { cleanup(); return -4; }
    }

    // Shared-memory budget
    //   M floats     (energyPerCand)
    // + chForPca*(rShift+d2u) int16 (stderivedRow)
    // + chForPca int16 (sdPrev — only used when useStder, harmless otherwise)
    // + BLOCK_SIZE * (4 + 4) bytes (s_red, reused for argmax)
    const size_t shmem_bytes =
        (size_t)M * sizeof(float)
      + (size_t)chForPca * (rShift + d2u) * sizeof(int16_t)
      + (size_t)chForPca * sizeof(int16_t)
      + (size_t)BLOCK_SIZE * (sizeof(int) + sizeof(int));   // s_red + (bestSim,bestC) overlay

    // Padding for alignment (round up to 16 bytes).
    const size_t shmem_aligned = (shmem_bytes + 15) & ~size_t(15);

    // Dynamic shared memory above the 48 KB default opt-out ceiling must be
    // explicitly opted into per kernel, otherwise the launch is rejected with
    // cudaErrorInvalidValue.  Newer GPUs allow far more (e.g. Blackwell's
    // ~100+ KB), but only after raising this attribute.  Query the device's
    // hard opt-in maximum first: if the request exceeds even that, fall back to
    // the CPU path rather than attempting a launch that cannot succeed.
    {
        int dev = 0;
        if (cudaGetDevice(&dev) != cudaSuccess) { cudaGetLastError(); cleanup(); return -5; }
        int maxOptin = 0;
        cudaDeviceGetAttribute(&maxOptin,
                               cudaDevAttrMaxSharedMemoryPerBlockOptin, dev);
        if (maxOptin > 0 && shmem_aligned > static_cast<size_t>(maxOptin)) {
            cleanup();
            return -2;   // window too large for this GPU — caller uses CPU
        }
        if (shmem_aligned > (48u * 1024u)) {
            cudaError_t a = cudaFuncSetAttribute(
                pca_refine_kernel,
                cudaFuncAttributeMaxDynamicSharedMemorySize,
                static_cast<int>(shmem_aligned));
            if (a != cudaSuccess) {
                fprintf(stderr, "[klusters] pca_refine_cuda: cannot opt into %zu B "
                                "shared mem: %s\n",
                        shmem_aligned, cudaGetErrorString(a));
                cleanup();
                return -5;
            }
        }
    }

    pca_refine_kernel<<<K, BLOCK_SIZE, shmem_aligned>>>(
        d_raw, d_evec, centered ? d_means : nullptr, d_best,
        K, M, wideLen, nSamp, nChan, chForPca,
        kComp, d2u, rShift, maxShift,
        useStder, centered);

    err = cudaGetLastError();
    if (err != cudaSuccess) {
        fprintf(stderr, "[klusters] pca_refine_cuda launch: %s\n", cudaGetErrorString(err));
        cleanup();
        return -5;
    }
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) {
        fprintf(stderr, "[klusters] pca_refine_cuda sync: %s\n", cudaGetErrorString(err));
        cleanup();
        return -5;
    }

    err = cudaMemcpy(bestShifts, d_best, best_bytes, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) { cleanup(); return -6; }

    cleanup();
    return 0;
}
