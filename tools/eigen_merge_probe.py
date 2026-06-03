#!/usr/bin/env python3
"""eigen_merge_probe — prototype of a soft-warp eigen-residual merge scorer for
neurosuite-3, plus a harness that scores it against curation-log GROUP decisions.

Idea (see design notes):  rigid normalised xcorr aligns two cluster-mean
templates with one global lag and scores a correlation.  It misses true merges
whose mean difference lives in a direction a single lag can't absorb — per-
channel latency, drift that slides the spatial footprint across channels, burst
shape change.  Those directions are exactly the *leading within-cluster
eigenmodes* of a real cluster (temporal jitter -> dV/dt mode; drift -> spatial-
gradient mode; amplitude -> V mode).  So:

  1. rigid-align B's mean to A's (integer + parabolic sub-sample lag), as today;
  2. build a SHARED nuisance subspace from the pooled within-cluster scatter of
     A and B (top-k eigenvectors of that covariance);
  3. let A's mean move toward B BOUNDED along those modes — at most c*sqrt(lambda_i)
     (a few sigma of the variation the cluster actually exhibits): you cannot
     warp in a direction the cluster never varies in (self-regularising);
  4. score the residual that movement could NOT explain, Mahalanobis-weighted
     with an eigenvalue SHRINKAGE FLOOR so a noisy near-zero mode can't make the
     metric blow up.  Low residual => mergeable, and — crucially — the residual
     is measured in the low-variance discriminative directions, so merged
     clusters stay low-variance.

Lower eigen-residual = more mergeable (opposite polarity to xcorr, where higher
= more similar).  Both scorers are swept over thresholds in the harness and
scored against the human GROUP ground truth.

Disk formats (neurosuite-3): .spk/.spkD int16, sample-major (index =
sample*nCh + channel), no header; .clu int32 header(nClusters)+ids.
"""
import argparse
import json
import os
import sys
import numpy as np


# ────────────────────────────── loaders ──────────────────────────────────
def read_clu(path):
    """Cluster id per spike (neurosuite-3 binary .clu: int32 nClusters header +
    int32 ids), header dropped."""
    a = np.fromfile(path, dtype="<i4")
    if a.size < 1:
        raise ValueError(f"{path}: empty .clu")
    return a[1:]                                   # drop nClusters header


def read_spk(path, n_ch, n_samp):
    """memmap .spk/.spkD as (nSpikes, nSamp, nCh) int16 (sample-major on disk)."""
    w = n_ch * n_samp
    raw = np.memmap(path, dtype="<i2", mode="r")
    n = raw.size // w
    if n * w != raw.size:
        raise ValueError(f"{path}: size {raw.size} not a multiple of nCh*nSamp={w}")
    return raw[: n * w].reshape(n, n_samp, n_ch)   # [spike, sample, channel]


def read_res(path):
    """Binary .res: little-endian int64 timestamps, one per spike, no header."""
    return np.fromfile(path, dtype="<i8")


def cluster_mean_times(clu, res):
    """Map cluster id -> mean spike time (sample units).  Empty dict if no res."""
    out = {}
    if res is None or res.size == 0:
        return out
    n = min(clu.size, res.size)
    c, t = clu[:n], res[:n].astype(np.float64)
    for cid in np.unique(c):
        if cid >= 2:
            out[int(cid)] = float(t[c == cid].mean())
    return out


def cluster_waveforms(spk, clu, cid, max_spikes, rng):
    """Return (m, D) float32 waveforms for cluster cid, D = nSamp*nCh, flattened
    sample-major (matches the on-disk order)."""
    idx = np.flatnonzero(clu == cid)
    if idx.size == 0:
        return np.empty((0, spk.shape[1] * spk.shape[2]), np.float32)
    if idx.size > max_spikes:
        idx = rng.choice(idx, size=max_spikes, replace=False)
    w = spk[idx].astype(np.float32)
    return w.reshape(w.shape[0], -1)


# ───────────────────────── rigid xcorr baseline ──────────────────────────
def _best_lag_xcorr(a2, b2, max_shift):
    """a2,b2: (nSamp, nCh).  Normalised cross-correlation over integer lags in
    [-max_shift, max_shift], all channels jointly; returns (peak_r, lag_int,
    sub-sample lag via 3-point parabola)."""
    n_samp = a2.shape[0]
    best_r, best_l, rm1, rp1 = -2.0, 0, None, None
    a_flatfull = a2
    for lag in range(-max_shift, max_shift + 1):
        if lag >= 0:
            aa = a_flatfull[lag:, :]; bb = b2[: n_samp - lag, :]
        else:
            aa = a_flatfull[: n_samp + lag, :]; bb = b2[-lag:, :]
        aa = aa.ravel(); bb = bb.ravel()
        if aa.size < 4:
            continue
        aa = aa - aa.mean(); bb = bb - bb.mean()
        denom = np.sqrt((aa @ aa) * (bb @ bb))
        r = (aa @ bb) / denom if denom > 0 else 0.0
        if r > best_r:
            best_r, best_l = r, lag
    return best_r, best_l


def rigid_xcorr_score(mA, mB, n_ch, n_samp, max_shift=6):
    """Baseline: peak normalised xcorr between two mean templates.  Range ~[-1,1];
    higher = more similar."""
    a2 = mA.reshape(n_samp, n_ch); b2 = mB.reshape(n_samp, n_ch)
    r, _ = _best_lag_xcorr(a2, b2, max_shift)
    return float(r)


