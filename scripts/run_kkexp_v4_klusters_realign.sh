#!/usr/bin/env bash
# =============================================================================
# run_kkexp_v4_klusters_realign.sh
#
# Production-tier KKE command for sirotaA-jg-000005 group 6 with the full
# stack of patches 0001-0013 applied.  Notable from v3:
#   + Phase 7c (klusters-faithful per-spike realignment) ON — patch 0013
#   + GlobalMergeIter 500 (was 50) — let Phase 6 deletion loop converge
#   + MeanSubtractionMerge ON — Phase 6b catches residual mixed clusters
#   + Hull + DipSplitBeforePhase2b ON — Phase 2a.5 + 2a.6 splitters
#
# Total runtime budget (nphy-069): ~16-20 minutes for one nRuns=1 pass.
#
# What to inspect after the run:
#   • [Phase 7c] line: how many spikes got non-zero shifts
#       healthy:   mean|Δ| 0.3-1.5 samples
#       suspect:   mean|Δ| > 3.0 samples (upstream alignment was poor)
#       no-op:    "0/0 spikes shifted"  (probe not initialised, see logs)
#   • [Phase 7 quality] gini/maxFrac/condMax — same scalars as before
#       target:    condMax < 5e+04 (was 1.9e+06 with Hull alone)
#   • Final K from `awk '{print $1}' *.clu.6 | sort -u | wc -l`
#       target:    120-180 clusters (healthy octrode unit count)
#
# Visual inspection in klusters: cluster waveforms should now have peaks
# all on the same sample within each cluster (matches the cluster 31
# screenshot quality from the interactive realign).
# =============================================================================

set -euo pipefail

DATASET="sirotaA-jg-000005-20120312"
GROUP="6"
LOG="${DATASET}.kkexp.${GROUP}.v4.log"

KKEXP="${KKEXP:-KlustaKwikExp}"
if ! command -v "${KKEXP}" >/dev/null 2>&1; then
    echo "ERROR: ${KKEXP} not in PATH.  Set KKEXP=/path/to/KlustaKwikExp." >&2
    exit 1
fi

echo "Running ${KKEXP} on ${DATASET} group ${GROUP} — v4 (patches 0001-0013)"
echo "  Phase 2a.5 (DipSplit) + 2a.6 (Hull) + 7b (MeanSub) + 7c (klusters realign) ON"
echo "Log: ${LOG}"
echo

"${KKEXP}" "${DATASET}" "${GROUP}" \
  \
  `# ── core sweep + chunking ─────────────────────────────────────────── ` \
  -MaxClusters                       200    \
  -MaxPossibleClusters               10000  `# patch 0012 — required nChunks*MaxClusters` \
  -nRuns                             1      \
  -ChunkMinutes                      10     \
  -ChunkOverlapMinutes               5      \
  -ChunkPreseedFraction              0.15   \
  \
  -Screen                            1      `# show Output() warnings on stdout` \
  \
  `# ── Splitters: long-standing + Phase 2a.5/2a.6 ────────────────────── ` \
  -DipSplitEnable                    1      \
  -DipSplitMinSize                   50     \
  -SubspaceRecluster                 1      \
  -SubspaceDims                      6      \
  -DipSplitBeforePhase2b             1      `# patch 0008 — Phase 2a.5 per-chunk DipSplit` \
  -HullSplitEnable                   1      `# patch 0009 — Phase 2a.6 k-NN hull splitter` \
  -HullSplitK                        10     \
  -HullSplitMinComponentSize         75     \
  -HullSplitMutualReachScale         1.4    \
  -HullSplitUseMutualReach           1      \
  \
  `# ── NEW splitters that remain OFF (require more tuning) ──────────── ` \
  -KnnSplitPerChunkEnable            0      \
  \
  `# ── Phase 2b perf fixes (patch 0006/0007, KEPT) ──────────────────── ` \
  -Phase2bEnableSplits               0      \
  -Phase2bMaxIter                    60     \
  \
  `# ── Long-standing consolidators ───────────────────────────────────── ` \
  -TemplateMatchScore                0.90   \
  -TemplateMatchIters                10     \
  -CrossChunkTemplateScore           0.80   \
  -CrossChunkDriftSigma              1.5    \
  -GlobalMergeIter                   500    `# was 50 — Phase 6 needs room to converge` \
  -AdaptiveMerge                     1      \
  -TimeMergeIter                     100    \
  \
  `# ── Phase 6b mean-subtraction merge (was off in v3) ───────────────── ` \
  -MeanSubtractionMergeEnable        1      \
  -MeanSubtractionMergeThresh        0.10   \
  -MeanSubtractionMergeMaxShift      3      \
  \
  `# ── Phase 7c klusters-faithful realignment (NEW, patch 0013) ──────── ` \
  -KlustersRealignEnable             1      \
  -KlustersRealignMaxShift           8      `# klusters default` \
  -KlustersRealignMinSize            10     \
  \
  `# ── Legacy TimeShift: FULLY OFF (deferred 'random shifts' bug) ───── ` \
  -TimeShiftAlignIter                0      \
  -TimeShiftSplitEnable              0      \
  -TimeShiftMergeEnable              0      \
  -TimeShiftAlignAfterPhase1         0      \
  -TimeShiftAlignAfterPhase1b        0      \
  -TimeShiftAlignAfterPhase2         0      \
  -TimeShiftAlignAfterPhase4         0      \
  -TimeShiftAlignAfterPhase5         0      \
  -TimeShiftAlignPostMerge           0      \
  -EnergyCOMRealign                  0      \
  \
  -PenaltyMix                        0.0    \
  -DipSplitGlobalEnable              0      \
  -RandomSeed                        42     \
  -Verbose                           1      \
  2>&1 | tee "${LOG}"

echo
echo "──────────────────────────────────────────────────────────────────────"
echo "Run complete. Log: ${LOG}"
echo
echo "Diagnostic checks (in order of importance):"
echo
echo "  1. Did Phase 7c run and update any shifts?"
echo "     grep -E '\\[Phase 7c\\]' ${LOG}"
echo "     Expect:  'KlustersStyle realignment: N clusters processed, X/Y spikes shifted, mean|Δ|=...'"
echo
echo "  2. Final cluster quality"
echo "     grep -E '\\[Phase 7 quality\\]|\\[final quality\\]' ${LOG}"
echo "     Target: condMax < 5e+04, gini 0.55-0.70, alive 120-180"
echo
echo "  3. Phase 6 convergence (did it hit the 500-iter cap or converge naturally?)"
echo "     grep -E 'Phase 7 converged at iter|P3 iter 49[0-9]:' ${LOG} | tail"
echo "     Healthy: 'converged at iter N' where N < 500"
echo "     Bad:     last visible line is 'P3 iter 499:' (still hitting cap)"
echo
echo "  4. Final .clu cluster count"
echo "     awk '{print \$1}' ${DATASET}.clu.${GROUP} | sort -u | wc -l"
echo
echo "  5. RefeaturizeFromShifts: did .spk/.fet get rewritten?"
echo "     grep -E 'RefeaturizeFromShifts|TimeShiftFinalize' ${LOG}"
echo "     Expect: a 'refeaturize: N spikes re-extracted from .fil' line"
echo
echo "──────────────────────────────────────────────────────────────────────"
echo "Then open the .clu in klusters and compare a cluster's waveforms"
echo "panel against your cluster 31 screenshot — peaks should all sit on"
echo "the same sample within each cluster, with no per-spike jitter."
