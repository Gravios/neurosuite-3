#!/usr/bin/env bash
# metal_validate.sh -- run the Metal-vs-CPU comparisons of
# doc/design/metal-backend.md §6 on a real recording, on an Apple-silicon Mac.
#
#   scripts/metal_validate.sh SESSION NCHANNELS SAMPLERATE [options]
#
#     SESSION      session base name: SESSION.dat (or SESSION.fil) is read
#     --seconds S  length of the excerpt the filters run on (default 120)
#     --window W   process_medianfilter half window (default 10)
#     --kk GROUP   also run KiloKlustaKwik and KiloKlustaKwik_cpu on GROUP of the
#                  session (in scratch directories holding links to SESSION.*)
#                  and compare the two .clu
#     --kk-args "..."  extra KiloKlustaKwik arguments (with --kk)
#     --keep       keep the scratch directory
#
# Each program runs twice on the same input: as built (Metal on a Mac with a
# Metal device) and with NS_DISABLE_METAL=1 (its CPU path).  Criteria (§6):
#   process_medianfilter     identical output
#   process_butterworth      -p single: max |diff| <= 1 LSB on < 0.5 % of samples
#   process_medianthreshold  identical thresholds
#   KiloKlustaKwik           adjusted Rand index vs KiloKlustaKwik_cpu, reported
#                            (the bar is "no further from the CPU than CUDA is":
#                            run the same --kk on the CUDA workstation to compare)
# Timings are printed for each run.  Exits non-zero if a criterion fails.
# The programs are taken from PATH (put the build's or the install's bin first).
set -u
usage() { sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
[ $# -ge 3 ] || usage
session=$1 nch=$2 sr=$3; shift 3
seconds=120 window=10 kkGroup= kkArgs= keep=
while [ $# -gt 0 ]; do
    case "$1" in
        --seconds) seconds=$2; shift 2 ;;
        --window)  window=$2; shift 2 ;;
        --kk)      kkGroup=$2; shift 2 ;;
        --kk-args) kkArgs=$2; shift 2 ;;
        --keep)    keep=1; shift ;;
        *) usage ;;
    esac
done
src=
for ext in fil dat; do [ -f "$session.$ext" ] && { src="$session.$ext"; break; }; done
[ -n "$src" ] || { echo "no $session.fil or $session.dat"; exit 2; }
session=$(cd "$(dirname "$session")" && pwd)/$(basename "$session")
src=$(cd "$(dirname "$src")" && pwd)/$(basename "$src")
command -v python3 >/dev/null || { echo "python3 (with numpy) is needed"; exit 2; }

tmp=$(mktemp -d "${TMPDIR:-/tmp}/metal_validate.XXXXXX")
[ -n "$keep" ] && echo "scratch: $tmp" || trap 'rm -rf "$tmp"' EXIT
fails=0
result() { printf '  [%s] %s\n' "$1" "$2"; if [ "$1" = FAIL ]; then fails=$((fails + 1)); fi; }
same() { [ -s "$1" ] && [ -s "$2" ] && cmp -s "$1" "$2"; }   # both non-empty, identical
now() { python3 -c 'import time; print(time.time())'; }
timed() {   # label cmd... ; runs, prints the wall time, returns the status
    local label=$1 t0 t1 st; shift
    t0=$(now); "$@" > "$tmp/$label.log" 2>&1; st=$?; t1=$(now)
    printf '  ....  %-34s %6.2f s%s\n' "$label" "$(python3 -c "print($t1 - $t0)")" \
        "$([ $st -ne 0 ] && echo "  (exit $st, see $tmp/$label.log)")"
    return $st
}

echo "machine: $(uname -sm)"
[ -n "${NS_DISABLE_METAL:-}" ] && echo "warning: NS_DISABLE_METAL is set in the environment; both runs use the CPU"

# ── excerpt ───────────────────────────────────────────────────────────────
bytes=$(( seconds * sr * nch * 2 ))
head -c "$bytes" "$src" > "$tmp/x.dat"
echo "excerpt: $(( $(wc -c < "$tmp/x.dat" | tr -d ' ') / (nch * 2) )) frames x $nch channels from $(basename "$src")"

# ── process_medianfilter ──────────────────────────────────────────────────
echo "process_medianfilter (w $window):"
if timed medianfilter_metal process_medianfilter -n "$nch" -w "$window" "$tmp/x.dat" "$tmp/mf_metal.fil" \
   && timed medianfilter_cpu env NS_DISABLE_METAL=1 process_medianfilter -n "$nch" -w "$window" "$tmp/x.dat" "$tmp/mf_cpu.fil"; then
    if same "$tmp/mf_metal.fil" "$tmp/mf_cpu.fil"; then result PASS "identical output"
    else result FAIL "outputs differ (or are empty)"; fi
else
    result FAIL "a run failed"
fi

