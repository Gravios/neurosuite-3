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


# ─── chunk assignment + drift coherence ──────────────────────────────────


def assign_chunks(t_means, gap_threshold_s):
    """Assign each cluster to a chunk index based on 1-D gap clustering
    of t_mean values.  Two clusters belong to the same chunk if their
    sorted t_mean values are within `gap_threshold_s` of each other.

    Returns (chunk_id_per_cluster (n,), n_chunks).
    """
    n = len(t_means)
    order = np.argsort(t_means)
    chunks = np.zeros(n, dtype=np.int32)
    if n == 0:
        return chunks, 0
    sorted_t = t_means[order]
    gaps = np.diff(sorted_t)
    # New chunk starts wherever the gap exceeds threshold.  cumsum gives
    # an ascending chunk index in sorted order.
    chunk_in_sorted = np.concatenate(([0], np.cumsum(gaps > gap_threshold_s)))
    # Invert the sort to get chunk per original index
    chunks[order] = chunk_in_sorted
    return chunks, int(chunk_in_sorted.max() + 1)


def drift_coherence_outlier_mask(pair_data, k_mad):
    """For each chunk-transition (cA, cB) with ≥3 AUTO_DRIFT pairs,
    flag pairs whose drift_um deviates >k_mad×MAD from the transition
    median.  Returns set of pair indices to demote from AUTO_DRIFT.

    Rationale: a single biological drift event affects ALL units active
    in both chunks coherently.  A pair claiming +30 µm drift when 20
    peer pairs all show +5 µm is structurally inconsistent — either
    the pair is not a real same-unit-drift (different units, contamination),
    or the chunk boundary itself is the wrong assignment.
    """
    # Group AUTO_DRIFT pair indices by their (chunk_A, chunk_B) tuple.
    by_transition = {}
    for k, p in enumerate(pair_data):
        if p["tier"] != "AUTO_DRIFT":
            continue
        # Canonical ordering of chunks so (cA, cB) and (cB, cA) collapse
        key = (min(p["chunk_A"], p["chunk_B"]),
               max(p["chunk_A"], p["chunk_B"]))
        if key[0] == key[1]:
            continue   # same-chunk AUTO_DRIFT shouldn't happen, but skip
        by_transition.setdefault(key, []).append(k)

    outliers = set()
    for trans_key, idx_list in by_transition.items():
        if len(idx_list) < 3:
            continue   # too few peers for a reliable median
        drifts = np.array([pair_data[k]["drift_um"] for k in idx_list])
        med = float(np.median(drifts))
        mad = float(np.median(np.abs(drifts - med)))
        # Floor: with very tight clusters (MAD ≈ 0), use 1 µm so we don't
        # demote pairs that are essentially on the median.
        mad_floor = max(mad, 1.0)
        for k in idx_list:
            if abs(pair_data[k]["drift_um"] - med) > k_mad * mad_floor:
                outliers.add(k)
    return outliers, by_transition


# ─── pair scoring + tier classification ──────────────────────────────────


def score_pair(i, j, *, M3, P, S_peak, ids, nspikes_idx, ch_dom, y_um,
               peak_slice, cosW, cosFP, xcc,
               tmin, tmax, tmean, chunk_id,
               have_time, signal_ptp_frac):
    """Compute every per-pair criterion.  Returns a dict OR None if
    the pair has no signal channels in common (should be filtered).

    S_peak: (n, C) per-cluster, per-channel std at peak sample.
    chunk_id: (n,) chunk index per cluster (or None if no time stats).
    """
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

    # Per-channel std ratio at peak sample — empirical variance bound.
    # Same biological unit at similar drift positions = similar SNR floor
    # = similar per-channel std.  Wildly different stds on the SAME signal
    # channel mean one cluster is contaminated.
    sA = S_peak[i, signal_mask]
    sB = S_peak[j, signal_mask]
    std_lo = np.maximum(np.minimum(sA, sB), 1e-6)
    std_hi = np.maximum(sA, sB)
    std_ratio_max = float((std_hi / std_lo).max())

    drift_um = centroid_drift(P[i], P[j], y_um)
    iou = (temporal_iou(tmin[i], tmax[i], tmin[j], tmax[j])
           if have_time else float("nan"))
    tgap = (abs(float(tmean[i]) - float(tmean[j]))
            if have_time else float("nan"))
    same_dom = bool(ch_dom[i] == ch_dom[j])

    return {
        "cid_A": int(ids[i]), "cid_B": int(ids[j]),
        "nspk_A": int(nspikes_idx[i]), "nspk_B": int(nspikes_idx[j]),
        "chunk_A": int(chunk_id[i]) if chunk_id is not None else -1,
        "chunk_B": int(chunk_id[j]) if chunk_id is not None else -1,
        "cosW": float(cosW[i, j]),
        "cosFP": float(cosFP[i, j]),
        "xcorr": float(xcc[i, j]),
        "same_dom_ch": int(same_dom),
        "n_signal_ch": n_signal,
        "alpha_min_signal": float(α_signal.min()),
        "alpha_max_signal": float(α_signal.max()),
        "alpha_spread": spread,
        "alpha_all_pos": int(alpha_all_pos),
        "std_ratio_max": std_ratio_max,
        "smoothness": float(smooth),
        "drift_um": float(drift_um),
        "t_iou": float(iou),
        "t_gap_s": float(tgap),
    }


