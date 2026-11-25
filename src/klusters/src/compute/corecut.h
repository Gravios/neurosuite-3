/***************************************************************************
 * corecut.h  --  the core of a cluster, and the spikes that are not it
 *
 * Qt-free and header-only, so the analysis is unit-tested on its own
 * (test/corecut_test.cpp) and KlustersDoc::detectCoreCut only reads the
 * waveforms and carries the result.
 *
 * One cluster's spikes are put in the order in which each next spike changes
 * the cumulative mean waveform least, and the order is cut where spikes that
 * are unlike the ones already taken start to come in:
 *
 *   1. CENTRE.  Every spike is moved (sub-sample, cubic) so that the point
 *      where its falling flank crosses half its trough depth, on the cluster's
 *      primary channel, lands where the median waveform's does.  The trough is
 *      a noisy landmark on a single spike; the flank is steep, so its crossing
 *      is not.
 *   2. WINDOW.  A tapered window is laid asymmetrically around that crossing:
 *      a short rise before it, flat over the trough and the rise after it, a
 *      longer fall.  What lies outside is the part of the snippet where other
 *      units' spikes and the background decide the distance.
 *   3. ALL CHANNELS, unweighted.  A weak channel is where a neighbouring cell
 *      shows itself; weighting by the cluster's own waveform hides it.
 *   4. ORDER.  Seed: the spike nearest the sample-wise median.  Then always
 *      the spike nearest the running mean -- adding spike x to a mean m of k
 *      spikes moves it by |x - m| / (k + 1).  The dominant unit's ordinary
 *      spikes come first and the running mean barely moves while they last.
 *   5. CUT.  Along the order, per channel, the excess kurtosis of the spikes
 *      taken so far (at every window sample, averaged) and its change over a
 *      span of 2% of the spikes.  The kurtosis is flat while the spikes taken
 *      are of one kind and starts to rise when far-out ones come in.  The
 *      onset on a channel is the first rank past half the order where that
 *      change exceeds onsetFactor times its mean size between 20% and 50% of
 *      the order; the cut is the EARLIEST onset over the channels.  (The
 *      largest change is always at the very end, where the last few spikes
 *      dominate the fourth moment: the onset is the knee, the maximum is not.)
 *
 * Spikes past the cut are the ones to split off.  The dominant unit is assumed
 * to be more than half the cluster: the baseline is taken from the first half
 * of the order.  No onset on any channel: nothing is cut.
 *
 * The order is quadratic in the number of spikes.  Above Params::maxOrdered an
 * evenly spaced subset is ordered and cut, and the others are kept when they
 * lie within the cut's own step of the kept subset's mean.  An OrderFn can
 * replace the ordering loop (the GPU backend does); it must return the same
 * order the loop here would, up to rounding.
 *
 * Measured on jg05-20120312 group 8 against a curated clustering
 * (analysis/core-cut in the project notes): the share of a parent's spikes in
 * the curator's largest unit rose from 75% to 97%, 66% to 80% and 56% to 78%
 * on the three parents the recipe was arrived at on, and from 79-83% to
 * 88-97% on four it was not, keeping 57-87% of that unit.  A unit the curator
 * had left whole lost 45% of its spikes: the cut errs on the side of splitting.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

namespace corecut {

struct Params {
    double sampleRateHz = 20000.0;   ///< of the waveform samples (a super-sampled space: rate x factor)
    // The window, in ms relative to the first half crossing.
    double riseStartMs  = -0.154;
    double flatStartMs  = -0.031;
    double flatEndMs    =  0.184;
    double fallEndMs    =  0.430;
    double maxShiftMs   =  0.184;    ///< a spike is never moved further than this
    double searchMs     =  0.280;    ///< a spike's trough is looked for this far either side of the median's
    double baselineMs   =  0.246;    ///< the snippet's first samples: the level the trough depth is measured from
    double onsetFactor  =  5.0;
    long   minSpikes    =  200;      ///< below this the kurtosis has nothing to stand on
    long   maxOrdered   =  40000;    ///< above this an evenly spaced subset is ordered
};

struct Result {
    bool        ok = false;          ///< the analysis ran (cut == ordered means: nothing to split off)
    const char* reason = "";         ///< why not, or why nothing is cut
    int         primaryChannel = -1;
    int         cutChannel = -1;     ///< the channel whose onset set the cut, -1 when none
    long        ordered = 0;         ///< spikes that were ordered (all of them, or the subset)
    long        cut = 0;             ///< ordered spikes kept: ranks [0, cut)
    double      cutStep = 0.0;       ///< |x - m| of the spikes around the cut
    bool        hookUsed = false;    ///< the OrderFn ran the ordering
    std::vector<long> order;         ///< the ordered spikes' indices, in order
    std::vector<char> keep;          ///< per input spike: 1 kept, 0 to split off
};

/** Replaces the ordering loop.  x: n rows of d values, row-major; seed: the
 *  first spike.  Fills order[n] (order[0] == seed) and step[n] (|x - m| at
 *  which each was taken, step[0] == 0).  Returns false to have the loop here
 *  run instead. */
