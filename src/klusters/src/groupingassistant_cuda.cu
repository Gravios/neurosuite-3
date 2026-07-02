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
#include <cfloat>
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
// FP32 (low-precision) variants.  The error matrix is used only for qualitative
// visual curation, so single precision is ample; on GPUs whose FP64 throughput
// is a small fraction of FP32 (e.g. RTX PRO 6000 Blackwell, ~1/64) this is far
// faster and halves the probability-buffer footprint.  The double kernels and
// host path above are left byte-for-byte untouched, so high-precision mode is
// unchanged; low precision is a purely additive, opt-in path.
// ---------------------------------------------------------------------------
__device__ static float
forwardSubstituteSq_f32(const float* __restrict__ L,
                        const float* __restrict__ b, int dim)
{
    float x[CUDA_MAHAL_MAX_DIM];
    float sq = 0.0f;
    for (int i = 0; i < dim; ++i) {
        float s = b[i];
        for (int j = 0; j < i; ++j)
            s -= L[i + j * dim] * x[j];
        x[i] = s / L[i + i * dim];
        sq  += x[i] * x[i];
    }
    return sq;
}

__global__ void cuda_mahalanobis_kernel_f32(
    const float* __restrict__ features,
    const float* __restrict__ choleskyAll,
    const float* __restrict__ means,
    const float* __restrict__ logTerms,
    float*                    probOut,
    const int*   __restrict__ ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim)
{
    int spike   = blockIdx.x * blockDim.x + threadIdx.x;
    int cluster = blockIdx.y;
    if (spike >= nbSpikes || cluster >= nbClusters) return;
    if (ignoreFlags[cluster]) return;

    const float* L  = choleskyAll + cluster * nbDim * nbDim;
    const float* mu = means       + cluster * nbDim;
    const float* x  = features    + spike   * nbDim;

    float b[CUDA_MAHAL_MAX_DIM];
    for (int d = 0; d < nbDim; ++d) b[d] = x[d] - mu[d];

    float mahal = forwardSubstituteSq_f32(L, b, nbDim);
    // Store the log-probability (logit), NOT expf: the normalize kernel does a
    // numerically stable softmax (subtract the per-row max before expf).  In FP32,
    // expf(-0.5*(mahal+logTerm)) underflows to 0 once the exponent drops below
    // ~-88 — routine for a 24-D space — which collapses whole rows to the zero-sum
    // fallback and corrupts the matrix.  Double (range ~1e-308) never hits this,
    // so the double path keeps its direct expf.
    probOut[spike * nbClusters + cluster] =
        -0.5f * (mahal + logTerms[cluster]);
}

__global__ void cuda_normalize_kernel_f32(
    float* probOut, const int* __restrict__ ignoreFlags,
    int nbSpikes, int nbClusters, int cluster1Col)
{
    int spike = blockIdx.x * blockDim.x + threadIdx.x;
    if (spike >= nbSpikes) return;
    float* row = probOut + spike * nbClusters;

    // Row cells hold logits from the mahalanobis kernel.  Stable softmax over the
    // non-ignored clusters: subtract the row max so the largest term is expf(0)=1
    // and nothing underflows.  Ignored clusters were never written by kernel 1, so
    // skip them via ignoreFlags rather than trusting their contents.  The result
    // matches the double path's exp/sum normalization (softmax is scale-invariant).
    float maxL = -FLT_MAX;
    for (int c = 0; c < nbClusters; ++c)
        if (!ignoreFlags[c] && row[c] > maxL) maxL = row[c];

    float sum = 0.0f;
    if (maxL > -FLT_MAX)
        for (int c = 0; c < nbClusters; ++c)
            if (!ignoreFlags[c]) sum += expf(row[c] - maxL);

    if (sum == 0.0f) {              // no valid cluster (all ignored) — match double fallback
        for (int c = 0; c < nbClusters; ++c) row[c] = 0.0f;
        row[cluster1Col] = 1.0f;
        return;
    }
    float inv = 1.0f / sum;
    for (int c = 0; c < nbClusters; ++c)
        row[c] = ignoreFlags[c] ? 0.0f : expf(row[c] - maxL) * inv;
}

