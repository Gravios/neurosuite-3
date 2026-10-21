/***************************************************************************
    process_medianfilter_cuda.cu
    CUDA implementation of the sliding-window median filter.

    Strategy
    --------
    Each thread handles one (channel, sample-tile) pair, not just one channel.
    This massively improves GPU occupancy for recordings with few channels
    (e.g. 32-384 channels) where the old single-thread-per-channel approach
    launched only 1-3 blocks and left the vast majority of SMs idle.

    Tiling
    ------
    The sample dimension is split into tiles of TILE_SAMPLES samples.
    Thread (ch, tile) processes samples [tile*TILE_SAMPLES .. (tile+1)*TILE_SAMPLES).
    Because the sliding window is stateful (each sample depends on the previous
    W samples), each tile first runs a warm-up pass of exactly wLen samples
    from just before its tile start to reconstruct the sorted window state,
    then produces TILE_SAMPLES output samples.

    The warm-up data is read from the globally visible padded input buffer
    (same padding scheme as before), so tiles are fully independent and run
    concurrently with zero inter-tile communication.

    Thread layout
    -------------
      blockDim.x = min(nChannels, 32)    -- warp-aligned channel dimension
      blockDim.y = BLOCK_TILES           -- tiles per block
      gridDim.x  = ceil(nChannels / blockDim.x)
      gridDim.y  = ceil(nTilesPerChunk / blockDim.y)

    Shared memory
    -------------
    The sorted sliding window per thread: windowLength shorts per thread,
    layout: sharedMem[(threadIdx.y * blockDim.x + threadIdx.x) * wLen].

    Host side
    ---------
    process_medianfilter.cpp reads the file in chunks, each with w frames of
    context on either side (zero outside the recording), for the CPU and the
    GPU alike; cudaMedianFilterChunk() filters one such chunk.  The output
    therefore equals the CPU path's (test/test_medianfilter.py checks it).

    Hardware target
    ---------------
    RTX 5070 Ti: 8960 CUDA cores, 70 SMs, 128 cores/SM, 16 GB VRAM.
    Old kernel: 384 channels -> 2 blocks -> ~3 active warps across 70 SMs.
    New kernel with TILE_SAMPLES=512 and a 128M-sample chunk:
      384 channels * ceil(128M/512) = 384 * 250000 = 96M threads
      -> full SM saturation throughout the entire chunk.

    copyright  (C) 2024-2025 neurosuite-3 contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/

#include <cuda_runtime.h>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

// On a CUDA error: report it and return false, so the caller continues on
// the CPU.
#define CUDA_TRY(call)                                                          \
    do {                                                                        \
        cudaError_t _e = (call);                                               \
        if (_e != cudaSuccess) {                                               \
            fprintf(stderr, "process_medianfilter: CUDA error at %s:%d: %s\n", \
                    __FILE__, __LINE__, cudaGetErrorString(_e));               \
            return false;                                                       \
        }                                                                       \
    } while (0)

// Number of output samples per tile per channel.
// Tune downward if register spilling is observed (nvcc --ptxas-options=-v).
#define TILE_SAMPLES  512

// Tiles per block along the y-axis.  Reduce if smem limit is hit.
#define BLOCK_TILES   4

// ---------------------------------------------------------------------------
// Device helpers
// ---------------------------------------------------------------------------

__device__ __forceinline__ static void
slidingWindowUpdate(short* __restrict__ win, int wLen, short oldV, short newV)
{
    if (newV == oldV) return;
    int lo = 0, hi = wLen - 1, idx = (lo + hi) / 2;
    while (lo <= hi) {
        idx = (lo + hi) / 2;
        if      (oldV > win[idx]) lo = idx + 1;
        else if (oldV < win[idx]) hi = idx - 1;
        else break;
    }
    if (newV > oldV) {
        while (idx < wLen - 1 && newV > win[idx + 1]) { win[idx] = win[idx + 1]; ++idx; }
        win[idx] = newV;
    } else {
        while (idx > 0 && newV < win[idx - 1]) { win[idx] = win[idx - 1]; --idx; }
        win[idx] = newV;
    }
}

__device__ __forceinline__ static void
insertionSort(short* arr, int n)
{
    for (int i = 1; i < n; ++i) {
        short key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) { arr[j + 1] = arr[j]; --j; }
        arr[j + 1] = key;
    }
}

// ---------------------------------------------------------------------------
// Main kernel: one thread per (channel, sample-tile).
//
// widebandData layout (interleaved, padded):
//   index = t * nChannels + ch,  t in [-windowHalfLength, nSamplesPerChunk + windowHalfLength)
//   The pointer passed in already points to t=0 (past the leading pad).
//
// filteredData layout: same interleaved, t in [0, nSamplesPerChunk).
//
// Indexing invariant
// ------------------
// output[s] = data[s] - median(data[s-wHL .. s+wHL])
//
// Each tile independently builds its window by reading the wLen samples
// centred at tileStart — indices [tileStart-wHL .. tileStart+wHL], all within
// the valid padded range since tileStart >= 0 and we have wHL padding samples.
// It then outputs sample tileStart directly (window already correct), then
// slides the window one step per sample for tileStart+1 .. tileEnd-1:
//   remove data[s-wHL-1]  (leftmost sample leaving the window)
//   add    data[s+wHL]    (rightmost sample entering the window)
// At s = tileStart+1, oldOff = (tileStart+1-wHL-1) = tileStart-wHL >= -wHL,
// which is always within the padded region — no out-of-bounds access.
// ---------------------------------------------------------------------------
__global__ void medianFilterKernel(
    const short* __restrict__ widebandData,
          short* __restrict__ filteredData,
    int  nSamplesPerChannel,
    int  nChannels,
    int  windowHalfLength,
    int  tileBase)          // first tile of this launch (grids are <= 65535 in y)
{
    const int ch   = blockIdx.x * blockDim.x + threadIdx.x;
    const int tile = tileBase + blockIdx.y * blockDim.y + threadIdx.y;
    if (ch >= nChannels) return;

    const int wLen      = 2 * windowHalfLength + 1;
    const int tileStart = tile * TILE_SAMPLES;
    if (tileStart >= nSamplesPerChannel) return;
    const int tileEnd   = min(tileStart + TILE_SAMPLES, nSamplesPerChannel);

    // Sorted window in shared memory
    extern __shared__ short sharedMem[];
    const int localIdx = threadIdx.y * blockDim.x + threadIdx.x;
    short* win = sharedMem + localIdx * wLen;

    // Warm-up: build the sorted window centred at tileStart.
    // Reads data[tileStart - wHL .. tileStart + wHL].
    // Minimum index: tileStart - wHL >= -wHL  (valid, covered by pad).
    // Maximum index: tileStart + wHL < nSamplesPerChunk + wHL (valid, within buffer).
    const long int warmBase = (long int)(tileStart - windowHalfLength) * nChannels + ch;
    for (int i = 0; i < wLen; ++i)
        win[i] = widebandData[warmBase + (long int)i * nChannels];
    insertionSort(win, wLen);

    // Output sample tileStart — window is already centred here.
    {
        const long int curOff = (long int)tileStart * nChannels + ch;
        filteredData[curOff] = widebandData[curOff] - win[windowHalfLength];
    }

    // Slide the window and output samples tileStart+1 .. tileEnd-1.
    // At step s: remove data[s - wHL - 1] (leaving), add data[s + wHL] (entering).
    // oldOff minimum: (tileStart+1) - wHL - 1 = tileStart - wHL >= -wHL  (valid).
    // newOff maximum: (tileEnd-1) + wHL < nSamplesPerChunk + wHL          (valid).
    for (int s = tileStart + 1; s < tileEnd; ++s) {
        const long int newOff = (long int)(s + windowHalfLength)     * nChannels + ch;
        const long int oldOff = (long int)(s - windowHalfLength - 1) * nChannels + ch;
        const long int curOff = (long int) s                         * nChannels + ch;

        slidingWindowUpdate(win, wLen, widebandData[oldOff], widebandData[newOff]);
        filteredData[curOff] = widebandData[curOff] - win[windowHalfLength];
    }
}

// ---------------------------------------------------------------------------
// Block configuration selector
// ---------------------------------------------------------------------------
static bool selectBlockConfig(int nChannels, int windowHalfLength,
                               int& bx, int& by, size_t& smem,
                               const cudaDeviceProp& prop)
{
    const int wLen = 2 * windowHalfLength + 1;
    bx = (nChannels >= 32) ? 32 : nChannels;
    by = BLOCK_TILES;
    smem = (size_t)bx * by * wLen * sizeof(short);
    while (smem > prop.sharedMemPerBlock && by > 1) {
        by /= 2;
        smem = (size_t)bx * by * wLen * sizeof(short);
    }
    return smem <= prop.sharedMemPerBlock;
}

// ---------------------------------------------------------------------------
// Device buffers, grown as needed and kept for the life of the program.
// ---------------------------------------------------------------------------
namespace {
struct DeviceBuffers {
    short* in = nullptr;
    short* out = nullptr;
    size_t inBytes = 0;
    size_t outBytes = 0;
    bool   reported = false;
};
DeviceBuffers buffers;

bool ensure(short*& ptr, size_t& have, size_t need)
{
    if (need <= have) return true;
    cudaFree(ptr);
    ptr = nullptr;
    have = 0;
    CUDA_TRY(cudaMalloc(&ptr, need));
    have = need;
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// Public entry point (process_medianfilter.cpp): filters nFrames frames.
// `data` points at frame 0 of an interleaved host buffer that also holds
// windowHalfLength frames of context before and after; `filtered` receives
// nFrames frames.  Returns false (message on stderr) if the GPU cannot.
// ---------------------------------------------------------------------------
extern "C" bool cudaMedianFilterChunk(const short* data, short* filtered, long long nFrames,
                                      int nChannels, int windowHalfLength, bool verbose)
{
    if (nFrames <= 0) return true;
    if (nFrames > INT_MAX) return false;

    int dev;
    CUDA_TRY(cudaGetDevice(&dev));
    cudaDeviceProp prop;
    CUDA_TRY(cudaGetDeviceProperties(&prop, dev));
    int bx, by;
    size_t smem;
    if (!selectBlockConfig(nChannels, windowHalfLength, bx, by, smem, prop)) {
        fprintf(stderr,
                "process_medianfilter: window half-length %d needs %zu bytes of shared "
                "memory per block, the device has %zu\n",
                windowHalfLength, smem, prop.sharedMemPerBlock);
        return false;
    }

    const size_t ctx = (size_t)windowHalfLength * nChannels;          // context samples
    const size_t inBytes  = ((size_t)nFrames * nChannels + 2 * ctx) * sizeof(short);
    const size_t outBytes = (size_t)nFrames * nChannels * sizeof(short);
    if (!ensure(buffers.in, buffers.inBytes, inBytes)) return false;
    if (!ensure(buffers.out, buffers.outBytes, outBytes)) return false;

    if (verbose && !buffers.reported) {
        printf("[CUDA medianfilter] %s: block %dx%d, %.1f KB shared, %d samples per tile\n",
               prop.name, bx, by, smem / 1024.0, TILE_SAMPLES);
        buffers.reported = true;
    }

    CUDA_TRY(cudaMemcpy(buffers.in, data - ctx, inBytes, cudaMemcpyHostToDevice));
    const int nPerCh = (int)nFrames;
    const int nTiles = (nPerCh + TILE_SAMPLES - 1) / TILE_SAMPLES;
    const int maxTilesPerLaunch = 65535 * by;
    dim3 block(bx, by);
    for (int tileBase = 0; tileBase < nTiles; tileBase += maxTilesPerLaunch) {
        const int tiles = std::min(maxTilesPerLaunch, nTiles - tileBase);
        dim3 grid((nChannels + bx - 1) / bx, (tiles + by - 1) / by);
        medianFilterKernel<<<grid, block, smem>>>(buffers.in + ctx, buffers.out, nPerCh,
                                                  nChannels, windowHalfLength, tileBase);
        CUDA_TRY(cudaGetLastError());
    }
    CUDA_TRY(cudaMemcpy(filtered, buffers.out, outBytes, cudaMemcpyDeviceToHost));
    return true;
}
