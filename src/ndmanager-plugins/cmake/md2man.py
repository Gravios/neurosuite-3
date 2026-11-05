#!/usr/bin/env python3
"""
md2man.py — a man page for one command, from its page in the command reference

  md2man.py --docdir doc/ndmanager-plugins/commands --name ndm_hipass
            [--source "neurosuite-3 3.1.0"] [--manual "NDManager Plugins"]
            [--section 1] [--date YYYY-MM-DD] [-o ndm_hipass.1]
  md2man.py --docdir DIR --find NAME     print the page a command is documented on

The command reference (doc/ndmanager-plugins/commands/*.md) is the maintained
description of every step; this renders it as roff for man(1) so the two cannot
drift.  A command is found on its own page (<name>.md, or a page whose title
names it), or as a "## `name` — ..." section of a shared page (signal-processing,
spike-detection, pca, ...).  From a shared page the man page takes that
section, the page's sections that belong to no command ("Methods", "Raw-data
layout"), its introduction (as NOTES) and its "See also".

Handled Markdown: headings, paragraphs, fenced code, bullet and numbered lists
(nested), tables (one tagged paragraph per row), block quotes, inline code,
bold, italics and links (link text kept; a web address is kept after it).

Standard library only.  copyright (C) 2026 neurosuite-3 contributors  GPL-3.0-or-later
"""
from __future__ import annotations

import argparse
import datetime
import glob
import os
import re
import sys

CMD = re.compile(r"`((?:ndm|process|kk)_?[A-Za-z0-9_.-]*)`")


def heading(line: str):
    m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", line)
    return (len(m.group(1)), m.group(2)) if m else None


def names_in(title: str) -> list[str]:
    """The commands a heading is about: the code spans BEFORE the dash."""
    head = re.split(r"\s+[—–]\s+", title, maxsplit=1)[0]
    return CMD.findall(head)


def find_page(docdir: str, name: str):
    """(path, level, start, end) of where `name` is documented: level 1 = the whole page."""
    pages = sorted(glob.glob(os.path.join(docdir, "*.md")))
    own = os.path.join(docdir, name + ".md")
    order = ([own] if own in pages else []) + [p for p in pages if p != own]
    for p in order:
        lines = open(p, encoding="utf-8").read().splitlines()
        for i, ln in enumerate(lines):
            h = heading(ln)
            if not h:
                continue
            lvl, title = h
            if lvl == 1 and (p == own or name in names_in(title)):
                return p, 1, 0, len(lines)
            if lvl == 2 and name in names_in(title):
                j = next((k for k in range(i + 1, len(lines))
                          if (heading(lines[k]) or (9, ""))[0] <= 2), len(lines))
                return p, 2, i, j
    return None


# ── inline ──────────────────────────────────────────────────────────────────

def uni(s: str) -> str:
    """Every non-ASCII character as \\[uXXXX], which groff and mandoc read whatever the input
    encoding (man-db's preconv is not always in the chain)."""
    return "".join(c if ord(c) < 128 else f"\\[u{ord(c):04X}]" for c in s)


def esc(s: str) -> str:
    return uni(s.replace("\\", "\\e"))


def code(s: str) -> str:
    return "\\fB" + esc(s).replace("-", "\\-") + "\\fR"


def inline(s: str) -> str:
    """Inline Markdown -> roff.  Code spans are set aside first (as placeholders), so bold,
    italics and links may contain them ("**`x` text**", "[`x` format](y)")."""
    raw: list[str] = []

    def keep(m):
        raw.append(m.group(1))
        return f"\x00{len(raw) - 1}\x00"
    t = re.sub(r"`([^`]*)`", keep, s)
    t = t.replace("\\", "\\e")      # non-ASCII is escaped last: its \\[u...] would break the [link] syntax

    def bare(x: str) -> str:            # a code span inside bold/italic: its text, in that font
        return re.sub(r"\x00(\d+)\x00", lambda m: esc(raw[int(m.group(1))]).replace("-", "\\-"), x)
    t = re.sub(r"!\[([^\]]*)\]\([^)]*\)", r"\1", t)                         # images: alt text
    t = re.sub(r"\[([^\]]*)\]\((https?://[^)\s]+)\)", r"\1 <\2>", t)        # web links: keep the address
    t = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", t)                          # other links: the text
    t = re.sub(r"\*\*(.+?)\*\*", lambda m: "\\fB" + bare(m.group(1)) + "\\fR", t)
    t = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", lambda m: "\\fI" + bare(m.group(1)) + "\\fR", t)
    t = re.sub(r"(?<![\w\\])_(?!\s)(.+?)(?<!\s)_(?!\w)", lambda m: "\\fI" + bare(m.group(1)) + "\\fR", t)
    t = uni(t)
    t = re.sub(r"\x00(\d+)\x00", lambda m: code(raw[int(m.group(1))]), t)
    return t


