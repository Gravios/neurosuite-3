#!/usr/bin/env bash
# ndm_detectspikes / ndm_extractspikes with spike groups of different waveform
# geometry (nSamples, peakSampleIndex).  The engines take one -w/-p for every
# group they are given, so the driver must run them once per geometry: each
# group cut with its own window, its own channels and its own sdiffPairs, and
# each output renamed to the right group.  Runs the real scripts with stand-in
# engines that record their arguments and write outputs naming their stem and
# index.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_extract_geometry.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/s"
printf '#!/bin/bash\necho stub\n' > "$TMP/bin/xpathReader"
printf '#!/bin/bash\ncat > /dev/null\necho 1,1,1,1:1,1\n' > "$TMP/bin/process_medianthreshold"
for b in process_extractspikes process_extractspikes_sdiff process_extractspikes_stderiv; do
    cat > "$TMP/bin/$b" <<'ENGINE'
#!/bin/bash
# stand-in: record the arguments; write <stem>.spk.<i> / .res.<i> holding
# "<stem> <i> w<nSamples> p<peak> c<channels>"
echo "$(basename "$0") $*" >> "$ARGLOG"
stem="${@: -1}"; chans=; w=; p=; reextract=0
while [ $# -gt 0 ]; do
    case "$1" in
        -c) chans=$2; shift 2 ;;
        -w) w=$2; shift 2 ;;
        -p) p=$2; shift 2 ;;
        -R) reextract=1; shift ;;
        *) shift ;;
    esac
done
i=0
for c in $(echo "$chans" | tr ':' ' '); do
    i=$((i + 1))
    echo "$(basename "$stem") $i w$w p$p c$c" > "$stem.spk.$i"
    [ "$reextract" = 1 ] || echo "$(basename "$stem") $i w$w p$p c$c" > "$stem.res.$i"
done
exit 0
ENGINE
done
chmod +x "$TMP/bin/"*
cd "$TMP/s"
export ARGLOG="$TMP/args.log"
fails=0
check() { if eval "$1"; then echo "  ok:   $2"; else echo "  FAIL: $2"; fails=$((fails + 1)); fi; }

# Groups 1 and 3 share a geometry; group 2 differs in both nSamples and peak.
session_yaml() {   # program method
    local extraParams=
    # (dedupSamples / noiseWindows belong to extraction; detection refuses
    # dedupSamples outside stderiv)
    [ "$1" = ndm_extractspikes ] && extraParams='      - {name: dedupSamples, value: -1, status: Optional}
      - {name: noiseWindows, value: 0, status: Optional}'
    cat > s.yaml <<YAML
acquisitionSystem: {nBits: 16, nChannels: 12, samplingRate: 20000}
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 32
      peakSampleIndex: 11
      sdiffPairs: "0-1+2,1-0+2,2-1+3,3-2+1"
    - channels: [4, 5, 6, 7]
      nSamples: 42
      peakSampleIndex: 16
      sdiffPairs: "0-1+3,1-0+2,2-3+1,3-2+0"
    - channels: [8, 9, 10, 11]
      nSamples: 32
      peakSampleIndex: 11
      sdiffPairs: "0-2+3,1-3+2,2-0+1,3-1+0"
programs:
  - name: $1
    parameters:
      - {name: method, value: $2, status: Optional}
$extraParams
      - {name: thresholdFactor, value: 4, status: Optional}
      - {name: refractoryPeriod, value: 16, status: Optional}
      - {name: peakSearchLength, value: 16, status: Optional}
      - {name: start, value: 0, status: Optional}
      - {name: duration, value: 1, status: Optional}
YAML
}

run() {   # program -> exit status
    rm -f s.spk.* "$ARGLOG"
    head -c 96000 /dev/zero > s.fil
    PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/$1" s > out.log 2>&1
}

echo "extract, standard:"
session_yaml ndm_extractspikes standard
for g in 1 2 3; do head -c 80 /dev/zero > s.res.standard.$g; done
run ndm_extractspikes; st=$?
check '[ $st -eq 0 ]' "runs"
check '[ $(wc -l < "$ARGLOG") -eq 2 ]' "two engine runs, one per geometry"
check 'grep -q -- "-c 0,1,2,3:8,9,10,11 -w 32 -p 11 .*-R" "$ARGLOG"' "groups 1 and 3 together at 32/11"
check 'grep -q -- "-c 4,5,6,7 -w 42 -p 16 .*-R" "$ARGLOG"' "group 2 alone at 42/16"
check 'grep -q "w32 p11 c0,1,2,3" s.spk.standard.1' ".spk.standard.1 is group 1's, cut at 32/11"
check 'grep -q "w42 p16 c4,5,6,7" s.spk.standard.2' ".spk.standard.2 is group 2's, cut at 42/16"
check 'grep -q "w32 p11 c8,9,10,11" s.spk.standard.3' ".spk.standard.3 is group 3's, cut at 32/11"
check '! ls s-extract-tmp* >/dev/null 2>&1' "no temporary files left"
check 'grep -q "runs once per geometry" out.log' "the split is reported"

echo "extract, stderiv_C5: each batch gets its own groups' patterns:"
session_yaml ndm_extractspikes stderiv_C5
for g in 1 2 3; do head -c 80 /dev/zero > s.res.stderiv_C5.$g; done
run ndm_extractspikes; st=$?
check '[ $st -eq 0 ]' "runs"
check 'grep -q -- "-c 0,1,2,3:8,9,10,11 -w 32 -p 11 -P 0-1+2,1-0+2,2-1+3,3-2+1:0-2+3,1-3+2,2-0+1,3-1+0 -R" "$ARGLOG"' "groups 1 and 3 with their two patterns"
check 'grep -q -- "-c 4,5,6,7 -w 42 -p 16 -P 0-1+3,1-0+2,2-3+1,3-2+0 -R" "$ARGLOG"' "group 2 with its pattern"
check 'grep -q "w42 p16" s.spk.stderiv_C5.2 && grep -q "w32 p11 c8" s.spk.stderiv_C5.3' "outputs renamed to the right groups"

echo "detect, standard:"
session_yaml ndm_detectspikes standard
rm -f s.res.*
run ndm_detectspikes; st=$?
check '[ $st -eq 0 ]' "runs"
check '[ $(wc -l < "$ARGLOG") -eq 2 ]' "two engine runs"
check 'grep -q "w42 p16 c4,5,6,7" s.res.standard.2 && grep -q "w32 p11 c8,9,10,11" s.res.standard.3' ".res of each group from its own run"
check '! ls s.spk.* >/dev/null 2>&1' "detection leaves no .spk"

echo "one geometry: one run, as before:"
session_yaml ndm_extractspikes standard
sed -i 's/nSamples: 42/nSamples: 32/; s/peakSampleIndex: 16/peakSampleIndex: 11/' s.yaml
for g in 1 2 3; do head -c 80 /dev/zero > s.res.standard.$g; done
run ndm_extractspikes; st=$?
check '[ $st -eq 0 ] && [ $(wc -l < "$ARGLOG") -eq 1 ]' "a single engine run"
check 'grep -q -- "-c 0,1,2,3:4,5,6,7:8,9,10,11 -w 32 -p 11" "$ARGLOG"' "all three groups in it"
check '! grep -q "once per geometry" out.log' "no split reported"

echo
if [ $fails -eq 0 ]; then echo "test_extract_geometry: PASS"; else echo "test_extract_geometry: FAIL ($fails)"; fi
exit $((fails > 0))
