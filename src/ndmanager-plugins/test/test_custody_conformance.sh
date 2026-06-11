#!/usr/bin/env bash
# bash runner for the shared chain-of-custody conformance vectors.
#
# Executes the SAME custody_vectors.tsv that the C++ and Python tests run,
# against the bash mirror ndm_custody.  One table, three runners.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_custody_conformance.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/../scripts/ndm_custody"
VECTORS="${1:-$HERE/../../libneurosuite-core/test/custody_vectors.tsv}"

# Split a tab-separated line into array F, preserving empty fields.  (bash's
# read -a collapses runs of tabs because tab is IFS whitespace, so we cannot
# use it here.)
split_tab() {
    local s="$1"
    F=()
    while true; do
        F+=("${s%%$'\t'*}")
        case "$s" in
            *$'\t'*) s="${s#*$'\t'}" ;;
            *) break ;;
        esac
    done
}

ran=0; fail=0
TMP="$(mktemp -d)"; B="$TMP/sess"
trap 'rm -rf "$TMP"' EXIT

while IFS= read -r line; do
    [ -z "$line" ] && continue
    [[ "$line" == \#* ]] && continue
    split_tab "$line"
    kind="${F[0]}"
    ran=$((ran+1))
    case "$kind" in
        classify)
            got="$(ndm_classify "${F[1]}")"
            [ "$got" = "${F[2]}" ] || { echo "FAIL classify ${F[1]} -> ${F[2]} (got $got)"; fail=$((fail+1)); }
            ;;
        method_of)
            got="$(ndm_method_of "${F[1]}")"
            [ "$got" = "${F[2]:-}" ] || { echo "FAIL method_of ${F[1]} -> '${F[2]:-}' (got '$got')"; fail=$((fail+1)); }
            ;;
        parse_anchor)
            ndm_parse_anchor "${F[1]}"
            want_ok="${F[7]:-0}"; ok=1
            [ "$NDM_A_OK" = "$want_ok" ] || ok=0
            if [ "$want_ok" = 1 ]; then
                { [ "$NDM_A_BASE" = "${F[2]:-}" ] && [ "$NDM_A_TYPE" = "${F[3]:-}" ] && \
                  [ "$NDM_A_METHOD" = "${F[4]:-}" ] && [ "$NDM_A_GROUP" = "${F[5]:-}" ] && \
                  [ "$NDM_A_SUFFIX" = "${F[6]:-}" ]; } || ok=0
            fi
            [ "$ok" = 1 ] || { echo "FAIL parse_anchor ${F[1]} (ok=$NDM_A_OK base=$NDM_A_BASE type=$NDM_A_TYPE method=$NDM_A_METHOD grp=$NDM_A_GROUP sfx=$NDM_A_SUFFIX)"; fail=$((fail+1)); }
            ;;
        resolve)
            IFS=',' read -r -a sufs <<< "${F[1]}"
            for s in "${sufs[@]}"; do [ -n "$s" ] && : > "$B.$s"; done
            path="$(ndm_resolve "$B" "${F[2]}" "${F[3]}" "${F[4]}")"
            if [ -f "$path" ]; then found=1; else found=0; fi
            want_found="${F[6]:-0}"
            { [ "${path##*/}" = "${F[5]}" ] && [ "$found" = "$want_found" ]; } || \
                { echo "FAIL resolve ${F[2]}/${F[3]}/${F[4]} -> ${F[5]} found=$want_found (got ${path##*/} found=$found)"; fail=$((fail+1)); }
            for s in "${sufs[@]}"; do [ -n "$s" ] && rm -f "$B.$s"; done
            ;;
        *)
            echo "FAIL unknown vector kind '$kind'"; fail=$((fail+1)) ;;
    esac
done < "$VECTORS"

echo "custody conformance (bash): $ran checks, $fail failed"
if [ "$fail" = 0 ]; then echo "ALL CUSTODY CONFORMANCE TESTS PASS"; exit 0; else exit 1; fi
