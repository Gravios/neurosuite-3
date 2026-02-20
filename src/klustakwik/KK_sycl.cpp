// KK_sycl.cpp — SYCL/oneAPI GPU acceleration for KlustaKwik hot loops.
//
// Kernels (one-to-one with KK_cuda.cu):
//   kk_estep_kernel         — E-step: Mahalanobis LogP for all (point, cluster)
//   kk_mstep_mean_kernel    — M-step: scatter mean accumulation
//   kk_mstep_cov_kernel     — M-step: scatter covariance accumulation
//   kk_cstep_kernel         — C-step: argmin LogP per point
//   kk_deletion_loss_kernel — ConsiderDeletion: per-cluster loss accumulation
//
// DATA LAYOUTS (identical to CUDA backend)
// ----------------------------------------
// d_Data  : dim-major    [d * nPoints + p]   uploaded once, coalesced E-step reads
// d_LogP  : cluster-major [c * nPoints + p]  coalesced E-step writes
// d_Mean  : cluster-major [c * nDims + d]
// d_Chol  : [c * nDims2 + i*nDims + j]       lower-triangular per cluster
// d_Class, d_Class2, d_OldClass : [nPoints]
// d_Weight, d_Loss, d_LogRootDet : [MaxClusters]
//
// SYCL NOTES
// ----------
// All kernels are submitted as parallel_for over nd_range with work-group
// size KK_SYCL_WG (default 256).  The in-order queue ensures correct
// sequencing without explicit event dependencies.
//
// Atomics use sycl::atomic_ref<T, memory_order::relaxed,
//                               memory_scope::device,
//                               access::address_space::global_space>
// which maps to native 32-bit atomics on Intel Arc (xe_gpu target).
//
// Local memory (equivalent to CUDA shared memory) is declared via
// sycl::local_accessor and passed into the kernel lambda.  The Cholesky
// matrix (nDims2 floats) and Mean vector (nDims floats) for each cluster
// fit comfortably: 17*17 + 17 = 306 floats = 1224 bytes, well within the
// 64 KB local memory per work-group on Intel Arc.

#ifdef USE_SYCL

#include "KK_sycl.h"
#include <sycl/sycl.hpp>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Error checking macro
// ---------------------------------------------------------------------------
#define SYCL_CHECK(expr) do { \
    try { (expr); } \
    catch (const sycl::exception &e) { \
        fprintf(stderr, "SYCL error at %s:%d — %s\n", \
                __FILE__, __LINE__, e.what()); \
        std::abort(); \
    } \
} while(0)

static constexpr int MAX_DIMS = 64;
static constexpr int WG       = KK_SYCL_WG;

// ---------------------------------------------------------------------------
// sycl_device_available
// Prefer Intel GPU; fall back to any GPU; refuse CPU (that's the CPU path).
// ---------------------------------------------------------------------------
bool sycl_device_available(sycl::device *out_dev) {
    try {
        // First try: Intel GPU specifically
        for (auto &plat : sycl::platform::get_platforms()) {
            for (auto &dev : plat.get_devices()) {
                if (dev.is_gpu() &&
                    dev.get_info<sycl::info::device::vendor>()
                       .find("Intel") != std::string::npos) {
                    if (out_dev) *out_dev = dev;
                    fprintf(stderr,
                        "[KlustaKwik SYCL] Selected Intel GPU: %s\n",
                        dev.get_info<sycl::info::device::name>().c_str());
                    return true;
                }
            }
        }
        // Fallback: any GPU (Arc might not self-report "Intel" in all drivers)
        sycl::device dev(sycl::gpu_selector_v);
        if (out_dev) *out_dev = dev;
        fprintf(stderr,
            "[KlustaKwik SYCL] Selected GPU: %s\n",
            dev.get_info<sycl::info::device::name>().c_str());
        return true;
    } catch (...) {
        return false;
    }
}

