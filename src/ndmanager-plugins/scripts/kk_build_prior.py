#!/usr/bin/env python3
"""
kk_build_prior.py  —  Build a KlustaKwik empirical prior from curation logs.

Reads one or more .curation_log.N.jl files produced by Klusters, filters to
well-isolated clusters, learns the spatial structure of the feature space, and
writes a .prior.N.yaml file that KlustaKwik can load via -PriorFile.

The core insight encoded in the prior
──────────────────────────────────────
Clusters in PCA feature space are NOT homogeneous in their spatial character:

  Type A — strong / localized
    A neuron recorded close to a probe contact produces a high-amplitude spike
    that dominates 1-3 PCA dimensions (those capturing its primary channels).
    In feature space, this cluster occupies a SMALL volume and its covariance
    matrix has only a few large eigenvalues.

      d_eff ≈ 2–5,  feat_var_top3 / feat_var_total ≈ 0.7–0.9
      waveform_snr > 6,  waveform_chan_spread ≤ 2

  Type B — weak / distributed
    A neuron recorded at moderate distance contributes a low-amplitude signal
    spread across many channels.  Its spike waveform is distinctive (a unique
    pattern across all channels) but each individual PCA component is small.
    In feature space, this cluster occupies a LARGER volume and its covariance
    is spread across many dimensions.

      d_eff ≈ 10–18,  feat_var_top3 / feat_var_total ≈ 0.2–0.4
      waveform_snr 2–5,  waveform_chan_spread ≥ 4

  Type C — interneuron-like
    High firing rate, narrow spike, moderate amplitude.  Usually a compact
    cluster in a sparse region of feature space.

      isi_cv < 0.8,  waveform_width_samp < 8,  firing_hz > 20

The participation ratio d_eff = (Σσ²_i)² / Σ(σ²_i)²  is the key feature.

KlustaKwik currently uses a fixed MergeThresh = chi²(nSpatialDims, 0.9999),
treating all cluster types identically.  The prior expresses the empirical
distribution of d_eff so KlustaKwik can use chi²(d_eff, 0.9999) per pair:
  Type A:  chi²(3.5, 0.9999) ≈ 17   (tight — these clusters are compact)
  Type B:  chi²(13,  0.9999) ≈ 35   (loose — noise naturally inflates variance)
  nSpatialDims fixed:          ≈ 42  (current default — too permissive for A)

Usage
─────
  python kk_build_prior.py session1.curation_log.1.jl session2.curation_log.1.jl \\
      --out session_combined.prior.1.yaml \\
      [--min-quality 2]     # only quality-annotated confident events
      [--max-l-ratio 0.05]  # L-ratio contamination filter
      [--min-isolation 20]  # isolation distance filter
      [--min-spikes 30]     # minimum spikes per accepted cluster
      [--session-preseed path/to/latest_session.curation_log.1.jl]
          # populate preseed_centres from the most recent curated session

Output: a YAML file readable by KlustaKwik's -PriorFile parameter.

Requirements: numpy, scipy, yaml (pyyaml).  No sklearn required.
"""

import argparse
import hashlib
import json
import math
import os
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import yaml


# ── Probe identity ────────────────────────────────────────────────────────────

def load_session_yaml(path):
    """Load and return a Neurosuite session YAML as a dict."""
    with open(path) as f:
        return yaml.safe_load(f)


