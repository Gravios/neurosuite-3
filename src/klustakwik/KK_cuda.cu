// KK_cuda.cu — GPU acceleration for KlustaKwik hot loops
//
// Accelerated kernels:
//   kk_estep_kernel   — E-step: Mahalanobis distance for all (point, cluster) pairs
//   kk_mstep_mean_kernel  — M-step phase 1: accumulate means
//   kk_mstep_cov_kernel   — M-step phase 2: accumulate covariance outer products
//   kk_cstep_kernel   — C-step: argmin of LogP per point
//   kk_deletion_loss_kernel — ConsiderDeletion: accumulate DeletionLoss per cluster
//
// DATA LAYOUT (GPU-optimised, transposed from CPU layout):
//   d_Data[d * nPoints + p]              — dim-major (coalesced reads in EStep)
//   d_LogP[c * nPoints + p]             — cluster-major (coalesced writes in EStep)
//   d_Mean[c * nDims + d]               — cluster-major (fits in shared mem per block)
//   d_Chol[c * nDims2 + i*nDims + j]    — lower-triangular Cholesky per cluster
//   d_Class[p], d_Class2[p], d_OldClass[p]
//   d_Weight[c]
//
// HOST API (called from KK.cpp when CUDA path selected):
//   void cuda_estep(...)
//   void cuda_mstep(...)
//   void cuda_cstep(...)
//   void cuda_deletion_loss(...)

#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>

// ---- Error checking macro --------------------------------------------------
#define CUDA_CHECK(call) do { \
    cudaError_t _e = (call); \
    if (_e != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d — %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(_e)); \
        std::abort(); \
    } \
} while(0)

// Maximum dimensions supported (matches KlustaKwik default MaxPossibleClusters)
// nDims is typically 12–17; we allocate shared mem dynamically.
#define MAX_DIMS     64
#define MAX_CLUSTERS 128

// ===========================================================================
// E-STEP KERNEL
//
// One thread per data point p.
// For each alive cluster c, computes:
//   Vec2Mean[d] = Data[d*nP + p] - Mean[c*nD + d]
//   Root = TriSolve(Chol[c], Vec2Mean)
//   Mahal = ||Root||^2
//   LogP[c*nP + p] = Mahal/2 + LogRootDet[c] - log(Weight[c]) + log(2π)*nD/2
//
// The Cholesky matrix for each cluster (~289 floats ≈ 1.1 KB) and the mean
// (~17 floats) are loaded into shared memory and broadcast to all threads
// in the block, eliminating repeated global memory reads.
//
// Skip heuristic: if the point didn't change class last step AND its current
// LogP for this cluster is already far from its best, skip recomputation.
// This mirrors the original DistThresh optimisation.
// ===========================================================================
__global__ void kk_estep_kernel(
    const float * __restrict__ d_Data,      // [nDims * nPoints]  dim-major
    const float * __restrict__ d_Mean,      // [MaxClusters * nDims]
    const float * __restrict__ d_Chol,      // [MaxClusters * nDims * nDims]
    const float * __restrict__ d_LogRootDet,// [MaxClusters]
    const float * __restrict__ d_Weight,    // [MaxClusters]
    const int   * __restrict__ d_AliveIndex,// [nClustersAlive]
    const int   * __restrict__ d_Class,     // [nPoints]
    const int   * __restrict__ d_OldClass,  // [nPoints]
          float * __restrict__ d_LogP,      // [MaxClusters * nPoints]  cluster-major
    int nPoints, int nDims, int nDims2,
    int nClustersAlive, int MaxClusters,
    float DistThresh, int FullStep,
    float log2piHalf   // log(2*PI) * nDims / 2, pre-computed on host
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;

    // Cluster 0: uniform noise — LogP[0*nPoints + p] = -log(Weight[0])
    // (handled separately by host before calling this kernel for c>=1)

    float vec[MAX_DIMS];   // Vec2Mean — register file
    float root[MAX_DIMS];  // TriSolve result

    // Shared memory: Cholesky matrix + mean for one cluster at a time
    // Loaded by all threads in the block collaboratively.
    extern __shared__ float smem[];
    float *s_chol = smem;                  // nDims2 floats
    float *s_mean = smem + nDims2;         // nDims floats

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = d_AliveIndex[cc];

        // --- Load Cholesky + mean into shared memory ---
        const float *g_chol = d_Chol + c * nDims2;
        const float *g_mean = d_Mean + c * nDims;
        for (int i = threadIdx.x; i < nDims2; i += blockDim.x) s_chol[i] = g_chol[i];
        for (int i = threadIdx.x; i < nDims;  i += blockDim.x) s_mean[i] = g_mean[i];
        __syncthreads();

        // --- Skip heuristic ---
        if (!FullStep) {
            const float *lp = d_LogP + c * nPoints;
            const float *lp_best = d_LogP + (int)d_Class[p] * nPoints;
            if (d_Class[p] == d_OldClass[p] &&
                lp[p] - lp_best[p] > DistThresh) {
                __syncthreads();
                continue;
            }
        }

        // --- Vec2Mean ---
        for (int d = 0; d < nDims; d++)
            vec[d] = d_Data[d * nPoints + p] - s_mean[d];

        // --- TriSolve: s_chol * root = vec (lower triangular) ---
        for (int i = 0; i < nDims; i++) {
            float s = vec[i];
            for (int j = 0; j < i; j++) s -= s_chol[i * nDims + j] * root[j];
            root[i] = s / s_chol[i * nDims + i];
        }

        // --- Mahalanobis distance ---
        float mahal = 0.0f;
        for (int i = 0; i < nDims; i++) mahal += root[i] * root[i];

        // --- Write LogP (cluster-major layout: coalesced write) ---
        d_LogP[c * nPoints + p] = mahal * 0.5f
                                 + d_LogRootDet[c]
                                 - logf(d_Weight[c])
                                 + log2piHalf;

        __syncthreads();  // protect shared mem before next cluster loads
    }
}

