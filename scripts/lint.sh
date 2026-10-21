#!/usr/bin/env bash
# lint.sh — the fast checks that need no build: run before sending a patch,
# and by the `lint` job in .github/workflows/ci-linux.yml.
#
#   scripts/lint.sh            # from anywhere inside the repository
#
# 1. bash -n (zsh -n for zsh scripts, when zsh is installed) on every tracked
#    shell script, found by shebang or, without one, by .sh.
# 2. ruff on every tracked Python file (found by .py or shebang), limited to
#    the rules that are defects rather than style: syntax errors (E9),
#    invalid comparisons/prints (F63), misplaced statements (F7) and
#    undefined names (F82).  The tree is clean against these; keep it so.
# 3. No tracked file matches .gitignore (generated output committed by hand).
# 4. The build-system invariants (tests/cmake_invariants.cmake), which need
#    only cmake.
#
# Vendored code (libsamplerate) is skipped.  Exits non-zero if any check fails.
set -u
root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel) || exit 2
cd "$root" || exit 2

fail=0
section() { printf '\n== %s\n' "$1"; }

# Tracked files, NUL-safe, without the vendored tree.
mapfile -d '' files < <(git ls-files -z | grep -zv 'libsamplerate')

# The shebang decides the language; without one, the extension does.
shells=() zshs=() pys=()
for f in "${files[@]}"; do
    [ -f "$f" ] || continue
    first=$(head -c 128 "$f" 2>/dev/null | head -n 1 | tr -d '\0')
    case "$first" in
        '#!'*python*) pys+=("$f"); continue ;;
        '#!'*zsh*)    zshs+=("$f"); continue ;;
        '#!'*bash*|'#!'*/sh|'#!'*/sh' '*) shells+=("$f"); continue ;;
    esac
    case "$f" in
        *.sh) shells+=("$f") ;;
        *.py) pys+=("$f") ;;
    esac
done

section "bash -n (${#shells[@]} scripts)"
bad=0
for f in "${shells[@]}"; do
    if ! out=$(bash -n "$f" 2>&1); then
        printf 'FAIL %s\n%s\n' "$f" "$out"; bad=1
    fi
done
[ $bad -eq 0 ] && echo "ok" || fail=1

if [ ${#zshs[@]} -gt 0 ]; then
    section "zsh -n (${#zshs[@]} scripts)"
    if command -v zsh >/dev/null 2>&1; then
        bad=0
        for f in "${zshs[@]}"; do
            if ! out=$(zsh -n "$f" 2>&1); then
                printf 'FAIL %s\n%s\n' "$f" "$out"; bad=1
            fi
        done
        [ $bad -eq 0 ] && echo "ok" || fail=1
    else
        echo "skipped: zsh not installed"
    fi
fi

section "ruff E9,F63,F7,F82 (${#pys[@]} files)"
if command -v ruff >/dev/null 2>&1; then
    ruff check --no-cache --quiet --select E9,F63,F7,F82 "${pys[@]}" && echo "ok" || fail=1
else
    echo "FAIL ruff not installed (python3 -m pip install ruff)"; fail=1
fi

section "no generated files tracked"
# A tracked file that .gitignore would ignore is generated output that was
# committed by hand (configure logs, Doxygen HTML, compile databases, ...).
mapfile -t ignored < <(git ls-files -ci --exclude-standard)
if [ ${#ignored[@]} -eq 0 ]; then
    echo "ok"
else
    printf 'FAIL tracked but ignored (generated?): %s\n' "${ignored[@]:0:20}"
    [ ${#ignored[@]} -gt 20 ] && echo "     ... and $(( ${#ignored[@]} - 20 )) more"
    fail=1
fi

section "build-system invariants"
if out=$(cmake -DREPO="$root" -P tests/cmake_invariants.cmake 2>&1); then
    echo "ok"
else
    printf '%s\n' "$out" | grep -E 'FAIL|Error|error' ; fail=1
fi

printf '\n%s\n' "$([ $fail -eq 0 ] && echo 'lint: all checks passed' || echo 'lint: FAILED')"
exit $fail