// ---------------------------------------------------------------------------
// KK_GPU::allocate — USM device allocations
// ---------------------------------------------------------------------------
void KK_GPU::allocate(int nP, int nD, int nD2, int maxC) {
    nPoints = nP; nDims = nD; nDims2 = nD2; MaxClusters = maxC;

    d_Data       = sycl::malloc_device<float>(nD   * nP,   q);
    d_LogP       = sycl::malloc_device<float>(maxC * nP,   q);
    d_Mean       = sycl::malloc_device<float>(maxC * nD,   q);
    d_MeanAcc    = sycl::malloc_device<float>(maxC * nD,   q);
    d_Cov        = sycl::malloc_device<float>(maxC * nD2,  q);
    d_CovAcc     = sycl::malloc_device<float>(maxC * nD2,  q);
    d_Weight     = sycl::malloc_device<float>(maxC,        q);
    d_Chol       = sycl::malloc_device<float>(maxC * nD2,  q);
    d_LogRootDet = sycl::malloc_device<float>(maxC,        q);
    d_Loss       = sycl::malloc_device<float>(maxC,        q);
    d_Class      = sycl::malloc_device<int>  (nP,          q);
    d_OldClass   = sycl::malloc_device<int>  (nP,          q);
    d_Class2     = sycl::malloc_device<int>  (nP,          q);
    d_AliveIndex = sycl::malloc_device<int>  (maxC,        q);
    d_nMembers   = sycl::malloc_device<int>  (maxC,        q);
    initialised  = true;
}

// ---------------------------------------------------------------------------
// KK_GPU::free_all
// ---------------------------------------------------------------------------
void KK_GPU::free_all() {
    if (!initialised) return;
    sycl::free(d_Data,       q);
    sycl::free(d_LogP,       q);
    sycl::free(d_Mean,       q);
    sycl::free(d_MeanAcc,    q);
    sycl::free(d_Cov,        q);
    sycl::free(d_CovAcc,     q);
    sycl::free(d_Weight,     q);
    sycl::free(d_Chol,       q);
    sycl::free(d_LogRootDet, q);
    sycl::free(d_Loss,       q);
    sycl::free(d_Class,      q);
    sycl::free(d_OldClass,   q);
    sycl::free(d_Class2,     q);
    sycl::free(d_AliveIndex, q);
    sycl::free(d_nMembers,   q);
    initialised = false;
}

// ---------------------------------------------------------------------------
// sycl_upload_data
// Transpose host point-major [p*nD+d] -> device dim-major [d*nP+p].
// ---------------------------------------------------------------------------
void sycl_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor) {
    const int nP = gpu->nPoints, nD = gpu->nDims;
    // Transpose on CPU into a staging buffer, then DMA to device
    std::vector<float> staged(nD * nP);
    for (int p = 0; p < nP; p++)
        for (int d = 0; d < nD; d++)
            staged[d * nP + p] = h_Data_pointMajor[p * nD + d];
    gpu->q.memcpy(gpu->d_Data, staged.data(), sizeof(float) * nD * nP).wait();
}

// ===========================================================================
// E-STEP KERNEL
//
// One work-item per point p.  For each alive cluster c (loop in item):
//   1. Load Chol[c] and Mean[c] into local memory (broadcast across WG).
//   2. Compute residual vec = Data[:,p] - Mean[c,:].
//   3. Forward-solve lower-triangular Chol * root = vec (sequential, D steps).
//   4. Mahalanobis = sum(root²).
//   5. Write LogP[c*nP + p].
//
// DistThresh heuristic: if a point's current best cluster hasn't changed and
// LogP[self] - LogP[best] > DistThresh, skip recomputing this cluster.
// Identical logic to the CUDA kernel.
// ===========================================================================
void sycl_estep_submit(KK_GPU *gpu,
    int nClustersAlive, float DistThresh, int FullStep, float log2piHalf)
{
    const int nP      = gpu->nPoints;
    const int nD      = gpu->nDims;
    const int nD2     = gpu->nDims2;

    float       *d_LogP       = gpu->d_LogP;
    const float *d_Data       = gpu->d_Data;
    const float *d_Mean       = gpu->d_Mean;
    const float *d_Chol       = gpu->d_Chol;
    const float *d_LogRootDet = gpu->d_LogRootDet;
    const float *d_Weight     = gpu->d_Weight;
    const int   *d_AliveIndex = gpu->d_AliveIndex;
    const int   *d_Class      = gpu->d_Class;
    const int   *d_OldClass   = gpu->d_OldClass;

    const int nGroups = (nP + WG - 1) / WG;
    sycl::nd_range<1> rng(nGroups * WG, WG);

    // Local memory sizes: nD2 floats for Chol + nD floats for Mean
    const int lm_chol_size = nD2;
    const int lm_mean_size = nD;

    gpu->q.submit([&](sycl::handler &h) {
        sycl::local_accessor<float, 1> lm_chol(lm_chol_size, h);
        sycl::local_accessor<float, 1> lm_mean(lm_mean_size, h);

        h.parallel_for(rng, [=](sycl::nd_item<1> it) {
            const int p    = static_cast<int>(it.get_global_id(0));
            const int lid  = static_cast<int>(it.get_local_id(0));
            const int lsz  = static_cast<int>(it.get_local_range(0));

            float vec[MAX_DIMS], root[MAX_DIMS];

            // Skip cluster 0 (noise): loop from cc=1
            for (int cc = 1; cc < nClustersAlive; cc++) {
                const int c = d_AliveIndex[cc];

                // Cooperatively load Chol and Mean into local memory
                for (int i = lid; i < nD2; i += lsz)
                    lm_chol[i] = d_Chol[c * nD2 + i];
                for (int i = lid; i < nD; i += lsz)
                    lm_mean[i] = d_Mean[c * nD + i];
                it.barrier(sycl::access::fence_space::local_space);

                if (p < nP) {
                    // DistThresh heuristic — skip if class stable and gap big
                    if (!FullStep) {
                        const float lp_self = d_LogP[c          * nP + p];
                        const float lp_best = d_LogP[d_Class[p] * nP + p];
                        if (d_Class[p] == d_OldClass[p] &&
                            lp_self - lp_best > DistThresh) {
                            it.barrier(sycl::access::fence_space::local_space);
                            continue;
                        }
                    }

                    for (int d = 0; d < nD; d++)
                        vec[d] = d_Data[d * nP + p] - lm_mean[d];

                    // Forward-solve lower-triangular system
                    for (int i = 0; i < nD; i++) {
                        float s = vec[i];
                        for (int j = 0; j < i; j++)
                            s -= lm_chol[i * nD + j] * root[j];
                        root[i] = s / lm_chol[i * nD + i];
                    }

                    float mahal = 0.0f;
                    for (int i = 0; i < nD; i++) mahal += root[i] * root[i];

                    d_LogP[c * nP + p] = mahal * 0.5f
                                       + d_LogRootDet[c]
                                       - sycl::log(d_Weight[c])
                                       + log2piHalf;
                }
                it.barrier(sycl::access::fence_space::local_space);
            }
        });
    });
}

