// =============================================================================
// test_proxy_isi.cpp  —  unit tests for the proxy_isi module
//
// Build (from src/kiloklustakwik/):
//   c++ -std=c++20 -O2 -fopenmp -I. -o test_proxy_isi  proxy_isi.cpp test/test_proxy_isi.cpp
//
// Tests
// -----
//   T1  empty / trivial inputs                   — no crashes
//   T2  single Poisson process, well-separated   — proxy_isi == true ISI
//   T3  two interleaved Poisson processes        — proxy_isi tracks
//                                                  same-process predecessor
//   T4  causal window honoured                   — out-of-window → undefined
//   T5  ε-gate honoured                          — cross-cluster spike with
//                                                  no close neighbour → undef
//   T6  weighted variant runs and gives finite
//       output where strict would
//   T7  NaN handling                             — NaN feature dim does not
//                                                  crash; affected spike
//                                                  may still resolve via
//                                                  other neighbours
// =============================================================================
#include "../src/proxy_isi.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <random>
#include <vector>

namespace {

// Simple ANSI colours for pass/fail output (works in any modern terminal).
constexpr const char* GREEN = "\033[32m";
constexpr const char* RED   = "\033[31m";
constexpr const char* RESET = "\033[0m";

int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg) do {                                              \
    if (cond) {                                                            \
        std::printf("  %sPASS%s %s\n", GREEN, RESET, msg);                 \
        ++g_pass;                                                          \
    } else {                                                               \
        std::printf("  %sFAIL%s %s   (%s:%d)\n", RED, RESET, msg,          \
                    __FILE__, __LINE__);                                   \
        ++g_fail;                                                          \
    }                                                                      \
} while (0)

// Generate a homogeneous Poisson process with rate `lambda` Hz on
// [0, T] seconds.  Returns sorted spike times.
std::vector<double> poissonTimes(double T, double lambda, std::mt19937& rng) {
    std::exponential_distribution<double> exp(lambda);
    std::vector<double> out;
    double t = 0.0;
    while (true) {
        t += exp(rng);
        if (t >= T) break;
        out.push_back(t);
    }
    return out;
}

}  // anonymous namespace

// -----------------------------------------------------------------------------
// T1: trivial inputs
// -----------------------------------------------------------------------------
static void test_trivial() {
    std::puts("T1: trivial inputs");

    // Empty input
    {
        proxy_isi::Config cfg;
        auto r = proxy_isi::compute(nullptr, 0, 0, nullptr, cfg);
        CHECK(r.isi.empty(), "empty input → empty output");
        CHECK(r.nDefined == 0 && r.nUndefined == 0, "empty diagnostics");
    }

    // Single spike (no predecessor possible)
    {
        std::vector<float>  feat = {1.0f, 2.0f};
        std::vector<double> tim  = {0.5};
        proxy_isi::Config cfg;
        auto r = proxy_isi::compute(feat.data(), 1, 2, tim.data(), cfg);
        CHECK(r.isi.size() == 1, "single-spike output sized");
        CHECK(std::isinf(r.isi[0]), "single spike → undefined");
        CHECK(r.predecessor[0] == -1, "single spike → no predecessor");
    }
}

// -----------------------------------------------------------------------------
// T2: single Poisson process, all spikes well-separated in feature space
//     from any other "cluster" (there isn't one).  proxy_isi[i] should
//     equal t_i − t_{i-1} (the true ISI) for i ≥ 1.
// -----------------------------------------------------------------------------
static void test_single_process() {
    std::puts("T2: single Poisson process, proxy_isi == true ISI");

    std::mt19937 rng(42);
    const double T      = 100.0;
    const double lambda = 50.0;  // Hz
    auto times = poissonTimes(T, lambda, rng);
    const int N = static_cast<int>(times.size());

    // All spikes have the same feature vector (one "unit") — every
    // predecessor is at zero distance, so the most-recent in the window
    // is always the spike at index i-1.
    std::vector<float> feat(static_cast<size_t>(N) * 3, 0.0f);

    proxy_isi::Config cfg;
    cfg.K = 8;
    cfg.causalWindowSec = 5.0f;
    cfg.epsilonFactor = std::numeric_limits<float>::infinity();  // disable gate
    auto r = proxy_isi::compute(feat.data(), N, 3, times.data(), cfg);

    // Check that all but the first few (whose causal window includes nothing
    // or the very early spikes) get the true previous ISI.
    int correct = 0;
    int total   = 0;
    for (int i = 1; i < N; ++i) {
        if (!std::isfinite(r.isi[i])) continue;
        ++total;
        const float trueIsi = static_cast<float>(times[i] - times[i - 1]);
        // Tolerance: float precision over a 100 s baseline.
        if (std::fabs(r.isi[i] - trueIsi) < 1e-4f) ++correct;
    }
    CHECK(total > N / 2, "most spikes have defined proxy_isi");
    CHECK(correct == total, "all defined proxy_isi match the true previous ISI");
}

