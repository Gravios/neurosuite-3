#!/usr/bin/env bash
# ndm_estimategeometry's command line follows ndm_pca / ndm_detectspikes:
#   - no parameter file: the session is the folder's name;
#   - [parameter-file] [electrodeGroup] positionals, a group alone works too;
#   - --force redoes a group already estimated (otherwise it is skipped);
#   - --wireDiameterUm / --layout / --estimateKnown win over the YAML;
#   - an unknown option, a non-numeric group or a bad layout is refused with
#     the usage and runs nothing.
# Data: 8 channels of noise mixed with distance-dependent weights (a few
# seconds), enough for every step to produce an estimate quickly.
# Run from anywhere:  bash src/ndmanager-plugins/test/test_estimategeometry_cli.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
python3 -c "import numpy, scipy.signal, yaml" 2>/dev/null || { echo "SKIPPED: numpy, scipy and pyyaml are needed"; exit 0; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fails=0
check() { if eval "$1"; then echo "  [PASS] $2"; else echo "  [FAIL] $2"; fails=$((fails + 1)); fi; }
export PATH="$SCRIPTS:$PATH"
S="$TMP/rat"
mkdir -p "$S"
python3 - "$S" <<'EOF'
import sys, numpy as np, yaml
d = sys.argv[1]
rng = np.random.default_rng(0)
sr, T = 20000, 20000 * 8
P = np.array([[0, 0], [15, 0], [30, 0], [7.5, 13], [22.5, 13], [37.5, 13], [15, 26], [30, 26]], float)
src = rng.uniform(-60, 100, (40, 2))
W = 1.0 / np.maximum(np.linalg.norm(src[:, None] - P[None], axis=2), 5.0)        # (40, 8)
X = (rng.standard_normal((T, 40)) * 4000) @ W + 5 * rng.standard_normal((T, 8))
X.astype(np.int16).tofile(f"{d}/rat.dat")
doc = dict(acquisitionSystem=dict(nChannels=8, samplingRate=sr, nBits=16),
           spikeDetection=dict(channelGroups=[dict(channels=list(range(8)))]),
           programs=[dict(name="ndm_estimategeometry", parameters=[
               dict(name="nSegments", value=8), dict(name="segmentSec", value=0.5),
               dict(name="nBootstrap", value=1), dict(name="wireDiameterUm", value=15)])])
yaml.safe_dump(doc, open(f"{d}/rat.yaml", "w"))
EOF
cd "$S"
rep=rat.probe.0.probe          # the group has no probe file: the estimate goes into a new one
val() { python3 -c "import yaml; print(yaml.safe_load(open('$rep'))['probeFile']['estimate'][0]['$1'])"; }

bash "$SCRIPTS/ndm_estimategeometry" >out1 2>&1
check '[ -f $rep ]' "no argument: the session is the folder's name"
check '[ "$(val wireDiameter_um)" = 15.0 ]' "the YAML value is used"
m1=$(cksum < $rep)
bash "$SCRIPTS/ndm_estimategeometry" rat.yaml 1 --wireDiameterUm 20 >out2 2>&1
check '[ "$(cksum < $rep)" = "$m1" ]' "an existing estimate is kept without --force"
bash "$SCRIPTS/ndm_estimategeometry" 1 --force --wireDiameterUm 20 --layout free >out3 2>&1
check '[ "$(val wireDiameter_um)" = 20.0 ] && [ "$(val layout)" = free ]' \
      "a group alone + --force: redone, the options win over the YAML"
bash "$SCRIPTS/ndm_estimategeometry" --force --wireDiameterUm=12 >out4 2>&1
check '[ "$(val wireDiameter_um)" = 12.0 ] && [ "$(val layout)" = bundle ]' "--option=value form"
m2=$(cksum < $rep)
for bad in "--bogus" "rat.yaml abc" "--layout round" "rat.yaml 1 2"; do
    # shellcheck disable=SC2086
    bash "$SCRIPTS/ndm_estimategeometry" $bad >out5 2>&1
    rc=$?
    check '[ $rc -ne 0 ] && [ "$(cksum < $rep)" = "$m2" ] && ! grep -q "Estimating wire-bundle" out5 && grep -q "usage:" out5' \
          "refused before running, with the usage: $bad"
done

[ $fails -eq 0 ] && { echo "OK (0 failures)"; exit 0; }
echo "FAILED ($fails)"; exit 1
