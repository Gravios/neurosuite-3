#!/usr/bin/env python3
"""
process_autoklusta.py
=====================
Learn manual spike-sorting curation from (auto, curated) ``.clu`` pairs
and replay it on fresh KlustaKwik output — the worker behind the
``ndm_autoklusta`` ndmanager plugin.

The premise
-----------
You curate KlustaKwik output in Klusters: you DISCARD junk clusters
(relabel them to artifact/MUA), you MERGE over-split fragments of one
unit, and you KEEP the clean ones.  The raw and the curated ``.clu.N``
index the *same spikes* (same ``.fet`` / ``.res`` / ``.spk``), so the
two together encode every decision you made.  This worker recovers
those decisions, fits two classifiers to them, and applies the fitted
policy to an un-curated ``.clu``.

Decisions recovered from a (raw, curated) pair
----------------------------------------------
Build the raw×curated overlap (contingency) matrix.  For each raw
cluster the curated cluster holding most of its spikes is its fate:

  DISCARD   majority landed in a noise cluster (0 artifact, 1 MUA by
            convention; override with ``--noise-clusters``)
  KEEP      majority landed in a real curated unit (id > 1)
  MERGE     two KEPT raw clusters whose majorities point at the SAME
            real curated unit were merged by you

Splits (one raw cluster fanned across several real curated units) are
detected and reported but not replayed — manual splits are spike-level
and cannot be reconstructed from cluster-level features.  Such clusters
surface with a low ``purity`` in the train report.

Two learned models (HistGradientBoosting — NaN-tolerant)
--------------------------------------------------------
  cluster classifier   p(keep)  from per-cluster features
  merge   classifier   p(merge) from per-pair features

Cross-shank / cross-session generalisation
-------------------------------------------
Raw feature coordinates are NOT used as inputs — a Buzsaki64L shank with
25 features and an A32 shank with a different count/scale would never
share a coordinate frame.  Instead every feature is a SCALE-INVARIANT
descriptor of cluster *geometry and spread* in feature space, so a model
fitted on one shank transfers to another:

  per cluster
    log10_n_spikes, log10_rate, frac_total
    log10_iso_dist, log10_l_ratio, isi_viol_frac
    radius_mahal              mean Mahalanobis radius (self-cov)
    spread_ratio              trace(cov_clu) / trace(cov_shank)  (scale-free)
    anisotropy                λ1/λ_d of the cluster covariance
    participation_ratio       (Σλ)²/Σλ²  — effective dimensionality
    top1_var_frac             λ1/Σλ      — how 1-D the cluster is
    nn_gap_mahal              Mahalanobis distance to nearest other unit
    crowding                  radius_mahal / nn_gap_mahal  (<1 isolated)
    wf_snr, wf_chan_conc      (optional, when .spk/.spkD present)

  per pair
    mahal_sym                 symmetric Mahalanobis centroid separation
    mahal_global              centroid separation whitened by SHANK cov
                              (comparable across shanks)
    bhattacharyya             Gaussian overlap (mean + shape)
    split_axis_alignment      |cos∠| between the centroid-join vector and
                              each cluster's principal axis, averaged —
                              over-split fragments separate ALONG their
                              own elongation; distinct units across it
    gap_ratio                 centroid distance / (sum of the two radii
                              measured along the join direction)
    size_ratio, span_iou
    cross_refractory_frac     refractory coincidence between the trains
    combined_isi_viol         ISI contamination of the merged train
    wf_cosine, ptp_ratio      (optional)

All descriptors are scalars regardless of the feature-space dimension,
so the model's input width is fixed across groups, shanks and probes.

Incremental training
---------------------
The fitted bundle carries the extracted (feature, label, group) rows in
a ``store``.  Re-running ``mode=train`` against an existing model loads
that store, appends the new session/groups (skipping any group signature
already present), and refits both classifiers on the full accumulation —
so you keep improving one model across many sessions and shanks without
re-reading earlier data.  ``--reset`` starts a fresh store.

Modes
-----
  train   accumulate (session × group) into --model-path, refit, save
  apply   replay the model: raw .clu  ->  --out-clu-ext .clu
  eval    apply where a curated .clu exists too, score vs the human

This worker is normally invoked by the ``ndm_autoklusta`` driver, which
fills the per-group geometry from the session YAML.  copyright (C) 2025
neurosuite-3 contributors  GPL-3.0-or-later
"""

import argparse
import glob
import json
import os
import struct
import sys
from pathlib import Path

import numpy as np

try:
    from scipy.stats import chi2
except ImportError:
    sys.stderr.write("ERROR: scipy required (pip install scipy)\n")
    sys.exit(1)

try:
    import joblib
    from scipy.stats import chi2 as _chi2
    from sklearn.ensemble import HistGradientBoostingClassifier
    from sklearn.dummy import DummyClassifier
    from sklearn.model_selection import GroupKFold, cross_val_predict
    from sklearn.metrics import (
        roc_auc_score, average_precision_score, classification_report,
        adjusted_rand_score,
    )
except ImportError:
    sys.stderr.write(
        "ERROR: scikit-learn + joblib required "
        "(pip install scikit-learn joblib)\n")
    sys.exit(1)


MODEL_VERSION = 4
EPS = 1e-12

CLUSTER_FEATURES = [
    "log10_n_spikes", "log10_rate_hz", "frac_total",
    "log10_iso_dist", "log10_l_ratio", "isi_viol_frac",
    "radius_mahal", "spread_ratio", "anisotropy",
    "participation_ratio", "top1_var_frac",
    "nn_gap_mahal", "crowding",
    "wf_snr", "wf_chan_conc",
]

MERGE_FEATURES = [
    "mahal_sym", "mahal_global", "bhattacharyya",
    "split_axis_alignment", "gap_ratio",
    "size_ratio", "span_iou",
    "cross_refractory_frac", "combined_isi_viol",
    "wf_cosine", "ptp_ratio",
]

# Split head — trained ONLY from the optional Klusters curation log, whose
# per-cluster snapshots carry this fixed set of quality metrics.  Order is
# load-bearing (model column layout); HistGradientBoosting tolerates NaN, so a
# missing field (e.g. waveform_* when no .spk was open) is left NaN.
SPLIT_FEATURES = [
    "n_spikes", "firing_hz",
    "isi_viol_pct", "isi_viol_pct_1ms", "isi_viol_pct_2ms",
    "isi_burst_pct", "isi_cv", "isi_mean_ms", "isi_median_ms",
    "feat_var_mean", "feat_var_frobenius", "feat_var_top3_mean",
    "feat_skewness_max", "feat_kurtosis_mean", "feat_kurtosis_max",
    "drift_ratio", "temporal_drift_index", "temporal_rate_cv",
    "spike_fraction", "n_clusters_in_group", "nearest_centroid_dist_norm",
    "l_ratio", "isolation_dist",
    "waveform_snr", "waveform_peak_amp", "waveform_width_samp",
    "waveform_asymmetry", "waveform_chan_spread",
]


# ════════════════════════════════════════════════════════════════════════
#  File IO (canonical neurosuite-3 formats; mirrors process_estimatedrift)
# ════════════════════════════════════════════════════════════════════════

def read_res(path: str) -> np.ndarray:
    """.res.N — little-endian int64 timestamps, no header."""
    return np.fromfile(path, dtype="<i8")


def read_clu(path: str) -> np.ndarray:
    """.clu.N — int32 nClusters header (discarded) + int32 id per spike.

    Falls back to legacy ASCII (first line = nClusters) if the leading
    int32 is not a plausible cluster count, so a hand-edited text .clu
    or an externally produced raw file still loads.
    """
    with open(path, "rb") as f:
        head = f.read(4)
    cand = struct.unpack("<i", head)[0] if len(head) == 4 else -1
    if 1 <= cand <= 65535:
        raw = np.fromfile(path, dtype="<i4")
        return raw[1:].astype(np.int64)
    with open(path, "r") as f:
        lines = [ln.strip() for ln in f if ln.strip()]
    return np.array(lines[1:], dtype=np.int64)


