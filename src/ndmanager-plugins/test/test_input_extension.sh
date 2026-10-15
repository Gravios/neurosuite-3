#!/usr/bin/env bash
# inputExtension: a leading dot is ignored, an explicitly named input that is
# missing stops the step with an error (instead of "skipping this step"), and
# an unset value still falls back to the default input.  Runs the real
# ndm_hipass and ndm_lfp with stand-in filter programs that only print their
# arguments.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_input_extension.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/s"
for b in process_butterworth process_medianfilter process_resample xpathReader; do
    printf '#!/bin/bash\necho "%s ARGS: $*"\n' "$b" > "$TMP/bin/$b"
done
chmod +x "$TMP/bin/"*
cd "$TMP/s"
: > s.dat; : > s.lfp; : > s.emgclean

session_yaml() {   # program  inputExtension-line
    cat > s.yaml <<YAML
acquisitionSystem: {nChannels: 4, samplingRate: 20000, nBits: 16}
fieldPotentials: {lfpSamplingRate: 1250}
programs:
  - name: $1
    parameters:
      - {name: samplingRate, value: 1250, status: Optional}
$2
YAML
}

run() {   # script → "<exit status>|<argument line or error>"
    rm -f s.fil s.lfp.out
    local out
    out=$(PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/$1" s 2>&1)
    local st=$?
    echo "$st|$(echo "$out" | grep -o 'process_[a-z]* ARGS: .*\|does not exist\|skipping this step' | head -1)"
}

fail=0
check() {   # name expected actual
    if [ "$2" == "$3" ]; then echo "  [PASS] $1"
    else echo "  [FAIL] $1: expected [$2], got [$3]"; fail=$((fail + 1)); fi
}

line() { echo "      - {name: inputExtension, value: '$1', status: Optional}"; }

session_yaml ndm_hipass "$(line .emgclean)"
check "hipass: leading dot ignored" "0|process_butterworth ARGS: -n 4 -s 20000 -l 800 -u 0 -o 3 -p auto -v s.emgclean s.fil" "$(run ndm_hipass)"
session_yaml ndm_hipass "$(line emgclean)"
check "hipass: plain extension" "0|process_butterworth ARGS: -n 4 -s 20000 -l 800 -u 0 -o 3 -p auto -v s.emgclean s.fil" "$(run ndm_hipass)"
session_yaml ndm_hipass "$(line nosuch)"
r=$(run ndm_hipass)
check "hipass: missing explicit input is an error" "does not exist" "${r#*|}"
[ "${r%%|*}" != 0 ] && echo "  [PASS] hipass: nonzero exit" || { echo "  [FAIL] hipass: exit 0"; fail=$((fail + 1)); }
session_yaml ndm_hipass ""
check "hipass: unset → .dat" "0|process_butterworth ARGS: -n 4 -s 20000 -l 800 -u 0 -o 3 -p auto -v s.dat s.fil" "$(run ndm_hipass)"
rm s.dat
r=$(run ndm_hipass)
check "hipass: unset and no .dat still just skips" "skipping this step" "${r#*|}"
: > s.dat

session_yaml ndm_lfp "$(line ..emgclean)"
rm -f s.lfp
r=$(run ndm_lfp)
case "$r" in *"s.emgclean"*) echo "  [PASS] lfp: leading dots ignored" ;;
             *) echo "  [FAIL] lfp: leading dots ignored: got [$r]"; fail=$((fail + 1)) ;; esac

# The command-line override ndm_detectspikes / ndm_extractspikes pass in
# (--inputExtension) wins over the YAML and is normalised the same way.
session_yaml ndm_detectspikes "$(line lfp)"
r=$(
    set +u
    source "$SCRIPTS/ndm_functions" >/dev/null 2>&1
    program=ndm_detectspikes; session=s
    ndm_input_extension fil ".emgclean"; a="$inputExtension/$NDM_INPUT_EXPLICIT"
    ndm_input_extension fil "";          b="$inputExtension/$NDM_INPUT_EXPLICIT"
    session_yaml ndm_detectspikes "" >/dev/null
    ndm_input_extension fil "";          c="$inputExtension/$NDM_INPUT_EXPLICIT"
    echo "$a $b $c"
)
check "override > YAML > default" "emgclean/1 lfp/1 fil/0" "$r"

[ $fail -eq 0 ] && echo "OK" || echo "FAILED ($fail)"
exit $fail
