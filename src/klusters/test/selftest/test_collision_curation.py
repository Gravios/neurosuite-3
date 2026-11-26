#!/usr/bin/env python3
"""The last look at marked collision rows: beside their cluster as a child, and taken back.

The session of test_mark_collisions with a child layer (cluster 2's 40 planted rows are one child,
its other rows another; cluster 3 is one child).  Through the `collisionChild` and
`unmarkCollisions` steps:

  - Clusters > Collision Rows to a Child, on cluster 2 after Mark Collisions: the marked rows --
    which sat in BOTH of cluster 2's children (the planted ones in one, the rows where the two
    units coincide by themselves in the other) -- become one new child of cluster 2, of exactly
    that many rows; cluster 2 holds its 400 spikes as before and cluster 3 is untouched;
  - run again, nothing is made: the rows are a child of their own already;
  - NEGATIVE CONTROL: before any mark the selection names no partner and no child is made;
  - Clusters > Remove Collision Marks on that new CHILD (child scope): its rows name their own
    class only, none is corrected any more, each is served as the .spk holds it, and the document
    is modified;
  - a mark made and removed in one session leaves, at Save, an .eap in which no row names two
    classes and every row of cluster 2 still names its own;
  - marks that were SAVED are removed too: marked and saved in one run, removed and saved in the
    next -- the saved .eap names no partner, and opened again no row is corrected;
  - removing the marks of cluster 3's rows leaves cluster 2's marks alone.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as R                           # noqa: E402
import test_mark_collisions as M          # noqa: E402

ABSENT = M.ABSENT
check = M.check


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "cur", children=True)
        ask = [int(i) for i in plan[2][0]]
        steps = [{"collisionChild": {"clusters": [2]}},
                 {"markCollisions": {"clusters": [2], "apply": True}},
                 {"hierarchy": {}},
                 {"collisionChild": {"clusters": [2]}},
                 {"hierarchy": {}},
                 {"collisionChild": {"clusters": [2]}},
                 {"collisionRows": {"rows": ask}},
                 {"unmarkCollisions": {"clusters": [], "children": True, "childOfRow": ask[0]}},
                 {"collisionRows": {"rows": ask}},
                 {"save": True},
                 {"collisionRows": {}}]
        ok, st = M.run(klusters, base, steps, d)
        check(ok, "the session opened and every step ran")
        if not ok:
            return 1
        check(st[0].get("rows") == [] and st[0].get("created") == [], "NEGATIVE CONTROL: before any mark no row names a partner and no child is made",
              str(st[0].get("summary")))
        marked = sorted(h["row"] for h in st[1].get("hits", []))
        kids_before = {int(k): v for k, v in st[2]["childParents"].items() if v == 2}
        made = st[3].get("created", [])
        print(f"      {len(marked)} rows marked in cluster 2, in {len(kids_before)} children; "
              f"Collision Rows to a Child: {st[3].get('summary')}")
        check(st[3].get("rows") == marked, "the rows of the selection that name a partner are the marked ones")
        check(len(made) == 1 and st[3]["createdParent"].get(str(made[0])) == 2 and st[3]["createdSize"].get(str(made[0])) == len(marked),
              "they become ONE new child of cluster 2, of exactly that many rows",
              str({k: st[3].get(k) for k in ("created", "createdParent", "createdSize")}))
        check(st[4]["parentSizes"].get("2") == 400 and st[4]["parentSizes"].get("3") == st[2]["parentSizes"].get("3"),
              "no row left its cluster: cluster 2 holds 400 spikes, cluster 3 what it held", str(st[4]["parentSizes"]))
        check(len(marked) > 40 and len(kids_before) >= 2, "(the marked rows came from both of cluster 2's children)")
        check(st[5].get("created") == [] and "already" in st[5].get("summary", ""), "run again, nothing is made: they are a child of their own",
              str(st[5].get("summary")))
        check(st[6].get("corrected") == len(marked), "before the marks are removed the rows are corrected")
        un = st[7]
        check(un.get("rows") == marked and un.get("removed") == len(marked) and un.get("modified") is True,
              "Remove Collision Marks on the new child (child scope): every one of its rows, and the document is modified",
              str({k: un.get(k) for k in ("removed", "modified")}))
        recs = st[8].get("records", [])
        check(st[8].get("corrected") == 0 and st[8].get("collisionRows") == 0 and all(r["served"] == r["raw"] for r in recs),
              "no row names a partner or is corrected any more; each is served as the .spk holds it",
              str({k: st[8].get(k) for k in ("collisionRows", "corrected")}))
        check(os.path.exists(f"{base}.eap.1"), "Save wrote the .eap")
        if os.path.exists(f"{base}.eap.1"):
            _, cells = M.read_eap(f"{base}.eap.1")
            present = cells != ABSENT
            check(int((present.sum(1) >= 2).sum()) == 0 and (present[clu == 2].sum(1) == 1).all(),
                  "marked and removed in one session: the saved .eap names no partner, and every row of cluster 2 its own class")
        check(st[10].get("corrected") == 0, "after Save no row is corrected")

    # ── marks that were saved; and one cluster's marks removed, the other's kept ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "sav", children=True)
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}},
                                        {"markCollisions": {"clusters": [3], "apply": True}}, {"save": True}], d)
        check(ok, "marked and saved")
        if not ok:
            return 1
        n2, n3 = st[0].get("marked"), st[1].get("marked")
        _, saved = M.read_eap(f"{base}.eap.1")
        check(int(((saved != ABSENT).sum(1) >= 2).sum()) == n2 + n3, "the saved .eap holds both clusters' marks", f"{n2} + {n3}")
        ok, st = M.run(klusters, base, [{"collisionRows": {}}, {"unmarkCollisions": {"clusters": [3]}}, {"collisionRows": {}},
                                        {"save": True}, {"collisionRows": {}}], d)
        check(ok, "opened again, cluster 3's marks removed, saved")
        if ok:
            check(st[0].get("corrected") == n2 + n3 and st[1].get("removed") == n3 and st[2].get("corrected") == n2,
                  "removing cluster 3's marks leaves cluster 2's: its rows are still corrected",
                  str([st[0].get("corrected"), st[1].get("removed"), st[2].get("corrected")]))
            _, saved = M.read_eap(f"{base}.eap.1")
            two = np.flatnonzero((saved != ABSENT).sum(1) >= 2)
            check(two.size == n2 and (clu[two] == 2).all(), "saved: only cluster 2's rows name a partner", f"{two.size} rows")
            check(st[4].get("corrected") == n2, "... and they are corrected from the saved files")
        ok, st = M.run(klusters, base, [{"unmarkCollisions": {"clusters": [2]}}, {"save": True}, {"collisionRows": {}}], d)
        check(ok, "opened again, cluster 2's saved marks removed, saved")
        if ok:
            _, saved = M.read_eap(f"{base}.eap.1")
            check(st[0].get("removed") == n2 and int(((saved != ABSENT).sum(1) >= 2).sum()) == 0 and st[2].get("corrected") == 0,
                  "marks that were saved are removed too: the .eap names no partner and no row is corrected")

    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
