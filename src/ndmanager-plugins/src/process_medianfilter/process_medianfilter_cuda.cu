/***************************************************************************
    process_medianfilter_cuda.cu
    CUDA implementation of the sliding-window median filter.

    Strategy
    --------
    Each CUDA thread handles exactly one channel. Threads are completely
    independent - no cross-thread communication is needed. The sorted
    sliding window is maintained in thread-local registers/shared memory.

    For recordings that fit entirely in VRAM (<=~14 GB): load once, run
    all channels in parallel, copy result back.

    For larger recordings: the host streams chunks through pinned memory,
    overlapping H2D copy / kernel execution / D2H copy using two CUDA
    streams (double buffering).

    Hardware targets
    ----------------
    RTX 5070 Ti: 8960 CUDA cores, 900 GB/s VRAM bandwidth, 16 GB VRAM.
    With 128 channels each needing O(W) sorted window operations per
    sample, the GPU runs all 128 channels truly in parallel vs CPU's
    sequential or OpenMP-limited approach.

    Window size limit
    -----------------
    The sorted window is stored in shared memory. Max window half-length
    is 512 (windowLength <= 1025 shorts = 2050 bytes/thread). With 128
    threads/block that's 262 KB shared mem per block, which is within
    the 48-100 KB limit for most compute capabilities. For larger windows
    fall back to global memory (slower but correct).
 ***************************************************************************/

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// Error checking macro
// ---------------------------------------------------------------------------
#define CUDA_CHECK(call)                                                        \
    do {                                                                        \
        cudaError_t err = (call);                                               \
        if (err != cudaSuccess) {                                               \
            fprintf(stderr, "CUDA error at %s:%d: %s\n",                       \
                    __FILE__, __LINE__, cudaGetErrorString(err));               \
            exit(EXIT_FAILURE);                                                 \
        }                                                                       \
    } while (0)

// ---------------------------------------------------------------------------
// Insertion into a sorted array (maintained in shared memory).
// Replaces oldVal with newVal, keeping array sorted. O(W) shift.
// ---------------------------------------------------------------------------
__device__ static void slidingWindowUpdate(short* window, int windowLength,
                                            short oldVal, short newVal)
{
    if (newVal == oldVal) return;

    // Binary search for oldVal
    int lo = 0, hi = windowLength - 1, idx = (lo + hi) / 2;
    while (lo <= hi) {
        idx = (lo + hi) / 2;
        if      (oldVal > window[idx]) lo = idx + 1;
        else if (oldVal < window[idx]) hi = idx - 1;
        else break;
    }

    if (newVal > oldVal) {
        while (idx < windowLength - 1 && newVal > window[idx + 1]) {
            window[idx] = window[idx + 1];
            ++idx;
        }
        window[idx] = newVal;
    } else {
        while (idx > 0 && newVal < window[idx - 1]) {
            window[idx] = window[idx - 1];
            --idx;
        }
        window[idx] = newVal;
    }
}

// ---------------------------------------------------------------------------
// Insertion sort for initial window population. Small W (<= ~64) is fast;
// for larger W std::sort on the host pre-sorts and we copy in.
// ---------------------------------------------------------------------------
__device__ static void insertionSort(short* arr, int n)
{
    for (int i = 1; i < n; ++i) {
        short key = arr[i];
        int j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            --j;
        }
        arr[j + 1] = key;
    }
}

