#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════
#  fiber_session.py  —  full-session fiber clusterer (validated-Python reference)
#
#  Mirrors KiloKlustaKwik's RunFiberStandalone, but built on the validated
#  fiber_lib / fiber_tracer primitives.  Reads .res / .spkD / .fil straight off
#  disk, chunks by time, and for each chunk runs the SAME per-chunk pipeline
#  that produced ~125 clean fibers on the g5 chunk:
#      off-spike .fil whitener (spkD space)
#        -> realign -> mask -> whiten
#        -> in-band mean-shift ridge seeding -> dedup
#        -> keep substantial centers (>= --min-group) -> reassign all spikes
#        -> run_from_seeds (per-fiber realign + whiteness assignment)
#  NB: no trajectory-coherence merge (the validated Python never merged; it
#  fires 0x on good features anyway).  Per-chunk counts are printed in the same
#  format as the C++ log so the two can be compared chunk-for-chunk.
#
#  Cross-chunk linking is intentionally NOT done here (chunk-disjoint ids): this
#  is the per-chunk reference.  Add linking once per-chunk parity is confirmed.
#
#  Usage:
#    python3 fiber_session.py <FileBase> <ElecNo> \
#        --channels 32,33,34,35,36,37,38,39 --ntotal 96 \
#        --nsamp 32 --nchan 8 --sr 32552 \
#        --chunk-min 5 --overlap-min 2 --min-group 100
# ════════════════════════════════════════════════════════════════════════════
import argparse, os, sys, time
import numpy as np
import fiber_lib as fl
import fiber_tracer as ft


# ── per-chunk fiber clustering (the reference core; matches the C++ intent) ──
def cluster_chunk(waves, W, nmean, min_group=100, kappa=20.0, dr_frac=0.15,
                  n_seeds=800, n_support=20000, dedup_deg=8.0, dedup_radf=0.12,
                  mask=fl.MASK_FULL):
    """waves: (n, nsamp, nch) spkD; W,nmean: chunk whitener.  Returns per-spike
    label array (int, 0-based) over `waves`; -1 = unassigned."""
    N = len(waves)
    if N < 2 * min_group:
        return np.full(N, -1, int)
    X = (fl.realign(waves)[:, mask, :].reshape(N, -1) - nmean) @ W
    r = np.linalg.norm(X, axis=1); d = X / (r[:, None] + 1e-12)
    # deterministic strided support + seeds (RNG-free, matches the C++)
    nsup = min(N, n_support); supi = np.arange(nsup) * N // nsup
    dsup, rsup = d[supi], r[supi]
    S = min(N, n_seeds); sdi = np.arange(S) * N // S
    ds, rs = d[sdi].copy(), r[sdi].copy()
    rsort = np.sort(r); dr = dr_frac * (rsort[int(0.99 * (N - 1))] - rsort[int(0.01 * (N - 1))])
    for _ in range(15):
        cos = ds @ dsup.T
        w = np.where(np.abs(rsup[None, :] - rs[:, None]) < dr, np.exp(kappa * (cos - 1)), 0.0)
        sw = w.sum(1); sw[sw < 1e-9] = 1e-9
        ds = w @ dsup; ds /= np.linalg.norm(ds, axis=1, keepdims=True) + 1e-12
        rs = (w * rsup[None, :]).sum(1) / sw
    # dedup converged seeds into ridge centers
    order = np.argsort(-rs); cd = []; cr = []; cth = np.cos(np.deg2rad(dedup_deg))
    for i in order:
        if all(not (ds[i] @ c > cth and abs(rs[i] - q) / q < dedup_radf) for c, q in zip(cd, cr)):
            cd.append(ds[i]); cr.append(rs[i])
    cd = np.array(cd); M = len(cd)
    lab = np.argmax(d @ cd.T, 1); sizes = np.bincount(lab, minlength=M)
    keep = np.flatnonzero(sizes >= min_group)
    if len(keep) == 0:
        return np.full(N, -1, int)
    sub = cd[keep]
    lab2 = np.argmax(d @ sub.T, 1)                 # reassign ALL spikes to nearest substantial center
    groups = {int(k): np.flatnonzero(lab2 == k) for k in range(len(keep))}
    out = ft.run_from_seeds(waves, groups, W, nmean, mask=mask)
    hard = out['hard']
    # run_from_seeds leaves None for spikes not in any >=50 group; map to label ints
    keys = {k: i for i, k in enumerate(out['keys'])}
    return np.array([keys.get(h, -1) if h is not None else -1 for h in hard], int)


# ── neurosuite binary readers ───────────────────────────────────────────────
def read_res(base, elec):
    p = f"{base}.res.{elec}"
    return np.fromfile(p, dtype='<i8').astype(np.int64)

