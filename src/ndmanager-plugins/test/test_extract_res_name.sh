#!/usr/bin/env bash
# ndm_extractspikes takes a group's spike times from the .res ndm_detectspikes
# writes, named from the session file:
#   1. the ndm_detectspikes node sets method M: <session>.res.M.<group> is used,
#      even when a .res.emg.<group> (ndm_extractemg's EMG events, which sorts
#      first) or a .res of the extraction's own method is present;
#   2. M set and its .res missing: the run stops before any engine call, naming
#      the expected file and the .res files that are present;
#   3. no detection method set: the first waveform-method .res is used, and a
#      .res.emg.<group> is never taken for spike times (alone it is an error);
#   4. the run names each group's .res with its spike count;
#   5. --check reports the same missing .res.
# Runs the real ndm_extractspikes with stand-in engines that record the size of
# the .res they were handed.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_extract_res_name.sh
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
# stand-in: record how many spike times each temp .res holds, write the .spk/.nst
stem="${@: -1}"; groups=0
while [ $# -gt 0 ]; do
    case "$1" in -c) groups=$(echo "$2" | tr ':' '\n' | wc -l); shift 2 ;; *) shift ;; esac
done
for i in $(seq 1 $groups); do
    echo "res.$i $(( $(wc -c < "$stem.res.$i") / 8 ))" >> "$ARGLOG"
    : > "$stem.spk.$i"; : > "$stem.nst.$i"
done
exit 0
ENGINE
done
chmod +x "$TMP/bin/"*
cd "$TMP/s"
export ARGLOG="$TMP/args.log"
fails=0
check() { if eval "$1"; then echo "  ok:   $2"; else echo "  FAIL: $2"; fails=$((fails + 1)); fi; }

session_yaml() {   # detection-method (empty = the node sets none)
    local det=
    [ -n "$1" ] && det="  - name: ndm_detectspikes
    parameters:
      - {name: method, value: $1, status: Optional}"
    cat > s.yaml <<YAML
acquisitionSystem: {nBits: 16, nChannels: 8, samplingRate: 20000}
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3, 4, 5, 6, 7]
      nSamples: 32
      peakSampleIndex: 11
programs:
$det
  - name: ndm_extractspikes
    parameters:
      - {name: method, value: stderiv_S2, status: Optional}
      - {name: dedupSamples, value: -1, status: Optional}
YAML
}
res() { head -c $(( $2 * 8 )) /dev/zero > "s.res.$1.1"; }   # token, spikes

run() {   # [flags] -> exit status; engine log in $ARGLOG, output in out.log
    rm -f s.spk.* s.nst.* "$ARGLOG"
    : > s.fil
    PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/ndm_extractspikes" s "$@" > out.log 2>&1
}
clean() { rm -f s.res.*; }

echo "detection method set (stderiv_S3):"
session_yaml stderiv_S3
clean; res emg 68; res stderiv_S3 1000; res stderiv_S2 5
run; st=$?
check '[ $st -eq 0 ]' "runs"
check 'grep -qx "res.1 1000" "$ARGLOG"' "engine gets .res.stderiv_S3.1, not .res.emg.1 or the extraction method's .res ($(cat "$ARGLOG" 2>/dev/null))"
check 'grep -q "group 1: s.res.stderiv_S3.1 (1000 spikes)" out.log' "the .res and its spike count are printed"
check '[ -e s.spk.stderiv_S2.1 ]' ".spk written under the extraction method"

echo "detection method set, its .res missing:"
clean; res emg 68; res stderiv_S2 5
run; st=$?
check '[ $st -ne 0 ]' "refused (exit $st)"
check '[ ! -e "$ARGLOG" ]' "no engine call"
check '[ ! -e s.spk.stderiv_S2.1 ]' "no .spk written"
check 'grep -q "s.res.stderiv_S3.1" out.log && grep -q "stderiv_S3" out.log' "names the expected s.res.stderiv_S3.1"
check 'grep -q "s.res.emg.1" out.log' "lists the .res files present"
run --check; st=$?
check '[ $st -ne 0 ] && grep -q "s.res.stderiv_S3.1" out.log' "--check reports it too"

echo "no detection method set:"
session_yaml ""
clean; res emg 68; res stderiv_S3 1000
run; st=$?
check '[ $st -eq 0 ] && grep -qx "res.1 1000" "$ARGLOG"' "the waveform-method .res is used, .res.emg.1 skipped ($(cat "$ARGLOG" 2>/dev/null))"
clean; res emg 68
run; st=$?
check '[ $st -ne 0 ] && [ ! -e "$ARGLOG" ]' ".res.emg.1 alone is not spike times: refused, no engine call"
check 'grep -q "not spike times" out.log && grep -q "s.res.emg.1" out.log' "and says why"
clean; head -c 80 /dev/zero > s.res.1
run; st=$?
check '[ $st -eq 0 ] && grep -qx "res.1 10" "$ARGLOG"' "an untagged .res still works"

echo
[ $fails -eq 0 ] && echo "test_extract_res_name: PASS" || echo "test_extract_res_name: $fails FAIL"
exit $fails