def _shift_template(m2, lag):
    """Integer-lag shift of (nSamp,nCh) template along time, edge-replicate."""
    if lag == 0:
        return m2
    out = np.empty_like(m2)
    if lag > 0:
        out[lag:] = m2[:-lag]; out[:lag] = m2[0]
    else:
        out[:lag] = m2[-lag:]; out[lag:] = m2[-1]
    return out


# ───────────────────── eigen-residual (soft-warp) ────────────────────────
def eigen_residual_score(wA, wB, n_ch, n_samp, k=6, c=3.0, floor_frac=0.02,
                         max_shift=6, max_basis=64):
    """Soft-warp eigen-residual between clusters A and B.

    wA,wB: (mA_spikes, D) and (mB_spikes, D) float32, D = nSamp*nCh.
    Returns residual score >= 0 in Mahalanobis-sigma units; LOWER = mergeable.
    Returns +inf when either cluster is too small for a stable basis.
    """
    if wA.shape[0] < 10 or wB.shape[0] < 10:
        return float("inf")
    muA = wA.mean(0); muB = wB.mean(0)

    # 1) rigid sub-sample-free integer align of B's mean onto A's
    a2 = muA.reshape(n_samp, n_ch); b2 = muB.reshape(n_samp, n_ch)
    _, lag = _best_lag_xcorr(a2, b2, max_shift)
    muB = _shift_template(b2, lag).ravel()

    # 2) shared nuisance subspace from pooled within-cluster scatter
    XA = wA - wA.mean(0); XB = wB - wB.mean(0)
    X = np.vstack([XA, XB])                       # (mA+mB, D)
    # SVD of centred residuals: covariance eigvecs = Vt rows, eigvals = S^2/(N-1)
    # subsample rows if huge (keeps it cheap; basis is stable well below all spikes)
    N = X.shape[0]
    _, S, Vt = np.linalg.svd(X, full_matrices=False)
    lam = (S ** 2) / max(N - 1, 1)                # (r,)
    r = min(lam.size, max_basis)
    U = Vt[:r].T                                  # (D, r)
    lam = lam[:r]
    lam_floor = floor_frac * lam[0] if lam.size else 1.0
    lam_floor = max(lam_floor, 1e-6)

    # 3) decompose the (aligned) mean difference
    d = muB - muA
    proj = U.T @ d                                # (r,) coords in nuisance basis
    d_perp = d - U @ proj                         # component outside the data subspace

    # 4) bounded movement along the top-k nuisance modes; full penalty elsewhere
    allow = c * np.sqrt(np.maximum(lam, 0.0))
    resid = proj.copy()
    kk = min(k, r)
    resid[:kk] = np.sign(proj[:kk]) * np.maximum(0.0, np.abs(proj[:kk]) - allow[:kk])
    # modes k..r are discriminative (low variance) -> no movement allowed

    w = 1.0 / np.maximum(lam, lam_floor)
    score2 = float(resid * resid @ w) + float(d_perp @ d_perp) / lam_floor
    # normalise by effective DOF so the threshold is interpretable in sigma
    dof = max(r - kk, 1)
    return float(np.sqrt(score2 / dof))


# ─────────────────────── curation-log ground truth ───────────────────────
def parse_curation_groups(path):
    """Return a set of frozenset({a,b}) cluster pairs a human merged (GROUP /
    GROUP-like ACTION_DETAIL records with >=2 source clusters)."""
    pairs = set()
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            act = str(rec.get("action", "")).upper()
            if "GROUP" not in act:
                continue
            srcs = rec.get("source_clusters") or rec.get("sources")
            if not srcs and rec.get("source_cluster") is not None:
                srcs = [rec["source_cluster"]]
            if not srcs or len(srcs) < 2:
                continue
            srcs = [int(s) for s in srcs if int(s) >= 2]
            for i in range(len(srcs)):
                for j in range(i + 1, len(srcs)):
                    pairs.add(frozenset((srcs[i], srcs[j])))
    return pairs


def parse_curation_families(path):
    """Return a list of frozenset(cluster ids) a human merged together (the full
    source-cluster set of each GROUP-like ACTION_DETAIL with >=2 sources)."""
    fams = []
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                continue
            if "GROUP" not in str(rec.get("action", "")).upper():
                continue
            srcs = rec.get("source_clusters") or rec.get("sources") or []
            srcs = [int(s) for s in srcs if int(s) >= 2]
            if len(srcs) >= 2:
                fams.append(frozenset(srcs))
    return fams


# ───────────────────────────── harness ───────────────────────────────────
def evaluate(spk, clu, n_ch, n_samp, gt_pairs, k, c, floor_frac, max_shift,
             max_spikes, min_size, rng):
    cids = sorted(int(x) for x in np.unique(clu) if x >= 2)
    waves, means, sizes = {}, {}, {}
    for cid in cids:
        w = cluster_waveforms(spk, clu, cid, max_spikes, rng)
        if w.shape[0] < min_size:
            continue
        waves[cid] = w; means[cid] = w.mean(0); sizes[cid] = w.shape[0]
    live = sorted(waves)
    rows = []
    for i in range(len(live)):
        for j in range(i + 1, len(live)):
            a, b = live[i], live[j]
            xc = rigid_xcorr_score(means[a], means[b], n_ch, n_samp, max_shift)
            er = eigen_residual_score(waves[a], waves[b], n_ch, n_samp,
                                      k, c, floor_frac, max_shift)
            label = 1 if (gt_pairs and frozenset((a, b)) in gt_pairs) else 0
            rows.append((a, b, sizes[a], sizes[b], xc, er, label))
    return rows, waves


