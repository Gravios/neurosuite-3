/***************************************************************************
 * waveformiou.h — overlap between two clusters' waveform envelopes.
 *
 * Intersection-over-union of the mean +/- SD bands, per sample per channel,
 * accumulated over the whole template.  1 = the two envelopes coincide, 0 = they
 * never touch.
 *
 * This is deliberately the SAME band the waveform view draws (mean plus/minus
 * stDeviation), so a number here means the thing the curator is looking at.
 *
 * Why this rather than the residual matrix:
 *
 *   - It is SYMMETRIC by construction.  IOU(A,B) == IOU(B,A) with no choice to
 *     make, which removes the min/mean/max question that the residual matrix's
 *     asymmetry forced — and got wrong.
 *   - It is ABSOLUTE and bounded in [0,1].  A residual is variance-scaled, so it
 *     only means anything relative to other residuals, which is why it could only
 *     ever contribute a rank.  An overlap of 0.8 means something on its own.
 *   - It reads the cached mean and SD, so it needs no residual matrix display
 *     open, no matrix compute, and no .spk pass.
 *   - It accounts for SPREAD, not just template distance: two clusters whose
 *     means differ by less than their scatter overlap heavily, which is exactly
 *     when they are worth merging.
 *
 * Qt-free and header-only so it can be unit-tested.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef WAVEFORMIOU_H
#define WAVEFORMIOU_H

#include <algorithm>
#include <functional>

/**Intersection-over-union of two mean +/- k*SD envelopes, maximised over a lag.
 *
 * Per sample per channel, the overlap of the two mean +/- k*SD bands divided by
 * their union, accumulated over the template.  1 = the envelopes coincide, 0 =
 * they never touch.  Deliberately the SAME band the waveform view draws, so a
 * number here means the thing the curator is looking at.
 *
 * maxShift = 0 is a strict sample-for-sample comparison and was the original
 * behaviour.  On this pipeline that is the wrong question to ask of a merge candidate: spikes
 * are extracted at a detected peak, and the whole realignment subsystem
 * (klustersdoc_realign, spikerealign, the nudge tooling, ndm_alignspikes as a
 * pre-pass) exists because that alignment is not exact.  Two halves of one neuron
 * split by a single sample -- 30 us at sr=32552 -- score like unrelated units at
 * zero shift: an identical pair offset by one sample drops from 1.00 to about 0.65
 * at a realistic SD of 10% of peak.  That is precisely the pair a curator wants
 * flagged, and it was the one the panel could not see.
 *
 * ONE EVALUATION WINDOW FOR EVERY SHIFT.  Samples [maxShift, nSamp-maxShift) are
 * scored at every lag.  The obvious alternative -- score each lag over whatever
 * samples it happens to have -- is not comparable across lags: a shift drops
 * samples from the ENDS, which is baseline, where both bands are noise and overlap
 * heavily.  Scoring lag 0 over the full window and lag 1 over a shorter one would
 * hand lag 0 extra easy samples and bias the maximum toward "no shift".  Every lag
 * is scored on the same samples or the comparison is rigged.
 *
 * @param nSamp,nChan  cache layout is index = sample * nChan + channel.
 * @param maxShift  lags searched are [-maxShift, +maxShift].  0 reproduces
 *        wfEnvelopeIou over the full window.
 * @param bestShift  if non-null, receives the winning lag, defined so that
 *        B[s + bestShift] lines up with A[s]: a POSITIVE value means B's spikes sit
 *        that many samples LATER in their extraction window than A's.  Ties resolve
 *        to the smallest magnitude, so an ambiguous pair reports 0 rather than an
 *        arbitrary lag.  Worth surfacing rather than hiding -- a pair that only
 *        matches at +/-1 is a different finding from one that matches at 0, and the
 *        curator should be told which.
 */
inline double wfEnvelopeIouBestShift(const std::function<double(int)>& meanA,
                                     const std::function<double(int)>& sdA,
                                     const std::function<double(int)>& meanB,
                                     const std::function<double(int)>& sdB,
                                     int nSamp, int nChan, int maxShift,
                                     double k = 1.0, int* bestShift = nullptr)
{
    if (bestShift) *bestShift = 0;
    if (nSamp < 1 || nChan < 1 || k <= 0.0) return 0.0;
    if (maxShift < 0) maxShift = 0;
    // Need something left to score once both ends are trimmed.
    if (2 * maxShift >= nSamp) return 0.0;

    const int s0 = maxShift;
    const int s1 = nSamp - maxShift;          // [s0, s1) scored at every lag

    double best = 0.0;
    int    bestS = 0;
    for (int sh = -maxShift; sh <= maxShift; ++sh) {
        double interSum = 0.0, unionSum = 0.0;
        for (int s = s0; s < s1; ++s) {
            const int sb = s + sh;            // in range by construction of [s0,s1)
            for (int c = 0; c < nChan; ++c) {
                const int ia = s  * nChan + c;
                const int ib = sb * nChan + c;
                const double ha = k * sdA(ia);
                const double hb = k * sdB(ib);
                const double loA = meanA(ia) - ha, hiA = meanA(ia) + ha;
                const double loB = meanB(ib) - hb, hiB = meanB(ib) + hb;
                const double inter = std::max(0.0, std::min(hiA, hiB) - std::max(loA, loB));
                const double uni   = (hiA - loA) + (hiB - loB) - inter;
                if (uni <= 0.0) continue;
                interSum += inter;
                unionSum += uni;
            }
        }
        const double iou = (unionSum > 0.0) ? (interSum / unionSum) : 0.0;
        // Strict >: ties keep the SMALLEST |shift| because sh runs -maxShift..+maxShift
        // and 0 is reached before the larger positive lags... which is not true for
        // negative lags, so prefer the smaller magnitude explicitly.
        if (iou > best || (iou == best && std::abs(sh) < std::abs(bestS))) {
            best = iou;
            bestS = sh;
        }
    }
    if (bestShift) *bestShift = bestS;
    return best;
}

#endif // WAVEFORMIOU_H
