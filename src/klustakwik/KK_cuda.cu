// KK_cuda.cu — GPU acceleration for KlustaKwik hot loops
//
// Kernels:
//   kk_estep_kernel         — E-step: Mahalanobis LogP for all (point, cluster)
//   kk_mstep_mean_kernel    — M-step: scatter mean accumulation
//   kk_mstep_cov_kernel     — M-step: scatter covariance accumulation
//   kk_cstep_kernel         — C-step: argmin LogP per point
//   kk_deletion_loss_kernel — ConsiderDeletion: per-cluster loss accumulation
//
// DATA LAYOUTS
// ------------
// d_Data  : dim-major    [d * nPoints + p]   — uploaded once, coalesced E-step reads
// d_LogP  : cluster-major [c * nPoints + p]  — coalesced E-step writes
// d_Mean  : cluster-major [c * nDims + d]
// d_Chol  : [c * nDims2 + i*nDims + j]       lower-triangular per cluster
// d_Class, d_Class2, d_OldClass : [nPoints]
// d_Weight, d_Loss, d_LogRootDet : [MaxClusters]
//
// All wrappers operate directly on device memory retained between calls.
// The host KK arrays stay point-major; the ONLY host<->device transfer
// of LogP is the single download+transpose at the end of cuda_estep.

#include "KK_cuda.h"   // single struct definition
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

#define CUDA_CHECK(call) do { \
    cudaError_t _e = (call); \
    if (_e != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d — %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(_e)); \
        std::abort(); \
    } \
} while(0)

#define MAX_DIMS 64

// ===========================================================================
// KK_GPU implementation
// ===========================================================================
void KK_GPU::allocate(int nP, int nD, int nD2, int maxC) {
    nPoints = nP; nDims = nD; nDims2 = nD2; MaxClusters = maxC;
    CUDA_CHECK(cudaMalloc(&d_Data,       sizeof(float) * nD   * nP));
    CUDA_CHECK(cudaMalloc(&d_LogP,       sizeof(float) * maxC * nP));
    CUDA_CHECK(cudaMalloc(&d_Mean,       sizeof(float) * maxC * nD));
    CUDA_CHECK(cudaMalloc(&d_MeanAcc,    sizeof(float) * maxC * nD));
    CUDA_CHECK(cudaMalloc(&d_Cov,        sizeof(float) * maxC * nD2));
    CUDA_CHECK(cudaMalloc(&d_CovAcc,     sizeof(float) * maxC * nD2));
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

void KK_GPU::free_all() {
    if (!initialised) return;
    cudaFree(d_Data);    cudaFree(d_LogP);    cudaFree(d_Mean);
    cudaFree(d_MeanAcc); cudaFree(d_Cov);     cudaFree(d_CovAcc);
    cudaFree(d_Weight);  cudaFree(d_Chol);    cudaFree(d_LogRootDet);
    cudaFree(d_Loss);    cudaFree(d_Class);   cudaFree(d_OldClass);
    cudaFree(d_Class2);  cudaFree(d_AliveIndex); cudaFree(d_nMembers);
    initialised = false;
}

// ===========================================================================
// E-STEP KERNEL
//
// One thread per point p.  For each alive cluster c >= 1:
//   vec   = Data[:,p] - Mean[c,:]          (nDims loads from dim-major d_Data)
//   root  = TriSolve(Chol[c], vec)         (lower-triangular forward solve)
//   Mahal = ||root||^2
//   LogP[c*nP + p] = Mahal/2 + LogRootDet[c] - log(Weight[c]) + log2piHalf
//
// Cholesky and Mean for each cluster are loaded into shared memory once and
// broadcast to all threads in the block, hiding global memory latency.
// Skip heuristic: unchanged class + LogP already far from best -> skip.
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
          float * __restrict__ d_LogP,       // cluster-major out
    int nPoints, int nDims, int nDims2,
    int nClustersAlive, int MaxClusters,
    float DistThresh, int FullStep,
    float log2piHalf
)
{
    const int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= nPoints) return;

    float vec[MAX_DIMS], root[MAX_DIMS];

    extern __shared__ float smem[];
    float *s_chol = smem;
    float *s_mean = smem + nDims2;

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = d_AliveIndex[cc];

        // Load Chol + Mean for this cluster into shared memory
        for (int i = threadIdx.x; i < nDims2; i += blockDim.x) s_chol[i] = d_Chol[c * nDims2 + i];
        for (int i = threadIdx.x; i < nDims;  i += blockDim.x) s_mean[i] = d_Mean[c * nDims  + i];
        __syncthreads();

        // Skip heuristic
        if (!FullStep) {
            const float lp_self = d_LogP[c             * nPoints + p];
            const float lp_best = d_LogP[d_Class[p]    * nPoints + p];
            if (d_Class[p] == d_OldClass[p] && lp_self - lp_best > DistThresh) {
                __syncthreads();
                continue;
            }
        }

        for (int d = 0; d < nDims; d++)
            vec[d] = d_Data[d * nPoints + p] - s_mean[d];

        // Forward solve: lower-triangular s_chol * root = vec
        for (int i = 0; i < nDims; i++) {
            float s = vec[i];
            for (int j = 0; j < i; j++) s -= s_chol[i * nDims + j] * root[j];
            root[i] = s / s_chol[i * nDims + i];
        }

        float mahal = 0.0f;
        for (int i = 0; i < nDims; i++) mahal += root[i] * root[i];

        d_LogP[c * nPoints + p] = mahal * 0.5f
                                 + d_LogRootDet[c]
                                 - __logf(d_Weight[c])
                                 + log2piHalf;
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
// Operates on d_LogP already on device (cluster-major).
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
// Operates on d_LogP, d_Class, d_Class2 already on device.
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

