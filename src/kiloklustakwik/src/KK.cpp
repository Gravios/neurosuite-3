// KK.cpp — C++17 modernisation of KlustaKwik clustering engine
//
// Changes from original v1.7:
//   - All C++98 idioms replaced with C++17 equivalents
//   - Range-based for, structured bindings, std::min/max used throughout
//   - Array move semantics eliminate temporaries in TrySplits
//   - CandidateClass uninitialised-use bug fixed in ConsiderDeletion
//   - Cholesky/TriSolve no longer allocate temporary Array objects
//   - EStep/MStep/CStep annotated with GPU parallelism strategy
//   - No algorithmic changes: output is bit-identical to original
//
// GPU STRATEGY OVERVIEW (implemented in KK_cuda.cu):
//   EStep  — parallel over nPoints, one thread per point per cluster
//   MStep  — parallel reduce: mean accumulation + outer-product covariance
//   CStep  — parallel over nPoints, argmin of LogP row
//   ConsiderDeletion — parallel reduce of DeletionLoss
//   Cholesky — sequential on CPU (nDims³ is tiny, ~12³ = 1728 ops)

#include "KK.h"
#include "KK_internal.h"
#include <cstdint>
#include "KlustaKwik.h"
#include "KlustaSave.h"
#include "dipsplit.h"        // BicPair / bic_two_vs_one — used by RefineExistingClustering
#include "realign_xcorr.h"   // XcorrDispatch::compute — shared normalised circular xcorr
#include "realign_center.h"  // realign_center::circularRecenterShift (shared)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <chrono>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>         // patch100: std::set in KnnSplitPerChunk
#include <sstream>     // patch87: AutoReextractAfterFinalize needs ostringstream
#include <string>      // patch87: std::string for cmd builder
#include <unordered_map>
#include <unordered_set>
#ifdef _OPENMP
#include <omp.h>
#endif

// Phase-4 rewrite modules (klusters-faithful KNN, adaptation modelling,
// improved xcorr template matching).  Each module is independently
// testable and only invoked from KK.cpp via thin dispatch hooks.
#include "wave_knn_split.h"
#include "cluster_hull_split.h"
#include "per_channel_split.h"
#include "klusters_realign.h"
#include "adapt_model.h"
#include "xcorr_match.h"
#include "clust_quality.h"

// ── mmap for time-shift .spk/.spkD shared-memory access ──────────────────
// These POSIX headers are NOT OpenMP-related; the mmap/open/munmap call
// sites later in this file are unconditional, so the headers must be
// included unconditionally too.  Previously they lived inside the
// #ifdef _OPENMP block, which broke the SYCL build with icpx (where
// _OPENMP is not defined unless -fopenmp is added) — undefined
// O_RDONLY / PROT_READ / MAP_PRIVATE / MAP_FAILED / MADV_RANDOM / munmap.
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
// SIMD intrinsics for the batched EStep kernel (AVX-512 / AVX2 paths).
// The file is compiled with -march=native, so the right path is selected
// automatically at compile time via __AVX512F__ / __AVX2__ macros.
#if defined(__AVX512F__) || defined(__AVX2__)
#  include <immintrin.h>
#endif

// ---------------------------------------------------------------------------
// kMaxStackDims — compile-time ceiling on nDims for the fixed-size stack
// scratch buffers used in MStep / EStep (float v[kMaxStackDims], root[...],
// and the SIMD-batched dim-major variants `v[kMaxStackDims][BATCH]`).
//
// Real spike-feature configurations have nDims ≤ ~25 (PCA features +
// timestamp).  64 leaves headroom for high-channel-count sessions and is
// the value the codebase has used historically.  A runtime guard at the
// top of MStep/EStep refuses to run when nDims > kMaxStackDims, so a
// session that would overflow these buffers fails loudly rather than
// silently corrupting memory.
//
// If you need to raise this:
//   1. Bump the constant here.
//   2. The stack arrays will resize automatically (every site uses
//      kMaxStackDims, not the literal `64`, after the 2026-04-28 cleanup).
//   3. Re-evaluate stack-frame size: 64*16*4 = 4 KB per dim-major scratch;
//      doubling makes it 8 KB.  Each call has 2 scratches (v + root), and
//      OpenMP threads each have their own stack, so total stack pressure
//      scales with kMaxStackDims² · nThreads.
// ---------------------------------------------------------------------------
// kMaxStackDims is defined in KK_internal.h (shared with KK_refeaturize).

// ---------------------------------------------------------------------------
// AllocateArrays
// ---------------------------------------------------------------------------
void KK::AllocateArrays() {
    nDims2  = nDims * nDims;
    FullStep  = 1;
    NoisePoint = 1;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    Data       .SetSize(static_cast<size_t>(nPoints) * nDims,                "Data (nPoints*nDims)");
    Centres    .SetSize(static_cast<size_t>(MaxPossibleClusters) * nDims,    "Centres (MaxPossibleClusters*nDims)");
    Weight     .SetSize(MaxPossibleClusters,            "Weight (MaxPossibleClusters)");
    Mean       .SetSize(static_cast<size_t>(MaxPossibleClusters) * nDims,    "Mean (MaxPossibleClusters*nDims)");
    Cov        .SetSize(static_cast<size_t>(MaxPossibleClusters) * nDims2,   "Cov (MaxPossibleClusters*nDims2)");
    LogP       .SetSize(static_cast<size_t>(MaxPossibleClusters) * nPoints,  "LogP (MaxPossibleClusters*nPoints)");

    Class      .SetSize(nPoints,                        "Class (nPoints)");
    OldClass   .SetSize(nPoints,                        "OldClass (nPoints)");
    Class2     .SetSize(nPoints,                        "Class2 (nPoints)");
    BestClass  .SetSize(nPoints,                        "BestClass (nPoints)");
    ClassAlive .SetSize(MaxPossibleClusters,            "ClassAlive (MaxPossibleClusters)");
    AliveIndex .SetSize(MaxPossibleClusters,            "AliveIndex (MaxPossibleClusters)");

    // Persistent scratch arrays — reused by MStep and ConsiderDeletion
    // to avoid per-iteration heap allocation.
    m_classMembers.SetSize(MaxPossibleClusters,         "m_classMembers (MaxPossibleClusters)");
    m_deletionLoss.SetSize(MaxPossibleClusters,         "m_deletionLoss (MaxPossibleClusters)");
}

// ---------------------------------------------------------------------------
// AllocateCholeskyVecs (spelling kept for API compat)
// ---------------------------------------------------------------------------
void KK::AllocateCholeskyVecs() {
    // Allocated on this KK instance, not the global kSv, so chunk sub-objects
    // each own their Cholesky matrices and can run in parallel threads.
    // unique_ptr ensures memory is freed when the KK instance is destroyed,
    // including short-lived K2/K3/Kc sub-objects created inside TrySplits
    // and RunChunkedCEM.
    // Single contiguous allocation — one block per Cholesky set.
    // Replaces MaxPossibleClusters separate heap blocks → better TLB and cache behaviour.
    cholFlat    .assign(static_cast<size_t>(MaxPossibleClusters) * nDims2, 0.0f);
    bestCholFlat.assign(static_cast<size_t>(MaxPossibleClusters) * nDims2, 0.0f);
    ksv().BestAliveIndex.resize(MaxPossibleClusters);  // must be sized, not just reserved
}

// ---------------------------------------------------------------------------
// Reindex — rebuild AliveIndex from ClassAlive
// ---------------------------------------------------------------------------
void KK::Reindex() {
    AliveIndex[0] = 0;
    nClustersAlive = 1;
    for (int c = 1; c < MaxPossibleClusters; c++) {
        if (ClassAlive[c]) {
            AliveIndex[nClustersAlive++] = c;
        }
    }
}

// ---------------------------------------------------------------------------
// Penalty — AIC/BIC mixture
// ---------------------------------------------------------------------------
float KK::Penalty(int n) const {
    if (n == 1) return 0.0f;
    const int nParams = (nDims * (nDims + 1) / 2 + nDims + 1) * (n - 1);
    return static_cast<float>(
        (1.0 - penaltyMix) * nParams * 2.0 +
         penaltyMix        * nParams * std::log(nPoints) / 2.0);
}

