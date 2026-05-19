#!/usr/bin/env python3
"""
cluster_merge_recommend.py — post-CEM merge recommendations from the
cluster_waveform_stats NPZ.

Two distinct merge classes discriminated by temporal overlap:

  DRIFT-MERGE  (IoU < 0.1, same waveform):
      Same biological unit, different drift positions across time chunks.
      The cross-chunk-merge step in the sorter missed this because the
      Mahalanobis test in PCA space dilutes the discriminating signal.

  OVER-SPLIT MERGE  (IoU > 0.7, same waveform):
      Same biological unit, same time chunk, was split into two
      sub-populations by the per-cluster CEM.  Rare but real.

Both classes legitimately merge, but with DIFFERENT criteria — drift
merges accept high IoU=0 with cosW > 0.95; over-split merges demand
stricter cosW > 0.98 and clean combined ISI.

CRITERION TABLE (all configured via CLI flags):

  C1   cosine(template)         > threshold        (default 0.95)
  C2   cosine(per-channel ptp)  > threshold        (default 0.95)
  C3   same dominant channel    (strict)
  C4   xcorr at ±2 samples      > threshold        (default 0.95)
  C5   all α_c > 0              (sign concordance)
  C6   max(α_c) / min(α_c)      < threshold        (default 5)
  C7   α-profile smoothness     < threshold        (default 2.5)
  C8   centroid drift           < threshold        (default 40 µm)
  C9   temporal IoU             classify as DRIFT (<0.1) / SPLIT (>0.7)
                                or REVIEW (0.1..0.7)

Auto-accept tiers:
  drift-tier:    C1+C2+C3+C4+C5+C6+C7+C8+(C9 says drift)
  oversplit-tier: stricter cosW > 0.98 + IoU > 0.7 + xcorr > 0.98

Outputs:
  <session>.clu.<group>.merged.rec  — binary .clu with auto-accepted
                                      merges applied (union-find)
  <session>.merge_recommendations.csv  — every candidate pair (cosW > 0.85)
                                      with all per-criterion scores
                                      and final tier classification

USAGE:
  python3 cluster_merge_recommend.py SESSION GROUP \\
      --npz <session>.cluster_waveforms.g<group>.npz \\
      [--y-spacing 20]      # µm; used when probe geometry not in NPZ
"""

import argparse
import csv
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from footprint_drift_diagnostic import read_clu
except ImportError:
    sys.stderr.write("ERROR: place next to footprint_drift_diagnostic.py\n")
    sys.exit(1)


# ─── per-criterion helpers ───────────────────────────────────────────────


def cosine_full(M, eps=1e-9):
    """(K, T*C) → (K, K) cosine matrix.  Diagonal set to −1."""
    Mn = M / (np.linalg.norm(M, axis=1, keepdims=True) + eps)
    out = Mn @ Mn.T
    np.fill_diagonal(out, -1.0)
    return out


def cosine_footprint(P, eps=1e-9):
    """Per-channel ptp footprint cosine.  P: (K, C). Returns (K, K)."""
    Pn = P / (np.linalg.norm(P, axis=1, keepdims=True) + eps)
    out = Pn @ Pn.T
    np.fill_diagonal(out, -1.0)
    return out


def xcorr_peak_at_shifts(means_TC, max_shift=2, eps=1e-9):
    """For each cluster pair, maximum cosine over CIRCULAR shifts of the
    time axis in [−max_shift, max_shift] samples.

    Circular (i.e., samples falling off one end wrap to the other) for
    consistency with Phase 5 of the chunked-CEM pipeline (within-chunk
    circular xcorr template matching).  For 32-sample windows with the
    spike centred at peak_sample=16 and the spike fully contained in
    samples ~13..26, the ±2 wraparound only involves baseline-noise
    samples and produces results numerically very close to the linear
    (truncating) variant, but the circular convention matches the
    sorter's own merge-test machinery exactly.

    means_TC: (K, T, C) → returns (K, K).  Diagonal set to −1.
    """
    K, T, C = means_TC.shape
    a = means_TC.reshape(K, -1)
    an = a / (np.linalg.norm(a, axis=1, keepdims=True) + eps)
    out = np.full((K, K), -1.0, dtype=np.float32)
    for s in range(-max_shift, max_shift + 1):
        # Shift only the time axis (axis=1), keep channel axis intact
        b_shifted = np.roll(means_TC, s, axis=1).reshape(K, -1)
        bn = b_shifted / (np.linalg.norm(b_shifted, axis=1, keepdims=True) + eps)
        out = np.maximum(out, an @ bn.T)
    np.fill_diagonal(out, -1.0)
    return out


