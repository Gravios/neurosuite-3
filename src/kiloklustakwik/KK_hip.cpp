// KK_hip.cpp — AMD ROCm/HIP GPU acceleration for KlustaKwik hot loops.
//
// Kernels (one-to-one with KK_cuda.cu):
//   kk_estep_kernel              — E-step: Mahalanobis LogP for all (point, cluster)
//   kk_mstep_mean_kernel         — M-step: scatter mean accumulation (LDS reduction)
//   kk_mstep_cov_kernel          — M-step: scatter covariance accumulation (per-cluster)
//   kk_mstep_normalise_mean_kernel — M-step: normalise MeanAcc -> Mean on device
//   kk_mstep_normalise_cov_kernel  — M-step: normalise CovAcc  -> Cov  on device
//   kk_cstep_kernel              — C-step: argmin LogP per point
//   kk_score_kernel              — score reduction: sum LogP[Class[p]*nP+p]
//   kk_deletion_loss_kernel      — ConsiderDeletion: per-cluster loss accumulation
//
// HIP vs CUDA differences:
//   cuda_runtime.h        -> hip/hip_runtime.h
//   __logf()              -> hipLogf()
//   cudaMalloc/Free/cpy   -> hipMalloc/Free/cpy
//   cudaMemset/Async      -> hipMemset/Async
//   cudaDeviceSynchronize -> hipDeviceSynchronize
//   CUDA_CHECK            -> HIP_CHECK
//   <<<grid,block,smem>>> -> hipLaunchKernelGGL(...)
//
// Fix summary (identical to KK_cuda.cu):
//   Fix 2 — LogP stays device-resident; hip_compute_score uses kk_score_kernel.
//   Fix 3 — Mean/Cov normalised on device; only final arrays downloaded for Cholesky.
//   Fix 4 — Mean scatter uses LDS block reduction (BLOCK x fewer global atomics).
//            Cov scatter iterates one cluster per launch.
//   Fix 5 — Three hipMemsetAsync without intermediate syncs.

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
// KK_GPU::allocate / free_all
// ---------------------------------------------------------------------------
void KK_GPU::allocate(int nP, int nD, int nD2, int maxC) {
    nPoints = nP; nDims = nD; nDims2 = nD2; MaxClusters = maxC;
    // Query device LDS limit for adaptive kernel selection
    {
        int dev = 0;
        hipGetDevice(&dev);
        hipDeviceProp_t prop;
        hipGetDeviceProperties(&prop, dev);
        smemLimit = (int)prop.sharedMemPerBlock;
    }
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
    HIP_CHECK(hipMalloc(&d_Score,      sizeof(float) * 1));
    initialised = true;
}

void KK_GPU::free_all() {
    if (!initialised) return;
    HIP_CHECK(hipFree(d_Data));       HIP_CHECK(hipFree(d_LogP));
    HIP_CHECK(hipFree(d_Mean));       HIP_CHECK(hipFree(d_MeanAcc));
    HIP_CHECK(hipFree(d_Cov));        HIP_CHECK(hipFree(d_CovAcc));
    HIP_CHECK(hipFree(d_Weight));     HIP_CHECK(hipFree(d_Chol));
    HIP_CHECK(hipFree(d_LogRootDet)); HIP_CHECK(hipFree(d_Loss));
    HIP_CHECK(hipFree(d_Class));      HIP_CHECK(hipFree(d_OldClass));
    HIP_CHECK(hipFree(d_Class2));     HIP_CHECK(hipFree(d_AliveIndex));
    HIP_CHECK(hipFree(d_nMembers));   HIP_CHECK(hipFree(d_Score));
    initialised = false;
}

// ===========================================================================
// E-STEP KERNEL
//
// One thread per point p.  LDS (shared memory) caches Chol + Mean per cluster.
// Skip heuristic: unchanged class + LogP far from best -> skip cluster.
// LogP remains device-resident; no download occurs here.
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

// Phase 1b: scatter mean with LDS block reduction.
// LDS layout: float lm_mean[maxC*nD] | int lm_nm[maxC]
// Budget: maxC*nD*4 + maxC*4 = 10,400 bytes for maxC=100, nD=25.
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

// Phase 1b (fallback): direct global atomics — no LDS buffer.
// Used when maxC*nD*4 + maxC*4 would exceed the device LDS limit.
__global__ void kk_mstep_mean_direct_kernel(
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

// Phase 1c: scatter covariance — one target cluster per launch.
// LDS buffer: nTri floats per block = 1,300 bytes for nD=25.
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
// LDS tree reduction; partial block sums atomicAdd'd into d_Score[0].
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

extern "C" void hip_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor) {
    const int nP = gpu->nPoints, nD = gpu->nDims;
    std::vector<float> staged(nD * nP);
    for (int p = 0; p < nP; p++)
        for (int d = 0; d < nD; d++)
            staged[d * nP + p] = h_Data_pointMajor[p * nD + d];
    HIP_CHECK(hipMemcpy(gpu->d_Data, staged.data(),
                        sizeof(float)*nD*nP, hipMemcpyHostToDevice));
}