// -----------------------------------------------------------------------------
// T3: two interleaved Poisson processes (two clearly-separated "units").
//     proxy_isi[i] should reference the previous spike of the SAME unit.
// -----------------------------------------------------------------------------
static void test_two_processes() {
    std::puts("T3: two interleaved processes, proxy_isi follows same-unit");

    std::mt19937 rng(7);
    const double T = 100.0;
    auto t1 = poissonTimes(T, 30.0, rng);
    auto t2 = poissonTimes(T, 30.0, rng);

    // Mark unit identity (0 or 1) for each spike, then sort by time.
    std::vector<std::pair<double, int>> merged;
    merged.reserve(t1.size() + t2.size());
    for (double t : t1) merged.emplace_back(t, 0);
    for (double t : t2) merged.emplace_back(t, 1);
    std::sort(merged.begin(), merged.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    const int N = static_cast<int>(merged.size());
    std::vector<double> times(static_cast<size_t>(N));
    std::vector<int>    unit(static_cast<size_t>(N));
    // Feature vector: a single channel where unit 0 sits at +5, unit 1 at −5.
    // Two extra noise dims to make the space non-degenerate.
    std::vector<float>  feat(static_cast<size_t>(N) * 3);
    std::normal_distribution<float> noise(0.0f, 0.2f);
    for (int i = 0; i < N; ++i) {
        times[i] = merged[i].first;
        unit[i]  = merged[i].second;
        feat[i*3 + 0] = (unit[i] == 0 ? 5.0f : -5.0f) + noise(rng);
        feat[i*3 + 1] = noise(rng);
        feat[i*3 + 2] = noise(rng);
    }

    proxy_isi::Config cfg;
    cfg.K = 8;
    cfg.causalWindowSec = 5.0f;
    cfg.epsilonFactor = 3.0f;  // moderate tolerance — within-unit jitter
    auto r = proxy_isi::compute(feat.data(), N, 3, times.data(), cfg);

    // For each spike with defined proxy_isi, check that the predecessor's
    // unit matches.
    int sameUnitCount = 0;
    int totalDefined  = 0;
    for (int i = 0; i < N; ++i) {
        if (!std::isfinite(r.isi[i])) continue;
        ++totalDefined;
        const int pred = r.predecessor[i];
        if (pred >= 0 && unit[pred] == unit[i]) ++sameUnitCount;
    }
    CHECK(totalDefined > N / 2, "most spikes have defined proxy_isi");
    // Expect ≥95% same-unit matches: two units well-separated, ε-gate
    // discards cross-unit candidates whose distance > 2× K-th nearest
    // (which will always be a same-unit spike with the noise variance
    // we used).
    const double fracSame = static_cast<double>(sameUnitCount) / totalDefined;
    std::printf("    same-unit fraction = %.4f\n", fracSame);
    CHECK(fracSame > 0.95, "≥95%% of proxy predecessors are same-unit");
}

// -----------------------------------------------------------------------------
// T4: spikes spaced > causalWindowSec apart should have undefined proxy_isi
// -----------------------------------------------------------------------------
static void test_causal_window() {
    std::puts("T4: causal window honoured");

    // 5 spikes spaced 10 s apart, same feature vector
    const int N = 5;
    std::vector<double> times = {0.0, 10.0, 20.0, 30.0, 40.0};
    std::vector<float>  feat(static_cast<size_t>(N) * 2, 1.0f);

    proxy_isi::Config cfg;
    cfg.K = 4;
    cfg.causalWindowSec = 5.0f;  // less than the 10 s gap
    cfg.epsilonFactor = std::numeric_limits<float>::infinity();
    auto r = proxy_isi::compute(feat.data(), N, 2, times.data(), cfg);

    int undefCount = 0;
    for (int i = 0; i < N; ++i) {
        if (std::isinf(r.isi[i])) ++undefCount;
    }
    CHECK(undefCount == N, "all spikes outside each other's window → all undefined");

    // Widen the window — now they should all (except spike 0) be defined.
    cfg.causalWindowSec = 15.0f;
    auto r2 = proxy_isi::compute(feat.data(), N, 2, times.data(), cfg);
    int defCount = 0;
    for (int i = 1; i < N; ++i) {
        if (std::isfinite(r2.isi[i])) ++defCount;
    }
    CHECK(defCount == N - 1, "widened window → all-but-first defined");
}

// -----------------------------------------------------------------------------
// T5: ε-gate filters out cross-cluster candidates even if they are in
//     the causal window, provided the within-cluster K-NN distance is
//     much smaller than the cross-cluster distance.
// -----------------------------------------------------------------------------
static void test_epsilon_gate() {
    std::puts("T5: ε-gate isolates well-separated clusters");

    // Cluster A: tight cloud at (0, 0)
    // Cluster B: tight cloud at (100, 0)  — far away
    // Interleave in time, all in 1 s window.
    std::mt19937 rng(99);
    std::normal_distribution<float> nA(0.0f, 0.1f);
    std::normal_distribution<float> nB(100.0f, 0.1f);

    const int N = 100;
    std::vector<double> times(static_cast<size_t>(N));
    std::vector<float>  feat(static_cast<size_t>(N) * 2);
    std::vector<int>    unit(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) {
        times[i] = static_cast<double>(i) * 0.01;  // 100 Hz
        unit[i]  = i % 2;
        feat[i*2 + 0] = (unit[i] == 0 ? nA(rng) : nB(rng));
        feat[i*2 + 1] = (unit[i] == 0 ? nA(rng) : nB(rng));
    }

    proxy_isi::Config cfg;
    // K=5 here even though units strictly alternate: the algorithm uses
    // the *nearest* d² in the K-NN pool as the local scale, not the K-th
    // nearest, so cross-unit pollution of the K-NN pool doesn't loosen
    // the ε-gate.  As long as the closest candidate is same-unit (which
    // it always is when units are well-separated in feature space), the
    // gate stays tight.
    cfg.K = 5;
    cfg.causalWindowSec = 1.0f;
    cfg.epsilonFactor = 1.0f;  // strict — only candidates at the within-
                                // unit scale qualify
    auto r = proxy_isi::compute(feat.data(), N, 2, times.data(), cfg);

    int sameUnit = 0, totalDef = 0;
    for (int i = 0; i < N; ++i) {
        if (!std::isfinite(r.isi[i])) continue;
        ++totalDef;
        if (r.predecessor[i] >= 0 && unit[r.predecessor[i]] == unit[i]) ++sameUnit;
    }
    CHECK(totalDef > N / 4, "many spikes have defined proxy_isi");
    CHECK(sameUnit == totalDef, "100%% same-unit when clusters are very far apart");
}

// -----------------------------------------------------------------------------
// T6: weighted variant runs to completion and produces finite output
// -----------------------------------------------------------------------------
static void test_weighted_variant() {
    std::puts("T6: weighted variant produces finite output");

    std::mt19937 rng(101);
    const int N = 200;
    std::vector<double> times(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) times[i] = static_cast<double>(i) * 0.005;  // 200 Hz

    std::vector<float> feat(static_cast<size_t>(N) * 4);
    std::normal_distribution<float> noise(0.0f, 0.3f);
    for (int i = 0; i < N; ++i) {
        for (int d = 0; d < 4; ++d) feat[i*4 + d] = noise(rng);
    }

    proxy_isi::Config cfg;
    cfg.K = 10;
    cfg.causalWindowSec = 0.5f;
    cfg.epsilonFactor = 2.0f;
    cfg.WeightedVariant = true;
    auto r = proxy_isi::compute(feat.data(), N, 4, times.data(), cfg);

    int finite = 0;
    for (int i = 0; i < N; ++i) if (std::isfinite(r.isi[i])) ++finite;
    CHECK(finite > N / 2, "weighted variant produces finite output for most spikes");
    CHECK(r.medianIsi > 0.0f && r.medianIsi < 0.5f,
          "median proxy_isi within expected range");
}

// -----------------------------------------------------------------------------
// T7: NaN features do not crash; affected predecessor candidates are
//     filtered out, but other neighbours still resolve.
// -----------------------------------------------------------------------------
static void test_nan_handling() {
    std::puts("T7: NaN handling does not crash");

    const int N = 50;
    std::vector<double> times(static_cast<size_t>(N));
    std::vector<float>  feat(static_cast<size_t>(N) * 3, 0.0f);
    for (int i = 0; i < N; ++i) times[i] = static_cast<double>(i) * 0.01;

    // Inject NaN into spike 25's first feature
    feat[25 * 3 + 0] = std::numeric_limits<float>::quiet_NaN();

    proxy_isi::Config cfg;
    cfg.K = 5;
    cfg.causalWindowSec = 1.0f;
    cfg.epsilonFactor = std::numeric_limits<float>::infinity();
    auto r = proxy_isi::compute(feat.data(), N, 3, times.data(), cfg);

    // Spikes after 25 should still resolve via other neighbours (the NaN
    // spike is excluded from their candidate pool but the rest are fine).
    int defAfter = 0;
    for (int i = 26; i < N; ++i) if (std::isfinite(r.isi[i])) ++defAfter;
    CHECK(defAfter > 0, "non-NaN spikes still resolve");
}

int main() {
    std::puts("==================================================");
    std::puts(" proxy_isi unit tests");
    std::puts("==================================================");
    test_trivial();
    test_single_process();
    test_two_processes();
    test_causal_window();
    test_epsilon_gate();
    test_weighted_variant();
    test_nan_handling();
    std::puts("==================================================");
    std::printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    std::puts("==================================================");
    return g_fail == 0 ? 0 : 1;
}