def pr_at_sweep(rows, score_idx, higher_is_merge):
    """Precision/recall sweep over a scorer column; returns (best_f1, ap-ish)."""
    labels = np.array([r[6] for r in rows])
    scores = np.array([r[score_idx] for r in rows], dtype=float)
    if labels.sum() == 0 or not np.isfinite(scores).any():
        return None
    finite = np.isfinite(scores)
    labels, scores = labels[finite], scores[finite]
    s = scores if higher_is_merge else -scores
    order = np.argsort(-s)
    labels = labels[order]
    tp = np.cumsum(labels); fp = np.cumsum(1 - labels)
    P = tp / np.maximum(tp + fp, 1)
    R = tp / max(labels.sum(), 1)
    f1 = 2 * P * R / np.maximum(P + R, 1e-9)
    ap = float(np.sum((R[1:] - R[:-1]) * P[1:])) if R.size > 1 else 0.0
    bi = int(np.argmax(f1))
    return dict(best_f1=float(f1[bi]), precision=float(P[bi]),
                recall=float(R[bi]), ap=ap, n_pos=int(labels.sum()),
                n_pairs=int(labels.size))


# ──────────────────── family stage: smooth warp curve ────────────────────
# A drifting neuron's sub-clusters don't scatter through the nuisance subspace
# — they trace a 1-D curve parameterised by time (drift) or rate (bursting),
# because the generator (electrode-relative position, firing state) moves
# continuously.  So the merge family is a PATH, not a clique:
#   * endpoints of a long drift can be far apart in direct pairwise residual
#     (footprints barely overlap) yet are the same neuron — connect them via
#     the chain of small-residual hops, never needing endpoint-endpoint to be
#     close (the transitivity fix);
#   * a smooth, time-MONOTONE warping coordinate confirms the family and vetoes
#     incoherent sets that a pairwise distance alone would fuse (the precision
#     lever).  This is the global generalisation of CrossChunkDriftSigma's
#     per-hop displacement prior, and should coincide with ndm_estimatedrift's
#     per-unit drift-vs-time curve.

def build_families(cids, pair_resid, edge_thresh):
    """Union-find connected components over edges with residual < edge_thresh."""
    parent = {c: c for c in cids}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x

    for (a, b), er in pair_resid.items():
        if a in parent and b in parent and np.isfinite(er) and er < edge_thresh:
            parent[find(a)] = find(b)
    comp = {}
    for c in cids:
        comp.setdefault(find(c), []).append(c)
    return [sorted(v) for v in comp.values()]


def _spearman_abs(x, y):
    """|rank correlation| in [0,1]; 1 = perfectly monotone."""
    if len(x) < 2:
        return 1.0
    rx = np.argsort(np.argsort(x)).astype(float)
    ry = np.argsort(np.argsort(y)).astype(float)
    rx -= rx.mean(); ry -= ry.mean()
    den = np.sqrt((rx @ rx) * (ry @ ry))
    return abs(float(rx @ ry) / den) if den > 0 else 0.0


def family_curve_metrics(members, waves, mean_time, n_ch, n_samp,
                         k_family=3, floor_frac=0.02):
    """For a candidate family (>=3 clusters), fit a smooth curve in the family's
    nuisance subspace parameterised by mean spike time, and return:
        n               members
        monotonicity    |rank-corr| of leading warp coord vs time   (1 = smooth drift)
        rms_offcurve    RMS distance of members to the fitted curve, in sigma
        endpoint_resid  span of the leading warp coord across the family, in sigma
                        (how far direct pairwise would have had to reach)
    """
    n = len(members)
    if n < 3 or not all(c in mean_time for c in members):
        return None
    means = {c: waves[c].mean(0) for c in members}
    # warp axes = principal directions of the cluster-MEAN spread (the drift
    # trajectory).  The within-cluster scatter only sets the sigma scale.
    M = np.array([means[c] for c in members])      # (n, D)
    gm = M.mean(0); Mc = M - gm
    _, _, VtM = np.linalg.svd(Mc, full_matrices=False)
    d = min(k_family, VtM.shape[0])
    W = VtM[:d].T                                  # (D, d) trajectory axes
    # within-cluster spread along each trajectory axis -> sigma normalisation
    Xs = [waves[c] - means[c] for c in members]
    X = np.vstack(Xs)
    within_var = (X @ W).var(axis=0)               # (d,)
    floor = floor_frac * float(within_var.max()) if within_var.size else 1.0
    within_var = np.maximum(within_var, max(floor, 1e-9))

    order = sorted(range(n), key=lambda i: mean_time[members[i]])
    t = np.array([mean_time[members[i]] for i in order], float)
    t = (t - t.min()) / (np.ptp(t) if np.ptp(t) > 0 else 1.0)
    P = np.array([(Mc[i] @ W) / np.sqrt(within_var) for i in order])  # (n,d) sigma units

    monot = _spearman_abs(P[:, 0], t)              # leading trajectory coord vs time
    deg = min(2, n - 1)
    resid2 = np.zeros(n)
    for j in range(d):
        coef = np.polyfit(t, P[:, j], deg)
        resid2 += (P[:, j] - np.polyval(coef, t)) ** 2
    rms_offcurve = float(np.sqrt(resid2.mean()))
    endpoint_resid = float(abs(P[-1, 0] - P[0, 0]))
    return dict(n=n, monotonicity=monot, rms_offcurve=rms_offcurve,
                endpoint_resid=endpoint_resid, members=[members[i] for i in order])


