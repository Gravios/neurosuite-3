#!/usr/bin/env python3
"""Hovering over the feature view tracks the nearest spike and draws it over the waveform view.

Two units of 300 spikes (clusters 2 and 3).  Driven through the self-test's `select` and `hover` steps
(the cursor is put where the spike is drawn, as a mouse move there does):

  - the cluster view tracks a spike drawn under the cursor (several spikes can share a pixel; the one
    tracked is drawn within a pixel of where the cursor was put) and names its cluster;
  - every waveform view overlays exactly the tracked spike: its samples are the .spk row, channel by channel;
  - the overlay colour differs clearly from the cluster's colour and from the background, and pixels of
    that colour are on the waveform view while the spike is hovered and gone once the cursor leaves;
  - moving to a spike of the other cluster switches the overlay to it, in that cluster's column;
  - a spike of a cluster that is not shown cannot be hovered;
  - Shift+V (the toggle) stops the tracking and starts it again;
  - Preferences > Cluster view (through the `prefs` key of the step, applied as the dialog's Apply does):
    by default the tracking is on, the ring is the point size + 4 px with a 2 px line and the trace is
    2 px; a wider trace paints more pixels of the overlay colour on the waveform view; a larger ring
    radius widens the ring and a wider ring line paints more of it; with the preference off no view
    tracks, and on again it does; the page has the "Spike under the cursor" group.
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


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def rgb(name):
    return np.array([int(name[i:i + 2], 16) for i in (1, 3, 5)], float)


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "hv", n_per_unit=300)
        clu, spk = m["clu"], m["spk"]                       # spk: (n, NSAMP, nch), sample-major as on disk
        a, b = int(np.flatnonzero(clu == 2)[17]), int(np.flatnonzero(clu == 2)[201])
        c = int(np.flatnonzero(clu == 3)[42])
        steps = [{"select": [2, 3]},
                 {"hover": {"spike": a}},                    # 1
                 {"hover": {"spike": b}},                    # 2
                 {"hover": {"spike": c}},                    # 3
                 {"hover": {"leave": True, "colourOf": 3}},  # 4
                 {"hover": {"toggle": True}},                # 5  off
                 {"hover": {"spike": a}},                    # 6
                 {"hover": {"toggle": True}},                # 7  on again
                 {"hover": {"spike": a}},                    # 8
                 {"select": [2]},                            # 9
                 {"hover": {"spike": c}},                    # 10 not shown
                 {"hover": {"spike": b}}]                    # 11
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        res = rep.get("results", [])
        check(rep.get("rc") == 0 and len(res) == len(steps), "the session ran every step",
              f"rc {rep.get('rc')}, {len(res)} of {len(steps)} results\n{rep.get('stderr', '')[-1500:]}")
        if len(res) != len(steps):
            return 1

        def wave_ok(r, i):
            views = r.get("waveformViews", [])
            if not views:
                return False, "no waveform view"
            want = spk[i].reshape(S.NSAMP, -1).T.astype(np.float32).ravel()       # the view holds it channel-major
            for v in views:
                got = np.asarray(v.get("wave", []), np.float32)
                if v.get("spike") != i or got.shape != want.shape or not np.array_equal(got, want):
                    return False, f"view shows spike {v.get('spike')}, {got.size} samples"
            return True, ""

        def tracked(r, cl):
            """the spike tracked is of cluster cl and drawn within a pixel of the cursor"""
            t = r.get("spike", -1)
            return (r.get("drawn") and t >= 0 and r.get("cluster") == cl and int(clu[t]) == cl
                    and abs(r.get("trackedX", 1e9) - r["x"]) <= 1 and abs(r.get("trackedY", 1e9) - r["y"]) <= 1)

        for k, i, cl, name in ((1, a, 2, "a spike of cluster 2"), (2, b, 2, "another spike of cluster 2"),
                               (3, c, 3, "a spike of cluster 3")):
            r = res[k]
            check(tracked(r, cl), f"{name}: the cluster view tracks a spike of cluster {cl} drawn under the cursor",
                  str({q: r.get(q) for q in ('drawn', 'spike', 'cluster', 'x', 'y', 'trackedX', 'trackedY')}))
            ok, why = wave_ok(r, r.get("spike", -1))
            check(ok, f"{name}: the waveform view overlays the tracked spike's .spk samples", why)
            for v in r.get("waveformViews", []):
                oc, cc, bg = rgb(v["overlayColour"]), rgb(v["clusterColour"]), rgb(v["background"])
                check(np.linalg.norm(oc - cc) >= 150 and np.linalg.norm(oc - bg) >= 150,
                      f"{name}: the overlay colour {v['overlayColour']} contrasts with the cluster's {v['clusterColour']} and the background {v['background']}",
                      f"distances {np.linalg.norm(oc - cc):.0f}, {np.linalg.norm(oc - bg):.0f}")
                check(v.get("overlayPixels", 0) >= 20, f"{name}: the overlay is painted ({v.get('overlayPixels')} pixels of its colour)")
        check(res[1].get("spike") != res[2].get("spike"), "moving within a cluster changes the spike tracked")
        check(all(v.get("cluster") == 3 for v in res[3]["waveformViews"]), "moving to the other cluster moves the overlay to it")
        for v2, v3 in zip(res[2]["waveformViews"], res[3]["waveformViews"]):
            check(v2.get("overlayXMax", 1e9) < v3.get("overlayXMin", -1),
                  "side by side, cluster 2's spike is drawn in cluster 2's column and cluster 3's in the next one",
                  f"cluster 2 columns {v2.get('overlayXMin')}..{v2.get('overlayXMax')}, cluster 3 {v3.get('overlayXMin')}..{v3.get('overlayXMax')} of {v3.get('width')}")

        r = res[4]
        check(r.get("spike") == -1 and all(v.get("spike") == -1 and not v.get("wave") for v in r["waveformViews"]),
              "the cursor leaves: nothing is tracked and the overlay's samples are dropped")
        check(all(v.get("overlayPixels", 1) == 0 for v in r["waveformViews"]),
              "the cursor leaves: no pixel of the overlay colour is left on the waveform view",
              str([v.get("overlayPixels") for v in r["waveformViews"]]))

        check(res[5].get("active") is False and res[6].get("spike") == -1 and all(v.get("spike") == -1 for v in res[6]["waveformViews"]),
              "toggled off: a move over a spike tracks nothing")
        check(res[7].get("active") is True and tracked(res[8], 2) and wave_ok(res[8], res[8].get("spike", -1))[0],
              "toggled on again: the spike is tracked and overlaid")

        check(res[10].get("drawn") is False and res[10].get("spike") == -1,
              "a spike of a cluster that is not shown is not among the points and cannot be hovered",
              str({q: res[10].get(q) for q in ('drawn', 'spike')}))
        check(tracked(res[11], 2) and wave_ok(res[11], res[11].get("spike", -1))[0], "with one cluster shown, its spikes are still tracked and overlaid")
    print("-- Preferences > Cluster view")
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "hv", n_per_unit=300)
        a = int(np.flatnonzero(m["clu"] == 2)[17])
        png = os.environ.get("HOVER_PREF_PNG", os.path.join(d, "clusterview.png"))
        steps = [{"select": [2]},
                 {"hover": {"spike": a}},                                              # 1 the defaults
                 {"hover": {"spike": a, "prefs": {"traceWidth": 6.0}}},                # 2
                 {"hover": {"spike": a, "prefs": {"traceWidth": 2.0, "ringMargin": 14}}},   # 3
                 {"hover": {"spike": a, "prefs": {"ringWidth": 6.0}}},                 # 4
                 {"hover": {"spike": a, "prefs": {"enabled": False}}},                 # 5
                 {"hover": {"spike": a, "prefs": {"enabled": True, "ringMargin": 4, "ringWidth": 2.0}}},   # 6
                 {"prefPage": "PrefClusterView", "saveTo": png, "apply": True},        # 7
                 {"hover": {"spike": a}}]                                              # 8
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        res = rep.get("results", [])
        check(rep.get("rc") == 0 and len(res) == len(steps), "the session ran every step",
              f"rc {rep.get('rc')}, {len(res)} of {len(steps)} results\n{rep.get('stderr', '')[-1500:]}")
        if len(res) == len(steps):
            def trace(r):
                return [v.get("overlayPixels", 0) for v in r.get("waveformViews", [])]
            r1 = res[1]
            check(r1.get("prefEnabled") is True and r1.get("ringMargin") == 4 and r1.get("ringWidth") == 2.0 and r1.get("traceWidth") == 2.0,
                  "the defaults: on, ring point size + 4 px with a 2 px line, trace 2 px",
                  str({q: r1.get(q) for q in ('prefEnabled', 'ringMargin', 'ringWidth', 'traceWidth')}))
            check(r1.get("ringPixels", 0) > 0, f"the ring is painted on the feature view ({r1.get('ringPixels')} pixels, {r1.get('ringSpan')} wide)")
            check(all(w6 >= 1.8 * w2 > 0 for w2, w6 in zip(trace(r1), trace(res[2]))) and trace(r1),
                  f"a 6 px trace paints more of the waveform view than a 2 px one ({trace(r1)} -> {trace(res[2])})")
            check(res[3].get("ringSpan", 0) >= r1.get("ringSpan", 1e9) + 16,
                  f"a ring radius 10 px larger widens the ring ({r1.get('ringSpan')} -> {res[3].get('ringSpan')} px)")
            check(trace(res[3]) == trace(r1), f"and the trace is back at its 2 px ({trace(res[3])})")
            check(res[4].get("ringPixels", 0) >= 1.8 * res[3].get("ringPixels", 1e9),
                  f"a 6 px ring line paints more than a 2 px one ({res[3].get('ringPixels')} -> {res[4].get('ringPixels')})")
            check(res[5].get("active") is False and res[5].get("spike") == -1 and all(v.get("spike") == -1 for v in res[5]["waveformViews"]),
                  "the preference off: the view does not track and nothing is overlaid")
            check(res[6].get("active") is True and res[6].get("spike") == r1.get("spike") and res[6].get("ringPixels") == r1.get("ringPixels"),
                  "the preference on again, at the defaults: the same spike, the same ring",
                  str({q: res[6].get(q) for q in ('active', 'spike', 'ringPixels')}))
            check(res[7].get("found") is True and "Spike under the cursor" in res[7].get("groups", []),
                  f"the Cluster view page has the group ({res[7].get('groups')})")
            check(res[8].get("active") is True and res[8].get("ringPixels") == r1.get("ringPixels") and trace(res[8]) == trace(r1),
                  "the dialog's values written back change nothing")
    print(f"test_hover_spike: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
