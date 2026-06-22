// CUDA / cuFFT implementation of the GPU multitaper spectrogram backend.
//
// IMPORTANT: this translation unit is compiled only when USE_CUDA is enabled
// and the CUDA toolkit is present. It is NOT built or run in the CPU-only CI
// used to validate the rest of the spectral stack, so it must be validated on
// real GPU hardware. The numerical recipe (tapered, zero-padded segments ->
// batched real FFT -> eigenvalue-weighted, Parseval-normalised one-sided PSD)
// is identical to the CPU multitaperPsd, so agreement with the CPU path on a
// device is the correctness check (see the tolerant self-test comparison).
//
// Strategy: assemble all nCols*K tapered segments, run one batched cuFFT D2Z,
// and reduce to per-window PSDs. The batched FFT — the dominant cost — runs on
// the GPU; segment assembly and the PSD reduction are on the host (cheap by
// comparison and kernel-free, which keeps this first version simple). Any
// failure or an oversized problem returns false so the engine falls back to the
// CPU, making the GPU strictly an optional accelerator.

#include "spectralgpu.h"

#include <cuda_runtime.h>
#include <cufft.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace neuroscope {
namespace spectral {
namespace gpu {

namespace {

// Cap on device memory for the batched transform; above this we decline and
// let the CPU handle it rather than risk an allocation failure.
constexpr std::size_t kMaxDeviceBytes = 512u * 1024u * 1024u;

inline bool cudaOk(cudaError_t e) { return e == cudaSuccess; }

} // namespace

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

    // Decline very large problems (fall back to CPU streaming).
    const std::size_t inBytes  = batch * nfft  * sizeof(double);
    const std::size_t outBytes = batch * nbins * sizeof(cufftDoubleComplex);
    if (inBytes + outBytes > kMaxDeviceBytes) return false;

    // Taper weights (match the CPU normalisation exactly).
    std::vector<double> w(K);
    double wsum = 0.0;
    for (int k = 0; k < K; ++k) {
        w[k] = (weighting == TaperWeighting::Eigenvalue) ? tapers.lambda[k] : 1.0;
        wsum += w[k];
    }
    if (wsum <= 0.0) wsum = static_cast<double>(K);

    // Host input: batch x nfft, tapered and zero-padded.
    std::vector<double> hIn(batch * nfft, 0.0);
    for (int c = 0; c < nCols; ++c) {
        const int start = c * step;
        for (int k = 0; k < K; ++k) {
            double* seg = hIn.data() + (static_cast<std::size_t>(c) * K + k) * nfft;
            const std::vector<double>& h = tapers.taper[k];
            for (int i = 0; i < N; ++i) seg[i] = signal[start + i] * h[i];
        }
    }

    // Device buffers and a batched real-to-complex plan.
    double* dIn = nullptr;
    cufftDoubleComplex* dOut = nullptr;
    cufftHandle plan = 0;
    bool planned = false;
    auto cleanup = [&]() {
        if (planned) cufftDestroy(plan);
        if (dIn) cudaFree(dIn);
        if (dOut) cudaFree(dOut);
    };

    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dIn), inBytes)))  { cleanup(); return false; }
    if (!cudaOk(cudaMalloc(reinterpret_cast<void**>(&dOut), outBytes))){ cleanup(); return false; }
    if (!cudaOk(cudaMemcpy(dIn, hIn.data(), inBytes, cudaMemcpyHostToDevice))) {
        cleanup(); return false;
    }

    int nfftArr = nfft;
    if (cufftPlanMany(&plan, 1, &nfftArr,
                      &nfftArr, 1, nfft,         // input layout
                      &nfftArr, 1, nbins,        // output layout
                      CUFFT_D2Z, static_cast<int>(batch)) != CUFFT_SUCCESS) {
        cleanup(); return false;
    }
    planned = true;

    if (cufftExecD2Z(plan, dIn, dOut) != CUFFT_SUCCESS) { cleanup(); return false; }
    if (!cudaOk(cudaDeviceSynchronize())) { cleanup(); return false; }

    // Pull spectra back and reduce to per-window PSDs.
    std::vector<cufftDoubleComplex> hOut(batch * nbins);
    if (!cudaOk(cudaMemcpy(hOut.data(), dOut, outBytes, cudaMemcpyDeviceToHost))) {
        cleanup(); return false;
    }
    cleanup();

    const double norm = 1.0 / (wsum * fs);
    out.assign(nCols, std::vector<double>(nbins, 0.0));
    for (int c = 0; c < nCols; ++c) {
        std::vector<double>& psd = out[c];
        for (int k = 0; k < K; ++k) {
            const cufftDoubleComplex* X =
                hOut.data() + (static_cast<std::size_t>(c) * K + k) * nbins;
            const double wk = w[k];
            for (int b = 0; b < nbins; ++b)
                psd[b] += wk * (X[b].x * X[b].x + X[b].y * X[b].y);
        }
        for (int b = 0; b < nbins; ++b) {
            double v = psd[b] * norm;
            if (b != 0 && b != half) v *= 2.0;
            psd[b] = v;
        }
    }
    return true;
}

} // namespace gpu
} // namespace spectral
} // namespace neuroscope