# ── process_butterworth ───────────────────────────────────────────────────
echo "process_butterworth (-p single, high-pass 800 Hz):"
if timed butterworth_metal process_butterworth -n "$nch" -s "$sr" -p single "$tmp/x.dat" "$tmp/bw_metal.fil" \
   && timed butterworth_cpu env NS_DISABLE_METAL=1 process_butterworth -n "$nch" -s "$sr" -p single "$tmp/x.dat" "$tmp/bw_cpu.fil"; then
    line=$(python3 - "$tmp/bw_metal.fil" "$tmp/bw_cpu.fil" <<'EOF'
import sys, numpy as np
a = np.fromfile(sys.argv[1], np.int16).astype(np.int32)
b = np.fromfile(sys.argv[2], np.int16).astype(np.int32)
d = np.abs(a - b) if a.size == b.size and a.size else np.array([99999])
print(int(d.max()), f"{(d > 0).mean():.4%}")
EOF
)
    set -- $line
    if [ "${1:-99999}" -le 1 ] && python3 -c "import sys; sys.exit(0 if float('${2%\%}') < 0.5 else 1)"; then
        result PASS "max |diff| $1 LSB, $2 of samples differ"
    else
        result FAIL "max |diff| ${1:-?} LSB, ${2:-?} of samples differ"
    fi
else
    result FAIL "a run failed"
fi

# ── process_medianthreshold ───────────────────────────────────────────────
echo "process_medianthreshold (each channel its own group):"
chans=$(seq -s: 0 $((nch - 1)))
if timed medianthreshold_metal process_medianthreshold -f "$tmp/bw_cpu.fil" -n "$nch" -c "$chans" \
   && timed medianthreshold_cpu env NS_DISABLE_METAL=1 process_medianthreshold -f "$tmp/bw_cpu.fil" -n "$nch" -c "$chans"; then
    if same "$tmp/medianthreshold_metal.log" "$tmp/medianthreshold_cpu.log"; then
        result PASS "identical thresholds ($(head -c 60 "$tmp/medianthreshold_cpu.log")...)"
    else result FAIL "thresholds differ (or are empty)"; fi
else
    result FAIL "a run failed"
fi

# ── KiloKlustaKwik ────────────────────────────────────────────────────────
if [ -n "$kkGroup" ]; then
    echo "KiloKlustaKwik vs KiloKlustaKwik_cpu (group $kkGroup):"
    base=$(basename "$session")
    for side in metal cpu; do
        mkdir -p "$tmp/kk_$side"
        for f in "$session".*; do
            case "$f" in *.clu.*|*.clu.*.*) continue ;; esac
            ln -s "$f" "$tmp/kk_$side/$(basename "$f")"
        done
    done
    # shellcheck disable=SC2086
    if (cd "$tmp/kk_metal" && timed kk_metal KiloKlustaKwik "$base" "$kkGroup" $kkArgs) \
       && (cd "$tmp/kk_cpu" && timed kk_cpu KiloKlustaKwik_cpu "$base" "$kkGroup" $kkArgs); then
        grep -h "compute:" "$tmp/kk_metal.log" | head -1 | sed 's/^/  ....  KiloKlustaKwik /'
        a=$(ls "$tmp"/kk_metal/"$base".clu.* 2>/dev/null | head -1)
        b=$(ls "$tmp"/kk_cpu/"$base".clu.* 2>/dev/null | head -1)
        if [ -z "$a" ] || [ -z "$b" ]; then
            result FAIL "a run wrote no .clu"
        else
            python3 - "$a" "$b" <<'EOF'
import sys, numpy as np
def load(p):
    raw = open(p, "rb").read()
    try:                                   # text: count, then one id per line
        v = np.array(raw.decode().split(), dtype=np.int64)
    except (UnicodeDecodeError, ValueError):
        v = np.frombuffer(raw, np.int32).astype(np.int64)
    return v[1:]
def ari(x, y):
    _, xi = np.unique(x, return_inverse=True); _, yi = np.unique(y, return_inverse=True)
    t = np.zeros((xi.max() + 1, yi.max() + 1)); np.add.at(t, (xi, yi), 1)
    c2 = lambda n: n * (n - 1) / 2
    s, a, b, n = c2(t).sum(), c2(t.sum(1)).sum(), c2(t.sum(0)).sum(), c2(len(x))
    e = a * b / n
    return (s - e) / (0.5 * (a + b) - e) if a + b != 2 * e else 1.0
x, y = load(sys.argv[1]), load(sys.argv[2])
if len(x) != len(y):
    print(f"  [FAIL] .clu lengths differ ({len(x)} vs {len(y)})"); sys.exit(1)
print(f"  ....  clusters: Metal {len(np.unique(x))}, CPU {len(np.unique(y))}; spikes {len(x)}")
print(f"  [INFO] adjusted Rand index Metal vs CPU: {ari(x, y):.4f} (compare with CUDA vs CPU on the workstation)")
EOF
            [ $? -eq 0 ] || fails=$((fails + 1))
        fi
    else
        result FAIL "a KiloKlustaKwik run failed"
    fi
fi

echo
if [ $fails -eq 0 ]; then echo "metal_validate: PASS"; else echo "metal_validate: FAIL ($fails)"; fi
exit $((fails > 0))
