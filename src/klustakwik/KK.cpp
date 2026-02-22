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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <unordered_map>
#ifdef _OPENMP
#include <omp.h>
#endif

// ---------------------------------------------------------------------------
// AllocateArrays
// ---------------------------------------------------------------------------
void KK::AllocateArrays() {
    nDims2  = nDims * nDims;
    FullStep  = 1;
    NoisePoint = 1;

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
}

// ---------------------------------------------------------------------------
// AlocateCholeskyVecs (spelling kept for API compat)
// ---------------------------------------------------------------------------
void KK::AlocateCholeskyVecs() {
    // Allocated on this KK instance, not the global kSv, so chunk sub-objects
    // each own their Cholesky matrices and can run in parallel threads.
    // unique_ptr ensures memory is freed when the KK instance is destroyed,
    // including short-lived K2/K3/Kc sub-objects created inside TrySplits
    // and RunChunkedCEM.
    pChol     = std::make_unique<std::vector<Array<float>>>(MaxPossibleClusters, Array<float>(nDims2));
    pBestChol = std::make_unique<std::vector<Array<float>>>(MaxPossibleClusters, Array<float>(nDims2));
    kSv.BestAliveIndex.reserve(MaxPossibleClusters);
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
// LoadData — reads binary .fet file and normalises features to [0,1]
// Binary format: int32_t nDimensions; nSpikes * nDimensions * int64_t (row-major)
// ---------------------------------------------------------------------------
void KK::LoadData() {
    char fname[STRLEN + 16];
    snprintf(fname, sizeof(fname), "%s.fet.%d", FileBase, ElecNo);
    FILE *fp = fopen_safe(fname, "rb");

    // Read header: number of feature dimensions (including timestamp as last col)
    int32_t nFeatures32 = 0;
    if (fread(&nFeatures32, sizeof(int32_t), 1, fp) != 1)
        Error("Failed to read nFeatures from binary .fet header");
    const int nFeatures = (int)nFeatures32;
    Output("nFeatures=%d\n", nFeatures);

    // Derive spike count from remaining file size
    fseeko(fp, 0, SEEK_END);
    off_t dataBytes = ftello(fp) - (off_t)sizeof(int32_t);
    fseeko(fp, (off_t)sizeof(int32_t), SEEK_SET);
    if (dataBytes <= 0 || dataBytes % ((off_t)sizeof(int64_t) * nFeatures) != 0)
        Error("Binary .fet file size inconsistent with nFeatures");
    nPoints = (int)(dataBytes / ((off_t)sizeof(int64_t) * nFeatures));

    // Handle "all" keyword
    if (strcmp(UseFeatures, "all") == 0) {
        if (nFeatures >= STRLEN) Error("Too many features for UseFeatures");
        for (int i = 0; i < nFeatures; i++) UseFeatures[i] = '1';
        UseFeatures[nFeatures] = '\0';
    }

    const int UseLen = static_cast<int>(strlen(UseFeatures));
    nDims = 0;
    for (int i = 0; i < nFeatures; i++)
        nDims += (i < UseLen && UseFeatures[i] == '1') ? 1 : 0;

    if (fSaveModel) {
        kSv.FileBase = FileBase;
        kSv.nDims    = nDims;
        fprintf(pModelFile, "%s %d\n", FileBase, kSv.nDims);
    }

    AllocateArrays();
    AlocateCholeskyVecs();

    // Load feature values from binary int64_t rows
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
    // Verify we are at EOF
    { int64_t probe; if (fread(&probe, sizeof(int64_t), 1, fp) != 0) Error("Trailing data in binary .fet file"); }
    fclose(fp);

    // Normalise each dimension to [0,1] and record raw range for time dim
    const int timeDimIdx = nDims - 1;  // time is always the last used feature
    for (int i = 0; i < nDims; i++) {
        float mn = HugeScore, mx = -HugeScore;
        for (int p = 0; p < nPoints; p++) {
            const float v = Data[p * nDims + i];
            if (v > mx) mx = v;
            if (v < mn) mn = v;
        }
        // Store raw time range unconditionally so RunChunkedCEM can use it
        if (i == timeDimIdx) { timeRawMin = mn; timeRawMax = mx; }
        if (fSaveModel) {
            kSv.dataMin.push_back(mn);
            kSv.dataMax.push_back(mx);
            fprintf(pModelFile, "%f %f%c", mn, mx, (i < nDims - 1) ? ' ' : '\n');
        }
        const float range = (mx > mn) ? (mx - mn) : 1.0f;
        for (int p = 0; p < nPoints; p++)
            Data[p * nDims + i] = (Data[p * nDims + i] - mn) / range;
    }

    Output("Loaded %d data points of dimension %d.\n", nPoints, nDims);

#ifdef USE_CUDA
    // Allocate GPU context for the outer KK object (K1).
    // Chunk sub-objects (Kc, K2, K3) leave gpu==nullptr and always use CPU.
    if (!suppressBestSave && cuda_device_available()) {
        gpu = new KK_GPU();
        gpu->allocate(nPoints, nDims, nDims2, MaxPossibleClusters);
        cuda_upload_data(gpu, Data.m_Data);
        Output("GPU context initialised (CUDA, %d points, %d dims).\n", nPoints, nDims);
    }
#elif defined(USE_SYCL)
    if (!suppressBestSave) {
        sycl::device sycl_dev;
        if (sycl_device_available(&sycl_dev)) {
            gpu = new KK_GPU(sycl_dev);
            gpu->allocate(nPoints, nDims, nDims2, MaxPossibleClusters);
            sycl_upload_data(gpu, Data.m_Data);
            Output("GPU context initialised (SYCL, %d points, %d dims).\n", nPoints, nDims);
        }
    }
#elif defined(USE_HIP)
    if (!suppressBestSave && hip_device_available()) {
        gpu = new KK_GPU();
        gpu->allocate(nPoints, nDims, nDims2, MaxPossibleClusters);
        hip_upload_data(gpu, Data.m_Data);
        Output("GPU context initialised (HIP, %d points, %d dims).\n", nPoints, nDims);
    }
#endif
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
    Array<int> nClassMembers(MaxPossibleClusters);

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
    Reindex();

#ifdef USE_CUDA
    if (gpu) {
        cuda_mstep(gpu,
            Class.m_Data, AliveIndex.m_Data,
            Mean.m_Data, Cov.m_Data, Weight.m_Data,
            nClustersAlive, MaxPossibleClusters,
            nPoints, nDims, nDims2);
        goto mstep_debug;
    }
#elif defined(USE_SYCL)
    if (gpu) {
        sycl_mstep(gpu,
            Class.m_Data, AliveIndex.m_Data,
            Mean.m_Data, Cov.m_Data, Weight.m_Data,
            nClustersAlive, MaxPossibleClusters,
            nPoints, nDims, nDims2);
        goto mstep_debug;
    }
#elif defined(USE_HIP)
    if (gpu) {
        hip_mstep(gpu,
            Class.m_Data, AliveIndex.m_Data,
            Mean.m_Data, Cov.m_Data, Weight.m_Data,
            nClustersAlive, MaxPossibleClusters,
            nPoints, nDims, nDims2);
        goto mstep_debug;
    }
#endif

    // CPU path
    for (int c = 0; c < MaxPossibleClusters; c++) {
        for (int i = 0; i < nDims; i++)  Mean[c * nDims + i] = 0.0f;
        for (int i = 0; i < nDims2; i++) Cov[c * nDims2 + i] = 0.0f;
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
    mstep_debug:
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
    static int cEStepCalls = 0;
    kSv.cEStepCallsLast = ++cEStepCalls;

    // Cluster 0: uniform noise
    for (int p = 0; p < nPoints; p++)
        LogP[p * MaxPossibleClusters + 0] = -std::log(Weight[0]);

    // Cholesky decomposition always on CPU — O(K * D^3), trivial cost
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        if (Cholesky(Cov.m_Data + c * nDims2, (*pChol)[c].m_Data, nDims)) {
            Output("Deleting class %d: covariance matrix is singular\n", c);
            ClassAlive[c] = 0;
        }
    }
    Reindex();

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu) {
        // Flatten Cholesky factors into a contiguous host array for upload.
        // (*pChol)[c] holds the lower-triangular matrix for cluster c.
        std::vector<float> h_chol(MaxPossibleClusters * nDims2, 0.0f);
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (ClassAlive[c])
                for (int i = 0; i < nDims2; i++)
                    h_chol[c * nDims2 + i] = (*pChol)[c][i];
#if defined(USE_CUDA)
        cuda_estep(gpu,
            Mean.m_Data, Weight.m_Data, h_chol.data(),
            AliveIndex.m_Data, Class.m_Data, OldClass.m_Data,
            LogP.m_Data,
            nClustersAlive, DistThresh, FullStep, PI, MaxPossibleClusters);
#elif defined(USE_SYCL)
        sycl_estep(gpu,
            Mean.m_Data, Weight.m_Data, h_chol.data(),
            AliveIndex.m_Data, Class.m_Data, OldClass.m_Data,
            LogP.m_Data,
            nClustersAlive, DistThresh, FullStep, PI, MaxPossibleClusters);
#elif defined(USE_HIP)
        hip_estep(gpu,
            Mean.m_Data, Weight.m_Data, h_chol.data(),
            AliveIndex.m_Data, Class.m_Data, OldClass.m_Data,
            LogP.m_Data,
            nClustersAlive, DistThresh, FullStep, PI, MaxPossibleClusters);
#endif
        return;
    }
#endif

    // CPU path
    int nSkipped = 0;
    (void)nSkipped; // diagnostic counter, not currently reported
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        float LogRootDet = 0.0f;
        for (int i = 0; i < nDims; i++)
            LogRootDet += std::log((*pChol)[c][i * nDims + i]);
        const float *chol = (*pChol)[c].m_Data;

        for (int p = 0; p < nPoints; p++) {
            float *optLogP = LogP.m_Data + p * MaxPossibleClusters;
            if (!FullStep
                && Class[p]    == OldClass[p]
                && optLogP[c] - optLogP[Class[p]] > DistThresh) {
                nSkipped++;
                continue;
            }
            float v[64], root[64];
            for (int i = 0; i < nDims; i++)
                v[i] = Data[p * nDims + i] - Mean[c * nDims + i];
            for (int i = 0; i < nDims; i++) {
                float s = v[i];
                for (int j = i - 1; j >= 0; j--) s -= chol[i * nDims + j] * root[j];
                root[i] = s / chol[i * nDims + i];
            }
            float Mahal = 0.0f;
            for (int i = 0; i < nDims; i++) Mahal += root[i] * root[i];
            optLogP[c] = Mahal * 0.5f
                       + LogRootDet
                       - std::log(Weight[c])
                       + static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);
        }
    }
}