// ===========================================================================
// M-STEP MEAN KERNEL
//
// Phase 1: each thread handles one point, atomically accumulates into
// per-cluster mean sum.  d_MeanAcc[c*nDims+d] is the accumulator.
// Phase 2 (normalise) is done on the host after atomics settle — it's
// O(nClusters * nDims) which is tiny.
//
// With nPoints~100k-1M and nDims~12-17 this is ~1.7M–17M atomic adds.
// On Blackwell (RTX 5070 Ti) atomicAdd on global float is ~2ns → ~4ms.
// ===========================================================================
__global__ void kk_mstep_mean_kernel(
    const float * __restrict__ d_Data,    // [nDims * nPoints]  dim-major
    const int   * __restrict__ d_Class,   // [nPoints]
          float * __restrict__ d_MeanAcc, // [MaxClusters * nDims]  zeroed before call
          int   * __restrict__ d_nMembers,// [MaxClusters]          zeroed before call
    int nPoints, int nDims
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;

    const int c = d_Class[p];
    atomicAdd(&d_nMembers[c], 1);
    for (int d = 0; d < nDims; d++)
        atomicAdd(&d_MeanAcc[c * nDims + d], d_Data[d * nPoints + p]);
}

// ===========================================================================
// M-STEP COVARIANCE KERNEL
//
// Each thread handles one point.  Accumulates the upper triangle of the
// outer product (Data[p] - Mean[c]) * (Data[p] - Mean[c])^T into d_CovAcc.
// nDims*(nDims+1)/2 ≈ 153 elements for nDims=17 — each is one atomicAdd.
// ===========================================================================
__global__ void kk_mstep_cov_kernel(
    const float * __restrict__ d_Data,    // [nDims * nPoints]  dim-major
    const float * __restrict__ d_Mean,    // [MaxClusters * nDims]
    const int   * __restrict__ d_Class,   // [nPoints]
          float * __restrict__ d_CovAcc,  // [MaxClusters * nDims * nDims]  zeroed
    int nPoints, int nDims, int nDims2
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;

    const int c = d_Class[p];
    float v[MAX_DIMS];
    for (int d = 0; d < nDims; d++)
        v[d] = d_Data[d * nPoints + p] - d_Mean[c * nDims + d];

    // Upper triangle only (i <= j)
    for (int i = 0; i < nDims; i++)
        for (int j = i; j < nDims; j++)
            atomicAdd(&d_CovAcc[c * nDims2 + i * nDims + j], v[i] * v[j]);
}