// Host FP32 path.  Signature mirrors the double contract: double host in/out,
// converted to/from float around the device compute.  probOut is written
// normalized, exactly like the double path.
static int cuda_compute_probabilities_f32(
    const double* features, const double* choleskyAll, const double* means,
    const double* logTerms, double* probOut, const int* ignoreFlags,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col)
{
    const bool timing = errmxTiming();
    ns3clock::time_point t0, tIn, tUp, tKer, tDl, tConv;
    if (timing) t0 = ns3clock::now();

    std::vector<float> h_feat ((size_t)nbSpikes   * nbDim);
    std::vector<float> h_chol ((size_t)nbClusters * nbDim * nbDim);
    std::vector<float> h_means((size_t)nbClusters * nbDim);
    std::vector<float> h_log  ((size_t)nbClusters);
    for (size_t i = 0; i < h_feat.size();  ++i) h_feat [i] = (float)features[i];
    for (size_t i = 0; i < h_chol.size();  ++i) h_chol [i] = (float)choleskyAll[i];
    for (size_t i = 0; i < h_means.size(); ++i) h_means[i] = (float)means[i];
    for (size_t i = 0; i < h_log.size();   ++i) h_log  [i] = (float)logTerms[i];
    if (timing) tIn = ns3clock::now();

    float *d_feat=nullptr,*d_chol=nullptr,*d_means=nullptr,*d_log=nullptr,*d_prob=nullptr;
    int   *d_ign=nullptr;

    size_t featSz  = (size_t)nbSpikes   * nbDim         * sizeof(float);
    size_t cholSz  = (size_t)nbClusters * nbDim * nbDim * sizeof(float);
    size_t meansSz = (size_t)nbClusters * nbDim         * sizeof(float);
    size_t logSz   = (size_t)nbClusters                 * sizeof(float);
    size_t probSz  = (size_t)nbSpikes   * nbClusters    * sizeof(float);
    size_t ignSz   = (size_t)nbClusters                 * sizeof(int);

#define CUDA_CHECK_F(call) \
    do { cudaError_t e=(call); if(e!=cudaSuccess){ \
        fprintf(stderr,"CUDA error %s at %s:%d\n", \
                cudaGetErrorString(e),__FILE__,__LINE__); \
        goto cuda_error_f; } } while(0)

    CUDA_CHECK_F(cudaMalloc(&d_feat,  featSz));
    CUDA_CHECK_F(cudaMalloc(&d_chol,  cholSz));
    CUDA_CHECK_F(cudaMalloc(&d_means, meansSz));
    CUDA_CHECK_F(cudaMalloc(&d_log,   logSz));
    CUDA_CHECK_F(cudaMalloc(&d_prob,  probSz));
    CUDA_CHECK_F(cudaMalloc(&d_ign,   ignSz));

    CUDA_CHECK_F(cudaMemcpy(d_feat,  h_feat.data(),  featSz,  cudaMemcpyHostToDevice));
    CUDA_CHECK_F(cudaMemcpy(d_chol,  h_chol.data(),  cholSz,  cudaMemcpyHostToDevice));
    CUDA_CHECK_F(cudaMemcpy(d_means, h_means.data(), meansSz, cudaMemcpyHostToDevice));
    CUDA_CHECK_F(cudaMemcpy(d_log,   h_log.data(),   logSz,   cudaMemcpyHostToDevice));
    CUDA_CHECK_F(cudaMemcpy(d_ign,   ignoreFlags,    ignSz,   cudaMemcpyHostToDevice));
    CUDA_CHECK_F(cudaMemset(d_prob,  0, probSz));
    if (timing) tUp = ns3clock::now();

    { dim3 blk(BLOCK_X,1); dim3 grd((nbSpikes+BLOCK_X-1)/BLOCK_X, nbClusters);
      cuda_mahalanobis_kernel_f32<<<grd,blk>>>(d_feat,d_chol,d_means,d_log,d_prob,d_ign,
                                               nbSpikes,nbClusters,nbDim);
      CUDA_CHECK_F(cudaGetLastError()); }

    { dim3 blk(BLOCK_X); dim3 grd((nbSpikes+BLOCK_X-1)/BLOCK_X);
      cuda_normalize_kernel_f32<<<grd,blk>>>(d_prob,d_ign,nbSpikes,nbClusters,cluster1Col);
      CUDA_CHECK_F(cudaGetLastError()); }

    CUDA_CHECK_F(cudaDeviceSynchronize());
    if (timing) tKer = ns3clock::now();
    {
        std::vector<float> h_prob((size_t)nbSpikes * nbClusters);
        CUDA_CHECK_F(cudaMemcpy(h_prob.data(), d_prob, probSz, cudaMemcpyDeviceToHost));
        if (timing) tDl = ns3clock::now();
        const size_t n = h_prob.size();
        for (size_t i = 0; i < n; ++i) probOut[i] = (double)h_prob[i];
        if (timing) { tConv = ns3clock::now();
            fprintf(stderr,
                "[errormatrix-timing] cuda-fp32: inConv=%lld upload=%lld kernel=%lld "
                "download=%lld outConv=%lld ms\n",
                ms_(t0,tIn), ms_(tIn,tUp), ms_(tUp,tKer), ms_(tKer,tDl), ms_(tDl,tConv)); }
    }

    cudaFree(d_feat); cudaFree(d_chol); cudaFree(d_means);
    cudaFree(d_log);  cudaFree(d_prob); cudaFree(d_ign);
    return 0;

cuda_error_f:
    if(d_feat)  cudaFree(d_feat);  if(d_chol)  cudaFree(d_chol);
    if(d_means) cudaFree(d_means); if(d_log)   cudaFree(d_log);
    if(d_prob)  cudaFree(d_prob);  if(d_ign)   cudaFree(d_ign);
    return -1;
#undef CUDA_CHECK_F
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
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col, int lowPrecision)
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

    if (lowPrecision)
        return cuda_compute_probabilities_f32(features, choleskyAll, means,
                   logTerms, probOut, ignoreFlags,
                   nbSpikes, nbClusters, nbDim, cluster1Col);

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
    CUDA_CHECK(cudaMemcpy(d_prob,  probOut,     probSz,  cudaMemcpyHostToDevice));
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