extern "C" void hip_estep(
    KK_GPU      *gpu,
    const float *h_Mean,
    const float *h_Weight,
    const float *h_Chol,
    const int   *h_AliveIndex,
    const int   *h_Class,
    const int   *h_OldClass,
          float *h_LogP,         // nullptr on normal path; non-null for DistDump only
    int nClustersAlive, float DistThresh, int FullStep,
    double PI, int MaxClusters)
{
    const int nP  = gpu->nPoints;
    const int nD  = gpu->nDims;
    const int nD2 = gpu->nDims2;
    const int maxC= gpu->MaxClusters;

    HIP_CHECK(hipMemcpy(gpu->d_Mean,       h_Mean,       sizeof(float)*maxC*nD,       hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_Weight,     h_Weight,     sizeof(float)*maxC,           hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_Chol,       h_Chol,       sizeof(float)*maxC*nD2,       hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_AliveIndex, h_AliveIndex, sizeof(int)  *nClustersAlive, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_Class,      h_Class,      sizeof(int)  *nP,             hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(gpu->d_OldClass,   h_OldClass,   sizeof(int)  *nP,             hipMemcpyHostToDevice));

    std::vector<float> h_lrd(maxC, 0.0f);
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        for (int i = 0; i < nD; i++)
            h_lrd[c] += std::log(h_Chol[c * nD2 + i * nD + i]);
    }
    HIP_CHECK(hipMemcpy(gpu->d_LogRootDet, h_lrd.data(),
                        sizeof(float)*maxC, hipMemcpyHostToDevice));

    // Noise cluster row
    {
        const float negLogW0 = -std::log(h_Weight[0]);
        std::vector<float> noise_row(nP, negLogW0);
        HIP_CHECK(hipMemcpy(gpu->d_LogP, noise_row.data(),
                            sizeof(float)*nP, hipMemcpyHostToDevice));
    }

    const float log2piHalf = static_cast<float>(std::log(2.0 * PI) * nD * 0.5);
    const int   smem_bytes = sizeof(float) * (nD2 + nD);
    const int   grid       = (nP + BLOCK - 1) / BLOCK;

    hipLaunchKernelGGL(kk_estep_kernel, grid, BLOCK, smem_bytes, nullptr,
        gpu->d_Data, gpu->d_Mean, gpu->d_Chol,
        gpu->d_LogRootDet, gpu->d_Weight,
        gpu->d_AliveIndex, gpu->d_Class, gpu->d_OldClass,
        gpu->d_LogP,
        nP, nD, nD2, nClustersAlive, maxC,
        DistThresh, FullStep, log2piHalf);
    HIP_CHECK(hipDeviceSynchronize());

    // LogP stays device-resident. Download only for DistDump debug mode.
    if (h_LogP)
        hip_download_logp(gpu, h_LogP, nClustersAlive, h_AliveIndex);
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
    const int grid = (nP + BLOCK - 1) / BLOCK;
    const int nTri = nD * (nD + 1) / 2;

    // Upload h_Class to d_Class.  During normal EM this costs 1.4 MB but is
    // trivial compared to the accumulation kernels.  On the first MStep of
    // Phase 3 it is essential — the warm-start Class[] lives only on the host
    // with no prior GPU CStep, so d_Class would otherwise be stale.
    HIP_CHECK(hipMemcpy(gpu->d_Class, h_Class,
                        sizeof(int)*nP, hipMemcpyHostToDevice));

    // Upload AliveIndex (may have changed after ConsiderDeletion)
    HIP_CHECK(hipMemcpy(gpu->d_AliveIndex, h_AliveIndex,
                        sizeof(int)*nClustersAlive, hipMemcpyHostToDevice));

    // Zero accumulators — three async memsets, one sync covers all (fix 5)
    HIP_CHECK(hipMemsetAsync(gpu->d_MeanAcc,  0, sizeof(float)*maxC*nD));
    HIP_CHECK(hipMemsetAsync(gpu->d_CovAcc,   0, sizeof(float)*maxC*nD2));
    HIP_CHECK(hipMemsetAsync(gpu->d_nMembers, 0, sizeof(int)  *maxC));
    HIP_CHECK(hipDeviceSynchronize());

    // Scatter mean — LDS reduction when it fits, direct atomics otherwise
    const int smem_mean = (int)(sizeof(float)*maxC*nD + sizeof(int)*maxC);
    if (smem_mean <= gpu->smemLimit) {
        hipLaunchKernelGGL(kk_mstep_mean_kernel, grid, BLOCK, smem_mean, nullptr,
            gpu->d_Data, gpu->d_Class, gpu->d_MeanAcc, gpu->d_nMembers, nP, nD, maxC);
    } else {
        hipLaunchKernelGGL(kk_mstep_mean_direct_kernel, grid, BLOCK, 0, nullptr,
            gpu->d_Data, gpu->d_Class, gpu->d_MeanAcc, gpu->d_nMembers, nP, nD);
    }
    HIP_CHECK(hipDeviceSynchronize());

    // Normalise mean on device (fix 3)
    {
        const int nWork = nClustersAlive * nD;
        const int ng    = (nWork + BLOCK - 1) / BLOCK;
        hipLaunchKernelGGL(kk_mstep_normalise_mean_kernel, ng, BLOCK, 0, nullptr,
            gpu->d_MeanAcc, gpu->d_nMembers, gpu->d_AliveIndex,
            gpu->d_Mean, nClustersAlive, nD);
        HIP_CHECK(hipDeviceSynchronize());
    }

    // Scatter covariance — one launch per cluster (fix 4)
    const int smem_cov = sizeof(float)*nTri;
    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        hipLaunchKernelGGL(kk_mstep_cov_kernel, grid, BLOCK, smem_cov, nullptr,
            gpu->d_Data, gpu->d_Mean, gpu->d_Class, gpu->d_CovAcc,
            nP, nD, nD2, c);
    }
    HIP_CHECK(hipDeviceSynchronize());

    // Normalise cov on device (fix 3)
    {
        const int nWork = nClustersAlive * nTri;
        const int ng    = (nWork + BLOCK - 1) / BLOCK;
        hipLaunchKernelGGL(kk_mstep_normalise_cov_kernel, ng, BLOCK, 0, nullptr,
            gpu->d_CovAcc, gpu->d_nMembers, gpu->d_AliveIndex,
            gpu->d_Cov, nClustersAlive, nD, nD2);
        HIP_CHECK(hipDeviceSynchronize());
    }

    // Download final Mean + Cov for CPU-side Cholesky (fix 3)
    HIP_CHECK(hipMemcpy(h_Mean, gpu->d_Mean, sizeof(float)*maxC*nD,  hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(h_Cov,  gpu->d_Cov,  sizeof(float)*maxC*nD2, hipMemcpyDeviceToHost));

    // Upload Weight (computed on host from deletion logic)
    HIP_CHECK(hipMemcpy(gpu->d_Weight, h_Weight, sizeof(float)*maxC, hipMemcpyHostToDevice));
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

    HIP_CHECK(hipMemcpy(h_Class,    gpu->d_Class,    sizeof(int)*nP, hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(h_OldClass, gpu->d_OldClass, sizeof(int)*nP, hipMemcpyDeviceToHost));
    HIP_CHECK(hipMemcpy(h_Class2,   gpu->d_Class2,   sizeof(int)*nP, hipMemcpyDeviceToHost));
}

extern "C" void hip_deletion_loss(
    KK_GPU    *gpu,
          float *h_Loss,
    int MaxClusters)
{
    const int nP   = gpu->nPoints;
    const int grid = (nP + BLOCK - 1) / BLOCK;

    HIP_CHECK(hipMemset(gpu->d_Loss, 0, sizeof(float)*MaxClusters));
    hipLaunchKernelGGL(kk_deletion_loss_kernel, grid, BLOCK, 0, nullptr,
        gpu->d_LogP, gpu->d_Class, gpu->d_Class2, gpu->d_Loss, nP);
    HIP_CHECK(hipDeviceSynchronize());
    HIP_CHECK(hipMemcpy(h_Loss, gpu->d_Loss, sizeof(float)*MaxClusters, hipMemcpyDeviceToHost));
}

extern "C" float hip_compute_score(KK_GPU *gpu, float penalty)
{
    const int nP   = gpu->nPoints;
    const int grid = (nP + BLOCK - 1) / BLOCK;

    HIP_CHECK(hipMemset(gpu->d_Score, 0, sizeof(float)));
    hipLaunchKernelGGL(kk_score_kernel, grid, BLOCK, sizeof(float)*BLOCK, nullptr,
        gpu->d_LogP, gpu->d_Class, gpu->d_Score, nP);
    HIP_CHECK(hipDeviceSynchronize());

    float h_score = 0.0f;
    HIP_CHECK(hipMemcpy(&h_score, gpu->d_Score, sizeof(float), hipMemcpyDeviceToHost));
    return h_score + penalty;
}

extern "C" void hip_download_logp(KK_GPU *gpu, float *h_LogP,
                                   int /*nClustersAlive*/, const int * /*h_AliveIndex*/)
{
    // Device LogP is cluster-major; host LogP is now also cluster-major.
    // Direct memcpy — no transpose required.
    const int nP   = gpu->nPoints;
    const int maxC = gpu->MaxClusters;
    HIP_CHECK(hipMemcpy(h_LogP, gpu->d_LogP,
                        sizeof(float)*maxC*nP, hipMemcpyDeviceToHost));
}

#endif // USE_HIP
