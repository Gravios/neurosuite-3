/***************************************************************************
 * duplicatespikes.h -- one spike held by more than one row
 *
 * Header-only and Qt-free.  A detector that fires twice on one spike, or two
 * detections that a realignment brings to the same peak, leave two rows for one
 * spike.  In a cluster they are rows closer in time than any neuron can fire
 * twice: a RUN of rows of one cluster in which every row is within `tolerance`
 * samples of the run's first.  (Chained from the first, not from the previous
 * row: three rows 5 samples apart with a tolerance of 6 are a run of two and a
 * row, not one spike 10 samples long.)
 *
 * This finds the runs.  Which row of a run to keep is the caller's (the one
 * most like the cluster's template: KlustersDoc::scanDuplicateSpikes).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace duplicatespikes {

/** The runs of two or more among @p times (ascending), as [first, onePastLast) index pairs. */
template <class T>
inline std::vector<std::pair<std::size_t, std::size_t>> runs(const std::vector<T>& times, T tolerance)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t from = 0; from < times.size();) {
        std::size_t to = from + 1;
        while (to < times.size() && times[to] - times[from] <= tolerance) ++to;
        if (to - from > 1) out.emplace_back(from, to);
        from = to;
    }
    return out;
}

/** Of @p times (ascending) with their @p labels: the neighbouring pairs within @p tolerance
 *  whose labels differ -- one spike in two clusters -- as index pairs (i, i + 1). */
template <class T, class L>
inline std::vector<std::pair<std::size_t, std::size_t>> acrossLabels(const std::vector<T>& times, const std::vector<L>& labels, T tolerance)
{
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t i = 0; i + 1 < times.size(); ++i)
        if (times[i + 1] - times[i] <= tolerance && labels[i] != labels[i + 1]) out.emplace_back(i, i + 1);
    return out;
}

} // namespace duplicatespikes
