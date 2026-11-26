/***************************************************************************
 * collisionscan.h -- is another unit's spike in this record?
 *
 * Header-only and Qt-free.  The search behind Clusters > Mark Collisions
 * (KlustersDoc::scanCollisions): the .eap names a row's partners when a sorter
 * wrote them; this finds the ones no sorter named, from the record and the
 * units' templates alone.
 *
 *   1. the row's own template is taken out at its best whole-sample shift
 *      (within +-ownShift) and gain (ownGainMin..ownGainMax);
 *   2. every candidate partner's template is tried in what is left at every
 *      lag that keeps its peak inside the window.  At amplitude a the partner
 *      removes  gain = 2 a <r, T> - a^2 <T, T>  of the residual energy; the
 *      best a is <r, T> / <T, T>, and only 0.5 .. 1.6 of a template counts as
 *      that unit's spike;
 *   3. the CONTROL is the same search for the partners' NEGATED templates
 *      (amplitude -1.6 .. -0.5).  No spike is the negative of a unit's, so
 *      what that search finds is what noise and misfit alone give.
 *
 * The caller sets the threshold on the control (KlustersDoc::scanCollisions
 * takes the gain the control reaches in falseRate of the rows) and confirms a
 * candidate with decollide_fit::fitPartner, the fit the corrected rows are
 * later made with.
 *
 * Layout everywhere: the .spk record, nSamples x nChannels, channel-fastest
 * (index = sample * nChannels + channel).  A lag is in whole samples, positive
 * when the partner's spike is LATER than the row's -- the .eap's offset.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace collisionscan {

struct Params {
    int    ownShift   = 2;      ///< whole samples the own template may sit off the row's time
    double ownGainMin = 0.6;    ///< the own spike's amplitude, as a share of its template
    double ownGainMax = 1.5;
    double ampMin     = 0.5;    ///< a partner below this share of its template is not that unit's spike
    double ampMax     = 1.6;
    int    edge       = 6;      ///< the partner's peak stays this many samples inside the window
};

/** The lags searched: the partner's peak (0-based @p peak in its own window) stays @p edge samples inside. */
inline void lagRange(int nSamples, int peak, int edge, int& first, int& last)
{
    first = edge - peak;
    last  = nSamples - 1 - edge - peak;
    if (first > last) { first = 0; last = 0; }
    first = std::max(first, -(nSamples - 1));
    last  = std::min(last, nSamples - 1);
}

/** <a, t moved lag samples later>, zero where t leaves the window. */
inline double laggedDot(const float* a, const float* t, int nSamples, int nChannels, int lag)
{
    const int from = std::max(0, lag), to = std::min(nSamples, nSamples + lag);   // samples of a
    if (to <= from) return 0.0;
    const float* pa = a + static_cast<std::size_t>(from) * nChannels;
    const float* pt = t + static_cast<std::size_t>(from - lag) * nChannels;
    const std::size_t n = static_cast<std::size_t>(to - from) * nChannels;
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += static_cast<double>(pa[i]) * pt[i];
    return s;
}

/** A candidate partner: a template with its energy at every lag searched. */
struct Candidate {
    int                 unit = -1;
    const float*        t = nullptr;      ///< nSamples x nChannels
    std::vector<double> energy;           ///< <T_lag, T_lag>, lag = first .. last
};

inline Candidate candidate(int unit, const float* t, int nSamples, int nChannels, int first, int last)
{
    Candidate c;
    c.unit = unit; c.t = t;
    c.energy.resize(static_cast<std::size_t>(last - first + 1));
    for (int lag = first; lag <= last; ++lag) {
        // the part of t that stays in the window when it is moved lag samples later
        const int from = std::max(0, -lag), to = std::min(nSamples, nSamples - lag);
        double s = 0.0;
        for (std::size_t i = static_cast<std::size_t>(std::max(from, 0)) * nChannels;
             to > from && i < static_cast<std::size_t>(to) * nChannels; ++i)
            s += static_cast<double>(t[i]) * t[i];
        c.energy[static_cast<std::size_t>(lag - first)] = s;
    }
    return c;
}

