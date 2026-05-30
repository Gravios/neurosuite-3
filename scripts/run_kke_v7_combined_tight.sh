#!/usr/bin/env bash
# run_kke_v7_combined_tight.sh
#
# Combined feature-space + subfeature-space tightening with a
# template-match quality gate to cull spurious noise sub-clusters.
# This is option C from the variability-tuning discussion — all the
# tightening knobs from options A and B layered together, plus a
# stricter template-match cleanup pass to drop sub-clusters whose
# templates don't pass quality.
#
# Changes vs v6 (which had only the subfeature-space block):
#
#   FEATURE SPACE
#     MergeThresh                46    → 32       # stricter merge gate
#     VBGMMPriorBlend            0.10  → 0.05     # trust data covariance more
#     PenaltyMix                 0     → 0.3      # mix in AIC for easier splits
#
#   SUBFEATURE SPACE  (carried from v6)
#     ResidualPCAComponents      3     → 4
#     ResidualPCASubK            3     → 5
#     ResidualPCADominantChannels 2    → 3
#     ResidualPCAConvTol         0.01  → 0.005
#
#   QUALITY GATE
#     TemplateMatchScore         0.95  → 0.97     # cull marginal templates
#     TemplateMatchEigRatio      3.0   → 2.5      # tighter shape match
#
# Use case: run AFTER v6 if v6's over-splits looked biologically
# meaningful and you want to push further.  If v6 produced noise
# sub-clusters, dial back v6 instead — don't layer v7 on top.
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

# ── Pre-flight: input files ──────────────────────────────────────────────
for ext in fetD spkD pcaD res; do
    f="${SESSION}.${ext}.${ELEC}"
    if [[ ! -f "${f}" ]]; then
        echo "WARNING: ${f} not found"
    fi
done

KKEXP="${KKEXP:-KiloKlustaKwik}"
PARALLEL_K="${PARALLEL_K:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="${SESSION}.kkexp.${ELEC}.v7.${TIMESTAMP}.log"

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

    # ── v7 feature-space tightening ───────────────────────────────────
    -MergeThresh             32        # was 46 — stricter merge gate (~χ²(12, 0.999))
    -AdaptiveMerge           0
    -GlobalMergeIter         0
    -TimeMergeIter           0

    -SubspaceRecluster       1
    -SubspaceDims            5
    -Phase2bMode             3

    -VBGMMMaxIter            400
    -VBGMMConvTol            1e-4
    -VBGMMPriorMode          2
    -VBGMMPriorBlend         0.05      # was 0.10 — trust data covariance more

    # ── v7 quality gate ───────────────────────────────────────────────
    -TemplateMatchScore      0.97      # was 0.95 — cull marginal templates
    -TemplateMatchIters      20
    -CrossChunkTemplateScore 0.97      # was 0.95 — same gate, cross-chunk pass
    -TemplateMatchEigRatio   2.5       # was 3.0 — tighter shape match

    -SplitEvery              6
    -SplitRecurseDepth       12
    -MaxIter                 32
    -PenaltyMix              0.3       # was 0 — mix AIC for easier splits

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

    # ── v7 subfeature-space tightening (carried from v6) ──────────────
    -ResidualPCAIter             5
    -ResidualPCAComponents       4        # was 3
    -ResidualPCASubK             5        # was 3
    -ResidualPCADominantChannels 3        # was 2
    -ResidualPCAConvTol          0.005    # was 0.01

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
    echo "# KiloKlustaKwik v7 (combined feature+subfeature tightening + quality gate)"
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

# ── Post-flight: cluster count + (optional) baseline diff ───────────────
echo
echo "# ─── Combined-tightening result ───"

CLU="${SESSION}.clu.${ELEC}"
if [[ -f "${CLU}" ]]; then
    declared=$(head -1 "${CLU}")
    used=$(awk 'NR>1' "${CLU}" | sort -nu | wc -l)
    spikes=$(awk 'NR>1' "${CLU}" | wc -l)
    echo "#   .clu.${ELEC}            : ${CLU}"
    echo "#   declared nClusters    : ${declared}"
    echo "#   distinct IDs in use   : ${used}"
    echo "#   total spike rows      : ${spikes}"

    # Size histogram — bucket cluster sizes to spot noise-y sub-clusters
    echo "#   cluster size distribution (top + tail):"
    awk 'NR>1' "${CLU}" | sort -n | uniq -c | sort -rn | \
        awk 'BEGIN{n=0} {n++; if(n<=5||(n>NR-5&&n<=NR)) printf "#     %8d spikes in cluster %s\n", $1, $2}' \
        <(awk 'NR>1' "${CLU}" | sort -n | uniq -c | sort -rn)
else
    echo "#   WARNING: ${CLU} not found — clustering may not have completed."
fi

# Phase-2b m3 activity
p2b_invocations=$(grep -c 'Phase 2b m3' "${LOG_FILE}" 2>/dev/null || echo 0)
p2b_splits=$(grep -cE 'subspace.*split|residual.*split|m3.*split' "${LOG_FILE}" 2>/dev/null || echo 0)
echo "#   Phase 2b m3 invocations: ${p2b_invocations}"
echo "#   subspace split events  : ${p2b_splits}"

# Template-match culls (new in v7 — would be cleanup line in log)
tm_culls=$(grep -cE 'template.*(cull|drop|reject)|TemplateMatch.*fail' "${LOG_FILE}" 2>/dev/null || echo 0)
echo "#   template-match culls    : ${tm_culls}"

# Optional baseline comparison
if [[ -n "${BASELINE_LOG:-}" && -f "${BASELINE_LOG}" ]]; then
    echo
    echo "# ─── Baseline comparison (BASELINE_LOG=${BASELINE_LOG}) ───"
    base_p2b_inv=$(grep -c 'Phase 2b m3' "${BASELINE_LOG}" 2>/dev/null || echo 0)
    base_p2b_split=$(grep -cE 'subspace.*split|residual.*split|m3.*split' "${BASELINE_LOG}" 2>/dev/null || echo 0)
    base_tm_culls=$(grep -cE 'template.*(cull|drop|reject)|TemplateMatch.*fail' "${BASELINE_LOG}" 2>/dev/null || echo 0)
    echo "#   baseline Phase 2b m3 invocations : ${base_p2b_inv}"
    echo "#                       this run    : ${p2b_invocations}   (Δ $((p2b_invocations - base_p2b_inv)))"
    echo "#   baseline subspace splits         : ${base_p2b_split}"
    echo "#                       this run    : ${p2b_splits}   (Δ $((p2b_splits - base_p2b_split)))"
    echo "#   baseline template-match culls    : ${base_tm_culls}"
    echo "#                       this run    : ${tm_culls}   (Δ $((tm_culls - base_tm_culls)))"
fi

echo
echo "# exit ${rc}  log: ${LOG_FILE}"
echo
echo "# To compare against v5 (no tightening) or v6 (subfeature only):"
echo "#   BASELINE_LOG=<path-to-prior-log> $0 ${ELEC}"
exit "${rc}"
