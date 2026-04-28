// KK.h — C++17 modernised KlustaKwik clustering engine
// Additions over v1.7:
//   - Farthest-point seeding: InitCentresFarthestPoint / InitClassFromCentres
//   - Two-phase CEM:       CEMTwoPhase(timeMergeIter)
//   - Three-phase chunked: RunChunkedCEM(...)  — parallel over chunks via OpenMP
#pragma once
#include <unordered_set>
#include "Array.h"
#include "KlustaSave.h"
extern KlustaSave kSv;  // global in KlustaKwik.cpp
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

    // Return the active KlustaSave: per-worker if pKsv is set, else global kSv.
    KlustaSave&       ksv()       { return pKsv ? *pKsv : ::kSv; }
    const KlustaSave& ksv() const { return pKsv ? *pKsv : ::kSv; }
    void  LoadData();
    // Deep-copy this KK's clustering state into `out` for use as an
    // independent ParallelK worker.  `out` must be a default-constructed
    // KK (so its `gpu` is null and arrays are empty).  After the call,
    // `out` is set up identically to *this for an independent CEM run
    // on the CPU (gpu is left null, so all EM math runs on the host).
    void  cloneInto(KK& out, int ompTeamSize = 0) const;
    float Penalty(int n) const;
    float ComputeScore() const;
    // Per-phase quality summary printed to stderr.  See ReportClusterQuality
    // implementation in KK.cpp for metric definitions.  `phaseLabel` is a
    // short tag that appears in the log line, e.g. "Phase 1", "Phase 7".
    void  ReportClusterQuality(const char* phaseLabel) const;
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
    // Centre seeding for CEMTwoPhase
    // -----------------------------------------------------------------------
    void InitCentresFarthestPoint(int nCentres, int nSpatialDims);
    // K-means++ D²-weighted random seeding.  Picks centre 0 uniformly at
    // random; each subsequent centre is sampled with probability proportional
    // to its squared distance from the nearest already-chosen centre.
    // Compared with farthest-point, this is randomised (different seeds on
    // different runs even for identical data) but keeps the spread that makes
    // farthest-point work — the D²-weighting is the original Arthur & Vassilvitskii
    // 2007 algorithm, which has an O(log k) expected approximation guarantee.
    // Selected via -InitMethod kmeans++ at the driver level.
    void InitCentresKMeansPP(int nCentres, int nSpatialDims);
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
        std::vector<float>   mean;    // [nDims]
        std::vector<float>   cov;     // [nDims*nDims], upper triangle populated
        std::vector<int16_t> meanWav; // [nChan*nSamples] channel-major, for template matching
    };

    int   MergeChunkModels(std::vector<ChunkModel>& models,
                           int   nSpatialDims,
                           float mergeThresh,
                           const std::vector<std::unordered_map<int,int>>& overlapVotes);

    // Post-Phase-2 per-cluster subspace reclustering.
    // Projects each global cluster into its top-subspaceDims eigenvector subspace
    // and runs CEM to find splits that are hidden in the full feature space.
    void SubspaceReclusterPass(int subspaceDims);

    // Per-chunk subspace reclustering — runs BEFORE Phase 1.5 and Phase 2.
    // For each per-chunk local cluster, projects its spikes into the top-subspaceDims
    // eigenvector subspace and tries to split it.  Updates perChunkClass and
    // perChunkModels in-place so downstream realignment and cross-chunk matching
    // see purer cluster models.
    // Within-chunk merge pass — runs after Phase 1.5 waveform realignment,
    // before Phase 2 cross-chunk matching.  Uses updated Data[] features to
    // merge over-split local clusters within each chunk.
    // Post-Phase-2 global waveform realignment.
    // Groups spikes by global Class[] assignment, builds per-cluster mean
    // waveform from .spk, runs xcorr alignment, then re-extracts from .fil.
    // This is what Klusters does interactively — here it runs automatically
    // after cross-chunk merging so all downstream analysis sees aligned waveforms.
    void GlobalRealignPass(int iters, float minXcorrScore,
                           int nChan, int nSamplesPerSpike, int bytesPerSample);

    // Within-chunk xcorr template matching — merges clusters whose mean
    // waveforms have normalised xcorr >= minScore (mutual best match).
    // Runs after Phase 1.5 realignment and meanWav harvest, before Phase 2.
    int  WithinChunkTemplateMatch(
        const std::vector<std::vector<int>>& chunkPoints,
        std::vector<std::vector<int>>&        perChunkClass,
        std::vector<std::vector<ChunkModel>>& perChunkModels,
        int nChan, int nSamplesPerSpike, float minScore);

    void WithinChunkMerge(
        const std::vector<std::vector<int>>& chunkPoints,
        std::vector<std::vector<int>>&        perChunkClass,
        std::vector<std::vector<ChunkModel>>& perChunkModels,
        int nSpatialDims, float mergeThresh);

    void SubspaceReclusterPerChunk(
        int subspaceDims,
        const std::vector<std::vector<int>>& chunkPoints,
        std::vector<std::vector<int>>&        perChunkClass,
        std::vector<std::vector<ChunkModel>>& perChunkModels,
        int nFullDims);

    // Refractory-period guided split: after SubspaceReclusterPerChunk, for
    // each cluster whose ISI violation rate exceeds minContamRate, attempt a
    // BIC-checked 2-cluster split seeded by violator vs non-violator centroids.
    void RefractorySplitPerChunk(
        const std::vector<std::vector<int>>& chunkPoints,
        std::vector<std::vector<int>>&        perChunkClass,
        std::vector<std::vector<ChunkModel>>& perChunkModels,
        int nFullDims,
        float refractSamples,   // absolute refractory period in raw samples
        float minContamRate,    // minimum ISI contamination rate to trigger split (e.g. 0.01)
        float sessionSamples);  // total recording length in raw samples (for normalisation)

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

    // RefeaturizeFromShifts — used by Phase 4 (TimeShiftFinalize).
    // For each spike with a non-zero shift, re-extracts its waveform from
    // the .fil broadband file at (rawTs - shift - PeakSampleIndex), projects
    // through the PCA eigenvectors in SESSION.pca.N, re-normalises, and
    // writes back to Data[].  Falls back to circular shift from .spk when
    // .fil is unavailable.
    void RefeaturizeFromShifts(const std::vector<int>& spikeShifts,
                               int nChan, int nSamplesPerSpike);

    // WritePhase15Checkpoint — write corrected .spk and .fet to .pending
    // files using the committed shifts from m_cumShift[] and the features
    // already updated in Data[] by RefeaturizeFromShifts, then atomically
    // rename each .pending file over the original.  Name kept for legacy
    // on-disk compatibility; function is used by Phase 4 (shift commit).
    //
    // .spk: re-extracted from .fil at (rawTs + shift - PeakSampleIndex)
    // .fet: PCA features from Data[] + extTs (rawTs+shift) in last column
    // .res: NOT modified (detection timestamp is unchanged)
    void WritePhase15Checkpoint(const std::vector<int>& spikeShifts,
                                int nChan, int nSamplesPerSpike);

    // -----------------------------------------------------------------------
    // Post-split shift-probe refeaturization  (klustakwikExp)
    // -----------------------------------------------------------------------
    // Hard upper bound on the pre-shifted basis fan's half-width.  A fan of
    // (2N+1) candidates means (2*5+1) = 11 worst case, which fits in a
    // small stack array per thread (CPU) / per SIMT lane (GPU).  The
    // runtime value comes from the MaxTimeShift parameter.
    static constexpr int kTimeShiftNmax = 5;

    // TimeShiftBasis — cached PCA eigenvectors + per-dim normalisation
    // parameters loaded once at startup.  Populated by InitTimeShift() and
    // re-used by every TimeShiftSplitCluster() call (avoids re-reading
    // the .pca.N file on every split).
    //
    // Eigenvectors are pre-shifted and zero-padded at basis-build time: for
    // each δ in {-N, …, +N} we hold a separate copy of the (ch, k, j) tensor
    // whose row j corresponds to RAW-WAVEFORM sample index (recShift + j).
    // A negative δ pulls values in from "above" (earlier samples) with zero
    // outside the valid range; a positive δ pulls from "below" (later
    // samples).  Because PCs of biological spike waveforms taper to zero at
    // the window edges, the dropped/padded entry contributes negligibly.
    //
    // With this layout, the hot projection loop reads each raw sample
    // exactly once and accumulates (2N+1) feature values — no modulo, no
    // branching — via an inner j-loop that fans out over the candidate
    // array.
    struct TimeShiftBasis {
        int nChan       = 0;
        int data2use    = 0;
        int nComp       = 0;
        int recShift    = 0;
        bool isCentered = false;
        int  N          = 0;   // half-width; total candidates = 2N+1
        // Stderiv support: when true, the basis was loaded from .pcaD.N and
        // operates on spatially-derived waveforms in .spkD.N.  nChan is the
        // basis channel count (already reduced: nRawChannels − 1 for orders
        // 1 and 3; equal to nRawChannels for order 0/2).  rawChannels records
        // the group's raw channel count for .spkD indexing (stride).
        bool isStderiv  = false;
        int  rawChannels = 0;
        // Pre-shifted per-channel means (2N+1 copies).
        // Index: meanShifted[cand][ch][j] where cand=0..2N, δ=cand-N
        std::vector<std::vector<std::vector<double>>> meanShifted;
        // Pre-shifted per-channel eigenvectors (2N+1 copies).
        // Index: eigvecShifted[cand][ch][k*data2use + j]
        std::vector<std::vector<std::vector<double>>> eigvecShifted;
        bool valid() const { return nChan > 0 && data2use > 0 && nComp > 0 && N >= 0; }
        int  nCand() const { return 2 * N + 1; }
    };

    // Shift-probe state (instance-owned; zero-initialised).  Populated only
    // on the main KK object; K2/K3/Kc scratch objects leave it empty and
    // skip the probe (they operate on projected subspace copies, not the
    // full PCA-backed feature space, so the probe is meaningless there).
    TimeShiftBasis m_timeShiftBasis;
    std::vector<int>   m_cumShift;           // [nPoints] cumulative sample shift per spike (+ = later)
    bool               m_timeShiftReady = false;
    int                m_timeShiftCallCount = 0;
    int                m_timeShiftMaxAbs = 1;   // hard clamp on |cumShift|; = basis N

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // Opaque GPU handle for shift-probe kernels.  Allocated by InitTimeShift
    // when a GPU device is available; null otherwise (falls through to CPU
    // path).  Freed by CloseTimeShift().
    struct TimeShiftGpuCtx;
    TimeShiftGpuCtx* m_timeShiftGpuCtx = nullptr;
