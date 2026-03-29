#!/usr/bin/env python3
"""
eval_tier1.py — Evaluate KlustaKwik Tier 1 sweep results
=========================================================
Reads .clu and .fet files from each sweep_tier1/mt*_pm* subdirectory,
computes per-run cluster quality metrics, and writes:

  sweep_tier1/results.csv             — full per-run summary
  sweep_tier1/heatmap_nclusters.png   — n_clusters grid
  sweep_tier1/heatmap_isodist.png     — mean isolation distance grid
  sweep_tier1/heatmap_lratio.png      — mean log10(L-ratio) grid
  sweep_tier1/heatmap_merged.png      — composite quality score grid

Usage:
  python3 eval_tier1.py <data_dir> <filebase> <elec_group>

Example:
  python3 eval_tier1.py /data/jg05-20120316 jg05-20120316 7
"""

import sys
import os
import re
import struct
import numpy as np
import pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from scipy.stats import chi2
from pathlib import Path


# ── Binary neurosuite file readers ──────────────────────────────────────────

def read_fet(path: str):
    """
    Read a .fet.N file (binary or legacy text).
    Returns (features, n_dims) where features is (n_spikes, n_dims) float64.
    The timestamp column (last column) is included.

    Binary format (produced by process_mergefeatures):
      int32_t  nDimensions
      nSpikes * nDimensions * int64_t   (row-major, PCA cols then timestamp)

    Legacy text format:
      line 0:  nDimensions
      lines 1+: space-separated feature values including timestamp
    """
    with open(path, 'rb') as f:
        header = f.read(4)
    if len(header) < 4:
        raise ValueError(f"read_fet: file too short: {path}")
    n_dims = struct.unpack('<i', header)[0]
    if 1 <= n_dims <= 256:
        # Binary: int32 header then n_spikes * n_dims int64 values
        payload = np.fromfile(path, dtype=np.int64, offset=4)
        n_spikes = len(payload) // n_dims
        features = payload[:n_spikes * n_dims].reshape(n_spikes, n_dims).astype(np.float64)
        return features, n_dims
    else:
        # Legacy text format
        with open(path, 'r') as f:
            n_dims = int(f.readline().strip())
            rows = []
            for line in f:
                line = line.strip()
                if line:
                    rows.append(list(map(float, line.split())))
        arr = np.array(rows, dtype=np.float64)
        return arr, n_dims


def read_clu(path: str):
    """
    Read a .clu.N file (binary or legacy text).
    Returns array of cluster ids (int32), length n_spikes.

    Binary format: little-endian int32 n_clusters header, then one int32 per spike.
    Text format:   first line = n_clusters (skipped), one cluster id per line.

    Detection mirrors read_fet: peek at the first 4 bytes as a little-endian
    int32.  If the value is in [1, 65535] the file is treated as binary.
    That range covers all plausible cluster counts while being safely above
    any ASCII digit or whitespace byte that would start a text file.
    """
    with open(path, 'rb') as f:
        header = f.read(4)

    if len(header) == 4:
        n_clusters_candidate = struct.unpack('<i', header)[0]
    else:
        n_clusters_candidate = -1

    if 1 <= n_clusters_candidate <= 65535:
        # Binary: int32 header (n_clusters) followed by n_spikes int32 cluster ids
        data = np.fromfile(path, dtype=np.int32)
        return data[1:]      # skip the n_clusters header word
    else:
        # Legacy text format
        with open(path, 'r') as f:
            lines = [l.strip() for l in f if l.strip()]
        return np.array(lines[1:], dtype=np.int32)


# ── Quality metrics ──────────────────────────────────────────────────────────

