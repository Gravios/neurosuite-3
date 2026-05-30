// =============================================================================
// test_wave_knn_split.cpp  —  unit tests for KNN majority-vote split
// =============================================================================
#include "../src/wave_knn_split.h"

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
// T3 (klusters-faithful default): source whose spikes are equidistant from
//     two references — ambiguous spikes pool into a RESIDUAL CLUSTER
//     (residualBecomesNewCluster=true by default).  Matches klusters'
//     Data::splitClusterByKnnVsReferences behaviour.
// -----------------------------------------------------------------------------
static void test_residual_becomes_cluster_default() {
    std::puts("T3: ambiguous spikes form a residual cluster (klusters default)");

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

    addRefCluster(2,  0.0f, 0.0f, 300);
    addRefCluster(3, 10.0f, 0.0f, 300);

    // 200 source spikes tightly at midpoint — equidistant, K-NN ~5/5,
    // no winner above majThr=0.8.  Klusters' fold turns the ambiguous
    // bucket into a single new residual cluster.
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
    cfg.majorityThreshold = 0.8f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 20;
    cfg.minNewClusterSize = 30;
    cfg.referenceIds = {2, 3};
    cfg.sourceIds    = {5};
    // residualBecomesNewCluster defaults to true (klusters semantics)

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.nSourcesConsidered == 1, "source 5 considered");
    CHECK(r.nResidualClusters == 1, "residual cluster formed");
    CHECK(r.nNewClusters == 1, "exactly 1 new cluster (the residual)");
    CHECK(r.nSpikesReassigned >= 190, "≥95% of source spikes moved to residual");
    CHECK(r.nSpikesResidual <= 10, "few spikes stayed in source");
    int still5 = 0;
    for (int i = 600; i < nPoints; ++i) {
        if (labels[i] == 5) ++still5;
    }
    CHECK(still5 <= 10, "source cluster 5 mostly emptied");
}

// -----------------------------------------------------------------------------
// T5 (residualBecomesNewCluster=false): ambiguous spikes stay in source.
//     KKE-friendly mode for callers not wanting extra cluster IDs.
// -----------------------------------------------------------------------------
static void test_residual_disabled_stays_in_source() {
    std::puts("T5: residualBecomesNewCluster=false → ambiguous spikes stay in source");

    std::mt19937 rng(7);
    std::normal_distribution<float> refNz(0.0f, 0.5f);
    std::normal_distribution<float> srcNz(0.0f, 0.05f);

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
    addRefCluster(2,  0.0f, 0.0f, 300);
    addRefCluster(3, 10.0f, 0.0f, 300);
    for (int i = 0; i < 200; ++i) {
        feat.push_back(5.0f + srcNz(rng));
        feat.push_back(0.0f + srcNz(rng));
        feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
        labels.push_back(5);
    }
    const int nPoints = static_cast<int>(labels.size());

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.8f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 20;
    cfg.minNewClusterSize = 30;
    cfg.referenceIds = {2, 3};
    cfg.sourceIds    = {5};
    cfg.residualBecomesNewCluster = false;  // KKE-compat mode

    std::vector<float> traces;
    std::vector<int>   traceIds;
    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.nResidualClusters == 0, "no residual cluster formed");
    CHECK(r.nNewClusters == 0, "no new clusters");
    CHECK(r.nSpikesReassigned == 0, "no spikes moved");
    CHECK(r.nSpikesResidual >= 190, "ambiguous spikes counted as residual-in-source");
    int still5 = 0;
    for (int i = 600; i < nPoints; ++i) {
        if (labels[i] == 5) ++still5;
    }
    CHECK(still5 >= 190, "source cluster 5 retains its spikes");
}

