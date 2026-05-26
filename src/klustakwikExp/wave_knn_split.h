// =============================================================================
// wave_knn_split.h  —  KNN majority-vote cluster split (klusters-faithful)
//
// The existing KK::KnnSplitPerChunk is NEAREST-TEMPLATE assignment: for each
// source-cluster spike, find the closest reference cluster's MEAN, assign
// to it.  This is k-means semantics, not k-NN semantics — every spike gets
// reassigned to exactly one reference, with no consensus check, so every
// source cluster fragments into up to K pieces (one per reference, modulo
// the minNewClusterSize floor).  In our benchmark run this produced 3125
// new sub-clusters from 753 source clusters (avg 4.1-way split per source),
// most of them too small (10–50 spikes) for downstream template-matching
// to behave sanely.
//
// This module implements the algorithm used by klusters' interactive
// `splitClusterByKnnVsReferences` (see src/klusters/src/data.cpp ~line 1789):
//
//   1. Build a reference POOL of well-isolated clusters' spikes (NOT means)
//   2. For each source-cluster spike, find its K nearest neighbours among
//      the pool spikes in feature space
//   3. Majority-vote the neighbours' cluster labels
//   4. If the winning fraction ≥ majorityThreshold, assign the spike to
//      that cluster; otherwise leave it in a RESIDUAL bucket (stays in
//      source)
//   5. Source-spike groups by majority-winner ID become new sub-clusters
//      of the source (NOT assignments to existing reference clusters —
//      this matches klusters which only creates new IDs)
//
// Key differences from KKE's existing KnnSplitPerChunk:
//   • per-spike K-NN against POOL of reference SPIKES (not cluster means)
//   • majority vote with threshold (not single nearest)
//   • residual bucket: low-confidence spikes are NOT reassigned, stay in
//     source — this is the mechanism that bounds fragmentation
//
// Why this matters: a source spike whose K neighbours are scattered across
// many reference clusters with no clear winner is precisely a spike that
// shouldn't be confidently reassigned.  Klusters' algorithm leaves it
// alone; KKE's nearest-template forces it to a single reference and
// thereby creates a spurious sub-cluster.
//
// Threading model: outer loop over source spikes is OMP-parallel.
// Pool KNN search is brute-force O(nSrc · nPool · nDims); for chunk-scale
// inputs (nSrc ~ 1000, nPool ~ 5000, nDims = 22) this is ~110 M ops per
// chunk → tens of milliseconds.  Future optimisation: kd-tree / HNSW.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace wave_knn_split {

// -----------------------------------------------------------------------------
// Configuration.
// -----------------------------------------------------------------------------
struct Config {
    // Explicit reference cluster IDs.  If non-empty, these are the ONLY
    // clusters whose spikes populate the K-NN reference pool, regardless
    // of trace.  Mirrors klusters' GUI semantics: user picks one source
    // cluster; all others become references.
    //
    // If empty, references are auto-selected: clusters with ≥
    // minRefClusterSize members (filtered by useTraceFilter or
    // skipMuaCluster1 as configured).
    std::vector<int> referenceIds;

    // Explicit source cluster IDs.  If non-empty, only these clusters
    // are split-tested.  If empty, source = every cluster ≥
    // minSourceClusterSize that is NOT in referenceIds.
    std::vector<int> sourceIds;

    // Number of nearest neighbours to consider in the vote.  Klusters
    // default is 10.  Smaller K → stricter (more local consensus);
    // larger K → smoother (regional consensus).
    int K = 10;

    // Fraction of the K neighbours that must share the winning label for
    // the assignment to be accepted.  Below this threshold the spike
    // stays in its source cluster (residual bucket).
    //   0.5 → simple majority
    //   0.6 → klusters' default
    //   0.7 → conservative; very few false reassignments, more residuals
    float majorityThreshold = 0.6f;

    // Minimum spikes a candidate cluster needs in the reference pool to
    // be included.  Mirrors klusters' `minRefClusterSize`.
    int minRefClusterSize = 50;

    // Minimum spikes a source cluster needs to be considered for
    // splitting.  Below this, leave it alone.
    int minSourceClusterSize = 20;

