#!/usr/bin/env python3
"""Every command id in use before the menus were redistributed still names its command.

A radial-menu layout and the icon table name commands by id.  A command without an id of its own
was named by its menu path (menu:Settings/Preferences), and the redistribution of 2026-11-24 moved
14 of those.  They now have fixed ids, and the old paths are kept as aliases
(KlustersApp::actionsById).  command_ids_before_20261124.txt is the list as it was.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    klusters = R.skip_without_binary()
    want = [l.strip() for l in open(os.path.join(HERE, "command_ids_before_20261124.txt"), encoding="utf-8")
            if l.strip() and not l.startswith("#")]
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"actionsEnabled": True}], d, timeout=600)
    r = next((x for x in rep.get("results", []) if "enabled" in x), None)
    if r is None:
        print("  FAIL: the run finished\n        " + rep.get("stderr", "")[-400:])
        return 1
    known = set(r["enabled"]) | set(r["disabled"])
    gone = [i for i in want if i not in known]
    print(("  ok:   " if not gone else "  FAIL: ") + f"the {len(want)} command ids of before 2026-11-24 all resolve"
          + ("" if not gone else "\n        " + str(gone)))
    print(f"test_command_ids_stable: {'PASS' if not gone else 'FAIL'}")
    return 1 if gone else 0


if __name__ == "__main__":
    sys.exit(main())