/** @p w less its own template at the best shift and gain; returns the residual energy.
 *  @p shift and @p gain get what was taken out. */
inline double ownResidual(const float* w, const float* own, int nSamples, int nChannels, const Params& p,
                          std::vector<float>& r, int* shift = nullptr, double* gain = nullptr)
{
    const std::size_t n = static_cast<std::size_t>(nSamples) * nChannels;
    r.assign(w, w + n);
    double ww = 0.0;
    for (std::size_t i = 0; i < n; ++i) ww += static_cast<double>(w[i]) * w[i];
    if (shift) *shift = 0;
    if (gain) *gain = 0.0;
    if (!own) return ww;
    double best = ww; int bestShift = 0; double bestGain = 0.0; bool any = false;
    for (int s = -p.ownShift; s <= p.ownShift; ++s) {
        const int from = std::max(0, -s), to = std::min(nSamples, nSamples - s);
        double oo = 0.0;
        for (std::size_t i = static_cast<std::size_t>(from) * nChannels;
             to > from && i < static_cast<std::size_t>(to) * nChannels; ++i)
            oo += static_cast<double>(own[i]) * own[i];
        if (!(oo > 0.0)) continue;
        const double wo = laggedDot(w, own, nSamples, nChannels, s);
        const double a = std::clamp(wo / oo, p.ownGainMin, p.ownGainMax);
        const double err = ww - 2.0 * a * wo + a * a * oo;
        if (!any || err < best) { best = err; bestShift = s; bestGain = a; any = true; }
    }
    if (!any) return ww;
    const int from = std::max(0, bestShift), to = std::min(nSamples, nSamples + bestShift);
    const float a = static_cast<float>(bestGain);
    for (int s = from; s < to; ++s)
        for (int c = 0; c < nChannels; ++c)
            r[static_cast<std::size_t>(s) * nChannels + c] -=
                a * own[static_cast<std::size_t>(s - bestShift) * nChannels + c];
    if (shift) *shift = bestShift;
    if (gain) *gain = bestGain;
    return best;
}

/** What the search over the candidates found in one residual. */
struct Found {
    int    candidate = -1;      ///< index into the candidates, -1 when none fits
    int    lag = 0;
    double amp = 0.0;
    double gain = 0.0;          ///< residual energy the best partner removes
    double control = 0.0;       ///< ... and the best NEGATED template
};

/** Tries candidate @p c (index @p index) at every lag in the residual @p r and keeps the best in @p f. */
inline void tryCandidate(const float* r, const Candidate& c, int index, int nSamples, int nChannels,
                         int first, int last, const Params& p, Found& f)
{
    for (int lag = first; lag <= last; ++lag) {
        const double e = c.energy[static_cast<std::size_t>(lag - first)];
        if (!(e > 0.0)) continue;
        const double d = laggedDot(r, c.t, nSamples, nChannels, lag);
        const double a = d / e;
        const double g = d * a;                           // 2 a d - a^2 e at the best a
        if (a >= p.ampMin && a <= p.ampMax) {
            if (g > f.gain) { f.gain = g; f.candidate = index; f.lag = lag; f.amp = a; }
        } else if (a <= -p.ampMin && a >= -p.ampMax) {
            if (g > f.control) f.control = g;
        }
    }
}

/** The largest cosine between @p a and @p b over whole-sample shifts of +-@p reach. */
inline double likeness(const float* a, const float* b, int nSamples, int nChannels, int reach)
{
    const std::size_t n = static_cast<std::size_t>(nSamples) * nChannels;
    double aa = 0.0, bb = 0.0;
    for (std::size_t i = 0; i < n; ++i) { aa += static_cast<double>(a[i]) * a[i]; bb += static_cast<double>(b[i]) * b[i]; }
    if (!(aa > 0.0) || !(bb > 0.0)) return 0.0;
    double best = -1.0;
    for (int s = -reach; s <= reach; ++s)
        best = std::max(best, laggedDot(a, b, nSamples, nChannels, s) / std::sqrt(aa * bb));
    return best;
}

} // namespace collisionscan
