/* ============================================================================
 * test_klusters_realign.cpp
 *
 * Unit tests for klusters-faithful per-spike realignment.
 *
 * Test strategy:
 *   1. Synthetic templates with known peak positions; verify PreAlignTemplate
 *      moves them to peakPos.
 *   2. Inject known per-spike shifts into copies of a template; verify
 *      ComputeClusterShifts returns the inverse shifts (so applying them
 *      undoes the injection).
 *   3. Pure-noise spikes: shifts should be small (no clean peak).
 *   4. Argument validation.
 *
 * Build:
 *   c++ -std=c++20 -fopenmp -I.. \
 *       -I../../libklustersshared/src/xcorr \
 *       test_klusters_realign.cpp ../klusters_realign.cpp \
 *       ../../libklustersshared/src/xcorr/realign_xcorr_dispatch.cpp \
 *       ../../libklustersshared/src/xcorr/realign_xcorr_omp.cpp \
 *       -o test_klusters_realign
 * ========================================================================== */

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "../klusters_realign.h"

// ---------------------------------------------------------------------------
// Mini test harness
// ---------------------------------------------------------------------------
static int g_failed = 0;
static int g_total  = 0;
static const char* g_testname = "(unknown)";

#define EXPECT(cond) do { \
    ++g_total; \
    if (!(cond)) { \
        ++g_failed; \
        std::fprintf(stderr, "  FAIL [%s:%d] %s: %s\n", \
                     __FILE__, __LINE__, g_testname, #cond); \
    } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    ++g_total; \
    auto _va = (a); auto _vb = (b); \
    if (!(_va == _vb)) { \
        ++g_failed; \
        std::fprintf(stderr, "  FAIL [%s:%d] %s: %s == %s  (got %lld != %lld)\n", \
                     __FILE__, __LINE__, g_testname, #a, #b, \
                     static_cast<long long>(_va), \
                     static_cast<long long>(_vb)); \
    } \
} while (0)

#define EXPECT_NEAR(a, b, tol) do { \
    ++g_total; \
    double _va = (a); double _vb = (b); double _t = (tol); \
    if (std::abs(_va - _vb) > _t) { \
        ++g_failed; \
        std::fprintf(stderr, "  FAIL [%s:%d] %s: |%s - %s| <= %g  (got |%g - %g| = %g)\n", \
                     __FILE__, __LINE__, g_testname, #a, #b, _t, _va, _vb, \
                     std::abs(_va - _vb)); \
    } \
} while (0)

#define TEST(name) do { g_testname = name; \
    std::fprintf(stdout, "  • %s\n", g_testname); } while (0)

// ---------------------------------------------------------------------------
// Synthetic spike: a unit with a sharp negative peak on channel 0 and
// progressively-attenuated copies on other channels (typical tetrode/octrode).
// Layout: sample-major a[s*nChan + ch], length nChan*nSamples.
// peakAt: sample index where the trough lands.
// ---------------------------------------------------------------------------
static std::vector<int16_t> make_synthetic_spike(int nChan, int nSamples,
                                                 int peakAt,
                                                 int peakAmpCh0 = -5000,
                                                 int seed = 0)
{
    std::vector<int16_t> wv(static_cast<size_t>(nChan) * nSamples, 0);
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0f, 80.0f);

    // Gaussian-shaped negative peak centred at peakAt
    const double sigma = 1.8;
    for (int s = 0; s < nSamples; ++s) {
        const double dt   = (s - peakAt);
        const double bell = std::exp(-(dt*dt) / (2.0 * sigma * sigma));
        for (int ch = 0; ch < nChan; ++ch) {
            const double atten = std::pow(0.6, ch);
            const double v     = peakAmpCh0 * atten * bell + noise(rng);
            wv[s * nChan + ch] = static_cast<int16_t>(std::lround(v));
        }
    }
    return wv;
}