    // Minimum spikes in a winner-bucket for it to materialise as a new
    // sub-cluster.  Below this, those spikes go to residual.
    int minNewClusterSize = 30;

    // Reference-selection mode (auto-pick path only — ignored when
    // explicit referenceIds are supplied).
    //
    //   false (klusters-compat, default): no trace filter.  Every cluster
    //         ≥ minRefClusterSize is BOTH a pool member and a source
    //         candidate; each source spike's K-NN is computed against
    //         the pool WITH OWN-CLUSTER EXCLUSION (a source spike's K
    //         nearest neighbours come from clusters other than its own).
    //         Matches klusters semantics where user picks one source
    //         and everyone else is a reference, generalised to all
    //         clusters in turn.  Lets cluster A's spikes potentially
    //         merge into cluster B if their K-NN votes for B.
    //
    //   true  (KKE alternative): the auto-pick uses the chunk-median
    //         trace filter — clusters with tr(Σ)/nDim below the median
    //         become references, the rest become sources.  Pool and
    //         source sets are DISJOINT.  Cheaper (no own-cluster
    //         exclusion needed) but excludes the common case "cluster A
    //         is a mixture whose spikes K-NN-vote for cluster B" when
    //         both are well-isolated (low trace) at the median split.
    bool useTraceFilter = false;

    // Cluster-1 (klusters: MUA) policy.
    //   true  (klusters-compat, default): cluster 1 is excluded from
    //         both pool and source candidates, matching klusters' `cid
    //         ≤ 1` skip at data.cpp:1857.  Klusters reserves cluster 1
    //         for multi-unit activity; it must not participate in the
    //         K-NN vote.
    //   false (KKE alternative): cluster 1 participates normally as
    //         both a potential reference and a potential source.  Use
    //         only when your label conventions don't reserve cluster 1.
    bool skipMuaCluster1 = true;

    // Anisotropy gate on source candidates.  When > 0.0 AND a per-
    // cluster anisotropy ratio λ_max(Σ)/tr(Σ) is supplied to Run(),
    // any cluster whose anisotropy is BELOW this threshold is removed
    // from the source-candidate set (refs are unaffected).
    //
    // Rationale: a unimodal cluster has roughly isotropic residual
    // covariance, so λ_max ≈ tr/nDim → anisotropy ≈ 1/nDim (≈ 0.045
    // for nDim=22).  A mixture stretches Σ along the axis separating
    // its sub-units, pushing the leading eigenvalue well above
    // tr/nDim and anisotropy into the 0.3–0.7 range.  Gating on
    // anisotropy is therefore a mixture-detector: we only K-NN-split
    // clusters that LOOK like mixtures, sparing well-isolated units.
    //
    // Recommended default 0.10 (~ 2.2× the isotropic floor at nDim=22)
    // — high enough to spare unimodals, low enough to admit obvious
    // mixtures.  0.0 disables the gate (every cluster is a candidate,
    // as before patch 0021).
    float minSourceAnisotropy = 0.0f;

    // Noise-cluster (cid=0) inclusion probability.  Default 0.0 (noise
    // cluster never used as a source).  When > 0, before each Run()
    // call a uniform [0, 1) draw is compared against this value; on
    // hit, cluster 0 is added to the source candidate set for this run.
    // The reasoning: the noise cluster in a well-tuned pipeline holds
    // mostly genuine noise, but a fraction is misclassified real spikes
    // that should be recoverable by KNN majority-vote against the
    // well-isolated reference clusters.  Re-running periodically
    // (probability > 0) lets those spikes drift back; not every run
    // (probability < 1) avoids over-eager re-classification and
    // fragmentation.  Typical: 0.1 - 0.3.
    //
    // The noise cluster is NEVER a member of the reference pool
    // regardless of size — it's only ever a source candidate.
    float noiseSourceProbability = 0.0f;

    // RNG seed for the noise-source decision.  0 = use system time
    // (non-reproducible).  Any positive value is used directly.
    unsigned rngSeed = 0;