// -----------------------------------------------------------------------------
// T6 (small-winner fold): a small confident winner (size < minNewClusterSize)
//     is FOLDED into the residual bucket, not directly dropped to source.
//     Matches klusters' data.cpp:1960-1967 fold loop.
//
//     Source 5: 20 spikes confidently near cluster 2 (too small to be its
//     own cluster) + 180 ambiguous midpoint spikes.  Expected:
//       - 20-spike confident bucket → folded into ambiguous
//       - 180 + 20 = 200 ≥ minNewClusterSize → 1 residual cluster
//       - 0 surviving normal winners
// -----------------------------------------------------------------------------
static void test_small_winner_folds_into_residual() {
    std::puts("T6: small confident winner folds into residual, not source");

    std::mt19937 rng(31);
    std::normal_distribution<float> refNz(0.0f, 0.5f);
    std::normal_distribution<float> srcNz(0.0f, 0.05f);

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
    addRefCluster(2,  0.0f, 0.0f, 300);
    addRefCluster(3, 10.0f, 0.0f, 300);

    // Source A: 20 spikes very near cluster 2 (confident vote, small)
    for (int i = 0; i < 20; ++i) {
        feat.push_back(0.5f + srcNz(rng));
        feat.push_back(0.0f + srcNz(rng));
        feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
        labels.push_back(5);
    }
    // Source B: 180 spikes at midpoint (ambiguous)
    for (int i = 0; i < 180; ++i) {
        feat.push_back(5.0f + srcNz(rng));
        feat.push_back(0.0f + srcNz(rng));
        feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
        labels.push_back(5);
    }
    const int nPoints = static_cast<int>(labels.size());

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.8f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 20;
    cfg.minNewClusterSize = 30;
    cfg.referenceIds = {2, 3};
    cfg.sourceIds    = {5};

    std::vector<float> traces;
    std::vector<int>   traceIds;
    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.nResidualClusters == 1, "1 residual cluster (the fold target)");
    CHECK(r.nNewClusters == 1, "no surviving confident winner — all folded");
    CHECK(r.nSpikesReassigned >= 180, "≥180 spikes moved to residual");
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
    cfg.useTraceFilter = true;  // ON; auto-pick refs by trace
    cfg.sourceIds = {5};                     // explicit source only

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.nSourcesSplit == 1, "source split using filtered refs");
    CHECK(r.nNewClusters == 2, "2 new clusters (only refs 2 and 3 picked up source's bimodal structure)");
}

// -----------------------------------------------------------------------------
// T7 (klusters mode): useTraceFilter=false → every sized-OK cluster is
//     both a pool member and a source candidate, per-spike K-NN excludes
//     own-cluster pool entries.  Source clusters A and B both well-
//     populated; expect their spikes to potentially merge into the
//     opposite cluster (which they wouldn't in trace-filter mode).
// -----------------------------------------------------------------------------
static void test_klusters_mode_overlapping() {
    std::puts("T7: klusters mode (useTraceFilter=false) — pool == sources");

    std::mt19937 rng(42);
    std::normal_distribution<float> nz(0.0f, 0.5f);

    const int nDims = 3;
    std::vector<float> feat;
    std::vector<int>   labels;
    auto add = [&](int label, float cx, float cy, int n) {
        for (int i = 0; i < n; ++i) {
            feat.push_back(cx + nz(rng));
            feat.push_back(cy + nz(rng));
            feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
            labels.push_back(label);
        }
    };
    // Two well-separated clusters of equal size — in trace-filter mode
    // neither would be tagged as the "source" (no trace info supplied),
    // but in klusters mode both are pool AND source candidates.
    add(2,  0.0f, 0.0f, 200);
    add(3, 10.0f, 0.0f, 200);

    const int nPoints = static_cast<int>(labels.size());
    std::vector<float> traces;
    std::vector<int>   traceIds;

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.6f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 50;
    cfg.minNewClusterSize = 50;
    cfg.useTraceFilter = false;  // klusters mode

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    // Both clusters are now sources.  Their spikes are well-isolated
    // around their own centres, so each spike's K-NN (with own-cluster
    // excluded) finds the K nearest from the OPPOSITE cluster, far away.
    // The K-NN is unanimous for the opposite cluster — but those votes
    // are above the threshold, so spikes get reassigned.  In practice
    // with well-separated clusters this confidently REASSIGNS each
    // cluster's spikes to the OTHER cluster (a flip).
    CHECK(r.nSourcesConsidered == 2, "both clusters are source candidates");
    // The interesting property to verify: own-cluster exclusion is
    // actually happening.  If it weren't, a cluster-2 spike's K-NN
    // would be all cluster-2 (it's surrounded by its own kind), and
    // the vote would be 10/10 for cluster 2 → no reassignment.
    // With exclusion, the K-NN is 10/10 for cluster 3, → reassignment.
    CHECK(r.nSpikesReassigned > 300, "own-cluster exclusion fires reassignment");
}

