// =============================================================================
// test_wave_knn_split.cpp  —  unit tests for KNN majority-vote split
// =============================================================================
#include "../wave_knn_split.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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

}  // anonymous

// -----------------------------------------------------------------------------
// T1: trivial / empty inputs
// -----------------------------------------------------------------------------
static void test_trivial() {
    std::puts("T1: trivial inputs");
    std::vector<int> labels;
    std::vector<float> traces;
    std::vector<int> traceIds;
    wave_knn_split::Config cfg;
    auto r = wave_knn_split::Run(nullptr, 0, 0, labels, traces, traceIds, cfg);
    CHECK(r.nSourcesConsidered == 0, "empty input → no sources");
    CHECK(r.nNewClusters == 0, "empty input → no new clusters");
}

// -----------------------------------------------------------------------------
// T2: two well-separated reference clusters, one mixed source.  The
//     source's spikes should be confidently assigned to whichever
//     reference they're closer to, generating 2 new sub-clusters.
// -----------------------------------------------------------------------------
static void test_two_refs_mixed_source() {
    std::puts("T2: two refs, mixed source splits into 2");

    std::mt19937 rng(42);
    std::normal_distribution<float> nz(0.0f, 0.5f);

    // Three feature dims: dim 0 + dim 1 are "real" features, dim 2 is time
    const int nDims = 3;
    const int nPerCluster = 200;
    std::vector<float> feat;
    std::vector<int>   labels;

    auto addCluster = [&](int label, float cx, float cy) {
        for (int i = 0; i < nPerCluster; ++i) {
            feat.push_back(cx + nz(rng));
            feat.push_back(cy + nz(rng));
            feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
            labels.push_back(label);
        }
    };

    // Two reference clusters at (0,0) and (10,0)
    addCluster(2, 0.0f, 0.0f);
    addCluster(3, 10.0f, 0.0f);

    // "Source" cluster: 200 spikes evenly mixed at (0,0)-ish and (10,0)-ish
    // i.e., should split into 2 sub-clusters when KNN-voted.
    int nA = 0, nB = 0;
    for (int i = 0; i < 200; ++i) {
        const bool nearA = (i % 2 == 0);
        feat.push_back((nearA ? 0.0f : 10.0f) + nz(rng));
        feat.push_back(nz(rng));
        feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
        labels.push_back(4);
        if (nearA) ++nA; else ++nB;
    }

    const int nPoints = static_cast<int>(labels.size());
    std::vector<float> traces;
    std::vector<int>   traceIds;

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.6f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 20;
    cfg.minNewClusterSize = 30;
    cfg.referenceIds = {2, 3};   // explicit refs
    cfg.sourceIds    = {4};      // explicit source
    cfg.Verbose = false;

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.nSourcesConsidered == 1, "source 4 considered");
    CHECK(r.nSourcesSplit == 1, "source 4 split");
    CHECK(r.nNewClusters == 2, "exactly 2 new sub-clusters");
    CHECK(r.nSpikesReassigned >= 180, "≥90%% of source spikes reassigned");

    // Verify source spikes near A got the same new label, distinct from
    // those near B.  Find the two new label IDs.
    std::vector<int> newLabels;
    for (int i = 2 * nPerCluster; i < nPoints; ++i) {
        if (labels[i] != 4) newLabels.push_back(labels[i]);
    }
    std::sort(newLabels.begin(), newLabels.end());
    newLabels.erase(std::unique(newLabels.begin(), newLabels.end()), newLabels.end());
    CHECK(newLabels.size() == 2, "exactly 2 distinct new IDs in labels");
}