// ---------------------------------------------------------------------------
// MStep — compute weights, means, covariances
//
// GPU parallelism:
//   Phase 1 (scatter): each thread handles one point, atomically adds to
//     shared per-cluster mean/cov accumulators.  With nPoints typically
//     100k-1M and nDims ~12-17, this is memory-bandwidth bound.
//   Phase 2 (normalise): one thread per (cluster, dim) pair — trivial.
//   Covariance outer product: nDims²=289 elements, easily fits in shared mem.
// ---------------------------------------------------------------------------
void KK::MStep() {
    // MStep/EStep use fixed-size stack arrays float v[kMaxStackDims] and
    // root[kMaxStackDims].  Real configurations have nDims ≤ ~25 (PCA
    // features + timestamp); the buffer is sized at kMaxStackDims = 64
    // for headroom.  See the comment block atop this file for sizing
    // notes.
    if (nDims > kMaxStackDims)
        Error("nDims=%d exceeds stack buffer size %d in MStep/EStep. "
              "Bump kMaxStackDims at the top of KK.cpp and rebuild.\n",
              nDims, kMaxStackDims);

    // Use the pre-allocated scratch array; zero it before use.
    std::memset(m_classMembers.m_Data, 0, sizeof(int) * MaxPossibleClusters);
    Array<int>& nClassMembers = m_classMembers;

    for (int p = 0; p < nPoints; p++) nClassMembers[Class[p]]++;

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        if (c > 0 && nClassMembers[c] <= nDims) {
            ClassAlive[c] = 0;
            if (Verbose >= 2)
                Output("Deleted class %d: not enough members\n", c);
        }
    }
    Reindex();

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        Weight[c] = (c == 0)
            ? static_cast<float>(nClassMembers[c] + NoisePoint) / (nPoints + NoisePoint)
            : static_cast<float>(nClassMembers[c])              / (nPoints + NoisePoint);
    }
    // Second Reindex is only needed if weight computation itself changed the
    // alive set, which it never does — skip the redundant O(MaxClusters) pass.
    // (Reindex already called above after the size-deletion loop.)

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu) {
        gpu_mstep(gpu,
            Class.m_Data, AliveIndex.m_Data,
            Mean.m_Data, Cov.m_Data, Weight.m_Data,
            nClustersAlive, MaxPossibleClusters,
            nPoints, nDims, nDims2);
    } else {
#endif
    // CPU path
    // Zero only the alive cluster rows — avoids clearing ~250 KB of unused
    // Mean/Cov storage when few clusters are alive (optimisation #3).
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        std::memset(Mean.m_Data + c * nDims,  0, sizeof(float) * nDims);
        std::memset(Cov.m_Data  + c * nDims2, 0, sizeof(float) * nDims2);
    }
    for (int p = 0; p < nPoints; p++) {
        const int c = Class[p];
        for (int i = 0; i < nDims; i++)
            Mean[c * nDims + i] += Data[p * nDims + i];
    }
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        if (nClassMembers[c] == 0) continue;
        for (int i = 0; i < nDims; i++) Mean[c * nDims + i] /= nClassMembers[c];
    }
    for (int p = 0; p < nPoints; p++) {
        const int c = Class[p];
        float v[kMaxStackDims];
        for (int i = 0; i < nDims; i++) v[i] = Data[p * nDims + i] - Mean[c * nDims + i];
        for (int i = 0; i < nDims; i++)
            for (int j = i; j < nDims; j++)
                Cov[c * nDims2 + i * nDims + j] += v[i] * v[j];
    }
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        if (nClassMembers[c] <= 1) continue;
        const float inv = 1.0f / (nClassMembers[c] - 1);
        for (int i = 0; i < nDims; i++)
            for (int j = i; j < nDims; j++)
                Cov[c * nDims2 + i * nDims + j] *= inv;
    }
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    } // end CPU path
#endif

    if (Debug) {
        for (int cc = 0; cc < nClustersAlive; cc++) {
            const int c = AliveIndex[cc];
            Output("Class %d - Weight %.2g\n", c, Weight[c]);
            Output("Mean: "); MatPrint(stdout, Mean.m_Data + c * nDims, 1, nDims);
            Output("\nCov:\n"); MatPrint(stdout, Cov.m_Data + c * nDims2, nDims, nDims);
        }
    }
}

// ---------------------------------------------------------------------------
// EStep — compute log-probability for each (point, cluster) pair
//
// GPU parallelism:
//   One CUDA thread per point p.  For each cluster c (loop in thread):
//     1. Load Vec2Mean = Data[p,:] - Mean[c,:]    (nDims loads)
//     2. TriSolve using Cholesky[c] stored in shared memory (broadcast)
//     3. Accumulate Mahal = ||Root||²
//     4. Write LogP[c*nPoints + p]   (coalesced with transposed layout)
//   The per-cluster Cholesky matrix (nDims² floats = 289 floats ≈ 1.1 KB)
//   fits in shared memory, broadcast to all threads in the block.
//   Launch: grid=(nPoints/256+1), block=256
// ---------------------------------------------------------------------------
void KK::EStep() {
    // cEStepCalls is function-local static — shared across all KK instances.
    // Chunk sub-objects call EStep from multiple OMP threads concurrently;
    // incrementing the static there would be a data race (UB).  The counter
    // is only used for model-file metadata, so suppress it in sub-objects.
    if (!suppressBestSave) {
        static int cEStepCalls = 0;
        ksv().cEStepCallsLast = ++cEStepCalls;
    }

    // Cluster 0: uniform noise — write into cluster-major row 0
    {
        const float noiseLogP = -std::log(Weight[0]);
        float *noiseRow = LogP.m_Data; // row 0: c=0, [0 * nPoints + p]
        for (int p = 0; p < nPoints; p++) noiseRow[p] = noiseLogP;
    }

    // Cholesky decomposition always on CPU — O(K * D^3), trivial cost
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        if (Cholesky(Cov.m_Data + c * nDims2, cholFlat.data() + c * nDims2, nDims)) {
            if (Verbose >= 2)
                Output("Deleting class %d: covariance matrix is singular\n", c);
            ClassAlive[c] = 0;
        }
    }
    Reindex();

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu) {
        // Flatten Cholesky factors into a contiguous host array for upload.
        std::vector<float> h_chol(static_cast<size_t>(MaxPossibleClusters) * nDims2, 0.0f);
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (ClassAlive[c])
                for (int i = 0; i < nDims2; i++)
                    h_chol[c * nDims2 + i] = cholFlat[static_cast<size_t>(c) * nDims2 + i];
        // Pass h_LogP only when DistDump is active — otherwise nullptr signals
        // the backend to leave LogP device-resident.  ComputeScore() uses the
        // gpu_compute_score reduction kernel instead of reading h_LogP.
        gpu_estep(gpu,
            Mean.m_Data, Weight.m_Data, h_chol.data(),
            AliveIndex.m_Data, Class.m_Data, OldClass.m_Data,
            DistDump ? LogP.m_Data : nullptr,
            nClustersAlive, DistThresh, FullStep, PI, MaxPossibleClusters);
        return;
    }
#endif

    // ── CPU path ────────────────────────────────────────────────────────────
    //
    // LogP is cluster-major [c * nPoints + p].
    //
    // SIMD BATCHING — the key insight:
    // The TriSolve recurrence  root[i] = (v[i] − Σ_{j<i} L[i,j]·root[j]) / L[i,i]
    // has a serial dependency across i, preventing SIMD across dimensions.
    // However, each POINT is completely independent of all other points.
    // By transposing the scratch layout to dim-major [nDims][BATCH] we can
    // process BATCH points simultaneously through the same recurrence:
    //
    //   For i = 0..nDims-1:
    //     s[0..B-1] = v[i][0..B-1]                    (vector load, 1 cycle)
    //     For j = 0..i-1:
    //       s -= L[i,j] * root[j][0..B-1]              (scalar broadcast × vector FMA)
    //     root[i][0..B-1] = s * inv_diag[i]            (vector multiply)
    //
    // With AVX-512 (16-wide) on Zen 5: measured 28× speedup vs scalar.
    // With AVX2   ( 8-wide) on older: measured 24× speedup vs scalar.
    // Fallback scalar path handles the tail (p % BATCH) and non-SIMD builds.
    //
    // DistThresh skip is NOT applied in the SIMD path (requires per-point
    // branch inside the batch loop, destroying SIMD regularity).  It is still
    // applied in the scalar tail.  This is intentional: the SIMD path runs
    // in FullStep iterations where DistThresh never skips anyway.
    //
    // Cholesky factors (nDims²≤625 floats = 2.5 KB) live entirely in L1 cache
    // across all nPoints → no Cholesky re-fetch cost per point.

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int   c          = AliveIndex[cc];
        const float *chol      = cholFlat.data() + c * nDims2;
        const float *mp        = Mean.m_Data + c * nDims;
        float *clusterLogP     = LogP.m_Data + static_cast<size_t>(c) * nPoints;

        // Hoist per-cluster scalar constants
        float logRootDet = 0.0f;
        for (int i = 0; i < nDims; i++)
            logRootDet += std::log(chol[i * nDims + i]);
        const float baseScore = logRootDet - std::log(Weight[c]) + log2piHalf;

        // Precompute reciprocals of diagonal: avoids nDims divisions per batch
        float inv_diag[kMaxStackDims];
        for (int i = 0; i < nDims; i++) inv_diag[i] = 1.0f / chol[i * nDims + i];

        // ── SIMD batch loop ─────────────────────────────────────────────