// -----------------------------------------------------------------------------
// T8 (skipMuaCluster1): cluster 1 is excluded from refs AND sources
//     when skipMuaCluster1=true, matching klusters' cid≤1 skip.
// -----------------------------------------------------------------------------
static void test_skip_mua_cluster_1() {
    std::puts("T8: skipMuaCluster1=true excludes cluster 1 entirely");

    std::mt19937 rng(13);
    std::normal_distribution<float> nz(0.0f, 0.5f);

    const int nDims = 3;
    std::vector<float> feat;
    std::vector<int>   labels;
    auto add = [&](int label, float cx, float cy, int n) {
        for (int i = 0; i < n; ++i) {
            feat.push_back(cx + nz(rng));
            feat.push_back(cy + nz(rng));
            feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
            labels.push_back(label);
        }
    };
    // Cluster 1 (MUA in klusters convention) at (0, 0), 200 spikes
    add(1,  0.0f, 0.0f, 200);
    // Real clusters 2 and 3
    add(2, 10.0f, 0.0f, 200);
    add(3,  5.0f, 8.0f, 200);

    const int nPoints = static_cast<int>(labels.size());
    std::vector<float> traces;
    std::vector<int>   traceIds;

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.6f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 50;
    cfg.minNewClusterSize = 50;
    cfg.useTraceFilter = false;
    cfg.skipMuaCluster1 = true;

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    // Only clusters 2 and 3 are eligible.  Cluster 1 spikes should NEVER
    // be touched (not in source set, not in pool).
    CHECK(r.nSourcesConsidered == 2, "only 2 clusters considered (cluster 1 skipped)");
    int still1 = 0;
    for (int i = 0; i < 200; ++i) {
        if (labels[i] == 1) ++still1;
    }
    CHECK(still1 == 200, "cluster 1 spikes untouched");
}

// -----------------------------------------------------------------------------
// T9 (noise as source): with noiseSourceProbability=1.0 and a fixed
//     rngSeed, cluster 0 spikes near a real reference get reassigned.
// -----------------------------------------------------------------------------
static void test_noise_as_source() {
    std::puts("T9: noiseSourceProbability=1.0 → cluster 0 spikes get split");

    std::mt19937 rng(99);
    std::normal_distribution<float> refNz(0.0f, 0.5f);
    std::normal_distribution<float> srcNz(0.0f, 0.1f);  // tight near refs

    const int nDims = 3;
    std::vector<float> feat;
    std::vector<int>   labels;
    auto add = [&](int label, float cx, float cy, int n,
                    std::normal_distribution<float>& nz) {
        for (int i = 0; i < n; ++i) {
            feat.push_back(cx + nz(rng));
            feat.push_back(cy + nz(rng));
            feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
            labels.push_back(label);
        }
    };
    // Two real clusters
    add(2,  0.0f, 0.0f, 200, refNz);
    add(3, 10.0f, 0.0f, 200, refNz);
    // "Noise" cluster (cid=0): half is real spikes near cluster 2's
    // centre, half real near cluster 3's centre.  These were misclassified
    // and should be recoverable via KNN voting.
    add(0,  0.0f, 0.0f, 100, srcNz);   // 100 misclassified as noise but really near cluster 2
    add(0, 10.0f, 0.0f, 100, srcNz);   // 100 misclassified as noise but really near cluster 3

    const int nPoints = static_cast<int>(labels.size());
    std::vector<float> traces;
    std::vector<int>   traceIds;

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.6f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 50;
    cfg.minNewClusterSize = 50;
    cfg.useTraceFilter = false;
    cfg.referenceIds = {2, 3};
    cfg.noiseSourceProbability = 1.0f;
    cfg.rngSeed = 1;  // deterministic for the test

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(r.noiseClusterTried, "noise cluster was drawn as a source");
    CHECK(r.nSourcesConsidered >= 1, "≥1 source considered (noise)");
    CHECK(r.nNewClusters >= 2, "≥2 new clusters from noise split");
    // Count how many noise-cluster spikes still have label 0
    int still0 = 0;
    for (int i = 400; i < nPoints; ++i) {  // noise spikes are at indices 400..599
        if (labels[i] == 0) ++still0;
    }
    CHECK(still0 < 50, "most noise spikes (≥75%) recovered to real clusters");
}

