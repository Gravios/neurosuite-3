#!/usr/bin/env python3
"""The undo depth saved while the default was 2 is raised to the default of today, once.

Every Configuration::write() stores the depth, so settings written before 2026-11-24 hold a 2
nobody chose.  A stored 2 becomes 20 the first time; any other stored value is kept, and so is
a 2 stored after the raise (the flag nbUndoRaisedFromOldDefault).
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402

fails = 0


def depth(klusters, general):
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        if general is not None:
            os.makedirs(os.path.join(d, "cfg", "neurosuite-3"))
            with open(os.path.join(d, "cfg", "neurosuite-3", "klusters.conf"), "w") as fh:
                fh.write("[%General]\n" + general + "\n")   # Qt writes a group named General so
        return R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"palette": True}], d, timeout=600).get("nbUndo")


def main():
    global fails
    klusters = R.skip_without_binary()
    for general, want, what in (
            (None, 20, "no settings: the default"),
            ("nbUndo=2", 20, "a stored 2 from the old default is raised"),
            ("nbUndo=5", 5, "a stored 5 was chosen: kept"),
            ("nbUndo=2\nnbUndoRaisedFromOldDefault=true", 2, "a 2 stored after the raise was chosen: kept")):
        got = depth(klusters, general)
        ok = got == want
        print(("  ok:   " if ok else "  FAIL: ") + f"{what} ({got})")
        fails += not ok
    print(f"test_undo_depth_migration: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
