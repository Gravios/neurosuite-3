/***************************************************************************
                   KK_templatematch.cpp
                   --------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Within-chunk template-matching cluster recovery, split out of KK.cpp.
 Implements KK::WithinChunkTemplateMatch and
 KK::WithinChunkTemplateMatchMedianKnn plus their file-local helpers
 (power-iteration top eigenvalue, Hann-window taper).  These are member
 definitions of the KK class declared in KK.h; partitioning them into their
 own translation unit changes no interface and no behaviour.
 ***************************************************************************/
#include "KK.h"
#include "KlustaKwik.h"
#include "realign_xcorr.h"     // XcorrDispatch::compute
#include "klusters_realign.h"  // KlustersRealign::RealignStats

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <omp.h>



// ---------------------------------------------------------------------------
// KK::SubspaceReclusterPerChunk
//
// Per-chunk subspace reclustering — Phase 2 of the runtime pipeline.
// Runs after Phase 1 (per-chunk CEM) on the per-chunk model lists,
// before Phase 4 (mean-waveform harvest) and Phase 6 (cross-chunk
// model matching).
//
// For each local cluster in each chunk, projects its spikes (by global index)
// into the top-subspaceDims eigenvector subspace of that cluster's covariance,
// then runs CEMTwoPhase in that reduced space.  Splits are accepted if the
// multi-cluster BIC score beats the single-cluster null.  perChunkClass[] and
// perChunkModels[] are updated in-place so downstream Phase 5 cross-chunk
// matching sees purer, better-separated cluster models.
//
// New local cluster IDs are assigned starting from maxLocalId+1 within each
// chunk so they remain globally unique across chunks.
// ---------------------------------------------------------------------------
// KK::WithinChunkTemplateMatch
//
// Within-chunk xcorr template matching — Phase 5 of the runtime pipeline.
// Runs after Phase 4 (mean-waveform harvest), before Phase 6 (cross-chunk
// matching).
//
// For each chunk, computes the normalised circular xcorr between every pair of
// cluster mean waveforms (already in ChunkModel::meanWav, channel-major int16).
// A mutual-best pair whose xcorr score >= minScore is merged:
//   - perChunkClass[k] is remapped so both clusters share the lower local ID.
//   - The surviving ChunkModel accumulates both clusters' members.
//
// This is a waveform-level analogue of the Mahalanobis MNN merge — it catches
// clusters that are separated in feature space but represent the same unit
// viewed at slightly different times within the chunk.  It also pre-consolidates
// the model list before Phase 6 so cross-chunk matching has fewer, purer models.
// ---------------------------------------------------------------------------