def probe_signature_from_session(session_dict):
    """
    Build a canonical, JSON-serialisable probe-signature dict from a
    Neurosuite session YAML dict.  Same probe (same channel layout, same
    spike-detection config) → same signature → same hash.

    The signature is intentionally narrow: it captures only what's invariant
    across recording sessions on the same physical probe.  Anything that
    varies session-to-session (notes, references, anatomy labels, voltage
    range adjustments) is excluded so a probe used in two regions still
    produces the same probe_id.

    Sample rate is NOT part of the signature — it's acquisition-system
    configuration, not probe identity.  A probe used at 32 kHz vs 20 kHz
    produces the same probe_id and can share priors.  (n_samples is part
    of the signature, so a probe whose spike-window length changes still
    gets a fresh id.)
    """
    acq = session_dict.get("acquisitionSystem", {}) or {}
    spk = session_dict.get("spikeDetection", {}) or {}

    n_channels = int(acq.get("nChannels", 0))

    groups_raw = spk.get("channelGroups", []) or []
    groups = []
    for g in groups_raw:
        if not isinstance(g, dict):
            continue
        ch = g.get("channels", []) or []
        # Sort channel ids so the hash is order-invariant within a group.
        ch_sorted = sorted(int(c) for c in ch)
        groups.append({
            "channels":          ch_sorted,
            "n_samples":         int(g.get("nSamples", 0)),
            "peak_sample_index": int(g.get("peakSampleIndex", 0)),
            "n_features":        int(g.get("nFeatures", 0)),
        })

    return {
        "n_channels":     n_channels,
        "channel_groups": groups,
    }


def probe_id_from_signature(sig):
    """
    Compute a 16-hex-char SHA-256 fingerprint of a probe-signature dict.
    Stable across kk_build_prior.py runs on the same probe; differs whenever
    any signature field changes (different probe, rewired probe, different
    nFeatures, different sample rate).
    """
    canonical = json.dumps(sig, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:16]


def _default_template_dirs():
    """Yield candidate directories holding ndmanager Template-*.yaml files."""
    env = os.environ.get("NDM_TEMPLATE_DIR")
    if env:
        for entry in env.split(":"):
            entry = entry.strip()
            if entry:
                yield Path(entry)
    yield Path("/usr/share/ndmanager/templates")
    yield Path("/usr/local/share/ndmanager/templates")
    # Build-tree fallback (relative to this script).
    here = Path(__file__).resolve().parent
    yield here / ".." / ".." / "ndmanager" / "src"
    yield here.parent / "ndmanager" / "src"


def find_matching_template(target_sig, template_dirs=None):
    """
    Walk Template-*.yaml in the candidate directories and return the
    template's basename (sans .yaml, sans "Template-" prefix) for the
    first template whose probe-signature matches target_sig.  Returns
    None if no match.

    Returns names like "4-octrodes-32552Hz".  Two physical probes of the
    same template type produce the same friendly name and the same hash,
    so they share priors — exactly the desired behaviour.
    """
    if template_dirs is None:
        template_dirs = _default_template_dirs()
    seen = set()
    for d in template_dirs:
        if not d.is_dir():
            continue
        d_resolved = d.resolve()
        if d_resolved in seen:
            continue
        seen.add(d_resolved)
        for tpl in sorted(d_resolved.glob("Template-*.yaml")):
            try:
                with open(tpl) as f:
                    tpl_dict = yaml.safe_load(f) or {}
            except Exception:
                continue
            tpl_sig = probe_signature_from_session(tpl_dict)
            if tpl_sig == target_sig:
                stem = tpl.stem
                if stem.startswith("Template-"):
                    stem = stem[len("Template-"):]
                return stem
    return None


def probe_id_for_session(session_dict, template_dirs=None):
    """
    Compute a probe identifier for a session.  Prefers the friendly
    template-derived name when the session's channel-group structure
    matches an installed Template-*.yaml; falls back to the SHA-256-hex
    hash otherwise.

    Returns (probe_id, probe_sig, signature_hash).  The hash is always
    returned so it can be stored alongside a friendly id as a
    verification field — a renamed or hand-edited prior file will fail
    cross-checks even if the friendly name still matches.
    """
    sig      = probe_signature_from_session(session_dict)
    sig_hash = probe_id_from_signature(sig)
    friendly = find_matching_template(sig, template_dirs)
    return (friendly or sig_hash), sig, sig_hash


# ── Helpers ──────────────────────────────────────────────────────────────────

