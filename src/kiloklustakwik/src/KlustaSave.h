// KlustaSave.h — C++17 modernisation
// Changes: using declarations removed from header (moved to .cpp files that need them)
#pragma once

#include <vector>
#include <string>
#include "Array.h"

class KlustaSave {
public:
    std::vector<Array<float>> *pChol     = nullptr;  // working Cholesky matrices
    std::vector<Array<float>> *pBestChol = nullptr;  // best Cholesky matrices

    int         nDims            = 0;
    std::string FileBase;
    std::vector<float> dataMin;
    std::vector<float> dataMax;
    std::vector<int>   BestAliveIndex;
    int         nDimsBest            = 0;
    int         nBestClustersAlive   = 0;
    Array<float> BestWeight;
    Array<float> BestMean;
    float BestScoreSave              = 1e32f;
    int   cEStepCallsLast            = 0;
    int   cEStepCallsSave            = 0;
};
