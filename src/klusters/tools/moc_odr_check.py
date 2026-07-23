#!/usr/bin/env python3
"""
moc_odr_check.py — catch functions declared in a Q_SIGNALS block that ALSO have
an out-of-class definition in a .cpp.

moc emits a definition for every signal, so such a function is defined twice and
the link fails with 'multiple definition'.  Under LTO the reported symbol names
are often misleading, which makes this cheap to misdiagnose.
"""
import re, sys, glob, os

SEC = re.compile(r'^\s*(public|protected|private)(\s+(Q_SLOTS|slots))?\s*:|^\s*(Q_SIGNALS|signals)\s*:')
FUNC = re.compile(r'^\s*(?:virtual\s+)?[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)\s*\(([^;{]*)\)\s*(?:const)?\s*;\s*$')

def arity(params):
    """Parameter count, ignoring commas nested in templates like QMap<int,int>."""
    p = params.strip()
    if not p or p == 'void':
        return 0
    depth, n = 0, 1
    for ch in p:
        if ch in '<([': depth += 1
        elif ch in '>)]': depth -= 1
        elif ch == ',' and depth == 0: n += 1
    return n

CLS = re.compile(r'^\s*class\s+(\w+)\s*(?::\s*(?:public|protected|private)\b|\{)')

def signals_in(path):
    """(name, arity, line, enclosing_class) for every Q_SIGNALS declaration.

    The enclosing class is the most recent class DEFINITION (one with a base
    list or an opening brace) above the line -- a header with several classes,
    which is common here, otherwise attributes a helper widget's signal to the
    main class and reports a conflict that does not exist."""
    out, cur, cls = [], None, None
    for i, line in enumerate(open(path, errors='ignore'), 1):
        c = CLS.match(line)
        if c:
            cls, cur = c.group(1), None      # new class body resets the section
            continue
        m = SEC.match(line)
        if m:
            cur = 'signal' if m.group(4) else 'other'
            continue
        if cur == 'signal':
            f = FUNC.match(line)
            if f: out.append((f.group(1), arity(f.group(2)), i, cls))
    return out

def main(srcdir):
    defs = {}
    for cpp in glob.glob(os.path.join(srcdir, '*.cpp')):
        for i, line in enumerate(open(cpp, errors='ignore'), 1):
            m = re.match(r'^\s*(?:[\w:<>,\s\*&]+\s+)?(\w+)::(\w+)\s*\(([^)]*)\)', line)
            if m and not line.strip().startswith('//'):
                defs.setdefault((m.group(1), m.group(2), arity(m.group(3))), []).append(
                    f"{os.path.basename(cpp)}:{i}")
    bad = 0
    for h in glob.glob(os.path.join(srcdir, '*.h')):
        for name, ar, ln, cls in signals_in(h):
            for (k_cls, k_fn, k_ar), where in defs.items():
                if k_fn == name and k_cls == cls and k_ar == ar:
                    print(f"  ODR: {os.path.basename(h)}:{ln} declares '{name}' in a Q_SIGNALS "
                          f"block, but {k_cls}::{name} is defined at {', '.join(where)}")
                    bad += 1
    print(f"{'FAIL' if bad else 'PASS'}: {bad} signal-with-definition conflict(s)")
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