def participation_ratio(var_dims):
    """d_eff = (Σσ²)² / Σ(σ²)²  from a list/array of per-dim variances."""
    v = np.asarray(var_dims, dtype=float)
    v = np.maximum(v, 0.0)
    sv = v.sum()
    sv2 = (v ** 2).sum()
    if sv2 < 1e-12:
        return float(len(v))
    return float(sv * sv / sv2)


def chi2_quantile_9999(d_eff):
    """Wilson-Hilferty chi²(d_eff, 0.9999).  z = 3.719 for p=0.9999."""
    d = max(1.0, d_eff)
    z = 3.719
    t = 1.0 - 2.0 / (9.0 * d) + z * math.sqrt(2.0 / (9.0 * d))
    return d * t ** 3


def _pct(arr, q):
    return float(np.percentile(arr, q)) if len(arr) > 0 else 0.0


# ── Loading ───────────────────────────────────────────────────────────────────

def load_log(path, min_quality, max_l_ratio, min_isolation, min_spikes):
    """
    Read one .jl log file and return a list of accepted cluster dicts.

    Filtering strategy
    ──────────────────
    A cluster snapshot is accepted when ALL of:
      1. phase == "after"  and  role == "result"  (post-action state)
      2. action is one of the quality-raising types
         (GROUP, SPLIT, SPLIT_N, RECLUSTER, REALIGN, NUDGE)
      3. action_idx NOT followed by an UNDO event in the same session
      4. n_spikes >= min_spikes
      5. If waveform_available: waveform_snr > 0
      6. l_ratio <= max_l_ratio  (if l_ratio is logged)
      7. isolation_dist >= min_isolation  (if logged)
      8. If quality annotation present for this action_idx: quality >= min_quality
    """
    events = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            events.append(obj)

    # Build per-session: set of action_idx reverted by UNDO, and quality map
    # {session_id: {action_idx: quality}}
    undone = defaultdict(set)
    quality_map = defaultdict(dict)
    for ev in events:
        if ev.get("action") == "UNDO":
            sid = ev.get("session_id", "")
            tidx = ev.get("target_action_idx", -1)
            if tidx >= 0:
                undone[sid].add(tidx)
        if ev.get("event") == "ANNOTATE":
            sid = ev.get("session_id", "")
            aidx = ev.get("action_idx", -1)
            q = ev.get("quality", -1)
            if aidx >= 0 and q >= 0:
                quality_map[sid][aidx] = q

    QUALITY_ACTIONS = {
        "GROUP", "SPLIT", "SPLIT_N", "RECLUSTER", "REALIGN", "NUDGE"
    }

    accepted = []
    for ev in events:
        if ev.get("phase") != "after":
            continue
        if ev.get("role") != "result":
            continue
        action = ev.get("action", "")
        if action not in QUALITY_ACTIONS:
            continue

        sid   = ev.get("session_id", "")
        aidx  = ev.get("action_idx", -1)

        # Drop if this action was undone
        if aidx in undone[sid]:
            continue

        # Quality gate: if there's a quality annotation, filter on it
        if sid in quality_map and aidx in quality_map[sid]:
            if quality_map[sid][aidx] < min_quality:
                continue

        n_spikes = ev.get("n_spikes", 0)
        if n_spikes < min_spikes:
            continue

        l_ratio = ev.get("l_ratio", None)
        if l_ratio is not None and l_ratio > max_l_ratio:
            continue

        iso_dist = ev.get("isolation_dist", None)
        if iso_dist is not None and iso_dist > 0 and iso_dist < min_isolation:
            continue

        accepted.append(ev)

    return accepted


# ── Feature-space analysis ────────────────────────────────────────────────────

def compute_d_eff(cluster):
    dims = cluster.get("feat_var_dims", [])
    if not dims:
        return None
    return participation_ratio(dims)


