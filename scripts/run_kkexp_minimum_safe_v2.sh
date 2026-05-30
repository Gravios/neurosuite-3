#!/usr/bin/env bash
# =============================================================================
# run_kkexp_minimum_safe_v2.sh
#
# Replaces the v1 script that incorrectly disabled the Phase 2b perf fixes
# (which caused stuck-on-single-core).  The perf fixes (patches 0006/0007)
# are orthogonal to the noise-collapse question — keep them on so Phase 2b
# completes in seconds rather than hours.
#
# What we're ruling in/out:
#   • Disabled: patch 0001-0004 (KnnSplit), 0008 (Phase 2a.5 DipSplit),
#               0009 (Phase 2a.6 HullSplit) — my NEW splitters.
#   • Enabled but with perf fixes: patches 0006/0007 (Phase 2b sort/cap).
#   • Enabled: long-standing splitters (Phase 1b DipSplit, Phase 2
#              refractory, Phase 2a per-cluster CEM, Phase 2.5 DipSplit).
#   • Enabled: long-standing consolidators (Phase 4 within-chunk template,
#              Phase 5 cross-chunk merge, Phase 6 Global EM).
#   • Disabled: TimeShift (deferred bug), Phase 6b mean-subtraction merge.
#
# Outcome interpretation:
#   • Collapse still happens → bug is in the long-standing pipeline
#     (Phase 4/5/6).  Look at the GLOBAL STATE / CLUSTER STATE log lines
#     to localise which phase drops K to ≈1.
#   • Sane cluster count → bug is in one of patches 0001-0004 / 0008 / 0009.
#     Re-enable one at a time to isolate.
# =============================================================================

set -euo pipefail

DATASET="sirotaA-jg-000005-20120312"
GROUP="6"
LOG="${DATASET}.kkexp.${GROUP}.minimum_safe_v2.log"

KKEXP="${KKEXP:-KiloKlustaKwik}"
if ! command -v "${KKEXP}" >/dev/null 2>&1; then
    echo "ERROR: ${KKEXP} not in PATH.  Set KKEXP=/path/to/KiloKlustaKwik." >&2
    exit 1
fi

echo "Running ${KKEXP} on ${DATASET} group ${GROUP} — MINIMUM SAFE v2"
echo "  perf fixes ON, new splitters OFF, TimeShift OFF"
echo "Log: ${LOG}"
echo

"${KKEXP}" "${DATASET}" "${GROUP}" \
  \
  `# ── core sweep + chunking ─────────────────────────────────────────── ` \
  -MaxClusters                       200    \
  -nRuns                             1      `# 1 run is sufficient to diagnose collapse` \
  -ChunkMinutes                      10     \
  -ChunkOverlapMinutes               5      \
  -ChunkPreseedFraction              0.15   \
  \
  `# ── Long-standing splitters (KEPT — these are pre-patch defaults) ─ ` \
  -DipSplitEnable                    1      \
  -DipSplitMinSize                   50     \
  -SubspaceRecluster                 1      \
  -SubspaceDims                      6      \
  \
  `# ── NEW splitters (DISABLED for this diagnostic) ──────────────────── ` \
  -KnnSplitPerChunkEnable            0      `# patches 0001-0004` \
  -DipSplitBeforePhase2b             0      `# patch 0008` \
  -HullSplitEnable                   0      `# patch 0009` \
  \
  `# ── Phase 2b perf fixes (KEPT — orthogonal to collapse question) ── ` \
  -Phase2bEnableSplits               0      `# patch 0007 — TrySplits off in Phase 2b CEM` \
  -Phase2bMaxIter                    60     `# patch 0006 — iter cap` \
  \
  `# ── Long-standing consolidators (KEPT) ───────────────────────────── ` \
  -TemplateMatchScore                0.90   `# Phase 4 within-chunk merge` \
  -TemplateMatchIters                10     \
  -CrossChunkTemplateScore           0.80   `# Phase 5 cross-chunk merge` \
  -CrossChunkDriftSigma              1.5    \
  -GlobalMergeIter                   50     `# Phase 6 Global EM` \
  -AdaptiveMerge                     1      \
  -TimeMergeIter                     100    \
  \
  `# ── Phase 6b (mean-subtraction merge): DISABLED ─────────────────── ` \
  -MeanSubtractionMergeEnable        0      `# rule out as collapse cause` \
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
  `# ── BIC penalty: pre-patch default ─────────────────────────────── ` \
  -PenaltyMix                        0.0    `# pure AIC, long-standing default` \
  \
  `# ── Output + diagnostics ──────────────────────────────────────────── ` \
  -DipSplitGlobalEnable              0      \
  -RandomSeed                        42     \
  -Verbose                           1      \
  2>&1 | tee "${LOG}"

echo
echo "─────────────────────────────────────────────────────────────────"
echo "Run complete. Log: ${LOG}"
echo
echo "Inspect the phase-by-phase progression:"
echo "  grep -E 'CLUSTER STATE|GLOBAL STATE|\\[Phase' ${LOG}"
echo
echo "Expected phase-by-phase (rough K progression):"
echo "  [Phase 1]    total=300-600    chunked CEM initial discovery"
echo "  [Phase 1b]   total=300-600    DipSplit may add a few"
echo "  [Phase 2]    total=300-600    refractory split rarely fires"
echo "  [Phase 2a]   total=900-1800   per-cluster CEM grows K ×2-3"
echo "  [Phase 2b]   total=300-900    ConsiderDeletion halves K"
echo "  [Phase 2.5]  total=300-900    final chunk-level DipSplit"
echo "  [Phase 4]    total=200-700    template-matching merges"
echo "  [Phase 5]    nGlobal=30-150   cross-chunk merge → global"
echo "                GLOBAL STATE: alive=30-150"
echo "  [Phase 6]    GLOBAL STATE: alive=30-150  (slight Phase 6 EM)"
echo "  [Phase 6a]   GLOBAL STATE: alive=30-150"
echo "  [Phase 6b]   GLOBAL STATE: alive=30-150  (skipped — disabled)"
echo
echo "If GLOBAL STATE drops to alive=1 at any boundary, that phase is the"
echo "collapse cause.  If alive stays sane through Phase 6 but the .clu"
echo "file is noise-only, the bug is in the .clu writer:"
echo "  awk '{print \$1}' ${DATASET}.clu.${GROUP} | sort -u | head"
echo
echo "If this run collapses, the bug is OLDER than patches 0001-0009."
echo "If this run is sane, the next test enables patches one at a time:"
echo "  -HullSplitEnable 1                 (test patch 0009)"
echo "  -DipSplitBeforePhase2b 1           (test patch 0008)"
echo "  -KnnSplitPerChunkEnable 1          (test patches 0001-0004)"
