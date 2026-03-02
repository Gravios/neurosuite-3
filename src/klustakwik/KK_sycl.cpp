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
    d_Score      = sycl::malloc_device<float>(1,           q);
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
    sycl::free(d_Score,      q);
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

                // All work-items must reach both barriers unconditionally to
                // avoid sub-group divergence (undefined behaviour in SYCL).
                // The DistThresh skip is expressed as a boolean guard around
                // the compute and write — never as a `continue` that would
                // cause some items to miss the closing barrier.
                if (p < nP) {
                    // DistThresh heuristic — skip compute if class stable and
                    // the gap to the best cluster is already large enough that
                    // this cluster cannot become the new winner.
                    bool skip = false;
                    if (!FullStep) {
                        const float lp_self = d_LogP[c          * nP + p];
                        const float lp_best = d_LogP[d_Class[p] * nP + p];
                        skip = (d_Class[p] == d_OldClass[p] &&
                                lp_self - lp_best > DistThresh);
                    }

                    if (!skip) {
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
                }
                // Barrier always reached by every work-item — safe to load
                // the next cluster's Chol/Mean into local memory.
                it.barrier(sycl::access::fence_space::local_space);
            }
        });
    });
}

// ===========================================================================
// M-STEP KERNELS
// ===========================================================================

// Phase 1b: scatter mean accumulation with local-memory reduction.
//
// Each work-group accumulates its WG points into a LOCAL mean array
// [maxC * nD] and a LOCAL nMembers array [maxC], then atomically flushes
// the partial sums to global memory once per work-group.  This reduces
// global atomic traffic by a factor of WG (256) compared with the naive
// per-point global atomic approach.
//
// Local memory budget: maxC * nD floats + maxC ints.
//   With maxC=100, nD=25: 100*25*4 + 100*4 = 10,400 bytes — fits within
//   the 64 KB local memory limit on Intel Arc.
//
// Initialisation: work-items cooperatively zero the local arrays before
// accumulating, using a strided loop to cover all maxC*nD elements.
static void sycl_mstep_mean_submit(KK_GPU *gpu) {
    const int nP   = gpu->nPoints;
    const int nD   = gpu->nDims;
    const int maxC = gpu->MaxClusters;
    const float *d_Data    = gpu->d_Data;
    const int   *d_Class   = gpu->d_Class;
    float       *d_MeanAcc = gpu->d_MeanAcc;
    int         *d_nMem    = gpu->d_nMembers;

    const int lm_mean_elems = maxC * nD;
    const int lm_nm_elems   = maxC;
    const int nGroups = (nP + WG - 1) / WG;

    gpu->q.submit([&](sycl::handler &h) {
        sycl::local_accessor<float, 1> lm_mean(lm_mean_elems, h);
        sycl::local_accessor<int,   1> lm_nm  (lm_nm_elems,   h);

        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                       [=](sycl::nd_item<1> it) {
            const int p   = static_cast<int>(it.get_global_id(0));
            const int lid = static_cast<int>(it.get_local_id(0));
            const int lsz = static_cast<int>(it.get_local_range(0));

            // Zero local accumulators cooperatively
            for (int i = lid; i < lm_mean_elems; i += lsz) lm_mean[i] = 0.0f;
            for (int i = lid; i < lm_nm_elems;   i += lsz) lm_nm[i]   = 0;
            it.barrier(sycl::access::fence_space::local_space);

            // Accumulate this point into local memory (no global atomics here)
            if (p < nP) {
                const int c = d_Class[p];
                using AtomLI = sycl::atomic_ref<int,
                    sycl::memory_order::relaxed,
                    sycl::memory_scope::work_group,
                    sycl::access::address_space::local_space>;
                using AtomLF = sycl::atomic_ref<float,
                    sycl::memory_order::relaxed,
                    sycl::memory_scope::work_group,
                    sycl::access::address_space::local_space>;
                AtomLI(lm_nm[c]).fetch_add(1);
                for (int d = 0; d < nD; d++)
                    AtomLF(lm_mean[c * nD + d]).fetch_add(d_Data[d * nP + p]);
            }
            it.barrier(sycl::access::fence_space::local_space);

            // Flush non-zero local partial sums to global (one atomic per element)
            using AtomGI = sycl::atomic_ref<int,
                sycl::memory_order::relaxed,
                sycl::memory_scope::device,
                sycl::access::address_space::global_space>;
            using AtomGF = sycl::atomic_ref<float,
                sycl::memory_order::relaxed,
                sycl::memory_scope::device,
                sycl::access::address_space::global_space>;
            for (int i = lid; i < lm_nm_elems; i += lsz)
                if (lm_nm[i] > 0) AtomGI(d_nMem[i]).fetch_add(lm_nm[i]);
            for (int i = lid; i < lm_mean_elems; i += lsz)
                if (lm_mean[i] != 0.0f) AtomGF(d_MeanAcc[i]).fetch_add(lm_mean[i]);
        });
    });
}

