// KK_hip.cpp — AMD ROCm/HIP GPU acceleration for KlustaKwik hot loops.
//
// Kernels (one-to-one with KK_cuda.cu):
//   kk_estep_kernel         — E-step: Mahalanobis LogP for all (point, cluster)
//   kk_mstep_mean_kernel    — M-step: scatter mean accumulation
//   kk_mstep_cov_kernel     — M-step: scatter covariance accumulation
//   kk_cstep_kernel         — C-step: argmin LogP per point
//   kk_deletion_loss_kernel — ConsiderDeletion: per-cluster loss accumulation
//
// HIP vs CUDA differences in this file:
//   cuda_runtime.h        -> hip/hip_runtime.h
//   __logf()              -> hipLogf()           (fast single-precision log)
//   cudaMalloc/Free/cpy   -> hipMalloc/Free/cpy
//   CUDA_CHECK            -> HIP_CHECK
//   cudaMemset            -> hipMemset
//   block/grid dim types  -> identical (__global__, blockDim, threadIdx etc.)
//   atomicAdd(float*)     -> atomicAdd(float*)   (same intrinsic, GCN/RDNA native)
//
// Everything else — kernel logic, data layout, shared memory usage, the
// DistThresh heuristic, the LogP cluster-major/host point-major transpose —
// is byte-for-byte identical to KK_cuda.cu.
//
// RDNA wavefront width is 64 (vs CUDA warp = 32). Block size KK_HIP_BLOCK=256
// means 4 wavefronts per CU, which is a safe occupancy default across all
// supported architectures.  For CDNA2 (MI210/MI250) with 64 CUs you can
// raise this to 512 for better latency hiding.

#ifdef USE_HIP

#include "KK_hip.h"
#include <hip/hip_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

static constexpr int MAX_DIMS = 64;
static constexpr int BLOCK    = KK_HIP_BLOCK;

// ---------------------------------------------------------------------------
// KK_GPU::allocate
// ---------------------------------------------------------------------------
void KK_GPU::allocate(int nP, int nD, int nD2, int maxC) {
    nPoints = nP; nDims = nD; nDims2 = nD2; MaxClusters = maxC;
    HIP_CHECK(hipMalloc(&d_Data,       sizeof(float) * nD   * nP));
    HIP_CHECK(hipMalloc(&d_LogP,       sizeof(float) * maxC * nP));
    HIP_CHECK(hipMalloc(&d_Mean,       sizeof(float) * maxC * nD));
    HIP_CHECK(hipMalloc(&d_MeanAcc,    sizeof(float) * maxC * nD));
    HIP_CHECK(hipMalloc(&d_Cov,        sizeof(float) * maxC * nD2));
    HIP_CHECK(hipMalloc(&d_CovAcc,     sizeof(float) * maxC * nD2));
    HIP_CHECK(hipMalloc(&d_Weight,     sizeof(float) * maxC));
    HIP_CHECK(hipMalloc(&d_Chol,       sizeof(float) * maxC * nD2));
    HIP_CHECK(hipMalloc(&d_LogRootDet, sizeof(float) * maxC));
    HIP_CHECK(hipMalloc(&d_Loss,       sizeof(float) * maxC));
    HIP_CHECK(hipMalloc(&d_Class,      sizeof(int)   * nP));
    HIP_CHECK(hipMalloc(&d_OldClass,   sizeof(int)   * nP));
    HIP_CHECK(hipMalloc(&d_Class2,     sizeof(int)   * nP));
    HIP_CHECK(hipMalloc(&d_AliveIndex, sizeof(int)   * maxC));
    HIP_CHECK(hipMalloc(&d_nMembers,   sizeof(int)   * maxC));
    initialised = true;
}

// ---------------------------------------------------------------------------
// KK_GPU::free_all
// ---------------------------------------------------------------------------
void KK_GPU::free_all() {
    if (!initialised) return;
    HIP_CHECK(hipFree(d_Data));
    HIP_CHECK(hipFree(d_LogP));
    HIP_CHECK(hipFree(d_Mean));
    HIP_CHECK(hipFree(d_MeanAcc));
    HIP_CHECK(hipFree(d_Cov));
    HIP_CHECK(hipFree(d_CovAcc));
    HIP_CHECK(hipFree(d_Weight));
    HIP_CHECK(hipFree(d_Chol));
    HIP_CHECK(hipFree(d_LogRootDet));
    HIP_CHECK(hipFree(d_Loss));
    HIP_CHECK(hipFree(d_Class));
    HIP_CHECK(hipFree(d_OldClass));
    HIP_CHECK(hipFree(d_Class2));
    HIP_CHECK(hipFree(d_AliveIndex));
    HIP_CHECK(hipFree(d_nMembers));
    initialised = false;
}

