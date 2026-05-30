// =============================================================================
// test_cluster_hull_split.cpp  —  unit tests for k-NN-graph cluster split
// =============================================================================
#include "../src/cluster_hull_split.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <vector>

namespace {

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

using cluster_hull_split::Config;
using cluster_hull_split::Result;
using cluster_hull_split::Run;

// ─── Data generators ─────────────────────────────────────────────────────────

// Append nPoints samples from N(mean, sigma²·I) to `out` (row-major).
void gen_gaussian(std::vector<float>& out, int nPoints, int nDims,
                  const std::vector<float>& mean, float sigma,
                  std::mt19937& rng) {
    std::normal_distribution<float> nrm(0.0f, sigma);
    for (int i = 0; i < nPoints; ++i) {
        for (int d = 0; d < nDims; ++d) {
            out.push_back(mean[static_cast<size_t>(d)] + nrm(rng));
        }
    }
}

// Append a uniform "bridge" of nPoints between mean_a and mean_b, with
// small Gaussian noise.  Used to test peanut topology.
void gen_bridge(std::vector<float>& out, int nPoints, int nDims,
                const std::vector<float>& meanA, const std::vector<float>& meanB,
                float sigma, std::mt19937& rng) {
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    std::normal_distribution<float>       nrm(0.0f, sigma);
    for (int i = 0; i < nPoints; ++i) {
        const float t = uni(rng);
        for (int d = 0; d < nDims; ++d) {
            const float interp =
                (1.0f - t) * meanA[static_cast<size_t>(d)] +
                t        * meanB[static_cast<size_t>(d)];
            out.push_back(interp + nrm(rng));
        }
    }
}

// Count spikes per label in a componentLabels vector.  Returns sorted-by-
// size-descending counts (excluding label 0).
std::vector<int> label_sizes(const std::vector<int>& labels, int nComponents) {
    std::vector<int> sizes(static_cast<size_t>(nComponents + 1), 0);
    for (int lbl : labels) {
        if (lbl >= 0 && lbl <= nComponents)
            ++sizes[static_cast<size_t>(lbl)];
    }
    std::vector<int> nonZero(sizes.begin() + 1, sizes.end());
    std::sort(nonZero.rbegin(), nonZero.rend());
    return nonZero;
}

// Count how many spikes have label 0 (absorbed).
int count_absorbed(const std::vector<int>& labels) {
    return static_cast<int>(std::count(labels.begin(), labels.end(), 0));
}

// ─── Tests ───────────────────────────────────────────────────────────────────

// T1: degenerate inputs return clean defaults.
void test_trivial() {
    std::puts("T1: trivial / degenerate inputs");

    // Empty.
    {
        Config cfg;
        Result r = Run(nullptr, 0, 5, cfg);
        CHECK(r.componentLabels.empty(), "empty input → empty labels");
        CHECK(!r.didSplit,                "empty input → no split");
        CHECK(r.nComponents == 0,         "empty input → 0 components");
    }

    // Too few points (< 2·minComponentSize).
    {
        std::vector<float> data(50 * 5, 0.0f);  // 50 points, minSize=50 → 50 < 100
        Config cfg;
        cfg.minComponentSize = 50;
        Result r = Run(data.data(), 50, 5, cfg);
        CHECK(r.componentLabels.size() == 50, "too-few → labels sized to nPoints");
        CHECK(r.nComponents == 1,             "too-few → single component");
        CHECK(!r.didSplit,                    "too-few → no split");
        CHECK(std::all_of(r.componentLabels.begin(), r.componentLabels.end(),
                          [](int x) { return x == 1; }),
              "too-few → all labels = 1");
    }

    // nDims = 1 with excludeTimeDim=true (dEff = 0) → no split.
    {
        std::vector<float> data(200, 0.0f);
        Config cfg;
        cfg.excludeTimeDim   = true;
        cfg.minComponentSize = 50;
        Result r = Run(data.data(), 200, 1, cfg);
        CHECK(r.nComponents == 1, "dEff=0 → single component");
        CHECK(!r.didSplit,        "dEff=0 → no split");
    }

    // Null data with positive nPoints.
    {
        Config cfg;
        Result r = Run(nullptr, 100, 5, cfg);
        CHECK(r.componentLabels.empty(), "null data → empty result");
    }
}

// T2: a single compact Gaussian blob.  Should NOT split.
void test_single_blob() {
    std::puts("T2: single Gaussian blob");
    std::mt19937 rng(42);

    std::vector<float> data;
    data.reserve(static_cast<size_t>(500 * 6));
    gen_gaussian(data, 500, 6, std::vector<float>(6, 0.0f), 1.0f, rng);

    Config cfg;
    cfg.minComponentSize  = 50;
    cfg.k                 = 10;
    cfg.excludeTimeDim    = false;  // all 6 dims spatial for this test
    Result r = Run(data.data(), 500, 6, cfg);

    CHECK(!r.didSplit,          "single blob → no split");
    CHECK(r.nComponents == 1,   "single blob → 1 component");
    // Gaussian tails: outermost spikes have d_k > median, so a small
    // fraction (typically 3-6%) lands in absorbed sub-components rather
    // than the main blob.  Accept up to 10%.  In real KKE usage these
    // absorbed spikes stay in the parent cluster (caller decision).
    CHECK(r.largestComponentSize >= static_cast<int>(500 * 0.90f),
          "single blob → main component holds ≥90% of spikes");
    CHECK(count_absorbed(r.componentLabels) <= 50,
          "single blob → ≤10% absorbed (Gaussian tail outliers)");
}

// T3: two well-separated Gaussian blobs.  Should split into 2 components.
void test_two_blobs_separated() {
    std::puts("T3: two well-separated Gaussian blobs");
    std::mt19937 rng(42);

    std::vector<float> meanA(6, 0.0f);
    std::vector<float> meanB(6, 0.0f);
    meanB[0] = 10.0f;  // 10σ separation in dim 0

    std::vector<float> data;
    data.reserve(static_cast<size_t>(1000 * 6));
    gen_gaussian(data, 500, 6, meanA, 1.0f, rng);
    gen_gaussian(data, 500, 6, meanB, 1.0f, rng);

    Config cfg;
    cfg.minComponentSize  = 50;
    cfg.k                 = 10;
    cfg.excludeTimeDim    = false;
    Result r = Run(data.data(), 1000, 6, cfg);

    CHECK(r.didSplit,         "two separated blobs → split");
    CHECK(r.nComponents == 2, "two separated blobs → 2 components");

    auto sizes = label_sizes(r.componentLabels, r.nComponents);
    CHECK(sizes.size() == 2 && sizes[0] >= 400 && sizes[1] >= 400,
          "two-blob component sizes balanced (~500 each)");
}

// T4: three blobs at different separations.
void test_three_blobs() {
    std::puts("T4: three blobs (well-separated)");
    std::mt19937 rng(43);

    std::vector<float> meanA(6, 0.0f);
    std::vector<float> meanB(6, 0.0f); meanB[0] = 12.0f;
    std::vector<float> meanC(6, 0.0f); meanC[0] =  6.0f; meanC[1] = 10.0f;

    std::vector<float> data;
    data.reserve(static_cast<size_t>(1500 * 6));
    gen_gaussian(data, 500, 6, meanA, 1.0f, rng);
    gen_gaussian(data, 500, 6, meanB, 1.0f, rng);
    gen_gaussian(data, 500, 6, meanC, 1.0f, rng);

    Config cfg;
    cfg.minComponentSize  = 50;
    cfg.k                 = 10;
    cfg.excludeTimeDim    = false;
    Result r = Run(data.data(), 1500, 6, cfg);

    CHECK(r.didSplit,         "three blobs → split");
    CHECK(r.nComponents == 3, "three blobs → 3 components");

    auto sizes = label_sizes(r.componentLabels, r.nComponents);
    CHECK(sizes.size() == 3 && sizes[0] >= 400 && sizes[2] >= 400,
          "three-blob sizes all ~500");
}

// T5: two blobs connected by a dense bridge — k-NN graph should merge them.
// This is the case (a) deliberately MISSES; alpha-shape variant (b) catches.
void test_peanut_topology_merged() {
    std::puts("T5: peanut topology with dense bridge (expect merge)");
    std::mt19937 rng(44);

    std::vector<float> meanA(6, 0.0f);
    std::vector<float> meanB(6, 0.0f); meanB[0] = 8.0f;

    std::vector<float> data;
    data.reserve(static_cast<size_t>(1100 * 6));
    gen_gaussian(data, 500, 6, meanA, 1.0f, rng);
    gen_bridge  (data, 100, 6, meanA, meanB, 0.5f, rng);
    gen_gaussian(data, 500, 6, meanB, 1.0f, rng);

    Config cfg;
    cfg.minComponentSize  = 50;
    cfg.k                 = 10;
    cfg.excludeTimeDim    = false;
    Result r = Run(data.data(), 1100, 6, cfg);

    // Bridge of 100 points spans 8 units in dim 0, so ~12.5 points/unit.
    // With sigma=0.5 each bridge point sees neighbours within ~1.5 unit
    // radius; the chain stays connected, the cluster reads as 1 component.
    CHECK(!r.didSplit,         "peanut + dense bridge → no split (case (a) limitation)");
    CHECK(r.nComponents == 1,  "peanut + bridge → 1 component");
}

// T6: two blobs connected by a SPARSE bridge.  Mutual reachability should
// recognise the low-density bridge and separate the components.
void test_peanut_topology_sparse_bridge() {
    std::puts("T6: peanut topology with sparse bridge (expect split via mutual-reachability)");
    std::mt19937 rng(45);

    std::vector<float> meanA(6, 0.0f);
    std::vector<float> meanB(6, 0.0f); meanB[0] = 12.0f;

    std::vector<float> data;
    data.reserve(static_cast<size_t>(1015 * 6));
    gen_gaussian(data, 500, 6, meanA, 1.0f, rng);
    gen_bridge  (data,  15, 6, meanA, meanB, 0.3f, rng);  // very sparse
    gen_gaussian(data, 500, 6, meanB, 1.0f, rng);

    Config cfg;
    cfg.minComponentSize     = 50;
    cfg.k                    = 10;
    cfg.excludeTimeDim       = false;
    cfg.useMutualReachability = true;
    Result r = Run(data.data(), 1015, 6, cfg);

    // 15 points over 12 units = 0.8 points/unit.  Bridge points have very
    // high d_k (their k-NN are far away), so mutual-reachability edges
    // through the bridge exceed threshold and the components separate.
    CHECK(r.didSplit,         "peanut + sparse bridge → split via mutual-reachability");
    CHECK(r.nComponents == 2, "peanut + sparse bridge → 2 large components");

    // The 15 bridge points themselves form a too-small component and are
    // absorbed (label 0).
    CHECK(count_absorbed(r.componentLabels) >= 5,
          "sparse bridge points absorbed (label 0)");
}

// T7: elongated single cluster (high aspect ratio).  Should NOT split.
// This tests robustness against drift-trail clusters where one dim has
// much larger variance than others.
void test_elongated_single() {
    std::puts("T7: elongated single cluster (high aspect ratio)");
    std::mt19937 rng(46);
    std::normal_distribution<float> nrm(0.0f, 1.0f);

    std::vector<float> data;
    data.reserve(static_cast<size_t>(800 * 6));
    // Aspect ratio: dim 0 sigma=15, all others sigma=1.  Spread along
    // dim 0 by a uniform distribution would look like a thin tube.  Use
    // sigma=15 Gaussian for a single elongated blob.
    std::normal_distribution<float> tall(0.0f, 15.0f);
    std::normal_distribution<float> wide(0.0f, 1.0f);
    for (int i = 0; i < 800; ++i) {
        data.push_back(tall(rng));
        for (int d = 1; d < 6; ++d) data.push_back(wide(rng));
    }

    Config cfg;
    cfg.minComponentSize  = 50;
    cfg.k                 = 10;
    cfg.excludeTimeDim    = false;
    Result r = Run(data.data(), 800, 6, cfg);

    // The blob is single — no real bimodality.  Density-wise it's
    // continuous along dim 0.  k-NN graph stays connected.
    CHECK(!r.didSplit,         "elongated single → no split");
    CHECK(r.nComponents == 1,  "elongated single → 1 component");
}

// T8: excludeTimeDim correctly drops the time dim.
// Construct: two clusters identical in first 5 dims but differing in dim 5
// (time).  With excludeTimeDim=true, should see 1 component (no spatial
// separation).  With excludeTimeDim=false, should see 2 components.
void test_exclude_time_dim() {
    std::puts("T8: excludeTimeDim drops the time dim from distance");
    std::mt19937 rng(47);

    std::vector<float> meanA(6, 0.0f);                // time = 0
    std::vector<float> meanB(6, 0.0f); meanB[5] = 30.0f;  // time = 30, same spatial
    std::vector<float> data;
    data.reserve(static_cast<size_t>(1000 * 6));
    gen_gaussian(data, 500, 6, meanA, 1.0f, rng);
    gen_gaussian(data, 500, 6, meanB, 1.0f, rng);

    // With time excluded: same spatial position → 1 component
    {
        Config cfg;
        cfg.minComponentSize  = 50;
        cfg.k                 = 10;
        cfg.excludeTimeDim    = true;
        Result r = Run(data.data(), 1000, 6, cfg);
        CHECK(!r.didSplit,          "time excluded → no split (spatial identical)");
        CHECK(r.nComponents == 1,   "time excluded → 1 component");
    }

    // With time included: clusters separate in time → 2 components
    {
        Config cfg;
        cfg.minComponentSize  = 50;
        cfg.k                 = 10;
        cfg.excludeTimeDim    = false;
        Result r = Run(data.data(), 1000, 6, cfg);
        CHECK(r.didSplit,           "time included → split (large time separation)");
        CHECK(r.nComponents == 2,   "time included → 2 components");
    }
}

// T9: many small components below minComponentSize get absorbed.
// 500 spikes scattered in 10 tiny well-separated blobs of 50 each.  With
// minComponentSize=100, all 10 components fall below the floor → all
// labels are 0 (absorbed).
void test_many_small_components_absorbed() {
    std::puts("T9: many small components → all absorbed");
    std::mt19937 rng(48);

    std::vector<float> data;
    data.reserve(static_cast<size_t>(500 * 6));
    for (int c = 0; c < 10; ++c) {
        std::vector<float> mean(6, 0.0f);
        mean[0] = static_cast<float>(c) * 20.0f;  // 20σ apart
        gen_gaussian(data, 50, 6, mean, 1.0f, rng);
    }

    Config cfg;
    cfg.minComponentSize  = 100;  // each component (size=50) is below
    cfg.k                 = 10;
    cfg.excludeTimeDim    = false;
    Result r = Run(data.data(), 500, 6, cfg);

    CHECK(!r.didSplit,         "all components small → no split");
    CHECK(r.nComponents == 0,  "all components small → 0 large components");
    CHECK(count_absorbed(r.componentLabels) == 500,
          "all 500 spikes absorbed (label 0)");
}

// T10: deterministic output — running twice gives identical labels.
void test_deterministic() {
    std::puts("T10: deterministic output across runs");
    std::mt19937 rng(49);

    std::vector<float> meanA(5, 0.0f);
    std::vector<float> meanB(5, 0.0f); meanB[0] = 8.0f;
    std::vector<float> data;
    data.reserve(static_cast<size_t>(1000 * 5));
    gen_gaussian(data, 500, 5, meanA, 1.0f, rng);
    gen_gaussian(data, 500, 5, meanB, 1.0f, rng);

    Config cfg;
    cfg.minComponentSize  = 50;
    cfg.excludeTimeDim    = false;

    Result r1 = Run(data.data(), 1000, 5, cfg);
    Result r2 = Run(data.data(), 1000, 5, cfg);

    CHECK(r1.nComponents == r2.nComponents,           "deterministic: nComponents");
    CHECK(r1.didSplit    == r2.didSplit,              "deterministic: didSplit");
    CHECK(r1.componentLabels == r2.componentLabels,   "deterministic: per-spike labels");
}

// T11: raw distance vs. mutual reachability — both should split well-
// separated blobs, but on the sparse-bridge case mutual reachability
// separates more cleanly.
void test_distance_metric_alternatives() {
    std::puts("T11: raw distance vs mutual reachability on well-separated blobs");
    std::mt19937 rng(50);

    std::vector<float> meanA(5, 0.0f);
    std::vector<float> meanB(5, 0.0f); meanB[0] = 10.0f;
    std::vector<float> data;
    data.reserve(static_cast<size_t>(1000 * 5));
    gen_gaussian(data, 500, 5, meanA, 1.0f, rng);
    gen_gaussian(data, 500, 5, meanB, 1.0f, rng);

    Config cfgMR;
    cfgMR.minComponentSize      = 50;
    cfgMR.excludeTimeDim        = false;
    cfgMR.useMutualReachability = true;
    Result rMR = Run(data.data(), 1000, 5, cfgMR);

    Config cfgRaw = cfgMR;
    cfgRaw.useMutualReachability = false;
    Result rRaw = Run(data.data(), 1000, 5, cfgRaw);

    CHECK(rMR.didSplit  && rMR.nComponents  == 2, "mutual-reachability splits cleanly");
    CHECK(rRaw.didSplit && rRaw.nComponents == 2, "raw distance also splits");
}

}  // anonymous namespace

int main() {
    std::printf("\n========== cluster_hull_split tests ==========\n\n");

    test_trivial();
    test_single_blob();
    test_two_blobs_separated();
    test_three_blobs();
    test_peanut_topology_merged();
    test_peanut_topology_sparse_bridge();
    test_elongated_single();
    test_exclude_time_dim();
    test_many_small_components_absorbed();
    test_deterministic();
    test_distance_metric_alternatives();

    std::printf("\n========== SUMMARY ==========\n");
    std::printf("  %sPASS%s: %d\n", GREEN, RESET, g_pass);
    std::printf("  %sFAIL%s: %d\n", RED,   RESET, g_fail);
    std::printf("  Total: %d\n\n",  g_pass + g_fail);

    return (g_fail == 0) ? 0 : 1;
}
