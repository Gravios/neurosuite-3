/***************************************************************************
 * reorder_similarity_gpu.h
 *
 * Common C interface implemented by each GPU backend (CUDA, HIP, SYCL) for
 * single-linkage agglomerative clustering on an N×N similarity matrix.
 *
 * Used by KlustersApp::slotReorderClustersBySimilarity (Shift+S) to speed
 * up the O(N³) merge loop on sessions with hundreds of clusters.  For small
 * N the inline CPU path in klusters.cpp remains faster than GPU dispatch
 * overhead — see the threshold in reorder_similarity_gpu_dispatch.cpp.
 *
 * Inputs / outputs
 * ----------------
 *   S            in   [N×N] row-major symmetric similarity matrix, host
 *                     pointer.  Diagonal entries are not read.
 *   N            in   Number of nodes (clusters >= 2 in the source matrix).
 *   mergeBi_out  out  [N-1] one index per merge step — the "kept" node
 *                     into which the partner is absorbed.  Sentinel value
 *                     -1 marks an early-exit step (disconnected matrix).
 *   mergeBj_out  out  [N-1] one index per merge step — the "absorbed"
 *                     node, marked dead after this step.
 *
 * Caller reconstructs the leaf order on the host by walking the merge tree
 * (leaves[mergeBi[s]].extend(leaves[mergeBj[s]])).  This split keeps the
 * GPU side stateless and avoids variable-length per-node lists in device
 * memory.
 ***************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the backend has a usable device, 0 otherwise. */
int cuda_reorder_similarity_available();
int hip_reorder_similarity_available();
int sycl_reorder_similarity_available();

/**
 * Run single-linkage agglomerative clustering on the given similarity
 * matrix.  Returns 0 on success, non-zero on failure.  On failure the
 * caller should fall back to the next backend (or the CPU path).
 */
int cuda_reorder_similarity(const double* S, int N,
                            int* mergeBi_out, int* mergeBj_out);
int hip_reorder_similarity (const double* S, int N,
                            int* mergeBi_out, int* mergeBj_out);
int sycl_reorder_similarity(const double* S, int N,
                            int* mergeBi_out, int* mergeBj_out);

#ifdef __cplusplus
}
#endif
