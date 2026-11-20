/* corecut_test.cpp -- standalone test of corecut::analyse.
 * No framework: exits 0 on success, non-zero on a failure.
 *
 *   g++ -std=c++20 -O2 -fopenmp -I src/klusters/src src/klusters/test/corecut_test.cpp -o cc && ./cc
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "corecut.h"

#include <cstdio>
#include <random>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

namespace {
constexpr int    kCh = 4, kSamp = 40, kPeak = 20;
constexpr double kRate = 20000.0;

/** A spike: a trough of the given depth and width (samples) at kPeak + offset, then a
 *  shallow overshoot; per-channel gains; white noise. */
void addSpike(std::vector<float>& wav, std::mt19937& rng, const double gain[kCh], double width, double offset, double noise)
{
    std::normal_distribution<double> g(0.0, noise);
    for (int c = 0; c < kCh; ++c)
        for (int t = 0; t < kSamp; ++t) {
            const double x = (t - kPeak - offset) / width;
            const double y = -std::exp(-0.5 * x * x) + 0.35 * std::exp(-0.5 * (x - 3.0) * (x - 3.0) / 2.5);
            wav.push_back(static_cast<float>(gain[c] * y + g(rng)));
        }
}

struct Set { std::vector<float> wav; std::vector<char> foreign; long n = 0; };

/** nCore spikes of one unit and nOther of a neighbour: another footprint over the channels and a wider trough. */
Set mixture(long nCore, long nOther, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> jitter(-1.5, 1.5), amp(0.85, 1.15);
    std::vector<char> who(static_cast<std::size_t>(nCore), 0);
    who.insert(who.end(), static_cast<std::size_t>(nOther), 1);
    std::shuffle(who.begin(), who.end(), rng);
    Set s;
    s.foreign = who;
    s.n = nCore + nOther;
    for (const char f : who) {
        const double a = amp(rng);
        const double core[kCh]  = {1000.0 * a, 300.0 * a, 120.0 * a, 60.0 * a};
        const double other[kCh] = {700.0 * a, 250.0 * a, 520.0 * a, 300.0 * a};
        addSpike(s.wav, rng, f ? other : core, f ? 2.6 : 1.8, jitter(rng), 40.0);
    }
    return s;
}

struct Score { double purity, coreKept, foreignRemoved; long kept; };
Score score(const Set& s, const corecut::Result& r)
{
    long kept = 0, keptCore = 0, core = 0, foreign = 0, foreignOut = 0;
    for (long i = 0; i < s.n; ++i) {
        const bool f = s.foreign[static_cast<std::size_t>(i)], k = r.keep[static_cast<std::size_t>(i)];
        core += !f; foreign += f; kept += k; keptCore += (k && !f); foreignOut += (f && !k);
    }
    return {kept ? static_cast<double>(keptCore) / kept : 0.0, core ? static_cast<double>(keptCore) / core : 0.0,
            foreign ? static_cast<double>(foreignOut) / foreign : 0.0, kept};
}

corecut::Params params() { corecut::Params p; p.sampleRateHz = kRate; return p; }
} // namespace

int main()
{
    // 1. A unit with a quarter of a neighbour's spikes in it: the neighbour goes, the core stays.
    {
        const Set s = mixture(1500, 500, 1);
        const corecut::Result r = corecut::analyse(s.wav.data(), s.n, kCh, kSamp, params());
        const Score q = score(s, r);
        std::printf("mixture: cut %ld of %ld (channel %d), kept %ld, purity %.3f, core kept %.3f, neighbour removed %.3f\n",
                    r.cut, r.ordered, r.cutChannel, q.kept, q.purity, q.coreKept, q.foreignRemoved);
        CHECK(r.ok, "the analysis runs");
        CHECK(r.primaryChannel == 0, "the primary channel is the largest one");
        CHECK(r.ordered == s.n && static_cast<long>(r.order.size()) == s.n, "every spike is ordered");
        CHECK(r.cut < r.ordered && r.cutChannel >= 0, "a cut is found");
        CHECK(q.purity >= 0.97, "what is kept is the core unit");
        CHECK(q.coreKept >= 0.85, "most of the core unit is kept");
        CHECK(q.foreignRemoved >= 0.90, "the neighbour's spikes are past the cut");
        CHECK(!s.foreign[static_cast<std::size_t>(r.order[0])], "the order starts in the core");

        // 2. An ordering hook that declines: the loop here runs, the result is the same.
        bool asked = false;
        const corecut::Result d = corecut::analyse(s.wav.data(), s.n, kCh, kSamp, params(),
            [&asked](long, int, const float*, long, long*, double*) { asked = true; return false; });
        CHECK(asked && !d.hookUsed && d.cut == r.cut && d.keep == r.keep, "a declining hook leaves the result unchanged");

        // 3. A hook that orders: its order is the one that is cut.
        const corecut::Result h = corecut::analyse(s.wav.data(), s.n, kCh, kSamp, params(),
            [](long n, int dim, const float* x, long seed, long* order, double* step) {
                corecut::detail::orderCpu(n, dim, x, seed, order, step);
                return true;
            });
        CHECK(h.hookUsed && h.cut == r.cut && h.keep == r.keep && h.order == r.order, "a hook's order is used");

        // 4. Above maxOrdered a subset is ordered and the others follow the kept subset's mean.
        corecut::Params ps = params();
        ps.maxOrdered = 600;
        const corecut::Result u = corecut::analyse(s.wav.data(), s.n, kCh, kSamp, ps);
        const Score qu = score(s, u);
        std::printf("subset of 600: cut %ld of %ld, kept %ld, purity %.3f, core kept %.3f, neighbour removed %.3f\n",
                    u.cut, u.ordered, qu.kept, qu.purity, qu.coreKept, qu.foreignRemoved);
        CHECK(u.ok && u.ordered == 600, "600 spikes are ordered");
        CHECK(static_cast<long>(u.keep.size()) == s.n, "every spike is classified");
        CHECK(qu.purity >= 0.95 && qu.coreKept >= 0.70 && qu.foreignRemoved >= 0.85, "the subset's cut carries to the others");
    }
    // 5. One unit alone: most of it stays (the cut errs on the side of splitting, but not by half).
    {
        const Set s = mixture(2000, 0, 2);
        const corecut::Result r = corecut::analyse(s.wav.data(), s.n, kCh, kSamp, params());
        const Score q = score(s, r);
        std::printf("one unit: cut %ld of %ld, kept %.3f (%s)\n", r.cut, r.ordered, static_cast<double>(q.kept) / s.n, r.reason);
        CHECK(r.ok, "the analysis runs on a clean unit");
        CHECK(q.kept >= static_cast<long>(0.75 * s.n), "a clean unit keeps at least three quarters of its spikes");
    }
    // 6. Too few spikes, and a waveform with no trough: nothing is cut, and it says why.
    {
        const Set s = mixture(120, 40, 3);
        const corecut::Result r = corecut::analyse(s.wav.data(), s.n, kCh, kSamp, params());
        CHECK(!r.ok && r.reason[0] != 0, "too few spikes: refused with a reason");
        CHECK(static_cast<long>(r.keep.size()) == s.n && std::count(r.keep.begin(), r.keep.end(), 1) == s.n, "and every spike is kept");

        const long nFlat = 400;
        const std::vector<float> flat(static_cast<std::size_t>(nFlat) * kCh * kSamp, 100.0f);
        const corecut::Result f = corecut::analyse(flat.data(), nFlat, kCh, kSamp, params());
        CHECK(!f.ok && f.reason[0] != 0, "no trough: refused with a reason");
    }
    std::printf("corecut_test: %s (%d failed)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