// Phase 1c: scatter covariance accumulation with per-cluster local reduction.
//
// The full covariance local buffer (maxC * nD² floats) exceeds the 64 KB
// local memory limit (100 * 625 * 4 = 250 KB).  Instead we iterate over
// clusters one at a time: for each target cluster C, only points whose
// Class[p] == C contribute, and the per-work-group local buffer holds just
// nD*(nD+1)/2 floats for the upper triangle — 25*26/2 * 4 = 1,300 bytes,
// well within limits.
//
// Cost: nClusters kernel launches instead of 1, but each launch is fast
// (most work-items skip immediately) and global atomic contention is
// eliminated entirely — all work-items in a group write to the same cluster's
// local buffer with work_group-scope atomics, then flush once per group.
static void sycl_mstep_cov_submit(KK_GPU *gpu) {
    const int nP   = gpu->nPoints;
    const int nD   = gpu->nDims;
    const int nD2  = gpu->nDims2;
    const int maxC = gpu->MaxClusters;
    const float *d_Data   = gpu->d_Data;
    const float *d_Mean   = gpu->d_Mean;
    const int   *d_Class  = gpu->d_Class;
    float       *d_CovAcc = gpu->d_CovAcc;

    const int nTri    = nD * (nD + 1) / 2;   // upper-triangle elements
    const int nGroups = (nP + WG - 1) / WG;

    for (int targetC = 0; targetC < maxC; targetC++) {
        const int tc = targetC;  // capture by value for lambda

        gpu->q.submit([&](sycl::handler &h) {
            // Local upper-triangle accumulator for this cluster
            sycl::local_accessor<float, 1> lm_cov(nTri, h);

            h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                           [=](sycl::nd_item<1> it) {
                const int p   = static_cast<int>(it.get_global_id(0));
                const int lid = static_cast<int>(it.get_local_id(0));
                const int lsz = static_cast<int>(it.get_local_range(0));

                // Zero local accumulator
                for (int i = lid; i < nTri; i += lsz) lm_cov[i] = 0.0f;
                it.barrier(sycl::access::fence_space::local_space);

                // Only points belonging to targetC contribute
                if (p < nP && d_Class[p] == tc) {
                    float v[MAX_DIMS];
                    for (int d = 0; d < nD; d++)
                        v[d] = d_Data[d * nP + p] - d_Mean[tc * nD + d];

                    using AtomLF = sycl::atomic_ref<float,
                        sycl::memory_order::relaxed,
                        sycl::memory_scope::work_group,
                        sycl::access::address_space::local_space>;

                    // Accumulate upper triangle into local flat index
                    int tri = 0;
                    for (int i = 0; i < nD; i++)
                        for (int j = i; j < nD; j++, tri++)
                            AtomLF(lm_cov[tri]).fetch_add(v[i] * v[j]);
                }
                it.barrier(sycl::access::fence_space::local_space);

                // Flush non-zero partial sums to global (one atomic per tri elem)
                using AtomGF = sycl::atomic_ref<float,
                    sycl::memory_order::relaxed,
                    sycl::memory_scope::device,
                    sycl::access::address_space::global_space>;

                // Decode tri index -> (i,j) for global address, flush cooperatively
                for (int tri = lid; tri < nTri; tri += lsz) {
                    if (lm_cov[tri] == 0.0f) continue;
                    // Decode flat tri index to (i, j)
                    int i = 0, rem = tri;
                    while (rem >= nD - i) { rem -= (nD - i); i++; }
                    const int j = i + rem;
                    AtomGF(d_CovAcc[tc * nD2 + i * nD + j]).fetch_add(lm_cov[tri]);
                }
            });
        });
    }
    // Single wait after all cluster passes — in-order queue ensures correct
    // sequencing of the per-cluster submits without intermediate waits.
    gpu->q.wait();
}

