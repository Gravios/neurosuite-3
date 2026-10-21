#!/usr/bin/env bash
# ndm_extractspikes: the session-file check (ndm_extract_preflight, --check) and
# --allMethods.  Runs the real scripts with stand-in engines that record their
# arguments and write the outputs a -R run writes.
#
#   1. --check refuses, before any engine runs, a session file that does not
#      hold what a method needs: missing geometry, a peak outside the window,
#      a channel beyond nChannels, a one-channel group under a spatial order,
#      stderiv_C<n> with a group lacking sdiffPairs / with the wrong entry
#      count / an index beyond the group / a pattern of another order, a
#      feature-space token, a group without .res; and accepts a complete one;
#   2. a refused method stops a normal run too (no engine call);
#   3. --allMethods finds the waveform methods of the session's files
#      (.spk/.nst/.fet/.pca/.clu, a _D feature token standing for its
#      waveform method, an opaque token skipped), re-extracts every group of
#      each method the session file covers (overwriting), reports the others
#      and exits non-zero, and lists the feature spaces to re-project;
#      --allMethods --check runs nothing.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_extract_all_methods.sh
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
echo "$(basename "$0") $*" >> "$ARGLOG"
stem="${@: -1}"; chans=
while [ $# -gt 0 ]; do case "$1" in -c) chans=$2; shift 2 ;; *) shift ;; esac; done
i=0
for c in $(echo "$chans" | tr ':' ' '); do i=$((i + 1)); echo new > "$stem.spk.$i"; done
exit 0
ENGINE
done
chmod +x "$TMP/bin/"*
cd "$TMP/s"
export ARGLOG="$TMP/args.log"
fails=0
check() { if eval "$1"; then echo "  ok:   $2"; else echo "  FAIL: $2"; fails=$((fails + 1)); fi; }

PAIRS1='"0-1+2,1-0+2,2-1+3,3-2+1"'
PAIRS2='"0-1+3,1-0+2,2-3+1,3-2+0"'
session_yaml() {   # group-2 extra lines (replace its sdiffPairs line), node method
    cat > s.yaml <<YAML
acquisitionSystem: {nBits: 16, nChannels: 8, samplingRate: 20000}
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 32
      peakSampleIndex: 11
      sdiffPairs: $PAIRS1
    - channels: [4, 5, 6, 7]
${1:-      nSamples: 32
      peakSampleIndex: 11
      sdiffPairs: $PAIRS2}
programs:
  - name: ndm_extractspikes
    parameters:
      - {name: method, value: ${2:-standard}, status: Optional}
      - {name: dedupSamples, value: -1, status: Optional}
      - {name: noiseWindows, value: 0, status: Optional}
YAML
}
reset_files() {
    rm -f s.* "$ARGLOG"
    head -c 96000 /dev/zero > s.fil
    for g in 1 2; do head -c 80 /dev/zero > s.res.standard.$g; done
}
xs() {   # args -> exit status; output in out.log
    PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/ndm_extractspikes" s "$@" > out.log 2>&1
}

echo "--check, complete session file:"
reset_files; session_yaml
for m in standard stderiv_C5 stderiv_S3 sdiff; do
    session_yaml; xs --method $m --check; st=$?
    check '[ $st -eq 0 ] && [ ! -e "$ARGLOG" ]' "$m: accepted, nothing run"
done

echo "--check, refusals:"
refuse() {   # label  group-2-lines  method  expected-message
    session_yaml "$2"; rm -f "$ARGLOG"; xs --method "$3" --check; st=$?
    want=$4
    check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ] && grep -qF -- "$want" out.log' "$1"
}
refuse "missing nSamples" "      peakSampleIndex: 11" standard "group 2: nSamples is missing"
refuse "missing peakSampleIndex" "      nSamples: 32" standard "group 2: peakSampleIndex is missing"
refuse "peak outside the window" "      nSamples: 32
      peakSampleIndex: 32" standard "is not inside the 32-sample window"
refuse "C5 without sdiffPairs on group 2" "      nSamples: 32
      peakSampleIndex: 11" stderiv_C5 "group 2 has no sdiffPairs"
refuse "C5, 3 entries for 4 channels" "      nSamples: 32
      peakSampleIndex: 11
      sdiffPairs: \"0-1+2,1-0+2,2-1+3\"" stderiv_C5 "has 3 entries for 4 channels"
refuse "C5, an index beyond the group" "      nSamples: 32
      peakSampleIndex: 11
      sdiffPairs: \"0-1+2,1-0+2,2-1+3,3-2+4\"" stderiv_C5 "names channel 4"
refuse "C4 token, order-5 pattern" "      nSamples: 32
      peakSampleIndex: 11
      sdiffPairs: $PAIRS2" stderiv_C4 "is an order-5 pattern, the method says 4"