# ───────────────────────────── self-test ─────────────────────────────────
def _make_template(n_ch, n_samp, peak_ch, width, foot_sigma, amp=200.0):
    s = np.arange(n_samp)
    t0 = n_samp * 0.35
    shape = -amp * np.exp(-((s - t0) ** 2) / (2 * width ** 2)) \
        + 0.45 * amp * np.exp(-((s - t0 - 2.2 * width) ** 2) / (2 * (width * 1.4) ** 2))
    ch = np.arange(n_ch)
    foot = np.exp(-((ch - peak_ch) ** 2) / (2 * foot_sigma ** 2))
    return (shape[:, None] * foot[None, :]).astype(np.float32)   # (nSamp,nCh)


def _gen_cluster(n, n_ch, n_samp, peak_ch_mean, peak_ch_sd, width, foot_sigma,
                 noise, rng):
    out = np.empty((n, n_samp, n_ch), np.float32)
    for i in range(n):
        pc = peak_ch_mean + peak_ch_sd * rng.standard_normal()   # footprint drift
        T = _make_template(n_ch, n_samp, pc, width, foot_sigma)
        out[i] = T + noise * rng.standard_normal((n_samp, n_ch))
    return out


# ════════════════════════ within-cluster split test ══════════════════════
# eigen_residual_score asks "are these two clusters the same unit?".  The dual
# question — "is this ONE cluster secretly two?" — is what the n-gated negative
# dominant-sample kurtosis in the cluster-stats npz flags.  Same philosophy as
# the pair test: build the within-cluster scatter eigenbasis, then look for a
# direction whose spike projection is BIMODAL (two peaks with a real gap), not
# merely spread.  The split axis is (approximately) a scatter eigenvector, since
# a genuine second mode adds a separation^2 term to that direction's eigenvalue.
# Reporting the eigen-RANK of the split axis is the thesis discriminator: a
# low-variance rank => the units differ in fine/shape structure that global
# xcorr averages away; a top rank => a gross difference.

def _bimodality_1d(p):
    """Gap-aware bimodality of a 1-D sample via a 2- vs 1-component GMM.

    Returns Ashman D (separation/spread of the two fitted modes), minority
    weight, valley depth (1 - trough/peak density of the mixture between the
    modes; ~1 = clean gap = two units, ~0 = filled continuum = one spread mode),
    and ΔBIC (negative favours two modes).  A drift continuum is platykurtic
    like a true split but has a SHALLOW valley — that is what separates them.
    """
    from sklearn.mixture import GaussianMixture
    x = np.asarray(p, float).reshape(-1, 1)
    if len(x) < 40 or x.std() < 1e-9:
        return dict(ashman=0.0, minority=0.0, valley=0.0, dbic=float("nan"))
    xs = (x - x.mean()) / (x.std() + 1e-12)
    g1 = GaussianMixture(1, covariance_type="full", reg_covar=1e-4,
                         random_state=0).fit(xs)
    g2 = GaussianMixture(2, covariance_type="full", reg_covar=1e-4,
                         n_init=3, random_state=0).fit(xs)
    dbic = float(g2.bic(xs) - g1.bic(xs))
    mu = g2.means_.ravel(); sd = np.sqrt(g2.covariances_.ravel()); w = g2.weights_.ravel()
    o = np.argsort(mu); mu, sd, w = mu[o], sd[o], w[o]
    if mu[1] - mu[0] < 1e-6:
        return dict(ashman=0.0, minority=float(w.min()), valley=0.0, dbic=dbic)
    ashman = float(np.sqrt(2) * (mu[1] - mu[0]) / np.sqrt(sd[0] ** 2 + sd[1] ** 2))
    grid = np.linspace(mu[0], mu[1], 64).reshape(-1, 1)
    dens = np.exp(g2.score_samples(grid))
    peak = min(np.exp(g2.score_samples(mu.reshape(-1, 1))))
    valley = float(1.0 - dens.min() / (peak + 1e-12))
    return dict(ashman=ashman, minority=float(w.min()), valley=valley, dbic=dbic)


