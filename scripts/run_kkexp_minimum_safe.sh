#!/usr/bin/env bash
# =============================================================================
# run_kkexp_minimum_safe.sh
#
# Most conservative pipeline run.  Disables EVERY optional phase added
# in patches 0001-0009; restores Phase 2b's pre-0007 behaviour
# (TrySplits enabled, no iter cap via flags); keeps only the long-
# established splitters (Phase 1b/2.5 DipSplit, Phase 2 refractory,
# Phase 2a per-cluster CEM); disables all advanced merging
# (cross-chunk, mean-subtraction).  TimeShift fully off.
#
# Purpose: localise where the noise-collapse failure lives.
#   • If THIS script also produces a noise-only result, the bug is
#     in the base pipeline (Phase 4/5/6/7) — NOT in my recent patches.
#   • If this script produces a sensible cluster count, the bug is
#     in one of the patches 0001-0009 that the production script
#     enables.  Selectively re-enable to find which.
#
# Patches 0010 (logging) MUST be applied so we can read the per-phase
# CLUSTER STATE lines.  Patch 0005 (TimeShift drift fix) is irrelevant
# here since all TimeShift is off; patches 0006-0007 (Phase 2b LPT +
# iter cap + splits-off) are irrelevant because we restore the
# pre-0007 splits-on behaviour explicitly below.
# =============================================================================

set -euo pipefail

DATASET="sirotaA-jg-000005-20120312"
GROUP="6"
LOG="${DATASET}.kkexp.${GROUP}.minimum_safe.log"

KKEXP="${KKEXP:-KiloKlustaKwik}"
if ! command -v "${KKEXP}" >/dev/null 2>&1; then
    echo "ERROR: ${KKEXP} not in PATH.  Set KKEXP=/path/to/KiloKlustaKwik." >&2
    exit 1
fi

echo "Running ${KKEXP} on ${DATASET} group ${GROUP} — MINIMUM SAFE config"
echo "Log: ${LOG}"
echo

"${KKEXP}" "${DATASET}" "${GROUP}" \
  \
  `# ── core sweep + chunking ─────────────────────────────────────────── ` \
  -MaxClusters                       200    \
  -nRuns                             1      `# minimum runs — bypass nRuns multiplicity` \
  -ChunkMinutes                      10     \
  -ChunkOverlapMinutes               5      \
  -ChunkPreseedFraction              0.15   \
  \
  `# ── Long-established phases (KEPT) ────────────────────────────────── ` \
  -DipSplitEnable                    1      `# Phase 1b + Phase 2.5 (pre-patches)` \
  -DipSplitMinSize                   50     \
  -SubspaceRecluster                 1      `# Phase 2 (with refractory split)` \
  -SubspaceDims                      6      \
  \
  `# ── Disable EVERY patch-0006+ optional phase ──────────────────────── ` \
  -KnnSplitPerChunkEnable            0      `# patches 0001-0004 — disable Phase 2b.5 KnnSplit` \
  -DipSplitBeforePhase2b             0      `# patch 0008 — disable Phase 2a.5 DipSplit` \
  -HullSplitEnable                   0      `# patch 0009 — disable Phase 2a.6 HullSplit` \
  -Phase2bEnableSplits               1      `# patch 0007 — restore pre-0007 (TrySplits on in Phase 2b)` \
  -Phase2bMaxIter                    0      `# patch 0006 — 0 falls back to global MaxIter = pre-0006 behaviour` \
  \
  `# ── Template matching: DISABLE both within- and cross-chunk ───────── ` \
  -TemplateMatchScore                0.0    `# Phase 4 off` \
  -CrossChunkTemplateScore           0.0    `# Phase 5 off` \
  -CrossChunkDriftSigma              0.0    \
  -MeanSubtractionMergeEnable        0      `# Phase 6b off` \
  -GlobalMergeIter                   50     `# Phase 6 kept — this is the long-standing global EM` \
  -AdaptiveMerge                     0      `# disable adaptive merge` \
  \
  `# ── TimeShift: FULLY OFF (deferred bug — see user msg) ───────────── ` \
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
  `# ── BIC penalty: pure AIC (the long-standing default) ─────────────── ` \
  -PenaltyMix                        0.0    \
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
echo "  grep 'CLUSTER STATE' ${LOG}"
echo
echo "Expected (rough):"
echo "  [Phase 1]    total=NNN"
echo "  [Phase 1b]   total=NNN+δ      ← DipSplit adds clusters"
echo "  [Phase 2]    total=NNN+δ      ← refractory adds a few"
echo "  [Phase 2a]   total=NNN×3-5    ← per-cluster CEM grows K significantly"
echo "  [Phase 2b]   total=NNN×0.5-1  ← ConsiderDeletion + warm-start consolidates"
echo "  [Phase 2.5]  total=NNN        ← roughly stable through final DipSplit"
echo
echo "If total collapses to 0 or noise% jumps to >95% at any phase,"
echo "that phase is the offender."
echo
echo "Globalstate after Phase 6/7 not yet in the log (patch 0011 will"
echo "add it).  For now the final cluster count is in the .clu.${GROUP}"
echo "file: \`awk 'NR>1 {print \$1}' ${DATASET}.clu.${GROUP} | sort -u | wc -l\`"