def open_spkD(base, elec, nsamp, nch):
    for ext in (f"{base}.spkD.{elec}", f"{base}.spk.{elec}"):
        if os.path.exists(ext):
            mm = np.memmap(ext, dtype='<i2', mode='r')
            n = mm.size // (nsamp * nch)
            return mm[:n * nsamp * nch].reshape(n, nsamp, nch), ext
    raise FileNotFoundError(f"no .spkD/.spk for {base} elec {elec}")

def fil_chunk_whitener(filmm, ntot, gch, s0, s1, spike_abs, nsamp, mask):
    s0 = max(0, s0); s1 = min(filmm.shape[0], s1)
    span = np.asarray(filmm[s0:s1, :][:, gch], dtype=float)      # (T, nch) group channels
    rel = (spike_abs - s0).astype(int)
    rel = rel[(rel >= 0) & (rel < span.shape[0])]
    return fl.chunk_whitener(span, rel, mask=mask)               # (W, nmean, nbase)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base"); ap.add_argument("elec", type=int)
    ap.add_argument("--channels", required=True, help="comma-separated group channels into the .fil")
    ap.add_argument("--ntotal", type=int, required=True)
    ap.add_argument("--nsamp", type=int, default=32); ap.add_argument("--nchan", type=int, default=8)
    ap.add_argument("--sr", type=float, default=32552.0)
    ap.add_argument("--chunk-min", type=float, default=5.0); ap.add_argument("--overlap-min", type=float, default=2.0)
    ap.add_argument("--min-group", type=int, default=100)
    ap.add_argument("--kappa", type=float, default=20.0); ap.add_argument("--dr-frac", type=float, default=0.15)
    ap.add_argument("--seeds", type=int, default=800)
    ap.add_argument("--out", default=None, help="output .clu path (default <base>.clu.<elec>)")
    a = ap.parse_args()
    gch = np.array([int(x) for x in a.channels.split(",")], int)
    assert len(gch) == a.nchan, f"--channels has {len(gch)} entries, --nchan={a.nchan}"

    t0 = time.time()
    res = read_res(a.base, a.elec); nspk = len(res)
    spk, spkpath = open_spkD(a.base, a.elec, a.nsamp, a.nchan)
    assert spk.shape[0] == nspk, f".res has {nspk} spikes, {spkpath} has {spk.shape[0]}"
    filmm = np.memmap(f"{a.base}.fil", dtype='<i2', mode='r')
    filmm = filmm.reshape(-1, a.ntotal)
    print(f"loaded {nspk} spikes  ({spkpath}),  .fil {filmm.shape[0]} samples x {a.ntotal} ch")

    chunk_s = a.chunk_min * 60.0 * a.sr; ov_s = a.overlap_min * 60.0 * a.sr
    t_min, t_max = res.min(), res.max()
    nchunks = int(np.ceil((t_max - t_min) / chunk_s))
    labels = np.full(nspk, -1, int); gbase = 0
    for c in range(nchunks):
        lo_s = t_min + c * chunk_s; hi_s = t_min + (c + 1) * chunk_s
        core = np.flatnonzero((res >= lo_s) & (res < hi_s))
        ext = np.flatnonzero((res >= lo_s - ov_s) & (res < hi_s + ov_s))
        if len(core) < 2 * a.min_group:
            print(f"[fiber_session] chunk {c+1}/{nchunks}: {len(core)} spikes -> 0 fibers (small)"); continue
        waves = np.asarray(spk[ext], dtype=float)
        s0 = int(res[ext].min()) - a.nsamp; s1 = int(res[ext].max()) + a.nsamp + 1
        W, nmean, nb = fil_chunk_whitener(filmm, a.ntotal, gch, s0, s1, res[ext], a.nsamp, fl.MASK_FULL)
        lab = cluster_chunk(waves, W, nmean, min_group=a.min_group, kappa=a.kappa,
                            dr_frac=a.dr_frac, n_seeds=a.seeds)
        nfib = int(lab.max()) + 1 if (lab >= 0).any() else 0
        # write only CORE spikes' labels (chunk-disjoint global ids)
        ext_set = {int(g): i for i, g in enumerate(ext)}
        for g in core:
            li = lab[ext_set[int(g)]]
            labels[g] = (gbase + li) if li >= 0 else -1
        gbase += nfib
        print(f"[fiber_session] chunk {c+1}/{nchunks}: {len(core)} spikes -> {nfib} fibers")
    nglob = gbase
    print(f"[fiber_session] done: {nglob} fibers across {nchunks} chunk(s)  (chunk-disjoint, no cross-chunk link)")

    # write .clu (id 0 = noise, fibers 1..); SaveOutput-compatible header
    clu = np.where(labels >= 0, labels + 1, 0).astype(np.int32)
    out = a.out or f"{a.base}.clu.{a.elec}"
    with open(out, "wb") as f:
        np.array([int(clu.max()) + 1], np.int32).tofile(f); clu.tofile(f)
    print(f"wrote {out}  ({time.time()-t0:.0f}s)")


if __name__ == "__main__":
    main()