def isolation_distance(features: np.ndarray, labels: np.ndarray, cluster_id: int):
    """
    Mahalanobis-based isolation distance for a single cluster.
    Returns float or NaN if the cluster has too few spikes.

    Isolation distance: the Mahalanobis distance from the cluster centre
    at which the number of noise spikes inside equals the number of cluster
    spikes (Harris et al. 2001).
    """
    mask = labels == cluster_id
    n_c = mask.sum()
    if n_c < features.shape[1] + 2:   # need > n_dims spikes for stable cov
        return np.nan

    cluster_pts = features[mask]
    other_pts   = features[~mask & (labels > 1)]  # exclude noise (cluster 1) and artefact (0)

    if len(other_pts) < n_c:
        return np.nan

    mu = cluster_pts.mean(axis=0)
    try:
        cov = np.cov(cluster_pts.T)
        if cov.ndim == 0:
            cov = np.array([[float(cov)]])
        cov_inv = np.linalg.inv(cov + 1e-10 * np.eye(len(mu)))
    except np.linalg.LinAlgError:
        return np.nan

    def mahal_sq(pts):
        d = pts - mu
        return np.einsum('ij,jk,ik->i', d, cov_inv, d)

    d_cluster = mahal_sq(cluster_pts)
    d_other   = mahal_sq(other_pts)

    # Isolation distance = d² at which |{other | d²(other) < iso}| == n_c
    d_other_sorted = np.sort(d_other)
    if n_c > len(d_other_sorted):
        return np.nan
    return float(d_other_sorted[n_c - 1])


def l_ratio(features: np.ndarray, labels: np.ndarray, cluster_id: int):
    """
    L-ratio (Schmitzer-Torbert et al. 2005).
    L = sum_{j not in C} (1 - CDF_chi2(d²_j, k)) / n_c
    where k = n_dims, d²_j = Mahalanobis distance from cluster centre.
    Returns float or NaN.
    """
    n_dims = features.shape[1]
    mask = labels == cluster_id
    n_c = mask.sum()
    if n_c < n_dims + 2:
        return np.nan

    cluster_pts = features[mask]
    other_pts   = features[~mask & (labels > 1)]

    if len(other_pts) == 0:
        return 0.0

    mu = cluster_pts.mean(axis=0)
    try:
        cov = np.cov(cluster_pts.T)
        if cov.ndim == 0:
            cov = np.array([[float(cov)]])
        cov_inv = np.linalg.inv(cov + 1e-10 * np.eye(len(mu)))
    except np.linalg.LinAlgError:
        return np.nan

    d = other_pts - mu
    d2 = np.einsum('ij,jk,ik->i', d, cov_inv, d)
    l_val = np.sum(1.0 - chi2.cdf(d2, df=n_dims)) / n_c
    return float(l_val)


def compute_metrics(fet_path: str, clu_path: str):
    """
    Return a dict of scalar metrics for one KlustaKwik run.
    Uses only spatial features (all dims except the last timestamp column).
    """
    features_all, n_dims = read_fet(fet_path)
    labels = read_clu(clu_path)

    if len(labels) != len(features_all):
        msg = (f'spike count mismatch: clu={len(labels)} fet={len(features_all)}'
               f' (fet_dims={n_dims})')
        return dict(
            n_spikes     = len(labels),
            n_clusters   = 0,
            n_noise      = 0,
            n_artefact   = 0,
            noise_frac   = np.nan,
            iso_dist_mean = np.nan,
            iso_dist_med  = np.nan,
            iso_dist_min  = np.nan,
            l_ratio_mean  = np.nan,
            l_ratio_med   = np.nan,
            log10_lr_mean = np.nan,
            error        = msg,
        )

    # Drop timestamp (last column) for distance metrics
    features = features_all[:, :-1].astype(np.float64)

    cluster_ids = sorted(set(labels.tolist()))
    # Exclude noise (1) and artefact (0)
    real_clusters = [c for c in cluster_ids if c > 1]
    n_real = len(real_clusters)

    iso_vals, lr_vals = [], []
    for cid in real_clusters:
        iso = isolation_distance(features, labels, cid)
        lr  = l_ratio(features, labels, cid)
        if not np.isnan(iso):
            iso_vals.append(iso)
        if not np.isnan(lr):
            lr_vals.append(lr)

    n_noise    = int((labels == 1).sum())
    n_artefact = int((labels == 0).sum())
    noise_frac = n_noise / max(len(labels), 1)

    return dict(
        n_spikes     = len(labels),
        n_clusters   = n_real,
        n_noise      = n_noise,
        n_artefact   = n_artefact,
        noise_frac   = round(noise_frac, 4),
        iso_dist_mean = round(float(np.nanmean(iso_vals)), 2) if iso_vals else np.nan,
        iso_dist_med  = round(float(np.nanmedian(iso_vals)), 2) if iso_vals else np.nan,
        iso_dist_min  = round(float(np.nanmin(iso_vals)), 2) if iso_vals else np.nan,
        l_ratio_mean  = round(float(np.nanmean(lr_vals)), 4) if lr_vals else np.nan,
        l_ratio_med   = round(float(np.nanmedian(lr_vals)), 4) if lr_vals else np.nan,
        log10_lr_mean = round(float(np.log10(np.nanmean(lr_vals) + 1e-12)), 3) if lr_vals else np.nan,
    )