// -----------------------------------------------------------------------------
// T3: source whose spikes are exactly equidistant from two references —
//     should produce 0 new clusters (no confident majority).  This is the
//     key property that KKE's existing nearest-template version VIOLATES
//     (it forces every spike to a winner).
// -----------------------------------------------------------------------------
static void test_low_confidence_no_split() {
    std::puts("T3: low-confidence source produces NO new clusters");

    std::mt19937 rng(7);
    std::normal_distribution<float> refNz(0.0f, 0.5f);   // ref spread
    std::normal_distribution<float> srcNz(0.0f, 0.05f);  // source: VERY tight

    const int nDims = 3;
    std::vector<float> feat;
    std::vector<int>   labels;

    auto addRefCluster = [&](int label, float cx, float cy, int n) {
        for (int i = 0; i < n; ++i) {
            feat.push_back(cx + refNz(rng));
            feat.push_back(cy + refNz(rng));
            feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
            labels.push_back(label);
        }
    };

    // TWO references on the x-axis
    addRefCluster(2,  0.0f, 0.0f, 300);
    addRefCluster(3, 10.0f, 0.0f, 300);

    // Source: 200 spikes clustered TIGHTLY at (5, 0) — exact midpoint.
    // Each source spike's K-NN will be ~5/5 from the two refs (by
    // symmetry), and with strict majority threshold 0.8 no winner
    // emerges, so all spikes remain in source.
    for (int i = 0; i < 200; ++i) {
        feat.push_back(5.0f + srcNz(rng));
        feat.push_back(0.0f + srcNz(rng));
        feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
        labels.push_back(5);
    }

    const int nPoints = static_cast<int>(labels.size());
    std::vector<float> traces;
    std::vector<int>   traceIds;

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.8f;  // strict — needs 8/10 votes
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 20;
    cfg.minNewClusterSize = 30;
    cfg.referenceIds = {2, 3};
    cfg.sourceIds    = {5};

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.nSourcesConsidered == 1, "source 5 considered");
    CHECK(r.nNewClusters == 0, "NO new clusters at the midpoint (strict threshold)");
    CHECK(r.nSpikesReassigned == 0, "no spikes reassigned at midpoint");
    int still5 = 0;
    for (int i = 600; i < nPoints; ++i) {
        if (labels[i] == 5) ++still5;
    }
    CHECK(still5 == 200, "all source spikes remain in cluster 5");
}

// -----------------------------------------------------------------------------
// T4: trace filter — only references below median trace count as refs.
//     With 4 ref candidates (traces 0.1, 0.15, 0.2, 5.0) the median is
//     ~0.175, so refs with trace < 0.175 → clusters 2 and 6.  Cluster 4
//     (trace 5.0) is excluded as too noisy.
// -----------------------------------------------------------------------------
static void test_trace_filter() {
    std::puts("T4: trace filter excludes high-variance references");

    std::mt19937 rng(11);
    std::normal_distribution<float> nz(0.0f, 0.5f);

    const int nDims = 3;
    std::vector<float> feat;
    std::vector<int>   labels;

    auto addCluster = [&](int label, float cx, float cy, int n) {
        for (int i = 0; i < n; ++i) {
            feat.push_back(cx + nz(rng));
            feat.push_back(cy + nz(rng));
            feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
            labels.push_back(label);
        }
    };
    // 4 ref candidates: 3 clean + 1 noisy (cluster 4)
    addCluster(2,  0.0f,  0.0f, 200);
    addCluster(3, 10.0f,  0.0f, 200);
    addCluster(4,  5.0f,  9.0f, 200);  // tagged noisy via trace
    addCluster(6,  0.0f, 10.0f, 200);

    std::vector<float> traces  = {0.1f, 0.15f, 5.0f, 0.2f};
    std::vector<int>   traceIds = {2,    3,     4,    6};

    // Bimodal source near (0,0) and (10,0)
    for (int i = 0; i < 200; ++i) {
        feat.push_back((i % 2 == 0 ? 0.0f : 10.0f) + nz(rng));
        feat.push_back(nz(rng));
        feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
        labels.push_back(5);
    }
    const int nPoints = static_cast<int>(labels.size());

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.6f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 20;
    cfg.minNewClusterSize = 30;
    cfg.referencesBelowMedianTrace = true;  // ON; auto-pick refs by trace
    cfg.sourceIds = {5};                     // explicit source only

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.nSourcesSplit == 1, "source split using filtered refs");
    CHECK(r.nNewClusters == 2, "2 new clusters (only refs 2 and 3 picked up source's bimodal structure)");
}

int main() {
    std::puts("===============================================");
    std::puts(" wave_knn_split unit tests");
    std::puts("===============================================");
    test_trivial();
    test_two_refs_mixed_source();
    test_low_confidence_no_split();
    test_trace_filter();
    std::puts("===============================================");
    std::printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    std::puts("===============================================");
    return g_fail == 0 ? 0 : 1;
}