#endif

    // Load PCA basis + keep FILE* to .spk open for the duration of the run.
    // N_halfWidth: pre-shifted basis fan half-width (0..kTimeShiftNmax); the
    // probe will test 2N+1 candidate shifts per call.  Passing 0 disables.
    // Returns true if the basis loaded and .spk is readable — the probe is a
    // no-op when either fails (so a legacy run without .pca/.spk still works).
    bool InitTimeShift(int nChan, int nSamplesPerSpike, int N_halfWidth);
    void CloseTimeShift();

    // Test shifts in {-maxShiftAbs … +maxShiftAbs} (currently only ±1 supported
    // via defaults) on every spike in cluster `cid`.  For each candidate TOTAL
    // shift, circularly shifts the spike's waveform (read from .spk), projects
    // through the cached PCA basis, and assembles a trial feature vector.
    // Chooses the candidate whose cluster-wide sum-of-per-dim variance is
    // largest (= "max variance" criterion — the shift that most exposes
    // residual mixture structure for the next split round).  Commits the
    // chosen shift by writing the trial features into Data[], updating
    // the normalised timestamp column, and recording the delta in m_cumShift.
    // No disk writes.  Returns the number of spikes whose shift changed.
    int TimeShiftSplitCluster(int clusterId, int nChan, int nSamplesPerSpike);

    // Same algorithm as TimeShiftSplitCluster but takes an explicit list
    // of global spike indices.  Used by per-chunk split paths where the
    // affected spikes are a subset of a chunk's points and the global Class[]
    // has not yet been updated (chunks commit back via MergeChunkModels).
    int TimeShiftSplit(const std::vector<int>& globalSpikeIndices,
                                  int nChan, int nSamplesPerSpike);

    // Merge tightener probe: for each spike, pick the shift δ ∈ {-N, …, +N}
    // that MINIMISES its Mahalanobis² distance to the receiving cluster's
    // Gaussian (mean `destMean`, Cholesky factor `destChol` lower-triangular,
    // stored contiguously as [nDims²]).  Per-spike selection (NOT cluster-
    // wide): different spikes may commit different deltas.  Used inside
    // ConsiderDeletion after a cluster is deleted and its points reassigned
    // to their second-best cluster — the probe tightens their fit.
    // Returns the number of spikes whose committed shift changed.
    int TimeShiftMergeTighten(
        const std::vector<int>& globalSpikeIndices,
        int nChan, int nSamplesPerSpike,
        const float* destMean,     // [nDims]
        const float* destChol);    // [nDims²] lower-triangular

    // ---- Cluster-internal alignment (Phase 1.5 replacement) ---------------
    // Per-spike min-Mahalanobis² alignment of spikes against their OWN
    // cluster's Gaussian.  Equivalent problem statement to canonical xcorr
    // realignment but operates in feature space (using Chol rather than
    // raw-waveform cross-correlation) — which automatically weights
    // dimensions by their discriminative power.  Runs at the Phase 1.5
    // slot in the driver (Phase 1.5), replacing the canonical xcorr pass
    // that has been removed.
    //
    // Returns the number of spikes whose shift changed.  A no-op when the
    // cluster has < 2 spikes or when the probe isn't ready.
    int TimeShiftAlignCluster(int clusterId, int nChan, int nSamplesPerSpike);

    // Whole-session alignment pass: iterates alive clusters (skipping the
    // noise cluster and any cluster with < 5 spikes), calls
    // TimeShiftAlignCluster on each, and logs a summary.  Cluster stats
    // (Mean + Cholesky) must be fresh — caller is responsible for running
    // MStep + EStep beforehand.
    int TimeShiftAlignPhase(int nChan, int nSamplesPerSpike);

    // ---- DipSplit: bimodal-cluster detection & split (Phase 8) ----------
    //
    // For each alive cluster that passes a χ²-calibrated bloat gate (its
    // 90th-percentile Mahalanobis² exceeds F · χ²(nDims, 0.9)), project
    // onto its top-3 principal components and run a KDE-based valley test
    // on each.  If the deepest valley depth ≥ DipSplitValleyThresh, seed
    // a k=2 partition at the valley, refine with a few k-means iterations,
    // and accept the split only if BIC(k=2) < BIC(k=1) and each child
    // cluster has ≥ DipSplitMinSize members.
    //
    // Addresses the CEM failure mode where a bimodal cluster looks
    // approximately Gaussian in full-dim but reveals a valley in some 2D
    // projection.  Caller is responsible for running MStep + EStep before
    // DipSplitPhase (bloat gate needs fresh LogP[]) and after any accepted
    // split (cluster stats out of date).
    //
    // Returns the number of accepted splits.
    int  DipSplitPhase();

    // Single-cluster DipSplit attempt.  Used as the backing engine for
    // DipSplitPhase and also callable directly by interactive tools
    // (Klusters menu action).  Returns true when the split was accepted
    // and Class[] has been updated; caller must run MStep + EStep to
    // refresh cluster stats.
    bool DipSplitAttempt(int clusterId);

    // Extended form: returns a pointer-to-string-literal describing the
    // outcome, used by DipSplitPhase for aggregate logging.  Tags:
    // "split", "small", "not_bloated", "no_valley", "small_child",
    // "bic_worse", "no_free_id".
    bool DipSplitAttemptEx(int clusterId, const char*& reason_out);

    // -----------------------------------------------------------------------
    // Shift-aware merge decision (klustakwikExp)
    //
    // Evaluates whether ConsiderDeletion's candidate victim cluster becomes
    // mergeable once we allow its spikes to shift temporally into their
    // second-best clusters.  Addresses the common failure mode of a single
    // biological unit being fragmented into two clusters because the spike
    // detector triggered at different peak samples.
    //
    // For each destination cluster `dest` that would receive spikes from
    // the victim, finds the CLUSTER-WIDE δ ∈ {-N,…,+N} that minimises the
    // sub-batch's aggregate Mahalanobis² under dest's Gaussian.  A non-zero
    // δ is accepted only when it beats δ=0 by more than
    //     chi2(nDims, 0.95) × sub_batch_size
    // which gives each spike a per-spike "improvement budget" of the 95th
    // percentile of its null-hypothesis χ² distribution.  Otherwise δ=0 is
    // retained (no shift committed on that sub-batch).
    //
    // Selection is CLUSTER-WIDE per destination — a consistent winning δ
    // across all spikes going to the same destination is the signal of
    // systematic mis-alignment.  Per-spike scatter of individual best-δs
    // is noise and is suppressed by the χ² threshold.
    // -----------------------------------------------------------------------
    struct TimeShiftMergePlan {
        bool               valid              = false;
        float              lossReductionTotal = 0.0f;  // always ≤ 0; in natural log units
        // Per-spike commit data, same order as the input list (victim's spikes)
        std::vector<int>   globalIdx;    // [nMem]       global point indices
        std::vector<int>   chosenCand;   // [nMem]       cand∈[0,2N]; = N means no shift
        std::vector<float> chosenFeats;  // [nMem * nPCA] projected features at chosen cand
        std::vector<float> chosenTime;   // [nMem]       normalised time at chosen cand
        int                nPCA = 0;     // PCA feature count (for chosenFeats stride)
    };

    // Build a TimeShiftMergePlan for a candidate victim cluster.  Returns
    // true when the evaluation succeeded (probe is ready, victim has >=1
    // spike, .spk reads succeeded).  `plan.lossReductionTotal` is the
    // change in DeletionLoss[victim] that would be realised if the plan
    // were committed (always ≤ 0, since δ=0 is among the candidates and
    // we apply the χ² threshold before switching).
    bool TimeShiftMergeEvaluate(
        int victim, int nChan, int nSamplesPerSpike,
        TimeShiftMergePlan& plan);

    // Commit a plan: write chosen features into Data[], bump m_cumShift[],
    // and re-upload to GPU if needed.  Idempotent — running on an
    // already-committed plan is a no-op.
    void TimeShiftMergeCommit(const TimeShiftMergePlan& plan);

    // After CEM completes, re-run RefeaturizeFromShifts and WritePhase15Checkpoint
    // with m_cumShift as the final shift vector.  This is the ONLY point at
    // which .fil re-extraction and .spk/.fet rewrites occur (the probe itself
    // is pure in-memory circular shift — wrap-around at ±1 sample is
    // negligible relative to the PCA basis vectors).
    void TimeShiftFinalize(int nChan, int nSamplesPerSpike);

