// noise_stats_test.cpp — the .nst noise statistics (noise_stats.hpp).
//
//   1. drawNoiseTimes keeps every window clear of every spike (plus the guard),
//      inside the recording, sorted, unique, and reproducible from the seed.
//   2. On a synthetic AR(1) .fil with known variance, lag-1 and cross-channel
//      correlation, the estimate recovers them.
//   3. Outlier rejection: undetected transients and a zero-filled gap are
//      rejected, and the variance estimate is closer to the truth with the
//      gates than without them.
//   4. The windows are the .spk's bytes: with the gates off, the moments equal
//      those computed from extractSpikeWindows at the same times with the
//      default read buffer, for the raw and the stderiv transform.
//   5. .nst write/load round trip; a wrong magic or a newer version is refused.
// Self-contained (own main), header-only, run via ctest.

#include "neurosuite/core/noise_stats.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace neurosuite::core;

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

namespace {

constexpr int kTot = 4, kNS = 20, kPeak = 8;
constexpr std::int64_t kT = 400000;
constexpr double kInnov = 86.6;                       // AR(1) phi 0.5 -> variance 86.6^2 / 0.75 = 10000

struct Synth {
    std::vector<std::int16_t> fil;
    std::vector<std::int64_t> spikes;
};

// ch0 and ch2 share 60% of their innovation (correlation 0.6); every channel AR(1), phi 0.5.
Synth makeFil(bool transients, bool gap) {
    Synth s;
    s.fil.assign(static_cast<std::size_t>(kT) * kTot, 0);
    std::mt19937_64 r(7); std::normal_distribution<double> g(0, 1);
    double a[kTot] = {0, 0, 0, 0};
    for (std::int64_t t = 0; t < kT; ++t) {
        const double e0 = g(r);
        for (int c = 0; c < kTot; ++c) {
            const double e = c == 0 ? e0 : (c == 2 ? 0.6 * e0 + 0.8 * g(r) : g(r));
            a[c] = 0.5 * a[c] + e * kInnov;
            s.fil[static_cast<std::size_t>(t) * kTot + c] = static_cast<std::int16_t>(std::lround(a[c]));
        }
    }
    for (std::int64_t t = 500; t < kT - 500; t += 997) {           // detected spikes
        s.spikes.push_back(t);
        for (int k = -3; k <= 3; ++k)
            for (int c = 0; c < kTot; ++c)
                s.fil[static_cast<std::size_t>(t + k) * kTot + c] =
                    static_cast<std::int16_t>(s.fil[static_cast<std::size_t>(t + k) * kTot + c]
                                              - 3000 * std::exp(-k * k / 2.0));
    }
    if (transients)                                                // undetected: not in spikes
        for (int i = 0; i < 300; ++i) {
            const std::int64_t t = 750 + static_cast<std::int64_t>(i) * 1301;
            s.fil[static_cast<std::size_t>(t) * kTot + 0] = 4000;
        }
    if (gap)
        for (std::int64_t t = 200000; t < 203000; ++t)
            for (int c = 0; c < kTot; ++c) s.fil[static_cast<std::size_t>(t) * kTot + c] = 0;
    return s;
}

std::FILE* writeTmp(const Synth& s, const char* path) {
    std::FILE* f = std::fopen(path, "wb");
    std::fwrite(s.fil.data(), 2, s.fil.size(), f);
    std::fclose(f);
    return std::fopen(path, "rb");
}

const int kGrp[3] = {0, 2, 3};

SpikeWindowSpec spec(bool stderiv) {
    SpikeWindowSpec sp;
    sp.nSamp = kNS; sp.peakSample = kPeak; sp.totalNbChannels = kTot;
    sp.groupChannels = kGrp; sp.nChan = 3;
    if (stderiv) { sp.applyTransform = true; sp.order = SdiffOrder::AllPairs; }
    return sp;
}

double covAt(const NoiseStats& s, int t1, int c1, int t2, int c2) {
    const int D = s.dim();
    return s.cov[static_cast<std::size_t>(t1 * s.nChan + c1) * D + (t2 * s.nChan + c2)];
}

}  // namespace

