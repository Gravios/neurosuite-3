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
#include <cstdint>
#include "KlustaKwik.h"
#include "KlustaSave.h"
#include "dipsplit.h"        // BicPair / bic_two_vs_one — used by RefineExistingClustering
#include "realign_xcorr.h"   // XcorrDispatch::compute — shared normalised circular xcorr

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
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
static constexpr int kMaxStackDims = 64;

// ---------------------------------------------------------------------------
// AllocateArrays
// ---------------------------------------------------------------------------
void KK::AllocateArrays() {
    nDims2  = nDims * nDims;
    FullStep  = 1;
    NoisePoint = 1;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    Data       .SetSize(nPoints * nDims,                "Data (nPoints*nDims)");
    Centres    .SetSize(MaxPossibleClusters * nDims,    "Centres (MaxPossibleClusters*nDims)");
    Weight     .SetSize(MaxPossibleClusters,            "Weight (MaxPossibleClusters)");
    Mean       .SetSize(MaxPossibleClusters * nDims,    "Mean (MaxPossibleClusters*nDims)");
    Cov        .SetSize(MaxPossibleClusters * nDims2,   "Cov (MaxPossibleClusters*nDims2)");
    LogP       .SetSize(MaxPossibleClusters * nPoints,  "LogP (MaxPossibleClusters*nPoints)");

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
    cholFlat    .assign(MaxPossibleClusters * nDims2, 0.0f);
    bestCholFlat.assign(MaxPossibleClusters * nDims2, 0.0f);
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
// LoadData — reads .fet file (auto-detects binary vs legacy text format)
//
// Binary format: int32_t nDimensions; nSpikes * nDimensions * int64_t (row-major)
// Text format  : "%d\n" nDimensions; then nSpikes lines of nDimensions space-
//                separated integers (legacy ndmanager-plugins output)
//
// Detection: if the first byte is an ASCII digit (0x30–0x39) the file is text;
// otherwise it is binary.  Note: this heuristic misclassifies binary files where
// nDimensions & 0xFF falls in 48–57 (e.g., 48–57 or 304–313 dimensions).  For
// typical probe configurations (< 48 dims) this is safe; higher-density arrays
// may need a magic-number format extension.
// ---------------------------------------------------------------------------
void KK::LoadData() {
    char fname[STRLEN + 16];
    // Prefer canonical .fet.N; fall back to stderiv .fetD.N if the
    // canonical is absent.  The ndm_reextractspikes_stderiv pipeline
    // produces .fetD; without this fallback, KlustaKwik would need
    // the caller to symlink .fetD → .fet before every invocation.
    const int fetVariant = pickInputPath(fname, sizeof(fname),
                                         FileBase, "fet", ElecNo);
    if (fetVariant == 1) {
        Output("LoadData: using .fetD variant (%s)\n", fname);
    }
    FILE *fp = fopen_safe(fname, "rb");

    // ── Format detection ──────────────────────────────────────────────────
    unsigned char firstByte = 0;
    if (fread(&firstByte, 1, 1, fp) != 1) Error("Empty .fet file");
    fseeko(fp, 0, SEEK_SET);
    const bool isBinary = (firstByte < 0x30 || firstByte > 0x39); // not ASCII digit

    int nFeatures = 0;

    if (isBinary) {
        // ── Binary path ───────────────────────────────────────────────────
        int32_t nFeatures32 = 0;
        if (fread(&nFeatures32, sizeof(int32_t), 1, fp) != 1)
            Error("Failed to read nFeatures from binary .fet header");
        nFeatures = (int)nFeatures32;
        Output("nFeatures=%d (binary .fet)\n", nFeatures);
        // Guard the modulo on line below: dataBytes % (8 * nFeatures) is
        // undefined behaviour when nFeatures == 0.  Some platforms surface
        // it as SIGFPE/abort, others as a fast-divide that yields random
        // bytes that survive the subsequent != 0 test.  Bail explicitly.
        if (nFeatures < 1)
            Error("Binary .fet header nFeatures=%d is invalid (file=%s)\n",
                  nFeatures, fname);

        // Derive spike count from remaining file size
        fseeko(fp, 0, SEEK_END);
        off_t dataBytes = ftello(fp) - (off_t)sizeof(int32_t);
        fseeko(fp, (off_t)sizeof(int32_t), SEEK_SET);
        if (dataBytes <= 0 || dataBytes % ((off_t)sizeof(int64_t) * nFeatures) != 0)
            Error("Binary .fet file size inconsistent with nFeatures");
        nPoints = (int)(dataBytes / ((off_t)sizeof(int64_t) * nFeatures));

        // Handle "all" keyword — also the default when -UseFeatures is not passed
        if (strcmp(UseFeatures, "all") == 0) {
            if (nFeatures >= STRLEN) Error("Too many features for UseFeatures");
            for (int i = 0; i < nFeatures; i++) UseFeatures[i] = '1';
            UseFeatures[nFeatures] = '\0';
        }
        const int UseLen = static_cast<int>(strlen(UseFeatures));
        if (UseLen != nFeatures)
            Output("WARNING: UseFeatures length (%d) != nFeatures (%d). "
                   "Features beyond position %d will be excluded. "
                   "Pass -UseFeatures all to select all features.\n",
                   UseLen, nFeatures, UseLen);
        nDims = 0;
        for (int i = 0; i < nFeatures; i++)
            nDims += (i < UseLen && UseFeatures[i] == '1') ? 1 : 0;

        if (fSaveModel) {
            ksv().FileBase = FileBase;
            ksv().nDims    = nDims;
            fprintf(pModelFile, "%s %d\n", FileBase, ksv().nDims);
        }
        AllocateArrays();
        AllocateCholeskyVecs();

        for (int p = 0; p < nPoints; p++) {
            int j = 0;
            for (int i = 0; i < nFeatures; i++) {
                int64_t raw;
                if (fread(&raw, sizeof(int64_t), 1, fp) != 1)
                    Error("Short read in binary .fet file");
                if (i < UseLen && UseFeatures[i] == '1')
                    Data[p * nDims + j++] = static_cast<float>(raw);
            }
        }
        { int64_t probe; if (fread(&probe, sizeof(int64_t), 1, fp) != 0)
            Error("Trailing data in binary .fet file"); }

    } else {
        // ── Legacy text path ──────────────────────────────────────────────
        // Count data lines to get nPoints before reading values
        {
            enum { INLINE, FIRST_DELIM } scst = INLINE;
            nPoints = -1;
            char ch, delim = '\n';
            do {
                ch = static_cast<char>(fgetc(fp));
                bool isDelim = (ch == '\n' || ch == '\r');
                bool isEof   = (ch == EOF);
                switch (scst) {
                case INLINE:
                    if (isDelim)    { scst = FIRST_DELIM; delim = ch; }
                    else if (isEof) { nPoints++; }
                    break;
                case FIRST_DELIM:
                    if (!isDelim || delim == ch) { nPoints++; scst = INLINE; }
                    break;
                }
            } while (ch != EOF);
            fseeko(fp, 0, SEEK_SET);
        }

        if (fscanf(fp, "%d", &nFeatures) != 1) Error("Failed to read nFeatures (text .fet)");
        Output("nFeatures=%d (text .fet)\n", nFeatures);

        // Handle "all" keyword — also the default when -UseFeatures is not passed
        if (strcmp(UseFeatures, "all") == 0) {
            if (nFeatures >= STRLEN) Error("Too many features for UseFeatures");
            for (int i = 0; i < nFeatures; i++) UseFeatures[i] = '1';
            UseFeatures[nFeatures] = '\0';
        }
        const int UseLen = static_cast<int>(strlen(UseFeatures));
        if (UseLen != nFeatures)
            Output("WARNING: UseFeatures length (%d) != nFeatures (%d). "
                   "Features beyond position %d will be excluded. "
                   "Pass -UseFeatures all to select all features.\n",
                   UseLen, nFeatures, UseLen);
        nDims = 0;
        for (int i = 0; i < nFeatures; i++)
            nDims += (i < UseLen && UseFeatures[i] == '1') ? 1 : 0;

        if (fSaveModel) {
            ksv().FileBase = FileBase;
            ksv().nDims    = nDims;
            fprintf(pModelFile, "%s %d\n", FileBase, ksv().nDims);
        }
        AllocateArrays();
        AllocateCholeskyVecs();

        for (int p = 0; p < nPoints; p++) {
            int j = 0;
            for (int i = 0; i < nFeatures; i++) {
                float val;
                if (fscanf(fp, "%f", &val) == EOF) Error("Error reading feature file (text)");
                if (i < UseLen && UseFeatures[i] == '1')
                    Data[p * nDims + j++] = val;
            }
        }
        { float val; if (fscanf(fp, "%f", &val) != EOF)
            Error("Mismatch reading feature file (text)"); }
    }

    fclose(fp);

    // Normalise each dimension to [0,1] and record raw range for time dim.
    //
    // Cache-efficiency note: Data is point-major (row-major), so a per-column
    // scan (for each dim d: for each point p) strides nDims floats between
    // accesses — a cache miss every access for large nPoints.
    //
    // We replace this with two row-major passes:
    //   Pass 1: one sweep over all points; accumulate per-dim min and max.
    //   Pass 2: one sweep over all points; apply (v - mn) / range.
    // Both passes are fully sequential in memory.
    const int timeDimIdx = nDims - 1;

    std::vector<float> dimMin(nDims,  HugeScore);
    std::vector<float> dimMax(nDims, -HugeScore);

    // Pass 1: find min/max row-major
    for (int p = 0; p < nPoints; p++) {
        const float *row = Data.m_Data + p * nDims;
        for (int i = 0; i < nDims; i++) {
            if (row[i] > dimMax[i]) dimMax[i] = row[i];
            if (row[i] < dimMin[i]) dimMin[i] = row[i];
        }
    }

    // Store raw time range and model metadata
    timeRawMin = dimMin[timeDimIdx];
    timeRawMax = dimMax[timeDimIdx];
    // Save per-dim normalisation for RefeaturizeFromShifts
    dimMin_   = dimMin;
    dimRange_.resize(nDims);
    for (int i = 0; i < nDims; i++)
        dimRange_[i] = (dimMax[i] > dimMin[i])
            ? 1.0f / (dimMax[i] - dimMin[i]) : 1.0f;
    if (fSaveModel) {
        for (int i = 0; i < nDims; i++) {
            ksv().dataMin.push_back(dimMin[i]);
            ksv().dataMax.push_back(dimMax[i]);
            fprintf(pModelFile, "%f %f%c", dimMin[i], dimMax[i],
                    (i < nDims - 1) ? ' ' : '\n');
        }
    }

    // Pass 2: normalise row-major
    std::vector<float> dimRange(nDims);
    for (int i = 0; i < nDims; i++)
        dimRange[i] = (dimMax[i] > dimMin[i]) ? 1.0f / (dimMax[i] - dimMin[i]) : 1.0f;

    for (int p = 0; p < nPoints; p++) {
        float *row = Data.m_Data + p * nDims;
        for (int i = 0; i < nDims; i++)
            row[i] = (row[i] - dimMin[i]) * dimRange[i];
    }

    Output("Loaded %d data points of dimension %d.\n", nPoints, nDims);

#if defined(USE_CUDA) || defined(USE_HIP)
    if (!suppressBestSave && gpu_device_available()) {
        gpu = new KK_GPU();
        gpu->allocate(nPoints, nDims, nDims2, MaxPossibleClusters);
        gpu_upload_data(gpu, Data.m_Data);
        Output("GPU context initialised (%s, %d points, %d dims).\n",
               GPU_BACKEND_NAME, nPoints, nDims);
    }
#elif defined(USE_SYCL)
    if (!suppressBestSave) {
        sycl::device sycl_dev;
        if (sycl_device_available(&sycl_dev)) {
            gpu = new KK_GPU(sycl_dev);
            gpu->allocate(nPoints, nDims, nDims2, MaxPossibleClusters);
            gpu_upload_data(gpu, Data.m_Data);
            Output("GPU context initialised (%s, %d points, %d dims).\n",
                   GPU_BACKEND_NAME, nPoints, nDims);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// cloneInto — deep-copy clustering state into a worker KK.
//
// Used by ParallelK to build N independent worker objects from a single
// "master" KK that owns the loaded data.  Each worker runs its own CEM
// trial in parallel; results are ranked at the end and the best is
// selected.
//
// Replaces the older return-by-value CloneForStart, which required KK
// to be movable.  Out-param form lets KK stay non-copyable AND non-movable,
// which is the safest configuration for a class that owns a raw `gpu`
// pointer (no risk of accidental shallow-copy double-free).
//
// `out` MUST be a default-constructed KK (gpu==nullptr, empty Arrays).
// After this call, out is fully set up for CPU-only EM.
// ---------------------------------------------------------------------------
void KK::cloneInto(KK& out, int ompTeamSz) const
{
    // ── scalar fields ─────────────────────────────────────────────────────
    out.nDims               = nDims;
    out.nDims2              = nDims2;
    out.nPoints             = nPoints;
    out.nStartingClusters   = nStartingClusters;
    out.nClustersAlive      = nClustersAlive;
    out.NoisePoint          = NoisePoint;
    out.FullStep            = FullStep;
    out.penaltyMix          = penaltyMix;
    out.minClustersAlive    = minClustersAlive;
    out.timeRawMin          = timeRawMin;
    out.timeRawMax          = timeRawMax;
    out.log2piHalf          = log2piHalf;
    out.suppressBestSave    = false;
    out.ompTeamSize         = ompTeamSz;  // 0 = all threads; >0 = nested team size
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    out.gpu                 = nullptr;   // CPU-only; GPU stays on master K1
#endif
    out.pKsv                = nullptr;   // caller must set before running EM
    // ── allocate and deep-copy arrays ─────────────────────────────────────
    out.AllocateArrays();
    out.AllocateCholeskyVecs();
    std::copy(Data.m_Data,       Data.m_Data       + nPoints * nDims,     out.Data.m_Data);
    std::copy(Class.m_Data,      Class.m_Data      + nPoints,             out.Class.m_Data);
    std::copy(OldClass.m_Data,   OldClass.m_Data   + nPoints,             out.OldClass.m_Data);
    std::copy(Class2.m_Data,     Class2.m_Data     + nPoints,             out.Class2.m_Data);
    std::copy(BestClass.m_Data,  BestClass.m_Data  + nPoints,             out.BestClass.m_Data);
    std::copy(ClassAlive.m_Data, ClassAlive.m_Data + MaxPossibleClusters, out.ClassAlive.m_Data);
    std::copy(AliveIndex.m_Data, AliveIndex.m_Data + MaxPossibleClusters, out.AliveIndex.m_Data);
    out.cholFlat       = cholFlat;
    out.bestCholFlat   = bestCholFlat;
    out.preseedCentres = preseedCentres;
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
        std::vector<float> h_chol(MaxPossibleClusters * nDims2, 0.0f);
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
        float *clusterLogP     = LogP.m_Data + c * nPoints;

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
                    && clusterLogP[p] - LogP.m_Data[Class[p]*nPoints+p] > DistThresh) {
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
            const float s = LogP.m_Data[c * nPoints + p];  // cluster-major
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
            LogP.m_Data[Class2[p] * nPoints + p] -   // cluster-major
            LogP.m_Data[Class[p]  * nPoints + p];
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

    // ── klustakwikExp: shift-aware merge decision ───────────────────────────
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
// LoadClu — read .clu file (auto-detects binary vs legacy text format)
// Binary format: int32_t nClusters; nSpikes * int32_t clusterIDs (1-based)
// Text format  : "%d\n" nClusters; then nSpikes lines each containing one int
// ---------------------------------------------------------------------------
void KK::LoadClu(const char *CluFile) {
    FILE *fp = fopen_safe(CluFile, "rb");

    unsigned char firstByte = 0;
    if (fread(&firstByte, 1, 1, fp) != 1) Error("Empty .clu file");
    fseeko(fp, 0, SEEK_SET);
    const bool isBinary = (firstByte < 0x30 || firstByte > 0x39);

    if (isBinary) {
        int32_t nclu32 = 0;
        if (fread(&nclu32, sizeof(int32_t), 1, fp) != 1)
            Error("Failed to read nClusters from binary .clu header");
        nStartingClusters = (int)nclu32;
        nClustersAlive = nStartingClusters;
        for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
        for (int p = 0; p < nPoints; p++) {
            int32_t val;
            if (fread(&val, sizeof(int32_t), 1, fp) != 1)
                Error("Short read in binary .clu file");
            Class[p] = (int)val - 1;
        }
    } else {
        int val;
        if (fscanf(fp, "%d", &nStartingClusters) != 1)
            Error("Failed to read nStartingClusters (text .clu)");
        nClustersAlive = nStartingClusters;
        for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
        for (int p = 0; p < nPoints; p++) {
            if (fscanf(fp, "%d", &val) == EOF) Error("Error reading cluster file (text)");
            Class[p] = val - 1;
        }
    }
    fclose(fp);
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

                // ── klustakwikExp: post-split shift-probe refeaturization ──
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
        score += LogP.m_Data[Class[p] * nPoints + p];  // cluster-major
    return score;
}

// ---------------------------------------------------------------------------
// ReportClusterQuality — print per-phase diagnostics to stderr
//
// Computes a compact summary of cluster-quality metrics and prints them as
// a table.  Designed to be called at phase boundaries (e.g. end of Phase 1,
// end of Phase 7) so a degenerate clustering shows up in the log instead of
// only in the final visual review.
//
// Metrics:
//
//   - **Gini** of cluster sizes.  Range [0, 1].  Gini=0 means perfectly
//     equal cluster sizes; Gini=1 means one cluster has all the spikes.
//     Real recordings have a long tail of low-rate units, so Gini ~ 0.5
//     is normal.  Gini ≥ 0.7 with a known mostly-balanced session is a
//     red flag for an absorbed-bimodal cluster (cluster 3 in jg05 group 6
//     gives Gini ~ 0.55 with 50%-of-spikes in one cluster).
//
//   - **maxFrac** — the fraction of spikes in the single largest cluster.
//     Complements Gini for very simple sanity checks.  > 0.4 with > 10
//     alive clusters is very suspicious.
//
//   - **CondMax** — the largest cluster condition number across alive
//     clusters.  Computed as λ_max(Σ_c) / λ_min(Σ_c) on the diagonal of
//     the Cholesky factor (cheap proxy: ratio of max² to min² of diag(L)).
//     A well-conditioned Gaussian has CondMax < ~1e3; > 1e6 means at
//     least one cluster is borderline-singular.
//
// Cost: O(nClusters) for size stats, O(nClusters · nDims) for condition
// numbers.  Negligible relative to MStep/EStep.
// ---------------------------------------------------------------------------
void KK::ReportClusterQuality(const char* phaseLabel) const {
    if (nClustersAlive < 2) return;   // nothing to report

    // ── Cluster sizes ────────────────────────────────────────────────────
    std::vector<int> sizes;
    sizes.reserve(nClustersAlive);
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const int c = AliveIndex[cc];
        if (c == 0) continue;          // skip noise
        int n = 0;
        for (int p = 0; p < nPoints; ++p)
            if (Class[p] == c) ++n;
        if (n > 0) sizes.push_back(n);
    }
    if (sizes.empty()) return;
    std::sort(sizes.begin(), sizes.end());

    // Gini coefficient via the standard sorted formula:
    //   G = (Σ (2i − n − 1) · x_i) / (n · Σ x_i)
    // where x_i is the i-th sorted value (1-indexed).
    double sum_xi = 0.0, weighted = 0.0;
    for (size_t i = 0; i < sizes.size(); ++i) {
        sum_xi   += static_cast<double>(sizes[i]);
        weighted += static_cast<double>(2 * (int)(i + 1) - (int)sizes.size() - 1)
                  * static_cast<double>(sizes[i]);
    }
    const double gini    = (sum_xi > 0.0)
                         ? weighted / (static_cast<double>(sizes.size()) * sum_xi)
                         : 0.0;
    const int    sumAll  = std::accumulate(sizes.begin(), sizes.end(), 0);
    const double maxFrac = static_cast<double>(sizes.back())
                         / static_cast<double>(std::max(1, sumAll));

    // ── Condition numbers via the Cholesky diagonal ──────────────────────
    // For a symmetric positive-definite Σ = L Lᵀ, the eigenvalues of Σ are
    // bounded by [d_min², d_max²] where d_min = min(diag(L)).  This is a
    // cheap upper bound on the condition number; for diagonal-dominant Σ
    // it is exact.  Sufficient for the "is anything borderline-singular"
    // diagnostic.
    double maxCond = 0.0;
    int    worstCluster = -1;
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const int c = AliveIndex[cc];
        if (c == 0) continue;
        const float* chol = cholFlat.data() + static_cast<size_t>(c) * nDims2;
        float dmin =  std::numeric_limits<float>::infinity();
        float dmax = -std::numeric_limits<float>::infinity();
        for (int i = 0; i < nDims; ++i) {
            const float dii = chol[i * nDims + i];
            if (dii < dmin) dmin = dii;
            if (dii > dmax) dmax = dii;
        }
        if (!(dmin > 0.0f) || !std::isfinite(dmax)) continue;
        const double cond = static_cast<double>(dmax / dmin)
                          * static_cast<double>(dmax / dmin);  // λ-ratio ≈ (d_max/d_min)²
        if (cond > maxCond) { maxCond = cond; worstCluster = c; }
    }

    fprintf(stderr,
            "[%s quality]  alive=%d  spikes=%d  gini=%.3f  maxFrac=%.3f  "
            "condMax=%.2g (cluster %d)\n",
            phaseLabel ? phaseLabel : "phase",
            nClustersAlive - 1,           // exclude noise from "alive" count
            sumAll, gini, maxFrac, maxCond,
            worstCluster);

    // Heuristic warnings — calibrated for typical extracellular spike-sorting
    // output, which has a long-tailed cluster-size distribution (a few high-
    // rate units, many low-rate units).  A long tail naturally produces high
    // Gini even when no single cluster dominates, so high-Gini alone is NOT
    // a failure signature.
    //
    // We previously warned on `gini > 0.7 && maxFrac > 0.4`, intending to
    // catch the absorbed-bimodal failure (e.g. jg05-group-6 cluster 3
    // pre-fix).  But the mass distribution alone can't distinguish that
    // failure from a fast-spiking interneuron legitimately holding most
    // of the mass — both produce identical (gini, maxFrac) signatures.
    // The actual failure-mode signature lives in the WAVEFORM (bimodal
    // valley in a PC projection, elongated covariance), which is exactly
    // what DipSplit's bloat + elongation gates measure.  If those gates
    // pass on a high-mass cluster, the cluster is a real unit, not a
    // failure — and the gini/maxFrac warning was firing on biology.
    //
    // The metrics themselves (gini, maxFrac, condMax) remain in the
    // header line above as informational diagnostics.  Only the false-
    // alarm warning is removed.
    //
    // condMax warns independently — borderline-singular covariance is
    // always a structural concern regardless of cluster-size distribution
    // or any biological interpretation.
    if (maxCond > 1e6)
        fprintf(stderr, "  WARNING: cluster %d condition number ≈ %.2g — "
                        "borderline-singular covariance\n",
                        worstCluster, maxCond);
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
            if (Verbose >= 1)
                Output("  %s iter %d%c: %d clusters score %.7g nChanged %d\n",
                       phaseLabel, iter, FullStep ? 'F' : 'Q',
                       nClustersAlive, score, nChanged);
        }
        iter++;

        lastStepFull = FullStep;
        FullStep = (nChanged > static_cast<int>(ChangedThresh * nPoints)
                    || nChanged == 0
                    || iter % FullStepEvery == 0);

        if (iter > maxIter) { Output("%s: max iterations exceeded\n", phaseLabel); break; }

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
        /*phaseLabel=*/     Recurse ? "iter" : "\titer");

    if (DistDump) fprintf(Distfp, "\n");

    // klustakwikExp Phase 8: DipSplit — bimodal-cluster detection & split.
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

// ===========================================================================
// RefineExistingClustering
//
// Curate a hand-edited or previously-sorted .clu using the existing centroids
// and per-cluster covariances as Gaussian priors.  The motivation: a curated
// .clu encodes a lot of information (the operator's manual splits/merges,
// the previous KK run's parameter sweep) that a fresh CEM run would discard.
// Refinement preserves that work and only changes assignments where the
// existing model says they are inconsistent.
//
// Convention: cluster ids 0 (artefact) and 1 (MUA) are not parents under the
// neurosuite convention.  When lockNoiseClu is true (default), no spike is
// ever moved INTO or OUT OF clusters 0 / 1, and those clusters are not
// considered for split or merge.  This matches what the user expects when
// running refinement after a triage pass that put bad spikes in 0/1.
//
// Drift-aware merge gate: when chunkBoundsSec is non-empty, each cluster's
// temporal occupancy is computed (histogram of Data[timeDim] over the chunk
// boundaries).  Two clusters whose occupancy lies in disjoint chunk sets
// are MORE likely to be the same drifting unit (the unit moved across the
// probe and the original sort failed to reconnect the trajectories), so
// merge on a relaxed threshold; co-occurring clusters need stronger
// evidence.  The gate factor is min(1.5, 1.0 + bhattacharyya_overlap),
// where bhattacharyya_overlap = sum_k sqrt(p_k * q_k) over chunks k.
//
// Returns the final score (lower is better, same convention as CEM).
// ===========================================================================
float KK::RefineExistingClustering(
    const char* cluFile,
    const char* mode,
    int   nIters,
    float mergeThresh,
    float splitMinDepth,
    bool  lockNoiseClu,
    const std::vector<float>& chunkBoundsSec)
{
    if (!cluFile || !*cluFile) {
        Error("RefineExistingClustering: cluFile is empty\n");
    }
    const std::string modeStr = mode ? mode : "full";
    const bool wantReassign = (modeStr != "off");
    const bool wantSplit    = (modeStr == "split" || modeStr == "full");
    const bool wantMerge    = (modeStr == "merge" || modeStr == "full");
    if (!wantReassign && !wantSplit && !wantMerge) {
        Output("RefineExistingClustering: mode=off — no-op\n");
        return ComputeScore();
    }

    // ── Load existing clustering ─────────────────────────────────────────────
    Output("\n[Refine] loading seed clustering from %s\n", cluFile);
    LoadClu(cluFile);
    Reindex();
    Output("[Refine]   K_in = %d clusters; %d points; %d dims\n",
           nClustersAlive, nPoints, nDims);

    // ── Phase A: REASSIGN ────────────────────────────────────────────────────
    // Run a constrained EM loop.  We re-use RunEMLoop but with splits disabled
    // (TrySplits would defeat the "warm-start" guarantee — it would explore
    //  K_in+1, K_in+2, ... configurations).  Iteration cap = nIters; the
    // existing ChangedThresh / FullStepEvery still apply, so loops that
    // converge early bail out with no work.
    //
    // A point can only move from cluster A to B if the Mahalanobis penalty
    // says B fits better than A — that's exactly what EStep + CStep already
    // do.  No additional gating needed: the warm models from MStep are
    // compact relative to the data, so spurious cross-cluster jumps are
    // rare unless the existing assignment was genuinely wrong.
    // Both arms below assign `score` unconditionally before any read, so no
    // initial value is needed.
    float score;
    if (wantReassign) {
        Output("[Refine] Phase A — reassign (%d iters max)\n",
               nIters > 0 ? nIters : MaxIter);
        const int saveSplitEvery = SplitEvery;
        SplitEvery = 0;     // disable all splits inside RunEMLoop
        score = RunEMLoop(
            /*enableSplits=*/   false,
            /*enableDistDump=*/ false,
            /*maxIter=*/        nIters > 0 ? nIters : MaxIter,
            /*phaseLabel=*/     "[Refine A]");
        SplitEvery = saveSplitEvery;
        Output("[Refine]   K_after_A = %d  score = %.4g\n", nClustersAlive, score);
    } else {
        // Even when reassign is disabled, we need fresh M-step models for
        // split/merge to operate on — otherwise Mean / Cov reflect whatever
        // state the previous CEM call left behind.
        MStep();
        score = ComputeScore();
    }

    // ── Phase B: SPLIT ───────────────────────────────────────────────────────
    // Reuse the existing DipSplit machinery.  DipSplitPhase iterates alive
    // clusters and probes for bimodal structure (BIC-gated, k-means refined,
    // valley-depth threshold).  The user controls the depth threshold via
    // RefineSplitMinDepth.
    if (wantSplit) {
        Output("[Refine] Phase B — DipSplit on %d clusters (min depth %.2f)\n",
               nClustersAlive, splitMinDepth);
        const float saveValleyThresh = DipSplitValleyThresh;
        const int   saveDipEnable    = DipSplitEnable;
        DipSplitValleyThresh = splitMinDepth;
        DipSplitEnable       = 1;
        // Honor the global-Phase-8 disable here too — if the user has
        // explicitly disabled Phase 8 (drift-resistant mode), don't
        // re-enable it via the refine path's local override.
        if (DipSplitGlobalEnable != 0) {
            DipSplitPhase();
        }
        DipSplitValleyThresh = saveValleyThresh;
        DipSplitEnable       = saveDipEnable;
        // DipSplitPhase mutates Class[] but does not refresh M-step models,
        // so re-fit before the merge phase.
        MStep();
        score = ComputeScore();
        Output("[Refine]   K_after_B = %d  score = %.4g\n", nClustersAlive, score);
    }

    // ── Phase C: MERGE ───────────────────────────────────────────────────────
    if (wantMerge) {
        const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
        const int timeDim      = nDims - 1;

        // Build per-cluster temporal occupancy distribution over chunks (if
        // chunkBoundsSec was given).  occupancy[c] is a [nChunks]-vector of
        // normalised counts (sums to 1 for each non-empty cluster).
        const int nChunks =
            (chunkBoundsSec.size() >= 2)
            ? static_cast<int>(chunkBoundsSec.size()) - 1
            : 0;
        std::vector<std::vector<float>> occupancy(MaxPossibleClusters);
        if (nChunks > 0) {
            // Convert raw-sample chunk boundaries to normalised-time
            // boundaries (Data[timeDim] is already in normalised [0,1] coords
            // for chunked-CEM consumers; we use the same convention here).
            const float sessionSamples = timeRawMax - timeRawMin;
            std::vector<float> normBounds(chunkBoundsSec.size());
            if (sessionSamples > 0.0f && SamplingRate > 0.0f) {
                for (size_t i = 0; i < chunkBoundsSec.size(); ++i)
                    normBounds[i] = (chunkBoundsSec[i] * SamplingRate - timeRawMin)
                                    / sessionSamples;
            } else {
                // Fallback: spread chunks uniformly.  Mostly hit during
                // unit tests where SamplingRate isn't set; in production
                // KlustaKwik.cpp ensures both fields are populated.
                for (int i = 0; i <= nChunks; ++i)
                    normBounds[i] = static_cast<float>(i) / nChunks;
            }

            for (int cc = 0; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                occupancy[c].assign(nChunks, 0.0f);
            }
            for (int p = 0; p < nPoints; ++p) {
                const int c = Class[p];
                if (occupancy[c].empty()) continue;  // not alive
                const float t = Data[p * nDims + timeDim];
                // upper_bound — chunk index = idx-1, clamp to [0, nChunks-1]
                int k = static_cast<int>(
                    std::upper_bound(normBounds.begin(), normBounds.end(), t)
                    - normBounds.begin()) - 1;
                if (k < 0)        k = 0;
                if (k >= nChunks) k = nChunks - 1;
                occupancy[c][k] += 1.0f;
            }
            for (int cc = 0; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                float sum = 0.0f;
                for (float v : occupancy[c]) sum += v;
                if (sum > 0.0f)
                    for (float& v : occupancy[c]) v /= sum;
            }
        }

        // Mahalanobis distance between cluster a and b in spatial dims, using
        // tgt's covariance (matches MergeChunkModels convention).  Returns
        // HugeScore if Cholesky fails.
        auto mahalDist = [&](int a, int b) -> float {
            if (nSpatialDims > kMaxStackDims) return HugeScore;
            float diff[kMaxStackDims];
            for (int d = 0; d < nSpatialDims; ++d)
                diff[d] = Mean[a*nDims + d] - Mean[b*nDims + d];
            float covB[kMaxStackDims*kMaxStackDims];
            float chol[kMaxStackDims*kMaxStackDims];
            float root[kMaxStackDims];
            for (int r = 0; r < nSpatialDims; ++r)
                for (int c = r; c < nSpatialDims; ++c)
                    covB[r*nSpatialDims + c] = Cov[b*nDims2 + r*nDims + c];
            if (Cholesky(covB, chol, nSpatialDims)) return HugeScore;
            TriSolve(chol, diff, root, nSpatialDims);
            float dist = 0.0f;
            for (int d = 0; d < nSpatialDims; ++d) dist += root[d] * root[d];
            return dist;
        };

        // Bhattacharyya overlap of two normalised occupancy distributions.
        // Returns 0 (disjoint) to 1 (identical).
        auto temporalOverlap = [&](int a, int b) -> float {
            if (occupancy[a].empty() || occupancy[b].empty()) return 1.0f;
            float bc = 0.0f;
            for (int k = 0; k < nChunks; ++k)
                bc += std::sqrt(occupancy[a][k] * occupancy[b][k]);
            return bc;
        };

        // Effective Mahalanobis threshold for this pair: relaxed when
        // temporal overlap is low (drift case), tightened when high.  Multiplier
        // ranges from 1.0 (full overlap) to 1.5 (disjoint), capped so we don't
        // merge truly different units that happen to be in different chunks.
        auto pairThresh = [&](int a, int b) -> float {
            if (nChunks <= 1) return mergeThresh;
            const float ov  = temporalOverlap(a, b);   // [0,1]
            const float mul = 1.0f + 0.5f * (1.0f - ov);
            return mergeThresh * mul;
        };

        Output("[Refine] Phase C — pairwise merge "
               "(spatialDims=%d, baseThresh=%.2f, %d chunks)\n",
               nSpatialDims, mergeThresh, nChunks);

        // Iterate until no more merges are accepted.  Each merge rebuilds the
        // alive set and re-fits MStep so subsequent pairs see correct stats.
        int mergePass = 0;
        int mergesThisRun = 0;
        const int kFirstParent = lockNoiseClu ? 2 : 0;
        while (true) {
            mergePass++;
            int nMergedThisPass = 0;

            // Snapshot the alive list at top of pass — Reindex() may mutate
            // mid-pass when ClassAlive[] is updated.
            std::vector<int> alive;
            alive.reserve(nClustersAlive);
            for (int cc = 0; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                if (c >= kFirstParent) alive.push_back(c);
            }

            // Score all candidate pairs by Mahalanobis distance, then process
            // closest-first.  Sorting once per pass is O(K² log K) which is
            // negligible — typical K is < 200.
            struct PairCand {
                int   a, b;
                float dist;
                float thresh;
            };
            std::vector<PairCand> cands;
            cands.reserve(alive.size() * (alive.size() - 1) / 2);
            for (size_t ia = 0; ia < alive.size(); ++ia) {
                for (size_t ib = ia + 1; ib < alive.size(); ++ib) {
                    const int a = alive[ia], b = alive[ib];
                    // Mahalanobis under both covariances; take the smaller —
                    // matches the "either fits the merged cloud" intuition.
                    const float dAB = mahalDist(a, b);
                    const float dBA = mahalDist(b, a);
                    const float d   = std::min(dAB, dBA);
                    const float th  = pairThresh(a, b);
                    if (d < th) cands.push_back({a, b, d, th});
                }
            }
            std::sort(cands.begin(), cands.end(),
                      [](const PairCand& x, const PairCand& y) {
                          return x.dist < y.dist;
                      });

            // Track which clusters have been merged this pass — once a
            // cluster is consumed, don't merge it again until the next pass
            // (so we operate on consistent stats).
            std::vector<char> consumed(MaxPossibleClusters, 0);
            for (const auto& pc : cands) {
                if (consumed[pc.a] || consumed[pc.b]) continue;

                // BIC gate: build labels from the candidate pair, run
                // bic_two_vs_one over the spatial dims.  If k=1 BIC wins
                // by >= 0, the merger is favoured.
                std::vector<int>   memberRows;
                std::vector<int>   memberLabels;
                memberRows.reserve(2048);
                memberLabels.reserve(2048);
                for (int p = 0; p < nPoints; ++p) {
                    if (Class[p] == pc.a)      { memberRows.push_back(p);
                                                 memberLabels.push_back(0); }
                    else if (Class[p] == pc.b) { memberRows.push_back(p);
                                                 memberLabels.push_back(1); }
                }
                const int M = static_cast<int>(memberRows.size());
                if (M < 2 * nSpatialDims) continue;  // too small for BIC

                // Pack spatial features into contiguous float buffer for
                // dipsplit::bic_two_vs_one (which takes float* rows × dim).
                std::vector<float> X(static_cast<size_t>(M) * nSpatialDims);
                for (int i = 0; i < M; ++i) {
                    const int p = memberRows[i];
                    for (int d = 0; d < nSpatialDims; ++d)
                        X[static_cast<size_t>(i) * nSpatialDims + d] =
                            Data[p * nDims + d];
                }
                const dipsplit::BicPair bp = dipsplit::bic_two_vs_one(
                    X.data(), M, nSpatialDims, memberLabels.data());

                // Lower BIC is better.  Accept merger if k=1 BIC <= k=2 BIC
                // (a tie still merges — this is the "absent evidence" case
                // and we lean toward fewer clusters by design).  Override
                // by raising RefineMergeThresh if you want the merge to be
                // stricter — that gates in pairThresh first.
                if (bp.bic_k1 > bp.bic_k2) {
                    // Two-cluster fit is significantly better — keep them.
                    continue;
                }

                // Commit: relabel all spikes from b to a, mark b dead,
                // mark both as consumed for this pass.
                for (int p = 0; p < nPoints; ++p)
                    if (Class[p] == pc.b) Class[p] = pc.a;
                ClassAlive[pc.b] = 0;
                consumed[pc.a] = consumed[pc.b] = 1;
                nMergedThisPass++;
                mergesThisRun++;

                if (Verbose >= 1) {
                    Output("[Refine C]   merged %d <- %d  (mahal²=%.2f, "
                           "thresh=%.2f, BIC₁=%.1f BIC₂=%.1f, ov=%.2f)\n",
                           pc.a, pc.b, pc.dist, pc.thresh,
                           bp.bic_k1, bp.bic_k2,
                           (nChunks > 1) ? temporalOverlap(pc.a, pc.b) : 1.0f);
                }
            }

            if (nMergedThisPass == 0) break;
            // Refresh stats so the next pass sees post-merge models.
            Reindex();
            MStep();
            Output("[Refine C] pass %d: %d merges accepted; K=%d\n",
                   mergePass, nMergedThisPass, nClustersAlive);
        }
        if (mergesThisRun == 0) {
            Output("[Refine C] no merges accepted; K=%d\n", nClustersAlive);
        }
        score = ComputeScore();
        Output("[Refine]   K_after_C = %d  score = %.4g\n", nClustersAlive, score);
    }

    // ── Final tidy ───────────────────────────────────────────────────────────
    // One last reassign pass: after split/merge, points near the new
    // cluster boundaries can have stale assignments.  Single iteration is
    // enough since Mean/Cov are already converged for the current
    // partition.
    if (wantSplit || wantMerge) {
        const int saveSplitEvery = SplitEvery;
        SplitEvery = 0;
        score = RunEMLoop(
            /*enableSplits=*/   false,
            /*enableDistDump=*/ false,
            /*maxIter=*/        std::max(2, std::min(nIters, 5)),
            /*phaseLabel=*/     "[Refine final]");
        SplitEvery = saveSplitEvery;
    }

    Output("[Refine] done — K_final=%d, score=%.4g\n", nClustersAlive, score);
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

    // Seed the RNG from RandomSeed plus an instance-side counter so each
    // call within a process gets a different stream.  RandomSeed comes
    // from KlustaKwik.h (extern int).  The static counter advances every
    // call, giving a different stream per call within a single -nRuns
    // session even when RandomSeed is fixed.
    static std::atomic<unsigned> kmppCallCount{0};
    const unsigned seed = static_cast<unsigned>(RandomSeed)
                        + 0xC0FFEEu * kmppCallCount.fetch_add(1);
    std::mt19937 rng(seed);

    // ── Pick centre 0 uniformly at random ────────────────────────────────
    {
        std::uniform_int_distribution<int> uni(0, nPoints - 1);
        const int p0 = uni(rng);
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
            std::uniform_real_distribution<double> ud(0.0, total);
            const double u = ud(rng);
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
        Output("KMeans++ seeds (%d centres, %d spatial dims, seed=%u):\n",
               nCentres, nSpatialDims, seed);
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
        Output("Phase 1 converged: %d clusters, score %.7g\n", nClustersAlive, score);
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
        Output("Phase 2 done: %d clusters, score %.7g\n", nClustersAlive, score);

        // klustakwikExp Phase 8: DipSplit — bimodal-cluster detection.
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



// ---------------------------------------------------------------------------
int KK::MergeChunkModels(std::vector<ChunkModel>& models,
                          int   nSpatialDims,
                          float mergeThresh,
                          const std::vector<std::unordered_map<int,int>>& overlapVotes)
{
    // ── Phase 6 (redesigned): cross-chunk model matching ──────────────────
    //
    // Two passes, no outer iteration:
    //
    //   Pass 1 (temporal sweep):
    //     For each adjacent chunk pair (k, k+1), use overlapVotes[k] to find
    //     mutual-plurality matches.  This is the AUTHORITATIVE source of
    //     cross-chunk identity — overlap-region spikes were sorted by real
    //     EM in both chunks, so a vote-match is direct evidence of cluster
    //     continuity.  Drift-immune by construction.  Sweeps pairs in
    //     temporal order so a unit's identity propagates through a chain
    //     of contiguous chunks.
    //
    //   Pass 2 (xcorr on leftovers):
    //     "Leftovers" are chunk-clusters that didn't receive any overlap
    //     vote-match in Pass 1 — typically clusters whose chunks have no
    //     overlap configured, or units that fire only in the middle of a
    //     chunk and miss the overlap region.  For each leftover, do a
    //     full N×M xcorr search against every chunk-cluster in any other
    //     chunk, using DIRECTIONAL edge-localised waveforms:
    //
    //         leftover (chunk k) right-edge  ↔  candidate (chunk j) left-edge   when k < j
    //         leftover (chunk k) left-edge   ↔  candidate (chunk j) right-edge  when k > j
    //
    //     The temporally-closest waveforms are used, minimising drift
    //     artefacts.  Falls back to chunk-wide meanWav when an edge
    //     waveform is empty (cluster has fewer than 5 spikes in the edge
    //     window).  Mutual nearest neighbour gating guards against
    //     promiscuous matching.
    //
    // Replaces the previous design which interleaved overlap voting,
    // Mahalanobis MNN, and chunk-wide xcorr per chunk pair, then iterated
    // the whole thing until convergence.  Mahalanobis MNN was filtering out
    // drift-affected merges (same unit, slightly different cov per chunk →
    // inflated symmetric Mahal distance) — the temporal-sweep + edge-waveform
    // xcorr design avoids the problem entirely.
    // ----------------------------------------------------------------------

    (void)nSpatialDims;  // unused (Mahalanobis MNN dropped)
    (void)mergeThresh;   // unused (Mahalanobis MNN dropped)

    const int n = static_cast<int>(models.size());

    // Union-Find with path compression
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> Find = [&](int x) -> int {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto Union = [&](int a, int b) {
        a = Find(a); b = Find(b);
        if (a != b) { parent[b] = a; }
    };

    // ── Merge noise unconditionally to global 0 ──────────────────────────
    int nNoiseMerged = 0, nNoiseChunks = 0, firstNoise = -1;
    for (int i = 0; i < n; i++) {
        if (models[i].localClusterId != 0) continue;
        nNoiseMerged += models[i].nMembers;
        nNoiseChunks++;
        if (firstNoise < 0) { firstNoise = i; continue; }
        Union(firstNoise, i);
    }
    if (firstNoise < 0)
        Output("MergeChunkModels: WARNING — no noise cluster found in any chunk.\n");
    else
        Output("MergeChunkModels: noise — %d spikes across %d chunks merged to global c=0\n",
               nNoiseMerged, nNoiseChunks);

    // ── Index models by chunkIdx ─────────────────────────────────────────
    std::unordered_map<int, std::vector<int>> byChunk;
    for (int i = 0; i < n; i++)
        if (models[i].localClusterId != 0)
            byChunk[models[i].chunkIdx].push_back(i);

    const int maxChunk = byChunk.empty() ? -1 :
        std::max_element(byChunk.begin(), byChunk.end(),
            [](const auto& a, const auto& b){ return a.first < b.first; })->first;

    // ── Pass 1: temporal-order overlap voting ───────────────────────────
    //
    // Mark each chunk-cluster that participated in a vote-match (or
    // vote-confirm — same component already) as "matched", so Pass 2
    // doesn't try to xcorr-match clusters that already have a continuity
    // partner via overlap.
    std::unordered_set<int> matched;   // model indices

    // Confirmed (chunk_pair, model_pair) records from Pass 1.  Harvested
    // alongside the matched/Union ops so we get drift-displacement
    // evidence per adjacent chunk pair for Pass 2's smoothness factor.
    struct ConfirmedPair { int chunkA, chunkB, modelA, modelB; };
    std::vector<ConfirmedPair> confirmedPairs;

    int totalVoteMerges = 0;
    for (int k = 0; k <= maxChunk - 1; k++) {
        auto itA = byChunk.find(k);
        auto itB = byChunk.find(k + 1);
        if (itA == byChunk.end() || itB == byChunk.end()) continue;
        if (k >= static_cast<int>(overlapVotes.size())) continue;
        const auto& votes = overlapVotes[k];
        if (votes.empty()) continue;

        const auto& vecA = itA->second;
        const auto& vecB = itB->second;

        // localClusterId -> model index (within chunk k, k+1)
        std::unordered_map<int,int> localToIdxA, localToIdxB;
        for (int a : vecA) localToIdxA[models[a].localClusterId] = a;
        for (int b : vecB) localToIdxB[models[b].localClusterId] = b;

        // Vote floor: max(3, nOverlapSpikes/500) — sparse clusters still
        // match while very-common noise pairs are filtered out.
        int nOverlapSpikes = 0;
        for (const auto& [key, count] : votes) nOverlapSpikes += count;
        const int voteFloor = std::max(3, nOverlapSpikes / 500);

        // Mutual best-from-each-side
        std::unordered_map<int, std::pair<int,int>> bestFromA, bestFromB;
        for (const auto& [key, count] : votes) {
            const int clsK  = key / MaxPossibleClusters;
            const int clsK1 = key % MaxPossibleClusters;
            auto& bA = bestFromA[clsK];
            if (count > bA.second) bA = {clsK1, count};
            auto& bB = bestFromB[clsK1];
            if (count > bB.second) bB = {clsK, count};
        }

        for (const auto& [clsK, topB] : bestFromA) {
            if (topB.second < voteFloor) continue;
            const int clsK1 = topB.first;
            auto itBest = bestFromB.find(clsK1);
            if (itBest == bestFromB.end() || itBest->second.first != clsK) continue;
            auto itA2 = localToIdxA.find(clsK);
            auto itB2 = localToIdxB.find(clsK1);
            if (itA2 == localToIdxA.end() || itB2 == localToIdxB.end()) continue;
            const int mA = itA2->second;
            const int mB = itB2->second;
            matched.insert(mA);
            matched.insert(mB);
            if (Find(mA) != Find(mB)) {
                Union(mA, mB);
                totalVoteMerges++;
                Output("  vote-match  chunk%d.c%d <-> chunk%d.c%d  votes=%d/%d\n",
                       models[mA].chunkIdx, clsK,
                       models[mB].chunkIdx, clsK1,
                       topB.second, itBest->second.second);
            } else {
                Output("  vote-confirm chunk%d.c%d <-> chunk%d.c%d  votes=%d/%d (already merged)\n",
                       models[mA].chunkIdx, clsK,
                       models[mB].chunkIdx, clsK1,
                       topB.second, itBest->second.second);
            }
            // Harvest this confirmed pair so we can later compute the
            // per-adjacent-chunk-pair drift displacement vector.  These
            // come from the authoritative overlap-vote source — direct
            // spike-sorting evidence of cluster continuity across chunks.
            confirmedPairs.push_back({k, k + 1, mA, mB});
        }
    }
    Output("MergeChunkModels: Pass 1 (overlap voting): %d new merges across %d chunk pairs\n",
           totalVoteMerges, std::max(0, maxChunk));

    // ── Build drift table from Pass 1 confirmed matches ─────────────────
    //
    // For each adjacent chunk pair (k, k+1) with ≥ 3 confirmed matches,
    // compute the mean displacement (per feature dim) and the residual
    // RMS scatter of individual match displacements around that mean.
    //
    //   expected_drift(k → k+1) = mean over matches of (mB.mean - mA.mean)
    //   scatter(k → k+1)        = RMS deviation of individual displacements
    //                             from the mean
    //
    // Used by Pass 2 to compute a smoothness factor on each candidate
    // xcorr match:
    //
    //   actual    = mB.mean - mA.mean
    //   deviation = ||actual - expected_drift(A → B)|| / scatter(A → B)
    //   smoothness = exp(-(deviation / CrossChunkDriftSigma)² / 2)
    //   effective_score = xcorr_score × smoothness
    //
    // Semantics: candidates whose displacement matches the population's
    // average drift (smooth trajectory, or coordinated jump from electrode
    // movement) → smoothness ≈ 1 → no penalty.  Solo jumps or
    // wrong-direction displacements → smoothness ≈ 0 → effectively vetoed.
    //
    // For non-adjacent chunk pairs (a, b), expected drift is the sum of
    // adjacent-pair drifts in the chain; scatter combines via variance
    // addition (independent per-pair errors).
    struct DriftEstimate {
        std::vector<double> mean_shift;
        double scatter;
        int    n_matches;
    };
    std::map<std::pair<int,int>, DriftEstimate> driftTable;

    if (CrossChunkDriftSigma > 0.0f && nSpatialDims > 0) {
        // Group confirmed pairs by adjacent chunk pair
        std::map<std::pair<int,int>, std::vector<std::pair<int,int>>> pairsByChunkPair;
        for (const auto& cp : confirmedPairs) {
            pairsByChunkPair[{cp.chunkA, cp.chunkB}].push_back({cp.modelA, cp.modelB});
        }

        for (const auto& [chunkPair, modelPairs] : pairsByChunkPair) {
            if (static_cast<int>(modelPairs.size()) < 3) continue;

            const int D = nSpatialDims;
            std::vector<double> mean_shift(D, 0.0);
            std::vector<std::vector<double>> displacements;
            displacements.reserve(modelPairs.size());

            for (const auto& [mA, mB] : modelPairs) {
                const auto& mvA = models[mA].mean;
                const auto& mvB = models[mB].mean;
                if (static_cast<int>(mvA.size()) < D ||
                    static_cast<int>(mvB.size()) < D) continue;
                std::vector<double> disp(D);
                for (int j = 0; j < D; j++) {
                    disp[j] = static_cast<double>(mvB[j])
                            - static_cast<double>(mvA[j]);
                    mean_shift[j] += disp[j];
                }
                displacements.push_back(std::move(disp));
            }
            if (displacements.size() < 3) continue;
            const double invN = 1.0 / displacements.size();
            for (int j = 0; j < D; j++) mean_shift[j] *= invN;

            // Residual RMS scatter (Euclidean norm of (disp - mean_shift))
            double sum_dev2 = 0.0;
            for (const auto& disp : displacements) {
                double r2 = 0.0;
                for (int j = 0; j < D; j++) {
                    const double r = disp[j] - mean_shift[j];
                    r2 += r * r;
                }
                sum_dev2 += r2;
            }
            const double scatter = std::sqrt(
                sum_dev2 / std::max<size_t>(1, displacements.size() - 1));

            driftTable[chunkPair] = {
                std::move(mean_shift), scatter,
                static_cast<int>(displacements.size())};
        }

        Output("MergeChunkModels: Drift table — %zu adjacent chunk pairs estimated "
               "(from ≥3 confirmed matches each)\n", driftTable.size());
    }

    // Helper: expected drift from chunk ca to chunk cb (signed) and combined
    // scatter.  For adjacent pairs uses the direct table entry; for
    // non-adjacent pairs chains through intermediate adjacent pairs.
    // Returns scatter = -1 if no estimate possible (any link in the chain
    // is missing from the table).
    auto lookupDrift = [&](int ca, int cb, int D)
        -> std::pair<std::vector<double>, double>
    {
        if (ca == cb) return {std::vector<double>(D, 0.0), 0.0};
        const int ci = std::min(ca, cb);
        const int cj = std::max(ca, cb);
        std::vector<double> total(D, 0.0);
        double total_var = 0.0;
        for (int k = ci; k < cj; k++) {
            auto it = driftTable.find({k, k + 1});
            if (it == driftTable.end())
                return {std::vector<double>(D, 0.0), -1.0};
            for (int j = 0; j < D; j++)
                total[j] += it->second.mean_shift[j];
            total_var += it->second.scatter * it->second.scatter;
        }
        if (ca > cb)
            for (double& v : total) v = -v;
        return {std::move(total), std::sqrt(total_var)};
    };

    // ── Pass 2: F1 N×M xcorr on leftovers, directional edge waveforms ───
    //
    // For each leftover (chunk-cluster not matched by overlap voting),
    // search every chunk-cluster in every other chunk for the best xcorr
    // match.  Edge waveform direction: the leftover's edge nearest the
    // candidate's chunk; the candidate's edge nearest the leftover's
    // chunk.  Falls back to chunk-wide meanWav when an edge waveform is
    // unavailable (cluster has fewer than 5 spikes in the edge window).
    //
    // MNN gating: a leftover's best partner must reciprocate (have the
    // leftover as ITS best back-match across ALL its candidates).  This
    // guards against promiscuous merging — a leftover that scores high
    // against many distant chunks gets matched only if some chunk also
    // scores it highest.
    int totalXcorrMerges = 0;
    int nLeftovers = 0;
    if (CrossChunkTemplateScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        const int mxSh   = std::max(1, NbSamplesPerSpike / 4);

        // Resolve which waveform to use for cluster i when matching against
        // cluster j.  Direction: i.chunkIdx vs j.chunkIdx.  Fall back to
        // chunk-wide meanWav when the requested edge is empty.
        auto pickWav = [&](const ChunkModel& mi, const ChunkModel& mj)
            -> const std::vector<int16_t>* {
            const std::vector<int16_t>* w = nullptr;
            if (mi.chunkIdx < mj.chunkIdx)      w = &mi.meanWavRight;
            else if (mi.chunkIdx > mj.chunkIdx) w = &mi.meanWavLeft;
            else                                w = &mi.meanWav;     // shouldn't happen
            if (!w || static_cast<int>(w->size()) != wElems) {
                if (static_cast<int>(mi.meanWav.size()) == wElems) w = &mi.meanWav;
                else w = nullptr;
            }
            return w;
        };

        // Identify leftovers (non-noise, not matched by Pass 1)
        std::vector<int> leftovers;
        leftovers.reserve(n);
        for (int i = 0; i < n; i++) {
            if (models[i].localClusterId == 0) continue;  // noise
            if (matched.count(i)) continue;
            leftovers.push_back(i);
        }
        nLeftovers = static_cast<int>(leftovers.size());

        // Score: best (li, j) pair seen.  Build bestForward[li] (best j for
        // each leftover li) and bestBackward[j] (best leftover for each j
        // candidate).  MNN: both must agree.
        std::unordered_map<int, std::pair<int,float>> bestForward;   // li -> (j, score)
        std::unordered_map<int, std::pair<int,float>> bestBackward;  // j  -> (li, score)

        // Adjacent-chunk gate (default 1 = adjacent only).  Without this
        // the inner loop is O(N²) over ALL cluster pairs across ALL
        // chunks — for typical fragmented sessions (~10k local clusters
        // across 36 chunks) that is ~100M xcorr calls and dominates wall
        // time.  Setting CrossChunkMaxChunkDistance=1 keeps the work
        // O(N × K_per_chunk × 2 neighbours), which is what the drift
        // model assumes anyway (Pass 1 drift table is per-adjacent-pair).
        const int maxChunkDist = std::max(0, CrossChunkMaxChunkDistance);
        if (maxChunkDist == 0) {
            Output("MergeChunkModels: Pass 2 (edge xcorr): disabled "
                   "(CrossChunkMaxChunkDistance = 0)\n");
        } else {
            Output("MergeChunkModels: Pass 2 (edge xcorr): max chunk "
                   "distance = %d (default 1 = adjacent only; raise to "
                   "match cross-chunk gaps from missing-data periods)\n",
                   maxChunkDist);
        }

        if (maxChunkDist > 0)
        for (int li : leftovers) {
            const ChunkModel& mA = models[li];
            for (int j = 0; j < n; j++) {
                if (j == li) continue;
                const ChunkModel& mB = models[j];
                if (mB.localClusterId == 0)        continue;  // noise
                if (mB.chunkIdx == mA.chunkIdx)    continue;  // same chunk
                if (std::abs(mB.chunkIdx - mA.chunkIdx) > maxChunkDist)
                    continue;                                  // distance gate
                const auto* wA = pickWav(mA, mB);
                const auto* wB = pickWav(mB, mA);
                if (!wA || !wB) continue;

                int sh = 0; float sc = 0.0f;
                XcorrDispatch::compute(
                    wA->data(), wB->data(),
                    1, NbChannels, NbSamplesPerSpike, mxSh, 0.0f, &sh, &sc);
                if (sc < CrossChunkTemplateScore) continue;

                // Drift-smoothness factor.  When CrossChunkDriftSigma > 0
                // AND the chunk pair has a drift estimate from Pass 1,
                // multiply the xcorr score by exp(-(dev/sigma)²/2) where
                // dev = ||actual_displacement - expected|| / scatter.
                // Real drifting units have actual ≈ expected → smoothness ≈ 1.
                // Wrong-direction or solo jumps → smoothness ≈ 0 → effectively
                // vetoed.  No drift estimate available (insufficient Pass 1
                // matches in the chain) → smoothness skipped, fall back to
                // pure xcorr scoring.
                if (CrossChunkDriftSigma > 0.0f && nSpatialDims > 0) {
                    const int D = nSpatialDims;
                    if (static_cast<int>(mA.mean.size()) >= D &&
                        static_cast<int>(mB.mean.size()) >= D)
                    {
                        const auto [expected, scatter] =
                            lookupDrift(mA.chunkIdx, mB.chunkIdx, D);
                        if (scatter > 0.0) {
                            double dev2 = 0.0;
                            for (int j = 0; j < D; j++) {
                                const double d =
                                    (static_cast<double>(mB.mean[j])
                                   - static_cast<double>(mA.mean[j]))
                                  - expected[j];
                                dev2 += d * d;
                            }
                            const double dev_norm =
                                std::sqrt(dev2) / scatter;
                            const double sig =
                                static_cast<double>(CrossChunkDriftSigma);
                            const double smoothness =
                                std::exp(-0.5 * (dev_norm / sig) * (dev_norm / sig));
                            sc = static_cast<float>(sc * smoothness);
                            if (sc < CrossChunkTemplateScore) continue;
                        }
                    }
                }

                auto itF = bestForward.find(li);
                if (itF == bestForward.end() || sc > itF->second.second)
                    bestForward[li] = {j, sc};
                auto itB = bestBackward.find(j);
                if (itB == bestBackward.end() || sc > itB->second.second)
                    bestBackward[j] = {li, sc};
            }
        }

        // Apply MNN merges
        for (auto& [li, fwd] : bestForward) {
            const int j = fwd.first;
            auto itB = bestBackward.find(j);
            if (itB == bestBackward.end() || itB->second.first != li) continue;
            if (Find(li) == Find(j)) continue;
            Union(li, j);
            totalXcorrMerges++;
            Output("  edge-xcorr  chunk%d.c%d <-> chunk%d.c%d  xcorr=%.3f\n",
                   models[li].chunkIdx, models[li].localClusterId,
                   models[j].chunkIdx,  models[j].localClusterId,
                   fwd.second);
        }
    }
    Output("MergeChunkModels: Pass 2 (edge xcorr): %d new merges from %d leftovers\n",
           totalXcorrMerges, nLeftovers);

    // ── Assign contiguous globalClusterIds from component roots ─────────
    std::unordered_map<int,int> rootToGlobal;
    int nextGlobal = 1;

    // Pin all noise roots to 0
    for (int i = 0; i < n; i++)
        if (models[i].localClusterId == 0) { rootToGlobal[Find(i)] = 0; break; }

    for (int i = 0; i < n; i++) {
        const int root = Find(i);
        if (rootToGlobal.count(root) == 0)
            rootToGlobal[root] = nextGlobal++;
        models[i].globalClusterId = rootToGlobal[root];
    }

    const int nGlobal = nextGlobal - 1;
    Output("MergeChunkModels: %d local clusters -> %d global clusters\n",
           n, nGlobal);
    return nGlobal;
}

// ---------------------------------------------------------------------------
// RefeaturizeFromShifts
//
// For each spike with a non-zero cumulative shift (set by the shift
// probe), re-extracts the aligned waveform from the .fil broadband
// file at the corrected sample offset, projects through the saved PCA
// eigenvectors, re-normalises, and writes back into Data[].  Called by
// Phase 9 (TimeShiftFinalize).
//
// Re-extracting from .fil rather than circular-shifting the .spk waveform
// eliminates wrap-around corruption: circular shift of N samples by sh
// fills positions [N-sh .. N-1] with noise from the beginning of the
// original window, which corrupts up to 30% of samples for typical
// 5–10 sample shifts.  Reading from .fil at (rawTs + sh - peakIdx)
// gives a clean, artifact-free aligned waveform from the broadband signal.
//
// Requires NbTotalChannels, GroupChannelIds, and PeakSampleIndex to be
// set (auto-filled from YAML at startup).  Falls back to circular shift
// from .spk when .fil is unavailable.
// ---------------------------------------------------------------------------
void KK::RefeaturizeFromShifts(const std::vector<int>& spikeShifts,
                                 int nChan, int nSamplesPerSpike)
{
    if (spikeShifts.empty() || nChan <= 0 || nSamplesPerSpike <= 0) return;

    // ── Load PCA model ────────────────────────────────────────────────────
    // Prefer canonical .pca.N; fall back to .pcaD.N (stderiv pipeline).
    char pcaPath[STRLEN + 16];
    pickInputPath(pcaPath, sizeof(pcaPath), FileBase, "pca", ElecNo);
    FILE* pf = fopen(pcaPath, "rb");
    if (!pf) {
        Output("RefeaturizeFromShifts: %s not found — skipping re-projection\n",
               pcaPath);
        return;
    }

    struct PcaModel {
        int nChan, data2use, nComp, recShift;
        bool isCentered;
        std::vector<std::vector<double>> mean;
        std::vector<std::vector<double>> eigvec;
    } pm;

    {
        // Header written by process_pca: 5 x int32
        //   [nChannels, data2use, nComponents, isCentered, recShift]
        int32_t nc, d2u, ncomp, ic, rs;
        auto rd = [&](int32_t& v) { return fread(&v, 4, 1, pf) == 1; };
        if (!rd(nc)||!rd(d2u)||!rd(ncomp)||!rd(ic)||!rd(rs)) {
            Output("RefeaturizeFromShifts: truncated PCA header in %s\n", pcaPath);
            fclose(pf); return;
        }
        pm.nChan = nc; pm.data2use = d2u; pm.nComp = ncomp;
        pm.recShift = rs; pm.isCentered = (ic != 0);

        Output("RefeaturizeFromShifts: PCA model — nChan=%d data2use=%d nComp=%d recShift=%d isCentered=%d\n",
               pm.nChan, pm.data2use, pm.nComp, pm.recShift, (int)pm.isCentered);

        if (pm.nChan != nChan) {
            Output("RefeaturizeFromShifts: PCA has %d channels, spike group has %d — "
                   "skipping\n", pm.nChan, nChan);
            fclose(pf); return;
        }

        // process_pca writes ALL means first (all channels), then ALL eigenvectors.
        // Layout: [mean_ch0..mean_ch(N-1)][evec_ch0..evec_ch(N-1)]
        pm.mean.resize(static_cast<size_t>(nc));
        pm.eigvec.resize(static_cast<size_t>(nc));
        for (int ch = 0; ch < nc; ++ch) {
            pm.mean[static_cast<size_t>(ch)].resize(static_cast<size_t>(d2u));
            if (fread(pm.mean[static_cast<size_t>(ch)].data(), 8,
                      static_cast<size_t>(d2u), pf) != static_cast<size_t>(d2u)) {
                Output("RefeaturizeFromShifts: truncated PCA means (ch %d)\n", ch);
                fclose(pf); return;
            }
        }
        for (int ch = 0; ch < nc; ++ch) {
            size_t evSz = static_cast<size_t>(d2u * ncomp);
            pm.eigvec[static_cast<size_t>(ch)].resize(evSz);
            if (fread(pm.eigvec[static_cast<size_t>(ch)].data(), 8, evSz, pf) != evSz) {
                Output("RefeaturizeFromShifts: truncated PCA eigenvectors (ch %d)\n", ch);
                fclose(pf); return;
            }
        }
    }
    fclose(pf);

    // Sanity check: print first mean and eigenvector values
    if (!pm.mean.empty() && !pm.mean[0].empty())
        Output("RefeaturizeFromShifts: ch0 mean[0]=%.4g ev[0]=%.4g\n",
               pm.mean[0][0],
               (!pm.eigvec.empty() && !pm.eigvec[0].empty()) ? pm.eigvec[0][0] : 0.0);

    const int nPCAFeatures = pm.nChan * pm.nComp;
    if (nPCAFeatures > nDims - 1) {
        Output("RefeaturizeFromShifts: PCA feature count (%d) exceeds nDims-1 (%d) — "
               "skipping\n", nPCAFeatures, nDims - 1);
        return;
    }

    // ── Read raw timestamps directly from .res to avoid float precision loss ──
    // Recovering rawTs from the normalised float Data[timeDimIdx] introduces
    // up to ±13 samples of error for late-session spikes (timestamp ~1.17×10^8
    // at 32552 Hz × 1 hour; float has only ~7 significant digits).
    // Klusters reads directly from .res as int64 — we do the same.
    // ── Prefer .fil re-extraction; fall back to .spk circular shift ───────
    const bool canUseFil = (NbTotalChannels > 0 &&
                            !GroupChannelIds.empty() &&
                            PeakSampleIndex >= 0);
    const float sessionSamples = timeRawMax - timeRawMin;
    const int   timeDimIdx     = nDims - 1;
    const int   waveSamples    = nChan * nSamplesPerSpike;

    // Open .res for exact int64 timestamps
    char resPathRFS[STRLEN + 16];
    snprintf(resPathRFS, sizeof(resPathRFS), "%s.res.%d", FileBase, ElecNo);
    FILE* resRFS = fopen(resPathRFS, "rb");

    char filPath[STRLEN + 8], spkPath[STRLEN + 16];
    snprintf(filPath, sizeof(filPath), "%s.fil",     FileBase);
    // Prefer canonical .spk.N; fall back to .spkD.N.  The .spk circular-shift
    // fallback (used when .fil is unavailable) still uses this handle.
    pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo);

    FILE* filFp = canUseFil ? fopen(filPath, "rb") : nullptr;
    FILE* spkFp = nullptr;
    if (!filFp) {
        // .fil unavailable — fall back to .spk circular shift
        spkFp = fopen(spkPath, "rb");
        if (!spkFp) {
            Output("RefeaturizeFromShifts: neither %s nor %s available — "
                   "skipping re-projection\n", filPath, spkPath);
            return;
        }
        Output("RefeaturizeFromShifts: .fil not available, using circular shift "
               "from .spk (may have minor wrap-around artefacts)\n");
    }

    std::vector<int16_t> wave(static_cast<size_t>(waveSamples));
    std::vector<int16_t> filRow;
    if (filFp) filRow.resize(static_cast<size_t>(NbTotalChannels));

    int nReproj = 0, nSkipped = 0;

    for (int p = 0; p < nPoints; ++p) {
        const int sh = (p < static_cast<int>(spikeShifts.size()))
                     ? spikeShifts[p] : 0;
        if (sh == 0 || sh == std::numeric_limits<int>::min()) { ++nSkipped; continue; }

        // Read exact int64 timestamp from .res; fall back to float if unavailable
        int64_t rawTsInt = 0;
        if (resRFS) {
            fseeko(resRFS, static_cast<off_t>(p) * sizeof(int64_t), SEEK_SET);
            { size_t _r = fread(&rawTsInt, sizeof(int64_t), 1, resRFS); (void)_r; }
        }
        const float normTs = Data.m_Data[p * nDims + timeDimIdx];
        const float rawTs  = (rawTsInt > 0)
            ? static_cast<float>(rawTsInt)
            : normTs * sessionSamples + timeRawMin;

        if (filFp) {
            // ── .fil path: re-extract at corrected timestamp ───────────────
            // extTs = rawTs + sh: the window that, after rolling forward by sh,
            // presents the waveform with its peak at PeakSampleIndex.
            // Matches Klusters: startSample = ts - peakSamp0 where ts = oldTs + cumShift.
            const int64_t off  = (rawTsInt > 0 ? rawTsInt : static_cast<int64_t>(rawTs)) + sh - PeakSampleIndex;
            if (off < 0 || off + nSamplesPerSpike >
                    static_cast<int64_t>(sessionSamples) + 1) { ++nSkipped; continue; }

            fseeko(filFp, off * NbTotalChannels * 2, SEEK_SET);
            bool ok = true;
            for (int s = 0; s < nSamplesPerSpike && ok; s++) {
                if (fread(filRow.data(), 2, NbTotalChannels, filFp) !=
                        static_cast<size_t>(NbTotalChannels)) { ok = false; break; }
                for (int c = 0; c < nChan; c++)
                    wave[s * nChan + c] = filRow[GroupChannelIds[c]];
            }
            if (!ok) { ++nSkipped; continue; }

            // For stderiv sessions the basis lives in SDIFF_ALLPAIRS + temporal
            // first-difference space, so the raw voltages we just read must be
            // transformed before projection.  One in-place pass matching
            // process_extractspikes_stderiv exactly.
            if (m_timeShiftBasis.isStderiv) {
                ApplySdiffAllpairsTemporalDiff(wave.data(), nChan, nSamplesPerSpike);
            }
        } else {
            // ── .spk fallback: circular shift ─────────────────────────────
            off_t offset = static_cast<off_t>(p) * waveSamples * sizeof(int16_t);
            if (fseeko(spkFp, offset, SEEK_SET) != 0) { ++nSkipped; continue; }
            std::vector<int16_t> raw(static_cast<size_t>(waveSamples));
            if (fread(raw.data(), sizeof(int16_t), waveSamples, spkFp)
                    != static_cast<size_t>(waveSamples)) { ++nSkipped; continue; }
            const int N = nSamplesPerSpike;
            for (int s = 0; s < N; ++s) {
                const int src = ((s + sh) % N + N) % N;
                for (int c = 0; c < nChan; ++c)
                    wave[s * nChan + c] = raw[src * nChan + c];
            }
        }

        // ── Project through PCA and update Data[] ─────────────────────────
        // Eigenvector layout in PCAE file (written by process_pca):
        //   evecBuf[component * data2use + sample]  (col-major)
        // Correct indexing: ev[k * pm.data2use + s]
        // NOT ev[s * pm.nComp + k] — that would be row-major (wrong).
        float* dataRow = Data.m_Data + p * nDims;
        for (int ch = 0; ch < pm.nChan; ++ch) {
            const auto& mu = pm.mean[static_cast<size_t>(ch)];
            const auto& ev = pm.eigvec[static_cast<size_t>(ch)];
            for (int k = 0; k < pm.nComp; ++k) {
                double val = 0.0;
                for (int s = 0; s < pm.data2use; ++s) {
                    const int sIdx = pm.recShift + s;
                    double raw = static_cast<double>(
                        wave[static_cast<size_t>(sIdx * nChan + ch)]);
                    if (pm.isCentered) raw -= mu[static_cast<size_t>(s)];
                    val += ev[static_cast<size_t>(k * pm.data2use + s)] * raw;
                }
                const int featIdx = ch * pm.nComp + k;
                dataRow[featIdx] = (static_cast<float>(val) - dimMin_[featIdx])
                                   * dimRange_[featIdx];
            }
        }

        // Update .fet timestamp with extTs = rawTs + sh (exact int64)
        if (sessionSamples > 0.0f) {
            const int64_t baseTs = (rawTsInt > 0) ? rawTsInt : static_cast<int64_t>(rawTs);
            dataRow[timeDimIdx] = (static_cast<float>(baseTs + sh) - timeRawMin) / sessionSamples;
        }

        ++nReproj;
    }

    if (resRFS) fclose(resRFS);
    if (filFp) fclose(filFp);
    if (spkFp) fclose(spkFp);

    Output("RefeaturizeFromShifts: re-projected %d spikes via %s, skipped %d\n",
           nReproj, filFp ? ".fil" : ".spk (circular shift)", nSkipped);
}


// ---------------------------------------------------------------------------
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
    return centres;
}

// RunChunkedCEM — three-phase temporal-chunk pipeline
//
// ---------------------------------------------------------------------------
// RunChunkedCEM overload — external boundary list (seconds)
//
// Converts the caller-supplied boundary times to normalised [0,1] fractions
// using the same timeRawMin/Max reference used by the uniform variant, then
// delegates to the core implementation with the pre-built chunkPoints list.
// Everything from Phase 1 onward is identical to the uniform variant.
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

    LockedStderr( "[Phase 0] Chunking (%.0f min, %d drift-adaptive chunks; "
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

    LockedStderr( "[Phase 1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
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
        srand(static_cast<unsigned>(RandomSeed)
              + static_cast<unsigned>(k) * 997u
              + static_cast<unsigned>(run));
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

    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1");

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
    RunAlignmentBlock(TimeShiftAlignAfterPhase1, "Phase 1a");

    // ── Phase 1b: per-chunk DipSplit ──────────────────────────────────────
    //
    // Catches bimodal/elongated clusters that the parametric Phase 1 CEM
    // missed.  Runs per-chunk before Phase 2 so subsequent passes operate
    // on the most-correctly-split inputs, and so cross-chunk merge in
    // Phase 6 sees the correct cluster count.  Replaces the old
    // post-Phase-7 global Phase 8 DipSplit.  No-op when DipSplitEnable=0.
    DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims,
                     "Phase 1b");
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1b");
    RunAlignmentBlock(TimeShiftAlignAfterPhase1b, "Phase 1c");

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
            LockedStderr( "[Phase 2] Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2");
        }

        // Phase 2a: per-cluster ordinary CEM in the full feature space.
        // Replaces SubspaceReclusterPerChunk.  For each cluster in each
        // chunk, runs CEM with splits enabled to find bimodal substructure;
        // updates perChunkClass[] only.
        PerClusterCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a");

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
                "Phase 2a.5");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.5");
        }

        // Phase 2a.6: per-chunk HullSplit (k-NN-graph connected components).
        // Complement to Phase 2a.5 DipSplit — catches multi-D topology
        // (two disconnected components in feature space) that 1D-projected
        // DipSplit misses.  Default off (HullSplitEnable=0).
        if (HullSplitEnable != 0) {
            HullSplitPerChunk(
                chunkPoints, perChunkClass, nFullDims, "Phase 2a.6");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.6");
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
                NbChannels, NbSamplesPerSpike, "Phase 2a.7");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.7");
        }

        // Phase 2b: chunk-level warm-start CEM.  Lets boundary spikes
        // reassign across the new fine-grained label set and lets CEM
        // merge oversplit fragments via ConsiderDeletion.  Rebuilds
        // perChunkModels[] from the converged state.
        ChunkReCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2b");

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
            "Phase 2.5");
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2.5");
    }


    // Phase 7 (Global EM) alignment also disabled: chunk means from
    // CEMTwoPhase are already blended by timeMergeIter and would produce
    // spurious shifts on clean data.  ConsiderDeletion / TimeShiftMergeEvaluate
    // handle the time-shift cases that actually occur (genuine same-unit
    // mergers across drift).

    // ── Post-Phase-2 alignment site ─────────────────────────────────────
    RunAlignmentBlock(TimeShiftAlignAfterPhase2, "Phase 2c");

    // Phase-ordering note: Phase 8 (global post-merge DipSplit) was
    // removed.  Its function is now served by per-chunk DipSplit at
    // Phase 1b and a second pass at Phase 2.5 — operating per-chunk
    // avoids the drift-axis false positives a global pass produces on
    // session-spanning clusters.  Phases 3 (mean-waveform harvest, the
    // [Phase 3] log tag below), 4, and 8 are intentionally non-major
    // phase numbers in the current pipeline; the comment block above
    // RunChunkedCEM enumerates which numbers are alive.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        LockedStderr( "[Phase 3] Mean waveform harvest (channel-major xcorr format)\n");
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
        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
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
                WaveKnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
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
                    "[Phase 4b] AlternatingKnnSplit: iter cap reached "
                    "(%d); continuing with template-match only\n",
                    AlternatingSplitMergeMaxIters);
            } else if (AlternatingSplitMergeEnable != 0 &&
                       AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                       _altNetGrowthStreak >= 2 &&
                       _tmIter < AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit: aborted by "
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
            LockedStderr( "[Phase 4] Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = WithinChunkTemplateMatch(chunkPoints, perChunkClass, perChunkModels,
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
                    "[Phase 4b] AlternatingKnnSplit (iter %d): %d spike "
                    "labels changed; +%d new clusters vs %d merged (net "
                    "growth streak %d/2)\n",
                    _tmIter + 1, _nSpikesSplit, _kNewThisIter, _nMerged,
                    _altNetGrowthStreak);
            }


            if (_nMerged == 0 && _nSpikesSplit == 0) break;
        }
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 4");
    }

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
    RunAlignmentBlock(TimeShiftAlignAfterPhase4, "Phase 4a");

    LockedStderr( "[Phase 5] Cross-chunk model matching (overlap-vote + edge-xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, noOverlapVotes);
    LockedStderr( "[Phase 5] Cross-chunk merge produced %d global clusters\n", nGlobal);
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 5");
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
                "[Phase 5] WARNING: MergeChunkModels produced %d global "
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
    RunAlignmentBlock(TimeShiftAlignAfterPhase5, "Phase 5a");

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
        LockedStderr( "[Phase 6] Global EM: skipped (GlobalMergeIter=0)\n");
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
                float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
        LockedStderr( "[Phase 6] Global warm-start EM\n");
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
                float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
            if (Verbose >= 1)
                Output("  P3 iter %d: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);
            FullStep = 1;
            if (nChanged == 0) { Output("Phase 7 converged at iter %d\n", iter); break; }
        }
    }

    // Per-phase quality summary after Phase 7 (Global EM) — gives a
    // checkpoint before DipSplit potentially mutates the cluster set.
    LogGlobalClusterState("Phase 6 (Global EM)");
    ReportClusterQuality("Phase 7");

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
        LockedStderr( "[Phase 6a] Post-merge cluster realignment\n");
        const int nShifted = TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);
        int nCOMShifted = 0;
        if (EnergyCOMRealign != 0) {
            LockedStderr( "[Phase 6a] Energy-COM realignment\n");
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
                    float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
            LogGlobalClusterState("Phase 6a (post-merge realign)"); ReportClusterQuality("Phase 7a");
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
        LockedStderr( "[Phase 6b] Mean-subtraction template merge "
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
                    float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
            LogGlobalClusterState("Phase 6b (mean-sub merge)"); ReportClusterQuality("Phase 7b");
        }
    }

    // ── Phase 7c — klusters-faithful per-spike realignment ─────────────
    // Mirror of Driver B (second overload).  See KK::KlustersStyleRealign-
    // AllClusters for algorithm details.  Opt-in via -KlustersRealignEnable 1.
    if (KlustersRealignEnable != 0) {
        LockedStderr(
                "[Phase 7c] Klusters-faithful per-spike realignment "
                "(maxShift=%d samples, minSize=%d)\n",
                KlustersRealignMaxShift, KlustersRealignMinSize);
        const int nChanged =
            KlustersStyleRealignAllClusters(NbChannels, NbSamplesPerSpike);
        if (nChanged > 0) {
            LogGlobalClusterState("Phase 7c (klusters realign)");
        }
    }

    // Phase 8 DipSplit removed: per-chunk DipSplit now runs as Phase 1b,
    // before cross-chunk model matching, so global splits at this stage
    // would be operating on already-merged clusters where apparent
    // bimodality is more likely a drift artefact than a real second mode.

    // Per-phase quality summary at end of pipeline (after DipSplit).  Catches
    // failures like "one cluster ate everything" before the user sees it in
    // the GUI.  See KK::ReportClusterQuality for metric definitions.
    ReportClusterQuality("final");

    Output("RunChunkedCEM(ext) done: %d clusters, score %.7g\n", nClustersAlive, score);
    return score;
}

// ---------------------------------------------------------------------------
// Chunked-CEM pipeline (RunChunkedCEM).  Phase numbering matches what is
// printed as `[Phase X]` banners in the run log.
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
//   Phase 3   mean-waveform harvest ([Phase 3] log tag — a setup step
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

    LockedStderr( "[Phase 0] Chunking (%.0f min, %d chunks, %.0f min/chunk; %s)\n",
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

    LockedStderr( "[Phase 1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
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
        srand(static_cast<unsigned>(RandomSeed)
              + static_cast<unsigned>(k) * 997u
              + static_cast<unsigned>(run));
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

    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1");

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
    RunAlignmentBlock(TimeShiftAlignAfterPhase1, "Phase 1a");

    // ── Phase 1b: per-chunk DipSplit ──────────────────────────────────────
    //
    // Catches bimodal/elongated clusters that the parametric Phase 1 CEM
    // missed.  Runs per-chunk before Phase 2 so subsequent passes operate
    // on the most-correctly-split inputs, and so cross-chunk merge in
    // Phase 6 sees the correct cluster count.  Replaces the old
    // post-Phase-7 global Phase 8 DipSplit.  No-op when DipSplitEnable=0.
    DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims,
                     "Phase 1b");
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1b");
    RunAlignmentBlock(TimeShiftAlignAfterPhase1b, "Phase 1c");

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
            LockedStderr( "[Phase 2] Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2");
        }

        // Phase 2a: per-cluster ordinary CEM in the full feature space.
        // Replaces SubspaceReclusterPerChunk.  For each cluster in each
        // chunk, runs CEM with splits enabled to find bimodal substructure;
        // updates perChunkClass[] only.
        PerClusterCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a");

        // Phase 2a.5: per-chunk DipSplit (see commentary at the matching
        // insertion in the first overload of RunChunkedCEM).  Gated by
        // DipSplitEnable AND DipSplitBeforePhase2b.
        if (DipSplitEnable != 0 && DipSplitBeforePhase2b != 0) {
            DipSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims,
                "Phase 2a.5");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.5");
        }

        // Phase 2a.6: per-chunk HullSplit, see commentary at the matching
        // insertion in the first overload of RunChunkedCEM.
        if (HullSplitEnable != 0) {
            HullSplitPerChunk(
                chunkPoints, perChunkClass, nFullDims, "Phase 2a.6");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.6");
        }

        // Phase 2a.7: per-channel amplitude+phase split, see commentary at
        // the matching insertion in the first overload.
        if (PerChannelSplitEnable != 0) {
            PerChannelSplitPerChunk(
                chunkPoints, perChunkClass,
                NbChannels, NbSamplesPerSpike, "Phase 2a.7");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.7");
        }

        // Phase 2b: chunk-level warm-start CEM.  Lets boundary spikes
        // reassign across the new fine-grained label set and lets CEM
        // merge oversplit fragments via ConsiderDeletion.  Rebuilds
        // perChunkModels[] from the converged state.
        ChunkReCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2b");

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
            "Phase 2.5");

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
	
    }


    // Phase 7 (Global EM) alignment also disabled: chunk means from
    // CEMTwoPhase are already blended by timeMergeIter and would produce
    // spurious shifts on clean data.  ConsiderDeletion / TimeShiftMergeEvaluate
    // handle the time-shift cases that actually occur (genuine same-unit
    // mergers across drift).

    // ── Post-Phase-2 alignment site ─────────────────────────────────────
    RunAlignmentBlock(TimeShiftAlignAfterPhase2, "Phase 2c");

    // Phase-ordering note: Phase 8 (global post-merge DipSplit) was
    // removed.  Its function is now served by per-chunk DipSplit at
    // Phase 1b and a second pass at Phase 2.5 — operating per-chunk
    // avoids the drift-axis false positives a global pass produces on
    // session-spanning clusters.  Phases 3 (mean-waveform harvest, the
    // [Phase 3] log tag below), 4, and 8 are intentionally non-major
    // phase numbers in the current pipeline; the comment block above
    // RunChunkedCEM enumerates which numbers are alive.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        LockedStderr( "[Phase 3] Mean waveform harvest (channel-major xcorr format)\n");
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
    // Build overlap vote matrices for MergeChunkModels.
    std::vector<std::unordered_map<int,int>> overlapVotes(
        nActive > 0 ? nActive - 1 : 0);
    if (chunkOverlapFrac > 0.0f) {
        for (int k = 0; k < nActive - 1; k++) {
            // Translate compacted chunk index k to the original chunk index that
            // was used when building overlapForPair.  If the compacted chunks k
            // and k+1 are NOT consecutive in the original numbering, the boundary
            // was absorbed by a chunk merge and no overlap entries are valid here.
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
            Output("  Overlap pair orig%d/orig%d (active %d/%d): %d shared spikes, %d (clsK,clsK1) pairs\n",
                   origK, origK + 1, k, k + 1, totalVotes, static_cast<int>(votes.size()));
        }
    }

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
        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
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
                WaveKnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
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
                    "[Phase 4b] AlternatingKnnSplit: iter cap reached "
                    "(%d); continuing with template-match only\n",
                    AlternatingSplitMergeMaxIters);
            } else if (AlternatingSplitMergeEnable != 0 &&
                       AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                       _altNetGrowthStreak >= 2 &&
                       _tmIter < AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit: aborted by "
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
            LockedStderr( "[Phase 4] Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = WithinChunkTemplateMatch(chunkPoints, perChunkClass, perChunkModels,
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
                    "[Phase 4b] AlternatingKnnSplit (iter %d): %d spike "
                    "labels changed; +%d new clusters vs %d merged (net "
                    "growth streak %d/2)\n",
                    _tmIter + 1, _nSpikesSplit, _kNewThisIter, _nMerged,
                    _altNetGrowthStreak);
            }


            if (_nMerged == 0 && _nSpikesSplit == 0) break;
        }
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 4");
    }

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
    RunAlignmentBlock(TimeShiftAlignAfterPhase4, "Phase 4a");

    LockedStderr( "[Phase 5] Cross-chunk model matching (overlap-vote + edge-xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, overlapVotes);
    LockedStderr( "[Phase 5] Cross-chunk merge produced %d global clusters\n", nGlobal);
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 5");
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
                "[Phase 5] WARNING: MergeChunkModels produced %d global "
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
    RunAlignmentBlock(TimeShiftAlignAfterPhase5, "Phase 5a");

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
                float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
        LockedStderr( "[Phase 6] Global warm-start EM\n");
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
                float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
            if (Verbose >= 1)
                Output("  P3 iter %d: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);
            FullStep = 1;
            if (nChanged == 0) { Output("Phase 7 converged at iter %d\n", iter); break; }
        }
    }

    // Per-phase quality summary after Phase 7 (Global EM) — gives a
    // checkpoint before DipSplit potentially mutates the cluster set.
    LogGlobalClusterState("Phase 6 (Global EM)");
    ReportClusterQuality("Phase 7");

    // ── Phase 6a (optional): post-merge cluster realignment ────────────────
    // Mirror of Driver A's Phase 7a.  See Driver A's body for rationale
    // (cross-chunk merge consolidates chunk-local clusters into global
    // units; spikes' Phase-1.5 alignments may no longer be optimal vs.
    // the new global means).
    if (TimeShiftAlignPostMerge != 0 && m_timeShiftReady) {
        LockedStderr( "[Phase 6a] Post-merge cluster realignment\n");
        const int nShifted = TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);
        int nCOMShifted = 0;
        if (EnergyCOMRealign != 0) {
            LockedStderr( "[Phase 6a] Energy-COM realignment\n");
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
                    float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
            LogGlobalClusterState("Phase 6a (post-merge realign)"); ReportClusterQuality("Phase 7a");
        }
    }

    // ── Phase 6b (optional): mean-waveform subtraction merge ─────────
    // Mirror of Driver A's Phase 7b.  See FinalMeanSubtractionMerge for
    // algorithm; opt-in via -MeanSubtractionMergeEnable 1 with threshold
    // -MeanSubtractionMergeThresh (default 0.05) and cyclic-shift
    // radius -MeanSubtractionMergeMaxShift (default 3).
    if (MeanSubtractionMergeEnable != 0) {
        LockedStderr( "[Phase 6b] Mean-subtraction template merge "
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
                    float& lp = LogP.m_Data[Class[p2] * nPoints + p2];
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
            LogGlobalClusterState("Phase 6b (mean-sub merge)"); ReportClusterQuality("Phase 7b");
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
                "[Phase 7c] Klusters-faithful per-spike realignment "
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
            LogGlobalClusterState("Phase 7c (klusters realign)");
        }
    }

    // Phase 8 DipSplit removed (see Driver A above for rationale).

    // Per-phase quality summary at end of pipeline (after DipSplit).
    // See KK::ReportClusterQuality for metric definitions.
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
    snprintf(resPathWPC, sizeof(resPathWPC), "%s.res.%d", FileBase, ElecNo);
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
                            for (int c = 0; c < nChan; c++)
                                spkRow[s * nChan + c] = filRow[GroupChannelIds[c]];
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
        snprintf(resOrigP85, sizeof(resOrigP85), "%s.res.%d", FileBase, ElecNo);
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


// ===========================================================================
// Post-split shift-probe refeaturization  (klustakwikExp)
//
// Purpose
// -------
// After a split is accepted, circularly shift each child cluster's spikes by
// δ ∈ {−1, 0, +1} samples, re-project through the cached PCA basis, and
// commit the shift that MAXIMISES the cluster's spatial-feature variance.
// Unlike standard realignment — which minimises variance by pulling spikes
// toward a template — maximising variance deliberately *spreads* any residual
// mixture structure along the PCA directions that separate subpopulations.
// The next TrySplits iteration then operates on features where residual
// mixtures are as exposed as possible.
//
// Cost
// ----
// Per call: O(|cluster| × 3 × data2use × nChan × nComp) PCA projections plus
// one .spk disk read per spike (no .fil, no .spk write).  At ±1 the wrap-
// around cost at the window boundary is negligible relative to the basis
// vectors' support: data2use is typically ≤ 0.9 × nSamplesPerSpike so the
// wrapped sample is usually outside the PCA's domain.
//
// Finalisation
// ------------
// m_cumShift[p] accumulates the chosen δ across all probe calls.  At program
// end, TimeShiftFinalize() invokes the existing RefeaturizeFromShifts +
// WritePhase15Checkpoint path, which re-extracts from .fil ONLY for spikes
// with m_cumShift[p] != 0 and rewrites .spk / .fet / (normalised .res via
// Data[timeDim]) accordingly.
// ===========================================================================

// ---------------------------------------------------------------------------
// InitTimeShift
// Load .pca[D].N once, build pre-shifted basis tensors for δ∈{-N,…,+N}, and
// open .spk (read-only) for the duration of the run.  Returns true on
// success; false makes the probe a no-op for this session.
// ---------------------------------------------------------------------------
bool KK::InitTimeShift(int nChan, int nSamplesPerSpike, int N_halfWidth)
{
    m_timeShiftReady = false;
    if (nChan <= 0 || nSamplesPerSpike <= 0) return false;
    if (nPoints <= 0 || nDims <= 1)          return false;
    if (N_halfWidth < 0) N_halfWidth = 0;
    if (N_halfWidth > kTimeShiftNmax) {
        Output("InitTimeShift: N_halfWidth=%d exceeds compile-time max %d — "
               "clamping\n", N_halfWidth, kTimeShiftNmax);
        N_halfWidth = kTimeShiftNmax;
    }
    if (N_halfWidth == 0) {
        Output("InitTimeShift: N_halfWidth=0 — probe disabled (no-op)\n");
        return false;
    }
    const int N     = N_halfWidth;
    const int kCand = 2 * N + 1;
    m_timeShiftMaxAbs = N;

    // --- Allocate cumulative-shift accumulator ---
    m_cumShift.assign(static_cast<size_t>(nPoints), 0);

    // --- Load raw PCA basis from .pca[D].N ---
    char pcaPath[STRLEN + 16];
    pickInputPath(pcaPath, sizeof(pcaPath), FileBase, "pca", ElecNo);
    FILE* pf = fopen(pcaPath, "rb");
    if (!pf) {
        Output("InitTimeShift: %s not found — post-split shift probe disabled\n",
               pcaPath);
        return false;
    }

    auto rd32 = [&](int32_t& v) { return fread(&v, 4, 1, pf) == 1; };
    int32_t nc, d2u, ncomp, ic, rs;
    if (!rd32(nc) || !rd32(d2u) || !rd32(ncomp) || !rd32(ic) || !rd32(rs)) {
        Output("InitTimeShift: truncated PCA header in %s — probe disabled\n",
               pcaPath);
        fclose(pf); return false;
    }
    // Channel-count check — accepts canonical .pca.N (nc == nChan) and
    // stderiv .pcaD.N variants with channel reduction.  For SDIFF_ALLPAIRS
    // (order 3) and SDIFF_FIRST (order 1), the last of nChan channels is
    // linearly dependent; process_pca_stderiv drops it at basis-build time,
    // so the .pcaD.N file has nc = nChan - 1 channels.  At probe time we
    // read from .spkD.N (which retains all nChan transformed channels, the
    // last being redundant) and iterate only the first nc = nChan - 1 in
    // the projection loop.
    const bool isStderiv = (nc == nChan - 1);
    if (nc != nChan && !isStderiv) {
        Output("InitTimeShift: PCA has %d channels, spike group has %d "
               "(expected %d for canonical .pca or %d for stderiv .pcaD) "
               "— probe disabled\n",
               nc, nChan, nChan, nChan - 1);
        fclose(pf); return false;
    }
    if (isStderiv) {
        Output("InitTimeShift: stderiv mode (.pcaD.%d basis, %d effective "
               "channels; last-channel-redundant convention)\n",
               ElecNo, nc);
    }
    m_timeShiftBasis.nChan       = nc;
    m_timeShiftBasis.data2use    = d2u;
    m_timeShiftBasis.nComp       = ncomp;
    m_timeShiftBasis.recShift    = rs;
    m_timeShiftBasis.isCentered  = (ic != 0);
    m_timeShiftBasis.N           = N;
    m_timeShiftBasis.isStderiv   = isStderiv;
    m_timeShiftBasis.rawChannels = nChan;   // .spkD stride uses raw count

    // Sanity check: N must be less than the PCA support, otherwise the
    // shifted bases are almost entirely zero and the probe is pointless.
    if (N >= d2u / 2) {
        Output("InitTimeShift: N=%d is >= data2use/2=%d; shifted bases would "
               "be nearly empty — probe disabled\n", N, d2u/2);
        fclose(pf); m_timeShiftBasis = TimeShiftBasis{}; return false;
    }

    // Stage the raw (unshifted) basis first — we discard it after building
    // the (2N+1) pre-shifted copies.
    std::vector<std::vector<double>> rawMean  (static_cast<size_t>(nc));
    std::vector<std::vector<double>> rawEigvec(static_cast<size_t>(nc));
    for (int ch = 0; ch < nc; ++ch) {
        rawMean[static_cast<size_t>(ch)]
            .resize(static_cast<size_t>(d2u));
        if (fread(rawMean[static_cast<size_t>(ch)].data(),
                  8, static_cast<size_t>(d2u), pf) != static_cast<size_t>(d2u)) {
            Output("InitTimeShift: truncated PCA means (ch %d) — probe disabled\n", ch);
            fclose(pf); m_timeShiftBasis = TimeShiftBasis{}; return false;
        }
    }
    const size_t evSz = static_cast<size_t>(d2u * ncomp);
    for (int ch = 0; ch < nc; ++ch) {
        rawEigvec[static_cast<size_t>(ch)].resize(evSz);
        if (fread(rawEigvec[static_cast<size_t>(ch)].data(),
                  8, evSz, pf) != evSz) {
            Output("InitTimeShift: truncated PCA eigenvectors (ch %d) — probe disabled\n", ch);
            fclose(pf); m_timeShiftBasis = TimeShiftBasis{}; return false;
        }
    }
    fclose(pf);

    const int nPCAFeatures = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCAFeatures > nDims - 1) {
        Output("InitTimeShift: PCA feature count (%d) exceeds nDims-1 (%d) — "
               "probe disabled\n", nPCAFeatures, nDims - 1);
        m_timeShiftBasis = TimeShiftBasis{};
        return false;
    }

    // --- Build pre-shifted bases for δ ∈ {-N, …, +N} -----------------------
    // Reindexing identity (derivation):
    //   y_δ[k] = Σ_j E[k,j] · (x[(rs+j+δ)*C+c] − μ[c,j])
    //   let j' = j+δ in the WAVEFORM indexing, so raw sample position is
    //   (rs+j')*C+c.  The basis row that multiplies raw sample (rs+j')*C+c
    //   is therefore E[k, j'−δ] with corresponding mean μ[c, j'−δ].
    //   When j'−δ is outside [0, data2use) we zero-pad (PC tails are near
    //   zero at the window edges — contribution is negligible).
    //
    // Storage layout: eigvecShifted[cand][ch] is a flat vector of length
    // d2u*ncomp indexed as k*d2u + j'.  cand=0..2N maps to δ=cand-N.
    m_timeShiftBasis.meanShifted.assign(static_cast<size_t>(kCand), {});
    m_timeShiftBasis.eigvecShifted.assign(static_cast<size_t>(kCand), {});
    for (int ci = 0; ci < kCand; ++ci) {
        const int delta = ci - N;
        m_timeShiftBasis.meanShifted  [static_cast<size_t>(ci)]
            .assign(static_cast<size_t>(nc), {});
        m_timeShiftBasis.eigvecShifted[static_cast<size_t>(ci)]
            .assign(static_cast<size_t>(nc), {});
        for (int ch = 0; ch < nc; ++ch) {
            auto& muOut = m_timeShiftBasis.meanShifted
                          [static_cast<size_t>(ci)]
                          [static_cast<size_t>(ch)];
            auto& evOut = m_timeShiftBasis.eigvecShifted
                          [static_cast<size_t>(ci)]
                          [static_cast<size_t>(ch)];
            muOut.assign(static_cast<size_t>(d2u), 0.0);
            evOut.assign(evSz, 0.0);
            const auto& muIn = rawMean  [static_cast<size_t>(ch)];
            const auto& evIn = rawEigvec[static_cast<size_t>(ch)];

            for (int jp = 0; jp < d2u; ++jp) {
                const int src = jp - delta;          // read index into raw basis
                if (src < 0 || src >= d2u) continue; // zero-pad outside domain
                muOut[static_cast<size_t>(jp)] =
                    muIn[static_cast<size_t>(src)];
                for (int k = 0; k < ncomp; ++k)
                    evOut[static_cast<size_t>(k * d2u + jp)] =
                        evIn[static_cast<size_t>(k * d2u + src)];
            }
        }
    }

    // --- Open .spk or .spkD ---
    // In stderiv mode the .spkD file holds transformed waveforms that match
    // the .pcaD basis.  Otherwise canonical .spk.
    //
    // Mapping strategy:
    //   Preferred: mmap(MAP_PRIVATE) — gives random-access shared-memory
    //   semantics via the kernel page cache, amortising cluster-scattered
    //   reads across the whole session without per-spike fseeko/fread.
    //   Fallback: fopen — used when mmap fails (e.g. exotic filesystems,
    //   NFS with wonky MAP_PRIVATE support).
    char spkPath[STRLEN + 16];
    if (isStderiv) {
        std::snprintf(spkPath, sizeof(spkPath), "%s.spkD.%d", FileBase, ElecNo);
    } else {
        pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo);
    }

    const int spkFd = open(spkPath, O_RDONLY);
    if (spkFd < 0) {
        Output("InitTimeShift: cannot open %s — probe disabled\n", spkPath);
        m_timeShiftBasis = TimeShiftBasis{};
        return false;
    }
    struct stat st;
    if (fstat(spkFd, &st) != 0 || st.st_size <= 0) {
        Output("InitTimeShift: cannot stat %s — probe disabled\n", spkPath);
        close(spkFd);
        m_timeShiftBasis = TimeShiftBasis{};
        return false;
    }
    const size_t spkBytes = static_cast<size_t>(st.st_size);
    void* spkMap = mmap(nullptr, spkBytes, PROT_READ, MAP_PRIVATE, spkFd, 0);
    close(spkFd);   // mmap holds its own reference

    if (spkMap == MAP_FAILED) {
        // Fallback to stdio.
        Output("InitTimeShift: mmap(%s) failed (%s) — falling back to stdio\n",
               spkPath, std::strerror(errno));
        m_timeShiftSpkFp = fopen(spkPath, "rb");
        if (!m_timeShiftSpkFp) {
            Output("InitTimeShift: cannot open %s — probe disabled\n", spkPath);
            m_timeShiftBasis = TimeShiftBasis{};
            return false;
        }
        m_timeShiftSpkMap = nullptr;
        m_timeShiftSpkLen = 0;
    } else {
        // Advise the kernel that random access is coming so it uses the
        // page cache aggressively without prefetching whole-file sequences.
        madvise(spkMap, spkBytes, MADV_RANDOM);
        m_timeShiftSpkMap = spkMap;
        m_timeShiftSpkLen = spkBytes;
        m_timeShiftSpkFp  = nullptr;
        Output("InitTimeShift: mmap(%s) %.2f MB — random-access page cache active\n",
               spkPath, spkBytes / (1024.0 * 1024.0));
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // Initialise GPU shift-probe context if a GPU is in play.
    // Implementation lives in shiftprobe_<backend>.{cu,hip,cpp}.
    // gpu_timeshift_init returns nullptr on failure; the probe then
    // falls through to the CPU path (still correct, just slower).
    if (gpu) {
        extern TimeShiftGpuCtx* gpu_timeshift_init(
            KK_GPU* base, const TimeShiftBasis& basis, int nChan,
            int nSamplesPerSpike, int nPoints, const char* spkPath);
        m_timeShiftGpuCtx = gpu_timeshift_init(gpu, m_timeShiftBasis,
                                             nChan, nSamplesPerSpike,
                                             nPoints, spkPath);
        if (m_timeShiftGpuCtx)
            Output("InitTimeShift: GPU kernel active (backend=%s)\n",
                   GPU_BACKEND_NAME);
    }
#endif

    m_timeShiftReady = true;
    m_timeShiftCallCount = 0;
    Output("InitTimeShift: ready (nChan=%d data2use=%d nComp=%d recShift=%d "
           "isCentered=%d, pre-shifted bases for δ∈{-%d,…,+%d} (%d candidates))\n",
           m_timeShiftBasis.nChan, m_timeShiftBasis.data2use,
           m_timeShiftBasis.nComp, m_timeShiftBasis.recShift,
           (int)m_timeShiftBasis.isCentered, N, N, kCand);
    return true;
}

// ---------------------------------------------------------------------------
// CloseTimeShift
// ---------------------------------------------------------------------------
void KK::CloseTimeShift()
{
    if (m_timeShiftSpkFp) { fclose(m_timeShiftSpkFp); m_timeShiftSpkFp = nullptr; }
    if (m_timeShiftSpkMap) {
        munmap(m_timeShiftSpkMap, m_timeShiftSpkLen);
        m_timeShiftSpkMap = nullptr;
        m_timeShiftSpkLen = 0;
    }
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (m_timeShiftGpuCtx) {
        extern void gpu_timeshift_free(TimeShiftGpuCtx* ctx);
        gpu_timeshift_free(m_timeShiftGpuCtx);
        m_timeShiftGpuCtx = nullptr;
    }
#endif
    m_timeShiftReady = false;
}

// ---------------------------------------------------------------------------
// TimeShiftReadSpikeWave — read one spike's waveform from .spk / .spkD.
//
// Transparently branches on whichever backing store InitTimeShift chose:
//   • m_timeShiftSpkMap (preferred): const-cast pointer arithmetic + memcpy
//   • m_timeShiftSpkFp  (fallback):  fseeko + fread
//
// Both paths return `waveSamples` int16_t values starting at byte offset
// (p * waveSamples * sizeof(int16_t)).  Caller owns dst.  Returns false on
// bounds failure, I/O error, or when neither backing store is active.
// ---------------------------------------------------------------------------
bool KK::TimeShiftReadSpikeWave(int p, int waveSamples, int16_t* dst)
{
    if (p < 0 || waveSamples <= 0 || !dst) return false;
    const size_t byteOff = static_cast<size_t>(p) *
                           static_cast<size_t>(waveSamples) * sizeof(int16_t);
    const size_t byteLen = static_cast<size_t>(waveSamples) * sizeof(int16_t);

    if (m_timeShiftSpkMap) {
        if (byteOff + byteLen > m_timeShiftSpkLen) return false;
        std::memcpy(dst,
                    static_cast<const char*>(m_timeShiftSpkMap) + byteOff,
                    byteLen);
        return true;
    }
    if (m_timeShiftSpkFp) {
        if (fseeko(m_timeShiftSpkFp, static_cast<off_t>(byteOff), SEEK_SET) != 0)
            return false;
        return fread(dst, sizeof(int16_t),
                     static_cast<size_t>(waveSamples), m_timeShiftSpkFp)
               == static_cast<size_t>(waveSamples);
    }
    return false;
}

// ---------------------------------------------------------------------------
// ApplySdiffAllpairsTemporalDiff — in-place stderiv transform on a spike wave.
//
// Mirrors process_extractspikes_stderiv.cpp::fill_sdiff_buffer() exactly:
//   step 1 per time sample t:  sdiff[t, ch] = nChan * raw[t, ch] − Σ raw[t, :]
//                              (saturating to int16 range)
//   step 2 per time sample t:  wave[t, ch] = sdiff[t, ch] − sdiff[t-1, ch]
//                              (saturating to int16 range)
// Boundary: sdiff[-1, ch] = 0 per spike.  The streaming extraction uses a
// persisted chunk boundary; for per-spike re-extraction (what Phase 4 does)
// sdPrev=0 matches process_pca_stderiv's -d 4 pass-through convention.
//
// Called by RefeaturizeFromShifts (before basis projection) and
// WritePhase15Checkpoint (before writing .spkD.pending) when the basis
// is stderiv.  Static so it's a pure function of its arguments — no
// dependence on KK state beyond what's passed in.
// ---------------------------------------------------------------------------
void KK::ApplySdiffAllpairsTemporalDiff(int16_t* wave, int nChan,
                                        int nSamplesPerSpike)
{
    if (!wave || nChan <= 0 || nSamplesPerSpike <= 0) return;

    // Reusable scratch: per-channel sdiff for "current" and "previous" samples.
    // nChan is small (≤ 16 for typical probes), so heap alloc is negligible.
    std::vector<int32_t> sdiffCur(static_cast<size_t>(nChan), 0);
    std::vector<int32_t> sdiffPrev(static_cast<size_t>(nChan), 0);

    auto satI16 = [](int32_t v) -> int16_t {
        if (v >  32767) return  32767;
        if (v < -32768) return -32768;
        return static_cast<int16_t>(v);
    };

    for (int s = 0; s < nSamplesPerSpike; ++s) {
        // Spatial sum
        int32_t sum = 0;
        for (int c = 0; c < nChan; ++c)
            sum += static_cast<int32_t>(wave[s * nChan + c]);

        // SDIFF_ALLPAIRS + saturate
        for (int c = 0; c < nChan; ++c) {
            const int32_t raw = static_cast<int32_t>(wave[s * nChan + c]);
            int32_t iv = nChan * raw - sum;
            if (iv >  32767) iv =  32767;
            if (iv < -32768) iv = -32768;
            sdiffCur[static_cast<size_t>(c)] = iv;
        }

        // Temporal first-difference + saturate, write in place
        for (int c = 0; c < nChan; ++c) {
            const int32_t diff = sdiffCur[static_cast<size_t>(c)]
                               - sdiffPrev[static_cast<size_t>(c)];
            wave[s * nChan + c] = satI16(diff);
        }

        // Save for next iteration's sdPrev (cannot overwrite before the
        // temporal-diff loop reads sdiffPrev).
        std::swap(sdiffCur, sdiffPrev);
    }
}

// ---------------------------------------------------------------------------
// TimeShiftSplit
//
// Multi-candidate shift-probe applied at split acceptance.  Projects every
// spike under (2N+1) δ-shifted PCA bases; picks the cluster-wide δ that
// maximises the per-PC-dim variance sum across the spike set.
//
// For stderiv sessions: .spkD contains already-transformed waveforms and
// .pcaD was built on matching stderiv data, so the pre-shifted-basis trick
// is applied directly to .spkD content.  The zero-pad approximation at the
// edges of the shifted basis is small for the shift magnitudes used here
// (|δ| ≤ 5 samples within a 20–50-sample window).
//
// Arguments:
//   globalSpikeIndices — indices into Data[] identifying the spikes
//   nChan, nSamplesPerSpike — .spk/.spkD layout dimensions (sample-major)
//
// Returns the number of spikes whose committed shift changed this call.
//
// Core loop: the (2N+1) candidate deltas share every raw-sample load from
// .spk/.spkD.  (2N+1) accumulators per (channel, PC) are stepped through
// data2use samples in a single pass, reading from (2N+1) pre-shifted basis
// buffers.  No modulo, no branching in the hot loop — maps cleanly to SIMD
// and GPU.
//
// Selection: cluster-wide max sum-of-per-dim variance across candidates.
// Winner is committed as a single delta for ALL spikes in the index list.
// ---------------------------------------------------------------------------
int KK::TimeShiftSplit(const std::vector<int>& globalSpikeIndices,
                                    int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady)                                      return 0;
    if (!m_timeShiftSpkMap && !m_timeShiftSpkFp)                return 0;
    if (!m_timeShiftBasis.valid())                              return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0)                    return 0;
    const int nMem = static_cast<int>(globalSpikeIndices.size());
    if (nMem < 2) return 0;   // variance of a single point is 0; skip

    const int waveSamples = nChan * nSamplesPerSpike;
    const int timeDimIdx  = nDims - 1;
    const int nSpatial    = nDims - 1;
    const int nPCA        = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCA <= 0 || nPCA > nSpatial) return 0;
    const float sessionSamples = timeRawMax - timeRawMin;
    if (!(sessionSamples > 0.0f)) return 0;

    const int N     = m_timeShiftBasis.N;
    const int kCand = m_timeShiftBasis.nCand();
    // kCand <= 2*kTimeShiftNmax + 1 = 11 by construction
    constexpr int kCandMax = 2 * kTimeShiftNmax + 1;

    // Pre-size scratch.
    if (static_cast<int>(m_timeShiftWaveScratch.size()) < waveSamples)
        m_timeShiftWaveScratch.assign(static_cast<size_t>(waveSamples), 0);
    m_timeShiftTrialFeats.assign(static_cast<size_t>(kCand * nMem * nPCA), 0.0f);
    m_timeShiftTrialTime .assign(static_cast<size_t>(kCand * nMem),        0.0f);

    std::vector<double> sumPerDim  (static_cast<size_t>(kCand * nPCA), 0.0);
    std::vector<double> sumSqPerDim(static_cast<size_t>(kCand * nPCA), 0.0);

    const auto& pca = m_timeShiftBasis;
    const int   data2use = pca.data2use;
    const int   nComp    = pca.nComp;
    const int   nChanPca = pca.nChan;
    const int   rs       = pca.recShift;
    const bool  isCen    = pca.isCentered;

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // GPU fast path: dispatcher runs the (2N+1)-candidate projection on-device
    // and returns trial features in the same layout as the CPU path.  Falls
    // back to CPU when the context is null or the cluster is too small to
    // amortise launch overhead (threshold tuned for typical L2-resident
    // data2use * nComp * nChan; < 128 spikes almost always lose on GPU).
    const bool useGpu = (m_timeShiftGpuCtx != nullptr) && (nMem >= 128);
    if (useGpu) {
        extern bool gpu_timeshift_project_batch(
            TimeShiftGpuCtx* ctx,
            const std::vector<int>& globalSpikeIndices,
            const std::vector<int>& cumShift,
            int maxShiftAbs,
            const std::vector<float>& dimMin, const std::vector<float>& dimRange,
            float* trialFeatsOut, float* trialTimeOut,
            const float* timeCol, int nDims, float sessionSamples);
        const float* timeCol = Data.m_Data + timeDimIdx;   // strided; kernel uses nDims
        const bool ok = gpu_timeshift_project_batch(
            m_timeShiftGpuCtx,
            globalSpikeIndices, m_cumShift, m_timeShiftMaxAbs,
            dimMin_, dimRange_,
            m_timeShiftTrialFeats.data(), m_timeShiftTrialTime.data(),
            timeCol, nDims, sessionSamples);
        if (ok) {
            // Fold results into per-candidate moments.
            for (int ci = 0; ci < kCand; ++ci)
                for (int mi = 0; mi < nMem; ++mi) {
                    const size_t base = (static_cast<size_t>(ci) * nMem + mi) * nPCA;
                    for (int fi = 0; fi < nPCA; ++fi) {
                        const float fv = m_timeShiftTrialFeats[base + fi];
                        sumPerDim  [ci * nPCA + fi] += fv;
                        sumSqPerDim[ci * nPCA + fi] += static_cast<double>(fv) * fv;
                    }
                }
            goto pick_best_and_commit;
        }
        // ok==false → fall through to CPU path (e.g. transient alloc failure)
    }
#endif

    // CPU path: pre-shifted bases + (2N+1) accumulators in the inner loop.
    {
        int nSkippedRead = 0;
        // Stack-allocated accumulator arrays — size bounded at compile time
        // by kCandMax so the compiler can allocate them to registers.
        for (int mi = 0; mi < nMem; ++mi) {
            const int p = globalSpikeIndices[static_cast<size_t>(mi)];
            if (p < 0 || p >= nPoints) { ++nSkippedRead; continue; }

            if (!TimeShiftReadSpikeWave(p, waveSamples,
                                        m_timeShiftWaveScratch.data())) {
                ++nSkippedRead; continue;
            }

            const int baseCum = m_cumShift[static_cast<size_t>(p)];
            const int16_t* raw = m_timeShiftWaveScratch.data();

            // Per-candidate out-of-range mask (fall back to the δ=0 features,
            // i.e. cand = N, when committing the candidate shift would exceed
            // the global clamp).
            bool candOk[kCandMax];
            for (int ci = 0; ci < kCand; ++ci)
                candOk[ci] = (std::abs(baseCum + (ci - N))
                              <= m_timeShiftMaxAbs);

            // For each channel, accumulate projections for all (2N+1)
            // candidates in one pass over samples.
            for (int ch = 0; ch < nChanPca; ++ch) {
                // Cache basis pointers for this channel × candidate.
                const double* evs[kCandMax];
                const double* mus[kCandMax];
                for (int ci = 0; ci < kCand; ++ci) {
                    evs[ci] = pca.eigvecShifted[ci][static_cast<size_t>(ch)].data();
                    mus[ci] = pca.meanShifted  [ci][static_cast<size_t>(ch)].data();
                }

                for (int k = 0; k < nComp; ++k) {
                    double acc[kCandMax] = {0.0};
                    // Fanned inner loop — reads every raw sample exactly once,
                    // multiplies against (2N+1) shifted basis rows.
                    for (int j = 0; j < data2use; ++j) {
                        const int s = rs + j;
                        const double rawV = static_cast<double>(raw[s * nChan + ch]);
                        if (isCen) {
                            for (int ci = 0; ci < kCand; ++ci)
                                acc[ci] += evs[ci][k * data2use + j]
                                         * (rawV - mus[ci][j]);
                        } else {
                            for (int ci = 0; ci < kCand; ++ci)
                                acc[ci] += evs[ci][k * data2use + j] * rawV;
                        }
                    }

                    const int   fi   = ch * nComp + k;
                    const float min_ = dimMin_  [fi];
                    const float rng_ = dimRange_[fi];
                    const int   cand0 = N;   // the δ=0 candidate index
                    const float f0   = (static_cast<float>(acc[cand0]) - min_) * rng_;

                    for (int ci = 0; ci < kCand; ++ci) {
                        const float fv = (static_cast<float>(acc[ci]) - min_) * rng_;
                        const size_t ofs =
                            (static_cast<size_t>(ci) * nMem + mi) * nPCA + fi;
                        // If candidate δ is out of range for this spike we
                        // substitute the δ=0 features so the variance
                        // criterion is unaffected by out-of-range spikes.
                        m_timeShiftTrialFeats[ofs] = candOk[ci] ? fv : f0;
                        sumPerDim  [ci * nPCA + fi] += m_timeShiftTrialFeats[ofs];
                        sumSqPerDim[ci * nPCA + fi] +=
                            static_cast<double>(m_timeShiftTrialFeats[ofs]) *
                            m_timeShiftTrialFeats[ofs];
                    }
                }
            }

            // Per-candidate trial timestamps (normalised).  Out-of-range
            // candidates keep the original timestamp.
            const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];
            for (int ci = 0; ci < kCand; ++ci) {
                const float dd = candOk[ci]
                    ? static_cast<float>(ci - N) / sessionSamples
                    : 0.0f;
                m_timeShiftTrialTime[static_cast<size_t>(ci) * nMem + mi] =
                    rawTsNorm + dd;
            }
        }
        if (nSkippedRead == nMem) return 0;
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
pick_best_and_commit:
#endif

    // --- Pick the candidate with the largest total spatial-feature variance ---
    const double invN = 1.0 / static_cast<double>(nMem);
    int   bestCand = N;        // default to δ=0 candidate
    double bestVar = -1.0;
    for (int ci = 0; ci < kCand; ++ci) {
        double tot = 0.0;
        for (int fi = 0; fi < nPCA; ++fi) {
            const double s  = sumPerDim  [ci * nPCA + fi];
            const double ss = sumSqPerDim[ci * nPCA + fi];
            const double mean = s * invN;
            const double var  = std::max(0.0, ss * invN - mean * mean);
            tot += var;
        }
        if (tot > bestVar) { bestVar = tot; bestCand = ci; }
    }
    const int bestDelta = bestCand - N;

    // --- Commit winner into Data[] + m_cumShift ---
    int nChanged = 0;
    if (bestDelta != 0) {
        for (int mi = 0; mi < nMem; ++mi) {
            const int p = globalSpikeIndices[static_cast<size_t>(mi)];
            if (p < 0 || p >= nPoints) continue;
            const int wouldBe = m_cumShift[static_cast<size_t>(p)] + bestDelta;
            if (std::abs(wouldBe) > m_timeShiftMaxAbs) continue;

            const size_t featBase =
                (static_cast<size_t>(bestCand) * nMem + mi) * nPCA;
            float* dataRow = Data.m_Data + p * nDims;
            for (int fi = 0; fi < nPCA; ++fi)
                dataRow[fi] = m_timeShiftTrialFeats[featBase + fi];
            dataRow[timeDimIdx] =
                m_timeShiftTrialTime[static_cast<size_t>(bestCand) * nMem + mi];
            m_cumShift[static_cast<size_t>(p)] = wouldBe;
            ++nChanged;
        }
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // Data[] changed on host.  Re-upload to device so the next MStep/EStep
    // reads the refreshed features.  Scatter-upload would be cheaper; for
    // now a full upload is simpler and still substantially cheaper than
    // the EM steps that follow.
    if (nChanged > 0 && gpu)
        gpu_upload_data(gpu, Data.m_Data);
#endif

    ++m_timeShiftCallCount;
    return nChanged;
}

// ---------------------------------------------------------------------------
// ShiftWaveformRowInPlace
//
// Apply m_cumShift[p] to a freshly-read .spk waveform row, in-place.
// Used by Phase 4 mean-waveform harvest so meanWav/Left/Right reflect
// the shifted spike geometry that EStep/MStep already see post-probe.
//
// Algorithm: per-channel circular shift.  For shift sh (in samples):
//   new_row[s, ch] = old_row[(s - sh) mod nSamples, ch]
// Implementing the shift on the .spk-window directly (rather than
// re-extracting from .fil) is correct to within boundary samples; on
// high-pass-filtered data with flat edges those wrapped samples are
// baseline noise and the approximation is essentially exact for
// |sh| <= 1-2 samples (which is the only range the split probe uses
// when InitTimeShift is called with N_halfWidth=1).
//
// No-ops if the probe never ran (m_cumShift empty) or this spike has
// not been shifted (sh == 0).  Cost when sh != 0: one tmp allocation
// of nSamples*nChan int16 + a single copy pass.
// ---------------------------------------------------------------------------
void KK::ShiftWaveformRowInPlace(int16_t* row, int p,
                                 int nChan, int nSamples) const
{
    if (m_cumShift.empty()) return;
    if (p < 0 || p >= static_cast<int>(m_cumShift.size())) return;
    const int sh = m_cumShift[static_cast<size_t>(p)];
    if (sh == 0) return;
    if (nSamples <= 0 || nChan <= 0) return;

    const int total = nChan * nSamples;
    std::vector<int16_t> tmp(static_cast<size_t>(total));
    for (int s = 0; s < nSamples; s++) {
        // Wrap (s - sh) into [0, nSamples).  Negative-modulo behavior
        // is robust under either pre-C++11 or post-C++11 rules with
        // the (... + nSamples) % nSamples form.
        const int src = ((s - sh) % nSamples + nSamples) % nSamples;
        const int srcRow = src * nChan;
        const int dstRow = s   * nChan;
        for (int ch = 0; ch < nChan; ch++)
            tmp[static_cast<size_t>(dstRow + ch)] =
                row[static_cast<size_t>(srcRow + ch)];
    }
    std::copy(tmp.begin(), tmp.end(), row);
}

// ---------------------------------------------------------------------------
// TimeShiftMergeTighten
//
// Per-spike shift selection under a Mahalanobis-minimum criterion against a
// receiving cluster's Gaussian (mean + Cholesky factor of covariance).  Each
// spike picks INDEPENDENTLY the δ ∈ {-N, …, +N} that maximises its fit to
// the destination cluster.
//
// Rationale.  Split-probe uses max-variance because we want to EXPOSE
// mixture structure.  Merge-probe uses min-Mahalanobis because we want to
// TIGHTEN the fit of newly-transferred points to the cluster that is
// absorbing them.  Same infrastructure (pre-shifted bases, I/O, GPU) but a
// different selection criterion and per-spike rather than cluster-wide.
//
// Mahalanobis²(x_δ) = ||L^-1 · (x_δ - μ)||²  where L = destChol (lower tri).
// Forward-substitute to compute y = L^-1 · (x - μ); Mahal² = yᵀy.
// Constants (logRootDet, log2π, log(weight)) cancel across candidates for
// the same destination cluster, so we skip them.
//
// Parameters
//   globalSpikeIndices  — 0-based global indices into Data[] / .spk / m_cumShift
//   nChan, nSamplesPerSpike — .spk layout dimensions
//   destMean  — cluster mean vector (nDims floats, INCLUDING the time dim)
//   destChol  — cluster Cholesky factor (nDims² floats, lower triangular
//               stored row-major; diag positive, super-diag entries 0)
//
// Returns the number of spikes whose cumShift changed this call.
// ---------------------------------------------------------------------------
int KK::TimeShiftMergeTighten(
    const std::vector<int>& globalSpikeIndices,
    int nChan, int nSamplesPerSpike,
    const float* destMean, const float* destChol)
{
    if (!m_timeShiftReady || (!m_timeShiftSpkMap && !m_timeShiftSpkFp))  return 0;
    if (!m_timeShiftBasis.valid())             return 0;
    if (!destMean || !destChol)               return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0)  return 0;
    const int nMem = static_cast<int>(globalSpikeIndices.size());
    if (nMem < 1) return 0;

    const int waveSamples = nChan * nSamplesPerSpike;
    const int timeDimIdx  = nDims - 1;
    const int nPCA        = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCA <= 0 || nPCA > nDims - 1) return 0;
    const float sessionSamples = timeRawMax - timeRawMin;
    if (!(sessionSamples > 0.0f)) return 0;

    const int N     = m_timeShiftBasis.N;
    const int kCand = m_timeShiftBasis.nCand();
    constexpr int kCandMax = 2 * kTimeShiftNmax + 1;

    // We reuse the split-probe scratch buffers to avoid a second allocation.
    // Trial features are computed for all candidates; Mahal² is computed on
    // the fly per spike per candidate.
    if (static_cast<int>(m_timeShiftWaveScratch.size()) < waveSamples)
        m_timeShiftWaveScratch.assign(static_cast<size_t>(waveSamples), 0);

    const auto& pca   = m_timeShiftBasis;
    const int   data2use = pca.data2use;
    const int   nComp    = pca.nComp;
    const int   nChanPca = pca.nChan;
    const int   rs       = pca.recShift;
    const bool  isCen    = pca.isCentered;

    // Stack buffers sized for the worst case.  nDims is small (O(30)) so
    // these fit comfortably.
    std::vector<float> trialFeats(static_cast<size_t>(kCand * nPCA));
    std::vector<float> yVec      (static_cast<size_t>(nDims));  // forward-sub scratch
    std::vector<float> xMinusMu  (static_cast<size_t>(nDims));

    int nChanged = 0;
    int nSkippedRead = 0;

    for (int mi = 0; mi < nMem; ++mi) {
        const int p = globalSpikeIndices[static_cast<size_t>(mi)];
        if (p < 0 || p >= nPoints) { ++nSkippedRead; continue; }

        if (!TimeShiftReadSpikeWave(p, waveSamples,
                                    m_timeShiftWaveScratch.data())) {
            ++nSkippedRead; continue;
        }
        // CRITICAL: bring the freshly-read .spk waveform forward by the
        // cumulative shift already committed for this spike before we
        // project through any candidate basis.  Without this step the
        // probe interprets `bestDelta` as an ABSOLUTE shift from raw .spk
        // rather than an INCREMENT from the current state — yielding a
        // runaway when chained across multiple alignment passes
        // (TimeShiftAlignPhase calls this in a loop of TimeShiftAlignIter
        // passes, ConsiderDeletion's post-merge tightener is itself called
        // from the various TimeShiftAlignAfterPhase* hooks).  With the
        // shift applied here, ci=N (bestDelta=0) correctly means "stay at
        // the current cumulative position" and convergence is possible.
        // (The mean-waveform harvest path applies this same correction;
        // see Phase 4 callers at ~3565/3704/4683/4860/7132.)
        ShiftWaveformRowInPlace(m_timeShiftWaveScratch.data(), p,
                                nChan, nSamplesPerSpike);

        const int baseCum = m_cumShift[static_cast<size_t>(p)];
        const int16_t* raw = m_timeShiftWaveScratch.data();

        bool candOk[kCandMax];
        for (int ci = 0; ci < kCand; ++ci)
            candOk[ci] = (std::abs(baseCum + (ci - N))
                          <= m_timeShiftMaxAbs);

        // Project this spike under every candidate δ — same (2N+1)-fanned
        // inner loop as the split probe, but results are kept per-spike in
        // the local `trialFeats` scratch (not the cluster-wide buffer).
        for (int ch = 0; ch < nChanPca; ++ch) {
            const double* evs[kCandMax];
            const double* mus[kCandMax];
            for (int ci = 0; ci < kCand; ++ci) {
                evs[ci] = pca.eigvecShifted[ci][static_cast<size_t>(ch)].data();
                mus[ci] = pca.meanShifted  [ci][static_cast<size_t>(ch)].data();
            }

            for (int k = 0; k < nComp; ++k) {
                double acc[kCandMax] = {0.0};
                for (int j = 0; j < data2use; ++j) {
                    const int s = rs + j;
                    const double rawV = static_cast<double>(raw[s * nChan + ch]);
                    if (isCen) {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j]
                                     * (rawV - mus[ci][j]);
                    } else {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j] * rawV;
                    }
                }
                const int   fi   = ch * nComp + k;
                const float min_ = dimMin_  [fi];
                const float rng_ = dimRange_[fi];
                for (int ci = 0; ci < kCand; ++ci)
                    trialFeats[static_cast<size_t>(ci * nPCA + fi)] =
                        (static_cast<float>(acc[ci]) - min_) * rng_;
            }
        }

        // Trial timestamps for this spike (normalised).
        const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];

        // Evaluate Mahalanobis² under the destination cluster for each
        // in-range candidate and pick the minimum.  Also track the
        // δ=0 candidate's Mahal² separately so the post-loop gate
        // (-TimeShiftAlignScoreThresh) can require a minimum
        // improvement before committing a non-baseline shift.
        int   bestCand     = N;
        float bestMahal    = std::numeric_limits<float>::infinity();
        float baselineMahal = std::numeric_limits<float>::infinity();

        for (int ci = 0; ci < kCand; ++ci) {
            if (!candOk[ci]) continue;

            // Build x (nDims): first nPCA from trial, remaining spatial dims
            // from the current Data row (unchanged by the probe), final dim
            // = candidate-shifted normalised time.
            const float* trial = trialFeats.data() + ci * nPCA;
            for (int d = 0; d < nPCA; ++d)
                xMinusMu[d] = trial[d] - destMean[d];
            // Pass-through for any non-PCA spatial dims that the cluster
            // carries — leaves them at their current Data[] values.  The
            // probe does not perturb these.
            for (int d = nPCA; d < nDims - 1; ++d)
                xMinusMu[d] = Data.m_Data[p * nDims + d] - destMean[d];
            // candOk[ci] is guaranteed true at this point — the !candOk
            // candidates were already skipped by the `continue` above.
            const float dt = static_cast<float>(ci - N) / sessionSamples;
            xMinusMu[nDims - 1] = (rawTsNorm + dt) - destMean[nDims - 1];

            // Forward substitution: L · y = (x − μ)  →  y
            // chol is lower-triangular row-major: chol[i*nDims + j] for j<=i.
            // Any cluster with a zero diagonal entry → singular → skip.
            float mahal2 = 0.0f;
            bool  bad    = false;
            for (int i = 0; i < nDims; ++i) {
                float s = xMinusMu[i];
                for (int j = 0; j < i; ++j)
                    s -= destChol[i * nDims + j] * yVec[j];
                const float diag = destChol[i * nDims + i];
                if (!(diag > 0.0f)) { bad = true; break; }
                yVec[i] = s / diag;
                mahal2 += yVec[i] * yVec[i];
            }
            if (bad) continue;

            if (ci == N) baselineMahal = mahal2;

            if (mahal2 < bestMahal) {
                bestMahal = mahal2;
                bestCand  = ci;
            }
        }

        // Threshold gate (-TimeShiftAlignScoreThresh): when a non-baseline
        // candidate wins, require its Mahal² improvement over δ=0 to
        // exceed the user-set threshold; otherwise fall back to baseline.
        // With the default 0.0, this gate is a no-op since the argmin
        // guarantees bestMahal ≤ baselineMahal, so `baselineMahal -
        // bestMahal` is always ≥ 0 and never strictly less than 0.
        // Raise the threshold to suppress sub-noise-floor shifts whose
        // Mahal² improvement is dominated by numerical jitter and which,
        // compounded over TimeShiftAlignIter passes, can tighten a wrong
        // post-merge composite mean around spikes that don't really
        // belong (the Phase 6a "reinforce a bad Phase 6 merge" mode).
        if (bestCand != N
            && std::isfinite(baselineMahal)
            && (baselineMahal - bestMahal) < TimeShiftAlignScoreThresh) {
            bestCand  = N;
            bestMahal = baselineMahal;
        }

        const int bestDelta = bestCand - N;
        if (bestDelta == 0) continue;   // no change

        const int wouldBe = baseCum + bestDelta;
        if (std::abs(wouldBe) > m_timeShiftMaxAbs) continue;

        // Commit the per-spike shift.
        float* dataRow = Data.m_Data + p * nDims;
        const float* bestTrial = trialFeats.data() + bestCand * nPCA;
        for (int fi = 0; fi < nPCA; ++fi)
            dataRow[fi] = bestTrial[fi];
        dataRow[timeDimIdx] = rawTsNorm +
            static_cast<float>(bestDelta) / sessionSamples;
        m_cumShift[static_cast<size_t>(p)] = wouldBe;
        ++nChanged;
    }

    (void)nSkippedRead;  // not fatal; partial commits still valid

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (nChanged > 0 && gpu)
        gpu_upload_data(gpu, Data.m_Data);
#endif

    ++m_timeShiftCallCount;
    return nChanged;
}

// ---------------------------------------------------------------------------
// wilsonHilfertyChi2(df, z) — χ²(df, p) inverse CDF via Wilson-Hilferty.
// Accurate to < 1% for df ≥ 4.  Matches the inline formula used elsewhere
// in this file (e.g. KK.cpp:2811, :3521) for MergeThresh calibration
// warnings.  z is the standard normal quantile corresponding to p:
//   p=0.95   → z=1.6449
//   p=0.99   → z=2.326
//   p=0.9999 → z=3.719
// ---------------------------------------------------------------------------
static inline float wilsonHilfertyChi2(int df, float z)
{
    const float d = static_cast<float>(df);
    const float a = 1.0f - 2.0f / (9.0f * d);
    const float b = z * std::sqrt(2.0f / (9.0f * d));
    return d * std::pow(a + b, 3.0f);
}

// ---------------------------------------------------------------------------
// TimeShiftMergeEvaluate
//
// Build a TimeShiftMergePlan for the victim cluster.  Does NOT commit any
// shifts or reassignments — caller inspects `plan.lossReductionTotal` and
// decides whether the merge becomes acceptable.  Pairs with
// TimeShiftMergeCommit.
//
// Complexity: O(nVictimSpikes × kCand × nChan × data2use) projection work
// plus O(nVictimSpikes × kCand × nDims²) Cholesky forward-sub work.
// I/O: one read per victim spike from .spk (cached via m_timeShiftSpkFp).
// ---------------------------------------------------------------------------
bool KK::TimeShiftMergeEvaluate(
    int victim, int nChan, int nSamplesPerSpike,
    TimeShiftMergePlan& plan)
{
    plan = TimeShiftMergePlan{};
    if (!m_timeShiftReady || (!m_timeShiftSpkMap && !m_timeShiftSpkFp)) return false;
    if (!m_timeShiftBasis.valid())            return false;
    if (victim < 0 || victim >= MaxPossibleClusters) return false;
    if (!ClassAlive[victim])                 return false;

    // Collect victim's spike indices.
    std::vector<int> idxs;
    idxs.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == victim) idxs.push_back(p);
    const int nMem = static_cast<int>(idxs.size());
    if (nMem < 1) return false;

    const int waveSamples = nChan * nSamplesPerSpike;
    const int timeDimIdx  = nDims - 1;
    const int nPCA        = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCA <= 0 || nPCA > nDims - 1) return false;
    const float sessionSamples = timeRawMax - timeRawMin;
    if (!(sessionSamples > 0.0f)) return false;

    const int N     = m_timeShiftBasis.N;
    const int kCand = m_timeShiftBasis.nCand();
    constexpr int kCandMax = 2 * kTimeShiftNmax + 1;

    if (static_cast<int>(m_timeShiftWaveScratch.size()) < waveSamples)
        m_timeShiftWaveScratch.assign(static_cast<size_t>(waveSamples), 0);

    // Per-spike trial features under every candidate.  Laid out
    // [mi][ci][fi] so a committed spike's features are contiguous.
    std::vector<float> trialFeats(
        static_cast<size_t>(nMem) * kCand * nPCA, 0.0f);
    std::vector<uint8_t> okMask(
        static_cast<size_t>(nMem) * kCand, 0);

    const auto& pca    = m_timeShiftBasis;
    const int   data2use = pca.data2use;
    const int   nComp    = pca.nComp;
    const int   nChanPca = pca.nChan;
    const int   rs       = pca.recShift;
    const bool  isCen    = pca.isCentered;

    // --- Project every victim spike under every candidate δ ----------------
    // Identical math to TimeShiftMergeTighten's projection loop,
    // but we save ALL candidates' features per spike (not just compute-and-
    // discard per-candidate Mahal²) so the caller can commit without a
    // second pass.
    int nRead = 0;
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = idxs[mi];
        if (!TimeShiftReadSpikeWave(p, waveSamples,
                                    m_timeShiftWaveScratch.data())) continue;
        // Bring the read waveform to the current cumulative frame — see
        // the matching commentary in TimeShiftMergeTighten.  Without this,
        // any prior cumShift on a victim spike from an earlier alignment
        // pass causes the candidate-shift selection to be biased by the
        // current cumulative shift's magnitude rather than the deviation
        // from current state.
        ShiftWaveformRowInPlace(m_timeShiftWaveScratch.data(), p,
                                nChan, nSamplesPerSpike);

        const int baseCum = m_cumShift[static_cast<size_t>(p)];
        const int16_t* raw = m_timeShiftWaveScratch.data();

        for (int ci = 0; ci < kCand; ++ci)
            okMask[static_cast<size_t>(mi) * kCand + ci] =
                (std::abs(baseCum + (ci - N)) <= m_timeShiftMaxAbs)
                    ? 1u : 0u;

        for (int ch = 0; ch < nChanPca; ++ch) {
            const double* evs[kCandMax];
            const double* mus[kCandMax];
            for (int ci = 0; ci < kCand; ++ci) {
                evs[ci] = pca.eigvecShifted[ci][ch].data();
                mus[ci] = pca.meanShifted  [ci][ch].data();
            }
            for (int k = 0; k < nComp; ++k) {
                double acc[kCandMax] = {0.0};
                for (int j = 0; j < data2use; ++j) {
                    const int s = rs + j;
                    const double rawV = static_cast<double>(raw[s * nChan + ch]);
                    if (isCen) {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j]
                                     * (rawV - mus[ci][j]);
                    } else {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j] * rawV;
                    }
                }
                const int   fi   = ch * nComp + k;
                const float min_ = dimMin_  [fi];
                const float rng_ = dimRange_[fi];
                for (int ci = 0; ci < kCand; ++ci)
                    trialFeats[(static_cast<size_t>(mi) * kCand + ci) * nPCA + fi] =
                        (static_cast<float>(acc[ci]) - min_) * rng_;
            }
        }
        ++nRead;
    }
    if (nRead == 0) return false;

    // --- Group by Class2 destination & evaluate per-destination δ ----------
    // Per destination: for each candidate δ, aggregate Mahalanobis² over
    // the sub-batch.  Apply χ² threshold to accept non-zero δ.
    std::vector<std::vector<int>> byDest(
        static_cast<size_t>(MaxPossibleClusters));
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = idxs[mi];
        const int d = Class2[p];
        if (d > 0 && d < MaxPossibleClusters && ClassAlive[d])
            byDest[static_cast<size_t>(d)].push_back(mi);
    }

    // χ² threshold per spike (95% quantile of each spike's mahal² under
    // the null hypothesis that it came from this destination cluster).
    const float chi2_95 = wilsonHilfertyChi2(nDims, 1.6449f);

    // Output buffers for the plan.
    plan.globalIdx.assign(static_cast<size_t>(nMem), -1);
    plan.chosenCand.assign(static_cast<size_t>(nMem), N);  // default: no shift
    plan.chosenFeats.assign(static_cast<size_t>(nMem) * nPCA, 0.0f);
    plan.chosenTime.assign(static_cast<size_t>(nMem), 0.0f);
    plan.nPCA = nPCA;

    for (int mi = 0; mi < nMem; ++mi) plan.globalIdx[mi] = idxs[mi];

    // Scratch for forward-substitution
    std::vector<float> yVec(static_cast<size_t>(nDims));
    std::vector<float> xMinusMu(static_cast<size_t>(nDims));

    double totalLossReduction = 0.0;
    int    nDestsShifted      = 0;

    for (int dest = 1; dest < MaxPossibleClusters; ++dest) {
        const auto& subIdxs = byDest[dest];
        const int M = static_cast<int>(subIdxs.size());
        if (M < 1) continue;

        const float* destMean = Mean.m_Data      + dest * nDims;
        const float* destChol = cholFlat.data()  + dest * nDims2;

        // Aggregate Mahalanobis² for each candidate δ over this sub-batch.
        double agg[kCandMax];
        for (int ci = 0; ci < kCand; ++ci) agg[ci] = 0.0;
        std::vector<uint8_t> validSpike(M, 1);   // per-spike bad-Chol flag

        for (int im = 0; im < M; ++im) {
            const int mi = subIdxs[im];
            const int p  = idxs[mi];
            const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];
            for (int ci = 0; ci < kCand; ++ci) {
                if (!okMask[static_cast<size_t>(mi) * kCand + ci]) {
                    // Out-of-range candidate: substitute δ=0 Mahal² so this
                    // candidate cannot "win" by exploiting unreachable
                    // shifts.  Infinite is safer but pollutes aggregates;
                    // δ=0 substitution is the same convention the split and
                    // per-spike merge probes use.
                    // (If δ=0 itself is out-of-range — impossible because
                    // baseCum is already bounded — we'd mark invalid.)
                }
                // Build x = trial features ⊕ pass-through spatial dims ⊕ time_δ
                const float* trial = trialFeats.data()
                    + (static_cast<size_t>(mi) * kCand + ci) * nPCA;
                for (int d = 0; d < nPCA; ++d)
                    xMinusMu[d] = trial[d] - destMean[d];
                for (int d = nPCA; d < nDims - 1; ++d)
                    xMinusMu[d] = Data.m_Data[p * nDims + d] - destMean[d];
                const float dt = okMask[static_cast<size_t>(mi) * kCand + ci]
                    ? static_cast<float>(ci - N) / sessionSamples
                    : 0.0f;
                xMinusMu[nDims - 1] = (rawTsNorm + dt) - destMean[nDims - 1];

                // Forward substitution: L·y = (x−μ)
                bool bad = false;
                float mahal2 = 0.0f;
                for (int i = 0; i < nDims; ++i) {
                    float s = xMinusMu[i];
                    for (int j = 0; j < i; ++j)
                        s -= destChol[i * nDims + j] * yVec[j];
                    const float diag = destChol[i * nDims + i];
                    if (!(diag > 0.0f)) { bad = true; break; }
                    yVec[i] = s / diag;
                    mahal2 += yVec[i] * yVec[i];
                }
                if (bad) {
                    validSpike[im] = 0;
                    break;
                }
                agg[ci] += static_cast<double>(mahal2);
            }
        }

        // Skip destinations with any singular Chol slab — data hasn't
        // converged enough for the probe to be meaningful here.
        int validM = 0;
        for (int im = 0; im < M; ++im) validM += validSpike[im];
        if (validM == 0) continue;

        // χ² threshold: a non-zero δ must beat δ=0 by more than
        // chi2_95 × validM to be accepted.
        const int    baselineCand = N;
        const double baselineAgg  = agg[baselineCand];
        int    bestCand = baselineCand;
        double bestAgg  = baselineAgg;
        const double threshold =
            static_cast<double>(chi2_95) * static_cast<double>(validM);
        for (int ci = 0; ci < kCand; ++ci) {
            if (ci == baselineCand) continue;
            if (agg[ci] < bestAgg - threshold) {
                bestAgg  = agg[ci];
                bestCand = ci;
            }
        }

        // Record per-spike commit data for this sub-batch.
        for (int im = 0; im < M; ++im) {
            const int mi = subIdxs[im];
            if (!validSpike[im]) continue;
            plan.chosenCand[mi] = bestCand;
            const float* trial = trialFeats.data()
                + (static_cast<size_t>(mi) * kCand + bestCand) * nPCA;
            std::memcpy(
                plan.chosenFeats.data() + static_cast<size_t>(mi) * nPCA,
                trial, sizeof(float) * nPCA);
            const int p = idxs[mi];
            const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];
            const float dt = okMask[static_cast<size_t>(mi) * kCand + bestCand]
                ? static_cast<float>(bestCand - N) / sessionSamples
                : 0.0f;
            plan.chosenTime[mi] = rawTsNorm + dt;
        }

        // Contribution to total loss reduction (in units of log-probability).
        // LogP uses the convention: higher = worse fit, with the formula
        // 0.5*mahal² + logRootDet - log(Weight) + const.  The δ-dependent
        // part is only 0.5*mahal², so the loss change is:
        //   ΔDeletionLoss(dest) = 0.5 × (bestAgg − baselineAgg)     (≤ 0)
        totalLossReduction += 0.5 * (bestAgg - baselineAgg);
        if (bestCand != baselineCand) ++nDestsShifted;
    }

    plan.valid              = true;
    plan.lossReductionTotal = static_cast<float>(totalLossReduction);

    if (nDestsShifted > 0)
        Output("  [tshift-merge-decision] victim=%d, %d destinations shifted, "
               "ΔDeletionLoss = %.3f\n",
               victim, nDestsShifted, plan.lossReductionTotal);
    return true;
}

// ---------------------------------------------------------------------------
// TimeShiftMergeCommit
// Apply the chosen shifts to Data[] and m_cumShift.  Caller is responsible
// for the subsequent reassignment (Class[p] = Class2[p]) and any follow-up
// post-merge tightening.
// ---------------------------------------------------------------------------
void KK::TimeShiftMergeCommit(const TimeShiftMergePlan& plan)
{
    if (!plan.valid || plan.globalIdx.empty()) return;
    if (plan.nPCA <= 0) return;

    const int timeDimIdx = nDims - 1;
    const int N          = m_timeShiftBasis.N;
    const int nPCA       = plan.nPCA;

    int nWritten = 0;
    const int nMem = static_cast<int>(plan.globalIdx.size());
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = plan.globalIdx[mi];
        if (p < 0 || p >= nPoints) continue;
        const int cand = plan.chosenCand[mi];
        if (cand == N) continue;   // no-shift default; skip

        const int delta   = cand - N;
        const int wouldBe = m_cumShift[p] + delta;
        if (std::abs(wouldBe) > m_timeShiftMaxAbs) continue;

        float* dataRow = Data.m_Data + p * nDims;
        std::memcpy(dataRow,
                    plan.chosenFeats.data() + static_cast<size_t>(mi) * nPCA,
                    sizeof(float) * nPCA);
        dataRow[timeDimIdx] = plan.chosenTime[mi];
        m_cumShift[p] = wouldBe;
        ++nWritten;
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (nWritten > 0 && gpu)
        gpu_upload_data(gpu, Data.m_Data);
#endif
}

// ---------------------------------------------------------------------------
// TimeShiftAlignCluster — in-cluster per-spike alignment
//
// Conceptually replaces canonical xcorr realignment (Phase 1a).  For each
// spike in the given cluster, picks the δ ∈ {-N,…,+N} that minimises its
// Mahalanobis² to the cluster's own Gaussian.  Equivalent to xcorr aligning
// each spike to its cluster mean, but weighted by the cluster's covariance
// structure instead of flat waveform overlap — which gives dimensions with
// high discriminative power (usually low-order PCs) more say.
//
// Implementation is a one-line wrapper over TimeShiftMergeTighten, pointed
// at the cluster's own stats instead of a destination's.  Per-spike scope
// is appropriate here: within-cluster alignment IS a per-spike operation.
// ---------------------------------------------------------------------------
int KK::TimeShiftAlignCluster(int clusterId, int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady)                        return 0;
    if (clusterId <= 0 || clusterId >= MaxPossibleClusters) return 0;
    if (!ClassAlive[clusterId])                    return 0;

    std::vector<int> members;
    members.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == clusterId) members.push_back(p);
    if (static_cast<int>(members.size()) < 2) return 0;

    const float* mean = Mean.m_Data     + clusterId * nDims;
    const float* chol = cholFlat.data() + clusterId * nDims2;

    return TimeShiftMergeTighten(members, nChan, nSamplesPerSpike, mean, chol);
}

// ---------------------------------------------------------------------------
// TimeShiftAlignPhase — Phase 1a driver
//
// Iterates alive clusters (skipping noise, and clusters with < 5 spikes
// that aren't worth the I/O) and calls TimeShiftAlignCluster on each.
// Called from the driver at the slot that canonical Phase 1a xcorr used
// to occupy.  Expects fresh MStep + EStep output (Mean + cholFlat current).
//
// Loops up to TimeShiftAlignIter times.  Each pass:
//   1. Iterate alive clusters, calling TimeShiftAlignCluster on each.
//   2. If any spikes shifted, run MStep so the next pass sees updated
//      cluster means (a spike that moved into cluster A now contributes
//      to A's mean before the next per-spike alignment decision).
//   3. Exit early when a pass produces zero shifts (converged).
//
// Between passes: MStep refreshes Mean/Cov, then a focused Cholesky-only
// refresh recomputes cholFlat from the new Cov.  Skipping the full EStep
// avoids the per-(point, cluster) LogP recompute, which alignment scoring
// doesn't need.  An EStep is still required before the NEXT phase that
// consumes LogP, but that's the caller's responsibility — the chunked-CEM
// driver runs MStep+EStep after Phase 1a in any case.
//
// Note: an earlier version of this function called only MStep() between
// passes and assumed cholFlat would be refreshed too.  It isn't — cholFlat
// lives inside EStep (KK::EStep, ~line 565).  Pass N+1 was therefore
// scoring with updated Mean against pre-shift Cholesky factors, an
// inconsistency that grew with each iteration.  The Cholesky-only refresh
// below mirrors the pattern at the provisional seeding site (~line 3199)
// and keeps Mean / Cov / cholFlat all in sync without paying for LogP.
//
// Returns the cumulative count of spikes whose shifts changed across
// all passes and clusters.
// ---------------------------------------------------------------------------
int KK::TimeShiftAlignPhase(int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady) return 0;
    if (TimeShiftAlignIter <= 0) return 0;

    int totalShifted = 0;

    for (int pass = 0; pass < TimeShiftAlignIter; ++pass) {
        int passShifted = 0;
        int nClustersProcessed = 0;
        for (int c = 1; c < MaxPossibleClusters; ++c) {
            if (!ClassAlive[c]) continue;
            const int shifted = TimeShiftAlignCluster(c, nChan, nSamplesPerSpike);
            if (shifted > 0) ++nClustersProcessed;
            passShifted += shifted;
        }
        totalShifted += passShifted;

        if (passShifted > 0) {
            Output("[Phase 1a] pass %d/%d: %d spikes shifted across "
                   "%d clusters\n",
                   pass + 1, TimeShiftAlignIter, passShifted, nClustersProcessed);
            // Refresh Mean/Cov so the next pass aligns against post-shift
            // centres, then refresh cholFlat from the new Cov.  Without
            // this Cholesky refresh, pass N+1 would score against stale
            // covariance factors.  Singular clusters are deleted in place
            // (matches EStep's behaviour at KK.cpp:565-572).
            MStep();
            for (int cc = 1; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                if (Cholesky(Cov.m_Data + c * nDims2,
                             cholFlat.data() + c * nDims2, nDims)) {
                    if (Verbose >= 2)
                        Output("[Phase 1a] class %d deleted: covariance "
                               "matrix is singular after shifts\n", c);
                    ClassAlive[c] = 0;
                }
            }
            Reindex();
        } else {
            // Converged — no spikes changed shift; further passes can't
            // change anything either.
            if (pass > 0)
                Output("[Phase 1a] converged after %d pass(es)\n", pass + 1);
            break;
        }
    }

    if (totalShifted > 0)
        Output("[Phase 1a] Cluster alignment: %d total spike-shifts\n",
               totalShifted);
    return totalShifted;
}



// ---------------------------------------------------------------------------
// EnergyCOMRealignPhase — per-cluster centre-of-energy realignment
//
// For each alive cluster, computes the MEAN waveform across all spikes
// assigned to that cluster, then the weighted-mean time of the per-sample
// channel-summed energy of that mean waveform:
//
//     mean_x(t, c) = ( 1/N ) · Σ_{p ∈ cluster}  x_p(t, c)
//     e(t)  = Σ_c |mean_x(t, c)|²       (EnergyCOMMetric = 1, default)
//           | Σ_c |mean_x(t, c)|        (EnergyCOMMetric = 0)
//     t_COM = ( Σ_t  t · e(t) ) / ( Σ_t e(t) )
//
// Then computes the integer shift δ_COM = round(t_COM − PeakSampleIndex)
// and adds it to m_cumShift[p] for every spike p in the cluster — the
// SAME delta to all members.  Computing the COM on the cluster mean rather
// than on individual spikes (a) averages out per-spike capture-noise
// (an individual spike's "energy COM" is a poor estimator with high
// variance), and (b) yields a single coherent direction per cluster, so
// the cluster's relative geometry in PCA space is preserved.  Per-spike
// COM realignment, by contrast, scatters cluster members in inconsistent
// directions and disrupts the cluster shape that downstream EM has just
// converged on.
//
// The per-cluster shift is ADDITIVE to whatever a preceding cluster-mean
// alignment (TimeShiftAlignPhase) wrote — the two are complementary:
// cluster-mean handles cluster-fit drift in PCA space, energy-COM handles
// waveform-anchor drift in time.  If applying delta to any cluster
// member would push its cumulative shift past ±m_timeShiftMaxAbs, the
// whole cluster is skipped rather than clamped per-spike (partial shifts
// would break the very uniformity this phase is meant to preserve).
//
// After updating m_cumShift, RefeaturizeFromShifts re-extracts the
// shifted spikes from .fil at the new sample offsets, projects through
// the saved PCA eigenvectors, re-normalises, and writes the new features
// into Data[].  This is the same machinery used by Phase 9
// (TimeShiftFinalize) for the final disk commit, just invoked mid-run.
//
// Reads the ORIGINAL .spk waveform per spike (TimeShiftReadSpikeWave
// does not account for m_cumShift), so the cluster mean is built from
// waveforms as originally captured — independent of any prior shift
// already in m_cumShift.  The additive update means the final shift is
// the SUM of all prior contributions plus the new COM correction, which
// is what Phase 9 / RefeaturizeFromShifts consume.
//
// Cost notes:
//  - O(nPoints · waveSamples) for the gather + accumulate step (one
//    pass over members per cluster; spike read from .spk dominates).
//  - The summed-waveform energy COM uses a double accumulator (sumWave)
//    so int16 sums across thousands of spikes do not overflow.
//  - Energy COM is computed on the SUMMED waveform — equivalent up to a
//    global scale factor (preserved by the t_COM ratio) to computing it
//    on the MEAN; saves the divide-by-N pass.
//
// Returns the total number of spikes whose m_cumShift was updated.
// ---------------------------------------------------------------------------
int KK::EnergyCOMRealignPhase(int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady) return 0;
    if (EnergyCOMRealign == 0) return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0) return 0;
    if (m_cumShift.empty()) return 0;

    const int waveSamples = nChan * nSamplesPerSpike;
    std::vector<int16_t> waveScratch(static_cast<size_t>(waveSamples), 0);
    std::vector<double>  sumWave   (static_cast<size_t>(waveSamples), 0.0);
    std::vector<int>     members;
    members.reserve(1024);

    const double peakRef = static_cast<double>(PeakSampleIndex);
    int nClustersShifted   = 0;
    int nSpikesShifted     = 0;
    int nClustersTooSmall  = 0;
    int nClustersBadRead   = 0;
    int nClustersSilent    = 0;
    int nClustersZeroShift = 0;
    int nClustersClamped   = 0;

    for (int c = 1; c < MaxPossibleClusters; ++c) {
        if (!ClassAlive[c]) continue;

        // Gather members of cluster c.
        members.clear();
        for (int p = 0; p < nPoints; ++p)
            if (Class[p] == c) members.push_back(p);
        if (members.size() < 5) { ++nClustersTooSmall; continue; }

        // Sum the .spk-captured waveforms across all members.  The COM is
        // scale-invariant so we don't need to divide by N — a double
        // accumulator avoids int16 overflow when N is large.
        std::fill(sumWave.begin(), sumWave.end(), 0.0);
        int nRead = 0;
        for (int p : members) {
            if (!TimeShiftReadSpikeWave(p, waveSamples, waveScratch.data())) continue;
            for (int i = 0; i < waveSamples; ++i)
                sumWave[i] += static_cast<double>(waveScratch[i]);
            ++nRead;
        }
        if (nRead == 0) { ++nClustersBadRead; continue; }

        // Energy COM on the summed-waveform — equivalent to MEAN-waveform
        // COM since the ratio cancels the 1/N factor.  This is the energy
        // profile of the cluster's typical waveform shape, with per-spike
        // capture noise averaged out.
        double totalE    = 0.0;
        double weightedT = 0.0;
        for (int t = 0; t < nSamplesPerSpike; ++t) {
            double e = 0.0;
            const double* row = sumWave.data() + t * nChan;
            if (EnergyCOMMetric == 0) {
                for (int ch = 0; ch < nChan; ++ch) e += std::fabs(row[ch]);
            } else {
                for (int ch = 0; ch < nChan; ++ch) {
                    const double x = row[ch];
                    e += x * x;
                }
            }
            totalE    += e;
            weightedT += static_cast<double>(t) * e;
        }
        if (!(totalE > 0.0)) { ++nClustersSilent; continue; }

        const double tCOM  = weightedT / totalE;
        const int    delta = static_cast<int>(std::lround(tCOM - peakRef));
        if (delta == 0) { ++nClustersZeroShift; continue; }

        // Cap check across all members: if applying delta would push ANY
        // member's cumulative shift past the cap, skip the whole cluster.
        // Clamping only the over-cap members would scatter the cluster
        // (some spikes shifted by delta, others by less), defeating the
        // per-cluster-uniform design.
        bool anyExceeds = false;
        for (int p : members) {
            const int wouldBe = m_cumShift[static_cast<size_t>(p)] + delta;
            if (std::abs(wouldBe) > m_timeShiftMaxAbs) { anyExceeds = true; break; }
        }
        if (anyExceeds) { ++nClustersClamped; continue; }

        // Apply the SAME delta to every spike in the cluster.
        for (int p : members)
            m_cumShift[static_cast<size_t>(p)] += delta;

        ++nClustersShifted;
        nSpikesShifted += static_cast<int>(members.size());
    }

    Output("[EnergyCOM] %d clusters realigned (%d spikes total; peak ref = "
           "sample %d, cap = ±%d); skipped: %d small, %d bad-read, %d silent, "
           "%d zero-shift, %d cap-clamped\n",
           nClustersShifted, nSpikesShifted, PeakSampleIndex,
           m_timeShiftMaxAbs, nClustersTooSmall, nClustersBadRead,
           nClustersSilent, nClustersZeroShift, nClustersClamped);

    if (nSpikesShifted > 0) {
        // Re-extract + re-featurize all currently-shifted spikes from .fil
        // at their NEW (cumulative) sample offsets, projecting through the
        // saved PCA eigenvectors and writing fresh features into Data[].
        RefeaturizeFromShifts(m_cumShift, nChan, nSamplesPerSpike);
    }

    return nSpikesShifted;
}

// ---------------------------------------------------------------------------
// KlustersStyleRealignAllClusters — Phase 7c
//
// Klusters-faithful per-spike realignment.  See KK.h for the algorithmic
// description.  This is the same algorithm that powers klusters' interactive
// "Realign top-ch" button (src/klusters/src/spikerealign.cpp); KKE reuses
// the same XcorrDispatch library and just supplies its own per-cluster
// spike lists and waveform reader.
//
// Per-cluster cost (N = cluster size, C = nChan, S = nSamples, M = maxShift):
//   Mean build      O(N·C·S)            CPU memcpy + accumulate
//   Pre-align       O(C·S)              CPU
//   xcorr           O(N·C·S·M)          GPU when available, OpenMP otherwise
//
// Whole-session cost on the user's sirotaA-jg-000005 data (189k spikes, 8
// channels, 32 samples, maxShift=8) is ~1-2 seconds end-to-end on CPU and
// well under 1 second on the RTX 5070 Ti.
//
// Sign convention (matches klusters spikerealign.cpp:669):
//   m_cumShift[p] += newShift  →  re-extraction reads from .fil starting
//   `newShift` samples LATER (or earlier when newShift is negative).
//
// Caller is responsible for:
//   • Running MStep + EStep beforehand if cluster means need to be fresh
//     (this function uses the .spk content, not Data[], so the cluster mean
//     is computed from the raw waveform — independent of stale Class[]
//     assignments).
//   • Calling RefeaturizeFromShifts (via TimeShiftFinalize) afterward to
//     turn the m_cumShift updates into actual .spk/.fet writes.
// ---------------------------------------------------------------------------
int KK::KlustersStyleRealignAllClusters(int nChan, int nSamplesPerSpike)
{
    if (nChan <= 0 || nSamplesPerSpike <= 0) return 0;
    if (!m_timeShiftReady) {
        // No .spk probe initialised — without it we can't read waveforms.
        // Skip silently (caller decides whether to warn).
        return 0;
    }
    if (PeakSampleIndex < 0 || PeakSampleIndex >= nSamplesPerSpike) {
        LockedStderr(
                "[Phase 7c] PeakSampleIndex=%d out of [0,%d) — skipping "
                "klusters-style realignment.\n",
                PeakSampleIndex, nSamplesPerSpike);
        return 0;
    }

    const int waveSamples = nChan * nSamplesPerSpike;
    const int peakPos     = PeakSampleIndex;
    const int maxShift    = std::max(1, std::min(nSamplesPerSpike / 4,
                                                  KlustersRealignMaxShift));
    const int minSize     = std::max(2, KlustersRealignMinSize);

    // Group spike global indices by cluster ID.  Using nClustersAlive +
    // AliveIndex[] gives us live clusters only; the noise cluster (id 0)
    // is skipped explicitly.
    std::vector<std::vector<int>> clusterSpikes(MaxPossibleClusters);
    for (int p = 0; p < nPoints; ++p) {
        const int c = Class[p];
        if (c <= 0 || c >= MaxPossibleClusters) continue;  // noise / OOB
        clusterSpikes[static_cast<size_t>(c)].push_back(p);
    }

    KlustersRealign::RealignStats stats;
    long long sumAbsShift = 0;
    int       nSpikesChanged = 0;

    // Scratch buffer reused across clusters — sized to the largest cluster.
    std::vector<int16_t> waveBuf;       // flat: nSpikes × waveSamples
    std::vector<int>     shifts;
    std::vector<float>   scores;

    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const int c = AliveIndex[cc];
        if (c <= 0 || c >= MaxPossibleClusters) continue;  // skip noise
        const auto& pts = clusterSpikes[static_cast<size_t>(c)];
        const int N     = static_cast<int>(pts.size());

        if (N < minSize) { ++stats.nClustersSkipped; continue; }

        // ── Read all N spikes' waveforms from .spk (or .spkD) ─────────
        waveBuf.assign(static_cast<size_t>(N) * waveSamples, 0);
        int nReadOk = 0;
        for (int i = 0; i < N; ++i) {
            int16_t* dst = waveBuf.data() +
                           static_cast<ptrdiff_t>(i) * waveSamples;
            if (TimeShiftReadSpikeWave(pts[static_cast<size_t>(i)],
                                       waveSamples, dst)) {
                ++nReadOk;
            } else {
                // Zero-fill the failed slot; it will contribute zero to the
                // mean and the xcorr will return shift=0 for it.
                std::memset(dst, 0,
                            static_cast<size_t>(waveSamples) *
                            sizeof(int16_t));
                ++stats.nSpikesReadFailed;
            }
        }
        if (nReadOk < minSize) {
            ++stats.nClustersSkipped;
            continue;
        }

        // ── Compute per-spike shifts via XcorrDispatch ────────────────
        const bool ok = KlustersRealign::ComputeClusterShiftsFlat(
            waveBuf.data(), N,
            nChan, nSamplesPerSpike, peakPos, maxShift,
            shifts, scores);
        if (!ok) { ++stats.nClustersSkipped; continue; }

        // ── Commit shifts to m_cumShift, clamping to maxAbs.
        //    Klusters writes back to .fil immediately; here we let
        //    TimeShiftFinalize handle that at the end of the run.
        for (int i = 0; i < N; ++i) {
            const int p  = pts[static_cast<size_t>(i)];
            const int sh = shifts[static_cast<size_t>(i)];
            if (sh == 0) continue;

            const int oldCum = m_cumShift[static_cast<size_t>(p)];
            int       newCum = oldCum + sh;
            if (newCum >  m_timeShiftMaxAbs) newCum =  m_timeShiftMaxAbs;
            if (newCum < -m_timeShiftMaxAbs) newCum = -m_timeShiftMaxAbs;
            if (newCum == oldCum) continue;  // clamped out

            m_cumShift[static_cast<size_t>(p)] = newCum;
            ++nSpikesChanged;
            sumAbsShift += std::abs(sh);
            stats.maxAbsShift = std::max(stats.maxAbsShift, std::abs(sh));
        }

        stats.nSpikesEvaluated += N;
        stats.nSpikesRealigned += static_cast<int>(
            std::count_if(shifts.begin(), shifts.end(),
                          [](int s){ return s != 0; }));
        ++stats.nClustersProcessed;
    }

    stats.meanAbsShift = stats.nSpikesEvaluated > 0
        ? static_cast<double>(sumAbsShift) / stats.nSpikesEvaluated
        : 0.0;

    LockedStderr(
            "[Phase 7c] KlustersStyle realignment: "
            "%d clusters processed (%d skipped), "
            "%d/%d spikes shifted, "
            "mean|Δ|=%.2f samples (max %d), "
            "%d read failures.\n",
            stats.nClustersProcessed, stats.nClustersSkipped,
            stats.nSpikesRealigned, stats.nSpikesEvaluated,
            stats.meanAbsShift, stats.maxAbsShift,
            stats.nSpikesReadFailed);

    return nSpikesChanged;
}

// ---------------------------------------------------------------------------
// FinalMeanSubtractionMerge — Phase 7b
//
// Pairwise merge of live global clusters based on the normalised L2
// residual between their mean spike waveforms:
//
//     D(i, j) = ||mean[i] - mean[j]||² / max(||mean[i]||², ||mean[j]||²)
//
// Mean waveforms are aggregated across all chunks (i.e. spike index
// 0..nPoints-1 walks the whole session's spikes), using either the
// time-shift probe's open .spk file when available (so the means
// reflect the post-alignment view) or a direct fopen on the canonical
// .spk / .spkD path when the probe never initialised.  Self-contained
// — does NOT require m_timeShiftReady, unlike EnergyCOMRealignPhase.
//
// Pairs below MeanSubtractionMergeThresh are merged via union-find,
// smallest D first.  Smaller cluster ID always wins (keeps the palette
// layout stable; chronological-order-by-ID gets preserved).  No
// iteration: union-find already handles transitive merges in one pass.
//
// Caller is responsible for the post-merge state refresh (MStep /
// Reindex / EStep / ComputeScore) — same convention as Phase 7a's
// TimeShiftAlignPhase.
// ---------------------------------------------------------------------------
int KK::FinalMeanSubtractionMerge(int nChan, int nSamplesPerSpike)
{
    if (MeanSubtractionMergeEnable == 0) return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0) return 0;
    if (!(MeanSubtractionMergeThresh > 0.0f)) return 0;

    const int wElems = nChan * nSamplesPerSpike;

    // ── Spike-file access ──────────────────────────────────────────────
    // Prefer the time-shift probe's open file (already mmap'd / fopen'd,
    // and applying any cumulative shifts via TimeShiftReadSpikeWave so
    // means reflect post-alignment geometry).  Fall back to a direct
    // open of .spk / .spkD when the probe isn't initialised.
    FILE* spkFallback = nullptr;
    if (!m_timeShiftReady) {
        char spkPath[STRLEN + 16];
        if (pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo) < 0) {
            LockedStderr( "[Phase 6b] No .spk/.spkD found for electrode %d; "
                            "skip mean-subtraction merge.\n", ElecNo);
            return 0;
        }
        spkFallback = fopen(spkPath, "rb");
        if (!spkFallback) {
            LockedStderr( "[Phase 6b] Could not open %s; "
                            "skip mean-subtraction merge.\n", spkPath);
            return 0;
        }
    }

    // ── Step 1: per-cluster mean waveform aggregation ──────────────────
    // Layout matches EnergyCOMRealignPhase's sumWave: time-major,
    // channel-minor (index = t * nChan + ch).  Sample-major is the
    // natural .spk layout; for an L2 residual the layout doesn't
    // matter as long as both means use the same indexing.
    constexpr int   kMinSpikesForMean = 5;
    std::vector<int16_t> row(static_cast<size_t>(wElems));
    std::vector<int>     liveClusters;
    std::unordered_map<int, std::vector<double>> meanWav;
    std::unordered_map<int, int>                 meanN;
    int nSkippedSmall   = 0;
    int nSkippedBadRead = 0;

    for (int c = 2; c < MaxPossibleClusters; ++c) {
        if (!ClassAlive[c]) continue;

        // Gather members.  Could O(N) once into a multimap but each
        // cluster's loop is short and we want to skip too-small ones
        // before allocating their accumulator.
        std::vector<int> members;
        for (int p = 0; p < nPoints; ++p)
            if (Class[p] == c) members.push_back(p);
        if (static_cast<int>(members.size()) < kMinSpikesForMean) {
            ++nSkippedSmall;
            continue;
        }

        std::vector<double> acc(static_cast<size_t>(wElems), 0.0);
        int nRead = 0;
        for (int p : members) {
            bool ok = false;
            if (m_timeShiftReady) {
                ok = TimeShiftReadSpikeWave(p, wElems, row.data());
            } else {
                // Fallback path: raw .spk read, then apply any shifts
                // we know about.  m_cumShift may still be populated even
                // if m_timeShiftReady is false (probe deinitialised
                // post-Phase-7), so ShiftWaveformRowInPlace is correct.
                if (fseeko(spkFallback,
                           static_cast<off_t>(p) * wElems * sizeof(int16_t),
                           SEEK_SET) == 0 &&
                    fread(row.data(), sizeof(int16_t),
                          static_cast<size_t>(wElems), spkFallback)
                        == static_cast<size_t>(wElems))
                {
                    ShiftWaveformRowInPlace(row.data(), p,
                                            nChan, nSamplesPerSpike);
                    ok = true;
                }
            }
            if (!ok) continue;
            for (int i = 0; i < wElems; ++i)
                acc[static_cast<size_t>(i)] +=
                    static_cast<double>(row[static_cast<size_t>(i)]);
            ++nRead;
        }
        if (nRead == 0) { ++nSkippedBadRead; continue; }

        const double inv = 1.0 / nRead;
        for (int i = 0; i < wElems; ++i) acc[static_cast<size_t>(i)] *= inv;

        liveClusters.push_back(c);
        meanWav[c] = std::move(acc);
        meanN[c]   = nRead;
    }
    if (spkFallback) fclose(spkFallback);

    if (liveClusters.size() < 2) {
        LockedStderr( "[Phase 6b] Mean-subtraction merge: only %zu eligible "
                        "clusters (skipped %d small, %d bad-read); nothing to do.\n",
                liveClusters.size(), nSkippedSmall, nSkippedBadRead);
        return 0;
    }

    // ── Step 2: per-cluster energy (denominator for normalisation) ─────
    std::unordered_map<int, double> energy;
    for (int c : liveClusters) {
        double e = 0.0;
        for (double v : meanWav[c]) e += v * v;
        energy[c] = e;
    }

    // ── Step 3: pairwise residual under cyclic time shifts ─────────────
    //
    // For each pair (i, j), the residual is computed at every cyclic
    // time-shift τ ∈ [−K, K] on mean[i] and the minimum is taken:
    //
    //     D(i, j) = min over τ of  ||shift_τ(m_i) - m_j||² / max(||m_i||², ||m_j||²)
    //
    // where shift_τ wraps the time axis (m[t * nChan + ch] →
    // m[((t + τ) mod nSamp) * nChan + ch]).  Two reasons cyclic is
    // safe here despite physical waveforms not being periodic: (1)
    // captured spike windows have near-zero amplitude at both edges
    // (baseline) since the peak sits at PeakSampleIndex, so wrap-around
    // moves baseline-on-baseline and contributes ≈0 to the residual;
    // (2) the search radius K is bounded by MeanSubtractionMergeMaxShift
    // (default 3) on a typical 32-42 sample window — ≤ 10% of the
    // window wraps, well outside the peak region.
    //
    // Single-shift (no sweep) comparison was misleading: cluster-mean
    // alignment converges to a "good enough" shift per cluster, but the
    // residual between two clusters' means can still have a 1-2 sample
    // misalignment that single-shift D would penalise as if the
    // waveforms differed in shape.  Min-over-shifts removes that.
    const int nSamp = nSamplesPerSpike;
    int maxShift = MeanSubtractionMergeMaxShift;
    if (maxShift < 0)              maxShift = 0;
    if (maxShift > nSamp / 2)      maxShift = nSamp / 2;  // hard cap

    struct Pair { int i; int j; double D; int bestShift; };
    std::vector<Pair> candidates;
    std::sort(liveClusters.begin(), liveClusters.end());

    for (size_t a = 0; a < liveClusters.size(); ++a) {
        const int    i  = liveClusters[a];
        const auto&  mi = meanWav[i];
        const double ei = energy[i];
        for (size_t b = a + 1; b < liveClusters.size(); ++b) {
            const int    j  = liveClusters[b];
            const auto&  mj = meanWav[j];
            const double ej = energy[j];
            const double denom = std::max(ei, ej);
            if (!(denom > 0.0)) continue;       // silent vs anything

            double bestD     = std::numeric_limits<double>::infinity();
            int    bestShift = 0;

            for (int tau = -maxShift; tau <= maxShift; ++tau) {
                double rss = 0.0;
                // Inner loops: for each (t, ch), pull shifted mi sample
                // and compare to mj at the same (t, ch).  Layout is
                // time-major channel-minor (idx = t * nChan + ch),
                // matching the aggregation step above.
                for (int t = 0; t < nSamp; ++t) {
                    // Cyclic source row: (t + tau) wrapped into [0, nSamp).
                    int tSrc = t + tau;
                    while (tSrc <    0)     tSrc += nSamp;
                    while (tSrc >= nSamp)   tSrc -= nSamp;
                    const int rowSrc = tSrc * nChan;
                    const int rowDst = t    * nChan;
                    for (int ch = 0; ch < nChan; ++ch) {
                        const double d =
                            mi[static_cast<size_t>(rowSrc + ch)] -
                            mj[static_cast<size_t>(rowDst + ch)];
                        rss += d * d;
                    }
                }
                const double D = rss / denom;
                if (D < bestD) { bestD = D; bestShift = tau; }
            }

            if (bestD < static_cast<double>(MeanSubtractionMergeThresh))
                candidates.push_back({i, j, bestD, bestShift});
        }
    }

    if (candidates.empty()) {
        LockedStderr( "[Phase 6b] Mean-subtraction merge: 0 pairs below "
                        "D=%.3f (%zu eligible clusters compared with "
                        "cyclic-shift search ±%d); no merges.\n",
                static_cast<double>(MeanSubtractionMergeThresh),
                liveClusters.size(), maxShift);
        return 0;
    }

    // ── Step 4: union-find merge (smallest D first) ────────────────────
    std::sort(candidates.begin(), candidates.end(),
              [](const Pair& a, const Pair& b) { return a.D < b.D; });

    std::unordered_map<int, int> parent;
    for (int c : liveClusters) parent[c] = c;
    auto findRoot = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };

    int nMerged = 0;
    for (const auto& p : candidates) {
        const int ri = findRoot(p.i);
        const int rj = findRoot(p.j);
        if (ri == rj) continue;
        // Smaller cluster ID always wins — palette stability, and the
        // surviving root represents the chronologically-earlier cluster
        // (chunk-merge order assigns lower IDs to earlier-detected units).
        const int keep = std::min(ri, rj);
        const int drop = std::max(ri, rj);
        parent[drop] = keep;
        LockedStderr( "[Phase 6b]   merge cluster %d <- %d  (D=%.4f at τ=%+d, "
                        "n=%d+%d=%d)\n",
                keep, drop, p.D, p.bestShift,
                meanN[keep], meanN[drop],
                meanN[keep] + meanN[drop]);
        ++nMerged;
    }

    // ── Step 5: apply resolved roots to Class[] + kill merged-away ─────
    for (int p = 0; p < nPoints; ++p) {
        const int c = Class[p];
        if (c < 2) continue;
        auto it = parent.find(c);
        if (it == parent.end()) continue;
        const int root = findRoot(c);
        if (root != c) Class[p] = root;
    }
    for (const auto& kv : parent) {
        if (kv.first != kv.second) ClassAlive[kv.first] = 0;
    }

    LockedStderr( "[Phase 6b] Mean-subtraction merge: %d merges applied "
                    "(%zu candidates below D=%.3f under cyclic-shift ±%d, "
                    "%zu clusters evaluated; skipped %d small, %d bad-read).\n",
            nMerged, candidates.size(),
            static_cast<double>(MeanSubtractionMergeThresh),
            maxShift,
            liveClusters.size(), nSkippedSmall, nSkippedBadRead);

    return nMerged;
}

// ---------------------------------------------------------------------------
// RunAlignmentBlock — per-phase-boundary alignment wrapper
//
// Invoked at each insertion site (after Phase 1, Phase 1b, Phase 2,
// Phase 2b, Phase 5, Phase 6) when the corresponding
// TimeShiftAlignAfterPhase* flag is set.  Runs cluster-mean alignment
// via TimeShiftAlignPhase, then (if -EnergyCOMRealign 1) the energy-COM
// pass via EnergyCOMRealignPhase.  Banners use the supplied label.
//
// Intentionally minimal — no MStep / EStep / ComputeScore here.  The
// existing convention (matching the original Phase 1a call site at the
// pre-patch line 3368) is that the next phase's own setup refreshes
// global state.  The dedicated Phase 6a site does its own
// ComputeScore + ReportClusterQuality because it's the LAST
// alignment of the run and the final score / quality dump consumes
// the refreshed numbers.
// ---------------------------------------------------------------------------
void KK::RunAlignmentBlock(int enableFlag, const char* phaseLabel)
{
    if (!enableFlag || !m_timeShiftReady) return;
    if (NbChannels <= 0 || NbSamplesPerSpike <= 0) return;

    LockedStderr( "[%s] Cluster-mean alignment\n", phaseLabel);
    (void)TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);

    if (EnergyCOMRealign != 0) {
        LockedStderr( "[%s] Energy-COM realignment\n", phaseLabel);
        (void)EnergyCOMRealignPhase(NbChannels, NbSamplesPerSpike);
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

// Helper: nth-percentile of a small double vector (sort-based, O(n log n)).
// For the bloat gate we call this once per alive cluster; the sort dominates.
static double percentile_sorted(std::vector<double>& v, double q)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = q * (v.size() - 1);
    const int    lo  = static_cast<int>(std::floor(idx));
    const int    hi  = static_cast<int>(std::ceil (idx));
    const double f   = idx - lo;
    return v[lo] * (1.0 - f) + v[hi] * f;
}


// ---------------------------------------------------------------------------
// DipSplitAttemptEx — try to split a single cluster.
//
// Returns true when the cluster was split and Class[] updated.  Caller is
// responsible for follow-up MStep+EStep to refresh cluster stats.
//
// The `reason_out` parameter receives a tag describing what happened: one
// of "split", "small", "not_bloated", "no_valley", "small_child",
// "bic_worse", "no_free_id".  Used by DipSplitPhase for summary logging.
// ---------------------------------------------------------------------------
bool KK::DipSplitAttemptEx(int clusterId, const char*& reason_out)
{
    reason_out = "skip";
    if (clusterId <= 0 || clusterId >= MaxPossibleClusters) return false;
    if (!ClassAlive[clusterId])                             return false;

    // ── Collect cluster members ──────────────────────────────────────────
    std::vector<int> members;
    members.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == clusterId) members.push_back(p);
    const int M = static_cast<int>(members.size());
    if (M < DipSplitMinSize * 2) { reason_out = "small"; return false; }

    // ── Gate A: bloat ────────────────────────────────────────────────────
    // mahal²(p, c) = 2·(LogP[c][p] − baseScore_c).  We drop baseScore_c from
    // the comparison because we only need the per-point excess for the 90th-
    // percentile, and baseScore_c is constant across members of cluster c.
    //
    // For a true single-Gaussian cluster this is calibrated to χ²(d, 0.9).
    // For a bimodal cluster fitted as one Gaussian, this *should* inflate
    // — but if CEM has been generous with the covariance to absorb the
    // separation, it can sit just under the χ² target.  Gate A' below
    // catches that case from the eigenvalue side.
    std::vector<double> mahal2(M);
    for (int i = 0; i < M; ++i) {
        const int p = members[i];
        mahal2[i] = 2.0 * static_cast<double>(
            LogP.m_Data[static_cast<size_t>(clusterId) * nPoints + p]);
    }
    // Subtract the within-cluster minimum so values are comparable to the
    // null χ²(d) distribution (the additive constant from baseScore_c cancels).
    const double mmin = *std::min_element(mahal2.begin(), mahal2.end());
    for (double& v : mahal2) v -= mmin;

    const double mahal2_p90 = percentile_sorted(mahal2, 0.90);
    // χ²(d, 0.9) via Wilson-Hilferty.  z for p=0.9 is 1.2816.
    const double d_  = static_cast<double>(nDims);
    const double a_wh = 1.0 - 2.0 / (9.0 * d_);
    const double b_wh = 1.2816 * std::sqrt(2.0 / (9.0 * d_));
    const double chi2_90 = d_ * std::pow(a_wh + b_wh, 3.0);
    const bool bloat_pass = (mahal2_p90 >= DipSplitBloatFactor * chi2_90);

    // ── Collect member feature vectors for PCA + dip + k-means ───────────
    // We do NOT use the time dim (last column) — it's a normalized timestamp
    // that shouldn't drive bimodality decisions.
    const int dPCA = nDims - 1;
    std::vector<float> Xmem(static_cast<size_t>(M) * dPCA);
    for (int i = 0; i < M; ++i) {
        const int p = members[i];
        for (int j = 0; j < dPCA; ++j)
            Xmem[static_cast<size_t>(i) * dPCA + j] =
                Data.m_Data[p * nDims + j];
    }

    // ── Compute top-3 PCs WITH eigenvalues ───────────────────────────────
    // Eigenvalues drive Gate A' (elongation) below.  Cost is ~free over the
    // PCs-only call: same covariance build, same iterations, plus one d²
    // Rayleigh quotient per converged PC.
    constexpr int kPCA = 3;
    std::vector<double> pcs(static_cast<size_t>(kPCA) * dPCA, 0.0);
    std::vector<double> eigs(kPCA, 0.0);
    dipsplit::top_pcs_with_eigenvalues(Xmem.data(), M, dPCA, kPCA,
                                        pcs.data(), eigs.data());

    // ── Gate A': elongation (covariance-eccentricity) ────────────────────
    // A unimodal Gaussian's top-3 eigenvalues are all of similar size (a
    // ratio of ~2-3 between top1 and the others is normal for spike feature
    // spaces where a few channels typically dominate).  Two well-separated
    // sub-modes fitted as one Gaussian show a much larger ratio: the inter-
    // mode separation contributes (D/2)² to the variance along that axis,
    // dwarfing the within-mode spread.  Threshold 4.0 is a conservative
    // pick — it lets bloat handle most cases and only kicks in for clearly
    // elongated covariances.
    //
    // The metric is eig_top1 / median(eig_top1..3).  Median (not mean) so
    // an enormous top eigenvalue doesn't drag its own reference up.  And
    // eig_top1 / median is more stable than eig_top1 / eig_top3 — the
    // latter can be tiny if the cluster is genuinely degenerate (rank-2).
    bool elong_pass = false;
    double elong_ratio = 0.0;
    if (DipSplitElongationFactor > 0.0f) {
        std::vector<double> sorted_eigs = eigs;
        std::sort(sorted_eigs.begin(), sorted_eigs.end());  // ascending
        const double eig_med = sorted_eigs[kPCA / 2];       // kPCA=3 → idx 1 = median
        const double eig_top = sorted_eigs.back();
        if (eig_med > 0.0) {
            elong_ratio = eig_top / eig_med;
            elong_pass  = (elong_ratio >= DipSplitElongationFactor);
        }
    }

    // Either gate is sufficient — bloated OR elongated triggers evaluation.
    if (!bloat_pass && !elong_pass) {
        reason_out = "not_bloated";   // legacy reason name; covers both gates
        return false;
    }

    // Compute cluster centroid (for projection centering).
    std::vector<double> centroid(dPCA, 0.0);
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < dPCA; ++j)
            centroid[j] += Xmem[static_cast<size_t>(i) * dPCA + j];
    for (int j = 0; j < dPCA; ++j) centroid[j] /= M;

    // ── Gate B: dip test on subdimensional projection ────────────────────
    //
    // Two modes (selected by -DipSplit2D):
    //
    // Mode 0 (default): test each top-K PC's 1D projection individually.
    // Fast, catches bimodality aligned with any single eigenvector.  Can
    // miss diagonal discriminants where neither PC1 nor PC2 alone shows
    // a valley but their joint distribution does.
    //
    // Mode 1: project to (PC1, PC2) plane, scan directions θ ∈ [0, π) at
    // 5° resolution (36 angles), run the 1D valley test on each direction,
    // keep the deepest valley.  Catches diagonal discriminants — the
    // optimal axis for separating two modes is the line between their
    // centroids, which the scan finds without knowing the centroids a
    // priori.  PC3+ is dropped under the assumption that real bimodality
    // shows up in the top 2 variance directions; if you suspect PC3+
    // bimodality on your data, stay in mode 0.
    int    best_pc     = -1;
    double best_depth  = 0.0;
    double best_valley = 0.0;
    double best_theta_deg = 0.0;       // mode 1 only
    bool   best_is_2d  = false;
    std::vector<double> best_projection;

    if (DipSplit2D != 0 && kPCA >= 2) {
        // Pre-compute PC1 and PC2 scores once for all spikes.
        std::vector<double> s1(static_cast<size_t>(M));
        std::vector<double> s2(static_cast<size_t>(M));
        for (int i = 0; i < M; ++i) {
            double a1 = 0.0, a2 = 0.0;
            for (int j = 0; j < dPCA; ++j) {
                const double v = Xmem[static_cast<size_t>(i) * dPCA + j]
                               - centroid[static_cast<size_t>(j)];
                a1 += v * pcs[0 * dPCA + j];
                a2 += v * pcs[1 * dPCA + j];
            }
            s1[static_cast<size_t>(i)] = a1;
            s2[static_cast<size_t>(i)] = a2;
        }

        // Directional scan over θ ∈ [0, π) at 5° resolution.  Dip statistic
        // is direction-symmetric (dip(θ) == dip(θ+π)) so half-circle suffices.
        constexpr int nDir = 36;
        std::vector<double> proj(static_cast<size_t>(M));
        for (int k = 0; k < nDir; ++k) {
            const double theta = M_PI * static_cast<double>(k) / nDir;
            const double c  = std::cos(theta);
            const double sn = std::sin(theta);
            for (int i = 0; i < M; ++i)
                proj[static_cast<size_t>(i)] =
                    c * s1[static_cast<size_t>(i)] + sn * s2[static_cast<size_t>(i)];
            const dipsplit::ValleyResult vr = dipsplit::valley_test(
                proj.data(), M, DipSplitValleyThresh);
            if (vr.depth > best_depth) {
                best_pc         = 0;                       // sentinel; "2D-PC12"
                best_depth      = vr.depth;
                best_valley     = vr.valley_loc;
                best_theta_deg  = theta * 180.0 / M_PI;
                best_is_2d      = true;
                best_projection = proj;                    // copy
            }
        }
    } else {
        // Per-PC 1D test (mode 0, current default).
        for (int pc = 0; pc < kPCA; ++pc) {
            const double* u = pcs.data() + pc * dPCA;
            std::vector<double> proj(M);
            for (int i = 0; i < M; ++i) {
                double s = 0.0;
                for (int j = 0; j < dPCA; ++j)
                    s += (Xmem[static_cast<size_t>(i) * dPCA + j] - centroid[j])
                         * u[j];
                proj[i] = s;
            }
            const dipsplit::ValleyResult vr = dipsplit::valley_test(
                proj.data(), M, DipSplitValleyThresh);
            if (vr.depth > best_depth) {
                best_pc         = pc;
                best_depth      = vr.depth;
                best_valley     = vr.valley_loc;
                best_projection = std::move(proj);
            }
        }
    }
    if (best_pc < 0 || best_depth < DipSplitValleyThresh) {
        reason_out = "no_valley";
        return false;
    }

    // ── Seed k=2 partition at the valley ─────────────────────────────────
    std::vector<int> labels(M, 0);
    for (int i = 0; i < M; ++i)
        labels[i] = (best_projection[i] >= best_valley) ? 1 : 0;

    // Compute initial centroids from the valley partition.
    std::vector<double> c0(dPCA, 0.0), c1(dPCA, 0.0);
    int n0 = 0, n1 = 0;
    for (int i = 0; i < M; ++i) {
        const float* row = Xmem.data() + i * dPCA;
        if (labels[i] == 0) {
            for (int j = 0; j < dPCA; ++j) c0[j] += row[j];
            ++n0;
        } else {
            for (int j = 0; j < dPCA; ++j) c1[j] += row[j];
            ++n1;
        }
    }
    if (n0 < DipSplitMinSize || n1 < DipSplitMinSize) {
        reason_out = "small_child";
        return false;
    }
    for (int j = 0; j < dPCA; ++j) { c0[j] /= n0; c1[j] /= n1; }

    // ── Refine with k-means k=2 ──────────────────────────────────────────
    dipsplit::kmeans2_refine(Xmem.data(), M, dPCA, c0.data(), c1.data(),
                             labels.data(), /*max_iters=*/20);

    // Recount post-refine
    n0 = n1 = 0;
    for (int i = 0; i < M; ++i) {
        if (labels[i] == 0) ++n0; else ++n1;
    }
    if (n0 < DipSplitMinSize || n1 < DipSplitMinSize) {
        reason_out = "small_child";
        return false;
    }

    // ── BIC gate ─────────────────────────────────────────────────────────
    const dipsplit::BicPair bp = dipsplit::bic_two_vs_one(
        Xmem.data(), M, dPCA, labels.data());
    if (!(bp.bic_k2 < bp.bic_k1)) {
        reason_out = "bic_worse";
        return false;
    }

    // ── Allocate a new cluster ID for the right half ──────────────────────
    int newId = -1;
    for (int c = 1; c < MaxPossibleClusters; ++c) {
        if (!ClassAlive[c]) { newId = c; break; }
    }
    if (newId < 0) { reason_out = "no_free_id"; return false; }

    // ── Commit split: relabel the right-half members ─────────────────────
    ClassAlive[newId] = 1;
    ++nClustersAlive;
    for (int i = 0; i < M; ++i) {
        if (labels[i] == 1) Class[members[i]] = newId;
    }

    if (best_is_2d) {
        Output("  [dipsplit] cluster %d → %d+%d  PC12@%.0f° depth=%.3f  "
               "mahal²₉₀=%.1f vs χ²₉₀=%.1f  elong=%.2fx  ΔBIC=%.1f  gate=%s\n",
               clusterId, n0, n1, best_theta_deg, best_depth,
               mahal2_p90, chi2_90, elong_ratio, bp.bic_k1 - bp.bic_k2,
               bloat_pass ? (elong_pass ? "both" : "bloat") : "elong");
    } else {
        Output("  [dipsplit] cluster %d → %d+%d  PC%d depth=%.3f  "
               "mahal²₉₀=%.1f vs χ²₉₀=%.1f  elong=%.2fx  ΔBIC=%.1f  gate=%s\n",
               clusterId, n0, n1, best_pc, best_depth,
               mahal2_p90, chi2_90, elong_ratio, bp.bic_k1 - bp.bic_k2,
               bloat_pass ? (elong_pass ? "both" : "bloat") : "elong");
    }
    reason_out = "split";
    return true;
}

// ---------------------------------------------------------------------------
// DipSplitPhase — iterate alive clusters, attempt dip-split on each.
//
// Always prints a summary line so users can see the phase ran and what
// gates rejected clusters.  Runs MStep+EStep at the end if any split was
// accepted, so cluster stats and LogP[] are fresh for the next phase.
// ---------------------------------------------------------------------------
int KK::DipSplitPhase()
{
    if (DipSplitEnable == 0) return 0;
    if (nDims < 2)           return 0;   // need at least 1 feature dim + time

    // Snapshot alive clusters — we'll create new ones during iteration and
    // shouldn't probe them recursively this pass.
    std::vector<int> alive_snapshot;
    alive_snapshot.reserve(32);
    for (int c = 1; c < MaxPossibleClusters; ++c)
        if (ClassAlive[c]) alive_snapshot.push_back(c);

    // Count-by-reason for summary
    int n_split       = 0;
    int n_small       = 0;
    int n_not_bloated = 0;
    int n_no_valley   = 0;
    int n_small_child = 0;
    int n_bic_worse   = 0;
    int n_no_free_id  = 0;

    LockedStderr( "[DipSplit] DipSplit: probing %zu alive clusters "
                    "(bloat=%.2f, elong=%.2f, valley=%.2f, minSize=%d)\n",
            alive_snapshot.size(), DipSplitBloatFactor,
            DipSplitElongationFactor, DipSplitValleyThresh, DipSplitMinSize);

    for (int c : alive_snapshot) {
        if (!ClassAlive[c]) continue;
        const char* reason = "skip";
        DipSplitAttemptEx(c, reason);
        if      (std::strcmp(reason, "split")       == 0) ++n_split;
        else if (std::strcmp(reason, "small")       == 0) ++n_small;
        else if (std::strcmp(reason, "not_bloated") == 0) ++n_not_bloated;
        else if (std::strcmp(reason, "no_valley")   == 0) ++n_no_valley;
        else if (std::strcmp(reason, "small_child") == 0) ++n_small_child;
        else if (std::strcmp(reason, "bic_worse")   == 0) ++n_bic_worse;
        else if (std::strcmp(reason, "no_free_id")  == 0) ++n_no_free_id;
    }

    LockedStderr( "[DipSplit] DipSplit: %d accepted  "
                    "(rejections: %d too-small, %d not-flagged, %d no-valley, "
                    "%d small-child, %d bic-worse, %d no-free-id)\n",
            n_split, n_small, n_not_bloated, n_no_valley,
            n_small_child, n_bic_worse, n_no_free_id);

    if (n_split > 0) {
        // Refresh cluster stats so downstream phases see consistent state.
        MStep();
        EStep();
        CStep();
        Reindex();
    }
    return n_split;
}

// ---------------------------------------------------------------------------
// TimeShiftSplitCluster — thin wrapper over the primitive
// ---------------------------------------------------------------------------
int KK::TimeShiftSplitCluster(int clusterId, int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady) return 0;
    std::vector<int> members;
    members.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == clusterId) members.push_back(p);
    return TimeShiftSplit(members, nChan, nSamplesPerSpike);
}

// ---------------------------------------------------------------------------
// TimeShiftFinalize — Phase 9: shift commit
//
// Re-extract each shifted spike from .fil at (rawTs - cumShift - PeakSampleIndex),
// re-project through the PCA basis, and rewrite .spk / .fet (via
// WritePhase15Checkpoint's .*.pending mechanism).  This is the ONLY point at
// which disk is touched by the shift-probe — all earlier probe operations
// work purely in memory on Data[] and m_cumShift[].
//
// Using .fil rather than circular-shifting the .spk waveforms eliminates
// wrap-around corruption that would otherwise poison ~sample/waveform of
// the shifted window.  For spikes with cumShift == 0, the existing .spk
// content is preserved (no re-extract).
//
// Owns the phase label so log output reflects what's happening in real
// time.  Phase 1a is now cluster alignment (TimeShiftAlignPhase);
// Phase 4 is the final disk commit that closes the probe session.
// ---------------------------------------------------------------------------

// patch87 ----------------------------------------------------------------
// AutoReextractAfterFinalize: after TimeShiftFinalize has committed the
// .res / .spk / .fet rewrites via WritePhase15Checkpoint, invoke the
// appropriate extractspikes binary in -R mode to refresh the .spk(D)
// content from .fil for the CURRENT group at the now-corrected .res
// timestamps.
//
// Why this is needed even though WritePhase15Checkpoint also rewrites
// .spk(D): for stderiv pipelines, the per-spike re-extract in
// WritePhase15Checkpoint computes the temporal first-difference at
// sample s=0 with prev_sdiff = 0, because it has no cross-spike state.
// That introduces a small sample-0 error in every re-extracted spike.
// The full streaming re-extractor — but using mmap-and-jump in -R mode
// (patch86), which reads one extra sample at (ws - 1) and uses its
// spatial derivative as prev_sdiff — produces bit-correct .spkD content.
//
// For non-stderiv pipelines (vanilla .spk) the sample-0 issue doesn't
// exist, but running this is still harmless: the re-extracted content
// matches what WritePhase15Checkpoint wrote.  Caller can disable via
// KKEXP_AUTO_REEXTRACT=0.
//
// Pipeline detection:
//   m_timeShiftBasis.isStderiv == true   → process_extractspikes_stderiv
//   otherwise                            → process_extractspikes
//   override:  KKEXP_REEXTRACT_TOOL=...   (e.g. for sdiff pipeline)
//
// Strategy:
//   1. mkdtemp /tmp/kkexp-rxn-XXXXXX
//   2. symlink <tmp>/x.fil  → <FileBase>.fil
//   3. symlink <tmp>/x.res.1 → <FileBase>.res.<ElecNo>
//   4. popen("<tool> -n N -c chans -w W -p P [-d ord] -R <tmp>/x")
//   5. rename <tmp>/x.spk(D).1 → <FileBase>.spk(D).<ElecNo>
//   6. clean up symlinks + tmpdir
//
// Failures along the way are logged; the original files are preserved
// (the rename in step 5 is the only write to the canonical paths).
static void AutoReextractAfterFinalize(int nChan,
                                       int nSamplesPerSpike,
                                       bool isStderiv,
                                       int peakSampleIdx)
{
    (void)nChan;
    (void)nSamplesPerSpike;

    // ── 1. env gating ────────────────────────────────────────────────────
    const char *disable = getenv("KKEXP_AUTO_REEXTRACT");
    if (disable && strcmp(disable, "0") == 0) {
        Output("[Phase 6d] auto-reextract disabled by KKEXP_AUTO_REEXTRACT=0\n");
        return;
    }

    // ── 2. pick the tool ─────────────────────────────────────────────────
    const char *tool = getenv("KKEXP_REEXTRACT_TOOL");
    const char *spkExt = isStderiv ? "spkD" : "spk";
    if (!tool || !*tool) {
        tool = isStderiv ? "process_extractspikes_stderiv"
                         : "process_extractspikes";
    }

    // ── 3. session-level inputs we need from the runtime ─────────────────
    if (NbTotalChannels <= 0 || GroupChannelIds.empty()) {
        Output("[Phase 6d] auto-reextract: missing NbTotalChannels or "
               "GroupChannelIds — skipping\n");
        return;
    }
    if (nSamplesPerSpike <= 0 || peakSampleIdx <= 0) {
        Output("[Phase 6d] auto-reextract: missing waveform geometry "
               "(nSamplesPerSpike=%d peakSampleIdx=%d) — skipping\n",
               nSamplesPerSpike, peakSampleIdx);
        return;
    }

    // ── 4. build the channel-list string (single group) ──────────────────
    std::string chans;
    for (size_t i = 0; i < GroupChannelIds.size(); i++) {
        if (i > 0) chans += ",";
        chans += std::to_string(GroupChannelIds[i]);
    }

    // ── 5. temp dir + symlinks ───────────────────────────────────────────
    char tmpDirTemplate[] = "/tmp/kkexp-rxn-XXXXXX";
    if (!mkdtemp(tmpDirTemplate)) {
        Output("[Phase 6d] auto-reextract: mkdtemp failed (%s) — skipping\n",
               strerror(errno));
        return;
    }
    const std::string tmpDir   = tmpDirTemplate;
    const std::string tmpStem  = tmpDir + "/x";
    const std::string filLink  = tmpStem + ".fil";
    const std::string resLink  = tmpStem + ".res.1";

    char filReal[1024], resReal[1024], spkReal[1024];
    snprintf(filReal, sizeof(filReal), "%s.fil",            FileBase);
    snprintf(resReal, sizeof(resReal), "%s.res.%d",         FileBase, ElecNo);
    snprintf(spkReal, sizeof(spkReal), "%s.%s.%d",          FileBase, spkExt,
             ElecNo);

    // Resolve to absolute paths for the symlinks (in case FileBase is
    // relative — symlinks resolve relative to the symlink's *directory*,
    // not the cwd).
    //
    // Pass NULL as the destination so realpath() malloc's a buffer of
    // up to PATH_MAX bytes itself.  Using a fixed-size stack buffer
    // (e.g. char[1024]) triggers glibc's __realpath_chk FORTIFY abort
    // ("*** buffer overflow detected ***: terminated") even when the
    // actual resolved path would have fit, because the check refuses
    // any destination smaller than PATH_MAX at compile time.  Caller
    // must free() the returned pointer.
    char* filAbs = realpath(filReal, nullptr);
    if (!filAbs) {
        Output("[Phase 6d] auto-reextract: cannot resolve %s (%s) — skipping\n",
               filReal, strerror(errno));
        rmdir(tmpDir.c_str());
        return;
    }
    char* resAbs = realpath(resReal, nullptr);
    if (!resAbs) {
        Output("[Phase 6d] auto-reextract: cannot resolve %s (%s) — skipping\n",
               resReal, strerror(errno));
        free(filAbs);
        rmdir(tmpDir.c_str());
        return;
    }

    if (symlink(filAbs, filLink.c_str()) != 0) {
        Output("[Phase 6d] auto-reextract: symlink %s → %s failed (%s) — "
               "skipping\n", filLink.c_str(), filAbs, strerror(errno));
        free(filAbs); free(resAbs);
        rmdir(tmpDir.c_str());
        return;
    }
    if (symlink(resAbs, resLink.c_str()) != 0) {
        Output("[Phase 6d] auto-reextract: symlink %s → %s failed (%s) — "
               "skipping\n", resLink.c_str(), resAbs, strerror(errno));
        free(filAbs); free(resAbs);
        unlink(filLink.c_str());
        rmdir(tmpDir.c_str());
        return;
    }

    // ── 6. build command line ────────────────────────────────────────────
    std::ostringstream cmd;
    cmd << tool;
    cmd << " -n " << NbTotalChannels;
    cmd << " -c " << chans;
    cmd << " -w " << nSamplesPerSpike;
    // PeakSampleIndex in KKExp is 0-based (per KlustaKwik.h declaration);
    // the binary's -p flag expects 1-based and subtracts 1 internally.
    cmd << " -p " << (peakSampleIdx + 1);
    if (isStderiv) {
        // Default sdiff order if not overridden.  3 = ALLPAIRS matches the
        // wrapper script default.  Configurable via env for non-default
        // sessions (e.g. probes that require a specific spatial-derivative
        // order, or stderiv-on-raw with order 0).
        const char *sdiffOrder = getenv("KKEXP_REEXTRACT_SDIFF_ORDER");
        cmd << " -d " << (sdiffOrder && *sdiffOrder ? sdiffOrder : "3");
    }
    cmd << " -R";
    if (getenv("KKEXP_REEXTRACT_VERBOSE")) cmd << " -v";
    cmd << " " << tmpStem;
    cmd << " 2>&1";  // fold stderr into stdout for popen capture

    Output("[Phase 6d] auto-reextract: %s\n", cmd.str().c_str());

    // ── 7. popen + stream output ─────────────────────────────────────────
    FILE *pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        Output("[Phase 6d] auto-reextract: popen failed (%s) — skipping\n",
               strerror(errno));
        free(filAbs); free(resAbs);
        unlink(filLink.c_str());
        unlink(resLink.c_str());
        rmdir(tmpDir.c_str());
        return;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe)) {
        Output("[Phase 6d]   %s", buf);  // tool output already has '\n'
    }
    const int rc = pclose(pipe);

    // ── 8. rename output to canonical location, only on success ──────────
    const std::string spkOut = tmpStem + "." + spkExt + ".1";
    if (rc == 0) {
        if (rename(spkOut.c_str(), spkReal) == 0) {
            Output("[Phase 6d] auto-reextract: refreshed %s from %s\n",
                   spkReal, filAbs);
        } else {
            Output("[Phase 6d] auto-reextract: rename %s → %s failed (%s) — "
                   "ORIGINAL .%s PRESERVED, tmp output discarded\n",
                   spkOut.c_str(), spkReal, strerror(errno), spkExt);
            unlink(spkOut.c_str());
        }
    } else {
        Output("[Phase 6d] auto-reextract: tool exited with status %d — "
               "ORIGINAL .%s PRESERVED, tmp output discarded\n",
               rc, spkExt);
        unlink(spkOut.c_str());  // tool may have created a partial file
    }

    // ── 9. cleanup symlinks + tmpdir ─────────────────────────────────────
    unlink(filLink.c_str());
    unlink(resLink.c_str());
    rmdir(tmpDir.c_str());
    free(filAbs);
    free(resAbs);
}

void KK::TimeShiftFinalize(int nChan, int nSamplesPerSpike)
{
    if (m_cumShift.empty()) { CloseTimeShift(); return; }

    const int nShifted = static_cast<int>(
        std::count_if(m_cumShift.begin(), m_cumShift.end(),
                      [](int s){ return s != 0; }));

    if (nShifted > 0) {
        LockedStderr(
                "[Phase 6c] Shift commit: re-extract %d spikes from .fil "
                "→ .spk/.fet\n", nShifted);
        Output("[Phase 6c] Shift commit: %d probe calls, %d spikes with "
               "non-zero cumulative shift\n",
               m_timeShiftCallCount, nShifted);
        // RefeaturizeFromShifts expects shift=0 to mean "skip" — which matches
        // our accumulator.  It re-extracts from .fil for non-zero entries,
        // projects, and re-normalises; this supersedes the in-memory features
        // with clean .fil-derived ones.  WritePhase15Checkpoint then writes
        // the corrected .spk / .fet / .res via the .*.pending mechanism.
        RefeaturizeFromShifts(m_cumShift, nChan, nSamplesPerSpike);
        WritePhase15Checkpoint(m_cumShift, nChan, nSamplesPerSpike);

        // patch87: after the .pending files are committed, invoke the
        // extractspikes binary in -R mode to refresh .spk(D) content from
        // .fil at the now-corrected .res timestamps.  Fixes the stderiv
        // sample-0 dispersion that WritePhase15Checkpoint can't fully
        // address per-spike (it has no cross-spike continuity for the
        // temporal first-difference).
        AutoReextractAfterFinalize(nChan, nSamplesPerSpike,
                                   m_timeShiftBasis.isStderiv,
                                   PeakSampleIndex);
    } else {
        Output("[Phase 6c] Shift commit: %d probe calls, 0 spikes shifted "
               "— nothing to write back\n", m_timeShiftCallCount);
    }
    CloseTimeShift();
}


// ---------------------------------------------------------------------------
// KK::DipSplitPerChunk
//
// Per-chunk DipSplit pass — runs immediately after Phase 1 chunked CEM,
// before any other refinement.  For each chunk, builds a thread-local
// scratch KK populated with the chunk's data and current Class[] labels,
// runs MStep + EStep so cluster stats are current, then invokes
// DipSplitPhase on the scratch KK to find bimodal/elongated clusters
// that the parametric CEM merged.  Refined labels are copied back to
// perChunkClass[ck] and the chunk's ChunkModel list is rebuilt from the
// new partition.
//
// This replaces the global Phase 8 DipSplit invocation that previously
// ran AFTER cross-chunk model matching.  Running per-chunk and early has
// two advantages over the late global pass:
//
//   - Catches missed splits at the chunk level before cross-chunk merge
//     reads the per-chunk models.  Misses at this stage propagate as
//     undersplit globals; catching them here makes Phase 6 see the
//     correct cluster count.
//
//   - Avoids splitting global clusters that look bimodal only because
//     drift across chunks made the spike distribution look two-moded.
//     Per-chunk views are drift-free within their time window.
//
// Cluster ID allocation: when a chunk-local cluster splits into two,
// sub-cluster 0 keeps the original local id; sub-cluster 1 gets a fresh
// id starting from the chunk's current maxLocalId+1.
// ---------------------------------------------------------------------------
void KK::DipSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims,
    const char* phaseLabel)
{
    if (DipSplitEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    int totalSplitsAcrossChunks = 0;
    int totalChunksWithSplits   = 0;

    LockedStderr(
            "[%s] Per-chunk DipSplit (bloat=%.2f, elong=%.2f, "
            "valley=%.2f, minSize=%d)\n",
            phaseLabel,
            DipSplitBloatFactor, DipSplitElongationFactor,
            DipSplitValleyThresh, DipSplitMinSize);

    // Per-chunk results: new perChunkClass and replacement ChunkModel list.
    // Computed in parallel below, applied serially after the parallel block
    // so model-rebuild side-effects don't race.
    struct ChunkResult {
        bool                     changed = false;
        std::vector<int>         newClass;
        std::vector<ChunkModel>  newModels;
        int                      nSplits = 0;
    };
    std::vector<ChunkResult> results(nCh);

    #pragma omp parallel for schedule(dynamic) reduction(+:totalSplitsAcrossChunks,totalChunksWithSplits)
    for (int ck = 0; ck < nCh; ck++) {
        const auto& pts = chunkPoints[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts < DipSplitMinSize) continue;
        if (perChunkClass[ck].size() != static_cast<size_t>(nPts)) continue;

        // Skip chunks with only noise.  Need at least one real cluster for
        // DipSplit to have anything to probe.
        int maxLc = 0;
        for (int c : perChunkClass[ck]) if (c > maxLc) maxLc = c;
        if (maxLc < 1) continue;

        // ── Build scratch KK with chunk's data and current labels ─────
        KK Ks;
        Ks.nDims              = nFullDims;
        Ks.nPoints            = nPts;
        Ks.penaltyMix         = penaltyMix;
        Ks.suppressBestSave   = true;
        Ks.minClustersAlive   = 1;
        Ks.NoisePoint         = 0;          // user spec: no real noise from extraction

        // DipSplit parameters (DipSplitEnable, DipSplitMinSize,
        // DipSplitElongationFactor, DipSplitValleyThresh, DipSplitBloatFactor)
        // are file-scope globals defined in Parameters.cpp, not class members.
        // DipSplitPhase reads them directly — no per-instance copy required.

        Ks.AllocateArrays();
        Ks.AllocateCholeskyVecs();
        Ks.ReinitForSplit(nPts, nFullDims, penaltyMix);

        // Copy chunk data
        for (int i = 0; i < nPts; i++) {
            const int p = pts[static_cast<size_t>(i)];
            for (int d = 0; d < nFullDims; d++)
                Ks.Data[static_cast<size_t>(i) * nFullDims + d] =
                    Data[static_cast<size_t>(p) * nFullDims + d];
        }
        Ks.timeRawMin = timeRawMin;
        Ks.timeRawMax = timeRawMax;

        // Set Class[] from current per-chunk labels and ClassAlive[]
        for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
        for (int i = 0; i < nPts; i++) {
            const int c = perChunkClass[ck][static_cast<size_t>(i)];
            Ks.Class[i] = c;
            if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
        }
        Ks.Reindex();

        // Compute cluster stats so DipSplit sees current Mean/Cov/cholFlat.
        // EStep is needed (not just MStep) because DipSplitAttemptEx reads
        // cholFlat for Mahalanobis-based gates.
        Ks.MStep();
        Ks.EStep();

        // Snapshot pre-split alive count to measure how many splits occurred
        const int aliveBefore = Ks.nClustersAlive;

        // Run DipSplit on the scratch KK.  DipSplitPhase prints a summary
        // line tagged "[DipSplit]"; for per-chunk usage we don't need the
        // verbose per-chunk summaries (the overall total is logged below
        // in the serial section), so we silence them by gating on a per-
        // chunk flag.  Rather than threading a verbosity flag, we accept
        // the existing output — it's harmless and useful for debugging.
        Ks.DipSplitPhase();

        const int aliveAfter = Ks.nClustersAlive;
        const int nSplits    = std::max(0, aliveAfter - aliveBefore);

        if (nSplits == 0) continue;

        // DipSplitPhase mutated Class[] but didn't refresh Mean/Cov for the
        // new sub-clusters.  Run MStep so we can populate ChunkModel mean/cov
        // from Ks.Mean/Ks.Cov below.  cholFlat doesn't need refreshing here
        // — it isn't read by the rebuild path.
        Ks.MStep();

        // Read back labels.  DipSplit may have allocated new cluster IDs
        // outside the previous maxLc range.  We preserve those IDs in the
        // chunk-local namespace by remapping: any new ID (> original maxLc)
        // gets a fresh local id starting from maxLc+1, allocated densely
        // and deterministically per chunk.
        std::unordered_map<int,int> remap;
        int nextLc = maxLc + 1;
        std::vector<int> newCls(nPts);
        for (int i = 0; i < nPts; i++) {
            const int sc = Ks.Class[i];
            if (sc <= maxLc) { newCls[i] = sc; continue; }
            auto it = remap.find(sc);
            if (it == remap.end()) {
                remap[sc] = nextLc++;
                it = remap.find(sc);
            }
            newCls[i] = it->second;
        }

        // Build a sub-cluster -> final-local-id map for the ChunkModel
        // rebuild loop below (sub-cluster id in Ks.Class[] -> local id we
        // committed to perChunkClass[ck]).
        std::unordered_map<int,int> scToLc;
        for (int sc = 0; sc < MaxPossibleClusters; sc++) {
            if (!Ks.ClassAlive[sc]) continue;
            if (sc <= maxLc)              scToLc[sc] = sc;
            else if (remap.count(sc))     scToLc[sc] = remap[sc];
            // else: alive but no spikes assigned — skip
        }

        // Rebuild ChunkModel list from Ks.Mean / Ks.Cov.  Mirror the
        // canonical pattern used in Phase 1's per-chunk seeding loop
        // (KK.cpp:3198-3218): full nFullDims mean, full nFullDims² cov
        // (upper triangle only), nMembers from Ks.Class scan.
        std::vector<ChunkModel> newMdls;
        newMdls.reserve(scToLc.size());
        for (const auto& [sc, lc] : scToLc) {
            ChunkModel cm;
            cm.chunkIdx        = ck;
            cm.localClusterId  = lc;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(static_cast<size_t>(nFullDims), 0.0f);
            cm.cov.assign (static_cast<size_t>(nFullDims) * nFullDims, 0.0f);

            for (int d = 0; d < nFullDims; d++)
                cm.mean[static_cast<size_t>(d)] =
                    Ks.Mean[static_cast<size_t>(sc) * nFullDims + d];
            for (int r = 0; r < nFullDims; r++)
                for (int col = r; col < nFullDims; col++)
                    cm.cov[static_cast<size_t>(r) * nFullDims + col] =
                        Ks.Cov[static_cast<size_t>(sc) * Ks.nDims2
                              + r * nFullDims + col];

            for (int i = 0; i < nPts; i++)
                if (Ks.Class[i] == sc) cm.nMembers++;
            if (cm.nMembers == 0) continue;

            newMdls.push_back(std::move(cm));
        }

        results[ck].changed   = true;
        results[ck].newClass  = std::move(newCls);
        results[ck].newModels = std::move(newMdls);
        results[ck].nSplits   = nSplits;
        totalSplitsAcrossChunks += nSplits;
        totalChunksWithSplits   += 1;
    }

    // ── Serial application ───────────────────────────────────────────
    for (int ck = 0; ck < nCh; ck++) {
        if (!results[ck].changed) continue;
        perChunkClass [ck] = std::move(results[ck].newClass);
        perChunkModels[ck] = std::move(results[ck].newModels);
    }

    LockedStderr(
            "[Phase 1b] DipSplit per-chunk: %d splits across %d chunks\n",
            totalSplitsAcrossChunks, totalChunksWithSplits);
}


// ---------------------------------------------------------------------------
// KK::HullSplitPerChunk
//
// Phase 2a.6: k-NN-graph connected-components split.  Per-cluster, treats
// each cluster's spikes as a point cloud in feature space, builds a k-NN
// graph, and asks: is this cluster topologically a single connected blob,
// or does it fall into multiple distinct components?  Components ≥
// minComponentSize materialise as new local clusters within the chunk;
// smaller components are absorbed (stay in the parent cluster).
//
// Complement to Phase 2a.5 DipSplit: DipSplit catches 1D-projected
// bimodality; HullSplit catches multi-D topology — two distinct units
// that occupy separate regions of (PC1, PC2, …, PC_K) feature space
// that no single PC marginal reveals.
//
// Model rebuild deferred to Phase 2b (ChunkReCEMPerChunk regenerates
// ChunkModels from the post-HullSplit labels via its MStep pass).  So
// HullSplitPerChunk only updates perChunkClass[].
//
// Cluster ID allocation: the largest component keeps the original local
// ID; additional components get fresh IDs starting from chunk's current
// maxLocalId+1.
//
// Cost: per-chunk parallel; brute-force k-NN within each cluster is
// O(n² · d / 16) with AVX-512 batched distance (~10 ms for a 5000-spike
// cluster at d=22).  Most clusters are small (≤500 spikes) so the dominant
// cost is the long tail.
// ---------------------------------------------------------------------------
void KK::HullSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    int nFullDims,
    const char* phaseLabel)
{
    if (HullSplitEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    LockedStderr(
            "[%s] HullSplit per-chunk (k=%d, minSize=%d, reachScale=%.2f, "
            "metric=%s)\n",
            phaseLabel, HullSplitK, HullSplitMinComponentSize,
            HullSplitMutualReachScale,
            HullSplitUseMutualReach ? "mutual-reachability" : "raw-euclidean");

    int totalSplitsAcrossChunks = 0;
    int totalChunksWithSplits   = 0;
    int totalNewSubClusters     = 0;

    struct ChunkResult {
        bool             changed = false;
        std::vector<int> newClass;
        int              nSplits = 0;
        int              nNewSubClusters = 0;
    };
    std::vector<ChunkResult> results(static_cast<size_t>(nCh));

    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalSplitsAcrossChunks,totalChunksWithSplits,totalNewSubClusters)
    for (int ck = 0; ck < nCh; ck++) {
        const auto& pts = chunkPoints[static_cast<size_t>(ck)];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts < 2 * HullSplitMinComponentSize) continue;
        if (perChunkClass[static_cast<size_t>(ck)].size() != static_cast<size_t>(nPts))
            continue;

        // Copy current labels so we mutate a local buffer and only
        // commit at the end.
        std::vector<int> newCls = perChunkClass[static_cast<size_t>(ck)];

        // Track maxLc across the chunk; new sub-cluster IDs allocate
        // starting from maxLc+1.  Each accepted split bumps maxLc.
        int maxLc = 0;
        for (int c : newCls) if (c > maxLc) maxLc = c;

        // Find unique non-noise local cluster IDs in this chunk.
        std::vector<int> uniqueLcs;
        {
            std::vector<bool> seen(static_cast<size_t>(maxLc + 1), false);
            for (int c : newCls)
                if (c > 0 && c < static_cast<int>(seen.size()) &&
                    !seen[static_cast<size_t>(c)]) {
                    seen[static_cast<size_t>(c)] = true;
                    uniqueLcs.push_back(c);
                }
        }

        int chunkSplits         = 0;
        int chunkNewSubClusters = 0;

        for (int lc : uniqueLcs) {
            // Gather this cluster's spike indices and feature buffer.
            std::vector<int> memberIdx;  // indices into pts[] / newCls[]
            memberIdx.reserve(64);
            for (int i = 0; i < nPts; i++)
                if (newCls[static_cast<size_t>(i)] == lc) memberIdx.push_back(i);

            const int nMem = static_cast<int>(memberIdx.size());
            if (nMem < 2 * HullSplitMinComponentSize) continue;

            // Pack feature vectors into a contiguous nMem × nFullDims
            // buffer (point-major, same stride as Data).  cluster_hull_split
            // reads with cfg.excludeTimeDim, so we keep all dims in the
            // buffer and let it drop the last one internally.
            std::vector<float> featBuf(
                static_cast<size_t>(nMem) * nFullDims);
            for (int i = 0; i < nMem; i++) {
                const int p = pts[static_cast<size_t>(memberIdx[static_cast<size_t>(i)])];
                for (int d = 0; d < nFullDims; d++) {
                    featBuf[static_cast<size_t>(i) * nFullDims + d] =
                        Data[static_cast<size_t>(p) * nFullDims + d];
                }
            }

            cluster_hull_split::Config cfg;
            cfg.k                       = HullSplitK;
            cfg.minComponentSize        = HullSplitMinComponentSize;
            cfg.mutualReachabilityScale = HullSplitMutualReachScale;
            cfg.useMutualReachability   = (HullSplitUseMutualReach != 0);
            cfg.excludeTimeDim          = true;

            cluster_hull_split::Result hr =
                cluster_hull_split::Run(featBuf.data(), nMem, nFullDims, cfg);

            if (!hr.didSplit) continue;

            // Identify the largest component — it keeps lc.  Other
            // components get fresh local IDs from maxLc+1.
            std::vector<int> compSize(
                static_cast<size_t>(hr.nComponents + 1), 0);
            for (int lbl : hr.componentLabels) {
                if (lbl >= 0 && lbl <= hr.nComponents)
                    ++compSize[static_cast<size_t>(lbl)];
            }
            int largestComp = 1;
            for (int c = 2; c <= hr.nComponents; c++)
                if (compSize[static_cast<size_t>(c)] >
                    compSize[static_cast<size_t>(largestComp)])
                    largestComp = c;

            // Allocate new local IDs for all non-largest non-zero components.
            std::vector<int> compToLc(
                static_cast<size_t>(hr.nComponents + 1), 0);
            compToLc[0]            = lc;  // absorbed → stay in parent
            compToLc[static_cast<size_t>(largestComp)] = lc;  // largest → keep
            int newIds = 0;
            for (int c = 1; c <= hr.nComponents; c++) {
                if (c == largestComp) continue;
                compToLc[static_cast<size_t>(c)] = ++maxLc;
                ++newIds;
            }

            // Write back per-member labels.
            for (int i = 0; i < nMem; i++) {
                const int lbl = hr.componentLabels[static_cast<size_t>(i)];
                const int newLc = compToLc[static_cast<size_t>(lbl)];
                newCls[static_cast<size_t>(memberIdx[static_cast<size_t>(i)])] = newLc;
            }

            ++chunkSplits;
            chunkNewSubClusters += newIds;
        }

        if (chunkSplits > 0) {
            results[static_cast<size_t>(ck)].changed         = true;
            results[static_cast<size_t>(ck)].newClass        = std::move(newCls);
            results[static_cast<size_t>(ck)].nSplits         = chunkSplits;
            results[static_cast<size_t>(ck)].nNewSubClusters = chunkNewSubClusters;
            totalSplitsAcrossChunks += chunkSplits;
            totalChunksWithSplits   += 1;
            totalNewSubClusters     += chunkNewSubClusters;
        }
    }

    // Serial application.
    for (int ck = 0; ck < nCh; ck++) {
        if (!results[static_cast<size_t>(ck)].changed) continue;
        perChunkClass[static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newClass);
    }

    LockedStderr(
            "[%s] HullSplit per-chunk: %d clusters split, +%d new sub-clusters, "
            "%d chunks affected\n",
            phaseLabel, totalSplitsAcrossChunks, totalNewSubClusters,
            totalChunksWithSplits);
}


// ---------------------------------------------------------------------------
// KK::PerChannelSplitPerChunk
//
// Phase 2a.7: per-channel amplitude+phase bimodality split.  Per chunk,
// per cluster: read each spike's full waveform, extract 4 features per
// channel (peak amp, peak time, trough amp, trough time), run 1D + 2D
// angle-sweep valley tests across channels, and split clusters whose
// strongest channel shows a KDE-detectable valley.  See per_channel_split.h
// for the algorithm in detail.
//
// Catches the failure mode where two real units share most of their
// waveform but differ by a small combination of amplitude AND phase on
// one specific channel (klusters cluster #131 case).  DipSplit misses
// this because the signal spreads across multiple PCA dims; HullSplit
// misses it because the clusters are topologically connected.
//
// Like HullSplit, defers model rebuild to Phase 2b (ChunkReCEMPerChunk
// will regenerate ChunkModels from the post-split labels via its MStep
// pass).  So this method only updates perChunkClass[].
//
// Cost: per-chunk parallel; dominated by waveform reads (one mmap memcpy
// per spike) and the KDE-based valley_test (O(n log n + G) per scan).
// ~200ms total for 36 chunks × 25 clusters × 8 channels × 22 tests.
// ---------------------------------------------------------------------------
void KK::PerChannelSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    int nChan, int nSamplesPerSpike,
    const char* phaseLabel)
{
    if (PerChannelSplitEnable == 0) return;
    if (!m_timeShiftReady) {
        LockedStderr(
                "[%s] PerChannelSplit skipped: TimeShift backing store not "
                "initialised (need .spk mmap/fp for waveform reads).\n",
                phaseLabel);
        return;
    }
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0 || nChan <= 0 || nSamplesPerSpike <= 0) return;

    LockedStderr(
            "[%s] PerChannelSplit per-chunk (minSize=%d, valley=%.2f, "
            "minSubSize=%d, bicMargin=%.1f+%.1f·logN, snrRatio=%.2f)\n",
            phaseLabel, PerChannelSplitMinClusterSize,
            PerChannelSplitValleyThreshold, PerChannelSplitMinSubClusterSize,
            PerChannelSplitBicMarginConstant, PerChannelSplitBicMarginPerLogN,
            PerChannelSplitMinChannelSnrRatio);

    const int waveSamples = nChan * nSamplesPerSpike;

    int totalSplits         = 0;
    int totalChunksWithSplit = 0;
    int totalNewClusters    = 0;

    struct ChunkResult {
        bool             changed = false;
        std::vector<int> newClass;
        int              nSplits = 0;
        int              nNewClusters = 0;
    };
    std::vector<ChunkResult> results(static_cast<size_t>(nCh));

    // NOTE: per-chunk parallelism is safe because each chunk has an
    // independent perChunkClass[ck] vector and an independent set of
    // (global) spike indices.  TimeShiftReadSpikeWave is thread-safe
    // when m_timeShiftSpkMap is non-null (read-only memcpy); the
    // fseeko+fread fallback path is NOT thread-safe so we serialise
    // if mmap is unavailable.
    const bool mapBacked = (m_timeShiftSpkMap != nullptr);
    const int  maxThreads = mapBacked ? omp_get_max_threads() : 1;

    #pragma omp parallel for schedule(dynamic) num_threads(maxThreads) \
        reduction(+:totalSplits,totalChunksWithSplit,totalNewClusters)
    for (int ck = 0; ck < nCh; ++ck) {
        const auto& pts  = chunkPoints[static_cast<size_t>(ck)];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts < 2 * PerChannelSplitMinSubClusterSize) continue;
        if (perChunkClass[static_cast<size_t>(ck)].size() !=
            static_cast<size_t>(nPts)) continue;

        // Build local labels indexed [0, nPts) and per-cluster spike lists
        // (also using local indices).  per_channel_split::Run will receive
        // local indices and we translate them to global at read time.
        std::vector<int> localLabels =
            perChunkClass[static_cast<size_t>(ck)];

        int maxLc = 0;
        for (int c : localLabels) if (c > maxLc) maxLc = c;

        // Per-cluster spike lists (local indices).
        std::vector<std::vector<int>> clusterSpikes(
            static_cast<size_t>(maxLc + 1));
        for (int i = 0; i < nPts; ++i) {
            const int c = localLabels[static_cast<size_t>(i)];
            if (c <= 0) continue;
            clusterSpikes[static_cast<size_t>(c)].push_back(i);
        }

        // readWaveform callback: translates local idx → global spike id.
        auto readWave = [&](int localIdx, int16_t* dst) -> bool {
            if (localIdx < 0 || localIdx >= nPts) return false;
            const int p = pts[static_cast<size_t>(localIdx)];
            return TimeShiftReadSpikeWave(p, waveSamples, dst);
        };

        per_channel_split::Config cfg;
        cfg.minClusterSize       = PerChannelSplitMinClusterSize;
        cfg.valleyThreshold      = PerChannelSplitValleyThreshold;
        cfg.minSubClusterSize    = PerChannelSplitMinSubClusterSize;
        cfg.bicMarginConstant    = PerChannelSplitBicMarginConstant;
        cfg.bicMarginPerLogN     = PerChannelSplitBicMarginPerLogN;
        cfg.minChannelSnrRatio   = PerChannelSplitMinChannelSnrRatio;
        cfg.usePeakAmplitude     = (PerChannelSplitUsePeakAmp    != 0);
        cfg.usePeakTime          = (PerChannelSplitUsePeakTime   != 0);
        cfg.useTroughAmplitude   = (PerChannelSplitUseTroughAmp  != 0);
        cfg.useTroughTime        = (PerChannelSplitUseTroughTime != 0);
        cfg.firstNewClusterId    = maxLc + 1;

        auto rr = per_channel_split::Run(
            clusterSpikes, readWave,
            nChan, nSamplesPerSpike,
            localLabels, cfg);

        if (rr.nClustersSplit > 0) {
            results[static_cast<size_t>(ck)].changed      = true;
            results[static_cast<size_t>(ck)].newClass     = std::move(localLabels);
            results[static_cast<size_t>(ck)].nSplits      = rr.nClustersSplit;
            results[static_cast<size_t>(ck)].nNewClusters = rr.nNewClusters;
            totalSplits          += rr.nClustersSplit;
            totalChunksWithSplit += 1;
            totalNewClusters     += rr.nNewClusters;
        }
    }

    // Serial application.
    for (int ck = 0; ck < nCh; ++ck) {
        if (!results[static_cast<size_t>(ck)].changed) continue;
        perChunkClass[static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newClass);
    }

    LockedStderr(
            "[%s] PerChannelSplit per-chunk: %d clusters split, +%d new "
            "sub-clusters, %d chunks affected\n",
            phaseLabel, totalSplits, totalNewClusters, totalChunksWithSplit);
}


// ---------------------------------------------------------------------------
// KK::LogPerChunkClusterState
//
// Diagnostic: one summary line per phase showing how the chunked label
// state evolves.  Reports:
//   • TOTAL across chunks: sum of distinct non-noise local IDs (upper bound
//     on final unique units; cross-chunk identity comes later in Phase 5/6)
//   • PER-CHUNK K: min/median/max of distinct non-noise local IDs
//   • NOISE: total spikes labelled 0 (cluster 0 = noise convention)
//   • SPIKES: total non-noise vs total
//
// Output format (one line, stderr):
//   [<phase>] CLUSTER STATE: total=N (K/chunk min=a median=b max=c),
//             noise=X/Y spikes (Z.Z%)
//
// Catches the "everything collapsed to noise" failure mode the user
// reported: when total drops to ~0 or noise% jumps to ~100%, the
// preceding phase is the culprit.
// ---------------------------------------------------------------------------
void KK::LogPerChunkClusterState(
    const std::vector<std::vector<int>>& chunkPoints,
    const std::vector<std::vector<int>>& perChunkClass,
    const char* phaseLabel) const
{
    const int nCh = static_cast<int>(perChunkClass.size());
    if (nCh == 0) {
        LockedStderr("[%s] CLUSTER STATE: (no chunks)\n", phaseLabel);
        return;
    }

    int total      = 0;
    int totalSpikes = 0;
    int noiseSpikes = 0;
    std::vector<int> kPerChunk;
    kPerChunk.reserve(static_cast<size_t>(nCh));

    for (int ck = 0; ck < nCh; ck++) {
        const auto& cls = perChunkClass[static_cast<size_t>(ck)];
        totalSpikes += static_cast<int>(cls.size());

        // Bound check chunkPoints sizes (defensive — they should match).
        if (ck < static_cast<int>(chunkPoints.size()) &&
            cls.size() != chunkPoints[static_cast<size_t>(ck)].size()) {
            // size mismatch — skip K count for this chunk but still count spikes
            kPerChunk.push_back(0);
            continue;
        }

        std::unordered_set<int> uniq;
        for (int c : cls) {
            if (c == 0) ++noiseSpikes;
            else        uniq.insert(c);
        }
        kPerChunk.push_back(static_cast<int>(uniq.size()));
        total += static_cast<int>(uniq.size());
    }

    // Per-chunk K stats: min, median, max
    int kMin = INT_MAX, kMax = 0;
    for (int k : kPerChunk) {
        if (k < kMin) kMin = k;
        if (k > kMax) kMax = k;
    }
    if (kPerChunk.empty()) { kMin = 0; }
    std::vector<int> kSorted = kPerChunk;
    std::sort(kSorted.begin(), kSorted.end());
    const int kMedian = kSorted.empty() ? 0 : kSorted[kSorted.size() / 2];

    const double noisePct = (totalSpikes > 0)
        ? 100.0 * static_cast<double>(noiseSpikes) / totalSpikes
        : 0.0;

    LockedStderr(
        "[%s] CLUSTER STATE: total=%d (K/chunk min=%d median=%d max=%d), "
        "noise=%d/%d spikes (%.1f%%)\n",
        phaseLabel, total, kMin, kMedian, kMax,
        noiseSpikes, totalSpikes, noisePct);
}


// ---------------------------------------------------------------------------
// KK::LogGlobalClusterState
//
// Global counterpart to LogPerChunkClusterState.  Called from the
// post-chunked phases (Phase 4, 5, 6, 6a, 6b, 7) where the perChunkClass
// labels have been promoted to global Class[]/ClassAlive[] and chunked
// diagnostics no longer apply.
//
// Output:
//   [<phase>] GLOBAL STATE: alive=N (incl noise), spikes per cluster
//             min=a median=b max=c, noise=X/Y spikes (Z.Z%)
//
// Same collapse-detection role: if alive drops to 1 or noise% goes to
// 100%, the phase between this line and the previous log entry is the
// offender.
// ---------------------------------------------------------------------------
void KK::LogGlobalClusterState(const char* phaseLabel) const
{
    // Defensive: if Class[] hasn't been populated yet (early in the
    // pipeline) the report is meaningless — emit a one-line note so the
    // log shows the call happened but no global state existed.
    if (nPoints <= 0 || !Class.m_Data) {
        LockedStderr("[%s] GLOBAL STATE: (no global Class state yet)\n",
                     phaseLabel);
        return;
    }

    // Count spikes per cluster from Class[].  Don't trust ClassAlive[]
    // alone — a cluster can be marked alive with zero members between
    // certain phase transitions (race between deletion and reassign).
    std::vector<int> sizeByCluster(static_cast<size_t>(MaxPossibleClusters), 0);
    int noiseSpikes = 0;
    int totalSpikes = nPoints;
    for (int p = 0; p < nPoints; ++p) {
        const int c = Class[p];
        if (c >= 0 && c < MaxPossibleClusters) {
            ++sizeByCluster[static_cast<size_t>(c)];
            if (c == 0) ++noiseSpikes;
        }
    }

    // Stats over non-noise non-empty clusters.
    std::vector<int> sizes;
    sizes.reserve(static_cast<size_t>(MaxPossibleClusters));
    int aliveNonNoise = 0;
    for (int c = 1; c < MaxPossibleClusters; ++c) {
        if (sizeByCluster[static_cast<size_t>(c)] > 0) {
            sizes.push_back(sizeByCluster[static_cast<size_t>(c)]);
            ++aliveNonNoise;
        }
    }

    int sMin = 0, sMax = 0, sMedian = 0;
    if (!sizes.empty()) {
        sMin = *std::min_element(sizes.begin(), sizes.end());
        sMax = *std::max_element(sizes.begin(), sizes.end());
        std::vector<int> sorted = sizes;
        std::sort(sorted.begin(), sorted.end());
        sMedian = sorted[sorted.size() / 2];
    }

    const double noisePct = (totalSpikes > 0)
        ? 100.0 * static_cast<double>(noiseSpikes) / totalSpikes
        : 0.0;

    // Single locked write — atomic against Output() under -Screen 1,
    // so 'Deleting Class ... Lose ... but Gain ...' lines no longer
    // splice GLOBAL STATE into their middle.
    LockedStderr(
        "[%s] GLOBAL STATE: alive=%d non-noise clusters, "
        "spikes/cluster min=%d median=%d max=%d, "
        "noise=%d/%d spikes (%.1f%%)\n",
        phaseLabel, aliveNonNoise,
        sMin, sMedian, sMax,
        noiseSpikes, totalSpikes, noisePct);
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
    int nFullDims)
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

    for (int ck = 0; ck < nCh; ck++) {
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

    LockedStderr(
            "[Phase 2a] Per-cluster CEM: probing %d items (min size %d, "
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

    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalSplits,totalNewSubClusters)
    for (int wi = 0; wi < static_cast<int>(items.size()); wi++) {
        const auto& item = items[static_cast<size_t>(wi)];
        const int   nMem = static_cast<int>(item.members.size());
        const auto& pts  = chunkPoints[item.ck];

        // Build sub-KK with only this cluster's spikes.
        //
        // SPATIAL-ONLY: time (last dim) is excluded.  Reasoning: this is
        // the same recluster that Klusters performs when the user "splits"
        // a cluster — Klusters spawns KlustaKwikExp on the cluster's
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
    for (int ck = 0; ck < nCh; ck++) {
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
            "[Phase 2a] Per-cluster CEM: %d clusters split, +%d new sub-clusters, "
            "%d chunks affected\n",
            totalSplits, totalNewSubClusters,
            static_cast<int>(chunksAffected.size()));
}


// ---------------------------------------------------------------------------
// TopKEigenPowerDeflation — top-K eigenvectors of a D×D symmetric matrix.
//
// Power-iteration with rank-1 deflation.  Cheap and adequate for the small
// K (≤ ~5) we need for residual-PCA: each eigenvector costs one chain of
// ~50 matvecs (D² ops each).  The matrix is modified in-place by the
// deflation A ← A − λ·v·vᵀ between iterations.
//
// Convergence criterion is on the Rayleigh quotient: stop when consecutive
// λ estimates differ by < relTol · |λ|, or after maxIter iterations.
//
// Outputs:
//   V       — row-major [K × D] eigenvectors (unit-norm, descending |λ|).
//   eigvals — [K] eigenvalues (descending in magnitude).
// ---------------------------------------------------------------------------
static void TopKEigenPowerDeflation(std::vector<double>& A,  // [D*D], modified
                                    int D, int K,
                                    int maxIter, double relTol,
                                    std::vector<double>& V,
                                    std::vector<double>& eigvals)
{
    V.assign(static_cast<size_t>(K) * D, 0.0);
    eigvals.assign(K, 0.0);
    if (D <= 0 || K <= 0) return;

    std::vector<double> v(D), Av(D);
    // Deterministic seeding so two runs on identical data give the same
    // eigenvectors (modulo sign).  Re-seeding per-eigenvector keeps things
    // reproducible across calls.
    uint64_t seed = 0x9E3779B97F4A7C15ULL;

    for (int k = 0; k < K; k++) {
        // Cheap xor-shift RNG inline; v has unit-norm random init.
        double n2 = 0.0;
        for (int i = 0; i < D; i++) {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
            const double r = static_cast<double>(seed & 0xFFFFFFFFULL) /
                             4294967296.0 - 0.5;
            v[i] = r; n2 += r * r;
        }
        if (!(n2 > 0)) { eigvals[k] = 0; continue; }
        const double inv = 1.0 / std::sqrt(n2);
        for (int i = 0; i < D; i++) v[i] *= inv;

        double lambda_prev = 0.0;
        bool   converged   = false;
        for (int it = 0; it < maxIter; it++) {
            // Av = A · v
            for (int i = 0; i < D; i++) {
                double s = 0.0;
                const double* row = A.data() + static_cast<size_t>(i) * D;
                for (int j = 0; j < D; j++) s += row[j] * v[j];
                Av[i] = s;
            }
            // λ = vᵀ Av
            double lambda = 0.0;
            for (int i = 0; i < D; i++) lambda += v[i] * Av[i];
            // Normalise Av → v
            double norm = 0.0;
            for (int i = 0; i < D; i++) norm += Av[i] * Av[i];
            if (!(norm > 0.0)) { lambda = 0; break; }
            const double ninv = 1.0 / std::sqrt(norm);
            for (int i = 0; i < D; i++) v[i] = Av[i] * ninv;

            if (it > 0 && std::fabs(lambda - lambda_prev) <
                          relTol * std::max(1.0, std::fabs(lambda))) {
                lambda_prev = lambda;
                converged   = true;
                break;
            }
            lambda_prev = lambda;
        }
        (void)converged;
        // Store eigenpair (rejecting near-zero / negative-noise eigenvalues
        // produced by deflation rounding on remaining low-variance modes).
        eigvals[k] = lambda_prev;
        for (int i = 0; i < D; i++)
            V[static_cast<size_t>(k) * D + i] = v[i];

        // Deflate: A ← A − λ · v vᵀ
        for (int i = 0; i < D; i++) {
            const double vi = lambda_prev * v[i];
            double* row = A.data() + static_cast<size_t>(i) * D;
            for (int j = 0; j < D; j++) row[j] -= vi * v[j];
        }
    }
}


// ---------------------------------------------------------------------------
// (FindDominantChannels + FindBestLagXCorr removed in patch37 — the per-
// cluster alignment in Phase 2b mode 3 now uses the shared, normalised-xcorr
// XcorrDispatch::compute (same library that backs Klusters' interactive
// realignSpikes), which operates on ALL channels rather than a dominant
// subset.  The deprecated ResidualPCADominantChannels CLI flag is kept for
// backwards compatibility but is ignored — see RunPhase2bMode3Chunk.)
// ---------------------------------------------------------------------------


// ===========================================================================
// Variational Bayesian GMM — used as an alternate Phase 2b inner loop.
//
// Reference: Bishop, "Pattern Recognition and Machine Learning" (2006), §10.2.
//
// Operates on hard input labels (warm-start from Phase 2a's cls0) and
// produces refined hard labels via argmax of the converged posterior
// responsibilities.  Differs from the CEM warm-start path in three
// substantive ways:
//
//   1. Soft assignment during iteration: boundary spikes split their
//      "credit" between candidate clusters via the variational posterior
//      r[n,k] ∈ [0,1], summing to 1 across k.  CEM commits hard at every
//      iteration.
//
//   2. Bayesian K selection via the Dirichlet prior: clusters whose
//      effective N_k drops below a threshold get pruned automatically,
//      no BIC tuning required.  Replaces ConsiderDeletion.
//
//   3. Conjugate prior regularisation on cluster covariances: the
//      Normal-Wishart prior keeps marginal clusters from collapsing into
//      degenerate point masses.  Replaces the manual "covariance is
//      singular" deletion path.
//
// The final hard assignment (argmax_k r[n,k]) is what gets handed back
// to the rest of the pipeline, so Phase 4 mean-waveform harvest, Phase 5
// template match, and Phase 5 cross-chunk match all continue to operate
// on hard labels and can build per-cluster Gaussians from those.
//
// All internal computation is double precision (Wishart updates are
// numerically sensitive; single precision can cause Cholesky failures
// on rank-deficient sub-clusters during early iterations before the
// prior fully regularises).  Inputs/outputs are float to match the
// surrounding pipeline.
// ===========================================================================

namespace {

// ----- Digamma (Stirling asymptotic series, recurse for small x) -----------
// ψ(x) ≈ ln(x) - 1/(2x) - 1/(12x²) + 1/(120x⁴) - 1/(252x⁶) for x > 6.
// For smaller x, use ψ(x) = ψ(x+1) - 1/x to push x into the asymptotic regime.
// Accuracy ~1e-12 for x ≥ 1; sufficient for ELBO convergence checks.
static double vbgmm_digamma(double x) {
    double result = 0.0;
    while (x < 6.0) {
        result -= 1.0 / x;
        x += 1.0;
    }
    const double r  = 1.0 / x;
    const double r2 = r * r;
    result += std::log(x) - 0.5 * r
            - r2 * (1.0/12.0 - r2 * (1.0/120.0 - r2 * (1.0/252.0)));
    return result;
}

// ----- Cholesky (double, lower-triangular, in-place) -----------------------
// Returns 0 on success, -1 if the matrix is non-positive-definite at any
// pivot.  Operates on dense [D × D] matrix; input M must be symmetric.
// Output L overwrites the lower triangle; upper triangle is left untouched
// (caller treats it as zero).
static int vbgmm_cholesky(double* M, int D) {
    for (int i = 0; i < D; i++) {
        for (int j = 0; j <= i; j++) {
            double s = M[i * D + j];
            for (int k = 0; k < j; k++)
                s -= M[i * D + k] * M[j * D + k];
            if (i == j) {
                if (s <= 0.0) return -1;
                M[i * D + j] = std::sqrt(s);
            } else {
                M[i * D + j] = s / M[j * D + j];
            }
        }
    }
    return 0;
}

// ----- Forward solve: L y = v, where L is lower triangular --------------
// L is the Cholesky factor (stored in lower triangle of D×D buffer); upper
// triangle is ignored.  Result returned in y; caller must size y to D.
static void vbgmm_forward_solve(const double* L, const double* v,
                                double* y, int D) {
    for (int i = 0; i < D; i++) {
        double s = v[i];
        for (int k = 0; k < i; k++) s -= L[i * D + k] * y[k];
        y[i] = s / L[i * D + i];
    }
}

// ----- ln |L L^T| = 2 Σ ln(diag(L)) ---------------------------------------
static double vbgmm_log_det_chol(const double* L, int D) {
    double s = 0.0;
    for (int i = 0; i < D; i++) s += std::log(L[i * D + i]);
    return 2.0 * s;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// RunVBGMM
//
// Run Variational-Bayes GMM on a packed feature buffer with hard initial
// labels.  Modifies labels[] in-place to the converged hard assignment
// (argmax of the variational posterior).  Returns the number of clusters
// surviving auto-pruning (clusters whose effective N_k < kPruneThresh
// after convergence are removed and their members reassigned to argmax
// over the survivors).
//
// Parameters
//   data         — [N × D] float, sample-major (row p starts at p*D)
//   labels       — [N] int (in/out), values in [0, K_init)
//   K_init       — number of initial clusters (max K, can shrink via prune)
//   N            — spike count
//   D            — feature dim
//   maxIter      — VB iteration cap (default 50)
//   convTol      — convergence: max |Δr| across all (n, k) below this stops
//
// Hyperparameters (constants in the function body — tweakable later):
//   alpha0      — Dirichlet concentration (1.0 = uniform; <1 favours sparsity)
//   beta0       — Normal prior strength on means
//   nu0         — Wishart d.o.f. (must be ≥ D)
//   m0          — Wishart prior mean = data mean (computed inside)
//   W0inv       — Wishart inverse scale = data variance × I (computed inside)
// ---------------------------------------------------------------------------
static int RunVBGMM(const float* data, int* labels, int K_init,
                    int N, int D, int maxIter = 50,
                    double convTol = 1e-3)
{
    if (K_init < 1 || N < 1 || D < 1) return std::max(1, K_init);

    // Hyperparameters from CLI globals (defaults set in KlustaKwik.cpp).
    // alpha0: Dirichlet concentration.  Lowering (e.g., 0.1) makes the
    //         prior favour sparser solutions — clusters with weak evidence
    //         get pruned more aggressively.
    // beta0:  Normal prior strength on means.
    // nu0:    Wishart d.o.f.  Must exceed D - 1 for a proper Wishart.  We
    //         enforce a floor of D + 0.5 to keep numerics stable when the
    //         user picks a small offset on a high-dim problem.
    const double alpha0 = static_cast<double>(VBGMMAlpha0);
    const double beta0  = static_cast<double>(VBGMMBeta0);
    double       nu0    = static_cast<double>(D)
                        + static_cast<double>(VBGMMNu0Offset);
    if (nu0 < D + 0.5) nu0 = D + 0.5;

    // Data mean (m0) and per-feature variance (for W0inv = mean_var * I).
    std::vector<double> m0(D, 0.0);
    for (int p = 0; p < N; p++)
        for (int d = 0; d < D; d++)
            m0[d] += data[p * D + d];
    for (int d = 0; d < D; d++) m0[d] /= N;

    double meanVar = 0.0;
    for (int p = 0; p < N; p++)
        for (int d = 0; d < D; d++) {
            const double v = data[p * D + d] - m0[d];
            meanVar += v * v;
        }
    meanVar /= (N * D);
    if (!(meanVar > 0.0)) meanVar = 1.0;  // pathological data; fall back

    // ── W0inv: per-cluster Wishart inverse-scale priors ────────────────
    //
    // Mode 0 (isotropic global): W0inv[k] = meanVar * I for every k.  The
    // prior expects all clusters to have the same per-feature variance,
    // ignoring that close-shank vs far-shank neurons have very different
    // per-channel variance signatures.
    //
    // Mode 1 (per-cluster diagonal empirical): W0inv[k] = diag of cluster
    // k's per-feature variance under the warm-start labels, blended with
    // a small isotropic floor for numerical safety.  Captures diagonal
    // anisotropy (different per-feature variance scales per cluster) but
    // not channel correlations within a cluster.
    //
    // Mode 2 (per-cluster FULL covariance empirical): W0inv[k] = full
    // empirical covariance of cluster k spikes under warm-start labels,
    // blended with isotropic floor on the diagonal.  Captures BOTH the
    // diagonal anisotropy of mode 1 AND off-diagonal correlations —
    // i.e., the channel-pattern signature of a neuron's spatial
    // waveform.  Higher numerical risk on small clusters (rank-deficient
    // empirical cov) — fallback to isotropic kicks in when N_k < D + 1.
    //
    // The blend term (VBGMMPriorBlend, default 0.1) adds blend*meanVar to
    // each cluster's prior diagonal, ensuring positive-definiteness even
    // when empirical cov is ill-conditioned.
    //
    // Stored as [K_init × D × D] dense (most off-diagonals zero in mode 0
    // and 1; populated in mode 2) for uniform indexing in init and M-step.
    std::vector<double> W0inv(static_cast<size_t>(K_init) * D * D, 0.0);
    {
        const double blend     = std::clamp(static_cast<double>(VBGMMPriorBlend), 0.0, 1.0);
        const double iso_floor = blend * meanVar;  // additive isotropic floor

        if (VBGMMPriorMode == 1 || VBGMMPriorMode == 2) {
            // Per-cluster empirical {variance | covariance} from warm-start
            // labels.  Common preamble: compute per-cluster N_k and means.
            std::vector<double> Nk_init(K_init, 0.0);
            std::vector<double> mean_init(static_cast<size_t>(K_init) * D, 0.0);
            for (int p = 0; p < N; p++) {
                const int k = labels[p];
                if (k < 0 || k >= K_init) continue;
                Nk_init[k] += 1.0;
                for (int d = 0; d < D; d++)
                    mean_init[k * D + d] += data[p * D + d];
            }
            for (int k = 0; k < K_init; k++) {
                if (Nk_init[k] < 1.0) continue;
                for (int d = 0; d < D; d++)
                    mean_init[k * D + d] /= Nk_init[k];
            }

            if (VBGMMPriorMode == 1) {
                // ── Mode 1: per-cluster DIAGONAL empirical ──────────────
                std::vector<double> emp_var(static_cast<size_t>(K_init) * D, 0.0);
                for (int p = 0; p < N; p++) {
                    const int k = labels[p];
                    if (k < 0 || k >= K_init) continue;
                    for (int d = 0; d < D; d++) {
                        const double v = data[p * D + d] - mean_init[k * D + d];
                        emp_var[k * D + d] += v * v;
                    }
                }
                for (int k = 0; k < K_init; k++) {
                    if (Nk_init[k] >= D + 1.0) {
                        for (int d = 0; d < D; d++) {
                            const double v_emp = emp_var[k * D + d] / Nk_init[k];
                            // (1 - blend) * empirical + blend * isotropic floor
                            W0inv[k * D * D + d * D + d] =
                                  (1.0 - blend) * v_emp + iso_floor;
                        }
                    } else {
                        // Fall back to isotropic for under-populated clusters.
                        for (int d = 0; d < D; d++)
                            W0inv[k * D * D + d * D + d] = meanVar;
                    }
                }
            } else {
                // ── Mode 2: per-cluster FULL covariance empirical ───────
                // Captures off-diagonal correlations (the channel-pattern
                // signature of a neuron's spatial waveform).  Requires
                // N_k >= D + 1 for a non-rank-deficient empirical cov;
                // smaller clusters fall back to isotropic.  Numerical
                // safety net: the blend term puts a positive isotropic
                // floor on every diagonal, keeping the matrix PD even if
                // empirical cov is ill-conditioned.
                //
                // Memory: [K × D²] doubles; for typical (K=20, D=6),
                // ~6 KB total — trivial.  For (K=50, D=21), ~180 KB.
                std::vector<double> emp_cov(static_cast<size_t>(K_init) * D * D, 0.0);
                for (int p = 0; p < N; p++) {
                    const int k = labels[p];
                    if (k < 0 || k >= K_init) continue;
                    for (int i = 0; i < D; i++) {
                        const double di = data[p * D + i] - mean_init[k * D + i];
                        for (int j = 0; j < D; j++) {
                            const double dj = data[p * D + j] - mean_init[k * D + j];
                            emp_cov[k * D * D + i * D + j] += di * dj;
                        }
                    }
                }
                for (int k = 0; k < K_init; k++) {
                    if (Nk_init[k] >= D + 1.0) {
                        const double inv_N = 1.0 / Nk_init[k];
                        for (int i = 0; i < D; i++) {
                            for (int j = 0; j < D; j++) {
                                const double c = emp_cov[k * D * D + i * D + j] * inv_N;
                                // (1 - blend) * empirical_cov_k for all (i,j),
                                // plus isotropic floor on diagonal only.
                                W0inv[k * D * D + i * D + j] = (1.0 - blend) * c;
                            }
                            W0inv[k * D * D + i * D + i] += iso_floor;
                        }
                    } else {
                        // Isotropic fallback for under-populated clusters.
                        for (int d = 0; d < D; d++)
                            W0inv[k * D * D + d * D + d] = meanVar;
                    }
                }
            }
        } else {
            // Mode 0 (isotropic global): every cluster gets meanVar * I.
            for (int k = 0; k < K_init; k++)
                for (int d = 0; d < D; d++)
                    W0inv[k * D * D + d * D + d] = meanVar;
        }
    }

    int K = K_init;

    // Variational state.  Indexed by [k] up to K (current alive count).
    // We don't compact k indices when pruning — instead we maintain an
    // alive[] mask and skip dead clusters in all loops.
    std::vector<double> alpha(K, alpha0);
    std::vector<double> beta (K, beta0);
    std::vector<double> nu   (K, nu0);
    std::vector<double> m    (K * D, 0.0);
    std::vector<double> Winv (K * D * D, 0.0);
    std::vector<double> Wchol(K * D * D, 0.0);
    std::vector<int>    alive(K, 1);

    std::vector<double> rResp(N * K, 0.0);  // responsibilities r[n, k]

    // ── Initialise from hard labels ──────────────────────────────────────
    // For each cluster k: set N_k from label count, x_bar_k from member mean,
    // S_k from member empirical scatter; apply M-step parameter updates.
    {
        std::vector<double> Nk(K, 0.0);
        std::vector<double> xbar(K * D, 0.0);
        for (int p = 0; p < N; p++) {
            const int k = labels[p];
            if (k < 0 || k >= K) continue;
            Nk[k] += 1.0;
            for (int d = 0; d < D; d++) xbar[k * D + d] += data[p * D + d];
        }
        for (int k = 0; k < K; k++) {
            if (Nk[k] < 1.0) { alive[k] = 0; continue; }
            for (int d = 0; d < D; d++) xbar[k * D + d] /= Nk[k];
        }
        // Empirical scatter S_k = (1/N_k) Σ_n (x_n - x_bar_k)(x_n - x_bar_k)^T
        std::vector<double> Sk(K * D * D, 0.0);
        for (int p = 0; p < N; p++) {
            const int k = labels[p];
            if (k < 0 || k >= K || !alive[k]) continue;
            for (int i = 0; i < D; i++) {
                const double di = data[p * D + i] - xbar[k * D + i];
                for (int j = i; j < D; j++) {  // upper triangle only
                    const double dj = data[p * D + j] - xbar[k * D + j];
                    Sk[k * D * D + i * D + j] += di * dj;
                }
            }
        }
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            for (int i = 0; i < D; i++)
                for (int j = i; j < D; j++) {
                    Sk[k * D * D + i * D + j] /= Nk[k];
                    if (j > i) Sk[k * D * D + j * D + i] = Sk[k * D * D + i * D + j];
                }
        }
        // Apply M-step parameter updates from these initial sufficient stats.
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            alpha[k] = alpha0 + Nk[k];
            beta [k] = beta0  + Nk[k];
            nu   [k] = nu0    + Nk[k];
            for (int d = 0; d < D; d++)
                m[k * D + d] = (beta0 * m0[d] + Nk[k] * xbar[k * D + d]) / beta[k];
            // W_k^{-1} = W0inv + N_k S_k + (β0 N_k / (β0 + N_k))(x_bar - m0)(x_bar - m0)^T
            const double w = (beta0 * Nk[k]) / (beta0 + Nk[k]);
            for (int i = 0; i < D; i++) {
                const double dxi = xbar[k * D + i] - m0[i];
                for (int j = 0; j < D; j++) {
                    const double dxj = xbar[k * D + j] - m0[j];
                    Winv[k * D * D + i * D + j] =
                        W0inv[k * D * D + i * D + j]
                      + Nk[k] * Sk[k * D * D + i * D + j]
                      + w * dxi * dxj;
                }
            }
            // Cholesky for fast quad-form / log-det later.
            std::vector<double> tmp(Winv.begin() + k * D * D,
                                    Winv.begin() + (k + 1) * D * D);
            if (vbgmm_cholesky(tmp.data(), D) != 0) {
                alive[k] = 0;  // shouldn't happen with W0inv regularisation
                continue;
            }
            std::copy(tmp.begin(), tmp.end(), Wchol.begin() + k * D * D);
        }
    }

    // ── VB iterations ─────────────────────────────────────────────────────
    std::vector<double> rPrev(N * K, 0.0);
    std::vector<double> y(D);
    int iter;
    for (iter = 0; iter < maxIter; iter++) {
        // -- E-step: compute log responsibilities (then softmax) ---------
        // log α̂ = ψ(α_k) - ψ(Σ_k' α_k')
        double alphaSum = 0.0;
        for (int k = 0; k < K; k++) if (alive[k]) alphaSum += alpha[k];
        const double psiAlphaSum = vbgmm_digamma(alphaSum);

        // Per-cluster precomputed terms: E[ln π_k], E[ln |Λ_k|]
        std::vector<double> ElnPi   (K, 0.0);
        std::vector<double> ElnLambda(K, 0.0);
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            ElnPi[k] = vbgmm_digamma(alpha[k]) - psiAlphaSum;
            // E[ln |Λ_k|] = Σ_d ψ((ν_k + 1 - d)/2) + D ln(2) - ln |W^{-1}_k|
            // Note: ln|W_k| = -ln|W^{-1}_k| = -2 Σ ln(diag(L)) for L = chol(W^{-1})
            double s = 0.0;
            for (int d = 1; d <= D; d++)
                s += vbgmm_digamma((nu[k] + 1.0 - d) / 2.0);
            ElnLambda[k] = s + D * std::log(2.0)
                         - vbgmm_log_det_chol(&Wchol[k * D * D], D);
        }

        // For each spike, compute log ρ[n, k] then softmax to r[n, k].
        for (int p = 0; p < N; p++) {
            double maxLog = -std::numeric_limits<double>::infinity();
            std::vector<double> logRho(K, -std::numeric_limits<double>::infinity());
            for (int k = 0; k < K; k++) {
                if (!alive[k]) continue;
                // E[(x - μ_k)^T Λ_k (x - μ_k)] = D/β_k + ν_k * (x - m_k)^T W_k (x - m_k)
                // (x - m_k)^T W_k (x - m_k) computed via forward-solve on W^{-1}'s Cholesky:
                //   z = L^{-1} (x - m_k) → quad = ||z||²,  since W = (LL^T)^{-1} → x^T W x = ||L^{-1} x||²
                std::vector<double> diff(D);
                for (int d = 0; d < D; d++)
                    diff[d] = data[p * D + d] - m[k * D + d];
                vbgmm_forward_solve(&Wchol[k * D * D], diff.data(), y.data(), D);
                double quad = 0.0;
                for (int d = 0; d < D; d++) quad += y[d] * y[d];
                const double EmahalSq = D / beta[k] + nu[k] * quad;
                logRho[k] = ElnPi[k] + 0.5 * ElnLambda[k]
                          - 0.5 * D * std::log(2.0 * M_PI)
                          - 0.5 * EmahalSq;
                if (logRho[k] > maxLog) maxLog = logRho[k];
            }
            // softmax
            double Zsum = 0.0;
            for (int k = 0; k < K; k++) {
                if (!alive[k]) { rResp[p * K + k] = 0.0; continue; }
                rResp[p * K + k] = std::exp(logRho[k] - maxLog);
                Zsum += rResp[p * K + k];
            }
            if (Zsum > 0.0) {
                for (int k = 0; k < K; k++) rResp[p * K + k] /= Zsum;
            } else {
                // Numerical underflow: fall back to uniform over alive
                int nAlive = 0;
                for (int k = 0; k < K; k++) if (alive[k]) nAlive++;
                if (nAlive > 0) {
                    const double u = 1.0 / nAlive;
                    for (int k = 0; k < K; k++) rResp[p * K + k] = alive[k] ? u : 0.0;
                }
            }
        }

        // -- Convergence check (max |Δr| over all (n, k)) ----------------
        if (iter > 0) {
            double maxDelta = 0.0;
            for (int p = 0; p < N; p++)
                for (int k = 0; k < K; k++) {
                    const double d = std::abs(rResp[p * K + k] - rPrev[p * K + k]);
                    if (d > maxDelta) maxDelta = d;
                }
            if (maxDelta < convTol) break;
        }
        std::copy(rResp.begin(), rResp.end(), rPrev.begin());

        // -- M-step: sufficient statistics → parameter updates -----------
        std::vector<double> Nk(K, 0.0);
        std::vector<double> xbar(K * D, 0.0);
        for (int p = 0; p < N; p++)
            for (int k = 0; k < K; k++) {
                if (!alive[k]) continue;
                const double r = rResp[p * K + k];
                if (r <= 0.0) continue;
                Nk[k] += r;
                for (int d = 0; d < D; d++) xbar[k * D + d] += r * data[p * D + d];
            }
        for (int k = 0; k < K; k++) {
            if (!alive[k] || Nk[k] <= 0.0) { alive[k] = 0; continue; }
            for (int d = 0; d < D; d++) xbar[k * D + d] /= Nk[k];
        }

        std::vector<double> Sk(K * D * D, 0.0);
        for (int p = 0; p < N; p++)
            for (int k = 0; k < K; k++) {
                if (!alive[k]) continue;
                const double r = rResp[p * K + k];
                if (r <= 0.0) continue;
                for (int i = 0; i < D; i++) {
                    const double di = data[p * D + i] - xbar[k * D + i];
                    for (int j = i; j < D; j++) {
                        const double dj = data[p * D + j] - xbar[k * D + j];
                        Sk[k * D * D + i * D + j] += r * di * dj;
                    }
                }
            }
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            for (int i = 0; i < D; i++)
                for (int j = i; j < D; j++) {
                    Sk[k * D * D + i * D + j] /= Nk[k];
                    if (j > i) Sk[k * D * D + j * D + i] = Sk[k * D * D + i * D + j];
                }
        }

        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            alpha[k] = alpha0 + Nk[k];
            beta [k] = beta0  + Nk[k];
            nu   [k] = nu0    + Nk[k];
            for (int d = 0; d < D; d++)
                m[k * D + d] = (beta0 * m0[d] + Nk[k] * xbar[k * D + d]) / beta[k];
            const double w = (beta0 * Nk[k]) / (beta0 + Nk[k]);
            for (int i = 0; i < D; i++) {
                const double dxi = xbar[k * D + i] - m0[i];
                for (int j = 0; j < D; j++) {
                    const double dxj = xbar[k * D + j] - m0[j];
                    Winv[k * D * D + i * D + j] =
                        W0inv[k * D * D + i * D + j]
                      + Nk[k] * Sk[k * D * D + i * D + j]
                      + w * dxi * dxj;
                }
            }
            std::vector<double> tmp(Winv.begin() + k * D * D,
                                    Winv.begin() + (k + 1) * D * D);
            if (vbgmm_cholesky(tmp.data(), D) != 0) {
                alive[k] = 0;
                continue;
            }
            std::copy(tmp.begin(), tmp.end(), Wchol.begin() + k * D * D);
        }

        // -- Auto-prune clusters with effective N below 1.0 spike --------
        // (At convergence the threshold could be tighter, e.g., 0.5; during
        // iteration we leave a margin to allow oscillating clusters to
        // recover before being killed.)
        for (int k = 0; k < K; k++) {
            if (alive[k] && Nk[k] < 1.0) alive[k] = 0;
        }
    }

    // ── Hard-assign by argmax over alive responsibilities ─────────────────
    int nAliveFinal = 0;
    for (int k = 0; k < K; k++) if (alive[k]) nAliveFinal++;
    if (nAliveFinal < 1) {
        // Pathological: nothing survived.  Punt by assigning everything to
        // cluster 0 and letting downstream phases decide.
        for (int p = 0; p < N; p++) labels[p] = 0;
        return 1;
    }
    for (int p = 0; p < N; p++) {
        double best = -1.0;
        int bestK = 0;
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            const double r = rResp[p * K + k];
            if (r > best) { best = r; bestK = k; }
        }
        labels[p] = bestK;
    }

    return nAliveFinal;
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
// ---------------------------------------------------------------------------
// RunPhase2bMode3Chunk — Phase 2b mode 3 per-chunk driver.
//
// Iterative residual-PCA refinement + dominant-channel xcorr realignment
// for one chunk's spikes.  See the long comment at the Phase2bMode == 3
// dispatch (in the Phase 2b chunk loop) for the full algorithm rationale.
//
// Inputs:
//   Ks                — the per-chunk sub-KK (warm-started from Phase 2a)
//   pts               — global spike indices in this chunk [nPts]
//   nChan             — channel count for this group
//   nSamplesPerSpike  — sample count per spike window
//   nStart            — initial cluster count (incl. noise=cluster 0)
//
// Side effects:
//   * Modifies Ks.Class[], Ks.ClassAlive[]: refined per-chunk labels.
//   * Modifies this->m_cumShift[] for indices in `pts`: per-spike
//     realignment shifts (additive, capped at ±m_timeShiftMaxAbs).
//
// Reads .spk via TimeShiftReadSpikeWave (mmap path is thread-safe; the
// FILE* fallback is serialised here with a local critical section).
//
// Convergence: stops early when the fraction of label-changing spikes in
// an iteration falls below ResidualPCAConvTol, or after ResidualPCAIter
// outer iterations.
// ---------------------------------------------------------------------------
void KK::RunPhase2bMode3Chunk(KK& Ks, const std::vector<int>& pts,
                              int nChan, int nSamplesPerSpike,
                              int nStart, int chunkIdx)
{
    const int nPts = static_cast<int>(pts.size());
    if (nPts < 5 || nChan <= 0 || nSamplesPerSpike <= 0) return;
    if (!m_timeShiftReady) {
        // mmap/FILE not set up — fall back to mode 0 behaviour silently.
        Ks.MStep(); Ks.EStep();
        Ks.RunEMLoop(true, false, 0, "[2b-m3-fallback]");
        return;
    }

    const int waveSamples = nChan * nSamplesPerSpike;
    const int nSubDims    = Ks.nDims;          // VBGMM feature width
    const int K_init      = nStart;            // initial K (noise + real)

    // Sanitise hyperparameters from CLI globals
    const int maxIter      = std::max(1, ResidualPCAIter);
    const int nResidComp   = std::clamp(ResidualPCAComponents, 1, 8);
    const int nDomCh       = std::clamp(ResidualPCADominantChannels, 1,
                                        std::max(1, nChan));
    const int subK         = std::clamp(ResidualPCASubK, 2, 8);
    const double convTol   = std::max(0.0,
                                     static_cast<double>(ResidualPCAConvTol));

    // Entry banner.  Each per-chunk worker emits one of these — when the
    // chunk loop is parallel the lines interleave across threads, which is
    // why every line is self-contained with its chunk index.
    Output("[Phase 2b m3] chunk %d START: %d spikes, %d sub-dims, K_init=%d, "
           "maxIter=%d, K_resid=%d, K_sub=%d, domCh=%d, convTol=%.4f, "
           "maxShift=%d\n",
           chunkIdx, nPts, nSubDims, K_init, maxIter, nResidComp, subK,
           nDomCh, convTol, m_timeShiftMaxAbs);

    // ── Read all chunk spikes into a local buffer ───────────────────────
    // Bulk-read upfront so the inner loop has random access without per-spike
    // file ops.  Memory cost: nPts × waveSamples × 2 bytes — for a typical
    // chunk of 50k spikes × 336 samples × 2B = ~32 MiB per chunk worker.
    std::vector<int16_t> waveBuf(static_cast<size_t>(nPts) * waveSamples);
    {
        int16_t scratch[2];  // unused; TimeShiftReadSpikeWave writes directly
        (void)scratch;
        bool needCrit = (m_timeShiftSpkMap == nullptr);  // FILE* path → serial
        if (needCrit) {
            #pragma omp critical (KK_TimeShiftReadSpikeWave)
            {
                for (int i = 0; i < nPts; i++) {
                    int16_t* dst = waveBuf.data() +
                                   static_cast<size_t>(i) * waveSamples;
                    if (!TimeShiftReadSpikeWave(pts[i], waveSamples, dst))
                        std::fill(dst, dst + waveSamples, 0);
                }
            }
        } else {
            // mmap is reentrant — issue reads in parallel-safe loop
            for (int i = 0; i < nPts; i++) {
                int16_t* dst = waveBuf.data() +
                               static_cast<size_t>(i) * waveSamples;
                if (!TimeShiftReadSpikeWave(pts[i], waveSamples, dst))
                    std::fill(dst, dst + waveSamples, 0);
            }
        }
    }

    // ── Iteration buffers ───────────────────────────────────────────────
    std::vector<int>    prevLabels(nPts), labels(nPts);
    std::vector<double> meanWave(static_cast<size_t>(waveSamples), 0.0);
    std::vector<double> residual;                  // re-sized per cluster
    std::vector<double> covR(static_cast<size_t>(waveSamples) *
                              waveSamples, 0.0);
    std::vector<double> eigvecs;                   // [K * waveSamples]
    std::vector<double> eigvals;                   // [K]
    std::vector<float>  subFeat;                   // [N_c * nResidComp]
    std::vector<int>    subLabels;
    std::vector<int>    members;
    std::vector<int>    domChans;

    // Seed labels from the warm-started Ks.Class[].
    for (int i = 0; i < nPts; i++) labels[i] = Ks.Class[i];

    int totalSplitsApplied = 0;
    int totalRealigned     = 0;
    int actualItersRun     = 0;

    for (int iter = 0; iter < maxIter; iter++) {
        prevLabels = labels;
        ++actualItersRun;

        // Snapshot alive-cluster count before this iter's VBGMM
        int aliveBefore = 0;
        {
            std::vector<int> seen; seen.reserve(64);
            for (int v : labels) {
                if (v <= 0) continue;
                bool found = false;
                for (int s : seen) if (s == v) { found = true; break; }
                if (!found) seen.push_back(v);
            }
            aliveBefore = static_cast<int>(seen.size());
        }

        // ── Step 1: VBGMM on chunk features with current label warm-start ─
        // First iter: warm-start from Phase 2a (already in labels).  Later
        // iters: warm-start from the previous iter's split-and-realign
        // labels (re-featurized via Data[] which doesn't change inside this
        // chunk loop, but the labels carry forward the structure we found).
        int vbgmmSurvivors = 0;
        {
            std::vector<int> lbuf = labels;
            // Cluster-id space may have expanded by splits in previous
            // iter; bound K_init by the labels actually present.
            int kCur = K_init;
            for (int v : lbuf) if (v + 1 > kCur) kCur = v + 1;
            vbgmmSurvivors = RunVBGMM(
                Ks.Data.m_Data, lbuf.data(), kCur, nPts, nSubDims,
                VBGMMMaxIter, static_cast<double>(VBGMMConvTol));
            labels = lbuf;
        }

        // Per-iter counters
        int splitsThisIter      = 0;  // # clusters that got split
        int newSubClustersIter  = 0;  // # new IDs created from splits
        int clustersExamined    = 0;
        int clustersTooSmall    = 0;
        int realignThisIter     = 0;

        // ── Step 2/3: per-cluster residual-PCA + split via residual VBGMM ─
        // Find max alive cluster id to know where to assign new sub-ids.
        int nextId = 0;
        for (int v : labels) if (v + 1 > nextId) nextId = v + 1;

        // Collect ids of currently-alive non-noise clusters (snapshot
        // BEFORE we start splitting — we don't recurse into freshly-made
        // sub-clusters in this same iter).
        std::vector<int> activeIds;
        {
            std::vector<int> seen;
            seen.reserve(64);
            for (int v : labels) {
                if (v <= 0) continue;
                bool found = false;
                for (int s : seen) if (s == v) { found = true; break; }
                if (!found) seen.push_back(v);
            }
            activeIds = std::move(seen);
        }

        for (int cid : activeIds) {
            ++clustersExamined;
            // Gather members of cluster cid
            members.clear();
            for (int i = 0; i < nPts; i++)
                if (labels[i] == cid) members.push_back(i);
            const int Nc = static_cast<int>(members.size());
            // Need enough spikes for stable mean + residual PCA: a soft
            // floor of 2·waveSamples keeps the per-cluster covariance
            // non-degenerate.  Below that, attempting to split is more
            // likely to overfit noise than discover real structure.
            if (Nc < std::max(20, 2 * nResidComp)) { ++clustersTooSmall; continue; }

            // Mean waveform of the cluster
            std::fill(meanWave.begin(), meanWave.end(), 0.0);
            for (int m : members) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(m) * waveSamples;
                for (int j = 0; j < waveSamples; j++)
                    meanWave[j] += static_cast<double>(w[j]);
            }
            const double invN = 1.0 / Nc;
            for (int j = 0; j < waveSamples; j++) meanWave[j] *= invN;

            // Build residuals: residual[i, j] = waveBuf[member_i, j] − mean[j]
            residual.assign(static_cast<size_t>(Nc) * waveSamples, 0.0);
            for (int i = 0; i < Nc; i++) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(members[i]) * waveSamples;
                double* rrow = residual.data() +
                               static_cast<size_t>(i) * waveSamples;
                for (int j = 0; j < waveSamples; j++)
                    rrow[j] = static_cast<double>(w[j]) - meanWave[j];
            }

            // Residual covariance Σ = (1/(N-1)) Rᵀ R  in [waveSamples × waveSamples]
            std::fill(covR.begin(), covR.end(), 0.0);
            const double cscale = 1.0 / std::max(1, Nc - 1);
            for (int i = 0; i < Nc; i++) {
                const double* rrow = residual.data() +
                                     static_cast<size_t>(i) * waveSamples;
                for (int a = 0; a < waveSamples; a++) {
                    const double ra = rrow[a];
                    double* crow = covR.data() +
                                   static_cast<size_t>(a) * waveSamples;
                    for (int b = 0; b < waveSamples; b++)
                        crow[b] += ra * rrow[b];
                }
            }
            for (size_t k = 0; k < covR.size(); k++) covR[k] *= cscale;

            // Top-K eigenvectors via power iteration with deflation
            TopKEigenPowerDeflation(covR, waveSamples, nResidComp,
                                    /*maxIter=*/50, /*relTol=*/1e-4,
                                    eigvecs, eigvals);

            // Project residuals onto the top eigenvectors → sub-features
            // subFeat[i * nResidComp + k] = ⟨residual[i], eigvecs[k]⟩
            subFeat.assign(static_cast<size_t>(Nc) * nResidComp, 0.0f);
            for (int i = 0; i < Nc; i++) {
                const double* rrow = residual.data() +
                                     static_cast<size_t>(i) * waveSamples;
                for (int k = 0; k < nResidComp; k++) {
                    const double* ev = eigvecs.data() +
                                       static_cast<size_t>(k) * waveSamples;
                    double s = 0.0;
                    for (int j = 0; j < waveSamples; j++) s += rrow[j] * ev[j];
                    subFeat[static_cast<size_t>(i) * nResidComp + k] =
                        static_cast<float>(s);
                }
            }

            // VBGMM on sub-features: initialise everyone in cluster 0 of
            // the sub-space, K_init = subK (Dirichlet prior shrinks K).
            subLabels.assign(static_cast<size_t>(Nc), 0);
            // Spread initial labels round-robin so VBGMM has K seeds to
            // grow from rather than collapsing immediately.
            for (int i = 0; i < Nc; i++)
                subLabels[static_cast<size_t>(i)] = i % subK;
            const int nSurv = RunVBGMM(subFeat.data(), subLabels.data(),
                                       subK, Nc, nResidComp,
                                       VBGMMMaxIter,
                                       static_cast<double>(VBGMMConvTol));

            if (nSurv <= 1) continue;  // no split discovered → keep cluster

            // Find the majority sub-cluster id (gets to keep cid); the rest
            // become new clusters numbered nextId, nextId+1, ...
            std::vector<int> subCounts(subK, 0);
            for (int v : subLabels) if (v >= 0 && v < subK) subCounts[v]++;
            int majority = 0, majCount = subCounts[0];
            for (int k = 1; k < subK; k++)
                if (subCounts[k] > majCount) { majCount = subCounts[k]; majority = k; }

            std::vector<int> subRemap(subK, -1);
            subRemap[majority] = cid;
            int newIdsAddedHere = 0;
            for (int k = 0; k < subK; k++) {
                if (k == majority) continue;
                if (subCounts[k] == 0)  continue;
                if (nextId >= MaxPossibleClusters) {
                    // Out of ID space — collapse remaining sub-clusters
                    // back to the original cid.  No structural loss; just
                    // skip the split for this iter.
                    subRemap[k] = cid;
                } else {
                    subRemap[k] = nextId++;
                    ++newIdsAddedHere;
                }
            }
            // Apply remap to chunk-wide labels[]
            for (int i = 0; i < Nc; i++) {
                const int sk  = subLabels[static_cast<size_t>(i)];
                const int newCid = (sk >= 0 && sk < subK) ? subRemap[sk] : cid;
                labels[members[i]] = newCid;
            }
            if (newIdsAddedHere > 0) {
                ++splitsThisIter;
                newSubClustersIter += newIdsAddedHere;
                ++totalSplitsApplied;
            }
        }

        // (Step 4 — per-spike xcorr realign — moved OUT of the iter loop.
        // Reason: TimeShiftReadSpikeWave reads the original .spk verbatim
        // (no .fil re-extraction mid-chunk for parallel-safety), so the
        // cluster mean computed in iter N+1 is identical to iter N's.
        // Per-iter xcorr would therefore return the same lag every iter
        // and either double-shift or cap-clamp.  One pass at the end on
        // the final cluster labels is both correct and matches Klusters'
        // single-shot realignSpikes behaviour.)
        (void)realignThisIter;  // intentionally always 0 inside the loop

        // ── Step 5: convergence check + per-iter banner ──────────────────
        int nChanged = 0;
        for (int i = 0; i < nPts; i++)
            if (labels[i] != prevLabels[i]) ++nChanged;
        const double frac = static_cast<double>(nChanged) / nPts;
        const bool   converged = (frac < convTol);

        Output("[Phase 2b m3] chunk %d iter %d/%d: alive %d -> VBGMM survivors %d, "
               "examined %d (%d too small), %d splits -> %d new sub-clusters, "
               "%d spike realigns, %d/%d (%.2f%%) labels changed%s\n",
               chunkIdx, iter + 1, maxIter,
               aliveBefore, vbgmmSurvivors,
               clustersExamined, clustersTooSmall,
               splitsThisIter, newSubClustersIter,
               realignThisIter,
               nChanged, nPts, 100.0 * frac,
               converged ? " [CONVERGED]" : "");

        if (converged) break;
    }

    // ──── Final per-cluster alignment (post-split-iterations) ────────────
    //
    // patch71 — Iterative on-the-spot xcorr alignment.
    //
    // Replaces the prior single-pass-with-cap-rejection design with an
    // iterative one that re-extracts each spike's waveform from .fil at
    // its updated position IMMEDIATELY after each xcorr step, and
    // re-projects features into Data[] in the same place.  Three things
    // changed from the original:
    //
    //   (a) DROP CAP-REJECTION on the cumulative shift.  The previous
    //       version refused any shift whose accumulated magnitude would
    //       exceed m_timeShiftMaxAbs (default 3), leaving spikes that
    //       needed larger total motion STUCK at their original
    //       mis-detected positions.  Visible result: ±5-sample
    //       within-cluster temporal dispersion after KlustaKwikExp
    //       completes.  Per-iter lag is naturally bounded by xcorr's
    //       maxShift parameter; cumulative motion across iters is
    //       unbounded by design — a spike needing 5 samples can get
    //       +3 in iter 0, then +2 in iter 1, then 0 in iter 2.  The
    //       only safety gate is the extraction-window bounds check
    //       (off must fit within sessionSamples).
    //
    //   (b) RE-EXTRACT AND RE-FEATURIZE ON THE SPOT.  After updating
    //       m_cumShift[gp], read the new waveform from .fil at
    //       (rawTs + cumShift - PeakSampleIndex), apply the stderiv
    //       transform if the pipeline is .pcaD/.spkD, overwrite
    //       waveBuf[i_local] so the NEXT iter's xcorr sees the
    //       re-extracted waveform, and project through the canonical
    //       PCA basis to update Data[gp].  This is what makes "merge
    //       toward the centre" converge: subsequent iters see the
    //       already-shifted waveforms and the cluster mean re-built
    //       from them is well-centred on PeakSampleIndex.
    //
    //   (c) USE INT64 rawTs from .res, NOT float-recovery from
    //       Data[timeDim].  RefeaturizeFromShifts at lines 2818-2822
    //       explicitly warns that recovering rawTs by inverse-
    //       normalising the float feature introduces up to ±13 samples
    //       of error for late-session spikes.  For a 1-hour 32 kHz
    //       session that error is ±~7 samples on the last spike —
    //       larger than the per-iter maxShift, so any code relying on
    //       float recovery never converges.  We pre-load int64 rawTs
    //       for every chunk-spike from .res ONCE at function entry and
    //       index by local i_local.
    //
    // "Merge toward the centre": the existing patch64 template pre-
    // alignment forces every cluster's mean template peak to land on
    // PeakSampleIndex (the waveform-window centre).  When two clusters
    // would merge (similar waveforms), both pre-aligned templates land
    // on the same centre; xcorr pulls both clusters' spikes there.
    // Clusters therefore merge AT the centre rather than at one
    // cluster's drifted peak.
    //
    // Disk writes (.spk / .fet / .res .pending) remain deferred to the
    // existing TimeShiftFinalize → RefeaturizeFromShifts path.  This
    // function only updates in-memory state (waveBuf, Data[], m_cumShift)
    // because the parallel chunk loop would race on the global disk
    // files otherwise.  TimeShiftFinalize is single-threaded and reads
    // the final m_cumShift values to do the actual disk write.
    int finalAligned     = 0;
    int finalLowScore    = 0;
    int finalOutOfBounds = 0;
    int alignItersRun    = 0;
    if (AlignPcaCenter == 2) {
        // ─── patch84: in-memory circular-shift PCA-centering, no m_cumShift ───
        //
        // REPLACES the xcorr iter loop + patch83 refine + patch74 monotonicity
        // entirely.  Designed to avoid the temporal-dispersion failure mode
        // that surfaces when cumulative shifts (m_cumShift) accumulate across
        // phases and survive into TimeShiftFinalize's disk writes.
        //
        // Algorithm:
        //   for iter = 0 .. alignMaxIter:
        //       for each alive cluster cid:
        //           μ_pca[f] = mean over cluster of Data[gp * nDims + f]
        //                      for f in 0 .. (basisNChan*basisNComp - 1)
        //           for each spike (member i) of cluster:
        //               for each candidate shift s in [-maxShift, +maxShift]:
        //                   scratch = circular_shift(waveBuf[i], s)
        //                   project scratch through canonical PCA basis with
        //                       dimMin_/dimRange_ normalisation
        //                   dist²(s) = sum_f (pca_f - μ_pca[f])²
        //               bestS = argmin_s dist²(s)
        //               if bestS != 0:
        //                   waveBuf[i] = circular_shift(waveBuf[i], bestS)
        //                   Data[gp]   = recomputed PCA features of new wave
        //       if no spike moved this iter: break
        //
        // What it touches:
        //   waveBuf[]      → circularly shifted in-place per spike
        //   Data[gp][f<D]  → PCA columns refreshed from shifted waveform
        //                    (NOT the time column at index nDims-1 —
        //                    circular shift doesn't change detection time)
        //
        // What it deliberately does NOT touch:
        //   m_cumShift[]   → left exactly as found.  If prior phases set
        //                    nonzero values, those are preserved; if zero,
        //                    they stay zero.  This is the design point:
        //                    TimeShiftFinalize keys off m_cumShift to write
        //                    .spk/.fet/.res, and we want it to write nothing
        //                    (for chunks running mode 2 fresh) or only what
        //                    earlier phases already settled.
        //
        // What's deliberately SKIPPED relative to modes 0/1:
        //   - raw-source verify (no .fil reads in mode 2)
        //   - patch73 pre-refresh of waveBuf from .fil (waveBuf stays in
        //     its as-loaded .spk state; circular shifts compose from there)
        //   - patch74 monotonicity check (m_cumShift unchanged so .res
        //     order is preserved by construction)
        //
        // Why circular shift is safe here: the .spk content is highpass-
        // filtered (or stderiv-transformed, which is doubly high-pass-like),
        // so there's effectively no DC and the wrap discontinuity at the
        // window edge is small.  More importantly, the PCA basis windows
        // around basisRecShift .. basisRecShift + basisData2use — typically
        // centered on the peak with margin — so a few samples of wrap at
        // the window EDGE don't enter the projection at small shift sizes.
        //
        // What the user does afterward: if they want the on-disk .spkD /
        // .fetD to reflect the alignment chosen in mode 2, they re-extract
        // out-of-band from .fil using their own tooling.  KKExp neither
        // assumes nor performs that step.
        const bool hasBasisP84 = m_timeShiftBasis.valid() &&
                                 !m_timeShiftBasis.meanShifted.empty();
        if (!hasBasisP84) {
            Output("[Phase 2b m3] chunk %d patch84 mode 2: "
                   "PCA basis not loaded — alignment skipped entirely.\n",
                   chunkIdx);
        } else {
            const int   candCanonicalP84 = m_timeShiftBasis.N;
            const int   basisNCompP84    = m_timeShiftBasis.nComp;
            const int   basisData2useP84 = m_timeShiftBasis.data2use;
            const int   basisRecShiftP84 = m_timeShiftBasis.recShift;
            const int   basisNChanP84    = m_timeShiftBasis.nChan;
            const bool  basisCenteredP84 = m_timeShiftBasis.isCentered;
            const int   nPCAFeatsP84     = basisNChanP84 * basisNCompP84;
            const int   maxShiftP84      = m_timeShiftMaxAbs;
            const int   alignMaxIterP84  = std::clamp(ResidualPCAIter, 2, 10);
            const int   nSampP84         = nSamplesPerSpike;

            const auto& meanCanonP84 =
                m_timeShiftBasis.meanShifted[
                    static_cast<size_t>(candCanonicalP84)];
            const auto& evecCanonP84 =
                m_timeShiftBasis.eigvecShifted[
                    static_cast<size_t>(candCanonicalP84)];

            std::vector<int16_t> scratchWaveP84(
                static_cast<size_t>(waveSamples));

            int p84TotalShifts = 0;
            int p84ItersRun    = 0;

            // The candidate-shift sweep and the post-commit refeaturize
            // both project a sample-major waveform through the canonical
            // PCA basis using dimMin_/dimRange_ normalisation.  Both are
            // inlined below (rather than factored into a lambda) because
            // (a) the hot path is the inner-most loop, function-call
            // overhead matters, and (b) the sweep version also needs to
            // accumulate distance² against muPca, while the refeaturize
            // version writes into Data[gp] — fusing those into a single
            // lambda would force conditional branching inside the loop.

            for (int iterP84 = 0; iterP84 < alignMaxIterP84; ++iterP84) {
                ++p84ItersRun;
                int shiftsThisIter = 0;

                // Recollect alive cluster ids from CURRENT labels (clusters
                // may have died/split between iters of the outer phase).
                std::vector<int> idsP84;
                idsP84.reserve(64);
                for (int v : labels) {
                    if (v <= 0) continue;
                    bool found = false;
                    for (int s : idsP84)
                        if (s == v) { found = true; break; }
                    if (!found) idsP84.push_back(v);
                }

                for (int cid : idsP84) {
                    std::vector<int> members;
                    for (int i = 0; i < nPts; ++i)
                        if (labels[i] == cid) members.push_back(i);
                    const int Nc = static_cast<int>(members.size());
                    if (Nc < 3) continue;

                    // μ_pca over cluster from current Data[]
                    std::vector<double> muPca(
                        static_cast<size_t>(nPCAFeatsP84), 0.0);
                    int contributors = 0;
                    for (int m : members) {
                        const int gp = pts[static_cast<size_t>(m)];
                        if (gp < 0) continue;
                        const float* row = Data.m_Data + gp * nDims;
                        for (int f = 0; f < nPCAFeatsP84; ++f)
                            muPca[static_cast<size_t>(f)] +=
                                static_cast<double>(row[f]);
                        ++contributors;
                    }
                    if (contributors < 3) continue;
                    const double invN = 1.0 / contributors;
                    for (int f = 0; f < nPCAFeatsP84; ++f)
                        muPca[static_cast<size_t>(f)] *= invN;

                    for (int sp = 0; sp < Nc; ++sp) {
                        const int iLocal = members[static_cast<size_t>(sp)];
                        const int gp     = pts[static_cast<size_t>(iLocal)];
                        if (gp < 0) continue;

                        int16_t* wave = waveBuf.data()
                            + static_cast<ptrdiff_t>(iLocal)
                            * static_cast<ptrdiff_t>(waveSamples);

                        int    bestS    = 0;
                        double bestDist = std::numeric_limits<double>::infinity();

                        for (int s = -maxShiftP84; s <= maxShiftP84; ++s) {
                            // Circular shift wave by s into scratchWaveP84.
                            // Sample-major: scratch[((t + s) mod N) * nChan + c]
                            //               = wave[t * nChan + c]
                            for (int t = 0; t < nSampP84; ++t) {
                                int tNew = (t + s) % nSampP84;
                                if (tNew < 0) tNew += nSampP84;
                                const int rowDst = tNew * nChan;
                                const int rowSrc = t    * nChan;
                                for (int c = 0; c < nChan; ++c)
                                    scratchWaveP84[
                                        static_cast<size_t>(rowDst + c)] =
                                            wave[rowSrc + c];
                            }

                            // PCA project scratchWaveP84 and accumulate
                            // distance² to muPca, inline (lambda-free for
                            // speed — this is the inner-most loop).
                            double dist2 = 0.0;
                            for (int ch = 0; ch < basisNChanP84; ++ch) {
                                const auto& mu = meanCanonP84[
                                    static_cast<size_t>(ch)];
                                const auto& ev = evecCanonP84[
                                    static_cast<size_t>(ch)];
                                for (int k = 0; k < basisNCompP84; ++k) {
                                    double val = 0.0;
                                    for (int u = 0; u < basisData2useP84; ++u) {
                                        const int sIdx = basisRecShiftP84 + u;
                                        double x = static_cast<double>(
                                            scratchWaveP84[
                                                static_cast<size_t>(
                                                    sIdx * nChan + ch)]);
                                        if (basisCenteredP84)
                                            x -= mu[static_cast<size_t>(u)];
                                        val += ev[static_cast<size_t>(
                                            k * basisData2useP84 + u)] * x;
                                    }
                                    const int featIdx = ch * basisNCompP84 + k;
                                    if (featIdx >= nPCAFeatsP84 ||
                                        featIdx >= static_cast<int>(
                                            dimMin_.size()))
                                        continue;
                                    const double normVal =
                                        (static_cast<float>(val)
                                       - dimMin_[featIdx])
                                      * dimRange_[featIdx];
                                    const double diff = normVal
                                      - muPca[static_cast<size_t>(featIdx)];
                                    dist2 += diff * diff;
                                }
                            }

                            if (dist2 < bestDist) {
                                bestDist = dist2;
                                bestS    = s;
                            }
                        }

                        if (bestS == 0) continue;  // already at the centre

                        // Commit the chosen circular shift to waveBuf in
                        // place (via scratch since source and dest overlap).
                        for (int t = 0; t < nSampP84; ++t) {
                            int tNew = (t + bestS) % nSampP84;
                            if (tNew < 0) tNew += nSampP84;
                            const int rowDst = tNew * nChan;
                            const int rowSrc = t    * nChan;
                            for (int c = 0; c < nChan; ++c)
                                scratchWaveP84[
                                    static_cast<size_t>(rowDst + c)] =
                                        wave[rowSrc + c];
                        }
                        std::memcpy(wave, scratchWaveP84.data(),
                                    static_cast<size_t>(waveSamples)
                                  * sizeof(int16_t));

                        // Refresh Data[gp]'s PCA features (only — the
                        // timestamp at index nDims-1 stays put because
                        // circular shift does not change detection time).
                        float* dataRow = Data.m_Data + gp * nDims;
                        for (int ch = 0; ch < basisNChanP84; ++ch) {
                            const auto& mu = meanCanonP84[
                                static_cast<size_t>(ch)];
                            const auto& ev = evecCanonP84[
                                static_cast<size_t>(ch)];
                            for (int k = 0; k < basisNCompP84; ++k) {
                                double val = 0.0;
                                for (int u = 0; u < basisData2useP84; ++u) {
                                    const int sIdx = basisRecShiftP84 + u;
                                    double x = static_cast<double>(
                                        wave[sIdx * nChan + ch]);
                                    if (basisCenteredP84)
                                        x -= mu[static_cast<size_t>(u)];
                                    val += ev[static_cast<size_t>(
                                        k * basisData2useP84 + u)] * x;
                                }
                                const int featIdx = ch * basisNCompP84 + k;
                                if (featIdx >= nPCAFeatsP84 ||
                                    featIdx >= static_cast<int>(
                                        dimMin_.size()))
                                    continue;
                                dataRow[featIdx] =
                                    (static_cast<float>(val)
                                   - dimMin_[featIdx])
                                  * dimRange_[featIdx];
                            }
                        }

                        ++shiftsThisIter;
                        ++p84TotalShifts;
                    }  // end per-spike loop
                }      // end per-cluster loop

                if (shiftsThisIter == 0) break;
            }          // end iter loop

            alignItersRun = p84ItersRun;
            finalAligned  = p84TotalShifts;

            Output("[Phase 2b m3] chunk %d patch84 PCA-centering (circular): "
                   "%d total shifts over %d iter(s); "
                   "m_cumShift NOT modified (mode 2)\n",
                   chunkIdx, p84TotalShifts, p84ItersRun);
        }
    } else
    {
        // Re-collect alive non-noise cluster ids from final labels
        std::vector<int> finalIds;
        finalIds.reserve(64);
        for (int v : labels) {
            if (v <= 0) continue;
            bool found = false;
            for (int s : finalIds) if (s == v) { found = true; break; }
            if (!found) finalIds.push_back(v);
        }

        // Scratch buffers — sized for the largest cluster's batch call
        std::vector<int16_t> tmpl(static_cast<size_t>(waveSamples), 0);
        std::vector<int16_t> xcWfm;
        std::vector<int>     xcShifts;
        std::vector<float>   xcScores;
        const float minScore = ResidualPCAMinScore;
        const int   maxShift = m_timeShiftMaxAbs;

        // ── Re-extract / re-featurize setup ──────────────────────────────
        char filPathP71[STRLEN + 16] = {0};
        pickInputPath(filPathP71, sizeof(filPathP71), FileBase, "fil", ElecNo);
        FILE* filFpP71 = fopen(filPathP71, "rb");

        const float   sessionSamplesF   = timeRawMax - timeRawMin;
        const int64_t sessionSamplesP71 = static_cast<int64_t>(
            std::max(0.0f, sessionSamplesF));
        const int     timeDimIdxP71     = nDims - 1;

        const bool hasBasis = m_timeShiftBasis.valid() &&
                              !m_timeShiftBasis.meanShifted.empty();
        const int  candCanonical = hasBasis ? m_timeShiftBasis.N : 0;
        const int  basisNComp    = hasBasis ? m_timeShiftBasis.nComp : 0;
        const int  basisData2use = hasBasis ? m_timeShiftBasis.data2use : 0;
        const int  basisRecShift = hasBasis ? m_timeShiftBasis.recShift : 0;
        const int  basisNChan    = hasBasis ? m_timeShiftBasis.nChan : nChan;
        const bool basisCentered = hasBasis ? m_timeShiftBasis.isCentered : false;
        const bool isStderiv     = hasBasis ? m_timeShiftBasis.isStderiv : false;
        const int  nPCAFeatsP71  = hasBasis ? basisNChan * basisNComp : 0;

        // Pre-load int64 rawTs for this chunk's pts from .res.  Indexed
        // by local i_local.  Avoids the ±~7 sample float-precision error
        // (see paragraph (c) above).
        std::vector<int64_t> rawTsByLocal(static_cast<size_t>(nPts), 0);
        {
            char resPathP71[STRLEN + 16] = {0};
            snprintf(resPathP71, sizeof(resPathP71),
                     "%s.res.%d", FileBase, ElecNo);
            FILE* resFpP71 = fopen(resPathP71, "rb");
            if (resFpP71) {
                for (int i = 0; i < nPts; ++i) {
                    const int gp = pts[static_cast<size_t>(i)];
                    if (gp < 0) continue;
                    if (fseeko(resFpP71,
                               static_cast<off_t>(gp)
                                 * static_cast<off_t>(sizeof(int64_t)),
                               SEEK_SET) != 0) continue;
                    int64_t ts = 0;
                    if (fread(&ts, sizeof(int64_t), 1, resFpP71) == 1
                            && ts > 0) {
                        rawTsByLocal[static_cast<size_t>(i)] = ts;
                    }
                }
                fclose(resFpP71);
            }
        }

        // Iteration bound: collapses to 1 if we can't refresh waveBuf
        // (no .fil or no PCA basis), where iterating is pointless.
        const int alignMaxIter = (filFpP71 && hasBasis)
            ? std::clamp(ResidualPCAIter, 2, 10)
            : 1;

        // Scratch row buffers reused across iterations
        std::vector<int16_t> filRowP71(static_cast<size_t>(NbTotalChannels));
        std::vector<int16_t> waveScratchP71(static_cast<size_t>(waveSamples));

        // patch72 — verify that .fil content matches .spk before any
        // alignment work.
        //
        // Ports the verifyRawSource check from Klusters' patch69
        // (klustersdoc.cpp:4046-4188) into KKExp.  Without it, a stale
        // .fil (e.g. recompiled with different filter settings, or
        // overwritten by a later pipeline stage) would silently corrupt
        // .spk on every shifted spike during the patch71 on-the-spot
        // re-extract and again during TimeShiftFinalize's
        // RefeaturizeFromShifts re-projection.
        //
        // Algorithm: pick the first chunk-spike whose extraction window
        // fits inside the .fil, read its waveform at the spike's
        // ORIGINAL timestamp (rawTsByLocal, no m_cumShift applied yet),
        // apply the stderiv transform if .spk is in stderiv format,
        // compare to that spike's existing waveBuf slot (loaded from
        // .spk at function entry).  RMS difference exceeding 50% of the
        // .spk content's RMS indicates the two raw sources do not
        // match — abort this chunk's alignment to prevent further
        // corruption.
        //
        // Skipped when .fil is unavailable or no spike's window fits
        // inside the recording (extremely unusual; would only happen
        // if every chunk-spike sits at a file boundary).
        bool rawSourceOk = true;
        if (filFpP71) {
            bool verifyDone = false;
            for (int iV = 0; iV < nPts && !verifyDone; ++iV) {
                const int64_t tsV = rawTsByLocal[static_cast<size_t>(iV)];
                if (tsV <= 0) continue;
                const int64_t startV = tsV - PeakSampleIndex;
                if (startV < 0 ||
                    startV + nSamplesPerSpike > sessionSamplesP71 + 1) continue;
                if (fseeko(filFpP71,
                           startV * static_cast<off_t>(NbTotalChannels)
                                  * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) continue;

                bool readVOk = true;
                for (int s = 0; s < nSamplesPerSpike && readVOk; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels),
                              filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        readVOk = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!readVOk) continue;

                if (isStderiv)
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);

                // Compare to waveBuf[iV * waveSamples ..] which holds the
                // .spk contents in the same sample-major layout.
                const int16_t* refSpk = waveBuf.data()
                    + static_cast<ptrdiff_t>(iV)
                    * static_cast<ptrdiff_t>(waveSamples);
                double sumDiff2 = 0.0, sumRef2 = 0.0;
                for (int e = 0; e < waveSamples; ++e) {
                    const double dv =
                        static_cast<double>(waveScratchP71[
                            static_cast<size_t>(e)])
                      - static_cast<double>(refSpk[static_cast<size_t>(e)]);
                    sumDiff2 += dv * dv;
                    sumRef2  += static_cast<double>(refSpk[
                        static_cast<size_t>(e)])
                              * static_cast<double>(refSpk[
                        static_cast<size_t>(e)]);
                }
                const double rmsRatio = (sumRef2 > 0.0)
                    ? std::sqrt(sumDiff2 / sumRef2)
                    : 0.0;

                if (rmsRatio > 0.5) {
                    Output("[Phase 2b m3] chunk %d patch72: RAW SOURCE "
                           "MISMATCH — .fil content does not match .spk\n"
                           "  verify spike local idx %d (gp=%d, ts=%lld)\n"
                           "  RMS difference: %.1f%% of .spk content "
                           "(threshold 50%%)\n"
                           "  Likely cause: .spk was extracted from a "
                           "different raw source than the .fil currently "
                           "on disk (e.g. .fil overwritten by a later "
                           "pipeline stage, or filter settings changed).\n"
                           "  Aborting alignment for this chunk to "
                           "prevent further .spk corruption.  Re-extract "
                           "waveforms with process_extractspikes before "
                           "re-running.\n",
                           chunkIdx, iV,
                           pts[static_cast<size_t>(iV)],
                           static_cast<long long>(tsV),
                           rmsRatio * 100.0);
                    rawSourceOk = false;
                } else {
                    Output("[Phase 2b m3] chunk %d patch72: raw source "
                           "verified (verify spike %d, RMS diff %.1f%%)\n",
                           chunkIdx, iV, rmsRatio * 100.0);
                }
                verifyDone = true;
            }
            if (!verifyDone) {
                Output("[Phase 2b m3] chunk %d patch72: WARNING — could "
                       "not verify raw source (no spike's window fit "
                       "within .fil); proceeding without verification\n",
                       chunkIdx);
            }
        }

        // Skip the iteration loop entirely if raw-source verification
        // tripped.  m_cumShift values from prior phases stay untouched;
        // TimeShiftFinalize will still process them, but at least we
        // don't compound the damage by shifting from a wrong source.
        if (rawSourceOk) {

        // patch73 — refresh waveBuf for spikes with non-zero m_cumShift.
        //
        // At function entry, waveBuf was loaded via TimeShiftReadSpikeWave
        // which reads .spk verbatim (no m_cumShift applied — see the
        // comment at line 9118 about parallel-safety).  If Phase 1
        // (cross-chunk alignment) or Phase 2.5 already populated
        // m_cumShift with non-zero values, those shifts are NOT reflected
        // in waveBuf at entry.  Iter 0's cluster mean is then built from
        // un-shifted .spk content, producing a slightly-off template,
        // and the iter-0 xcorr lag is computed against a not-quite-right
        // mean.
        //
        // Spikes whose iter-0 lag is zero (already match the off-mean)
        // never trigger the on-the-spot .fil re-extract in the iter
        // loop, so their waveBuf stays in the .spk state and the
        // prior-phase shifts remain "invisible" through all iters of
        // this function.  Final disk output is still correct (finalize
        // re-extracts everyone), but per-iter mean quality suffers and
        // alignment converges to a slightly-suboptimal m_cumShift.
        //
        // Fix: at function entry into the alignment block, for every
        // chunk-spike with prior m_cumShift != 0, re-extract its
        // waveform from .fil at (rawTs + prevCum - PeakSampleIndex) and
        // overwrite waveBuf[iLocal] so iter 0's mean is built from
        // already-shifted content.  Sample-major layout matches the
        // .spk load.  Spikes that fail the bounds or read check stay
        // in their .spk state — same fallback as the iter-loop body.
        //
        // Cost: one .fil read per pre-shifted spike at function entry,
        // independent of the iter loop.  For sessions where most chunks
        // have m_cumShift == 0 (the common case before Phase 1 starts
        // operating), this is a no-op.
        if (filFpP71 && hasBasis) {
            int prerefreshed = 0;
            for (int i = 0; i < nPts; ++i) {
                const int gp = pts[static_cast<size_t>(i)];
                if (gp < 0 || gp >= static_cast<int>(m_cumShift.size()))
                    continue;
                const int prevCum = m_cumShift[static_cast<size_t>(gp)];
                if (prevCum == 0) continue;
                const int64_t tsP = rawTsByLocal[static_cast<size_t>(i)];
                if (tsP <= 0) continue;
                const int64_t offP = tsP + prevCum - PeakSampleIndex;
                if (offP < 0 ||
                    offP + nSamplesPerSpike > sessionSamplesP71 + 1)
                    continue;
                if (fseeko(filFpP71,
                           offP * static_cast<off_t>(NbTotalChannels)
                                * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) continue;
                bool okR = true;
                for (int s = 0; s < nSamplesPerSpike && okR; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels),
                              filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        okR = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!okR) continue;
                if (isStderiv)
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);
                int16_t* dstBuf = waveBuf.data()
                    + static_cast<ptrdiff_t>(i)
                    * static_cast<ptrdiff_t>(waveSamples);
                std::copy(waveScratchP71.begin(),
                          waveScratchP71.end(), dstBuf);
                ++prerefreshed;
            }
            if (prerefreshed > 0) {
                Output("[Phase 2b m3] chunk %d patch73: pre-refreshed "
                       "waveBuf for %d spikes with non-zero prior "
                       "m_cumShift before iter loop\n",
                       chunkIdx, prerefreshed);
            }
        }

        for (int iterAlign = 0; iterAlign < alignMaxIter; ++iterAlign) {
            ++alignItersRun;
            int shiftsThisIter = 0;

        for (int cid : finalIds) {
            members.clear();
            for (int i = 0; i < nPts; i++)
                if (labels[i] == cid) members.push_back(i);
            const int Nc = static_cast<int>(members.size());
            if (Nc < 5) continue;

            // Build channel-major template = int16 mean.  Use int64
            // accumulator to avoid overflow on large clusters.
            std::vector<int64_t> accCh(static_cast<size_t>(waveSamples), 0);
            for (int m : members) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(m) * waveSamples;
                for (int s = 0; s < nSamplesPerSpike; s++)
                    for (int ch = 0; ch < nChan; ch++)
                        accCh[static_cast<size_t>(ch * nSamplesPerSpike + s)]
                            += w[s * nChan + ch];
            }
            for (int e = 0; e < waveSamples; e++)
                tmpl[static_cast<size_t>(e)] = static_cast<int16_t>(
                    accCh[static_cast<size_t>(e)] / Nc);

            // patch64 — pre-align the template to PeakSampleIndex (so
            // xcorr pulls every spike toward the window centre, and any
            // two clusters that would merge converge at the same point).
            if (PeakSampleIndex >= 0 && PeakSampleIndex < nSamplesPerSpike) {
                int    meanPeakSamp = PeakSampleIndex;
                double bestAmp      = -1.0;
                for (int s = 0; s < nSamplesPerSpike; ++s) {
                    double amp = 0.0;
                    for (int ch = 0; ch < nChan; ++ch)
                        amp += std::abs(static_cast<double>(
                            accCh[static_cast<size_t>(ch * nSamplesPerSpike + s)]));
                    if (amp > bestAmp) { bestAmp = amp; meanPeakSamp = s; }
                }
                const int tmplShift = PeakSampleIndex - meanPeakSamp;
                if (tmplShift != 0) {
                    std::vector<int16_t> shifted(static_cast<size_t>(waveSamples), 0);
                    for (int ch = 0; ch < nChan; ++ch) {
                        for (int s = 0; s < nSamplesPerSpike; ++s) {
                            const int src = s - tmplShift;
                            if (src < 0 || src >= nSamplesPerSpike) continue;
                            shifted[static_cast<size_t>(ch * nSamplesPerSpike + s)] =
                                tmpl[static_cast<size_t>(ch * nSamplesPerSpike + src)];
                        }
                    }
                    tmpl = std::move(shifted);
                }
            }

            // Repack cluster's waveforms into channel-major batch
            xcWfm.assign(static_cast<size_t>(Nc) * waveSamples, 0);
            for (int sp = 0; sp < Nc; sp++) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(members[sp]) * waveSamples;
                int16_t* dst = xcWfm.data() +
                               static_cast<size_t>(sp) * waveSamples;
                for (int s = 0; s < nSamplesPerSpike; s++)
                    for (int ch = 0; ch < nChan; ch++)
                        dst[ch * nSamplesPerSpike + s] = w[s * nChan + ch];
            }

            xcShifts.assign(static_cast<size_t>(Nc), 0);
            xcScores.assign(static_cast<size_t>(Nc), 0.0f);
            const int rc = XcorrDispatch::compute(
                xcWfm.data(), tmpl.data(),
                Nc, nChan, nSamplesPerSpike,
                maxShift, minScore,
                xcShifts.data(), xcScores.data());
            (void)rc;

            // Apply each non-zero lag on the spot.
            for (int sp = 0; sp < Nc; sp++) {
                const int lag = xcShifts[static_cast<size_t>(sp)];
                if (lag == 0) {
                    if (xcScores[static_cast<size_t>(sp)] < minScore)
                        ++finalLowScore;
                    continue;
                }
                const int iLocal = members[static_cast<size_t>(sp)];
                const int gp     = pts[static_cast<size_t>(iLocal)];
                if (gp < 0 || gp >= static_cast<int>(m_cumShift.size()))
                    continue;
                const int prevCum = m_cumShift[static_cast<size_t>(gp)];
                const int newCum  = prevCum + lag;

                // Fast path when re-extract is unavailable: just bump
                // m_cumShift and let TimeShiftFinalize handle the disk
                // catch-up.  No cap-reject regardless.
                if (!filFpP71 || !hasBasis) {
                    m_cumShift[static_cast<size_t>(gp)] = newCum;
                    ++finalAligned;
                    ++totalRealigned;
                    ++shiftsThisIter;
                    continue;
                }

                // Use precise int64 rawTs from .res (no float recovery).
                const int64_t rawTsInt = rawTsByLocal[static_cast<size_t>(iLocal)];
                if (rawTsInt <= 0) {
                    // No valid .res entry — skip rather than fall back
                    // to lossy float recovery.
                    ++finalOutOfBounds;
                    continue;
                }
                const int64_t off = rawTsInt + newCum - PeakSampleIndex;
                if (off < 0 ||
                    off + nSamplesPerSpike > sessionSamplesP71 + 1) {
                    // Window off the recording — skip without committing.
                    ++finalOutOfBounds;
                    continue;
                }

                // Tentative commit; reverted on .fil read failure.
                m_cumShift[static_cast<size_t>(gp)] = newCum;

                // Read sample-major waveform from .fil.
                bool readOk = true;
                if (fseeko(filFpP71,
                           off * static_cast<off_t>(NbTotalChannels)
                               * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) {
                    readOk = false;
                }
                for (int s = 0; s < nSamplesPerSpike && readOk; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels), filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        readOk = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!readOk) {
                    m_cumShift[static_cast<size_t>(gp)] = prevCum;
                    ++finalOutOfBounds;
                    continue;
                }

                if (isStderiv) {
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);
                }

                // Overwrite waveBuf so next iter's xcorr sees the
                // re-extracted waveform (sample-major, matching the
                // original TimeShiftReadSpikeWave load).
                int16_t* dstBuf = waveBuf.data() +
                                  static_cast<ptrdiff_t>(iLocal)
                                * static_cast<ptrdiff_t>(waveSamples);
                std::copy(waveScratchP71.begin(),
                          waveScratchP71.end(), dstBuf);

                // Re-project through the canonical (cand=N) PCA basis
                // and update Data[gp] features in place.  Eigenvector
                // indexing matches RefeaturizeFromShifts:
                //     ev[k * data2use + s]   (col-major)
                {
                    float* dataRow = Data.m_Data + gp * nDims;
                    const auto& meanCanon = m_timeShiftBasis.meanShifted[
                        static_cast<size_t>(candCanonical)];
                    const auto& evecCanon = m_timeShiftBasis.eigvecShifted[
                        static_cast<size_t>(candCanonical)];
                    for (int ch = 0; ch < basisNChan; ++ch) {
                        const auto& mu = meanCanon[static_cast<size_t>(ch)];
                        const auto& ev = evecCanon[static_cast<size_t>(ch)];
                        for (int k = 0; k < basisNComp; ++k) {
                            double val = 0.0;
                            for (int s = 0; s < basisData2use; ++s) {
                                const int sIdx = basisRecShift + s;
                                double xraw = static_cast<double>(
                                    waveScratchP71[static_cast<size_t>(
                                        sIdx * nChan + ch)]);
                                if (basisCentered)
                                    xraw -= mu[static_cast<size_t>(s)];
                                val += ev[static_cast<size_t>(
                                    k * basisData2use + s)] * xraw;
                            }
                            const int featIdx = ch * basisNComp + k;
                            if (featIdx < nPCAFeatsP71 &&
                                featIdx < static_cast<int>(dimMin_.size())) {
                                dataRow[featIdx] =
                                    (static_cast<float>(val) - dimMin_[featIdx])
                                    * dimRange_[featIdx];
                            }
                        }
                    }
                    if (sessionSamplesF > 0.0f) {
                        dataRow[timeDimIdxP71] =
                            (static_cast<float>(rawTsInt + newCum) - timeRawMin)
                            / sessionSamplesF;
                    }
                }

                ++finalAligned;
                ++totalRealigned;
                ++shiftsThisIter;
            }
        }   // end cluster loop

            // Converged: nobody moved this iter (every remaining shift
            // is below minScore or out-of-bounds).  Stop early.
            if (shiftsThisIter == 0) break;
        }   // end iter loop

        // patch83 — PCA-centering refine pass (opt-in via AlignPcaCenter)
        //
        // After the xcorr alignment loop converges, optionally run a
        // second pass that refines each spike's position to MINIMIZE
        // its distance to the cluster's mean PCA position:
        //
        //     dist²(s) = sum_f (pca_f(spike at shift s) − μ_pca[f])²
        //
        // where μ_pca[f] is the mean of feature f across the cluster's
        // current members and pca_f comes from projecting the freshly
        // re-extracted waveform through the canonical .pca basis with
        // the same normalisation Data[] uses (dimMin_/dimRange_).
        //
        // This is the KKExp analogue of klusters' patch82 PCA-energy
        // refine pass, but with the energy criterion (||pca||²) swapped
        // for the centering criterion (||pca − μ_pca_cluster||²).  The
        // centering version directly tightens cluster compactness in
        // PCA space — exactly the geometric property that downstream
        // EM / merge decisions key off.  Energy is useful when there
        // is no cluster mean to compare against (e.g. the klusters
        // single-cluster refine path); here we have full cluster
        // membership from the converged iter loop, so centering is
        // the natural choice.
        //
        // Cost: per qualifying spike, (2·m_timeShiftMaxAbs + 1) .fil
        // reads + PCA projections.  For typical maxShift=3, that's
        // 7 reads/projections per spike — well under a second per
        // chunk on a warm cache.
        //
        // Falls back silently (logs an info line, runs no refine) when:
        //   - filFpP71 is null (no .fil available)
        //   - hasBasis is false (no canonical PCA basis loaded)
        //   - AlignPcaCenter is 0 (the default)
        if (AlignPcaCenter && filFpP71 && hasBasis) {
            int p83Refined  = 0;
            int p83Skipped  = 0;
            int p83Examined = 0;
            for (int cid : finalIds) {
                members.clear();
                for (int i = 0; i < nPts; i++)
                    if (labels[i] == cid) members.push_back(i);
                const int Nc = static_cast<int>(members.size());
                if (Nc < 3) continue;

                // Compute cluster mean PCA position μ_pca over the kept
                // PCA feature columns.  Read straight from Data[] — it
                // was kept in sync by the iter loop's on-the-spot
                // re-featurize step.
                std::vector<double> muPca(
                    static_cast<size_t>(nPCAFeatsP71), 0.0);
                int clusterMembersWithGp = 0;
                for (int m : members) {
                    const int gp = pts[static_cast<size_t>(m)];
                    if (gp < 0 || gp >=
                        static_cast<int>(m_cumShift.size())) continue;
                    const float* row = Data.m_Data + gp * nDims;
                    for (int f = 0; f < nPCAFeatsP71; ++f)
                        muPca[static_cast<size_t>(f)] +=
                            static_cast<double>(row[f]);
                    ++clusterMembersWithGp;
                }
                if (clusterMembersWithGp < 3) continue;
                const double invN = 1.0 / clusterMembersWithGp;
                for (int f = 0; f < nPCAFeatsP71; ++f)
                    muPca[static_cast<size_t>(f)] *= invN;

                // Per-spike: sweep candidate shifts, pick the one that
                // minimizes distance² to μ_pca.
                for (int sp = 0; sp < Nc; ++sp) {
                    const int iLocal = members[static_cast<size_t>(sp)];
                    const int gp     = pts[static_cast<size_t>(iLocal)];
                    if (gp < 0 || gp >=
                        static_cast<int>(m_cumShift.size())) continue;

                    const int     prevCum   = m_cumShift[static_cast<size_t>(gp)];
                    const int64_t rawTsInt  = rawTsByLocal[static_cast<size_t>(iLocal)];
                    if (rawTsInt <= 0) continue;
                    ++p83Examined;

                    int    bestS    = 0;
                    double bestDist = std::numeric_limits<double>::infinity();
                    int    nValid   = 0;

                    for (int s = -m_timeShiftMaxAbs;
                             s <=  m_timeShiftMaxAbs; ++s) {
                        const int     candCum = prevCum + s;
                        const int64_t off     = rawTsInt + candCum - PeakSampleIndex;
                        if (off < 0 ||
                            off + nSamplesPerSpike > sessionSamplesP71 + 1)
                            continue;

                        if (fseeko(filFpP71,
                                   off * static_cast<off_t>(NbTotalChannels)
                                       * static_cast<off_t>(sizeof(int16_t)),
                                   SEEK_SET) != 0) continue;

                        bool readOk = true;
                        for (int t = 0; t < nSamplesPerSpike && readOk; ++t) {
                            if (fread(filRowP71.data(), sizeof(int16_t),
                                      static_cast<size_t>(NbTotalChannels),
                                      filFpP71)
                                != static_cast<size_t>(NbTotalChannels)) {
                                readOk = false; break;
                            }
                            for (int c = 0; c < nChan; ++c)
                                waveScratchP71[static_cast<size_t>(t * nChan + c)] =
                                    filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                        }
                        if (!readOk) continue;

                        if (isStderiv)
                            ApplySdiffAllpairsTemporalDiff(
                                waveScratchP71.data(), nChan, nSamplesPerSpike);

                        // PCA projection.  Mirrors the per-spike code in
                        // the iter loop body — same basis access, same
                        // dimMin_/dimRange_ normalisation.  Distance² is
                        // accumulated against muPca[featIdx] in the same
                        // normalised feature space.
                        double dist2 = 0.0;
                        const auto& meanCanon = m_timeShiftBasis.meanShifted[
                            static_cast<size_t>(candCanonical)];
                        const auto& evecCanon = m_timeShiftBasis.eigvecShifted[
                            static_cast<size_t>(candCanonical)];
                        for (int ch = 0; ch < basisNChan; ++ch) {
                            const auto& mu = meanCanon[static_cast<size_t>(ch)];
                            const auto& ev = evecCanon[static_cast<size_t>(ch)];
                            for (int k = 0; k < basisNComp; ++k) {
                                double val = 0.0;
                                for (int u = 0; u < basisData2use; ++u) {
                                    const int sIdx = basisRecShift + u;
                                    double xraw = static_cast<double>(
                                        waveScratchP71[static_cast<size_t>(
                                            sIdx * nChan + ch)]);
                                    if (basisCentered)
                                        xraw -= mu[static_cast<size_t>(u)];
                                    val += ev[static_cast<size_t>(
                                        k * basisData2use + u)] * xraw;
                                }
                                const int featIdx = ch * basisNComp + k;
                                if (featIdx >= nPCAFeatsP71 ||
                                    featIdx >= static_cast<int>(dimMin_.size()))
                                    continue;
                                const double normVal =
                                    (static_cast<float>(val) - dimMin_[featIdx])
                                  * dimRange_[featIdx];
                                const double diff = normVal
                                  - muPca[static_cast<size_t>(featIdx)];
                                dist2 += diff * diff;
                            }
                        }

                        ++nValid;
                        if (dist2 < bestDist) {
                            bestDist = dist2;
                            bestS    = s;
                        }
                    }   // end candidate-shift loop

                    if (nValid == 0) { ++p83Skipped; continue; }
                    if (bestS == 0) continue;   // already at the centre

                    m_cumShift[static_cast<size_t>(gp)] = prevCum + bestS;
                    ++p83Refined;
                }
            }   // end cluster loop

            Output("[Phase 2b m3] chunk %d patch83 PCA-centering: "
                   "examined %d, refined %d, skipped %d (off-edge)\n",
                   chunkIdx, p83Examined, p83Refined, p83Skipped);
        } else if (AlignPcaCenter) {
            Output("[Phase 2b m3] chunk %d patch83: AlignPcaCenter "
                   "requested but %s%s%s — skipping.\n",
                   chunkIdx,
                   !filFpP71 ? ".fil unavailable" : "",
                   (!filFpP71 && !hasBasis) ? " and " : "",
                   !hasBasis ? "PCA basis not loaded" : "");
        }

        }   // end if (rawSourceOk) — patch72

        // patch74 — post-alignment .res monotonicity check.
        //
        // After cumulative shifts are applied, two adjacent spikes whose
        // original detection timestamps were close together may end up
        // with crossed (newTs1 > newTs2 where origTs1 < origTs2).
        // Klusters explicitly handles this case (klustersdoc.cpp lines
        // 3922-3938) by sorting spikes by their new timestamps before
        // writing .res, but KKExp leaves spikes in original detection
        // order — TimeShiftFinalize writes them at their original .res
        // slots with only the shifted-timestamp value updated.
        //
        // Non-monotonic .res is technically valid (most consumers index
        // by spike number, not by sample-time), but several tools assume
        // .res is sorted (process_smrconvert, some plotting helpers,
        // grep-by-time analyses).  And a non-monotonic .res signals
        // something has likely gone wrong with the alignment — large
        // cross-detection shifts that the alignment shouldn't be
        // producing under normal operation.
        //
        // This check scans this chunk's pts in their original gp order,
        // computes the effective post-shift timestamp for each
        // (rawTs + m_cumShift), and warns when adjacent pairs cross.
        // It does NOT modify .res or m_cumShift — the audit purpose is
        // visibility, not correction.  If crossings are detected and
        // matter, the user can decide whether to: (a) tighten
        // m_timeShiftMaxAbs to prevent them, (b) port Klusters' sort
        // step into TimeShiftFinalize, or (c) accept them.
        {
            int crossings = 0;
            int64_t maxCross = 0;
            int firstCrossGp1 = -1, firstCrossGp2 = -1;
            for (int i = 1; i < nPts; ++i) {
                const int gpPrev = pts[static_cast<size_t>(i - 1)];
                const int gpCur  = pts[static_cast<size_t>(i)];
                if (gpPrev < 0 || gpCur < 0) continue;
                if (gpPrev >= static_cast<int>(m_cumShift.size()) ||
                    gpCur  >= static_cast<int>(m_cumShift.size())) continue;
                const int64_t tsPrev = rawTsByLocal[
                    static_cast<size_t>(i - 1)];
                const int64_t tsCur  = rawTsByLocal[
                    static_cast<size_t>(i)];
                if (tsPrev <= 0 || tsCur <= 0) continue;
                const int64_t effPrev = tsPrev
                    + m_cumShift[static_cast<size_t>(gpPrev)];
                const int64_t effCur  = tsCur
                    + m_cumShift[static_cast<size_t>(gpCur)];
                // Original order: tsPrev <= tsCur (assuming .res was
                // monotonic to start with — true for typical sessions).
                // Crossing: post-shift, effPrev > effCur.
                if (effPrev > effCur) {
                    if (crossings == 0) {
                        firstCrossGp1 = gpPrev;
                        firstCrossGp2 = gpCur;
                    }
                    const int64_t gap = effPrev - effCur;
                    if (gap > maxCross) maxCross = gap;
                    ++crossings;
                }
            }
            if (crossings > 0) {
                Output("[Phase 2b m3] chunk %d patch74: WARNING — "
                       "%d adjacent-spike timestamp crossings detected "
                       "after alignment (max gap %lld samples, "
                       "first at gp %d → gp %d).  .res will be written "
                       "non-monotonic by TimeShiftFinalize.  Tools that "
                       "assume .res is sorted may misbehave; consider "
                       "lowering m_timeShiftMaxAbs or porting Klusters' "
                       "sortedOrder+targetPos permutation (klustersdoc."
                       "cpp:3922-3938) into TimeShiftFinalize.\n",
                       chunkIdx, crossings,
                       static_cast<long long>(maxCross),
                       firstCrossGp1, firstCrossGp2);
            }
        }

        if (filFpP71) fclose(filFpP71);
    }

    // Write final labels back into the sub-KK and refresh its bookkeeping.
    for (int i = 0; i < nPts; i++) {
        const int v = labels[i];
        Ks.Class[i] = (v >= 0 && v < MaxPossibleClusters) ? v : 0;
    }
    for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
    for (int i = 0; i < nPts; i++) {
        const int c = Ks.Class[i];
        if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
    }
    Ks.Reindex();
    Ks.MStep();

    // The outer Phase 9 (TimeShiftFinalize) refeaturizes globally; we
    // don't call RefeaturizeFromShifts here because the parallel chunk
    // loop would race on Data[] writes if multiple chunks tried.
    //
    // Per-chunk final-alignment summary — bookend to the chunk-START
    // banner so each chunk has a paired START/ALIGN/END trio in the log.
    Output("[Phase 2b m3] chunk %d ALIGN: %d spike realigns (xcorr >= %.2f); "
           "skipped: %d low-score, %d out-of-bounds; ran %d align iters\n",
           chunkIdx, finalAligned,
           static_cast<double>(ResidualPCAMinScore),
           finalLowScore, finalOutOfBounds, alignItersRun);
    Output("[Phase 2b m3] chunk %d END: %d total splits, %d total realigns, "
           "ran %d/%d iters\n",
           chunkIdx, totalSplitsApplied, totalRealigned,
           actualItersRun, maxIter);
}

void KK::ChunkReCEMPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
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
    LockedStderr( "[Phase 2b] Chunk re-CEM (%s)\n", p2bDesc);

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
            LockedStderr(
                    "[Phase 2b] LPT order: nChunks=%d, K range %d-%d, "
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

    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalChunksProcessed,totalDeltaClusters)
    for (int oi = 0; oi < nCh; oi++) {
        const int ck = chunkOrder[static_cast<size_t>(oi)];
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
            "[Phase 2b] Chunk re-CEM: %d chunks processed, net cluster delta = %+d\n",
            totalChunksProcessed, totalDeltaClusters);
}


// ---------------------------------------------------------------------------
// KK::SubspaceReclusterPerChunk
//
// Per-chunk subspace reclustering — Phase 2 of the runtime pipeline.
// Runs after Phase 1 (per-chunk CEM) on the per-chunk model lists,
// before Phase 4 (mean-waveform harvest) and Phase 6 (cross-chunk
// model matching).
//
// For each local cluster in each chunk, projects its spikes (by global index)
// into the top-subspaceDims eigenvector subspace of that cluster's covariance,
// then runs CEMTwoPhase in that reduced space.  Splits are accepted if the
// multi-cluster BIC score beats the single-cluster null.  perChunkClass[] and
// perChunkModels[] are updated in-place so downstream Phase 5 cross-chunk
// matching sees purer, better-separated cluster models.
//
// New local cluster IDs are assigned starting from maxLocalId+1 within each
// chunk so they remain globally unique across chunks.
// ---------------------------------------------------------------------------
// KK::WithinChunkTemplateMatch
//
// Within-chunk xcorr template matching — Phase 5 of the runtime pipeline.
// Runs after Phase 4 (mean-waveform harvest), before Phase 6 (cross-chunk
// matching).
//
// For each chunk, computes the normalised circular xcorr between every pair of
// cluster mean waveforms (already in ChunkModel::meanWav, channel-major int16).
// A mutual-best pair whose xcorr score >= minScore is merged:
//   - perChunkClass[k] is remapped so both clusters share the lower local ID.
//   - The surviving ChunkModel accumulates both clusters' members.
//
// This is a waveform-level analogue of the Mahalanobis MNN merge — it catches
// clusters that are separated in feature space but represent the same unit
// viewed at slightly different times within the chunk.  It also pre-consolidates
// the model list before Phase 6 so cross-chunk matching has fewer, purer models.
// ---------------------------------------------------------------------------

namespace {

// ----- Top eigenvalue of a symmetric D × D matrix via power iteration ------
// Returns lambda_max; converges in O(D² · iters), no eigvecs stored.  Used by
// the Phase 5 union-elongation gate: top eigenvalue of pooled+between
// covariance vs top eigenvalues of the individual cluster covariances.
//
// Reference matrix M must be symmetric; only the values M[i*D + j] (full
// dense, with M[i,j] = M[j,i]) are read.  Stable on near-degenerate
// spectra to within tol; if the matrix is identically zero, returns 0.
static double tmpl_top_eigenvalue(const double* M, int D,
                                  int max_iter = 60, double tol = 1e-7)
{
    if (D < 1) return 0.0;
    if (D == 1) return M[0];
    std::vector<double> u(static_cast<size_t>(D), 1.0 / std::sqrt(static_cast<double>(D)));
    std::vector<double> v(static_cast<size_t>(D));
    double lambda = 0.0;
    for (int it = 0; it < max_iter; it++) {
        // v = M u
        for (int i = 0; i < D; i++) {
            double s = 0.0;
            for (int j = 0; j < D; j++) s += M[i * D + j] * u[static_cast<size_t>(j)];
            v[static_cast<size_t>(i)] = s;
        }
        // ||v||
        double n2 = 0.0;
        for (int i = 0; i < D; i++) n2 += v[static_cast<size_t>(i)] * v[static_cast<size_t>(i)];
        const double nrm = std::sqrt(n2);
        if (nrm < 1e-30) return 0.0;
        for (int i = 0; i < D; i++) v[static_cast<size_t>(i)] /= nrm;
        // Rayleigh quotient: λ = vᵀ M v
        double rq = 0.0;
        for (int i = 0; i < D; i++) {
            double s = 0.0;
            for (int j = 0; j < D; j++) s += M[i * D + j] * v[static_cast<size_t>(j)];
            rq += v[static_cast<size_t>(i)] * s;
        }
        const bool converged = (std::abs(rq - lambda) <= tol * std::abs(rq) + 1e-12);
        lambda = rq;
        std::swap(u, v);
        if (converged) break;
    }
    return lambda;
}

// ----- Union-elongation ratio test ----------------------------------------
//
// For a candidate merge pair (A, B), compute the union covariance under
// equal-weight pooled-within + Welch-style between term:
//
//   Σ_pooled = ((N_A-1)·Σ_A + (N_B-1)·Σ_B) / (N_A+N_B-2)
//   Σ_union  = Σ_pooled + (N_A·N_B / ((N_A+N_B)(N_A+N_B-1))) · (m_A-m_B)(m_A-m_B)ᵀ
//
// Then compare top eigenvalues:
//
//   ratio = λ_max(Σ_union) / max(λ_max(Σ_A), λ_max(Σ_B))
//
// Interpretation:
//   ratio ≈ 1   →  union's top eigenvalue is dominated by the more-elongated
//                  component's natural anisotropy — the centroid separation
//                  added no new structure → A and B are the same unit.
//   ratio >> 1  →  union has elongation that NEITHER component had on its
//                  own — driven by inter-centroid separation → A and B
//                  are distinct units, do not merge.
//
// Crucially: this test does NOT have the failure mode that killed patch21's
// per-cluster PCA preprocessing.  patch21 projected a single cluster's spikes
// onto that cluster's own top eigenvector — which by construction produced
// a 1-D distribution that CEM-with-K=2 would happily fit, giving a 67%
// false-positive split rate on elongated single clusters.  The union-ratio
// test SUBTRACTS the per-cluster top eigenvalue contribution, so an
// elongated single cluster has ratio ≈ 1 (its own elongation cancels
// against the pooled-within term) and does not trigger a false veto.
//
// Returns the ratio.  Returns 1.0 (no veto) on degenerate inputs.
static double tmpl_union_eig_ratio(const KK::ChunkModel& A,
                                   const KK::ChunkModel& B)
{
    const int D = static_cast<int>(A.mean.size());
    if (D < 2 || static_cast<int>(B.mean.size()) != D) return 1.0;
    if (A.cov.size() != static_cast<size_t>(D) * D) return 1.0;
    if (B.cov.size() != static_cast<size_t>(D) * D) return 1.0;
    if (A.nMembers < 2 || B.nMembers < 2)            return 1.0;

    const double Na = A.nMembers, Nb = B.nMembers, Nu = Na + Nb;

    // Symmetrise both per-cluster covariances from their upper-triangle storage.
    std::vector<double> SA(static_cast<size_t>(D) * D);
    std::vector<double> SB(static_cast<size_t>(D) * D);
    for (int i = 0; i < D; i++) {
        for (int j = i; j < D; j++) {
            const double a = static_cast<double>(A.cov[static_cast<size_t>(i) * D + j]);
            const double b = static_cast<double>(B.cov[static_cast<size_t>(i) * D + j]);
            SA[i * D + j] = a; SA[j * D + i] = a;
            SB[i * D + j] = b; SB[j * D + i] = b;
        }
    }

    // Pooled-within covariance.
    std::vector<double> SU(static_cast<size_t>(D) * D);
    const double pooledDen = std::max(1.0, Nu - 2.0);
    const double wA = (Na - 1.0) / pooledDen;
    const double wB = (Nb - 1.0) / pooledDen;
    for (int i = 0; i < D * D; i++) SU[i] = wA * SA[i] + wB * SB[i];

    // Between-cluster outer product.
    const double between = (Na * Nb) / (Nu * std::max(1.0, Nu - 1.0));
    for (int i = 0; i < D; i++) {
        const double di = static_cast<double>(A.mean[i]) - static_cast<double>(B.mean[i]);
        for (int j = 0; j < D; j++) {
            const double dj = static_cast<double>(A.mean[j]) - static_cast<double>(B.mean[j]);
            SU[i * D + j] += between * di * dj;
        }
    }

    // Top eigenvalues of each.
    const double tA = tmpl_top_eigenvalue(SA.data(), D);
    const double tB = tmpl_top_eigenvalue(SB.data(), D);
    const double tU = tmpl_top_eigenvalue(SU.data(), D);
    const double tMax = std::max(tA, tB);
    if (tMax <= 1e-30) return 1.0;
    return tU / tMax;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
int  KK::WithinChunkTemplateMatch(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nChan, int nSamplesPerSpike, float minScore)
{
    const int wElems  = nChan * nSamplesPerSpike;
    const int maxShft = std::max(1, nSamplesPerSpike / 4);
    const int nCh     = static_cast<int>(chunkPoints.size());
    int totalMerged   = 0;

    for (int ck = 0; ck < nCh; ck++) {
        auto& cls  = perChunkClass[ck];
        auto& mdls = perChunkModels[ck];
        const int n = static_cast<int>(mdls.size());
        if (n < 2) continue;

        // ── Compute all pairwise xcorr scores ─────────────────────────────
        // score[a][b] = normalised xcorr of mdls[a].meanWav vs mdls[b].meanWav
        std::vector<float> scoreAB(static_cast<size_t>(n) * n, -1.0f);

        for (int a = 0; a < n; a++) {
            if (mdls[static_cast<size_t>(a)].localClusterId == 0) continue;
            if (static_cast<int>(mdls[static_cast<size_t>(a)].meanWav.size()) != wElems) continue;
            for (int b = a + 1; b < n; b++) {
                if (mdls[static_cast<size_t>(b)].localClusterId == 0) continue;
                if (static_cast<int>(mdls[static_cast<size_t>(b)].meanWav.size()) != wElems) continue;
                int sh = 0; float sc = 0.0f;
                XcorrDispatch::compute(
                    mdls[static_cast<size_t>(a)].meanWav.data(),
                    mdls[static_cast<size_t>(b)].meanWav.data(),
                    1, nChan, nSamplesPerSpike,
                    maxShft, 0.0f, &sh, &sc);
                scoreAB[static_cast<size_t>(a * n + b)] = sc;
                scoreAB[static_cast<size_t>(b * n + a)] = sc;  // xcorr is symmetric for single spike
            }
        }

        // ── Find mutual-best pairs above threshold ─────────────────────────
        // For each cluster find its highest-scoring partner
        std::vector<int>   bestB(static_cast<size_t>(n), -1);
        std::vector<float> bestS(static_cast<size_t>(n), -1.0f);
        for (int a = 0; a < n; a++) {
            for (int b = 0; b < n; b++) {
                if (a == b) continue;
                float s = scoreAB[static_cast<size_t>(a * n + b)];
                if (s >= minScore && s > bestS[static_cast<size_t>(a)]) {
                    bestS[static_cast<size_t>(a)] = s;
                    bestB[static_cast<size_t>(a)] = b;
                }
            }
        }

        // ── Union-Find for transitive merges ──────────────────────────────
        std::vector<int> parent(static_cast<size_t>(n));
        std::iota(parent.begin(), parent.end(), 0);
        auto Find = [&](int x) -> int {
            while (parent[static_cast<size_t>(x)] != x) {
                parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(
                    parent[static_cast<size_t>(x)])];
                x = parent[static_cast<size_t>(x)];
            }
            return x;
        };
        auto Union = [&](int a, int b) {
            a = Find(a); b = Find(b);
            if (a != b) parent[static_cast<size_t>(b)] = a;
        };

        int chunkMerged = 0;
        for (int a = 0; a < n; a++) {
            int b = bestB[static_cast<size_t>(a)];
            if (b < 0) continue;
            if (bestB[static_cast<size_t>(b)] != a) continue;  // not mutual best
            if (Find(a) == Find(b)) continue;

            // Optional eigenvalue-ratio veto.  When -TemplateMatchEigRatio > 0,
            // additionally require that the union covariance's top eigenvalue
            // is not dominated by inter-centroid separation.  See
            // tmpl_union_eig_ratio() docs for the rationale and why this
            // avoids the failure mode that killed patch21's per-cluster PCA.
            if (TemplateMatchEigRatio > 0.0f) {
                const double eigRatio = tmpl_union_eig_ratio(
                    mdls[static_cast<size_t>(a)],
                    mdls[static_cast<size_t>(b)]);
                if (eigRatio > static_cast<double>(TemplateMatchEigRatio)) {
                    Output("  tmpl-within: chunk%d c%d+c%d xcorr=%.3f "
                           "eig-ratio=%.2f > %.2f → VETO\n",
                           ck,
                           mdls[static_cast<size_t>(a)].localClusterId,
                           mdls[static_cast<size_t>(b)].localClusterId,
                           bestS[static_cast<size_t>(a)],
                           eigRatio,
                           static_cast<double>(TemplateMatchEigRatio));
                    continue;
                }
            }

            Union(a, b);
            Output("  tmpl-within: chunk%d c%d+c%d xcorr=%.3f\n",
                   ck,
                   mdls[static_cast<size_t>(a)].localClusterId,
                   mdls[static_cast<size_t>(b)].localClusterId,
                   bestS[static_cast<size_t>(a)]);
            chunkMerged++;
            totalMerged++;
        }

        if (chunkMerged == 0) continue;

        // ── Remap perChunkClass: each component → lowest localClusterId ────
        // Identify canonical (lowest) ID per component
        std::unordered_map<int,int> rootToCanonIdx;
        for (int a = 0; a < n; a++) {
            int root = Find(a);
            auto it = rootToCanonIdx.find(root);
            if (it == rootToCanonIdx.end() ||
                mdls[static_cast<size_t>(a)].localClusterId <
                mdls[static_cast<size_t>(it->second)].localClusterId)
                rootToCanonIdx[root] = a;
        }
        // Build localClusterId → canonical localClusterId remap
        std::unordered_map<int,int> lcRemap;
        for (int a = 0; a < n; a++) {
            int canon = mdls[static_cast<size_t>(rootToCanonIdx[Find(a)])].localClusterId;
            lcRemap[mdls[static_cast<size_t>(a)].localClusterId] = canon;
        }
        for (auto& lc : cls)
            if (lcRemap.count(lc)) lc = lcRemap[lc];

        // ── Aggregate waveforms from merged-away models into canonicals ───
        //
        // Before label remap, accumulate weighted means per canonical
        // component using each cluster's stored counts as weights.
        // Without this step, the surviving model's meanWav and
        // meanWavLeft/Right still reflect ONLY the canonical cluster's
        // pre-merge spikes — Phase 6 then runs cross-chunk xcorr on
        // stale templates that don't include the merged-in cluster's
        // contribution.  This was a real bug that biased Phase 6
        // decisions.
        //
        // Mathematics: arithmetic mean is associative under proper
        // weighting — mean(A∪B) = (|A|·mean(A) + |B|·mean(B)) / (|A|+|B|).
        // Per-edge means use the per-edge counts (nMembersLeft/Right)
        // so spikes that fell outside the edge windows don't pollute
        // the boundary template.
        //
        // dst.nMembers itself is recomputed below from the remapped
        // cls labels (authoritative); we only need a temporary running
        // count for weighting the meanWav merge.  nMembersLeft/Right
        // ARE updated here since they're not recomputed elsewhere.
        auto wmerge = [](std::vector<int16_t>& d, int& dN,
                         const std::vector<int16_t>& s, int sN) {
            if (s.empty() || sN <= 0) return;
            if (d.empty() || dN <= 0) { d = s; dN = sN; return; }
            const size_t L = d.size();
            for (size_t e = 0; e < L; e++) {
                const int64_t num = static_cast<int64_t>(d[e]) * dN
                                  + static_cast<int64_t>(s[e]) * sN;
                d[e] = static_cast<int16_t>(num / (dN + sN));
            }
            dN += sN;
        };

        for (const auto& [root, canonIdx] : rootToCanonIdx) {
            ChunkModel& dst = mdls[static_cast<size_t>(canonIdx)];
            int runningN     = dst.nMembers;
            int runningNLeft = dst.nMembersLeft;
            int runningNRight= dst.nMembersRight;
            for (int a = 0; a < n; a++) {
                if (a == canonIdx) continue;
                if (Find(a) != root) continue;
                const ChunkModel& src = mdls[static_cast<size_t>(a)];
                wmerge(dst.meanWav,      runningN,      src.meanWav,      src.nMembers);
                wmerge(dst.meanWavLeft,  runningNLeft,  src.meanWavLeft,  src.nMembersLeft);
                wmerge(dst.meanWavRight, runningNRight, src.meanWavRight, src.nMembersRight);
            }
            // Persist the updated edge counts; nMembers is recomputed below.
            dst.nMembersLeft  = runningNLeft;
            dst.nMembersRight = runningNRight;
        }

        // Remove merged-away ChunkModels; keep canonical ones
        std::unordered_set<int> keepLc;
        for (auto& [root, idx2] : rootToCanonIdx)
            keepLc.insert(mdls[static_cast<size_t>(idx2)].localClusterId);
        mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
            [&](const ChunkModel& cm){ return !keepLc.count(cm.localClusterId); }),
            mdls.end());

        // Update nMembers on surviving models
        for (auto& cm : mdls) {
            cm.nMembers = 0;
            for (const auto& lc : cls)
                if (lc == cm.localClusterId) cm.nMembers++;
        }
    }

    Output("WithinChunkTemplateMatch: %d cluster pair(s) merged across all chunks\n",
           totalMerged);
    return totalMerged;
}








// ---------------------------------------------------------------------------
void KK::SaveBestMeans() {
    // Chunk sub-objects set suppressBestSave = true so parallel per-chunk EM
    // cannot corrupt the outer loop's global best-score state.
    if (suppressBestSave) return;

    ksv().cEStepCallsSave     = ksv().cEStepCallsLast;
    ksv().nDimsBest           = nDims;
    ksv().nBestClustersAlive  = nClustersAlive;

    if (ksv().BestWeight.size() < Weight.size())  ksv().BestWeight.SetSize(Weight.size());
    if (ksv().BestMean.size()   < Mean.size())    ksv().BestMean.SetSize(Mean.size());
    // BestAliveIndex is a std::vector; resize (not just reserve) before indexing.
    if (static_cast<int>(ksv().BestAliveIndex.size()) < nClustersAlive)
        ksv().BestAliveIndex.resize(MaxPossibleClusters);

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        ksv().BestAliveIndex[cc] = c;
        ksv().BestWeight[cc]     = Weight[c];
        for (int i = 0; i < nDims; i++)
            ksv().BestMean[cc * nDims + i] = Mean[c * nDims + i];
    }

    for (int cc = 1; cc < ksv().nBestClustersAlive; cc++) {
        const int c = ksv().BestAliveIndex[cc];
        for (int i = 0; i < ksv().nDimsBest; i++)
            for (int j = 0; j <= i; j++)
                bestCholFlat[static_cast<size_t>(c) * nDims2 + i * ksv().nDimsBest + j] =
                    cholFlat    [static_cast<size_t>(c) * nDims2 + i * ksv().nDimsBest + j];
    }
}

// ---------------------------------------------------------------------------
// RefractorySplitPerChunk
// ---------------------------------------------------------------------------
// Physiologically-motivated splitting phase, run after SubspaceReclusterPerChunk.
//
// Rationale: a single neuron cannot fire twice within the absolute refractory
// period (~1–1.5 ms).  Any cluster with ISI violations in that window is a
// mixture of at least two units.  This phase exploits that constraint directly:
//
//   1. For each cluster, sort its spike timestamps (normalised time dimension).
//   2. Find all refractory violations: pairs (i,j) with |t_i - t_j| < refractNorm.
//   3. Compute the contamination rate = 2 * n_violations / n_spikes.
//   4. If rate >= minContamRate AND n_spikes >= minSplitSpikes:
//      a. Partition spikes into "violator" set (at least one in a violating pair)
//         and "clean" set (no refractory violation partner).
//      b. Seed two-cluster CEM using violator centroid vs clean centroid.
//      c. Run full-space CEM in the normalised nFullDims space with splits enabled.
//      d. Accept the split only if multi-cluster BIC < single-cluster BIC.
//
// This handles cases SubspaceRecluster misses because:
//   a. The mixture of two units with similar but distinct waveforms may not
//      produce a bimodal subspace projection, yet refractory violations reveal
//      the contamination directly.
//   b. It works on clusters too small for subspace CEM (minimum 2*nFullDims).
//
// Parameters:
//   refractSamples  — absolute refractory period in raw samples (e.g. 1.5ms
//                     × SamplingRate, typically 49 at 32552 Hz)
//   minContamRate   — contamination rate threshold to trigger a split; 0.01
//                     means ≥1% violation rate (1 violation per 50 clean ISIs)
//   sessionSamples  — full recording length in samples (= timeRawMax-timeRawMin)
// ---------------------------------------------------------------------------
void KK::RefractorySplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int   nFullDims,
    float refractSamples,
    float minContamRate,
    float sessionSamples)
{
    if (refractSamples <= 0.0f || sessionSamples <= 0.0f) return;

    const int nSpatial    = nFullDims - 1;   // dimensions excluding time
    const int timeDimIdx  = nFullDims - 1;   // normalised time is last dim
    const float refractNorm = refractSamples / sessionSamples;  // in [0,1]

    // Minimum spike count for a 2-cluster fit: need enough in each half for
    // a stable nFullDims-dimensional Gaussian.  nFullDims*4 is conservative.
    const int minSplitSpikes = std::max(nFullDims * 4, 20);

    const int nCh = static_cast<int>(chunkPoints.size());
    int totalSplit = 0;

    // ── Diagnostic accounting ────────────────────────────────────────────────
    // Mirrors the accounting in SubspaceReclusterPerChunk so the user can see
    // the filter funnel: how many candidate clusters were visited, how many
    // were filtered before the CEM call, and why.  Printed at end-of-phase
    // to stderr regardless of `Log`.
    int nVisited      = 0;   // alive non-noise clusters considered
    int nSkipNoise    = 0;
    int nSkipTooSmall = 0;   // nMem < minSplitSpikes
    int nSkipLowContam= 0;   // contamRate < minContamRate (intentional: no need to split)
    int nAttempted    = 0;   // ran the CEM split trial
    int nRejNoSplit   = 0;   // CEM didn't split the cluster
    int nRejWorseNull = 0;   // splitScore >= nullScore

    for (int ck = 0; ck < nCh; ck++) {
        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;

        std::vector<ChunkModel> snapshot = mdls;
        for (const auto& origCm : snapshot) {
            const int lc = origCm.localClusterId;
            if (lc == 0) { ++nSkipNoise; continue; }
            ++nVisited;

            // Collect member local-indices and their normalised timestamps
            std::vector<int>   members;
            std::vector<float> ts;
            for (int i = 0; i < nPts; i++) {
                if (cls[static_cast<size_t>(i)] != lc) continue;
                members.push_back(i);
                ts.push_back(Data[pts[static_cast<size_t>(i)] * nDims + timeDimIdx]);
            }
            const int nMem = static_cast<int>(members.size());
            if (nMem < minSplitSpikes) { ++nSkipTooSmall; continue; }

            // Sort by time for efficient ISI scan
            std::vector<int> order(static_cast<size_t>(nMem));
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(),
                      [&ts](int a, int b){ return ts[static_cast<size_t>(a)]
                                                < ts[static_cast<size_t>(b)]; });

            // Count refractory violations (forward scan: consecutive sorted spikes)
            // and mark which spikes are involved in at least one violation.
            std::vector<bool> isViolator(static_cast<size_t>(nMem), false);
            int nViol = 0;
            for (int ii = 0; ii < nMem - 1; ii++) {
                const int a = order[static_cast<size_t>(ii)];
                const int b = order[static_cast<size_t>(ii + 1)];
                if ((ts[static_cast<size_t>(b)] - ts[static_cast<size_t>(a)])
                        < refractNorm) {
                    isViolator[static_cast<size_t>(a)] = true;
                    isViolator[static_cast<size_t>(b)] = true;
                    ++nViol;
                }
            }

            const float contamRate = 2.0f * static_cast<float>(nViol)
                                   / static_cast<float>(nMem);
            if (contamRate < minContamRate) { ++nSkipLowContam; continue; }
            ++nAttempted;

            // Log the violation
            Output("  RefractorySplit: chunk%d cluster%d  %d spikes  "
                   "%.1f%% ISI contamination (%.1fms refract)\\n",
                   ck, lc, nMem, contamRate * 100.0f,
                   refractSamples / (SamplingRate > 0.0f ? SamplingRate : 30000.0f) * 1000.0f);

            // Partition: violators vs clean.
            // If all spikes are violators (highly contaminated), fall back to
            // median split by first spatial dimension.
            std::vector<int> violIdx, cleanIdx;
            for (int ii = 0; ii < nMem; ii++) {
                if (isViolator[static_cast<size_t>(ii)]) violIdx.push_back(ii);
                else                                      cleanIdx.push_back(ii);
            }
            if (violIdx.empty() || cleanIdx.empty()) {
                // Fallback: split by median of first spatial feature
                std::vector<float> vals(static_cast<size_t>(nMem));
                for (int ii = 0; ii < nMem; ii++)
                    vals[static_cast<size_t>(ii)] =
                        Data[pts[static_cast<size_t>(members[static_cast<size_t>(ii)])]
                             * nDims + 0];
                float med = vals[static_cast<size_t>(nMem / 2)];
                std::nth_element(vals.begin(), vals.begin() + nMem/2, vals.end());
                med = vals[static_cast<size_t>(nMem / 2)];
                for (int ii = 0; ii < nMem; ii++) {
                    if (Data[pts[static_cast<size_t>(members[static_cast<size_t>(ii)])]
                             * nDims + 0] < med)
                        cleanIdx.push_back(ii);
                    else
                        violIdx.push_back(ii);
                }
            }

            // Compute centroids of violator and clean sets in normalised nFullDims space
            std::vector<float> centViol(static_cast<size_t>(nFullDims), 0.0f);
            std::vector<float> centClean(static_cast<size_t>(nFullDims), 0.0f);
            for (int ii : violIdx) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    centViol[static_cast<size_t>(d)] += Data[p * nDims + d];
            }
            for (int ii : cleanIdx) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    centClean[static_cast<size_t>(d)] += Data[p * nDims + d];
            }
            for (float& v : centViol)  v /= static_cast<float>(violIdx.size());
            for (float& v : centClean) v /= static_cast<float>(cleanIdx.size());

            // Build scratch KK for this cluster's spikes in full nFullDims space
            KK Ks;
            Ks.nDims = nFullDims; Ks.nPoints = nMem;
            Ks.penaltyMix = penaltyMix; Ks.suppressBestSave = true;
            Ks.minClustersAlive = 1; Ks.AllocateArrays(); Ks.AllocateCholeskyVecs();
            Ks.timeRawMin = timeRawMin; Ks.timeRawMax = timeRawMax;

            Ks.ReinitForSplit(nMem, nFullDims, penaltyMix);
            for (int ii = 0; ii < nMem; ii++) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    Ks.Data[ii * nFullDims + d] = Data[p * nDims + d];
            }

            // Place centroid seeds into Centres[]
            // Centres layout: [cluster-1 (clean), cluster-2 (violator)] × nFullDims
            Ks.Centres.SetSize(2 * nFullDims);
            for (int d = 0; d < nFullDims; d++) {
                Ks.Centres[0 * nFullDims + d] = centClean[static_cast<size_t>(d)];
                Ks.Centres[1 * nFullDims + d] = centViol[static_cast<size_t>(d)];
            }
            Ks.nStartingClusters = 3;  // noise(0) + clean(1) + violator(2)
            Ks.NoisePoint = 0;
            for (int c = 0; c < MaxPossibleClusters; c++)
                Ks.ClassAlive[c] = (c < 3) ? 1 : 0;
            Ks.Reindex();
            // Assign initial classes from centroid proximity
            Ks.InitClassFromCentres(nSpatial);

            // Null score (single cluster)
            Ks.ReinitForSplit(nMem, nFullDims, penaltyMix);
            for (int ii = 0; ii < nMem; ii++) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    Ks.Data[ii * nFullDims + d] = Data[p * nDims + d];
            }
            Ks.nStartingClusters = 1; Ks.NoisePoint = 0;
            for (int ii = 0; ii < nMem; ii++) Ks.Class[ii] = 1;
            Ks.ClassAlive[1] = 1; Ks.nClustersAlive = 1; Ks.AliveIndex[0] = 1;
            Ks.MStep(); Ks.EStep();
            const float nullScore = Ks.ComputeScore();

            // Multi-cluster CEM seeded from centroids
            Ks.ReinitForSplit(nMem, nFullDims, penaltyMix);
            for (int ii = 0; ii < nMem; ii++) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    Ks.Data[ii * nFullDims + d] = Data[p * nDims + d];
            }
            // Re-place centroid seeds
            Ks.Centres.SetSize(2 * nFullDims);
            for (int d = 0; d < nFullDims; d++) {
                Ks.Centres[0 * nFullDims + d] = centClean[static_cast<size_t>(d)];
                Ks.Centres[1 * nFullDims + d] = centViol[static_cast<size_t>(d)];
            }
            Ks.nStartingClusters = 3; Ks.NoisePoint = 0;
            for (int c = 0; c < MaxPossibleClusters; c++)
                Ks.ClassAlive[c] = (c < 3) ? 1 : 0;
            Ks.Reindex();
            Ks.InitClassFromCentres(nSpatial);
            Ks.timeRawMin = timeRawMin; Ks.timeRawMax = timeRawMax;

            const float splitScore = Ks.RunEMLoop(
                /*enableSplits=*/   true,
                /*enableDistDump=*/ false,
                /*maxIter=*/        0,
                /*phaseLabel=*/     "rfsplit");

            // nClustersAlive INCLUDES the noise slot (always alive even when
            // empty under NoisePoint=0).  A genuine 2-way split has noise +
            // ≥2 real clusters = nClustersAlive >= 3.  The previous `<= 1`
            // guard let the no-split case (noise + 1 real) through, which
            // then failed the splitScore < nullScore comparison anyway —
            // but the rejection was logged as "no improvement" rather than
            // "no split", masking what actually happened.
            if (Ks.nClustersAlive <= 2) {
                Output("  RefractorySplit: chunk%d cluster%d — CEM did not split "
                       "(splitScore=%.4g, null=%.4g)\\n",
                       ck, lc, splitScore, nullScore);
                ++nRejNoSplit;
                continue;
            }
            if (splitScore >= nullScore) {
                Output("  RefractorySplit: chunk%d cluster%d — split worse than null "
                       "(splitScore=%.4g, null=%.4g), keeping\\n",
                       ck, lc, splitScore, nullScore);
                ++nRejWorseNull;
                continue;
            }

            // Apply split
            Output("  RefractorySplit: chunk%d cluster%d -> %d sub-clusters "
                   "(splitScore=%.4g < null=%.4g)\\n",
                   ck, lc, Ks.nClustersAlive, splitScore, nullScore);

            // Find next free local ID
            int nextLocalId = 0;
            for (const auto& cm : mdls)
                if (cm.localClusterId > nextLocalId) nextLocalId = cm.localClusterId;
            nextLocalId++;

            std::unordered_map<int,int> subToLocal;
            subToLocal[Ks.Class[0]] = lc;  // first sub-cluster keeps the original ID
            for (int ii = 0; ii < nMem; ii++) {
                int sc = Ks.Class[ii];
                if (!subToLocal.count(sc)) subToLocal[sc] = nextLocalId++;
            }

            for (int ii = 0; ii < nMem; ii++)
                cls[static_cast<size_t>(members[static_cast<size_t>(ii)])] =
                    subToLocal[Ks.Class[ii]];

            // P2.G: per-sub-cluster shift-probe loop deleted (same as
            // SubspaceReclusterPerChunk above) — was building `sub` only to
            // discard it.

            // Remove old model entry and build new ones
            mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
                [lc](const ChunkModel& m){ return m.localClusterId == lc; }),
                mdls.end());

            for (auto& [sc2, newLc] : subToLocal) {
                ChunkModel cm;
                cm.chunkIdx = ck; cm.localClusterId = newLc;
                cm.globalClusterId = -1; cm.nMembers = 0;
                cm.mean.assign(static_cast<size_t>(nFullDims), 0.0f);
                cm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
                for (int ii = 0; ii < nPts; ii++) {
                    if (cls[static_cast<size_t>(ii)] != newLc) continue;
                    const int p2 = pts[static_cast<size_t>(ii)];
                    for (int d = 0; d < nFullDims; d++)
                        cm.mean[static_cast<size_t>(d)] += Data[p2 * nDims + d];
                    cm.nMembers++;
                }
                if (cm.nMembers > 0)
                    for (float& v : cm.mean) v /= cm.nMembers;
                for (int ii = 0; ii < nPts; ii++) {
                    if (cls[static_cast<size_t>(ii)] != newLc) continue;
                    const int p2 = pts[static_cast<size_t>(ii)];
                    for (int r = 0; r < nFullDims; r++)
                        for (int cc2 = r; cc2 < nFullDims; cc2++) {
                            float dr = Data[p2 * nDims + r]   - cm.mean[static_cast<size_t>(r)];
                            float dc = Data[p2 * nDims + cc2] - cm.mean[static_cast<size_t>(cc2)];
                            cm.cov[r * nFullDims + cc2] += dr * dc;
                        }
                }
                if (cm.nMembers > 1)
                    for (float& v : cm.cov) v /= static_cast<float>(cm.nMembers - 1);
                mdls.push_back(std::move(cm));
            }
            ++totalSplit;
        }
    }

    // End-of-phase stderr summary — always visible regardless of Log setting.
    LockedStderr(
            "[Phase 2] Per-chunk refractory split: %d split / %d attempted "
            "(visited %d, skipped: %d too-small <%d / %d low-contam <%.0f%%; "
            "rejected: %d no-split / %d worse-than-null)\n",
            totalSplit, nAttempted,
            nVisited, nSkipTooSmall, minSplitSpikes,
            nSkipLowContam, minContamRate * 100.0f,
            nRejNoSplit, nRejWorseNull);
}


// ---------------------------------------------------------------------------
// KK::KnnSplitPerChunk  (Phase 2b.5)
// ---------------------------------------------------------------------------
// K-template chunk split.  After Phase 2b has converged each chunk's
// classification, identify the K best-isolated clusters in the chunk as
// reference templates and partition the remaining "source" clusters'
// spikes by their nearest-template assignment.  Each (source, ref)
// bucket of sufficient size becomes a NEW chunk-local cluster; Phase 6
// cross-chunk template matching consolidates the new clusters into
// global units.
//
// The point: when Phase 2b's ConsiderDeletion leaves behind clusters
// with high residual variance (mixtures or drift-tail fragments), the
// parametric merge decision is unreliable precisely because the source
// cluster's own Σ estimate is bad.  K-template nearest-mean bypasses
// the bad covariance entirely — it just asks "which good template is
// each spike closest to?" — and the partition that emerges tells Phase 6
// what the source cluster ACTUALLY was a mixture of.  No spike is
// assigned to an existing reference cluster; the algorithm only
// generates new clusters (consistent with the user-side GUI version,
// see KlustersDoc::splitClusterByKnnVsReferences, patch99).
//
// Performance: brute-force O(nSrc × K × nSpatial) per chunk.  For a
// 5000-spike chunk with 2000 source spikes, K=10, 22 spatial dims:
// ~440k FLOPs — under 0.1 ms single-threaded.  No OMP parallel needed
// at this scale; the outer chunk loop is already serial-friendly.
// ---------------------------------------------------------------------------
void KK::KnnSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    if (KnnSplitK < 2) return;

    const int K                   = KnnSplitK;
    const int minRefSize          = KnnSplitMinRefSize;
    const int minSourceSize       = KnnSplitMinSourceSize;
    const int minNewClusterSize   = KnnSplitMinNewClusterSize;
    const int nSpatial            = nFullDims - 1;     // exclude time dim
    const int nCh                 = static_cast<int>(chunkPoints.size());

    // ── Diagnostic accounting ────────────────────────────────────────────────
    // Mirrors the funnel-style accounting in SubspaceReclusterPerChunk /
    // RefractorySplitPerChunk so the user can see how many clusters
    // were considered and where the filter dropped them.
    int chunksTotal           = 0;
    int chunksProcessed       = 0;
    int chunksSkippedNoRefs   = 0;
    int chunksSkippedNoSrc    = 0;
    int refClustersUsed       = 0;
    int sourceClustersVisited = 0;
    int sourceClustersSplit   = 0;
    int newClustersGenerated  = 0;
    int spikesReassigned      = 0;

    LockedStderr(
        "[Phase 2b.5] KnnSplitPerChunk: K=%d, minRefSize=%d, "
        "minSourceSize=%d, minNewClusterSize=%d\n",
        K, minRefSize, minSourceSize, minNewClusterSize);

    for (int ck = 0; ck < nCh; ck++) {
        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;
        chunksTotal++;

        // 1. Compute trace(Σ)/nSpatial for every non-noise non-empty
        //    chunk-local cluster, and index ChunkModels by localClusterId.
        std::map<int, float>  traceByLocal;
        std::map<int, int>    nByLocal;
        std::map<int, size_t> mdlIdxByLocal;  // index into mdls[]
        for (size_t k = 0; k < mdls.size(); ++k) {
            const auto& cm = mdls[k];
            if (cm.localClusterId == 0) continue;
            if (cm.nMembers == 0) continue;
            float tr = 0.0f;
            for (int j = 0; j < nSpatial; ++j)
                tr += cm.cov[static_cast<size_t>(j) * nFullDims + j];
            traceByLocal[cm.localClusterId] = tr / std::max(1, nSpatial);
            nByLocal[cm.localClusterId] = cm.nMembers;
            mdlIdxByLocal[cm.localClusterId] = k;
        }

        // 2. Reference selection: nbSpikes ≥ minRefSize AND trace below
        //    the chunk median (the "low variance" criterion).  Sort by
        //    trace ascending and take top K.
        std::vector<int>   sizeOkCandidates;
        std::vector<float> allTraces;
        for (const auto& kv : traceByLocal) allTraces.push_back(kv.second);
        if (allTraces.size() < 2) { chunksSkippedNoRefs++; continue; }
        std::sort(allTraces.begin(), allTraces.end());
        const float medianTrace = allTraces[allTraces.size() / 2];

        for (const auto& kv : nByLocal)
            if (kv.second >= minRefSize
                && traceByLocal[kv.first] < medianTrace)
                sizeOkCandidates.push_back(kv.first);

        if (sizeOkCandidates.size() < 2) { chunksSkippedNoRefs++; continue; }

        std::sort(sizeOkCandidates.begin(), sizeOkCandidates.end(),
                  [&](int a, int b) {
                      return traceByLocal[a] < traceByLocal[b];
                  });
        if (static_cast<int>(sizeOkCandidates.size()) > K)
            sizeOkCandidates.resize(K);
        const int nRefs = static_cast<int>(sizeOkCandidates.size());
        const std::vector<int>& refs = sizeOkCandidates;

        // Pre-cache reference mean pointers — stable because mdls[] is
        // not mutated during the partitioning loop (we only push_back
        // new entries afterwards, see step 6).
        std::vector<const float*> refMeans(nRefs);
        for (int r = 0; r < nRefs; ++r)
            refMeans[r] = mdls[mdlIdxByLocal[refs[r]]].mean.data();

        // 3. Source selection: every other non-noise cluster with
        //    nbSpikes ≥ minSourceSize.  No upper-percentile filter — once
        //    refs are fixed, anything not in the ref set with enough
        //    spikes is a candidate.
        std::set<int> refSet(refs.begin(), refs.end());
        std::vector<int> sources;
        for (const auto& kv : nByLocal)
            if (!refSet.count(kv.first) && kv.second >= minSourceSize)
                sources.push_back(kv.first);
        if (sources.empty()) { chunksSkippedNoSrc++; continue; }
        sourceClustersVisited += static_cast<int>(sources.size());

        // 4. Next free local cluster ID — new clusters appended at tail.
        int nextLocalId = 0;
        for (const auto& cm : mdls)
            nextLocalId = std::max(nextLocalId, cm.localClusterId);
        nextLocalId++;

        // 5. Per source-cluster, partition each spike by nearest template.
        //    bucket[srcId][refIdx] = LOCAL chunk-relative indices.
        std::map<int, std::vector<std::vector<int>>> bucket;
        for (int srcId : sources) bucket[srcId].resize(nRefs);
        std::set<int> sourceSet(sources.begin(), sources.end());

        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (!sourceSet.count(c)) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            float bestD = std::numeric_limits<float>::max();
            int   bestR = 0;
            for (int r = 0; r < nRefs; ++r) {
                const float* rm = refMeans[r];
                float d = 0.0f;
                for (int j = 0; j < nSpatial; ++j) {
                    const float diff = x[j] - rm[j];
                    d += diff * diff;
                }
                if (d < bestD) { bestD = d; bestR = r; }
            }
            bucket[c][bestR].push_back(i);
        }

        // 6. Materialize buckets ≥ minNewClusterSize as new chunk-local
        //    clusters.  Smaller buckets leave their spikes in source.
        bool anySplit = false;
        for (int srcId : sources) {
            int srcSplit = 0;
            for (int r = 0; r < nRefs; ++r) {
                auto& indices = bucket[srcId][r];
                if (static_cast<int>(indices.size()) < minNewClusterSize)
                    continue;

                const int newId = nextLocalId++;
                for (int i : indices) cls[i] = newId;

                ChunkModel newCm{};
                newCm.chunkIdx        = ck;
                newCm.localClusterId  = newId;
                newCm.globalClusterId = -1;
                newCm.nMembers        = static_cast<int>(indices.size());
                newCm.mean.assign(nFullDims, 0.0f);
                newCm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
                mdls.push_back(newCm);

                spikesReassigned     += static_cast<int>(indices.size());
                newClustersGenerated++;
                srcSplit++;
                anySplit = true;
            }
            if (srcSplit > 0) sourceClustersSplit++;
        }

        if (!anySplit) continue;
        refClustersUsed += nRefs;
        chunksProcessed++;

        // 7. Rebuild .mean and .nMembers for every cluster in this chunk
        //    from the updated label assignment.  Cov is left zero-filled
        //    for new clusters — Phase 2c alignment and Phase 3 template
        //    harvest only read .mean here; Phase 6 recomputes its own
        //    statistics from the realigned waveforms.
        std::map<int, int>                  newN;
        std::map<int, std::vector<double>>  newMeanAcc;
        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (c == 0) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            newN[c]++;
            auto& acc = newMeanAcc[c];
            if (acc.empty()) acc.assign(nFullDims, 0.0);
            for (int j = 0; j < nFullDims; ++j) acc[j] += x[j];
        }
        for (auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            const int n = newN[cm.localClusterId];
            cm.nMembers = n;
            if (n == 0) { cm.mean.assign(nFullDims, 0.0f); continue; }
            const auto& acc = newMeanAcc[cm.localClusterId];
            cm.mean.assign(nFullDims, 0.0f);
            for (int j = 0; j < nFullDims; ++j)
                cm.mean[j] = static_cast<float>(acc[j] / n);
        }
        // Drop emptied source ChunkModels.
        mdls.erase(
            std::remove_if(mdls.begin(), mdls.end(),
                [](const ChunkModel& cm) {
                    return cm.localClusterId != 0 && cm.nMembers == 0;
                }),
            mdls.end());
    }

    LockedStderr(
        "[Phase 2b.5] KnnSplitPerChunk: chunks=%d (processed=%d, "
        "skipped[no refs]=%d, skipped[no sources]=%d), "
        "ref clusters used=%d, source clusters visited=%d, "
        "split=%d, new clusters generated=%d, spikes reassigned=%d\n",
        chunksTotal, chunksProcessed, chunksSkippedNoRefs,
        chunksSkippedNoSrc, refClustersUsed, sourceClustersVisited,
        sourceClustersSplit, newClustersGenerated, spikesReassigned);
}

// ---------------------------------------------------------------------------
// KK::WaveKnnSplitPerChunk  (Phase 2b.5, klusters-faithful variant)
// ---------------------------------------------------------------------------
// Replaces KKE's nearest-template KnnSplitPerChunk with the klusters-
// faithful K-nearest-neighbours majority-vote split.  The semantic
// differences (see wave_knn_split.h for full details):
//
//   • Per-spike K-NN against a POOL of reference SPIKES (not means)
//   • Majority-vote threshold (WaveKnnMajorityThreshold) — spikes whose
//     K nearest neighbours don't agree above the threshold stay in
//     source (residual bucket).
//
// The latter is the missing mechanism that bounds fragmentation:
// klusters' algorithm leaves ambiguous spikes alone; KKE's existing
// version force-assigns every source spike to the closest reference,
// producing up to K-way fragmentation per source.  In the sirotaA
// group-6 benchmark this generated 3125 new clusters from 753 sources
// (avg 4.1-way split), most too small for downstream xcorr template-
// matching to behave sanely, leading to cascade collapse-to-noise.
//
// Activated via -KnnSplitPerChunkEnable 1 -KnnSplitMode 1.
// ---------------------------------------------------------------------------
void KK::WaveKnnSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims) {
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    LockedStderr(
        "[Phase 2b.5] WaveKnnSplitPerChunk (klusters-faithful): "
        "K=%d majThr=%.2f minRefSize=%d minSourceSize=%d minNewClusterSize=%d\n",
        KnnSplitK, WaveKnnMajorityThreshold,
        KnnSplitMinRefSize, KnnSplitMinSourceSize, KnnSplitMinNewClusterSize);

    int chunksTotal           = 0;
    int chunksCalled          = 0;
    int chunksProcessed       = 0;
    int totalSourcesConsidered = 0;
    int totalSourcesAnisoFiltered = 0;
    int totalSourcesSplit      = 0;
    int totalNewClusters       = 0;
    int totalResidualClusters  = 0;
    int totalSpikesReassigned  = 0;
    int totalSpikesResidual    = 0;

    for (int ck = 0; ck < nCh; ++ck) {
        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;
        chunksTotal++;

        // Build per-chunk feature scratch: copy the slice of Data
        // corresponding to chunkPoints[ck] into a contiguous buffer the
        // way wave_knn_split expects (nPts × nFullDims, row-major).
        std::vector<float> chunkFeat(static_cast<size_t>(nPts) * nFullDims);
        for (int i = 0; i < nPts; ++i) {
            const float* src = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            std::copy(src, src + nFullDims,
                      chunkFeat.data() + static_cast<size_t>(i) * nFullDims);
        }

        // Build the chunk-local label array (copy in, modify, copy out).
        std::vector<int> chunkLabels(cls.begin(), cls.end());

        // Build trace vector (one entry per non-zero localClusterId).
        // Also compute anisotropy ratio λ_max(Σ)/tr(Σ) via power iteration
        // — this is the mixture detector that gates source candidates
        // when WaveKnnMinSourceAnisotropy > 0.  For nDim=22 a unimodal
        // cluster has anisotropy ≈ 1/22 ≈ 0.045; a clear 2-unit mixture
        // gives 0.3–0.7.
        const int nSpatial = nFullDims - 1;
        std::vector<float> traces;
        std::vector<int>   traceIds;
        std::vector<float> aniso;
        std::vector<int>   anisoIds;
        traces.reserve(mdls.size());
        traceIds.reserve(mdls.size());
        aniso.reserve(mdls.size());
        anisoIds.reserve(mdls.size());

        // Scratch vectors for power iteration (re-used per cluster).
        std::vector<double> v(nSpatial), w(nSpatial);
        for (const auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            if (cm.nMembers == 0) continue;
            float tr = 0.0f;
            for (int j = 0; j < nSpatial; ++j) {
                tr += cm.cov[static_cast<size_t>(j) * nFullDims + j];
            }
            traces.push_back(tr / std::max(1, nSpatial));
            traceIds.push_back(cm.localClusterId);

            // Skip anisotropy if trace is degenerate.
            if (tr <= 1e-20f || nSpatial < 2) continue;

            // Power iteration: λ_max of the upper-triangle-populated
            // cov matrix.  Symmetrise on-the-fly by reading
            // cov[i,j] = cov[min(i,j), max(i,j)].
            auto C = [&](int i, int j) -> float {
                if (i > j) std::swap(i, j);
                return cm.cov[static_cast<size_t>(i) * nFullDims + j];
            };
            // Seed v with normalised diagonal — biases toward the
            // largest-variance axis, converges in ~10-20 iters for the
            // matrices we see.
            double sumAbs = 0.0;
            for (int i = 0; i < nSpatial; ++i) {
                v[i] = std::abs(C(i, i)) + 1e-12;
                sumAbs += v[i] * v[i];
            }
            const double invNorm0 = 1.0 / std::sqrt(sumAbs);
            for (int i = 0; i < nSpatial; ++i) v[i] *= invNorm0;

            double lambda = 0.0;
            const int powerIters = 30;
            for (int it = 0; it < powerIters; ++it) {
                // w = C * v
                for (int i = 0; i < nSpatial; ++i) {
                    double acc = 0.0;
                    for (int j = 0; j < nSpatial; ++j) acc += C(i, j) * v[j];
                    w[i] = acc;
                }
                // λ = ||w||
                double n2 = 0.0;
                for (int i = 0; i < nSpatial; ++i) n2 += w[i] * w[i];
                if (n2 < 1e-40) { lambda = 0.0; break; }
                lambda = std::sqrt(n2);
                const double inv = 1.0 / lambda;
                for (int i = 0; i < nSpatial; ++i) v[i] = w[i] * inv;
            }
            const float ratio = static_cast<float>(lambda / tr);
            aniso.push_back(ratio);
            anisoIds.push_back(cm.localClusterId);
        }

        wave_knn_split::Config cfg;
        cfg.K                          = KnnSplitK;
        cfg.majorityThreshold          = WaveKnnMajorityThreshold;
        cfg.minRefClusterSize          = KnnSplitMinRefSize;
        cfg.minSourceClusterSize       = KnnSplitMinSourceSize;
        cfg.minNewClusterSize          = KnnSplitMinNewClusterSize;
        cfg.useTraceFilter             = (WaveKnnUseTraceFilter != 0);
        cfg.skipMuaCluster1            = (WaveKnnSkipMuaCluster1 != 0);
        cfg.noiseSourceProbability     = WaveKnnNoiseSourceProbability;
        // Vary the RNG seed per chunk so the noiseSourceProbability draw
        // is INDEPENDENT across chunks.  If RandomSeed=0 (time-based),
        // wave_knn_split will use std::time(nullptr); same effect (each
        // chunk gets a different stream because std::time() advances).
        // Otherwise: deterministic, but per-chunk-unique = RandomSeed XOR
        // (chunk_idx * 1000003), a prime offset that avoids low-bit
        // collisions in the mt19937 stream.
        cfg.rngSeed                    = (RandomSeed != 0)
            ? static_cast<unsigned>(RandomSeed) ^
              (static_cast<unsigned>(ck) * 1000003u)
            : 0u;
        cfg.residualBecomesNewCluster  = (WaveKnnResidualBecomesCluster != 0);
        cfg.minSourceAnisotropy        = WaveKnnMinSourceAnisotropy;
        cfg.Verbose                    = (Verbose >= 2);

        auto r = wave_knn_split::Run(chunkFeat.data(), nPts, nFullDims,
                                      chunkLabels, traces, traceIds,
                                      aniso, anisoIds, cfg);

        // Always counted: wave_knn_split was invoked on this chunk and
        // reported how many source clusters it considered.  These counters
        // are needed to distinguish "wave_knn_split rejected every
        // candidate" from "wave_knn_split was never called".
        chunksCalled++;
        totalSourcesConsidered    += r.nSourcesConsidered;
        totalSourcesAnisoFiltered += r.nSourcesAnisotropyFiltered;

        if (r.nNewClusters == 0) continue;

        // Identify which labels appeared in chunkLabels that weren't in
        // the original `cls` — these are the new sub-clusters introduced
        // by wave_knn_split (IDs allocated after maxLabel+1).
        std::set<int> oldIds;
        for (const auto& cm : mdls) oldIds.insert(cm.localClusterId);

        // Apply updated labels back to cls.
        for (int i = 0; i < nPts; ++i) cls[i] = chunkLabels[i];

        // Materialise ChunkModels for the new clusters.  Means recomputed
        // below; cov left zero (recomputed by downstream phases).
        std::map<int, int>                 newN;
        std::map<int, std::vector<double>> newMeanAcc;
        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (c == 0) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            newN[c]++;
            auto& acc = newMeanAcc[c];
            if (acc.empty()) acc.assign(nFullDims, 0.0);
            for (int j = 0; j < nFullDims; ++j) acc[j] += x[j];
        }
        // Append entries for new IDs.
        for (const auto& kv : newN) {
            if (oldIds.count(kv.first)) continue;
            ChunkModel newCm{};
            newCm.chunkIdx        = ck;
            newCm.localClusterId  = kv.first;
            newCm.globalClusterId = -1;
            newCm.nMembers        = kv.second;
            newCm.mean.assign(nFullDims, 0.0f);
            newCm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
            mdls.push_back(newCm);
        }
        // Rebuild means + counts for all clusters in the chunk.
        for (auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            const int n = newN[cm.localClusterId];
            cm.nMembers = n;
            if (n == 0) { cm.mean.assign(nFullDims, 0.0f); continue; }
            const auto& acc = newMeanAcc[cm.localClusterId];
            cm.mean.assign(nFullDims, 0.0f);
            for (int j = 0; j < nFullDims; ++j)
                cm.mean[j] = static_cast<float>(acc[j] / n);
        }

        // CRITICAL: rebuild covariances for all clusters with current
        // membership.  Without this, the anisotropy gate (patch 0021)
        // sees zero-cov on every cluster wave_knn_split touched — including
        // newly-created sub-clusters — and admits them all on the next
        // Phase 4b iteration regardless of whether they look like
        // mixtures.  That's the cluster-explosion regression where
        // a session ends up with 10k+ local clusters before merge.
        //
        // Upper-triangle accumulation, divide by n at end (population
        // covariance, matches ChunkReCEM convention).  O(nPts·nDims²/2)
        // per chunk; ~60ms total for the user's session (36 chunks,
        // nFullDims=22, ~7k pts/chunk).
        std::map<int, std::vector<double>> newCovAcc;
        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (c == 0) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            // Find the cluster's mean we just computed (use newMeanAcc/n).
            auto itAcc = newMeanAcc.find(c);
            const int n = newN[c];
            if (itAcc == newMeanAcc.end() || n == 0) continue;
            const auto& meanAcc = itAcc->second;
            auto& covAcc = newCovAcc[c];
            if (covAcc.empty())
                covAcc.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0);
            for (int j = 0; j < nFullDims; ++j) {
                const double dj = static_cast<double>(x[j])
                                - meanAcc[j] / n;
                for (int k = j; k < nFullDims; ++k) {
                    const double dk = static_cast<double>(x[k])
                                    - meanAcc[k] / n;
                    covAcc[static_cast<size_t>(j) * nFullDims + k] += dj * dk;
                }
            }
        }
        // Assign to ChunkModels (upper triangle; symmetric reader assumed).
        for (auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            const int n = cm.nMembers;
            if (n == 0) continue;
            auto it = newCovAcc.find(cm.localClusterId);
            if (it == newCovAcc.end()) continue;
            const auto& acc = it->second;
            if (cm.cov.size() != static_cast<size_t>(nFullDims) * nFullDims)
                cm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
            const double invN = 1.0 / n;
            for (int j = 0; j < nFullDims; ++j) {
                for (int k = j; k < nFullDims; ++k) {
                    cm.cov[static_cast<size_t>(j) * nFullDims + k] =
                        static_cast<float>(
                            acc[static_cast<size_t>(j) * nFullDims + k] * invN);
                }
            }
        }

        // Drop emptied source ChunkModels.
        mdls.erase(
            std::remove_if(mdls.begin(), mdls.end(),
                [](const ChunkModel& cm) {
                    return cm.localClusterId != 0 && cm.nMembers == 0;
                }),
            mdls.end());

        chunksProcessed++;
        totalSourcesSplit      += r.nSourcesSplit;
        totalNewClusters       += r.nNewClusters;
        totalResidualClusters  += r.nResidualClusters;
        totalSpikesReassigned  += r.nSpikesReassigned;
        totalSpikesResidual    += r.nSpikesResidual;
    }

    LockedStderr(
        "[Phase 2b.5] WaveKnnSplitPerChunk: chunks=%d (called=%d, with-splits=%d), "
        "sources visited=%d, aniso-filtered=%d, split=%d, new clusters=%d "
        "(of which residual=%d), spikes reassigned=%d, "
        "spikes kept-in-source=%d\n",
        chunksTotal, chunksCalled, chunksProcessed,
        totalSourcesConsidered, totalSourcesAnisoFiltered,
        totalSourcesSplit, totalNewClusters,
        totalResidualClusters,
        totalSpikesReassigned, totalSpikesResidual);
}

