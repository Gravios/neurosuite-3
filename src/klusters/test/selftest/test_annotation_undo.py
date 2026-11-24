#!/usr/bin/env python3
"""A change of a cluster's information is one step of the undo history.

The information (structure, type, quality, notes) travels with the labelling levels: an Undo
restores the level from before an edit, with the information as it was then.  Without a step of
its own, a change made after a merge was dropped when the merge was undone, and came back with
the redo.  As a step it is undone first, on its own, and does not count against the depth.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402

fails = 0
A = lambda **k: {"annotate": k}    # noqa: E731


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "a")
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1",
                    [{"group": [3, 4]}, A(cluster=2, quality="good"), A(cluster=2, notes="pyr"), A(cluster=2, quality="good"),
                     {"undo": True}, A(cluster=2), {"undo": True}, A(cluster=2), {"palette": True},
                     {"undo": True}, A(cluster=2), {"palette": True},
                     {"redo": True}, {"redo": True}, {"redo": True}, A(cluster=2), {"palette": True}],
                    d, timeout=900, autoRenumber=False, nbUndo=1)
        an = [r for r in rep.get("results", []) if "annotate" in r.get("step", {})]
        pal = [sorted(int(k) for k in r["counts"]) for r in rep.get("results", []) if "palette" in r.get("step", {})]
        check(len(an) == 7 and an[0].get("changed") is True and an[1].get("changed") is True and an[1].get("undoSteps") == 3,
              "two changes after a merge are two more steps, at depth 1 too (they do not count against it)",
              [(a.get("changed"), a.get("undoSteps")) for a in an])
        check(an[2].get("changed") is False and an[2].get("undoSteps") == 3, "setting what is already set is no step")
        check((an[3].get("quality"), an[3].get("notes")) == ("good", ""), "Undo takes the notes back, alone", an[3])
        check((an[4].get("quality"), an[4].get("notes")) == ("", "") and len(pal) == 3 and 4 not in pal[0],
              "the next Undo takes the quality back; the merge stands", (an[4], pal[:1]))
        check(an[5].get("undoSteps") == 0 and 4 in pal[1], "the third Undo reaches the merge", (an[5].get("undoSteps"), pal[1:2]))
        check((an[6].get("quality"), an[6].get("notes")) == ("good", "pyr") and 4 not in pal[2],
              "three Redos: the merge, the quality, the notes", (an[6], pal[2:]))
    print(f"test_annotation_undo: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
