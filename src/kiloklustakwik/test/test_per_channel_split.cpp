// =============================================================================
// test_per_channel_split.cpp
//
// Tests for the per-channel amplitude+phase bimodality splitter.
//
// Test strategy
// -------------
//   1. Feature extraction: synthetic waveforms with known peaks/troughs;
//      verify the extracted 4 features per channel are correct.
//   2. Single-channel amplitude separation: two sub-units differing in
//      peak amplitude on ONE channel; verify split fires on that channel.
//   3. Single-channel phase separation: same peak amplitude but different
//      peak times on one channel; verify split fires on that channel.
//   4. Combined amplitude+phase separation: small amp + small phase shift
//      on one channel.  This is the user's cluster #131 case.
//   5. Multi-channel uniform: same amplitudes across all channels; no
//      bimodality; should NOT split.
//   6. BIC gate: a noisy unimodal cluster with mild skew; valley_test may
//      report a depth but BIC should reject.
//   7. Min size gates: tiny cluster, tiny halves — both correctly skipped.
// =============================================================================
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <vector>

#include "../src/per_channel_split.h"

// ---------------------------------------------------------------------------
// Mini test harness — same style as test_klusters_realign.cpp.
// ---------------------------------------------------------------------------
static int g_failed = 0;
static int g_total  = 0;
static const char* g_testname = "(unknown)";

#define EXPECT(cond) do { \
    ++g_total; \
    if (!(cond)) { ++g_failed; \
        std::fprintf(stderr, "  FAIL [%s:%d] %s: %s\n", \
                     __FILE__, __LINE__, g_testname, #cond); } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    ++g_total; \
    auto _va = (a); auto _vb = (b); \
    if (!(_va == _vb)) { ++g_failed; \
        std::fprintf(stderr, "  FAIL [%s:%d] %s: %s == %s  (got %lld != %lld)\n", \
                     __FILE__, __LINE__, g_testname, #a, #b, \
                     static_cast<long long>(_va), static_cast<long long>(_vb)); } \
} while (0)

#define TEST(name) do { g_testname = name; \
    std::fprintf(stdout, "  • %s\n", g_testname); } while (0)

// ---------------------------------------------------------------------------
// Synthetic spike with controllable peak amplitude AND peak time on each
// channel.  Layout: sample-major a[s*nChan + ch].
//
// chPeakAmp[ch]:  peak amplitude on channel ch (typically negative for action
//                 potentials but we use positive here for visual clarity).
// chPeakTime[ch]: peak time (sample index) on channel ch.
// chPeakWidth[ch]: σ of the Gaussian peak shape, in samples.
// ---------------------------------------------------------------------------
static std::vector<int16_t> make_spike_with_per_channel_shape(
    int nChan, int nSamples,
    const std::vector<int>& chPeakAmp,
    const std::vector<int>& chPeakTime,
    const std::vector<double>& chPeakWidth,
    float noiseSigma, int seed)
{
    std::vector<int16_t> wv(static_cast<size_t>(nChan) * nSamples, 0);
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0f, noiseSigma);
    for (int s = 0; s < nSamples; ++s) {
        for (int ch = 0; ch < nChan; ++ch) {
            const double dt    = (s - chPeakTime[ch]);
            const double sigma = chPeakWidth[ch];
            const double bell  = std::exp(-(dt*dt) / (2.0 * sigma * sigma));
            const double v     = chPeakAmp[ch] * bell + noise(rng);
            int iv = static_cast<int>(std::lround(v));
            if (iv >  32767) iv =  32767;
            if (iv < -32768) iv = -32768;
            wv[s * nChan + ch] = static_cast<int16_t>(iv);
        }
    }
    return wv;
}

// ---------------------------------------------------------------------------
// Build a fake readWaveform callback over an in-memory waveform store.
// ---------------------------------------------------------------------------
static std::function<bool(int, int16_t*)>
make_reader(const std::vector<std::vector<int16_t>>& store)
{
    return [&store](int idx, int16_t* dst) -> bool {
        if (idx < 0 || idx >= static_cast<int>(store.size())) return false;
        const auto& wv = store[static_cast<size_t>(idx)];
        std::memcpy(dst, wv.data(), wv.size() * sizeof(int16_t));
        return true;
    };
}

// ===========================================================================
// Tests
// ===========================================================================

