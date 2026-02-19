// KK.h — C++17 modernised KlustaKwik clustering engine
// Additions over v1.7:
//   - Farthest-point seeding: InitCentresFarthestPoint / InitClassFromCentres
//   - Two-phase CEM:       CEMTwoPhase(timeMergeIter)
//   - Three-phase chunked: RunChunkedCEM(...)  — parallel over chunks via OpenMP
#pragma once
#include "Array.h"
#include <functional>
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
    void  CStep();
    void  ConsiderDeletion();
    void  LoadClu(const char *StartCluFile);
    int   TrySplits();
    float CEM(const char *CluFile = nullptr, int recurse = 1);
    void  Reindex();

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
                           float mergeThresh);

    float RunChunkedCEM(float chunkMinutes,
                        float samplingRate,
                        float mergeThresh,
                        int   globalMergeIter,
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

    // Raw time range for the last feature dimension, captured by LoadData()
    // before normalisation so RunChunkedCEM can convert chunkMinutes ->
    // normalised chunk boundaries without needing kSv.dataMin/dataMax.
    float timeRawMin = 0.0f;
    float timeRawMax = 1.0f;

    // -----------------------------------------------------------------------
    // Data arrays
    // -----------------------------------------------------------------------
    Array<float> Data;       // [nPoints x nDims]  point-major
    Array<float> Centres;    // [MaxPossibleClusters x nDims]  farthest-point seeds
    Array<float> Weight;     // [MaxPossibleClusters]
    Array<float> Mean;       // [MaxPossibleClusters x nDims]
    Array<float> Cov;        // [MaxPossibleClusters x nDims²]  upper-tri stored
    Array<float> LogP;       // [nPoints x MaxPossibleClusters]
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
    // threads.  Allocated by AlocateCholeskyVecs(); owned by this instance.
    // -----------------------------------------------------------------------
    std::vector<Array<float>> *pChol     = nullptr;
    std::vector<Array<float>> *pBestChol = nullptr;

    // When true, CEMTwoPhase inner loops skip SaveBestMeans() and do not
    // update kSv.BestScoreSave.  Set automatically on chunk sub-objects so
    // parallel per-chunk EM cannot corrupt the outer loop's best-score state.
    bool suppressBestSave = false;
};
