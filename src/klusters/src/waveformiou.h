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

/**Intersection-over-union of two mean +/- k*SD envelopes.
 *
 * @param meanA,sdA,meanB,sdB  accessors over [0, nSamp*nChan), indexed exactly
 *        as the waveform cache is: index = sample * nChan + channel.
 * @param nTotal  nSamp * nChan.
 * @param k  half-width of the band in standard deviations (1 = what the
 *        waveform view draws).
 *
 * Returns 0 when the templates carry no width at all (every SD zero), since two
 * zero-width bands overlap only if they are exactly equal and that is a
 * measure-zero coincidence, not evidence of anything.
 */
inline double wfEnvelopeIou(const std::function<double(int)>& meanA,
                            const std::function<double(int)>& sdA,
                            const std::function<double(int)>& meanB,
                            const std::function<double(int)>& sdB,
                            int nTotal, double k = 1.0)
{
    if (nTotal < 1 || k <= 0.0) return 0.0;

    double interSum = 0.0;
    double unionSum = 0.0;
    for (int i = 0; i < nTotal; ++i) {
        const double ha = k * sdA(i);
        const double hb = k * sdB(i);
        const double loA = meanA(i) - ha, hiA = meanA(i) + ha;
        const double loB = meanB(i) - hb, hiB = meanB(i) + hb;

        const double inter = std::max(0.0, std::min(hiA, hiB) - std::max(loA, loB));
        const double uni   = (hiA - loA) + (hiB - loB) - inter;
        if (uni <= 0.0) continue;          // both bands are points here
        interSum += inter;
        unionSum += uni;
    }
    if (unionSum <= 0.0) return 0.0;
    return interSum / unionSum;
}

#endif // WAVEFORMIOU_H
