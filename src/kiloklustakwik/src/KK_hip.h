// KK_hip.h — GPU context struct and host-callable wrapper declarations (AMD HIP/ROCm).
//
// Mirrors KK_cuda.h exactly — same struct layout, same wrapper signatures,
// same data layout contracts — so KK.cpp dispatch needs only an
// #elif defined(USE_HIP) branch at each call site.
//
// DESIGN NOTES
// ------------
// HIP is source-level compatible with CUDA: hipMalloc/hipFree/hipMemcpy
// map 1-to-1 onto cudaMalloc/cudaFree/cudaMemcpy, and __global__ kernels
// compile unchanged with hipcc.  The only substantive differences are:
//
//   * hipAtomicAdd(float*)  is a native instruction on GCN4+ (RX 470+) and
//     RDNA (RX 5000+), so the M-step scatter and deletion-loss reduce have
//     the same hardware cost as on NVIDIA Pascal+.
//
//   * LDS (Local Data Share) = HIP __shared__ = CUDA shared memory.
//     GCN/RDNA wavefront width is 64 (vs CUDA warp = 32), so the optimal
//     block size is a multiple of 64.  KK_HIP_BLOCK defaults to 256
//     (4 wavefronts) which is safe on all RDNA/CDNA generations.
//     Override at compile time: -DKK_HIP_BLOCK=512
//
//   * hipLogf() is the fast single-precision log intrinsic (= __logf on CUDA).
//
//   * AMD architecture targets are set via -–offload-arch=gfx<id> in hipcc.
//     gfx1100/1101/1102 = RDNA3 (RX 7000 series)
//     gfx1030/1031/1032 = RDNA2 (RX 6000 series)
//     gfx906/908/90a    = CDNA1/CDNA2 (Instinct MI50/MI100/MI210)
//
// Data layout (identical to CUDA/SYCL backends):
//   d_Data  : dim-major    [d * nPoints + p]
//   d_LogP  : cluster-major [c * nPoints + p]
//   d_Mean  : [c * nDims + d]
//   d_Chol  : [c * nDims2 + i*nDims + j]   lower-triangular
//   scalar arrays: d_Weight, d_Loss, d_LogRootDet [MaxClusters]
//   index arrays:  d_Class, d_OldClass, d_Class2, d_AliveIndex [nPoints / MaxClusters]
#pragma once
#ifdef USE_HIP

#include <hip/hip_runtime.h>

#ifndef KK_HIP_BLOCK
#define KK_HIP_BLOCK 256
#endif

// ---------------------------------------------------------------------------
// HIP error-checking macro
// ---------------------------------------------------------------------------
#define HIP_CHECK(call) do { \
    hipError_t _e = (call); \
    if (_e != hipSuccess) { \
        fprintf(stderr, "HIP error at %s:%d — %s\n", \
                __FILE__, __LINE__, hipGetErrorString(_e)); \
        std::abort(); \
    } \
} while(0)

// ---------------------------------------------------------------------------
// KK_GPU — persistent GPU context (identical member layout to CUDA version)
// ---------------------------------------------------------------------------
struct KK_GPU {
    float *d_Data       = nullptr;
    float *d_LogP       = nullptr;
    float *d_Mean       = nullptr;
    float *d_MeanAcc    = nullptr;
    float *d_Cov        = nullptr;
    float *d_CovAcc     = nullptr;
    float *d_Weight     = nullptr;
    float *d_Chol       = nullptr;
    float *d_LogRootDet = nullptr;
    float *d_Loss       = nullptr;
    int   *d_Class      = nullptr;
    int   *d_OldClass   = nullptr;
    int   *d_Class2     = nullptr;
    int   *d_AliveIndex = nullptr;
    int   *d_nMembers   = nullptr;
    float *d_Score      = nullptr;  // [1]  score reduction accumulator

    int  nPoints = 0, nDims = 0, nDims2 = 0, MaxClusters = 0;
    int  smemLimit = 49152;  // device LDS limit per workgroup (bytes), queried at allocate()
    bool initialised = false;

    void allocate(int nP, int nD, int nD2, int maxC);
    void free_all();
};
    bool initialised = false;

// ---------------------------------------------------------------------------
// Host-callable wrappers (defined in KK_hip.cpp, called from KK.cpp).
// Signatures are identical to the CUDA versions.
// ---------------------------------------------------------------------------
extern "C" {

void hip_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor);

void hip_estep(
    KK_GPU      *gpu,
    const float *h_Mean,
    const float *h_Weight,
    const float *h_Chol,
    const int   *h_AliveIndex,
    const int   *h_Class,
    const int   *h_OldClass,
          float *h_LogP,
    int nClustersAlive, float DistThresh, int FullStep,
    double PI, int MaxClusters);

void hip_mstep(
    KK_GPU    *gpu,
    const int *h_Class,
    const int *h_AliveIndex,
          float *h_Mean,
          float *h_Cov,
          float *h_Weight,
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2);

void hip_cstep(
    KK_GPU    *gpu,
          int *h_Class,
          int *h_OldClass,
          int *h_Class2,
    int nClustersAlive, int MaxClusters, float HugeScore);

void hip_deletion_loss(
    KK_GPU    *gpu,
          float *h_Loss,
    int MaxClusters);

// Score reduction: sum of LogP[Class[p]*nP+p] over all points + penalty.
// LogP stays device-resident; no full LogP download needed.
float hip_compute_score(KK_GPU *gpu, float penalty);

// Full LogP download for DistDump debug mode only.
void hip_download_logp(KK_GPU *gpu, float *h_LogP, int nClustersAlive,
                        const int *h_AliveIndex);

} // extern "C"

// Returns true if at least one AMD GPU with ROCm support is present.
inline bool hip_device_available() {
    int count = 0;
    return (hipGetDeviceCount(&count) == hipSuccess && count > 0);
}

#endif // USE_HIP
