#ifndef NEUROSCOPE_SPECTRAL_DPSS_H
#define NEUROSCOPE_SPECTRAL_DPSS_H

// Discrete prolate spheroidal sequences (Slepian tapers).
//
// For a window of length N and time-half-bandwidth product NW, the first K
// DPSS tapers are the K most spectrally concentrated unit-energy sequences
// of length N whose energy lies in the band [-W, W], W = NW/N (cycles per
// sample). They are the eigenvectors of the sinc concentration kernel and,
// equivalently, of a symmetric tridiagonal matrix that commutes with it
// (Slepian 1978; Percival & Walden 1993, ch. 8).
//
// Because neuroscope links no LAPACK, the tridiagonal eigenproblem is
// solved here directly: Sturm-sequence bisection isolates the K largest
// eigenvalues and inverse iteration recovers each eigenvector. The
// concentration ratio lambda_k (fraction of energy inside the band, in
// [0,1], decreasing in k) is then computed from the sinc kernel and is
// available for eigenvalue weighting.

#include <vector>

namespace neuroscope {
namespace spectral {

struct DpssTapers {
    int N = 0;                              // taper length
    int K = 0;                              // number of tapers
    double NW = 0.0;                        // time-half-bandwidth product
    std::vector<std::vector<double>> taper; // taper[k] : length-N unit-energy sequence
    std::vector<double> lambda;             // lambda[k] : concentration ratio in [0,1]

    bool valid() const { return N > 0 && K > 0 && static_cast<int>(taper.size()) == K; }
};

// Compute the first K DPSS tapers for length N and time-half-bandwidth NW.
//
// Requirements: N >= 2, 1 <= K <= N, 0 < NW < N/2. K is clamped to a
// sensible maximum of floor(2*NW) tapers if the caller asks for more than
// remain usefully concentrated, but never below 1. On invalid input an
// empty (invalid) result is returned.
//
// Tapers follow the usual polarity convention: even-order tapers have a
// positive sum; odd-order tapers are positive-going (positive average
// slope about the centre).
DpssTapers computeDpss(int N, double NW, int K);

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_DPSS_H