// Inject a shift δ into a sample-major waveform.  Returns the shifted copy.
// Positive δ rolls the spike *later* in the window (peak moves to higher s).
static std::vector<int16_t> shift_spike(
    const std::vector<int16_t>& src,
    int nChan, int nSamples, int delta)
{
    std::vector<int16_t> out(src.size(), 0);
    for (int s = 0; s < nSamples; ++s) {
        const int srcS = s - delta;
        if (srcS < 0 || srcS >= nSamples) continue;
        for (int ch = 0; ch < nChan; ++ch)
            out[s * nChan + ch] = src[srcS * nChan + ch];
    }
    return out;
}

// Find the sample with the largest Σ_ch |amp|.  Mirrors the algorithm in
// PreAlignTemplate, used by tests to confirm the peak landed where expected.
static int find_peak_sample(const int16_t* wv, int nChan, int nSamples)
{
    int    bestS  = 0;
    double bestA  = -1.0;
    for (int s = 0; s < nSamples; ++s) {
        double amp = 0.0;
        for (int ch = 0; ch < nChan; ++ch)
            amp += std::abs(static_cast<double>(wv[s * nChan + ch]));
        if (amp > bestA) { bestA = amp; bestS = s; }
    }
    return bestS;
}

// ===========================================================================
// Tests for PreAlignTemplate
// ===========================================================================

static void test_prealign_already_aligned()
{
    TEST("PreAlignTemplate: already at peakPos -> no shift");
    const int nChan = 4, nSamples = 32, peakPos = 16;
    auto wv = make_synthetic_spike(nChan, nSamples, /*peakAt=*/peakPos, -5000, 1);
    const int sh = KlustersRealign::PreAlignTemplate(wv.data(), nChan, nSamples, peakPos);
    EXPECT_EQ(sh, 0);
    EXPECT_EQ(find_peak_sample(wv.data(), nChan, nSamples), peakPos);
}

static void test_prealign_peak_late()
{
    TEST("PreAlignTemplate: peak at sample 20, peakPos=16 -> shift = -4");
    const int nChan = 4, nSamples = 32, peakPos = 16, peakAt = 20;
    auto wv = make_synthetic_spike(nChan, nSamples, peakAt, -5000, 2);
    EXPECT_EQ(find_peak_sample(wv.data(), nChan, nSamples), peakAt);

    const int sh = KlustersRealign::PreAlignTemplate(wv.data(), nChan, nSamples, peakPos);
    EXPECT_EQ(sh, -4);  // peakPos - peakAt = -4
    EXPECT_EQ(find_peak_sample(wv.data(), nChan, nSamples), peakPos);
}

static void test_prealign_peak_early()
{
    TEST("PreAlignTemplate: peak at sample 10, peakPos=16 -> shift = +6");
    const int nChan = 4, nSamples = 32, peakPos = 16, peakAt = 10;
    auto wv = make_synthetic_spike(nChan, nSamples, peakAt, -5000, 3);
    const int sh = KlustersRealign::PreAlignTemplate(wv.data(), nChan, nSamples, peakPos);
    EXPECT_EQ(sh, 6);
    EXPECT_EQ(find_peak_sample(wv.data(), nChan, nSamples), peakPos);
}

static void test_prealign_arg_validation()
{
    TEST("PreAlignTemplate: invalid args return 0 without crashing");
    int16_t dummy[8] = {0};
    EXPECT_EQ(KlustersRealign::PreAlignTemplate(nullptr, 4, 32, 16), 0);
    EXPECT_EQ(KlustersRealign::PreAlignTemplate(dummy, 0, 32, 16), 0);
    EXPECT_EQ(KlustersRealign::PreAlignTemplate(dummy, 4, 0, 16), 0);
    EXPECT_EQ(KlustersRealign::PreAlignTemplate(dummy, 4, 32, -1), 0);
    EXPECT_EQ(KlustersRealign::PreAlignTemplate(dummy, 4, 32, 32), 0);
}

// ===========================================================================
// Tests for ComputeClusterShifts
// ===========================================================================

