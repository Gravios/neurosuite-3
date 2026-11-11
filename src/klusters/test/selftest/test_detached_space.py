#!/usr/bin/env python3
"""A DETACHED feature space -- a per-spike table with no basis, such as fiber-manifold's
.fet.manifold.<g> (snr, lag, position, ...) -- must not block the realign of the spaces it
shares waveforms with, and must follow the spike times without losing its features.

Session: the .clu in `standard`; rat.fet.manifold.1 beside it (3 columns: two per-spike values
and the time), which custody resolves to the standard .spk (shared).  Then:
  * realign cluster 2 in `standard`: runs (it used to be refused: "no readable basis
    rat.pca.manifold.1"); the manifold space follows -- its working copy's time column is the
    new .res for the moved spikes, its two feature columns are untouched;
  * `show manifold`, realign cluster 3 there: refused (not a projection of waveforms), nothing
    written;
  * Save: rat.fet.manifold.1's time column is the saved .res, its features unchanged.
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


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, made = S.make_session(d, "rat")
        clu = f"{base}.clu.standard.1"
        res0 = made["res"]
        rng = np.random.default_rng(5)
        feats = rng.integers(-500, 500, (res0.size, 2)).astype(np.int64)
        man = np.concatenate([feats, res0[:, None]], axis=1)
        with open(f"{base}.fet.manifold.1", "wb") as fh:
            np.array([3], np.int32).tofile(fh)
            man.tofile(fh)
        # run 1: realign in `standard` with the detached space beside it, then save
        steps = [{"dump": True, "cluster": 2}, {"realign": 2}, {"dump": True, "cluster": 2}, {"save": True}]
        rep = R.run(klusters, clu, steps, d, realignFromAnySpace=True)
        results = [r for r in rep.get("results", []) if "step" in r]
        for r in results:
            if not r.get("ok"):
                print("   step failed:", r)
        check(rep.get("code") == 0 and rep["rc"] == 0,
              f"run 1 (realign in standard, save) ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", "")[-3000:])
        if rep.get("code") != 0:
            print(f"test_detached_space: FAIL ({fails} failed)")
            return 1
        dumps = [r["dump"] for r in results if "dump" in r]
        sp0 = {s["method"]: s for s in dumps[0]["spaces"]}
        check("manifold" in sp0 and sp0["manifold"].get("usable", True), "the manifold table is listed as a (usable) space")
        done = [r for r in rep.get("results", []) if "realignDone" in r]
        check(len(done) == 1 and done[0]["realignDone"] and done[0]["nShifted"] > 0,
              f"the realign of cluster 2 in `standard` ran and shifted spikes ({done[0]['nShifted'] if done else 'no record'})",
              done[0]["log"][-1500:] if done else "")
        check(not any("no readable basis" in r.get("log", "") for r in done), "nothing complained about a basis for the manifold space")
        clu_ids = S.read_clu(clu)
        c2 = clu_ids == 2
        res = S.read_res(f"{base}.res.standard.1")
        moved = res != res0
        check(moved[c2].any() and not moved[~c2].any(), f"the realign moved {int(moved[c2].sum())} of cluster 2's spikes and nothing else")
        man_out = S.read_fet(f"{base}.fet.manifold.1")
        check(np.array_equal(man_out[:, :2], feats), "the manifold features are untouched by the realign and the save")
        check(np.array_equal(man_out[:, 2], res), "the manifold time column is the saved .res (it followed the moved spikes)")
        check(not os.path.exists(f"{base}.fet.manifold.1.pending"), "no working copy of the manifold table is left after the save")
        check(not os.path.exists(f"{base}.pca.manifold.1"), "no basis was invented for the detached space")

        # run 2: a realign asked for IN the detached space is refused and writes nothing
        res1 = res.copy()
        steps = [{"show": "manifold"}, {"realign": 3}, {"dump": True, "cluster": 3}]
        rep = R.run(klusters, clu, steps, d, realignFromAnySpace=True)
        results = [r for r in rep.get("results", []) if "step" in r]
        done = [r for r in rep.get("results", []) if "realignDone" in r]
        shown = [r for r in results if "show" in r.get("step", {})]
        check(shown and shown[0].get("ok"), "the manifold space can be shown", str(shown[:1]))
        refused_early = any("realign" in r.get("step", {}) and r.get("ok") is False for r in results)
        refused_job = done and not done[0]["realignDone"] and "not a projection" in done[0].get("log", "")
        check(refused_early or refused_job, "a realign in the manifold space is refused (not a projection of waveforms)",
              str(results) + str(done)[-1500:])
        check(np.array_equal(S.read_res(f"{base}.res.standard.1"), res1), "the refusal moved no spike")
        check(np.array_equal(S.read_fet(f"{base}.fet.manifold.1"), man_out), "and the manifold table is as saved")
    print(f"test_detached_space: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
