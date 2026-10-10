#!/usr/bin/env python3
"""Split the single converted handbook Markdown into one file per top-level
chapter (H1), and rewrite every intra-document link ](#id) to point at the
chapter file that declares the target id, so anchors keep working across pages.

Usage: split_rewrite.py <final.md> <outdir>
"""
import os
import re
import sys

src, outdir = sys.argv[1], sys.argv[2]
os.makedirs(outdir, exist_ok=True)


def slug(t):
    t = re.sub(r'\{#[^}]+\}', '', t)
    t = re.sub(r'[*`_\[\]()]', '', t).strip().lower()
    return re.sub(r'[^a-z0-9]+', '-', t).strip('-')


lines = open(src).read().splitlines()
chapters, cur = [], None
for ln in lines:
    m = re.match(r'^# (.*)', ln)
    if m:
        cur = {"title": re.sub(r'\{#[^}]+\}', '', m.group(1)).strip(), "lines": [ln]}
        chapters.append(cur)
    elif cur is not None:
        cur["lines"].append(ln)
for i, c in enumerate(chapters, 1):
    c["file"] = f"{i:02d}-{slug(c['title'])}.md"

# id -> declaring file
id2file = {}
for c in chapters:
    body = "\n".join(c["lines"])
    for i in set(re.findall(r'\{#([A-Za-z0-9._-]+)', body)) | set(re.findall(r'id="([A-Za-z0-9._-]+)"', body)):
        id2file[i] = c["file"]
    for ln in c["lines"]:
        hm = re.match(r'^#+\s+(.*)$', ln)
        if hm:
            s = slug(hm.group(1))
            if s:
                id2file.setdefault(s, c["file"])

broken = []
for c in chapters:
    body = "\n".join(c["lines"])

    def repl(m):
        tid = m.group(1)
        f = id2file.get(tid)
        if not f:
            broken.append(tid)
            return m.group(0)
        return f"](./{f}#{tid})"

    body = re.sub(r'\]\(#([A-Za-z0-9._-]+)\)', repl, body)
    open(os.path.join(outdir, c["file"]), "w").write(body + "\n")

print("split_rewrite: " + ", ".join(c["file"] for c in chapters))
if broken:
    sys.exit("split_rewrite: BROKEN anchors: " + ", ".join(sorted(set(broken))))
