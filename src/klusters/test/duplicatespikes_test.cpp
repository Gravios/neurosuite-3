/* duplicatespikes_test.cpp -- standalone test of duplicatespikes::runs / acrossLabels.
 * No framework: exits 0 on success, non-zero on a failure.
 *
 *   g++ -std=c++20 -O2 -I src/klusters/src/document src/klusters/test/duplicatespikes_test.cpp -o ds && ./ds
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "duplicatespikes.h"

#include <cstdio>

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
    using namespace duplicatespikes;
    using Run = std::pair<std::size_t, std::size_t>;
    using Runs = std::vector<Run>;
    {
        const std::vector<long long> t = {100, 100, 500, 503, 506, 900, 2000, 2006, 2007, 3000};
        const Runs r = runs<long long>(t, 6);
        CHECK(r.size() == 3, "three runs");
        CHECK(r.size() == 3 && (r[0] == Run(0, 2)), "two rows at one time are a run");
        CHECK(r.size() == 3 && (r[1] == Run(2, 5)), "500, 503, 506: all within 6 of the first");
        CHECK(r.size() == 3 && (r[2] == Run(6, 8)),
              "2000, 2006 are a run; 2007 is 7 from the first and stands alone");
    }
    {
        const std::vector<long long> t = {10, 15, 20, 25};
        const Runs r = runs<long long>(t, 6);
        CHECK(r.size() == 2 && r[0].second - r[0].first == 2 && r[1].second - r[1].first == 2,
              "chained from the first: four rows 5 apart are two runs of two, not one of four");
    }
    {
        CHECK(runs<long long>({}, 6).empty() && runs<long long>({5}, 6).empty(), "no rows, or one: no run");
        CHECK(runs<long long>({1, 8, 15}, 6).empty(), "rows 7 apart with a tolerance of 6: no run");
        CHECK(runs<long long>({1, 1}, 0).size() == 1 && runs<long long>({1, 2}, 0).empty(), "tolerance 0: only the same time");
    }
    {
        const std::vector<long long> t = {100, 102, 300, 301, 302, 900};
        const std::vector<int> c = {7, 8, 7, 7, 9, 9};
        const Runs a = acrossLabels<long long, int>(t, c, 6);
        CHECK(a.size() == 2 && a[0].first == 0 && a[1].first == 3,
              "neighbours within the tolerance in different clusters: (100, 102) and (301, 302); the same cluster is not counted");
        const std::vector<long long> noTimes;
        const std::vector<int> noLabels;
        CHECK(acrossLabels(noTimes, noLabels, 6LL).empty(), "nothing in, nothing out");
    }
    if (g_fail == 0) std::printf("OK\n");
    return g_fail == 0 ? 0 : 1;
}