using OrderFn = std::function<bool(long n, int d, const float* x, long seed, long* order, double* step)>;

namespace detail {

/** Raised-cosine flanks, 1 between f0 and f1, 0 outside (r0, r1); x relative to the crossing. */
inline double taper(double x, double r0, double f0, double f1, double r1)
{
    static const double pi = 3.14159265358979323846;
    if (x >= f0 && x <= f1) return 1.0;
    if (x > r0 && x < f0) return 0.5 - 0.5 * std::cos(pi * (x - r0) / (f0 - r0));
    if (x > f1 && x < r1) return 0.5 + 0.5 * std::cos(pi * (x - f1) / (r1 - f1));
    return 0.0;
}

/** The sample where the falling flank of w crosses half its trough depth, the
 *  trough looked for in [lo, hi]; depth from the mean of the first nBase
 *  samples.  False when w has no trough below that level there. */
inline bool firstHalfCrossing(const float* w, int nSamp, int lo, int hi, int nBase, double& crossing)
{
    double base = 0.0;
    for (int t = 0; t < nBase; ++t) base += w[t];
    base /= nBase;
    int k = lo;
    for (int t = lo + 1; t <= hi; ++t) if (w[t] < w[k]) k = t;
    const double v = w[k] - base;
    if (!(v < 0.0)) return false;
    int a = k;
    while (a > 0 && (w[a] - base) < v / 2) --a;
    if (!((w[a] - base) >= v / 2) || a >= k || a + 1 >= nSamp) return false;
    const double wa = w[a] - base, wb = w[a + 1] - base;
    if (wa == wb) return false;
    crossing = a + (wa - v / 2) / (wa - wb);
    return true;
}

/** w at the fractional sample t (Catmull-Rom, the ends held). */
inline float cubicAt(const float* w, int nSamp, double t)
{
    if (t <= 0.0) return w[0];
    if (t >= nSamp - 1) return w[nSamp - 1];
    const int i = static_cast<int>(t);
    const float u = static_cast<float>(t - i);
    const float p0 = w[std::max(i - 1, 0)], p1 = w[i], p2 = w[std::min(i + 1, nSamp - 1)], p3 = w[std::min(i + 2, nSamp - 1)];
    return p1 + 0.5f * u * (p2 - p0 + u * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + u * (3.0f * (p1 - p2) + p3 - p0)));
}

inline float medianOf(std::vector<float>& v)
{
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    return v[mid];
}

