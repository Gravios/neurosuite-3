#!/usr/bin/env python3
"""Clusters > Mark Collisions finds the rows that hold a second cluster's spike and names the partner.

Two synthetic units of 400 spikes and NO .eap.  A spike of unit 3 (0.9 of its mean waveform) is
added to the recording at 40 spikes of unit 2, 5-8 samples later or earlier, and a spike of unit 2
at 30 spikes of unit 3; the .spk is cut again.  Through the `markCollisions` step:

  - the scan of cluster 2 finds at least 34 of the 40 planted rows, at most 4 rows that hold
    no second spike, names cluster 3 as the partner and places it within a sample of where it was planted
    (bar at most 4); nothing is changed by a scan;
  - NEGATIVE CONTROL: the same session cut from the recording WITHOUT the added spikes gives at
    most 4 hits in each cluster beside the rows where the generator itself put a spike of each
    unit at one time (both units are drawn from one grid of times; about 18 of 400 coincide);
  - marked, the rows are served corrected -- far closer to the record they had before the partner
    was added -- with the .spk unchanged, two template classes made, and the document modified;
  - nothing is written before Save: a session closed without saving leaves no .eap and no .tcl;
  - the marks FOLLOW a move made before the Save: after a nudge of unit 2 by one sample the rows
    are still corrected, and the saved .eap holds each partner one sample earlier than it was found;
  - at Save the .eap and the .tcl are written: every row of a cluster names its class, the marked
    rows name the partner's as well, and the .tcl holds the two classes with the clusters they were
    made for;
  - opened again from the saved files, the marked rows are corrected and a new scan of cluster 2
    leaves them out as already named;
  - marks made AFTER a move: on the saved session (which has an .eap) unit 2 is nudged one sample,
    then cluster 3 is scanned and marked.  Its rows are corrected at once, no new class is made
    (both clusters have one), and the saved .eap holds the partner where it was found;
  - a row that names ANOTHER cluster's class (a sorter named every spike; the spike was moved
    since): on a session whose .eap gives every row a class, ten planted rows of cluster 2 name
    cluster 3's class and ten a second class of cluster 3.  Marked, each has that class taken out
    (or, when it is the partner's, turned into the partner's offset) and names cluster 2's; rows
    that are not marked keep what they name; no class is made;
  - an .eap wider than the default with no .tcl beside it: the .tcl written at Save is as wide;
  - with the preference off the rows are marked and written at Save all the same, and served raw;
  - in the child view the scan takes the rows of the selected CHILD and searches them as rows of
    their parent cluster.
"""
import os
import struct
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402

fails = 0
ABSENT = -128


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def read_eap(path):
    with open(path, "rb") as fh:
        magic = fh.read(4)
        n, t, group, flags = struct.unpack("<IIII", fh.read(16))
        fh.read(12)
        cells = np.fromfile(fh, np.int8).reshape(n, t)
    return magic, cells


def read_tcl(path):
    out = {}
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if len(f) >= 4 and f[0].strip().isdigit():
            out[int(f[0])] = (f[1].strip(), f[3].strip())
    return out


def build(d, name, plant=True, children=False):
    """The session; returns base, clu, the planted rows and lags of each cluster, the records before
    the partners were added."""
    nch = len(S.CHANS)
    base, m = S.make_session(d, name, n_per_unit=400)
    clu, res = m["clu"], m["res"]
    spk = m["spk"].reshape(-1, S.NSAMP, nch).astype(np.float64)
    a, b = np.flatnonzero(clu == 2), np.flatnonzero(clu == 3)
    rng = np.random.default_rng(23)
    lags = np.array([-8, -7, -6, -5, 5, 6, 7, 8])
    plan = {}
    fil = m["fil"].astype(np.float64)
    for own, other, count in ((a, b, 40), (b, a, 30)):
        rows = np.sort(rng.choice(own[5:-5], count, replace=False))
        tau = rng.choice(lags, count)
        plan[int(clu[own[0]])] = (rows, tau)
        add = 0.9 * spk[other].mean(0)
        if plant:
            for i, k in zip(rows, tau):
                s0 = int(res[i]) - (S.PEAK - 1) + int(k)
                fil[s0:s0 + S.NSAMP, S.CHANS] += add
    fil = np.clip(np.rint(fil), -32768, 32767).astype(np.int16)
    fil.tofile(base + ".fil")
    fil.tofile(base + ".dat")
    spk16 = S.cut(fil, res, S.CHANS, S.NSAMP, S.PEAK)
    spk16.tofile(f"{base}.spk.standard.1")
    if children:                                         # cluster 2: its planted rows are child 9
        clc = clu.copy()
        clc[plan[2][0]] = 9
        with open(f"{base}.clc.standard.1", "wb") as fh:
            np.array([np.unique(clc).size], np.int32).tofile(fh)
            clc.astype(np.int32).tofile(fh)
        clp = np.zeros(9, np.int32)
        clp[1], clp[2], clp[8] = 2, 3, 2
        with open(f"{base}.clp.standard.1", "wb") as fh:
            np.array([clp.size], np.int32).tofile(fh)
            clp.tofile(fh)
    # The generator puts both units on one grid of times, so some spikes of the two coincide in the
    # recording as it is: collisions nobody planted, and the scan is right to find them.
    truth = m["truth"]
    other = {2: np.sort(truth[clu == 3]), 3: np.sort(truth[clu == 2])}
    natural = {}
    for c in (2, 3):
        rows = np.flatnonzero(clu == c)
        k = np.clip(np.searchsorted(other[c], truth[rows]), 1, len(other[c]) - 1)
        near = np.minimum(np.abs(other[c][k] - truth[rows]), np.abs(other[c][k - 1] - truth[rows]))
        natural[c] = set(int(r) for r in rows[near <= S.NSAMP])
    build.natural = natural
    return base, clu, plan, spk, spk16.reshape(-1, S.NSAMP * nch)


