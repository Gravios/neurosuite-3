/***************************************************************************
                   KK_chunked.cpp
                   --------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Chunked clustering pipeline for the KK class: subsample preseeding (with its
 on-disk preseed cache), the two RunChunkedCEM drivers, per-cluster and
 re-CEM per-chunk passes, chunk-model merging, and the numbered pipeline
 phases (2b mode-3, 4c remix, 8 variance-split, 15 checkpoint), split out of
 KK.cpp.  Carries the preseed-cache anonymous namespace (kPreseedMagic,
 PreseedCacheHeader, StatFetFile, TryLoadPreseedCache, SavePreseedCache) used
 by PreseedSubsampleCEM.  ChunkReCEMPerChunk and RunPhase2bMode3Chunk reach
 the shared numerical kernels (RunVBGMM, TopKEigenPowerDeflation) through
 KK_internal.h.  Member definitions of the KK class declared in KK.h.
 ***************************************************************************/
#include "KK.h"
#include "KK_internal.h"
#include "KlustaKwik.h"
#include "dipsplit.h"
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
                if (sc < CrossChunkTemplateScore) continue;

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



// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// PreseedSubsampleCEM — cache helpers
//
// On-disk format (little-endian; KKE only runs on x86_64 / aarch64 in
// practice).  Header followed by the flat centres array:
//
//   char     magic[8]        = "KKEPRSD1"   // KKE PReSeeD format v1
//   uint32_t headerSize      = sizeof(header bytes that follow magic)
//   uint64_t fetMtimeSec     = mtime of input .fet file
//   uint64_t fetSize         = bytes of input .fet file
//   int32_t  nPoints
//   int32_t  nDims
//   int32_t  nSpatialDims
//   int32_t  nCentres        = nFound returned by the CEM
//   float    preseedFraction
//   int32_t  randomSeed
//   int32_t  maxClusters
//   float    penaltyMix
//   int32_t  timeMergeIter
//   ── then nCentres * nSpatialDims float32 values ──
//
// Cache is rejected (recompute) if ANY field differs from the current
// run's value.  Input-file mtime+size guards against changing input
// data without changing parameters.
// ---------------------------------------------------------------------------
namespace {

constexpr char kPreseedMagic[8] = {'K','K','E','P','R','S','D','1'};

struct PreseedCacheHeader {
    char     magic[8];
    uint32_t headerSize;
    uint64_t fetMtimeSec;
    uint64_t fetSize;
    int32_t  nPoints;
    int32_t  nDims;
    int32_t  nSpatialDims;
    int32_t  nCentres;
    float    preseedFraction;
    int32_t  randomSeed;
    int32_t  maxClusters;
    float    penaltyMix;
    int32_t  timeMergeIter;
};

// Stat the input .fet file (canonical .fet or stderiv .fetD variant);
// returns false if neither exists.  Uses pickInputPath to match the
// same resolution logic the rest of KKE uses to actually open the .fet.
bool StatFetFile(uint64_t* outMtimeSec, uint64_t* outSize) {
    char path[STRLEN + 32];
    if (pickInputPath(path, sizeof(path), FileBase, "fet", ElecNo) < 0) {
        // Neither .fet.<n> nor .fetD.<n> exists — caller error path.
        return false;
    }
    struct stat st{};
    if (stat(path, &st) != 0) return false;
    *outMtimeSec = static_cast<uint64_t>(st.st_mtime);
    *outSize     = static_cast<uint64_t>(st.st_size);
    return true;
}

// Try to load cached preseed centres.  Returns empty vector on any
// mismatch or I/O failure.
std::vector<float> TryLoadPreseedCache(int    nPoints,
                                        int    nDims,
                                        int    nSpatialDims,
                                        float  preseedFraction,
                                        int    timeMergeIter) {
    if (PreseedCacheFile[0] == '\0') return {};

    FILE* fp = fopen(PreseedCacheFile, "rb");
    if (!fp) return {};   // first run, expected

    PreseedCacheHeader h{};
    if (fread(&h, sizeof(h), 1, fp) != 1) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache header read failed — recomputing\n");
        return {};
    }
    if (memcmp(h.magic, kPreseedMagic, 8) != 0 ||
        h.headerSize != sizeof(PreseedCacheHeader) - 8) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache magic/size mismatch — recomputing\n");
        return {};
    }

    uint64_t curMtime = 0, curSize = 0;
    if (!StatFetFile(&curMtime, &curSize)) {
        fclose(fp);
        Output("PreseedSubsampleCEM: input .fet stat failed — recomputing\n");
        return {};
    }

    // Validate every field.
    const bool valid =
        h.fetMtimeSec     == curMtime &&
        h.fetSize         == curSize  &&
        h.nPoints         == nPoints  &&
        h.nDims           == nDims    &&
        h.nSpatialDims    == nSpatialDims &&
        h.preseedFraction == preseedFraction &&
        h.randomSeed      == RandomSeed &&
        h.maxClusters     == MaxClusters &&
        h.penaltyMix      == PenaltyMix &&
        h.timeMergeIter   == timeMergeIter;

    if (!valid) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache key mismatch — recomputing "
               "(input or parameter changed since last run)\n");
        return {};
    }
    if (h.nCentres <= 0 || h.nCentres > 1000000) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache nCentres=%d out of range — recomputing\n",
               h.nCentres);
        return {};
    }

    std::vector<float> centres(
        static_cast<size_t>(h.nCentres) * nSpatialDims, 0.0f);
    const size_t want = centres.size();
    if (fread(centres.data(), sizeof(float), want, fp) != want) {
        fclose(fp);
        Output("PreseedSubsampleCEM: cache body read failed (wanted %zu floats) — "
               "recomputing\n", want);
        return {};
    }
    fclose(fp);

    Output("PreseedSubsampleCEM: cache HIT — loaded %d centres from %s\n",
           h.nCentres, PreseedCacheFile);
    return centres;
}

// Write computed centres to the cache file.  Errors are logged but not
// fatal; failing to write a cache shouldn't break the run.
void SavePreseedCache(const std::vector<float>& centres,
                      int    nPoints,
                      int    nDims,
                      int    nSpatialDims,
                      float  preseedFraction,
                      int    timeMergeIter) {
    if (PreseedCacheFile[0] == '\0') return;
    if (centres.empty()) return;

    uint64_t mtime = 0, size = 0;
    if (!StatFetFile(&mtime, &size)) {
        Output("PreseedSubsampleCEM: input .fet stat failed — cache not written\n");
        return;
    }
    FILE* fp = fopen(PreseedCacheFile, "wb");
    if (!fp) {
        Output("PreseedSubsampleCEM: cannot open %s for writing — cache not written\n",
               PreseedCacheFile);
        return;
    }

    PreseedCacheHeader h{};
    memcpy(h.magic, kPreseedMagic, 8);
    h.headerSize      = static_cast<uint32_t>(sizeof(PreseedCacheHeader) - 8);
    h.fetMtimeSec     = mtime;
    h.fetSize         = size;
    h.nPoints         = nPoints;
    h.nDims           = nDims;
    h.nSpatialDims    = nSpatialDims;
    h.nCentres        = static_cast<int32_t>(centres.size() / nSpatialDims);
    h.preseedFraction = preseedFraction;
    h.randomSeed      = RandomSeed;
    h.maxClusters     = MaxClusters;
    h.penaltyMix      = PenaltyMix;
    h.timeMergeIter   = timeMergeIter;

    const bool ok_h = (fwrite(&h, sizeof(h), 1, fp) == 1);
    const bool ok_b = (fwrite(centres.data(), sizeof(float), centres.size(), fp)
                       == centres.size());
    fclose(fp);
    if (!ok_h || !ok_b) {
        Output("PreseedSubsampleCEM: short write to %s — cache may be corrupt\n",
               PreseedCacheFile);
        return;
    }
    Output("PreseedSubsampleCEM: cache SAVED — %d centres to %s\n",
           h.nCentres, PreseedCacheFile);
}

}   // anonymous namespace


// ---------------------------------------------------------------------------
// PreseedSubsampleCEM
//
// Phase 0 for chunked CEM: randomly sample preseedFraction of all spikes,
// run a full CEMTwoPhase on the subset, and return the converged spatial
// cluster centres as a flat vector [nCentres × nSpatialDims].
//
// This gives every per-chunk CEM the same globally-informed starting point
// rather than independent farthest-point seeds.  The key benefit: units that
// persist across all chunks start near their true centres, so cross-chunk
// model matching sees more consistent cluster IDs and fewer spurious splits.
//
// Returns empty vector on failure (too few spikes, bad fraction, etc.).
//
// If PreseedCacheFile is set and the cache validates against the current
// inputs and parameters, the cached centres are returned without
// recomputing.  After a fresh compute, centres are written back to the
// cache for the next run.
// ---------------------------------------------------------------------------
std::vector<float> KK::PreseedSubsampleCEM(float preseedFraction,
                                            int   nCentres,
                                            int   nSpatialDims,
                                            int   timeMergeIter)
{
    if (preseedFraction <= 0.0f || preseedFraction > 1.0f || nCentres < 1) {
        Output("PreseedSubsampleCEM: invalid fraction %.3f or nCentres %d\n",
               preseedFraction, nCentres);
        return {};
    }

    // ── Try cache first ──
    {
        std::vector<float> cached = TryLoadPreseedCache(
            nPoints, nDims, nSpatialDims, preseedFraction, timeMergeIter);
        if (!cached.empty()) return cached;
    }

    const int nSub = std::max(nCentres * nSpatialDims * 3,
                              static_cast<int>(nPoints * preseedFraction));
    if (nSub >= nPoints) {
        Output("PreseedSubsampleCEM: subsample (%d) >= nPoints (%d) — "
               "skipping preseed, using farthest-point directly.\n", nSub, nPoints);
        return {};
    }

    Output("PreseedSubsampleCEM: sampling %d / %d spikes (%.1f%%) for %d centres\n",
           nSub, nPoints, 100.0f * nSub / nPoints, nCentres);

    // Random subsample without replacement using Fisher-Yates partial shuffle.
    std::vector<int> idx(nPoints);
    std::iota(idx.begin(), idx.end(), 0);
    // Use the global RandomSeed so runs are reproducible.
    std::mt19937 rng(static_cast<unsigned>(RandomSeed));
    for (int i = 0; i < nSub; i++) {
        std::uniform_int_distribution<int> dist(i, nPoints - 1);
        std::swap(idx[i], idx[dist(rng)]);
    }

    // Build subsample KK object.
    // nStartingClusters = nCentres + 1 (noise) so Phase 1 seeds at the target K.
    // MaxClusters and MaxPossibleClusters are extern globals, visible automatically.
    KK Ks;
    Ks.nDims                = nDims;
    Ks.nPoints              = nSub;
    Ks.nStartingClusters    = nCentres + 1;
    Ks.penaltyMix           = penaltyMix;
    Ks.suppressBestSave     = true;
    Ks.minClustersAlive     = 2;
    Ks.AllocateArrays();
    Ks.AllocateCholeskyVecs();

    for (int i = 0; i < nSub; i++) {
        const int p = idx[i];
        for (int d = 0; d < nDims; d++)
            Ks.Data[i * nDims + d] = Data[p * nDims + d];
    }

    Ks.CEMTwoPhase(timeMergeIter);

    const int nFound = Ks.nClustersAlive;
    Output("PreseedSubsampleCEM: converged to %d clusters\n", nFound);

    if (nFound < 1) return {};

    // Extract spatial means from the live clusters.
    // If fewer clusters found than requested, we return what we have;
    // CEMTwoPhase in each chunk will add more via splits as needed.
    std::vector<float> centres(nFound * nSpatialDims, 0.0f);
    for (int cc = 0; cc < nFound; cc++) {
        const int c = Ks.AliveIndex[cc];
        for (int d = 0; d < nSpatialDims; d++)
            centres[cc * nSpatialDims + d] = Ks.Mean[c * nDims + d];
    }

    // ── Persist for next run ──
    SavePreseedCache(centres, nPoints, nDims, nSpatialDims,
                     preseedFraction, timeMergeIter);

    return centres;
}


