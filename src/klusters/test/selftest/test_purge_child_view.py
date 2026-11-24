#!/usr/bin/env python3
"""Purge Small Clusters follows the view it is run from.

In the child view the purge collected the small children of EVERY parent and handed their ids to
the parent-layer delete, which knows none of them: nothing was purged, and Klusters crashed.

  - child view (parent 2 curated, one of its children shown): only parent 2's children below the
    threshold go to noise; parent 3's small children stay; one undo step; Undo puts them back;
  - parent view: the parents below the threshold go, as before; no child is touched by id.

Parent 2 has the children 20000 (370 spikes) and 20001 (30); parent 3 has 20004 (310), 20005 (30)
and 20006 (60).
"""
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import session as S                # noqa: E402
import test_undo_pairing as U      # noqa: E402

fails = 0
H = lambda **k: {"hierarchy": k}          # noqa: E731
CP = lambda **k: {"childPalette": k}      # noqa: E731


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def hier(d):
    base, m = S.make_session(d, "p", n_per_unit=400, method=U.M)
    clu = m["clu"]
    child = clu.copy()
    two, three = np.flatnonzero(clu == 2), np.flatnonzero(clu == 3)
    child[two] = 20000
    child[two[:30]] = 20001
    child[three] = 20004
    child[three[:30]] = 20005
    child[three[30:90]] = 20006
    clp = np.zeros(int(child.max()), np.int32)
    for c in np.unique(child):
        clp[c - 1] = clu[child == c][0]
    for kind, ids, hdr in (("clc", child, np.unique(child).size), ("clp", clp, clp.size)):
        with open(f"{base}.{kind}.{U.M}.1", "wb") as fh:
            np.array([hdr], np.int32).tofile(fh)
            ids.astype(np.int32).tofile(fh)
    return base


def kids(h):
    return {int(c): p for c, p in h["childParents"].items() if int(c) > 1}


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base = hier(d)
        rep = R.run(klusters, f"{base}.clu.{U.M}.1",
                    [H(), {"select": [2]}, CP(show=True), CP(click=20000), {"purge": 50}, H(), H(undo=True)],
                    d, timeout=600, autoRenumber=False, autoRealign=False)
        hs = [r for r in rep.get("results", []) if "childParents" in r]
        p = next((r for r in rep.get("results", []) if "purge" in r.get("step", {})), None)
        check(rep.get("rc") == 0 and p is not None and len(hs) == 3, "child view: the purge runs to its end (it crashed)",
              rep.get("stderr", "")[-400:])
        if p is not None and len(hs) == 3:
            check(p["childScopeBefore"] and p["purged"] == [20001], "child view: only the curated parent's small child is purged", p)
            check(kids(hs[1]) == {20000: 2, 20001: 1, 20004: 3, 20005: 3, 20006: 3} and hs[1]["parentSizes"] == {"1": 30, "2": 370, "3": 400},
                  "its spikes are in the noise cluster; parent 3 and its small children are untouched", (kids(hs[1]), hs[1]["parentSizes"]))
            check(p["undoSteps"] == 1 and kids(hs[2]) == kids(hs[0]) and hs[2]["parentSizes"] == hs[0]["parentSizes"],
                  "one undo step, and Undo puts the child back")
    with tempfile.TemporaryDirectory() as d:
        base = hier(d)
        rep = R.run(klusters, f"{base}.clu.{U.M}.1", [H(), {"select": [2]}, {"purge": 50}, H(), {"purge": 401}, H()],
                    d, timeout=600, autoRenumber=False, autoRealign=False)
        hs = [r for r in rep.get("results", []) if "childParents" in r]
        ps = [r for r in rep.get("results", []) if "purge" in r.get("step", {})]
        check(len(ps) == 2 and len(hs) == 3 and ps[0]["purged"] == [] and hs[1]["childSizes"] == hs[0]["childSizes"],
              "parent view: no parent is below 50 spikes, and no small CHILD is purged in its place", ps[:1])
        check(len(ps) == 2 and ps[1]["purged"] == [2, 3] and len(hs) == 3 and set(hs[2]["parentSizes"]) == {"1"},
              "parent view: the parents below the threshold go to noise", (ps[1:], hs[2]["parentSizes"] if len(hs) == 3 else None))
    print(f"test_purge_child_view: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
