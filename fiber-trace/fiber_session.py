#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════
#  fiber_session.py  —  full-session fiber clusterer (validated-Python reference)
#
#  Per chunk (all on the validated fiber_lib/fiber_tracer primitives):
#    off-spike .fil whitener (spkD space) -> realign -> mask -> whiten
#    -> COARSE in-band mean-shift ridge seeding -> dedup -> substantial centers
#       -> reassign -> run_from_seeds   (fat drift-stable fibers, for linking)
#    -> FINE refinement WITHIN the chunk: BIC-GMM (the Python stand-in for KK's
#       CEM split) on each coarse fiber -> sub-units.  Units inside a coarse fiber
#       are blobs separated in directions ORTHOGONAL to the ridge, which the
#       ridge-tracking fiber algorithm cannot split robustly (it leaves them whole
#       or shatters them: 14 vs GMM's 42 clean units on g5); CEM/GMM adapts per
#       fiber.  --fine-method fiber|none available but underperforms.
#  Refinement stays per-chunk so each fiber keeps its OWN geometry at each point
#  in time -> drift is visible across chunks.
#
#  Cross-chunk: OVERLAP-ANCHOR linking on the fine fibers.  A spike in chunk c's
#  overlap is the SAME physical spike in chunk c+1's overlap, so two fibers that
#  claim the same overlap spikes are the same unit (mutual-majority, drift-free).
#
#  Outputs:
#    <base>.clu.<elec>                          int32 nClusters header + ids (0=noise)
#    <base>.fibers.<method>.<elec>              npz: per (chunk,fiber) geometry,
#         keys: gid chunk tmin coarse nspk radius refrac depth (M,);
#               template (M,nsamp,nch); grid (M,n_grid); dir (M,n_grid,p);
#               + meta (elec, channels, sr, mask, n_grid, p, method, chunk/overlap min)
#         "fiber geometry over time" = rows sharing a gid, ordered by chunk.
#
#  Usage:
#    python3 fiber_session.py <FileBase> <ElecNo> \
#        --channels 32,33,34,35,36,37,38,39 --ntotal 96 --nsamp 32 --nchan 8 \
#        --sr 32552 --chunk-min 12 --overlap-min 4 \
#        --min-group 200 --fine-kappa 40 --fine-dedup-deg 5 --fine-min-group 40 \
#        --method stderiv [--no-fine] [--no-link]
# ════════════════════════════════════════════════════════════════════════════
import argparse, os, time
import numpy as np
from collections import defaultdict, Counter
import fiber_lib as fl
import fiber_tracer as ft
from sklearn.mixture import GaussianMixture

P_DIM = len(fl.MASK_FULL) * 8     # default masked feature dim (recomputed per nchan below)


def cluster_chunk(waves, W, nmean, min_group=100, kappa=20.0, dr_frac=0.15,
                  n_seeds=800, n_support=20000, dedup_deg=8.0, dedup_radf=0.12,
                  mask=fl.MASK_FULL):
    """waves (n,nsamp,nch) spkD + chunk whitener -> per-spike label (0-based, -1=none)."""
    N = len(waves)
    if N < 2 * min_group:
        return np.full(N, -1, int)
    X = (fl.realign(waves)[:, mask, :].reshape(N, -1) - nmean) @ W
    r = np.linalg.norm(X, axis=1); d = X / (r[:, None] + 1e-12)
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
    order = np.argsort(-rs); cd = []; cr = []; cth = np.cos(np.deg2rad(dedup_deg))
    for i in order:
        if all(not (ds[i] @ c > cth and abs(rs[i] - q) / q < dedup_radf) for c, q in zip(cd, cr)):
            cd.append(ds[i]); cr.append(rs[i])
    cd = np.array(cd); M = len(cd)
    lab = np.argmax(d @ cd.T, 1); sizes = np.bincount(lab, minlength=M)
    keep = np.flatnonzero(sizes >= min_group)
    if len(keep) == 0:
        return np.full(N, -1, int)
    lab2 = np.argmax(d @ cd[keep].T, 1)
    groups = {int(k): np.flatnonzero(lab2 == k) for k in range(len(keep))}
    out = ft.run_from_seeds(waves, groups, W, nmean, mask=mask)
    keys = {k: i for i, k in enumerate(out['keys'])}
    return np.array([keys.get(h, -1) if h is not None else -1 for h in out['hard']], int)


