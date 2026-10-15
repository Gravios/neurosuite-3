/***************************************************************************
 * realign_xcorr_dispatch.cpp
 *
 * Runtime dispatcher for the normalised cross-correlation spike-alignment
 * kernel.  Probes CUDA → HIP → SYCL → OpenMP in order and routes each
 * call to the first backend that reports a usable device.
 *
 * Enabled backends are selected at compile time by CMake preprocessor
 * defines: USE_CUDA, USE_HIP, USE_SYCL.  Any combination is valid.
 * The OpenMP CPU path is always compiled and is the final fallback.
 *
 * Public API
 * ──────────
 *   XcorrDispatch::compute(...)  — run the alignment kernel
 *   XcorrDispatch::backendName() — human-readable active backend label
 *
 * Consumers (each compiles these sources itself; they are not in the library):
 *   KiloKlustaKwik (KK_templatematch.cpp) — calls compute() from inside an
 *                                           OpenMP parallel loop
 *   Klusters (klustersdoc_realign.cpp)     — interactive per-cluster realignment
 *
 * Thread safety: compute() and backendName() may be called concurrently.  The
 * back end is probed once (a function-local static, initialised under the
 * C++ guarantee) and held in an atomic, so a GPU demotion by one thread is
 * seen by the others without a data race.
 ***************************************************************************/

#include "realign_xcorr.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Compile-time stubs for backends that were not compiled in.
// The real symbols come from their respective .cu / .hip / _sycl.cpp files.
// ---------------------------------------------------------------------------

#ifndef USE_CUDA
extern "C" {
    int xcorr_cuda_available() { return 0; }
    int xcorr_cuda_compute(const int16_t*, const int16_t*,
                           int, int, int, int, float,
                           int*, float*) { return -1; }
}
#endif

#ifndef USE_HIP
extern "C" {
    int xcorr_hip_available() { return 0; }
    int xcorr_hip_compute(const int16_t*, const int16_t*,
                          int, int, int, int, float,
                          int*, float*) { return -1; }
}
#endif

#ifndef USE_SYCL
extern "C" {
    int xcorr_sycl_available() { return 0; }
    int xcorr_sycl_compute(const int16_t*, const int16_t*,
                           int, int, int, int, float,
                           int*, float*) { return -1; }
}
#endif

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

namespace XcorrDispatch {

enum class Backend { OMP, CUDA, HIP, SYCL };

static Backend detectBackend()
{
#ifdef USE_CUDA
    if (xcorr_cuda_available()) {
        fprintf(stdout, "[realign] xcorr backend: CUDA\n");
        return Backend::CUDA;
    }
#endif
#ifdef USE_HIP
    if (xcorr_hip_available()) {
        fprintf(stdout, "[realign] xcorr backend: HIP (AMD ROCm)\n");
        return Backend::HIP;
    }
#endif
#ifdef USE_SYCL
    // xcorr_sycl_available() initialises the oneAPI runtime, which on some
    // level-zero driver versions calls std::terminate() on a background thread
    // during JIT compilation.  Don't probe at all unless the user has
    // explicitly opted in with KLUSTERS_USE_SYCL=1.
    if (std::getenv("KLUSTERS_USE_SYCL") && xcorr_sycl_available()) {
        fprintf(stdout, "[realign] xcorr backend: SYCL (Intel oneAPI)\n");
        return Backend::SYCL;
    }
#endif
    fprintf(stdout, "[realign] xcorr backend: OpenMP CPU\n");
    return Backend::OMP;
}

// The active back end.  The initialiser runs detectBackend() exactly once, even
// when the first calls arrive from several threads at the same time; later
// demotions are atomic stores.
static std::atomic<Backend>& backendState()
{
    static std::atomic<Backend> state{detectBackend()};
    return state;
}

static Backend activeBackend()
{
    return backendState().load(std::memory_order_acquire);
}

// Permanently fall back to OpenMP after a GPU failure.  Only the thread whose
// call actually moves the state from `from` reports it, so the message
// appears once however many threads hit the failure together.
static void demote(Backend from, const char* name, int rc)
{
    Backend expected = from;
    if (backendState().compare_exchange_strong(expected, Backend::OMP,
                                               std::memory_order_acq_rel))
        fprintf(stderr, "[realign] %s xcorr failed (rc=%d), falling back to OMP"
                        " for the rest of this process\n", name, rc);
}

// The CUDA and HIP kernels reduce all 2*maxShift+1 lags in one block of at
// most 1024 threads.  Larger shifts are not a device failure: such calls go
// straight to OpenMP and leave the GPU in use for the others.
static bool gpuCanShift(int maxShift) { return 2 * maxShift + 1 <= 1024; }

const char* backendName()
{
    switch (activeBackend()) {
    case Backend::CUDA: return "CUDA";
    case Backend::HIP:  return "HIP (AMD ROCm)";
    case Backend::SYCL: return "SYCL (Intel oneAPI)";
    default:            return "OpenMP CPU";
    }
}

/**
 * Compute optimal realignment shifts for a batch of waveforms against
 * a cluster template, using the best available backend.
 *
 * On GPU failure the call is retried on the OpenMP path and later calls use
 * OpenMP; a maxShift beyond the CUDA/HIP block limit runs that call on OpenMP
 * without demoting.
 */
int compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out)
{
    int rc = -1;
    switch (activeBackend()) {
    case Backend::CUDA:
        if (!gpuCanShift(maxShift)) break;
        rc = xcorr_cuda_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                                maxShift, minScore, shifts_out, scores_out);
        if (rc == 0) return 0;
        demote(Backend::CUDA, "CUDA", rc);
        break;
    case Backend::HIP:
        if (!gpuCanShift(maxShift)) break;
        rc = xcorr_hip_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                               maxShift, minScore, shifts_out, scores_out);
        if (rc == 0) return 0;
        demote(Backend::HIP, "HIP", rc);
        break;
    case Backend::SYCL:
        rc = xcorr_sycl_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                                maxShift, minScore, shifts_out, scores_out);
        if (rc == 0) return 0;
        demote(Backend::SYCL, "SYCL", rc);
        break;
    default:
        break;
    }
    // Always-available OMP fallback
    return xcorr_omp_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                             maxShift, minScore, shifts_out, scores_out);
}

} // namespace XcorrDispatch
