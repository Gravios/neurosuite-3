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