def fiber_geom(wsub, res_sub, W, nmean, mask, sr, n_grid=40):
    """Geometry of one fiber: realigned mean template, energy-resampled trajectory,
    mean radius, refractory %, depth (energy-weighted channel centroid)."""
    p = len(mask) * wsub.shape[2]
    w_al = fl.realign(wsub); template = w_al.mean(0)
    Xg = (w_al[:, mask, :].reshape(len(w_al), -1) - nmean) @ W
    grid, D = ft.trajectory(Xg)
    radii = np.linspace(grid[0], grid[-1], n_grid)
    dirs = np.array([ft.predict((grid, D), float(rr)) for rr in radii]) if grid[-1] > grid[0] \
        else np.repeat(D[:1], n_grid, 0)
    ptp = np.maximum(template.max(0) - template.min(0), 0.0)
    depth = float((ptp * np.arange(template.shape[1])).sum() / (ptp.sum() + 1e-9))
    t = np.sort(res_sub)
    refr = float((np.diff(t) / sr * 1000 < 2).mean() * 100) if len(t) > 10 else float('nan')
    wav = np.cumsum(template, axis=0); dom = int(np.argmax(wav.max(0) - wav.min(0))); sdom = wav[:, dom]
    tr = int(np.argmin(sdom)); pk = (tr + int(np.argmax(sdom[tr:]))) if tr < len(sdom) - 1 else tr
    width_ms = float((pk - tr) / sr * 1000.0)
    return dict(n=int(len(wsub)), radius=float(np.linalg.norm(Xg, axis=1).mean()),
                refrac=refr, depth=depth, width_ms=width_ms,
                template=template.astype(np.float32),
                grid=radii.astype(np.float32),
                dir=dirs.astype(np.float32).reshape(n_grid, p))


def gmm_split(wf, pca_k=6, max_sub=8, mask=fl.MASK_FULL, reg=1e-3):
    """BIC-selected Gaussian mixture on PCA of a coarse fiber's realigned waveforms
    (the Python stand-in for KK's CEM split).  Returns sub-labels 0..k-1."""
    N = len(wf)
    if N < 60: return np.zeros(N, int)
    w = fl.realign(wf)[:, mask, :].reshape(N, -1); w = w - w.mean(0)
    U, S, Vt = np.linalg.svd(w, full_matrices=False); F = U[:, :pca_k] * S[:pca_k]
    best = None
    for k in range(1, max_sub + 1):
        if k * 3 > N: break
        g = GaussianMixture(k, covariance_type='full', reg_covar=reg, random_state=0, n_init=2).fit(F)
        b = g.bic(F)
        if best is None or b < best[0]: best = (b, k, g)
    return best[2].predict(F)