extern "C" void cuda_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor) {
    // Transpose point-major [p*nD+d] -> dim-major [d*nP+p] for coalesced reads
    const int nP = gpu->nPoints, nD = gpu->nDims;
    std::vector<float> tmp(nP * nD);
    for (int p = 0; p < nP; p++)
        for (int d = 0; d < nD; d++)
            tmp[d * nP + p] = h_Data_pointMajor[p * nD + d];
    CUDA_CHECK(cudaMemcpy(gpu->d_Data, tmp.data(), sizeof(float) * nP * nD,
                          cudaMemcpyHostToDevice));
}

extern "C" void cuda_estep(
    KK_GPU      *gpu,
    const float *h_Mean,
    const float *h_Weight,
    const float *h_Chol,
    const int   *h_AliveIndex,
    const int   *h_Class,
    const int   *h_OldClass,
          float *h_LogP,         // point-major out [nPoints * MaxClusters]
    int nClustersAlive, float DistThresh, int FullStep,
    double PI, int MaxClusters
)
{
    const int nP = gpu->nPoints, nD = gpu->nDims, nD2 = gpu->nDims2;

    // Upload per-step metadata
    CUDA_CHECK(cudaMemcpy(gpu->d_Mean,       h_Mean,       sizeof(float)*MaxClusters*nD,  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Weight,     h_Weight,     sizeof(float)*MaxClusters,     cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Chol,       h_Chol,       sizeof(float)*MaxClusters*nD2, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_AliveIndex, h_AliveIndex, sizeof(int)  *MaxClusters,     cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Class,      h_Class,      sizeof(int)  *nP,              cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_OldClass,   h_OldClass,   sizeof(int)  *nP,              cudaMemcpyHostToDevice));

    // LogRootDet: computed on host (tiny loop, O(K*D))
    std::vector<float> logRootDet(MaxClusters, 0.0f);
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        for (int i = 0; i < nD; i++)
            logRootDet[c] += std::log(h_Chol[c * nD2 + i * nD + i]);
    }
    CUDA_CHECK(cudaMemcpy(gpu->d_LogRootDet, logRootDet.data(),
                          sizeof(float)*MaxClusters, cudaMemcpyHostToDevice));

    // Cluster 0: uniform noise row in d_LogP (cluster-major, row 0)
    {
        const float negLogW0 = -std::log(h_Weight[0]);
        std::vector<float> noise_row(nP, negLogW0);
        CUDA_CHECK(cudaMemcpy(gpu->d_LogP, noise_row.data(),
                              sizeof(float)*nP, cudaMemcpyHostToDevice));
    }

    // Launch E-step kernel
    const int   block       = 256;
    const int   grid        = (nP + block - 1) / block;
    const int   smem        = sizeof(float) * (nD2 + nD);
    const float log2piHalf  = static_cast<float>(std::log(2.0 * PI) * nD * 0.5);

    kk_estep_kernel<<<grid, block, smem>>>(
        gpu->d_Data, gpu->d_Mean, gpu->d_Chol, gpu->d_LogRootDet, gpu->d_Weight,
        gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass, gpu->d_LogP,
        nP, nD, nD2, nClustersAlive, MaxClusters, DistThresh, FullStep, log2piHalf);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    // Download d_LogP (cluster-major) and transpose to host point-major layout
    std::vector<float> tmp(MaxClusters * nP);
    CUDA_CHECK(cudaMemcpy(tmp.data(), gpu->d_LogP,
                          sizeof(float)*MaxClusters*nP, cudaMemcpyDeviceToHost));
    for (int c = 0; c < MaxClusters; c++)
        for (int p = 0; p < nP; p++)
            h_LogP[p * MaxClusters + c] = tmp[c * nP + p];
}

