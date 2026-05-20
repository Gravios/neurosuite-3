/***************************************************************************
 * reorder_similarity_gpu_dispatch.cpp
 *
 * Runtime dispatcher for single-linkage agglomerative clustering on the
 * cluster similarity matrix.  Probes CUDA → HIP → SYCL in order and calls
 * the first backend that reports a usable device.  Returns failure if no
 * backend is compiled in or no GPU is available; the caller then falls
 * back to the CPU loop in klusters.cpp.
 *
 * Which backends are compiled in is controlled by preprocessor defines set
 * by CMake: USE_CUDA, USE_HIP, USE_SYCL.  Any combination is valid; if
 * none are defined all GPU paths degrade to the stubs below that return
 * "not available".
 *
 * A small-N threshold (kGpuThreshold) keeps tiny problems on the CPU
 * where the inline O(N³) loop beats GPU launch + transfer overhead.
 ***************************************************************************/

#include "reorder_similarity_gpu.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Stub implementations for backends that were not compiled in.
// The real implementations live in reorder_similarity_{cuda.cu,hip.hip,sycl.cpp}.
// ---------------------------------------------------------------------------

#ifndef USE_CUDA
extern "C" {
    int cuda_reorder_similarity_available() { return 0; }
    int cuda_reorder_similarity(const double*, int, int*, int*) { return -1; }
}
#endif

#ifndef USE_HIP
extern "C" {
    int hip_reorder_similarity_available() { return 0; }
    int hip_reorder_similarity(const double*, int, int*, int*) { return -1; }
}
#endif

#ifndef USE_SYCL
extern "C" {
    int sycl_reorder_similarity_available() { return 0; }
    int sycl_reorder_similarity(const double*, int, int*, int*) { return -1; }
}
#endif

// ---------------------------------------------------------------------------
// Public C++ dispatcher
// ---------------------------------------------------------------------------

namespace ReorderSimilarityGpu {

enum class Backend { None, CUDA, HIP, SYCL };

// GPU launch + H↔D transfer overhead is ~100 µs at minimum.  For very
// small N the CPU loop finishes in well under that, so we skip the GPU
// path below this threshold.  At N=64 the CPU does ~262K ops in ~0.25 ms,
// so 64 is a conservative floor; the GPU starts to clearly win around
// N≥256.
static constexpr int kGpuThreshold = 64;

/** Detect the best available backend once and cache the result. */
static Backend detectBackend()
{
#ifdef USE_CUDA
    if (cuda_reorder_similarity_available()) {
        fprintf(stdout, "[klusters] reorder-by-similarity GPU backend: CUDA\n");
        return Backend::CUDA;
    }
#endif
#ifdef USE_HIP
    if (hip_reorder_similarity_available()) {
        fprintf(stdout, "[klusters] reorder-by-similarity GPU backend: HIP\n");
        return Backend::HIP;
    }
#endif
#ifdef USE_SYCL
    if (sycl_reorder_similarity_available()) {
        fprintf(stdout, "[klusters] reorder-by-similarity GPU backend: SYCL\n");
        return Backend::SYCL;
    }
#endif
    return Backend::None;
}

static Backend g_backend  = Backend::None;
static bool    g_detected = false;

Backend activeBackend()
{
    if (!g_detected) {
        g_backend  = detectBackend();
        g_detected = true;
    }
    return g_backend;
}

bool hasGpu() { return activeBackend() != Backend::None; }
int  gpuThreshold() { return kGpuThreshold; }

/**
 * Try to compute the merge log on the GPU.  Returns 0 on success, non-zero
 * if no usable backend is available, N is below the threshold, or the
 * chosen backend failed at any stage.  On non-zero the caller must run
 * the CPU loop.
 */
int singleLinkage(const double* S, int N, int* mergeBi_out, int* mergeBj_out)
{
    if (N < kGpuThreshold) return 1;        // intentional CPU path
    switch (activeBackend()) {
    case Backend::CUDA:
        return cuda_reorder_similarity(S, N, mergeBi_out, mergeBj_out);
    case Backend::HIP:
        return hip_reorder_similarity (S, N, mergeBi_out, mergeBj_out);
    case Backend::SYCL:
        return sycl_reorder_similarity(S, N, mergeBi_out, mergeBj_out);
    default:
        return -1;
    }
}

} // namespace ReorderSimilarityGpu
