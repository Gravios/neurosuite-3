#!/usr/bin/env python3
"""doc/ndmanager-plugins documents every step, and its links hold.

  - every ndm_* script (and the documented helpers) appears in the command
    reference doc/ndmanager-plugins/commands/README.md, linked to a page;
  - every page under commands/ and formats/ is listed in its README.md;
  - every relative link in doc/ndmanager-plugins — and every link into it
    from the rest of the repository's Markdown — points at an existing file,
    and an #anchor at an existing heading of that file.
Exits nonzero on failure (repo test convention).
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
DOC = os.path.join(ROOT, "doc", "ndmanager-plugins")
SCRIPTS = os.path.join(ROOT, "src", "ndmanager-plugins", "scripts")

# Documented helpers besides the ndm_* scripts.
EXTRA = ["kk_build_prior.py", "kk_resolve_prior.py", "process_initeap"]

fails = 0


def check(name, cond, detail=""):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}{'' if cond or not detail else ': ' + detail}")
    if not cond:
        fails += 1


LINK = re.compile(r"(?<!!)\[([^\]]*)\]\(([^)\s]+)\)")


def slug(heading: str) -> str:
    """GitHub-style anchor of a Markdown heading."""
    h = re.sub(r"[`*_]", lambda m: "_" if m.group(0) == "_" else "", heading.strip().lower())
    h = "".join(c for c in h if c.isalnum() or c in " -_")
    return h.replace(" ", "-")


_anchors: dict = {}


def anchors(path: str) -> set:
    if path not in _anchors:
        seen: dict = {}
        out = set()
        in_code = False
        for ln in open(path, encoding="utf-8"):
            if ln.startswith("```"):
                in_code = not in_code
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", ln)
            if m and not in_code:
                s = slug(m.group(2))
                n = seen.get(s, 0)
                seen[s] = n + 1
                out.add(s if n == 0 else f"{s}-{n}")
        _anchors[path] = out
    return _anchors[path]


def links(path: str):
    in_code = False
    for ln in open(path, encoding="utf-8"):
        if ln.startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        for m in LINK.finditer(ln):
            yield m.group(1), m.group(2)


def broken(path: str, target: str):
    if re.match(r"^[a-z]+:", target) or target.startswith("mailto:"):
        return None
    file, _, anchor = target.partition("#")
    dest = os.path.normpath(os.path.join(os.path.dirname(path), file)) if file else path
    if not os.path.exists(dest):
        return f"{os.path.relpath(path, ROOT)} → {target} (no such file)"
    if anchor and dest.endswith(".md") and anchor not in anchors(dest):
        return f"{os.path.relpath(path, ROOT)} → {target} (no such heading)"
    return None


# ── every script in the command reference ───────────────────────────────────
index = os.path.join(DOC, "commands", "README.md")
linked_names = set()
for text, target in links(index):
    for name in re.findall(r"`([^`]+)`", text):
        linked_names.add(name)
names = []
for p in sorted(glob.glob(os.path.join(SCRIPTS, "ndm_*"))):
    b = os.path.basename(p)
    if "." in b or os.path.isdir(p):
        continue
    with open(p, errors="replace") as fh:
        if fh.read(2) == "#!" or b == "ndm_custody" or b == "ndm_functions":
            names.append(b)
names += EXTRA
missing = [n for n in names if n not in linked_names]
check("every ndm_* step is in commands/README.md", not missing, ", ".join(missing))

# ── every page listed in its index ──────────────────────────────────────────
for sub in ("commands", "formats"):
    idx = os.path.join(DOC, sub, "README.md")
    listed = {os.path.normpath(os.path.join(DOC, sub, t.partition("#")[0]))
              for _, t in links(idx)}
    pages = [p for p in glob.glob(os.path.join(DOC, sub, "*.md")) if not p.endswith("README.md")]
    unlisted = sorted(os.path.basename(p) for p in pages if os.path.normpath(p) not in listed)
    check(f"every {sub}/ page is in {sub}/README.md", not unlisted, ", ".join(unlisted))

# ── links ───────────────────────────────────────────────────────────────────
bad = []
for p in sorted(glob.glob(os.path.join(DOC, "**", "*.md"), recursive=True)):
    for _, t in links(p):
        e = broken(p, t)
        if e:
            bad.append(e)
check("links inside doc/ndmanager-plugins resolve", not bad, "\n      " + "\n      ".join(bad))

bad = []
for p in sorted(glob.glob(os.path.join(ROOT, "**", "*.md"), recursive=True)):
    if p.startswith(DOC + os.sep) or "/build" in p or "/.git/" in p:
        continue
    for _, t in links(p):
        if "ndmanager-plugins/" not in t:
            continue
        e = broken(p, t)
        if e:
            bad.append(e)
check("links into doc/ndmanager-plugins resolve", not bad, "\n      " + "\n      ".join(bad))

print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
sys.exit(1 if fails else 0)