def run(klusters, base, steps, d):
    rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
    st = rep.get("results", [])
    ok = rep.get("rc") == 0 and len(st) == len(steps)
    if not ok:
        print(rep.get("stderr", "")[-3000:])
    return ok, st


def found(result, cluster):
    return {h["row"]: h for h in result.get("hits", []) if h["cluster"] == cluster}


def served_ratio(result, rows, before, raw):
    recs = {r["row"]: r for r in result.get("records", [])}
    out = []
    for i in rows:
        e_raw = ((raw[i].astype(float) - before[i].ravel()) ** 2).sum()
        e_cor = ((np.array(recs[int(i)]["served"], float) - before[i].ravel()) ** 2).sum()
        out.append(e_cor / e_raw)
    return np.array(out), all(np.array_equal(np.array(recs[int(i)]["raw"], np.int16), raw[i]) for i in rows)


def main():
    klusters = R.skip_without_binary()

    # ── the negative control: nothing planted ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, _, _ = build(d, "clean", plant=False)
        ok, st = run(klusters, base, [{"markCollisions": {"clusters": [2]}}, {"markCollisions": {"clusters": [3]}}], d)
        check(ok, "the clean session opened and both scans ran")
        if ok:
            nat = build.natural
            n = [len([h for h in s.get("hits", []) if h["row"] not in nat[c]]) for s, c in zip(st, (2, 3))]
            seen = [len([h for h in s.get("hits", []) if h["row"] in nat[c]]) for s, c in zip(st, (2, 3))]
            print(f"      nothing planted: {seen} of {[len(nat[2]), len(nat[3])]} rows where the two units coincide in the recording are found; "
                  f"other hits {n}, over the threshold {[s.get('candidates') for s in st]}, "
                  f"control {[s.get('controlOver') for s in st]} of {[s.get('scanned') for s in st]}")
            check(max(n) <= 4, "NEGATIVE CONTROL: with no spike added, at most 4 rows are called in each cluster beside those where the units coincide", str(n))

    # ── scan, mark, close without saving ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = build(d, "mk")
        rows2, tau2 = plan[2]
        on_disk = open(f"{base}.spk.standard.1", "rb").read()
        ask = [int(i) for i in rows2]
        steps = [{"markCollisions": {"clusters": [2]}},
                 {"collisionRows": {"rows": ask[:3]}},
                 {"markCollisions": {"clusters": [2], "apply": True}},
                 {"collisionRows": {"rows": ask}}]
        ok, st = run(klusters, base, steps, d)
        check(ok, "the session opened and every step ran")
        if not ok:
            return 1
        scan = st[0]
        hits = found(scan, 2)
        got = [i for i in rows2 if int(i) in hits]
        extra = [r for r in hits if r not in set(ask) and r not in build.natural[2]]
        print(f"      cluster 2: {scan.get('scanned')} rows searched in {scan.get('milliseconds')} ms, "
              f"{scan.get('candidates')} over the threshold (control {scan.get('controlOver')}), {len(hits)} confirmed: "
              f"{len(got)} of 40 planted, {len(hits) - len(got) - len(extra)} where the units coincide anyway, {len(extra)} others")
        check(len(got) >= 34, "at least 34 of the 40 planted rows are found", str(len(got)))
        check(len(extra) <= 4, "at most 4 rows that hold no second spike are called", str(extra))
        check(all(h["partner"] == 3 for h in hits.values()), "the partner named is cluster 3")
        off = [abs(hits[int(i)]["tau"] - int(k)) for i, k in zip(rows2, tau2) if int(i) in hits]
        check(sum(o > 1 for o in off) <= 4, "the partner is placed within a sample of where it was planted (bar at most 4)",
              str(sorted(off)[-6:]))
        check(scan.get("pending") == 0 and not scan.get("modified") and st[1].get("corrected") == 0,
              "a scan changes nothing: no mark pending, the document not modified, no row corrected")
        mark = st[2]
        check(mark.get("markOk") and mark.get("marked") == len(mark.get("hits", [])) and mark.get("pending") == mark.get("marked"),
              "every confirmed row is marked", str({k: mark.get(k) for k in ("markOk", "markError", "marked", "pending")}))
        check(mark.get("classesCreated") == 2 and mark.get("strayRows") == 0 and mark.get("withoutClass") == 0,
              "two template classes are made, one for each cluster", str(mark.get("classesCreated")))
        check(mark.get("modified") is True, "the document is modified: the marks are there to save")
        marked = sorted(found(mark, 2))
        col = st[3]
        check(col.get("corrected") == len(marked) and col.get("active") == len(marked),
              "the marked rows are corrected and served", str({k: col.get(k) for k in ("collisionRows", "corrected", "active")}))
        mine = [i for i in rows2 if int(i) in marked]
        ratio, raw_ok = served_ratio(col, mine, before, raw)
        print(f"      marked rows: error against the record before the partner was added, as a share of the raw row's: "
              f"median {np.median(ratio):.3f}, worst {ratio.max():.3f}")
        check(np.median(ratio) < 0.10, "a marked row is served corrected (median error under a tenth of the raw row's)")
        check(raw_ok and open(f"{base}.spk.standard.1", "rb").read() == on_disk, "the .spk is unchanged, in memory and on disk")
        check(not os.path.exists(f"{base}.eap.1") and not os.path.exists(f"{base}.tcl.1"),
              "closed without saving: no .eap and no .tcl were written")

        # ── marked, moved, saved ──
        steps = [{"markCollisions": {"clusters": [2], "apply": True}},
                 {"nudge": [2, 1]},
                 {"collisionRows": {"rows": ask}},
                 {"save": True},
                 {"collisionRows": {}}]
        ok, st = run(klusters, base, steps, d)
        check(ok, "marked, nudged and saved")
        if not ok:
            return 1
        hits = found(st[0], 2)
        marked = sorted(hits)
        check(st[2].get("corrected") == len(marked), "after a nudge of unit 2 the marked rows are still corrected",
              str({k: st[2].get(k) for k in ("collisionRows", "corrected", "rowsMoved")}))
        check(st[4].get("corrected") == len(marked) and st[4].get("rowsMoved") == 0,
              "after Save they are corrected from the saved files, and no row counts as moved",
              str({k: st[4].get(k) for k in ("collisionRows", "corrected", "rowsMoved", "eap")}))
        check(os.path.exists(f"{base}.eap.1") and os.path.exists(f"{base}.tcl.1"), "Save wrote the .eap and the .tcl")
        if not os.path.exists(f"{base}.eap.1"):
            return 1
        magic, cells = read_eap(f"{base}.eap.1")
        tcl = read_tcl(f"{base}.tcl.1")
        active = {c: v for c, v in tcl.items() if v[0] == "active"}
        made = {int(v[1]): c for c, v in active.items()}
        check(magic == b"EAP\x01" and cells.shape[0] == len(clu) and sorted(made) == [2, 3],
              "the .tcl holds one class for cluster 2 and one for cluster 3", str(active))
        c2, c3 = made.get(2, 0), made.get(3, 1)
        present = cells != ABSENT
        check(present[clu == 2, c2].all() and present[clu == 3, c3].all(), "every row names its own cluster's class")
        two = np.flatnonzero(present.sum(1) == 2)
        check(list(two) == marked and present[two, c3].all(), "the marked rows, and only they, name cluster 3's class as well")
        want = np.array([hits[r]["tau"] - 1 for r in marked])
        diff = np.abs(cells[marked, c3].astype(int) - want)
        check((diff <= 1).sum() >= len(marked) - 2,
              "the saved offsets are for the saved times: one sample earlier than found, unit 2 having moved one later",
              str(sorted(diff)[-5:]))

        # ── opened again; marks made after a move, on a session that has an .eap ──
        rows3, tau3 = plan[3]
        steps = [{"collisionRows": {}},
                 {"markCollisions": {"clusters": [2]}},
                 {"nudge": [2, 1]},
                 {"markCollisions": {"clusters": [3], "apply": True}},
                 {"collisionRows": {}},
                 {"save": True},
                 {"collisionRows": {}}]
        ok, st = run(klusters, base, steps, d)
        check(ok, "the saved session opened and every step ran")
        if not ok:
            return 1
        check(st[0].get("corrected") == len(marked), "opened from the saved files, the marked rows are corrected",
              str({k: st[0].get(k) for k in ("collisionRows", "corrected")}))
        again = st[1]
        check(again.get("alreadyNamed") == len(marked) and len([h for h in again.get("hits", []) if h["row"] in set(ask)]) <= 6,
              "a new scan leaves the marked rows out: they name their partner already",
              str({k: again.get(k) for k in ("alreadyNamed", "scanned")}))
        m3 = st[3]
        hits3 = found(m3, 3)
        got3 = [i for i in rows3 if int(i) in hits3]
        print(f"      cluster 3, after a nudge of unit 2: {len(hits3)} confirmed, {len(got3)} of 30 planted")
        others3 = [r for r in hits3 if r not in set(int(i) for i in rows3) and r not in build.natural[3]]
        check(len(got3) >= 25 and len(others3) <= 4, "cluster 3: at least 25 of the 30 planted rows, at most 4 rows without a second spike",
              f"{len(got3)} planted, {len(others3)} others")
        check(m3.get("classesCreated") == 0 and m3.get("marked") == len(hits3), "no class is made: both clusters have one",
              str({k: m3.get(k) for k in ("classesCreated", "marked", "strayRows", "withoutClass")}))
        check(st[4].get("corrected") == len(marked) + len(hits3), "the rows of both clusters are corrected",
              str({k: st[4].get(k) for k in ("collisionRows", "corrected")}))
        check(st[6].get("corrected") == len(marked) + len(hits3), "... and after Save, from the saved files",
              str({k: st[6].get(k) for k in ("collisionRows", "corrected", "rowsMoved")}))
        _, cells = read_eap(f"{base}.eap.1")
        r3 = sorted(hits3)
        diff = np.abs(cells[r3, c2].astype(int) - np.array([hits3[r]["tau"] for r in r3]))
        check((cells[r3, c2] != ABSENT).all() and (diff <= 1).sum() >= len(r3) - 2,
              "the saved .eap holds unit 2 in cluster 3's rows where it was found after the move", str(sorted(diff)[-5:]))

    # ── rows naming another cluster's class ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = build(d, "stray")
        rows2 = [int(i) for i in plan[2][0]]
        as3, as_other = rows2[:10], rows2[10:20]
        b_rows = np.flatnonzero(clu == 3)
        cells = np.full((len(clu), 4), ABSENT, np.int8)
        cells[clu == 2, 0] = 0
        cells[b_rows, 1] = 0
        cells[b_rows[:60], 1] = ABSENT; cells[b_rows[:60], 2] = 0      # a second class of cluster 3
        for r in as3:
            cells[r, 0] = ABSENT; cells[r, 1] = 0
        for r in as_other:
            cells[r, 0] = ABSENT; cells[r, 2] = 0
        with open(f"{base}.eap.1", "wb") as fh:
            fh.write(b"EAP\x01" + struct.pack("<IIII", len(clu), 4, 1, 0) + b"\x00" * 12)
            cells.tofile(fh)
        ok, st = run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}},
                                      {"collisionRows": {}}, {"save": True}, {"collisionRows": {}}], d)
        check(ok, "rows naming another cluster's class: every step ran")
        if ok:
            m = st[0]
            hits = found(m, 2)
            moved = [r for r in as3 + as_other if r in hits]
            print(f"      {len(hits)} marked; {len(moved)} of the 20 rows naming another cluster's class among them "
                  f"(reported: {m.get('strayRows')})")
            check(len(moved) >= 16 and m.get("strayRows") == len(moved) and m.get("marked") == len(hits),
                  "they are marked like the others, and counted", str({k: m.get(k) for k in ("marked", "strayRows", "withoutClass")}))
            check(m.get("classesCreated") == 0, "no class is made: every cluster has one")
            check(st[1].get("corrected") == len(hits) and st[3].get("corrected") == len(hits),
                  "all marked rows are corrected, before and after Save",
                  str([{k: x.get(k) for k in ("collisionRows", "corrected")} for x in (st[1], st[3])]))
            _, saved = read_eap(f"{base}.eap.1")
            mk = sorted(hits)
            own_ok = all(saved[r, 0] != ABSENT and saved[r, 1] != ABSENT and saved[r, 2] == ABSENT for r in mk)
            check(own_ok, "saved: every marked row names cluster 2's class and cluster 3's, and no other")
            near = [abs(int(saved[r, 1]) - hits[r]["tau"]) <= 1 for r in mk]
            check(sum(near) >= len(mk) - 2, "saved: the partner where it was found, also where the row named it before")
            rest = np.setdiff1d(np.arange(len(clu)), mk)
            check(np.array_equal(saved[rest], cells[rest]), "saved: rows that were not marked keep what they named")

    # ── an .eap wider than the default and no .tcl: the registry written is as wide as the table ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = build(d, "wide")
        cells = np.full((len(clu), 256), ABSENT, np.int8)
        cells[clu == 2, 0] = 0                                    # cluster 3 names no class
        with open(f"{base}.eap.1", "wb") as fh:
            fh.write(b"EAP\x01" + struct.pack("<IIII", len(clu), 256, 1, 0) + b"\x00" * 12)
            cells.tofile(fh)
        ok, st = run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}}, {"save": True}, {"collisionRows": {}}], d)
        check(ok, "wide table: every step ran")
        if ok:
            _, saved = read_eap(f"{base}.eap.1")
            tcl_lines = [ln for ln in open(f"{base}.tcl.1") if ln.startswith("nClasses")]
            width = int(tcl_lines[0].split()[1]) if tcl_lines else -1
            tcl = read_tcl(f"{base}.tcl.1")
            print(f"      a 256-column .eap and no .tcl: the saved .eap has {saved.shape[1]} columns, the .tcl {width}")
            check(st[0].get("classesCreated") == 1 and saved.shape[1] == 256, "a class is made for cluster 3; the .eap keeps its width",
                  str(st[0].get("classesCreated")))
            check(width == 256 and len(tcl) == 256, "the .tcl written is as wide as the .eap", f"{width} vs {saved.shape[1]}")
            check(st[2].get("corrected") == st[0].get("marked") and st[0].get("marked") >= 34,
                  "the marked rows are corrected from the saved files", str({k: st[2].get(k) for k in ("collisionRows", "corrected")}))

    # ── the preference off ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = build(d, "off")
        ask = [int(i) for i in plan[2][0]]
        steps = [{"collisionRows": {"prefs": {"enabled": False}, "refresh": True}},
                 {"markCollisions": {"clusters": [2], "apply": True}},
                 {"collisionRows": {"rows": ask}},
                 {"save": True},
                 {"collisionRows": {}}]
        ok, st = run(klusters, base, steps, d)
        check(ok, "preference off: every step ran")
        if ok:
            n = st[1].get("marked")
            recs = st[2].get("records", [])
            check(n >= 34 and all(r["served"] == r["raw"] for r in recs), "preference off: the rows are marked and served raw", str(n))
            okf = os.path.exists(f"{base}.eap.1")
            check(okf, "preference off: Save writes the marks all the same")
            if okf:
                _, cells = read_eap(f"{base}.eap.1")
                check(int(((cells != ABSENT).sum(1) == 2).sum()) == n, "... every marked row, with its partner")

    # ── a child selected in the child view ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, _, _ = build(d, "kid", children=True)
        ok, st = run(klusters, base, [{"markCollisions": {"clusters": [], "children": True, "childOfRow": int(plan[2][0][0])}}], d)
        check(ok, "child view: the scan ran")
        if ok:
            s = st[0]
            hits = s.get("hits", [])
            kid = (s.get("searched") or [0])[0]
            per = s.get("perCluster", {}).get(str(kid), {})
            print(f"      child {kid} (the 40 planted rows of cluster 2): {per.get('rows')} rows searched, {len(hits)} confirmed")
            check(per.get("rows") == 40 and s.get("scanned") == 40, "the rows searched are the child's", str(per))
            check(len(hits) >= 30 and all(h["cluster"] == 2 and h["partner"] == 3 for h in hits),
                  "they are searched as rows of their parent, cluster 2, and found with cluster 3 as partner", str(len(hits)))

    print("FAILED" if fails else "PASSED", f"({fails} failure(s))" if fails else "")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