// ===========================================================================
// M-STEP KERNELS
// ===========================================================================

// Phase 1a: zero accumulators
static void sycl_mstep_zero(KK_GPU *gpu) {
    const int nP = gpu->nPoints, nD = gpu->nDims,
              nD2 = gpu->nDims2, maxC = gpu->MaxClusters;
    gpu->q.memset(gpu->d_MeanAcc,  0, sizeof(float) * maxC * nD).wait();
    gpu->q.memset(gpu->d_CovAcc,   0, sizeof(float) * maxC * nD2).wait();
    gpu->q.memset(gpu->d_nMembers, 0, sizeof(int)   * maxC).wait();
    (void)nP;
}

// Phase 1b: scatter mean accumulation — one item per point
static void sycl_mstep_mean_submit(KK_GPU *gpu) {
    const int nP = gpu->nPoints, nD = gpu->nDims;
    const float *d_Data    = gpu->d_Data;
    const int   *d_Class   = gpu->d_Class;
    float       *d_MeanAcc = gpu->d_MeanAcc;
    int         *d_nMem    = gpu->d_nMembers;

    const int nGroups = (nP + WG - 1) / WG;
    gpu->q.submit([&](sycl::handler &h) {
        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                        [=](sycl::nd_item<1> it) {
            const int p = static_cast<int>(it.get_global_id(0));
            if (p >= nP) return;
            const int c = d_Class[p];

            using AtomI = sycl::atomic_ref<int,
                sycl::memory_order::relaxed,
                sycl::memory_scope::device,
                sycl::access::address_space::global_space>;
            using AtomF = sycl::atomic_ref<float,
                sycl::memory_order::relaxed,
                sycl::memory_scope::device,
                sycl::access::address_space::global_space>;

            AtomI(d_nMem[c]).fetch_add(1);
            for (int d = 0; d < nD; d++)
                AtomF(d_MeanAcc[c * nD + d]).fetch_add(d_Data[d * nP + p]);
        });
    });
}

