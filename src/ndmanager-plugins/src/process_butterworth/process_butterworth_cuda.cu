/***************************************************************************
    process_butterworth_cuda.cu  --  CUDA path of process_butterworth

    One thread per (channel, block) unit runs bwFilterBlock (butterworth_core.h,
    shared with the CPU path) on the chunk held in device memory.  Unit u maps to
    channel u % nCh and block u / nCh, so the threads of a warp read consecutive
    channels of the same interleaved row (coalesced).  The forward-pass scratch is
    laid out element-major (element i of unit u at scratch[i * nUnits + u]) for the
    same reason.

    copyright  (C) 2026 neurosuite-3 contributors
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>

#include "butterworth_core.h"

#define BW_CUDA_CHECK(call)                                                   \
    do {                                                                      \
        cudaError_t e_ = (call);                                              \
        if (e_ != cudaSuccess) {                                              \
            fprintf(stderr, "process_butterworth: CUDA error at %s:%d: %s\n", \
                    __FILE__, __LINE__, cudaGetErrorString(e_));              \
            exit(EXIT_FAILURE);                                               \
        }                                                                     \
    } while (0)

template <typename T>
__global__ void bwKernel(const short* chunk, long long row0, long long nRows, int nCh,
                         long long c0, long long c1, int M, long long B,
                         BwCoefs<T> c, T* scratch, short* out)
{
    const long long nBlk   = (c1 - c0 + B - 1) / B;
    const long long nUnits = nBlk * nCh;
    const long long u = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (u >= nUnits) return;
    const int ch = (int)(u % nCh);
    const long long s = c0 + (u / nCh) * B;
    const long long e = (s + B < c1) ? s + B : c1;
    bwFilterBlock<T>(chunk, row0, nRows, nCh, ch, s, e, M, c, scratch + u, nUnits, out, c0);
}

struct BwCudaHandle {
    int useDouble, nCh, M;
    long long B, maxChunkRows, maxOutRows, maxUnits;
    short* dIn;
    short* dOut;
    void* dScratch;
    BwCoefs<double> cd;
    BwCoefs<float> cf;
};

extern "C" int bwCudaAvailable(int verbose)
{
    int n = 0;
    if (cudaGetDeviceCount(&n) != cudaSuccess || n < 1) {
        cudaGetLastError();                                  // clear the error state
        if (verbose) printf("  no usable CUDA device; using the CPU path\n");
        return 0;
    }
    if (verbose) {
        cudaDeviceProp p;
        if (cudaGetDeviceProperties(&p, 0) == cudaSuccess)
            printf("  CUDA device: %s (%d SMs, %.1f GB)\n", p.name, p.multiProcessorCount,
                   p.totalGlobalMem / 1073741824.0);
    }
    return 1;
}

extern "C" void* bwCudaInit(int useDouble, const double* sos, int nSec, int nCh,
                            long long maxChunkRows, long long maxOutRows, int M, int B)
{
    BwCudaHandle* h = new BwCudaHandle();
    h->useDouble = useDouble; h->nCh = nCh; h->M = M; h->B = B;
    h->maxChunkRows = maxChunkRows; h->maxOutRows = maxOutRows;
    h->maxUnits = ((maxOutRows + B - 1) / B) * nCh;
    h->cd.nSec = h->cf.nSec = nSec;
    for (int k = 0; k < nSec; ++k) {
        h->cd.b0[k] = sos[5 * k + 0]; h->cd.b1[k] = sos[5 * k + 1]; h->cd.b2[k] = sos[5 * k + 2];
        h->cd.a1[k] = sos[5 * k + 3]; h->cd.a2[k] = sos[5 * k + 4];
        h->cf.b0[k] = (float)sos[5 * k + 0]; h->cf.b1[k] = (float)sos[5 * k + 1];
        h->cf.b2[k] = (float)sos[5 * k + 2]; h->cf.a1[k] = (float)sos[5 * k + 3];
        h->cf.a2[k] = (float)sos[5 * k + 4];
    }
    const size_t elem = useDouble ? sizeof(double) : sizeof(float);
    BW_CUDA_CHECK(cudaMalloc(&h->dIn,  (size_t)maxChunkRows * nCh * sizeof(short)));
    BW_CUDA_CHECK(cudaMalloc(&h->dOut, (size_t)maxOutRows * nCh * sizeof(short)));
    BW_CUDA_CHECK(cudaMalloc(&h->dScratch, (size_t)(B + 2LL * M) * (size_t)h->maxUnits * elem));
    return h;
}

extern "C" void bwCudaChunk(void* handle, const short* chunk, long long row0, long long rows,
                            long long nRows, long long c0, long long c1, short* out)
{
    BwCudaHandle* h = static_cast<BwCudaHandle*>(handle);
    BW_CUDA_CHECK(cudaMemcpy(h->dIn, chunk, (size_t)rows * h->nCh * sizeof(short),
                             cudaMemcpyHostToDevice));
    const long long nUnits = ((c1 - c0 + h->B - 1) / h->B) * h->nCh;
    const int threads = 128;
    const unsigned int grid = (unsigned int)((nUnits + threads - 1) / threads);
    if (h->useDouble)
        bwKernel<double><<<grid, threads>>>(h->dIn, row0, nRows, h->nCh, c0, c1, h->M, h->B,
                                           h->cd, static_cast<double*>(h->dScratch), h->dOut);
    else
        bwKernel<float><<<grid, threads>>>(h->dIn, row0, nRows, h->nCh, c0, c1, h->M, h->B,
                                          h->cf, static_cast<float*>(h->dScratch), h->dOut);
    BW_CUDA_CHECK(cudaGetLastError());
    BW_CUDA_CHECK(cudaMemcpy(out, h->dOut, (size_t)(c1 - c0) * h->nCh * sizeof(short),
                             cudaMemcpyDeviceToHost));
}

extern "C" void bwCudaFree(void* handle)
{
    BwCudaHandle* h = static_cast<BwCudaHandle*>(handle);
    cudaFree(h->dIn); cudaFree(h->dOut); cudaFree(h->dScratch);
    delete h;
}
