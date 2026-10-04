// ═══════════════════════════════════════════════════════════════════════════
//  template_generate.hpp — LINKED per-unit/class median templates, in C++
//
//  A native port of fiber-kit's fiber_template.py (fiber_kit/fiber_template.py)
//  so Klusters can build the template library (.wti/.wtf) without the external
//  Python plugin — see claude/template-curation-plan.md §6.
//
//  A template "like a fiber": not one global median but a LINKED SERIES of
//  per-bin medians along an axis, so the stored template tracks how a unit's
//  waveform changes:
//    * link "drift"  — median per time CHUNK (probe/tissue drift);
//    * link "adapt"  — median per spike-ENERGY bin (within-burst adaptation).
//
//  PURE computation: the caller hands in the res times, the membership (.clu
//  ids OR an .eap matrix), and the .spk int16 stack(s), and gets back the .wti
//  rows + the per-variant .wtf stacks (ready for neurofileio::writeWti /
//  writeSpk).  No disk, no Qt — testable in isolation and callable from both
//  Klusters (in-memory Data) and a thin on-disk wrapper.
//
//  numpy parity: the median (np.median — mean of the two middle for an even
//  count), the energy-bin edges (np.quantile, linear interpolation), the
//  subsample stride (np.linspace(...).astype(int)), the res-frame roll and the
//  final np.rint→int16 all replicate fiber_template.py exactly, so a C++ run
//  reproduces the Python one.  (The .wti also carries src_clu_* trailing columns
//  in the Python writer; those are index-only metadata the C++ reader ignores.)
// ═══════════════════════════════════════════════════════════════════════════
#ifndef NEUROSUITE_CORE_TEMPLATE_GENERATE_HPP
#define NEUROSUITE_CORE_TEMPLATE_GENERATE_HPP

