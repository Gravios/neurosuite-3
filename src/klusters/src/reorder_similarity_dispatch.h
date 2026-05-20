/***************************************************************************
 * reorder_similarity_dispatch.h
 *
 * C++ public face of the reorder-by-similarity GPU dispatcher.  The C
 * symbols in reorder_similarity_gpu.h are an implementation detail;
 * klusters.cpp talks to the dispatcher through this namespace.
 ***************************************************************************/

#pragma once

namespace ReorderSimilarityGpu {

/** True iff at least one GPU backend reported a usable device on its
 *  first probe.  Cached after the first call. */
bool hasGpu();

/** Below this N the dispatcher always returns failure (forces the CPU
 *  loop) — at small sizes the GPU launch overhead dominates. */
int gpuThreshold();

/** Try to compute the single-linkage merge log on the GPU.
 *  @param S            in   N×N row-major symmetric similarity matrix
 *  @param N            in   number of nodes
 *  @param mergeBi_out  out  [N-1] surviving-node indices, one per step
 *  @param mergeBj_out  out  [N-1] absorbed-node indices, one per step
 *  Returns 0 on success.  Any non-zero return means the caller should
 *  run the CPU fallback loop. */
int singleLinkage(const double* S, int N, int* mergeBi_out, int* mergeBj_out);

} // namespace ReorderSimilarityGpu
