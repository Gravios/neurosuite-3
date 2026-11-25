/* burstfollowers.h -- which of two similarly shaped clusters a burst's spikes belong to.
 *
 * A complex-spike burst is one cell firing two or more spikes a few milliseconds
 * apart, each smaller than the last.  When two cells have similar waveforms, the
 * shrunken later spikes of one cell's bursts are easily clustered with the other
 * cell.  The waveform cannot settle it (that is the premise); the timing can: two
 * different cells seldom fire one after the other inside a burst interval.
 *
 * The two clusters' spikes are pooled and sorted by time.  A BURST is a maximal
 * run whose consecutive intervals are at most maxIsi.  Its LEADER is its first
 * spike -- the largest and least adapted, hence the best classified -- and the
 * leader's cluster is the burst's REFERENCE cell.  Every later spike of the burst
 * that carries the other cluster's label is a FOLLOWER: a spike that should be
 * with the reference cell.  A burst in which spikes of the two clusters fall
 * closer than minIsi is two cells firing together, not one cell bursting: it is
 * left alone (collided).
 *
 * Chance, twice.  Two different cells also fall into each other's bursts, for two
 * reasons, and a direction is SIGNIFICANT only when it stands above both (and
 * holds at least minCount followers):
 *   - unrelated firing.  The same count is taken with one train shifted against
 *     the other by each of `shifts` (tens of ms: longer than a burst, shorter than
 *     the rate changes); the mean is the count expected of independent trains,
 *     tested as a Poisson mean (pValue);
 *   - synchrony.  Cells that fire together within a few ms (ripples, a shared
 *     field) are above the shifted count without being one cell.  But firing
 *     together has no order: the other cluster's spikes come BEFORE the reference
 *     cluster's as often as after.  A cell's own burst has: the misplaced spikes
 *     are behind the leader.  So the same count is taken on the train run
 *     backwards (the spikes of the other cluster ahead of a burst's LAST spike),
 *     and the forward count is tested against it as an even split (pOrder).
 * With many spikes a small excess passes any test, so the count must also be at
 * least minRatio times each chance count: a few percent more spikes behind than
 * ahead is a synapse or a shared rhythm, not misplaced spikes.
 * Both tests count BURSTS, not spikes: followers arrive several at a time (a
 * whole burst of the other cell landing behind a leader), so the spike count is
 * over-dispersed and a test on it calls clean pairs.
 *
 * Qt-free and header-only (klusters_test_burstfollowers).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BURSTFOLLOWERS_H
#define BURSTFOLLOWERS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace burstfollowers {

struct Params {
    double minIsi = 0.0;              ///< samples: closer than this, two clusters' spikes are two cells
    double maxIsi = 0.0;              ///< samples: the longest interval inside a burst
    std::vector<double> shifts;       ///< samples: the surrogate shifts of one train against the other
    double alpha    = 1.0e-3;         ///< both tail probabilities must be below this
    long   minCount = 5;              ///< fewer followers than this are never significant
    double minRatio = 2.0;            ///< ... and so are fewer bursts than this many times either chance count
};

/** The followers of one reference cell: spikes labelled as the OTHER cluster in
 *  bursts the reference cluster leads. */
struct Direction {
    std::vector<int64_t> rows;        ///< the followers' row ids, in time order
    long   observed    = 0;           ///< rows.size()
    double expected    = 0.0;         ///< the followers expected by chance (mean over the shifts)
    long   bursts      = 0;           ///< the bursts those followers are in
    double expectedBursts = 0.0;      ///< ... expected by chance
    double pValue      = 1.0;         ///< P(count >= bursts | Poisson(expectedBursts))
    long   reversedBursts = 0;        ///< the same count on the train run backwards
    double pOrder      = 1.0;         ///< P(count >= bursts | bursts + reversedBursts, even split)
    bool   significant = false;
};

struct Result {
    Direction ofA;                    ///< B-labelled followers in bursts led by A: they should be A
    Direction ofB;                    ///< A-labelled followers in bursts led by B: they should be B
    long bursts   = 0;                ///< runs of two or more spikes
    long mixed    = 0;                ///< ... holding both labels and not collided
    long collided = 0;                ///< ... with the two labels closer than minIsi
};

