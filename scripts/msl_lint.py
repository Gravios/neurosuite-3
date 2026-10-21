#!/usr/bin/env python3
"""msl_lint.py -- static checks on the Metal kernels (*.metal) that only Apple's
run-time compiler would otherwise catch, so they fail on Linux, in scripts/lint.sh.

For every `kernel` function:

1. The position and size attributes (thread_position_in_grid,
   threadgroup_position_in_grid, thread_position_in_threadgroup,
   threads_per_threadgroup, threadgroups_per_grid, threads_per_grid,
   dispatch_threads_per_threadgroup) are all scalars or all vectors of one
   width.  Apple's compiler refuses a mix ("expecting input declarations with
   either all scalar types or all vector types with the same number of
   elements"); the index attributes (thread_index_in_threadgroup, ...) are
   always scalar and exempt.
2. No two parameters share a [[buffer(n)]] or a [[threadgroup(n)]] index.
3. Every `threadgroup` pointer parameter carries [[threadgroup(n)]].

Usage:  msl_lint.py FILE.metal...      (exit 1 on any finding)
"""
import re
import sys

POSITION_ATTRS = {
    "thread_position_in_grid", "threadgroup_position_in_grid",
    "thread_position_in_threadgroup", "threads_per_threadgroup",
    "threadgroups_per_grid", "threads_per_grid", "dispatch_threads_per_threadgroup",
}
WIDTH = {"uint": 1, "ushort": 1, "uint2": 2, "ushort2": 2, "uint3": 3, "ushort3": 3}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def kernels(text):
    """(name, line, [param strings]) for every `kernel ... name(params)`."""
    for m in re.finditer(r"\bkernel\s+void\s+(\w+)\s*\(", text):
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
        body = text[m.end():i - 1]
        params, depth, cur = [], 0, ""
        for ch in body:                       # split on top-level commas
            if ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            if ch == "," and depth == 0:
                params.append(cur.strip())
                cur = ""
            else:
                cur += ch
        if cur.strip():
            params.append(cur.strip())
        yield m.group(1), text.count("\n", 0, m.start()) + 1, params


def check(path):
    problems = []
    text = strip_comments(open(path, encoding="utf-8").read())
    for name, line, params in kernels(text):
        where = f"{path}:{line}: kernel {name}"
        widths, bufs, tgs = {}, {}, {}
        for p in params:
            attrs = re.findall(r"\[\[\s*(\w+)(?:\s*\(\s*(\d+)\s*\))?\s*\]\]", p)
            decl = re.sub(r"\[\[.*?\]\]", "", p).split()
            for attr, idx in attrs:
                if attr in POSITION_ATTRS:
                    t = decl[-2] if len(decl) >= 2 else "?"
                    widths[f"{t} {decl[-1] if decl else '?'} [[{attr}]]"] = WIDTH.get(t, 0)
                elif attr == "buffer":
                    if idx in bufs:
                        problems.append(f"{where}: [[buffer({idx})]] used by '{bufs[idx]}' and '{p}'")
                    bufs[idx] = p
                elif attr == "threadgroup":
                    if idx in tgs:
                        problems.append(f"{where}: [[threadgroup({idx})]] used twice")
                    tgs[idx] = p
            if re.match(r"threadgroup\b", p) and "*" in p and not any(a == "threadgroup" for a, _ in attrs):
                problems.append(f"{where}: threadgroup pointer '{p}' has no [[threadgroup(n)]]")
        if len(set(widths.values())) > 1:
            listing = "; ".join(widths)
            problems.append(f"{where}: position/size attributes are not all of one vector width "
                            f"(Apple's compiler refuses this): {listing}")
    return problems


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    problems = [p for path in argv[1:] for p in check(path)]
    for p in problems:
        print(p)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
