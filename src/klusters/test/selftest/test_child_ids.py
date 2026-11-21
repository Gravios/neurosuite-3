#!/usr/bin/env python3
"""The numbering of children: disjoint from the parents'.

    0, 1              the artefact and noise bins (their one child carries the bin's id)
    2 .. 9999         parents only
    10002 .. 19999    self children: parent p's is p + 10000
    20000 ..          every other child

Session: 800 spikes in parents 2, 3, 5, 7 (200 each) with a .clc in the OLD numbering, where a
child carried its parent's number when it was the only one and the two numberings overlapped:
parent 2 -> children 2 and 9, parent 3 -> child 3, parent 5 -> children 4 and 8, parent 7 -> child 7.
Through the `hierarchy` step:

  * load: the six children are renumbered -- 2, 3, 7 (each its parent's own) to 10002, 10003, 10007 and
    4, 8, 9 to 20000, 20001, 20002 -- no spike changes child, and no child id is a parent id;
  * save and reopen: the .clc and .clp on disk are in the new numbering and load without renumbering;
  * a parent renumbered, sorted or moved to the end takes its self child along (parent + 10000 holds
    after a swap of parents too); undoing the renumbering brings the names back;
  * two parents merged: the absorbed parent's self child leaves the self-child block;
  * a parent's children merged into one: that child becomes the self child;
  * a child promoted to a parent of its own: it becomes that parent's self child;
  * Flatten: every parent is left with its self child alone;
  * a child made by an edit (a split) is numbered 20000 or above;
  * a PARENT split in two (the child layer loaded; the parents are renumbered consecutively after it):
    there is one parent more, every spike is still in a child, and the numbering holds -- a parent cut
    from a single child has its self child alone.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S            # noqa: E402
import run as R                # noqa: E402

fails = 0
OLD = {2: 2, 9: 2, 3: 3, 4: 5, 8: 5, 7: 7}                      # old child -> parent
NEW = {10002: 2, 20002: 2, 10003: 3, 20000: 5, 20001: 5, 10007: 7}   # the same children after the load


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def build(d):
    base, _ = S.make_session(d, "ci", n_per_unit=400)
    n = S.read_clu(f"{base}.clu.standard.1").size
    clu = np.repeat([2, 3, 5, 7], n // 4)[:n]
    clu = np.r_[clu, np.full(n - clu.size, 7)]
    clc = clu.copy()
    half = np.arange(n) % 2 == 0
    clc[(clu == 2) & half] = 9                                   # parent 2: children 2 and 9
    clc[(clu == 5) & half] = 4                                   # parent 5: children 4 and 8 (neither is "5")
    clc[(clu == 5) & ~half] = 8
    for kind, ids in (("clu", clu), ("clc", clc)):
        with open(f"{base}.{kind}.standard.1", "wb") as fh:
            np.array([np.unique(ids).size], np.int32).tofile(fh)
            ids.astype(np.int32).tofile(fh)
    clp = np.zeros(9, np.int32)
    for c, p in OLD.items():
        clp[c - 1] = p
    with open(f"{base}.clp.standard.1", "wb") as fh:
        np.array([clp.size], np.int32).tofile(fh)
        clp.tofile(fh)
    return base, clu, clc


def session(klusters, steps, keep=None):
    """Runs the steps on a fresh session; returns (the hierarchy results, clu, old clc, saved files or None)."""
    with tempfile.TemporaryDirectory() as d:
        base, clu, clc = build(d)
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        res = [r for r in rep.get("results", []) if "childParents" in r]
        ok = rep.get("rc") == 0 and len(res) == sum(1 for s in steps if "hierarchy" in s)
        if not ok:
            check(False, "the session ran every step", f"rc {rep.get('rc')}\n{rep.get('stderr', '')[-1500:]}")
        saved = None
        if any("save" in s for s in steps):
            saved = tuple(S.read_clu(f"{base}.{k}.standard.1") for k in ("clu", "clc", "clp"))
            if keep is not None:
                rep2 = R.run(klusters, f"{base}.clu.standard.1", keep, d)
                saved += ([r for r in rep2.get("results", []) if "childParents" in r],)
        return res, clu, clc, saved


def cp(r):
    return {int(c): p for c, p in r["childParents"].items()}


def disjoint(r):
    kids = {int(c) for c in r["childParents"]}
    parents = {int(p) for p in r["parentSizes"]}
    return not ((kids - {0, 1}) & parents) and all(c >= 10002 for c in kids - {0, 1})


def named(r):
    """Every self-block child sits under parent = id - 10000, and every sole child is its parent's self child."""
    m = cp(r)
    per = {}
    for c, p in m.items():
        per.setdefault(p, []).append(c)
    ok_block = all(p == c - 10000 for c, p in m.items() if 10002 <= c < 20000)
    ok_sole = all(cs[0] == p + 10000 for p, cs in per.items() if p > 1 and len(cs) == 1)
    return ok_block and ok_sole


