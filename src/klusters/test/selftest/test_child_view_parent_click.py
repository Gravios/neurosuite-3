#!/usr/bin/env python3
"""In the child view, a click on the curated parent in the parent palette returns to the parent.

The parent palette keeps the curated parent selected while its children are shown.  A click on it
does not change the list's selection, so nothing was emitted and nothing happened: the only way
back to the parent was to click another parent first.

Session of test_child_ids.py: parents 2, 3, 5, 7; parent 2 has children 10002 and 20002.

  * child 20002 is shown in the child view (after its realign: parent 2 selected in the parent
    palette, the child layer shown);
  * a click on parent 2 (`clickParent`): the parent layer is shown, the view shows 2, the parent
    palette still has 2 selected;
  * a second click changes nothing and does not switch the layer;
  * a click on another parent from the child view shows that parent, as before.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as R                # noqa: E402
import test_child_ids as T     # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def run(klusters, steps):
    with tempfile.TemporaryDirectory() as d:
        base, _, _ = T.build(d)
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        res = rep.get("results", [])
        hier = [(r.get("childScope"), r.get("shown"), r.get("layerSwitches")) for r in res if "childParents" in r]
        pal = [r.get("selected") for r in res if "order" in r and "selected" in r]
        if rep.get("rc") != 0:
            print("  FAIL: run", rep.get("rc"), rep.get("stderr", "")[-600:])
            return None, None
        return hier, pal


def main():
    global fails
    klusters = R.skip_without_binary()
    H, P = {"hierarchy": {}}, {"palette": True}
    hier, pal = run(klusters, [{"realign": 20002, "children": True}, H, P,
                               {"clickParent": 2}, H, P, {"clickParent": 2}, H])
    if not hier or len(hier) != 3 or len(pal) != 2:
        print("  FAIL: report", hier, pal)
        fails += 1
    else:
        check(hier[0][:2] == (True, [20002]) and pal[0] == [2],
              "child view: child 20002 shown, parent 2 selected in the parent palette", f"{hier[0]} {pal[0]}")
        check(hier[1][:2] == (False, [2]), "click on parent 2: the parent view shows 2", str(hier[1]))
        check(pal[1] == [2], "parent 2 is still the selected parent", str(pal[1]))
        check(hier[2] == hier[1], "a second click changes nothing", str(hier[2]))
    hier, pal = run(klusters, [{"realign": 20002, "children": True}, {"clickParent": 5}, H, P])
    if not hier or len(hier) != 1:
        print("  FAIL: report", hier, pal)
        fails += 1
    else:
        check(hier[0][:2] == (False, [5]) and pal[0] == [5], "click on parent 5 from the child view shows 5",
              f"{hier[0]} {pal[0]}")
    print("FAILED" if fails else "PASSED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
