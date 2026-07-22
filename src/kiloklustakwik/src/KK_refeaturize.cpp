/***************************************************************************
                   KK_refeaturize.cpp
                   ------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Shift-probe refeaturization for the KK class (refine an existing clustering,
 recompute features from per-spike time shifts, refeaturize changed spikes,
 maintain the .fil group cache), split out of KK.cpp.  Member definitions of
 the KK class declared in KK.h; no interface or behaviour change.
 RefineExistingClustering uses kMaxStackDims from KK_internal.h.
 ***************************************************************************/
#include "KK.h"
#include "KK_internal.h"
#include "KlustaKwik.h"
#include "dipsplit.h"
#include "realign_xcorr.h"
#include "realign_center.h"
#include "klusters_realign.h"
#include <neurosuite/core/pca_projection.hpp>  // canonical PCAE loader (block-wise, v1+v2)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <set>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <omp.h>


// ===========================================================================
// RefineExistingClustering
//
// Curate a hand-edited or previously-sorted .clu using the existing centroids
// and per-cluster covariances as Gaussian priors.  The motivation: a curated
// .clu encodes a lot of information (the operator's manual splits/merges,
// the previous KK run's parameter sweep) that a fresh CEM run would discard.
// Refinement preserves that work and only changes assignments where the
// existing model says they are inconsistent.
//
// Convention: cluster ids 0 (artefact) and 1 (MUA) are not parents under the
// neurosuite convention.  When lockNoiseClu is true (default), no spike is
// ever moved INTO or OUT OF clusters 0 / 1, and those clusters are not
// considered for split or merge.  This matches what the user expects when
// running refinement after a triage pass that put bad spikes in 0/1.
//
// Drift-aware merge gate: when chunkBoundsSec is non-empty, each cluster's
// temporal occupancy is computed (histogram of Data[timeDim] over the chunk
// boundaries).  Two clusters whose occupancy lies in disjoint chunk sets
// are MORE likely to be the same drifting unit (the unit moved across the
// probe and the original sort failed to reconnect the trajectories), so
// merge on a relaxed threshold; co-occurring clusters need stronger
// evidence.  The gate factor is min(1.5, 1.0 + bhattacharyya_overlap),
// where bhattacharyya_overlap = sum_k sqrt(p_k * q_k) over chunks k.
//
// Returns the final score (lower is better, same convention as CEM).
// ===========================================================================
float KK::RefineExistingClustering(
    const char* cluFile,
    const char* mode,
    int   nIters,
    float mergeThresh,
    float splitMinDepth,
    bool  lockNoiseClu,
    const std::vector<float>& chunkBoundsSec)
{
    if (!cluFile || !*cluFile) {
        Error("RefineExistingClustering: cluFile is empty\n");
    }
    const std::string modeStr = mode ? mode : "full";
    const bool wantReassign = (modeStr != "off");
    const bool wantSplit    = (modeStr == "split" || modeStr == "full");
    const bool wantMerge    = (modeStr == "merge" || modeStr == "full");
    if (!wantReassign && !wantSplit && !wantMerge) {
        Output("RefineExistingClustering: mode=off — no-op\n");
        return ComputeScore();
    }

    // ── Load existing clustering ─────────────────────────────────────────────
    Output("\n[Refine] loading seed clustering from %s\n", cluFile);
    LoadClu(cluFile);
    Reindex();
    Output("[Refine]   K_in = %d clusters; %d points; %d dims\n",
           nClustersAlive, nPoints, nDims);

    // ── Phase A: REASSIGN ────────────────────────────────────────────────────
    // Run a constrained EM loop.  We re-use RunEMLoop but with splits disabled
    // (TrySplits would defeat the "warm-start" guarantee — it would explore
    //  K_in+1, K_in+2, ... configurations).  Iteration cap = nIters; the
    // existing ChangedThresh / FullStepEvery still apply, so loops that
    // converge early bail out with no work.
    //
    // A point can only move from cluster A to B if the Mahalanobis penalty
    // says B fits better than A — that's exactly what EStep + CStep already
    // do.  No additional gating needed: the warm models from MStep are
    // compact relative to the data, so spurious cross-cluster jumps are
    // rare unless the existing assignment was genuinely wrong.
    // Both arms below assign `score` unconditionally before any read, so no
    // initial value is needed.
    float score;
    if (wantReassign) {
        Output("[Refine] Phase A — reassign (%d iters max)\n",
               nIters > 0 ? nIters : MaxIter);
        const int saveSplitEvery = SplitEvery;
        SplitEvery = 0;     // disable all splits inside RunEMLoop
        score = RunEMLoop(
            /*enableSplits=*/   false,
            /*enableDistDump=*/ false,
            /*maxIter=*/        nIters > 0 ? nIters : MaxIter,
            /*phaseLabel=*/     "[Refine A]");
        SplitEvery = saveSplitEvery;
        Output("[Refine]   K_after_A = %d  score = %.4g\n", nClustersAlive, score);
    } else {
        // Even when reassign is disabled, we need fresh M-step models for
        // split/merge to operate on — otherwise Mean / Cov reflect whatever
        // state the previous CEM call left behind.
        MStep();
        score = ComputeScore();
    }

    // ── Phase B: SPLIT ───────────────────────────────────────────────────────
    // Reuse the existing DipSplit machinery.  DipSplitPhase iterates alive
    // clusters and probes for bimodal structure (BIC-gated, k-means refined,
    // valley-depth threshold).  The user controls the depth threshold via
    // RefineSplitMinDepth.
    if (wantSplit) {
        Output("[Refine] Phase B — DipSplit on %d clusters (min depth %.2f)\n",
               nClustersAlive, splitMinDepth);
        const float saveValleyThresh = DipSplitValleyThresh;
        const int   saveDipEnable    = DipSplitEnable;
        DipSplitValleyThresh = splitMinDepth;
        DipSplitEnable       = 1;
        // Honor the global-Phase-8 disable here too — if the user has
        // explicitly disabled Phase 8 (drift-resistant mode), don't
        // re-enable it via the refine path's local override.
        if (DipSplitGlobalEnable != 0) {
            DipSplitPhase();
        }
        DipSplitValleyThresh = saveValleyThresh;
        DipSplitEnable       = saveDipEnable;
        // DipSplitPhase mutates Class[] but does not refresh M-step models,
        // so re-fit before the merge phase.
        MStep();
        score = ComputeScore();
        Output("[Refine]   K_after_B = %d  score = %.4g\n", nClustersAlive, score);
    }

    // ── Phase C: MERGE ───────────────────────────────────────────────────────
    if (wantMerge) {
        const int nSpatialDims = (nDims > 1) ? nDims - 1 : nDims;
        const int timeDim      = nDims - 1;

        // Build per-cluster temporal occupancy distribution over chunks (if
        // chunkBoundsSec was given).  occupancy[c] is a [nChunks]-vector of
        // normalised counts (sums to 1 for each non-empty cluster).
        const int nChunks =
            (chunkBoundsSec.size() >= 2)
            ? static_cast<int>(chunkBoundsSec.size()) - 1
            : 0;
        std::vector<std::vector<float>> occupancy(MaxPossibleClusters);
        if (nChunks > 0) {
            // Convert raw-sample chunk boundaries to normalised-time
            // boundaries (Data[timeDim] is already in normalised [0,1] coords
            // for chunked-CEM consumers; we use the same convention here).
            const float sessionSamples = timeRawMax - timeRawMin;
            std::vector<float> normBounds(chunkBoundsSec.size());
            if (sessionSamples > 0.0f && SamplingRate > 0.0f) {
                for (size_t i = 0; i < chunkBoundsSec.size(); ++i)
                    normBounds[i] = (chunkBoundsSec[i] * SamplingRate - timeRawMin)
                                    / sessionSamples;
            } else {
                // Fallback: spread chunks uniformly.  Mostly hit during
                // unit tests where SamplingRate isn't set; in production
                // KlustaKwik.cpp ensures both fields are populated.
                for (int i = 0; i <= nChunks; ++i)
                    normBounds[i] = static_cast<float>(i) / nChunks;
            }

            for (int cc = 0; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                occupancy[c].assign(nChunks, 0.0f);
            }
            for (int p = 0; p < nPoints; ++p) {
                const int c = Class[p];
                if (occupancy[c].empty()) continue;  // not alive
                const float t = Data[p * nDims + timeDim];
                // upper_bound — chunk index = idx-1, clamp to [0, nChunks-1]
                int k = static_cast<int>(
                    std::upper_bound(normBounds.begin(), normBounds.end(), t)
                    - normBounds.begin()) - 1;
                if (k < 0)        k = 0;
                if (k >= nChunks) k = nChunks - 1;
                occupancy[c][k] += 1.0f;
            }
            for (int cc = 0; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                float sum = 0.0f;
                for (float v : occupancy[c]) sum += v;
                if (sum > 0.0f)
                    for (float& v : occupancy[c]) v /= sum;
            }
        }

        // Mahalanobis distance between cluster a and b in spatial dims, using
        // tgt's covariance (matches MergeChunkModels convention).  Returns
        // HugeScore if Cholesky fails.
        auto mahalDist = [&](int a, int b) -> float {
            if (nSpatialDims > kMaxStackDims) return HugeScore;
            float diff[kMaxStackDims];
            for (int d = 0; d < nSpatialDims; ++d)
                diff[d] = Mean[a*nDims + d] - Mean[b*nDims + d];
            float covB[kMaxStackDims*kMaxStackDims];
            float chol[kMaxStackDims*kMaxStackDims];
            float root[kMaxStackDims];
            for (int r = 0; r < nSpatialDims; ++r)
                for (int c = r; c < nSpatialDims; ++c)
                    covB[r*nSpatialDims + c] = Cov[b*nDims2 + r*nDims + c];
            if (Cholesky(covB, chol, nSpatialDims)) return HugeScore;
            TriSolve(chol, diff, root, nSpatialDims);
            float dist = 0.0f;
            for (int d = 0; d < nSpatialDims; ++d) dist += root[d] * root[d];
            return dist;
        };

        // Bhattacharyya overlap of two normalised occupancy distributions.
        // Returns 0 (disjoint) to 1 (identical).
        auto temporalOverlap = [&](int a, int b) -> float {
            if (occupancy[a].empty() || occupancy[b].empty()) return 1.0f;
            float bc = 0.0f;
            for (int k = 0; k < nChunks; ++k)
                bc += std::sqrt(occupancy[a][k] * occupancy[b][k]);
            return bc;
        };

        // Effective Mahalanobis threshold for this pair: relaxed when
        // temporal overlap is low (drift case), tightened when high.  Multiplier
        // ranges from 1.0 (full overlap) to 1.5 (disjoint), capped so we don't
        // merge truly different units that happen to be in different chunks.
        auto pairThresh = [&](int a, int b) -> float {
            if (nChunks <= 1) return mergeThresh;
            const float ov  = temporalOverlap(a, b);   // [0,1]
            const float mul = 1.0f + 0.5f * (1.0f - ov);
            return mergeThresh * mul;
        };

        Output("[Refine] Phase C — pairwise merge "
               "(spatialDims=%d, baseThresh=%.2f, %d chunks)\n",
               nSpatialDims, mergeThresh, nChunks);

        // Iterate until no more merges are accepted.  Each merge rebuilds the
        // alive set and re-fits MStep so subsequent pairs see correct stats.
        int mergePass = 0;
        int mergesThisRun = 0;
        const int kFirstParent = lockNoiseClu ? 2 : 0;
        while (true) {
            mergePass++;
            int nMergedThisPass = 0;

            // Snapshot the alive list at top of pass — Reindex() may mutate
            // mid-pass when ClassAlive[] is updated.
            std::vector<int> alive;
            alive.reserve(nClustersAlive);
            for (int cc = 0; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                if (c >= kFirstParent) alive.push_back(c);
            }

            // Score all candidate pairs by Mahalanobis distance, then process
            // closest-first.  Sorting once per pass is O(K² log K) which is
            // negligible — typical K is < 200.
            struct PairCand {
                int   a, b;
                float dist;
                float thresh;
            };
            std::vector<PairCand> cands;
            cands.reserve(alive.size() * (alive.size() - 1) / 2);
            for (size_t ia = 0; ia < alive.size(); ++ia) {
                for (size_t ib = ia + 1; ib < alive.size(); ++ib) {
                    const int a = alive[ia], b = alive[ib];
                    // Mahalanobis under both covariances; take the smaller —
                    // matches the "either fits the merged cloud" intuition.
                    const float dAB = mahalDist(a, b);
                    const float dBA = mahalDist(b, a);
                    const float d   = std::min(dAB, dBA);
                    const float th  = pairThresh(a, b);
                    if (d < th) cands.push_back({a, b, d, th});
                }
            }
            std::sort(cands.begin(), cands.end(),
                      [](const PairCand& x, const PairCand& y) {
                          return x.dist < y.dist;
                      });

            // Track which clusters have been merged this pass — once a
            // cluster is consumed, don't merge it again until the next pass
            // (so we operate on consistent stats).
            std::vector<char> consumed(MaxPossibleClusters, 0);
            for (const auto& pc : cands) {
                if (consumed[pc.a] || consumed[pc.b]) continue;

                // BIC gate: build labels from the candidate pair, run
                // bic_two_vs_one over the spatial dims.  If k=1 BIC wins
                // by >= 0, the merger is favoured.
                std::vector<int>   memberRows;
                std::vector<int>   memberLabels;
                memberRows.reserve(2048);
                memberLabels.reserve(2048);
                for (int p = 0; p < nPoints; ++p) {
                    if (Class[p] == pc.a)      { memberRows.push_back(p);
                                                 memberLabels.push_back(0); }
                    else if (Class[p] == pc.b) { memberRows.push_back(p);
                                                 memberLabels.push_back(1); }
                }
                const int M = static_cast<int>(memberRows.size());
                if (M < 2 * nSpatialDims) continue;  // too small for BIC

                // Pack spatial features into contiguous float buffer for
                // dipsplit::bic_two_vs_one (which takes float* rows × dim).
                std::vector<float> X(static_cast<size_t>(M) * nSpatialDims);
                for (int i = 0; i < M; ++i) {
                    const int p = memberRows[i];
                    for (int d = 0; d < nSpatialDims; ++d)
                        X[static_cast<size_t>(i) * nSpatialDims + d] =
                            Data[p * nDims + d];
                }
                const dipsplit::BicPair bp = dipsplit::bic_two_vs_one(
                    X.data(), M, nSpatialDims, memberLabels.data());

                // Lower BIC is better.  Accept merger if k=1 BIC <= k=2 BIC
                // (a tie still merges — this is the "absent evidence" case
                // and we lean toward fewer clusters by design).  Override
                // by raising RefineMergeThresh if you want the merge to be
                // stricter — that gates in pairThresh first.
                if (bp.bic_k1 > bp.bic_k2) {
                    // Two-cluster fit is significantly better — keep them.
                    continue;
                }

                // Commit: relabel all spikes from b to a, mark b dead,
                // mark both as consumed for this pass.
                for (int p = 0; p < nPoints; ++p)
                    if (Class[p] == pc.b) Class[p] = pc.a;
                ClassAlive[pc.b] = 0;
                consumed[pc.a] = consumed[pc.b] = 1;
                nMergedThisPass++;
                mergesThisRun++;

                if (Verbose >= 1) {
                    Output("[Refine C]   merged %d <- %d  (mahal²=%.2f, "
                           "thresh=%.2f, BIC₁=%.1f BIC₂=%.1f, ov=%.2f)\n",
                           pc.a, pc.b, pc.dist, pc.thresh,
                           bp.bic_k1, bp.bic_k2,
                           (nChunks > 1) ? temporalOverlap(pc.a, pc.b) : 1.0f);
                }
            }

            if (nMergedThisPass == 0) break;
            // Refresh stats so the next pass sees post-merge models.
            Reindex();
            MStep();
            Output("[Refine C] pass %d: %d merges accepted; K=%d\n",
                   mergePass, nMergedThisPass, nClustersAlive);
        }
        if (mergesThisRun == 0) {
            Output("[Refine C] no merges accepted; K=%d\n", nClustersAlive);
        }
        score = ComputeScore();
        Output("[Refine]   K_after_C = %d  score = %.4g\n", nClustersAlive, score);
    }

    // ── Final tidy ───────────────────────────────────────────────────────────
    // One last reassign pass: after split/merge, points near the new
    // cluster boundaries can have stale assignments.  Single iteration is
    // enough since Mean/Cov are already converged for the current
    // partition.
    if (wantSplit || wantMerge) {
        const int saveSplitEvery = SplitEvery;
        SplitEvery = 0;
        score = RunEMLoop(
            /*enableSplits=*/   false,
            /*enableDistDump=*/ false,
            /*maxIter=*/        std::max(2, std::min(nIters, 5)),
            /*phaseLabel=*/     "[Refine final]");
        SplitEvery = saveSplitEvery;
    }

    Output("[Refine] done — K_final=%d, score=%.4g\n", nClustersAlive, score);
    return score;
}


