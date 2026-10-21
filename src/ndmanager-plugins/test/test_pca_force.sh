#!/usr/bin/env bash
# ndm_pca --force: a group whose .fet exists is skipped (exit 10) without it,
# and refit and overwritten with it -- the .fet and its .pca, and with a
# _D<lag><dims> token both the ordinary and the lag pair.  --force is not
# passed on to process_pca.  Runs the real ndm_pca with stand-in tools:
# process_pca writes the .tmp feature file(s) and the .pca named as the real
# one names it (.tmp stripped, .fet. -> .pca.); process_mergefeatures copies.
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_pca_force.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/bin" "$TMP/s"
printf '#!/bin/bash\necho stub\n' > "$TMP/bin/xpathReader"
cat > "$TMP/bin/process_pca" <<'TOOL'
#!/bin/bash
echo "process_pca $*" >> "$ARGLOG"
f=; lag=
while [ $# -gt 0 ]; do
    case "$1" in -f) f=$2; shift 2 ;; --lag-fet) lag=$2; shift 2 ;; *) shift ;; esac
done
for out in "$f" "$lag"; do
    [ -n "$out" ] || continue
    echo "new" > "$out"
    pca=${out%.tmp}; pca=${pca/.fet./.pca.}
    echo "new" > "$pca"
done
TOOL
printf '#!/bin/bash\ncat "$@" > /dev/null; echo new\n' > "$TMP/bin/process_pca_stderiv"
printf '#!/bin/bash\ncp "$1" "$3"\n' > "$TMP/bin/process_mergefeatures"
chmod +x "$TMP/bin/"*
cd "$TMP/s"
export ARGLOG="$TMP/args.log"
fails=0
check() { if eval "$1"; then echo "  ok:   $2"; else echo "  FAIL: $2"; fails=$((fails + 1)); fi; }

cat > s.yaml <<'YAML'
acquisitionSystem: {nBits: 16, nChannels: 8, samplingRate: 20000}
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3]
      nSamples: 32
      peakSampleIndex: 11
      nFeatures: 3
    - channels: [4, 5, 6, 7]
      nSamples: 32
      peakSampleIndex: 11
      nFeatures: 3
programs:
  - name: ndm_pca
    parameters:
      - {name: method, value: standard, status: Optional}
YAML
reset() {   # method...: an "old" .fet/.pca per group and method
    rm -f s.fet.* s.pca.* "$ARGLOG"
    : > s.fil
    for g in 1 2; do
        head -c 80 /dev/zero > s.res.standard.$g
        : > s.spk.standard.$g; : > s.spk.stderiv_S3.$g
        for m in "$@"; do echo old > s.fet.$m.$g; echo old > s.pca.$m.$g; done
    done
}
pca() { PATH="$TMP/bin:$SCRIPTS:$PATH" bash "$SCRIPTS/ndm_pca" s "$@" > out.log 2>&1; }

echo "without --force:"
reset standard
pca 1; st=$?
check '[ $st -eq 10 ] && [ ! -e "$ARGLOG" ]' "group 1 skipped (exit 10), process_pca not run"
check 'grep -q old s.fet.standard.1 && grep -q old s.pca.standard.1' "its .fet and .pca kept"

echo "--force, one group:"
pca 1 --force; st=$?
check '[ $st -eq 0 ] && grep -q new s.fet.standard.1 && grep -q new s.pca.standard.1' ".fet and .pca overwritten"
check 'grep -q old s.fet.standard.2' "the other group untouched"
check '! grep -q -- "--force" "$ARGLOG"' "--force not passed to process_pca"
check '! ls s.fet.*.tmp >/dev/null 2>&1' "no .tmp left"
check 'grep -q "refitting and overwriting s.fet.standard.1" out.log' "says what it overwrites"

echo "--force, all groups:"
reset standard
pca --force; st=$?
check '[ $st -eq 0 ] && grep -q new s.fet.standard.1 && grep -q new s.fet.standard.2' "both groups overwritten"

echo "--force with process_pca flags after it:"
reset standard
pca 1 --force -- --varimax-max-iter 50; st=$?
check '[ $st -eq 0 ] && grep -q -- "--varimax-max-iter 50" "$ARGLOG" && ! grep -q -- "--force" "$ARGLOG"' "flags after -- still reach process_pca, --force does not"

echo "_D lag token:"
reset stderiv_S3 stderiv_S3_D13
pca 1 --method stderiv_S3_D13; st=$?
check '[ $st -eq 10 ] && [ ! -e "$ARGLOG" ]' "both pairs exist: skipped without --force"
pca 1 --method stderiv_S3_D13 --force; st=$?
check '[ $st -eq 0 ]' "runs with --force"
check 'grep -q new s.fet.stderiv_S3.1 && grep -q new s.pca.stderiv_S3.1' "ordinary pair overwritten"
check 'grep -q new s.fet.stderiv_S3_D13.1 && grep -q new s.pca.stderiv_S3_D13.1' "lag pair overwritten"

echo
if [ $fails -eq 0 ]; then echo "test_pca_force: PASS"; else echo "test_pca_force: FAIL ($fails)"; fi
exit $((fails > 0))