int main()
{
    // ── 1. drawNoiseTimes ────────────────────────────────────────────────────
    {
        std::vector<std::int64_t> sp = {1000, 1100, 5000, 9000};
        const int guard = 7;
        auto t = drawNoiseTimes(sp, 10000, kNS, kPeak, guard, 400, 3);
        bool clear = true, inside = true, sorted = true;
        for (std::size_t i = 0; i < t.size(); ++i) {
            for (std::int64_t s : sp) clear = clear && std::llabs(t[i] - s) >= kNS + guard;
            inside = inside && t[i] >= kPeak + 1 && t[i] <= 10000 - (kNS - kPeak);
            if (i) sorted = sorted && t[i] > t[i - 1];
        }
        check(!t.empty(), "drawNoiseTimes returns windows");
        check(clear, "every noise window clears every spike by nSamp + guard");
        check(inside, "every noise window (and its seed sample) lies inside the recording");
        check(sorted, "noise times sorted and unique");
        check(t == drawNoiseTimes(sp, 10000, kNS, kPeak, guard, 400, 3), "same seed -> same times");
        check(t != drawNoiseTimes(sp, 10000, kNS, kPeak, guard, 400, 4), "other seed -> other times");
        check(drawNoiseTimes(sp, 10, kNS, kPeak, guard, 400, 3).empty(), "recording shorter than a window -> none");
        check(t.size() == 400, "exactly the requested number of distinct windows when there is room");
        // a span with room for only 4 distinct times (t = peak+1 .. total-nSamp+peak): each once
        auto few = drawNoiseTimes({}, kNS + 4, kNS, kPeak, 0, 400, 3);
        check(few.size() == 4, "a short span yields each possible time once (" + std::to_string(few.size()) + ")");
    }

    // ── 2. recovery on clean noise ───────────────────────────────────────────
    {
        Synth s = makeFil(false, false);
        std::FILE* f = writeTmp(s, "nst_test_clean.fil");
        NoiseStatsParams p; p.nWindows = 6000;
        NoiseStats st;
        check(estimateNoiseStats(f, spec(false), s.spikes, p, st), "estimate (clean)");
        std::fclose(f);
        const double v = covAt(st, 10, 0, 10, 0);
        check(std::fabs(v - 10000.0) < 600.0, "variance recovered (" + std::to_string(v) + " vs 10000)");
        const double l1 = covAt(st, 10, 0, 11, 0) / v;
        check(std::fabs(l1 - 0.5) < 0.05, "lag-1 correlation recovered (" + std::to_string(l1) + " vs 0.5)");
        const double xc = covAt(st, 10, 0, 10, 1) / std::sqrt(v * covAt(st, 10, 1, 10, 1));
        check(std::fabs(xc - 0.6) < 0.05, "cross-channel correlation recovered (" + std::to_string(xc) + " vs 0.6)");
        check(std::fabs(st.sigma[0] - 100.0) < 8.0, "robust sigma ~ 100 (" + std::to_string(st.sigma[0]) + ")");
        check(static_cast<double>(st.nKept) > 0.97 * static_cast<double>(st.nDrawn), "clean noise: almost nothing rejected");
        check(st.nRejectedFlat == 0, "clean noise: no flat windows");
    }

    // ── 3. outlier rejection ─────────────────────────────────────────────────
    {
        Synth s = makeFil(true, true);
        std::FILE* f = writeTmp(s, "nst_test_dirty.fil");
        NoiseStatsParams p; p.nWindows = 6000;
        NoiseStats gated, open;
        check(estimateNoiseStats(f, spec(false), s.spikes, p, gated), "estimate (gated)");
        NoiseStatsParams q = p; q.zMax = 1e9; q.passes = 0;
        check(estimateNoiseStats(f, spec(false), s.spikes, q, open), "estimate (gates off)");
        std::fclose(f);
        check(gated.nRejectedFlat > 0, "zero-filled gap rejected as flat");
        check(gated.nRejectedOutlier > 50, "transients rejected (" + std::to_string(gated.nRejectedOutlier) + ")");
        double eg = 0, eo = 0;
        for (int t = 0; t < kNS; ++t) {
            eg += std::fabs(covAt(gated, t, 0, t, 0) - 10000.0);
            eo += std::fabs(covAt(open, t, 0, t, 0) - 10000.0);
        }
        check(eg < 0.5 * eo, "gated variance closer to the truth than ungated");
        check(std::fabs(covAt(gated, 10, 0, 10, 0) - 10000.0) < 700.0, "gated variance ~ 10000");
    }

    // ── 4. the windows are the .spk's bytes (raw and stderiv) ────────────────
    for (int mode = 0; mode < 2; ++mode) {
        Synth s = makeFil(false, false);
        std::FILE* f = writeTmp(s, "nst_test_parity.fil");
        NoiseStatsParams p; p.nWindows = 800; p.zMax = 1e9; p.passes = 0; p.seed = 11;
        NoiseStats st;
        check(estimateNoiseStats(f, spec(mode == 1), s.spikes, p, st), "estimate (parity)");
        std::vector<std::int64_t> sp = s.spikes;
        auto times = drawNoiseTimes(sp, kT, kNS, kPeak, kNS, p.nWindows, p.seed);
        const int D = kNS * 3;
        std::vector<double> rows;
        extractSpikeWindows(f, static_cast<std::int64_t>(times.size()), spec(mode == 1),
                            [&](std::int64_t i) { return times[static_cast<std::size_t>(i)]; },
                            [&](std::int64_t, const std::int16_t* r) { rows.insert(rows.end(), r, r + D); });
        std::fclose(f);
        const std::size_t n = times.size();
        std::vector<double> mu(static_cast<std::size_t>(D), 0.0);
        for (std::size_t w = 0; w < n; ++w)
            for (int j = 0; j < D; ++j) mu[static_cast<std::size_t>(j)] += rows[w * D + j];
        for (double& m : mu) m /= static_cast<double>(n);
        double maxd = 0;
        for (int a = 0; a < D; ++a) {
            maxd = std::max(maxd, std::fabs(mu[static_cast<std::size_t>(a)] - st.mean[static_cast<std::size_t>(a)]));
            for (int b = 0; b < D; ++b) {
                double c = 0;
                for (std::size_t w = 0; w < n; ++w)
                    c += (rows[w * D + a] - mu[static_cast<std::size_t>(a)]) * (rows[w * D + b] - mu[static_cast<std::size_t>(b)]);
                c /= static_cast<double>(n - 1);
                maxd = std::max(maxd, std::fabs(c - st.cov[static_cast<std::size_t>(a) * D + b]) / (1.0 + std::fabs(c)));
            }
        }
        check(st.nKept == static_cast<std::int64_t>(n), "gates off keep every window");
        check(maxd < 1e-9, std::string(mode ? "stderiv" : "raw") + " moments equal those of extractSpikeWindows rows");
    }

    // ── 5. file round trip ───────────────────────────────────────────────────
    {
        Synth s = makeFil(false, false);
        std::FILE* f = writeTmp(s, "nst_test_rt.fil");
        NoiseStatsParams p; p.nWindows = 500;
        NoiseStats st, back;
        estimateNoiseStats(f, spec(true), s.spikes, p, st);
        std::fclose(f);
        check(writeNoiseStats("nst_test.nst", st), "write .nst");
        check(loadNoiseStats("nst_test.nst", back), "load .nst");
        check(back.cov == st.cov && back.mean == st.mean && back.sigma == st.sigma
              && back.nKept == st.nKept && back.nDrawn == st.nDrawn && back.zMax == st.zMax
              && back.peakSample == st.peakSample, "round trip is exact");
        std::FILE* g = std::fopen("nst_test.nst", "r+b");
        std::int32_t v = kNsteVersion + 1;
        std::fseek(g, 4, SEEK_SET); std::fwrite(&v, 4, 1, g); std::fclose(g);
        check(!loadNoiseStats("nst_test.nst", back), "newer version refused");
        writeNoiseStats("nst_test.nst", st);
        g = std::fopen("nst_test.nst", "r+b");
        std::int32_t m = 0x12345678;
        std::fwrite(&m, 4, 1, g); std::fclose(g);
        check(!loadNoiseStats("nst_test.nst", back), "wrong magic refused");
        NoiseStats bad;
        check(!writeNoiseStats("nst_test.nst", bad), "invalid stats not written");
    }

    for (const char* p : {"nst_test_clean.fil", "nst_test_dirty.fil", "nst_test_parity.fil",
                          "nst_test_rt.fil", "nst_test.nst"})
        std::remove(p);
    std::printf("noise_stats: %d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
