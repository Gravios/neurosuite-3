// KK_cuda.h — GPU context struct and host-callable wrapper declarations.
// Included by KK.cpp (when compiled with -DUSE_CUDA) and by KK_cuda.cu.
//
// DESIGN NOTES
// ------------
// * KK_GPU is the single authoritative definition of the GPU context.
//   KK_cuda.cu includes this header; it does NOT redefine the struct.
// * Device LogP is always cluster-major [c * nPoints + p] for coalesced
//   writes in the E-step kernel.  The host KK arrays remain point-major
//   [p * MaxPossibleClusters + c].  A single download+transpose happens
//   at the end of cuda_estep; all other functions operate on device memory
//   directly without round-tripping through the host.
// * d_Data is uploaded once after LoadData (dim-major [d*nPoints+p]).
//   It is never re-uploaded during EM iterations.
// * Cholesky decomposition stays on the CPU (O(D^3), D<=25 -- trivial).
//   cuda_estep receives the pre-computed Cholesky factors as host pointers
//   and uploads them as part of the per-step metadata upload.
#pragma once
#ifdef USE_CUDA

#include <cuda_runtime.h>

// ---------------------------------------------------------------------------
// KK_GPU — persistent GPU context, one per KK instance
// ---------------------------------------------------------------------------
struct KK_GPU {
    // --- Device buffers (dim-major or cluster-major as noted) ---
    float *d_Data       = nullptr;  // [nDims  * nPoints]   dim-major
    float *d_LogP       = nullptr;  // [MaxClusters * nPoints]  cluster-major
    float *d_Mean       = nullptr;  // [MaxClusters * nDims]
    float *d_MeanAcc    = nullptr;  // [MaxClusters * nDims]   accumulator
    float *d_Cov        = nullptr;  // [MaxClusters * nDims2]
    float *d_CovAcc     = nullptr;  // [MaxClusters * nDims2]  accumulator
    float *d_Weight     = nullptr;  // [MaxClusters]
    float *d_Chol       = nullptr;  // [MaxClusters * nDims2]
    float *d_LogRootDet = nullptr;  // [MaxClusters]
    float *d_Loss       = nullptr;  // [MaxClusters]
    int   *d_Class      = nullptr;  // [nPoints]
    int   *d_OldClass   = nullptr;  // [nPoints]
    int   *d_Class2     = nullptr;  // [nPoints]
    int   *d_AliveIndex = nullptr;  // [MaxClusters]
    int   *d_nMembers   = nullptr;  // [MaxClusters]
    float *d_Score      = nullptr;  // [1]  score reduction accumulator

    int  nPoints = 0, nDims = 0, nDims2 = 0, MaxClusters = 0;
    bool initialised = false;

    void allocate(int nP, int nD, int nD2, int maxC);
    void free_all();
};

// ---------------------------------------------------------------------------
// Host-callable wrappers (defined in KK_cuda.cu, called from KK.cpp)
// ---------------------------------------------------------------------------
extern "C" {

// Upload transposed Data once after LoadData.
// h_Data_pointMajor: host array [nPoints * nDims] point-major.
void cuda_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor);

// E-step: compute LogP on GPU for all alive clusters c>=1.
// Cholesky decomposition is done on the host before calling this.
// h_Chol: host array [MaxClusters * nDims2], lower-triangular per cluster.
// Downloads result and transposes back to h_LogP (point-major).
void cuda_estep(
    KK_GPU      *gpu,
    const float *h_Mean,        // [MaxClusters * nDims]
    const float *h_Weight,      // [MaxClusters]
    const float *h_Chol,        // [MaxClusters * nDims2]
    const int   *h_AliveIndex,  // [nClustersAlive]
    const int   *h_Class,       // [nPoints]
    const int   *h_OldClass,    // [nPoints]
          float *h_LogP,        // [nPoints * MaxClusters] out (point-major)
    int nClustersAlive, float DistThresh, int FullStep,
    double PI, int MaxClusters);

// M-step: accumulate means and covariances on GPU, normalise on host.
// h_Mean, h_Cov, h_Weight are output arrays (updated in-place).
void cuda_mstep(
    KK_GPU    *gpu,
    const int *h_Class,         // [nPoints]
    const int *h_AliveIndex,    // [nClustersAlive]
          float *h_Mean,        // [MaxClusters * nDims]  out
          float *h_Cov,         // [MaxClusters * nDims2] out
          float *h_Weight,      // [MaxClusters] out
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2);

// C-step: argmin of LogP per point, entirely on GPU.
// Operates on device d_LogP already uploaded by cuda_estep.
// Downloads updated Class, OldClass, Class2 to host.
void cuda_cstep(
    KK_GPU    *gpu,
          int *h_Class,         // [nPoints]  in/out
          int *h_OldClass,      // [nPoints]  out
          int *h_Class2,        // [nPoints]  out
    int nClustersAlive, int MaxClusters, float HugeScore);

// ConsiderDeletion loss accumulation: one GPU pass instead of O(N) CPU loop.
// Operates on device d_LogP, d_Class, d_Class2 already on GPU.
// Downloads per-cluster loss into h_Loss[MaxClusters].
void cuda_deletion_loss(
    KK_GPU    *gpu,
          float *h_Loss,        // [MaxClusters]  out
    int MaxClusters);

// Score reduction: sum of LogP[Class[p]*nP+p] over all points + penalty.
// LogP stays device-resident; no full LogP download needed.
float cuda_compute_score(KK_GPU *gpu, float penalty);

// Full LogP download for DistDump debug mode only.
// Transposes device cluster-major [c*nP+p] to host point-major [p*maxC+c].
void cuda_download_logp(KK_GPU *gpu, float *h_LogP, int nClustersAlive,
                         const int *h_AliveIndex);

} // extern "C"

// Returns true if at least one CUDA-capable device is present.
inline bool cuda_device_available() {
    int count = 0;
    return (cudaGetDeviceCount(&count) == cudaSuccess && count > 0);
}

#endif // USE_CUDA