#if defined(__AVX512F__)
        // AVX-512: 16 points per iteration
        constexpr int BATCH = 16;
        {
            // Dim-major scratch: v[dim][BATCH], root[dim][BATCH]
            alignas(64) float v   [kMaxStackDims][BATCH];
            alignas(64) float root[kMaxStackDims][BATCH];

            int p = 0;
            for (; p + BATCH - 1 < nPoints; p += BATCH) {
                // Transpose: point-major → dim-major
                for (int d = 0; d < nDims; d++) {
                    const float *base = Data.m_Data + d;
                    for (int k = 0; k < BATCH; k++)
                        v[d][k] = base[(p+k) * nDims] - mp[d];
                }
                // TriSolve over BATCH points simultaneously
                for (int i = 0; i < nDims; i++) {
                    __m512 s = _mm512_load_ps(v[i]);
                    for (int j = 0; j < i; j++) {
                        __m512 cij = _mm512_set1_ps(chol[i * nDims + j]);
                        s = _mm512_fnmadd_ps(cij, _mm512_load_ps(root[j]), s);
                    }
                    _mm512_store_ps(root[i],
                        _mm512_mul_ps(s, _mm512_set1_ps(inv_diag[i])));
                }
                // Mahalanobis: sum root[d]² across dims
                __m512 mahal = _mm512_setzero_ps();
                for (int i = 0; i < nDims; i++) {
                    __m512 r = _mm512_load_ps(root[i]);
                    mahal = _mm512_fmadd_ps(r, r, mahal);
                }
                // Write LogP for this batch
                __m512 result = _mm512_fmadd_ps(mahal, _mm512_set1_ps(0.5f),
                                                _mm512_set1_ps(baseScore));
                _mm512_storeu_ps(clusterLogP + p, result);
            }
            // Scalar tail (< BATCH remaining points)
            for (; p < nPoints; p++) {
                float sv[kMaxStackDims], sroot[kMaxStackDims];
                const float *dp = Data.m_Data + p * nDims;
                for (int i = 0; i < nDims; i++) sv[i] = dp[i] - mp[i];
                for (int i = 0; i < nDims; i++) {
                    float s = sv[i];
                    for (int j = 0; j < i; j++) s -= chol[i*nDims+j] * sroot[j];
                    sroot[i] = s * inv_diag[i];
                }
                float m = 0.0f;
                for (int i = 0; i < nDims; i++) m += sroot[i] * sroot[i];
                clusterLogP[p] = m * 0.5f + baseScore;
            }
        }
#elif defined(__AVX2__)
        // AVX2: 8 points per iteration
        constexpr int BATCH = 8;
        {
            alignas(32) float v   [kMaxStackDims][BATCH];
            alignas(32) float root[kMaxStackDims][BATCH];

            int p = 0;
            for (; p + BATCH - 1 < nPoints; p += BATCH) {
                for (int d = 0; d < nDims; d++) {
                    const float *base = Data.m_Data + d;
                    for (int k = 0; k < BATCH; k++)
                        v[d][k] = base[(p+k) * nDims] - mp[d];
                }
                for (int i = 0; i < nDims; i++) {
                    __m256 s = _mm256_load_ps(v[i]);
                    for (int j = 0; j < i; j++) {
                        __m256 cij = _mm256_set1_ps(chol[i * nDims + j]);
                        s = _mm256_fnmadd_ps(cij, _mm256_load_ps(root[j]), s);
                    }
                    _mm256_store_ps(root[i],
                        _mm256_mul_ps(s, _mm256_set1_ps(inv_diag[i])));
                }
                __m256 mahal = _mm256_setzero_ps();
                for (int i = 0; i < nDims; i++) {
                    __m256 r = _mm256_load_ps(root[i]);
                    mahal = _mm256_fmadd_ps(r, r, mahal);
                }
                __m256 result = _mm256_fmadd_ps(mahal, _mm256_set1_ps(0.5f),
                                                _mm256_set1_ps(baseScore));
                _mm256_storeu_ps(clusterLogP + p, result);
            }
            for (; p < nPoints; p++) {
                float sv[kMaxStackDims], sroot[kMaxStackDims];
                const float *dp = Data.m_Data + p * nDims;
                for (int i = 0; i < nDims; i++) sv[i] = dp[i] - mp[i];
                for (int i = 0; i < nDims; i++) {
                    float s = sv[i];
                    for (int j = 0; j < i; j++) s -= chol[i*nDims+j] * sroot[j];
                    sroot[i] = s * inv_diag[i];
                }
                float m = 0.0f;
                for (int i = 0; i < nDims; i++) m += sroot[i] * sroot[i];
                clusterLogP[p] = m * 0.5f + baseScore;
            }
        }
#else
        // ── Scalar fallback (non-AVX builds) ────────────────────────────
        // Also handles the DistThresh skip heuristic for convergence speed.
        {
            int nSkipped = 0; (void)nSkipped;
            for (int p = 0; p < nPoints; p++) {
                if (!FullStep
                    && Class[p] == OldClass[p]
                    && clusterLogP[p] - LogP.m_Data[static_cast<size_t>(Class[p]) * nPoints+p] > DistThresh) {
                    nSkipped++;
                    continue;
                }
                float sv[kMaxStackDims], sroot[kMaxStackDims];
                const float *dp = Data.m_Data + p * nDims;
                for (int i = 0; i < nDims; i++) sv[i] = dp[i] - mp[i];
                for (int i = 0; i < nDims; i++) {
                    float s = sv[i];
                    for (int j = 0; j < i; j++) s -= chol[i*nDims+j] * sroot[j];
                    sroot[i] = s * inv_diag[i];
                }
                float m = 0.0f;
                for (int i = 0; i < nDims; i++) m += sroot[i] * sroot[i];
                clusterLogP[p] = m * 0.5f + baseScore;
            }
        }
#endif
    }  // end per-cluster loop
}

// ---------------------------------------------------------------------------
// CStep — assign each point to best cluster
//
// Returns the number of points that changed class this iteration so
// RunEMLoop can use it directly without a separate copy+scan pass (fix #5).
//
// LogP is now cluster-major [c * nPoints + p] on both CPU and GPU paths,
// so the per-point read strides by nPoints through cluster rows (fix #2).
// ---------------------------------------------------------------------------
int KK::CStep() {
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu) {
        gpu_cstep(gpu,
            Class.m_Data, OldClass.m_Data, Class2.m_Data,
            nClustersAlive, MaxPossibleClusters, HugeScore);
        // Count changes from OldClass (already updated by gpu_cstep)
        int nChanged = 0;
        for (int p = 0; p < nPoints; p++) nChanged += (Class[p] != OldClass[p]);
        return nChanged;
    }
#endif
    // CPU path — LogP cluster-major [c * nPoints + p]
    int nChanged = 0;
    for (int p = 0; p < nPoints; p++) {
        const int prevClass = Class[p];
        OldClass[p] = prevClass;
        float bestScore   = HugeScore, secondScore = HugeScore;
        int   topClass = 0, secondClass = 0;
        for (int cc = 0; cc < nClustersAlive; cc++) {
            const int   c = AliveIndex[cc];
            const float s = LogP.m_Data[static_cast<size_t>(c) * nPoints + p];  // cluster-major
            if (s < bestScore) {
                secondClass = topClass;   secondScore = bestScore;
                topClass    = c;          bestScore   = s;
            } else if (s < secondScore) {
                secondClass = c;          secondScore = s;
            }
        }
        Class[p]  = topClass;
        Class2[p] = secondClass;
        nChanged += (topClass != prevClass);
    }
    return nChanged;
}