def read_fet(path: str):
    """.fet.N — int32 nDimensions header + n_spikes*nDims int64 (row-major),
    or legacy text (line 0 = nDims).  The timestamp column (last) stays.
    Returns (features float64 (n, nDims), nDims)."""
    with open(path, "rb") as f:
        head = f.read(4)
    if len(head) < 4:
        raise ValueError(f"read_fet: file too short: {path}")
    n_dims = struct.unpack("<i", head)[0]
    if 1 <= n_dims <= 256:
        payload = np.fromfile(path, dtype=np.int64, offset=4)
        n = len(payload) // n_dims
        return payload[:n * n_dims].reshape(n, n_dims).astype(np.float64), n_dims
    with open(path, "r") as f:
        n_dims = int(f.readline().strip())
        rows = [list(map(float, ln.split())) for ln in f if ln.strip()]
    return np.array(rows, dtype=np.float64), n_dims


def read_spk(path: str, n_sites: int, n_samp: int):
    """Memory-map .spk(D).N as (k, n_samp, n_sites) int16, sample-major."""
    stride = n_samp * n_sites
    if stride <= 0 or not os.path.isfile(path):
        return None
    k = os.path.getsize(path) // (stride * 2)
    if k <= 0:
        return None
    return np.memmap(path, dtype="<i2", mode="r", shape=(k, n_samp, n_sites))


def write_clu(path: str, labels: np.ndarray):
    """Write canonical binary .clu: int32 nDistinct header + int32 ids.
    Matches Data::saveClusters (src/klusters/src/data.cpp)."""
    labels = np.asarray(labels, dtype=np.int32)
    with open(path, "wb") as f:
        f.write(struct.pack("<i", int(np.unique(labels).size)))
        labels.tofile(f)


# ════════════════════════════════════════════════════════════════════════
#  Geometry / quality primitives
# ════════════════════════════════════════════════════════════════════════

def safe_cov_inv(pts):
    mu = pts.mean(axis=0)
    cov = np.cov(pts.T)
    if cov.ndim == 0:
        cov = np.array([[float(cov)]])
    try:
        inv = np.linalg.inv(cov + 1e-9 * np.eye(cov.shape[0]))
    except np.linalg.LinAlgError:
        inv = np.linalg.pinv(cov)
    return mu, cov, inv


def mahal_sq(pts, mu, inv):
    d = pts - mu
    return np.einsum("ij,jk,ik->i", d, inv, d)


def isolation_distance(feats, labels, cid):
    mask = labels == cid
    n_c = int(mask.sum())
    nd = feats.shape[1]
    if n_c < nd + 2:
        return np.nan
    other = feats[(~mask) & (labels > 1)]
    if len(other) < n_c:
        return np.nan
    mu, _, inv = safe_cov_inv(feats[mask])
    return float(np.sort(mahal_sq(other, mu, inv))[n_c - 1])


def l_ratio(feats, labels, cid):
    nd = feats.shape[1]
    mask = labels == cid
    n_c = int(mask.sum())
    if n_c < nd + 2:
        return np.nan
    other = feats[(~mask) & (labels > 1)]
    if len(other) == 0:
        return 0.0
    mu, _, inv = safe_cov_inv(feats[mask])
    d2 = mahal_sq(other, mu, inv)
    return float(np.sum(1.0 - chi2.cdf(d2, df=nd)) / n_c)


def isi_violation_frac(times, sr, refractory_ms=2.0):
    t = np.sort(np.asarray(times, dtype=np.float64))
    if t.size < 3:
        return np.nan
    isi_ms = np.diff(t) / sr * 1000.0
    return float(np.mean(isi_ms < refractory_ms))


def bhattacharyya(mu1, cov1, mu2, cov2):
    nd = len(mu1)
    cov = 0.5 * (cov1 + cov2) + 1e-9 * np.eye(nd)
    dmu = (mu1 - mu2).reshape(-1, 1)
    try:
        inv = np.linalg.inv(cov)
        _, ld1 = np.linalg.slogdet(cov1 + 1e-9 * np.eye(nd))
        _, ld2 = np.linalg.slogdet(cov2 + 1e-9 * np.eye(nd))
        _, ldc = np.linalg.slogdet(cov)
    except np.linalg.LinAlgError:
        return np.nan
    return 0.125 * float((dmu.T @ inv @ dmu).item()) + 0.5 * (ldc - 0.5 * (ld1 + ld2))


def cross_refractory_frac(ta, tb, sr, refractory_ms=2.0):
    ta = np.sort(ta.astype(np.float64))
    tb = np.sort(tb.astype(np.float64))
    if ta.size == 0 or tb.size == 0:
        return np.nan
    win = refractory_ms / 1000.0 * sr
    small, big = (ta, tb) if ta.size <= tb.size else (tb, ta)
    idx = np.searchsorted(big, small)
    coincident = 0
    for i, s in enumerate(small):
        j = idx[i]
        nearest = np.inf
        if j < big.size:
            nearest = min(nearest, abs(big[j] - s))
        if j > 0:
            nearest = min(nearest, abs(s - big[j - 1]))
        if nearest < win:
            coincident += 1
    return coincident / small.size


def span_iou(amin, amax, bmin, bmax):
    inter = max(0.0, min(amax, bmax) - max(amin, bmin))
    union = max(amax, bmax) - min(amin, bmin)
    return inter / union if union > 0 else 0.0


# ════════════════════════════════════════════════════════════════════════
#  Per-group loaded bundle
# ════════════════════════════════════════════════════════════════════════

