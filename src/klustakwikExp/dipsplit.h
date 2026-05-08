// =============================================================================
// dipsplit.h  —  bimodality-detection-driven cluster splitting for klustakwikExp
//
// Helpers for detecting and partitioning "flat" clusters — cases where CEM's
// single-Gaussian model masks a bimodal sub-structure because the full-dim
// joint distribution looks approximately Gaussian even though a 2D projection
// reveals a valley.
//
// Algorithm (applied by KK::DipSplitPhase in KK.cpp):
//
//   1. GATE A  (bloat):   χ²-calibrated Mahalanobis-90th-percentile test on
//                         the cluster.  Skip unless mahal²₉₀ > F · χ²(d, 0.9).
//   2. TOP PCs:           compute top-3 principal components of the cluster's
//                         spatial features via power iteration with deflation.
//   3. GATE B  (dip):     project onto each PC and run `valley_test()`.  If
//                         the deepest valley depth ≥ `valley_thresh`, we have
//                         a candidate bimodal axis.
//   4. SEED:              initialise a k=2 partition at the valley location.
//   5. REFINE:            a few iterations of k-means seeded at the valley.
//   6. BIC GATE:          accept only if BIC(k=2) < BIC(k=1) AND both halves
//                         pass min-size.
//
// The first two gates are cheap.  The expensive steps (k-means + BIC) only
// run on flagged candidates, so steady-state cost scales with the number of
// actually-bimodal clusters, not the cluster count.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace dipsplit {

// -----------------------------------------------------------------------------
// valley_test — KDE-based bimodality detector
//
// Projects the 1D sample array onto a grid, computes a Gaussian KDE with
// Silverman-rule bandwidth, and measures the deepest valley between adjacent
// peaks.
//
// The "depth" is defined as:
//      depth = 1 − (valley_density / min(left_peak, right_peak))
//
// depth = 0.0  → perfectly unimodal (no valley)
// depth = 0.5  → valley is half the height of the shorter peak
// depth = 1.0  → complete separation
//
// Cost: O(n log n) sort + O(G) KDE grid (G≈300).  For n=5000 ≈ 0.5 ms.
// -----------------------------------------------------------------------------
struct ValleyResult {
    bool   is_bimodal   = false;   // depth >= threshold
    double depth        = 0.0;     // 0..1  (see above)
    double valley_loc   = 0.0;     // position along axis where valley sits
    double left_peak    = 0.0;     // mode positions on each side of the valley
    double right_peak   = 0.0;
};

ValleyResult valley_test(const double* samples, int n, double threshold);


// -----------------------------------------------------------------------------
// top_pcs_with_eigenvalues — top-k principal components via power iteration
// with Gram-Schmidt deflation on the d×d covariance matrix, plus the
// eigenvalue (Rayleigh quotient λ = uᵀ C u, which equals the variance of
// the cluster's data along that principal direction) for each PC.
//
// X is row-major nPoints × d; output PCs are row-major k × d (each row a PC).
// Centres X internally.  O(n·d²) for cov build + O(k·d²·iters) for iteration
// + k·d² for the Rayleigh quotient — negligible.
//
// Used by the elongation gate in KK::DipSplitAttemptEx — a cluster whose top
// eigenvalue dominates its second/third eigenvalue by a wide margin is the
// classic signature of two sub-modes well-separated along one axis but fitted
// as a single inflated Gaussian by CEM.  This catches the failure mode that
// the χ²-based bloat gate misses (when CEM's covariance has been so inflated
// that mahal²₉₀ stays near the unimodal Gaussian expectation).
// -----------------------------------------------------------------------------
void top_pcs_with_eigenvalues(
    const float* X, int nPoints, int d, int k,
    double* pcs_out,            // [k * d]   (each row: a PC)
    double* eigs_out,           // [k]       (variance along each PC)
    int max_iters = 50,
    double tol    = 1e-6);

// -----------------------------------------------------------------------------
// kmeans2_refine — single-pass k=2 k-means refinement seeded at init centroids.
// Writes labels ∈ {0,1} into `labels_out`.  Returns number of iterations until
// convergence (labels unchanged).
// -----------------------------------------------------------------------------
int kmeans2_refine(
    const float* X, int nPoints, int d,
    const double* init_centroid0,   // [d]
    const double* init_centroid1,   // [d]
    int*   labels_out,              // [nPoints]
    int    max_iters = 20);

// -----------------------------------------------------------------------------
// bic_two_vs_one — returns (BIC_1cluster, BIC_2cluster) for the given point
// set under the diagonal-covariance Gaussian assumption.  Caller compares.
// -----------------------------------------------------------------------------
struct BicPair {
    double bic_k1 = 0.0;
    double bic_k2 = 0.0;
};
BicPair bic_two_vs_one(
    const float* X, int nPoints, int d,
    const int* labels_k2);

}  // namespace dipsplit