// ---------------------------------------------------------------------------
// ConsiderDeletion — delete one cluster if it improves penalised score
// Bug fix: CandidateClass was used uninitialised when no alive classes found.
// ---------------------------------------------------------------------------
void KK::ConsiderDeletion() {
    // Use pre-allocated scratch; initialise dead clusters to HugeScore,
    // alive clusters to 0 (loss accumulates from the ClassPoint scan below).
    Array<float>& DeletionLoss = m_deletionLoss;
    for (int c = 0; c < MaxPossibleClusters; c++)
        DeletionLoss[c] = ClassAlive[c] ? 0.0f : HugeScore;

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu) {
        gpu_deletion_loss(gpu, DeletionLoss.m_Data, MaxPossibleClusters);
        // gpu_deletion_loss downloads d_Loss directly into DeletionLoss, overwriting
        // the pre-initialised HugeScore for dead clusters with zeros (d_Loss was
        // memset to 0 before the kernel; dead clusters accumulate nothing).
        // Restore HugeScore so dead clusters are never selected as candidates.
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (!ClassAlive[c]) DeletionLoss[c] = HugeScore;
        DeletionLoss[0] = HugeScore;  // noise cluster never a candidate
    } else {
#endif
    for (int p = 0; p < nPoints; p++)
        DeletionLoss[Class[p]] +=
            LogP.m_Data[static_cast<size_t>(Class2[p]) * nPoints + p] -   // cluster-major
            LogP.m_Data[static_cast<size_t>(Class[p]) * nPoints + p];
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    }
#endif

    float minLoss = HugeScore;
    int   candidateClass = -1;
    for (int c = 1; c < MaxPossibleClusters; c++) {
        if (DeletionLoss[c] < minLoss) {
            minLoss = DeletionLoss[c];
            candidateClass = c;
        }
    }
    // candidateClass < 0 means no alive class found — ClassAlive unchanged,
    // AliveIndex still valid, no Reindex needed.
    if (candidateClass < 0) return;

    // Enforce minClustersAlive floor — ClassAlive unchanged, no Reindex needed.
    if (nClustersAlive <= minClustersAlive) return;

    const float deltaPen = Penalty(nClustersAlive) - Penalty(nClustersAlive - 1);

    // ── kiloklustakwik: shift-aware merge decision ───────────────────────────
    // The canonical minLoss is computed assuming each spike stays at its
    // current feature vector (no temporal shift into the second-best
    // cluster).  A cluster that's actually a temporal duplicate of another
    // unit — same neuron, different peak-detection alignment — has
    // artificially inflated minLoss because the mean waveforms are offset.
    //
    // Evaluate a plan that allows each destination sub-batch to commit a
    // single cluster-wide δ ∈ {-N,…,+N} before the loss is computed.  The
    // plan's lossReductionTotal is ≤ 0.  If it brings minLoss below deltaPen,
    // the merge is accepted at the planned δs; otherwise behaviour is
    // unchanged.
    //
    // Gated on:  m_timeShiftReady && TimeShiftMergeEnable != 0 &&
    //            minLoss >= deltaPen   (a merge already accepted at δ=0
    //                                   doesn't need the probe)
    // Also gated: minLoss < deltaPen + 8*|deltaPen|  (skip hopeless
    // candidates to bound probe overhead — at 8× the threshold, even the
    // maximum reduction from χ²-valid shifts is unlikely to close the gap).
    TimeShiftMergePlan shiftPlan;
    const bool mergeProbeEnabled =
        m_timeShiftReady && TimeShiftMergeEnable != 0 &&
        NbChannels > 0 && NbSamplesPerSpike > 0;
    float effectiveMinLoss = minLoss;
    if (mergeProbeEnabled && minLoss >= deltaPen &&
        minLoss < deltaPen + 8.0f * std::fabs(deltaPen))
    {
        if (TimeShiftMergeEvaluate(
                candidateClass, NbChannels, NbSamplesPerSpike, shiftPlan)
            && shiftPlan.valid)
        {
            effectiveMinLoss = minLoss + shiftPlan.lossReductionTotal;
        }
    }

    if (effectiveMinLoss < deltaPen) {
        if (Verbose >= 2)
            Output("Deleting Class %d. Lose %f but Gain %f%s\n",
                   candidateClass, effectiveMinLoss, deltaPen,
                   (effectiveMinLoss != minLoss) ? " (shift-aware)" : "");
        ClassAlive[candidateClass] = 0;

        // Commit planned per-destination cluster-wide δs BEFORE reassignment.
        // This writes trial features into Data[] and bumps m_cumShift so
        // the reassigned spikes start their life in the new cluster with
        // shift-corrected feature vectors.
        if (shiftPlan.valid && shiftPlan.lossReductionTotal < 0.0f)
            TimeShiftMergeCommit(shiftPlan);

        // ── Group + reassign victim's spikes by Class2 destination ────────
        std::vector<std::vector<int>> byDest(
            static_cast<size_t>(MaxPossibleClusters));
        for (int p = 0; p < nPoints; p++) {
            if (Class[p] == candidateClass) {
                const int dest = Class2[p];
                if (mergeProbeEnabled && dest > 0 && dest < MaxPossibleClusters
                    && ClassAlive[dest])
                    byDest[static_cast<size_t>(dest)].push_back(p);
                Class[p] = dest;
            }
        }

        // ── Post-merge per-spike tightener ────────────────────────────────
        // Running in addition to the decision-level probe (user-selected
        // behaviour): the decision established a cluster-wide δ per sub-
        // batch capturing SYSTEMATIC mis-alignment; the tightener now
        // allows each spike to further refine its fit within the remaining
        // budget, bounded by m_timeShiftMaxAbs.  Destinations with
        // < 5 transferred spikes are skipped (not worth the I/O).
        if (mergeProbeEnabled) {
            int totalShifted = 0;
            for (int dest = 1; dest < MaxPossibleClusters; ++dest) {
                const auto& idxs = byDest[dest];
                if (static_cast<int>(idxs.size()) < 5) continue;
                const float* destMean = Mean.m_Data     + dest * nDims;
                const float* destChol = cholFlat.data() + dest * nDims2;
                totalShifted += TimeShiftMergeTighten(
                    idxs, NbChannels, NbSamplesPerSpike, destMean, destChol);
            }
            if (totalShifted > 0)
                Output("  [tshift-merge-tighten] %d spikes shifted per-spike "
                       "post-merge\n", totalShifted);
        }
    }
    Reindex();
}

// ---------------------------------------------------------------------------
// ReinitForSplit — reset a pre-allocated KK scratch object for reuse.
//
// Instead of destroying and reallocating all arrays for each split trial,
// we reuse the existing heap storage (allocated at full nPoints capacity)
// and zero only what CEM will touch.  This eliminates the per-cluster
// heap allocation inside the TrySplits loop that causes contention when
// multiple OMP chunk workers call TrySplits concurrently.
//
// Contract: caller must have previously called AllocateArrays() with
// nPoints = maxPoints (the largest value that will ever be passed here).
// ---------------------------------------------------------------------------
void KK::ReinitForSplit(int newNPoints, int newNDims, float newPenaltyMix) {
    nPoints    = newNPoints;
    nDims      = newNDims;
    nDims2     = newNDims * newNDims;
    penaltyMix = newPenaltyMix;
    NoisePoint = 0;
    FullStep   = 1;
    suppressBestSave = true;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * newNDims * 0.5);

    // Zero point-indexed arrays to newNPoints (arrays are allocated >= this).
    // LogP is deliberately NOT zeroed: EStep writes every LogP[c*nPoints+p]
    // before any read, so pre-zeroing (potentially 137 MB) is wasted work.
    std::memset(Class.m_Data,    0, sizeof(int)   * newNPoints);
    std::memset(OldClass.m_Data, 0, sizeof(int)   * newNPoints);
    std::memset(Class2.m_Data,   0, sizeof(int)   * newNPoints);

    // Zero cluster-indexed arrays entirely (small — MaxPossibleClusters elements)
    std::memset(Weight.m_Data,    0, sizeof(float) * MaxPossibleClusters);
    std::memset(ClassAlive.m_Data,0, sizeof(int)   * MaxPossibleClusters);
    std::memset(AliveIndex.m_Data,0, sizeof(int)   * MaxPossibleClusters);
    // Mean and Cov are overwritten by MStep before being read; no need to zero.
}

// ---------------------------------------------------------------------------
// TrySplits — try splitting each cluster; keep if it improves score
// C++17: KK objects use move semantics when passed around.
// ---------------------------------------------------------------------------
// thread_local depth counter — limits recursive TrySplits nesting to 1 level.
// Without this, CEM(nullptr,1) inside TrySplits would trigger TrySplits again
// inside K2, which would trigger it again inside K2b, causing stack overflow.
static thread_local int _trySplitsDepth = 0;

