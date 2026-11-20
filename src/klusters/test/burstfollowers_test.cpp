/* burstfollowers_test.cpp -- standalone test of burstfollowers::analyse.
 * No framework: exits 0 on success, non-zero on a failure.
 *
 *   g++ -std=c++20 -O2 -I src/klusters/src src/klusters/test/burstfollowers_test.cpp -o bf && ./bf
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "burstfollowers.h"

#include <cstdio>
#include <random>
#include <set>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

namespace {
constexpr double kRate = 20000.0;
double ms(double x) { return x * kRate / 1000.0; }

burstfollowers::Params params()
{
    burstfollowers::Params p;
    p.minIsi = ms(2.0);
    p.maxIsi = ms(10.0);
    for (const double s : {40.0, 50.0, 60.0, 70.0}) { p.shifts.push_back(ms(s)); p.shifts.push_back(-ms(s)); }
    return p;
}

struct Train { std::vector<int64_t> times, rows; };

/** A bursting cell: events at Poisson times, each 1..3 spikes 4-7 ms apart.
 *  @p first receives the leaders, @p later the followers. */
void bursting(std::mt19937& rng, double seconds, double eventRate, Train& first, Train& later, int64_t& nextRow)
{
    std::exponential_distribution<double> gap(eventRate / kRate);
    std::uniform_real_distribution<double> isi(ms(4.0), ms(7.0));
    std::uniform_int_distribution<int> extra(0, 2);
    double t = ms(100.0);
    while (t < seconds * kRate) {
        first.times.push_back(static_cast<int64_t>(t));
        first.rows.push_back(nextRow++);
        double u = t;
        const int n = extra(rng);
        for (int k = 0; k < n; ++k) {
            u += isi(rng);
            later.times.push_back(static_cast<int64_t>(u));
            later.rows.push_back(nextRow++);
        }
        t = u + ms(30.0) + gap(rng);
    }
}
}