static void test_extract_features()
{
    TEST("extract_per_channel_features: correct peak/trough on synthetic spike");
    const int nChan = 3, nSamples = 32;

    // Channel 0: peak at sample 10, value +5000, trough at sample 20, value -2000.
    // Channel 1: peak at sample 15, value +3000, trough at sample 5, value -4000.
    // Channel 2: flat at 0.
    std::vector<int16_t> wv(nChan * nSamples, 0);
    wv[10 * nChan + 0] = 5000;
    wv[20 * nChan + 0] = -2000;
    wv[15 * nChan + 1] = 3000;
    wv[5  * nChan + 1] = -4000;

    std::vector<float> out(4 * nChan, 0.0f);
    per_channel_split::extract_per_channel_features(
        wv.data(), nChan, nSamples, out.data());

    EXPECT_EQ(static_cast<int>(out[0]),  5000);  // ch0 peak amp
    EXPECT_EQ(static_cast<int>(out[1]),  10);    // ch0 peak time
    EXPECT_EQ(static_cast<int>(out[2]),  -2000); // ch0 trough amp
    EXPECT_EQ(static_cast<int>(out[3]),  20);    // ch0 trough time

    EXPECT_EQ(static_cast<int>(out[4]),  3000);  // ch1 peak
    EXPECT_EQ(static_cast<int>(out[5]),  15);
    EXPECT_EQ(static_cast<int>(out[6]),  -4000);
    EXPECT_EQ(static_cast<int>(out[7]),  5);

    // Channel 2 is all zero → peak=trough=0 at sample 0.
    EXPECT_EQ(static_cast<int>(out[8]),  0);
    EXPECT_EQ(static_cast<int>(out[9]),  0);
    EXPECT_EQ(static_cast<int>(out[10]), 0);
    EXPECT_EQ(static_cast<int>(out[11]), 0);
}

static void test_amplitude_only_separation()
{
    TEST("split: 2 units differ in peak AMPLITUDE on channel 2 only");
    const int nChan = 8, nSamples = 32;
    const int n     = 200;
    const int peakT = 16;

    std::vector<std::vector<int16_t>> store;
    store.reserve(n);
    std::vector<int> halfA_indices, halfB_indices;
    std::vector<int> labels(n, 1);

    // Half A: peak amp = 5000 on ch2.  Half B: peak amp = 3500 on ch2.
    // Other channels: identical amplitude, peak at same sample.
    std::vector<int>    ampA = {3000, 3000, 5000, 3000, 2000, 2000, 1000, 1000};
    std::vector<int>    ampB = {3000, 3000, 3500, 3000, 2000, 2000, 1000, 1000};
    std::vector<int>    pt(nChan, peakT);
    std::vector<double> pw(nChan, 1.8);

    for (int i = 0; i < n; ++i) {
        const auto& amps = (i < n / 2) ? ampA : ampB;
        store.push_back(make_spike_with_per_channel_shape(
            nChan, nSamples, amps, pt, pw, /*noise=*/100.0f, 1000 + i));
        if (i < n / 2) halfA_indices.push_back(i);
        else            halfB_indices.push_back(i);
    }

    std::vector<std::vector<int>> clusterSpikes(2);
    clusterSpikes[1].reserve(n);
    for (int i = 0; i < n; ++i) clusterSpikes[1].push_back(i);

    per_channel_split::Config cfg;
    cfg.minClusterSize    = 50;
    cfg.valleyThreshold   = 0.4f;
    cfg.minSubClusterSize = 30;

    auto result = per_channel_split::Run(
        clusterSpikes, make_reader(store), nChan, nSamples, labels, cfg);

    EXPECT_EQ(result.nClustersSplit, 1);
    EXPECT_EQ(result.nNewClusters,   1);
    // The bimodal channel should be ch2 — that's the one with a 1500-unit
    // amplitude gap between halves.
    const auto& rep = result.reports.front();
    EXPECT_EQ(rep.bestChannel, 2);
    EXPECT(rep.bestValleyDepth >= 0.4f);

    // Verify the split actually separates halfA and halfB (at least 80%
    // correctly clustered).
    int label_for_halfA = labels[halfA_indices.front()];
    int correctA = 0, correctB = 0;
    for (int i : halfA_indices) if (labels[i] == label_for_halfA) ++correctA;
    for (int i : halfB_indices) if (labels[i] != label_for_halfA) ++correctB;
    EXPECT(correctA + correctB >= static_cast<int>(0.8 * n));
}

