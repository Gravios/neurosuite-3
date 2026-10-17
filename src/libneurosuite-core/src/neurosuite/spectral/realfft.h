#ifndef NEUROSCOPE_SPECTRAL_REALFFT_H
#define NEUROSCOPE_SPECTRAL_REALFFT_H

// Minimal, dependency-free FFT for the spectral engine.
//
// neuroscope links neither FFTW nor any other transform library, so the
// multitaper estimator needs a self-contained FFT. A radix-2
// decimation-in-time transform is sufficient: the spectral engine always
// chooses nfft as a power of two (the next power of two >= the taper
// length), which is also the natural choice for a spectrogram.
//
// All routines operate on std::complex<double>. A thin real-input helper
// (rfftPowerOneSided) returns the one-sided power spectrum |X(f)|^2 for
// f = 0 .. nfft/2, which is what the multitaper estimator accumulates.

#include <vector>
#include <complex>
#include <cmath>
#include <cstddef>

namespace neuroscope {
namespace spectral {

// True iff n is a positive power of two.
inline bool isPowerOfTwo(std::size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

// Smallest power of two >= n (n >= 1).
inline std::size_t nextPowerOfTwo(std::size_t n)
{
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

// In-place iterative radix-2 FFT. n = x.size() must be a power of two.
// inverse == false : forward transform (no scaling).
// inverse == true  : inverse transform (divided by n).
inline void fftRadix2(std::vector<std::complex<double>>& x, bool inverse)
{
    const std::size_t n = x.size();
    if (n <= 1) return;

    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    const double sign = inverse ? 1.0 : -1.0;
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = sign * 2.0 * M_PI / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t k = 0; k < (len >> 1); ++k) {
                const std::complex<double> u = x[i + k];
                const std::complex<double> v = x[i + k + (len >> 1)] * w;
                x[i + k] = u + v;
                x[i + k + (len >> 1)] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        const double invN = 1.0 / static_cast<double>(n);
        for (auto& c : x) c *= invN;
    }
}

// One-sided power spectrum of a real, already-tapered segment.
//
// in        : nfft real samples (zero-padded by the caller if needed).
// outPower  : resized to nfft/2 + 1, outPower[k] = |X(k)|^2.
//
// No normalisation other than the raw |X|^2 is applied here; the
// multitaper estimator applies the sampling-rate / taper normalisation so
// that the result is a power spectral density.
inline void rfftPowerOneSided(const std::vector<double>& in,
                              std::vector<double>& outPower)
{
    const std::size_t nfft = in.size();
    std::vector<std::complex<double>> buf(nfft);
    for (std::size_t i = 0; i < nfft; ++i) buf[i] = std::complex<double>(in[i], 0.0);
    fftRadix2(buf, /*inverse=*/false);

    const std::size_t half = nfft / 2;
    outPower.resize(half + 1);
    for (std::size_t k = 0; k <= half; ++k) {
        const double re = buf[k].real();
        const double im = buf[k].imag();
        outPower[k] = re * re + im * im;
    }
}

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_REALFFT_H
