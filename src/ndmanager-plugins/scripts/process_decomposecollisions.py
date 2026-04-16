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
            score(k, τ) = <W_trimmed(τ), T_k> / (||W_trimmed|| · ||T_k||)
        where W_trimmed(τ) is W sliced to the same sample range as T_k after
        applying shift τ (zero-padding is not used — slicing keeps the inner
        product dimensionally consistent).
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
       For each (k, τ): compute amplitude a = <W, T_k(τ)> / ||T_k||²
                         residual norm after subtraction
       Accept (k1, τ1, a1) with the lowest residual norm.

     Pass 2 — fit the residual:
       R = W - a1 · T_k1(τ1)
       For each (k, τ) (including k == k1 for burst pairs):
           a = <R, T_k(τ)> / ||T_k||²
       Accept (k2, τ2, a2) with the lowest residual norm of R.

     Acceptance criteria (all must pass):
       - residual_norm(R2) / ||W|| < residualThreshold (default 0.25,
         meaning the two-template model explains ≥ 75 % of the waveform energy)
       - a1 and a2 both fall within the 1st–99th percentile of the
         amplitude distribution of their respective units
       - neither a1 nor a2 is negative (no anti-phase solutions)

     Sub-sample shift refinement:
       After finding the integer-sample optimum τ, parabolic interpolation
       on the three score values around the peak gives a fractional-sample
       refinement (same method as the drift estimator's xcorr peak finder).
       The refinement is stored separately and does not alter the integer
       index used for waveform subtraction.

4. Output
   One YAML sidecar file SESSION.col.N is written per spikeDetection group.
   It contains:
     - Global statistics (n_spikes, n_templates, n_candidates, n_decomposed)
     - Template metadata (unit id, n_spikes, dominant channel, mean PTP)
     - Per-spike collision records (spike index, timestamp, both components)
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
    with open(path) as f:
        return np.array([int(l) for l in f if l.strip()], dtype=np.int64)


def read_clu(path: str) -> np.ndarray:
    """Read .clu.N — first line is the cluster count, remaining are labels."""
    with open(path) as f:
        lines = [l.strip() for l in f if l.strip()]
    if len(lines) <= 1:
        return np.array([], dtype=np.int32)
    return np.array([int(l) for l in lines[1:]], dtype=np.int32)


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

        templates[uid] = {
            "template":      tmpl,
            "template_norm": tmpl_norm,
            "n_spikes":      int(len(idx)),
            "dominant_ch":   dom_ch,
            "amp_pct01":     float(np.percentile(dom_ptp, 1)),
            "amp_pct99":     float(np.percentile(dom_ptp, 99)),
            "amp_mean":      float(dom_ptp.mean()),
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
) -> tuple[int, int, float]:
    """
    Find the (unit, shift, correlation) triplet that maximises the Pearson r
    between the spike waveform and a shifted template.

    The shift τ is applied to the *template* (positive τ means the template
    is shifted later, i.e. the spike arrived earlier relative to the template
    peak — equivalent to the spike being τ samples early).  Only the
    overlapping region after shifting is used for the correlation; no
    zero-padding is applied.

    Returns (best_unit_id, best_shift_samp, best_corr).
    best_corr is in [−1, 1]; negative values are physically nonsensical for
    same-polarity spikes and indicate a non-match.
    """
    n_samp, n_sites = wf.shape
    best_uid  = -1
    best_tau  = 0
    best_corr = -2.0

    for uid, td in templates.items():
        tmpl_norm = td["template_norm"]    # (n_samp, n_sites)

        for tau in range(-max_shift, max_shift + 1):
            # Slice the overlapping region
            if tau >= 0:
                # template shifted right: compare wf[tau:] with tmpl[:n-tau]
                w_sl = wf[tau:,       :]
                t_sl = tmpl_norm[:n_samp - tau, :]
            else:
                # template shifted left: compare wf[:n+tau] with tmpl[-tau:]
                w_sl = wf[:n_samp + tau, :]
                t_sl = tmpl_norm[-tau:,   :]

            if w_sl.shape[0] < 4:
                continue

            # Flatten to 1-D for a single Pearson r over all (sample, site) pairs
            a = w_sl.ravel()
            b = t_sl.ravel()
            a_c = a - a.mean()
            b_c = b - b.mean()
            denom = math.sqrt((a_c ** 2).sum() * (b_c ** 2).sum())
            if denom < 1e-12:
                continue
            r = float(np.dot(a_c, b_c) / denom)

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
) -> tuple[np.ndarray, float]:
    """
    Compute the scalar amplitude a = <wf_aligned, tmpl_aligned> / ||tmpl||²
    and return (residual, a), where residual = wf - a * tmpl_aligned.

    The amplitude is the least-squares solution for a single-scalar model
    wf ≈ a * tmpl.  Only the overlapping region (after applying shift tau)
    is used for both the dot product and the residual.

    The returned residual has the same shape as wf (n_samp, n_sites);
    regions outside the overlap are left at their original wf values.
    """
    if tau >= 0:
        sl_w = slice(tau,           n_samp)
        sl_t = slice(0,             n_samp - tau)
    else:
        sl_w = slice(0,             n_samp + tau)
        sl_t = slice(-tau,          n_samp)

    w_overlap = wf[sl_w, :]
    t_overlap = tmpl[sl_t, :]

    if w_overlap.shape[0] < 4 or norm2 < 1e-12:
        return wf.copy(), 0.0

    a = float(np.sum(w_overlap * t_overlap) / norm2)

    # Build the full-length subtracted waveform
    residual = wf.copy()
    residual[sl_w, :] -= a * t_overlap
    return residual, a