refuse "feature-space token" "" stderiv_C5_D34 "names a feature space"
refuse "unknown token" "" fiber "unknown method"
session_yaml; sed -i 's/channels: \[4, 5, 6, 7\]/channels: [4, 5, 6, 9]/' s.yaml; rm -f "$ARGLOG"; xs --method standard --check; st=$?
check '[ $st -ne 0 ] && grep -q "channel 9 is beyond nChannels (8)" out.log' "a channel beyond nChannels"
session_yaml; sed -i 's/channels: \[4, 5, 6, 7\]/channels: [4]/; s/sdiffPairs: "0-1+3,1-0+2,2-3+1,3-2+0"//' s.yaml
rm -f "$ARGLOG"; xs --method stderiv_S3 --check; st=$?
check '[ $st -ne 0 ] && grep -q "group 2 has one channel" out.log' "one-channel group under stderiv_S3"
xs --method standard --check; st=$?
check '[ $st -eq 0 ]' "... but fine under standard"
session_yaml; rm -f s.res.standard.2 "$ARGLOG"; xs --method standard --check; st=$?
check '[ $st -ne 0 ] && grep -q "group 2 has no .res" out.log' "a group without .res"

echo "a refused method stops a normal run:"
reset_files; session_yaml "      nSamples: 32
      peakSampleIndex: 11" stderiv_C5
xs; st=$?
check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ] && ! ls s.spk.* >/dev/null 2>&1' "no engine call, no .spk"

echo "--allMethods:"
make_session_files() {
    reset_files
    for g in 1 2; do echo old > s.spk.standard.$g; echo old > s.spk.stderiv_C5.$g; done
    : > s.fet.stderiv_C5_D34.1; : > s.pca.stderiv_C5_D34.1
    : > s.clu.stderiv_C5_D34.1.fiber_session_curated2
    : > s.pca.stderiv_S3.2                 # a method with features only
    : > s.fet.fibermagic.1                 # not an extraction method
}
make_session_files; session_yaml "      nSamples: 32
      peakSampleIndex: 11"                   # group 2 lacks sdiffPairs: C5 refused
xs --allMethods --check; st=$?
check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ]' "--check: refused method -> non-zero, nothing run"
check 'grep -q "method standard" out.log && grep -q "method stderiv_C5" out.log && grep -q "method stderiv_S3" out.log' "finds standard, stderiv_C5 (from .spk and the _D34 files) and stderiv_S3 (from a .pca)"
check '[ $(grep -c "^.*method stderiv_C5$" out.log) -eq 1 ]' "stderiv_C5 listed once"
check 'grep -q "fibermagic.*not of an extraction method" out.log' "the opaque token is skipped with a warning"
check 'grep -q "group 2 has no sdiffPairs" out.log' "says why stderiv_C5 is refused"

xs --allMethods; st=$?
check '[ $st -ne 0 ]' "run with a refused method exits non-zero"
check '[ $(wc -l < "$ARGLOG") -eq 2 ]' "two engine runs (standard, stderiv_S3)"
check 'grep -q "^process_extractspikes .*-R" "$ARGLOG" && grep -q "^process_extractspikes_stderiv .*-d 3 .*-R" "$ARGLOG"' "standard and stderiv_S3 engines"
check 'grep -q new s.spk.standard.1 && grep -q new s.spk.standard.2' "existing standard .spk overwritten (implied --force)"
check 'grep -q old s.spk.stderiv_C5.1' "refused stderiv_C5 .spk untouched"
check '[ -e s.spk.stderiv_S3.1 ] && [ -e s.spk.stderiv_S3.2 ]' "stderiv_S3 extracted for both groups"
check 'grep -q "not run.*stderiv_C5" out.log' "summary names the refused method"
check 'grep -q "re-project them.*stderiv_S3" out.log' "stale feature spaces listed"
check 'grep -q "ndm_refeaturize s --method stderiv_S3" out.log && grep -q "ndm_pca s --method stderiv_S3 --force" out.log' "... with both commands for an ordinary space"

make_session_files; session_yaml
xs --allMethods; st=$?
check '[ $st -eq 0 ] && [ $(wc -l < "$ARGLOG") -eq 3 ]' "complete session file: all three methods run, exit 0"
check 'grep -q -- "-P 0-1+2,1-0+2,2-1+3,3-2+1:0-1+3,1-0+2,2-3+1,3-2+0 -R" "$ARGLOG"' "stderiv_C5 with both groups' patterns"
check 'grep -q new s.spk.stderiv_C5.2' "stderiv_C5 .spk re-extracted"
check 'grep -q "re-project them.*stderiv_C5_D34" out.log' "stderiv_C5_D34 listed for re-projection"
check 'grep -q "ndm_pca s --method stderiv_C5_D34 --force" out.log' "... with the ndm_pca --force command for the lag space"
check 'xs --allMethods --method standard; [ $? -ne 0 ] && grep -q "drop --method" out.log' "--method is refused with --allMethods"

echo
if [ $fails -eq 0 ]; then echo "test_extract_all_methods: PASS"; else echo "test_extract_all_methods: FAIL ($fails)"; fi
exit $((fails > 0))
