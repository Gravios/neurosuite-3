/***************************************************************************
                   KK_chunked.cpp
                   --------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Chunked clustering pipeline for the KK class: subsample preseeding (with its
 on-disk preseed cache), the two RunChunkedCEM drivers, per-cluster and
 re-CEM per-chunk passes, and the numbered pipeline phases (4c remix,
 8 variance-split, 9 CCG merge, 15 checkpoint), split out of KK.cpp.
 Chunk-model merging (Phase 6, MergeChunkModels) is in KK_chunk_merge.cpp
 and Phase 2b mode 3 (RunPhase2bMode3Chunk) in KK_phase2b_mode3.cpp.
 Carries the preseed-cache anonymous namespace (kPreseedMagic,
 PreseedCacheHeader, StatFetFile, TryLoadPreseedCache, SavePreseedCache) used
 by PreseedSubsampleCEM.  ChunkReCEMPerChunk reaches the shared numerical
 kernel RunVBGMM through KK_internal.h.  Member definitions of the KK class
 declared in KK.h.
 ***************************************************************************/
#include "KK.h"
#include "KK_internal.h"
#include "KlustaKwik.h"
#include <neurosuite/core/dipsplit.hpp>
#include "proxy_isi.h"
#include "clust_quality.h"
#include "realign_xcorr.h"
#include "realign_center.h"
#include "klusters_realign.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <unordered_map>

// ── Phase 9 CCG refractory-dip merge helpers (testing) ────────────────────
namespace {

// Count cross-cluster spike pairs within a flank window, split into the central
// (|dt|<=refr) and flank (refr<|dt|<=flank) bins.  A,B are sorted spike times
// (any consistent unit).  O(|A|+|B|+pairs-in-window).  Used by Phase9CCGMerge.
void p9_ccgCounts(const std::vector<float>& A, const std::vector<float>& B,
                  double refr, double flank, long& center, long& flankN) {
    center = 0; flankN = 0;
    size_t lo = 0, hi = 0;
    for (size_t i = 0; i < A.size(); i++) {
        const double t = static_cast<double>(A[i]);
        while (lo < B.size() && static_cast<double>(B[lo]) <  t - flank) lo++;
        while (hi < B.size() && static_cast<double>(B[hi]) <= t + flank) hi++;
        for (size_t j = lo; j < hi; j++) {
            const double dt = std::fabs(static_cast<double>(B[j]) - t);
            if (dt <= refr)       center++;
            else if (dt <= flank) flankN++;
        }
    }
}

// Pearson correlation of two equal-length vectors (shape gate, scale/offset
// invariant so drift that scales/shifts the feature pattern still scores high).
double p9_pearson(const std::vector<float>& a, const std::vector<float>& b) {
    const int n = static_cast<int>(a.size());
    if (n < 2 || static_cast<int>(b.size()) != n) return 0.0;
    double ma = 0, mb = 0;
    for (int i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; i++) {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    const double den = std::sqrt(saa * sbb);
    return (den > 0) ? sab / den : 0.0;
}

}  // namespace

#include <unordered_set>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <omp.h>






// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// PreseedSubsampleCEM — cache helpers
//
// On-disk format (little-endian; KKE only runs on x86_64 / aarch64 in
// practice).  Header followed by the flat centres array:
//
//   char     magic[8]        = "KKEPRSD1"   // KKE PReSeeD format v1
//   uint32_t headerSize      = sizeof(header bytes that follow magic)
//   uint64_t fetMtimeSec     = mtime of input .fet file
//   uint64_t fetSize         = bytes of input .fet file
//   int32_t  nPoints
//   int32_t  nDims
//   int32_t  nSpatialDims
//   int32_t  nCentres        = nFound returned by the CEM
//   float    preseedFraction
//   int32_t  randomSeed
//   int32_t  maxClusters
//   float    penaltyMix
//   int32_t  timeMergeIter
//   ── then nCentres * nSpatialDims float32 values ──
//
// Cache is rejected (recompute) if ANY field differs from the current
// run's value.  Input-file mtime+size guards against changing input
// data without changing parameters.
// ---------------------------------------------------------------------------
namespace {

constexpr char kPreseedMagic[8] = {'K','K','E','P','R','S','D','1'};

struct PreseedCacheHeader {
    char     magic[8];
    uint32_t headerSize;
    uint64_t fetMtimeSec;
    uint64_t fetSize;
    int32_t  nPoints;
    int32_t  nDims;
    int32_t  nSpatialDims;
    int32_t  nCentres;
    float    preseedFraction;
    int32_t  randomSeed;
    int32_t  maxClusters;
    float    penaltyMix;
    int32_t  timeMergeIter;
};

// Stat the input .fet file (canonical .fet or stderiv .fetD variant);
// returns false if neither exists.  Uses pickInputPath to match the
// same resolution logic the rest of KKE uses to actually open the .fet.
bool StatFetFile(uint64_t* outMtimeSec, uint64_t* outSize) {
    char path[STRLEN + 32];
    if (pickInputPath(path, sizeof(path), FileBase, "fet", ElecNo) < 0) {
        // Neither .fet.<n> nor .fetD.<n> exists — caller error path.
        return false;
    }
    struct stat st{};
    if (stat(path, &st) != 0) return false;
    *outMtimeSec = static_cast<uint64_t>(st.st_mtime);
    *outSize     = static_cast<uint64_t>(st.st_size);
    return true;
}

// Try to load cached preseed centres.  Returns empty vector on any
// mismatch or I/O failure.
std::vector<float> TryLoadPreseedCache(int    nPoints,
                                        int    nDims,
                                        int    nSpatialDims,
                                        float  preseedFraction,
                                        int    timeMergeIter) {
    if (PreseedCacheFile[0] == '\0') return {};

    FILE* fp = fopen(PreseedCacheFile, "rb");
    if (!fp) return {};   // first run, expected

    PreseedCacheHeader h{};
    if (fread(&h, sizeof(h), 1, fp) != 1) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache header read failed — recomputing\n");
        return {};
    }
    if (memcmp(h.magic, kPreseedMagic, 8) != 0 ||
        h.headerSize != sizeof(PreseedCacheHeader) - 8) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache magic/size mismatch — recomputing\n");
        return {};
    }

    uint64_t curMtime = 0, curSize = 0;
    if (!StatFetFile(&curMtime, &curSize)) {
        fclose(fp);
        Output("PreseedSubsampleCEM: input .fet stat failed — recomputing\n");
        return {};
    }

    // Validate every field.
    const bool valid =
        h.fetMtimeSec     == curMtime &&
        h.fetSize         == curSize  &&
        h.nPoints         == nPoints  &&
        h.nDims           == nDims    &&
        h.nSpatialDims    == nSpatialDims &&
        h.preseedFraction == preseedFraction &&
        h.randomSeed      == RandomSeed &&
        h.maxClusters     == MaxClusters &&
        h.penaltyMix      == PenaltyMix &&
        h.timeMergeIter   == timeMergeIter;

    if (!valid) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache key mismatch — recomputing "
               "(input or parameter changed since last run)\n");
        return {};
    }
    if (h.nCentres <= 0 || h.nCentres > 1000000) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache nCentres=%d out of range — recomputing\n",
               h.nCentres);
        return {};
    }

    std::vector<float> centres(
        static_cast<size_t>(h.nCentres) * nSpatialDims, 0.0f);
    const size_t want = centres.size();
    if (fread(centres.data(), sizeof(float), want, fp) != want) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache body read failed (wanted %zu floats) — "
               "recomputing\n", want);
        return {};
    }
    fclose(fp);

    Output("PreseedSubsampleCEM: cache HIT — loaded %d centres from %s\n",
           h.nCentres, PreseedCacheFile);
    return centres;
}

// Write computed centres to the cache file.  Errors are logged but not
// fatal; failing to write a cache shouldn't break the run.
void SavePreseedCache(const std::vector<float>& centres,
                      int    nPoints,
                      int    nDims,
                      int    nSpatialDims,
                      float  preseedFraction,
                      int    timeMergeIter) {
    if (PreseedCacheFile[0] == '\0') return;
    if (centres.empty()) return;

    uint64_t mtime = 0, size = 0;
    if (!StatFetFile(&mtime, &size)) {
        Output("PreseedSubsampleCEM: input .fet stat failed — cache not written\n");
        return;
    }
    FILE* fp = fopen(PreseedCacheFile, "wb");
    if (!fp) {
        Output("PreseedSubsampleCEM: cannot open %s for writing — cache not written\n",
               PreseedCacheFile);
        return;
    }

    PreseedCacheHeader h{};
    memcpy(h.magic, kPreseedMagic, 8);
    h.headerSize      = static_cast<uint32_t>(sizeof(PreseedCacheHeader) - 8);
    h.fetMtimeSec     = mtime;
    h.fetSize         = size;
    h.nPoints         = nPoints;
    h.nDims           = nDims;
    h.nSpatialDims    = nSpatialDims;
    h.nCentres        = static_cast<int32_t>(centres.size() / nSpatialDims);
    h.preseedFraction = preseedFraction;
    h.randomSeed      = RandomSeed;
    h.maxClusters     = MaxClusters;
    h.penaltyMix      = PenaltyMix;
    h.timeMergeIter   = timeMergeIter;

    const bool ok_h = (fwrite(&h, sizeof(h), 1, fp) == 1);
    const bool ok_b = (fwrite(centres.data(), sizeof(float), centres.size(), fp)
                       == centres.size());
    fclose(fp);
    if (!ok_h || !ok_b) {
        Output("PreseedSubsampleCEM: short write to %s — cache may be corrupt\n",
               PreseedCacheFile);
        return;
    }
    Output("PreseedSubsampleCEM: cache SAVED — %d centres to %s\n",
           h.nCentres, PreseedCacheFile);
}

}   // anonymous namespace


// ---------------------------------------------------------------------------
// PreseedSubsampleCEM
//
// Phase 0 for chunked CEM: randomly sample preseedFraction of all spikes,
// run a full CEMTwoPhase on the subset, and return the converged spatial
// cluster centres as a flat vector [nCentres × nSpatialDims].
//
// This gives every per-chunk CEM the same globally-informed starting point
// rather than independent farthest-point seeds.  The key benefit: units that
// persist across all chunks start near their true centres, so cross-chunk
// model matching sees more consistent cluster IDs and fewer spurious splits.
//
// Returns empty vector on failure (too few spikes, bad fraction, etc.).
//
// If PreseedCacheFile is set and the cache validates against the current
// inputs and parameters, the cached centres are returned without
// recomputing.  After a fresh compute, centres are written back to the
// cache for the next run.
// ---------------------------------------------------------------------------
std::vector<float> KK::PreseedSubsampleCEM(float preseedFraction,
                                            int   nCentres,
                                            int   nSpatialDims,
                                            int   timeMergeIter)
{
    if (preseedFraction <= 0.0f || preseedFraction > 1.0f || nCentres < 1) {
        Output("PreseedSubsampleCEM: invalid fraction %.3f or nCentres %d\n",
               preseedFraction, nCentres);
        return {};
    }

    // ── Try cache first ──
    {
        std::vector<float> cached = TryLoadPreseedCache(
            nPoints, nDims, nSpatialDims, preseedFraction, timeMergeIter);
        if (!cached.empty()) return cached;
    }

    const int nSub = std::max(nCentres * nSpatialDims * 3,
                              static_cast<int>(nPoints * preseedFraction));
    if (nSub >= nPoints) {
        Output("PreseedSubsampleCEM: subsample (%d) >= nPoints (%d) — "
               "skipping preseed, using farthest-point directly.\n", nSub, nPoints);
        return {};
    }

    Output("PreseedSubsampleCEM: sampling %d / %d spikes (%.1f%%) for %d centres\n",
           nSub, nPoints, 100.0f * nSub / nPoints, nCentres);

    // Random subsample without replacement using Fisher-Yates partial shuffle.
    std::vector<int> idx(nPoints);
    std::iota(idx.begin(), idx.end(), 0);
    // Use the global RandomSeed so runs are reproducible.
    std::mt19937 rng(static_cast<unsigned>(RandomSeed));
    for (int i = 0; i < nSub; i++) {
        std::uniform_int_distribution<int> dist(i, nPoints - 1);
        std::swap(idx[i], idx[dist(rng)]);
    }

    // Build subsample KK object.
    // nStartingClusters = nCentres + 1 (noise) so Phase 1 seeds at the target K.
    // MaxClusters and MaxPossibleClusters are extern globals, visible automatically.
    KK Ks;
    Ks.nDims                = nDims;
    Ks.nPoints              = nSub;
    Ks.nStartingClusters    = nCentres + 1;
    Ks.penaltyMix           = penaltyMix;
    Ks.suppressBestSave     = true;
    Ks.minClustersAlive     = 2;
    Ks.AllocateArrays();
    Ks.AllocateCholeskyVecs();

    for (int i = 0; i < nSub; i++) {
        const int p = idx[i];
        for (int d = 0; d < nDims; d++)
            Ks.Data[i * nDims + d] = Data[p * nDims + d];
    }

    Ks.CEMTwoPhase(timeMergeIter);

    const int nFound = Ks.nClustersAlive;
    Output("PreseedSubsampleCEM: converged to %d clusters\n", nFound);

    if (nFound < 1) return {};

    // Extract spatial means from the live clusters.
    // If fewer clusters found than requested, we return what we have;
    // CEMTwoPhase in each chunk will add more via splits as needed.
    std::vector<float> centres(nFound * nSpatialDims, 0.0f);
    for (int cc = 0; cc < nFound; cc++) {
        const int c = Ks.AliveIndex[cc];
        for (int d = 0; d < nSpatialDims; d++)
            centres[cc * nSpatialDims + d] = Ks.Mean[c * nDims + d];
    }

    // ── Persist for next run ──
    SavePreseedCache(centres, nPoints, nDims, nSpatialDims,
                     preseedFraction, timeMergeIter);

    return centres;
}


