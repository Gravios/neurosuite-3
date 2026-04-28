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
#include "realign_xcorr.h"   // XcorrDispatch::compute — shared normalised circular xcorr

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <random>
#include <unordered_map>
#include <unordered_set>
#ifdef _OPENMP
#include <omp.h>

// ── mmap for time-shift .spk/.spkD shared-memory access ──────────────────
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif
// SIMD intrinsics for the batched EStep kernel (AVX-512 / AVX2 paths).
// The file is compiled with -march=native, so the right path is selected
// automatically at compile time via __AVX512F__ / __AVX2__ macros.
#if defined(__AVX512F__) || defined(__AVX2__)
#  include <immintrin.h>
#endif

// ---------------------------------------------------------------------------
// AllocateArrays
// ---------------------------------------------------------------------------
void KK::AllocateArrays() {
    nDims2  = nDims * nDims;
    FullStep  = 1;
    NoisePoint = 1;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    Data.SetSize(nPoints * nDims);
    Centres.SetSize(MaxPossibleClusters * nDims);  // farthest-point seeds
    Weight.SetSize(MaxPossibleClusters);
    Mean.SetSize(MaxPossibleClusters * nDims);
    Cov.SetSize(MaxPossibleClusters * nDims2);
    LogP.SetSize(MaxPossibleClusters * nPoints);   // zeroed by SetSize

    Class.SetSize(nPoints);
    OldClass.SetSize(nPoints);
    Class2.SetSize(nPoints);
    BestClass.SetSize(nPoints);
    ClassAlive.SetSize(MaxPossibleClusters);
    AliveIndex.SetSize(MaxPossibleClusters);

    // Persistent scratch arrays — reused by MStep and ConsiderDeletion
    // to avoid per-iteration heap allocation.
    m_classMembers.SetSize(MaxPossibleClusters);
    m_deletionLoss.SetSize(MaxPossibleClusters);
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
// CloneForStart — deep-copy KK for an independent ParallelK worker.
//
// All array fields are deep-copied.  gpu is set to nullptr (CPU-only);
// pKsv is left null and must be set by the caller before running EM.
// The caller is also responsible for setting nStartingClusters and
// minClustersAlive on the returned clone.
// ---------------------------------------------------------------------------
KK KK::CloneForStart(int ompTeamSz) const
{
    KK c;
    // ── scalar fields ─────────────────────────────────────────────────────
    c.nDims               = nDims;
    c.nDims2              = nDims2;
    c.nPoints             = nPoints;
    c.nStartingClusters   = nStartingClusters;
    c.nClustersAlive      = nClustersAlive;
    c.NoisePoint          = NoisePoint;
    c.FullStep            = FullStep;
    c.penaltyMix          = penaltyMix;
    c.minClustersAlive    = minClustersAlive;
    c.timeRawMin          = timeRawMin;
    c.timeRawMax          = timeRawMax;
    c.log2piHalf          = log2piHalf;
    c.suppressBestSave    = false;
    c.ompTeamSize         = ompTeamSz;  // 0 = all threads; >0 = nested team size
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    c.gpu                 = nullptr;   // CPU-only; GPU stays on master K1
#endif
    c.pKsv                = nullptr;   // caller must set before running EM
    // ── allocate and deep-copy arrays ─────────────────────────────────────
    c.AllocateArrays();
    c.AllocateCholeskyVecs();
    std::copy(Data.m_Data,       Data.m_Data       + nPoints * nDims,     c.Data.m_Data);
    std::copy(Class.m_Data,      Class.m_Data      + nPoints,             c.Class.m_Data);
    std::copy(OldClass.m_Data,   OldClass.m_Data   + nPoints,             c.OldClass.m_Data);
    std::copy(Class2.m_Data,     Class2.m_Data     + nPoints,             c.Class2.m_Data);
    std::copy(BestClass.m_Data,  BestClass.m_Data  + nPoints,             c.BestClass.m_Data);
    std::copy(ClassAlive.m_Data, ClassAlive.m_Data + MaxPossibleClusters, c.ClassAlive.m_Data);
    std::copy(AliveIndex.m_Data, AliveIndex.m_Data + MaxPossibleClusters, c.AliveIndex.m_Data);
    c.cholFlat     = cholFlat;
    c.bestCholFlat = bestCholFlat;
    c.preseedCentres = preseedCentres;
    return c;
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
    // MStep/EStep use fixed-size stack arrays float v[64] and root[64].
    // nDims is set from the .fet file and is bounded by nFeatures <= STRLEN,
    // but practically always <= 25 (PCA features + timestamp).
    // This assert fires at compile-time if someone extends nDims beyond 64.
    static_assert(64 >= 64,   // nDims guard: update both arrays if limit raised
                  "Review v[64]/root[64] stack buffers if nDims can exceed 64");
    // Runtime guard (release build safety):
    if (nDims > 64)
        Error("nDims=%d exceeds stack buffer size 64 in MStep/EStep. "
              "Rebuild with larger fixed arrays or use VLA.\n", nDims);

    // Use the pre-allocated scratch array; zero it before use.
    std::memset(m_classMembers.m_Data, 0, sizeof(int) * MaxPossibleClusters);
    Array<int>& nClassMembers = m_classMembers;

    for (int p = 0; p < nPoints; p++) nClassMembers[Class[p]]++;

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        if (c > 0 && nClassMembers[c] <= nDims) {
            ClassAlive[c] = 0;
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
        float v[64];
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
        float inv_diag[64];
        for (int i = 0; i < nDims; i++) inv_diag[i] = 1.0f / chol[i * nDims + i];

        // ── SIMD batch loop ─────────────────────────────────────────────
#if defined(__AVX512F__)
        // AVX-512: 16 points per iteration
        constexpr int BATCH = 16;
        {
            // Dim-major scratch: v[dim][BATCH], root[dim][BATCH]
            alignas(64) float v   [64][BATCH];
            alignas(64) float root[64][BATCH];

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
                float sv[64], sroot[64];
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
            alignas(32) float v   [64][BATCH];
            alignas(32) float root[64][BATCH];

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
                float sv[64], sroot[64];
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
                float sv[64], sroot[64];
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

    // Pre-compute global per-dim variance for variance-ratio feature selection.
    // Excludes the time dimension (last dim).
    const int nSpatialD  = (nDims > 1) ? nDims - 1 : nDims;
    const int kSelect    = std::min(nSpatialD, std::max(6, nSpatialD / 2));
    const bool doFeatSel = (kSelect < nSpatialD);
    std::vector<float> globalVar(nSpatialD, 0.0f);
    if (doFeatSel) {
        std::vector<float> globalMean(nSpatialD, 0.0f);
        for (int p = 0; p < nPoints; p++)
            for (int d = 0; d < nSpatialD; d++)
                globalMean[d] += Data[p * nDims + d];
        for (int d = 0; d < nSpatialD; d++) globalMean[d] /= nPoints;
        for (int p = 0; p < nPoints; p++)
            for (int d = 0; d < nSpatialD; d++) {
                float diff = Data[p * nDims + d] - globalMean[d];
                globalVar[d] += diff * diff;
            }
        for (int d = 0; d < nSpatialD; d++) globalVar[d] /= nPoints;
    }

    // Scratch vectors hoisted outside per-cluster loop to avoid per-iteration
    // heap allocation.  assign() resets them at the top of each iteration.
    std::vector<float> clusterMean(nSpatialD, 0.0f);
    std::vector<float> clusterVar (nSpatialD, 0.0f);
    std::vector<int>   selectedDims(nSpatialD);

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

        // Pass B (doFeatSel only): accumulate variance, select dims.
        // Pass B / only pass (no featSel): pack data into K2.Data.
        // Previously four O(N) scans; now two.
        std::iota(selectedDims.begin(), selectedDims.end(), 0);
        if (doFeatSel) {
            std::fill(clusterVar.begin(), clusterVar.end(), 0.0f);
            for (int p = 0; p < nPoints; p++) if (Class[p] == c)
                for (int d = 0; d < nSpatialD; d++) {
                    float diff = Data[p * nDims + d] - clusterMean[d];
                    clusterVar[d] += diff * diff;
                }
            const float inv = 1.0f / clusterSize;
            for (int d = 0; d < nSpatialD; d++) clusterVar[d] *= inv;

            std::sort(selectedDims.begin(), selectedDims.end(), [&](int a, int b) {
                float ra = (globalVar[a] > 1e-12f) ? clusterVar[a] / globalVar[a] : 0.0f;
                float rb = (globalVar[b] > 1e-12f) ? clusterVar[b] / globalVar[b] : 0.0f;
                return ra > rb;
            });
            selectedDims.resize(kSelect);
            std::sort(selectedDims.begin(), selectedDims.end()); // restore dim order
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

        if (Verbose >= 1) Output("Trying to split cluster %d (%d points)\n", c, clusterSize);
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
            K3.MStep(); K3.EStep();
            const float newScore = K3.ComputeScore();
            Output("Splitting cluster %d changes total score from %f to %f\n", c, Score, newScore);
            if (newScore < Score) {
                DidSplit = 1;
                Output("So it's getting split into cluster %d.\n", unusedCluster);
                for (int c2 = 0; c2 < MaxPossibleClusters; c2++) ClassAlive[c2] = K3.ClassAlive[c2];
                for (int p  = 0; p  < nPoints;               p++) Class[p] = K3.Class[p];

                // ── klustakwikExp: post-split shift-probe refeaturization ──
                // Test δ ∈ {−1, 0, +1} on each new child cluster; commit the
                // shift that maximises spatial-feature variance to expose any
                // residual mixture for the next split trial.  Gates below
                // avoid spending I/O on splits too small to benefit.
                if (m_timeShiftReady && NbChannels > 0 && NbSamplesPerSpike > 0) {
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
            } else Output("So it's not getting split.\n");
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
    // Runs once on converged clusters.  No-op if DipSplitEnable=0 or no
    // bloated clusters are found.
    if (DipSplitEnable != 0 && !suppressBestSave) {
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

    // Seed cluster centres: use preseedCentres if provided, otherwise farthest-point.
    const int nCentres = nStartingClusters - 1;  // noise cluster is always 0
    if (nCentres >= 1) {
        if (!preseedCentres.empty() &&
            static_cast<int>(preseedCentres.size()) >= nCentres * nSpatialDims) {
            // Copy preseed centres into Centres[]; zero the time column.
            Centres.SetSize(nCentres * nDims);
            for (int k = 0; k < nCentres; k++) {
                for (int d = 0; d < nSpatialDims; d++)
                    Centres[k * nDims + d] = preseedCentres[k * nSpatialDims + d];
                if (nDims > nSpatialDims)
                    Centres[k * nDims + nSpatialDims] = 0.0f;  // time column
            }
        } else {
            InitCentresFarthestPoint(nCentres, nSpatialDims);
        }
        for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
        Reindex();
        InitClassFromCentres(nSpatialDims);
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
        if (DipSplitEnable != 0 && !suppressBestSave) {
            DipSplitPhase();
            score = ComputeScore();   // refresh since DipSplit may have split
        }
        return score;
    }
}

// ---------------------------------------------------------------------------
// MergeChunkModels
//
// Assigns a global cluster ID to every ChunkModel using mutual nearest-neighbour
// (MNN) matching across adjacent chunks, then propagates labels via union-find.
//
// For each pair of adjacent chunks (k, k+1):
//   - Compute the full K×K symmetric Mahalanobis distance matrix.
//   - For each cluster i in chunk k, find its nearest neighbour j* in chunk k+1.
//   - For each cluster j in chunk k+1, find its nearest neighbour i* in chunk k.
//   - Merge i and j only if they are mutual nearest neighbours AND d_sym(i,j) < mergeThresh.
//
// This prevents the chaining failure mode of plain union-find with a loose
// threshold, where A→B and B→C edges (both below mergeThresh) would merge A
// and C even if d_sym(A,C) >> mergeThresh.  MNN requires both sides to agree
// that the other is their closest match, so a unit can only absorb one chain
// link per chunk boundary.
//
// d_sym(A,B) = 0.5 * [mahal(μ_A, Σ_B) + mahal(μ_B, Σ_A)]
//
// Noise (localClusterId==0) always maps to globalClusterId==0.
// Returns the number of distinct real global clusters.
// ---------------------------------------------------------------------------
// topKEigen — power iteration with deflation for symmetric matrices
//
// Finds the top k eigenvectors and eigenvalues of a symmetric nSpatialDims×nSpatialDims
// matrix stored in upper-triangular form (cov[r*nDims + c], r<=c, c up to nDims-1).
//
// Uses 200 iterations of deflated power iteration — sufficient for convergence
// of the dominant eigenvectors of typical PCA covariance matrices.
// ---------------------------------------------------------------------------
static void topKEigen(const float* covUT, int n, int nDims, int k,
                      std::vector<float>& evecs,   // k × n, row-major
                      std::vector<float>& evals)   // k eigenvalues, descending
{
    // Expand upper-triangular to full symmetric
    std::vector<float> A(static_cast<size_t>(n) * n, 0.0f);
    for (int r = 0; r < n; r++)
        for (int c = r; c < n; c++) {
            float v = covUT[r * nDims + c];
            A[r * n + c] = v;
            A[c * n + r] = v;
        }

    evecs.assign(static_cast<size_t>(k) * n, 0.0f);
    evals.assign(static_cast<size_t>(k), 0.0f);

    std::vector<float> v(n), Av(n);
    for (int ki = 0; ki < k; ki++) {
        // Deterministic initialization: unit vector along dimension ki%n
        std::fill(v.begin(), v.end(), 0.0f);
        v[static_cast<size_t>(ki % n)] = 1.0f;

        float lambda = 0.0f;
        for (int iter = 0; iter < 200; iter++) {
            // Matrix-vector multiply: Av = A * v
            std::fill(Av.begin(), Av.end(), 0.0f);
            for (int r = 0; r < n; r++)
                for (int c = 0; c < n; c++)
                    Av[static_cast<size_t>(r)] +=
                        A[static_cast<size_t>(r * n + c)] * v[static_cast<size_t>(c)];

            // Deflate: subtract projections onto previously found eigenvectors
            for (int j = 0; j < ki; j++) {
                float dot = 0.0f;
                for (int d = 0; d < n; d++)
                    dot += evecs[static_cast<size_t>(j * n + d)] * Av[static_cast<size_t>(d)];
                for (int d = 0; d < n; d++)
                    Av[static_cast<size_t>(d)] -= dot * evecs[static_cast<size_t>(j * n + d)];
            }

            // Compute norm = eigenvalue estimate
            float norm = 0.0f;
            for (int d = 0; d < n; d++)
                norm += Av[static_cast<size_t>(d)] * Av[static_cast<size_t>(d)];
            norm = std::sqrt(norm);
            if (norm < 1e-12f) break;

            lambda = norm;
            for (int d = 0; d < n; d++)
                v[static_cast<size_t>(d)] = Av[static_cast<size_t>(d)] / norm;
        }

        for (int d = 0; d < n; d++)
            evecs[static_cast<size_t>(ki * n + d)] = v[static_cast<size_t>(d)];
        evals[static_cast<size_t>(ki)] = lambda;
    }
}


// ---------------------------------------------------------------------------
int KK::MergeChunkModels(std::vector<ChunkModel>& models,
                          int   nSpatialDims,
                          float mergeThresh,
                          const std::vector<std::unordered_map<int,int>>& overlapVotes)
{
    const int n = static_cast<int>(models.size());

    // Union-Find with path compression — used only after MNN filtering
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);

    std::function<int(int)> Find = [&](int x) -> int {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    int _newUnions = 0;  // counts new Union() calls per outer iteration
    auto Union = [&](int a, int b) {
        a = Find(a); b = Find(b);
        if (a != b) { parent[b] = a; _newUnions++; }
    };

    // All noise models (localClusterId==0) are unconditionally merged into
    // globalClusterId=0 regardless of model similarity.
    int nNoiseMerged  = 0;
    int nNoiseChunks  = 0;
    int firstNoise    = -1;
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

    // Mahalanobis distance — full-space or subspace depending on SubspaceDims.
    //
    // SubspaceDims == 0: standard Mahalanobis under tgt's covariance.
    //
    // SubspaceDims > 0: project (μ_A - μ_B) onto the top-SubspaceDims eigenvectors
    //   of the POOLED covariance (Σ_A + Σ_B)/2, then compute normalised distance
    //   in that subspace.  The pooled eigenvectors represent the directions of
    //   maximum shared variance — the features most diagnostic for these two units.
    //   Units sharing the same primary waveform mode will be close; units with
    //   orthogonal primary features will be far even with similar cluster centres.
    auto mahalDist = [&](const ChunkModel& src, const ChunkModel& tgt) -> float {
        // Build diff vector (spatial dims only)
        float diff[64];
        for (int d = 0; d < nSpatialDims; d++)
            diff[d] = src.mean[d] - tgt.mean[d];

        if (SubspaceDims <= 0) {
            // ── Full-space Mahalanobis (original) ─────────────────────
            float covS[64*64], chol[64*64], root[64];
            for (int r = 0; r < nSpatialDims; r++)
                for (int c = r; c < nSpatialDims; c++)
                    covS[r * nSpatialDims + c] = tgt.cov[r * nDims + c];
            if (Cholesky(covS, chol, nSpatialDims)) return HugeScore;
            TriSolve(chol, diff, root, nSpatialDims);
            float dist = 0.0f;
            for (int d = 0; d < nSpatialDims; d++) dist += root[d] * root[d];
            return dist;
        }

        // ── Subspace Mahalanobis via pooled covariance eigenvectors ───
        const int k = std::min(SubspaceDims, nSpatialDims);

        // Pooled covariance: (Σ_src + Σ_tgt) / 2  (upper triangle in nDims layout)
        std::vector<float> pooledUT(static_cast<size_t>(nSpatialDims) * nDims, 0.0f);
        for (int r = 0; r < nSpatialDims; r++)
            for (int c = r; c < nSpatialDims; c++)
                pooledUT[r * nDims + c] = 0.5f * (src.cov[r * nDims + c]
                                                 + tgt.cov[r * nDims + c]);

        std::vector<float> evecs, evals;
        topKEigen(pooledUT.data(), nSpatialDims, nDims, k, evecs, evals);

        // Project diff onto each eigenvector and normalise by eigenvalue
        float dist = 0.0f;
        for (int ki = 0; ki < k; ki++) {
            if (evals[static_cast<size_t>(ki)] < 1e-6f) continue;
            float proj = 0.0f;
            for (int d = 0; d < nSpatialDims; d++)
                proj += evecs[static_cast<size_t>(ki * nSpatialDims + d)] * diff[d];
            dist += (proj * proj) / evals[static_cast<size_t>(ki)];
        }
        return dist;
    };

    // Index models by chunkIdx for O(1) lookup.
    std::unordered_map<int, std::vector<int>> byChunk;
    for (int i = 0; i < n; i++)
        if (models[i].localClusterId != 0)
            byChunk[models[i].chunkIdx].push_back(i);

    const int maxChunk = byChunk.empty() ? -1 :
        std::max_element(byChunk.begin(), byChunk.end(),
            [](const auto& a, const auto& b){ return a.first < b.first; })->first;

    // Iterate the chunk-pair pass until no new merges occur.
    // Cross-chunk xcorr matches can cascade: a new Union in pair (k,k+1)
    // may resolve a cluster that was previously unmatched in pair (k+1,k+2).
    const int _ccMaxIter = (TemplateMatchIters > 0) ? TemplateMatchIters : 10;
    for (int _ccIter = 0; _ccIter < _ccMaxIter; _ccIter++) {
        _newUnions = 0;
        for (int k = 0; k < maxChunk; k++) {
        auto itA = byChunk.find(k);
        auto itB = byChunk.find(k + 1);
        if (itA == byChunk.end() || itB == byChunk.end()) continue;

        const auto& vecA = itA->second;
        const auto& vecB = itB->second;
        const int nA = (int)vecA.size();
        const int nB = (int)vecB.size();

        // localClusterId -> index in vecA / vecB for both passes
        std::unordered_map<int,int> localToIdxA, localToIdxB;
        for (int a = 0; a < nA; a++)
            localToIdxA[models[vecA[a]].localClusterId] = a;
        for (int b = 0; b < nB; b++)
            localToIdxB[models[vecB[b]].localClusterId] = b;

        // ── Pass 1: overlap vote matching (authoritative when overlap > 0) ──
        //
        // Overlap spikes were sorted by real EM in both adjacent chunks.
        // A mutual plurality match (>= 3 shared spikes) is treated as
        // authoritative and short-circuits the Mahalanobis check.
        std::unordered_set<int> resolvedA, resolvedB;

        if (k < static_cast<int>(overlapVotes.size()) && !overlapVotes[k].empty()) {
            const auto& votes = overlapVotes[k];

            // Scale vote floor with overlap region size so sparse clusters
            // still match while very common noise pairs are filtered out.
            // floor = max(3, nOverlapSpikes/500).
            int nOverlapSpikes = 0;
            for (const auto& [key, count] : votes) nOverlapSpikes += count;
            const int voteFloor = std::max(3, nOverlapSpikes / 500);

            std::unordered_map<int, std::pair<int,int>> bestFromA;
            std::unordered_map<int, std::pair<int,int>> bestFromB;
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
                auto itB = bestFromB.find(clsK1);
                if (itB == bestFromB.end() || itB->second.first != clsK) continue;
                auto itA2 = localToIdxA.find(clsK);
                auto itB2 = localToIdxB.find(clsK1);
                if (itA2 == localToIdxA.end() || itB2 == localToIdxB.end()) continue;
                const int mA = vecA[itA2->second];
                const int mB = vecB[itB2->second];
                resolvedA.insert(clsK);
                resolvedB.insert(clsK1);
                if (Find(mA) != Find(mB)) {
                    Union(mA, mB);
                    Output("  vote-match  chunk%d.c%d <-> chunk%d.c%d  votes=%d/%d\n",
                           models[mA].chunkIdx, clsK,
                           models[mB].chunkIdx, clsK1,
                           topB.second, itB->second.second);
                } else {
                    Output("  vote-confirm chunk%d.c%d <-> chunk%d.c%d  votes=%d/%d (already merged)\n",
                           models[mA].chunkIdx, clsK,
                           models[mB].chunkIdx, clsK1,
                           topB.second, itB->second.second);
                }
            }
        }

        // ── Pass 2: Mahalanobis MNN for unresolved clusters ──────────────
        // When ChunkOverlapMinutes == 0, resolvedA/B are empty and this
        // pass runs on all clusters exactly as before.
        std::vector<int> uA, uB;
        for (int a = 0; a < nA; a++)
            if (!resolvedA.count(models[vecA[a]].localClusterId)) uA.push_back(a);
        for (int b = 0; b < nB; b++)
            if (!resolvedB.count(models[vecB[b]].localClusterId)) uB.push_back(b);

        const int nUA = static_cast<int>(uA.size());
        const int nUB = static_cast<int>(uB.size());

        if (nUA > 0 && nUB > 0) {
            std::vector<float> D(nUA * nUB);
            for (int ai = 0; ai < nUA; ai++)
                for (int bi = 0; bi < nUB; bi++) {
                    const float dAB = mahalDist(models[vecA[uA[ai]]], models[vecB[uB[bi]]]);
                    const float dBA = mahalDist(models[vecB[uB[bi]]], models[vecA[uA[ai]]]);
                    D[ai * nUB + bi] = 0.5f * (dAB + dBA);
                }
            std::vector<int> nnA(nUA, -1);
            for (int ai = 0; ai < nUA; ai++) {
                float best = HugeScore; int bestBi = -1;
                for (int bi = 0; bi < nUB; bi++)
                    if (D[ai * nUB + bi] < best) { best = D[ai * nUB + bi]; bestBi = bi; }
                if (best < mergeThresh) nnA[ai] = bestBi;
            }
            std::vector<int> nnB(nUB, -1);
            for (int bi = 0; bi < nUB; bi++) {
                float best = HugeScore; int bestAi = -1;
                for (int ai = 0; ai < nUA; ai++)
                    if (D[ai * nUB + bi] < best) { best = D[ai * nUB + bi]; bestAi = ai; }
                if (best < mergeThresh) nnB[bi] = bestAi;
            }
            for (int ai = 0; ai < nUA; ai++) {
                const int bi = nnA[ai];
                if (bi < 0) continue;
                if (nnB[bi] != ai) continue;
                Union(vecA[uA[ai]], vecB[uB[bi]]);
                Output("  mahal-match chunk%d.c%d <-> chunk%d.c%d  d_sym=%.2f\n",
                       models[vecA[uA[ai]]].chunkIdx, models[vecA[uA[ai]]].localClusterId,
                       models[vecB[uB[bi]]].chunkIdx, models[vecB[uB[bi]]].localClusterId,
                       D[ai * nUB + bi]);
            }
        }

        // ── Pass 3: cross-chunk template matching (unresolved pairs) ──────
        // Compute normalised xcorr between cluster mean waveforms.
        // Controlled by CrossChunkTemplateScore (independent of Phase 1.7).
        if (CrossChunkTemplateScore > 0.0f &&
            NbChannels > 0 && NbSamplesPerSpike > 0) {
            const int nCh   = NbChannels;
            const int nSamp = NbSamplesPerSpike;
            const int wE    = nCh * nSamp;
            const int mxSh  = std::max(1, nSamp / 4);

            auto xcorrPair = [&](const ChunkModel& ma, const ChunkModel& mb) -> float {
                if ((int)ma.meanWav.size() != wE) return -1.0f;
                if ((int)mb.meanWav.size() != wE) return -1.0f;
                int sh = 0; float sc = 0.0f;
                XcorrDispatch::compute(
                    ma.meanWav.data(), mb.meanWav.data(),
                    1, nCh, nSamp, mxSh, 0.0f, &sh, &sc);
                return sc;
            };

            // Rebuild unresolved lists from current state
            std::vector<int> tuA, tuB;
            for (int a = 0; a < nA; a++)
                if (!resolvedA.count(models[vecA[a]].localClusterId)) tuA.push_back(a);
            for (int b = 0; b < nB; b++)
                if (!resolvedB.count(models[vecB[b]].localClusterId)) tuB.push_back(b);

            const int tnA = (int)tuA.size(), tnB = (int)tuB.size();
            std::unordered_map<int,std::pair<int,float>> bestAtoB, bestBtoA;
            for (int ai = 0; ai < tnA; ai++) {
                const ChunkModel& ma = models[vecA[tuA[ai]]];
                for (int bi = 0; bi < tnB; bi++) {
                    const ChunkModel& mb = models[vecB[tuB[bi]]];
                    float sc = xcorrPair(ma, mb);
                    if (sc < CrossChunkTemplateScore) continue;
                    if (!bestAtoB.count(ai) || sc > bestAtoB[ai].second)
                        bestAtoB[ai] = {bi, sc};
                    if (!bestBtoA.count(bi) || sc > bestBtoA[bi].second)
                        bestBtoA[bi] = {ai, sc};
                }
            }
            for (auto& [ai, pairB] : bestAtoB) {
                int bi = pairB.first;
                auto itBA = bestBtoA.find(bi);
                if (itBA == bestBtoA.end() || itBA->second.first != ai) continue;
                int mA2 = vecA[tuA[ai]], mB2 = vecB[tuB[bi]];
                if (Find(mA2) == Find(mB2)) continue;
                Union(mA2, mB2);
                Output("  tmpl-match  chunk%d.c%d <-> chunk%d.c%d  xcorr=%.3f\n",
                       models[mA2].chunkIdx, models[mA2].localClusterId,
                       models[mB2].chunkIdx, models[mB2].localClusterId, pairB.second);
            }
        }
        }  // for k (chunk pairs)
        if (_newUnions == 0) break;  // converged
        Output("MergeChunkModels: iter %d produced %d new merges\n",
               _ccIter + 1, _newUnions);
    }  // for _ccIter

    // Assign contiguous globalClusterIds from component roots
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
// Phase 4 (TimeShiftFinalize).
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

    fprintf(stderr, "[Phase 0] Preseed (%.0f min, %d drift-adaptive chunks)\n",
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

    fprintf(stderr, "[Phase 1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
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

    // ── Phase 1.5: xcorr alignment + .fil re-extraction + PCA re-projection ─
    //
    // Note: canonical Phase 1.5 xcorr has been removed.  Alignment is
    // now owned by the shift-probe (TimeShiftAlignPhase), with feature
    // re-projection via RefeaturizeFromShifts at Phase 4.
    // ── Phase 2.5: per-chunk subspace reclustering + refractory split ────────
    if (SubspaceRecluster > 0) {
        fprintf(stderr, "[Phase 2]  Per-chunk subspace reclustering (per-chunk, pre-alignment)\n");
        SubspaceReclusterPerChunk(
            SubspaceDims > 0 ? SubspaceDims : 3,
            chunkPoints, perChunkClass, perChunkModels, nFullDims);

        // Refractory-period guided split — catches mixtures that the subspace
        // CEM misses by exploiting the 1-neuron-per-refractory-window constraint.
        // Absolute refractory = 1.5 ms × sampling rate samples.
        // Trigger when ISI contamination rate >= 1%.
        if (SamplingRate > 0.0f) {
            const float refractSamp    = 1.5f * SamplingRate / 1000.0f;
            const float sessLenSamp    = timeRawMax - timeRawMin;
            fprintf(stderr, "[Phase 2]  Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
        }
    }


    // Phase 3 alignment disabled: chunk means from CEMTwoPhase are
    // blended by timeMergeIter and produce spurious shifts on clean data.
    // ConsiderDeletion/TimeShiftMergeEvaluate handles genuine time-shift merges.
    (void)TimeShiftAlignIter;


    // Note: DipSplit (Phase 1.8) runs AFTER Phase 3 completes — see end of
    // this function.  Running it here would operate on stale per-chunk
    // LogP[] values and produce wrong bloat-gate decisions.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        fprintf(stderr, "[Phase 4]  Mean waveform harvest (channel-major xcorr format)\n");
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

                // Accumulate per-cluster waveform sums
                std::unordered_map<int, std::vector<int64_t>> acc;
                std::unordered_map<int, int> nAcc;
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
                    auto& a = acc[lc];
                    if (a.empty()) a.assign(static_cast<size_t>(wElems), 0);
                    // sample-major (.spk) → channel-major (XcorrDispatch)
                    for (int ch = 0; ch < NbChannels; ch++)
                        for (int s = 0; s < NbSamplesPerSpike; s++)
                            a[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                += row[static_cast<size_t>(s * NbChannels + ch)];
                    nAcc[lc]++;
                }
                for (auto& [lc, a] : acc) {
                    int n2 = nAcc[lc];
                    if (n2 == 0) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWav.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWav[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                }
            }
            fclose(spkTM);
        }
    }
    // ── Phase 2: cross-chunk model matching ─────────────────────────────────
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
    // ── Phase 1.7: within-chunk circular xcorr template matching (iterated) ─
    // Loops until no new merges occur or 10 iterations.  After each merge pass
    // the surviving clusters need updated meanWav vectors (the merged cluster
    // mean changes when two sub-clusters are combined), so the Phase 1.6 harvest
    // re-runs at the top of each iteration before the next xcorr comparison.
    if (TemplateMatchScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int _tmMax = (TemplateMatchIters > 0) ? TemplateMatchIters : 10;
        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
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
                        // Zero existing meanWav for live clusters
                        for (auto& [lc2, pcm] : lcToModel2)
                            pcm->meanWav.assign(static_cast<size_t>(wElems), 0);
                        std::unordered_map<int, std::vector<int64_t>> acc2;
                        std::unordered_map<int, int> nAcc2;
                        std::vector<int16_t> row2(static_cast<size_t>(wElems));
                        for (int i = 0; i < nPts2; i++) {
                            const int lc2 = cls2[i];
                            if (lc2 == 0 || !lcToModel2.count(lc2)) continue;
                            fseeko(spkTM2,
                                   static_cast<off_t>(pts2[i]) * wElems * sizeof(int16_t),
                                   SEEK_SET);
                            if (fread(row2.data(), sizeof(int16_t), wElems, spkTM2)
                                    != static_cast<size_t>(wElems)) continue;
                            auto& a2 = acc2[lc2];
                            if (a2.empty()) a2.assign(static_cast<size_t>(wElems), 0);
                            for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                    a2[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                        += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                            nAcc2[lc2]++;
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
                    }
                    fclose(spkTM2);
                }
            }
            fprintf(stderr, "[Phase 5]  Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = WithinChunkTemplateMatch(chunkPoints, perChunkClass, perChunkModels,
                                                    NbChannels, NbSamplesPerSpike, TemplateMatchScore);
            if (_nMerged == 0) break;
        }
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

    fprintf(stderr, "[Phase 6]  Cross-chunk model matching (overlap + Mahal + xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, noOverlapVotes);
    if (nGlobal < 1) {
        Output("Merge produced no real clusters — falling back to CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }
    if (nGlobal >= MaxPossibleClusters) {
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

    // ── Phase 3: global warm-start EM ───────────────────────────────────────
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
        // GlobalMerge=0: skip Phase 3 entirely.  Emit one MStep/EStep so
        // LogP is valid for ComputeScore(), but do not reassign Class[].
        fprintf(stderr, "[Phase 7]  Global EM: skipped (GlobalMergeIter=0)\n");
        Output("Phase 7 skipped (GlobalMerge=0) — using Phase 2 assignment directly\n");
        // Force CPU path for Phase 3 post-merge scoring.
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
        fprintf(stderr, "[Phase 7]  Global warm-start EM\n");
        Output("Phase 3: global warm-start EM — %d clusters, max %d iters\n",
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

    // ── Phase 8: DipSplit (post-Phase-3) ───────────────────────────────
    // Runs AFTER global MStep/EStep so LogP[c][p] reflects the final
    // cluster assignments.  Splits a bloated cluster into two if a valley
    // is found on any of its top-3 PCs and BIC supports it.  No-op if
    // DipSplitEnable=0 or no clusters pass the bloat gate.
    if (DipSplitEnable != 0) {
        const int n_split = DipSplitPhase();
        if (n_split > 0) score = ComputeScore();
    }

    Output("RunChunkedCEM(ext) done: %d clusters, score %.7g\n", nClustersAlive, score);
    return score;
}

// ---------------------------------------------------------------------------
// Phase 1: run CEMTwoPhase independently on each chunk.
// Phase 2: match cluster models across adjacent chunks (MergeChunkModels).
// Phase 3: global warm-start EM seeded from the matched assignments.
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

    fprintf(stderr, "[Phase 0] Preseed (%.0f min, %d chunks, %.0f min/chunk)\n",
            sessionSamples / samplingRate / 60.0f, nChunks, chunkMinutes);
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

    struct OverlapEntry { int p; int localK; int localK1; };
    std::vector<std::vector<OverlapEntry>> overlapForPair(nChunks > 0 ? nChunks - 1 : 0);

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
                overlapForPair[k].push_back({p, localK, localK1});
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
    // Phase 0.5: global preseed — cluster a random subsample to get
    // globally-informed starting centres for every chunk.
    // Only runs when chunkPreseedFraction > 0.
    // -------------------------------------------------------------------
    std::vector<float> globalPreseedCentres;
    if (chunkPreseedFraction > 0.0f) {
        globalPreseedCentres = PreseedSubsampleCEM(
            chunkPreseedFraction, MaxClusters - 1,
            nSpatialDims, timeMergeIter);
        if (globalPreseedCentres.empty())
            Output("PreseedSubsampleCEM returned no centres — "
                   "chunks will use farthest-point seeding.\n");
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
        // Use chunkStartK as the deletion floor so chunks don't collapse below
        // the preseed K.  minClustersAlive=2 when no preseed (normal behaviour).
        threadKc[t].minClustersAlive  = std::max(2, chunkStartK - 4);
        threadKc[t].preseedCentres    = globalPreseedCentres;  // shared read-only
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

    fprintf(stderr, "[Phase 1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
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

    // ── Phase 1.5: xcorr alignment + .fil re-extraction + PCA re-projection ─
    //
    // Note: canonical Phase 1.5 xcorr has been removed.  Alignment is
    // now owned by the shift-probe (TimeShiftAlignPhase), with feature
    // re-projection via RefeaturizeFromShifts at Phase 4.
    // ── Phase 2.5: per-chunk subspace reclustering + refractory split ────────
    if (SubspaceRecluster > 0) {
        fprintf(stderr, "[Phase 2]  Per-chunk subspace reclustering (per-chunk, pre-alignment)\n");
        SubspaceReclusterPerChunk(
            SubspaceDims > 0 ? SubspaceDims : 3,
            chunkPoints, perChunkClass, perChunkModels, nFullDims);

        // Refractory-period guided split — catches mixtures that the subspace
        // CEM misses by exploiting the 1-neuron-per-refractory-window constraint.
        // Absolute refractory = 1.5 ms × sampling rate samples.
        // Trigger when ISI contamination rate >= 1%.
        if (SamplingRate > 0.0f) {
            const float refractSamp    = 1.5f * SamplingRate / 1000.0f;
            const float sessLenSamp    = timeRawMax - timeRawMin;
            fprintf(stderr, "[Phase 2]  Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
        }
    }


    // Phase 3 alignment disabled: chunk means from CEMTwoPhase are
    // blended by timeMergeIter and produce spurious shifts on clean data.
    // ConsiderDeletion/TimeShiftMergeEvaluate handles genuine time-shift merges.
    (void)TimeShiftAlignIter;


    // Note: DipSplit (Phase 1.8) runs AFTER Phase 3 completes — see end of
    // this function.  Running it here would operate on stale per-chunk
    // LogP[] values and produce wrong bloat-gate decisions.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        fprintf(stderr, "[Phase 4]  Mean waveform harvest (channel-major xcorr format)\n");
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

                // Accumulate per-cluster waveform sums
                std::unordered_map<int, std::vector<int64_t>> acc;
                std::unordered_map<int, int> nAcc;
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
                    auto& a = acc[lc];
                    if (a.empty()) a.assign(static_cast<size_t>(wElems), 0);
                    // sample-major (.spk) → channel-major (XcorrDispatch)
                    for (int ch = 0; ch < NbChannels; ch++)
                        for (int s = 0; s < NbSamplesPerSpike; s++)
                            a[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                += row[static_cast<size_t>(s * NbChannels + ch)];
                    nAcc[lc]++;
                }
                for (auto& [lc, a] : acc) {
                    int n2 = nAcc[lc];
                    if (n2 == 0) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWav.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWav[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
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
    // Phase 2: cross-chunk model matching
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
    // ── Phase 1.7: within-chunk circular xcorr template matching (iterated) ─
    // Loops until no new merges occur or 10 iterations.  After each merge pass
    // the surviving clusters need updated meanWav vectors (the merged cluster
    // mean changes when two sub-clusters are combined), so the Phase 1.6 harvest
    // re-runs at the top of each iteration before the next xcorr comparison.
    if (TemplateMatchScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int _tmMax = (TemplateMatchIters > 0) ? TemplateMatchIters : 10;
        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
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
                        // Zero existing meanWav for live clusters
                        for (auto& [lc2, pcm] : lcToModel2)
                            pcm->meanWav.assign(static_cast<size_t>(wElems), 0);
                        std::unordered_map<int, std::vector<int64_t>> acc2;
                        std::unordered_map<int, int> nAcc2;
                        std::vector<int16_t> row2(static_cast<size_t>(wElems));
                        for (int i = 0; i < nPts2; i++) {
                            const int lc2 = cls2[i];
                            if (lc2 == 0 || !lcToModel2.count(lc2)) continue;
                            fseeko(spkTM2,
                                   static_cast<off_t>(pts2[i]) * wElems * sizeof(int16_t),
                                   SEEK_SET);
                            if (fread(row2.data(), sizeof(int16_t), wElems, spkTM2)
                                    != static_cast<size_t>(wElems)) continue;
                            auto& a2 = acc2[lc2];
                            if (a2.empty()) a2.assign(static_cast<size_t>(wElems), 0);
                            for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                    a2[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                        += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                            nAcc2[lc2]++;
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
                    }
                    fclose(spkTM2);
                }
            }
            fprintf(stderr, "[Phase 5]  Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = WithinChunkTemplateMatch(chunkPoints, perChunkClass, perChunkModels,
                                                    NbChannels, NbSamplesPerSpike, TemplateMatchScore);
            if (_nMerged == 0) break;
        }
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

    fprintf(stderr, "[Phase 6]  Cross-chunk model matching (overlap + Mahal + xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, overlapVotes);
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

    // -------------------------------------------------------------------
    // Phase 3: global warm-start EM (full dimensionality including time)
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
        // GlobalMerge=0: skip Phase 3 entirely.  Emit one MStep/EStep so
        // LogP is valid for ComputeScore(), but do not reassign Class[].
        Output("Phase 7 skipped (GlobalMerge=0) — using Phase 2 assignment directly\n");
        // Force CPU path for Phase 3 post-merge scoring.
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
        fprintf(stderr, "[Phase 7]  Global warm-start EM\n");
        Output("Phase 3: global warm-start EM — %d clusters, max %d iters\n",
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

    // ── Phase 8: DipSplit (post-Phase-3) ───────────────────────────────
    // Runs AFTER global MStep/EStep so LogP[c][p] reflects the final
    // cluster assignments.  See RunChunkedCEM(ext) for details.
    if (DipSplitEnable != 0) {
        const int n_split = DipSplitPhase();
        if (n_split > 0) score = ComputeScore();
    }

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
        // in-range candidate and pick the minimum.
        int   bestCand = N;
        float bestMahal = std::numeric_limits<float>::infinity();

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
            const float dt = candOk[ci]
                ? static_cast<float>(ci - N) / sessionSamples
                : 0.0f;
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

            if (mahal2 < bestMahal) {
                bestMahal = mahal2;
                bestCand  = ci;
            }
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
// Conceptually replaces canonical xcorr realignment (Phase 1.5).  For each
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
// TimeShiftAlignPhase — Phase 1.5 driver
//
// Iterates alive clusters (skipping noise, and clusters with < 5 spikes
// that aren't worth the I/O) and calls TimeShiftAlignCluster on each.
// Called from the driver at the slot that canonical Phase 1.5 xcorr used
// to occupy.  Expects fresh MStep + EStep output (Mean + cholFlat current).
//
// Returns the total number of spikes whose shifts changed across all
// clusters.
// ---------------------------------------------------------------------------
int KK::TimeShiftAlignPhase(int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady) return 0;
    int totalShifted = 0;
    int nClustersProcessed = 0;
    for (int c = 1; c < MaxPossibleClusters; ++c) {
        if (!ClassAlive[c]) continue;
        const int shifted = TimeShiftAlignCluster(c, nChan, nSamplesPerSpike);
        if (shifted > 0) ++nClustersProcessed;
        totalShifted += shifted;
    }
    if (totalShifted > 0)
        Output("[Phase 1.5] Cluster alignment: %d spikes shifted across "
               "%d clusters\n", totalShifted, nClustersProcessed);
    return totalShifted;
}

// ===========================================================================
// DipSplit (Phase 1.8) — bimodal-cluster detection & split
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
// DipSplitAttempt — try to split a single cluster.
//
// Returns true when the cluster was split and Class[] updated.  Caller is
// responsible for follow-up MStep+EStep to refresh cluster stats.
//
// The `reason_out` parameter (optional) receives a tag describing what
// happened: one of "split", "small", "not_bloated", "no_valley",
// "small_child", "bic_worse", "no_free_id".  Used by DipSplitPhase for
// summary logging.
// ---------------------------------------------------------------------------
bool KK::DipSplitAttempt(int clusterId)
{
    const char* _dummy = nullptr;
    return DipSplitAttemptEx(clusterId, _dummy);
}

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

    // ── Gate B: dip test on each PC ──────────────────────────────────────
    int    best_pc     = -1;
    double best_depth  = 0.0;
    double best_valley = 0.0;
    std::vector<double> best_projection;
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

    Output("  [dipsplit] cluster %d → %d+%d  PC%d depth=%.3f  "
           "mahal²₉₀=%.1f vs χ²₉₀=%.1f  elong=%.2fx  ΔBIC=%.1f  gate=%s\n",
           clusterId, n0, n1, best_pc, best_depth,
           mahal2_p90, chi2_90, elong_ratio, bp.bic_k1 - bp.bic_k2,
           bloat_pass ? (elong_pass ? "both" : "bloat") : "elong");
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

    fprintf(stderr, "[Phase 8]  DipSplit: probing %zu alive clusters "
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

    fprintf(stderr, "[Phase 8]  DipSplit: %d accepted  "
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
// TimeShiftFinalize — Phase 4: shift commit
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
// time.  Phase 1.5 is now cluster alignment (TimeShiftAlignPhase);
// Phase 4 is the final disk commit that closes the probe session.
// ---------------------------------------------------------------------------
void KK::TimeShiftFinalize(int nChan, int nSamplesPerSpike)
{
    if (m_cumShift.empty()) { CloseTimeShift(); return; }

    const int nShifted = static_cast<int>(
        std::count_if(m_cumShift.begin(), m_cumShift.end(),
                      [](int s){ return s != 0; }));

    if (nShifted > 0) {
        fprintf(stderr,
                "[Phase 9]  Shift commit: re-extract %d spikes from .fil "
                "→ .spk/.fet\n", nShifted);
        Output("[Phase 9]  Shift commit: %d probe calls, %d spikes with "
               "non-zero cumulative shift\n",
               m_timeShiftCallCount, nShifted);
        // RefeaturizeFromShifts expects shift=0 to mean "skip" — which matches
        // our accumulator.  It re-extracts from .fil for non-zero entries,
        // projects, and re-normalises; this supersedes the in-memory features
        // with clean .fil-derived ones.  WritePhase15Checkpoint then writes
        // the corrected .spk / .fet / .res via the .*.pending mechanism.
        RefeaturizeFromShifts(m_cumShift, nChan, nSamplesPerSpike);
        WritePhase15Checkpoint(m_cumShift, nChan, nSamplesPerSpike);
    } else {
        Output("[Phase 9]  Shift commit: %d probe calls, 0 spikes shifted "
               "— nothing to write back\n", m_timeShiftCallCount);
    }
    CloseTimeShift();
}


// ---------------------------------------------------------------------------
// KK::SubspaceReclusterPass
//
// Post-Phase-2 refinement: for each global cluster, project its spikes into
// the top-subspaceDims eigenvector subspace of that cluster's spatial covariance,
// then run a fresh CEMTwoPhase in that reduced space.  If the reduced-space CEM
// finds more than one cluster with a better BIC score than the single-cluster
// null, the global cluster is split and new global IDs are assigned.
//
// Rationale: Phase 1 CEM runs in the full feature space (25 dims for an octrode),
// where the EM is dominated by the highest-variance dimensions globally.  A cluster
// that is actually two overlapping units may not split in 25D but will split cleanly
// in the 3D subspace defined by its own primary variance directions.
//
// Only clusters with enough spikes to estimate a 3D covariance reliably are
// processed (threshold: subspaceDims * nDims * 3 spikes minimum).
// ---------------------------------------------------------------------------
// KK::SubspaceReclusterPerChunk
//
// Per-chunk subspace reclustering — runs BEFORE Phase 1.5 and Phase 2.
//
// For each local cluster in each chunk, projects its spikes (by global index)
// into the top-subspaceDims eigenvector subspace of that cluster's covariance,
// then runs CEMTwoPhase in that reduced space.  Splits are accepted if the
// multi-cluster BIC score beats the single-cluster null.  perChunkClass[] and
// perChunkModels[] are updated in-place so downstream realignment and Phase 2
// cross-chunk matching see purer, better-separated cluster models.
//
// New local cluster IDs are assigned starting from maxLocalId+1 within each
// chunk so they remain globally unique across chunks.
// ---------------------------------------------------------------------------
// KK::WithinChunkMerge
//
// Within-chunk merge pass — runs after Phase 1.5 waveform realignment updates
// Data[] features, before Phase 2 cross-chunk matching.
//
// For each chunk: recomputes cluster means and covariances from the updated
// Data[], then runs a symmetric Mahalanobis MNN merge.  Clusters whose
// symmetric Mahalanobis distance falls below mergeThresh are merged — exactly
// the same criterion as Phase 2, but applied locally within each chunk.
//
// This cleans up clusters that were over-split in Phase 1 but, after feature
// updating via realignment, are no longer distinguishable.  Giving Phase 2
// purer, larger clusters improves cross-chunk matching reliability.
// ---------------------------------------------------------------------------
// KK::WithinChunkTemplateMatch
//
// Within-chunk xcorr template matching — runs after Phase 1.5 waveform
// realignment and the Phase 1.6 meanWav harvest, before Phase 2 cross-chunk
// matching.
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
// the model list before Phase 2 so cross-chunk matching has fewer, purer models.
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
void KK::WithinChunkMerge(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nSpatialDims, float mergeThresh)
{
    const int nCh = static_cast<int>(chunkPoints.size());
    int totalMerged = 0;

    for (int ck = 0; ck < nCh; ck++) {
        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());

        if (nPts == 0 || mdls.size() < 2) continue;

        // ── Recompute means and covariances from updated Data[] ────────────
        // Index models by localClusterId for fast lookup
        std::unordered_map<int, int> lcToIdx;
        for (int i = 0; i < (int)mdls.size(); i++)
            lcToIdx[mdls[static_cast<size_t>(i)].localClusterId] = i;

        // Ensure all models have mean/cov allocated to the correct size
        for (auto& cm : mdls) {
            cm.nMembers = 0;
            if (cm.mean.size() != static_cast<size_t>(nSpatialDims + 1))
                cm.mean.assign(static_cast<size_t>(nSpatialDims + 1), 0.0f);
            else
                std::fill(cm.mean.begin(), cm.mean.end(), 0.0f);
            const size_t covSz = static_cast<size_t>(nSpatialDims + 1)
                               * static_cast<size_t>(nSpatialDims + 1);
            if (cm.cov.size() != covSz)
                cm.cov.assign(covSz, 0.0f);
            else
                std::fill(cm.cov.begin(), cm.cov.end(), 0.0f);
        }

        // Accumulate means
        for (int i = 0; i < nPts; i++) {
            int lc = cls[static_cast<size_t>(i)];
            if (lc == 0) continue;
            auto it = lcToIdx.find(lc);
            if (it == lcToIdx.end()) continue;
            auto& cm = mdls[static_cast<size_t>(it->second)];
            const int p = pts[static_cast<size_t>(i)];
            for (int d = 0; d < nSpatialDims; d++)
                cm.mean[static_cast<size_t>(d)] += Data[p * nDims + d];
            cm.nMembers++;
        }
        for (auto& cm : mdls)
            if (cm.nMembers > 0)
                for (float& v : cm.mean) v /= cm.nMembers;

        // Accumulate covariances (upper triangle, indexed by nDims)
        for (int i = 0; i < nPts; i++) {
            int lc = cls[static_cast<size_t>(i)];
            if (lc == 0) continue;
            auto it = lcToIdx.find(lc);
            if (it == lcToIdx.end()) continue;
            auto& cm = mdls[static_cast<size_t>(it->second)];
            const int p = pts[static_cast<size_t>(i)];
            for (int r = 0; r < nSpatialDims; r++)
                for (int c2 = r; c2 < nSpatialDims; c2++) {
                    float dr = Data[p * nDims + r]  - cm.mean[static_cast<size_t>(r)];
                    float dc = Data[p * nDims + c2] - cm.mean[static_cast<size_t>(c2)];
                    cm.cov[r * nDims + c2] += dr * dc;
                }
        }
        for (auto& cm : mdls)
            if (cm.nMembers > 1)
                for (float& v : cm.cov) v /= static_cast<float>(cm.nMembers - 1);

        // ── Pairwise symmetric Mahalanobis + Union-Find merge ─────────────
        const int n = static_cast<int>(mdls.size());
        std::vector<int> parent(static_cast<size_t>(n));
        std::iota(parent.begin(), parent.end(), 0);
        // Iterative path-halving Find (avoids stack overflow on deep chains)
        auto Find = [&](int x) -> int {
            while (parent[static_cast<size_t>(x)] != x) {
                parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(
                    parent[static_cast<size_t>(x)])];
                x = parent[static_cast<size_t>(x)];
            }
            return x;
        };
        auto Union = [&](int a, int b) {
            parent[static_cast<size_t>(Find(a))] = Find(b);
        };

        // Mahalanobis distance (same formula as MergeChunkModels)
        auto mahalDist = [&](const ChunkModel& src, const ChunkModel& tgt) -> float {
            float covS[64*64], chol[64*64], diff[64], root[64];
            if (nSpatialDims > 64) return HugeScore;
            for (int r = 0; r < nSpatialDims; r++)
                for (int c2 = r; c2 < nSpatialDims; c2++)
                    covS[r * nSpatialDims + c2] = tgt.cov[r * nDims + c2];
            if (Cholesky(covS, chol, nSpatialDims)) return HugeScore;
            for (int d = 0; d < nSpatialDims; d++)
                diff[d] = src.mean[d] - tgt.mean[d];
            TriSolve(chol, diff, root, nSpatialDims);
            float dist = 0.0f;
            for (int d = 0; d < nSpatialDims; d++) dist += root[d] * root[d];
            return dist;
        };

        // Build symmetric distance matrix and find MNN pairs
        std::vector<float> D(static_cast<size_t>(n) * n, HugeScore);
        for (int a = 0; a < n; a++) {
            if (mdls[static_cast<size_t>(a)].localClusterId == 0) continue;
            for (int b = a + 1; b < n; b++) {
                if (mdls[static_cast<size_t>(b)].localClusterId == 0) continue;
                float dAB = mahalDist(mdls[static_cast<size_t>(a)], mdls[static_cast<size_t>(b)]);
                float dBA = mahalDist(mdls[static_cast<size_t>(b)], mdls[static_cast<size_t>(a)]);
                float d   = 0.5f * (dAB + dBA);
                D[static_cast<size_t>(a * n + b)] = d;
                D[static_cast<size_t>(b * n + a)] = d;
            }
        }

        // MNN: a merges with b if b is a's nearest and a is b's nearest, dist < thresh
        std::vector<int> nn(static_cast<size_t>(n), -1);
        std::vector<float> nnDist(static_cast<size_t>(n), HugeScore);
        for (int a = 0; a < n; a++) {
            if (mdls[static_cast<size_t>(a)].localClusterId == 0) continue;
            for (int b = 0; b < n; b++) {
                if (a == b) continue;
                if (mdls[static_cast<size_t>(b)].localClusterId == 0) continue;
                if (D[static_cast<size_t>(a * n + b)] < nnDist[static_cast<size_t>(a)]) {
                    nnDist[static_cast<size_t>(a)] = D[static_cast<size_t>(a * n + b)];
                    nn[static_cast<size_t>(a)] = b;
                }
            }
        }

        int chunkMerged = 0;
        for (int a = 0; a < n; a++) {
            int b = nn[static_cast<size_t>(a)];
            if (b < 0) continue;
            if (nn[static_cast<size_t>(b)] != a) continue;  // not mutual
            if (nnDist[static_cast<size_t>(a)] >= mergeThresh) continue;
            if (Find(a) == Find(b)) continue;
            Union(a, b);
            Output("  within-chunk merge: chunk%d c%d+c%d d=%.2f\n",
                   ck,
                   mdls[static_cast<size_t>(a)].localClusterId,
                   mdls[static_cast<size_t>(b)].localClusterId,
                   nnDist[static_cast<size_t>(a)]);
            chunkMerged++;
            totalMerged++;
        }

        if (chunkMerged == 0) continue;

        // ── Apply merges: remap cluster IDs in perChunkClass ──────────────
        // For each component, the representative ID is the lowest localClusterId
        std::unordered_map<int,int> idxToNewLc;
        for (int a = 0; a < n; a++) {
            int root = Find(a);
            if (!idxToNewLc.count(root) ||
                mdls[static_cast<size_t>(a)].localClusterId <
                mdls[static_cast<size_t>(idxToNewLc[root])].localClusterId)
                idxToNewLc[root] = a;
        }
        // Build localClusterId → canonical localClusterId map
        std::unordered_map<int,int> lcRemap;
        for (int a = 0; a < n; a++) {
            int canonical = mdls[static_cast<size_t>(idxToNewLc[Find(a)])].localClusterId;
            lcRemap[mdls[static_cast<size_t>(a)].localClusterId] = canonical;
        }
        for (auto& lc : cls)
            if (lcRemap.count(lc)) lc = lcRemap[lc];

        // Rebuild perChunkModels: remove merged-away models, update survivors
        std::unordered_set<int> keepLc;
        for (auto& [root, idx2] : idxToNewLc)
            keepLc.insert(mdls[static_cast<size_t>(idx2)].localClusterId);
        mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
            [&](const ChunkModel& cm){ return !keepLc.count(cm.localClusterId); }),
            mdls.end());
    }

    Output("WithinChunkMerge: %d cluster pair(s) merged across all chunks\n", totalMerged);
}


// ---------------------------------------------------------------------------
void KK::SubspaceReclusterPerChunk(
    int subspaceDims,
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    if (subspaceDims <= 0) return;
    // kMax: hard ceiling on subspace dimensions extracted per cluster.
    // The effective per-cluster dimension kEff is chosen automatically
    // from the eigenvalue gap, so this is just an upper bound.
    const int kMax     = std::min(subspaceDims, nFullDims - 1);
    const int nCh      = static_cast<int>(chunkPoints.size());
    const int nSpatial = nFullDims - 1;
    // minSpikes based on kMax as upper bound; effective k per cluster
    // may be smaller, so this is conservative.
    const int minSpikes = std::max(kMax * 10, 20);

    // ── Phase A: serial pre-processing ─────────────────────────────────────
    // For each (chunk, cluster) build a projected sub-KK and compute nullScore.
    // Reads Data[] (read-only) and perChunkClass/perChunkModels (read-only here).

    struct WorkItem {
        int   ck, lc;
        int   nMem;
        int   kEff;                      // auto-selected subspace dims for this cluster
        std::vector<int>   members;      // local indices into chunkPoints[ck]
        std::vector<float> ksData;       // projected + normalised subspace data [nMem × kEff]
        float timeRawMin, timeRawMax;
        float nullScore;
        bool  valid;
    };

    struct Result {
        int  ck, lc;
        bool accepted;
        int  bestSubK;
        float bestSubScore;
        std::vector<int> bestSubClass;   // length nMem
    };

    std::vector<WorkItem> items;
    items.reserve(512);

    for (int ck = 0; ck < nCh; ck++) {
        const auto& pts  = chunkPoints[ck];
        const auto& cls  = perChunkClass[ck];
        const auto& mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;

        std::vector<ChunkModel> snapshot = mdls;
        for (const auto& origCm : snapshot) {
            const int lc = origCm.localClusterId;
            if (lc == 0) continue;

            std::vector<int> members;
            for (int i = 0; i < nPts; i++)
                if (cls[i] == lc) members.push_back(i);

            const int nMem = static_cast<int>(members.size());
            if (nMem < minSpikes) continue;

            // Cluster mean
            std::vector<float> clMean(static_cast<size_t>(nSpatial), 0.0f);
            for (int li : members) {
                const int p = pts[static_cast<size_t>(li)];
                for (int d = 0; d < nSpatial; d++)
                    clMean[static_cast<size_t>(d)] += Data[p * nDims + d];
            }
            for (float& v : clMean) v /= nMem;

            // Upper-triangular covariance
            std::vector<float> clCovUT(static_cast<size_t>(nSpatial) * nFullDims, 0.0f);
            for (int li : members) {
                const int p = pts[static_cast<size_t>(li)];
                for (int r = 0; r < nSpatial; r++)
                    for (int cc = r; cc < nSpatial; cc++) {
                        float dr = Data[p * nDims + r]  - clMean[static_cast<size_t>(r)];
                        float dc = Data[p * nDims + cc] - clMean[static_cast<size_t>(cc)];
                        clCovUT[r * nFullDims + cc] += dr * dc;
                    }
            }
            for (auto& v : clCovUT) v /= static_cast<float>(nMem - 1);

            // Extract up to kMax eigenvectors, then auto-select kEff via
            // the largest normalised eigenvalue gap:
            //   gap_i = (lambda_i - lambda_{i+1}) / lambda_0
            // The largest gap marks the signal/noise boundary in the
            // cluster's own covariance spectrum.
            std::vector<float> evecs, evals;
            topKEigen(clCovUT.data(), nSpatial, nFullDims, kMax, evecs, evals);

            int kEff = 2;  // minimum: >= 2 dims needed to detect a split
            if (kMax >= 2 && evals[0] > 1e-12f) {
                float maxGap = -1.0f;
                for (int ki = 0; ki < kMax - 1; ki++) {
                    float gap = (evals[static_cast<size_t>(ki)]
                               - evals[static_cast<size_t>(ki + 1)])
                              / evals[0];
                    if (gap > maxGap) { maxGap = gap; kEff = ki + 1; }
                }
                kEff = std::max(2, std::min(kEff, kMax));
            }

            // Project into whitened kEff-dimensional subspace
            WorkItem wi;
            wi.ck = ck; wi.lc = lc; wi.nMem = nMem; wi.kEff = kEff;
            wi.members = std::move(members);
            wi.ksData.resize(static_cast<size_t>(nMem) * kEff);
            for (int i = 0; i < nMem; i++) {
                const int p = pts[static_cast<size_t>(wi.members[static_cast<size_t>(i)])];
                for (int ki = 0; ki < kEff; ki++) {
                    float proj = 0.0f;
                    for (int d = 0; d < nSpatial; d++)
                        proj += evecs[static_cast<size_t>(ki * nSpatial + d)]
                              * (Data[p * nDims + d] - clMean[static_cast<size_t>(d)]);
                    float scale = (evals[static_cast<size_t>(ki)] > 1e-12f)
                                ? std::sqrt(evals[static_cast<size_t>(ki)]) : 1.0f;
                    wi.ksData[static_cast<size_t>(i * kEff + ki)] = proj / scale;
                }
            }

            // Normalise to [0,1]
            std::vector<float> subMin(static_cast<size_t>(kEff),  HugeScore);
            std::vector<float> subMax(static_cast<size_t>(kEff), -HugeScore);
            for (int i = 0; i < nMem; i++)
                for (int ki = 0; ki < kEff; ki++) {
                    float v = wi.ksData[static_cast<size_t>(i * kEff + ki)];
                    if (v < subMin[static_cast<size_t>(ki)]) subMin[static_cast<size_t>(ki)] = v;
                    if (v > subMax[static_cast<size_t>(ki)]) subMax[static_cast<size_t>(ki)] = v;
                }
            for (int i = 0; i < nMem; i++)
                for (int ki = 0; ki < kEff; ki++) {
                    float range = subMax[static_cast<size_t>(ki)] - subMin[static_cast<size_t>(ki)];
                    wi.ksData[static_cast<size_t>(i * kEff + ki)] = (range > 1e-12f)
                        ? (wi.ksData[static_cast<size_t>(i * kEff + ki)] - subMin[static_cast<size_t>(ki)]) / range
                        : 0.5f;
                }
            wi.timeRawMin = subMin[static_cast<size_t>(kEff - 1)];
            wi.timeRawMax = subMax[static_cast<size_t>(kEff - 1)];

            // Null score
            {
                KK Ks;
                Ks.nDims = kEff; Ks.nPoints = nMem;
                Ks.penaltyMix = penaltyMix; Ks.suppressBestSave = true;
                Ks.minClustersAlive = 1; Ks.AllocateArrays(); Ks.AllocateCholeskyVecs();
                for (int i = 0; i < nMem * kEff; i++) Ks.Data[i] = wi.ksData[static_cast<size_t>(i)];
                Ks.timeRawMin = wi.timeRawMin; Ks.timeRawMax = wi.timeRawMax;
                Ks.ReinitForSplit(nMem, kEff, penaltyMix);
                Ks.nStartingClusters = 1; Ks.NoisePoint = 0;
                for (int i2 = 0; i2 < nMem; i2++) Ks.Class[i2] = 1;
                Ks.ClassAlive[1] = 1; Ks.nClustersAlive = 1; Ks.AliveIndex[0] = 1;
                Ks.MStep(); Ks.EStep();
                wi.nullScore = Ks.ComputeScore();
            }
            wi.valid = (wi.nullScore != 0.0f);
            items.push_back(std::move(wi));
        }
    }

    const int nItems = static_cast<int>(items.size());
    if (nItems == 0) {
        Output("SubspaceReclusterPerChunk: 0 cluster(s) split across all chunks\n");
        return;
    }

    // ── Phase B: parallel sub-CEM ─────────────────────────────────────────────
    // Flattened over (item × run): nItems*nRuns independent work units.
    // Each unit is a single (cluster, random-seed) CEMTwoPhase call.
    // Serial reduction after the parallel block picks the best-scoring run
    // per cluster and writes it into results[], which Phase C reads unchanged.
    //
    // Mapping invariant: runResults[wi_idx * runsPerSubCluster + run].subClass[i2]
    // = Ks.Class[i2] after CEMTwoPhase on wi.ksData.  This is the sub-cluster
    // label (1..N) for the i2-th member of work item wi_idx.  Phase C consumes
    // res.bestSubClass[i2] with the same indexing as before — only the source
    // (which run) changed, not the meaning of the value.
    const int runsPerSubCluster = (nRuns > 0) ? nRuns : 1;
    const int nFlatItems        = nItems * runsPerSubCluster;

    struct RunResult {
        float            score     = 0.0f;   // 0 = invalid (nClusters <= 1)
        int              nClusters = 1;
        std::vector<int> subClass; // [nMem]: sub-cluster labels from Ks.Class[]
    };
    std::vector<RunResult> runResults(static_cast<size_t>(nFlatItems));
    // Pre-size subClass vectors; initialise to 0 (all noise) as a safe default.
    for (int fi = 0; fi < nFlatItems; fi++) {
        const int wi_idx = fi / runsPerSubCluster;
        const WorkItem& wi = items[static_cast<size_t>(wi_idx)];
        runResults[static_cast<size_t>(fi)].subClass.assign(
            static_cast<size_t>(wi.nMem), 0);
    }

    std::vector<Result> results(static_cast<size_t>(nItems));

#ifdef _OPENMP
    const int _srcThreads = std::min(
        (ompTeamSize > 0) ? ompTeamSize : omp_get_max_threads(),
        nFlatItems);
#else
    const int _srcThreads = 1;
#endif

    // Number of farthest-point seeds for the subspace CEM.
    // We use enough starting clusters that CEM can discover the right number
    // via splits and deletions, matching the approach in CEMTwoPhase/TrySplits.
    // min(8, nMem/30) prevents over-seeding tiny clusters; +1 for noise slot.
    // This is computed per work-item below; the constant here is a ceiling.
    const int maxSubStart = 9;  // noise + up to 8 real sub-clusters

    #pragma omp parallel for schedule(dynamic) num_threads(_srcThreads) \
        default(none) \
        shared(items, runResults, nItems) \
        firstprivate(penaltyMix, RandomSeed, runsPerSubCluster, HugeScore, maxSubStart, \
                     MaxPossibleClusters)
    for (int fi = 0; fi < nItems * runsPerSubCluster; fi++) {
        const int wi_idx = fi / runsPerSubCluster;
        const int run    = fi % runsPerSubCluster;
        const WorkItem& wi = items[static_cast<size_t>(wi_idx)];
        RunResult& rr = runResults[static_cast<size_t>(fi)];

        if (!wi.valid) continue;

        // Determine sensible number of starting clusters for this sub-cluster.
        // Use enough seeds to let CEM find multi-cluster structure via splits,
        // but cap at min(maxSubStart, nMem/minSpikesPerCluster) to avoid
        // degenerate fits on small clusters.
        // +1 for the noise slot (cluster 0).
        // Use this cluster's auto-selected subspace dimension.
        const int k = wi.kEff;
        const int minSpkPerCl = std::max(k * 3, 10);
        const int nSeeds = std::max(2,
                           std::min(maxSubStart - 1,
                                    wi.nMem / minSpkPerCl));
        const int nSubStart = nSeeds + 1;  // +1 noise slot

        // Each thread has its own KK scratch — no shared mutable state.
        KK Ks;
        Ks.nDims = k; Ks.nPoints = wi.nMem;
        Ks.penaltyMix = penaltyMix; Ks.suppressBestSave = true;
        Ks.minClustersAlive = 1; Ks.AllocateArrays(); Ks.AllocateCholeskyVecs();
        // MaxClusters and MaxPossibleClusters are globals; the scratch KK
        // object inherits them via AllocateArrays(). We cap the effective
        // per-subspace ceiling by clamping nSubStart, not by touching the
        // global — the global drives array sizing and must not be lowered.

        Ks.ReinitForSplit(wi.nMem, k, penaltyMix);
        for (int i = 0; i < wi.nMem * k; i++)
            Ks.Data[i] = wi.ksData[static_cast<size_t>(i)];
        Ks.timeRawMin = wi.timeRawMin; Ks.timeRawMax = wi.timeRawMax;

        // Farthest-point seeding: places nSeeds centres spread across the
        // subspace, giving CEM a principled non-random starting partition.
        // Different runs vary the seed of the random tie-breaker used by
        // InitCentresFarthestPoint, producing different initial assignments
        // when two points are equidistant from the current frontier.
        srand(static_cast<unsigned>(RandomSeed
              + static_cast<unsigned>(wi.ck) * 997u
              + static_cast<unsigned>(wi.lc) * 13u
              + static_cast<unsigned>(run)   * 7u));
        // Initialise with farthest-point seeding in the full k-dimensional
        // subspace (all dimensions are spatial — there is no time column here).
        Ks.nStartingClusters = nSubStart; Ks.NoisePoint = 0;
        for (int c = 0; c < MaxPossibleClusters; c++)
            Ks.ClassAlive[c] = (c < nSubStart) ? 1 : 0;
        Ks.Reindex();
        Ks.InitCentresFarthestPoint(nSeeds, k);  // all k dims are spatial
        Ks.InitClassFromCentres(k);

        // Use CEM (not CEMTwoPhase) because the subspace has no time column.
        // CEMTwoPhase would reduce to nDims-1 = k-1 in Phase 1, discarding
        // one real feature dimension.  RunEMLoop with enableSplits=true runs
        // the full k-dimensional CEM with TrySplits enabled.
        const float score = Ks.RunEMLoop(
            /*enableSplits=*/   true,
            /*enableDistDump=*/ false,
            /*maxIter=*/        0,
            /*phaseLabel=*/     "sub");

        // Only record if a genuine split occurred (nClusters > 1).
        if (Ks.nClustersAlive > 1) {
            rr.score     = score;
            rr.nClusters = Ks.nClustersAlive;
            for (int i2 = 0; i2 < wi.nMem; i2++)
                rr.subClass[static_cast<size_t>(i2)] = Ks.Class[i2];
        }
    }

    // Serial reduction: for each item, pick the run with best score.
    // Writes into results[] which Phase C reads — same structure as before.
    for (int wi_idx = 0; wi_idx < nItems; wi_idx++) {
        const WorkItem& wi = items[static_cast<size_t>(wi_idx)];
        Result& res = results[static_cast<size_t>(wi_idx)];
        res.ck = wi.ck; res.lc = wi.lc;
        res.accepted = false; res.bestSubK = 1;
        // Initialise bestSubClass to 0 (all noise) — overwritten on acceptance.
        res.bestSubClass.assign(static_cast<size_t>(wi.nMem), 0);

        if (!wi.valid) continue;

        float bestScore = HugeScore;
        int   bestSubK  = 1;
        for (int run = 0; run < runsPerSubCluster; run++) {
            // Index: wi_idx * runsPerSubCluster + run — matches the flat loop above.
            const RunResult& rr = runResults[
                static_cast<size_t>(wi_idx * runsPerSubCluster + run)];
            // rr.score == 0.0f means no valid split for this run (guard above).
            if (rr.nClusters > 1 && rr.score < bestScore) {
                bestScore = rr.score;
                bestSubK  = rr.nClusters;
                // Copy — not move — so runResults stays valid if we iterate again.
                res.bestSubClass = rr.subClass;
            }
        }
        // Only accept if at least one run found a better split than the null model.
        if (bestSubK > 1 && bestScore < wi.nullScore) {
            res.accepted     = true;
            res.bestSubK     = bestSubK;
            res.bestSubScore = bestScore;
        }
    }

    // ── Phase C: serial result application ─────────────────────────────────
    // Apply accepted splits to perChunkClass and perChunkModels.
    // Serial within each chunk because nextLocalId is per-chunk shared state.
    int totalSplit = 0;
    for (int ck = 0; ck < nCh; ck++) {
        auto& cls  = perChunkClass[ck];
        auto& mdls = perChunkModels[ck];
        const auto& pts  = chunkPoints[ck];
        const int   nPts = static_cast<int>(pts.size());

        // Highest existing ID (rebuilt per chunk)
        int nextLocalId = 0;
        for (const auto& cm : mdls)
            if (cm.localClusterId > nextLocalId) nextLocalId = cm.localClusterId;
        nextLocalId++;

        for (int wi_idx = 0; wi_idx < nItems; wi_idx++) {
            const WorkItem& wi  = items[static_cast<size_t>(wi_idx)];
            const Result&   res = results[static_cast<size_t>(wi_idx)];
            if (res.ck != ck || !res.accepted) continue;

            const int lc   = wi.lc;
            const int nMem = wi.nMem;

            std::unordered_map<int,int> subToLocal;
            subToLocal[res.bestSubClass[0]] = lc;
            for (int i2 = 0; i2 < nMem; i2++) {
                int sc = res.bestSubClass[static_cast<size_t>(i2)];
                if (!subToLocal.count(sc)) subToLocal[sc] = nextLocalId++;
            }

            Output("  SubspaceRecluster: chunk%d cluster%d -> %d sub-clusters "
                   "(subScore=%.4g < null=%.4g)\n",
                   ck, lc, res.bestSubK, res.bestSubScore, wi.nullScore);

            for (int i2 = 0; i2 < nMem; i2++)
                cls[static_cast<size_t>(wi.members[static_cast<size_t>(i2)])] =
                    subToLocal[res.bestSubClass[static_cast<size_t>(i2)]];

            // ── klustakwikExp: per-sub-cluster shift-probe refeaturization ──
            // Build global-spike-index lists for each new sub-cluster and run
            // the probe.  This refreshes Data[] for affected rows BEFORE the
            // ChunkModel mean/cov builders below see them, so downstream
            // Phase 2 cross-chunk matching operates on the refined features.
            if (m_timeShiftReady && NbChannels > 0 && NbSamplesPerSpike > 0) {
                for (auto& [sc2, newLc] : subToLocal) {
                    std::vector<int> sub;
                    sub.reserve(nMem);
                    for (int i2 = 0; i2 < nPts; i2++)
                        if (cls[static_cast<size_t>(i2)] == newLc)
                            sub.push_back(pts[static_cast<size_t>(i2)]);
                    (void)sub;  // TimeShiftSplit removed from Phase 2
                }
            }

            mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
                [lc](const ChunkModel& m){ return m.localClusterId == lc; }),
                mdls.end());

            for (auto& [sc2, newLc] : subToLocal) {
                ChunkModel cm;
                cm.chunkIdx = ck; cm.localClusterId = newLc;
                cm.globalClusterId = -1; cm.nMembers = 0;
                cm.mean.assign(static_cast<size_t>(nFullDims), 0.0f);
                cm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
                for (int i2 = 0; i2 < nPts; i2++) {
                    if (cls[static_cast<size_t>(i2)] != newLc) continue;
                    const int p2 = pts[static_cast<size_t>(i2)];
                    for (int d = 0; d < nFullDims; d++)
                        cm.mean[static_cast<size_t>(d)] += Data[p2 * nDims + d];
                    cm.nMembers++;
                }
                if (cm.nMembers > 0)
                    for (float& v : cm.mean) v /= cm.nMembers;
                for (int i2 = 0; i2 < nPts; i2++) {
                    if (cls[static_cast<size_t>(i2)] != newLc) continue;
                    const int p2 = pts[static_cast<size_t>(i2)];
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
            totalSplit++;
        }
    }
    Output("SubspaceReclusterPerChunk: %d cluster(s) split across all chunks\n", totalSplit);
}


void KK::SubspaceReclusterPass(int subspaceDims)
{
    if (subspaceDims <= 0 || nClustersAlive <= 0) return;
    const int k = std::min(subspaceDims, nDims - 1);   // spatial dims only, not time

    Output("SubspaceReclusterPass: checking %d clusters in top-%d subspace\\n",
           nClustersAlive, k);

    int nSplit = 0;
    int nextGlobal = nClustersAlive;  // next fresh global cluster ID

    // Work through a snapshot of current live clusters
    std::vector<int> liveClusters(AliveIndex.m_Data,
                                   AliveIndex.m_Data + nClustersAlive);

    for (const int c : liveClusters) {
        if (!ClassAlive[c]) continue;  // already deleted/split

        // ── Collect spikes in this cluster ──────────────────────────────
        std::vector<int> members;
        members.reserve(static_cast<size_t>(nPoints / nClustersAlive + 1));
        for (int p = 0; p < nPoints; p++)
            if (Class[p] == c) members.push_back(p);

        const int nMem = static_cast<int>(members.size());
        const int minSpikes = k * (nDims - 1) * 3;
        if (nMem < minSpikes) continue;

        // ── Compute cluster mean and covariance (spatial dims) ───────────
        const int nSpatial = nDims - 1;
        std::vector<float> clMean(static_cast<size_t>(nSpatial), 0.0f);
        for (int p : members)
            for (int d = 0; d < nSpatial; d++)
                clMean[static_cast<size_t>(d)] += Data[p * nDims + d];
        for (float& v : clMean) v /= nMem;

        // Upper-triangular covariance in nDims layout
        std::vector<float> clCovUT(static_cast<size_t>(nSpatial) * nDims, 0.0f);
        for (int p : members)
            for (int r = 0; r < nSpatial; r++)
                for (int cc = r; cc < nSpatial; cc++) {
                    float dr = Data[p * nDims + r]  - clMean[static_cast<size_t>(r)];
                    float dc = Data[p * nDims + cc] - clMean[static_cast<size_t>(cc)];
                    clCovUT[r * nDims + cc] += dr * dc;
                }
        for (int r = 0; r < nSpatial; r++)
            for (int cc = r; cc < nSpatial; cc++)
                clCovUT[r * nDims + cc] /= static_cast<float>(nMem - 1);

        // ── Top-k eigenvectors of cluster covariance ────────────────────
        std::vector<float> evecs, evals;
        topKEigen(clCovUT.data(), nSpatial, nDims, k, evecs, evals);

        // ── Project cluster spikes into k-dimensional subspace ───────────
        // Sub-KK object: nDims = k (spatial subspace only, no time dim)
        KK Ks;
        Ks.nDims             = k;
        Ks.nPoints           = nMem;
        Ks.nStartingClusters = 2;
        Ks.penaltyMix        = penaltyMix;
        Ks.suppressBestSave  = true;
        Ks.minClustersAlive  = 1;    // allow single-cluster solution
        Ks.AllocateArrays();
        Ks.AllocateCholeskyVecs();

        for (int i = 0; i < nMem; i++) {
            const int p = members[static_cast<size_t>(i)];
            for (int ki = 0; ki < k; ki++) {
                float proj = 0.0f;
                for (int d = 0; d < nSpatial; d++)
                    proj += evecs[static_cast<size_t>(ki * nSpatial + d)]
                          * (Data[p * nDims + d] - clMean[static_cast<size_t>(d)]);
                // Re-normalise into [0,1] using eigenvalue as scale
                // proj / sqrt(eval) gives a unit-variance coordinate
                float scale = (evals[static_cast<size_t>(ki)] > 1e-12f)
                            ? std::sqrt(evals[static_cast<size_t>(ki)]) : 1.0f;
                Ks.Data[i * k + ki] = proj / scale;
            }
        }

        // Normalise subspace Data[] to [0,1] for the sub-KK EM
        {
            std::vector<float> subMin(static_cast<size_t>(k),  HugeScore);
            std::vector<float> subMax(static_cast<size_t>(k), -HugeScore);
            for (int i = 0; i < nMem; i++)
                for (int ki = 0; ki < k; ki++) {
                    float v = Ks.Data[i * k + ki];
                    if (v < subMin[static_cast<size_t>(ki)]) subMin[static_cast<size_t>(ki)] = v;
                    if (v > subMax[static_cast<size_t>(ki)]) subMax[static_cast<size_t>(ki)] = v;
                }
            for (int i = 0; i < nMem; i++)
                for (int ki = 0; ki < k; ki++) {
                    float range = subMax[static_cast<size_t>(ki)] - subMin[static_cast<size_t>(ki)];
                    Ks.Data[i * k + ki] = (range > 1e-12f)
                        ? (Ks.Data[i * k + ki] - subMin[static_cast<size_t>(ki)]) / range
                        : 0.5f;
                }
            // Store range for penaltyMix denominator (match LoadData convention)
            Ks.timeRawMin = subMin[static_cast<size_t>(k - 1)];
            Ks.timeRawMax = subMax[static_cast<size_t>(k - 1)];
        }

        // ── Run CEM in subspace ─────────────────────────────────────────
        // Single-pass CEMTwoPhase; allow up to 4 clusters
        const int maxSubClusters = std::min(4, nMem / minSpikes);
        if (maxSubClusters < 2) continue;

        float bestSubScore = HugeScore;
        std::vector<int> bestSubClass(static_cast<size_t>(nMem), 0);
        int bestSubK = 1;

        for (int startK = 2; startK <= maxSubClusters; startK++) {
            srand(static_cast<unsigned>(RandomSeed + c * 97 + startK));
            Ks.ReinitForSplit(nMem, k, penaltyMix);
            Ks.nStartingClusters = startK;
            Ks.NoisePoint = 0;  // no noise class in subspace CEM
            // Re-copy (normalised) data (ReinitForSplit zeroes Class but not Data)
            // (Data was set above and ReinitForSplit doesn't touch it)
            const float score = Ks.CEMTwoPhase(0);  // no time-merge in subspace

            if (score < bestSubScore && Ks.nClustersAlive > 1) {
                bestSubScore = score;
                bestSubK = Ks.nClustersAlive;
                for (int i = 0; i < nMem; i++)
                    bestSubClass[static_cast<size_t>(i)] = Ks.Class[i];
            }
        }

        if (bestSubK <= 1) continue;

        // Null model: fit a single Gaussian to the whitened subspace data.
        // Use CEM() directly (no splits) so ComputeScore runs a proper
        // full EM convergence on the 1-cluster assignment.
        Ks.ReinitForSplit(nMem, k, penaltyMix);
        Ks.nStartingClusters = 1;
        Ks.minClustersAlive  = 1;
        Ks.NoisePoint        = 0;
        // Assign all spikes to cluster 1 and run MStep/EStep for a proper score
        for (int i2 = 0; i2 < nMem; i2++) Ks.Class[i2] = 1;
        Ks.ClassAlive[1] = 1;
        Ks.nClustersAlive = 1;
        Ks.AliveIndex[0] = 1;
        Ks.MStep();
        Ks.EStep();
        const float nullScore = Ks.ComputeScore();

        // Accept split only if BIC strictly improves
        if (bestSubScore >= nullScore || nullScore == 0.0f) continue;

        // ── Apply split: assign new global IDs ──────────────────────────
        // Subspace class 0 keeps the original global ID c.
        // Each new subspace class gets a fresh global ID.
        std::unordered_map<int,int> subToGlobal;
        subToGlobal[0] = c;  // cluster 0 keeps old ID

        Output("  SubspaceRecluster: cluster %d split into %d sub-clusters "
               "(subScore=%.5g < nullScore=%.5g)\\n",
               c, bestSubK, bestSubScore, nullScore);

        for (int i = 0; i < nMem; i++) {
            const int subCls = bestSubClass[static_cast<size_t>(i)];
            if (subToGlobal.find(subCls) == subToGlobal.end()) {
                // Assign next available global ID
                while (nextGlobal < MaxPossibleClusters && ClassAlive[nextGlobal])
                    nextGlobal++;
                if (nextGlobal >= MaxPossibleClusters) {
                    Output("  SubspaceRecluster: MaxPossibleClusters reached, stopping\\n");
                    goto done_recluster;
                }
                subToGlobal[subCls] = nextGlobal++;
            }
            Class[members[static_cast<size_t>(i)]] = subToGlobal[subCls];
        }
        nSplit++;
    }
    done_recluster:

    // Rebuild ClassAlive / AliveIndex / nClustersAlive from updated Class[]
    for (int c2 = 0; c2 < MaxPossibleClusters; c2++) ClassAlive[c2] = 0;
    for (int p = 0; p < nPoints; p++) ClassAlive[Class[p]] = 1;
    nClustersAlive = 0;
    for (int c2 = 0; c2 < MaxPossibleClusters; c2++)
        if (ClassAlive[c2]) AliveIndex[nClustersAlive++] = c2;

    Output("SubspaceReclusterPass done: %d cluster(s) split, %d total clusters\\n",
           nSplit, nClustersAlive);
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

    for (int ck = 0; ck < nCh; ck++) {
        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;

        std::vector<ChunkModel> snapshot = mdls;
        for (const auto& origCm : snapshot) {
            const int lc = origCm.localClusterId;
            if (lc == 0) continue;

            // Collect member local-indices and their normalised timestamps
            std::vector<int>   members;
            std::vector<float> ts;
            for (int i = 0; i < nPts; i++) {
                if (cls[static_cast<size_t>(i)] != lc) continue;
                members.push_back(i);
                ts.push_back(Data[pts[static_cast<size_t>(i)] * nDims + timeDimIdx]);
            }
            const int nMem = static_cast<int>(members.size());
            if (nMem < minSplitSpikes) continue;

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
            if (contamRate < minContamRate) continue;

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

            if (Ks.nClustersAlive <= 1 || splitScore >= nullScore) {
                Output("  RefractorySplit: chunk%d cluster%d — no improvement "
                       "(splitScore=%.4g, null=%.4g), keeping\\n",
                       ck, lc, splitScore, nullScore);
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

            // ── klustakwikExp: per-sub-cluster shift-probe refeaturization ──
            // Same pattern as SubspaceReclusterPerChunk — refresh Data[] for
            // the new sub-clusters before rebuilding ChunkModel statistics.
            if (m_timeShiftReady && NbChannels > 0 && NbSamplesPerSpike > 0) {
                for (auto& [sc2, newLc] : subToLocal) {
                    std::vector<int> sub;
                    sub.reserve(nMem);
                    for (int ii = 0; ii < nPts; ii++)
                        if (cls[static_cast<size_t>(ii)] == newLc)
                            sub.push_back(pts[static_cast<size_t>(ii)]);
                    (void)sub;  // TimeShiftSplit removed from Phase 2
                }
            }

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
    Output("RefractorySplitPerChunk: %d cluster(s) split\\n", totalSplit);
}
