#!/usr/bin/env python3
"""The post-alignment centering honours the correlation window (--kernel).  On test_realign_kernel's
collided unit (40 % of unit 2's spikes carry a second, larger spike 6-12 samples after their peak) the RMS
circular recenter measured on the whole window puts the cluster's energy centroid in the collision tail and
moves EVERY spike of the cluster there, the clean ones included -- after the windowed passes had aligned
them.  Under the same Gaussian window the recenter must leave the cluster on its true time.  The PCA
refine is only checked to run under the window and say so: on this two-unit fixture the refine misplaces
the cluster with or without a window (it maximises the energy of the deviation from the basis mean), so
there is no accuracy to assert for it here.
"""
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as R                          # noqa: E402
import test_realign_kernel as K          # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n" + extra) if (not ok and extra) else ""))
    fails += not ok


def main():
    klusters = R.skip_without_binary()
    out = {}
    for label, prefs in (("rms, whole window", dict(realignMode=2, realignKernel=0)),
                         ("rms, gauss", dict(realignMode=2, realignKernel=1, realignKernelWidth=5)),
                         ("pca, gauss", dict(realignMode=1, realignKernel=1, realignKernelWidth=5))):
        with tempfile.TemporaryDirectory() as d:
            ok, hit_ok, clean_ok, rep, hist = K.realign_err(klusters, d, "rat", prefs)
            check(ok, f"{label}: the realign ran and saved (code {rep.get('code')})", rep.get("stderr", "")[-1500:])
            log = next((r["log"] for r in rep.get("results", []) if "realignDone" in r), "")
            out[label] = (hit_ok, clean_ok, log)
            print(f"    {label:18s} within 1 sample of the true time: collided {100 * hit_ok:.0f} %   clean {100 * clean_ok:.0f} %   ({hist})")
    w, g, p = out["rms, whole window"], out["rms, gauss"], out["pca, gauss"]
    check("RMS recenter: under the gauss" in g[2], "rms, gauss: the log says the recenter ran under the window", g[2][-800:])
    check(g[0] >= 0.9 and g[1] >= 0.9,
          f"rms, gauss: the cluster stays on its true time ({g[0]:.2f} collided / {g[1]:.2f} clean within a sample)")
    check(g[1] > w[1] + 0.5,
          f"rms, gauss: the clean spikes are not carried into the collision tail as the whole-window recenter carries them ({g[1]:.2f} vs {w[1]:.2f})")
    check("PCA-projection refine under the gauss" in p[2], "pca, gauss: the log says the refine ran under the window", p[2][-800:])
    print(f"test_realign_center_window: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
