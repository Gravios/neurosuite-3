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
    void  AlocateCholeskyVecs();
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

public:
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
    // Per-instance Cholesky storage
    //
    // Moved from KlustaSave (global singleton) onto KK so that chunk
    // sub-objects are fully self-contained and safe to run in parallel
    // threads.  Allocated by AlocateCholeskyVecs(); freed automatically
    // when the KK instance is destroyed (unique_ptr).
    // -----------------------------------------------------------------------
    std::unique_ptr<std::vector<Array<float>>> pChol;
    std::unique_ptr<std::vector<Array<float>>> pBestChol;

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
