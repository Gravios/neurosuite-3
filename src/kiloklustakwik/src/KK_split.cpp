/***************************************************************************
                   KK_split.cpp
                   ------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Per-chunk cluster-split strategies for the KK class (dip test, convex hull,
 per-channel, refractory, kNN / waveform-kNN, quality-weighted dispatch, and
 the full-CEM split), split out of KK.cpp.  Carries the split-only file-local
 helper percentile_sorted (used by DipSplitAttemptEx).  Self-contained: none
 of these use the shared numerical kernels in KK_vbgmm.cpp.  Member
 definitions of the KK class declared in KK.h; no interface change.
 ***************************************************************************/
#include "KK.h"
#include "KlustaKwik.h"
#include "dipsplit.h"
#include "cluster_hull_split.h"
#include "per_channel_split.h"
#include "wave_knn_split.h"
#include "proxy_isi.h"
#include "clust_quality.h"
#include "realign_xcorr.h"
#include "realign_center.h"
#include "klusters_realign.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <omp.h>
#include <atomic>


// Helper: nth-percentile of a small double vector (sort-based, O(n log n)).
// For the bloat gate we call this once per alive cluster; the sort dominates.
static double percentile_sorted(std::vector<double>& v, double q)
{
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const double idx = q * (v.size() - 1);
    const int    lo  = static_cast<int>(std::floor(idx));
    const int    hi  = static_cast<int>(std::ceil (idx));
    const double f   = idx - lo;
    return v[lo] * (1.0 - f) + v[hi] * f;
}



// ---------------------------------------------------------------------------
// DipSplitAttemptEx — try to split a single cluster.
//
// Returns true when the cluster was split and Class[] updated.  Caller is
// responsible for follow-up MStep+EStep to refresh cluster stats.
//
// The `reason_out` parameter receives a tag describing what happened: one
// of "split", "small", "not_bloated", "no_valley", "small_child",
// "bic_worse", "no_free_id".  Used by DipSplitPhase for summary logging.
// ---------------------------------------------------------------------------
bool KK::DipSplitAttemptEx(int clusterId, const char*& reason_out)
{
    reason_out = "skip";
    if (clusterId <= 0 || clusterId >= MaxPossibleClusters) return false;
    if (!ClassAlive[clusterId])                             return false;

    // ── Collect cluster members ──────────────────────────────────────────
    std::vector<int> members;
    members.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == clusterId) members.push_back(p);
    const int M = static_cast<int>(members.size());
    if (M < DipSplitMinSize * 2) { reason_out = "small"; return false; }

    // ── Gate A: bloat ────────────────────────────────────────────────────
    // mahal²(p, c) = 2·(LogP[c][p] − baseScore_c).  We drop baseScore_c from
    // the comparison because we only need the per-point excess for the 90th-
    // percentile, and baseScore_c is constant across members of cluster c.
    //
    // For a true single-Gaussian cluster this is calibrated to χ²(d, 0.9).
    // For a bimodal cluster fitted as one Gaussian, this *should* inflate
    // — but if CEM has been generous with the covariance to absorb the
    // separation, it can sit just under the χ² target.  Gate A' below
    // catches that case from the eigenvalue side.
    std::vector<double> mahal2(M);
    for (int i = 0; i < M; ++i) {
        const int p = members[i];
        mahal2[i] = 2.0 * static_cast<double>(
            LogP.m_Data[static_cast<size_t>(clusterId) * nPoints + p]);
    }
    // Subtract the within-cluster minimum so values are comparable to the
    // null χ²(d) distribution (the additive constant from baseScore_c cancels).
    const double mmin = *std::min_element(mahal2.begin(), mahal2.end());
    for (double& v : mahal2) v -= mmin;

    const double mahal2_p90 = percentile_sorted(mahal2, 0.90);
    // χ²(d, 0.9) via Wilson-Hilferty.  z for p=0.9 is 1.2816.
    const double d_  = static_cast<double>(nDims);
    const double a_wh = 1.0 - 2.0 / (9.0 * d_);
    const double b_wh = 1.2816 * std::sqrt(2.0 / (9.0 * d_));
    const double chi2_90 = d_ * std::pow(a_wh + b_wh, 3.0);
    const bool bloat_pass = (mahal2_p90 >= DipSplitBloatFactor * chi2_90);

    // ── Collect member feature vectors for PCA + dip + k-means ───────────
    // We do NOT use the time dim (last column) — it's a normalized timestamp
    // that shouldn't drive bimodality decisions.
    const int dPCA = nDims - 1;
    std::vector<float> Xmem(static_cast<size_t>(M) * dPCA);
    for (int i = 0; i < M; ++i) {
        const int p = members[i];
        for (int j = 0; j < dPCA; ++j)
            Xmem[static_cast<size_t>(i) * dPCA + j] =
                Data.m_Data[p * nDims + j];
    }

    // ── Compute top-3 PCs WITH eigenvalues ───────────────────────────────
    // Eigenvalues drive Gate A' (elongation) below.  Cost is ~free over the
    // PCs-only call: same covariance build, same iterations, plus one d²
    // Rayleigh quotient per converged PC.
    constexpr int kPCA = 3;
    std::vector<double> pcs(static_cast<size_t>(kPCA) * dPCA, 0.0);
    std::vector<double> eigs(kPCA, 0.0);
    dipsplit::top_pcs_with_eigenvalues(Xmem.data(), M, dPCA, kPCA,
                                        pcs.data(), eigs.data());

    // ── Gate A': elongation (covariance-eccentricity) ────────────────────
    // A unimodal Gaussian's top-3 eigenvalues are all of similar size (a
    // ratio of ~2-3 between top1 and the others is normal for spike feature
    // spaces where a few channels typically dominate).  Two well-separated
    // sub-modes fitted as one Gaussian show a much larger ratio: the inter-
    // mode separation contributes (D/2)² to the variance along that axis,
    // dwarfing the within-mode spread.  Threshold 4.0 is a conservative
    // pick — it lets bloat handle most cases and only kicks in for clearly
    // elongated covariances.
    //
    // The metric is eig_top1 / median(eig_top1..3).  Median (not mean) so
    // an enormous top eigenvalue doesn't drag its own reference up.  And
    // eig_top1 / median is more stable than eig_top1 / eig_top3 — the
    // latter can be tiny if the cluster is genuinely degenerate (rank-2).
    bool elong_pass = false;
    double elong_ratio = 0.0;
    if (DipSplitElongationFactor > 0.0f) {
        std::vector<double> sorted_eigs = eigs;
        std::sort(sorted_eigs.begin(), sorted_eigs.end());  // ascending
        const double eig_med = sorted_eigs[kPCA / 2];       // kPCA=3 → idx 1 = median
        const double eig_top = sorted_eigs.back();
        if (eig_med > 0.0) {
            elong_ratio = eig_top / eig_med;
            elong_pass  = (elong_ratio >= DipSplitElongationFactor);
        }
    }

    // Either gate is sufficient — bloated OR elongated triggers evaluation.
    if (!bloat_pass && !elong_pass) {
        reason_out = "not_bloated";   // legacy reason name; covers both gates
        return false;
    }

    // Compute cluster centroid (for projection centering).
    std::vector<double> centroid(dPCA, 0.0);
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < dPCA; ++j)
            centroid[j] += Xmem[static_cast<size_t>(i) * dPCA + j];
    for (int j = 0; j < dPCA; ++j) centroid[j] /= M;

    // ── Gate B: dip test on subdimensional projection ────────────────────
    //
    // Two modes (selected by -DipSplit2D):
    //
    // Mode 0 (default): test each top-K PC's 1D projection individually.
    // Fast, catches bimodality aligned with any single eigenvector.  Can
    // miss diagonal discriminants where neither PC1 nor PC2 alone shows
    // a valley but their joint distribution does.
    //
    // Mode 1: project to (PC1, PC2) plane, scan directions θ ∈ [0, π) at
    // 5° resolution (36 angles), run the 1D valley test on each direction,
    // keep the deepest valley.  Catches diagonal discriminants — the
    // optimal axis for separating two modes is the line between their
    // centroids, which the scan finds without knowing the centroids a
    // priori.  PC3+ is dropped under the assumption that real bimodality
    // shows up in the top 2 variance directions; if you suspect PC3+
    // bimodality on your data, stay in mode 0.
    int    best_pc     = -1;
    double best_depth  = 0.0;
    double best_valley = 0.0;
    double best_theta_deg = 0.0;       // mode 1 only
    bool   best_is_2d  = false;
    std::vector<double> best_projection;

    if (DipSplit2D != 0 && kPCA >= 2) {
        // Pre-compute PC1 and PC2 scores once for all spikes.
        std::vector<double> s1(static_cast<size_t>(M));
        std::vector<double> s2(static_cast<size_t>(M));
        for (int i = 0; i < M; ++i) {
            double a1 = 0.0, a2 = 0.0;
            for (int j = 0; j < dPCA; ++j) {
                const double v = Xmem[static_cast<size_t>(i) * dPCA + j]
                               - centroid[static_cast<size_t>(j)];
                a1 += v * pcs[0 * dPCA + j];
                a2 += v * pcs[1 * dPCA + j];
            }
            s1[static_cast<size_t>(i)] = a1;
            s2[static_cast<size_t>(i)] = a2;
        }

        // Directional scan over θ ∈ [0, π) at 5° resolution.  Dip statistic
        // is direction-symmetric (dip(θ) == dip(θ+π)) so half-circle suffices.
        constexpr int nDir = 36;
        std::vector<double> proj(static_cast<size_t>(M));
        for (int k = 0; k < nDir; ++k) {
            const double theta = M_PI * static_cast<double>(k) / nDir;
            const double c  = std::cos(theta);
            const double sn = std::sin(theta);
            for (int i = 0; i < M; ++i)
                proj[static_cast<size_t>(i)] =
                    c * s1[static_cast<size_t>(i)] + sn * s2[static_cast<size_t>(i)];
            const dipsplit::ValleyResult vr = dipsplit::valley_test(
                proj.data(), M, DipSplitValleyThresh);
            if (vr.depth > best_depth) {
                best_pc         = 0;                       // sentinel; "2D-PC12"
                best_depth      = vr.depth;
                best_valley     = vr.valley_loc;
                best_theta_deg  = theta * 180.0 / M_PI;
                best_is_2d      = true;
                best_projection = proj;                    // copy
            }
        }
    } else {
        // Per-PC 1D test (mode 0, current default).
        for (int pc = 0; pc < kPCA; ++pc) {
            const double* u = pcs.data() + pc * dPCA;
            std::vector<double> proj(M);
            for (int i = 0; i < M; ++i) {
                double s = 0.0;
                for (int j = 0; j < dPCA; ++j)
                    s += (Xmem[static_cast<size_t>(i) * dPCA + j] - centroid[j])
                         * u[j];
                proj[i] = s;
            }
            const dipsplit::ValleyResult vr = dipsplit::valley_test(
                proj.data(), M, DipSplitValleyThresh);
            if (vr.depth > best_depth) {
                best_pc         = pc;
                best_depth      = vr.depth;
                best_valley     = vr.valley_loc;
                best_projection = std::move(proj);
            }
        }
    }
    if (best_pc < 0 || best_depth < DipSplitValleyThresh) {
        reason_out = "no_valley";
        return false;
    }

    // ── Seed k=2 partition at the valley ─────────────────────────────────
    std::vector<int> labels(M, 0);
    for (int i = 0; i < M; ++i)
        labels[i] = (best_projection[i] >= best_valley) ? 1 : 0;

    // Compute initial centroids from the valley partition.
    std::vector<double> c0(dPCA, 0.0), c1(dPCA, 0.0);
    int n0 = 0, n1 = 0;
    for (int i = 0; i < M; ++i) {
        const float* row = Xmem.data() + i * dPCA;
        if (labels[i] == 0) {
            for (int j = 0; j < dPCA; ++j) c0[j] += row[j];
            ++n0;
        } else {
            for (int j = 0; j < dPCA; ++j) c1[j] += row[j];
            ++n1;
        }
    }
    if (n0 < DipSplitMinSize || n1 < DipSplitMinSize) {
        reason_out = "small_child";
        return false;
    }
    for (int j = 0; j < dPCA; ++j) { c0[j] /= n0; c1[j] /= n1; }

    // ── Refine with k-means k=2 ──────────────────────────────────────────
    dipsplit::kmeans2_refine(Xmem.data(), M, dPCA, c0.data(), c1.data(),
                             labels.data(), /*max_iters=*/20);

    // Recount post-refine
    n0 = n1 = 0;
    for (int i = 0; i < M; ++i) {
        if (labels[i] == 0) ++n0; else ++n1;
    }
    if (n0 < DipSplitMinSize || n1 < DipSplitMinSize) {
        reason_out = "small_child";
        return false;
    }

    // ── BIC gate ─────────────────────────────────────────────────────────
    const dipsplit::BicPair bp = dipsplit::bic_two_vs_one(
        Xmem.data(), M, dPCA, labels.data());
    if (!(bp.bic_k2 < bp.bic_k1)) {
        reason_out = "bic_worse";
        return false;
    }

    // ── Allocate a new cluster ID for the right half ──────────────────────
    int newId = -1;
    for (int c = 1; c < MaxPossibleClusters; ++c) {
        if (!ClassAlive[c]) { newId = c; break; }
    }
    if (newId < 0) { reason_out = "no_free_id"; return false; }

    // ── Commit split: relabel the right-half members ─────────────────────
    ClassAlive[newId] = 1;
    ++nClustersAlive;
    for (int i = 0; i < M; ++i) {
        if (labels[i] == 1) Class[members[i]] = newId;
    }

    if (best_is_2d) {
        Output("  [dipsplit] cluster %d → %d+%d  PC12@%.0f° depth=%.3f  "
               "mahal²₉₀=%.1f vs χ²₉₀=%.1f  elong=%.2fx  ΔBIC=%.1f  gate=%s\n",
               clusterId, n0, n1, best_theta_deg, best_depth,
               mahal2_p90, chi2_90, elong_ratio, bp.bic_k1 - bp.bic_k2,
               bloat_pass ? (elong_pass ? "both" : "bloat") : "elong");
    } else {
        Output("  [dipsplit] cluster %d → %d+%d  PC%d depth=%.3f  "
               "mahal²₉₀=%.1f vs χ²₉₀=%.1f  elong=%.2fx  ΔBIC=%.1f  gate=%s\n",
               clusterId, n0, n1, best_pc, best_depth,
               mahal2_p90, chi2_90, elong_ratio, bp.bic_k1 - bp.bic_k2,
               bloat_pass ? (elong_pass ? "both" : "bloat") : "elong");
    }
    reason_out = "split";
    return true;
}


