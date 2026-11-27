#!/usr/bin/env python3
"""Add Residual Spikes as Clusters: the partners' spikes no row holds become rows of new clusters.

The session of test_mark_collisions: a spike of unit 3 (0.9 of its mean waveform) is added to the
RECORDING at 40 spikes of unit 2, 5-8 samples away, and a spike of unit 2 at 30 spikes of unit 3.
Those are rows of no cluster.  About 18 spikes of each unit also coincide with a spike of the other
that IS a row.  Through the `residualSpikes` step (the action without its dialog, the new stage
written but not opened):

  - NEGATIVE CONTROL: before any mark there is no residual and nothing to add;
  - after Mark Collisions on cluster 2 (saved): one spike per marked planted row, named for
    cluster 3, its time within 1 sample of where the spike was planted -- aligned at its own
    peak, not left where the fit put it;
  - a spike of unit 3 that is a row already is NOT added (counted as known);
  - the stage <name>.rs holds every old row, in order and byte for byte (.res, .clu, .spk, .fet,
    .eap), and the new rows in ONE new cluster (4, for partner 3), each beside its source row;
    the .res stays in time order;
  - a new row's .spk record is the added spike, centred (median error under 15 % of its energy),
    its features the projection of that record on the session's .pca with its time last, its
    .eap row names no class;
  - the open stage's files are not changed;
  - the new stage opens (on its own rows), and asked again adds nothing: every residual of
    cluster 2 is now a row;
  - with BOTH clusters marked there are two new clusters, one per partner;
  - a hierarchical session: the new cluster is one child, its self child, in the stage's
    .clc / .clp, and the old children are kept.
"""
import hashlib
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402
import test_mark_collisions as M          # noqa: E402

check = M.check
NCH = len(S.CHANS)
REC = S.NSAMP * NCH


def digest(paths):
    return {p: hashlib.sha1(open(p, "rb").read()).hexdigest() for p in paths}