class Group:
    """One spikeDetection group, all arrays length-aligned."""

    def __init__(self, session, group, raw_clu_path, curated_clu_path,
                 n_chan, n_samp, sr, noise, load_curated, use_waveforms,
                 refractory_ms):
        self.session = Path(session)
        self.group = group
        self.name = f"{self.session.name}:{group}"
        self.sr = float(sr)
        self.noise = set(int(c) for c in noise)
        self.refractory_ms = refractory_ms

        feats_all, n_dims = read_fet(fet_file(self.session, group))
        res_fp = resolve_input(self.session, "res", group, ["", "stderiv", "D"])
        res_raw = read_res(res_fp) if os.path.isfile(res_fp) else None
        self.raw = read_clu(raw_clu_path)
        self.cur = read_clu(curated_clu_path) if load_curated else None

        lengths = [len(feats_all), len(self.raw)]
        if res_raw is not None:
            lengths.append(len(res_raw))
        if self.cur is not None:
            lengths.append(len(self.cur))
        n = min(lengths)
        if len(set(lengths)) != 1:
            sys.stderr.write(f"  [{self.name}] length mismatch {lengths}; "
                             f"truncating to {n}\n")
        self.feats = feats_all[:n, :-1].astype(np.float64)
        self.ts = feats_all[:n, -1].astype(np.float64)
        # .res holds original detection times; when absent (some stderiv
        # pipelines keep times only in the .fet timestamp column) derive
        # them from that column — Klusters treats .fet ts as authoritative.
        if res_raw is not None:
            self.res = res_raw[:n].astype(np.int64)
        else:
            self.res = np.rint(self.ts).astype(np.int64)
        self.raw = self.raw[:n].astype(np.int64)
        if self.cur is not None:
            self.cur = self.cur[:n].astype(np.int64)
        self.n_dims = self.feats.shape[1]

        self.wf = None
        if use_waveforms:
            spk_fp = resolve_input(self.session, "spk", group, PREFER_DERIVED)
            wf = read_spk(spk_fp, n_chan, n_samp)
            if wf is not None and len(wf) >= n:
                self.wf = wf[:n]
            else:
                # Don't silently fall back to geometry-only features: report
                # exactly why the waveforms were dropped (missing file, bad
                # per-group dims, or fewer spikes in .spk than in .fet/.clu).
                exists = os.path.isfile(spk_fp)
                size = os.path.getsize(spk_fp) if exists else 0
                stride = max(int(n_chan), 0) * max(int(n_samp), 0)
                k = size // (stride * 2) if stride > 0 else 0
                sys.stderr.write(
                    f"  [{self.name}] waveforms disabled: "
                    f"{'file not found ' if not exists else ''}"
                    f"{os.path.basename(spk_fp)} "
                    f"(size={size}B, n_sites={n_chan}, n_samp={n_samp}, "
                    f"stride={stride} int16/spike, implied spikes={k}, "
                    f"need {n}); check this group's channel/sample counts "
                    f"in the YAML\n")

        # .fet ts is authoritative window position (realign writes cumulative
        # shift there); fall back to .res if it is not a real clock.
        self.time = self.ts if np.ptp(self.ts) > 0 else self.res.astype(np.float64)
        self.duration_s = max((self.time.max() - self.time.min()) / self.sr, 1e-6)

        # Shank-level reference geometry: covariance over all real spikes,
        # used to whiten cross-cluster separations into a scale that is
        # comparable across shanks.
        real_mask = ~np.isin(self.raw, list(self.noise))
        ref_pts = self.feats[real_mask] if real_mask.sum() > self.n_dims + 1 \
            else self.feats
        _, self.shank_cov, self.shank_inv = safe_cov_inv(ref_pts)
        self.shank_trace = float(np.trace(self.shank_cov)) + EPS

        ids = np.unique(self.raw)
        self.clusters = [int(c) for c in ids if int(c) not in self.noise]

    def mask(self, cid):
        return self.raw == cid


def cluster_stats(g: Group):
    stats = {}
    total = len(g.raw)
    for cid in g.clusters:
        m = g.mask(cid)
        n = int(m.sum())
        pts = g.feats[m]
        if n >= max(2, g.n_dims):
            mu, cov, inv = safe_cov_inv(pts)
            evals = np.clip(np.linalg.eigvalsh(cov), EPS, None)
        else:
            mu = pts.mean(axis=0) if n else np.zeros(g.n_dims)
            cov = np.eye(g.n_dims); inv = np.eye(g.n_dims)
            evals = np.ones(g.n_dims)
        evals = np.sort(evals)[::-1]
        tm = g.time[m]
        st = dict(
            n=n, frac=n / total if total else 0.0,
            mu=mu, cov=cov, inv=inv, evals=evals,
            trace=float(np.trace(cov)),
            tmin=float(tm.min()) if n else 0.0,
            tmax=float(tm.max()) if n else 0.0,
            res=g.res[m].astype(np.float64),
        )
        d2 = mahal_sq(pts, mu, inv) if n else np.array([])
        st["radius"] = float(np.sqrt(np.mean(d2))) if d2.size else np.nan
        if g.wf is not None and n:
            tmpl = np.asarray(g.wf[m]).mean(axis=0)
            ptp_chan = tmpl.max(axis=0) - tmpl.min(axis=0)
            st["template"] = tmpl
            st["ptp"] = float(ptp_chan.max())
            noise = float(np.median(np.std(np.asarray(g.wf[m]), axis=0))) + EPS
            st["snr"] = st["ptp"] / noise
            pp = ptp_chan / (ptp_chan.sum() + EPS)
            st["chan_conc"] = float(np.max(pp))
        stats[cid] = st

    # Nearest-neighbour Mahalanobis gap (symmetric) per cluster.
    cids = g.clusters
    for cid in cids:
        sa = stats[cid]
        best = np.inf
        for oid in cids:
            if oid == cid:
                continue
            sb = stats[oid]
            d_ab = float((sb["mu"] - sa["mu"]) @ sa["inv"] @ (sb["mu"] - sa["mu"]))
            d_ba = float((sa["mu"] - sb["mu"]) @ sb["inv"] @ (sa["mu"] - sb["mu"]))
            best = min(best, np.sqrt(0.5 * (d_ab + d_ba)))
        sa["nn_gap"] = best if np.isfinite(best) else np.nan
    return stats


def cluster_feature_row(g: Group, cid, st):
    iso = isolation_distance(g.feats, g.raw, cid)
    lr = l_ratio(g.feats, g.raw, cid)
    isi = isi_violation_frac(st["res"], g.sr, g.refractory_ms)
    evals = st["evals"]
    aniso = float(evals[0] / (evals[-1] + EPS))
    pr = float((evals.sum() ** 2) / (np.sum(evals ** 2) + EPS))
    top1 = float(evals[0] / (evals.sum() + EPS))
    nn = st.get("nn_gap", np.nan)
    crowd = (st["radius"] / nn) if (nn and np.isfinite(nn) and nn > 0
                                    and np.isfinite(st["radius"])) else np.nan
    row = [
        np.log10(max(st["n"], 1)),
        np.log10(max(st["n"] / g.duration_s, EPS)),
        st["frac"],
        np.log10(iso) if (iso is not None and iso > 0) else np.nan,
        np.log10(lr + 1e-6) if lr is not None else np.nan,
        isi if isi is not None else np.nan,
        st["radius"],
        st["trace"] / g.shank_trace,
        aniso, pr, top1, nn, crowd,
    ]
    if "snr" in st:
        row += [st["snr"], st["chan_conc"]]
    else:
        row += [np.nan, np.nan]
    return np.array(row, dtype=np.float64)


def merge_feature_row(g: Group, a, b, sa, sb):
    mu_a, mu_b = sa["mu"], sb["mu"]
    join = mu_b - mu_a
    d_ab = float(join @ sa["inv"] @ join)
    d_ba = float(join @ sb["inv"] @ join)
    mahal_sym = np.sqrt(0.5 * (d_ab + d_ba))
    d_glob = float(join @ g.shank_inv @ join)
    mahal_global = np.sqrt(max(d_glob, 0.0))
    bdist = bhattacharyya(mu_a, sa["cov"], mu_b, sb["cov"])

    # Split-axis alignment: |cos| between the join direction and each
    # cluster's principal eigenvector, averaged.  ~1 => the two sit along
    # one cluster's elongation (over-split fragment); ~0 => separated
    # across their shapes (distinct units).
    jn = np.linalg.norm(join) + EPS
    jhat = join / jn
    align = np.nan
    try:
        wa, Va = np.linalg.eigh(sa["cov"])
        wb, Vb = np.linalg.eigh(sb["cov"])
        pa = Va[:, int(np.argmax(wa))]
        pb = Vb[:, int(np.argmax(wb))]
        align = 0.5 * (abs(float(jhat @ pa)) + abs(float(jhat @ pb)))
    except np.linalg.LinAlgError:
        pass

    # Gap ratio: centroid distance vs the two radii measured along join.
    ra = np.sqrt(max(float(jhat @ sa["cov"] @ jhat), EPS))
    rb = np.sqrt(max(float(jhat @ sb["cov"] @ jhat), EPS))
    gap_ratio = jn / (ra + rb + EPS)

    size_ratio = min(sa["n"], sb["n"]) / max(sa["n"], sb["n"])
    iou = span_iou(sa["tmin"], sa["tmax"], sb["tmin"], sb["tmax"])
    cross = cross_refractory_frac(sa["res"], sb["res"], g.sr, g.refractory_ms)
    combined = isi_violation_frac(np.concatenate([sa["res"], sb["res"]]),
                                  g.sr, g.refractory_ms)

    if "template" in sa and "template" in sb:
        va = sa["template"].ravel(); vb = sb["template"].ravel()
        cos = float(va @ vb / (np.linalg.norm(va) * np.linalg.norm(vb) + EPS))
        ptp_ratio = min(sa["ptp"], sb["ptp"]) / (max(sa["ptp"], sb["ptp"]) + EPS)
    else:
        cos, ptp_ratio = np.nan, np.nan

    return np.array([
        mahal_sym, mahal_global, bdist if bdist is not None else np.nan,
        align, gap_ratio, size_ratio, iou,
        cross if cross is not None else np.nan,
        combined if combined is not None else np.nan,
        cos, ptp_ratio,
    ], dtype=np.float64)


