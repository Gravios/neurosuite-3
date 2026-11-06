#!/usr/bin/env bash
# The ndm front end:
#   - `ndm list steps` names every ndm_* step scripts/CMakeLists.txt installs
#     (except the helpers and the deprecated aliases), and only those;
#   - `ndm <step> ...` runs ndm_<step> with the arguments; `ndm help <step>`
#     prints the step's own help; a mistyped step is refused with suggestions;
#   - `ndm configure [session]` starts ndmanager with the session;
#   - `ndm list probes|spikegroups|programs` read the session named after the
#     folder (or given), find probe files as ndm_setupgroups does, and show
#     estimated / template / missing files and where site positions come from;
#   - `ndm list groupids` and the bash completion: steps, a step's options
#     and their values from its usage line, session files, and the session's
#     spike-group numbers for the steps that take a group.
# Run from anywhere:  bash src/ndmanager-plugins/test/test_ndm_cli.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$(cd "$HERE/../scripts" && pwd)"
python3 -c "import yaml" 2>/dev/null || { echo "SKIPPED: python3 with pyyaml is needed"; exit 0; }
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fails=0
check() { if eval "$1"; then echo "  [PASS] $2"; else echo "  [FAIL] $2"; fails=$((fails + 1)); fi; }
export PATH="$SCRIPTS:$PATH"
ndm() { bash "$SCRIPTS/ndm" "$@"; }

# ── the step table against the install list ──
installed=$(awk '/^set\(_ndm_scripts/,/^\)/' "$SCRIPTS/CMakeLists.txt" | sed 's/#.*//' | tr ' ' '\n' \
            | grep '^ndm_' | grep -vxE 'ndm_functions|ndm_custody|ndm_extractspikes_sdiff|ndm_extractspikes_stderiv|ndm_pca_stderiv' \
            | sed 's/^ndm_//' | sort -u)
listed=$(ndm list steps | awk '/^  / { print $1 }' | sort)
check '[ -n "$installed" ] && [ "$installed" = "$listed" ]' "ndm list steps = the installed steps"
missing=""
for s in $listed; do [ -f "$SCRIPTS/ndm_$s" ] || missing="$missing $s"; done
check '[ -z "$missing" ]' "every listed step has a script${missing:+ (missing:$missing)}"

# ── help and dispatch ──
ndm help >"$TMP/h" 2>&1; rc=$?
check '[ $rc -eq 0 ] && grep -q "ndm configure" "$TMP/h" && grep -q "estimategeometry" "$TMP/h"' "ndm help: usage and the steps"
ndm >"$TMP/h0" 2>&1
check 'cmp -s "$TMP/h" "$TMP/h0"' "ndm alone = ndm help"
ndm help hipass >"$TMP/a" 2>&1; bash "$SCRIPTS/ndm_hipass" --help >"$TMP/b" 2>&1
check 'cmp -s "$TMP/a" "$TMP/b"' "ndm help hipass = ndm_hipass --help"
ndm ndm_hipass -h >"$TMP/c" 2>&1
check 'cmp -s "$TMP/a" "$TMP/c"' "ndm ndm_hipass -h works too"
( cd "$TMP" && ndm estimategeometry --bogus >"$TMP/d" 2>&1 ); rc=$?
check '[ $rc -ne 0 ] && grep -q "unknown option: --bogus" "$TMP/d" && grep -q "usage: ndm_estimategeometry" "$TMP/d"' \
      "ndm <step> passes the arguments on (the step itself refuses --bogus)"
ndm detctspikes >"$TMP/e" 2>&1; rc=$?
check '[ $rc -eq 2 ] && grep -q "did you mean: detectspikes" "$TMP/e"' "a mistyped step: refused, with suggestions"
ndm functions >/dev/null 2>&1; rc=$?
check '[ $rc -eq 2 ]' "a helper is not a step"
ndm list foo >/dev/null 2>&1; rc=$?
check '[ $rc -eq 2 ]' "ndm list foo: refused"

