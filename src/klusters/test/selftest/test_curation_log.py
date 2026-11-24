#!/usr/bin/env python3
"""The curation log says what started an action, what it did, and what was looked at and left.

A session under a method of its own (a "standard" session is not logged) with three clusters: units 2 and 3,
and 4 = a third of unit 3's spikes.  Scripted as a curator would work:

  select 2 + 3, look, select something else      -> CONSIDERED (2, 3): a comparison left unmerged
  the panel recommends 3 + 4, look, Group them   -> GROUP, then Klusters' own realign of the result
  Realign the result by hand                     -> REALIGN again, the curator's this time
  Save

Checked on the log file:
  * every action line carries "trigger"; the realign after the merge is "after_merge", the one
    asked for is "manual", and SESSION_OPEN says the automatic realign is on;
  * REALIGN has a detail with what it did (spikes, how many shifted, how far);
  * the GROUP pair detail has the new measurements (lag, lag-0 cosine, profile cosine, amplitude
    ratio, co-active overlap, observed coincidences), how long the pair was shown, that the
    selection was the merged set, and that the panel recommended it;
  * CONSIDERED: one record for 2 + 3, with the same pair evidence, its dwell, not recommended;
    none for a selection shown for less than the dwell, for a single cluster, or for the merged pair;
  * SESSION_CLOSE counts the actions by kind;
  * a realign that is refused closes its entry with outcome "not_applied";
  * RECLUSTER has a detail: mode, sources, the clusters returned and their sizes (skipped without
    a KiloKlustaKwik_cpu);
  * a session whose .clc is there says hierarchical in SESSION_OPEN (it is written before the
    child layer is loaded, and said false), and gets a SESSION_CONTEXT line with the counts.
  * the child layer: a CONSIDERED for two children shown and left (layer child, their parent);
    mergeChildren with the pair record of what it merged; promoteChildren and dropChildrenToBin
    with the child against the largest sibling that stays; none of those also a CONSIDERED.
Negative control: the same script with the dwell at an hour logs no CONSIDERED.
"""
import json
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R       # noqa: E402
import session as S   # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def log_lines(d, name):
    path = next((os.path.join(d, f) for f in os.listdir(d) if f.startswith(name + ".cur.")), None)
    if not path:
        return []
    with open(path) as fh:
        return [json.loads(line) for line in fh if line.strip()]


def last(lines):
    """The decision per (action_idx, phase/detail, cluster): a Save writes final:false lines that
    close() writes again."""
    out = {}
    for i, o in enumerate(lines):
        if "action" not in o:
            continue
        key = (o["action_idx"], o.get("event", o.get("phase")), o.get("cluster", o.get("cluster_a", -1)), o.get("edit", ""))
        out[key] = o
    return list(out.values())


METHOD = "raw"            # any method but "standard" is logged; the waveforms are the plain windows


def session(d, name, method=METHOD):
    base, m = S.make_session(d, name, n_per_unit=400, method=method)
    clu = m["clu"].copy()
    three = np.flatnonzero(clu == 3)
    clu[three[::3]] = 4                                   # a third of unit 3 as its own cluster
    with open(f"{base}.clu.{method}.1", "wb") as fh:
        np.array([np.unique(clu).size], np.int32).tofile(fh)
        clu.astype(np.int32).tofile(fh)
    return base, clu


def curate(klusters, dwell):
    with tempfile.TemporaryDirectory() as d:
        base, clu = session(d, "cl")
        steps = [{"curationLog": {"consideredDwell": dwell}},
                 {"select": [2, 3]}, {"wait": 900}, {"select": [2]},           # looked at, left
                 {"select": [2, 4]}, {"select": [4]},                          # passed over: too short
                 {"wait": 900},                                                # one cluster: no comparison
                 {"curationLog": {"recommend": [3, 4]}}, {"wait": 900},
                 {"group": [3, 4]},
                 {"palette": True},
                 {"select": [2]}, {"realign": 2},
                 {"save": True}]
        rep = R.run(klusters, f"{base}.clu.{METHOD}.1", steps, d, timeout=900, autoRenumber=False)
        return rep, log_lines(d, "cl")