// ===========================================================================
// C-STEP KERNEL
//
// One thread per point.  Scans LogP[c * nPoints + p] for all alive clusters,
// finds argmin (best class) and second argmin.
// Non-coalesced access pattern — each thread strides by nPoints across
// clusters.  With nClusters~10-20 and nPoints~100k, this is fast regardless.
// ===========================================================================
__global__ void kk_cstep_kernel(
    const float * __restrict__ d_LogP,      // [MaxClusters * nPoints]  cluster-major
    const int   * __restrict__ d_AliveIndex,// [nClustersAlive]
          int   * __restrict__ d_Class,     // [nPoints]  out
          int   * __restrict__ d_OldClass,  // [nPoints]  out (copy of Class before update)
          int   * __restrict__ d_Class2,    // [nPoints]  out
    int nPoints, int nClustersAlive, int MaxClusters,
    float HugeScore
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;

    d_OldClass[p] = d_Class[p];

    float bestScore   = HugeScore, secondScore = HugeScore;
    int   topClass    = 0,         secondClass = 0;

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int   c = d_AliveIndex[cc];
        const float s = d_LogP[c * nPoints + p];
        if (s < bestScore) {
            secondClass = topClass;   secondScore = bestScore;
            topClass    = c;          bestScore   = s;
        } else if (s < secondScore) {
            secondClass = c;          secondScore = s;
        }
    }
    d_Class[p]  = topClass;
    d_Class2[p] = secondClass;
}

// ===========================================================================
// DELETION LOSS KERNEL (for ConsiderDeletion)
//
// Each thread handles one point p.
// loss[Class[p]] += LogP[Class2[p]*nP+p] - LogP[Class[p]*nP+p]
// ===========================================================================
__global__ void kk_deletion_loss_kernel(
    const float * __restrict__ d_LogP,   // [MaxClusters * nPoints]
    const int   * __restrict__ d_Class,  // [nPoints]
    const int   * __restrict__ d_Class2, // [nPoints]
          float * __restrict__ d_Loss,   // [MaxClusters]  zeroed before call
    int nPoints
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;

    const int   c1 = d_Class[p];
    const int   c2 = d_Class2[p];
    const float delta = d_LogP[c2 * nPoints + p] - d_LogP[c1 * nPoints + p];
    atomicAdd(&d_Loss[c1], delta);
}

// ===========================================================================
// HOST-SIDE GPU CONTEXT
// Holds device pointers and mirrors of host arrays.
// Created once in KK::LoadData() when CUDA is available.
// ===========================================================================
struct KK_GPU {
    // Device pointers
    float *d_Data     = nullptr;   // [nDims * nPoints]  dim-major
    float *d_DataT    = nullptr;   // transpose buffer if needed
    float *d_Mean     = nullptr;   // [MaxClusters * nDims]
    float *d_MeanAcc  = nullptr;   // accumulator
    float *d_Cov      = nullptr;   // [MaxClusters * nDims²]
    float *d_CovAcc   = nullptr;
    float *d_LogP     = nullptr;   // [MaxClusters * nPoints]  cluster-major
    float *d_Weight   = nullptr;
    float *d_Chol     = nullptr;   // [MaxClusters * nDims²]
    float *d_LogRootDet = nullptr; // [MaxClusters]
    float *d_Loss     = nullptr;   // [MaxClusters]
    int   *d_Class    = nullptr;
    int   *d_OldClass = nullptr;
    int   *d_Class2   = nullptr;
    int   *d_AliveIndex = nullptr;
    int   *d_nMembers = nullptr;