// ---------------------------------------------------------------------------
// DipSplitPhase — iterate alive clusters, attempt dip-split on each.
//
// Always prints a summary line so users can see the phase ran and what
// gates rejected clusters.  Runs MStep+EStep at the end if any split was
// accepted, so cluster stats and LogP[] are fresh for the next phase.
// ---------------------------------------------------------------------------
int KK::DipSplitPhase()
{
    if (DipSplitEnable == 0) return 0;
    if (nDims < 2)           return 0;   // need at least 1 feature dim + time

    // Snapshot alive clusters — we'll create new ones during iteration and
    // shouldn't probe them recursively this pass.
    std::vector<int> alive_snapshot;
    alive_snapshot.reserve(32);
    for (int c = 1; c < MaxPossibleClusters; ++c)
        if (ClassAlive[c]) alive_snapshot.push_back(c);

    // Count-by-reason for summary
    int n_split       = 0;
    int n_small       = 0;
    int n_not_bloated = 0;
    int n_no_valley   = 0;
    int n_small_child = 0;
    int n_bic_worse   = 0;
    int n_no_free_id  = 0;

    LockedStderr( "[DipSplit] DipSplit: probing %zu alive clusters "
                    "(bloat=%.2f, elong=%.2f, valley=%.2f, minSize=%d)\n",
            alive_snapshot.size(), DipSplitBloatFactor,
            DipSplitElongationFactor, DipSplitValleyThresh, DipSplitMinSize);

    for (int c : alive_snapshot) {
        if (!ClassAlive[c]) continue;
        const char* reason = "skip";
        DipSplitAttemptEx(c, reason);
        if      (std::strcmp(reason, "split")       == 0) ++n_split;
        else if (std::strcmp(reason, "small")       == 0) ++n_small;
        else if (std::strcmp(reason, "not_bloated") == 0) ++n_not_bloated;
        else if (std::strcmp(reason, "no_valley")   == 0) ++n_no_valley;
        else if (std::strcmp(reason, "small_child") == 0) ++n_small_child;
        else if (std::strcmp(reason, "bic_worse")   == 0) ++n_bic_worse;
        else if (std::strcmp(reason, "no_free_id")  == 0) ++n_no_free_id;
    }

    LockedStderr( "[DipSplit] DipSplit: %d accepted  "
                    "(rejections: %d too-small, %d not-flagged, %d no-valley, "
                    "%d small-child, %d bic-worse, %d no-free-id)\n",
            n_split, n_small, n_not_bloated, n_no_valley,
            n_small_child, n_bic_worse, n_no_free_id);

    if (n_split > 0) {
        // Refresh cluster stats so downstream phases see consistent state.
        MStep();
        EStep();
        CStep();
        Reindex();
    }
    return n_split;
}



// ---------------------------------------------------------------------------
// KK::DipSplitPerChunk
//
// Per-chunk DipSplit pass — runs immediately after Phase 1 chunked CEM,
// before any other refinement.  For each chunk, builds a thread-local
// scratch KK populated with the chunk's data and current Class[] labels,
// runs MStep + EStep so cluster stats are current, then invokes
// DipSplitPhase on the scratch KK to find bimodal/elongated clusters
// that the parametric CEM merged.  Refined labels are copied back to
// perChunkClass[ck] and the chunk's ChunkModel list is rebuilt from the
// new partition.
//
// This replaces the global Phase 8 DipSplit invocation that previously
// ran AFTER cross-chunk model matching.  Running per-chunk and early has
// two advantages over the late global pass:
//
//   - Catches missed splits at the chunk level before cross-chunk merge
//     reads the per-chunk models.  Misses at this stage propagate as
//     undersplit globals; catching them here makes Phase 6 see the
//     correct cluster count.
//
//   - Avoids splitting global clusters that look bimodal only because
//     drift across chunks made the spike distribution look two-moded.
//     Per-chunk views are drift-free within their time window.
//
// Cluster ID allocation: when a chunk-local cluster splits into two,
// sub-cluster 0 keeps the original local id; sub-cluster 1 gets a fresh
// id starting from the chunk's current maxLocalId+1.
// ---------------------------------------------------------------------------
void KK::DipSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims,
    const char* phaseLabel, int onlyChunk)
{
    if (DipSplitEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    int totalSplitsAcrossChunks = 0;
    int totalChunksWithSplits   = 0;

    if (onlyChunk < 0) LockedStderr(
            "[%s] Per-chunk DipSplit (bloat=%.2f, elong=%.2f, "
            "valley=%.2f, minSize=%d)\n",
            phaseLabel,
            DipSplitBloatFactor, DipSplitElongationFactor,
            DipSplitValleyThresh, DipSplitMinSize);

    // Per-chunk results: new perChunkClass and replacement ChunkModel list.
    // Computed in parallel below, applied serially after the parallel block
    // so model-rebuild side-effects don't race.
    struct ChunkResult {
        bool                     changed = false;
        std::vector<int>         newClass;
        std::vector<ChunkModel>  newModels;
        int                      nSplits = 0;
    };
    std::vector<ChunkResult> results(nCh);

    const int _ckLo = (onlyChunk >= 0) ? onlyChunk : 0;
    const int _ckHi = (onlyChunk >= 0) ? onlyChunk + 1 : nCh;
    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) reduction(+:totalSplitsAcrossChunks,totalChunksWithSplits)
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        const auto& pts = chunkPoints[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts < DipSplitMinSize) continue;
        if (perChunkClass[ck].size() != static_cast<size_t>(nPts)) continue;

        // Skip chunks with only noise.  Need at least one real cluster for
        // DipSplit to have anything to probe.
        int maxLc = 0;
        for (int c : perChunkClass[ck]) if (c > maxLc) maxLc = c;
        if (maxLc < 1) continue;

        // ── Build scratch KK with chunk's data and current labels ─────
        KK Ks;
        Ks.nDims              = nFullDims;
        Ks.nPoints            = nPts;
        Ks.penaltyMix         = penaltyMix;
        Ks.suppressBestSave   = true;
        Ks.minClustersAlive   = 1;
        Ks.NoisePoint         = 0;          // user spec: no real noise from extraction

        // DipSplit parameters (DipSplitEnable, DipSplitMinSize,
        // DipSplitElongationFactor, DipSplitValleyThresh, DipSplitBloatFactor)
        // are file-scope globals defined in Parameters.cpp, not class members.
        // DipSplitPhase reads them directly — no per-instance copy required.

        Ks.AllocateArrays();
        Ks.AllocateCholeskyVecs();
        Ks.ReinitForSplit(nPts, nFullDims, penaltyMix);

        // Copy chunk data
        for (int i = 0; i < nPts; i++) {
            const int p = pts[static_cast<size_t>(i)];
            for (int d = 0; d < nFullDims; d++)
                Ks.Data[static_cast<size_t>(i) * nFullDims + d] =
                    Data[static_cast<size_t>(p) * nFullDims + d];
        }
        Ks.timeRawMin = timeRawMin;
        Ks.timeRawMax = timeRawMax;

        // Set Class[] from current per-chunk labels and ClassAlive[]
        for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
        for (int i = 0; i < nPts; i++) {
            const int c = perChunkClass[ck][static_cast<size_t>(i)];
            Ks.Class[i] = c;
            if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
        }
        Ks.Reindex();

        // Compute cluster stats so DipSplit sees current Mean/Cov/cholFlat.
        // EStep is needed (not just MStep) because DipSplitAttemptEx reads
        // cholFlat for Mahalanobis-based gates.
        Ks.MStep();
        Ks.EStep();

        // Snapshot pre-split alive count to measure how many splits occurred
        const int aliveBefore = Ks.nClustersAlive;

        // Run DipSplit on the scratch KK.  DipSplitPhase prints a summary
        // line tagged "[DipSplit]"; for per-chunk usage we don't need the
        // verbose per-chunk summaries (the overall total is logged below
        // in the serial section), so we silence them by gating on a per-
        // chunk flag.  Rather than threading a verbosity flag, we accept
        // the existing output — it's harmless and useful for debugging.
        Ks.DipSplitPhase();

        const int aliveAfter = Ks.nClustersAlive;
        const int nSplits    = std::max(0, aliveAfter - aliveBefore);

        if (nSplits == 0) continue;

        // DipSplitPhase mutated Class[] but didn't refresh Mean/Cov for the
        // new sub-clusters.  Run MStep so we can populate ChunkModel mean/cov
        // from Ks.Mean/Ks.Cov below.  cholFlat doesn't need refreshing here
        // — it isn't read by the rebuild path.
        Ks.MStep();

        // Read back labels.  DipSplit may have allocated new cluster IDs
        // outside the previous maxLc range.  We preserve those IDs in the
        // chunk-local namespace by remapping: any new ID (> original maxLc)
        // gets a fresh local id starting from maxLc+1, allocated densely
        // and deterministically per chunk.
        std::unordered_map<int,int> remap;
        int nextLc = maxLc + 1;
        std::vector<int> newCls(nPts);
        for (int i = 0; i < nPts; i++) {
            const int sc = Ks.Class[i];
            if (sc <= maxLc) { newCls[i] = sc; continue; }
            auto it = remap.find(sc);
            if (it == remap.end()) {
                remap[sc] = nextLc++;
                it = remap.find(sc);
            }
            newCls[i] = it->second;
        }

        // Build a sub-cluster -> final-local-id map for the ChunkModel
        // rebuild loop below (sub-cluster id in Ks.Class[] -> local id we
        // committed to perChunkClass[ck]).
        std::unordered_map<int,int> scToLc;
        for (int sc = 0; sc < MaxPossibleClusters; sc++) {
            if (!Ks.ClassAlive[sc]) continue;
            if (sc <= maxLc)              scToLc[sc] = sc;
            else if (remap.count(sc))     scToLc[sc] = remap[sc];
            // else: alive but no spikes assigned — skip
        }

        // Rebuild ChunkModel list from Ks.Mean / Ks.Cov.  Mirror the
        // canonical pattern used in Phase 1's per-chunk seeding loop
        // (KK.cpp:3198-3218): full nFullDims mean, full nFullDims² cov
        // (upper triangle only), nMembers from Ks.Class scan.
        std::vector<ChunkModel> newMdls;
        newMdls.reserve(scToLc.size());
        for (const auto& [sc, lc] : scToLc) {
            ChunkModel cm;
            cm.chunkIdx        = ck;
            cm.localClusterId  = lc;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(static_cast<size_t>(nFullDims), 0.0f);
            cm.cov.assign (static_cast<size_t>(nFullDims) * nFullDims, 0.0f);

            for (int d = 0; d < nFullDims; d++)
                cm.mean[static_cast<size_t>(d)] =
                    Ks.Mean[static_cast<size_t>(sc) * nFullDims + d];
            for (int r = 0; r < nFullDims; r++)
                for (int col = r; col < nFullDims; col++)
                    cm.cov[static_cast<size_t>(r) * nFullDims + col] =
                        Ks.Cov[static_cast<size_t>(sc) * Ks.nDims2
                              + r * nFullDims + col];

            for (int i = 0; i < nPts; i++)
                if (Ks.Class[i] == sc) cm.nMembers++;
            if (cm.nMembers == 0) continue;

            newMdls.push_back(std::move(cm));
        }

        results[ck].changed   = true;
        results[ck].newClass  = std::move(newCls);
        results[ck].newModels = std::move(newMdls);
        results[ck].nSplits   = nSplits;
        totalSplitsAcrossChunks += nSplits;
        totalChunksWithSplits   += 1;
    }

    // ── Serial application ───────────────────────────────────────────
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        if (!results[ck].changed) continue;
        perChunkClass [ck] = std::move(results[ck].newClass);
        perChunkModels[ck] = std::move(results[ck].newModels);
    }

    if (onlyChunk < 0) LockedStderr(
            "[Stage 2.2] DipSplit per-chunk: %d splits across %d chunks\n",
            totalSplitsAcrossChunks, totalChunksWithSplits);
}