#include "neurosuite/core/neurofileio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace neurosuite {
namespace templategen {

// ── parameters (mirror the fiber-template CLI) ──────────────────────────────
struct Params {
    int    nSamples   = 0;       ///< samples per waveform record
    int    nChannels  = 0;       ///< channels per record
    double sr         = 32552.0; ///< sampling rate (Hz) — drift windows are seconds
    bool   linkDrift  = true;    ///< build the per-time-chunk series
    bool   linkAdapt  = false;   ///< build the per-energy-bin series
    int    nChunks    = 0;       ///< drift: fixed chunk count (0 ⇒ use chunkMin / default)
    double chunkMin   = 0.0;     ///< drift: chunk duration in minutes (0 ⇒ default)
    int    nEnergy    = 5;       ///< adapt: number of energy bins
    int    maxPer     = 800;     ///< median over ≤ this many strided spikes per bin
    bool   dropEmpty  = true;    ///< drop a bin with no spikes (else a 0-filled placeholder row)
    bool   eap        = false;   ///< membership from the .eap matrix (class = column), not .clu
};

// ── result (ready for neurofileio::writeWti + writeSpk per variant) ─────────
struct Result {
    std::vector<neurofileio::WtiRow>             rows;   ///< one per (unit/class, link, bin) template
    std::map<std::string, std::vector<int16_t>>  wtf;    ///< variant → .spk-layout int16 stack (rows.size() records)
    bool        ok = false;
    std::string err;
};

// ── numpy-matching numeric helpers ──────────────────────────────────────────

// np.median of a (reordered in place) sample: the middle for an odd count, the
// mean of the two middle for an even count.
inline double median_np(std::vector<float>& v)
{
    const std::size_t n = v.size();
    if (n == 0) return std::nan("");
    const std::size_t mid = n / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    const double hi = static_cast<double>(v[mid]);
    if (n & 1u) return hi;
    const double lo = static_cast<double>(*std::max_element(v.begin(),
                              v.begin() + static_cast<std::ptrdiff_t>(mid)));
    return 0.5 * (lo + hi);
}

// np.quantile(sorted, q) with the default "linear" interpolation.  `s` ascending.
inline double quantile_linear(const std::vector<double>& s, double q)
{
    const std::size_t n = s.size();
    if (n == 0) return 0.0;
    if (n == 1) return s[0];
    const double pos = q * static_cast<double>(n - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    if (lo >= n - 1) return s[n - 1];
    const double frac = pos - static_cast<double>(lo);
    return s[lo] + frac * (s[lo + 1] - s[lo]);
}

// np.searchsorted(edges, v, side="right") - 1, clipped to [0, nbins-1].
inline int assign_bin(const std::vector<double>& edges, double v)
{
    const int nbins = static_cast<int>(edges.size()) - 1;
    int idx = static_cast<int>(std::upper_bound(edges.begin(), edges.end(), v) - edges.begin()) - 1;
    if (idx < 0) idx = 0;
    if (idx > nbins - 1) idx = nbins - 1;
    return idx;
}

// Drift chunk edges in SAMPLE units + the chunk count, matching chunk_edges():
// fixed count (nChunks) or fixed duration (chunkMin minutes), else one chunk.
inline std::pair<std::vector<double>, int>
chunk_edges(const std::vector<int64_t>& times, double sr, double chunkMin, int nChunks)
{
    double tmax = 0.0;
    for (int64_t t : times) tmax = std::max(tmax, static_cast<double>(t));
    int nC;
    if (nChunks > 0)          nC = std::max(1, nChunks);
    else if (chunkMin > 0.0)  nC = std::max(1, static_cast<int>(std::ceil((tmax / sr / 60.0) / chunkMin)));
    else                      nC = 1;
    std::vector<double> edges(static_cast<std::size_t>(nC) + 1);
    for (int i = 0; i <= nC; ++i)
        edges[static_cast<std::size_t>(i)] = static_cast<double>(i) * (tmax + 1.0) / static_cast<double>(nC);
    return { edges, nC };
}

// Equal-occupancy (quantile) energy-bin edges, length nEnergy+1; last edge nudged
// so the max falls in the last bin (energy_edges()).  `Esorted` ascending.
inline std::vector<double> energy_edges(const std::vector<double>& Esorted, int nEnergy)
{
    std::vector<double> edges(static_cast<std::size_t>(nEnergy) + 1);
    for (int i = 0; i <= nEnergy; ++i)
        edges[static_cast<std::size_t>(i)] = quantile_linear(Esorted, static_cast<double>(i) / static_cast<double>(nEnergy));
    edges[static_cast<std::size_t>(nEnergy)] += 1e-6;
    return edges;
}

// L2 norm of one record (float64) — the amplitude proxy the adapt axis bins on.
inline double spike_energy(const int16_t* rec, std::size_t recLen)
{
    double s = 0.0;
    for (std::size_t e = 0; e < recLen; ++e) { const double w = static_cast<double>(rec[e]); s += w * w; }
    return std::sqrt(s);
}

// Roll a record so the class EAP (stored `offset` samples from res) lands in the
// res frame: aligned[t] = rec[t+offset], 0 where t+offset leaves the window
// (_align_to_res).  A 0 offset is the identity.
inline std::vector<float> align_to_res(const int16_t* rec, int offset, int nsamp, int nchan)
{
    std::vector<float> out(static_cast<std::size_t>(nsamp) * static_cast<std::size_t>(nchan), 0.0f);
    for (int t = 0; t < nsamp; ++t) {
        const int src = t + offset;
        if (src >= 0 && src < nsamp)
            for (int c = 0; c < nchan; ++c)
                out[static_cast<std::size_t>(t) * nchan + c] =
                    static_cast<float>(rec[static_cast<std::size_t>(src) * nchan + c]);
    }
    return out;
}

// np.linspace(0, S-1, maxPer).astype(int): the subsample positions into a bin's
// member list when it exceeds maxPer.
inline std::vector<std::size_t> linspace_indices(std::size_t S, int maxPer)
{
    std::vector<std::size_t> out(static_cast<std::size_t>(maxPer));
    for (int j = 0; j < maxPer; ++j) {
        const double pos = (maxPer == 1) ? 0.0
                         : static_cast<double>(j) * static_cast<double>(S - 1) / static_cast<double>(maxPer - 1);
        std::size_t k = static_cast<std::size_t>(pos);   // astype(int) truncates (floor for ≥0)
        if (k >= S) k = S - 1;
        out[static_cast<std::size_t>(j)] = k;
    }
    return out;
}

// Round to `d` decimals with round-half-to-even (Python round()), for the .wti
// window coordinates (cosmetic — they describe the bin, not the waveform).
inline double round_nd(double x, int d)
{
    double p = 1.0; for (int i = 0; i < d; ++i) p *= 10.0;
    return std::rint(x * p) / p;
}

// ── the generator ───────────────────────────────────────────────────────────
//
// `times`    : per-spike res times (length N).
// `variants` : the .spk variants to template (one .wtf stack each in the result).
// `spk`      : variant → flat int16 stack, N records of nSamples*nChannels
//              (neurofileio::SpkFile::samples layout).
// `energyVariant` : variant whose energy defines the adapt bins ("" ⇒ "standard"
//              if present, else variants[0]).
// Membership — exactly one of:
//   `clu` (length N, clu-mode): a class is a cluster id; members are clu==id;
//         default units = the ids ≥ 2.
//   `eapPtr` (eap-mode): a class is an .eap COLUMN; members are the spikes present
//         in that column, each aligned to res by its stored offset; default units
//         = any column with a member (callers usually pass the .tcl active cols).
// `units`    : explicit unit/class ids (empty ⇒ derive as above).
inline Result generate(const std::vector<int64_t>&                       times,
                       const std::vector<std::string>&                   variants,
                       const std::map<std::string, std::vector<int16_t>>& spk,
                       const std::string&                                energyVariant,
                       const std::vector<int>*                           clu,
                       const neurofileio::EapFile*                       eapPtr,
                       std::vector<int>                                  units,
                       const Params&                                     pIn)
{
    Result R;
    Params p = pIn;
    const int nsamp = p.nSamples, nchan = p.nChannels;
    const int64_t N = static_cast<int64_t>(times.size());
    const std::size_t recLen = static_cast<std::size_t>(nsamp) * static_cast<std::size_t>(nchan);

    if (nsamp <= 0 || nchan <= 0) { R.err = "bad waveform geometry";  return R; }
    if (variants.empty())         { R.err = "no variants requested";  return R; }
    for (const std::string& v : variants) {
        auto it = spk.find(v);
        if (it == spk.end() || it->second.size() != static_cast<std::size_t>(N) * recLen) {
            R.err = "spk variant '" + v + "' missing or wrong length"; return R;
        }
    }

    // Chunk defaults (main()): explicit flag wins, else 12-min chunks in eap mode
    // (stable across concatenation), else the legacy 6-chunk default.
    if (p.nChunks == 0 && p.chunkMin == 0.0) {
        if (p.eap) p.chunkMin = 12.0;
        else       p.nChunks  = 6;
    }

    // Membership + default unit/class set.
    if (p.eap) {
        if (!eapPtr || !eapPtr->ok)      { R.err = "no .eap matrix";          return R; }
        if (eapPtr->nSpikes != N)        { R.err = "res/eap length mismatch"; return R; }
        if (units.empty()) {
            const int T = eapPtr->nClasses;
            for (int j = 0; j < T; ++j) {
                bool any = false;
                for (int64_t i = 0; i < N && !any; ++i)
                    any = neurofileio::eapPresent(eapPtr->cells[static_cast<std::size_t>(i) * T + j]);
                if (any) units.push_back(j);
            }
        }
    } else {
        if (!clu || static_cast<int64_t>(clu->size()) != N) { R.err = "res/clu length mismatch"; return R; }
        if (units.empty()) {
            std::set<int> s;
            for (int c : *clu) if (c >= 2) s.insert(c);
            units.assign(s.begin(), s.end());
        }
    }

    // Drift chunk assignment (variant-independent — from the shared res times).
    auto chres = chunk_edges(times, p.sr, p.chunkMin, p.nChunks);
    const std::vector<double>& edges = chres.first;
    const int nC = chres.second;
    std::vector<int> chOf(static_cast<std::size_t>(N));
    for (int64_t i = 0; i < N; ++i) chOf[static_cast<std::size_t>(i)] = assign_bin(edges, static_cast<double>(times[i]));

    // Energy reference variant.
    std::string eref = energyVariant;
    if (eref.empty())
        eref = (std::find(variants.begin(), variants.end(), std::string("standard")) != variants.end())
             ? std::string("standard") : variants.front();

    for (const std::string& v : variants) R.wtf[v] = {};

    // Emit one (unit, link) series: per-bin median in every variant + the rows.
    // `binOf[k]` is member idx[k]'s bin; windowLo/Hi give the bin's [a,b].
    auto emit_series =
        [&](int unit, const std::string& link, int nbins,
            const std::vector<int64_t>& idx, const std::vector<int>& binOf,
            const std::vector<int>& off,
            const std::function<std::pair<double,double>(int)>& window)
    {
        std::vector<std::vector<std::size_t>> byBin(static_cast<std::size_t>(nbins));
        for (std::size_t k = 0; k < idx.size(); ++k) {
            const int b = binOf[k];
            if (b >= 0 && b < nbins) byBin[static_cast<std::size_t>(b)].push_back(k);
        }
        for (int b = 0; b < nbins; ++b) {
            const std::vector<std::size_t>& members = byBin[static_cast<std::size_t>(b)];
            const int64_t nsp = static_cast<int64_t>(members.size());   // full count (nsp[b] = ix.size)
            if (p.dropEmpty && nsp == 0) continue;

            neurofileio::WtiRow row;
            row.row     = static_cast<int>(R.rows.size());
            row.unitId  = unit;
            row.link    = link;
            row.bin     = b;
            const std::pair<double,double> ab = window(b);
            row.a = ab.first; row.b = ab.second;
            row.nSpikes = nsp;
            R.rows.push_back(row);

            // Subsample positions (into `members`) to ≤ maxPer.
            std::vector<std::size_t> sub = members;
            if (static_cast<int>(sub.size()) > p.maxPer) {
                const std::vector<std::size_t> keep = linspace_indices(sub.size(), p.maxPer);
                std::vector<std::size_t> picked(keep.size());
                for (std::size_t j = 0; j < keep.size(); ++j) picked[j] = sub[keep[j]];
                sub.swap(picked);
            }

            for (const std::string& v : variants) {
                const std::vector<int16_t>& stack = spk.at(v);
                std::vector<int16_t> med(recLen, 0);
                if (!sub.empty()) {
                    // Materialise the bin's (possibly offset-aligned) waveforms, then
                    // take the per-element median across them.
                    std::vector<std::vector<float>> W(sub.size());
                    for (std::size_t m = 0; m < sub.size(); ++m) {
                        const int64_t i = idx[sub[m]];
                        const int16_t* rec = &stack[static_cast<std::size_t>(i) * recLen];
                        if (p.eap) W[m] = align_to_res(rec, off[sub[m]], nsamp, nchan);
                        else {
                            W[m].resize(recLen);
                            for (std::size_t e = 0; e < recLen; ++e) W[m][e] = static_cast<float>(rec[e]);
                        }
                    }
                    std::vector<float> col(sub.size());
                    for (std::size_t e = 0; e < recLen; ++e) {
                        for (std::size_t m = 0; m < sub.size(); ++m) col[m] = W[m][e];
                        const double md = median_np(col);          // np.median
                        med[e] = static_cast<int16_t>(std::rint(md)); // np.rint → int16
                    }
                }
                std::vector<int16_t>& out = R.wtf[v];
                out.insert(out.end(), med.begin(), med.end());
            }
        }
    };

    for (int u : units) {
        std::vector<int64_t> idx;
        std::vector<int>     off;   // eap member offsets (parallel to idx)
        if (p.eap) {
            const int T = eapPtr->nClasses;
            for (int64_t i = 0; i < N; ++i) {
                const int8_t cell = eapPtr->cells[static_cast<std::size_t>(i) * T + u];
                if (neurofileio::eapPresent(cell)) { idx.push_back(i); off.push_back(static_cast<int>(cell)); }
            }
        } else {
            for (int64_t i = 0; i < N; ++i) if ((*clu)[static_cast<std::size_t>(i)] == u) idx.push_back(i);
        }

        if (p.linkDrift) {
            std::vector<int> binOf(idx.size());
            for (std::size_t k = 0; k < idx.size(); ++k) binOf[k] = chOf[static_cast<std::size_t>(idx[k])];
            emit_series(u, "drift", nC, idx, binOf, off,
                [&](int b) {
                    return std::make_pair(round_nd(edges[static_cast<std::size_t>(b)]     / p.sr, 3),
                                          round_nd(edges[static_cast<std::size_t>(b) + 1] / p.sr, 3));
                });
        }
        if (p.linkAdapt && !idx.empty()) {
            const std::vector<int16_t>& sref = spk.at(eref);
            std::vector<double> E(idx.size());
            for (std::size_t k = 0; k < idx.size(); ++k)
                E[k] = spike_energy(&sref[static_cast<std::size_t>(idx[k]) * recLen], recLen);
            std::vector<double> esorted = E; std::sort(esorted.begin(), esorted.end());
            const std::vector<double> eedges = energy_edges(esorted, p.nEnergy);
            std::vector<int> binOf(idx.size());
            for (std::size_t k = 0; k < idx.size(); ++k) binOf[k] = assign_bin(eedges, E[k]);
            emit_series(u, "adapt", p.nEnergy, idx, binOf, off,
                [&](int b) {
                    return std::make_pair(round_nd(eedges[static_cast<std::size_t>(b)],     1),
                                          round_nd(eedges[static_cast<std::size_t>(b) + 1], 1));
                });
        }
    }

    R.ok = true;
    return R;
}

} // namespace templategen
} // namespace neurosuite

#endif // NEUROSUITE_CORE_TEMPLATE_GENERATE_HPP
