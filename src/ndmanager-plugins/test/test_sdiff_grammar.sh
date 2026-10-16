#!/usr/bin/env bash
# test_sdiff_grammar.sh -- ndm_custody's ndm_sdiff_pattern_order against the order
# column of the shared sdiff_grammar_vectors.tsv (libneurosuite-core/test), the table
# sdiff_pairs.h (sdiffSpecOrder) and fiber-kit's fiber_stderiv run too.  The scripts
# pick an extraction method token (_C4/_C5/_C6) from it, so a disagreement names
# the wrong feature space.  Exits non-zero on failure.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TABLE="$HERE/../../libneurosuite-core/test/sdiff_grammar_vectors.tsv"
# shellcheck source=/dev/null
. "$HERE/../scripts/ndm_custody"

fails=0 rows=0
while IFS=$'\t' read -r spec order _pairs _sets; do
    case "$spec" in '#'*|'') continue ;; esac
    rows=$((rows + 1))
    got=$(ndm_sdiff_pattern_order "$spec")
    if [ "$got" != "$order" ]; then
        echo "  FAIL: order of '$spec': got $got, want $order"
        fails=$((fails + 1))
    fi
done < "$TABLE"

if [ "$rows" -lt 30 ]; then echo "  FAIL: the table has its rows ($rows)"; fails=$((fails + 1)); fi
echo "test_sdiff_grammar: $rows patterns, $fails failed"
[ "$fails" -eq 0 ]
