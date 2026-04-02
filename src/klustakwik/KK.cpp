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
    snprintf(fname, sizeof(fname), "%s.fet.%d", FileBase, ElecNo);
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
    if (minLoss < deltaPen) {
        Output("Deleting Class %d. Lose %f but Gain %f\n", candidateClass, minLoss, deltaPen);
        ClassAlive[candidateClass] = 0;
        for (int p = 0; p < nPoints; p++)
            if (Class[p] == candidateClass) Class[p] = Class2[p];
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
void KK::RealignChunkWaveforms(
    const std::vector<std::vector<int>>& chunkPoints,
    const std::vector<std::vector<int>>& chunkClass,
    int nChan, int nSamplesPerSpike, int bytesPerSample,
    std::vector<int>& spikeShifts)
{
    if (nChan <= 0 || nSamplesPerSpike <= 0) return;
    if (bytesPerSample != 2 && bytesPerSample != 4) {
        Output("RealignChunkWaveforms: unsupported bytesPerSample=%d — skipping\n",
               bytesPerSample);
        return;
    }

    char spkFname[STRLEN + 16];
    snprintf(spkFname, sizeof(spkFname), "%s.spk.%d", FileBase, ElecNo);
    FILE* fp = fopen(spkFname, "r+b");
    if (!fp) {
        Output("RealignChunkWaveforms: cannot open %s for in-place rewrite — skipping\n",
               spkFname);
        return;
    }

    Output("RealignChunkWaveforms: backend = %s, bytesPerSample = %d\n",
           XcorrDispatch::backendName(), bytesPerSample);

    const int waveSamples = nChan * nSamplesPerSpike;
    const int maxShift    = std::max(1, nSamplesPerSpike / 4);
    int nAligned = 0, nSkipped = 0;

    const int nChunks = static_cast<int>(chunkPoints.size());

    // ── Overlap deferral sets ──────────────────────────────────────────────
    // An overlap spike (global index p) appears in both chunkPoints[k] and
    // chunkPoints[k+1].  If we write it back during chunk k's pass, chunk k+1
    // will read the already-shifted waveform and shift it again (double-align),
    // and its cluster mean will be a mixture of aligned and unaligned waveforms
    // (biased template).
    //
    // Fix: skip the writeback for p during chunk k's pass.  Chunk k+1 still
    // reads the original waveform (not yet written), computes a clean mean,
    // and owns the single authoritative write.  The mean computation for chunk k
    // is unaffected — we read before we write, so the wave buffer is correct.
    //
    // overlapInNext[k] = {p : p in chunkPoints[k] AND p in chunkPoints[k+1]}
    std::vector<std::unordered_set<int>> overlapInNext(nChunks);
    for (int k = 0; k + 1 < nChunks; k++) {
        std::unordered_set<int> nextSet(chunkPoints[k + 1].begin(),
                                        chunkPoints[k + 1].end());
        for (int p : chunkPoints[k])
            if (nextSet.count(p)) overlapInNext[k].insert(p);
        if (!overlapInNext[k].empty())
            Output("  Chunk %d: deferring writeback of %d overlap spikes to chunk %d\n",
                   k, static_cast<int>(overlapInNext[k].size()), k + 1);
    }

    for (int k = 0; k < nChunks; k++) {
        const auto& pts = chunkPoints[k];
        const auto& cls = chunkClass[k];
        const int   nPts = static_cast<int>(pts.size());

        // ── Cluster membership map ─────────────────────────────────────────
        // cid -> list of local indices into pts/cls
        std::unordered_map<int, std::vector<int>> members;
        members.reserve(MaxPossibleClusters);
        for (int i = 0; i < nPts; i++)
            members[cls[i]].push_back(i);

        // ── Read waveforms for this chunk ──────────────────────────────────
        // Sample-major layout: waves[localIdx*waveSamples + s*nChan + c]
        // Always stored as int16 internally; upsample from int32 if needed.
        std::vector<int16_t> waves(static_cast<size_t>(nPts) * waveSamples, 0);
        for (int i = 0; i < nPts; i++) {
            const int p = pts[i];
            fseeko(fp, static_cast<off_t>(p) * waveSamples * bytesPerSample, SEEK_SET);
            int16_t* dst = waves.data() + static_cast<size_t>(i) * waveSamples;
            if (bytesPerSample == 2) {
                const size_t nRead = fread(dst, sizeof(int16_t), waveSamples, fp);
                if (static_cast<int>(nRead) != waveSamples)
                    std::fill(dst, dst + waveSamples, int16_t(0));
            } else {
                // 32-bit recording: read int32, downcast to int16 for xcorr
                std::vector<int32_t> tmp(static_cast<size_t>(waveSamples));
                const size_t nRead = fread(tmp.data(), sizeof(int32_t), waveSamples, fp);
                if (static_cast<int>(nRead) != waveSamples)
                    std::fill(dst, dst + waveSamples, int16_t(0));
                else
                    for (int s = 0; s < waveSamples; ++s)
                        dst[s] = static_cast<int16_t>(tmp[s] >> 16);
            }
        }

        // ── Per-cluster iterative alignment (mirrors Klusters realignSpikes) ──
        //
        // Phase15Iters iterations, matching Klusters' nIter loop:
        //   1. Build cluster mean from current waveBuf (updated each iter)
        //   2. Pre-align template peak to PeakSampleIndex
        //   3. Xcorr waveBuf against template
        //   4. Circularly shift waveBuf in-place (cheap; improves next iter's mean)
        //   5. Accumulate cumulative shift into spikeShifts[]
        // After all iters, RefeaturizeFromShifts re-extracts from .fil using
        // the total cumulative shift — no wrap-around artifact.
        for (auto& [cid, idxList] : members) {
            if (cid == 0) { nSkipped += static_cast<int>(idxList.size()); continue; }
            const int nMem = static_cast<int>(idxList.size());

            // Build channel-major spike batch — updated in-place each iteration
            //   waveBuf[(mi*nChan + ch) * nSamplesPerSpike + t]
            std::vector<int16_t> waveBuf(
                static_cast<size_t>(nMem) * nChan * nSamplesPerSpike);
            for (int mi = 0; mi < nMem; mi++) {
                const int localIdx = idxList[mi];
                const int16_t* src = waves.data()
                                   + static_cast<size_t>(localIdx) * waveSamples;
                for (int ch = 0; ch < nChan; ch++)
                    for (int s = 0; s < nSamplesPerSpike; s++)
                        waveBuf[(static_cast<size_t>(mi) * nChan + ch)
                                * nSamplesPerSpike + s] = src[s * nChan + ch];
            }

            // cumIterShift[mi] = total shift accumulated across all iters for spike mi
            std::vector<int> cumIterShift(static_cast<size_t>(nMem), 0);

            const int nIter = Phase15Iters;  // always > 0 (guard above)
            for (int iter = 0; iter < nIter; iter++) {

                // 1. Build mean from current waveBuf (channel-major, int64 accumulator)
                std::vector<int64_t> acc(
                    static_cast<size_t>(nChan) * nSamplesPerSpike, 0);
                for (int mi = 0; mi < nMem; mi++) {
                    const int16_t* w = waveBuf.data()
                        + static_cast<ptrdiff_t>(mi) * (nChan * nSamplesPerSpike);
                    for (int e = 0; e < nChan * nSamplesPerSpike; e++)
                        acc[static_cast<size_t>(e)] += static_cast<int64_t>(w[e]);
                }
                std::vector<int16_t> tmplBuf(static_cast<size_t>(nChan) * nSamplesPerSpike);
                for (int e = 0; e < nChan * nSamplesPerSpike; e++)
                    tmplBuf[static_cast<size_t>(e)] =
                        static_cast<int16_t>(acc[static_cast<size_t>(e)] / nMem);

                // 2. Xcorr waveBuf vs template
                // Note: template peak pre-alignment (as used in Klusters) is intentionally
                // omitted here. On multi-channel probes a single unit may have genuine
                // temporal offsets between channels (e.g. one channel peaks 2-3 samples
                // before another). Forcing the summed-amplitude peak to PeakSampleIndex
                // distorts such templates and causes erroneous shifts. The xcorr finds
                // the lag maximising correlation without needing the template repositioned.
                // minScore=0.70 matches Klusters default: only accept shifts
                // where the normalised xcorr exceeds 0.70. Below this the
                // correlation is unreliable and the shift would be spurious.
                const float minXcorrScore = 0.70f;
                std::vector<int>   iterShifts(static_cast<size_t>(nMem), 0);
                std::vector<float> iterScores(static_cast<size_t>(nMem), 0.0f);
                XcorrDispatch::compute(
                    waveBuf.data(), tmplBuf.data(),
                    nMem, nChan, nSamplesPerSpike,
                    maxShift, minXcorrScore,
                    iterShifts.data(), iterScores.data());

                // 3. Circularly shift waveBuf in-place (channel-major)
                //    newSpike[ch*N + t] = oldSpike[ch*N + (t + s + N) % N]
                //    Mirrors Klusters: tmp[ch*N+t] = w[ch*N + (t+s+N)%N]
                int changed = 0;
                for (int mi = 0; mi < nMem; mi++) {
                    const int s = iterShifts[static_cast<size_t>(mi)];
                    if (s == 0) continue;
                    int16_t* w = waveBuf.data()
                        + static_cast<ptrdiff_t>(mi) * (nChan * nSamplesPerSpike);
                    std::vector<int16_t> tmp(static_cast<size_t>(nChan) * nSamplesPerSpike);
                    for (int t = 0; t < nSamplesPerSpike; t++) {
                        const int src = (t + s + nSamplesPerSpike) % nSamplesPerSpike;
                        for (int ch = 0; ch < nChan; ch++)
                            tmp[static_cast<size_t>(ch * nSamplesPerSpike + t)] =
                                w[static_cast<size_t>(ch * nSamplesPerSpike + src)];
                    }
                    std::copy(tmp.begin(), tmp.end(), w);
                    cumIterShift[static_cast<size_t>(mi)] += s;
                    ++changed;
                }
                if (changed == 0) break;   // converged early

            } // end iter loop

            // 4. Write total cumulative shift into spikeShifts[] — home-chunk
            //    first-write-wins for overlap spikes.
            //    RefeaturizeFromShifts will re-extract from .fil at
            //    rawTs + cumShift - PeakSampleIndex (clean, no wrap-around).
            for (int mi = 0; mi < nMem; mi++) {
                const int sh       = cumIterShift[static_cast<size_t>(mi)];
                const int localIdx = idxList[mi];
                const int p        = pts[localIdx];
                if (p < static_cast<int>(spikeShifts.size()) &&
                    spikeShifts[p] == std::numeric_limits<int>::min())
                    spikeShifts[p] = sh;
                if (sh != 0) nAligned++;
            }
        }
    }

    fclose(fp);
    Output("RealignChunkWaveforms: aligned %d spikes, skipped %d noise\n",
           nAligned, nSkipped);
}



// ---------------------------------------------------------------------------
// RefeaturizeFromShifts
//
// For each spike with a non-zero xcorr shift (set by RealignChunkWaveforms),
// re-extracts the aligned waveform from the .fil broadband file at the
// corrected sample offset, projects through the saved PCA eigenvectors,
// re-normalises, and writes back into Data[].
//
// Re-extracting from .fil rather than circular-shifting the .spk waveform
// eliminates wrap-around corruption: circular shift of N samples by sh
// fills positions [N-sh .. N-1] with noise from the beginning of the
// original window, which corrupts up to 30% of samples for typical
// 5–10 sample shifts.  Reading from .fil at (rawTs - sh - peakIdx)
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
    char pcaPath[STRLEN + 16];
    snprintf(pcaPath, sizeof(pcaPath), "%s.pca.%d", FileBase, ElecNo);
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
    snprintf(spkPath, sizeof(spkPath), "%s.spk.%d", FileBase, ElecNo);

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
            (void)fread(&rawTsInt, sizeof(int64_t), 1, resRFS);
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

    fprintf(stderr, "[Phase 1] Chunk CEM (%d threads, %d chunks)\n",
            nThreads, nActive);
    #pragma omp parallel for schedule(dynamic) default(none) \
        num_threads(nThreads) \
        shared(perChunkModels, perChunkAssign, perChunkScore, perChunkNClusters, \
               chunkPoints, perChunkClass, nActive, nFullDims, timeMergeIter, threadKc) \
        firstprivate(MaxPossibleClusters, nStartingClusters, penaltyMix, \
                     nRuns, HugeScore, RandomSeed) \
        shared(stderr)
    for (int k = 0; k < nActive; k++) {
        const std::vector<int>& pts = chunkPoints[k];
        const int nPts = static_cast<int>(pts.size());

        KK& Kc = threadKc[
#ifdef _OPENMP
            omp_get_thread_num()
#else
            0
#endif
        ];
        // ── nRuns restarts per chunk ────────────────────────────────────
        // nRuns > 0: run CEMTwoPhase nRuns times with different seeds,
        // keep the best-scoring solution for this chunk before Phase 2.
        // Mirrors nStarts in non-chunked mode but confined to one chunk
        // so Phase 0, Phase 1.5 and Phase 2 each still run only once.
        const int runsPerChunk = (nRuns > 0) ? nRuns : 1;
        float bestChunkScore = HugeScore;
        int   bestNClusters  = 0;
        std::vector<int> bestClass(static_cast<size_t>(nPts), 0);

        for (int run = 0; run < runsPerChunk; run++) {
            srand(static_cast<unsigned>(RandomSeed)
                  + static_cast<unsigned>(k) * 997u
                  + static_cast<unsigned>(run));
            Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
            Kc.nStartingClusters = nStartingClusters;
            Kc.NoisePoint        = 1;
            for (int i2 = 0; i2 < nPts; i2++) {
                const int p2 = pts[i2];
                for (int d = 0; d < nFullDims; d++)
                    Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
            }
            const float runScore = Kc.CEMTwoPhase(timeMergeIter);
            if (runScore < bestChunkScore) {
                bestChunkScore = runScore;
                bestNClusters  = Kc.nClustersAlive;
                for (int i2 = 0; i2 < nPts; i2++) bestClass[i2] = Kc.Class[i2];
            }
            #pragma omp critical
            { fprintf(stderr, "  [chunk %d/%d  run %d/%d] score=%.4g  nclusters=%d\n",
                      k + 1, nActive, run + 1, runsPerChunk,
                      runScore, Kc.nClustersAlive); }
        }
        // Restore best Class[] for harvesting, then recompute Mean/Cov.
        for (int i2 = 0; i2 < nPts; i2++) Kc.Class[i2] = bestClass[i2];
        for (int c = 0; c < MaxPossibleClusters; c++) Kc.ClassAlive[c] = 0;
        for (int i2 = 0; i2 < nPts; i2++) Kc.ClassAlive[Kc.Class[i2]] = 1;
        Kc.nClustersAlive = 0;
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (Kc.ClassAlive[c]) Kc.AliveIndex[Kc.nClustersAlive++] = c;
        if (runsPerChunk > 1) Kc.MStep();  // recompute Mean/Cov for harvesting

        perChunkScore[k]     = bestChunkScore;
        perChunkNClusters[k] = bestNClusters;

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
            assign.emplace_back(pts[i], k * MaxPossibleClusters + Kc.Class[i]);

        // Harvest Class[] for Phase 1.5 realignment
        auto& classArr = perChunkClass[k];
        classArr.resize(nPts);
        for (int i = 0; i < nPts; i++) classArr[i] = Kc.Class[i];
    }

    for (int k = 0; k < nActive; k++) {
        Output("  Chunk %d / %d  (%d spikes): %d clusters, score %.5g\n",
               k, nActive - 1, static_cast<int>(chunkPoints[k].size()),
               perChunkNClusters[k], perChunkScore[k]);
        // Do NOT move models into allModels yet — WithinChunkTemplateMatch
        // will update perChunkModels (remove merged-away entries) and
        // perChunkClass (remap IDs). allModels must reflect post-merge state
        // so that MergeChunkModels vote keys match allModels localClusterIds.
        for (auto& [p, packed] : perChunkAssign[k]) pointPacked[p] = packed;
    }


    // ── Phase 1.5: xcorr alignment + .fil re-extraction + PCA re-projection ─
    //
    // Step 1 — RealignChunkWaveforms: xcorr each spike against its cluster mean
    //   waveform (from .spk).  Cheap: .spk is already on disk; XcorrDispatch
    //   is batched per cluster.  Home-chunk first-write-wins for overlap spikes.
    //
    // Step 2 — RefeaturizeFromShifts: re-extract each shifted spike from .fil
    //   at (rawTs - shift - PeakSampleIndex), project through PCA eigenvectors,
    //   update Data[] before Phase 2.  Using .fil eliminates the circular-shift
    //   wrap-around that corrupts ~30% of samples when shifting in .spk alone.
    //   Falls back to circular shift when .fil is unavailable.
    // ── Phase 2.5: per-chunk subspace reclustering (pre-realignment) ─────────
    if (SubspaceRecluster > 0) {
        fprintf(stderr, "[Phase 2.5] Subspace reclustering (per-chunk, pre-alignment)\n");
        SubspaceReclusterPerChunk(
            SubspaceDims > 0 ? SubspaceDims : 3,
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
    }


    if (Phase15Iters > 0 && NbChannels > 0 && NbSamplesPerSpike > 0) {
        fprintf(stderr, "[Phase 1.5] Xcorr waveform realignment (circular)\n");
        Output("Phase 1.5: xcorr alignment (nChan=%d nSamp=%d nIter=%d)\n",
               NbChannels, NbSamplesPerSpike, Phase15Iters);
        std::vector<int> spikeShifts(static_cast<size_t>(nPoints),
                                     std::numeric_limits<int>::min());
        RealignChunkWaveforms(chunkPoints, perChunkClass,
                              NbChannels, NbSamplesPerSpike, NbBytesPerSample,
                              spikeShifts);
        RefeaturizeFromShifts(spikeShifts, NbChannels, NbSamplesPerSpike);
        WritePhase15Checkpoint(spikeShifts, NbChannels, NbSamplesPerSpike);
    }

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        fprintf(stderr, "[Phase 1.6] Mean waveform harvest (channel-major xcorr format)\n");
    // Populate ChunkModel::meanWav for template matching.
    // Done serially after the parallel chunk loop since all chunks share
    // the same .spk file handle and fseeko calls cannot be parallelised safely.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
        NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        char spkPathTM[STRLEN + 16];
        snprintf(spkPathTM, sizeof(spkPathTM), "%s.spk.%d", FileBase, ElecNo);
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
                snprintf(spkPathTM2, sizeof(spkPathTM2), "%s.spk.%d", FileBase, ElecNo);
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
            fprintf(stderr, "[Phase 1.7] Within-chunk circular xcorr template matching (iter %d)\n",
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

    fprintf(stderr, "[Phase 2]   Cross-chunk model matching (overlap + Mahal + xcorr)\n");
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
        fprintf(stderr, "[Phase 3]   Skipped (GlobalMergeIter=0)\n");
        Output("Phase 3 skipped (GlobalMerge=0) — using Phase 2 assignment directly\n");
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
        fprintf(stderr, "[Phase 3]   Global warm-start EM\n");
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
            if (nChanged == 0) { Output("Phase 3 converged at iter %d\n", iter); break; }
        }
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

    fprintf(stderr, "[Phase 1] Chunk CEM (%d threads, %d chunks)\n",
            nThreads, nActive);
    #pragma omp parallel for schedule(dynamic) default(none) \
        num_threads(nThreads) \
        shared(perChunkModels, perChunkAssign, perChunkScore, perChunkNClusters, \
               chunkPoints, perChunkClass, globalPreseedCentres, \
               nActive, nFullDims, timeMergeIter, threadKc) \
        firstprivate(MaxPossibleClusters, nStartingClusters, penaltyMix, chunkStartK, \
                     nRuns, HugeScore, RandomSeed) \
        shared(stderr)
    for (int k = 0; k < nActive; k++) {
        const std::vector<int>& pts = chunkPoints[k];
        const int nPts = static_cast<int>(pts.size());

        KK& Kc = threadKc[
#ifdef _OPENMP
            omp_get_thread_num()
#else
            0
#endif
        ];
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        // ── nRuns restarts per chunk ────────────────────────────────────
        const int runsPerChunk = (nRuns > 0) ? nRuns : 1;
        float bestChunkScore = HugeScore;
        int   bestNClusters  = 0;
        std::vector<int> bestClass(static_cast<size_t>(nPts), 0);
        for (int run = 0; run < runsPerChunk; run++) {
            srand(static_cast<unsigned>(RandomSeed)
                  + static_cast<unsigned>(k) * 997u
                  + static_cast<unsigned>(run));
            Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
            Kc.nStartingClusters = chunkStartK;
            Kc.NoisePoint        = 1;
            for (int i2 = 0; i2 < nPts; i2++) {
                const int p2 = pts[i2];
                for (int d = 0; d < nFullDims; d++)
                    Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
            }
            const float runScore = Kc.CEMTwoPhase(timeMergeIter);
            if (runScore < bestChunkScore) {
                bestChunkScore = runScore;
                bestNClusters  = Kc.nClustersAlive;
                for (int i2 = 0; i2 < nPts; i2++) bestClass[i2] = Kc.Class[i2];
            }
            #pragma omp critical
            { fprintf(stderr, "  [chunk %d/%d  run %d/%d] score=%.4g  nclusters=%d\n",
                      k + 1, nActive, run + 1, runsPerChunk,
                      runScore, Kc.nClustersAlive); }
        }
        for (int i2 = 0; i2 < nPts; i2++) Kc.Class[i2] = bestClass[i2];
        for (int c = 0; c < MaxPossibleClusters; c++) Kc.ClassAlive[c] = 0;
        for (int i2 = 0; i2 < nPts; i2++) Kc.ClassAlive[Kc.Class[i2]] = 1;
        Kc.nClustersAlive = 0;
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (Kc.ClassAlive[c]) Kc.AliveIndex[Kc.nClustersAlive++] = c;
        if (runsPerChunk > 1) Kc.MStep();  // recompute Mean/Cov for harvesting
        perChunkScore[k]     = bestChunkScore;
        perChunkNClusters[k] = bestNClusters;

        // Harvest models into this chunk's private vector
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

        // Harvest point assignments into this chunk's private vector
        auto& assign = perChunkAssign[k];
        assign.reserve(nPts);
        for (int i = 0; i < nPts; i++)
            assign.emplace_back(pts[i], k * MaxPossibleClusters + Kc.Class[i]);

        // Harvest Class[] for overlap vote building (each k writes its own slot)
        {
            auto& classArr = perChunkClass[k];
            classArr.resize(nPts);
            for (int i = 0; i < nPts; i++) classArr[i] = Kc.Class[i];
        }
    }   // end omp parallel for

    // Serial reduction: gather per-chunk results in chunk order
    for (int k = 0; k < nActive; k++) {
        Output("  Chunk %d / %d  (%d spikes): %d clusters, score %.5g\n",
               k, nActive - 1, static_cast<int>(chunkPoints[k].size()),
               perChunkNClusters[k], perChunkScore[k]);
        for (auto& [p, packed] : perChunkAssign[k])
            if (pointPacked[p] < 0) pointPacked[p] = packed;  // first-write-wins
    }


    // ── Phase 1.5: xcorr alignment + .fil re-extraction + PCA re-projection ─
    //
    // Step 1 — RealignChunkWaveforms: xcorr each spike against its cluster mean
    //   waveform (from .spk).  Cheap: .spk is already on disk; XcorrDispatch
    //   is batched per cluster.  Home-chunk first-write-wins for overlap spikes.
    //
    // Step 2 — RefeaturizeFromShifts: re-extract each shifted spike from .fil
    //   at (rawTs - shift - PeakSampleIndex), project through PCA eigenvectors,
    //   update Data[] before Phase 2.  Using .fil eliminates the circular-shift
    //   wrap-around that corrupts ~30% of samples when shifting in .spk alone.
    //   Falls back to circular shift when .fil is unavailable.
    // ── Phase 2.5: per-chunk subspace reclustering (pre-realignment) ─────────
    if (SubspaceRecluster > 0) {
        fprintf(stderr, "[Phase 2.5] Subspace reclustering (per-chunk, pre-alignment)\n");
        SubspaceReclusterPerChunk(
            SubspaceDims > 0 ? SubspaceDims : 3,
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
    }


    if (Phase15Iters > 0 && NbChannels > 0 && NbSamplesPerSpike > 0) {
        fprintf(stderr, "[Phase 1.5] Xcorr waveform realignment (circular)\n");
        Output("Phase 1.5: xcorr alignment (nChan=%d nSamp=%d nIter=%d)\n",
               NbChannels, NbSamplesPerSpike, Phase15Iters);
        std::vector<int> spikeShifts(static_cast<size_t>(nPoints),
                                     std::numeric_limits<int>::min());
        RealignChunkWaveforms(chunkPoints, perChunkClass,
                              NbChannels, NbSamplesPerSpike, NbBytesPerSample,
                              spikeShifts);
        RefeaturizeFromShifts(spikeShifts, NbChannels, NbSamplesPerSpike);
        WritePhase15Checkpoint(spikeShifts, NbChannels, NbSamplesPerSpike);
    }

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        fprintf(stderr, "[Phase 1.6] Mean waveform harvest (channel-major xcorr format)\n");
    // Populate ChunkModel::meanWav for template matching.
    // Done serially after the parallel chunk loop since all chunks share
    // the same .spk file handle and fseeko calls cannot be parallelised safely.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
        NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        char spkPathTM[STRLEN + 16];
        snprintf(spkPathTM, sizeof(spkPathTM), "%s.spk.%d", FileBase, ElecNo);
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
                snprintf(spkPathTM2, sizeof(spkPathTM2), "%s.spk.%d", FileBase, ElecNo);
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
            fprintf(stderr, "[Phase 1.7] Within-chunk circular xcorr template matching (iter %d)\n",
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

    fprintf(stderr, "[Phase 2]   Cross-chunk model matching (overlap + Mahal + xcorr)\n");
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
        Output("Phase 3 skipped (GlobalMerge=0) — using Phase 2 assignment directly\n");
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
        fprintf(stderr, "[Phase 3]   Global warm-start EM\n");
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
            if (nChanged == 0) { Output("Phase 3 converged at iter %d\n", iter); break; }
        }
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
// first-write-wins in RealignChunkWaveforms, so every spike's shift is
// derived from the chunk where it naturally lives.  Overlap spikes that
// also appeared in a later chunk retain their home-chunk shift here.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// KK::WritePhase15Checkpoint
//
// After RealignChunkWaveforms + RefeaturizeFromShifts, write corrected .spk
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
    snprintf(spkOrig, sizeof(spkOrig), "%s.spk.%d",         FileBase, ElecNo);
    snprintf(fetOrig, sizeof(fetOrig), "%s.fet.%d",         FileBase, ElecNo);
    snprintf(spkTmp,  sizeof(spkTmp),  "%s.spk.%d.pending", FileBase, ElecNo);
    snprintf(fetTmp,  sizeof(fetTmp),  "%s.fet.%d.pending", FileBase, ElecNo);

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
                        (void)fread(&rawTsWPC, sizeof(int64_t), 1, resWPC);
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
                    }
                    // If .fil read fails, keep original (already in spkRow)
                }
                // If no .fil, spkRow already holds the original; the circular
                // shift applied by RealignChunkWaveforms is NOT in .spk (write-
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
                        (void)fread(&rawTsF, sizeof(int64_t), 1, resWPC);
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
    const int k       = std::min(subspaceDims, nFullDims - 1);
    const int nCh     = static_cast<int>(chunkPoints.size());
    const int nSpatial = nFullDims - 1;
    const int minSpikes = k * nFullDims * 3;

    // ── Phase A: serial pre-processing ─────────────────────────────────────
    // For each (chunk, cluster) build a projected sub-KK and compute nullScore.
    // Reads Data[] (read-only) and perChunkClass/perChunkModels (read-only here).

    struct WorkItem {
        int   ck, lc;
        int   nMem;
        std::vector<int>   members;      // local indices into chunkPoints[ck]
        std::vector<float> ksData;       // projected + normalised subspace data [nMem × k]
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

            // Top-k eigenvectors
            std::vector<float> evecs, evals;
            topKEigen(clCovUT.data(), nSpatial, nFullDims, k, evecs, evals);

            // Project into whitened k-space
            WorkItem wi;
            wi.ck = ck; wi.lc = lc; wi.nMem = nMem;
            wi.members = std::move(members);
            wi.ksData.resize(static_cast<size_t>(nMem) * k);
            for (int i = 0; i < nMem; i++) {
                const int p = pts[static_cast<size_t>(wi.members[static_cast<size_t>(i)])];
                for (int ki = 0; ki < k; ki++) {
                    float proj = 0.0f;
                    for (int d = 0; d < nSpatial; d++)
                        proj += evecs[static_cast<size_t>(ki * nSpatial + d)]
                              * (Data[p * nDims + d] - clMean[static_cast<size_t>(d)]);
                    float scale = (evals[static_cast<size_t>(ki)] > 1e-12f)
                                ? std::sqrt(evals[static_cast<size_t>(ki)]) : 1.0f;
                    wi.ksData[static_cast<size_t>(i * k + ki)] = proj / scale;
                }
            }

            // Normalise to [0,1]
            std::vector<float> subMin(static_cast<size_t>(k),  HugeScore);
            std::vector<float> subMax(static_cast<size_t>(k), -HugeScore);
            for (int i = 0; i < nMem; i++)
                for (int ki = 0; ki < k; ki++) {
                    float v = wi.ksData[static_cast<size_t>(i * k + ki)];
                    if (v < subMin[static_cast<size_t>(ki)]) subMin[static_cast<size_t>(ki)] = v;
                    if (v > subMax[static_cast<size_t>(ki)]) subMax[static_cast<size_t>(ki)] = v;
                }
            for (int i = 0; i < nMem; i++)
                for (int ki = 0; ki < k; ki++) {
                    float range = subMax[static_cast<size_t>(ki)] - subMin[static_cast<size_t>(ki)];
                    wi.ksData[static_cast<size_t>(i * k + ki)] = (range > 1e-12f)
                        ? (wi.ksData[static_cast<size_t>(i * k + ki)] - subMin[static_cast<size_t>(ki)]) / range
                        : 0.5f;
                }
            wi.timeRawMin = subMin[static_cast<size_t>(k - 1)];
            wi.timeRawMax = subMax[static_cast<size_t>(k - 1)];

            // Null score
            {
                KK Ks;
                Ks.nDims = k; Ks.nPoints = nMem;
                Ks.penaltyMix = penaltyMix; Ks.suppressBestSave = true;
                Ks.minClustersAlive = 1; Ks.AllocateArrays(); Ks.AllocateCholeskyVecs();
                for (int i = 0; i < nMem * k; i++) Ks.Data[i] = wi.ksData[static_cast<size_t>(i)];
                Ks.timeRawMin = wi.timeRawMin; Ks.timeRawMax = wi.timeRawMax;
                Ks.ReinitForSplit(nMem, k, penaltyMix);
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

    // ── Phase B: parallel sub-CEM ───────────────────────────────────────────
    // Each work item is independent — different KK scratch objects, read-only Data[].
    // Thread cap: min(nThreads, nItems) avoids idle threads.
    std::vector<Result> results(static_cast<size_t>(nItems));

#ifdef _OPENMP
    const int _srcThreads = std::min(
        (ompTeamSize > 0) ? ompTeamSize : omp_get_max_threads(),
        nItems);
#else
    const int _srcThreads = 1;
#endif

    #pragma omp parallel for schedule(dynamic) num_threads(_srcThreads) \
        default(none) \
        shared(items, results, nItems, k) \
        firstprivate(penaltyMix, RandomSeed, nRuns, HugeScore)
    for (int wi_idx = 0; wi_idx < nItems; wi_idx++) {
        const WorkItem& wi = items[static_cast<size_t>(wi_idx)];
        Result& res = results[static_cast<size_t>(wi_idx)];
        res.ck = wi.ck; res.lc = wi.lc;
        res.accepted = false; res.bestSubK = 1;
        res.bestSubClass.assign(static_cast<size_t>(wi.nMem), 0);

        if (!wi.valid) continue;

        const int runsPerSubCluster = (nRuns > 0) ? nRuns : 1;
        float bestSubScore = HugeScore;
        int   bestSubK = 1;

        KK Ks;
        Ks.nDims = k; Ks.nPoints = wi.nMem;
        Ks.penaltyMix = penaltyMix; Ks.suppressBestSave = true;
        Ks.minClustersAlive = 1; Ks.AllocateArrays(); Ks.AllocateCholeskyVecs();
        Ks.timeRawMin = wi.timeRawMin; Ks.timeRawMax = wi.timeRawMax;

        for (int run = 0; run < runsPerSubCluster; run++) {
            srand(static_cast<unsigned>(RandomSeed
                  + static_cast<unsigned>(wi.ck) * 997u
                  + static_cast<unsigned>(wi.lc) * 13u
                  + static_cast<unsigned>(run) * 7u));
            Ks.ReinitForSplit(wi.nMem, k, penaltyMix);
            for (int i = 0; i < wi.nMem * k; i++)
                Ks.Data[i] = wi.ksData[static_cast<size_t>(i)];
            Ks.timeRawMin = wi.timeRawMin; Ks.timeRawMax = wi.timeRawMax;
            Ks.nStartingClusters = 2; Ks.NoisePoint = 0;
            const float score = Ks.CEMTwoPhase(0);
            if (score < bestSubScore && Ks.nClustersAlive > 1) {
                bestSubScore = score;
                bestSubK     = Ks.nClustersAlive;
                for (int i2 = 0; i2 < wi.nMem; i2++)
                    res.bestSubClass[static_cast<size_t>(i2)] = Ks.Class[i2];
            }
        }

        if (bestSubK > 1 && bestSubScore < wi.nullScore) {
            res.accepted      = true;
            res.bestSubK      = bestSubK;
            res.bestSubScore  = bestSubScore;
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
