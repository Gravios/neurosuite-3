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
 * Consumers:
 *   KK.cpp (KlustaKwik)          — Phase 1.5 batch realignment after chunked CEM
 *   klustersdoc.cpp (Klusters)   — interactive per-cluster realignment in the GUI
 ***************************************************************************/

#include "realign_xcorr.h"

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

static Backend s_backend = Backend::OMP;
static bool    s_detected = false;

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

static Backend activeBackend()
{
    if (!s_detected) {
        s_backend  = detectBackend();
        s_detected = true;
    }
    return s_backend;
}

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
 * On GPU failure the call is automatically retried on the OpenMP path.
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
        rc = xcorr_cuda_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                                maxShift, minScore, shifts_out, scores_out);
        if (rc == 0) return 0;
        fprintf(stderr, "[realign] CUDA xcorr failed (rc=%d), falling back to OMP\n", rc);
        // Permanently demote to OMP so we don't re-probe a failing GPU each call.
        s_backend = Backend::OMP;
        break;
    case Backend::HIP:
        rc = xcorr_hip_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                               maxShift, minScore, shifts_out, scores_out);
        if (rc == 0) return 0;
        fprintf(stderr, "[realign] HIP xcorr failed (rc=%d), falling back to OMP\n", rc);
        // Permanently demote to OMP so we don't re-probe a failing GPU each call.
        s_backend = Backend::OMP;
        break;
    case Backend::SYCL:
        rc = xcorr_sycl_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                                maxShift, minScore, shifts_out, scores_out);
        if (rc == 0) return 0;
        fprintf(stderr, "[realign] SYCL xcorr failed (rc=%d), falling back to OMP\n", rc);
        // Permanently demote to OMP so we don't attempt SYCL again this session.
        s_backend  = Backend::OMP;
        break;
    default:
        break;
    }
    // Always-available OMP fallback
    return xcorr_omp_compute(waveforms, tmpl, nSpikes, nChannels, nSamples,
                             maxShift, minScore, shifts_out, scores_out);
}

} // namespace XcorrDispatch
