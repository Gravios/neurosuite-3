#!/usr/bin/env python3
"""After T (move the selected clusters to the end) the parent palette scrolls to them.

The moved clusters keep the selection, but their ids are now the highest and they sit at the tail of the
list.  The list stayed where it was -- or, once the post-edit renumber had rebuilt it, jumped to the top --
so the selection was out of view whenever the palette held more clusters than it shows.

120 clusters; `select`, `moveToEnd` and `palette` (which reports the ids inside the list's viewport), with
the post-edit automation on (the default) and with all of it off:

  - one cluster, three adjacent ones, and two far apart are each the last ids afterwards, still selected;
  - every one of them is inside the viewport;
  - before the move the viewport did not show the tail (the test would pass trivially otherwise).
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


def build(d, n=120):
    base, m = S.make_session(d, "mv", n_per_unit=600)
    clu = (2 + np.arange(m["clu"].size) * n // m["clu"].size).astype(np.int32)
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([int(clu.max()) + 1], np.int32).tofile(fh)
        clu.tofile(fh)
    return base


def main():
    klusters = R.binary()
    R.skip_without_binary()
    for prefs in ({}, {"autoRealign": False, "autoRenumber": False, "autoMatrices": False}):
        label = "automation off" if prefs else "automation on"
        for sel in ([5], [4, 5, 6], [3, 40]):
            with tempfile.TemporaryDirectory() as d:
                base = build(d)
                rep = R.run(klusters, f"{base}.clu.standard.1",
                            [{"select": sel}, {"palette": True}, {"moveToEnd": True}, {"palette": True}], d, **prefs)
                pal = [r for r in rep.get("results", []) if "order" in r]
                if len(pal) < 2:
                    check(False, f"{label}: {sel}: the run produced two palette reports", rep.get("stderr", "")[-400:])
                    continue
                before, after = pal
                moved = sorted(after["selected"])
                check(len(moved) == len(sel) and moved == after["order"][-len(sel):],
                      f"{label}: {sel} moved: still selected, and the last {len(sel)} of the list", f"selected {moved}, tail {after['order'][-4:]}")
                check(before["order"][-1] not in before["visible"], f"{label}: {sel}: the tail was out of view before the move")
                check(all(i in after["visible"] for i in moved), f"{label}: {sel}: the moved clusters are in view",
                      f"visible {after['visible'][:1]}..{after['visible'][-1:]}, moved {moved}")
    print(f"test_move_to_end_follows: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