def within_cluster_split(wX, n_ch, n_samp, times=None, max_basis=64,
                         floor_frac=0.02, scan=None,
                         min_minority=0.03, min_ashman=2.0, min_valley=0.45):
    """Scan a cluster's within-scatter eigenmodes for a bimodal split axis.

    wX    : (m, D) float spikes of ONE cluster, D = nSamp*nCh (sample-major).
    times : optional (m,) spike times (.res units) — if given, reports how
            time-segregated the two modes are.  Time-segregated bimodality is
            DRIFT (one unit at two positions); time-interleaved bimodality is
            two co-active units.  This is the drift-vs-identity disambiguator.

    Returns a compact dict (JSON-friendly): the best bimodal axis, its eigen
    rank + variance fraction, the gap statistics, the time verdict, and a small
    standardised projection histogram.
    """
    m = wX.shape[0]
    if m < 40:
        return dict(n=m, bimodal=False, note="n<40")
    mu = wX.mean(0)
    R = wX - mu
    _, S, Vt = np.linalg.svd(R, full_matrices=False)
    lam = (S ** 2) / max(m - 1, 1)
    r = int(min(lam.size, max_basis))
    U = Vt[:r].T                                   # (D, r) scatter eigenvectors
    lam = lam[:r]
    lam_floor = max(floor_frac * (lam[0] if lam.size else 1.0), 1e-9)
    scan = r if scan is None else int(min(scan, r))

    best = None
    for j in range(scan):
        if lam[j] < lam_floor:                     # below noise floor: unstable
            continue
        p = R @ U[:, j]
        bm = _bimodality_1d(p)
        # gate: real gap + balanced enough + separated
        ok = (bm["ashman"] >= min_ashman and bm["minority"] >= min_minority
              and bm["valley"] >= min_valley)
        score = bm["ashman"] * bm["valley"] * (bm["minority"] >= min_minority)
        cand = dict(rank=j, var_frac=float(lam[j] / (lam[0] + 1e-12)),
                    ashman=round(bm["ashman"], 3), minority=round(bm["minority"], 4),
                    valley=round(bm["valley"], 3),
                    dbic=(round(bm["dbic"], 1) if bm["dbic"] == bm["dbic"] else None),
                    ok=bool(ok), _score=score, _proj=p)
        if best is None or score > best["_score"]:
            best = cand
    if best is None:
        return dict(n=m, bimodal=False, note="no stable axis above floor")

    p = best.pop("_proj"); best.pop("_score")
    # time-segregation verdict on the best axis (DRIFT vs two co-active units)
    time_verdict = None; time_auc = None
    if times is not None and len(times) == m:
        thr = np.median(p)
        lo, hi = times[p <= thr], times[p > thr]
        if len(lo) and len(hi):
            # rank-AUC of "is later" for the two modes; 0.5 = interleaved
            allt = np.concatenate([lo, hi])
            order = np.argsort(allt, kind="mergesort")
            ranks = np.empty_like(order, float); ranks[order] = np.arange(len(allt))
            rhi = ranks[len(lo):].sum()
            auc = (rhi - len(hi) * (len(hi) - 1) / 2) / (len(lo) * len(hi))
            time_auc = float(abs(auc - 0.5) * 2)   # 0 interleaved .. 1 fully segregated
            time_verdict = ("drift (time-segregated modes)" if time_auc > 0.6
                            else "co-active (time-interleaved) -> genuine split")
    ps = (p - p.mean()) / (p.std() + 1e-12)
    hist = np.histogram(ps, bins=np.linspace(-4, 4, 17))[0].tolist()
    # a time-segregated bimodality is DRIFT, not two co-active units -> veto the
    # split flag even when the gap gates pass.
    drift = (time_auc is not None and time_auc > 0.6)
    bimodal = bool(best["ok"]) and not drift
    return dict(n=m, r=r, bimodal=bimodal, gap_ok=bool(best["ok"]),
                split_axis=best,
                time_auc=(round(time_auc, 3) if time_auc is not None else None),
                time_verdict=time_verdict, hist=hist, hist_edges=[-4, 4, 16])


def npz_split_targets(npz_path, n_min=100, kurt_thr=-0.7):
    """Cluster ids from a cluster_waveforms npz flagged internally bimodal,
    gated to n>=n_min so the kurtosis statistic is meaningful (it is forced
    toward its -2 floor at small n)."""
    z = np.load(npz_path, allow_pickle=True)
    clu = z["clusters"]; nspk = z["nspikes"]; mk = z["min_kurt_dom"]
    sel = (nspk >= n_min) & (mk < kurt_thr)
    order = np.argsort(mk[sel])
    return [int(c) for c in np.asarray(clu)[sel][order]]


