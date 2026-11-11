#!/usr/bin/env python3
"""The other direction: the .clu lives in the super-sampled space, the realign runs in `standard`.

Build standard_U4, Save As under it (adopts the space, reopens there: cluMethod standard_U4),
realign cluster 2 there first (so the _U4 space carries real phases), then show `standard`,
realign cluster 3 THERE (a base-rate alignment) and Save.  Then:
  * the .clu is rat.clu.standard_U4.1 and the session reopened in standard_U4;
  * the shared .res moved for cluster 3 (the base-rate realign) by whole samples;
  * the _U4 space -- the .clu's OWN -- followed the base-rate realign: .fet.standard_U4.1's time
    column is the new .res, its phases for cluster 3 are unchanged (a whole-sample move keeps
    the phase), cluster 2's phases survived, and .res.standard_U4.1 == 4 x .res + phase;
  * .spk.standard_U4.1 is cut at the new times and the phases (tick 4k - p == a fresh
    base-rate sample k) and .spk.standard.1 == a fresh cut at the new .res.
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
        clu_u = f"{base}.clu.standard_U4.1"
        steps = [{"supersample": 4}, {"saveas": clu_u}, {"dump": True, "cluster": 2},
                 {"realign": 2}, {"dump": True, "cluster": 2},
                 {"show": "standard"}, {"realign": 3}, {"save": True}, {"dump": True, "cluster": 3}]
        rep = R.run(klusters, clu, steps, d, realignFromAnySpace=True, supersampleInterp=0)
        check(rep.get("code") == 0 and rep["rc"] == 0,
              f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", "")[-3000:])
        results = [r for r in rep.get("results", []) if "step" in r]
        for r in results:
            if not r.get("ok"):
                print("   step failed:", r)
        if rep.get("code") != 0:
            print(f"test_realign_base_from_supersampled_clu: FAIL ({fails + 1} failed)")
            return 1
        dumps = [r["dump"] for r in results if "dump" in r]
        check(dumps[0]["cluMethod"] == "standard_U4" and dumps[0]["active"] == "standard_U4"
              and dumps[0]["clu"] == clu_u, f"Save As adopted the _U4 space and reopened there ({dumps[0]['cluMethod']}, {dumps[0]['active']})")
        check(os.path.exists(clu_u), "rat.clu.standard_U4.1 exists")
        ph_after_u4 = np.asarray(dumps[1]["cluster"]["phases"], np.int64)
        check((ph_after_u4 != 0).any(), f"the x4 realign of cluster 2 gave {int((ph_after_u4 != 0).sum())} spikes a non-zero phase")
        check(dumps[-1]["active"] == "standard", "standard stayed the shown space for the base-rate realign")

        nch, ns = len(S.CHANS), S.NSAMP
        res = S.read_res(f"{base}.res.standard.1")
        clu_ids = S.read_clu(clu_u)
        c2, c3 = clu_ids == 2, clu_ids == 3
        moved = res != made["res"]
        check(moved[c3].any() and not moved[~(c2 | c3)].any(),
              f"the base-rate realign moved {int(moved[c3].sum())} of cluster 3's spikes by whole samples")
        fet_u = S.read_fet(f"{base}.fet.standard_U4.1")
        phase = fet_u[:, -2]
        check(np.array_equal(fet_u[:, -1], res), ".fet.standard_U4.1 (the .clu's own, followed): the time column is the new .res")
        check(not phase[c3].any(), "cluster 3's phases are still 0 (a whole-sample move keeps the phase)")
        check(np.array_equal(phase[c2], ph_after_u4), "cluster 2's phases from the x4 realign survived the follow")
        ticks = S.read_res(f"{base}.res.standard_U4.1")
        check(np.array_equal(ticks, res * 4 + phase), ".res.standard_U4.1 == 4 x .res + phase")
        spk_u = S.read_spk(f"{base}.spk.standard_U4.1", ns * 4, nch).reshape(-1, ns * 4, nch)
        recut = S.cut(made["fil"], res, S.CHANS, ns, S.PEAK).reshape(-1, ns, nch)
        bad = 0
        for i in np.flatnonzero(c2 | c3):
            p = int(phase[i])
            ks = np.arange(1 if p else 0, ns)
            if not np.array_equal(spk_u[i, 4 * ks - p, :], recut[i, ks, :]):
                bad += 1
        check(bad == 0, f".spk.standard_U4.1: tick 4k-phase == sample k of a fresh cut at the new .res for clusters 2 and 3 ({bad} rows differ)")
        spk_b = S.read_spk(f"{base}.spk.standard.1", ns, nch)
        check(np.array_equal(spk_b, recut.reshape(-1, ns * nch)), ".spk.standard.1 == a fresh cut at the new .res")
        t3 = np.asarray(dumps[-1]["cluster"]["times"], np.int64)
        check(np.array_equal(t3, res[c3]), "the loaded times of cluster 3 are the saved .res")
    print(f"test_realign_base_from_supersampled_clu: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
