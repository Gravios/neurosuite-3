// KK_sycl.h — GPU context struct and host-callable wrapper declarations (SYCL/oneAPI).
//
// Mirrors KK_cuda.h exactly so that KK.cpp can select between backends with
// a single #if USE_CUDA / #elif USE_SYCL guard and identical call sites.
//
// DESIGN NOTES
// ------------
// Memory model: Unified Shared Memory (USM) device allocations via
//   sycl::malloc_device.  All buffers are allocated once in sycl_allocate()
//   and live for the lifetime of the KK instance.  Host<->device transfers
//   are explicit (sycl::queue::memcpy), matching the CUDA approach.
//
// Queue: a single in-order sycl::queue is owned by KK_GPU.  In-order means
//   submit order == execution order with no explicit event dependencies needed
//   for the sequential EStep -> MStep -> CStep -> Deletion pipeline.
//
// Data layout: identical to the CUDA backend —
//   d_Data  : dim-major    [d * nPoints + p]   uploaded once, coalesced E-step reads
//   d_LogP  : cluster-major [c * nPoints + p]  coalesced E-step writes
//   d_Mean  : cluster-major [c * nDims + d]
//   d_Chol  : [c * nDims2 + i*nDims + j]       lower-triangular per cluster
//   d_Class, d_Class2, d_OldClass : [nPoints]
//   d_Weight, d_Loss, d_LogRootDet : [MaxClusters]
//
// Atomic operations: SYCL 2020 sycl::atomic_ref with memory_order::relaxed
//   and memory_scope::device for the M-step scatter and deletion-loss reduce.
//   These are supported on Intel Arc (intel_gpu target) since oneAPI 2023.1.
//
// Work-group size: WORK_GROUP_SIZE=256 default (tunable at compile time via
//   -DKK_SYCL_WG=N).  Intel Arc Xe cores have 512 EU threads per subslice;
//   256 is a safe, portable default that avoids occupancy limits.
//
// Cholesky: stays on CPU, identical to CUDA backend.
#pragma once
#ifdef USE_SYCL

#include <sycl/sycl.hpp>
#include <cstdio>
#include <cstdlib>

#ifndef KK_SYCL_WG
#define KK_SYCL_WG 256
#endif

// ---------------------------------------------------------------------------
// KK_GPU — persistent GPU context, one per outer KK instance.
// Layout and member names are identical to the CUDA version.
// ---------------------------------------------------------------------------
struct KK_GPU {
    sycl::queue q;   // in-order; constructed by sycl_device_available() selector

    // Device buffers — USM device allocations
    float *d_Data       = nullptr;
    float *d_LogP       = nullptr;
    float *d_Mean       = nullptr;
    float *d_MeanAcc    = nullptr;
    float *d_Cov        = nullptr;
    float *d_CovAcc     = nullptr;
    float *d_Weight     = nullptr;
    float *d_Chol       = nullptr;
    float *d_LogRootDet = nullptr;
    float *d_Loss       = nullptr;
    int   *d_Class      = nullptr;
    int   *d_OldClass   = nullptr;
    int   *d_Class2     = nullptr;
    int   *d_AliveIndex = nullptr;
    int   *d_nMembers   = nullptr;

    int  nPoints = 0, nDims = 0, nDims2 = 0, MaxClusters = 0;
    bool initialised = false;

    // Constructs with the GPU selector passed in from sycl_device_available()
    explicit KK_GPU(sycl::device dev)
        : q(dev, sycl::property::queue::in_order{}) {}

    void allocate(int nP, int nD, int nD2, int maxC);
    void free_all();
};

// ---------------------------------------------------------------------------
// Host-callable wrappers — identical signatures to the CUDA versions so
// KK.cpp dispatch code needs only an #elif USE_SYCL substitution.
// Defined in KK_sycl.cpp.
// ---------------------------------------------------------------------------

// Upload transposed Data once after LoadData.
// h_Data_pointMajor: host array [nPoints * nDims] point-major.
void sycl_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor);

// E-step: compute LogP on GPU for all alive clusters c>=1.
// Cholesky factors pre-computed on CPU.  Downloads result, transposes to h_LogP.
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
    double PI, int MaxClusters);

// M-step: scatter-accumulate means and covariances on GPU, normalise on host.
void sycl_mstep(
    KK_GPU    *gpu,
    const int *h_Class,
    const int *h_AliveIndex,
          float *h_Mean,
          float *h_Cov,
          float *h_Weight,
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2);

// C-step: argmin LogP per point, entirely on GPU.
void sycl_cstep(
    KK_GPU    *gpu,
          int *h_Class,
          int *h_OldClass,
          int *h_Class2,
    int nClustersAlive, int MaxClusters, float HugeScore);

// Deletion-loss accumulation.
void sycl_deletion_loss(
    KK_GPU    *gpu,
          float *h_Loss,
    int MaxClusters);

// Returns true if at least one Intel GPU (or any GPU with SYCL support) exists.
// On success, fills *out_dev with the selected device for queue construction.
bool sycl_device_available(sycl::device *out_dev = nullptr);

#endif // USE_SYCL