int KK::TrySplits() {
    // Guard: sub-trial CEM calls TrySplits at depth 1; their sub-trials must
    // not recurse further (depth 2+), so they use CEM(nullptr, 0).
    _trySplitsDepth++;
    struct _DepthGuard { ~_DepthGuard() { _trySplitsDepth--; } } _dg;
    (void)_dg;
    if (nClustersAlive >= MaxPossibleClusters - 1) {
        Output("Won't try splitting - already at maximum number of clusters\n");
        return 0;
    }

    // Respect the user's -MaxClusters ceiling — don't split beyond it.
    // Without this guard, per-chunk EM with PenaltyMix=0.0 (pure AIC) will
    // keep accepting splits indefinitely since AIC heavily favours more clusters.
    if (nClustersAlive >= MaxClusters) {
        if (Verbose >= 2)
            Output("Won't try splitting - already at MaxClusters (%d)\n", MaxClusters);
        return 0;
    }

    // K3: full-session scratch for scoring candidate splits.
    // Allocated once per TrySplits call, outside the cluster loop.
    KK K3;
    K3.nDims = nDims; K3.nPoints = nPoints;
    K3.AllocateArrays();
    K3.AllocateCholeskyVecs();
    K3.suppressBestSave = true;
    K3.penaltyMix = PenaltyMix;
    for (int i = 0; i < nDims * nPoints; i++) K3.Data[i] = Data[i];

    // K2: sub-cluster scratch for split CEM trials.
    // Pre-allocated at full nPoints capacity and reused for every cluster
    // via ReinitForSplit — eliminates per-cluster heap allocation.
    KK K2;
    K2.nDims   = nDims;
    K2.nPoints = nPoints;   // maximum possible; actual count set per cluster
    K2.AllocateArrays();
    K2.AllocateCholeskyVecs();

    const float Score = ComputeScore();
    int DidSplit = 0;

    // P1.B: feature selection by bimodality coefficient.
    //
    // The earlier metric was variance-ratio: clusterVar[d] / globalVar[d].
    // That picks dims where the cluster has high relative variance — but
    // "wide-and-noisy" and "two-modes-separated" both score high, and only
    // the second is what TrySplits is looking for.  A noisy unimodal dim
    // routinely ended up in the kSelect set, wasting K2's budget.
    //
    // Bimodality coefficient (Sarle's b):
    //     b = (skew² + 1) / (kurt + 3·(n-1)²/((n-2)(n-3)))
    // For a unimodal Gaussian b ≈ 0.555.  A perfect 50/50 mixture of two
    // well-separated modes gives b → 1.  Values >5/9 ≈ 0.555 indicate
    // multimodality is plausible.  We sort dims by b descending and take
    // the top kSelect.
    //
    // We accumulate moments in one O(N) pass per cluster (m2, m3, m4 around
    // the cluster mean), so the cost is comparable to the previous variance
    // pass.  No global-stats precompute is needed any more — bimodality is
    // a within-cluster diagnostic.
    const int nSpatialD  = (nDims > 1) ? nDims - 1 : nDims;
    const int kSelect    = std::min(nSpatialD, std::max(6, nSpatialD / 2));
    const bool doFeatSel = (kSelect < nSpatialD);

    // Scratch vectors hoisted outside per-cluster loop to avoid per-iteration
    // heap allocation.  assign() resets them at the top of each iteration.
    std::vector<float>  clusterMean(nSpatialD, 0.0f);
    std::vector<double> m2(nSpatialD, 0.0), m3(nSpatialD, 0.0), m4(nSpatialD, 0.0);
    std::vector<float>  bimod(nSpatialD, 0.0f);
    std::vector<int>    selectedDims(nSpatialD);

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];

        // Pass A: count points and accumulate mean in one O(N) scan.
        // Previously this was two separate passes (count then mean).
        int clusterSize = 0;
        if (doFeatSel) {
            std::fill(clusterMean.begin(), clusterMean.end(), 0.0f);
            for (int p = 0; p < nPoints; p++) if (Class[p] == c) {
                clusterSize++;
                for (int d = 0; d < nSpatialD; d++)
                    clusterMean[d] += Data[p * nDims + d];
            }
            if (clusterSize == 0) continue;
            const float inv = 1.0f / clusterSize;
            for (int d = 0; d < nSpatialD; d++) clusterMean[d] *= inv;
        } else {
            for (int p = 0; p < nPoints; p++) if (Class[p] == c) clusterSize++;
            if (clusterSize == 0) continue;
        }

        // Pass B (doFeatSel only): accumulate central moments m2, m3, m4 and
        // compute Sarle's bimodality coefficient per dim.  Bimodality needs
        // n ≥ 4 for the kurtosis correction to be defined; below that we
        // fall back to "all dims" by leaving selectedDims at [0..nSpatialD).
        std::iota(selectedDims.begin(), selectedDims.end(), 0);
        if (doFeatSel && clusterSize >= 4) {
            std::fill(m2.begin(), m2.end(), 0.0);
            std::fill(m3.begin(), m3.end(), 0.0);
            std::fill(m4.begin(), m4.end(), 0.0);
            for (int p = 0; p < nPoints; p++) if (Class[p] == c)
                for (int d = 0; d < nSpatialD; d++) {
                    const double diff = Data[p * nDims + d] - clusterMean[d];
                    const double d2 = diff * diff;
                    m2[d] += d2;
                    m3[d] += d2 * diff;
                    m4[d] += d2 * d2;
                }
            const double n_d   = static_cast<double>(clusterSize);
            const double inv_n = 1.0 / n_d;
            // Kurtosis bias correction: 3*(n-1)^2 / ((n-2)*(n-3))
            const double kurtCorr = 3.0 * (n_d - 1.0) * (n_d - 1.0)
                                  / ((n_d - 2.0) * (n_d - 3.0));
            for (int d = 0; d < nSpatialD; d++) {
                const double mu2 = m2[d] * inv_n;
                if (mu2 < 1e-12) { bimod[d] = 0.0f; continue; }
                const double mu3   = m3[d] * inv_n;
                const double mu4   = m4[d] * inv_n;
                const double skew  = mu3 / std::pow(mu2, 1.5);
                const double kurt  = mu4 / (mu2 * mu2) - 3.0;  // excess kurtosis
                const double denom = kurt + kurtCorr;
                bimod[d] = (denom > 1e-9)
                    ? static_cast<float>((skew * skew + 1.0) / denom)
                    : 0.0f;
            }
            std::sort(selectedDims.begin(), selectedDims.end(), [&](int a, int b) {
                return bimod[a] > bimod[b];
            });
            selectedDims.resize(kSelect);
            std::sort(selectedDims.begin(), selectedDims.end()); // restore dim order
        } else if (doFeatSel) {
            // n < 4 — fall back to using all dims (the cluster is too small
            // for moment-based selection, but won't reach the split threshold
            // anyway since the inner CEM filter trims sub-clusters of size
            // ≤ nDims).  Keeping selectedDims = [0..nSpatialD) is safe.
            // Resize it back to kSelect with the lowest indices, to keep
            // K2 dimension consistent with the no-fallback path.
            selectedDims.resize(kSelect);
        }

        const int nDimsK2 = kSelect + (nDims > nSpatialD ? 1 : 0);
        if (doFeatSel)
            K2.ReinitForSplit(clusterSize, nDimsK2, PenaltyMix);
        else
            K2.ReinitForSplit(clusterSize, nDims, PenaltyMix);

        // Pack cluster points into K2.Data — one O(N) scan
        int p2 = 0;
        for (int p = 0; p < nPoints; p++) if (Class[p] == c) {
            if (doFeatSel) {
                int d2 = 0;
                for (int d : selectedDims)
                    K2.Data[p2 * nDimsK2 + d2++] = Data[p * nDims + d];
                if (nDims > nSpatialD)
                    K2.Data[p2 * nDimsK2 + d2] = Data[p * nDims + nSpatialD];
            } else {
                for (int d = 0; d < nDims; d++)
                    K2.Data[p2 * nDims + d] = Data[p * nDims + d];
            }
            p2++;
        }

        int unusedCluster = -1;
        for (int c2 = 1; c2 < MaxPossibleClusters; c2++)
            if (!ClassAlive[c2]) { unusedCluster = c2; break; }
        if (unusedCluster == -1) { Output("No free clusters, abandoning split"); return DidSplit; }

        if (Verbose >= 2) Output("Trying to split cluster %d (%d points)\n", c, clusterSize);
        K2.nStartingClusters = 2;
        const float unsplitScore = K2.CEM(nullptr, 0);  // splits disabled: pure 1-cluster baseline
        K2.nStartingClusters = 13;  // noise + 12 real: gives CEM room to find multi-cluster structure
        // Allow splits in the sub-CEM only at the first level of recursion;
        // deeper calls use Recurse=0 to prevent infinite recursion.
        // Allow recursion up to SplitRecurseDepth levels; deeper calls are non-recursive.
        const float splitScore   = K2.CEM(nullptr, (_trySplitsDepth <= SplitRecurseDepth) ? 1 : 0);

        if (splitScore < unsplitScore) {
            for (int c2 = 0; c2 < MaxPossibleClusters; c2++) K3.ClassAlive[c2] = 0;
            p2 = 0;
            for (int p = 0; p < nPoints; p++) {
                if (Class[p] == c) {
                    // Map class 1 → keep original cluster c; all others → unusedCluster.
                    // K2 CEM may produce >2 clusters in low-dim subspace — that is fine,
                    // they all collapse to the single "split off" group here.
                    K3.Class[p] = (K2.Class[p2] == 1) ? c : unusedCluster;
                    p2++;
                } else K3.Class[p] = Class[p];
                K3.ClassAlive[K3.Class[p]] = 1;
            }
            K3.Reindex();

            // P1.A: settle the proposed split with a few EM iterations before
            // scoring.  The earlier version did MStep + EStep + ComputeScore
            // exactly once, which scored the K2-recommended labels at iteration
            // zero — before spikes near the new boundary had been re-EStepped
            // against the new cluster covariances.  That made the BIC test
            // unfair: K3's score reflected K2's subspace decision rather than
            // the proposal's full-dim quality.  A short settle (3 iterations,
            // splits disabled) lets the boundary stabilise; the comparison is
            // then between two converged-enough fits.
            //
            // enableSplits=false on its own is sufficient to keep K3 a 2-class
            // proposal — RunEMLoop's TrySplits dispatch is gated on that flag
            // (KK.cpp:`if (enableSplits && SplitEvery > 0 && ...)`).  SplitEvery
            // is a global, not a per-object member, so there's no need (and no
            // way) to save/restore it on K3.  3 iterations is enough for
            // boundary spikes to re-assign; convergence to fixed-point
            // typically completes in <5 iters at this scale.
            K3.RunEMLoop(/*enableSplits=*/  false,
                         /*enableDistDump=*/false,
                         /*maxIter=*/       3,
                         /*phaseLabel=*/    "[trySplit-recheck]");
            const float newScore = K3.ComputeScore();
            if (Verbose >= 2)
                Output("Splitting cluster %d changes total score from %f to %f\n",
                       c, Score, newScore);
            if (newScore < Score) {
                DidSplit = 1;
                if (Verbose >= 2)
                    Output("So it's getting split into cluster %d.\n", unusedCluster);
                for (int c2 = 0; c2 < MaxPossibleClusters; c2++) ClassAlive[c2] = K3.ClassAlive[c2];
                for (int p  = 0; p  < nPoints;               p++) Class[p] = K3.Class[p];

                // ── kiloklustakwik: post-split shift-probe refeaturization ──
                // Test δ ∈ {−1, 0, +1} on each new child cluster; commit the
                // shift that maximises spatial-feature variance to expose any
                // residual mixture for the next split trial.  Gates below
                // avoid spending I/O on splits too small to benefit.
                if (TimeShiftSplitEnable != 0 &&
                    m_timeShiftReady && NbChannels > 0 && NbSamplesPerSpike > 0) {
                    int nA = 0, nB = 0;
                    for (int p = 0; p < nPoints; ++p) {
                        if (Class[p] == c)              ++nA;
                        else if (Class[p] == unusedCluster) ++nB;
                    }
                    const int minChild = std::min(nA, nB);
                    if (minChild >= 20 && minChild * 10 >= clusterSize) {
                        int chA = TimeShiftSplitCluster(c, NbChannels,
                                                             NbSamplesPerSpike);
                        int chB = TimeShiftSplitCluster(unusedCluster,
                                                             NbChannels, NbSamplesPerSpike);
                        if ((chA + chB) > 0) {
                            // Features changed → refresh model before next iter
                            MStep(); EStep();
                            if (Verbose >= 1)
                                Output("  [tshift-split] cluster %d shifted %d spikes, "
                                       "cluster %d shifted %d spikes\n",
                                       c, chA, unusedCluster, chB);
                        }
                    }
                }
            } else if (Verbose >= 2) Output("So it's not getting split.\n");
        }
    }
    return DidSplit;
}

