#!/usr/bin/env python3
"""Strip Residual Outliers runs per TIME CHUNK (the Template view's partition, or uniform chunks of the
default template chunk length).  A unit that drifts across the probe has no session-wide template: the
median over the whole recording is the shape of no moment in particular and the spread around it is
drift plus contamination, so the tail is sited on the sum and the contaminants hide under it.

The fixture: unit 2's channel profile moves linearly from one profile to another over an 80 s session
(`drift` = how far), 8 % of its spikes carry a weak foreign waveform, and the unit-3 spikes that happen
to fall inside a unit-2 window count as contaminants too.  Every spike sits at its true time.  The scan
is the action's own (`residualScan` in the self-test spec: no dialog, nothing applied) at its defaults,
on the whole session and in 10 s chunks:

  - with drift, the chunked scan finds far more of the contaminants and flags about as few clean
    spikes (within one point);
  - without drift it still finds them all -- chunking costs a little there (smaller samples, noisier
    tails) and the check bounds that cost;
  - chunks too small for a statistic borrow their neighbours and say so;
  - a seed (a clean core carved into its own cluster) is read per chunk as well.
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


def build(d, drift=1.0, frac=0.08, cscale=0.35, n=1200, dur=80.0, seed=5, seedfrac=0.0):
    base, made = S.make_session(d, "rat", n_per_unit=n, dur=dur)
    rng = np.random.default_rng(seed)
    fil = made["fil"].astype(np.float64)
    res, clu, truth, sub = made["res"], made["clu"], made["truth"], made["sub"]
    up = 16
    tA = S.template(0, S.NSAMP + 8, up)
    gA = np.array([1.0, 0.7, 0.3, 0.1]); gB = np.array([0.35, 0.8, 1.0, 0.45])
    wave = tA[0] / gA[0]                                       # the unit's time course, upsampled
    tC = S.template(1, S.NSAMP + 8, up)
    u2 = np.flatnonzero(clu == 2)
    Tn = fil.shape[0]
    cont = np.zeros(res.size, bool)
    cont[rng.choice(u2, int(frac * u2.size), replace=False)] = True
    for i in u2:
        phase = int(round(-sub[i] * up))
        s = int(truth[i]) - (S.PEAK - 1)
        g = drift * truth[i] / Tn                              # 0 -> drift over the session
        w = wave[phase::up][:S.NSAMP + 4]
        fil[s:s + w.size, S.CHANS] += (g * (gB - gA))[None, :] * w[:, None]
        if cont[i]:
            c = tC[:, phase::up][:, :S.NSAMP + 4] * cscale
            fil[s:s + c.shape[1], S.CHANS] += c.T
    fil = np.clip(np.rint(fil), -32768, 32767).astype(np.int16)
    fil.tofile(base + ".fil"); fil.tofile(base + ".dat")
    # An ALIGNED sort: every spike at its true time (the fixture's .res is jittered by +-2 samples
    # to give the realign tests something to do; here that would only be a second contamination).
    order = np.argsort(truth, kind="stable")
    res = truth[order].astype(np.int64); clu = clu[order]; cont = cont[order]
    res.tofile(f"{base}.res.standard.1")
    with open(f"{base}.clu.standard.1", "wb") as fh:
        np.array([int(clu.max()) + 1], np.int32).tofile(fh); clu.astype(np.int32).tofile(fh)
    # A unit-3 spike inside a unit-2 window is a contaminant the fixture made by itself: count it.
    t2 = res[clu == 2]; t3 = res[clu == 3]
    near = np.abs(t2[:, None] - t3[None, :]).min(1) <= S.NSAMP
    coll = np.zeros(res.size, bool); coll[np.flatnonzero(clu == 2)[near]] = True
    cont = cont | coll
    if seedfrac > 0:                                           # carve a clean core of unit 2 into cluster 4
        clean = np.flatnonzero((clu == 2) & ~cont)
        clu = clu.copy(); clu[rng.choice(clean, int(seedfrac * clean.size), replace=False)] = 4
        with open(f"{base}.clu.standard.1", "wb") as fh:
            np.array([int(clu.max()) + 1], np.int32).tofile(fh); clu.astype(np.int32).tofile(fh)
    spk = S.cut(fil, res, S.CHANS, S.NSAMP, S.PEAK)
    spk.tofile(f"{base}.spk.standard.1")
    means, evecs = S.write_pca(f"{base}.pca.standard.1", spk, len(S.CHANS), S.NSAMP, S.NCOMP)
    pcs = S.project(spk, means, evecs, len(S.CHANS), S.NSAMP, S.NCOMP)
    fet = np.concatenate([pcs, res[:, None]], axis=1).astype(np.int64)
    with open(f"{base}.fet.standard.1", "wb") as fh:
        np.array([fet.shape[1]], np.int32).tofile(fh); fet.tofile(fh)
    return base, clu, cont, res

def scan(klusters, drift, chunk, **kw):
    with tempfile.TemporaryDirectory() as d:
        base, clu, cont, res = build(d, drift=drift, **{k: v for k, v in kw.items() if k in ("frac", "cscale", "n", "dur", "seedfrac")})
        step = {"residualScan": dict(clusters=[2], chunkSeconds=chunk, **{k: v for k, v in kw.items() if k in ("kOn", "kOff", "minSpikes", "seed", "seedThreshold")})}
        if "clusters" in kw: step["residualScan"]["clusters"] = kw["clusters"]
        rep = R.run(klusters, f"{base}.clu.standard.1", [step], d)
        r = next((x for x in rep.get("results", []) if "flagged" in x), {})
        fl = np.zeros(res.size, bool)
        for idx in r.get("flagged", {}).values(): fl[np.asarray(idx, int)] = True
        u2 = clu == 2
        tp = int((fl & cont & u2).sum()); fp = int((fl & ~cont & u2).sum()); nc = int((cont & u2).sum())
        return dict(code=rep.get("code"), chunks=r.get("chunks"), tp=tp, fp=fp, n_cont=nc, n=int(u2.sum()),
                    recall=tp / max(nc, 1), fp_rate=fp / max(int((u2 & ~cont).sum()), 1),
                    lines=r.get("lines", []), reason=r.get("reason", ""), err=rep.get("stderr", "")[-1500:])



def main():
    K = R.skip_without_binary()

    def show(tag, r):
        print(f"    {tag:34s} {r['chunks']:>2} chunk(s): {100 * r['recall']:5.1f} % of {r['n_cont']} contaminants found, "
              f"{100 * r['fp_rate']:4.1f} % of the clean spikes flagged")

    whole = scan(K, 0.5, 0);   show("drift, whole session", whole)
    chunk = scan(K, 0.5, 10);  show("drift, 10 s chunks", chunk)
    check(whole["code"] == 0 and chunk["code"] == 0, "the scans ran", whole["err"] + chunk["err"])
    check(whole["chunks"] == 1 and chunk["chunks"] == 8, f"80 s in 10 s chunks is 8 chunks ({chunk['chunks']})")
    check(chunk["recall"] > whole["recall"] + 0.25,
          f"drift: the chunked scan finds far more of the contaminants ({chunk['recall']:.2f} vs {whole['recall']:.2f})")
    check(chunk["fp_rate"] <= whole["fp_rate"] + 0.01,
          f"drift: and flags about as few clean spikes, within one point ({chunk['fp_rate']:.3f} vs {whole['fp_rate']:.3f})")

    flat_w = scan(K, 0.0, 0);  show("no drift, whole session", flat_w)
    flat_c = scan(K, 0.0, 10); show("no drift, 10 s chunks", flat_c)
    check(flat_c["recall"] >= 0.95, f"no drift: the chunked scan still finds them ({flat_c['recall']:.2f})")
    check(flat_c["fp_rate"] <= flat_w["fp_rate"] + 0.03,
          f"no drift: the cost of chunking stays under 3 % of the clean spikes ({flat_c['fp_rate']:.3f} vs {flat_w['fp_rate']:.3f})")

    # 2 s chunks hold ~30 of the cluster's spikes, under the 40 a tail needs: they borrow neighbours.
    small = scan(K, 0.5, 2);   show("drift, 2 s chunks (pooled)", small)
    line = " ".join(small["lines"])
    check(small["chunks"] == 40 and "pooled with neighbours" in line and " 0 of them pooled" not in line,
          "chunks below Min spikes borrow their neighbours, and the report says so", line)
    check(small["recall"] > whole["recall"] + 0.25,
          f"pooled chunks still follow the drift ({small['recall']:.2f} vs {whole['recall']:.2f})")

    # A seed: 30 % of unit 2's clean spikes carved into cluster 4; template and cut from it.
    seed_w = scan(K, 0.5, 0, seedfrac=0.3, seed=4, clusters=[2, 4], seedThreshold=True);  show("drift, seed, whole session", seed_w)
    seed_c = scan(K, 0.5, 10, seedfrac=0.3, seed=4, clusters=[2, 4], seedThreshold=True); show("drift, seed, 10 s chunks", seed_c)
    check(seed_c["recall"] > seed_w["recall"] + 0.25 and seed_c["fp_rate"] <= seed_w["fp_rate"],
          f"a seed is read per chunk too: more found, fewer clean flagged "
          f"({seed_c['recall']:.2f}/{seed_c['fp_rate']:.3f} vs {seed_w['recall']:.2f}/{seed_w['fp_rate']:.3f})")
    check(any("template window(s) over 8 time chunks" in l for l in seed_c["lines"]),
          "the report names the seed's per-chunk templates", " ".join(seed_c["lines"]))

    print(f"test_residual_strip_chunks: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
