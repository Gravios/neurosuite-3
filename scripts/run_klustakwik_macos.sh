#!/bin/bash
# run_klustakwik_macos.sh - Run KlustaKwik on all spike groups (macOS)
#
# Sampling rate  : parsed from <basename>.yaml  (field: SamplingRate / samplingRate)
# UseFeatures    : derived from column count in each .fet file
# OMP_NUM_THREADS: derived from sysctl hw.logicalcpu
#
# Usage:
#   run_klustakwik_macos.sh [basename] [options]
#
# All KlustaKwik options can be overridden on the command line, e.g.:
#   run_klustakwik_macos.sh jg05-20120316 -MaxClusters 16 -nStarts 3

set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults for every KlustaKwik option
# ---------------------------------------------------------------------------
OPT_MinClusters=5
OPT_MaxClusters=12
OPT_ChunkMinutes=10
OPT_MergeThresh=60
OPT_GlobalMergeIter=50
OPT_TimeMergeIter=45
OPT_PenaltyMix=0.0
OPT_nStarts=1
OPT_SplitEvery=40
OPT_SaveIntermediates=0
OPT_Log=1
OPT_Screen=0

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
BASENAME=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        -MinClusters)        OPT_MinClusters="$2";        shift 2 ;;
        -MaxClusters)        OPT_MaxClusters="$2";        shift 2 ;;
        -ChunkMinutes)       OPT_ChunkMinutes="$2";       shift 2 ;;
        -MergeThresh)        OPT_MergeThresh="$2";        shift 2 ;;
        -GlobalMergeIter)    OPT_GlobalMergeIter="$2";    shift 2 ;;
        -TimeMergeIter)      OPT_TimeMergeIter="$2";      shift 2 ;;
        -PenaltyMix)         OPT_PenaltyMix="$2";         shift 2 ;;
        -nStarts)            OPT_nStarts="$2";             shift 2 ;;
        -SplitEvery)         OPT_SplitEvery="$2";         shift 2 ;;
        -SaveIntermediates)  OPT_SaveIntermediates="$2";  shift 2 ;;
        -Log)                OPT_Log="$2";                 shift 2 ;;
        -Screen)             OPT_Screen="$2";              shift 2 ;;
        -*)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
        *)
            if [ -z "$BASENAME" ]; then
                BASENAME="$1"
            else
                echo "Unexpected argument: $1" >&2
                exit 1
            fi
            shift
            ;;
    esac
done

# Auto-detect basename from .xml if not supplied
if [ -z "$BASENAME" ]; then
    BASENAME=$(ls *.xml 2>/dev/null | head -1 | sed 's/\.xml$//')
    if [ -z "$BASENAME" ]; then
        echo "Could not detect basename. Supply it as the first argument." >&2
        exit 1
    fi
    echo "No basename supplied – using detected: $BASENAME"
fi

# ---------------------------------------------------------------------------
# Parse sampling rate from YAML
# ---------------------------------------------------------------------------
YAML_FILE="${BASENAME}.yaml"
if [ ! -f "$YAML_FILE" ]; then
    echo "YAML file not found: $YAML_FILE" >&2
    exit 1
fi

# Accept keys: SamplingRate, samplingRate, sampling_rate
SAMPLING_RATE=$(grep -iE '^\s*(SamplingRate|sampling_rate)\s*:' "$YAML_FILE" \
    | head -1 \
    | sed 's/.*:\s*//' \
    | tr -d '[:space:]"')

if [ -z "$SAMPLING_RATE" ]; then
    echo "Could not parse SamplingRate from $YAML_FILE" >&2
    exit 1
fi

echo "SamplingRate    = $SAMPLING_RATE  (from $YAML_FILE)"

# ---------------------------------------------------------------------------
# OMP_NUM_THREADS from sysctl (macOS)
# ---------------------------------------------------------------------------
OMP_NUM_THREADS=$(sysctl -n hw.logicalcpu)
echo "OMP_NUM_THREADS = $OMP_NUM_THREADS  (from sysctl hw.logicalcpu)"

# ---------------------------------------------------------------------------
# Iterate over every spike group that has a .fet file
# ---------------------------------------------------------------------------
shopt -s nullglob
FET_FILES=( "${BASENAME}".fet.* )

if [ ${#FET_FILES[@]} -eq 0 ]; then
    echo "No .fet files found for basename '${BASENAME}'" >&2
    exit 1
fi

for FET_FILE in "${FET_FILES[@]}"; do

    GROUP="${FET_FILE##*.fet.}"

    # Skip group 0 (noise / unsorted channel)
    if [ "$GROUP" -eq 0 ] 2>/dev/null; then
        echo "Skipping group 0"
        continue
    fi

    # First line of a .fet file = total column count;
    # last column is the timestamp, the rest are features.
    TOTAL_COLS=$(head -1 "$FET_FILE")
    N_FEATURES=$(( TOTAL_COLS - 1 ))

    if [ "$N_FEATURES" -le 0 ]; then
        echo "Group $GROUP: no features (TOTAL_COLS=$TOTAL_COLS), skipping" >&2
        continue
    fi

    USE_FEATURES=$(printf '1%.0s' $(seq 1 "$N_FEATURES"))

    echo ""
    echo "=== Group $GROUP | features: $N_FEATURES | UseFeatures: $USE_FEATURES ==="

    OMP_NUM_THREADS=$OMP_NUM_THREADS \
    KlustaKwik "$BASENAME" "$GROUP" \
        -MinClusters        "$OPT_MinClusters" \
        -MaxClusters        "$OPT_MaxClusters" \
        -UseFeatures        "$USE_FEATURES" \
        -ChunkMinutes       "$OPT_ChunkMinutes" \
        -SamplingRate       "$SAMPLING_RATE" \
        -MergeThresh        "$OPT_MergeThresh" \
        -GlobalMergeIter    "$OPT_GlobalMergeIter" \
        -TimeMergeIter      "$OPT_TimeMergeIter" \
        -PenaltyMix         "$OPT_PenaltyMix" \
        -nStarts            "$OPT_nStarts" \
        -SplitEvery         "$OPT_SplitEvery" \
        -SaveIntermediates  "$OPT_SaveIntermediates" \
        -Log                "$OPT_Log" \
        -Screen             "$OPT_Screen"

done

echo ""
echo "Done."