// ---------------------------------------------------------------------------
// KK::HullSplitPerChunk
//
// Phase 2a.6: k-NN-graph connected-components split.  Per-cluster, treats
// each cluster's spikes as a point cloud in feature space, builds a k-NN
// graph, and asks: is this cluster topologically a single connected blob,
// or does it fall into multiple distinct components?  Components ≥
// minComponentSize materialise as new local clusters within the chunk;
// smaller components are absorbed (stay in the parent cluster).
//
// Complement to Phase 2a.5 DipSplit: DipSplit catches 1D-projected
// bimodality; HullSplit catches multi-D topology — two distinct units
// that occupy separate regions of (PC1, PC2, …, PC_K) feature space
// that no single PC marginal reveals.
//
// Model rebuild deferred to Phase 2b (ChunkReCEMPerChunk regenerates
// ChunkModels from the post-HullSplit labels via its MStep pass).  So
// HullSplitPerChunk only updates perChunkClass[].
//
// Cluster ID allocation: the largest component keeps the original local
// ID; additional components get fresh IDs starting from chunk's current
// maxLocalId+1.
//
// Cost: per-chunk parallel; brute-force k-NN within each cluster is
// O(n² · d / 16) with AVX-512 batched distance (~10 ms for a 5000-spike
// cluster at d=22).  Most clusters are small (≤500 spikes) so the dominant
// cost is the long tail.
// ---------------------------------------------------------------------------
void KK::HullSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    int nFullDims,
    const char* phaseLabel, int onlyChunk)
{
    if (HullSplitEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    if (onlyChunk < 0) LockedStderr(
            "[%s] HullSplit per-chunk (k=%d, minSize=%d, reachScale=%.2f, "
            "metric=%s)\n",
            phaseLabel, HullSplitK, HullSplitMinComponentSize,
            HullSplitMutualReachScale,
            HullSplitUseMutualReach ? "mutual-reachability" : "raw-euclidean");

    int totalSplitsAcrossChunks = 0;
    int totalChunksWithSplits   = 0;
    int totalNewSubClusters     = 0;

    struct ChunkResult {
        bool             changed = false;
        std::vector<int> newClass;
        int              nSplits = 0;
        int              nNewSubClusters = 0;
    };
    std::vector<ChunkResult> results(static_cast<size_t>(nCh));

    const int _ckLo = (onlyChunk >= 0) ? onlyChunk : 0;
    const int _ckHi = (onlyChunk >= 0) ? onlyChunk + 1 : nCh;
    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) \
        reduction(+:totalSplitsAcrossChunks,totalChunksWithSplits,totalNewSubClusters)
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        const auto& pts = chunkPoints[static_cast<size_t>(ck)];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts < 2 * HullSplitMinComponentSize) continue;
        if (perChunkClass[static_cast<size_t>(ck)].size() != static_cast<size_t>(nPts))
            continue;

        // Copy current labels so we mutate a local buffer and only
        // commit at the end.
        std::vector<int> newCls = perChunkClass[static_cast<size_t>(ck)];

        // Track maxLc across the chunk; new sub-cluster IDs allocate
        // starting from maxLc+1.  Each accepted split bumps maxLc.
        int maxLc = 0;
        for (int c : newCls) if (c > maxLc) maxLc = c;

        // Find unique non-noise local cluster IDs in this chunk.
        std::vector<int> uniqueLcs;
        {
            std::vector<bool> seen(static_cast<size_t>(maxLc + 1), false);
            for (int c : newCls)
                if (c > 0 && c < static_cast<int>(seen.size()) &&
                    !seen[static_cast<size_t>(c)]) {
                    seen[static_cast<size_t>(c)] = true;
                    uniqueLcs.push_back(c);
                }
        }

        int chunkSplits         = 0;
        int chunkNewSubClusters = 0;

        for (int lc : uniqueLcs) {
            // Gather this cluster's spike indices and feature buffer.
            std::vector<int> memberIdx;  // indices into pts[] / newCls[]
            memberIdx.reserve(64);
            for (int i = 0; i < nPts; i++)
                if (newCls[static_cast<size_t>(i)] == lc) memberIdx.push_back(i);

            const int nMem = static_cast<int>(memberIdx.size());
            if (nMem < 2 * HullSplitMinComponentSize) continue;

            // Pack feature vectors into a contiguous nMem × nFullDims
            // buffer (point-major, same stride as Data).  cluster_hull_split
            // reads with cfg.excludeTimeDim, so we keep all dims in the
            // buffer and let it drop the last one internally.
            std::vector<float> featBuf(
                static_cast<size_t>(nMem) * nFullDims);
            for (int i = 0; i < nMem; i++) {
                const int p = pts[static_cast<size_t>(memberIdx[static_cast<size_t>(i)])];
                for (int d = 0; d < nFullDims; d++) {
                    featBuf[static_cast<size_t>(i) * nFullDims + d] =
                        Data[static_cast<size_t>(p) * nFullDims + d];
                }
            }

            cluster_hull_split::Config cfg;
            cfg.k                       = HullSplitK;
            cfg.minComponentSize        = HullSplitMinComponentSize;
            cfg.mutualReachabilityScale = HullSplitMutualReachScale;
            cfg.useMutualReachability   = (HullSplitUseMutualReach != 0);
            cfg.excludeTimeDim          = true;

            cluster_hull_split::Result hr =
                cluster_hull_split::Run(featBuf.data(), nMem, nFullDims, cfg);

            if (!hr.didSplit) continue;

            // Identify the largest component — it keeps lc.  Other
            // components get fresh local IDs from maxLc+1.
            std::vector<int> compSize(
                static_cast<size_t>(hr.nComponents + 1), 0);
            for (int lbl : hr.componentLabels) {
                if (lbl >= 0 && lbl <= hr.nComponents)
                    ++compSize[static_cast<size_t>(lbl)];
            }
            int largestComp = 1;
            for (int c = 2; c <= hr.nComponents; c++)
                if (compSize[static_cast<size_t>(c)] >
                    compSize[static_cast<size_t>(largestComp)])
                    largestComp = c;

            // Allocate new local IDs for all non-largest non-zero components.
            std::vector<int> compToLc(
                static_cast<size_t>(hr.nComponents + 1), 0);
            compToLc[0]            = lc;  // absorbed → stay in parent
            compToLc[static_cast<size_t>(largestComp)] = lc;  // largest → keep
            int newIds = 0;
            for (int c = 1; c <= hr.nComponents; c++) {
                if (c == largestComp) continue;
                compToLc[static_cast<size_t>(c)] = ++maxLc;
                ++newIds;
            }

            // Write back per-member labels.
            for (int i = 0; i < nMem; i++) {
                const int lbl = hr.componentLabels[static_cast<size_t>(i)];
                const int newLc = compToLc[static_cast<size_t>(lbl)];
                newCls[static_cast<size_t>(memberIdx[static_cast<size_t>(i)])] = newLc;
            }

            ++chunkSplits;
            chunkNewSubClusters += newIds;
        }

        if (chunkSplits > 0) {
            results[static_cast<size_t>(ck)].changed         = true;
            results[static_cast<size_t>(ck)].newClass        = std::move(newCls);
            results[static_cast<size_t>(ck)].nSplits         = chunkSplits;
            results[static_cast<size_t>(ck)].nNewSubClusters = chunkNewSubClusters;
            totalSplitsAcrossChunks += chunkSplits;
            totalChunksWithSplits   += 1;
            totalNewSubClusters     += chunkNewSubClusters;
        }
    }

    // Serial application.
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        if (!results[static_cast<size_t>(ck)].changed) continue;
        perChunkClass[static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newClass);
    }

    if (onlyChunk < 0) LockedStderr(
            "[%s] HullSplit per-chunk: %d clusters split, +%d new sub-clusters, "
            "%d chunks affected\n",
            phaseLabel, totalSplitsAcrossChunks, totalNewSubClusters,
            totalChunksWithSplits);
}



// ---------------------------------------------------------------------------
// KK::PerChannelSplitPerChunk
//
// Phase 2a.7: per-channel amplitude+phase bimodality split.  Per chunk,
// per cluster: read each spike's full waveform, extract 4 features per
// channel (peak amp, peak time, trough amp, trough time), run 1D + 2D
// angle-sweep valley tests across channels, and split clusters whose
// strongest channel shows a KDE-detectable valley.  See per_channel_split.h
// for the algorithm in detail.
//
// Catches the failure mode where two real units share most of their
// waveform but differ by a small combination of amplitude AND phase on
// one specific channel (klusters cluster #131 case).  DipSplit misses
// this because the signal spreads across multiple PCA dims; HullSplit
// misses it because the clusters are topologically connected.
//
// Like HullSplit, defers model rebuild to Phase 2b (ChunkReCEMPerChunk
// will regenerate ChunkModels from the post-split labels via its MStep
// pass).  So this method only updates perChunkClass[].
//
// Cost: per-chunk parallel; dominated by waveform reads (one mmap memcpy
// per spike) and the KDE-based valley_test (O(n log n + G) per scan).
// ~200ms total for 36 chunks × 25 clusters × 8 channels × 22 tests.
// ---------------------------------------------------------------------------
void KK::PerChannelSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    int nChan, int nSamplesPerSpike,
    const char* phaseLabel, int onlyChunk)
{
    if (PerChannelSplitEnable == 0) return;
    if (!m_timeShiftReady) {
        if (onlyChunk < 0) LockedStderr(
                "[%s] PerChannelSplit skipped: TimeShift backing store not "
                "initialised (need .spk mmap/fp for waveform reads).\n",
                phaseLabel);
        return;
    }
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0 || nChan <= 0 || nSamplesPerSpike <= 0) return;

    LockedStderr(
            "[%s] PerChannelSplit per-chunk (minSize=%d, valley=%.2f, "
            "minSubSize=%d, bicMargin=%.1f+%.1f·logN, snrRatio=%.2f)\n",
            phaseLabel, PerChannelSplitMinClusterSize,
            PerChannelSplitValleyThreshold, PerChannelSplitMinSubClusterSize,
            PerChannelSplitBicMarginConstant, PerChannelSplitBicMarginPerLogN,
            PerChannelSplitMinChannelSnrRatio);

    const int waveSamples = nChan * nSamplesPerSpike;

    int totalSplits         = 0;
    int totalChunksWithSplit = 0;
    int totalNewClusters    = 0;

    struct ChunkResult {
        bool             changed = false;
        std::vector<int> newClass;
        int              nSplits = 0;
        int              nNewClusters = 0;
    };
    std::vector<ChunkResult> results(static_cast<size_t>(nCh));

    // NOTE: per-chunk parallelism is safe because each chunk has an
    // independent perChunkClass[ck] vector and an independent set of
    // (global) spike indices.  TimeShiftReadSpikeWave is thread-safe
    // when m_timeShiftSpkMap is non-null (read-only memcpy); the
    // fseeko+fread fallback path is NOT thread-safe so we serialise
    // if mmap is unavailable.
    const bool mapBacked = (m_timeShiftSpkMap != nullptr);
    const int  maxThreads = mapBacked ? omp_get_max_threads() : 1;

    // Optional wall-clock cap: once exceeded, un-started chunks are skipped
    // and the in-flight chunk stops between clusters (splits are optional, so
    // the worst case is an under-split chunk — never inconsistent state).
    const double splitDeadline =
        (Phase2SplitTimeLimitSec > 0.0f) ? omp_get_wtime() + Phase2SplitTimeLimitSec : 0.0;
    std::atomic<bool> splitLimitHit{false};

    const int _ckLo = (onlyChunk >= 0) ? onlyChunk : 0;
    const int _ckHi = (onlyChunk >= 0) ? onlyChunk + 1 : nCh;
    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) num_threads(maxThreads) \
        reduction(+:totalSplits,totalChunksWithSplit,totalNewClusters)
    for (int ck = _ckLo; ck < _ckHi; ++ck) {
        if (splitDeadline > 0.0 && omp_get_wtime() > splitDeadline) {
            splitLimitHit.store(true, std::memory_order_relaxed); continue;
        }
        const auto& pts  = chunkPoints[static_cast<size_t>(ck)];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts < 2 * PerChannelSplitMinSubClusterSize) continue;
        if (perChunkClass[static_cast<size_t>(ck)].size() !=
            static_cast<size_t>(nPts)) continue;

        // Build local labels indexed [0, nPts) and per-cluster spike lists
        // (also using local indices).  per_channel_split::Run will receive
        // local indices and we translate them to global at read time.
        std::vector<int> localLabels =
            perChunkClass[static_cast<size_t>(ck)];

        int maxLc = 0;
        for (int c : localLabels) if (c > maxLc) maxLc = c;

        // Per-cluster spike lists (local indices).
        std::vector<std::vector<int>> clusterSpikes(
            static_cast<size_t>(maxLc + 1));
        for (int i = 0; i < nPts; ++i) {
            const int c = localLabels[static_cast<size_t>(i)];
            if (c <= 0) continue;
            clusterSpikes[static_cast<size_t>(c)].push_back(i);
        }

        // readWaveform callback: translates local idx → global spike id.
        auto readWave = [&](int localIdx, int16_t* dst) -> bool {
            if (localIdx < 0 || localIdx >= nPts) return false;
            const int p = pts[static_cast<size_t>(localIdx)];
            return TimeShiftReadSpikeWave(p, waveSamples, dst);
        };

        per_channel_split::Config cfg;
        cfg.minClusterSize       = PerChannelSplitMinClusterSize;
        cfg.valleyThreshold      = PerChannelSplitValleyThreshold;
        cfg.minSubClusterSize    = PerChannelSplitMinSubClusterSize;
        cfg.bicMarginConstant    = PerChannelSplitBicMarginConstant;
        cfg.bicMarginPerLogN     = PerChannelSplitBicMarginPerLogN;
        cfg.minChannelSnrRatio   = PerChannelSplitMinChannelSnrRatio;
        cfg.usePeakAmplitude     = (PerChannelSplitUsePeakAmp    != 0);
        cfg.usePeakTime          = (PerChannelSplitUsePeakTime   != 0);
        cfg.useTroughAmplitude   = (PerChannelSplitUseTroughAmp  != 0);
        cfg.useTroughTime        = (PerChannelSplitUseTroughTime != 0);
        cfg.firstNewClusterId    = maxLc + 1;
        if (splitDeadline > 0.0)
            cfg.shouldStop = [&splitLimitHit, splitDeadline]() {
                if (omp_get_wtime() > splitDeadline) {
                    splitLimitHit.store(true, std::memory_order_relaxed);
                    return true;
                }
                return false;
            };

        auto rr = per_channel_split::Run(
            clusterSpikes, readWave,
            nChan, nSamplesPerSpike,
            localLabels, cfg);

        if (rr.nClustersSplit > 0) {
            results[static_cast<size_t>(ck)].changed      = true;
            results[static_cast<size_t>(ck)].newClass     = std::move(localLabels);
            results[static_cast<size_t>(ck)].nSplits      = rr.nClustersSplit;
            results[static_cast<size_t>(ck)].nNewClusters = rr.nNewClusters;
            totalSplits          += rr.nClustersSplit;
            totalChunksWithSplit += 1;
            totalNewClusters     += rr.nNewClusters;
        }
    }

    // Serial application.
    for (int ck = _ckLo; ck < _ckHi; ++ck) {
        if (!results[static_cast<size_t>(ck)].changed) continue;
        perChunkClass[static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newClass);
    }

    if (splitLimitHit.load())
        LockedStderr("[%s] PerChannelSplit: time limit (%.0fs) reached — "
                     "some chunks/clusters left unsplit\n", phaseLabel, Phase2SplitTimeLimitSec);
    if (onlyChunk < 0) LockedStderr(
            "[%s] PerChannelSplit per-chunk: %d clusters split, +%d new "
            "sub-clusters, %d chunks affected\n",
            phaseLabel, totalSplits, totalNewClusters, totalChunksWithSplit);
}