    int nPoints = 0, nDims = 0, nDims2 = 0, MaxClusters = 0;
    bool initialised = false;

    void allocate(int nP, int nD, int nD2, int maxC) {
        nPoints = nP; nDims = nD; nDims2 = nD2; MaxClusters = maxC;
        CUDA_CHECK(cudaMalloc(&d_Data,       sizeof(float) * nD  * nP));
        CUDA_CHECK(cudaMalloc(&d_Mean,       sizeof(float) * maxC * nD));
        CUDA_CHECK(cudaMalloc(&d_MeanAcc,    sizeof(float) * maxC * nD));
        CUDA_CHECK(cudaMalloc(&d_Cov,        sizeof(float) * maxC * nD2));
        CUDA_CHECK(cudaMalloc(&d_CovAcc,     sizeof(float) * maxC * nD2));
        CUDA_CHECK(cudaMalloc(&d_LogP,       sizeof(float) * maxC * nP));
        CUDA_CHECK(cudaMalloc(&d_Weight,     sizeof(float) * maxC));
        CUDA_CHECK(cudaMalloc(&d_Chol,       sizeof(float) * maxC * nD2));
        CUDA_CHECK(cudaMalloc(&d_LogRootDet, sizeof(float) * maxC));
        CUDA_CHECK(cudaMalloc(&d_Loss,       sizeof(float) * maxC));
        CUDA_CHECK(cudaMalloc(&d_Class,      sizeof(int)   * nP));
        CUDA_CHECK(cudaMalloc(&d_OldClass,   sizeof(int)   * nP));
        CUDA_CHECK(cudaMalloc(&d_Class2,     sizeof(int)   * nP));
        CUDA_CHECK(cudaMalloc(&d_AliveIndex, sizeof(int)   * maxC));
        CUDA_CHECK(cudaMalloc(&d_nMembers,   sizeof(int)   * maxC));
        initialised = true;
    }

    void free_all() {
        if (!initialised) return;
        cudaFree(d_Data);   cudaFree(d_Mean);     cudaFree(d_MeanAcc);
        cudaFree(d_Cov);    cudaFree(d_CovAcc);   cudaFree(d_LogP);
        cudaFree(d_Weight); cudaFree(d_Chol);     cudaFree(d_LogRootDet);
        cudaFree(d_Loss);   cudaFree(d_Class);    cudaFree(d_OldClass);
        cudaFree(d_Class2); cudaFree(d_AliveIndex); cudaFree(d_nMembers);
        initialised = false;
    }
};

// ===========================================================================
// Host-callable wrappers — called from KK.cpp CUDA path
// ===========================================================================

// Called once after LoadData to upload transposed Data to device
extern "C" void cuda_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor) {
    // Transpose from point-major [p*nD+d] to dim-major [d*nP+p]
    // (could use cuBLAS Sgeam for large datasets)
    const int nP = gpu->nPoints, nD = gpu->nDims;
    std::vector<float> tmp(nP * nD);
    for (int p = 0; p < nP; p++)
        for (int d = 0; d < nD; d++)
            tmp[d * nP + p] = h_Data_pointMajor[p * nD + d];
    CUDA_CHECK(cudaMemcpy(gpu->d_Data, tmp.data(), sizeof(float) * nP * nD, cudaMemcpyHostToDevice));
}

