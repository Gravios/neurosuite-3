#!/usr/bin/env bash
# =============================================================================
# run_kkexp_noshift_hullon.sh
#
# Diagnostic run for sirotaA-jg-000005-20120312 group 6.
#
# Purpose: isolate whether the "everything collapsed to noise" failure
# in the previous run came from the TimeShift alignment machinery.
# All TimeShift hooks (split probe, merge probe, alignment passes,
# post-merge alignment) are disabled.  EnergyCOMRealign was already off
# (user finding: less reliable than PCA-centering realign).
#
# Also enables Phase 2a.6 HullSplit (k-NN-graph connected components,
# patch 0009) with conservative defaults, and tightens the downstream
# consolidate thresholds (template-match score, cross-chunk score,
# mean-subtraction merge) to be LESS likely to over-merge real units.
#
# The per-phase CLUSTER STATE diagnostic (patch 0010) prints to stderr
# after each phase so we can see where K is growing vs shrinking.  If
# noise% jumps to 100% at any phase boundary, that phase is the
# culprit for the noise-collapse failure mode.
#
# Pre-conditions: patches 0001-0010 applied to neurosuite-3 and the
# binary rebuilt.  Run from the directory containing
# sirotaA-jg-000005-20120312.fet.6 / .spk.6 / .res.6 / .fil / .xml.
#
# Expected runtime on the user's nphy-069 (Ryzen 9800X3D, 16 threads):
#   ~5-15 min on 189k spikes / 36 chunks, depending on how aggressive
#   the split phases turn out to be.  Without TimeShift, Phase 3-9
#   skip all .spk re-extraction, saving substantial I/O.
# =============================================================================

set -euo pipefail

DATASET="sirotaA-jg-000005-20120312"
GROUP="6"
LOG="${DATASET}.kkexp.${GROUP}.noshift_hullon.log"

# Locate the binary; allow override via env.
KKEXP="${KKEXP:-KlustaKwikExp}"
if ! command -v "${KKEXP}" >/dev/null 2>&1; then
    echo "ERROR: ${KKEXP} not in PATH.  Set KKEXP=/path/to/KlustaKwikExp." >&2
    exit 1
fi

echo "Running ${KKEXP} on ${DATASET} group ${GROUP}"
echo "Log: ${LOG}"
echo "TimeShift: DISABLED   HullSplit: ENABLED   PenaltyMix: 0.5 (BIC-biased)"
echo

