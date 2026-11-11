#!/usr/bin/env python3
"""A super-sampled space is re-cut with the interpolator it was BUILT with, whatever the
preference says now (a space built by ndm_extractspikes --interp smooth under a Klusters whose
saved preference is the cubic spline failed the realign's raw-source verification: "unexplained
... RMS difference up to 8.4%").

Run 1 builds standard_U4 with the exact cubic spline (supersampleInterp 0) and saves.  Run 2 opens
the session with the smoothing spline preferred (2), shows standard_U4 and realigns cluster 2: the
realign must run (the file's interpolator is detected from its rows and used for the re-cut), the
re-cut rows at tick 4k must still equal the raw samples (the exact spline's signature), and the
diagnostic says which interpolator the file was built with.
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
        rep = R.run(klusters, clu, [{"supersample": 4}, {"save": True}], d, realignFromAnySpace=True, supersampleInterp=0)
        check(rep.get("code") == 0 and rep["rc"] == 0, f"run 1: standard_U4 built with the exact spline and saved (code {rep.get('code')})",
              rep.get("stderr", "")[-2000:])
        if rep.get("code") != 0:
            print(f"test_supersample_interp_detect: FAIL ({fails} failed)")
            return 1
        nch, ns = len(S.CHANS), S.NSAMP
        spk_u0 = S.read_spk(f"{base}.spk.standard_U4.1", ns * 4, nch).reshape(-1, ns * 4, nch)
        res0 = S.read_res(f"{base}.res.standard.1")
        recut0 = S.cut(made["fil"], res0, S.CHANS, ns, S.PEAK).reshape(-1, ns, nch)
        check(np.array_equal(spk_u0[:, ::4, :], recut0), "the exact spline's signature: tick 4k == raw sample k")

        steps = [{"show": "standard_U4"}, {"realign": 2}, {"dump": True, "cluster": 2}, {"save": True}]
        rep = R.run(klusters, clu, steps, d, realignFromAnySpace=True, supersampleInterp=2)
        results = [r for r in rep.get("results", []) if "step" in r]
        for r in results:
            if not r.get("ok"):
                print("   step failed:", r)
        check(rep.get("code") == 0 and rep["rc"] == 0, f"run 2 (smoothing spline preferred) ran every step (code {rep.get('code')})",
              rep.get("stderr", "")[-3000:])
        done = [r for r in rep.get("results", []) if "realignDone" in r]
        check(bool(done) and done[0]["realignDone"] and done[0]["nShifted"] > 0,
              f"the realign in standard_U4 ran under the other preference ({done[0]['nShifted'] if done else 'no record'} shifted)",
              (done[0]["log"][-2500:] if done else ""))
        check(not any("raw source verification FAILED" in r.get("log", "") for r in done), "no raw-source verification failure")
        check("was built with the spline interpolator" in rep.get("stderr", ""),
              "the diagnostic names the file's interpolator (spline) against the preference (smooth)", rep.get("stderr", "")[-1500:])
        clu_ids = S.read_clu(clu); c2 = clu_ids == 2
        res = S.read_res(f"{base}.res.standard.1")
        spk_u = S.read_spk(f"{base}.spk.standard_U4.1", ns * 4, nch).reshape(-1, ns * 4, nch)
        fet_u = S.read_fet(f"{base}.fet.standard_U4.1"); phase = fet_u[:, -2]
        recut = S.cut(made["fil"], res, S.CHANS, ns, S.PEAK).reshape(-1, ns, nch)
        bad = 0
        for i in np.flatnonzero(c2):
            p = int(phase[i]); ks = np.arange(1 if p else 0, ns)
            if not np.array_equal(spk_u[i, 4 * ks - p, :], recut[i, ks, :]):
                bad += 1
        check(bad == 0, f"the moved rows were re-cut with the exact spline (tick 4k-phase == a fresh raw cut; {bad} rows differ)")
    print(f"test_supersample_interp_detect: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
