#!/usr/bin/env bash
# =============================================================================
# sweep_tier1.sh — KlustaKwik Tier 1 parameter sweep
#
# Grid: MergeThresh × PenaltyMix  (5×5 = 25 runs)
#
# Usage:
#   ./sweep_tier1.sh <data_dir> <filebase> <elec_group> [n_threads]
#
# Example:
#   ./sweep_tier1.sh /data/jg05-20120316 jg05-20120316 7 22
#
# Output:
#   <data_dir>/sweep_tier1/mt<MT>_pm<PM>/
#     <filebase>.clu.<elec>   — KlustaKwik output
#     <filebase>.klg.<elec>   — run log
#     params.txt              — parameters used
#
# After all runs complete, evaluate with:
#   python3 eval_tier1.py <data_dir> <filebase> <elec_group>
# =============================================================================

set -euo pipefail

DATA_DIR="${1:?Usage: $0 <data_dir> <filebase> <elec> [n_threads]}"
FILEBASE="${2:?}"
ELEC="${3:?}"
N_THREADS="${4:-$(nproc)}"

# ── Fixed parameters (tuned for jg05-20120316 group 7: 25 dims, 79 min, 32552 Hz)
MIN_CLUSTERS=5
MAX_CLUSTERS=30
MAX_POSSIBLE=500
CHUNK_MINUTES=10
SAMPLING_RATE=32552
N_STARTS=1
SPLIT_EVERY=15
GLOBAL_MERGE_ITER=150
TIME_MERGE_ITER=220
USE_FEATURES=all        # "all" passes safely with the UseFeatures-length fix
DIST_THRESH=6.9
FULL_STEP_EVERY=10
CHANGED_THRESH=0.05
MAX_ITER=500

# ── Tier 1 grid
MERGE_THRESH_VALUES=(30 36 43 51 65)   # chi²(24): p=0.80..0.9999
PENALTY_MIX_VALUES=(0.0 0.25 0.5 0.75 1.0)   # AIC → BIC

# ── Locate KlustaKwik binary
KK_BIN=$(command -v KlustaKwik 2>/dev/null || true)
if [[ -z "$KK_BIN" ]]; then
    echo "ERROR: KlustaKwik not found on PATH. Build it and add to PATH."
    exit 1
fi

SWEEP_DIR="${DATA_DIR}/sweep_tier1"
mkdir -p "$SWEEP_DIR"

TOTAL=$(( ${#MERGE_THRESH_VALUES[@]} * ${#PENALTY_MIX_VALUES[@]} ))
RUN=0

echo "========================================================"
echo "  Tier 1 sweep: ${TOTAL} runs"
echo "  Data:  ${DATA_DIR}/${FILEBASE}.fet.${ELEC}"
echo "  Threads: ${N_THREADS}"
echo "  Output: ${SWEEP_DIR}"
echo "========================================================"
echo ""

for MT in "${MERGE_THRESH_VALUES[@]}"; do
    for PM in "${PENALTY_MIX_VALUES[@]}"; do
        RUN=$(( RUN + 1 ))
        TAG="mt${MT}_pm${PM}"
        RUN_DIR="${SWEEP_DIR}/${TAG}"
        mkdir -p "$RUN_DIR"

        # Check if this run already produced a .clu file (allows resuming)
        CLU_OUT="${RUN_DIR}/${FILEBASE}.clu.${ELEC}"
        if [[ -f "$CLU_OUT" ]]; then
            echo "[${RUN}/${TOTAL}] SKIP (exists): MT=${MT} PM=${PM}"
            continue
        fi

        echo "[${RUN}/${TOTAL}] START: MergeThresh=${MT} PenaltyMix=${PM}  $(date '+%H:%M:%S')"

        # Save params for reference
        cat > "${RUN_DIR}/params.txt" << PARAMS
MergeThresh     ${MT}
PenaltyMix      ${PM}
MinClusters     ${MIN_CLUSTERS}
MaxClusters     ${MAX_CLUSTERS}
MaxPossibleClusters ${MAX_POSSIBLE}
ChunkMinutes    ${CHUNK_MINUTES}
SamplingRate    ${SAMPLING_RATE}
nStarts         ${N_STARTS}
SplitEvery      ${SPLIT_EVERY}
GlobalMergeIter ${GLOBAL_MERGE_ITER}
TimeMergeIter   ${TIME_MERGE_ITER}
UseFeatures     ${USE_FEATURES}
DistThresh      ${DIST_THRESH}
MaxIter         ${MAX_ITER}
PARAMS

        # Symlink the input files so KlustaKwik can find them by name in RUN_DIR
        for EXT in fet res spk clu; do
            SRC="${DATA_DIR}/${FILEBASE}.${EXT}.${ELEC}"
            LINK="${RUN_DIR}/${FILEBASE}.${EXT}.${ELEC}"
            [[ -f "$SRC" && ! -e "$LINK" ]] && ln -s "$SRC" "$LINK" || true
        done

        # Run KlustaKwik from inside the run directory so .clu and .klg land there
        (
            cd "$RUN_DIR"
            OMP_NUM_THREADS="${N_THREADS}" \
            "$KK_BIN" "${FILEBASE}" "${ELEC}" \
                -MinClusters    "${MIN_CLUSTERS}" \
                -MaxClusters    "${MAX_CLUSTERS}" \
                -MaxPossibleClusters "${MAX_POSSIBLE}" \
                -nStarts        "${N_STARTS}" \
                -UseFeatures    "${USE_FEATURES}" \
                -PenaltyMix     "${PM}" \
                -ChunkMinutes   "${CHUNK_MINUTES}" \
                -SamplingRate   "${SAMPLING_RATE}" \
                -MergeThresh    "${MT}" \
                -GlobalMergeIter "${GLOBAL_MERGE_ITER}" \
                -TimeMergeIter  "${TIME_MERGE_ITER}" \
                -SplitEvery     "${SPLIT_EVERY}" \
                -DistThresh     "${DIST_THRESH}" \
                -FullStepEvery  "${FULL_STEP_EVERY}" \
                -ChangedThresh  "${CHANGED_THRESH}" \
                -MaxIter        "${MAX_ITER}" \
                -SaveIntermediates 0 \
                -Screen 0 \
                -Log 1 \
                2>&1 | tee "${TAG}.stdout.txt"
        )

        if [[ -f "$CLU_OUT" ]]; then
            N_CLU=$(head -1 "$CLU_OUT")
            echo "[${RUN}/${TOTAL}] DONE:  MT=${MT} PM=${PM}  →  ${N_CLU} clusters  $(date '+%H:%M:%S')"
        else
            echo "[${RUN}/${TOTAL}] FAIL:  MT=${MT} PM=${PM}  — no .clu output!"
        fi
        echo ""
    done
done

echo "========================================================"
echo "  All ${TOTAL} runs complete."
echo "  Evaluate with:"
echo "    python3 eval_tier1.py ${DATA_DIR} ${FILEBASE} ${ELEC}"
echo "========================================================"