extern "C" void cuda_estep(
    KK_GPU *gpu,
    const float *h_Mean,      // host, cluster-major
    const float *h_Weight,
    const float *h_Chol,      // host, lower-triangular per cluster
    const int   *h_AliveIndex,
    const int   *h_Class,
    const int   *h_OldClass,
          float *h_LogP,      // host output, will be transposed back
    int nClustersAlive, float DistThresh, int FullStep, double PI,
    int MaxClusters
) {
    const int nP = gpu->nPoints, nD = gpu->nDims, nD2 = gpu->nDims2;

    // Upload per-step data
    CUDA_CHECK(cudaMemcpy(gpu->d_Mean,       h_Mean,       sizeof(float)*MaxClusters*nD,  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Weight,     h_Weight,     sizeof(float)*MaxClusters,     cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Chol,       h_Chol,       sizeof(float)*MaxClusters*nD2, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_AliveIndex, h_AliveIndex, sizeof(int)*nClustersAlive,    cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Class,      h_Class,      sizeof(int)*nP,                cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_OldClass,   h_OldClass,   sizeof(int)*nP,                cudaMemcpyHostToDevice));

    // Compute LogRootDet per cluster on host (sequential, tiny)
    std::vector<float> logRootDet(MaxClusters, 0.0f);
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        for (int i = 0; i < nD; i++)
            logRootDet[c] += std::log(h_Chol[c * nD2 + i * nD + i]);
    }
    CUDA_CHECK(cudaMemcpy(gpu->d_LogRootDet, logRootDet.data(), sizeof(float)*MaxClusters, cudaMemcpyHostToDevice));

    // Cluster 0: uniform noise — fill LogP[0*nP .. nP-1] = -log(Weight[0])
    // cudaMemset only works for byte patterns; use a host-side vector fill + H2D copy.
    {
        const float negLogW0 = -std::log(h_Weight[0]);
        std::vector<float> noise_row(nP, negLogW0);
        CUDA_CHECK(cudaMemcpy(gpu->d_LogP, noise_row.data(), sizeof(float)*nP, cudaMemcpyHostToDevice));
    }

    // Launch EStep kernel
    const int block = 256;
    const int grid  = (nP + block - 1) / block;
    const int smem  = sizeof(float) * (nD2 + nD);
    const float log2piHalf = static_cast<float>(std::log(2.0 * PI) * nD * 0.5);

    kk_estep_kernel<<<grid, block, smem>>>(
        gpu->d_Data, gpu->d_Mean, gpu->d_Chol, gpu->d_LogRootDet, gpu->d_Weight,
        gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass, gpu->d_LogP,
        nP, nD, nD2, nClustersAlive, MaxClusters, DistThresh, FullStep, log2piHalf);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    // Download LogP: device is cluster-major [c*nP+p], host expects point-major [p*maxC+c]
    std::vector<float> tmp(MaxClusters * nP);
    CUDA_CHECK(cudaMemcpy(tmp.data(), gpu->d_LogP, sizeof(float)*MaxClusters*nP, cudaMemcpyDeviceToHost));
    for (int c = 0; c < MaxClusters; c++)
        for (int p = 0; p < nP; p++)
            h_LogP[p * MaxClusters + c] = tmp[c * nP + p];
}

