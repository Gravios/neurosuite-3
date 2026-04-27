#!/usr/bin/env bash
#
# apply-doc-reorg.sh — finish the doc tree reorganisation.
#
# After extracting the tarball over your repo, run this script from
# the repo root to remove files that have been moved to new
# locations.  The script is idempotent: running it twice is safe.
#
# It uses `git rm` when run inside a git checkout, falling back to
# plain `rm` otherwise.

set -euo pipefail

if [ ! -f CHANGELOG.md ] || [ ! -d doc/design ]; then
    echo "ERROR: this script must be run from the repo root, AFTER" >&2
    echo "extracting the doc-reorg tarball.  CHANGELOG.md and" >&2
    echo "doc/design/ should already exist." >&2
    exit 1
fi

# Pick remover based on git presence.
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    rm_cmd() { git rm -f "$@"; }
else
    rm_cmd() { rm -f "$@"; }
fi

# ── Files that have been MOVED to doc/design/ ───────────────────────
declare -a moved_to_design=(
    CHANGES-reextract-v2.md
    CHANGES-reextractspikes.md
    CHANGES-decomposecollisions-fixes.md
    CHANGES-subtractspikes-fixes.md
    CHANGES-neuroscope-raster-fixes.md
    CHANGES-template-yaml.md
    CHANGES-kk-prior.md
    OPTIMIZE.md
    modeling-recommendations.md
)

# ── Files that have been REPLACED ──────────────────────────────────
# CHANGES.md → CHANGELOG.md (same content, renamed).
# We never remove CHANGES.md if CHANGELOG.md doesn't exist for some reason.

# ── File that has been MOVED to doc/workflows/ ─────────────────────
declare -a moved_to_workflows=(
    doc/ndmanager-plugins/kk-prior-workflow.md
)

removed=0
skipped=0

for f in "${moved_to_design[@]}"; do
    if [ -f "$f" ]; then
        # Confirm the new location actually exists before deleting.
        topic="${f#CHANGES-}"
        topic="${topic%-fixes.md}"
        topic="${topic%.md}"
        case "$f" in
            CHANGES-reextract-v2.md)              new=doc/design/reextract-v2.md ;;
            CHANGES-reextractspikes.md)           new=doc/design/reextractspikes-v1.md ;;
            CHANGES-decomposecollisions-fixes.md) new=doc/design/decomposecollisions.md ;;
            CHANGES-subtractspikes-fixes.md)      new=doc/design/subtractspikes-botm.md ;;
            CHANGES-neuroscope-raster-fixes.md)   new=doc/design/neuroscope-raster.md ;;
            CHANGES-template-yaml.md)             new=doc/design/template-yaml.md ;;
            CHANGES-kk-prior.md)                  new=doc/design/kk-prior.md ;;
            OPTIMIZE.md)                          new=doc/design/optimization.md ;;
            modeling-recommendations.md)          new=doc/design/modeling-l1-vs-botm.md ;;
        esac
        if [ -f "$new" ]; then
            echo "  removing $f  (moved to $new)"
            rm_cmd "$f"
            removed=$((removed + 1))
        else
            echo "  SKIPPING $f  ($new not found — extract the tarball first)" >&2
            skipped=$((skipped + 1))
        fi
    fi
done

# CHANGES.md → CHANGELOG.md (rename).
if [ -f CHANGES.md ] && [ -f CHANGELOG.md ]; then
    # Make sure CHANGELOG.md is the new (reorganised) version, not just
    # a stale copy.  The new version references doc/design/.  If it
    # doesn't, leave both alone and let the user investigate.
    if grep -q "doc/design/" CHANGELOG.md; then
        echo "  removing CHANGES.md  (renamed to CHANGELOG.md)"
        rm_cmd CHANGES.md
        removed=$((removed + 1))
    else
        echo "  SKIPPING CHANGES.md  (CHANGELOG.md doesn't look reorganised)" >&2
        skipped=$((skipped + 1))
    fi
fi

# kk-prior-workflow.md → workflows/empirical-priors.md
for f in "${moved_to_workflows[@]}"; do
    if [ -f "$f" ]; then
        if [ -f doc/workflows/empirical-priors.md ]; then
            echo "  removing $f  (moved to doc/workflows/empirical-priors.md)"
            rm_cmd "$f"
            removed=$((removed + 1))
        else
            echo "  SKIPPING $f  (workflows/empirical-priors.md not found)" >&2
            skipped=$((skipped + 1))
        fi
    fi
done

echo
echo "Done.  Removed $removed file(s).  Skipped $skipped."
if [ "$skipped" -gt 0 ]; then
    echo "Re-run after addressing the SKIPPING messages above." >&2
    exit 2
fi