// ---------------------------------------------------------------------------
// RefeaturizeFromShifts
//
// For each spike with a non-zero cumulative shift (set by the shift
// probe), re-extracts the aligned waveform from the .fil broadband
// file at the corrected sample offset, projects through the saved PCA
// eigenvectors, re-normalises, and writes back into Data[].  Called by
// Phase 9 (TimeShiftFinalize).
//
// Re-extracting from .fil rather than circular-shifting the .spk waveform
// eliminates wrap-around corruption: circular shift of N samples by sh
// fills positions [N-sh .. N-1] with noise from the beginning of the
// original window, which corrupts up to 30% of samples for typical
// 5–10 sample shifts.  Reading from .fil at (rawTs + sh - peakIdx)
// gives a clean, artifact-free aligned waveform from the broadband signal.
//
// Requires NbTotalChannels, GroupChannelIds, and PeakSampleIndex to be
// set (auto-filled from YAML at startup).  Falls back to circular shift
// from .spk when .fil is unavailable.
// ---------------------------------------------------------------------------
void KK::RefeaturizeFromShifts(const std::vector<int>& spikeShifts,
                                 int nChan, int nSamplesPerSpike)
{
    if (spikeShifts.empty() || nChan <= 0 || nSamplesPerSpike <= 0) return;

    // ── Load PCA model ────────────────────────────────────────────────────
    // Prefer canonical .pca.N; fall back to .pcaD.N (stderiv pipeline).
    char pcaPath[STRLEN + 16];
    pickInputPath(pcaPath, sizeof(pcaPath), FileBase, "pca", ElecNo);

    struct PcaModel {
        int nChan, data2use, nComp, recShift;
        bool isCentered;
        std::vector<std::vector<double>> mean;
        std::vector<std::vector<double>> eigvec;
    } pm;

    {
        // PCAE loader (libneurosuite-core): block-wise body, magic+version
        // checked, v1 and v2 both accepted.  Replaces the inline legacy 5-int
        // reader, which would silently fail once process_pca writes PCAE.
        neurosuite::core::PcaBasis basis;
        if (!neurosuite::core::loadPca(pcaPath, basis)) {
            Output("RefeaturizeFromShifts: %s not found or not a valid PCAE basis "
                   "— skipping re-projection\n", pcaPath);
            return;
        }
        pm.nChan = basis.nCh; pm.data2use = basis.data2use; pm.nComp = basis.nComp;
        pm.recShift = basis.recShift; pm.isCentered = basis.centered;

        Output("RefeaturizeFromShifts: PCA model — nChan=%d data2use=%d nComp=%d recShift=%d isCentered=%d\n",
               pm.nChan, pm.data2use, pm.nComp, pm.recShift, (int)pm.isCentered);

        if (pm.nChan != nChan) {
            Output("RefeaturizeFromShifts: PCA has %d channels, spike group has %d — "
                   "skipping\n", pm.nChan, nChan);
            return;
        }
        pm.mean   = basis.means;   // [ch][data2use]
        pm.eigvec = basis.evec;    // [ch][data2use*nComp], col-major
    }

    // Sanity check: print first mean and eigenvector values
    if (!pm.mean.empty() && !pm.mean[0].empty())
        Output("RefeaturizeFromShifts: ch0 mean[0]=%.4g ev[0]=%.4g\n",
               pm.mean[0][0],
               (!pm.eigvec.empty() && !pm.eigvec[0].empty()) ? pm.eigvec[0][0] : 0.0);

    const int nPCAFeatures = pm.nChan * pm.nComp;
    if (nPCAFeatures > nDims - 1) {
        Output("RefeaturizeFromShifts: PCA feature count (%d) exceeds nDims-1 (%d) — "
               "skipping\n", nPCAFeatures, nDims - 1);
        return;
    }

    // Defensive bounds guard: the projection below reads wave[(recShift+s)*nChan+ch]
    // for s in [0,data2use), and wave holds only nChan*nSamplesPerSpike samples.
    // If the PCA window [recShift, recShift+data2use) exceeds the spike length
    // (mismatched PCA model) this would read out of bounds -> skip instead.
    if (pm.recShift < 0 || pm.recShift + pm.data2use > nSamplesPerSpike) {
        Output("RefeaturizeFromShifts: PCA window [recShift %d, +%d) exceeds spike "
               "length %d — skipping re-projection to avoid OOB\n",
               pm.recShift, pm.data2use, nSamplesPerSpike);
        return;
    }

    // ── Read raw timestamps directly from .res to avoid float precision loss ──
    // Recovering rawTs from the normalised float Data[timeDimIdx] introduces
    // up to ±13 samples of error for late-session spikes (timestamp ~1.17×10^8
    // at 32552 Hz × 1 hour; float has only ~7 significant digits).
    // Klusters reads directly from .res as int64 — we do the same.
    // ── Prefer .fil re-extraction; fall back to .spk circular shift ───────
    const bool canUseFil = (NbTotalChannels > 0 &&
                            !GroupChannelIds.empty() &&
                            PeakSampleIndex >= 0);
    const float sessionSamples = timeRawMax - timeRawMin;
    const int   timeDimIdx     = nDims - 1;
    const int   waveSamples    = nChan * nSamplesPerSpike;

    // Open .res for exact int64 timestamps
    char resPathRFS[STRLEN + 16];
    resolveAnyC(resPathRFS, sizeof(resPathRFS), FileBase, "res", ElecNo);
    FILE* resRFS = fopen(resPathRFS, "rb");

    char filPath[STRLEN + 8], spkPath[STRLEN + 16];
    snprintf(filPath, sizeof(filPath), "%s.fil",     FileBase);
    // Prefer canonical .spk.N; fall back to .spkD.N.  The .spk circular-shift
    // fallback (used when .fil is unavailable) still uses this handle.
    pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo);

    FILE* filFp = canUseFil ? fopen(filPath, "rb") : nullptr;
    FILE* spkFp = nullptr;
    if (!filFp) {
        // .fil unavailable — fall back to .spk circular shift
        spkFp = fopen(spkPath, "rb");
        if (!spkFp) {
            Output("RefeaturizeFromShifts: neither %s nor %s available — "
                   "skipping re-projection\n", filPath, spkPath);
            return;
        }
        Output("RefeaturizeFromShifts: .fil not available, using circular shift "
               "from .spk (may have minor wrap-around artefacts)\n");
    }

    std::vector<int16_t> wave(static_cast<size_t>(waveSamples));
    std::vector<int16_t> filRow;
    if (filFp) filRow.resize(static_cast<size_t>(NbTotalChannels));

    int nReproj = 0, nSkipped = 0;

    for (int p = 0; p < nPoints; ++p) {
        const int sh = (p < static_cast<int>(spikeShifts.size()))
                     ? spikeShifts[p] : 0;
        if (sh == 0 || sh == std::numeric_limits<int>::min()) { ++nSkipped; continue; }

        // Read exact int64 timestamp from .res; fall back to float if unavailable
        int64_t rawTsInt = 0;
        if (resRFS) {
            fseeko(resRFS, static_cast<off_t>(p) * sizeof(int64_t), SEEK_SET);
            { size_t _r = fread(&rawTsInt, sizeof(int64_t), 1, resRFS); (void)_r; }
        }
        const float normTs = Data.m_Data[p * nDims + timeDimIdx];
        const float rawTs  = (rawTsInt > 0)
            ? static_cast<float>(rawTsInt)
            : normTs * sessionSamples + timeRawMin;

        if (filFp) {
            // ── .fil path: re-extract at corrected timestamp ───────────────
            // extTs = rawTs + sh: the window that, after rolling forward by sh,
            // presents the waveform with its peak at PeakSampleIndex.
            // Matches Klusters: startSample = ts - peakSamp0 where ts = oldTs + cumShift.
            const int64_t off  = (rawTsInt > 0 ? rawTsInt : static_cast<int64_t>(rawTs)) + sh - PeakSampleIndex;
            if (off < 0 || off + nSamplesPerSpike >
                    static_cast<int64_t>(sessionSamples) + 1) { ++nSkipped; continue; }

            fseeko(filFp, off * NbTotalChannels * 2, SEEK_SET);
            bool ok = true;
            for (int s = 0; s < nSamplesPerSpike && ok; s++) {
                if (fread(filRow.data(), 2, NbTotalChannels, filFp) !=
                        static_cast<size_t>(NbTotalChannels)) { ok = false; break; }
                for (int c = 0; c < nChan; c++) {
                    const int gc = (c < static_cast<int>(GroupChannelIds.size()))
                                 ? GroupChannelIds[c] : -1;
                    wave[s * nChan + c] = (gc >= 0 && gc < NbTotalChannels)
                                        ? filRow[gc] : 0;
                }
            }
            if (!ok) { ++nSkipped; continue; }

            // For stderiv sessions the basis lives in SDIFF_ALLPAIRS + temporal
            // first-difference space, so the raw voltages we just read must be
            // transformed before projection.  One in-place pass matching
            // process_extractspikes_stderiv exactly.
            if (m_timeShiftBasis.isStderiv) {
                ApplySdiffAllpairsTemporalDiff(wave.data(), nChan, nSamplesPerSpike);
            }
        } else {
            // ── .spk fallback: circular shift ─────────────────────────────
            off_t offset = static_cast<off_t>(p) * waveSamples * sizeof(int16_t);
            if (fseeko(spkFp, offset, SEEK_SET) != 0) { ++nSkipped; continue; }
            std::vector<int16_t> raw(static_cast<size_t>(waveSamples));
            if (fread(raw.data(), sizeof(int16_t), waveSamples, spkFp)
                    != static_cast<size_t>(waveSamples)) { ++nSkipped; continue; }
            const int N = nSamplesPerSpike;
            for (int s = 0; s < N; ++s) {
                const int src = ((s + sh) % N + N) % N;
                for (int c = 0; c < nChan; ++c)
                    wave[s * nChan + c] = raw[src * nChan + c];
            }
        }

        // ── Project through PCA and update Data[] ─────────────────────────
        // Eigenvector layout in PCAE file (written by process_pca):
        //   evecBuf[component * data2use + sample]  (col-major)
        // Correct indexing: ev[k * pm.data2use + s]
        // NOT ev[s * pm.nComp + k] — that would be row-major (wrong).
        float* dataRow = Data.m_Data + p * nDims;
        for (int ch = 0; ch < pm.nChan; ++ch) {
            const auto& mu = pm.mean[static_cast<size_t>(ch)];
            const auto& ev = pm.eigvec[static_cast<size_t>(ch)];
            for (int k = 0; k < pm.nComp; ++k) {
                double val = 0.0;
                for (int s = 0; s < pm.data2use; ++s) {
                    const int sIdx = pm.recShift + s;
                    double raw = static_cast<double>(
                        wave[static_cast<size_t>(sIdx * nChan + ch)]);
                    if (pm.isCentered) raw -= mu[static_cast<size_t>(s)];
                    val += ev[static_cast<size_t>(k * pm.data2use + s)] * raw;
                }
                const int featIdx = ch * pm.nComp + k;
                dataRow[featIdx] = (static_cast<float>(val) - dimMin_[featIdx])
                                   * dimRange_[featIdx];
            }
        }

        // Update .fet timestamp with extTs = rawTs + sh (exact int64)
        if (sessionSamples > 0.0f) {
            const int64_t baseTs = (rawTsInt > 0) ? rawTsInt : static_cast<int64_t>(rawTs);
            dataRow[timeDimIdx] = (static_cast<float>(baseTs + sh) - timeRawMin) / sessionSamples;
        }

        ++nReproj;
    }

    if (resRFS) fclose(resRFS);
    if (filFp) fclose(filFp);
    if (spkFp) fclose(spkFp);

    Output("RefeaturizeFromShifts: re-projected %d spikes via %s, skipped %d\n",
           nReproj, filFp ? ".fil" : ".spk (circular shift)", nSkipped);
}