// ===========================================================================
// E-STEP KERNEL
// One thread per point p; inner loop over alive clusters.
// Chol + Mean for each cluster are loaded into LDS (shared memory) and
// broadcast to all threads in the block before the forward-solve.
// ===========================================================================
__global__ void kk_estep_kernel(
    const float * __restrict__ d_Data,
    const float * __restrict__ d_Mean,
    const float * __restrict__ d_Chol,
    const float * __restrict__ d_LogRootDet,
    const float * __restrict__ d_Weight,
    const int   * __restrict__ d_AliveIndex,
    const int   * __restrict__ d_Class,
    const int   * __restrict__ d_OldClass,
          float * __restrict__ d_LogP,
    int nPoints, int nDims, int nDims2,
    int nClustersAlive, int MaxClusters,
    float DistThresh, int FullStep,
    float log2piHalf
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;

    float vec[MAX_DIMS], root[MAX_DIMS];

    // LDS: Chol + Mean for one cluster (broadcast across block)
    extern __shared__ float smem[];
    float *s_chol = smem;
    float *s_mean = smem + nDims2;

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = d_AliveIndex[cc];

        for (int i = threadIdx.x; i < nDims2; i += blockDim.x)
            s_chol[i] = d_Chol[c * nDims2 + i];
        for (int i = threadIdx.x; i < nDims;  i += blockDim.x)
            s_mean[i] = d_Mean[c * nDims  + i];
        __syncthreads();

        if (p < nPoints) {
            if (!FullStep) {
                const float lp_self = d_LogP[c          * nPoints + p];
                const float lp_best = d_LogP[d_Class[p] * nPoints + p];
                if (d_Class[p] == d_OldClass[p] &&
                    lp_self - lp_best > DistThresh) {
                    __syncthreads();
                    continue;
                }
            }

            for (int d = 0; d < nDims; d++)
                vec[d] = d_Data[d * nPoints + p] - s_mean[d];

            // Forward-solve lower-triangular s_chol * root = vec
            for (int i = 0; i < nDims; i++) {
                float s = vec[i];
                for (int j = 0; j < i; j++)
                    s -= s_chol[i * nDims + j] * root[j];
                root[i] = s / s_chol[i * nDims + i];
            }

            float mahal = 0.0f;
            for (int i = 0; i < nDims; i++) mahal += root[i] * root[i];

            d_LogP[c * nPoints + p] = mahal * 0.5f
                                    + d_LogRootDet[c]
                                    - hipLogf(d_Weight[c])
                                    + log2piHalf;
        }
        __syncthreads();
    }
}

