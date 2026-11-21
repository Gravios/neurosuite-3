#!/usr/bin/env python3
"""Strip Residual Outliers on CHILDREN.  With one or more children of a parent selected, the scan reads
the child (.clc) layer, scores each child WHOLE -- no time chunks -- and the split makes a sibling child
under the same parent, so the parent's membership does not change.

The fixture is test_residual_strip_chunks' drifting unit with a hierarchy on top: parent 2 has three
children, its spikes cut by time into thirds (children 10002, 20000, 20001: its self child and two
others); parent 3 has one (its self child, 10003).  116 of
parent 2's 1200 spikes are contaminants.  Driven through `residualScan` with "children": true (and
"apply": true to split as the action does):

  - the children are scored on the child layer, in one chunk each whatever chunk length is asked for;
  - scored as three children the scan finds the contaminants a parent-wide template hides (each child
    spans a third of the drift);
  - applied, the flagged spikes become ONE new child whose parent is 2; parent 2 keeps all its spikes
    and the children still add up to it;
  - children of two parents are never pooled into one new child: one per parent;
  - a seed child is the template and is not stripped itself.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402
import test_residual_strip_chunks as C    # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n" + extra) if (not ok and extra) else ""))
    fails += not ok


def build(d, drift=0.5):
    """The drifting unit, plus .clc / .clp: parent 2 -> children 10002, 20000, 20001 (time thirds), parent 3 -> 10003."""
    base, clu, cont, res = C.build(d, drift=drift)
    clc = np.where(clu >= 2, clu + 10000, clu)             # a parent's self child is parent + 10000
    u2 = np.flatnonzero(clu == 2)
    third = np.array_split(u2, 3)
    clc[third[1]] = 20000                                   # every other child: 20000 and up
    clc[third[2]] = 20001
    with open(f"{base}.clc.standard.1", "wb") as fh:
        np.array([len(np.unique(clc))], np.int32).tofile(fh); clc.astype(np.int32).tofile(fh)
    clp = np.zeros(20001, np.int32)                         # entry c-1 = the parent of child c
    clp[[10002 - 1, 20000 - 1, 20001 - 1]] = 2
    clp[10003 - 1] = 3
    with open(f"{base}.clp.standard.1", "wb") as fh:
        np.array([clp.size], np.int32).tofile(fh); clp.tofile(fh)
    return base, clu, clc, cont, res


def scan(klusters, ids, children=True, **kw):
    with tempfile.TemporaryDirectory() as d:
        base, clu, clc, cont, res = build(d)
        step = {"residualScan": dict(clusters=ids, children=children, **kw)}
        rep = R.run(klusters, f"{base}.clu.standard.1", [step], d)
        r = next((x for x in rep.get("results", []) if "flagged" in x or "error" in x), {})
        fl = np.zeros(res.size, bool)
        by = {int(k): np.asarray(v, int) for k, v in r.get("flagged", {}).items()}
        for idx in by.values():
            fl[idx] = True
        u2 = clu == 2
        nc = int((cont & u2).sum())
        r.update(code=rep.get("code"), err=rep.get("stderr", "")[-1500:], by=by, fl=fl, clu=clu, clc=clc,
                 recall=int((fl & cont & u2).sum()) / max(nc, 1),
                 fp_rate=int((fl & ~cont & u2).sum()) / max(int((u2 & ~cont).sum()), 1))
        return r


def main():
    K = R.skip_without_binary()

    parent = scan(K, [2], children=False)
    kids = scan(K, [10002, 20000, 20001], chunkSeconds=10)
    check(parent.get("code") == 0 and kids.get("code") == 0, "the scans ran", parent.get("err", "") + kids.get("err", "") + str(kids.get("error", "")))
    print(f"    parent 2, whole session : {100 * parent['recall']:5.1f} % of the contaminants found, {100 * parent['fp_rate']:4.1f} % of the clean spikes flagged")
    print(f"    its three children      : {100 * kids['recall']:5.1f} % of the contaminants found, {100 * kids['fp_rate']:4.1f} % of the clean spikes flagged")
    check(kids.get("chunks") == 1, f"children are scored whole: one chunk, though 10 s chunks were asked for ({kids.get('chunks')})")
    check(kids.get("scored") == 3 and all("child " in l for l in kids.get("lines", [])),
          "three children scored, named as children in the report", " ".join(kids.get("lines", [])))
    ok_layer = all(np.all(kids["clc"][idx] == c) for c, idx in kids["by"].items())
    check(ok_layer and set(kids["by"]) <= {10002, 20000, 20001}, "every flagged spike belongs to the child it is reported under (child-layer ids)")
    check(kids["recall"] > parent["recall"] + 0.25,
          f"scored as children, more of the contaminants are found than under one parent-wide template ({kids['recall']:.2f} vs {parent['recall']:.2f})")

    # Applied: one new sibling child of parent 2.
    app = scan(K, [10002, 20000, 20001], apply=True)
    total = int(app.get("total", -1))
    created = app.get("created", [])
    check(app.get("code") == 0 and len(created) == 1, f"applied: one new child ({created})", app.get("err", "") + " ".join(app.get("made", [])))
    if len(created) == 1:
        new = str(created[0])
        check(app["createdParent"].get(new) == 2, f"its parent is 2 ({app['createdParent'].get(new)})")
        check(app["childSizes"].get(new) == total == app.get("placed"), f"it holds exactly the flagged spikes ({app['childSizes'].get(new)} of {total})")
        check(app["parentSizes"].get("2") == 1200, f"parent 2 keeps all 1200 spikes ({app['parentSizes'].get('2')})")
        kids2 = sum(app["childSizes"].get(k, 0) for k in ("10002", "20000", "20001", new))
        check(kids2 == 1200, f"and its children still add up to it ({kids2})")
        check(app["childSizes"].get("10003") == app["parentSizes"].get("3"), "the other parent's child is untouched")

    # Children of two parents: never one new child across both.
    two = scan(K, [10002, 20000, 20001, 10003], apply=True)
    # Judged on the layer as it stands afterwards (ids may be renumbered between splits).
    cp, cs, ps = two.get("childParents", {}), two.get("childSizes", {}), two.get("parentSizes", {})
    flagged3 = len(two["by"].get(10003, []))
    flagged2 = sum(len(two["by"].get(c, [])) for c in (10002, 20000, 20001))
    n2 = sum(1 for p in cp.values() if p == 2)
    n3 = sum(1 for p in cp.values() if p == 3)
    check(two.get("code") == 0 and flagged2 > 0 and flagged3 > 0, f"both parents had spikes flagged ({flagged2}, {flagged3})", two.get("err", ""))
    check(n2 == 4 and n3 == 2, f"children of two parents: each parent gains one child, none shared ({n2} under 2, {n3} under 3)", str(cp))
    for p in (2, 3):
        tot = sum(cs.get(c, 0) for c, q in cp.items() if q == p)
        check(tot == ps.get(str(p)), f"parent {p}'s children add up to it ({tot} of {ps.get(str(p))})")
    check(sorted(cs.get(c, 0) for c, q in cp.items() if q == 3)[0] == flagged3,
          f"the new child of parent 3 holds exactly child 3's flagged spikes ({flagged3})", str(cs))

    # A seed child: the template, never stripped.
    seed = scan(K, [10002, 20000], seed=10002, seedThreshold=True)
    check(seed.get("code") == 0 and seed.get("scored") == 1 and set(seed["by"]) <= {20000},
          f"seed child 10002: only child 20000 is scored and stripped ({sorted(seed['by'])})", " ".join(seed.get("lines", [])) + seed.get("reason", ""))
    check(any(l.startswith("template child 10002") for l in seed.get("lines", [])), "the report names the seed child", " ".join(seed.get("lines", [])))

    print(f"test_residual_strip_children: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