def main():
    klusters = R.skip_without_binary()
    rep, raw = curate(klusters, 0.5)
    check(rep.get("rc") == 0 and all(r.get("ok", True) for r in rep.get("results", [])), "the script ran",
          (rep.get("stderr") or "")[-1500:])
    L = last(raw)
    opened = next((o for o in raw if o.get("event") == "SESSION_OPEN"), {})
    closed = next((o for o in raw if o.get("event") == "SESSION_CLOSE"), {})
    check(opened.get("auto_realign_after_merge") is True and "auto_renumber_after_merge" in opened
          and opened.get("considered_min_dwell_s") == 2.0 and opened.get("hierarchical") is False,
          "SESSION_OPEN: what Klusters does by itself after a merge, the dwell, flat session", opened)
    acts = [o for o in L]
    check(acts and all("trigger" in o for o in acts), "every action line carries a trigger")

    realigns = sorted({(o["action_idx"], o["trigger"]) for o in acts if o["action"] == "REALIGN"})
    groups = [o for o in acts if o["action"] == "GROUP" and o.get("event") == "ACTION_DETAIL"]
    check(len(groups) == 1, "one GROUP pair detail (3, 4)", groups)
    g = groups[0] if groups else {}
    gidx = g.get("action_idx", -1)
    check([t for _, t in realigns] == ["after_merge", "manual"] and realigns and realigns[0][0] > gidx,
          "two realigns after the merge: Klusters' own (after_merge), then the curator's (manual)", realigns)
    check(g.get("trigger") == "manual", "the merge itself is the curator's")
    rd = [o for o in acts if o["action"] == "REALIGN" and o.get("edit") == "realign"]
    check(len(rd) == 2 and all(o.get("outcome") == "applied" and o["n_spikes"] > 0 and 0 <= o["n_shifted"] <= o["n_spikes"] and "shift_abs_median" in o
                               and "shift_abs_max" in o and "shift_mean" in o and "not_recut" in o for o in rd),
          "each REALIGN says what it did (spikes, shifted, how far)", [{k: v for k, v in o.items() if k not in ("session_id",)} for o in acts if o["action"] == "REALIGN" and o.get("event") == "ACTION_DETAIL"])
    check(bool(rd) and rd[0]["n_spikes"] == 400 and rd[1]["n_spikes"] == 400,
          "the automatic realign ran on the merged cluster (unit 3 whole again), the manual one on unit 2", [o["n_spikes"] for o in rd])

    new = ("template_lag", "template_cos_lag0", "profile_cos", "amp_ratio", "coactive_overlap", "cross_refrac_observed")
    check(all(k in g for k in new), "GROUP pair detail: lag, lag-0 cosine, profile cosine, amplitude ratio, co-active overlap, observed coincidences",
          sorted(g))
    check({g.get("cluster_a"), g.get("cluster_b")} == {3, 4} and g.get("recommended") is True and g.get("selection_matches") is True
          and g.get("dwell_s", 0) >= 0.8, "... the pair, that the panel recommended it, that it was the selection, how long it was shown", g)
    check(g.get("coactive_overlap", 0) > 0.9 and g.get("cross_refrac_observed") == 0,
          "... two halves of one unit: active in the same minutes, never within 1.5 ms of each other", g)
    if g.get("template_cos") is not None:
        check(g["template_cos"] > 0.95 and g["profile_cos"] > 0.95 and 0.7 < g["amp_ratio"] <= 1.0 and abs(g["template_lag"]) <= 4,
              "... and alike (cosine, profile, amplitude ratio)", g)
    else:
        print("  note: template cosine not logged (waveform means were not cached in this run)")

    cons = [o for o in acts if o["action"] == "CONSIDERED"]
    check(len(cons) == 1 and {cons[0].get("cluster_a"), cons[0].get("cluster_b")} == {2, 3},
          "one CONSIDERED: 2 + 3, shown and left (not the pair passed over, not the single cluster, not the merged pair)",
          [(o.get("cluster_a"), o.get("cluster_b"), o.get("dwell_s")) for o in cons])
    c = cons[0] if cons else {}
    check(c.get("dwell_s", 0) >= 0.8 and c.get("recommended") is False and c.get("n_shown") == 2 and c.get("status") == "good"
          and all(k in c for k in new) and c.get("event") == "ACTION_DETAIL", "... with the pair evidence, its dwell, not recommended", c)
    by = closed.get("n_by_action", {})
    check(by.get("GROUP") == 1 and by.get("REALIGN") == 2 and by.get("CONSIDERED") == 1 and "duration_s" in closed,
          "SESSION_CLOSE counts the actions by kind", closed)

    _, raw2 = curate(klusters, 3600.0)
    check(not [o for o in raw2 if o.get("action") == "CONSIDERED"] and [o for o in raw2 if o.get("action") == "GROUP"],
          "control: with the dwell at an hour nothing is logged as CONSIDERED (the merge still is)")

    # ---- a realign that is refused: the waveforms of a "stderiv" session that are not that transform
    with tempfile.TemporaryDirectory() as d:
        base, clu = session(d, "ab", method="stderiv")
        R.run(klusters, f"{base}.clu.stderiv.1", [{"select": [2]}, {"realign": 2}, {"select": [2, 3]}, {"wait": 900},
                                                 {"select": [3]}, {"save": True}], d, timeout=900)
        A = last(log_lines(d, "ab"))
        ra = [o for o in A if o["action"] == "REALIGN"]
        check(len([o for o in ra if o.get("edit") == "realign" and o.get("outcome") == "not_applied"]) == 1
              and not [o for o in ra if o.get("phase") == "after"],
              "a realign that changed nothing says so (outcome not_applied, no after-snapshot); it left a before line and silence",
              [(o.get("event", o.get("phase")), o.get("outcome")) for o in ra])
        check(len([o for o in A if o["action"] == "CONSIDERED"]) == 0,
              "... (the default 2 s dwell: a 0.9 s look is not a CONSIDERED)")

    # ---- a recluster: what went in and what came back
    import test_recluster_klustakwik as RK
    kkk = RK.sorter()
    if not kkk:
        print("  note: recluster detail not checked (no KiloKlustaKwik_cpu: build it or set KKK_BIN)")
    else:
        with tempfile.TemporaryDirectory() as d:
            base, m = S.make_session(d, "rc", n_per_unit=400, method=METHOD)
            with open(f"{base}.clu.{METHOD}.1", "wb") as fh:           # both units in one cluster
                np.array([1], np.int32).tofile(fh)
                np.full_like(m["clu"], 2).astype(np.int32).tofile(fh)
            R.run(klusters, f"{base}.clu.{METHOD}.1",
                  [{"select": [2]}, {"recluster": "", "opts": dict(exe=kkk, autoSelect=False)}, {"palette": True}, {"save": True}],
                  d, timeout=900)
            rc = [o for o in last(log_lines(d, "rc")) if o["action"] == "RECLUSTER" and o.get("edit") == "recluster"]
            r0 = rc[0] if rc else {}
            check(len(rc) == 1 and r0.get("mode") == "plain" and r0.get("layer") == "parent" and r0.get("source_clusters") == [2]
                  and r0.get("n_result_clusters") == len(r0.get("result_sizes", [])) >= 2 and sum(r0["result_sizes"]) == 800
                  and 0.4 < r0.get("largest_share", 0) < 0.7 and r0.get("unchanged") is False,
                  "RECLUSTER says its mode, its sources, the clusters that came back and their sizes (two units: about half each)", r0)

    # ---- hierarchical: SESSION_OPEN and the context line
    with tempfile.TemporaryDirectory() as d:
        base, clu = session(d, "hi")
        child = np.where(clu >= 2, clu + 10000, clu)
        clp = np.zeros(int(child.max()), np.int32)
        for c in np.unique(child):
            clp[c - 1] = c - 10000 if c >= 10002 else c
        for kind, ids, hdr in (("clc", child, np.unique(child).size), ("clp", clp, clp.size)):
            with open(f"{base}.{kind}.{METHOD}.1", "wb") as fh:
                np.array([hdr], np.int32).tofile(fh)
                ids.astype(np.int32).tofile(fh)
        rep = R.run(klusters, f"{base}.clu.{METHOD}.1", [{"hierarchy": {}}, {"save": True}], d, timeout=900)
        raw = log_lines(d, "hi")
        ctx = [o for o in raw if o.get("event") == "SESSION_CONTEXT"]
        op = next((o for o in raw if o.get("event") == "SESSION_OPEN"), {})
        check(op.get("hierarchical") is True, "a session with its .clc there: SESSION_OPEN says hierarchical (it said false for every one)", op)
        check(bool(ctx) and ctx[-1].get("hierarchical") is True and ctx[-1].get("n_children") == 3 and ctx[-1].get("n_parents") == 3
              and ctx[-1].get("session_id") == op.get("session_id"),
              "... and a SESSION_CONTEXT line with the layer's counts once it is loaded", ctx)

    # ---- the child layer: comparisons, a merge of children, a child taken out of its parent
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "ch", n_per_unit=400, method=METHOD)
        clu = m["clu"]
        child = clu.copy()
        two, three = np.flatnonzero(clu == 2), np.flatnonzero(clu == 3)
        child[two[0::2]] = 20000; child[two[1::2]] = 20001                    # parent 2: two children
        child[three[0::4]] = 20002; child[three[1::4]] = 20003                # parent 3: four
        child[three[2::4]] = 20004; child[three[3::4]] = 20005
        clp = np.zeros(int(child.max()), np.int32)
        for c in np.unique(child):
            clp[c - 1] = clu[child == c][0]
        for kind, ids, hdr in (("clc", child, np.unique(child).size), ("clp", clp, clp.size)):
            with open(f"{base}.{kind}.{METHOD}.1", "wb") as fh:
                np.array([hdr], np.int32).tofile(fh)
                ids.astype(np.int32).tofile(fh)
        H = lambda **k: {"hierarchy": k}
        steps = [{"curationLog": {"consideredDwell": 0.5}}, {"select": [2]},
                 H(showChildren=[20000, 20001]), {"wait": 900}, H(showChildren=[20002]),      # looked at, left
                 H(showChildren=[20002, 20003]), {"wait": 900}, H(mergeChildren=[20002, 20003]),
                 H(showChildren=[20004, 20005]), {"wait": 900}, H(promoteChildren=[20005]),   # taken out
                 H(showChildren=[20000, 20001]), H(dropChildren=[20001]),
                 H(showChildren=[]), {"select": [2, 3]}, {"wait": 900}, {"select": [2]},      # parents again
                 {"save": True}]
        rep = R.run(klusters, f"{base}.clu.{METHOD}.1", steps, d, timeout=900, autoRenumber=False)
        check(rep.get("rc") == 0 and all(r.get("ok", True) for r in rep.get("results", [])), "the child-layer script ran",
              (rep.get("stderr") or "")[-1500:])
        L = [o for o in last(log_lines(d, "ch")) if o.get("event") == "ACTION_DETAIL" and o.get("detail") == "pair"]
        cons = [o for o in L if o["action"] == "CONSIDERED"]
        cc = [o for o in cons if o.get("layer") == "child"]
        check(len(cc) == 1 and {cc[0]["cluster_a"], cc[0]["cluster_b"]} == {20000, 20001} and cc[0].get("parent_a") == 2
              and cc[0].get("parent_b") == 2 and cc[0].get("same_parent") is True and cc[0].get("dwell_s", 0) >= 0.8
              and "coactive_overlap" in cc[0] and "template_lag" in cc[0],
              "CONSIDERED on the child layer: the two children shown and left, with their parent and the pair evidence",
              [(o.get("layer"), o.get("cluster_a"), o.get("cluster_b")) for o in cons])
        cp = [o for o in cons if o.get("layer") == "parent"]
        check(len(cp) == 1 and {cp[0]["cluster_a"], cp[0]["cluster_b"]} <= set(range(2, 10)) and "parent_a" not in cp[0],
              "... and back on the parents the comparison is a parent one", cp)
        mg = [o for o in L if o["action"] == "HIERARCHY_EDIT"]
        check(len(mg) == 1 and {mg[0]["cluster_a"], mg[0]["cluster_b"]} == {20002, 20003} and mg[0].get("layer") == "child"
              and mg[0].get("parent") == 3 and mg[0].get("pairing") == "all" and mg[0].get("n_merged") == 2
              and mg[0].get("selection_matches") is True and mg[0].get("dwell_s", 0) >= 0.8 and mg[0].get("cross_refrac_observed") == 0,
              "mergeChildren: the pair record of the merged children (measured before the merge), its dwell, that it was the selection", mg)
        sep = [o for o in L if o["action"] == "GESTURE" and o.get("role") == "leaving_vs_largest_staying"]
        pr = [o for o in sep if o.get("cluster_a") == 20005 or o.get("cluster_b") == 20005]
        check(len(pr) == 1 and pr[0].get("parent") == 3 and pr[0].get("n_leaving") == 1 and pr[0].get("layer") == "child"
              and {pr[0]["n_a"], pr[0]["n_b"]} == {100, 200} and pr[0].get("selection_matches") is False,
              "promoteChildren: the child against the largest sibling that stays (the merged one, 200 spikes)", pr)
        dr = [o for o in sep if o.get("cluster_a") == 20001 or o.get("cluster_b") == 20001]
        check(len(dr) == 1 and {dr[0]["cluster_a"], dr[0]["cluster_b"]} == {20000, 20001} and dr[0].get("parent") == 2,
              "dropChildrenToBin: the dropped child against the sibling it left", dr)
        check(not [o for o in cc if {o["cluster_a"], o["cluster_b"]} & {20002, 20003, 20004, 20005}],
              "children that were merged or taken apart are not also logged as CONSIDERED")

    print(f"test_curation_log: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
