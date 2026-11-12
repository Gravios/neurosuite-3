#!/usr/bin/env python3
"""The realign's correlation window (--kernel, Preferences > Realignment > Correlation window) keeps a
COLLISION from steering the lag.  On the synthetic session, 40 % of unit 2's spikes get a second, larger
spike 6-12 samples after their peak (inside the 32-sample window).  Realign cluster 2 with the classic
whole-window kernel and with `--kernel gauss`: the window must bring more of the collided spikes onto
their true time than the whole window does, and leave the clean spikes as well aligned as before.  The
template-energy window is run too and must not be worse than the whole window.
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


def collide(d, name, frac=0.4, after=(6, 12), seed=11):
    """Build the session, then add a bigger spike 6-12 samples (random per spike, so the mean template does
    not learn it) after the peak of a fraction of unit 2's spikes (in the .fil, the re-cut .spk and the
    .fet), and return (base, truth, collided mask, clu)."""
    base, made = S.make_session(d, name)
    rng = np.random.default_rng(seed)
    fil = made["fil"].astype(np.float64)
    clu, res, truth = made["clu"], made["res"], made["truth"]
    u2 = np.flatnonzero(clu == 2)
    hit = np.zeros(res.size, bool)
    hit[rng.choice(u2, int(frac * u2.size), replace=False)] = True
    tpl = S.template(1, S.NSAMP + 8, 16)[:, ::16][:, :S.NSAMP + 4] * 2.5     # the other unit's shape, larger
    for i in np.flatnonzero(hit):
        s = int(truth[i]) + int(rng.integers(after[0], after[1] + 1)) - (S.PEAK - 1)
        fil[s:s + tpl.shape[1], S.CHANS] += tpl.T
    fil = np.clip(np.rint(fil), -32768, 32767).astype(np.int16)
    fil.tofile(base + ".fil"); fil.tofile(base + ".dat")
    spk = S.cut(fil, res, S.CHANS, S.NSAMP, S.PEAK)
    spk.tofile(f"{base}.spk.standard.1")
    means, evecs = S.write_pca(f"{base}.pca.standard.1", spk, len(S.CHANS), S.NSAMP, S.NCOMP)
    pcs = S.project(spk, means, evecs, len(S.CHANS), S.NSAMP, S.NCOMP)
    fet = np.concatenate([pcs, res[:, None]], axis=1).astype(np.int64)
    with open(f"{base}.fet.standard.1", "wb") as fh:
        np.array([fet.shape[1]], np.int32).tofile(fh); fet.tofile(fh)
    return base, truth, hit, clu


def realign_err(klusters, d, name, prefs):
    base, truth, hit, clu = collide(d, name)
    rep = R.run(klusters, f"{base}.clu.standard.1", [{"realign": 2}, {"save": True}], d, realignFromAnySpace=True,
                **prefs)
    ok = rep.get("code") == 0 and rep["rc"] == 0
    res = S.read_res(f"{base}.res.standard.1")
    err = res - truth
    u2 = clu == 2
    hist = []
    for what, m in (("collided", u2 & hit), ("clean", u2 & ~hit)):
        v, c = np.unique(err[m], return_counts=True)
        hist.append(f"{what} res-truth " + " ".join(f"{int(a):+d}:{int(b)}" for a, b in zip(v, c)))
    return ok, float(np.mean(np.abs(err[u2 & hit]) <= 1)), float(np.mean(np.abs(err[u2 & ~hit]) <= 1)), rep, "; ".join(hist)


def main():
    klusters = R.skip_without_binary()
    out = {}
    for label, prefs in (("whole", dict(realignKernel=0)),
                         ("gauss", dict(realignKernel=1, realignKernelWidth=5)),
                         ("template", dict(realignKernel=3))):
        with tempfile.TemporaryDirectory() as d:
            ok, hit_ok, clean_ok, rep, hist = realign_err(klusters, d, "rat", prefs)
            check(ok, f"{label}: the realign ran and saved (code {rep.get('code')})", rep.get("stderr", "")[-1500:])
            log = next((r["log"] for r in rep.get("results", []) if "realignDone" in r), "")
            if label != "whole":
                check("kernel=" in log and ("gauss" in log or "template" in log),
                      f"{label}: the realign log names the window", log[-800:])
            out[label] = (hit_ok, clean_ok)
            print(f"    {label:9s} collided spikes within 1 sample of their true time: {100 * hit_ok:.0f} %   clean: {100 * clean_ok:.0f} %   ({hist})")
    w, g, t = out["whole"], out["gauss"], out["template"]
    check(g[0] > w[0] + 0.1, f"gauss: more collided spikes land within a sample of their true time than with the whole window ({g[0]:.2f} vs {w[0]:.2f})")
    check(g[1] >= w[1] - 0.05, f"gauss: the clean spikes are aligned as well ({g[1]:.2f} vs {w[1]:.2f})")
    check(t[0] >= w[0] - 0.05 and t[1] >= w[1] - 0.05, f"template energy: not worse than the whole window ({t[0]:.2f}/{t[1]:.2f} vs {w[0]:.2f}/{w[1]:.2f})")
    print(f"test_realign_kernel: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