private:
    // Open .spk/.spkD once and cache either a FILE handle or an mmap
    // pointer for the duration of the probe run.  Opened by InitTimeShift(),
    // closed by CloseTimeShift().
    //
    // The mmap path (m_timeShiftSpkMap != nullptr) is preferred when
    // available — the kernel page cache gives us shared memory semantics
    // across subsequent cluster probes without re-reading the file, and
    // cluster-scatter random access avoids fseeko/fread overhead.  The
    // FILE* path is retained as a fallback (e.g. for NFS mounts where
    // MAP_PRIVATE semantics differ).
    FILE*   m_timeShiftSpkFp  = nullptr;
    void*   m_timeShiftSpkMap = nullptr;  // mmap base pointer (or nullptr)
    size_t  m_timeShiftSpkLen = 0;        // mmap length in bytes

    // Reusable per-cluster scratch buffers.  Resized on demand; cleared between
    // clusters so high-water-mark is retained.
    std::vector<int16_t> m_timeShiftWaveScratch;        // [waveSamples] one spike
    std::vector<float>   m_timeShiftTrialFeats;         // [3 candidates × nMem × (nDims-1)]
    std::vector<float>   m_timeShiftTrialTime;          // [3 candidates × nMem]

    // Read one spike's full waveform (nChan × nSamplesPerSpike int16_t values)
    // into dst.  Branches internally on m_timeShiftSpkMap vs m_timeShiftSpkFp
    // so callers don't need to know which access mode init chose.  Returns
    // true on success; false if neither backing store is valid, bounds fail,
    // or I/O errors out.  Used by all three projection paths (TimeShiftSplit,
    // TimeShiftMergeTighten, TimeShiftMergeEvaluate).
    bool TimeShiftReadSpikeWave(int p, int waveSamples, int16_t* dst);

    // Apply SDIFF_ALLPAIRS + temporal first-difference in place to one spike's
    // waveform (nChan × nSamplesPerSpike int16_t, sample-major layout).  This
    // is the EXACT transformation process_extractspikes_stderiv applies when
    // writing .spkD, with saturation at the int16 boundary at both stages.
    //
    // Per-spike boundary: sdiff[-1, ch] = 0.  Matches the per-spike convention
    // of process_pca_stderiv (isolated spike windows) rather than the
    // streaming boundary used inside process_extractspikes_stderiv.  The
    // difference is a single-sample effect at t=0 and is far smaller than the
    // shift-probe approximation we're already accepting.
    //
    // Used by RefeaturizeFromShifts (before projection) and
    // WritePhase15Checkpoint (before writing .spkD.pending) to make the
    // round-trip .fil → (shifted window) → .spkD.pending produce outputs
    // bit-compatible with ndm_extractspikes_stderiv's writes.
    static void ApplySdiffAllpairsTemporalDiff(
        int16_t* wave, int nChan, int nSamplesPerSpike);