int main()
{
    const burstfollowers::Params p = params();

    // 1. A hand-made train.  A leads two bursts; B's spikes at 5 ms and 11 ms after
    //    the first leader and 6 ms after the second are followers of A.  B leads one
    //    burst with an A follower.  One burst collides (B 1 ms after A).
    {
        const std::vector<int64_t> tA = {1000, 20000, 60000 + 120, 100000};
        const std::vector<int64_t> rA = {0, 1, 2, 3};
        const std::vector<int64_t> tB = {1000 + 100, 1000 + 220, 20000 + 120, 60000, 100000 + 20, 100000 + 120, 200000};
        const std::vector<int64_t> rB = {10, 11, 12, 13, 14, 15, 16};
        const burstfollowers::Result r = burstfollowers::analyse(tA, rA, tB, rB, p);
        CHECK(r.bursts == 4, "four bursts");
        CHECK(r.collided == 1, "one collided burst");
        CHECK(r.mixed == 3, "three mixed bursts");
        CHECK(r.ofA.rows == (std::vector<int64_t>{10, 11, 12}), "B's followers of A's leaders");
        CHECK(r.ofB.rows == (std::vector<int64_t>{2}), "A's follower of B's leader");
        CHECK(!r.ofA.significant && !r.ofB.significant, "fewer than minCount: not significant");
    }

    // 2. Cell 1 bursts; its followers were clustered with cell 2 (B), which fires on
    //    its own.  Every follower of A's leaders is found; the reverse stays at chance.
    {
        std::mt19937 rng(11);
        Train a, followers, b, bLater;
        int64_t row = 0;
        bursting(rng, 600.0, 2.0, a, followers, row);
        bursting(rng, 600.0, 2.0, b, bLater, row);
        std::set<int64_t> truth(followers.rows.begin(), followers.rows.end());
        Train labelledB = b;                                   // cell 2's own leaders and followers ...
        labelledB.times.insert(labelledB.times.end(), bLater.times.begin(), bLater.times.end());
        labelledB.rows.insert(labelledB.rows.end(), bLater.rows.begin(), bLater.rows.end());
        labelledB.times.insert(labelledB.times.end(), followers.times.begin(), followers.times.end());   // ... and cell 1's followers
        labelledB.rows.insert(labelledB.rows.end(), followers.rows.begin(), followers.rows.end());
        const burstfollowers::Result r = burstfollowers::analyse(a.times, a.rows, labelledB.times, labelledB.rows, p);
        long hit = 0;
        for (const int64_t x : r.ofA.rows) hit += truth.count(x);
        const double recall    = static_cast<double>(hit) / static_cast<double>(truth.size());
        const double precision = static_cast<double>(hit) / static_cast<double>(r.ofA.rows.size());
        std::printf("misplaced followers: %zu true, %ld found, recall %.3f, precision %.3f, chance %.1f, p %.2g; reverse %ld (chance %.1f, p %.2g)\n",
                    truth.size(), r.ofA.observed, recall, precision, r.ofA.expected, r.ofA.pValue,
                    r.ofB.observed, r.ofB.expected, r.ofB.pValue);
        CHECK(r.ofA.significant, "the misplaced followers are significant");
        CHECK(r.ofA.reversedBursts * 5 < r.ofA.bursts, "... and far above the count run backwards");
        CHECK(recall > 0.9, "more than 90% of them are found");
        CHECK(precision > 0.9, "more than 90% of the found are true");
        CHECK(!r.ofB.significant, "the reverse direction is not significant");
        CHECK(std::fabs(static_cast<double>(r.ofB.observed) - r.ofB.expected) < 4.0 * std::sqrt(r.ofB.expected + 1.0),
              "the reverse count is at its chance level");
    }

    // 3. Two cells, correctly clustered (each keeps its own followers): nothing significant.
    {
        std::mt19937 rng(5);
        Train a, aLater, b, bLater;
        int64_t row = 0;
        bursting(rng, 600.0, 2.0, a, aLater, row);
        bursting(rng, 600.0, 2.0, b, bLater, row);
        a.times.insert(a.times.end(), aLater.times.begin(), aLater.times.end());
        a.rows.insert(a.rows.end(), aLater.rows.begin(), aLater.rows.end());
        b.times.insert(b.times.end(), bLater.times.begin(), bLater.times.end());
        b.rows.insert(b.rows.end(), bLater.rows.begin(), bLater.rows.end());
        const burstfollowers::Result r = burstfollowers::analyse(a.times, a.rows, b.times, b.rows, p);
        std::printf("clean pair: of A %ld (chance %.1f, p %.2g), of B %ld (chance %.1f, p %.2g)\n",
                    r.ofA.observed, r.ofA.expected, r.ofA.pValue, r.ofB.observed, r.ofB.expected, r.ofB.pValue);
        CHECK(!r.ofA.significant && !r.ofB.significant, "a clean pair has no significant direction");
    }

    // 4. Two cells that fire TOGETHER (a shared event, each within +-8 ms of it, in no
    //    order): far above the shifted count, and still not one cell.
    {
        std::mt19937 rng(23);
        std::exponential_distribution<double> gap(3.0 / kRate);
        std::uniform_real_distribution<double> jitter(-ms(8.0), ms(8.0));
        Train a, b;
        int64_t row = 0;
        for (double t = ms(100.0); t < 1800.0 * kRate; t += ms(40.0) + gap(rng)) {
            a.times.push_back(static_cast<int64_t>(t + jitter(rng))); a.rows.push_back(row++);
            b.times.push_back(static_cast<int64_t>(t + jitter(rng))); b.rows.push_back(row++);
        }
        const burstfollowers::Result r = burstfollowers::analyse(a.times, a.rows, b.times, b.rows, p);
        std::printf("synchronous pair: of A %ld bursts (shifted %.1f, reversed %ld), of B %ld bursts (shifted %.1f, reversed %ld)\n",
                    r.ofA.bursts, r.ofA.expectedBursts, r.ofA.reversedBursts, r.ofB.bursts, r.ofB.expectedBursts, r.ofB.reversedBursts);
        CHECK(r.ofA.pValue < p.alpha && r.ofB.pValue < p.alpha, "a synchronous pair is far above the shifted count");
        CHECK(!r.ofA.significant && !r.ofB.significant, "... and is not split: its spikes come in no order");
    }

    // 5. The tails.
    CHECK(std::fabs(burstfollowers::detail::poissonTail(1, 1.0) - (1.0 - std::exp(-1.0))) < 1e-12, "P(K>=1 | 1)");
    CHECK(std::fabs(burstfollowers::detail::poissonTail(3, 2.0) - 0.3233235838) < 1e-9, "P(K>=3 | 2)");
    CHECK(burstfollowers::detail::poissonTail(200, 5.0) < 1e-100 && burstfollowers::detail::poissonTail(200, 5.0) > 0.0, "a tiny tail stays positive");
    CHECK(burstfollowers::detail::poissonTail(0, 5.0) == 1.0, "P(K>=0) = 1");
    CHECK(std::fabs(burstfollowers::detail::evenSplitTail(3, 4) - 5.0 / 16.0) < 1e-12, "P(K>=3 | 4, 1/2)");
    CHECK(std::fabs(burstfollowers::detail::evenSplitTail(10, 10) - 1.0 / 1024.0) < 1e-12, "P(K>=10 | 10, 1/2)");
    CHECK(burstfollowers::detail::evenSplitTail(0, 7) == 1.0, "P(K>=0) = 1 (even split)");

    std::printf(g_fail ? "burstfollowers_test: %d FAILED\n" : "burstfollowers_test: all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
