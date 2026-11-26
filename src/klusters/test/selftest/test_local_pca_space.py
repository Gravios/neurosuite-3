#!/usr/bin/env python3
"""The local PCA space (View > Local PCA of the Selection, Shift+Y).

A per-channel PCA fitted on the selected clusters alone and applied to every spike, shown in
place of the features on screen until the key is pressed again.  Checked on the session of
test_child_ids.py (parents 2, 3, 5, 7; parent 2 has two children):

  * with parent 2 selected the key shows a space called "local PCA"; the features of every row
    probed are the projection on a basis fitted here, in numpy, on parent 2's rows alone -- and
    not the shown space's own features;
  * the dimension count and the time column do not change;
  * the key again: the standard features are back, value for value;
  * a realign asked for in the local space leaves it first and then runs in `standard`;
  * nothing was written: the .fet on disk is the one the session was built with;
  * in the child view, with one child of parent 2 clicked, the basis is that child's rows, not
    the parent's.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S            # noqa: E402
import run as R                # noqa: E402
import test_child_ids as T     # noqa: E402

fails = 0
ROWS = list(range(0, 1600, 37))
KEY = {"keyCommand": "action.localPcaSpace"}
FEAT = {"collisionRows": {"rows": ROWS}}
NCH, NSAMP, NCOMP = len(S.CHANS), S.NSAMP, S.NCOMP


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def local_pca(spk, rows):
    """What KlustersDoc::showLocalPcaSpace computes: per channel, the mean and the leading
    eigenvectors of the covariance over `rows`, the largest entry of each made positive, and
    every spike projected and rounded."""
    X = spk.reshape(len(spk), NSAMP, NCH).astype(np.float64)
    out = np.zeros((len(spk), NCH * NCOMP))
    for ch in range(NCH):
        x = X[rows][:, :, ch]
        mu = x.mean(0)
        w, v = np.linalg.eigh(np.cov(x - mu, rowvar=False))
        ev = v[:, np.argsort(w)[::-1][:NCOMP]].T.copy()
        for k in range(NCOMP):
            if ev[k][np.argmax(np.abs(ev[k]))] < 0:
                ev[k] *= -1
        out[:, ch * NCOMP:(ch + 1) * NCOMP] = (X[:, :, ch] - mu) @ ev.T
    return out


def feats(result):
    return np.array([r["features"] for r in result["records"]], float)


def close(a, b):
    return a.shape == b.shape and float(np.max(np.abs(a - b))) <= 1.01      # rounding, and the two eigen-solvers


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, clu, clc = T.build(d)
        clu_path = f"{base}.clu.standard.1"
        spk = S.read_spk(f"{base}.spk.standard.1", NSAMP, NCH)
        fet0 = S.read_fet(f"{base}.fet.standard.1")
        with open(f"{base}.fet.standard.1", "rb") as fh:
            fet_bytes = fh.read()
        rows = [r for r in ROWS if r < len(clu)]
        own = fet0[rows, :-1].astype(float)

        # ── the parent layer ─────────────────────────────────────────────────────────────
        steps = [{"dump": True}, {"collisionRows": {"rows": rows}}, {"select": [2]}, KEY, {"dump": True},
                 {"collisionRows": {"rows": rows}}, KEY, {"dump": True}, {"collisionRows": {"rows": rows}},
                 {"select": [2]}, KEY, {"dump": True}, {"realign": 2}, {"dump": True}]
        rep = R.run(klusters, clu_path, steps, d)
        res = [r for r in rep.get("results", []) if "step" in r]
        check(rep.get("rc") == 0 and len(res) == len(steps), f"the run did every step (rc {rep.get('rc')}, {len(res)} of {len(steps)})",
              rep.get("stderr", "")[-1500:])
        if rep.get("rc") != 0 or len(res) != len(steps):
            print(f"test_local_pca_space: FAIL ({fails} failed)")
            return 1
        dumps = [r["dump"] for r in res if "dump" in r]
        f = [feats(r) for r in res if "records" in r]
        check(dumps[0]["active"] == "standard" and close(f[0], own), "at the start the standard features are shown")
        want = local_pca(spk, np.flatnonzero(clu == 2))[rows]
        check(dumps[1]["active"] == "local PCA", f"Shift+Y shows the space 'local PCA' ({dumps[1]['active']!r})")
        check(close(f[1], want), "its features are the projection on a basis fitted on parent 2's spikes alone",
              f"largest difference {np.max(np.abs(f[1] - want)) if f[1].shape == want.shape else f[1].shape}")
        check(not close(f[1], own), "and not the standard space's features")
        check(f[1].shape == f[0].shape, f"the dimension count is unchanged ({f[1].shape[1]} + time)")
        check(dumps[2]["active"] == "standard" and np.array_equal(f[2], f[0]), "Shift+Y again: the standard features are back, value for value")
        check(dumps[3]["active"] == "local PCA", "the local space can be entered again")
        done = [r for r in rep.get("results", []) if "realignDone" in r]
        check(dumps[4]["active"] == "standard" and len(done) == 1 and done[0]["realignDone"],
              f"a realign asked for in the local space leaves it and runs in standard ({dumps[4]['active']!r})",
              (done[0].get("log", "")[-800:] if done else "no realign record"))
        with open(f"{base}.fet.standard.1", "rb") as fh:
            check(fh.read() == fet_bytes, "the .fet on disk is untouched (the realign is not saved, the local table never written)")
        check(not [p for p in os.listdir(d) if "local" in p.lower()], "no file carries the local space")

    # ── the child layer ──────────────────────────────────────────────────────────────────
    with tempfile.TemporaryDirectory() as d:
        base, clu, clc = T.build(d)
        spk = S.read_spk(f"{base}.spk.standard.1", NSAMP, NCH)
        rows = [r for r in ROWS if r < len(clu)]
        steps = [{"hierarchy": {}}, {"select": [2]}, {"childPalette": {"show": True}}]
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        hier = [r for r in rep.get("results", []) if "childParents" in r]
        kids = sorted(int(c) for c, p in (hier[0]["childParents"].items() if hier else []) if p == 2)
        check(len(kids) == 2, f"parent 2 has two children ({kids})")
        if len(kids) == 2:
            steps = [{"hierarchy": {}}, {"select": [2]}, {"childPalette": {"show": True}}, {"childPalette": {"click": kids[0]}},
                     KEY, {"dump": True}, {"collisionRows": {"rows": rows}}]
            rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
            res = [r for r in rep.get("results", []) if "step" in r]
            ok = rep.get("rc") == 0 and len(res) == len(steps)
            check(ok, f"the child-view run did every step (rc {rep.get('rc')}, {len(res)} of {len(steps)})", rep.get("stderr", "")[-1500:])
            if ok:
                dump = [r["dump"] for r in res if "dump" in r][0]
                got = [feats(r) for r in res if "records" in r][0]
                half = np.arange(len(clu)) % 2 == 0
                cands = {"even rows": np.flatnonzero((clu == 2) & half), "odd rows": np.flatnonzero((clu == 2) & ~half)}
                match = [k for k, rr in cands.items() if close(got, local_pca(spk, rr)[rows])]
                check(dump["active"] == "local PCA" and len(match) == 1,
                      f"in the child view the basis is the clicked child's spikes ({match or 'neither child'})")
                check(not close(got, local_pca(spk, np.flatnonzero(clu == 2))[rows]), "and not the whole parent's")
    print(f"test_local_pca_space: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
