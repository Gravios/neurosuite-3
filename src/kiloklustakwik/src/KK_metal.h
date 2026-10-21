// KK_metal.h — KiloKlustaKwik's CEM steps on an Apple GPU (Metal, macOS on
// Apple silicon).  Design: doc/design/metal-backend.md, Phase 4.
//
// The same entry points and data contracts as KK_cuda.h / KK_hip.h, so KK.h
// maps gpu_* onto metal_* and the call sites are shared.  Differences:
//
//   * KK_GPU is opaque here (the Metal objects live in KK_metal.cpp), so
//     KK.h does not pull metal-cpp into every translation unit.
//   * allocate() returns false when the device cannot be used (no buffer of
//     the size needed, a kernel that does not compile, nDims > 64); the
//     caller then keeps the CPU path.
//   * Buffers are shared memory: "uploads" and "downloads" are memcpy.
//   * Float only (Apple GPUs have no double), like the CUDA kernels.  The
//     M-step, score and deletion loss use no float atomics: per-tile sums in
//     a fixed order, added on the host in double.  A run is deterministic,
//     and the means and covariances are at least as accurate as the CPU's
//     single float accumulator.
//   * A command buffer that fails aborts the run with a message, as
//     CUDA_CHECK/HIP_CHECK do.
//   * The E- and M-steps take KK's current nDims/nDims2, which can be fewer
//     than were loaded (CEMTwoPhase's spatial phase); the buffers are sized
//     for the loaded dimensions.
#pragma once
#ifdef USE_METAL

struct KK_GPU {
    struct Impl;
    Impl *impl = nullptr;

    int  nPoints = 0, nDims = 0, nDims2 = 0, MaxClusters = 0;
    bool initialised = false;

    bool allocate(int nP, int nD, int nD2, int maxC);
    void free_all();
};

extern "C" {

void metal_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor);

void metal_estep(
    KK_GPU      *gpu,
    const float *h_Mean,
    const float *h_Weight,
    const float *h_Chol,
    const int   *h_AliveIndex,
    const int   *h_Class,
    const int   *h_OldClass,
          float *h_LogP,
    int nClustersAlive, float DistThresh, int FullStep,
    double PI, int MaxClusters, int nDims, int nDims2);

void metal_mstep(
    KK_GPU    *gpu,
    const int *h_Class,
    const int *h_AliveIndex,
          float *h_Mean,
          float *h_Cov,
          float *h_Weight,
    int nClustersAlive, int MaxClusters,
    int nPoints, int nDims, int nDims2);

void metal_cstep(
    KK_GPU    *gpu,
          int *h_Class,
          int *h_OldClass,
          int *h_Class2,
    int nClustersAlive, int MaxClusters, float HugeScore);

void metal_deletion_loss(
    KK_GPU    *gpu,
          float *h_Loss,
    int MaxClusters);

float metal_compute_score(KK_GPU *gpu, float penalty);

void metal_download_logp(KK_GPU *gpu, float *h_LogP, int nClustersAlive,
                         const int *h_AliveIndex);

} // extern "C"

// True if the machine has a Metal device (KK_GPU::allocate can still fail).
bool kk_metal_device_available();

#endif // USE_METAL
