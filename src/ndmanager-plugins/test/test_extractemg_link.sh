#!/usr/bin/env bash
# ndm_extractemg makes the EMG-cleaned recording reachable as
# SESSION.emgclean (a relative symlink to SESSION-emgclean.dat), so a later
# step can read it with inputExtension: emgclean — after a run, on a re-run
# that finds every output present, and never over a regular file of that
# name.  process_extractemg is replaced by a stand-in that writes the files.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_extractemg_link.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/s"
cat > "$TMP/bin/process_extractemg" <<'SH'
#!/bin/bash
base=${@: -1}
: > "$base-emgclean.dat"; : > "$base.res.emg.1"; : > "$base.spk.emg.1"; : > "$base.emg.1"
SH
printf '#!/bin/bash\nexit 0\n' > "$TMP/bin/xpathReader"
chmod +x "$TMP/bin/"*
cd "$TMP/s"
: > s.dat
cat > s.yaml <<'YAML'
acquisitionSystem: {nChannels: 4, samplingRate: 20000, nBits: 16}
anatomicalDescription:
  channelGroups:
    - channels: [{id: 0, skip: 0}, {id: 1, skip: 0}, {id: 2, skip: 0}, {id: 3, skip: 0}]
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 32
      peakSampleIndex: 16
probes:
  - {probeId: 1, probeFile: x.probe, channelOffset: 0, anatomicalGroups: [1]}
programs:
  - name: ndm_extractemg
    parameters: []
YAML

fail=0
check() {   # name expected actual
    if [ "$2" == "$3" ]; then echo "  [PASS] $1"
    else echo "  [FAIL] $1: expected [$2], got [$3]"; fail=$((fail + 1)); fi
}
run() { PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/ndm_extractemg" s >run.log 2>&1; echo $?; }

check "run succeeds" 0 "$(run)"
check "s.emgclean links to s-emgclean.dat" "s-emgclean.dat" "$(readlink s.emgclean 2>/dev/null)"
rm -f s.emgclean
check "re-run with outputs present" 0 "$(run)"
check "link restored on re-run" "s-emgclean.dat" "$(readlink s.emgclean 2>/dev/null)"
rm -f s.emgclean; echo keep > s.emgclean
run >/dev/null
check "a regular s.emgclean is left alone" "keep" "$(cat s.emgclean)"

[ $fail -eq 0 ] && echo "OK" || { echo "FAILED ($fail)"; sed 's/\x1b\[[0-9;]*m//g' run.log | tail -5; }
exit $fail
