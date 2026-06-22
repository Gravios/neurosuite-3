#ifndef NEUROSCOPE_SPECTRAL_MULTITAPER_H
#define NEUROSCOPE_SPECTRAL_MULTITAPER_H

// Multitaper (Thomson) spectral estimation built on the DPSS tapers, plus a
// cross-channel "common" whitening transform.
//
// The estimator returns a one-sided power spectral density on the bins
// f_k = k * Fs / nfft for k = 0 .. nfft/2, normalised so that the integral
// over [0, Fs/2] approximates the segment variance (Parseval). Interior
// bins carry the usual factor of two for the folded negative frequencies.

#include "dpss.h"

#include <vector>

namespace neuroscope {
namespace spectral {

enum class TaperWeighting {
    Uniform,      // simple average over tapers
    Eigenvalue    // weight taper k by its concentration ratio lambda_k
};

// One-sided multitaper PSD of a single real segment.
//
// x      : nfft-or-shorter real segment; the first min(len, N) samples are
//          multiplied by each taper of length N. (Caller passes len == N.)
// tapers : precomputed DPSS for the segment length N.
// fs     : sampling rate (Hz); set to 1.0 for power-per-bin units.
// nfft   : transform length (power of two, >= N); zero-padding interpolates
//          the spectrum.
// Returns psd of length nfft/2 + 1.
std::vector<double> multitaperPsd(const double* x, int len,
                                  const DpssTapers& tapers,
                                  double fs, int nfft,
                                  TaperWeighting weighting = TaperWeighting::Eigenvalue);

// Sliding-window multitaper spectrogram of one channel (display mode
// "time vs frequency for a single channel").
//
// signal  : full length-n real channel signal.
// step    : hop between consecutive windows, in samples (> 0).
// out     : out[t] is the PSD column for window t (length nfft/2 + 1); the
//           number of columns is 1 + (n - N) / step when n >= N, else 0.
// Windows are processed in parallel (OpenMP) when available.
void multitaperSpectrogram(const double* signal, int n,
                           const DpssTapers& tapers,
                           double fs, int nfft, int step,
                           TaperWeighting weighting,
                           std::vector<std::vector<double>>& out);

// Frequencies (Hz) of the one-sided PSD bins for a given nfft and fs.
std::vector<double> psdFrequencies(int nfft, double fs);

// In-place ZCA "common" whitening across channels.
//
// data    : row-major [nChannels][nSamples], data[c*nSamples + s].
// Removes the per-channel mean, forms the nChannels x nChannels sample
// covariance, and applies its inverse square root (symmetric/ZCA whitening)
// so that correlated activity shared across channels (common-mode noise,
// reference drift) is suppressed before spectral estimation. nChannels is
// expected to be small (one display group); the symmetric eigenproblem is
// solved with cyclic Jacobi. eps regularises near-zero eigenvalues.
void commonWhiten(double* data, int nChannels, int nSamples, double eps = 1e-6);

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_MULTITAPER_H