// RunChunkedCEM — three-phase temporal-chunk pipeline
//
// ---------------------------------------------------------------------------
// RunChunkedCEM overload — external boundary list (seconds)
//
// Converts the caller-supplied boundary times to normalised [0,1] fractions
// using the same timeRawMin/Max reference used by the uniform variant and
// builds chunkPoints from them.  Phases 1-8 below are this driver's OWN copy,
// not a delegation: they have drifted from the uniform driver (no preseed,
// deletion floor at the starting K, no -FusePerChunkStages, no oscillation
// guard, a different K-template/DipSplit order, and the only Stage 2.12 fiber
// pass).  doc/design/runchunkedcem-drivers.md lists every difference.
// ---------------------------------------------------------------------------
float KK::RunChunkedCEM(const std::vector<float>& chunkBoundsSec,
                         float samplingRate,
                         float mergeThresh,
                         int   globalMergeIter,
                         int   timeMergeIter)
{
    const int nFullDims    = nDims;
    const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
    const int timeDim      = nDims - 1;

    const float sessionSamples = timeRawMax - timeRawMin;
    if (sessionSamples <= 0.0f || samplingRate <= 0.0f || chunkBoundsSec.size() < 2) {
        Output("RunChunkedCEM(ext): degenerate boundaries — falling back.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // Convert boundary times (seconds) to normalised [0,1] fractions.
    // normBounds[i] = boundary_i_seconds * samplingRate / sessionSamples
    // (timeRawMin is the unnormalized start sample; for normalised data the
    //  start is 0, but we subtract it just as LoadData() does.)
    const int nBounds = static_cast<int>(chunkBoundsSec.size());
    const int nChunks = nBounds - 1;

    std::vector<float> normBounds(nBounds);
    for (int i = 0; i < nBounds; i++) {
        // boundary_samples = bound_sec * SR;  norm = boundary_samples / sessionSamples
        // Subtract timeRawMin so the normalised boundary matches the
        // [0,1] range that Data[p*nDims+timeDim] actually occupies.
        // Without this offset all boundaries are shifted right by
        // timeRawMin/sessionSamples, misassigning early spikes.
        normBounds[i] = (chunkBoundsSec[i] * samplingRate - timeRawMin) / sessionSamples;
        // Clamp to [0,1]
        if (normBounds[i] < 0.0f) normBounds[i] = 0.0f;
        if (normBounds[i] > 1.0f) normBounds[i] = 1.0f;
    }

    LockedStderr( "[Stage 1] Chunking (%.0f min, %d drift-adaptive chunks; "
                    "per-chunk random-init CEM)\n",
            sessionSamples / samplingRate / 60.0f, nChunks);
    Output("RunChunkedCEM(ext): session %.1f min, %d drift-adaptive chunks\n",
           sessionSamples / samplingRate / 60.0f, nChunks);

    // Assign each point to its chunk by binary search on normBounds.
    // A point with normalised time t belongs to chunk k where
    //   normBounds[k] <= t < normBounds[k+1].
    std::vector<std::vector<int>> chunkPoints(nChunks);
    for (int p = 0; p < nPoints; p++) {
        const float t = Data[p * nDims + timeDim];
        // upper_bound gives the first boundary strictly > t;
        // subtracting begin() gives the 1-based index of that boundary,
        // so chunk index = that index - 1, clamped.
        int k = static_cast<int>(
            std::upper_bound(normBounds.begin(), normBounds.end(), t)
            - normBounds.begin()) - 1;
        if (k < 0)       k = 0;
        if (k >= nChunks) k = nChunks - 1;
        chunkPoints[k].push_back(p);
    }

    // Merge undersized trailing chunks (same logic as the uniform variant).
    const int minSpikes = nStartingClusters * nSpatialDims * 3;
    for (int k = nChunks - 1; k >= 1; k--) {
        if (static_cast<int>(chunkPoints[k].size()) < minSpikes) {
            Output("  Chunk %d: %d spikes < %d minimum — merging into chunk %d.\n",
                   k, static_cast<int>(chunkPoints[k].size()), minSpikes, k - 1);
            for (int p : chunkPoints[k]) chunkPoints[k-1].push_back(p);
            chunkPoints[k].clear();
        }
    }
    chunkPoints.erase(
        std::remove_if(chunkPoints.begin(), chunkPoints.end(),
                       [](const std::vector<int>& v){ return v.empty(); }),
        chunkPoints.end());
    const int nActive = static_cast<int>(chunkPoints.size());

    if (nActive <= 1) {
        Output("Only one chunk after size-check merges — running CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }
    Output("Active chunks: %d\n", nActive);

    // ── Phases 1 / 2 / 3 are identical to the uniform variant. ─────────────
    // Rather than duplicating ~250 lines, we delegate: build a temporary
    // 'chunkMinutes' value that exactly reproduces the chunkPoints assignment
    // we already computed, then call the uniform variant with that value AND
    // pass our pre-built chunkPoints via a thin shim.
    //
    // Implementation note: we inline the phase 1/2/3 body here because the
    // uniform variant's phase 0 would re-compute boundaries and might not
    // reproduce identical chunkPoints.  The phase 1/2/3 code is factored
    // into a private helper _RunChunkedCEMFromPoints() shared by both
    // overloads.  Until that refactor lands, we inline the body.

    // ── Phase 1: per-chunk CEMTwoPhase (parallel) ───────────────────────────
    std::vector<ChunkModel> allModels;
    std::vector<int>        pointPacked(nPoints, -1);  // -1 sentinel: unwritten spikes become noise

    std::vector<std::vector<ChunkModel>> perChunkModels(nActive);
    std::vector<std::vector<std::pair<int,int>>> perChunkAssign(nActive);
    std::vector<std::vector<int>> perChunkClass(nActive);  // for realignment
    std::vector<float> perChunkScore(nActive, 0.0f);
    std::vector<int>   perChunkNClusters(nActive, 0);

    int maxChunkSize = 0;
    for (int k = 0; k < nActive; k++)
        maxChunkSize = std::max(maxChunkSize, static_cast<int>(chunkPoints[k].size()));

#ifdef _OPENMP
    // When running as a ParallelK worker, ompTeamSize limits the inner
    // team so that multiple workers can share the machine without fighting
    // for threads.  0 = use all available (normal non-parallel path).
    const int nThreads = (ompTeamSize > 0)
        ? ompTeamSize
        : omp_get_max_threads();
#else
    const int nThreads = 1;
#endif
    std::vector<KK> threadKc(nThreads);
    for (int t = 0; t < nThreads; t++) {
        threadKc[t].nDims             = nFullDims;
        threadKc[t].nPoints           = maxChunkSize;
        threadKc[t].nStartingClusters = nStartingClusters;
        threadKc[t].penaltyMix        = penaltyMix;
        threadKc[t].suppressBestSave  = true;
        threadKc[t].minClustersAlive  = nStartingClusters;
        threadKc[t].chunkInitRandom   = true;  // per-chunk random init
        threadKc[t].AllocateArrays();
        threadKc[t].AllocateCholeskyVecs();
    }

    const int runsPerChunk = (nRuns > 0) ? nRuns : 1;
    const int nFlatPhase1  = nActive * runsPerChunk;

    struct ChunkRunResult {
        float            score     = HugeScore;
        int              nClusters = 0;
        std::vector<int> cls;
    };
    std::vector<ChunkRunResult> flatResults(static_cast<size_t>(nFlatPhase1));
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k    = fi / runsPerChunk;
        const int nPts = static_cast<int>(chunkPoints[static_cast<size_t>(k)].size());
        flatResults[static_cast<size_t>(fi)].cls.assign(static_cast<size_t>(nPts), 0);
        flatResults[static_cast<size_t>(fi)].score = HugeScore;
    }

    LockedStderr( "[Stage 2.1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
            nThreads, nActive, runsPerChunk, nFlatPhase1);
        #pragma omp parallel for schedule(dynamic) default(none) \
        num_threads(nThreads) \
        shared(flatResults, chunkPoints, nActive, nFullDims, timeMergeIter, threadKc, stderr) \
        firstprivate(MaxPossibleClusters, nStartingClusters, penaltyMix, \
                     runsPerChunk, nFlatPhase1, HugeScore, RandomSeed)
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k   = fi / runsPerChunk;
        const int run = fi % runsPerChunk;
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        KK& Kc = threadKc[
#ifdef _OPENMP
            omp_get_thread_num()
#else
            0
#endif
        ];
        kk_seed_rng(kk_mix_seed(kk_mix_seed(static_cast<uint64_t>(RandomSeed),
                                            static_cast<uint64_t>(k)),
                                static_cast<uint64_t>(run)));
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = nStartingClusters;
        Kc.NoisePoint        = 1;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        const float runScore = Kc.CEMTwoPhase(timeMergeIter);

        ChunkRunResult& cr = flatResults[static_cast<size_t>(fi)];
        cr.score     = runScore;
        cr.nClusters = Kc.nClustersAlive;
        for (int i2 = 0; i2 < nPts; i2++)
            cr.cls[static_cast<size_t>(i2)] = Kc.Class[i2];

        #pragma omp critical
        { fprintf(stderr, "  [chunk %d/%d  run %d/%d] score=%.4g  nclusters=%d\n",
                  k + 1, nActive, run + 1, runsPerChunk,
                  runScore, Kc.nClustersAlive); }
    }

    // Serial reduction: pick best run per chunk, rebuild KK, harvest models.
    for (int k = 0; k < nActive; k++) {
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        float bestScore = HugeScore;
        int   bestRun   = 0;
        for (int run = 0; run < runsPerChunk; run++) {
            const float s = flatResults[static_cast<size_t>(k * runsPerChunk + run)].score;
            if (s < bestScore) { bestScore = s; bestRun = run; }
        }
        const ChunkRunResult& best = flatResults[
            static_cast<size_t>(k * runsPerChunk + bestRun)];

        KK& Kc = threadKc[0];
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = nStartingClusters;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        for (int i2 = 0; i2 < nPts; i2++) Kc.Class[i2] = best.cls[static_cast<size_t>(i2)];
        for (int c = 0; c < MaxPossibleClusters; c++) Kc.ClassAlive[c] = 0;
        for (int i2 = 0; i2 < nPts; i2++) Kc.ClassAlive[Kc.Class[i2]] = 1;
        Kc.nClustersAlive = 0;
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (Kc.ClassAlive[c]) Kc.AliveIndex[Kc.nClustersAlive++] = c;
        Kc.MStep();

        perChunkScore[k]     = bestScore;
        perChunkNClusters[k] = best.nClusters;

        auto& models = perChunkModels[k];
        for (int cc = 0; cc < Kc.nClustersAlive; cc++) {
            const int c = Kc.AliveIndex[cc];
            ChunkModel cm;
            cm.chunkIdx        = k;
            cm.localClusterId  = c;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(nFullDims, 0.0f);
            cm.cov.assign(nFullDims * nFullDims, 0.0f);
            for (int d = 0; d < nFullDims; d++)
                cm.mean[d] = Kc.Mean[c * nFullDims + d];
            for (int r = 0; r < nFullDims; r++)
                for (int col = r; col < nFullDims; col++)
                    cm.cov[r * nFullDims + col] =
                        Kc.Cov[c * Kc.nDims2 + r * nFullDims + col];
            for (int i = 0; i < nPts; i++)
                if (Kc.Class[i] == c) cm.nMembers++;
            models.push_back(std::move(cm));
        }

        auto& assign = perChunkAssign[k];
        assign.reserve(nPts);
        for (int i = 0; i < nPts; i++)
            assign.emplace_back(pts[static_cast<size_t>(i)],
                                k * MaxPossibleClusters + Kc.Class[i]);

        auto& classArr = perChunkClass[k];
        classArr.resize(nPts);
        for (int i = 0; i < nPts; i++) classArr[i] = Kc.Class[i];
    }

    LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.1");

    // ── Provisional Class[] seeding for Phase 2 ─────────────────────────────
    // Phase 1 ran on local threadKc[] objects; K1.Class[] is all-zero here.
    // Phase 2 SubspaceReclusterPerChunk needs alive cluster statistics.
    if (SubspaceRecluster > 0) {
        std::fill(Class.m_Data, Class.m_Data + nPoints, 0);
        std::fill(ClassAlive.m_Data, ClassAlive.m_Data + MaxPossibleClusters, 0);
        ClassAlive[0] = 1;
        for (int k_ps = 0; k_ps < nActive; ++k_ps) {
            const auto& pts_ps = chunkPoints[static_cast<size_t>(k_ps)];
            const auto& cls_ps = perChunkClass[static_cast<size_t>(k_ps)];
            for (int i_ps = 0; i_ps < static_cast<int>(pts_ps.size()); ++i_ps) {
                const int p_ps = pts_ps[static_cast<size_t>(i_ps)];
                const int c_ps = cls_ps[static_cast<size_t>(i_ps)];
                if (Class[p_ps] == 0 && c_ps > 0) {
                    Class[p_ps] = c_ps;  ClassAlive[c_ps] = 1;
                }
            }
        }
        Reindex();
        nDims = nFullDims;  nDims2 = nDims * nDims;
        MStep();
        for (int cc2 = 1; cc2 < nClustersAlive; ++cc2) {
            const int c2 = AliveIndex[cc2];
            if (Cholesky(Cov.m_Data + c2*nDims2, cholFlat.data() + c2*nDims2, nDims))
                ClassAlive[c2] = 0;
        }
        Reindex();
        Output("Provisional Class[] seeded: %d alive clusters\n", nClustersAlive);
    }

    // ── Phase 1a: per-cluster shift-probe alignment ────────────────────────
    //
    // For each alive cluster, picks the per-spike δ ∈ {-N,…,+N} that
    // minimises Mahalanobis² to the cluster's own Gaussian (using the
    // cluster's Mean + Cholesky-factored Cov).  Aligns spikes WITHIN each
    // cluster — not to a canonical peak sample.  The cluster's mean is
    // wherever Phase 1 CEM put it; this phase tightens spikes around that
    // centre, it does not move the centre to peakSampleIndex.
    //
    // Caveats inherited from the design:
    //  - Per-cluster myopia: spikes near a cluster boundary may be pulled
    //    deeper into the wrong cluster (the score sees only the assigned
    //    cluster's distribution, not neighbours').  The next EStep will
    //    re-evaluate, but on slightly distorted features.
    //  - Asymmetric search window: candOk[ci] enforces |baseCum + δ| ≤
    //    m_timeShiftMaxAbs, so spikes already near ±maxAbs see fewer
    //    candidates on the cap-side.  Intentional bound on cumulative
    //    drift, not a bug.
    //  - Pre-shifted PCA basis is fixed at InitTimeShift; large cumulative
    //    shifts (across many iterations) make the basis statistically
    //    less efficient but not incorrect.
    //
    // Stats are current at this point: the SubspaceRecluster=1 seeding
    // block above ran MStep + Cholesky.  When SubspaceRecluster=0 no
    // clusters are alive yet and the call no-ops cleanly (the loop in
    // TimeShiftAlignPhase iterates ClassAlive and finds nothing).
    RunAlignmentBlock(TimeShiftAlignAfterPhase1, "Align");

    // ── Phase 1b: per-chunk DipSplit ──────────────────────────────────────
    //
    // Catches bimodal/elongated clusters that the parametric Phase 1 CEM
    // missed.  Runs per-chunk before Phase 2 so subsequent passes operate
    // on the most-correctly-split inputs, and so cross-chunk merge in
    // Phase 6 sees the correct cluster count.  Replaces the old
    // post-Phase-7 global Phase 8 DipSplit.  No-op when DipSplitEnable=0.
    DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims,
                     "Stage 2.2");
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.2");
    RunAlignmentBlock(TimeShiftAlignAfterPhase1b, "Align");

    // ── Phase 2: per-chunk refractory split + subspace reclustering ────────
    //
    // P2.D: refractory split runs FIRST.  Refractory contamination is a
    // physical signal — two units sharing a cluster cannot satisfy
    // biological refractory periods — so it's a stronger prior than any
    // covariance-based statistical split.  Splitting on physics first gives
    // the subspace CEM cleaner inputs.
    //
    // The earlier order (subspace then refractory) had a failure mode: if
    // subspace incorrectly split a clean cluster (multiple-comparisons
    // false positive), the children might individually pass the 1% ISI
    // contamination threshold (each carrying half the violations) and the
    // over-split would stick.  Running refractory first locks in physically-
    // motivated splits before the statistical pass can over-fit.
    if (SubspaceRecluster > 0) {
        // Refractory-period guided split — catches mixtures by exploiting the
        // 1-neuron-per-refractory-window constraint.  Absolute refractory =
        // 1.5 ms × sampling rate samples.  Trigger when ISI contamination
        // rate >= 1%.
        if (SamplingRate > 0.0f) {
            const float refractSamp    = 1.5f * SamplingRate / 1000.0f;
            const float sessLenSamp    = timeRawMax - timeRawMin;
            LockedStderr( "[Stage 2.3] Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.3");
        }

        // Phase 2a: per-cluster ordinary CEM in the full feature space.
        // Replaces SubspaceReclusterPerChunk.  For each cluster in each
        // chunk, runs CEM with splits enabled to find bimodal substructure;
        // updates perChunkClass[] only.
        PerClusterCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.4");

        // Phase 2a.5: per-chunk DipSplit between Phase 2a and Phase 2b.
        //
        // Rationale: Phase 2a's per-cluster CEM is a parametric Gaussian
        // test that absorbs single-dimension bimodality by inflating the
        // covariance along the bimodal axis (the inflation penalty in BIC
        // is smaller than the constant cost of accepting a new cluster).
        // DipSplit looks for KDE valleys in 1D projections — exactly the
        // structure CEM misses.
        //
        // Inserting BEFORE Phase 2b gives Phase 2b finer-grained
        // warm-start clusters.  With Phase2bEnableSplits=0 (default
        // since patch 0007), Phase 2b only runs warm-start CEM plus
        // ConsiderDeletion — which converges faster from a finer
        // partition because boundary-spike reassignment is local and
        // ConsiderDeletion's per-iter K-loop catches any oversplits
        // cheaply.  The existing Phase 2.5 (after-Phase-2b) DipSplit
        // continues to run as a safety net.
        if (DipSplitEnable != 0 && DipSplitBeforePhase2b != 0) {
            DipSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims,
                "Stage 2.5");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.5");
        }

        // Phase 2a.6: per-chunk HullSplit (k-NN-graph connected components).
        // Complement to Phase 2a.5 DipSplit — catches multi-D topology
        // (two disconnected components in feature space) that 1D-projected
        // DipSplit misses.  Default off (HullSplitEnable=0).
        if (HullSplitEnable != 0) {
            HullSplitPerChunk(
                chunkPoints, perChunkClass, nFullDims, "Stage 2.6");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.6");
        }

        // Phase 2a.7: per-channel amplitude+phase bimodality split.  Reads
        // raw waveforms and tests each channel's (peak amp, peak time,
        // trough amp, trough time) 4-vector for 1D / 2D-angle-sweep KDE
        // valleys.  Catches the failure mode that DipSplit and HullSplit
        // both miss — two units differing by a small combination of
        // amplitude AND phase on a single channel.  Default off
        // (PerChannelSplitEnable=0).
        if (PerChannelSplitEnable != 0) {
            PerChannelSplitPerChunk(
                chunkPoints, perChunkClass,
                NbChannels, NbSamplesPerSpike, "Stage 2.7");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.7");
        }

        // Phase 2b: chunk-level warm-start CEM.  Lets boundary spikes
        // reassign across the new fine-grained label set and lets CEM
        // merge oversplit fragments via ConsiderDeletion.  Rebuilds
        // perChunkModels[] from the converged state.
        ChunkReCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.8");

        // Phase 2b.5: K-template chunk split (optional, off by default).
        // For each chunk, picks K well-isolated reference cluster means
        // and partitions every "source" cluster's spikes by their
        // nearest reference template.  Each viable (source, ref) bucket
        // becomes a new chunk-local cluster.  Phase 6 (cross-chunk
        // model matching) handles consolidation of the new clusters
        // into global units.  Enable with -KnnSplitPerChunkEnable 1.
        // -KnnSplitMode 0 (default): legacy nearest-template; mode 1:
        // klusters-faithful K-NN majority-vote (see wave_knn_split.h).
        if (KnnSplitPerChunkEnable) {
            if (KnnSplitMode == 1) {
                WaveKnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            } else {
                KnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            }
        }
        if (FullCemSplitEnable) {
            FullCemSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims);
        }
        // Phase 2.5: second per-chunk DipSplit pass.
        //
        // The Phase 1c DipSplit ran before subspace reclustering, so it
        // could only see bimodality in the parent clusters produced by
        // Phase 1 CEM.  PerClusterCEMPerChunk (Phase 2a) re-partitions
        // those parents into finer pieces via likelihood-based CEM with
        // splits enabled — but CEM is a parametric Gaussian test that
        // absorbs single-dimension bimodality by inflating variance
        // along the bimodal axis (the inflation penalty is smaller than
        // the constant cost of a new cluster).  Clusters that emerge
        // from Phase 2a with a clear KDE valley in one PC projection
        // but a covariance the parametric test accepts are exactly the
        // cases this second pass catches.
        //
        // Per-chunk (not global) for the same reason as Phase 1c: a
        // chunk's spikes don't span the session-drift range, so apparent
        // bimodality from drift is suppressed.
        DipSplitPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims,
            "Stage 2.9");
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.9");
    }


    // Phase 7 (Global EM) alignment also disabled: chunk means from
    // CEMTwoPhase are already blended by timeMergeIter and would produce
    // spurious shifts on clean data.  ConsiderDeletion / TimeShiftMergeEvaluate
    // handle the time-shift cases that actually occur (genuine same-unit
    // mergers across drift).

    // ── Post-Phase-2 alignment site ─────────────────────────────────────
    RunAlignmentBlock(TimeShiftAlignAfterPhase2, "Align");

    // Phase-ordering note: Phase 8 (global post-merge DipSplit) was
    // removed.  Its function is now served by per-chunk DipSplit at
    // Phase 1b and a second pass at Phase 2.5 — operating per-chunk
    // avoids the drift-axis false positives a global pass produces on
    // session-spanning clusters.  Phases 3 (mean-waveform harvest, the
    // [Stage 2.11 harvest] log tag below), 4, and 8 are intentionally non-major
    // phase numbers in the current pipeline; the comment block above
    // RunChunkedCEM enumerates which numbers are alive.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        LockedStderr( "[Stage 2.11 harvest] Mean waveform harvest (channel-major xcorr format)\n");
    // Populate ChunkModel::meanWav for template matching.
    // Done serially after the parallel chunk loop since all chunks share
    // the same .spk file handle and fseeko calls cannot be parallelised safely.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
        NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        char spkPathTM[STRLEN + 16];
        // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
        pickInputPath(spkPathTM, sizeof(spkPathTM), FileBase, "spk", ElecNo);
        FILE* spkTM = fopen(spkPathTM, "rb");
        if (spkTM) {
            for (int k = 0; k < nActive; k++) {
                const auto& pts  = chunkPoints[k];
                const auto& cls  = perChunkClass[k];
                const int   nPts = static_cast<int>(pts.size());

                // Build localClusterId → ChunkModel* map for this chunk
                std::unordered_map<int, ChunkModel*> lcToModel;
                for (auto& cm : perChunkModels[k])
                    if (cm.chunkIdx == k)
                        lcToModel[cm.localClusterId] = &cm;

                // Determine this chunk's time range so we can classify spikes
                // as left-edge (first 25%) / middle / right-edge (last 25%)
                // for the boundary-localised waveforms used in Phase 6's
                // cross-chunk xcorr.  Time = last feature dim, raw samples.
                float tMin = std::numeric_limits<float>::infinity();
                float tMax = -std::numeric_limits<float>::infinity();
                for (int i = 0; i < nPts; i++) {
                    const float t = Data[static_cast<size_t>(pts[i]) * nDims + (nDims - 1)];
                    if (t < tMin) tMin = t;
                    if (t > tMax) tMax = t;
                }
                const float tRange    = (tMax > tMin) ? (tMax - tMin) : 1.0f;
                const float tLeftEnd  = tMin + 0.25f * tRange;
                const float tRightBeg = tMin + 0.75f * tRange;

                // Accumulate per-cluster waveform sums.  Three accumulators
                // per cluster: full chunk, left-edge (first 25%), right-edge
                // (last 25%).  Every spike contributes to acc; spikes within
                // the edge windows additionally contribute to accLeft/Right.
                std::unordered_map<int, std::vector<int64_t>> acc, accLeft, accRight;
                std::unordered_map<int, int> nAcc, nAccLeft, nAccRight;
                std::vector<int16_t> row(static_cast<size_t>(wElems));
                for (int i = 0; i < nPts; i++) {
                    const int lc = cls[i];
                    if (lc == 0) continue;  // skip noise
                    if (!lcToModel.count(lc)) continue;
                    const int p2 = pts[i];
                    fseeko(spkTM,
                           static_cast<off_t>(p2) * wElems * sizeof(int16_t),
                           SEEK_SET);
                    if (fread(row.data(), sizeof(int16_t), wElems, spkTM)
                            != static_cast<size_t>(wElems)) continue;

                    // Apply committed shift (TimeShiftSplitEnable path):
                    // ensure the harvested waveform geometry matches the
                    // PCA features in Data[].
                    ShiftWaveformRowInPlace(row.data(), p2,
                                            NbChannels, NbSamplesPerSpike);

                    // Full-chunk accumulator
                    auto& a = acc[lc];
                    if (a.empty()) a.assign(static_cast<size_t>(wElems), 0);
                    // sample-major (.spk) → channel-major (XcorrDispatch)
                    for (int ch = 0; ch < NbChannels; ch++)
                        for (int s = 0; s < NbSamplesPerSpike; s++)
                            a[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                += row[static_cast<size_t>(s * NbChannels + ch)];
                    nAcc[lc]++;

                    // Edge accumulators.  A spike can only fall into one of
                    // the two edge windows (mutually exclusive at 25% each);
                    // middle-50% spikes contribute only to the chunk-wide mean.
                    const float t = Data[static_cast<size_t>(p2) * nDims + (nDims - 1)];
                    std::vector<int64_t>* edgeAcc = nullptr;
                    int* edgeN = nullptr;
                    if (t <= tLeftEnd)       { edgeAcc = &accLeft[lc];  edgeN = &nAccLeft[lc];  }
                    else if (t >= tRightBeg) { edgeAcc = &accRight[lc]; edgeN = &nAccRight[lc]; }
                    if (edgeAcc) {
                        if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                        for (int ch = 0; ch < NbChannels; ch++)
                            for (int s = 0; s < NbSamplesPerSpike; s++)
                                (*edgeAcc)[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                    += row[static_cast<size_t>(s * NbChannels + ch)];
                        (*edgeN)++;
                    }
                }
                // Finalise full-chunk meanWav
                for (auto& [lc, a] : acc) {
                    int n2 = nAcc[lc];
                    if (n2 == 0) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWav.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWav[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                }
                // Finalise meanWavLeft.  Require a minimum of 5 spikes for a
                // meaningful mean — below that, leave empty so Phase 6 falls
                // back to chunk-wide meanWav for this cluster.
                for (auto& [lc, a] : accLeft) {
                    const int n2 = nAccLeft[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavLeft.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavLeft[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersLeft = n2;
                }
                for (auto& [lc, a] : accRight) {
                    const int n2 = nAccRight[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavRight.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavRight[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersRight = n2;
                }
            }
            fclose(spkTM);
        }
    }
    // ── Stage 2.12: fiber consolidation (validated; see fiber_stage.h) ────────
    // Runs after the Stage 2.11 harvest, before cross-chunk Phase 5/6, so the
    // matcher sees consolidated fibers (curves) instead of the over-split
    // fragments the mean-template merge cannot close.  No-op unless
    // -FiberStageEnable 1.
    if (FiberStageEnable)
        FiberStagePerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims);

    // ── Phase 6 preflight: MergeThresh sanity check ─────────────────────────
    // The cross-chunk matching body follows the Phase 5 xcorr-iteration
    // block below.  This block only emits a warning if MergeThresh is
    // far outside chi2(nSpatialDims, 0.9999), which would make Phase 6
    // merges either too eager or never trigger.
    {
        const float d_    = static_cast<float>(nSpatialDims);
        const float chi2_9999 = d_ * std::pow(1.0f - 2.0f/(9.0f*d_) + 3.719f * std::sqrt(2.0f/(9.0f*d_)), 3.0f);
        const float chi2_99   = d_ * std::pow(1.0f - 2.0f/(9.0f*d_) + 2.326f * std::sqrt(2.0f/(9.0f*d_)), 3.0f);
        if (mergeThresh > chi2_9999 * 1.5f) {
            Output("WARNING: MergeThresh=%.1f is far above chi2(%d, 0.9999)=%.1f.\n"
                   "  Recommended: MergeThresh=%.1f (chi2(%d, 0.99))\n",
                   mergeThresh, nSpatialDims, chi2_9999, chi2_99, nSpatialDims);
        }
    }
    static const std::vector<std::unordered_map<int,int>> noOverlapVotes;
    // ── Phase 5: within-chunk circular xcorr template matching (iterated) ─
    // Loops until no new merges occur or 10 iterations.  After each merge pass
    // the surviving clusters need updated meanWav vectors (the merged cluster
    // mean changes when two sub-clusters are combined), so the Phase 3 harvest
    // re-runs at the top of each iteration before the next xcorr comparison.
    if (TemplateMatchScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int _tmMax = (TemplateMatchIters > 0) ? TemplateMatchIters : 10;
        int _altNetGrowthStreak = 0;

        // Per-iter convergence digest (printed as a table after the loop).
        struct Phase4DigestRow {
            int iter; int merges; int spikesSplit; int kNew;
            int totalClusters; double wallMs;
        };
        std::vector<Phase4DigestRow> _phase4Digest;

        // Reset the Phase 4 median-template cache at phase start (patch
        // 0052).  Bounds cache memory across the run; entries accumulate
        // within this phase's iters and are reused when cluster
        // membership is unchanged.
        m_medianCache.clear();

        auto _countClusters = [&]() {
            std::set<long long> uniq;
            for (int _k = 0; _k < nActive; _k++)
                for (int _c : perChunkClass[_k])
                    if (_c != 0) uniq.insert(
                        static_cast<long long>(_k) * MaxPossibleClusters + _c);
            return static_cast<int>(uniq.size());
        };

        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
            const auto _iterT0 = std::chrono::steady_clock::now();
            // Phase 4b: optional alternating KnnSplit inside the Phase 4
            // loop — runs BEFORE merge so each iter is split→harvest→merge.
            // The closing iter of the loop is template-merge-only, so its
            // output going to Phase 5 is template-converged (no raw split
            // products escape to downstream phases).
            //
            // Two guards against runaway split-domination:
            //   * iter cap (AlternatingSplitMergeMaxIters, default 2)
            //   * net-growth abort (if WaveKnnSplit produced more new
            //     clusters than template-match merged for two iters in a
            //     row, stop running split; merge-only iters continue up
            //     to TemplateMatchIters).
            int _nSpikesSplit = 0;
            int _kNewThisIter = 0;
            const bool _altGate =
                AlternatingSplitMergeEnable != 0 &&
                KnnSplitPerChunkEnable != 0 && KnnSplitMode == 1 &&
                _tmIter < AlternatingSplitMergeMaxIters &&
                !(AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                  _altNetGrowthStreak >= 2);
            if (_altGate) {
                std::vector<std::vector<int>> _beforeSplit = perChunkClass;
                std::set<int> _kBefore;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kBefore.insert(c);
                if (QualityWeightedSplitEnable) {
                    // Quality-routed: dispatcher computes ISI contamination
                    // + waveform variance for an oversampled pool and routes
                    // each neediest cluster to CEM or knn.  Replaces the
                    // direct WaveKnn+FullCem calls.
                    QualityWeightedSplitDispatch(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                } else {
                    WaveKnnSplitPerChunk(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    if (FullCemSplitEnable) {
                        FullCemSplitPerChunk(
                            chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    }
                }
                for (size_t _ckb = 0; _ckb < perChunkClass.size(); ++_ckb) {
                    const auto& aft = perChunkClass[_ckb];
                    const auto& bef = _beforeSplit[_ckb];
                    if (aft.size() != bef.size()) continue;
                    for (size_t _ii = 0; _ii < aft.size(); ++_ii)
                        if (aft[_ii] != bef[_ii]) ++_nSpikesSplit;
                }
                std::set<int> _kAfter;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kAfter.insert(c);
                _kNewThisIter = static_cast<int>(_kAfter.size())
                              - static_cast<int>(_kBefore.size());
            } else if (AlternatingSplitMergeEnable != 0 &&
                       _tmIter == AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Stage 2.11 split] AlternatingKnnSplit: iter cap reached "
                    "(%d); continuing with template-match only\n",
                    AlternatingSplitMergeMaxIters);
            } else if (AlternatingSplitMergeEnable != 0 &&
                       AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                       _altNetGrowthStreak >= 2 &&
                       _tmIter < AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Stage 2.11 split] AlternatingKnnSplit: aborted by "
                    "net-growth streak; continuing with template-match "
                    "only\n");
                _altNetGrowthStreak = 1000;  // suppress further logs
            }

            // Re-harvest meanWav with current perChunkClass
            if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                const int wElems = NbChannels * NbSamplesPerSpike;
                char spkPathTM2[STRLEN + 16];
                // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
                pickInputPath(spkPathTM2, sizeof(spkPathTM2), FileBase, "spk", ElecNo);
                FILE* spkTM2 = fopen(spkPathTM2, "rb");
                if (spkTM2) {
                    for (int k = 0; k < nActive; k++) {
                        const auto& pts2  = chunkPoints[k];
                        const auto& cls2  = perChunkClass[k];
                        const int   nPts2 = static_cast<int>(pts2.size());
                        std::unordered_map<int, ChunkModel*> lcToModel2;
                        for (auto& cm : perChunkModels[k])
                            if (cm.chunkIdx == k)
                                lcToModel2[cm.localClusterId] = &cm;
                        // Zero existing meanWav (and edge waveforms) for live clusters
                        for (auto& [lc2, pcm] : lcToModel2) {
                            pcm->meanWav.assign(static_cast<size_t>(wElems), 0);
                            pcm->meanWavLeft.clear();
                            pcm->meanWavRight.clear();
                        }
                        // Determine this chunk's time range for edge classification
                        float tMin2 = std::numeric_limits<float>::infinity();
                        float tMax2 = -std::numeric_limits<float>::infinity();
                        for (int i = 0; i < nPts2; i++) {
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            if (t < tMin2) tMin2 = t;
                            if (t > tMax2) tMax2 = t;
                        }
                        const float tRange2    = (tMax2 > tMin2) ? (tMax2 - tMin2) : 1.0f;
                        const float tLeftEnd2  = tMin2 + 0.25f * tRange2;
                        const float tRightBeg2 = tMin2 + 0.75f * tRange2;

                        std::unordered_map<int, std::vector<int64_t>> acc2, accLeft2, accRight2;
                        std::unordered_map<int, int> nAcc2, nAccLeft2, nAccRight2;
                        std::vector<int16_t> row2(static_cast<size_t>(wElems));
                        for (int i = 0; i < nPts2; i++) {
                            const int lc2 = cls2[i];
                            if (lc2 == 0 || !lcToModel2.count(lc2)) continue;
                            fseeko(spkTM2,
                                   static_cast<off_t>(pts2[i]) * wElems * sizeof(int16_t),
                                   SEEK_SET);
                            if (fread(row2.data(), sizeof(int16_t), wElems, spkTM2)
                                    != static_cast<size_t>(wElems)) continue;
                            ShiftWaveformRowInPlace(row2.data(), pts2[i],
                                                    NbChannels, NbSamplesPerSpike);
                            auto& a2 = acc2[lc2];
                            if (a2.empty()) a2.assign(static_cast<size_t>(wElems), 0);
                            for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                    a2[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                        += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                            nAcc2[lc2]++;

                            // Edge accumulators
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            std::vector<int64_t>* edgeAcc = nullptr;
                            int* edgeN = nullptr;
                            if (t <= tLeftEnd2)       { edgeAcc = &accLeft2[lc2];  edgeN = &nAccLeft2[lc2];  }
                            else if (t >= tRightBeg2) { edgeAcc = &accRight2[lc2]; edgeN = &nAccRight2[lc2]; }
                            if (edgeAcc) {
                                if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                                for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                    for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                        (*edgeAcc)[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                            += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                                (*edgeN)++;
                            }
                        }
                        for (auto& [lc2, a2] : acc2) {
                            int n2b = nAcc2[lc2];
                            if (n2b == 0) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWav.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWav[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accLeft2) {
                            const int n2b = nAccLeft2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavLeft.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavLeft[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accRight2) {
                            const int n2b = nAccRight2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavRight.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavRight[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                    }
                    fclose(spkTM2);
                }
            }
            LockedStderr( "[Stage 2.11] Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = (MedianKnnTemplateMatchEnable != 0)
                ? WithinChunkTemplateMatchMedianKnn(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore)
                : WithinChunkTemplateMatch(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore);

            // Phase 4b streak update — now that this iter's merge count
            // is known, compare against the iter's split-induced cluster
            // growth.  Updating here (rather than at split time) means
            // the streak reflects the OUTCOME of an alternation round
            // (split + cleanup-merge together), not the split alone.
            if (_altGate) {
                if (_kNewThisIter > _nMerged) ++_altNetGrowthStreak;
                else                          _altNetGrowthStreak = 0;
                LockedStderr(
                    "[Stage 2.11 split] AlternatingKnnSplit (iter %d): %d spike "
                    "labels changed; +%d new clusters vs %d merged (net "
                    "growth streak %d/2)\n",
                    _tmIter + 1, _nSpikesSplit, _kNewThisIter, _nMerged,
                    _altNetGrowthStreak);
            }


            // Phase 4b heavy realignment: at the END of every Phase 4 iter,
            // realign each per-chunk cluster's spikes (klusters-faithful
            // xcorr against the cluster's mean) and refeaturize only the
            // changed spikes from the cached .fil group channels.  The
            // next iter's meanWav harvest (which reads .spk through
            // m_cumShift) and Phase 4b WaveKnnSplit (which reads Data[])
            // both see the realigned state.
            //
            // Gated on KlustersRealignAfterPhase4; requires
            // KlustersRealignEnable so MaxShift/MinSize have valid values.
            if (KlustersRealignAfterPhase4 != 0 &&
                m_timeShiftReady &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                std::vector<int> _changedSpikes;
                _changedSpikes.reserve(1024);
                const int _nChanged = KlustersStyleRealignPerChunkClusters(
                    chunkPoints, perChunkClass,
                    NbChannels, NbSamplesPerSpike,
                    _changedSpikes);
                if (_nChanged > 0) {
                    RefeaturizeChangedSpikes(
                        _changedSpikes, NbChannels, NbSamplesPerSpike);
                }
            }

            const double _iterMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - _iterT0).count();
            _phase4Digest.push_back({ _tmIter + 1, _nMerged, _nSpikesSplit,
                                      _kNewThisIter, _countClusters(),
                                      _iterMs });

            if (_nMerged == 0 && _nSpikesSplit == 0) break;
        }
        // ── Phase 4 convergence digest ────────────────────────────────
        LockedStderr("[Stage 2.11] convergence digest:\n");
        LockedStderr("    iter | merges | splits | +new | clusters | wall_ms\n");
        LockedStderr("    -----+--------+--------+------+----------+--------\n");
        for (const auto& _r : _phase4Digest) {
            LockedStderr("    %4d | %6d | %6d | %4d | %8d | %7.0f\n",
                         _r.iter, _r.merges, _r.spikesSplit, _r.kNew,
                         _r.totalClusters, _r.wallMs);
        }
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.11");
    }

        // ── Phase 4c: neighborhood-remix split (patch 0062) ──────────────
        // Runs after Phase 4 converges; pools each random source with its N
        // closest clusters and re-splits, guarded by disjointness + a
        // tightness mask.  No-op unless -Phase4cRemixEnable 1.
        RunPhase4cRemix(chunkPoints, perChunkClass, perChunkModels, nFullDims);

        // ── Phase 8: variance-targeted knn-split (patch 0067) ───────────
        // Iterates Phase 4b's WaveKnn-split on high-variance clusters
        // only.  No-op unless -Phase8VarianceSplitEnable 1.  FullCEM is
        // intentionally skipped — diffuse clusters have no Gaussian-
        // mixture modes for FullCEM to find; WaveKnn redistributes their
        // spikes to nearby reference clusters instead.
        RunPhase8VarianceSplit(chunkPoints, perChunkClass, perChunkModels, nFullDims);

    // Rebuild pointPacked[] using post-template-merge cluster IDs.
    // perChunkAssign was built with original Phase 1 IDs; after
    // WithinChunkTemplateMatch the IDs in perChunkClass differ.
    // packedToGlobal (built after MergeChunkModels) uses post-merge IDs,
    // so pointPacked must use the same scheme.
    std::fill(pointPacked.begin(), pointPacked.end(), -1);
    for (int k = 0; k < nActive; k++) {
        const auto& pts = chunkPoints[k];
        const auto& cls = perChunkClass[k];
        const int nPts  = static_cast<int>(pts.size());
        for (int i = 0; i < nPts; i++) {
            const int p2 = pts[i];
            if (pointPacked[p2] < 0)  // first-write-wins for overlap spikes
                pointPacked[p2] = k * MaxPossibleClusters + cls[i];
        }
    }

    // Rebuild allModels from perChunkModels now that within-chunk template
    // matching has finalised the local cluster set.  Cluster IDs in
    // perChunkClass and in allModels must agree so that MergeChunkModels vote
    // keys (clsK * MaxPossibleClusters + clsK1) resolve correctly.
    allModels.clear();
    for (int k = 0; k < nActive; k++)
        for (auto& cm : perChunkModels[k])
            allModels.push_back(cm);  // copy — perChunkModels still needed below

    // ── Post-Phase-5 alignment site ─────────────────────────────────────
    // Within-chunk template merges have consolidated per-chunk clusters.
    // Aligns spikes against the merged means before cross-chunk matching.
    RunAlignmentBlock(TimeShiftAlignAfterPhase4, "Align");

    LockedStderr( "[Stage 3.1] Cross-chunk model matching (overlap-vote + edge-xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, noOverlapVotes);
    LockedStderr( "[Stage 3.1] Cross-chunk merge produced %d global clusters\n", nGlobal);
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 3.1");
    if (nGlobal < 1) {
        Output("Merge produced no real clusters — falling back to CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }
    if (nGlobal >= MaxPossibleClusters) {
        // Emit on stderr unconditionally — this is a hard fall-back from
        // chunked CEM to single-pass CEMTwoPhase that the user must know
        // about (it dramatically changes the output cluster set and was
        // silent under Output() unless -Screen 1 was set).  Keep the
        // Output() call so logging still captures it.
        fprintf(stderr,
                "[Stage 3.1] WARNING: MergeChunkModels produced %d global "
                "clusters >= MaxPossibleClusters (%d) — falling back to "
                "CEMTwoPhase.  Fix: re-run with -MaxPossibleClusters %d "
                "or higher (rule of thumb: nChunks * MaxClusters).\n",
                nGlobal, MaxPossibleClusters, nGlobal + 100);
        Output("WARNING: MergeChunkModels produced %d global clusters >= "
               "MaxPossibleClusters (%d).\n"
               "  The cross-chunk merge succeeded (clusters matched between\n"
               "  adjacent chunks), but there are more unique global units\n"
               "  than the MaxPossibleClusters table can hold.\n"
               "  Fix: increase -MaxPossibleClusters to at least %d\n"
               "  (nChunks * MaxClusters is a safe upper bound).\n"
               "  Falling back to CEMTwoPhase on the full session.\n",
               nGlobal, MaxPossibleClusters,
               nGlobal + 10);
        return CEMTwoPhase(timeMergeIter);
    }

    std::unordered_map<int,int> packedToGlobal;
    packedToGlobal.reserve(allModels.size());
    for (const auto& cm : allModels)
        packedToGlobal[cm.chunkIdx * MaxPossibleClusters + cm.localClusterId] =
            cm.globalClusterId;

    // ── Post-Phase-6 alignment site ─────────────────────────────────────
    // Runs after cross-chunk model matching: per-chunk clusters have been
    // mapped to global cluster IDs (above) but Class[] is still per-chunk.
    // Aligns spikes to per-chunk cluster means BEFORE the Phase 7 init
    // block remaps Class[] to global IDs.
    RunAlignmentBlock(TimeShiftAlignAfterPhase5, "Align");

    // ── Phase 7: global warm-start EM ───────────────────────────────────────
    nDims  = nFullDims;
    nDims2 = nDims * nDims;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = 0;
    for (int p = 0; p < nPoints; p++) {
        const int pp = pointPacked[p];
        auto it = (pp >= 0) ? packedToGlobal.find(pp) : packedToGlobal.end();
        const int g = (it != packedToGlobal.end()) ? it->second : 0;
        Class[p] = g;
        ClassAlive[g] = 1;
    }
    Reindex();

    float score;
    if (globalMergeIter <= 0) {
        // GlobalMerge=0: skip Phase 7 entirely.  Emit one MStep/EStep so
        // LogP is valid for ComputeScore(), but do not reassign Class[].
        LockedStderr( "[Stage 3.2] Global EM: skipped (GlobalMergeIter=0)\n");
        Output("Phase 7 skipped (GlobalMerge=0) — using Phase 6 assignment directly\n");
        // Force CPU path for Phase 7 post-merge scoring.
        // GPU EStep writes d_LogP in GPU memory; the CPU LogP.m_Data clamp below
        // would be a no-op on the GPU path.  Temporarily null gpu so MStep/EStep/
        // ComputeScore all run on CPU, where LogP.m_Data is authoritative.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        // Reassign any spikes whose cluster was deleted by MStep (singular covariance)
        // to noise (class 0) so EStep and ComputeScore see valid Class[] values.
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();
        {
            int nNan = 0;
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) { lp = kLargeLogP; nNan++; }
            }
            if (nNan > 0)
                Output("Phase3-skip: clamped %d non-finite LogP entries\n", nNan);
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu);
#endif
        if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
    } else {
        LockedStderr( "[Stage 3.2] Global warm-start EM\n");
        Output("Phase 7: global warm-start EM — %d clusters, max %d iters\n",
               nClustersAlive, globalMergeIter);
        int   iter = 0, nChanged = 1;
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu2 = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();  // CPU path: populates LogP.m_Data directly
        {
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) lp = kLargeLogP;
            }
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu2);
#endif
        FullStep = 1;
        for (; iter < globalMergeIter; iter++) {
            MStep(); EStep(); nChanged = CStep(); ConsiderDeletion();
            score = ComputeScore();
            if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
            if (Verbose >= 2)
                Output("  P3 iter %d: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);
            FullStep = 1;
            if (nChanged == 0) { Output("Phase 7 converged at iter %d\n", iter); break; }
        }
    }

    // Per-phase quality summary after Phase 7 (Global EM) — gives a
    // checkpoint before DipSplit potentially mutates the cluster set.
    LogGlobalClusterState("Stage 3.2 (Global EM)");
    ReportClusterQuality("Stage 3.2");

    // ── Phase 6a (optional): post-merge cluster realignment ────────────────
    // Named "6a" because it refines Phase 6's cross-chunk merges, but
    // EXECUTES AFTER Phase 7 (Global EM) has consolidated chunk-local
    // clusters into the final global cluster set — only at that point
    // are the merged means stable enough for a meaningful alignment
    // pass.  When -TimeShiftAlignPostMerge != 0, run another
    // TimeShiftAlignPhase pass against the post-Phase-7 global cluster
    // state.  This catches realignment opportunities that opened up
    // only after Phase 6's cross-chunk merges consolidated chunk-local
    // clusters into global
    // units: a spike whose Phase-1.5 alignment was optimal vs. its
    // chunk-local cluster mean may not be optimal vs. the global cluster
    // mean (which is the weighted average across all the chunk-local
    // pieces).  Effectively replicates what Klusters' realignment dialog
    // does post-hoc on the output, but does it inside the run so any
    // downstream consumer (final mean waveform, .clu file) sees clean
    // features.  No-op when m_timeShiftReady is false.
    if (TimeShiftAlignPostMerge != 0 && m_timeShiftReady) {
        LockedStderr( "[Stage 3.3] Post-merge cluster realignment\n");
        const int nShifted = TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);
        int nCOMShifted = 0;
        if (EnergyCOMRealign != 0) {
            LockedStderr( "[Stage 3.3] Energy-COM realignment\n");
            nCOMShifted = EnergyCOMRealignPhase(NbChannels, NbSamplesPerSpike);
        }
        if (nShifted > 0 || nCOMShifted > 0) {
            // Refresh global state after realignment: MStep recomputes
            // Mean/Cov from updated Data[], EStep populates LogP, then a
            // ComputeScore captures the post-realignment score.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuR = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuR);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Stage 3.3 (post-merge realign)"); ReportClusterQuality("Stage 3.3");
        }
    }

    // ── Phase 6b (optional): mean-waveform subtraction merge ─────────
    // Same naming logic as Phase 6a: named "6b" because it refines the
    // Phase 6 merge decisions using post-merged means, but EXECUTES
    // AFTER Phase 7 (and after Phase 6a if enabled).  Runs after
    // Phase 6a so the means it aggregates already reflect the final
    // post-merge alignment.  Opt-in via
    // -MeanSubtractionMergeEnable 1; threshold via
    // -MeanSubtractionMergeThresh (default 0.05), cyclic-shift radius
    // via -MeanSubtractionMergeMaxShift (default 3).  See
    // KK::FinalMeanSubtractionMerge for algorithm details.
    if (MeanSubtractionMergeEnable != 0) {
        LockedStderr( "[Stage 3.4] Mean-subtraction template merge "
                        "(threshold D < %.3f)\n",
                static_cast<double>(MeanSubtractionMergeThresh));
        const int nMergedSub =
            FinalMeanSubtractionMerge(NbChannels, NbSamplesPerSpike);
        if (nMergedSub > 0) {
            // Same state-refresh dance as Phase 6a — Class[] now points
            // to consolidated roots; MStep recomputes Mean/Cov, Reindex
            // compacts dead IDs, EStep populates LogP for the new layout.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuB = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuB);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Stage 3.4 (mean-sub merge)"); ReportClusterQuality("Stage 3.4");
        }
    }

    // ── Phase 7c — klusters-faithful per-spike realignment ─────────────
    // Mirror of Driver B (second overload).  See KK::KlustersStyleRealign-
    // AllClusters for algorithm details.  Opt-in via -KlustersRealignEnable 1.
    if (KlustersRealignEnable != 0) {
        LockedStderr(
                "[Stage 3.6] Klusters-faithful per-spike realignment "
                "(maxShift=%d samples, minSize=%d)\n",
                KlustersRealignMaxShift, KlustersRealignMinSize);
        const int nChanged =
            KlustersStyleRealignAllClusters(NbChannels, NbSamplesPerSpike);
        if (nChanged > 0) {
            LogGlobalClusterState("Stage 3.6 (klusters realign)");
        }
    }

    // Phase 8 DipSplit removed: per-chunk DipSplit now runs as Phase 1b,
    // before cross-chunk model matching, so global splits at this stage
    // would be operating on already-merged clusters where apparent
    // bimodality is more likely a drift artefact than a real second mode.

    // Per-phase quality summary at end of pipeline (after DipSplit).  Catches
    // failures like "one cluster ate everything" before the user sees it in
    // the GUI.  See KK::ReportClusterQuality for metric definitions.
    if (Phase9CCGMergeEnable) Phase9CCGMerge(samplingRate);
    ReportClusterQuality("final");

    Output("RunChunkedCEM(ext) done: %d clusters, score %.7g\n", nClustersAlive, score);
    return score;
}


// ---------------------------------------------------------------------------
// Chunked-CEM pipeline (RunChunkedCEM).  Phase numbering matches what is
// printed as `[Stage N.M]` banners in the run log.
//
//   Phase 0   Chunking — divide session into drift-adaptive chunks.
//   Phase 1   Per-chunk CEM — independent CEMTwoPhase on each chunk.
//   Phase 1a  Cluster realignment — xcorr-based per-spike shift (was 1.5).
//   Phase 1b  Per-chunk DipSplit — bimodal-cluster splits (was 1.6).
//   Phase 2   Per-chunk refractory split.
//   Phase 2a  Per-cluster CEM refinement.
//   Phase 2b  Chunk re-CEM (rebuild after per-cluster work).
//   Phase 4   Mean waveform harvest — build templates for xcorr.
//   Phase 5   Within-chunk template match (xcorr + optional eig-ratio veto).
//   Phase 6   Cross-chunk model match (overlap voting + edge xcorr).
//   Phase 7   Global warm-start EM (optional; -GlobalMergeIter > 0).
//   Phase 6a  Post-merge cluster realignment (was 7.5; optional, patch26).
//   Phase 8   Legacy global DipSplit (deprecated; gated off by default).
//   Phase 9   Shift commit (write refined .spk/.fet/.res).
//
// Pipeline phase map (RunChunkedCEM — both overloads):
//   Phase 0   global preseed (optional)
//   Phase 1   per-chunk CEMTwoPhase (parallel over chunks)
//     1a      per-cluster shift-probe alignment
//     1b      per-chunk DipSplit  ← KDE valley split, runs before Phase 2
//     1c      alignment after DipSplit
//   Phase 2   per-chunk refractory split + subspace recluster
//     2.D     refractory split (physical prior, runs FIRST inside Phase 2)
//     2a      per-cluster ordinary CEM in full feature space
//     2b      chunk-level warm-start CEM (boundary reassignment + merging)
//     2.5     SECOND per-chunk DipSplit (catches bimodality EXPOSED by 2a)
//     2c      alignment after the chunk-level re-CEM
//   Phase 3   mean-waveform harvest ([Stage 2.11 harvest] log tag — a setup step
//             for the cross-chunk template matching in Phases 5/6,
//             not an independent clustering phase)
//   Phase 5   within-Phase-6-iteration circular xcorr template matching
//   Phase 6   cross-chunk model matching (begins with a mergeThresh
//             calibration check)
//   Phase 7   global warm-start EM (full dimensionality)
//   Phase 6a  post-Phase-7 cluster realignment (named '6a' because it's
//             a refinement of Phase 6's merge decisions, but executes
//             AFTER Phase 7 has consolidated chunk-locals into globals)
//   Phase 6b  post-Phase-7 mean-waveform subtraction merge (same naming
//             logic as 6a)
//   Phase 9   TimeShiftFinalize (final commit of cumulative shifts)
//
// Phases 4 and 8 are intentionally unused.  Phase 8 was the legacy
// global post-merge DipSplit; per-chunk DipSplit at 1b + 2.5 supplants
// it without the drift-axis false positives that bisect single drifting
// units across the session boundary.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Phase 9 CCG refractory-dip merge (testing).  See header + Phase9CCGMerge*
// params.  Works in NORMALISED time units (Data[*,timeDim] is [0,1]); the
// refractory/flank windows are converted from ms via samplingRate and the
// recorded raw time span (timeRawMax - timeRawMin).
int KK::Phase9CCGMerge(float samplingRate)
{
    if (!Phase9CCGMergeEnable) return 0;
    const int timeDim      = nDims - 1;
    const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
    const double sessionSamples = static_cast<double>(timeRawMax) - timeRawMin;
    if (sessionSamples <= 0.0 || samplingRate <= 0.0f || nSpatialDims < 2) return 0;

    // refractory / flank windows in normalised-time units
    const double refrNorm  = static_cast<double>(Phase9CCGRefractoryMs) * 1e-3
                             * samplingRate / sessionSamples;
    const double flankNorm = static_cast<double>(Phase9CCGFlankMs) * 1e-3
                             * samplingRate / sessionSamples;
    if (flankNorm <= refrNorm) return 0;

    // gather per-cluster point indices (skip noise/artefact clusters 0,1)
    std::vector<std::vector<int>> members(MaxPossibleClusters);
    for (int pIdx = 0; pIdx < nPoints; pIdx++) members[Class[pIdx]].push_back(pIdx);

    std::vector<int> alive;
    for (int c = 2; c < MaxPossibleClusters; c++)
        if (static_cast<int>(members[c].size()) >= Phase9CCGMinSpikes) alive.push_back(c);
    const int M = static_cast<int>(alive.size());
    if (M < 2) return 0;

    // per-cluster sorted normalised times + spatial-feature mean
    std::vector<std::vector<float>> times(M);
    std::vector<std::vector<float>> smean(M, std::vector<float>(nSpatialDims, 0.0f));
    for (int a = 0; a < M; a++) {
        const std::vector<int>& mem = members[alive[a]];
        times[a].reserve(mem.size());
        for (int pIdx : mem) {
            times[a].push_back(Data[static_cast<size_t>(pIdx) * nDims + timeDim]);
            for (int d = 0; d < nSpatialDims; d++)
                smean[a][d] += Data[static_cast<size_t>(pIdx) * nDims + d];
        }
        std::sort(times[a].begin(), times[a].end());
        for (int d = 0; d < nSpatialDims; d++) smean[a][d] /= static_cast<float>(mem.size());
    }

    // union-find over alive index
    std::vector<int> uf(M);
    std::iota(uf.begin(), uf.end(), 0);
    auto ufFind = [&uf](int x){ while (uf[x] != x) { uf[x] = uf[uf[x]]; x = uf[x]; } return x; };

    int nMerges = 0;
    for (int a = 0; a < M; a++)
        for (int b = a + 1; b < M; b++) {
            if (p9_pearson(smean[a], smean[b]) < static_cast<double>(Phase9CCGGateCosine))
                continue;                                  // shapes too different
            long center = 0, flankN = 0;
            p9_ccgCounts(times[a], times[b], refrNorm, flankNorm, center, flankN);
            if (flankN < Phase9CCGMinFlankPairs) continue; // underpowered CCG
            const double centerRate = static_cast<double>(center) / (2.0 * refrNorm);
            const double flankRate   = static_cast<double>(flankN) / (2.0 * (flankNorm - refrNorm));
            if (flankRate <= 0.0) continue;
            if (centerRate / flankRate <= static_cast<double>(Phase9CCGDepletionRatio)) {
                uf[ufFind(a)] = ufFind(b);
                ++nMerges;
            }
        }

    if (nMerges == 0) {
        Output("[Stage 3.7 CCG] refractory-dip merge: 0 merges "
               "(tested %d alive clusters; refr=%.1fms flank=%.1fms ratio<=%.2f cos>=%.2f)\n",
               M, Phase9CCGRefractoryMs, Phase9CCGFlankMs,
               Phase9CCGDepletionRatio, Phase9CCGGateCosine);
        return 0;
    }

    // destination = smallest cluster id in each connected component
    std::vector<int> compMin(M);
    for (int a = 0; a < M; a++) compMin[a] = alive[a];
    for (int a = 0; a < M; a++) {
        const int r = ufFind(a);
        if (alive[a] < compMin[r]) compMin[r] = alive[a];
    }
    std::vector<int> dest(MaxPossibleClusters, -1);
    for (int a = 0; a < M; a++) dest[alive[a]] = compMin[ufFind(a)];
    for (int pIdx = 0; pIdx < nPoints; pIdx++) {
        const int d = dest[Class[pIdx]];
        if (d >= 0) Class[pIdx] = d;
    }

    // recount alive clusters from Class[] (same pattern as the EM driver)
    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = 0;
    for (int pIdx = 0; pIdx < nPoints; pIdx++) ClassAlive[Class[pIdx]] = 1;
    nClustersAlive = 0;
    for (int c = 0; c < MaxPossibleClusters; c++)
        if (ClassAlive[c]) AliveIndex[nClustersAlive++] = c;

    Output("[Stage 3.7 CCG] refractory-dip merge: %d pair-merge(s) -> %d alive clusters "
           "(refr=%.1fms flank=%.1fms ratio<=%.2f cos>=%.2f)\n",
           nMerges, nClustersAlive, Phase9CCGRefractoryMs, Phase9CCGFlankMs,
           Phase9CCGDepletionRatio, Phase9CCGGateCosine);
    return nMerges;
}


float KK::RunChunkedCEM(float chunkMinutes,
                         float samplingRate,
                         float mergeThresh,
                         int   globalMergeIter,
                         int   timeMergeIter,
                         float chunkOverlapMinutes,
                         float chunkPreseedFraction)
{
    const int nFullDims    = nDims;
    const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
    const int timeDim      = nDims - 1;

    // -------------------------------------------------------------------
    // Phase 0: build temporal chunk index
    //
    // timeRawMin/Max captured by LoadData() before normalisation.
    // Normalised time in [0,1] spans sessionSamples raw samples.
    // chunkFrac = fraction of [0,1] covered by one chunk.
    // -------------------------------------------------------------------
    const float sessionSamples = timeRawMax - timeRawMin;
    if (sessionSamples <= 0.0f) {
        Output("RunChunkedCEM: cannot determine session length — falling back.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    const float chunkSamples = samplingRate * chunkMinutes * 60.0f;
    const float chunkFrac    = chunkSamples / sessionSamples;
    const int   nChunks      = std::max(1, static_cast<int>(std::ceil(1.0f / chunkFrac)));

    LockedStderr( "[Stage 1] Chunking (%.0f min, %d chunks, %.0f min/chunk; %s)\n",
            sessionSamples / samplingRate / 60.0f, nChunks, chunkMinutes,
            (chunkPreseedFraction > 0.0f)
                ? "global preseed enabled"
                : "preseed disabled — per-chunk random-init CEM");
    Output("RunChunkedCEM: session %.1f min, chunk %.1f min, %d chunks\n",
           sessionSamples / samplingRate / 60.0f, chunkMinutes, nChunks);

    if (nChunks <= 1) {
        Output("Session shorter than one chunk — running CEMTwoPhase directly.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // If chunkOverlapMinutes > 0, spikes within the trailing overlapFrac of
    // chunk k are also appended to chunk k+1.  After per-chunk EM their
    // assignments form a vote matrix that supplements Mahalanobis MNN.
    const float chunkOverlapFrac = (chunkOverlapMinutes > 0.0f)
        ? (samplingRate * chunkOverlapMinutes * 60.0f) / sessionSamples
        : 0.0f;
    if (chunkOverlapFrac >= chunkFrac * 0.5f && chunkOverlapFrac > 0.0f)
        Output("RunChunkedCEM: WARNING — overlap (%.1f min) >= half chunk size; "
               "consider reducing ChunkOverlapMinutes.\n", chunkOverlapMinutes);

    struct OverlapEntry { int localK; int localK1; };
    // nChunks >= 2 here: the early return at the top of this function exits
    // when nChunks <= 1, so `nChunks - 1` is unconditionally valid.
    std::vector<std::vector<OverlapEntry>> overlapForPair(nChunks - 1);

    std::vector<std::vector<int>> chunkPoints(nChunks);
    for (int p = 0; p < nPoints; p++) {
        const float t = Data[p * nDims + timeDim];
        int k = static_cast<int>(t / chunkFrac);
        if (k < 0) k = 0;
        if (k >= nChunks) k = nChunks - 1;
        const int localK = static_cast<int>(chunkPoints[k].size());
        chunkPoints[k].push_back(p);
        // Trailing overlap: if within overlapFrac of k's right boundary, also add to k+1
        if (chunkOverlapFrac > 0.0f && k + 1 < nChunks) {
            const float rightBoundary = (k + 1) * chunkFrac;
            if ((rightBoundary - t) <= chunkOverlapFrac) {
                const int localK1 = static_cast<int>(chunkPoints[k + 1].size());
                chunkPoints[k + 1].push_back(p);
                overlapForPair[k].push_back({localK, localK1});
            }
        }
    }

    // Merge undersized trailing chunks into their predecessor.
    // Need at least nStartingClusters * nSpatialDims * 3 spikes to reliably
    // estimate all cluster covariances.
    const int minSpikes = nStartingClusters * nSpatialDims * 3;
    for (int k = nChunks - 1; k >= 1; k--) {
        if (static_cast<int>(chunkPoints[k].size()) < minSpikes) {
            Output("  Chunk %d: %d spikes < %d minimum — merging into chunk %d.\n",
                   k, static_cast<int>(chunkPoints[k].size()), minSpikes, k - 1);
            for (int p : chunkPoints[k]) chunkPoints[k-1].push_back(p);
            chunkPoints[k].clear();
        }
    }
    // Build compacted-index → original-index mapping BEFORE erasing empty chunks.
    // This is required so the overlap vote collection loop can look up
    // overlapForPair[origK] rather than overlapForPair[k], which diverge
    // whenever a non-trailing chunk is merged away and the compacted indices
    // shift relative to the original ones.
    std::vector<int> activeOrigIdx;
    activeOrigIdx.reserve(nChunks);
    for (int k = 0; k < nChunks; k++)
        if (!chunkPoints[k].empty())
            activeOrigIdx.push_back(k);

    chunkPoints.erase(
        std::remove_if(chunkPoints.begin(), chunkPoints.end(),
                       [](const std::vector<int>& v){ return v.empty(); }),
        chunkPoints.end());
    const int nActive = static_cast<int>(chunkPoints.size());

    if (nActive <= 1) {
        Output("Only one chunk after size-check merges — running CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }
    Output("Active chunks: %d\n", nActive);

    // -------------------------------------------------------------------
    // Phase 0: optional global preseed (re-enabled in patch10).
    //
    // When chunkPreseedFraction > 0, sample that fraction of spikes
    // globally and run a single CEMTwoPhase on the subsample to get
    // shared starting centres for every chunk in Phase 1.  Pros: chunks
    // start from a coherent global cluster picture, which can stabilise
    // small/sparse chunks and helps Phase 5 cross-chunk matching.
    // Cons: bias — every chunk inherits the same initial bias from the
    // subsample, and rare units that are absent from the subsample get
    // no preseed centre at all.
    //
    // When chunkPreseedFraction <= 0, skip this and let each chunk run
    // its own random-init CEM (the patch6 default).  In that mode
    // chunkInitRandom = true gates the per-thread KK to use
    // irand(1, nCentres) per spike rather than farthest-point init.
    //
    // Set -ChunkPreseedFraction 0 (or omit) to keep the patch6 default.
    // Set -ChunkPreseedFraction 0.05..0.20 to A/B test against random
    // init.  PreseedSubsampleCEM emits its own progress lines.
    // -------------------------------------------------------------------
    std::vector<float> globalPreseedCentres;
    if (chunkPreseedFraction > 0.0f) {
        Output("Phase 0: global preseed (fraction=%.3f, nCentres=%d)\n",
               chunkPreseedFraction, MaxClusters - 1);
        globalPreseedCentres = PreseedSubsampleCEM(
            chunkPreseedFraction, MaxClusters - 1,
            nSpatialDims, timeMergeIter);
        if (globalPreseedCentres.empty()) {
            Output("Phase 0: preseed produced no centres — falling back to "
                   "per-chunk random init.\n");
        } else {
            Output("Phase 0: preseed produced %zu centres × %d spatial dims\n",
                   globalPreseedCentres.size() / nSpatialDims, nSpatialDims);
        }
    }

    // -------------------------------------------------------------------
    // Phase 1: per-chunk CEMTwoPhase — parallel over chunks
    //
    // Each chunk builds a self-contained KK sub-object with:
    //   suppressBestSave = true   — no writes to global kSv during parallel section
    //   its own cholFlat/bestCholFlat — no shared Cholesky storage
    //
    // To avoid per-chunk heap allocation inside the parallel region (which
    // causes allocator contention across threads), we pre-allocate one KK
    // scratch object per OMP thread, sized to the largest chunk.  Each
    // iteration reinitialises the relevant fields via ReinitForSplit rather
    // than allocating fresh arrays.
    //
    // Thread-local vectors accumulate models and point assignments.
    // A serial reduction gathers them into allModels / pointPacked after
    // the parallel block, so no atomic or mutex is needed.
    //
    // Output() calls inside CEMTwoPhase are guarded by a single critical
    // section to prevent interleaved log lines; set Verbose=0 to suppress.
    // -------------------------------------------------------------------
    std::vector<ChunkModel> allModels;
    std::vector<int>        pointPacked(nPoints, -1);  // -1 sentinel: unwritten -> noise

    // Per-chunk accumulators: indexed by chunk k.
    std::vector<std::vector<ChunkModel>> perChunkModels(nActive);
    std::vector<std::vector<std::pair<int,int>>> perChunkAssign(nActive);
    std::vector<std::vector<int>> perChunkClass(nActive);  // for overlap votes
    // per-chunk score (for logging)
    std::vector<float> perChunkScore(nActive, 0.0f);
    std::vector<int>   perChunkNClusters(nActive, 0);

    // Find the largest chunk size so we can pre-allocate at that capacity.
    int maxChunkSize = 0;
    for (int k = 0; k < nActive; k++)
        maxChunkSize = std::max(maxChunkSize, static_cast<int>(chunkPoints[k].size()));

    // One pre-allocated KK scratch per OMP thread.
#ifdef _OPENMP
    // When running as a ParallelK worker, ompTeamSize limits the inner
    // team so that multiple workers can share the machine without fighting
    // for threads.  0 = use all available (normal non-parallel path).
    const int nThreads = (ompTeamSize > 0)
        ? ompTeamSize
        : omp_get_max_threads();
#else
    const int nThreads = 1;
#endif
    // When preseed succeeded, start each chunk from the preseed's K rather than
    // the outer loop's nStartingClusters.  The preseed already found a good
    // partition of the full dataset, so seeding chunks at that K avoids the
    // slow bottom-up split cascade from K=2.
    //
    // With Phase 0 preseed disabled (default), globalPreseedCentres is always
    // empty and chunkStartK falls back to nStartingClusters.  Each chunk
    // starts at K = nStartingClusters with random Class[] init via
    // chunkInitRandom — TrySplits and ConsiderDeletion settle K from there.
    const int chunkStartK = (!globalPreseedCentres.empty())
        ? static_cast<int>(globalPreseedCentres.size() / nSpatialDims) + 1
        : nStartingClusters;

    std::vector<KK> threadKc(nThreads);
    for (int t = 0; t < nThreads; t++) {
        threadKc[t].nDims             = nFullDims;
        threadKc[t].nPoints           = maxChunkSize;
        threadKc[t].nStartingClusters = chunkStartK;
        threadKc[t].penaltyMix        = penaltyMix;
        threadKc[t].suppressBestSave  = true;
        // Deletion floor: with preseed, hold near chunkStartK to preserve
        // the preseed's K.  Without preseed (default), drop to 2 so CEM
        // can shrink down naturally from random init.
        threadKc[t].minClustersAlive  = (!globalPreseedCentres.empty())
            ? std::max(2, chunkStartK - 4)
            : 2;
        threadKc[t].preseedCentres    = globalPreseedCentres;  // empty by default
        threadKc[t].chunkInitRandom   = globalPreseedCentres.empty();
        threadKc[t].AllocateArrays();
        threadKc[t].AllocateCholeskyVecs();
    }

    const int runsPerChunk = (nRuns > 0) ? nRuns : 1;
    const int nFlatPhase1  = nActive * runsPerChunk;

    struct ChunkRunResult {
        float            score     = HugeScore;
        int              nClusters = 0;
        std::vector<int> cls;
    };
    std::vector<ChunkRunResult> flatResults(static_cast<size_t>(nFlatPhase1));
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k    = fi / runsPerChunk;
        const int nPts = static_cast<int>(chunkPoints[static_cast<size_t>(k)].size());
        flatResults[static_cast<size_t>(fi)].cls.assign(static_cast<size_t>(nPts), 0);
        flatResults[static_cast<size_t>(fi)].score = HugeScore;
    }

    LockedStderr( "[Stage 2.1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
            nThreads, nActive, runsPerChunk, nFlatPhase1);
        #pragma omp parallel for schedule(dynamic) default(none) \
        num_threads(nThreads) \
        shared(flatResults, chunkPoints, nActive, nFullDims, timeMergeIter, threadKc, stderr) \
        firstprivate(MaxPossibleClusters, chunkStartK, penaltyMix, \
                     runsPerChunk, nFlatPhase1, HugeScore, RandomSeed)
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k   = fi / runsPerChunk;
        const int run = fi % runsPerChunk;
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        KK& Kc = threadKc[
#ifdef _OPENMP
            omp_get_thread_num()
#else
            0
#endif
        ];
        kk_seed_rng(kk_mix_seed(kk_mix_seed(static_cast<uint64_t>(RandomSeed),
                                            static_cast<uint64_t>(k)),
                                static_cast<uint64_t>(run)));
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = chunkStartK;
        Kc.NoisePoint        = 1;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        const float runScore = Kc.CEMTwoPhase(timeMergeIter);

        ChunkRunResult& cr = flatResults[static_cast<size_t>(fi)];
        cr.score     = runScore;
        cr.nClusters = Kc.nClustersAlive;
        for (int i2 = 0; i2 < nPts; i2++)
            cr.cls[static_cast<size_t>(i2)] = Kc.Class[i2];

        #pragma omp critical
        { fprintf(stderr, "  [chunk %d/%d  run %d/%d] score=%.4g  nclusters=%d\n",
                  k + 1, nActive, run + 1, runsPerChunk,
                  runScore, Kc.nClustersAlive); }
    }

    // Serial reduction: pick best run per chunk, rebuild KK, harvest models.
    for (int k = 0; k < nActive; k++) {
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        float bestScore = HugeScore;
        int   bestRun   = 0;
        for (int run = 0; run < runsPerChunk; run++) {
            const float s = flatResults[static_cast<size_t>(k * runsPerChunk + run)].score;
            if (s < bestScore) { bestScore = s; bestRun = run; }
        }
        const ChunkRunResult& best = flatResults[
            static_cast<size_t>(k * runsPerChunk + bestRun)];

        KK& Kc = threadKc[0];
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = chunkStartK;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        for (int i2 = 0; i2 < nPts; i2++) Kc.Class[i2] = best.cls[static_cast<size_t>(i2)];
        for (int c = 0; c < MaxPossibleClusters; c++) Kc.ClassAlive[c] = 0;
        for (int i2 = 0; i2 < nPts; i2++) Kc.ClassAlive[Kc.Class[i2]] = 1;
        Kc.nClustersAlive = 0;
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (Kc.ClassAlive[c]) Kc.AliveIndex[Kc.nClustersAlive++] = c;
        Kc.MStep();

        perChunkScore[k]     = bestScore;
        perChunkNClusters[k] = best.nClusters;

        auto& models = perChunkModels[k];
        for (int cc = 0; cc < Kc.nClustersAlive; cc++) {
            const int c = Kc.AliveIndex[cc];
            ChunkModel cm;
            cm.chunkIdx        = k;
            cm.localClusterId  = c;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(nFullDims, 0.0f);
            cm.cov.assign(nFullDims * nFullDims, 0.0f);
            for (int d = 0; d < nFullDims; d++)
                cm.mean[d] = Kc.Mean[c * nFullDims + d];
            for (int r = 0; r < nFullDims; r++)
                for (int col = r; col < nFullDims; col++)
                    cm.cov[r * nFullDims + col] =
                        Kc.Cov[c * Kc.nDims2 + r * nFullDims + col];
            for (int i = 0; i < nPts; i++)
                if (Kc.Class[i] == c) cm.nMembers++;
            models.push_back(std::move(cm));
        }

        auto& assign = perChunkAssign[k];
        assign.reserve(nPts);
        for (int i = 0; i < nPts; i++)
            assign.emplace_back(pts[static_cast<size_t>(i)],
                                k * MaxPossibleClusters + Kc.Class[i]);

        auto& classArr = perChunkClass[k];
        classArr.resize(nPts);
        for (int i = 0; i < nPts; i++) classArr[i] = Kc.Class[i];
    }

    LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.1");

    // ── Provisional Class[] seeding for Phase 2 ─────────────────────────────
    // Phase 1 ran on local threadKc[] objects; K1.Class[] is all-zero here.
    // Phase 2 SubspaceReclusterPerChunk needs alive cluster statistics.
    if (SubspaceRecluster > 0) {
        std::fill(Class.m_Data, Class.m_Data + nPoints, 0);
        std::fill(ClassAlive.m_Data, ClassAlive.m_Data + MaxPossibleClusters, 0);
        ClassAlive[0] = 1;
        for (int k_ps = 0; k_ps < nActive; ++k_ps) {
            const auto& pts_ps = chunkPoints[static_cast<size_t>(k_ps)];
            const auto& cls_ps = perChunkClass[static_cast<size_t>(k_ps)];
            for (int i_ps = 0; i_ps < static_cast<int>(pts_ps.size()); ++i_ps) {
                const int p_ps = pts_ps[static_cast<size_t>(i_ps)];
                const int c_ps = cls_ps[static_cast<size_t>(i_ps)];
                if (Class[p_ps] == 0 && c_ps > 0) {
                    Class[p_ps] = c_ps;  ClassAlive[c_ps] = 1;
                }
            }
        }
        Reindex();
        nDims = nFullDims;  nDims2 = nDims * nDims;
        MStep();
        for (int cc2 = 1; cc2 < nClustersAlive; ++cc2) {
            const int c2 = AliveIndex[cc2];
            if (Cholesky(Cov.m_Data + c2*nDims2, cholFlat.data() + c2*nDims2, nDims))
                ClassAlive[c2] = 0;
        }
        Reindex();
        Output("Provisional Class[] seeded: %d alive clusters\n", nClustersAlive);
    }

    // ── Phase 1a: per-cluster shift-probe alignment ────────────────────────
    //
    // For each alive cluster, picks the per-spike δ ∈ {-N,…,+N} that
    // minimises Mahalanobis² to the cluster's own Gaussian (using the
    // cluster's Mean + Cholesky-factored Cov).  Aligns spikes WITHIN each
    // cluster — not to a canonical peak sample.  The cluster's mean is
    // wherever Phase 1 CEM put it; this phase tightens spikes around that
    // centre, it does not move the centre to peakSampleIndex.
    //
    // Caveats inherited from the design:
    //  - Per-cluster myopia: spikes near a cluster boundary may be pulled
    //    deeper into the wrong cluster (the score sees only the assigned
    //    cluster's distribution, not neighbours').  The next EStep will
    //    re-evaluate, but on slightly distorted features.
    //  - Asymmetric search window: candOk[ci] enforces |baseCum + δ| ≤
    //    m_timeShiftMaxAbs, so spikes already near ±maxAbs see fewer
    //    candidates on the cap-side.  Intentional bound on cumulative
    //    drift, not a bug.
    //  - Pre-shifted PCA basis is fixed at InitTimeShift; large cumulative
    //    shifts (across many iterations) make the basis statistically
    //    less efficient but not incorrect.
    //
    // Stats are current at this point: the SubspaceRecluster=1 seeding
    // block above ran MStep + Cholesky.  When SubspaceRecluster=0 no
    // clusters are alive yet and the call no-ops cleanly (the loop in
    // TimeShiftAlignPhase iterates ClassAlive and finds nothing).
    RunAlignmentBlock(TimeShiftAlignAfterPhase1, "Align");

    // ── Phase 1b: per-chunk DipSplit ──────────────────────────────────────
    //
    // Catches bimodal/elongated clusters that the parametric Phase 1 CEM
    // missed.  Runs per-chunk before Phase 2 so subsequent passes operate
    // on the most-correctly-split inputs, and so cross-chunk merge in
    // Phase 6 sees the correct cluster count.  Replaces the old
    // post-Phase-7 global Phase 8 DipSplit.  No-op when DipSplitEnable=0.
    DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims,
                     "Stage 2.2");
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.2");
    RunAlignmentBlock(TimeShiftAlignAfterPhase1b, "Align");

    // ── Phase 2: per-chunk refractory split + subspace reclustering ────────
    //
    // P2.D: refractory split runs FIRST.  Refractory contamination is a
    // physical signal — two units sharing a cluster cannot satisfy
    // biological refractory periods — so it's a stronger prior than any
    // covariance-based statistical split.  Splitting on physics first gives
    // the subspace CEM cleaner inputs.
    //
    // The earlier order (subspace then refractory) had a failure mode: if
    // subspace incorrectly split a clean cluster (multiple-comparisons
    // false positive), the children might individually pass the 1% ISI
    // contamination threshold (each carrying half the violations) and the
    // over-split would stick.  Running refractory first locks in physically-
    // motivated splits before the statistical pass can over-fit.
    if (SubspaceRecluster > 0) {
        // Refractory-period guided split — catches mixtures by exploiting the
        // 1-neuron-per-refractory-window constraint.  Absolute refractory =
        // 1.5 ms × sampling rate samples.  Trigger when ISI contamination
        // rate >= 1%.
        // ── Fused per-chunk block (Stages 2.3-2.10) ────────────────────
        //   -FusePerChunkStages 1: each chunk runs its whole refinement
        //   pipeline on one thread (one barrier instead of ~8 sub-phase
        //   barriers).  These stages are per-chunk independent; FullCem
        //   (global source pool) stays separate below.  Per-stage cluster-
        //   state digests collapse into one after the barrier.
        if (FusePerChunkStages) {
            const float refractSamp = 1.5f * SamplingRate / 1000.0f;
            const float sessLenSamp = timeRawMax - timeRawMin;
            const int   nChFuse     = static_cast<int>(chunkPoints.size());
            #pragma omp parallel for schedule(dynamic)
            for (int ck = 0; ck < nChFuse; ck++) {
                if (SamplingRate > 0.0f)
                    RefractorySplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                        nFullDims, refractSamp, 0.01f, sessLenSamp, ck);
                PerClusterCEMPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims, ck);
                if (DipSplitEnable != 0 && DipSplitBeforePhase2b != 0)
                    DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims, "Stage 2.5", ck);
                if (HullSplitEnable != 0)
                    HullSplitPerChunk(chunkPoints, perChunkClass, nFullDims, "Stage 2.6", ck);
                if (PerChannelSplitEnable != 0)
                    PerChannelSplitPerChunk(chunkPoints, perChunkClass,
                        NbChannels, NbSamplesPerSpike, "Stage 2.7", ck);
                ChunkReCEMPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims, ck);
                DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims, "Stage 2.9", ck);
                if (KnnSplitPerChunkEnable) {
                    if (KnnSplitMode == 1)
                        WaveKnnSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims, nullptr, ck);
                    else
                        KnnSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims, ck);
                }
            }
            // The per-chunk Refractory guards race on the shared CEM-limit
            // static; force it back to 0 so the cap can't leak into Stage 3.
            s_cemCallTimeLimitSec.store(0.0, std::memory_order_relaxed);
            LockedStderr("[Stage 2.x] per-chunk block fused: %d chunks, single barrier\n", nChFuse);
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.10");
        } else {
        if (SamplingRate > 0.0f) {
            const float refractSamp    = 1.5f * SamplingRate / 1000.0f;
            const float sessLenSamp    = timeRawMax - timeRawMin;
            LockedStderr( "[Stage 2.3] Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.3");
        }

        // Phase 2a: per-cluster ordinary CEM in the full feature space.
        // Replaces SubspaceReclusterPerChunk.  For each cluster in each
        // chunk, runs CEM with splits enabled to find bimodal substructure;
        // updates perChunkClass[] only.
        PerClusterCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.4");

        // Phase 2a.5: per-chunk DipSplit (see commentary at the matching
        // insertion in the first overload of RunChunkedCEM).  Gated by
        // DipSplitEnable AND DipSplitBeforePhase2b.
        if (DipSplitEnable != 0 && DipSplitBeforePhase2b != 0) {
            DipSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims,
                "Stage 2.5");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.5");
        }

        // Phase 2a.6: per-chunk HullSplit, see commentary at the matching
        // insertion in the first overload of RunChunkedCEM.
        if (HullSplitEnable != 0) {
            HullSplitPerChunk(
                chunkPoints, perChunkClass, nFullDims, "Stage 2.6");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.6");
        }

        // Phase 2a.7: per-channel amplitude+phase split, see commentary at
        // the matching insertion in the first overload.
        if (PerChannelSplitEnable != 0) {
            PerChannelSplitPerChunk(
                chunkPoints, perChunkClass,
                NbChannels, NbSamplesPerSpike, "Stage 2.7");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.7");
        }

        // Phase 2b: chunk-level warm-start CEM.  Lets boundary spikes
        // reassign across the new fine-grained label set and lets CEM
        // merge oversplit fragments via ConsiderDeletion.  Rebuilds
        // perChunkModels[] from the converged state.
        ChunkReCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.8");

        // Phase 2.5: second per-chunk DipSplit pass.
        //
        // The Phase 1c DipSplit ran before subspace reclustering, so it
        // could only see bimodality in the parent clusters produced by
        // Phase 1 CEM.  PerClusterCEMPerChunk (Phase 2a) re-partitions
        // those parents into finer pieces via likelihood-based CEM with
        // splits enabled — but CEM is a parametric Gaussian test that
        // absorbs single-dimension bimodality by inflating variance
        // along the bimodal axis (the inflation penalty is smaller than
        // the constant cost of a new cluster).  Clusters that emerge
        // from Phase 2a with a clear KDE valley in one PC projection
        // but a covariance the parametric test accepts are exactly the
        // cases this second pass catches.
        //
        // Per-chunk (not global) for the same reason as Phase 1c: a
        // chunk's spikes don't span the session-drift range, so apparent
        // bimodality from drift is suppressed.
        DipSplitPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims,
            "Stage 2.9");

        // Phase 2b.5: K-template chunk split (optional, off by default).
        // For each chunk, picks K well-isolated reference cluster means
        // and partitions every "source" cluster's spikes by their
        // nearest reference template.  Each viable (source, ref) bucket
        // becomes a new chunk-local cluster.  Phase 6 (cross-chunk
        // model matching) handles consolidation of the new clusters
        // into global units.  Enable with -KnnSplitPerChunkEnable 1.
        // -KnnSplitMode 0 (default): legacy nearest-template; mode 1:
        // klusters-faithful K-NN majority-vote (see wave_knn_split.h).
        if (KnnSplitPerChunkEnable) {
            if (KnnSplitMode == 1) {
                WaveKnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            } else {
                KnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            }
        }
        }   // end else (staged per-chunk path)
        if (FullCemSplitEnable) {
            FullCemSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims);
        }
	
    }


    // Phase 7 (Global EM) alignment also disabled: chunk means from
    // CEMTwoPhase are already blended by timeMergeIter and would produce
    // spurious shifts on clean data.  ConsiderDeletion / TimeShiftMergeEvaluate
    // handle the time-shift cases that actually occur (genuine same-unit
    // mergers across drift).

    // ── Post-Phase-2 alignment site ─────────────────────────────────────
    RunAlignmentBlock(TimeShiftAlignAfterPhase2, "Align");

    // Phase-ordering note: Phase 8 (global post-merge DipSplit) was
    // removed.  Its function is now served by per-chunk DipSplit at
    // Phase 1b and a second pass at Phase 2.5 — operating per-chunk
    // avoids the drift-axis false positives a global pass produces on
    // session-spanning clusters.  Phases 3 (mean-waveform harvest, the
    // [Stage 2.11 harvest] log tag below), 4, and 8 are intentionally non-major
    // phase numbers in the current pipeline; the comment block above
    // RunChunkedCEM enumerates which numbers are alive.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        LockedStderr( "[Stage 2.11 harvest] Mean waveform harvest (channel-major xcorr format)\n");
    // Populate ChunkModel::meanWav for template matching.
    // Done serially after the parallel chunk loop since all chunks share
    // the same .spk file handle and fseeko calls cannot be parallelised safely.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
        NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        char spkPathTM[STRLEN + 16];
        // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
        pickInputPath(spkPathTM, sizeof(spkPathTM), FileBase, "spk", ElecNo);
        FILE* spkTM = fopen(spkPathTM, "rb");
        if (spkTM) {
            for (int k = 0; k < nActive; k++) {
                const auto& pts  = chunkPoints[k];
                const auto& cls  = perChunkClass[k];
                const int   nPts = static_cast<int>(pts.size());

                // Build localClusterId → ChunkModel* map for this chunk
                std::unordered_map<int, ChunkModel*> lcToModel;
                for (auto& cm : perChunkModels[k])
                    if (cm.chunkIdx == k)
                        lcToModel[cm.localClusterId] = &cm;

                // Determine this chunk's time range so we can classify spikes
                // as left-edge (first 25%) / middle / right-edge (last 25%)
                // for the boundary-localised waveforms used in Phase 6's
                // cross-chunk xcorr.  Time = last feature dim, raw samples.
                float tMin = std::numeric_limits<float>::infinity();
                float tMax = -std::numeric_limits<float>::infinity();
                for (int i = 0; i < nPts; i++) {
                    const float t = Data[static_cast<size_t>(pts[i]) * nDims + (nDims - 1)];
                    if (t < tMin) tMin = t;
                    if (t > tMax) tMax = t;
                }
                const float tRange    = (tMax > tMin) ? (tMax - tMin) : 1.0f;
                const float tLeftEnd  = tMin + 0.25f * tRange;
                const float tRightBeg = tMin + 0.75f * tRange;

                // Accumulate per-cluster waveform sums.  Three accumulators
                // per cluster: full chunk, left-edge (first 25%), right-edge
                // (last 25%).  Every spike contributes to acc; spikes within
                // the edge windows additionally contribute to accLeft/Right.
                std::unordered_map<int, std::vector<int64_t>> acc, accLeft, accRight;
                std::unordered_map<int, int> nAcc, nAccLeft, nAccRight;
                std::vector<int16_t> row(static_cast<size_t>(wElems));
                for (int i = 0; i < nPts; i++) {
                    const int lc = cls[i];
                    if (lc == 0) continue;  // skip noise
                    if (!lcToModel.count(lc)) continue;
                    const int p2 = pts[i];
                    fseeko(spkTM,
                           static_cast<off_t>(p2) * wElems * sizeof(int16_t),
                           SEEK_SET);
                    if (fread(row.data(), sizeof(int16_t), wElems, spkTM)
                            != static_cast<size_t>(wElems)) continue;

                    // Apply committed shift (TimeShiftSplitEnable path):
                    // ensure the harvested waveform geometry matches the
                    // PCA features in Data[].
                    ShiftWaveformRowInPlace(row.data(), p2,
                                            NbChannels, NbSamplesPerSpike);

                    // Full-chunk accumulator
                    auto& a = acc[lc];
                    if (a.empty()) a.assign(static_cast<size_t>(wElems), 0);
                    // sample-major (.spk) → channel-major (XcorrDispatch)
                    for (int ch = 0; ch < NbChannels; ch++)
                        for (int s = 0; s < NbSamplesPerSpike; s++)
                            a[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                += row[static_cast<size_t>(s * NbChannels + ch)];
                    nAcc[lc]++;

                    // Edge accumulators.  A spike can only fall into one of
                    // the two edge windows (mutually exclusive at 25% each);
                    // middle-50% spikes contribute only to the chunk-wide mean.
                    const float t = Data[static_cast<size_t>(p2) * nDims + (nDims - 1)];
                    std::vector<int64_t>* edgeAcc = nullptr;
                    int* edgeN = nullptr;
                    if (t <= tLeftEnd)       { edgeAcc = &accLeft[lc];  edgeN = &nAccLeft[lc];  }
                    else if (t >= tRightBeg) { edgeAcc = &accRight[lc]; edgeN = &nAccRight[lc]; }
                    if (edgeAcc) {
                        if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                        for (int ch = 0; ch < NbChannels; ch++)
                            for (int s = 0; s < NbSamplesPerSpike; s++)
                                (*edgeAcc)[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                    += row[static_cast<size_t>(s * NbChannels + ch)];
                        (*edgeN)++;
                    }
                }
                // Finalise full-chunk meanWav
                for (auto& [lc, a] : acc) {
                    int n2 = nAcc[lc];
                    if (n2 == 0) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWav.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWav[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                }
                // Finalise meanWavLeft.  Require a minimum of 5 spikes for a
                // meaningful mean — below that, leave empty so Phase 6 falls
                // back to chunk-wide meanWav for this cluster.
                for (auto& [lc, a] : accLeft) {
                    const int n2 = nAccLeft[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavLeft.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavLeft[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersLeft = n2;
                }
                for (auto& [lc, a] : accRight) {
                    const int n2 = nAccRight[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavRight.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavRight[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersRight = n2;
                }
            }
            fclose(spkTM);
        }
    }
    // Declare overlap vote container here; ACTUAL build is deferred to
    // AFTER Phase 4 within-chunk merge has finalised the per-chunk
    // cluster IDs.  See the build block just before MergeChunkModels.
    //
    // Why deferred: vote keys are (clsK, clsK1) cluster ID pairs.
    // Building them from pre-Phase-4 perChunkClass bakes in cluster IDs
    // that Phase 4's lcRemap then merges away.  Phase 5 would receive
    // votes keyed by clusters that no longer exist, causing missed
    // cross-chunk matches (the vote-match step would search for
    // {origK.clsK_pre <-> origK1.clsK1_pre} pairs that have since
    // become {origK.canon_post <-> origK1.canon_post}).
    //
    // The companion comment at the allModels rebuild site below
    // documented the cluster-ID-consistency requirement but the
    // overlapVotes build had been left at its original pre-Phase-4
    // position.  This fix relocates it.
    std::vector<std::unordered_map<int,int>> overlapVotes(
        nActive > 0 ? nActive - 1 : 0);

    // -------------------------------------------------------------------
    // Phase 6: cross-chunk model matching
    // -------------------------------------------------------------------
    // Sanity-check mergeThresh against the chi²(nSpatialDims) distribution.
    // The symmetric Mahalanobis² distance between two draws from the SAME
    // Gaussian is distributed as chi²(nSpatialDims), so a threshold much
    // larger than chi²(nSpatialDims, 0.9999) means essentially every pair
    // is a candidate — the MNN filter degenerates and all local clusters
    // tend to union into one global component.
    // chi²(d, p) ≈ d * (1 - 2/(9d) + z_p * sqrt(2/(9d)))³  (Wilson-Hilferty)
    {
        const float d   = static_cast<float>(nSpatialDims);
        // z for p=0.9999 ≈ 3.719
        const float chi2_9999 = d * std::pow(1.0f - 2.0f/(9.0f*d) + 3.719f * std::sqrt(2.0f/(9.0f*d)), 3.0f);
        // z for p=0.99 ≈ 2.326
        const float chi2_99   = d * std::pow(1.0f - 2.0f/(9.0f*d) + 2.326f * std::sqrt(2.0f/(9.0f*d)), 3.0f);
        if (mergeThresh > chi2_9999 * 1.5f) {
            Output("WARNING: MergeThresh=%.1f is far above chi2(%d, 0.9999)=%.1f.\n"
                   "  With this threshold nearly every cluster pair is a candidate,\n"
                   "  which causes the MNN chain to collapse all clusters into one\n"
                   "  global component.\n"
                   "  Recommended: MergeThresh=%.1f (chi2(%d, 0.99))\n",
                   mergeThresh, nSpatialDims, chi2_9999, chi2_99, nSpatialDims);
        }
    }
    // ── Phase 5: within-chunk circular xcorr template matching (iterated) ─
    // Loops until no new merges occur or 10 iterations.  After each merge pass
    // the surviving clusters need updated meanWav vectors (the merged cluster
    // mean changes when two sub-clusters are combined), so the Phase 3 harvest
    // re-runs at the top of each iteration before the next xcorr comparison.
    if (TemplateMatchScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int _tmMax = (TemplateMatchIters > 0) ? TemplateMatchIters : 10;
        int _altNetGrowthStreak = 0;

        // Per-iter convergence digest (printed as a table after the loop).
        struct Phase4DigestRow {
            int iter; int merges; int spikesSplit; int kNew;
            int totalClusters; double wallMs;
        };
        std::vector<Phase4DigestRow> _phase4Digest;

        // Reset the Phase 4 median-template cache at phase start (patch
        // 0052).  Bounds cache memory across the run; entries accumulate
        // within this phase's iters and are reused when cluster
        // membership is unchanged.
        m_medianCache.clear();

        auto _countClusters = [&]() {
            std::set<long long> uniq;
            for (int _k = 0; _k < nActive; _k++)
                for (int _c : perChunkClass[_k])
                    if (_c != 0) uniq.insert(
                        static_cast<long long>(_k) * MaxPossibleClusters + _c);
            return static_cast<int>(uniq.size());
        };

        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
            const auto _iterT0 = std::chrono::steady_clock::now();
            m_phase4Iter = _tmIter;

            // Oscillation-guard bounce tracking (patch 0053).  A cluster
            // hash present pre-split, ABSENT post-split (the split
            // destroyed it), and present again post-merge (the merge
            // exactly reassembled it) is a round-trip bounce -> cooldown.
            std::set<uint64_t> _preSplitHashes, _postSplitHashes;
            bool _didSplitThisIter = false;
            auto _hashesOf = [&](const std::vector<std::vector<int>>& cls) {
                std::set<uint64_t> out;
                for (size_t _ck = 0; _ck < cls.size(); ++_ck) {
                    std::unordered_map<int, std::vector<int>> byLc;
                    const auto& cv = cls[_ck];
                    const auto& pv = chunkPoints[_ck];
                    for (size_t _i = 0; _i < cv.size(); ++_i)
                        if (cv[_i] != 0) byLc[cv[_i]].push_back(pv[_i]);
                    for (auto& _kv : byLc)
                        out.insert(ClusterMembershipHash(_kv.second));
                }
                return out;
            };
            // Phase 4b: optional alternating KnnSplit inside the Phase 4
            // loop — runs BEFORE merge so each iter is split→harvest→merge.
            // The closing iter of the loop is template-merge-only, so its
            // output going to Phase 5 is template-converged (no raw split
            // products escape to downstream phases).
            //
            // Two guards against runaway split-domination:
            //   * iter cap (AlternatingSplitMergeMaxIters, default 2)
            //   * net-growth abort (if WaveKnnSplit produced more new
            //     clusters than template-match merged for two iters in a
            //     row, stop running split; merge-only iters continue up
            //     to TemplateMatchIters).
            int _nSpikesSplit = 0;
            int _kNewThisIter = 0;
            const bool _altGate =
                AlternatingSplitMergeEnable != 0 &&
                KnnSplitPerChunkEnable != 0 && KnnSplitMode == 1 &&
                _tmIter < AlternatingSplitMergeMaxIters &&
                !(AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                  _altNetGrowthStreak >= 2);
            if (_altGate) {
                std::vector<std::vector<int>> _beforeSplit = perChunkClass;
                std::set<int> _kBefore;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kBefore.insert(c);
                if (AlternatingSplitCooldownIters > 0)
                    _preSplitHashes = _hashesOf(perChunkClass);
                if (QualityWeightedSplitEnable) {
                    // Quality-routed: dispatcher computes ISI contamination
                    // + waveform variance for an oversampled pool and routes
                    // each neediest cluster to CEM or knn.  Replaces the
                    // direct WaveKnn+FullCem calls.
                    QualityWeightedSplitDispatch(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                } else {
                    WaveKnnSplitPerChunk(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    if (FullCemSplitEnable) {
                        FullCemSplitPerChunk(
                            chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    }
                }
                if (AlternatingSplitCooldownIters > 0) {
                    _postSplitHashes = _hashesOf(perChunkClass);
                    _didSplitThisIter = true;
                }
                for (size_t _ckb = 0; _ckb < perChunkClass.size(); ++_ckb) {
                    const auto& aft = perChunkClass[_ckb];
                    const auto& bef = _beforeSplit[_ckb];
                    if (aft.size() != bef.size()) continue;
                    for (size_t _ii = 0; _ii < aft.size(); ++_ii)
                        if (aft[_ii] != bef[_ii]) ++_nSpikesSplit;
                }
                std::set<int> _kAfter;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kAfter.insert(c);
                _kNewThisIter = static_cast<int>(_kAfter.size())
                              - static_cast<int>(_kBefore.size());
            } else if (AlternatingSplitMergeEnable != 0 &&
                       _tmIter == AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Stage 2.11 split] AlternatingKnnSplit: iter cap reached "
                    "(%d); continuing with template-match only\n",
                    AlternatingSplitMergeMaxIters);
            } else if (AlternatingSplitMergeEnable != 0 &&
                       AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                       _altNetGrowthStreak >= 2 &&
                       _tmIter < AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Stage 2.11 split] AlternatingKnnSplit: aborted by "
                    "net-growth streak; continuing with template-match "
                    "only\n");
                _altNetGrowthStreak = 1000;  // suppress further logs
            }

            // Re-harvest meanWav with current perChunkClass
            if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                const int wElems = NbChannels * NbSamplesPerSpike;
                char spkPathTM2[STRLEN + 16];
                // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
                pickInputPath(spkPathTM2, sizeof(spkPathTM2), FileBase, "spk", ElecNo);
                FILE* spkTM2 = fopen(spkPathTM2, "rb");
                if (spkTM2) {
                    for (int k = 0; k < nActive; k++) {
                        const auto& pts2  = chunkPoints[k];
                        const auto& cls2  = perChunkClass[k];
                        const int   nPts2 = static_cast<int>(pts2.size());
                        std::unordered_map<int, ChunkModel*> lcToModel2;
                        for (auto& cm : perChunkModels[k])
                            if (cm.chunkIdx == k)
                                lcToModel2[cm.localClusterId] = &cm;
                        // Zero existing meanWav (and edge waveforms) for live clusters
                        for (auto& [lc2, pcm] : lcToModel2) {
                            pcm->meanWav.assign(static_cast<size_t>(wElems), 0);
                            pcm->meanWavLeft.clear();
                            pcm->meanWavRight.clear();
                        }
                        // Determine this chunk's time range for edge classification
                        float tMin2 = std::numeric_limits<float>::infinity();
                        float tMax2 = -std::numeric_limits<float>::infinity();
                        for (int i = 0; i < nPts2; i++) {
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            if (t < tMin2) tMin2 = t;
                            if (t > tMax2) tMax2 = t;
                        }
                        const float tRange2    = (tMax2 > tMin2) ? (tMax2 - tMin2) : 1.0f;
                        const float tLeftEnd2  = tMin2 + 0.25f * tRange2;
                        const float tRightBeg2 = tMin2 + 0.75f * tRange2;

                        std::unordered_map<int, std::vector<int64_t>> acc2, accLeft2, accRight2;
                        std::unordered_map<int, int> nAcc2, nAccLeft2, nAccRight2;
                        std::vector<int16_t> row2(static_cast<size_t>(wElems));
                        for (int i = 0; i < nPts2; i++) {
                            const int lc2 = cls2[i];
                            if (lc2 == 0 || !lcToModel2.count(lc2)) continue;
                            fseeko(spkTM2,
                                   static_cast<off_t>(pts2[i]) * wElems * sizeof(int16_t),
                                   SEEK_SET);
                            if (fread(row2.data(), sizeof(int16_t), wElems, spkTM2)
                                    != static_cast<size_t>(wElems)) continue;
                            ShiftWaveformRowInPlace(row2.data(), pts2[i],
                                                    NbChannels, NbSamplesPerSpike);
                            auto& a2 = acc2[lc2];
                            if (a2.empty()) a2.assign(static_cast<size_t>(wElems), 0);
                            for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                    a2[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                        += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                            nAcc2[lc2]++;

                            // Edge accumulators
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            std::vector<int64_t>* edgeAcc = nullptr;
                            int* edgeN = nullptr;
                            if (t <= tLeftEnd2)       { edgeAcc = &accLeft2[lc2];  edgeN = &nAccLeft2[lc2];  }
                            else if (t >= tRightBeg2) { edgeAcc = &accRight2[lc2]; edgeN = &nAccRight2[lc2]; }
                            if (edgeAcc) {
                                if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                                for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                    for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                        (*edgeAcc)[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                            += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                                (*edgeN)++;
                            }
                        }
                        for (auto& [lc2, a2] : acc2) {
                            int n2b = nAcc2[lc2];
                            if (n2b == 0) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWav.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWav[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accLeft2) {
                            const int n2b = nAccLeft2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavLeft.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavLeft[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accRight2) {
                            const int n2b = nAccRight2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavRight.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavRight[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                    }
                    fclose(spkTM2);
                }
            }
            LockedStderr( "[Stage 2.11] Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = (MedianKnnTemplateMatchEnable != 0)
                ? WithinChunkTemplateMatchMedianKnn(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore)
                : WithinChunkTemplateMatch(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore);

            // Phase 4b streak update — now that this iter's merge count
            // is known, compare against the iter's split-induced cluster
            // growth.  Updating here (rather than at split time) means
            // the streak reflects the OUTCOME of an alternation round
            // (split + cleanup-merge together), not the split alone.
            // Oscillation-guard bounce detection (patch 0053): a cluster
            // hash that was pre-split, destroyed by the split, and exactly
            // recreated by the merge round-tripped -> cooldown it so the
            // next iters' split budget skips it.
            if (_didSplitThisIter && AlternatingSplitCooldownIters > 0) {
                const std::set<uint64_t> _postMergeHashes =
                    _hashesOf(perChunkClass);
                int _nBounced = 0;
                for (uint64_t _h : _postMergeHashes) {
                    if (_preSplitHashes.count(_h)
                        && !_postSplitHashes.count(_h)) {
                        m_splitCooldown[_h] =
                            _tmIter + AlternatingSplitCooldownIters;
                        ++_nBounced;
                    }
                }
                if (_nBounced > 0) {
                    LockedStderr("[Stage 2.11 split] oscillation guard: %d cluster(s) "
                                 "round-tripped (split->merge no-op); cooled "
                                 "down for %d iter(s)\n",
                                 _nBounced, AlternatingSplitCooldownIters);
                }
            }

            if (_altGate) {
                if (_kNewThisIter > _nMerged) ++_altNetGrowthStreak;
                else                          _altNetGrowthStreak = 0;
                LockedStderr(
                    "[Stage 2.11 split] AlternatingKnnSplit (iter %d): %d spike "
                    "labels changed; +%d new clusters vs %d merged (net "
                    "growth streak %d/2)\n",
                    _tmIter + 1, _nSpikesSplit, _kNewThisIter, _nMerged,
                    _altNetGrowthStreak);
            }


            // Phase 4b heavy realignment: at the END of every Phase 4 iter,
            // realign each per-chunk cluster's spikes (klusters-faithful
            // xcorr against the cluster's mean) and refeaturize only the
            // changed spikes from the cached .fil group channels.  The
            // next iter's meanWav harvest (which reads .spk through
            // m_cumShift) and Phase 4b WaveKnnSplit (which reads Data[])
            // both see the realigned state.
            //
            // Gated on KlustersRealignAfterPhase4; requires
            // KlustersRealignEnable so MaxShift/MinSize have valid values.
            if (KlustersRealignAfterPhase4 != 0 &&
                m_timeShiftReady &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                std::vector<int> _changedSpikes;
                _changedSpikes.reserve(1024);
                const int _nChanged = KlustersStyleRealignPerChunkClusters(
                    chunkPoints, perChunkClass,
                    NbChannels, NbSamplesPerSpike,
                    _changedSpikes);
                if (_nChanged > 0) {
                    RefeaturizeChangedSpikes(
                        _changedSpikes, NbChannels, NbSamplesPerSpike);
                }
            }

            const double _iterMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - _iterT0).count();
            _phase4Digest.push_back({ _tmIter + 1, _nMerged, _nSpikesSplit,
                                      _kNewThisIter, _countClusters(),
                                      _iterMs });

            if (_nMerged == 0 && _nSpikesSplit == 0) break;
        }
        // ── Phase 4 convergence digest ────────────────────────────────
        LockedStderr("[Stage 2.11] convergence digest:\n");
        LockedStderr("    iter | merges | splits | +new | clusters | wall_ms\n");
        LockedStderr("    -----+--------+--------+------+----------+--------\n");
        for (const auto& _r : _phase4Digest) {
            LockedStderr("    %4d | %6d | %6d | %4d | %8d | %7.0f\n",
                         _r.iter, _r.merges, _r.spikesSplit, _r.kNew,
                         _r.totalClusters, _r.wallMs);
        }
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 2.11");
    }

        // ── Phase 4c: neighborhood-remix split (patch 0062) ──────────────
        // Runs after Phase 4 converges; pools each random source with its N
        // closest clusters and re-splits, guarded by disjointness + a
        // tightness mask.  No-op unless -Phase4cRemixEnable 1.
        RunPhase4cRemix(chunkPoints, perChunkClass, perChunkModels, nFullDims);

        // ── Phase 8: variance-targeted knn-split (patch 0067) ───────────
        // Iterates Phase 4b's WaveKnn-split on high-variance clusters
        // only.  No-op unless -Phase8VarianceSplitEnable 1.  FullCEM is
        // intentionally skipped — diffuse clusters have no Gaussian-
        // mixture modes for FullCEM to find; WaveKnn redistributes their
        // spikes to nearby reference clusters instead.
        RunPhase8VarianceSplit(chunkPoints, perChunkClass, perChunkModels, nFullDims);

    // Rebuild pointPacked[] using post-template-merge cluster IDs.
    // perChunkAssign was built with original Phase 1 IDs; after
    // WithinChunkTemplateMatch the IDs in perChunkClass differ.
    // packedToGlobal (built after MergeChunkModels) uses post-merge IDs,
    // so pointPacked must use the same scheme.
    std::fill(pointPacked.begin(), pointPacked.end(), -1);
    for (int k = 0; k < nActive; k++) {
        const auto& pts = chunkPoints[k];
        const auto& cls = perChunkClass[k];
        const int nPts  = static_cast<int>(pts.size());
        for (int i = 0; i < nPts; i++) {
            const int p2 = pts[i];
            if (pointPacked[p2] < 0)  // first-write-wins for overlap spikes
                pointPacked[p2] = k * MaxPossibleClusters + cls[i];
        }
    }

    // Rebuild allModels from perChunkModels now that within-chunk template
    // matching has finalised the local cluster set.  Cluster IDs in
    // perChunkClass and in allModels must agree so that MergeChunkModels vote
    // keys (clsK * MaxPossibleClusters + clsK1) resolve correctly.
    allModels.clear();
    for (int k = 0; k < nActive; k++)
        for (auto& cm : perChunkModels[k])
            allModels.push_back(cm);  // copy — perChunkModels still needed below

    // ── Build overlap vote matrix (deferred from Phase 3) ─────────────
    // Now that Phase 4's lcRemap chain has settled and perChunkClass
    // reflects the final per-chunk cluster IDs, build the (clsK, clsK1)
    // vote keys against the CURRENT cluster set.  Phase 5's
    // MergeChunkModels reads these keys directly.
    //
    // (Vote VALUES — shared spike counts per cluster-id pair — are also
    // affected by the move: a pre-Phase-4 vote key {clsA_pre,
    // clsB_pre} with N votes that gets merged in chunk K into
    // {canonA_post, clsB_pre} now correctly aggregates with any other
    // pre-merge keys that landed on canonA_post in the same chunk.
    // No vote double-counting because each shared spike contributes
    // exactly one (clsK, clsK1) increment, regardless of which pre-
    // merge cluster ID it had.)
    if (chunkOverlapFrac > 0.0f) {
        for (int k = 0; k < nActive - 1; k++) {
            const int origK = activeOrigIdx[k];
            if (origK + 1 != activeOrigIdx[k + 1]) continue;
            if (origK >= static_cast<int>(overlapForPair.size())) continue;

            auto& votes = overlapVotes[k];
            for (const auto& oe : overlapForPair[origK]) {
                if (oe.localK  >= static_cast<int>(perChunkClass[k].size()))   continue;
                if (oe.localK1 >= static_cast<int>(perChunkClass[k+1].size())) continue;
                const int clsK  = perChunkClass[k][oe.localK];
                const int clsK1 = perChunkClass[k+1][oe.localK1];
                if (clsK == 0 || clsK1 == 0) continue;  // noise
                votes[clsK * MaxPossibleClusters + clsK1]++;
            }
            int totalVotes = 0;
            for (const auto& [key, cnt] : votes) totalVotes += cnt;
            Output("  [Stage 3.1] Overlap pair orig%d/orig%d (active %d/%d): "
                   "%d shared spikes, %d (clsK,clsK1) pairs\n",
                   origK, origK + 1, k, k + 1, totalVotes,
                   static_cast<int>(votes.size()));
        }
    }

    // ── Post-Phase-5 alignment site ─────────────────────────────────────
    // Within-chunk template merges have consolidated per-chunk clusters.
    // Aligns spikes against the merged means before cross-chunk matching.
    RunAlignmentBlock(TimeShiftAlignAfterPhase4, "Align");

    LockedStderr( "[Stage 3.1] Cross-chunk model matching (overlap-vote + edge-xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, overlapVotes);
    LockedStderr( "[Stage 3.1] Cross-chunk merge produced %d global clusters\n", nGlobal);
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Stage 3.1");
    if (nGlobal < 1) {
        Output("Merge produced no real clusters — falling back to CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // Guard: if nGlobal >= MaxPossibleClusters, globalClusterIds would write
    // out-of-bounds into ClassAlive[].  This is NOT a MergeThresh problem —
    // the merge can succeed perfectly (100% vote-matches) yet produce more
    // global clusters than MaxPossibleClusters when the table is set too small
    // relative to nChunks * MaxClusters.  Fall back to CEMTwoPhase and emit
    // a precise diagnostic so the user knows what to increase.
    if (nGlobal >= MaxPossibleClusters) {
        // Emit on stderr unconditionally — this is a hard fall-back from
        // chunked CEM to single-pass CEMTwoPhase that the user must know
        // about (it dramatically changes the output cluster set and was
        // silent under Output() unless -Screen 1 was set).  Keep the
        // Output() call so logging still captures it.
        fprintf(stderr,
                "[Stage 3.1] WARNING: MergeChunkModels produced %d global "
                "clusters >= MaxPossibleClusters (%d) — falling back to "
                "CEMTwoPhase.  Fix: re-run with -MaxPossibleClusters %d "
                "or higher (rule of thumb: nChunks * MaxClusters).\n",
                nGlobal, MaxPossibleClusters, nGlobal + 100);
        Output("WARNING: MergeChunkModels produced %d global clusters >= "
               "MaxPossibleClusters (%d).\n"
               "  The cross-chunk merge itself succeeded — this is a table-size\n"
               "  limit, not a MergeThresh problem.  The number of unique\n"
               "  global units exceeds the pre-allocated cluster table.\n"
               "  Fix: set -MaxPossibleClusters to at least %d\n"
               "  (a safe rule: nChunks * MaxClusters; use 300-500 for long\n"
               "  recordings with many chunks).\n"
               "  Falling back to CEMTwoPhase on the full session.\n",
               nGlobal, MaxPossibleClusters,
               nGlobal + 10);
        return CEMTwoPhase(timeMergeIter);
    }

    // Build lookup: packed -> globalClusterId
    std::unordered_map<int,int> packedToGlobal;
    packedToGlobal.reserve(allModels.size());
    for (const auto& cm : allModels)
        packedToGlobal[cm.chunkIdx * MaxPossibleClusters + cm.localClusterId] =
            cm.globalClusterId;

    // ── Post-Phase-6 alignment site ─────────────────────────────────────
    // Runs after cross-chunk model matching: per-chunk clusters have been
    // mapped to global cluster IDs (above) but no Class[] update has happened
    // yet — that occurs inside the Phase 7 init block below.  Alignment here
    // uses the still-per-chunk Class state, which is correct: spikes are
    // aligned to their per-chunk cluster means BEFORE the per-chunk → global
    // remap kicks in.  This catches alignment drift WITHIN per-chunk clusters
    // that Phase 5's template matching may have left behind.
    RunAlignmentBlock(TimeShiftAlignAfterPhase5, "Align");

    // -------------------------------------------------------------------
    // Phase 7: global warm-start EM (full dimensionality including time)
    //
    // Seed Class[] from the matched global labels, then run a short
    // full-dimensional EM pass.  Because the starting assignment is
    // already well-separated, this typically converges in < 10 iterations.
    // A drifting unit correctly fits a tilted ellipse in (PCA + time) space.
    // -------------------------------------------------------------------
    nDims  = nFullDims;
    nDims2 = nDims * nDims;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = 0;
    for (int p = 0; p < nPoints; p++) {
        const int pp = pointPacked[p];
        auto it = (pp >= 0) ? packedToGlobal.find(pp) : packedToGlobal.end();
        const int g = (it != packedToGlobal.end()) ? it->second : 0;
        // g is in [0, nGlobal] and nGlobal < MaxPossibleClusters (checked above)
        Class[p] = g;
        ClassAlive[g] = 1;
    }
    Reindex();

    float score;
    if (globalMergeIter <= 0) {
        // GlobalMerge=0: skip Phase 7 entirely.  Emit one MStep/EStep so
        // LogP is valid for ComputeScore(), but do not reassign Class[].
        Output("Phase 7 skipped (GlobalMerge=0) — using Phase 6 assignment directly\n");
        // Force CPU path for Phase 7 post-merge scoring.
        // GPU EStep writes d_LogP in GPU memory; the CPU LogP.m_Data clamp below
        // would be a no-op on the GPU path.  Temporarily null gpu so MStep/EStep/
        // ComputeScore all run on CPU, where LogP.m_Data is authoritative.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        // Reassign any spikes whose cluster was deleted by MStep (singular covariance)
        // to noise (class 0) so EStep and ComputeScore see valid Class[] values.
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();
        {
            int nNan = 0;
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) { lp = kLargeLogP; nNan++; }
            }
            if (nNan > 0)
                Output("Phase3-skip: clamped %d non-finite LogP entries\n", nNan);
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu);
#endif
        if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
    } else {
        LockedStderr( "[Stage 3.2] Global warm-start EM\n");
        Output("Phase 7: global warm-start EM — %d clusters, max %d iters\n",
               nClustersAlive, globalMergeIter);
        int   iter = 0, nChanged = 1;
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu2 = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();  // CPU path: populates LogP.m_Data directly
        {
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) lp = kLargeLogP;
            }
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu2);
#endif
        FullStep = 1;
        for (; iter < globalMergeIter; iter++) {
            MStep(); EStep(); nChanged = CStep(); ConsiderDeletion();
            score = ComputeScore();
            if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
            if (Verbose >= 2)
                Output("  P3 iter %d: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);
            FullStep = 1;
            if (nChanged == 0) { Output("Phase 7 converged at iter %d\n", iter); break; }
        }
    }

    // Per-phase quality summary after Phase 7 (Global EM) — gives a
    // checkpoint before DipSplit potentially mutates the cluster set.
    LogGlobalClusterState("Stage 3.2 (Global EM)");
    ReportClusterQuality("Stage 3.2");

    // ── Phase 6a (optional): post-merge cluster realignment ────────────────
    // Mirror of Driver A's Phase 7a.  See Driver A's body for rationale
    // (cross-chunk merge consolidates chunk-local clusters into global
    // units; spikes' Phase-1.5 alignments may no longer be optimal vs.
    // the new global means).
    if (TimeShiftAlignPostMerge != 0 && m_timeShiftReady) {
        LockedStderr( "[Stage 3.3] Post-merge cluster realignment\n");
        const int nShifted = TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);
        int nCOMShifted = 0;
        if (EnergyCOMRealign != 0) {
            LockedStderr( "[Stage 3.3] Energy-COM realignment\n");
            nCOMShifted = EnergyCOMRealignPhase(NbChannels, NbSamplesPerSpike);
        }
        if (nShifted > 0 || nCOMShifted > 0) {
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuR = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuR);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Stage 3.3 (post-merge realign)"); ReportClusterQuality("Stage 3.3");
        }
    }

    // ── Phase 6b (optional): mean-waveform subtraction merge ─────────
    // Mirror of Driver A's Phase 7b.  See FinalMeanSubtractionMerge for
    // algorithm; opt-in via -MeanSubtractionMergeEnable 1 with threshold
    // -MeanSubtractionMergeThresh (default 0.05) and cyclic-shift
    // radius -MeanSubtractionMergeMaxShift (default 3).
    if (MeanSubtractionMergeEnable != 0) {
        LockedStderr( "[Stage 3.4] Mean-subtraction template merge "
                        "(threshold D < %.3f)\n",
                static_cast<double>(MeanSubtractionMergeThresh));
        const int nMergedSub =
            FinalMeanSubtractionMerge(NbChannels, NbSamplesPerSpike);
        if (nMergedSub > 0) {
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuB = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuB);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Stage 3.4 (mean-sub merge)"); ReportClusterQuality("Stage 3.4");
        }
    }

    // ── Phase 7c — klusters-faithful per-spike realignment ─────────────
    // Final tightening pass that uses XcorrDispatch (the same library
    // klusters' interactive "Realign top-ch" button uses) to compute a
    // per-spike shift against each cluster's pre-aligned mean template.
    // Updates m_cumShift; TimeShiftFinalize at the very end of the run
    // will read .fil at the new offsets and rewrite .spk/.fet.
    //
    // Opt-in via -KlustersRealignEnable 1.  No-op when the time-shift
    // probe never initialised or no spikes meet the min-cluster-size
    // threshold.  See KK::KlustersStyleRealignAllClusters for details.
    if (KlustersRealignEnable != 0) {
        LockedStderr(
                "[Stage 3.6] Klusters-faithful per-spike realignment "
                "(maxShift=%d samples, minSize=%d)\n",
                KlustersRealignMaxShift, KlustersRealignMinSize);
        const int nChanged =
            KlustersStyleRealignAllClusters(NbChannels, NbSamplesPerSpike);
        if (nChanged > 0) {
            // m_cumShift was updated.  We do NOT re-run MStep/EStep here:
            // the clustering result (.clu) is final at this point, and
            // .spk/.fet re-projection happens in TimeShiftFinalize at the
            // very end.  Re-running EStep would require fresh features
            // which we don't have yet (would need RefeaturizeFromShifts
            // first, doubling the .fil-read cost).  Leave the score and
            // BestMeans as-is.
            LogGlobalClusterState("Stage 3.6 (klusters realign)");
        }
    }

    // Phase 8 DipSplit removed (see Driver A above for rationale).

    // Per-phase quality summary at end of pipeline (after DipSplit).
    // See KK::ReportClusterQuality for metric definitions.
    if (Phase9CCGMergeEnable) Phase9CCGMerge(samplingRate);
    ReportClusterQuality("final");

    Output("RunChunkedCEM done: %d clusters, score %.7g\n", nClustersAlive, score);
    return score;
}


// ---------------------------------------------------------------------------
// RefeaturizeFromShifts
//
// For every spike whose xcorr shift is non-zero, re-projects the aligned
// waveform through the saved PCA eigenvectors and updates Data[] in-place.
//
// PCA file format (written by process_pca -e):
//   int32  magic = 0x50434145 ("PCAE")
//   int32  version = 1
//   int32  nChannels
//   int32  data2use     (samples per channel used for PCA)
//   int32  nComponents  (PCs kept)
//   int32  recShift     (first sample offset within waveform)
//   int32  isCentered   (1 = subtract mean before projecting)
//   for each channel:
//     double[data2use]             per-channel mean
//     double[data2use*nComponents] eigenvectors (col-major: col=component)
//
// Overlap-resolution invariant: spikeShifts[p] was set by home-chunk
// owned by the shift-probe (m_cumShift[p]), so every spike's shift is
// derived from the chunk where it naturally lives.  Overlap spikes that
// also appeared in a later chunk retain their home-chunk shift here.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// KK::WritePhase15Checkpoint
//
// After RefeaturizeFromShifts (Phase 4), write corrected .spk
// and .fet files using the pending-file pattern from Klusters:
//   1. Write to SESSION.spk.N.pending and SESSION.fet.N.pending
//   2. Rename each .pending file over the original when fully written
//
// This ensures the originals are never partially overwritten; on failure
// the originals remain intact.
//
// .spk: for each shifted spike, re-extracts from .fil at
//       (rawTs + shift - PeakSampleIndex), matching Klusters extTs.
//       Unshifted spikes are copied from the original .spk unchanged.
//
// .fet: for each shifted spike, writes updated PCA features from Data[]
//       (set by RefeaturizeFromShifts) with extTs = rawTs+shift in the
//       last (timestamp) column.  Unshifted spikes are copied unchanged.
//
// .res: NOT modified.  The spike detection timestamp is the sample of the
//       peak in the raw signal — that physical event did not move.  Only
//       the extraction window moved.  Matches Klusters' resTs = original.
// ---------------------------------------------------------------------------
void KK::WritePhase15Checkpoint(const std::vector<int>& spikeShifts,
                                  int nChan, int nSamplesPerSpike)
{
    const int nShifted = static_cast<int>(
        std::count_if(spikeShifts.begin(), spikeShifts.end(),
                      [](int s){ return s != 0 && s != std::numeric_limits<int>::min(); }));
    if (nShifted == 0) {
        Output("WritePhase15Checkpoint: no shifts — files unchanged\n");
        return;
    }
    Output("WritePhase15Checkpoint: updating %d / %d spikes\n", nShifted, nPoints);

    // Read exact int64 timestamps directly from .res (avoids float precision loss:
    // a timestamp of ~1e8 samples stored as float has ±13 samples round-trip error).
    char resPathWPC[STRLEN + 16];
    resolveAnyC(resPathWPC, sizeof(resPathWPC), FileBase, "res", ElecNo);
    FILE* resWPC = fopen(resPathWPC, "rb");
    if (!resWPC)
        Output("WritePhase15Checkpoint: cannot open .res — falling back to float timestamps\n");

    char spkOrig[STRLEN+16], fetOrig[STRLEN+16];
    char spkTmp [STRLEN+32], fetTmp [STRLEN+32];
    // Pick the variant (canonical .spk.N/.fet.N or stderiv .spkD.N/.fetD.N)
    // that actually exists on disk, then derive the .pending names from the
    // picked paths.  On success we rename .pending → original, which must
    // therefore preserve the variant of the file we read.
    pickInputPath(spkOrig, sizeof(spkOrig), FileBase, "spk", ElecNo);
    pickInputPath(fetOrig, sizeof(fetOrig), FileBase, "fet", ElecNo);
    snprintf(spkTmp,  sizeof(spkTmp),  "%s.pending", spkOrig);
    snprintf(fetTmp,  sizeof(fetTmp),  "%s.pending", fetOrig);

    const float sessionSamples = timeRawMax - timeRawMin;
    const int   timeDimIdx     = nDims - 1;
    const int   waveSamples    = nChan * nSamplesPerSpike;

    // ── .spk: open original for read, pending for write ──────────────────
    FILE* spkR = fopen(spkOrig, "rb");
    FILE* spkW = fopen(spkTmp,  "wb");

    // Open .fil for re-extraction of shifted spikes
    char filPath[STRLEN + 8];
    snprintf(filPath, sizeof(filPath), "%s.fil", FileBase);
    FILE* filF = (NbTotalChannels > 0 && !GroupChannelIds.empty())
               ? fopen(filPath, "rb") : nullptr;
    if (!filF)
        Output("WritePhase15Checkpoint: .fil not available — shifted spikes "
               "copied from .spk (circular shift artefact possible)\n");

    if (!spkR || !spkW) {
        if (spkR) fclose(spkR);
        if (spkW) fclose(spkW);
        if (filF) fclose(filF);
        Output("WritePhase15Checkpoint: cannot open .spk files — skipping\n");
        goto skip_spk;
    }
    {
        std::vector<int16_t> spkRow(static_cast<size_t>(waveSamples));
        std::vector<int16_t> filRow;
        if (filF) filRow.resize(static_cast<size_t>(NbTotalChannels));

        for (int p = 0; p < nPoints; p++) {
            // Read original waveform
            if (fread(spkRow.data(), sizeof(int16_t), waveSamples, spkR)
                    != static_cast<size_t>(waveSamples)) {
                Output("WritePhase15Checkpoint: .spk short read at spike %d\n", p);
                break;
            }

            const int sh = (p < static_cast<int>(spikeShifts.size())) ? spikeShifts[p] : 0;
            if (sh != 0 && sh != std::numeric_limits<int>::min()) {
                if (filF) {
                    // Re-extract from .fil at extTs - PeakSampleIndex
                    int64_t rawTsWPC = 0;
                    if (resWPC) {
                        fseeko(resWPC, static_cast<off_t>(p) * sizeof(int64_t), SEEK_SET);
                        { size_t _r = fread(&rawTsWPC, sizeof(int64_t), 1, resWPC); (void)_r; }
                    }
                    if (rawTsWPC == 0 && sessionSamples > 0.0f) {
                        const float normTs = Data.m_Data[p * nDims + timeDimIdx];
                        rawTsWPC = static_cast<int64_t>(normTs * sessionSamples + timeRawMin);
                    }
                    const int64_t off  = rawTsWPC + sh - PeakSampleIndex;
                    bool ok = (off >= 0);
                    if (ok) {
                        fseeko(filF, off * NbTotalChannels * 2, SEEK_SET);
                        for (int s = 0; s < nSamplesPerSpike && ok; s++) {
                            if (fread(filRow.data(), 2, NbTotalChannels, filF)
                                    != static_cast<size_t>(NbTotalChannels)) { ok=false; break; }
                            for (int c = 0; c < nChan; c++) {
                                const int gc = (c < static_cast<int>(GroupChannelIds.size()))
                                             ? GroupChannelIds[c] : -1;
                                spkRow[s * nChan + c] = (gc >= 0 && gc < NbTotalChannels)
                                                      ? filRow[gc] : 0;
                            }
                        }
                        // For stderiv sessions the original .spkD on disk stores
                        // SDIFF_ALLPAIRS + temporal-diff output — not raw voltages.
                        // Apply the same transform so our .spkD.pending matches
                        // the format ndm_extractspikes_stderiv would have written
                        // at this timestamp.
                        if (ok && m_timeShiftBasis.isStderiv) {
                            ApplySdiffAllpairsTemporalDiff(spkRow.data(),
                                                           nChan,
                                                           nSamplesPerSpike);
                        }
                    }
                    // If .fil read fails, keep original (already in spkRow)
                }
                // If no .fil, spkRow already holds the original; the circular
                // shift applied by the shift-probe is NOT in .spk yet (write-
                // back was suppressed), so we leave it as-is.
            }

            if (fwrite(spkRow.data(), sizeof(int16_t), waveSamples, spkW)
                    != static_cast<size_t>(waveSamples)) {
                Output("WritePhase15Checkpoint: .spk write failed at spike %d\n", p);
                break;
            }
        }
        fclose(spkR); fclose(spkW);
        if (filF) fclose(filF);
    }
    rename(spkTmp, spkOrig);
    Output("  .spk updated\n");
    skip_spk:;

    // ── .fet: open original for read (header + all rows), pending for write ──
    {
        FILE* fetR = fopen(fetOrig, "rb");
        FILE* fetW = fopen(fetTmp,  "wb");
        if (!fetR || !fetW) {
            if (fetR) fclose(fetR);
            if (fetW) fclose(fetW);
            Output("WritePhase15Checkpoint: cannot open .fet files — skipping\n");
            goto skip_fet;
        }

        int32_t nFetDims = 0;
        if (fread(&nFetDims, sizeof(int32_t), 1, fetR) != 1 || nFetDims <= 0) {
            fclose(fetR); fclose(fetW);
            Output("WritePhase15Checkpoint: bad .fet header\n");
            goto skip_fet;
        }
        fwrite(&nFetDims, sizeof(int32_t), 1, fetW);

        const int nd = static_cast<int>(nFetDims);
        std::vector<int64_t> rowBuf(static_cast<size_t>(nd));

        for (int p = 0; p < nPoints; p++) {
            if (fread(rowBuf.data(), sizeof(int64_t), nd, fetR)
                    != static_cast<size_t>(nd)) {
                Output("WritePhase15Checkpoint: .fet short read at spike %d\n", p);
                break;
            }
            const int sh = (p < static_cast<int>(spikeShifts.size())) ? spikeShifts[p] : 0;
            if (sh != 0 && sh != std::numeric_limits<int>::min()) {
                // PCA feature columns from Data[] (already updated by RefeaturizeFromShifts)
                const float* row  = Data.m_Data + p * nDims;
                const int nPCACols = std::min(nDims - 1, nd - 1);
                for (int d = 0; d < nPCACols; d++) {
                    // Denormalise: raw = norm / dimRange + dimMin
                    const float raw = (dimRange_[d] > 0.0f)
                        ? row[d] / dimRange_[d] + dimMin_[d]
                        : dimMin_[d];
                    rowBuf[static_cast<size_t>(d)] = static_cast<int64_t>(std::llroundf(raw));
                }
                // Last column: extTs = rawTs + sh (exact int64)
                if (nd > 1) {
                    int64_t rawTsF = 0;
                    if (resWPC) {
                        fseeko(resWPC, static_cast<off_t>(p) * sizeof(int64_t), SEEK_SET);
                        { size_t _r = fread(&rawTsF, sizeof(int64_t), 1, resWPC); (void)_r; }
                    }
                    if (rawTsF == 0 && sessionSamples > 0.0f) {
                        const float normTs = row[nDims - 1];
                        rawTsF = static_cast<int64_t>(normTs * sessionSamples + timeRawMin);
                    }
                    rowBuf[static_cast<size_t>(nd - 1)] = rawTsF + sh;
                }
            }
            if (fwrite(rowBuf.data(), sizeof(int64_t), nd, fetW)
                    != static_cast<size_t>(nd)) {
                Output("WritePhase15Checkpoint: .fet write failed at spike %d\n", p);
                break;
            }
        }
        fclose(fetR); fclose(fetW);
        rename(fetTmp, fetOrig);
        Output("  .fet updated\n");
    }
    skip_fet:;

    // ── patch85: .res rewrite ────────────────────────────────────────────
    //
    // Bug fix: pre-patch, WritePhase15Checkpoint re-extracted .spk content
    // from .fil at rawTs + sh - PeakSampleIndex and wrote .fet's last
    // column as rawTs + sh — but NEVER rewrote .res.  The on-disk
    // asymmetry left .res holding original detection times while
    // .spk/.fet content was anchored at sh-shifted positions.  Klusters
    // (and any other reader that uses .res[i] as the anchor for spike
    // i's window) saw spike windows offset from each other by exactly
    // the per-spike shift, producing peaks scattered across a
    // 2*m_timeShiftMaxAbs + 1 sample range when cluster members were
    // overlaid.  Mean waveforms smeared.  Cluster sorting itself stayed
    // good because feature-space distances are independent of the
    // absolute time anchor.
    //
    // Fix: write .res.pending with rawTs + sh for shifted spikes,
    // rawTs for the rest, then atomic rename to .res.  Same .pending
    // discipline as .spk and .fet above so a crash mid-write doesn't
    // leave a half-written .res.
    //
    // Skipped (no-op, original .res preserved) when:
    //   - resWPC failed to open earlier (we'd have to fall back to
    //     Data[]'s normalised time-dim, which has ±13 sample float-
    //     precision loss at session-scale timestamps — that would
    //     CREATE dispersion rather than fix it).
    //   - the .res.pending file can't be created.
    //   - the rewrite loop aborts mid-write (partial .pending removed).
    if (resWPC) {
        char resOrigP85[STRLEN + 16];
        char resTmpP85 [STRLEN + 32];
        // Rewrite the SAME file opened above: resolving again would compose
        // this method's own name and could leave two .res for one group.
        std::strncpy(resOrigP85, resPathWPC, sizeof(resOrigP85));
        resOrigP85[sizeof(resOrigP85) - 1] = '\0';
        snprintf(resTmpP85,  sizeof(resTmpP85),  "%s.pending", resOrigP85);

        FILE* resWriteP85 = fopen(resTmpP85, "wb");
        if (!resWriteP85) {
            Output("WritePhase15Checkpoint: cannot create %s — .res preserved\n",
                   resTmpP85);
        } else {
            fseeko(resWPC, 0, SEEK_SET);
            int nResWritten = 0;
            bool resOk = true;
            int  nResShifted = 0;
            for (int p = 0; p < nPoints && resOk; ++p) {
                int64_t rawTsP85 = 0;
                if (fread(&rawTsP85, sizeof(int64_t), 1, resWPC) != 1) {
                    Output("WritePhase15Checkpoint: .res short read at "
                           "spike %d — aborting .res rewrite\n", p);
                    resOk = false;
                    break;
                }
                const int shP85 = (p < static_cast<int>(spikeShifts.size()))
                                ? spikeShifts[p] : 0;
                const bool shiftValid = (shP85 != 0 &&
                                         shP85 != std::numeric_limits<int>::min());
                const int64_t outTs = shiftValid
                                    ? (rawTsP85 + static_cast<int64_t>(shP85))
                                    : rawTsP85;
                if (fwrite(&outTs, sizeof(int64_t), 1, resWriteP85) != 1) {
                    Output("WritePhase15Checkpoint: .res write failed at "
                           "spike %d — aborting .res rewrite\n", p);
                    resOk = false;
                    break;
                }
                ++nResWritten;
                if (shiftValid) ++nResShifted;
            }
            fclose(resWriteP85);

            if (resOk && nResWritten == nPoints) {
                // Atomic rename — same discipline as .spk / .fet above.
                if (rename(resTmpP85, resOrigP85) == 0) {
                    Output("  .res updated (%d spikes, %d shifted)\n",
                           nResWritten, nResShifted);
                } else {
                    Output("WritePhase15Checkpoint: rename %s -> %s failed "
                           "— .res preserved\n", resTmpP85, resOrigP85);
                    ::remove(resTmpP85);
                }
            } else {
                // Partial write — leave original .res intact.
                ::remove(resTmpP85);
                Output("  .res NOT updated (wrote %d of %d) — "
                       "original preserved\n", nResWritten, nPoints);
            }
        }
    } else {
        Output("WritePhase15Checkpoint: .res not opened for read — "
               "cannot rewrite (would lose precision via Data[] fallback)\n");
    }

    if (resWPC) fclose(resWPC);
    Output("WritePhase15Checkpoint: done\n");
}



// ---------------------------------------------------------------------------
// KK::PerClusterCEMPerChunk
//
// Phase 2a: per-cluster ordinary CEM in the FULL feature space.  For each
// cluster in each chunk, builds a thread-local scratch KK populated with
// only that cluster's spikes, warm-starts from a single Gaussian (all
// spikes assigned to cluster 1, cluster 0 = noise but unused), and runs
// RunEMLoop with splits enabled.  TrySplits inside the loop probes for
// bimodal substructure using the bimodality-coefficient feature selection
// from P1.B; the K3 outer recheck (P1.A) settles each candidate split for
// 3 EM iterations before scoring.
//
// Replaces the former subspace-projected SubspaceReclusterPerChunk.  The
// projection-then-CEM design had a structural failure mode: an elongated
// unimodal Gaussian, when projected into its own top-variance eigenvectors,
// looks bimodal-along-the-long-axis to a 2-Gaussian fit, producing high
// false-positive split rates (~67% acceptance was observed on octrode data).
// Running ordinary CEM in the full feature space avoids the projection
// bias entirely and lets the standard BIC-gated split machinery decide.
//
// Cluster ID assignment:
//   - sub-cluster 0 (noise): NoisePoint=0 keeps it empty (user spec: no real
//     noise from extraction)
//   - sub-cluster 1: keeps the parent's local ID
//   - sub-clusters 2,3,...: get fresh chunk-local IDs starting at maxLc+1
//
// ChunkModel rebuild is deferred to ChunkReCEMPerChunk, which re-runs a
// chunk-level CEM that lets fine-grained sub-clusters reorganise across
// the chunk's full data (boundary spikes can reassign, oversplit pieces
// can merge via ConsiderDeletion).  Building stale ChunkModels here only
// to discard them in 2b would be wasted work.
// ---------------------------------------------------------------------------
void KK::PerClusterCEMPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& /*perChunkModels*/,
    int nFullDims, int onlyChunk)
{
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    // Minimum cluster size for full-dim covariance.  Below this, the
    // per-cluster CEM would build a singular covariance and degenerate.
    // nFullDims+5 (a few spikes' margin above the rank requirement) is a
    // sane floor; raise to 25 for stability with small clusters.
    const int minClusterSize = std::max(nFullDims + 5, 25);

    // ── Load-balancing knobs ─────────────────────────────────────────
    // Phase 2a is bottlenecked by the largest 2–3 clusters per chunk,
    // each of which serializes a single thread for minutes while 100+
    // other threads sit idle.  Mitigation: when a cluster exceeds
    // kLargeThreshold spikes, split its members randomly into batches
    // of ~kTargetBatch spikes each and dispatch each batch as a
    // separate work item.  This trades a small amount of redundant work
    // (each batch re-discovers any common sub-modes) for genuine
    // parallelism — Phase 2b's chunk re-CEM with ConsiderDeletion
    // re-merges any over-splits across batches.
    //
    // After items are built we LPT-sort (longest processing time first)
    // so the giant items start while the worker pool is full; small
    // ones backfill as threads free up.
    const int kPhase2aLargeThreshold = 800;   // subdivide if size > this
    const int kPhase2aTargetBatch    = 400;   // target spikes per batch

    // ── Phase A: build (chunk, cluster) work items ───────────────────
    struct WorkItem {
        int ck;
        int lc;                       // cluster's local id within the chunk
                                      // (-1 marks "subdivided" — sub-labels
                                      // all get fresh chunk-local IDs at
                                      // write-back; original cluster's ID
                                      // is replaced by the union of all
                                      // batches' sub-clusters)
        std::vector<int> members;     // indices into chunkPoints[ck]
    };
    std::vector<WorkItem> items;

    const int _ckLo = (onlyChunk >= 0) ? onlyChunk : 0;
    const int _ckHi = (onlyChunk >= 0) ? onlyChunk + 1 : nCh;
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        const auto& cls = perChunkClass[ck];
        const auto& pts = chunkPoints[ck];
        if (pts.empty()) continue;
        if (cls.size() != pts.size()) continue;

        // Find unique non-noise local cluster IDs in this chunk.
        std::unordered_set<int> uniqueLcs;
        for (int c : cls) if (c >= 1) uniqueLcs.insert(c);

        for (int lc : uniqueLcs) {
            std::vector<int> mem;
            for (int i = 0; i < static_cast<int>(pts.size()); i++)
                if (cls[static_cast<size_t>(i)] == lc) mem.push_back(i);
            if (static_cast<int>(mem.size()) < minClusterSize) continue;

            if (static_cast<int>(mem.size()) > kPhase2aLargeThreshold) {
                // Subdivide into ceil(N / kPhase2aTargetBatch) random
                // batches of approximately equal size.  Use a per-cluster
                // RNG seed (RandomSeed XOR'd with chunk/cluster keys) so
                // shuffles are reproducible.
                std::mt19937 rng(static_cast<unsigned>(RandomSeed)
                               ^ static_cast<unsigned>(ck * 7919 + lc * 31));
                std::shuffle(mem.begin(), mem.end(), rng);

                const int N      = static_cast<int>(mem.size());
                const int nBatch = (N + kPhase2aTargetBatch - 1)
                                 / kPhase2aTargetBatch;
                const int base   = N / nBatch;
                const int extra  = N - base * nBatch;

                int offset = 0;
                for (int b = 0; b < nBatch; b++) {
                    const int sz = base + (b < extra ? 1 : 0);
                    if (sz < minClusterSize) {
                        // Edge case: tiny final batch — fold remainder
                        // into the previous batch instead of dropping.
                        if (!items.empty() && items.back().lc == -1
                            && items.back().ck == ck) {
                            auto& prev = items.back().members;
                            prev.insert(prev.end(),
                                        mem.begin() + offset,
                                        mem.begin() + offset + sz);
                        }
                        offset += sz;
                        continue;
                    }
                    std::vector<int> sub(mem.begin() + offset,
                                         mem.begin() + offset + sz);
                    offset += sz;
                    items.push_back({ck, /*lc=*/-1, std::move(sub)});
                }
            } else {
                items.push_back({ck, lc, std::move(mem)});
            }
        }
    }

    // LPT scheduling: largest items first.  With schedule(dynamic) this
    // ensures the long-running giant clusters (or their batches) start
    // while the thread pool is full, and small items backfill at the
    // tail.  Avoids the wall-time "stragglers" pattern where a few
    // late-starting giants leave 100+ threads idle for minutes.
    std::sort(items.begin(), items.end(),
              [](const WorkItem& a, const WorkItem& b) {
                  return a.members.size() > b.members.size();
              });

    // Quick stats for diagnostic logging.
    int nSubdivided = 0;
    size_t maxSize = 0;
    for (const auto& it : items) {
        if (it.lc < 0) nSubdivided++;
        if (it.members.size() > maxSize) maxSize = it.members.size();
    }

    if (onlyChunk < 0) LockedStderr(
            "[Stage 2.4] Per-cluster CEM: probing %d items (min size %d, "
            "max %zu, %d subdivided batches)\n",
            static_cast<int>(items.size()), minClusterSize,
            maxSize, nSubdivided);

    if (items.empty()) return;

    // ── Phase B: parallel per-cluster CEM ────────────────────────────
    struct ItemResult {
        bool changed = false;
        int  nSubClusters = 0;
        std::vector<int> newSubLabels;  // per-member sub-cluster id from CEM
    };
    std::vector<ItemResult> results(items.size());

    int totalSplits        = 0;
    int totalNewSubClusters = 0;

    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) \
        reduction(+:totalSplits,totalNewSubClusters)
    for (int wi = 0; wi < static_cast<int>(items.size()); wi++) {
        const auto& item = items[static_cast<size_t>(wi)];
        const int   nMem = static_cast<int>(item.members.size());
        const auto& pts  = chunkPoints[item.ck];

        // Build sub-KK with only this cluster's spikes.
        //
        // SPATIAL-ONLY: time (last dim) is excluded.  Reasoning: this is
        // the same recluster that Klusters performs when the user "splits"
        // a cluster — Klusters spawns KiloKlustaKwik on the cluster's
        // spikes, which runs CEMTwoPhase whose Phase 1 is spatial-only
        // (excludes time).  TrySplits on full-dim data was rejecting
        // bimodal pairs whose split improvement was below the BIC penalty
        // because the time dim's parameter cost was being counted in the
        // penalty without contributing meaningfully to the spatial
        // discrimination (per-cluster time variance ≈ chunk duration is
        // large, so time's Mahalanobis contribution is weak).  Stripping
        // time at copy-in time gives us correct stride throughout (no
        // reliance on CEMTwoPhase's nDims-mutation trick) and matches
        // the standalone Klusters recluster path.
        const int nSpatialDimsFull = (nFullDims > 1) ? nFullDims - 1 : nFullDims;

        // ── Per-cluster feature selection ─────────────────────────
        // When SubspaceDims > 0 and < nSpatialDimsFull, rank the
        // spatial features by within-cluster variance over this item's
        // members and pick the top SubspaceDims.  CEM then runs in a
        // K-dim subspace.
        //
        // Why: with K=21 spatial dims and ~1000 spikes per cluster,
        // each Gaussian costs 21+231=252 BIC parameters → log(N)·252/2
        // ≈ 880 BIC units per cluster, which rejects every marginal
        // split.  At K=6, the cost drops to 6+21=27 params → ~95 BIC
        // units, comparable to what classic KlustaKwik with
        // -UseFeatures (auto-4) operates on.  Marginal real bimodal
        // pairs that get rejected at 21 dims pass cleanly at 6.
        //
        // Selection is per-item (each cluster picks its own best
        // features) — a cluster contaminated mainly along ch3-PC2
        // and ch5-PC1 will have those features ranked high; another
        // cluster with different sub-structure picks different ones.
        // Each picked feature stays interpretable (it's still
        // "PC_i of channel_j" in the original .fetD layout).
        int  nSubDims;                          // active dim count for CEM
        std::vector<int> selFeat;               // indices into spatial dims [0..nSpatialDimsFull)

        if (SubspaceDims > 0 && SubspaceDims < nSpatialDimsFull) {
            // Compute per-feature mean and variance on this cluster's spikes.
            std::vector<double> sum(nSpatialDimsFull, 0.0);
            std::vector<double> sqsum(nSpatialDimsFull, 0.0);
            for (int i = 0; i < nMem; i++) {
                const int p = pts[static_cast<size_t>(item.members[static_cast<size_t>(i)])];
                for (int d = 0; d < nSpatialDimsFull; d++) {
                    const double v = Data[static_cast<size_t>(p) * nFullDims + d];
                    sum[d]   += v;
                    sqsum[d] += v * v;
                }
            }
            std::vector<std::pair<double,int>> rank(nSpatialDimsFull);
            const double invN = 1.0 / nMem;
            for (int d = 0; d < nSpatialDimsFull; d++) {
                const double m  = sum[d] * invN;
                const double v  = std::max(0.0, sqsum[d] * invN - m * m);
                rank[d] = {v, d};
            }
            std::sort(rank.begin(), rank.end(),
                      [](const auto& a, const auto& b){ return a.first > b.first; });

            nSubDims = SubspaceDims;
            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int k = 0; k < nSubDims; k++)
                selFeat[static_cast<size_t>(k)] = rank[static_cast<size_t>(k)].second;
            // Sort selected indices ascending so the packed columns
            // remain in original feature order — purely cosmetic, but
            // makes any debug printout interpretable.
            std::sort(selFeat.begin(), selFeat.end());
        } else {
            // No subspace: use all spatial features in original order.
            nSubDims = nSpatialDimsFull;
            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int d = 0; d < nSubDims; d++)
                selFeat[static_cast<size_t>(d)] = d;
        }

        KK Ks;
        Ks.nDims              = nSubDims;
        Ks.nPoints            = nMem;
        Ks.penaltyMix         = penaltyMix;
        Ks.suppressBestSave   = true;
        Ks.minClustersAlive   = 1;
        Ks.NoisePoint         = 0;

        Ks.AllocateArrays();
        Ks.AllocateCholeskyVecs();
        Ks.ReinitForSplit(nMem, nSubDims, penaltyMix);

        // Pack data using selected features only.  Stride = nSubDims.
        for (int i = 0; i < nMem; i++) {
            const int p = pts[static_cast<size_t>(item.members[static_cast<size_t>(i)])];
            for (int k = 0; k < nSubDims; k++) {
                const int d = selFeat[static_cast<size_t>(k)];
                Ks.Data[static_cast<size_t>(i) * nSubDims + k] =
                    Data[static_cast<size_t>(p) * nFullDims + d];
            }
        }
        Ks.timeRawMin = timeRawMin;
        Ks.timeRawMax = timeRawMax;

        // Init mode depends on whether this item was subdivided:
        //
        //  • Non-subdivided (item.lc ≥ 0, original small cluster): warm
        //    start at K=2 with all spikes in cluster 1.  Let TrySplits
        //    drive K growth for full bimodality discovery.
        //
        //  • Subdivided (item.lc == -1, batch from a large cluster):
        //    flat-K random init at K = ceil(N / kInitSize).  Disable
        //    TrySplits and cap MaxIter so each batch finishes in
        //    bounded time.  Phase 2b's chunk re-CEM (with TrySplits +
        //    ConsiderDeletion at the chunk level) is responsible for
        //    discovering structure across the union of all batches.
        //    Without this cap, a noisy batch can land in a TrySplits-
        //    ConsiderDeletion oscillation and serialize a single
        //    thread for many minutes — the original straggler problem
        //    just relocated to one batch instead of one cluster.
        bool   enableSplits = true;
        int    capMaxIter   = 0;        // 0 = no override
        if (item.lc < 0) {
            // Subdivided batch.
            const int kInitSize = 100;  // target spikes per starting cluster
            const int nReal     = std::max(2, (nMem + kInitSize - 1) / kInitSize);
            Ks.nStartingClusters = nReal + 1;
            for (int c = 0; c < MaxPossibleClusters; c++)
                Ks.ClassAlive[c] = (c <= nReal) ? 1 : 0;
            for (int i = 0; i < nMem; i++)
                Ks.Class[i] = irand(1, nReal);
            enableSplits = false;
            capMaxIter   = 50;          // hard cap; flat-K converges fast
        } else {
            // Non-subdivided cluster: warm-start at K=2.
            Ks.nStartingClusters = 2;
            for (int c = 0; c < MaxPossibleClusters; c++)
                Ks.ClassAlive[c] = (c < 2) ? 1 : 0;
            for (int i = 0; i < nMem; i++) Ks.Class[i] = 1;
        }
        Ks.Reindex();

        Ks.MStep();
        Ks.EStep();

        Ks.RunEMLoop(/*enableSplits=*/   enableSplits,
                     /*enableDistDump=*/ false,
                     /*maxIter=*/        capMaxIter,
                     /*phaseLabel=*/     (item.lc < 0) ? "[2a-batch]" : "[2a]");

        // nClustersAlive includes noise (0).  No-split: == 2 (noise + 1).
        // Real split: > 2.
        if (Ks.nClustersAlive <= 2) continue;

        results[static_cast<size_t>(wi)].changed       = true;
        results[static_cast<size_t>(wi)].nSubClusters  = Ks.nClustersAlive - 1;
        results[static_cast<size_t>(wi)].newSubLabels.resize(static_cast<size_t>(nMem));
        for (int i = 0; i < nMem; i++)
            results[static_cast<size_t>(wi)].newSubLabels[static_cast<size_t>(i)] = Ks.Class[i];

        totalSplits         += 1;
        totalNewSubClusters += (Ks.nClustersAlive - 2);  // beyond the original 1
    }

    // ── Phase C: serial application — assign fresh local IDs ─────────
    std::vector<int> nextLc(nCh, 1);
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        int maxLc = 0;
        for (int c : perChunkClass[static_cast<size_t>(ck)])
            if (c > maxLc) maxLc = c;
        nextLc[static_cast<size_t>(ck)] = maxLc + 1;
    }

    std::unordered_set<int> chunksAffected;
    for (int wi = 0; wi < static_cast<int>(items.size()); wi++) {
        const auto& res = results[static_cast<size_t>(wi)];
        if (!res.changed) continue;
        const auto& item = items[static_cast<size_t>(wi)];
        auto& cls = perChunkClass[static_cast<size_t>(item.ck)];

        // Map sub-cluster ID to chunk-local ID:
        //   0 -> 0 (noise; should be empty under NoisePoint=0)
        //   For non-subdivided items (item.lc >= 0):
        //     1 -> item.lc (parent retains its ID)
        //     ≥2 -> fresh chunk-local ID
        //   For subdivided items (item.lc == -1):
        //     ALL sub-labels (including 1) -> fresh chunk-local IDs.
        //     The original cluster's ID is replaced entirely by the
        //     union of all batches' sub-clusters; Phase 2b's chunk
        //     re-CEM is responsible for re-merging coherent ones.
        std::unordered_map<int,int> subToLc;
        subToLc[0] = 0;
        if (item.lc >= 0) subToLc[1] = item.lc;

        const int nMem = static_cast<int>(item.members.size());
        for (int i = 0; i < nMem; i++) {
            const int sc = res.newSubLabels[static_cast<size_t>(i)];
            auto it = subToLc.find(sc);
            if (it == subToLc.end()) {
                subToLc[sc] = nextLc[static_cast<size_t>(item.ck)]++;
                it = subToLc.find(sc);
            }
            cls[static_cast<size_t>(item.members[static_cast<size_t>(i)])] = it->second;
        }
        chunksAffected.insert(item.ck);
    }

    LockedStderr(
            "[Stage 2.4] Per-cluster CEM: %d clusters split, +%d new sub-clusters, "
            "%d chunks affected\n",
            totalSplits, totalNewSubClusters,
            static_cast<int>(chunksAffected.size()));
}