// ===========================================================================
// M-STEP KERNELS
// ===========================================================================
__global__ void kk_mstep_mean_kernel(
    const float * __restrict__ d_Data,
    const int   * __restrict__ d_Class,
          float * __restrict__ d_MeanAcc,
          int   * __restrict__ d_nMembers,
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

__global__ void kk_mstep_cov_kernel(
    const float * __restrict__ d_Data,
    const float * __restrict__ d_Mean,
    const int   * __restrict__ d_Class,
          float * __restrict__ d_CovAcc,
    int nPoints, int nDims, int nDims2
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;
    const int c = d_Class[p];
    float v[MAX_DIMS];
    for (int d = 0; d < nDims; d++)
        v[d] = d_Data[d * nPoints + p] - d_Mean[c * nDims + d];
    for (int i = 0; i < nDims; i++)
        for (int j = i; j < nDims; j++)
            atomicAdd(&d_CovAcc[c * nDims2 + i * nDims + j], v[i] * v[j]);
}

// ===========================================================================
// C-STEP KERNEL
// ===========================================================================
__global__ void kk_cstep_kernel(
    const float * __restrict__ d_LogP,
    const int   * __restrict__ d_AliveIndex,
          int   * __restrict__ d_Class,
          int   * __restrict__ d_OldClass,
          int   * __restrict__ d_Class2,
    int nPoints, int nClustersAlive, float HugeScore
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;

    d_OldClass[p] = d_Class[p];
    float bestScore = HugeScore, secondScore = HugeScore;
    int   topClass  = 0,         secondClass  = 0;

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
// DELETION LOSS KERNEL
// ===========================================================================
__global__ void kk_deletion_loss_kernel(
    const float * __restrict__ d_LogP,
    const int   * __restrict__ d_Class,
    const int   * __restrict__ d_Class2,
          float * __restrict__ d_Loss,
    int nPoints
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;
    const int   c1    = d_Class[p];
    const int   c2    = d_Class2[p];
    const float delta = d_LogP[c2 * nPoints + p] - d_LogP[c1 * nPoints + p];
    atomicAdd(&d_Loss[c1], delta);
}

// ===========================================================================
// HOST-CALLABLE WRAPPERS
// ===========================================================================

extern "C" void hip_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor) {
    const int nP = gpu->nPoints, nD = gpu->nDims;
    // Transpose point-major [p*nD+d] -> dim-major [d*nP+p] on CPU, then DMA
    std::vector<float> staged(nD * nP);
    for (int p = 0; p < nP; p++)
        for (int d = 0; d < nD; d++)
            staged[d * nP + p] = h_Data_pointMajor[p * nD + d];
    HIP_CHECK(hipMemcpy(gpu->d_Data, staged.data(),
                        sizeof(float) * nD * nP, hipMemcpyHostToDevice));
}

extern "C" void hip_estep(
    KK_GPU      *gpu,
    const float *h_Mean,
    const float *h_Weight,
    const float *h_Chol,
    const int   *h_AliveIndex,
    const int   *h_Class,
    const int   *h_OldClass,
          float *h_LogP,
    int nClustersAlive, float DistThresh, int FullStep,
    double PI, int MaxClusters)
{
    const int nP  = gpu->nPoints;
    const int nD  = gpu->nDims;
    const int nD2 = gpu->nDims2;
    const int maxC= gpu->MaxClusters;

    HIP_CHECK(hipMemcpy(gpu->d_Mean,       h_Mean,
                        sizeof(float) * maxC * nD,   hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_Weight,     h_Weight,
                        sizeof(float) * maxC,         hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_Chol,       h_Chol,
                        sizeof(float) * maxC * nD2,  hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_AliveIndex, h_AliveIndex,
                        sizeof(int)   * nClustersAlive, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_Class,      h_Class,
                        sizeof(int)   * nP,           hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_OldClass,   h_OldClass,
                        sizeof(int)   * nP,           hipMemcpyHostToDevice));

    // Compute LogRootDet on host (D diagonal elements per cluster)
    std::vector<float> h_lrd(maxC, 0.0f);
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        float lrd = 0.0f;
        for (int i = 0; i < nD; i++)
            lrd += std::log(h_Chol[c * nD2 + i * nD + i]);
        h_lrd[c] = lrd;
    }
    HIP_CHECK(hipMemcpy(gpu->d_LogRootDet, h_lrd.data(),
                        sizeof(float) * maxC, hipMemcpyHostToDevice));

    const float log2piHalf = static_cast<float>(std::log(2.0 * PI) * nD * 0.5);
    const int smem_bytes    = sizeof(float) * (nD2 + nD);
    const int grid          = (nP + BLOCK - 1) / BLOCK;

    hipLaunchKernelGGL(kk_estep_kernel, grid, BLOCK, smem_bytes, nullptr,
        gpu->d_Data, gpu->d_Mean, gpu->d_Chol,
        gpu->d_LogRootDet, gpu->d_Weight,
        gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass,
        gpu->d_LogP,
        nP, nD, nD2, nClustersAlive, maxC,
        DistThresh, FullStep, log2piHalf);
    HIP_CHECK(hipDeviceSynchronize());

    // Download LogP cluster-major and transpose to host point-major
    std::vector<float> staging(maxC * nP);
    HIP_CHECK(hipMemcpy(staging.data(), gpu->d_LogP,
                        sizeof(float) * maxC * nP, hipMemcpyDeviceToHost));
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        for (int p = 0; p < nP; p++)
            h_LogP[p * maxC + c] = staging[c * nP + p];
    }
}

