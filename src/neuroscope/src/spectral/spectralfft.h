#ifndef NEUROSCOPE_SPECTRAL_SPECTRALFFT_H
#define NEUROSCOPE_SPECTRAL_SPECTRALFFT_H

// Real-input power-spectrum backend for the multitaper estimator.
//
// When the build finds FFTW (HAVE_FFTW), this wraps an FFTW real-to-complex
// (r2c) plan: SIMD-vectorised, computes only the nfft/2+1 non-redundant bins
// of a real transform, and accepts an arbitrary (not necessarily power-of-
// two) nfft. Without FFTW it falls back to the in-tree radix-2 transform,
// which requires a power-of-two size, so the effective nfft is rounded up;
// callers must read the actual size back from nfft().
//
// power() is thread-safe: the FFTW plan is created once (guarded) and only
// the thread-safe new-array execute is used, with per-thread scratch, so the
// OpenMP spectrogram loop can share one plan across all windows.

#include "realfft.h"

#include <vector>

namespace neuroscope {
namespace spectral {

// True if the FFTW backend was compiled in.
bool fftwAvailable();

class RealFftPlan {
public:
    // Build a plan for a real transform. With FFTW the size is exactly
    // requestedNfft; without it, the next power of two >= requestedNfft.
    explicit RealFftPlan(int requestedNfft);
    ~RealFftPlan();

    RealFftPlan(const RealFftPlan&) = delete;
    RealFftPlan& operator=(const RealFftPlan&) = delete;

    int nfft() const { return nfft_; }            // effective transform length
    int nbins() const { return nfft_ / 2 + 1; }   // one-sided bin count

    // One-sided power spectrum of a real segment.
    //  in       : nfft() real samples (caller zero-pads).
    //  outPower : resized to nbins(); outPower[k] = |X(k)|^2.
    // Thread-safe across concurrent calls on the same plan.
    void power(const double* in, std::vector<double>& outPower) const;

private:
    int nfft_;
    void* impl_ = nullptr; // opaque FFTW plan handle when HAVE_FFTW, else null
};

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_SPECTRALFFT_H