/** The ordering loop.  Float products, double sums; ties go to the lower index. */
inline void orderCpu(long n, int d, const float* x, long seed, long* order, double* step)
{
    std::vector<double> x2(static_cast<std::size_t>(n)), xm(static_cast<std::size_t>(n), 0.0);
    std::vector<char>   left(static_cast<std::size_t>(n), 1);
    std::vector<double> m(static_cast<std::size_t>(d));
    for (long i = 0; i < n; ++i) {
        const float* xi = x + static_cast<std::size_t>(i) * d;
        double s = 0.0;
        for (int u = 0; u < d; ++u) s += static_cast<double>(xi[u]) * xi[u];
        x2[static_cast<std::size_t>(i)] = s;
    }
    for (int u = 0; u < d; ++u) m[static_cast<std::size_t>(u)] = x[static_cast<std::size_t>(seed) * d + u];
    order[0] = seed;
    step[0] = 0.0;
    long j = seed;                                   // the spike taken last
    for (long k = 1; k < n; ++k) {
        // The spike taken last enters every x.m (m moved by (x_j - m) / k), and
        // the nearest of the spikes left is found in the same pass.
        const float* xj = x + static_cast<std::size_t>(j) * d;
        const double inv = 1.0 / static_cast<double>(k);
        left[static_cast<std::size_t>(j)] = 0;
        long   best = -1;
        double bestKey = std::numeric_limits<double>::infinity();
#if defined(_OPENMP)
#pragma omp parallel if (n >= 2000)
#endif
        {
            long   myBest = -1;
            double myKey = std::numeric_limits<double>::infinity();
#if defined(_OPENMP)
#pragma omp for nowait schedule(static)
#endif
            for (long i = 0; i < n; ++i) {
                const float* xi = x + static_cast<std::size_t>(i) * d;
                float dot = 0.0f;
                for (int u = 0; u < d; ++u) dot += xi[u] * xj[u];
                double& v = xm[static_cast<std::size_t>(i)];
                v += (static_cast<double>(dot) - v) * inv;
                if (!left[static_cast<std::size_t>(i)]) continue;
                const double key = x2[static_cast<std::size_t>(i)] - 2.0 * v;      // |x - m|^2 less the common m.m
                if (key < myKey) { myKey = key; myBest = i; }
            }
#if defined(_OPENMP)
#pragma omp critical(corecut_order)
#endif
            if (myBest >= 0 && (myKey < bestKey || (myKey == bestKey && myBest < best))) { bestKey = myKey; best = myBest; }
        }
        // m as it stands with k spikes in it (the last one, j, now added).
        if (k > 1)
            for (int u = 0; u < d; ++u) m[static_cast<std::size_t>(u)] += (static_cast<double>(xj[u]) - m[static_cast<std::size_t>(u)]) * inv;
        double mm = 0.0;
        for (int u = 0; u < d; ++u) mm += m[static_cast<std::size_t>(u)] * m[static_cast<std::size_t>(u)];
        order[k] = best;
        step[k] = std::sqrt(std::max(bestKey + mm, 0.0));
        j = best;
    }
}

} // namespace detail

