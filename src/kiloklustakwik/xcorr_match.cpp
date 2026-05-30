// =============================================================================
// xcorr_match.cpp  —  implementation of sub-sample-refined, amplitude-
//                     scaled template matching
// =============================================================================
#include "xcorr_match.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace xcorr_match {

namespace {

// Inner product <a, shift_τ(b)> where shift_τ is a CIRCULAR shift by τ
// samples (positive τ = b shifted RIGHT, i.e., b[s] → result at s+τ).
// Channel-major layout: a[ch*nSamp + s].
// τ can be positive or negative.  Wrap-around at the edges (consistent
// with realign_xcorr_omp.cpp's circular convention).
static double innerProdShifted(const float* a, const float* b,
                                int nCh, int nSamp, int tau) {
    double acc = 0.0;
    // τ-normalise into [0, nSamp)
    const int t = ((tau % nSamp) + nSamp) % nSamp;
    for (int ch = 0; ch < nCh; ++ch) {
        const float* arow = a + static_cast<size_t>(ch) * nSamp;
        const float* brow = b + static_cast<size_t>(ch) * nSamp;
        for (int s = 0; s < nSamp; ++s) {
            const int bs = (s + nSamp - t) % nSamp;  // shifted index
            acc += static_cast<double>(arow[s]) * static_cast<double>(brow[bs]);
        }
    }
    return acc;
}

// Squared L2 norm of a channel-major waveform.
static double l2NormSq(const float* a, int nCh, int nSamp) {
    double acc = 0.0;
    const size_t n = static_cast<size_t>(nCh) * nSamp;
    for (size_t i = 0; i < n; ++i) {
        acc += static_cast<double>(a[i]) * static_cast<double>(a[i]);
    }
    return acc;
}

// Parabolic interpolation of three samples for sub-sample peak refinement.
// Given y(-1), y(0), y(+1) at the integer-lag peak and its neighbours,
// fit y = a·δ² + b·δ + c and solve δ* = -b/(2a).  Returns δ* ∈ [-1, +1]
// and the interpolated peak value; on degenerate (a ≥ 0 i.e. not a max)
// returns δ* = 0 and y(0).
static inline void parabolicPeak(double yLeft, double yMid, double yRight,
                                  double& deltaOut, double& valueOut) {
    const double a = 0.5 * (yLeft + yRight) - yMid;
    const double b = 0.5 * (yRight - yLeft);
    if (a >= 0.0) {  // not a maximum — return integer-lag result
        deltaOut = 0.0;
        valueOut = yMid;
        return;
    }
    double delta = -b / (2.0 * a);
    if (delta < -1.0) delta = -1.0;
    if (delta > +1.0) delta = +1.0;
    deltaOut = delta;
    valueOut = yMid - 0.25 * b * delta;  // = c + b·δ/2 simplification
}

}  // anonymous namespace

PairScore matchPair(const float* a, const float* b,
                    int nCh, int nSamp, int maxShift) {
    PairScore result;

    const double normASq = l2NormSq(a, nCh, nSamp);
    const double normBSq = l2NormSq(b, nCh, nSamp);
    if (normASq <= 0.0 || normBSq <= 0.0) {
        // degenerate waveform — return neutral result
        return result;
    }
    const double normA = std::sqrt(normASq);
    const double normB = std::sqrt(normBSq);

    // 1. Integer-lag cosine xcorr search over [-maxShift, +maxShift].
    int    bestTau    = 0;
    double bestInner  = innerProdShifted(a, b, nCh, nSamp, 0);
    for (int t = -maxShift; t <= maxShift; ++t) {
        if (t == 0) continue;
        const double ip = innerProdShifted(a, b, nCh, nSamp, t);
        if (ip > bestInner) {
            bestInner = ip;
            bestTau   = t;
        }
    }
    result.bestShiftInt = bestTau;

    // 2. Parabolic sub-sample refinement — needs both neighbours of the
    //    integer peak.  Skip if the peak is at the search edge.
    double subDelta = 0.0;
    double subPeak  = bestInner;
    if (std::abs(bestTau) < maxShift) {
        const double yL = innerProdShifted(a, b, nCh, nSamp, bestTau - 1);
        const double yR = innerProdShifted(a, b, nCh, nSamp, bestTau + 1);
        parabolicPeak(yL, bestInner, yR, subDelta, subPeak);
    }
    result.bestShiftFrac = static_cast<float>(subDelta);

    // 3. Cosine score at the sub-sample-refined peak.
    result.cosineScore = static_cast<float>(subPeak / (normA * normB));

    // 4. Amplitude-scaled residual-energy score.
    //    α* = <a, shift_τ(b)> / ‖b‖²
    //    R  = 1 − ‖a − α*·shift_τ(b)‖² / ‖a‖²
    //       = 1 − (‖a‖² + (α*)² ‖b‖² − 2 α* <a, shift_τ b>) / ‖a‖²
    //       = (2 α* <a, shift_τ b> − (α*)² ‖b‖²) / ‖a‖²
    //       = (<a, shift_τ b>)² / (‖a‖² ‖b‖²)             // after subbing α*
    //       = cosineScore²
    //    SO at first glance R looks redundant — it's just the squared
    //    cosine.  But the *interpretation* differs and α* itself is a
    //    useful diagnostic for amplitude mismatch.  We expose both.
    const double inner = subPeak;
    const double alpha = inner / normBSq;
    result.alphaStar    = static_cast<float>(alpha);
    const double resid  = 1.0 - (inner * inner) / (normASq * normBSq);
    result.residualScore = static_cast<float>(1.0 - resid);

    return result;
}

void matchAllPairs(const std::vector<const float*>& waveforms,
                   int                              nCh,
                   int                              nSamp,
                   int                              maxShift,
                   std::vector<PairScore>&          out) {
    const int N = static_cast<int>(waveforms.size());
    out.assign(static_cast<size_t>(N) * N, PairScore{});
    if (N < 2) return;

    // Pairs (i, j) with j < i.  Parallelise the outer loop.
    #pragma omp parallel for schedule(dynamic, 4)
    for (int i = 1; i < N; ++i) {
        for (int j = 0; j < i; ++j) {
            const PairScore ps = matchPair(waveforms[i], waveforms[j],
                                            nCh, nSamp, maxShift);
            out[static_cast<size_t>(i) * N + j] = ps;
            // For symmetric quantities, mirror.  Asymmetric (residualScore
            // direction, sign of shift) are left in lower triangle only —
            // the caller should compute the j→i direction separately if
            // they want both.
            PairScore mirror = ps;
            mirror.bestShiftInt  = -ps.bestShiftInt;
            mirror.bestShiftFrac = -ps.bestShiftFrac;
            // residualScore would need a separate computation for j→i;
            // we leave it equal to the i→j direction with a note that
            // callers wanting symmetric handling should do
            //   merge_score = min(out[i*N+j].residualScore,
            //                     <call matchPair(j, i)>.residualScore)
            out[static_cast<size_t>(j) * N + i] = mirror;
        }
    }
}

}  // namespace xcorr_match
