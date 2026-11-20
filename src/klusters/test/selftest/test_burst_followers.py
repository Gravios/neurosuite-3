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
    split;
  * two references and a mixed cluster (`"mixed": 4`): cells 1 and 2 each fire 100 bursts of two
    spikes; the leaders are clusters 2 and 3, the followers of both are in cluster 4 with 200
    spikes of its own.  Two new clusters, holding exactly the followers of each reference;
    cluster 4 keeps its own spikes and the references are untouched.
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


def retime_mixed(base, seed=5):
    """Clusters 2 and 3 lead bursts; cluster 4 holds the followers of both and 200 spikes of its
    own.  Returns (clu, owner): owner is 2 or 3 for a follower of that reference, else 0."""
    rng = np.random.default_rng(seed)
    n = S.read_res(f"{base}.res.standard.1").size            # 600
    nb = n // 6                                              # bursts per reference
    n_solo = n - 4 * nb
    slot = int(0.030 * S.SR)
    slots = rng.choice(np.arange(40, 40 + 2 * nb + n_solo + 100), 2 * nb + n_solo, replace=False)
    start = slots * slot + rng.integers(0, int(0.010 * S.SR), slots.size)
    la, lb, solo = start[:nb], start[nb:2 * nb], start[2 * nb:]
    gap = int(0.005 * S.SR)
    times = np.concatenate([la, lb, la + gap, lb + gap, solo])
    labels = np.concatenate([np.full(nb, 2), np.full(nb, 3), np.full(2 * nb + n_solo, 4)])
    owner = np.concatenate([np.zeros(2 * nb, int), np.full(nb, 2), np.full(nb, 3), np.zeros(n_solo, int)])
    order = np.argsort(times, kind="stable")
    res, clu, owner = times[order].astype(np.int64), labels[order].astype(np.int32), owner[order]
    res.tofile(f"{base}.res.standard.1")
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([3], np.int32).tofile(fh)
        clu.tofile(fh)
    fet = S.read_fet(f"{base}.fet.standard.1")
    fet[:, -1] = res
    with open(f"{base}.fet.standard.1", "wb") as fh:
        np.array([fet.shape[1]], np.int32).tofile(fh)
        fet.astype(np.int64).tofile(fh)
    return clu, owner


def run_mixed(klusters):
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        clu, owner = retime_mixed(base)
        steps = [{"burstFollowers": [2, 3], "mixed": 4}, {"save": True}]
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        check(rep.get("code") == 0 and rep["rc"] == 0,
              f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", ""))
        results = [r for r in rep.get("results", []) if "step" in r]
        step = results[0] if results else {}
        print("   " + step.get("summary", "(no summary)"))
        return step, clu, owner, S.read_clu(f"{base}.clu.standard.1")


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

    print("-- two references (2, 3) sort a mixed cluster (4)")
    step, clu, owner, saved = run_mixed(klusters)
    created = step.get("created", [])
    check(len(created) == 2, f"two new clusters (got {created})")
    if len(created) == 2:
        of2, of3 = created
        check(np.array_equal(saved == of2, owner == 2),
              f"the first holds exactly the {int((owner == 2).sum())} followers of reference 2 ({int((saved == of2).sum())} spikes)")
        check(np.array_equal(saved == of3, owner == 3),
              f"the second holds exactly the {int((owner == 3).sum())} followers of reference 3 ({int((saved == of3).sum())} spikes)")
        check(np.array_equal(saved[owner == 0], clu[owner == 0]),
              "the references and the mixed cluster's own spikes kept their clusters")

    print(f"test_burst_followers: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