def recover_actions(g: Group):
    fate, keep, purity = {}, {}, {}
    for cid in g.clusters:
        man = g.cur[g.mask(cid)]
        if man.size == 0:
            fate[cid], keep[cid], purity[cid] = -1, False, 0.0
            continue
        vals, counts = np.unique(man, return_counts=True)
        top = int(vals[np.argmax(counts)])
        fate[cid] = top
        purity[cid] = float(counts.max() / counts.sum())
        keep[cid] = top not in g.noise
    return fate, keep, purity


# ════════════════════════════════════════════════════════════════════════
#  Optional Klusters curation-log input (split supervision)
# ════════════════════════════════════════════════════════════════════════
#
# Klusters appends a per-group JSON-Lines action log,
# <session>.curation_log.<group>.<method>, recording every GROUP/SPLIT/DELETE/...
# action with before/after cluster snapshots.  Unlike endpoint-diffing the
# (raw, curated) .clu pair, this captures SPLITs — a raw cluster the human
# fanned into several units — which endpoint-diffing can detect (low purity)
# but never replay.
#
# The log is append-mode across every reopen, and a reopen on a freshly
# re-clustered .clu starts a brand-new cluster numbering while the old
# sessions stay in the file.  Only the LAST run of sessions whose cluster
# count chains across boundaries (end[i] == start[i+1]) pertains to the
# current .clu; earlier sessions are abandoned attempts and are discarded.
#
# SPLIT is the only action with an authoritative ACTION_DETAIL record
# (source_cluster + accepted/rejected), so the split head is trained on those
# source-cluster snapshots; GROUP/DELETE are tallied for reporting only (they
# are already covered by the endpoint-diff merge/discard heads).

def curation_log_path(args, group):
    """Resolve the optional curation log for a group, or None when disabled.

    --curation-log: ""/false -> off; true/auto -> the staged Klusters log
    <session>.curation_log.<group>.<method> (matched by suffix); a directory
    -> the matching log in that dir; else an explicit path (may contain {g})."""
    v = (getattr(args, "curation_log", "") or "").strip()
    if v == "" or v.lower() in ("false", "no", "0", "off"):
        return None

    def _staged_log(dirpath):
        # Klusters names the log for the clustering stage,
        # <session>.curation_log.<group>.<method> (no ".jl"). Match whichever
        # staged log exists for this group, ignoring .bak backups.
        stem = os.path.basename(args.session)
        pat = os.path.join(dirpath, f"{stem}.curation_log.{group}.*")
        hits = sorted(p for p in glob.glob(pat) if not p.endswith(".bak"))
        return hits[0] if hits else None

    if v.lower() in ("true", "auto", "yes", "on", "1"):
        return _staged_log(os.path.dirname(args.session) or ".")
    if os.path.isdir(v):
        return _staged_log(v)
    return v.replace("{g}", str(group))


def _clog_sessions(path):
    """Parse the JSONL log and segment into sessions (file order)."""
    sessions, cur = [], None
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                o = json.loads(line)
            except Exception:                       # noqa: BLE001
                continue
            ev = o.get("event")
            if ev == "SESSION_OPEN":
                cur = {"open": o, "rows": []}
                sessions.append(cur)
            elif cur is not None and (ev == "ACTION_DETAIL"
                                      or o.get("role") in ("source", "result")):
                cur["rows"].append(o)
    return sessions


def _clog_ncl(session, which):
    vals = [r["n_clusters_in_group"] for r in session["rows"]
            if "n_clusters_in_group" in r]
    if not vals:
        return None
    return vals[0] if which == "first" else vals[-1]


def _clog_last_block(sessions):
    """Indices of the last contiguous run of non-empty sessions whose
    n_clusters_in_group chains across boundaries."""
    nonempty = [i for i, s in enumerate(sessions)
                if any("n_clusters_in_group" in r for r in s["rows"])]
    if not nonempty:
        return []
    block = [nonempty[-1]]
    for k in range(len(nonempty) - 2, -1, -1):
        i, j = nonempty[k], block[0]
        if _clog_ncl(sessions[i], "last") == _clog_ncl(sessions[j], "first"):
            block.insert(0, i)
        else:
            break
    return block


def split_feature_row(row):
    def f(v):
        try:
            return float(v)
        except (TypeError, ValueError):
            return np.nan
    return np.array([f(row.get(k)) for k in SPLIT_FEATURES], dtype=np.float64)


def parse_curation_log(path):
    """Extract split supervision from the LAST contiguous block of the log.

    Returns dict(n_sessions, block, examples, tally) where examples is a list of
    (feature_row_dict, label, session_id): label=1 if the human ACCEPTED an
    algorithmic split of that source cluster, 0 = rejected or never split."""
    sessions = _clog_sessions(path)
    block = _clog_last_block(sessions)
    examples = []
    tally = dict(SPLIT_accept=0, SPLIT_reject=0, GROUP=0, DELETE=0, not_split=0)
    for i in block:
        rows = sessions[i]["rows"]
        sid = sessions[i]["open"].get("session_id", "?")
        befores = {(r["action_idx"], r["cluster"]): r
                   for r in rows if r.get("phase") == "before"}
        pos, neg = set(), set()
        for r in rows:
            if r.get("event") == "ACTION_DETAIL" and \
               r.get("action") in ("SPLIT", "SPLIT_N"):
                key = (r["action_idx"], r.get("source_cluster"))
                if r.get("status") == "accepted":
                    pos.add(key); tally["SPLIT_accept"] += 1
                else:
                    neg.add(key); tally["SPLIT_reject"] += 1
        for r in rows:
            if r.get("phase") == "before":
                a = r.get("action")
                if a == "GROUP":
                    tally["GROUP"] += 1
                elif a in ("DELETE_NOISE", "DELETE_ARTEFACT"):
                    tally["DELETE"] += 1
        seen = set()
        for key, row in befores.items():
            if key in pos:
                label = 1
            else:
                label = 0
                if key not in neg:
                    tally["not_split"] += 1
            dedup = (sid, row.get("cluster"), row.get("n_spikes"), label)
            if dedup in seen:
                continue
            seen.add(dedup)
            examples.append((row, label, sid))
    return dict(n_sessions=len(sessions), block=block,
                examples=examples, tally=tally)


# ════════════════════════════════════════════════════════════════════════
#  Model store helpers (incremental training)
# ════════════════════════════════════════════════════════════════════════

def empty_store():
    return dict(
        cluster_X=[], cluster_y=[], cluster_g=[],
        merge_X=[], merge_y=[], merge_g=[],
        split_X=[], split_y=[], split_g=[], split_seen=[],
        seen_groups=[],
    )


def load_bundle(path):
    if path and os.path.isfile(path):
        try:
            return joblib.load(path)
        except Exception as e:                   # noqa: BLE001
            sys.stderr.write(f"WARNING: could not load model {path}: {e}\n")
    return None