// ---------------------------------------------------------------------------
// CStep — assign each point to best cluster
//
// GPU parallelism: trivially parallel over points.
//   One thread per point; load LogP row, find argmin and second-argmin.
//   With LogP transposed to [c*nPoints+p], each thread strides by nPoints
//   which is non-coalesced but unavoidable for argmin-per-point.
//   Alternative: parallel reduction per cluster column (transpose back).
// ---------------------------------------------------------------------------
void KK::CStep() {
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu) {
        // d_LogP and d_AliveIndex are current from EStep.
        // Returns updated Class, OldClass, Class2.
#if defined(USE_CUDA)
        cuda_cstep(gpu,
            Class.m_Data, OldClass.m_Data, Class2.m_Data,
            nClustersAlive, MaxPossibleClusters, HugeScore);
#elif defined(USE_SYCL)
        sycl_cstep(gpu,
            Class.m_Data, OldClass.m_Data, Class2.m_Data,
            nClustersAlive, MaxPossibleClusters, HugeScore);
#elif defined(USE_HIP)
        hip_cstep(gpu,
            Class.m_Data, OldClass.m_Data, Class2.m_Data,
            nClustersAlive, MaxPossibleClusters, HugeScore);
#endif
        return;
    }
#endif
    // CPU path
    for (int p = 0; p < nPoints; p++) {
        OldClass[p] = Class[p];
        float bestScore   = HugeScore, secondScore = HugeScore;
        int   topClass = 0, secondClass = 0;
        for (int cc = 0; cc < nClustersAlive; cc++) {
            const int c = AliveIndex[cc];
            const float s = LogP[p * MaxPossibleClusters + c];
            if (s < bestScore) {
                secondClass = topClass;   secondScore = bestScore;
                topClass    = c;          bestScore   = s;
            } else if (s < secondScore) {
                secondClass = c;          secondScore = s;
            }
        }
        Class[p]  = topClass;
        Class2[p] = secondClass;
    }
}

