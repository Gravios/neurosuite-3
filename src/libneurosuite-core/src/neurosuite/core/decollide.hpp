/***************************************************************************
 * decollide.hpp
 *
 * Apply a set of two-component collision decompositions to a clustering,
 * growing it by mutual subtraction — the C++ port of fiber-kit's
 * fiber_decollide.decollide_spikes (patch 0546), kept header-only and Qt-free
 * so klusters (the interactive lasso-against-basis decollide), the
 * ndmanager-plugins, and any other consumer share ONE implementation.
 *
 * The decomposition itself (finding k1/k2, the shifts tau and amplitudes a) is
 * NOT here: that is the caller's job — a two-template matching-pursuit fit for
 * the interactive mode, or process_decomposecollisions for the batch mode.  This
 * module only APPLIES a decomposition, which is the piece both modes share.
 *
 * Model (per resolved collision at spike i, components (k1,a1,tau1),(k2,a2,tau2)):
 *     c       = the collision waveform
 *     clean1  = c - a2 * roll0(T2, tau2)                  // cell k1, reference frame
 *     clean2  = roll0(c - a1 * roll0(T1, tau1), -tau2)    // cell k2, recentred
 * where Tk is unit k's mean template and roll0 is a zero-filled (NOT circular)
 * time shift.  The collision spike is REPLACED by two events — k1 at det+tau1 and
 * k2 at det+tau2 — so the clustering grows by one per resolved collision.
 *
 * The reconstruction is LINEAR, so applyMethod runs once PER METHOD (that
 * method's .spk and its own per-unit templates, the SAME decomposition): the
 * .res/.clu it returns are therefore identical across methods (same stable
 * time-sort), while each method's .spk carries that method's waveforms.
 ***************************************************************************/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace neurosuite {
namespace decollide {

// One component of a two-template decomposition: cluster id, integer sample
// shift, amplitude (exactly process_decomposecollisions / fiber-kit output).
struct Component { int unit = 0; int tau = 0; double amp = 0.0; };

// A resolved collision: the spike to replace (index into the caller's res/clu/spk)
// and its two constituents.
struct Decomp { int64_t spikeIndex = -1; Component c1; Component c2; };

// Grown single-method result.  res/clu are method-independent (identical across
// methods); spk is this method's int16 waveforms in the SpkFile layout
// (nSpikes * nSamples * nChannels, channel-fastest).
struct Grown {
    std::vector<int64_t> res;
    std::vector<int>     clu;
    std::vector<int16_t> spk;
    bool                 ok = false;
};

// Zero-filled time shift of one (nSamples × nChannels, channel-fastest) window by
// `s` samples; the vacated edge is zero (a wrapped spike tail is not real signal).
// Matches fiber-kit _roll0: out[t] = w[t-s] where 0 <= t-s < nSamples, else 0.
inline void roll0(const std::vector<float>& w, int nSamples, int nChannels, int s,
                  std::vector<float>& out)
{
    const std::size_t rec = static_cast<std::size_t>(nSamples) * nChannels;
    out.assign(rec, 0.0f);
    for (int t = 0; t < nSamples; ++t) {
        const int src = t - s;
        if (src < 0 || src >= nSamples) continue;
        for (int c = 0; c < nChannels; ++c)
            out[static_cast<std::size_t>(t) * nChannels + c] =
                w[static_cast<std::size_t>(src) * nChannels + c];
    }
}

// Mean waveform per unit (float), over the spikes assigned to that unit.  Units
// with no spikes are absent.  spk is int16 in the SpkFile layout; clu is one id
// per spike.
inline std::map<int, std::vector<float>> meanTemplates(
    const std::vector<int16_t>& spk, int nSamples, int nChannels,
    const std::vector<int>& clu)
{
    const std::size_t rec = static_cast<std::size_t>(nSamples) * nChannels;
    std::map<int, std::vector<double>> sum;
    std::map<int, long>                cnt;
    const std::size_t n = clu.size();
    for (std::size_t i = 0; i < n; ++i) {
        const int k = clu[i];
        auto& s = sum[k];
        if (s.empty()) s.assign(rec, 0.0);
        const int16_t* w = &spk[i * rec];
        for (std::size_t j = 0; j < rec; ++j) s[j] += static_cast<double>(w[j]);
        ++cnt[k];
    }
    std::map<int, std::vector<float>> mean;
    for (auto& kv : sum) {
        const long c = cnt[kv.first];
        if (c <= 0) continue;
        std::vector<float> m(rec);
        for (std::size_t j = 0; j < rec; ++j)
            m[j] = static_cast<float>(kv.second[j] / static_cast<double>(c));
        mean[kv.first] = std::move(m);
    }
    return mean;
}

// ── grow plan (method-independent order + provenance) ───────────────────────
// Decides, ONCE, the grown output: each unselected spike passes through; each
// decomp (a resolved collision) becomes its two constituents.  Stable time-sorted,
// so res/clu/spk/fet rendered from it stay row-aligned AND identical across methods
// (the order depends only on times, not on any method's waveforms/features).
struct OutRow {
    int64_t time      = 0;
    int     clu       = 0;
    int64_t src       = -1;  ///< source spike index (passthrough, or the collision for a constituent)
    int     decompIdx = -1;  ///< index into decomps for a constituent, else -1
    int     comp      = 0;   ///< 0 passthrough, 1 = c1 (clean1), 2 = c2 (clean2)
};

inline std::vector<OutRow> planGrow(
    const std::vector<int64_t>& res, const std::vector<int>& clu,
    const std::vector<Decomp>& decomps)
{
    const std::size_t n = res.size();
    std::vector<char> selected(n, 0);
    for (const auto& d : decomps)
        if (d.spikeIndex >= 0 && static_cast<std::size_t>(d.spikeIndex) < n)
            selected[static_cast<std::size_t>(d.spikeIndex)] = 1;

    std::vector<OutRow> plan;
    plan.reserve(n + decomps.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (selected[i]) continue;
        OutRow r; r.time = res[i]; r.clu = clu[i]; r.src = static_cast<int64_t>(i); r.comp = 0;
        plan.push_back(r);
    }
    for (std::size_t j = 0; j < decomps.size(); ++j) {
        const Decomp& d = decomps[j];
        if (d.spikeIndex < 0 || static_cast<std::size_t>(d.spikeIndex) >= n) continue;
        const int64_t det = res[static_cast<std::size_t>(d.spikeIndex)];
        OutRow a; a.time = det + d.c1.tau; a.clu = d.c1.unit; a.src = d.spikeIndex;
        a.decompIdx = static_cast<int>(j); a.comp = 1;
        OutRow b; b.time = det + d.c2.tau; b.clu = d.c2.unit; b.src = d.spikeIndex;
        b.decompIdx = static_cast<int>(j); b.comp = 2;
        plan.push_back(a); plan.push_back(b);
    }
    std::vector<std::size_t> order(plan.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t x, std::size_t y) { return plan[x].time < plan[y].time; });
    std::vector<OutRow> sorted(plan.size());
    for (std::size_t o = 0; o < order.size(); ++o) sorted[o] = plan[order[o]];
    return sorted;
}

// Apply `decomps` to ONE method's (res, clu, spk) via the shared plan.  Per-unit
// templates are the mean of that method's spk.  A constituent's waveform is the
// mutual-subtraction clean1/clean2 (a missing template — not expected for real
// basis units — degrades to the collision waveform, preserving row alignment).
// Output int16, round-half-to-even (numpy rint).
inline Grown applyMethod(
    const std::vector<int64_t>& res, const std::vector<int>& clu,
    const std::vector<int16_t>& spk, int nSamples, int nChannels,
    const std::vector<Decomp>& decomps)
{
    Grown g;
    const std::size_t rec = static_cast<std::size_t>(nSamples) * nChannels;
    const std::size_t n   = res.size();
    if (nSamples <= 0 || nChannels <= 0 || clu.size() != n || spk.size() != n * rec)
        return g;

    const auto T    = meanTemplates(spk, nSamples, nChannels, clu);
    const auto plan = planGrow(res, clu, decomps);

    g.res.resize(plan.size());
    g.clu.resize(plan.size());
    g.spk.resize(plan.size() * rec);

    std::vector<float> rolled, mid, out(rec);
    for (std::size_t o = 0; o < plan.size(); ++o) {
        const OutRow& r = plan[o];
        g.res[o] = r.time;
        g.clu[o] = r.clu;
        const std::size_t i = static_cast<std::size_t>(r.src);
        const int16_t* csrc = &spk[i * rec];
        if (r.comp == 0) {
            for (std::size_t j = 0; j < rec; ++j) out[j] = static_cast<float>(csrc[j]);
        } else {
            const Decomp& d = decomps[static_cast<std::size_t>(r.decompIdx)];
            std::vector<float> c(rec);
            for (std::size_t j = 0; j < rec; ++j) c[j] = static_cast<float>(csrc[j]);
            const auto it1 = T.find(d.c1.unit);
            const auto it2 = T.find(d.c2.unit);
            if (it1 == T.end() || it2 == T.end()) {
                out = c;                                   // no template: keep aligned, copy collision
            } else if (r.comp == 1) {                      // clean1 = c - a2*roll0(T2,t2)
                roll0(it2->second, nSamples, nChannels, d.c2.tau, rolled);
                for (std::size_t j = 0; j < rec; ++j) out[j] = c[j] - static_cast<float>(d.c2.amp) * rolled[j];
            } else {                                       // clean2 = roll0(c - a1*roll0(T1,t1), -t2)
                roll0(it1->second, nSamples, nChannels, d.c1.tau, rolled);
                mid.assign(rec, 0.0f);
                for (std::size_t j = 0; j < rec; ++j) mid[j] = c[j] - static_cast<float>(d.c1.amp) * rolled[j];
                roll0(mid, nSamples, nChannels, -d.c2.tau, out);
            }
        }
        for (std::size_t j = 0; j < rec; ++j)
            g.spk[o * rec + j] = static_cast<int16_t>(std::nearbyint(out[j]));
    }
    g.ok = true;
    return g;
}

// Grow the IN-MEMORY feature table alongside applyMethod, using the SAME plan so
// rows stay aligned with res/clu/spk.  PCA projection is linear and Klusters
// already holds every spike's features, so a constituent's features follow from
// the collision's existing row and the other unit's mean features — no
// re-projection, no .pca read:
//     f(clean1) = f(collision) - a2 * meanFeat(unit2)
//     f(clean2) = f(collision) - a1 * meanFeat(unit1)
// (an approximation only in the subtracted template's shift; exact at tau = 0).
// `fet` is one int64 row per spike (the .fet body, header excluded); returns the
// grown rows rounded to int64, row-aligned with applyMethod's res/clu.
inline std::vector<std::vector<int64_t>> applyFeatures(
    const std::vector<int64_t>& res, const std::vector<int>& clu,
    const std::vector<std::vector<int64_t>>& fet,
    const std::vector<Decomp>& decomps)
{
    std::vector<std::vector<int64_t>> out;
    const std::size_t n = res.size();
    if (fet.size() != n || n == 0) return out;
    const std::size_t D = fet[0].size();

    std::map<int, std::vector<double>> sum; std::map<int, long> cnt;
    for (std::size_t i = 0; i < n; ++i) {
        auto& s = sum[clu[i]]; if (s.empty()) s.assign(D, 0.0);
        const std::size_t d = std::min(D, fet[i].size());
        for (std::size_t j = 0; j < d; ++j) s[j] += static_cast<double>(fet[i][j]);
        ++cnt[clu[i]];
    }
    std::map<int, std::vector<double>> meanF;
    for (auto& kv : sum) {
        const long c = cnt[kv.first]; std::vector<double> m(D, 0.0);
        if (c > 0) for (std::size_t j = 0; j < D; ++j) m[j] = kv.second[j] / static_cast<double>(c);
        meanF[kv.first] = std::move(m);
    }

    const auto plan = planGrow(res, clu, decomps);
    out.resize(plan.size());
    for (std::size_t o = 0; o < plan.size(); ++o) {
        const OutRow& r = plan[o];
        const std::size_t i = static_cast<std::size_t>(r.src);
        std::vector<int64_t> row(D, 0);
        if (r.comp == 0) {
            row = fet[i];
        } else {
            const Decomp& d = decomps[static_cast<std::size_t>(r.decompIdx)];
            const int    otherUnit = (r.comp == 1) ? d.c2.unit : d.c1.unit;
            const double otherAmp  = (r.comp == 1) ? d.c2.amp  : d.c1.amp;
            const auto mit = meanF.find(otherUnit);
            for (std::size_t j = 0; j < D; ++j) {
                double v = (j < fet[i].size()) ? static_cast<double>(fet[i][j]) : 0.0;
                if (mit != meanF.end()) v -= otherAmp * mit->second[j];
                row[j] = static_cast<int64_t>(std::llround(v));
            }
        }
        out[o] = std::move(row);
    }
    return out;
}

// ── fit: two-template matching pursuit against a FIXED basis pair {A,B} ──────
// The interactive mode's "find the decomposition" step (the batch mode uses
// process_decomposecollisions instead).  A collision is modelled as one event of
// each basis unit; a greedy two-pass fit over integer shifts recovers
// (unit,tau,amp) for each, with the least-squares amplitude a = <r,T@tau>/<T@tau,T@tau>.
// Operates in the SAME channel-fastest layout as applyMethod, so its output feeds
// applyMethod directly.
struct Fit {
    Component c1;              ///< stronger component (found first)
    Component c2;              ///< the other
    double    residFrac = 1.0; ///< ||c - a1*T1@t1 - a2*T2@t2||^2 / ||c||^2
    bool      ok = false;
};

namespace detail {
inline double dot(const float* a, const float* b, std::size_t n) {
    double s = 0.0; for (std::size_t i = 0; i < n; ++i) s += static_cast<double>(a[i]) * b[i]; return s;
}
// Best (tau in [-maxShift,maxShift], least-squares amplitude) of template T
// against target r; bestResid = ||r - amp*T@tau||^2 (computed analytically).
inline void bestShiftAmp(const std::vector<float>& r, const std::vector<float>& T,
                         int nS, int nC, int maxShift,
                         int& bestTau, double& bestAmp, double& bestResid) {
    const std::size_t rec = static_cast<std::size_t>(nS) * nC;
    const double rr = dot(r.data(), r.data(), rec);
    std::vector<float> sh;
    bestResid = -1.0; bestTau = 0; bestAmp = 0.0;
    for (int tau = -maxShift; tau <= maxShift; ++tau) {
        roll0(T, nS, nC, tau, sh);
        const double tt = dot(sh.data(), sh.data(), rec);
        if (tt <= 1e-12) continue;
        const double a     = dot(r.data(), sh.data(), rec) / tt;   // least-squares amplitude
        const double resid = rr - a * a * tt;                      // ||r - a*sh||^2
        if (bestResid < 0.0 || resid < bestResid) { bestResid = resid; bestTau = tau; bestAmp = a; }
    }
}
}  // namespace detail

// Fit one collision waveform `c` to the pair; greedy: whichever of A/B explains
// more is c1, the other fit to the residual is c2.
inline Fit fitOne(const std::vector<float>& c,
                  int unitA, const std::vector<float>& TA,
                  int unitB, const std::vector<float>& TB,
                  int nSamples, int nChannels, int maxShift) {
    Fit f;
    const std::size_t rec = static_cast<std::size_t>(nSamples) * nChannels;
    if (c.size() != rec || TA.size() != rec || TB.size() != rec) return f;
    int tauA, tauB; double ampA, ampB, resA, resB;
    detail::bestShiftAmp(c, TA, nSamples, nChannels, maxShift, tauA, ampA, resA);
    detail::bestShiftAmp(c, TB, nSamples, nChannels, maxShift, tauB, ampB, resB);

    int k1, t1; double a1; const std::vector<float>* T2; int k2;
    if (resA <= resB) { k1 = unitA; t1 = tauA; a1 = ampA; T2 = &TB; k2 = unitB; }
    else              { k1 = unitB; t1 = tauB; a1 = ampB; T2 = &TA; k2 = unitA; }
    const std::vector<float>& T1 = (k1 == unitA) ? TA : TB;

    std::vector<float> sh, r(rec);
    roll0(T1, nSamples, nChannels, t1, sh);
    for (std::size_t j = 0; j < rec; ++j) r[j] = c[j] - static_cast<float>(a1) * sh[j];

    int t2; double a2, resid2;
    detail::bestShiftAmp(r, *T2, nSamples, nChannels, maxShift, t2, a2, resid2);

    const double cc = detail::dot(c.data(), c.data(), rec);
    f.c1 = Component{k1, t1, a1};
    f.c2 = Component{k2, t2, a2};
    f.residFrac = (cc > 1e-12) ? (resid2 / cc) : 0.0;
    f.ok = true;
    return f;
}

// Per-unit empirical amplitude gate (peak-to-peak bounds); hi<=lo disables it.
struct AmpGate { double lo = 0.0; double hi = 0.0; };

// Peak-to-peak of a template scaled by `amp` (the fitted component's amplitude).
inline double peakToPeak(const std::vector<float>& w, double amp) {
    double mx = -1e300, mn = 1e300;
    for (float v : w) { const double s = amp * v; if (s > mx) mx = s; if (s < mn) mn = s; }
    return (w.empty() ? 0.0 : mx - mn);
}

// Fit each collision index to the pair and return the ACCEPTED decompositions
// (rejected ones are simply omitted — applyMethod then leaves those spikes as-is).
// Gates mirror process_decomposecollisions / fiber-kit: residFrac <
// residualThreshold, both amplitudes > 0, and (when a gate's hi>lo) the
// component's peak-to-peak amplitude within [lo,hi].
inline std::vector<Decomp> fitPair(
    const std::vector<int16_t>& spk, int nSamples, int nChannels,
    const std::vector<int64_t>& idx,
    int unitA, const std::vector<float>& TA,
    int unitB, const std::vector<float>& TB,
    int maxShift, double residualThreshold,
    AmpGate gateA = AmpGate{}, AmpGate gateB = AmpGate{}) {
    std::vector<Decomp> out;
    const std::size_t rec = static_cast<std::size_t>(nSamples) * nChannels;
    if (nSamples <= 0 || nChannels <= 0 || TA.size() != rec || TB.size() != rec) return out;
    const std::size_t n = (rec ? spk.size() / rec : 0);
    for (int64_t i : idx) {
        if (i < 0 || static_cast<std::size_t>(i) >= n) continue;
        std::vector<float> c(rec);
        for (std::size_t j = 0; j < rec; ++j) c[j] = static_cast<float>(spk[static_cast<std::size_t>(i) * rec + j]);
        const Fit f = fitOne(c, unitA, TA, unitB, TB, nSamples, nChannels, maxShift);
        if (!f.ok || f.residFrac >= residualThreshold) continue;
        if (f.c1.amp <= 0.0 || f.c2.amp <= 0.0) continue;
        const AmpGate g1 = (f.c1.unit == unitA) ? gateA : gateB;
        const AmpGate g2 = (f.c2.unit == unitA) ? gateA : gateB;
        if (g1.hi > g1.lo) { const double v = peakToPeak(f.c1.unit == unitA ? TA : TB, f.c1.amp);
                             if (v < g1.lo || v > g1.hi) continue; }
        if (g2.hi > g2.lo) { const double v = peakToPeak(f.c2.unit == unitA ? TA : TB, f.c2.amp);
                             if (v < g2.lo || v > g2.hi) continue; }
        Decomp d; d.spikeIndex = i; d.c1 = f.c1; d.c2 = f.c2;
        out.push_back(d);
    }
    return out;
}

}  // namespace decollide
}  // namespace neurosuite
