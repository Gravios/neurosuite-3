// =============================================================================
// cluster_hull_split.h  —  K-NN-graph connected-components cluster split
//
// Detects sub-cluster structure that the parametric splitters miss by treating
// each cluster as a point cloud and asking: is it topologically a single
// connected blob, or does it fall into multiple disconnected (or weakly-
// connected) components in feature space?
//
// Motivation
// ----------
// CEM (TrySplits) assumes Gaussian mixtures; if a cluster contains two
// distinct units whose covariances overlap in any single axis, CEM tends to
// fit one inflated Gaussian rather than two compact ones — BIC accepts the
// wider single Gaussian because the inflation penalty is smaller than the
// constant cost of a new component.  DipSplit catches one-dimensional KDE
// valleys per PC, but cannot see bimodality that's diagonal across PCs.
// Hull-based detection catches the multi-D topology directly: two units
// that occupy distinct regions of (PC1, PC2, …, PC_K) space will appear as
// two disconnected components in their mutual k-NN graph, regardless of
// whether any single PC marginal shows a valley.
//
// Algorithm (variant (a) — k-NN graph connected components)
// ---------------------------------------------------------
//   1. For each cluster spike p, find its k nearest neighbours in the
//      cluster's feature space (excluding the time dim by default — drift
//      separation is not a meaningful sub-cluster signal).
//   2. Compute d_k[p] = distance to the k-th NN of p; let m = median over
//      cluster of d_k.  m is the cluster's characteristic neighbour scale.
//   3. Build a graph with one edge per (p, q) pair where q is among p's
//      k-NN, weighted by either:
//        • raw Euclidean distance d(p,q)                 (useMutualReachability=false)
//        • mutual reachability max(d_k[p], d_k[q], d(p,q))   (useMutualReachability=true)
//      The mutual reachability metric is HDBSCAN's; it down-weights edges
//      crossing low-density regions, which is exactly the regime where two
//      sub-clusters meet.
//   4. Keep only edges with weight < mutualReachabilityScale · m.  This
//      threshold turns "everyone is connected through chains" into "only
//      points within a cluster-typical neighbour distance are connected".
//   5. Run union-find over the surviving edges → connected components.
//   6. Components smaller than minComponentSize are 'absorbed' (label 0);
//      the caller decides whether to leave them in the parent cluster or
//      reassign to nearest large component.
//   7. didSplit is true iff ≥ 2 components are above the size floor.
//
// Variants we deliberately did NOT implement here
// -----------------------------------------------
// (b) Low-D alpha shapes: catches neck-pinch geometry that (a) sees only
//     as a single component.  Requires per-cluster PCA + 3D Delaunay
//     triangulation; deferred until we have evidence (a) is insufficient.
// (c) KDE / mountain-mode detection: robust but grid-resolution-bound.
//     The k-NN approach gives adaptive resolution (dense regions → fine,
//     sparse regions → coarse) for free.
//
// Output semantics
// ----------------
//   componentLabels[p] ∈ [0, nComponents]:
//     0           → spike was in a small component, absorbed
//     1..K        → spike's component index, 1-indexed contiguous
//   didSplit = (nComponents ≥ 2)
//
// Thread safety / parallelism
// ---------------------------
// Run() processes a single cluster's data — no shared state with other
// Run() calls.  The outer caller's per-cluster loop can be OMP-parallel
// without further synchronisation.  Internally Run() OMP-parallelises the
// k-NN brute-force search across spikes; nested-parallel-disable on the
// caller side keeps the per-cluster work single-threaded if total OMP
// threads are committed elsewhere.
//
// Cost (brute-force k-NN, single cluster)
// ---------------------------------------
//   k-NN search:    O(n² · d / 16)   (AVX-512 batched)
//   union-find:     O(n · k · α(n))  (α is inverse Ackermann, ~constant)
//   memory:         O(n · k) for neighbour indices + distances
//
// For n=500 spikes (typical cluster), d=22, k=10: roughly 350k FLOPs +
// 5000 union-find ops ≈ 0.1 ms.  Cluster-of-the-day at n=5000: ~10 ms.
// Total across 36 chunks × ~50 clusters/chunk ≈ 1800 cluster-Runs, mostly
// small: a few seconds at most for a full pipeline pass.
//
// Validation hook
// ---------------
// Run() does NOT validate proposed splits via BIC or any other quality
// gate.  The caller is expected to:
//   1. Inspect didSplit / nComponents,
//   2. If accepting the split, materialise sub-clusters at the chunk
//      level (assign new cluster IDs to components 2..K),
//   3. Optionally run a short CEM settle + BIC compare against the
//      unsplit baseline.  At chunk scale a CEM settle of 3-5 iters is
//      cheap and catches the few false positives the topology test
//      produces on cluster shapes with extreme aspect ratios.
// =============================================================================