// ─────────────────────────────────────────────────────────────────────────
// KK::RunPhase8VarianceSplit — Phase 8 variance-targeted knn-split (patch 0067)
// ─────────────────────────────────────────────────────────────────────────
// Iterates Phase 4b's WaveKnn-split machinery on the high-variance clusters
// only.  Per iteration: for each chunk, compute ρ per cluster; clusters with
// ρ ≥ Phase8VarianceThreshold (and size ≥ minSize) form the WaveKnn-split
// allowlist for this iteration.  Then merge + count changes + quit when
// stable (or maxIters reached).
//
// Design choice: FullCEM is intentionally NOT called here.  The target
// scenario is diffuse clusters with no obvious Gaussian-mixture modes — the
// kind that show up as a smear in feature space without internal structure.
// FullCEM would either no-op (no modes for the mixture to find) or invent
// spurious modes; both are wasteful.  WaveKnn-split is the right tool: it
// redistributes the source cluster's spikes to nearby reference clusters
// by k-NN voting, with anything that doesn't vote strongly staying as a
// smaller residual.  Iterated, the diffuse cluster shrinks and concentrates
// (or fully dissolves into its neighbors).
//
// Hooks in after Phase 4c at both CEM call sites; no-op unless
// -Phase8VarianceSplitEnable 1.  Requires m_timeShiftReady (needs to read
// waveforms for the tightness measurement); without it the phase is
// skipped with a warning.
//
// CLI: -Phase8VarianceSplitEnable        0   (master switch)
//      -Phase8VarianceSplitMaxIters      3
//      -Phase8VarianceThreshold          0.10  (ρ threshold for eligibility)
//      -Phase8VarianceSignalChannelFraction 0.1 (τ; same as Phase 4c)
//      -Phase8VarianceMinClusterSize     0   (0 = max(nFullDims+5, 25))
void KK::RunPhase8VarianceSplit(
    const std::vector<std::vector<int>>&  chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    if (Phase8VarianceSplitEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    const int nChan  = NbChannels;
    const int nSamp  = NbSamplesPerSpike;
    const int wElems = nChan * nSamp;
    if (nChan <= 0 || nSamp <= 0) {
        LockedStderr("[Stage 2.13] skipped: missing spike geometry\n");
        return;
    }
    if (!m_timeShiftReady) {
        LockedStderr("[Stage 2.13] skipped: time-shift machinery not ready "
                     "(need MaxTimeShift > 0 for spike-read access)\n");
        return;
    }

    const int    maxIters = std::max(1, Phase8VarianceSplitMaxIters);
    const double thr      = std::max(0.0, static_cast<double>(Phase8VarianceThreshold));
    const double tau      = static_cast<double>(Phase8VarianceSignalChannelFraction);
    const int    minSize  = (Phase8VarianceMinClusterSize > 0)
                          ? Phase8VarianceMinClusterSize
                          : std::max(nFullDims + 5, 25);

    LockedStderr("[Stage 2.13] Variance-targeted knn-split: up to %d iters, "
                 "ρ_thresh=%.3f, min size=%d (FullCEM skipped — diffuse "
                 "clusters have no modes)\n",
                 maxIters, thr, minSize);

    int grandRelabeled       = 0;
    int grandSourcesProcessed = 0;

    for (int it = 0; it < maxIters; ++it) {
        // Per-chunk allowlists, written from disjoint slots under the
        // parallel for; gathered into the std::map after the parallel
        // section (std::map insert is not thread-safe).
        std::vector<std::vector<int>> highVarPerChunk(static_cast<size_t>(nCh));
        int totalHighVar = 0;

        #pragma omp parallel for schedule(dynamic) reduction(+:totalHighVar)
        for (int ck = 0; ck < nCh; ++ck) {
            const auto& pts  = chunkPoints[static_cast<size_t>(ck)];
            const auto& cls  = perChunkClass[static_cast<size_t>(ck)];
            const auto& mdls = perChunkModels[static_cast<size_t>(ck)];
            const int   nPts = static_cast<int>(pts.size());
            if (nPts == 0 || mdls.empty()) continue;

            std::vector<int16_t> rbuf(static_cast<size_t>(wElems));

            // Members per cluster.
            std::unordered_map<int, std::vector<int>> memLocal;
            for (int i = 0; i < nPts; ++i) {
                const int c = cls[static_cast<size_t>(i)];
                if (c != 0) memLocal[c].push_back(i);
            }

            for (const auto& kv : memLocal) {
                const int lc = kv.first;
                const auto& idx = kv.second;
                if (static_cast<int>(idx.size()) < minSize) continue;
                std::vector<int> gids;
                gids.reserve(idx.size());
                for (int li : idx) gids.push_back(pts[static_cast<size_t>(li)]);
                int nSig = 0;
                const double rho = computeClusterTightnessRho(
                    gids, rbuf, nChan, nSamp, tau, nSig);
                if (std::isfinite(rho) && rho >= thr) {
                    highVarPerChunk[static_cast<size_t>(ck)].push_back(lc);
                    ++totalHighVar;
                }
            }
        }

        if (totalHighVar == 0) {
            LockedStderr("[Stage 2.13] iter %d: no clusters above ρ_thresh; "
                         "converged\n", it + 1);
            break;
        }

        // Gather scratch → final allowlist map.
        std::map<int, std::vector<int>> knnAllow;
        for (int ck = 0; ck < nCh; ++ck) {
            if (!highVarPerChunk[static_cast<size_t>(ck)].empty())
                knnAllow[ck] = std::move(highVarPerChunk[static_cast<size_t>(ck)]);
        }

        // Snapshot perChunkClass to count spikes redistributed.
        std::vector<std::vector<int>> before = perChunkClass;

        // ── knn-split only (FullCEM intentionally skipped) ───────────────
        WaveKnnSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                             nFullDims, &knnAllow);

        // Merge cleanup — same Phase 4 within-chunk template merge.
        const int merged = MedianKnnTemplateMatchEnable
            ? WithinChunkTemplateMatchMedianKnn(chunkPoints, perChunkClass,
                                                perChunkModels, nChan, nSamp,
                                                TemplateMatchScore)
            : WithinChunkTemplateMatch(chunkPoints, perChunkClass,
                                       perChunkModels, nChan, nSamp,
                                       TemplateMatchScore);

        int changedThisIter = 0;
        for (size_t k = 0; k < perChunkClass.size(); ++k) {
            const auto& a = perChunkClass[k];
            const auto& b = before[k];
            if (a.size() != b.size()) continue;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) ++changedThisIter;
        }
        grandRelabeled        += changedThisIter;
        grandSourcesProcessed += totalHighVar;

        LockedStderr("[Stage 2.13] iter %d/%d: %d high-variance sources, "
                     "%d spikes redistributed, %d merge(s)\n",
                     it + 1, maxIters, totalHighVar,
                     changedThisIter, merged);

        if (changedThisIter == 0 && merged == 0) {
            LockedStderr("[Stage 2.13] iter %d: no further changes; converged\n",
                         it + 1);
            break;
        }
    }

    LockedStderr("[Stage 2.13] done: %d source-iterations, %d spikes redistributed\n",
                 grandSourcesProcessed, grandRelabeled);
}



