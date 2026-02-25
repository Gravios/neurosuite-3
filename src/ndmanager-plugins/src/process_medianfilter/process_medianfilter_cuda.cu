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

    Preallocation
    -------------
    The output file is preallocated to the exact input size using
    posix_fallocate before the streaming loop.  This reserves contiguous disk
    extents upfront, eliminating per-chunk metadata updates and fragmentation.

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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#ifdef __linux__
#  include <fcntl.h>
#endif

#define CUDA_CHECK(call)                                                        \
    do {                                                                        \
        cudaError_t _e = (call);                                               \
        if (_e != cudaSuccess) {                                               \
            fprintf(stderr, "CUDA error at %s:%d: %s\n",                       \
                    __FILE__, __LINE__, cudaGetErrorString(_e));               \
            exit(EXIT_FAILURE);                                                 \
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
    int  windowHalfLength)
{
    const int ch   = blockIdx.x * blockDim.x + threadIdx.x;
    const int tile = blockIdx.y * blockDim.y + threadIdx.y;
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
// Host-side state
// ---------------------------------------------------------------------------
struct CudaMedianFilter {
    int          nChannels;
    int          windowHalfLength;
    int          bx, by;
    size_t       smemPerBlock;
    size_t       bufBytes;      // padded input buffer size
    size_t       chunkBytes;    // unpadded output buffer size

    short*       h_ping;  short* d_ping_in;  short* d_ping_out;
    short*       h_pong;  short* d_pong_in;  short* d_pong_out;
    cudaStream_t stream[2];

    void init(int nCh, int wHL, int nSampPerChunk, const cudaDeviceProp& prop) {
        nChannels        = nCh;
        windowHalfLength = wHL;
        if (!selectBlockConfig(nCh, wHL, bx, by, smemPerBlock, prop)) {
            fprintf(stderr,
                "process_medianfilter: window half-length %d requires %zu bytes "
                "shared memory per block, but device limit is %zu bytes.\n"
                "Reduce windowHalfLength or use --cpu.\n",
                wHL, smemPerBlock, prop.sharedMemPerBlock);
            exit(EXIT_FAILURE);
        }
        const size_t padBytes = (size_t)nCh * wHL * sizeof(short);
        chunkBytes = (size_t)nSampPerChunk * sizeof(short);
        bufBytes   = chunkBytes + 2 * padBytes;

        CUDA_CHECK(cudaMallocHost(&h_ping, bufBytes));
        CUDA_CHECK(cudaMallocHost(&h_pong, bufBytes));
        CUDA_CHECK(cudaMalloc(&d_ping_in,  bufBytes));
        CUDA_CHECK(cudaMalloc(&d_ping_out, chunkBytes));
        CUDA_CHECK(cudaMalloc(&d_pong_in,  bufBytes));
        CUDA_CHECK(cudaMalloc(&d_pong_out, chunkBytes));
        CUDA_CHECK(cudaStreamCreate(&stream[0]));
        CUDA_CHECK(cudaStreamCreate(&stream[1]));
    }

    void destroy() {
        cudaFreeHost(h_ping); cudaFreeHost(h_pong);
        cudaFree(d_ping_in);  cudaFree(d_ping_out);
        cudaFree(d_pong_in);  cudaFree(d_pong_out);
        cudaStreamDestroy(stream[0]);
        cudaStreamDestroy(stream[1]);
    }

    void launchKernel(const short* d_in, short* d_out,
                      int nSampThisChunk, cudaStream_t s) const {
        const int padSamp          = nChannels * windowHalfLength;
        // Tile over per-channel samples, not total interleaved samples.
        const int nSampPerCh       = nSampThisChunk / nChannels;
        const int nTiles           = (nSampPerCh + TILE_SAMPLES - 1) / TILE_SAMPLES;
        dim3 block(bx, by);
        dim3 grid((nChannels + bx - 1) / bx,
                  (nTiles    + by - 1) / by);
        medianFilterKernel<<<grid, block, smemPerBlock, s>>>(
            d_in + padSamp, d_out, nSampPerCh, nChannels, windowHalfLength);
    }
};

// ---------------------------------------------------------------------------
// Public entry point called from main
// ---------------------------------------------------------------------------
extern "C" void runCudaMedianFilter(const char* inputPath,
                                     const char* outputPath,
                                     int   nChannels,
                                     int   windowHalfLength,
                                     long int chunkSizeBytes,
                                     bool  verbose)
{
    FILE* fin = fopen(inputPath, "rb");
    if (!fin) { perror(inputPath); exit(EXIT_FAILURE); }

    fseeko(fin, 0, SEEK_END);
    const long long fileSize = ftello(fin);
    fseeko(fin, 0, SEEK_SET);

    // Open output and preallocate to the exact input size.
    FILE* fout = fopen(outputPath, "wb");
    if (!fout) { perror(outputPath); fclose(fin); exit(EXIT_FAILURE); }
#ifdef __linux__
    {
        int fd = fileno(fout);
        if (fd >= 0) (void)posix_fallocate(fd, 0, (off_t)fileSize);
    }
#endif

    const int sampleSize = (int)sizeof(short);
    const int padSamples = nChannels * windowHalfLength;
    const long int padBytes = (long int)padSamples * sampleSize;

    int nSamplesPerChunk = (int)(chunkSizeBytes / sampleSize);
    nSamplesPerChunk = (nSamplesPerChunk / nChannels) * nChannels;
    if (nSamplesPerChunk < nChannels) nSamplesPerChunk = nChannels;
    const long int chunkBytes = (long int)nSamplesPerChunk * sampleSize;

    int dev; cudaGetDevice(&dev);
    cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);

    if (verbose) {
        int bx, by; size_t smem;
        selectBlockConfig(nChannels, windowHalfLength, bx, by, smem, prop);
        const int nSampPerCh = nSamplesPerChunk / nChannels;
        const int nTiles = (nSampPerCh + TILE_SAMPLES - 1) / TILE_SAMPLES;
        const int nThreads = ((nChannels + bx-1)/bx * bx) * ((nTiles + by-1)/by * by);
        printf("\n[CUDA medianfilter]\n");
        printf("  File:             %s  (%.2f GB)\n", inputPath, fileSize / 1e9);
        printf("  Channels:         %d\n", nChannels);
        printf("  Window half:      %d\n", windowHalfLength);
        printf("  Chunk:            %.1f MB  (%d samples/ch)\n",
               chunkBytes / 1e6, nSamplesPerChunk / nChannels);
        printf("  GPU:              %s  (%d SMs, %.0f MB VRAM)\n",
               prop.name, prop.multiProcessorCount, prop.totalGlobalMem / 1e6);
        printf("  Block:            %dx%d  smem: %.1f KB\n",
               bx, by, smem / 1024.0);
        printf("  Tiles/chunk:      %d  (TILE=%d samples)\n", nTiles, TILE_SAMPLES);
        printf("  Threads/chunk:    ~%d  (~%.0f warps/SM)\n",
               nThreads, (double)nThreads / 32.0 / prop.multiProcessorCount);
    }

    CudaMedianFilter mf;
    mf.init(nChannels, windowHalfLength, nSamplesPerChunk, prop);

    short** h_bufs = new short*[2]{ mf.h_ping, mf.h_pong };
    short** d_in   = new short*[2]{ mf.d_ping_in,  mf.d_pong_in  };
    short** d_out  = new short*[2]{ mf.d_ping_out, mf.d_pong_out };

    long long sizeLeft     = fileSize;
    long int  prevChunkBytes = 0;
    int       pong         = 0;
    bool      first        = true;

    while (sizeLeft > 0) {
        const int cur = pong;
        const int prv = 1 - pong;

        const long int readBytes = (sizeLeft < chunkBytes) ? (long int)sizeLeft : chunkBytes;
        const int nSampThisChunk = (int)(readBytes / sampleSize);
        short* hbuf = h_bufs[cur];

        if (first) {
            memset(hbuf, 0, padBytes);
            size_t got = fread(hbuf + padSamples, sampleSize,
                               nSampThisChunk + padSamples, fin);
            (void)got;
        } else {
            memcpy(hbuf,
                   h_bufs[prv] + prevChunkBytes / sampleSize,
                   2 * padBytes);
            size_t got = fread(hbuf + 2 * padSamples, sampleSize, nSampThisChunk, fin);
            (void)got;
        }

        if (!first) {
            CUDA_CHECK(cudaStreamSynchronize(mf.stream[prv]));
            size_t got = fwrite(h_bufs[prv] + padSamples, sampleSize,
                                prevChunkBytes / sampleSize, fout);
            (void)got;
        }

        CUDA_CHECK(cudaMemcpyAsync(d_in[cur], hbuf,
                                   readBytes + 2 * padBytes,
                                   cudaMemcpyHostToDevice, mf.stream[cur]));
        mf.launchKernel(d_in[cur], d_out[cur], nSampThisChunk, mf.stream[cur]);
        CUDA_CHECK(cudaMemcpyAsync(h_bufs[cur] + padSamples, d_out[cur],
                                   readBytes,
                                   cudaMemcpyDeviceToHost, mf.stream[cur]));

        prevChunkBytes = readBytes;
        sizeLeft      -= readBytes;
        first          = false;
        pong           = 1 - pong;
    }

    // Flush last chunk
    const int last = 1 - pong;
    CUDA_CHECK(cudaStreamSynchronize(mf.stream[last]));
    fwrite(h_bufs[last] + padSamples, sampleSize,
           prevChunkBytes / sampleSize, fout);

    mf.destroy();
    delete[] h_bufs; delete[] d_in; delete[] d_out;
    fclose(fin); fclose(fout);

    if (verbose) printf("  Done.\n");
}