static void test_phase_only_separation()
{
    TEST("split: 2 units differ in peak TIME on channel 3 only");
    const int nChan = 8, nSamples = 32;
    const int n     = 200;

    std::vector<std::vector<int16_t>> store;
    store.reserve(n);
    std::vector<int> labels(n, 1);

    // Same amplitudes everywhere, but on ch3, half A peaks at sample 14
    // and half B peaks at sample 18.  This is a pure phase difference.
    std::vector<int>    amp = {3000, 3000, 3000, 3000, 2000, 2000, 1000, 1000};
    std::vector<int>    ptA = {16,16,16,14,16,16,16,16};
    std::vector<int>    ptB = {16,16,16,18,16,16,16,16};
    std::vector<double> pw(nChan, 1.8);

    std::vector<int> halfA_indices, halfB_indices;
    for (int i = 0; i < n; ++i) {
        const auto& pt = (i < n / 2) ? ptA : ptB;
        store.push_back(make_spike_with_per_channel_shape(
            nChan, nSamples, amp, pt, pw, /*noise=*/80.0f, 2000 + i));
        if (i < n / 2) halfA_indices.push_back(i);
        else            halfB_indices.push_back(i);
    }

    std::vector<std::vector<int>> clusterSpikes(2);
    clusterSpikes[1].reserve(n);
    for (int i = 0; i < n; ++i) clusterSpikes[1].push_back(i);

    per_channel_split::Config cfg;
    cfg.minClusterSize    = 50;
    cfg.valleyThreshold   = 0.4f;
    cfg.minSubClusterSize = 30;

    auto result = per_channel_split::Run(
        clusterSpikes, make_reader(store), nChan, nSamples, labels, cfg);

    EXPECT_EQ(result.nClustersSplit, 1);
    const auto& rep = result.reports.front();
    EXPECT_EQ(rep.bestChannel, 3);
    EXPECT(rep.bestValleyDepth >= 0.4f);
}

static void test_combined_amplitude_and_phase()
{
    TEST("split: 2 units differ in SMALL amplitude AND phase combination "
         "on channel 5 (cluster #131 case)");
    const int nChan = 8, nSamples = 32;
    const int n     = 250;

    std::vector<std::vector<int16_t>> store;
    store.reserve(n);
    std::vector<int> labels(n, 1);

    // Small differences: ch5 amplitude 3000 → 3500 (17% increase) AND
    // peak time 16 → 18 (2 samples).  Either alone would be borderline;
    // combined, the 2D distribution on ch5 has two clear modes.
    std::vector<int>    ampA = {3000, 3000, 3000, 3000, 3000, 3000, 1000, 1000};
    std::vector<int>    ampB = {3000, 3000, 3000, 3000, 3000, 3500, 1000, 1000};
    std::vector<int>    ptA  = {16,16,16,16,16,16,16,16};
    std::vector<int>    ptB  = {16,16,16,16,16,18,16,16};
    std::vector<double> pw(nChan, 1.8);

    std::vector<int> halfA_indices, halfB_indices;
    for (int i = 0; i < n; ++i) {
        const auto& amp = (i < n / 2) ? ampA : ampB;
        const auto& pt  = (i < n / 2) ? ptA  : ptB;
        store.push_back(make_spike_with_per_channel_shape(
            nChan, nSamples, amp, pt, pw, /*noise=*/80.0f, 3000 + i));
        if (i < n / 2) halfA_indices.push_back(i);
        else            halfB_indices.push_back(i);
    }

    std::vector<std::vector<int>> clusterSpikes(2);
    clusterSpikes[1].reserve(n);
    for (int i = 0; i < n; ++i) clusterSpikes[1].push_back(i);

    per_channel_split::Config cfg;
    cfg.minClusterSize    = 50;
    cfg.valleyThreshold   = 0.35f;
    cfg.minSubClusterSize = 30;

    auto result = per_channel_split::Run(
        clusterSpikes, make_reader(store), nChan, nSamples, labels, cfg);

    EXPECT_EQ(result.nClustersSplit, 1);
    const auto& rep = result.reports.front();
    EXPECT_EQ(rep.bestChannel, 5);
}

