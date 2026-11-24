#!/usr/bin/env python3
"""After the palette's Group, the children are filed under the merged parent.

Hierarchy > Merge Parents re-derives the child<->parent maps and renames the folded parents'
self children; the palette's Group (G) did neither.  With the automatic renumbering on, the
renumbering did it by the way; with it off, the children stayed filed under parents that no
longer existed until the next hierarchy operation (19 of 763 on jg05-20120312 group 8).
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_undo_pairing as U      # noqa: E402

fails = 0
H = lambda **k: {"hierarchy": k}   # noqa: E731


def main():
    global fails
    klusters = R.skip_without_binary()
    for ren in (False, True):
        with tempfile.TemporaryDirectory() as d:
            base = U.hier(d, "m")
            rep = R.run(klusters, f"{base}.clu.{U.M}.1", [H(), {"group": [2, 3]}, H(), H(undo=True)], d, timeout=900,
                        autoRenumber=ren, autoRealign=False)
            hs = [r for r in rep.get("results", []) if "childParents" in r]
            ok = len(hs) == 3
            if ok:
                a = hs[1]
                lost = {c: p for c, p in a["childParents"].items() if int(c) > 1 and str(p) not in a["parentSizes"]}
                ok = not lost and len(a["childParents"]) == len(hs[0]["childParents"])
                back = {c: p for c, p in hs[2]["childParents"].items() if int(c) > 1} == {c: p for c, p in hs[0]["childParents"].items() if int(c) > 1}
            print(("  ok:   " if ok else "  FAIL: ") + f"automatic renumbering {'on' if ren else 'off'}: every child's parent exists after the merge"
                  + ("" if ok else f"\n        {lost if len(hs) == 3 else rep.get('stderr', '')[-300:]}"))
            fails += not ok
            if len(hs) == 3:
                print(("  ok:   " if back else "  FAIL: ") + "... and Undo files them under the two parents again")
                fails += not back
    print(f"test_palette_merge_children: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
