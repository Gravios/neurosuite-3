/* resorder_test.cpp -- standalone test of resorder::keepInOrder / inversions.
 * No framework: exits 0 on success, non-zero on a failure.
 *
 *   g++ -std=c++20 -O2 -I src/klusters/src/document src/klusters/test/resorder_test.cpp -o ro && ./ro
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "resorder.h"

#include <cstdio>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

using V = std::vector<int64_t>;

int main()
{
    using namespace resorder;
    const V res = {100, 110, 120, 130, 140};
    {   // moves inside the neighbours: kept
        V newTs = {113, 127};
        const auto back = keepInOrder(res, V{1, 2}, V{110, 120}, newTs);
        CHECK(back.empty() && newTs == V({113, 127}), "moves that stay between the neighbours are kept");
    }
    {   // NEGATIVE: one passes the row after it
        V newTs = {125};
        const auto back = keepInOrder(res, V{1}, V{110}, newTs);
        CHECK(back.size() == 1 && newTs[0] == 110, "a spike moved past the next row's spike is put back");
    }
    {   // and one passes the row before it
        V newTs = {105};
        const auto back = keepInOrder(res, V{2}, V{120}, newTs);
        CHECK(back.size() == 1 && newTs[0] == 120, "a spike moved before the previous row's spike is put back");
    }
    {   // landing on the neighbour's time is in order
        V newTs = {120};
        const auto back = keepInOrder(res, V{1}, V{110}, newTs);
        CHECK(back.empty() && newTs[0] == 120, "the same time as a neighbour is in order");
    }
    {   // two neighbours that both move: each against where the other ends
        V newTs = {124, 126};                              // rows 1 and 2: fine together
        auto back = keepInOrder(res, V{1, 2}, V{110, 120}, newTs);
        CHECK(back.empty(), "two neighbours moving the same way stay in order");
        newTs = {126, 115};                                // they would swap
        back = keepInOrder(res, V{1, 2}, V{110, 120}, newTs);
        CHECK(!back.empty() && newTs[0] <= newTs[1] && newTs[1] <= 130 && newTs[0] >= 100,
              "two neighbours that would swap: put back until in order");
    }
    {   // a put-back neighbour can stop a spike that was fine against where it was going
        V newTs = {128, 131};                              // row 2 -> 128 (fine vs 131), row 3 -> 131 passes 140? no: fine
        auto back = keepInOrder(res, V{2, 3}, V{120, 130}, newTs);
        CHECK(back.empty(), "a chain of moves in order is kept");
        newTs = {128, 145};                                // row 3 passes row 4 (140): back to 130; row 2 stays (128 <= 130)
        back = keepInOrder(res, V{2, 3}, V{120, 130}, newTs);
        CHECK(back.size() == 1 && newTs == V({128, 130}), "only the spike that slips is put back");
        newTs = {133, 145};                                // row 3 back to 130: now row 2 (133) passes it, back too
        back = keepInOrder(res, V{2, 3}, V{120, 130}, newTs);
        CHECK(back.size() == 2 && newTs == V({120, 130}), "putting one back can put its neighbour back too");
    }
    {   // a .res out of order before: the old inversion does not hold the spike back
        const V bad = {100, 125, 120, 130};                // row 1 is already after row 2
        V newTs = {127};
        const auto back = keepInOrder(bad, V{1}, V{125}, newTs);
        CHECK(back.empty() && newTs[0] == 127, "an inversion that was there before does not hold a spike back");
        CHECK(inversions(bad) == 1 && inversions(res) == 0, "inversions counts the steps back");
    }
    {   // the first and the last row
        V newTs = {90, 150};
        const auto back = keepInOrder(res, V{0, 4}, V{100, 140}, newTs);
        CHECK(back.empty(), "the first row has nothing before it, the last nothing after it");
    }
    std::printf(g_fail ? "resorder_test: %d failure(s)\n" : "resorder_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