extern "C" void cuda_mstep(
    KK_GPU *gpu,
    const int   *h_Class,
    const int   *h_AliveIndex,
          float *h_Mean,     // out — host, cluster-major
          float *h_Cov,      // out — host
          float *h_Weight,   // out — host
    int nClustersAlive, int MaxClusters, int nPoints, int nDims, int nDims2
) {
    const int nP = nPoints, nD = nDims;

    CUDA_CHECK(cudaMemcpy(gpu->d_Class, h_Class, sizeof(int)*nP, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(gpu->d_MeanAcc, 0, sizeof(float)*MaxClusters*nD));
    CUDA_CHECK(cudaMemset(gpu->d_nMembers, 0, sizeof(int)*MaxClusters));

    const int block = 256, grid = (nP + block - 1) / block;
    kk_mstep_mean_kernel<<<grid, block>>>(
        gpu->d_Data, gpu->d_Class, gpu->d_MeanAcc, gpu->d_nMembers, nP, nD);
    CUDA_CHECK(cudaGetLastError());

    // Download accumulators; normalise on host (trivial)
    std::vector<float> meanAcc(MaxClusters * nD);
    std::vector<int>   nMem(MaxClusters);
    CUDA_CHECK(cudaMemcpy(meanAcc.data(), gpu->d_MeanAcc, sizeof(float)*MaxClusters*nD, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(nMem.data(),   gpu->d_nMembers, sizeof(int)*MaxClusters,      cudaMemcpyDeviceToHost));

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        if (nMem[c] == 0) continue;
        for (int d = 0; d < nD; d++)
            h_Mean[c * nD + d] = meanAcc[c * nD + d] / nMem[c];
    }

    // Covariance accumulation
    CUDA_CHECK(cudaMemcpy(gpu->d_Mean, h_Mean, sizeof(float)*MaxClusters*nD, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(gpu->d_CovAcc, 0, sizeof(float)*MaxClusters*nDims2));
    kk_mstep_cov_kernel<<<grid, block>>>(
        gpu->d_Data, gpu->d_Mean, gpu->d_Class, gpu->d_CovAcc, nP, nD, nDims2);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<float> covAcc(MaxClusters * nDims2);
    CUDA_CHECK(cudaMemcpy(covAcc.data(), gpu->d_CovAcc, sizeof(float)*MaxClusters*nDims2, cudaMemcpyDeviceToHost));
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        if (nMem[c] <= 1) continue;
        const float inv = 1.0f / (nMem[c] - 1);
        for (int i = 0; i < nD; i++)
            for (int j = i; j < nD; j++)
                h_Cov[c * nDims2 + i * nD + j] = covAcc[c * nDims2 + i * nD + j] * inv;
    }
}

extern "C" void cuda_cstep(
    KK_GPU *gpu,
    const float *h_LogP,
    const int   *h_AliveIndex,
          int   *h_Class,
          int   *h_OldClass,
          int   *h_Class2,
    int nPoints, int nClustersAlive, int MaxClusters, float HugeScore
) {
    const int nP = nPoints;
    // Upload cluster-major LogP
    std::vector<float> tmp(MaxClusters * nP);
    for (int c = 0; c < MaxClusters; c++)
        for (int p = 0; p < nP; p++)
            tmp[c * nP + p] = h_LogP[p * MaxClusters + c];
    CUDA_CHECK(cudaMemcpy(gpu->d_LogP,      tmp.data(),   sizeof(float)*MaxClusters*nP, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_AliveIndex,h_AliveIndex, sizeof(int)*nClustersAlive,   cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Class,     h_Class,      sizeof(int)*nP,               cudaMemcpyHostToDevice));

    const int block = 256, grid = (nP + block - 1) / block;
    kk_cstep_kernel<<<grid, block>>>(
        gpu->d_LogP, gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass, gpu->d_Class2,
        nP, nClustersAlive, MaxClusters, HugeScore);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(h_Class,    gpu->d_Class,    sizeof(int)*nP, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_OldClass, gpu->d_OldClass, sizeof(int)*nP, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_Class2,   gpu->d_Class2,   sizeof(int)*nP, cudaMemcpyDeviceToHost));
}

extern "C" void cuda_deletion_loss(
    KK_GPU *gpu,
    const float *h_LogP,
    const int   *h_Class,
    const int   *h_Class2,
          float *h_Loss,
    int nPoints, int MaxClusters
) {
    const int nP = nPoints;
    std::vector<float> tmp(MaxClusters * nP);
    for (int c = 0; c < MaxClusters; c++)
        for (int p = 0; p < nP; p++)
            tmp[c * nP + p] = h_LogP[p * MaxClusters + c];
    CUDA_CHECK(cudaMemcpy(gpu->d_LogP,  tmp.data(),  sizeof(float)*MaxClusters*nP, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Class, h_Class,     sizeof(int)*nP,               cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Class2,h_Class2,    sizeof(int)*nP,               cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(gpu->d_Loss,  0,           sizeof(float)*MaxClusters));

    const int block = 256, grid = (nP + block - 1) / block;
    kk_deletion_loss_kernel<<<grid, block>>>(
        gpu->d_LogP, gpu->d_Class, gpu->d_Class2, gpu->d_Loss, nP);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(h_Loss, gpu->d_Loss, sizeof(float)*MaxClusters, cudaMemcpyDeviceToHost));
}