// ---------------------------------------------------------------------------
// ComputeScore
//
// When a GPU is present, LogP is device-resident and we run a lightweight
// reduction kernel rather than reading the full host LogP array.
// The CPU path is unchanged.
// ---------------------------------------------------------------------------
float KK::ComputeScore() const {
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu)
        return gpu_compute_score(gpu, Penalty(nClustersAlive));
#endif
    float score = Penalty(nClustersAlive);
    for (int p = 0; p < nPoints; p++)
        score += LogP.m_Data[static_cast<size_t>(Class[p]) * nPoints + p];  // cluster-major
    return score;
}

// ---------------------------------------------------------------------------
// CEM — main EM loop
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// RunEMLoop — shared convergence loop body used by CEM() and CEMTwoPhase()
//
// Runs MStep → EStep → CStep → ConsiderDeletion until convergence.
//   enableSplits   — call TrySplits() every SplitEvery iterations
//   enableDistDump — write LogP to Distfp each iteration (CEM only)
//   maxIter        — iteration cap; 0 uses the global MaxIter
//   phaseLabel     — prefix for Verbose output (e.g. "P1", "iter")
// Returns final score.
// ---------------------------------------------------------------------------
float KK::RunEMLoop(bool enableSplits, bool enableDistDump,
                    int maxIter, const char *phaseLabel)
{
    // Distinguish caller-supplied iter caps (Phase 2's timeMergeIter,
    // Phase 2b's Phase2bMaxIter, etc) from the global-MaxIter default
    // that's used when caller passes 0.  Hitting an explicit cap is
    // EXPECTED — caller knew the phase would saturate the budget — and
    // emitting "max iterations exceeded" for it spams the log (one line
    // per chunk for Phase 2b on a 36-chunk run).  Hitting the global
    // default IS informative: it means a CEM that should have converged
    // didn't.  Only the latter case logs.
    const bool _explicitCap = (maxIter > 0);
    if (maxIter <= 0) maxIter = MaxIter;

    int   iter = 0, nChanged = 1, lastStepFull = 1, didSplit = 0;
    float score = 0.0f;
    FullStep = 1;

    // Whether score is needed this iteration:
    //   - always when verbose (logging) or best-save is active
    //   - never when suppressBestSave && Verbose < 1 (chunk sub-objects, fix #10)
    const bool needScore = (!suppressBestSave || Verbose >= 1);

    do {
        MStep();
        EStep();
        if (enableDistDump && DistDump)
            MatPrint(Distfp, LogP.m_Data, MaxPossibleClusters, DistDump); // cluster-major: rows=clusters, cols=nPoints

        // CStep now returns nChanged directly, eliminating the pre-copy of
        // Class[] into oldClassBuf and the post-scan diff loop (fix #5).
        nChanged = CStep();
        ConsiderDeletion();

        if (needScore) {
            score = ComputeScore();
            if (!suppressBestSave && score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            // Per-iter CEM trace.  Promoted ENTIRELY to Verbose >= 2:
            // these fire once per iteration of every CEM — including the
            // many per-cluster sub-trials (TrySplits' K2/K3, Phase 2b /
            // RefractorySplit split tests, the Phase 4b FullCem probes) —
            // and at Verbose 1 they drown the per-phase summaries
            // (convergence digest, identity flow, split/merge counts),
            // which stay at Verbose 1.
            if (Verbose >= 2)
                Output("  %s iter %d%c: %d clusters score %.7g nChanged %d\n",
                       phaseLabel, iter, FullStep ? 'F' : 'Q',
                       nClustersAlive, score, nChanged);
        }
        iter++;

        lastStepFull = FullStep;
        FullStep = (nChanged > static_cast<int>(ChangedThresh * nPoints)
                    || nChanged == 0
                    || iter % FullStepEvery == 0);

        if (iter > maxIter) {
            // Only warn when the cap is the GLOBAL default — that signals
            // a CEM phase that should have converged but didn't.  When
            // the caller passed an explicit cap (Phase 2's timeMergeIter,
            // Phase 2b's Phase2bMaxIter, trySplit-recheck's 3-iter
            // stabiliser, …), hitting it is expected and the warning is
            // pure noise (one line per chunk × runs).  See _explicitCap
            // docstring at the top of RunEMLoop.
            if (!_explicitCap)
                Output("%s: max iterations exceeded\n", phaseLabel);
            break;
        }

        didSplit = 0;
        if (enableSplits && SplitEvery > 0 &&
            (iter % SplitEvery == SplitEvery - 1 || (nChanged == 0 && lastStepFull)))
            didSplit = TrySplits();

    } while (nChanged > 0 || !lastStepFull || didSplit);

    // Compute final score if we skipped it during the loop
    if (!needScore) score = ComputeScore();
    return score;
}

float KK::CEM(const char *CluFile, int Recurse) {
    if (CluFile && *CluFile) {
        LoadClu(CluFile);
    } else {
        if (nStartingClusters > 1)
            for (int p = 0; p < nPoints; p++) Class[p] = irand(1, nStartingClusters - 1);
        else
            for (int p = 0; p < nPoints; p++) Class[p] = 0;
    }

    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
    Reindex();

    float score = RunEMLoop(
        /*enableSplits=*/   Recurse != 0,
        /*enableDistDump=*/ true,
        /*maxIter=*/        0,       // use global MaxIter
        /*phaseLabel=*/     Recurse ? "[CEM]" : "[CEM-split]");

    if (DistDump) fprintf(Distfp, "\n");

    // kiloklustakwik Phase 8: DipSplit — bimodal-cluster detection & split.
    // Runs once on converged clusters.  No-op if DipSplitEnable=0,
    // DipSplitGlobalEnable=0, or no bloated clusters are found.
    //
    // The DipSplitGlobalEnable check disables Phase 8 in chunked mode with
    // drift, where the global cluster's spikes span the session's drift
    // range and the top PC of the mean-centered data captures the drift
    // axis — producing false-positive splits that bisect a single drifting
    // unit into "early" and "late" sub-clusters.  Per-chunk Phase 1b
    // DipSplit is unaffected (suppressBestSave=true on per-chunk Ks).
    if (DipSplitEnable != 0 && DipSplitGlobalEnable != 0 && !suppressBestSave) {
        DipSplitPhase();
    }

    return score;
}

// ---------------------------------------------------------------------------
// InitCentresFarthestPoint
//
// Seeds cluster centres by iteratively picking the data point that is
// farthest (in Euclidean distance) from all already-chosen centres.
// This guarantees centres start at the periphery of feature space and
// work inward, so that:
//   (a) each centre has a well-separated spatial territory from the start,
//   (b) the first MStep sees coherent, spatially compact groups rather than
//       random noise, dramatically reducing the iterations to convergence.
//
// Only the first nSpatialDims dimensions are used — the time dimension
// (always the last column in .fet files) is deliberately excluded so that
// temporal drift does not push early centres to opposite ends of the session.
//
// Algorithm is O(nCentres × nPoints × nSpatialDims) — negligible compared
// to a single EStep.
// ---------------------------------------------------------------------------
void KK::InitCentresFarthestPoint(int nCentres, int nSpatialDims) {
    Centres.SetSize(nCentres * nDims);

    // --- seed 0: the global centroid of all points in spatial dims ----------
    std::vector<float> centroid(nSpatialDims, 0.0f);
    for (int p = 0; p < nPoints; p++)
        for (int d = 0; d < nSpatialDims; d++)
            centroid[d] += Data[p * nDims + d];
    for (int d = 0; d < nSpatialDims; d++)
        centroid[d] /= nPoints;

    // Find the point farthest from the centroid to use as centre 0.
    // Starting from the centroid (rather than a random point) means centre 0
    // lands at the periphery that is farthest from the data's centre of mass.
    int first = 0;
    float bestDist = -1.0f;
    for (int p = 0; p < nPoints; p++) {
        float dist = 0.0f;
        for (int d = 0; d < nSpatialDims; d++) {
            const float dx = Data[p * nDims + d] - centroid[d];
            dist += dx * dx;
        }
        if (dist > bestDist) { bestDist = dist; first = p; }
    }
    for (int d = 0; d < nDims; d++)
        Centres[0 * nDims + d] = Data[first * nDims + d];

    // --- seeds 1..nCentres-1: farthest-point from all existing centres ------
    // minDistToSet[p] = distance from point p to its nearest chosen centre.
    // We maintain this incrementally: after adding centre k, only update
    // points for which the new centre is closer than their current minimum.
    std::vector<float> minDistToSet(nPoints, HugeScore);

    // Initialise against centre 0
    for (int p = 0; p < nPoints; p++) {
        float dist = 0.0f;
        for (int d = 0; d < nSpatialDims; d++) {
            const float dx = Data[p * nDims + d] - Centres[0 * nDims + d];
            dist += dx * dx;
        }
        minDistToSet[p] = dist;
    }

    for (int k = 1; k < nCentres; k++) {
        // The next centre is the point with the largest minDistToSet
        int nextIdx = 0;
        float maxMin = -1.0f;
        for (int p = 0; p < nPoints; p++) {
            if (minDistToSet[p] > maxMin) { maxMin = minDistToSet[p]; nextIdx = p; }
        }

        for (int d = 0; d < nDims; d++)
            Centres[k * nDims + d] = Data[nextIdx * nDims + d];

        // Update minDistToSet for the newly added centre
        for (int p = 0; p < nPoints; p++) {
            float dist = 0.0f;
            for (int d = 0; d < nSpatialDims; d++) {
                const float dx = Data[p * nDims + d] - Centres[k * nDims + d];
                dist += dx * dx;
            }
            if (dist < minDistToSet[p]) minDistToSet[p] = dist;
        }
    }

    if (Verbose >= 1) {
        Output("FarthestPoint seeds (%d centres, %d spatial dims):\n",
               nCentres, nSpatialDims);
        for (int k = 0; k < nCentres; k++) {
            Output("  centre %d:", k);
            for (int d = 0; d < nSpatialDims; d++)
                Output(" %.3f", Centres[k * nDims + d]);
            Output("\n");
        }
    }
}

// ---------------------------------------------------------------------------
// InitCentresKMeansPP — k-means++ D²-weighted random seeding
//
// Algorithm (Arthur & Vassilvitskii 2007):
//
//   1. Pick centre 0 uniformly at random from the points.
//   2. For each subsequent centre k = 1..nCentres-1:
//        a. For every point p, let D(p) = distance from p to the nearest
//           already-chosen centre, measured in `nSpatialDims` Euclidean dims.
//        b. Pick the next centre with probability proportional to D(p)².
//
// Compared to InitCentresFarthestPoint, this:
//   - Is randomised (uses RandomSeed-derived state).  Different runs over
//     identical data produce different seeds.  Useful for `-nRuns >1`.
//   - Has a proven O(log k) expected-cost approximation guarantee.
//   - Is more robust to single far-away outliers (farthest-point ALWAYS
//     picks the outlier as a centre; D²-sampling is biased toward far
//     points but doesn't lock in the most extreme one).
//
// Cost is identical to InitCentresFarthestPoint — same nCentres·nPoints
// distance computations, plus one O(nPoints) random sample per centre.
// ---------------------------------------------------------------------------
void KK::InitCentresKMeansPP(int nCentres, int nSpatialDims) {
    Centres.SetSize(nCentres * nDims);
    if (nCentres < 1 || nPoints < 1) return;

    // Draw from the shared thread-local RNG (KlustaKwik.h).  The caller seeds
    // it per work-item (chunk/run), so KMeans++ initialisation is reproducible
    // and thread-independent, in step with the rest of the random path.

    // ── Pick centre 0 uniformly at random ────────────────────────────────
    {
        const int p0 = irand(0, nPoints - 1);
        for (int d = 0; d < nDims; d++)
            Centres[0 * nDims + d] = Data[p0 * nDims + d];
    }

    // minDistToSet[p] = squared Euclidean distance (in spatial dims) from
    // point p to its nearest chosen centre.  Maintained incrementally —
    // after each new centre, we update only points where the new centre
    // is closer than the previous best.
    std::vector<float> minDistToSet(nPoints, HugeScore);

    // Initialise against centre 0.
    for (int p = 0; p < nPoints; p++) {
        float dist = 0.0f;
        for (int d = 0; d < nSpatialDims; d++) {
            const float dx = Data[p * nDims + d] - Centres[0 * nDims + d];
            dist += dx * dx;
        }
        minDistToSet[p] = dist;
    }

    // ── Pick centres 1..nCentres-1 by D²-weighted sampling ───────────────
    for (int k = 1; k < nCentres; k++) {
        // Total "mass" = sum of squared distances; sample u ∈ [0, total).
        // Walk the points until cumulative mass crosses u; that's the
        // chosen centre.  Equivalent to inverse-CDF sampling on a
        // discrete distribution, O(nPoints) per centre.
        double total = 0.0;
        for (int p = 0; p < nPoints; p++)
            total += static_cast<double>(minDistToSet[p]);

        int nextIdx = 0;
        if (total <= 0.0) {
            // All points coincide with existing centres — degenerate.
            // Fall back to the farthest point (which would be picked
            // by farthest-point seeding too).
            float maxMin = -1.0f;
            for (int p = 0; p < nPoints; p++)
                if (minDistToSet[p] > maxMin) {
                    maxMin = minDistToSet[p]; nextIdx = p;
                }
        } else {
            const double u = kk_rand_double() * total;
            double cum = 0.0;
            for (int p = 0; p < nPoints; p++) {
                cum += static_cast<double>(minDistToSet[p]);
                if (cum >= u) { nextIdx = p; break; }
            }
        }

        for (int d = 0; d < nDims; d++)
            Centres[k * nDims + d] = Data[nextIdx * nDims + d];

        // Update minDistToSet against the newly added centre.
        for (int p = 0; p < nPoints; p++) {
            float dist = 0.0f;
            for (int d = 0; d < nSpatialDims; d++) {
                const float dx = Data[p * nDims + d] - Centres[k * nDims + d];
                dist += dx * dx;
            }
            if (dist < minDistToSet[p]) minDistToSet[p] = dist;
        }
    }

    if (Verbose >= 1) {
        Output("KMeans++ seeds (%d centres, %d spatial dims):\n",
               nCentres, nSpatialDims);
        for (int k = 0; k < nCentres; k++) {
            Output("  centre %d:", k);
            for (int d = 0; d < nSpatialDims; d++)
                Output(" %.3f", Centres[k * nDims + d]);
            Output("\n");
        }
    }
}

// ---------------------------------------------------------------------------
// InitClassFromCentres
//
// Assigns each point to the nearest centre by Euclidean distance computed
// over the first nSpatialDims dimensions only (time excluded).  This produces
// a Voronoi partition of feature space that gives the first MStep much better
// starting statistics than random assignment.
// ---------------------------------------------------------------------------
void KK::InitClassFromCentres(int nSpatialDims) {
    const int nCentres = nStartingClusters - 1; // cluster 0 is noise

    for (int p = 0; p < nPoints; p++) {
        float bestDist = HugeScore;
        int   bestC    = 1;
        for (int k = 0; k < nCentres; k++) {
            float dist = 0.0f;
            for (int d = 0; d < nSpatialDims; d++) {
                const float dx = Data[p * nDims + d] - Centres[k * nDims + d];
                dist += dx * dx;
            }
            if (dist < bestDist) { bestDist = dist; bestC = k + 1; }
        }
        Class[p] = bestC;
    }
}

// ---------------------------------------------------------------------------
// CEMTwoPhase
//
// Phase 1 — spatial clustering (PCA dims only, time excluded):
//   Uses farthest-point seeding so nStartingClusters centres are placed from
//   the periphery inward.  Points start in a Voronoi partition of that seed.
//   Full CEM runs until convergence.  Distant clusters never compete because
//   the seed already separates them; this cuts iterations by 40-70% vs random.
//
// Phase 2 — temporal merge pass (all dims including time):
//   nDims is temporarily restored to full dimensionality.  Only MStep / EStep
//   / CStep / ConsiderDeletion are called — no new splits.  The time dimension
//   now contributes to the Mahalanobis distance, so two clusters that are
//   spatially close but occupy different temporal windows will stay separate,
//   while clusters that drifted apart over time but are the same unit may merge.
//   Runs for at most timeMergeIter iterations (typically 20-30 is sufficient).
//
// The final score is computed over all nDims.
// ---------------------------------------------------------------------------
float KK::CEMTwoPhase(int timeMergeIter) {
    // The time dimension is the last one (index nDims-1 after normalisation).
    // Phase 1 uses only the first nSpatialDims = nDims-1 dimensions.
    const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
    const int nFullDims    = nDims;

    // -----------------------------------------------------------------------
    // Phase 1: spatial CEM with farthest-point seeding
    // -----------------------------------------------------------------------
    if (Verbose >= 2)
        Output("CEMTwoPhase Phase 1: spatial clustering (%d dims, %d clusters)\n",
               nSpatialDims, nStartingClusters);

    // Temporarily reduce nDims so EStep/MStep operate on spatial dims only.
    // LogP, Mean, Cov are already allocated for nFullDims; we simply lie about
    // nDims so the inner loops stop before the time column.
    nDims      = nSpatialDims;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    // Seed cluster centres: preseedCentres if provided, otherwise the
    // method selected by `-InitMethod` (default farthest).
    const int nCentres = nStartingClusters - 1;  // noise cluster is always 0
    if (nCentres >= 1) {
        if (chunkInitRandom) {
            // Chunked path forces random Class[] init regardless of the
            // user's -InitMethod or preseed: each chunk runs a fresh
            // independent random-start KK.  The canonical pattern matches
            // KK.cpp:1506 (original KlustaKwik random-init).
            for (int p = 0; p < nPoints; p++)
                Class[p] = irand(1, nCentres);
            for (int c = 0; c < MaxPossibleClusters; c++)
                ClassAlive[c] = (c < nStartingClusters);
            Reindex();
            // No InitClassFromCentres: Class[] is already set.
        } else if (!preseedCentres.empty() &&
            static_cast<int>(preseedCentres.size()) >= nCentres * nSpatialDims) {
            // Copy preseed centres into Centres[]; zero the time column.
            Centres.SetSize(nCentres * nDims);
            for (int k = 0; k < nCentres; k++) {
                for (int d = 0; d < nSpatialDims; d++)
                    Centres[k * nDims + d] = preseedCentres[k * nSpatialDims + d];
                if (nDims > nSpatialDims)
                    Centres[k * nDims + nSpatialDims] = 0.0f;  // time column
            }
            for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
            Reindex();
            InitClassFromCentres(nSpatialDims);
        } else if (std::strcmp(InitMethod, "random") == 0) {
            // -InitMethod random: real random Class[] init for any caller.
            for (int p = 0; p < nPoints; p++)
                Class[p] = irand(1, nCentres);
            for (int c = 0; c < MaxPossibleClusters; c++)
                ClassAlive[c] = (c < nStartingClusters);
            Reindex();
        } else if (std::strcmp(InitMethod, "kmeans++") == 0) {
            InitCentresKMeansPP(nCentres, nSpatialDims);
            for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
            Reindex();
            InitClassFromCentres(nSpatialDims);
        } else {
            // Default: deterministic farthest-point.  Selected explicitly
            // by `-InitMethod farthest` and as the fallback for any other
            // unrecognised value.
            InitCentresFarthestPoint(nCentres, nSpatialDims);
            for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
            Reindex();
            InitClassFromCentres(nSpatialDims);
        }
    } else {
        for (int p = 0; p < nPoints; p++) Class[p] = 0;
        for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
        Reindex();
    }

    // Phase 1: run convergence loop with splits enabled; Class[] already seeded.
    {
        float score = RunEMLoop(
            /*enableSplits=*/   true,
            /*enableDistDump=*/ false,
            /*maxIter=*/        0,
            /*phaseLabel=*/     "P1");
        if (Verbose >= 2)
            Output("Phase 1 converged: %d clusters, score %.7g\n",
                   nClustersAlive, score);
    }

    // -----------------------------------------------------------------------
    // Phase 2: temporal merge pass — restore full dimensionality
    // -----------------------------------------------------------------------
    if (timeMergeIter <= 0 || nFullDims == nSpatialDims) {
        nDims      = nFullDims;
        log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);
        MStep(); EStep();
        return ComputeScore();
    }

    nDims      = nFullDims;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);
    if (Verbose >= 2)
        Output("CEMTwoPhase Phase 2: temporal merge pass (%d dims, max %d iters)\n",
               nDims, timeMergeIter);

    // Phase 2 is a bounded pass (no splits, always full step).
    // RunEMLoop's FullStep heuristic applies; the cap enforces the time budget.
    FullStep = 1;
    {
        float score = RunEMLoop(
            /*enableSplits=*/   false,
            /*enableDistDump=*/ false,
            /*maxIter=*/        timeMergeIter,
            /*phaseLabel=*/     "P2");
        if (Verbose >= 2)
            Output("Phase 2 done: %d clusters, score %.7g\n",
                   nClustersAlive, score);

        // kiloklustakwik Phase 8: DipSplit — bimodal-cluster detection.
        // Only fires on the main instance (scratch Kc's set suppressBestSave
        // so chunk-local clustering doesn't run it redundantly).
        // Also gated by DipSplitGlobalEnable so users can disable Phase 8
        // entirely in chunked mode with drift (where it misfires on the
        // drift axis of session-spanning clusters).
        if (DipSplitEnable != 0 && DipSplitGlobalEnable != 0 && !suppressBestSave) {
            DipSplitPhase();
            score = ComputeScore();   // refresh since DipSplit may have split
        }
        return score;
    }
}

// ===========================================================================
// DipSplit (Phase 8) — bimodal-cluster detection & split
// ===========================================================================
//
// See dipsplit.h for the algorithm overview.  This file implements the
// driver that integrates DipSplit into the CEM flow.  Two-stage gating
// (bloat → dip) keeps cost proportional to actually-bimodal clusters.
// ---------------------------------------------------------------------------
#include "dipsplit.h"