#ifndef CLUSTER_HULL_SPLIT_H
#define CLUSTER_HULL_SPLIT_H

#include <vector>

namespace cluster_hull_split {

struct Config {
    // k for k-NN search.  10 is the same default as wave_knn_split and is
    // robust across cluster sizes.  Smaller k makes the graph more
    // fragmented (catches finer sub-structure but more false positives);
    // larger k makes it more connected (misses subtle structure).
    int   k                       = 10;

    // Components with fewer than this many spikes are absorbed (label 0).
    // Matches the DipSplitMinSize default; consistent with the rule that
    // a sub-cluster smaller than this is not statistically distinguishable
    // from noise plus a few stray contamination spikes.
    int   minComponentSize        = 50;

    // Edge cutoff: keep edge (p,q) iff its weight < scale · median(d_k).
    // 1.5 is permissive (graph stays well-connected, true splits stand
    // out); 1.0 is strict (more candidate splits); 2.0 is very permissive
    // (only the most disconnected components separate).  Tune downward if
    // the test set shows missed splits, upward if it shows false splits.
    float mutualReachabilityScale = 1.5f;

    // Whether to exclude the last dimension (time) from the distance
    // metric.  Default true: drift separates spikes in the time dim
    // without indicating distinct units, so including it inflates intra-
    // cluster distances and breaks single units into multiple temporal
    // components.  Set to false only if calling on data that does not
    // include a time dim.
    bool  excludeTimeDim          = true;

    // Edge weight metric.  See header commentary.  Mutual reachability is
    // the principled choice (HDBSCAN's metric); raw distance is faster
    // and adequate when clusters are similarly sized.
    bool  useMutualReachability   = true;
};

struct Result {
    // 0 if no large components were found (cluster is too small or all
    // components fell below minComponentSize); otherwise the number of
    // large components.  nComponents ≥ 2 ⇔ didSplit == true.
    int               nComponents          = 0;

    // Largest component's spike count.  Useful for the caller's decision
    // about whether to accept the split (if one component holds 99% of
    // spikes, the proposed split is probably spurious).
    int               largestComponentSize = 0;

    // Per-spike component label (1-indexed).  0 means "absorbed into a
    // small component, no component label assigned" — caller decides what
    // to do with these (typical: leave in parent cluster).
    std::vector<int>  componentLabels;

    // Convenience flag: nComponents ≥ 2.
    bool              didSplit             = false;
};

// Run k-NN-graph connected-component split detection on one cluster.
//
// Inputs:
//   data       — pointer to nPoints × nDims point-major float array, the
//                feature vectors of the cluster's spikes (already
//                gathered by the caller — Run does not index a parent
//                array).  Stride is nDims floats per spike.
//   nPoints    — number of spikes in this cluster.
//   nDims      — number of feature dimensions in `data`.  If
//                cfg.excludeTimeDim, the last dim is dropped from the
//                distance calculation (but data layout is unchanged).
//   cfg        — see Config above.
//
// Output:
//   Result with componentLabels.size() == nPoints.
//
// Edge cases:
//   nPoints < 2 · minComponentSize  →  didSplit = false, single label.
//   k ≥ nPoints                    →  k clamped to nPoints - 1.
//   nDims ≤ 0 or data == nullptr   →  empty Result, didSplit = false.
Result Run(const float* data, int nPoints, int nDims, const Config& cfg);

}  // namespace cluster_hull_split

#endif  // CLUSTER_HULL_SPLIT_H
