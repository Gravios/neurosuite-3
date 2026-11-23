/***************************************************************************
 * decollide_fit.hpp -- a collision row with its partners subtracted
 *
 * Header-only and Qt-free, so Klusters (which redoes the subtraction whenever
 * realign or nudge re-cuts a row), the plugins and the tests share ONE
 * implementation; fiber-kit mirrors it (fiber_spkc.py).
 *
 * A collision row is one .spk record that holds two (or more) units' spikes:
 * its .eap row has two (or more) classes, each with the whole-sample offset of
 * its spike from the row's .res time.  The row belongs, in the .clu, to ONE of
 * them -- its OWN unit -- and what a reader should see of it is the record less
 * the others:
 *
 *     w        = aOwn * Town  +  sum over partners of  a_p * T_p(tau_p)  + noise
 *     corrected = w  -  sum over partners of  a_p * T_p(tau_p)
 *
 * The .eap holds which partners and about where; the amplitudes and the exact
 * shifts are FITTED here, every time, from the record and the templates as they
 * now are.  So nothing but the .eap needs keeping in step when a row or a unit
 * is moved: the fit searches a few samples either side of the stored offset and
 * returns the offset it found, which the caller writes back.
 *
 * The fit, per partner and per candidate shift: least squares for (aOwn, a_p)
 * JOINTLY -- fitting the partner alone would let it absorb what the own
 * template explains wherever the two overlap.  Among the whole-sample shifts
 * in [tau - radius, tau + radius] the one with the least residual is taken and
 * refined to a fraction of a sample by a parabola through its neighbours; the
 * partner's template is moved by cubic interpolation, zero-filled where it
 * leaves the window (a wrapped tail is not signal).
 *
 * A subtraction is ACCEPTED only when the partner's amplitude is plausible
 * (ampMin..ampMax) and the fit with it leaves at least minGain less residual
 * energy than the fit without it.  A rejected partner is not subtracted: the
 * row then keeps what the recording holds.
 *
 * Limits.  The partner's template is as long as the window: the part of its
 * spike that lies outside its own window is not subtracted.  The templates
 * must come from rows that are NOT collisions (ownTemplates below), or each
 * unit's template carries the other's remainder.
 *
 * Layout everywhere: the .spk record, nSamples x nChannels, channel-fastest
 * (index = sample * nChannels + channel).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

#include "neurofileio.h"

namespace neurosuite {
namespace decollide_fit {

struct Params {
    int    radius  = 4;      ///< whole samples searched either side of the stored offset
    double ampMin  = 0.3;    ///< a partner fitted below this is not there
    double ampMax  = 2.5;    ///< ... and above this it is not this unit
    double minGain = 0.05;   ///< least share of the residual energy the partner must remove
};

struct Fit {
    bool   accepted = false;
    int    tau = 0;          ///< the fitted offset, to the nearest sample (what the .eap holds)
    double shift = 0.0;      ///< the fitted offset, samples (tau plus the fraction)
    double amp = 0.0;        ///< the partner's amplitude
    double ampOwn = 0.0;     ///< the own unit's amplitude in the joint fit (0 without an own template)
    double residBefore = 0.0;   ///< residual energy with the own template alone
    double residAfter = 0.0;    ///< ... and with the partner
};

/** @p t moved @p shift samples later (out[s] = t[s - shift]), cubic
 *  (Catmull-Rom) between samples, zero where the source lies outside the window. */
inline void shifted(const float* t, int nSamples, int nChannels, double shift, std::vector<float>& out)
{
    out.assign(static_cast<std::size_t>(nSamples) * nChannels, 0.0f);
    const double fl = std::floor(shift);
    const int    whole = static_cast<int>(fl);
    const float  u = static_cast<float>(shift - fl);         // 0 <= u < 1: source lies u BEFORE sample (s - whole)
    auto at = [&](int s, int c) -> float {
        return (s < 0 || s >= nSamples) ? 0.0f : t[static_cast<std::size_t>(s) * nChannels + c];
    };
    for (int s = 0; s < nSamples; ++s) {
        const int i = s - whole;                             // source position i - u
        if (i < -1 || i > nSamples + 1) continue;
        for (int c = 0; c < nChannels; ++c) {
            float v;
            if (u == 0.0f) {
                v = at(i, c);
            } else {                                         // between i-1 and i, at fraction (1-u) from i-1
                const float p0 = at(i - 2, c), p1 = at(i - 1, c), p2 = at(i, c), p3 = at(i + 1, c);
                const float f = 1.0f - u;
                v = p1 + 0.5f * f * (p2 - p0 + f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3
                                                    + f * (3.0f * (p1 - p2) + p3 - p0)));
            }
            out[static_cast<std::size_t>(s) * nChannels + c] = v;
        }
    }
}

