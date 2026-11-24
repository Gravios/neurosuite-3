#!/usr/bin/env python3
"""Undo / redo: what the data reverts, the history and the curation log must agree on.

From an audit of the undo paths (2026-11-24).  One history for both layers; each entry holds the
child labelling to restore.

  1. the log held one undo fewer than the data (a realign counted against the depth while it was
     being logged): merge, merge, undo, undo at depth 2 -> both GROUP entries are "bad".
  2. depth: three child merges at depth 2 -> two child undos, the third is refused and not
     logged (it reported success and changed nothing).  Each UNDO names what it reverts.
  3. order: merge parents, then merge a child of each; Undo takes the child merge first (it is
     the newer edit), then the parent merge; the forced child undo does not step over a newer
     parent edit.  It left a child across two parents.
  4. a hierarchy command is one step: promoting three children is undone, and redone, by one
     Undo / Redo (it was three steps; at depth 2 the first could not be undone).
  5. a parent undo puts the children back: a parent split that cut across the children leaves
     them as they were once undone (it left the fragments as extra children), and the child
     merge made before the split is still undoable (the split dropped the child history).
     Redo brings the split and its fragments back.
  6. a merge and the renumbering Klusters runs after it are one step.
  7. a change to the children that is no edit of one is a step of its own: an id compaction
     that moved only atoms is undone alone, and the child merge before it by the next Undo
     (undoing the merge took the compaction along).
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
                    d, timeout=900, autoRenumber=False, nbUndo=2)
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
                     H(undoChild=True), H(undoChild=True), H(undoChild=True), {"save": True}], d, timeout=900, autoRenumber=False, nbUndo=2)
        res = [r for r in rep.get("results", []) if r.get("step", {}).get("hierarchy", {}).get("undoChild")]
        check([r.get("returned") for r in res] == [True, True, False],
              "three child merges at depth 2: two child undos, the third is refused (it reported success)", [r.get("returned") for r in res])
        check(len(res) == 3 and len(res[2]["childParents"]) == 7 and res[2]["childParents"] == res[1]["childParents"],
              "... and changes nothing", res[2].get("childParents") if res else None)
        lines = T.log_lines(d, "c")
        undos = {o["action_idx"]: o for o in lines if o.get("action") == "UNDO" and o.get("event") == "ACTION_DETAIL"}
        edits = sorted(i for i, v in status(lines).items() if v[0] == "HIERARCHY_EDIT")
        check(len(undos) == 2, "two UNDO records, not three", sorted(undos))
        check([status(lines)[i][1] for i in edits] == ["good", "bad", "bad"],
              "the two undone child merges are written bad (a child edit was never bad)",
              [status(lines)[i] for i in edits])
        check(sorted(o.get("reverts_action_idx") for o in undos.values()) == edits[1:],
              "each UNDO names the child edit it reverts (reverts_action_idx): the last two merges",
              ([o.get("reverts_action_idx") for o in undos.values()], edits))

    def kids(r):
        return {int(c): (int(p), r["childSizes"][c]) for c, p in r["childParents"].items()}

    def nested(r):
        tot = {}
        for c, p in r["childParents"].items():
            tot[str(p)] = tot.get(str(p), 0) + r["childSizes"][c]
        return tot == {str(k): v for k, v in r["parentSizes"].items()}

    # ---- 3. order: the newest edit first, whatever its layer
    with tempfile.TemporaryDirectory() as d:
        base = hier(d, "p")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [H(), H(mergeParents=[2, 3]), H(mergeChildren=[20000, 20004]), H(undo=True), H(undoChild=True),
                     H(undo=True), {"save": True}], d, timeout=900, autoRenumber=False)
        res = [r for r in rep.get("results", []) if "childParents" in r]
        start, merged, u1, uc, u2 = res[0], res[1], res[3], res[4], res[5]
        check(kids(u1) == kids(merged) and u1["parentSizes"] == merged["parentSizes"],
              "Undo after a parent merge and then a child merge takes the child merge (the newer edit)", kids(u1))
        check(uc.get("returned") is False and kids(uc) == kids(u1),
              "the forced child undo does not step over the parent merge now on top", uc.get("returned"))
        check(kids(u2) == kids(start) and u2["parentSizes"] == start["parentSizes"], "the next Undo takes the parent merge: the start again", kids(u2))
        check(all(nested(r) for r in res) and "span more than one parent" not in (rep.get("stderr") or ""),
              "every child lies in one parent at every step (one spanned parents 2 and 3)")

    # ---- 4. a hierarchy command is one step
    with tempfile.TemporaryDirectory() as d:
        base = hier(d, "g")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [H(), H(promoteChildren=[20004, 20005, 20006]), H(undo=True), H(redo=True), H(undo=True), {"save": True}],
                    d, timeout=900, autoRenumber=False)
        res = [r for r in rep.get("results", []) if "childParents" in r]
        start, prom, u, rd, u2 = res
        check(prom.get("undoSteps") == 1 and kids(u) == kids(start) and u["parentSizes"] == start["parentSizes"] and u.get("undoSteps") == 0,
              "promoting three children is ONE undo step, and one Undo takes all of it back", (prom.get("undoSteps"), kids(u)))
        check(kids(rd) == kids(prom) and rd["parentSizes"] == prom["parentSizes"] and rd.get("redoSteps") == 0,
              "one Redo makes it again, the children's names included", kids(rd))
        check(kids(u2) == kids(start), "... and it undoes again")
        st = status(T.log_lines(d, "g"))
        check([v[1] for k, v in sorted(st.items()) if v[0] == "MOVE_SPIKES"] == ["bad"] * 3, "the log: its three moves are bad", st)

    # ---- 5. a parent undo puts the children back; the child history survives a parent edit
    with tempfile.TemporaryDirectory() as d:
        base = hier(d, "s")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [H(), H(mergeChildren=[20000, 20001]), {"coreCut": [3]}, H(), H(undo=True), H(redo=True), H(undo=True),
                     H(undoChild=True), {"save": True}], d, timeout=900, autoRenumber=False)
        res = [r for r in rep.get("results", []) if "childParents" in r]
        start, cm, cut, u, rd, u2, uc = res
        check(len(kids(cut)) > len(kids(cm)) and len(cut["parentSizes"]) == 3, "(the parent split cut across the children of parent 3)", kids(cut))
        check(kids(u) == kids(cm) and u["parentSizes"] == cm["parentSizes"],
              "undoing the parent split puts the children back as they were (it left the fragments as extra children)", kids(u))
        check(kids(rd) == kids(cut) and rd["parentSizes"] == cut["parentSizes"], "Redo brings the split and its fragments back", kids(rd))
        check(kids(u2) == kids(cm) and uc.get("returned") is True and kids(uc) == kids(start),
              "the child merge made BEFORE the split is still undoable after it (the split dropped the child history)", (uc.get("returned"), kids(uc)))
        check(all(nested(r) for r in res), "every child lies in one parent at every step")

    # ---- 6. a merge and Klusters' own renumbering after it: one step
    with tempfile.TemporaryDirectory() as d:
        base, clu = T.session(d, "r")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [{"palette": True}, {"group": [2, 3]}, {"palette": True}, {"undo": True}, {"palette": True}, {"redo": True}, {"palette": True}],
                    d, timeout=900, autoRenumber=True)
        pal = [r["counts"] for r in rep.get("results", []) if "palette" in r.get("step", {})]
        check(len(pal) == 4 and sorted(map(int, pal[1])) == [2, 3] and sorted(pal[1].values()) == [134, 666],
              "(merge 2 + 3 with renumbering on: clusters 2 and 3 afterwards)", pal[1] if len(pal) > 1 else pal)
        check(len(pal) == 4 and pal[2] == pal[0], "ONE Undo takes back the merge and the renumbering (it took two)", pal[2] if len(pal) > 2 else pal)
        check(len(pal) == 4 and pal[3] == pal[1], "one Redo makes both again")

    # ---- 7. an atom-only compaction is its own step
    with tempfile.TemporaryDirectory() as d:
        base = hier(d, "k")
        rep = R.run(klusters, f"{base}.clu.{M}.1",
                    [H(), H(mergeChildren=[20000, 20001]), H(compact=True), H(undo=True), H(undo=True)],
                    d, timeout=900, autoRenumber=False)
        res = [r for r in rep.get("results", []) if "hierarchy" in r.get("step", {})]
        kids = [sorted(int(k) for k in r.get("childParents", {})) for r in res]
        check(len(res) == 5 and res[2].get("returned") is True and kids[2] != kids[1] and len(kids[2]) == 7,
              "after a child merge, Compact Cluster IDs renumbers the atoms", kids[1:3])
        check(res[2].get("undoSteps") == 2, "... and is a step of the history (2 steps: the merge, the compaction)",
              res[2].get("undoSteps"))
        check(kids[3] == kids[1], "Undo takes the compaction back alone: the atoms as the merge left them", kids[3])
        check(kids[4] == kids[0] and len(kids[4]) == 8, "the next Undo takes the merge back", kids[4])

    print(f"test_undo_pairing: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