def selftest_split():
    rng = np.random.default_rng(7)
    n_ch, n_samp = 8, 32
    D = n_ch * n_samp
    print("\n── within-cluster split test (synthetic ground truth) ──")

    # (a) ONE unit drifting smoothly: footprint walks ch3->ch5 over the session.
    #     Bimodal-ish along the drift mode but TIME-SEGREGATED -> not a split.
    n = 1500
    t = np.sort(rng.random(n))                       # session time 0..1
    one = np.empty((n, n_samp, n_ch), np.float32)
    for i in range(n):
        pc = 3.0 + 2.0 * t[i]                        # monotonic drift with time
        one[i] = _make_template(n_ch, n_samp, pc, 2.2, 1.6) + 12 * rng.standard_normal((n_samp, n_ch))
    r = within_cluster_split(one.reshape(n, -1), n_ch, n_samp, times=t)
    print(f"(a) drifting single unit : bimodal={r['bimodal']} "
          f"axis_rank={r['split_axis']['rank']} valley={r['split_axis']['valley']} "
          f"time_auc={r['time_auc']} -> {r['time_verdict']}")

    # (b) TWO units, differ only in a LOW-variance SHAPE direction (narrower
    #     spike), interleaved in time.  The case global xcorr merges.
    a = _gen_cluster(800, n_ch, n_samp, 3.0, 0.8, width=2.2, foot_sigma=1.6, noise=12, rng=rng)
    b = _gen_cluster(700, n_ch, n_samp, 3.0, 0.8, width=1.25, foot_sigma=1.6, noise=12, rng=rng)
    mix = np.vstack([a, b]); tm = rng.random(len(mix))   # random times = interleaved
    r = within_cluster_split(mix.reshape(len(mix), -1), n_ch, n_samp, times=tm)
    sa = r["split_axis"]
    print(f"(b) two units (fine shape): bimodal={r['bimodal']} "
          f"axis_rank={sa['rank']} var_frac={sa['var_frac']:.3f} ashman={sa['ashman']} "
          f"valley={sa['valley']} minority={sa['minority']} time_auc={r['time_auc']}")
    print("    (expect: bimodal=True, low-ish var_frac = fine-structure axis, "
          "time_auc~0 = co-active)")

    # (c) clean single Gaussian-ish unit, no structure -> not bimodal
    c = _gen_cluster(1500, n_ch, n_samp, 3.0, 0.8, width=2.2, foot_sigma=1.6, noise=12, rng=rng)
    r = within_cluster_split(c.reshape(len(c), -1), n_ch, n_samp)
    print(f"(c) single clean unit    : bimodal={r['bimodal']} "
          f"valley={r['split_axis']['valley']} (expect False)")

    # (d) the thesis-critical case: a LARGE drift dominates variance (top modes),
    #     and TWO units differ only in a subtle low-variance shape direction,
    #     interleaved in time.  The split axis must be recovered at a LOW rank.
    n = 1600
    t = np.sort(rng.random(n))
    mode = rng.random(n) < 0.45                      # which of two units (time-interleaved)
    dd = np.empty((n, n_samp, n_ch), np.float32)
    for i in range(n):
        pc = 3.0 + 3.0 * t[i]                        # big drift ch3->ch6 (dominant variance)
        w = 1.35 if mode[i] else 2.15                # subtle width diff = the two units
        dd[i] = _make_template(n_ch, n_samp, pc, w, 1.6) + 12 * rng.standard_normal((n_samp, n_ch))
    r = within_cluster_split(dd.reshape(n, -1), n_ch, n_samp, times=t)
    sa = r["split_axis"]
    print(f"(d) drift + fine split   : bimodal={r['bimodal']} gap_ok={r['gap_ok']} "
          f"axis_rank={sa['rank']} var_frac={sa['var_frac']:.3f} ashman={sa['ashman']} "
          f"valley={sa['valley']} time_auc={r['time_auc']}")
    print("    (expect: bimodal=True at a NON-zero rank = the low-variance shape axis,\n"
          "     recovered from under a drift that owns the top modes)")


def selftest():
    rng = np.random.default_rng(0)
    n_ch, n_samp = 8, 32
    # A and A' = SAME neuron whose footprint drifts across the session (ch3 ->
    # ch5.2).  Within each sub-cluster the footprint also jitters (sd 0.8), so
    # the spatial-gradient ("drift") direction IS a leading within-cluster
    # eigenmode — the A->A' separation (~2.8 sigma) lives along it.
    A  = _gen_cluster(1500, n_ch, n_samp, 3.0, 0.8, width=2.2, foot_sigma=1.6, noise=12, rng=rng)
    Ap = _gen_cluster(1500, n_ch, n_samp, 5.2, 0.8, width=2.2, foot_sigma=1.6, noise=12, rng=rng)
    # B = DIFFERENT neuron: narrower spike, same nominal footprint (discriminative
    # in a low-variance/shape direction, NOT along the drift axis).
    B  = _gen_cluster(1500, n_ch, n_samp, 3.0, 0.8, width=1.2, foot_sigma=1.6, noise=12, rng=rng)

    spk = np.vstack([A, Ap, B])
    clu = np.array([2] * len(A) + [3] * len(Ap) + [4] * len(B), dtype=np.int32)
    spk_f = spk.reshape(spk.shape[0], -1)

    def waves(cid):
        return spk_f[clu == cid]

    def means(cid):
        return waves(cid).mean(0)

    print("Synthetic check (A=cluster2, A'=cluster3 SAME neuron drifted ch3->ch5.2, "
          "B=cluster4 DIFFERENT/narrower):\n")
    print(f"{'pair':10s} {'rigid xcorr':>12s} {'eigen-resid':>12s}   verdict")
    for (a, b, truth) in [(2, 3, "MERGE"), (2, 4, "keep"), (3, 4, "keep"), (2, 2, "self")]:
        xc = rigid_xcorr_score(means(a), means(b), n_ch, n_samp)
        er = eigen_residual_score(waves(a), waves(b), n_ch, n_samp,
                                  k=6, c=3.0, floor_frac=0.02)
        print(f"{a}-{b:<8d} {xc:12.4f} {er:12.4f}   (truth: {truth})")
    print("\nReading: rigid xcorr INVERTS — the true merge 2-3 scores LOWER than the "
          "keep pair 2-4,\nbecause it can't tell the drift axis is 'free' for this "
          "neuron; no threshold separates them.\nEigen-residual absorbs the drift "
          "(it's a leading within-cluster mode) -> small for 2-3, large for 2-4/3-4 "
          "(the\nnarrower-spike difference lands in a low-variance discriminative "
          "direction).  The merged cluster stays low-variance.")
    selftest_family()
    selftest_split()


