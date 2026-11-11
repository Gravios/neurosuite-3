#!/usr/bin/env python3
"""A super-sampled space FOLLOWS a realign made in the .clu's own space (0960, step 1).

On the synthetic session: build standard_U4 (Actions > Super-sample x4), realign cluster 2 in
`standard` (the .clu's space), Save; then:
  * the shared .res moved for some of cluster 2's spikes (the realign had whole-sample work:
    the session's .res is jittered), and no other cluster's;
  * .res.standard_U4.1 (ticks) == 4 x .res (every phase 0: the move was made at the base rate);
  * .fet.standard_U4.1: the time column == .res, the phase column == 0 everywhere;
  * .spk.standard_U4.1 followed: for EVERY spike its tick 4k equals sample k of
    .spk.standard.1 (the spline is exact at the samples; the base .spk was re-cut at the new
    times by the realign), moved spikes included -- before 0960 the _U4 space was left stale.
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


def case(klusters, shown):
    """Realign cluster 2 with `shown` on screen (the .clu's own `standard`, or the _U4 space:
    today's "from any space" swaps to the .clu's space and back; the _U4 space follows either way)."""
    print(f"-- realign with {shown} shown")
    with tempfile.TemporaryDirectory() as d:
        base, made = S.make_session(d, "rat")
        clu = f"{base}.clu.standard.1"
        steps = [{"supersample": 4}, {"show": shown}, {"realign": 2},
                 {"dump": True, "cluster": 2}, {"save": True}, {"dump": True, "cluster": 2}]
        rep = R.run(klusters, clu, steps, d, realignFromAnySpace=True)
        check(rep.get("code") == 0 and rep["rc"] == 0,
              f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", ""))
        results = [r for r in rep.get("results", []) if "step" in r]
        for r in results:
            if not r.get("ok"):
                print("   step failed:", r)
        if rep.get("code") != 0:
            return
        tok = results[0].get("token")
        check(tok == "standard_U4", f"the super-sampled space is standard_U4 (got {tok!r})")

        nch, ns = len(S.CHANS), S.NSAMP
        res = S.read_res(f"{base}.res.standard.1")
        clu_ids = S.read_clu(clu)
        moved = res != made["res"]
        check(moved.any() and not moved[clu_ids != 2].any(),
              f"the realign moved {int(moved.sum())} of cluster 2's {int((clu_ids == 2).sum())} spikes and nothing else")
        ticks = S.read_res(f"{base}.res.standard_U4.1")
        check(ticks.shape == res.shape and np.array_equal(ticks, res * 4),
              ".res.standard_U4.1 is the moved .res in ticks (x4, phase 0)")
        fet_u = S.read_fet(f"{base}.fet.standard_U4.1")
        check(np.array_equal(fet_u[:, -1], res), ".fet.standard_U4.1: the time column is the moved .res")
        check(not fet_u[:, -2].any(), ".fet.standard_U4.1: every phase is 0 (a base-rate move)")
        spk_b = S.read_spk(f"{base}.spk.standard.1", ns, nch)
        spk_u = S.read_spk(f"{base}.spk.standard_U4.1", ns * 4, nch)
        check(spk_b.shape[0] == spk_u.shape[0] == res.size, "both .spk hold every spike")
        # the base .spk was re-cut at the moved times
        recut = S.cut(made["fil"], res, S.CHANS, ns, S.PEAK)
        check(np.array_equal(spk_b, recut), ".spk.standard.1 == a fresh cut at the moved times")
        # tick 4k of the _U4 window == sample k of the base window, for every spike
        u = spk_u.reshape(-1, ns * 4, nch)[:, ::4, :].reshape(-1, ns * nch)
        bad = np.flatnonzero((u != spk_b).any(1))
        check(bad.size == 0, f".spk.standard_U4.1 followed: tick 4k == sample k for every spike ({bad.size} differ)",
              f"first differing rows {bad[:10].tolist()}, moved among them: {int(moved[bad].sum()) if bad.size else 0}")
        check(np.array_equal(u[moved], spk_b[moved]) and moved.any(),
              "... the moved spikes included (the _U4 space followed the realign)")
        # the dumps: the loaded times of cluster 2 after the realign are the saved .res
        dumps = [r["dump"] for r in results if "dump" in r]
        t_loaded = np.asarray(dumps[-1]["cluster"]["times"], np.int64)
        check(np.array_equal(t_loaded, res[clu_ids == 2]), "the loaded times of cluster 2 are the saved .res")
        spaces = {s["method"]: s for s in dumps[-1]["spaces"]}
        check("standard_U4" in spaces and spaces["standard_U4"]["usable"] and not spaces["standard_U4"].get("syncStale"),
              "after Save the _U4 space is usable and not stale")
        check(dumps[-1]["active"] == shown, f"the space shown is back to {shown} (got {dumps[-1]['active']!r})")


def main():
    klusters = R.skip_without_binary()
    for shown in ("standard", "standard_U4"):
        case(klusters, shown)
    print(f"test_follow_supersampled: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
