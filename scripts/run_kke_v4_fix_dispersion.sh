#!/usr/bin/env bash
# run_kke_v4_fix_dispersion.sh
#
# WORKFLOW A: re-cluster with mode 1 to fix on-disk temporal dispersion.
# Requires patch85 applied (rewrites .res alongside .spk/.fet during
# WritePhase15Checkpoint so the three files stay consistent).
#
# Mode 1 (-AlignPcaCenter 1) runs:
#   - xcorr iter loop in Phase 2b m3 (writes m_cumShift)
#   - patch83 PCA-centering refine as a second pass (writes m_cumShift)
#   - TimeShiftFinalize → RefeaturizeFromShifts → WritePhase15Checkpoint
#     → writes .spk/.fet/.res atomically via the .pending mechanism
#
# Mode 2 (-AlignPcaCenter 2) was the previous setting; it stays in-memory
# only by design and cannot fix dispersion already on disk.
#
# Pre-flight check this run is on patch85-or-later: search for the
# ".res updated" log line in KKExp's stderr after Phase 6c.  If you
# don't see it, KKExp is still on the pre-patch85 binary and will
# reintroduce the same asymmetry (.spk shifted, .res not).
#
# Differences from run_kke_v3.sh:
#   -AlignPcaCenter 2 → 1               # let alignment write m_cumShift
#   -TimeShiftAlignIter unchanged       # 5 iters give convergence room
#   -MaxTimeShift unchanged             # per-iter cap; total can be larger
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

# ── Pre-flight: check .spkD/.fetD/.res presence and sanity ──────────────
for ext in fetD spkD res; do
    f="${SESSION}.${ext}.${ELEC}"
    if [[ ! -f "${f}" ]]; then
        echo "WARNING: ${f} not found" >&2
    fi
done

# ── Pre-flight: confirm patch85 build is in place ───────────────────────
KKEXP="${KKEXP:-KiloKlustaKwik}"
if command -v "${KKEXP}" >/dev/null 2>&1; then
    # Some builds embed a version banner with a patch list; if present, look
    # for patch85.  This is best-effort — not all builds embed it.
    if "${KKEXP}" --help 2>&1 | grep -qi 'patch85\|res.pending'; then
        :
    else
        echo "NOTE: cannot confirm patch85 is in this KiloKlustaKwik build."
        echo "      If KKExp's stderr shows '.spk updated' + '.fet updated'"
        echo "      WITHOUT a matching '.res updated' line during Phase 6c,"
        echo "      you have the pre-patch85 binary — the run will not fix"
        echo "      the dispersion.  Apply patch85 first."
    fi
fi

PARALLEL_K="${PARALLEL_K:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="${SESSION}.kkexp.${ELEC}.${TIMESTAMP}.log"

