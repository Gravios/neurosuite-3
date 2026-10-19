#!/usr/bin/env python3
"""The scripts that run KiloKlustaKwik pass only options it has.

KiloKlustaKwik's option parser (src/kiloklustakwik/src/param.c) looks each
registered name up on the command line and ignores everything else, so a
misspelt, renamed or removed option is dropped without a word.  This test
takes the registered names from the sources (INT_PARAM(Name), FLOAT_PARAM,
BOOLEAN_PARAM, STRING_PARAM, plus "help") and checks, for every script that
runs the program:

  - each -Option in its KiloKlustaKwik command line is registered (names are
    case-sensitive: -nStarts, -fSaveModel);
  - it runs KiloKlustaKwik (or KiloKlustaKwik_cpu), not KlustaKwik, which
    neurosuite-3 does not install;
  - it passes -Method, so KiloKlustaKwik reads and writes the method-tagged
    files (.fet.<method>.N, .clu.<method>.N) the pipeline uses.

Exits nonzero on failure (repo test convention).
"""
from __future__ import annotations

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
KK_SRC = os.path.join(ROOT, "src", "kiloklustakwik", "src")
SCRIPTS = os.path.join(ROOT, "src", "ndmanager-plugins", "scripts")

# Scripts that run KiloKlustaKwik.  A script that starts running it must be
# added here; the "no other script runs it" check below catches one that is not.
CALLERS = [
    os.path.join(SCRIPTS, "ndm_klustakwik"),
    os.path.join(SCRIPTS, "ndm_subcluster_unmatched"),
    os.path.join(SCRIPTS, "ndm_applydrift"),
    os.path.join(ROOT, "src", "kiloklustakwik", "KlustaKwikRefine"),
]

PARAM = re.compile(r"\b(?:INT|FLOAT|BOOLEAN|STRING)_PARAM\(\s*(\w+)\s*\)")
# A command line that runs the program: the binary at command position, or
# KlustaKwikRefine's cmd=("$KK_BIN" ...) array.
START = re.compile(r'^\s*(?:(?:KiloKlustaKwik(?:_cpu)?|KlustaKwik)\s|cmd\+?=\()')
BARE_KK = re.compile(r'^\s*(?:KlustaKwik\s|check_commands_installed\b.*\bKlustaKwik\b)')
OPTION = re.compile(r"""^["']?-([A-Za-z]\w*)["']?$""")


def registered() -> set[str]:
    names = {"help"}
    for path in glob.glob(os.path.join(KK_SRC, "*.c*")) + glob.glob(os.path.join(KK_SRC, "*.h")):
        with open(path, errors="replace") as fh:
            names |= set(PARAM.findall(fh.read()))
    return names


def command_lines(path: str) -> list[tuple[int, list[str]]]:
    """[(line number, tokens)] of each KiloKlustaKwik command in the script,
    continuation lines (trailing backslash) and cmd=( ... ) arrays joined."""
    out = []
    lines = open(path, errors="replace").read().split("\n")
    i = 0
    while i < len(lines):
        ln = lines[i]
        if ln.lstrip().startswith("#") or not START.match(ln):
            i += 1
            continue
        start, text = i + 1, ""
        is_array = re.match(r"^\s*cmd\+?=\(", ln) is not None
        while i < len(lines):
            cur = lines[i].split(" #")[0]
            text += " " + cur.rstrip().rstrip("\\")
            if is_array:
                if ")" in cur:
                    break
            elif not cur.rstrip().endswith("\\"):
                break
            i += 1
        out.append((start, text.split()))
        i += 1
    return out


def main() -> int:
    fails = 0

    def check(name, cond, detail=""):
        nonlocal fails
        print(f"  [{'PASS' if cond else 'FAIL'}] {name}{'' if cond else ': ' + detail}")
        if not cond:
            fails += 1

    known = registered()
    check("KiloKlustaKwik's registered options were found", len(known) > 100, f"{len(known)}")

    for path in CALLERS:
        rel = os.path.relpath(path, ROOT)
        cmds = command_lines(path)
        check(f"{rel}: runs KiloKlustaKwik", bool(cmds))
        for line, tokens in cmds:
            unknown = sorted({m.group(1) for t in tokens if (m := OPTION.match(t))} - known)
            check(f"{rel}:{line}: every option is one KiloKlustaKwik has",
                  not unknown, ", ".join("-" + u for u in unknown))
            if tokens and not tokens[0].startswith("cmd"):
                check(f"{rel}:{line}: the program is KiloKlustaKwik",
                      tokens[0].startswith("KiloKlustaKwik"), tokens[0])
        has_method = any("-Method" in tokens for _, tokens in cmds if tokens)
        check(f"{rel}: passes -Method", has_method)

    callers = {os.path.realpath(p) for p in CALLERS}
    others = []
    for path in sorted(glob.glob(os.path.join(SCRIPTS, "*"))):
        if os.path.realpath(path) in callers or os.path.isdir(path) or "." in os.path.basename(path):
            continue
        text = open(path, errors="replace").read()
        if any(START.match(l) or BARE_KK.match(l) for l in text.split("\n")
               if not l.lstrip().startswith("#")):
            others.append(os.path.basename(path))
    check("no other script runs KiloKlustaKwik or KlustaKwik", not others, ", ".join(others))
    for path in CALLERS:
        text = open(path, errors="replace").read()
        bare = [n + 1 for n, l in enumerate(text.split("\n"))
                if not l.lstrip().startswith("#") and BARE_KK.match(l)]
        check(f"{os.path.relpath(path, ROOT)}: no KlustaKwik program", not bare,
              "lines " + ", ".join(map(str, bare)))

    print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
