#!/usr/bin/env python3
"""After a multi-lasso split the selection is the PIECES and nothing else.

The split carves every lassoed region and each source's remainder out as new clusters, so a source the
lassos were drawn in is emptied and its id goes away.  The landing used to park that id with the pieces;
the auto-renumber that follows hands the number to the next cluster up, and the landing then selected
that neighbour too.  It happened whenever the source was not the last cluster and auto-renumber was on.

Six clusters of 100 spikes (2..7).  Driven through the self-test's `select`, `multiLasso` (lassos given as
spike sets) and `palette` steps, with auto-renumber on (the default) and off:

  - two lassos inside a middle, the first and the last cluster: three pieces selected (30, 30, 40);
  - overlapping lassos: four pieces (20, 20, 20, 40);
  - lassos that consume the cluster: two pieces, no remainder;
  - a lasso across two clusters: the pieces of both, and neither neighbour;
  - in no case is a 100-spike cluster -- an untouched neighbour -- selected.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def build(d):
    base, m = S.make_session(d, "ml", n_per_unit=300)
    clu = m["clu"].copy()
    for unit, ids in ((2, (2, 3, 4)), (3, (5, 6, 7))):
        for part, cid in zip(np.array_split(np.flatnonzero(m["clu"] == unit), 3), ids):
            clu[part] = cid
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([int(clu.max()) + 1], np.int32).tofile(fh)
        clu.astype(np.int32).tofile(fh)
    return base, clu


def of(clu, c, a, b):
    return np.flatnonzero(clu == c)[a:b].tolist()


CASES = [
    ("two lassos in a middle cluster", [4], lambda c: [of(c, 4, 0, 30), of(c, 4, 40, 70)], [30, 30, 40]),
    ("two lassos in the first cluster", [2], lambda c: [of(c, 2, 0, 30), of(c, 2, 40, 70)], [30, 30, 40]),
    ("two lassos in the last cluster", [7], lambda c: [of(c, 7, 0, 30), of(c, 7, 40, 70)], [30, 30, 40]),
    ("overlapping lassos", [5], lambda c: [of(c, 5, 0, 40), of(c, 5, 20, 60)], [20, 20, 20, 40]),
    ("lassos that consume the cluster", [4], lambda c: [of(c, 4, 0, 50), of(c, 4, 50, 100)], [50, 50]),
    ("a lasso across two clusters", [4, 5], lambda c: [of(c, 4, 0, 30) + of(c, 5, 0, 30), of(c, 4, 50, 80)], [30, 40, 60, 70]),
]


def main():
    klusters = R.binary()
    R.skip_without_binary()
    for prefs in ({}, {"autoRenumber": False}):
        label = "auto-renumber off" if prefs else "auto-renumber on"
        for name, sel, lassos, want in CASES:
            with tempfile.TemporaryDirectory() as d:
                base, clu = build(d)
                rep = R.run(klusters, f"{base}.clu.standard.1",
                            [{"select": sel}, {"multiLasso": lassos(clu)}, {"palette": True}], d, **prefs)
                pal = [r for r in rep.get("results", []) if "order" in r]
                if not pal:
                    check(False, f"{label}: {name}: the run produced a palette report", rep.get("stderr", "")[-400:])
                    continue
                sizes = sorted(pal[0]["counts"][str(i)] for i in pal[0]["selected"])
                check(sizes == want, f"{label}: {name}: the selection is the {len(want)} pieces {want}",
                      f"selected {sorted(pal[0]['selected'])} with sizes {sizes}, palette {pal[0]['order']}")
    print(f"test_multilasso_landing: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
