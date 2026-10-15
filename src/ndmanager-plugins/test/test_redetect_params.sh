#!/usr/bin/env bash
# ndm_redetectspikes takes its detection parameters from ndm_detectspikes
# when its own program block does not set them, and its own block wins when
# it does.  Runs the script's parameter-reading lines (not the processing)
# against two session YAMLs.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_redetect_params.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

cat > inherit.yaml <<'YAML'
programs:
  - name: ndm_hipass
    parameters:
      - {name: windowHalfLength, value: 16, status: Optional}
  - name: ndm_detectspikes
    parameters:
      - {name: thresholdFactor,  value: 1.5, status: Mandatory}
      - {name: refractoryPeriod, value: 16,  status: Mandatory}
      - {name: peakSearchLength, value: 32,  status: Mandatory}
      - {name: start,            value: 0,   status: Mandatory}
      - {name: duration,         value: 60,  status: Mandatory}
  - name: ndm_redetectspikes
    parameters: []
YAML

cat > own.yaml <<'YAML'
programs:
  - name: ndm_hipass
    parameters:
      - {name: windowHalfLength, value: 16, status: Optional}
  - name: ndm_detectspikes
    parameters:
      - {name: thresholdFactor,  value: 1.5, status: Mandatory}
      - {name: refractoryPeriod, value: 16,  status: Mandatory}
      - {name: peakSearchLength, value: 32,  status: Mandatory}
      - {name: start,            value: 0,   status: Mandatory}
      - {name: duration,         value: 60,  status: Mandatory}
  - name: ndm_redetectspikes
    parameters:
      - {name: thresholdFactor,  value: 1.2, status: Optional}
YAML

# The script's own parameter lines, so the test follows the script.
grep -E '^(windowHalfLength|thresholdFactor|refractoryPeriod|peakSearchLength|start|duration|memCapGB)=\$\(read_script_parameter' \
    "$SCRIPTS/ndm_redetectspikes" > reads.sh
[ "$(wc -l < reads.sh)" -eq 7 ] || { echo "  [FAIL] expected 7 parameter lines in ndm_redetectspikes"; exit 1; }

fail=0
check() {   # name expected actual
    if [ "$2" == "$3" ]; then echo "  [PASS] $1"
    else echo "  [FAIL] $1: expected [$2], got [$3]"; fail=$((fail + 1)); fi
}

read_all() {   # session
    (
        set +u
        source "$SCRIPTS/ndm_functions" >/dev/null 2>&1
        program=ndm_redetectspikes
        session=$1
        source ./reads.sh
        echo "$windowHalfLength $thresholdFactor $refractoryPeriod $peakSearchLength $start $duration"
    )
}

check "inherits ndm_detectspikes"      "16 1.5 16 32 0 60" "$(read_all inherit)"
check "own block overrides one value"  "16 1.2 16 32 0 60" "$(read_all own)"

[ $fail -eq 0 ] && echo "OK" || echo "FAILED ($fail)"
exit $fail
