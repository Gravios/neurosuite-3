/***************************************************************************
    resorder.h  -  a realign keeps the .res in time order

    The .res is in time order, and a realign moves times, never rows: so a
    spike may move only as far as the spikes of the rows on either side of it.
    One that would pass a neighbour has slipped -- the alignment locked onto
    another unit's spike colliding with it -- and is left where it was.

    Header-only, no Qt (unit test: test/resorder_test.cpp).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef RESORDER_H
#define RESORDER_H

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace resorder {

/** @p res: the session's times by row, before the move.  @p rows: the rows being moved, with
 *  @p oldTs their times before and @p newTs after (the same order).  Every moved spike whose new
 *  time is before the row before it or after the row after it -- each neighbour at the time it
 *  ends at, a neighbour that also moves included -- gets its old time back in @p newTs, until no
 *  such spike is left.  A neighbour the spike was already out of order with (a .res written
 *  before this rule) does not hold it back.  Returns the indices into @p rows put back. */
template <typename T>
std::vector<std::size_t> keepInOrder(const std::vector<T>& res, const std::vector<int64_t>& rows,
                                     const std::vector<T>& oldTs, std::vector<T>& newTs)
{
    std::vector<std::size_t> back;
    const int64_t n = static_cast<int64_t>(res.size());
    std::unordered_map<int64_t, std::size_t> at;           // row -> index, for the rows that move
    at.reserve(rows.size() * 2);
    for (std::size_t i = 0; i < rows.size(); ++i) at.emplace(rows[i], i);
    const auto now = [&](int64_t row) {                    // where row r ends, so far
        const auto it = at.find(row);
        return it == at.end() ? res[static_cast<std::size_t>(row)] : newTs[it->second];
    };
    const auto before = [&](int64_t row) {                 // where row r was
        const auto it = at.find(row);
        return it == at.end() ? res[static_cast<std::size_t>(row)] : oldTs[it->second];
    };
    std::vector<char> isBack(rows.size(), 0);
    for (bool changed = true; changed;) {
        changed = false;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (isBack[i] || newTs[i] == oldTs[i]) continue;
            const int64_t r = rows[i];
            if (r < 0 || r >= n) continue;
            const bool passesPrev = r > 0 && newTs[i] < now(r - 1) && !(oldTs[i] < before(r - 1));
            const bool passesNext = r + 1 < n && newTs[i] > now(r + 1) && !(oldTs[i] > before(r + 1));
            if (passesPrev || passesNext) {
                newTs[i] = oldTs[i];
                isBack[i] = 1;
                back.push_back(i);
                changed = true;
            }
        }
    }
    return back;
}

/** The rows of @p res whose time is before the row before them. */
template <typename T>
int64_t inversions(const std::vector<T>& res)
{
    int64_t k = 0;
    for (std::size_t i = 1; i < res.size(); ++i)
        if (res[i] < res[i - 1]) ++k;
    return k;
}

} // namespace resorder

#endif // RESORDER_H