def fit_classifier(name, X, y, groups):
    X = np.asarray(X, dtype=np.float64)
    y = np.asarray(y, dtype=np.int64)
    groups = np.asarray(groups)
    classes = np.unique(y)
    if len(y) == 0:
        return None
    if classes.size < 2:
        clf = DummyClassifier(strategy="constant", constant=int(classes[0]))
        clf.fit(X, y)
        print(f"  [{name}] only class {classes.tolist()} present -> "
              f"constant predictor")
        return clf

    w = np.ones(len(y))
    for c in classes:
        w[y == c] = len(y) / (classes.size * np.sum(y == c))

    clf = HistGradientBoostingClassifier(
        max_iter=300, learning_rate=0.06, l2_regularization=1.0,
        early_stopping=False, random_state=0)

    n_groups = len(set(groups.tolist()))
    print(f"  [{name}] {len(y)} samples, {X.shape[1]} features, "
          f"{n_groups} (session:group) fold(s)")
    if n_groups >= 2 and min(np.bincount(y)) >= 2:
        n_splits = min(n_groups, 5)
        try:
            proba = cross_val_predict(
                clf, X, y, groups=groups, cv=GroupKFold(n_splits=n_splits),
                method="predict_proba", params={"sample_weight": w})[:, 1]
            print(f"    grouped {n_splits}-fold CV: "
                  f"ROC-AUC={roc_auc_score(y, proba):.3f}  "
                  f"PR-AUC={average_precision_score(y, proba):.3f}")
            print("    " + classification_report(
                y, (proba >= 0.5).astype(int), zero_division=0
            ).replace("\n", "\n    "))
        except Exception as e:                   # noqa: BLE001
            print(f"    CV skipped ({e})")
    else:
        print("    (CV needs >=2 folds and >=2 per class; fitting on all)")
    clf.fit(X, y, sample_weight=w)
    return clf


# ════════════════════════════════════════════════════════════════════════
#  Apply
# ════════════════════════════════════════════════════════════════════════

def _proba_keep(clf, X):
    if hasattr(clf, "predict_proba"):
        try:
            return clf.predict_proba(X)[:, 1]
        except Exception:                        # noqa: BLE001
            pass
    return clf.predict(X).astype(float)


def _connected_components(nodes, edges):
    parent = {n: n for n in nodes}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for a, b in edges:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra
    comps = {}
    for n in nodes:
        comps.setdefault(find(n), []).append(n)
    return list(comps.values())


def cluster_centroids(g: Group):
    """Per-cluster feature-space centroids over ALL clusters (incl. noise),
    mirroring Data::computeAllCentroids (mean over the nFeat feature cols)."""
    cents = {}
    for cid in np.unique(g.raw):
        m = g.raw == cid
        if m.any():
            cents[int(cid)] = g.feats[m].mean(axis=0)
    return cents


def split_feature_vec(fd):
    return np.array([fd.get(k, np.nan) for k in SPLIT_FEATURES], dtype=np.float64)


def compute_split_features(g: Group, cid, centroids, isi_thresh_ms):
    """Recompute the curation-log snapshot metrics (SPLIT_FEATURES) for a raw
    cluster from .fet/.spk, faithfully mirroring Klusters' Data::computeSnapshot,
    so a split head trained on the log applies to the same feature space.
    Returns a vector aligned to SPLIT_FEATURES (NaN where undefined — HGB
    tolerates NaN)."""
    f = {k: np.nan for k in SPLIT_FEATURES}
    mask = g.raw == cid
    n = int(mask.sum())
    n_tot = len(g.raw)
    nFeat = g.feats.shape[1]
    f["n_spikes"] = float(n)
    f["spike_fraction"] = (n / n_tot) if n_tot else np.nan
    f["n_clusters_in_group"] = float(len(centroids))
    if n == 0:
        return split_feature_vec(f)

    order = np.argsort(g.ts[mask].astype(np.float64))
    ts = (g.ts[mask].astype(np.float64) / g.sr)[order]      # seconds, time-sorted
    feats_t = g.feats[mask].astype(np.float64)[order]        # rows in time order
    feats = g.feats[mask].astype(np.float64)

    # A. firing rate
    if n >= 2:
        span = ts[-1] - ts[0]
        f["firing_hz"] = (n / span) if span > 0 else 0.0
    # B. ISI distribution (ms)
    if n >= 2:
        isi = np.diff(ts) * 1000.0
        nPairs = isi.size
        f["isi_viol_pct"]     = 100.0 * np.count_nonzero(isi < isi_thresh_ms) / nPairs
        f["isi_viol_pct_1ms"] = 100.0 * np.count_nonzero(isi < 1.0) / nPairs
        f["isi_viol_pct_2ms"] = 100.0 * np.count_nonzero(isi < 2.0) / nPairs
        f["isi_burst_pct"]    = 100.0 * np.count_nonzero(isi < 10.0) / nPairs
        mean_isi = float(isi.mean())
        f["isi_mean_ms"] = mean_isi
        s = np.sort(isi); mid = nPairs // 2
        if nPairs % 2 == 1:
            f["isi_median_ms"] = float(s[mid])
        else:
            f["isi_median_ms"] = float(0.5 * (s[mid] + s[mid + 1])) \
                if mid + 1 < nPairs else float(s[mid])
        if mean_isi > 0:
            var = float((isi * isi).mean() - mean_isi * mean_isi)
            f["isi_cv"] = (np.sqrt(var) / mean_isi) if var > 0 else 0.0
    # E. temporal rate CV (10 equal-time bins)
    if n >= 2:
        span = ts[-1] - ts[0]
        if span > 0:
            bins = np.minimum(((ts - ts[0]) / span * 10).astype(int), 9)
            counts = np.bincount(bins, minlength=10).astype(np.float64)
            meanC = counts.mean()
            if meanC > 0:
                var = float((counts * counts).mean() - meanC * meanC)
                f["temporal_rate_cv"] = (np.sqrt(var) / meanC) if var > 0 else 0.0
    # Pass 2: per-feature moments
    frob = np.nan
    if n >= 2 and nFeat > 0:
        mean = feats.mean(axis=0)
        d = feats - mean
        m2 = (d * d).sum(axis=0) / (n - 1)          # sample variance
        m3 = (d ** 3).sum(axis=0) / n               # population 3rd moment
        m4 = (d ** 4).sum(axis=0) / n               # population 4th moment
        f["feat_var_mean"] = float(m2.mean())
        frob = float(np.sqrt((m2 * m2).sum()))
        f["feat_var_frobenius"] = frob
        vmin, vmax = float(m2.min()), float(m2.max())
        f["drift_ratio"] = (vmax / vmin) if vmin > 0 else 0.0
        top = np.sort(m2)[::-1][:min(3, nFeat)]
        f["feat_var_top3_mean"] = float(top.mean())
        pos = m2 > 0
        with np.errstate(invalid="ignore", divide="ignore"):
            skew = np.where(pos, m3 / np.power(m2, 1.5), 0.0)
            excess = m4 / (m2 * m2) - 3.0
        f["feat_skewness_max"] = float(np.max(np.abs(skew[pos]))) if pos.any() else 0.0
        f["feat_kurtosis_mean"] = float(np.where(pos, excess, 0.0).sum() / nFeat)
        f["feat_kurtosis_max"] = float(np.max(excess[pos])) if pos.any() else 0.0
        # 2e. temporal drift: first-half vs second-half centroid (time order)
        if n >= 4 and frob > 0:
            half = n // 2
            dist = float(np.linalg.norm(feats_t[half:].mean(axis=0)
                                        - feats_t[:half].mean(axis=0)))
            f["temporal_drift_index"] = dist / frob
        # G. nearest-cluster centroid (normalised)
        if len(centroids) > 1 and cid in centroids and frob > 0:
            own = centroids[cid]; best = np.inf
            for k, c in centroids.items():
                if k == cid or c.shape != own.shape:
                    continue
                dd = float(np.sum((own - c) ** 2))
                if dd < best:
                    best = dd
            if np.isfinite(best):
                f["nearest_centroid_dist_norm"] = np.sqrt(best) / frob
        # J. L-ratio and isolation distance (diagonal Mahalanobis)
        if frob > 0:
            vbase = m2.copy()
            if nFeat > 32:                          # Klusters logs only 32 dims;
                vbase[32:] = f["feat_var_mean"]     # beyond that it uses the mean
            invVar = np.where(vbase > 1e-12, 1.0 / np.where(vbase > 1e-12, vbase, 1.0), 0.0)
            other = g.feats[~mask].astype(np.float64)
            if invVar.any() and other.shape[0] > 0:
                D2 = (((other - mean) ** 2) * invVar).sum(axis=1)
                f["l_ratio"] = float(_chi2.sf(D2, nFeat).sum() / n)
                if D2.size >= n:
                    f["isolation_dist"] = float(np.sqrt(np.partition(D2, n - 1)[n - 1]))
    # H. waveform morphology (mean waveform of the cluster's spikes)
    if g.wf is not None:
        mw = g.wf[mask].mean(axis=0).astype(np.float64)     # (n_samp, n_chan)
        nSamp, nChan = mw.shape
        chanAmp = mw.max(axis=0) - mw.min(axis=0)
        maxI = mw.argmax(axis=0); minI = mw.argmin(axis=0)
        bestCh = int(np.argmax(chanAmp)); bestAmp = float(chanAmp[bestCh])
        f["waveform_peak_amp"] = bestAmp
        f["waveform_chan_spread"] = float(np.count_nonzero(chanAmp >= 0.25 * bestAmp)) \
            if bestAmp > 0 else 0.0
        f["waveform_width_samp"] = float(abs(int(maxI[bestCh]) - int(minI[bestCh])))
        posVal = float(mw[maxI[bestCh], bestCh]); negVal = float(mw[minI[bestCh], bestCh])
        denom = posVal - negVal
        if abs(denom) > 1e-9:
            f["waveform_asymmetry"] = (posVal + negVal) / denom
        nBase = min(4, nSamp)
        baseRms = float(np.sqrt((mw[:nBase, bestCh] ** 2).mean()))
        if baseRms > 0:
            f["waveform_snr"] = bestAmp / (2.0 * baseRms)
    return split_feature_vec(f)