// ---------------------------------------------------------------------------
// KK::EnsureFilGroupCache
//
// Lazily read <FileBase>.fil and extract just the group's channels into
// m_filGroupCache.  Returns true on success, false if .fil is missing
// or memory allocation fails.  Cached for the lifetime of the KK
// instance — subsequent calls are no-ops.
//
// Memory layout: m_filGroupCache[sample * nChan + c]
// where c indexes into GroupChannelIds.
// ---------------------------------------------------------------------------
bool KK::EnsureFilGroupCache(int nChan)
{
    if (!m_filGroupCache.empty() && m_filGroupNChan == nChan) return true;
    if (nChan <= 0 || NbTotalChannels <= 0) return false;

    char filPath[STRLEN + 8];
    snprintf(filPath, sizeof(filPath), "%s.fil", FileBase);
    FILE* fp = fopen(filPath, "rb");
    if (!fp) {
        Output("EnsureFilGroupCache: %s not found — cache not built\n", filPath);
        return false;
    }

    if (fseeko(fp, 0, SEEK_END) != 0) { fclose(fp); return false; }
    const int64_t filBytes = static_cast<int64_t>(ftello(fp));
    if (filBytes <= 0) { fclose(fp); return false; }
    const int64_t bytesPerSample = static_cast<int64_t>(NbTotalChannels) * 2;
    if (bytesPerSample <= 0 || filBytes % bytesPerSample != 0) {
        Output("EnsureFilGroupCache: %s size %lld not a multiple of "
               "NbTotalChannels(%d)*2 — cache not built\n",
               filPath, static_cast<long long>(filBytes), NbTotalChannels);
        fclose(fp); return false;
    }
    const int64_t totalSamples = filBytes / bytesPerSample;

    // Allocate the cache.  May throw bad_alloc on huge files; surface
    // that gracefully instead of crashing the whole run.
    try {
        m_filGroupCache.assign(
            static_cast<size_t>(totalSamples) * static_cast<size_t>(nChan), 0);
    } catch (const std::bad_alloc&) {
        Output("EnsureFilGroupCache: out of memory allocating %lld bytes "
               "(%lld samples × %d ch × 2) — falling back to streamed reads\n",
               static_cast<long long>(totalSamples * nChan * 2),
               static_cast<long long>(totalSamples), nChan);
        fclose(fp);
        return false;
    }

    if (fseeko(fp, 0, SEEK_SET) != 0) {
        m_filGroupCache.clear(); m_filGroupCache.shrink_to_fit();
        fclose(fp); return false;
    }

    // Sequential read in chunks; project each row through GroupChannelIds.
    // Block size tuned for SSD throughput (8 MiB / row stride).
    const int blockRows = std::max(1, static_cast<int>(
        (8 * 1024 * 1024) / std::max<int>(1, NbTotalChannels * 2)));
    std::vector<int16_t> buf(
        static_cast<size_t>(blockRows) * static_cast<size_t>(NbTotalChannels));

    int64_t doneSamples = 0;
    while (doneSamples < totalSamples) {
        const int64_t want = std::min<int64_t>(blockRows, totalSamples - doneSamples);
        const size_t  nRead = fread(buf.data(), bytesPerSample,
                                    static_cast<size_t>(want), fp);
        if (static_cast<int64_t>(nRead) != want) {
            Output("EnsureFilGroupCache: short read at sample %lld "
                   "(wanted %lld got %zu) — cache built partially\n",
                   static_cast<long long>(doneSamples),
                   static_cast<long long>(want), nRead);
            break;
        }
        // Extract group channels.  Cache layout: [sample][c].
        for (int64_t r = 0; r < want; ++r) {
            const int16_t* srcRow = &buf[static_cast<size_t>(r) * NbTotalChannels];
            int16_t*       dstRow = &m_filGroupCache[
                static_cast<size_t>((doneSamples + r) * nChan)];
            for (int c = 0; c < nChan; ++c) {
                dstRow[c] = srcRow[GroupChannelIds[static_cast<size_t>(c)]];
            }
        }
        doneSamples += want;
    }
    fclose(fp);

    m_filGroupSessionSamples = doneSamples;
    m_filGroupNChan          = nChan;

    const double mbCached = (m_filGroupCache.size() * 2) / (1024.0 * 1024.0);
    Output("EnsureFilGroupCache: cached %lld samples × %d channels "
           "(%.1f MiB) from %s\n",
           static_cast<long long>(doneSamples), nChan, mbCached, filPath);
    return true;
}


