#ifndef NEUROSCOPE_SPECTRAL_SPECTRALFFT_H
#define NEUROSCOPE_SPECTRAL_SPECTRALFFT_H

// Real-input power-spectrum backend for the multitaper estimator.
//
// Three transform providers, chosen per plan:
//
//   Fftw   (HAVE_FFTW)  an FFTW real-to-complex plan: SIMD-vectorised,
//                       arbitrary (not necessarily power-of-two) size.
//   Vdsp   (HAVE_VDSP)  Apple Accelerate's vDSP real DFT (macOS only):
//                       sizes 2^n and f*2^n (f = 3, 5, 15; n >= 4).
//   Radix2 (always)     the in-tree radix-2 transform in realfft.h:
//                       power-of-two sizes only.
//
// FftBackend::Auto takes the first provider that can do the requested size
// exactly: Fftw, then Vdsp. If neither can, it rounds the size up to the next
// power of two and takes Vdsp (when built) or Radix2. Callers must therefore
// read the effective size back from nfft(). The choice depends only on the
// size and the providers built in, so RealFftPlan(RealFftPlan(n).nfft())
// always has the same size as RealFftPlan(n). The spectral engine relies on
// that: it sizes one plan and builds the spectrogram's plan from its nfft().
//
// An explicit provider is used whenever it is built in (and, for vDSP, passes
// its start-up self-check, see spectralfft.cpp): exactly at the requested size
// if it can, else at the next power of two. One that is not usable falls back
// to Auto. The environment variable NS_SPECTRAL_FFT (fftw | vdsp | radix2)
// sets the provider of every plan built with Auto, for comparing providers
// inside NeuroScope.
//
// power() is thread-safe: plans are created once and each concurrent call
// uses its own scratch (and, for vDSP, its own setup from a pool), so the
// OpenMP spectrogram loop can share one plan across all windows.

#include "realfft.h"

#include <memory>
#include <vector>

namespace neuroscope {
namespace spectral {

enum class FftBackend { Auto, Fftw, Vdsp, Radix2 };

// True if the FFTW / vDSP provider was compiled in. Radix2 is always built.
bool fftwAvailable();
bool vdspAvailable();

// "auto", "fftw", "vdsp", "radix2".
const char* fftBackendName(FftBackend backend);

class RealFftPlan {
public:
    // Build a plan for a real transform of requestedNfft samples (sizes below
    // 2 become 2) with the preferred provider, or by the Auto rule above.
    explicit RealFftPlan(int requestedNfft, FftBackend preferred = FftBackend::Auto);
    ~RealFftPlan();

    RealFftPlan(const RealFftPlan&) = delete;
    RealFftPlan& operator=(const RealFftPlan&) = delete;

    int nfft() const { return nfft_; }            // effective transform length
    int nbins() const { return nfft_ / 2 + 1; }   // one-sided bin count
    FftBackend backend() const { return backend_; } // never Auto

    // One-sided power spectrum of a real segment.
    //  in       : nfft() real samples (caller zero-pads).
    //  outPower : resized to nbins(); outPower[k] = |X(k)|^2, the unscaled
    //             DFT X(k) = sum_t x[t] exp(-2 pi i k t / nfft), whichever
    //             provider computes it.
    // Thread-safe across concurrent calls on the same plan.
    void power(const double* in, std::vector<double>& outPower) const;

private:
    struct Impl;                    // provider handles, defined in the .cpp
    int nfft_ = 2;
    FftBackend backend_ = FftBackend::Radix2;
    std::unique_ptr<Impl> impl_;
};

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_SPECTRALFFT_H
