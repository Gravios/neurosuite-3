#!/usr/bin/env python3
"""Undo and Redo of a realign.  A realign rewrites rows of the .res, the .spk and the .fet; Undo must put
back exactly what was there and Redo exactly what the realign wrote.  Every case is judged on the saved
files, byte for byte: a sequence of steps ending in Save must leave the files another, simpler sequence
leaves ("realign, undo, save" the untouched session; "realign, undo, redo, save" what "realign, save"
leaves; and so on).  Also: what a realign step is in the history (one step; Align All one step; the
automatic realign after a merge part of the merge's step), that a nudge ends the undo of earlier
realigns, and that a realign done in another feature space is refused until that space is shown.
"""
import glob
import hashlib
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S            # noqa: E402
import run as R                # noqa: E402
import test_child_ids as T     # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n" + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def digest(d, name="rat"):
    """Every per-spike file of the session, whatever its space."""
    out = {}
    for f in sorted(glob.glob(os.path.join(d, name + ".*"))):
        name = os.path.basename(f)
        if any(("." + k + ".") in name for k in ("res", "spk", "fet", "clu", "clc")) and not name.endswith((".pending", ".autosave", ".bak")):
            out[name] = hashlib.sha1(open(f, "rb").read()).hexdigest()
    return out


def run(klusters, steps, **prefs):
    """The steps on a fresh session; returns (report, files afterwards, files before)."""
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        before = digest(d)
        prefs.setdefault("realignFromAnySpace", True)
        prefs.setdefault("autoRealign", False)
        prefs.setdefault("autoRenumber", False)
        prefs.setdefault("autoMatrices", False)
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d, **prefs)
        return rep, digest(d), before


def results(rep, key):
    return [r for r in rep.get("results", []) if key in r]


def ran(rep):
    return rep.get("code") == 0 and rep.get("rc") == 0


def differing(a, b):
    return sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))


