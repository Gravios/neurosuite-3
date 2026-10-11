// spikeorder.h — keep spikes in file order when their timestamps move.
//
// Every per-group artifact of a session (.res, every method's .spk / .fet, the
// .clu) is indexed by the same spike row: row i of each file is the same spike.
// That shared index is what lets one .clu be viewed through any method's
// features, so it must never be permuted.  Realign and nudge move timestamps;
// this helper bounds each move so a spike can never cross its neighbour in
// .res, and the rows therefore never need reordering.
//
// Bound for a moving spike at file row p with current timestamp c:
//   - a neighbour that does NOT move is a hard wall: stay strictly inside it
//     (prev + 1 .. next - 1);
//   - a neighbour that ALSO moves gets half the gap: the two spikes split it at
//     splitPoint(), so whatever each one does they cannot meet or cross.
//   - equal timestamps (a duplicate pair) pin the spike on that side.
// The first / last spike of the file is unbounded on its open side.
//
// Header-only and Qt-free so it is unit-tested directly
// (tests/spikeorder_test.cpp).

#ifndef KLUSTERS_SPIKEORDER_H
#define KLUSTERS_SPIKEORDER_H

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace spikeorder {

/// The largest timestamp the earlier of two neighbours (a < b) may take when
/// both may move; the later one may go no lower than splitPoint(a, b) + 1.
inline int64_t splitPoint(int64_t a, int64_t b)
{
    return a + (b - a - 1) / 2;
}

/// Allowed [lower, upper] timestamp range for a spike whose current timestamp
/// is `cur`, given its file-order neighbours.
struct Range { int64_t lower; int64_t upper; };

inline Range allowedRange(int64_t cur,
                          bool hasPrev, int64_t prev, bool prevMoves,
                          bool hasNext, int64_t next, bool nextMoves)
{
    Range r{std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()};
    if (hasPrev)
        r.lower = (prev < cur) ? (prevMoves ? splitPoint(prev, cur) + 1 : prev + 1) : cur;
    if (hasNext)
        r.upper = (cur < next) ? (nextMoves ? splitPoint(cur, next) : next - 1) : cur;
    return r;
}

/// Clamp the proposed timestamps of a set of moving spikes so file order is
/// preserved.
///   rows     — 0-based file rows of the moving spikes (any order, no duplicates)
///   curTs    — their current timestamps (parallel to rows)
///   newTs    — their proposed timestamps; clamped in place
///   nTotal   — number of spikes in the file
///   tsAt(p)  — current timestamp of file row p (any row, moving or not)
/// Returns the number of spikes whose proposed timestamp was changed.
template <class TsAt>
int64_t clampToNeighbours(const std::vector<int64_t>& rows,
                          const std::vector<int64_t>& curTs,
                          std::vector<int64_t>& newTs,
                          int64_t nTotal, TsAt tsAt)
{
    std::vector<int64_t> moving(rows);
    std::sort(moving.begin(), moving.end());
    auto moves = [&](int64_t p) { return std::binary_search(moving.begin(), moving.end(), p); };

    int64_t nClamped = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const int64_t p   = rows[i];
        const int64_t cur = curTs[i];
        const bool hasPrev = p > 0;
        const bool hasNext = p + 1 < nTotal;
        const Range r = allowedRange(cur,
                                     hasPrev, hasPrev ? tsAt(p - 1) : 0, hasPrev && moves(p - 1),
                                     hasNext, hasNext ? tsAt(p + 1) : 0, hasNext && moves(p + 1));
        const int64_t clamped = std::clamp(newTs[i], r.lower, r.upper);
        if (clamped != newTs[i]) { newTs[i] = clamped; ++nClamped; }
    }
    return nClamped;
}

} // namespace spikeorder

#endif // KLUSTERS_SPIKEORDER_H