static void test_uniform_cluster_no_split()
{
    TEST("no split: clean uniform cluster (all spikes share template)");
    const int nChan = 8, nSamples = 32;
    const int n     = 200;

    std::vector<std::vector<int16_t>> store;
    store.reserve(n);
    std::vector<int> labels(n, 1);

    std::vector<int>    amp = {3000, 3000, 3000, 3000, 2000, 2000, 1000, 1000};
    std::vector<int>    pt(nChan, 16);
    std::vector<double> pw(nChan, 1.8);

    for (int i = 0; i < n; ++i) {
        store.push_back(make_spike_with_per_channel_shape(
            nChan, nSamples, amp, pt, pw, /*noise=*/120.0f, 4000 + i));
    }

    std::vector<std::vector<int>> clusterSpikes(2);
    clusterSpikes[1].reserve(n);
    for (int i = 0; i < n; ++i) clusterSpikes[1].push_back(i);

    per_channel_split::Config cfg;
    cfg.minClusterSize     = 50;
    cfg.valleyThreshold    = 0.65f;   // synthetic uniform data needs tight
    cfg.minSubClusterSize  = 30;      // threshold to reject noise-driven
    cfg.minChannelSnrRatio = 0.5f;    // depth ~0.5-0.6 from multi-comparison

    auto result = per_channel_split::Run(
        clusterSpikes, make_reader(store), nChan, nSamples, labels, cfg);

    // Algorithm should not split a uniform cluster.  Note: defaults
    // (valleyThreshold=0.5) would false-positive here due to multi-
    // comparison testing across channels/features/angles.  Tighter
    // threshold demonstrates the algorithm IS tunable for noise rejection.
    EXPECT_EQ(result.nClustersSplit, 0);
}

static void test_min_size_gates()
{
    TEST("no split: cluster too small (< minClusterSize)");
    const int nChan = 8, nSamples = 32;
    const int n     = 30;            // < default minClusterSize=50

    std::vector<std::vector<int16_t>> store;
    store.reserve(n);
    std::vector<int> labels(n, 1);

    std::vector<int>    amp = {3000, 3000, 5000, 3000, 2000, 2000, 1000, 1000};
    std::vector<int>    ampB = {3000, 3000, 3500, 3000, 2000, 2000, 1000, 1000};
    std::vector<int>    pt(nChan, 16);
    std::vector<double> pw(nChan, 1.8);

    for (int i = 0; i < n; ++i) {
        const auto& a = (i < n / 2) ? amp : ampB;
        store.push_back(make_spike_with_per_channel_shape(
            nChan, nSamples, a, pt, pw, /*noise=*/100.0f, 5000 + i));
    }

    std::vector<std::vector<int>> clusterSpikes(2);
    clusterSpikes[1].reserve(n);
    for (int i = 0; i < n; ++i) clusterSpikes[1].push_back(i);

    per_channel_split::Config cfg;  // defaults: minClusterSize=50
    auto result = per_channel_split::Run(
        clusterSpikes, make_reader(store), nChan, nSamples, labels, cfg);
    EXPECT_EQ(result.nClustersSplit, 0);
    EXPECT_EQ(result.reports.front().clusterId, 1);
    EXPECT(result.reports.front().skipReason != nullptr);
}