public:

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

    // Per-dimension normalisation parameters saved by LoadData() so
    // RefeaturizeFromShifts can apply identical normalisation to
    // re-projected PCA features.
    std::vector<float> dimMin_;    // [nDims] raw minima
    std::vector<float> dimRange_;  // [nDims] 1/(max-min), or 1 if flat

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
    int  ompTeamSize       = 0;    // 0 = use all OMP threads; set by ParallelK workers

    // Optional per-instance KlustaSave for parallel workers.
    // When non-null, ksv() returns *pKsv instead of the global kSv.
    // Null by default — global kSv used in the normal single-worker path.
    KlustaSave* pKsv       = nullptr;
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

    // ── Rule of three / five: explicit non-copyable, non-movable ─────────────
    // KK owns a `gpu` resource through ~KK(); a default copy would alias
    // the pointer, leading to a double-free when both copies are destroyed.
    // The default move would shallow-copy `gpu` and leave the source's
    // pointer non-null, with the same hazard.
    //
    // The codebase initialises worker KKs via `vector<KK>(N)` (default
    // ctor, no copy/move) and configures them via field assignment or via
    // the `cloneInto(out, ...)` helper.  All currently-needed paths work
    // without copy or move, so we delete both to make accidental copies
    // (or `vector<KK>::push_back`) a compile error rather than a runtime
    // double-free.
    //
    // Default ctor is restored explicitly because = delete on copies/moves
    // would otherwise also suppress the implicit default ctor.
    KK() = default;
    KK(const KK&)            = delete;
    KK& operator=(const KK&) = delete;
    KK(KK&&)            = delete;
    KK& operator=(KK&&) = delete;
};
