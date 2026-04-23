#!/usr/bin/env python3
"""
kk_estimate_prior.py  —  Estimate a KlustaKwik prior directly from raw .fet.N data.

No curation log required.  The script:

  1. Loads and subsamples a .fet.N file (binary or legacy-text auto-detected)
  2. Runs a coarse k-means with large K to get rough cluster geometry
  3. Estimates the participation ratio (d_eff) per coarse cluster and globally
  4. Derives an adaptive MergeThresh = chi²(d_eff_median, 0.9999)
  5. Estimates the natural cluster count by merging coarse sub-clusters whose
     symmetric Mahalanobis distance is below the adaptive threshold
  6. Computes per-dimension Fisher discriminant ratios from the coarse partition
  7. Fits the SNR→variance model from spike amplitude proxies (if .spk available)
  8. Writes a .prior.N.yaml readable by KlustaKwik's -PriorFile parameter

Geometry model
──────────────
Two cluster archetypes dominate the feature space:

  Type A — strong / localized
    Spike close to a recording contact: high amplitude, 1-2 dominant channels.
    In PCA space: few large eigenvalues → d_eff ≈ 2–5.
    Correct MergeThresh = chi²(3.5, 0.9999) ≈ 17  (not chi²(24) ≈ 42).

  Type B — weak / distributed
    Spike at moderate distance: low amplitude spread across many channels.
    In PCA space: many moderate eigenvalues → d_eff ≈ 12–18.
    Correct MergeThresh = chi²(14, 0.9999) ≈ 37.

The participation ratio d_eff = (Σσ²_i)² / Σ(σ²_i)² distinguishes these
without needing any cluster labels.  KlustaKwik uses this per-pair from its
own fitted covariances (AdaptiveMerge=1, default on); this script provides
an empirical calibration from the feature data itself.

Usage
─────
  python kk_estimate_prior.py <session.fet.N>  [options]
      --out     PATH          output .prior.N.yaml  [default: <session>.prior.N.yaml]
      --n-sub   INT           subsample size        [default: 8000]
      --k-coarse INT          coarse k-means K       [default: auto = sqrt(N/4)]
      --n-init  INT           k-means restarts       [default: 5]
      --spk     PATH          .spk.N path for amplitude proxy (optional)
      --n-channels INT        channels per spike group (required with --spk)
      --n-samples  INT        samples per spike (required with --spk)
      --report                print geometry report to stdout

  Also accepts .fetD.N directly.
"""

import argparse
import math
import struct
import sys
import os
from pathlib import Path

import numpy as np
from scipy.cluster.vq import kmeans2, whiten
from scipy import stats


# ── Constants ─────────────────────────────────────────────────────────────────

SUBSAMPLE_DEFAULT = 8000
K_COARSE_MIN      = 6
K_COARSE_MAX      = 60


# ── Maths helpers ─────────────────────────────────────────────────────────────

def participation_ratio(variances):
    """d_eff = (Σσ²)² / Σ(σ²)²  — effective dimensionality of a variance profile."""
    v = np.maximum(np.asarray(variances, dtype=float), 0.0)
    sv  = v.sum()
    sv2 = (v ** 2).sum()
    if sv2 < 1e-12:
        return float(len(v))
    return float(sv * sv / sv2)


def chi2_quantile(d_eff, p_z):
    """Wilson-Hilferty chi²(d_eff) at normal quantile p_z.
    p_z = 3.719 → p = 0.9999;  p_z = 2.576 → p = 0.9950;  p_z = 1.282 → p = 0.90"""
    d = max(1.0, float(d_eff))
    t = 1.0 - 2.0 / (9.0 * d) + p_z * math.sqrt(2.0 / (9.0 * d))
    return d * t ** 3


def chi2_quantile_9999(d_eff):
    return chi2_quantile(d_eff, 3.719)


def sym_mahalanobis_diag(ci, cj, var_i, var_j, eps=1e-6):
    """Symmetric Mahalanobis distance using diagonal covariance approximation.
    d_sym = ½ · (||ci-cj||²_Σj^{-1} + ||ci-cj||²_Σi^{-1})
    Valid when PCA features are uncorrelated (which they are by construction)."""
    diff = ci - cj
    d2 = diff ** 2
    inv_i = 1.0 / np.maximum(var_i, eps)
    inv_j = 1.0 / np.maximum(var_j, eps)
    return 0.5 * (d2 @ inv_i + d2 @ inv_j)