// ---------------------------------------------------------------------------
// KK::QualityWeightedSplitDispatch — Phase 4b quality-routed splitter.
// See the QualityWeightedSplit doc block in KlustaKwik.cpp.
// ---------------------------------------------------------------------------
void KK::QualityWeightedSplitDispatch(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    // Effective N: explicit flag, else max of the two per-call caps,
    // else 4.
    int N = QualityWeightedSplitN;
    if (N <= 0) N = std::max(WaveKnnMaxSourcesPerCall,
                             FullCemSplitMaxSourcesPerCall);
    if (N <= 0) N = 4;
    const int poolFactor = std::max(1, QualityWeightedSplitPoolFactor);
    const int poolSize   = poolFactor * N;

    const int minClusterSize = (FullCemSplitMinClusterSize > 0)
                             ? FullCemSplitMinClusterSize
                             : std::max(nFullDims + 5, 25);

    // ── Gather eligible source clusters across all chunks. ──
    struct Cand {
        int ck;
        int lc;
        std::vector<int> globalIds;   // global spike indices
    };
    std::vector<Cand> pool;
    int nCooldownSkipped = 0;
    for (int ck = 0; ck < nCh; ck++) {
        const auto& cls = perChunkClass[static_cast<size_t>(ck)];
        const auto& pts = chunkPoints[static_cast<size_t>(ck)];
        std::unordered_map<int, std::vector<int>> byLc;
        const int n = static_cast<int>(cls.size());
        for (int i = 0; i < n; i++) {
            const int lc = cls[static_cast<size_t>(i)];
            if (lc <= 0) continue;
            byLc[lc].push_back(pts[static_cast<size_t>(i)]);
        }
        for (auto& kv : byLc) {
            if (static_cast<int>(kv.second.size()) < minClusterSize) continue;
            // Oscillation guard (patch 0053): skip clusters on active
            // split cooldown (their membership round-tripped recently).
            if (!m_splitCooldown.empty()) {
                const uint64_t h = ClusterMembershipHash(kv.second);
                auto it = m_splitCooldown.find(h);
                if (it != m_splitCooldown.end() && it->second > m_phase4Iter) {
                    ++nCooldownSkipped;
                    continue;
                }
            }
            pool.push_back({ck, kv.first, std::move(kv.second)});
        }
    }
    if (nCooldownSkipped > 0) {
        LockedStderr("[Stage 2.11 split] QualityWeightedSplit: skipped %d cluster(s) "
                     "on oscillation cooldown\n", nCooldownSkipped);
    }
    if (pool.empty()) {
        LockedStderr("[Stage 2.11 split] QualityWeightedSplit: no eligible sources "
                     "(minClusterSize=%d)\n", minClusterSize);
        return;
    }

    // ── Random oversample to poolSize (callSalt-varied). ──
    const int nEligible = static_cast<int>(pool.size());
    const unsigned callSalt = m_phase4SplitCallCount++;
    {
        const unsigned base = (RandomSeed != 0)
            ? static_cast<unsigned>(RandomSeed) : static_cast<unsigned>(std::time(nullptr));
        std::mt19937 rng(base ^ 0x5a5a5a5au ^ (callSalt * 2654435761u));
        std::shuffle(pool.begin(), pool.end(), rng);
    }
    if (static_cast<int>(pool.size()) > poolSize)
        pool.resize(static_cast<size_t>(poolSize));

    // ── Compute metrics for each candidate (parallel — each cluster's
    //    metrics are independent; ClusterWaveformVariance reads spikes
    //    from the mmap'd/cached store, safe for concurrent reads). ──
    const float refractorySamples =
        (SamplingRate > 0.0f && QualityWeightedISIRefractoryMs > 0.0f)
            ? SamplingRate * QualityWeightedISIRefractoryMs / 1000.0f
            : 0.0f;

    // Fallback: when the spike-waveform store is unavailable
    // (m_timeShiftReady == false, e.g. stderiv pipeline with TimeShift
    // legacy off), ClusterWaveformVariance returns 0 for every cluster
    // and ALL clusters would route to CEM.  Detect this and substitute
    // a FEATURE-SPACE variance proxy: total within-cluster variance of
    // the PCA features (sum of per-dim variance).  It correlates with
    // waveform spread well enough to keep the knn route populated.
    const bool useFeatureVarProxy = !m_timeShiftReady;
    if (useFeatureVarProxy) {
        LockedStderr("[Stage 2.11 split] QualityWeightedSplit: spike store "
                     "unavailable; using feature-space variance proxy "
                     "for the knn-route metric\n");
    }

    const int nC = static_cast<int>(pool.size());
    std::vector<double> contam(static_cast<size_t>(nC), 0.0);
    std::vector<double> wavVar(static_cast<size_t>(nC), 0.0);
    const int timeDimDispatch = nDims - 1;
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < nC; i++) {
        const auto& ids = pool[static_cast<size_t>(i)].globalIds;
        contam[static_cast<size_t>(i)] =
            ClusterISIContamination(ids, refractorySamples);
        if (useFeatureVarProxy) {
            // Sum of per-feature variance over the cluster's spikes
            // (spatial dims only; exclude the time dim).
            const int m = static_cast<int>(ids.size());
            double vsum = 0.0;
            if (m >= 2) {
                for (int d = 0; d < timeDimDispatch; d++) {
                    double s = 0.0, sq = 0.0;
                    for (int p : ids) {
                        const double v = Data[static_cast<size_t>(p) * nDims + d];
                        s += v; sq += v * v;
                    }
                    const double mean = s / m;
                    vsum += std::max(0.0, sq / m - mean * mean);
                }
            }
            wavVar[static_cast<size_t>(i)] = vsum;
        } else {
            wavVar[static_cast<size_t>(i)] =
                ClusterWaveformVariance(ids, NbChannels, NbSamplesPerSpike);
        }
    }

    // ── Min-max normalise each metric across the pool to [0,1]. ──
    auto normalise = [](std::vector<double>& v) {
        double lo = std::numeric_limits<double>::infinity();
        double hi = -std::numeric_limits<double>::infinity();
        for (double x : v) { lo = std::min(lo, x); hi = std::max(hi, x); }
        const double range = hi - lo;
        if (range <= 0.0) { for (double& x : v) x = 0.0; return; }
        for (double& x : v) x = (x - lo) / range;
    };
    std::vector<double> contamN = contam, wavVarN = wavVar;
    normalise(contamN);
    normalise(wavVarN);

    // ── Rank by needs-split score = max(contamN, varN); take top N. ──
    std::vector<int> order(static_cast<size_t>(nC));
    for (int i = 0; i < nC; i++) order[static_cast<size_t>(i)] = i;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const double sa = std::max(contamN[static_cast<size_t>(a)],
                                   wavVarN[static_cast<size_t>(a)]);
        const double sb = std::max(contamN[static_cast<size_t>(b)],
                                   wavVarN[static_cast<size_t>(b)]);
        return sa > sb;
    });
    const int take = std::min(N, nC);

    // ── Route: contamination-dominant → CEM, variance-dominant → knn. ──
    std::map<int, std::vector<int>> cemAllow, knnAllow;
    int nCem = 0, nKnn = 0;
    for (int r = 0; r < take; r++) {
        const int i  = order[static_cast<size_t>(r)];
        const Cand& c = pool[static_cast<size_t>(i)];
        if (contamN[static_cast<size_t>(i)] >= wavVarN[static_cast<size_t>(i)]) {
            cemAllow[c.ck].push_back(c.lc);
            ++nCem;
        } else {
            knnAllow[c.ck].push_back(c.lc);
            ++nKnn;
        }
    }

    LockedStderr("[Stage 2.11 split] QualityWeightedSplit: pool=%d (of %d eligible), "
                 "routed %d→CEM (contamination) + %d→knn (variance), "
                 "refractory=%.1f samp\n",
                 nC, nEligible, nCem, nKnn, refractorySamples);

    // ── Dispatch.  Each splitter gets ONLY its routed clusters. ──
    if (!cemAllow.empty()) {
        FullCemSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                             nFullDims, &cemAllow);
    }
    if (!knnAllow.empty()) {
        WaveKnnSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                             nFullDims, &knnAllow);
    }
}


