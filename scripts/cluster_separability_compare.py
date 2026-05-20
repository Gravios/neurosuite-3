#!/usr/bin/env python3
"""
cluster_separability_compare.py — compare waveform-space discriminability
between two cluster_waveforms NPZ snapshots.

Use case: after Klusters' batch realign (or any preprocessing step), did
the per-cluster signal-to-noise improve?  Did clusters become more
separable from their nearest neighbours?

CAVEAT: This is a population-level diagnostic.  If the two snapshots have
different cluster memberships (merges happened in between), the comparison
isn't a clean A/B test — it conflates "did realignment help" with
"did merges help".  Confidence is highest when the two snapshots share
the same .clu (only spike alignment changed).

METRICS reported, computed entirely from the means + stds + ptp_mean in
the NPZ (no .fet needed):

1. F-ratio (Fisher-like discriminability)
   F = between-cluster variance / pooled within-cluster variance
   Higher F = clusters are more spread apart relative to their internal
   spread.  Computed on each cluster's mean waveform vs its std envelope.

2. Per-cluster total within-cluster spread (sum-of-stds²)
   How tight is each cluster in waveform space?  Distribution shift
   tells us whether realignment is uniformly tightening or having
   mixed effects.

3. Pairwise centroid separability
   For each cluster, the L2 distance from its mean waveform to the
   NEAREST other cluster's mean, normalised by its own internal spread.
   Distribution shift reveals whether typical clusters are getting
   closer/farther from their nearest neighbours.

USAGE:
  python3 cluster_separability_compare.py PRE_NPZ POST_NPZ [--top-channels N]

  --top-channels: limit metric computation to the N strongest channels
                  per cluster (default 4 for V-probe; 1 for tetrode-like
                  sparse coverage).  Reduces noise-channel contribution
                  to the within-cluster variance.
"""

import argparse
import sys
from pathlib import Path

import numpy as np


def per_cluster_stats(npz_path: Path, top_channels: int):
    """Load NPZ, return per-cluster summary stats.

    Returns dict with arrays of shape (K,) unless noted:
      cid                 — cluster IDs
      nspk                — spike counts
      dom_ch              — dominant channel index
      peak_per            — empirical trough sample (per cluster)
      tws                 — total within-cluster spread, Σ stds² over the
                            top_channels strongest channels, in the peak
                            ± 6 sample window (so we measure spread of
                            the spike body, not the noise floor)
      max_ptp             — peak ptp value
      mean_flat           — (K, T*top_channels) flattened mean waveforms
                            on top channels, for pairwise distance
    """
    d = np.load(npz_path, allow_pickle=True)
    means = d['means']            # (T, C, K)
    stds  = d['stds']             # (T, C, K)
    ptp   = d['ptp_mean']         # (C, K)
    cid   = d['clusters']
    nspk  = d['nspikes']
    peak_global = int(d['peak_sample'])
    T, C, K = means.shape

    # Per-cluster trough
    if 'peak_sample_per_cluster' in d:
        peak_per = d['peak_sample_per_cluster'].astype(np.int64)
    else:
        peak_per = np.full(K, peak_global, dtype=np.int64)

    dom_ch = np.argmax(ptp, axis=0)
    max_ptp = ptp.max(axis=0)

    # Per-cluster top-channels by ptp
    top_channels = min(top_channels, C)
    top_ch_per = np.argsort(-ptp, axis=0)[:top_channels, :].T   # (K, top_channels)

    # Total within-cluster spread on top channels, in spike-body window
    halfwin = 6
    tws = np.zeros(K)
    mean_flat = np.zeros((K, T * top_channels))
    for k in range(K):
        t_lo = max(0, int(peak_per[k]) - halfwin)
        t_hi = min(T, int(peak_per[k]) + halfwin + 1)
        chs = top_ch_per[k]
        s_sq = stds[t_lo:t_hi, :, k][:, chs] ** 2     # (window, top)
        tws[k] = float(s_sq.sum())
        mean_flat[k] = means[:, :, k][:, chs].ravel()  # (T, top) → flat

    return dict(
        cid=cid, nspk=nspk, dom_ch=dom_ch, peak_per=peak_per,
        tws=tws, max_ptp=max_ptp, mean_flat=mean_flat,
        K=K, T=T, C=C, top_channels=top_channels,
    )