# ── .fet file I/O ─────────────────────────────────────────────────────────────

def load_fet(path):
    """Load a .fet.N file.  Returns (features, n_dims_total) where features
    is float32 ndarray (nSpikes, nDims) including the timestamp column."""
    path = str(path)
    with open(path, 'rb') as f:
        first = f.read(1)
        if not first:
            raise IOError(f"Empty file: {path}")
        f.seek(0)

        is_binary = (first[0] < 0x30 or first[0] > 0x39)

        if is_binary:
            # Binary: int32 nDims, then nSpikes×nDims×int64, row-major
            n_dims = struct.unpack('<i', f.read(4))[0]
            raw = f.read()
            n_spikes = len(raw) // (8 * n_dims)
            data = np.frombuffer(raw[:n_spikes * n_dims * 8],
                                 dtype='<i8').reshape(n_spikes, n_dims)
        else:
            # Text: first line = nDims, then nSpikes lines of nDims integers
            header = f.readline().decode()
            n_dims = int(header.strip())
            lines = f.read().decode()
            rows = []
            for line in lines.splitlines():
                line = line.strip()
                if line:
                    rows.append([int(x) for x in line.split()])
            if not rows:
                raise IOError(f"No spike data in {path}")
            data = np.array(rows, dtype='int64')

    return data.astype(np.float32), n_dims


# ── Amplitude proxy from .spk ─────────────────────────────────────────────────

