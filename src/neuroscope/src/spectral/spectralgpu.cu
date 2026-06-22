// CUDA / cuFFT implementation of the GPU multitaper spectrogram backend,
// pinned-host + multi-stream variant.
//
// IMPORTANT: compiled only when USE_CUDA is enabled and the CUDA toolkit is
// present; NOT built or run in the CPU-only CI. The per-element arithmetic and
// the window partitioning live in spectralgpu_kernels.h and are unit-tested on
// the CPU; only the kernel launches, the cuFFT calls and the stream/async
// plumbing here need real GPU hardware to validate.
//
// Offload + overlap: the signal, flattened tapers and weights are uploaded once
// to shared device buffers. The nCols windows are split into one contiguous
// chunk per CUDA stream (kern::chunkRange). Each stream independently builds its
// chunk's tapered segments (taperKernel), transforms them (a per-stream cuFFT
// plan bound with cufftSetStream), reduces them to PSDs (psdKernel) writing into
// the shared output at the chunk's column offset, and copies that chunk's PSDs
// back asynchronously into pinned host memory. Running the chunks on separate
// streams lets one chunk's device->host copy overlap another chunk's compute.
// Any failure or an oversized problem returns false so the engine falls back to
// the CPU, keeping the GPU strictly an optional accelerator.

#include "spectralgpu.h"
#include "spectralgpu_kernels.h"