void KK::FullCemSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims,
    const std::map<int, std::vector<int>>* sourceAllowlist,
    int reprobeDepth)
{
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    // Per-call salt (see m_phase4SplitCallCount doc) — increments every
    // invocation so the shuffle picks a different subset each Phase 4b
    // iter.  Shared counter with WaveKnnSplitPerChunk, so the two
    // splitters also never collide on the same call value.
    const unsigned callSalt = m_phase4SplitCallCount++;

    const int minClusterSize = (FullCemSplitMinClusterSize > 0)
                             ? FullCemSplitMinClusterSize
                             : std::max(nFullDims + 5, 25);

    // ── Phase A: build (chunk, cluster) work items, ONE per source
    //    cluster.  No subdivision.  Filter by minClusterSize.
    struct WorkItem {
        int ck;
        int lc;                      // chunk-local cluster id
        std::vector<int> members;    // indices into chunkPoints[ck]
    };
    std::vector<WorkItem> items;

    for (int ck = 0; ck < nCh; ck++) {
        const auto& cls = perChunkClass[static_cast<size_t>(ck)];
        // If an allowlist was supplied, restrict to its clusters for
        // this chunk (skip the chunk entirely if absent).
        const std::vector<int>* allowed = nullptr;
        if (sourceAllowlist) {
            auto it = sourceAllowlist->find(ck);
            if (it == sourceAllowlist->end() || it->second.empty()) continue;
            allowed = &it->second;
        }
        std::unordered_set<int> allowedSet;
        if (allowed) allowedSet.insert(allowed->begin(), allowed->end());

        // Group spike-indices by local cluster id.
        std::unordered_map<int, std::vector<int>> byLc;
        const int n = static_cast<int>(cls.size());
        for (int i = 0; i < n; i++) {
            const int lc = cls[static_cast<size_t>(i)];
            if (lc <= 0) continue;       // skip noise (lc==0)
            if (allowed && !allowedSet.count(lc)) continue;
            byLc[lc].push_back(i);
        }
        for (auto& kv : byLc) {
            if (static_cast<int>(kv.second.size()) < minClusterSize) continue;
            items.push_back({ck, kv.first, std::move(kv.second)});
        }
    }

    if (items.empty()) {
        LockedStderr("[Stage 2.11 split] FullCemSplit: no eligible source clusters "
                     "(minClusterSize=%d)\n", minClusterSize);
        return;
    }

    // ── Phase B: random shuffle + optional cap.  When an allowlist was
    //    supplied (quality-weighted dispatch), the items ARE the
    //    selection — skip the shuffle+cap so all allowlisted clusters
    //    get processed.  Otherwise shuffle (callSalt-varied) and cap.
    if (!sourceAllowlist) {
        const unsigned base = static_cast<unsigned>(RandomSeed)
                                ? static_cast<unsigned>(RandomSeed)
                                : static_cast<unsigned>(std::time(nullptr));
        std::mt19937 ordRng(base ^ 0xc1d2e3f4u ^ (callSalt * 2654435761u));
        std::shuffle(items.begin(), items.end(), ordRng);
        if (FullCemSplitMaxSourcesPerCall > 0
            && static_cast<int>(items.size()) > FullCemSplitMaxSourcesPerCall) {
            items.resize(static_cast<size_t>(FullCemSplitMaxSourcesPerCall));
        }
    }

    // ── Phase B.5: LPT load-balance.  Sort work items by member count
    //    descending so the OMP dynamic schedule hands out the biggest
    //    (slowest) clusters first — the classic longest-processing-time
    //    heuristic.  Matters more now that adaptive feature selection
    //    (patch 0047/0048) makes per-cluster CEM cost vary widely with
    //    nSubDims.  Pure reordering; does not change which clusters are
    //    processed or the results.
    std::sort(items.begin(), items.end(),
              [](const WorkItem& a, const WorkItem& b) {
                  return a.members.size() > b.members.size();
              });

    LockedStderr("[Stage 2.11 split] FullCemSplit: probing %d source cluster(s) "
                 "(minClusterSize=%d, cap=%d)\n",
                 static_cast<int>(items.size()), minClusterSize,
                 FullCemSplitMaxSourcesPerCall);

    // ── Phase C: parallel per-cluster CEM.  Inner body MIRRORS
    //    PerClusterCEMPerChunk's non-subdivided path (item.lc >= 0)
    //    -- see that function for the SubspaceDims rationale, the
    //    K=2 warm-start, and the spatial-only-dims convention.
    struct ItemResult {
        bool changed = false;
        int  nSubClusters = 0;
        std::vector<int> newSubLabels;
    };
    std::vector<ItemResult> results(items.size());

    int totalSplits         = 0;
    int totalNewSubClusters = 0;
    int totalRefractoryVetoed = 0;   // patch 0057: splits rejected by the gate

    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalSplits,totalNewSubClusters,totalRefractoryVetoed)
    for (int wi = 0; wi < static_cast<int>(items.size()); wi++) {
        const auto& item = items[static_cast<size_t>(wi)];
        // Deterministic per-work-item RNG seed (KlustaKwik.h): the split CEM
        // trial below draws randomness, so key the stream to (chunk,cluster)
        // identity -- reproducible and independent of thread count/schedule.
        kk_seed_rng(kk_mix_seed(kk_mix_seed(static_cast<uint64_t>(RandomSeed),
                                            static_cast<uint64_t>(item.ck)),
                                static_cast<uint64_t>(item.lc)));
        const int   nMem = static_cast<int>(item.members.size());
        const auto& pts  = chunkPoints[item.ck];

        const int nSpatialDimsFull = (nFullDims > 1) ? nFullDims - 1 : nFullDims;

        // Per-cluster feature selection.
        int  nSubDims;
        std::vector<int> selFeat;

        if (FullCemSplitAdaptiveFeatures != 0) {
            // ── Adaptive: rank features by 1-D bimodality (valley depth),
            //    select the minimum set that shows separable structure. ──
            int maxFeat = (FullCemSplitMaxFeatures > 0)
                            ? FullCemSplitMaxFeatures
                            : (SubspaceDims > 0 ? SubspaceDims : nSpatialDimsFull);
            maxFeat = std::min(maxFeat, nSpatialDimsFull);
            const int minFeat = std::min(std::max(1, FullCemSplitMinFeatures),
                                         maxFeat);

            // Project the cluster's spikes onto each feature, run the
            // valley test, record depth.
            std::vector<std::pair<double,int>> depthRank(nSpatialDimsFull);
            std::vector<double> proj(static_cast<size_t>(nMem));
            for (int d = 0; d < nSpatialDimsFull; d++) {
                for (int i = 0; i < nMem; i++) {
                    const int p = pts[static_cast<size_t>(
                        item.members[static_cast<size_t>(i)])];
                    proj[static_cast<size_t>(i)] =
                        Data[static_cast<size_t>(p) * nFullDims + d];
                }
                // threshold 0 so valley_test always reports the raw depth;
                // we apply our own gate below.
                const dipsplit::ValleyResult vr =
                    dipsplit::valley_test(proj.data(), nMem, 0.0);
                depthRank[static_cast<size_t>(d)] = { vr.depth, d };
            }
            std::sort(depthRank.begin(), depthRank.end(),
                      [](const auto& a, const auto& b){ return a.first > b.first; });

            // Count features passing the bimodality gate.
            int nPass = 0;
            for (const auto& dr : depthRank) {
                if (dr.first >= static_cast<double>(
                        FullCemSplitFeatureBimodalThreshold))
                    ++nPass;
                else
                    break;   // sorted desc, so first failure ends the run
            }
            // Use the minimum bimodal set PLUS ONE extra feature: the
            // next-highest-depth axis (the strongest one that did NOT
            // pass the gate) gives CEM a little off-axis context to fit
            // the Gaussians' covariance orientation, without inflating
            // the dimensionality.  Clamp to [minFeat, maxFeat].
            nSubDims = std::min(std::max(nPass + 1, minFeat), maxFeat);

            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int k = 0; k < nSubDims; k++)
                selFeat[static_cast<size_t>(k)] =
                    depthRank[static_cast<size_t>(k)].second;
            std::sort(selFeat.begin(), selFeat.end());

            if (Verbose >= 2) {
                LockedStderr("  [4b-cem] chunk%d c%d: adaptive features "
                             "nPass=%d -> nSubDims=%d (nPass+1, top depth=%.3f)\n",
                             item.ck, item.lc, nPass, nSubDims,
                             depthRank.empty() ? 0.0 : depthRank[0].first);
            }
        } else if (SubspaceDims > 0 && SubspaceDims < nSpatialDimsFull) {
            // ── Variance-ranked fixed-count (original Phase 2a logic). ──
            std::vector<double> sum(nSpatialDimsFull, 0.0);
            std::vector<double> sqsum(nSpatialDimsFull, 0.0);
            for (int i = 0; i < nMem; i++) {
                const int p = pts[static_cast<size_t>(item.members[static_cast<size_t>(i)])];
                for (int d = 0; d < nSpatialDimsFull; d++) {
                    const double v = Data[static_cast<size_t>(p) * nFullDims + d];
                    sum[d]   += v;
                    sqsum[d] += v * v;
                }
            }
            std::vector<std::pair<double,int>> rank(nSpatialDimsFull);
            const double invN = 1.0 / nMem;
            for (int d = 0; d < nSpatialDimsFull; d++) {
                const double m = sum[d] * invN;
                const double v = std::max(0.0, sqsum[d] * invN - m * m);
                rank[d] = {v, d};
            }
            std::sort(rank.begin(), rank.end(),
                      [](const auto& a, const auto& b){ return a.first > b.first; });
            nSubDims = SubspaceDims;
            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int k = 0; k < nSubDims; k++)
                selFeat[static_cast<size_t>(k)] = rank[static_cast<size_t>(k)].second;
            std::sort(selFeat.begin(), selFeat.end());
        } else {
            nSubDims = nSpatialDimsFull;
            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int d = 0; d < nSubDims; d++)
                selFeat[static_cast<size_t>(d)] = d;
        }

        KK Ks;
        Ks.nDims              = nSubDims;
        Ks.nPoints            = nMem;
        Ks.penaltyMix         = penaltyMix;
        Ks.suppressBestSave   = true;
        Ks.minClustersAlive   = 1;
        Ks.NoisePoint         = 0;

        Ks.AllocateArrays();
        Ks.AllocateCholeskyVecs();
        Ks.ReinitForSplit(nMem, nSubDims, penaltyMix);

        // Pack with selected features.
        for (int i = 0; i < nMem; i++) {
            const int p = pts[static_cast<size_t>(item.members[static_cast<size_t>(i)])];
            for (int k = 0; k < nSubDims; k++) {
                const int d = selFeat[static_cast<size_t>(k)];
                Ks.Data[static_cast<size_t>(i) * nSubDims + k] =
                    Data[static_cast<size_t>(p) * nFullDims + d];
            }
        }
        Ks.timeRawMin = timeRawMin;
        Ks.timeRawMax = timeRawMax;

        // Warm-start at K=2; TrySplits drives growth.
        Ks.nStartingClusters = 2;
        for (int c = 0; c < MaxPossibleClusters; c++)
            Ks.ClassAlive[c] = (c < 2) ? 1 : 0;
        for (int i = 0; i < nMem; i++) Ks.Class[i] = 1;
        Ks.Reindex();

        Ks.MStep();
        Ks.EStep();
        Ks.RunEMLoop(/*enableSplits=*/   true,
                     /*enableDistDump=*/ false,
                     /*maxIter=*/        0,
                     /*phaseLabel=*/     "[4b-cem]");

        if (Ks.nClustersAlive <= 2) continue;     // no split

        auto& r        = results[static_cast<size_t>(wi)];
        r.changed      = true;
        r.nSubClusters = Ks.nClustersAlive - 1;
        r.newSubLabels.resize(static_cast<size_t>(nMem));
        for (int i = 0; i < nMem; i++)
            r.newSubLabels[static_cast<size_t>(i)] = Ks.Class[i];

        // ── Refractory-aware acceptance gate (patch 0057) ──────────────
        // A genuine split of a temporally-contaminated cluster should
        // SEPARATE the parent's sub-refractory violating spike pairs into
        // different children — those short intervals are cross-unit, so a
        // correct split puts the two spikes in different clusters.  A
        // spurious split (cutting one unit along an amplitude axis) does
        // not preferentially separate them.  Require >= MinSep of the
        // violating consecutive-in-time pairs to be separated; else reject
        // and keep the parent intact (the CEM split was feature-driven but
        // does not resolve the refractory contamination).
        //
        // Note: bursts (ISI ~3-10 ms) sit ABOVE the ~2 ms refractory
        // window, so they are not counted as violations here — the gate is
        // already burst-robust at the metric level.  (Burst-spike
        // amplitude smear is a separate, feature-space concern.)
        if (FullCemSplitRefractoryGate != 0 && SamplingRate > 0.0f
            && QualityWeightedISIRefractoryMs > 0.0f && nMem >= 2) {
            const double refr =
                SamplingRate * QualityWeightedISIRefractoryMs / 1000.0;
            const int gateTimeDim = nDims - 1;
            std::vector<std::pair<double,int>> ts(
                static_cast<size_t>(nMem));
            for (int i = 0; i < nMem; i++) {
                const int p = pts[static_cast<size_t>(
                    item.members[static_cast<size_t>(i)])];
                ts[static_cast<size_t>(i)] = {
                    static_cast<double>(
                        Data[static_cast<size_t>(p) * nDims + gateTimeDim]),
                    r.newSubLabels[static_cast<size_t>(i)] };
            }
            std::sort(ts.begin(), ts.end(),
                      [](const std::pair<double,int>& a,
                         const std::pair<double,int>& b){
                          return a.first < b.first; });
            int nViol = 0, nSep = 0;
            for (int i = 1; i < nMem; i++) {
                if (ts[static_cast<size_t>(i)].first
                        - ts[static_cast<size_t>(i - 1)].first < refr) {
                    ++nViol;
                    if (ts[static_cast<size_t>(i)].second
                            != ts[static_cast<size_t>(i - 1)].second)
                        ++nSep;
                }
            }
            if (nViol > 0) {
                const double sepRate =
                    static_cast<double>(nSep) / static_cast<double>(nViol);
                if (sepRate < static_cast<double>(
                        FullCemSplitRefractoryGateMinSep)) {
                    r.changed      = false;
                    r.nSubClusters = 0;
                    r.newSubLabels.clear();
                    ++totalRefractoryVetoed;
                    if (Verbose >= 2)
                        LockedStderr("  [4b-cem] chunk%d c%d: split VETOED "
                                     "by refractory gate (%d/%d viol pairs "
                                     "separated = %.2f < %.2f)\n",
                                     item.ck, item.lc, nSep, nViol, sepRate,
                                     static_cast<double>(
                                         FullCemSplitRefractoryGateMinSep));
                    continue;
                }
            }
        }

        totalSplits         += 1;
        totalNewSubClusters += (Ks.nClustersAlive - 2);
    }

    // ── Phase D: serial commit — assign fresh chunk-local IDs ────────
    std::vector<int> nextLc(nCh, 1);
    for (int ck = 0; ck < nCh; ck++) {
        int maxLc = 0;
        for (int c : perChunkClass[static_cast<size_t>(ck)])
            if (c > maxLc) maxLc = c;
        nextLc[static_cast<size_t>(ck)] = maxLc + 1;
    }

    std::unordered_set<int> chunksAffected;
    // Collect newly-created sub-cluster IDs per chunk for the optional
    // reprobe pass (patch 0055).  A sub-cluster gets its own adaptive
    // feature selection on reprobe, catching mixtures along axes the
    // parent split didn't select.
    std::map<int, std::vector<int>> newSubClusterIds;
    for (int wi = 0; wi < static_cast<int>(items.size()); wi++) {
        const auto& res = results[static_cast<size_t>(wi)];
        if (!res.changed) continue;
        const auto& item = items[static_cast<size_t>(wi)];
        auto& cls = perChunkClass[static_cast<size_t>(item.ck)];

        // sub-label 0 -> 0 (noise; empty under NoisePoint=0)
        // sub-label 1 -> parent's local id (item.lc)
        // sub-label >= 2 -> fresh chunk-local id
        std::unordered_map<int,int> subToLc;
        subToLc[0] = 0;
        subToLc[1] = item.lc;

        const int nMem = static_cast<int>(item.members.size());
        for (int i = 0; i < nMem; i++) {
            const int sc = res.newSubLabels[static_cast<size_t>(i)];
            auto it = subToLc.find(sc);
            if (it == subToLc.end()) {
                const int fresh = nextLc[static_cast<size_t>(item.ck)]++;
                subToLc[sc] = fresh;
                newSubClusterIds[item.ck].push_back(fresh);
                it = subToLc.find(sc);
            }
            cls[static_cast<size_t>(item.members[static_cast<size_t>(i)])] = it->second;
        }
        // The parent ID (item.lc) also survived as sub-cluster 1; include
        // it so reprobe can split it further too.
        newSubClusterIds[item.ck].push_back(item.lc);
        chunksAffected.insert(item.ck);
    }

    LockedStderr("[Stage 2.11 split] FullCemSplit%s: %d clusters split, +%d new "
                 "sub-clusters, %d chunks affected\n",
                 reprobeDepth > 0 ? " (reprobe)" : "",
                 totalSplits, totalNewSubClusters,
                 static_cast<int>(chunksAffected.size()));
    if (FullCemSplitRefractoryGate != 0 && totalRefractoryVetoed > 0)
        LockedStderr("[Stage 2.11 split] FullCemSplit: refractory gate vetoed %d "
                     "feature-space split(s) that did not resolve sub-"
                     "refractory contamination\n", totalRefractoryVetoed);

    // ── Optional reprobe: feed the new sub-clusters back through FullCem,
    //    re-selecting adaptive features per sub-cluster.  Bounded by
    //    FullCemSplitReprobePasses (depth) to prevent runaway recursion. ──
    if (reprobeDepth < FullCemSplitReprobePasses && totalSplits > 0
        && !newSubClusterIds.empty()) {
        FullCemSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                             nFullDims, &newSubClusterIds, reprobeDepth + 1);
    }
}


