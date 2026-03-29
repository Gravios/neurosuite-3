// KK.h — C++17 modernised KlustaKwik clustering engine
// Additions over v1.7:
//   - Farthest-point seeding: InitCentresFarthestPoint / InitClassFromCentres
//   - Two-phase CEM:       CEMTwoPhase(timeMergeIter)
//   - Three-phase chunked: RunChunkedCEM(...)  — parallel over chunks via OpenMP
#pragma once
#include "Array.h"
#include "KK_cuda.h"   // no-op when USE_CUDA not defined
#include "KK_sycl.h"   // no-op when USE_SYCL not defined
#include "KK_hip.h"    // no-op when USE_HIP  not defined

// ---------------------------------------------------------------------------
// GPU dispatch macros — map backend-agnostic names to the active backend.
// KK.cpp uses gpu_upload_data / gpu_estep / etc. everywhere; the preprocessor
// selects the right prefix at compile time.  Adding a new backend only
// requires adding a new #elif block here and a new KK_<backend>.h/.cpp pair.
// ---------------------------------------------------------------------------
#if defined(USE_CUDA)
#  define gpu_upload_data    cuda_upload_data
#  define gpu_estep          cuda_estep
#  define gpu_mstep          cuda_mstep
#  define gpu_cstep          cuda_cstep
#  define gpu_deletion_loss  cuda_deletion_loss
#  define gpu_compute_score  cuda_compute_score
#  define gpu_download_logp  cuda_download_logp
#  define gpu_device_available() cuda_device_available()
#  define GPU_BACKEND_NAME   "CUDA"
#elif defined(USE_SYCL)
#  define gpu_upload_data    sycl_upload_data
#  define gpu_estep          sycl_estep
#  define gpu_mstep          sycl_mstep
#  define gpu_cstep          sycl_cstep
#  define gpu_deletion_loss  sycl_deletion_loss
#  define gpu_compute_score  sycl_compute_score
#  define gpu_download_logp  sycl_download_logp
#  define GPU_BACKEND_NAME   "SYCL"
#elif defined(USE_HIP)
#  define gpu_upload_data    hip_upload_data
#  define gpu_estep          hip_estep
#  define gpu_mstep          hip_mstep
#  define gpu_cstep          hip_cstep
#  define gpu_deletion_loss  hip_deletion_loss
#  define gpu_compute_score  hip_compute_score
#  define gpu_download_logp  hip_download_logp
#  define gpu_device_available() hip_device_available()
#  define GPU_BACKEND_NAME   "HIP"
#endif
#include <functional>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <vector>

class KK {
public:
    // -----------------------------------------------------------------------
    // Core EM
    // -----------------------------------------------------------------------
    void  AllocateArrays();
    void  AllocateCholeskyVecs();
    void  SaveBestMeans();
    void  LoadData();
    float Penalty(int n) const;
    float ComputeScore() const;
    void  MStep();
    void  EStep();
    int   CStep();   // returns number of points that changed class
    void  ConsiderDeletion();
    void  LoadClu(const char *StartCluFile);
    int   TrySplits();
    float CEM(const char *CluFile = nullptr, int recurse = 1);
    void  Reindex();

private:
    // Shared convergence loop body used by CEM() and CEMTwoPhase() Phase 1.
    // Runs MStep/EStep/CStep/ConsiderDeletion until convergence.
    // Returns the final score.
    //   enableSplits   — call TrySplits() every SplitEvery iterations
    //   enableDistDump — write LogP to Distfp (only used in CEM())
    //   maxIter        — iteration cap (0 = use global MaxIter)
    //   phaseLabel     — prefix for Verbose output (e.g. "P1", "P2")
    float RunEMLoop(bool enableSplits, bool enableDistDump,
                    int maxIter, const char *phaseLabel);

public:

    // -----------------------------------------------------------------------
    // Farthest-point seeding
    // -----------------------------------------------------------------------
    void InitCentresFarthestPoint(int nCentres, int nSpatialDims);
    void InitClassFromCentres(int nSpatialDims);

    // -----------------------------------------------------------------------
    // Two-phase CEM
    // Phase 1: spatial-only EM (dims 0..nDims-2) with farthest-point seeding.
    // Phase 2: short merge pass reintroducing the time dimension.
    // -----------------------------------------------------------------------
    float CEMTwoPhase(int timeMergeIter);