namespace {

// ----- Top eigenvalue of a symmetric D × D matrix via power iteration ------
// Returns lambda_max; converges in O(D² · iters), no eigvecs stored.  Used by
// the Phase 5 union-elongation gate: top eigenvalue of pooled+between
// covariance vs top eigenvalues of the individual cluster covariances.
//
// Reference matrix M must be symmetric; only the values M[i*D + j] (full
// dense, with M[i,j] = M[j,i]) are read.  Stable on near-degenerate
// spectra to within tol; if the matrix is identically zero, returns 0.
static double tmpl_top_eigenvalue(const double* M, int D,
                                  int max_iter = 60, double tol = 1e-7)
{
    if (D < 1) return 0.0;
    if (D == 1) return M[0];
    std::vector<double> u(static_cast<size_t>(D), 1.0 / std::sqrt(static_cast<double>(D)));
    std::vector<double> v(static_cast<size_t>(D));
    double lambda = 0.0;
    for (int it = 0; it < max_iter; it++) {
        // v = M u
        for (int i = 0; i < D; i++) {
            double s = 0.0;
            for (int j = 0; j < D; j++) s += M[i * D + j] * u[static_cast<size_t>(j)];
            v[static_cast<size_t>(i)] = s;
        }
        // ||v||
        double n2 = 0.0;
        for (int i = 0; i < D; i++) n2 += v[static_cast<size_t>(i)] * v[static_cast<size_t>(i)];
        const double nrm = std::sqrt(n2);
        if (nrm < 1e-30) return 0.0;
        for (int i = 0; i < D; i++) v[static_cast<size_t>(i)] /= nrm;
        // Rayleigh quotient: λ = vᵀ M v
        double rq = 0.0;
        for (int i = 0; i < D; i++) {
            double s = 0.0;
            for (int j = 0; j < D; j++) s += M[i * D + j] * v[static_cast<size_t>(j)];
            rq += v[static_cast<size_t>(i)] * s;
        }
        const bool converged = (std::abs(rq - lambda) <= tol * std::abs(rq) + 1e-12);
        lambda = rq;
        std::swap(u, v);
        if (converged) break;
    }
    return lambda;
}

// ----- Union-elongation ratio test ----------------------------------------
//
// For a candidate merge pair (A, B), compute the union covariance under
// equal-weight pooled-within + Welch-style between term:
//
//   Σ_pooled = ((N_A-1)·Σ_A + (N_B-1)·Σ_B) / (N_A+N_B-2)
//   Σ_union  = Σ_pooled + (N_A·N_B / ((N_A+N_B)(N_A+N_B-1))) · (m_A-m_B)(m_A-m_B)ᵀ
//
// Then compare top eigenvalues:
//
//   ratio = λ_max(Σ_union) / max(λ_max(Σ_A), λ_max(Σ_B))
//
// Interpretation:
//   ratio ≈ 1   →  union's top eigenvalue is dominated by the more-elongated
//                  component's natural anisotropy — the centroid separation
//                  added no new structure → A and B are the same unit.
//   ratio >> 1  →  union has elongation that NEITHER component had on its
//                  own — driven by inter-centroid separation → A and B
//                  are distinct units, do not merge.
//
// Crucially: this test does NOT have the failure mode that killed patch21's
// per-cluster PCA preprocessing.  patch21 projected a single cluster's spikes
// onto that cluster's own top eigenvector — which by construction produced
// a 1-D distribution that CEM-with-K=2 would happily fit, giving a 67%
// false-positive split rate on elongated single clusters.  The union-ratio
// test SUBTRACTS the per-cluster top eigenvalue contribution, so an
// elongated single cluster has ratio ≈ 1 (its own elongation cancels
// against the pooled-within term) and does not trigger a false veto.
//
// Returns the ratio.  Returns 1.0 (no veto) on degenerate inputs.
static double tmpl_union_eig_ratio(const KK::ChunkModel& A,
                                   const KK::ChunkModel& B)
{
    const int D = static_cast<int>(A.mean.size());
    if (D < 2 || static_cast<int>(B.mean.size()) != D) return 1.0;
    if (A.cov.size() != static_cast<size_t>(D) * D) return 1.0;
    if (B.cov.size() != static_cast<size_t>(D) * D) return 1.0;
    if (A.nMembers < 2 || B.nMembers < 2)            return 1.0;

    const double Na = A.nMembers, Nb = B.nMembers, Nu = Na + Nb;

    // Symmetrise both per-cluster covariances from their upper-triangle storage.
    std::vector<double> SA(static_cast<size_t>(D) * D);
    std::vector<double> SB(static_cast<size_t>(D) * D);
    for (int i = 0; i < D; i++) {
        for (int j = i; j < D; j++) {
            const double a = static_cast<double>(A.cov[static_cast<size_t>(i) * D + j]);
            const double b = static_cast<double>(B.cov[static_cast<size_t>(i) * D + j]);
            SA[i * D + j] = a; SA[j * D + i] = a;
            SB[i * D + j] = b; SB[j * D + i] = b;
        }
    }

    // Pooled-within covariance.
    std::vector<double> SU(static_cast<size_t>(D) * D);
    const double pooledDen = std::max(1.0, Nu - 2.0);
    const double wA = (Na - 1.0) / pooledDen;
    const double wB = (Nb - 1.0) / pooledDen;
    for (int i = 0; i < D * D; i++) SU[i] = wA * SA[i] + wB * SB[i];

    // Between-cluster outer product.
    const double between = (Na * Nb) / (Nu * std::max(1.0, Nu - 1.0));
    for (int i = 0; i < D; i++) {
        const double di = static_cast<double>(A.mean[i]) - static_cast<double>(B.mean[i]);
        for (int j = 0; j < D; j++) {
            const double dj = static_cast<double>(A.mean[j]) - static_cast<double>(B.mean[j]);
            SU[i * D + j] += between * di * dj;
        }
    }

    // Top eigenvalues of each.
    const double tA = tmpl_top_eigenvalue(SA.data(), D);
    const double tB = tmpl_top_eigenvalue(SB.data(), D);
    const double tU = tmpl_top_eigenvalue(SU.data(), D);
    const double tMax = std::max(tA, tB);
    if (tMax <= 1e-30) return 1.0;
    return tU / tMax;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// build_hann_weights — precompute Hann window weights of full width hannN,
// centered on `peak`, for a waveform of length nSamples.
//
//   k = s - peak
//   w(s) = 0.5 * (1 + cos(2π·k / hannN))   for |k| ≤ hannN/2
//   w(s) = 0                               otherwise
//
// Returns all-ones (no taper) when hannN <= 0 or hannN >= nSamples.
// ---------------------------------------------------------------------------
namespace {
static inline void build_hann_weights(std::vector<float>& w,
                                       int nSamples, int peak, int hannN)
{
    w.assign(static_cast<size_t>(nSamples), 1.0f);
    if (hannN <= 0 || hannN >= nSamples) return;
    std::fill(w.begin(), w.end(), 0.0f);
    const int half   = hannN / 2;
    const int sStart = std::max(0, peak - half);
    const int sEnd   = std::min(nSamples, peak + (hannN - half));
    const double inv = 2.0 * M_PI / static_cast<double>(hannN);
    for (int s = sStart; s < sEnd; ++s) {
        const int k = s - peak;
        w[static_cast<size_t>(s)] =
            static_cast<float>(0.5 * (1.0 + std::cos(inv * k)));
    }
}

// Apply precomputed Hann weights to a sample-major [nSamples × nChan]
// template in place.  Each sample's nChan values get scaled by w[s].
// Round-to-int16 (templates carry the full int16 range so this is lossy
// only at the bit-1 level; insignificant vs the noise floor).  No-op
// when w is all-ones (set by build_hann_weights for the no-taper case).
static inline void apply_hann_weights(std::vector<int16_t>& tpl,
                                       const std::vector<float>& w,
                                       int nChan, int nSamples)
{
    if (nChan <= 0 || nSamples <= 0)                         return;
    if (static_cast<int>(tpl.size()) != nChan * nSamples)    return;
    if (static_cast<int>(w.size())   != nSamples)            return;
    for (int s = 0; s < nSamples; ++s) {
        const float ws = w[static_cast<size_t>(s)];
        if (ws == 1.0f) continue;       // skip the no-taper region (small win)
        if (ws == 0.0f) {                // hot path: flanks
            std::memset(tpl.data() + static_cast<size_t>(s) * nChan, 0,
                        static_cast<size_t>(nChan) * sizeof(int16_t));
            continue;
        }
        int16_t* row = tpl.data() + static_cast<size_t>(s) * nChan;
        for (int c = 0; c < nChan; ++c) {
            row[c] = static_cast<int16_t>(
                std::lround(static_cast<float>(row[c]) * ws));
        }
    }
}
}  // namespace

int  KK::WithinChunkTemplateMatch(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nChan, int nSamplesPerSpike, float minScore)
{
    const int wElems  = nChan * nSamplesPerSpike;
    const int maxShft = std::max(1, nSamplesPerSpike / 4);
    const int nCh     = static_cast<int>(chunkPoints.size());
    int totalMerged   = 0;

    // ── Identity-flow instrumentation.  Counts per call:
    //     spikesRelabeled   — # spikes whose cls[] value changed via lcRemap
    //     canonicalsTouched — # unique canonicals that absorbed >= 1 cluster
    //     absorbedClusters  — # clusters that were absorbed into a canonical
    //
    // Designed to make Phase 4 convergence behaviour directly inspectable:
    //   * Healthy convergence: spikesRelabeled DECREASES across iters
    //     (fewer spikes need to move because fewer clusters remain to
    //     consolidate).
    //   * Pathological shuffling: spikesRelabeled stays high across iters
    //     while pair count drops (would indicate spikes oscillating
    //     between clusters without genuine consolidation).
    //   * Genuine large-cluster sweep: absorbedClusters drops but
    //     spikesRelabeled stays high or rises (later iters merging
    //     fewer-but-larger clusters).
    int callSpikesRelabeled   = 0;
    int callCanonicalsTouched = 0;
    int callAbsorbedClusters  = 0;

    // ── Hann taper precompute (no-op when flag = 0). ──
    const bool _useTaper = (TemplateMatchTaperHannSamples > 0
                            && TemplateMatchTaperHannSamples < nSamplesPerSpike);
    std::vector<float> hannW;
    if (_useTaper) {
        build_hann_weights(hannW, nSamplesPerSpike, PeakSampleIndex,
                            TemplateMatchTaperHannSamples);
    }

    // ── Post-merge realign accumulator (no-op when MergeRealignEnable = 0).
    //    Each chunk's merge step appends global spike IDs whose cumulative
    //    shift was just updated; refeaturized once at end of call.
    //
    //    Per-chunk scratch (patch 0066): each chunk writes to its own
    //    chunkChangedSpikes[ck] / chunkStats[ck] slot while the outer ck
    //    loop runs in parallel.  Concatenated/summed into the final
    //    mergeRealign* variables after the parallel section. ──
    std::vector<std::vector<int>> chunkChangedSpikes(static_cast<size_t>(nCh));
    std::vector<KlustersRealign::RealignStats> chunkStats(static_cast<size_t>(nCh));
    int mergeRealignClustersTouched = 0;

    // ── Per-chunk parallelism (patch 0066) ─────────────────────────────
    // Each iteration mutates only perChunkClass[ck] / perChunkModels[ck]
    // (disjoint slots) and writes its own chunkChangedSpikes[ck] /
    // chunkStats[ck] entry.  Counters via reduction.  m_cumShift writes
    // from realign callers go to disjoint per-spike indices (each chunk
    // owns disjoint global spike IDs via chunkPoints[ck]).  Local scratch
    // (taperedMeanWav, lcRemap, canonNewSpikes, mergedCanonicalLcs) is
    // iteration-local.
    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalMerged,callSpikesRelabeled, \
                  callCanonicalsTouched,callAbsorbedClusters, \
                  mergeRealignClustersTouched)
    for (int ck = 0; ck < nCh; ck++) {
        auto& cls  = perChunkClass[ck];
        auto& mdls = perChunkModels[ck];
        const int n = static_cast<int>(mdls.size());
        if (n < 2) continue;

        // ── Build Hann-tapered templates once per chunk (no-op when
        //    TemplateMatchTaperHannSamples <= 0).  Used only for xcorr;
        //    the original meanWav stays intact for the post-merge
        //    weighted accumulation below.
        std::vector<std::vector<int16_t>> taperedMeanWav;
        if (_useTaper) {
            taperedMeanWav.resize(static_cast<size_t>(n));
            for (int a = 0; a < n; a++) {
                const auto& src = mdls[static_cast<size_t>(a)].meanWav;
                if (static_cast<int>(src.size()) != wElems) continue;
                taperedMeanWav[static_cast<size_t>(a)] = src;
                apply_hann_weights(taperedMeanWav[static_cast<size_t>(a)],
                                    hannW, nChan, nSamplesPerSpike);
            }
        }
        auto xcorrPtr = [&](int a) -> const int16_t* {
            if (_useTaper && !taperedMeanWav[static_cast<size_t>(a)].empty())
                return taperedMeanWav[static_cast<size_t>(a)].data();
            return mdls[static_cast<size_t>(a)].meanWav.data();
        };

        // ── Compute all pairwise xcorr scores ─────────────────────────────
        // score[a][b] = normalised xcorr of mdls[a].meanWav vs mdls[b].meanWav
        std::vector<float> scoreAB(static_cast<size_t>(n) * n, -1.0f);

        if (TemplateMatchBatchedXcorr != 0) {
            // Batched (patch 0056): one XcorrDispatch::compute per
            // reference cluster a, with all partners b>a packed as the
            // "waveform" batch.  Converts the O(n^2) individual kernel
            // launches into O(n) batched launches -- on CUDA the per-call
            // launch latency dominated the 256-element xcorr, so this is
            // a large wall-clock win for cluster-rich chunks.  Numerically
            // identical to the per-pair path (same compute(), same
            // template layout, just many waveforms per call).
            const int wlen = wElems;
            std::vector<int16_t> batch;
            std::vector<int>     bIdx;
            std::vector<int>     shifts;
            std::vector<float>   scores;
            batch.reserve(static_cast<size_t>(n) * wlen);
            for (int a = 0; a < n; a++) {
                if (mdls[static_cast<size_t>(a)].localClusterId == 0) continue;
                if (static_cast<int>(mdls[static_cast<size_t>(a)].meanWav.size())
                        != wElems) continue;
                batch.clear(); bIdx.clear();
                for (int b = a + 1; b < n; b++) {
                    if (mdls[static_cast<size_t>(b)].localClusterId == 0) continue;
                    if (static_cast<int>(mdls[static_cast<size_t>(b)].meanWav.size())
                            != wElems) continue;
                    const int16_t* bp = xcorrPtr(b);
                    batch.insert(batch.end(), bp, bp + wlen);
                    bIdx.push_back(b);
                }
                const int nb = static_cast<int>(bIdx.size());
                if (nb == 0) continue;
                shifts.assign(static_cast<size_t>(nb), 0);
                scores.assign(static_cast<size_t>(nb), -1.0f);
                XcorrDispatch::compute(
                    batch.data(), xcorrPtr(a),
                    nb, nChan, nSamplesPerSpike,
                    maxShft, 0.0f, shifts.data(), scores.data());
                for (int j = 0; j < nb; j++) {
                    const int b  = bIdx[static_cast<size_t>(j)];
                    const float sc = scores[static_cast<size_t>(j)];
                    scoreAB[static_cast<size_t>(a * n + b)] = sc;
                    scoreAB[static_cast<size_t>(b * n + a)] = sc;
                }
            }
        } else {
        for (int a = 0; a < n; a++) {
            if (mdls[static_cast<size_t>(a)].localClusterId == 0) continue;
            if (static_cast<int>(mdls[static_cast<size_t>(a)].meanWav.size()) != wElems) continue;
            for (int b = a + 1; b < n; b++) {
                if (mdls[static_cast<size_t>(b)].localClusterId == 0) continue;
                if (static_cast<int>(mdls[static_cast<size_t>(b)].meanWav.size()) != wElems) continue;
                int sh = 0; float sc = 0.0f;
                XcorrDispatch::compute(
                    xcorrPtr(a), xcorrPtr(b),
                    1, nChan, nSamplesPerSpike,
                    maxShft, 0.0f, &sh, &sc);
                scoreAB[static_cast<size_t>(a * n + b)] = sc;
                scoreAB[static_cast<size_t>(b * n + a)] = sc;  // xcorr is symmetric for single spike
            }
        }
        }

        // ── Find mutual-best pairs above threshold ─────────────────────────
        // For each cluster find its highest-scoring partner
        std::vector<int>   bestB(static_cast<size_t>(n), -1);
        std::vector<float> bestS(static_cast<size_t>(n), -1.0f);
        for (int a = 0; a < n; a++) {
            for (int b = 0; b < n; b++) {
                if (a == b) continue;
                float s = scoreAB[static_cast<size_t>(a * n + b)];
                if (s >= minScore && s > bestS[static_cast<size_t>(a)]) {
                    bestS[static_cast<size_t>(a)] = s;
                    bestB[static_cast<size_t>(a)] = b;
                }
            }
        }

        // ── Union-Find for transitive merges ──────────────────────────────
        std::vector<int> parent(static_cast<size_t>(n));
        std::iota(parent.begin(), parent.end(), 0);
        auto Find = [&](int x) -> int {
            while (parent[static_cast<size_t>(x)] != x) {
                parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(
                    parent[static_cast<size_t>(x)])];
                x = parent[static_cast<size_t>(x)];
            }
            return x;
        };
        auto Union = [&](int a, int b) {
            a = Find(a); b = Find(b);
            if (a != b) parent[static_cast<size_t>(b)] = a;
        };

        int chunkMerged = 0;
        for (int a = 0; a < n; a++) {
            int b = bestB[static_cast<size_t>(a)];
            if (b < 0) continue;
            if (bestB[static_cast<size_t>(b)] != a) continue;  // not mutual best
            if (Find(a) == Find(b)) continue;

            // Optional eigenvalue-ratio veto.  When -TemplateMatchEigRatio > 0,
            // additionally require that the union covariance's top eigenvalue
            // is not dominated by inter-centroid separation.  See
            // tmpl_union_eig_ratio() docs for the rationale and why this
            // avoids the failure mode that killed patch21's per-cluster PCA.
            if (TemplateMatchEigRatio > 0.0f) {
                const double eigRatio = tmpl_union_eig_ratio(
                    mdls[static_cast<size_t>(a)],
                    mdls[static_cast<size_t>(b)]);
                if (eigRatio > static_cast<double>(TemplateMatchEigRatio)) {
                    if (Verbose >= 1)
                        Output("  tmpl-within: chunk%d c%d+c%d xcorr=%.3f "
                               "eig-ratio=%.2f > %.2f → VETO\n",
                               ck,
                               mdls[static_cast<size_t>(a)].localClusterId,
                               mdls[static_cast<size_t>(b)].localClusterId,
                               bestS[static_cast<size_t>(a)],
                               eigRatio,
                               static_cast<double>(TemplateMatchEigRatio));
                    continue;
                }
            }

            Union(a, b);
            if (Verbose >= 1)
                Output("  tmpl-within: chunk%d c%d+c%d xcorr=%.3f\n",
                       ck,
                       mdls[static_cast<size_t>(a)].localClusterId,
                       mdls[static_cast<size_t>(b)].localClusterId,
                       bestS[static_cast<size_t>(a)]);
            chunkMerged++;
            totalMerged++;
        }

        if (chunkMerged == 0) continue;

        // ── Remap perChunkClass: each component → lowest localClusterId ────
        // Identify canonical (lowest) ID per component
        std::unordered_map<int,int> rootToCanonIdx;
        for (int a = 0; a < n; a++) {
            int root = Find(a);
            auto it = rootToCanonIdx.find(root);
            if (it == rootToCanonIdx.end() ||
                mdls[static_cast<size_t>(a)].localClusterId <
                mdls[static_cast<size_t>(it->second)].localClusterId)
                rootToCanonIdx[root] = a;
        }
        // Build localClusterId → canonical localClusterId remap
        std::unordered_map<int,int> lcRemap;
        for (int a = 0; a < n; a++) {
            int canon = mdls[static_cast<size_t>(rootToCanonIdx[Find(a)])].localClusterId;
            lcRemap[mdls[static_cast<size_t>(a)].localClusterId] = canon;
        }

        // Capture which canonicals absorbed >= 1 other cluster.  Used
        // by both the post-merge realign (when MergeRealignEnable) AND
        // by the identity-flow instrumentation below.
        std::unordered_set<int> mergedCanonicalLcs;
        int chunkAbsorbedClusters = 0;
        {
            std::unordered_map<int,int> canonAbsorbCount;
            for (auto& [src, canon] : lcRemap) {
                if (src != canon) {
                    canonAbsorbCount[canon]++;
                    ++chunkAbsorbedClusters;
                }
            }
            for (auto& [canon, cnt] : canonAbsorbCount) {
                if (cnt > 0) mergedCanonicalLcs.insert(canon);
            }
        }
        callCanonicalsTouched += static_cast<int>(mergedCanonicalLcs.size());
        callAbsorbedClusters  += chunkAbsorbedClusters;

        // Remap labels.  Count spikes whose label changed (lcRemap maps
        // them to something different from themselves) -- this is the
        // # of cls[] entries that will actually change value.
        int chunkSpikesRelabeled = 0;
        // Incremental realign (patch 0054): record GLOBAL spike IDs newly
        // absorbed into each canonical this merge.  Only these need
        // realigning against the canonical's updated template.
        std::unordered_map<int, std::vector<int>> canonNewSpikes;
        const bool _wantIncremental =
            (MergeRealignEnable != 0 && MergeRealignIncremental != 0
             && m_timeShiftReady);
        for (size_t _i = 0; _i < cls.size(); ++_i) {
            int& lc = cls[_i];
            auto it = lcRemap.find(lc);
            if (it != lcRemap.end() && it->second != lc) {
                const int canon = it->second;
                if (_wantIncremental)
                    canonNewSpikes[canon].push_back(
                        chunkPoints[static_cast<size_t>(ck)][_i]);
                lc = canon;
                ++chunkSpikesRelabeled;
            }
        }
        callSpikesRelabeled += chunkSpikesRelabeled;

        // ── Aggregate waveforms from merged-away models into canonicals ───
        //
        // Before label remap, accumulate weighted means per canonical
        // component using each cluster's stored counts as weights.
        // Without this step, the surviving model's meanWav and
        // meanWavLeft/Right still reflect ONLY the canonical cluster's
        // pre-merge spikes — Phase 6 then runs cross-chunk xcorr on
        // stale templates that don't include the merged-in cluster's
        // contribution.  This was a real bug that biased Phase 6
        // decisions.
        //
        // Mathematics: arithmetic mean is associative under proper
        // weighting — mean(A∪B) = (|A|·mean(A) + |B|·mean(B)) / (|A|+|B|).
        // Per-edge means use the per-edge counts (nMembersLeft/Right)
        // so spikes that fell outside the edge windows don't pollute
        // the boundary template.
        //
        // dst.nMembers itself is recomputed below from the remapped
        // cls labels (authoritative); we only need a temporary running
        // count for weighting the meanWav merge.  nMembersLeft/Right
        // ARE updated here since they're not recomputed elsewhere.
        auto wmerge = [](std::vector<int16_t>& d, int& dN,
                         const std::vector<int16_t>& s, int sN) {
            if (s.empty() || sN <= 0) return;
            if (d.empty() || dN <= 0) { d = s; dN = sN; return; }
            const size_t L = d.size();
            for (size_t e = 0; e < L; e++) {
                const int64_t num = static_cast<int64_t>(d[e]) * dN
                                  + static_cast<int64_t>(s[e]) * sN;
                d[e] = static_cast<int16_t>(num / (dN + sN));
            }
            dN += sN;
        };

        for (const auto& [root, canonIdx] : rootToCanonIdx) {
            ChunkModel& dst = mdls[static_cast<size_t>(canonIdx)];
            int runningN     = dst.nMembers;
            int runningNLeft = dst.nMembersLeft;
            int runningNRight= dst.nMembersRight;
            for (int a = 0; a < n; a++) {
                if (a == canonIdx) continue;
                if (Find(a) != root) continue;
                const ChunkModel& src = mdls[static_cast<size_t>(a)];
                wmerge(dst.meanWav,      runningN,      src.meanWav,      src.nMembers);
                wmerge(dst.meanWavLeft,  runningNLeft,  src.meanWavLeft,  src.nMembersLeft);
                wmerge(dst.meanWavRight, runningNRight, src.meanWavRight, src.nMembersRight);
            }
            // Persist the updated edge counts; nMembers is recomputed below.
            dst.nMembersLeft  = runningNLeft;
            dst.nMembersRight = runningNRight;
        }

        // Remove merged-away ChunkModels; keep canonical ones
        std::unordered_set<int> keepLc;
        for (auto& [root, idx2] : rootToCanonIdx)
            keepLc.insert(mdls[static_cast<size_t>(idx2)].localClusterId);
        mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
            [&](const ChunkModel& cm){ return !keepLc.count(cm.localClusterId); }),
            mdls.end());

        // Update nMembers on surviving models
        for (auto& cm : mdls) {
            cm.nMembers = 0;
            for (const auto& lc : cls)
                if (lc == cm.localClusterId) cm.nMembers++;
        }

        // ── Post-merge realignment for canonicals that absorbed >= 1
        //    other cluster.  Gathers the canonical's now-merged spike
        //    set (chunk-local indices where cls[i] == canonLc, mapped
        //    to global spike IDs via chunkPoints[ck]) and runs the
        //    klusters-faithful per-cluster realign.  Shifts commit to
        //    m_cumShift; changed-spike list is appended for a single
        //    end-of-call RefeaturizeChangedSpikes.
        //
        //    Why here: at this point cls[] reflects the post-merge
        //    label state, mdls' canonical models exist, the absorbed
        //    spikes are now visible under canonLc.  Realigning to the
        //    canonical's (newly weighted-meaned) template tightens the
        //    spike-to-template variance that the merge introduced.
        if (MergeRealignEnable != 0 && m_timeShiftReady
            && !mergedCanonicalLcs.empty()) {
            const int peakPos  = PeakSampleIndex;
            const int maxShift = std::max(1,
                std::min(nSamplesPerSpike / 4, KlustersRealignMaxShift));
            const int minSize  = std::max(2, KlustersRealignMinSize);
            // Lookup canonLc -> model index for the incremental template.
            std::unordered_map<int,int> _lcToIdx;
            if (MergeRealignIncremental != 0) {
                for (int _a = 0; _a < n; ++_a)
                    _lcToIdx[mdls[static_cast<size_t>(_a)].localClusterId] = _a;
            }
            for (int canonLc : mergedCanonicalLcs) {
                if (MergeRealignIncremental != 0) {
                    // Incremental: align ONLY the spikes just absorbed
                    // into this canonical, against the canonical's
                    // post-wmerge meanWav (its actual shape) -- not a
                    // template re-derived from the few absorbed spikes,
                    // and not the whole canonical (wasteful).
                    auto _ns = canonNewSpikes.find(canonLc);
                    if (_ns == canonNewSpikes.end() || _ns->second.empty())
                        continue;
                    auto _mi = _lcToIdx.find(canonLc);
                    if (_mi == _lcToIdx.end()) continue;
                    const auto& _tmpl =
                        mdls[static_cast<size_t>(_mi->second)].meanWav;
                    if (static_cast<int>(_tmpl.size()) != wElems) continue;
                    RealignSpikesAgainstTemplate(
                        _ns->second, _tmpl.data(), nChan, nSamplesPerSpike,
                        peakPos, maxShift,
                        chunkChangedSpikes[static_cast<size_t>(ck)], chunkStats[static_cast<size_t>(ck)]);
                    ++mergeRealignClustersTouched;
                    continue;
                }
                std::vector<int> spikeIds;
                spikeIds.reserve(256);
                for (size_t i = 0; i < cls.size(); ++i) {
                    if (cls[i] == canonLc) {
                        spikeIds.push_back(
                            chunkPoints[static_cast<size_t>(ck)][i]);
                    }
                }
                if (static_cast<int>(spikeIds.size()) < minSize) continue;
                KlustersStyleRealignOneCluster(
                    spikeIds, nChan, nSamplesPerSpike,
                    peakPos, maxShift, minSize,
                    chunkChangedSpikes[static_cast<size_t>(ck)], chunkStats[static_cast<size_t>(ck)]);
                ++mergeRealignClustersTouched;
            }
        }
    }

    // ── End of call: refeaturize the spikes whose m_cumShift changed
    //    via post-merge realign.  Single call (better than per-chunk
    //    because the PCA model load + .fil cache check happens once).
    //    No-op when MergeRealignEnable == 0 (vector stays empty).
    // ── Gather: concatenate per-chunk changed spikes; sum / max stats.
    //    Sums for counters; max for maxAbsShift; weighted mean for
    //    meanAbsShift (weighted by nSpikesRealigned per chunk).
    std::vector<int> mergeRealignChangedSpikes;
    KlustersRealign::RealignStats mergeRealignStats;
    {
        size_t total = 0;
        for (const auto& v : chunkChangedSpikes) total += v.size();
        mergeRealignChangedSpikes.reserve(total);
        double weightedShift = 0.0;
        for (size_t k = 0; k < chunkChangedSpikes.size(); ++k) {
            mergeRealignChangedSpikes.insert(
                mergeRealignChangedSpikes.end(),
                chunkChangedSpikes[k].begin(), chunkChangedSpikes[k].end());
            const auto& s = chunkStats[k];
            mergeRealignStats.nClustersProcessed += s.nClustersProcessed;
            mergeRealignStats.nClustersSkipped   += s.nClustersSkipped;
            mergeRealignStats.nSpikesEvaluated   += s.nSpikesEvaluated;
            mergeRealignStats.nSpikesRealigned   += s.nSpikesRealigned;
            mergeRealignStats.nSpikesReadFailed  += s.nSpikesReadFailed;
            if (s.maxAbsShift > mergeRealignStats.maxAbsShift)
                mergeRealignStats.maxAbsShift = s.maxAbsShift;
            weightedShift += s.meanAbsShift * s.nSpikesRealigned;
        }
        if (mergeRealignStats.nSpikesRealigned > 0)
            mergeRealignStats.meanAbsShift =
                weightedShift / mergeRealignStats.nSpikesRealigned;
    }

    if (!mergeRealignChangedSpikes.empty()) {
        RefeaturizeChangedSpikes(mergeRealignChangedSpikes,
                                  nChan, nSamplesPerSpike);
        LockedStderr(
            "  [WithinChunkTemplateMatch] post-merge realign: "
            "%d clusters touched, %d/%d spikes shifted (max|Δ|=%d), "
            "%zu spikes refeaturized\n",
            mergeRealignClustersTouched,
            mergeRealignStats.nSpikesRealigned,
            mergeRealignStats.nSpikesEvaluated,
            mergeRealignStats.maxAbsShift,
            mergeRealignChangedSpikes.size());
    }

    Output("WithinChunkTemplateMatch: %d cluster pair(s) merged across all "
           "chunks  [identity flow: %d spikes relabeled, %d canonicals "
           "absorbed %d clusters]\n",
           totalMerged,
           callSpikesRelabeled,
           callCanonicalsTouched,
           callAbsorbedClusters);
    return totalMerged;
}