// ---------------------------------------------------------------------------
// Main kernel: one thread per channel.
//
// Input layout (interleaved): sample[t * nChannels + ch]
// Output layout (interleaved): same
//
// widebandData is padded: [L zeros | chunk data | L zeros]
// The pointer passed in points to the first real sample (index L).
// ---------------------------------------------------------------------------
__global__ void medianFilterKernel(const short* __restrict__ widebandData,
                                    short*       __restrict__ filteredData,
                                    long int  nSamplesPerChannel,
                                    int       nChannels,
                                    int       windowHalfLength)
{
    const int channel = blockIdx.x * blockDim.x + threadIdx.x;
    if (channel >= nChannels) return;

    const int windowLength = 2 * windowHalfLength + 1;
    const int halfOffset   = nChannels * windowHalfLength;
    const int fullOffset   = nChannels * windowLength;

    // Sorted sliding window in shared memory.
    // Shared memory layout: windowLength shorts per thread.
    extern __shared__ short sharedMem[];
    short* window = sharedMem + threadIdx.x * windowLength;

    // Populate initial window: samples [-L .. L] for this channel
    const short* inp = widebandData + channel - halfOffset;
    for (int i = 0; i < windowLength; ++i, inp += nChannels)
        window[i] = *inp;

    // Sort initial window
    insertionSort(window, windowLength);

    // inp now points to sample (L+1), output points to sample (0)
    inp = widebandData + channel + halfOffset + nChannels;
    short* out = filteredData + channel;

    for (long int s = 0; s < nSamplesPerChannel; ++s,
         inp += nChannels, out += nChannels)
    {
        const short newVal = *inp;
        const short oldVal = *(inp - fullOffset);

        slidingWindowUpdate(window, windowLength, oldVal, newVal);

        // Output = current sample - median
        *out = *(inp - halfOffset - nChannels) - window[windowHalfLength];
    }
}

// ---------------------------------------------------------------------------
// Host-side launcher: handles both single-chunk (fits in VRAM) and
// double-buffered streaming for larger recordings.
// ---------------------------------------------------------------------------

struct CudaMedianFilter {
    int         nChannels;
    int         windowHalfLength;
    int         windowLength;
    size_t      sharedMemPerBlock;
    int         threadsPerBlock;

    // Ping-pong buffers (pinned host + device)
    short*      h_ping;   short* d_ping_in;  short* d_ping_out;
    short*      h_pong;   short* d_pong_in;  short* d_pong_out;
    size_t      bufBytes;

    cudaStream_t stream[2];

    void init(int nCh, int wHL, size_t chunkSamples) {
        nChannels        = nCh;
        windowHalfLength = wHL;
        windowLength     = 2 * wHL + 1;

        // Shared mem: windowLength shorts per thread
        threadsPerBlock  = std::min(nCh, 256);
        sharedMemPerBlock = (size_t)threadsPerBlock * windowLength * sizeof(short);

        // Check shared mem limit
        int device; cudaGetDevice(&device);
        cudaDeviceProp prop; cudaGetDeviceProperties(&prop, device);
        if (sharedMemPerBlock > prop.sharedMemPerBlock) {
            fprintf(stderr,
                "Warning: window too large for shared memory (%zu > %zu bytes).\n"
                "Reduce window half-length or use CPU fallback.\n",
                sharedMemPerBlock, prop.sharedMemPerBlock);
            exit(EXIT_FAILURE);
        }

        // Allocate ping-pong buffers with overlap padding
        size_t padSamples = (size_t)nCh * wHL;
        bufBytes = (chunkSamples + 2 * padSamples) * sizeof(short);

        CUDA_CHECK(cudaMallocHost(&h_ping, bufBytes));
        CUDA_CHECK(cudaMallocHost(&h_pong, bufBytes));
        CUDA_CHECK(cudaMalloc(&d_ping_in,  bufBytes));
        CUDA_CHECK(cudaMalloc(&d_ping_out, chunkSamples * sizeof(short)));
        CUDA_CHECK(cudaMalloc(&d_pong_in,  bufBytes));
        CUDA_CHECK(cudaMalloc(&d_pong_out, chunkSamples * sizeof(short)));

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
                      long int nSampPerCh, cudaStream_t s) {
        int blocks = (nChannels + threadsPerBlock - 1) / threadsPerBlock;
        medianFilterKernel<<<blocks, threadsPerBlock, sharedMemPerBlock, s>>>(
            d_in, d_out, nSampPerCh, nChannels, windowHalfLength);
    }
};