// RunChunkedCEM — three-phase temporal-chunk pipeline
//
// ---------------------------------------------------------------------------
// RunChunkedCEM overload — external boundary list (seconds)
//
// Converts the caller-supplied boundary times to normalised [0,1] fractions
// using the same timeRawMin/Max reference used by the uniform variant, then
// delegates to the core implementation with the pre-built chunkPoints list.
// Everything from Phase 1 onward is identical to the uniform variant.
// ---------------------------------------------------------------------------
float KK::RunChunkedCEM(const std::vector<float>& chunkBoundsSec,
                         float samplingRate,
                         float mergeThresh,
                         int   globalMergeIter,
                         int   timeMergeIter)
{
    const int nFullDims    = nDims;
    const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
    const int timeDim      = nDims - 1;

    const float sessionSamples = timeRawMax - timeRawMin;
    if (sessionSamples <= 0.0f || samplingRate <= 0.0f || chunkBoundsSec.size() < 2) {
        Output("RunChunkedCEM(ext): degenerate boundaries — falling back.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // Convert boundary times (seconds) to normalised [0,1] fractions.
    // normBounds[i] = boundary_i_seconds * samplingRate / sessionSamples
    // (timeRawMin is the unnormalized start sample; for normalised data the
    //  start is 0, but we subtract it just as LoadData() does.)
    const int nBounds = static_cast<int>(chunkBoundsSec.size());
    const int nChunks = nBounds - 1;

    std::vector<float> normBounds(nBounds);
    for (int i = 0; i < nBounds; i++) {
        // boundary_samples = bound_sec * SR;  norm = boundary_samples / sessionSamples
        // Subtract timeRawMin so the normalised boundary matches the
        // [0,1] range that Data[p*nDims+timeDim] actually occupies.
        // Without this offset all boundaries are shifted right by
        // timeRawMin/sessionSamples, misassigning early spikes.
        normBounds[i] = (chunkBoundsSec[i] * samplingRate - timeRawMin) / sessionSamples;
        // Clamp to [0,1]
        if (normBounds[i] < 0.0f) normBounds[i] = 0.0f;
        if (normBounds[i] > 1.0f) normBounds[i] = 1.0f;
    }

    LockedStderr( "[Phase 0] Chunking (%.0f min, %d drift-adaptive chunks; "
                    "per-chunk random-init CEM)\n",
            sessionSamples / samplingRate / 60.0f, nChunks);
    Output("RunChunkedCEM(ext): session %.1f min, %d drift-adaptive chunks\n",
           sessionSamples / samplingRate / 60.0f, nChunks);

    // Assign each point to its chunk by binary search on normBounds.
    // A point with normalised time t belongs to chunk k where
    //   normBounds[k] <= t < normBounds[k+1].
    std::vector<std::vector<int>> chunkPoints(nChunks);
    for (int p = 0; p < nPoints; p++) {
        const float t = Data[p * nDims + timeDim];
        // upper_bound gives the first boundary strictly > t;
        // subtracting begin() gives the 1-based index of that boundary,
        // so chunk index = that index - 1, clamped.
        int k = static_cast<int>(
            std::upper_bound(normBounds.begin(), normBounds.end(), t)
            - normBounds.begin()) - 1;
        if (k < 0)       k = 0;
        if (k >= nChunks) k = nChunks - 1;
        chunkPoints[k].push_back(p);
    }

    // Merge undersized trailing chunks (same logic as the uniform variant).
    const int minSpikes = nStartingClusters * nSpatialDims * 3;
    for (int k = nChunks - 1; k >= 1; k--) {
        if (static_cast<int>(chunkPoints[k].size()) < minSpikes) {
            Output("  Chunk %d: %d spikes < %d minimum — merging into chunk %d.\n",
                   k, static_cast<int>(chunkPoints[k].size()), minSpikes, k - 1);
            for (int p : chunkPoints[k]) chunkPoints[k-1].push_back(p);
            chunkPoints[k].clear();
        }
    }
    chunkPoints.erase(
        std::remove_if(chunkPoints.begin(), chunkPoints.end(),
                       [](const std::vector<int>& v){ return v.empty(); }),
        chunkPoints.end());
    const int nActive = static_cast<int>(chunkPoints.size());

    if (nActive <= 1) {
        Output("Only one chunk after size-check merges — running CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }
    Output("Active chunks: %d\n", nActive);

    // ── Phases 1 / 2 / 3 are identical to the uniform variant. ─────────────
    // Rather than duplicating ~250 lines, we delegate: build a temporary
    // 'chunkMinutes' value that exactly reproduces the chunkPoints assignment
    // we already computed, then call the uniform variant with that value AND
    // pass our pre-built chunkPoints via a thin shim.
    //
    // Implementation note: we inline the phase 1/2/3 body here because the
    // uniform variant's phase 0 would re-compute boundaries and might not
    // reproduce identical chunkPoints.  The phase 1/2/3 code is factored
    // into a private helper _RunChunkedCEMFromPoints() shared by both
    // overloads.  Until that refactor lands, we inline the body.

    // ── Phase 1: per-chunk CEMTwoPhase (parallel) ───────────────────────────
    std::vector<ChunkModel> allModels;
    std::vector<int>        pointPacked(nPoints, -1);  // -1 sentinel: unwritten spikes become noise

    std::vector<std::vector<ChunkModel>> perChunkModels(nActive);
    std::vector<std::vector<std::pair<int,int>>> perChunkAssign(nActive);
    std::vector<std::vector<int>> perChunkClass(nActive);  // for realignment
    std::vector<float> perChunkScore(nActive, 0.0f);
    std::vector<int>   perChunkNClusters(nActive, 0);

    int maxChunkSize = 0;
    for (int k = 0; k < nActive; k++)
        maxChunkSize = std::max(maxChunkSize, static_cast<int>(chunkPoints[k].size()));

#ifdef _OPENMP
    // When running as a ParallelK worker, ompTeamSize limits the inner
    // team so that multiple workers can share the machine without fighting
    // for threads.  0 = use all available (normal non-parallel path).
    const int nThreads = (ompTeamSize > 0)
        ? ompTeamSize
        : omp_get_max_threads();
#else
    const int nThreads = 1;
#endif
    std::vector<KK> threadKc(nThreads);
    for (int t = 0; t < nThreads; t++) {
        threadKc[t].nDims             = nFullDims;
        threadKc[t].nPoints           = maxChunkSize;
        threadKc[t].nStartingClusters = nStartingClusters;
        threadKc[t].penaltyMix        = penaltyMix;
        threadKc[t].suppressBestSave  = true;
        threadKc[t].minClustersAlive  = nStartingClusters;
        threadKc[t].chunkInitRandom   = true;  // per-chunk random init
        threadKc[t].AllocateArrays();
        threadKc[t].AllocateCholeskyVecs();
    }

    const int runsPerChunk = (nRuns > 0) ? nRuns : 1;
    const int nFlatPhase1  = nActive * runsPerChunk;

    struct ChunkRunResult {
        float            score     = HugeScore;
        int              nClusters = 0;
        std::vector<int> cls;
    };
    std::vector<ChunkRunResult> flatResults(static_cast<size_t>(nFlatPhase1));
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k    = fi / runsPerChunk;
        const int nPts = static_cast<int>(chunkPoints[static_cast<size_t>(k)].size());
        flatResults[static_cast<size_t>(fi)].cls.assign(static_cast<size_t>(nPts), 0);
        flatResults[static_cast<size_t>(fi)].score = HugeScore;
    }

    LockedStderr( "[Phase 1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
            nThreads, nActive, runsPerChunk, nFlatPhase1);
        #pragma omp parallel for schedule(dynamic) default(none) \
        num_threads(nThreads) \
        shared(flatResults, chunkPoints, nActive, nFullDims, timeMergeIter, threadKc, stderr) \
        firstprivate(MaxPossibleClusters, nStartingClusters, penaltyMix, \
                     runsPerChunk, nFlatPhase1, HugeScore, RandomSeed)
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k   = fi / runsPerChunk;
        const int run = fi % runsPerChunk;
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        KK& Kc = threadKc[
#ifdef _OPENMP
            omp_get_thread_num()
#else
            0
#endif
        ];
        kk_seed_rng(kk_mix_seed(kk_mix_seed(static_cast<uint64_t>(RandomSeed),
                                            static_cast<uint64_t>(k)),
                                static_cast<uint64_t>(run)));
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = nStartingClusters;
        Kc.NoisePoint        = 1;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        const float runScore = Kc.CEMTwoPhase(timeMergeIter);

        ChunkRunResult& cr = flatResults[static_cast<size_t>(fi)];
        cr.score     = runScore;
        cr.nClusters = Kc.nClustersAlive;
        for (int i2 = 0; i2 < nPts; i2++)
            cr.cls[static_cast<size_t>(i2)] = Kc.Class[i2];

        #pragma omp critical
        { fprintf(stderr, "  [chunk %d/%d  run %d/%d] score=%.4g  nclusters=%d\n",
                  k + 1, nActive, run + 1, runsPerChunk,
                  runScore, Kc.nClustersAlive); }
    }

    // Serial reduction: pick best run per chunk, rebuild KK, harvest models.
    for (int k = 0; k < nActive; k++) {
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        float bestScore = HugeScore;
        int   bestRun   = 0;
        for (int run = 0; run < runsPerChunk; run++) {
            const float s = flatResults[static_cast<size_t>(k * runsPerChunk + run)].score;
            if (s < bestScore) { bestScore = s; bestRun = run; }
        }
        const ChunkRunResult& best = flatResults[
            static_cast<size_t>(k * runsPerChunk + bestRun)];

        KK& Kc = threadKc[0];
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = nStartingClusters;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        for (int i2 = 0; i2 < nPts; i2++) Kc.Class[i2] = best.cls[static_cast<size_t>(i2)];
        for (int c = 0; c < MaxPossibleClusters; c++) Kc.ClassAlive[c] = 0;
        for (int i2 = 0; i2 < nPts; i2++) Kc.ClassAlive[Kc.Class[i2]] = 1;
        Kc.nClustersAlive = 0;
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (Kc.ClassAlive[c]) Kc.AliveIndex[Kc.nClustersAlive++] = c;
        Kc.MStep();

        perChunkScore[k]     = bestScore;
        perChunkNClusters[k] = best.nClusters;

        auto& models = perChunkModels[k];
        for (int cc = 0; cc < Kc.nClustersAlive; cc++) {
            const int c = Kc.AliveIndex[cc];
            ChunkModel cm;
            cm.chunkIdx        = k;
            cm.localClusterId  = c;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(nFullDims, 0.0f);
            cm.cov.assign(nFullDims * nFullDims, 0.0f);
            for (int d = 0; d < nFullDims; d++)
                cm.mean[d] = Kc.Mean[c * nFullDims + d];
            for (int r = 0; r < nFullDims; r++)
                for (int col = r; col < nFullDims; col++)
                    cm.cov[r * nFullDims + col] =
                        Kc.Cov[c * Kc.nDims2 + r * nFullDims + col];
            for (int i = 0; i < nPts; i++)
                if (Kc.Class[i] == c) cm.nMembers++;
            models.push_back(std::move(cm));
        }

        auto& assign = perChunkAssign[k];
        assign.reserve(nPts);
        for (int i = 0; i < nPts; i++)
            assign.emplace_back(pts[static_cast<size_t>(i)],
                                k * MaxPossibleClusters + Kc.Class[i]);

        auto& classArr = perChunkClass[k];
        classArr.resize(nPts);
        for (int i = 0; i < nPts; i++) classArr[i] = Kc.Class[i];
    }

    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1");

    // ── Provisional Class[] seeding for Phase 2 ─────────────────────────────
    // Phase 1 ran on local threadKc[] objects; K1.Class[] is all-zero here.
    // Phase 2 SubspaceReclusterPerChunk needs alive cluster statistics.
    if (SubspaceRecluster > 0) {
        std::fill(Class.m_Data, Class.m_Data + nPoints, 0);
        std::fill(ClassAlive.m_Data, ClassAlive.m_Data + MaxPossibleClusters, 0);
        ClassAlive[0] = 1;
        for (int k_ps = 0; k_ps < nActive; ++k_ps) {
            const auto& pts_ps = chunkPoints[static_cast<size_t>(k_ps)];
            const auto& cls_ps = perChunkClass[static_cast<size_t>(k_ps)];
            for (int i_ps = 0; i_ps < static_cast<int>(pts_ps.size()); ++i_ps) {
                const int p_ps = pts_ps[static_cast<size_t>(i_ps)];
                const int c_ps = cls_ps[static_cast<size_t>(i_ps)];
                if (Class[p_ps] == 0 && c_ps > 0) {
                    Class[p_ps] = c_ps;  ClassAlive[c_ps] = 1;
                }
            }
        }
        Reindex();
        nDims = nFullDims;  nDims2 = nDims * nDims;
        MStep();
        for (int cc2 = 1; cc2 < nClustersAlive; ++cc2) {
            const int c2 = AliveIndex[cc2];
            if (Cholesky(Cov.m_Data + c2*nDims2, cholFlat.data() + c2*nDims2, nDims))
                ClassAlive[c2] = 0;
        }
        Reindex();
        Output("Provisional Class[] seeded: %d alive clusters\n", nClustersAlive);
    }

    // ── Phase 1a: per-cluster shift-probe alignment ────────────────────────
    //
    // For each alive cluster, picks the per-spike δ ∈ {-N,…,+N} that
    // minimises Mahalanobis² to the cluster's own Gaussian (using the
    // cluster's Mean + Cholesky-factored Cov).  Aligns spikes WITHIN each
    // cluster — not to a canonical peak sample.  The cluster's mean is
    // wherever Phase 1 CEM put it; this phase tightens spikes around that
    // centre, it does not move the centre to peakSampleIndex.
    //
    // Caveats inherited from the design:
    //  - Per-cluster myopia: spikes near a cluster boundary may be pulled
    //    deeper into the wrong cluster (the score sees only the assigned
    //    cluster's distribution, not neighbours').  The next EStep will
    //    re-evaluate, but on slightly distorted features.
    //  - Asymmetric search window: candOk[ci] enforces |baseCum + δ| ≤
    //    m_timeShiftMaxAbs, so spikes already near ±maxAbs see fewer
    //    candidates on the cap-side.  Intentional bound on cumulative
    //    drift, not a bug.
    //  - Pre-shifted PCA basis is fixed at InitTimeShift; large cumulative
    //    shifts (across many iterations) make the basis statistically
    //    less efficient but not incorrect.
    //
    // Stats are current at this point: the SubspaceRecluster=1 seeding
    // block above ran MStep + Cholesky.  When SubspaceRecluster=0 no
    // clusters are alive yet and the call no-ops cleanly (the loop in
    // TimeShiftAlignPhase iterates ClassAlive and finds nothing).
    RunAlignmentBlock(TimeShiftAlignAfterPhase1, "Phase 1a");

    // ── Phase 1b: per-chunk DipSplit ──────────────────────────────────────
    //
    // Catches bimodal/elongated clusters that the parametric Phase 1 CEM
    // missed.  Runs per-chunk before Phase 2 so subsequent passes operate
    // on the most-correctly-split inputs, and so cross-chunk merge in
    // Phase 6 sees the correct cluster count.  Replaces the old
    // post-Phase-7 global Phase 8 DipSplit.  No-op when DipSplitEnable=0.
    DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims,
                     "Phase 1b");
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1b");
    RunAlignmentBlock(TimeShiftAlignAfterPhase1b, "Phase 1c");

    // ── Phase 2: per-chunk refractory split + subspace reclustering ────────
    //
    // P2.D: refractory split runs FIRST.  Refractory contamination is a
    // physical signal — two units sharing a cluster cannot satisfy
    // biological refractory periods — so it's a stronger prior than any
    // covariance-based statistical split.  Splitting on physics first gives
    // the subspace CEM cleaner inputs.
    //
    // The earlier order (subspace then refractory) had a failure mode: if
    // subspace incorrectly split a clean cluster (multiple-comparisons
    // false positive), the children might individually pass the 1% ISI
    // contamination threshold (each carrying half the violations) and the
    // over-split would stick.  Running refractory first locks in physically-
    // motivated splits before the statistical pass can over-fit.
    if (SubspaceRecluster > 0) {
        // Refractory-period guided split — catches mixtures by exploiting the
        // 1-neuron-per-refractory-window constraint.  Absolute refractory =
        // 1.5 ms × sampling rate samples.  Trigger when ISI contamination
        // rate >= 1%.
        if (SamplingRate > 0.0f) {
            const float refractSamp    = 1.5f * SamplingRate / 1000.0f;
            const float sessLenSamp    = timeRawMax - timeRawMin;
            LockedStderr( "[Phase 2] Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2");
        }

        // Phase 2a: per-cluster ordinary CEM in the full feature space.
        // Replaces SubspaceReclusterPerChunk.  For each cluster in each
        // chunk, runs CEM with splits enabled to find bimodal substructure;
        // updates perChunkClass[] only.
        PerClusterCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a");

        // Phase 2a.5: per-chunk DipSplit between Phase 2a and Phase 2b.
        //
        // Rationale: Phase 2a's per-cluster CEM is a parametric Gaussian
        // test that absorbs single-dimension bimodality by inflating the
        // covariance along the bimodal axis (the inflation penalty in BIC
        // is smaller than the constant cost of accepting a new cluster).
        // DipSplit looks for KDE valleys in 1D projections — exactly the
        // structure CEM misses.
        //
        // Inserting BEFORE Phase 2b gives Phase 2b finer-grained
        // warm-start clusters.  With Phase2bEnableSplits=0 (default
        // since patch 0007), Phase 2b only runs warm-start CEM plus
        // ConsiderDeletion — which converges faster from a finer
        // partition because boundary-spike reassignment is local and
        // ConsiderDeletion's per-iter K-loop catches any oversplits
        // cheaply.  The existing Phase 2.5 (after-Phase-2b) DipSplit
        // continues to run as a safety net.
        if (DipSplitEnable != 0 && DipSplitBeforePhase2b != 0) {
            DipSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims,
                "Phase 2a.5");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.5");
        }

        // Phase 2a.6: per-chunk HullSplit (k-NN-graph connected components).
        // Complement to Phase 2a.5 DipSplit — catches multi-D topology
        // (two disconnected components in feature space) that 1D-projected
        // DipSplit misses.  Default off (HullSplitEnable=0).
        if (HullSplitEnable != 0) {
            HullSplitPerChunk(
                chunkPoints, perChunkClass, nFullDims, "Phase 2a.6");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.6");
        }

        // Phase 2a.7: per-channel amplitude+phase bimodality split.  Reads
        // raw waveforms and tests each channel's (peak amp, peak time,
        // trough amp, trough time) 4-vector for 1D / 2D-angle-sweep KDE
        // valleys.  Catches the failure mode that DipSplit and HullSplit
        // both miss — two units differing by a small combination of
        // amplitude AND phase on a single channel.  Default off
        // (PerChannelSplitEnable=0).
        if (PerChannelSplitEnable != 0) {
            PerChannelSplitPerChunk(
                chunkPoints, perChunkClass,
                NbChannels, NbSamplesPerSpike, "Phase 2a.7");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.7");
        }

        // Phase 2b: chunk-level warm-start CEM.  Lets boundary spikes
        // reassign across the new fine-grained label set and lets CEM
        // merge oversplit fragments via ConsiderDeletion.  Rebuilds
        // perChunkModels[] from the converged state.
        ChunkReCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2b");

        // Phase 2b.5: K-template chunk split (optional, off by default).
        // For each chunk, picks K well-isolated reference cluster means
        // and partitions every "source" cluster's spikes by their
        // nearest reference template.  Each viable (source, ref) bucket
        // becomes a new chunk-local cluster.  Phase 6 (cross-chunk
        // model matching) handles consolidation of the new clusters
        // into global units.  Enable with -KnnSplitPerChunkEnable 1.
        // -KnnSplitMode 0 (default): legacy nearest-template; mode 1:
        // klusters-faithful K-NN majority-vote (see wave_knn_split.h).
        if (KnnSplitPerChunkEnable) {
            if (KnnSplitMode == 1) {
                WaveKnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            } else {
                KnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            }
        }
        if (FullCemSplitEnable) {
            FullCemSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims);
        }
        // Phase 2.5: second per-chunk DipSplit pass.
        //
        // The Phase 1c DipSplit ran before subspace reclustering, so it
        // could only see bimodality in the parent clusters produced by
        // Phase 1 CEM.  PerClusterCEMPerChunk (Phase 2a) re-partitions
        // those parents into finer pieces via likelihood-based CEM with
        // splits enabled — but CEM is a parametric Gaussian test that
        // absorbs single-dimension bimodality by inflating variance
        // along the bimodal axis (the inflation penalty is smaller than
        // the constant cost of a new cluster).  Clusters that emerge
        // from Phase 2a with a clear KDE valley in one PC projection
        // but a covariance the parametric test accepts are exactly the
        // cases this second pass catches.
        //
        // Per-chunk (not global) for the same reason as Phase 1c: a
        // chunk's spikes don't span the session-drift range, so apparent
        // bimodality from drift is suppressed.
        DipSplitPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims,
            "Phase 2.5");
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2.5");
    }


    // Phase 7 (Global EM) alignment also disabled: chunk means from
    // CEMTwoPhase are already blended by timeMergeIter and would produce
    // spurious shifts on clean data.  ConsiderDeletion / TimeShiftMergeEvaluate
    // handle the time-shift cases that actually occur (genuine same-unit
    // mergers across drift).

    // ── Post-Phase-2 alignment site ─────────────────────────────────────
    RunAlignmentBlock(TimeShiftAlignAfterPhase2, "Phase 2c");

    // Phase-ordering note: Phase 8 (global post-merge DipSplit) was
    // removed.  Its function is now served by per-chunk DipSplit at
    // Phase 1b and a second pass at Phase 2.5 — operating per-chunk
    // avoids the drift-axis false positives a global pass produces on
    // session-spanning clusters.  Phases 3 (mean-waveform harvest, the
    // [Phase 3] log tag below), 4, and 8 are intentionally non-major
    // phase numbers in the current pipeline; the comment block above
    // RunChunkedCEM enumerates which numbers are alive.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        LockedStderr( "[Phase 3] Mean waveform harvest (channel-major xcorr format)\n");
    // Populate ChunkModel::meanWav for template matching.
    // Done serially after the parallel chunk loop since all chunks share
    // the same .spk file handle and fseeko calls cannot be parallelised safely.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
        NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        char spkPathTM[STRLEN + 16];
        // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
        pickInputPath(spkPathTM, sizeof(spkPathTM), FileBase, "spk", ElecNo);
        FILE* spkTM = fopen(spkPathTM, "rb");
        if (spkTM) {
            for (int k = 0; k < nActive; k++) {
                const auto& pts  = chunkPoints[k];
                const auto& cls  = perChunkClass[k];
                const int   nPts = static_cast<int>(pts.size());

                // Build localClusterId → ChunkModel* map for this chunk
                std::unordered_map<int, ChunkModel*> lcToModel;
                for (auto& cm : perChunkModels[k])
                    if (cm.chunkIdx == k)
                        lcToModel[cm.localClusterId] = &cm;

                // Determine this chunk's time range so we can classify spikes
                // as left-edge (first 25%) / middle / right-edge (last 25%)
                // for the boundary-localised waveforms used in Phase 6's
                // cross-chunk xcorr.  Time = last feature dim, raw samples.
                float tMin = std::numeric_limits<float>::infinity();
                float tMax = -std::numeric_limits<float>::infinity();
                for (int i = 0; i < nPts; i++) {
                    const float t = Data[static_cast<size_t>(pts[i]) * nDims + (nDims - 1)];
                    if (t < tMin) tMin = t;
                    if (t > tMax) tMax = t;
                }
                const float tRange    = (tMax > tMin) ? (tMax - tMin) : 1.0f;
                const float tLeftEnd  = tMin + 0.25f * tRange;
                const float tRightBeg = tMin + 0.75f * tRange;

                // Accumulate per-cluster waveform sums.  Three accumulators
                // per cluster: full chunk, left-edge (first 25%), right-edge
                // (last 25%).  Every spike contributes to acc; spikes within
                // the edge windows additionally contribute to accLeft/Right.
                std::unordered_map<int, std::vector<int64_t>> acc, accLeft, accRight;
                std::unordered_map<int, int> nAcc, nAccLeft, nAccRight;
                std::vector<int16_t> row(static_cast<size_t>(wElems));
                for (int i = 0; i < nPts; i++) {
                    const int lc = cls[i];
                    if (lc == 0) continue;  // skip noise
                    if (!lcToModel.count(lc)) continue;
                    const int p2 = pts[i];
                    fseeko(spkTM,
                           static_cast<off_t>(p2) * wElems * sizeof(int16_t),
                           SEEK_SET);
                    if (fread(row.data(), sizeof(int16_t), wElems, spkTM)
                            != static_cast<size_t>(wElems)) continue;

                    // Apply committed shift (TimeShiftSplitEnable path):
                    // ensure the harvested waveform geometry matches the
                    // PCA features in Data[].
                    ShiftWaveformRowInPlace(row.data(), p2,
                                            NbChannels, NbSamplesPerSpike);

                    // Full-chunk accumulator
                    auto& a = acc[lc];
                    if (a.empty()) a.assign(static_cast<size_t>(wElems), 0);
                    // sample-major (.spk) → channel-major (XcorrDispatch)
                    for (int ch = 0; ch < NbChannels; ch++)
                        for (int s = 0; s < NbSamplesPerSpike; s++)
                            a[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                += row[static_cast<size_t>(s * NbChannels + ch)];
                    nAcc[lc]++;

                    // Edge accumulators.  A spike can only fall into one of
                    // the two edge windows (mutually exclusive at 25% each);
                    // middle-50% spikes contribute only to the chunk-wide mean.
                    const float t = Data[static_cast<size_t>(p2) * nDims + (nDims - 1)];
                    std::vector<int64_t>* edgeAcc = nullptr;
                    int* edgeN = nullptr;
                    if (t <= tLeftEnd)       { edgeAcc = &accLeft[lc];  edgeN = &nAccLeft[lc];  }
                    else if (t >= tRightBeg) { edgeAcc = &accRight[lc]; edgeN = &nAccRight[lc]; }
                    if (edgeAcc) {
                        if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                        for (int ch = 0; ch < NbChannels; ch++)
                            for (int s = 0; s < NbSamplesPerSpike; s++)
                                (*edgeAcc)[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                    += row[static_cast<size_t>(s * NbChannels + ch)];
                        (*edgeN)++;
                    }
                }
                // Finalise full-chunk meanWav
                for (auto& [lc, a] : acc) {
                    int n2 = nAcc[lc];
                    if (n2 == 0) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWav.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWav[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                }
                // Finalise meanWavLeft.  Require a minimum of 5 spikes for a
                // meaningful mean — below that, leave empty so Phase 6 falls
                // back to chunk-wide meanWav for this cluster.
                for (auto& [lc, a] : accLeft) {
                    const int n2 = nAccLeft[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavLeft.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavLeft[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersLeft = n2;
                }
                for (auto& [lc, a] : accRight) {
                    const int n2 = nAccRight[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavRight.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavRight[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersRight = n2;
                }
            }
            fclose(spkTM);
        }
    }
    // ── Phase 6 preflight: MergeThresh sanity check ─────────────────────────
    // The cross-chunk matching body follows the Phase 5 xcorr-iteration
    // block below.  This block only emits a warning if MergeThresh is
    // far outside chi2(nSpatialDims, 0.9999), which would make Phase 6
    // merges either too eager or never trigger.
    {
        const float d_    = static_cast<float>(nSpatialDims);
        const float chi2_9999 = d_ * std::pow(1.0f - 2.0f/(9.0f*d_) + 3.719f * std::sqrt(2.0f/(9.0f*d_)), 3.0f);
        const float chi2_99   = d_ * std::pow(1.0f - 2.0f/(9.0f*d_) + 2.326f * std::sqrt(2.0f/(9.0f*d_)), 3.0f);
        if (mergeThresh > chi2_9999 * 1.5f) {
            Output("WARNING: MergeThresh=%.1f is far above chi2(%d, 0.9999)=%.1f.\n"
                   "  Recommended: MergeThresh=%.1f (chi2(%d, 0.99))\n",
                   mergeThresh, nSpatialDims, chi2_9999, chi2_99, nSpatialDims);
        }
    }
    static const std::vector<std::unordered_map<int,int>> noOverlapVotes;
    // ── Phase 5: within-chunk circular xcorr template matching (iterated) ─
    // Loops until no new merges occur or 10 iterations.  After each merge pass
    // the surviving clusters need updated meanWav vectors (the merged cluster
    // mean changes when two sub-clusters are combined), so the Phase 3 harvest
    // re-runs at the top of each iteration before the next xcorr comparison.
    if (TemplateMatchScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int _tmMax = (TemplateMatchIters > 0) ? TemplateMatchIters : 10;
        int _altNetGrowthStreak = 0;

        // Per-iter convergence digest (printed as a table after the loop).
        struct Phase4DigestRow {
            int iter; int merges; int spikesSplit; int kNew;
            int totalClusters; double wallMs;
        };
        std::vector<Phase4DigestRow> _phase4Digest;

        // Reset the Phase 4 median-template cache at phase start (patch
        // 0052).  Bounds cache memory across the run; entries accumulate
        // within this phase's iters and are reused when cluster
        // membership is unchanged.
        m_medianCache.clear();

        auto _countClusters = [&]() {
            std::set<long long> uniq;
            for (int _k = 0; _k < nActive; _k++)
                for (int _c : perChunkClass[_k])
                    if (_c != 0) uniq.insert(
                        static_cast<long long>(_k) * MaxPossibleClusters + _c);
            return static_cast<int>(uniq.size());
        };

        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
            const auto _iterT0 = std::chrono::steady_clock::now();
            // Phase 4b: optional alternating KnnSplit inside the Phase 4
            // loop — runs BEFORE merge so each iter is split→harvest→merge.
            // The closing iter of the loop is template-merge-only, so its
            // output going to Phase 5 is template-converged (no raw split
            // products escape to downstream phases).
            //
            // Two guards against runaway split-domination:
            //   * iter cap (AlternatingSplitMergeMaxIters, default 2)
            //   * net-growth abort (if WaveKnnSplit produced more new
            //     clusters than template-match merged for two iters in a
            //     row, stop running split; merge-only iters continue up
            //     to TemplateMatchIters).
            int _nSpikesSplit = 0;
            int _kNewThisIter = 0;
            const bool _altGate =
                AlternatingSplitMergeEnable != 0 &&
                KnnSplitPerChunkEnable != 0 && KnnSplitMode == 1 &&
                _tmIter < AlternatingSplitMergeMaxIters &&
                !(AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                  _altNetGrowthStreak >= 2);
            if (_altGate) {
                std::vector<std::vector<int>> _beforeSplit = perChunkClass;
                std::set<int> _kBefore;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kBefore.insert(c);
                if (QualityWeightedSplitEnable) {
                    // Quality-routed: dispatcher computes ISI contamination
                    // + waveform variance for an oversampled pool and routes
                    // each neediest cluster to CEM or knn.  Replaces the
                    // direct WaveKnn+FullCem calls.
                    QualityWeightedSplitDispatch(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                } else {
                    WaveKnnSplitPerChunk(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    if (FullCemSplitEnable) {
                        FullCemSplitPerChunk(
                            chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    }
                }
                for (size_t _ckb = 0; _ckb < perChunkClass.size(); ++_ckb) {
                    const auto& aft = perChunkClass[_ckb];
                    const auto& bef = _beforeSplit[_ckb];
                    if (aft.size() != bef.size()) continue;
                    for (size_t _ii = 0; _ii < aft.size(); ++_ii)
                        if (aft[_ii] != bef[_ii]) ++_nSpikesSplit;
                }
                std::set<int> _kAfter;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kAfter.insert(c);
                _kNewThisIter = static_cast<int>(_kAfter.size())
                              - static_cast<int>(_kBefore.size());
            } else if (AlternatingSplitMergeEnable != 0 &&
                       _tmIter == AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit: iter cap reached "
                    "(%d); continuing with template-match only\n",
                    AlternatingSplitMergeMaxIters);
            } else if (AlternatingSplitMergeEnable != 0 &&
                       AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                       _altNetGrowthStreak >= 2 &&
                       _tmIter < AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit: aborted by "
                    "net-growth streak; continuing with template-match "
                    "only\n");
                _altNetGrowthStreak = 1000;  // suppress further logs
            }

            // Re-harvest meanWav with current perChunkClass
            if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                const int wElems = NbChannels * NbSamplesPerSpike;
                char spkPathTM2[STRLEN + 16];
                // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
                pickInputPath(spkPathTM2, sizeof(spkPathTM2), FileBase, "spk", ElecNo);
                FILE* spkTM2 = fopen(spkPathTM2, "rb");
                if (spkTM2) {
                    for (int k = 0; k < nActive; k++) {
                        const auto& pts2  = chunkPoints[k];
                        const auto& cls2  = perChunkClass[k];
                        const int   nPts2 = static_cast<int>(pts2.size());
                        std::unordered_map<int, ChunkModel*> lcToModel2;
                        for (auto& cm : perChunkModels[k])
                            if (cm.chunkIdx == k)
                                lcToModel2[cm.localClusterId] = &cm;
                        // Zero existing meanWav (and edge waveforms) for live clusters
                        for (auto& [lc2, pcm] : lcToModel2) {
                            pcm->meanWav.assign(static_cast<size_t>(wElems), 0);
                            pcm->meanWavLeft.clear();
                            pcm->meanWavRight.clear();
                        }
                        // Determine this chunk's time range for edge classification
                        float tMin2 = std::numeric_limits<float>::infinity();
                        float tMax2 = -std::numeric_limits<float>::infinity();
                        for (int i = 0; i < nPts2; i++) {
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            if (t < tMin2) tMin2 = t;
                            if (t > tMax2) tMax2 = t;
                        }
                        const float tRange2    = (tMax2 > tMin2) ? (tMax2 - tMin2) : 1.0f;
                        const float tLeftEnd2  = tMin2 + 0.25f * tRange2;
                        const float tRightBeg2 = tMin2 + 0.75f * tRange2;

                        std::unordered_map<int, std::vector<int64_t>> acc2, accLeft2, accRight2;
                        std::unordered_map<int, int> nAcc2, nAccLeft2, nAccRight2;
                        std::vector<int16_t> row2(static_cast<size_t>(wElems));
                        for (int i = 0; i < nPts2; i++) {
                            const int lc2 = cls2[i];
                            if (lc2 == 0 || !lcToModel2.count(lc2)) continue;
                            fseeko(spkTM2,
                                   static_cast<off_t>(pts2[i]) * wElems * sizeof(int16_t),
                                   SEEK_SET);
                            if (fread(row2.data(), sizeof(int16_t), wElems, spkTM2)
                                    != static_cast<size_t>(wElems)) continue;
                            ShiftWaveformRowInPlace(row2.data(), pts2[i],
                                                    NbChannels, NbSamplesPerSpike);
                            auto& a2 = acc2[lc2];
                            if (a2.empty()) a2.assign(static_cast<size_t>(wElems), 0);
                            for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                    a2[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                        += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                            nAcc2[lc2]++;

                            // Edge accumulators
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            std::vector<int64_t>* edgeAcc = nullptr;
                            int* edgeN = nullptr;
                            if (t <= tLeftEnd2)       { edgeAcc = &accLeft2[lc2];  edgeN = &nAccLeft2[lc2];  }
                            else if (t >= tRightBeg2) { edgeAcc = &accRight2[lc2]; edgeN = &nAccRight2[lc2]; }
                            if (edgeAcc) {
                                if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                                for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                    for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                        (*edgeAcc)[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                            += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                                (*edgeN)++;
                            }
                        }
                        for (auto& [lc2, a2] : acc2) {
                            int n2b = nAcc2[lc2];
                            if (n2b == 0) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWav.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWav[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accLeft2) {
                            const int n2b = nAccLeft2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavLeft.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavLeft[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accRight2) {
                            const int n2b = nAccRight2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavRight.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavRight[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                    }
                    fclose(spkTM2);
                }
            }
            LockedStderr( "[Phase 4] Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = (MedianKnnTemplateMatchEnable != 0)
                ? WithinChunkTemplateMatchMedianKnn(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore)
                : WithinChunkTemplateMatch(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore);

            // Phase 4b streak update — now that this iter's merge count
            // is known, compare against the iter's split-induced cluster
            // growth.  Updating here (rather than at split time) means
            // the streak reflects the OUTCOME of an alternation round
            // (split + cleanup-merge together), not the split alone.
            if (_altGate) {
                if (_kNewThisIter > _nMerged) ++_altNetGrowthStreak;
                else                          _altNetGrowthStreak = 0;
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit (iter %d): %d spike "
                    "labels changed; +%d new clusters vs %d merged (net "
                    "growth streak %d/2)\n",
                    _tmIter + 1, _nSpikesSplit, _kNewThisIter, _nMerged,
                    _altNetGrowthStreak);
            }


            // Phase 4b heavy realignment: at the END of every Phase 4 iter,
            // realign each per-chunk cluster's spikes (klusters-faithful
            // xcorr against the cluster's mean) and refeaturize only the
            // changed spikes from the cached .fil group channels.  The
            // next iter's meanWav harvest (which reads .spk through
            // m_cumShift) and Phase 4b WaveKnnSplit (which reads Data[])
            // both see the realigned state.
            //
            // Gated on KlustersRealignAfterPhase4; requires
            // KlustersRealignEnable so MaxShift/MinSize have valid values.
            if (KlustersRealignAfterPhase4 != 0 &&
                m_timeShiftReady &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                std::vector<int> _changedSpikes;
                _changedSpikes.reserve(1024);
                const int _nChanged = KlustersStyleRealignPerChunkClusters(
                    chunkPoints, perChunkClass,
                    NbChannels, NbSamplesPerSpike,
                    _changedSpikes);
                if (_nChanged > 0) {
                    RefeaturizeChangedSpikes(
                        _changedSpikes, NbChannels, NbSamplesPerSpike);
                }
            }

            const double _iterMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - _iterT0).count();
            _phase4Digest.push_back({ _tmIter + 1, _nMerged, _nSpikesSplit,
                                      _kNewThisIter, _countClusters(),
                                      _iterMs });

            if (_nMerged == 0 && _nSpikesSplit == 0) break;
        }
        // ── Phase 4 convergence digest ────────────────────────────────
        LockedStderr("[Phase 4] convergence digest:\n");
        LockedStderr("    iter | merges | splits | +new | clusters | wall_ms\n");
        LockedStderr("    -----+--------+--------+------+----------+--------\n");
        for (const auto& _r : _phase4Digest) {
            LockedStderr("    %4d | %6d | %6d | %4d | %8d | %7.0f\n",
                         _r.iter, _r.merges, _r.spikesSplit, _r.kNew,
                         _r.totalClusters, _r.wallMs);
        }
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 4");
    }

        // ── Phase 4c: neighborhood-remix split (patch 0062) ──────────────
        // Runs after Phase 4 converges; pools each random source with its N
        // closest clusters and re-splits, guarded by disjointness + a
        // tightness mask.  No-op unless -Phase4cRemixEnable 1.
        RunPhase4cRemix(chunkPoints, perChunkClass, perChunkModels, nFullDims);

        // ── Phase 8: variance-targeted knn-split (patch 0067) ───────────
        // Iterates Phase 4b's WaveKnn-split on high-variance clusters
        // only.  No-op unless -Phase8VarianceSplitEnable 1.  FullCEM is
        // intentionally skipped — diffuse clusters have no Gaussian-
        // mixture modes for FullCEM to find; WaveKnn redistributes their
        // spikes to nearby reference clusters instead.
        RunPhase8VarianceSplit(chunkPoints, perChunkClass, perChunkModels, nFullDims);

    // Rebuild pointPacked[] using post-template-merge cluster IDs.
    // perChunkAssign was built with original Phase 1 IDs; after
    // WithinChunkTemplateMatch the IDs in perChunkClass differ.
    // packedToGlobal (built after MergeChunkModels) uses post-merge IDs,
    // so pointPacked must use the same scheme.
    std::fill(pointPacked.begin(), pointPacked.end(), -1);
    for (int k = 0; k < nActive; k++) {
        const auto& pts = chunkPoints[k];
        const auto& cls = perChunkClass[k];
        const int nPts  = static_cast<int>(pts.size());
        for (int i = 0; i < nPts; i++) {
            const int p2 = pts[i];
            if (pointPacked[p2] < 0)  // first-write-wins for overlap spikes
                pointPacked[p2] = k * MaxPossibleClusters + cls[i];
        }
    }

    // Rebuild allModels from perChunkModels now that within-chunk template
    // matching has finalised the local cluster set.  Cluster IDs in
    // perChunkClass and in allModels must agree so that MergeChunkModels vote
    // keys (clsK * MaxPossibleClusters + clsK1) resolve correctly.
    allModels.clear();
    for (int k = 0; k < nActive; k++)
        for (auto& cm : perChunkModels[k])
            allModels.push_back(cm);  // copy — perChunkModels still needed below

    // ── Post-Phase-5 alignment site ─────────────────────────────────────
    // Within-chunk template merges have consolidated per-chunk clusters.
    // Aligns spikes against the merged means before cross-chunk matching.
    RunAlignmentBlock(TimeShiftAlignAfterPhase4, "Phase 4a");

    LockedStderr( "[Phase 5] Cross-chunk model matching (overlap-vote + edge-xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, noOverlapVotes);
    LockedStderr( "[Phase 5] Cross-chunk merge produced %d global clusters\n", nGlobal);
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 5");
    if (nGlobal < 1) {
        Output("Merge produced no real clusters — falling back to CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }
    if (nGlobal >= MaxPossibleClusters) {
        // Emit on stderr unconditionally — this is a hard fall-back from
        // chunked CEM to single-pass CEMTwoPhase that the user must know
        // about (it dramatically changes the output cluster set and was
        // silent under Output() unless -Screen 1 was set).  Keep the
        // Output() call so logging still captures it.
        fprintf(stderr,
                "[Phase 5] WARNING: MergeChunkModels produced %d global "
                "clusters >= MaxPossibleClusters (%d) — falling back to "
                "CEMTwoPhase.  Fix: re-run with -MaxPossibleClusters %d "
                "or higher (rule of thumb: nChunks * MaxClusters).\n",
                nGlobal, MaxPossibleClusters, nGlobal + 100);
        Output("WARNING: MergeChunkModels produced %d global clusters >= "
               "MaxPossibleClusters (%d).\n"
               "  The cross-chunk merge succeeded (clusters matched between\n"
               "  adjacent chunks), but there are more unique global units\n"
               "  than the MaxPossibleClusters table can hold.\n"
               "  Fix: increase -MaxPossibleClusters to at least %d\n"
               "  (nChunks * MaxClusters is a safe upper bound).\n"
               "  Falling back to CEMTwoPhase on the full session.\n",
               nGlobal, MaxPossibleClusters,
               nGlobal + 10);
        return CEMTwoPhase(timeMergeIter);
    }

    std::unordered_map<int,int> packedToGlobal;
    packedToGlobal.reserve(allModels.size());
    for (const auto& cm : allModels)
        packedToGlobal[cm.chunkIdx * MaxPossibleClusters + cm.localClusterId] =
            cm.globalClusterId;

    // ── Post-Phase-6 alignment site ─────────────────────────────────────
    // Runs after cross-chunk model matching: per-chunk clusters have been
    // mapped to global cluster IDs (above) but Class[] is still per-chunk.
    // Aligns spikes to per-chunk cluster means BEFORE the Phase 7 init
    // block remaps Class[] to global IDs.
    RunAlignmentBlock(TimeShiftAlignAfterPhase5, "Phase 5a");

    // ── Phase 7: global warm-start EM ───────────────────────────────────────
    nDims  = nFullDims;
    nDims2 = nDims * nDims;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = 0;
    for (int p = 0; p < nPoints; p++) {
        const int pp = pointPacked[p];
        auto it = (pp >= 0) ? packedToGlobal.find(pp) : packedToGlobal.end();
        const int g = (it != packedToGlobal.end()) ? it->second : 0;
        Class[p] = g;
        ClassAlive[g] = 1;
    }
    Reindex();

    float score;
    if (globalMergeIter <= 0) {
        // GlobalMerge=0: skip Phase 7 entirely.  Emit one MStep/EStep so
        // LogP is valid for ComputeScore(), but do not reassign Class[].
        LockedStderr( "[Phase 6] Global EM: skipped (GlobalMergeIter=0)\n");
        Output("Phase 7 skipped (GlobalMerge=0) — using Phase 6 assignment directly\n");
        // Force CPU path for Phase 7 post-merge scoring.
        // GPU EStep writes d_LogP in GPU memory; the CPU LogP.m_Data clamp below
        // would be a no-op on the GPU path.  Temporarily null gpu so MStep/EStep/
        // ComputeScore all run on CPU, where LogP.m_Data is authoritative.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        // Reassign any spikes whose cluster was deleted by MStep (singular covariance)
        // to noise (class 0) so EStep and ComputeScore see valid Class[] values.
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();
        {
            int nNan = 0;
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) { lp = kLargeLogP; nNan++; }
            }
            if (nNan > 0)
                Output("Phase3-skip: clamped %d non-finite LogP entries\n", nNan);
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu);
#endif
        if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
    } else {
        LockedStderr( "[Phase 6] Global warm-start EM\n");
        Output("Phase 7: global warm-start EM — %d clusters, max %d iters\n",
               nClustersAlive, globalMergeIter);
        int   iter = 0, nChanged = 1;
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu2 = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();  // CPU path: populates LogP.m_Data directly
        {
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) lp = kLargeLogP;
            }
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu2);
#endif
        FullStep = 1;
        for (; iter < globalMergeIter; iter++) {
            MStep(); EStep(); nChanged = CStep(); ConsiderDeletion();
            score = ComputeScore();
            if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
            if (Verbose >= 2)
                Output("  P3 iter %d: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);
            FullStep = 1;
            if (nChanged == 0) { Output("Phase 7 converged at iter %d\n", iter); break; }
        }
    }

    // Per-phase quality summary after Phase 7 (Global EM) — gives a
    // checkpoint before DipSplit potentially mutates the cluster set.
    LogGlobalClusterState("Phase 6 (Global EM)");
    ReportClusterQuality("Phase 7");

    // ── Phase 6a (optional): post-merge cluster realignment ────────────────
    // Named "6a" because it refines Phase 6's cross-chunk merges, but
    // EXECUTES AFTER Phase 7 (Global EM) has consolidated chunk-local
    // clusters into the final global cluster set — only at that point
    // are the merged means stable enough for a meaningful alignment
    // pass.  When -TimeShiftAlignPostMerge != 0, run another
    // TimeShiftAlignPhase pass against the post-Phase-7 global cluster
    // state.  This catches realignment opportunities that opened up
    // only after Phase 6's cross-chunk merges consolidated chunk-local
    // clusters into global
    // units: a spike whose Phase-1.5 alignment was optimal vs. its
    // chunk-local cluster mean may not be optimal vs. the global cluster
    // mean (which is the weighted average across all the chunk-local
    // pieces).  Effectively replicates what Klusters' realignment dialog
    // does post-hoc on the output, but does it inside the run so any
    // downstream consumer (final mean waveform, .clu file) sees clean
    // features.  No-op when m_timeShiftReady is false.
    if (TimeShiftAlignPostMerge != 0 && m_timeShiftReady) {
        LockedStderr( "[Phase 6a] Post-merge cluster realignment\n");
        const int nShifted = TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);
        int nCOMShifted = 0;
        if (EnergyCOMRealign != 0) {
            LockedStderr( "[Phase 6a] Energy-COM realignment\n");
            nCOMShifted = EnergyCOMRealignPhase(NbChannels, NbSamplesPerSpike);
        }
        if (nShifted > 0 || nCOMShifted > 0) {
            // Refresh global state after realignment: MStep recomputes
            // Mean/Cov from updated Data[], EStep populates LogP, then a
            // ComputeScore captures the post-realignment score.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuR = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuR);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Phase 6a (post-merge realign)"); ReportClusterQuality("Phase 7a");
        }
    }

    // ── Phase 6b (optional): mean-waveform subtraction merge ─────────
    // Same naming logic as Phase 6a: named "6b" because it refines the
    // Phase 6 merge decisions using post-merged means, but EXECUTES
    // AFTER Phase 7 (and after Phase 6a if enabled).  Runs after
    // Phase 6a so the means it aggregates already reflect the final
    // post-merge alignment.  Opt-in via
    // -MeanSubtractionMergeEnable 1; threshold via
    // -MeanSubtractionMergeThresh (default 0.05), cyclic-shift radius
    // via -MeanSubtractionMergeMaxShift (default 3).  See
    // KK::FinalMeanSubtractionMerge for algorithm details.
    if (MeanSubtractionMergeEnable != 0) {
        LockedStderr( "[Phase 6b] Mean-subtraction template merge "
                        "(threshold D < %.3f)\n",
                static_cast<double>(MeanSubtractionMergeThresh));
        const int nMergedSub =
            FinalMeanSubtractionMerge(NbChannels, NbSamplesPerSpike);
        if (nMergedSub > 0) {
            // Same state-refresh dance as Phase 6a — Class[] now points
            // to consolidated roots; MStep recomputes Mean/Cov, Reindex
            // compacts dead IDs, EStep populates LogP for the new layout.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuB = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuB);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Phase 6b (mean-sub merge)"); ReportClusterQuality("Phase 7b");
        }
    }

    // ── Phase 7c — klusters-faithful per-spike realignment ─────────────
    // Mirror of Driver B (second overload).  See KK::KlustersStyleRealign-
    // AllClusters for algorithm details.  Opt-in via -KlustersRealignEnable 1.
    if (KlustersRealignEnable != 0) {
        LockedStderr(
                "[Phase 7c] Klusters-faithful per-spike realignment "
                "(maxShift=%d samples, minSize=%d)\n",
                KlustersRealignMaxShift, KlustersRealignMinSize);
        const int nChanged =
            KlustersStyleRealignAllClusters(NbChannels, NbSamplesPerSpike);
        if (nChanged > 0) {
            LogGlobalClusterState("Phase 7c (klusters realign)");
        }
    }

    // Phase 8 DipSplit removed: per-chunk DipSplit now runs as Phase 1b,
    // before cross-chunk model matching, so global splits at this stage
    // would be operating on already-merged clusters where apparent
    // bimodality is more likely a drift artefact than a real second mode.

    // Per-phase quality summary at end of pipeline (after DipSplit).  Catches
    // failures like "one cluster ate everything" before the user sees it in
    // the GUI.  See KK::ReportClusterQuality for metric definitions.
    ReportClusterQuality("final");

    Output("RunChunkedCEM(ext) done: %d clusters, score %.7g\n", nClustersAlive, score);
    return score;
}


// ---------------------------------------------------------------------------
// Chunked-CEM pipeline (RunChunkedCEM).  Phase numbering matches what is
// printed as `[Phase X]` banners in the run log.
//
//   Phase 0   Chunking — divide session into drift-adaptive chunks.
//   Phase 1   Per-chunk CEM — independent CEMTwoPhase on each chunk.
//   Phase 1a  Cluster realignment — xcorr-based per-spike shift (was 1.5).
//   Phase 1b  Per-chunk DipSplit — bimodal-cluster splits (was 1.6).
//   Phase 2   Per-chunk refractory split.
//   Phase 2a  Per-cluster CEM refinement.
//   Phase 2b  Chunk re-CEM (rebuild after per-cluster work).
//   Phase 4   Mean waveform harvest — build templates for xcorr.
//   Phase 5   Within-chunk template match (xcorr + optional eig-ratio veto).
//   Phase 6   Cross-chunk model match (overlap voting + edge xcorr).
//   Phase 7   Global warm-start EM (optional; -GlobalMergeIter > 0).
//   Phase 6a  Post-merge cluster realignment (was 7.5; optional, patch26).
//   Phase 8   Legacy global DipSplit (deprecated; gated off by default).
//   Phase 9   Shift commit (write refined .spk/.fet/.res).
//
// Pipeline phase map (RunChunkedCEM — both overloads):
//   Phase 0   global preseed (optional)
//   Phase 1   per-chunk CEMTwoPhase (parallel over chunks)
//     1a      per-cluster shift-probe alignment
//     1b      per-chunk DipSplit  ← KDE valley split, runs before Phase 2
//     1c      alignment after DipSplit
//   Phase 2   per-chunk refractory split + subspace recluster
//     2.D     refractory split (physical prior, runs FIRST inside Phase 2)
//     2a      per-cluster ordinary CEM in full feature space
//     2b      chunk-level warm-start CEM (boundary reassignment + merging)
//     2.5     SECOND per-chunk DipSplit (catches bimodality EXPOSED by 2a)
//     2c      alignment after the chunk-level re-CEM
//   Phase 3   mean-waveform harvest ([Phase 3] log tag — a setup step
//             for the cross-chunk template matching in Phases 5/6,
//             not an independent clustering phase)
//   Phase 5   within-Phase-6-iteration circular xcorr template matching
//   Phase 6   cross-chunk model matching (begins with a mergeThresh
//             calibration check)
//   Phase 7   global warm-start EM (full dimensionality)
//   Phase 6a  post-Phase-7 cluster realignment (named '6a' because it's
//             a refinement of Phase 6's merge decisions, but executes
//             AFTER Phase 7 has consolidated chunk-locals into globals)
//   Phase 6b  post-Phase-7 mean-waveform subtraction merge (same naming
//             logic as 6a)
//   Phase 9   TimeShiftFinalize (final commit of cumulative shifts)
//
// Phases 4 and 8 are intentionally unused.  Phase 8 was the legacy
// global post-merge DipSplit; per-chunk DipSplit at 1b + 2.5 supplants
// it without the drift-axis false positives that bisect single drifting
// units across the session boundary.
// ---------------------------------------------------------------------------
float KK::RunChunkedCEM(float chunkMinutes,
                         float samplingRate,
                         float mergeThresh,
                         int   globalMergeIter,
                         int   timeMergeIter,
                         float chunkOverlapMinutes,
                         float chunkPreseedFraction)
{
    const int nFullDims    = nDims;
    const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
    const int timeDim      = nDims - 1;

    // -------------------------------------------------------------------
    // Phase 0: build temporal chunk index
    //
    // timeRawMin/Max captured by LoadData() before normalisation.
    // Normalised time in [0,1] spans sessionSamples raw samples.
    // chunkFrac = fraction of [0,1] covered by one chunk.
    // -------------------------------------------------------------------
    const float sessionSamples = timeRawMax - timeRawMin;
    if (sessionSamples <= 0.0f) {
        Output("RunChunkedCEM: cannot determine session length — falling back.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    const float chunkSamples = samplingRate * chunkMinutes * 60.0f;
    const float chunkFrac    = chunkSamples / sessionSamples;
    const int   nChunks      = std::max(1, static_cast<int>(std::ceil(1.0f / chunkFrac)));

    LockedStderr( "[Phase 0] Chunking (%.0f min, %d chunks, %.0f min/chunk; %s)\n",
            sessionSamples / samplingRate / 60.0f, nChunks, chunkMinutes,
            (chunkPreseedFraction > 0.0f)
                ? "global preseed enabled"
                : "preseed disabled — per-chunk random-init CEM");
    Output("RunChunkedCEM: session %.1f min, chunk %.1f min, %d chunks\n",
           sessionSamples / samplingRate / 60.0f, chunkMinutes, nChunks);

    if (nChunks <= 1) {
        Output("Session shorter than one chunk — running CEMTwoPhase directly.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // If chunkOverlapMinutes > 0, spikes within the trailing overlapFrac of
    // chunk k are also appended to chunk k+1.  After per-chunk EM their
    // assignments form a vote matrix that supplements Mahalanobis MNN.
    const float chunkOverlapFrac = (chunkOverlapMinutes > 0.0f)
        ? (samplingRate * chunkOverlapMinutes * 60.0f) / sessionSamples
        : 0.0f;
    if (chunkOverlapFrac >= chunkFrac * 0.5f && chunkOverlapFrac > 0.0f)
        Output("RunChunkedCEM: WARNING — overlap (%.1f min) >= half chunk size; "
               "consider reducing ChunkOverlapMinutes.\n", chunkOverlapMinutes);

    struct OverlapEntry { int localK; int localK1; };
    // nChunks >= 2 here: the early return at the top of this function exits
    // when nChunks <= 1, so `nChunks - 1` is unconditionally valid.
    std::vector<std::vector<OverlapEntry>> overlapForPair(nChunks - 1);

    std::vector<std::vector<int>> chunkPoints(nChunks);
    for (int p = 0; p < nPoints; p++) {
        const float t = Data[p * nDims + timeDim];
        int k = static_cast<int>(t / chunkFrac);
        if (k < 0) k = 0;
        if (k >= nChunks) k = nChunks - 1;
        const int localK = static_cast<int>(chunkPoints[k].size());
        chunkPoints[k].push_back(p);
        // Trailing overlap: if within overlapFrac of k's right boundary, also add to k+1
        if (chunkOverlapFrac > 0.0f && k + 1 < nChunks) {
            const float rightBoundary = (k + 1) * chunkFrac;
            if ((rightBoundary - t) <= chunkOverlapFrac) {
                const int localK1 = static_cast<int>(chunkPoints[k + 1].size());
                chunkPoints[k + 1].push_back(p);
                overlapForPair[k].push_back({localK, localK1});
            }
        }
    }

    // Merge undersized trailing chunks into their predecessor.
    // Need at least nStartingClusters * nSpatialDims * 3 spikes to reliably
    // estimate all cluster covariances.
    const int minSpikes = nStartingClusters * nSpatialDims * 3;
    for (int k = nChunks - 1; k >= 1; k--) {
        if (static_cast<int>(chunkPoints[k].size()) < minSpikes) {
            Output("  Chunk %d: %d spikes < %d minimum — merging into chunk %d.\n",
                   k, static_cast<int>(chunkPoints[k].size()), minSpikes, k - 1);
            for (int p : chunkPoints[k]) chunkPoints[k-1].push_back(p);
            chunkPoints[k].clear();
        }
    }
    // Build compacted-index → original-index mapping BEFORE erasing empty chunks.
    // This is required so the overlap vote collection loop can look up
    // overlapForPair[origK] rather than overlapForPair[k], which diverge
    // whenever a non-trailing chunk is merged away and the compacted indices
    // shift relative to the original ones.
    std::vector<int> activeOrigIdx;
    activeOrigIdx.reserve(nChunks);
    for (int k = 0; k < nChunks; k++)
        if (!chunkPoints[k].empty())
            activeOrigIdx.push_back(k);

    chunkPoints.erase(
        std::remove_if(chunkPoints.begin(), chunkPoints.end(),
                       [](const std::vector<int>& v){ return v.empty(); }),
        chunkPoints.end());
    const int nActive = static_cast<int>(chunkPoints.size());

    if (nActive <= 1) {
        Output("Only one chunk after size-check merges — running CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }
    Output("Active chunks: %d\n", nActive);

    // -------------------------------------------------------------------
    // Phase 0: optional global preseed (re-enabled in patch10).
    //
    // When chunkPreseedFraction > 0, sample that fraction of spikes
    // globally and run a single CEMTwoPhase on the subsample to get
    // shared starting centres for every chunk in Phase 1.  Pros: chunks
    // start from a coherent global cluster picture, which can stabilise
    // small/sparse chunks and helps Phase 5 cross-chunk matching.
    // Cons: bias — every chunk inherits the same initial bias from the
    // subsample, and rare units that are absent from the subsample get
    // no preseed centre at all.
    //
    // When chunkPreseedFraction <= 0, skip this and let each chunk run
    // its own random-init CEM (the patch6 default).  In that mode
    // chunkInitRandom = true gates the per-thread KK to use
    // irand(1, nCentres) per spike rather than farthest-point init.
    //
    // Set -ChunkPreseedFraction 0 (or omit) to keep the patch6 default.
    // Set -ChunkPreseedFraction 0.05..0.20 to A/B test against random
    // init.  PreseedSubsampleCEM emits its own progress lines.
    // -------------------------------------------------------------------
    std::vector<float> globalPreseedCentres;
    if (chunkPreseedFraction > 0.0f) {
        Output("Phase 0: global preseed (fraction=%.3f, nCentres=%d)\n",
               chunkPreseedFraction, MaxClusters - 1);
        globalPreseedCentres = PreseedSubsampleCEM(
            chunkPreseedFraction, MaxClusters - 1,
            nSpatialDims, timeMergeIter);
        if (globalPreseedCentres.empty()) {
            Output("Phase 0: preseed produced no centres — falling back to "
                   "per-chunk random init.\n");
        } else {
            Output("Phase 0: preseed produced %zu centres × %d spatial dims\n",
                   globalPreseedCentres.size() / nSpatialDims, nSpatialDims);
        }
    }

    // -------------------------------------------------------------------
    // Phase 1: per-chunk CEMTwoPhase — parallel over chunks
    //
    // Each chunk builds a self-contained KK sub-object with:
    //   suppressBestSave = true   — no writes to global kSv during parallel section
    //   its own cholFlat/bestCholFlat — no shared Cholesky storage
    //
    // To avoid per-chunk heap allocation inside the parallel region (which
    // causes allocator contention across threads), we pre-allocate one KK
    // scratch object per OMP thread, sized to the largest chunk.  Each
    // iteration reinitialises the relevant fields via ReinitForSplit rather
    // than allocating fresh arrays.
    //
    // Thread-local vectors accumulate models and point assignments.
    // A serial reduction gathers them into allModels / pointPacked after
    // the parallel block, so no atomic or mutex is needed.
    //
    // Output() calls inside CEMTwoPhase are guarded by a single critical
    // section to prevent interleaved log lines; set Verbose=0 to suppress.
    // -------------------------------------------------------------------
    std::vector<ChunkModel> allModels;
    std::vector<int>        pointPacked(nPoints, -1);  // -1 sentinel: unwritten -> noise

    // Per-chunk accumulators: indexed by chunk k.
    std::vector<std::vector<ChunkModel>> perChunkModels(nActive);
    std::vector<std::vector<std::pair<int,int>>> perChunkAssign(nActive);
    std::vector<std::vector<int>> perChunkClass(nActive);  // for overlap votes
    // per-chunk score (for logging)
    std::vector<float> perChunkScore(nActive, 0.0f);
    std::vector<int>   perChunkNClusters(nActive, 0);

    // Find the largest chunk size so we can pre-allocate at that capacity.
    int maxChunkSize = 0;
    for (int k = 0; k < nActive; k++)
        maxChunkSize = std::max(maxChunkSize, static_cast<int>(chunkPoints[k].size()));

    // One pre-allocated KK scratch per OMP thread.
#ifdef _OPENMP
    // When running as a ParallelK worker, ompTeamSize limits the inner
    // team so that multiple workers can share the machine without fighting
    // for threads.  0 = use all available (normal non-parallel path).
    const int nThreads = (ompTeamSize > 0)
        ? ompTeamSize
        : omp_get_max_threads();
#else
    const int nThreads = 1;
#endif
    // When preseed succeeded, start each chunk from the preseed's K rather than
    // the outer loop's nStartingClusters.  The preseed already found a good
    // partition of the full dataset, so seeding chunks at that K avoids the
    // slow bottom-up split cascade from K=2.
    //
    // With Phase 0 preseed disabled (default), globalPreseedCentres is always
    // empty and chunkStartK falls back to nStartingClusters.  Each chunk
    // starts at K = nStartingClusters with random Class[] init via
    // chunkInitRandom — TrySplits and ConsiderDeletion settle K from there.
    const int chunkStartK = (!globalPreseedCentres.empty())
        ? static_cast<int>(globalPreseedCentres.size() / nSpatialDims) + 1
        : nStartingClusters;

    std::vector<KK> threadKc(nThreads);
    for (int t = 0; t < nThreads; t++) {
        threadKc[t].nDims             = nFullDims;
        threadKc[t].nPoints           = maxChunkSize;
        threadKc[t].nStartingClusters = chunkStartK;
        threadKc[t].penaltyMix        = penaltyMix;
        threadKc[t].suppressBestSave  = true;
        // Deletion floor: with preseed, hold near chunkStartK to preserve
        // the preseed's K.  Without preseed (default), drop to 2 so CEM
        // can shrink down naturally from random init.
        threadKc[t].minClustersAlive  = (!globalPreseedCentres.empty())
            ? std::max(2, chunkStartK - 4)
            : 2;
        threadKc[t].preseedCentres    = globalPreseedCentres;  // empty by default
        threadKc[t].chunkInitRandom   = globalPreseedCentres.empty();
        threadKc[t].AllocateArrays();
        threadKc[t].AllocateCholeskyVecs();
    }

    const int runsPerChunk = (nRuns > 0) ? nRuns : 1;
    const int nFlatPhase1  = nActive * runsPerChunk;

    struct ChunkRunResult {
        float            score     = HugeScore;
        int              nClusters = 0;
        std::vector<int> cls;
    };
    std::vector<ChunkRunResult> flatResults(static_cast<size_t>(nFlatPhase1));
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k    = fi / runsPerChunk;
        const int nPts = static_cast<int>(chunkPoints[static_cast<size_t>(k)].size());
        flatResults[static_cast<size_t>(fi)].cls.assign(static_cast<size_t>(nPts), 0);
        flatResults[static_cast<size_t>(fi)].score = HugeScore;
    }

    LockedStderr( "[Phase 1] Chunk CEM (%d threads, %d chunks × %d runs = %d units)\n",
            nThreads, nActive, runsPerChunk, nFlatPhase1);
        #pragma omp parallel for schedule(dynamic) default(none) \
        num_threads(nThreads) \
        shared(flatResults, chunkPoints, nActive, nFullDims, timeMergeIter, threadKc, stderr) \
        firstprivate(MaxPossibleClusters, chunkStartK, penaltyMix, \
                     runsPerChunk, nFlatPhase1, HugeScore, RandomSeed)
    for (int fi = 0; fi < nFlatPhase1; fi++) {
        const int k   = fi / runsPerChunk;
        const int run = fi % runsPerChunk;
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        KK& Kc = threadKc[
#ifdef _OPENMP
            omp_get_thread_num()
#else
            0
#endif
        ];
        kk_seed_rng(kk_mix_seed(kk_mix_seed(static_cast<uint64_t>(RandomSeed),
                                            static_cast<uint64_t>(k)),
                                static_cast<uint64_t>(run)));
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = chunkStartK;
        Kc.NoisePoint        = 1;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        const float runScore = Kc.CEMTwoPhase(timeMergeIter);

        ChunkRunResult& cr = flatResults[static_cast<size_t>(fi)];
        cr.score     = runScore;
        cr.nClusters = Kc.nClustersAlive;
        for (int i2 = 0; i2 < nPts; i2++)
            cr.cls[static_cast<size_t>(i2)] = Kc.Class[i2];

        #pragma omp critical
        { fprintf(stderr, "  [chunk %d/%d  run %d/%d] score=%.4g  nclusters=%d\n",
                  k + 1, nActive, run + 1, runsPerChunk,
                  runScore, Kc.nClustersAlive); }
    }

    // Serial reduction: pick best run per chunk, rebuild KK, harvest models.
    for (int k = 0; k < nActive; k++) {
        const std::vector<int>& pts = chunkPoints[static_cast<size_t>(k)];
        const int nPts = static_cast<int>(pts.size());

        float bestScore = HugeScore;
        int   bestRun   = 0;
        for (int run = 0; run < runsPerChunk; run++) {
            const float s = flatResults[static_cast<size_t>(k * runsPerChunk + run)].score;
            if (s < bestScore) { bestScore = s; bestRun = run; }
        }
        const ChunkRunResult& best = flatResults[
            static_cast<size_t>(k * runsPerChunk + bestRun)];

        KK& Kc = threadKc[0];
        Kc.ReinitForSplit(nPts, nFullDims, penaltyMix);
        Kc.nStartingClusters = chunkStartK;
        for (int i2 = 0; i2 < nPts; i2++) {
            const int p2 = pts[static_cast<size_t>(i2)];
            for (int d = 0; d < nFullDims; d++)
                Kc.Data[i2 * nFullDims + d] = Data[p2 * nFullDims + d];
        }
        for (int i2 = 0; i2 < nPts; i2++) Kc.Class[i2] = best.cls[static_cast<size_t>(i2)];
        for (int c = 0; c < MaxPossibleClusters; c++) Kc.ClassAlive[c] = 0;
        for (int i2 = 0; i2 < nPts; i2++) Kc.ClassAlive[Kc.Class[i2]] = 1;
        Kc.nClustersAlive = 0;
        for (int c = 0; c < MaxPossibleClusters; c++)
            if (Kc.ClassAlive[c]) Kc.AliveIndex[Kc.nClustersAlive++] = c;
        Kc.MStep();

        perChunkScore[k]     = bestScore;
        perChunkNClusters[k] = best.nClusters;

        auto& models = perChunkModels[k];
        for (int cc = 0; cc < Kc.nClustersAlive; cc++) {
            const int c = Kc.AliveIndex[cc];
            ChunkModel cm;
            cm.chunkIdx        = k;
            cm.localClusterId  = c;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(nFullDims, 0.0f);
            cm.cov.assign(nFullDims * nFullDims, 0.0f);
            for (int d = 0; d < nFullDims; d++)
                cm.mean[d] = Kc.Mean[c * nFullDims + d];
            for (int r = 0; r < nFullDims; r++)
                for (int col = r; col < nFullDims; col++)
                    cm.cov[r * nFullDims + col] =
                        Kc.Cov[c * Kc.nDims2 + r * nFullDims + col];
            for (int i = 0; i < nPts; i++)
                if (Kc.Class[i] == c) cm.nMembers++;
            models.push_back(std::move(cm));
        }

        auto& assign = perChunkAssign[k];
        assign.reserve(nPts);
        for (int i = 0; i < nPts; i++)
            assign.emplace_back(pts[static_cast<size_t>(i)],
                                k * MaxPossibleClusters + Kc.Class[i]);

        auto& classArr = perChunkClass[k];
        classArr.resize(nPts);
        for (int i = 0; i < nPts; i++) classArr[i] = Kc.Class[i];
    }

    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1");

    // ── Provisional Class[] seeding for Phase 2 ─────────────────────────────
    // Phase 1 ran on local threadKc[] objects; K1.Class[] is all-zero here.
    // Phase 2 SubspaceReclusterPerChunk needs alive cluster statistics.
    if (SubspaceRecluster > 0) {
        std::fill(Class.m_Data, Class.m_Data + nPoints, 0);
        std::fill(ClassAlive.m_Data, ClassAlive.m_Data + MaxPossibleClusters, 0);
        ClassAlive[0] = 1;
        for (int k_ps = 0; k_ps < nActive; ++k_ps) {
            const auto& pts_ps = chunkPoints[static_cast<size_t>(k_ps)];
            const auto& cls_ps = perChunkClass[static_cast<size_t>(k_ps)];
            for (int i_ps = 0; i_ps < static_cast<int>(pts_ps.size()); ++i_ps) {
                const int p_ps = pts_ps[static_cast<size_t>(i_ps)];
                const int c_ps = cls_ps[static_cast<size_t>(i_ps)];
                if (Class[p_ps] == 0 && c_ps > 0) {
                    Class[p_ps] = c_ps;  ClassAlive[c_ps] = 1;
                }
            }
        }
        Reindex();
        nDims = nFullDims;  nDims2 = nDims * nDims;
        MStep();
        for (int cc2 = 1; cc2 < nClustersAlive; ++cc2) {
            const int c2 = AliveIndex[cc2];
            if (Cholesky(Cov.m_Data + c2*nDims2, cholFlat.data() + c2*nDims2, nDims))
                ClassAlive[c2] = 0;
        }
        Reindex();
        Output("Provisional Class[] seeded: %d alive clusters\n", nClustersAlive);
    }

    // ── Phase 1a: per-cluster shift-probe alignment ────────────────────────
    //
    // For each alive cluster, picks the per-spike δ ∈ {-N,…,+N} that
    // minimises Mahalanobis² to the cluster's own Gaussian (using the
    // cluster's Mean + Cholesky-factored Cov).  Aligns spikes WITHIN each
    // cluster — not to a canonical peak sample.  The cluster's mean is
    // wherever Phase 1 CEM put it; this phase tightens spikes around that
    // centre, it does not move the centre to peakSampleIndex.
    //
    // Caveats inherited from the design:
    //  - Per-cluster myopia: spikes near a cluster boundary may be pulled
    //    deeper into the wrong cluster (the score sees only the assigned
    //    cluster's distribution, not neighbours').  The next EStep will
    //    re-evaluate, but on slightly distorted features.
    //  - Asymmetric search window: candOk[ci] enforces |baseCum + δ| ≤
    //    m_timeShiftMaxAbs, so spikes already near ±maxAbs see fewer
    //    candidates on the cap-side.  Intentional bound on cumulative
    //    drift, not a bug.
    //  - Pre-shifted PCA basis is fixed at InitTimeShift; large cumulative
    //    shifts (across many iterations) make the basis statistically
    //    less efficient but not incorrect.
    //
    // Stats are current at this point: the SubspaceRecluster=1 seeding
    // block above ran MStep + Cholesky.  When SubspaceRecluster=0 no
    // clusters are alive yet and the call no-ops cleanly (the loop in
    // TimeShiftAlignPhase iterates ClassAlive and finds nothing).
    RunAlignmentBlock(TimeShiftAlignAfterPhase1, "Phase 1a");

    // ── Phase 1b: per-chunk DipSplit ──────────────────────────────────────
    //
    // Catches bimodal/elongated clusters that the parametric Phase 1 CEM
    // missed.  Runs per-chunk before Phase 2 so subsequent passes operate
    // on the most-correctly-split inputs, and so cross-chunk merge in
    // Phase 6 sees the correct cluster count.  Replaces the old
    // post-Phase-7 global Phase 8 DipSplit.  No-op when DipSplitEnable=0.
    DipSplitPerChunk(chunkPoints, perChunkClass, perChunkModels, nFullDims,
                     "Phase 1b");
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 1b");
    RunAlignmentBlock(TimeShiftAlignAfterPhase1b, "Phase 1c");

    // ── Phase 2: per-chunk refractory split + subspace reclustering ────────
    //
    // P2.D: refractory split runs FIRST.  Refractory contamination is a
    // physical signal — two units sharing a cluster cannot satisfy
    // biological refractory periods — so it's a stronger prior than any
    // covariance-based statistical split.  Splitting on physics first gives
    // the subspace CEM cleaner inputs.
    //
    // The earlier order (subspace then refractory) had a failure mode: if
    // subspace incorrectly split a clean cluster (multiple-comparisons
    // false positive), the children might individually pass the 1% ISI
    // contamination threshold (each carrying half the violations) and the
    // over-split would stick.  Running refractory first locks in physically-
    // motivated splits before the statistical pass can over-fit.
    if (SubspaceRecluster > 0) {
        // Refractory-period guided split — catches mixtures by exploiting the
        // 1-neuron-per-refractory-window constraint.  Absolute refractory =
        // 1.5 ms × sampling rate samples.  Trigger when ISI contamination
        // rate >= 1%.
        if (SamplingRate > 0.0f) {
            const float refractSamp    = 1.5f * SamplingRate / 1000.0f;
            const float sessLenSamp    = timeRawMax - timeRawMin;
            LockedStderr( "[Phase 2] Per-chunk refractory split (refract=%.0f samp, "
                            "contam_thresh=1%%)\n", refractSamp);
            RefractorySplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels,
                nFullDims, refractSamp, 0.01f, sessLenSamp);
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2");
        }

        // Phase 2a: per-cluster ordinary CEM in the full feature space.
        // Replaces SubspaceReclusterPerChunk.  For each cluster in each
        // chunk, runs CEM with splits enabled to find bimodal substructure;
        // updates perChunkClass[] only.
        PerClusterCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a");

        // Phase 2a.5: per-chunk DipSplit (see commentary at the matching
        // insertion in the first overload of RunChunkedCEM).  Gated by
        // DipSplitEnable AND DipSplitBeforePhase2b.
        if (DipSplitEnable != 0 && DipSplitBeforePhase2b != 0) {
            DipSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims,
                "Phase 2a.5");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.5");
        }

        // Phase 2a.6: per-chunk HullSplit, see commentary at the matching
        // insertion in the first overload of RunChunkedCEM.
        if (HullSplitEnable != 0) {
            HullSplitPerChunk(
                chunkPoints, perChunkClass, nFullDims, "Phase 2a.6");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.6");
        }

        // Phase 2a.7: per-channel amplitude+phase split, see commentary at
        // the matching insertion in the first overload.
        if (PerChannelSplitEnable != 0) {
            PerChannelSplitPerChunk(
                chunkPoints, perChunkClass,
                NbChannels, NbSamplesPerSpike, "Phase 2a.7");
            LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2a.7");
        }

        // Phase 2b: chunk-level warm-start CEM.  Lets boundary spikes
        // reassign across the new fine-grained label set and lets CEM
        // merge oversplit fragments via ConsiderDeletion.  Rebuilds
        // perChunkModels[] from the converged state.
        ChunkReCEMPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims);
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 2b");

        // Phase 2.5: second per-chunk DipSplit pass.
        //
        // The Phase 1c DipSplit ran before subspace reclustering, so it
        // could only see bimodality in the parent clusters produced by
        // Phase 1 CEM.  PerClusterCEMPerChunk (Phase 2a) re-partitions
        // those parents into finer pieces via likelihood-based CEM with
        // splits enabled — but CEM is a parametric Gaussian test that
        // absorbs single-dimension bimodality by inflating variance
        // along the bimodal axis (the inflation penalty is smaller than
        // the constant cost of a new cluster).  Clusters that emerge
        // from Phase 2a with a clear KDE valley in one PC projection
        // but a covariance the parametric test accepts are exactly the
        // cases this second pass catches.
        //
        // Per-chunk (not global) for the same reason as Phase 1c: a
        // chunk's spikes don't span the session-drift range, so apparent
        // bimodality from drift is suppressed.
        DipSplitPerChunk(
            chunkPoints, perChunkClass, perChunkModels, nFullDims,
            "Phase 2.5");

        // Phase 2b.5: K-template chunk split (optional, off by default).
        // For each chunk, picks K well-isolated reference cluster means
        // and partitions every "source" cluster's spikes by their
        // nearest reference template.  Each viable (source, ref) bucket
        // becomes a new chunk-local cluster.  Phase 6 (cross-chunk
        // model matching) handles consolidation of the new clusters
        // into global units.  Enable with -KnnSplitPerChunkEnable 1.
        // -KnnSplitMode 0 (default): legacy nearest-template; mode 1:
        // klusters-faithful K-NN majority-vote (see wave_knn_split.h).
        if (KnnSplitPerChunkEnable) {
            if (KnnSplitMode == 1) {
                WaveKnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            } else {
                KnnSplitPerChunk(
                    chunkPoints, perChunkClass, perChunkModels, nFullDims);
            }
        }
        if (FullCemSplitEnable) {
            FullCemSplitPerChunk(
                chunkPoints, perChunkClass, perChunkModels, nFullDims);
        }
	
    }


    // Phase 7 (Global EM) alignment also disabled: chunk means from
    // CEMTwoPhase are already blended by timeMergeIter and would produce
    // spurious shifts on clean data.  ConsiderDeletion / TimeShiftMergeEvaluate
    // handle the time-shift cases that actually occur (genuine same-unit
    // mergers across drift).

    // ── Post-Phase-2 alignment site ─────────────────────────────────────
    RunAlignmentBlock(TimeShiftAlignAfterPhase2, "Phase 2c");

    // Phase-ordering note: Phase 8 (global post-merge DipSplit) was
    // removed.  Its function is now served by per-chunk DipSplit at
    // Phase 1b and a second pass at Phase 2.5 — operating per-chunk
    // avoids the drift-axis false positives a global pass produces on
    // session-spanning clusters.  Phases 3 (mean-waveform harvest, the
    // [Phase 3] log tag below), 4, and 8 are intentionally non-major
    // phase numbers in the current pipeline; the comment block above
    // RunChunkedCEM enumerates which numbers are alive.

    // ── Serial meanWav harvest (post-realignment) ────────────────────────────
    // Runs AFTER WritePhase15Checkpoint so templates use realigned waveforms.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f)
        && NbChannels > 0 && NbSamplesPerSpike > 0)
        LockedStderr( "[Phase 3] Mean waveform harvest (channel-major xcorr format)\n");
    // Populate ChunkModel::meanWav for template matching.
    // Done serially after the parallel chunk loop since all chunks share
    // the same .spk file handle and fseeko calls cannot be parallelised safely.
    if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
        NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int wElems = NbChannels * NbSamplesPerSpike;
        char spkPathTM[STRLEN + 16];
        // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
        pickInputPath(spkPathTM, sizeof(spkPathTM), FileBase, "spk", ElecNo);
        FILE* spkTM = fopen(spkPathTM, "rb");
        if (spkTM) {
            for (int k = 0; k < nActive; k++) {
                const auto& pts  = chunkPoints[k];
                const auto& cls  = perChunkClass[k];
                const int   nPts = static_cast<int>(pts.size());

                // Build localClusterId → ChunkModel* map for this chunk
                std::unordered_map<int, ChunkModel*> lcToModel;
                for (auto& cm : perChunkModels[k])
                    if (cm.chunkIdx == k)
                        lcToModel[cm.localClusterId] = &cm;

                // Determine this chunk's time range so we can classify spikes
                // as left-edge (first 25%) / middle / right-edge (last 25%)
                // for the boundary-localised waveforms used in Phase 6's
                // cross-chunk xcorr.  Time = last feature dim, raw samples.
                float tMin = std::numeric_limits<float>::infinity();
                float tMax = -std::numeric_limits<float>::infinity();
                for (int i = 0; i < nPts; i++) {
                    const float t = Data[static_cast<size_t>(pts[i]) * nDims + (nDims - 1)];
                    if (t < tMin) tMin = t;
                    if (t > tMax) tMax = t;
                }
                const float tRange    = (tMax > tMin) ? (tMax - tMin) : 1.0f;
                const float tLeftEnd  = tMin + 0.25f * tRange;
                const float tRightBeg = tMin + 0.75f * tRange;

                // Accumulate per-cluster waveform sums.  Three accumulators
                // per cluster: full chunk, left-edge (first 25%), right-edge
                // (last 25%).  Every spike contributes to acc; spikes within
                // the edge windows additionally contribute to accLeft/Right.
                std::unordered_map<int, std::vector<int64_t>> acc, accLeft, accRight;
                std::unordered_map<int, int> nAcc, nAccLeft, nAccRight;
                std::vector<int16_t> row(static_cast<size_t>(wElems));
                for (int i = 0; i < nPts; i++) {
                    const int lc = cls[i];
                    if (lc == 0) continue;  // skip noise
                    if (!lcToModel.count(lc)) continue;
                    const int p2 = pts[i];
                    fseeko(spkTM,
                           static_cast<off_t>(p2) * wElems * sizeof(int16_t),
                           SEEK_SET);
                    if (fread(row.data(), sizeof(int16_t), wElems, spkTM)
                            != static_cast<size_t>(wElems)) continue;

                    // Apply committed shift (TimeShiftSplitEnable path):
                    // ensure the harvested waveform geometry matches the
                    // PCA features in Data[].
                    ShiftWaveformRowInPlace(row.data(), p2,
                                            NbChannels, NbSamplesPerSpike);

                    // Full-chunk accumulator
                    auto& a = acc[lc];
                    if (a.empty()) a.assign(static_cast<size_t>(wElems), 0);
                    // sample-major (.spk) → channel-major (XcorrDispatch)
                    for (int ch = 0; ch < NbChannels; ch++)
                        for (int s = 0; s < NbSamplesPerSpike; s++)
                            a[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                += row[static_cast<size_t>(s * NbChannels + ch)];
                    nAcc[lc]++;

                    // Edge accumulators.  A spike can only fall into one of
                    // the two edge windows (mutually exclusive at 25% each);
                    // middle-50% spikes contribute only to the chunk-wide mean.
                    const float t = Data[static_cast<size_t>(p2) * nDims + (nDims - 1)];
                    std::vector<int64_t>* edgeAcc = nullptr;
                    int* edgeN = nullptr;
                    if (t <= tLeftEnd)       { edgeAcc = &accLeft[lc];  edgeN = &nAccLeft[lc];  }
                    else if (t >= tRightBeg) { edgeAcc = &accRight[lc]; edgeN = &nAccRight[lc]; }
                    if (edgeAcc) {
                        if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                        for (int ch = 0; ch < NbChannels; ch++)
                            for (int s = 0; s < NbSamplesPerSpike; s++)
                                (*edgeAcc)[static_cast<size_t>(ch * NbSamplesPerSpike + s)]
                                    += row[static_cast<size_t>(s * NbChannels + ch)];
                        (*edgeN)++;
                    }
                }
                // Finalise full-chunk meanWav
                for (auto& [lc, a] : acc) {
                    int n2 = nAcc[lc];
                    if (n2 == 0) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWav.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWav[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                }
                // Finalise meanWavLeft.  Require a minimum of 5 spikes for a
                // meaningful mean — below that, leave empty so Phase 6 falls
                // back to chunk-wide meanWav for this cluster.
                for (auto& [lc, a] : accLeft) {
                    const int n2 = nAccLeft[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavLeft.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavLeft[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersLeft = n2;
                }
                for (auto& [lc, a] : accRight) {
                    const int n2 = nAccRight[lc];
                    if (n2 < 5) continue;
                    if (!lcToModel.count(lc)) continue;
                    ChunkModel* cm = lcToModel[lc];
                    cm->meanWavRight.resize(static_cast<size_t>(wElems));
                    for (int e = 0; e < wElems; e++)
                        cm->meanWavRight[static_cast<size_t>(e)] =
                            static_cast<int16_t>(a[static_cast<size_t>(e)] / n2);
                    cm->nMembersRight = n2;
                }
            }
            fclose(spkTM);
        }
    }
    // Declare overlap vote container here; ACTUAL build is deferred to
    // AFTER Phase 4 within-chunk merge has finalised the per-chunk
    // cluster IDs.  See the build block just before MergeChunkModels.
    //
    // Why deferred: vote keys are (clsK, clsK1) cluster ID pairs.
    // Building them from pre-Phase-4 perChunkClass bakes in cluster IDs
    // that Phase 4's lcRemap then merges away.  Phase 5 would receive
    // votes keyed by clusters that no longer exist, causing missed
    // cross-chunk matches (the vote-match step would search for
    // {origK.clsK_pre <-> origK1.clsK1_pre} pairs that have since
    // become {origK.canon_post <-> origK1.canon_post}).
    //
    // The companion comment at the allModels rebuild site below
    // documented the cluster-ID-consistency requirement but the
    // overlapVotes build had been left at its original pre-Phase-4
    // position.  This fix relocates it.
    std::vector<std::unordered_map<int,int>> overlapVotes(
        nActive > 0 ? nActive - 1 : 0);

    // -------------------------------------------------------------------
    // Phase 6: cross-chunk model matching
    // -------------------------------------------------------------------
    // Sanity-check mergeThresh against the chi²(nSpatialDims) distribution.
    // The symmetric Mahalanobis² distance between two draws from the SAME
    // Gaussian is distributed as chi²(nSpatialDims), so a threshold much
    // larger than chi²(nSpatialDims, 0.9999) means essentially every pair
    // is a candidate — the MNN filter degenerates and all local clusters
    // tend to union into one global component.
    // chi²(d, p) ≈ d * (1 - 2/(9d) + z_p * sqrt(2/(9d)))³  (Wilson-Hilferty)
    {
        const float d   = static_cast<float>(nSpatialDims);
        // z for p=0.9999 ≈ 3.719
        const float chi2_9999 = d * std::pow(1.0f - 2.0f/(9.0f*d) + 3.719f * std::sqrt(2.0f/(9.0f*d)), 3.0f);
        // z for p=0.99 ≈ 2.326
        const float chi2_99   = d * std::pow(1.0f - 2.0f/(9.0f*d) + 2.326f * std::sqrt(2.0f/(9.0f*d)), 3.0f);
        if (mergeThresh > chi2_9999 * 1.5f) {
            Output("WARNING: MergeThresh=%.1f is far above chi2(%d, 0.9999)=%.1f.\n"
                   "  With this threshold nearly every cluster pair is a candidate,\n"
                   "  which causes the MNN chain to collapse all clusters into one\n"
                   "  global component.\n"
                   "  Recommended: MergeThresh=%.1f (chi2(%d, 0.99))\n",
                   mergeThresh, nSpatialDims, chi2_9999, chi2_99, nSpatialDims);
        }
    }
    // ── Phase 5: within-chunk circular xcorr template matching (iterated) ─
    // Loops until no new merges occur or 10 iterations.  After each merge pass
    // the surviving clusters need updated meanWav vectors (the merged cluster
    // mean changes when two sub-clusters are combined), so the Phase 3 harvest
    // re-runs at the top of each iteration before the next xcorr comparison.
    if (TemplateMatchScore > 0.0f && NbChannels > 0 && NbSamplesPerSpike > 0) {
        const int _tmMax = (TemplateMatchIters > 0) ? TemplateMatchIters : 10;
        int _altNetGrowthStreak = 0;

        // Per-iter convergence digest (printed as a table after the loop).
        struct Phase4DigestRow {
            int iter; int merges; int spikesSplit; int kNew;
            int totalClusters; double wallMs;
        };
        std::vector<Phase4DigestRow> _phase4Digest;

        // Reset the Phase 4 median-template cache at phase start (patch
        // 0052).  Bounds cache memory across the run; entries accumulate
        // within this phase's iters and are reused when cluster
        // membership is unchanged.
        m_medianCache.clear();

        auto _countClusters = [&]() {
            std::set<long long> uniq;
            for (int _k = 0; _k < nActive; _k++)
                for (int _c : perChunkClass[_k])
                    if (_c != 0) uniq.insert(
                        static_cast<long long>(_k) * MaxPossibleClusters + _c);
            return static_cast<int>(uniq.size());
        };

        for (int _tmIter = 0; _tmIter < _tmMax; _tmIter++) {
            const auto _iterT0 = std::chrono::steady_clock::now();
            m_phase4Iter = _tmIter;

            // Oscillation-guard bounce tracking (patch 0053).  A cluster
            // hash present pre-split, ABSENT post-split (the split
            // destroyed it), and present again post-merge (the merge
            // exactly reassembled it) is a round-trip bounce -> cooldown.
            std::set<uint64_t> _preSplitHashes, _postSplitHashes;
            bool _didSplitThisIter = false;
            auto _hashesOf = [&](const std::vector<std::vector<int>>& cls) {
                std::set<uint64_t> out;
                for (size_t _ck = 0; _ck < cls.size(); ++_ck) {
                    std::unordered_map<int, std::vector<int>> byLc;
                    const auto& cv = cls[_ck];
                    const auto& pv = chunkPoints[_ck];
                    for (size_t _i = 0; _i < cv.size(); ++_i)
                        if (cv[_i] != 0) byLc[cv[_i]].push_back(pv[_i]);
                    for (auto& _kv : byLc)
                        out.insert(ClusterMembershipHash(_kv.second));
                }
                return out;
            };
            // Phase 4b: optional alternating KnnSplit inside the Phase 4
            // loop — runs BEFORE merge so each iter is split→harvest→merge.
            // The closing iter of the loop is template-merge-only, so its
            // output going to Phase 5 is template-converged (no raw split
            // products escape to downstream phases).
            //
            // Two guards against runaway split-domination:
            //   * iter cap (AlternatingSplitMergeMaxIters, default 2)
            //   * net-growth abort (if WaveKnnSplit produced more new
            //     clusters than template-match merged for two iters in a
            //     row, stop running split; merge-only iters continue up
            //     to TemplateMatchIters).
            int _nSpikesSplit = 0;
            int _kNewThisIter = 0;
            const bool _altGate =
                AlternatingSplitMergeEnable != 0 &&
                KnnSplitPerChunkEnable != 0 && KnnSplitMode == 1 &&
                _tmIter < AlternatingSplitMergeMaxIters &&
                !(AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                  _altNetGrowthStreak >= 2);
            if (_altGate) {
                std::vector<std::vector<int>> _beforeSplit = perChunkClass;
                std::set<int> _kBefore;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kBefore.insert(c);
                if (AlternatingSplitCooldownIters > 0)
                    _preSplitHashes = _hashesOf(perChunkClass);
                if (QualityWeightedSplitEnable) {
                    // Quality-routed: dispatcher computes ISI contamination
                    // + waveform variance for an oversampled pool and routes
                    // each neediest cluster to CEM or knn.  Replaces the
                    // direct WaveKnn+FullCem calls.
                    QualityWeightedSplitDispatch(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                } else {
                    WaveKnnSplitPerChunk(
                        chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    if (FullCemSplitEnable) {
                        FullCemSplitPerChunk(
                            chunkPoints, perChunkClass, perChunkModels, nFullDims);
                    }
                }
                if (AlternatingSplitCooldownIters > 0) {
                    _postSplitHashes = _hashesOf(perChunkClass);
                    _didSplitThisIter = true;
                }
                for (size_t _ckb = 0; _ckb < perChunkClass.size(); ++_ckb) {
                    const auto& aft = perChunkClass[_ckb];
                    const auto& bef = _beforeSplit[_ckb];
                    if (aft.size() != bef.size()) continue;
                    for (size_t _ii = 0; _ii < aft.size(); ++_ii)
                        if (aft[_ii] != bef[_ii]) ++_nSpikesSplit;
                }
                std::set<int> _kAfter;
                for (const auto& v : perChunkClass)
                    for (int c : v) if (c != 0) _kAfter.insert(c);
                _kNewThisIter = static_cast<int>(_kAfter.size())
                              - static_cast<int>(_kBefore.size());
            } else if (AlternatingSplitMergeEnable != 0 &&
                       _tmIter == AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit: iter cap reached "
                    "(%d); continuing with template-match only\n",
                    AlternatingSplitMergeMaxIters);
            } else if (AlternatingSplitMergeEnable != 0 &&
                       AlternatingSplitMergeAbortOnNetGrowth != 0 &&
                       _altNetGrowthStreak >= 2 &&
                       _tmIter < AlternatingSplitMergeMaxIters) {
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit: aborted by "
                    "net-growth streak; continuing with template-match "
                    "only\n");
                _altNetGrowthStreak = 1000;  // suppress further logs
            }

            // Re-harvest meanWav with current perChunkClass
            if ((TemplateMatchScore > 0.0f || CrossChunkTemplateScore > 0.0f) &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                const int wElems = NbChannels * NbSamplesPerSpike;
                char spkPathTM2[STRLEN + 16];
                // Prefer canonical .spk.N; fall back to .spkD.N (stderiv).
                pickInputPath(spkPathTM2, sizeof(spkPathTM2), FileBase, "spk", ElecNo);
                FILE* spkTM2 = fopen(spkPathTM2, "rb");
                if (spkTM2) {
                    for (int k = 0; k < nActive; k++) {
                        const auto& pts2  = chunkPoints[k];
                        const auto& cls2  = perChunkClass[k];
                        const int   nPts2 = static_cast<int>(pts2.size());
                        std::unordered_map<int, ChunkModel*> lcToModel2;
                        for (auto& cm : perChunkModels[k])
                            if (cm.chunkIdx == k)
                                lcToModel2[cm.localClusterId] = &cm;
                        // Zero existing meanWav (and edge waveforms) for live clusters
                        for (auto& [lc2, pcm] : lcToModel2) {
                            pcm->meanWav.assign(static_cast<size_t>(wElems), 0);
                            pcm->meanWavLeft.clear();
                            pcm->meanWavRight.clear();
                        }
                        // Determine this chunk's time range for edge classification
                        float tMin2 = std::numeric_limits<float>::infinity();
                        float tMax2 = -std::numeric_limits<float>::infinity();
                        for (int i = 0; i < nPts2; i++) {
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            if (t < tMin2) tMin2 = t;
                            if (t > tMax2) tMax2 = t;
                        }
                        const float tRange2    = (tMax2 > tMin2) ? (tMax2 - tMin2) : 1.0f;
                        const float tLeftEnd2  = tMin2 + 0.25f * tRange2;
                        const float tRightBeg2 = tMin2 + 0.75f * tRange2;

                        std::unordered_map<int, std::vector<int64_t>> acc2, accLeft2, accRight2;
                        std::unordered_map<int, int> nAcc2, nAccLeft2, nAccRight2;
                        std::vector<int16_t> row2(static_cast<size_t>(wElems));
                        for (int i = 0; i < nPts2; i++) {
                            const int lc2 = cls2[i];
                            if (lc2 == 0 || !lcToModel2.count(lc2)) continue;
                            fseeko(spkTM2,
                                   static_cast<off_t>(pts2[i]) * wElems * sizeof(int16_t),
                                   SEEK_SET);
                            if (fread(row2.data(), sizeof(int16_t), wElems, spkTM2)
                                    != static_cast<size_t>(wElems)) continue;
                            ShiftWaveformRowInPlace(row2.data(), pts2[i],
                                                    NbChannels, NbSamplesPerSpike);
                            auto& a2 = acc2[lc2];
                            if (a2.empty()) a2.assign(static_cast<size_t>(wElems), 0);
                            for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                    a2[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                        += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                            nAcc2[lc2]++;

                            // Edge accumulators
                            const float t = Data[static_cast<size_t>(pts2[i]) * nDims + (nDims - 1)];
                            std::vector<int64_t>* edgeAcc = nullptr;
                            int* edgeN = nullptr;
                            if (t <= tLeftEnd2)       { edgeAcc = &accLeft2[lc2];  edgeN = &nAccLeft2[lc2];  }
                            else if (t >= tRightBeg2) { edgeAcc = &accRight2[lc2]; edgeN = &nAccRight2[lc2]; }
                            if (edgeAcc) {
                                if (edgeAcc->empty()) edgeAcc->assign(static_cast<size_t>(wElems), 0);
                                for (int ch2 = 0; ch2 < NbChannels; ch2++)
                                    for (int s2 = 0; s2 < NbSamplesPerSpike; s2++)
                                        (*edgeAcc)[static_cast<size_t>(ch2 * NbSamplesPerSpike + s2)]
                                            += row2[static_cast<size_t>(s2 * NbChannels + ch2)];
                                (*edgeN)++;
                            }
                        }
                        for (auto& [lc2, a2] : acc2) {
                            int n2b = nAcc2[lc2];
                            if (n2b == 0) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWav.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWav[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accLeft2) {
                            const int n2b = nAccLeft2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavLeft.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavLeft[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                        for (auto& [lc2, a2] : accRight2) {
                            const int n2b = nAccRight2[lc2];
                            if (n2b < 5 || !lcToModel2.count(lc2)) continue;
                            auto* cm2 = lcToModel2[lc2];
                            cm2->meanWavRight.resize(static_cast<size_t>(wElems));
                            for (int e2 = 0; e2 < wElems; e2++)
                                cm2->meanWavRight[static_cast<size_t>(e2)] =
                                    static_cast<int16_t>(a2[static_cast<size_t>(e2)] / n2b);
                        }
                    }
                    fclose(spkTM2);
                }
            }
            LockedStderr( "[Phase 4] Within-chunk xcorr template matching (iter %d)\n",
                    _tmIter + 1);
            int _nMerged = (MedianKnnTemplateMatchEnable != 0)
                ? WithinChunkTemplateMatchMedianKnn(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore)
                : WithinChunkTemplateMatch(
                      chunkPoints, perChunkClass, perChunkModels,
                      NbChannels, NbSamplesPerSpike, TemplateMatchScore);

            // Phase 4b streak update — now that this iter's merge count
            // is known, compare against the iter's split-induced cluster
            // growth.  Updating here (rather than at split time) means
            // the streak reflects the OUTCOME of an alternation round
            // (split + cleanup-merge together), not the split alone.
            // Oscillation-guard bounce detection (patch 0053): a cluster
            // hash that was pre-split, destroyed by the split, and exactly
            // recreated by the merge round-tripped -> cooldown it so the
            // next iters' split budget skips it.
            if (_didSplitThisIter && AlternatingSplitCooldownIters > 0) {
                const std::set<uint64_t> _postMergeHashes =
                    _hashesOf(perChunkClass);
                int _nBounced = 0;
                for (uint64_t _h : _postMergeHashes) {
                    if (_preSplitHashes.count(_h)
                        && !_postSplitHashes.count(_h)) {
                        m_splitCooldown[_h] =
                            _tmIter + AlternatingSplitCooldownIters;
                        ++_nBounced;
                    }
                }
                if (_nBounced > 0) {
                    LockedStderr("[Phase 4b] oscillation guard: %d cluster(s) "
                                 "round-tripped (split->merge no-op); cooled "
                                 "down for %d iter(s)\n",
                                 _nBounced, AlternatingSplitCooldownIters);
                }
            }

            if (_altGate) {
                if (_kNewThisIter > _nMerged) ++_altNetGrowthStreak;
                else                          _altNetGrowthStreak = 0;
                LockedStderr(
                    "[Phase 4b] AlternatingKnnSplit (iter %d): %d spike "
                    "labels changed; +%d new clusters vs %d merged (net "
                    "growth streak %d/2)\n",
                    _tmIter + 1, _nSpikesSplit, _kNewThisIter, _nMerged,
                    _altNetGrowthStreak);
            }


            // Phase 4b heavy realignment: at the END of every Phase 4 iter,
            // realign each per-chunk cluster's spikes (klusters-faithful
            // xcorr against the cluster's mean) and refeaturize only the
            // changed spikes from the cached .fil group channels.  The
            // next iter's meanWav harvest (which reads .spk through
            // m_cumShift) and Phase 4b WaveKnnSplit (which reads Data[])
            // both see the realigned state.
            //
            // Gated on KlustersRealignAfterPhase4; requires
            // KlustersRealignEnable so MaxShift/MinSize have valid values.
            if (KlustersRealignAfterPhase4 != 0 &&
                m_timeShiftReady &&
                NbChannels > 0 && NbSamplesPerSpike > 0) {
                std::vector<int> _changedSpikes;
                _changedSpikes.reserve(1024);
                const int _nChanged = KlustersStyleRealignPerChunkClusters(
                    chunkPoints, perChunkClass,
                    NbChannels, NbSamplesPerSpike,
                    _changedSpikes);
                if (_nChanged > 0) {
                    RefeaturizeChangedSpikes(
                        _changedSpikes, NbChannels, NbSamplesPerSpike);
                }
            }

            const double _iterMs =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - _iterT0).count();
            _phase4Digest.push_back({ _tmIter + 1, _nMerged, _nSpikesSplit,
                                      _kNewThisIter, _countClusters(),
                                      _iterMs });

            if (_nMerged == 0 && _nSpikesSplit == 0) break;
        }
        // ── Phase 4 convergence digest ────────────────────────────────
        LockedStderr("[Phase 4] convergence digest:\n");
        LockedStderr("    iter | merges | splits | +new | clusters | wall_ms\n");
        LockedStderr("    -----+--------+--------+------+----------+--------\n");
        for (const auto& _r : _phase4Digest) {
            LockedStderr("    %4d | %6d | %6d | %4d | %8d | %7.0f\n",
                         _r.iter, _r.merges, _r.spikesSplit, _r.kNew,
                         _r.totalClusters, _r.wallMs);
        }
        LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 4");
    }

        // ── Phase 4c: neighborhood-remix split (patch 0062) ──────────────
        // Runs after Phase 4 converges; pools each random source with its N
        // closest clusters and re-splits, guarded by disjointness + a
        // tightness mask.  No-op unless -Phase4cRemixEnable 1.
        RunPhase4cRemix(chunkPoints, perChunkClass, perChunkModels, nFullDims);

        // ── Phase 8: variance-targeted knn-split (patch 0067) ───────────
        // Iterates Phase 4b's WaveKnn-split on high-variance clusters
        // only.  No-op unless -Phase8VarianceSplitEnable 1.  FullCEM is
        // intentionally skipped — diffuse clusters have no Gaussian-
        // mixture modes for FullCEM to find; WaveKnn redistributes their
        // spikes to nearby reference clusters instead.
        RunPhase8VarianceSplit(chunkPoints, perChunkClass, perChunkModels, nFullDims);

    // Rebuild pointPacked[] using post-template-merge cluster IDs.
    // perChunkAssign was built with original Phase 1 IDs; after
    // WithinChunkTemplateMatch the IDs in perChunkClass differ.
    // packedToGlobal (built after MergeChunkModels) uses post-merge IDs,
    // so pointPacked must use the same scheme.
    std::fill(pointPacked.begin(), pointPacked.end(), -1);
    for (int k = 0; k < nActive; k++) {
        const auto& pts = chunkPoints[k];
        const auto& cls = perChunkClass[k];
        const int nPts  = static_cast<int>(pts.size());
        for (int i = 0; i < nPts; i++) {
            const int p2 = pts[i];
            if (pointPacked[p2] < 0)  // first-write-wins for overlap spikes
                pointPacked[p2] = k * MaxPossibleClusters + cls[i];
        }
    }

    // Rebuild allModels from perChunkModels now that within-chunk template
    // matching has finalised the local cluster set.  Cluster IDs in
    // perChunkClass and in allModels must agree so that MergeChunkModels vote
    // keys (clsK * MaxPossibleClusters + clsK1) resolve correctly.
    allModels.clear();
    for (int k = 0; k < nActive; k++)
        for (auto& cm : perChunkModels[k])
            allModels.push_back(cm);  // copy — perChunkModels still needed below

    // ── Build overlap vote matrix (deferred from Phase 3) ─────────────
    // Now that Phase 4's lcRemap chain has settled and perChunkClass
    // reflects the final per-chunk cluster IDs, build the (clsK, clsK1)
    // vote keys against the CURRENT cluster set.  Phase 5's
    // MergeChunkModels reads these keys directly.
    //
    // (Vote VALUES — shared spike counts per cluster-id pair — are also
    // affected by the move: a pre-Phase-4 vote key {clsA_pre,
    // clsB_pre} with N votes that gets merged in chunk K into
    // {canonA_post, clsB_pre} now correctly aggregates with any other
    // pre-merge keys that landed on canonA_post in the same chunk.
    // No vote double-counting because each shared spike contributes
    // exactly one (clsK, clsK1) increment, regardless of which pre-
    // merge cluster ID it had.)
    if (chunkOverlapFrac > 0.0f) {
        for (int k = 0; k < nActive - 1; k++) {
            const int origK = activeOrigIdx[k];
            if (origK + 1 != activeOrigIdx[k + 1]) continue;
            if (origK >= static_cast<int>(overlapForPair.size())) continue;

            auto& votes = overlapVotes[k];
            for (const auto& oe : overlapForPair[origK]) {
                if (oe.localK  >= static_cast<int>(perChunkClass[k].size()))   continue;
                if (oe.localK1 >= static_cast<int>(perChunkClass[k+1].size())) continue;
                const int clsK  = perChunkClass[k][oe.localK];
                const int clsK1 = perChunkClass[k+1][oe.localK1];
                if (clsK == 0 || clsK1 == 0) continue;  // noise
                votes[clsK * MaxPossibleClusters + clsK1]++;
            }
            int totalVotes = 0;
            for (const auto& [key, cnt] : votes) totalVotes += cnt;
            Output("  [Phase 4→5] Overlap pair orig%d/orig%d (active %d/%d): "
                   "%d shared spikes, %d (clsK,clsK1) pairs\n",
                   origK, origK + 1, k, k + 1, totalVotes,
                   static_cast<int>(votes.size()));
        }
    }

    // ── Post-Phase-5 alignment site ─────────────────────────────────────
    // Within-chunk template merges have consolidated per-chunk clusters.
    // Aligns spikes against the merged means before cross-chunk matching.
    RunAlignmentBlock(TimeShiftAlignAfterPhase4, "Phase 4a");

    LockedStderr( "[Phase 5] Cross-chunk model matching (overlap-vote + edge-xcorr)\n");
    const int nGlobal = MergeChunkModels(allModels, nSpatialDims, mergeThresh, overlapVotes);
    LockedStderr( "[Phase 5] Cross-chunk merge produced %d global clusters\n", nGlobal);
    LogPerChunkClusterState(chunkPoints, perChunkClass, "Phase 5");
    if (nGlobal < 1) {
        Output("Merge produced no real clusters — falling back to CEMTwoPhase.\n");
        return CEMTwoPhase(timeMergeIter);
    }

    // Guard: if nGlobal >= MaxPossibleClusters, globalClusterIds would write
    // out-of-bounds into ClassAlive[].  This is NOT a MergeThresh problem —
    // the merge can succeed perfectly (100% vote-matches) yet produce more
    // global clusters than MaxPossibleClusters when the table is set too small
    // relative to nChunks * MaxClusters.  Fall back to CEMTwoPhase and emit
    // a precise diagnostic so the user knows what to increase.
    if (nGlobal >= MaxPossibleClusters) {
        // Emit on stderr unconditionally — this is a hard fall-back from
        // chunked CEM to single-pass CEMTwoPhase that the user must know
        // about (it dramatically changes the output cluster set and was
        // silent under Output() unless -Screen 1 was set).  Keep the
        // Output() call so logging still captures it.
        fprintf(stderr,
                "[Phase 5] WARNING: MergeChunkModels produced %d global "
                "clusters >= MaxPossibleClusters (%d) — falling back to "
                "CEMTwoPhase.  Fix: re-run with -MaxPossibleClusters %d "
                "or higher (rule of thumb: nChunks * MaxClusters).\n",
                nGlobal, MaxPossibleClusters, nGlobal + 100);
        Output("WARNING: MergeChunkModels produced %d global clusters >= "
               "MaxPossibleClusters (%d).\n"
               "  The cross-chunk merge itself succeeded — this is a table-size\n"
               "  limit, not a MergeThresh problem.  The number of unique\n"
               "  global units exceeds the pre-allocated cluster table.\n"
               "  Fix: set -MaxPossibleClusters to at least %d\n"
               "  (a safe rule: nChunks * MaxClusters; use 300-500 for long\n"
               "  recordings with many chunks).\n"
               "  Falling back to CEMTwoPhase on the full session.\n",
               nGlobal, MaxPossibleClusters,
               nGlobal + 10);
        return CEMTwoPhase(timeMergeIter);
    }

    // Build lookup: packed -> globalClusterId
    std::unordered_map<int,int> packedToGlobal;
    packedToGlobal.reserve(allModels.size());
    for (const auto& cm : allModels)
        packedToGlobal[cm.chunkIdx * MaxPossibleClusters + cm.localClusterId] =
            cm.globalClusterId;

    // ── Post-Phase-6 alignment site ─────────────────────────────────────
    // Runs after cross-chunk model matching: per-chunk clusters have been
    // mapped to global cluster IDs (above) but no Class[] update has happened
    // yet — that occurs inside the Phase 7 init block below.  Alignment here
    // uses the still-per-chunk Class state, which is correct: spikes are
    // aligned to their per-chunk cluster means BEFORE the per-chunk → global
    // remap kicks in.  This catches alignment drift WITHIN per-chunk clusters
    // that Phase 5's template matching may have left behind.
    RunAlignmentBlock(TimeShiftAlignAfterPhase5, "Phase 5a");

    // -------------------------------------------------------------------
    // Phase 7: global warm-start EM (full dimensionality including time)
    //
    // Seed Class[] from the matched global labels, then run a short
    // full-dimensional EM pass.  Because the starting assignment is
    // already well-separated, this typically converges in < 10 iterations.
    // A drifting unit correctly fits a tilted ellipse in (PCA + time) space.
    // -------------------------------------------------------------------
    nDims  = nFullDims;
    nDims2 = nDims * nDims;
    log2piHalf = static_cast<float>(std::log(2.0 * PI) * nDims * 0.5);

    for (int c = 0; c < MaxPossibleClusters; c++) ClassAlive[c] = 0;
    for (int p = 0; p < nPoints; p++) {
        const int pp = pointPacked[p];
        auto it = (pp >= 0) ? packedToGlobal.find(pp) : packedToGlobal.end();
        const int g = (it != packedToGlobal.end()) ? it->second : 0;
        // g is in [0, nGlobal] and nGlobal < MaxPossibleClusters (checked above)
        Class[p] = g;
        ClassAlive[g] = 1;
    }
    Reindex();

    float score;
    if (globalMergeIter <= 0) {
        // GlobalMerge=0: skip Phase 7 entirely.  Emit one MStep/EStep so
        // LogP is valid for ComputeScore(), but do not reassign Class[].
        Output("Phase 7 skipped (GlobalMerge=0) — using Phase 6 assignment directly\n");
        // Force CPU path for Phase 7 post-merge scoring.
        // GPU EStep writes d_LogP in GPU memory; the CPU LogP.m_Data clamp below
        // would be a no-op on the GPU path.  Temporarily null gpu so MStep/EStep/
        // ComputeScore all run on CPU, where LogP.m_Data is authoritative.
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        // Reassign any spikes whose cluster was deleted by MStep (singular covariance)
        // to noise (class 0) so EStep and ComputeScore see valid Class[] values.
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();
        {
            int nNan = 0;
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) { lp = kLargeLogP; nNan++; }
            }
            if (nNan > 0)
                Output("Phase3-skip: clamped %d non-finite LogP entries\n", nNan);
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu);
#endif
        if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
    } else {
        LockedStderr( "[Phase 6] Global warm-start EM\n");
        Output("Phase 7: global warm-start EM — %d clusters, max %d iters\n",
               nClustersAlive, globalMergeIter);
        int   iter = 0, nChanged = 1;
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        void* savedGpu2 = static_cast<void*>(gpu); gpu = nullptr;
#endif
        MStep();
        for (int p2 = 0; p2 < nPoints; p2++)
            if (!ClassAlive[Class[p2]]) Class[p2] = 0;
        ClassAlive[0] = 1;
        Reindex();
        EStep();  // CPU path: populates LogP.m_Data directly
        {
            const float kLargeLogP = 1e15f;
            for (int p2 = 0; p2 < nPoints; p2++) {
                float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                if (!std::isfinite(lp)) lp = kLargeLogP;
            }
        }
        score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
        gpu = static_cast<decltype(gpu)>(savedGpu2);
#endif
        FullStep = 1;
        for (; iter < globalMergeIter; iter++) {
            MStep(); EStep(); nChanged = CStep(); ConsiderDeletion();
            score = ComputeScore();
            if (score < ksv().BestScoreSave) { SaveBestMeans(); ksv().BestScoreSave = score; }
            if (Verbose >= 2)
                Output("  P3 iter %d: %d clusters score %.7g nChanged %d\n",
                       iter, nClustersAlive, score, nChanged);
            FullStep = 1;
            if (nChanged == 0) { Output("Phase 7 converged at iter %d\n", iter); break; }
        }
    }

    // Per-phase quality summary after Phase 7 (Global EM) — gives a
    // checkpoint before DipSplit potentially mutates the cluster set.
    LogGlobalClusterState("Phase 6 (Global EM)");
    ReportClusterQuality("Phase 7");

    // ── Phase 6a (optional): post-merge cluster realignment ────────────────
    // Mirror of Driver A's Phase 7a.  See Driver A's body for rationale
    // (cross-chunk merge consolidates chunk-local clusters into global
    // units; spikes' Phase-1.5 alignments may no longer be optimal vs.
    // the new global means).
    if (TimeShiftAlignPostMerge != 0 && m_timeShiftReady) {
        LockedStderr( "[Phase 6a] Post-merge cluster realignment\n");
        const int nShifted = TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);
        int nCOMShifted = 0;
        if (EnergyCOMRealign != 0) {
            LockedStderr( "[Phase 6a] Energy-COM realignment\n");
            nCOMShifted = EnergyCOMRealignPhase(NbChannels, NbSamplesPerSpike);
        }
        if (nShifted > 0 || nCOMShifted > 0) {
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuR = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuR);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Phase 6a (post-merge realign)"); ReportClusterQuality("Phase 7a");
        }
    }

    // ── Phase 6b (optional): mean-waveform subtraction merge ─────────
    // Mirror of Driver A's Phase 7b.  See FinalMeanSubtractionMerge for
    // algorithm; opt-in via -MeanSubtractionMergeEnable 1 with threshold
    // -MeanSubtractionMergeThresh (default 0.05) and cyclic-shift
    // radius -MeanSubtractionMergeMaxShift (default 3).
    if (MeanSubtractionMergeEnable != 0) {
        LockedStderr( "[Phase 6b] Mean-subtraction template merge "
                        "(threshold D < %.3f)\n",
                static_cast<double>(MeanSubtractionMergeThresh));
        const int nMergedSub =
            FinalMeanSubtractionMerge(NbChannels, NbSamplesPerSpike);
        if (nMergedSub > 0) {
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            void* savedGpuB = static_cast<void*>(gpu); gpu = nullptr;
#endif
            MStep();
            for (int p2 = 0; p2 < nPoints; p2++)
                if (!ClassAlive[Class[p2]]) Class[p2] = 0;
            ClassAlive[0] = 1;
            Reindex();
            EStep();
            {
                const float kLargeLogP = 1e15f;
                for (int p2 = 0; p2 < nPoints; p2++) {
                    float& lp = LogP.m_Data[static_cast<size_t>(Class[p2]) * nPoints + p2];
                    if (!std::isfinite(lp)) lp = kLargeLogP;
                }
            }
            score = ComputeScore();
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
            gpu = static_cast<decltype(gpu)>(savedGpuB);
#endif
            if (score < ksv().BestScoreSave) {
                SaveBestMeans();
                ksv().BestScoreSave = score;
            }
            LogGlobalClusterState("Phase 6b (mean-sub merge)"); ReportClusterQuality("Phase 7b");
        }
    }

    // ── Phase 7c — klusters-faithful per-spike realignment ─────────────
    // Final tightening pass that uses XcorrDispatch (the same library
    // klusters' interactive "Realign top-ch" button uses) to compute a
    // per-spike shift against each cluster's pre-aligned mean template.
    // Updates m_cumShift; TimeShiftFinalize at the very end of the run
    // will read .fil at the new offsets and rewrite .spk/.fet.
    //
    // Opt-in via -KlustersRealignEnable 1.  No-op when the time-shift
    // probe never initialised or no spikes meet the min-cluster-size
    // threshold.  See KK::KlustersStyleRealignAllClusters for details.
    if (KlustersRealignEnable != 0) {
        LockedStderr(
                "[Phase 7c] Klusters-faithful per-spike realignment "
                "(maxShift=%d samples, minSize=%d)\n",
                KlustersRealignMaxShift, KlustersRealignMinSize);
        const int nChanged =
            KlustersStyleRealignAllClusters(NbChannels, NbSamplesPerSpike);
        if (nChanged > 0) {
            // m_cumShift was updated.  We do NOT re-run MStep/EStep here:
            // the clustering result (.clu) is final at this point, and
            // .spk/.fet re-projection happens in TimeShiftFinalize at the
            // very end.  Re-running EStep would require fresh features
            // which we don't have yet (would need RefeaturizeFromShifts
            // first, doubling the .fil-read cost).  Leave the score and
            // BestMeans as-is.
            LogGlobalClusterState("Phase 7c (klusters realign)");
        }
    }

    // Phase 8 DipSplit removed (see Driver A above for rationale).

    // Per-phase quality summary at end of pipeline (after DipSplit).
    // See KK::ReportClusterQuality for metric definitions.
    ReportClusterQuality("final");

    Output("RunChunkedCEM done: %d clusters, score %.7g\n", nClustersAlive, score);
    return score;
}


// ---------------------------------------------------------------------------
// RefeaturizeFromShifts
//
// For every spike whose xcorr shift is non-zero, re-projects the aligned
// waveform through the saved PCA eigenvectors and updates Data[] in-place.
//
// PCA file format (written by process_pca -e):
//   int32  magic = 0x50434145 ("PCAE")
//   int32  version = 1
//   int32  nChannels
//   int32  data2use     (samples per channel used for PCA)
//   int32  nComponents  (PCs kept)
//   int32  recShift     (first sample offset within waveform)
//   int32  isCentered   (1 = subtract mean before projecting)
//   for each channel:
//     double[data2use]             per-channel mean
//     double[data2use*nComponents] eigenvectors (col-major: col=component)
//
// Overlap-resolution invariant: spikeShifts[p] was set by home-chunk
// owned by the shift-probe (m_cumShift[p]), so every spike's shift is
// derived from the chunk where it naturally lives.  Overlap spikes that
// also appeared in a later chunk retain their home-chunk shift here.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// KK::WritePhase15Checkpoint
//
// After RefeaturizeFromShifts (Phase 4), write corrected .spk
// and .fet files using the pending-file pattern from Klusters:
//   1. Write to SESSION.spk.N.pending and SESSION.fet.N.pending
//   2. Rename each .pending file over the original when fully written
//
// This ensures the originals are never partially overwritten; on failure
// the originals remain intact.
//
// .spk: for each shifted spike, re-extracts from .fil at
//       (rawTs + shift - PeakSampleIndex), matching Klusters extTs.
//       Unshifted spikes are copied from the original .spk unchanged.
//
// .fet: for each shifted spike, writes updated PCA features from Data[]
//       (set by RefeaturizeFromShifts) with extTs = rawTs+shift in the
//       last (timestamp) column.  Unshifted spikes are copied unchanged.
//
// .res: NOT modified.  The spike detection timestamp is the sample of the
//       peak in the raw signal — that physical event did not move.  Only
//       the extraction window moved.  Matches Klusters' resTs = original.
// ---------------------------------------------------------------------------
void KK::WritePhase15Checkpoint(const std::vector<int>& spikeShifts,
                                  int nChan, int nSamplesPerSpike)
{
    const int nShifted = static_cast<int>(
        std::count_if(spikeShifts.begin(), spikeShifts.end(),
                      [](int s){ return s != 0 && s != std::numeric_limits<int>::min(); }));
    if (nShifted == 0) {
        Output("WritePhase15Checkpoint: no shifts — files unchanged\n");
        return;
    }
    Output("WritePhase15Checkpoint: updating %d / %d spikes\n", nShifted, nPoints);

    // Read exact int64 timestamps directly from .res (avoids float precision loss:
    // a timestamp of ~1e8 samples stored as float has ±13 samples round-trip error).
    char resPathWPC[STRLEN + 16];
    snprintf(resPathWPC, sizeof(resPathWPC), "%s.res.%d", FileBase, ElecNo);
    FILE* resWPC = fopen(resPathWPC, "rb");
    if (!resWPC)
        Output("WritePhase15Checkpoint: cannot open .res — falling back to float timestamps\n");

    char spkOrig[STRLEN+16], fetOrig[STRLEN+16];
    char spkTmp [STRLEN+32], fetTmp [STRLEN+32];
    // Pick the variant (canonical .spk.N/.fet.N or stderiv .spkD.N/.fetD.N)
    // that actually exists on disk, then derive the .pending names from the
    // picked paths.  On success we rename .pending → original, which must
    // therefore preserve the variant of the file we read.
    pickInputPath(spkOrig, sizeof(spkOrig), FileBase, "spk", ElecNo);
    pickInputPath(fetOrig, sizeof(fetOrig), FileBase, "fet", ElecNo);
    snprintf(spkTmp,  sizeof(spkTmp),  "%s.pending", spkOrig);
    snprintf(fetTmp,  sizeof(fetTmp),  "%s.pending", fetOrig);

    const float sessionSamples = timeRawMax - timeRawMin;
    const int   timeDimIdx     = nDims - 1;
    const int   waveSamples    = nChan * nSamplesPerSpike;

    // ── .spk: open original for read, pending for write ──────────────────
    FILE* spkR = fopen(spkOrig, "rb");
    FILE* spkW = fopen(spkTmp,  "wb");

    // Open .fil for re-extraction of shifted spikes
    char filPath[STRLEN + 8];
    snprintf(filPath, sizeof(filPath), "%s.fil", FileBase);
    FILE* filF = (NbTotalChannels > 0 && !GroupChannelIds.empty())
               ? fopen(filPath, "rb") : nullptr;
    if (!filF)
        Output("WritePhase15Checkpoint: .fil not available — shifted spikes "
               "copied from .spk (circular shift artefact possible)\n");

    if (!spkR || !spkW) {
        if (spkR) fclose(spkR);
        if (spkW) fclose(spkW);
        if (filF) fclose(filF);
        Output("WritePhase15Checkpoint: cannot open .spk files — skipping\n");
        goto skip_spk;
    }
    {
        std::vector<int16_t> spkRow(static_cast<size_t>(waveSamples));
        std::vector<int16_t> filRow;
        if (filF) filRow.resize(static_cast<size_t>(NbTotalChannels));

        for (int p = 0; p < nPoints; p++) {
            // Read original waveform
            if (fread(spkRow.data(), sizeof(int16_t), waveSamples, spkR)
                    != static_cast<size_t>(waveSamples)) {
                Output("WritePhase15Checkpoint: .spk short read at spike %d\n", p);
                break;
            }

            const int sh = (p < static_cast<int>(spikeShifts.size())) ? spikeShifts[p] : 0;
            if (sh != 0 && sh != std::numeric_limits<int>::min()) {
                if (filF) {
                    // Re-extract from .fil at extTs - PeakSampleIndex
                    int64_t rawTsWPC = 0;
                    if (resWPC) {
                        fseeko(resWPC, static_cast<off_t>(p) * sizeof(int64_t), SEEK_SET);
                        { size_t _r = fread(&rawTsWPC, sizeof(int64_t), 1, resWPC); (void)_r; }
                    }
                    if (rawTsWPC == 0 && sessionSamples > 0.0f) {
                        const float normTs = Data.m_Data[p * nDims + timeDimIdx];
                        rawTsWPC = static_cast<int64_t>(normTs * sessionSamples + timeRawMin);
                    }
                    const int64_t off  = rawTsWPC + sh - PeakSampleIndex;
                    bool ok = (off >= 0);
                    if (ok) {
                        fseeko(filF, off * NbTotalChannels * 2, SEEK_SET);
                        for (int s = 0; s < nSamplesPerSpike && ok; s++) {
                            if (fread(filRow.data(), 2, NbTotalChannels, filF)
                                    != static_cast<size_t>(NbTotalChannels)) { ok=false; break; }
                            for (int c = 0; c < nChan; c++)
                                spkRow[s * nChan + c] = filRow[GroupChannelIds[c]];
                        }
                        // For stderiv sessions the original .spkD on disk stores
                        // SDIFF_ALLPAIRS + temporal-diff output — not raw voltages.
                        // Apply the same transform so our .spkD.pending matches
                        // the format ndm_extractspikes_stderiv would have written
                        // at this timestamp.
                        if (ok && m_timeShiftBasis.isStderiv) {
                            ApplySdiffAllpairsTemporalDiff(spkRow.data(),
                                                           nChan,
                                                           nSamplesPerSpike);
                        }
                    }
                    // If .fil read fails, keep original (already in spkRow)
                }
                // If no .fil, spkRow already holds the original; the circular
                // shift applied by the shift-probe is NOT in .spk yet (write-
                // back was suppressed), so we leave it as-is.
            }

            if (fwrite(spkRow.data(), sizeof(int16_t), waveSamples, spkW)
                    != static_cast<size_t>(waveSamples)) {
                Output("WritePhase15Checkpoint: .spk write failed at spike %d\n", p);
                break;
            }
        }
        fclose(spkR); fclose(spkW);
        if (filF) fclose(filF);
    }
    rename(spkTmp, spkOrig);
    Output("  .spk updated\n");
    skip_spk:;

    // ── .fet: open original for read (header + all rows), pending for write ──
    {
        FILE* fetR = fopen(fetOrig, "rb");
        FILE* fetW = fopen(fetTmp,  "wb");
        if (!fetR || !fetW) {
            if (fetR) fclose(fetR);
            if (fetW) fclose(fetW);
            Output("WritePhase15Checkpoint: cannot open .fet files — skipping\n");
            goto skip_fet;
        }

        int32_t nFetDims = 0;
        if (fread(&nFetDims, sizeof(int32_t), 1, fetR) != 1 || nFetDims <= 0) {
            fclose(fetR); fclose(fetW);
            Output("WritePhase15Checkpoint: bad .fet header\n");
            goto skip_fet;
        }
        fwrite(&nFetDims, sizeof(int32_t), 1, fetW);

        const int nd = static_cast<int>(nFetDims);
        std::vector<int64_t> rowBuf(static_cast<size_t>(nd));

        for (int p = 0; p < nPoints; p++) {
            if (fread(rowBuf.data(), sizeof(int64_t), nd, fetR)
                    != static_cast<size_t>(nd)) {
                Output("WritePhase15Checkpoint: .fet short read at spike %d\n", p);
                break;
            }
            const int sh = (p < static_cast<int>(spikeShifts.size())) ? spikeShifts[p] : 0;
            if (sh != 0 && sh != std::numeric_limits<int>::min()) {
                // PCA feature columns from Data[] (already updated by RefeaturizeFromShifts)
                const float* row  = Data.m_Data + p * nDims;
                const int nPCACols = std::min(nDims - 1, nd - 1);
                for (int d = 0; d < nPCACols; d++) {
                    // Denormalise: raw = norm / dimRange + dimMin
                    const float raw = (dimRange_[d] > 0.0f)
                        ? row[d] / dimRange_[d] + dimMin_[d]
                        : dimMin_[d];
                    rowBuf[static_cast<size_t>(d)] = static_cast<int64_t>(std::llroundf(raw));
                }
                // Last column: extTs = rawTs + sh (exact int64)
                if (nd > 1) {
                    int64_t rawTsF = 0;
                    if (resWPC) {
                        fseeko(resWPC, static_cast<off_t>(p) * sizeof(int64_t), SEEK_SET);
                        { size_t _r = fread(&rawTsF, sizeof(int64_t), 1, resWPC); (void)_r; }
                    }
                    if (rawTsF == 0 && sessionSamples > 0.0f) {
                        const float normTs = row[nDims - 1];
                        rawTsF = static_cast<int64_t>(normTs * sessionSamples + timeRawMin);
                    }
                    rowBuf[static_cast<size_t>(nd - 1)] = rawTsF + sh;
                }
            }
            if (fwrite(rowBuf.data(), sizeof(int64_t), nd, fetW)
                    != static_cast<size_t>(nd)) {
                Output("WritePhase15Checkpoint: .fet write failed at spike %d\n", p);
                break;
            }
        }
        fclose(fetR); fclose(fetW);
        rename(fetTmp, fetOrig);
        Output("  .fet updated\n");
    }
    skip_fet:;

    // ── patch85: .res rewrite ────────────────────────────────────────────
    //
    // Bug fix: pre-patch, WritePhase15Checkpoint re-extracted .spk content
    // from .fil at rawTs + sh - PeakSampleIndex and wrote .fet's last
    // column as rawTs + sh — but NEVER rewrote .res.  The on-disk
    // asymmetry left .res holding original detection times while
    // .spk/.fet content was anchored at sh-shifted positions.  Klusters
    // (and any other reader that uses .res[i] as the anchor for spike
    // i's window) saw spike windows offset from each other by exactly
    // the per-spike shift, producing peaks scattered across a
    // 2*m_timeShiftMaxAbs + 1 sample range when cluster members were
    // overlaid.  Mean waveforms smeared.  Cluster sorting itself stayed
    // good because feature-space distances are independent of the
    // absolute time anchor.
    //
    // Fix: write .res.pending with rawTs + sh for shifted spikes,
    // rawTs for the rest, then atomic rename to .res.  Same .pending
    // discipline as .spk and .fet above so a crash mid-write doesn't
    // leave a half-written .res.
    //
    // Skipped (no-op, original .res preserved) when:
    //   - resWPC failed to open earlier (we'd have to fall back to
    //     Data[]'s normalised time-dim, which has ±13 sample float-
    //     precision loss at session-scale timestamps — that would
    //     CREATE dispersion rather than fix it).
    //   - the .res.pending file can't be created.
    //   - the rewrite loop aborts mid-write (partial .pending removed).
    if (resWPC) {
        char resOrigP85[STRLEN + 16];
        char resTmpP85 [STRLEN + 32];
        snprintf(resOrigP85, sizeof(resOrigP85), "%s.res.%d", FileBase, ElecNo);
        snprintf(resTmpP85,  sizeof(resTmpP85),  "%s.pending", resOrigP85);

        FILE* resWriteP85 = fopen(resTmpP85, "wb");
        if (!resWriteP85) {
            Output("WritePhase15Checkpoint: cannot create %s — .res preserved\n",
                   resTmpP85);
        } else {
            fseeko(resWPC, 0, SEEK_SET);
            int nResWritten = 0;
            bool resOk = true;
            int  nResShifted = 0;
            for (int p = 0; p < nPoints && resOk; ++p) {
                int64_t rawTsP85 = 0;
                if (fread(&rawTsP85, sizeof(int64_t), 1, resWPC) != 1) {
                    Output("WritePhase15Checkpoint: .res short read at "
                           "spike %d — aborting .res rewrite\n", p);
                    resOk = false;
                    break;
                }
                const int shP85 = (p < static_cast<int>(spikeShifts.size()))
                                ? spikeShifts[p] : 0;
                const bool shiftValid = (shP85 != 0 &&
                                         shP85 != std::numeric_limits<int>::min());
                const int64_t outTs = shiftValid
                                    ? (rawTsP85 + static_cast<int64_t>(shP85))
                                    : rawTsP85;
                if (fwrite(&outTs, sizeof(int64_t), 1, resWriteP85) != 1) {
                    Output("WritePhase15Checkpoint: .res write failed at "
                           "spike %d — aborting .res rewrite\n", p);
                    resOk = false;
                    break;
                }
                ++nResWritten;
                if (shiftValid) ++nResShifted;
            }
            fclose(resWriteP85);

            if (resOk && nResWritten == nPoints) {
                // Atomic rename — same discipline as .spk / .fet above.
                if (rename(resTmpP85, resOrigP85) == 0) {
                    Output("  .res updated (%d spikes, %d shifted)\n",
                           nResWritten, nResShifted);
                } else {
                    Output("WritePhase15Checkpoint: rename %s -> %s failed "
                           "— .res preserved\n", resTmpP85, resOrigP85);
                    ::remove(resTmpP85);
                }
            } else {
                // Partial write — leave original .res intact.
                ::remove(resTmpP85);
                Output("  .res NOT updated (wrote %d of %d) — "
                       "original preserved\n", nResWritten, nPoints);
            }
        }
    } else {
        Output("WritePhase15Checkpoint: .res not opened for read — "
               "cannot rewrite (would lose precision via Data[] fallback)\n");
    }

    if (resWPC) fclose(resWPC);
    Output("WritePhase15Checkpoint: done\n");
}



// ---------------------------------------------------------------------------
// KK::PerClusterCEMPerChunk
//
// Phase 2a: per-cluster ordinary CEM in the FULL feature space.  For each
// cluster in each chunk, builds a thread-local scratch KK populated with
// only that cluster's spikes, warm-starts from a single Gaussian (all
// spikes assigned to cluster 1, cluster 0 = noise but unused), and runs
// RunEMLoop with splits enabled.  TrySplits inside the loop probes for
// bimodal substructure using the bimodality-coefficient feature selection
// from P1.B; the K3 outer recheck (P1.A) settles each candidate split for
// 3 EM iterations before scoring.
//
// Replaces the former subspace-projected SubspaceReclusterPerChunk.  The
// projection-then-CEM design had a structural failure mode: an elongated
// unimodal Gaussian, when projected into its own top-variance eigenvectors,
// looks bimodal-along-the-long-axis to a 2-Gaussian fit, producing high
// false-positive split rates (~67% acceptance was observed on octrode data).
// Running ordinary CEM in the full feature space avoids the projection
// bias entirely and lets the standard BIC-gated split machinery decide.
//
// Cluster ID assignment:
//   - sub-cluster 0 (noise): NoisePoint=0 keeps it empty (user spec: no real
//     noise from extraction)
//   - sub-cluster 1: keeps the parent's local ID
//   - sub-clusters 2,3,...: get fresh chunk-local IDs starting at maxLc+1
//
// ChunkModel rebuild is deferred to ChunkReCEMPerChunk, which re-runs a
// chunk-level CEM that lets fine-grained sub-clusters reorganise across
// the chunk's full data (boundary spikes can reassign, oversplit pieces
// can merge via ConsiderDeletion).  Building stale ChunkModels here only
// to discard them in 2b would be wasted work.
// ---------------------------------------------------------------------------
void KK::PerClusterCEMPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& /*perChunkModels*/,
    int nFullDims)
{
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    // Minimum cluster size for full-dim covariance.  Below this, the
    // per-cluster CEM would build a singular covariance and degenerate.
    // nFullDims+5 (a few spikes' margin above the rank requirement) is a
    // sane floor; raise to 25 for stability with small clusters.
    const int minClusterSize = std::max(nFullDims + 5, 25);

    // ── Load-balancing knobs ─────────────────────────────────────────
    // Phase 2a is bottlenecked by the largest 2–3 clusters per chunk,
    // each of which serializes a single thread for minutes while 100+
    // other threads sit idle.  Mitigation: when a cluster exceeds
    // kLargeThreshold spikes, split its members randomly into batches
    // of ~kTargetBatch spikes each and dispatch each batch as a
    // separate work item.  This trades a small amount of redundant work
    // (each batch re-discovers any common sub-modes) for genuine
    // parallelism — Phase 2b's chunk re-CEM with ConsiderDeletion
    // re-merges any over-splits across batches.
    //
    // After items are built we LPT-sort (longest processing time first)
    // so the giant items start while the worker pool is full; small
    // ones backfill as threads free up.
    const int kPhase2aLargeThreshold = 800;   // subdivide if size > this
    const int kPhase2aTargetBatch    = 400;   // target spikes per batch

    // ── Phase A: build (chunk, cluster) work items ───────────────────
    struct WorkItem {
        int ck;
        int lc;                       // cluster's local id within the chunk
                                      // (-1 marks "subdivided" — sub-labels
                                      // all get fresh chunk-local IDs at
                                      // write-back; original cluster's ID
                                      // is replaced by the union of all
                                      // batches' sub-clusters)
        std::vector<int> members;     // indices into chunkPoints[ck]
    };
    std::vector<WorkItem> items;

    for (int ck = 0; ck < nCh; ck++) {
        const auto& cls = perChunkClass[ck];
        const auto& pts = chunkPoints[ck];
        if (pts.empty()) continue;
        if (cls.size() != pts.size()) continue;

        // Find unique non-noise local cluster IDs in this chunk.
        std::unordered_set<int> uniqueLcs;
        for (int c : cls) if (c >= 1) uniqueLcs.insert(c);

        for (int lc : uniqueLcs) {
            std::vector<int> mem;
            for (int i = 0; i < static_cast<int>(pts.size()); i++)
                if (cls[static_cast<size_t>(i)] == lc) mem.push_back(i);
            if (static_cast<int>(mem.size()) < minClusterSize) continue;

            if (static_cast<int>(mem.size()) > kPhase2aLargeThreshold) {
                // Subdivide into ceil(N / kPhase2aTargetBatch) random
                // batches of approximately equal size.  Use a per-cluster
                // RNG seed (RandomSeed XOR'd with chunk/cluster keys) so
                // shuffles are reproducible.
                std::mt19937 rng(static_cast<unsigned>(RandomSeed)
                               ^ static_cast<unsigned>(ck * 7919 + lc * 31));
                std::shuffle(mem.begin(), mem.end(), rng);

                const int N      = static_cast<int>(mem.size());
                const int nBatch = (N + kPhase2aTargetBatch - 1)
                                 / kPhase2aTargetBatch;
                const int base   = N / nBatch;
                const int extra  = N - base * nBatch;

                int offset = 0;
                for (int b = 0; b < nBatch; b++) {
                    const int sz = base + (b < extra ? 1 : 0);
                    if (sz < minClusterSize) {
                        // Edge case: tiny final batch — fold remainder
                        // into the previous batch instead of dropping.
                        if (!items.empty() && items.back().lc == -1
                            && items.back().ck == ck) {
                            auto& prev = items.back().members;
                            prev.insert(prev.end(),
                                        mem.begin() + offset,
                                        mem.begin() + offset + sz);
                        }
                        offset += sz;
                        continue;
                    }
                    std::vector<int> sub(mem.begin() + offset,
                                         mem.begin() + offset + sz);
                    offset += sz;
                    items.push_back({ck, /*lc=*/-1, std::move(sub)});
                }
            } else {
                items.push_back({ck, lc, std::move(mem)});
            }
        }
    }

    // LPT scheduling: largest items first.  With schedule(dynamic) this
    // ensures the long-running giant clusters (or their batches) start
    // while the thread pool is full, and small items backfill at the
    // tail.  Avoids the wall-time "stragglers" pattern where a few
    // late-starting giants leave 100+ threads idle for minutes.
    std::sort(items.begin(), items.end(),
              [](const WorkItem& a, const WorkItem& b) {
                  return a.members.size() > b.members.size();
              });

    // Quick stats for diagnostic logging.
    int nSubdivided = 0;
    size_t maxSize = 0;
    for (const auto& it : items) {
        if (it.lc < 0) nSubdivided++;
        if (it.members.size() > maxSize) maxSize = it.members.size();
    }

    LockedStderr(
            "[Phase 2a] Per-cluster CEM: probing %d items (min size %d, "
            "max %zu, %d subdivided batches)\n",
            static_cast<int>(items.size()), minClusterSize,
            maxSize, nSubdivided);

    if (items.empty()) return;

    // ── Phase B: parallel per-cluster CEM ────────────────────────────
    struct ItemResult {
        bool changed = false;
        int  nSubClusters = 0;
        std::vector<int> newSubLabels;  // per-member sub-cluster id from CEM
    };
    std::vector<ItemResult> results(items.size());

    int totalSplits        = 0;
    int totalNewSubClusters = 0;

    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalSplits,totalNewSubClusters)
    for (int wi = 0; wi < static_cast<int>(items.size()); wi++) {
        const auto& item = items[static_cast<size_t>(wi)];
        const int   nMem = static_cast<int>(item.members.size());
        const auto& pts  = chunkPoints[item.ck];

        // Build sub-KK with only this cluster's spikes.
        //
        // SPATIAL-ONLY: time (last dim) is excluded.  Reasoning: this is
        // the same recluster that Klusters performs when the user "splits"
        // a cluster — Klusters spawns KiloKlustaKwik on the cluster's
        // spikes, which runs CEMTwoPhase whose Phase 1 is spatial-only
        // (excludes time).  TrySplits on full-dim data was rejecting
        // bimodal pairs whose split improvement was below the BIC penalty
        // because the time dim's parameter cost was being counted in the
        // penalty without contributing meaningfully to the spatial
        // discrimination (per-cluster time variance ≈ chunk duration is
        // large, so time's Mahalanobis contribution is weak).  Stripping
        // time at copy-in time gives us correct stride throughout (no
        // reliance on CEMTwoPhase's nDims-mutation trick) and matches
        // the standalone Klusters recluster path.
        const int nSpatialDimsFull = (nFullDims > 1) ? nFullDims - 1 : nFullDims;

        // ── Per-cluster feature selection ─────────────────────────
        // When SubspaceDims > 0 and < nSpatialDimsFull, rank the
        // spatial features by within-cluster variance over this item's
        // members and pick the top SubspaceDims.  CEM then runs in a
        // K-dim subspace.
        //
        // Why: with K=21 spatial dims and ~1000 spikes per cluster,
        // each Gaussian costs 21+231=252 BIC parameters → log(N)·252/2
        // ≈ 880 BIC units per cluster, which rejects every marginal
        // split.  At K=6, the cost drops to 6+21=27 params → ~95 BIC
        // units, comparable to what classic KlustaKwik with
        // -UseFeatures (auto-4) operates on.  Marginal real bimodal
        // pairs that get rejected at 21 dims pass cleanly at 6.
        //
        // Selection is per-item (each cluster picks its own best
        // features) — a cluster contaminated mainly along ch3-PC2
        // and ch5-PC1 will have those features ranked high; another
        // cluster with different sub-structure picks different ones.
        // Each picked feature stays interpretable (it's still
        // "PC_i of channel_j" in the original .fetD layout).
        int  nSubDims;                          // active dim count for CEM
        std::vector<int> selFeat;               // indices into spatial dims [0..nSpatialDimsFull)

        if (SubspaceDims > 0 && SubspaceDims < nSpatialDimsFull) {
            // Compute per-feature mean and variance on this cluster's spikes.
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
                const double m  = sum[d] * invN;
                const double v  = std::max(0.0, sqsum[d] * invN - m * m);
                rank[d] = {v, d};
            }
            std::sort(rank.begin(), rank.end(),
                      [](const auto& a, const auto& b){ return a.first > b.first; });

            nSubDims = SubspaceDims;
            selFeat.resize(static_cast<size_t>(nSubDims));
            for (int k = 0; k < nSubDims; k++)
                selFeat[static_cast<size_t>(k)] = rank[static_cast<size_t>(k)].second;
            // Sort selected indices ascending so the packed columns
            // remain in original feature order — purely cosmetic, but
            // makes any debug printout interpretable.
            std::sort(selFeat.begin(), selFeat.end());
        } else {
            // No subspace: use all spatial features in original order.
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

        // Pack data using selected features only.  Stride = nSubDims.
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

        // Init mode depends on whether this item was subdivided:
        //
        //  • Non-subdivided (item.lc ≥ 0, original small cluster): warm
        //    start at K=2 with all spikes in cluster 1.  Let TrySplits
        //    drive K growth for full bimodality discovery.
        //
        //  • Subdivided (item.lc == -1, batch from a large cluster):
        //    flat-K random init at K = ceil(N / kInitSize).  Disable
        //    TrySplits and cap MaxIter so each batch finishes in
        //    bounded time.  Phase 2b's chunk re-CEM (with TrySplits +
        //    ConsiderDeletion at the chunk level) is responsible for
        //    discovering structure across the union of all batches.
        //    Without this cap, a noisy batch can land in a TrySplits-
        //    ConsiderDeletion oscillation and serialize a single
        //    thread for many minutes — the original straggler problem
        //    just relocated to one batch instead of one cluster.
        bool   enableSplits = true;
        int    capMaxIter   = 0;        // 0 = no override
        if (item.lc < 0) {
            // Subdivided batch.
            const int kInitSize = 100;  // target spikes per starting cluster
            const int nReal     = std::max(2, (nMem + kInitSize - 1) / kInitSize);
            Ks.nStartingClusters = nReal + 1;
            for (int c = 0; c < MaxPossibleClusters; c++)
                Ks.ClassAlive[c] = (c <= nReal) ? 1 : 0;
            for (int i = 0; i < nMem; i++)
                Ks.Class[i] = irand(1, nReal);
            enableSplits = false;
            capMaxIter   = 50;          // hard cap; flat-K converges fast
        } else {
            // Non-subdivided cluster: warm-start at K=2.
            Ks.nStartingClusters = 2;
            for (int c = 0; c < MaxPossibleClusters; c++)
                Ks.ClassAlive[c] = (c < 2) ? 1 : 0;
            for (int i = 0; i < nMem; i++) Ks.Class[i] = 1;
        }
        Ks.Reindex();

        Ks.MStep();
        Ks.EStep();

        Ks.RunEMLoop(/*enableSplits=*/   enableSplits,
                     /*enableDistDump=*/ false,
                     /*maxIter=*/        capMaxIter,
                     /*phaseLabel=*/     (item.lc < 0) ? "[2a-batch]" : "[2a]");

        // nClustersAlive includes noise (0).  No-split: == 2 (noise + 1).
        // Real split: > 2.
        if (Ks.nClustersAlive <= 2) continue;

        results[static_cast<size_t>(wi)].changed       = true;
        results[static_cast<size_t>(wi)].nSubClusters  = Ks.nClustersAlive - 1;
        results[static_cast<size_t>(wi)].newSubLabels.resize(static_cast<size_t>(nMem));
        for (int i = 0; i < nMem; i++)
            results[static_cast<size_t>(wi)].newSubLabels[static_cast<size_t>(i)] = Ks.Class[i];

        totalSplits         += 1;
        totalNewSubClusters += (Ks.nClustersAlive - 2);  // beyond the original 1
    }

    // ── Phase C: serial application — assign fresh local IDs ─────────
    std::vector<int> nextLc(nCh, 1);
    for (int ck = 0; ck < nCh; ck++) {
        int maxLc = 0;
        for (int c : perChunkClass[static_cast<size_t>(ck)])
            if (c > maxLc) maxLc = c;
        nextLc[static_cast<size_t>(ck)] = maxLc + 1;
    }

    std::unordered_set<int> chunksAffected;
    for (int wi = 0; wi < static_cast<int>(items.size()); wi++) {
        const auto& res = results[static_cast<size_t>(wi)];
        if (!res.changed) continue;
        const auto& item = items[static_cast<size_t>(wi)];
        auto& cls = perChunkClass[static_cast<size_t>(item.ck)];

        // Map sub-cluster ID to chunk-local ID:
        //   0 -> 0 (noise; should be empty under NoisePoint=0)
        //   For non-subdivided items (item.lc >= 0):
        //     1 -> item.lc (parent retains its ID)
        //     ≥2 -> fresh chunk-local ID
        //   For subdivided items (item.lc == -1):
        //     ALL sub-labels (including 1) -> fresh chunk-local IDs.
        //     The original cluster's ID is replaced entirely by the
        //     union of all batches' sub-clusters; Phase 2b's chunk
        //     re-CEM is responsible for re-merging coherent ones.
        std::unordered_map<int,int> subToLc;
        subToLc[0] = 0;
        if (item.lc >= 0) subToLc[1] = item.lc;

        const int nMem = static_cast<int>(item.members.size());
        for (int i = 0; i < nMem; i++) {
            const int sc = res.newSubLabels[static_cast<size_t>(i)];
            auto it = subToLc.find(sc);
            if (it == subToLc.end()) {
                subToLc[sc] = nextLc[static_cast<size_t>(item.ck)]++;
                it = subToLc.find(sc);
            }
            cls[static_cast<size_t>(item.members[static_cast<size_t>(i)])] = it->second;
        }
        chunksAffected.insert(item.ck);
    }

    LockedStderr(
            "[Phase 2a] Per-cluster CEM: %d clusters split, +%d new sub-clusters, "
            "%d chunks affected\n",
            totalSplits, totalNewSubClusters,
            static_cast<int>(chunksAffected.size()));
}



// ─────────────────────────────────────────────────────────────────────────
// KK::RunPhase8VarianceSplit — Phase 8 variance-targeted knn-split (patch 0067)
// ─────────────────────────────────────────────────────────────────────────
// Iterates Phase 4b's WaveKnn-split machinery on the high-variance clusters
// only.  Per iteration: for each chunk, compute ρ per cluster; clusters with
// ρ ≥ Phase8VarianceThreshold (and size ≥ minSize) form the WaveKnn-split
// allowlist for this iteration.  Then merge + count changes + quit when
// stable (or maxIters reached).
//
// Design choice: FullCEM is intentionally NOT called here.  The target
// scenario is diffuse clusters with no obvious Gaussian-mixture modes — the
// kind that show up as a smear in feature space without internal structure.
// FullCEM would either no-op (no modes for the mixture to find) or invent
// spurious modes; both are wasteful.  WaveKnn-split is the right tool: it
// redistributes the source cluster's spikes to nearby reference clusters
// by k-NN voting, with anything that doesn't vote strongly staying as a
// smaller residual.  Iterated, the diffuse cluster shrinks and concentrates
// (or fully dissolves into its neighbors).
//
// Hooks in after Phase 4c at both CEM call sites; no-op unless
// -Phase8VarianceSplitEnable 1.  Requires m_timeShiftReady (needs to read
// waveforms for the tightness measurement); without it the phase is
// skipped with a warning.
//
// CLI: -Phase8VarianceSplitEnable        0   (master switch)
//      -Phase8VarianceSplitMaxIters      3
//      -Phase8VarianceThreshold          0.10  (ρ threshold for eligibility)
//      -Phase8VarianceSignalChannelFraction 0.1 (τ; same as Phase 4c)
//      -Phase8VarianceMinClusterSize     0   (0 = max(nFullDims+5, 25))
void KK::RunPhase8VarianceSplit(
    const std::vector<std::vector<int>>&  chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    if (Phase8VarianceSplitEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    const int nChan  = NbChannels;
    const int nSamp  = NbSamplesPerSpike;
    const int wElems = nChan * nSamp;
    if (nChan <= 0 || nSamp <= 0) {
        LockedStderr("[Phase 8] skipped: missing spike geometry\n");
        return;
    }
    if (!m_timeShiftReady) {
        LockedStderr("[Phase 8] skipped: time-shift machinery not ready "
                     "(need MaxTimeShift > 0 for spike-read access)\n");
        return;
    }

    const int    maxIters = std::max(1, Phase8VarianceSplitMaxIters);
    const double thr      = std::max(0.0, static_cast<double>(Phase8VarianceThreshold));
    const double tau      = static_cast<double>(Phase8VarianceSignalChannelFraction);
    const int    minSize  = (Phase8VarianceMinClusterSize > 0)
                          ? Phase8VarianceMinClusterSize
                          : std::max(nFullDims + 5, 25);

    LockedStderr("[Phase 8] Variance-targeted knn-split: up to %d iters, "
                 "ρ_thresh=%.3f, min size=%d (FullCEM skipped — diffuse "
                 "clusters have no modes)\n",
                 maxIters, thr, minSize);

    int grandRelabeled       = 0;
    int grandSourcesProcessed = 0;

    for (int it = 0; it < maxIters; ++it) {
        // Per-chunk allowlists, written from disjoint slots under the
        // parallel for; gathered into the std::map after the parallel
        // section (std::map insert is not thread-safe).
        std::vector<std::vector<int>> highVarPerChunk(static_cast<size_t>(nCh));
        int totalHighVar = 0;

        #pragma omp parallel for schedule(dynamic) reduction(+:totalHighVar)
        for (int ck = 0; ck < nCh; ++ck) {
            const auto& pts  = chunkPoints[static_cast<size_t>(ck)];
            const auto& cls  = perChunkClass[static_cast<size_t>(ck)];
            const auto& mdls = perChunkModels[static_cast<size_t>(ck)];
            const int   nPts = static_cast<int>(pts.size());
            if (nPts == 0 || mdls.empty()) continue;

            std::vector<int16_t> rbuf(static_cast<size_t>(wElems));

            // Members per cluster.
            std::unordered_map<int, std::vector<int>> memLocal;
            for (int i = 0; i < nPts; ++i) {
                const int c = cls[static_cast<size_t>(i)];
                if (c != 0) memLocal[c].push_back(i);
            }

            for (const auto& kv : memLocal) {
                const int lc = kv.first;
                const auto& idx = kv.second;
                if (static_cast<int>(idx.size()) < minSize) continue;
                std::vector<int> gids;
                gids.reserve(idx.size());
                for (int li : idx) gids.push_back(pts[static_cast<size_t>(li)]);
                int nSig = 0;
                const double rho = computeClusterTightnessRho(
                    gids, rbuf, nChan, nSamp, tau, nSig);
                if (std::isfinite(rho) && rho >= thr) {
                    highVarPerChunk[static_cast<size_t>(ck)].push_back(lc);
                    ++totalHighVar;
                }
            }
        }

        if (totalHighVar == 0) {
            LockedStderr("[Phase 8] iter %d: no clusters above ρ_thresh; "
                         "converged\n", it + 1);
            break;
        }

        // Gather scratch → final allowlist map.
        std::map<int, std::vector<int>> knnAllow;
        for (int ck = 0; ck < nCh; ++ck) {
            if (!highVarPerChunk[static_cast<size_t>(ck)].empty())
                knnAllow[ck] = std::move(highVarPerChunk[static_cast<size_t>(ck)]);
        }

        // Snapshot perChunkClass to count spikes redistributed.
        std::vector<std::vector<int>> before = perChunkClass;

        // ── knn-split only (FullCEM intentionally skipped) ───────────────
        WaveKnnSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                             nFullDims, &knnAllow);

        // Merge cleanup — same Phase 4 within-chunk template merge.
        const int merged = MedianKnnTemplateMatchEnable
            ? WithinChunkTemplateMatchMedianKnn(chunkPoints, perChunkClass,
                                                perChunkModels, nChan, nSamp,
                                                TemplateMatchScore)
            : WithinChunkTemplateMatch(chunkPoints, perChunkClass,
                                       perChunkModels, nChan, nSamp,
                                       TemplateMatchScore);

        int changedThisIter = 0;
        for (size_t k = 0; k < perChunkClass.size(); ++k) {
            const auto& a = perChunkClass[k];
            const auto& b = before[k];
            if (a.size() != b.size()) continue;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) ++changedThisIter;
        }
        grandRelabeled        += changedThisIter;
        grandSourcesProcessed += totalHighVar;

        LockedStderr("[Phase 8] iter %d/%d: %d high-variance sources, "
                     "%d spikes redistributed, %d merge(s)\n",
                     it + 1, maxIters, totalHighVar,
                     changedThisIter, merged);

        if (changedThisIter == 0 && merged == 0) {
            LockedStderr("[Phase 8] iter %d: no further changes; converged\n",
                         it + 1);
            break;
        }
    }

    LockedStderr("[Phase 8] done: %d source-iterations, %d spikes redistributed\n",
                 grandSourcesProcessed, grandRelabeled);
}



// ─────────────────────────────────────────────────────────────────────────
// KK::RunPhase4cRemix — Phase 4c neighborhood-remix split (patch 0062)
// ─────────────────────────────────────────────────────────────────────────
// Runs once after the Phase 4 loop converges, gated by -Phase4cRemixEnable.
// Structurally a small split→harvest→merge loop like Phase 4b, but the source
// assembly is different: random source clusters are each POOLED with their N
// closest clusters (transient merge), and the splitter re-partitions the
// pooled super-cluster.  Intent: de-fragmentation — give the splitter a second
// look at a cluster together with its nearest neighbours so over-split pieces
// get redrawn or reabsorbed.  knn-split and FullCEM get separate source counts.
//
// Guards:
//   * Disjointness — across the selected work items, if any two pools share a
//     cluster (overlapping spikes) a whole source is dropped at random until
//     all surviving pools are pairwise spike-disjoint, so the parallel
//     dispatch is race-free.
//   * Tightness mask — clusters whose post-alignment residual dispersion is
//     low relative to their own signal power (a scale-free inverse-SNR², so a
//     single threshold is comparable across high-amplitude/concentrated and
//     low-amplitude/diffuse units) are excluded entirely: never a source,
//     never pooled into a neighbourhood.  Protects already-clean units.
//
// CLI: -Phase4cRemixEnable -Phase4cMaxIters -Phase4cKnnSources
//      -Phase4cFullCemSources -Phase4cNeighbors -Phase4cMinClusterSize
//      -Phase4cMaskTightClusters -Phase4cTightnessThreshold
//      -Phase4cSignalChannelFraction -Phase4cTightnessSpreadBeta
void KK::RunPhase4cRemix(
    const std::vector<std::vector<int>>&  chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    if (Phase4cRemixEnable == 0) return;
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    const int nChan     = NbChannels;
    const int nSamp     = NbSamplesPerSpike;
    const int wElems    = nChan * nSamp;
    if (nChan <= 0 || nSamp <= 0) {
        LockedStderr("[Phase 4c] skipped: missing spike geometry\n");
        return;
    }
    const int minSize = (Phase4cMinClusterSize > 0)
                      ? Phase4cMinClusterSize
                      : std::max(nFullDims + 5, 25);
    const int nNeigh  = std::max(0, Phase4cNeighbors);
    const int maxIters = std::max(1, Phase4cMaxIters);
    const bool doMask = (Phase4cMaskTightClusters != 0) && m_timeShiftReady;
    const double tau  = std::max(0.0, static_cast<double>(Phase4cSignalChannelFraction));

    LockedStderr("[Phase 4c] Neighborhood-remix split: up to %d iters, "
                 "sources knn=%d cem=%d, N=%d neighbours%s\n",
                 maxIters, Phase4cKnnSources, Phase4cFullCemSources, nNeigh,
                 doMask ? ", tight-cluster mask ON" : "");

    // Residual dispersion ρ = V_res / P_sig on the signal-support channels for
    // one cluster's global spike ids.  Scale-free (≈1/SNR²): amplitude cancels
    // (both terms ∝ k²) and per-element averaging on the signal support makes
    // concentrated and diffuse footprints comparable.  Returns +inf if it
    // cannot be measured (so such a cluster is never masked).
    auto clusterTightness = [&](const std::vector<int>& gids,
                                std::vector<int16_t>& rbufLocal)
        -> std::pair<double,int>
    {
        const int N = static_cast<int>(gids.size());
        if (N < 2) return { std::numeric_limits<double>::infinity(), 1 };
        std::vector<double> mean(static_cast<size_t>(wElems), 0.0);
        int ok = 0;
        std::vector<std::vector<int16_t>> cache;
        cache.reserve(static_cast<size_t>(N));
        for (int i = 0; i < N; ++i) {
            if (!TimeShiftReadSpikeWave(gids[static_cast<size_t>(i)],
                                        wElems, rbufLocal.data())) continue;
            cache.push_back(rbufLocal);
            for (int e = 0; e < wElems; ++e)
                mean[static_cast<size_t>(e)] +=
                    static_cast<double>(rbufLocal[static_cast<size_t>(e)]);
            ++ok;
        }
        if (ok < 2) return { std::numeric_limits<double>::infinity(), 1 };
        const double invOk = 1.0 / ok;
        for (int e = 0; e < wElems; ++e) mean[static_cast<size_t>(e)] *= invOk;

        // Per-channel energy → signal support (channel-major: ch*nSamp+samp).
        double maxE = 0.0;
        std::vector<double> Ech(static_cast<size_t>(nChan), 0.0);
        for (int ch = 0; ch < nChan; ++ch) {
            double e2 = 0.0;
            for (int s = 0; s < nSamp; ++s) {
                const double m = mean[static_cast<size_t>(ch) * nSamp + s];
                e2 += m * m;
            }
            Ech[static_cast<size_t>(ch)] = e2;
            if (e2 > maxE) maxE = e2;
        }
        if (maxE <= 0.0) return { std::numeric_limits<double>::infinity(), 1 };

        double Psig = 0.0, Vres = 0.0;
        long   nSig = 0;
        long long nResElem = 0;
        for (int ch = 0; ch < nChan; ++ch) {
            if (Ech[static_cast<size_t>(ch)] < tau * maxE) continue;
            ++nSig;
            for (int s = 0; s < nSamp; ++s) {
                const int e = ch * nSamp + s;
                const double m = mean[static_cast<size_t>(e)];
                Psig += m * m;
            }
        }
        if (nSig == 0) return { std::numeric_limits<double>::infinity(), 1 };
        const long sigElems = nSig * nSamp;
        Psig /= static_cast<double>(sigElems);

        for (const auto& w : cache)
            for (int ch = 0; ch < nChan; ++ch) {
                if (Ech[static_cast<size_t>(ch)] < tau * maxE) continue;
                for (int s = 0; s < nSamp; ++s) {
                    const int e = ch * nSamp + s;
                    const double d = static_cast<double>(w[static_cast<size_t>(e)])
                                   - mean[static_cast<size_t>(e)];
                    Vres += d * d;
                    ++nResElem;
                }
            }
        if (nResElem == 0 || Psig <= 0.0)
            return { std::numeric_limits<double>::infinity(), 1 };
        Vres /= static_cast<double>(nResElem);
        // Effective threshold scaling lives at the call site (uses nSig).
        return { Vres / Psig, static_cast<int>(nSig) };
    };

    int grandChanged = 0;
    for (int it = 0; it < maxIters; ++it) {
        int changedThisIter = 0;

        // Per-route allowlists, assembled per-chunk under the parallel for
        // (disjoint slots — no concurrent map insert).  Gathered into the
        // final std::map serially after the loop.
        std::vector<std::vector<int>> knnForChunk(static_cast<size_t>(nCh));
        std::vector<std::vector<int>> cemForChunk(static_cast<size_t>(nCh));

        // ── Per-chunk parallelism (patch 0064) ──────────────────────────
        // Each iteration mutates only perChunkClass[ck] / perChunkModels[ck]
        // (disjoint slots) and writes its own knnForChunk[ck]/cemForChunk[ck]
        // entry.  Hazards moved per-thread: rbuf (read-spike scratch),
        // chunkRng (deterministic per-(ck,it,seed)).  The outer `it` loop is
        // intentionally serial — each iter depends on the previous iter's
        // post-split/merge state.
        #pragma omp parallel for schedule(dynamic)
        for (int ck = 0; ck < nCh; ++ck) {
            // Per-thread scratch (lives in this iteration's stack frame).
            std::vector<int16_t> rbuf(static_cast<size_t>(wElems));
            std::mt19937 chunkRng((RandomSeed != 0
                ? static_cast<unsigned>(RandomSeed) ^ 0x4c4c4c4cu
                : std::random_device{}())
                ^ (static_cast<unsigned>(ck) * 0x9E3779B9u)
                ^ (static_cast<unsigned>(it) << 16));

            const auto& pts = chunkPoints[static_cast<size_t>(ck)];
            auto&       cls = perChunkClass[static_cast<size_t>(ck)];
            auto&       mdls = perChunkModels[static_cast<size_t>(ck)];
            const int   nPts = static_cast<int>(pts.size());
            if (nPts == 0 || mdls.empty()) continue;

            // Members per local cluster.
            std::unordered_map<int, std::vector<int>> memLocal;  // lc -> local idx
            for (int i = 0; i < nPts; ++i) {
                const int c = cls[static_cast<size_t>(i)];
                if (c != 0) memLocal[c].push_back(i);
            }

            // Eligible clusters: size ≥ minSize, not tight-masked.
            std::vector<int> eligible;
            for (const auto& [lc, idx] : memLocal) {
                if (static_cast<int>(idx.size()) < minSize) continue;
                if (doMask) {
                    std::vector<int> gids; gids.reserve(idx.size());
                    for (int li : idx) gids.push_back(pts[static_cast<size_t>(li)]);
                    const auto [rho, nSig] = clusterTightness(gids, rbuf);
                    const double thr = static_cast<double>(Phase4cTightnessThreshold)
                        * std::pow(std::max(1, nSig),
                                   static_cast<double>(Phase4cTightnessSpreadBeta));
                    if (rho < thr) continue;   // tight → masked out
                }
                eligible.push_back(lc);
            }
            if (static_cast<int>(eligible.size()) < 2) continue;

            // meanWav L2 between eligible clusters → each source's N closest.
            auto modelOf = [&](int lc) -> ChunkModel* {
                for (auto& cm : mdls)
                    if (cm.chunkIdx == ck && cm.localClusterId == lc) return &cm;
                return nullptr;
            };
            auto tmplL2 = [&](int a, int b) -> double {
                ChunkModel* ma = modelOf(a); ChunkModel* mb = modelOf(b);
                if (!ma || !mb) return std::numeric_limits<double>::infinity();
                const auto& wa = ma->meanWav; const auto& wb = mb->meanWav;
                if (wa.empty() || wb.empty() || wa.size() != wb.size())
                    return std::numeric_limits<double>::infinity();
                double s = 0.0;
                for (size_t e = 0; e < wa.size(); ++e) {
                    const double d = static_cast<double>(wa[e])
                                   - static_cast<double>(wb[e]);
                    s += d * d;
                }
                return s;
            };

            // Random source order.
            std::vector<int> order = eligible;
            std::shuffle(order.begin(), order.end(), chunkRng);

            const int wantKnn = std::max(0, Phase4cKnnSources);
            const int wantCem = std::max(0, Phase4cFullCemSources);
            const int wantTot = wantKnn + wantCem;
            if (wantTot == 0) continue;

            // Build candidate pools: source + its N closest eligible clusters.
            struct Pool { int src; int route; std::vector<int> members; };  // route 0=knn 1=cem
            std::vector<Pool> pools;
            int kCount = 0, cCount = 0;
            for (int src : order) {
                if (kCount >= wantKnn && cCount >= wantCem) break;
                // N closest eligible neighbours by template L2.
                std::vector<std::pair<double,int>> rank;
                for (int other : eligible)
                    if (other != src) rank.push_back({ tmplL2(src, other), other });
                std::sort(rank.begin(), rank.end());
                std::vector<int> nb;  nb.push_back(src);
                for (int n = 0; n < nNeigh && n < static_cast<int>(rank.size()); ++n)
                    nb.push_back(rank[static_cast<size_t>(n)].second);

                int route;
                if (kCount < wantKnn) { route = 0; ++kCount; }
                else                  { route = 1; ++cCount; }
                pools.push_back({ src, route, std::move(nb) });
            }

            // ── Disjointness guard ──────────────────────────────────────
            // Drop whole sources at random until no two surviving pools share
            // a cluster (→ no overlapping spikes).
            {
                bool conflict = true;
                while (conflict && !pools.empty()) {
                    conflict = false;
                    // cluster -> list of pool indices claiming it
                    std::unordered_map<int, std::vector<int>> claim;
                    for (size_t pi = 0; pi < pools.size(); ++pi)
                        for (int lc : pools[pi].members)
                            claim[lc].push_back(static_cast<int>(pi));
                    std::set<int> conflicting;
                    for (auto& [lc, owners] : claim)
                        if (owners.size() > 1) {
                            conflict = true;
                            for (int pi : owners) conflicting.insert(pi);
                        }
                    if (conflict) {
                        std::vector<int> cv(conflicting.begin(), conflicting.end());
                        std::uniform_int_distribution<size_t> pick(0, cv.size() - 1);
                        const int drop = cv[pick(chunkRng)];
                        pools.erase(pools.begin() + drop);
                    }
                }
            }
            if (pools.empty()) continue;

            // ── Transient relabel: pool neighbours → source; refresh source
            //    model (mean/cov/meanWav) and drop absorbed-neighbour models.
            for (const auto& P : pools) {
                std::set<int> absorbed(P.members.begin(), P.members.end());
                absorbed.erase(P.src);
                if (!absorbed.empty()) {
                    for (int i = 0; i < nPts; ++i)
                        if (absorbed.count(cls[static_cast<size_t>(i)]))
                            cls[static_cast<size_t>(i)] = P.src;
                }

                // Recompute source mean/cov directly from its (enlarged) members.
                std::vector<int> mem;
                for (int i = 0; i < nPts; ++i)
                    if (cls[static_cast<size_t>(i)] == P.src) mem.push_back(i);
                const int M = static_cast<int>(mem.size());
                ChunkModel* sm = modelOf(P.src);
                if (sm && M > 0) {
                    std::vector<double> mu(static_cast<size_t>(nFullDims), 0.0);
                    for (int li : mem)
                        for (int d = 0; d < nFullDims; ++d)
                            mu[static_cast<size_t>(d)] +=
                                Data[static_cast<size_t>(pts[static_cast<size_t>(li)])
                                     * nFullDims + d];
                    const double invM = 1.0 / M;
                    for (int d = 0; d < nFullDims; ++d) mu[static_cast<size_t>(d)] *= invM;
                    sm->mean.assign(static_cast<size_t>(nFullDims), 0.0f);
                    for (int d = 0; d < nFullDims; ++d)
                        sm->mean[static_cast<size_t>(d)] =
                            static_cast<float>(mu[static_cast<size_t>(d)]);
                    sm->cov.assign(static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
                    for (int li : mem) {
                        const int p = pts[static_cast<size_t>(li)];
                        for (int r = 0; r < nFullDims; ++r) {
                            const double dr = Data[static_cast<size_t>(p) * nFullDims + r]
                                            - mu[static_cast<size_t>(r)];
                            for (int col = r; col < nFullDims; ++col) {
                                const double dc =
                                    Data[static_cast<size_t>(p) * nFullDims + col]
                                    - mu[static_cast<size_t>(col)];
                                sm->cov[static_cast<size_t>(r) * nFullDims + col] +=
                                    static_cast<float>(dr * dc * invM);
                            }
                        }
                    }
                    sm->nMembers = M;
                    // Refresh meanWav from current alignment.
                    if (m_timeShiftReady) {
                        std::vector<double> mw(static_cast<size_t>(wElems), 0.0);
                        int okw = 0;
                        for (int li : mem) {
                            if (!TimeShiftReadSpikeWave(pts[static_cast<size_t>(li)],
                                                        wElems, rbuf.data())) continue;
                            for (int e = 0; e < wElems; ++e)
                                mw[static_cast<size_t>(e)] +=
                                    static_cast<double>(rbuf[static_cast<size_t>(e)]);
                            ++okw;
                        }
                        if (okw > 0) {
                            sm->meanWav.assign(static_cast<size_t>(wElems), 0);
                            for (int e = 0; e < wElems; ++e)
                                sm->meanWav[static_cast<size_t>(e)] =
                                    static_cast<int16_t>(std::lround(
                                        mw[static_cast<size_t>(e)] / okw));
                        }
                    }
                }

                // Remove absorbed-neighbour models so they are not phantom
                // references for WaveKnn.
                if (!absorbed.empty()) {
                    mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
                        [&](const ChunkModel& cm){
                            return cm.chunkIdx == ck
                                && absorbed.count(cm.localClusterId) > 0;
                        }), mdls.end());
                }

                if (P.route == 0) knnForChunk[static_cast<size_t>(ck)].push_back(P.src);
                else              cemForChunk[static_cast<size_t>(ck)].push_back(P.src);
            }
        }

        // ── Serial gather: scratch per-chunk vectors → final allowlist maps.
        std::map<int, std::vector<int>> knnAllow, cemAllow;
        for (int ck = 0; ck < nCh; ++ck) {
            if (!knnForChunk[static_cast<size_t>(ck)].empty())
                knnAllow[ck] = std::move(knnForChunk[static_cast<size_t>(ck)]);
            if (!cemForChunk[static_cast<size_t>(ck)].empty())
                cemAllow[ck] = std::move(cemForChunk[static_cast<size_t>(ck)]);
        }

        if (knnAllow.empty() && cemAllow.empty()) {
            LockedStderr("[Phase 4c] iter %d: no eligible pools; stopping\n", it + 1);
            break;
        }

        // Snapshot to count spikes changed by the re-split.
        std::vector<std::vector<int>> before = perChunkClass;

        // ── Re-split each pooled super-cluster (allowlisted) ────────────
        if (!knnAllow.empty())
            WaveKnnSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                                 nFullDims, &knnAllow);
        if (!cemAllow.empty())
            FullCemSplitPerChunk(chunkPoints, perChunkClass, perChunkModels,
                                 nFullDims, &cemAllow);

        // ── Harvest + merge (reuse Phase 4 within-chunk template merge) ──
        const int merged = MedianKnnTemplateMatchEnable
            ? WithinChunkTemplateMatchMedianKnn(chunkPoints, perChunkClass,
                                                perChunkModels, nChan, nSamp,
                                                TemplateMatchScore)
            : WithinChunkTemplateMatch(chunkPoints, perChunkClass,
                                       perChunkModels, nChan, nSamp,
                                       TemplateMatchScore);

        for (size_t k = 0; k < perChunkClass.size(); ++k) {
            const auto& a = perChunkClass[k];
            const auto& b = before[k];
            if (a.size() != b.size()) continue;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i] != b[i]) ++changedThisIter;
        }
        grandChanged += changedThisIter;

        LockedStderr("[Phase 4c] iter %d/%d: %d spikes relabeled, %d merge(s)\n",
                     it + 1, maxIters, changedThisIter, merged);
        if (changedThisIter == 0 && merged == 0) break;   // converged
    }

    LockedStderr("[Phase 4c] done: %d spikes relabeled across remix passes\n",
                 grandChanged);
}


// ---------------------------------------------------------------------------
// KK::ChunkReCEMPerChunk
//
// Phase 2b: chunk-level CEM with RANDOM initialisation.  After
// PerClusterCEMPerChunk has produced fine-grained per-cluster splits,
// this pass runs an ordinary chunk-level CEM seeded with random class
// assignment (the canonical irand(1, K-1) pattern), with K preserved
// from the count of distinct non-noise labels in cls0.
//
// Random init rather than warm-start from cls0: a warm-started CEM
// converges to the nearest local optimum, which is essentially a
// refinement of the input labels — it cannot escape local minima
// Phase 1 and Phase 2a got stuck in.  Random init forces the CEM to
// re-explore the configuration space at the same K, and TrySplits +
// ConsiderDeletion within the loop prune empty sub-clusters and merge
// oversplit groups so the final partition reflects the data's
// structure rather than the warm-start's biases.
//
// Splits are kept enabled; CEM may discover further structure or
// reduce K below nReal via ConsiderDeletion.
//
// Skipped chunks: those with fewer than 2 distinct non-noise labels
// (random init into one cluster is degenerate).
//
// ChunkModel rebuild from final Ks.Mean / Ks.Cov.  This replaces all
// previous ChunkModels for the chunk; cross-chunk Phase 6 reads only
// the rebuilt list.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// RunPhase2bMode3Chunk — Phase 2b mode 3 per-chunk driver.
//
// Iterative residual-PCA refinement + dominant-channel xcorr realignment
// for one chunk's spikes.  See the long comment at the Phase2bMode == 3
// dispatch (in the Phase 2b chunk loop) for the full algorithm rationale.
//
// Inputs:
//   Ks                — the per-chunk sub-KK (warm-started from Phase 2a)
//   pts               — global spike indices in this chunk [nPts]
//   nChan             — channel count for this group
//   nSamplesPerSpike  — sample count per spike window
//   nStart            — initial cluster count (incl. noise=cluster 0)
//
// Side effects:
//   * Modifies Ks.Class[], Ks.ClassAlive[]: refined per-chunk labels.
//   * Modifies this->m_cumShift[] for indices in `pts`: per-spike
//     realignment shifts (additive, capped at ±m_timeShiftMaxAbs).
//
// Reads .spk via TimeShiftReadSpikeWave (mmap path is thread-safe; the
// FILE* fallback is serialised here with a local critical section).
//
// Convergence: stops early when the fraction of label-changing spikes in
// an iteration falls below ResidualPCAConvTol, or after ResidualPCAIter
// outer iterations.
// ---------------------------------------------------------------------------
void KK::RunPhase2bMode3Chunk(KK& Ks, const std::vector<int>& pts,
                              int nChan, int nSamplesPerSpike,
                              int nStart, int chunkIdx)
{
    const int nPts = static_cast<int>(pts.size());
    if (nPts < 5 || nChan <= 0 || nSamplesPerSpike <= 0) return;
    if (!m_timeShiftReady) {
        // mmap/FILE not set up — fall back to mode 0 behaviour silently.
        Ks.MStep(); Ks.EStep();
        Ks.RunEMLoop(true, false, 0, "[2b-m3-fallback]");
        return;
    }

    const int waveSamples = nChan * nSamplesPerSpike;
    const int nSubDims    = Ks.nDims;          // VBGMM feature width
    const int K_init      = nStart;            // initial K (noise + real)

    // Sanitise hyperparameters from CLI globals
    const int maxIter      = std::max(1, ResidualPCAIter);
    const int nResidComp   = std::clamp(ResidualPCAComponents, 1, 8);
    const int nDomCh       = std::clamp(ResidualPCADominantChannels, 1,
                                        std::max(1, nChan));
    const int subK         = std::clamp(ResidualPCASubK, 2, 8);
    const double convTol   = std::max(0.0,
                                     static_cast<double>(ResidualPCAConvTol));

    // Entry banner.  Each per-chunk worker emits one of these — when the
    // chunk loop is parallel the lines interleave across threads, which is
    // why every line is self-contained with its chunk index.
    Output("[Phase 2b m3] chunk %d START: %d spikes, %d sub-dims, K_init=%d, "
           "maxIter=%d, K_resid=%d, K_sub=%d, domCh=%d, convTol=%.4f, "
           "maxShift=%d\n",
           chunkIdx, nPts, nSubDims, K_init, maxIter, nResidComp, subK,
           nDomCh, convTol, m_timeShiftMaxAbs);

    // ── Read all chunk spikes into a local buffer ───────────────────────
    // Bulk-read upfront so the inner loop has random access without per-spike
    // file ops.  Memory cost: nPts × waveSamples × 2 bytes — for a typical
    // chunk of 50k spikes × 336 samples × 2B = ~32 MiB per chunk worker.
    std::vector<int16_t> waveBuf(static_cast<size_t>(nPts) * waveSamples);
    {
        int16_t scratch[2];  // unused; TimeShiftReadSpikeWave writes directly
        (void)scratch;
        bool needCrit = (m_timeShiftSpkMap == nullptr);  // FILE* path → serial
        if (needCrit) {
            #pragma omp critical (KK_TimeShiftReadSpikeWave)
            {
                for (int i = 0; i < nPts; i++) {
                    int16_t* dst = waveBuf.data() +
                                   static_cast<size_t>(i) * waveSamples;
                    if (!TimeShiftReadSpikeWave(pts[i], waveSamples, dst))
                        std::fill(dst, dst + waveSamples, 0);
                }
            }
        } else {
            // mmap is reentrant — issue reads in parallel-safe loop
            for (int i = 0; i < nPts; i++) {
                int16_t* dst = waveBuf.data() +
                               static_cast<size_t>(i) * waveSamples;
                if (!TimeShiftReadSpikeWave(pts[i], waveSamples, dst))
                    std::fill(dst, dst + waveSamples, 0);
            }
        }
    }

    // ── Iteration buffers ───────────────────────────────────────────────
    std::vector<int>    prevLabels(nPts), labels(nPts);
    std::vector<double> meanWave(static_cast<size_t>(waveSamples), 0.0);
    std::vector<double> residual;                  // re-sized per cluster
    std::vector<double> covR(static_cast<size_t>(waveSamples) *
                              waveSamples, 0.0);
    std::vector<double> eigvecs;                   // [K * waveSamples]
    std::vector<double> eigvals;                   // [K]
    std::vector<float>  subFeat;                   // [N_c * nResidComp]
    std::vector<int>    subLabels;
    std::vector<int>    members;
    std::vector<int>    domChans;

    // Seed labels from the warm-started Ks.Class[].
    for (int i = 0; i < nPts; i++) labels[i] = Ks.Class[i];

    int totalSplitsApplied = 0;
    int totalRealigned     = 0;
    int actualItersRun     = 0;

    for (int iter = 0; iter < maxIter; iter++) {
        prevLabels = labels;
        ++actualItersRun;

        // Snapshot alive-cluster count before this iter's VBGMM
        int aliveBefore = 0;
        {
            std::vector<int> seen; seen.reserve(64);
            for (int v : labels) {
                if (v <= 0) continue;
                bool found = false;
                for (int s : seen) if (s == v) { found = true; break; }
                if (!found) seen.push_back(v);
            }
            aliveBefore = static_cast<int>(seen.size());
        }

        // ── Step 1: VBGMM on chunk features with current label warm-start ─
        // First iter: warm-start from Phase 2a (already in labels).  Later
        // iters: warm-start from the previous iter's split-and-realign
        // labels (re-featurized via Data[] which doesn't change inside this
        // chunk loop, but the labels carry forward the structure we found).
        int vbgmmSurvivors = 0;
        {
            std::vector<int> lbuf = labels;
            // Cluster-id space may have expanded by splits in previous
            // iter; bound K_init by the labels actually present.
            int kCur = K_init;
            for (int v : lbuf) if (v + 1 > kCur) kCur = v + 1;
            vbgmmSurvivors = RunVBGMM(
                Ks.Data.m_Data, lbuf.data(), kCur, nPts, nSubDims,
                VBGMMMaxIter, static_cast<double>(VBGMMConvTol));
            labels = lbuf;
        }

        // Per-iter counters
        int splitsThisIter      = 0;  // # clusters that got split
        int newSubClustersIter  = 0;  // # new IDs created from splits
        int clustersExamined    = 0;
        int clustersTooSmall    = 0;
        int realignThisIter     = 0;

        // ── Step 2/3: per-cluster residual-PCA + split via residual VBGMM ─
        // Find max alive cluster id to know where to assign new sub-ids.
        int nextId = 0;
        for (int v : labels) if (v + 1 > nextId) nextId = v + 1;

        // Collect ids of currently-alive non-noise clusters (snapshot
        // BEFORE we start splitting — we don't recurse into freshly-made
        // sub-clusters in this same iter).
        std::vector<int> activeIds;
        {
            std::vector<int> seen;
            seen.reserve(64);
            for (int v : labels) {
                if (v <= 0) continue;
                bool found = false;
                for (int s : seen) if (s == v) { found = true; break; }
                if (!found) seen.push_back(v);
            }
            activeIds = std::move(seen);
        }

        for (int cid : activeIds) {
            ++clustersExamined;
            // Gather members of cluster cid
            members.clear();
            for (int i = 0; i < nPts; i++)
                if (labels[i] == cid) members.push_back(i);
            const int Nc = static_cast<int>(members.size());
            // Need enough spikes for stable mean + residual PCA: a soft
            // floor of 2·waveSamples keeps the per-cluster covariance
            // non-degenerate.  Below that, attempting to split is more
            // likely to overfit noise than discover real structure.
            if (Nc < std::max(20, 2 * nResidComp)) { ++clustersTooSmall; continue; }

            // Mean waveform of the cluster
            std::fill(meanWave.begin(), meanWave.end(), 0.0);
            for (int m : members) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(m) * waveSamples;
                for (int j = 0; j < waveSamples; j++)
                    meanWave[j] += static_cast<double>(w[j]);
            }
            const double invN = 1.0 / Nc;
            for (int j = 0; j < waveSamples; j++) meanWave[j] *= invN;

            // Build residuals: residual[i, j] = waveBuf[member_i, j] − mean[j]
            residual.assign(static_cast<size_t>(Nc) * waveSamples, 0.0);
            for (int i = 0; i < Nc; i++) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(members[i]) * waveSamples;
                double* rrow = residual.data() +
                               static_cast<size_t>(i) * waveSamples;
                for (int j = 0; j < waveSamples; j++)
                    rrow[j] = static_cast<double>(w[j]) - meanWave[j];
            }

            // Residual covariance Σ = (1/(N-1)) Rᵀ R  in [waveSamples × waveSamples]
            std::fill(covR.begin(), covR.end(), 0.0);
            const double cscale = 1.0 / std::max(1, Nc - 1);
            for (int i = 0; i < Nc; i++) {
                const double* rrow = residual.data() +
                                     static_cast<size_t>(i) * waveSamples;
                for (int a = 0; a < waveSamples; a++) {
                    const double ra = rrow[a];
                    double* crow = covR.data() +
                                   static_cast<size_t>(a) * waveSamples;
                    for (int b = 0; b < waveSamples; b++)
                        crow[b] += ra * rrow[b];
                }
            }
            for (size_t k = 0; k < covR.size(); k++) covR[k] *= cscale;

            // Top-K eigenvectors via power iteration with deflation
            TopKEigenPowerDeflation(covR, waveSamples, nResidComp,
                                    /*maxIter=*/50, /*relTol=*/1e-4,
                                    eigvecs, eigvals);

            // Project residuals onto the top eigenvectors → sub-features
            // subFeat[i * nResidComp + k] = ⟨residual[i], eigvecs[k]⟩
            subFeat.assign(static_cast<size_t>(Nc) * nResidComp, 0.0f);
            for (int i = 0; i < Nc; i++) {
                const double* rrow = residual.data() +
                                     static_cast<size_t>(i) * waveSamples;
                for (int k = 0; k < nResidComp; k++) {
                    const double* ev = eigvecs.data() +
                                       static_cast<size_t>(k) * waveSamples;
                    double s = 0.0;
                    for (int j = 0; j < waveSamples; j++) s += rrow[j] * ev[j];
                    subFeat[static_cast<size_t>(i) * nResidComp + k] =
                        static_cast<float>(s);
                }
            }

            // VBGMM on sub-features: initialise everyone in cluster 0 of
            // the sub-space, K_init = subK (Dirichlet prior shrinks K).
            subLabels.assign(static_cast<size_t>(Nc), 0);
            // Spread initial labels round-robin so VBGMM has K seeds to
            // grow from rather than collapsing immediately.
            for (int i = 0; i < Nc; i++)
                subLabels[static_cast<size_t>(i)] = i % subK;
            const int nSurv = RunVBGMM(subFeat.data(), subLabels.data(),
                                       subK, Nc, nResidComp,
                                       VBGMMMaxIter,
                                       static_cast<double>(VBGMMConvTol));

            if (nSurv <= 1) continue;  // no split discovered → keep cluster

            // Find the majority sub-cluster id (gets to keep cid); the rest
            // become new clusters numbered nextId, nextId+1, ...
            std::vector<int> subCounts(subK, 0);
            for (int v : subLabels) if (v >= 0 && v < subK) subCounts[v]++;
            int majority = 0, majCount = subCounts[0];
            for (int k = 1; k < subK; k++)
                if (subCounts[k] > majCount) { majCount = subCounts[k]; majority = k; }

            std::vector<int> subRemap(subK, -1);
            subRemap[majority] = cid;
            int newIdsAddedHere = 0;
            for (int k = 0; k < subK; k++) {
                if (k == majority) continue;
                if (subCounts[k] == 0)  continue;
                if (nextId >= MaxPossibleClusters) {
                    // Out of ID space — collapse remaining sub-clusters
                    // back to the original cid.  No structural loss; just
                    // skip the split for this iter.
                    subRemap[k] = cid;
                } else {
                    subRemap[k] = nextId++;
                    ++newIdsAddedHere;
                }
            }
            // Apply remap to chunk-wide labels[]
            for (int i = 0; i < Nc; i++) {
                const int sk  = subLabels[static_cast<size_t>(i)];
                const int newCid = (sk >= 0 && sk < subK) ? subRemap[sk] : cid;
                labels[members[i]] = newCid;
            }
            if (newIdsAddedHere > 0) {
                ++splitsThisIter;
                newSubClustersIter += newIdsAddedHere;
                ++totalSplitsApplied;
            }
        }

        // (Step 4 — per-spike xcorr realign — moved OUT of the iter loop.
        // Reason: TimeShiftReadSpikeWave reads the original .spk verbatim
        // (no .fil re-extraction mid-chunk for parallel-safety), so the
        // cluster mean computed in iter N+1 is identical to iter N's.
        // Per-iter xcorr would therefore return the same lag every iter
        // and either double-shift or cap-clamp.  One pass at the end on
        // the final cluster labels is both correct and matches Klusters'
        // single-shot realignSpikes behaviour.)
        (void)realignThisIter;  // intentionally always 0 inside the loop

        // ── Step 5: convergence check + per-iter banner ──────────────────
        int nChanged = 0;
        for (int i = 0; i < nPts; i++)
            if (labels[i] != prevLabels[i]) ++nChanged;
        const double frac = static_cast<double>(nChanged) / nPts;
        const bool   converged = (frac < convTol);

        Output("[Phase 2b m3] chunk %d iter %d/%d: alive %d -> VBGMM survivors %d, "
               "examined %d (%d too small), %d splits -> %d new sub-clusters, "
               "%d spike realigns, %d/%d (%.2f%%) labels changed%s\n",
               chunkIdx, iter + 1, maxIter,
               aliveBefore, vbgmmSurvivors,
               clustersExamined, clustersTooSmall,
               splitsThisIter, newSubClustersIter,
               realignThisIter,
               nChanged, nPts, 100.0 * frac,
               converged ? " [CONVERGED]" : "");

        if (converged) break;
    }

    // ──── Final per-cluster alignment (post-split-iterations) ────────────
    //
    // patch71 — Iterative on-the-spot xcorr alignment.
    //
    // Replaces the prior single-pass-with-cap-rejection design with an
    // iterative one that re-extracts each spike's waveform from .fil at
    // its updated position IMMEDIATELY after each xcorr step, and
    // re-projects features into Data[] in the same place.  Three things
    // changed from the original:
    //
    //   (a) DROP CAP-REJECTION on the cumulative shift.  The previous
    //       version refused any shift whose accumulated magnitude would
    //       exceed m_timeShiftMaxAbs (default 3), leaving spikes that
    //       needed larger total motion STUCK at their original
    //       mis-detected positions.  Visible result: ±5-sample
    //       within-cluster temporal dispersion after KiloKlustaKwik
    //       completes.  Per-iter lag is naturally bounded by xcorr's
    //       maxShift parameter; cumulative motion across iters is
    //       unbounded by design — a spike needing 5 samples can get
    //       +3 in iter 0, then +2 in iter 1, then 0 in iter 2.  The
    //       only safety gate is the extraction-window bounds check
    //       (off must fit within sessionSamples).
    //
    //   (b) RE-EXTRACT AND RE-FEATURIZE ON THE SPOT.  After updating
    //       m_cumShift[gp], read the new waveform from .fil at
    //       (rawTs + cumShift - PeakSampleIndex), apply the stderiv
    //       transform if the pipeline is .pcaD/.spkD, overwrite
    //       waveBuf[i_local] so the NEXT iter's xcorr sees the
    //       re-extracted waveform, and project through the canonical
    //       PCA basis to update Data[gp].  This is what makes "merge
    //       toward the centre" converge: subsequent iters see the
    //       already-shifted waveforms and the cluster mean re-built
    //       from them is well-centred on PeakSampleIndex.
    //
    //   (c) USE INT64 rawTs from .res, NOT float-recovery from
    //       Data[timeDim].  RefeaturizeFromShifts at lines 2818-2822
    //       explicitly warns that recovering rawTs by inverse-
    //       normalising the float feature introduces up to ±13 samples
    //       of error for late-session spikes.  For a 1-hour 32 kHz
    //       session that error is ±~7 samples on the last spike —
    //       larger than the per-iter maxShift, so any code relying on
    //       float recovery never converges.  We pre-load int64 rawTs
    //       for every chunk-spike from .res ONCE at function entry and
    //       index by local i_local.
    //
    // "Merge toward the centre": the existing patch64 template pre-
    // alignment forces every cluster's mean template peak to land on
    // PeakSampleIndex (the waveform-window centre).  When two clusters
    // would merge (similar waveforms), both pre-aligned templates land
    // on the same centre; xcorr pulls both clusters' spikes there.
    // Clusters therefore merge AT the centre rather than at one
    // cluster's drifted peak.
    //
    // Disk writes (.spk / .fet / .res .pending) remain deferred to the
    // existing TimeShiftFinalize → RefeaturizeFromShifts path.  This
    // function only updates in-memory state (waveBuf, Data[], m_cumShift)
    // because the parallel chunk loop would race on the global disk
    // files otherwise.  TimeShiftFinalize is single-threaded and reads
    // the final m_cumShift values to do the actual disk write.
    int finalAligned     = 0;
    int finalLowScore    = 0;
    int finalOutOfBounds = 0;
    int alignItersRun    = 0;
    if (AlignPcaCenter == 2) {
        // ─── patch84: in-memory circular-shift PCA-centering, no m_cumShift ───
        //
        // REPLACES the xcorr iter loop + patch83 refine + patch74 monotonicity
        // entirely.  Designed to avoid the temporal-dispersion failure mode
        // that surfaces when cumulative shifts (m_cumShift) accumulate across
        // phases and survive into TimeShiftFinalize's disk writes.
        //
        // Algorithm:
        //   for iter = 0 .. alignMaxIter:
        //       for each alive cluster cid:
        //           μ_pca[f] = mean over cluster of Data[gp * nDims + f]
        //                      for f in 0 .. (basisNChan*basisNComp - 1)
        //           for each spike (member i) of cluster:
        //               for each candidate shift s in [-maxShift, +maxShift]:
        //                   scratch = circular_shift(waveBuf[i], s)
        //                   project scratch through canonical PCA basis with
        //                       dimMin_/dimRange_ normalisation
        //                   dist²(s) = sum_f (pca_f - μ_pca[f])²
        //               bestS = argmin_s dist²(s)
        //               if bestS != 0:
        //                   waveBuf[i] = circular_shift(waveBuf[i], bestS)
        //                   Data[gp]   = recomputed PCA features of new wave
        //       if no spike moved this iter: break
        //
        // What it touches:
        //   waveBuf[]      → circularly shifted in-place per spike
        //   Data[gp][f<D]  → PCA columns refreshed from shifted waveform
        //                    (NOT the time column at index nDims-1 —
        //                    circular shift doesn't change detection time)
        //
        // What it deliberately does NOT touch:
        //   m_cumShift[]   → left exactly as found.  If prior phases set
        //                    nonzero values, those are preserved; if zero,
        //                    they stay zero.  This is the design point:
        //                    TimeShiftFinalize keys off m_cumShift to write
        //                    .spk/.fet/.res, and we want it to write nothing
        //                    (for chunks running mode 2 fresh) or only what
        //                    earlier phases already settled.
        //
        // What's deliberately SKIPPED relative to modes 0/1:
        //   - raw-source verify (no .fil reads in mode 2)
        //   - patch73 pre-refresh of waveBuf from .fil (waveBuf stays in
        //     its as-loaded .spk state; circular shifts compose from there)
        //   - patch74 monotonicity check (m_cumShift unchanged so .res
        //     order is preserved by construction)
        //
        // Why circular shift is safe here: the .spk content is highpass-
        // filtered (or stderiv-transformed, which is doubly high-pass-like),
        // so there's effectively no DC and the wrap discontinuity at the
        // window edge is small.  More importantly, the PCA basis windows
        // around basisRecShift .. basisRecShift + basisData2use — typically
        // centered on the peak with margin — so a few samples of wrap at
        // the window EDGE don't enter the projection at small shift sizes.
        //
        // What the user does afterward: if they want the on-disk .spkD /
        // .fetD to reflect the alignment chosen in mode 2, they re-extract
        // out-of-band from .fil using their own tooling.  KKExp neither
        // assumes nor performs that step.
        const bool hasBasisP84 = m_timeShiftBasis.valid() &&
                                 !m_timeShiftBasis.meanShifted.empty();
        if (!hasBasisP84) {
            Output("[Phase 2b m3] chunk %d patch84 mode 2: "
                   "PCA basis not loaded — alignment skipped entirely.\n",
                   chunkIdx);
        } else {
            const int   candCanonicalP84 = m_timeShiftBasis.N;
            const int   basisNCompP84    = m_timeShiftBasis.nComp;
            const int   basisData2useP84 = m_timeShiftBasis.data2use;
            const int   basisRecShiftP84 = m_timeShiftBasis.recShift;
            const int   basisNChanP84    = m_timeShiftBasis.nChan;
            const bool  basisCenteredP84 = m_timeShiftBasis.isCentered;
            const int   nPCAFeatsP84     = basisNChanP84 * basisNCompP84;
            const int   maxShiftP84      = m_timeShiftMaxAbs;
            const int   alignMaxIterP84  = std::clamp(ResidualPCAIter, 2, 10);
            const int   nSampP84         = nSamplesPerSpike;

            const auto& meanCanonP84 =
                m_timeShiftBasis.meanShifted[
                    static_cast<size_t>(candCanonicalP84)];
            const auto& evecCanonP84 =
                m_timeShiftBasis.eigvecShifted[
                    static_cast<size_t>(candCanonicalP84)];

            std::vector<int16_t> scratchWaveP84(
                static_cast<size_t>(waveSamples));

            int p84TotalShifts = 0;
            int p84ItersRun    = 0;

            // The candidate-shift sweep and the post-commit refeaturize
            // both project a sample-major waveform through the canonical
            // PCA basis using dimMin_/dimRange_ normalisation.  Both are
            // inlined below (rather than factored into a lambda) because
            // (a) the hot path is the inner-most loop, function-call
            // overhead matters, and (b) the sweep version also needs to
            // accumulate distance² against muPca, while the refeaturize
            // version writes into Data[gp] — fusing those into a single
            // lambda would force conditional branching inside the loop.

            for (int iterP84 = 0; iterP84 < alignMaxIterP84; ++iterP84) {
                ++p84ItersRun;
                int shiftsThisIter = 0;

                // Recollect alive cluster ids from CURRENT labels (clusters
                // may have died/split between iters of the outer phase).
                std::vector<int> idsP84;
                idsP84.reserve(64);
                for (int v : labels) {
                    if (v <= 0) continue;
                    bool found = false;
                    for (int s : idsP84)
                        if (s == v) { found = true; break; }
                    if (!found) idsP84.push_back(v);
                }

                for (int cid : idsP84) {
                    std::vector<int> members;
                    for (int i = 0; i < nPts; ++i)
                        if (labels[i] == cid) members.push_back(i);
                    const int Nc = static_cast<int>(members.size());
                    if (Nc < 3) continue;

                    // μ_pca over cluster from current Data[]
                    std::vector<double> muPca(
                        static_cast<size_t>(nPCAFeatsP84), 0.0);
                    int contributors = 0;
                    for (int m : members) {
                        const int gp = pts[static_cast<size_t>(m)];
                        if (gp < 0) continue;
                        const float* row = Data.m_Data + gp * nDims;
                        for (int f = 0; f < nPCAFeatsP84; ++f)
                            muPca[static_cast<size_t>(f)] +=
                                static_cast<double>(row[f]);
                        ++contributors;
                    }
                    if (contributors < 3) continue;
                    const double invN = 1.0 / contributors;
                    for (int f = 0; f < nPCAFeatsP84; ++f)
                        muPca[static_cast<size_t>(f)] *= invN;

                    for (int sp = 0; sp < Nc; ++sp) {
                        const int iLocal = members[static_cast<size_t>(sp)];
                        const int gp     = pts[static_cast<size_t>(iLocal)];
                        if (gp < 0) continue;

                        int16_t* wave = waveBuf.data()
                            + static_cast<ptrdiff_t>(iLocal)
                            * static_cast<ptrdiff_t>(waveSamples);

                        int    bestS    = 0;
                        double bestDist = std::numeric_limits<double>::infinity();

                        for (int s = -maxShiftP84; s <= maxShiftP84; ++s) {
                            // Circular shift wave by s into scratchWaveP84.
                            // Sample-major: scratch[((t + s) mod N) * nChan + c]
                            //               = wave[t * nChan + c]
                            for (int t = 0; t < nSampP84; ++t) {
                                int tNew = (t + s) % nSampP84;
                                if (tNew < 0) tNew += nSampP84;
                                const int rowDst = tNew * nChan;
                                const int rowSrc = t    * nChan;
                                for (int c = 0; c < nChan; ++c)
                                    scratchWaveP84[
                                        static_cast<size_t>(rowDst + c)] =
                                            wave[rowSrc + c];
                            }

                            // PCA project scratchWaveP84 and accumulate
                            // distance² to muPca, inline (lambda-free for
                            // speed — this is the inner-most loop).
                            double dist2 = 0.0;
                            for (int ch = 0; ch < basisNChanP84; ++ch) {
                                const auto& mu = meanCanonP84[
                                    static_cast<size_t>(ch)];
                                const auto& ev = evecCanonP84[
                                    static_cast<size_t>(ch)];
                                for (int k = 0; k < basisNCompP84; ++k) {
                                    double val = 0.0;
                                    for (int u = 0; u < basisData2useP84; ++u) {
                                        const int sIdx = basisRecShiftP84 + u;
                                        double x = static_cast<double>(
                                            scratchWaveP84[
                                                static_cast<size_t>(
                                                    sIdx * nChan + ch)]);
                                        if (basisCenteredP84)
                                            x -= mu[static_cast<size_t>(u)];
                                        val += ev[static_cast<size_t>(
                                            k * basisData2useP84 + u)] * x;
                                    }
                                    const int featIdx = ch * basisNCompP84 + k;
                                    if (featIdx >= nPCAFeatsP84 ||
                                        featIdx >= static_cast<int>(
                                            dimMin_.size()))
                                        continue;
                                    const double normVal =
                                        (static_cast<float>(val)
                                       - dimMin_[featIdx])
                                      * dimRange_[featIdx];
                                    const double diff = normVal
                                      - muPca[static_cast<size_t>(featIdx)];
                                    dist2 += diff * diff;
                                }
                            }

                            if (dist2 < bestDist) {
                                bestDist = dist2;
                                bestS    = s;
                            }
                        }

                        if (bestS == 0) continue;  // already at the centre

                        // Commit the chosen circular shift to waveBuf in
                        // place (via scratch since source and dest overlap).
                        for (int t = 0; t < nSampP84; ++t) {
                            int tNew = (t + bestS) % nSampP84;
                            if (tNew < 0) tNew += nSampP84;
                            const int rowDst = tNew * nChan;
                            const int rowSrc = t    * nChan;
                            for (int c = 0; c < nChan; ++c)
                                scratchWaveP84[
                                    static_cast<size_t>(rowDst + c)] =
                                        wave[rowSrc + c];
                        }
                        std::memcpy(wave, scratchWaveP84.data(),
                                    static_cast<size_t>(waveSamples)
                                  * sizeof(int16_t));

                        // Refresh Data[gp]'s PCA features (only — the
                        // timestamp at index nDims-1 stays put because
                        // circular shift does not change detection time).
                        float* dataRow = Data.m_Data + gp * nDims;
                        for (int ch = 0; ch < basisNChanP84; ++ch) {
                            const auto& mu = meanCanonP84[
                                static_cast<size_t>(ch)];
                            const auto& ev = evecCanonP84[
                                static_cast<size_t>(ch)];
                            for (int k = 0; k < basisNCompP84; ++k) {
                                double val = 0.0;
                                for (int u = 0; u < basisData2useP84; ++u) {
                                    const int sIdx = basisRecShiftP84 + u;
                                    double x = static_cast<double>(
                                        wave[sIdx * nChan + ch]);
                                    if (basisCenteredP84)
                                        x -= mu[static_cast<size_t>(u)];
                                    val += ev[static_cast<size_t>(
                                        k * basisData2useP84 + u)] * x;
                                }
                                const int featIdx = ch * basisNCompP84 + k;
                                if (featIdx >= nPCAFeatsP84 ||
                                    featIdx >= static_cast<int>(
                                        dimMin_.size()))
                                    continue;
                                dataRow[featIdx] =
                                    (static_cast<float>(val)
                                   - dimMin_[featIdx])
                                  * dimRange_[featIdx];
                            }
                        }

                        ++shiftsThisIter;
                        ++p84TotalShifts;
                    }  // end per-spike loop
                }      // end per-cluster loop

                if (shiftsThisIter == 0) break;
            }          // end iter loop

            alignItersRun = p84ItersRun;
            finalAligned  = p84TotalShifts;

            Output("[Phase 2b m3] chunk %d patch84 PCA-centering (circular): "
                   "%d total shifts over %d iter(s); "
                   "m_cumShift NOT modified (mode 2)\n",
                   chunkIdx, p84TotalShifts, p84ItersRun);
        }
    } else
    {
        // Re-collect alive non-noise cluster ids from final labels
        std::vector<int> finalIds;
        finalIds.reserve(64);
        for (int v : labels) {
            if (v <= 0) continue;
            bool found = false;
            for (int s : finalIds) if (s == v) { found = true; break; }
            if (!found) finalIds.push_back(v);
        }

        // Scratch buffers — sized for the largest cluster's batch call
        std::vector<int16_t> tmpl(static_cast<size_t>(waveSamples), 0);
        std::vector<int16_t> xcWfm;
        std::vector<int>     xcShifts;
        std::vector<float>   xcScores;
        const float minScore = ResidualPCAMinScore;
        const int   maxShift = m_timeShiftMaxAbs;

        // ── Re-extract / re-featurize setup ──────────────────────────────
        char filPathP71[STRLEN + 16] = {0};
        pickInputPath(filPathP71, sizeof(filPathP71), FileBase, "fil", ElecNo);
        FILE* filFpP71 = fopen(filPathP71, "rb");

        const float   sessionSamplesF   = timeRawMax - timeRawMin;
        const int64_t sessionSamplesP71 = static_cast<int64_t>(
            std::max(0.0f, sessionSamplesF));
        const int     timeDimIdxP71     = nDims - 1;

        const bool hasBasis = m_timeShiftBasis.valid() &&
                              !m_timeShiftBasis.meanShifted.empty();
        const int  candCanonical = hasBasis ? m_timeShiftBasis.N : 0;
        const int  basisNComp    = hasBasis ? m_timeShiftBasis.nComp : 0;
        const int  basisData2use = hasBasis ? m_timeShiftBasis.data2use : 0;
        const int  basisRecShift = hasBasis ? m_timeShiftBasis.recShift : 0;
        const int  basisNChan    = hasBasis ? m_timeShiftBasis.nChan : nChan;
        const bool basisCentered = hasBasis ? m_timeShiftBasis.isCentered : false;
        const bool isStderiv     = hasBasis ? m_timeShiftBasis.isStderiv : false;
        const int  nPCAFeatsP71  = hasBasis ? basisNChan * basisNComp : 0;

        // Pre-load int64 rawTs for this chunk's pts from .res.  Indexed
        // by local i_local.  Avoids the ±~7 sample float-precision error
        // (see paragraph (c) above).
        std::vector<int64_t> rawTsByLocal(static_cast<size_t>(nPts), 0);
        {
            char resPathP71[STRLEN + 16] = {0};
            snprintf(resPathP71, sizeof(resPathP71),
                     "%s.res.%d", FileBase, ElecNo);
            FILE* resFpP71 = fopen(resPathP71, "rb");
            if (resFpP71) {
                for (int i = 0; i < nPts; ++i) {
                    const int gp = pts[static_cast<size_t>(i)];
                    if (gp < 0) continue;
                    if (fseeko(resFpP71,
                               static_cast<off_t>(gp)
                                 * static_cast<off_t>(sizeof(int64_t)),
                               SEEK_SET) != 0) continue;
                    int64_t ts = 0;
                    if (fread(&ts, sizeof(int64_t), 1, resFpP71) == 1
                            && ts > 0) {
                        rawTsByLocal[static_cast<size_t>(i)] = ts;
                    }
                }
                fclose(resFpP71);
            }
        }

        // Iteration bound: collapses to 1 if we can't refresh waveBuf
        // (no .fil or no PCA basis), where iterating is pointless.
        const int alignMaxIter = (filFpP71 && hasBasis)
            ? std::clamp(ResidualPCAIter, 2, 10)
            : 1;

        // Scratch row buffers reused across iterations
        std::vector<int16_t> filRowP71(static_cast<size_t>(NbTotalChannels));
        std::vector<int16_t> waveScratchP71(static_cast<size_t>(waveSamples));

        // patch72 — verify that .fil content matches .spk before any
        // alignment work.
        //
        // Ports the verifyRawSource check from Klusters' patch69
        // (klustersdoc.cpp:4046-4188) into KKExp.  Without it, a stale
        // .fil (e.g. recompiled with different filter settings, or
        // overwritten by a later pipeline stage) would silently corrupt
        // .spk on every shifted spike during the patch71 on-the-spot
        // re-extract and again during TimeShiftFinalize's
        // RefeaturizeFromShifts re-projection.
        //
        // Algorithm: pick the first chunk-spike whose extraction window
        // fits inside the .fil, read its waveform at the spike's
        // ORIGINAL timestamp (rawTsByLocal, no m_cumShift applied yet),
        // apply the stderiv transform if .spk is in stderiv format,
        // compare to that spike's existing waveBuf slot (loaded from
        // .spk at function entry).  RMS difference exceeding 50% of the
        // .spk content's RMS indicates the two raw sources do not
        // match — abort this chunk's alignment to prevent further
        // corruption.
        //
        // Skipped when .fil is unavailable or no spike's window fits
        // inside the recording (extremely unusual; would only happen
        // if every chunk-spike sits at a file boundary).
        bool rawSourceOk = true;
        if (filFpP71) {
            bool verifyDone = false;
            for (int iV = 0; iV < nPts && !verifyDone; ++iV) {
                const int64_t tsV = rawTsByLocal[static_cast<size_t>(iV)];
                if (tsV <= 0) continue;
                const int64_t startV = tsV - PeakSampleIndex;
                if (startV < 0 ||
                    startV + nSamplesPerSpike > sessionSamplesP71 + 1) continue;
                if (fseeko(filFpP71,
                           startV * static_cast<off_t>(NbTotalChannels)
                                  * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) continue;

                bool readVOk = true;
                for (int s = 0; s < nSamplesPerSpike && readVOk; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels),
                              filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        readVOk = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!readVOk) continue;

                if (isStderiv)
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);

                // Compare to waveBuf[iV * waveSamples ..] which holds the
                // .spk contents in the same sample-major layout.
                const int16_t* refSpk = waveBuf.data()
                    + static_cast<ptrdiff_t>(iV)
                    * static_cast<ptrdiff_t>(waveSamples);
                double sumDiff2 = 0.0, sumRef2 = 0.0;
                for (int e = 0; e < waveSamples; ++e) {
                    const double dv =
                        static_cast<double>(waveScratchP71[
                            static_cast<size_t>(e)])
                      - static_cast<double>(refSpk[static_cast<size_t>(e)]);
                    sumDiff2 += dv * dv;
                    sumRef2  += static_cast<double>(refSpk[
                        static_cast<size_t>(e)])
                              * static_cast<double>(refSpk[
                        static_cast<size_t>(e)]);
                }
                const double rmsRatio = (sumRef2 > 0.0)
                    ? std::sqrt(sumDiff2 / sumRef2)
                    : 0.0;

                if (rmsRatio > 0.5) {
                    Output("[Phase 2b m3] chunk %d patch72: RAW SOURCE "
                           "MISMATCH — .fil content does not match .spk\n"
                           "  verify spike local idx %d (gp=%d, ts=%lld)\n"
                           "  RMS difference: %.1f%% of .spk content "
                           "(threshold 50%%)\n"
                           "  Likely cause: .spk was extracted from a "
                           "different raw source than the .fil currently "
                           "on disk (e.g. .fil overwritten by a later "
                           "pipeline stage, or filter settings changed).\n"
                           "  Aborting alignment for this chunk to "
                           "prevent further .spk corruption.  Re-extract "
                           "waveforms with process_extractspikes before "
                           "re-running.\n",
                           chunkIdx, iV,
                           pts[static_cast<size_t>(iV)],
                           static_cast<long long>(tsV),
                           rmsRatio * 100.0);
                    rawSourceOk = false;
                } else {
                    Output("[Phase 2b m3] chunk %d patch72: raw source "
                           "verified (verify spike %d, RMS diff %.1f%%)\n",
                           chunkIdx, iV, rmsRatio * 100.0);
                }
                verifyDone = true;
            }
            if (!verifyDone) {
                Output("[Phase 2b m3] chunk %d patch72: WARNING — could "
                       "not verify raw source (no spike's window fit "
                       "within .fil); proceeding without verification\n",
                       chunkIdx);
            }
        }

        // Skip the iteration loop entirely if raw-source verification
        // tripped.  m_cumShift values from prior phases stay untouched;
        // TimeShiftFinalize will still process them, but at least we
        // don't compound the damage by shifting from a wrong source.
        if (rawSourceOk) {

        // patch73 — refresh waveBuf for spikes with non-zero m_cumShift.
        //
        // At function entry, waveBuf was loaded via TimeShiftReadSpikeWave
        // which reads .spk verbatim (no m_cumShift applied — see the
        // comment at line 9118 about parallel-safety).  If Phase 1
        // (cross-chunk alignment) or Phase 2.5 already populated
        // m_cumShift with non-zero values, those shifts are NOT reflected
        // in waveBuf at entry.  Iter 0's cluster mean is then built from
        // un-shifted .spk content, producing a slightly-off template,
        // and the iter-0 xcorr lag is computed against a not-quite-right
        // mean.
        //
        // Spikes whose iter-0 lag is zero (already match the off-mean)
        // never trigger the on-the-spot .fil re-extract in the iter
        // loop, so their waveBuf stays in the .spk state and the
        // prior-phase shifts remain "invisible" through all iters of
        // this function.  Final disk output is still correct (finalize
        // re-extracts everyone), but per-iter mean quality suffers and
        // alignment converges to a slightly-suboptimal m_cumShift.
        //
        // Fix: at function entry into the alignment block, for every
        // chunk-spike with prior m_cumShift != 0, re-extract its
        // waveform from .fil at (rawTs + prevCum - PeakSampleIndex) and
        // overwrite waveBuf[iLocal] so iter 0's mean is built from
        // already-shifted content.  Sample-major layout matches the
        // .spk load.  Spikes that fail the bounds or read check stay
        // in their .spk state — same fallback as the iter-loop body.
        //
        // Cost: one .fil read per pre-shifted spike at function entry,
        // independent of the iter loop.  For sessions where most chunks
        // have m_cumShift == 0 (the common case before Phase 1 starts
        // operating), this is a no-op.
        if (filFpP71 && hasBasis) {
            int prerefreshed = 0;
            for (int i = 0; i < nPts; ++i) {
                const int gp = pts[static_cast<size_t>(i)];
                if (gp < 0 || gp >= static_cast<int>(m_cumShift.size()))
                    continue;
                const int prevCum = m_cumShift[static_cast<size_t>(gp)];
                if (prevCum == 0) continue;
                const int64_t tsP = rawTsByLocal[static_cast<size_t>(i)];
                if (tsP <= 0) continue;
                const int64_t offP = tsP + prevCum - PeakSampleIndex;
                if (offP < 0 ||
                    offP + nSamplesPerSpike > sessionSamplesP71 + 1)
                    continue;
                if (fseeko(filFpP71,
                           offP * static_cast<off_t>(NbTotalChannels)
                                * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) continue;
                bool okR = true;
                for (int s = 0; s < nSamplesPerSpike && okR; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels),
                              filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        okR = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!okR) continue;
                if (isStderiv)
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);
                int16_t* dstBuf = waveBuf.data()
                    + static_cast<ptrdiff_t>(i)
                    * static_cast<ptrdiff_t>(waveSamples);
                std::copy(waveScratchP71.begin(),
                          waveScratchP71.end(), dstBuf);
                ++prerefreshed;
            }
            if (prerefreshed > 0) {
                Output("[Phase 2b m3] chunk %d patch73: pre-refreshed "
                       "waveBuf for %d spikes with non-zero prior "
                       "m_cumShift before iter loop\n",
                       chunkIdx, prerefreshed);
            }
        }

        for (int iterAlign = 0; iterAlign < alignMaxIter; ++iterAlign) {
            ++alignItersRun;
            int shiftsThisIter = 0;

        for (int cid : finalIds) {
            members.clear();
            for (int i = 0; i < nPts; i++)
                if (labels[i] == cid) members.push_back(i);
            const int Nc = static_cast<int>(members.size());
            if (Nc < 5) continue;

            // Build channel-major template = int16 mean.  Use int64
            // accumulator to avoid overflow on large clusters.
            std::vector<int64_t> accCh(static_cast<size_t>(waveSamples), 0);
            for (int m : members) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(m) * waveSamples;
                for (int s = 0; s < nSamplesPerSpike; s++)
                    for (int ch = 0; ch < nChan; ch++)
                        accCh[static_cast<size_t>(ch * nSamplesPerSpike + s)]
                            += w[s * nChan + ch];
            }
            for (int e = 0; e < waveSamples; e++)
                tmpl[static_cast<size_t>(e)] = static_cast<int16_t>(
                    accCh[static_cast<size_t>(e)] / Nc);

            // patch64 — pre-align the template to PeakSampleIndex (so
            // xcorr pulls every spike toward the window centre, and any
            // two clusters that would merge converge at the same point).
            if (PeakSampleIndex >= 0 && PeakSampleIndex < nSamplesPerSpike) {
                int    meanPeakSamp = PeakSampleIndex;
                double bestAmp      = -1.0;
                for (int s = 0; s < nSamplesPerSpike; ++s) {
                    double amp = 0.0;
                    for (int ch = 0; ch < nChan; ++ch)
                        amp += std::abs(static_cast<double>(
                            accCh[static_cast<size_t>(ch * nSamplesPerSpike + s)]));
                    if (amp > bestAmp) { bestAmp = amp; meanPeakSamp = s; }
                }
                const int tmplShift = PeakSampleIndex - meanPeakSamp;
                if (tmplShift != 0) {
                    std::vector<int16_t> shifted(static_cast<size_t>(waveSamples), 0);
                    for (int ch = 0; ch < nChan; ++ch) {
                        for (int s = 0; s < nSamplesPerSpike; ++s) {
                            const int src = s - tmplShift;
                            if (src < 0 || src >= nSamplesPerSpike) continue;
                            shifted[static_cast<size_t>(ch * nSamplesPerSpike + s)] =
                                tmpl[static_cast<size_t>(ch * nSamplesPerSpike + src)];
                        }
                    }
                    tmpl = std::move(shifted);
                }
            }

            // Repack cluster's waveforms into channel-major batch
            xcWfm.assign(static_cast<size_t>(Nc) * waveSamples, 0);
            for (int sp = 0; sp < Nc; sp++) {
                const int16_t* w = waveBuf.data() +
                                   static_cast<size_t>(members[sp]) * waveSamples;
                int16_t* dst = xcWfm.data() +
                               static_cast<size_t>(sp) * waveSamples;
                for (int s = 0; s < nSamplesPerSpike; s++)
                    for (int ch = 0; ch < nChan; ch++)
                        dst[ch * nSamplesPerSpike + s] = w[s * nChan + ch];
            }

            xcShifts.assign(static_cast<size_t>(Nc), 0);
            xcScores.assign(static_cast<size_t>(Nc), 0.0f);
            const int rc = XcorrDispatch::compute(
                xcWfm.data(), tmpl.data(),
                Nc, nChan, nSamplesPerSpike,
                maxShift, minScore,
                xcShifts.data(), xcScores.data());
            (void)rc;

            // Apply each non-zero lag on the spot.
            for (int sp = 0; sp < Nc; sp++) {
                const int lag = xcShifts[static_cast<size_t>(sp)];
                if (lag == 0) {
                    if (xcScores[static_cast<size_t>(sp)] < minScore)
                        ++finalLowScore;
                    continue;
                }
                const int iLocal = members[static_cast<size_t>(sp)];
                const int gp     = pts[static_cast<size_t>(iLocal)];
                if (gp < 0 || gp >= static_cast<int>(m_cumShift.size()))
                    continue;
                const int prevCum = m_cumShift[static_cast<size_t>(gp)];
                const int newCum  = prevCum + lag;

                // Fast path when re-extract is unavailable: just bump
                // m_cumShift and let TimeShiftFinalize handle the disk
                // catch-up.  No cap-reject regardless.
                if (!filFpP71 || !hasBasis) {
                    m_cumShift[static_cast<size_t>(gp)] = newCum;
                    ++finalAligned;
                    ++totalRealigned;
                    ++shiftsThisIter;
                    continue;
                }

                // Use precise int64 rawTs from .res (no float recovery).
                const int64_t rawTsInt = rawTsByLocal[static_cast<size_t>(iLocal)];
                if (rawTsInt <= 0) {
                    // No valid .res entry — skip rather than fall back
                    // to lossy float recovery.
                    ++finalOutOfBounds;
                    continue;
                }
                const int64_t off = rawTsInt + newCum - PeakSampleIndex;
                if (off < 0 ||
                    off + nSamplesPerSpike > sessionSamplesP71 + 1) {
                    // Window off the recording — skip without committing.
                    ++finalOutOfBounds;
                    continue;
                }

                // Tentative commit; reverted on .fil read failure.
                m_cumShift[static_cast<size_t>(gp)] = newCum;

                // Read sample-major waveform from .fil.
                bool readOk = true;
                if (fseeko(filFpP71,
                           off * static_cast<off_t>(NbTotalChannels)
                               * static_cast<off_t>(sizeof(int16_t)),
                           SEEK_SET) != 0) {
                    readOk = false;
                }
                for (int s = 0; s < nSamplesPerSpike && readOk; ++s) {
                    if (fread(filRowP71.data(), sizeof(int16_t),
                              static_cast<size_t>(NbTotalChannels), filFpP71)
                        != static_cast<size_t>(NbTotalChannels)) {
                        readOk = false; break;
                    }
                    for (int c = 0; c < nChan; ++c)
                        waveScratchP71[static_cast<size_t>(s * nChan + c)] =
                            filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                }
                if (!readOk) {
                    m_cumShift[static_cast<size_t>(gp)] = prevCum;
                    ++finalOutOfBounds;
                    continue;
                }

                if (isStderiv) {
                    ApplySdiffAllpairsTemporalDiff(
                        waveScratchP71.data(), nChan, nSamplesPerSpike);
                }

                // Overwrite waveBuf so next iter's xcorr sees the
                // re-extracted waveform (sample-major, matching the
                // original TimeShiftReadSpikeWave load).
                int16_t* dstBuf = waveBuf.data() +
                                  static_cast<ptrdiff_t>(iLocal)
                                * static_cast<ptrdiff_t>(waveSamples);
                std::copy(waveScratchP71.begin(),
                          waveScratchP71.end(), dstBuf);

                // Re-project through the canonical (cand=N) PCA basis
                // and update Data[gp] features in place.  Eigenvector
                // indexing matches RefeaturizeFromShifts:
                //     ev[k * data2use + s]   (col-major)
                {
                    float* dataRow = Data.m_Data + gp * nDims;
                    const auto& meanCanon = m_timeShiftBasis.meanShifted[
                        static_cast<size_t>(candCanonical)];
                    const auto& evecCanon = m_timeShiftBasis.eigvecShifted[
                        static_cast<size_t>(candCanonical)];
                    for (int ch = 0; ch < basisNChan; ++ch) {
                        const auto& mu = meanCanon[static_cast<size_t>(ch)];
                        const auto& ev = evecCanon[static_cast<size_t>(ch)];
                        for (int k = 0; k < basisNComp; ++k) {
                            double val = 0.0;
                            for (int s = 0; s < basisData2use; ++s) {
                                const int sIdx = basisRecShift + s;
                                double xraw = static_cast<double>(
                                    waveScratchP71[static_cast<size_t>(
                                        sIdx * nChan + ch)]);
                                if (basisCentered)
                                    xraw -= mu[static_cast<size_t>(s)];
                                val += ev[static_cast<size_t>(
                                    k * basisData2use + s)] * xraw;
                            }
                            const int featIdx = ch * basisNComp + k;
                            if (featIdx < nPCAFeatsP71 &&
                                featIdx < static_cast<int>(dimMin_.size())) {
                                dataRow[featIdx] =
                                    (static_cast<float>(val) - dimMin_[featIdx])
                                    * dimRange_[featIdx];
                            }
                        }
                    }
                    if (sessionSamplesF > 0.0f) {
                        dataRow[timeDimIdxP71] =
                            (static_cast<float>(rawTsInt + newCum) - timeRawMin)
                            / sessionSamplesF;
                    }
                }

                ++finalAligned;
                ++totalRealigned;
                ++shiftsThisIter;
            }
        }   // end cluster loop

            // Converged: nobody moved this iter (every remaining shift
            // is below minScore or out-of-bounds).  Stop early.
            if (shiftsThisIter == 0) break;
        }   // end iter loop

        // patch83 — PCA-centering refine pass (opt-in via AlignPcaCenter)
        //
        // After the xcorr alignment loop converges, optionally run a
        // second pass that refines each spike's position to MINIMIZE
        // its distance to the cluster's mean PCA position:
        //
        //     dist²(s) = sum_f (pca_f(spike at shift s) − μ_pca[f])²
        //
        // where μ_pca[f] is the mean of feature f across the cluster's
        // current members and pca_f comes from projecting the freshly
        // re-extracted waveform through the canonical .pca basis with
        // the same normalisation Data[] uses (dimMin_/dimRange_).
        //
        // This is the KKExp analogue of klusters' patch82 PCA-energy
        // refine pass, but with the energy criterion (||pca||²) swapped
        // for the centering criterion (||pca − μ_pca_cluster||²).  The
        // centering version directly tightens cluster compactness in
        // PCA space — exactly the geometric property that downstream
        // EM / merge decisions key off.  Energy is useful when there
        // is no cluster mean to compare against (e.g. the klusters
        // single-cluster refine path); here we have full cluster
        // membership from the converged iter loop, so centering is
        // the natural choice.
        //
        // Cost: per qualifying spike, (2·m_timeShiftMaxAbs + 1) .fil
        // reads + PCA projections.  For typical maxShift=3, that's
        // 7 reads/projections per spike — well under a second per
        // chunk on a warm cache.
        //
        // Falls back silently (logs an info line, runs no refine) when:
        //   - filFpP71 is null (no .fil available)
        //   - hasBasis is false (no canonical PCA basis loaded)
        //   - AlignPcaCenter is 0 (the default)
        if (AlignPcaCenter && filFpP71 && hasBasis) {
            int p83Refined  = 0;
            int p83Skipped  = 0;
            int p83Examined = 0;
            for (int cid : finalIds) {
                members.clear();
                for (int i = 0; i < nPts; i++)
                    if (labels[i] == cid) members.push_back(i);
                const int Nc = static_cast<int>(members.size());
                if (Nc < 3) continue;

                // Compute cluster mean PCA position μ_pca over the kept
                // PCA feature columns.  Read straight from Data[] — it
                // was kept in sync by the iter loop's on-the-spot
                // re-featurize step.
                std::vector<double> muPca(
                    static_cast<size_t>(nPCAFeatsP71), 0.0);
                int clusterMembersWithGp = 0;
                for (int m : members) {
                    const int gp = pts[static_cast<size_t>(m)];
                    if (gp < 0 || gp >=
                        static_cast<int>(m_cumShift.size())) continue;
                    const float* row = Data.m_Data + gp * nDims;
                    for (int f = 0; f < nPCAFeatsP71; ++f)
                        muPca[static_cast<size_t>(f)] +=
                            static_cast<double>(row[f]);
                    ++clusterMembersWithGp;
                }
                if (clusterMembersWithGp < 3) continue;
                const double invN = 1.0 / clusterMembersWithGp;
                for (int f = 0; f < nPCAFeatsP71; ++f)
                    muPca[static_cast<size_t>(f)] *= invN;

                // Per-spike: sweep candidate shifts, pick the one that
                // minimizes distance² to μ_pca.
                for (int sp = 0; sp < Nc; ++sp) {
                    const int iLocal = members[static_cast<size_t>(sp)];
                    const int gp     = pts[static_cast<size_t>(iLocal)];
                    if (gp < 0 || gp >=
                        static_cast<int>(m_cumShift.size())) continue;

                    const int     prevCum   = m_cumShift[static_cast<size_t>(gp)];
                    const int64_t rawTsInt  = rawTsByLocal[static_cast<size_t>(iLocal)];
                    if (rawTsInt <= 0) continue;
                    ++p83Examined;

                    int    bestS    = 0;
                    double bestDist = std::numeric_limits<double>::infinity();
                    int    nValid   = 0;

                    for (int s = -m_timeShiftMaxAbs;
                             s <=  m_timeShiftMaxAbs; ++s) {
                        const int     candCum = prevCum + s;
                        const int64_t off     = rawTsInt + candCum - PeakSampleIndex;
                        if (off < 0 ||
                            off + nSamplesPerSpike > sessionSamplesP71 + 1)
                            continue;

                        if (fseeko(filFpP71,
                                   off * static_cast<off_t>(NbTotalChannels)
                                       * static_cast<off_t>(sizeof(int16_t)),
                                   SEEK_SET) != 0) continue;

                        bool readOk = true;
                        for (int t = 0; t < nSamplesPerSpike && readOk; ++t) {
                            if (fread(filRowP71.data(), sizeof(int16_t),
                                      static_cast<size_t>(NbTotalChannels),
                                      filFpP71)
                                != static_cast<size_t>(NbTotalChannels)) {
                                readOk = false; break;
                            }
                            for (int c = 0; c < nChan; ++c)
                                waveScratchP71[static_cast<size_t>(t * nChan + c)] =
                                    filRowP71[static_cast<size_t>(GroupChannelIds[c])];
                        }
                        if (!readOk) continue;

                        if (isStderiv)
                            ApplySdiffAllpairsTemporalDiff(
                                waveScratchP71.data(), nChan, nSamplesPerSpike);

                        // PCA projection.  Mirrors the per-spike code in
                        // the iter loop body — same basis access, same
                        // dimMin_/dimRange_ normalisation.  Distance² is
                        // accumulated against muPca[featIdx] in the same
                        // normalised feature space.
                        double dist2 = 0.0;
                        const auto& meanCanon = m_timeShiftBasis.meanShifted[
                            static_cast<size_t>(candCanonical)];
                        const auto& evecCanon = m_timeShiftBasis.eigvecShifted[
                            static_cast<size_t>(candCanonical)];
                        for (int ch = 0; ch < basisNChan; ++ch) {
                            const auto& mu = meanCanon[static_cast<size_t>(ch)];
                            const auto& ev = evecCanon[static_cast<size_t>(ch)];
                            for (int k = 0; k < basisNComp; ++k) {
                                double val = 0.0;
                                for (int u = 0; u < basisData2use; ++u) {
                                    const int sIdx = basisRecShift + u;
                                    double xraw = static_cast<double>(
                                        waveScratchP71[static_cast<size_t>(
                                            sIdx * nChan + ch)]);
                                    if (basisCentered)
                                        xraw -= mu[static_cast<size_t>(u)];
                                    val += ev[static_cast<size_t>(
                                        k * basisData2use + u)] * xraw;
                                }
                                const int featIdx = ch * basisNComp + k;
                                if (featIdx >= nPCAFeatsP71 ||
                                    featIdx >= static_cast<int>(dimMin_.size()))
                                    continue;
                                const double normVal =
                                    (static_cast<float>(val) - dimMin_[featIdx])
                                  * dimRange_[featIdx];
                                const double diff = normVal
                                  - muPca[static_cast<size_t>(featIdx)];
                                dist2 += diff * diff;
                            }
                        }

                        ++nValid;
                        if (dist2 < bestDist) {
                            bestDist = dist2;
                            bestS    = s;
                        }
                    }   // end candidate-shift loop

                    if (nValid == 0) { ++p83Skipped; continue; }
                    if (bestS == 0) continue;   // already at the centre

                    m_cumShift[static_cast<size_t>(gp)] = prevCum + bestS;
                    ++p83Refined;
                }
            }   // end cluster loop

            Output("[Phase 2b m3] chunk %d patch83 PCA-centering: "
                   "examined %d, refined %d, skipped %d (off-edge)\n",
                   chunkIdx, p83Examined, p83Refined, p83Skipped);
        } else if (AlignPcaCenter) {
            Output("[Phase 2b m3] chunk %d patch83: AlignPcaCenter "
                   "requested but %s%s%s — skipping.\n",
                   chunkIdx,
                   !filFpP71 ? ".fil unavailable" : "",
                   (!filFpP71 && !hasBasis) ? " and " : "",
                   !hasBasis ? "PCA basis not loaded" : "");
        }

        }   // end if (rawSourceOk) — patch72

        // patch74 — post-alignment .res monotonicity check.
        //
        // After cumulative shifts are applied, two adjacent spikes whose
        // original detection timestamps were close together may end up
        // with crossed (newTs1 > newTs2 where origTs1 < origTs2).
        // Klusters explicitly handles this case (klustersdoc.cpp lines
        // 3922-3938) by sorting spikes by their new timestamps before
        // writing .res, but KKExp leaves spikes in original detection
        // order — TimeShiftFinalize writes them at their original .res
        // slots with only the shifted-timestamp value updated.
        //
        // Non-monotonic .res is technically valid (most consumers index
        // by spike number, not by sample-time), but several tools assume
        // .res is sorted (process_smrconvert, some plotting helpers,
        // grep-by-time analyses).  And a non-monotonic .res signals
        // something has likely gone wrong with the alignment — large
        // cross-detection shifts that the alignment shouldn't be
        // producing under normal operation.
        //
        // This check scans this chunk's pts in their original gp order,
        // computes the effective post-shift timestamp for each
        // (rawTs + m_cumShift), and warns when adjacent pairs cross.
        // It does NOT modify .res or m_cumShift — the audit purpose is
        // visibility, not correction.  If crossings are detected and
        // matter, the user can decide whether to: (a) tighten
        // m_timeShiftMaxAbs to prevent them, (b) port Klusters' sort
        // step into TimeShiftFinalize, or (c) accept them.
        {
            int crossings = 0;
            int64_t maxCross = 0;
            int firstCrossGp1 = -1, firstCrossGp2 = -1;
            for (int i = 1; i < nPts; ++i) {
                const int gpPrev = pts[static_cast<size_t>(i - 1)];
                const int gpCur  = pts[static_cast<size_t>(i)];
                if (gpPrev < 0 || gpCur < 0) continue;
                if (gpPrev >= static_cast<int>(m_cumShift.size()) ||
                    gpCur  >= static_cast<int>(m_cumShift.size())) continue;
                const int64_t tsPrev = rawTsByLocal[
                    static_cast<size_t>(i - 1)];
                const int64_t tsCur  = rawTsByLocal[
                    static_cast<size_t>(i)];
                if (tsPrev <= 0 || tsCur <= 0) continue;
                const int64_t effPrev = tsPrev
                    + m_cumShift[static_cast<size_t>(gpPrev)];
                const int64_t effCur  = tsCur
                    + m_cumShift[static_cast<size_t>(gpCur)];
                // Original order: tsPrev <= tsCur (assuming .res was
                // monotonic to start with — true for typical sessions).
                // Crossing: post-shift, effPrev > effCur.
                if (effPrev > effCur) {
                    if (crossings == 0) {
                        firstCrossGp1 = gpPrev;
                        firstCrossGp2 = gpCur;
                    }
                    const int64_t gap = effPrev - effCur;
                    if (gap > maxCross) maxCross = gap;
                    ++crossings;
                }
            }
            if (crossings > 0) {
                Output("[Phase 2b m3] chunk %d patch74: WARNING — "
                       "%d adjacent-spike timestamp crossings detected "
                       "after alignment (max gap %lld samples, "
                       "first at gp %d → gp %d).  .res will be written "
                       "non-monotonic by TimeShiftFinalize.  Tools that "
                       "assume .res is sorted may misbehave; consider "
                       "lowering m_timeShiftMaxAbs or porting Klusters' "
                       "sortedOrder+targetPos permutation (klustersdoc."
                       "cpp:3922-3938) into TimeShiftFinalize.\n",
                       chunkIdx, crossings,
                       static_cast<long long>(maxCross),
                       firstCrossGp1, firstCrossGp2);
            }
        }

        if (filFpP71) fclose(filFpP71);
    }

    // Write final labels back into the sub-KK and refresh its bookkeeping.
    for (int i = 0; i < nPts; i++) {
        const int v = labels[i];
        Ks.Class[i] = (v >= 0 && v < MaxPossibleClusters) ? v : 0;
    }
    for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
    for (int i = 0; i < nPts; i++) {
        const int c = Ks.Class[i];
        if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
    }
    Ks.Reindex();
    Ks.MStep();

    // The outer Phase 9 (TimeShiftFinalize) refeaturizes globally; we
    // don't call RefeaturizeFromShifts here because the parallel chunk
    // loop would race on Data[] writes if multiple chunks tried.
    //
    // Per-chunk final-alignment summary — bookend to the chunk-START
    // banner so each chunk has a paired START/ALIGN/END trio in the log.
    Output("[Phase 2b m3] chunk %d ALIGN: %d spike realigns (xcorr >= %.2f); "
           "skipped: %d low-score, %d out-of-bounds; ran %d align iters\n",
           chunkIdx, finalAligned,
           static_cast<double>(ResidualPCAMinScore),
           finalLowScore, finalOutOfBounds, alignItersRun);
    Output("[Phase 2b m3] chunk %d END: %d total splits, %d total realigns, "
           "ran %d/%d iters\n",
           chunkIdx, totalSplitsApplied, totalRealigned,
           actualItersRun, maxIter);
}