ARGS=(
    "${SESSION}" "${ELEC}"

    # Cluster count bounds
    -MinClusters             2
    -MaxClusters             200
    -MaxPossibleClusters     2000

    # Init + repeat
    -InitMethod              farthest
    -nRuns                   5
    -RandomSeed              1

    # Chunked CEM (Phase 0/1)
    -ChunkPreseedFraction    0.05
    -ChunkMinutes            12
    -ChunkOverlapMinutes     3

    # Cross-chunk match + global EM
    -MergeThresh             46
    -AdaptiveMerge           0
    -GlobalMergeIter         0
    -TimeMergeIter           0

    # Per-chunk re-CEM
    -SubspaceRecluster       1
    -SubspaceDims            5
    -Phase2bMode             3

    # VB-GMM tuning
    -VBGMMMaxIter            400
    -VBGMMConvTol            1e-4
    -VBGMMPriorMode          2
    -VBGMMPriorBlend         0.1

    # Phase 5 within-chunk template match
    -TemplateMatchScore      0.95
    -TemplateMatchIters      20
    -CrossChunkTemplateScore 0.95

    # Split probe
    -SplitEvery              6
    -SplitRecurseDepth       12
    -MaxIter                 32
    -PenaltyMix              0

    # Drift-aware stack
    -TemplateMatchEigRatio   3.0
    -DipSplitGlobalEnable    0
    -DipSplit2D              1
    -CrossChunkDriftSigma    3.0

    # Phase 1b per-chunk DipSplit
    -DipSplitEnable              1
    -DipSplitMinSize             10
    -DipSplitElongationFactor    4.0
    -DipSplitValleyThresh        0.0

    # Split/merge time-shift probes — all OFF except Phase 2 alignment.
    # Keep these off so the only m_cumShift writer is the Phase 2b m3
    # alignment block (with patch85 active, that writer is consistent
    # across .spk/.fet/.res).
    -TimeShiftSplitEnable        0
    -TimeShiftMergeEnable        0
    -TimeShiftAlignScoreThresh   0.0

    # Output verbosity
    -Log                         0
    -Screen                      0
    -fSaveModel                  0
    -SaveIntermediates           0

    # ── Cluster-mean alignment ──────────────────────────────────────────
    # MaxTimeShift = per-iter cap (xcorr lag range).  TimeShiftAlignIter
    # gates total convergence; with iter=5 and maxShift=3 the maximum
    # achievable total shift is +/-15 samples — well past your observed
    # +/-3 range so no spike will be left stranded.
    -MaxTimeShift                3
    -TimeShiftAlignIter          5
    -TimeShiftAlignAfterPhase1   0
    -TimeShiftAlignAfterPhase1b  0
    -TimeShiftAlignAfterPhase2   1
    -TimeShiftAlignAfterPhase5   0
    -TimeShiftAlignAfterPhase6   0
    -TimeShiftAlignPostMerge     0
    -EnergyCOMRealign            0

    # ── AlignPcaCenter mode ─────────────────────────────────────────────
    # 0 = classic xcorr alignment, m_cumShift updated by iter loop.
    # 1 = xcorr iter loop + patch83 PCA-centering second-pass refine,
    #     m_cumShift updated by both.  Recommended for fixing existing
    #     dispersion: tighter alignment than mode 0.
    # 2 = patch84 mode: in-memory circular shifts, m_cumShift NEVER
    #     touched.  Disk content unchanged.  Use ONLY if you already
    #     have pristine .spkD/.fetD/.res and want to cluster without
    #     modifying disk.
    -AlignPcaCenter              1

    # Phase 2b mode 3 hyperparameters
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
    echo
    echo "# (dry-run; would log to: ${LOG_FILE})"
    exit 0
fi

command -v "${KKEXP}" >/dev/null \
    || { echo "ERROR: ${KKEXP} not on PATH" >&2; exit 1; }

{
    echo "# KiloKlustaKwik v4 (workflow A: fix dispersion via mode 1 + patch85)"
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

# ── Post-run consistency check ──────────────────────────────────────────
echo
echo "# ─── Phase 6c output check ───"
if grep -qE 'Phase 6c.*Shift commit:.*[1-9]' "${LOG_FILE}"; then
    nspk=$(grep -c '.spk updated' "${LOG_FILE}" 2>/dev/null || echo 0)
    nfet=$(grep -c '.fet updated' "${LOG_FILE}" 2>/dev/null || echo 0)
    nres=$(grep -c '.res updated' "${LOG_FILE}" 2>/dev/null || echo 0)
    echo "#   .spk update lines: ${nspk}"
    echo "#   .fet update lines: ${nfet}"
    echo "#   .res update lines: ${nres}"
    if (( nres == 0 && (nspk > 0 || nfet > 0) )); then
        echo
        echo "# WARNING: .spk and/or .fet were rewritten but .res was NOT."
        echo "#   This is the patch85 symptom — your KKExp build is older"
        echo "#   than patch85.  Apply patch85 and re-run; otherwise the"
        echo "#   temporal dispersion will persist on disk."
    fi
fi

echo
echo "# exit ${rc}  log: ${LOG_FILE}"
exit "${rc}"
