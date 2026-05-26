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
    // minRefClusterSize members, AND (if referencesBelowMedianTrace=true
    // and traces are supplied) trace below the chunk-median.
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

    // Optional reference-quality filter (auto-pick mode only): only
    // clusters whose tr(Σ)/nDim is below the chunk-median trace
    // participate as references.  Helps avoid using noisy mixtures as
    // refs.  Ignored when referenceIds is non-empty.
    bool referencesBelowMedianTrace = true;

    bool Verbose = false;
};

// -----------------------------------------------------------------------------
// Result.
// -----------------------------------------------------------------------------
struct Result {
    // After-the-fact diagnostics.
    int nSourcesConsidered  = 0;
    int nSourcesSplit       = 0;   // produced ≥1 new sub-cluster
    int nNewClusters        = 0;
    int nSpikesReassigned   = 0;
    int nSpikesResidual     = 0;   // would-have-been-reassigned but below threshold
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
//                         ID, used by referencesBelowMedianTrace filter.
//                         If empty AND referencesBelowMedianTrace=true,
//                         the filter is skipped (all-clusters used).
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
           const Config&                      cfg);

}  // namespace wave_knn_split