# ── configure ──
mkdir -p "$TMP/bin"
printf '#!/usr/bin/env bash\necho "ndmanager[$#] $*"\n' >"$TMP/bin/ndmanager"; chmod +x "$TMP/bin/ndmanager"
out=$(PATH="$TMP/bin:$PATH" ndm configure rat.yaml)
check '[ "$out" = "ndmanager[1] rat.yaml" ]' "ndm configure rat.yaml starts ndmanager with it"
out=$(PATH="$TMP/bin:$PATH" ndm configure)
check '[ "$out" = "ndmanager[0] " ]' "ndm configure alone: ndmanager opens the folder's session"

# ── list probes / spikegroups / programs on a session folder ──
S="$TMP/rat01"
mkdir -p "$S"
cat >"$S/rat01.yaml" <<'EOF'
acquisitionSystem: {nChannels: 24, samplingRate: 20000}
probes:
  - {probeId: 0, probeFile: rat01.probe.0.probe, channelOffset: 0}
  - {probeId: 1, probeFile: nowhere.probe, channelOffset: 8}
  - {probeId: 2, probeFile: tet.probe, channelOffset: 12}
spikeDetection:
  channelGroups:
    - {channels: [0, 1, 2, 3, 4, 5, 6, 7], probeId: 0, shankIndex: 0, nSamples: 32, peakSampleIndex: 16}
    - {channels: [8, 9, 10, 11], probeId: 1, shankIndex: 0}
    - {channels: [12, 13, 14, 15], probeId: 2, shankIndex: 0}
    - {channels: [16, 17, 18, 20], sitePositions_um: [[0, 0], [0, 20], [0, 40], [0, 60]]}
    - {channels: [{id: 21, skip: 0}, {id: 22, skip: 1}, {id: 23, skip: 0}]}
programs:
  - name: ndm_hipass
    parameters:
      - {name: highPassHz, value: 800}
      - {name: outputExtension, value: ~}
EOF
cat >"$S/rat01.probe.0.probe" <<'EOF'
probeFile:
  model: octrode
  substrate: {material: wire-bundle}
  shanks: {count: 1}
  sites:
    count_per_shank: 8
    layout: octrode
    geometry: [[0, 0], [15, 0], [30, 0], [7, 13], [22, 13], [37, 13], [15, 26], [30, 26]]
    geometry_backup: [[0, 0], [0, 20], [0, 40], [20, 0], [20, 20], [20, 40], [40, 0], [40, 20]]
  estimate:
    0: {date: '2026-11-04', channels: [0, 1, 2, 3, 4, 5, 6, 7]}
EOF
cat >"$S/tet.probe" <<'EOF'
probeFile:
  vendor: general
  model: tetrode
  substrate: {material: wire-bundle}
  shanks: {count: 1}
  sites: {count_per_shank: 4, layout: tetrode, geometry: [[-18, 0], [-6, 0], [6, 0], [18, 0]]}
EOF
( cd "$S" && ndm list probes >"$TMP/p" 2>&1 ); rc=$?
check '[ $rc -eq 0 ] && grep -q "rat01.yaml: 3 probe(s)" "$TMP/p"' "list probes: the folder's session"
check 'grep -E "^  0 .*estimated 2026-11-04; geometry_backup" "$TMP/p" >/dev/null && grep -E "^  0 .* 1/0 " "$TMP/p" >/dev/null' \
      "... an estimated file, its backup and the group on it"
check 'grep -E "^  1 .*not found" "$TMP/p" >/dev/null' "... a missing file"
check 'grep -E "^  2 .*template" "$TMP/p" >/dev/null' "... a wire-bundle template"
( cd "$TMP" && ndm list spikegroups rat01/rat01.yaml >"$TMP/g" 2>&1 ); rc=$?
check '[ $rc -eq 0 ] && grep -q "5 spike group(s)" "$TMP/g"' "list spikegroups: a session given by path"
check 'grep -E "^  1 +0-7 +8 +32 +16 .*probe file \(estimated 2026-11-04\)" "$TMP/g" >/dev/null' "... channels, window, estimated geometry"
check 'grep -E "^  2 .*nowhere.probe not found" "$TMP/g" >/dev/null' "... a probe file not found"
check 'grep -E "^  3 .*probe file \(template\)" "$TMP/g" >/dev/null' "... a template"
check 'grep -E "^  4 +16-18,20 .*sitePositions_um" "$TMP/g" >/dev/null' "... channel ranges and inline positions"
check 'grep -E "^  5 +21-23 .* none +22$" "$TMP/g" >/dev/null' "... no geometry, a skipped channel"
( cd "$S" && ndm list programs >"$TMP/r" 2>&1 )
check 'grep -q "ndm_hipass  (1 of 2 parameters set)" "$TMP/r" && grep -q "highPassHz = 800" "$TMP/r"' "list programs: the values set"
( cd "$S" && ndm list programs | head -1 >/dev/null ) 2>"$TMP/pipe"
check '! grep -q Traceback "$TMP/pipe"' "a closed pipe (| head) is quiet"
( cd "$TMP" && ndm list probes >"$TMP/n" 2>&1 ); rc=$?
check '[ $rc -ne 0 ] && grep -q "no session given and no" "$TMP/n"' "no session in the folder: a clear error"