static void test_compute_no_injected_shifts()
{
    TEST("ComputeClusterShifts: aligned spikes return all-zero shifts");
    const int nChan = 4, nSamples = 32, peakPos = 16, maxShift = 8;
    const int nSpikes = 50;

    std::vector<std::vector<int16_t>> spikes;
    spikes.reserve(nSpikes);
    for (int i = 0; i < nSpikes; ++i)
        spikes.push_back(make_synthetic_spike(nChan, nSamples, peakPos, -5000,
                                              100 + i));

    std::vector<int>   shifts;
    std::vector<float> scores;
    const bool ok = KlustersRealign::ComputeClusterShifts(
        spikes, nChan, nSamples, peakPos, maxShift, shifts, scores);
    EXPECT(ok);
    EXPECT_EQ(static_cast<int>(shifts.size()), nSpikes);

    int nNonZero = 0;
    int maxAbs   = 0;
    for (int sh : shifts) {
        if (sh != 0) ++nNonZero;
        maxAbs = std::max(maxAbs, std::abs(sh));
    }
    // Up to a few spikes can have |shift|=1 due to noise; allow it.
    EXPECT(nNonZero <= 5);
    EXPECT(maxAbs <= 1);
}

static void test_compute_recovers_injected_shifts()
{
    TEST("ComputeClusterShifts: recovers known injected per-spike shifts");
    const int nChan = 8, nSamples = 32, peakPos = 16, maxShift = 8;
    const int nSpikes = 100;

    // 80% aligned, 20% shifted by various amounts ∈ {-4, -2, +1, +3, +5}.
    auto base = make_synthetic_spike(nChan, nSamples, peakPos, -5000, 999);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> pickShift(0, 4);
    const int candShifts[5] = {-4, -2, 1, 3, 5};

    std::vector<std::vector<int16_t>> spikes;
    std::vector<int> injected(nSpikes, 0);
    spikes.reserve(nSpikes);
    for (int i = 0; i < nSpikes; ++i) {
        // Re-generate with fresh noise so xcorr has work to do.
        auto wv = make_synthetic_spike(nChan, nSamples, peakPos, -5000, 1000 + i);
        if (i < nSpikes * 8 / 10) {
            spikes.push_back(std::move(wv));
        } else {
            const int dt = candShifts[pickShift(rng)];
            injected[i] = dt;
            spikes.push_back(shift_spike(wv, nChan, nSamples, dt));
        }
    }

    std::vector<int>   shifts;
    std::vector<float> scores;
    const bool ok = KlustersRealign::ComputeClusterShifts(
        spikes, nChan, nSamples, peakPos, maxShift, shifts, scores);
    EXPECT(ok);

    // XcorrDispatch sign convention (matches klusters spikerealign.cpp:669,
    // `newTs = oldTs + sh`): returned shift = +δ when spike's peak is δ
    // samples LATER than the template's peak.  Since shift_spike injects
    // +dt by putting the source-spike's peak at peakPos+dt, the returned
    // shift should equal injected[i].
    int nMatched = 0;
    int nClose   = 0;
    for (int i = 0; i < nSpikes; ++i) {
        const int expected = injected[i];
        if (shifts[i] == expected)               ++nMatched;
        if (std::abs(shifts[i] - expected) <= 1) ++nClose;
    }
    // Tolerate occasional ±1 due to noise: require ≥90% close, ≥70% exact.
    EXPECT(nMatched >= nSpikes * 7 / 10);
    EXPECT(nClose   >= nSpikes * 9 / 10);
}