extern "C" void cuda_mstep(
    KK_GPU    *gpu,
    const int *h_Class,
    const int *h_AliveIndex,
          float *h_Mean,
          float *h_Cov,
          float *h_Weight,
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2
)
{
    const int nP = nPoints, nD = nDims;

    CUDA_CHECK(cudaMemcpy(gpu->d_Class, h_Class, sizeof(int)*nP, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(gpu->d_MeanAcc,  0, sizeof(float)*MaxClusters*nD));
    CUDA_CHECK(cudaMemset(gpu->d_nMembers, 0, sizeof(int)  *MaxClusters));

    const int block = 256, grid = (nP + block - 1) / block;
    kk_mstep_mean_kernel<<<grid, block>>>(
        gpu->d_Data, gpu->d_Class, gpu->d_MeanAcc, gpu->d_nMembers, nP, nD);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    // Download and normalise means on host
    std::vector<float> meanAcc(MaxClusters * nD);
    std::vector<int>   nMem(MaxClusters);
    CUDA_CHECK(cudaMemcpy(meanAcc.data(), gpu->d_MeanAcc, sizeof(float)*MaxClusters*nD, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(nMem.data(),   gpu->d_nMembers, sizeof(int)  *MaxClusters,    cudaMemcpyDeviceToHost));

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        if (nMem[c] == 0) continue;
        for (int d = 0; d < nD; d++)
            h_Mean[c * nD + d] = meanAcc[c * nD + d] / nMem[c];
    }

    // Upload normalised means for covariance kernel
    CUDA_CHECK(cudaMemcpy(gpu->d_Mean, h_Mean, sizeof(float)*MaxClusters*nD, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemset(gpu->d_CovAcc, 0, sizeof(float)*MaxClusters*nDims2));

    kk_mstep_cov_kernel<<<grid, block>>>(
        gpu->d_Data, gpu->d_Mean, gpu->d_Class, gpu->d_CovAcc, nP, nD, nDims2);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    std::vector<float> covAcc(MaxClusters * nDims2);
    CUDA_CHECK(cudaMemcpy(covAcc.data(), gpu->d_CovAcc,
                          sizeof(float)*MaxClusters*nDims2, cudaMemcpyDeviceToHost));

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        if (nMem[c] <= 1) continue;
        const float inv = 1.0f / (nMem[c] - 1);
        for (int i = 0; i < nD; i++)
            for (int j = i; j < nD; j++)
                h_Cov[c * nDims2 + i * nD + j] = covAcc[c * nDims2 + i * nD + j] * inv;
    }
}

// C-step: d_LogP and d_Class already on device from the preceding E-step.
// We upload AliveIndex, run the kernel, download updated Class arrays.
extern "C" void cuda_cstep(
    KK_GPU *gpu,
          int *h_Class,
          int *h_OldClass,
          int *h_Class2,
    int nClustersAlive, int MaxClusters, float HugeScore
)
{
    const int nP = gpu->nPoints;
    // d_Class holds the pre-E-step class assignments (uploaded by cuda_estep)
    // d_AliveIndex already uploaded by cuda_estep
    const int block = 256, grid = (nP + block - 1) / block;
    kk_cstep_kernel<<<grid, block>>>(
        gpu->d_LogP, gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass, gpu->d_Class2,
        nP, nClustersAlive, HugeScore);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(h_Class,    gpu->d_Class,    sizeof(int)*nP, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_OldClass, gpu->d_OldClass, sizeof(int)*nP, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_Class2,   gpu->d_Class2,   sizeof(int)*nP, cudaMemcpyDeviceToHost));
}

// Deletion loss: d_LogP, d_Class, d_Class2 all already on device from C-step.
extern "C" void cuda_deletion_loss(
    KK_GPU *gpu,
          float *h_Loss,
    int MaxClusters
)
{
    const int nP = gpu->nPoints;
    CUDA_CHECK(cudaMemset(gpu->d_Loss, 0, sizeof(float)*MaxClusters));
    const int block = 256, grid = (nP + block - 1) / block;
    kk_deletion_loss_kernel<<<grid, block>>>(
        gpu->d_LogP, gpu->d_Class, gpu->d_Class2, gpu->d_Loss, nP);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(h_Loss, gpu->d_Loss, sizeof(float)*MaxClusters,
                          cudaMemcpyDeviceToHost));
}
