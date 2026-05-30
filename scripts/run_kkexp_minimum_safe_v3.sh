#!/usr/bin/env bash
# =============================================================================
# run_kkexp_minimum_safe_v3.sh
#
# v3 fix:  -MaxPossibleClusters 10000  (was implicit 500)
#
# Root cause of the noise-collapse:
#   Phase 5 produced 777 global clusters > MaxPossibleClusters (500).
#   The guard at KK.cpp:5041 falls back to CEMTwoPhase, which doesn't
#   initialise Class[] for the chunked path and returns HugeScore.  The
#   parent loop in KlustaKwik.cpp:1592 requires score < BestScore to
#   update BestClass; since the score is HugeScore, BestClass stays at
#   its zero-init state → .clu file is all zeros (noise).
#
# Rule of thumb: MaxPossibleClusters >= nChunks * MaxClusters.
# Here: 36 * 200 = 7200.  We set 10000 for safety margin.
#
# v3 also flips off the noise-cluster fallback diagnostic so we can see
# the WARNING about MaxPossibleClusters if it still fires:
#   -Screen 1  forces Output() messages to stdout (default 0 = silent)
# =============================================================================

set -euo pipefail

DATASET="sirotaA-jg-000005-20120312"
GROUP="6"
LOG="${DATASET}.kkexp.${GROUP}.minimum_safe_v3.log"

KKEXP="${KKEXP:-KiloKlustaKwik}"
if ! command -v "${KKEXP}" >/dev/null 2>&1; then
    echo "ERROR: ${KKEXP} not in PATH.  Set KKEXP=/path/to/KiloKlustaKwik." >&2
    exit 1
fi

echo "Running ${KKEXP} on ${DATASET} group ${GROUP} — MINIMUM SAFE v3"
echo "  MaxPossibleClusters=10000 (was 500, caused fallback-to-CEMTwoPhase noise collapse)"
echo "Log: ${LOG}"
echo

"${KKEXP}" "${DATASET}" "${GROUP}" \
  \
  `# ── core sweep + chunking ─────────────────────────────────────────── ` \
  -MaxClusters                       200    \
  -MaxPossibleClusters               10000  `# CRITICAL FIX: was 500 default, must accommodate nChunks*MaxClusters` \
  -nRuns                             1      \
  -ChunkMinutes                      10     \
  -ChunkOverlapMinutes               5      \
  -ChunkPreseedFraction              0.15   \
  \
  `# ── Force Output() messages to stdout so we see the MergeChunkModels` \
  `# warning if it still fires ────────────────────────────────────────── ` \
  -Screen                            1      \
  \
  `# ── Long-standing splitters (KEPT) ────────────────────────────────── ` \
  -DipSplitEnable                    1      \
  -DipSplitMinSize                   50     \
  -SubspaceRecluster                 1      \
  -SubspaceDims                      6      \
  \
  `# ── NEW splitters (DISABLED for this diagnostic) ──────────────────── ` \
  -KnnSplitPerChunkEnable            0      \
  -DipSplitBeforePhase2b             0      \
  -HullSplitEnable                   0      \
  \
  `# ── Phase 2b perf fixes (KEPT — orthogonal to collapse question) ── ` \
  -Phase2bEnableSplits               0      \
  -Phase2bMaxIter                    60     \
  \
  `# ── Long-standing consolidators (KEPT) ───────────────────────────── ` \
  -TemplateMatchScore                0.90   \
  -TemplateMatchIters                10     \
  -CrossChunkTemplateScore           0.80   \
  -CrossChunkDriftSigma              1.5    \
  -GlobalMergeIter                   50     \
  -AdaptiveMerge                     1      \
  -TimeMergeIter                     100    \
  \
  -MeanSubtractionMergeEnable        0      \
  \
  `# ── TimeShift: FULLY OFF (deferred bug) ──────────────────────────── ` \
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
echo "─────────────────────────────────────────────────────────────────"
echo "Run complete. Log: ${LOG}"
echo
echo "Diagnostic checks:"
echo "  Phase 5 should report < ${MaxPossibleClusters:-10000} global clusters."
echo "  Phase 6 [Phase 6] Global warm-start EM should now appear."
echo "  Final .clu cluster count:"
echo "    awk '{print \$1}' ${DATASET}.clu.${GROUP} | sort -u | wc -l"
echo "  Per-cluster size distribution:"
echo "    awk 'NR>1 {print \$1}' ${DATASET}.clu.${GROUP} | sort | uniq -c | sort -rn | head -20"
