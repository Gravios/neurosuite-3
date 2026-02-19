// KK.h — C++17 modernised KlustaKwik clustering engine
// Changes:
//   - Member variables use descriptive initialisers
//   - Data layout annotated for CUDA port (see GPU notes)
#pragma once
#include "Array.h"

class KK {
public:
    // --- Methods -----------------------------------------------------------
    void AllocateArrays();
    void AlocateCholeskyVecs();
    void SaveBestMeans();
    void LoadData();
    float Penalty(int n) const;
    float ComputeScore() const;
    void MStep();
    void EStep();
    void CStep();
    void ConsiderDeletion();
    void LoadClu(const char *StartCluFile);
    int  TrySplits();
    float CEM(const char *CluFile = nullptr, int recurse = 1);
    void Reindex();

public:
    // --- Dimensions --------------------------------------------------------
    int nDims  = 0;   // number of features actually used
    int nDims2 = 0;   // nDims * nDims

    // --- Cluster bookkeeping -----------------------------------------------
    int nStartingClusters = 0;
    int nClustersAlive    = 0;
    int nPoints           = 0;
    int NoisePoint        = 1;
    int FullStep          = 1;

    float penaltyMix = 0.0f;

    // --- Data arrays -------------------------------------------------------
    //
    // GPU LAYOUT NOTE:
    //   Current layout: Data[p * nDims + d]  (point-major)
    //   For coalesced GPU reads in EStep (threads over points, dim in register):
    //     Preferred: Data[d * nPoints + p]   (dim-major / SoA)
    //   The GPU kernel in KK_cuda.cu uses the transposed layout.
    //   CPU path keeps original layout for backward compatibility.
    //
    Array<float> Data;    // [nPoints × nDims]  point-major on CPU
    Array<float> Weight;  // [MaxPossibleClusters]
    Array<float> Mean;    // [MaxPossibleClusters × nDims]
    Array<float> Cov;     // [MaxPossibleClusters × nDims × nDims] upper-tri stored
    //
    // GPU LAYOUT NOTE:
    //   Current: LogP[p * MaxPossibleClusters + c]  (point-major)
    //   For coalesced GPU writes in EStep (threads over points):
    //     Preferred: LogP[c * nPoints + p]   (cluster-major / SoA)
    //   GPU path transposes on transfer; CPU path keeps original.
    //
    Array<float> LogP;       // [nPoints × MaxPossibleClusters]
    Array<int>   Class;      // [nPoints]
    Array<int>   OldClass;   // [nPoints]
    Array<int>   Class2;     // [nPoints]
    Array<int>   BestClass;  // [nPoints]
    Array<int>   ClassAlive; // [MaxPossibleClusters]
    Array<int>   AliveIndex; // [MaxPossibleClusters]
};
