/***************************************************************************
                   KK_chunk_merge.cpp
                   ------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Phase 6 of the chunked pipeline: KK::MergeChunkModels, which merges the
 per-chunk cluster models into global clusters, with the feature-space
 soft-warp eigen-residual helpers its Pass-2 gate uses.  Moved verbatim out
 of KK_chunked.cpp.  Member definitions of the KK class declared in KK.h.
 ***************************************************************************/
#include "KK.h"
#include "KK_internal.h"
#include "KlustaKwik.h"
#include <neurosuite/core/dipsplit.hpp>
#include "proxy_isi.h"
#include "clust_quality.h"
#include "realign_xcorr.h"
#include "realign_center.h"
#include "klusters_realign.h"

#include <algorithm>
#include <chrono>
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

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <omp.h>


// ── Phase 6 feature-space soft-warp eigen-residual (testing) ──────────────
// Self-contained; no external linear-algebra dependency.  See
// Phase6EigenResidual* params and the Pass-2 gate in MergeChunkModels.
namespace {

// Cyclic Jacobi eigensolver for a small symmetric matrix A (dim*dim row-major).
// Returns eigenvalues in eval[i] and eigenvector j as column j of V (row-major).
void p6_jacobiSym(std::vector<double> A, int dim,
                  std::vector<double>& eval, std::vector<double>& V) {
    V.assign(static_cast<size_t>(dim) * dim, 0.0);
    for (int i = 0; i < dim; i++) V[static_cast<size_t>(i) * dim + i] = 1.0;
    for (int sweep = 0; sweep < 100; sweep++) {
        double off = 0.0;
        for (int p = 0; p < dim; p++)
            for (int q = p + 1; q < dim; q++)
                off += A[static_cast<size_t>(p)*dim+q] * A[static_cast<size_t>(p)*dim+q];
        if (off < 1e-20) break;
        for (int p = 0; p < dim; p++) {
            for (int q = p + 1; q < dim; q++) {
                double apq = A[static_cast<size_t>(p)*dim+q];
                if (std::fabs(apq) < 1e-300) continue;
                double app = A[static_cast<size_t>(p)*dim+p];
                double aqq = A[static_cast<size_t>(q)*dim+q];
                double phi = 0.5 * std::atan2(2.0*apq, aqq - app);
                double cs = std::cos(phi), sn = std::sin(phi);
                for (int i = 0; i < dim; i++) {
                    double aip = A[static_cast<size_t>(i)*dim+p];
                    double aiq = A[static_cast<size_t>(i)*dim+q];
                    A[static_cast<size_t>(i)*dim+p] = cs*aip - sn*aiq;
                    A[static_cast<size_t>(i)*dim+q] = sn*aip + cs*aiq;
                }
                for (int i = 0; i < dim; i++) {
                    double api = A[static_cast<size_t>(p)*dim+i];
                    double aqi = A[static_cast<size_t>(q)*dim+i];
                    A[static_cast<size_t>(p)*dim+i] = cs*api - sn*aqi;
                    A[static_cast<size_t>(q)*dim+i] = sn*api + cs*aqi;
                }
                for (int i = 0; i < dim; i++) {
                    double vip = V[static_cast<size_t>(i)*dim+p];
                    double viq = V[static_cast<size_t>(i)*dim+q];
                    V[static_cast<size_t>(i)*dim+p] = cs*vip - sn*viq;
                    V[static_cast<size_t>(i)*dim+q] = sn*vip + cs*viq;
                }
            }
        }
    }
    eval.assign(dim, 0.0);
    for (int i = 0; i < dim; i++) eval[i] = A[static_cast<size_t>(i)*dim+i];
}

// Feature-space soft-warp eigen-residual between two cluster Gaussians.
// mean: [dim], cov: [dim*dim] (symmetric).  Bounded movement (c sigma) along
// the top-k pooled eigenmodes; residual weighted by 1/max(lambda, floor).
// Lower = mergeable.  +inf when a cluster is too small to trust its cov.
double p6_eigenResidualFeature(
        const std::vector<float>& meanA, const std::vector<float>& covA, int nA,
        const std::vector<float>& meanB, const std::vector<float>& covB, int nB,
        int k, double c, double floorFrac) {
    const int dim = static_cast<int>(meanA.size());
    if (dim <= 0 || static_cast<int>(meanB.size()) != dim) return INFINITY;
    if (static_cast<int>(covA.size()) != dim*dim ||
        static_cast<int>(covB.size()) != dim*dim) return INFINITY;
    if (nA < dim + 2 || nB < dim + 2) return INFINITY;

    std::vector<double> C(static_cast<size_t>(dim)*dim, 0.0);
    const double wA = static_cast<double>(nA) / (nA + nB);
    const double wB = static_cast<double>(nB) / (nA + nB);
    for (int i = 0; i < dim; i++)
        for (int j = 0; j < dim; j++) {
            double a = 0.5*(covA[static_cast<size_t>(i)*dim+j] + covA[static_cast<size_t>(j)*dim+i]);
            double b = 0.5*(covB[static_cast<size_t>(i)*dim+j] + covB[static_cast<size_t>(j)*dim+i]);
            C[static_cast<size_t>(i)*dim+j] = wA*a + wB*b;
        }
    std::vector<double> lam, V;
    p6_jacobiSym(C, dim, lam, V);

    std::vector<int> ord(dim);
    std::iota(ord.begin(), ord.end(), 0);
    std::sort(ord.begin(), ord.end(), [&](int a, int b){ return lam[a] > lam[b]; });
    double lamMax = (lam[ord[0]] > 0.0) ? lam[ord[0]] : 1.0;
    double floorv = std::max(floorFrac * lamMax, 1e-12);

    std::vector<double> d(dim);
    for (int i = 0; i < dim; i++)
        d[i] = static_cast<double>(meanB[i]) - static_cast<double>(meanA[i]);

    double score2 = 0.0;
    int kk = std::min(k, dim);
    for (int r = 0; r < dim; r++) {
        int e = ord[r];
        double proj = 0.0;
        for (int i = 0; i < dim; i++) proj += V[static_cast<size_t>(i)*dim+e] * d[i];
        double l = lam[e];
        double resid = (r < kk)
            ? std::max(0.0, std::fabs(proj) - c * std::sqrt(std::max(l, 0.0)))
            : std::fabs(proj);
        score2 += resid*resid / std::max(l, floorv);
    }
    int dof = std::max(dim - kk, 1);
    return std::sqrt(score2 / dof);
}
}  // namespace