# ── group numbers and tab completion ──
( cd "$S" && ndm list groupids >"$TMP/ids" 2>&1 )
check '[ "$(cut -f1 "$TMP/ids" | tr "\n" " ")" = "1 2 3 4 5 " ] && grep -qxF "$(printf "4\t16-18,20")" "$TMP/ids"' \
      "list groupids: one number and channel range per group"
# The completion finds ndm and the steps on PATH as executables, as installed.
mkdir -p "$TMP/inst"
cp "$SCRIPTS"/ndm "$SCRIPTS"/ndm_* "$SCRIPTS"/*.py "$TMP/inst/" 2>/dev/null
chmod +x "$TMP/inst"/*
: >"$S/rat01.fil"; : >"$S/rat01.lfp"
comp() {   # comp WORD... : the completions of the last word, space-separated
    ( cd "$S" && PATH="$TMP/inst:$PATH" bash -c '
        shopt -s extglob
        source "$1"; shift
        COMP_WORDS=("$@"); COMP_CWORD=$(($# - 1)); COMPREPLY=()
        _ndm
        [ ${#COMPREPLY[@]} -gt 0 ] && printf "%s\n" "${COMPREPLY[@]}" | sort | tr "\n" " "' _ "$SCRIPTS/ndm.bash-completion" "$@" )
}
check '[ "$(comp ndm klusta)" = "klustakwik " ]' "completion: a step"
check '[ "$(comp ndm klustakwik "")" = "1 2 3 4 5 rat01.yaml " ]' "completion: a group step offers the session and its groups"
check '[ "$(comp ndm pca rat01 "")" = "1 2 3 4 5 " ]' "completion: groups after the session"
check '[ -z "$(comp ndm pca rat01 3 "")" ]' "completion: one group only"
check '[ "$(comp ndm applydrift rat01 1 2 "")" = "1 2 3 4 5 " ]' "completion: target groups repeat (applydrift)"
check '[ "$(comp ndm hipass "")" = "rat01.yaml " ]' "completion: a step without groups offers the session only"
check '[ "$(comp ndm estimategeometry -)" = "--estimateKnown --force --help --inputExtension --layout --wireDiameterUm " ]' \
      "completion: a step's options, from its usage line"
check '[ "$(comp ndm detectspikes --m)" = "--method --methodOrder " ]' "completion: options parsed in ndm_functions (detect/extract)"
check '[ "$(comp ndm estimategeometry --layout "")" = "bundle free " ]' "completion: an option's choices"
check '[ "$(comp ndm klustakwik --method "")" = "sdiff standard stderiv " ]' "completion: --method"
check '[ "$(comp ndm pca 2 --methodOrder "")" = "0 1 2 3 " ]' "completion: --methodOrder"
x=$(comp ndm estimategeometry --inputExtension "")
check 'printf "%s\n" $x | grep -qx fil && printf "%s\n" $x | grep -qx lfp && ! printf "%s\n" $x | grep -qx yaml' \
      "completion: --inputExtension from the session's files (not the parameter file)"
check '[ "$(comp ndm help locali)" = "localise " ]' "completion: help <step>"

[ $fails -eq 0 ] && { echo "OK (0 failures)"; exit 0; }
echo "FAILED ($fails)"; exit 1
