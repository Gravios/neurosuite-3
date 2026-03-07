#!/usr/bin/env bash
# =============================================================================
# run_clazy_connect.sh — convert old-style SIGNAL/SLOT connects to Qt5 pointer
#                        syntax across the neurosuite-3 source tree.
#
# Prerequisites:
#   sudo apt install clazy clang-tools
#
# Usage:
#   # From the repo root, after a full cmake configure + build:
#   ./tools/run_clazy_connect.sh [BUILD_DIR] [--dry-run]
#
#   BUILD_DIR  Path to the top-level superbuild directory (default: ./build)
#   --dry-run  Show what would be changed without modifying files
#
# How it works:
#   1. Locates every sub-project compile_commands.json under BUILD_DIR.
#   2. Merges them into a single combined_compile_commands.json so that
#      clazy-standalone can be pointed at one file.
#   3. Runs clazy-standalone --checks=old-style-connect --export-fixes on every
#      affected .cpp file (the 57 files with SIGNAL/SLOT strings).
#   4. clazy rewrites the source files in-place with correct pointer syntax,
#      inserting qOverload<>() casts automatically where signals are overloaded.
#
# Notes:
#   - Run from a clean git working tree so you can review / revert the diff.
#   - A few connects involving custom third-party signals (KUrlLabel etc.) may
#     be left unconverted if clazy can't resolve the type — fix those manually.
#   - After running, do a full rebuild to confirm no new errors were introduced.
# =============================================================================

set -euo pipefail

# ── Args ──────────────────────────────────────────────────────────────────────
BUILD_DIR="${1:-./build}"
DRY_RUN=0
for arg in "$@"; do
    [[ "$arg" == "--dry-run" ]] && DRY_RUN=1
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$(realpath "$BUILD_DIR")"
CLAZY_DB_DIR="$REPO_ROOT/tools/clazy_db"
COMBINED_DB="$CLAZY_DB_DIR/compile_commands.json"
mkdir -p "$CLAZY_DB_DIR"

# ── Sanity checks ─────────────────────────────────────────────────────────────
if ! command -v clazy-standalone &>/dev/null; then
    echo "ERROR: clazy-standalone not found."
    echo "Install with:  sudo apt install clazy"
    exit 1
fi

if ! command -v python3 &>/dev/null; then
    echo "ERROR: python3 not found."
    exit 1
fi

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "ERROR: Build directory not found: $BUILD_DIR"
    echo "Run cmake -B build && cmake --build build first."
    exit 1
fi

# ── Step 1: find all compile_commands.json files ──────────────────────────────
echo ">> Searching for compile_commands.json under $BUILD_DIR ..."
mapfile -t DB_FILES < <(find "$BUILD_DIR" -name "compile_commands.json" -not -path "*/CMakeFiles/*")

