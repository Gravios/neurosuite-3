#!/usr/bin/env python3
"""
process_decomposecollisions.py
==============================
Template-matching collision decomposition for ndm_decomposecollisions.

Overview
--------
After spike sorting and manual curation in Klusters, every spike that was
assigned to cluster 0 (noise) or kept as a single-unit was evaluated by eye
on amplitude and waveform shape.  However, a subset of spikes that *look*
clean are actually temporal overlaps of two genuine action potentials: when
two neurons fire within roughly one spike-width of each other (~1 ms at
32 kHz), their extracellular waveforms sum linearly at each electrode site
and the recorded waveform lands in a region of feature space between the two
parent clusters.  These "collision spikes" are systematically misassigned —
typically to the larger-amplitude parent or to the noise cluster — and
inflate false-positive and false-negative firing rates.

Algorithm
---------
All template matching is performed **within a single spikeDetection group**
(same tetrode / polytrode shank).  Cross-shank collisions are ignored because
their spatial overlap at each electrode is negligible compared to same-shank
waveforms.

For each spikeDetection group the algorithm proceeds in four stages:

1. Template building
   For each curated single unit (cluster ≥ 2, or ≥ 0 if excludeNoise=false)
   that fired at least minSpikesTemplate spikes, compute the mean waveform
   template T_k of shape (n_samples × n_sites).  Templates are stored in
   floating-point and L2-normalised channel-by-channel before matching
   (this makes the inner products amplitude-independent and avoids bias
   toward large-amplitude units).  The per-unit amplitude distribution
   (peak-to-peak on the dominant channel) is also stored so that the final
   acceptance gate can check that component amplitudes are physiologically
   plausible.

2. Single-template screening
   For every spike waveform W:
     a. For each template T_k and shift τ in [-maxShiftSamp, +maxShiftSamp]:
            screen score(k, τ) = Pearson_r(W, roll(T_k, τ))   [circular]
            fit    amplitude a = <W_overlap, T_k_overlap> / ||T_k||²  [linear]
        Circular roll is used for correlation screening (shape comparison).
        Linear shift (zero-padded edges) is used for amplitude fitting
        and residual subtraction (arithmetic correctness).
        The spike W is the fixed reference (detected at its canonical position).
        Correlation is computed by flattening across ALL channels simultaneously.
        Circular shift preserves the full n_samp window and the template L2 norm.
        NOTE: noise/MUA templates (uid 0,1) are excluded by default.  If they
        were included, both spike and template would need to float (no canonical
        peak time), hence excludeNoise=true is the recommended setting.
     b. Find (k*, τ*) = argmax score.  If score(k*, τ*) ≥ corrThreshold
        the spike is well explained by a single template and is skipped.
     c. If the spike peak amplitude is < minSnrRms × estimated RMS noise,
        skip it (too weak to decompose reliably).
     d. Otherwise the spike is a collision candidate.

   The RMS noise floor is estimated per site from the median absolute
   deviation of all spike waveforms (robust, does not require a noise period):
       σ ≈ median(|W_all|) / 0.6745

3. Two-component matching pursuit
   For each candidate spike W:
     Pass 1 — find dominant component:
       For each (k, τ): compute amplitude a = <W, roll(T_k, τ)> / ||T_k||²
                         residual R1 = W - a · roll(T_k, τ)  (full window)
       Accept (k1, τ1, a1) with the lowest ||R1||.

     Pass 2 — fit the residual:
       For each (k, τ) where k ≠ k1 (same-unit pairs are excluded):
           a = <R1, roll(T_k, τ)> / ||T_k||²
           R2 = R1 - a · roll(T_k, τ)
       Accept (k2, τ2, a2) with the lowest ||R2||.

     Acceptance criterion:
       - residual_norm(R2) / ||W|| < residualThreshold (default 0.25,
         meaning the two-template model explains ≥ 75 % of the waveform energy)
       The amplitude gate has been removed: a1 and a2 are least-squares
       projections and are not constrained to the unit's PTP distribution.
       Negative amplitudes (anti-phase) are still rejected inside
       two_component_pursuit (a > 0 guard).

     Sub-sample shift refinement:
       After finding the integer-sample optimum τ, parabolic interpolation
       on the three score values around the peak gives a fractional-sample
       refinement (same method as the drift estimator's xcorr peak finder).
       The refinement is stored separately and does not alter the integer
       index used for waveform subtraction.

4. Output
   One binary sidecar file SESSION.col.N is written per spikeDetection group.
   See "Output format" below for the binary layout.
   The original .clu.N, .res.N and .spk.N files are never modified.

   Downstream users have two options:
     a. Use the sidecar to *exclude* collision spikes from firing-rate /
        cross-correlogram analyses (flag collision_accepted == true).
     b. Use the sidecar to *reassign* collisions: each decomposed spike at
        time t becomes two events at t + shift_samp_1 and t + shift_samp_2
        (in samples, relative to the original detection time), assigned to
        units k1 and k2 respectively.

Output format
-------------
SESSION.col.N  —  little-endian binary sidecar.

Header (32 bytes):
  [0:4]   magic           char[4]  b"COL\\x01"
  [4:8]   n_spikes        uint32
  [8:12]  n_records       uint32   candidate count
  [12:16] n_templates     uint32
  [16:20] group_idx       uint32   1-based
  [20:24] flags           uint32   bit0=excludeNoise bit1=stderiv
  [24:32] reserved        uint8[8]

Parameter block (32 bytes):
  corr_threshold(f32) residual_threshold(f32) max_shift(i32)
  min_snr_rms(f32) min_spikes_template(i32) pad(12)

Template table  n_templates × 24 bytes:
  unit_id(i32) n_spikes(i32) dominant_ch(i32)
  mean_ptp(f32) amp_pct01(f32) amp_pct99(f32)

Record table  n_records × 60 bytes:
  timestamp(i64) spike_index(i32) best_unit(i32) best_corr(f32)
  flags(u32,bit0=accepted,bit1=amp1ok,bit2=amp2ok) resid_norm(f32)
  unit1(i32) shift1(i32) shiftfrac1(f32) amp1(f32)
  unit2(i32) shift2(i32) shiftfrac2(f32) amp2(f32)

Reading in Python:
  import numpy as np, struct
  with open("session.col.7","rb") as f:
      hdr   = struct.unpack("<4sIIIII8s", f.read(32))
      prm   = struct.unpack("<ffifI12s",  f.read(32))
      n_tmpl, n_rec = hdr[3], hdr[2]
      tmpls = np.frombuffer(f.read(n_tmpl*24), dtype="<i4,i4,i4,f4,f4,f4")
      recs  = np.frombuffer(f.read(n_rec*60), dtype=[
          ("ts","<i8"),("idx","<i4"),("bsu","<i4"),("bsc","<f4"),
          ("flags","<u4"),("resnorm","<f4"),
          ("u1","<i4"),("sh1","<i4"),("sf1","<f4"),("a1","<f4"),
          ("u2","<i4"),("sh2","<i4"),("sf2","<f4"),("a2","<f4")])

Dependencies:  python3 >= 3.10,  numpy
Optional:      pyyaml (for reading group params from .yaml)

Copyright (C) 2025 neurosuite-3 contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations
import argparse
import math
import multiprocessing as mp
import os
import struct
import sys
from typing import Optional
import numpy as np

# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    p.add_argument("--session",             required=True)
    p.add_argument("--param-file",          required=True)
    p.add_argument("--sampling-rate",       type=float, required=True)
    p.add_argument("--n-channels",          type=int,   required=True)
    p.add_argument("--n-bits",              type=int,   default=16)
    p.add_argument("--n-groups",            type=int,   required=True)
    p.add_argument("--max-shift-samp",      type=int,   default=10,
                   help="Maximum temporal shift in samples (±) when scanning "
                        "template matches.  At 32 kHz, 10 samples ≈ 0.31 ms.")
    p.add_argument("--corr-window",          type=int,   default=0,
                   help="Half-width in samples of the central window used "
                        "for correlation scoring.  Restricts the Pearson r "
                        "to samples [peak-W .. peak+W] to focus on the spike "
                        "core and ignore baseline noise at the edges.  "
                        "0 = use the full waveform (default).")
    p.add_argument("--corr-threshold",      type=float, default=0.85,
                   help="Pearson r below which a spike is flagged as a "
                        "collision candidate (not well explained by any "
                        "single template).  Typical range 0.80–0.92.")
    p.add_argument("--residual-threshold",  type=float, default=0.25,
                   help="Maximum fractional residual norm (||R||/||W||) for "
                        "a two-template decomposition to be accepted.  0.25 "
                        "means the model explains ≥75 %% of waveform energy.")
    p.add_argument("--min-snr-rms",         type=float, default=4.0,
                   help="Minimum spike amplitude in units of estimated RMS "
                        "noise.  Spikes below this are too weak to decompose "
                        "reliably and are skipped.")
    p.add_argument("--min-spikes-template", type=int,   default=30,
                   help="Minimum spikes a unit must have to contribute a "
                        "template.  Units with fewer spikes are skipped.")
    p.add_argument("--exclude-noise",       default="true",
                   help="Exclude cluster 0 (artifact) and cluster 1 (MUA) "
                        "from template building (default true).")
    p.add_argument("--overwrite",           default="false",
                   help="Overwrite existing .col.N files (default false).")
    p.add_argument("--n-workers",            type=int,   default=0,
                   help="Worker processes for parallel spike processing. "
                        "0 = use all logical CPUs (default).  1 = serial.")
    return p.parse_args()


# ─────────────────────────────────────────────────────────────────────────────
# YAML parameter helpers
# ─────────────────────────────────────────────────────────────────────────────

def read_spike_group_params(param: dict, group_idx: int) -> dict:
    """
    Return nSamples, peakSampleIndex, and channel list for spikeDetection
    group `group_idx` (1-based).  Falls back to sensible defaults when the
    YAML does not contain per-group values.
    """
    groups = (param.get("spikeDetection") or {}).get("channelGroups", [])
    idx = group_idx - 1
    if 0 <= idx < len(groups):
        g = groups[idx]
        return {
            "n_samples":       int(g.get("nSamples",       52)),
            "peak_sample_idx": int(g.get("peakSampleIndex", 26)),
            "channels":        [int(c) for c in g.get("channels", [])],
        }
    return {"n_samples": 52, "peak_sample_idx": 26, "channels": []}


# ─────────────────────────────────────────────────────────────────────────────
# File I/O
# ─────────────────────────────────────────────────────────────────────────────

def read_res(path: str) -> np.ndarray:
    """Read .res.N — binary little-endian int64 timestamps, no header."""
    return np.fromfile(path, dtype="<i8")


def read_clu(path: str) -> np.ndarray:
    """
    Read .clu.N — binary format (neurosuite-3 / klusters / KlustaKwik):
      int32   nClusters  header (discarded)
      int32[] cluster id per spike in timestamp order
    """
    raw = np.fromfile(path, dtype="<i4")
    if len(raw) < 2:
        return np.array([], dtype=np.int32)
    return raw[1:]  # raw[0] = nClusters header


def read_spk(path: str, n_sites: int, n_samp: int) -> np.ndarray:
    """
    Read the raw .spk.N binary (int16, row-major: spike × sample × site).
    Returns float32 array of shape (n_spk, n_samp, n_sites).
    Infers n_spk from file length; truncates to the last complete spike.
    """
    raw  = np.fromfile(path, dtype=np.int16)
    stride = n_samp * n_sites
    n_spk  = raw.size // stride
    if n_spk == 0:
        return np.zeros((0, n_samp, n_sites), dtype=np.float32)
    return raw[:n_spk * stride].reshape(n_spk, n_samp, n_sites).astype(np.float32)


def resolve_spk_path(session: str, group_idx: int) -> tuple[str, bool]:
    """
    Return (path, is_stderiv).  Prefers .spkD.N (stderiv pipeline) over
    .spk.N so the same waveforms used for PCA are decomposed.  Both
    formats share the same on-disk layout (int16 sample-major).
    """
    spkD = f"{session}.spkD.{group_idx}"
    spk  = f"{session}.spk.{group_idx}"
    if os.path.isfile(spkD):
        return spkD, True
    return spk, False


# ─────────────────────────────────────────────────────────────────────────────
# Noise floor estimation
# ─────────────────────────────────────────────────────────────────────────────

def estimate_rms_noise(wf_all: np.ndarray) -> float:
    """
    Estimate the per-sample RMS noise floor from the full waveform matrix
    using the Quiroga 2004 MAD estimator:
        σ ≈ median(|x|) / 0.6745
    Applied to all samples of all spikes on all sites simultaneously.
    This is robust to the spike signal itself (which is sparse relative to
    the background fluctuations).
    Returns a scalar σ in the same units as the waveform data.
    """
    if wf_all.size == 0:
        return 1.0
    flat = np.abs(wf_all.ravel())
    return float(np.median(flat) / 0.6745)


# ─────────────────────────────────────────────────────────────────────────────
# Template building
# ─────────────────────────────────────────────────────────────────────────────

def build_templates(
    wf_all:              np.ndarray,   # (n_spk, n_samp, n_sites)  float32
    clu:                 np.ndarray,   # (n_spk,)                  int32
    noise_clusters:      set[int],
    min_spikes:          int,
) -> dict[int, dict]:
    """
    Build mean waveform templates for every qualified unit.

    Returns a dict keyed by unit id, each value containing:
      template     : (n_samp, n_sites) float64 mean waveform
      template_norm: same, L2-normalised over all (sample × site) values
      n_spikes     : int
      dominant_ch  : int   (site index with largest mean PTP)
      amp_pct01    : float (1st percentile of per-spike PTP on dominant_ch)
      amp_pct99    : float (99th percentile of per-spike PTP on dominant_ch)
      amp_mean     : float (mean PTP on dominant_ch)
      template_norm2 : float  ||template||²  (for fast amplitude computation)
    """
    if wf_all.ndim != 3:
        return {}

    n_spk, n_samp, n_sites = wf_all.shape
    units = sorted(set(int(c) for c in clu) - noise_clusters)
    templates: dict[int, dict] = {}

    for uid in units:
        idx = np.where(clu == uid)[0]
        if len(idx) < min_spikes:
            continue

        wf_unit = wf_all[idx].astype(np.float64)          # (n_uid, n_samp, n_sites)
        tmpl    = wf_unit.mean(axis=0)                     # (n_samp, n_sites)

        # Per-spike peak-to-peak on each site
        ptp_per_spike = wf_unit.max(axis=1) - wf_unit.min(axis=1)  # (n_uid, n_sites)
        dom_ch        = int(np.argmax(ptp_per_spike.mean(axis=0)))
        dom_ptp       = ptp_per_spike[:, dom_ch]

        norm2 = float(np.sum(tmpl ** 2))
        if norm2 < 1e-12:
            continue
        tmpl_norm = tmpl / math.sqrt(norm2)

        # Projection coefficient distribution: for each spike compute
        # a = <wf_flat, tmpl_flat> / ||tmpl||²  — same quantity as fit_amplitude at τ=0.
        # This is the direct calibration of what a will look like for clean spikes.
        tmpl_flat = tmpl.ravel()
        proj_coefs = np.array([
            float(np.dot(wf_unit[k].ravel(), tmpl_flat) / norm2)
            for k in range(len(idx))
        ])
        a_pct01 = float(np.percentile(proj_coefs, 1))
        a_pct99 = float(np.percentile(proj_coefs, 99))
        a_mean  = float(proj_coefs.mean())

        templates[uid] = {
            "template":      tmpl,
            "template_norm": tmpl_norm,
            "n_spikes":      int(len(idx)),
            "dominant_ch":   dom_ch,
            "amp_pct01":     a_pct01,
            "amp_pct99":     a_pct99,
            "amp_mean":      a_mean,
            "template_norm2": norm2,
        }

    return templates


# ─────────────────────────────────────────────────────────────────────────────
# Single-template correlation scan
# ─────────────────────────────────────────────────────────────────────────────

def best_single_match(
    wf:           np.ndarray,     # (n_samp, n_sites) float64
    templates:    dict[int, dict],
    max_shift:    int,
    corr_window:  int = 0,        # half-width around peak; 0 = full waveform
    peak_idx:     int = -1,        # peak sample index; -1 = n_samp//2
) -> tuple[int, int, float]:
    """
    Find the (unit, shift, correlation) triplet that maximises the all-channel
    Pearson r between the spike waveform and the circularly-shifted template.

    When corr_window > 0, the Pearson r is computed only on samples
    [peak_idx - corr_window .. peak_idx + corr_window] of the rolled
    template vs the spike.  This focuses scoring on the spike core and
    suppresses baseline noise at the waveform edges.  corr_window = 0
    uses the full waveform.

    Returns (best_unit_id, best_shift_samp, best_corr).
    """
    n_samp, n_sites = wf.shape
    best_uid  = -1
    best_tau  = 0
    best_corr = -2.0

    # Compute the slice for the central correlation window
    if corr_window > 0:
        pk = peak_idx if peak_idx >= 0 else n_samp // 2
        c_lo = max(0, pk - corr_window)
        c_hi = min(n_samp, pk + corr_window + 1)
    else:
        c_lo, c_hi = 0, n_samp

    # Pre-flatten and centre the spike window once outside the inner loop
    a = wf[c_lo:c_hi, :].ravel().astype(np.float64)
    a_c = a - a.mean()
    a_norm = math.sqrt(float((a_c ** 2).sum()))
    if a_norm < 1e-12:
        return best_uid, best_tau, best_corr

    for uid, td in templates.items():
        tmpl_norm = td["template_norm"]    # (n_samp, n_sites)

        for tau in range(-max_shift, max_shift + 1):
            # Circular shift then slice the same central window
            t_shifted = np.roll(tmpl_norm, tau, axis=0)
            b   = t_shifted[c_lo:c_hi, :].ravel()
            b_c = b - b.mean()
            b_norm = math.sqrt(float((b_c ** 2).sum()))
            if b_norm < 1e-12:
                continue

            r = float(np.dot(a_c, b_c) / (a_norm * b_norm))

            if r > best_corr:
                best_corr = r
                best_uid  = uid
                best_tau  = tau

    return best_uid, best_tau, best_corr


# ─────────────────────────────────────────────────────────────────────────────
# Sub-sample shift refinement via parabolic interpolation
# ─────────────────────────────────────────────────────────────────────────────

def parabolic_peak(scores: np.ndarray, pk: int) -> float:
    """
    Refine the discrete peak at index `pk` to sub-sample precision using
    a three-point parabolic fit.  Returns the fractional peak offset
    relative to `pk` (in samples); add to pk to get the refined position.
    If pk is at a boundary, returns 0.0.
    """
    if pk <= 0 or pk >= len(scores) - 1:
        return 0.0
    y0, y1, y2 = scores[pk - 1], scores[pk], scores[pk + 1]
    denom = 2.0 * (2.0 * y1 - y0 - y2)
    if abs(denom) < 1e-12:
        return 0.0
    return float((y0 - y2) / denom)


# ─────────────────────────────────────────────────────────────────────────────
# Amplitude extraction
# ─────────────────────────────────────────────────────────────────────────────

def fit_amplitude(
    wf:        np.ndarray,     # (n_samp, n_sites) float64 — the signal to fit
    tmpl:      np.ndarray,     # (n_samp, n_sites) float64 — mean template
    norm2:     float,          # ||tmpl||²
    tau:       int,
    n_samp:    int,
    a_min:     float = 0.0,    # lower clamp derived from cluster amplitude dist
    a_max:     float = 3.0,    # upper clamp derived from cluster amplitude dist
) -> tuple[np.ndarray, float]:
    """
    Compute the scalar amplitude a = <wf_overlap, tmpl_overlap> / ||tmpl||²
    and return (residual, a) where residual = wf - a * tmpl_shifted.

    a is clamped to [a_min, a_max], which are derived from the template's
    empirical amplitude distribution: a_min = pct01/mean, a_max = pct99/mean,
    extended outward to allow for partial fits on collision mixtures.
    """
    if tau >= 0:
        sl_w = slice(tau, n_samp)
        sl_t = slice(0,   n_samp - tau)
    else:
        sl_w = slice(0,  n_samp + tau)
        sl_t = slice(-tau, n_samp)

    w_overlap = wf[sl_w, :]
    t_overlap = tmpl[sl_t, :]

    if w_overlap.shape[0] < 4 or norm2 < 1e-12:
        return wf.copy(), 0.0

    a = float(np.sum(w_overlap * t_overlap) / norm2)
    a = max(a_min, min(a, a_max))

    # Apply scaled template over the FULL window (zero outside the overlap)
    residual = wf.copy()
    residual[sl_w, :] -= a * t_overlap
    return residual, a


# ─────────────────────────────────────────────────────────────────────────────
# Two-component matching pursuit
# ─────────────────────────────────────────────────────────────────────────────

def two_component_pursuit_assigned(
    wf:           np.ndarray,        # (n_samp, n_sites) float64
    templates:    dict[int, dict],
    assigned_uid: int,               # cluster the spike belongs to (u1)
    max_shift:    int,
    corr_window:  int = 0,
    peak_idx:     int = -1,
) -> tuple[dict, dict, float, float, int]:
    """
    Collision decomposition with u1 fixed to the spike's assigned cluster.

    The spike was detected at its peak and sorted into assigned_uid, so
    u1's template is already aligned at τ=0.  Only the amplitude a1 is
    fitted (no shift search for u1).  u2 is found by searching all other
    templates at all shifts in [-max_shift, +max_shift].

    Returns (comp1, comp2, rel_residual, bsc, bsc_uid) where bsc is the
    Pearson r of the raw spike vs its assigned template at τ=0.
    """
    n_samp, n_sites = wf.shape
    stride = n_samp * n_sites
    wf_norm = float(np.linalg.norm(wf))
    if wf_norm < 1e-12:
        return {}, {}, 1.0, 0.0, assigned_uid

    td1 = templates.get(assigned_uid)
    if td1 is None:
        # Assigned unit has no template — fall back to unconstrained pursuit
        comp1, comp2, rel_res, _ = two_component_pursuit(wf, templates, max_shift)
        return comp1, comp2, rel_res, 0.0, assigned_uid

    # BSC: correlation of spike vs assigned template at τ=0 (correlation window)
    if corr_window > 0:
        pk   = peak_idx if peak_idx >= 0 else n_samp // 2
        c_lo = max(0, pk - corr_window)
        c_hi = min(n_samp, pk + corr_window + 1)
    else:
        c_lo, c_hi = 0, n_samp
    a_w = wf[c_lo:c_hi, :].ravel()
    b_w = td1["template_norm"][c_lo:c_hi, :].ravel()
    a_c = a_w - a_w.mean(); b_c = b_w - b_w.mean()
    da  = float(np.sqrt((a_c**2).sum())); db = float(np.sqrt((b_c**2).sum()))
    bsc = float(np.dot(a_c, b_c) / (da * db)) if da > 1e-12 and db > 1e-12 else 0.0

    # Pass 1: fit u1 at τ=0 only
    R1, a1 = fit_amplitude(wf, td1["template"], td1["template_norm2"], 0, n_samp)

    # Parabolic refinement for u1 (τ fixed at 0, but compute ±1 for sub-sample frac)
    scores_u1 = []
    for tau in range(-max_shift, max_shift + 1):
        _, a_t = fit_amplitude(wf, td1["template"], td1["template_norm2"], tau, n_samp)
        tmp_R = wf.copy()
        if tau >= 0:
            tmp_R[tau:, :] -= a_t * td1["template"][:n_samp-tau, :]
        else:
            tmp_R[:n_samp+tau, :] -= a_t * td1["template"][-tau:, :]
        scores_u1.append(float(np.linalg.norm(tmp_R)))
    frac1 = parabolic_peak(scores_u1, max_shift)  # τ=0 is at index max_shift

    comp1 = {
        "unitId":    assigned_uid,
        "shiftSamp": 0,
        "shiftFrac": round(frac1, 3),
        "amplitude": round(a1, 4),
    }

    # Pass 2: search all OTHER templates with circular shifts
    best2_uid = -1; best2_tau = 0; best2_a = 0.0
    best2_res = float(np.linalg.norm(R1)) * 2
    best2_R   = R1.copy()
    score_grid2: dict[int, list[float]] = {}

    for uid, td in templates.items():
        if uid == assigned_uid:
            continue
        _a2_min, _a2_max = amplitude_clamp(td)
        scores2 = []
        for tau in range(-max_shift, max_shift + 1):
            tmp_R, a2 = fit_amplitude(R1, td["template"], td["template_norm2"],
                                       tau, n_samp, _a2_min, _a2_max)
            res = float(np.linalg.norm(tmp_R))
            scores2.append(res)
            if res < best2_res and a2 > 0.0:
                best2_res = res; best2_uid = uid
                best2_tau = tau; best2_a   = a2
                best2_R   = tmp_R
        score_grid2[uid] = scores2

    frac2 = float(best2_tau)
    if best2_uid >= 0:
        sc2  = np.array(score_grid2[best2_uid])
        frac2 = best2_tau + parabolic_peak(sc2, best2_tau + max_shift)

    comp2 = {
        "unitId":    best2_uid,
        "shiftSamp": best2_tau,
        "shiftFrac": round(frac2, 3),
        "amplitude": round(best2_a, 4),
    }

    rel_residual = float(np.linalg.norm(best2_R)) / wf_norm
    return comp1, comp2, rel_residual, bsc, assigned_uid



def two_component_pursuit(
    wf:            np.ndarray,        # (n_samp, n_sites) float64
    templates:     dict[int, dict],
    max_shift:     int,
) -> tuple[dict, dict, float, np.ndarray]:
    """
    Run two-pass greedy matching pursuit.

    Pass 1: find the (unit, shift) that minimises ||W - a·T(τ)||².
    Pass 2: find the (unit, shift) that minimises ||R1 - a·T(τ)||², where
            R1 is the residual after subtracting the first component.
            The second unit must be different from the first;
            same-unit pairs are bursts, not collisions, and are excluded.

    Returns:
        comp1 : dict with keys unitId, shiftSamp, shiftFrac, amplitude
        comp2 : dict with keys unitId, shiftSamp, shiftFrac, amplitude
        rel_residual : float  ||R2|| / ||W||
        R2    : (n_samp, n_sites) residual after both subtractions
    """
    n_samp, n_sites = wf.shape
    wf_norm = float(np.linalg.norm(wf))
    if wf_norm < 1e-12:
        return {}, {}, 1.0, wf.copy()

    # ── Pass 1 ──────────────────────────────────────────────────────────────
    best1_uid   = -1
    best1_tau   = 0
    best1_res   = wf_norm * 2
    best1_a     = 0.0
    best1_R     = wf.copy()

    # Collect pass-1 scores for sub-sample refinement
    score_grid1: dict[int, list[float]] = {}   # uid → list of residual norms per τ

    for uid, td in templates.items():
        tmpl  = td["template"]
        norm2 = td["template_norm2"]
        taus  = range(-max_shift, max_shift + 1)
        scores = []
        _a_min, _a_max = amplitude_clamp(td)
        for tau in taus:
            R, a = fit_amplitude(wf, tmpl, norm2, tau, n_samp, _a_min, _a_max)
            res  = float(np.linalg.norm(R))
            scores.append(res)
            if res < best1_res and a > 0:
                best1_res = res
                best1_uid = uid
                best1_tau = tau
                best1_a   = a
                best1_R   = R
        score_grid1[uid] = scores

    # Sub-sample refinement for pass 1
    best1_frac = best1_tau  # default: integer
    if best1_uid >= 0:
        sc   = np.array(score_grid1[best1_uid])
        pk   = best1_tau + max_shift    # index into score list
        frac = parabolic_peak(-sc, pk)  # minimise residual = maximise -residual
        best1_frac = best1_tau + frac

    comp1 = {
        "unitId":    best1_uid,
        "shiftSamp": best1_tau,
        "shiftFrac": round(best1_frac, 3),
        "amplitude": round(best1_a,    4),
    }

    # ── Pass 2 ──────────────────────────────────────────────────────────────
    R1 = best1_R

    best2_uid   = -1
    best2_tau   = 0
    best2_res   = float(np.linalg.norm(R1)) * 2
    best2_a     = 0.0
    best2_R     = R1.copy()

    score_grid2: dict[int, list[float]] = {}

    for uid, td in templates.items():
        tmpl  = td["template"]
        norm2 = td["template_norm2"]
        taus  = range(-max_shift, max_shift + 1)
        scores = []
        for tau in taus:
            R, a = fit_amplitude(R1, tmpl, norm2, tau, n_samp)
            res  = float(np.linalg.norm(R))
            scores.append(res)
            # A spike cannot "collide with itself" — same unit at any
            # shift is a burst, not a collision.  Skip entirely.
            if uid == best1_uid:
                continue
            if res < best2_res and a > 0:
                best2_res = res
                best2_uid = uid
                best2_tau = tau
                best2_a   = a
                best2_R   = R
        score_grid2[uid] = scores

    best2_frac = best2_tau
    if best2_uid >= 0:
        sc   = np.array(score_grid2[best2_uid])
        pk   = best2_tau + max_shift
        frac = parabolic_peak(-sc, pk)
        best2_frac = best2_tau + frac

    comp2 = {
        "unitId":    best2_uid,
        "shiftSamp": best2_tau,
        "shiftFrac": round(best2_frac, 3),
        "amplitude": round(best2_a,    4),
    }

    rel_residual = float(np.linalg.norm(best2_R)) / wf_norm
    return comp1, comp2, rel_residual, best2_R


# ─────────────────────────────────────────────────────────────────────────────
# Amplitude plausibility gate
# ─────────────────────────────────────────────────────────────────────────────

def amplitude_in_range(
    amplitude:   float,
    unit_id:     int,
    templates:   dict[int, dict],
) -> bool:
    """
    Check that the fitted scalar amplitude is within the 1st–99th percentile
    of the unit's empirical amplitude distribution on its dominant channel.
    """
    td = templates.get(unit_id)
    if td is None:
        return False
    mean_amp = td["amp_mean"]
    if mean_amp < 1e-6:
        return False
    lo = td["amp_pct01"] / mean_amp
    hi = td["amp_pct99"] / mean_amp
    return lo <= amplitude <= hi


def amplitude_clamp(td: dict) -> tuple[float, float]:
    """Return (a_min, a_max) clamp limits from the unit's projection
    coefficient distribution.  amp_pct01/pct99 are the 1st/99th percentile
    of <wf, T>/||T||² across all clean spikes of this unit — the same
    quantity as the fitted amplitude a.  Extended 50% outward to accommodate
    partial fits on collision mixtures.
    """
    mean = td["amp_mean"]
    if mean < 1e-6:
        return 0.0, 3.0
    lo = td["amp_pct01"] / mean
    hi = td["amp_pct99"] / mean
    half_range = (hi - lo) * 0.5
    return max(0.0, lo - half_range), hi + half_range


# ─────────────────────────────────────────────────────────────────────────────
# Parallel worker helpers
# ─────────────────────────────────────────────────────────────────────────────

# Module-level globals set by the pool initialiser so templates and
# screening parameters are shared across all tasks without pickling
# them with every work item.
_W_templates:    dict = {}
_W_clu:          np.ndarray = np.array([], dtype=np.int32)
_W_max_shift:    int  = 10
_W_corr_window:  int  = 0
_W_peak_idx:     int  = -1
_W_corr_thresh:  float = 0.85
_W_resid_thresh: float = 0.25
_W_min_amp_abs:  float = 0.0


def _worker_init(
    templates:    dict,
    clu:          np.ndarray,
    max_shift:    int,
    corr_window:  int,
    peak_idx:     int,
    corr_thresh:  float,
    resid_thresh: float,
    min_amp_abs:  float,
) -> None:
    global _W_templates, _W_clu, _W_max_shift, _W_corr_window, _W_peak_idx
    global _W_corr_thresh, _W_resid_thresh, _W_min_amp_abs
    _W_templates   = templates
    _W_clu         = clu
    _W_max_shift   = max_shift
    _W_corr_window = corr_window
    _W_peak_idx    = peak_idx
    _W_corr_thresh = corr_thresh
    _W_resid_thresh = resid_thresh
    _W_min_amp_abs = min_amp_abs


def _screen_task(args: tuple) -> int | None:
    """Return spike index if it is a collision candidate, else None.
    Screens by correlating the spike against its OWN assigned unit template
    at τ=0 only.  The spike was extracted at its peak, so u1's template
    is already aligned.  A low correlation means the spike shape is not
    well explained by u1 alone — it may contain a second unit.
    """
    i, wf_row = args
    wf = wf_row.astype(np.float64)
    ptp = float(wf.max() - wf.min())
    if ptp < _W_min_amp_abs:
        return None
    assigned_uid = int(_W_clu[i]) if i < len(_W_clu) else -1
    td = _W_templates.get(assigned_uid)
    if td is None:
        # Unit not in templates (too few spikes); skip
        return None
    # Correlation of spike against its assigned template at τ=0
    n_samp, n_sites = wf.shape
    if _W_corr_window > 0:
        pk   = _W_peak_idx if _W_peak_idx >= 0 else n_samp // 2
        c_lo = max(0, pk - _W_corr_window)
        c_hi = min(n_samp, pk + _W_corr_window + 1)
    else:
        c_lo, c_hi = 0, n_samp
    a = wf[c_lo:c_hi, :].ravel()
    b = td["template_norm"][c_lo:c_hi, :].ravel()
    a_c = a - a.mean(); b_c = b - b.mean()
    da = float(np.sqrt((a_c**2).sum())); db = float(np.sqrt((b_c**2).sum()))
    r = float(np.dot(a_c, b_c) / (da * db)) if da > 1e-12 and db > 1e-12 else 0.0
    return i if r < _W_corr_thresh else None


def _decompose_task(args: tuple) -> dict:
    """Run collision decomposition for one candidate spike.
    u1 is the spike's assigned cluster (fixed at τ=0, no shift search).
    u2 is found by searching all other templates with circular shifts.
    """
    i, wf_row, timestamp = args
    wf = wf_row.astype(np.float64)
    assigned_uid = int(_W_clu[i]) if i < len(_W_clu) else -1
    comp1, comp2, rel_res, bsc, bsc_uid = \
        two_component_pursuit_assigned(
            wf, _W_templates, assigned_uid, _W_max_shift,
            _W_corr_window, _W_peak_idx)
    amp1_ok = comp1.get("unitId", -1) >= 0
    amp2_ok = comp2.get("unitId", -1) >= 0
    accepted = (rel_res < _W_resid_thresh) and amp1_ok and amp2_ok
    return {
        "spikeIndex":         i,
        "timestamp":          timestamp,
        "bestSingleUnit":     bsc_uid,
        "bestSingleCorr":     round(bsc, 4),
        "collision_accepted": accepted,
        "component1":         {**comp1, "ampInRange": amp1_ok} if comp1 else {},
        "component2":         {**comp2, "ampInRange": amp2_ok} if comp2 else {},
        "residualNorm":       round(rel_res, 4),
    }


# ─────────────────────────────────────────────────────────────────────────────
# Binary output writer
# ─────────────────────────────────────────────────────────────────────────────

_HDR_FMT  = "<4sIIIII8s"
_PRM_FMT  = "<ffifI12s"
_TMPL_FMT = "<iiifff"
_REC_FMT  = "<qiifIfiiffiiff"


def write_col_binary(
    out_path:            str,
    group_idx:           int,
    n_spikes:            int,
    is_stderiv:          bool,
    exclude_noise:       bool,
    templates:           dict,
    spike_records:       list[dict],
    corr_threshold:      float,
    residual_threshold:  float,
    max_shift:           int,
    min_snr_rms:         float,
    min_spikes_tmpl:     int,
) -> None:
    """Write the binary .col.N sidecar file."""
    flags = (1 if exclude_noise else 0) | (2 if is_stderiv else 0)
    n_templates = len(templates)
    n_records   = len(spike_records)
    with open(out_path, "wb") as f:
        # Header
        f.write(struct.pack(_HDR_FMT,
            b"COL\x01",
            n_spikes, n_records, n_templates, group_idx, flags,
            b"\x00" * 8))
        # Parameter block
        f.write(struct.pack(_PRM_FMT,
            corr_threshold, residual_threshold, max_shift,
            min_snr_rms, min_spikes_tmpl,
            b"\x00" * 12))
        # Template table
        for uid in sorted(templates):
            td = templates[uid]
            f.write(struct.pack(_TMPL_FMT,
                uid, td["n_spikes"], td["dominant_ch"],
                td["amp_mean"], td["amp_pct01"], td["amp_pct99"]))
        # Record table
        for rec in spike_records:
            c1 = rec.get("component1", {})
            c2 = rec.get("component2", {})
            rec_flags = (
                (1 if rec["collision_accepted"]   else 0) |
                (2 if c1.get("ampInRange", False) else 0) |
                (4 if c2.get("ampInRange", False) else 0))
            f.write(struct.pack(_REC_FMT,
                rec["timestamp"],
                rec["spikeIndex"],
                rec["bestSingleUnit"],
                rec["bestSingleCorr"],
                rec_flags,
                rec["residualNorm"],
                c1.get("unitId",    -1),
                c1.get("shiftSamp",  0),
                c1.get("shiftFrac",  0.0),
                c1.get("amplitude",  0.0),
                c2.get("unitId",    -1),
                c2.get("shiftSamp",  0),
                c2.get("shiftFrac",  0.0),
                c2.get("amplitude",  0.0)))


# ─────────────────────────────────────────────────────────────────────────────
# Per-group driver
# ─────────────────────────────────────────────────────────────────────────────

def decompose_group(
    session:          str,
    group_idx:        int,
    n_samp:           int,
    n_sites:          int,
    noise_clusters:   set[int],
    max_shift:        int,
    corr_window:      int,
    peak_idx:         int,
    corr_threshold:   float,
    residual_threshold: float,
    min_snr_rms:      float,
    min_spikes_tmpl:  int,
    exclude_noise:    bool,
    overwrite:        bool,
    n_workers:        int = 0,
) -> bool:
    """
    Run the full collision-decomposition pipeline for one spikeDetection group.
    Returns True if output was written, False if skipped.
    """
    res_path          = f"{session}.res.{group_idx}"
    clu_path          = f"{session}.clu.{group_idx}"
    spk_path, is_stderiv = resolve_spk_path(session, group_idx)
    out_path          = f"{session}.col.{group_idx}"

    if not overwrite and os.path.isfile(out_path):
        print(f"  group {group_idx}: {out_path} exists, skipping", file=sys.stderr)
        return False

    spk_label = ".spkD" if is_stderiv else ".spk"
    for path, label in [(res_path, ".res"), (clu_path, ".clu"),
                        (spk_path, spk_label)]:
        if not os.path.isfile(path):
            print(f"  group {group_idx}: missing {label} file, skipping", file=sys.stderr)
            return False
    # .spkD stores the same nCG channels as .spk — the spatial+temporal
    # derivative transform is applied but the channel count is unchanged.
    # Only process_pca_stderiv drops the last linearly-dependent channel
    # before PCA, so .fetD has (nChan-1)*nComp+1 features, NOT .spkD.
    print(f"  group {group_idx}: {spk_label} waveforms, n_sites={n_sites}", file=sys.stderr)

    # ── Load data ────────────────────────────────────────────────────────────
    res = read_res(res_path)
    clu = read_clu(clu_path)
    n   = min(len(res), len(clu))
    if n == 0:
        print(f"  group {group_idx}: empty files, skipping", file=sys.stderr)
        return False
    res = res[:n]
    clu = clu[:n]

    try:
        wf_all = read_spk(spk_path, n_sites, n_samp)
    except Exception as exc:
        print(f"  group {group_idx}: cannot read {spk_label} ({exc}), skipping",
              file=sys.stderr)
        return False

    k = wf_all.shape[0]
    if k < n:
        print(f"  group {group_idx}: .spk has {k} spikes, .clu has {n}; "
              f"truncating to {k}", file=sys.stderr)
        n = k
        res = res[:n]
        clu = clu[:n]

    # ── Noise floor ──────────────────────────────────────────────────────────
    rms_noise = estimate_rms_noise(wf_all[:n])
    min_amp_abs = min_snr_rms * rms_noise   # minimum PTP for a reliable candidate

    # ── Build templates ───────────────────────────────────────────────────────
    templates = build_templates(wf_all[:n], clu, noise_clusters, min_spikes_tmpl)
    if len(templates) < 2:
        print(f"  group {group_idx}: fewer than 2 templates; "
              f"collision decomposition requires ≥2 units, skipping", file=sys.stderr)
        return False

    print(f"  group {group_idx}: {n} spikes, {len(templates)} templates, "
          f"RMS noise ≈ {rms_noise:.1f}", file=sys.stderr)

    # ── Pool initialiser args (shared across both phases) ────────────────────
    nw = n_workers if n_workers >= 1 else mp.cpu_count()
    init_args = (templates, clu, max_shift, corr_window, peak_idx,
                 corr_threshold, residual_threshold, min_amp_abs)
    # Chunk size: balance IPC overhead vs granularity
    chunksize = max(1, min(256, n // (nw * 4)))

    # ── Single-template screening (parallel) ──────────────────────────────────
    screen_tasks = [(i, wf_all[i]) for i in range(n)]
    candidate_indices: list[int] = []
    if nw == 1:
        _worker_init(*init_args)  # sets _W_clu
        for task in screen_tasks:
            r = _screen_task(task)
            if r is not None:
                candidate_indices.append(r)
    else:
        with mp.Pool(nw, initializer=_worker_init, initargs=init_args) as pool:
            for r in pool.imap(_screen_task, screen_tasks, chunksize=chunksize):
                if r is not None:
                    candidate_indices.append(r)

    print(f"  group {group_idx}: {len(candidate_indices)} collision candidates "
          f"({100*len(candidate_indices)/max(n,1):.1f} %)", file=sys.stderr)

    # ── Two-component matching pursuit (parallel) ─────────────────────────────
    decompose_tasks = [(i, wf_all[i], int(res[i])) for i in candidate_indices]
    # _decompose_task reads clu from _W_clu global; no change to task tuple needed
    nc = len(candidate_indices)
    chunksize2 = max(1, min(64, nc // (nw * 4)))
    if nw == 1:
        results = [_decompose_task(t) for t in decompose_tasks]
    else:
        with mp.Pool(nw, initializer=_worker_init, initargs=init_args) as pool:
            results = list(pool.imap(_decompose_task, decompose_tasks,
                                     chunksize=chunksize2))

    # Restore original spike-index order (imap preserves order, but be explicit)
    results.sort(key=lambda r: r["spikeIndex"])
    spike_records = results
    n_decomposed  = sum(1 for r in spike_records if r["collision_accepted"])

    print(f"  group {group_idx}: {n_decomposed} accepted decompositions "
          f"({100*n_decomposed/max(nc,1):.1f} % of candidates)",
          file=sys.stderr)

    write_col_binary(
        out_path        = out_path,
        group_idx       = group_idx,
        n_spikes        = n,
        is_stderiv      = is_stderiv,
        exclude_noise   = exclude_noise,
        templates       = templates,
        spike_records   = spike_records,
        corr_threshold  = corr_threshold,
        residual_threshold = residual_threshold,
        max_shift       = max_shift,
        min_snr_rms     = min_snr_rms,
        min_spikes_tmpl = min_spikes_tmpl,
    )
    print(f"  Wrote {out_path} "
          f"({n_decomposed} accepted / {len(candidate_indices)} candidates)",
          file=sys.stderr)
    return True


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main() -> int:
    args = parse_args()

    exclude_noise = str(args.exclude_noise).lower() not in ("false", "0", "no")
    overwrite     = str(args.overwrite).lower()     not in ("false", "0", "no")
    noise_clusters: set[int] = {0, 1} if exclude_noise else set()

    try:
        import yaml
        with open(args.param_file) as f:
            param = yaml.safe_load(f) or {}
    except ImportError:
        print("pyyaml not installed — group params use defaults", file=sys.stderr)
        param = {}

    n_written = 0

    for g in range(1, args.n_groups + 1):
        gp       = read_spike_group_params(param, g)
        n_samp   = gp["n_samples"]
        n_sites  = len(gp["channels"]) or args.n_channels
        peak_idx = gp["peak_sample_idx"]
        # corr_window: CLI arg, with 0 meaning full waveform
        corr_window = args.corr_window

        print(f"  Group {g}: n_samp={n_samp} n_sites={n_sites} "
              f"peak={peak_idx} corr_window={corr_window or 'full'}",
              file=sys.stderr)

        wrote = decompose_group(
            session            = args.session,
            group_idx          = g,
            n_samp             = n_samp,
            n_sites            = n_sites,
            noise_clusters     = noise_clusters,
            max_shift          = args.max_shift_samp,
            corr_window        = corr_window,
            peak_idx           = peak_idx,
            corr_threshold     = args.corr_threshold,
            residual_threshold = args.residual_threshold,
            min_snr_rms        = args.min_snr_rms,
            min_spikes_tmpl    = args.min_spikes_template,
            exclude_noise      = exclude_noise,
            overwrite          = overwrite,
            n_workers          = args.n_workers,
        )
        if wrote:
            n_written += 1

    if n_written == 0:
        print("  No output files written.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    mp.freeze_support()   # no-op on Linux; needed for frozen executables
    sys.exit(main())
