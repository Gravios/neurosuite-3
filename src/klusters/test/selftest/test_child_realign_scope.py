#!/usr/bin/env python3
"""A child's realign leaves the views on the children.

Every realign ends with the refresh a tab switch makes, and that refresh used to hand the view's
ids to the PARENT palette.  In the child view those ids are children: the parent palette selected
nothing (or, when a child still carried the number of a parent, that unrelated parent) and its one
emission took the views to the parent layer.

Session of test_child_ids.py: parents 2, 3, 5, 7; after the load parent 2 has children 10002 and
20002.

  * child 20002 is realigned from the child view: afterwards the child layer is still the shown
    one and the view shows 20002;
  * the refresh on its own (`tabRefresh`) changes neither, and the shown layer does not change
    even in passing (`layerSwitches` -- with disjoint ids the child palette brought the child view
    back by itself, so the end state alone does not show the detour);
  * the same for the self child 10003 of parent 3;
  * from the parent view the refresh still selects what the view shows (a parent realign ends on
    the parent layer showing that parent).
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


def states(klusters, steps):
    with tempfile.TemporaryDirectory() as d:
        base, _, _ = T.build(d)
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        res = [r for r in rep.get("results", []) if "childParents" in r]
        if rep.get("rc") != 0 or len(res) != sum(1 for s in steps if "hierarchy" in s):
            print("  FAIL: run", rep.get("rc"), rep.get("stderr", "")[-600:])
            return None
        return [(r.get("childScope"), r.get("shown"), r.get("layerSwitches")) for r in res]


def main():
    klusters = R.skip_without_binary()
    for child in (20002, 10003):
        st = states(klusters, [{"realign": child, "children": True}, {"hierarchy": {}},
                               {"tabRefresh": True}, {"hierarchy": {}}])
        if st is None:
            global fails
            fails += 1
            continue
        check(st[0][:2] == (True, [child]), f"child {child} realigned: the child view still shows it", str(st[0]))
        check(st[1][:2] == (True, [child]), f"child {child}: the refresh keeps the child view", str(st[1]))
        check(st[1][2] == st[0][2], f"child {child}: the refresh never leaves the child layer",
              f"{st[1][2] - st[0][2]} layer switches")
    st = states(klusters, [{"realign": 5}, {"hierarchy": {}}, {"tabRefresh": True}, {"hierarchy": {}}])
    if st is None:
        fails += 1
    else:
        check(st[0][:2] == (False, [5]), "parent 5 realigned: the parent view shows it", str(st[0]))
        check(st[1][:2] == (False, [5]), "parent 5: the refresh keeps it", str(st[1]))
    print("FAILED" if fails else "PASSED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
