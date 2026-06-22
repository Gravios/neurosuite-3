// CUDA / cuFFT implementation of the GPU multitaper spectrogram backend.
//
// IMPORTANT: this translation unit is compiled only when USE_CUDA is enabled
// and the CUDA toolkit is present. It is NOT built or run in the CPU-only CI
// used to validate the rest of the spectral stack, so it must be validated on
// real GPU hardware. The per-element arithmetic (index decomposition, tapering,
// and the eigenvalue-weighted, Parseval-normalised reduction) lives in
// spectralgpu_kernels.h as __host__ __device__ functions and is unit-tested on
// the CPU, so only the kernel launches and cuFFT plumbing are unverified here.
//
// Full offload: only the signal, the flattened tapers and the taper weights are
// uploaded; the tapered/zero-padded segments are built on the device by
// taperKernel, transformed by one batched cuFFT D2Z, and reduced to per-window
// one-sided PSDs by psdKernel. Only nCols*nbins doubles are copied back. Any
// failure or an oversized problem returns false so the engine falls back to the
// CPU, keeping the GPU strictly an optional accelerator.

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
inline bool cudaOk(cudaError_t e) { return e == cudaSuccess; }
} // namespace

// Build the tapered, zero-padded segments on the device (batch*nfft doubles).
__global__ void taperKernel(const double* signal, int sigLen,
                            const double* tapersFlat, int N, int nfft,
                            int step, int K, long total, double* seg)
{
    const long gid = blockIdx.x * static_cast<long>(blockDim.x) + threadIdx.x;
    if (gid >= total) return;
    int c, k, i;
    kern::decodeSeg(gid, nfft, K, c, k, i);
    seg[gid] = kern::taperedSample(signal, sigLen, tapersFlat, N, step, c, k, i);
}

// Reduce the per-taper complex spectra to one-sided PSDs (nCols*nbins doubles).
__global__ void psdKernel(const double* spectraInterleaved, const double* w,
                          double wsum, double fs, int nbins, int half, int K,
                          long total, double* out)
{
    const long gid = blockIdx.x * static_cast<long>(blockDim.x) + threadIdx.x;
    if (gid >= total) return;
    int c, b;
    kern::decodePsd(gid, nbins, c, b);
    out[gid] = kern::psdValue(spectraInterleaved, w, wsum, fs, nbins, half, K, c, b);
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
    const std::size_t batch = static_cast<std::size_t>(nCols) * K;

    const std::size_t segBytes  = batch * nfft  * sizeof(double);
    const std::size_t specBytes = batch * nbins * sizeof(cufftDoubleComplex);
    const std::size_t psdBytes  = static_cast<std::size_t>(nCols) * nbins * sizeof(double);
    if (segBytes + specBytes + psdBytes > kMaxDeviceBytes) return false;

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

    double* dSignal = nullptr;
    double* dTapers = nullptr;
    double* dW = nullptr;
    double* dSeg = nullptr;
    cufftDoubleComplex* dSpec = nullptr;
    double* dPsd = nullptr;
    cufftHandle plan = 0;
    bool planned = false;
    auto cleanup = [&]() {
        if (planned) cufftDestroy(plan);
        if (dSignal) cudaFree(dSignal);
        if (dTapers) cudaFree(dTapers);
        if (dW) cudaFree(dW);
        if (dSeg) cudaFree(dSeg);
        if (dSpec) cudaFree(dSpec);
        if (dPsd) cudaFree(dPsd);
    };

    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dSignal), static_cast<std::size_t>(n) * sizeof(double)))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dTapers), tapersFlat.size() * sizeof(double)))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dW), static_cast<std::size_t>(K) * sizeof(double)))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dSeg), segBytes))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dSpec), specBytes))) { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dPsd), psdBytes))) { cleanup(); return false; }

    if (!cudaOk(cudaMemcpy(dSignal, signal, static_cast<std::size_t>(n) * sizeof(double), cudaMemcpyHostToDevice))) { cleanup(); return false; }
    if (!cudaOk(cudaMemcpy(dTapers, tapersFlat.data(), tapersFlat.size() * sizeof(double), cudaMemcpyHostToDevice))) { cleanup(); return false; }
    if (!cudaOk(cudaMemcpy(dW, w.data(), static_cast<std::size_t>(K) * sizeof(double), cudaMemcpyHostToDevice))) { cleanup(); return false; }

    const long segTotal = static_cast<long>(batch) * nfft;
    const long segGrid = (segTotal + kBlock - 1) / kBlock;
    taperKernel<<<static_cast<unsigned int>(segGrid), kBlock>>>(
        dSignal, n, dTapers, N, nfft, step, K, segTotal, dSeg);
    if (!cudaOk(cudaGetLastError())) { cleanup(); return false; }

    int nfftArr = nfft;
    if (cufftPlanMany(&plan, 1, &nfftArr,
                      &nfftArr, 1, nfft,
                      &nfftArr, 1, nbins,
                      CUFFT_D2Z, static_cast<int>(batch)) != CUFFT_SUCCESS) {
        cleanup(); return false;
    }
    planned = true;
    if (cufftExecD2Z(plan, dSeg, dSpec) != CUFFT_SUCCESS) { cleanup(); return false; }

    const long psdTotal = static_cast<long>(nCols) * nbins;
    const long psdGrid = (psdTotal + kBlock - 1) / kBlock;
    psdKernel<<<static_cast<unsigned int>(psdGrid), kBlock>>>(
        reinterpret_cast<const double*>(dSpec), dW, wsum, fs, nbins, half, K,
        psdTotal, dPsd);
    if (!cudaOk(cudaGetLastError())) { cleanup(); return false; }
    if (!cudaOk(cudaDeviceSynchronize())) { cleanup(); return false; }

    std::vector<double> hPsd(static_cast<std::size_t>(nCols) * nbins);
    if (!cudaOk(cudaMemcpy(hPsd.data(), dPsd, psdBytes, cudaMemcpyDeviceToHost))) {
        cleanup(); return false;
    }
    cleanup();

    out.assign(nCols, std::vector<double>(nbins, 0.0));
    for (int c = 0; c < nCols; ++c) {
        double* dst = out[c].data();
        const double* src = hPsd.data() + static_cast<std::size_t>(c) * nbins;
        for (int b = 0; b < nbins; ++b) dst[b] = src[b];
    }
    return true;
}

} // namespace gpu
} // namespace spectral
} // namespace neuroscope
