// =============================================================================
// xcorr_match.h  —  template matching with sub-sample alignment +
//                   amplitude-scaled residual-energy scoring
//
// Improves on the existing realign_xcorr_omp by adding:
//
//   1. Parabolic sub-sample peak refinement.  After the integer-lag peak
//      is found via the existing time-domain xcorr, fit a parabola to
//      (score[τ*-1], score[τ*], score[τ*+1]) and analytically solve for
//      the sub-sample lag.  Cost: 3 multiplies per pair, well worth it
//      because 0.5-sample jitter at 32 kHz can move a real template
//      match's xcorr from 0.95 down to 0.85.
//
//   2. Amplitude-scaled residual-energy score (KiloSort-style).  At the
//      best lag, solve   min_α  ‖a − α·shift_τ(b)‖²   for the spike-
//      under-test `a` matched against template `b`.  The optimum is
//      α* = <a, shift_τ(b)> / ‖b‖², and the score becomes
//          R = 1 − ‖a − α* shift_τ(b)‖² / ‖a‖²
//        = fraction of `a`'s energy explained by `b` at optimum shift,
//      with a clean physical interpretation: R ≥ 0.85 means ≥ 85 % of
//      `a`'s waveform energy is captured by `b`'s shape (possibly scaled).
//
//      Unlike normalised xcorr, R is asymmetric (R(a, b) ≠ R(b, a) in
//      general) and is robust to the case where two templates have the
//      same shape but different amplitudes — the amplitude-scaled match
//      sees them as the same.  This is exactly the situation that drift
//      and adaptation produce.
//
// The existing realign_xcorr_omp infrastructure is reused for the raw
// per-lag cross-correlation inner loop; this module is the wrapper that
// adds the post-processing.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace xcorr_match {

// -----------------------------------------------------------------------------
// Per-pair match result.
// -----------------------------------------------------------------------------
struct PairScore {
    // Integer-sample lag at the cosine-xcorr peak.  Range: [-maxShift, +maxShift].
    int   bestShiftInt = 0;
    // Sub-sample refinement: actual peak at bestShiftInt + bestShiftFrac.
    // bestShiftFrac ∈ [-0.5, +0.5] from parabolic fit; 0 if the peak was at
    // the edge of the search window (no sub-sample refinement possible).
    float bestShiftFrac = 0.0f;
    // Normalised cosine cross-correlation at (sub-sample-refined) best lag,
    // ∈ [-1, +1].  Symmetric (cosineScore(a,b) = cosineScore(b,a)).
    float cosineScore = 0.0f;
    // Amplitude-scaled residual-energy score, ∈ (-∞, +1].
    // R = 1 means template b perfectly explains a (up to scaling).
    // R = 0 means b explains none of a's energy.
    // R < 0 means matching to b is worse than predicting a = 0.
    // Asymmetric: residualScore(a,b) is the fraction of a's energy explained
    // by b; residualScore(b,a) the reverse.  For pair-merge decisions,
    // typically use min(residualScore(a,b), residualScore(b,a)) so both
    // directions agree the match is good.
    float residualScore = 0.0f;
    // Optimal amplitude scaling α* such that  α* · shift_τ(b) ≈ a.
    // For honest same-unit pairs at the same drift state α* ≈ 1.
    // |α* − 1| > 0.3 is a drift / amplitude-mismatch flag.
    float alphaStar = 1.0f;
};

// -----------------------------------------------------------------------------
// Compute a per-pair match score between two mean waveforms.
//
// Layout matches the existing KKE Phase-3 mean waveform format:
// channel-major within each waveform, i.e. a[ch * nSamp + s].
//
// Parameters:
//   waveformA, waveformB   the two mean waveforms, [nCh × nSamp] each
//   nCh, nSamp             dimensions
//   maxShift               half-width of the integer-lag search, in samples.
//                          0 disables shift search (lag fixed at 0).
//                          Typical: nSamp / 4.
//
// Returns:
//   PairScore — see struct doc.  Sub-sample lag is interpolated using a
//   3-point parabolic fit through the integer-lag peak and its neighbours;
//   if the peak hits the edge of [-maxShift, +maxShift], bestShiftFrac
//   stays 0 and a diagnostic is emitted (silently — caller may want to
//   widen maxShift in that case).
// -----------------------------------------------------------------------------
PairScore matchPair(const float* waveformA,
                    const float* waveformB,
                    int          nCh,
                    int          nSamp,
                    int          maxShift);

// -----------------------------------------------------------------------------
// Batch interface: score all C(N, 2) pairs from a list of mean waveforms.
// Output is a flattened lower-triangular matrix indexed as
//     out[i * N + j]  for j < i
// with out[i * N + j].cosineScore (etc.) holding the result.
// Symmetric quantities (cosineScore, bestShiftInt magnitude) are filled
// in both upper and lower triangles; asymmetric (residualScore, sign of
// shift, alphaStar) are filled only in the lower triangle.
// -----------------------------------------------------------------------------
void matchAllPairs(const std::vector<const float*>& waveforms,
                   int                              nCh,
                   int                              nSamp,
                   int                              maxShift,
                   std::vector<PairScore>&          out);

}  // namespace xcorr_match
