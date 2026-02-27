// KK_cuda.cu — GPU acceleration for KlustaKwik hot loops
//
// Kernels:
//   kk_estep_kernel              — E-step: Mahalanobis LogP for all (point, cluster)
//   kk_mstep_mean_kernel         — M-step: scatter mean accumulation (local-mem reduction)
//   kk_mstep_cov_kernel          — M-step: scatter covariance accumulation (per-cluster)
//   kk_mstep_normalise_mean_kernel — M-step: normalise MeanAcc -> Mean on device
//   kk_mstep_normalise_cov_kernel  — M-step: normalise CovAcc  -> Cov  on device
//   kk_cstep_kernel              — C-step: argmin LogP per point
//   kk_score_kernel              — score reduction: sum LogP[Class[p]*nP+p]
//   kk_deletion_loss_kernel      — ConsiderDeletion: per-cluster loss accumulation
//
// DATA LAYOUTS
// ------------
// d_Data  : dim-major    [d * nPoints + p]   — uploaded once, coalesced E-step reads
// d_LogP  : cluster-major [c * nPoints + p]  — coalesced E-step writes, stays device-resident
// d_Mean  : cluster-major [c * nDims + d]
// d_Chol  : [c * nDims2 + i*nDims + j]       lower-triangular per cluster
// d_Class, d_Class2, d_OldClass : [nPoints]
// d_Weight, d_Loss, d_LogRootDet : [MaxClusters]
// d_Score : [1]  score reduction accumulator
//
// Fix summary:
//   Fix 2 — LogP stays device-resident; ComputeScore uses kk_score_kernel;
//            cuda_download_logp available for DistDump mode only.
//   Fix 3 — Mean/Cov normalised on device; only final arrays downloaded for Cholesky.
//   Fix 4 — Mean scatter uses block shared-memory reduction (256x fewer global atomics).
//            Cov scatter iterates one cluster at a time (fits in shared memory).
//   Fix 5 — Three memsets submitted async without intermediate syncs.

#include "KK_cuda.h"
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>

#define CUDA_CHECK(call) do { \
    cudaError_t _e = (call); \
    if (_e != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d -- %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(_e)); \
        std::abort(); \
    } \
} while(0)

#define MAX_DIMS 64
static constexpr int BLOCK = 256;

// ===========================================================================
// KK_GPU allocate / free
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
    CUDA_CHECK(cudaMalloc(&d_Score,      sizeof(float) * 1));
    initialised = true;
}

void KK_GPU::free_all() {
    if (!initialised) return;
    cudaFree(d_Data);       cudaFree(d_LogP);       cudaFree(d_Mean);
    cudaFree(d_MeanAcc);    cudaFree(d_Cov);        cudaFree(d_CovAcc);
    cudaFree(d_Weight);     cudaFree(d_Chol);       cudaFree(d_LogRootDet);
    cudaFree(d_Loss);       cudaFree(d_Class);      cudaFree(d_OldClass);
    cudaFree(d_Class2);     cudaFree(d_AliveIndex); cudaFree(d_nMembers);
    cudaFree(d_Score);
    initialised = false;
}

// ===========================================================================
// E-STEP KERNEL
//
// One thread per point p. For each alive cluster c >= 1:
//   Chol + Mean loaded into shared memory per cluster and broadcast.
//   Skip heuristic: unchanged class + LogP far from best -> skip cluster.
//   LogP remains device-resident; no download occurs in this kernel.
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
    if (p >= nPoints) return;

    float vec[MAX_DIMS], root[MAX_DIMS];

    extern __shared__ float smem[];
    float *s_chol = smem;
    float *s_mean = smem + nDims2;

    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = d_AliveIndex[cc];

        for (int i = threadIdx.x; i < nDims2; i += blockDim.x) s_chol[i] = d_Chol[c * nDims2 + i];
        for (int i = threadIdx.x; i < nDims;  i += blockDim.x) s_mean[i] = d_Mean[c * nDims  + i];
        __syncthreads();

        if (!FullStep) {
            const float lp_self = d_LogP[c          * nPoints + p];
            const float lp_best = d_LogP[d_Class[p] * nPoints + p];
            if (d_Class[p] == d_OldClass[p] && lp_self - lp_best > DistThresh) {
                __syncthreads();
                continue;
            }
        }

        for (int d = 0; d < nDims; d++)
            vec[d] = d_Data[d * nPoints + p] - s_mean[d];

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