# ── Heatmap plotting ─────────────────────────────────────────────────────────

def plot_heatmap(matrix, row_labels, col_labels, title, xlabel, ylabel,
                 fmt='.1f', cmap='viridis', out_path=None,
                 vmin=None, vmax=None, annot_size=9):
    fig, ax = plt.subplots(figsize=(7, 5))
    im = ax.imshow(matrix, cmap=cmap, aspect='auto', vmin=vmin, vmax=vmax)
    plt.colorbar(im, ax=ax, shrink=0.8)

    ax.set_xticks(range(len(col_labels)))
    ax.set_yticks(range(len(row_labels)))
    ax.set_xticklabels([str(c) for c in col_labels])
    ax.set_yticklabels([str(r) for r in row_labels])
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title)

    # Annotate cells
    for i in range(matrix.shape[0]):
        for j in range(matrix.shape[1]):
            v = matrix[i, j]
            if np.isnan(v):
                text = 'N/A'
            else:
                text = format(v, fmt)
            ax.text(j, i, text, ha='center', va='center',
                    fontsize=annot_size, color='white' if v < (matrix[~np.isnan(matrix)].mean() if matrix[~np.isnan(matrix)].size else 0) else 'black')

    plt.tight_layout()
    if out_path:
        fig.savefig(out_path, dpi=150)
        plt.close(fig)
        print(f'  Saved: {out_path}')
    return fig


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)

    data_dir  = sys.argv[1]
    filebase  = sys.argv[2]
    elec      = sys.argv[3]
    sweep_dir = os.path.join(data_dir, 'sweep_tier1')

    MT_VALUES = [30, 36, 43, 51, 65]
    PM_VALUES = [0.0, 0.25, 0.5, 0.75, 1.0]

    FET_PATH = os.path.join(data_dir, f'{filebase}.fet.{elec}')
    if not os.path.exists(FET_PATH):
        print(f'ERROR: {FET_PATH} not found')
        sys.exit(1)

    records = []
    print(f'\nEvaluating sweep runs in {sweep_dir}\n')

    for mt in MT_VALUES:
        for pm in PM_VALUES:
            tag     = f'mt{mt}_pm{pm}'
            run_dir = os.path.join(sweep_dir, tag)
            clu_path = os.path.join(run_dir, f'{filebase}.clu.{elec}')

            if not os.path.exists(clu_path):
                print(f'  MISSING: {tag}')
                rec = dict(mt=mt, pm=pm, tag=tag, n_clusters=np.nan,
                           error='no clu file')
            elif os.path.islink(clu_path) and os.path.realpath(clu_path) == os.path.realpath(FET_PATH).replace('.fet.', '.clu.'):
                # Symlink still points at DATA_DIR — KK did not write its own output.
                # This happens when sweep_tier1.sh symlinked .clu before the run
                # (old bug) and KK overwrote DATA_DIR/filebase.clu.elec through it.
                print(f'  SYMLINK {tag}: .clu is a symlink to DATA_DIR — KK output missing')
                rec = dict(mt=mt, pm=pm, tag=tag, n_clusters=np.nan,
                           error='clu is symlink to data_dir (sweep bug)')
            else:
                try:
                    m = compute_metrics(FET_PATH, clu_path)
                    m['mt']  = mt
                    m['pm']  = pm
                    m['tag'] = tag
                    rec = m
                except Exception as e:
                    print(f'  ERROR {tag}: {e}')
                    rec = dict(mt=mt, pm=pm, tag=tag, n_clusters=np.nan,
                               error=str(e))
                else:
                    if 'error' in m:
                        print(f'  WARN  {tag}: {m["error"]}')
                    else:
                        print(f'  {tag}: {m["n_clusters"]} clusters, '
                              f'iso={m["iso_dist_mean"]}, '
                              f'log10(Lr)={m["log10_lr_mean"]}')
            records.append(rec)

    df = pd.DataFrame(records)
    csv_path = os.path.join(sweep_dir, 'results.csv')
    df.to_csv(csv_path, index=False)
    print(f'\nResults saved to {csv_path}')

    # ── Build matrices for heatmaps
    def make_matrix(col):
        M = np.full((len(MT_VALUES), len(PM_VALUES)), np.nan)
        for i, mt in enumerate(MT_VALUES):
            for j, pm in enumerate(PM_VALUES):
                row = df[(df.mt == mt) & (df.pm == pm)]
                if not row.empty and col in row.columns:
                    v = row[col].values[0]
                    M[i, j] = float(v) if pd.notna(v) else np.nan
        return M

    print('\nGenerating heatmaps...')

    plot_heatmap(
        make_matrix('n_clusters'),
        MT_VALUES, PM_VALUES,
        title=f'N clusters  ({filebase}.{elec})',
        xlabel='PenaltyMix  (0=AIC → 1=BIC)',
        ylabel='MergeThresh  (chi² threshold)',
        fmt='.0f', cmap='YlOrRd',
        out_path=os.path.join(sweep_dir, 'heatmap_nclusters.png')
    )

    plot_heatmap(
        make_matrix('iso_dist_mean'),
        MT_VALUES, PM_VALUES,
        title=f'Mean Isolation Distance  ({filebase}.{elec})\n(higher = better separation)',
        xlabel='PenaltyMix  (0=AIC → 1=BIC)',
        ylabel='MergeThresh',
        fmt='.1f', cmap='viridis',
        out_path=os.path.join(sweep_dir, 'heatmap_isodist.png')
    )

    plot_heatmap(
        make_matrix('log10_lr_mean'),
        MT_VALUES, PM_VALUES,
        title=f'Mean log₁₀(L-ratio)  ({filebase}.{elec})\n(lower = better isolation)',
        xlabel='PenaltyMix  (0=AIC → 1=BIC)',
        ylabel='MergeThresh',
        fmt='.2f', cmap='viridis_r',
        out_path=os.path.join(sweep_dir, 'heatmap_lratio.png')
    )

    # Composite: normalise iso (higher→1) and l-ratio (lower→1), average
    iso_m  = make_matrix('iso_dist_mean')
    lr_m   = make_matrix('log10_lr_mean')
    nc_m   = make_matrix('n_clusters')

    def norm01(M, flip=False):
        valid = M[~np.isnan(M)]
        if len(valid) == 0:
            return M
        lo, hi = valid.min(), valid.max()
        if hi == lo:
            return np.where(np.isnan(M), np.nan, 0.5)
        out = (M - lo) / (hi - lo)
        return 1 - out if flip else out

    # composite = 0.5 * norm(iso) + 0.5 * norm(1/lr)  (both higher = better)
    composite = 0.5 * norm01(iso_m) + 0.5 * norm01(lr_m, flip=True)

    plot_heatmap(
        composite,
        MT_VALUES, PM_VALUES,
        title=f'Composite quality score  ({filebase}.{elec})\n(higher = better: 0.5×iso + 0.5×(1-Lr), both normalised)',
        xlabel='PenaltyMix  (0=AIC → 1=BIC)',
        ylabel='MergeThresh',
        fmt='.2f', cmap='RdYlGn',
        out_path=os.path.join(sweep_dir, 'heatmap_composite.png')
    )

    # ── Print best runs
    df_valid = df[df.n_clusters.notna() & (df.n_clusters > 1)].copy()
    if not df_valid.empty and 'iso_dist_mean' in df_valid:
        df_valid = df_valid.dropna(subset=['iso_dist_mean', 'log10_lr_mean'])
        if not df_valid.empty:
            df_valid['composite'] = (
                (df_valid.iso_dist_mean - df_valid.iso_dist_mean.min()) /
                (df_valid.iso_dist_mean.max() - df_valid.iso_dist_mean.min() + 1e-9)
                +
                (df_valid.log10_lr_mean.max() - df_valid.log10_lr_mean) /
                (df_valid.log10_lr_mean.max() - df_valid.log10_lr_mean.min() + 1e-9)
            ) / 2
            best = df_valid.sort_values('composite', ascending=False).head(5)
            print('\n── Top 5 runs by composite score ──────────────────────')
            print(best[['tag', 'n_clusters', 'iso_dist_mean',
                         'log10_lr_mean', 'noise_frac', 'composite']].to_string(index=False))

    print('\nDone.')


if __name__ == '__main__':
    main()