def selftest_family():
    rng = np.random.default_rng(1)
    n_ch, n_samp = 12, 32
    # One neuron drifting ch3 -> ch9 across the session as five time-ordered
    # sub-clusters (10..14), within-cluster footprint sd 0.5.
    peaks = [3.0, 4.5, 6.0, 7.5, 9.0]
    cids = [10, 11, 12, 13, 14]
    waves, mean_time = {}, {}
    for ci, (cid, pk) in enumerate(zip(cids, peaks)):
        w = _gen_cluster(1200, n_ch, n_samp, pk, 0.5, width=2.2, foot_sigma=1.8,
                         noise=12, rng=rng)
        waves[cid] = w.reshape(w.shape[0], -1)
        mean_time[cid] = float(ci)                 # increasing time = real drift order

    # pairwise residuals across the chain
    pair = {}
    for i in range(len(cids)):
        for j in range(i + 1, len(cids)):
            a, b = cids[i], cids[j]
            pair[(a, b)] = eigen_residual_score(waves[a], waves[b], n_ch, n_samp,
                                                k=6, c=3.0, floor_frac=0.02)

    print("\n\n── Family-curve stage ──────────────────────────────────────────")
    print("One neuron drifting ch3->ch9 as 5 time-ordered sub-clusters (10..14).")
    print("\nPairwise eigen-residual (adjacent hops small, endpoints large):")
    print("        " + "".join(f"{c:>7d}" for c in cids))
    for a in cids:
        row = []
        for b in cids:
            if a == b:
                row.append(f"{0.0:7.2f}")
            else:
                row.append(f"{pair[tuple(sorted((a, b)))]:7.2f}")
        print(f"  c{a:<5d}" + "".join(row))
    e = pair[(10, 14)]
    print(f"\nEndpoints 10-14 direct residual = {e:.2f} (large — direct pairwise/clique "
          "would NOT merge them).")

    edge = 1.5
    fams = build_families(cids, pair, edge_thresh=edge)
    fams = [f for f in fams if len(f) >= 2]
    print(f"Connected components at edge<{edge}: {fams}")

    # curve metrics: real (time-ordered) vs scrambled-time (same geometry)
    big = max(fams, key=len)
    m_real = family_curve_metrics(big, waves, mean_time, n_ch, n_samp)
    scrambled = {10: 2.0, 11: 0.0, 12: 4.0, 13: 1.0, 14: 3.0}   # same clusters, shuffled times
    m_scr = family_curve_metrics(big, waves, scrambled, n_ch, n_samp)
    print(f"\n{'family':28s} {'monotonicity':>13s} {'rms_offcurve':>13s} "
          f"{'endpoint_span':>14s}")
    print(f"{'real drift order':28s} {m_real['monotonicity']:13.3f} "
          f"{m_real['rms_offcurve']:13.3f} {m_real['endpoint_resid']:14.2f}")
    print(f"{'scrambled times (same geom)':28s} {m_scr['monotonicity']:13.3f} "
          f"{m_scr['rms_offcurve']:13.3f} {m_scr['endpoint_resid']:14.2f}")
    print("\nReading: the chain links all 5 sub-clusters into ONE family through small "
          "adjacent hops,\nso the endpoints (residual %.1f) merge via the curve without "
          "ever being directly close.\nThe SAME geometry with scrambled times drops "
          "monotonicity (%.2f -> %.2f): the warp coord no\nlonger tracks time, so the "
          "veto fires — that monotonicity is the precision lever that keeps the\nhigher-"
          "recall scorer from over-merging."
          % (e, m_real['monotonicity'], m_scr['monotonicity']))


