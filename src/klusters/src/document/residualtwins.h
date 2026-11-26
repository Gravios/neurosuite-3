/***************************************************************************
 * residualtwins.h -- is a residual spike a row already?
 *
 * Header-only and Qt-free.  The residual of a collision row is its partner's
 * spike (klustersdoc_residuals.cpp, formats/spkr.md), and that spike may be
 * known already.  A tool that turns residual spikes into rows must add each
 * spike once, so every residual gets a verdict:
 *
 *   Detected    a row lies within `tolerance` samples of the residual's time: the
 *               spike was detected on its own.  twin = that row -- the nearest in
 *               the PARTNER's cluster when it has one (the two rows are then one
 *               collision seen from both sides), else the nearest in any cluster
 *               (the spike is a row, but not where the partner named says: the
 *               reader compares the twin's cluster with the partner).
 *   Repeated    not detected, but another residual of the same partner lies
 *               within `tolerance` of it: one partner spike in the windows of
 *               two rows.  Of such a run the residual of the LOWEST row is kept
 *               (Recovered); the others name it as their twin.  A run is chained
 *               from its first time: a residual joins while it is within
 *               `tolerance` of the run's first.
 *   Recovered   neither: a spike no row holds.  twin = -1.
 *
 * A residual whose partner is unknown (<= 1: no cluster, or the noise and
 * artefact bins) can still be Detected -- any row at its time is the spike --
 * but is never Repeated: two unknowns are not known to be the same spike.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace residualtwins {

enum Kind { Recovered = 0, Detected = 1, Repeated = 2 };   // neurofileio::SpkrKind's values

/** A collision row's residual spike. */
struct Spike {
    long long time = 0;      ///< the row's time + the partner's fitted offset
    long long row = 0;       ///< the collision row
    int       partner = -1;  ///< the partner's cluster, <= 1 when unknown
};

struct Verdict {
    int       kind = Recovered;
    long long twin = -1;
};

using Rows = std::vector<std::pair<long long, long long>>;   ///< (time, row), ascending

/** The row of @p rows nearest to @p time within @p tolerance, other than @p self; -1 if none. */
inline long long nearestRow(const Rows& rows, long long time, long long tolerance, long long self)
{
    auto at = std::lower_bound(rows.begin(), rows.end(), std::make_pair(time - tolerance, -1LL));
    long long best = -1, bestGap = tolerance + 1;
    for (; at != rows.end() && at->first <= time + tolerance; ++at) {
        if (at->second == self) continue;
        const long long gap = at->first > time ? at->first - time : time - at->first;
        if (gap < bestGap) { bestGap = gap; best = at->second; }
    }
    return best;
}

/** One verdict per spike, in the order given.  @p rowsOf(cluster) returns the cluster's own
 *  rows, @p allRows every row of the group. */
template <class RowsOf>
inline std::vector<Verdict> classify(const std::vector<Spike>& spikes, RowsOf rowsOf, const Rows& allRows, long long tolerance)
{
    std::vector<Verdict> out(spikes.size());
    std::vector<std::size_t> open;                         // not detected, partner known
    for (std::size_t i = 0; i < spikes.size(); ++i) {
        const Spike& s = spikes[i];
        long long best = s.partner > 1 ? nearestRow(rowsOf(s.partner), s.time, tolerance, s.row) : -1;
        if (best < 0) best = nearestRow(allRows, s.time, tolerance, s.row);
        if (best >= 0) { out[i].kind = Detected; out[i].twin = best; }
        else if (s.partner > 1) open.push_back(i);
    }
    std::sort(open.begin(), open.end(), [&](std::size_t a, std::size_t b) {
        if (spikes[a].partner != spikes[b].partner) return spikes[a].partner < spikes[b].partner;
        if (spikes[a].time != spikes[b].time) return spikes[a].time < spikes[b].time;
        return spikes[a].row < spikes[b].row;
    });
    for (std::size_t from = 0; from < open.size();) {
        std::size_t to = from + 1;
        while (to < open.size() && spikes[open[to]].partner == spikes[open[from]].partner
               && spikes[open[to]].time - spikes[open[from]].time <= tolerance) ++to;
        if (to - from > 1) {
            std::size_t keeper = open[from];
            for (std::size_t k = from; k < to; ++k)
                if (spikes[open[k]].row < spikes[keeper].row) keeper = open[k];
            for (std::size_t k = from; k < to; ++k)
                if (open[k] != keeper) { out[open[k]].kind = Repeated; out[open[k]].twin = spikes[keeper].row; }
        }
        from = to;
    }
    return out;
}

} // namespace residualtwins