def classify_tier(scores, args, have_time):
    """Tier the pair given its scores + thresholds.

    Tiers:
      AUTO_DRIFT           same unit at different chunks (waveform good, IoU low)
      AUTO_OVERSPLIT       same unit in same chunk over-split — STRICTER
                           than DRIFT on alpha_spread, drift_um, cosW, xcorr
                           because same time = same source position.
      REVIEW_PARTIAL_OVERLAP  base waveform/structure pass but IoU is
                              borderline (0.1..0.7) — manual decision
      REVIEW               at least one structural check failed
    """
    # Base structural pass — applies to BOTH tiers.
    base_pass = (
        scores["cosW"]          >= args.cosw_thresh and
        scores["cosFP"]         >= args.cosfp_thresh and
        scores["same_dom_ch"]   == 1 and
        scores["xcorr"]         >= args.xcorr_thresh and
        scores["alpha_all_pos"] == 1 and
        scores["alpha_spread"]  <  args.alpha_spread_max and
        scores["std_ratio_max"] <  args.std_ratio_max and
        scores["smoothness"]    <  args.smoothness_max and
        scores["drift_um"]      <  args.drift_max_um
    )
    if not base_pass:
        return "REVIEW"

    if not have_time:
        return "AUTO_DRIFT"

    iou = scores["t_iou"]
    if iou < args.drift_iou_max:
        return "AUTO_DRIFT"

    # AUTO_OVERSPLIT: stricter than base — same time = nearly identical
    # source position = drift_um near 0 and alpha_spread near 1.
    if (iou >= args.oversplit_iou_min and
            scores["cosW"]         >= args.oversplit_cosw and
            scores["xcorr"]        >= args.oversplit_cosw and
            scores["drift_um"]     <  args.oversplit_drift_um and
            scores["alpha_spread"] <  args.oversplit_alpha_spread):
        return "AUTO_OVERSPLIT"

    return "REVIEW_PARTIAL_OVERLAP"


# ─── union-find for transitive merges ────────────────────────────────────