def read_eap_cells(path, n):
    b = np.fromfile(path, np.int8)
    return b[len(b) - (len(b) // n) * n:].reshape(n, -1) if n else b      # the cells are the file's tail


def stage(base, tag):
    return dict(clu=S.read_clu(f"{base}.clu.standard.1.{tag}"), res=S.read_res(f"{base}.res.standard.1.{tag}"),
                spk=S.read_spk(f"{base}.spk.standard.1.{tag}", S.NSAMP, NCH), fet=S.read_fet(f"{base}.fet.standard.1.{tag}"))


def pca_of(base):
    """means[ch][nsamp], evecs[ch][ncomp * nsamp] of the session's .pca (session.write_pca's layout)."""
    b = open(f"{base}.pca.standard.1", "rb").read()
    v = np.frombuffer(b, np.float64, offset=36)
    means = [v[c * S.NSAMP:(c + 1) * S.NSAMP] for c in range(NCH)]
    at = NCH * S.NSAMP
    step = S.NCOMP * S.NSAMP
    evecs = [v[at + c * step:at + (c + 1) * step] for c in range(NCH)]
    return means, evecs


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "rs")
        res = S.read_res(f"{base}.res.standard.1")
        fet = S.read_fet(f"{base}.fet.standard.1")
        n = len(clu)
        rows2, lag2 = plan[2]
        planted = {int(r): int(k) for r, k in zip(rows2, lag2)}
        add = (0.9 * before[clu == 3].mean(0)).reshape(-1)

        # (a step that finds nothing ends the run: read its result, not the run's status)
        rep = R.run(klusters, f"{base}.clu.standard.1", [{"residualSpikes": {"clusters": [2, 3], "apply": True}}], d)
        st = [x for x in rep.get("results", []) if "step" in x]
        check(len(st) == 1 and st[0].get("ok") is False and st[0].get("candidates") == 0 and st[0].get("spikes") == []
              and not st[0].get("written", False) and not os.path.exists(f"{base}.clu.standard.1.rs"),
              "NEGATIVE CONTROL: before any mark there is no residual, nothing to add, no stage written",
              str({x: st[0].get(x) for x in ("ok", "reason", "candidates", "written")}) if st else "")

        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}}, {"save": True}], d)
        check(ok, "cluster 2 marked and saved")
        if not ok:
            return 1
        marked = sorted(h["row"] for h in st[0].get("hits", []))
        own = [f"{base}.{t}.standard.1" for t in ("clu", "res", "spk", "fet")] + [f"{base}.eap.1"]
        was = digest(own)

        ok, st = M.run(klusters, base, [{"residualSpikes": {"clusters": [2], "apply": True}}], d)
        check(ok and st[0].get("written") is True, "the residual spikes of cluster 2 were found and the stage written",
              str(st[0].get("reason")) if ok else "")
        if not ok:
            return 1
        r = st[0]
        spikes = r["spikes"]
        by_row = {s["sourceRow"]: s for s in spikes}
        want = [m for m in marked if m in planted]
        nat = [m for m in marked if m in M.build.natural[2]]
        off = [abs(s["time"] - (int(res[s["sourceRow"]]) + planted[s["sourceRow"]])) for s in spikes if s["sourceRow"] in planted]
        print(f"      {len(marked)} marked rows: {len(want)} planted, {len(nat)} whose partner is a row; {len(spikes)} spikes to add "
              f"(known {r.get('known')}, on a row once aligned {r.get('alreadyRow')}, repeated {r.get('repeated')}); "
              f"moved by the alignment {r.get('moved')}; time against the planted one: max {max(off) if off else '-'} sample(s)")
        check(sorted(by_row) == want and len(spikes) == len(want) and len(want) >= 35, "one spike per marked planted row",
              f"{len(spikes)} spikes, {len(want)} planted and marked")
        check(all(s["partner"] == 3 for s in spikes), "each is named for cluster 3")
        check(len(off) == len(spikes) and max(off) <= 1, "each is aligned at its peak: within 1 sample of where the spike was planted",
              str(sorted(off)[-5:]))
        check(not any(m in by_row for m in nat) and r.get("known") + r.get("alreadyRow") >= len(nat) and len(nat) >= 10,
              "a spike of unit 3 that is a row already is not added", f"known {r.get('known')}, natural {len(nat)}")
        check(r.get("source", "").endswith(".fil") and r.get("methods") == ["standard"], "cut from the .fil", str(r.get("source")))
        check(r.get("newClusters") == {"3": 4}, "partner 3 gets the new cluster 4", str(r.get("newClusters")))
        check(r.get("stage", "").endswith(".clu.standard.1.rs"), "the stage is <name>.rs", str(r.get("stage")))
        check(digest(own) == was and r.get("modified") is False, "the open stage's files are not changed")

        g = stage(base, "rs")
        k = len(spikes)
        check(len(g["clu"]) == n + k == len(g["res"]) == len(g["spk"]) == len(g["fet"]), "the stage holds the old rows and the new ones",
              str([len(g[x]) for x in ("clu", "res", "spk", "fet")]))
        new = np.flatnonzero(g["clu"] == 4)
        old = np.flatnonzero(g["clu"] != 4)
        check(len(new) == k and int(np.fromfile(f"{base}.clu.standard.1.rs", np.int32, 1)[0]) == 3,
              "the new rows are ONE new cluster, and the .clu counts three", str(len(new)))
        check(np.array_equal(g["clu"][old], clu) and np.array_equal(g["res"][old], res) and np.array_equal(g["spk"][old], raw)
              and np.array_equal(g["fet"][old], fet), "every old row is there, in order, as it was (.clu .res .spk .fet)")
        check(bool(np.all(np.diff(g["res"]) >= 0)), "the .res is still in time order")
        times = sorted(s["time"] for s in spikes)
        check(sorted(int(t) for t in g["res"][new]) == times, "the new rows carry the aligned times")
        # each new row sits beside its source row: the old row before or after it is the source
        beside = 0
        for i in new:
            src = [s["sourceRow"] for s in spikes if s["time"] == int(g["res"][i])]
            near = set(int(np.searchsorted(old, i)) + j for j in (-2, -1, 0, 1))        # old-row numbers around position i
            beside += bool(set(src) & near)
        check(beside == k, "each new row is beside the row it was found in", f"{beside} of {k}")

        err = [float(((g["spk"][i].astype(float) - add) ** 2).sum() / (add ** 2).sum()) for i in new]
        print(f"      a new row's record against the added spike, error as a share of its energy: median {np.median(err):.3f}, worst {max(err):.3f}")
        check(np.median(err) < 0.15, "a new row's record is the added spike, centred (median error under 15 % of its energy)")
        means, evecs = pca_of(base)
        proj = S.project(g["spk"][new], means, evecs, NCH, S.NSAMP, S.NCOMP)
        dev = np.abs(g["fet"][new][:, :-1] - proj).max()
        check(dev <= 1 and np.array_equal(g["fet"][new][:, -1], g["res"][new]),
              "its features are the projection of that record on the .pca, its time last", f"max deviation {dev}")

        e0 = read_eap_cells(f"{base}.eap.1", n)
        e1 = read_eap_cells(f"{base}.eap.1.rs", n + k)
        check(e1.shape[1] == e0.shape[1] and np.array_equal(e1[old], e0) and bool(np.all(e1[new] == -128)),
              "the .eap is carried: old rows as they were, a new row names no class", str((e0.shape, e1.shape)))
        check(not os.path.exists(f"{base}.spkr.standard.1.rs") and not os.path.exists(f"{base}.spkc.standard.1.rs"),
              "no .spkc / .spkr is carried (they index rows that have moved)")

        # the new stage opens, and has nothing more to add
        rep = R.run(klusters, f"{base}.clu.standard.1.rs", [{"collisionRows": {}}, {"residualSpikes": {"clusters": [2], "apply": True}},
                                                            {"duplicates": {"clusters": [3, 4]}}], d, timeout=300)
        st = [x for x in rep.get("results", []) if "step" in x]
        ok = rep.get("rc") == 0 and len(st) == 3
        check(ok, "the new stage opens")
        if ok:
            check(st[0].get("collisionRows") == len(marked), "its marked rows are the same rows", str(st[0].get("collisionRows")))
            check(st[1].get("candidates") == len(marked) and st[1].get("spikes") == [] and st[1].get("known") == len(marked)
                  and not st[1].get("written", False) and not os.path.exists(f"{base}.clu.standard.1.rs_rs"),
                  "asked again it adds nothing: every residual is a row now", str({x: st[1].get(x) for x in ("candidates", "known", "alreadyRow")}))
            check(st[2].get("scanned") == int((clu == 3).sum()) + k and st[2].get("runs") == 0 and "3+4" not in st[2].get("acrossPairs", {}),
                  "no added spike is within the duplicate tolerance of a row of cluster 3",
                  str({x: st[2].get(x) for x in ("scanned", "runs", "acrossPairs")}))

    # ── both marked: a new cluster per partner ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "two")
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}},
                                        {"markCollisions": {"clusters": [3], "apply": True}}, {"save": True}], d)
        ok2, st2 = M.run(klusters, base, [{"residualSpikes": {"clusters": [2, 3], "apply": True}}], d) if ok else (False, [])
        check(ok and ok2 and st2[0].get("written") is True, "both clusters marked, the stage written")
        if ok and ok2:
            r = st2[0]
            made = r.get("newClusters")
            g = stage(base, "rs")
            a, b = int((g["clu"] == 4).sum()), int((g["clu"] == 5).sum())
            per = {p: sum(1 for s in r["spikes"] if s["partner"] == p) for p in (2, 3)}
            print(f"      both marked: {len(r['spikes'])} spikes, partner 2 -> cluster {made.get('2')} ({a}), partner 3 -> cluster {made.get('3')} ({b})")
            check(made == {"2": 4, "3": 5} and a == per[2] and b == per[3] and a >= 20 and b >= 30,
                  "two new clusters, one per partner, each with that partner's spikes", str((made, a, b, per)))
            src = {s["time"]: s for s in r["spikes"]}
            wrong = sum(1 for i in np.flatnonzero(g["clu"] >= 4) if clu[src[int(g["res"][i])]["sourceRow"]] == src[int(g["res"][i])]["partner"])
            check(wrong == 0, "no spike is named for the cluster of the row it was found in")

    # ── hierarchical ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "kid", children=True)
        n = len(clu)
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}}, {"save": True}], d)
        ok2, st2 = M.run(klusters, base, [{"residualSpikes": {"clusters": [2], "apply": True}}], d) if ok else (False, [])
        check(ok and ok2 and st2[0].get("written") is True, "a hierarchical session: the stage written")
        if ok and ok2 and os.path.exists(f"{base}.clc.standard.1.rs"):
            g = S.read_clu(f"{base}.clu.standard.1.rs")
            c = S.read_clu(f"{base}.clc.standard.1.rs")
            p = np.fromfile(f"{base}.clp.standard.1.rs", np.int32)
            c0 = S.read_clu(f"{base}.clc.standard.1")            # as the save left it
            new = g == 4
            check(len(c) == len(g) and bool(np.all(c[new] == 10004)), "the new cluster is one child, its self child (10004)",
                  str(np.unique(c[new])))
            groups = lambda x: sorted(tuple(np.flatnonzero(x == v)) for v in np.unique(x))      # noqa: E731
            check(groups(c[~new]) == groups(c0) and len(np.unique(c0)) >= 3, "the old rows keep their children")
            check(p[0] == len(p) - 1 and p[10004] == 4 and all(p[v] == g[np.flatnonzero(c == v)[0]] for v in np.unique(c) if v > 0),
                  "the .clp names every child's parent, the new child's is cluster 4")
            rep = R.run(klusters, f"{base}.clu.standard.1.rs", [{"residualSpikes": {"clusters": [10004], "children": True}}], d, timeout=300)
            stc = [x for x in rep.get("results", []) if "step" in x]
            check(rep.get("rc") == 0 and len(stc) == 1 and stc[0].get("ok") is True and stc[0].get("spikes") == [],
                  "the hierarchical stage opens with its child layer")
        else:
            check(False, "the stage has a .clc")

    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