def f_ratio(S: dict, weight_by_nspk: bool = True, real_only: bool = True):
    """Fisher-like discriminability of the population.

    F = between-cluster variance / pooled within-cluster variance

    Both numerator and denominator computed on mean_flat (top-channel
    spike body), so they're directly comparable across snapshots even
    if cluster memberships differ.
    """
    cid = S['cid']; nspk = S['nspk']
    mask = (cid > 1) & (nspk >= 50) if real_only else np.ones_like(cid, bool)
    if mask.sum() < 2:
        return float('nan')
    means = S['mean_flat'][mask]             # (K_real, D)
    tws = S['tws'][mask]                     # (K_real,)
    n = nspk[mask].astype(np.float64) if weight_by_nspk else np.ones(mask.sum())

    # Global centroid
    centroid = (n[:, None] * means).sum(0) / n.sum()
    # Between-cluster: Σ n_k ||μ_k − μ_global||²
    bss = float((n * ((means - centroid) ** 2).sum(1)).sum())
    # Pooled within: Σ n_k × tws_k  (tws is Σ stds², the per-cluster spread)
    wss = float((n * tws).sum())
    return bss / max(wss, 1e-9)


def nearest_centroid_distance(S: dict, real_only: bool = True):
    """For each real cluster, distance from its mean to the nearest
    OTHER cluster's mean, normalised by sqrt(tws) (typical internal
    spread).  Larger = more separable from neighbours.

    Returns (K_real,) array.
    """
    cid = S['cid']; nspk = S['nspk']
    mask = (cid > 1) & (nspk >= 50) if real_only else np.ones_like(cid, bool)
    idx = np.flatnonzero(mask)
    if len(idx) < 2:
        return np.array([])
    M = S['mean_flat'][idx]
    tws = S['tws'][idx]
    # Pairwise squared distances
    sqd = ((M[:, None, :] - M[None, :, :]) ** 2).sum(-1)
    np.fill_diagonal(sqd, np.inf)
    nearest = np.sqrt(sqd.min(1))
    # Normalise by typical internal spread (sqrt of tws)
    own_scale = np.sqrt(np.maximum(tws, 1e-6))
    return nearest / own_scale