// ---------------------------------------------------------------------------
// KK::WithinChunkTemplateMatchMedianKnn  (Phase 4 alternative)
//
// k-NN-restricted variant of WithinChunkTemplateMatch that operates on
// per-cluster MEDIAN waveforms instead of the stored mean waveforms.
//
// Per Phase 4 iteration:
//   0. For each cluster, gather its spike waveforms via
//      TimeShiftReadSpikeWave and call BuildClusterMedianWaveform.
//   1. Pre-screen: raw L2 between each pair of cluster MEDIAN templates
//      (no xcorr alignment).
//   2. For each cluster A, keep the top-K closest others by that L2.
//   3. For each MUTUAL k-NN pair (B ∈ A.topK ∧ A ∈ B.topK), run the
//      full xcorr-alignment merge gate (+ optional eigenvalue veto)
//      on the MEDIAN templates.
//   4. Union-find + canonical-id remap + weighted-mean accumulation +
//      perChunkModels pruning — identical to WithinChunkTemplateMatch's
//      commit path.
//
// Two reasons for median, not mean:
//   * Robust to outlier spikes contaminating the template.  Mixture
//     clusters where a minority sub-unit pulls the mean toward a
//     misleading shape are handled cleanly — the median tracks the
//     dominant sub-unit and ignores the minority.
//   * For genuinely similar clusters (the merge targets), the median
//     suppresses spike-to-spike variance that the alignment search
//     would otherwise have to absorb; xcorr scores between true
//     duplicates rise, scores between coincidentally-similar
//     clusters fall.
//
// Cost analysis (n = clusters in chunk, w = nChan*nSamples, N̄ = mean
// cluster size, M = maxShift):
//   median build:    O(n · N̄ · w)            — sequential .spk reads
//                                              (memcpy from mmap)
//                                              + std::nth_element per
//                                              waveform position
//   L2 pre-screen:   O(n² · w)
//   topK selection:  O(n · n log n)
//   xcorr merge:     O(n · K · w · M)        — only on mutual k-NN pairs
//
// For a typical 30-cluster chunk with N̄ ≈ 500, this adds ~4 MB of
// .spk reads (memory-bound when mmap'd) and ~15M nth_element ops
// per chunk per iter.  Net runtime cost vs the mean version:
// ~50–200 ms per chunk per iter on the user's RTX 5070 Ti / 9800X3D.
//
// Mutual k-NN gate motivation: relaxing the existing mutual-BEST
// (k=1) requirement to mutual-K-NN lets a cluster have multiple
// merge candidates per pass, but each merge still requires
// symmetric agreement, so no cluster drags another into a merge
// against its own ranking.  The k=K relaxation matters specifically
// when several clusters are tightly grouped: with k=1, only the
// strongest pair merges per pass and the rest wait for the next
// alternation iter; with k=K, the whole group merges in one pass.
//
// Fallback: clusters whose median couldn't be built (no time-shift
// backing store, or fewer than 2 member spikes) keep meanWav as
// their template via the `tpl()` accessor inside the function.
//
// CLI: -MedianKnnTemplateMatchEnable / -MedianKnnTemplateMatchK
// Drop-in replacement when MedianKnnTemplateMatchEnable != 0; same
// return value (total pair-merges across all chunks).
// ---------------------------------------------------------------------------
int  KK::WithinChunkTemplateMatchMedianKnn(
    const std::vector<std::vector<int>>& chunkPoints,
    std::vector<std::vector<int>>&        perChunkClass,
    std::vector<std::vector<ChunkModel>>& perChunkModels,
    int nChan, int nSamplesPerSpike, float minScore)
{
    const int wElems  = nChan * nSamplesPerSpike;
    const int maxShft = std::max(1, nSamplesPerSpike / 4);
    const int nCh     = static_cast<int>(chunkPoints.size());
    const int K       = std::max(1, MedianKnnTemplateMatchK);
    int totalMerged   = 0;

    // Identity-flow instrumentation; see the identical block in
    // WithinChunkTemplateMatch for semantics.
    int callSpikesRelabeled   = 0;
    int callCanonicalsTouched = 0;
    int callAbsorbedClusters  = 0;

    // ── Hann taper precompute (no-op when flag = 0). ──
    const bool _useTaper = (TemplateMatchTaperHannSamples > 0
                            && TemplateMatchTaperHannSamples < nSamplesPerSpike);
    std::vector<float> hannW;
    if (_useTaper) {
        build_hann_weights(hannW, nSamplesPerSpike, PeakSampleIndex,
                            TemplateMatchTaperHannSamples);
    }

    // ── Post-merge realign accumulator (no-op when flag = 0). ──
    //    Per-chunk scratch for the parallel outer loop (patch 0066).
    std::vector<std::vector<int>> chunkChangedSpikes(static_cast<size_t>(nCh));
    std::vector<KlustersRealign::RealignStats> chunkStats(static_cast<size_t>(nCh));
    int mergeRealignClustersTouched = 0;

    // ── Per-chunk parallelism (patch 0066) ─────────────────────────────
    // Same independence argument as WithinChunkTemplateMatch.  Extra care:
    //   * m_medianCache is a class-member std::unordered_map keyed by
    //     (ck * MaxPossibleClusters + lc) — disjoint keys per chunk, but
    //     map structural mutations during insert/find still race; guarded
    //     by #pragma omp critical(median_cache) at the access sites.
    //   * The inner per-cluster median-build parallel for (patch 0051) is
    //     gated by if(!omp_in_parallel()) so it does not nest under this
    //     outer parallel region (nested OMP off by default; the if-clause
    //     makes it explicit and correct under any nesting setting).
    #pragma omp parallel for schedule(dynamic) \
        reduction(+:totalMerged,callSpikesRelabeled, \
                  callCanonicalsTouched,callAbsorbedClusters, \
                  mergeRealignClustersTouched)
    for (int ck = 0; ck < nCh; ck++) {
        auto& cls  = perChunkClass[ck];
        auto& mdls = perChunkModels[ck];
        const int n = static_cast<int>(mdls.size());
        if (n < 2) continue;

        // ── 0. Build a median template per cluster.
        //
        // Per Phase 4 iter, for each cluster, gather its spike waveforms
        // via TimeShiftReadSpikeWave and call BuildClusterMedianWaveform.
        // Median (not mean) is what distinguishes this variant from the
        // all-pairs WithinChunkTemplateMatch — robust to outlier spikes
        // contaminating a cluster's mean template, and (more importantly
        // for the merge decision) sharper peaks when clusters share most
        // of their structure but differ at a few sample positions.
        //
        // Cost: O(N · wElems) per cluster for the median computation
        // (std::nth_element per position).  Across a 30-cluster, 500-
        // spike-avg chunk and a 256-sample waveform, ~3.8 MB of .spk
        // reads (memcpy from mmap when m_timeShiftSpkMap is non-null),
        // ~15M nth_element ops — typically < 200 ms per chunk.
        std::vector<std::vector<int16_t>> medianTpls(
            static_cast<size_t>(n));
        if (m_timeShiftReady) {
            // Per-cluster spike global indices in this chunk.
            std::vector<std::vector<int>> clusterSpikes(static_cast<size_t>(n));
            std::unordered_map<int, int> lcToIdx;
            for (int a = 0; a < n; a++) {
                lcToIdx[mdls[static_cast<size_t>(a)].localClusterId] = a;
            }
            for (size_t i = 0; i < cls.size(); i++) {
                auto it = lcToIdx.find(cls[i]);
                if (it == lcToIdx.end()) continue;
                clusterSpikes[static_cast<size_t>(it->second)]
                    .push_back(chunkPoints[static_cast<size_t>(ck)][i]);
            }

            // ── Median template cache (patch 0052) ────────────────────
            // Membership hash via the shared ClusterMembershipHash method
            // (also used by the oscillation guard).  Including cumShift
            // means a post-merge realign invalidates the entry.
            auto membershipHash = [&](const std::vector<int>& sp) -> uint64_t {
                return ClusterMembershipHash(sp);
            };

            // Serial pass: resolve cache hits, mark misses dirty.
            std::vector<uint64_t> wantHash(static_cast<size_t>(n), 0);
            std::vector<char>     dirty(static_cast<size_t>(n), 0);
            int nReused = 0, nRebuilt = 0;
            for (int a = 0; a < n; a++) {
                const auto& cm = mdls[static_cast<size_t>(a)];
                if (cm.localClusterId == 0) continue;
                const auto& sp = clusterSpikes[static_cast<size_t>(a)];
                if (static_cast<int>(sp.size()) < 2) continue;
                const uint64_t hh = membershipHash(sp);
                wantHash[static_cast<size_t>(a)] = hh;
                const long long key =
                    static_cast<long long>(ck) * MaxPossibleClusters
                    + cm.localClusterId;
                // Patch 0066: m_medianCache (std::unordered_map) accessed
                // by multiple threads concurrently when the outer chunk
                // loop is parallel.  Different chunks → disjoint keys, but
                // std::unordered_map's structural mutation (rehash on
                // insert) still races.  Critical section is short (single
                // find + copy of a small vector), so contention is low.
                bool cacheHit = false;
                std::vector<int16_t> cachedTpl;
                #pragma omp critical(median_cache)
                {
                    auto it = m_medianCache.find(key);
                    if (it != m_medianCache.end() && it->second.first == hh
                        && static_cast<int>(it->second.second.size()) == wElems) {
                        cachedTpl = it->second.second;
                        cacheHit = true;
                    }
                }
                if (cacheHit) {
                    medianTpls[static_cast<size_t>(a)] = std::move(cachedTpl);
                    ++nReused;
                } else {
                    dirty[static_cast<size_t>(a)] = 1;
                    ++nRebuilt;
                }
            }

            // Parallel pass: build the dirty clusters only.  (mmap-safe;
            // see patch 0051 note.)
            //
            // Patch 0066: also gated by !omp_in_parallel() so this inner
            // parallel does not nest under the outer chunk loop when that
            // is parallelized.  When outer is parallel, each thread runs
            // this inner serially (its assigned chunk's per-cluster work
            // sums across chunks at the outer level).
            #pragma omp parallel for schedule(dynamic) \
                if(m_timeShiftSpkMap != nullptr && !omp_in_parallel())
            for (int a = 0; a < n; a++) {
                if (!dirty[static_cast<size_t>(a)]) continue;
                const auto& sp = clusterSpikes[static_cast<size_t>(a)];
                const int   N  = static_cast<int>(sp.size());

                std::vector<int16_t> waveBuf(
                    static_cast<size_t>(N) * wElems, 0);
                for (int i = 0; i < N; i++) {
                    int16_t* dst = waveBuf.data() +
                                   static_cast<ptrdiff_t>(i) * wElems;
                    if (!TimeShiftReadSpikeWave(sp[static_cast<size_t>(i)],
                                                 wElems, dst)) {
                        std::memset(dst, 0,
                                    static_cast<size_t>(wElems) *
                                    sizeof(int16_t));
                    }
                }
                // Compute per-sample median across this cluster's spikes.
                //
                // Inlined here rather than calling
                // KlustersRealign::BuildClusterMedianWaveform so this
                // function doesn't depend on patch 0033's exposure of
                // that helper (the patches went out in a bundle but
                // may be applied independently — keep them composable).
                //
                // For each (channel, sample) position p, gather the N
                // spike values into a scratch vector, std::nth_element
                // to find the middle element, store.  O(N · wElems)
                // per cluster — ~15M ops for a 30-cluster chunk with
                // N=500 and wElems=256.
                {
                    auto& medianTpl = medianTpls[static_cast<size_t>(a)];
                    medianTpl.assign(static_cast<size_t>(wElems), 0);
                    std::vector<int16_t> col(static_cast<size_t>(N));
                    const int midIdx = N / 2;
                    for (int p = 0; p < wElems; ++p) {
                        for (int i = 0; i < N; ++i) {
                            col[static_cast<size_t>(i)] = waveBuf[
                                static_cast<size_t>(i) * wElems + p];
                        }
                        std::nth_element(col.begin(),
                                         col.begin() + midIdx,
                                         col.end());
                        medianTpl[static_cast<size_t>(p)] =
                            col[static_cast<size_t>(midIdx)];
                    }
                }
            }

            // Serial cache-store pass: persist the medians just built for
            // the dirty clusters (the parallel loop above has joined, so
            // medianTpls[a] is complete).  Concurrent map writes are
            // avoided by doing this serially.
            for (int a = 0; a < n; a++) {
                if (!dirty[static_cast<size_t>(a)]) continue;
                if (medianTpls[static_cast<size_t>(a)].empty()) continue;
                const long long key =
                    static_cast<long long>(ck) * MaxPossibleClusters
                    + mdls[static_cast<size_t>(a)].localClusterId;
                // Patch 0066: see read-side note above.
                #pragma omp critical(median_cache)
                {
                    m_medianCache[key] = { wantHash[static_cast<size_t>(a)],
                                           medianTpls[static_cast<size_t>(a)] };
                }
            }
            if (Verbose >= 1 && (nReused + nRebuilt) > 0) {
                LockedStderr("  [MedianKnn] chunk%d median cache: "
                             "%d reused, %d rebuilt\n", ck, nReused, nRebuilt);
            }
        }

        // Fallback: clusters that didn't get a median (no time-shift
        // backing store, or N < 2) keep meanWav as the template.  This
        // lets the function run even when realignment isn't initialised;
        // the merge gate just degrades to the meanWav comparison for
        // those specific clusters.  Tracked by `usingMedian[a]` only
        // for log clarity below.
        auto tpl = [&](int a) -> const std::vector<int16_t>& {
            const auto& m = medianTpls[static_cast<size_t>(a)];
            return m.empty() ? mdls[static_cast<size_t>(a)].meanWav : m;
        };

        // ── If TemplateMatchTaperHannSamples > 0, apply the Hann taper
        //    to whichever template tpl() resolves to (median preferred,
        //    meanWav fallback).  The tapered copies replace the
        //    accessor for downstream L2 + xcorr; the originals stay
        //    in medianTpls / mdls.meanWav for any other consumer. ──
        std::vector<std::vector<int16_t>> taperedTpls;
        if (_useTaper) {
            taperedTpls.resize(static_cast<size_t>(n));
            for (int a = 0; a < n; a++) {
                const auto& src = tpl(a);
                if (static_cast<int>(src.size()) != wElems) continue;
                taperedTpls[static_cast<size_t>(a)] = src;
                apply_hann_weights(taperedTpls[static_cast<size_t>(a)],
                                    hannW, nChan, nSamplesPerSpike);
            }
        }
        auto effTpl = [&](int a) -> const std::vector<int16_t>& {
            if (_useTaper && !taperedTpls[static_cast<size_t>(a)].empty())
                return taperedTpls[static_cast<size_t>(a)];
            return tpl(a);
        };

        // ── 1. Raw-L2 pre-screen between cluster MEDIAN templates
        //       (with fall-through to meanWav for clusters that didn't
        //       get a median; see `tpl` accessor above).
        // dist[a*n + b] = Σ_i (median_a[i] - median_b[i])² over wElems.
        // Sentinel for invalid pairs (noise cluster or size-mismatched
        // template): std::numeric_limits<float>::infinity() so the
        // partial_sort sends them to the end.
        const float INF = std::numeric_limits<float>::infinity();
        std::vector<float> dist(static_cast<size_t>(n) * n, INF);
        for (int a = 0; a < n; a++) {
            const auto& A = mdls[static_cast<size_t>(a)];
            if (A.localClusterId == 0) continue;
            const auto& tA = effTpl(a);
            if (static_cast<int>(tA.size()) != wElems) continue;
            dist[static_cast<size_t>(a * n + a)] = INF;   // exclude self
            for (int b = a + 1; b < n; b++) {
                const auto& B = mdls[static_cast<size_t>(b)];
                if (B.localClusterId == 0) continue;
                const auto& tB = effTpl(b);
                if (static_cast<int>(tB.size()) != wElems) continue;
                double d2 = 0.0;
                for (int i = 0; i < wElems; i++) {
                    const double diff =
                        static_cast<double>(tA[static_cast<size_t>(i)])
                      - static_cast<double>(tB[static_cast<size_t>(i)]);
                    d2 += diff * diff;
                }
                const float fd = static_cast<float>(d2);
                dist[static_cast<size_t>(a * n + b)] = fd;
                dist[static_cast<size_t>(b * n + a)] = fd;
            }
        }

        // ── 2. For each cluster A, find its top-K closest other clusters
        //       (partial_sort by L2 distance).  Store as a set per A so
        //       the mutual-k-NN check is O(1) lookup.
        std::vector<std::set<int>> topK(static_cast<size_t>(n));
        std::vector<int> idxBuf(static_cast<size_t>(n));
        for (int a = 0; a < n; a++) {
            if (mdls[static_cast<size_t>(a)].localClusterId == 0) continue;
            std::iota(idxBuf.begin(), idxBuf.end(), 0);
            const float* dA = &dist[static_cast<size_t>(a) * n];
            const int kClamp = std::min(K, n - 1);
            std::partial_sort(
                idxBuf.begin(),
                idxBuf.begin() + kClamp,
                idxBuf.end(),
                [dA](int u, int v) { return dA[u] < dA[v]; });
            for (int t = 0; t < kClamp; t++) {
                const int b = idxBuf[static_cast<size_t>(t)];
                if (b == a) continue;
                if (!std::isfinite(dA[b])) continue;
                topK[static_cast<size_t>(a)].insert(b);
            }
        }

        // ── 3. For each mutual-k-NN candidate pair (A, B), run the full
        //       xcorr alignment + eigenvalue veto.  Same gate as the
        //       all-pairs version; we just restrict which pairs reach it.
        struct Edge { int a, b; float score; };
        std::vector<Edge> edges;
        edges.reserve(static_cast<size_t>(n) * K);
        for (int a = 0; a < n; a++) {
            for (int b : topK[static_cast<size_t>(a)]) {
                if (b <= a) continue;   // process each pair once
                if (!topK[static_cast<size_t>(b)].count(a)) continue;   // mutual

                const auto& tA = effTpl(a);
                const auto& tB = effTpl(b);
                if (static_cast<int>(tA.size()) != wElems) continue;
                if (static_cast<int>(tB.size()) != wElems) continue;

                int sh = 0; float sc = 0.0f;
                XcorrDispatch::compute(
                    tA.data(), tB.data(),
                    1, nChan, nSamplesPerSpike,
                    maxShft, 0.0f, &sh, &sc);
                if (sc < minScore) continue;

                if (TemplateMatchEigRatio > 0.0f) {
                    const double eigRatio = tmpl_union_eig_ratio(
                        mdls[static_cast<size_t>(a)],
                        mdls[static_cast<size_t>(b)]);
                    if (eigRatio > static_cast<double>(TemplateMatchEigRatio)) {
                        if (Verbose >= 1)
                            Output("  tmpl-medknn: chunk%d c%d+c%d xcorr=%.3f "
                                   "eig-ratio=%.2f > %.2f → VETO\n",
                                   ck,
                                   mdls[static_cast<size_t>(a)].localClusterId,
                                   mdls[static_cast<size_t>(b)].localClusterId,
                               sc, eigRatio,
                               static_cast<double>(TemplateMatchEigRatio));
                        continue;
                    }
                }

                edges.push_back({a, b, sc});
            }
        }

        if (edges.empty()) continue;

        // ── 4. Union-Find for transitive merges.  Apply edges in
        //       descending xcorr score so the strongest pair "leads"
        //       the component.  Same algorithm as the all-pairs version;
        //       only the candidate set differs.
        std::sort(edges.begin(), edges.end(),
                  [](const Edge& x, const Edge& y) { return x.score > y.score; });

        std::vector<int> parent(static_cast<size_t>(n));
        std::iota(parent.begin(), parent.end(), 0);
        auto Find = [&](int x) -> int {
            while (parent[static_cast<size_t>(x)] != x) {
                parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(
                    parent[static_cast<size_t>(x)])];
                x = parent[static_cast<size_t>(x)];
            }
            return x;
        };
        auto Union = [&](int a, int b) {
            a = Find(a); b = Find(b);
            if (a != b) parent[static_cast<size_t>(b)] = a;
        };

        int chunkMerged = 0;
        for (const Edge& e : edges) {
            if (Find(e.a) == Find(e.b)) continue;
            Union(e.a, e.b);
            if (Verbose >= 1)
                Output("  tmpl-medknn: chunk%d c%d+c%d xcorr=%.3f\n",
                       ck,
                   mdls[static_cast<size_t>(e.a)].localClusterId,
                   mdls[static_cast<size_t>(e.b)].localClusterId,
                   e.score);
            chunkMerged++;
            totalMerged++;
        }

        if (chunkMerged == 0) continue;

        // ── 5. Remap perChunkClass + weighted mean accumulation +
        //       model pruning — IDENTICAL to WithinChunkTemplateMatch's
        //       post-merge logic.  Copy-paste rather than refactor:
        //       both functions live in the same file and either could
        //       evolve independently (e.g. if WithinChunkTemplateMatch
        //       grows new gate semantics, the k-NN variant might not
        //       inherit them automatically — that's intentional, the
        //       user chose which version to run via the flag).
        std::unordered_map<int,int> rootToCanonIdx;
        for (int a = 0; a < n; a++) {
            int root = Find(a);
            auto it = rootToCanonIdx.find(root);
            if (it == rootToCanonIdx.end() ||
                mdls[static_cast<size_t>(a)].localClusterId <
                mdls[static_cast<size_t>(it->second)].localClusterId)
                rootToCanonIdx[root] = a;
        }
        std::unordered_map<int,int> lcRemap;
        for (int a = 0; a < n; a++) {
            int canon = mdls[static_cast<size_t>(
                rootToCanonIdx[Find(a)])].localClusterId;
            lcRemap[mdls[static_cast<size_t>(a)].localClusterId] = canon;
        }

        // Capture merged canonicals + identity-flow counters before
        // the cls remap.  Mirrors the equivalent block in
        // WithinChunkTemplateMatch.
        std::unordered_set<int> mergedCanonicalLcs;
        int chunkAbsorbedClusters = 0;
        {
            std::unordered_map<int,int> canonAbsorbCount;
            for (auto& [src, canon] : lcRemap) {
                if (src != canon) {
                    canonAbsorbCount[canon]++;
                    ++chunkAbsorbedClusters;
                }
            }
            for (auto& [canon, cnt] : canonAbsorbCount) {
                if (cnt > 0) mergedCanonicalLcs.insert(canon);
            }
        }
        callCanonicalsTouched += static_cast<int>(mergedCanonicalLcs.size());
        callAbsorbedClusters  += chunkAbsorbedClusters;

        int chunkSpikesRelabeled = 0;
        // Incremental realign (patch 0054): record GLOBAL spike IDs newly
        // absorbed into each canonical this merge.  Only these need
        // realigning against the canonical's updated template.
        std::unordered_map<int, std::vector<int>> canonNewSpikes;
        const bool _wantIncremental =
            (MergeRealignEnable != 0 && MergeRealignIncremental != 0
             && m_timeShiftReady);
        for (size_t _i = 0; _i < cls.size(); ++_i) {
            int& lc = cls[_i];
            auto it = lcRemap.find(lc);
            if (it != lcRemap.end() && it->second != lc) {
                const int canon = it->second;
                if (_wantIncremental)
                    canonNewSpikes[canon].push_back(
                        chunkPoints[static_cast<size_t>(ck)][_i]);
                lc = canon;
                ++chunkSpikesRelabeled;
            }
        }
        callSpikesRelabeled += chunkSpikesRelabeled;

        auto wmerge = [](std::vector<int16_t>& d, int& dN,
                         const std::vector<int16_t>& s, int sN) {
            if (s.empty() || sN <= 0) return;
            if (d.empty() || dN <= 0) { d = s; dN = sN; return; }
            const size_t L = d.size();
            for (size_t e = 0; e < L; e++) {
                const int64_t num = static_cast<int64_t>(d[e]) * dN
                                  + static_cast<int64_t>(s[e]) * sN;
                d[e] = static_cast<int16_t>(num / (dN + sN));
            }
            dN += sN;
        };

        // Accumulate merged-away models into canonicals
        for (auto& [root, canonIdx] : rootToCanonIdx) {
            auto& dst = mdls[static_cast<size_t>(canonIdx)];
            int runningN      = dst.nMembers;
            int runningNLeft  = dst.nMembersLeft;
            int runningNRight = dst.nMembersRight;
            for (int a = 0; a < n; a++) {
                if (a == canonIdx) continue;
                if (Find(a) != root) continue;
                auto& src = mdls[static_cast<size_t>(a)];
                wmerge(dst.meanWav,      runningN,      src.meanWav,      src.nMembers);
                wmerge(dst.meanWavLeft,  runningNLeft,  src.meanWavLeft,  src.nMembersLeft);
                wmerge(dst.meanWavRight, runningNRight, src.meanWavRight, src.nMembersRight);
            }
            dst.nMembersLeft  = runningNLeft;
            dst.nMembersRight = runningNRight;
        }

        // Remove merged-away ChunkModels
        std::unordered_set<int> keepLc;
        for (auto& [root, idx2] : rootToCanonIdx)
            keepLc.insert(mdls[static_cast<size_t>(idx2)].localClusterId);
        mdls.erase(std::remove_if(mdls.begin(), mdls.end(),
            [&](const ChunkModel& cm){ return !keepLc.count(cm.localClusterId); }),
            mdls.end());

        // Recompute nMembers on surviving models
        for (auto& cm : mdls) {
            cm.nMembers = 0;
            for (const auto& lc : cls)
                if (lc == cm.localClusterId) cm.nMembers++;
        }

        // Post-merge realignment (see WithinChunkTemplateMatch for the
        // rationale; identical algorithm here).
        if (MergeRealignEnable != 0 && m_timeShiftReady
            && !mergedCanonicalLcs.empty()) {
            const int peakPos  = PeakSampleIndex;
            const int maxShift = std::max(1,
                std::min(nSamplesPerSpike / 4, KlustersRealignMaxShift));
            const int minSize  = std::max(2, KlustersRealignMinSize);
            // Lookup canonLc -> model index for the incremental template.
            std::unordered_map<int,int> _lcToIdx;
            if (MergeRealignIncremental != 0) {
                for (int _a = 0; _a < n; ++_a)
                    _lcToIdx[mdls[static_cast<size_t>(_a)].localClusterId] = _a;
            }
            for (int canonLc : mergedCanonicalLcs) {
                if (MergeRealignIncremental != 0) {
                    // Incremental: align ONLY the spikes just absorbed
                    // into this canonical, against the canonical's
                    // post-wmerge meanWav (its actual shape) -- not a
                    // template re-derived from the few absorbed spikes,
                    // and not the whole canonical (wasteful).
                    auto _ns = canonNewSpikes.find(canonLc);
                    if (_ns == canonNewSpikes.end() || _ns->second.empty())
                        continue;
                    auto _mi = _lcToIdx.find(canonLc);
                    if (_mi == _lcToIdx.end()) continue;
                    const auto& _tmpl =
                        mdls[static_cast<size_t>(_mi->second)].meanWav;
                    if (static_cast<int>(_tmpl.size()) != wElems) continue;
                    RealignSpikesAgainstTemplate(
                        _ns->second, _tmpl.data(), nChan, nSamplesPerSpike,
                        peakPos, maxShift,
                        chunkChangedSpikes[static_cast<size_t>(ck)], chunkStats[static_cast<size_t>(ck)]);
                    ++mergeRealignClustersTouched;
                    continue;
                }
                std::vector<int> spikeIds;
                spikeIds.reserve(256);
                for (size_t i = 0; i < cls.size(); ++i) {
                    if (cls[i] == canonLc) {
                        spikeIds.push_back(
                            chunkPoints[static_cast<size_t>(ck)][i]);
                    }
                }
                if (static_cast<int>(spikeIds.size()) < minSize) continue;
                KlustersStyleRealignOneCluster(
                    spikeIds, nChan, nSamplesPerSpike,
                    peakPos, maxShift, minSize,
                    chunkChangedSpikes[static_cast<size_t>(ck)], chunkStats[static_cast<size_t>(ck)]);
                ++mergeRealignClustersTouched;
            }
        }
    }

    // ── Gather: concatenate per-chunk changed spikes; sum / max stats.
    //    Sums for counters; max for maxAbsShift; weighted mean for
    //    meanAbsShift (weighted by nSpikesRealigned per chunk).
    std::vector<int> mergeRealignChangedSpikes;
    KlustersRealign::RealignStats mergeRealignStats;
    {
        size_t total = 0;
        for (const auto& v : chunkChangedSpikes) total += v.size();
        mergeRealignChangedSpikes.reserve(total);
        double weightedShift = 0.0;
        for (size_t k = 0; k < chunkChangedSpikes.size(); ++k) {
            mergeRealignChangedSpikes.insert(
                mergeRealignChangedSpikes.end(),
                chunkChangedSpikes[k].begin(), chunkChangedSpikes[k].end());
            const auto& s = chunkStats[k];
            mergeRealignStats.nClustersProcessed += s.nClustersProcessed;
            mergeRealignStats.nClustersSkipped   += s.nClustersSkipped;
            mergeRealignStats.nSpikesEvaluated   += s.nSpikesEvaluated;
            mergeRealignStats.nSpikesRealigned   += s.nSpikesRealigned;
            mergeRealignStats.nSpikesReadFailed  += s.nSpikesReadFailed;
            if (s.maxAbsShift > mergeRealignStats.maxAbsShift)
                mergeRealignStats.maxAbsShift = s.maxAbsShift;
            weightedShift += s.meanAbsShift * s.nSpikesRealigned;
        }
        if (mergeRealignStats.nSpikesRealigned > 0)
            mergeRealignStats.meanAbsShift =
                weightedShift / mergeRealignStats.nSpikesRealigned;
    }

    if (!mergeRealignChangedSpikes.empty()) {
        RefeaturizeChangedSpikes(mergeRealignChangedSpikes,
                                  nChan, nSamplesPerSpike);
        LockedStderr(
            "  [WithinChunkTemplateMatchMedianKnn] post-merge realign: "
            "%d clusters touched, %d/%d spikes shifted (max|Δ|=%d), "
            "%zu spikes refeaturized\n",
            mergeRealignClustersTouched,
            mergeRealignStats.nSpikesRealigned,
            mergeRealignStats.nSpikesEvaluated,
            mergeRealignStats.maxAbsShift,
            mergeRealignChangedSpikes.size());
    }

    Output("WithinChunkTemplateMatchMedianKnn (K=%d): %d cluster pair(s) "
           "merged across all chunks  [identity flow: %d spikes relabeled, "
           "%d canonicals absorbed %d clusters]\n",
           K, totalMerged,
           callSpikesRelabeled,
           callCanonicalsTouched,
           callAbsorbedClusters);
    return totalMerged;
}








