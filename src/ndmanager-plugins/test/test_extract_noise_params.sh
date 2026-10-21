#!/usr/bin/env bash
# ndm_extractspikes noise statistics: noiseWindows / noiseRejectZ / noiseGuard
# reach the engine as -N / -E / -G (default -N 20000 -E 5), noiseWindows 0
# passes none and writes no .nst, a bad value stops the step, and the engine's
# untagged <tmp>.nst.<i> is renamed to <session>.nst.<method>.<group> beside the
# .spk.  Runs the real ndm_extractspikes with stand-in engines that record their
# arguments and write the outputs a -R run writes.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_extract_noise_params.sh
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
# stand-in: record the arguments, write <stem>.spk.<i> (and .nst.<i> under -N)
echo "$(basename "$0") $*" >> "$ARGLOG"
stem="${@: -1}"; groups=0; noise=0
while [ $# -gt 0 ]; do
    case "$1" in
        -c) groups=$(echo "$2" | tr ':' '\n' | wc -l); shift 2 ;;
        -N) noise=$2; shift 2 ;;
        *) shift ;;
    esac
done
for i in $(seq 1 $groups); do
    : > "$stem.spk.$i"
    [ "$noise" -gt 0 ] && : > "$stem.nst.$i"
done
exit 0
ENGINE
done
chmod +x "$TMP/bin/"*
cd "$TMP/s"
export ARGLOG="$TMP/args.log"
fails=0
check() { if eval "$1"; then echo "  ok:   $2"; else echo "  FAIL: $2"; fails=$((fails + 1)); fi; }

session_yaml() {   # method  extra-parameter-lines
    cat > s.yaml <<YAML
acquisitionSystem: {nBits: 16, nChannels: 8, samplingRate: 20000}
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 32
      peakSampleIndex: 11
    - channels: [4, 5, 6, 7]
      nSamples: 32
      peakSampleIndex: 11
programs:
  - name: ndm_extractspikes
    parameters:
      - {name: method, value: $1, status: Optional}
      - {name: dedupSamples, value: -1, status: Optional}
$2
YAML
}

run() {   # -> exit status; engine args in $ARGLOG
    rm -f s.spk.* s.nst.* "$ARGLOG"
    : > s.fil
    head -c 80 /dev/zero > s.res.standard.1
    head -c 80 /dev/zero > s.res.standard.2
    PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/ndm_extractspikes" s > out.log 2>&1
}

echo "defaults:"
session_yaml standard ""
run; st=$?
check '[ $st -eq 0 ]' "runs"
check 'grep -q -- "-N 20000 -E 5 -R" "$ARGLOG"' "engine gets -N 20000 -E 5 ($(cat "$ARGLOG" 2>/dev/null | grep -o -- '-N.*-R'))"
check '[ -e s.nst.standard.1 ] && [ -e s.nst.standard.2 ]' ".nst.standard.<g> written for both groups"
check '[ ! -e s-extract-tmp.nst.1 ]' "no untagged temp .nst left"

echo "noiseRejectZ / noiseGuard:"
session_yaml standard "      - {name: noiseRejectZ, value: 3.5, status: Optional}
      - {name: noiseGuard, value: 10, status: Optional}"
run; st=$?
check '[ $st -eq 0 ] && grep -q -- "-N 20000 -E 3.5 -G 10 -R" "$ARGLOG"' "engine gets -E 3.5 -G 10"

echo "noiseWindows 0:"
session_yaml standard "      - {name: noiseWindows, value: 0, status: Optional}"
run; st=$?
check '[ $st -eq 0 ] && ! grep -q -- "-N" "$ARGLOG"' "no -N passed"
check '! ls s.nst.* >/dev/null 2>&1' "no .nst written"

echo "stderiv_S3:"
session_yaml stderiv_S3 ""
run; st=$?
check '[ $st -eq 0 ] && grep -q "^process_extractspikes_stderiv .*-N 20000 -E 5 -R" "$ARGLOG"' "stderiv engine gets -N"
check '[ -e s.nst.stderiv_S3.1 ] && [ -e s.nst.stderiv_S3.2 ]' ".nst.stderiv_S3.<g> beside .spk.stderiv_S3.<g>"

echo "refusals:"
for bad in "noiseWindows, value: abc" "noiseWindows, value: -5" "noiseRejectZ, value: x" "noiseGuard, value: 1.5"; do
    session_yaml standard "      - {name: $bad, status: Optional}"
    run; st=$?
    check '[ $st -ne 0 ] && [ ! -s "$ARGLOG" ]' "{$bad} stops the step before the engine runs"
done

echo
if [ $fails -eq 0 ]; then echo "test_extract_noise_params: PASS"; else echo "test_extract_noise_params: FAIL ($fails)"; fi
exit $((fails > 0))
