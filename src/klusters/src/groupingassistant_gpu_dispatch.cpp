/***************************************************************************
 * groupingassistant_gpu_dispatch.cpp
 *
 * Runtime dispatcher: probes CUDA → HIP → SYCL → CPU-OpenMP in order and
 * calls the first backend that reports a usable device.
 *
 * Which backends are compiled in is controlled by preprocessor defines set
 * by CMake: USE_CUDA, USE_HIP, USE_SYCL. Any combination is valid; if none
 * are defined all GPU paths degrade to stubs that return "not available".
 ***************************************************************************/

#include "groupingassistant_gpu.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Stub implementations for backends that were not compiled in.
// The real implementations live in their respective .cu / .hip / _sycl.cpp files.
// ---------------------------------------------------------------------------

#ifndef USE_CUDA
extern "C" {
    int cuda_device_available() { return 0; }
    int cuda_compute_probabilities(const double*, const double*, const double*,
        const double*, double*, const int*, int, int, int, int) { return -1; }
    int cuda_compute_error_matrix(const double*, const double*, const double*,
        const double*, const int*, const int*, const int*, const int*,
        double*, int, int, int, int) { return -1; }
}
#endif

#ifndef USE_HIP
extern "C" {
    int hip_device_available() { return 0; }
    int hip_compute_probabilities(const double*, const double*, const double*,
        const double*, double*, const int*, int, int, int, int) { return -1; }
}
#endif

#ifndef USE_SYCL
extern "C" {
    int sycl_device_available() { return 0; }
    int sycl_compute_probabilities(const double*, const double*, const double*,
        const double*, double*, const int*, int, int, int, int) { return -1; }
}
#endif

// ---------------------------------------------------------------------------
// Public dispatcher — called from groupingassistant.cpp
// ---------------------------------------------------------------------------

namespace GpuDispatch {

enum class Backend { None, CUDA, HIP, SYCL };

/** Detect the best available backend once and cache the result. */
static Backend detectBackend()
{
#ifdef USE_CUDA
    if (cuda_device_available()) {
        fprintf(stdout, "[klusters] GPU backend: CUDA\n");
        return Backend::CUDA;
    }
#endif
#ifdef USE_HIP
    if (hip_device_available()) {
        fprintf(stdout, "[klusters] GPU backend: HIP (AMD ROCm)\n");
        return Backend::HIP;
    }
#endif
#ifdef USE_SYCL
    if (sycl_device_available()) {
        fprintf(stdout, "[klusters] GPU backend: SYCL (Intel oneAPI)\n");
        return Backend::SYCL;
    }
#endif
    fprintf(stdout, "[klusters] GPU backend: none — using OpenMP CPU\n");
    return Backend::None;
}

static Backend g_backend = Backend::None;
static bool    g_detected = false;

Backend activeBackend()
{
    if (!g_detected) {
        g_backend  = detectBackend();
        g_detected = true;
    }
    return g_backend;
}

/**
 * Dispatch the probability computation to the active GPU backend.
 * Returns 0 on success. On failure (returns non-zero) the caller should
 * fall back to the OpenMP CPU path.
 */
int computeProbabilities(
    const double* features,
    const double* choleskyAll,
    const double* means,
    const double* logTerms,
    double*       probOut,
    const int*    ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col)
{
    switch (activeBackend()) {
    case Backend::CUDA:
        return cuda_compute_probabilities(features, choleskyAll, means, logTerms,
                                          probOut, ignoreFlags,
                                          nbSpikes, nbClusters, nbDim, cluster1Col);
    case Backend::HIP:
        return hip_compute_probabilities(features, choleskyAll, means, logTerms,
                                         probOut, ignoreFlags,
                                         nbSpikes, nbClusters, nbDim, cluster1Col);
    case Backend::SYCL:
        return sycl_compute_probabilities(features, choleskyAll, means, logTerms,
                                          probOut, ignoreFlags,
                                          nbSpikes, nbClusters, nbDim, cluster1Col);
    default:
        return -1;  // No GPU — caller uses OpenMP.
    }
}

/**
 * Dispatch the fused posterior + error-matrix aggregation. Only CUDA implements
 * it; on HIP/SYCL/None this returns non-zero and the caller falls back to the
 * host path (full posteriors + OpenMP aggregation).
 */
int computeErrorMatrix(
    const double* features, const double* choleskyAll, const double* means,
    const double* logTerms, const int* ignoreFlags,
    const int* featRow, const int* first, const int* nb,
    double* errOut,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col)
{
    if (activeBackend() == Backend::CUDA)
        return cuda_compute_error_matrix(features, choleskyAll, means, logTerms,
                                         ignoreFlags, featRow, first, nb, errOut,
                                         nbSpikes, nbClusters, nbDim, cluster1Col);
    return -1;  // host aggregation fallback
}

bool hasGpu() { return activeBackend() != Backend::None; }

} // namespace GpuDispatch
