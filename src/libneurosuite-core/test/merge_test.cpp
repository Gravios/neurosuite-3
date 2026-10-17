// merge_test.cpp — core/waveformiou.hpp and core/mergerecommend.hpp: the
// envelope overlap and the two-witness merge ranking, on inputs whose answer
// is known.
#include <neurosuite/core/mergerecommend.hpp>
#include <neurosuite/core/waveformiou.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace nm = neurosuite::merge;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++fails; }
}

static std::function<double(int)> at(const std::vector<double>& v) {
    return [&v](int i) { return v[static_cast<size_t>(i)]; };
}

int main() {
    // ── wfEnvelopeIouBestShift ─────────────────────────────────────────────
    {
        const int ns = 32, nc = 2;
        std::vector<double> m(ns * nc), sd(ns * nc, 5.0), far(ns * nc), shifted(ns * nc), half(ns * nc);
        for (int s = 0; s < ns; ++s)
            for (int c = 0; c < nc; ++c) {
                const double v = 100.0 * std::exp(-0.5 * (s - 16) * (s - 16) / 9.0);
                m[s * nc + c] = v;
                far[s * nc + c] = v + 1000.0;
                half[s * nc + c] = v + 5.0;                                   // bands overlap by half
                const int s2 = s - 3;                                          // the same shape 3 samples later
                shifted[s * nc + c] = (s2 >= 0) ? 100.0 * std::exp(-0.5 * (s2 - 16) * (s2 - 16) / 9.0) : 0.0;
            }
        int sh = 99;
        check(std::fabs(nm::wfEnvelopeIouBestShift(at(m), at(sd), at(m), at(sd), ns, nc, 0, 1.0, &sh) - 1.0) < 1e-12 && sh == 0,
              "identical envelopes: IOU 1 at shift 0");
        check(nm::wfEnvelopeIouBestShift(at(m), at(sd), at(far), at(sd), ns, nc, 0) == 0.0, "disjoint envelopes: IOU 0");
        const double ab = nm::wfEnvelopeIouBestShift(at(m), at(sd), at(half), at(sd), ns, nc, 0);
        const double ba = nm::wfEnvelopeIouBestShift(at(half), at(sd), at(m), at(sd), ns, nc, 0);
        check(std::fabs(ab - 1.0 / 3.0) < 1e-12, "bands offset by half their width: IOU 1/3 (" + std::to_string(ab) + ")");
        check(ab == ba, "IOU is symmetric");
        const double strict = nm::wfEnvelopeIouBestShift(at(m), at(sd), at(shifted), at(sd), ns, nc, 0);
        const double lag = nm::wfEnvelopeIouBestShift(at(m), at(sd), at(shifted), at(sd), ns, nc, 4, 1.0, &sh);
        check(lag > 0.99 && sh == 3 && strict < lag, "a 3-sample lag is found within maxShift 4 (shift " + std::to_string(sh) + ")");
        check(nm::wfEnvelopeIouBestShift(at(m), at(sd), at(m), at(sd), ns, nc, 16) == 0.0,
              "maxShift leaving nothing to score: 0");
    }

    // ── mrPercentileRanks ──────────────────────────────────────────────────
    {
        const auto r = nm::mrPercentileRanks({1.0, 3.0, 2.0}, true);
        check(r.size() == 3 && r[1] == 1.0 && r[2] == 0.5 && r[0] == 0.0, "ranks: best 1, worst 0");
        const auto t = nm::mrPercentileRanks({2.0, 2.0, 1.0}, true);
        check(t[0] == t[1] && t[0] > t[2], "ties share their rank");
        const auto f = nm::mrPercentileRanks({4.0, 4.0, 4.0}, true);
        check(f[0] == 1.0 && f[1] == 1.0 && f[2] == 1.0, "no spread: every rank 1 (the metric abstains)");
    }

    // ── mrRecommendMerges ──────────────────────────────────────────────────
    {
        // Clusters 10, 11, 12, 13.  Error (symmetrised) and overlap agree that
        // 10-11 is the best pair and 12-13 second; 10-12 has a high error
        // score but no overlap witness (no waveform) and must be dropped.
        const std::vector<int> ids = {10, 11, 12, 13};
        const double E[4][4] = {{0, .9, .8, .1}, {.9, 0, .1, .1}, {.8, .1, 0, .6}, {.1, .1, .6, 0}};
        auto errAt = [&](int a, int b) { return E[a][b]; };
        auto ov = [](int a, int b, double& o) {
            const int lo = std::min(a, b), hi = std::max(a, b);
            if (lo == 10 && hi == 12) return false;
            o = (lo == 10 && hi == 11) ? 0.9 : (lo == 12 && hi == 13) ? 0.7 : 0.1;
            return true;
        };
        const auto all = nm::mrRecommendMerges(ids, errAt, ov, 10, 0.05, 0.0);
        check(!all.empty() && all[0].a == 10 && all[0].b == 11 && all[0].quality == 1.0, "the pair both witnesses favour ranks first");
        check(all.size() >= 2 && all[1].a == 12 && all[1].b == 13, "the second-best pair ranks second");
        bool has1012 = false;
        for (const auto& c : all) has1012 = has1012 || (c.a == 10 && c.b == 12);
        check(!has1012, "a pair without an overlap witness is dropped");

        const auto gated = nm::mrRecommendMerges(ids, errAt, ov, 10, 0.5, 0.0);
        check(gated.size() == 2, "the absolute error gate keeps only pairs above it (" + std::to_string(gated.size()) + ")");
        check(nm::mrRecommendMerges(ids, errAt, ov, 1, 0.05, 0.0).size() == 1, "maxCount caps the list");
        const auto sel = nm::mrRecommendMerges(ids, errAt, ov, 10, 0.05, 0.0, {13});
        bool only13 = !sel.empty();
        for (const auto& c : sel) only13 = only13 && (c.a == 13 || c.b == 13);
        check(only13 && sel[0].a == 12, "restrictTo keeps pairs with a selected member, ranked over all pairs");
        const auto floor = nm::mrRecommendMerges(ids, errAt, ov, 10, 0.05, 0.99);
        check(floor.size() == 1, "qualityFloor keeps only pairs at or above it");

        // When one witness cannot tell the pairs apart it abstains, and the
        // other decides: equal overlaps leave the error ranking in charge.
        auto flat = [](int a, int b, double& o) { (void)a; (void)b; o = 0.5; return true; };
        const auto byError = nm::mrRecommendMerges(ids, errAt, flat, 10, 0.05, 0.9);
        check(byError.size() == 1 && byError[0].a == 10 && byError[0].b == 11,
              "equal overlaps: the error witness alone passes the best pair at floor 0.9 ("
                  + std::to_string(byError.size()) + ")");
    }

    if (fails) { std::printf("%d check(s) failed\n", fails); return 1; }
    std::printf("merge: all checks passed\n");
    return 0;
}
