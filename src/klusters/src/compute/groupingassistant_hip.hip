/***************************************************************************
 * groupingassistant_hip.hip
 *
 * HIP (AMD ROCm) backend for the errormatrix probability computation.
 *
 * HIP is source-compatible with CUDA; the kernels are identical to the CUDA
 * version.  hipcc compiles this file on AMD hardware; nvcc can also compile
 * it targeting NVIDIA hardware, but we only use this path on AMD GPUs.
 *
 * Supported architectures (set in CMakeLists via CMAKE_HIP_ARCHITECTURES):
 *   gfx1100  — RDNA 3  (RX 7900 series)
 *   gfx1030  — RDNA 2  (RX 6000 series)
 *   gfx90a   — CDNA 2  (MI210/250)
 *   gfx908   — CDNA 1  (MI100)
 ***************************************************************************/

#include <hip/hip_runtime.h>
#include <math.h>
#include <stdio.h>

#include "groupingassistant_gpu.h"

// ---------------------------------------------------------------------------
// Device helpers  (identical to CUDA version)
// ---------------------------------------------------------------------------

__device__ static double
forwardSubstituteSq(const double* __restrict__ L,
                    const double* __restrict__ b,
                    int dim)
{
    double x[64];
    double sq = 0.0;
    for (int i = 0; i < dim; ++i) {
        double s = b[i];
        for (int j = 0; j < i; ++j)
            s -= L[i + j * dim] * x[j];
        x[i] = s / L[i + i * dim];
        sq  += x[i] * x[i];
    }
    return sq;
}

// ---------------------------------------------------------------------------
// Kernel 1: Mahalanobis distances → raw probabilities
// Grid: (ceil(nbSpikes/256), nbClusters)   Block: (256, 1)
// ---------------------------------------------------------------------------
#define BLOCK_X 256

__global__ void hip_mahalanobis_kernel(
    const double* __restrict__ features,
    const double* __restrict__ choleskyAll,
    const double* __restrict__ means,
    const double* __restrict__ logTerms,
    double*                    probOut,
    const int*   __restrict__ ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim)
{
    int spike   = blockIdx.x * blockDim.x + threadIdx.x;
    int cluster = blockIdx.y;
    if (spike >= nbSpikes || cluster >= nbClusters) return;
    if (ignoreFlags[cluster]) return;

    const double* L  = choleskyAll + cluster * nbDim * nbDim;
    const double* mu = means       + cluster * nbDim;
    const double* x  = features    + spike   * nbDim;

    double b[64];
    for (int d = 0; d < nbDim; ++d) b[d] = x[d] - mu[d];

    double mahal = forwardSubstituteSq(L, b, nbDim);
    probOut[spike * nbClusters + cluster] = exp(-0.5 * (mahal + logTerms[cluster]));
}

// ---------------------------------------------------------------------------
// Kernel 2: row-wise normalization
// Grid: (ceil(nbSpikes/256))   Block: (256)
// ---------------------------------------------------------------------------
__global__ void hip_normalize_kernel(
    double* probOut, int nbSpikes, int nbClusters, int cluster1Col)
{
    int spike = blockIdx.x * blockDim.x + threadIdx.x;
    if (spike >= nbSpikes) return;

    double* row = probOut + spike * nbClusters;
    double sum = 0.0;
    for (int c = 0; c < nbClusters; ++c) sum += row[c];
    if (sum == 0.0) { sum = 1.0; row[cluster1Col] = 1.0; }
    double inv = 1.0 / sum;
    for (int c = 0; c < nbClusters; ++c) row[c] *= inv;
}