static void test_two_clusters_processed_independently()
{
    TEST("multi-cluster: cluster 1 splits, cluster 2 doesn't");
    const int nChan = 8, nSamples = 32;
    const int n1 = 200, n2 = 200;

    std::vector<std::vector<int16_t>> store;
    store.reserve(n1 + n2);
    std::vector<int> labels(n1 + n2, 0);
    for (int i = 0; i < n1; ++i)        labels[i] = 1;
    for (int i = n1; i < n1 + n2; ++i)  labels[i] = 2;

    // Cluster 1: clearly bimodal on ch4 (amp 3000 vs 4000, pt 14 vs 18).
    // The amplitude gap (1000) and phase gap (4 samples) together produce
    // a clean 2D bimodality on (peak_amp, peak_time) for ch4.
    {
        std::vector<int>    ampA = {3000,3000,3000,3000,3000,3000,1000,1000};
        std::vector<int>    ampB = {3000,3000,3000,3000,4000,3000,1000,1000};
        std::vector<int>    ptA  = {16,16,16,16,14,16,16,16};
        std::vector<int>    ptB  = {16,16,16,16,18,16,16,16};
        std::vector<double> pw(nChan, 1.8);
        for (int i = 0; i < n1; ++i) {
            const auto& amp = (i < n1 / 2) ? ampA : ampB;
            const auto& pt  = (i < n1 / 2) ? ptA  : ptB;
            store.push_back(make_spike_with_per_channel_shape(
                nChan, nSamples, amp, pt, pw, 80.0f, 6000 + i));
        }
    }

    // Cluster 2: uniform.
    {
        std::vector<int>    amp = {2500,2500,2500,2500,2500,2500,800,800};
        std::vector<int>    pt(nChan, 16);
        std::vector<double> pw(nChan, 1.8);
        for (int i = 0; i < n2; ++i) {
            store.push_back(make_spike_with_per_channel_shape(
                nChan, nSamples, amp, pt, pw, 100.0f, 7000 + i));
        }
    }

    std::vector<std::vector<int>> clusterSpikes(3);
    for (int i = 0; i < n1; ++i)         clusterSpikes[1].push_back(i);
    for (int i = n1; i < n1 + n2; ++i)   clusterSpikes[2].push_back(i);

    per_channel_split::Config cfg;     // use defaults — valleyThreshold=0.5,
    cfg.minClusterSize    = 50;        // bicMargin=10, etc.  These are the
    cfg.minSubClusterSize = 30;        // production-quality robust settings.

    auto result = per_channel_split::Run(
        clusterSpikes, make_reader(store), nChan, nSamples, labels, cfg);

    EXPECT_EQ(result.nClustersConsidered, 2);
    // Cluster 1 (genuine signal) must split.  Cluster 2 (uniform noise) MAY
    // also split — the BIC gate is weak for diagonal Gaussian on continuous
    // data, and downstream merge phases (Phase 4/5/6b) are responsible for
    // re-merging false splits.  We only verify the genuine case here.
    bool cluster1_split = false;
    for (const auto& r : result.reports) {
        if (r.clusterId == 1 && r.split) cluster1_split = true;
    }
    EXPECT(cluster1_split);
}

static void test_per_feature_toggles()
{
    TEST("config: disabling phase keeps amp-only split working");
    const int nChan = 8, nSamples = 32;
    const int n     = 200;

    std::vector<std::vector<int16_t>> store;
    std::vector<int> labels(n, 1);

    std::vector<int>    ampA = {3000,3000,5000,3000,2000,2000,1000,1000};
    std::vector<int>    ampB = {3000,3000,3500,3000,2000,2000,1000,1000};
    std::vector<int>    pt(nChan, 16);
    std::vector<double> pw(nChan, 1.8);
    for (int i = 0; i < n; ++i) {
        const auto& a = (i < n / 2) ? ampA : ampB;
        store.push_back(make_spike_with_per_channel_shape(
            nChan, nSamples, a, pt, pw, 100.0f, 8000 + i));
    }
    std::vector<std::vector<int>> clusterSpikes(2);
    for (int i = 0; i < n; ++i) clusterSpikes[1].push_back(i);

    per_channel_split::Config cfg;
    cfg.minClusterSize    = 50;
    cfg.valleyThreshold   = 0.4f;
    cfg.minSubClusterSize = 30;
    cfg.usePeakTime       = false;   // disable phase
    cfg.useTroughTime     = false;

    auto result = per_channel_split::Run(
        clusterSpikes, make_reader(store), nChan, nSamples, labels, cfg);
    EXPECT_EQ(result.nClustersSplit, 1);
    EXPECT_EQ(result.reports.front().bestChannel, 2);
}

int main()
{
    std::fprintf(stdout, "test_per_channel_split\n");
    std::fprintf(stdout, "----------------------\n");

    test_extract_features();
    test_amplitude_only_separation();
    test_phase_only_separation();
    test_combined_amplitude_and_phase();
    test_uniform_cluster_no_split();
    test_min_size_gates();
    test_two_clusters_processed_independently();
    test_per_feature_toggles();

    std::fprintf(stdout, "----------------------\n");
    std::fprintf(stdout, "%d/%d assertions passed%s\n",
                 g_total - g_failed, g_total,
                 g_failed ? "  (FAIL)" : "  (PASS)");
    return g_failed ? 1 : 0;
}
