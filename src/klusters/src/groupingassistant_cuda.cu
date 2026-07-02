/***************************************************************************
 * groupingassistant_cuda.cu
 *
 * CUDA (NVIDIA) backend for the errormatrix probability computation.
 *
 * Supported architectures (set via CUDA_ARCHITECTURES in CMakeLists):
 *   75  — Turing   (RTX 2000 series)
 *   86  — Ampere   (RTX 3000 series)
 *   89  — Ada      (RTX 4000 series)
 *   120 — Blackwell (RTX 5000 series, e.g. RTX 5070 Ti)
 ***************************************************************************/

#include <cuda_runtime.h>
#include <math.h>
#include <stdio.h>
#include <vector>
#include <chrono>
#include <cstdlib>

// Opt-in phase timing (NS3_ERRORMATRIX_TIMING): separates host<->device transfer
// from kernel compute so the error-matrix cost can be attributed. Zero overhead
// when the variable is unset.
namespace {
    using ns3clock = std::chrono::steady_clock;
    inline bool errmxTiming(){ static const bool on = (std::getenv("NS3_ERRORMATRIX_TIMING")!=nullptr); return on; }
    inline long long ms_(ns3clock::time_point a, ns3clock::time_point b){
        return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count();
    }
}

#include "groupingassistant_gpu.h"

// ---------------------------------------------------------------------------
// Device helpers
// ---------------------------------------------------------------------------

// Per-thread device stack arrays (forwardSubstituteSq::x[] and the kernel's
// b[]) are fixed at this width.  nbDim greater than this would index past the
// arrays — undefined behaviour on the device.  The host entry point rejects
// anything larger so the caller falls back to the (unbounded) CPU path.
#define CUDA_MAHAL_MAX_DIM 64

