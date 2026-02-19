// KK_cuda.h — declarations for GPU context and host-callable wrappers
// Included by KK.cpp when compiled with USE_CUDA.
#pragma once

#ifdef USE_CUDA
#include <cuda_runtime.h>
#include <vector>

struct KK_GPU {
    float *d_Data      = nullptr;
    float *d_Mean      = nullptr;
    float *d_MeanAcc   = nullptr;
    float *d_Cov       = nullptr;
    float *d_CovAcc    = nullptr;
    float *d_LogP      = nullptr;
    float *d_Weight    = nullptr;
    float *d_Chol      = nullptr;
    float *d_LogRootDet= nullptr;
    float *d_Loss      = nullptr;
    int   *d_Class     = nullptr;
    int   *d_OldClass  = nullptr;
    int   *d_Class2    = nullptr;
    int   *d_AliveIndex= nullptr;
    int   *d_nMembers  = nullptr;

    int  nPoints = 0, nDims = 0, nDims2 = 0, MaxClusters = 0;
    bool initialised = false;

    void allocate(int nP, int nD, int nD2, int maxC);
    void free_all();
};

extern "C" {
void cuda_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor);
void cuda_estep(KK_GPU *gpu,
    const float *h_Mean, const float *h_Weight, const float *h_Chol,
    const int *h_AliveIndex, const int *h_Class, const int *h_OldClass,
    float *h_LogP,
    int nClustersAlive, float DistThresh, int FullStep, double PI, int MaxClusters);
void cuda_mstep(KK_GPU *gpu,
    const int *h_Class, const int *h_AliveIndex,
    float *h_Mean, float *h_Cov, float *h_Weight,
    int nClustersAlive, int MaxClusters, int nPoints, int nDims, int nDims2);
void cuda_cstep(KK_GPU *gpu,
    const float *h_LogP, const int *h_AliveIndex,
    int *h_Class, int *h_OldClass, int *h_Class2,
    int nPoints, int nClustersAlive, int MaxClusters, float HugeScore);
void cuda_deletion_loss(KK_GPU *gpu,
    const float *h_LogP, const int *h_Class, const int *h_Class2,
    float *h_Loss, int nPoints, int MaxClusters);
}

// Detect CUDA device and return true if one is available
inline bool cuda_device_available() {
    int count = 0;
    return (cudaGetDeviceCount(&count) == cudaSuccess && count > 0);
}
#endif // USE_CUDA