namespace detail {

inline double dot(const float* a, const float* b, std::size_t n)
{
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += static_cast<double>(a[i]) * b[i];
    return s;
}

/** Least squares of w on (own, q) -- own may be null -- returning the residual energy. */
inline double joint(const float* w, const float* own, const float* q, std::size_t n,
                    double ww, double oo, double wo, double& aOwn, double& aQ)
{
    const double qq = dot(q, q, n), wq = dot(w, q, n);
    aOwn = 0.0; aQ = 0.0;
    if (!(qq > 0.0)) return std::numeric_limits<double>::infinity();
    if (!own || !(oo > 0.0)) {
        aQ = wq / qq;
        return ww - aQ * wq;
    }
    const double oq = dot(own, q, n);
    const double det = oo * qq - oq * oq;
    if (!(det > 1e-9 * oo * qq)) {                           // the two are (nearly) the same shape here
        aQ = 0.0; aOwn = wo / oo;
        return std::numeric_limits<double>::infinity();
    }
    aOwn = (wo * qq - wq * oq) / det;
    aQ   = (wq * oo - wo * oq) / det;
    return ww - aOwn * wo - aQ * wq;
}

} // namespace detail

/** Fit @p partner (a template, window layout) into the record @p w around the
 *  stored offset @p tau, jointly with @p own (the row's own unit's template;
 *  may be null).  Nothing is subtracted here. */