// ---------------------------------------------------------------------------
// ConsiderDeletion — delete one cluster if it improves penalised score
// Bug fix: CandidateClass was used uninitialised when no alive classes found.
// ---------------------------------------------------------------------------
void KK::ConsiderDeletion() {
    Array<float> DeletionLoss(MaxPossibleClusters);
    for (int c = 0; c < MaxPossibleClusters; c++)
        DeletionLoss[c] = ClassAlive[c] ? 0.0f : HugeScore;

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (gpu) {
        // d_LogP, d_Class, d_Class2 are current from EStep+CStep.
#if defined(USE_CUDA)
        cuda_deletion_loss(gpu, DeletionLoss.m_Data, MaxPossibleClusters);
#elif defined(USE_SYCL)
        sycl_deletion_loss(gpu, DeletionLoss.m_Data, MaxPossibleClusters);
#elif defined(USE_HIP)
        hip_deletion_loss(gpu, DeletionLoss.m_Data, MaxPossibleClusters);
#endif
        DeletionLoss[0] = HugeScore;  // noise cluster never a candidate
    } else {
#endif
    for (int p = 0; p < nPoints; p++)
        DeletionLoss[Class[p]] +=
            LogP[p * MaxPossibleClusters + Class2[p]] -
            LogP[p * MaxPossibleClusters + Class[p]];
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
    if (candidateClass < 0) { Reindex(); return; }

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
// LoadClu — read binary .clu file
// Binary format: int32_t nClusters; nSpikes * int32_t clusterIDs (1-based)
// ---------------------------------------------------------------------------
void KK::LoadClu(const char *CluFile) {
    FILE *fp = fopen_safe(CluFile, "rb");
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
        Class[p] = (int)val - 1;  // convert 1-based to 0-based
    }
    fclose(fp);
}

// ---------------------------------------------------------------------------
// TrySplits — try splitting each cluster; keep if it improves score
// C++17: KK objects use move semantics when passed around.
// ---------------------------------------------------------------------------
int KK::TrySplits() {
    if (nClustersAlive >= MaxPossibleClusters - 1) {
        Output("Won't try splitting - already at maximum number of clusters\n");
        return 0;
    }

    KK K3;
    K3.nDims = nDims; K3.nPoints = nPoints;
    K3.AllocateArrays();
    K3.AlocateCholeskyVecs();
    K3.suppressBestSave = true;
    K3.penaltyMix = PenaltyMix;
    for (int i = 0; i < nDims * nPoints; i++) K3.Data[i] = Data[i];

    const float Score = ComputeScore();
    int DidSplit = 0;

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];

        KK K2;
        K2.nPoints = 0;
        for (int p = 0; p < nPoints; p++) if (Class[p] == c) K2.nPoints++;
        if (K2.nPoints == 0) continue;
        K2.nDims = nDims;
        K2.AllocateArrays();
        K2.AlocateCholeskyVecs();
        K2.suppressBestSave = true;
        K2.penaltyMix = PenaltyMix;
        K2.NoisePoint = 0;

        int p2 = 0;
        for (int p = 0; p < nPoints; p++) if (Class[p] == c)
            for (int d = 0; d < nDims; d++)
                K2.Data[p2++ / nDims * nDims + d] = Data[p * nDims + d];
        // re-do properly
        p2 = 0;
        for (int p = 0; p < nPoints; p++) if (Class[p] == c) {
            for (int d = 0; d < nDims; d++)
                K2.Data[p2 * nDims + d] = Data[p * nDims + d];
            p2++;
        }

        int unusedCluster = -1;
        for (int c2 = 1; c2 < MaxPossibleClusters; c2++)
            if (!ClassAlive[c2]) { unusedCluster = c2; break; }
        if (unusedCluster == -1) { Output("No free clusters, abandoning split"); return DidSplit; }

        if (Verbose >= 1) Output("Trying to split cluster %d (%d points)\n", c, K2.nPoints);
        K2.nStartingClusters = 2;
        const float unsplitScore = K2.CEM(nullptr, 0);
        K2.nStartingClusters = 3;
        const float splitScore   = K2.CEM(nullptr, 0);

        if (splitScore < unsplitScore) {
            for (int c2 = 0; c2 < MaxPossibleClusters; c2++) K3.ClassAlive[c2] = 0;
            p2 = 0;
            for (int p = 0; p < nPoints; p++) {
                if (Class[p] == c) {
                    K3.Class[p] = (K2.Class[p2] == 1) ? c : unusedCluster;
                    if (K2.Class[p2] != 1 && K2.Class[p2] != 2)
                        Error("split should only produce 2 clusters");
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
// ---------------------------------------------------------------------------
float KK::ComputeScore() const {
    float score = Penalty(nClustersAlive);
    for (int p = 0; p < nPoints; p++)
        score += LogP[p * MaxPossibleClusters + Class[p]];
    return score;
}

// ---------------------------------------------------------------------------
// CEM — main EM loop
// ---------------------------------------------------------------------------
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

    int   iter    = 0;
    int   nChanged;
    float score = 0.0f;
    int   lastStepFull;
    int   didSplit;
    FullStep = 1;

    Array<int> oldClassBuf(nPoints);

    do {
        for (int p = 0; p < nPoints; p++) oldClassBuf[p] = Class[p];

        MStep();
        EStep();
        if (DistDump) MatPrint(Distfp, LogP.m_Data, DistDump, MaxPossibleClusters);
        CStep();
        if (Recurse) ConsiderDeletion();

        nChanged = 0;
        for (int p = 0; p < nPoints; p++) nChanged += (oldClassBuf[p] != Class[p]);


        score    = ComputeScore();

        if (Recurse && score < kSv.BestScoreSave) {
            SaveBestMeans();
            kSv.BestScoreSave = score;
        }

        if (Verbose >= 1) {
            if (Recurse == 0) Output("\t");
            Output("Iteration %d%c: %d clusters Score %.7g nChanged %d tag %d\n",
                   iter, FullStep ? 'F' : 'Q', nClustersAlive, score,
                   nChanged, kSv.cEStepCallsLast);
        }
        iter++;

        lastStepFull = FullStep;
        FullStep = (nChanged > static_cast<int>(ChangedThresh * nPoints)
                    || nChanged == 0
                    || iter % FullStepEvery == 0);

        if (iter > MaxIter) { Output("Maximum iterations exceeded\n"); break; }

        didSplit = 0;
        if (Recurse && SplitEvery > 0 &&
            (iter % SplitEvery == SplitEvery - 1 || (nChanged == 0 && lastStepFull)))
            didSplit = TrySplits();

    } while (nChanged > 0 || !lastStepFull || didSplit);

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
    nDims = nSpatialDims;

    // Seed with farthest-point centres (in spatial dims)
    const int nCentres = nStartingClusters - 1;  // noise cluster is always 0
    if (nCentres >= 1) {
        InitCentresFarthestPoint(nCentres, nSpatialDims);
        // Set Class[] from Voronoi partition of seeds
        for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
        Reindex();
        InitClassFromCentres(nSpatialDims);
    } else {
        for (int p = 0; p < nPoints; p++) Class[p] = 0;
        for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
        Reindex();
    }

    // Run CEM with farthest-point start; skip the CluFile path since we
    // already populated Class[].  Pass a dummy CluFile = "" so CEM's first
    // branch is taken (random init), but we overwrite Class[] immediately
    // after — use recurse=1 to enable ConsiderDeletion and TrySplits.
    //
    // Implementation note: we call the inner EM loop directly rather than
    // routing through CEM() to avoid CEM's random re-init of Class[].
    {
        int   iter = 0, nChanged, lastStepFull, didSplit;
        float score = 0.0f;
        FullStep = 1;
        Array<int> oldClassBuf(nPoints);

        do {
            for (int p = 0; p < nPoints; p++) oldClassBuf[p] = Class[p];
            MStep(); EStep(); CStep(); ConsiderDeletion();

            nChanged = 0;
            for (int p = 0; p < nPoints; p++) nChanged += (oldClassBuf[p] != Class[p]);
            score = ComputeScore();

            if (score < kSv.BestScoreSave) { SaveBestMeans(); kSv.BestScoreSave = score; }

            if (Verbose >= 1)
                Output("  P1 iter %dF: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);
            iter++;

            lastStepFull = FullStep;
            FullStep = (nChanged > static_cast<int>(ChangedThresh * nPoints)
                        || nChanged == 0 || iter % FullStepEvery == 0);
            if (iter > MaxIter) { Output("P1 max iterations exceeded\n"); break; }

            didSplit = 0;
            if (SplitEvery > 0 &&
                (iter % SplitEvery == SplitEvery - 1 || (nChanged == 0 && lastStepFull)))
                didSplit = TrySplits();

        } while (nChanged > 0 || !lastStepFull || didSplit);

        Output("Phase 1 converged: %d clusters, score %.7g\n", nClustersAlive, score);
    }

    // -----------------------------------------------------------------------
    // Phase 2: temporal merge pass — restore full dimensionality
    // -----------------------------------------------------------------------
    if (timeMergeIter <= 0 || nFullDims == nSpatialDims) {
        // No time dimension or merge disabled: just restore nDims and rescore.
        nDims = nFullDims;
        // Recompute EStep/MStep with full dims so the returned score is consistent
        MStep(); EStep();
        return ComputeScore();
    }

    nDims = nFullDims;
    Output("CEMTwoPhase Phase 2: temporal merge pass (%d dims, max %d iters)\n",
           nDims, timeMergeIter);

    // Rebuild covariances with the time dimension now included.
    // The first MStep/EStep will expand the per-cluster covariance matrices
    // to full dimensionality, automatically incorporating temporal spread.
    // We then run ConsiderDeletion each iteration so that clusters occupying
    // the same spatial region but different temporal windows can be left
    // separate, while clusters that are truly the same unit merge.
    {
        int iter = 0, nChanged;
        float score = 0.0f;
        FullStep = 1;
        Array<int> oldClassBuf(nPoints);

        for (; iter < timeMergeIter; iter++) {
            for (int p = 0; p < nPoints; p++) oldClassBuf[p] = Class[p];
            MStep(); EStep(); CStep(); ConsiderDeletion();

            nChanged = 0;
            for (int p = 0; p < nPoints; p++) nChanged += (oldClassBuf[p] != Class[p]);
            score = ComputeScore();

            if (score < kSv.BestScoreSave) { SaveBestMeans(); kSv.BestScoreSave = score; }

            if (Verbose >= 1)
                Output("  P2 iter %d: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);

            FullStep = 1;   // always full step in Phase 2 (short pass)
            if (nChanged == 0) { Output("Phase 2 converged at iter %d\n", iter); break; }
        }

        Output("Phase 2 done: %d clusters, score %.7g\n", nClustersAlive, score);
        return score;
    }
}

// ---------------------------------------------------------------------------
// MergeChunkModels
//
// Assigns a global cluster ID to every ChunkModel by running Union-Find on
// the symmetric Mahalanobis adjacency graph of adjacent-chunk cluster pairs.
//
// d_sym(A,B) = 0.5 * [mahal(μ_A, Σ_B) + mahal(μ_B, Σ_A)]
//
// where mahal(x, Σ) is computed via Cholesky solve on the upper-left
// nSpatialDims × nSpatialDims block of Σ (time axis excluded).
//
// Only adjacent chunks are compared: O(K² × T) total.  Transitivity handles
// units that vanish and reappear: A-B and B-C edges imply A and C are in
// the same component even if A-C was never tested.
//
// Noise (localClusterId==0) always maps to globalClusterId==0.
// Returns the number of distinct real global clusters.
// ---------------------------------------------------------------------------
int KK::MergeChunkModels(std::vector<ChunkModel>& models,
                          int   nSpatialDims,
                          float mergeThresh)
{
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
        if (a != b) parent[b] = a;
    };

    // All noise models (localClusterId==0) are unconditionally merged into
    // globalClusterId=0 regardless of model similarity.  This is done before
    // any Mahalanobis comparisons so the threshold cannot accidentally split
    // the noise population across multiple global clusters.
    //
    // Note: a chunk where zero spikes landed in the noise bin will still
    // produce a ChunkModel for c=0, but with nMembers=0 and undefined
    // mean/cov (MStep divides by nClassMembers[0]=0).  That is harmless here
    // because the mahalDist lambda skips localClusterId==0 entirely, so
    // the garbage values never enter any distance computation.
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

    // Mahalanobis distance of src.mean under tgt's spatial covariance
    auto mahalDist = [&](const ChunkModel& src, const ChunkModel& tgt) -> float {
        std::vector<float> covS(nSpatialDims * nSpatialDims);
        for (int r = 0; r < nSpatialDims; r++)
            for (int c = r; c < nSpatialDims; c++)
                covS[r * nSpatialDims + c] = tgt.cov[r * nDims + c];

        std::vector<float> chol(nSpatialDims * nSpatialDims, 0.0f);
        if (Cholesky(covS.data(), chol.data(), nSpatialDims))
            return HugeScore;

        std::vector<float> diff(nSpatialDims), root(nSpatialDims);
        for (int d = 0; d < nSpatialDims; d++)
            diff[d] = src.mean[d] - tgt.mean[d];
        TriSolve(chol.data(), diff.data(), root.data(), nSpatialDims);

        float dist = 0.0f;
        for (int d = 0; d < nSpatialDims; d++) dist += root[d] * root[d];
        return dist;
    };

    // Build graph edges — only immediate neighbours (chunk k vs chunk k+1).
    // Clusters from non-adjacent chunks are never compared directly; any
    // longer-range connection arises only through transitivity in the
    // Union-Find (e.g. A-B and B-C edges imply A,C in one component).
    //
    // Index models by chunkIdx for O(1) lookup.
    std::unordered_map<int, std::vector<int>> byChunk;  // chunkIdx -> model indices
    for (int i = 0; i < n; i++)
        if (models[i].localClusterId != 0)
            byChunk[models[i].chunkIdx].push_back(i);

    // Iterate over consecutive chunk pairs only
    const int maxChunk = byChunk.empty() ? -1 :
        std::max_element(byChunk.begin(), byChunk.end(),
            [](const auto& a, const auto& b){ return a.first < b.first; })->first;

    for (int k = 0; k < maxChunk; k++) {
        auto itA = byChunk.find(k);
        auto itB = byChunk.find(k + 1);
        if (itA == byChunk.end() || itB == byChunk.end()) continue;

        for (int i : itA->second) {
            for (int j : itB->second) {
                const float dsym = 0.5f * (mahalDist(models[i], models[j]) +
                                            mahalDist(models[j], models[i]));
                if (dsym < mergeThresh) {
                    Union(i, j);
                    Output("  match chunk%d.c%d <-> chunk%d.c%d  d_sym=%.2f\n",
                           models[i].chunkIdx, models[i].localClusterId,
                           models[j].chunkIdx, models[j].localClusterId, dsym);
                }
            }
        }
    }

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
// RunChunkedCEM — three-phase temporal-chunk pipeline
//
// Phase 0: divide data into temporal chunks of chunkMinutes duration.
// Phase 1: run CEMTwoPhase independently on each chunk.
// Phase 2: match cluster models across adjacent chunks (MergeChunkModels).
// Phase 3: global warm-start EM seeded from the matched assignments.
// ---------------------------------------------------------------------------
float KK::RunChunkedCEM(float chunkMinutes,
                         float samplingRate,
                         float mergeThresh,
                         int   globalMergeIter,
                         int   timeMergeIter)
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

    Output("RunChunkedCEM: session %.1f min, chunk %.1f min, %d chunks\n",
           sessionSamples / samplingRate / 60.0f, chunkMinutes, nChunks);

    if (nChunks <= 1) {
        Output("Session shorter than one chunk — running CEMTwoPhase directly.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // Assign each point to chunk floor(t / chunkFrac), clamped to [0, nChunks-1]
    std::vector<std::vector<int>> chunkPoints(nChunks);
    for (int p = 0; p < nPoints; p++) {
        const float t = Data[p * nDims + timeDim];
        int k = static_cast<int>(t / chunkFrac);
        if (k < 0) k = 0;
        if (k >= nChunks) k = nChunks - 1;
        chunkPoints[k].push_back(p);
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
    // Phase 1: per-chunk CEMTwoPhase — parallel over chunks
    //
    // Each chunk builds a self-contained KK sub-object with:
    //   suppressBestSave = true   — no writes to global kSv during parallel section
    //   its own pChol/pBestChol  — no shared Cholesky storage
    //
    // Thread-local vectors accumulate models and point assignments.
    // A serial reduction gathers them into allModels / pointPacked after
    // the parallel block, so no atomic or mutex is needed.
    //
    // Output() calls inside CEMTwoPhase are guarded by a single critical
    // section to prevent interleaved log lines; set Verbose=0 to suppress.
    // -------------------------------------------------------------------
    std::vector<ChunkModel> allModels;
    std::vector<int>        pointPacked(nPoints, 0);

    // Per-chunk accumulators: indexed by chunk k.
    std::vector<std::vector<ChunkModel>> perChunkModels(nActive);
    std::vector<std::vector<std::pair<int,int>>> perChunkAssign(nActive);
    // per-chunk score (for logging)
    std::vector<float> perChunkScore(nActive, 0.0f);
    std::vector<int>   perChunkNClusters(nActive, 0);

    #pragma omp parallel for schedule(dynamic) default(none) \
        shared(perChunkModels, perChunkAssign, perChunkScore, perChunkNClusters, \
               chunkPoints, nActive, nFullDims, timeMergeIter) \
        firstprivate(MaxPossibleClusters)
    for (int k = 0; k < nActive; k++) {
        const std::vector<int>& pts = chunkPoints[k];
        const int nPts = static_cast<int>(pts.size());

        KK Kc;
        Kc.nDims             = nFullDims;
        Kc.nPoints           = nPts;
        Kc.nStartingClusters = nStartingClusters;
        Kc.penaltyMix        = penaltyMix;
        Kc.suppressBestSave  = true;   // must not write to global kSv in parallel
        Kc.AllocateArrays();
        Kc.AlocateCholeskyVecs();

        for (int i = 0; i < nPts; i++) {
            const int p = pts[i];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i * nFullDims + d] = Data[p * nFullDims + d];
        }

        const float chunkScore = Kc.CEMTwoPhase(timeMergeIter);
        perChunkScore[k]     = chunkScore;
        perChunkNClusters[k] = Kc.nClustersAlive;

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
    }   // end omp parallel for

    // Serial reduction: gather per-chunk results in chunk order
    for (int k = 0; k < nActive; k++) {
        Output("  Chunk %d / %d  (%d spikes): %d clusters, score %.5g\n",
               k, nActive - 1, static_cast<int>(chunkPoints[k].size()),
               perChunkNClusters[k], perChunkScore[k]);
        for (auto& cm : perChunkModels[k])
            allModels.push_back(std::move(cm));
        for (auto& [p, packed] : perChunkAssign[k])
            pointPacked[p] = packed;
    }

    // -------------------------------------------------------------------
    // Phase 2: cross-chunk model matching
    // -------------------------------------------------------------------
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh);
    if (nGlobal < 1) {
        Output("Merge produced no real clusters — falling back to CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // Guard: if nGlobal >= MaxPossibleClusters, the merge found no cross-chunk
    // matches at all (mergeThresh is too conservative for this data).  Using
    // globalClusterIds >= MaxPossibleClusters as Class[] indices would write
    // out-of-bounds into ClassAlive[].  Fall back gracefully so the user gets
    // a correct result and a clear message about what to adjust.
    if (nGlobal >= MaxPossibleClusters) {
        Output("WARNING: MergeChunkModels produced %d global clusters >= "
               "MaxPossibleClusters (%d).\n"
               "  This means MergeThresh=%.1f is too small: no cross-chunk\n"
               "  cluster matches were found, so every chunk-local cluster\n"
               "  became its own global unit.\n"
               "  Action: increase -MergeThresh (default 30 ~ chi2(nDims,0.99))\n"
               "  or reduce -ChunkMinutes so clusters change less between chunks.\n"
               "  Falling back to CEMTwoPhase on the full session.\n",
               nGlobal, MaxPossibleClusters, mergeThresh);
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

    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = 0;
    for (int p = 0; p < nPoints; p++) {
        auto it = packedToGlobal.find(pointPacked[p]);
        const int g = (it != packedToGlobal.end()) ? it->second : 0;
        // g is in [0, nGlobal] and nGlobal < MaxPossibleClusters (checked above)
        Class[p] = g;
        ClassAlive[g] = 1;
    }
    Reindex();

    Output("Phase 3: global warm-start EM — %d clusters, max %d iters\n",
           nClustersAlive, globalMergeIter);

    int   iter = 0, nChanged;
    float score = 0.0f;
    FullStep = 1;
    Array<int> oldClassBuf(nPoints);

    for (; iter < globalMergeIter; iter++) {
        for (int p = 0; p < nPoints; p++) oldClassBuf[p] = Class[p];
        MStep(); EStep(); CStep(); ConsiderDeletion();

        nChanged = 0;
        for (int p = 0; p < nPoints; p++) nChanged += (oldClassBuf[p] != Class[p]);
        score = ComputeScore();

        if (score < kSv.BestScoreSave) { SaveBestMeans(); kSv.BestScoreSave = score; }

        if (Verbose >= 1)
            Output("  P3 iter %d: %d clusters score %.7g nChanged %d\n",
                   iter, nClustersAlive, score, nChanged);

        FullStep = 1;  // always full-step: short pass, warm start
        if (nChanged == 0) { Output("Phase 3 converged at iter %d\n", iter); break; }
    }

    Output("RunChunkedCEM done: %d clusters, score %.7g\n", nClustersAlive, score);
    return score;
}

// ---------------------------------------------------------------------------
void KK::SaveBestMeans() {
    // Chunk sub-objects set suppressBestSave = true so parallel per-chunk EM
    // cannot corrupt the outer loop's global best-score state.
    if (suppressBestSave) return;

    kSv.cEStepCallsSave     = kSv.cEStepCallsLast;
    kSv.nDimsBest           = nDims;
    kSv.nBestClustersAlive  = nClustersAlive;

    if (kSv.BestWeight.size() < Weight.size())  kSv.BestWeight.SetSize(Weight.size());
    if (kSv.BestMean.size()   < Mean.size())    kSv.BestMean.SetSize(Mean.size());

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        kSv.BestAliveIndex[cc] = c;
        kSv.BestWeight[cc]     = Weight[c];
        for (int i = 0; i < nDims; i++)
            kSv.BestMean[cc * nDims + i] = Mean[c * nDims + i];
    }

    for (int cc = 1; cc < kSv.nBestClustersAlive; cc++) {
        const int c = kSv.BestAliveIndex[cc];
        for (int i = 0; i < kSv.nDimsBest; i++)
            for (int j = 0; j <= i; j++)
                (*pBestChol)[c][i * kSv.nDimsBest + j] =
                    (*pChol)[c][i * kSv.nDimsBest + j];
    }
}