    // Reinitialise a pre-allocated KK scratch object for a new split trial
    // without freeing/reallocating its arrays.  Arrays were allocated at
    // maxPoints capacity; only nPoints is updated and point-indexed arrays
    // are zeroed to the new size.  Cluster-indexed arrays (Weight, Mean, Cov,
    // ClassAlive, AliveIndex) are small and zeroed entirely.
    // Must be called after the arrays were initially allocated via
    // AllocateArrays() with nPoints = maxPoints.
    void ReinitForSplit(int newNPoints, int newNDims, float newPenaltyMix);

    // -----------------------------------------------------------------------
    // Three-phase chunked CEM
    // -----------------------------------------------------------------------
    struct ChunkModel {
        int chunkIdx;
        int localClusterId;
        int globalClusterId;  // -1 until assigned by MergeChunkModels
        int nMembers;
        std::vector<float> mean; // [nDims]
        std::vector<float> cov;  // [nDims*nDims], upper triangle populated
    };

    int   MergeChunkModels(std::vector<ChunkModel>& models,
                           int   nSpatialDims,
                           float mergeThresh,
                           const std::vector<std::unordered_map<int,int>>& overlapVotes);

    float RunChunkedCEM(float chunkMinutes,
                        float samplingRate,
                        float mergeThresh,
                        int   globalMergeIter,
                        int   timeMergeIter,
                        float chunkOverlapMinutes   = 0.0f,
                        float chunkPreseedFraction  = 0.0f);

    // Overload that accepts externally-computed chunk boundaries (in seconds).
    // Used by KlustaKwik.cpp when -ChunkFile is provided.  The boundaries
    // vector must be sorted ascending; the first element is typically 0.0
    // and the last is the session end time.  Each consecutive pair defines
    // one chunk: [bounds[k], bounds[k+1]).
    float RunChunkedCEM(const std::vector<float>& chunkBoundsSec,
                        float samplingRate,
                        float mergeThresh,
                        int   globalMergeIter,
                        int   timeMergeIter);

    // Phase 0 preseed: cluster a random subsample, return centres as a flat
    // vector [nCentres × nSpatialDims].  Returns empty on failure.
    std::vector<float> PreseedSubsampleCEM(float preseedFraction,
                                           int   nCentres,
                                           int   nSpatialDims,
                                           int   timeMergeIter);

    // Phase 1.5 waveform realignment — in-place .spk rewrite.
    //
    // Reads each spike's waveform from the .spk file, aligns it to its
    // chunk-cluster mean via integer circular cross-correlation, and writes
    // it back at the GLOBAL SPIKE INDEX p as the file offset:
    //
    //   offset = (off_t)p * nChan * nSamplesPerSpike * bytesPerSample
    //
    // This is the only correct seek formula.  Using a sequential slot counter
    // (slot++) causes overlap-duplicated spikes to be written past nPoints,
    // inflating the .spk from nPoints×wavelen to (nPoints+nOverlap)×wavelen —
    // which is the root cause of the count mismatch Klusters reports.
    //
    // Overlap spikes (p appears in two adjacent chunks) are processed twice
    // and written to the SAME slot; last-write-wins, file size is unchanged.
    //
    // nChan == 0 or nSamplesPerSpike == 0 → silently skipped (safe default).
    // bytesPerSample: 2 for ≤16-bit recordings (default), 4 for 32-bit.
    void RealignChunkWaveforms(
        const std::vector<std::vector<int>>& chunkPoints,
        const std::vector<std::vector<int>>& chunkClass,
        int nChan, int nSamplesPerSpike, int bytesPerSample = 2);

    // -----------------------------------------------------------------------
    // Pre-allocated scratch arrays — sized by AllocateArrays(), reused every
    // MStep / ConsiderDeletion call.  Eliminates ~125 K short-lived heap
    // allocs per typical run (one new[100] + delete[] per EM iteration each).
    // -----------------------------------------------------------------------
    Array<int>   m_classMembers;   // [MaxPossibleClusters] — point counts per cluster
    Array<float> m_deletionLoss;   // [MaxPossibleClusters] — score-loss estimate

    // -----------------------------------------------------------------------
    // Dimensions
    // -----------------------------------------------------------------------
    int nDims  = 0;
    int nDims2 = 0;