def main():
    klusters = R.skip_without_binary()
    SAVE, UNDO, REDO, STATE = {"save": True}, {"undo": True}, {"redo": True}, {"undoState": True}

    # ── one realign ─────────────────────────────────────────────
    rep, realigned, orig = run(klusters, [{"realign": 2}, STATE, SAVE])
    check(ran(rep) and realigned != orig, "a realign changes the saved files", rep.get("stderr", "")[-800:])
    st = results(rep, "undoSteps")
    check(st and st[0]["undoSteps"] == 1 and st[0]["redoSteps"] == 0, "a realign is one undo step", st)

    rep, got, _ = run(klusters, [{"realign": 2}, UNDO, SAVE])
    st = results(rep, "undoSteps")
    check(ran(rep) and got == orig, "realign, undo, save: the files are the untouched session's", differing(got, orig))
    check(st and st[0]["undoSteps"] == 0 and st[0]["redoSteps"] == 1, "... and the step is on the redo side", st)

    rep, got, _ = run(klusters, [{"realign": 2}, UNDO, REDO, SAVE])
    check(ran(rep) and got == realigned, "realign, undo, redo, save: the files are the realign's", differing(got, realigned))

    rep, got, _ = run(klusters, [{"realign": 2}, UNDO, REDO, UNDO, SAVE])
    check(ran(rep) and got == orig, "undo, redo, undo: the untouched session's again", differing(got, orig))

    # ── several realigns: the newest first ──────────────────────
    rep, got, _ = run(klusters, [{"realign": 2}, {"realign": 3}, UNDO, SAVE])
    check(ran(rep) and got == realigned, "two clusters realigned, one undo: the first realign alone remains",
          differing(got, realigned))
    rep, got, _ = run(klusters, [{"realign": 2}, {"realign": 3}, UNDO, UNDO, SAVE])
    check(ran(rep) and got == orig, "... two undos: the untouched session's", differing(got, orig))
    rep, twice, _ = run(klusters, [{"realign": 2}, {"realign": 2}, SAVE])
    rep, got, _ = run(klusters, [{"realign": 2}, {"realign": 2}, UNDO, SAVE])
    check(ran(rep) and got == realigned, "one cluster realigned twice, one undo: the first realign's files",
          differing(got, realigned))
    rep, got, _ = run(klusters, [{"realign": 2}, {"realign": 2}, UNDO, UNDO, REDO, REDO, SAVE])
    check(ran(rep) and got == twice, "... undone twice and redone twice: both realigns' files", differing(got, twice))

    # ── across a save ───────────────────────────────────────────
    rep, got, _ = run(klusters, [{"realign": 2}, SAVE, UNDO, SAVE])
    check(ran(rep) and got == orig, "realign, save, undo, save: the untouched session's", differing(got, orig))
    rep, got, _ = run(klusters, [{"realign": 2}, SAVE, UNDO, SAVE, REDO, SAVE])
    check(ran(rep) and got == realigned, "... redo after that, save: the realign's", differing(got, realigned))

    # ── the other modes write other rows; the undo does not care ─
    for mode, name in ((1, "PCA refine"), (2, "RMS recenter"), (3, "centroid")):
        rep, got, _ = run(klusters, [{"realign": 2}, UNDO, SAVE], realignMode=mode)
        check(ran(rep) and got == orig, f"mode {name}: realign, undo, save is the untouched session", differing(got, orig))

    # ── the child layer shares the rows (session of test_child_ids.py: parent 2 has children 10002, 20002) ─
    def child_run(steps):
        with tempfile.TemporaryDirectory() as d:
            base, _, _ = T.build(d)
            before = digest(d, "ci")
            rep = R.run(klusters, f"{base}.clu.standard.1", steps, d, realignFromAnySpace=True, autoRealign=False,
                        autoRenumber=False, autoMatrices=False)
            return rep, digest(d, "ci"), before
    # A save of this session rewrites the .clc by itself (the load renumbers the children),
    # so the reference is the session loaded and saved, not the files as built.
    rep, child_orig, _ = child_run([{"hierarchy": {}}, SAVE])
    rep, child_real, _ = child_run([{"hierarchy": {}}, {"realign": 2}, SAVE])
    check(ran(rep) and child_real != child_orig, "with a child layer: a parent's realign changes the files")
    rep, got, _ = child_run([{"hierarchy": {}}, {"realign": 2}, UNDO, SAVE])
    check(ran(rep) and got == child_orig, "... realign, undo, save: the untouched session's", differing(got, child_orig))
    rep, got, _ = child_run([{"realign": 20002, "children": True}, UNDO, SAVE])
    check(ran(rep) and got == child_orig, "a child realigned from the child view, undo, save: the untouched session's",
          differing(got, child_orig))
    rep, kid, _ = child_run([{"realign": 20002, "children": True}, SAVE])
    rep, got, _ = child_run([{"realign": 20002, "children": True}, UNDO, REDO, SAVE])
    check(ran(rep) and kid != child_orig and got == kid, "... and redone: the child realign's files", differing(got, kid))

    # ── a nudge rewrites rows without a record: the realign steps end ─
    rep, nudged, _ = run(klusters, [{"realign": 2}, {"nudge": [3, 1]}, STATE, SAVE])
    st = results(rep, "undoSteps")
    check(ran(rep) and st and st[0]["undoSteps"] == 0, "after a nudge the realign is no longer an undo step", st)
    rep, got, _ = run(klusters, [{"realign": 2}, {"nudge": [3, 1]}, UNDO, SAVE])
    check(ran(rep) and got == nudged, "... and Undo then changes nothing", differing(got, nudged))

    # ── a realign in another feature space ──────────────────────
    up = [{"supersample": 4}, {"show": "standard_U4"}]
    rep, base_up, _ = run(klusters, up + [SAVE], supersampleInterp=0)
    rep, real_up, _ = run(klusters, up + [{"realign": 2}, SAVE], supersampleInterp=0)
    check(ran(rep) and real_up != base_up, "a realign in the super-sampled space changes the files")
    rep, got, _ = run(klusters, up + [{"realign": 2}, UNDO, {"wait": 300}, SAVE], supersampleInterp=0)
    check(ran(rep) and got == base_up, "... realign, undo, save there: every space's files as without the realign",
          differing(got, base_up))
    rep, got, _ = run(klusters, up + [{"realign": 2}, UNDO, {"wait": 300}, REDO, {"wait": 300}, SAVE], supersampleInterp=0)
    check(ran(rep) and got == real_up, "... and redone: every space's files as after the realign", differing(got, real_up))
    rep, elsewhere, _ = run(klusters, up + [{"realign": 2}, {"show": "standard"}, SAVE], supersampleInterp=0)
    rep, got, _ = run(klusters, up + [{"realign": 2}, {"show": "standard"}, UNDO, STATE, SAVE], supersampleInterp=0)
    st = results(rep, "undoSteps")[1:]
    check(ran(rep) and st and st[0]["undoSteps"] == 1 and "standard_U4" in st[0].get("status", ""),
          "with another space shown, Undo is refused and says which space to show", st)
    check(got == elsewhere, "... and nothing was written", differing(got, elsewhere))
    rep, got, _ = run(klusters, up + [{"realign": 2}, {"show": "standard"}, {"show": "standard_U4"}, UNDO, {"wait": 300}, SAVE],
                      supersampleInterp=0)
    check(ran(rep) and got == base_up, "... back in the realign's space, Undo works", differing(got, base_up))

    # ── the memory kept for it is a preference ───────────────────
    rep, got, _ = run(klusters, [{"realign": 2}, {"wait": 50}, STATE, UNDO, SAVE], realignUndoMemoryMB=0)
    st = results(rep, "undoSteps")
    check(ran(rep) and st and st[0]["undoSteps"] == 0 and "not kept for undo" in st[0].get("status", ""),
          "with 0 MB for the undo of realigns, a realign is no step and the status bar says why", st[:1])
    check(got == realigned, "... and Undo then changes nothing", differing(got, realigned))
    rep, got, _ = run(klusters, [{"realign": 2}, {"realign": 3}, STATE, UNDO, UNDO, SAVE], realignUndoMemoryMB=1)
    st = results(rep, "undoSteps")
    check(ran(rep) and st and st[0]["undoSteps"] == 2, "1 MB holds both small realigns", st[:1])
    check(got == orig, "... and both are undone", differing(got, orig))

    # ── Align All is one step ────────────────────────────────────
    rep, all_aligned, _ = run(klusters, [{"alignAll": True}, STATE, SAVE])
    st = results(rep, "undoSteps")
    check(ran(rep) and all_aligned != orig and st and st[0]["undoSteps"] == 1, "Align All is one undo step", st)
    rep, got, _ = run(klusters, [{"alignAll": True}, UNDO, SAVE])
    check(ran(rep) and got == orig, "Align All, undo, save: the untouched session's", differing(got, orig))
    rep, got, _ = run(klusters, [{"alignAll": True}, UNDO, REDO, SAVE])
    check(ran(rep) and got == all_aligned, "... and redone: Align All's files", differing(got, all_aligned))

    # ── the automatic realign after a merge is part of the merge's step ─
    rep, merged, _ = run(klusters, [{"group": [2, 3]}, STATE, SAVE], autoRealign=True)
    st = results(rep, "undoSteps")
    check(ran(rep) and st and st[0]["undoSteps"] == 1, "a merge with its automatic realign is one undo step", st)
    rep, got, _ = run(klusters, [{"group": [2, 3]}, UNDO, SAVE], autoRealign=True)
    check(ran(rep) and got == orig, "merge with automatic realign, undo, save: the untouched session's (labels and spike times)",
          differing(got, orig))
    rep, got, _ = run(klusters, [{"group": [2, 3]}, UNDO, REDO, SAVE], autoRealign=True)
    check(ran(rep) and got == merged, "... and redone: the merge's files", differing(got, merged))

    print(f"test_realign_undo: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
