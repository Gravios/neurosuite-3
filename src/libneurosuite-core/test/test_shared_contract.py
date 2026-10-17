#!/usr/bin/env python3
"""
test_shared_contract.py — neurosuite-3 and fiber-kit agree on what they share.

custody_vectors.tsv, pca_method_vectors.tsv, sdiff_expr_vectors.tsv,
stderiv_vectors.tsv and sdiff_grammar_vectors.tsv are shipped in BOTH
repositories on purpose: each is one table run by every language mirror, so
the implementations cannot drift apart.  So are the template-library reference
files in golden/ (.wti/.wtf/.wtl/.eap/.tcl written by each side's writers and
read by the other's readers).  Each repository also keeps a
contract.yaml at its root (plugin protocol, session schema, the versions of the
version-tagged formats, and the hashes of the shared tables).

Always checked, with this repository alone:
  - every shared table is present, and its MD5 is the one contract.yaml
    records (so a table cannot change without the contract saying so).

With FK_ROOT pointing at a fiber-kit checkout, also:
  - each table is byte-identical to fiber-kit's copy;
  - the two contract.yaml files agree: the same contract number, plugin
    protocol, session schema and table hashes, and for every format both
    list, each side reads the version the other writes.

    FK_ROOT=/path/to/fiber-kit python3 src/libneurosuite-core/test/test_shared_contract.py

Under ctest it is core_shared_contract; -DNS_FIBER_KIT_ROOT=/path/to/fiber-kit
sets FK_ROOT.  fiber-kit carries the mirror-image script (NS3_ROOT).
contract.yaml is kept flat and read here without a YAML library.
"""
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

# file in this repo -> its path relative to the fiber-kit checkout
SHARED = {
    "custody_vectors.tsv":     "test/custody_vectors.tsv",
    "pca_method_vectors.tsv":  "test/pca_method_vectors.tsv",
    "sdiff_expr_vectors.tsv":  "test/sdiff_expr_vectors.tsv",
    "stderiv_vectors.tsv":     "test/stderiv_vectors.tsv",
    "sdiff_grammar_vectors.tsv": "test/sdiff_grammar_vectors.tsv",
}
# The template-library reference files (golden_formats_test.cpp,
# fiber-kit's test_golden_formats.py).
for _g in ['fk.wti.1', 'fk.wti.2', 'fk.wtl.1', 'golden.eap.1', 'golden.tcl.1', 'golden.wtf.standard.1', 'golden.wti.1', 'golden.wti.2', 'golden.wtl.1']:
    SHARED["golden/" + _g] = "test/golden/" + _g

bad = 0


def report(ok, what):
    global bad
    print(f"  {'ok  ' if ok else 'FAIL'} {what}")
    bad += not ok


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def read_contract(path):
    """contract.yaml -> dict: top-level scalars, 'formats' {name: (read, write)},
    'vectors' {file: md5}.  Only the flat layout the file documents is read."""
    out = {"formats": {}, "vectors": {}}
    section = None
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("#"):
                continue
            m = re.match(r"^([A-Za-z_]+):\s*(.*?)\s*$", line)
            if m:
                section = m.group(1)
                if m.group(2):
                    out[section] = m.group(2).strip("'\"")
                continue
            if section == "formats":
                m = re.match(r"^\s+(\w+):\s*\{\s*read:\s*(\d+)\s*,\s*write:\s*(\d+)\s*\}\s*$", line)
                if m:
                    out["formats"][m.group(1)] = (int(m.group(2)), int(m.group(3)))
            elif section == "vectors":
                m = re.match(r"^\s+(\S+):\s*([0-9a-f]{32})\s*$", line)
                if m:
                    out["vectors"][m.group(1)] = m.group(2)
    return out


def main():
    mine_path = os.path.join(ROOT, "contract.yaml")
    if not os.path.exists(mine_path):
        print(f"FAIL: {mine_path} is missing")
        return 1
    mine = read_contract(mine_path)

    print("Shared tables against this repository's contract.yaml:")
    report(set(mine["vectors"]) == set(SHARED),
           "contract.yaml lists exactly the shared tables")
    for name in sorted(SHARED):
        path = os.path.join(HERE, name)
        if not os.path.exists(path):
            report(False, f"{name}: missing from this repository")
            continue
        h = md5(path)
        report(mine["vectors"].get(name) == h,
               f"{name}: {h}" + ("" if mine["vectors"].get(name) == h
                                 else f" (contract.yaml says {mine['vectors'].get(name)})"))

    fk = os.environ.get("FK_ROOT")
    if not fk:
        print("\nFK_ROOT not set: fiber-kit not compared.  To compare:\n"
              "    FK_ROOT=/path/to/fiber-kit python3 src/libneurosuite-core/test/test_shared_contract.py")
    else:
        print(f"\nAgainst fiber-kit at {fk}:")
        for name, rel in sorted(SHARED.items()):
            theirs = os.path.join(fk, rel)
            if not os.path.exists(theirs):
                report(False, f"{name}: not at {theirs}")
                continue
            mine_file = os.path.join(HERE, name)
            if os.path.exists(mine_file):
                report(md5(mine_file) == md5(theirs), f"{name}: identical")
        theirs_path = os.path.join(fk, "contract.yaml")
        if not os.path.exists(theirs_path):
            report(False, f"fiber-kit has no contract.yaml at {theirs_path}")
        else:
            theirs = read_contract(theirs_path)
            for key in ("contract", "plugin_protocol", "session_schema"):
                report(mine.get(key) is not None and mine.get(key) == theirs.get(key),
                       f"{key}: {mine.get(key)} here, {theirs.get(key)} in fiber-kit")
            report(mine["vectors"] == theirs["vectors"], "the two contracts record the same table hashes")
            for name in sorted(set(mine["formats"]) & set(theirs["formats"])):
                (mr, mw), (tr, tw) = mine["formats"][name], theirs["formats"][name]
                report(tr >= mw, f"{name}: fiber-kit reads up to v{tr}, neurosuite-3 writes v{mw}")
                report(mr >= tw, f"{name}: neurosuite-3 reads up to v{mr}, fiber-kit writes v{tw}")

    print(f"\n{'FAIL' if bad else 'PASS'}: {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