    // -----------------------------------------------------------------------
    // Cluster bookkeeping
    // -----------------------------------------------------------------------
    int   nStartingClusters = 0;
    int   nClustersAlive    = 0;
    int   nPoints           = 0;
    int   NoisePoint        = 1;
    int   FullStep          = 1;
    float penaltyMix        = 0.0f;

    // Hard floor for ConsiderDeletion: it will never delete a cluster if doing
    // so would bring nClustersAlive below this value.  Set from MinClusters by
    // the outer loop in KlustaKwik.cpp, and propagated to chunk sub-objects by
    // RunChunkedCEM so that per-chunk EM never drops below the user's requested
    // minimum — which is what causes under-clustering when MinClusters=MaxClusters.
    // Default 1 matches legacy behaviour (noise cluster always kept).
    int   minClustersAlive  = 1;

    // Raw time range for the last feature dimension, captured by LoadData()
    // before normalisation so RunChunkedCEM can convert chunkMinutes ->
    // normalised chunk boundaries without needing kSv.dataMin/dataMax.
    float timeRawMin = 0.0f;
    float timeRawMax = 1.0f;

    // Precomputed constant: log(2π)^(nDims/2).
    // Set by AllocateArrays() and updated whenever nDims changes (CEMTwoPhase
    // temporarily reduces nDims for the spatial phase).  Avoids recomputing
    // the transcendental inside the EStep inner loop (optimisation #11).
    float log2piHalf = 0.0f;

    // -----------------------------------------------------------------------
    // Data arrays
    // -----------------------------------------------------------------------
    Array<float> Data;       // [nPoints x nDims]  point-major
    Array<float> Centres;    // [MaxPossibleClusters x nDims]  farthest-point seeds
    Array<float> Weight;     // [MaxPossibleClusters]
    Array<float> Mean;       // [MaxPossibleClusters x nDims]
    Array<float> Cov;        // [MaxPossibleClusters x nDims²]  upper-tri stored
    Array<float> LogP;       // [MaxPossibleClusters x nPoints]  cluster-major (c*nPoints+p)
    Array<int>   Class;      // [nPoints]
    Array<int>   OldClass;   // [nPoints]
    Array<int>   Class2;     // [nPoints]
    Array<int>   BestClass;  // [nPoints]
    Array<int>   ClassAlive; // [MaxPossibleClusters]
    Array<int>   AliveIndex; // [MaxPossibleClusters]

    // -----------------------------------------------------------------------
    // Per-instance Cholesky storage — flat contiguous allocation
    //
    // Previously: unique_ptr<vector<Array<float>>> holding MaxPossibleClusters
    // separate heap blocks.  100 separate allocations (old design) meant:
    //   - 100 distinct TLB entries in the EStep hot path
    //   - pointer-chase overhead on every separate block access
    //
    // Now: two flat std::vector<float> of size MaxPossibleClusters * nDims²,
    // laid out as [cluster 0 | cluster 1 | ... | cluster K-1].
    // All Cholesky data is contiguous → single TLB entry, cache-line friendly.
    // Access: cholFlat.data() + c * nDims2
    //
    // Allocated by AllocateCholeskyVecs(); sized to MaxPossibleClusters * nDims2.
    // -----------------------------------------------------------------------
    std::vector<float> cholFlat;      // [MaxPossibleClusters * nDims²]
    std::vector<float> bestCholFlat;  // [MaxPossibleClusters * nDims²]

    // When true, CEMTwoPhase inner loops skip SaveBestMeans() and do not
    // update kSv.BestScoreSave.  Set automatically on chunk sub-objects so
    // parallel per-chunk EM cannot corrupt the outer loop's best-score state.
    bool suppressBestSave  = false;
    // When non-empty, CEMTwoPhase uses these as initial Voronoi centres
    // instead of running InitCentresFarthestPoint.
    // Layout: [nCentres × nDims], spatial dims only (time column = 0).
    std::vector<float> preseedCentres;

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // GPU context — allocated by LoadData() when a device is present.
    // nullptr on chunk sub-objects (K2/K3/Kc) which always run on the CPU.
    // Freed by ~KK() when non-null.
    KK_GPU *gpu = nullptr;

    ~KK() { if (gpu) { gpu->free_all(); delete gpu; } }
#endif
};
