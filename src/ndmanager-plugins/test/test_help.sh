#!/usr/bin/env bash
# Every ndm_* command answers -h and --help with a description and exits 0,
# without running anything, even inside a session folder; and declines
# NDManager's external-plugin handshake (--ndm-version / --ndm-describe)
# without running anything.
#
# Each command runs in a folder holding a minimal session named after it
# (s/s.yaml, s/s.dat), the session a step falls back to when it gets no
# parameter file.  The folder must be unchanged afterwards.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_help.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fails=0
check() { if eval "$1"; then :; else echo "  FAIL: $2"; fails=$((fails + 1)); fi; }

runner() {   # the interpreter of a script, from its first line
    case "$(head -1 "$1")" in
        *python*) echo python3 ;;
        *) echo bash ;;
    esac
}
snapshot() { (cd "$1" && find . -type f -exec cksum {} + | sort); }

n=0
for path in "$SCRIPTS"/ndm_*; do
    name=$(basename "$path")
    case "$name" in *.*|ndm_functions) continue ;; esac   # docbook pages, the helper library
    n=$((n + 1))
    sess="$TMP/$name/s"
    mkdir -p "$sess"
    printf 'acquisitionSystem: {nBits: 16, nChannels: 4, samplingRate: 20000}\n' > "$sess/s.yaml"
    head -c 8000 /dev/zero > "$sess/s.dat"
    before=$(snapshot "$sess")
    run=$(runner "$path")

    for flag in -h --help; do
        out=$(cd "$sess" && PATH="$SCRIPTS:$PATH" timeout 30 "$run" "$path" "$flag" 2>&1 </dev/null)
        rc=$?
        check '[ $rc -eq 0 ]' "$name $flag exits $rc"
        check '[ $(printf "%s" "$out" | wc -l) -ge 2 ]' "$name $flag prints no description"
        check '! printf "%s" "$out" | grep -qi "unknown option\|not found, skipping\|^s - "' \
              "$name $flag ran or refused instead of helping: $(printf '%s' "$out" | head -2 | tr '\n' ' ')"
    done
    if [ "$run" = bash ]; then
        for flag in --ndm-version --ndm-describe; do
            out=$(cd "$sess" && PATH="$SCRIPTS:$PATH" timeout 30 bash "$path" "$flag" 2>/dev/null </dev/null)
            rc=$?
            check '[ $rc -ne 0 ] && ! printf "%s" "$out" | grep -q "^ndm-plugin-protocol"' \
                  "$name $flag answered the plugin handshake (rc $rc)"
        done
    fi
    after=$(snapshot "$sess")
    check '[ "$before" = "$after" ]' "$name changed its session folder"
done

echo
echo "$n commands checked"
if [ $fails -eq 0 ]; then echo "test_help: PASS"; else echo "test_help: FAIL ($fails)"; fi
exit $((fails > 0))
