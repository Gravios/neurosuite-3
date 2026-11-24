#!/usr/bin/env python3
"""No editing command can be triggered while a job runs.

Each job state switched off a hand-kept list of actions, and the lists were incomplete: during a
recluster every Hierarchy command, Save and a dozen split / strip commands stayed enabled; during a
realign the same split / strip commands and the feature-space switch.  Whatever enabled an action
afterwards undid them too (the undo counts re-enabled Undo).  One lock now:

  - while a recluster process runs, and while a realign's worker runs, every editing command is
    disabled -- the display, view and navigation commands are not;
  - when the job has ended, exactly the commands that were enabled before are enabled again.

The realign is asked for 9000 spikes a unit so that it is still running at the next harness tick; a
run in which the job had already ended is reported and not counted.
"""
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                           # noqa: E402
import session as S                       # noqa: E402
import test_curation_log as T             # noqa: E402
import test_recluster_klustakwik as K     # noqa: E402
import test_undo_pairing as U             # noqa: E402

fails = 0
H = lambda **k: {"hierarchy": k}          # noqa: E731
AE = {"actionsEnabled": True}
BUSY = {"actionsEnabled": True, "whileBusy": True}
FREE = {"action.nextChunk", "action.prevChunk", "action.updateDisplay", "action.updateErrorMatrix", "action.setObliqueBasis"}
MENUS = ("menu:Tools/", "menu:Hierarchy/", "menu:Actions/Sort Spikes by Time", "menu:Actions/Sync Feature Spaces")


def editing(i):
    if i in FREE or i.startswith("recluster.engine."):
        return False
    return (i.startswith(("action.", "hierarchy.", "recluster.", "sort.", "space.") + MENUS)
            or i in ("edit.undo", "edit.redo", "file.save", "file.saveAs", "file.renumberAndSave", "file.import"))


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def judge(tag, rep, flag):
    rs = [r for r in rep.get("results", []) if "enabled" in r]
    if len(rs) != 3:
        check(False, f"{tag}: the run finished", rep.get("stderr", "")[-400:])
        return
    idle, busy, after = rs
    if not busy[flag]:
        print(f"  note: {tag}: the job had ended before the next step; not counted")
        return
    on = sorted(i for i in busy["enabled"] if editing(i))
    check(not on, f"{tag}: no editing command is enabled while it runs ({sum(editing(i) for i in idle['enabled'])} were, idle)", on)
    view = ("display.", "view.", "waveforms.", "trace.", "help.", "edit.selectAll")
    gone = [i for i in idle["enabled"] if i.startswith(view) and i not in busy["enabled"]]
    check(not gone, f"{tag}: the display, view and navigation commands stay enabled", gone)
    check(set(after["enabled"]) == set(idle["enabled"]), f"{tag}: afterwards the same commands are enabled as before",
          sorted(set(after["enabled"]) ^ set(idle["enabled"])))


def big_hier(d, name, n):
    base, m = S.make_session(d, name, n_per_unit=n, method=U.M)
    clu = m["clu"]
    child = clu.copy()
    for parent, first in ((2, 20000), (3, 20004)):
        rows = np.flatnonzero(clu == parent)
        for i in range(4):
            child[rows[i::4]] = first + i
    clp = np.zeros(int(child.max()), np.int32)
    for c in np.unique(child):
        clp[c - 1] = clu[child == c][0]
    for kind, ids, hdr in (("clc", child, np.unique(child).size), ("clp", clp, clp.size)):
        with open(f"{base}.{kind}.{U.M}.1", "wb") as fh:
            np.array([hdr], np.int32).tofile(fh)
            ids.astype(np.int32).tofile(fh)
    return base


def main():
    klusters = R.skip_without_binary()
    kkk = K.sorter()
    if kkk:
        with tempfile.TemporaryDirectory() as d:
            base = big_hier(d, "r", 9000)
            # an edit first, so that Undo is enabled when the job starts
            rep = R.run(klusters, f"{base}.clu.{U.M}.1",
                        [H(), H(mergeChildren=[20004, 20005]), H(showChildren=[20000]), H(showChildren=[]), {"select": [2]}, AE,
                         {"recluster": "", "opts": dict(exe=kkk, autoSelect=False, args=K.T + "1111111111111")}, BUSY, {"undo": True}, AE],
                        d, timeout=1200, autoRenumber=False, autoRealign=False)
            rs = [r for r in rep.get("results", []) if "enabled" in r]
            # the last report follows an Undo of the recluster: compare what it re-enables apart from the undo pair
            for r in rs:
                r["enabled"] = [i for i in r["enabled"] if i not in ("edit.undo", "edit.redo", "menu:Edit/Undo", "menu:Edit/Redo",
                                                                     "hierarchy.undoChildEdit", "hierarchy.redoChildEdit",
                                                                     "menu:Hierarchy/Undo Child-Layer Edit", "menu:Hierarchy/Redo Child-Layer Edit")] \
                    if r is rs[-1] or r is rs[0] else r["enabled"]
            judge("recluster", rep, "processRunning")
    else:
        print("  note: no KiloKlustaKwik_cpu (build it or set KKK_BIN): the recluster case is skipped")
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "a", n_per_unit=9000, method=T.METHOD)
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"select": [2]}, AE, {"realign": 2}, BUSY, AE], d, timeout=1200)
        judge("realign", rep, "realignRunning")
    # A key the resolver dispatches is not an action, so disabling actions does not stop it:
    # T (move the selection to the end) renumbers the cluster.  It must refuse during a job.
    KEY = {"keyCommand": "clusters.moveSelectionToEnd", "paletteSelect": [2]}
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "k", n_per_unit=9000, method=T.METHOD)
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1",
                    [{"select": [2]}, {"realign": 2}, dict(KEY, whileBusy=True), KEY], d, timeout=1200, autoRenumber=False)
        ks = [r for r in rep.get("results", []) if "keyCommand" in r]
        if len(ks) != 2 or not all(k["known"] for k in ks):
            check(False, "key command: the run finished", rep.get("stderr", "")[-400:])
        elif not ks[0]["blocked"]:
            print("  note: key command: the realign had ended before the key; not counted")
        else:
            check(not ks[0]["idsChanged"], "a key command that edits (T) does nothing while a realign runs")
            check(not ks[1]["blocked"] and ks[1]["idsChanged"], "and works again when the realign has ended", ks[1])
    print(f"test_edit_lock: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