// ---------------------------------------------------------------------------
int KK::MergeChunkModels(std::vector<ChunkModel>& models,
                          int   nSpatialDims,
                          float mergeThresh,
                          const std::vector<std::unordered_map<int,int>>& overlapVotes)
{
    // ── Phase 6 (redesigned): cross-chunk model matching ──────────────────
    //
    // Two passes, no outer iteration:
    //
    //   Pass 1 (temporal sweep):
    //     For each adjacent chunk pair (k, k+1), use overlapVotes[k] to find
    //     mutual-plurality matches.  This is the AUTHORITATIVE source of
    //     cross-chunk identity — overlap-region spikes were sorted by real
    //     EM in both chunks, so a vote-match is direct evidence of cluster
    //     continuity.  Drift-immune by construction.  Sweeps pairs in
    //     temporal order so a unit's identity propagates through a chain
    //     of contiguous chunks.
    //
    //   Pass 2 (xcorr on leftovers):
    //     "Leftovers" are chunk-clusters that didn't receive any overlap
    //     vote-match in Pass 1 — typically clusters whose chunks have no
    //     overlap configured, or units that fire only in the middle of a
    //     chunk and miss the overlap region.  For each leftover, do a
    //     full N×M xcorr search against every chunk-cluster in any other
    //     chunk, using DIRECTIONAL edge-localised waveforms:
    //
    //         leftover (chunk k) right-edge  ↔  candidate (chunk j) left-edge   when k < j
    //         leftover (chunk k) left-edge   ↔  candidate (chunk j) right-edge  when k > j
    //
    //     The temporally-closest waveforms are used, minimising drift
    //     artefacts.  Falls back to chunk-wide meanWav when an edge
    //     waveform is empty (cluster has fewer than 5 spikes in the edge
    //     window).  Mutual nearest neighbour gating guards against
    //     promiscuous matching.
    //
    // Replaces the previous design which interleaved overlap voting,
    // Mahalanobis MNN, and chunk-wide xcorr per chunk pair, then iterated
    // the whole thing until convergence.  Mahalanobis MNN was filtering out
    // drift-affected merges (same unit, slightly different cov per chunk →
    // inflated symmetric Mahal distance) — the temporal-sweep + edge-waveform
    // xcorr design avoids the problem entirely.
    // ----------------------------------------------------------------------

    (void)nSpatialDims;  // unused (Mahalanobis MNN dropped)
    (void)mergeThresh;   // unused (Mahalanobis MNN dropped)

    const int n = static_cast<int>(models.size());

    // Union-Find with path compression
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> Find = [&](int x) -> int {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto Union = [&](int a, int b) {
        a = Find(a); b = Find(b);
        if (a != b) { parent[b] = a; }
    };

    // ── Merge noise unconditionally to global 0 ──────────────────────────
    int nNoiseMerged = 0, nNoiseChunks = 0, firstNoise = -1;
    for (int i = 0; i < n; i++) {
        if (models[i].localClusterId != 0) continue;
        nNoiseMerged += models[i].nMembers;
        nNoiseChunks++;
        if (firstNoise < 0) { firstNoise = i; continue; }
        Union(firstNoise, i);
    }
    if (firstNoise < 0)
        Output("MergeChunkModels: WARNING — no noise cluster found in any chunk.\n");
    else
        Output("MergeChunkModels: noise — %d spikes across %d chunks merged to global c=0\n",
               nNoiseMerged, nNoiseChunks);

    // ── Index models by chunkIdx ─────────────────────────────────────────
    std::unordered_map<int, std::vector<int>> byChunk;
    for (int i = 0; i < n; i++)
        if (models[i].localClusterId != 0)
            byChunk[models[i].chunkIdx].push_back(i);

    const int maxChunk = byChunk.empty() ? -1 :
        std::max_element(byChunk.begin(), byChunk.end(),
            [](const auto& a, const auto& b){ return a.first < b.first; })->first;

    // ── Pass 1: temporal-order overlap voting ───────────────────────────
    //
    // Mark each chunk-cluster that participated in a vote-match (or
    // vote-confirm — same component already) as "matched", so Pass 2
    // doesn't try to xcorr-match clusters that already have a continuity
    // partner via overlap.
    std::unordered_set<int> matched;   // model indices

    // Confirmed (chunk_pair, model_pair) records from Pass 1.  Harvested
    // alongside the matched/Union ops so we get drift-displacement
    // evidence per adjacent chunk pair for Pass 2's smoothness factor.
    struct ConfirmedPair { int chunkA, chunkB, modelA, modelB; };
    std::vector<ConfirmedPair> confirmedPairs;

    int totalVoteMerges = 0;
    for (int k = 0; k <= maxChunk - 1; k++) {
        auto itA = byChunk.find(k);
        auto itB = byChunk.find(k + 1);
        if (itA == byChunk.end() || itB == byChunk.end()) continue;
        if (k >= static_cast<int>(overlapVotes.size())) continue;
        const auto& votes = overlapVotes[k];
        if (votes.empty()) continue;

        const auto& vecA = itA->second;
        const auto& vecB = itB->second;

        // localClusterId -> model index (within chunk k, k+1)
        std::unordered_map<int,int> localToIdxA, localToIdxB;
        for (int a : vecA) localToIdxA[models[a].localClusterId] = a;
        for (int b : vecB) localToIdxB[models[b].localClusterId] = b;

        // Vote floor: max(3, nOverlapSpikes/500) — sparse clusters still
        // match while very-common noise pairs are filtered out.
        int nOverlapSpikes = 0;
        for (const auto& [key, count] : votes) nOverlapSpikes += count;
        const int voteFloor = std::max(3, nOverlapSpikes / 500);

        // Mutual best-from-each-side.  Also track, per A-cluster, its TOTAL
        // overlap votes and its SECOND-best partner count (patch 0059) so we
        // can report a meaningful agreement fraction and optionally gate on a
        // best-vs-second-best margin.  The old display printed the same
        // per-pair count from both sides (always X/X — see below), which was
        // unanimous by construction and conveyed no confidence information.
        std::unordered_map<int, std::pair<int,int>> bestFromA, bestFromB;
        std::unordered_map<int, int>                totalFromA;   // clsK -> Σ partner votes
        std::unordered_map<int, int>                secondFromA;  // clsK -> 2nd-best count
        for (const auto& [key, count] : votes) {
            const int clsK  = key / MaxPossibleClusters;
            const int clsK1 = key % MaxPossibleClusters;
            totalFromA[clsK] += count;
            auto& bA = bestFromA[clsK];
            if (count > bA.second) {
                secondFromA[clsK] = bA.second;   // old best demoted to 2nd
                bA = {clsK1, count};
            } else if (count > secondFromA[clsK]) {
                secondFromA[clsK] = count;
            }
            auto& bB = bestFromB[clsK1];
            if (count > bB.second) bB = {clsK, count};
        }

        for (const auto& [clsK, topB] : bestFromA) {
            if (topB.second < voteFloor) continue;
            const int clsK1 = topB.first;
            auto itBest = bestFromB.find(clsK1);
            if (itBest == bestFromB.end() || itBest->second.first != clsK) continue;
            auto itA2 = localToIdxA.find(clsK);
            auto itB2 = localToIdxB.find(clsK1);
            if (itA2 == localToIdxA.end() || itB2 == localToIdxB.end()) continue;

            // Meaningful confidence (patch 0059): of clsK's overlap spikes,
            // what fraction co-assigned to clsK1, and by what margin over its
            // next-best partner.  (topB.second == itBest->second.second always,
            // since both reference votes[clsK,clsK1] for a mutual best — that
            // is why the legacy X/X display was uninformative.)
            const int    kTotal = totalFromA[clsK];
            const int    second = secondFromA.count(clsK) ? secondFromA[clsK] : 0;
            const double purity = (kTotal > 0)
                ? static_cast<double>(topB.second) / kTotal : 0.0;

            // Optional gates — default 0.0 preserves prior accept behaviour.
            if (CrossChunkVoteMinFraction > 0.0f
                && purity < static_cast<double>(CrossChunkVoteMinFraction))
                continue;
            if (CrossChunkVoteMinMargin > 0.0f
                && static_cast<double>(topB.second)
                       < (1.0 + static_cast<double>(CrossChunkVoteMinMargin))
                             * static_cast<double>(second))
                continue;

            const int mA = itA2->second;
            const int mB = itB2->second;
            matched.insert(mA);
            matched.insert(mB);
            if (Find(mA) != Find(mB)) {
                Union(mA, mB);
                totalVoteMerges++;
                Output("  vote-match  chunk%d.c%d <-> chunk%d.c%d  %d/%d spikes "
                       "(purity %.2f, 2nd %d)\n",
                       models[mA].chunkIdx, clsK,
                       models[mB].chunkIdx, clsK1,
                       topB.second, kTotal, purity, second);
            } else {
                Output("  vote-confirm chunk%d.c%d <-> chunk%d.c%d  %d/%d spikes "
                       "(purity %.2f, 2nd %d) (already merged)\n",
                       models[mA].chunkIdx, clsK,
                       models[mB].chunkIdx, clsK1,
                       topB.second, kTotal, purity, second);
            }
            // Harvest this confirmed pair so we can later compute the
            // per-adjacent-chunk-pair drift displacement vector.  These
            // come from the authoritative overlap-vote source — direct
            // spike-sorting evidence of cluster continuity across chunks.
            confirmedPairs.push_back({k, k + 1, mA, mB});
        }
    }
    Output("MergeChunkModels: Pass 1 (overlap voting): %d new merges across %d chunk pairs\n",
           totalVoteMerges, std::max(0, maxChunk));

    // ── Build drift table from Pass 1 confirmed matches ─────────────────
    //
    // For each adjacent chunk pair (k, k+1) with ≥ 3 confirmed matches,
    // compute the mean displacement (per feature dim) and the residual
    // RMS scatter of individual match displacements around that mean.
    //
    //   expected_drift(k → k+1) = mean over matches of (mB.mean - mA.mean)
    //   scatter(k → k+1)        = RMS deviation of individual displacements
    //                             from the mean
    //
    // Used by Pass 2 to compute a smoothness factor on each candidate
    // xcorr match:
    //
    //   actual    = mB.mean - mA.mean
    //   deviation = ||actual - expected_drift(A → B)|| / scatter(A → B)
    //   smoothness = exp(-(deviation / CrossChunkDriftSigma)² / 2)
    //   effective_score = xcorr_score × smoothness
    //
    // Semantics: candidates whose displacement matches the population's
    // average drift (smooth trajectory, or coordinated jump from electrode
    // movement) → smoothness ≈ 1 → no penalty.  Solo jumps or
    // wrong-direction displacements → smoothness ≈ 0 → effectively vetoed.
    //
    // For non-adjacent chunk pairs (a, b), expected drift is the sum of
    // adjacent-pair drifts in the chain; scatter combines via variance
    // addition (independent per-pair errors).
    struct DriftEstimate {
        std::vector<double> mean_shift;
        double scatter;
        int    n_matches;
    };
    std::map<std::pair<int,int>, DriftEstimate> driftTable;

    if (CrossChunkDriftSigma > 0.0f && nSpatialDims > 0) {
        // Group confirmed pairs by adjacent chunk pair
        std::map<std::pair<int,int>, std::vector<std::pair<int,int>>> pairsByChunkPair;
        for (const auto& cp : confirmedPairs) {
            pairsByChunkPair[{cp.chunkA, cp.chunkB}].push_back({cp.modelA, cp.modelB});
        }

        for (const auto& [chunkPair, modelPairs] : pairsByChunkPair) {
            if (static_cast<int>(modelPairs.size()) < 3) continue;

            const int D = nSpatialDims;
            std::vector<double> mean_shift(D, 0.0);
            std::vector<std::vector<double>> displacements;
            displacements.reserve(modelPairs.size());

            for (const auto& [mA, mB] : modelPairs) {
                const auto& mvA = models[mA].mean;
                const auto& mvB = models[mB].mean;
                if (static_cast<int>(mvA.size()) < D ||
                    static_cast<int>(mvB.size()) < D) continue;
                std::vector<double> disp(D);
                for (int j = 0; j < D; j++) {
                    disp[j] = static_cast<double>(mvB[j])
                            - static_cast<double>(mvA[j]);
                    mean_shift[j] += disp[j];
                }
                displacements.push_back(std::move(disp));
            }
            if (displacements.size() < 3) continue;
            const double invN = 1.0 / displacements.size();
            for (int j = 0; j < D; j++) mean_shift[j] *= invN;

            // Residual RMS scatter (Euclidean norm of (disp - mean_shift))
            double sum_dev2 = 0.0;
            for (const auto& disp : displacements) {
                double r2 = 0.0;
                for (int j = 0; j < D; j++) {
                    const double r = disp[j] - mean_shift[j];
                    r2 += r * r;
                }
                sum_dev2 += r2;
            }
            const double scatter = std::sqrt(
                sum_dev2 / std::max<size_t>(1, displacements.size() - 1));

            driftTable[chunkPair] = {
                std::move(mean_shift), scatter,
                static_cast<int>(displacements.size())};
        }

        Output("MergeChunkModels: Drift table — %zu adjacent chunk pairs estimated "
               "(from ≥3 confirmed matches each)\n", driftTable.size());
    }

    // Helper: expected drift from chunk ca to chunk cb (signed) and combined
    // scatter.  For adjacent pairs uses the direct table entry; for
    // non-adjacent pairs chains through intermediate adjacent pairs.
    // Returns scatter = -1 if no estimate possible (any link in the chain
    // is missing from the table).
    auto lookupDrift = [&](int ca, int cb, int D)
        -> std::pair<std::vector<double>, double>
    {
        if (ca == cb) return {std::vector<double>(D, 0.0), 0.0};
        const int ci = std::min(ca, cb);
        const int cj = std::max(ca, cb);
        std::vector<double> total(D, 0.0);
        double total_var = 0.0;
        for (int k = ci; k < cj; k++) {
            auto it = driftTable.find({k, k + 1});
            if (it == driftTable.end())
                return {std::vector<double>(D, 0.0), -1.0};
            for (int j = 0; j < D; j++)
                total[j] += it->second.mean_shift[j];
            total_var += it->second.scatter * it->second.scatter;
        }
        if (ca > cb)
            for (double& v : total) v = -v;
        return {std::move(total), std::sqrt(total_var)};
    };

    // ── Phase 5b: affine cross-chunk drift transforms (patch 0063) ─────
    // Generalises the translation-only drift table.  For each adjacent
    // chunk pair (k, k+1), fit an affine map T_k: x → M_k·x + t_k by
    // IRLS-Huber from the Pass-1 confirmed matches' feature-space means.
    // Captures the "constellation rotation/shear" a pure displacement
    // vector cannot — units near probe edges drift differently from those
    // at the centre, producing coherent non-translational structure in
    // the cluster-mean cloud.  Subset of clusters per chunk is fine: the
    // fit only needs m ≥ D+2 confirmed matches per pair (so the affine is
    // identifiable); the transform then applies to ALL clusters, matched
    // or not.  Chain smoothness via Tikhonov on each parameter (M_ij, t_j)
    // sequence along k, since drift is continuous in time.
    //
    // Off by default — when off, the existing translation-only drift table
    // path runs unchanged.  When on, both tables are built and Pass 2's
    // smoothness gate prefers the transform.
    struct ChunkTransform {
        std::vector<double> M;       // D*D row-major (M[d*D+r] is M_{d,r})
        std::vector<double> t;       // D
        double              scatter; // robust residual scale (Huber MAD)
        int                 n_matches;
    };
    std::map<std::pair<int,int>, ChunkTransform> transformTable;

    if (CrossChunkTransformDriftEnable != 0 &&
        CrossChunkDriftSigma > 0.0f && nSpatialDims > 0)
    {
        const int D       = nSpatialDims;
        const int Dp1     = D + 1;
        const int minMatch = std::max(D + 2,
            std::max(3, CrossChunkTransformMinMatches));

        // Group Pass-1 confirmed pairs by adjacent chunk pair.
        std::map<std::pair<int,int>, std::vector<std::pair<int,int>>> pbcp;
        for (const auto& cp : confirmedPairs)
            pbcp[{cp.chunkA, cp.chunkB}].push_back({cp.modelA, cp.modelB});

        // In-place Cholesky: G (Dp1 × Dp1, row-major) → L (lower).
        auto cholFactor = [](std::vector<double>& G, int N) -> bool {
            for (int i = 0; i < N; ++i) {
                for (int j = 0; j <= i; ++j) {
                    double s = G[i*N + j];
                    for (int k = 0; k < j; ++k)
                        s -= G[i*N + k] * G[j*N + k];
                    if (i == j) {
                        if (s <= 0.0) return false;
                        G[i*N + j] = std::sqrt(s);
                    } else {
                        G[i*N + j] = s / G[j*N + j];
                    }
                }
                // zero the strict upper for safety
                for (int j = i + 1; j < N; ++j) G[i*N + j] = 0.0;
            }
            return true;
        };
        // Solve L L^T x = b in-place on x.
        auto cholSolve = [](const std::vector<double>& L,
                            std::vector<double>& x, int N) {
            for (int i = 0; i < N; ++i) {
                double s = x[i];
                for (int k = 0; k < i; ++k) s -= L[i*N + k] * x[k];
                x[i] = s / L[i*N + i];
            }
            for (int i = N - 1; i >= 0; --i) {
                double s = x[i];
                for (int k = i + 1; k < N; ++k) s -= L[k*N + i] * x[k];
                x[i] = s / L[i*N + i];
            }
        };

        const double kH = std::max(0.1,
            static_cast<double>(CrossChunkTransformHuberK));
        const int maxIRLS = std::max(1, CrossChunkTransformIRLSIters);

        // Fit one chunk pair.
        auto fitPair = [&](const std::vector<double>& Aflat,
                           const std::vector<double>& Bflat,
                           int m, ChunkTransform& out) -> bool {
            std::vector<double> w(m, 1.0);
            std::vector<double> M_aug(Dp1 * D, 0.0);    // [M^T ; t^T]
            double scale = 1.0;

            for (int it = 0; it < maxIRLS; ++it) {
                // Build G = X^T W X (lower triangle) and H = X^T W B.
                std::vector<double> G(Dp1 * Dp1, 0.0);
                std::vector<double> H(Dp1 * D,   0.0);
                for (int i = 0; i < m; ++i) {
                    const double wi = w[i];
                    // G: only lower triangle (r ≥ c)
                    for (int r = 0; r < D; ++r) {
                        const double ar = Aflat[i*D + r];
                        for (int c = 0; c <= r; ++c)
                            G[r*Dp1 + c] += wi * ar * Aflat[i*D + c];
                        for (int d = 0; d < D; ++d)
                            H[r*D + d] += wi * ar * Bflat[i*D + d];
                    }
                    // last row of G (the bias row): r = D
                    for (int c = 0; c < D; ++c)
                        G[D*Dp1 + c] += wi * Aflat[i*D + c];
                    G[D*Dp1 + D] += wi;
                    for (int d = 0; d < D; ++d) H[D*D + d] += wi * Bflat[i*D + d];
                }

                if (!cholFactor(G, Dp1)) return false;
                // Solve for each output column.
                for (int d = 0; d < D; ++d) {
                    std::vector<double> x(Dp1);
                    for (int r = 0; r < Dp1; ++r) x[r] = H[r*D + d];
                    cholSolve(G, x, Dp1);
                    for (int r = 0; r < Dp1; ++r) M_aug[r*D + d] = x[r];
                }

                // Residuals → robust scale (MAD) → Huber weights.
                std::vector<double> res(static_cast<size_t>(m));
                for (int i = 0; i < m; ++i) {
                    double r2 = 0.0;
                    for (int d = 0; d < D; ++d) {
                        double pred = M_aug[D*D + d];     // t[d]
                        for (int r = 0; r < D; ++r)
                            pred += M_aug[r*D + d] * Aflat[i*D + r];
                        const double e = Bflat[i*D + d] - pred;
                        r2 += e * e;
                    }
                    res[static_cast<size_t>(i)] = std::sqrt(r2);
                }
                std::vector<double> srt = res;
                std::nth_element(srt.begin(),
                                 srt.begin() + m/2, srt.end());
                const double med = srt[static_cast<size_t>(m/2)];
                std::vector<double> ad(static_cast<size_t>(m));
                for (int i = 0; i < m; ++i)
                    ad[static_cast<size_t>(i)] =
                        std::abs(res[static_cast<size_t>(i)] - med);
                std::nth_element(ad.begin(),
                                 ad.begin() + m/2, ad.end());
                scale = 1.4826 * ad[static_cast<size_t>(m/2)];
                if (scale < 1e-9) scale = 1e-9;
                for (int i = 0; i < m; ++i) {
                    const double rn = res[static_cast<size_t>(i)] / scale;
                    w[static_cast<size_t>(i)] = (rn <= kH) ? 1.0 : (kH / rn);
                }
            }

            // Extract M (D×D, row-major: M[d*D + r] = M_{d,r}) and t (D).
            out.M.assign(static_cast<size_t>(D) * D, 0.0);
            out.t.assign(static_cast<size_t>(D), 0.0);
            for (int d = 0; d < D; ++d) {
                for (int r = 0; r < D; ++r)
                    out.M[static_cast<size_t>(d) * D + r] = M_aug[r*D + d];
                out.t[static_cast<size_t>(d)] = M_aug[D*D + d];
            }
            out.scatter   = scale;
            out.n_matches = m;
            return true;
        };

        for (const auto& [chunkPair, modelPairs] : pbcp) {
            if (static_cast<int>(modelPairs.size()) < minMatch) continue;
            const int m = static_cast<int>(modelPairs.size());
            std::vector<double> A(static_cast<size_t>(m) * D, 0.0);
            std::vector<double> B(static_cast<size_t>(m) * D, 0.0);
            int mUsed = 0;
            for (const auto& [mA, mB] : modelPairs) {
                const auto& mvA = models[mA].mean;
                const auto& mvB = models[mB].mean;
                if (static_cast<int>(mvA.size()) < D ||
                    static_cast<int>(mvB.size()) < D) continue;
                for (int j = 0; j < D; ++j) {
                    A[static_cast<size_t>(mUsed) * D + j] =
                        static_cast<double>(mvA[j]);
                    B[static_cast<size_t>(mUsed) * D + j] =
                        static_cast<double>(mvB[j]);
                }
                ++mUsed;
            }
            if (mUsed < minMatch) continue;
            ChunkTransform T;
            if (!fitPair(A, B, mUsed, T)) continue;
            transformTable[chunkPair] = std::move(T);
        }

        // Chain smoothness (Tikhonov): smooth each parameter sequence
        // (M_ij)_k and (t_j)_k along k via tridiagonal Thomas algorithm,
        // pulling per-pair fits toward their neighbours when drift is
        // continuous.  Pairs with poor fits (few matches → high scatter) get
        // pulled most.  λ = 0 disables smoothing.
        if (CrossChunkTransformChainSmoothLambda > 0.0f && !transformTable.empty()) {
            // Collect sorted chunk pairs (assumed adjacent, sorted by start).
            std::vector<std::pair<int,int>> keys;
            keys.reserve(transformTable.size());
            for (const auto& kv : transformTable) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());

            const int K = static_cast<int>(keys.size());
            if (K >= 3) {
                const double lam =
                    static_cast<double>(CrossChunkTransformChainSmoothLambda);

                // Thomas-algorithm tridiagonal solver: smooth observed seq y_k.
                //   (1 + 2λ) x_k  − λ x_{k−1} − λ x_{k+1} = y_k
                // (endpoints have one λ neighbour each).
                auto smooth1D = [&](std::vector<double>& y) {
                    std::vector<double> a(K, -lam), b(K, 1.0 + 2.0 * lam),
                                        c(K, -lam), d = y;
                    a[0] = 0.0; b[0] = 1.0 + lam;
                    c[K - 1] = 0.0; b[K - 1] = 1.0 + lam;
                    // Forward sweep
                    for (int k = 1; k < K; ++k) {
                        const double mlt = a[k] / b[k - 1];
                        b[k] -= mlt * c[k - 1];
                        d[k] -= mlt * d[k - 1];
                    }
                    // Back substitution
                    y[K - 1] = d[K - 1] / b[K - 1];
                    for (int k = K - 2; k >= 0; --k)
                        y[k] = (d[k] - c[k] * y[k + 1]) / b[k];
                };

                // Each (d, r) of M and each d of t smoothed independently.
                for (int d = 0; d < D; ++d) {
                    for (int r = 0; r < D; ++r) {
                        std::vector<double> seq(K);
                        for (int k = 0; k < K; ++k)
                            seq[k] = transformTable[keys[k]]
                                        .M[static_cast<size_t>(d) * D + r];
                        smooth1D(seq);
                        for (int k = 0; k < K; ++k)
                            transformTable[keys[k]]
                                .M[static_cast<size_t>(d) * D + r] = seq[k];
                    }
                    std::vector<double> seq(K);
                    for (int k = 0; k < K; ++k)
                        seq[k] = transformTable[keys[k]].t[static_cast<size_t>(d)];
                    smooth1D(seq);
                    for (int k = 0; k < K; ++k)
                        transformTable[keys[k]].t[static_cast<size_t>(d)] = seq[k];
                }
            }
        }

        Output("MergeChunkModels: Transform table — %zu adjacent chunk pairs "
               "fitted (affine, IRLS-Huber, min %d matches, chain smooth λ=%.2f)\n",
               transformTable.size(), minMatch,
               static_cast<double>(CrossChunkTransformChainSmoothLambda));
    }

    // Helper: chained transform position from chunk ca to chunk cb applied
    // to point x_a ∈ ℝ^D (typically a cluster mean).  Returns the predicted
    // position and the chained scatter (variance addition).  scatter = -1 if
    // any link in the chain is missing.  For ca == cb returns (x_a, 0).
    auto lookupTransform = [&](int ca, int cb, int D,
                               const std::vector<float>& xA)
        -> std::pair<std::vector<double>, double>
    {
        if (ca == cb) {
            std::vector<double> p(D);
            for (int j = 0; j < D; ++j) p[j] = static_cast<double>(xA[j]);
            return {p, 0.0};
        }
        const int sgn = (ca < cb) ? 1 : -1;
        const int ci = std::min(ca, cb);
        const int cj = std::max(ca, cb);
        // Compose adjacent transforms ci → ci+1 → ... → cj
        // Forward composition:  x ← M_k x + t_k repeatedly.
        // Reverse composition (cb < ca): apply inverse at each step.
        std::vector<double> p(D);
        for (int j = 0; j < D; ++j) p[j] = static_cast<double>(xA[j]);
        double total_var = 0.0;
        for (int k = ci; k < cj; ++k) {
            auto it = transformTable.find({k, k + 1});
            if (it == transformTable.end())
                return {std::vector<double>(D, 0.0), -1.0};
            const ChunkTransform& T = it->second;
            if (sgn > 0) {
                std::vector<double> np(D, 0.0);
                for (int d = 0; d < D; ++d) {
                    double s = T.t[static_cast<size_t>(d)];
                    for (int r = 0; r < D; ++r)
                        s += T.M[static_cast<size_t>(d) * D + r] * p[r];
                    np[d] = s;
                }
                p = std::move(np);
            } else {
                // Inverse: x = M^{-1} (y − t).  For small D, invert via the
                // adjugate/Cramer path is overkill; reuse cholFactor on M·Mᵀ
                // would also be overkill.  In practice ca > cb is rare here
                // (Phase 2 iterates forward).  Mark unsupported by returning
                // missing — the smoothness gate then falls back to xcorr.
                return {std::vector<double>(D, 0.0), -1.0};
            }
            total_var += T.scatter * T.scatter;
        }
        return {std::move(p), std::sqrt(total_var)};
    };

    // ── Pass 2: F1 N×M xcorr on leftovers, directional edge waveforms ───
    //
    // For each leftover (chunk-cluster not matched by overlap voting),
    // search every chunk-cluster in every other chunk for the best xcorr
    // match.  Edge waveform direction: the leftover's edge nearest the
    // candidate's chunk; the candidate's edge nearest the leftover's
    // chunk.  Falls back to chunk-wide meanWav when an edge waveform is
    // unavailable (cluster has fewer than 5 spikes in the edge window).
    //
    // MNN gating: a leftover's best partner must reciprocate (have the
    // leftover as ITS best back-match across ALL its candidates).  This
    // guards against promiscuous merging — a leftover that scores high
    // against many distant chunks gets matched only if some chunk also
    // scores it highest.
    int totalXcorrMerges = 0;
    int nLeftovers = 0;
    int nEigenAdmitted = 0;   // Phase6EigenResidual: pairs xcorr rejected but eigen admitted
    if (CrossChunkTemplateScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        const int mxSh   = std::max(1, NbSamplesPerSpike / 4);

        // Resolve which waveform to use for cluster i when matching against
        // cluster j.  Direction: i.chunkIdx vs j.chunkIdx.  Fall back to
        // chunk-wide meanWav when the requested edge is empty.
        auto pickWav = [&](const ChunkModel& mi, const ChunkModel& mj)
            -> const std::vector<int16_t>* {
            const std::vector<int16_t>* w = nullptr;
            if (mi.chunkIdx < mj.chunkIdx)      w = &mi.meanWavRight;
            else if (mi.chunkIdx > mj.chunkIdx) w = &mi.meanWavLeft;
            else                                w = &mi.meanWav;     // shouldn't happen
            if (!w || static_cast<int>(w->size()) != wElems) {
                if (static_cast<int>(mi.meanWav.size()) == wElems) w = &mi.meanWav;
                else w = nullptr;
            }
            return w;
        };

        // Identify leftovers (non-noise, not matched by Pass 1)
        std::vector<int> leftovers;
        leftovers.reserve(n);
        for (int i = 0; i < n; i++) {
            if (models[i].localClusterId == 0) continue;  // noise
            if (matched.count(i)) continue;
            leftovers.push_back(i);
        }
        nLeftovers = static_cast<int>(leftovers.size());

        // Score: best (li, j) pair seen.  Build bestForward[li] (best j for
        // each leftover li) and bestBackward[j] (best leftover for each j
        // candidate).  MNN: both must agree.
        std::unordered_map<int, std::pair<int,float>> bestForward;   // li -> (j, score)
        std::unordered_map<int, std::pair<int,float>> bestBackward;  // j  -> (li, score)

        // Adjacent-chunk gate (default 1 = adjacent only).  Without this
        // the inner loop is O(N²) over ALL cluster pairs across ALL
        // chunks — for typical fragmented sessions (~10k local clusters
        // across 36 chunks) that is ~100M xcorr calls and dominates wall
        // time.  Setting CrossChunkMaxChunkDistance=1 keeps the work
        // O(N × K_per_chunk × 2 neighbours), which is what the drift
        // model assumes anyway (Pass 1 drift table is per-adjacent-pair).
        const int maxChunkDist = std::max(0, CrossChunkMaxChunkDistance);
        if (maxChunkDist == 0) {
            Output("MergeChunkModels: Pass 2 (edge xcorr): disabled "
                   "(CrossChunkMaxChunkDistance = 0)\n");
        } else {
            Output("MergeChunkModels: Pass 2 (edge xcorr): max chunk "
                   "distance = %d (default 1 = adjacent only; raise to "
                   "match cross-chunk gaps from missing-data periods)\n",
                   maxChunkDist);
        }

        if (Phase6EigenResidualEnable)
            Output("MergeChunkModels: Pass 2 [Stage 3.1 Pass2]: eigen-residual gate ON "
                   "(thresh=%.3f, k=%d, c=%.1f, floorFrac=%.3f)\n",
                   Phase6EigenResidualThresh, Phase6EigenResidualK,
                   Phase6EigenResidualC, Phase6EigenResidualFloorFrac);

        if (maxChunkDist > 0)
        for (int li : leftovers) {
            const ChunkModel& mA = models[li];
            for (int j = 0; j < n; j++) {
                if (j == li) continue;
                const ChunkModel& mB = models[j];
                if (mB.localClusterId == 0)        continue;  // noise
                if (mB.chunkIdx == mA.chunkIdx)    continue;  // same chunk
                if (std::abs(mB.chunkIdx - mA.chunkIdx) > maxChunkDist)
                    continue;                                  // distance gate
                const auto* wA = pickWav(mA, mB);
                const auto* wB = pickWav(mB, mA);
                if (!wA || !wB) continue;

                int sh = 0; float sc = 0.0f;
                XcorrDispatch::compute(
                    wA->data(), wB->data(),
                    1, NbChannels, NbSamplesPerSpike, mxSh, 0.0f, &sh, &sc);
                // Phase 6 eigen-residual gate (testing; -Phase6EigenResidualEnable 1):
                // admit when EITHER rigid xcorr passes OR the feature-space
                // soft-warp eigen-residual is small.  Default off -> the plain
                // xcorr gate is the only condition.
                const bool _xcorrPass = (sc >= CrossChunkTemplateScore);
                bool _eigenPass = false;
                if (Phase6EigenResidualEnable) {
                    const double _er = p6_eigenResidualFeature(
                        mA.mean, mA.cov, mA.nMembers,
                        mB.mean, mB.cov, mB.nMembers,
                        Phase6EigenResidualK,
                        static_cast<double>(Phase6EigenResidualC),
                        static_cast<double>(Phase6EigenResidualFloorFrac));
                    _eigenPass = std::isfinite(_er) &&
                                 _er <= static_cast<double>(Phase6EigenResidualThresh);
                    if (_eigenPass && !_xcorrPass) {
                        ++nEigenAdmitted;
                        // Residual-derived MNN score so good eigen matches actually
                        // compete (and can commit) instead of tying at the floor:
                        // er=0 -> ~1.0, er=Thresh -> the xcorr floor.  The drift-
                        // smoothness factor below still multiplies this.
                        const double _frac = std::max(0.0,
                            1.0 - static_cast<double>(_er)
                                / std::max(static_cast<double>(Phase6EigenResidualThresh), 1e-9));
                        sc = static_cast<float>(
                            static_cast<double>(CrossChunkTemplateScore)
                            + (1.0 - static_cast<double>(CrossChunkTemplateScore)) * _frac);
                    }
                }
                if (!_xcorrPass && !_eigenPass) continue;

                // Drift-smoothness factor.  When CrossChunkDriftSigma > 0
                // AND the chunk pair has a drift estimate from Pass 1,
                // multiply the xcorr score by exp(-(dev/sigma)²/2) where
                // dev = ||actual_displacement - expected|| / scatter.
                // Real drifting units have actual ≈ expected → smoothness ≈ 1.
                // Wrong-direction or solo jumps → smoothness ≈ 0 → effectively
                // vetoed.  No drift estimate available (insufficient Pass 1
                // matches in the chain) → smoothness skipped, fall back to
                // pure xcorr scoring.
                if (CrossChunkDriftSigma > 0.0f && nSpatialDims > 0) {
                    const int D = nSpatialDims;
                    if (static_cast<int>(mA.mean.size()) >= D &&
                        static_cast<int>(mB.mean.size()) >= D)
                    {
                        // Patch 0063: prefer the affine transform when
                        // enabled and the chunk pair has a fitted T;
                        // else fall back to translation-only drift table.
                        double dev2 = 0.0;
                        double scatter = -1.0;
                        if (CrossChunkTransformDriftEnable != 0
                            && !transformTable.empty())
                        {
                            const auto [pred, sc_t] =
                                lookupTransform(mA.chunkIdx, mB.chunkIdx,
                                                D, mA.mean);
                            if (sc_t > 0.0) {
                                for (int j = 0; j < D; ++j) {
                                    const double d =
                                        static_cast<double>(mB.mean[j])
                                        - pred[j];
                                    dev2 += d * d;
                                }
                                scatter = sc_t;
                            }
                        }
                        if (scatter < 0.0) {
                            const auto [expected, sc_d] =
                                lookupDrift(mA.chunkIdx, mB.chunkIdx, D);
                            if (sc_d > 0.0) {
                                for (int j = 0; j < D; j++) {
                                    const double d =
                                        (static_cast<double>(mB.mean[j])
                                       - static_cast<double>(mA.mean[j]))
                                      - expected[j];
                                    dev2 += d * d;
                                }
                                scatter = sc_d;
                            }
                        }
                        if (scatter > 0.0) {
                            const double dev_norm =
                                std::sqrt(dev2) / scatter;
                            const double sig =
                                static_cast<double>(CrossChunkDriftSigma);
                            const double smoothness =
                                std::exp(-0.5 * (dev_norm / sig) * (dev_norm / sig));
                            sc = static_cast<float>(sc * smoothness);
                            if (sc < CrossChunkTemplateScore) continue;
                        }
                    }
                }

                auto itF = bestForward.find(li);
                if (itF == bestForward.end() || sc > itF->second.second)
                    bestForward[li] = {j, sc};
                auto itB = bestBackward.find(j);
                if (itB == bestBackward.end() || sc > itB->second.second)
                    bestBackward[j] = {li, sc};
            }
        }

        // Apply MNN merges
        for (auto& [li, fwd] : bestForward) {
            const int j = fwd.first;
            auto itB = bestBackward.find(j);
            if (itB == bestBackward.end() || itB->second.first != li) continue;
            if (Find(li) == Find(j)) continue;
            Union(li, j);
            totalXcorrMerges++;
            Output("  edge-xcorr  chunk%d.c%d <-> chunk%d.c%d  xcorr=%.3f\n",
                   models[li].chunkIdx, models[li].localClusterId,
                   models[j].chunkIdx,  models[j].localClusterId,
                   fwd.second);
        }
    }
    Output("MergeChunkModels: Pass 2 (edge xcorr): %d new merges from %d leftovers\n",
           totalXcorrMerges, nLeftovers);
    if (Phase6EigenResidualEnable)
        Output("MergeChunkModels: Pass 2 eigen-residual: %d directional candidate(s) "
               "admitted at the gate that rigid xcorr rejected (pre-MNN; thresh=%.3f, "
               "k=%d, c=%.1f)\n",
               nEigenAdmitted, Phase6EigenResidualThresh, Phase6EigenResidualK,
               Phase6EigenResidualC);

    // ── Assign contiguous globalClusterIds from component roots ─────────
    std::unordered_map<int,int> rootToGlobal;
    int nextGlobal = 1;

    // Pin all noise roots to 0
    for (int i = 0; i < n; i++)
        if (models[i].localClusterId == 0) { rootToGlobal[Find(i)] = 0; break; }

    for (int i = 0; i < n; i++) {
        const int root = Find(i);
        if (rootToGlobal.count(root) == 0)
            rootToGlobal[root] = nextGlobal++;
        models[i].globalClusterId = rootToGlobal[root];
    }

    const int nGlobal = nextGlobal - 1;
    Output("MergeChunkModels: %d local clusters -> %d global clusters\n",
           n, nGlobal);
    return nGlobal;
}