inline Fit fitPartner(const float* w, const float* own, const float* partner,
                      int nSamples, int nChannels, int tau, const Params& p = {})
{
    Fit f;
    f.tau = tau; f.shift = tau;
    if (!w || !partner || nSamples < 4 || nChannels < 1) return f;
    const std::size_t n = static_cast<std::size_t>(nSamples) * nChannels;
    const double ww = detail::dot(w, w, n);
    const double oo = own ? detail::dot(own, own, n) : 0.0;
    const double wo = own ? detail::dot(w, own, n) : 0.0;
    f.residBefore = (own && oo > 0.0) ? ww - wo * wo / oo : ww;

    const int radius = std::max(0, p.radius);
    std::vector<double> resid(static_cast<std::size_t>(2 * radius + 1));
    std::vector<float> q;
    for (int k = -radius; k <= radius; ++k) {
        shifted(partner, nSamples, nChannels, tau + k, q);
        double aO, aQ;
        resid[static_cast<std::size_t>(k + radius)] = detail::joint(w, own, q.data(), n, ww, oo, wo, aO, aQ);
    }
    int best = -radius;                                      // the least residual; the earlier shift on a tie
    for (int k = -radius + 1; k <= radius; ++k)
        if (resid[static_cast<std::size_t>(k + radius)] < resid[static_cast<std::size_t>(best + radius)]) best = k;
    if (!std::isfinite(resid[static_cast<std::size_t>(best + radius)])) return f;
    double frac = 0.0;
    if (best > -radius && best < radius) {                   // a parabola through the minimum and its neighbours
        const double a = resid[static_cast<std::size_t>(best + radius - 1)];
        const double b = resid[static_cast<std::size_t>(best + radius)];
        const double c = resid[static_cast<std::size_t>(best + radius + 1)];
        const double den = a - 2.0 * b + c;
        if (std::isfinite(a) && std::isfinite(c) && den > 0.0) frac = std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    double shift = tau + best + frac, aO = 0.0, aQ = 0.0;
    shifted(partner, nSamples, nChannels, shift, q);
    double r = detail::joint(w, own, q.data(), n, ww, oo, wo, aO, aQ);
    if (!(r <= resid[static_cast<std::size_t>(best + radius)])) {   // the refinement did not help: keep the whole sample
        shift = tau + best;
        shifted(partner, nSamples, nChannels, shift, q);
        r = detail::joint(w, own, q.data(), n, ww, oo, wo, aO, aQ);
    }
    f.shift = shift;
    f.tau = static_cast<int>(std::lround(shift));
    f.amp = aQ; f.ampOwn = aO; f.residAfter = r;
    f.accepted = std::isfinite(r) && aQ >= p.ampMin && aQ <= p.ampMax
                 && f.residBefore > 0.0 && (f.residBefore - r) >= p.minGain * f.residBefore;
    return f;
}

/** @p w less the fitted partner (in place); nothing happens for a fit that was not accepted. */
inline void subtractPartner(std::vector<float>& w, const float* partner, int nSamples, int nChannels, const Fit& f)
{
    if (!f.accepted) return;
    std::vector<float> q;
    shifted(partner, nSamples, nChannels, f.shift, q);
    const float a = static_cast<float>(f.amp);
    for (std::size_t i = 0; i < w.size() && i < q.size(); ++i) w[i] -= a * q[i];
}

/** A float record as the int16 the .spk holds: rounded, saturated, NaN -> 0. */
inline void toInt16(const std::vector<float>& w, int16_t* out)
{
    for (std::size_t i = 0; i < w.size(); ++i) {
        const float v = w[i];
        out[i] = !(v == v) ? static_cast<int16_t>(0)
               : v >= 32767.0f ? static_cast<int16_t>(32767)
               : v <= -32768.0f ? static_cast<int16_t>(-32768)
               : static_cast<int16_t>(std::lround(v));
    }
}

// ── which cluster a class is, and which class a row's own unit is ─────────────
//
// A class id is an .eap column; cluster ids churn (renumber, merge, split) and
// the .tcl only remembers the id a class was created from.  So the cluster a
// class NOW is, is read off the membership: the cluster that holds most of the
// rows in which that class is the only one.

/** Per class: the cluster holding most of its single-class rows, or -1 when it
 *  has none.  @p clu has one id per .eap row; ids <= 1 (artefacts, noise) are
 *  nobody's home. */
inline std::vector<int> homeClusters(const neurofileio::EapFile& e, const std::vector<int>& clu)
{
    std::vector<int> home(static_cast<std::size_t>(std::max(e.nClasses, 0)), -1);
    if (!e.ok || static_cast<int64_t>(clu.size()) != e.nSpikes) return home;
    std::vector<std::map<int, int64_t>> votes(home.size());
    for (int64_t i = 0; i < e.nSpikes; ++i) {
        const int8_t* row = &e.cells[static_cast<std::size_t>(i) * e.nClasses];
        int only = -1, count = 0;
        for (int j = 0; j < e.nClasses; ++j)
            if (neurofileio::eapPresent(row[j])) { only = j; if (++count > 1) break; }
        if (count == 1 && clu[static_cast<std::size_t>(i)] > 1)
            ++votes[static_cast<std::size_t>(only)][clu[static_cast<std::size_t>(i)]];
    }
    for (std::size_t j = 0; j < home.size(); ++j) {
        int64_t most = 0;
        for (const auto& kv : votes[j])
            if (kv.second > most) { most = kv.second; home[j] = kv.first; }   // ties: the lower id (map order)
    }
    return home;
}

/** One class in a row. */
struct Member { int classId = -1; int offset = 0; };

/** The classes of row @p i split into the row's OWN one and its partners.  Own
 *  is the class whose home cluster is the row's cluster; failing that (the row
 *  was moved by hand, or its class has no home) the class nearest offset 0, the
 *  lower id on a tie.  @p own.classId is -1 for a row without any class.
 *  Returns true when the row has at least one partner. */
inline bool splitRow(const neurofileio::EapFile& e, int64_t i, int cluster, const std::vector<int>& home,
                     Member& own, std::vector<Member>& partners)
{
    own = Member{}; partners.clear();
    if (!e.ok || i < 0 || i >= e.nSpikes) return false;
    const int8_t* row = &e.cells[static_cast<std::size_t>(i) * e.nClasses];
    std::vector<Member> all;
    for (int j = 0; j < e.nClasses; ++j)
        if (neurofileio::eapPresent(row[j])) all.push_back(Member{j, static_cast<int>(row[j])});
    if (all.empty()) return false;
    std::size_t pick = all.size();
    for (std::size_t k = 0; k < all.size(); ++k)
        if (cluster > 1 && static_cast<std::size_t>(all[k].classId) < home.size()
            && home[static_cast<std::size_t>(all[k].classId)] == cluster
            && (pick == all.size() || std::abs(all[k].offset) < std::abs(all[pick].offset))) pick = k;
    if (pick == all.size()) {
        pick = 0;
        for (std::size_t k = 1; k < all.size(); ++k)
            if (std::abs(all[k].offset) < std::abs(all[pick].offset)) pick = k;
    }
    own = all[pick];
    for (std::size_t k = 0; k < all.size(); ++k)
        if (k != pick) partners.push_back(all[k]);
    return !partners.empty();
}

/** A collision row corrected: every partner whose template is given is fitted
 *  (strongest stored first is not known, so in class order, each on what the
 *  ones before left) and, when accepted, subtracted.  @p templates: class id ->
 *  window-layout template; a partner without one is skipped.  @p fits gets one
 *  entry per partner, in @p partners order (not accepted for a skipped one).
 *  Returns the number of partners subtracted. */
inline int correctRow(std::vector<float>& w, int nSamples, int nChannels, const Member& own,
                      const std::vector<Member>& partners,
                      const std::map<int, std::vector<float>>& templates,
                      std::vector<Fit>& fits, const Params& p = {})
{
    fits.assign(partners.size(), Fit{});
    const auto ownIt = templates.find(own.classId);
    const float* ownT = (ownIt != templates.end()
                         && ownIt->second.size() == static_cast<std::size_t>(nSamples) * nChannels)
                        ? ownIt->second.data() : nullptr;
    std::vector<float> ownShifted;
    if (ownT && own.offset != 0) {                           // the row is not on its own unit's peak
        shifted(ownT, nSamples, nChannels, own.offset, ownShifted);
        ownT = ownShifted.data();
    }
    int done = 0;
    for (std::size_t k = 0; k < partners.size(); ++k) {
        fits[k].tau = partners[k].offset; fits[k].shift = partners[k].offset;
        const auto it = templates.find(partners[k].classId);
        if (it == templates.end() || it->second.size() != static_cast<std::size_t>(nSamples) * nChannels) continue;
        fits[k] = fitPartner(w.data(), ownT, it->second.data(), nSamples, nChannels, partners[k].offset, p);
        if (fits[k].accepted) { subtractPartner(w, it->second.data(), nSamples, nChannels, fits[k]); ++done; }
    }
    return done;
}

} // namespace decollide_fit
} // namespace neurosuite
