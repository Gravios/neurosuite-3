/***************************************************************************
 * pca_refine_gpu_dispatch.cpp
 *
 * Runtime dispatcher for the PCA-projection-energy refine pass.  Probes
 * CUDA → HIP → SYCL in order and calls the first backend that reports a
 * usable device.  Returns failure if no backend is compiled in or no GPU
 * is available; KlustersDoc::realignSpikes then falls back to the
 * existing CPU per-candidate loop.
 *
 * Which backends are compiled in is controlled by USE_CUDA / USE_HIP /
 * USE_SYCL.  Stubs below cover the not-compiled-in case.
 ***************************************************************************/

#include "pca_refine_gpu.h"
#include "pca_refine_dispatch.h"

#include <cstdio>

// ---------------------------------------------------------------------------
// Stub implementations for backends that were not compiled in.
// ---------------------------------------------------------------------------

#ifndef USE_CUDA
extern "C" {
    int cuda_pca_refine_available() { return 0; }
    int cuda_pca_refine(int,int,int,int,int,int,int,int,int,int,int,int,
                        const int16_t*,const float*,const float*,int*) { return -1; }
}
#endif

#ifndef USE_HIP
extern "C" {
    int hip_pca_refine_available() { return 0; }
    int hip_pca_refine(int,int,int,int,int,int,int,int,int,int,int,int,
                       const int16_t*,const float*,const float*,int*) { return -1; }
}
#endif

#ifndef USE_SYCL
extern "C" {
    int sycl_pca_refine_available() { return 0; }
    int sycl_pca_refine(int,int,int,int,int,int,int,int,int,int,int,int,
                        const int16_t*,const float*,const float*,int*) { return -1; }
}
#endif

// ---------------------------------------------------------------------------
// Public C++ dispatcher
// ---------------------------------------------------------------------------

namespace PcaRefineGpu {

enum class Backend { None, CUDA, HIP, SYCL };

// Below this K (spike count) the wide-window read + GPU launch overhead
// dominates over the per-spike compute saved.  A typical small cluster
// (~50 spikes) runs in under a millisecond on the CPU; the GPU launch
// alone is ~100 µs, so the threshold is conservative.
static constexpr int kGpuThreshold = 128;

static Backend detectBackend()
{
#ifdef USE_CUDA
    if (cuda_pca_refine_available()) {
        fprintf(stdout, "[klusters] pca-refine GPU backend: CUDA\n");
        return Backend::CUDA;
    }
#endif
#ifdef USE_HIP
    if (hip_pca_refine_available()) {
        fprintf(stdout, "[klusters] pca-refine GPU backend: HIP\n");
        return Backend::HIP;
    }
#endif
#ifdef USE_SYCL
    if (sycl_pca_refine_available()) {
        fprintf(stdout, "[klusters] pca-refine GPU backend: SYCL\n");
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

bool hasGpu()        { return activeBackend() != Backend::None; }
int  gpuThreshold() { return kGpuThreshold; }

int refine(int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
           int kComp, int d2u, int rShift, int maxShift,
           int centered, int useStder,
           const int16_t* rawWindowsCM,
           const float*   pcaEvec,
           const float*   pcaMeans,
           int*           bestShifts)
{
    if (K < kGpuThreshold) return 1;            // intentional CPU path

    switch (activeBackend()) {
    case Backend::CUDA:
        return cuda_pca_refine(K, M, wideLen, nSamp, nChan, chForPca,
                               kComp, d2u, rShift, maxShift, centered, useStder,
                               rawWindowsCM, pcaEvec, pcaMeans, bestShifts);
    case Backend::HIP:
        return hip_pca_refine(K, M, wideLen, nSamp, nChan, chForPca,
                              kComp, d2u, rShift, maxShift, centered, useStder,
                              rawWindowsCM, pcaEvec, pcaMeans, bestShifts);
    case Backend::SYCL:
        return sycl_pca_refine(K, M, wideLen, nSamp, nChan, chForPca,
                               kComp, d2u, rShift, maxShift, centered, useStder,
                               rawWindowsCM, pcaEvec, pcaMeans, bestShifts);
    default:
        return -1;
    }
}

} // namespace PcaRefineGpu