// Phase 1b: scatter mean with block shared-memory reduction.
//
// Each block accumulates BLOCK points into shared lm_mean[maxC*nD] and
// lm_nm[maxC], then flushes with one atomicAdd per element per block.
// Reduces global atomic traffic by a factor of BLOCK (256x).
//
// Shared memory: maxC*nD*4 + maxC*4. With maxC=100, nD=25: 10,400 bytes.
__global__ void kk_mstep_mean_kernel(
    const float * __restrict__ d_Data,
    const int   * __restrict__ d_Class,
          float * __restrict__ d_MeanAcc,
          int   * __restrict__ d_nMembers,
    int nPoints, int nDims, int maxC
)
{
    extern __shared__ char smem_raw[];
    float *lm_mean = reinterpret_cast<float*>(smem_raw);
    int   *lm_nm   = reinterpret_cast<int*>(lm_mean + maxC * nDims);

    const int tid = threadIdx.x;
    const int lsz = blockDim.x;

    for (int i = tid; i < maxC * nDims; i += lsz) lm_mean[i] = 0.0f;
    for (int i = tid; i < maxC;         i += lsz) lm_nm[i]   = 0;
    __syncthreads();

    const int p = blockIdx.x * lsz + tid;
    if (p < nPoints) {
        const int c = d_Class[p];
        atomicAdd(&lm_nm[c], 1);
        for (int d = 0; d < nDims; d++)
            atomicAdd(&lm_mean[c * nDims + d], d_Data[d * nPoints + p]);
    }
    __syncthreads();

    for (int i = tid; i < maxC;         i += lsz) if (lm_nm[i])   atomicAdd(&d_nMembers[i], lm_nm[i]);
    for (int i = tid; i < maxC * nDims; i += lsz) if (lm_mean[i]) atomicAdd(&d_MeanAcc[i], lm_mean[i]);
}

// Phase 1c: scatter covariance — one target cluster per launch.
//
// Shared buffer holds nTri = nD*(nD+1)/2 floats per block (1,300 bytes for
// nD=25). Eliminates inter-cluster atomic contention entirely.
__global__ void kk_mstep_cov_kernel(
    const float * __restrict__ d_Data,
    const float * __restrict__ d_Mean,
    const int   * __restrict__ d_Class,
          float * __restrict__ d_CovAcc,
    int nPoints, int nDims, int nDims2, int targetC
)
{
    extern __shared__ float lm_cov[];
    const int tid  = threadIdx.x;
    const int lsz  = blockDim.x;
    const int nTri = nDims * (nDims + 1) / 2;

    for (int i = tid; i < nTri; i += lsz) lm_cov[i] = 0.0f;
    __syncthreads();

    const int p = blockIdx.x * lsz + tid;
    if (p < nPoints && d_Class[p] == targetC) {
        float v[MAX_DIMS];
        for (int d = 0; d < nDims; d++)
            v[d] = d_Data[d * nPoints + p] - d_Mean[targetC * nDims + d];
        int tri = 0;
        for (int i = 0; i < nDims; i++)
            for (int j = i; j < nDims; j++, tri++)
                atomicAdd(&lm_cov[tri], v[i] * v[j]);
    }
    __syncthreads();

    // Decode tri -> (i,j), flush to global CovAcc
    for (int tri = tid; tri < nTri; tri += lsz) {
        if (lm_cov[tri] == 0.0f) continue;
        int i = 0, rem = tri;
        while (rem >= nDims - i) { rem -= (nDims - i); i++; }
        const int j = i + rem;
        atomicAdd(&d_CovAcc[targetC * nDims2 + i * nDims + j], lm_cov[tri]);
    }
}

