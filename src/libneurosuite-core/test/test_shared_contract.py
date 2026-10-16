#!/usr/bin/env python3
"""
test_shared_contract.py — the cross-repo shared files must be byte-identical.

custody_vectors.tsv, pca_method_vectors.tsv, sdiff_expr_vectors.tsv and
stderiv_vectors.tsv are
shipped in BOTH neurosuite-3 and fiber-kit on purpose: each is one table run by
every language mirror, so the implementations cannot drift apart.  That only
holds while the two copies are the same file, so changing a vector file is a
two-repo change: this test fails until both sides carry it.

Point FK_ROOT at a fiber-kit checkout and the two copies are compared:

    FK_ROOT=/path/to/fiber-kit python3 src/libneurosuite-core/test/test_shared_contract.py

A file missing on either side is a failure.  Without FK_ROOT there is nothing
to compare against: the hashes are listed and the test reports SKIPPED.

Under ctest it is registered as core_shared_contract when the build is
configured with -DNS_FIBER_KIT_ROOT=/path/to/fiber-kit.  fiber-kit carries the
mirror-image script (NS3_ROOT).
"""
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# file in this repo -> its path relative to the fiber-kit checkout
SHARED = {
    "custody_vectors.tsv":     "test/custody_vectors.tsv",
    "pca_method_vectors.tsv":  "test/pca_method_vectors.tsv",
    "sdiff_expr_vectors.tsv":  "test/sdiff_expr_vectors.tsv",
    "stderiv_vectors.tsv":     "test/stderiv_vectors.tsv",
}


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ns3 = os.environ.get("FK_ROOT")
    bad = 0
    for name, rel in sorted(SHARED.items()):
        mine = os.path.join(HERE, name)
        if not os.path.exists(mine):
            print(f"  FAIL {name}: missing from this repo")
            bad += 1
            continue
        h = md5(mine)
        if ns3:
            theirs = os.path.join(ns3, rel)
            if not os.path.exists(theirs):
                print(f"  FAIL {name}: not at {theirs}")
                bad += 1
                continue
            t = md5(theirs)
            ok = h == t
            print(f"  {'ok  ' if ok else 'FAIL'} {name}: {h[:12]} vs {t[:12]} "
                  f"({'identical' if ok else 'DIVERGED'})")
            bad += not ok
        else:
            print(f"  {name}: {h}")

    if not ns3:
        print("\nFK_ROOT not set, so this listed the hashes rather than comparing.\n"
              "Set it to a fiber-kit checkout to verify the two repos agree:\n"
              "    FK_ROOT=/path/to/fiber-kit python3 src/libneurosuite-core/test/test_shared_contract.py\n"
              "The paths on the other side are:")
        for name, rel in sorted(SHARED.items()):
            print(f"    {name:26s} -> {rel}")
        if not bad:
            print("\nSKIPPED: nothing compared (FK_ROOT not set)")
            return 0

    print(f"\n{'FAIL' if bad else 'PASS'}: {bad} problem(s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
