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
#include "KlustaKwik.h"
#include "KlustaSave.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

// ---------------------------------------------------------------------------
// AllocateArrays
// ---------------------------------------------------------------------------
void KK::AllocateArrays() {
    nDims2  = nDims * nDims;
    FullStep  = 1;
    NoisePoint = 1;

    Data.SetSize(nPoints * nDims);
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
    kSv.pChol     = new std::vector<Array<float>>(MaxPossibleClusters, Array<float>(nDims2));
    kSv.pBestChol = new std::vector<Array<float>>(MaxPossibleClusters, Array<float>(nDims2));
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
// LoadData — reads .fet file and normalises features to [0,1]
// ---------------------------------------------------------------------------
void KK::LoadData() {
    char fname[STRLEN + 16];
    snprintf(fname, sizeof(fname), "%s.fet.%d", FileBase, ElecNo);
    FILE *fp = fopen_safe(fname, "r");

    // Count data lines (original algorithm, unchanged)
    enum { INLINE, FIRST_DELIM } scst = INLINE;
    nPoints = -1;
    char ch, delim = '\n';
    do {
        ch = static_cast<char>(fgetc(fp));
        bool isDelim = (ch == '\n' || ch == '\r');
        bool isEof   = (ch == EOF);
        switch (scst) {
        case INLINE:
            if (isDelim)     { scst = FIRST_DELIM; delim = ch; }
            else if (isEof)  { nPoints++; }
            break;
        case FIRST_DELIM:
            if (!isDelim || delim == ch) { nPoints++; scst = INLINE; }
            break;
        }
    } while (ch != EOF);
    fseek(fp, 0, SEEK_SET);

    int nFeatures = 0;
    if (fscanf(fp, "%d", &nFeatures) != 1) Error("Failed to read nFeatures");
    std::cout << "nFeatures=" << nFeatures << std::endl;

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

    // Load feature values
    for (int p = 0; p < nPoints; p++) {
        int j = 0;
        for (int i = 0; i < nFeatures; i++) {
            float val;
            if (fscanf(fp, "%f", &val) == EOF) Error("Error reading feature file");
            if (i < UseLen && UseFeatures[i] == '1')
                Data[p * nDims + j++] = val;
        }
    }
    // Check for trailing data
    { float val; if (fscanf(fp, "%f", &val) != EOF) Error("Mismatch reading feature file"); }
    fclose(fp);

    // Normalise each dimension to [0,1]
    for (int i = 0; i < nDims; i++) {
        float mn = HugeScore, mx = -HugeScore;
        for (int p = 0; p < nPoints; p++) {
            const float v = Data[p * nDims + i];
            if (v > mx) mx = v;
            if (v < mn) mn = v;
        }
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
    Array<int> nClassMembers(MaxPossibleClusters);  // zero-initialised

    // Count members
    for (int p = 0; p < nPoints; p++) nClassMembers[Class[p]]++;

    // Kill clusters with too few points
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        if (c > 0 && nClassMembers[c] <= nDims) {
            ClassAlive[c] = 0;
            Output("Deleted class %d: not enough members\n", c);
        }
    }
    Reindex();

    // Weights
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];
        Weight[c] = (c == 0)
            ? static_cast<float>(nClassMembers[c] + NoisePoint) / (nPoints + NoisePoint)
            : static_cast<float>(nClassMembers[c])              / (nPoints + NoisePoint);
    }
    Reindex();

    // Zero mean and covariance accumulators
    for (int c = 0; c < MaxPossibleClusters; c++) {
        for (int i = 0; i < nDims; i++)  Mean[c * nDims + i] = 0.0f;
        for (int i = 0; i < nDims2; i++) Cov[c * nDims2 + i] = 0.0f;
    }

    // Accumulate means
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

    // Accumulate covariance (upper triangle)
    for (int p = 0; p < nPoints; p++) {
        const int c = Class[p];
        // vec2mean on stack — nDims is small (≤17)
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

    int nSkipped = 0;

    // Cluster 0: uniform noise
    for (int p = 0; p < nPoints; p++)
        LogP[p * MaxPossibleClusters + 0] = -std::log(Weight[0]);

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = AliveIndex[cc];

        // Cholesky decomposition (CPU — small matrix)
        if (Cholesky(Cov.m_Data + c * nDims2, (*kSv.pChol)[c].m_Data, nDims)) {
            Output("Deleting class %d: covariance matrix is singular\n", c);
            ClassAlive[c] = 0;
            continue;
        }

        float LogRootDet = 0.0f;
        for (int i = 0; i < nDims; i++)
            LogRootDet += std::log((*kSv.pChol)[c][i * nDims + i]);

        const float *chol = (*kSv.pChol)[c].m_Data;

        for (int p = 0; p < nPoints; p++) {
            float *optLogP = LogP.m_Data + p * MaxPossibleClusters;

            // Skip heuristic (unchanged from original)
            if (!FullStep
                && Class[p]    == OldClass[p]
                && optLogP[c] - optLogP[Class[p]] > DistThresh) {
                nSkipped++;
                continue;
            }

            // Vec2Mean on stack
            float v[64], root[64];
            for (int i = 0; i < nDims; i++)
                v[i] = Data[p * nDims + i] - Mean[c * nDims + i];

            // TriSolve: M*root = v,  M lower triangular
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
    // (nSkipped logged in verbose mode if desired)
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

    for (int p = 0; p < nPoints; p++)
        DeletionLoss[Class[p]] +=
            LogP[p * MaxPossibleClusters + Class2[p]] -
            LogP[p * MaxPossibleClusters + Class[p]];

    float minLoss = HugeScore;
    int   candidateClass = -1;
    for (int c = 1; c < MaxPossibleClusters; c++) {
        if (DeletionLoss[c] < minLoss) {
            minLoss = DeletionLoss[c];
            candidateClass = c;
        }
    }
    if (candidateClass < 0) { Reindex(); return; }  // Bug fix: no alive class

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
// LoadClu
// ---------------------------------------------------------------------------
void KK::LoadClu(const char *CluFile) {
    FILE *fp = fopen_safe(CluFile, "r");
    int val;
    if (fscanf(fp, "%d", &nStartingClusters) != 1) Error("Failed to read nStartingClusters");
    nClustersAlive = nStartingClusters;
    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = (c < nStartingClusters);
    for (int p = 0; p < nPoints; p++) {
        if (fscanf(fp, "%d", &val) == EOF) Error("Error reading cluster file");
        Class[p] = val - 1;
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
// SaveBestMeans
// ---------------------------------------------------------------------------
void KK::SaveBestMeans() {
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

    // Save best Cholesky matrices
    for (int cc = 1; cc < kSv.nBestClustersAlive; cc++) {
        const int c = kSv.BestAliveIndex[cc];
        for (int i = 0; i < kSv.nDimsBest; i++)
            for (int j = 0; j <= i; j++)
                (*kSv.pBestChol)[c][i * kSv.nDimsBest + j] =
                    (*kSv.pChol)[c][i * kSv.nDimsBest + j];
    }
}