// ---------------------------------------------------------------------------
// Public C interface
// ---------------------------------------------------------------------------
extern "C" {

int hip_device_available()
{
    int ndev = 0;
    if (hipGetDeviceCount(&ndev) != hipSuccess) return 0;
    return ndev > 0 ? 1 : 0;
}

int hip_compute_probabilities(
    const double* features,
    const double* choleskyAll,
    const double* means,
    const double* logTerms,
    double*       probOut,
    const int*    ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col)
{
    // The device Mahalanobis path uses fixed-width per-thread stack arrays
    // (double x[64] / b[64]).  Refuse anything wider rather than overrun them
    // on-device; the dispatcher then falls back to the CPU path, which sizes
    // its scratch to nbDimensions and has no such bound.
    if (nbDim <= 0 || nbDim > 64) {
        fprintf(stderr,
                "[klusters] grouping HIP: nbDim=%d unsupported (max 64) — using CPU\n",
                nbDim);
        return -1;
    }

    double *d_feat=nullptr, *d_chol=nullptr, *d_means=nullptr;
    double *d_log=nullptr,  *d_prob=nullptr;
    int    *d_ign=nullptr;

    size_t featSz   = (size_t)nbSpikes   * nbDim          * sizeof(double);
    size_t cholSz   = (size_t)nbClusters * nbDim * nbDim  * sizeof(double);
    size_t meansSz  = (size_t)nbClusters * nbDim           * sizeof(double);
    size_t logSz    = (size_t)nbClusters                   * sizeof(double);
    size_t probSz   = (size_t)nbSpikes   * nbClusters      * sizeof(double);
    size_t ignSz    = (size_t)nbClusters                   * sizeof(int);

#define HIP_CHECK(call) \
    do { hipError_t e = (call); \
         if (e != hipSuccess) { \
             fprintf(stderr, "HIP error %s at %s:%d\n", \
                     hipGetErrorString(e), __FILE__, __LINE__); \
             goto hip_error; \
         } \
    } while(0)

    HIP_CHECK(hipMalloc(&d_feat,  featSz));
    HIP_CHECK(hipMalloc(&d_chol,  cholSz));
    HIP_CHECK(hipMalloc(&d_means, meansSz));
    HIP_CHECK(hipMalloc(&d_log,   logSz));
    HIP_CHECK(hipMalloc(&d_prob,  probSz));
    HIP_CHECK(hipMalloc(&d_ign,   ignSz));

    HIP_CHECK(hipMemcpy(d_feat,  features,    featSz,  hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_chol,  choleskyAll, cholSz,  hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_means, means,       meansSz, hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_log,   logTerms,    logSz,   hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_prob,  probOut,     probSz,  hipMemcpyHostToDevice));
    HIP_CHECK(hipMemcpy(d_ign,   ignoreFlags, ignSz,   hipMemcpyHostToDevice));

    {
        dim3 block(BLOCK_X, 1);
        dim3 grid((nbSpikes + BLOCK_X - 1) / BLOCK_X, nbClusters);
        hipLaunchKernelGGL(hip_mahalanobis_kernel, grid, block, 0, 0,
            d_feat, d_chol, d_means, d_log, d_prob, d_ign,
            nbSpikes, nbClusters, nbDim);
        HIP_CHECK(hipGetLastError());
    }
    {
        dim3 block(BLOCK_X);
        dim3 grid((nbSpikes + BLOCK_X - 1) / BLOCK_X);
        hipLaunchKernelGGL(hip_normalize_kernel, grid, block, 0, 0,
            d_prob, nbSpikes, nbClusters, cluster1Col);
        HIP_CHECK(hipGetLastError());
    }

    HIP_CHECK(hipDeviceSynchronize());
    HIP_CHECK(hipMemcpy(probOut, d_prob, probSz, hipMemcpyDeviceToHost));

    hipFree(d_feat); hipFree(d_chol); hipFree(d_means);
    hipFree(d_log);  hipFree(d_prob); hipFree(d_ign);
    return 0;

hip_error:
    if (d_feat)  hipFree(d_feat);
    if (d_chol)  hipFree(d_chol);
    if (d_means) hipFree(d_means);
    if (d_log)   hipFree(d_log);
    if (d_prob)  hipFree(d_prob);
    if (d_ign)   hipFree(d_ign);
    return -1;

#undef HIP_CHECK
}

} // extern "C"