void KK::ChunkReCEMPerChunk(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nFullDims)
{
    const int nCh = static_cast<int>(chunkPoints.size());
    if (nCh == 0) return;

    const char* p2bDesc = nullptr;
    switch (Phase2bMode) {
        case 1:  p2bDesc = "Variational-Bayes GMM, warm-start from Phase-2a labels"; break;
        case 2:  p2bDesc = "warm-start CEM with splits -> VB-GMM auto-prune"; break;
        case 3:  p2bDesc = "residual-PCA split iterations + post-loop normalised-xcorr realign (Klusters)"; break;
        default: p2bDesc = "warm-start from Phase-2a labels (CEM)";  break;
    }
    LockedStderr( "[Phase 2b] Chunk re-CEM (%s)\n", p2bDesc);

    // ── LPT scheduling: sort chunks by descending expected work ─────────
    // Without this sort, with schedule(dynamic), heterogeneously-sized
    // chunks leave straggler-tail behaviour: smaller chunks finish quickly
    // and the largest chunk (most cluster-spikes after Phase 2a's splits)
    // runs alone on one core for the remainder of the phase.  Phase 2a
    // has the same machinery; Phase 2b was missed.
    //
    // Expected work ~ O(K · N · D²) per CEM iter × max-iter cap.  With N
    // ~ uniform across chunks and D constant, the dominant variable is K
    // = number of distinct alive clusters from Phase 2a.  We sort by
    // K·N descending so the heaviest chunk gets a worker thread while
    // the pool is still full.
    std::vector<int> chunkOrder(nCh);
    {
        std::vector<int> chunkK(nCh, 0);
        for (int ck = 0; ck < nCh; ck++) {
            const auto& cls = perChunkClass[static_cast<size_t>(ck)];
            std::unordered_set<int> uniq(cls.begin(), cls.end());
            chunkK[static_cast<size_t>(ck)] = static_cast<int>(uniq.size());
            chunkOrder[static_cast<size_t>(ck)] = ck;
        }
        std::sort(chunkOrder.begin(), chunkOrder.end(),
                  [&](int a, int b) {
                      const long long workA =
                          static_cast<long long>(chunkK[static_cast<size_t>(a)]) *
                          static_cast<long long>(chunkPoints[static_cast<size_t>(a)].size());
                      const long long workB =
                          static_cast<long long>(chunkK[static_cast<size_t>(b)]) *
                          static_cast<long long>(chunkPoints[static_cast<size_t>(b)].size());
                      return workA > workB;
                  });
        if (Verbose >= 1) {
            int kMax = 0, kMin = INT_MAX;
            size_t nMax = 0, nMin = SIZE_MAX;
            for (int ck = 0; ck < nCh; ck++) {
                kMax = std::max(kMax, chunkK[static_cast<size_t>(ck)]);
                kMin = std::min(kMin, chunkK[static_cast<size_t>(ck)]);
                nMax = std::max(nMax, chunkPoints[static_cast<size_t>(ck)].size());
                nMin = std::min(nMin, chunkPoints[static_cast<size_t>(ck)].size());
            }
            LockedStderr(
                    "[Phase 2b] LPT order: nChunks=%d, K range %d-%d, "
                    "N range %zu-%zu (heaviest first)\n",
                    nCh, kMin, kMax, nMin, nMax);
        }
    }

    struct ChunkResult {
        bool                    changed = false;
        int                     deltaClusters = 0;
        std::vector<int>        newClass;
        std::vector<ChunkModel> newModels;
    };
    std::vector<ChunkResult> results(nCh);

    int totalChunksProcessed = 0;
    int totalDeltaClusters   = 0;

    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalChunksProcessed,totalDeltaClusters)
    for (int oi = 0; oi < nCh; oi++) {
        const int ck = chunkOrder[static_cast<size_t>(oi)];
        const auto& pts = chunkPoints[static_cast<size_t>(ck)];
        const int   nPts = static_cast<int>(pts.size());
        if (nPts == 0) continue;

        const auto& cls0 = perChunkClass[static_cast<size_t>(ck)];
        if (static_cast<int>(cls0.size()) != nPts) continue;

        // Skip chunks with ≤ 2 distinct labels (noise + at most 1 cluster).
        std::unordered_set<int> uniqueLcs(cls0.begin(), cls0.end());
        if (uniqueLcs.size() <= 2) continue;

        const int aliveBefore = static_cast<int>(uniqueLcs.size());

        // Build sub-KK warm-started from cls0.
        //
        // SPATIAL-ONLY: time excluded.  Same rationale as Phase 2a — the
        // standalone Klusters recluster path runs CEMTwoPhase which is
        // spatial-only in Phase 1, so chunk re-CEM should match that for
        // bimodality discovery.  Time as a feature within a single chunk
        // is weak signal (per-cluster time variance ≈ chunk duration is
        // large) and its parameter cost in BIC inflates the rejection
        // bar for marginal spatial splits.  Stripping time at copy-in
        // gives consistent stride throughout.
        const int nSpatialDimsFull = (nFullDims > 1) ? nFullDims - 1 : nFullDims;

        // ── Per-chunk feature selection ──────────────────────────
        // Same mechanism as Phase 2a (see comment there).  Phase 2b
        // operates on the chunk's full spike set rather than one
        // cluster's spikes, so within-chunk variance ranks features
        // by overall spread across all units in the chunk — features
        // along which the chunk's spike cloud is fattest.  Those are
        // the directions where multi-unit structure can hide; CEM
        // discovers it.
        int  nSubDims;
        std::vector<int> selFeat;

        if (SubspaceDims > 0 && SubspaceDims < nSpatialDimsFull) {
            std::vector<double> sum(nSpatialDimsFull, 0.0);
            std::vector<double> sqsum(nSpatialDimsFull, 0.0);
            for (int i = 0; i < nPts; i++) {
                const int p = pts[static_cast<size_t>(i)];
                for (int d = 0; d < nSpatialDimsFull; d++) {
                    const double v = Data[static_cast<size_t>(p) * nFullDims + d];
                    sum[d]   += v;
                    sqsum[d] += v * v;
                }
            }
            std::vector<std::pair<double,int>> rank(nSpatialDimsFull);
            const double invN = 1.0 / nPts;
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
        Ks.nPoints            = nPts;
        Ks.penaltyMix         = penaltyMix;
        Ks.suppressBestSave   = true;
        Ks.minClustersAlive   = 1;
        Ks.NoisePoint         = 0;

        Ks.AllocateArrays();
        Ks.AllocateCholeskyVecs();
        Ks.ReinitForSplit(nPts, nSubDims, penaltyMix);

        // Pack data using selected features only.
        for (int i = 0; i < nPts; i++) {
            const int p = pts[static_cast<size_t>(i)];
            for (int k = 0; k < nSubDims; k++) {
                const int d = selFeat[static_cast<size_t>(k)];
                Ks.Data[static_cast<size_t>(i) * nSubDims + k] =
                    Data[static_cast<size_t>(p) * nFullDims + d];
            }
        }
        Ks.timeRawMin = timeRawMin;
        Ks.timeRawMax = timeRawMax;

        // Phase 2b WARM-STARTS from cls0 (Phase 2a's labels).  Rationale:
        // with patch11 feature selection in Phase 2a, structure discovery
        // happens at 2a in a low-dim subspace where BIC permits marginal
        // splits.  Phase 2b's job is then to REFINE those labels at the
        // chunk level (or, with -SubspaceDims > 0, in 2b's chunk-wide
        // subspace) — not to re-explore from scratch.  Warm-start
        // converges fast and preserves 2a's discoveries; ConsiderDeletion
        // and (optionally) TrySplits adjust K from there.
        //
        // Map cls0's possibly-sparse cluster IDs ({0, 5, 7, 12, ...}) to
        // a contiguous range ({0, 1, 2, 3, ...}) for the sub-KK.  Cluster
        // 0 stays as noise; non-noise IDs get sequential mapping in the
        // order they appear in uniqueLcs.
        int nReal = 0;
        for (int c : uniqueLcs) if (c > 0) nReal++;
        if (nReal < 2) continue;  // not enough structure to refine

        std::unordered_map<int,int> lcToK;
        lcToK.reserve(uniqueLcs.size());
        lcToK[0] = 0;  // noise stays as noise
        int nextK = 1;
        for (int c : uniqueLcs) {
            if (c <= 0) continue;
            lcToK[c] = nextK++;
        }

        const int nStart = nReal + 1;  // +1 for noise (cluster 0)
        Ks.nStartingClusters = nStart;
        for (int c = 0; c < MaxPossibleClusters; c++)
            Ks.ClassAlive[c] = (c < nStart) ? 1 : 0;
        for (int i = 0; i < nPts; i++) {
            const auto it = lcToK.find(cls0[static_cast<size_t>(i)]);
            Ks.Class[i] = (it != lcToK.end()) ? it->second : 0;
        }
        Ks.Reindex();

        if (Phase2bMode == 1) {
            // ── Phase 2b mode 1: Variational-Bayes GMM (patch16) ──────────
            //
            // Replace CEM warm-start + RunEMLoop with VB-GMM iterations on
            // the same warm-start labels.  VB-GMM's Bayesian K-pruning via
            // the Dirichlet prior replaces ConsiderDeletion's BIC-driven
            // pruning; soft posteriors give boundary spikes graceful
            // assignment instead of hard commitment.
            //
            // VB output is hard-assigned (argmax of converged posteriors)
            // so the rest of the pipeline interfaces unchanged.  Then call
            // MStep to repopulate Ks.Mean / Ks.Cov from the hard labels —
            // ChunkModel rebuild reads from those.
            std::vector<int> labelsBuf(static_cast<size_t>(nPts));
            for (int i = 0; i < nPts; i++) labelsBuf[i] = Ks.Class[i];

            const int nSurvivors = RunVBGMM(
                Ks.Data.m_Data, labelsBuf.data(),
                /*K_init=*/ nStart,
                /*N=*/      nPts,
                /*D=*/      nSubDims,
                /*maxIter=*/VBGMMMaxIter,
                /*convTol=*/static_cast<double>(VBGMMConvTol));
            (void)nSurvivors;

            // Write back labels.  VB may have left some k indices unused
            // (pruned); ChunkModel rebuild's nMembers count handles the
            // gaps.  Reindex compacts alive-cluster IDs.
            for (int i = 0; i < nPts; i++) Ks.Class[i] = labelsBuf[i];
            // Refresh ClassAlive from the labels actually present
            for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
            for (int i = 0; i < nPts; i++) {
                const int c = Ks.Class[i];
                if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
            }
            Ks.Reindex();
            Ks.MStep();   // populate Mean/Cov for ChunkModel rebuild below
        } else if (Phase2bMode == 2) {
            // ── Phase 2b mode 2: preseed -> split -> VB-GMM ───────────────
            //
            // Three-stage hybrid that exercises both directions of K change:
            //
            //   1. Preseed: warm-start from Phase-2a labels (already set
            //      up above; Ks.Class[] populated, Ks.ClassAlive[] set).
            //
            //   2. Split: run CEM with TrySplits enabled.  Grows K wherever
            //      BIC supports a split — including reorganisations within
            //      Phase 2a's clusters that 2a's per-cluster CEM didn't
            //      explore (because 2a operates on each cluster in
            //      isolation).  ConsiderDeletion may also fire here.
            //
            //   3. Prune: hand the (possibly expanded) label set to VB-GMM.
            //      The Dirichlet prior consolidates clusters that CEM's
            //      TrySplits accepted on marginal BIC evidence, when the
            //      data don't support the split under the Bayesian
            //      criterion.
            //
            // Net effect: K can both grow (via CEM in step 2) AND shrink
            // (via VB in step 3) within a single Phase 2b pass, whereas
            // mode 0 only shrinks via deletion and mode 1 only refines
            // around Phase 2a's existing K.
            //
            // ── Stage 2: CEM with splits ────────────────────────────────
            Ks.MStep();
            Ks.EStep();
            Ks.RunEMLoop(/*enableSplits=*/  true,
                         /*enableDistDump=*/ false,
                         /*maxIter=*/        0,
                         /*phaseLabel=*/     "[2b-split]");

            // ── Stage 3: VB-GMM auto-prune on post-CEM labels ───────────
            // K_init = max-alive-index + 1; fresh scan of Class[] avoids
            // having to track through Reindex / TrySplits internals.
            int K_init = 1;
            for (int i = 0; i < nPts; i++) {
                const int c = Ks.Class[i];
                if (c >= K_init) K_init = c + 1;
            }
            std::vector<int> labelsBuf(static_cast<size_t>(nPts));
            for (int i = 0; i < nPts; i++) labelsBuf[i] = Ks.Class[i];

            const int nSurvivors = RunVBGMM(
                Ks.Data.m_Data, labelsBuf.data(),
                /*K_init=*/ K_init,
                /*N=*/      nPts,
                /*D=*/      nSubDims,
                /*maxIter=*/VBGMMMaxIter,
                /*convTol=*/static_cast<double>(VBGMMConvTol));
            (void)nSurvivors;

            for (int i = 0; i < nPts; i++) Ks.Class[i] = labelsBuf[i];
            for (int c = 0; c < MaxPossibleClusters; c++) Ks.ClassAlive[c] = 0;
            for (int i = 0; i < nPts; i++) {
                const int c = Ks.Class[i];
                if (c >= 0 && c < MaxPossibleClusters) Ks.ClassAlive[c] = 1;
            }
            Ks.Reindex();
            Ks.MStep();
        } else if (Phase2bMode == 3) {
            // ── Phase 2b mode 3: residual-PCA iterative refinement ────────
            //
            // Per-chunk loop that addresses two failure modes mode 1/2 don't:
            //
            //   (i)  Sub-structure hiding INSIDE an existing cluster — two
            //        neurons whose mean waveforms are similar (so the
            //        cluster passes initial CEM/VBGMM) but whose RESIDUAL
            //        waveforms differ.  Subtracting each cluster's mean and
            //        re-PCA'ing the residual exposes that difference;
            //        running VBGMM on the residual features splits the
            //        cluster.
            //
            //   (ii) Within-cluster temporal jitter — spikes from the same
            //        neuron whose .spk windows are misaligned by 1-3 samples
            //        (Phase 1a's cluster-mean Mahal alignment can leave
            //        residual jitter on the dominant channels because Mahal
            //        is dominated by the high-variance directions, not the
            //        peak channels).  Per-spike cross-correlation against
            //        the cluster mean on the 1-2 DOMINANT channels — the
            //        channels where the neuron is strongest, hence cleanest
            //        SNR — tightens the alignment.
            //
            // Loop body (up to ResidualPCAIter iterations, default 3):
            //   1. VBGMM on current chunk features → labels
            //   2. Read raw spike waveforms from .spk (or .spkD)
            //   3. For each alive cluster:
            //        a. Compute mean waveform (per-channel)
            //        b. Compute residuals = spike − mean
            //        c. Eigendecompose residual covariance → top-K eigenvecs
            //        d. Project residuals → K-dim sub-features
            //        e. Run VBGMM on sub-features; if K > 1 survivors,
            //           SPLIT: keep majority as original ID, new IDs
            //           assigned to the rest.
            //   4. Per-spike xcorr realignment on 1-2 dominant channels
            //      → updates the OUTER KK's m_cumShift[p] for each chunk
            //      member.
            //   5. Convergence: stop if label-change fraction < ConvTol.
            //
            // After the loop, refeaturize-from-.fil happens at the outer
            // scope (driver issues a global RefeaturizeFromShifts after
            // all chunks complete).  Sub-cluster IDs above MaxPossibleClusters
            // get clamped back to the original cluster (no-split) to keep
            // the per-chunk cluster space bounded.
            //
            // Reads the ORIGINAL .spk waveforms via TimeShiftReadSpikeWave
            // (mmap path is thread-safe; FILE* path is serialised on a
            // critical section, which is the existing pattern).  Writes to
            // m_cumShift use disjoint per-chunk indices (the parallel chunks
            // never share spike indices), so the parallel-for stays safe.
            //
            // Hyperparameters via CLI flags (defaults in KlustaKwik.cpp):
            //   ResidualPCAIter             — max outer iterations (3)
            //   ResidualPCAComponents       — top-K residual eigenvecs (3)
            //   ResidualPCASubK             — K_init for residual VBGMM (4)
            //   ResidualPCADominantChannels — 1 or 2 (2)
            //   ResidualPCAConvTol          — convergence threshold (0.01)
            this->RunPhase2bMode3Chunk(Ks, pts, NbChannels,
                                       NbSamplesPerSpike, nStart, ck);
        } else {
            // ── Phase 2b mode 0: warm-start CEM (patch12 default) ─────────
            // Initial MStep + EStep so RunEMLoop has consistent stats, then
            // run normal CEM with splits enabled.  Warm-start means MStep
            // sees Phase 2a's clusters intact; subsequent EStep refines the
            // assignments; TrySplits and ConsiderDeletion adjust K only if
            // BIC strictly prefers the change.
            //
            // Two flag-controlled bounds prevent the per-chunk runtime from
            // exploding on K-heavy chunks (K = 122 was reported with
            // -KnnSplitMinNewClusterSize 50 in user testing):
            //
            //   -Phase2bEnableSplits 0 (default)
            //     Disable TrySplits inside the inner CEM.  Phase 2a's
            //     per-cluster CEM has already done split discovery; the
            //     244·K inner-CEM cost of repeating it here dominates the
            //     phase runtime for chunks with K > ~50.  ConsiderDeletion
            //     remains active (essential for merging Phase 2a's over-
            //     splits via warm-start CEM at the chunk level).
            //
            //   -Phase2bMaxIter 60 (default)
            //     Cap inner CEM iterations.  Warm-start CEM converges in
            //     10-30 iter typically; the cap bounds straggler chunks
            //     where TrySplits/ConsiderDeletion oscillate around a
            //     marginal BIC boundary and the convergence predicate
            //     stays true forever.
            const bool enableSplits =
                (Phase2bEnableSplits != 0);
            const int  innerMaxIter =
                (Phase2bMaxIter > 0) ? Phase2bMaxIter : 0;  // 0 → use MaxIter
            Ks.MStep();
            Ks.EStep();
            Ks.RunEMLoop(enableSplits,
                         /*enableDistDump=*/ false,
                         /*maxIter=*/        innerMaxIter,
                         /*phaseLabel=*/     "[2b]");
        }

        // Read back labels
        std::vector<int> newCls(static_cast<size_t>(nPts));
        for (int i = 0; i < nPts; i++) newCls[static_cast<size_t>(i)] = Ks.Class[i];

        // Refresh Mean/Cov for ChunkModel build
        Ks.MStep();

        // Rebuild ChunkModel list from Ks.Mean / Ks.Cov.
        //
        // Ks operated on `nSubDims` selected features (could be < the
        // full spatial dim count when -SubspaceDims > 0).  Map each
        // Ks-internal index k back to its original spatial slot via
        // selFeat[k], so cm.mean/cov are indexed by the canonical
        // nFullDims layout regardless of which features Ks used.
        //
        // selFeat is ascending-sorted, so selFeat[r] < selFeat[col]
        // when r < col, which preserves the upper-triangle schema.
        // Unused spatial dims (and the time row/col) stay zero.
        std::vector<ChunkModel> newMdls;
        newMdls.reserve(static_cast<size_t>(Ks.nClustersAlive));
        for (int cc = 0; cc < Ks.nClustersAlive; cc++) {
            const int c = Ks.AliveIndex[cc];
            ChunkModel cm;
            cm.chunkIdx        = ck;
            cm.localClusterId  = c;
            cm.globalClusterId = -1;
            cm.nMembers        = 0;
            cm.mean.assign(static_cast<size_t>(nFullDims), 0.0f);
            cm.cov.assign (static_cast<size_t>(nFullDims) * nFullDims, 0.0f);
            for (int k = 0; k < nSubDims; k++) {
                const int d = selFeat[static_cast<size_t>(k)];
                cm.mean[static_cast<size_t>(d)] =
                    Ks.Mean[static_cast<size_t>(c) * nSubDims + k];
            }
            for (int r = 0; r < nSubDims; r++) {
                const int rd = selFeat[static_cast<size_t>(r)];
                for (int col = r; col < nSubDims; col++) {
                    const int cd = selFeat[static_cast<size_t>(col)];
                    cm.cov[static_cast<size_t>(rd) * nFullDims + cd] =
                        Ks.Cov[static_cast<size_t>(c) * Ks.nDims2
                              + r * nSubDims + col];
                }
            }
            for (int i = 0; i < nPts; i++)
                if (Ks.Class[i] == c) cm.nMembers++;
            if (cm.nMembers == 0) continue;
            newMdls.push_back(std::move(cm));
        }

        results[static_cast<size_t>(ck)].changed       = true;
        results[static_cast<size_t>(ck)].deltaClusters = Ks.nClustersAlive - aliveBefore;
        results[static_cast<size_t>(ck)].newClass      = std::move(newCls);
        results[static_cast<size_t>(ck)].newModels     = std::move(newMdls);

        totalChunksProcessed += 1;
        totalDeltaClusters   += (Ks.nClustersAlive - aliveBefore);
    }

    // Serial application
    for (int ck = 0; ck < nCh; ck++) {
        if (!results[static_cast<size_t>(ck)].changed) continue;
        perChunkClass [static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newClass);
        perChunkModels[static_cast<size_t>(ck)] =
            std::move(results[static_cast<size_t>(ck)].newModels);
    }

    LockedStderr(
            "[Phase 2b] Chunk re-CEM: %d chunks processed, net cluster delta = %+d\n",
            totalChunksProcessed, totalDeltaClusters);
}
