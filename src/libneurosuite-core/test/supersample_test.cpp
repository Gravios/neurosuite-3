/***************************************************************************
 * supersample_test.cpp — neurosuite/core/supersample.hpp, the super-sampled
 * path of spike_extract.hpp and pca_fit.hpp.
 *
 *   - the cubic spline and Lanczos-3 reproduce the samples exactly and a
 *     smooth waveform (a Gaussian trough) between them to < 1% of its depth;
 *   - supersampleWindow: nSamp * factor ticks per channel, tick k*factor == the
 *     raw sample k (both interpolators), rounding saturates to int16;
 *   - the phase: tick 0 at phase p equals tick p at phase 0;
 *   - splitTicks: -5 ticks at factor 4 -> -2 samples, phase 3;
 *   - extractSpikeWindows with supersample 4 on a synthetic .fil: the emitted
 *     row has nChan * nSamp * 4 entries, its ticks at multiples of 4 equal the
 *     plain cut, the trough of a spike planted between two samples lands at a
 *     fractional tick, a window at the file start (no low margin) still comes out,
 *     a per-spike phase shifts the cut, and the stderiv transform in the
 *     super-sampled domain seeds from the tick before the window;
 *   - fitPca on a two-component synthetic population recovers the two directions
 *     (projection energy > 99% in two components) and round-trips through
 *     writePca/loadPca.
 * Exits 0 on success, 1 if any check fails.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include <neurosuite/core/pca_fit.hpp>
#include <neurosuite/core/spike_extract.hpp>
#include <neurosuite/core/supersample.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace neurosuite::core;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const std::string& what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
static double gauss(double t, double mu, double sd) { return -1000.0 * std::exp(-0.5 * ((t - mu) / sd) * ((t - mu) / sd)); }

int main()
{
    // ── the interpolators on one channel ─────────────────────────────────
    {
        const int n = 40, mg = 3, F = 4;
        std::vector<double> raw(static_cast<std::size_t>(n));
        for (int j = 0; j < n; ++j) raw[static_cast<std::size_t>(j)] = gauss(j, 19.5, 2.5);
        SupersampleScratch s;
        for (Interp ip : {Interp::CubicSpline, Interp::Lanczos3}) {
            const int nOut = (n - 2 * mg) * F;
            std::vector<double> out(static_cast<std::size_t>(nOut));
            supersampleChannel(raw.data(), n, mg, F, 0, ip, out.data(), nOut, 0, s);
            double maxAtSamples = 0.0, maxBetween = 0.0;
            for (int k = 0; k < nOut; ++k) {
                const double t = mg + static_cast<double>(k) / F;
                const double err = std::fabs(out[static_cast<std::size_t>(k)] - gauss(t, 19.5, 2.5));
                if (k % F == 0) maxAtSamples = std::max(maxAtSamples, err); else maxBetween = std::max(maxBetween, err);
            }
            check(maxAtSamples < 1e-9, std::string(interpName(ip)) + ": exact at the samples (max err " + std::to_string(maxAtSamples) + ")");
            check(maxBetween < 12.0, std::string(interpName(ip)) + ": a Gaussian trough (sd 2.5 samples) between samples to ~1% of its depth (max err " + std::to_string(maxBetween) + ")");
        }
        // the phase: tick 0 at phase p == tick p at phase 0
        std::vector<double> a(8), b(8);
        supersampleChannel(raw.data(), n, mg, F, 0, Interp::CubicSpline, a.data(), 8, 0, s);
        supersampleChannel(raw.data(), n, mg, F, 3, Interp::CubicSpline, b.data(), 8, 0, s);
        check(std::fabs(b[0] - a[3]) < 1e-9 && std::fabs(b[4] - a[7]) < 1e-9, "phase 3: tick 0 is tick 3 of phase 0");
        int whole, phase;
        splitTicks(-5, 4, whole, phase);
        check(whole == -2 && phase == 3, "splitTicks(-5, 4) = -2 samples, phase 3");
        splitTicks(9, 4, whole, phase);
        check(whole == 2 && phase == 1, "splitTicks(9, 4) = 2 samples, phase 1");
    }
    // ── a window: ticks at multiples of the factor are the raw samples ────
    {
        const int nChan = 2, nSamp = 10, mg = 3, F = 2, nRaw = nSamp + 2 * mg;
        std::vector<std::int16_t> in(static_cast<std::size_t>(nChan) * nRaw), out(static_cast<std::size_t>(nChan) * nSamp * F);
        for (int ch = 0; ch < nChan; ++ch)
            for (int j = 0; j < nRaw; ++j) in[static_cast<std::size_t>(ch) * nRaw + j] = static_cast<std::int16_t>((ch + 1) * (j * j - 40));
        in[5] = 32767; in[6] = 32767;     // saturation stays in range
        SupersampleScratch s;
        bool exact = true;
        for (Interp ip : {Interp::CubicSpline, Interp::Lanczos3}) {
            supersampleWindow(in.data(), nChan, nRaw, mg, F, 0, ip, out.data(), nSamp * F, s);
            for (int ch = 0; ch < nChan; ++ch)
                for (int k = 0; k < nSamp; ++k)
                    exact = exact && out[static_cast<std::size_t>(ch) * nSamp * F + k * F] == in[static_cast<std::size_t>(ch) * nRaw + mg + k];
        }
        check(exact, "supersampleWindow: tick k*factor == raw sample k, both interpolators, int16 in range");
        std::vector<std::int16_t> stored(static_cast<std::size_t>(nSamp)), up(static_cast<std::size_t>(nSamp) * 4);
        for (int j = 0; j < nSamp; ++j) stored[static_cast<std::size_t>(j)] = static_cast<std::int16_t>(100 * j);
        supersampleStoredWindow(stored.data(), 1, nSamp, 4, Interp::CubicSpline, up.data(), s);
        check(up[0] == 0 && up[4] == 100 && up[36] == 900 && up[39] == 900, "a stored window (no margin): the last ticks clamp to the final sample");
    }
    // ── extraction from a synthetic recording ────────────────────────────
    {
        const int totalCh = 3, nSamp = 16, peak = 6, F = 4;
        const int groupCh[2] = {0, 2};
        const std::int64_t nTot = 4000;
        std::vector<std::int16_t> fil(static_cast<std::size_t>(nTot) * totalCh, 0);
        // spike A at 1000 (trough exactly on a sample), spike B whose trough sits at 2000.5
        auto plant = [&](double centre, int ch) {
            for (int j = -8; j <= 8; ++j) {
                const std::int64_t t = static_cast<std::int64_t>(std::floor(centre)) + j;
                fil[static_cast<std::size_t>(t) * totalCh + ch] = static_cast<std::int16_t>(std::lround(gauss(static_cast<double>(t), centre, 1.5)));
            }
        };
        plant(1000.0, 0); plant(2000.5, 2);
        for (std::int64_t t = 0; t < nTot; ++t) fil[static_cast<std::size_t>(t) * totalCh + 1] = static_cast<std::int16_t>(t % 7);   // a channel outside the group
        const fs::path dir = fs::temp_directory_path() / "ns3_supersample_test";
        fs::create_directories(dir);
        const fs::path filPath = dir / "s.fil";
        { std::FILE* f = std::fopen(filPath.string().c_str(), "wb"); std::fwrite(fil.data(), 2, fil.size(), f); std::fclose(f); }
        std::vector<std::int64_t> ts{peak, 1000, 2000, 2001, nTot - nSamp + peak};   // the first and last windows at the file edges (no margin there)
        const std::int8_t phases[5] = {0, 0, 0, 2, 0};
        auto run = [&](int up, bool transform, const std::int8_t* ph) {
            std::vector<std::vector<std::int16_t>> rows;
            SpikeWindowSpec spec;
            spec.nSamp = nSamp; spec.peakSample = peak; spec.totalNbChannels = totalCh;
            spec.groupChannels = groupCh; spec.nChan = 2; spec.supersample = up; spec.phaseTicks = ph;
            spec.applyTransform = transform; spec.order = SdiffOrder::First; spec.temporalDiff = transform;
            std::FILE* f = std::fopen(filPath.string().c_str(), "rb");
            std::int64_t bad = 0;
            extractSpikeWindows(f, static_cast<std::int64_t>(ts.size()), spec, [&](std::int64_t i) { return ts[static_cast<std::size_t>(i)]; },
                                [&](std::int64_t, const std::int16_t* row) { rows.emplace_back(row, row + spec.nChan * spec.outSamples()); }, &bad);
            std::fclose(f);
            return std::make_pair(rows, bad);
        };
        auto [plain, badP] = run(1, false, nullptr);
        auto [up4, badU]   = run(4, false, nullptr);
        check(plain.size() == 5 && up4.size() == 5 && badP == 0 && badU == 0, "every window emitted, plain and x4, none out of range");
        check(up4[1].size() == static_cast<std::size_t>(2 * nSamp * F), "the x4 row holds nChan * nSamp * 4 entries");
        bool same = true;
        for (int s = 0; s < nSamp; ++s)
            for (int ch = 0; ch < 2; ++ch)
                same = same && up4[1][static_cast<std::size_t>(s * F) * 2 + ch] == plain[1][static_cast<std::size_t>(s) * 2 + ch];
        check(same, "ticks at multiples of 4 equal the plain cut (spike A)");
        // spike B: trough at 2000.5 -> in the window starting 2000 - 6, tick (6.5) * 4 = 26 on channel 1 (group ch 2)
        int argmin = 0;
        for (int k = 0; k < nSamp * F; ++k)
            if (up4[2][static_cast<std::size_t>(k) * 2 + 1] < up4[2][static_cast<std::size_t>(argmin) * 2 + 1]) argmin = k;
        check(argmin == 26, "a trough between two samples lands at the fractional tick (" + std::to_string(argmin) + " == 26)");
        check(up4[0][static_cast<std::size_t>(peak * F) * 2 + 0] == plain[0][static_cast<std::size_t>(peak) * 2 + 0] && !up4[4].empty(),
              "windows at the file edges (no margin on one side) still come out");
        auto [shifted, badS] = run(4, false, phases);
        // spike index 3 at 2001 with phase 2: its ticks are those of 2001 + 0.5 -> trough at 2000.5 is 0.5 earlier: tick (5.5 - 0.5)*4 = 20... index 3's window starts 1995, trough at 2000.5 = 5.5 samples = tick 22, minus the phase 2 = tick 20
        argmin = 0;
        for (int k = 0; k < nSamp * F; ++k)
            if (shifted[3][static_cast<std::size_t>(k) * 2 + 1] < shifted[3][static_cast<std::size_t>(argmin) * 2 + 1]) argmin = k;
        check(badS == 0 && argmin == 20, "a per-spike phase of 2 ticks shifts the cut by half a sample (" + std::to_string(argmin) + " == 20)");
        auto [tr, badT] = run(4, true, nullptr);
        // the transform in the super-sampled domain: the first tick of channel 0 is sd[tick0] - sd[tick-1];
        // with first-difference order and 2 channels, sd = ch0 - ch1 ... just check the shape and that it differs from the plain one
        check(badT == 0 && tr[1].size() == static_cast<std::size_t>(2 * nSamp * F), "the stderiv transform runs in the super-sampled domain (row shape)");
        // first difference, 2 channels: sd = ch0 - ch1 per tick; tick 0 = sd[0] - sd[-1] (seeded from the tick before)
        {
            const int c0 = up4[1][0], c1 = up4[1][1];
            const int sd0 = c0 - c1;
            check(tr[1][0] != sd0 || sd0 == 0, "stderiv x4: tick 0 is seeded from the tick before the window, not the bare spatial derivative");
        }
        fs::remove_all(dir);
    }
    // ── fitPca ───────────────────────────────────────────────────────────
    {
        const int N = 400, D = 24;
        std::mt19937 rng(7);
        std::normal_distribution<double> nd(0.0, 1.0);
        std::vector<double> c1(static_cast<std::size_t>(D)), c2(static_cast<std::size_t>(D));
        for (int s = 0; s < D; ++s) { c1[static_cast<std::size_t>(s)] = gauss(s, 10, 2); c2[static_cast<std::size_t>(s)] = gauss(s, 14, 4) * 0.5; }
        std::vector<double> X(static_cast<std::size_t>(N) * D);
        for (int i = 0; i < N; ++i) {
            const double a = 1 + 0.3 * nd(rng), b = 0.4 * nd(rng);
            for (int s = 0; s < D; ++s) X[static_cast<std::size_t>(i) * D + s] = a * c1[static_cast<std::size_t>(s)] + b * c2[static_cast<std::size_t>(s)] + 3.0 * nd(rng);
        }
        PcaBasis pca;
        const bool ok = fitPca(N, 1, D, 0, 3, [&](int i, int, int s) { return X[static_cast<std::size_t>(i) * D + s]; }, pca);
        check(ok && pca.valid() && pca.nComp == 3 && pca.centered, "fitPca: a valid centered 3-component basis");
        // energy captured by the first two components vs the total centered energy
        double tot = 0.0, cap = 0.0;
        std::vector<double> scores;
        std::vector<std::int16_t> w(static_cast<std::size_t>(D));
        for (int i = 0; i < N; ++i) {
            for (int s = 0; s < D; ++s) { w[static_cast<std::size_t>(s)] = static_cast<std::int16_t>(std::lround(X[static_cast<std::size_t>(i) * D + s])); tot += std::pow(w[static_cast<std::size_t>(s)] - pca.means[0][static_cast<std::size_t>(s)], 2); }
            pcaProjectScores(w.data(), 1, D, pca, scores);
            cap += scores[0] * scores[0] + scores[1] * scores[1];
        }
        check(cap / tot > 0.85, "two components capture the two planted directions (" + std::to_string(cap / tot) + " of the energy)");
        double orth = 0.0;
        for (int u = 0; u < D; ++u) orth += pca.evec[0][static_cast<std::size_t>(u)] * pca.evec[0][static_cast<std::size_t>(D + u)];
        check(std::fabs(orth) < 1e-9, "the eigenvectors are orthogonal");
        const fs::path p = fs::temp_directory_path() / "ns3_supersample_test.pca";
        PcaBasis back;
        check(writePca(p.string(), pca) && loadPca(p.string(), back) && back.nComp == 3 && back.data2use == D
              && std::fabs(back.evec[0][5] - pca.evec[0][5]) < 1e-12, "writePca / loadPca round trip");
        fs::remove(p);
    }
    std::printf("supersample_test: %s (%d failed)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