// Phase 1d: normalise mean on device — one thread per (cluster, dim).
__global__ void kk_mstep_normalise_mean_kernel(
    const float * __restrict__ d_MeanAcc,
    const int   * __restrict__ d_nMembers,
    const int   * __restrict__ d_AliveIndex,
          float * __restrict__ d_Mean,
    int nClustersAlive, int nDims
)
{
    const int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= nClustersAlive * nDims) return;
    const int cc = idx / nDims;
    const int d  = idx % nDims;
    const int c  = d_AliveIndex[cc];
    const int nm = d_nMembers[c];
    d_Mean[c * nDims + d] = (nm > 0) ? d_MeanAcc[c * nDims + d] / (float)nm : 0.0f;
}

// Phase 1e: normalise cov on device — one thread per (cluster, upper-tri element).
__global__ void kk_mstep_normalise_cov_kernel(
    const float * __restrict__ d_CovAcc,
    const int   * __restrict__ d_nMembers,
    const int   * __restrict__ d_AliveIndex,
          float * __restrict__ d_Cov,
    int nClustersAlive, int nDims, int nDims2
)
{
    const int nTri = nDims * (nDims + 1) / 2;
    const int idx  = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= nClustersAlive * nTri) return;
    const int cc  = idx / nTri;
    const int tri = idx % nTri;
    const int c   = d_AliveIndex[cc];
    const int nm  = d_nMembers[c];

    int i = 0, rem = tri;
    while (rem >= nDims - i) { rem -= (nDims - i); i++; }
    const int j = i + rem;

    d_Cov[c * nDims2 + i * nDims + j] =
        (nm > 1) ? d_CovAcc[c * nDims2 + i * nDims + j] / (float)(nm - 1) : 0.0f;
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
// SCORE REDUCTION KERNEL
// Shared-memory tree reduction; partial block sums atomicAdd'd into d_Score[0].
// ===========================================================================
__global__ void kk_score_kernel(
    const float * __restrict__ d_LogP,
    const int   * __restrict__ d_Class,
          float * __restrict__ d_Score,
    int nPoints
)
{
    extern __shared__ float s_sum[];
    const int tid = threadIdx.x;
    const int p   = blockIdx.x * blockDim.x + tid;

    s_sum[tid] = (p < nPoints) ? d_LogP[d_Class[p] * nPoints + p] : 0.0f;
    __syncthreads();

    for (int stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (tid < stride) s_sum[tid] += s_sum[tid + stride];
        __syncthreads();
    }

    if (tid == 0) atomicAdd(d_Score, s_sum[0]);
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

extern "C" void cuda_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor) {
    const int nP = gpu->nPoints, nD = gpu->nDims;
    std::vector<float> tmp(nP * nD);
    for (int p = 0; p < nP; p++)
        for (int d = 0; d < nD; d++)
            tmp[d * nP + p] = h_Data_pointMajor[p * nD + d];
    CUDA_CHECK(cudaMemcpy(gpu->d_Data, tmp.data(), sizeof(float)*nP*nD, cudaMemcpyHostToDevice));
}