def concentration_ratio(cluster):
    """top3 variance fraction = feat_var_top3_mean * 3 / (feat_var_mean * nDims)."""
    top3 = cluster.get("feat_var_top3_mean", None)
    mean = cluster.get("feat_var_mean", None)
    ndims = cluster.get("n_feat_dims", None)
    if not all([top3, mean, ndims]) or mean < 1e-9 or ndims < 3:
        return None
    return float(top3 * 3.0 / (mean * ndims))


def fisher_discriminant_ratios(clusters, n_dims):
    """
    Per-dimension Fisher discriminant ratio (FDR):

        FDR[d] = between_cluster_variance[d] / within_cluster_variance[d]

    'Between' = variance of per-cluster mean variances across clusters
    (proxy: since we don't have raw centroids, we use the CV of each
    cluster's σ²_d across all clusters — high CV means some clusters
    have much higher variance on dim d than others, indicating it
    discriminates between cluster types).

    'Within' = mean of per-cluster σ²_d
    """
    dim_vars = [[] for _ in range(n_dims)]
    for c in clusters:
        dims = c.get("feat_var_dims", [])
        for d in range(min(n_dims, len(dims))):
            dim_vars[d].append(dims[d])

    fdr = []
    for d in range(n_dims):
        v = np.array(dim_vars[d])
        if len(v) < 2:
            fdr.append(0.0)
            continue
        within = float(v.mean())
        between = float(v.std())
        fdr.append(between / (within + 1e-9))

    return fdr


def cluster_type_taxonomy(clusters, n_types=3):
    """
    Identify cluster types using k-means on a 4-feature space:
      (d_eff, concentration_ratio, waveform_chan_spread, waveform_snr)

    Returns list of dicts with per-type statistics.
    """
    # Build feature matrix — skip clusters with missing data
    rows = []
    valid_idx = []
    for i, c in enumerate(clusters):
        d_eff = compute_d_eff(c)
        conc  = concentration_ratio(c)
        if d_eff is None or conc is None:
            continue
        snr    = c.get("waveform_snr", 0.0) if c.get("waveform_available") else 0.0
        spread = float(c.get("waveform_chan_spread", 0))
        rows.append([d_eff, conc, spread, snr])
        valid_idx.append(i)

    if len(rows) < n_types * 5:
        return []

    X = np.array(rows, dtype=float)

    # Normalise columns to [0, 1] range
    Xmin = X.min(axis=0)
    Xmax = X.max(axis=0)
    Xrange = np.where(Xmax - Xmin > 1e-9, Xmax - Xmin, 1.0)
    Xn = (X - Xmin) / Xrange

    # Simple k-means (no sklearn dependency)
    rng = np.random.default_rng(42)
    centres = Xn[rng.choice(len(Xn), n_types, replace=False)]
    labels = np.zeros(len(Xn), dtype=int)

    for _ in range(100):
        dists = np.stack([np.linalg.norm(Xn - c, axis=1) for c in centres], axis=1)
        new_labels = dists.argmin(axis=1)
        if (new_labels == labels).all():
            break
        labels = new_labels
        for k in range(n_types):
            mask = labels == k
            if mask.sum() > 0:
                centres[k] = Xn[mask].mean(axis=0)

    # Build per-type statistics
    type_names = ["unknown"] * n_types
    types = []
    for k in range(n_types):
        mask = labels == k
        if mask.sum() == 0:
            continue
        subset = [clusters[valid_idx[i]] for i in range(len(valid_idx)) if labels[i] == k]

        d_effs  = [compute_d_eff(c) for c in subset if compute_d_eff(c) is not None]
        spreads = [c.get("waveform_chan_spread", 0) for c in subset if c.get("waveform_available")]
        snrs    = [c.get("waveform_snr", 0) for c in subset if c.get("waveform_available")]
        frobes  = [c.get("feat_var_frobenius", 0) for c in subset]
        isi_cvs = [c.get("isi_cv", 0) for c in subset]
        fw      = [c.get("waveform_width_samp", 0) for c in subset if c.get("waveform_available")]

        med_d   = float(np.median(d_effs)) if d_effs else 0.0
        med_snr = float(np.median(snrs)) if snrs else 0.0
        med_spr = float(np.median(spreads)) if spreads else 0.0
        med_cv  = float(np.median(isi_cvs)) if isi_cvs else 0.0
        med_wid = float(np.median(fw)) if fw else 0.0

        # Label heuristic
        if med_snr > 6 and med_spr <= 2.5 and med_d < 6:
            label = "strong_localized"
        elif med_snr < 4 and med_spr >= 4 and med_d > 9:
            label = "weak_distributed"
        elif med_cv < 0.9 and med_wid < 9 and med_snr > 4:
            label = "interneuron_like"
        else:
            label = f"mixed_type_{k}"

        types.append({
            "label": label,
            "fraction": float(mask.sum()) / len(valid_idx),
            "n_clusters": int(mask.sum()),
            "d_eff_median": round(med_d, 2),
            "waveform_chan_spread_median": round(med_spr, 1),
            "waveform_snr_median": round(med_snr, 2),
            "feat_var_frobenius_median": round(float(np.median(frobes)), 1) if frobes else 0.0,
            "isi_cv_median": round(med_cv, 2),
            "merge_thresh_for_type": round(chi2_quantile_9999(med_d), 2),
        })

    # Sort by fraction descending
    types.sort(key=lambda t: -t["fraction"])
    return types