def summarise(label: str, S: dict, real_only: bool = True):
    """Compact one-line summary of a snapshot."""
    cid = S['cid']; nspk = S['nspk']
    mask = (cid > 1) & (nspk >= 50) if real_only else np.ones_like(cid, bool)
    n_real = int(mask.sum())
    F = f_ratio(S, real_only=real_only)
    sep = nearest_centroid_distance(S, real_only=real_only)
    tws = S['tws'][mask]
    print(f"  {label}")
    print(f"    {n_real} real clusters (nspk≥50, id>1)")
    print(f"    F-ratio (between/within):      {F:.3f}")
    print(f"    median √(within-spread):        {np.median(np.sqrt(tws)):.0f}")
    if len(sep):
        print(f"    nearest-neighbour separability (median):  {np.median(sep):.2f}")
        print(f"                                   (5/95%):  "
              f"[{np.percentile(sep, 5):.2f}, {np.percentile(sep, 95):.2f}]")
    return dict(n_real=n_real, F=F, tws=tws, sep=sep)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pre_npz", type=Path,
                    help="NPZ from BEFORE the change (baseline)")
    ap.add_argument("post_npz", type=Path,
                    help="NPZ from AFTER the change")
    ap.add_argument("--top-channels", type=int, default=4,
                    help="Restrict metric computation to the N strongest "
                         "channels per cluster (default 4)")
    ap.add_argument("--min-spikes", type=int, default=50,
                    help="Only include clusters with at least this many "
                         "spikes (default 50)")
    args = ap.parse_args()

    if not args.pre_npz.is_file():
        sys.stderr.write(f"ERROR: {args.pre_npz} not found\n"); sys.exit(1)
    if not args.post_npz.is_file():
        sys.stderr.write(f"ERROR: {args.post_npz} not found\n"); sys.exit(1)

    print(f"Loading PRE: {args.pre_npz}")
    S_pre = per_cluster_stats(args.pre_npz, args.top_channels)
    print(f"  {S_pre['K']} clusters, {S_pre['C']} channels, T={S_pre['T']}")

    print(f"Loading POST: {args.post_npz}")
    S_post = per_cluster_stats(args.post_npz, args.top_channels)
    print(f"  {S_post['K']} clusters, {S_post['C']} channels, T={S_post['T']}")

    print()
    print(f"Discriminability metrics (top-{args.top_channels} channels, "
          f"spike-body window peak±6, min spikes={args.min_spikes}):")
    res_pre  = summarise("PRE-change:",  S_pre)
    res_post = summarise("POST-change:", S_post)

    # Population shifts (paired by cluster ID where possible)
    print()
    print("Δ (POST − PRE):")
    print(f"  Δ F-ratio:                       "
          f"{res_post['F'] - res_pre['F']:+.3f}    "
          f"({'+' if res_post['F'] > res_pre['F'] else ''}"
          f"{(res_post['F']/res_pre['F']-1)*100:+.0f}%)")
    print(f"  Δ median √(within-spread):       "
          f"{np.median(np.sqrt(res_post['tws'])) - np.median(np.sqrt(res_pre['tws'])):+.0f} µV")
    if len(res_post['sep']) and len(res_pre['sep']):
        print(f"  Δ median nearest-separability:   "
              f"{np.median(res_post['sep']) - np.median(res_pre['sep']):+.2f}")

    # Paired analysis for clusters present in both snapshots
    pre_cids = {int(c): i for i, c in enumerate(S_pre['cid'])}
    post_cids = {int(c): i for i, c in enumerate(S_post['cid'])}
    shared = sorted(set(pre_cids) & set(post_cids))
    if shared:
        pre_idx  = np.array([pre_cids[c]  for c in shared])
        post_idx = np.array([post_cids[c] for c in shared])
        # Restrict to stable clusters (≥50 spikes in both, size within ±25%)
        nspk_pre  = S_pre['nspk'][pre_idx]
        nspk_post = S_post['nspk'][post_idx]
        stable = ((nspk_pre >= args.min_spikes) &
                  (nspk_post >= args.min_spikes) &
                  (nspk_post / np.maximum(nspk_pre, 1) > 0.8) &
                  (nspk_post / np.maximum(nspk_pre, 1) < 1.25))
        if stable.sum() >= 5:
            d_tws = (np.sqrt(S_post['tws'][post_idx[stable]]) -
                     np.sqrt(S_pre['tws'][pre_idx[stable]]))
            print(f"\nPaired analysis ({int(stable.sum())} clusters w/ stable "
                  f"size and IDs in both):")
            print(f"  within-spread improved (Δ < 0):  "
                  f"{int((d_tws < 0).sum())}/{int(stable.sum())} "
                  f"({(d_tws < 0).mean():.0%})")
            print(f"  median Δ √(within-spread):       "
                  f"{np.median(d_tws):+.0f} µV  "
                  f"5-95%[{np.percentile(d_tws, 5):+.0f}, "
                  f"{np.percentile(d_tws, 95):+.0f}]")
        else:
            print(f"\nPaired analysis: only {int(stable.sum())} stable pairs — "
                  f"too few for population statistics.  The two snapshots "
                  f"may have very different cluster memberships (merges, "
                  f"collision splits etc. happened between them).")

    # Interpretation guide
    print()
    print("─" * 60)
    print("Interpretation:")
    print("  F-ratio up    → clusters more distinct from each other")
    print("  within-spread down → spikes within a cluster better aligned")
    print("  nearest-sep up → typical cluster is farther from its nearest peer")
    print("  Negative changes on F-ratio with high merge count between")
    print("  snapshots usually mean: merges absorbed easy cases, leaving")
    print("  the harder ones — apparent discriminability dropped though")
    print("  realignment may still have helped individual clusters.")


if __name__ == "__main__":
    main()
