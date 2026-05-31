/***************************************************************************
                   KK_internal.h
                   -------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Shared internal symbols for the KK.cpp decomposition.  Holds the
 cross-cutting constants and free functions used by more than one of the
 KK_*.cpp translation units, so they need a single point of declaration
 rather than travelling with one group.
 ***************************************************************************/
#ifndef KK_INTERNAL_H
#define KK_INTERNAL_H

// Compile-time ceiling on nDims for the fixed-size stack scratch buffers in
// MStep/EStep (KK.cpp) and the split-trial covariance buffers in
// RefineExistingClustering (KK_refeaturize.cpp).
inline constexpr int kMaxStackDims = 64;

// Variational-Bayes GMM inner loop (defined in KK_vbgmm.cpp).  Called from
// both FullCemSplitPerChunk (KK_split.cpp) and ChunkReCEMPerChunk
// (KK_chunked.cpp), so it has external linkage and lives in its own TU
// instead of travelling with either consumer.
int RunVBGMM(const float* data, int* labels, int K_init,
             int N, int D, int maxIter = 50, double convTol = 1e-3);

#endif // KK_INTERNAL_H
