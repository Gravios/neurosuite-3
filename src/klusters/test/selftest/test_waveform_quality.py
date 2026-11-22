#!/usr/bin/env python3
"""The quality dot of the waveform view: its value, its colour, its place, for parents and children.

Two synthetic units of 400 spikes.  The session is relabelled so that

  parent 2   = 240 spikes of the first unit                     (one unit)
  parent 3   = the second unit less 20 spikes + the other 160 spikes of the first unit   (two units)
  parent 5   = 20 spikes of the second unit                     (too few for a value)
  children   : 10002 (all of parent 2); 20000 and 20001 (parent 3, one unit each); 10005

Through the `waveformQuality` step, with the views holding every spike of a cluster:

  - the value of each dot is the normalised residual variance of that cluster's spikes about their
    median waveform, computed here with numpy from the .spk (waveformquality.h's definition);
  - the two-unit parent reads above either unit alone, and its children (one unit each) below it --
    parents and children are measured the same way (the session's .res is jittered by up to two
    samples on purpose, so even one unit reads high here);
  - with thresholds put between the two, the one-unit clusters are green and the mixed one red; moving
    the thresholds moves the colours through yellow and orange; the pixel at a dot's centre has the
    colour reported;
  - a filled dot is the quality colour alone, with no perimeter in the cluster's colour;
  - a cluster with fewer than 30 spikes has a hollow dot and no value;
  - side by side a dot sits at the top of the view, at the right end of its cluster's column, in
    cluster order; overlaid the dots line up at the view's upper right corner;
  - with the preference off there is no dot; the Preferences page has the group.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402

fails = 0
GREEN, YELLOW, ORANGE, RED = "#2eb84c", "#f0d628", "#f58c1e", "#e12d2d"


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def expected(w):
    """w: (n, nsamp, nch).  The definition of waveformquality.h."""
    n, ns, _ = w.shape
    if n < 30:
        return None
    w = w.astype(np.float64)
    k = max(2, ns // 10)
    edge = np.r_[0:k, ns - k:ns]
    med = np.median(w, 0)
    noise = (w[:, edge, :] ** 2).mean((0, 1))
    v = ((w - med) ** 2).mean(0)[k:ns - k]
    return float((v - noise).sum() / (med[k:ns - k] ** 2).sum())


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "wq", n_per_unit=400)
        unit = m["clu"].copy()
        spk = m["spk"].reshape(-1, S.NSAMP, len(S.CHANS))
        a, b = np.flatnonzero(unit == 2), np.flatnonzero(unit == 3)
        clu = unit.copy()
        clu[a[240:]] = 3
        clu[b[:20]] = 5
        clc = np.where(clu == 2, 10002, np.where(clu == 5, 10005, np.where(unit == 3, 20000, 20001))).astype(np.int32)
        clp = np.zeros(20001, np.int32)
        clp[10002 - 1], clp[10005 - 1], clp[20000 - 1], clp[20001 - 1] = 2, 5, 3, 3
        for kind, ids in (("clu", clu), ("clc", clc)):
            with open(f"{base}.{kind}.standard.1", "wb") as fh:
                np.array([np.unique(ids).size], np.int32).tofile(fh)
                ids.astype(np.int32).tofile(fh)
        with open(f"{base}.clp.standard.1", "wb") as fh:
            np.array([clp.size], np.int32).tofile(fh)
            clp.tofile(fh)
        want = {c: expected(spk[clu == c]) for c in (2, 3, 5)}
        want.update({c: expected(spk[clc == c]) for c in (10002, 20000, 20001)})
        lo, hi = max(want[2], want[20000], want[20001]), want[3]
        mid = (lo + hi) / 2
        thr = lambda y, o, r: {"yellow": y, "orange": o, "red": r}       # noqa: E731
        shot = os.environ.get("WQ_SHOT") or os.path.join(d, "wq.png")
        steps = [
            {"waveformQuality": {"show": [2, 3, 5], "nbSpikes": 2000, "overlay": False,
                                 "prefs": thr(mid, mid, mid), "saveTo": shot}},                   # 0
            {"waveformQuality": {"prefs": thr(hi + 1, hi + 2, hi + 3)}},                            # 1 all green
            {"waveformQuality": {"prefs": thr(lo - 3, hi + 1, hi + 2)}},                            # 2 all yellow
            {"waveformQuality": {"prefs": thr(lo - 3, lo - 2, hi + 1)}},                            # 3 all orange
            {"waveformQuality": {"prefs": thr(mid, mid, mid), "overlay": True}},                    # 4
            {"waveformQuality": {"overlay": False, "prefs": {"enabled": False}}},                   # 5
            {"waveformQuality": {"prefs": {"enabled": True, "diameter": 20}}},                      # 6
            {"waveformQuality": {"show": [10002, 20000, 20001], "children": True, "nbSpikes": 2000,
                                 "prefs": {"diameter": 12}}},                                       # 7
            {"prefPage": "PrefWaveformView"},                                                       # 8
        ]
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        res = rep.get("results", [])
        if rep.get("rc") != 0 or len(res) < len(steps) or any("waveformViews" not in r for r in res[:8]):
            print("  FAIL: run", rep.get("rc"), rep.get("stderr", "")[-1500:], [r.get("error") for r in res])
            print("FAILED")
            return 1
        views = [r["waveformViews"] for r in res[:8]]
        check(all(len(v) >= 1 for v in views), "the display has a waveform view")
        v0 = views[0][0]
        dots = {dd["cluster"]: dd for dd in v0["dots"]}
        check(sorted(dots) == [2, 3, 5], "one dot per shown cluster", str(sorted(dots)))

        for c in (2, 3):
            got = dots.get(c, {}).get("value")
            check(got is not None and abs(got - want[c]) <= 1e-6 * max(1.0, abs(want[c])),
                  f"parent {c}: the value is the numpy value ({want[c]:.4f})", f"got {got}")
        check(dots[2]["nSpikes"] == 240 and dots[3]["nSpikes"] == 540, "the views hold every spike of the clusters",
              f"{dots[2]['nSpikes']}, {dots[3]['nSpikes']}")
        check(want[3] > lo + 0.1, f"two units in one parent read higher than either unit alone ({want[3]:.3f} against {lo:.3f} at most)")
        check(dots[2].get("colour") == GREEN and dots[3].get("colour") == RED, "one unit green, two units red",
              f"{dots[2].get('colour')} {dots[3].get('colour')}")
        check(all(dots[c]["centrePixel"] == dots[c]["colour"] for c in (2, 3)), "the dots are painted in those colours",
              f"{[(dots[c]['centrePixel'], dots[c]['colour']) for c in (2, 3)]}")
        check("value" not in dots[5] and "colour" not in dots[5] and dots[5]["nSpikes"] == 20,
              "20 spikes: no value, a hollow dot", str(dots[5]))
        check(dots[5]["centrePixel"] not in (GREEN, YELLOW, ORANGE, RED), "the hollow dot is not filled", dots[5]["centrePixel"])

        # where: the top of the view, the right end of each column, in cluster order
        r2, r3, r5 = (dots[c]["rect"] for c in (2, 3, 5))
        check(all(0 <= r[1] <= 8 for r in (r2, r3, r5)), "the dots are at the top of the view", f"{r2} {r3} {r5}")
        check(r2[0] < r3[0] < r5[0] and r5[0] + r5[2] <= v0["width"], "one per column, left to right, inside the view",
              f"{r2} {r3} {r5} width {v0['width']}")
        col = v0["width"] / 3.0
        check(all(abs((r[0] + r[2]) - (i + 1) * col) <= 6 for i, r in enumerate((r2, r3, r5))),
              "each at the right end of its column", f"{r2} {r3} {r5} column {col:.0f}")
        check(r2[2] == r2[3] == 12, "12 px across by default", str(r2))
        check(os.path.getsize(shot) > 0, "the view was grabbed")

        for i, colour, name in ((1, GREEN, "green"), (2, YELLOW, "yellow"), (3, ORANGE, "orange")):
            cs = [dd.get("colour") for dd in views[i][0]["dots"] if dd["cluster"] in (2, 3)]
            check(cs == [colour, colour], f"thresholds moved: both {name}", str(cs))

        ov = sorted(views[4][0]["dots"], key=lambda dd: dd["cluster"])
        xs = [dd["rect"][0] for dd in ov]
        w = views[4][0]["width"]
        check(len(ov) == 3 and xs == sorted(xs) and all(w - 3 * 16 - 8 <= x <= w for x in xs) and
              all(dd["rect"][1] <= 8 for dd in ov), "overlaid: the dots line up at the upper right corner", f"{xs} width {w}")
        check(views[5][0]["dots"] == [], "preference off: no dot", str(views[5][0]["dots"]))
        check(all(dd["rect"][2] == 20 for dd in views[6][0]["dots"]) and len(views[6][0]["dots"]) == 3,
              "preference on again, 20 px across", str([dd["rect"] for dd in views[6][0]["dots"]]))
        # No perimeter in the cluster's colour: at 20 px a ring left 19-20 pixels of exactly that colour
        # in the dot's square; without it there are none.
        own = [(dd["cluster"], dd["clusterColourPixels"]) for dd in views[6][0]["dots"] if "colour" in dd]
        check(len(own) == 2 and all(n <= 6 for _, n in own), f"a filled dot has no perimeter in the cluster's colour {own}", str(own))

        kids = {dd["cluster"]: dd for dd in views[7][0]["dots"]}
        check(sorted(kids) == [10002, 20000, 20001], "child view: one dot per shown child", str(sorted(kids)))
        for c in (10002, 20000, 20001):
            got = kids.get(c, {}).get("value")
            check(got is not None and abs(got - want[c]) <= 1e-6 * max(1.0, abs(want[c])),
                  f"child {c}: the value is the numpy value ({want[c]:.4f})", f"got {got}")
        check(all(kids[c].get("colour") == GREEN for c in (10002, 20000, 20001)),
              "the children of the two-unit parent hold one unit each: green", str([kids[c].get("colour") for c in kids]))

        groups = res[8].get("groups", [])
        check(res[8].get("found") and "Cluster quality dot" in groups, "Preferences > Waveform view has the group", str(groups))
    print("FAILED" if fails else "PASSED")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