// ─────────────────────────────────────────────────────────────────────────
// KK::RunPhase4cRemix — Phase 4c neighborhood-remix split (patch 0062)
// ─────────────────────────────────────────────────────────────────────────
// Runs once after the Phase 4 loop converges, gated by -Phase4cRemixEnable.
// Structurally a small split→harvest→merge loop like Phase 4b, but the source
// assembly is different: random source clusters are each POOLED with their N
// closest clusters (transient merge), and the splitter re-partitions the
// pooled super-cluster.  Intent: de-fragmentation — give the splitter a second
// look at a cluster together with its nearest neighbours so over-split pieces
// get redrawn or reabsorbed.  knn-split and FullCEM get separate source counts.
//
// Guards:
//   * Disjointness — across the selected work items, if any two pools share a
//     cluster (overlapping spikes) a whole source is dropped at random until
//     all surviving pools are pairwise spike-disjoint, so the parallel
//     dispatch is race-free.
//   * Tightness mask — clusters whose post-alignment residual dispersion is
//     low relative to their own signal power (a scale-free inverse-SNR², so a
//     single threshold is comparable across high-amplitude/concentrated and
//     low-amplitude/diffuse units) are excluded entirely: never a source,
//     never pooled into a neighbourhood.  Protects already-clean units.
//
// CLI: -Phase4cRemixEnable -Phase4cMaxIters -Phase4cKnnSources
//      -Phase4cFullCemSources -Phase4cNeighbors -Phase4cMinClusterSize
//      -Phase4cMaskTightClusters -Phase4cTightnessThreshold
//      -Phase4cSignalChannelFraction -Phase4cTightnessSpreadBeta
void KK::RunPhase4cRemix(
    const std::vector<std::vector<int>>&  chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    if (Phase4cRemixEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    const int nChan     = NbChannels;
    const int nSamp     = NbSamplesPerSpike;
    const int wElems    = nChan * nSamp;
    if (nChan <= 0 || nSamp <= 0) {
        LockedStderr("[Stage 2.12] skipped: missing spike geometry\n");
        return;
    }
    const int minSize = (Phase4cMinClusterSize > 0)
                      ? Phase4cMinClusterSize
                      : std::max(nFullDims + 5, 25);
    const int nNeigh  = std::max(0, Phase4cNeighbors);
    const int maxIters = std::max(1, Phase4cMaxIters);
    const bool doMask = (Phase4cMaskTightClusters != 0) && m_timeShiftReady;
    const double tau  = std::max(0.0, static_cast<double>(Phase4cSignalChannelFraction));

    LockedStderr("[Stage 2.12] Neighborhood-remix split: up to %d iters, "
                 "sources knn=%d cem=%d, N=%d neighbours%s\n",
                 maxIters, Phase4cKnnSources, Phase4cFullCemSources, nNeigh,
                 doMask ? ", tight-cluster mask ON" : "");

    // Residual dispersion ρ = V_res / P_sig on the signal-support channels for
    // one cluster's global spike ids.  Scale-free (≈1/SNR²): amplitude cancels
    // (both terms ∝ k²) and per-element averaging on the signal support makes
    // concentrated and diffuse footprints comparable.  Returns +inf if it
    // cannot be measured (so such a cluster is never masked).
    auto clusterTightness = [&](const std::vector<int>& gids,
                                std::vector<int16_t>& rbufLocal)
        -> std::pair<double,int>
    {
        const int N = static_cast<int>(gids.size());
        if (N < 2) return { std::numeric_limits<double>::infinity(), 1 };
        std::vector<double> mean(static_cast<size_t>(wElems), 0.0);
        int ok = 0;
        std::vector<std::vector<int16_t>> cache;
        cache.reserve(static_cast<size_t>(N));
        for (int i = 0; i < N; ++i) {
            if (!TimeShiftReadSpikeWave(gids[static_cast<size_t>(i)],
                                        wElems, rbufLocal.data())) continue;
            cache.push_back(rbufLocal);
            for (int e = 0; e < wElems; ++e)
                mean[static_cast<size_t>(e)] +=
                    static_cast<double>(rbufLocal[static_cast<size_t>(e)]);
            ++ok;
        }
        if (ok < 2) return { std::numeric_limits<double>::infinity(), 1 };
        const double invOk = 1.0 / ok;
        for (int e = 0; e < wElems; ++e) mean[static_cast<size_t>(e)] *= invOk;

        // Per-channel energy → signal support (channel-major: ch*nSamp+samp).
        double maxE = 0.0;
        std::vector<double> Ech(static_cast<size_t>(nChan), 0.0);
        for (int ch = 0; ch < nChan; ++ch) {
            double e2 = 0.0;
            for (int s = 0; s < nSamp; ++s) {
                const double m = mean[static_cast<size_t>(ch) * nSamp + s];
                e2 += m * m;
            }
            Ech[static_cast<size_t>(ch)] = e2;
            if (e2 > maxE) maxE = e2;
        }
        if (maxE <= 0.0) return { std::numeric_limits<double>::infinity(), 1 };

        double Psig = 0.0, Vres = 0.0;
        long   nSig = 0;
        long long nResElem = 0;
        for (int ch = 0; ch < nChan; ++ch) {
            if (Ech[static_cast<size_t>(ch)] < tau * maxE) continue;
            ++nSig;
            for (int s = 0; s < nSamp; ++s) {
                const int e = ch * nSamp + s;
                const double m = mean[static_cast<size_t>(e)];
                Psig += m * m;
            }
        }
        if (nSig == 0) return { std::numeric_limits<double>::infinity(), 1 };
        const long sigElems = nSig * nSamp;
        Psig /= static_cast<double>(sigElems);

        for (const auto& w : cache)
            for (int ch = 0; ch < nChan; ++ch) {
                if (Ech[static_cast<size_t>(ch)] < tau * maxE) continue;
                for (int s = 0; s < nSamp; ++s) {
                    const int e = ch * nSamp + s;
                    const double d = static_cast<double>(w[static_cast<size_t>(e)])
                                   - mean[static_cast<size_t>(e)];
                    Vres += d * d;
                    ++nResElem;
                }
            }
        if (nResElem == 0 || Psig <= 0.0)
            return { std::numeric_limits<double>::infinity(), 1 };
        Vres /= static_cast<double>(nResElem);
        // Effective threshold scaling lives at the call site (uses nSig).
        return { Vres / Psig, static_cast<int>(nSig) };
    };

    int grandChanged = 0;
    for (int it = 0; it < maxIters; ++it) {
        int changedThisIter = 0;

        // Per-route allowlists, assembled per-chunk under the parallel for
        // (disjoint slots — no concurrent map insert).  Gathered into the
        // final std::map serially after the loop.
        std::vector<std::vector<int>> knnForChunk(static_cast<size_t>(nCh));
        std::vector<std::vector<int>> cemForChunk(static_cast<size_t>(nCh));

        // ── Per-chunk parallelism (patch 0064) ──────────────────────────
        // Each iteration mutates only perChunkClass[ck] / perChunkModels[ck]
        // (disjoint slots) and writes its own knnForChunk[ck]/cemForChunk[ck]
        // entry.  Hazards moved per-thread: rbuf (read-spike scratch),
        // chunkRng (deterministic per-(ck,it,seed)).  The outer `it` loop is
        // intentionally serial — each iter depends on the previous iter's
        // post-split/merge state.
        #pragma omp parallel for schedule(dynamic)
        for (int ck = 0; ck < nCh; ++ck) {
            // Per-thread scratch (lives in this iteration's stack frame).
            std::vector<int16_t> rbuf(static_cast<size_t>(wElems));
            std::mt19937 chunkRng((RandomSeed != 0
                ? static_cast<unsigned>(RandomSeed) ^ 0x4c4c4c4cu
                : std::random_device{}())
                ^ (static_cast<unsigned>(ck) * 0x9E3779B9u)
                ^ (static_cast<unsigned>(it) << 16));

            const auto& pts = chunkPoints[static_cast<size_t>(ck)];
            auto&       cls = perChunkClass[static_cast<size_t>(ck)];
            auto&       mdls = perChunkModels[static_cast<size_t>(ck)];
            const int   nPts = static_cast<int>(pts.size());
            if (nPts == 0 || mdls.empty()) continue;

            // Members per local cluster.
            std::unordered_map<int, std::vector<int>> memLocal;  // lc -> local idx
            for (int i = 0; i < nPts; ++i) {
                const int c = cls[static_cast<size_t>(i)];
                if (c != 0) memLocal[c].push_back(i);
            }

            // Eligible clusters: size ≥ minSize, not tight-masked.
            std::vector<int> eligible;
            for (const auto& [lc, idx] : memLocal) {
                if (static_cast<int>(idx.size()) < minSize) continue;
                if (doMask) {
                    std::vector<int> gids; gids.reserve(idx.size());
                    for (int li : idx) gids.push_back(pts[static_cast<size_t>(li)]);
                    const auto [rho, nSig] = clusterTightness(gids, rbuf);
                    const double thr = static_cast<double>(Phase4cTightnessThreshold)
                        * std::pow(std::max(1, nSig),
                                   static_cast<double>(Phase4cTightnessSpreadBeta));
                    if (rho < thr) continue;   // tight → masked out
                }
                eligible.push_back(lc);
            }
            if (static_cast<int>(eligible.size()) < 2) continue;

            // meanWav L2 between eligible clusters → each source's N closest.
            auto modelOf = [&](int lc) -> ChunkModel* {
                for (auto& cm : mdls)
                    if (cm.chunkIdx == ck && cm.localClusterId == lc) return &cm;
                return nullptr;
            };
            auto tmplL2 = [&](int a, int b) -> double {
                ChunkModel* ma = modelOf(a); ChunkModel* mb = modelOf(b);
                if (!ma || !mb) return std::numeric_limits<double>::infinity();
                const auto& wa = ma->meanWav; const auto& wb = mb->meanWav;
                if (wa.empty() || wb.empty() || wa.size() != wb.size())
                    return std::numeric_limits<double>::infinity();
                double s = 0.0;
                for (size_t e = 0; e < wa.size(); ++e) {
                    const double d = static_cast<double>(wa[e])
                                   - static_cast<double>(wb[e]);
                    s += d * d;
                }
                return s;
            };

            // Random source order.
            std::vector<int> order = eligible;
            std::shuffle(order.begin(), order.end(), chunkRng);

            const int wantKnn = std::max(0, Phase4cKnnSources);
            const int wantCem = std::max(0, Phase4cFullCemSources);
            const int wantTot = wantKnn + wantCem;
            if (wantTot == 0) continue;

            // Build candidate pools: source + its N closest eligible clusters.
            struct Pool { int src; int route; std::vector<int> members; };  // route 0=knn 1=cem
            std::vector<Pool> pools;
            int kCount = 0, cCount = 0;
            for (int src : order) {
                if (kCount >= wantKnn && cCount >= wantCem) break;
                // N closest eligible neighbours by template L2.
                std::vector<std::pair<double,int>> rank;
                for (int other : eligible)
                    if (other != src) rank.push_back({ tmplL2(src, other), other });
                std::sort(rank.begin(), rank.end());
                std::vector<int> nb;  nb.push_back(src);
                for (int n = 0; n < nNeigh && n < static_cast<int>(rank.size()); ++n)
                    nb.push_back(rank[static_cast<size_t>(n)].second);

                int route;
                if (kCount < wantKnn) { route = 0; ++kCount; }
                else                  { route = 1; ++cCount; }
                pools.push_back({ src, route, std::move(nb) });
            }

            // ── Disjointness guard ──────────────────────────────────────
            // Drop whole sources at random until no two surviving pools share
            // a cluster (→ no overlapping spikes).
            {
                bool conflict = true;
                while (conflict && !pools.empty()) {
                    conflict = false;
                    // cluster -> list of pool indices claiming it
                    std::unordered_map<int, std::vector<int>> claim;
                    for (size_t pi = 0; pi < pools.size(); ++pi)
                        for (int lc : pools[pi].members)
                            claim[lc].push_back(static_cast<int>(pi));
                    std::set<int> conflicting;
                    for (auto& [lc, owners] : claim)
                        if (owners.size() > 1) {
                            conflict = true;
                            for (int pi : owners) conflicting.insert(pi);
                        }
                    if (conflict) {
                        std::vector<int> cv(conflicting.begin(), conflicting.end());
                        std::uniform_int_distribution<size_t> pick(0, cv.size() - 1);
                        const int drop = cv[pick(chunkRng)];
                        pools.erase(pools.begin() + drop);
                    }
                }
            }
            if (pools.empty()) continue;

            // ── Transient relabel: pool neighbours → source; refresh source
            //    model (mean/cov/meanWav) and drop absorbed-neighbour models.
            for (const auto& P : pools) {
                std::set<int> absorbed(P.members.begin(), P.members.end());
                absorbed.erase(P.src);
                if (!absorbed.empty()) {
                    for (int i = 0; i < nPts; ++i)
                        if (absorbed.count(cls[static_cast<size_t>(i)]))
                            cls[static_cast<size_t>(i)] = P.src;
                }

                // Recompute source mean/cov directly from its (enlarged) members.
                std::vector<int> mem;
                for (int i = 0; i < nPts; ++i)
                    if (cls[static_cast<size_t>(i)] == P.src) mem.push_back(i);
                const int M = static_cast<int>(mem.size());
                ChunkModel* sm = modelOf(P.src);
                if (sm && M > 0) {
                    std::vector<double> mu(static_cast<size_t>(nFullDims), 0.0);
                    for (int li : mem)
                        for (int d = 0; d < nFullDims; ++d)
                            mu[static_cast<size_t>(d)] +=
                                Data[static_cast<size_t>(pts[static_cast<size_t>(li)])
                                     * nFullDims + d];
                    const double invM = 1.0 / M;
                    for (int d = 0; d < nFullDims; ++d) mu[static_cast<size_t>(d)] *= invM;
                    sm->mean.assign(static_cast<size_t>(nFullDims), 0.0f);
                    for (int d = 0; d < nFullDims; ++d)
                        sm->mean[static_cast<size_t>(d)] =
                            static_cast<float>(mu[static_cast<size_t>(d)]);
                    sm->cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
                    for (int li : mem) {
                        const int p = pts[static_cast<size_t>(li)];
                        for (int r = 0; r < nFullDims; ++r) {
                            const double dr = Data[static_cast<size_t>(p) * nFullDims + r]
                                            - mu[static_cast<size_t>(r)];
                            for (int col = r; col < nFullDims; ++col) {
                                const double dc =
                                    Data[static_cast<size_t>(p) * nFullDims + col]
                                    - mu[static_cast<size_t>(col)];
                                sm->cov[static_cast<size_t>(r) * nFullDims + col] +=
                                    static_cast<float>(dr * dc * invM);
                            }
                        }
                    }
                    sm->nMembers = M;
                    // Refresh meanWav from current alignment.
                    if (m_timeShiftReady) {
                        std::vector<double> mw(static_cast<size_t>(wElems), 0.0);
                        int okw = 0;
                        for (int li : mem) {
                            if (!TimeShiftReadSpikeWave(pts[static_cast<size_t>(li)],
                                                        wElems, rbuf.data())) continue;
                            for (int e = 0; e < wElems; ++e)
                                mw[static_cast<size_t>(e)] +=
                                    static_cast<double>(rbuf[static_cast<size_t>(e)]);
                            ++okw;
                        }
                        if (okw > 0) {
                            sm->meanWav.assign(static_cast<size_t>(wElems), 0);
                            for (int e = 0; e < wElems; ++e)
                                sm->meanWav[static_cast<size_t>(e)] =
                                    static_cast<int16_t>(std::lround(
                                        mw[static_cast<size_t>(e)] / okw));
                        }
                    }
                }

                // Remove absorbed-neighbour models so they are not phantom
                // references for WaveKnn.
                if (!absorbed.empty()) {
                    mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
                        [&](const ChunkModel& cm){
                            return cm.chunkIdx == ck
                                && absorbed.count(cm.localClusterId) > 0;
                        }), mdls.end());
                }

                if (P.route == 0) knnForChunk[static_cast<size_t>(ck)].push_back(P.src);
                else              cemForChunk[static_cast<size_t>(ck)].push_back(P.src);
            }
        }

        // ── Serial gather: scratch per-chunk vectors → final allowlist maps.
        std::map<int, std::vector<int>> knnAllow, cemAllow;
        for (int ck = 0; ck < nCh; ++ck) {
            if (!knnForChunk[static_cast<size_t>(ck)].empty())
                knnAllow[ck] = std::move(knnForChunk[static_cast<size_t>(ck)]);
            if (!cemForChunk[static_cast<size_t>(ck)].empty())
                cemAllow[ck] = std::move(cemForChunk[static_cast<size_t>(ck)]);
        }

        if (knnAllow.empty() && cemAllow.empty()) {
            LockedStderr("[Stage 2.12] iter %d: no eligible pools; stopping\n", it + 1);
            break;
        }

        // Snapshot to count spikes changed by the re-split.
        std::vector<std::vector<int>> before = perChunkClass;

        // ── Re-split each pooled super-cluster (allowlisted) ────────────
        if (!knnAllow.empty())
            WaveKnnSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                                 nFullDims, &knnAllow);
        if (!cemAllow.empty())
            FullCemSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                                 nFullDims, &cemAllow);

        // ── Harvest + merge (reuse Phase 4 within-chunk template merge) ──
        const int merged = MedianKnnTemplateMatchEnable
            ? WithinChunkTemplateMatchMedianKnn(chunkPoints, perChunkClass,
                                                perChunkModels, nChan, nSamp,
                                                TemplateMatchScore)
            : WithinChunkTemplateMatch(chunkPoints, perChunkClass,
                                       perChunkModels, nChan, nSamp,
                                       TemplateMatchScore);

        for (size_t k = 0; k < perChunkClass.size(); ++k) {
            const auto& a = perChunkClass[k];
            const auto& b = before[k];
            if (a.size() != b.size()) continue;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) ++changedThisIter;
        }
        grandChanged += changedThisIter;

        LockedStderr("[Stage 2.12] iter %d/%d: %d spikes relabeled, %d merge(s)\n",
                     it + 1, maxIters, changedThisIter, merged);
        if (changedThisIter == 0 && merged == 0) break;   // converged
    }

    LockedStderr("[Stage 2.12] done: %d spikes relabeled across remix passes\n",
                 grandChanged);
}