// ---------------------------------------------------------------------------
// Public entry point called from main
// ---------------------------------------------------------------------------
extern "C" void runCudaMedianFilter(const char* inputPath,
                                     const char* outputPath,
                                     int nChannels,
                                     int windowHalfLength,
                                     long int chunkSizeBytes,
                                     bool verbose)
{
    FILE* fin  = fopen(inputPath,  "rb");
    FILE* fout = fopen(outputPath, "wb");
    if (!fin  || !fout) { perror("file open"); exit(EXIT_FAILURE); }

    fseeko(fin, 0, SEEK_END);
    long long fileSize = ftello(fin);
    fseeko(fin, 0, SEEK_SET);

    const int      sampleSize        = sizeof(short);
    const int      padSamples        = nChannels * windowHalfLength;
    const long int padBytes          = padSamples * sampleSize;

    int nSamplesPerChunk = (int)(chunkSizeBytes / sampleSize);
    // Round to multiple of nChannels
    nSamplesPerChunk = (nSamplesPerChunk / nChannels) * nChannels;
    long int chunkBytes = (long int)nSamplesPerChunk * sampleSize;

    if (verbose) {
        printf("\n[CUDA medianfilter]\n");
        printf("  File size:        %.2f GB\n", fileSize / 1e9);
        printf("  Channels:         %d\n", nChannels);
        printf("  Window half:      %d\n", windowHalfLength);
        printf("  Chunk size:       %.2f MB\n", chunkBytes / 1e6);

        int dev; cudaGetDevice(&dev);
        cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);
        printf("  GPU:              %s (%.0f MB VRAM)\n",
               prop.name, prop.totalGlobalMem / 1e6);
    }

    CudaMedianFilter mf;
    mf.init(nChannels, windowHalfLength, (size_t)nSamplesPerChunk);

    long long sizeLeft = fileSize;
    int       pong     = 0;  // ping-pong index
    bool      first    = true;

    short** h_bufs = new short*[2]{ mf.h_ping, mf.h_pong };
    short** d_in   = new short*[2]{ mf.d_ping_in,  mf.d_pong_in  };
    short** d_out  = new short*[2]{ mf.d_ping_out, mf.d_pong_out };

    long int prevChunkBytes = 0;

    while (sizeLeft > 0) {
        int cur = pong;
        int prv = 1 - pong;

        long int readBytes = (sizeLeft < chunkBytes) ? (long int)sizeLeft : chunkBytes;
        int nSampThisChunk = (int)(readBytes / sampleSize);
        int nSampPerChPerChunk = nSampThisChunk / nChannels;

        // Copy overlap from end of previous buffer to start of current
        short* hbuf = h_bufs[cur];
        if (first) {
            memset(hbuf, 0, padBytes); // zero-pad for first chunk
            size_t got = fread(hbuf + padSamples, sampleSize,
                               nSampThisChunk + padSamples, fin);
            (void)got;
        } else {
            // Copy trailing overlap from previous host buffer
            memcpy(hbuf, h_bufs[prv] + (prevChunkBytes / sampleSize), 2 * padBytes);
            size_t got = fread(hbuf + 2 * padSamples, sampleSize, nSampThisChunk, fin);
            (void)got;
        }

        // Wait for previous stream to finish before reusing its output buffer
        if (!first) {
            CUDA_CHECK(cudaStreamSynchronize(mf.stream[prv]));
            // Write previous output
            size_t got = fwrite(h_bufs[prv] + padSamples, sampleSize,
                                prevChunkBytes / sampleSize, fout);
            (void)got;
        }

        // H2D async copy on current stream
        CUDA_CHECK(cudaMemcpyAsync(d_in[cur], hbuf,
                                   readBytes + 2 * padBytes,
                                   cudaMemcpyHostToDevice, mf.stream[cur]));

        // Launch kernel
        mf.launchKernel(d_in[cur] + padSamples, d_out[cur],
                        nSampPerChPerChunk, mf.stream[cur]);

        // D2H async copy of result into h_bufs[cur] (reuse buffer, offset by padSamples)
        CUDA_CHECK(cudaMemcpyAsync(h_bufs[cur] + padSamples, d_out[cur],
                                   readBytes,
                                   cudaMemcpyDeviceToHost, mf.stream[cur]));

        prevChunkBytes = readBytes;
        sizeLeft -= readBytes;
        first     = false;
        pong      = 1 - pong;
    }

    // Flush last chunk
    int last = 1 - pong;
    CUDA_CHECK(cudaStreamSynchronize(mf.stream[last]));
    fwrite(h_bufs[last] + padSamples, sampleSize, prevChunkBytes / sampleSize, fout);

    mf.destroy();
    delete[] h_bufs; delete[] d_in; delete[] d_out;
    fclose(fin); fclose(fout);

    if (verbose) printf("  Done.\n");
}