def apply_group(bundle, g: Group, keep_thr, merge_thr, split_thr=0.5):
    stats = cluster_stats(g)
    cclf = bundle["cluster_clf"]
    mclf = bundle["merge_clf"]

    keep_p = {}
    if g.clusters and cclf is not None:
        Xc = np.array([cluster_feature_row(g, c, stats[c]) for c in g.clusters])
        keep_p = dict(zip(g.clusters, _proba_keep(cclf, Xc)))
    kept = [c for c in g.clusters if keep_p.get(c, 0.0) >= keep_thr]
    discarded = [c for c in g.clusters if c not in kept]

    edges = []
    if len(kept) >= 2 and mclf is not None:
        pairs, rows = [], []
        for i in range(len(kept)):
            for j in range(i + 1, len(kept)):
                a, b = kept[i], kept[j]
                pairs.append((a, b))
                rows.append(merge_feature_row(g, a, b, stats[a], stats[b]))
        pm = _proba_keep(mclf, np.array(rows))
        edges = [pairs[k] for k in range(len(pairs)) if pm[k] >= merge_thr]

    comps = _connected_components(kept, edges)
    comps.sort(key=lambda c: min(stats[x]["tmin"] for x in c))

    new = g.raw.copy()
    out_id = 2
    cid_to_new = {}
    for comp in comps:
        for c in comp:
            cid_to_new[c] = out_id
        out_id += 1
    for cid in g.clusters:
        m = g.mask(cid)
        new[m] = cid_to_new.get(cid, 1)          # discarded -> MUA(1)

    # Split head (optional; from the curation log).  We do NOT auto-split —
    # autoklusta has no split operator — we flag kept clusters whose recomputed
    # quality metrics the model predicts the human would have split, for review
    # in Klusters.
    split_cand = []
    sclf = bundle.get("split_clf")
    if sclf is not None and kept and bundle.get("split_features"):
        cents = cluster_centroids(g)
        isi_thr = bundle.get("config", {}).get("refractory_ms", g.refractory_ms)
        Xs = np.array([compute_split_features(g, c, cents, isi_thr) for c in kept])
        ps = _proba_keep(sclf, Xs)
        split_cand = [(c, float(p)) for c, p in zip(kept, ps) if p >= split_thr]
        split_cand.sort(key=lambda x: -x[1])

    report = dict(
        group=g.group, n_raw_real=len(g.clusters), n_kept=len(kept),
        n_discarded=len(discarded), n_final=len(comps),
        merges=[sorted(c) for c in comps if len(c) > 1],
        discarded=sorted(discarded),
        split_candidates=split_cand,
    )
    return new, report


# ════════════════════════════════════════════════════════════════════════
#  Per-mode drivers
# ════════════════════════════════════════════════════════════════════════

def raw_clu_path(args, g):
    """Resolve the un-curated .clu for group g.

    Klusters orders cluster files SESSION.clu.<group>.<label> (e.g.
    .clu.5.K for raw KlustaKwik output, .clu.5.final for the curated
    version) — the group number always precedes the trailing label.
    --raw-clu-ext is that label; empty means the plain SESSION.clu.N."""
    if args.raw_clu_ext:
        return f"{args.session}.clu.{g}.{args.raw_clu_ext}"
    return f"{args.session}.clu.{g}"


def curated_clu_path(args, g):
    """Resolve the manually curated .clu for group g (Klusters ordering)."""
    if args.curated_clu_ext:
        return f"{args.session}.clu.{g}.{args.curated_clu_ext}"
    return f"{args.session}.clu.{g}"


def out_clu_path(args, g):
    """Resolve where mode=apply writes the curated .clu for group g
    (Klusters ordering SESSION.clu.<group>.<label>)."""
    if args.out_clu_ext:
        return f"{args.session}.clu.{g}.{args.out_clu_ext}"
    return f"{args.session}.clu.{g}"


def resolve_input(session, ftype, group, prefer):
    """Resolve a per-group typed input file, mirroring neurosuite-core's
    neurofileio::resolveInput.  The group is ALWAYS the trailing token; a
    variant, when present, sits between the type and the group:

        <base>.<type>.<group>             canonical (no variant)
        <base>.<type>.<variant>.<group>   dotted variant (preferred new form)
        <base>.<type><variant>.<group>    legacy glued (.fetD/.spkD/.pcaD)

    `prefer` lists variants in order; "" denotes the canonical form.  For each
    non-empty variant the dotted form is probed first, then the legacy glued
    form.  Returns the first existing path, or the canonical path if none
    exists (so the caller's os.path.isfile check still reports it missing).
    """
    base = str(session)
    g = str(group)
    canonical = f"{base}.{ftype}.{g}"
    for v in prefer:
        if v == "":
            if os.path.isfile(canonical):
                return canonical
            continue
        dotted = f"{base}.{ftype}.{v}.{g}"
        if os.path.isfile(dotted):
            return dotted
        glued = f"{base}.{ftype}{v}.{g}"   # legacy, e.g. .fetD.N
        if os.path.isfile(glued):
            return glued
    return canonical


# Prefer a derived representation (dotted .stderiv / .D, or legacy glued .D),
# else canonical — the order Klusters uses when it favours .fetD/.spkD.
PREFER_DERIVED = ["stderiv", "D", ""]


