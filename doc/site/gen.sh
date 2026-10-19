#!/usr/bin/env bash
#
# Regenerate the documentation from the canonical DocBook source
# (src/neuroscope/doc/en/index.docbook) into two outputs:
#
#   1. doc/site/docs/handbook/*.md  -- the MkDocs handbook (one file per chapter)
#   2. src/neuroscope/doc/en/handbook.html -- a single, QTextBrowser-safe HTML
#                                       handbook for the in-app F1 viewer; the
#                                       install puts it beside Images/ as index.html.
#                                       Committed, like (1), so a build needs no pandoc.
#      With INAPP_OUT_DIR, also <out>/index.html + Images/ (a browsable copy).
#
# This replaces the retired KDE meinproc / DocBook-XSL toolchain with pandoc,
# which installs anywhere. Requires: pandoc, xmllint, iconv, python3.
#
# Usage:  doc/site/gen.sh [INAPP_OUT_DIR]
#
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
tools="$here/tools"
src="$root/src/neuroscope/doc/en/index.docbook"
images="$root/src/neuroscope/doc/en/Images"
inapp_out="${1:-}"
inapp_html="$root/src/neuroscope/doc/en/handbook.html"

for bin in pandoc xmllint iconv python3; do
    command -v "$bin" >/dev/null 2>&1 || { echo "gen.sh: missing required tool: $bin" >&2; exit 1; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# 1. Make the DocBook self-contained (no external KDE DTD) and UTF-8, then
#    resolve all general entities so pandoc sees plain XML.
{ cat "$tools/doctype.xml"; sed -n '/<book/,$p' "$src" | iconv -f ISO-8859-1 -t UTF-8; } > "$work/self.xml"
xmllint --noent --nonet "$work/self.xml" > "$work/resolved.xml"

# 2. Preserve non-heading DocBook ids as explicit anchors (pandoc drops them).
python3 "$tools/inject_anchors.py" "$work/resolved.xml" "$work/anchored.xml"

# 3a. MkDocs Markdown: convert, tidy, split per chapter + rewrite cross-file links.
pandoc -f docbook -t markdown-smart --wrap=none "$work/anchored.xml" -o "$work/raw.md"
python3 "$tools/tidy.py" "$work/raw.md" "$work/final.md"
rm -f "$here/docs/handbook"/*.md
mkdir -p "$here/docs/handbook"
python3 "$tools/split_rewrite.py" "$work/final.md" "$here/docs/handbook"

# 3b. In-app HTML: single self-contained page for neuroscope's QTextBrowser viewer.
pandoc -f docbook -t html4 --toc --toc-depth=2 \
       --template="$tools/inapp.html.template" \
       --metadata title="The NeuroScope Handbook" \
       "$work/anchored.xml" -o "$inapp_html"
if [ -n "$inapp_out" ]; then
    mkdir -p "$inapp_out"
    cp "$inapp_html" "$inapp_out/index.html"
    rm -rf "$inapp_out/Images"
    cp -r "$images" "$inapp_out/Images"
fi

echo "gen.sh: handbook Markdown -> $here/docs/handbook/"
echo "gen.sh: in-app HTML       -> $inapp_html${inapp_out:+ (copy in $inapp_out)}"