// ---------------------------------------------------------------------------
// RefractorySplitPerChunk
// ---------------------------------------------------------------------------
// Physiologically-motivated splitting phase, run after SubspaceReclusterPerChunk.
//
// Rationale: a single neuron cannot fire twice within the absolute refractory
// period (~1–1.5 ms).  Any cluster with ISI violations in that window is a
// mixture of at least two units.  This phase exploits that constraint directly:
//
//   1. For each cluster, sort its spike timestamps (normalised time dimension).
//   2. Find all refractory violations: pairs (i,j) with |t_i - t_j| < refractNorm.
//   3. Compute the contamination rate = 2 * n_violations / n_spikes.
//   4. If rate >= minContamRate AND n_spikes >= minSplitSpikes:
//      a. Partition spikes into "violator" set (at least one in a violating pair)
//         and "clean" set (no refractory violation partner).
//      b. Seed two-cluster CEM using violator centroid vs clean centroid.
//      c. Run full-space CEM in the normalised nFullDims space with splits enabled.
//      d. Accept the split only if multi-cluster BIC < single-cluster BIC.
//
// This handles cases SubspaceRecluster misses because:
//   a. The mixture of two units with similar but distinct waveforms may not
//      produce a bimodal subspace projection, yet refractory violations reveal
//      the contamination directly.
//   b. It works on clusters too small for subspace CEM (minimum 2*nFullDims).
//
// Parameters:
//   refractSamples  — absolute refractory period in raw samples (e.g. 1.5ms
//                     × SamplingRate, typically 49 at 32552 Hz)
//   minContamRate   — contamination rate threshold to trigger a split; 0.01
//                     means ≥1% violation rate (1 violation per 50 clean ISIs)
//   sessionSamples  — full recording length in samples (= timeRawMax-timeRawMin)
// ---------------------------------------------------------------------------
void KK::RefractorySplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int   nFullDims,
    float refractSamples,
    float minContamRate,
    float sessionSamples, int onlyChunk)
{
    // Bound any single CEM call during this phase to Phase2SplitTimeLimitSec
    // seconds (checked per iteration inside RunEMLoop).  RAII so every return
    // path restores the previous value.  0 = unlimited (default).
    struct CemLimitGuard {
        double prev;
        explicit CemLimitGuard(float v)
            : prev(KK::s_cemCallTimeLimitSec.load(std::memory_order_relaxed)) {
            if (v > 0.0f)
                KK::s_cemCallTimeLimitSec.store(static_cast<double>(v),
                                                std::memory_order_relaxed);
        }
        ~CemLimitGuard() {
            KK::s_cemCallTimeLimitSec.store(prev, std::memory_order_relaxed);
        }
    } _cemLimitGuard(Phase2SplitTimeLimitSec);

    if (refractSamples <= 0.0f || sessionSamples <= 0.0f) return;

    const int nSpatial    = nFullDims - 1;   // dimensions excluding time
    const int timeDimIdx  = nFullDims - 1;   // normalised time is last dim
    const float refractNorm = refractSamples / sessionSamples;  // in [0,1]

    // Minimum spike count for a 2-cluster fit: need enough in each half for
    // a stable nFullDims-dimensional Gaussian.  nFullDims*4 is conservative.
    const int minSplitSpikes = std::max(nFullDims * 4, 20);

    const int nCh = static_cast<int>(chunkPoints.size());
    int totalSplit = 0;

    // ── Diagnostic accounting ────────────────────────────────────────────────
    // Mirrors the accounting in SubspaceReclusterPerChunk so the user can see
    // the filter funnel: how many candidate clusters were visited, how many
    // were filtered before the CEM call, and why.  Printed at end-of-phase
    // to stderr regardless of `Log`.
    int nVisited      = 0;   // alive non-noise clusters considered
    int nSkipNoise    = 0;
    int nSkipTooSmall = 0;   // nMem < minSplitSpikes
    int nSkipLowContam= 0;   // contamRate < minContamRate (intentional: no need to split)
    int nAttempted    = 0;   // ran the CEM split trial
    int nRejNoSplit   = 0;   // CEM didn't split the cluster
    int nRejWorseNull = 0;   // splitScore >= nullScore

    // ── Progress tracking ─────────────────────────────────────────────────────
    // The loop is parallel with dynamic scheduling, so chunks finish out of
    // order and a per-chunk line would interleave uninformatively.  Instead
    // report aggregate progress from a shared atomic counter at ~5% milestones;
    // LockedStderr serialises each line, so the output stays readable under any
    // thread count.
    const int        nThreads  = omp_get_max_threads();
    const int        progStep  = std::max(1, nCh / 20);
    std::atomic<int> chunksDone{0};
    if (onlyChunk < 0) LockedStderr("[Stage 2.3]   refractory split: %d chunk%s across %d thread%s\n",
                 nCh, nCh == 1 ? "" : "s", nThreads, nThreads == 1 ? "" : "s");

    const int _ckLo = (onlyChunk >= 0) ? onlyChunk : 0;
    const int _ckHi = (onlyChunk >= 0) ? onlyChunk + 1 : nCh;
    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) \
        reduction(+:totalSplit,nVisited,nSkipNoise,nSkipTooSmall, \
                    nSkipLowContam,nAttempted,nRejNoSplit,nRejWorseNull)
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        // Deterministic per-chunk RNG seed (KlustaKwik.h): the refractory split
        // trials below draw randomness, so key the stream to chunk identity --
        // reproducible and independent of thread count/schedule.
        kk_seed_rng(kk_mix_seed(static_cast<uint64_t>(RandomSeed), static_cast<uint64_t>(ck)));

        // Aggregate progress.  Counted as each chunk is picked up; with dynamic
        // chunk-size-1 scheduling that tracks completion to within nThreads.
        // Each post-increment value is unique, so a milestone fires exactly once.
        {
            const int done = chunksDone.fetch_add(1, std::memory_order_relaxed) + 1;
            if (done == nCh || done % progStep == 0)
                LockedStderr("[Stage 2.3]   refractory split: %d/%d chunks (%d%%)\n",
                             done, nCh, (100 * done) / nCh);
        }

        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;

        std::vector<ChunkModel> snapshot = mdls;
        for (const auto& origCm : snapshot) {
            const int lc = origCm.localClusterId;
            if (lc == 0) { ++nSkipNoise; continue; }
            ++nVisited;

            // Collect member local-indices and their normalised timestamps
            std::vector<int>   members;
            std::vector<float> ts;
            for (int i = 0; i < nPts; i++) {
                if (cls[static_cast<size_t>(i)] != lc) continue;
                members.push_back(i);
                ts.push_back(Data[pts[static_cast<size_t>(i)] * nDims + timeDimIdx]);
            }
            const int nMem = static_cast<int>(members.size());
            if (nMem < minSplitSpikes) { ++nSkipTooSmall; continue; }

            // Sort by time for efficient ISI scan
            std::vector<int> order(static_cast<size_t>(nMem));
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(),
                      [&ts](int a, int b){ return ts[static_cast<size_t>(a)]
                                                < ts[static_cast<size_t>(b)]; });

            // Count refractory violations (forward scan: consecutive sorted spikes)
            // and mark which spikes are involved in at least one violation.
            std::vector<bool> isViolator(static_cast<size_t>(nMem), false);
            int nViol = 0;
            for (int ii = 0; ii < nMem - 1; ii++) {
                const int a = order[static_cast<size_t>(ii)];
                const int b = order[static_cast<size_t>(ii + 1)];
                if ((ts[static_cast<size_t>(b)] - ts[static_cast<size_t>(a)])
                        < refractNorm) {
                    isViolator[static_cast<size_t>(a)] = true;
                    isViolator[static_cast<size_t>(b)] = true;
                    ++nViol;
                }
            }

            const float contamRate = 2.0f * static_cast<float>(nViol)
                                   / static_cast<float>(nMem);
            if (contamRate < minContamRate) { ++nSkipLowContam; continue; }
            ++nAttempted;

            // Log the violation
            if (Verbose >= 2)
                Output("  RefractorySplit: chunk%d cluster%d  %d spikes  "
                       "%.1f%% ISI contamination (%.1fms refract)\n",
                       ck, lc, nMem, contamRate * 100.0f,
                       refractSamples / (SamplingRate > 0.0f ? SamplingRate : 30000.0f) * 1000.0f);

            // Partition: violators vs clean.
            // If all spikes are violators (highly contaminated), fall back to
            // median split by first spatial dimension.
            std::vector<int> violIdx, cleanIdx;
            for (int ii = 0; ii < nMem; ii++) {
                if (isViolator[static_cast<size_t>(ii)]) violIdx.push_back(ii);
                else                                      cleanIdx.push_back(ii);
            }
            if (violIdx.empty() || cleanIdx.empty()) {
                // Fallback: ignore the degenerate viol/clean partition and split
                // by the median of the first spatial feature.  Both vectors MUST
                // be reset first — otherwise the non-empty side keeps its entries
                // and the median pass appends duplicates, corrupting the centroids
                // (this path triggers on all-violator clusters, exactly the most
                // contaminated ones the phase targets).
                violIdx.clear();
                cleanIdx.clear();
                std::vector<float> vals(static_cast<size_t>(nMem));
                for (int ii = 0; ii < nMem; ii++)
                    vals[static_cast<size_t>(ii)] =
                        Data[pts[static_cast<size_t>(members[static_cast<size_t>(ii)])]
                             * nDims + 0];
                std::nth_element(vals.begin(), vals.begin() + nMem/2, vals.end());
                const float med = vals[static_cast<size_t>(nMem / 2)];
                for (int ii = 0; ii < nMem; ii++) {
                    if (Data[pts[static_cast<size_t>(members[static_cast<size_t>(ii)])]
                             * nDims + 0] < med)
                        cleanIdx.push_back(ii);
                    else
                        violIdx.push_back(ii);
                }
                // A degenerate spatial dim (e.g. all values == median) can leave
                // one side empty, which would divide the centroid by zero below.
                // Two groups can't be formed — treat as "no split".
                if (violIdx.empty() || cleanIdx.empty()) {
                    if (Verbose >= 2)
                        Output("  RefractorySplit: chunk%d cluster%d — median fallback "
                               "could not form two groups, skipping\n", ck, lc);
                    ++nRejNoSplit;
                    continue;
                }
            }

            // Compute centroids of violator and clean sets in normalised nFullDims space
            std::vector<float> centViol(static_cast<size_t>(nFullDims), 0.0f);
            std::vector<float> centClean(static_cast<size_t>(nFullDims), 0.0f);
            for (int ii : violIdx) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    centViol[static_cast<size_t>(d)] += Data[p * nDims + d];
            }
            for (int ii : cleanIdx) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    centClean[static_cast<size_t>(d)] += Data[p * nDims + d];
            }
            for (float& v : centViol)  v /= static_cast<float>(violIdx.size());
            for (float& v : centClean) v /= static_cast<float>(cleanIdx.size());

            // Build scratch KK for this cluster's spikes in full nFullDims space
            KK Ks;
            Ks.nDims = nFullDims; Ks.nPoints = nMem;
            Ks.penaltyMix = penaltyMix; Ks.suppressBestSave = true;
            Ks.minClustersAlive = 1; Ks.AllocateArrays(); Ks.AllocateCholeskyVecs();
            Ks.timeRawMin = timeRawMin; Ks.timeRawMax = timeRawMax;

            // The scratch KK is allocated above.  Build the null (single-
            // cluster) model, then the split model.  (A duplicate centroid-
            // seed setup here was dead: ReinitForSplit below resets
            // Class/ClassAlive, and the split block re-fills Data and re-sets
            // Centres before use.)
            // Null score (single cluster)
            Ks.ReinitForSplit(nMem, nFullDims, penaltyMix);
            for (int ii = 0; ii < nMem; ii++) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    Ks.Data[ii * nFullDims + d] = Data[p * nDims + d];
            }
            Ks.nStartingClusters = 1; Ks.NoisePoint = 0;
            for (int ii = 0; ii < nMem; ii++) Ks.Class[ii] = 1;
            Ks.ClassAlive[1] = 1; Ks.nClustersAlive = 1; Ks.AliveIndex[0] = 1;
            Ks.MStep(); Ks.EStep();
            const float nullScore = Ks.ComputeScore();

            // Multi-cluster CEM seeded from centroids
            Ks.ReinitForSplit(nMem, nFullDims, penaltyMix);
            for (int ii = 0; ii < nMem; ii++) {
                const int p = pts[static_cast<size_t>(members[static_cast<size_t>(ii)])];
                for (int d = 0; d < nFullDims; d++)
                    Ks.Data[ii * nFullDims + d] = Data[p * nDims + d];
            }
            // Re-place centroid seeds
            Ks.Centres.SetSize(2 * nFullDims);
            for (int d = 0; d < nFullDims; d++) {
                Ks.Centres[0 * nFullDims + d] = centClean[static_cast<size_t>(d)];
                Ks.Centres[1 * nFullDims + d] = centViol[static_cast<size_t>(d)];
            }
            Ks.nStartingClusters = 3; Ks.NoisePoint = 0;
            for (int c = 0; c < MaxPossibleClusters; c++)
                Ks.ClassAlive[c] = (c < 3) ? 1 : 0;
            Ks.Reindex();
            Ks.InitClassFromCentres(nSpatial);
            Ks.timeRawMin = timeRawMin; Ks.timeRawMax = timeRawMax;

            const float splitScore = Ks.RunEMLoop(
                /*enableSplits=*/   true,
                /*enableDistDump=*/ false,
                /*maxIter=*/        RefractorySplitMaxIter,
                /*phaseLabel=*/     "rfsplit");

            // nClustersAlive INCLUDES the noise slot (always alive even when
            // empty under NoisePoint=0).  A genuine 2-way split has noise +
            // ≥2 real clusters = nClustersAlive >= 3.  The previous `<= 1`
            // guard let the no-split case (noise + 1 real) through, which
            // then failed the splitScore < nullScore comparison anyway —
            // but the rejection was logged as "no improvement" rather than
            // "no split", masking what actually happened.
            if (Ks.nClustersAlive <= 2) {
                if (Verbose >= 2)
                    Output("  RefractorySplit: chunk%d cluster%d — CEM did not split "
                           "(splitScore=%.4g, null=%.4g)\n",
                           ck, lc, splitScore, nullScore);
                ++nRejNoSplit;
                continue;
            }
            if (splitScore >= nullScore) {
                if (Verbose >= 2)
                    Output("  RefractorySplit: chunk%d cluster%d — split worse than null "
                           "(splitScore=%.4g, null=%.4g), keeping\n",
                           ck, lc, splitScore, nullScore);
                ++nRejWorseNull;
                continue;
            }

            // Apply split
            if (Verbose >= 2)
                Output("  RefractorySplit: chunk%d cluster%d -> %d sub-clusters "
                       "(splitScore=%.4g < null=%.4g)\n",
                       ck, lc, Ks.nClustersAlive, splitScore, nullScore);

            // Find next free local ID
            int nextLocalId = 0;
            for (const auto& cm : mdls)
                if (cm.localClusterId > nextLocalId) nextLocalId = cm.localClusterId;
            nextLocalId++;

            std::unordered_map<int,int> subToLocal;
            subToLocal[Ks.Class[0]] = lc;  // first sub-cluster keeps the original ID
            for (int ii = 0; ii < nMem; ii++) {
                int sc = Ks.Class[ii];
                if (!subToLocal.count(sc)) subToLocal[sc] = nextLocalId++;
            }

            for (int ii = 0; ii < nMem; ii++)
                cls[static_cast<size_t>(members[static_cast<size_t>(ii)])] =
                    subToLocal[Ks.Class[ii]];

            // P2.G: per-sub-cluster shift-probe loop deleted (same as
            // SubspaceReclusterPerChunk above) — was building `sub` only to
            // discard it.

            // Remove old model entry and build new ones
            mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
                [lc](const ChunkModel& m){ return m.localClusterId == lc; }),
                mdls.end());

            for (auto& [sc2, newLc] : subToLocal) {
                ChunkModel cm;
                cm.chunkIdx = ck; cm.localClusterId = newLc;
                cm.globalClusterId = -1; cm.nMembers = 0;
                cm.mean.assign(static_cast<size_t>(nFullDims), 0.0f);
                cm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
                for (int ii = 0; ii < nPts; ii++) {
                    if (cls[static_cast<size_t>(ii)] != newLc) continue;
                    const int p2 = pts[static_cast<size_t>(ii)];
                    for (int d = 0; d < nFullDims; d++)
                        cm.mean[static_cast<size_t>(d)] += Data[p2 * nDims + d];
                    cm.nMembers++;
                }
                if (cm.nMembers > 0)
                    for (float& v : cm.mean) v /= cm.nMembers;
                for (int ii = 0; ii < nPts; ii++) {
                    if (cls[static_cast<size_t>(ii)] != newLc) continue;
                    const int p2 = pts[static_cast<size_t>(ii)];
                    for (int r = 0; r < nFullDims; r++)
                        for (int cc2 = r; cc2 < nFullDims; cc2++) {
                            float dr = Data[p2 * nDims + r]   - cm.mean[static_cast<size_t>(r)];
                            float dc = Data[p2 * nDims + cc2] - cm.mean[static_cast<size_t>(cc2)];
                            cm.cov[r * nFullDims + cc2] += dr * dc;
                        }
                }
                if (cm.nMembers > 1)
                    for (float& v : cm.cov) v /= static_cast<float>(cm.nMembers - 1);
                mdls.push_back(std::move(cm));
            }
            ++totalSplit;
        }
    }

    // End-of-phase stderr summary — always visible regardless of Log setting.
    if (onlyChunk < 0) LockedStderr(
            "[Stage 2.3] Per-chunk refractory split: %d split / %d attempted "
            "(visited %d, skipped: %d too-small <%d / %d low-contam <%.0f%%; "
            "rejected: %d no-split / %d worse-than-null)\n",
            totalSplit, nAttempted,
            nVisited, nSkipTooSmall, minSplitSpikes,
            nSkipLowContam, minContamRate * 100.0f,
            nRejNoSplit, nRejWorseNull);
}