namespace detail {

struct Spike { double t; int64_t row; bool isA; };

inline std::vector<Spike> pooled(const std::vector<int64_t>& timesA, const std::vector<int64_t>& rowsA,
                                 const std::vector<int64_t>& timesB, const std::vector<int64_t>& rowsB,
                                 double shiftB)
{
    std::vector<Spike> s;
    s.reserve(timesA.size() + timesB.size());
    for (size_t i = 0; i < timesA.size(); ++i)
        s.push_back({static_cast<double>(timesA[i]), rowsA[i], true});
    for (size_t i = 0; i < timesB.size(); ++i)
        s.push_back({static_cast<double>(timesB[i]) + shiftB, rowsB[i], false});
    std::stable_sort(s.begin(), s.end(), [](const Spike& x, const Spike& y) { return x.t < y.t; });
    return s;
}

/** One pass over the pooled train.  @p rowsOfA / @p rowsOfB collect the followers
 *  when not null; the follower counts are returned through @p nOfA / @p nOfB and
 *  the numbers of bursts holding them through @p burstsOfA / @p burstsOfB. */
inline void scan(const std::vector<Spike>& s, double minIsi, double maxIsi,
                 long& nOfA, long& nOfB, long& burstsOfA, long& burstsOfB,
                 long* bursts, long* mixed, long* collided,
                 std::vector<int64_t>* rowsOfA, std::vector<int64_t>* rowsOfB)
{
    nOfA = nOfB = burstsOfA = burstsOfB = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t j = i + 1;
        bool hasA = s[i].isA, hasB = !s[i].isA, collide = false;
        while (j < s.size() && s[j].t - s[j - 1].t <= maxIsi) {
            if (s[j].isA != s[j - 1].isA && s[j].t - s[j - 1].t < minIsi) collide = true;
            hasA = hasA || s[j].isA;
            hasB = hasB || !s[j].isA;
            ++j;
        }
        if (j - i >= 2) {
            if (bursts) ++*bursts;
            if (collide) {
                if (collided) ++*collided;
            } else if (hasA && hasB) {
                if (mixed) ++*mixed;
                const bool leaderIsA = s[i].isA;
                bool any = false;
                for (size_t k = i + 1; k < j; ++k) {
                    if (s[k].isA == leaderIsA) continue;
                    any = true;
                    if (leaderIsA) { ++nOfA; if (rowsOfA) rowsOfA->push_back(s[k].row); }
                    else           { ++nOfB; if (rowsOfB) rowsOfB->push_back(s[k].row); }
                }
                if (any) ++(leaderIsA ? burstsOfA : burstsOfB);
            }
        }
        i = j;
    }
}

/** P(K >= k) for K ~ Poisson(lambda). */
inline double poissonTail(long k, double lambda)
{
    if (k <= 0) return 1.0;
    if (lambda <= 0.0) return 0.0;
    // 1 - sum_{i<k} e^-l l^i / i!, summed in logs to stay finite for large counts.
    double below = 0.0;
    for (long i = 0; i < k; ++i)
        below += std::exp(-lambda + static_cast<double>(i) * std::log(lambda) - std::lgamma(static_cast<double>(i) + 1.0));
    if (below < 0.999999) return 1.0 - below;
    // The tail is tiny: sum it directly (the terms fall off fast beyond k > lambda).
    double tail = 0.0;
    for (long i = k; i < k + 2000; ++i) {
        const double term = std::exp(-lambda + static_cast<double>(i) * std::log(lambda) - std::lgamma(static_cast<double>(i) + 1.0));
        tail += term;
        if (term < tail * 1.0e-15) break;
    }
    return tail;
}

/** P(K >= k) for K ~ Binomial(n, 1/2). */
inline double evenSplitTail(long k, long n)
{
    if (k <= 0) return 1.0;
    if (k > n) return 0.0;
    double tail = 0.0;
    for (long i = k; i <= n; ++i)
        tail += std::exp(std::lgamma(static_cast<double>(n) + 1.0) - std::lgamma(static_cast<double>(i) + 1.0)
                         - std::lgamma(static_cast<double>(n - i) + 1.0) - static_cast<double>(n) * std::log(2.0));
    return std::min(tail, 1.0);
}

} // namespace detail