/** @p wav: n spikes, each nCh x nSamp floats, channel-major ([ch * nSamp + t]). */
inline Result analyse(const float* wav, long n, int nCh, int nSamp, const Params& p, const OrderFn& hook = {})
{
    Result R;
    R.keep.assign(static_cast<std::size_t>(std::max(n, 0L)), 1);
    if (!wav || nCh <= 0 || nSamp < 8) { R.reason = "no waveforms"; return R; }
    if (n < p.minSpikes) { R.reason = "too few spikes"; return R; }
    const std::size_t rec = static_cast<std::size_t>(nCh) * nSamp;
    const double perMs = p.sampleRateHz / 1000.0;

    // ── the median waveform (<= 2000 evenly spaced spikes): primary channel, trough, crossing ──
    const long strideT = std::max(1L, n / 2000L);
    std::vector<float> med(rec), col;
    for (std::size_t q = 0; q < rec; ++q) {
        col.clear();
        for (long i = 0; i < n; i += strideT) col.push_back(wav[static_cast<std::size_t>(i) * rec + q]);
        med[q] = detail::medianOf(col);
    }
    double bestPtp = -1.0;
    for (int c = 0; c < nCh; ++c) {
        const float* w = &med[static_cast<std::size_t>(c) * nSamp];
        const auto mm = std::minmax_element(w, w + nSamp);
        if (*mm.second - *mm.first > bestPtp) { bestPtp = *mm.second - *mm.first; R.primaryChannel = c; }
    }
    const float* medP = &med[static_cast<std::size_t>(R.primaryChannel) * nSamp];
    const int trough = static_cast<int>(std::min_element(medP, medP + nSamp) - medP);
    const int nBase  = std::clamp(static_cast<int>(std::lround(p.baselineMs * perMs)), 2, nSamp / 2);
    const int search = std::max(2, static_cast<int>(std::lround(p.searchMs * perMs)));
    const int lo = std::max(1, trough - search), hi = std::min(nSamp - 2, trough + search);
    double target = 0.0;
    if (lo >= hi || !detail::firstHalfCrossing(medP, nSamp, lo, hi, nBase, target)) {
        R.reason = "the median waveform has no trough to centre on";
        return R;
    }
    const double maxShift = p.maxShiftMs * perMs;

    // ── the window's samples and weights ──
    std::vector<int>   winT;
    std::vector<float> winW;
    for (int t = 0; t < nSamp; ++t) {
        const double w = detail::taper(t - target, p.riseStartMs * perMs, p.flatStartMs * perMs, p.flatEndMs * perMs, p.fallEndMs * perMs);
        if (w > 0.0) { winT.push_back(t); winW.push_back(static_cast<float>(w)); }
    }
    const int nWin = static_cast<int>(winT.size());
    if (nWin < 4) { R.reason = "the window falls outside the snippet"; return R; }
    const int d = nWin * nCh;                        // [s * nCh + c]: window sample s, channel c

    // ── centred, windowed spikes ──
    std::vector<float> X(static_cast<std::size_t>(n) * d);
#if defined(_OPENMP)
#pragma omp parallel for schedule(static) if (n >= 2000)
#endif
    for (long i = 0; i < n; ++i) {
        const float* w = wav + static_cast<std::size_t>(i) * rec;
        double crossing = 0.0, shift = 0.0;
        if (detail::firstHalfCrossing(w + static_cast<std::size_t>(R.primaryChannel) * nSamp, nSamp, lo, hi, nBase, crossing))
            shift = std::clamp(target - crossing, -maxShift, maxShift);
        float* xi = &X[static_cast<std::size_t>(i) * d];
        for (int s = 0; s < nWin; ++s)
            for (int c = 0; c < nCh; ++c)
                xi[s * nCh + c] = winW[static_cast<std::size_t>(s)]
                                  * detail::cubicAt(w + static_cast<std::size_t>(c) * nSamp, nSamp, winT[static_cast<std::size_t>(s)] - shift);
    }

    // ── the spikes that are ordered ──
    const long nOrd = std::min(n, std::max(p.maxOrdered, p.minSpikes));
    std::vector<long> pick(static_cast<std::size_t>(nOrd));
    for (long k = 0; k < nOrd; ++k) pick[static_cast<std::size_t>(k)] = (nOrd == n) ? k : static_cast<long>((static_cast<double>(k) * n) / nOrd);
    std::vector<float> subset;
    const float* Xo = X.data();
    if (nOrd != n) {
        subset.resize(static_cast<std::size_t>(nOrd) * d);
        for (long k = 0; k < nOrd; ++k)
            std::copy_n(&X[static_cast<std::size_t>(pick[static_cast<std::size_t>(k)]) * d], d, &subset[static_cast<std::size_t>(k) * d]);
        Xo = subset.data();
    }

    // ── seed: nearest the sample-wise median (of <= 4000 of them) ──
    const long strideS = std::max(1L, nOrd / 4000L);
    std::vector<float> medX(static_cast<std::size_t>(d));
    for (int u = 0; u < d; ++u) {
        col.clear();
        for (long k = 0; k < nOrd; k += strideS) col.push_back(Xo[static_cast<std::size_t>(k) * d + u]);
        medX[static_cast<std::size_t>(u)] = detail::medianOf(col);
    }
    long seed = 0;
    double seedDist = std::numeric_limits<double>::infinity();
    for (long k = 0; k < nOrd; ++k) {
        double s = 0.0;
        const float* xk = Xo + static_cast<std::size_t>(k) * d;
        for (int u = 0; u < d; ++u) { const double e = static_cast<double>(xk[u]) - medX[static_cast<std::size_t>(u)]; s += e * e; }
        if (s < seedDist) { seedDist = s; seed = k; }
    }

    // ── order ──
    std::vector<long>   order(static_cast<std::size_t>(nOrd));
    std::vector<double> step(static_cast<std::size_t>(nOrd));
    if (hook && hook(nOrd, d, Xo, seed, order.data(), step.data())) R.hookUsed = true;
    else detail::orderCpu(nOrd, d, Xo, seed, order.data(), step.data());

    // ── cumulative kurtosis per channel along the order, and its onset ──
    const long span = std::max(20L, nOrd / 50L);
    const long half = nOrd / 2, fifth = nOrd / 5;
    long cut = nOrd;
    std::vector<long> onsetOf(static_cast<std::size_t>(nCh), nOrd);
#if defined(_OPENMP)
#pragma omp parallel for schedule(dynamic)
#endif
    for (int c = 0; c < nCh; ++c) {
        std::vector<double> K(static_cast<std::size_t>(nOrd), 0.0);
        for (int s = 0; s < nWin; ++s) {
            double s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
            for (long k = 0; k < nOrd; ++k) {
                const double v = Xo[static_cast<std::size_t>(order[static_cast<std::size_t>(k)]) * d + s * nCh + c];
                s1 += v; s2 += v * v; s3 += v * v * v; s4 += v * v * v * v;
                const double kk = static_cast<double>(k + 1);
                const double m1 = s1 / kk, m2 = s2 / kk, m3 = s3 / kk, m4 = s4 / kk;
                const double var = std::max(m2 - m1 * m1, 1e-9);
                const double mu4 = m4 - 4.0 * m1 * m3 + 6.0 * m1 * m1 * m2 - 3.0 * m1 * m1 * m1 * m1;
                K[static_cast<std::size_t>(k)] += (mu4 / (var * var) - 3.0) / nWin;
            }
        }
        double base = 0.0;
        for (long k = fifth; k < half; ++k)
            base += std::fabs(k >= span ? K[static_cast<std::size_t>(k)] - K[static_cast<std::size_t>(k - span)] : 0.0);
        base /= static_cast<double>(std::max(1L, half - fifth));
        for (long k = half; k < nOrd; ++k) {
            if (k < span) continue;
            if (K[static_cast<std::size_t>(k)] - K[static_cast<std::size_t>(k - span)] > p.onsetFactor * base) {
                onsetOf[static_cast<std::size_t>(c)] = std::max(1L, k - span / 2);
                break;
            }
        }
    }
    for (int c = 0; c < nCh; ++c)
        if (onsetOf[static_cast<std::size_t>(c)] < cut) { cut = onsetOf[static_cast<std::size_t>(c)]; R.cutChannel = c; }

    R.ok = true;
    R.ordered = nOrd;
    R.cut = cut;
    R.order.resize(static_cast<std::size_t>(nOrd));
    for (long k = 0; k < nOrd; ++k) R.order[static_cast<std::size_t>(k)] = pick[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
    if (cut >= nOrd) { R.reason = "no change point: the spikes are of one kind"; return R; }

    // The step around the cut: the distance a spike just inside it has to the mean.
    std::vector<double> near(step.begin() + std::max(1L, cut - span), step.begin() + cut);
    if (!near.empty()) {
        std::nth_element(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(near.size() / 2), near.end());
        R.cutStep = near[near.size() / 2];
    }
    for (long k = cut; k < nOrd; ++k) R.keep[static_cast<std::size_t>(R.order[static_cast<std::size_t>(k)])] = 0;

    if (nOrd != n) {
        // The spikes that were not ordered: kept when within the cut's step of the kept subset's mean.
        std::vector<double> core(static_cast<std::size_t>(d), 0.0);
        for (long k = 0; k < cut; ++k) {
            const float* xk = Xo + static_cast<std::size_t>(order[static_cast<std::size_t>(k)]) * d;
            for (int u = 0; u < d; ++u) core[static_cast<std::size_t>(u)] += xk[u];
        }
        for (int u = 0; u < d; ++u) core[static_cast<std::size_t>(u)] /= static_cast<double>(cut);
        std::vector<char> ordered(static_cast<std::size_t>(n), 0);
        for (const long i : pick) ordered[static_cast<std::size_t>(i)] = 1;
        const double limit = R.cutStep * R.cutStep;
#if defined(_OPENMP)
#pragma omp parallel for schedule(static)
#endif
        for (long i = 0; i < n; ++i) {
            if (ordered[static_cast<std::size_t>(i)]) continue;
            const float* xi = &X[static_cast<std::size_t>(i) * d];
            double s = 0.0;
            for (int u = 0; u < d; ++u) { const double e = static_cast<double>(xi[u]) - core[static_cast<std::size_t>(u)]; s += e * e; }
            R.keep[static_cast<std::size_t>(i)] = (s <= limit) ? 1 : 0;
        }
    }
    return R;
}

} // namespace corecut
