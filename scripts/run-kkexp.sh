#!/usr/bin/env bash
# ============================================================================
# run-kkexp.sh — KiloKlustaKwik runner for the mode-3 workflow
#                (residual-PCA split iterations + Klusters-style xcorr realign)
#
# Sets ONLY the flags that diverge from upstream defaults.  Everything else
# is left to KiloKlustaKwik's compiled-in defaults so this script doesn't
# drift out of sync as upstream evolves.  Verified against patch39
# baseline (`02d404c bugfix(kke) fixed xcorr spike realignment for good`).
#
# Edit the SESSION block.  Pass --dry-run to inspect the command.
# Override threading with PARALLEL_K=N, the binary with KKEXP=path.
# Any extra args go straight to KiloKlustaKwik:
#   ./run-kkexp.sh -MaxClusters 100 -RandomSeed 7
# ============================================================================

set -euo pipefail

# ──────────────────────────────────────────────────────────────────────────
# Quick electrode override
# If a bare positive integer appears anywhere in the args, it becomes ELEC
# (the first such arg wins).  Everything else still flows through —
# --dry-run is still recognised, and stray flags still forward to
# KiloKlustaKwik.  Examples:
#   ./run-kkexp.sh 8                        # cluster group 8
#   ./run-kkexp.sh 12 --dry-run             # inspect command for group 12
#   ./run-kkexp.sh 8 -MaxClusters 50        # group 8 with overrides
#   ELEC=4 ./run-kkexp.sh                   # env-var alternative
# ──────────────────────────────────────────────────────────────────────────
ELEC_FROM_CLI=""
EXTRA=()
DRY_RUN=0
for arg in "$@"; do
    case "${arg}" in
        --dry-run)              DRY_RUN=1 ;;
        [1-9]|[1-9][0-9]|[1-9][0-9][0-9])
            if [[ -z "${ELEC_FROM_CLI}" ]]; then
                ELEC_FROM_CLI="${arg}"
            else
                EXTRA+=( "${arg}" )
            fi ;;
        *)                      EXTRA+=( "${arg}" ) ;;
    esac
done

# ──────────────────────────────────────────────────────────────────────────
# SESSION + ELECTRODE — edit defaults here; CLI/env override at runtime
# ──────────────────────────────────────────────────────────────────────────
SESSION_DIR="/data/processed/ephys/sirotaA/sirotaA-jg/sirotaA-jg-000005/sirotaA-jg-000005-20120312"
SESSION_NAME="sirotaA-jg-000005-20120312"

ELEC="${ELEC_FROM_CLI:-${ELEC:-8}}"     # CLI arg > $ELEC env > script default

# Per-group geometry — these are constant across all sirotaA shanks
# (8-channel tetrode-pairs).  If you run a session with non-uniform group
# geometry, override per-call:
#   NB_CHANNELS=6 ./run-kkexp.sh 12
# or via extra flags:
#   ./run-kkexp.sh 12 -NbChannels 6
NB_CHANNELS="${NB_CHANNELS:-8}"
NB_TOTAL_CHANNELS="${NB_TOTAL_CHANNELS:-96}"
NB_SAMPLES_PER_SPIKE="${NB_SAMPLES_PER_SPIKE:-42}"
PEAK_SAMPLE_INDEX="${PEAK_SAMPLE_INDEX:-21}"
NB_BYTES_PER_SAMPLE="${NB_BYTES_PER_SAMPLE:-2}"
SAMPLING_RATE="${SAMPLING_RATE:-20000}"

# Alt sessions (uncomment to swap):
# SESSION_NAME="jg05-20120316"
# SESSION_NAME="eb05-20251118"

KKEXP="${KKEXP:-KiloKlustaKwik}"
# Auto-detect logical CPUs; override with PARALLEL_K=N ./run-kkexp.sh
PARALLEL_K="${PARALLEL_K:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

# ──────────────────────────────────────────────────────────────────────────
# Derived
# ──────────────────────────────────────────────────────────────────────────
FILE_BASE="${SESSION_DIR}/${SESSION_NAME}"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="${FILE_BASE}.kkexp.${ELEC}.${TIMESTAMP}.log"

ARGS=( "${FILE_BASE}" "${ELEC}" )

# ── Geometry (required; no defaults possible) ─────────────────────────────
ARGS+=( -NbChannels         "${NB_CHANNELS}"          )
ARGS+=( -NbTotalChannels    "${NB_TOTAL_CHANNELS}"    )
ARGS+=( -NbSamplesPerSpike  "${NB_SAMPLES_PER_SPIKE}" )
ARGS+=( -PeakSampleIndex    "${PEAK_SAMPLE_INDEX}"    )
ARGS+=( -NbBytesPerSample   "${NB_BYTES_PER_SAMPLE}"  )
ARGS+=( -SamplingRate       "${SAMPLING_RATE}"        )

# ── Runtime ───────────────────────────────────────────────────────────────
ARGS+=( -Verbose       1   )            # default 0; need 1 to see mode-3 banners
ARGS+=( -ParallelK   "${PARALLEL_K}" )  # default 0 (serial); auto-detected here

# ──────────────────────────────────────────────────────────────────────────
# Phase 2b mode 3 — the new loop
# Per chunk: VBGMM → for each cluster: residual = spike − cluster-mean →
# top-K eigvecs of Rᵀ R → project → residual VBGMM (may split).  Loops up
# to ResidualPCAIter or until label-change fraction < ResidualPCAConvTol.
# Then ONCE per chunk: per-cluster XcorrDispatch::compute (the shared
# normalised-xcorr library used by Klusters' interactive realignSpikes,
# all channels, minScore-gated).
# ──────────────────────────────────────────────────────────────────────────
ARGS+=( -Phase2bMode    3   )            # default 0 (warm-start CEM)

