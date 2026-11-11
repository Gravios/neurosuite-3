#!/usr/bin/env python3
"""Realign and nudge run IN THE SHOWN SPACE; the .clu's own space follows (step 2).

On the synthetic session (.clu in `standard`): build standard_U4, show it, realign cluster 2
there, nudge cluster 3 by +1 there, Save.  Then, on the files:
  * the realign was computed at the sub-sample level: cluster 2's phase column in
    .fet.standard_U4.1 is not all 0 (the session places every spike at a random 16th-sample
    offset, so a x4 alignment finds non-zero quarter-sample lags);
  * the shared .res moved for (some of) cluster 2 by whole samples, and by exactly +1 for every
    spike of cluster 3 (the nudge), nothing else;
  * .res.standard_U4.1 == 4 x .res + phase (the shared tick res, from the shown space);
  * .spk.standard_U4.1: tick 4k - phase == sample k of a fresh base-rate cut at the new .res
    (the spline is exact at the samples; a window with phase p starts p ticks after the sample);
  * the .clu's own space FOLLOWED: .spk.standard.1 == a fresh cut at the new .res (nudged and
    realigned spikes included) and .fet.standard.1's time column == the new .res;
  * the space shown stayed standard_U4 throughout (no swap), and the dump after the realign
    shows the _U4 space reading its working copies (.pending).
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
        steps = [{"supersample": 4}, {"show": "standard_U4"}, {"realign": 2}, {"dump": True, "cluster": 2},
                 {"nudge": [3, 1]}, {"dump": True, "cluster": 3}, {"save": True}, {"dump": True, "cluster": 2}]
        rep = R.run(klusters, clu, steps, d, realignFromAnySpace=True)
        check(rep.get("code") == 0 and rep["rc"] == 0,
              f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", ""))
        results = [r for r in rep.get("results", []) if "step" in r]
        for r in results:
            if not r.get("ok"):
                print("   step failed:", r)
        if rep.get("code") != 0:
            print(f"test_realign_in_shown_space: FAIL ({fails + 1} failed)")
            return 1
        dumps = [r["dump"] for r in results if "dump" in r]
        check(all(dd["active"] == "standard_U4" for dd in dumps), "standard_U4 stayed the shown space (no swap)")
        sp = {s["method"]: s for s in dumps[0]["spaces"]}
        check(sp["standard_U4"]["fet"].endswith(".pending") and sp["standard_U4"]["spk"].endswith(".pending"),
              "after the realign the _U4 space reads its working copies", str(sp["standard_U4"]))

        nch, ns = len(S.CHANS), S.NSAMP
        res = S.read_res(f"{base}.res.standard.1")
        clu_ids = S.read_clu(clu)
        c2, c3 = clu_ids == 2, clu_ids == 3
        moved = res != made["res"]
        check(moved[c2].any() and not moved[~(c2 | c3)].any(),
              f"the realign moved {int(moved[c2].sum())} of cluster 2's spikes by whole samples; no other cluster but 3 moved")
        check(np.array_equal(res[c3], made["res"][c3] + 1), "the nudge moved every spike of cluster 3 by exactly +1 sample")
        fet_u = S.read_fet(f"{base}.fet.standard_U4.1")
        phase = fet_u[:, -2]
        check(np.array_equal(fet_u[:, -1], res), ".fet.standard_U4.1: the time column is the new .res")
        check(((phase >= 0) & (phase < 4)).all(), "every phase is in 0..3")
        check((phase[c2] != 0).any(), f"the realign was sub-sample: {int((phase[c2] != 0).sum())} of cluster 2's spikes carry a non-zero phase")
        check(not phase[~c2].any(), "no other cluster's phase changed (the nudge keeps phases)")
        ticks = S.read_res(f"{base}.res.standard_U4.1")
        check(np.array_equal(ticks, res * 4 + phase), ".res.standard_U4.1 == 4 x .res + phase (the shared tick res)")
        # the _U4 waveforms were cut at the phases: tick 4k - p == base sample k
        spk_u = S.read_spk(f"{base}.spk.standard_U4.1", ns * 4, nch).reshape(-1, ns * 4, nch)
        recut = S.cut(made["fil"], res, S.CHANS, ns, S.PEAK).reshape(-1, ns, nch)
        bad = 0
        for i in np.flatnonzero(c2 | c3):
            p = int(phase[i])
            ks = np.arange(1 if p else 0, ns)
            if not np.array_equal(spk_u[i, 4 * ks - p, :], recut[i, ks, :]):
                bad += 1
        check(bad == 0, f".spk.standard_U4.1: tick 4k-phase == sample k of a fresh cut at the new .res for clusters 2 and 3 ({bad} rows differ)")
        # the .clu's own space followed
        spk_b = S.read_spk(f"{base}.spk.standard.1", ns, nch)
        check(np.array_equal(spk_b, recut.reshape(-1, ns * nch)), ".spk.standard.1 (the .clu's own, followed) == a fresh cut at the new .res")
        fet_b = S.read_fet(f"{base}.fet.standard.1")
        check(np.array_equal(fet_b[:, -1], res), ".fet.standard.1: the time column is the new .res")
        # alignment quality: the x4 realign recovers the sub-sample peak times.  truth + sub is each
        # spike's true peak time; the realigned estimate is res + phase/4.  A constant offset (where
        # the template's reference lands) is allowed, the SPREAD is what the alignment removes:
        # the session's jitter spreads the input by ~1.4 samples.
        t_true = made["truth"] + made["sub"]
        err_in = made["res"][c2] - t_true[c2]
        err_out = res[c2] + phase[c2] / 4.0 - t_true[c2]
        check(err_in.std() > 1.0, f"the session's input jitter spreads cluster 2 by {err_in.std():.2f} samples")
        check(err_out.std() < 0.3, f"after the x4 realign the spread is {err_out.std():.3f} samples (sub-sample alignment)")
        # the loaded times after the save are the saved .res
        t2 = np.asarray(dumps[-1]["cluster"]["times"], np.int64)
        ph2 = np.asarray(dumps[-1]["cluster"]["phases"], np.int64)
        check(np.array_equal(t2, res[c2]) and np.array_equal(ph2, phase[c2]), "the loaded times and phases of cluster 2 are the saved ones")
    print(f"test_realign_in_shown_space: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