// ---------------------------------------------------------------------------
// KK::ChunkReCEMPerChunk
//
// Phase 2b: chunk-level CEM with RANDOM initialisation.  After
// PerClusterCEMPerChunk has produced fine-grained per-cluster splits,
// this pass runs an ordinary chunk-level CEM seeded with random class
// assignment (the canonical irand(1, K-1) pattern), with K preserved
// from the count of distinct non-noise labels in cls0.
//
// Random init rather than warm-start from cls0: a warm-started CEM
// converges to the nearest local optimum, which is essentially a
// refinement of the input labels — it cannot escape local minima
// Phase 1 and Phase 2a got stuck in.  Random init forces the CEM to
// re-explore the configuration space at the same K, and TrySplits +
// ConsiderDeletion within the loop prune empty sub-clusters and merge
// oversplit groups so the final partition reflects the data's
// structure rather than the warm-start's biases.
//
// Splits are kept enabled; CEM may discover further structure or
// reduce K below nReal via ConsiderDeletion.
//
// Skipped chunks: those with fewer than 2 distinct non-noise labels
// (random init into one cluster is degenerate).
//
// ChunkModel rebuild from final Ks.Mean / Ks.Cov.  This replaces all
// previous ChunkModels for the chunk; cross-chunk Phase 6 reads only
// the rebuilt list.
// ---------------------------------------------------------------------------
void KK::ChunkReCEMPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims, int onlyChunk)
{
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    const char* p2bDesc = nullptr;
    switch (Phase2bMode) {
        case 1:  p2bDesc = "Variational-Bayes GMM, warm-start from Phase-2a labels"; break;
        case 2:  p2bDesc = "warm-start CEM with splits -> VB-GMM auto-prune"; break;
        case 3:  p2bDesc = "residual-PCA split iterations + post-loop normalised-xcorr realign (Klusters)"; break;
        default: p2bDesc = "warm-start from Phase-2a labels (CEM)";  break;
    }
    if (onlyChunk < 0) LockedStderr( "[Stage 2.8] Chunk re-CEM (%s)\n", p2bDesc);

    // ── LPT scheduling: sort chunks by descending expected work ─────────
    // Without this sort, with schedule(dynamic), heterogeneously-sized
    // chunks leave straggler-tail behaviour: smaller chunks finish quickly
    // and the largest chunk (most cluster-spikes after Phase 2a's splits)
    // runs alone on one core for the remainder of the phase.  Phase 2a
    // has the same machinery; Phase 2b was missed.
    //
    // Expected work ~ O(K · N · D²) per CEM iter × max-iter cap.  With N
    // ~ uniform across chunks and D constant, the dominant variable is K
    // = number of distinct alive clusters from Phase 2a.  We sort by
    // K·N descending so the heaviest chunk gets a worker thread while
    // the pool is still full.
    std::vector<int> chunkOrder(nCh);
    {
        std::vector<int> chunkK(nCh, 0);
        for (int ck = 0; ck < nCh; ck++) {
            const auto& cls = perChunkClass[static_cast<size_t>(ck)];
            std::unordered_set<int> uniq(cls.begin(), cls.end());
            chunkK[static_cast<size_t>(ck)] = static_cast<int>(uniq.size());
            chunkOrder[static_cast<size_t>(ck)] = ck;
        }
        std::sort(chunkOrder.begin(), chunkOrder.end(),
                  [&](int a, int b) {
                      const long long workA =
                          static_cast<long long>(chunkK[static_cast<size_t>(a)]) *
                          static_cast<long long>(chunkPoints[static_cast<size_t>(a)].size());
                      const long long workB =
                          static_cast<long long>(chunkK[static_cast<size_t>(b)]) *
                          static_cast<long long>(chunkPoints[static_cast<size_t>(b)].size());
                      return workA > workB;
                  });
        if (Verbose >= 1) {
            int kMax = 0, kMin = INT_MAX;
            size_t nMax = 0, nMin = SIZE_MAX;
            for (int ck = 0; ck < nCh; ck++) {
                kMax = std::max(kMax, chunkK[static_cast<size_t>(ck)]);
                kMin = std::min(kMin, chunkK[static_cast<size_t>(ck)]);
                nMax = std::max(nMax, chunkPoints[static_cast<size_t>(ck)].size());
                nMin = std::min(nMin, chunkPoints[static_cast<size_t>(ck)].size());
            }
            if (onlyChunk < 0) LockedStderr(
                    "[Stage 2.8] LPT order: nChunks=%d, K range %d-%d, "
                    "N range %zu-%zu (heaviest first)\n",
                    nCh, kMin, kMax, nMin, nMax);
        }
    }

    struct ChunkResult {
        bool                    changed = false;
        int                     deltaClusters = 0;
        std::vector<int>        newClass;
        std::vector<ChunkModel> newModels;
    };
    std::vector<ChunkResult> results(nCh);

    int totalChunksProcessed = 0;
    int totalDeltaClusters   = 0;

    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) \
        reduction(+:totalChunksProcessed,totalDeltaClusters)
    for (int oi = 0; oi < nCh; oi++) {
        const int ck = chunkOrder[static_cast<size_t>(oi)];
        if (onlyChunk >= 0 && ck != onlyChunk) continue;
        const auto& pts = chunkPoints[static_cast<size_t>(ck)];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0) continue;

        const auto& cls0 = perChunkClass[static_cast<size_t>(ck)];
        if (static_cast<int>(cls0.size()) != nPts) continue;

        // Skip chunks with ≤ 2 distinct labels (noise + at most 1 cluster).
        std::unordered_set<int> uniqueLcs(cls0.begin(), cls0.end());
        if (uniqueLcs.size() <= 2) continue;

        const int aliveBefore = static_cast<int>(uniqueLcs.size());

        // Build sub-KK warm-started from cls0.
        //
        // SPATIAL-ONLY: time excluded.  Same rationale as Phase 2a — the
        // standalone Klusters recluster path runs CEMTwoPhase which is
        // spatial-only in Phase 1, so chunk re-CEM should match that for
        // bimodality discovery.  Time as a feature within a single chunk
        // is weak signal (per-cluster time variance ≈ chunk duration is
        // large) and its parameter cost in BIC inflates the rejection
        // bar for marginal spatial splits.  Stripping time at copy-in
        // gives consistent stride throughout.
        const int nSpatialDimsFull = (nFullDims > 1) ? nFullDims - 1 : nFullDims;

        // ── Per-chunk feature selection ──────────────────────────
        // Same mechanism as Phase 2a (see comment there).  Phase 2b
        // operates on the chunk's full spike set rather than one
        // cluster's spikes, so within-chunk variance ranks features
        // by overall spread across all units in the chunk — features
        // along which the chunk's spike cloud is fattest.  Those are
        // the directions where multi-unit structure can hide; CEM
        // discovers it.
        int  nSubDims;
        std::vector<int> selFeat;

        if (SubspaceDims > 0 && SubspaceDims < nSpatialDimsFull) {
            std::vector<double> sum(nSpatialDimsFull, 0.0);
            std::vector<double> sqsum(nSpatialDimsFull, 0.0);
            for (int i = 0; i < nPts; i++) {
                const int p = pts[static_cast<size_t>(i)];
                for (int d = 0; d < nSpatialDimsFull; d++) {
                    const double v = Data[static_cast<size_t>(p) * nFullDims + d];
                    sum[d]   += v;
                    sqsum[d] += v * v;
                }
            }
            std::vector<std::pair<double,int>> rank(nSpatialDimsFull);
            const double invN = 1.0 / nPts;
            for (int d = 0; d < nSpatialDimsFull; d++) {
                const double m = sum[d] * invN;
                const double v = std::max(0.0, sqsum[d] * invN - m * m);
                rank[d] = {v, d};
            }
            std::sort(rank.begin(), rank.end(),
                      [](const auto& a, const auto& b){ return a.first > b.first; });
            nSubDims = SubspaceDims;
            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int k = 0; k < nSubDims; k++)
                selFeat[static_cast<size_t>(k)] = rank[static_cast<size_t>(k)].second;
            std::sort(selFeat.begin(), selFeat.end());
        } else {
            nSubDims = nSpatialDimsFull;
            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int d = 0; d < nSubDims; d++)
                selFeat[static_cast<size_t>(d)] = d;
        }

        KK Ks;
        Ks.nDims              = nSubDims;
        Ks.nPoints            = nPts;
        Ks.penaltyMix         = penaltyMix;
        Ks.suppressBestSave   = true;
        Ks.minClustersAlive   = 1;
        Ks.NoisePoint         = 0;

        Ks.AllocateArrays();
        Ks.AllocateCholeskyVecs();
        Ks.ReinitForSplit(nPts, nSubDims, penaltyMix);

        // Pack data using selected features only.
        for (int i = 0; i < nPts; i++) {
            const int p = pts[static_cast<size_t>(i)];
            for (int k = 0; k < nSubDims; k++) {
                const int d = selFeat[static_cast<size_t>(k)];
                Ks.Data[static_cast<size_t>(i) * nSubDims + k] =
                    Data[static_cast<size_t>(p) * nFullDims + d];
            }
        }
        Ks.timeRawMin = timeRawMin;
        Ks.timeRawMax = timeRawMax;

        // Phase 2b WARM-STARTS from cls0 (Phase 2a's labels).  Rationale:
        // with patch11 feature selection in Phase 2a, structure discovery
        // happens at 2a in a low-dim subspace where BIC permits marginal
        // splits.  Phase 2b's job is then to REFINE those labels at the
        // chunk level (or, with -SubspaceDims > 0, in 2b's chunk-wide
        // subspace) — not to re-explore from scratch.  Warm-start
        // converges fast and preserves 2a's discoveries; ConsiderDeletion
        // and (optionally) TrySplits adjust K from there.
        //
        // Map cls0's possibly-sparse cluster IDs ({0, 5, 7, 12, ...}) to
        // a contiguous range ({0, 1, 2, 3, ...}) for the sub-KK.  Cluster
        // 0 stays as noise; non-noise IDs get sequential mapping in the
        // order they appear in uniqueLcs.
        int nReal = 0;
        for (int c : uniqueLcs) if (c > 0) nReal++;
        if (nReal < 2) continue;  // not enough structure to refine

        std::unordered_map<int,int> lcToK;
        lcToK.reserve(uniqueLcs.size());
        lcToK[0] = 0;  // noise stays as noise
        int nextK = 1;
        for (int c : uniqueLcs) {
            if (c <= 0) continue;
            lcToK[c] = nextK++;
        }

        const int nStart = nReal + 1;  // +1 for noise (cluster 0)
        Ks.nStartingClusters = nStart;
        for (int c = 0; c < MaxPossibleClusters; c++)
            Ks.ClassAlive[c] = (c < nStart) ? 1 : 0;
        for (int i = 0; i < nPts; i++) {
            const auto it = lcToK.find(cls0[static_cast<size_t>(i)]);
            Ks.Class[i] = (it != lcToK.end()) ? it->second : 0;
        }
        Ks.Reindex();

        if (Phase2bMode == 1) {
            // ── Phase 2b mode 1: Variational-Bayes GMM (patch16) ──────────
            //
            // Replace CEM warm-start + RunEMLoop with VB-GMM iterations on
            // the same warm-start labels.  VB-GMM's Bayesian K-pruning via
            // the Dirichlet prior replaces ConsiderDeletion's BIC-driven
            // pruning; soft posteriors give boundary spikes graceful
            // assignment instead of hard commitment.
            //
            // VB output is hard-assigned (argmax of converged posteriors)
            // so the rest of the pipeline interfaces unchanged.  Then call
            // MStep to repopulate Ks.Mean / Ks.Cov from the hard labels —
            // ChunkModel rebuild reads from those.
            std::vector<int> labelsBuf(static_cast<size_t>(nPts));
            for (int i = 0; i < nPts; i++) labelsBuf[i] = Ks.Class[i];

            const int nSurvivors = RunVBGMM(
                Ks.Data.m_Data, labelsBuf.data(),
                /*K_init=*/ nStart,
                /*N=*/      nPts,
                /*D=*/      nSubDims,
                /*maxIter=*/VBGMMMaxIter,
                /*convTol=*/static_cast<double>(VBGMMConvTol));
            (void)nSurvivors;

            // Write back labels.  VB may have left some k indices unused
            // (pruned); ChunkModel rebuild's nMembers count handles the
            // gaps.  Reindex compacts alive-cluster IDs.
            for (int i = 0; i < nPts; i++) Ks.Class[i] = labelsBuf[i];
            // Refresh ClassAlive from the labels actually present
            for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
            for (int i = 0; i < nPts; i++) {
                const int c = Ks.Class[i];
                if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
            }
            Ks.Reindex();
            Ks.MStep();   // populate Mean/Cov for ChunkModel rebuild below
        } else if (Phase2bMode == 2) {
            // ── Phase 2b mode 2: preseed -> split -> VB-GMM ───────────────
            //
            // Three-stage hybrid that exercises both directions of K change:
            //
            //   1. Preseed: warm-start from Phase-2a labels (already set
            //      up above; Ks.Class[] populated, Ks.ClassAlive[] set).
            //
            //   2. Split: run CEM with TrySplits enabled.  Grows K wherever
            //      BIC supports a split — including reorganisations within
            //      Phase 2a's clusters that 2a's per-cluster CEM didn't
            //      explore (because 2a operates on each cluster in
            //      isolation).  ConsiderDeletion may also fire here.
            //
            //   3. Prune: hand the (possibly expanded) label set to VB-GMM.
            //      The Dirichlet prior consolidates clusters that CEM's
            //      TrySplits accepted on marginal BIC evidence, when the
            //      data don't support the split under the Bayesian
            //      criterion.
            //
            // Net effect: K can both grow (via CEM in step 2) AND shrink
            // (via VB in step 3) within a single Phase 2b pass, whereas
            // mode 0 only shrinks via deletion and mode 1 only refines
            // around Phase 2a's existing K.
            //
            // ── Stage 2: CEM with splits ────────────────────────────────
            Ks.MStep();
            Ks.EStep();
            Ks.RunEMLoop(/*enableSplits=*/  true,
                         /*enableDistDump=*/ false,
                         /*maxIter=*/        0,
                         /*phaseLabel=*/     "[2b-split]");

            // ── Stage 3: VB-GMM auto-prune on post-CEM labels ───────────
            // K_init = max-alive-index + 1; fresh scan of Class[] avoids
            // having to track through Reindex / TrySplits internals.
            int K_init = 1;
            for (int i = 0; i < nPts; i++) {
                const int c = Ks.Class[i];
                if (c >= K_init) K_init = c + 1;
            }
            std::vector<int> labelsBuf(static_cast<size_t>(nPts));
            for (int i = 0; i < nPts; i++) labelsBuf[i] = Ks.Class[i];

            const int nSurvivors = RunVBGMM(
                Ks.Data.m_Data, labelsBuf.data(),
                /*K_init=*/ K_init,
                /*N=*/      nPts,
                /*D=*/      nSubDims,
                /*maxIter=*/VBGMMMaxIter,
                /*convTol=*/static_cast<double>(VBGMMConvTol));
            (void)nSurvivors;

            for (int i = 0; i < nPts; i++) Ks.Class[i] = labelsBuf[i];
            for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
            for (int i = 0; i < nPts; i++) {
                const int c = Ks.Class[i];
                if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
            }
            Ks.Reindex();
            Ks.MStep();
        } else if (Phase2bMode == 3) {
            // ── Phase 2b mode 3: residual-PCA iterative refinement ────────
            //
            // Per-chunk loop that addresses two failure modes mode 1/2 don't:
            //
            //   (i)  Sub-structure hiding INSIDE an existing cluster — two
            //        neurons whose mean waveforms are similar (so the
            //        cluster passes initial CEM/VBGMM) but whose RESIDUAL
            //        waveforms differ.  Subtracting each cluster's mean and
            //        re-PCA'ing the residual exposes that difference;
            //        running VBGMM on the residual features splits the
            //        cluster.
            //
            //   (ii) Within-cluster temporal jitter — spikes from the same
            //        neuron whose .spk windows are misaligned by 1-3 samples
            //        (Phase 1a's cluster-mean Mahal alignment can leave
            //        residual jitter on the dominant channels because Mahal
            //        is dominated by the high-variance directions, not the
            //        peak channels).  Per-spike cross-correlation against
            //        the cluster mean on the 1-2 DOMINANT channels — the
            //        channels where the neuron is strongest, hence cleanest
            //        SNR — tightens the alignment.
            //
            // Loop body (up to ResidualPCAIter iterations, default 3):
            //   1. VBGMM on current chunk features → labels
            //   2. Read raw spike waveforms from .spk (or .spkD)
            //   3. For each alive cluster:
            //        a. Compute mean waveform (per-channel)
            //        b. Compute residuals = spike − mean
            //        c. Eigendecompose residual covariance → top-K eigenvecs
            //        d. Project residuals → K-dim sub-features
            //        e. Run VBGMM on sub-features; if K > 1 survivors,
            //           SPLIT: keep majority as original ID, new IDs
            //           assigned to the rest.
            //   4. Per-spike xcorr realignment on 1-2 dominant channels
            //      → updates the OUTER KK's m_cumShift[p] for each chunk
            //      member.
            //   5. Convergence: stop if label-change fraction < ConvTol.
            //
            // After the loop, refeaturize-from-.fil happens at the outer
            // scope (driver issues a global RefeaturizeFromShifts after
            // all chunks complete).  Sub-cluster IDs above MaxPossibleClusters
            // get clamped back to the original cluster (no-split) to keep
            // the per-chunk cluster space bounded.
            //
            // Reads the ORIGINAL .spk waveforms via TimeShiftReadSpikeWave
            // (mmap path is thread-safe; FILE* path is serialised on a
            // critical section, which is the existing pattern).  Writes to
            // m_cumShift use disjoint per-chunk indices (the parallel chunks
            // never share spike indices), so the parallel-for stays safe.
            //
            // Hyperparameters via CLI flags (defaults in KlustaKwik.cpp):
            //   ResidualPCAIter             — max outer iterations (3)
            //   ResidualPCAComponents       — top-K residual eigenvecs (3)
            //   ResidualPCASubK             — K_init for residual VBGMM (4)
            //   ResidualPCADominantChannels — 1 or 2 (2)
            //   ResidualPCAConvTol          — convergence threshold (0.01)
            this->RunPhase2bMode3Chunk(Ks, pts, NbChannels,
                                       NbSamplesPerSpike, nStart, ck);
        } else {
            // ── Phase 2b mode 0: warm-start CEM (patch12 default) ─────────
            // Initial MStep + EStep so RunEMLoop has consistent stats, then
            // run normal CEM with splits enabled.  Warm-start means MStep
            // sees Phase 2a's clusters intact; subsequent EStep refines the
            // assignments; TrySplits and ConsiderDeletion adjust K only if
            // BIC strictly prefers the change.
            //
            // Two flag-controlled bounds prevent the per-chunk runtime from
            // exploding on K-heavy chunks (K = 122 was reported with
            // -KnnSplitMinNewClusterSize 50 in user testing):
            //
            //   -Phase2bEnableSplits 0 (default)
            //     Disable TrySplits inside the inner CEM.  Phase 2a's
            //     per-cluster CEM has already done split discovery; the
            //     244·K inner-CEM cost of repeating it here dominates the
            //     phase runtime for chunks with K > ~50.  ConsiderDeletion
            //     remains active (essential for merging Phase 2a's over-
            //     splits via warm-start CEM at the chunk level).
            //
            //   -Phase2bMaxIter 60 (default)
            //     Cap inner CEM iterations.  Warm-start CEM converges in
            //     10-30 iter typically; the cap bounds straggler chunks
            //     where TrySplits/ConsiderDeletion oscillate around a
            //     marginal BIC boundary and the convergence predicate
            //     stays true forever.
            const bool enableSplits =
                (Phase2bEnableSplits != 0);
            const int  innerMaxIter =
                (Phase2bMaxIter > 0) ? Phase2bMaxIter : 0;  // 0 → use MaxIter
            Ks.MStep();
            Ks.EStep();
            Ks.RunEMLoop(enableSplits,
                         /*enableDistDump=*/ false,
                         /*maxIter=*/        innerMaxIter,
                         /*phaseLabel=*/     "[2b]");
        }

        // Read back labels
        std::vector<int> newCls(static_cast<size_t>(nPts));
        for (int i = 0; i < nPts; i++) newCls[static_cast<size_t>(i)] = Ks.Class[i];

        // Refresh Mean/Cov for ChunkModel build
        Ks.MStep();

        // Rebuild ChunkModel list from Ks.Mean / Ks.Cov.
        //
        // Ks operated on `nSubDims` selected features (could be < the
        // full spatial dim count when -SubspaceDims > 0).  Map each
        // Ks-internal index k back to its original spatial slot via
        // selFeat[k], so cm.mean/cov are indexed by the canonical
        // nFullDims layout regardless of which features Ks used.
        //
        // selFeat is ascending-sorted, so selFeat[r] < selFeat[col]
        // when r < col, which preserves the upper-triangle schema.
        // Unused spatial dims (and the time row/col) stay zero.
        std::vector<ChunkModel> newMdls;
        newMdls.reserve(static_cast<size_t>(Ks.nClustersAlive));
        for (int cc = 0; cc < Ks.nClustersAlive; cc++) {
            const int c = Ks.AliveIndex[cc];
            ChunkModel cm;
            cm.chunkIdx        = ck;
            cm.localClusterId  = c;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(static_cast<size_t>(nFullDims), 0.0f);
            cm.cov.assign (static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
            for (int k = 0; k < nSubDims; k++) {
                const int d = selFeat[static_cast<size_t>(k)];
                cm.mean[static_cast<size_t>(d)] =
                    Ks.Mean[static_cast<size_t>(c) * nSubDims + k];
            }
            for (int r = 0; r < nSubDims; r++) {
                const int rd = selFeat[static_cast<size_t>(r)];
                for (int col = r; col < nSubDims; col++) {
                    const int cd = selFeat[static_cast<size_t>(col)];
                    cm.cov[static_cast<size_t>(rd) * nFullDims + cd] =
                        Ks.Cov[static_cast<size_t>(c) * Ks.nDims2
                              + r * nSubDims + col];
                }
            }
            for (int i = 0; i < nPts; i++)
                if (Ks.Class[i] == c) cm.nMembers++;
            if (cm.nMembers == 0) continue;
            newMdls.push_back(std::move(cm));
        }

        results[static_cast<size_t>(ck)].changed       = true;
        results[static_cast<size_t>(ck)].deltaClusters = Ks.nClustersAlive - aliveBefore;
        results[static_cast<size_t>(ck)].newClass      = std::move(newCls);
        results[static_cast<size_t>(ck)].newModels     = std::move(newMdls);

        totalChunksProcessed += 1;
        totalDeltaClusters   += (Ks.nClustersAlive - aliveBefore);
    }

    // Serial application
    for (int ck = 0; ck < nCh; ck++) {
        if (!results[static_cast<size_t>(ck)].changed) continue;
        perChunkClass [static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newClass);
        perChunkModels[static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newModels);
    }

    LockedStderr(
            "[Stage 2.8] Chunk re-CEM: %d chunks processed, net cluster delta = %+d\n",
            totalChunksProcessed, totalDeltaClusters);
}