// ---------------------------------------------------------------------------
// KK::KnnSplitPerChunk  (Phase 2b.5)
// ---------------------------------------------------------------------------
// K-template chunk split.  After Phase 2b has converged each chunk's
// classification, identify the K best-isolated clusters in the chunk as
// reference templates and partition the remaining "source" clusters'
// spikes by their nearest-template assignment.  Each (source, ref)
// bucket of sufficient size becomes a NEW chunk-local cluster; Phase 6
// cross-chunk template matching consolidates the new clusters into
// global units.
//
// The point: when Phase 2b's ConsiderDeletion leaves behind clusters
// with high residual variance (mixtures or drift-tail fragments), the
// parametric merge decision is unreliable precisely because the source
// cluster's own Σ estimate is bad.  K-template nearest-mean bypasses
// the bad covariance entirely — it just asks "which good template is
// each spike closest to?" — and the partition that emerges tells Phase 6
// what the source cluster ACTUALLY was a mixture of.  No spike is
// assigned to an existing reference cluster; the algorithm only
// generates new clusters (consistent with the user-side GUI version,
// see KlustersDoc::splitClusterByKnnVsReferences, patch99).
//
// Performance: brute-force O(nSrc × K × nSpatial) per chunk.  For a
// 5000-spike chunk with 2000 source spikes, K=10, 22 spatial dims:
// ~440k FLOPs — under 0.1 ms single-threaded.  No OMP parallel needed
// at this scale; the outer chunk loop is already serial-friendly.
// ---------------------------------------------------------------------------
void KK::KnnSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims, int onlyChunk)
{
    if (KnnSplitK < 2) return;

    const int K                   = KnnSplitK;
    const int minRefSize          = KnnSplitMinRefSize;
    const int minSourceSize       = KnnSplitMinSourceSize;
    const int minNewClusterSize   = KnnSplitMinNewClusterSize;
    const int nSpatial            = nFullDims - 1;     // exclude time dim
    const int nCh                 = static_cast<int>(chunkPoints.size());

    // ── Diagnostic accounting ────────────────────────────────────────────────
    // Mirrors the funnel-style accounting in SubspaceReclusterPerChunk /
    // RefractorySplitPerChunk so the user can see how many clusters
    // were considered and where the filter dropped them.
    int chunksTotal           = 0;
    int chunksProcessed       = 0;
    int chunksSkippedNoRefs   = 0;
    int chunksSkippedNoSrc    = 0;
    int refClustersUsed       = 0;
    int sourceClustersVisited = 0;
    int sourceClustersSplit   = 0;
    int newClustersGenerated  = 0;
    int spikesReassigned      = 0;

    if (onlyChunk < 0) LockedStderr(
        "[Stage 2.10] KnnSplitPerChunk: K=%d, minRefSize=%d, "
        "minSourceSize=%d, minNewClusterSize=%d\n",
        K, minRefSize, minSourceSize, minNewClusterSize);

    const int _ckLo = (onlyChunk >= 0) ? onlyChunk : 0;
    const int _ckHi = (onlyChunk >= 0) ? onlyChunk + 1 : nCh;
    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) \
        reduction(+:chunksTotal,chunksProcessed,chunksSkippedNoRefs,chunksSkippedNoSrc, \
                    refClustersUsed,sourceClustersVisited,sourceClustersSplit, \
                    newClustersGenerated,spikesReassigned)
    for (int ck = _ckLo; ck < _ckHi; ck++) {
        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;
        chunksTotal++;

        // 1. Compute trace(Σ)/nSpatial for every non-noise non-empty
        //    chunk-local cluster, and index ChunkModels by localClusterId.
        std::map<int, float>  traceByLocal;
        std::map<int, int>    nByLocal;
        std::map<int, size_t> mdlIdxByLocal;  // index into mdls[]
        for (size_t k = 0; k < mdls.size(); ++k) {
            const auto& cm = mdls[k];
            if (cm.localClusterId == 0) continue;
            if (cm.nMembers == 0) continue;
            float tr = 0.0f;
            for (int j = 0; j < nSpatial; ++j)
                tr += cm.cov[static_cast<size_t>(j) * nFullDims + j];
            traceByLocal[cm.localClusterId] = tr / std::max(1, nSpatial);
            nByLocal[cm.localClusterId] = cm.nMembers;
            mdlIdxByLocal[cm.localClusterId] = k;
        }

        // 2. Reference selection: nbSpikes ≥ minRefSize AND trace below
        //    the chunk median (the "low variance" criterion).  Sort by
        //    trace ascending and take top K.
        std::vector<int>   sizeOkCandidates;
        std::vector<float> allTraces;
        for (const auto& kv : traceByLocal) allTraces.push_back(kv.second);
        if (allTraces.size() < 2) { chunksSkippedNoRefs++; continue; }
        std::sort(allTraces.begin(), allTraces.end());
        const float medianTrace = allTraces[allTraces.size() / 2];

        for (const auto& kv : nByLocal)
            if (kv.second >= minRefSize
                && traceByLocal[kv.first] < medianTrace)
                sizeOkCandidates.push_back(kv.first);

        if (sizeOkCandidates.size() < 2) { chunksSkippedNoRefs++; continue; }

        std::sort(sizeOkCandidates.begin(), sizeOkCandidates.end(),
                  [&](int a, int b) {
                      return traceByLocal[a] < traceByLocal[b];
                  });
        if (static_cast<int>(sizeOkCandidates.size()) > K)
            sizeOkCandidates.resize(K);
        const int nRefs = static_cast<int>(sizeOkCandidates.size());
        const std::vector<int>& refs = sizeOkCandidates;

        // Pre-cache reference mean pointers — stable because mdls[] is
        // not mutated during the partitioning loop (we only push_back
        // new entries afterwards, see step 6).
        std::vector<const float*> refMeans(nRefs);
        for (int r = 0; r < nRefs; ++r)
            refMeans[r] = mdls[mdlIdxByLocal[refs[r]]].mean.data();

        // 3. Source selection: every other non-noise cluster with
        //    nbSpikes ≥ minSourceSize.  No upper-percentile filter — once
        //    refs are fixed, anything not in the ref set with enough
        //    spikes is a candidate.
        std::set<int> refSet(refs.begin(), refs.end());
        std::vector<int> sources;
        for (const auto& kv : nByLocal)
            if (!refSet.count(kv.first) && kv.second >= minSourceSize)
                sources.push_back(kv.first);
        if (sources.empty()) { chunksSkippedNoSrc++; continue; }
        sourceClustersVisited += static_cast<int>(sources.size());

        // 4. Next free local cluster ID — new clusters appended at tail.
        int nextLocalId = 0;
        for (const auto& cm : mdls)
            nextLocalId = std::max(nextLocalId, cm.localClusterId);
        nextLocalId++;

        // 5. Per source-cluster, partition each spike by nearest template.
        //    bucket[srcId][refIdx] = LOCAL chunk-relative indices.
        std::map<int, std::vector<std::vector<int>>> bucket;
        for (int srcId : sources) bucket[srcId].resize(nRefs);
        std::set<int> sourceSet(sources.begin(), sources.end());

        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (!sourceSet.count(c)) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            float bestD = std::numeric_limits<float>::max();
            int   bestR = 0;
            for (int r = 0; r < nRefs; ++r) {
                const float* rm = refMeans[r];
                float d = 0.0f;
                for (int j = 0; j < nSpatial; ++j) {
                    const float diff = x[j] - rm[j];
                    d += diff * diff;
                }
                if (d < bestD) { bestD = d; bestR = r; }
            }
            bucket[c][bestR].push_back(i);
        }

        // 6. Materialize buckets ≥ minNewClusterSize as new chunk-local
        //    clusters.  Smaller buckets leave their spikes in source.
        bool anySplit = false;
        for (int srcId : sources) {
            int srcSplit = 0;
            for (int r = 0; r < nRefs; ++r) {
                auto& indices = bucket[srcId][r];
                if (static_cast<int>(indices.size()) < minNewClusterSize)
                    continue;

                const int newId = nextLocalId++;
                for (int i : indices) cls[i] = newId;

                ChunkModel newCm{};
                newCm.chunkIdx        = ck;
                newCm.localClusterId  = newId;
                newCm.globalClusterId = -1;
                newCm.nMembers        = static_cast<int>(indices.size());
                newCm.mean.assign(nFullDims, 0.0f);
                newCm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
                mdls.push_back(newCm);

                spikesReassigned     += static_cast<int>(indices.size());
                newClustersGenerated++;
                srcSplit++;
                anySplit = true;
            }
            if (srcSplit > 0) sourceClustersSplit++;
        }

        if (!anySplit) continue;
        refClustersUsed += nRefs;
        chunksProcessed++;

        // 7. Rebuild .mean and .nMembers for every cluster in this chunk
        //    from the updated label assignment.  Cov is left zero-filled
        //    for new clusters — Phase 2c alignment and Phase 3 template
        //    harvest only read .mean here; Phase 6 recomputes its own
        //    statistics from the realigned waveforms.
        std::map<int, int>                  newN;
        std::map<int, std::vector<double>>  newMeanAcc;
        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (c == 0) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            newN[c]++;
            auto& acc = newMeanAcc[c];
            if (acc.empty()) acc.assign(nFullDims, 0.0);
            for (int j = 0; j < nFullDims; ++j) acc[j] += x[j];
        }
        for (auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            const int n = newN[cm.localClusterId];
            cm.nMembers = n;
            if (n == 0) { cm.mean.assign(nFullDims, 0.0f); continue; }
            const auto& acc = newMeanAcc[cm.localClusterId];
            cm.mean.assign(nFullDims, 0.0f);
            for (int j = 0; j < nFullDims; ++j)
                cm.mean[j] = static_cast<float>(acc[j] / n);
        }
        // Drop emptied source ChunkModels.
        mdls.erase(
            std::remove_if(mdls.begin(), mdls.end(),
                [](const ChunkModel& cm) {
                    return cm.localClusterId != 0 && cm.nMembers == 0;
                }),
            mdls.end());
    }

    if (onlyChunk < 0) LockedStderr(
        "[Stage 2.10] KnnSplitPerChunk: chunks=%d (processed=%d, "
        "skipped[no refs]=%d, skipped[no sources]=%d), "
        "ref clusters used=%d, source clusters visited=%d, "
        "split=%d, new clusters generated=%d, spikes reassigned=%d\n",
        chunksTotal, chunksProcessed, chunksSkippedNoRefs,
        chunksSkippedNoSrc, refClustersUsed, sourceClustersVisited,
        sourceClustersSplit, newClustersGenerated, spikesReassigned);
}


