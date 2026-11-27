/***************************************************************************
 * corecut_gpu_dispatch.cpp
 *
 * Runtime dispatcher for the core cut's ordering loop: CUDA -> HIP -> SYCL,
 * the first backend compiled in that reports a usable device
 * (corecut_cuda.cu, corecut_hip.hip, corecut_sycl.cpp).  Metal builds, and a
 * machine with no device, run the OpenMP loop in corecut.h.
 ***************************************************************************/

#include "corecut_gpu.h"
#include "corecut_dispatch.h"

#include <cstdio>

// Stubs for the backends not compiled in.
#ifndef USE_CUDA
extern "C" {
    int cuda_corecut_available() { return 0; }
    int cuda_corecut_order(long, int, const float*, long, long*, double*) { return -1; }
}
#endif
#ifndef USE_HIP
extern "C" {
    int hip_corecut_available() { return 0; }
    int hip_corecut_order(long, int, const float*, long, long*, double*) { return -1; }
}
#endif
#ifndef USE_SYCL
extern "C" {
    int sycl_corecut_available() { return 0; }
    int sycl_corecut_order(long, int, const float*, long, long*, double*) { return -1; }
}
#endif

namespace CoreCutGpu {

// One kernel launch and one small copy back per spike taken: below a few
// thousand spikes the CPU loop finishes in well under a second.
static constexpr long kGpuThreshold = 4000;

enum class Backend { None, CUDA, HIP, SYCL };

static Backend g_backend  = Backend::None;
static bool    g_detected = false;

static Backend activeBackend()
{
    if (!g_detected) {
        g_detected = true;
        if (cuda_corecut_available())      { g_backend = Backend::CUDA; fprintf(stdout, "[klusters] core-cut GPU backend: CUDA\n"); }
        else if (hip_corecut_available())  { g_backend = Backend::HIP;  fprintf(stdout, "[klusters] core-cut GPU backend: HIP\n"); }
        else if (sycl_corecut_available()) { g_backend = Backend::SYCL; fprintf(stdout, "[klusters] core-cut GPU backend: SYCL\n"); }
    }
    return g_backend;
}

bool hasGpu() { return activeBackend() != Backend::None; }

long gpuThreshold() { return kGpuThreshold; }

bool order(long n, int d, const float* x, long seed, long* order, double* step)
{
    if (n < kGpuThreshold || !hasGpu()) return false;
    int rc = -1;
    switch (activeBackend()) {
    case Backend::CUDA: rc = cuda_corecut_order(n, d, x, seed, order, step); break;
    case Backend::HIP:  rc = hip_corecut_order(n, d, x, seed, order, step);  break;
    case Backend::SYCL: rc = sycl_corecut_order(n, d, x, seed, order, step); break;
    default: return false;
    }
    if (rc != 0) fprintf(stderr, "[klusters] core-cut GPU order failed (%d): CPU loop\n", rc);
    return rc == 0;
}

} // namespace CoreCutGpu