def per_channel_alpha(mA_TC, mB_TC, peak_slice):
    """Per-channel scaling factor α_c from cluster A's mean to B's,
    estimated by least-squares on the peak window."""
    C = mA_TC.shape[1]
    α = np.zeros(C, dtype=np.float64)
    for c in range(C):
        a = mA_TC[peak_slice, c]; b = mB_TC[peak_slice, c]
        α[c] = np.dot(a, b) / (np.dot(a, a) + 1e-12)
    return α


def smoothness_metric(α, y_pos):
    """Total variation of α sorted by physical y, normalized by mean |α|."""
    order = np.argsort(y_pos)
    α_sorted = α[order]
    tv = np.abs(np.diff(α_sorted)).sum()
    return float(tv / (np.abs(α_sorted).mean() + 1e-9))


def centroid_drift(ptp_A, ptp_B, y_pos):
    """Centroid of ptp² distribution — estimated source y-position.
    Drift = difference between A's centroid and B's, in µm."""
    wA = (ptp_A / (ptp_A.max() + 1e-9)) ** 2
    wB = (ptp_B / (ptp_B.max() + 1e-9)) ** 2
    yA = float((wA * y_pos).sum() / wA.sum())
    yB = float((wB * y_pos).sum() / wB.sum())
    return abs(yB - yA)


def temporal_iou(tmin_i, tmax_i, tmin_j, tmax_j):
    """Jaccard-style overlap of [tmin, tmax] intervals."""
    inter = max(0.0, min(tmax_i, tmax_j) - max(tmin_i, tmin_j))
    union = max(tmax_i, tmax_j) - min(tmin_i, tmin_j)
    return inter / union if union > 0 else 0.0


# ─── pair scoring + tier classification ──────────────────────────────────


def score_pair(i, j, *, M3, P, ids, nspikes_idx, ch_dom, y_um,
               peak_slice, cosW, cosFP, xcc, tmin, tmax, tmean,
               have_time, signal_ptp_frac):
    """Compute every per-pair criterion.  Returns a dict OR None if
    the pair has no signal channels in common (should be filtered)."""
    α = per_channel_alpha(M3[i], M3[j], peak_slice)
    # Signal mask: only channels with significant ptp in BOTH clusters
    # contribute to α-structure tests.  Noise channels give random α.
    max_ptp_pair = max(P[i].max(), P[j].max())
    signal_mask = np.minimum(P[i], P[j]) >= signal_ptp_frac * max_ptp_pair
    n_signal = int(signal_mask.sum())
    if n_signal == 0:
        return None

    α_signal = α[signal_mask]
    alpha_all_pos = bool(np.all(α_signal > 0))
    if α_signal.min() > 0:
        spread = float(α_signal.max() / α_signal.min())
    else:
        spread = float("inf")

    # Smoothness needs ≥3 signal channels to define a spatial profile.
    if n_signal >= 3:
        smooth = smoothness_metric(α[signal_mask], y_um[signal_mask])
    else:
        smooth = 0.0  # too sparse to test — don't penalise

    drift_um = centroid_drift(P[i], P[j], y_um)
    iou = (temporal_iou(tmin[i], tmax[i], tmin[j], tmax[j])
           if have_time else float("nan"))
    tgap = (abs(float(tmean[i]) - float(tmean[j]))
            if have_time else float("nan"))
    same_dom = bool(ch_dom[i] == ch_dom[j])

    return {
        "cid_A": int(ids[i]), "cid_B": int(ids[j]),
        "nspk_A": int(nspikes_idx[i]), "nspk_B": int(nspikes_idx[j]),
        "cosW": float(cosW[i, j]),
        "cosFP": float(cosFP[i, j]),
        "xcorr": float(xcc[i, j]),
        "same_dom_ch": int(same_dom),
        "n_signal_ch": n_signal,
        "alpha_min_signal": float(α_signal.min()),
        "alpha_max_signal": float(α_signal.max()),
        "alpha_spread": spread,
        "alpha_all_pos": int(alpha_all_pos),
        "smoothness": float(smooth),
        "drift_um": float(drift_um),
        "t_iou": float(iou),
        "t_gap_s": float(tgap),
    }


