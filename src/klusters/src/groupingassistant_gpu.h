/***************************************************************************
 * groupingassistant_gpu.h
 *
 * Common C interface implemented by each GPU backend
 * (CUDA, HIP, SYCL) and by the CPU fallback stub.
 *
 * All arrays are flat, row-major, 0-based doubles on the HOST.
 *
 * Array layouts
 * -------------
 *   features    [nbSpikes  × nbDim]            features[s*nbDim + d]
 *   choleskyAll [nbClusters × nbDim × nbDim]   col-major lower-triangle per cluster
 *                                               chol[c*nbDim*nbDim + row + col*nbDim]
 *   means       [nbClusters × nbDim]            means[c*nbDim + d]
 *   logTerms    [nbClusters]                    logRootDet - logWeight + piTerm
 *   ignoreFlags [nbClusters]                    1 = skip cluster
 *   probOut     [nbSpikes  × nbClusters]        output, in-place normalized
 *
 * cluster1Col : 0-based column index of cluster 1 (used for zero-probability fallback)
 ***************************************************************************/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the backend has a usable device, 0 otherwise. */
int cuda_device_available();
int hip_device_available();
int sycl_device_available();

/**
 * Full probability computation on the specified backend.
 * Returns 0 on success, non-zero on failure.
 * On failure the caller should fall back to the next backend.
 */
int cuda_compute_probabilities(
    const double* features,
    const double* choleskyAll,
    const double* means,
    const double* logTerms,
    double*       probOut,
    const int*    ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col);

/**
 * Compute posteriors and aggregate them into the nbClusters x nbClusters error
 * matrix entirely on the device; only errOut (nbClusters^2 doubles) is written
 * back, not the full nbSpikes x nbClusters intermediate.
 *   featRow [nbSpikes]     : 0-based feature row of the spike at each position
 *   first   [nbClusters]   : 0-based first position of each cluster's spikes
 *   nb      [nbClusters]   : spike count per cluster (contiguous positions)
 *   errOut  [nbClusters^2] : errOut[i*nbClusters + j] = mean over cluster-i's
 *                            spikes of the posterior under cluster j (0 if either
 *                            cluster is ignored).
 * Only CUDA implements this; other backends return non-zero (host fallback).
 */
int cuda_compute_error_matrix(
    const double* features, const double* choleskyAll, const double* means,
    const double* logTerms, const int* ignoreFlags,
    const int* featRow, const int* first, const int* nb,
    double* errOut,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col);

int hip_compute_probabilities(
    const double* features,
    const double* choleskyAll,
    const double* means,
    const double* logTerms,
    double*       probOut,
    const int*    ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col);

int sycl_compute_probabilities(
    const double* features,
    const double* choleskyAll,
    const double* means,
    const double* logTerms,
    double*       probOut,
    const int*    ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col);

#ifdef __cplusplus
}
#endif