// ---------------------------------------------------------------------------
// KK::RefeaturizeChangedSpikes
//
// Selective version of RefeaturizeFromShifts.  Reads each listed
// spike's window from the group-channel .fil cache, applies the
// stderiv transform if needed, projects through the PCA model, and
// writes the result back into Data[].  m_cumShift[p] supplies the
// absolute shift (matching RefeaturizeFromShifts semantics).
//
// Uses the same PCA model loading + per-channel projection logic as
// RefeaturizeFromShifts but iterates only the spikes in
// changedSpikeIds, avoiding the O(nPoints) scan that's wasteful when
// only a handful of spikes changed in a Phase 4 iter.
// ---------------------------------------------------------------------------
void KK::RefeaturizeChangedSpikes(
        const std::vector<int>& changedSpikeIds,
        int nChan, int nSamplesPerSpike)
{
    if (changedSpikeIds.empty()) return;
    if (nChan <= 0 || nSamplesPerSpike <= 0) return;
    if (PeakSampleIndex < 0 || PeakSampleIndex >= nSamplesPerSpike) return;

    if (!EnsureFilGroupCache(nChan)) {
        // Fall back to the existing full-file reader by constructing a
        // per-spike shift vector and calling RefeaturizeFromShifts.
        // This costs an O(nPoints) scan but is correctness-preserving.
        std::vector<int> shifts(static_cast<size_t>(nPoints), 0);
        for (int p : changedSpikeIds) {
            if (p < 0 || p >= nPoints) continue;
            shifts[static_cast<size_t>(p)] =
                m_cumShift[static_cast<size_t>(p)];
        }
        RefeaturizeFromShifts(shifts, nChan, nSamplesPerSpike);
        return;
    }

    // ── Load PCA model via libneurosuite-core (PCAE, block-wise, v1+v2). ──
    //    This fixes two pre-existing bugs in the old inline reader: it read the
    //    body as float (process_pca writes double — so it consumed half the bytes
    //    as garbage eigenvectors), and it read the header as
    //    [nChan,data2use,nComp,recShift,isCentered], swapping recShift/isCentered
    //    relative to the legacy writer.  core reads the correct layout; the
    //    downstream projection keeps float, so the doubles are narrowed on copy.
    char pcaPath[STRLEN + 16];
    pickInputPath(pcaPath, sizeof(pcaPath), FileBase, "pca", ElecNo);
    neurosuite::core::PcaBasis pcaBasis;
    if (!neurosuite::core::loadPca(pcaPath, pcaBasis)) {
        Output("RefeaturizeChangedSpikes: %s not found or not a valid PCAE basis "
               "— skipping\n", pcaPath);
        return;
    }
    const int pcaNChan    = pcaBasis.nCh;
    const int pcaData2use = pcaBasis.data2use;
    const int pcaNComp    = pcaBasis.nComp;
    const int pcaRecShift = pcaBasis.recShift;
    const int pcaIsCentered = pcaBasis.centered ? 1 : 0;
    (void)pcaRecShift; (void)pcaIsCentered;
    if (pcaNChan != nChan) {
        Output("RefeaturizeChangedSpikes: PCA nChan=%d != spike-group nChan=%d "
               "— skipping\n", pcaNChan, nChan);
        return;
    }

    std::vector<std::vector<float>> pcaMean(pcaNChan);
    std::vector<std::vector<float>> pcaEv  (pcaNChan);
    for (int ch = 0; ch < pcaNChan; ++ch) {
        pcaMean[ch].assign(pcaBasis.means[ch].begin(), pcaBasis.means[ch].end());
        pcaEv[ch].assign(pcaBasis.evec[ch].begin(),  pcaBasis.evec[ch].end());
    }

    // ── Read int64 timestamps once (for affected spikes). ──
    char resPath[STRLEN + 8];
    resolveAnyC(resPath, sizeof(resPath), FileBase, "res", ElecNo);
    FILE* resFp = fopen(resPath, "rb");

    const int   waveSamples = nChan * nSamplesPerSpike;
    const int   timeDimIdx  = nDims - 1;
    std::vector<int16_t> wave(static_cast<size_t>(waveSamples));

    int nReproj = 0, nSkipped = 0;
    const int64_t sessionSamplesLocal = m_filGroupSessionSamples;

    for (int p : changedSpikeIds) {
        if (p < 0 || p >= nPoints) { ++nSkipped; continue; }
        const int sh = m_cumShift[static_cast<size_t>(p)];

        int64_t rawTsInt = 0;
        if (resFp) {
            fseeko(resFp, static_cast<off_t>(p) * sizeof(int64_t), SEEK_SET);
            { size_t _r = fread(&rawTsInt, sizeof(int64_t), 1, resFp); (void)_r; }
        }
        const float normTs = Data.m_Data[p * nDims + timeDimIdx];
        const float rawTs  = (rawTsInt > 0)
            ? static_cast<float>(rawTsInt)
            : normTs * static_cast<float>(sessionSamplesLocal);

        // Read shifted window from group-channel cache.
        const int64_t off = (rawTsInt > 0 ? rawTsInt : static_cast<int64_t>(rawTs))
                          + sh - PeakSampleIndex;
        if (off < 0 || off + nSamplesPerSpike > sessionSamplesLocal) {
            ++nSkipped; continue;
        }
        for (int s = 0; s < nSamplesPerSpike; ++s) {
            const int16_t* srcRow = &m_filGroupCache[
                static_cast<size_t>((off + s) * nChan)];
            for (int c = 0; c < nChan; ++c) {
                wave[s * nChan + c] = srcRow[c];
            }
        }
        if (m_timeShiftBasis.isStderiv) {
            ApplySdiffAllpairsTemporalDiff(wave.data(), nChan, nSamplesPerSpike);
        }

        // PCA projection (same math as RefeaturizeFromShifts).
        float* dataRow = Data.m_Data + p * nDims;
        for (int ch = 0; ch < pcaNChan; ++ch) {
            const auto& mu = pcaMean[ch];
            const auto& ev = pcaEv[ch];
            for (int k = 0; k < pcaNComp; ++k) {
                double val = 0.0;
                for (int s = 0; s < pcaData2use; ++s) {
                    const int sIdx = pcaRecShift + s;
                    double raw = static_cast<double>(
                        wave[static_cast<size_t>(sIdx * nChan + ch)]);
                    if (pcaIsCentered) raw -= mu[static_cast<size_t>(s)];
                    val += ev[static_cast<size_t>(k * pcaData2use + s)] * raw;
                }
                const int featIdx = ch * pcaNComp + k;
                dataRow[featIdx] = (static_cast<float>(val) - dimMin_[featIdx])
                                   * dimRange_[featIdx];
            }
        }
        // Update timestamp dim.
        if (sessionSamplesLocal > 0) {
            const int64_t baseTs = (rawTsInt > 0) ? rawTsInt
                                                   : static_cast<int64_t>(rawTs);
            dataRow[timeDimIdx] =
                static_cast<float>(baseTs + sh)
                / static_cast<float>(sessionSamplesLocal);
        }
        ++nReproj;
    }
    if (resFp) fclose(resFp);

    Output("RefeaturizeChangedSpikes: re-projected %d / %zu spikes "
           "from .fil group cache (%d skipped)\n",
           nReproj, changedSpikeIds.size(), nSkipped);
}