// Phase 1d: normalise mean in-place on device — one item per (cluster, dim)
// d_MeanAcc[c*nD+d] /= d_nMembers[c], written into d_Mean.
// Only alive clusters (nMembers > 0) are touched; dead clusters are skipped.
static void sycl_mstep_normalise_mean_submit(KK_GPU *gpu,
    int nClustersAlive, const int *d_AliveIndex_param)
{
    const int nD = gpu->nDims;
    const float *d_MeanAcc = gpu->d_MeanAcc;
    const int   *d_nMem    = gpu->d_nMembers;
    float       *d_Mean    = gpu->d_Mean;

    // Launch one work-item per (cluster, dim): at most maxC * nD items.
    // With nD=25 and nClusters<=100 this is trivially small — even a single
    // work-group is fine.  We launch enough groups for a full WG multiple.
    const int nWork   = nClustersAlive * nD;
    const int nGroups = (nWork + WG - 1) / WG;
    gpu->q.submit([&](sycl::handler &h) {
        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                       [=](sycl::nd_item<1> it) {
            const int idx = static_cast<int>(it.get_global_id(0));
            if (idx >= nWork) return;
            const int cc = idx / nD;
            const int d  = idx % nD;
            const int c  = d_AliveIndex_param[cc];
            const int nm = d_nMem[c];
            d_Mean[c * nD + d] = (nm > 0)
                ? d_MeanAcc[c * nD + d] / static_cast<float>(nm)
                : 0.0f;
        });
    });
}