/** @p timesA / @p timesB: the two clusters' spike times in samples (any order);
 *  @p rowsA / @p rowsB: an id per spike, returned for the followers. */
inline Result analyse(const std::vector<int64_t>& timesA, const std::vector<int64_t>& rowsA,
                      const std::vector<int64_t>& timesB, const std::vector<int64_t>& rowsB,
                      const Params& p)
{
    Result r;
    if (timesA.size() != rowsA.size() || timesB.size() != rowsB.size() || p.maxIsi <= 0.0) return r;

    r.ofA.rows.clear();
    r.ofB.rows.clear();
    long nOfA = 0, nOfB = 0;
    detail::scan(detail::pooled(timesA, rowsA, timesB, rowsB, 0.0), p.minIsi, p.maxIsi,
                 nOfA, nOfB, r.ofA.bursts, r.ofB.bursts, &r.bursts, &r.mixed, &r.collided,
                 &r.ofA.rows, &r.ofB.rows);
    r.ofA.observed = nOfA;
    r.ofB.observed = nOfB;

    double sumA = 0.0, sumB = 0.0, sumBurstsA = 0.0, sumBurstsB = 0.0;
    for (const double shift : p.shifts) {
        long a = 0, b = 0, ba = 0, bb = 0;
        detail::scan(detail::pooled(timesA, rowsA, timesB, rowsB, shift), p.minIsi, p.maxIsi,
                     a, b, ba, bb, nullptr, nullptr, nullptr, nullptr, nullptr);
        sumA += static_cast<double>(a);
        sumB += static_cast<double>(b);
        sumBurstsA += static_cast<double>(ba);
        sumBurstsB += static_cast<double>(bb);
    }
    // The train run backwards: every time negated, so the order within a burst flips.
    {
        std::vector<int64_t> backA(timesA.size()), backB(timesB.size());
        for (size_t i = 0; i < timesA.size(); ++i) backA[i] = -timesA[i];
        for (size_t i = 0; i < timesB.size(); ++i) backB[i] = -timesB[i];
        long a = 0, b = 0;
        detail::scan(detail::pooled(backA, rowsA, backB, rowsB, 0.0), p.minIsi, p.maxIsi,
                     a, b, r.ofA.reversedBursts, r.ofB.reversedBursts, nullptr, nullptr, nullptr, nullptr, nullptr);
        r.ofA.pOrder = detail::evenSplitTail(r.ofA.bursts, r.ofA.bursts + r.ofA.reversedBursts);
        r.ofB.pOrder = detail::evenSplitTail(r.ofB.bursts, r.ofB.bursts + r.ofB.reversedBursts);
    }

    const double n = static_cast<double>(p.shifts.size());
    if (n > 0.0) {
        // Never below half a spike over all the shifts: a zero mean from a handful
        // of surrogates is "less than we could measure", not "impossible".
        r.ofA.expected = sumA / n;
        r.ofB.expected = sumB / n;
        r.ofA.expectedBursts = std::max(sumBurstsA, 0.5) / n;
        r.ofB.expectedBursts = std::max(sumBurstsB, 0.5) / n;
        r.ofA.pValue = detail::poissonTail(r.ofA.bursts, r.ofA.expectedBursts);
        r.ofB.pValue = detail::poissonTail(r.ofB.bursts, r.ofB.expectedBursts);
        const auto significant = [&p](const Direction& d) {
            const double bursts = static_cast<double>(d.bursts);
            return d.observed >= p.minCount && d.pValue < p.alpha && d.pOrder < p.alpha
                && bursts >= p.minRatio * d.expectedBursts
                && bursts >= p.minRatio * static_cast<double>(d.reversedBursts);
        };
        r.ofA.significant = significant(r.ofA);
        r.ofB.significant = significant(r.ofB);
    }
    return r;
}

} // namespace burstfollowers

#endif // BURSTFOLLOWERS_H
