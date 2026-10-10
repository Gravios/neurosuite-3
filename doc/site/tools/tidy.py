#!/usr/bin/env python3
"""Tidy pandoc's DocBook->Markdown output for MkDocs/Material:
  * <span class="menuchoice">File > Open</span>  ->  **File > Open**
  * pandoc fenced <note> divs (:::+ note ... :::+)  ->  Material "!!! note" admonitions
  * other fenced divs (figures/screenshots wrappers)  ->  unwrapped (markers dropped)
Definition-list ":" lines (single colon) are left untouched.

Usage: tidy.py <raw.md> <final.md>
"""
import re
import sys

txt = open(sys.argv[1]).read()
txt = re.sub(r'<span class="menuchoice">(.*?)</span>', r'**\1**', txt, flags=re.S)
# pandoc writes injected anchors as empty spans []{#id}; Python-Markdown doesn't
# grok that, so emit a real HTML anchor it passes through verbatim.
txt = re.sub(r'\[\]\{#([A-Za-z0-9._-]+)\}', r'<a id="\1"></a>', txt)

op_note = re.compile(r'^:{3,}\s*(\{[^}]*\.note[^}]*\}|\{?\s*\.?note\s*\}?)\s*$')
op_gen = re.compile(r'^:{3,}\s*\{[^}]*\}\s*$')
close = re.compile(r'^:{3,}\s*$')

out, stack = [], []
for ln in txt.split("\n"):
    if op_note.match(ln):
        out += ["!!! note", ""]
        stack.append("note")
        continue
    if op_gen.match(ln):
        stack.append("generic")
        continue
    if close.match(ln) and stack:
        if stack.pop() == "note":
            out.append("")
        continue
    if any(s == "note" for s in stack):
        out.append(("    " + ln) if ln.strip() else "")
    else:
        out.append(ln)
open(sys.argv[2], "w").write("\n".join(out) + "\n")