    // Residual handling — matches klusters' behaviour when true (default):
    //
    //   true  : Small confident winners (< minNewClusterSize) are folded
    //           into the ambiguous (no-majority) bucket.  If the resulting
    //           pool size ≥ minNewClusterSize, it materialises as a NEW
    //           cluster ID (a "residual" cluster).  Otherwise its spikes
    //           stay in source.  This is the klusters semantics from
    //           Data::splitClusterByKnnVsReferences (data.cpp:1956–1974).
    //           In batch mode this isolates ambiguous spikes from the
    //           source's confidently-kept ones, so downstream Phase 5
    //           cross-chunk matching sees the residual as its own object
    //           and doesn't contaminate the source's mean waveform.
    //
    //   false : Small winners simply revert to source (no fold); ambiguous
    //           spikes always stay in source.  No residual cluster is
    //           ever materialised.  Use this if downstream phases don't
    //           handle the extra cluster cleanly.
    bool residualBecomesNewCluster = true;

    bool Verbose = false;
};

// -----------------------------------------------------------------------------
// Result.
// -----------------------------------------------------------------------------
struct Result {
    // After-the-fact diagnostics.
    int nSourcesConsidered  = 0;
    int nSourcesAnisotropyFiltered = 0;   // dropped by the anisotropy gate
    int nSourcesSplit       = 0;   // produced ≥1 new sub-cluster (winner OR residual)
    int nNewClusters        = 0;   // total new IDs allocated (winners + residuals)
    int nResidualClusters   = 0;   // of nNewClusters, how many were residual buckets
    int nSpikesReassigned   = 0;   // spikes moved out of source to ANY new cluster
    int nSpikesResidual     = 0;   // ambiguous + small-winner spikes that stayed in source
    bool noiseClusterTried  = false;  // true if cluster 0 was a source candidate this run
};

// -----------------------------------------------------------------------------
// Run() — applies the KNN majority-vote split in-place on the supplied
// per-chunk labelling.
//
// Parameters:
//   features              [nPoints × nDims] point-major (KKE Data layout)
//   nPoints, nDims        dimensions of the feature array.  Last dim is
//                         assumed to be the normalised timestamp and
//                         excluded from the distance computation (so the
//                         distance is computed over dims [0, nDims-2)).
//   labels                [nPoints] cluster ID per spike; updated in place.
//                         0 = noise (untouched); ≥ 2 = real clusters.
//   clusterTraces         optional [labels] map of tr(Σ)/nDim per cluster
//                         ID, used by the useTraceFilter auto-pick filter.
//                         If empty AND useTraceFilter=true, the filter
//                         is skipped (all-clusters used as references).
//   clusterTraceIds       parallel array of cluster IDs for clusterTraces.
//   clusterAnisotropy     optional [labels] map of λ_max(Σ)/tr(Σ) per
//                         cluster ID, used by the source-side anisotropy
//                         gate (cfg.minSourceAnisotropy).  Parallel to
//                         clusterAnisotropyIds.  When empty OR
//                         minSourceAnisotropy ≤ 0 the gate is bypassed.
//   clusterAnisotropyIds  parallel array of cluster IDs for the
//                         anisotropy values.
//   cfg                   configuration
//
// Returns:
//   Result with diagnostics.  Labels updated in place; new cluster IDs
//   are appended starting from (max existing ID + 1).
// -----------------------------------------------------------------------------
Result Run(const float*                       features,
           int                                nPoints,
           int                                nDims,
           std::vector<int>&                  labels,
           const std::vector<float>&          clusterTraces,
           const std::vector<int>&            clusterTraceIds,
           const std::vector<float>&          clusterAnisotropy,
           const std::vector<int>&            clusterAnisotropyIds,
           const Config&                      cfg);

// Backward-compatible overload — forwards to the 9-arg form with empty
// anisotropy vectors so the gate is bypassed.  Pre-patch-0021 callers
// (including the test suite) continue to compile unchanged.
inline Result Run(const float*                features,
                  int                         nPoints,
                  int                         nDims,
                  std::vector<int>&           labels,
                  const std::vector<float>&   clusterTraces,
                  const std::vector<int>&     clusterTraceIds,
                  const Config&               cfg) {
    static const std::vector<float> kEmptyF;
    static const std::vector<int>   kEmptyI;
    return Run(features, nPoints, nDims, labels, clusterTraces,
               clusterTraceIds, kEmptyF, kEmptyI, cfg);
}

}  // namespace wave_knn_split