__device__ static double
forwardSubstituteSq(const double* __restrict__ L,
                    const double* __restrict__ b, int dim)
{
    double x[CUDA_MAHAL_MAX_DIM];
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
// Kernel 1: Mahalanobis → raw probabilities
// Grid: (ceil(nbSpikes/256), nbClusters)   Block: (256,1)
// ---------------------------------------------------------------------------
#define BLOCK_X 256

__global__ void cuda_mahalanobis_kernel(
    const double* __restrict__ features,
    const double* __restrict__ choleskyAll,
    const double* __restrict__ means,
    const double* __restrict__ logTerms,
    double*                    probOut,
    const int*   __restrict__  ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim)
{
    int spike   = blockIdx.x * blockDim.x + threadIdx.x;
    int cluster = blockIdx.y;
    if (spike >= nbSpikes || cluster >= nbClusters) return;
    if (ignoreFlags[cluster]) return;

    const double* L  = choleskyAll + cluster * nbDim * nbDim;
    const double* mu = means       + cluster * nbDim;
    const double* x  = features    + spike   * nbDim;

    double b[CUDA_MAHAL_MAX_DIM];
    for (int d = 0; d < nbDim; ++d) b[d] = x[d] - mu[d];

    double mahal = forwardSubstituteSq(L, b, nbDim);
    probOut[spike * nbClusters + cluster] =
        exp(-0.5 * (mahal + logTerms[cluster]));
}

// ---------------------------------------------------------------------------
// Kernel 2: row-wise normalization
// ---------------------------------------------------------------------------
__global__ void cuda_normalize_kernel(
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

int cuda_device_available()
{
    int ndev = 0;
    if (cudaGetDeviceCount(&ndev) != cudaSuccess) return 0;
    return ndev > 0 ? 1 : 0;
}

int cuda_compute_probabilities(
    const double* features, const double* choleskyAll, const double* means,
    const double* logTerms, double* probOut, const int* ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col)
{
    // The Mahalanobis kernel uses fixed-width per-thread stack arrays
    // (CUDA_MAHAL_MAX_DIM).  Refuse anything wider rather than overrun them on
    // the device; the dispatcher then falls back to the CPU path, which has no
    // such bound.  (Typical feature counts are well under this — e.g. 24 for
    // the stderiv PCA space — so this never triggers in practice.)
    if (nbDim <= 0 || nbDim > CUDA_MAHAL_MAX_DIM) {
        fprintf(stderr,
                "[klusters] grouping CUDA: nbDim=%d unsupported (max %d) — using CPU\n",
                nbDim, CUDA_MAHAL_MAX_DIM);
        return -1;
    }

    const bool timing = errmxTiming();
    ns3clock::time_point t0, tUp, tKer, tDl;
    if (timing) t0 = ns3clock::now();

    double *d_feat=nullptr,*d_chol=nullptr,*d_means=nullptr;
    double *d_log=nullptr, *d_prob=nullptr;
    int    *d_ign=nullptr;

    size_t featSz  = (size_t)nbSpikes   * nbDim         * sizeof(double);
    size_t cholSz  = (size_t)nbClusters * nbDim * nbDim * sizeof(double);
    size_t meansSz = (size_t)nbClusters * nbDim          * sizeof(double);
    size_t logSz   = (size_t)nbClusters                  * sizeof(double);
    size_t probSz  = (size_t)nbSpikes   * nbClusters     * sizeof(double);
    size_t ignSz   = (size_t)nbClusters                  * sizeof(int);

#define CUDA_CHECK(call) \
    do { cudaError_t e=(call); if(e!=cudaSuccess){ \
        fprintf(stderr,"CUDA error %s at %s:%d\n", \
                cudaGetErrorString(e),__FILE__,__LINE__); \
        goto cuda_error; } } while(0)

    CUDA_CHECK(cudaMalloc(&d_feat,  featSz));
    CUDA_CHECK(cudaMalloc(&d_chol,  cholSz));
    CUDA_CHECK(cudaMalloc(&d_means, meansSz));
    CUDA_CHECK(cudaMalloc(&d_log,   logSz));
    CUDA_CHECK(cudaMalloc(&d_prob,  probSz));
    CUDA_CHECK(cudaMalloc(&d_ign,   ignSz));

    CUDA_CHECK(cudaMemcpy(d_feat,  features,    featSz,  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_chol,  choleskyAll, cholSz,  cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_means, means,       meansSz, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMemcpy(d_log,   logTerms,    logSz,   cudaMemcpyHostToDevice));
    // Zero the output buffer device-side instead of uploading the (already
    // zeroed) ~15 GB host probOut.  The kernel writes only non-ignored cells and
    // ignored cells must read as 0, so a memset is equivalent to the upload but
    // avoids a large H2D transfer (~0.4 s at this matrix size).
    CUDA_CHECK(cudaMemset(d_prob, 0, probSz));
    CUDA_CHECK(cudaMemcpy(d_ign,   ignoreFlags, ignSz,   cudaMemcpyHostToDevice));
    if (timing) tUp = ns3clock::now();

    { dim3 blk(BLOCK_X,1); dim3 grd((nbSpikes+BLOCK_X-1)/BLOCK_X, nbClusters);
      cuda_mahalanobis_kernel<<<grd,blk>>>(d_feat,d_chol,d_means,d_log,d_prob,d_ign,
                                            nbSpikes,nbClusters,nbDim);
      CUDA_CHECK(cudaGetLastError()); }

    { dim3 blk(BLOCK_X); dim3 grd((nbSpikes+BLOCK_X-1)/BLOCK_X);
      cuda_normalize_kernel<<<grd,blk>>>(d_prob,nbSpikes,nbClusters,cluster1Col);
      CUDA_CHECK(cudaGetLastError()); }

    CUDA_CHECK(cudaDeviceSynchronize());
    if (timing) tKer = ns3clock::now();
    CUDA_CHECK(cudaMemcpy(probOut,d_prob,probSz,cudaMemcpyDeviceToHost));
    if (timing) { tDl = ns3clock::now();
        fprintf(stderr,
            "[errormatrix-timing] cuda-fp64: upload=%lld kernel=%lld download=%lld ms\n",
            ms_(t0,tUp), ms_(tUp,tKer), ms_(tKer,tDl)); }

    cudaFree(d_feat); cudaFree(d_chol); cudaFree(d_means);
    cudaFree(d_log);  cudaFree(d_prob); cudaFree(d_ign);
    return 0;

cuda_error:
    if(d_feat)  cudaFree(d_feat);  if(d_chol)  cudaFree(d_chol);
    if(d_means) cudaFree(d_means); if(d_log)   cudaFree(d_log);
    if(d_prob)  cudaFree(d_prob);  if(d_ign)   cudaFree(d_ign);
    return -1;
#undef CUDA_CHECK
}

} // extern "C"
