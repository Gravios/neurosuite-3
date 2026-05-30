#!/usr/bin/env bash
# run_kke_v5_fix_dispersion.sh
#
# Same logic as v4 BUT with -Log 1 -Screen 1 so we can see InitTimeShift
# diagnostics.  Plus a stricter post-flight that walks through the
# alignment chain step-by-step and tells you exactly where it stopped.
#
# Diff from v4:
#   -Log    0 → 1
#   -Screen 0 → 1
#   Stronger post-flight grep
#
# Note: the -Log 0 -Screen 0 in v4 was a mistake.  Output() bails out
# when both are zero, so any InitTimeShift / RunAlignmentBlock /
# TimeShiftFinalize diagnostic that uses Output() never reaches the
# log.  Switching to 1/1 means a much chattier run, but for now we
# need that chatter to find out why alignment never activates.
set -euo pipefail

ELEC_FROM_CLI=""
EXTRA=()
DRY_RUN=0
for arg in "$@"; do
    case "${arg}" in
        --dry-run) DRY_RUN=1 ;;
        [1-9]|[1-9][0-9]|[1-9][0-9][0-9])
            [[ -z "${ELEC_FROM_CLI}" ]] \
                && ELEC_FROM_CLI="${arg}" \
                || EXTRA+=( "${arg}" ) ;;
        *) EXTRA+=( "${arg}" ) ;;
    esac
done