static void test_compute_with_misaligned_template_input()
{
    TEST("ComputeClusterShifts: works when ALL spikes are uniformly shifted");
    // If every spike has its peak at peakPos+3, the cluster mean also peaks
    // at +3, PreAlignTemplate moves the template's peak BACK to peakPos
    // (template shift = -3), and then every spike's peak is +3 from the
    // pre-aligned template → xcorr returns +3 for every spike.
    // This is the bulk-drift case (cluster mean was uniformly mis-aligned).
    const int nChan = 4, nSamples = 32, peakPos = 16, maxShift = 8;
    const int nSpikes = 40;

    std::vector<std::vector<int16_t>> spikes;
    spikes.reserve(nSpikes);
    for (int i = 0; i < nSpikes; ++i) {
        auto wv = make_synthetic_spike(nChan, nSamples, peakPos + 3, -5000,
                                       2000 + i);
        spikes.push_back(std::move(wv));
    }

    std::vector<int>   shifts;
    std::vector<float> scores;
    KlustersRealign::ComputeClusterShifts(spikes, nChan, nSamples,
                                          peakPos, maxShift, shifts, scores);

    // All shifts should cluster around +3, with allowed ±1 jitter.
    int nNear = 0;
    for (int sh : shifts) if (std::abs(sh - 3) <= 1) ++nNear;
    EXPECT(nNear >= nSpikes * 9 / 10);
}

static void test_compute_arg_validation()
{
    TEST("ComputeClusterShifts: argument validation");
    std::vector<int> sh; std::vector<float> sc;
    // Empty input.
    EXPECT(!KlustersRealign::ComputeClusterShifts({}, 4, 32, 16, 8, sh, sc));

    // Mismatched per-spike size.
    std::vector<std::vector<int16_t>> bad;
    bad.push_back(std::vector<int16_t>(100, 0));   // wrong size; expects 4*32=128
    EXPECT(!KlustersRealign::ComputeClusterShifts(bad, 4, 32, 16, 8, sh, sc));

    // maxShift too large (≥ nSamples/2 by contract).
    auto wv = make_synthetic_spike(4, 32, 16, -5000, 0);
    std::vector<std::vector<int16_t>> ok = {wv};
    EXPECT(!KlustersRealign::ComputeClusterShifts(ok, 4, 32, 16, 16, sh, sc));
}

static void test_compute_flat_matches_vec_of_vec()
{
    TEST("ComputeClusterShiftsFlat: produces identical output to vec-of-vec API");
    const int nChan = 4, nSamples = 32, peakPos = 16, maxShift = 6;
    const int nSpikes = 25;

    std::vector<std::vector<int16_t>> spikes;
    spikes.reserve(nSpikes);
    for (int i = 0; i < nSpikes; ++i)
        spikes.push_back(make_synthetic_spike(nChan, nSamples,
                                              peakPos + (i % 3 - 1),
                                              -5000, 3000 + i));

    std::vector<int> sh1, sh2;  std::vector<float> sc1, sc2;
    KlustersRealign::ComputeClusterShifts(spikes, nChan, nSamples,
                                          peakPos, maxShift, sh1, sc1);

    const size_t nPts = static_cast<size_t>(nChan) * nSamples;
    std::vector<int16_t> flat(nSpikes * nPts);
    for (int i = 0; i < nSpikes; ++i)
        std::memcpy(flat.data() + i * nPts, spikes[i].data(),
                    nPts * sizeof(int16_t));
    KlustersRealign::ComputeClusterShiftsFlat(flat.data(), nSpikes,
                                              nChan, nSamples,
                                              peakPos, maxShift, sh2, sc2);
    for (int i = 0; i < nSpikes; ++i) {
        EXPECT_EQ(sh1[i], sh2[i]);
        EXPECT_NEAR(sc1[i], sc2[i], 1e-5);
    }
}

int main()
{
    std::fprintf(stdout, "test_klusters_realign\n");
    std::fprintf(stdout, "---------------------\n");

    test_prealign_already_aligned();
    test_prealign_peak_late();
    test_prealign_peak_early();
    test_prealign_arg_validation();

    test_compute_no_injected_shifts();
    test_compute_recovers_injected_shifts();
    test_compute_with_misaligned_template_input();
    test_compute_arg_validation();
    test_compute_flat_matches_vec_of_vec();

    std::fprintf(stdout, "---------------------\n");
    std::fprintf(stdout, "%d/%d assertions passed%s\n",
                 g_total - g_failed, g_total,
                 g_failed ? "  (FAIL)" : "  (PASS)");
    return g_failed ? 1 : 0;
}
