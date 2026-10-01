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

// Apply `decomps` to ONE method's (res, clu, spk).  Per-unit templates are the
// mean of that method's spk (computed here).  Unselected spikes are carried over
// unchanged; each resolved collision is replaced by its two constituents (a
// decomp referencing a unit with no template is left as-is); the output is stable
// sorted by time and rounded to int16 (round-half-to-even, matching numpy rint).
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

    const auto T = meanTemplates(spk, nSamples, nChannels, clu);

    std::vector<char> selected(n, 0);
    for (const auto& d : decomps)
        if (d.spikeIndex >= 0 && static_cast<std::size_t>(d.spikeIndex) < n)
            selected[static_cast<std::size_t>(d.spikeIndex)] = 1;

    std::vector<int64_t>            ot;
    std::vector<int>                oc;
    std::vector<std::vector<float>> ow;
    ot.reserve(n + decomps.size());
    oc.reserve(n + decomps.size());
    ow.reserve(n + decomps.size());

    // Unselected spikes, unchanged.
    for (std::size_t i = 0; i < n; ++i) {
        if (selected[i]) continue;
        ot.push_back(res[i]);
        oc.push_back(clu[i]);
        ow.emplace_back(spk.begin() + static_cast<std::ptrdiff_t>(i * rec),
                        spk.begin() + static_cast<std::ptrdiff_t>((i + 1) * rec));
    }

    // Resolved collisions.
    std::vector<float> rolled, mid;
    for (const auto& d : decomps) {
        if (d.spikeIndex < 0 || static_cast<std::size_t>(d.spikeIndex) >= n) continue;
        const std::size_t i = static_cast<std::size_t>(d.spikeIndex);
        const std::vector<float> c(spk.begin() + static_cast<std::ptrdiff_t>(i * rec),
                                   spk.begin() + static_cast<std::ptrdiff_t>((i + 1) * rec));
        const auto it1 = T.find(d.c1.unit);
        const auto it2 = T.find(d.c2.unit);
        if (it1 == T.end() || it2 == T.end()) {   // no template to subtract: leave as-is
            ot.push_back(res[i]); oc.push_back(clu[i]); ow.push_back(c);
            continue;
        }
        // clean1 = c - a2 * roll0(T2, tau2)
        roll0(it2->second, nSamples, nChannels, d.c2.tau, rolled);
        std::vector<float> clean1(rec);
        for (std::size_t j = 0; j < rec; ++j)
            clean1[j] = c[j] - static_cast<float>(d.c2.amp) * rolled[j];
        // clean2 = roll0(c - a1 * roll0(T1, tau1), -tau2)
        roll0(it1->second, nSamples, nChannels, d.c1.tau, rolled);
        mid.assign(rec, 0.0f);
        for (std::size_t j = 0; j < rec; ++j)
            mid[j] = c[j] - static_cast<float>(d.c1.amp) * rolled[j];
        std::vector<float> clean2;
        roll0(mid, nSamples, nChannels, -d.c2.tau, clean2);

        const int64_t det = res[i];
        ot.push_back(det + d.c1.tau); oc.push_back(d.c1.unit); ow.push_back(std::move(clean1));
        ot.push_back(det + d.c2.tau); oc.push_back(d.c2.unit); ow.push_back(std::move(clean2));
    }

    // Stable sort by time (keeps the cross-method res/clu identical).
    std::vector<std::size_t> order(ot.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return ot[a] < ot[b]; });

    g.res.resize(order.size());
    g.clu.resize(order.size());
    g.spk.resize(order.size() * rec);
    for (std::size_t o = 0; o < order.size(); ++o) {
        const std::size_t s = order[o];
        g.res[o] = ot[s];
        g.clu[o] = oc[s];
        const std::vector<float>& w = ow[s];
        for (std::size_t j = 0; j < rec; ++j)
            g.spk[o * rec + j] = static_cast<int16_t>(std::nearbyint(w[j]));
    }
    g.ok = true;
    return g;
}

}  // namespace decollide
}  // namespace neurosuite