def fet_file(session, g):
    """Feature file for group g.  Prefers a derived representation
    (.fet.stderiv.N / legacy .fetD.N) over canonical .fet.N, matching
    Klusters and the neurosuite-core resolver.  The feature/waveform/res
    files are SHARED across .clu variants — only .clu carries the tag."""
    return resolve_input(session, "fet", g, PREFER_DERIVED)


def iter_groups(args, load_curated):
    n_samp = [int(x) for x in args.n_samples_per_group.split(",") if x != ""]
    n_chan = [int(x) for x in args.n_channels_per_group.split(",") if x != ""]
    noise = [int(x) for x in args.noise_clusters.split(",") if x != ""]
    use_wf = args.use_waveforms.lower() in ("1", "true", "yes")
    # --group N restricts the run to a single group; 0/absent = all groups.
    groups = ([args.group] if args.group and args.group > 0
              else range(1, args.n_groups + 1))
    for gi in groups:
        raw_path = raw_clu_path(args, gi)
        cur_path = curated_clu_path(args, gi)
        fet_path = fet_file(args.session, gi)
        # Required: raw .clu and the feature file (.fetD.N preferred, else
        # .fet.N).  .res.N is optional — derived from the .fet ts column when
        # missing (see Group).  The .clu tag never applies to .fet/.spk/.res.
        missing = []
        if not os.path.isfile(raw_path):
            missing.append(os.path.basename(raw_path))
        if not os.path.isfile(fet_path):
            base = os.path.basename(args.session)
            missing.append(f"{base}.fet[.stderiv].{gi} / .fetD.{gi} / .fet.{gi}")
        if missing:
            if args.group and args.group > 0:
                print(f"  group {gi}: missing {', '.join(missing)}")
            continue
        if load_curated and not os.path.isfile(cur_path):
            print(f"  group {gi}: no curated {os.path.basename(cur_path)} "
                  f"— skipping")
            continue
        ns = n_samp[gi - 1] if gi - 1 < len(n_samp) else 32
        nc = n_chan[gi - 1] if gi - 1 < len(n_chan) else args.n_channels
        try:
            yield Group(args.session, gi, raw_path, cur_path, nc, ns,
                        args.sampling_rate, noise, load_curated,
                        use_wf, args.refractory_ms)
        except Exception as e:                   # noqa: BLE001
            print(f"  group {gi}: load failed ({e}) — skipping")


def mode_train(args):
    if args.raw_clu_ext == args.curated_clu_ext:
        sys.stderr.write(
            "ERROR: train needs raw and curated .clu to be different files; "
            "--raw-clu-ext and --curated-clu-ext both resolve to "
            f"{os.path.basename(raw_clu_path(args, 'N'))}. Set distinct "
            "Klusters labels (e.g. --raw-clu-ext K --curated-clu-ext final).\n")
        return 1
    bundle = None if args.reset.lower() in ("1", "true", "yes") \
        else load_bundle(args.model_path)
    store = bundle["store"] if bundle and "store" in bundle else empty_store()
    for k in ("split_X", "split_y", "split_g", "split_seen"):
        store.setdefault(k, [])
    seen = set(store["seen_groups"])
    split_seen = set(store["split_seen"])

    n_new = 0
    for g in iter_groups(args, load_curated=True):
        if g.name in seen and args.force.lower() not in ("1", "true", "yes"):
            print(f"  {g.name}: already in store — skipping (use force=true)")
            continue
        stats = cluster_stats(g)
        fate, keep, purity = recover_actions(g)
        kept_ids = [c for c in g.clusters if keep[c]]

        for cid in g.clusters:
            store["cluster_X"].append(cluster_feature_row(g, cid, stats[cid]).tolist())
            store["cluster_y"].append(1 if keep[cid] else 0)
            store["cluster_g"].append(g.name)
            if keep[cid] and purity[cid] < args.split_purity:
                print(f"    {g.name} cluster {cid}: purity {purity[cid]:.2f} "
                      f"-> likely human SPLIT (not replayable)")
        for i in range(len(kept_ids)):
            for j in range(i + 1, len(kept_ids)):
                a, b = kept_ids[i], kept_ids[j]
                store["merge_X"].append(merge_feature_row(g, a, b, stats[a], stats[b]).tolist())
                store["merge_y"].append(1 if fate[a] == fate[b] else 0)
                store["merge_g"].append(g.name)
        store["seen_groups"].append(g.name)
        seen.add(g.name)

        # Optional: fold in the Klusters curation log for this group (split
        # supervision the (raw, curated) endpoint diff cannot recover).
        clog = curation_log_path(args, g.group)
        force_on = args.force.lower() in ("1", "true", "yes")
        if clog and (g.name not in split_seen or force_on):
            if os.path.isfile(clog):
                try:
                    parsed = parse_curation_log(clog)
                except Exception as e:               # noqa: BLE001
                    print(f"    curation log {os.path.basename(clog)}: "
                          f"parse failed ({e}) — skipped")
                    parsed = None
                if parsed is not None:
                    for row, label, sid in parsed["examples"]:
                        store["split_X"].append(split_feature_row(row).tolist())
                        store["split_y"].append(int(label))
                        store["split_g"].append(f"{g.name}#{sid}")
                    store["split_seen"].append(g.name)
                    split_seen.add(g.name)
                    t = parsed["tally"]
                    npos = sum(1 for _, l, _ in parsed["examples"] if l == 1)
                    print(f"    curation log: sessions {parsed['block']} of "
                          f"{parsed['n_sessions']} (last contiguous block); "
                          f"splits {t['SPLIT_accept']} accept / "
                          f"{t['SPLIT_reject']} reject, "
                          f"GROUP {t['GROUP']}, DELETE {t['DELETE']}; "
                          f"+{len(parsed['examples'])} split examples "
                          f"({npos} pos)")
            elif args.curation_log.lower() not in ("true", "auto", "yes", "on", "1"):
                print(f"    curation log not found: {os.path.basename(clog)}")
        n_keep = sum(keep[c] for c in g.clusters)
        print(f"  {g.name}: {len(g.clusters)} raw clusters "
              f"({n_keep} keep / {len(g.clusters) - n_keep} discard) -> "
              f"{len(set(fate[c] for c in kept_ids))} units; "
              f"waveforms={'yes' if g.wf is not None else 'no'}")
        n_new += 1

    if n_new == 0 and not (bundle and bundle.get("cluster_clf") is not None):
        print("No new trainable groups and no prior model — nothing written.")
        return 1

    print(f"\nAccumulated store: {len(store['cluster_y'])} clusters "
          f"({int(np.sum(store['cluster_y']))} keep), "
          f"{len(store['merge_y'])} pairs "
          f"({int(np.sum(store['merge_y']))} merge), "
          f"{len(set(store['seen_groups']))} (session:group) fold(s).")
    print("\nFitting cluster classifier:")
    cclf = fit_classifier("cluster", store["cluster_X"], store["cluster_y"],
                          store["cluster_g"])
    print("Fitting merge classifier:")
    mclf = fit_classifier("merge", store["merge_X"], store["merge_y"],
                          store["merge_g"])
    sclf = None
    if store["split_y"]:
        print("Fitting split classifier (from curation log):")
        sclf = fit_classifier("split", store["split_X"], store["split_y"],
                              store["split_g"])

    out = dict(
        version=MODEL_VERSION, cluster_clf=cclf, merge_clf=mclf, split_clf=sclf,
        cluster_features=CLUSTER_FEATURES, merge_features=MERGE_FEATURES,
        split_features=SPLIT_FEATURES,
        store=store,
        config=dict(
            noise_clusters=[int(x) for x in args.noise_clusters.split(",") if x != ""],
            refractory_ms=args.refractory_ms,
            keep_threshold=args.keep_threshold,
            merge_threshold=args.merge_threshold,
            split_purity=args.split_purity,
            split_threshold=(args.split_threshold if args.split_threshold >= 0
                             else 0.5),
        ),
    )
    os.makedirs(os.path.dirname(os.path.abspath(args.model_path)), exist_ok=True)
    joblib.dump(out, args.model_path)
    print(f"\nModel saved -> {args.model_path}")
    return 0