shopt -s nullglob
fetD_files=( *.fetD.* )
shopt -u nullglob
if (( ${#fetD_files[@]} == 0 )); then
    echo "ERROR: no .fetD.<group> file found in $(pwd)" >&2
    exit 1
fi

if [[ -n "${ELEC_FROM_CLI}" ]]; then
    ELEC="${ELEC_FROM_CLI}"
elif [[ -n "${ELEC:-}" ]]; then
    :
else
    ELEC="${fetD_files[0]##*.}"
    if (( ${#fetD_files[@]} > 1 )); then
        echo "Multiple .fetD.* files; using group ${ELEC} (first match)."
        echo "Override:  $0 <group>"
    fi
fi

SESSION=""
for f in "${fetD_files[@]}"; do
    [[ "$f" == *.fetD."${ELEC}" ]] && SESSION="${f%.fetD."${ELEC}"}" && break
done
[[ -n "${SESSION}" ]] || { echo "ERROR: no .fetD.${ELEC} matches" >&2; exit 1; }

# ── Pre-flight: check that the files InitTimeShift needs are present ────
for ext in fetD spkD pcaD res; do
    f="${SESSION}.${ext}.${ELEC}"
    if [[ ! -f "${f}" ]]; then
        echo "WARNING: ${f} not found"
        if [[ "${ext}" == "pcaD" ]]; then
            echo "         ↑ likely cause of silent InitTimeShift failure."
            echo "         Without .pcaD.${ELEC}, the alignment fan can't"
            echo "         build its pre-shifted basis tensors and the"
            echo "         entire post-phase alignment chain stays disabled."
        fi
    else
        bytes=$(stat -c%s "${f}" 2>/dev/null || stat -f%z "${f}" 2>/dev/null)
        echo "OK: ${f} (${bytes} bytes)"
    fi
done

KKEXP="${KKEXP:-KiloKlustaKwik}"
PARALLEL_K="${PARALLEL_K:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="${SESSION}.kkexp.${ELEC}.${TIMESTAMP}.log"

ARGS=(
    "${SESSION}" "${ELEC}"

    -MinClusters             2
    -MaxClusters             200
    -MaxPossibleClusters     2000

    -InitMethod              farthest
    -nRuns                   5
    -RandomSeed              1

    -ChunkPreseedFraction    0.05
    -ChunkMinutes            12
    -ChunkOverlapMinutes     3

    -MergeThresh             46
    -AdaptiveMerge           0
    -GlobalMergeIter         0
    -TimeMergeIter           0

    -SubspaceRecluster       1
    -SubspaceDims            5
    -Phase2bMode             3

    -VBGMMMaxIter            400
    -VBGMMConvTol            1e-4
    -VBGMMPriorMode          2
    -VBGMMPriorBlend         0.1

    -TemplateMatchScore      0.95
    -TemplateMatchIters      20
    -CrossChunkTemplateScore 0.95

    -SplitEvery              6
    -SplitRecurseDepth       12
    -MaxIter                 32
    -PenaltyMix              0

    -TemplateMatchEigRatio   3.0
    -DipSplitGlobalEnable    0
    -DipSplit2D              1
    -CrossChunkDriftSigma    3.0

    -DipSplitEnable              1
    -DipSplitMinSize             10
    -DipSplitElongationFactor    4.0
    -DipSplitValleyThresh        0.0

    -TimeShiftSplitEnable        0
    -TimeShiftMergeEnable        0
    -TimeShiftAlignScoreThresh   0.0

    # ── LOGGING ON ─────────────────────────────────────────────────────
    # Output() bails out when both Log and Screen are 0.  InitTimeShift,
    # RunAlignmentBlock entry/exit, and TimeShiftFinalize summary all use
    # Output().  We need them visible to diagnose why alignment doesn't run.
    -Log                         1
    -Screen                      1
    -fSaveModel                  0
    -SaveIntermediates           0

    -MaxTimeShift                3
    -TimeShiftAlignIter          5
    -TimeShiftAlignAfterPhase1   0
    -TimeShiftAlignAfterPhase1b  0
    -TimeShiftAlignAfterPhase2   1
    -TimeShiftAlignAfterPhase5   0
    -TimeShiftAlignAfterPhase6   0
    -TimeShiftAlignPostMerge     0
    -EnergyCOMRealign            0
    -AlignPcaCenter              1

    -ResidualPCAIter             5
    -ResidualPCAComponents       3
    -ResidualPCASubK             3
    -ResidualPCADominantChannels 2
    -ResidualPCAConvTol          0.01

    -MeanSubtractionMergeEnable  0
    -MeanSubtractionMergeThresh  0.02

    -ParallelK               "${PARALLEL_K}"
)

ARGS+=( "${EXTRA[@]}" )

print_cmd() {
    printf '%s' "${KKEXP}"
    for a in "${ARGS[@]}"; do printf ' \\\n    %q' "$a"; done
    printf '\n'
}

if (( DRY_RUN )); then
    print_cmd
    exit 0
fi

command -v "${KKEXP}" >/dev/null \
    || { echo "ERROR: ${KKEXP} not on PATH" >&2; exit 1; }

{
    echo "# KiloKlustaKwik v5 (diagnostic logging on)"
    echo "# date:    $(date -Is)"
    echo "# pwd:     $(pwd)"
    echo "# binary:  $(command -v "${KKEXP}")"
    echo "# session: ${SESSION}  group ${ELEC}"
    print_cmd
    echo
} | tee "${LOG_FILE}"

set +e
"${KKEXP}" "${ARGS[@]}" 2>&1 | tee -a "${LOG_FILE}"
rc=${PIPESTATUS[0]}
set -e

# ── Step-by-step alignment-chain post-flight ────────────────────────────
echo
echo "# ─── Alignment chain diagnosis ───"

step=0
saw_init=$(  grep -c 'InitTimeShift'              "${LOG_FILE}" 2>/dev/null || echo 0)
saw_disabled=$(grep -c 'InitTimeShift.*disabled\|InitTimeShift.*not found\|InitTimeShift.*probe disabled' \
                                                  "${LOG_FILE}" 2>/dev/null || echo 0)
saw_p2c=$(   grep -c '\[Phase 2c\] Cluster-mean'  "${LOG_FILE}" 2>/dev/null || echo 0)
saw_p6c=$(   grep -c '\[Phase 6c\] Shift commit'  "${LOG_FILE}" 2>/dev/null || echo 0)
saw_res=$(   grep -c '\.res updated'              "${LOG_FILE}" 2>/dev/null || echo 0)
saw_p6d=$(   grep -c '\[Phase 6d\] auto-reextract' "${LOG_FILE}" 2>/dev/null || echo 0)

echo "#   InitTimeShift mentioned        : ${saw_init}"
echo "#   InitTimeShift reported disabled: ${saw_disabled}"
echo "#   [Phase 2c] entry line          : ${saw_p2c}"
echo "#   [Phase 6c] Shift commit line   : ${saw_p6c}"
echo "#   .res updated line (patch85)    : ${saw_res}"
echo "#   [Phase 6d] auto-reextract line : ${saw_p6d}"
echo

if (( saw_disabled > 0 )); then
    echo "# DIAGNOSIS: InitTimeShift bailed.  Look in the log for the exact"
    echo "#            'InitTimeShift: ... disabled' line; it tells you why"
    echo "#            (usually .pcaD.${ELEC} missing or has a bad header)."
elif (( saw_p2c == 0 )); then
    echo "# DIAGNOSIS: RunAlignmentBlock's silent return path fired."
    echo "#            Either InitTimeShift returned false (now visible above"
    echo "#            with -Log 1 -Screen 1) or TimeShiftAlignAfterPhase2"
    echo "#            wasn't actually 1 at the binary."
elif (( saw_p6c == 0 )); then
    echo "# DIAGNOSIS: Phase 2c entered but found zero shifts.  Either xcorr"
    echo "#            converged at lag 0 for every spike (cluster means are"
    echo "#            already perfectly aligned, somehow) or there's a bug"
    echo "#            in patch83/84 gating the m_cumShift writes."
elif (( saw_res == 0 )); then
    echo "# DIAGNOSIS: shifts found, but patch85's .res rewrite didn't fire."
    echo "#            Your binary may not have patch85 applied."
elif (( saw_p6d == 0 )); then
    echo "# DIAGNOSIS: .res rewritten, but patch87's auto-reextract didn't fire."
    echo "#            Your binary may not have patch87 applied, or"
    echo "#            KKEXP_AUTO_REEXTRACT=0 is set."
else
    echo "# DIAGNOSIS: full chain fired.  Check the .spkD content directly."
fi

echo
echo "# exit ${rc}  log: ${LOG_FILE}"
exit "${rc}"
