/***************************************************************************
    process_medianthreshold_cuda.cu

    CUDA implementation of the median-based spike threshold estimator.
    Implements: threshold = 4 * sigma_n, where sigma_n = median(|x|) / 0.6745
    (Quiroga et al., Neural Computation 2004)

    Algorithm
    ---------
    Phase 1 – Absolute value: one kernel, all samples in parallel.
    Phase 2 – Per-channel median via radix-based partial sort (GPU nth_element).
              Each channel gets its own block of threads that cooperatively
              sort only enough of the data to expose the median element.
              Uses a histogram-based approach to avoid a full sort:
              - Two passes of radix over the 16-bit values
              - First pass: find which 256-value bucket the median falls in
              - Second pass: find exact value within that bucket
              This is O(N) with a small constant and fully data-parallel.

    For recordings that fit in VRAM: single-shot.
    For larger: stream channel-by-channel through VRAM (each channel's
    data is copied, processed, threshold computed, GPU memory reused).
 ***************************************************************************/

#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

#define CUDA_CHECK(call)                                                   \
    do {                                                                   \
        cudaError_t _e = (call);                                           \
        if (_e != cudaSuccess) {                                           \
            fprintf(stderr, "CUDA error %s:%d  %s\n",                     \
                    __FILE__, __LINE__, cudaGetErrorString(_e));           \
            exit(EXIT_FAILURE);                                            \
        }                                                                  \
    } while (0)

// ---------------------------------------------------------------------------
// Phase 1: abs() in-place across all samples
// ---------------------------------------------------------------------------
__global__ void absKernel(short* data, long int n)
{
    long int idx = (long int)blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        short v = data[idx];
        data[idx] = (v < 0) ? -v : v;
    }
}

// ---------------------------------------------------------------------------
// Phase 2: histogram-based median for a single channel's data.
// gridDim.x  = number of channels (one block per channel)
// blockDim.x = HIST_THREADS (power of 2, e.g. 256)
//
// Each block works on one channel's worth of samples.
// Uses 16-bit values (0..32767 after abs), two-pass radix histogram.
// ---------------------------------------------------------------------------
#define HIST_THREADS 256
#define BUCKET_SIZE  256   // 2^8 buckets per pass, covers 16-bit in 2 passes

__global__ void medianKernel(const short* __restrict__ data,
                              long int    nSamplesPerChannel,
                              int         nChannels,
                              double*     sigmas)   // output: one sigma_n per channel
{
    // Each block handles one channel
    const int   ch        = blockIdx.x;
    const long int offset = (long int)ch;   // interleaved layout: ch, ch+nCh, ch+2*nCh...
    const long int stride = nChannels;
    const long int n      = nSamplesPerChannel;
    const long int median_idx = n / 2;      // index of median in sorted order

    __shared__ unsigned int hist[BUCKET_SIZE];   // histogram buckets
    __shared__ unsigned int cumul;               // cumulative count before target bucket
    __shared__ unsigned int targetBucket;        // which bucket contains the median
    __shared__ unsigned int targetVal;           // final median value

    // ---- Pass 1: histogram of high byte (bits 15..8) ----
    for (int i = threadIdx.x; i < BUCKET_SIZE; i += blockDim.x)
        hist[i] = 0;
    __syncthreads();

    for (long int i = threadIdx.x; i < n; i += blockDim.x) {
        unsigned int v = (unsigned int)(unsigned short)data[offset + i * stride];
        atomicAdd(&hist[v >> 8], 1u);
    }
    __syncthreads();

    // Find which high-byte bucket the median falls in (thread 0)
    if (threadIdx.x == 0) {
        unsigned int count = 0;
        targetBucket = 0;
        cumul        = 0;
        for (int b = 0; b < BUCKET_SIZE; ++b) {
            if (count + hist[b] > (unsigned int)median_idx) {
                targetBucket = (unsigned int)b;
                cumul        = count;
                break;
            }
            count += hist[b];
        }
    }
    __syncthreads();

    // ---- Pass 2: histogram of low byte within target high-byte bucket ----
    for (int i = threadIdx.x; i < BUCKET_SIZE; i += blockDim.x)
        hist[i] = 0;
    __syncthreads();

    const unsigned int hb = targetBucket;
    for (long int i = threadIdx.x; i < n; i += blockDim.x) {
        unsigned int v = (unsigned int)(unsigned short)data[offset + i * stride];
        if ((v >> 8) == hb)
            atomicAdd(&hist[v & 0xFF], 1u);
    }
    __syncthreads();

    // Find exact value (thread 0)
    if (threadIdx.x == 0) {
        unsigned int count = cumul;
        targetVal = (hb << 8);   // default fallback
        for (int b = 0; b < BUCKET_SIZE; ++b) {
            if (count + hist[b] > (unsigned int)median_idx) {
                targetVal = (hb << 8) | (unsigned int)b;
                break;
            }
            count += hist[b];
        }
        // sigma_n = median / 0.6745
        sigmas[ch] = (double)targetVal / 0.6745;
    }
}

// ---------------------------------------------------------------------------
// Host entry point
// ---------------------------------------------------------------------------
extern "C" void runCudaMedianThreshold(const short* hostData,
                                        long int     nSamplesPerChannel,
                                        int          nChannels,
                                        double*      sigmasOut,   // length nChannels
                                        bool         verbose)
{
    long int totalSamples = nSamplesPerChannel * nChannels;
    size_t   dataBytes    = (size_t)totalSamples * sizeof(short);
    size_t   outBytes     = (size_t)nChannels    * sizeof(double);

    int dev; cudaGetDevice(&dev);
    cudaDeviceProp prop; cudaGetDeviceProperties(&prop, dev);

    if (verbose) {
        printf("[CUDA medianthreshold]\n");
        printf("  GPU:     %s\n", prop.name);
        printf("  Data:    %.2f MB (%d channels x %ld samples)\n",
               dataBytes / 1e6, nChannels, nSamplesPerChannel);
    }

    // Upload data
    short*  d_data;
    double* d_sigmas;
    CUDA_CHECK(cudaMalloc(&d_data,   dataBytes));
    CUDA_CHECK(cudaMalloc(&d_sigmas, outBytes));
    CUDA_CHECK(cudaMemcpy(d_data, hostData, dataBytes, cudaMemcpyHostToDevice));

    // Phase 1: abs in-place
    {
        int threads = 256;
        long int blocks = (totalSamples + threads - 1) / threads;
        absKernel<<<(int)blocks, threads>>>(d_data, totalSamples);
        CUDA_CHECK(cudaGetLastError());
    }

    // Phase 2: per-channel median (one block per channel)
    {
        medianKernel<<<nChannels, HIST_THREADS>>>(
            d_data, nSamplesPerChannel, nChannels, d_sigmas);
        CUDA_CHECK(cudaGetLastError());
    }

    CUDA_CHECK(cudaDeviceSynchronize());

    // Download results
    CUDA_CHECK(cudaMemcpy(sigmasOut, d_sigmas, outBytes, cudaMemcpyDeviceToHost));

    cudaFree(d_data);
    cudaFree(d_sigmas);

    if (verbose) printf("  Done.\n");
}
