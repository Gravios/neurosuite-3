#!/usr/bin/env python3
"""Actions > Recluster > Recluster Against References: a mixed cluster sorted pooled with the
clusters it is a mixture of; only the mixed cluster changes.

Two synthetic units of 400 spikes.  150 spikes of each are moved into a third cluster (4, "the
mixed one"); clusters 2 and 3 keep 250 each and are its references.  The selection is 2, 3, 4 and
the mixed cluster is named as the dialog would be answered.

  - the references are left EXACTLY as they were: the same rows, the same ids;
  - the 300 rows of cluster 4 are in new clusters (the automatic renumber may give one of them
    the id 4 again), nothing else moved;
  - the two large pieces are each one unit's spikes, and the report says which reference each
    was sorted with -- the right one;
  - the report's counts are the clustering's (pieces, their sizes, every reference pooled whole);
  - the summary in the Recluster output tab names the pieces by the ids they have after the
    automatic renumbering;
  - two Undos put cluster 4 back, row for row (the automatic renumber is its own step, as after
    any recluster);
  - NEGATIVE CONTROL: an ordinary Recluster of the same selection replaces the references too
    (2 and 3 hold other rows afterwards) -- the difference the action is for;
  - "referenceCap": 240 pools 240 spikes of each reference instead of 250, and the split of the
    mixed cluster is still the two units.  (With 150 each -- a pool of 600 spikes in 13
    dimensions -- KiloKlustaKwik returns ONE cluster and the mixed cluster comes back whole: the
    pool has to be large enough for the sorter, which is why the rule pools at least 1000 per
    reference when it has them.)
  - the mixed cluster must be one of the selection: naming another runs nothing.

Not tested here: the dialog itself (the step answers it), a session with a child layer, and a
mixture a plain recluster of the mixed cluster alone could not split (these two units are well
apart, so it could).
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402
from test_recluster_klustakwik import sorter, T   # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def session(d):
    base, m = S.make_session(d, "ag", n_per_unit=400)
    truth = m["clu"].copy()
    clu = truth.copy()
    rng = np.random.default_rng(4)
    for u in (2, 3):
        clu[rng.choice(np.flatnonzero(truth == u), 150, replace=False)] = 4
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([5], np.int32).tofile(fh)
        clu.astype(np.int32).tofile(fh)
    return base, truth, clu


def labels_of(rep, k=0):
    got = [np.array(r["labels"]) for r in rep.get("results", []) if "labels" in r]
    return got[k] if len(got) > k else np.zeros(0, int)


def main():
    klusters = R.skip_without_binary()
    kkk = sorter()
    if not kkk:
        print("test_recluster_against_references: SKIP (no KiloKlustaKwik_cpu: build it or set KKK_BIN)")
        return 0
    opts = dict(exe=kkk, args=T + "%features", autoSelect=False)

    with tempfile.TemporaryDirectory() as d:
        base, truth, clu = session(d)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"select": [2, 3, 4]}, {"reclusterAgainst": 4, "opts": opts}, {"reclusterAgainstReport": True},
                     {"reclusterLog": True}, {"labels": True}, {"undo": True}, {"labels": True}, {"undo": True}, {"labels": True}],
                    d, timeout=900)
        after, undone1, undone = labels_of(rep, 0), labels_of(rep, 1), labels_of(rep, 2)
        pane = "\n".join(next((x["lines"] for x in rep.get("results", []) if "lines" in x), []))
        r = next((x for x in rep.get("results", []) if "pieces" in x), {})
        check(rep.get("rc") == 0 and len(after) == len(clu), "the run finished", rep.get("stderr", "")[-600:])
        if len(after) != len(clu):
            print("test_recluster_against_references: FAIL")
            return 1
        mixed = clu == 4
        check(np.array_equal(after[~mixed], clu[~mixed]), "the references are exactly as they were: the same rows under 2 and 3")
        check(not np.isin(after[mixed], [2, 3]).any() and len(np.unique(after[mixed])) >= 2,
              "the 300 rows of the mixed cluster are in two or more clusters, none of them a reference", str(np.unique(after[mixed])))
        ids, cnt = np.unique(after[mixed], return_counts=True)
        big = ids[np.argsort(-cnt)][:2]
        pure = [float(np.mean(truth[mixed & (after == b)] == np.bincount(truth[mixed & (after == b)]).argmax())) for b in big]
        unit = [int(np.bincount(truth[mixed & (after == b)]).argmax()) for b in big]
        held = int(sum(np.sum(mixed & (after == b)) for b in big))
        print(f"      pieces: {dict(zip(ids.tolist(), cnt.tolist()))}; the two largest are units {unit}, pure {pure[0]:.3f} / {pure[1]:.3f}, "
              f"holding {held} of 300")
        check(sorted(unit) == [2, 3] and min(pure) >= 0.97 and held >= 270, "the two large pieces are the two units' spikes")
        pieces = {int(k): int(v) for k, v in r.get("pieces", {}).items()}
        check(pieces == dict(zip(ids.tolist(), cnt.tolist())) and r.get("mixed") == 4 and r.get("mixedSpikes") == 300,
              "the report's pieces are the clustering's", str(pieces))
        refs = {x["id"]: x for x in r.get("references", [])}
        check(sorted(refs) == [2, 3] and all(x["spikes"] == 250 and x["pooled"] == 250 for x in refs.values()),
              "both references were pooled whole (250 of 250: under the cap)")
        with_ref = {}
        for b in big:
            share = {rid: x["byPiece"].get(str(int(b)), 0) / max(1, x["pooled"]) for rid, x in refs.items()}
            with_ref[int(b)] = max(share, key=share.get) if max(share.values()) >= 0.5 else None
        check(all(with_ref[int(b)] == u for b, u in zip(big, unit)),
              "the report names the right reference for each large piece (most of that reference was sorted with it)", str(with_ref))
        summary = "\n".join(r.get("summary", []))
        check("the references are unchanged" in summary and all(f"new cluster {int(b)}" in summary for b in big)
              and "SPLIT" not in summary, "the summary lists the pieces, and no reference is reported as split", summary)
        check(all(f"new cluster {int(b)}:" in pane for b in ids) and "the references are unchanged" in pane,
              "the Recluster output tab ends with the summary, the pieces under the ids they have now", pane[-700:])
        check(np.array_equal(undone, clu) and np.array_equal(undone1[~mixed], clu[~mixed]),
              "two Undos (the automatic renumber, then the recluster -- as after any recluster): cluster 4 is back, row for row",
              str(dict(zip(*[x.tolist() for x in np.unique(undone, return_counts=True)]))))

    with tempfile.TemporaryDirectory() as d:
        base, truth, clu = session(d)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"select": [2, 3, 4]}, {"recluster": "", "opts": opts}, {"labels": True}], d, timeout=900)
        plain = labels_of(rep)
        check(len(plain) == len(clu) and not np.array_equal(plain == 2, clu == 2) and not np.array_equal(plain == 3, clu == 3),
              "NEGATIVE CONTROL: an ordinary Recluster of the same selection replaces the references too (2 and 3 hold other rows)",
              str(dict(zip(*[x.tolist() for x in np.unique(plain, return_counts=True)]))))

    with tempfile.TemporaryDirectory() as d:
        base, truth, clu = session(d)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"select": [2, 3, 4]}, {"reclusterAgainst": 4, "opts": dict(opts, referenceCap=240)},
                     {"reclusterAgainstReport": True}, {"labels": True}], d, timeout=900)
        after = labels_of(rep)
        r = next((x for x in rep.get("results", []) if "pieces" in x), {})
        mixed = clu == 4
        ok = len(after) == len(clu)
        check(ok and [x["pooled"] for x in r.get("references", [])] == [240, 240] and "240 of its 250 spikes pooled" in "\n".join(r.get("summary", [])),
              "referenceCap 240: 240 spikes of each reference are pooled, and the summary says so", str(r.get("references")))
        if ok:
            ids, cnt = np.unique(after[mixed], return_counts=True)
            big = ids[np.argsort(-cnt)][:2]
            unit = sorted(int(np.bincount(truth[mixed & (after == b)]).argmax()) for b in big)
            check(np.array_equal(after[~mixed], clu[~mixed]) and unit == [2, 3] and int(cnt[np.argsort(-cnt)][:2].sum()) >= 270,
                  "... the references are untouched and the mixed cluster still splits into the two units", str(dict(zip(ids.tolist(), cnt.tolist()))))

    with tempfile.TemporaryDirectory() as d:
        base, truth, clu = session(d)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"select": [2, 4]}, {"reclusterAgainst": 3, "opts": opts}, {"labels": True}], d, timeout=300)
        started = next((x.get("started") for x in rep.get("results", []) if "started" in x), None)
        check(started is False and np.array_equal(labels_of(rep), clu),
              "a mixed cluster that is not in the selection: nothing is run, nothing changes")

    print(f"test_recluster_against_references: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
