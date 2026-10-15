#!/usr/bin/env python3
"""templates/template.yaml lists every parameter the ndm_* scripts read.

The scripts are the ground truth: a parameter is any name a script looks up
in a program block, through read_script_parameter (its own block, `-s
other` for another block, `-a other` for a fallback block), read_kk_param
(ndm_klustakwik's block, or the caller's with -b), ndm_process_spikes
(ndm_detectspikes / ndm_extractspikes, in ndm_functions), ndm_input_extension
(inputExtension) or a direct
yaml_read of a //program/…/parameter path.

Checks, for every program block:
  - each parameter the scripts read from it is in the template, either as an
    entry or as a commented-out `# - name:` entry.  Parameters a script only
    reads with a fallback block (-a) are exempt: leaving them out is how the
    fallback is used;
  - every template entry is read by some script (no stale entries), except
    the KiloKlustaKwik parameters a caller passes on through read_kk_param -b,
    and external plugins that describe themselves (ndm-plugin protocol).
  - every ndm_* script that reads parameters has a program block.

--report prints the full table instead (the defaults found in the scripts
included).  Exits nonzero on failure (repo test convention).
"""
from __future__ import annotations

import glob
import os
import re
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SCRIPTS = os.path.join(ROOT, "src", "ndmanager-plugins", "scripts")
TEMPLATE = os.path.join(ROOT, "templates", "template.yaml")

# Plugins that describe themselves at runtime (`--ndm-describe`); their
# parameters are not in this repository's scripts.
EXTERNAL = {"ndm_fiber-kit"}

# Parameters ndm_process_spikes (ndm_functions) reads under $program.
PROCESS_SPIKES = {
    "ndm_detectspikes": ["method", "methodOrder", "inputExtension", "thresholdFactor",
                         "refractoryPeriod", "peakSearchLength", "start", "duration",
                         "thresholdNoise", "recenterMode", "recenterHalfWidth", "detectArms",
                         "flatRunMax", "dedupSamples", "memCapGB"],
    "ndm_extractspikes": ["method", "methodOrder", "inputExtension", "memCapGB",
                          "flatRunMax", "dedupSamples"],
}

RSP = re.compile(r'(?:(\w+)=\$\(\s*)?read_script_parameter\s+([^)|;`]*)')
KK = re.compile(r'(?:(\w+)=\$\(\s*)?read_kk_param\s+([^)|;`]*)')
XP = re.compile(r"program/n(?:ame)?\[\.='([^']+)'\]/\.\./parameters/parameter/n(?:ame)?\[\.='([^']+)'\]")


def logical_lines(text: str) -> list[str]:
    out, buf = [], ""
    for ln in text.splitlines():
        if ln.rstrip().endswith("\\"):
            buf += ln.rstrip()[:-1] + " "
        else:
            out.append(buf + ln)
            buf = ""
    return out


def _tokens(s: str) -> list[str]:
    return [t.strip("\"'") for t in s.split()]


def _default(var: str | None, text: str) -> str | None:
    if not var:
        return None
    pats = [rf'check_optional_variable\s+"?\${{?{var}}}?"?\s*\|\|\s*{var}=("[^"]*"|\'[^\']*\'|[^\s;]+)',
            rf'\[\s*-z\s+"?\${{?{var}}}?"?\s*\]\s*&&\s*{var}=("[^"]*"|\'[^\']*\'|[^\s;]+)',
            rf'{var}=\${{{var}:-([^}}]*)}}', rf'{var}=\${{{var}:=([^}}]*)}}']
    for p in pats:
        m = re.search(p, text)
        if m:
            return m.group(1).strip("\"'")
    return None


def script_files() -> list[str]:
    out = []
    for path in sorted(glob.glob(os.path.join(SCRIPTS, "ndm_*"))):
        base = os.path.basename(path)
        if base in ("ndm_functions", "ndm_custody") or "." in base:
            continue
        with open(path, errors="replace") as fh:
            if fh.read(2) == "#!":
                out.append(path)
    return out