// -----------------------------------------------------------------------------
// T10 (noise probability = 0): cluster 0 is never a source when the
//     probability is zero — even with mock spikes near real clusters.
// -----------------------------------------------------------------------------
static void test_noise_excluded_by_default() {
    std::puts("T10: noiseSourceProbability=0.0 → noise cluster untouched");

    std::mt19937 rng(99);
    std::normal_distribution<float> refNz(0.0f, 0.5f);
    std::normal_distribution<float> srcNz(0.0f, 0.1f);

    const int nDims = 3;
    std::vector<float> feat;
    std::vector<int>   labels;
    auto add = [&](int label, float cx, float cy, int n,
                    std::normal_distribution<float>& nz) {
        for (int i = 0; i < n; ++i) {
            feat.push_back(cx + nz(rng));
            feat.push_back(cy + nz(rng));
            feat.push_back(static_cast<float>(labels.size()) / 1000.0f);
            labels.push_back(label);
        }
    };
    add(2,  0.0f, 0.0f, 200, refNz);
    add(3, 10.0f, 0.0f, 200, refNz);
    add(0,  0.0f, 0.0f, 100, srcNz);
    add(0, 10.0f, 0.0f, 100, srcNz);

    const int nPoints = static_cast<int>(labels.size());
    std::vector<float> traces;
    std::vector<int>   traceIds;

    wave_knn_split::Config cfg;
    cfg.K = 10;
    cfg.majorityThreshold = 0.6f;
    cfg.minRefClusterSize = 50;
    cfg.minSourceClusterSize = 50;
    cfg.minNewClusterSize = 50;
    cfg.useTraceFilter = false;
    cfg.referenceIds = {2, 3};
    cfg.noiseSourceProbability = 0.0f;  // OFF

    auto r = wave_knn_split::Run(feat.data(), nPoints, nDims, labels,
                                  traces, traceIds, cfg);
    CHECK(!r.noiseClusterTried, "noise cluster NOT considered");
    int still0 = 0;
    for (int i = 400; i < nPoints; ++i) {
        if (labels[i] == 0) ++still0;
    }
    CHECK(still0 == 200, "all noise spikes remain in cluster 0");
}

int main() {
    std::puts("===============================================");
    std::puts(" wave_knn_split unit tests");
    std::puts("===============================================");
    test_trivial();
    test_two_refs_mixed_source();
    test_residual_becomes_cluster_default();
    test_trace_filter();
    test_residual_disabled_stays_in_source();
    test_small_winner_folds_into_residual();
    test_klusters_mode_overlapping();
    test_skip_mua_cluster_1();
    test_noise_as_source();
    test_noise_excluded_by_default();
    std::puts("===============================================");
    std::printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    std::puts("===============================================");
    return g_fail == 0 ? 0 : 1;
}