def cluster_chunk_fine(waves, res_abs, W, nmean, coarse_mg, mask, sr, method="gmm",
                       fine_kappa=40.0, fine_dedup=5.0, fine_mg=40, pca_k=6, max_sub=8,
                       n_grid=40, incl_k=3.0):
    """Coarse fibers (for linking), each refined WITHIN the chunk into sub-units so
    every fiber keeps its own geometry at this point in time.  method: 'gmm' (BIC
    mixture, robust; the validated choice) | 'fiber' (re-run mean-shift; under-splits
    or shatters) | 'none' (coarse only).  Returns (fine_label per spike, geom list)."""
    coarse = cluster_chunk(waves, W, nmean, min_group=coarse_mg)
    fine = np.full(len(waves), -1, int); geoms = []; nid = 0
    for cf in np.unique(coarse[coarse >= 0]):
        cidx = np.flatnonzero(coarse == cf)
        if method == "none":
            subs = [cidx]
        elif method == "fiber":
            sub = cluster_chunk(waves[cidx], W, nmean, min_group=fine_mg,
                                kappa=fine_kappa, dedup_deg=fine_dedup)
            subs = ([cidx] if (sub < 0).all()
                    else [cidx[sub == s] for s in np.unique(sub[sub >= 0])])
        else:  # gmm
            sub = gmm_split(waves[cidx], pca_k=pca_k, max_sub=max_sub, mask=mask)
            subs = [cidx[sub == s] for s in np.unique(sub)]
        for sidx in subs:
            if len(sidx) < fine_mg: continue
            rad = float('nan'); rej = 0
            if incl_k > 0 and len(sidx) >= 20:
                # inclusion radius = median + k*MAD of THIS fiber's residuals to its own
                # trajectory; the per-fiber MAD is the neuron-type dependence (tight cells
                # small radius, bursty/spread cells large).  Spikes beyond -> noise.
                w_al = fl.realign(waves[sidx])
                Xg = (w_al[:, mask, :].reshape(len(sidx), -1) - nmean) @ W
                grid, D = ft.trajectory(Xg); rr = np.linalg.norm(Xg, axis=1)
                resid = np.linalg.norm(Xg - np.array([rr[i] * ft.predict((grid, D), float(rr[i]))
                                                      for i in range(len(sidx))]), axis=1)
                med = float(np.median(resid)); mad = 1.4826 * float(np.median(np.abs(resid - med)))
                rad = med + incl_k * mad; keep = resid <= rad; rej = int((~keep).sum())
                sidx = sidx[keep]
                if len(sidx) < fine_mg: continue
            fine[sidx] = nid
            g = fiber_geom(waves[sidx], res_abs[sidx], W, nmean, mask, sr, n_grid)
            g['coarse'] = int(cf); g['radius_incl'] = rad; g['n_rejected'] = rej
            geoms.append(g); nid += 1
    return fine, geoms


def geoms_from_labels(waves, res_abs, lab, W, nmean, mask, sr, n_grid=40):
    geoms = []
    for f in np.unique(lab[lab >= 0]):
        idx = np.flatnonzero(lab == f)
        g = fiber_geom(waves[idx], res_abs[idx], W, nmean, mask, sr, n_grid)
        g['coarse'] = int(f); g['radius_incl'] = float('nan'); g['n_rejected'] = 0
        geoms.append(g)
    return geoms


def link_chunks(ext_idx, ext_lab, min_anchor=8, frac=0.5):
    parent = {}
    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x: parent[x] = parent[parent[x]]; x = parent[x]
        return x
    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb: parent[rb] = ra
    nC = len(ext_idx)
    for c in range(nC):
        for l in set(int(x) for x in ext_lab[c] if x >= 0): find((c, l))
    for c in range(nC - 1):
        A = {int(g): int(l) for g, l in zip(ext_idx[c],     ext_lab[c])     if l >= 0}
        B = {int(g): int(l) for g, l in zip(ext_idx[c + 1], ext_lab[c + 1]) if l >= 0}
        shared = set(A) & set(B)
        if not shared: continue
        ab = defaultdict(Counter); ba = defaultdict(Counter)
        for s in shared: ab[A[s]][B[s]] += 1; ba[B[s]][A[s]] += 1
        for f, row in ab.items():
            g, cnt = row.most_common(1)[0]
            if cnt < min_anchor or cnt < frac * sum(row.values()): continue
            f2, cnt2 = ba[g].most_common(1)[0]
            if f2 != f or cnt2 < frac * sum(ba[g].values()): continue
            union((c, f), (c + 1, g))
    roots = {}; gid = {}
    for c in range(nC):
        for l in set(int(x) for x in ext_lab[c] if x >= 0):
            r = find((c, l)); roots.setdefault(r, len(roots)); gid[(c, l)] = roots[r]
    return gid, len(roots)


def read_res(base, elec):
    return np.fromfile(f"{base}.res.{elec}", dtype='<i8').astype(np.int64)

def open_spkD(base, elec, nsamp, nch):
    for ext in (f"{base}.spkD.{elec}", f"{base}.spk.{elec}"):
        if os.path.exists(ext):
            mm = np.memmap(ext, dtype='<i2', mode='r'); n = mm.size // (nsamp * nch)
            return mm[:n * nsamp * nch].reshape(n, nsamp, nch), ext
    raise FileNotFoundError(f"no .spkD/.spk for {base} elec {elec}")