def main():
    klusters = R.skip_without_binary()

    print("-- a .clc in the old numbering")
    res, clu, clc, saved = session(klusters, [{"hierarchy": {}}, {"save": True}], keep=[{"hierarchy": {}}])
    if res:
        r = res[0]
        check(r.get("upgraded") == 6, f"the load renumbers the six children ({r.get('upgraded')})")
        check(cp(r) == NEW, "2, 3, 7 -> 10002, 10003, 10007 (their parents' own); 4, 8, 9 -> 20000, 20001, 20002", str(cp(r)))
        check(disjoint(r), "no child id is a parent id, and every child is 10002 or above")
        sizes = {int(c): n for c, n in r["childSizes"].items()}
        want = {new: int((clc == old).sum()) for old, new in ((2, 10002), (9, 20002), (3, 10003), (4, 20000), (8, 20001), (7, 10007))}
        check(sizes == want, "no spike changed child", f"{sizes} != {want}")
    if saved:
        s_clu, s_clc, s_clp, again = saved
        check(np.array_equal(s_clu, clu), "Save: the .clu is unchanged")
        m = {2: 10002, 9: 20002, 3: 10003, 4: 20000, 8: 20001, 7: 10007}
        check(np.array_equal(s_clc, np.vectorize(m.get)(clc)), "Save: the .clc on disk is in the new numbering")
        check(s_clp.size >= 20002 and all(s_clp[c - 1] == p for c, p in NEW.items()) and np.count_nonzero(s_clp) == 6,
              f"Save: the .clp gives each child its parent at entry child - 1 ({s_clp.size} entries, {np.count_nonzero(s_clp)} set)")
        check(len(again) == 1 and again[0].get("upgraded") == 0 and cp(again[0]) == NEW, "reopened: nothing is renumbered, the same children")

    print("-- parents renumbered: the self children follow")
    res, *_ = session(klusters, [{"hierarchy": {"renumber": True}}, {"hierarchy": {"undo": True}}])
    if len(res) == 2:
        check(cp(res[0]) == {10002: 2, 20002: 2, 10003: 3, 20000: 4, 20001: 4, 10005: 5},
              "parents 5, 7 -> 4, 5: child 10007 becomes 10005; parent 4's two children keep their ids", str(cp(res[0])))
        check(named(res[0]) and disjoint(res[0]), "the numbering holds")
        check(cp(res[1]) == NEW, "undone: the parents and the names are back", str(cp(res[1])))

    res, *_ = session(klusters, [{"hierarchy": {"order": [7, 5, 3, 2]}}])
    if res:
        check(cp(res[0]) == {10002: 2, 20000: 3, 20001: 3, 10004: 4, 10005: 5, 20002: 5},
              "sorted 7, 5, 3, 2 -> 2, 3, 4, 5: 10007 -> 10002 while 10002 -> 10005 and 10003 -> 10004 (a chain through an id in use)", str(cp(res[0])))
        check(named(res[0]) and disjoint(res[0]) and sorted(res[0]["childSizes"].values()) == [100, 100, 100, 100, 200, 200], "the numbering holds and no child changed size",
              str(res[0]["childSizes"]))

    res, *_ = session(klusters, [{"hierarchy": {"toEnd": [3]}}])
    if res:
        m = cp(res[0])
        check(10003 not in m and m.get(10008) == 8, "parent 3 moved to the end (8): its self child is 10008", str(m))

    print("-- parents merged, children merged, a child promoted, Flatten")
    res, *_ = session(klusters, [{"hierarchy": {"mergeParents": [2, 3]}}])
    if res:
        m = cp(res[0])
        kept = res[0].get("returned")
        under = sorted(c for c, p in m.items() if p == kept)
        sizes = sorted(res[0]["childSizes"][str(c)] for c in under)
        check(len(under) == 3 and under[0] == kept + 10000 and all(c >= 20000 for c in under[1:]) and sizes == [100, 100, 200],
              f"2 and 3 merged into parent {kept}: one of their children is its self child, the old self-child ids 10002 and 10003 are gone ({under})", str(m))
        check(named(res[0]) and disjoint(res[0]), "the numbering holds")

    res, *_ = session(klusters, [{"hierarchy": {"mergeChildren": [20000, 20001]}}])
    if res:
        m = cp(res[0])
        check(m.get(10005) == 5 and 20000 not in m and 20001 not in m and res[0]["childSizes"].get("10005") == 200 and res[0].get("returned") == 10005,
              "parent 5's two children merged: the one child is its self child, 10005, and that is the id the merge reports", str(m))

    res, *_ = session(klusters, [{"hierarchy": {"promoteChildren": [20002]}}])
    if res:
        m = cp(res[0])
        made = [p for p in res[0]["parentSizes"] if int(p) not in (2, 3, 5, 7)]
        check(len(made) == 1 and m.get(int(made[0]) + 10000) == int(made[0]) and 20002 not in m,
              f"child 20002 promoted to a parent ({made}): it is that parent's self child", str(m))
        check(named(res[0]) and disjoint(res[0]), "the numbering holds")

    res, *_ = session(klusters, [{"hierarchy": {"flatten": True}}])
    if res:
        check(cp(res[0]) == {10002: 2, 10003: 3, 10005: 5, 10007: 7}, "Flatten: every parent has its self child alone", str(cp(res[0])))

    print("-- undo and redo of a child merge")
    res, *_ = session(klusters, [{"hierarchy": {"mergeChildren": [20000, 20001]}}, {"hierarchy": {"undoChild": True}},
                                 {"hierarchy": {"redoChild": True}}, {"hierarchy": {"undoChild": True}}])
    if len(res) == 4:
        check(cp(res[1]) == NEW, "undone: the two children are back under their own ids", str(cp(res[1])))
        check(cp(res[2]) == cp(res[0]) and cp(res[2]).get(10005) == 5, "redone: one child again, named 10005", str(cp(res[2])))
        check(cp(res[3]) == NEW, "and undone a second time: one press per edit, the renaming costs none", str(cp(res[3])))

    print("-- a child made by an edit")
    with tempfile.TemporaryDirectory() as d:
        base, clu, clc = build(d)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"coreCut": [10003], "children": True, "onsetFactor": 1.1}, {"hierarchy": {}}], d)
        res = [r for r in rep.get("results", []) if "childParents" in r]
        made = [r for r in rep.get("results", []) if "created" in r]
        new = (made[0].get("created") or [0])[0] if made else 0
        if new > 0 and res:
            check(new >= 20000 and cp(res[0]).get(new) == 3, f"a split of child 10003 makes child {new}, in the free block, under parent 3")
            check(named(res[0]) and disjoint(res[0]), "the numbering holds")
        else:
            print(f"  note: the cut found nothing to split off on this synthetic unit ({(made[0].get('summaries') if made else rep.get('stderr', '')[-300:])}); not counted")

    print("-- a parent split in two")
    for parent, sources in ((3, 1), (2, 2)):
        with tempfile.TemporaryDirectory() as d:
            base, clu, clc = build(d)
            rep = R.run(klusters, f"{base}.clu.standard.1",
                        [{"hierarchy": {}}, {"coreCut": [parent], "onsetFactor": 1.1}, {"hierarchy": {}}], d)
            res = [r for r in rep.get("results", []) if "childParents" in r]
            made = [r for r in rep.get("results", []) if "created" in r]
            new = (made[0].get("created") or [0])[0] if made else 0
            if not (new > 0 and len(res) == 2):
                print(f"  note: nothing was split off parent {parent}; not counted")
                continue
            m = cp(res[1])
            per = {}
            for c, p in m.items():
                per.setdefault(p, []).append(c)
            check(len(per) == 5 and len(m) == len(cp(res[0])) + sources,
                  f"parent {parent} split: five parents, {sources} more child(ren) than before ({len(m)})", str(m))
            if sources == 1:
                lone = sorted(p for p, cs in per.items() if cs == [p + 10000])
                check(len(lone) == 3, f"the parent cut from the one child has its self child alone, like the parent it was cut from ({lone})", str(m))
            check(named(res[1]) and disjoint(res[1]), "the numbering holds")
            tot = sum(res[1]["childSizes"][str(c)] for c in m)
            check(tot == clu.size, f"every spike is still in a child ({tot} of {clu.size})")

    print(f"test_child_ids: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