def load_spk_amplitudes(path, n_samples, n_spikes_expected,
                        n_channels=0):
    """Load peak-to-trough amplitude per spike from a .spk.N or .spkD.N file.

    Layout (both variants): nSpikes × nChannels × nSamples, int16, sample-major.
    For .spkD.N the spatial derivative has already been applied, so nChannels
    is (nRawChannels - 1) for SDIFF_ALLPAIRS orders 1 and 3.

    Args:
        path:              .spk.N or .spkD.N path.
        n_samples:         Samples per waveform window (required).
        n_spikes_expected: Spike count from the paired .fet file (used to
                           auto-derive channel count when n_channels==0).
        n_channels:        If > 0, use directly.  If 0, derive from file size.
    Returns:
        float32 array of shape (nSpikes,) — max peak-to-trough across channels.
        None on failure.
    """
    path = str(path)
    n_int16 = os.path.getsize(path) // 2
    if n_int16 == 0 or n_spikes_expected <= 0 or n_samples <= 0:
        return None

    if n_channels <= 0:
        # Auto-derive: file_size / (nSpikes × nSamples × 2 bytes)
        n_channels = n_int16 // (n_spikes_expected * n_samples)
        if n_channels <= 0:
            print(f"  WARNING: cannot derive channel count from {path} "
                  f"(size={n_int16*2} bytes, {n_spikes_expected} spikes, "
                  f"{n_samples} samples/spike)", file=sys.stderr)
            return None
        print(f"  Auto-detected channels = {n_channels} from file size")

    n_elem_per_spike = n_channels * n_samples
    raw = np.fromfile(path, dtype='<i2')
    n_spikes_actual = min(len(raw) // n_elem_per_spike, n_spikes_expected)
    if n_spikes_actual == 0:
        return None
    raw = raw[:n_spikes_actual * n_elem_per_spike].reshape(
        n_spikes_actual, n_channels, n_samples)
    amp = (raw.max(axis=2) - raw.min(axis=2)).max(axis=1).astype(np.float32)
    return amp


# ── Coarse k-means ────────────────────────────────────────────────────────────

def coarse_kmeans(X, K, n_init=5, seed=42):
    """Run k-means K times with different seeds, return best labels + centroids."""
    rng = np.random.default_rng(seed)
    best_labels = None
    best_inertia = np.inf

    for i in range(n_init):
        init_idx = rng.choice(len(X), K, replace=False)
        try:
            centroids, labels = kmeans2(X, X[init_idx], minit='matrix',
                                        iter=100, check_finite=False)
        except Exception:
            continue
        inertia = sum(
            float(np.sum((X[labels == k] - centroids[k]) ** 2))
            for k in range(K) if (labels == k).any()
        )
        if inertia < best_inertia:
            best_inertia = inertia
            best_labels  = labels.copy()
            best_centroids = centroids.copy()

    if best_labels is None:
        raise RuntimeError("All k-means restarts failed")
    return best_labels, best_centroids


# ── Cluster-count estimation by merging coarse sub-clusters ──────────────────

def estimate_cluster_count(centroids, var_per_cluster, sizes,
                            min_fraction=0.005):
    """
    Merge coarse k-means sub-clusters greedily when their symmetric
    Mahalanobis distance < chi²(d_eff_pair, 0.9999).  The number of
    surviving super-clusters is the estimated natural cluster count.

    This replicates KlustaKwik's own MNN merge logic but on the coarse
    k-means output, giving an estimate before any actual CEM run.

    min_fraction: clusters smaller than this fraction of total spikes
    are treated as noise and excluded from the count.
    """
    total = sum(sizes)
    K = len(centroids)

    # Build alive set (exclude micro-clusters)
    alive = [i for i in range(K) if sizes[i] >= min_fraction * total]
    if not alive:
        alive = list(range(K))  # fallback: keep all

    # Union-Find
    parent = list(range(K))
    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    def union(a, b):
        parent[find(a)] = find(b)

    n_alive = len(alive)

    # All-pairs symmetric Mahalanobis and adaptive threshold
    for ii in range(n_alive):
        for jj in range(ii + 1, n_alive):
            i, j = alive[ii], alive[jj]
            if find(i) == find(j):
                continue
            d_eff_i = participation_ratio(var_per_cluster[i])
            d_eff_j = participation_ratio(var_per_cluster[j])
            thresh  = chi2_quantile_9999(0.5 * (d_eff_i + d_eff_j))
            d_sym   = sym_mahalanobis_diag(
                centroids[i], centroids[j],
                var_per_cluster[i], var_per_cluster[j])
            if d_sym < thresh:
                union(i, j)

    # Count surviving super-clusters
    roots = {find(i) for i in alive}
    return len(roots)


# ── Per-cluster geometry ──────────────────────────────────────────────────────

def cluster_geometry(X, labels, K):
    """Compute per-cluster statistics: centroid, variance, d_eff, frobenius, size."""
    D = X.shape[1]
    centroids    = np.zeros((K, D), dtype=np.float64)
    var_clusters = np.zeros((K, D), dtype=np.float64)
    sizes        = np.zeros(K, dtype=int)

    for k in range(K):
        mask = labels == k
        n = mask.sum()
        sizes[k] = n
        if n < 2:
            continue
        members = X[mask].astype(np.float64)
        centroids[k]    = members.mean(axis=0)
        var_clusters[k] = members.var(axis=0, ddof=1)

    return centroids, var_clusters, sizes


# ── Fisher discriminant ratios ────────────────────────────────────────────────

def fisher_discriminant_ratios(X, labels, K, centroids, var_clusters, sizes):
    """
    Per-dimension Fisher discriminant ratio:
      FDR[d] = between_cluster_variance[d] / pooled_within_cluster_variance[d]

    between: weighted variance of cluster means in dim d
    within:  pooled (size-weighted) within-cluster variance in dim d
    """
    D = X.shape[1]
    total = sizes.sum()
    global_mean = (sizes[:, None] * centroids).sum(axis=0) / max(total, 1)

    between = np.zeros(D)
    within  = np.zeros(D)

    for k in range(K):
        n = sizes[k]
        if n < 2:
            continue
        diff = centroids[k] - global_mean
        between += (n / total) * (diff ** 2)
        within  += (n / total) * var_clusters[k]

    eps = 1e-9
    fdr = between / (within + eps)
    return fdr.tolist()


# ── Raw dimension statistics ──────────────────────────────────────────────────

def dimension_stats(X):
    """Per-dimension: variance, excess kurtosis, bimodality coefficient."""
    D = X.shape[1]
    variances = X.var(axis=0, ddof=1).tolist()
    kurtoses  = []
    bimods    = []
    for d in range(D):
        col = X[:, d].astype(np.float64)
        kurt = float(stats.kurtosis(col, fisher=True))   # excess kurtosis
        skew = float(stats.skew(col))
        bc   = (skew**2 + 1.0) / (kurt + 3.0 + 1e-9)    # Sarle's bimodality coeff
        kurtoses.append(kurt)
        bimods.append(bc)
    return variances, kurtoses, bimods


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fet_file",
                    help=".fet.N or .fetD.N file to analyse")
    ap.add_argument("--out", default=None,
                    help="Output .prior.N.yaml path")
    ap.add_argument("--n-sub",      type=int, default=SUBSAMPLE_DEFAULT,
                    help=f"Subsample size for geometry estimation (default {SUBSAMPLE_DEFAULT})")
    ap.add_argument("--k-coarse",   type=int, default=0,
                    help="Coarse k-means K (default: auto = sqrt(N_sub/4))")
    ap.add_argument("--n-init",     type=int, default=5,
                    help="k-means restarts (default 5)")
    ap.add_argument("--spk",        default=None,
                    help=".spk.N or .spkD.N file (auto-detected from "
                         ".fet path when absent)")
    ap.add_argument("--n-channels", type=int, default=0,
                    help="Channels in the spk file (auto-derived from "
                         "file size when omitted; for stderiv = nRawChan-1)")
    ap.add_argument("--n-samples",  type=int, default=0,
                    help="Samples per spike waveform (required for spk loading)")
    ap.add_argument("--report",     action="store_true",
                    help="Print geometry report to stdout")
    args = ap.parse_args()

    import yaml   # import here so the rest of the script is usable without it

    # ── Load .fet file ─────────────────────────────────────────────────────
    # ── Resolve .fet / .fetD path and detect stderiv session ─────────────────
    # Accept:  session.fet.1  session.fetD.1  (explicit)
    # Fallback: if canonical .fet.N missing, try .fetD.N automatically.
    raw_path = Path(args.fet_file)
    if raw_path.exists():
        fet_path = raw_path
    else:
        # Try the other variant
        name = str(raw_path.name)
        if '.fet.' in name:
            alt = raw_path.parent / name.replace('.fet.', '.fetD.', 1)
        else:
            alt = raw_path.parent / name.replace('.fetD.', '.fet.', 1)
        if alt.exists():
            fet_path = alt
            print(f"Note: using variant: {fet_path}")
        else:
            print(f"ERROR: {args.fet_file} not found", file=sys.stderr)
            sys.exit(1)

    is_stderiv = '.fetD.' in str(fet_path.name)
    print(f"Loading {fet_path}  (stderiv={is_stderiv})…")
    data, n_dims_total = load_fet(fet_path)
    n_spikes, n_dims_loaded = data.shape
    print(f"  {n_spikes} spikes × {n_dims_loaded} features (nDims={n_dims_total})")

    # Last column is the timestamp — drop it for all geometry computations
    X_full = data[:, :-1].astype(np.float64)   # shape (N, nPCA)
    n_feat  = X_full.shape[1]                   # PCA features only

    # ── Subsample ──────────────────────────────────────────────────────────
    N_sub = min(args.n_sub, n_spikes)
    rng = np.random.default_rng(42)
    idx = rng.choice(n_spikes, N_sub, replace=False)
    idx.sort()
    X = X_full[idx]                             # shape (N_sub, nPCA)
    print(f"  Subsampled to {N_sub} spikes")

    # ── Global participation ratio ─────────────────────────────────────────
    global_var  = X.var(axis=0, ddof=1)
    d_eff_global = participation_ratio(global_var)
    global_frob  = float(np.sqrt((global_var**2).sum()))
    print(f"\nGlobal feature space:")
    print(f"  d_eff_global = {d_eff_global:.2f}  (1=localized, {n_feat}=distributed)")
    print(f"  Frobenius    = {global_frob:.1f}")
    print(f"  chi²(d_eff_global, 0.9999) = {chi2_quantile_9999(d_eff_global):.1f}")
    print(f"  chi²({n_feat}, 0.9999)  = {chi2_quantile_9999(n_feat):.1f}  ← current default")

    # ── Raw dimension statistics ───────────────────────────────────────────
    dim_variances, dim_kurtoses, dim_bimods = dimension_stats(X)

    # ── Coarse k-means ─────────────────────────────────────────────────────
    K_auto = int(max(K_COARSE_MIN, min(K_COARSE_MAX, math.sqrt(N_sub / 4))))
    K = args.k_coarse if args.k_coarse > 0 else K_auto
    print(f"\nRunning coarse k-means: K={K}, n_init={args.n_init}…")

    # Whiten X for k-means stability (scipy's kmeans2 works in original space
    # but whitening accelerates convergence for elongated distributions)
    std = X.std(axis=0)
    std[std < 1e-9] = 1.0
    Xw = X / std

    labels, centroids_w = coarse_kmeans(Xw, K, n_init=args.n_init)

    # Un-whiten centroids and compute per-cluster geometry in original space
    centroids = centroids_w * std           # shape (K, nPCA)
    _, var_clusters, sizes = cluster_geometry(X, labels, K)
    # Use original centroids from cluster_geometry (not whitened)
    centroids, var_clusters, sizes = cluster_geometry(X, labels, K)

    alive_mask = sizes >= 2
    n_alive = alive_mask.sum()
    print(f"  {n_alive}/{K} sub-clusters have ≥ 2 members")

    # ── Per-sub-cluster d_eff ──────────────────────────────────────────────
    d_effs = []
    for k in range(K):
        if sizes[k] < 5:
            continue
        de = participation_ratio(var_clusters[k])
        d_effs.append(de)

    d_effs = np.array(d_effs)
    d_eff_p05    = float(np.percentile(d_effs, 5))
    d_eff_p25    = float(np.percentile(d_effs, 25))
    d_eff_median = float(np.median(d_effs))
    d_eff_p75    = float(np.percentile(d_effs, 75))
    d_eff_p95    = float(np.percentile(d_effs, 95))
    merge_thresh = chi2_quantile_9999(d_eff_median)

    print(f"\nd_eff distribution across coarse sub-clusters:")
    print(f"  p05={d_eff_p05:.1f}  p25={d_eff_p25:.1f}  median={d_eff_median:.1f}"
          f"  p75={d_eff_p75:.1f}  p95={d_eff_p95:.1f}")
    print(f"  MergeThresh = chi²({d_eff_median:.1f}, 0.9999) = {merge_thresh:.1f}")

    # ── Cluster-type fingerprinting ────────────────────────────────────────
    # Classify sub-clusters into Type A (localized) vs Type B (distributed)
    # using a simple threshold on d_eff.
    # Boundary: clusters with d_eff < 1/3 of n_feat are Type A.
    boundary = n_feat / 3.0
    type_a = d_effs[d_effs < boundary]
    type_b = d_effs[d_effs >= boundary]
    frac_a = len(type_a) / max(len(d_effs), 1)
    frac_b = len(type_b) / max(len(d_effs), 1)

    cluster_types = []
    if len(type_a) >= 2:
        cluster_types.append({
            "label": "strong_localized",
            "fraction": round(frac_a, 3),
            "d_eff_median": round(float(np.median(type_a)), 2),
            "merge_thresh_for_type": round(chi2_quantile_9999(np.median(type_a)), 2),
            "n_sub_clusters": int(len(type_a)),
        })
    if len(type_b) >= 2:
        cluster_types.append({
            "label": "weak_distributed",
            "fraction": round(frac_b, 3),
            "d_eff_median": round(float(np.median(type_b)), 2),
            "merge_thresh_for_type": round(chi2_quantile_9999(np.median(type_b)), 2),
            "n_sub_clusters": int(len(type_b)),
        })
    for t in cluster_types:
        print(f"  {t['label']:22s}  {t['fraction']*100:.0f}%  "
              f"d_eff={t['d_eff_median']:.1f}  thresh={t['merge_thresh_for_type']:.1f}")

    # ── Natural cluster count estimate ─────────────────────────────────────
    valid_k = [k for k in range(K) if sizes[k] >= 5]
    n_est = estimate_cluster_count(
        centroids[valid_k], var_clusters[valid_k], sizes[valid_k],
        min_fraction=0.005)

    print(f"\nEstimated natural cluster count: {n_est}")
    print(f"  (from {len(valid_k)} coarse sub-clusters merged at adaptive threshold)")

    n_est_lo = max(2, n_est - 1)
    n_est_hi = n_est + 2

    # ── Pairwise inter-cluster distances ───────────────────────────────────
    sym_mahals = []
    for ii in range(len(valid_k)):
        for jj in range(ii + 1, len(valid_k)):
            i, j = valid_k[ii], valid_k[jj]
            if sizes[i] < 5 or sizes[j] < 5:
                continue
            d_sym = sym_mahalanobis_diag(
                centroids[i], centroids[j],
                var_clusters[i], var_clusters[j])
            sym_mahals.append(float(d_sym))

    sym_mahals = np.array(sym_mahals)
    if len(sym_mahals) > 0:
        ic_p05    = float(np.percentile(sym_mahals, 5))
        ic_median = float(np.percentile(sym_mahals, 50))
    else:
        ic_p05 = ic_median = 0.0

    print(f"\nInter-cluster sym-Mahal (pairwise, coarse): "
          f"p05={ic_p05:.1f}  median={ic_median:.1f}")

    # ── Feature dimension importance ───────────────────────────────────────
    fdr = fisher_discriminant_ratios(X, labels, K, centroids, var_clusters, sizes)
    importance_order = sorted(range(len(fdr)), key=lambda i: -fdr[i])

    # Combine FDR with bimodality coefficient for a composite score
    bimod_arr = np.array(dim_bimods[:len(fdr)])
    fdr_arr   = np.array(fdr)
    # Normalise each to [0,1] and average
    def norm01(a):
        r = a.max() - a.min()
        return (a - a.min()) / (r if r > 1e-9 else 1.0)
    composite = 0.6 * norm01(fdr_arr) + 0.4 * norm01(bimod_arr)
    composite_order = sorted(range(len(composite)), key=lambda i: -composite[i])

    top5 = composite_order[:5]
    print(f"\nTop 5 discriminating dims (FDR + bimodality): {top5}")
    print(f"  Bimodality coefficients (Sarle BC > 5/9 = 0.556 suggests bimodal):")
    for d in top5:
        bc = dim_bimods[d] if d < len(dim_bimods) else 0
        fr = fdr[d] if d < len(fdr) else 0
        print(f"    dim {d:2d}: FDR={fr:.3f}  BC={bc:.3f}  var={dim_variances[d]:.1f}")

    # ── Amplitude proxy model from .spk/.spkD (optional) ───────────────────
    # Auto-locate companion .spk/.spkD when --spk not given.
    snr_A = snr_B = snr_r2 = 0.0
    spk_path = args.spk
    if not spk_path:
        # Derive spk path from fet path: session.fetD.1 → session.spkD.1
        p_name = str(fet_path.name)
        if '.fetD.' in p_name:
            spk_candidate = fet_path.parent / p_name.replace('.fetD.', '.spkD.', 1)
        else:
            spk_candidate = fet_path.parent / p_name.replace('.fet.', '.spk.', 1)
        if spk_candidate.exists():
            spk_path = str(spk_candidate)
            print(f"\nAuto-located spike file: {spk_path}")

    if spk_path and args.n_samples > 0:
        if is_stderiv:
            print(f"  Note: stderiv session — channel count in .spkD.N = nRawChannels-1")
        print(f"\nLoading amplitudes from {spk_path}…")
        amps = load_spk_amplitudes(spk_path, args.n_samples, n_spikes,
                                   n_channels=args.n_channels)
        if amps is not None:
            # Map spike amplitudes to their coarse sub-cluster, compute per-cluster
            # mean amplitude and per-cluster Frobenius radius.
            # Fit: frobenius ~ A / amp² + B
            frob_per_sub = []
            amp_per_sub  = []
            for k in range(K):
                if sizes[k] < 10:
                    continue
                k_orig_idx = idx[labels == k]     # indices in full spike array
                k_amps = amps[k_orig_idx].astype(float)
                if k_amps.mean() < 1.0:
                    continue
                frob_k = float(np.sqrt((var_clusters[k]**2).sum()))
                frob_per_sub.append(frob_k)
                amp_per_sub.append(float(k_amps.mean()))

            if len(frob_per_sub) >= 5:
                A_arr = 1.0 / (np.array(amp_per_sub) ** 2)
                Y_arr = np.array(frob_per_sub)
                mat = np.column_stack([A_arr, np.ones_like(A_arr)])
                coeff = np.linalg.lstsq(mat, Y_arr, rcond=None)[0]
                snr_A, snr_B = float(coeff[0]), float(coeff[1])
                y_pred = snr_A * A_arr + snr_B
                ss_res = ((Y_arr - y_pred)**2).sum()
                ss_tot = ((Y_arr - Y_arr.mean())**2).sum()
                snr_r2 = float(1.0 - ss_res / (ss_tot + 1e-12))
                print(f"  frobenius = {snr_A:.0f}/amp² + {snr_B:.0f}  (R²={snr_r2:.3f})")

    # ── PenaltyMix heuristic ───────────────────────────────────────────────
    # If the data is predominantly Type A (localized), the default PenaltyMix=0
    # tends to over-split.  Recommend a small non-zero value.
    penalty_mix = 0.02
    if frac_a > 0.6:
        penalty_mix = 0.05

    # ── Assemble prior YAML ────────────────────────────────────────────────
    prior = {
        "source": {
            "method":     "raw_feature_estimation",
            "fet_file":   str(fet_path),
            "is_stderiv": bool(is_stderiv),
            "n_spikes":   int(n_spikes),
            "n_sub":      int(N_sub),
            "k_coarse":   int(K),
            "note": ("Estimated from raw feature-space geometry. "
                     "Refine with kk_build_prior.py after curation sessions."),
        },
        "probe_signature": {
            "n_pca_dims":  int(n_feat),
            "sample_rate": 0.0,    # not available from .fet alone
        },
        "n_clusters": {
            "p05":    max(2, n_est_lo),
            "p25":    max(2, n_est),
            "median": max(2, n_est),
            "p75":    n_est_hi,
            "p95":    n_est_hi + 1,
        },
        "effective_dimensionality": {
            "d_eff_global":  round(d_eff_global, 2),
            "d_eff_p05":     round(d_eff_p05,    2),
            "d_eff_p25":     round(d_eff_p25,    2),
            "d_eff_median":  round(d_eff_median,  2),
            "d_eff_p75":     round(d_eff_p75,    2),
            "d_eff_p95":     round(d_eff_p95,    2),
            "merge_thresh_from_median_d_eff": round(merge_thresh, 2),
            "merge_thresh_current_default":  round(chi2_quantile_9999(n_feat), 2),
        },
        "inter_cluster_distance": {
            "sym_mahal_p05":    round(ic_p05,    2),
            "sym_mahal_median": round(ic_median, 2),
        },
        "dim_fisher_ratios":    [round(float(v), 4) for v in fdr],
        "dim_importance_order": importance_order,
        "dim_bimodality_coeff": [round(float(v), 4) for v in dim_bimods],
        "snr_variance_model": {
            "A":  round(snr_A, 1),
            "B":  round(snr_B, 1),
            "r2": round(snr_r2, 3),
        },
        "penalty_mix_suggested": round(penalty_mix, 4),
        "cluster_types": cluster_types,
        "preseed_centres": [],
    }

    # ── Determine output path ──────────────────────────────────────────────
    if args.out:
        out_path = Path(args.out)
    else:
        # Replace .fet.N or .fetD.N suffix with .prior.N.yaml
        name = str(fet_path.name)
        for ext in ('.fetD.', '.fet.'):
            if ext in name:
                grp = name.split(ext)[-1]
                out_path = fet_path.parent / f"{name.split(ext)[0]}.prior.{grp}.yaml"
                break
        else:
            out_path = fet_path.with_suffix('.prior.yaml')

    with open(out_path, 'w') as f:
        yaml.dump(prior, f, default_flow_style=False, sort_keys=False,
                  allow_unicode=True)

    print(f"\nPrior written to: {out_path}")

    # ── Summary ────────────────────────────────────────────────────────────
    print("\n" + "─"*60)
    print("What KlustaKwik will use (with -PriorFile):")
    print(f"  MinClusters  <- {max(2, n_est_lo)}")
    print(f"  MaxClusters  <- {n_est_hi + 1}")
    print(f"  MergeThresh  <- {merge_thresh:.1f}  (was {chi2_quantile_9999(n_feat):.1f})")
    print(f"  PenaltyMix   <- {penalty_mix:.3f}")
    print(f"  AdaptiveMerge = 1  (per-pair d_eff, no prior needed)")
    print()
    print("Geometry summary:")
    print(f"  Global d_eff = {d_eff_global:.1f}  →  data is "
          + ("LOCALIZED (few dominant dims)" if d_eff_global < n_feat/3
             else "DISTRIBUTED (many active dims)" if d_eff_global > 2*n_feat/3
             else "MIXED"))
    if d_eff_median < chi2_quantile_9999(n_feat) * 0.6:
        gap = chi2_quantile_9999(n_feat) - merge_thresh
        print(f"  IMPORTANT: adaptive threshold saves {gap:.1f} units vs default "
              f"— reduces false merges of tight Type A clusters")
    print("─"*60)


if __name__ == "__main__":
    main()