# ───────────────────────────────── CLI ───────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true",
                    help="run the synthetic mechanism demo and exit")
    ap.add_argument("--spk", help=".spk/.spkD file")
    ap.add_argument("--clu", help=".clu file (binary int32)")
    ap.add_argument("--res", help=".res file (binary int64) for per-cluster mean time")
    ap.add_argument("--nch", type=int, help="channels in the group")
    ap.add_argument("--nsamp", type=int, help="samples per spike")
    ap.add_argument("--curation-log", help="curation_log .jl for GROUP ground truth")
    ap.add_argument("--k", type=int, default=6, help="nuisance modes allowed to move")
    ap.add_argument("--c", type=float, default=3.0, help="movement bound in sigma")
    ap.add_argument("--floor-frac", type=float, default=0.02,
                    help="eigenvalue shrinkage floor as fraction of top eigenvalue")
    ap.add_argument("--max-shift", type=int, default=6)
    ap.add_argument("--max-spikes", type=int, default=2000)
    ap.add_argument("--min-size", type=int, default=20)
    ap.add_argument("--edge-thresh", type=float, default=1.0,
                    help="eigen-residual below which two clusters get a family edge")
    ap.add_argument("--top", type=int, default=30, help="print N most-mergeable pairs")
    # within-cluster split scan
    ap.add_argument("--split-scan", action="store_true",
                    help="test clusters for internal bimodality instead of pair merging")
    ap.add_argument("--npz", help="cluster_waveforms npz to pick n-gated bimodal targets")
    ap.add_argument("--split-targets", help="comma list of cluster ids to test")
    ap.add_argument("--n-min", type=int, default=100,
                    help="min spikes for a split target (kurtosis is degenerate below)")
    ap.add_argument("--kurt-thr", type=float, default=-0.7,
                    help="min_kurt_dom threshold for npz target selection")
    ap.add_argument("--split-out", default="split_scan_out.json")
    args = ap.parse_args()

    if args.selftest:
        selftest(); return 0

    for req in ("spk", "clu", "nch", "nsamp"):
        if getattr(args, req) is None:
            ap.error("--spk, --clu, --nch, --nsamp are required (or use --selftest)")

    rng = np.random.default_rng(0)
    clu = read_clu(args.clu)
    spk = read_spk(args.spk, args.nch, args.nsamp)
    if spk.shape[0] != clu.size:
        sys.stderr.write(f"  WARNING: {spk.shape[0]} spikes in .spk vs {clu.size} in "
                         f".clu — using the smaller count\n")
        n = min(spk.shape[0], clu.size); spk = spk[:n]; clu = clu[:n]

    # ── within-cluster split scan ─────────────────────────────────────────
    if args.split_scan:
        import json as _json
        res = read_res(args.res) if args.res else None
        targets = []
        if args.npz:
            targets += npz_split_targets(args.npz, args.n_min, args.kurt_thr)
        if args.split_targets:
            targets += [int(x) for x in args.split_targets.split(",")]
        seen = set(); targets = [c for c in targets if not (c in seen or seen.add(c))]
        if not targets:
            ap.error("--split-scan needs --npz and/or --split-targets")
        out = dict(clu=os.path.basename(args.clu), n_targets=len(targets), splits=[])
        spk_f = spk.reshape(spk.shape[0], -1)
        for cid in targets:
            idx = np.flatnonzero(clu == cid)
            if idx.size < args.min_size:
                out["splits"].append(dict(clu=cid, skipped=f"n<{args.min_size}")); continue
            if idx.size > args.max_spikes:
                idx = rng.choice(idx, size=args.max_spikes, replace=False)
            times = res[idx].astype(float) if res is not None and res.size == clu.size else None
            r = within_cluster_split(spk_f[idx], args.nch, args.nsamp, times=times,
                                     floor_frac=args.floor_frac)
            r["clu"] = cid; out["splits"].append(r)
            sa = r.get("split_axis", {})
            sys.stderr.write(f"  SPLIT {cid}: bimodal={r.get('bimodal')} "
                             f"rank={sa.get('rank')} var_frac={sa.get('var_frac')} "
                             f"ashman={sa.get('ashman')} valley={sa.get('valley')} "
                             f"time={r.get('time_verdict')}\n")
        with open(args.split_out, "w") as fh:
            _json.dump(out, fh, indent=1)
        print(_json.dumps(out, indent=1))
        sys.stderr.write(f"  wrote {args.split_out} ({os.path.getsize(args.split_out)} bytes)\n")
        return 0

    gt = parse_curation_groups(args.curation_log) if args.curation_log else set()
    rows, waves = evaluate(spk, clu, args.nch, args.nsamp, gt, args.k, args.c,
                           args.floor_frac, args.max_shift, args.max_spikes,
                           args.min_size, rng)
    rows.sort(key=lambda r: r[5])                 # most-mergeable (low residual) first

    print(f"{'A':>5s} {'B':>5s} {'nA':>7s} {'nB':>7s} {'xcorr':>8s} "
          f"{'eigResid':>9s} {'GROUPed':>8s}")
    for (a, b, na, nb, xc, er, lab) in rows[: args.top]:
        print(f"{a:5d} {b:5d} {na:7d} {nb:7d} {xc:8.4f} {er:9.4f} "
              f"{'yes' if lab else '':>8s}")

    if gt:
        print("\nPairwise — against curation-log GROUP ground truth:")
        for name, idx, hi in [("rigid xcorr", 4, True), ("eigen-residual", 5, False)]:
            m = pr_at_sweep(rows, idx, hi)
            if m:
                print(f"  {name:16s} best-F1={m['best_f1']:.3f} "
                      f"P={m['precision']:.3f} R={m['recall']:.3f} "
                      f"AP={m['ap']:.3f}  ({m['n_pos']} positives / {m['n_pairs']} pairs)")
        print("  (win: eigen-residual recall/AP > rigid xcorr at equal precision)")

    # ── family stage ──────────────────────────────────────────────────────
    res = read_res(args.res) if args.res else None
    mean_time = cluster_mean_times(clu, res)
    if not mean_time:
        print("\n(no --res given: skipping the family-curve stage, which needs "
              "per-cluster mean spike time)")
        return 0

    pair_resid = {(a, b): er for (a, b, *_rest, er, _l) in
                  ((r[0], r[1], r[4], r[5], r[6]) for r in rows)}
    live = sorted(waves)
    fams = [f for f in build_families(live, pair_resid, args.edge_thresh) if len(f) >= 3]
    gt_fams = parse_curation_families(args.curation_log) if args.curation_log else []

    print(f"\nFamily-curve stage (edge<{args.edge_thresh}; {len(fams)} component(s) "
          f">=3 clusters):")
    print(f"  {'members':32s} {'monot':>6s} {'offcurve':>9s} {'span(σ)':>8s} {'GROUP?':>7s}")
    for f in sorted(fams, key=len, reverse=True):
        cm = family_curve_metrics(f, waves, mean_time, args.nch, args.nsamp,
                                  k_family=3, floor_frac=args.floor_frac)
        if cm is None:
            continue
        # does this component match a human GROUP family? (Jaccard >= 0.5)
        best = 0.0
        for gf in gt_fams:
            inter = len(set(f) & gf); uni = len(set(f) | gf)
            best = max(best, inter / uni if uni else 0.0)
        tag = f"J={best:.2f}" if gt_fams else ""
        mem = ",".join(str(c) for c in cm["members"])
        if len(mem) > 31:
            mem = mem[:28] + "..."
        print(f"  {mem:32s} {cm['monotonicity']:6.2f} {cm['rms_offcurve']:9.2f} "
              f"{cm['endpoint_resid']:8.2f} {tag:>7s}")
    print("  Accept a family when monotonicity is high AND off-curve RMS is low; "
          "high span\n  with low off-curve = a long drift correctly bridged via the "
          "curve (not direct pairwise).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
