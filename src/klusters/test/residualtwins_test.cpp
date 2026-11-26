/* residualtwins_test.cpp -- standalone test of residualtwins::classify.
 * No framework: exits 0 on success, non-zero on a failure.
 *
 *   g++ -std=c++20 -O2 -I src/klusters/src/document src/klusters/test/residualtwins_test.cpp -o rt && ./rt
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "residualtwins.h"

#include <cstdio>
#include <map>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

int main()
{
    using namespace residualtwins;
    using residualtwins::Rows;
    // Cluster 7 has rows at 1000, 2000, 2005 and 9000; cluster 8 at 5000.
    std::map<int, Rows> clusters = {{7, {{1000, 10}, {2000, 20}, {2005, 21}, {9000, 90}}}, {8, {{5000, 50}}}};
    const Rows none;
    auto rowsOf = [&](int c) -> const Rows& { const auto it = clusters.find(c); return it == clusters.end() ? none : it->second; };
    // Every row of the group: those of 7 and 8, and one of cluster 3 at 7000.
    const Rows all = {{1000, 10}, {2000, 20}, {2005, 21}, {5000, 50}, {7000, 70}, {9000, 90}};

    {   // detected: the partner's cluster has the spike, within the tolerance; the nearest row is the twin
        const std::vector<Spike> s = {{1004, 100, 7}, {2003, 101, 7}, {1007, 102, 7}, {1006, 103, 7}};
        const std::vector<Verdict> v = classify(s, rowsOf, all, 6);
        CHECK(v[0].kind == Detected && v[0].twin == 10, "a row of the partner 4 samples away is the twin");
        CHECK(v[1].kind == Detected && v[1].twin == 21, "of two rows within the tolerance the nearer is the twin");
        CHECK(v[2].kind == Recovered && v[2].twin == -1, "7 samples away with a tolerance of 6: not the same spike");
        CHECK(v[3].kind == Detected && v[3].twin == 10, "exactly the tolerance away: the same spike");
    }
    {   // a row is never its own twin
        const std::vector<Spike> s = {{1000, 10, 7}};
        const std::vector<Verdict> v = classify(s, rowsOf, all, 6);
        CHECK(v[0].kind == Recovered, "the row itself, found in the partner's cluster, is not a twin");
    }
    {   // repeated: one partner spike in two rows' windows; the lowest row keeps it
        const std::vector<Spike> s = {{4000, 205, 7}, {4003, 200, 7}, {4040, 210, 7}};
        const std::vector<Verdict> v = classify(s, rowsOf, all, 6);
        CHECK(v[1].kind == Recovered && v[1].twin == -1, "the lowest row of a run keeps the spike");
        CHECK(v[0].kind == Repeated && v[0].twin == 200, "the other names it, whatever the order given");
        CHECK(v[2].kind == Recovered, "40 samples on: another spike");
    }
    {   // a run is chained from its first time
        const std::vector<Spike> s = {{4000, 300, 7}, {4005, 301, 7}, {4010, 302, 7}};
        const std::vector<Verdict> v = classify(s, rowsOf, all, 6);
        CHECK(v[0].kind == Recovered && v[1].kind == Repeated && v[1].twin == 300, "5 samples after the first: the same spike");
        CHECK(v[2].kind == Recovered, "10 samples after the first: not in its run, though 5 after the second");
    }
    {   // different partners are different spikes; a detected one is in no run
        const std::vector<Spike> s = {{5001, 400, 8}, {6002, 401, 7}, {5003, 402, 8}, {6003, 403, 8}};
        const std::vector<Verdict> v = classify(s, rowsOf, all, 6);
        CHECK(v[0].kind == Detected && v[0].twin == 50 && v[2].kind == Detected && v[2].twin == 50,
              "both residuals of a spike the partner's cluster holds are detected, not repeated");
        CHECK(v[1].kind == Recovered && v[3].kind == Recovered, "the same time with another partner is another spike");
    }
    {   // an unknown partner: nothing to look in, nothing to compare
        const std::vector<Spike> s = {{3000, 500, -1}, {3001, 501, -1}, {3000, 502, 1}, {3500, 503, 99}, {3502, 504, 99}};
        const std::vector<Verdict> v = classify(s, rowsOf, all, 6);
        CHECK(v[0].kind == Recovered && v[1].kind == Recovered && v[2].kind == Recovered,
              "residuals without a partner cluster, where no row is, are recovered: never twins of each other");
        CHECK(v[3].kind == Recovered && v[4].kind == Repeated && v[4].twin == 503,
              "a partner cluster without rows of its own still groups its residuals");
    }
    {   // the spike is a row, but of another cluster than the partner named
        const std::vector<Spike> s = {{7002, 600, 7}, {5001, 601, 7}, {7001, 602, -1}};
        const std::vector<Verdict> v = classify(s, rowsOf, all, 6);
        CHECK(v[0].kind == Detected && v[0].twin == 70, "a row of a third cluster at the residual's time: detected, that row the twin");
        CHECK(v[1].kind == Detected && v[1].twin == 50, "... also when the partner named has rows, but none there");
        CHECK(v[2].kind == Detected && v[2].twin == 70, "an unknown partner is detected by any row at its time");
        const std::vector<Spike> p = {{2003, 603, 7}};
        Rows crowded = all;
        crowded.push_back({2003, 77});                     // a row of another cluster exactly there
        std::sort(crowded.begin(), crowded.end());
        CHECK(classify(p, rowsOf, crowded, 6)[0].twin == 21, "the partner's own row is preferred to a nearer row of another cluster");
    }
    {   // nothing in, nothing out
        CHECK(classify({}, rowsOf, all, 6).empty(), "no spikes, no verdicts");
    }
    if (g_fail == 0) std::printf("OK\n");
    return g_fail == 0 ? 0 : 1;
}
