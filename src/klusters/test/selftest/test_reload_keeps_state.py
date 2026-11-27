#!/usr/bin/env python3
"""A reload of the document keeps the displays, the selections and the space shown.

Add Residual Spikes, decollide, Sort Spikes by Time and a plugin's rewrite close the document and
open it again (or open the stage written).  That used to leave a fresh Overview with one cluster
shown.  Now what was arranged is put back (KlustersApp::captureGuiState / restoreGuiState): every
display -- its type, the views added to it, its clusters, its feature dimensions --, their order,
the current one, and the feature space shown.

The session of test_residual_spikes (cluster 2 marked), with the projection space `alt`:

  - NEGATIVE CONTROL: a plain reopen of the arranged session -- what any reload did before: the
    arrangement is gone;
  - a reopen that keeps the state: the same displays, in the same order, each with its clusters,
    its added views and its dimensions, the same current one, `alt` still shown;
  - Add Residual Spikes from `alt`, opening the stage as the command does: the stage opens with the
    same displays and clusters (the ids it had before are kept), and `alt` shown;
  - the child view (a session with a child clustering, child 20001 of parent 2 clicked in the child
    palette): a plain reopen leaves it (NEGATIVE CONTROL); a reopen keeping the state shows the
    child palette and selects child 20001 again.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402
import test_mark_collisions as M          # noqa: E402
import test_residual_spikes as T          # noqa: E402
import test_undo_pairing as U            # noqa: E402

check = M.check

ARRANGE = [                                                 # (the session's clusters are 2 and 3)
    {"show": "alt"},
    {"select": [2, 3]},                                     # the Overview: both
    {"guiState": {"newDisplay": "clusters", "label": "pair"}},
    {"select": [3]},
    {"guiState": {"dims": [2, 3]}},
    {"guiState": {"newDisplay": "waveforms"}},
    {"select": [2]},
    {"guiState": {"newDisplay": "groupingAssistant"}},
    {"select": [2, 3]},
    {"guiState": {"current": 2}},                           # "pair" is the one shown
]


def states(rep):
    return [x for x in rep.get("results", []) if "guiState" in x.get("step", {})]


def key(s):
    """What must come back: the displays (label, type, added views, clusters, dims), the current one, the space.
    (The clusters as a set: the order they are drawn in follows the palette.)"""
    return ([(d["label"], d["type"], tuple(d["added"]), tuple(sorted(d["clusters"])), tuple(d["dims"])) for d in s.get("displays", [])],
            s.get("current"), s.get("featureSpace"))


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "ks")
        T.add_spaces(base, raw, S.read_res(f"{base}.res.standard.1"))
        ok, _ = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}}, {"save": True}], d)
        check(ok, "cluster 2 marked and saved")
        cluf = f"{base}.clu.standard.1"

        rep = R.run(klusters, cluf, ARRANGE + [{"guiState": {}}, {"reopen": True}, {"guiState": {}}], d)
        st = states(rep)
        arranged, plain = (st[-2], st[-1]) if len(st) >= 2 else ({}, {})
        print("      arranged:", key(arranged))
        check(len(arranged.get("displays", [])) >= 4 and arranged.get("featureSpace") == "alt" and arranged.get("current") == 2,
              "the arrangement: four displays or more, the third shown, alt shown", str(key(arranged)))
        check(key(plain) != key(arranged),
              "NEGATIVE CONTROL: a plain reopen loses the arrangement", str(key(plain)))

        rep = R.run(klusters, cluf, ARRANGE + [{"guiState": {}}, {"reopen": {"keepState": True}}, {"guiState": {}}], d)
        st = states(rep)
        a, b = (st[-2], st[-1]) if len(st) >= 2 else ({}, {})
        check(bool(a) and key(a) == key(b), "a reopen keeping the state: the same displays, order, clusters, views, dims, current one and space",
              f"before {key(a)}\n      after  {key(b)}")

        rep = R.run(klusters, cluf, ARRANGE + [{"guiState": {}},
                                               {"residualSpikes": {"clusters": [2], "apply": True, "open": True}},
                                               {"guiState": {}}], d)
        st = states(rep)
        r = next((x for x in rep.get("results", []) if "residualSpikes" in x.get("step", {})), {})
        a, b = (st[-2], st[-1]) if len(st) >= 2 else ({}, {})
        check(r.get("written") is True and r.get("opened") is True, "Add Residual Spikes from alt: the stage written and opened",
              str({k: r.get(k) for k in ("written", "opened", "stage", "reason", "error")}))
        check(bool(a) and key(a) == key(b),
              "the stage opens with the same displays, clusters, views, dims, current one, and alt shown",
              f"before {key(a)}\n      after  {key(b)}")

    # the child view: the child palette shown, a child clicked (the curator's way into it)
    with tempfile.TemporaryDirectory() as d:
        base = U.hier(d, "kid")                         # parent 2: children 20000-20003
        cluf = f"{base}.clu.{U.M}.1"
        into = [{"hierarchy": {}}, {"select": [2]}, {"childPalette": {"show": True}},
                {"childPalette": {"click": 20001}}, {"guiState": {}}]
        for keep in (False, True):
            rep = R.run(klusters, cluf, into + [{"reopen": {"keepState": True} if keep else True}, {"guiState": {}}], d)
            st = states(rep)
            a, b = (st[-2], st[-1]) if len(st) >= 2 else ({}, {})
            child = lambda x: (x.get("childPaletteShown"), x.get("childView"), x.get("childSelection"))  # noqa: E731
            if not keep:
                check(child(a) == (True, True, [20001]), "the child view entered: palette shown, child 20001 selected", str(child(a)))
                check(child(b) != child(a), "NEGATIVE CONTROL: a plain reopen leaves the child view", str(child(b)))
            else:
                check(child(b) == child(a), "a reopen keeping the state: the child palette shown, child 20001 selected again",
                      f"before {child(a)} after {child(b)}")
    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
