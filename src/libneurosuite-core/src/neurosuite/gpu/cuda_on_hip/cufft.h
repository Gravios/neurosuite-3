/***************************************************************************
 * cuda_on_hip/cufft.h -- the cuFFT calls of a CUDA file compiled as HIP,
 * as hipFFT (see cuda_runtime.h here).  What the spectral engine
 * (spectralgpu.cu) uses: batched double real-to-complex plans on a stream.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include "cuda_runtime.h"
#include <hipfft/hipfft.h>

#define cufftHandle           hipfftHandle
#define cufftResult           hipfftResult
#define cufftDoubleComplex    hipfftDoubleComplex
#define cufftDoubleReal       hipfftDoubleReal
#define cufftPlanMany         hipfftPlanMany
#define cufftExecD2Z          hipfftExecD2Z
#define cufftDestroy          hipfftDestroy
#define cufftSetStream        hipfftSetStream
#define CUFFT_SUCCESS         HIPFFT_SUCCESS
#define CUFFT_D2Z             HIPFFT_D2Z