if [[ ${#DB_FILES[@]} -eq 0 ]]; then
    echo "ERROR: No compile_commands.json found under $BUILD_DIR."
    echo "Ensure CMAKE_EXPORT_COMPILE_COMMANDS=ON is set (already done in CMakeLists.txt)."
    echo "Re-run: cmake -B build && cmake --build build"
    exit 1
fi

echo "   Found ${#DB_FILES[@]} database(s):"
for f in "${DB_FILES[@]}"; do
    echo "     $f"
done

# ── Step 2: merge into one combined database ──────────────────────────────────
echo ""
echo ">> Merging into $COMBINED_DB ..."
python3 - "${DB_FILES[@]}" "$COMBINED_DB" <<'PYEOF'
import json, sys

combined = []
seen = set()
for path in sys.argv[1:-1]:
    try:
        entries = json.load(open(path))
        for e in entries:
            key = e.get("file", "")
            if key not in seen:
                seen.add(key)
                combined.append(e)
    except Exception as ex:
        print(f"  WARNING: could not read {path}: {ex}", file=sys.stderr)

out = sys.argv[-1]
json.dump(combined, open(out, "w"), indent=2)
print(f"   Merged {len(combined)} translation units → {out}")
PYEOF

# ── Step 3: collect the affected .cpp files ───────────────────────────────────
#
# These are the 57 files that contain SIGNAL() / SLOT() macro calls.
# We regenerate this list dynamically from the source tree so it stays
# accurate as files are added or cleaned up over time.
echo ""
echo ">> Collecting files with SIGNAL/SLOT macro usage ..."
mapfile -t CPP_FILES < <(
    grep -rl "SIGNAL\s*(\|SLOT\s*(" \
        "$REPO_ROOT/src/klusters/src" \
        "$REPO_ROOT/src/neuroscope/src" \
        "$REPO_ROOT/src/ndmanager/src" \
        "$REPO_ROOT/src/libklustersshared/src" \
        2>/dev/null \
        | grep "\.cpp$" \
        | sort
)

echo "   Found ${#CPP_FILES[@]} file(s) to process."

if [[ ${#CPP_FILES[@]} -eq 0 ]]; then
    echo "   Nothing to do — no SIGNAL/SLOT macros found. Already clean!"
    exit 0
fi

# ── Step 4: run clazy ─────────────────────────────────────────────────────────
echo ""
if [[ $DRY_RUN -eq 1 ]]; then
    echo ">> DRY RUN — showing clazy command only (no files will be modified)"
    echo ""
    echo "   clazy-standalone \\"
    echo "     -p $CLAZY_DB_DIR \\"
    echo "     ${EXTRA_ARGS[*]} \\"
    echo "     --checks=old-style-connect \\"
    echo "     --export-fixes=<per-file.yaml> \\"
    echo "     <file>"
    echo "   clang-apply-replacements $REPO_ROOT/tools/clazy_fixes"
    echo ""
    printf '   %s\n' "${CPP_FILES[@]}"
    exit 0
fi

FIXES_DIR="$REPO_ROOT/tools/clazy_fixes"
rm -rf "$FIXES_DIR"
mkdir -p "$FIXES_DIR"

# ── Detect GCC stdlib include paths for clang-based clazy ────────────────────
#
# clazy-standalone is built on clang, which doesn't automatically find GCC's
# C++ standard library headers (type_traits, algorithm, etc.) even when the
# compile_commands.json was generated with GCC.  We pass --gcc-toolchain=/usr
# so clang locates the correct libstdc++ headers.
GCC_VER=$(gcc -dumpversion 2>/dev/null | cut -d. -f1)
EXTRA_ARGS=(
    "--extra-arg=--gcc-toolchain=/usr"
)
if [[ -n "$GCC_VER" ]]; then
    EXTRA_ARGS+=(
        "--extra-arg=-I/usr/include/c++/${GCC_VER}"
        "--extra-arg=-I/usr/include/x86_64-linux-gnu/c++/${GCC_VER}"
    )
fi
echo "   GCC version: ${GCC_VER:-unknown}"
echo "   Extra clazy args: ${EXTRA_ARGS[*]}"
echo ""
echo "   Check: old-style-connect"
echo "   Mode:  --export-fixes → clang-apply-replacements"
echo ""

# clazy 1.11+ (clang-15 based, Ubuntu 25) uses --export-fixes=<file>
# instead of --fixit. We export one yaml file per source file, then
# apply them all at once with clang-apply-replacements.

ERRORS=0
PROCESSED=0
for cpp in "${CPP_FILES[@]}"; do
    rel="${cpp#$REPO_ROOT/}"
    # Derive a safe filename for the fixes yaml
    safe="${cpp//\//_}"
    fixes_file="$FIXES_DIR/${safe}.yaml"

    printf "   %-60s " "$rel"
    if clazy-standalone \
            -p "$CLAZY_DB_DIR" \
            "${EXTRA_ARGS[@]}" \
            --checks="old-style-connect" \
            --export-fixes="$fixes_file" \
            "$cpp" 2>/tmp/clazy_err.txt; then
        echo "OK"
    else
        # clazy exits non-zero when it finds issues to fix — that's expected.
        # Only treat it as a real error if there's an unrecognised argument etc.
        if grep -q "Unknown command line argument\|error:" /tmp/clazy_err.txt; then
            echo "ERROR (see below)"
            cat /tmp/clazy_err.txt | grep -v "^$" | sed 's/^/     /'
            ((ERRORS++)) || true
        else
            echo "fixes exported"
        fi
    fi
    ((PROCESSED++)) || true
done

# ── Step 5: apply all collected fixes ─────────────────────────────────────────
YAML_COUNT=$(find "$FIXES_DIR" -name "*.yaml" 2>/dev/null | wc -l)

if [[ $YAML_COUNT -eq 0 ]]; then
    echo ""
    echo "   No fix files generated — nothing to apply."
else
    echo ""
    echo ">> Applying $YAML_COUNT fix file(s) with clang-apply-replacements ..."

    if ! command -v clang-apply-replacements &>/dev/null; then
        # Try versioned name (Ubuntu ships clang-apply-replacements-15 etc.)
        CAR=$(compgen -c clang-apply-replacements 2>/dev/null | sort -V | tail -1)
        if [[ -z "$CAR" ]]; then
            echo ""
            echo "ERROR: clang-apply-replacements not found."
            echo "Install with:  sudo apt install clang-tools"
            echo "The fix yaml files are preserved in: $FIXES_DIR"
            echo "Apply manually with:  clang-apply-replacements $FIXES_DIR"            exit 1
        fi
    else
        CAR=clang-apply-replacements
    fi

    "$CAR" "$FIXES_DIR"
    echo "   Done."
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "================================================================"
echo " clazy run complete"
echo "   Files processed : $PROCESSED"
echo "   Files with warns: $ERRORS"
echo ""
echo " Next steps:"
echo "   1. git diff src/  — review the rewrites"
echo "   2. cmake --build build  — confirm clean compile"
echo "   3. git add -p && git commit  — stage incrementally"
echo ""
echo " Any connects clazy could not convert (e.g. KUrlLabel signals)"
echo " will remain as SIGNAL/SLOT strings — convert those manually."
echo "================================================================"