# Mode-3 hyperparameters (all currently match upstream defaults; listed
# for visibility/tuning — comment out any to fall back).
ARGS+=( -ResidualPCAIter        3    )   # max outer iters per chunk
ARGS+=( -ResidualPCAComponents  3    )   # top-K residual eigvecs
ARGS+=( -ResidualPCASubK        4    )   # init K for residual VBGMM
ARGS+=( -ResidualPCAConvTol     0.01 )   # 1% label-change early-stop
ARGS+=( -ResidualPCAMinScore    0.7  )   # normalised-xcorr accept gate
# ResidualPCADominantChannels: DEPRECATED — ignored. (patch37 swapped to
# all-channels XcorrDispatch; the flag is still parsed but does nothing.)

# ──────────────────────────────────────────────────────────────────────────
# Time-shift alignment workflow (defaults to OFF in upstream)
# Each enabled boundary runs Mahal cluster-mean align (TimeShiftAlignPhase)
# then per-cluster EnergyCOM.  Mode 3's own post-loop xcorr is independent
# of these flags — runs automatically inside RunPhase2bMode3Chunk.
# ──────────────────────────────────────────────────────────────────────────
ARGS+=( -MaxTimeShift   5  )             # default 3; hard max is 5 (KK
                                          # prints a warning + clamps values
                                          # > 5).  Sets both the ± cap on
                                          # m_cumShift AND the xcorr search
                                          # radius.
ARGS+=( -TimeShiftAlignAfterPhase1   1 )  # all default 0
ARGS+=( -TimeShiftAlignAfterPhase1b  1 )
ARGS+=( -TimeShiftAlignAfterPhase2   1 )
ARGS+=( -TimeShiftAlignAfterPhase5   1 )
ARGS+=( -TimeShiftAlignAfterPhase6   1 )
ARGS+=( -TimeShiftAlignPostMerge     1 )  # legacy Phase 7a slot
ARGS+=( -EnergyCOMRealign  1 )            # default 0; per-cluster mean COM
# EnergyCOMMetric (default 1 = Σx²) left at default.

# ──────────────────────────────────────────────────────────────────────────
# Chunked CEM tuning — only the one value that diverges from defaults
# ──────────────────────────────────────────────────────────────────────────
ARGS+=( -ChunkPreseedFraction 0.08 )      # default 0.1; roadmap test value
# ChunkMinutes (default 7.0), ChunkOverlapMinutes (default 4.0),
# TimeMergeIter (default 100): left at upstream defaults.

# ──────────────────────────────────────────────────────────────────────────
# Everything else (InitMethod=farthest, MinClusters=2, MaxClusters=200,
# MaxPossibleClusters=500, nStarts=1, nRuns=20, RandomSeed=1,
# MergeThresh=0→auto-χ²(nDims,0.99), AdaptiveMerge=1, GlobalMergeIter=0,
# MaxIter=500, ChangedThresh=0.05, FullStepEvery=10, SplitEvery=8,
# SplitRecurseDepth=8, DipSplit* defaults, TemplateMatchScore=0.85,
# TemplateMatchIters=10, CrossChunkTemplateScore=0.80, CrossChunkDriftSigma=0,
# SubspaceDims=0, SubspaceRecluster=1, VBGMM* defaults, PenaltyMix=0,
# UseFeatures="all", TimeShiftMergeEnable=1, TimeShiftSplitEnable=0,
# fSaveModel=0, DistDump=0) — all left at upstream defaults.  Override
# via extra args:  ./run-kkexp.sh 8 -MaxClusters 100 -RandomSeed 7
# ──────────────────────────────────────────────────────────────────────────

# Forward any extra CLI args collected at the top.
ARGS+=( "${EXTRA[@]}" )

# ──────────────────────────────────────────────────────────────────────────
# Run
# ──────────────────────────────────────────────────────────────────────────
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

[[ -d "${SESSION_DIR}" ]] || { echo "ERROR: SESSION_DIR not found: ${SESSION_DIR}" >&2; exit 1; }
command -v "${KKEXP}" >/dev/null || { echo "ERROR: ${KKEXP} not on PATH (set KKEXP=…)" >&2; exit 1; }

# HDF5 file-locking off — required by NTFS scratch on fuseblk, harmless
# on ext4/xfs.  Must be exported before HDF5 initialisation.
export HDF5_USE_FILE_LOCKING=FALSE

{
    echo "# ─────────────────────────────────────────────────────────────────"
    echo "# KiloKlustaKwik — $(date -Is)"
    echo "# Host:    $(hostname)"
    echo "# Binary:  $(command -v "${KKEXP}")"
    echo "# Session: ${SESSION_NAME}   group ${ELEC}"
    echo "# Loop:    Phase 2b mode 3 (residual-PCA split + Klusters xcorr realign)"
    echo "# ─────────────────────────────────────────────────────────────────"
    print_cmd
    echo "# ─────────────────────────────────────────────────────────────────"
    echo
} | tee "${LOG_FILE}"

echo "Starting…  (output → ${LOG_FILE})"

set +e
"${KKEXP}" "${ARGS[@]}" 2>&1 | tee -a "${LOG_FILE}"
rc=${PIPESTATUS[0]}
set -e

echo
echo "# Finished — exit ${rc}  ($(date -Is))"
echo "# Log: ${LOG_FILE}"
exit "${rc}"