def snr_variance_model(clusters):
    """
    Fit: feat_var_frobenius = A / waveform_snr² + B
    Returns (A, B, r2).
    """
    xs, ys = [], []
    for c in clusters:
        if not c.get("waveform_available"):
            continue
        snr = c.get("waveform_snr", 0.0)
        frob = c.get("feat_var_frobenius", 0.0)
        if snr > 0.5 and frob > 0.0:
            xs.append(1.0 / (snr * snr))
            ys.append(frob)

    if len(xs) < 5:
        return 0.0, 0.0, 0.0

    X = np.array(xs)
    Y = np.array(ys)
    # Least squares: Y ≈ A*X + B
    A_mat = np.column_stack([X, np.ones_like(X)])
    result = np.linalg.lstsq(A_mat, Y, rcond=None)
    coeff = result[0]
    A, B = float(coeff[0]), float(coeff[1])

    # R²
    y_pred = A * X + B
    ss_res = ((Y - y_pred) ** 2).sum()
    ss_tot = ((Y - Y.mean()) ** 2).sum()
    r2 = float(1.0 - ss_res / (ss_tot + 1e-12))
    return round(A, 1), round(B, 1), round(r2, 3)


# ── Preseed centres from latest session ───────────────────────────────────────

def extract_preseed_centres(path, min_spikes=30):
    """
    Extract cluster centroids from the most recent curated session in a log file.
    We don't store raw centroids in the log, so we use the after-state snapshots
    as a proxy: for each accepted cluster, its position is approximated by the
    first 3 principal components of its variance profile (a rough centre estimate).

    For proper preseed, the caller should pass a session where the LAST
    action per cluster is recorded — the final "after" state gives the best
    centroids.  Returns a flat float list or [] if unavailable.

    NOTE: because the log doesn't store raw feature vectors, this function
    returns an EMPTY list by default.  True preseed centres require a separate
    export step (e.g. from the .clu.N + .fet.N files of the curated session).
    The placeholder is here so the YAML schema is populated.
    """
    # TODO: implement proper centroid export from .fet.N + .clu.N
    return []


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log_files", nargs="+",
                    help=".curation_log.N.jl files to analyse")
    ap.add_argument("--session-yaml", required=True,
                    help="Path to a session .yaml from this probe.  Used to "
                         "compute probe_id and channel_groups.  Any session "
                         "recorded on the target probe works — the computed "
                         "id is invariant across sessions.")
    ap.add_argument("--out-dir", default=".",
                    help="Directory in which to write the prior YAML "
                         "(default: current directory).  The filename is "
                         "auto-generated as <probe_id>.<group>.prior.yaml.")
    ap.add_argument("--out", default=None,
                    help="Explicit output path.  Overrides --out-dir auto-naming "
                         "(legacy/manual use).")
    ap.add_argument("--min-quality",  type=int,   default=1,
                    help="Minimum quality annotation (0=bad,1=uncertain,2=good). "
                         "Default 1 (uncertain+good). Use 2 for confident-only.")
    ap.add_argument("--max-l-ratio",  type=float, default=0.1,
                    help="Maximum L-ratio for accepted clusters (default 0.1)")
    ap.add_argument("--min-isolation",type=float, default=15.0,
                    help="Minimum isolation distance (default 15)")
    ap.add_argument("--min-spikes",   type=int,   default=30,
                    help="Minimum spike count per accepted cluster (default 30)")
    ap.add_argument("--n-types",      type=int,   default=3,
                    help="Number of cluster types for taxonomy (default 3)")
    ap.add_argument("--electrode-group", type=int, required=True,
                    help="Electrode group (1-based) this prior is for.  "
                         "kk_build_prior produces one YAML per shank.")
    ap.add_argument("--templates-dir", action="append", default=None,
                    help="Override search path for Template-*.yaml files.  "
                         "May be specified multiple times.  When the session's "
                         "channel-group structure matches an installed "
                         "template, the prior gets a human-readable id "
                         "(e.g. '4-octrodes-32552Hz') instead of a hex hash.")
    ap.add_argument("--session-preseed", default=None,
                    help="Log file to extract preseed centres from "
                         "(latest session for chronic recording)")
    args = ap.parse_args()

    # ── Probe identity ─────────────────────────────────────────────────
    session_dict = load_session_yaml(args.session_yaml)
    template_dirs = ([Path(p) for p in args.templates_dir]
                     if args.templates_dir else None)
    probe_id, probe_sig, sig_hash = probe_id_for_session(session_dict,
                                                          template_dirs)

    # Validate the requested electrode group exists in this session.
    if args.electrode_group < 1 or \
       args.electrode_group > len(probe_sig["channel_groups"]):
        print(f"ERROR: --electrode-group {args.electrode_group} out of range "
              f"(session has {len(probe_sig['channel_groups'])} groups).",
              file=sys.stderr)
        sys.exit(1)

    # Resolve output path.
    if args.out:
        out_path = Path(args.out)
    else:
        out_dir = Path(args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        out_path = out_dir / f"{probe_id}.{args.electrode_group}.prior.yaml"

    print(f"Probe id: {probe_id}"
          + ("" if probe_id == sig_hash else f"  (hash: {sig_hash})"))
    print(f"  groups: {len(probe_sig['channel_groups'])}, "
          f"n_channels: {probe_sig['n_channels']}")
    print(f"Output:   {out_path}")
    print()

    # ── Load all logs ──────────────────────────────────────────────────
    all_clusters = []
    n_sessions   = 0
    for path in args.log_files:
        clusters = load_log(path,
                            min_quality=args.min_quality,
                            max_l_ratio=args.max_l_ratio,
                            min_isolation=args.min_isolation,
                            min_spikes=args.min_spikes)
        all_clusters.extend(clusters)
        n_sessions += 1
        print(f"  {path}: {len(clusters)} accepted clusters")

    if not all_clusters:
        print("ERROR: no clusters passed the quality filters.", file=sys.stderr)
        sys.exit(1)

    n_total = len(all_clusters)
    print(f"\nTotal accepted clusters: {n_total} from {n_sessions} session(s)")

    # ── Probe context: prefer the session yaml (authoritative) over the log
    # snapshot fields, which may be stale or stripped depending on logger
    # version.  n_pca_dims comes from the log (it's a feature-space property,
    # not a probe property — depends on the spike-sorting pipeline).  ────────
    ref = all_clusters[0]
    n_pca_dims  = int(ref.get("n_pca_dims",  0))
    n_feat_dims = int(ref.get("n_feat_dims", n_pca_dims))

    # ── Cluster count distribution ─────────────────────────────────────
    # n_clusters_in_group is the total number of clusters at the time of the
    # "after" snapshot — this is the curator's accepted count for that session
    # frame.  Group by (session_id, action_idx) and take the "after" cluster
    # count at the moment of acceptance.
    counts_per_frame = defaultdict(int)
    for c in all_clusters:
        key = (c.get("session_id", ""), c.get("action_idx", 0))
        counts_per_frame[key] = max(counts_per_frame[key],
                                    c.get("n_clusters_in_group", 0))
    all_counts = sorted(counts_per_frame.values())

    print(f"\nCluster count distribution: "
          f"p05={_pct(all_counts,5):.0f} "
          f"median={_pct(all_counts,50):.0f} "
          f"p95={_pct(all_counts,95):.0f}")

    # ── d_eff distribution ─────────────────────────────────────────────
    d_effs = [compute_d_eff(c) for c in all_clusters if compute_d_eff(c) is not None]
    if not d_effs:
        print("WARNING: no feat_var_dims data — d_eff model unavailable", file=sys.stderr)
        d_effs = [n_pca_dims] * 5  # fallback

    d_effs = np.array(d_effs)
    d_eff_median = float(np.median(d_effs))
    merge_thresh_median = chi2_quantile_9999(d_eff_median)

    print(f"d_eff: p05={_pct(d_effs,5):.1f}  median={d_eff_median:.1f}  "
          f"p95={_pct(d_effs,95):.1f}")
    print(f"MergeThresh from median d_eff: {merge_thresh_median:.1f}  "
          f"(chi2({d_eff_median:.1f}, 0.9999))")
    print(f"Compare: chi2({n_pca_dims}, 0.9999) = {chi2_quantile_9999(n_pca_dims):.1f} "
          f"(current default if nSpatialDims = {n_pca_dims})")

    # ── Inter-cluster distance ─────────────────────────────────────────
    # nearest_centroid_dist_norm is dist/frobenius — convert to approximate
    # symmetric Mahalanobis: sym_mahal ≈ (nearest_centroid_dist_norm)² × d_eff
    sym_mahals = []
    for c in all_clusters:
        ncdn = c.get("nearest_centroid_dist_norm", None)
        de   = compute_d_eff(c)
        if ncdn is not None and de is not None and ncdn > 0:
            sym_mahals.append(ncdn * ncdn * de)

    ic_p05 = _pct(sym_mahals, 5) if sym_mahals else 20.0

    # ── Fisher dimension importance ────────────────────────────────────
    effective_n_dims = n_feat_dims if n_feat_dims > 0 else n_pca_dims
    fdr = fisher_discriminant_ratios(all_clusters, effective_n_dims)
    importance_order = sorted(range(len(fdr)), key=lambda i: -fdr[i])
    top5_dims = [importance_order[i] for i in range(min(5, len(importance_order)))]
    print(f"Top 5 discriminating PCA dims: {top5_dims}")

    # ── SNR → variance model ───────────────────────────────────────────
    snr_A, snr_B, snr_r2 = snr_variance_model(all_clusters)
    if snr_r2 > 0:
        print(f"SNR→variance: frobenius = {snr_A:.0f}/snr² + {snr_B:.0f}  (R²={snr_r2:.2f})")

    # ── Cluster type taxonomy ──────────────────────────────────────────
    types = cluster_type_taxonomy(all_clusters, n_types=args.n_types)
    print(f"\nCluster types ({len(types)} identified):")
    for t in types:
        print(f"  {t['label']:25s}  {t['fraction']*100:.0f}%  "
              f"d_eff={t['d_eff_median']:.1f}  snr={t['waveform_snr_median']:.1f}  "
              f"thresh={t['merge_thresh_for_type']:.1f}")

    # ── PenaltyMix suggestion ──────────────────────────────────────────
    # Heuristic: if strong_localized type fraction > 0.4, slightly increase
    # penalty to suppress over-splitting of tight clusters.
    penalty_mix = 0.03
    for t in types:
        if "strong_localized" in t["label"] and t["fraction"] > 0.4:
            penalty_mix = 0.05

    # ── Preseed centres ────────────────────────────────────────────────
    preseed_centres = []
    if args.session_preseed:
        preseed_centres = extract_preseed_centres(args.session_preseed,
                                                   min_spikes=args.min_spikes)
        print(f"Preseed: {len(preseed_centres) // max(1, n_pca_dims)} centres "
              f"from {args.session_preseed}")

    # ── Assemble prior YAML ────────────────────────────────────────────
    prior = {
        "probe_id": probe_id,
        "probe_signature_hash": sig_hash,
        "electrode_group": args.electrode_group,
        "probe_signature": {
            "n_channels":     probe_sig["n_channels"],
            "n_pca_dims":     n_pca_dims,
            "channel_groups": probe_sig["channel_groups"],
        },
        "source": {
            "n_sessions":      n_sessions,
            "n_clusters":      n_total,
            "log_files":       [str(p) for p in args.log_files],
            "session_yaml":    str(args.session_yaml),
            "is_stderiv":      any("fetD" in str(p) or "spkD" in str(p)
                                   for p in args.log_files),
        },
        "n_clusters": {
            "p05":    int(round(_pct(all_counts, 5))),
            "p25":    int(round(_pct(all_counts, 25))),
            "median": int(round(_pct(all_counts, 50))),
            "p75":    int(round(_pct(all_counts, 75))),
            "p95":    int(round(_pct(all_counts, 95))),
        },
        "effective_dimensionality": {
            "d_eff_p05":    round(float(_pct(d_effs, 5)),  2),
            "d_eff_p25":    round(float(_pct(d_effs, 25)), 2),
            "d_eff_median": round(float(d_eff_median),     2),
            "d_eff_p75":    round(float(_pct(d_effs, 75)), 2),
            "d_eff_p95":    round(float(_pct(d_effs, 95)), 2),
            "merge_thresh_from_median_d_eff": round(merge_thresh_median, 2),
        },
        "inter_cluster_distance": {
            "sym_mahal_p05":    round(ic_p05, 2),
            "sym_mahal_median": round(float(_pct(sym_mahals, 50)) if sym_mahals else 0.0, 2),
        },
        "dim_fisher_ratios":    [round(float(v), 4) for v in fdr],
        "dim_importance_order": importance_order,
        "snr_variance_model": {
            "A":  snr_A,
            "B":  snr_B,
            "r2": snr_r2,
        },
        "penalty_mix_suggested": round(penalty_mix, 4),
        "cluster_types": types,
        "preseed_centres": [round(float(v), 6) for v in preseed_centres],
    }

    with open(out_path, "w") as f:
        yaml.dump(prior, f, default_flow_style=False, sort_keys=False,
                  allow_unicode=True)

    print(f"\nPrior written to: {out_path}")

    # ── Summary of what KlustaKwik will use ───────────────────────────
    print("\nKlustaKwik will apply (when -PriorFile is set):")
    print(f"  MinClusters  <- {max(2, int(round(_pct(all_counts, 5))))}")
    print(f"  MaxClusters  <- {int(round(_pct(all_counts, 95))) + 1}")
    print(f"  MergeThresh  <- {merge_thresh_median:.1f}  (chi2 at median d_eff={d_eff_median:.1f})")
    print(f"  PenaltyMix   <- {penalty_mix:.3f}")
    print(f"  AdaptiveMerge = 1  (per-pair d_eff threshold — no prior needed for this)")
    print()
    print("The adaptive merge threshold is the most impactful change.")
    print("It requires no prior file: KlustaKwik computes d_eff from each")
    print("cluster's fitted covariance and uses chi²(d_eff, 0.9999) per pair.")
    print("The prior file adds empirical calibration and Min/MaxClusters tuning.")


if __name__ == "__main__":
    main()
