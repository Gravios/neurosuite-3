#!/usr/bin/env zsh
# run_klustakwik_macos.sh - Run KlustaKwik on all spike groups (macOS / zsh)
#
# Sampling rate  : parsed from <basename>.yaml  (field: SamplingRate / samplingRate)
# UseFeatures    : derived from feature count in binary .fet header (first int32)
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
            if [[ -z "$BASENAME" ]]; then
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
if [[ -z "$BASENAME" ]]; then
    local_xml=(*.xml(N))
    if [[ ${#local_xml[@]} -eq 0 ]]; then
        echo "Could not detect basename. Supply it as the first argument." >&2
        exit 1
    fi
    BASENAME="${local_xml[1]:r}"
    echo "No basename supplied – using detected: $BASENAME"
fi

# ---------------------------------------------------------------------------
# Parse sampling rate from YAML
# ---------------------------------------------------------------------------
YAML_FILE="${BASENAME}.yaml"
if [[ ! -f "$YAML_FILE" ]]; then
    echo "YAML file not found: $YAML_FILE" >&2
    exit 1
fi

SAMPLING_RATE=$(grep -iE '^\s*(SamplingRate|sampling_rate)\s*:' "$YAML_FILE" \
    | head -1 \
    | sed 's/.*:\s*//' \
    | tr -d '[:space:]"')

if [[ -z "$SAMPLING_RATE" ]]; then
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
fet_files=(${BASENAME}.fet.*(N))

if [[ ${#fet_files[@]} -eq 0 ]]; then
    echo "No .fet files found for basename '${BASENAME}'" >&2
    exit 1
fi

for FET_FILE in "${fet_files[@]}"; do

    GROUP="${FET_FILE##*.fet.}"

    if [[ "$GROUP" -eq 0 ]]; then
        echo "Skipping group 0"
        continue
    fi

    # Binary .fet format: first 4 bytes = little-endian int32 = number of features
    # (this count excludes the trailing timestamp column)
    N_FEATURES=$(od -An -t d4 -N 4 "$FET_FILE" | tr -d ' \n')

    if ! [[ "$N_FEATURES" =~ ^[0-9]+$ ]] || [[ "$N_FEATURES" -le 0 ]]; then
        echo "Group $GROUP: could not read feature count from $FET_FILE (got: '$N_FEATURES'), skipping" >&2
        continue
    fi

    USE_FEATURES=$(printf '1%.0s' {1..$N_FEATURES})

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
