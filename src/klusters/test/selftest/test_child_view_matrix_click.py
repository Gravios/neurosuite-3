#!/usr/bin/env python3
"""In the child view, a click in a matrix of parents returns to the parents.

Parent 2 is curated, its child 20000 is selected in the child palette (the views show the child
layer), and the matrices are not scoped to the children: they show parents.  A click on a cell
selected the cell's parents in the parent palette, but whenever the cell held the curated parent
the child stayed selected and the views stayed on the child layer, showing the cell's PARENT ids
as if they were children.  Only a cell without the curated parent left the child view.

  - a cell of the curated parent and another, the curated parent's own cell, another parent's
    cell, and a Ctrl-click that adds a parent: each shows the parent layer with the cell's parents,
    selects them in the parent palette and leaves no child selected.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_undo_pairing as U      # noqa: E402

fails = 0
H = lambda **k: {"hierarchy": k}          # noqa: E731
CP = lambda **k: {"childPalette": k}      # noqa: E731


def main():
    global fails
    klusters = R.skip_without_binary()
    for what, click, add, want in (("a cell of the curated parent and another", [2, 3], False, [2, 3]),
                                   ("the curated parent's own cell", [2], False, [2]),
                                   ("another parent's cell", [3], False, [3]),
                                   ("a Ctrl-click adding another parent", [3], True, [2, 3])):
        with tempfile.TemporaryDirectory() as d:
            base = U.hier(d, "m")
            rep = R.run(klusters, f"{base}.clu.{U.M}.1",
                        [H(), {"select": [2]}, CP(show=True), CP(click=20000), {"matrixSelect": click, "add": add}, CP()], d, timeout=600)
            r = next((x for x in rep.get("results", []) if "matrixSelect" in x.get("step", {})), None)
            ok = bool(r) and r["childScopeBefore"] and r["childSelectionBefore"] == [20000]
            print(("  ok:   " if ok else "  FAIL: ") + f"({what}: the child view, child 20000 selected, before the click)"
                  + ("" if ok else f"\n        {r or rep.get('stderr', '')[-300:]}"))
            fails += not ok
            if not r:
                continue
            ok = (not r["childScope"] and sorted(r["shown"]) == want and sorted(r["parentSelection"]) == want and r["childSelection"] == [])
            print(("  ok:   " if ok else "  FAIL: ") + f"{what}: the parent view shows {want}, no child is selected"
                  + ("" if ok else f"\n        {dict((k, r[k]) for k in ('childScope', 'shown', 'parentSelection', 'childSelection'))}"))
            fails += not ok
    print(f"test_child_view_matrix_click: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
