#!/usr/bin/env bash
# The ndm_* YAML readers treat a value that is exactly "~" as unset.
#
# ndmanager builds before the libklustersshared null fix wrote every empty
# field as the quoted string "~" (a string, not a YAML null).  Read as a
# value, it reached the plugins as an argument (process_plxconvert
# --chunk "~").  This checks read_script_parameter and yaml_read on every
# spelling of "empty", that text merely containing ~ is untouched, that
# ${var:-default} falls through to the default, and that process_plxyaml.py
# fills a generalInfo field holding "~".
#
# Run from anywhere:  bash src/ndmanager-plugins/test/test_yaml_tilde.sh
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS="$HERE/../scripts"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

cat > sess.yaml <<'YAML'
generalInfo:
  description: "~"
  notes: ~/lab/notes.txt
acquisitionSystem:
  nChannels: 8
programs:
  - name: ndm_test
    parameters:
      - {name: dquoted, value: "~",    status: Optional}
      - {name: squoted, value: '~',    status: Optional}
      - {name: plain,   value: ~,      status: Optional}
      - {name: empty,   value:,        status: Optional}
      - {name: home,    value: ~/data, status: Optional}
      - {name: real,    value: 262144, status: Optional}
      - {name: word,    value: WB,     status: Optional}
YAML

fail=0; ran=0
check() {   # name expected actual
    ran=$((ran + 1))
    if [ "$2" == "$3" ]; then echo "  [PASS] $1"
    else echo "  [FAIL] $1: expected [$2], got [$3]"; fail=$((fail + 1)); fi
}

(
    set +u      # ndm_functions runs without nounset, as in the ndm_* scripts
    source "$SCRIPTS/ndm_functions" >/dev/null 2>&1
    program=ndm_test
    for n in dquoted squoted plain empty; do
        v=$(read_script_parameter sess "$n")
        echo "$n=[$v]"
        d=${v:-default}
        echo "${n}_default=[$d]"
        check_optional_variable "$v" && echo "${n}_set=[yes]" || echo "${n}_set=[no]"
    done
    echo "home=[$(read_script_parameter sess home)]"
    echo "real=[$(read_script_parameter sess real)]"
    echo "word=[$(read_script_parameter sess word)]"
    echo "desc=[$(yaml_read sess //generalInfo/description)]"
    echo "notes=[$(yaml_read sess //generalInfo/notes)]"
    echo "key=[$(yaml_read_key sess.yaml generalInfo.description)]"
) > out.txt 2>&1

get() { sed -n "s/^$1=\[\(.*\)\]$/\1/p" out.txt; }
for n in dquoted squoted plain empty; do
    check "program value $n reads as unset"             ""        "$(get $n)"
    check "program value $n: \${v:-default} -> default" "default" "$(get ${n}_default)"
    check "program value $n: check_optional_variable"   "no"      "$(get ${n}_set)"
done
check "~/data unchanged"                 "~/data"  "$(get home)"
check "numeric value unchanged"          "262144"  "$(get real)"
check "text value unchanged"             "WB"      "$(get word)"
check "yaml_read: \"~\" field is unset"  ""        "$(get desc)"
check "yaml_read: ~/path unchanged"      "~/lab/notes.txt" "$(get notes)"
check "yaml_read_key: \"~\" is unset"    ""        "$(get key)"

# process_plxyaml.py fills a generalInfo field that holds "~"
cat > sess.plexon.yaml <<'YAML'
sourceRecording:
  header: {comment: "plexon comment", recordedAt: "2026-10-06T13:55:47"}
  export: {acquisitionSystem: {nChannels: 8}}
YAML
python3 "$SCRIPTS/process_plxyaml.py" --param-file sess.yaml --meta sess.plexon.yaml > merge.txt 2>&1
desc=$(python3 -c "import yaml;print(yaml.safe_load(open('sess.yaml'))['generalInfo']['description'])")
check "process_plxyaml fills a \"~\" description" "plexon comment" "$desc"

echo
[ $fail -eq 0 ] && echo "OK: $ran checks" || echo "FAILED: $fail of $ran"
exit $fail