def fil_chunk_whitener(filmm, gch, s0, s1, spike_abs, nsamp, mask):
    s0 = max(0, s0); s1 = min(filmm.shape[0], s1)
    span = np.asarray(filmm[s0:s1, :][:, gch], dtype=float)
    rel = (spike_abs - s0).astype(int); rel = rel[(rel >= 0) & (rel < span.shape[0])]
    return fl.chunk_whitener(span, rel, mask=mask)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base"); ap.add_argument("elec", type=int)
    ap.add_argument("--channels", required=True); ap.add_argument("--ntotal", type=int, required=True)
    ap.add_argument("--nsamp", type=int, default=32); ap.add_argument("--nchan", type=int, default=8)
    ap.add_argument("--sr", type=float, default=32552.0)
    ap.add_argument("--chunk-min", type=float, default=12.0); ap.add_argument("--overlap-min", type=float, default=4.0)
    ap.add_argument("--min-group", type=int, default=200, help="COARSE min spikes/fiber (for linking)")
    ap.add_argument("--fine-method", choices=["gmm","fiber","none"], default="gmm")
    ap.add_argument("--pca-k", type=int, default=6); ap.add_argument("--max-sub", type=int, default=8)
    ap.add_argument("--inclusion-k", type=float, default=3.0, help="per-fiber radius = median+k*MAD of residuals; 0 disables")
    ap.add_argument("--fine-kappa", type=float, default=40.0)
    ap.add_argument("--fine-dedup-deg", type=float, default=5.0)
    ap.add_argument("--fine-min-group", type=int, default=40)
    ap.add_argument("--no-fine", action="store_true", help="coarse fibers only, no within-chunk refinement")
    ap.add_argument("--min-anchor", type=int, default=8)
    ap.add_argument("--no-link", action="store_true")
    ap.add_argument("--n-grid", type=int, default=40)
    ap.add_argument("--method", default="stderiv", help="extraction method tag in the .fibers filename")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    gch = np.array([int(x) for x in a.channels.split(",")], int)
    assert len(gch) == a.nchan, f"--channels has {len(gch)} entries, --nchan={a.nchan}"
    mask = fl.MASK_FULL; p = len(mask) * a.nchan

    t0 = time.time()
    res = read_res(a.base, a.elec); nspk = len(res)
    spk, spkpath = open_spkD(a.base, a.elec, a.nsamp, a.nchan)
    assert spk.shape[0] == nspk, f".res {nspk} vs {spkpath} {spk.shape[0]}"
    filmm = np.memmap(f"{a.base}.fil", dtype='<i2', mode='r').reshape(-1, a.ntotal)
    print(f"loaded {nspk} spikes ({spkpath}); .fil {filmm.shape[0]} samples x {a.ntotal} ch")

    chunk_s = a.chunk_min * 60.0 * a.sr; ov_s = a.overlap_min * 60.0 * a.sr
    t_min, t_max = int(res.min()), int(res.max())
    nchunks = int(np.ceil((t_max - t_min) / chunk_s))
    ext_idx = [np.array([], int)] * nchunks; ext_lab = [np.array([], int)] * nchunks
    chunk_geoms = [[] for _ in range(nchunks)]; chunk_tmin = [0.0] * nchunks
    for c in range(nchunks):
        lo_s = t_min + c * chunk_s; hi_s = t_min + (c + 1) * chunk_s
        chunk_tmin[c] = (lo_s - t_min) / a.sr / 60.0
        ext = np.flatnonzero((res >= lo_s - ov_s) & (res < hi_s + ov_s))
        ncore = int(((res[ext] >= lo_s) & (res[ext] < hi_s)).sum())
        if len(ext) < 2 * a.min_group:
            print(f"[fiber_session] chunk {c+1}/{nchunks}: {ncore} core ({len(ext)} ext) -> 0 fibers (small)"); continue
        waves = np.asarray(spk[ext], dtype=float); res_e = res[ext]
        s0 = int(res_e.min()) - a.nsamp; s1 = int(res_e.max()) + a.nsamp + 1
        W, nmean, _ = fil_chunk_whitener(filmm, gch, s0, s1, res_e, a.nsamp, mask)
        meth = "none" if a.no_fine else a.fine_method
        lab, geoms = cluster_chunk_fine(waves, res_e, W, nmean, a.min_group, mask, a.sr,
                                        method=meth, fine_kappa=a.fine_kappa, fine_dedup=a.fine_dedup_deg,
                                        fine_mg=a.fine_min_group, pca_k=a.pca_k, max_sub=a.max_sub, n_grid=a.n_grid,
                                        incl_k=a.inclusion_k)
        ext_idx[c] = ext; ext_lab[c] = lab; chunk_geoms[c] = geoms
        nfib = len(geoms)
        print(f"[fiber_session] chunk {c+1}/{nchunks}: {ncore} core ({len(ext)} ext) -> {nfib} fibers")

    if a.no_link:
        gid = {}; n = 0
        for c in range(nchunks):
            for l in sorted(set(int(x) for x in ext_lab[c] if x >= 0)): gid[(c, l)] = n; n += 1
        nglob = n; mode = "chunk-disjoint"
    else:
        gid, nglob = link_chunks(ext_idx, ext_lab, min_anchor=a.min_anchor)
        mode = f"overlap-anchor linked (min_anchor={a.min_anchor})"
    print(f"[fiber_session] {nglob} global fibers across {nchunks} chunks  ({mode})")

    # ── .clu : core spikes -> global id (+1; 0=noise) ──
    labels = np.full(nspk, -1, int)
    for c in range(nchunks):
        if len(ext_idx[c]) == 0: continue
        lo_s = t_min + c * chunk_s; hi_s = t_min + (c + 1) * chunk_s
        emap = {int(g): int(l) for g, l in zip(ext_idx[c], ext_lab[c])}
        for g in np.flatnonzero((res >= lo_s) & (res < hi_s)):
            l = emap.get(int(g), -1)
            if l >= 0: labels[g] = gid[(c, l)]
    clu = np.where(labels >= 0, labels + 1, 0).astype(np.int32)
    clu_out = a.out or f"{a.base}.clu.{a.elec}"
    with open(clu_out, "wb") as f:
        np.array([int(clu.max()) + 1], np.int32).tofile(f); clu.tofile(f)

    # ── .fibers.<method>.<elec> : per (chunk,fiber) geometry, tagged with gid ──
    rows = []
    for c in range(nchunks):
        for l, g in enumerate(chunk_geoms[c]):
            g2 = dict(g); g2['gid'] = gid.get((c, l), -1); g2['chunk'] = c; g2['tmin'] = chunk_tmin[c]
            rows.append(g2)
    M = len(rows)
    def col(k, dt): return np.array([r[k] for r in rows], dt) if M else np.zeros(0, dt)
    fib_out = f"{a.base}.fibers.{a.method}.{a.elec}"
    arrs = dict(
        gid=col('gid', int), chunk=col('chunk', int), tmin=col('tmin', np.float32),
        coarse=col('coarse', int), nspk=col('n', int), radius=col('radius', np.float32),
        refrac=col('refrac', np.float32), depth=col('depth', np.float32),
        width_ms=col('width_ms', np.float32), radius_incl=col('radius_incl', np.float32),
        n_rejected=col('n_rejected', int),
        template=np.stack([r['template'] for r in rows]) if M else np.zeros((0, a.nsamp, a.nchan), np.float32),
        grid=np.stack([r['grid'] for r in rows]) if M else np.zeros((0, a.n_grid), np.float32),
        dir=np.stack([r['dir'] for r in rows]) if M else np.zeros((0, a.n_grid, p), np.float32),
        meta_elec=a.elec, meta_channels=gch, meta_sr=a.sr, meta_mask=np.asarray(mask),
        meta_n_grid=a.n_grid, meta_p=p, meta_nsamp=a.nsamp, meta_nchan=a.nchan,
        meta_method=a.method, meta_chunk_min=a.chunk_min, meta_overlap_min=a.overlap_min)
    with open(fib_out, "wb") as f:
        np.savez_compressed(f, **arrs)
    print(f"wrote {clu_out}")
    print(f"wrote {fib_out}  ({M} fiber-instances, {nglob} global; geometry over time = rows sharing gid)")
    print(f"({time.time()-t0:.0f}s)")


if __name__ == "__main__":
    main()