def plain(s: str) -> str:
    """Markup removed (for titles)."""
    s = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", s)
    return re.sub(r"[`*]", "", s).strip()


def line_safe(s: str) -> str:
    return "\\&" + s if s[:1] in (".", "'") else s


# ── blocks ──────────────────────────────────────────────────────────────────

LIST = re.compile(r"^(\s*)([-*+]|\d+[.)])\s+(.*)$")


def split_row(row: str) -> list[str]:
    row = row.strip()
    if row.startswith("|"):
        row = row[1:]
    if row.endswith("|") and not row.endswith("\\|"):
        row = row[:-1]
    cells, cur, i = [], "", 0
    in_code = False
    while i < len(row):
        c = row[i]
        if c == "\\" and i + 1 < len(row) and row[i + 1] == "|":
            cur += "|"; i += 2; continue
        if c == "`":
            in_code = not in_code
        if c == "|" and not in_code:
            cells.append(cur.strip()); cur = ""
        else:
            cur += c
        i += 1
    cells.append(cur.strip())
    return cells


def render(lines: list[str], level_map: dict[int, str]) -> list[str]:
    out: list[str] = []
    i, n = 0, len(lines)
    while i < n:
        ln = lines[i]
        s = ln.strip()
        if not s or re.fullmatch(r"-{3,}|\*{3,}|_{3,}", s) or s.startswith("<!--"):
            i += 1; continue
        if re.match(r"^\*Part of the .*\*$", s):
            i += 1; continue
        h = heading(ln)
        if h:
            lvl, title = h
            kind = level_map.get(lvl, ".SS")
            t = plain(title)
            t = esc(t.upper() if kind == ".SH" else t).replace('"', "\\(dq")
            out.append(f'{kind} "{t}"')
            i += 1; continue
        if s.startswith("```"):
            body = []
            i += 1
            while i < n and not lines[i].strip().startswith("```"):
                body.append(lines[i]); i += 1
            i += 1
            out += [".PP", ".RS 4", ".nf"] + [line_safe(esc(b)) for b in body] + [".fi", ".RE"]
            continue
        if s.startswith("|"):
            rows = []
            while i < n and lines[i].strip().startswith("|"):
                rows.append(split_row(lines[i])); i += 1
            hdr = rows[0]
            data = [r for r in rows[1:] if not all(re.fullmatch(r":?-{2,}:?", c or "--") for c in r)]
            for r in data:
                r = r + [""] * (len(hdr) - len(r))
                tag = inline(r[0]) or " "
                body = inline(r[-1]) if len(r) > 1 else ""
                mid = [f"{esc(plain(hdr[k]))}: {inline(r[k])}" for k in range(1, len(r) - 1) if r[k]]
                if mid:
                    body = "(" + "; ".join(mid) + ")" + (" " + body if body else "")
                out += [".TP", line_safe(tag), line_safe(body)]
            continue
        if s.startswith(">"):
            body = []
            while i < n and lines[i].strip().startswith(">"):
                body.append(lines[i].strip()[1:].strip()); i += 1
            out += [".RS 4", ".PP", line_safe(inline(" ".join(body))), ".RE"]
            continue
        m = LIST.match(ln)
        if m:
            depth_stack = [len(m.group(1))]
            while i < n:
                m = LIST.match(lines[i])
                if not m:
                    # continuation of the last item (indented), or the end of the list
                    if lines[i].strip() and lines[i].startswith(" ") and not lines[i].strip().startswith("```"):
                        out[-1] += " " + inline(lines[i].strip()); i += 1; continue
                    if not lines[i].strip() and i + 1 < n and (LIST.match(lines[i + 1])
                                                               or lines[i + 1].startswith("  ")):
                        i += 1; continue
                    break
                ind, mark, text = len(m.group(1)), m.group(2), m.group(3)
                while ind > depth_stack[-1] + 1:
                    out.append(".RS 4"); depth_stack.append(ind)
                    break
                while len(depth_stack) > 1 and ind < depth_stack[-1]:
                    out.append(".RE"); depth_stack.pop()
                bullet = "\\(bu" if mark in "-*+" else mark
                out += [f'.IP "{bullet}" {2 if mark in "-*+" else 4}', line_safe(inline(text))]
                i += 1
            while len(depth_stack) > 1:
                out.append(".RE"); depth_stack.pop()
            continue
        para = []
        while i < n and lines[i].strip() and not heading(lines[i]) and not LIST.match(lines[i]) \
                and not lines[i].strip().startswith(("```", "|", ">")):
            para.append(lines[i].strip()); i += 1
        out += [".PP", line_safe(inline(" ".join(para)))]
    return out


