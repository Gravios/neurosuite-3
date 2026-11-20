/***************************************************************************
 * corecut_gpu_dispatch.cpp
 *
 * Runtime dispatcher for the core cut's ordering loop.  CUDA is the one
 * backend with a kernel (corecut_cuda.cu); HIP, SYCL and Metal builds, and a
 * machine with no device, run the OpenMP loop in corecut.h.
 ***************************************************************************/

#include "corecut_gpu.h"
#include "corecut_dispatch.h"

#include <cstdio>

// Stub for a build without the CUDA kernel.
#ifndef USE_CUDA
extern "C" {
    int cuda_corecut_available() { return 0; }
    int cuda_corecut_order(long, int, const float*, long, long*, double*) { return -1; }
}
#endif

namespace CoreCutGpu {

// One kernel launch and one small copy back per spike taken: below a few
// thousand spikes the CPU loop finishes in well under a second.
static constexpr long kGpuThreshold = 4000;

static bool g_available = false;
static bool g_detected  = false;

bool hasGpu()
{
    if (!g_detected) {
        g_available = cuda_corecut_available() != 0;
        g_detected  = true;
        if (g_available) fprintf(stdout, "[klusters] core-cut GPU backend: CUDA\n");
    }
    return g_available;
}

long gpuThreshold() { return kGpuThreshold; }

bool order(long n, int d, const float* x, long seed, long* order, double* step)
{
    if (n < kGpuThreshold || !hasGpu()) return false;
    const int rc = cuda_corecut_order(n, d, x, seed, order, step);
    if (rc != 0) fprintf(stderr, "[klusters] core-cut GPU order failed (%d): CPU loop\n", rc);
    return rc == 0;
}

} // namespace CoreCutGpu