# ─────────────────────────────────────────────────────────────────────────────
# Two-component matching pursuit
# ─────────────────────────────────────────────────────────────────────────────

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
            The second unit may be the same as the first (burst pair).

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
        for tau in taus:
            R, a = fit_amplitude(wf, tmpl, norm2, tau, n_samp)
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

    The fitted amplitude is a dimensionless scale factor relative to the mean
    template.  A value of 1.0 means the component exactly matches the template
    amplitude.  We compare against the ratio (per-spike PTP) / (mean PTP),
    so the 1st–99th percentile band is in the same dimensionless units.
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
    corr_threshold:   float,
    residual_threshold: float,
    min_snr_rms:      float,
    min_spikes_tmpl:  int,
    exclude_noise:    bool,
    overwrite:        bool,
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
    print(f"  group {group_idx}: using {spk_label} waveforms", file=sys.stderr)

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

    # ── Single-template screening ─────────────────────────────────────────────
    candidate_indices: list[int] = []

    for i in range(n):
        wf = wf_all[i].astype(np.float64)

        # SNR gate: peak PTP across sites must exceed min_snr_rms * σ_noise
        ptp_spike = float(wf.max() - wf.min())
        if ptp_spike < min_amp_abs:
            continue

        _, _, best_corr = best_single_match(wf, templates, max_shift)
        if best_corr < corr_threshold:
            candidate_indices.append(i)

    print(f"  group {group_idx}: {len(candidate_indices)} collision candidates "
          f"({100*len(candidate_indices)/max(n,1):.1f} %)", file=sys.stderr)

    # ── Two-component matching pursuit ────────────────────────────────────────
    spike_records: list[dict] = []
    n_decomposed = 0

    for i in candidate_indices:
        wf        = wf_all[i].astype(np.float64)
        timestamp = int(res[i])

        # Best single-template match (for reporting)
        best_single_uid, best_single_tau, best_single_corr = \
            best_single_match(wf, templates, max_shift)

        # Two-component pursuit
        comp1, comp2, rel_res, _ = two_component_pursuit(wf, templates, max_shift)

        # Acceptance gates
        accepted = False
        amp1_ok  = False
        amp2_ok  = False
        if comp1.get("unitId", -1) >= 0 and comp2.get("unitId", -1) >= 0:
            amp1_ok = amplitude_in_range(comp1["amplitude"], comp1["unitId"], templates)
            amp2_ok = amplitude_in_range(comp2["amplitude"], comp2["unitId"], templates)
            accepted = (rel_res < residual_threshold) and amp1_ok and amp2_ok

        if accepted:
            n_decomposed += 1

        # Annotate components with ampInRange flag
        comp1_out = {**comp1, "ampInRange": amp1_ok} if comp1 else {}
        comp2_out = {**comp2, "ampInRange": amp2_ok} if comp2 else {}

        spike_records.append({
            "spikeIndex":        i,
            "timestamp":         timestamp,
            "bestSingleUnit":    best_single_uid,
            "bestSingleCorr":    round(best_single_corr, 4),
            "collision_accepted": accepted,
            "component1":        comp1_out,
            "component2":        comp2_out,
            "residualNorm":      round(rel_res, 4),
        })

    print(f"  group {group_idx}: {n_decomposed} accepted decompositions "
          f"({100*n_decomposed/max(len(candidate_indices),1):.1f} % of candidates)",
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
        gp      = read_spike_group_params(param, g)
        n_samp  = gp["n_samples"]
        n_sites = len(gp["channels"]) or args.n_channels

        print(f"  Group {g}: n_samp={n_samp} n_sites={n_sites}", file=sys.stderr)

        wrote = decompose_group(
            session            = args.session,
            group_idx          = g,
            n_samp             = n_samp,
            n_sites            = n_sites,
            noise_clusters     = noise_clusters,
            max_shift          = args.max_shift_samp,
            corr_threshold     = args.corr_threshold,
            residual_threshold = args.residual_threshold,
            min_snr_rms        = args.min_snr_rms,
            min_spikes_tmpl    = args.min_spikes_template,
            exclude_noise      = exclude_noise,
            overwrite          = overwrite,
        )
        if wrote:
            n_written += 1

    if n_written == 0:
        print("  No output files written.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