def classify_tier(scores, args, have_time):
    """Tier the pair given its scores + thresholds.

    Tiers:
      AUTO_DRIFT           same unit at different chunks (waveform good, IoU low)
      AUTO_OVERSPLIT       same unit in same chunk over-split (stricter waveform,
                           full structural checks, high IoU)
      REVIEW_PARTIAL_OVERLAP  base waveform/structure pass but IoU is
                              borderline (0.1..0.7) — manual decision
      REVIEW               at least one structural check failed
    """
    # Base structural pass: pair survives all the geometry- and waveform-
    # consistency checks at the default thresholds.  BOTH tiers require it.
    base_pass = (
        scores["cosW"]         >= args.cosw_thresh and
        scores["cosFP"]        >= args.cosfp_thresh and
        scores["same_dom_ch"]  == 1 and
        scores["xcorr"]        >= args.xcorr_thresh and
        scores["alpha_all_pos"] == 1 and
        scores["alpha_spread"] <  args.alpha_spread_max and
        scores["smoothness"]   <  args.smoothness_max and
        scores["drift_um"]     <  args.drift_max_um
    )
    if not base_pass:
        return "REVIEW"

    if not have_time:
        return "AUTO_DRIFT"   # no temporal info: treat as drift-style merge

    iou = scores["t_iou"]
    if iou < args.drift_iou_max:
        return "AUTO_DRIFT"

    # AUTO_OVERSPLIT also requires base_pass AND stricter waveform AND high IoU
    if (iou >= args.oversplit_iou_min and
            scores["cosW"]  >= args.oversplit_cosw and
            scores["xcorr"] >= args.oversplit_cosw):
        return "AUTO_OVERSPLIT"

    return "REVIEW_PARTIAL_OVERLAP"


# ─── union-find for transitive merges ────────────────────────────────────


class UnionFind:
    def __init__(self, items):
        self.parent = {x: x for x in items}
    def find(self, x):
        while self.parent[x] != x:
            self.parent[x] = self.parent[self.parent[x]]
            x = self.parent[x]
        return x
    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.parent[rb] = ra
    def groups(self):
        out = {}
        for x in self.parent:
            r = self.find(x)
            out.setdefault(r, []).append(x)
        return out