#include <cuda_runtime.h>
#include <cufft.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace neuroscope {
namespace spectral {
namespace gpu {

namespace {
constexpr std::size_t kMaxDeviceBytes = 512u * 1024u * 1024u;
constexpr int kBlock = 256;
constexpr int kStreams = 2;          // chunks pipelined concurrently
inline bool cudaOk(cudaError_t e) { return e == cudaSuccess; }
} // namespace

// Tapered, zero-padded segments for one chunk; colOffset maps local windows to
// global windows so the signal is sampled from the right place.
__global__ void taperKernel(const double* signal, int sigLen,
                            const double* tapersFlat, int N, int nfft,
                            int step, int K, long total, int colOffset,
                            double* seg)
{
    const long gid = blockIdx.x * static_cast<long>(blockDim.x) + threadIdx.x;
    if (gid >= total) return;
    int localC, k, i;
    kern::decodeSeg(gid, nfft, K, localC, k, i);
    seg[gid] = kern::taperedSample(signal, sigLen, tapersFlat, N, step,
                                   colOffset + localC, k, i);
}

// Reduce one chunk's per-taper spectra to PSDs, writing into the shared output
// at the chunk's column offset.
__global__ void psdKernel(const double* spectraInterleaved, const double* w,
                          double wsum, double fs, int nbins, int half, int K,
                          long total, int colOffset, double* out)
{
    const long gid = blockIdx.x * static_cast<long>(blockDim.x) + threadIdx.x;
    if (gid >= total) return;
    int localC, b;
    kern::decodePsd(gid, nbins, localC, b);
    out[(static_cast<long>(colOffset) + localC) * nbins + b] =
        kern::psdValue(spectraInterleaved, w, wsum, fs, nbins, half, K, localC, b);
}

bool available()
{
    int n = 0;
    if (!cudaOk(cudaGetDeviceCount(&n))) return false;
    return n > 0;
}

bool multitaperSpectrogram(const double* signal, int n,
                           const DpssTapers& tapers,
                           double fs, int nfft, int step,
                           TaperWeighting weighting,
                           std::vector<std::vector<double>>& out)
{
    out.clear();
    const int N = tapers.N;
    const int K = tapers.K;
    if (!tapers.valid() || step <= 0 || n < N || nfft < N || fs <= 0.0)
        return false;

    const int nCols = 1 + (n - N) / step;
    const int half = nfft / 2;
    const int nbins = half + 1;

    // Upper-bound memory guard (whole problem).
    const std::size_t fullBatch = static_cast<std::size_t>(nCols) * K;
    const std::size_t segBytes  = fullBatch * nfft  * sizeof(double);
    const std::size_t specBytes = fullBatch * nbins * sizeof(cufftDoubleComplex);
    const std::size_t psdBytes  = static_cast<std::size_t>(nCols) * nbins * sizeof(double);
    if (segBytes + specBytes + psdBytes > kMaxDeviceBytes) return false;

    const int nStreams = std::min(kStreams, std::max(1, nCols));

    // Small shared host inputs.
    std::vector<double> tapersFlat(static_cast<std::size_t>(K) * N);
    for (int k = 0; k < K; ++k)
        for (int i = 0; i < N; ++i)
            tapersFlat[static_cast<std::size_t>(k) * N + i] = tapers.taper[k][i];
    std::vector<double> w(K);
    double wsum = 0.0;
    for (int k = 0; k < K; ++k) {
        w[k] = (weighting == TaperWeighting::Eigenvalue) ? tapers.lambda[k] : 1.0;
        wsum += w[k];
    }
    if (wsum <= 0.0) wsum = static_cast<double>(K);

    // Shared device buffers and pinned host output.
    double* dSignal = nullptr;
    double* dTapers = nullptr;
    double* dW = nullptr;
    double* dPsd = nullptr;
    double* hPsd = nullptr;
    std::vector<cudaStream_t> stream(nStreams, nullptr);
    std::vector<cufftHandle> plan(nStreams, 0);
    std::vector<bool> planned(nStreams, false);
    std::vector<double*> dSeg(nStreams, nullptr);
    std::vector<cufftDoubleComplex*> dSpec(nStreams, nullptr);

    auto cleanup = [&]() {
        for (int s = 0; s < nStreams; ++s) {
            if (planned[s]) cufftDestroy(plan[s]);
            if (dSeg[s]) cudaFree(dSeg[s]);
            if (dSpec[s]) cudaFree(dSpec[s]);
            if (stream[s]) cudaStreamDestroy(stream[s]);
        }
        if (dSignal) cudaFree(dSignal);
        if (dTapers) cudaFree(dTapers);
        if (dW) cudaFree(dW);
        if (dPsd) cudaFree(dPsd);
        if (hPsd) cudaFreeHost(hPsd);
    };

    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dSignal), static_cast<std::size_t>(n) * sizeof(double)))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dTapers), tapersFlat.size() * sizeof(double)))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dW), static_cast<std::size_t>(K) * sizeof(double)))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dPsd), psdBytes))) { cleanup(); return false; }
    if (!cudaOk(cudaMallocHost(reinterpret_cast<void**>(&hPsd), psdBytes))) { cleanup(); return false; }

    if (!cudaOk(cudaMemcpy(dSignal, signal, static_cast<std::size_t>(n) * sizeof(double), cudaMemcpyHostToDevice))) { cleanup(); return false; }
    if (!cudaOk(cudaMemcpy(dTapers, tapersFlat.data(), tapersFlat.size() * sizeof(double), cudaMemcpyHostToDevice))) { cleanup(); return false; }
    if (!cudaOk(cudaMemcpy(dW, w.data(), static_cast<std::size_t>(K) * sizeof(double), cudaMemcpyHostToDevice))) { cleanup(); return false; }

    // Per-stream resources sized to each chunk.
    for (int s = 0; s < nStreams; ++s) {
        int start, count;
        kern::chunkRange(nStreams, nCols, s, start, count);
        if (count <= 0) continue;
        const std::size_t cbatch = static_cast<std::size_t>(count) * K;
        if (!cudaOk(cudaStreamCreate(&stream[s]))) { cleanup(); return false; }
        if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dSeg[s]), cbatch * nfft * sizeof(double)))) { cleanup(); return false; }
        if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dSpec[s]), cbatch * nbins * sizeof(cufftDoubleComplex)))) { cleanup(); return false; }
        int nfftArr = nfft;
        if (cufftPlanMany(&plan[s], 1, &nfftArr,
                          &nfftArr, 1, nfft,
                          &nfftArr, 1, nbins,
                          CUFFT_D2Z, static_cast<int>(cbatch)) != CUFFT_SUCCESS) { cleanup(); return false; }
        planned[s] = true;
        if (cufftSetStream(plan[s], stream[s]) != CUFFT_SUCCESS) { cleanup(); return false; }
    }

    // Pipeline: each chunk runs taper -> FFT -> reduce -> async copy-back on its
    // own stream, so copies and compute of different chunks overlap.
    for (int s = 0; s < nStreams; ++s) {
        int start, count;
        kern::chunkRange(nStreams, nCols, s, start, count);
        if (count <= 0) continue;
        const std::size_t cbatch = static_cast<std::size_t>(count) * K;

        const long segTotal = static_cast<long>(cbatch) * nfft;
        const long segGrid = (segTotal + kBlock - 1) / kBlock;
        taperKernel<<<static_cast<unsigned int>(segGrid), kBlock, 0, stream[s]>>>(
            dSignal, n, dTapers, N, nfft, step, K, segTotal, start, dSeg[s]);
        if (!cudaOk(cudaGetLastError())) { cleanup(); return false; }

        if (cufftExecD2Z(plan[s], dSeg[s], dSpec[s]) != CUFFT_SUCCESS) { cleanup(); return false; }

        const long psdTotal = static_cast<long>(count) * nbins;
        const long psdGrid = (psdTotal + kBlock - 1) / kBlock;
        psdKernel<<<static_cast<unsigned int>(psdGrid), kBlock, 0, stream[s]>>>(
            reinterpret_cast<const double*>(dSpec[s]), dW, wsum, fs, nbins, half, K,
            psdTotal, start, dPsd);
        if (!cudaOk(cudaGetLastError())) { cleanup(); return false; }

        const std::size_t off = static_cast<std::size_t>(start) * nbins;
        if (!cudaOk(cudaMemcpyAsync(hPsd + off, dPsd + off,
                                    static_cast<std::size_t>(count) * nbins * sizeof(double),
                                    cudaMemcpyDeviceToHost, stream[s]))) { cleanup(); return false; }
    }

    for (int s = 0; s < nStreams; ++s)
        if (stream[s] && !cudaOk(cudaStreamSynchronize(stream[s]))) { cleanup(); return false; }

    out.assign(nCols, std::vector<double>(nbins, 0.0));
    for (int c = 0; c < nCols; ++c) {
        double* dst = out[c].data();
        const double* src = hPsd + static_cast<std::size_t>(c) * nbins;
        for (int b = 0; b < nbins; ++b) dst[b] = src[b];
    }
    cleanup();
    return true;
}

} // namespace gpu
} // namespace spectral
} // namespace neuroscope