extern "C" void cuda_estep(
    KK_GPU      *gpu,
    const float *h_Mean,
    const float *h_Weight,
    const float *h_Chol,
    const int   *h_AliveIndex,
    const int   *h_Class,
    const int   *h_OldClass,
          float *h_LogP,         // nullptr on normal path; non-null for DistDump only
    int nClustersAlive, float DistThresh, int FullStep,
    double PI, int MaxClusters
)
{
    const int nP = gpu->nPoints, nD = gpu->nDims, nD2 = gpu->nDims2;

    CUDA_CHECK(cudaMemcpy(gpu->d_Mean,       h_Mean,       sizeof(float)*MaxClusters*nD,  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Weight,     h_Weight,     sizeof(float)*MaxClusters,      cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Chol,       h_Chol,       sizeof(float)*MaxClusters*nD2,  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_AliveIndex, h_AliveIndex, sizeof(int)  *nClustersAlive,   cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_Class,      h_Class,      sizeof(int)  *nP,               cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(gpu->d_OldClass,   h_OldClass,   sizeof(int)  *nP,               cudaMemcpyHostToDevice));

    std::vector<float> logRootDet(MaxClusters, 0.0f);
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        for (int i = 0; i < nD; i++)
            logRootDet[c] += std::log(h_Chol[c * nD2 + i * nD + i]);
    }
    CUDA_CHECK(cudaMemcpy(gpu->d_LogRootDet, logRootDet.data(),
                          sizeof(float)*MaxClusters, cudaMemcpyHostToDevice));

    {
        const float negLogW0 = -std::log(h_Weight[0]);
        std::vector<float> noise_row(nP, negLogW0);
        CUDA_CHECK(cudaMemcpy(gpu->d_LogP, noise_row.data(),
                              sizeof(float)*nP, cudaMemcpyHostToDevice));
    }

    const int   grid       = (nP + BLOCK - 1) / BLOCK;
    const int   smem       = sizeof(float) * (nD2 + nD);
    const float log2piHalf = static_cast<float>(std::log(2.0 * PI) * nD * 0.5);

    kk_estep_kernel<<<grid, BLOCK, smem>>>(
        gpu->d_Data, gpu->d_Mean, gpu->d_Chol, gpu->d_LogRootDet, gpu->d_Weight,
        gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass, gpu->d_LogP,
        nP, nD, nD2, nClustersAlive, MaxClusters, DistThresh, FullStep, log2piHalf);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    // LogP stays device-resident. Download only for DistDump debug mode.
    if (h_LogP)
        cuda_download_logp(gpu, h_LogP, nClustersAlive, h_AliveIndex);
}