def script_parameters() -> dict:
    """{block: {param: {"default", "readers": set, "own": bool}}}.

    own = read by some script without a fallback block (so the template must
    list it)."""
    blocks: dict = {}

    def add(block, name, reader, default, own):
        e = blocks.setdefault(block, {}).setdefault(
            name, {"default": None, "readers": set(), "own": False})
        e["readers"].add(reader)
        e["own"] |= own
        if e["default"] is None and default not in (None, ""):
            e["default"] = default

    for path in script_files():
        prog = os.path.basename(path)
        text = open(path, errors="replace").read()
        for ln in logical_lines(text):
            if ln.lstrip().startswith("#"):
                continue
            for m in RSP.finditer(ln):
                var, args = m.group(1), _tokens(m.group(2))
                block, fallback = prog, None
                while args and args[0] in ("-s", "-a") and len(args) > 1:
                    if args[0] == "-s":
                        block = args[1]
                    else:
                        fallback = args[1]
                    args = args[2:]
                names = [a for a in args[1:] if re.fullmatch(r"[A-Za-z][\w.]*", a)
                         and not a.startswith("ndm_")]
                if not names:
                    continue
                add(block, names[0], prog, _default(var, text), own=fallback is None)
                for alt in names[1:]:          # legacy names: accepted, not required
                    add(block, alt, prog, None, own=False)
                if fallback:
                    add(fallback, names[0], prog, None, own=False)
            for m in KK.finditer(ln):
                var, args = m.group(1), _tokens(m.group(2))
                caller = None
                if args[:1] == ["-b"] and len(args) > 1:
                    caller, args = args[1], args[2:]
                if len(args) >= 2 and re.fullmatch(r"[A-Za-z]\w*", args[1]):
                    add("ndm_klustakwik", args[1], prog, args[2] if len(args) > 2 else None, own=True)
                    if caller and caller != "ndm_klustakwik":
                        add(caller, args[1], prog, None, own=False)
            if re.match(r"\s*ndm_input_extension\b", ln):   # reads inputExtension under $program
                add(prog, "inputExtension", prog, ln.split()[1] if len(ln.split()) > 1 else None, own=True)
            for m in XP.finditer(ln):
                var = re.match(r"\s*(\w+)=", ln)
                add(m.group(1), m.group(2), prog, _default(var.group(1) if var else None, text), own=True)
    for block, names in PROCESS_SPIKES.items():
        for n in names:
            add(block, n, block, None, own=True)
    return blocks


def template_parameters() -> tuple[dict, dict]:
    """({program: {param: value}} for entries, {program: set} commented-out)."""
    text = open(TEMPLATE).read()
    doc = yaml.safe_load(text) or {}
    active = {}
    for prg in doc.get("programs") or []:
        active[prg["name"]] = {p["name"]: p.get("value") for p in prg.get("parameters") or []}
    commented, cur = {}, None
    for ln in text.splitlines():
        m = re.match(r"^  - name: (\S+)", ln)
        if m:
            cur = m.group(1)
            continue
        m = re.match(r"^\s*#\s*-\s*name:\s*(\S+)", ln)
        if m and cur:
            commented.setdefault(cur, set()).add(m.group(1))
    return active, commented


def main(argv: list[str]) -> int:
    code = script_parameters()
    active, commented = template_parameters()
    if "--report" in argv:
        for b in sorted(set(code) | set(active)):
            t = set(active.get(b, {})) | commented.get(b, set())
            print(f"== {b}")
            for p, e in sorted(code.get(b, {}).items()):
                mark = "ok " if p in t else ("-- " if e["own"] else ".. ")
                print(f"   {mark}{p:28s} default={e['default']!s:14s} own={e['own']} by={','.join(sorted(e['readers']))}")
            for p in sorted(t - set(code.get(b, {}))):
                print(f"   ?? {p} (template only)")
        return 0

    fails = 0

    def check(name, cond, detail=""):
        nonlocal fails
        if not cond:
            fails += 1
        print(f"  [{'PASS' if cond else 'FAIL'}] {name}{'' if cond else ': ' + detail}")

    for b in sorted(code):
        need = {p for p, e in code[b].items() if e["own"]}
        if not need:
            continue
        have = set(active.get(b, {})) | commented.get(b, set())
        check(f"{b}: every parameter listed", need <= have, ", ".join(sorted(need - have)))
    for b in sorted(active):
        if b in EXTERNAL:
            continue
        known = set(code.get(b, {}))
        have = set(active[b]) | commented.get(b, set())
        check(f"{b}: no stale entries", have <= known, ", ".join(sorted(have - known)))
    print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
