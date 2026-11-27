/***************************************************************************
 * cuda_on_hip/cuda_runtime.h -- a CUDA kernel file compiled as HIP
 *
 * Several GPU paths (the ndmanager plugins' filters and detectors,
 * KiloKlustaKwik's fiber pass) have one kernel file, written for CUDA.  A HIP
 * build compiles that same file with ROCm's clang (ns_gpu_add_cuda_as_hip in
 * cmake/GpuBackends.cmake), with this directory first on the include path:
 * its <cuda_runtime.h> is this header, which brings in HIP and names the HIP
 * runtime calls by their CUDA names.
 *
 * One kernel source for both, so the two cannot drift.  What it covers is
 * what those files use: device memory and copies, the device count and
 * properties, errors, synchronisation.  A kernel file that needs more
 * (streams, cuFFT, warp intrinsics with 32 lanes) adds its names here -- or
 * keeps a HIP file of its own, as Klusters' kernels do.
 *
 * NS_GPU_API_NAME is "HIP" here and "CUDA" in a CUDA build: the kernel files
 * use it in their messages.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <hip/hip_runtime.h>

#define NS_GPU_API_NAME "HIP"

#define cudaError_t                 hipError_t
#define cudaSuccess                 hipSuccess
#define cudaDeviceProp              hipDeviceProp_t
#define cudaMalloc                  hipMalloc
#define cudaFree                    hipFree
#define cudaMemcpy                  hipMemcpy
#define cudaMemset                  hipMemset
#define cudaMemcpyHostToDevice      hipMemcpyHostToDevice
#define cudaMemcpyDeviceToHost      hipMemcpyDeviceToHost
#define cudaMemcpyDeviceToDevice    hipMemcpyDeviceToDevice
#define cudaGetDeviceCount          hipGetDeviceCount
#define cudaGetDevice               hipGetDevice
#define cudaSetDevice               hipSetDevice
#define cudaGetDeviceProperties     hipGetDeviceProperties
#define cudaGetLastError            hipGetLastError
#define cudaPeekAtLastError         hipPeekAtLastError
#define cudaGetErrorString          hipGetErrorString
#define cudaDeviceSynchronize       hipDeviceSynchronize