extern "C" void hip_mstep(
    KK_GPU    *gpu,
    const int *h_Class,
    const int *h_AliveIndex,
          float *h_Mean,
          float *h_Cov,
          float *h_Weight,
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2)
{
    const int nP = nPoints, nD = nDims, nD2 = nDims2, maxC = MaxClusters;

    HIP_CHECK(hipMemcpy(gpu->d_Class, h_Class, sizeof(int) * nP, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemset(gpu->d_MeanAcc,  0, sizeof(float) * maxC * nD));
    HIP_CHECK(hipMemset(gpu->d_CovAcc,   0, sizeof(float) * maxC * nD2));
    HIP_CHECK(hipMemset(gpu->d_nMembers, 0, sizeof(int)   * maxC));

    const int grid = (nP + BLOCK - 1) / BLOCK;

    // Phase 1a: scatter mean
    hipLaunchKernelGGL(kk_mstep_mean_kernel, grid, BLOCK, 0, nullptr,
        gpu->d_Data, gpu->d_Class, gpu->d_MeanAcc, gpu->d_nMembers, nP, nD);
    HIP_CHECK(hipDeviceSynchronize());

    // Download nMembers + MeanAcc, normalise mean on host, re-upload
    std::vector<int>   h_nm(maxC);
    std::vector<float> h_mean(maxC * nD, 0.0f);
    HIP_CHECK(hipMemcpy(h_nm.data(), gpu->d_nMembers,
                        sizeof(int)   * maxC, hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(h_mean.data(), gpu->d_MeanAcc,
                        sizeof(float) * maxC * nD, hipMemcpyDeviceToHost));

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        if (h_nm[c] == 0) continue;
        const float inv = 1.0f / h_nm[c];
        for (int d = 0; d < nD; d++) {
            h_mean[c * nD + d] *= inv;
            h_Mean[c * nD + d]  = h_mean[c * nD + d];
        }
    }
    HIP_CHECK(hipMemcpy(gpu->d_Mean, h_mean.data(),
                        sizeof(float) * maxC * nD, hipMemcpyHostToDevice));

    // Phase 1b: scatter covariance
    hipLaunchKernelGGL(kk_mstep_cov_kernel, grid, BLOCK, 0, nullptr,
        gpu->d_Data, gpu->d_Mean, gpu->d_Class, gpu->d_CovAcc, nP, nD, nD2);
    HIP_CHECK(hipDeviceSynchronize());

    // Download CovAcc, normalise on host
    std::vector<float> h_cov(maxC * nD2, 0.0f);
    HIP_CHECK(hipMemcpy(h_cov.data(), gpu->d_CovAcc,
                        sizeof(float) * maxC * nD2, hipMemcpyDeviceToHost));
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        if (h_nm[c] <= 1) continue;
        const float inv = 1.0f / (h_nm[c] - 1);
        for (int i = 0; i < nD; i++)
            for (int j = i; j < nD; j++) {
                h_cov[c * nD2 + i * nD + j] *= inv;
                h_Cov[c * nD2 + i * nD + j]  = h_cov[c * nD2 + i * nD + j];
            }
    }

    HIP_CHECK(hipMemcpy(gpu->d_Weight, h_Weight,
                        sizeof(float) * maxC, hipMemcpyHostToDevice));
}

extern "C" void hip_cstep(
    KK_GPU    *gpu,
          int *h_Class,
          int *h_OldClass,
          int *h_Class2,
    int nClustersAlive, int MaxClusters, float HugeScore)
{
    const int nP   = gpu->nPoints;
    const int grid = (nP + BLOCK - 1) / BLOCK;

    hipLaunchKernelGGL(kk_cstep_kernel, grid, BLOCK, 0, nullptr,
        gpu->d_LogP, gpu->d_AliveIndex,
        gpu->d_Class, gpu->d_OldClass, gpu->d_Class2,
        nP, nClustersAlive, HugeScore);
    HIP_CHECK(hipDeviceSynchronize());

    HIP_CHECK(hipMemcpy(h_Class,    gpu->d_Class,
                        sizeof(int) * nP, hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(h_OldClass, gpu->d_OldClass,
                        sizeof(int) * nP, hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(h_Class2,   gpu->d_Class2,
                        sizeof(int) * nP, hipMemcpyDeviceToHost));
}

extern "C" void hip_deletion_loss(
    KK_GPU    *gpu,
          float *h_Loss,
    int MaxClusters)
{
    const int nP   = gpu->nPoints;
    const int grid = (nP + BLOCK - 1) / BLOCK;

    HIP_CHECK(hipMemset(gpu->d_Loss, 0, sizeof(float) * MaxClusters));

    hipLaunchKernelGGL(kk_deletion_loss_kernel, grid, BLOCK, 0, nullptr,
        gpu->d_LogP, gpu->d_Class, gpu->d_Class2, gpu->d_Loss, nP);
    HIP_CHECK(hipDeviceSynchronize());

    HIP_CHECK(hipMemcpy(h_Loss, gpu->d_Loss,
                        sizeof(float) * MaxClusters, hipMemcpyDeviceToHost));
}

#endif // USE_HIP
