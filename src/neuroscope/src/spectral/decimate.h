#ifndef NEUROSCOPE_SPECTRAL_DECIMATE_H
#define NEUROSCOPE_SPECTRAL_DECIMATE_H

// Anti-aliased integer decimation by a power of two, for spectral analysis of a
// band that sits well below Nyquist. Low-passing and downsampling by M shrinks
// the transform that follows (same frequency resolution in the band at 1/M the
// samples), at the cost of an anti-alias filter and small edge effects.

#include <vector>

namespace neuroscope {
namespace spectral {

// Largest power-of-two decimation factor whose post-decimation Nyquist
// (fs / (2M)) still clears the highest frequency of interest with the given
// margin. Returns 1 (no decimation) when freqHigh <= 0 (full band) or when no
// factor >= 2 is worthwhile. maxFactor caps the result; minOutLen keeps the
// decimated signal long enough to remain useful (the factor is reduced until
// n / M >= minOutLen).
int decimationFactor(double fs, double freqHigh, double marginFactor,
                     int maxFactor, int n, int minOutLen);

// Anti-aliased decimation of a real signal by factor M (a power of two >= 1).
//  in/n : input signal.
//  M    : decimation factor (rounded down to a power of two; M <= 1 copies).
//  out  : filtered + downsampled signal, length n / M, time-aligned with the
//         input (the linear-phase FIR group delay is compensated). Applied as
//         repeated half-band /2 stages so each stage uses a short filter.
void decimate(const double* in, int n, int M, std::vector<double>& out);

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_DECIMATE_H