def mode_apply(args):
    bundle = load_bundle(args.model_path)
    if bundle is None or bundle.get("cluster_clf") is None:
        sys.stderr.write(f"ERROR: no usable model at {args.model_path}; "
                         f"run mode=train first.\n")
        return 1
    cfg = bundle.get("config", {})
    keep_thr = args.keep_threshold if args.keep_threshold >= 0 \
        else cfg.get("keep_threshold", 0.5)
    merge_thr = args.merge_threshold if args.merge_threshold >= 0 \
        else cfg.get("merge_threshold", 0.5)
    split_thr = args.split_threshold if args.split_threshold >= 0 \
        else cfg.get("split_threshold", 0.5)
    has_split = bundle.get("split_clf") is not None

    wrote = 0
    for g in iter_groups(args, load_curated=False):
        new, rep = apply_group(bundle, g, keep_thr, merge_thr, split_thr)
        out_path = out_clu_path(args, g.group)
        write_clu(out_path, new)
        wrote += 1
        print(f"  group {g.group}: {rep['n_raw_real']} raw -> "
              f"{rep['n_final']} curated "
              f"({rep['n_discarded']} discarded, "
              f"{len(rep['merges'])} merge groups) -> "
              f"{os.path.basename(out_path)}")
        for m in rep["merges"]:
            print(f"      merge {m}")
        if rep["discarded"]:
            print(f"      discard->MUA {rep['discarded']}")
        cand = rep.get("split_candidates", [])
        if cand:
            sc_path = f"{args.session}.split_candidates.{g.group}"
            with open(sc_path, "w") as fh:
                fh.write("# cluster  p_split  (review/split in Klusters; "
                         "autoklusta does not auto-split)\n")
                for c, p in cand:
                    fh.write(f"{c}\t{p:.4f}\n")
            print(f"      split candidates ({len(cand)}, p>={split_thr:g}): "
                  f"{[c for c, _ in cand]} -> {os.path.basename(sc_path)}")
        elif has_split:
            print("      split candidates: none above threshold")
    if wrote == 0:
        sys.stderr.write("ERROR: no groups with raw .clu found to curate.\n")
        return 1
    return 0


def mode_eval(args):
    bundle = load_bundle(args.model_path)
    if bundle is None or bundle.get("cluster_clf") is None:
        sys.stderr.write(f"ERROR: no usable model at {args.model_path}.\n")
        return 1
    cfg = bundle.get("config", {})
    keep_thr = args.keep_threshold if args.keep_threshold >= 0 \
        else cfg.get("keep_threshold", 0.5)
    merge_thr = args.merge_threshold if args.merge_threshold >= 0 \
        else cfg.get("merge_threshold", 0.5)

    any_grp = False
    for g in iter_groups(args, load_curated=True):
        any_grp = True
        new, rep = apply_group(bundle, g, keep_thr, merge_thr)
        fate, keep, _ = recover_actions(g)
        pred_keep = {c: (new[g.mask(c)][0] > 1) if g.mask(c).any() else False
                     for c in g.clusters}
        tp = sum(1 for c in g.clusters if keep[c] and pred_keep[c])
        tn = sum(1 for c in g.clusters if not keep[c] and not pred_keep[c])
        fp = sum(1 for c in g.clusters if not keep[c] and pred_keep[c])
        fn = sum(1 for c in g.clusters if keep[c] and not pred_keep[c])
        ari = adjusted_rand_score(g.cur, new)
        prec = tp / (tp + fp) if (tp + fp) else float("nan")
        rec = tp / (tp + fn) if (tp + fn) else float("nan")
        print(f"  group {g.group}: raw={len(g.clusters)} "
              f"human_units={len(set(fate[c] for c in g.clusters if keep[c]))} "
              f"model_units={rep['n_final']}")
        print(f"      keep TP={tp} TN={tn} FP={fp} FN={fn} "
              f"(precision={prec:.3f} recall={rec:.3f})")
        print(f"      spike-level adjusted Rand vs human = {ari:.3f}")
    if not any_grp:
        sys.stderr.write("ERROR: no groups with both raw and curated .clu.\n")
        return 1
    return 0


# ════════════════════════════════════════════════════════════════════════
#  CLI
# ════════════════════════════════════════════════════════════════════════

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--session", required=True)
    p.add_argument("--param-file", default="")
    p.add_argument("--sampling-rate", type=float, required=True)
    p.add_argument("--n-channels", type=int, default=0)
    p.add_argument("--n-bits", type=int, default=16)
    p.add_argument("--n-groups", type=int, required=True)
    p.add_argument("--n-samples-per-group", default="")
    p.add_argument("--n-channels-per-group", default="")
    p.add_argument("--group", type=int, default=0,
                   help="restrict to a single group (0 = all groups)")
    p.add_argument("--mode", choices=["train", "apply", "eval"], default="apply")
    p.add_argument("--model-path", required=True)
    p.add_argument("--model-key", default="default",
                   help="filename stem used when --model-path is a directory")
    p.add_argument("--raw-clu-ext", default="",
                   help="raw clu label: reads SESSION.clu.N.<ext> (Klusters "
                        "ordering); empty = plain SESSION.clu.N")
    p.add_argument("--curated-clu-ext", default="",
                   help="curated clu label: reads SESSION.clu.N.<ext>; "
                        "empty = plain SESSION.clu.N")
    p.add_argument("--out-clu-ext", default="autocur",
                   help="apply writes SESSION.clu.N.<ext>; empty = plain "
                        "SESSION.clu.N (default autocur, non-destructive)")
    p.add_argument("--noise-clusters", default="0,1")
    p.add_argument("--keep-threshold", type=float, default=-1.0)
    p.add_argument("--merge-threshold", type=float, default=-1.0)
    p.add_argument("--split-threshold", type=float, default=-1.0,
                   help="p(split) above which a kept cluster is flagged as a "
                        "split candidate at apply time (default 0.5)")
    p.add_argument("--split-purity", type=float, default=0.8)
    p.add_argument("--use-waveforms", default="true")
    p.add_argument("--refractory-ms", type=float, default=2.0)
    p.add_argument("--reset", default="false")
    p.add_argument("--force", default="false")
    p.add_argument("--probe-library", default="")
    p.add_argument("--curation-log", default="",
                   help="optional Klusters curation log for split supervision: "
                        "''/false=off, true/auto=<session>.curation_log.<g>.<method>, "
                        "or a directory / explicit path (may contain {g})")
    return p.parse_args()


def main():
    args = parse_args()
    # Normalise the model location: expand a leading ~, and if a directory was
    # given (explicit trailing sep, or an existing dir) place the per-key model
    # file inside it — so modelPath: ~/models/ -> ~/models/<key>.curatemodel.joblib
    # rather than trying to open a directory for writing.
    args.model_path = os.path.expanduser(args.model_path)
    if args.model_path.endswith(os.sep) or os.path.isdir(args.model_path):
        args.model_path = os.path.join(args.model_path.rstrip(os.sep),
                                       f"{args.model_key}.curatemodel.joblib")
    if args.keep_threshold < 0 and args.mode == "train":
        args.keep_threshold = 0.5
    if args.merge_threshold < 0 and args.mode == "train":
        args.merge_threshold = 0.5
    if args.mode == "train":
        return mode_train(args)
    if args.mode == "apply":
        return mode_apply(args)
    return mode_eval(args)


if __name__ == "__main__":
    sys.exit(main())
