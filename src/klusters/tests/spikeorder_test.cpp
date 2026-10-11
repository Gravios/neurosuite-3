// spikeorder_test.cpp — spikeorder::clampToNeighbours keeps .res in file order.
//
// Fixed cases (fixed vs moving neighbours, duplicate timestamps, file edges)
// plus a randomized property check: whatever subset of spikes moves and by how
// much, the clamped timestamps written back must leave .res strictly increasing
// (non-decreasing where it started with duplicates), so no row ever needs
// reordering.  Self-contained, run via ctest.

#include "spikeorder.h"

#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

// Apply a clamped move to a copy of res and return it.
static std::vector<int64_t> applyMove(std::vector<int64_t> res,
                                      const std::vector<int64_t>& rows,
                                      std::vector<int64_t>& newTs,
                                      int64_t* nClamped = nullptr)
{
    std::vector<int64_t> cur(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) cur[i] = res[static_cast<size_t>(rows[i])];
    const int64_t n = spikeorder::clampToNeighbours(
        rows, cur, newTs, static_cast<int64_t>(res.size()),
        [&](int64_t p) { return res[static_cast<size_t>(p)]; });
    if (nClamped) *nClamped = n;
    for (size_t i = 0; i < rows.size(); ++i) res[static_cast<size_t>(rows[i])] = newTs[i];
    return res;
}

int main()
{
    using spikeorder::splitPoint;
    using spikeorder::allowedRange;

    // splitPoint: the earlier spike may reach it, the later one splitPoint + 1.
    check(splitPoint(100, 101) == 100, "splitPoint adjacent");
    check(splitPoint(100, 110) == 104, "splitPoint gap 10");
    check(splitPoint(100, 111) == 105, "splitPoint gap 11");

    // A fixed neighbour is a hard wall one sample away.
    {
        auto r = allowedRange(100, true, 90, false, true, 120, false);
        check(r.lower == 91 && r.upper == 119, "fixed neighbours: (prev+1, next-1)");
    }
    // A moving neighbour gives half the gap.
    {
        auto r = allowedRange(100, true, 90, true, true, 120, true);
        check(r.lower == splitPoint(90, 100) + 1 && r.upper == splitPoint(100, 120),
              "moving neighbours: split the gaps");
    }
    // Duplicate timestamps pin the spike on that side.
    {
        auto r = allowedRange(100, true, 100, false, true, 100, true);
        check(r.lower == 100 && r.upper == 100, "duplicates pin");
    }
    // File edges are open.
    {
        auto r = allowedRange(5, false, 0, false, true, 50, false);
        check(r.lower == INT64_MIN && r.upper == 49, "first spike: open below");
    }

    // Nudge-like: a cluster shifts +8, one spike sits 3 samples before another
    // cluster's spike and is clamped; the rest move fully.
    {
        std::vector<int64_t> res = {100, 200, 203, 300, 400};
        std::vector<int64_t> rows = {1, 3};           // the moving cluster
        std::vector<int64_t> newTs = {208, 308};
        int64_t nc = 0;
        auto out = applyMove(res, rows, newTs, &nc);
        check(nc == 1, "nudge: one spike clamped");
        check(out[1] == 202 && out[3] == 308, "nudge: clamped one stops at next-1, other moves fully");
    }

    // Two adjacent moving spikes shifting toward each other never cross.
    {
        std::vector<int64_t> res = {0, 100, 102, 1000};
        std::vector<int64_t> rows = {1, 2};
        std::vector<int64_t> newTs = {110, 90};       // would swap
        auto out = applyMove(res, rows, newTs);
        check(out[1] < out[2], "adjacent movers stay ordered");
    }

    // Property: random sessions, random moving subsets, random shifts.
    std::mt19937_64 rng(12345);
    int violations = 0;
    for (int trial = 0; trial < 2000; ++trial) {
        const int n = 2 + static_cast<int>(rng() % 60);
        std::vector<int64_t> res(static_cast<size_t>(n));
        int64_t t = static_cast<int64_t>(rng() % 50);
        for (int i = 0; i < n; ++i) {
            t += static_cast<int64_t>(rng() % 12);   // gaps 0..11, duplicates included
            res[static_cast<size_t>(i)] = t;
        }
        std::vector<int64_t> rows, newTs;
        for (int i = 0; i < n; ++i)
            if (rng() % 3 == 0) {
                rows.push_back(i);
                newTs.push_back(res[static_cast<size_t>(i)]
                                + static_cast<int64_t>(rng() % 41) - 20);
            }
        const auto out = applyMove(res, rows, newTs);
        for (int i = 1; i < n; ++i) {
            const bool wasStrict = res[static_cast<size_t>(i - 1)] < res[static_cast<size_t>(i)];
            const bool ok = wasStrict ? out[static_cast<size_t>(i - 1)] < out[static_cast<size_t>(i)]
                                      : out[static_cast<size_t>(i - 1)] <= out[static_cast<size_t>(i)];
            if (!ok) ++violations;
        }
    }
    check(violations == 0, "property: clamped moves never reorder .res");

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail == 0 ? 0 : 1;
}
