#!/usr/bin/env python3
"""The four curation matrices (error, template, residual, drift) under the mouse: what a Ctrl+wheel, a
Ctrl+drag, a click, a follow and a restored view state leave.  Each view is opened at a fixed size on
the session of test_child_ids.py, computed, and sent the same list of events; after each event the test
records the zoom and pan the view announced and the clusters the display shows.  Compared with a
snapshot (matrix_views.golden.json): the views share their geometry and navigation, and a change to it
has to show here, in all four or in none.  The picture itself is not in the snapshot (fonts differ
between machines).

    python3 test_matrix_views.py --update    rewrites the snapshot: read its diff before committing it
"""
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as R                # noqa: E402
import test_child_ids as T     # noqa: E402

GOLDEN = os.path.join(HERE, "matrix_views.golden.json")
VIEWS = ("error", "template", "residual", "drift")
DO = [{"wheel": [200, 150, 120]}, {"wheel": [200, 150, 120]}, {"wheel": [120, 90, 360]}, {"drag": [150, 150, 210, 120]},
      {"click": [180, 140]}, {"click": [130, 100]}, {"wheel": [100, 100, -120]}, {"follow": 5}, {"follow": 2},
      {"drag": [200, 100, 20, 30]}, {"state": [1.0, 0.0, 0.0]}, {"click": [60, 60]}, {"click": [90, 60], "shift": True},
      {"wheel": [200, 150, 120], "plain": True}, {"state": [3.0, -40.0, 25.0]}, {"click": [200, 150]},
      {"wheel": [10, 10, -1200]}, {"click": [45, 45]}]


def label(a):
    return json.dumps(a, sort_keys=True)


def observe(klusters):
    out = {}
    for which in VIEWS:
        with tempfile.TemporaryDirectory() as d:
            base, _, _ = T.build(d)
            rep = R.run(klusters, f"{base}.clu.standard.1",
                        [{"matrixView": {"which": which, "open": True, "size": [420, 360], "update": True, "do": DO}}], d)
            r = next((x for x in rep.get("results", []) if "found" in x), {})
            if rep.get("code") != 0 or not r.get("found") or not r.get("computed"):
                out[which] = {"error": f"code {rep.get('code')}, found {r.get('found')}, computed {r.get('computed')}"}
                continue
            out[which] = {"clusters": r["clusters"],
                          "after": [{"event": label(DO[i]), "zoom": round(a["zoom"], 4), "panX": round(a["panX"], 2),
                                     "panY": round(a["panY"], 2), "shown": sorted(a["shown"])}
                                    for i, a in enumerate(r.get("after", []))]}
    return out


def main():
    klusters = R.skip_without_binary()
    got = observe(klusters)
    if "--update" in sys.argv:
        with open(GOLDEN, "w") as fh:
            json.dump(got, fh, indent=1, sort_keys=True)
            fh.write("\n")
        print(f"wrote {GOLDEN}")
        return 0
    with open(GOLDEN) as fh:
        want = json.load(fh)
    fails = 0
    for which in VIEWS:
        g, w = got.get(which, {}), want.get(which, {})
        if "error" in g:
            print(f"  FAIL: {which}: {g['error']}")
            fails += 1
            continue
        diffs = []
        if g.get("clusters") != w.get("clusters"):
            diffs.append(f"clusters {w.get('clusters')} -> {g.get('clusters')}")
        for i, (a, b) in enumerate(zip(w.get("after", []), g.get("after", []))):
            if a != b:
                diffs.append(f"after {a['event']}: " + ", ".join(f"{k} {a[k]} -> {b[k]}" for k in a if a[k] != b[k]))
        if len(w.get("after", [])) != len(g.get("after", [])):
            diffs.append(f"{len(g.get('after', []))} events reported, {len(w.get('after', []))} in the snapshot")
        print(("  ok:   " if not diffs else "  FAIL: ") + f"{which} matrix: the {len(DO)} events leave what the snapshot has")
        for dline in diffs[:8]:
            print("        " + dline)
        fails += bool(diffs)
    print(f"test_matrix_views: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
