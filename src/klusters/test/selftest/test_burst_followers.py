#!/usr/bin/env python3
"""Actions > Split Burst Followers: of two clusters, the spikes of one that follow the other's
burst leaders are split off into a new cluster, and nothing else is.

Session: the synthetic one, with its spike times and labels replaced (the action reads times
only).  Cell 1 fires 150 bursts of two spikes 5 ms apart: the leaders are cluster 2, the
followers were "clustered" with cluster 3.  Cluster 3 also holds 300 spikes of its own, each at
least 20 ms from any other spike.  Then:
  * `burstFollowers [2, 3]`: one new cluster, holding exactly the 150 followers; the reverse
    direction (spikes of 2 behind leaders of 3: none) is left alone;
  * Save: the .clu carries it; clusters 2 and 3 keep every other spike;
  * a second session with the followers left in cluster 2 (a correct clustering): nothing is
    split.
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


def retime(base, follower_label, seed=3):
    """Replace the session's times and labels; returns (res, clu, is_follower)."""
    rng = np.random.default_rng(seed)
    n = S.read_res(f"{base}.res.standard.1").size            # 600
    n_burst, n_solo = n // 4, n // 2
    slot = int(0.030 * S.SR)                                 # one event per 30 ms slot at most
    slots = rng.choice(np.arange(40, 40 + (n_burst + n_solo) + 100), n_burst + n_solo, replace=False)
    start = slots * slot + rng.integers(0, int(0.010 * S.SR), slots.size)
    lead, solo = start[:n_burst], start[n_burst:]
    times = np.concatenate([lead, lead + int(0.005 * S.SR), solo])
    labels = np.concatenate([np.full(n_burst, 2), np.full(n_burst, follower_label), np.full(n_solo, 3)])
    follower = np.concatenate([np.zeros(n_burst, bool), np.ones(n_burst, bool), np.zeros(n_solo, bool)])
    order = np.argsort(times, kind="stable")
    res, clu, follower = times[order].astype(np.int64), labels[order].astype(np.int32), follower[order]
    res.tofile(f"{base}.res.standard.1")
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([int(clu.max()) + 1], np.int32).tofile(fh)
        clu.tofile(fh)
    fet = S.read_fet(f"{base}.fet.standard.1")
    fet[:, -1] = res
    with open(f"{base}.fet.standard.1", "wb") as fh:
        np.array([fet.shape[1]], np.int32).tofile(fh)
        fet.astype(np.int64).tofile(fh)
    return res, clu, follower


def run_case(klusters, follower_label):
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        res, clu, follower = retime(base, follower_label)
        steps = [{"burstFollowers": [2, 3]}, {"save": True}]
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        check(rep.get("code") == 0 and rep["rc"] == 0,
              f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", ""))
        results = [r for r in rep.get("results", []) if "step" in r]
        step = results[0] if results else {}
        print("   " + step.get("summary", "(no summary)"))
        return step, clu, follower, S.read_clu(f"{base}.clu.standard.1")


def main():
    klusters = R.skip_without_binary()

    print("-- cluster 3 holds the followers of cluster 2's bursts")
    step, clu, follower, saved = run_case(klusters, follower_label=3)
    created = step.get("created", [])
    check(len(created) == 1, f"one new cluster (got {created})")
    if len(created) == 1:
        new = created[0]
        check(new not in (2, 3), f"it has an id of its own ({new})")
        check(np.array_equal(saved == new, follower),
              f"it holds exactly the {int(follower.sum())} followers ({int((saved == new).sum())} spikes in it)")
        check(np.array_equal(saved[~follower], clu[~follower]), "every other spike kept its cluster")
    check("not above chance" in step.get("summary", ""), "the reverse direction is reported as not above chance")

    print("-- the followers are in cluster 2 already (a correct clustering)")
    step, clu, follower, saved = run_case(klusters, follower_label=2)
    check(step.get("created") == [], f"nothing is split (got {step.get('created')})")
    check(np.array_equal(saved, clu), "the saved .clu is unchanged")

    print(f"test_burst_followers: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
