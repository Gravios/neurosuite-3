#!/usr/bin/env bash
# ndm_extractspikes / ndm_detectspikes --group N[,M...]: only the named spike groups
# are processed, and they are (re)processed whether or not their output exists.
#   1. without --group, groups whose .spk exists are skipped (the standing rule);
#   2. --group 2 re-extracts group 2 alone, overwriting its .spk, and leaves the
#      others' files alone; -g and --group=2,3 and repeated --group work too;
#   3. a group number outside the session, a non-number, and a group with no
#      channels are errors before any engine call; so is the engine's -R;
#   4. --check with --group checks only those groups.
# Runs the real ndm_extractspikes with stand-in engines that record which groups
# they were handed (the -c channel list has one entry per group).
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_extract_group.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/s"
printf '#!/bin/bash\necho stub\n' > "$TMP/bin/xpathReader"
for b in process_extractspikes process_extractspikes_sdiff process_extractspikes_stderiv; do
    cat > "$TMP/bin/$b" <<'ENGINE'
#!/bin/bash
# stand-in: record the groups handed over (one -c entry each), write the .spk/.nst
stem="${@: -1}"; groups=0
while [ $# -gt 0 ]; do
    case "$1" in -c) groups=$(echo "$2" | tr ':' '\n' | wc -l); shift 2 ;; *) shift ;; esac
done
for i in $(seq 1 $groups); do
    echo "res.$i $(( $(wc -c < "$stem.res.$i") / 8 ))" >> "$ARGLOG"
    echo new > "$stem.spk.$i"; : > "$stem.nst.$i"
done
exit 0
ENGINE
done
chmod +x "$TMP/bin/"*
cd "$TMP/s"
export ARGLOG="$TMP/args.log"
fails=0
check() { if eval "$1"; then echo "  ok:   $2"; else echo "  FAIL: $2"; fails=$((fails + 1)); fi; }

cat > s.yaml <<'YAML'
acquisitionSystem: {nBits: 16, nChannels: 8, samplingRate: 20000}
spikeDetection:
  channelGroups:
    - channels: [0, 1]
      nSamples: 32
      peakSampleIndex: 11
    - channels: [2, 3]
      nSamples: 32
      peakSampleIndex: 11
    - channels: [4, 5]
      nSamples: 32
      peakSampleIndex: 11
    - channels: []
      nSamples: 32
      peakSampleIndex: 11
programs:
  - name: ndm_detectspikes
    parameters:
      - {name: method, value: standard, status: Optional}
  - name: ndm_extractspikes
    parameters:
      - {name: method, value: standard, status: Optional}
      - {name: dedupSamples, value: -1, status: Optional}
YAML
: > s.fil
res() { head -c $(( $2 * 8 )) /dev/zero > "s.res.standard.$1"; }   # group, spikes
res 1 10; res 2 20; res 3 30
old_spk() { for g in "$@"; do echo old > "s.spk.standard.$g"; : > "s.nst.standard.$g"; done; }

run() {   # [flags] -> exit status; engine log in $ARGLOG, output in out.log
    rm -f "$ARGLOG"
    PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/ndm_extractspikes" s "$@" > out.log 2>&1
}
logged() { sort "$ARGLOG" 2>/dev/null | tr '\n' ' '; }

echo "without --group:"
rm -f s.spk.* s.nst.*; old_spk 1 2 3
run; st=$?
check '[ $st -eq 0 ] && [ ! -e "$ARGLOG" ]' "every .spk present: nothing to do, no engine call"
check 'grep -q "nothing to do" out.log' "and says so"
rm -f s.spk.* s.nst.*; old_spk 1 3
run; st=$?
check '[ $st -eq 0 ] && [ "$(logged)" = "res.1 20 " ]' "only the group without a .spk is extracted (group 2: $(logged))"
check '[ "$(cat s.spk.standard.1)" = old ] && [ "$(cat s.spk.standard.3)" = old ]' "the others' .spk are untouched"

echo "--group 2:"
rm -f s.spk.* s.nst.*; old_spk 1 2 3
run --group 2; st=$?
check '[ $st -eq 0 ] && [ "$(logged)" = "res.1 20 " ]' "group 2 alone is handed to the engine, .spk present or not ($(logged))"
check '[ "$(cat s.spk.standard.2)" = new ]' "its .spk is overwritten"
check '[ "$(cat s.spk.standard.1)" = old ] && [ "$(cat s.spk.standard.3)" = old ]' "groups 1 and 3 keep theirs"
check 'grep -q "group(s) 2 only" out.log' "the run says which groups it is restricted to"

echo "spellings:"
rm -f s.spk.* s.nst.*; old_spk 1 2 3
run -g 3; st=$?
check '[ $st -eq 0 ] && [ "$(logged)" = "res.1 30 " ]' "-g 3 ($(logged))"
run --group=2,3; st=$?
check '[ $st -eq 0 ] && [ "$(logged)" = "res.1 20 res.2 30 " ]' "--group=2,3 ($(logged))"
run --group 1 --group 3; st=$?
check '[ $st -eq 0 ] && [ "$(logged)" = "res.1 10 res.2 30 " ]' "repeated --group ($(logged))"
run --groups 1 --force; st=$?
check '[ $st -eq 0 ] && [ "$(logged)" = "res.1 10 " ]' "--groups with --force still restricts ($(logged))"

echo "refusals:"
rm -f s.spk.* s.nst.*
run --group 9; st=$?
check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ] && grep -q "spike groups 1-4" out.log' "a group outside the session file: error, no engine call"
run --group 4; st=$?
check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ] && grep -q "no channels" out.log' "a group with no channels: error"
run --group six; st=$?
check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ] && grep -q "not a spike group number" out.log' "a non-number: error"
run -R 2; st=$?
check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ] && grep -q "unknown option: -R" out.log && grep -q -- "--group N" out.log' "-R is no user option: refused, usage names --group"

echo "--check with --group:"
rm -f s.res.standard.1
run --group 2 --check; st=$?
check '[ $st -eq 0 ] && [ ! -e "$ARGLOG" ]' "checks group 2 only (group 1's missing .res is not its business), no engine call"
run --group 1 --check; st=$?
check '[ $st -ne 0 ] && grep -q "s.res.standard.1" out.log' "checks group 1: its missing .res is reported"

echo
[ $fails -eq 0 ] && echo "test_extract_group: PASS" || echo "test_extract_group: $fails FAIL"
exit $fails
