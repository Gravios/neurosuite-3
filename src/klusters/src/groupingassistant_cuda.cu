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

__device__ static float
forwardSubstituteSq_f32(const float* __restrict__ L,
                        const float* __restrict__ b, int dim)
{
    float x[CUDA_MAHAL_MAX_DIM];
    float sq = 0.f;
    for (int i = 0; i < dim; ++i) {
        float s = b[i];
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

// FP32 Mahalanobis (error-matrix path).  Reads the double inputs and casts to
// float; stages this block's cluster Cholesky L and mean in shared memory (all
// threads in a blockIdx.y block share one cluster), then writes the UNNORMALIZED
// log-posterior -0.5*(mahal + logTerm) to probOut (float).  Ignored clusters get
// -1e30f so the softmax drops them.  Writing log-posteriors (not exp) lets the
// normalize pass stabilize the softmax, avoiding FP32 exp underflow.
__global__ void cuda_mahalanobis_kernel_f32(
    const double* __restrict__ features,
    const double* __restrict__ choleskyAll,
    const double* __restrict__ means,
    const double* __restrict__ logTerms,
    float*                     probOut,
    const int*   __restrict__  ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim)
{
    int cluster = blockIdx.y;
    if (cluster >= nbClusters) return;
    const bool ig = ignoreFlags[cluster] != 0;

    extern __shared__ float sh[];
    float* shL  = sh;
    float* shMu = sh + nbDim * nbDim;
    if (!ig) {
        for (int t = threadIdx.x; t < nbDim * nbDim; t += blockDim.x)
            shL[t]  = (float)choleskyAll[(size_t)cluster * nbDim * nbDim + t];
        for (int t = threadIdx.x; t < nbDim; t += blockDim.x)
            shMu[t] = (float)means[(size_t)cluster * nbDim + t];
    }
    __syncthreads();

    int spike = blockIdx.x * blockDim.x + threadIdx.x;
    if (spike >= nbSpikes) return;
    if (ig) { probOut[(size_t)spike * nbClusters + cluster] = -1e30f; return; }

    const double* x = features + (size_t)spike * nbDim;
    float b[CUDA_MAHAL_MAX_DIM];
    for (int d = 0; d < nbDim; ++d) b[d] = (float)x[d] - shMu[d];

    float mahal = forwardSubstituteSq_f32(shL, b, nbDim);
    probOut[(size_t)spike * nbClusters + cluster] =
        -0.5f * (mahal + (float)logTerms[cluster]);
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

// FP32 row-wise softmax of the log-posteriors, stabilized by the row max so exp()
// cannot underflow the whole row to zero (the failure mode of a naive FP32
// softmax).  To match the FP64 path for genuine outliers, if even the best
// cluster's log-posterior is below the FP64 exp underflow threshold the spike is
// assigned wholly to cluster 1 (noise) instead of its least-bad cluster.
__global__ void cuda_normalize_kernel_f32(
    float* probOut, int nbSpikes, int nbClusters, int cluster1Col)
{
    int spike = blockIdx.x * blockDim.x + threadIdx.x;
    if (spike >= nbSpikes) return;
    float* row = probOut + (size_t)spike * nbClusters;

    float mx = -3.0e38f;
    for (int c = 0; c < nbClusters; ++c) mx = fmaxf(mx, row[c]);

    if (mx < -745.0f) {   // FP64 exp() underflows here too -> unclassifiable
        for (int c = 0; c < nbClusters; ++c) row[c] = 0.0f;
        row[cluster1Col] = 1.0f;
        return;
    }
    float sum = 0.0f;
    for (int c = 0; c < nbClusters; ++c) { float e = __expf(row[c] - mx); row[c] = e; sum += e; }
    float inv = 1.0f / sum;
    for (int c = 0; c < nbClusters; ++c) row[c] *= inv;
}

// ---------------------------------------------------------------------------
// Kernel 3: aggregate row-normalized posteriors into the cluster x cluster
// error matrix, on the device — so only the small (nbClusters^2) result crosses
// PCIe, not the full nbSpikes x nbClusters intermediate.
// Grid: (nbClusters) blocks, one per source cluster i (matrix row).
// Block: (BLOCK_X) threads striding over destination clusters j (columns).
//   errOut[i*nbClusters + j] = mean over cluster-i's spikes of prob[featRow][j]
// featRow[first[i] + s] is the 0-based feature row of cluster i's s-th spike;
// cluster i's spikes occupy contiguous positions [first[i], first[i]+nb[i]).
// Ignored clusters (as rows or columns) yield 0, matching the CPU aggregation.
// ---------------------------------------------------------------------------
__global__ void cuda_aggregate_kernel(
    const float*  __restrict__ prob,
    const int*    __restrict__ featRow,
    const int*    __restrict__ first,
    const int*    __restrict__ nb,
    const int*    __restrict__ ignoreFlags,
    double*                    errOut,
    int nbClusters)
{
    int i = blockIdx.x;
    if (i >= nbClusters) return;
    const bool ig = ignoreFlags[i] != 0;
    const int  f  = first[i];
    const int  n  = nb[i];
    for (int j = threadIdx.x; j < nbClusters; j += blockDim.x) {
        if (ig || ignoreFlags[j] || n <= 0) { errOut[(size_t)i * nbClusters + j] = 0.0; continue; }
        // Accumulate in float: the posteriors are already FP32, so promoting each
        // read to double buys no accuracy (the ~5e-4 FP32 floor dominates) while
        // costing a per-element float->double conversion x nSpikes x nClusters,
        // which on the throttled-FP64 GB202 runs through the scarce FP64 path.
        // The mean's float rounding error is ~1e-5 relative, well under that floor.
        float sum = 0.0f;
        for (int s = 0; s < n; ++s) {
            int row = featRow[f + s];
            sum += prob[(size_t)row * nbClusters + j];
        }
        errOut[(size_t)i * nbClusters + j] = (double)(sum / (float)n);
    }
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

// Compute posteriors AND aggregate them into the error matrix entirely on the
// device; only the nbClusters x nbClusters result is copied back.  nbClusters
// here is the GPU-space cluster count (clusterInfoMap order); the caller places
// the result into the final matrix with any cluster-1 prepend offset.
int cuda_compute_error_matrix(
    const double* features, const double* choleskyAll, const double* means,
    const double* logTerms, const int* ignoreFlags,
    const int* featRow, const int* first, const int* nb,
    double* errOut,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col)
{
    if (nbDim <= 0 || nbDim > CUDA_MAHAL_MAX_DIM) {
        fprintf(stderr,
                "[klusters] grouping CUDA: nbDim=%d unsupported (max %d) - using CPU\n",
                nbDim, CUDA_MAHAL_MAX_DIM);
        return -1;
    }

    const bool timing = errmxTiming();
    ns3clock::time_point t0, tUp, tKer, tAgg, tDl;
    if (timing) t0 = ns3clock::now();

    double *d_feat=nullptr,*d_chol=nullptr,*d_means=nullptr,*d_log=nullptr,*d_err=nullptr;
    float  *d_prob=nullptr;   // FP32 posteriors: halves the buffer and aggregate reads
    int    *d_ign=nullptr,*d_featRow=nullptr,*d_first=nullptr,*d_nb=nullptr;

    size_t featSz  = (size_t)nbSpikes   * nbDim         * sizeof(double);
    size_t cholSz  = (size_t)nbClusters * nbDim * nbDim * sizeof(double);
    size_t meansSz = (size_t)nbClusters * nbDim         * sizeof(double);
    size_t logSz   = (size_t)nbClusters                 * sizeof(double);
    size_t probSz  = (size_t)nbSpikes   * nbClusters    * sizeof(float);
    size_t ignSz   = (size_t)nbClusters                 * sizeof(int);
    size_t frSz    = (size_t)nbSpikes                   * sizeof(int);
    size_t spanSz  = (size_t)nbClusters                 * sizeof(int);
    size_t errSz   = (size_t)nbClusters * nbClusters    * sizeof(double);
    size_t shBytes = (size_t)(nbDim * nbDim + nbDim)     * sizeof(float);

#define CUDA_CHECK_E(call) \
    do { cudaError_t e=(call); if(e!=cudaSuccess){ \
        fprintf(stderr,"CUDA error %s at %s:%d\n", \
                cudaGetErrorString(e),__FILE__,__LINE__); \
        goto cuda_error_e; } } while(0)

    CUDA_CHECK_E(cudaMalloc(&d_feat,    featSz));
    CUDA_CHECK_E(cudaMalloc(&d_chol,    cholSz));
    CUDA_CHECK_E(cudaMalloc(&d_means,   meansSz));
    CUDA_CHECK_E(cudaMalloc(&d_log,     logSz));
    CUDA_CHECK_E(cudaMalloc(&d_prob,    probSz));
    CUDA_CHECK_E(cudaMalloc(&d_ign,     ignSz));
    CUDA_CHECK_E(cudaMalloc(&d_featRow, frSz));
    CUDA_CHECK_E(cudaMalloc(&d_first,   spanSz));
    CUDA_CHECK_E(cudaMalloc(&d_nb,      spanSz));
    CUDA_CHECK_E(cudaMalloc(&d_err,     errSz));

    CUDA_CHECK_E(cudaMemcpy(d_feat,    features,    featSz,  cudaMemcpyHostToDevice));
    CUDA_CHECK_E(cudaMemcpy(d_chol,    choleskyAll, cholSz,  cudaMemcpyHostToDevice));
    CUDA_CHECK_E(cudaMemcpy(d_means,   means,       meansSz, cudaMemcpyHostToDevice));
    CUDA_CHECK_E(cudaMemcpy(d_log,     logTerms,    logSz,   cudaMemcpyHostToDevice));
    CUDA_CHECK_E(cudaMemcpy(d_ign,     ignoreFlags, ignSz,   cudaMemcpyHostToDevice));
    CUDA_CHECK_E(cudaMemcpy(d_featRow, featRow,     frSz,    cudaMemcpyHostToDevice));
    CUDA_CHECK_E(cudaMemcpy(d_first,   first,       spanSz,  cudaMemcpyHostToDevice));
    CUDA_CHECK_E(cudaMemcpy(d_nb,      nb,          spanSz,  cudaMemcpyHostToDevice));
    if (timing) tUp = ns3clock::now();
    // No d_prob memset: the FP32 Mahalanobis kernel writes every cell (ignored
    // clusters get -1e30f, which the softmax drops).

    // FP32 compute: the error matrix is qualitative, so the ~1:64 FP64 throttle
    // on Blackwell is not worth paying.  Cholesky L + mean are staged in shared
    // memory (one cluster per blockIdx.y block); posteriors stay FP32 on device
    // and the aggregation promotes to double so the matrix values stay accurate.
    { dim3 blk(BLOCK_X,1); dim3 grd((nbSpikes+BLOCK_X-1)/BLOCK_X, nbClusters);
      cuda_mahalanobis_kernel_f32<<<grd,blk,shBytes>>>(d_feat,d_chol,d_means,d_log,d_prob,d_ign,
                                                       nbSpikes,nbClusters,nbDim);
      CUDA_CHECK_E(cudaGetLastError()); }
    { dim3 blk(BLOCK_X); dim3 grd((nbSpikes+BLOCK_X-1)/BLOCK_X);
      cuda_normalize_kernel_f32<<<grd,blk>>>(d_prob,nbSpikes,nbClusters,cluster1Col);
      CUDA_CHECK_E(cudaGetLastError()); }
    CUDA_CHECK_E(cudaDeviceSynchronize());
    if (timing) tKer = ns3clock::now();

    { dim3 blk(BLOCK_X); dim3 grd(nbClusters);
      cuda_aggregate_kernel<<<grd,blk>>>(d_prob,d_featRow,d_first,d_nb,d_ign,d_err,nbClusters);
      CUDA_CHECK_E(cudaGetLastError()); }
    CUDA_CHECK_E(cudaDeviceSynchronize());
    if (timing) tAgg = ns3clock::now();

    CUDA_CHECK_E(cudaMemcpy(errOut, d_err, errSz, cudaMemcpyDeviceToHost));
    if (timing) { tDl = ns3clock::now();
        fprintf(stderr,
            "[errormatrix-timing] cuda-agg: upload=%lld kernel=%lld aggregate=%lld download=%lld ms\n",
            ms_(t0,tUp), ms_(tUp,tKer), ms_(tKer,tAgg), ms_(tAgg,tDl)); }

    cudaFree(d_feat); cudaFree(d_chol); cudaFree(d_means); cudaFree(d_log);
    cudaFree(d_prob); cudaFree(d_ign);  cudaFree(d_featRow); cudaFree(d_first);
    cudaFree(d_nb);   cudaFree(d_err);
    return 0;

cuda_error_e:
    if(d_feat)cudaFree(d_feat);   if(d_chol)cudaFree(d_chol);  if(d_means)cudaFree(d_means);
    if(d_log)cudaFree(d_log);     if(d_prob)cudaFree(d_prob);  if(d_ign)cudaFree(d_ign);
    if(d_featRow)cudaFree(d_featRow); if(d_first)cudaFree(d_first); if(d_nb)cudaFree(d_nb);
    if(d_err)cudaFree(d_err);
    return -1;
#undef CUDA_CHECK_E
}

} // extern "C"
