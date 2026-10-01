#ifndef OBLIQUE_EMBED_H
#define OBLIQUE_EMBED_H

// Oblique (non-orthogonal dual-basis) projection — the engine behind the
// feature view's "paired-template" alternate presentation (Shift+O).
//
// Motivation: two cells whose templates are nearly parallel (e.g. coupled
// basket cells, cos ~0.95 in raw voltage) cannot be told apart by an
// ORTHOGONAL basis — PCA hands you PC1 = the shared direction and PC2 = the
// tiny, noisy difference.  The natural parametrisation keeps the templates
// THEMSELVES as the (oblique) axes: decompose each spike x as
//      x ~= a_1 T_1 + a_2 T_2 + ... + a_K T_K
// and read off the per-cell coordinates a_k.  With B = [T_1 ... T_K] the
// least-squares coordinates are the dual (oblique) projection
//      a = (B^T B)^{-1} B^T x .
// The Gram matrix G = B^T B is near-singular exactly when the templates are
// collinear; its condition number is reported so the view can tell the curator
// when a pair is genuinely fusing (cond -> infinity) versus cleanly separable
// (cond ~ 1).  In the feature view's z-scored feature space the pair is far
// better conditioned than in raw voltage (that is why the cells separate at
// all), so the projection is done on the same z-scored features the t-SNE
// presentation already uses.
//
// Dependency-free (STL only), deterministic, no threads: the whole thing is a
// few small matrix ops plus one pass over the points, so unlike t-SNE it runs
// synchronously on the GUI thread.  Verified standalone in-sandbox (see the
// commit message): well-separated template pairs place their points at the unit
// coordinates with >99% nearest-corner purity, a collinear pair reports a large
// condition number while the orthogonal control reports ~1, and the K>2 PCA
// view reproduces the input structure.
//
// NORMALISATION IS SUPPLIED BY THE CALLER, NOT COMPUTED HERE.  The standardising
// mean/std must be GLOBAL -- over all spikes in the group, not just the selected
// clusters.  Centering over only the basis clusters forces their means to be
// near-antiparallel (for two clusters, exactly so), collapsing the Gram to rank
// one and the condition number to infinity.  The global centre is what removes
// the shared common-mode and leaves the discriminating difference, which is why
// two cells that are ~0.95-collinear in raw voltage become well-conditioned here.

#include <string>
#include <vector>

/** Project N points of dimension D (row-major, size N*D) onto the oblique basis
 *  spanned by the per-cluster mean templates of @p basisClusters, writing a 2-D
 *  scatter to @p outXY (size N*2).
 *
 *  @param data          row-major N*D features (raw; standardised with the
 *                       caller-supplied global mean/invStd below).
 *  @param N,D           point count and feature dimension (D >= 1, N >= K).
 *  @param rowCluster    size N; each point's cluster id (names a template).
 *  @param basisClusters the K >= 2 cluster ids, in display order, whose
 *                       standardised means form the basis columns.  Each must
 *                       own >= 1 point.
 *  @param featMean      size D; GLOBAL per-dimension mean (over all group spikes).
 *  @param featInvStd    size D; GLOBAL per-dimension 1/std (0 marks a dead dim,
 *                       which is then dropped from the geometry).
 *  @param outXY         size N*2 on return.  For K == 2 the axes ARE the two
 *                       cell coordinates (a_1, a_2); for K > 2 they are the two
 *                       principal axes of the K-dimensional coordinate cloud
 *                       (max-variance 2-D view of the oblique coordinates).
 *  @param condNumber    condition number of the K*K Gram matrix (lambda_max /
 *                       lambda_min): ~1 well separated, large => collinear.
 *  @param err           optional failure message.
 *  @return false (with *err set) on invalid input or an empty basis cluster. */
bool obliqueProject(const std::vector<double>& data, int N, int D,
                    const std::vector<int>& rowCluster,
                    const std::vector<int>& basisClusters,
                    const std::vector<double>& featMean,
                    const std::vector<double>& featInvStd,
                    std::vector<double>& outXY,
                    double& condNumber,
                    std::string* err = nullptr);

#endif
