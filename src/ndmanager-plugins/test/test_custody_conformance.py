"""Python-side runner for the shared chain-of-custody conformance vectors.

Executes the SAME custody_vectors.tsv that the C++ test
(libneurosuite-core/test/custody_conformance_test.cpp) runs, against the Python
mirror ndm_resolve_io.  One table, multiple runners, so the implementations
cannot drift.

Run from anywhere:
    python3 src/ndmanager-plugins/test/test_custody_conformance.py
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.normpath(os.path.join(HERE, "..", "scripts"))
VECTORS = os.path.normpath(os.path.join(
    HERE, "..", "..", "libneurosuite-core", "test", "custody_vectors.tsv"))
sys.path.insert(0, SCRIPTS)

import ndm_resolve_io as cst  # noqa: E402


def run_vectors(vpath):
    ran = fail = 0
    with tempfile.TemporaryDirectory() as d:
        base = os.path.join(d, "sess")
        with open(vpath) as fh:
            for raw in fh:
                line = raw.rstrip("\n")
                if not line or line.startswith("#"):
                    continue
                f = line.split("\t")
                kind = f[0]
                ran += 1
                if kind == "classify":
                    got = cst.classify(f[1])
                    ok = got == f[2]
                    desc = "classify {} -> {} (got {})".format(f[1], f[2], got)
                elif kind == "method_of":
                    got = cst.method_of(f[1])
                    ok = got == f[2]
                    desc = "method_of {} -> '{}' (got '{}')".format(f[1], f[2], got)
                elif kind == "parse_anchor":
                    a = cst.parse_anchor(f[1])
                    want_ok = f[7] == "1"
                    ok = a.ok == want_ok
                    if want_ok:
                        ok = (ok and a.base == f[2] and a.type == f[3]
                              and a.method == f[4] and str(a.group) == f[5]
                              and a.suffix == f[6])
                    desc = "parse_anchor {}".format(f[1])
                elif kind == "resolve":
                    existing = [s for s in f[1].split(",") if s]
                    for suf in existing:
                        open(base + "." + suf, "w").close()
                    try:
                        r = cst.resolve(base, f[2], int(f[3]), f[4])
                        want_found = f[6] == "1"
                        ok = (os.path.basename(r.path) == f[5]
                              and r.found == want_found)
                        desc = "resolve {}/{}/{} -> {} (found={})".format(
                            f[2], f[3], f[4], f[5], f[6])
                    finally:
                        for suf in existing:
                            p = base + "." + suf
                            if os.path.exists(p):
                                os.remove(p)
                elif kind == "method_token":
                    ms = cst.parse_method_token(f[1])
                    kind_str = ms.kind or ""
                    order_str = "" if ms.order is None else str(ms.order)
                    ok = (ms.family == f[2] and kind_str == f[3]
                          and order_str == f[4])
                    desc = "method_token {} -> {}".format(f[1], ms)
                elif kind == "is_stderiv":
                    got = cst.is_stderiv_method(f[1])
                    ok = got == (f[2] == "1")
                    desc = "is_stderiv {} -> {} (got {})".format(f[1], f[2], got)
                else:
                    ok = False
                    desc = "unknown vector kind '{}'".format(kind)

                if not ok:
                    print("FAIL: " + desc)
                    fail += 1
    return ran, fail


def main():
    if not os.path.isfile(VECTORS):
        print("FAIL: cannot find vectors at " + VECTORS)
        return 2
    ran, fail = run_vectors(VECTORS)
    print("custody conformance (python): {} checks, {} failed".format(ran, fail))
    if fail == 0:
        print("ALL CUSTODY CONFORMANCE TESTS PASS")
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