def man_page(docdir: str, name: str, section: str, source: str, manual: str, date: str) -> str:
    found = find_page(docdir, name)
    if not found:
        raise SystemExit(f"md2man: {name} is not documented in {docdir}")
    path, lvl, a, b = found
    lines = open(path, encoding="utf-8").read().splitlines()
    title_i = next((k for k, ln in enumerate(lines) if (heading(ln) or (0,))[0] == 1), None)
    after_title = 0 if title_i is None else title_i + 1
    if lvl == 1:
        title = heading(lines[title_i])[1] if title_i is not None else name
        body = render(lines[after_title:], {2: ".SH", 3: ".SS", 4: ".SS"})
    else:
        title = heading(lines[a])[1]
        sec = render(lines[a + 1:b], {3: ".SH", 4: ".SS"})
        # the page's own sections that belong to no command, its intro, its See also
        extra, notes, see = [], [], []
        first2 = next((k for k, ln in enumerate(lines) if (heading(ln) or (0,))[0] == 2), len(lines))
        intro = [ln for ln in lines[after_title:first2] if not ln.strip().startswith("**Contents:**")
                 and not re.match(r"^\[`.*`\]\(#", ln.strip())]
        if any(x.strip() for x in intro):
            notes = ['.SH "NOTES"'] + render(intro, {3: ".SS"})
        k = first2
        while k < len(lines):
            h = heading(lines[k])
            if h and h[0] == 2:
                e = next((j for j in range(k + 1, len(lines)) if (heading(lines[j]) or (9,))[0] <= 2), len(lines))
                t = plain(h[1])
                if not names_in(h[1]):
                    blk = render(lines[k:e], {2: ".SH", 3: ".SS", 4: ".SS"})
                    (see if t.lower() == "see also" else extra).extend(blk)
                k = e
            else:
                k += 1
        body = sec + extra + notes + see
    purpose = re.split(r"\s+[—–]\s+", title, maxsplit=1)
    purpose = plain(purpose[1]) if len(purpose) > 1 else plain(title)
    head = [
        f'.\\" Generated by md2man.py from {os.path.relpath(path, os.path.dirname(docdir.rstrip("/")))} '
        "-- edit that page, not this file.",
        f'.TH "{name.upper()}" "{section}" "{date}" "{source}" "{manual}"',
        ".nh",
        '.SH "NAME"', f"{esc(name).replace('-', chr(92) + '-')} \\- {esc(purpose)}",
    ]
    return "\n".join(head + body) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--docdir", required=True)
    ap.add_argument("--name")
    ap.add_argument("--find")
    ap.add_argument("--section", default="1")
    ap.add_argument("--source", default="neurosuite-3")
    ap.add_argument("--manual", default="NDManager Plugins")
    ap.add_argument("--date", default=os.environ.get("SOURCE_DATE_EPOCH") and datetime.datetime.fromtimestamp(
        int(os.environ["SOURCE_DATE_EPOCH"]), datetime.timezone.utc).date().isoformat()
        or datetime.date.today().isoformat())
    ap.add_argument("-o", "--output")
    a = ap.parse_args(argv)
    if a.find:
        f = find_page(a.docdir, a.find)
        if not f:
            return 1
        print(f[0])
        return 0
    if not a.name:
        ap.error("--name or --find")
    text = man_page(a.docdir, a.name, a.section, a.source, a.manual, a.date)
    if a.output:
        with open(a.output, "w", encoding="utf-8") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