extern "C" void cuda_mstep(
    KK_GPU    *gpu,
    const int *h_Class,          // d_Class already current from CStep; h_Class unused
    const int *h_AliveIndex,
          float *h_Mean,
          float *h_Cov,
          float *h_Weight,
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2
)
{
    const int nP = nPoints, nD = nDims, nD2 = nDims2, maxC = MaxClusters;
    const int grid = (nP + BLOCK - 1) / BLOCK;
    const int nTri = nD * (nD + 1) / 2;

    // Upload AliveIndex (may have changed after ConsiderDeletion)
    CUDA_CHECK(cudaMemcpy(gpu->d_AliveIndex, h_AliveIndex,
                          sizeof(int)*nClustersAlive, cudaMemcpyHostToDevice));

    // Zero accumulators — three async memsets, one sync covers all (fix 5)
    CUDA_CHECK(cudaMemsetAsync(gpu->d_MeanAcc,  0, sizeof(float)*maxC*nD));
    CUDA_CHECK(cudaMemsetAsync(gpu->d_CovAcc,   0, sizeof(float)*maxC*nD2));
    CUDA_CHECK(cudaMemsetAsync(gpu->d_nMembers, 0, sizeof(int)  *maxC));
    CUDA_CHECK(cudaDeviceSynchronize());

    // Scatter mean with shared-memory block reduction (fix 4)
    const int smem_mean = sizeof(float)*maxC*nD + sizeof(int)*maxC;
    kk_mstep_mean_kernel<<<grid, BLOCK, smem_mean>>>(
        gpu->d_Data, gpu->d_Class, gpu->d_MeanAcc, gpu->d_nMembers, nP, nD, maxC);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    // Normalise mean on device (fix 3)
    {
        const int nWork = nClustersAlive * nD;
        const int ng    = (nWork + BLOCK - 1) / BLOCK;
        kk_mstep_normalise_mean_kernel<<<ng, BLOCK>>>(
            gpu->d_MeanAcc, gpu->d_nMembers, gpu->d_AliveIndex,
            gpu->d_Mean, nClustersAlive, nD);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    // Scatter covariance — one launch per cluster (fix 4)
    const int smem_cov = sizeof(float)*nTri;
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        kk_mstep_cov_kernel<<<grid, BLOCK, smem_cov>>>(
            gpu->d_Data, gpu->d_Mean, gpu->d_Class, gpu->d_CovAcc,
            nP, nD, nD2, c);
        CUDA_CHECK(cudaGetLastError());
    }
    CUDA_CHECK(cudaDeviceSynchronize());

    // Normalise cov on device (fix 3)
    {
        const int nWork = nClustersAlive * nTri;
        const int ng    = (nWork + BLOCK - 1) / BLOCK;
        kk_mstep_normalise_cov_kernel<<<ng, BLOCK>>>(
            gpu->d_CovAcc, gpu->d_nMembers, gpu->d_AliveIndex,
            gpu->d_Cov, nClustersAlive, nD, nD2);
        CUDA_CHECK(cudaGetLastError());
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    // Download final Mean + Cov for CPU-side Cholesky (fix 3)
    CUDA_CHECK(cudaMemcpy(h_Mean, gpu->d_Mean, sizeof(float)*maxC*nD,  cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_Cov,  gpu->d_Cov,  sizeof(float)*maxC*nD2, cudaMemcpyDeviceToHost));

    // Upload Weight (computed on host from deletion logic)
    CUDA_CHECK(cudaMemcpy(gpu->d_Weight, h_Weight, sizeof(float)*maxC, cudaMemcpyHostToDevice));
}

extern "C" void cuda_cstep(
    KK_GPU *gpu,
          int *h_Class,
          int *h_OldClass,
          int *h_Class2,
    int nClustersAlive, int MaxClusters, float HugeScore
)
{
    const int nP   = gpu->nPoints;
    const int grid = (nP + BLOCK - 1) / BLOCK;
    kk_cstep_kernel<<<grid, BLOCK>>>(
        gpu->d_LogP, gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass, gpu->d_Class2,
        nP, nClustersAlive, HugeScore);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    CUDA_CHECK(cudaMemcpy(h_Class,    gpu->d_Class,    sizeof(int)*nP, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_OldClass, gpu->d_OldClass, sizeof(int)*nP, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaMemcpy(h_Class2,   gpu->d_Class2,   sizeof(int)*nP, cudaMemcpyDeviceToHost));
}

extern "C" void cuda_deletion_loss(
    KK_GPU *gpu,
          float *h_Loss,
    int MaxClusters
)
{
    const int nP   = gpu->nPoints;
    const int grid = (nP + BLOCK - 1) / BLOCK;
    CUDA_CHECK(cudaMemset(gpu->d_Loss, 0, sizeof(float)*MaxClusters));
    kk_deletion_loss_kernel<<<grid, BLOCK>>>(
        gpu->d_LogP, gpu->d_Class, gpu->d_Class2, gpu->d_Loss, nP);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(h_Loss, gpu->d_Loss, sizeof(float)*MaxClusters, cudaMemcpyDeviceToHost));
}

extern "C" float cuda_compute_score(KK_GPU *gpu, float penalty)
{
    const int nP   = gpu->nPoints;
    const int grid = (nP + BLOCK - 1) / BLOCK;
    CUDA_CHECK(cudaMemset(gpu->d_Score, 0, sizeof(float)));
    kk_score_kernel<<<grid, BLOCK, sizeof(float)*BLOCK>>>(
        gpu->d_LogP, gpu->d_Class, gpu->d_Score, nP);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    float h_score = 0.0f;
    CUDA_CHECK(cudaMemcpy(&h_score, gpu->d_Score, sizeof(float), cudaMemcpyDeviceToHost));
    return h_score + penalty;
}

extern "C" void cuda_download_logp(KK_GPU *gpu, float *h_LogP,
                                    int nClustersAlive, const int *h_AliveIndex)
{
    const int nP   = gpu->nPoints;
    const int maxC = gpu->MaxClusters;
    std::vector<float> staging(maxC * nP);
    CUDA_CHECK(cudaMemcpy(staging.data(), gpu->d_LogP,
                          sizeof(float)*maxC*nP, cudaMemcpyDeviceToHost));
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        for (int p = 0; p < nP; p++)
            h_LogP[p * maxC + c] = staging[c * nP + p];
    }
}