// ---------------------------------------------------------------------------
// KK::WaveKnnSplitPerChunk  (Phase 2b.5, klusters-faithful variant)
// ---------------------------------------------------------------------------
// Replaces KKE's nearest-template KnnSplitPerChunk with the klusters-
// faithful K-nearest-neighbours majority-vote split.  The semantic
// differences (see wave_knn_split.h for full details):
//
//   • Per-spike K-NN against a POOL of reference SPIKES (not means)
//   • Majority-vote threshold (WaveKnnMajorityThreshold) — spikes whose
//     K nearest neighbours don't agree above the threshold stay in
//     source (residual bucket).
//
// The latter is the missing mechanism that bounds fragmentation:
// klusters' algorithm leaves ambiguous spikes alone; KKE's existing
// version force-assigns every source spike to the closest reference,
// producing up to K-way fragmentation per source.  In the sirotaA
// group-6 benchmark this generated 3125 new clusters from 753 sources
// (avg 4.1-way split), most too small for downstream xcorr template-
// matching to behave sanely, leading to cascade collapse-to-noise.
//
// Activated via -KnnSplitPerChunkEnable 1 -KnnSplitMode 1.
// ---------------------------------------------------------------------------
void KK::WaveKnnSplitPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims,
    const std::map<int, std::vector<int>>* sourceAllowlist, int onlyChunk) {
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    // Per-call salt: increments every invocation so the random source
    // shuffle inside wave_knn_split::Run picks a different subset each
    // Phase 4b alternation iter (see m_phase4SplitCallCount doc).
    const unsigned callSalt = m_phase4SplitCallCount++;

    if (onlyChunk < 0) LockedStderr(
        "[Stage 2.10] WaveKnnSplitPerChunk (klusters-faithful): "
        "K=%d majThr=%.2f minRefSize=%d minSourceSize=%d minNewClusterSize=%d\n",
        KnnSplitK, WaveKnnMajorityThreshold,
        KnnSplitMinRefSize, KnnSplitMinSourceSize, KnnSplitMinNewClusterSize);

    int chunksTotal           = 0;
    int chunksCalled          = 0;
    int chunksProcessed       = 0;
    int totalSourcesConsidered = 0;
    int totalSourcesAnisoFiltered = 0;
    int totalSourcesSplit      = 0;
    int totalNewClusters       = 0;
    int totalResidualClusters  = 0;
    int totalSpikesReassigned  = 0;
    int totalSpikesResidual    = 0;

    // ── Per-chunk parallelism (patch 0064) ─────────────────────────────
    // Each iteration writes only to perChunkClass[ck] / perChunkModels[ck]
    // (disjoint slots) and uses local scratch (chunkFeat, chunkLabels,
    // traces, aniso, v, w); wave_knn_split::Run is reentrant (no statics
    // beyond a pure inline helper).  Data[] is read-only.  callSalt was
    // already advanced once before the loop, so per-chunk seeds (ck XOR
    // callSalt-mixed) are deterministic and stable under reordering.
    const double splitDeadline =
        (Phase2SplitTimeLimitSec > 0.0f) ? omp_get_wtime() + Phase2SplitTimeLimitSec : 0.0;
    std::atomic<bool> splitLimitHit{false};

    const int _ckLo = (onlyChunk >= 0) ? onlyChunk : 0;
    const int _ckHi = (onlyChunk >= 0) ? onlyChunk + 1 : nCh;
    #pragma omp parallel for if(onlyChunk < 0) schedule(dynamic) \
        reduction(+:chunksTotal,chunksCalled,chunksProcessed, \
                  totalSourcesConsidered,totalSourcesAnisoFiltered, \
                  totalSourcesSplit,totalNewClusters, \
                  totalResidualClusters,totalSpikesReassigned, \
                  totalSpikesResidual)
    for (int ck = _ckLo; ck < _ckHi; ++ck) {
        if (splitDeadline > 0.0 && omp_get_wtime() > splitDeadline) {
            splitLimitHit.store(true, std::memory_order_relaxed); continue;
        }
        const auto& pts  = chunkPoints[ck];
        auto&       cls  = perChunkClass[ck];
        auto&       mdls = perChunkModels[ck];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0 || mdls.empty()) continue;
        chunksTotal++;

        // Build per-chunk feature scratch: copy the slice of Data
        // corresponding to chunkPoints[ck] into a contiguous buffer the
        // way wave_knn_split expects (nPts × nFullDims, row-major).
        std::vector<float> chunkFeat(static_cast<size_t>(nPts) * nFullDims);
        for (int i = 0; i < nPts; ++i) {
            const float* src = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            std::copy(src, src + nFullDims,
                      chunkFeat.data() + static_cast<size_t>(i) * nFullDims);
        }

        // Build the chunk-local label array (copy in, modify, copy out).
        std::vector<int> chunkLabels(cls.begin(), cls.end());

        // Build trace vector (one entry per non-zero localClusterId).
        // Also compute anisotropy ratio λ_max(Σ)/tr(Σ) via power iteration
        // — this is the mixture detector that gates source candidates
        // when WaveKnnMinSourceAnisotropy > 0.  For nDim=22 a unimodal
        // cluster has anisotropy ≈ 1/22 ≈ 0.045; a clear 2-unit mixture
        // gives 0.3–0.7.
        const int nSpatial = nFullDims - 1;
        std::vector<float> traces;
        std::vector<int>   traceIds;
        std::vector<float> aniso;
        std::vector<int>   anisoIds;
        traces.reserve(mdls.size());
        traceIds.reserve(mdls.size());
        aniso.reserve(mdls.size());
        anisoIds.reserve(mdls.size());

        // Scratch vectors for power iteration (re-used per cluster).
        std::vector<double> v(nSpatial), w(nSpatial);
        for (const auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            if (cm.nMembers == 0) continue;
            float tr = 0.0f;
            for (int j = 0; j < nSpatial; ++j) {
                tr += cm.cov[static_cast<size_t>(j) * nFullDims + j];
            }
            traces.push_back(tr / std::max(1, nSpatial));
            traceIds.push_back(cm.localClusterId);

            // Skip anisotropy if trace is degenerate.
            if (tr <= 1e-20f || nSpatial < 2) continue;

            // Power iteration: λ_max of the upper-triangle-populated
            // cov matrix.  Symmetrise on-the-fly by reading
            // cov[i,j] = cov[min(i,j), max(i,j)].
            auto C = [&](int i, int j) -> float {
                if (i > j) std::swap(i, j);
                return cm.cov[static_cast<size_t>(i) * nFullDims + j];
            };
            // Seed v with normalised diagonal — biases toward the
            // largest-variance axis, converges in ~10-20 iters for the
            // matrices we see.
            double sumAbs = 0.0;
            for (int i = 0; i < nSpatial; ++i) {
                v[i] = std::abs(C(i, i)) + 1e-12;
                sumAbs += v[i] * v[i];
            }
            const double invNorm0 = 1.0 / std::sqrt(sumAbs);
            for (int i = 0; i < nSpatial; ++i) v[i] *= invNorm0;

            double lambda = 0.0;
            const int powerIters = 30;
            for (int it = 0; it < powerIters; ++it) {
                // w = C * v
                for (int i = 0; i < nSpatial; ++i) {
                    double acc = 0.0;
                    for (int j = 0; j < nSpatial; ++j) acc += C(i, j) * v[j];
                    w[i] = acc;
                }
                // λ = ||w||
                double n2 = 0.0;
                for (int i = 0; i < nSpatial; ++i) n2 += w[i] * w[i];
                if (n2 < 1e-40) { lambda = 0.0; break; }
                lambda = std::sqrt(n2);
                const double inv = 1.0 / lambda;
                for (int i = 0; i < nSpatial; ++i) v[i] = w[i] * inv;
            }
            const float ratio = static_cast<float>(lambda / tr);
            aniso.push_back(ratio);
            anisoIds.push_back(cm.localClusterId);
        }

        wave_knn_split::Config cfg;
        cfg.K                          = KnnSplitK;
        cfg.majorityThreshold          = WaveKnnMajorityThreshold;
        cfg.minRefClusterSize          = KnnSplitMinRefSize;
        cfg.minSourceClusterSize       = KnnSplitMinSourceSize;
        cfg.minNewClusterSize          = KnnSplitMinNewClusterSize;
        cfg.useTraceFilter             = (WaveKnnUseTraceFilter != 0);
        cfg.skipMuaCluster1            = (WaveKnnSkipMuaCluster1 != 0);
        cfg.noiseSourceProbability     = WaveKnnNoiseSourceProbability;
        // Vary the RNG seed per chunk so the noiseSourceProbability draw
        // is INDEPENDENT across chunks.  If RandomSeed=0 (time-based),
        // wave_knn_split will use std::time(nullptr); same effect (each
        // chunk gets a different stream because std::time() advances).
        // Otherwise: deterministic, but per-chunk-unique = RandomSeed XOR
        // (chunk_idx * 1000003), a prime offset that avoids low-bit
        // collisions in the mt19937 stream.  ALSO mixed with callSalt
        // (Knuth multiplicative hash) so each Phase 4b iter shuffles a
        // different source order; without callSalt the same clusters
        // were picked every iter (constant spike-relabel count bug).
        cfg.rngSeed                    = (RandomSeed != 0)
            ? static_cast<unsigned>(RandomSeed) ^
              (static_cast<unsigned>(ck) * 1000003u) ^
              (callSalt * 2654435761u)
            : 0u;
        cfg.residualBecomesNewCluster  = (WaveKnnResidualBecomesCluster != 0);
        cfg.minSourceAnisotropy        = WaveKnnMinSourceAnisotropy;
        cfg.maskNeighbors              = (WaveKnnMaskNeighbors != 0);
        cfg.maxSourcesPerCall          = WaveKnnMaxSourcesPerCall;
        cfg.Verbose                    = (Verbose >= 2);

        // If an explicit source allowlist was supplied (quality-weighted
        // dispatch), restrict this chunk's sources to the listed cluster
        // IDs.  A chunk absent from the allowlist is skipped entirely.
        // cfg.sourceIds non-empty makes wave_knn_split::Run use exactly
        // those IDs (intersected with eligibility) as sources, bypassing
        // its own random selection + maxSourcesPerCall cap.
        if (sourceAllowlist) {
            auto it = sourceAllowlist->find(ck);
            if (it == sourceAllowlist->end() || it->second.empty()) continue;
            cfg.sourceIds         = it->second;
            cfg.maxSourcesPerCall = 0;   // allowlist already IS the cap
        }

        if (splitDeadline > 0.0)
            cfg.shouldStop = [&splitLimitHit, splitDeadline]() {
                if (omp_get_wtime() > splitDeadline) {
                    splitLimitHit.store(true, std::memory_order_relaxed);
                    return true;
                }
                return false;
            };

        auto r = wave_knn_split::Run(chunkFeat.data(), nPts, nFullDims,
                                      chunkLabels, traces, traceIds,
                                      aniso, anisoIds, cfg);

        // Always counted: wave_knn_split was invoked on this chunk and
        // reported how many source clusters it considered.  These counters
        // are needed to distinguish "wave_knn_split rejected every
        // candidate" from "wave_knn_split was never called".
        chunksCalled++;
        totalSourcesConsidered    += r.nSourcesConsidered;
        totalSourcesAnisoFiltered += r.nSourcesAnisotropyFiltered;

        if (r.nNewClusters == 0) continue;

        // Identify which labels appeared in chunkLabels that weren't in
        // the original `cls` — these are the new sub-clusters introduced
        // by wave_knn_split (IDs allocated after maxLabel+1).
        std::set<int> oldIds;
        for (const auto& cm : mdls) oldIds.insert(cm.localClusterId);

        // Apply updated labels back to cls.
        for (int i = 0; i < nPts; ++i) cls[i] = chunkLabels[i];

        // Materialise ChunkModels for the new clusters.  Means recomputed
        // below; cov left zero (recomputed by downstream phases).
        std::map<int, int>                 newN;
        std::map<int, std::vector<double>> newMeanAcc;
        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (c == 0) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            newN[c]++;
            auto& acc = newMeanAcc[c];
            if (acc.empty()) acc.assign(nFullDims, 0.0);
            for (int j = 0; j < nFullDims; ++j) acc[j] += x[j];
        }
        // Append entries for new IDs.
        for (const auto& kv : newN) {
            if (oldIds.count(kv.first)) continue;
            ChunkModel newCm{};
            newCm.chunkIdx        = ck;
            newCm.localClusterId  = kv.first;
            newCm.globalClusterId = -1;
            newCm.nMembers        = kv.second;
            newCm.mean.assign(nFullDims, 0.0f);
            newCm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
            mdls.push_back(newCm);
        }
        // Rebuild means + counts for all clusters in the chunk.
        for (auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            const int n = newN[cm.localClusterId];
            cm.nMembers = n;
            if (n == 0) { cm.mean.assign(nFullDims, 0.0f); continue; }
            const auto& acc = newMeanAcc[cm.localClusterId];
            cm.mean.assign(nFullDims, 0.0f);
            for (int j = 0; j < nFullDims; ++j)
                cm.mean[j] = static_cast<float>(acc[j] / n);
        }

        // CRITICAL: rebuild covariances for all clusters with current
        // membership.  Without this, the anisotropy gate (patch 0021)
        // sees zero-cov on every cluster wave_knn_split touched — including
        // newly-created sub-clusters — and admits them all on the next
        // Phase 4b iteration regardless of whether they look like
        // mixtures.  That's the cluster-explosion regression where
        // a session ends up with 10k+ local clusters before merge.
        //
        // Upper-triangle accumulation, divide by n at end (population
        // covariance, matches ChunkReCEM convention).  O(nPts·nDims²/2)
        // per chunk; ~60ms total for the user's session (36 chunks,
        // nFullDims=22, ~7k pts/chunk).
        std::map<int, std::vector<double>> newCovAcc;
        for (int i = 0; i < nPts; ++i) {
            const int c = cls[i];
            if (c == 0) continue;
            const float* x = &Data[static_cast<size_t>(pts[i]) * nFullDims];
            // Find the cluster's mean we just computed (use newMeanAcc/n).
            auto itAcc = newMeanAcc.find(c);
            const int n = newN[c];
            if (itAcc == newMeanAcc.end() || n == 0) continue;
            const auto& meanAcc = itAcc->second;
            auto& covAcc = newCovAcc[c];
            if (covAcc.empty())
                covAcc.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0);
            for (int j = 0; j < nFullDims; ++j) {
                const double dj = static_cast<double>(x[j])
                                - meanAcc[j] / n;
                for (int k = j; k < nFullDims; ++k) {
                    const double dk = static_cast<double>(x[k])
                                    - meanAcc[k] / n;
                    covAcc[static_cast<size_t>(j) * nFullDims + k] += dj * dk;
                }
            }
        }
        // Assign to ChunkModels (upper triangle; symmetric reader assumed).
        for (auto& cm : mdls) {
            if (cm.localClusterId == 0) continue;
            const int n = cm.nMembers;
            if (n == 0) continue;
            auto it = newCovAcc.find(cm.localClusterId);
            if (it == newCovAcc.end()) continue;
            const auto& acc = it->second;
            if (cm.cov.size() != static_cast<size_t>(nFullDims) * nFullDims)
                cm.cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
            const double invN = 1.0 / n;
            for (int j = 0; j < nFullDims; ++j) {
                for (int k = j; k < nFullDims; ++k) {
                    cm.cov[static_cast<size_t>(j) * nFullDims + k] =
                        static_cast<float>(
                            acc[static_cast<size_t>(j) * nFullDims + k] * invN);
                }
            }
        }

        // Drop emptied source ChunkModels.
        mdls.erase(
            std::remove_if(mdls.begin(), mdls.end(),
                [](const ChunkModel& cm) {
                    return cm.localClusterId != 0 && cm.nMembers == 0;
                }),
            mdls.end());

        chunksProcessed++;
        totalSourcesSplit      += r.nSourcesSplit;
        totalNewClusters       += r.nNewClusters;
        totalResidualClusters  += r.nResidualClusters;
        totalSpikesReassigned  += r.nSpikesReassigned;
        totalSpikesResidual    += r.nSpikesResidual;
    }

    if (splitLimitHit.load())
        LockedStderr("[Stage 2.10] WaveKnnSplitPerChunk: time limit (%.0fs) reached — "
                     "some chunks/clusters left unsplit\n", Phase2SplitTimeLimitSec);
    if (onlyChunk < 0) LockedStderr(
        "[Stage 2.10] WaveKnnSplitPerChunk: chunks=%d (called=%d, with-splits=%d), "
        "sources visited=%d, aniso-filtered=%d, split=%d, new clusters=%d "
        "(of which residual=%d), spikes reassigned=%d, "
        "spikes kept-in-source=%d\n",
        chunksTotal, chunksCalled, chunksProcessed,
        totalSourcesConsidered, totalSourcesAnisoFiltered,
        totalSourcesSplit, totalNewClusters,
        totalResidualClusters,
        totalSpikesReassigned, totalSpikesResidual);
}