// Phase 1c: scatter covariance accumulation — one item per point
// Mean must be normalised on device before calling this.
static void sycl_mstep_cov_submit(KK_GPU *gpu) {
    const int nP = gpu->nPoints, nD = gpu->nDims, nD2 = gpu->nDims2;
    const float *d_Data   = gpu->d_Data;
    const float *d_Mean   = gpu->d_Mean;
    const int   *d_Class  = gpu->d_Class;
    float       *d_CovAcc = gpu->d_CovAcc;

    const int nGroups = (nP + WG - 1) / WG;
    gpu->q.submit([&](sycl::handler &h) {
        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                        [=](sycl::nd_item<1> it) {
            const int p = static_cast<int>(it.get_global_id(0));
            if (p >= nP) return;
            const int c = d_Class[p];

            using AtomF = sycl::atomic_ref<float,
                sycl::memory_order::relaxed,
                sycl::memory_scope::device,
                sycl::access::address_space::global_space>;

            float v[MAX_DIMS];
            for (int d = 0; d < nD; d++)
                v[d] = d_Data[d * nP + p] - d_Mean[c * nD + d];
            for (int i = 0; i < nD; i++)
                for (int j = i; j < nD; j++)
                    AtomF(d_CovAcc[c * nD2 + i * nD + j]).fetch_add(v[i] * v[j]);
        });
    });
}

// ===========================================================================
// C-STEP KERNEL — argmin LogP per point
// ===========================================================================
static void sycl_cstep_submit(KK_GPU *gpu,
    int nClustersAlive, float HugeScore)
{
    const int nP   = gpu->nPoints;
    const float *d_LogP      = gpu->d_LogP;
    const int   *d_AliveIndex= gpu->d_AliveIndex;
    int         *d_Class     = gpu->d_Class;
    int         *d_OldClass  = gpu->d_OldClass;
    int         *d_Class2    = gpu->d_Class2;

    const int nGroups = (nP + WG - 1) / WG;
    gpu->q.submit([&](sycl::handler &h) {
        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                        [=](sycl::nd_item<1> it) {
            const int p = static_cast<int>(it.get_global_id(0));
            if (p >= nP) return;

            d_OldClass[p] = d_Class[p];
            float bestScore   = HugeScore, secondScore = HugeScore;
            int   topClass = 0, secondClass = 0;

            for (int cc = 0; cc < nClustersAlive; cc++) {
                const int   c = d_AliveIndex[cc];
                const float s = d_LogP[c * nP + p];
                if (s < bestScore) {
                    secondClass = topClass;   secondScore = bestScore;
                    topClass    = c;          bestScore   = s;
                } else if (s < secondScore) {
                    secondClass = c;          secondScore = s;
                }
            }
            d_Class[p]  = topClass;
            d_Class2[p] = secondClass;
        });
    });
}

// ===========================================================================
// DELETION LOSS KERNEL
// ===========================================================================
static void sycl_deletion_loss_submit(KK_GPU *gpu) {
    const int nP = gpu->nPoints;
    const float *d_LogP   = gpu->d_LogP;
    const int   *d_Class  = gpu->d_Class;
    const int   *d_Class2 = gpu->d_Class2;
    float       *d_Loss   = gpu->d_Loss;

    const int nGroups = (nP + WG - 1) / WG;
    gpu->q.submit([&](sycl::handler &h) {
        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                        [=](sycl::nd_item<1> it) {
            const int p = static_cast<int>(it.get_global_id(0));
            if (p >= nP) return;

            using AtomF = sycl::atomic_ref<float,
                sycl::memory_order::relaxed,
                sycl::memory_scope::device,
                sycl::access::address_space::global_space>;

            const int   c1    = d_Class[p];
            const int   c2    = d_Class2[p];
            const float delta = d_LogP[c2 * nP + p] - d_LogP[c1 * nP + p];
            AtomF(d_Loss[c1]).fetch_add(delta);
        });
    });
}

// ===========================================================================
// HOST-CALLABLE WRAPPERS
// Signatures are identical to the CUDA equivalents in KK_cuda.cu.
// ===========================================================================

void sycl_estep(
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
    (void)MaxClusters; // parameter kept for API consistency with CUDA path
    auto &q = gpu->q;

    // Upload per-step metadata: Mean, Weight, Chol, AliveIndex, Class, OldClass
    q.memcpy(gpu->d_Mean,       h_Mean,       sizeof(float) * maxC * nD).wait();
    q.memcpy(gpu->d_Weight,     h_Weight,     sizeof(float) * maxC).wait();
    q.memcpy(gpu->d_Chol,       h_Chol,       sizeof(float) * maxC * nD2).wait();
    q.memcpy(gpu->d_AliveIndex, h_AliveIndex, sizeof(int)   * nClustersAlive).wait();
    q.memcpy(gpu->d_Class,      h_Class,      sizeof(int)   * nP).wait();
    q.memcpy(gpu->d_OldClass,   h_OldClass,   sizeof(int)   * nP).wait();

    // Compute LogRootDet for each alive cluster (CPU, trivial — D diag elements)
    std::vector<float> h_lrd(maxC, 0.0f);
    for (int cc = 1; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        float lrd = 0.0f;
        for (int i = 0; i < nD; i++)
            lrd += std::log(h_Chol[c * nD2 + i * nD + i]);
        h_lrd[c] = lrd;
    }
    q.memcpy(gpu->d_LogRootDet, h_lrd.data(), sizeof(float) * maxC).wait();

    const float log2piHalf = static_cast<float>(std::log(2.0 * PI) * nD * 0.5);

    // Launch E-step kernel
    sycl_estep_submit(gpu, nClustersAlive, DistThresh, FullStep, log2piHalf);
    q.wait();

    // Download LogP cluster-major [c*nP+p] and transpose to host point-major
    // [p*maxC+c] — identical transpose to the CUDA backend.
    std::vector<float> d_logp_staging(maxC * nP);
    q.memcpy(d_logp_staging.data(), gpu->d_LogP,
             sizeof(float) * maxC * nP).wait();

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        for (int p = 0; p < nP; p++)
            h_LogP[p * maxC + c] = d_logp_staging[c * nP + p];
    }
}