"${KKEXP}" "${DATASET}" "${GROUP}" \
  \
  `# ── core sweep + chunking (unchanged) ────────────────────────────────── ` \
  -MaxClusters                       200    \
  -nRuns                             30     \
  -ChunkMinutes                      10     \
  -ChunkOverlapMinutes               5      \
  -ChunkPreseedFraction              0.15   \
  \
  `# ── Phase 2b.5: klusters-faithful KNN (unchanged) ──────────────────── ` \
  -KnnSplitPerChunkEnable            1      \
  -KnnSplitMode                      1      \
  -KnnSplitK                         10     \
  -WaveKnnMajorityThreshold          0.65   `# tighter than 0.60 → more confidence req'd before reassignment` \
  -WaveKnnUseTraceFilter             1      \
  -WaveKnnResidualBecomesCluster     1      \
  -WaveKnnSkipMuaCluster1            0      \
  -WaveKnnNoiseSourceProbability     0.15   `# slightly lower than 0.20 → less noise-source probing` \
  -KnnSplitMinRefSize                75     `# was 50; tighter ⇒ fewer flimsy refs` \
  -KnnSplitMinSourceSize             75     \
  -KnnSplitMinNewClusterSize         75     \
  \
  `# ── Phase 4 within-chunk template matching ────────────────────────── ` \
  -TemplateMatchScore                0.92   `# was 0.90; tighter ⇒ fewer false merges` \
  -TemplateMatchIters                10     \
  \
  `# ── DipSplit + subspace recluster ─────────────────────────────────── ` \
  -DipSplitEnable                    1      \
  -DipSplitMinSize                   75     `# was 50; matches HullSplit floor` \
  -DipSplitBeforePhase2b             1      \
  -SubspaceRecluster                 1      \
  -SubspaceDims                      6      \
  \
  `# ── Phase 2a.6 HullSplit (NEW, patch 0009) ────────────────────────── ` \
  -HullSplitEnable                   1      \
  -HullSplitK                        10     \
  -HullSplitMinComponentSize         75     `# match DipSplitMinSize and KnnSplitMin*` \
  -HullSplitMutualReachScale         1.4    `# slightly tighter than the 1.5 default ⇒ fewer false splits` \
  -HullSplitUseMutualReach           1      \
  \
  `# ── Phase 5 cross-chunk merging (drift-aware) ─────────────────────── ` \
  -TimeMergeIter                     100    \
  -CrossChunkTemplateScore           0.85   `# was 0.80; tighter` \
  -CrossChunkDriftSigma              1.5    \
  -GlobalMergeIter                   50     \
  -AdaptiveMerge                     1      \
  -MeanSubtractionMergeEnable        1      \
  -MeanSubtractionMergeThresh        0.08   `# was 0.10; tighter ⇒ fewer mean-subtract merges` \
  -MeanSubtractionMergeMaxShift      3      \
  \
  `# ── Phase 2b inner CEM (patches 0006-0007) ───────────────────────── ` \
  -Phase2bEnableSplits               0      `# disable TrySplits in Phase 2b inner (split discovery already done by 2a + 2a.5 + 2a.6)` \
  -Phase2bMaxIter                    60     \
  \
  `# ── TimeShift: FULLY DISABLED (this is the test) ──────────────────── ` \
  -TimeShiftAlignIter                0      `# 0 ⇒ all TimeShiftAlignPhase calls no-op` \
  -TimeShiftSplitEnable              0      `# disable per-split shift probe` \
  -TimeShiftMergeEnable              0      `# disable per-merge shift probe` \
  -TimeShiftAlignAfterPhase1         0      \
  -TimeShiftAlignAfterPhase1b        0      \
  -TimeShiftAlignAfterPhase2         0      \
  -TimeShiftAlignAfterPhase4         0      \
  -TimeShiftAlignAfterPhase5         0      \
  -TimeShiftAlignPostMerge           0      \
  -TimeShiftAlignScoreThresh         0.5    `# value irrelevant when iter=0, kept for documentation` \
  -EnergyCOMRealign                  0      `# already user's preference; PCA-centering more reliable` \
  \
  `# ── BIC penalty: bias slightly toward BIC, away from AIC ──────────── ` \
  -PenaltyMix                        0.5    `# was implicitly 0.0 (pure AIC, very split-happy)` \
                                            `# 0.5 = halfway between AIC and BIC; reduces over-splitting` \
  \
  `# ── final global pass + output ────────────────────────────────────── ` \
  -DipSplitGlobalEnable              0      \
  -RandomSeed                        42     \
  -Verbose                           1      \
  2>&1 | tee "${LOG}"

echo
echo "─────────────────────────────────────────────────────────────────"
echo "Run complete. Log: ${LOG}"
echo
echo "Key diagnostic lines to inspect:"
echo "  grep '\\[Phase' ${LOG} | grep -E 'CLUSTER STATE|LPT order|max iterations'"
echo
echo "  • The CLUSTER STATE lines show K-progression after every phase."
echo "  • If noise% spikes to >50% at any phase, that phase is over-merging."
echo "  • If K drops to 0 or 1 at any phase, that phase is consolidating wrong."
echo "  • Final cluster count comes from the .clu.${GROUP} file."
echo
echo "If this run produces sane clusters (Phase 7 final K in the"
echo "30-150 range, ISI plots clean), the TimeShift machinery is"
echo "still broken and patch 0005 didn't fully fix it.  In that case"
echo "compare with the previous TimeShift-ON log to localise the issue:"
echo "  grep '\\[Phase' previous.log    | grep CLUSTER\\ STATE > with_shift.txt"
echo "  grep '\\[Phase' ${LOG}         | grep CLUSTER\\ STATE > without_shift.txt"
echo "  diff with_shift.txt without_shift.txt"