# ─── main ────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path)
    ap.add_argument("group", type=int)
    ap.add_argument("--npz", type=Path, required=True,
                    help="cluster_waveforms NPZ from cluster_waveform_stats.py")
    ap.add_argument("--output-clu", type=Path, default=None,
                    help="Output .clu.N.rec path (default: <session>.clu.<group>.merged.rec)")
    ap.add_argument("--output-csv", type=Path, default=None,
                    help="Output CSV (default: <session>.merge_recommendations.g<group>.csv)")
    # Thresholds
    ap.add_argument("--cosw-thresh", type=float, default=0.95)
    ap.add_argument("--cosfp-thresh", type=float, default=0.95)
    ap.add_argument("--xcorr-thresh", type=float, default=0.95)
    ap.add_argument("--alpha-spread-max", type=float, default=5.0)
    ap.add_argument("--smoothness-max", type=float, default=2.5)
    ap.add_argument("--drift-max-um", type=float, default=40.0)
    ap.add_argument("--drift-iou-max", type=float, default=0.1)
    ap.add_argument("--oversplit-cosw", type=float, default=0.98)
    ap.add_argument("--oversplit-iou-min", type=float, default=0.7)
    ap.add_argument("--candidate-cosw", type=float, default=0.85,
                    help="cosW > this for any pair to enter the CSV")
    ap.add_argument("--peak-window", type=int, default=3,
                    help="half-window around peak_sample for α and kurt analysis")
    ap.add_argument("--signal-ptp-frac", type=float, default=0.3,
                    help="channel counts as 'signal' if its ptp ≥ this "
                         "fraction of the pair's max ptp (default 0.3)")
    # Filters on which clusters to consider
    ap.add_argument("--min-spikes", type=int, default=50)
    ap.add_argument("--min-ptp", type=float, default=1000.0)
    # Geometry fallback
    ap.add_argument("--y-spacing", type=float, default=None,
                    help="µm vertical spacing if NPZ has no probe geometry "
                         "(falls back to channel-index ordering with warning)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    print(f"cluster_merge_recommend — {args.session.name} group {args.group}")

    # Load NPZ
    d = np.load(args.npz, allow_pickle=True)
    means = d["means"]            # (T, C, K)
    stds  = d["stds"]
    ptp_mean = d["ptp_mean"]      # (C, K)
    clusters = d["clusters"]      # (K,) cluster IDs
    nspikes = d["nspikes"]
    T, C, K_all = means.shape
    peak_sample = int(d["peak_sample"])
    sampling_rate = float(d["sampling_rate"])
    print(f"  NPZ: K={K_all}, T={T} samples, C={C} channels, "
          f"sr={sampling_rate:.0f} Hz, peak at sample {peak_sample}")

    # Time stats (added by latest cluster_waveform_stats.py)
    t_min = d["t_min_s"] if "t_min_s" in d.files else None
    t_max = d["t_max_s"] if "t_max_s" in d.files else None
    t_mean = d["t_mean_s"] if "t_mean_s" in d.files else None
    have_time = t_min is not None
    if have_time:
        rec_span = float(t_max.max() - t_min.min())
        print(f"  time stats: recording span {rec_span:.0f} s, "
              f"median cluster range {float(np.median(t_max - t_min)):.0f} s")
    else:
        print(f"  WARNING: no time stats in NPZ (older version) — "
              f"temporal-overlap criterion DISABLED")

    # Geometry (x_um, y_um) or fallback
    x_um = d["x_um"] if "x_um" in d.files else np.full(C, np.nan, np.float32)
    y_um = d["y_um"] if "y_um" in d.files else np.full(C, np.nan, np.float32)
    geometry_available = not np.any(np.isnan(y_um))
    if geometry_available:
        print(f"  geometry: y span {float(np.nanmax(y_um) - np.nanmin(y_um)):.0f} µm "
              f"(probe geometry from NPZ)")
    elif args.y_spacing is not None:
        y_um = (np.arange(C) * args.y_spacing).astype(np.float32)
        x_um = np.zeros(C, dtype=np.float32)
        print(f"  geometry: --y-spacing {args.y_spacing} µm fallback "
              f"(NPZ had NaN coords)")
    else:
        # last resort: use channel index, warn user
        y_um = np.arange(C, dtype=np.float32)
        x_um = np.zeros(C, dtype=np.float32)
        print(f"  WARNING: no geometry in NPZ and --y-spacing not given.  "
              f"Using channel index as y proxy — geometry-aware tests "
              f"(smoothness, centroid drift) will be qualitatively right "
              f"but absolute thresholds may need rescaling.")

    # Filter to well-isolated, reasonably-sized clusters
    ptp_max = ptp_mean.max(axis=0)
    keep = (clusters > 1) & (nspikes >= args.min_spikes) & (ptp_max >= args.min_ptp)
    idx = np.flatnonzero(keep)
    n_keep = len(idx)
    print(f"  filtered: {n_keep}/{K_all} clusters (id>1, nspk≥{args.min_spikes}, "
          f"max_ptp≥{args.min_ptp:.0f})")

    # Reshape per-cluster arrays for the selected subset
    M3 = means.transpose(2, 0, 1)[idx]              # (n, T, C)
    M  = M3.reshape(n_keep, T * C)
    P  = ptp_mean.T[idx]                            # (n, C)
    ids = clusters[idx]
    if have_time:
        tmin = t_min[idx]; tmax = t_max[idx]; tmean = t_mean[idx]
    ch_dom = np.argmax(P, axis=1)
    peak_slice = slice(max(0, peak_sample - args.peak_window),
                        min(T, peak_sample + args.peak_window + 1))

    # Pairwise matrices: cos templates, cos footprints, xcorr
    print("  computing pairwise cosine + xcorr ...")
    cosW = cosine_full(M)
    cosFP = cosine_footprint(P)
    xcc = xcorr_peak_at_shifts(M3, max_shift=2)

    # Iterate candidate pairs (cosW > candidate threshold).  Per-pair
    # scoring + tier classification factored into score_pair / classify_tier.
    print(f"  finding candidate pairs (cosW > {args.candidate_cosw}) ...")
    pair_data = []
    nspikes_idx = nspikes[idx]
    for i in range(n_keep):
        for j in range(i + 1, n_keep):
            if cosW[i, j] < args.candidate_cosw:
                continue
            scores = score_pair(
                i, j,
                M3=M3, P=P, ids=ids, nspikes_idx=nspikes_idx,
                ch_dom=ch_dom, y_um=y_um, peak_slice=peak_slice,
                cosW=cosW, cosFP=cosFP, xcc=xcc,
                tmin=tmin if have_time else None,
                tmax=tmax if have_time else None,
                tmean=tmean if have_time else None,
                have_time=have_time,
                signal_ptp_frac=args.signal_ptp_frac,
            )
            if scores is None:
                continue
            scores["tier"] = classify_tier(scores, args, have_time)
            pair_data.append(scores)

    print(f"  {len(pair_data)} candidate pairs (cosW > {args.candidate_cosw})")
    tier_counts = {}
    for p in pair_data:
        tier_counts[p["tier"]] = tier_counts.get(p["tier"], 0) + 1
    for t in ("AUTO_DRIFT", "AUTO_OVERSPLIT", "REVIEW_PARTIAL_OVERLAP", "REVIEW"):
        if t in tier_counts:
            print(f"     {t:<24s} {tier_counts[t]:>5d}")

    # Apply auto-accepted merges via union-find
    accepted_pairs = [(p["cid_A"], p["cid_B"]) for p in pair_data
                      if p["tier"] in ("AUTO_DRIFT", "AUTO_OVERSPLIT")]
    print(f"\n  applying {len(accepted_pairs)} auto-accepted merges (union-find)...")

    clu_orig = read_clu(args.session, args.group).astype(np.int32)
    all_cids = np.unique(clu_orig).tolist()
    uf = UnionFind(all_cids)
    for a, b in accepted_pairs:
        uf.union(a, b)

    # Build remap: each cluster → its root (smallest ID in its group)
    groups = uf.groups()
    # Choose label per group = smallest member, preserving id 0 / 1 as roots
    remap = {}
    for root, members in groups.items():
        label = min(members)
        for m in members:
            remap[m] = label
    # Build merge counts log
    merge_summary = [(min(g), len(g), sorted(g)) for g in groups.values() if len(g) > 1]
    merge_summary.sort()
    n_merged = sum(len(g) - 1 for g in groups.values() if len(g) > 1)
    n_groups_changed = sum(1 for g in groups.values() if len(g) > 1)
    print(f"  cluster count: {len(all_cids)} → {len(set(remap.values()))} "
          f"({n_groups_changed} groups merged from {n_groups_changed + n_merged} clusters)")
    for label, sz, members in merge_summary[:20]:
        print(f"    {label} ← {members}")
    if len(merge_summary) > 20:
        print(f"    ... and {len(merge_summary) - 20} more merge groups")

    clu_new = np.fromiter((remap.get(c, c) for c in clu_orig.tolist()),
                           dtype=np.int32, count=len(clu_orig))

    # Write .clu.N.merged.rec
    if not args.dry_run:
        if args.output_clu is None:
            out_clu = Path(f"{args.session}.clu.{args.group}.merged.rec")
        else:
            out_clu = args.output_clu
        n_clusters_new = int(np.unique(clu_new).size)
        with open(out_clu, "wb") as f:
            np.array([n_clusters_new], dtype=np.int32).tofile(f)
            clu_new.astype(np.int32).tofile(f)
        print(f"\nWrote {out_clu}")
        print(f"  {n_clusters_new} distinct cluster IDs, {len(clu_new)} spike labels")

    # Write CSV of all candidate pairs with all scores
    if not args.dry_run:
        if args.output_csv is None:
            out_csv = Path(f"{args.session}.merge_recommendations.g{args.group}.csv")
        else:
            out_csv = args.output_csv
        # Rank: auto-tiers first, then by combined score (cosW × cosFP × xcorr)
        for p in pair_data:
            p["_rank_score"] = p["cosW"] * p["cosFP"] * p["xcorr"]
        pair_data.sort(key=lambda p: (p["tier"] != "AUTO_DRIFT",
                                       p["tier"] != "AUTO_OVERSPLIT",
                                       -p["_rank_score"]))
        fieldnames = [k for k in pair_data[0].keys() if not k.startswith("_")] \
            if pair_data else []
        with open(out_csv, "w", newline="") as f:
            if fieldnames:
                w = csv.DictWriter(f, fieldnames=fieldnames)
                w.writeheader()
                for p in pair_data:
                    w.writerow({k: p[k] for k in fieldnames})
        print(f"Wrote {out_csv}")
        print(f"  {len(pair_data)} pairs, sorted by tier then composite score")

    print("\nDone.")


if __name__ == "__main__":
    main()