// Phase 1e: normalise covariance in-place on device — one item per (cluster, upper-tri element)
// d_CovAcc[c*nD2+i*nD+j] /= (nMembers[c]-1), written into d_Cov.
// Upper triangle only (j >= i); clusters with nMembers <= 1 are zeroed.
static void sycl_mstep_normalise_cov_submit(KK_GPU *gpu,
    int nClustersAlive, const int *d_AliveIndex_param)
{
    const int nD  = gpu->nDims;
    const int nD2 = gpu->nDims2;
    const float *d_CovAcc = gpu->d_CovAcc;
    const int   *d_nMem   = gpu->d_nMembers;
    float       *d_Cov    = gpu->d_Cov;

    // Upper-triangle elements per cluster: nD*(nD+1)/2
    const int nTri  = nD * (nD + 1) / 2;
    const int nWork = nClustersAlive * nTri;
    const int nGroups = (nWork + WG - 1) / WG;
    gpu->q.submit([&](sycl::handler &h) {
        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                       [=](sycl::nd_item<1> it) {
            const int idx = static_cast<int>(it.get_global_id(0));
            if (idx >= nWork) return;
            const int cc   = idx / nTri;
            const int tri  = idx % nTri;
            const int c    = d_AliveIndex_param[cc];
            const int nm   = d_nMem[c];

            // Map flat triangle index to (i, j) with j >= i
            // tri = i*(2*nD-i-1)/2 + j-i  — use running decode
            int i = 0, rem = tri;
            while (rem >= nD - i) { rem -= (nD - i); i++; }
            const int j = i + rem;

            d_Cov[c * nD2 + i * nD + j] = (nm > 1)
                ? d_CovAcc[c * nD2 + i * nD + j] / static_cast<float>(nm - 1)
                : 0.0f;
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
// SCORE REDUCTION KERNEL
//
// Computes sum_p LogP[Class[p] * nP + p] on device using local memory
// reduction, then adds penalty on host.  Returns the complete penalised score.
//
// This replaces the ~138 MB/step full LogP download that ComputeScore()
// previously required.  Only nPoints ints (Class) and nPoints floats
// (best-cluster LogP values) are touched — all device-resident.
// ===========================================================================
float sycl_compute_score(KK_GPU *gpu, float penalty)
{
    const int nP   = gpu->nPoints;
    const float *d_LogP  = gpu->d_LogP;
    const int   *d_Class = gpu->d_Class;
    float       *d_Score = gpu->d_Score;

    // Zero the accumulator
    gpu->q.memset(d_Score, 0, sizeof(float)).wait();

    const int nGroups = (nP + WG - 1) / WG;
    gpu->q.submit([&](sycl::handler &h) {
        sycl::local_accessor<float, 1> lm_sum(WG, h);

        h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                       [=](sycl::nd_item<1> it) {
            const int p   = static_cast<int>(it.get_global_id(0));
            const int lid = static_cast<int>(it.get_local_id(0));
            const int lsz = static_cast<int>(it.get_local_range(0));

            // Each work-item accumulates its point's contribution
            lm_sum[lid] = (p < nP) ? d_LogP[d_Class[p] * nP + p] : 0.0f;
            it.barrier(sycl::access::fence_space::local_space);

            // Tree reduction within work-group
            for (int stride = lsz / 2; stride > 0; stride >>= 1) {
                if (lid < stride) lm_sum[lid] += lm_sum[lid + stride];
                it.barrier(sycl::access::fence_space::local_space);
            }

            // Work-item 0 atomically adds work-group partial sum to global
            if (lid == 0) {
                using AtomF = sycl::atomic_ref<float,
                    sycl::memory_order::relaxed,
                    sycl::memory_scope::device,
                    sycl::access::address_space::global_space>;
                AtomF(*d_Score).fetch_add(lm_sum[0]);
            }
        });
    });
    gpu->q.wait();

    float h_score = 0.0f;
    gpu->q.memcpy(&h_score, d_Score, sizeof(float)).wait();
    return h_score + penalty;
}

// ===========================================================================
// FULL LOGP DOWNLOAD — DistDump debug mode only
//
// Transposes device cluster-major [c*nP+p] to host point-major [p*maxC+c].
// Only called when DistDump != 0.  Not on the hot path.
// ===========================================================================
void sycl_download_logp(KK_GPU *gpu, float *h_LogP,
                         int /*nClustersAlive*/, const int * /*h_AliveIndex*/)
{
    // Device LogP is cluster-major; host LogP is now also cluster-major.
    // Direct memcpy — no transpose required.
    const int nP   = gpu->nPoints;
    const int maxC = gpu->MaxClusters;
    gpu->q.memcpy(h_LogP, gpu->d_LogP, sizeof(float) * maxC * nP).wait();
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

    // Fill cluster-0 (noise) row of d_LogP: noiseLogP = -log(Weight[0]).
    // The main E-step kernel loops from cc=1 and never writes d_LogP[0*nP+p].
    // Without this fill the noise row contains uninitialised GPU memory, which
    // CStep reads and often mistakes for the best cluster (very negative garbage
    // values), assigning all points to noise and making ConsiderDeletion see
    // zero deletion loss for every real cluster.
    {
        const float noiseLogP = -std::log(h_Weight[0]);
        float *d_noiseRow = gpu->d_LogP;   // row 0: c=0, offset = 0 * nP
        const int nGroups = (nP + WG - 1) / WG;
        q.submit([&](sycl::handler &h) {
            h.parallel_for(sycl::nd_range<1>(nGroups * WG, WG),
                           [=](sycl::nd_item<1> it) {
                const int p = static_cast<int>(it.get_global_id(0));
                if (p < nP) d_noiseRow[p] = noiseLogP;
            });
        });
        // No wait needed — in-order queue; sycl_estep_submit is next
    }

    // Launch E-step kernel
    sycl_estep_submit(gpu, nClustersAlive, DistThresh, FullStep, log2piHalf);
    q.wait();

    // LogP remains device-resident after EStep.  The host h_LogP array is
    // only populated here when DistDump is active; otherwise ComputeScore()
    // runs a lightweight device reduction (sycl_compute_score) and CStep /
    // ConsiderDeletion / the DistThresh heuristic all operate on d_LogP
    // directly.  This eliminates the ~138 MB/step transfer that previously
    // dominated iteration time on large datasets.
    // h_LogP is non-null only when KK.cpp's DistDump path is active.
    // The caller (KK::EStep) already gates this on DistDump != 0.
    if (h_LogP) {
        sycl_download_logp(gpu, h_LogP, nClustersAlive, h_AliveIndex);
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
    const int nD = nDims, nD2 = nDims2, maxC = MaxClusters;
    (void)nPoints; // nPoints implicit in gpu->nPoints

    // Upload h_Class to d_Class.  During normal EM, d_Class is technically
    // already current from the preceding CStep, but uploading it here costs
    // only 1.4 MB (344k × 4 bytes) and is far cheaper than the accumulation
    // kernels that follow.  On the first MStep of Phase 3 this upload is
    // essential: the warm-start Class[] was assembled on the CPU from merged
    // per-chunk labels with no preceding GPU CStep, so d_Class would otherwise
    // contain stale data from before RunChunkedCEM, causing every cluster to
    // accumulate zero members and produce a singular covariance.
    q.memcpy(gpu->d_Class, h_Class, sizeof(int) * gpu->nPoints).wait();

    // AliveIndex may have changed due to ConsiderDeletion, so upload it once
    // for use by the normalisation kernels.
    q.memcpy(gpu->d_AliveIndex, h_AliveIndex, sizeof(int) * nClustersAlive).wait();

    // Zero accumulators (three independent memsets — no .wait() between them)
    q.memset(gpu->d_MeanAcc,  0, sizeof(float) * maxC * nD);
    q.memset(gpu->d_CovAcc,   0, sizeof(float) * maxC * nD2);
    q.memset(gpu->d_nMembers, 0, sizeof(int)   * maxC);
    q.wait();  // one barrier covers all three

    // Scatter mean accumulators
    sycl_mstep_mean_submit(gpu);
    q.wait();

    // Normalise mean in-place on device (MeanAcc -> Mean)
    sycl_mstep_normalise_mean_submit(gpu, nClustersAlive, gpu->d_AliveIndex);
    q.wait();

    // Scatter covariance accumulators (uses normalised d_Mean).
    // sycl_mstep_cov_submit issues its own q.wait() after all cluster passes.
    sycl_mstep_cov_submit(gpu);

    // Normalise covariance in-place on device (CovAcc -> Cov)
    sycl_mstep_normalise_cov_submit(gpu, nClustersAlive, gpu->d_AliveIndex);
    q.wait();

    // Download final normalised Mean and Cov to host for Cholesky (CPU-side).
    // These are the only two transfers remaining in the M-step.
    q.memcpy(h_Mean, gpu->d_Mean, sizeof(float) * maxC * nD).wait();
    q.memcpy(h_Cov,  gpu->d_Cov,  sizeof(float) * maxC * nD2).wait();

    // Weight is computed on host (deletion logic runs before sycl_mstep).
    // Upload so EStep can read it from device.
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
    KK_GPU  *gpu,
    float   *h_Loss,
    int      MaxClusters)
{
    // Zero loss accumulators
    gpu->q.memset(gpu->d_Loss, 0, sizeof(float) * MaxClusters).wait();

    sycl_deletion_loss_submit(gpu);
    gpu->q.wait();

    gpu->q.memcpy(h_Loss, gpu->d_Loss, sizeof(float) * MaxClusters).wait();

}

#endif // USE_SYCL