def greedy_max_clique(members, accepted_pair_set, max_seeds=20):
    """Find a maximal clique among `members` such that every pairwise
    edge is in `accepted_pair_set` (canonical (min, max) tuples).

    Algorithm: degree-ordered greedy expansion with multi-seed search.
    For each of the top-`max_seeds` highest-degree members, start a
    clique and iteratively add every other member that is connected to
    ALL current clique members.  Return the largest clique found across
    all seed attempts.

    Maximum clique is NP-hard in general, but for the sizes we see
    (rejected groups of 4-100 clusters with sparse internal AUTO_*
    edges), this heuristic finds the global optimum or close to it.

    Used to RECOVER valid sub-merges from groups that fail strict
    complete-link verification.  Without this, a single-link chain
    bridging 100 similar clusters loses every internal valid drift
    sub-sequence (typically 3-10 clusters each).
    """
    members = sorted(members)
    n = len(members)
    if n < 2:
        return set()

    # Build adjacency restricted to AUTO_* edges within the group
    adj = {m: set() for m in members}
    for i, a in enumerate(members):
        for b in members[i + 1:]:
            if (a, b) in accepted_pair_set:
                adj[a].add(b)
                adj[b].add(a)

    # Seed candidates: highest internal degree first
    by_degree = sorted(members, key=lambda m: -len(adj[m]))

    best = set()
    for seed in by_degree[:min(max_seeds, n)]:
        clique = {seed}
        # Try to add every other member, in degree order
        for cand in by_degree:
            if cand == seed or cand in clique:
                continue
            # cand joins iff connected to ALL current clique members
            if clique.issubset(adj[cand]):
                clique.add(cand)
        if len(clique) > len(best):
            best = clique
    return best


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
    ap.add_argument("--xcorr-thresh", type=float, default=0.97,
                    help="xcorr ≥ cosW by construction (max over shifts), "
                         "so threshold must exceed cosw-thresh to add info")
    ap.add_argument("--alpha-spread-max", type=float, default=5.0)
    ap.add_argument("--smoothness-max", type=float, default=2.5)
    ap.add_argument("--drift-max-um", type=float, default=40.0)
    ap.add_argument("--drift-iou-max", type=float, default=0.1)
    ap.add_argument("--std-ratio-max", type=float, default=3.0,
                    help="reject pair if any signal channel has "
                         "max(σ_A,σ_B)/min(σ_A,σ_B) > this — contamination guard")
    # OVERSPLIT-specific (stricter than DRIFT — same source, same time)
    ap.add_argument("--oversplit-cosw", type=float, default=0.98)
    ap.add_argument("--oversplit-iou-min", type=float, default=0.7)
    ap.add_argument("--oversplit-drift-um", type=float, default=5.0,
                    help="OVERSPLIT pairs must have centroid drift < this "
                         "(same time = same source position)")
    ap.add_argument("--oversplit-alpha-spread", type=float, default=1.3,
                    help="OVERSPLIT pairs must have α-spread < this "
                         "(same time = uniform scaling near 1)")
    ap.add_argument("--candidate-cosw", type=float, default=0.85,
                    help="cosW > this for any pair to enter the CSV")
    ap.add_argument("--peak-window", type=int, default=3,
                    help="half-window around peak_sample for α and kurt analysis")
    ap.add_argument("--signal-ptp-frac", type=float, default=0.3,
                    help="channel counts as 'signal' if its ptp ≥ this "
                         "fraction of the pair's max ptp (default 0.3)")
    # Per-cluster quality filter (D)
    ap.add_argument("--max-cluster-cv", type=float, default=0.30,
                    help="reject clusters whose max-CV on STRONG signal "
                         "channels (≥50%% peak ptp) exceeds this.  CV = "
                         "std_at_peak / ptp_per_channel: clean clusters "
                         "0.05-0.15, contaminated >0.30.")
    # Drift coherence across chunks (E)
    ap.add_argument("--chunk-gap-s", type=float, default=180.0,
                    help="t_mean gap > this defines a chunk boundary "
                         "(default 180 s; smaller than your 720 s chunks)")
    ap.add_argument("--no-drift-coherence", dest="drift_coherence",
                    action="store_false", default=True,
                    help="Disable per-transition drift coherence check "
                         "(default: pairs whose centroid_drift_um deviates "
                         ">3 MAD from the median across pairs spanning the "
                         "SAME chunk-transition get demoted from AUTO_DRIFT "
                         "to REVIEW)")
    ap.add_argument("--drift-coherence-k", type=float, default=3.0,
                    help="MAD multiplier for drift outlier detection")
    ap.add_argument("--no-require-complete-link", dest="require_complete_link",
                    action="store_false", default=True,
                    help="Disable complete-link verification (use raw single-link "
                         "union-find).  Default is to verify: a proposed merge "
                         "group is only accepted if EVERY internal pair passed "
                         "AUTO_DRIFT/AUTO_OVERSPLIT.  Without this check, "
                         "single-link chaining can absorb hundreds of clusters "
                         "via mid-cosine bridges.")
    ap.add_argument("--no-clique-recovery", dest="clique_recovery",
                    action="store_false", default=True,
                    help="Disable greedy clique recovery on rejected merge "
                         "groups.  Default: when complete-link rejects a "
                         "group, find the largest internally-consistent "
                         "sub-clique (all pairs AUTO_*) and merge that "
                         "instead.  Without this, all clusters in a rejected "
                         "group stay separate even if some legitimate sub-"
                         "merges exist.")
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

    # Filter to well-isolated, reasonably-sized clusters.  Two passes:
    # (1) basic — id, nspikes, ptp; (2) per-cluster CV filter — reject
    # clusters whose within-cluster amplitude variance is too high on
    # signal channels (contamination / mixture of units).
    ptp_max = ptp_mean.max(axis=0)
    keep = (clusters > 1) & (nspikes >= args.min_spikes) & (ptp_max >= args.min_ptp)

    # Per-cluster CV at peak sample — within-cluster amplitude variance
    # bound.  CV is std_at_peak / ptp_per_channel: physical normalization
    # (peak-to-peak amplitude is the channel's effective signal level),
    # robust to where exactly the trough sample sits.
    #
    # IMPORTANT: restrict to STRONG signal channels (≥ 50% of peak ptp).
    # Secondary channels at 30-40% ptp naturally have higher CV without
    # indicating contamination — they sit closer to the noise floor.
    # A contaminated cluster shows elevated CV on its DOMINANT channel,
    # which is what we actually want to catch.
    # Per-cluster trough: prefer the value saved in the NPZ; fall back
    # to a global peak_sample if working with an older NPZ that doesn't
    # have it.
    if "peak_sample_per_cluster" in d:
        peak_per_clust = d["peak_sample_per_cluster"].astype(np.int64)
    else:
        peak_per_clust = np.full(K_all, peak_sample, dtype=np.int64)
        print(f"  NOTE: NPZ has no peak_sample_per_cluster; using global "
              f"peak_sample={peak_sample} for all clusters.  Regenerate "
              f"with the latest cluster_waveform_stats.py for tighter "
              f"per-cluster metrics.")
    # CV at each cluster's empirical trough (fancy indexing):
    #   stds shape is (T, C, K); we want stds[peak_per_clust[k], c, k]
    # for every (k, c), yielding (K, C).
    S_peak_all   = stds[peak_per_clust, :, np.arange(K_all)]   # (K, C)
    ptp_per_ch   = ptp_mean.T                             # (K, C)
    cv_all       = S_peak_all / np.maximum(ptp_per_ch, 100.0)   # 100 µV floor
    strong_mask  = ptp_per_ch >= 0.5 * ptp_max[:, None]    # ≥ 50% peak ptp
    cv_masked    = np.where(strong_mask, cv_all, 0.0)
    max_cv_strong = cv_masked.max(axis=1)                  # (K,)
    n_high_cv = int(((max_cv_strong > args.max_cluster_cv) & keep).sum())
    keep &= (max_cv_strong <= args.max_cluster_cv)
    idx = np.flatnonzero(keep)
    n_keep = len(idx)
    print(f"  filtered: {n_keep}/{K_all} clusters (id>1, nspk≥{args.min_spikes}, "
          f"max_ptp≥{args.min_ptp:.0f}, max-CV-signal ≤ {args.max_cluster_cv})")
    if n_high_cv:
        print(f"    ({n_high_cv} additional clusters rejected on CV "
              f"threshold — contamination / mixture)")

    # Reshape per-cluster arrays for the selected subset
    M3 = means.transpose(2, 0, 1)[idx]              # (n, T, C)
    M  = M3.reshape(n_keep, T * C)
    P  = ptp_mean.T[idx]                            # (n, C)
    S_peak = S_peak_all[idx]                        # (n, C) — for std-ratio test
    ids = clusters[idx]
    if have_time:
        tmin = t_min[idx]; tmax = t_max[idx]; tmean = t_mean[idx]
        # Chunk assignment from t_mean — pairs spanning the same
        # chunk-transition are peer groups for drift coherence.
        chunk_id, n_chunks = assign_chunks(tmean, args.chunk_gap_s)
        print(f"  chunk assignment: {n_chunks} chunks detected "
              f"(gap threshold {args.chunk_gap_s:.0f} s)")
    else:
        tmin = tmax = tmean = None
        chunk_id = None
        n_chunks = 0
    ch_dom = np.argmax(P, axis=1)
    # Per-cluster peak sample for the kept subset
    peak_per_idx = peak_per_clust[idx]              # (n,) int64

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
            # α-window centred on the MIDPOINT of the two clusters'
            # empirical troughs.  Sub-sample alignment differences (±1
            # sample) are absorbed by the ±peak_window slack.
            peak_mid = (peak_per_idx[i] + peak_per_idx[j]) // 2
            peak_slice = slice(max(0, peak_mid - args.peak_window),
                                min(T, peak_mid + args.peak_window + 1))
            scores = score_pair(
                i, j,
                M3=M3, P=P, S_peak=S_peak, ids=ids, nspikes_idx=nspikes_idx,
                ch_dom=ch_dom, y_um=y_um, peak_slice=peak_slice,
                cosW=cosW, cosFP=cosFP, xcc=xcc,
                tmin=tmin, tmax=tmax, tmean=tmean, chunk_id=chunk_id,
                have_time=have_time,
                signal_ptp_frac=args.signal_ptp_frac,
            )
            if scores is None:
                continue
            scores["tier"] = classify_tier(scores, args, have_time)
            pair_data.append(scores)

    # Drift coherence pass — clusters in one chunk-pair-transition all
    # experience the same drift event.  AUTO_DRIFT pairs whose drift_um
    # deviates >K MAD from their transition's median are flagged as
    # incoherent (likely different units, not real drift) and demoted
    # to REVIEW.  Transitions with <3 AUTO_DRIFT peers can't establish
    # a reliable median and are skipped.
    if args.drift_coherence and have_time:
        outliers, by_trans = drift_coherence_outlier_mask(
            pair_data, args.drift_coherence_k)
        n_demoted = 0
        for k in outliers:
            pair_data[k]["tier"] = "REVIEW"
            pair_data[k]["demoted_reason"] = "drift_incoherent"
            n_demoted += 1
        n_trans = sum(1 for v in by_trans.values() if len(v) >= 3)
        if n_demoted or n_trans:
            print(f"  drift coherence: {n_trans} chunk-transitions had "
                  f"≥3 peer pairs; demoted {n_demoted} outliers "
                  f"(>{args.drift_coherence_k}×MAD from transition median)")

    print(f"  {len(pair_data)} candidate pairs (cosW > {args.candidate_cosw})")
    tier_counts = {}
    for p in pair_data:
        tier_counts[p["tier"]] = tier_counts.get(p["tier"], 0) + 1
    for t in ("AUTO_DRIFT", "AUTO_OVERSPLIT", "REVIEW_PARTIAL_OVERLAP", "REVIEW"):
        if t in tier_counts:
            print(f"     {t:<24s} {tier_counts[t]:>5d}")

    # Apply auto-accepted merges via union-find, then VALIDATE each
    # proposed group via complete-link verification.  Pure union-find is
    # single-link clustering — it merges A and C whenever A↔B and B↔C
    # both pass, even if A↔C itself fails.  With many broadly-similar
    # waveforms this chains uncontrollably (a single group can absorb
    # 100+ clusters via mid-cosine bridges).  Complete-link demands that
    # every internal pair within a merge group independently passed
    # AUTO_DRIFT or AUTO_OVERSPLIT.
    accepted_pairs = [(p["cid_A"], p["cid_B"]) for p in pair_data
                      if p["tier"] in ("AUTO_DRIFT", "AUTO_OVERSPLIT")]
    print(f"\n  proposing merges from {len(accepted_pairs)} auto-accepted pairs "
          f"(union-find)...")

    clu_orig = read_clu(args.session, args.group).astype(np.int32)
    all_cids = np.unique(clu_orig).tolist()
    uf = UnionFind(all_cids)
    for a, b in accepted_pairs:
        uf.union(a, b)

    # Build pair lookup for complete-link verification
    pair_lookup = {}
    for p in pair_data:
        key = (min(p["cid_A"], p["cid_B"]), max(p["cid_A"], p["cid_B"]))
        pair_lookup[key] = p["tier"]

    # Validate each proposed group: ALL internal pairs must be in
    # pair_lookup with AUTO tier.  Missing pairs (cosW < candidate_cosw)
    # are treated as failed — they were never auto-accepted.
    if args.require_complete_link:
        print(f"  complete-link verification of proposed merge groups...")
    groups = uf.groups()
    remap = {c: c for c in all_cids}
    valid_groups = []
    rejected_groups = []
    for root, members in groups.items():
        if len(members) < 2:
            continue
        if not args.require_complete_link:
            # Original single-link behaviour — accept all
            label = min(members)
            for m in members:
                remap[m] = label
            valid_groups.append(members)
            continue
        # Check all C(n,2) internal pairs
        bad = []
        members_sorted = sorted(members)
        for ii, a in enumerate(members_sorted):
            for b in members_sorted[ii + 1:]:
                key = (a, b)
                tier = pair_lookup.get(key, "MISSING")
                if tier not in ("AUTO_DRIFT", "AUTO_OVERSPLIT"):
                    bad.append((a, b, tier))
                    if len(bad) >= 3:
                        break   # 3 examples is enough for diagnostic
            if len(bad) >= 3:
                break
        if not bad:
            label = min(members)
            for m in members:
                remap[m] = label
            valid_groups.append(members)
        else:
            rejected_groups.append((sorted(members), bad))

    # Clique recovery: for each rejected group, ITERATIVELY find the
    # largest internally-consistent sub-clique, merge it, remove its
    # members, and repeat until no more cliques ≥2 can be found.  This
    # extracts ALL valid sub-merges from a single rejected runaway in
    # one pass — e.g., a 100-cluster chain containing 3 real drift
    # sequences yields 3 separate merge groups.  Every clique returned
    # is guaranteed complete-link by construction.
    recovered_groups = []
    if args.clique_recovery and rejected_groups:
        accepted_pair_set = set()
        for p in pair_data:
            if p["tier"] in ("AUTO_DRIFT", "AUTO_OVERSPLIT"):
                accepted_pair_set.add(
                    (min(p["cid_A"], p["cid_B"]),
                     max(p["cid_A"], p["cid_B"])))
        rejected_sorted = sorted(rejected_groups, key=lambda x: -len(x[0]))
        still_rejected = []
        for members, bad_examples in rejected_sorted:
            parent_size = len(members)
            remaining = set(members)
            while len(remaining) >= 2:
                clique = greedy_max_clique(sorted(remaining), accepted_pair_set)
                if len(clique) < 2:
                    break
                # Defensive complete-link check on extracted clique
                clique_sorted = sorted(clique)
                if not all(
                    (a, b) in accepted_pair_set
                    for i, a in enumerate(clique_sorted)
                    for b in clique_sorted[i + 1:]
                ):
                    break  # algorithm error — shouldn't happen
                label = min(clique)
                for m in clique:
                    remap[m] = label
                recovered_groups.append((clique_sorted, parent_size))
                remaining -= clique
            if remaining:
                # Singletons / fully disconnected leftovers
                still_rejected.append((sorted(remaining), bad_examples))
        rejected_groups = still_rejected

    # Reporting
    n_valid_merged = sum(len(g) - 1 for g in valid_groups)
    n_valid_groups = len(valid_groups)
    n_recovered_groups = len(recovered_groups)
    n_recovered_merged = sum(len(g) - 1 for g, _ in recovered_groups)
    n_rejected = len(rejected_groups)
    n_rejected_members = sum(len(m) for m, _ in rejected_groups)
    new_count = len(set(remap.values()))
    print(f"  cluster count: {len(all_cids)} → {new_count}")
    print(f"    {n_valid_groups} groups merged via complete-link "
          f"({n_valid_groups + n_valid_merged} clusters)")
    if n_recovered_groups:
        print(f"    {n_recovered_groups} groups merged via clique recovery "
              f"({n_recovered_groups + n_recovered_merged} clusters)")
    if rejected_groups:
        print(f"  REJECTED: {n_rejected} proposed groups containing "
              f"{n_rejected_members} clusters had NO recoverable clique")
        print(f"           (each rejected cluster KEEPS its original ID)")
    # Show valid merges
    valid_summary = sorted([(min(g), len(g), sorted(g)) for g in valid_groups])
    for label, sz, members in valid_summary[:20]:
        print(f"    {label} ← {members}    (complete-link)")
    if len(valid_summary) > 20:
        print(f"    ... and {len(valid_summary) - 20} more complete-link merges")
    # Show recovered cliques — note how big the rejected parent was
    if recovered_groups:
        recovered_groups.sort(key=lambda x: -len(x[0]))
        print(f"\n  Recovered cliques (sub-clique extracted from rejected groups):")
        for clique, parent_size in recovered_groups[:15]:
            print(f"    {min(clique)} ← {clique}    "
                  f"(clique from {parent_size}-cluster rejected group)")
        if len(recovered_groups) > 15:
            print(f"    ... and {len(recovered_groups) - 15} more recovered cliques")
    # Show a few still-rejected groups so user can inspect in CSV
    if rejected_groups:
        rejected_groups.sort(key=lambda x: -len(x[0]))
        print(f"\n  Top still-rejected groups (no usable clique found):")
        for members, bad in rejected_groups[:5]:
            example = bad[0]
            print(f"    {len(members):>4d} clusters proposed (root={min(members)}): "
                  f"e.g. pair ({example[0]}, {example[1]}) tier={example[2]} "
                  f"breaks the chain")
        if len(rejected_groups) > 5:
            print(f"    ... and {len(rejected_groups) - 5} more rejected groups")

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
