#!/usr/bin/env python3
"""Clusters > Remove Duplicate Spikes finds the rows that hold a spike another row of the cluster holds.

Two synthetic units of 400 spikes at 20 kHz.  Rows are ADDED to the session (res, clu, spk, fet,
kept in time order):

  - 12 spikes of cluster 2 a second time, at the very same time (the same record);
  - 8 spikes of cluster 2 a second time, cut 3 samples later (a detector that fired twice);
  - 4 spikes of cluster 2 a second time, cut 9 samples later -- beyond 0.2 ms (4 samples);
  - 5 spikes of cluster 3 a second time at the same time, labelled cluster 2 (one spike sorted
    into two clusters).

Through the `duplicates` step:

  - the tolerance is 0.2 ms: 4 samples at 20 kHz;
  - cluster 2: 20 spikes are held by two rows, 12 of them at one time; each run is an added row
    and the row it copies.  Of two rows at one time the lower is kept; of a row and its copy 3
    samples late the one more like the cluster's template -- the row in place, mostly (the
    generator's own times are off by up to 2 samples, so a late copy can be the better aligned);
  - NEGATIVE CONTROLS: the copies 9 samples late are in no run; cluster 3, which has no row
    twice, has none; a tolerance of 0 finds only the 12 at one time;
  - rows this near in two DIFFERENT clusters are counted and not among the rows too many;
  - nothing is changed by a scan; applied, the 20 rows are in the artefact cluster after Save,
    every other row keeps its cluster, and a new scan finds none;
  - in the child view the scan works on the selected child: with cluster 2's rows dealt to two
    children by turns, a row and its copy are in different children and neither child has a run.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def build(d, name, children=False):
    nch = len(S.CHANS)
    base, m = S.make_session(d, name, n_per_unit=400)
    clu, res = m["clu"].astype(np.int32), m["res"].astype(np.int64)
    fil = m["fil"]
    spk = S.read_spk(f"{base}.spk.standard.1", S.NSAMP, nch)
    fet = S.read_fet(f"{base}.fet.standard.1")
    n = len(clu)
    rng = np.random.default_rng(5)
    a = np.flatnonzero(clu == 2)[20:-20]
    b = np.flatnonzero(clu == 3)[20:-20]
    pick = rng.choice(a, 24, replace=False)
    same, late3, late9 = pick[:12], pick[12:20], pick[20:]
    twice = rng.choice(b, 5, replace=False)
    add_res, add_clu, add_src, kind = [], [], [], []
    for src, shift, label, k in ([(r, 0, 2, "same") for r in same] + [(r, 3, 2, "late3") for r in late3]
                                 + [(r, 9, 2, "late9") for r in late9] + [(r, 0, 2, "other") for r in twice]):
        add_res.append(res[src] + shift); add_clu.append(label); add_src.append(src); kind.append(k)
    add_res = np.array(add_res, np.int64)
    add_spk = S.cut(fil.astype(np.int16) if fil.dtype != np.int16 else fil, add_res, S.CHANS, S.NSAMP, S.PEAK).reshape(len(add_res), -1)
    add_fet = fet[add_src].copy()
    add_fet[:, -1] = add_res
    res2 = np.concatenate([res, add_res]); clu2 = np.concatenate([clu, np.array(add_clu, np.int32)])
    spk2 = np.concatenate([spk, add_spk]); fet2 = np.concatenate([fet, add_fet])
    origin = np.concatenate([np.arange(n), np.array(add_src)])          # the row a row is a copy of (itself for the originals)
    is_new = np.concatenate([np.zeros(n, bool), np.ones(len(add_res), bool)])
    kinds = np.array([""] * n + kind)
    order = np.argsort(res2, kind="stable")                             # an original before its copy at the same time
    res2, clu2, spk2, fet2, origin, is_new, kinds = res2[order], clu2[order], spk2[order], fet2[order], origin[order], is_new[order], kinds[order]
    where = np.empty(len(order), np.int64); where[order] = np.arange(len(order))
    origin = where[origin]                                              # in the new numbering
    res2.astype(np.int64).tofile(f"{base}.res.standard.1")
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([int(clu2.max()) + 1], np.int32).tofile(fh)
        clu2.astype(np.int32).tofile(fh)
    spk2.astype(np.int16).tofile(f"{base}.spk.standard.1")
    with open(f"{base}.fet.standard.1", "wb") as fh:
        np.array([fet2.shape[1]], np.int32).tofile(fh)
        fet2.tofile(fh)
    if children:                                                        # cluster 2's rows dealt to two children by turns
        clc = clu2.copy()
        rows = np.flatnonzero(clu2 == 2)
        clc[rows[1::2]] = 9
        with open(f"{base}.clc.standard.1", "wb") as fh:
            np.array([np.unique(clc).size], np.int32).tofile(fh)
            clc.astype(np.int32).tofile(fh)
        clp = np.zeros(9, np.int32)
        clp[1], clp[2], clp[8] = 2, 3, 2
        with open(f"{base}.clp.standard.1", "wb") as fh:
            np.array([clp.size], np.int32).tofile(fh)
            clp.tofile(fh)
    return base, res2, clu2, origin, is_new, kinds


def run(klusters, base, steps, d):
    rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
    st = [x for x in rep.get("results", []) if "step" in x]
    ok = rep.get("rc") == 0 and len(st) == len(steps)
    if not ok:
        print(rep.get("stderr", "")[-3000:])
    return ok, st


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, res, clu, origin, is_new, kinds = build(d, "dup")
        want = sorted(int(i) for i in np.flatnonzero(is_new & np.isin(kinds, ["same", "late3"])))
        # rows of two different real clusters next to each other within 4 samples, as the scan counts them
        real = np.flatnonzero(clu > 1)
        t, c = res[real], clu[real]
        across = int(((np.diff(t) <= 4) & (c[1:] != c[:-1])).sum())
        steps = [{"duplicates": {"clusters": [2]}},
                 {"duplicates": {"clusters": [3]}},
                 {"duplicates": {"clusters": [2], "toleranceMs": 0.0}},
                 {"duplicates": {"clusters": [2], "apply": True}},
                 {"duplicates": {"clusters": [2]}},
                 {"save": True}]
        ok, st = run(klusters, base, steps, d)
        check(ok, "the session opened and every step ran")
        if not ok:
            return 1
        s = st[0]
        print(f"      cluster 2: {s.get('scanned')} rows, tolerance {s.get('toleranceSamples')} samples: {s.get('runs')} spikes held twice "
              f"({s.get('atOneTime')} at one time), {len(s.get('extras', []))} rows too many; "
              f"{s.get('acrossClusters')} pairs across clusters {s.get('acrossPairs')}")
        check(s.get("toleranceSamples") == 4, "0.2 ms is 4 samples at 20 kHz", str(s.get("toleranceSamples")))
        check(s.get("runs") == 20 and s.get("atOneTime") == 12, "20 spikes of cluster 2 are held by two rows, 12 of them at one time",
              f"{s.get('runs')}, {s.get('atOneTime')}")
        runs = [sorted(r) for r in s.get("runRows", [])]
        pairs = sorted(sorted((int(origin[i]), int(i))) for i in want)
        check(sorted(runs) == pairs, "each run is one of the added copies and the row it copies",
              f"{len(runs)} runs, {len(pairs)} pairs")
        kept = {r[0] for r in s.get("runRows", [])}
        same = [int(i) for i in np.flatnonzero(is_new & (kinds == "same"))]
        late = [int(i) for i in np.flatnonzero(is_new & (kinds == "late3"))]
        check(all(int(origin[i]) in kept for i in same), "of two rows at one time (one record) the lower row is kept")
        in_place = sum(int(origin[i]) in kept for i in late)
        print(f"      of a row and its copy cut 3 samples late, the row in place is kept in {in_place} of {len(late)} "
              f"(the generator's own times are off by up to 2 samples: where the copy is kept it is the better aligned)")
        check(in_place >= 6, "of a row and its copy 3 samples late the one like the cluster's template is kept: the row in place, mostly")
        check(len(s.get("extras", [])) == 20 and all(len(r) == 2 for r in runs), "20 rows too many, one of each run")
        late9 = set(int(i) for i in np.flatnonzero(kinds == "late9"))
        in_runs = {r for run_ in s.get("runRows", []) for r in run_}
        check(not (late9 & in_runs) and not (set(int(origin[i]) for i in late9) & in_runs),
              "NEGATIVE CONTROL: a copy 9 samples late is no duplicate (beyond 0.2 ms)")
        check(st[1].get("runs") == 0 and st[1].get("extras") == [], "NEGATIVE CONTROL: cluster 3 has no spike twice",
              str(st[1].get("runs")))
        check(st[2].get("toleranceSamples") == 0 and st[2].get("runs") == 12 and st[2].get("atOneTime") == 12,
              "NEGATIVE CONTROL: with a tolerance of 0 only the 12 at one time are found", str(st[2].get("runs")))
        other = set(int(i) for i in np.flatnonzero(kinds == "other"))
        check(s.get("acrossClusters") == across and across >= 5 and s.get("acrossPairs") == {"2+3": across},
              "rows this near in two different clusters are counted", f"{s.get('acrossClusters')} vs {across}")
        check(st[1].get("acrossClusters") == across, "... the same from the other cluster's side")
        check(not (other & set(s.get("extras", []))) and not s.get("modified"), "... and are not among the rows too many; a scan changes nothing")
        check(st[4].get("runs") == 0 and st[4].get("scanned") == s.get("scanned") - 20, "applied: cluster 2 holds 20 rows fewer and no spike twice",
              f"{st[4].get('runs')}, {st[4].get('scanned')}")
        saved = S.read_clu(f"{base}.clu.standard.1")
        moved = [int(i) for i in np.flatnonzero(saved != clu)]
        check(moved == s.get("extras") and (saved[moved] == 0).all(), "saved: the 20 rows are in the artefact cluster, every other row where it was",
              f"{len(moved)} rows changed, to {sorted(set(saved[moved].tolist()))}")

    with tempfile.TemporaryDirectory() as d:
        base, res, clu, origin, is_new, kinds = build(d, "kid", children=True)
        rows2 = np.flatnonzero(clu == 2)
        steps = [{"duplicates": {"clusters": [], "children": True, "childOfRow": int(rows2[0])}},
                 {"duplicates": {"clusters": [], "children": True, "childOfRow": int(rows2[1])}}]
        ok, st = run(klusters, base, steps, d)
        check(ok, "child view: both scans ran")
        if ok:
            sizes = [s.get("scanned") for s in st]
            print(f"      child view: cluster 2's {len(rows2)} rows dealt by turns to two children of {sizes} rows; runs {[s.get('runs') for s in st]}")
            check(sum(sizes) == len(rows2), "the rows looked at are the selected child's")
            # a row and its copy are neighbours in time, so they sit in different children
            check(all(s.get("runs") <= 2 for s in st), "a run is looked for inside the selected child: a spike held by rows of two children is not one",
                  str([s.get("runs") for s in st]))

    print("FAILED" if fails else "PASSED", f"({fails} failure(s))" if fails else "")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
