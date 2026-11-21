#!/usr/bin/env python3
"""Actions > Split Off Spurious Spikes: a cluster's spikes are ordered by least change of the
cumulative mean waveform and cut where a channel's cumulative kurtosis starts to rise; the spikes
past the cut become one new cluster and the core stays.

Session: the synthetic one (two units of 300 spikes with different footprints over the four
channels), relabelled: cluster 2 holds all of unit A and 100 spikes of unit B, cluster 3 the
other 200 of unit B.  Then:
  * `coreCut [2]`: one new cluster; it holds nearly all of the 100 foreign spikes and few of
    unit A's; cluster 3 is untouched;
  * `coreCut [3]` (one unit alone): at least three quarters of it stays where it is;
  * the same on CHILDREN (`"children": true`), child c = parent c: the new child is a child of
    parent 2 and the parent clustering (.clu) is unchanged;
  * the Preferences > Splitting knobs reach the analysis: with the cut sensitivity at its
    largest value nothing is split off from cluster 2; the default is 5 and the action's key is
    Ctrl+Space;
  * the Preferences dialog has the Splitting page with a "Split Off Spurious Spikes" group, and
    writing the dialog's values back leaves the cut where it was.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S            # noqa: E402
import run as R                # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n" + extra) if (not ok and extra) else ""))
    fails += not ok


def relabel(base, seed=11):
    """Cluster 2 = unit A (was 2) + 100 spikes of unit B (was 3).  Returns (clu, foreign)."""
    rng = np.random.default_rng(seed)
    clu = S.read_clu(f"{base}.clu.standard.1")
    foreign = np.zeros(clu.size, bool)
    foreign[rng.choice(np.flatnonzero(clu == 3), 100, replace=False)] = True
    clu = clu.copy()
    clu[foreign] = 2
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([int(clu.max()) + 1], np.int32).tofile(fh)
        clu.astype(np.int32).tofile(fh)
    return clu, foreign


def run_case(klusters, ids, children=False, onset=None):
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        clu, foreign = relabel(base)
        if children:
            with open(f"{base}.clc.standard.1", "wb") as fh:      # child c = parent c: the OLD numbering, upgraded on load
                np.array([len(np.unique(clu))], np.int32).tofile(fh)
                clu.astype(np.int32).tofile(fh)
            clp = np.array([0, 2, 3], np.int32)                   # entry c-1 = the parent of child c
            with open(f"{base}.clp.standard.1", "wb") as fh:
                np.array([clp.size], np.int32).tofile(fh)
                clp.tofile(fh)
        step = {"coreCut": ids}
        if children:
            step["children"] = True
        if onset is not None:
            step["onsetFactor"] = onset
        rep = R.run(klusters, f"{base}.clu.standard.1", [step, {"save": True}], d)
        check(rep.get("code") == 0 and rep["rc"] == 0,
              f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", ""))
        results = [r for r in rep.get("results", []) if "step" in r]
        out = results[0] if results else {}
        for line in out.get("summaries", [out.get("error", "(no summary)")]):
            print("   " + line)
        saved_clc = S.read_clu(f"{base}.clc.standard.1") if children else None
        saved_clp = S.read_clu(f"{base}.clp.standard.1") if children else None
        return out, clu, foreign, S.read_clu(f"{base}.clu.standard.1"), saved_clc, saved_clp


def main():
    klusters = R.skip_without_binary()

    print("-- cluster 2: unit A with 100 spikes of unit B in it")
    out, clu, foreign, saved, _, _ = run_case(klusters, [2])
    created = out.get("created", [])
    check(len(created) == 1 and created[0] > 0, f"one new cluster (got {created})")
    check(out.get("onsetFactor") == 5.0, f"the default cut sensitivity is 5 (got {out.get('onsetFactor')})")
    check(out.get("shortcut") == "Ctrl+Space", f"the action's key is Ctrl+Space (got {out.get('shortcut')!r})")
    if len(created) == 1 and created[0] > 0:
        new = created[0]
        gone = saved == new
        own = (clu == 2) & ~foreign
        check(new not in (2, 3), f"it has an id of its own ({new})")
        check((gone & foreign).sum() >= 90, f"it holds the foreign spikes ({int((gone & foreign).sum())} of 100)")
        check((gone & own).sum() <= 0.25 * own.sum(), f"and few of the unit's own ({int((gone & own).sum())} of {int(own.sum())})")
        check(np.array_equal(saved[~gone], clu[~gone]), "every other spike kept its cluster")
        check(np.array_equal(saved == 3, clu == 3), "cluster 3 is untouched")

    print("-- cluster 2 again with the cut sensitivity at its largest: the knob reaches the analysis")
    out, clu, foreign, saved, _, _ = run_case(klusters, [2], onset=1000.0)
    check(out.get("onsetFactor") == 1000.0, f"the knob is set ({out.get('onsetFactor')})")
    check(out.get("created") == [0], f"nothing is split off (got {out.get('created')})")
    check(np.array_equal(saved, clu), "the clustering is unchanged")

    print("-- Preferences > Splitting")
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        relabel(base)
        png = os.environ.get("CORE_CUT_PREF_PNG", os.path.join(d, "splitting.png"))
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"prefPage": "PrefSplitting", "saveTo": png, "apply": True}, {"coreCut": [2]}], d)
        steps = [r for r in rep.get("results", []) if "step" in r]
        page = steps[0] if steps else {}
        check(page.get("found") is True, "the Preferences dialog has the Splitting page")
        check("Split Off Spurious Spikes" in page.get("groups", []), f"with the action's group ({page.get('groups')})")
        check(page.get("saved") is True and os.path.getsize(png) > 1000, "the page renders")
        after = steps[1] if len(steps) > 1 else {}
        check(after.get("onsetFactor") == 5.0, f"the dialog's values written back: the sensitivity is still 5 ({after.get('onsetFactor')})")
        check(len(after.get("created", [])) == 1 and after["created"][0] > 0, f"and the cut is still made ({after.get('created')})")

    print("-- cluster 3: one unit alone")
    out, clu, foreign, saved, _, _ = run_case(klusters, [3])
    stay = ((saved == 3) & (clu == 3)).sum()
    check(stay >= 0.75 * (clu == 3).sum(), f"at least three quarters of it stays ({int(stay)} of {int((clu == 3).sum())})")
    check(np.array_equal(saved[clu == 2], clu[clu == 2]), "cluster 2 is untouched")

    print("-- children: child 10002 (the self child of parent 2) holds unit A and the 100 foreign spikes")
    out, clu, foreign, saved_clu, saved_clc, saved_clp = run_case(klusters, [10002], children=True)
    created = out.get("created", [])
    check(len(created) == 1 and created[0] > 0, f"one new child (got {created})")
    if len(created) == 1 and created[0] > 0:
        new = created[0]
        check(out.get("createdParent", {}).get(str(new)) == 2, f"it is a child of parent 2 ({out.get('createdParent')})")
        check(np.array_equal(saved_clu, clu), "the parent clustering (.clu) is unchanged")
        # The saved .clc may be renumbered: the new child is the one holding most foreign spikes.
        ids, counts = np.unique(saved_clc[foreign], return_counts=True)
        child = ids[np.argmax(counts)]
        gone = saved_clc == child
        own = (clu == 2) & ~foreign
        check(counts.max() >= 90, f"one child of the saved .clc holds the foreign spikes ({int(counts.max())} of 100)")
        check((gone & own).sum() <= 0.25 * own.sum(), f"and few of the unit's own ({int((gone & own).sum())} of {int(own.sum())})")
        if 1 <= child <= saved_clp.size:
            check(saved_clp[child - 1] == 2, f"the saved .clp gives it parent 2 (got {saved_clp[child - 1]})")
        check(np.all(saved_clc[clu == 3] == 10003), "parent 3's child is its self child, 10003, in the saved .clc")
        check(child >= 20000 and np.all(saved_clc[own & ~gone] == 10002), f"the new child ({child}) is in the free block and the core is still 10002")

    print(f"test_core_cut: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