void sycl_mstep(
    KK_GPU    *gpu,
    const int *h_Class,
    const int *h_AliveIndex,
          float *h_Mean,
          float *h_Cov,
          float *h_Weight,
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2)
{
    auto &q = gpu->q;
    const int nP = nPoints, nD = nDims, nD2 = nDims2, maxC = MaxClusters;

    // Upload class assignments
    q.memcpy(gpu->d_Class, h_Class, sizeof(int) * nP).wait();

    // Zero accumulators
    sycl_mstep_zero(gpu);

    // Scatter mean
    sycl_mstep_mean_submit(gpu);
    q.wait();

    // Download nMembers and normalise mean on host, then re-upload
    std::vector<int>   h_nm(maxC);
    std::vector<float> h_mean(maxC * nD, 0.0f);
    q.memcpy(h_nm.data(),   gpu->d_nMembers, sizeof(int)   * maxC).wait();
    q.memcpy(h_mean.data(), gpu->d_MeanAcc,  sizeof(float) * maxC * nD).wait();

    for (int cc = 0; cc < nClustersAlive; cc++) {
        const int c = h_AliveIndex[cc];
        if (h_nm[c] == 0) continue;
        const float inv = 1.0f / h_nm[c];
        for (int d = 0; d < nD; d++) {
            h_mean[c * nD + d] *= inv;
            h_Mean[c * nD + d]  = h_mean[c * nD + d];
        }
    }
    // Upload normalised mean back for cov kernel
    q.memcpy(gpu->d_Mean, h_mean.data(), sizeof(float) * maxC * nD).wait();

    // Scatter covariance
    sycl_mstep_cov_submit(gpu);
    q.wait();

    // Download cov accumulator and normalise on host
    std::vector<float> h_cov(maxC * nD2, 0.0f);
    q.memcpy(h_cov.data(), gpu->d_CovAcc, sizeof(float) * maxC * nD2).wait();

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

    // Weight is already computed on host by KK::MStep before calling sycl_mstep;
    // upload so EStep can read it from device.
    q.memcpy(gpu->d_Weight, h_Weight, sizeof(float) * maxC).wait();
}

void sycl_cstep(
    KK_GPU    *gpu,
          int *h_Class,
          int *h_OldClass,
          int *h_Class2,
    int nClustersAlive, int MaxClusters, float HugeScore)
{
    // d_LogP and d_AliveIndex are current from sycl_estep.
    // d_Class is current (uploaded in sycl_estep).
    (void)MaxClusters; // kept for API consistency with CUDA path
    sycl_cstep_submit(gpu, nClustersAlive, HugeScore);
    gpu->q.wait();

    const int nP = gpu->nPoints;
    gpu->q.memcpy(h_Class,    gpu->d_Class,    sizeof(int) * nP).wait();
    gpu->q.memcpy(h_OldClass, gpu->d_OldClass, sizeof(int) * nP).wait();
    gpu->q.memcpy(h_Class2,   gpu->d_Class2,   sizeof(int) * nP).wait();
}

void sycl_deletion_loss(
    KK_GPU    *gpu,
          float *h_Loss,
    int MaxClusters)
{
    // Zero loss accumulators
    gpu->q.memset(gpu->d_Loss, 0, sizeof(float) * MaxClusters).wait();

    sycl_deletion_loss_submit(gpu);
    gpu->q.wait();

    gpu->q.memcpy(h_Loss, gpu->d_Loss, sizeof(float) * MaxClusters).wait();
}

#endif // USE_SYCL
