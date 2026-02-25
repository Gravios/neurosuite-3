/***************************************************************************
    process_spikegrouper_cuda.cu
    ----------------------------
    CUDA GPU path for process_spikegrouper.

    Implements steps 2-4 of the algorithm for one channel group:
      2. Per-channel noise threshold via histogram-based median (same two-pass
         radix approach as process_medianthreshold_cuda.cu).
      3. Threshold-crossing event detection with refractory enforcement.
      4. N×N spike-coincidence matrix.

    Kernel overview
    ---------------
    absKernel          : in-place abs() across all loaded samples.
    medianKernel       : one block per channel; histogram-based O(N) median.
    detectKernel       : one block per channel; stores event timestamps in a
                         pre-allocated per-channel event buffer.
    coincidenceKernel  : one block per (i,j) channel pair; counts coincident
                         events using binary search on sorted event lists.

    Data layout
    -----------
    .fil data is interleaved: sample t, channel c → data[t * nChannelsTotal + c]
    Only the channels belonging to the current group are processed.
    They are copied into a compact (nSamplesPerCh, groupSize) device buffer
    before processing so all kernels see stride-1 access within a channel.

    copyright  (C) 2025 neurosuite-3 contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <climits>

#define CUDA_CHECK(call)                                                       \
    do {                                                                       \
        cudaError_t _e = (call);                                               \
        if (_e != cudaSuccess) {                                               \
            fprintf(stderr, "[CUDA] %s:%d  %s\n",                             \
                    __FILE__, __LINE__, cudaGetErrorString(_e));               \
            exit(EXIT_FAILURE);                                                \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define HIST_THREADS   256
#define BUCKET_SIZE    256
// Maximum events per channel stored on GPU.
// For a 60-second window at 32 kHz with 3% firing rate: ~57,600 events.
// 1M gives large headroom for burst activity.
#define MAX_EVENTS_PER_CH  1048576

// ---------------------------------------------------------------------------
// Kernel 1: scatter interleaved input to compact per-channel layout
//           out[ch * nSamplesPerCh + t] = in[t * nChannelsTotal + groupChannels[ch]]
// ---------------------------------------------------------------------------
__global__ void scatterKernel(const short* __restrict__ in,
                               short*       __restrict__ out,
                               long int  nSamplesPerCh,
                               int       nChannelsTotal,
                               const int* __restrict__ groupChannels,
                               int       groupSize)
{
    long int idx = (long int)blockIdx.x * blockDim.x + threadIdx.x;
    long int total = (long int)nSamplesPerCh * groupSize;
    if (idx >= total) return;

    int   ch = (int)(idx / nSamplesPerCh);
    long int t  = idx % nSamplesPerCh;
    int   srcCh = groupChannels[ch];
    out[idx] = in[t * nChannelsTotal + srcCh];
}

// ---------------------------------------------------------------------------
// Kernel 2: abs in-place
// ---------------------------------------------------------------------------
__global__ void absKernel(short* data, long int n)
{
    long int idx = (long int)blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        short v = data[idx];
        if (v < 0) data[idx] = -v;
    }
}

// ---------------------------------------------------------------------------
// Kernel 3: histogram-based median per channel → sigma_n
//   gridDim.x = groupSize (one block per channel)
//   Compact layout: channel ch occupies data[ch * nSamplesPerCh .. (ch+1)*...]
// ---------------------------------------------------------------------------
__global__ void medianKernel(const short* __restrict__ data,
                              long int   nSamplesPerCh,
                              double*    sigmasOut)
{
    const int    ch     = blockIdx.x;
    const long int base = (long int)ch * nSamplesPerCh;
    const long int n    = nSamplesPerCh;
    const long int midIdx = n / 2;

    __shared__ unsigned int hist[BUCKET_SIZE];
    __shared__ unsigned int cumul;
    __shared__ unsigned int targetBucket;
    __shared__ unsigned int targetVal;

    // Pass 1: histogram of high byte
    for (int i = threadIdx.x; i < BUCKET_SIZE; i += blockDim.x) hist[i] = 0;
    __syncthreads();

    for (long int i = threadIdx.x; i < n; i += blockDim.x) {
        unsigned int v = (unsigned int)(unsigned short)data[base + i];
        atomicAdd(&hist[v >> 8], 1u);
    }
    __syncthreads();

    if (threadIdx.x == 0) {
        unsigned int count = 0;
        targetBucket = 0; cumul = 0;
        for (int b = 0; b < BUCKET_SIZE; ++b) {
            if (count + hist[b] > (unsigned int)midIdx) {
                targetBucket = (unsigned int)b;
                cumul = count;
                break;
            }
            count += hist[b];
        }
    }
    __syncthreads();

    // Pass 2: histogram of low byte within target bucket
    for (int i = threadIdx.x; i < BUCKET_SIZE; i += blockDim.x) hist[i] = 0;
    __syncthreads();

    const unsigned int hb = targetBucket;
    for (long int i = threadIdx.x; i < n; i += blockDim.x) {
        unsigned int v = (unsigned int)(unsigned short)data[base + i];
        if ((v >> 8) == hb)
            atomicAdd(&hist[v & 0xFF], 1u);
    }
    __syncthreads();

    if (threadIdx.x == 0) {
        unsigned int count = cumul;
        targetVal = (hb << 8);
        for (int b = 0; b < BUCKET_SIZE; ++b) {
            if (count + hist[b] > (unsigned int)midIdx) {
                targetVal = (hb << 8) | (unsigned int)b;
                break;
            }
            count += hist[b];
        }
        sigmasOut[ch] = (double)targetVal / 0.6745;
    }
}

// ---------------------------------------------------------------------------
// Kernel 4: threshold-crossing detection with refractory period
//   gridDim.x = groupSize
//   Each block processes one channel sequentially (inherently sequential
//   due to refractory dependency) but uses shared memory for the event buffer.
//
//   Output: eventCounts[ch], events[ch * MAX_EVENTS_PER_CH + 0..count-1]
// ---------------------------------------------------------------------------
__global__ void detectKernel(const short* __restrict__ data,
                              long int     nSamplesPerCh,
                              const double* __restrict__ sigmas,
                              double       thresholdFactor,
                              int          refractorySamples,
                              long int*    events,       // [groupSize * MAX_EVENTS_PER_CH]
                              int*         eventCounts)  // [groupSize]
{
    const int    ch     = blockIdx.x;
    const long int base = (long int)ch * nSamplesPerCh;
    const double  thr   = thresholdFactor * sigmas[ch];

    // Single-threaded sequential scan (refractory constraint)
    if (threadIdx.x != 0) return;

    long int* myEvents = events + (long int)ch * MAX_EVENTS_PER_CH;
    int count = 0;
    bool above = false;
    long int lastEvent = -(long int)refractorySamples - 1;

    for (long int s = 0; s < nSamplesPerCh; ++s) {
        short v = data[base + s];
        bool nowAbove = (v > (short)thr || v < -(short)thr);
        if (nowAbove && !above) {
            if (s - lastEvent >= refractorySamples) {
                if (count < MAX_EVENTS_PER_CH) {
                    myEvents[count++] = s;
                    lastEvent = s;
                }
            }
        }
        above = nowAbove;
    }
    eventCounts[ch] = count;
}

// ---------------------------------------------------------------------------
// Kernel 5: coincidence matrix
//   One block per (i,j) pair.  gridDim.x = groupSize, gridDim.y = groupSize.
//   For each event in channel i, binary search in channel j's event list.
//   Output: coinc[i * groupSize + j] = coincident / (count_i + 1)
// ---------------------------------------------------------------------------
__global__ void coincidenceKernel(const long int* __restrict__ events,
                                   const int*      __restrict__ eventCounts,
                                   int     groupSize,
                                   int     coincidenceSamples,
                                   double* coinc)   // [groupSize * groupSize]
{
    const int i = blockIdx.x;
    const int j = blockIdx.y;

    if (i == j) {
        if (threadIdx.x == 0) coinc[i * groupSize + j] = 1.0;
        return;
    }

    const long int* ei    = events + (long int)i * MAX_EVENTS_PER_CH;
    const long int* ej    = events + (long int)j * MAX_EVENTS_PER_CH;
    const int       ni    = eventCounts[i];
    const int       nj    = eventCounts[j];

    if (ni == 0 || nj == 0) {
        if (threadIdx.x == 0) coinc[i * groupSize + j] = 0.0;
        return;
    }

    // Each thread handles a subset of ei events
    __shared__ unsigned int sharedCount;
    if (threadIdx.x == 0) sharedCount = 0;
    __syncthreads();

    unsigned int localCount = 0;
    for (int k = threadIdx.x; k < ni; k += blockDim.x) {
        long int t  = ei[k];
        long int lo = t - coincidenceSamples;
        long int hi = t + coincidenceSamples;
        // Binary search in ej for lo
        int left = 0, right = nj - 1, found = 0;
        while (left <= right) {
            int mid = (left + right) / 2;
            if (ej[mid] >= lo) { right = mid - 1; found = mid; }
            else { left = mid + 1; found = left; }
        }
        // found is the first index >= lo
        if (found < nj && ej[found] <= hi) ++localCount;
    }
    atomicAdd(&sharedCount, localCount);
    __syncthreads();

    if (threadIdx.x == 0)
        coinc[i * groupSize + j] = (double)sharedCount / (double)(ni + 1);
}

// ---------------------------------------------------------------------------
// Host entry point
// ---------------------------------------------------------------------------
extern "C" void runCudaSpikeGrouper(
    const short*  hostData,
    long int      nSamplesTotal,       // nSamplesPerCh * nChannelsTotal
    long int      nSamplesPerCh,
    int           nChannelsTotal,
    const int*    groupChannels,
    int           groupSize,
    double        thresholdFactor,
    int           refractorySamples,
    int           coincidenceSamples,
    double*       sigmasOut,
    double*       coincOut,
    bool          verbose)
{
    int dev; cudaGetDevice(&dev);
    cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);
    if (verbose)
        printf("  [CUDA] GPU: %s  (%.0f MB VRAM)\n",
               prop.name, prop.totalGlobalMem / 1.0e6);

    // --- Upload interleaved data -------------------------------------------
    size_t inBytes = (size_t)nSamplesTotal * sizeof(short);
    short* d_in;
    CUDA_CHECK(cudaMalloc(&d_in, inBytes));
    CUDA_CHECK(cudaMemcpy(d_in, hostData, inBytes, cudaMemcpyHostToDevice));

    // --- Compact group channels into contiguous layout --------------------
    size_t compBytes = (size_t)nSamplesPerCh * groupSize * sizeof(short);
    short* d_compact;
    CUDA_CHECK(cudaMalloc(&d_compact, compBytes));

    int* d_groupChs;
    CUDA_CHECK(cudaMalloc(&d_groupChs, groupSize * sizeof(int)));
    CUDA_CHECK(cudaMemcpy(d_groupChs, groupChannels,
                          groupSize * sizeof(int), cudaMemcpyHostToDevice));

    {
        long int total = (long int)nSamplesPerCh * groupSize;
        int threads = 256;
        int blocks  = (int)((total + threads - 1) / threads);
        scatterKernel<<<blocks, threads>>>(d_in, d_compact,
                                           nSamplesPerCh, nChannelsTotal,
                                           d_groupChs, groupSize);
        CUDA_CHECK(cudaGetLastError());
    }
    cudaFree(d_in);
    cudaFree(d_groupChs);

    // --- Abs in-place -------------------------------------------------------
    {
        long int n = (long int)nSamplesPerCh * groupSize;
        int threads = 256;
        int blocks  = (int)((n + threads - 1) / threads);
        absKernel<<<blocks, threads>>>(d_compact, n);
        CUDA_CHECK(cudaGetLastError());
    }

    // --- Per-channel median → sigma_n --------------------------------------
    double* d_sigmas;
    CUDA_CHECK(cudaMalloc(&d_sigmas, groupSize * sizeof(double)));
    medianKernel<<<groupSize, HIST_THREADS>>>(d_compact, nSamplesPerCh, d_sigmas);
    CUDA_CHECK(cudaGetLastError());

    // --- Restore un-abs'd data for event detection (re-scatter) -----------
    // We need the signed data for threshold detection, so re-upload it
    cudaFree(d_compact);
    CUDA_CHECK(cudaMalloc(&d_in, inBytes));
    CUDA_CHECK(cudaMemcpy(d_in, hostData, inBytes, cudaMemcpyHostToDevice));
    CUDA_CHECK(cudaMalloc(&d_compact, compBytes));
    CUDA_CHECK(cudaMalloc(&d_groupChs, groupSize * sizeof(int)));
    CUDA_CHECK(cudaMemcpy(d_groupChs, groupChannels,
                          groupSize * sizeof(int), cudaMemcpyHostToDevice));
    {
        long int total = (long int)nSamplesPerCh * groupSize;
        int threads = 256;
        int blocks  = (int)((total + threads - 1) / threads);
        scatterKernel<<<blocks, threads>>>(d_in, d_compact,
                                           nSamplesPerCh, nChannelsTotal,
                                           d_groupChs, groupSize);
        CUDA_CHECK(cudaGetLastError());
    }
    cudaFree(d_in);
    cudaFree(d_groupChs);

    // --- Event detection ---------------------------------------------------
    size_t eventBufBytes = (size_t)groupSize * MAX_EVENTS_PER_CH * sizeof(long int);
    long int* d_events;
    int*      d_eventCounts;
    CUDA_CHECK(cudaMalloc(&d_events,      eventBufBytes));
    CUDA_CHECK(cudaMalloc(&d_eventCounts, groupSize * sizeof(int)));
    CUDA_CHECK(cudaMemset(d_eventCounts, 0, groupSize * sizeof(int)));

    detectKernel<<<groupSize, 1>>>(d_compact, nSamplesPerCh,
                                   d_sigmas, thresholdFactor,
                                   refractorySamples,
                                   d_events, d_eventCounts);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    cudaFree(d_compact);

    // Download sigmas
    CUDA_CHECK(cudaMemcpy(sigmasOut, d_sigmas,
                          groupSize * sizeof(double), cudaMemcpyDeviceToHost));
    cudaFree(d_sigmas);

    if (verbose) {
        // Print per-channel event counts — use malloc to stay STL-free
        int* counts = (int*)malloc((size_t)groupSize * sizeof(int));
        CUDA_CHECK(cudaMemcpy(counts, d_eventCounts,
                              (size_t)groupSize * sizeof(int), cudaMemcpyDeviceToHost));
        for (int i = 0; i < groupSize; ++i)
            printf("    ch_idx %3d: sigma_n=%.1f  threshold=%.1f  events=%d\n",
                   i, sigmasOut[i], thresholdFactor * sigmasOut[i], counts[i]);
        free(counts);
    }

    // --- Coincidence matrix -----------------------------------------------
    size_t coincBytes = (size_t)groupSize * groupSize * sizeof(double);
    double* d_coinc;
    CUDA_CHECK(cudaMalloc(&d_coinc, coincBytes));
    CUDA_CHECK(cudaMemset(d_coinc, 0, coincBytes));

    dim3 grid(groupSize, groupSize);
    coincidenceKernel<<<grid, HIST_THREADS>>>(
        d_events, d_eventCounts, groupSize, coincidenceSamples, d_coinc);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());

    cudaFree(d_events);
    cudaFree(d_eventCounts);

    // Download and symmetrise
    CUDA_CHECK(cudaMemcpy(coincOut, d_coinc, coincBytes, cudaMemcpyDeviceToHost));
    cudaFree(d_coinc);

    // Symmetrise on host
    for (int i = 0; i < groupSize; ++i)
        for (int j = i+1; j < groupSize; ++j) {
            double s = 0.5 * (coincOut[i*groupSize+j] + coincOut[j*groupSize+i]);
            coincOut[i*groupSize+j] = coincOut[j*groupSize+i] = s;
        }
}

// ---------------------------------------------------------------------------
// Device availability probe — called from process_spikegrouper.cpp so that
// the .cpp translation unit never needs to include cuda_runtime.h directly.
// ---------------------------------------------------------------------------
extern "C" int cudaHasDevice()
{
    int n = 0;
    cudaGetDeviceCount(&n);
    return n;
}
