#!/usr/bin/env python3
"""Undo / redo: what the data reverts, the stacks and the curation log must agree on.

Three faults found by an audit of the undo paths (2026-11-24), each reproduced here:

  1. the log held one undo fewer than the data.  A realign (Klusters' own, after every merge) is
     begun like any action and only then marked not-undoable; while open it counted against the
     undo depth and evicted the oldest merge, which was then written "good" although it was
     undone.  Flat session, depth 2: merge, merge, undo, undo -> both GROUP entries are "bad".
  2. the child undo stack was deeper than the child data's own history (the undo depth), so a
     child undo past it popped nothing: it reported success, logged an UNDO and changed nothing.
     Three child merges, three child undos at depth 2 -> the third is refused and not logged.
     Each UNDO names the entry it reverts (reverts_action_idx).
  3. a parent undo re-cuts the children it leaves across two parents, and that re-cut is a level
     of the child history with no entry on the child stack: the next child undo popped the
     re-cut itself and put the child back across two parents.  Merge parents 2 + 3, merge one
     child of each, undo the parent merge, child undo -> refused; every child lies in one parent.
Also: Ctrl+Z with nothing to undo flips no log entry.
"""
import json
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import session as S                # noqa: E402
import test_curation_log as T      # noqa: E402

fails = 0
M = T.METHOD
H = lambda **k: {"hierarchy": k}   # noqa: E731


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def hier(d, name):
    base, m = S.make_session(d, name, n_per_unit=400, method=M)
    clu = m["clu"]
    child = clu.copy()
    two, three = np.flatnonzero(clu == 2), np.flatnonzero(clu == 3)
    for i, c in enumerate((20000, 20001, 20002, 20003)):
        child[two[i::4]] = c
    for i, c in enumerate((20004, 20005, 20006, 20007)):
        child[three[i::4]] = c
    clp = np.zeros(int(child.max()), np.int32)
    for c in np.unique(child):
        clp[c - 1] = clu[child == c][0]
    for kind, ids, hdr in (("clc", child, np.unique(child).size), ("clp", clp, clp.size)):
        with open(f"{base}.{kind}.{M}.1", "wb") as fh:
            np.array([hdr], np.int32).tofile(fh)
            ids.astype(np.int32).tofile(fh)
    return base


def status(lines):
    """{action_idx: (action, status)} at the end of the log."""
    out = {}
    for o in lines:
        if "action" in o:
            out[o["action_idx"]] = (o["action"], o["status"])
    return out


def main():
    klusters = R.skip_without_binary()

    # ---- 1. two merges, two undos, the automatic realign after each
    with tempfile.TemporaryDirectory() as d:
        base, clu = T.session(d, "f")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [{"group": [3, 4]}, {"palette": True}, {"group": [2, 5]}, {"palette": True},
                     {"undo": True}, {"undo": True}, {"palette": True}, {"undo": True}, {"save": True}],
                    d, timeout=900, autoRenumber=False)
        pal = [r for r in rep.get("results", []) if "palette" in r.get("step", {})]
        check(len(pal) == 3 and sorted(int(k) for k in pal[1]["counts"]) != [2, 3, 4]
              and sorted(int(k) for k in pal[2]["counts"]) == [2, 3, 4],
              "the data: both merges are undone (clusters 2, 3, 4 again)", [p.get("counts") for p in pal])
        st = status(T.log_lines(d, "f"))
        groups = [v[1] for k, v in sorted(st.items()) if v[0] == "GROUP"]
        check(groups == ["bad", "bad"], "the log: both GROUP entries are bad (the first was written good)", st)
        check([v[1] for k, v in sorted(st.items()) if v[0] == "REALIGN"] == ["good", "good"],
              "... the two automatic realigns stay good, and a third Ctrl+Z with nothing to undo flips nothing", st)

    # ---- 2. the child stack is as deep as the child history
    with tempfile.TemporaryDirectory() as d:
        base = hier(d, "c")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [H(), H(mergeChildren=[20000, 20001]), H(mergeChildren=[20004, 20005]), H(mergeChildren=[20006, 20007]),
                     H(undoChild=True), H(undoChild=True), H(undoChild=True), {"save": True}], d, timeout=900, autoRenumber=False)
        res = [r for r in rep.get("results", []) if r.get("step", {}).get("hierarchy", {}).get("undoChild")]
        check([r.get("returned") for r in res] == [True, True, False],
              "three child merges at depth 2: two child undos, the third is refused (it reported success)", [r.get("returned") for r in res])
        check(len(res) == 3 and len(res[2]["childParents"]) == 7 and res[2]["childParents"] == res[1]["childParents"],
              "... and changes nothing", res[2].get("childParents") if res else None)
        lines = T.log_lines(d, "c")
        undos = {o["action_idx"]: o for o in lines if o.get("action") == "UNDO" and o.get("event") == "ACTION_DETAIL"}
        edits = sorted(i for i, v in status(lines).items() if v[0] == "HIERARCHY_EDIT")
        check(len(undos) == 2, "two UNDO records, not three", sorted(undos))
        check(sorted(o.get("reverts_action_idx") for o in undos.values()) == edits[1:],
              "each UNDO names the child edit it reverts (reverts_action_idx): the last two merges",
              ([o.get("reverts_action_idx") for o in undos.values()], edits))

    # ---- 3. a child undo after a parent undo that re-cut the children
    with tempfile.TemporaryDirectory() as d:
        base = hier(d, "p")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [H(), H(mergeParents=[2, 3]), H(mergeChildren=[20000, 20004]), H(undo=True), H(undoChild=True), {"save": True}],
                    d, timeout=900, autoRenumber=False)
        res = [r for r in rep.get("results", []) if "childParents" in r]
        after_undo, after_child = res[-2], res[-1]
        check(after_child.get("returned") is False, "the child undo after the parent undo is refused (it popped the re-cut)", after_child.get("returned"))

        def nested(r):
            tot = {}
            for c, p in r["childParents"].items():
                tot[str(p)] = tot.get(str(p), 0) + r["childSizes"][c]
            return tot == {str(k): v for k, v in r["parentSizes"].items()}
        check(nested(after_undo) and nested(after_child) and after_child["childParents"] == after_undo["childParents"],
              "every child lies in one parent, before and after it (one child spanned parents 2 and 3)",
              (after_child.get("parentSizes"), after_child.get("childParents"), after_child.get("childSizes")))
        check("span more than one parent" not in (rep.get("stderr") or ""), "no nesting warning")

    print(f"test_undo_pairing: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
