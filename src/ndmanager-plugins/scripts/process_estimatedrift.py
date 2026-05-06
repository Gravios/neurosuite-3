#!/usr/bin/env python3
"""
process_estimatedrift.py
========================
Spatial drift estimation for ndm_estimatedrift.

Two complementary methods are run and both reported in SESSION.drift.

Method 1 — Per-unit spatial-profile cross-correlation (primary)
----------------------------------------------------------------
For each unit U that fired ≥ minSpikes in BOTH the reference window and
window W:

  1. Build the unit's amplitude-vs-depth fingerprint in each window:
       profile[U, w] = mean PTP amplitude per electrode site
                       across all spikes of unit U in window w
     This is a vector of length n_sites, not a scalar.

  2. Cross-correlate profile[U, ref] against profile[U, W] along the depth
     axis.  The lag at the correlation peak — interpolated to sub-site
     precision with a parabolic fit — is the depth shift of that unit's
     spatial footprint (µm).

       δ[U, W] = xcorr_peak_lag(profile[U, ref], profile[U, W])  (µm)

  3. The shank drift estimate is the weighted median of {δ[U, W]}, with
     weights = sqrt(n_spikes_ref[U] × n_spikes_win[U]).

Why per-unit xcorr beats CoM difference (δ = CoM_win − CoM_ref):
  • CoM collapses the entire spatial footprint to a single number.  A unit
    with an asymmetric profile or one sitting near the edge of the array
    can shift its CoM substantially just from spike-count fluctuations at
    the dominant site, even with no real displacement.
  • xcorr uses the SHAPE of the amplitude profile: it finds the integer+
    fractional site offset that best aligns the whole profile.  A shift of
    even half a site spacing is detectable from the asymmetry of the
    correlation peak, independent of the centroid position.
  • For an octrode (±11 µm stagger, 20 µm pitch) a 5 µm drift produces a
    ~0.25-site lag; CoM would register < 1 µm change for the same event.

Method 2 — Population amplitude-profile cross-correlation (fallback)
----------------------------------------------------------------------
For each time window build a mean-PTP-per-site profile pooled across ALL
non-noise spikes (the full multi-unit spatial fingerprint).  Cross-correlate
against the reference profile at sub-site resolution.

  - Does not require isolated single units.
  - Is the primary estimate when fewer than minUnits units are tracked.
  - Degrades gracefully when .spk is absent (uses spike counts as proxy).

Geometric consistency check
----------------------------
Before combining per-unit estimates, verify that pairwise inter-unit depth
distances are preserved between the reference and current window.  A unit is
flagged as an outlier and excluded from method 1 if its median pairwise
distance change to all other tracked units exceeds outlierThreshold (µm).
Outliers are still listed in the per-unit detail block for inspection —
they may indicate genuine single-unit displacement or curation errors.

Combined estimate
-----------------
  • ≥ minUnits inliers: weighted combination of method 1 (n_inliers weight)
    and method 2 (weight 1).
  • < minUnits inliers: method 2 only.
  • Method 2 unavailable (no .spk): method 1 only.

Output: SESSION.drift (YAML)

Dependencies:  python3 ≥ 3.10,  pyyaml,  numpy
Optional:      scipy  (cross-correlation; numpy fallback used otherwise)

Copyright (C) 2025 neurosuite-3 contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations
import argparse, math, os, sys, yaml
import numpy as np
from pathlib import Path
from typing import Optional

# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--session",           required=True)
    p.add_argument("--param-file",        required=True)
    p.add_argument("--sampling-rate",     type=float, required=True)
    p.add_argument("--n-channels",        type=int,   required=True)
    p.add_argument("--n-bits",            type=int,   default=16)
    p.add_argument("--n-groups",          type=int,   required=True)
    p.add_argument("--window-sec",        type=float, default=60.0)
    p.add_argument("--min-units",         type=int,   default=3)
    p.add_argument("--min-spikes",        type=int,   default=20)
    p.add_argument("--exclude-noise",     default="true")
    p.add_argument("--outlier-threshold", type=float, default=5.0,
                   help="Max pairwise distance change (µm) to flag a unit as outlier")
    p.add_argument("--weight-mode",
                   choices=["geometry", "sharpness", "count"], default="geometry",
                   help="Per-unit weighting in the Method-1 weighted median.  "
                        "'geometry' (default) extends 'sharpness' with a "
                        "profile_drift_sensitivity term — the L2 norm of the "
                        "spatial derivative of the unit's amplitude profile, "
                        "computed from the reference window.  This down-weights "
                        "far-field units whose profile is stable across drift "
                        "(sharp xcorr peak, but the peak is uninformative).  "
                        "'sharpness' uses curvature × n_active × sqrt(n_spikes); "
                        "'count' reproduces the legacy sqrt(n_ref × n_win) "
                        "weighting for back-compat.")
    p.add_argument("--n-workers", type=int, default=1,
                   help="Number of worker processes for shank-parallel "
                        "estimation.  Default 1 (sequential, fully "
                        "deterministic ordering of stderr messages).  Each "
                        "shank is independent — independent .res/.clu/.spk "
                        "reads, no shared state — so this scales nearly "
                        "linearly with shank count up to physical core "
                        "count.  Set 0 to mean 'use all CPUs'.")
    p.add_argument("--probe-library",     default="")
    p.add_argument("--n-samples-per-group", default="",
                   help="Comma-separated list of nSamplesPerSpike for each group "
                        "(1-based, e.g. '41,41,32').  Falls back to 32 when absent.")
    p.add_argument("--source-group",      type=int, default=0,
                   help="1-based spike group whose curated .clu drives the drift "
                        "estimate. 0 = estimate independently for every group "
                        "(default). When non-zero only this group is processed "
                        "and its drift windows are propagated to all sibling groups "
                        "(same probeId) in the output file.")
    p.add_argument("--output",            required=True)
    return p.parse_args()


# ─────────────────────────────────────────────────────────────────────────────
# Probe geometry
# ─────────────────────────────────────────────────────────────────────────────

def find_probe_library(override: str = "") -> list[str]:
    paths: list[str] = []
    if override:             paths.append(override)
    env = os.environ.get("NEUROSUITE_PROBE_PATH", "")
    if env:                  paths.append(env)
    paths.append(str(Path.home() / ".local/share/neurosuite/probes"))
    paths.append("/usr/share/neurosuite/probes")
    paths.append("probes")
    return paths


def load_probe_file(probe_file: str, library_paths: list[str]) -> Optional[dict]:
    for lib in ["", *library_paths]:
        cand = os.path.join(lib, probe_file) if lib else probe_file
        if os.path.isfile(cand):
            with open(cand) as f:
                doc = yaml.safe_load(f)
            return doc.get("probeFile") if doc else None
    return None


def site_depths_from_probe(probe: dict, shank_index: int = 0) -> np.ndarray:
    sites = probe.get("sites", {})
    n     = sites.get("count_per_shank", 0)
    if n == 0:
        return np.array([])

    gg = sites.get("geometry_groups")
    if gg and isinstance(gg, dict):
        ng   = gg.get("n_groups", 1)
        gs   = gg.get("group_spacing_um", 500)
        wg   = gg.get("within_group", [])
        d    = [grp * gs + s[1] for grp in range(ng) for s in wg]
        return np.array(d[:n], dtype=float)

    geom = sites.get("geometry")
    if geom and isinstance(geom, list):
        return np.array([g[1] for g in geom[:n]], dtype=float)

    sp = sites.get("spacing_um") or 50
    return np.arange(n, dtype=float) * sp


def default_site_depths(n: int, spacing: float = 50.0) -> np.ndarray:
    return np.arange(n, dtype=float) * spacing


# ─────────────────────────────────────────────────────────────────────────────
# File I/O
# ─────────────────────────────────────────────────────────────────────────────

def read_res(path: str) -> np.ndarray:
    """Read .res.N — binary little-endian int64 timestamps, no header."""
    return np.fromfile(path, dtype="<i8")


def read_clu(path: str) -> np.ndarray:
    """
    Read .clu.N — binary format:
      int32   nClusters  header (discarded)
      int32[] cluster id per spike in timestamp order
    """
    raw = np.fromfile(path, dtype="<i4")
    if len(raw) < 2:
        return np.array([], dtype=np.int32)
    return raw[1:]



def read_spk(path: str, n_sites: int, n_samp: int, n_spk: int) -> np.ndarray:
    """
    Memory-map a .spk(D).N file as a (k, n_samp, n_sites) int16 array.

    Uses ``np.memmap`` so the OS page-cache holds only what we actually
    touch.  Fancy-indexed reads like ``wf_all[idx]`` for one cluster's
    spikes pull in just those pages; the full file is never resident.

    The on-disk layout is sample-major: each spike is ``n_samp`` rows
    of ``n_sites`` int16 values, stored back-to-back.  We expose this
    as a 3-D view shaped (k, n_samp, n_sites) where ``k`` is whatever
    integer number of complete spikes fit in the file.

    The file size is computed once via ``os.path.getsize`` so this
    works for both ``.spk.N`` (raw) and ``.spkD.N`` (stderiv) without
    needing to know which pipeline wrote it.
    """
    stride = n_samp * n_sites
    if stride <= 0:
        return np.empty((0, max(1, n_samp), max(1, n_sites)), dtype=np.int16)
    total_bytes = os.path.getsize(path)
    k_in_file   = total_bytes // (stride * 2)   # int16 = 2 bytes
    if n_spk > 0:
        k = min(k_in_file, n_spk)
    else:
        k = k_in_file
    if k <= 0:
        return np.empty((0, n_samp, n_sites), dtype=np.int16)
    mm = np.memmap(path, dtype="<i2", mode="r",
                   shape=(k, n_samp, n_sites))
    return mm


# ─────────────────────────────────────────────────────────────────────────────
# Waveform analysis
# ─────────────────────────────────────────────────────────────────────────────

def ptp(wf: np.ndarray) -> np.ndarray:
    """Peak-to-peak per spike per site. (n_spk, n_sites)"""
    return (wf.max(axis=1) - wf.min(axis=1)).astype(float)


def amplitude_com(ptp_arr: np.ndarray, depths: np.ndarray) -> np.ndarray:
    """Amplitude-weighted CoM per spike. (n_spk,)

    Sites with NaN depth (absent from probe geometry) are masked out:
    their amplitude contributes 0 to both numerator and denominator.
    """
    valid_mask = np.isfinite(depths)                  # (n_sites,)
    ptp_valid  = ptp_arr * valid_mask[np.newaxis, :]  # zero out NaN-depth sites
    total = ptp_valid.sum(axis=1, keepdims=True)
    total = np.where(total == 0, 1.0, total)
    # Use nan-safe depths: NaN → 0 for multiplication; already zeroed in ptp_valid
    safe_depths = np.where(valid_mask, depths, 0.0)
    return (ptp_valid / total * safe_depths[np.newaxis, :]).sum(axis=1)


def unit_com(wf: np.ndarray, depths: np.ndarray) -> float:
    """Median CoM across spikes for one unit."""
    if wf.shape[0] == 0 or wf.shape[2] != len(depths):
        return float("nan")
    return float(np.median(amplitude_com(ptp(wf), depths)))


def unit_amplitude_profile(wf: np.ndarray) -> np.ndarray:
    """Mean PTP per site. (n_sites,)"""
    return ptp(wf).mean(axis=0) if wf.shape[0] > 0 else np.zeros(wf.shape[2])


def profile_drift_sensitivity(profile: np.ndarray,
                               depths:  np.ndarray) -> float:
    """
    Estimate how sensitive a unit's amplitude profile is to small
    probe-depth shifts, in units of (L2-normalised amplitude per µm).

    The intuition:
        * A near-source unit with a tight peak has a steep profile
          gradient on either flank.  A 1 µm probe shift moves the
          centroid by 1 µm and the per-site amplitudes change visibly.
          → large sensitivity.
        * A far-source unit with a smeared, near-flat profile has
          almost no gradient anywhere.  A 1 µm probe shift barely
          changes the per-site amplitudes.
          → tiny sensitivity.

    This separation matters because both kinds of unit can produce
    sharp xcorr peaks (a flat profile cross-correlated with itself
    peaks sharply at zero), so curvature alone is misleading.  The
    sensitivity term distinguishes "sharp because informative" from
    "sharp because invariant".

    Computation
    -----------
    L2-normalise the profile so absolute amplitude doesn't enter
    (the count and active-site terms already encode that).  Restrict
    to active sites with PTP > 10 % of the per-window peak — the
    same threshold used by ``per_unit_xcorr_shift``.  Compute central
    differences of the normalised profile w.r.t. depth on the
    sorted-by-depth active subset, then return the L2 norm.

    Returns 0.0 when fewer than 3 active sites are available
    (central differences need a 3-point stencil).  This forces the
    weight to zero, which is correct: we have no shape information.

    For the Buzsaki64L (8 sites/shank, ~20 µm pitch) typical values
    range from ~0.005 (far-field, near-flat profile) to ~0.05
    (tight near-tip dipole) — a 10× spread that meaningfully
    redistributes weight across the unit population.

    For the A32 (32 sites, 50 µm pitch) the dynamic range is wider
    still: a unit registering sharply on a single site neighbourhood
    will dwarf far-field units by 50× or more in this metric.
    """
    valid_depths = np.isfinite(depths)
    if valid_depths.sum() < 3:
        return 0.0
    threshold = 0.1 * max(profile.max(), 1e-6)
    active    = valid_depths & (profile > threshold)
    if active.sum() < 3:
        return 0.0

    p_a = profile[active].astype(np.float64)
    z_a = depths[active].astype(np.float64)

    # Sort by depth (no guarantee active-site order is monotone).
    order = np.argsort(z_a)
    p_s   = p_a[order]
    z_s   = z_a[order]

    # L2-normalise so absolute amplitude doesn't bleed in.
    norm = np.linalg.norm(p_s)
    if norm <= 1e-12:
        return 0.0
    p_n = p_s / norm

    # Central differences in depth.  At array edges fall back to
    # forward/backward differences so we don't drop endpoint info.
    n = len(p_n)
    grad = np.empty(n, dtype=np.float64)
    grad[0]    = (p_n[1]  - p_n[0])     / max(z_s[1]    - z_s[0],    1e-9)
    grad[-1]   = (p_n[-1] - p_n[-2])    / max(z_s[-1]   - z_s[-2],   1e-9)
    if n >= 3:
        dz = z_s[2:] - z_s[:-2]
        dz = np.where(dz < 1e-9, 1e-9, dz)
        grad[1:-1] = (p_n[2:] - p_n[:-2]) / dz

    return float(np.linalg.norm(grad))


# ─────────────────────────────────────────────────────────────────────────────
# Geometric consistency check
# ─────────────────────────────────────────────────────────────────────────────

def flag_outlier_units(ref_coms: dict[int, float],
                       win_coms: dict[int, float],
                       threshold: float) -> set[int]:
    """
    Flag units whose median pairwise depth-distance to all other tracked
    units has changed by more than `threshold` µm between reference and
    current window.  These units are excluded from method 1.
    """
    common = sorted(set(ref_coms) & set(win_coms))
    if len(common) < 2:
        return set()
    ref_arr = np.array([ref_coms[u] for u in common])
    win_arr = np.array([win_coms[u] for u in common])
    d_ref   = np.abs(ref_arr[:, None] - ref_arr[None, :])
    d_win   = np.abs(win_arr[:, None] - win_arr[None, :])
    delta   = np.abs(d_ref - d_win)
    np.fill_diagonal(delta, np.nan)
    median_change = np.nanmedian(delta, axis=1)
    return {common[i] for i, mc in enumerate(median_change) if mc > threshold}


# ─────────────────────────────────────────────────────────────────────────────
# Method 1: per-unit spatial-profile cross-correlation
# ─────────────────────────────────────────────────────────────────────────────

def weighted_median(values: np.ndarray, weights: np.ndarray) -> float:
    """Weighted median via sorted cumulative weight."""
    order   = np.argsort(values)
    values  = values[order]
    weights = weights[order]
    cumw    = np.cumsum(weights)
    idx     = np.searchsorted(cumw, cumw[-1] / 2.0)
    return float(values[min(idx, len(values) - 1)])


def per_unit_xcorr_shift(ref_profile: np.ndarray,
                          win_profile: np.ndarray,
                          depths: np.ndarray
                          ) -> Optional[tuple[float, float, int, int]]:
    """
    Estimate the depth shift of a single unit by cross-correlating its
    amplitude-vs-depth profile between the reference and current window.

    ref_profile, win_profile : (n_sites,) mean PTP amplitude per site
    depths                   : (n_sites,) depth in µm, monotone

    Returns
    -------
    (shift_um, peak_curvature, n_active_ref, n_active_win) or None.

    Returns None only when the profile fails the hard floor of *at least
    two* sites with appreciable signal in both windows.  Above that
    floor, the continuous-valued ``n_active_*`` and ``peak_curvature``
    are returned so the caller can use them as a soft confidence weight
    (low n_active_* = profile concentrated on a single site, low
    curvature = ambiguous xcorr peak).

    "Appreciable" is fixed at 10 % of the per-window peak — same
    threshold as the population-xcorr fallback.  This is a hard floor
    because below it the cross-correlation is dominated by noise; the
    soft-weight scheme handles the gradient between "barely usable" and
    "perfectly clean" above it.
    """
    threshold      = 0.1 * max(ref_profile.max(), win_profile.max(), 1e-6)
    n_active_ref   = int((ref_profile > threshold).sum())
    n_active_win   = int((win_profile > threshold).sum())
    if n_active_ref < 2 or n_active_win < 2:
        return None
    res = xcorr_shift(ref_profile, win_profile, depths)
    if res is None:
        return None
    shift, curvature = res
    return shift, curvature, n_active_ref, n_active_win


def method1_per_unit_xcorr(
        ref_profiles: dict[int, np.ndarray],  # unit → amplitude profile in ref window
        win_profiles: dict[int, np.ndarray],  # unit → amplitude profile in win window
        ref_coms:     dict[int, float],        # unit → CoM in ref (for consistency check)
        win_coms:     dict[int, float],        # unit → CoM in win (for consistency check)
        ref_cnts:     dict[int, int],          # unit → spike count in ref
        win_cnts:     dict[int, int],          # unit → spike count in win
        depths:       np.ndarray,
        threshold:    float,
        weight_mode:  str = "geometry",
) -> tuple[Optional[float], int, list[dict]]:
    """
    Compute rigid-body drift as the weighted median of per-unit spatial-profile
    cross-correlation shifts.

    For each unit tracked in both windows:
      1. Cross-correlate amplitude_profile[ref] vs amplitude_profile[win].
      2. The peak lag (µm, sub-site via parabolic interpolation) = this unit's
         individual displacement estimate.
      3. Weight depends on weight_mode:

         geometry  (default):
            profile_drift_sensitivity(ref_profile)
          × peak_curvature
          × sqrt(n_active_ref × n_active_win)
          × sqrt(n_spikes_ref × n_spikes_win)

         sharpness (legacy, prior default):
            peak_curvature
          × sqrt(n_active_ref × n_active_win)
          × sqrt(n_spikes_ref × n_spikes_win)

         count     (legacy, original):
            sqrt(n_spikes_ref × n_spikes_win)

    Geometry-mode rationale.
    Sharpness mode rewards units whose xcorr peak is well-defined.  But a
    sharp xcorr peak can arise for two opposite reasons:

      (a) the unit has a tight spatial dipole that genuinely tracks
          drift — its amplitude profile changes substantially per µm
          of shift, and the xcorr unambiguously locates that shift.
          We want this unit dominating the weighted median.

      (b) the unit has a stable, near-flat amplitude profile (far
          from the array, recorded in the array's far field) — the
          profile barely changes shape over a small drift, so the
          xcorr peaks sharply at zero or near-zero regardless of the
          true drift.  This unit gives no drift information.

    The profile_drift_sensitivity term — the L2 norm of the spatial
    derivative of the unit's reference-window profile — separates
    these cases.  Tight near-tip units have steep gradients;
    far-field units have ~flat profiles.  The two factors of
    ~10× spread (typical) means a single near-tip unit with 100
    spikes can outweigh five far-field units with 1000 spikes each
    — which is exactly the right outcome for drift estimation.

    On a Buzsaki64L (8 sites/shank, 20 µm pitch, 140 µm shank span)
    or A32 linear (32 sites, 50 µm pitch) the dynamic range of
    profile_drift_sensitivity across well-isolated units in a
    typical session is ~10–50×, depending on how broad the
    amplitude-vs-depth spread of curated units is.

    The geometric consistency check (pairwise CoM distances) is still used to
    flag outliers — units whose relative spatial arrangement changed — before
    combining into the population estimate.

    Returns (drift_um, n_inlier_units, detail_list).
    """
    outliers = flag_outlier_units(ref_coms, win_coms, threshold)
    tracked  = sorted(set(ref_profiles) & set(win_profiles))

    shifts:  list[float] = []
    weights: list[float] = []
    details: list[dict]  = []

    for uid in tracked:
        flagged  = uid in outliers
        ref_p    = ref_profiles[uid]
        win_p    = win_profiles[uid]
        ref_com  = ref_coms.get(uid, float("nan"))
        win_com  = win_coms.get(uid, float("nan"))
        n_ref    = ref_cnts.get(uid, 0)
        n_win    = win_cnts.get(uid, 0)

        res            = per_unit_xcorr_shift(ref_p, win_p, depths)
        shift          = None
        curvature      = 0.0
        n_active_ref   = 0
        n_active_win   = 0
        if res is not None:
            shift, curvature, n_active_ref, n_active_win = res

        # Component weights — kept separate for inspection in the YAML.
        # Sensitivity is computed from the REFERENCE window's profile only:
        # it's a per-unit constant across the session, computed once when
        # the unit is registered against its reference geometric snapshot.
        w_count       = math.sqrt(n_ref * n_win)
        w_active      = math.sqrt(n_active_ref * n_active_win)
        w_curvature   = curvature
        w_sensitivity = profile_drift_sensitivity(ref_p, depths)

        if weight_mode == "count":
            w_total = w_count
        elif weight_mode == "sharpness":
            w_total = w_curvature * w_active * w_count
        else:  # "geometry" (default)
            w_total = w_sensitivity * w_curvature * w_active * w_count

        if flagged or shift is None or w_total <= 0:
            w_total = 0.0

        details.append({
            "unit":              uid,
            # CoM values are still reported for reference / visual inspection
            "ref_com_um":        round(ref_com, 2) if not math.isnan(ref_com) else None,
            "win_com_um":        round(win_com, 2) if not math.isnan(win_com) else None,
            # The xcorr shift — the primary per-unit displacement estimate
            "xcorr_shift_um":    round(shift, 2) if shift is not None else None,
            # CoM difference retained as a cross-check (should be close to xcorr_shift)
            "com_delta_um":      round(win_com - ref_com, 2)
                                 if not (math.isnan(ref_com) or math.isnan(win_com)) else None,
            "weight":            round(w_total, 4),
            # Component breakdown — exposed so users can see WHY a unit
            # got its weight (e.g. sensitivity 0.005 = far-field unit;
            # curvature 0.02 = ambiguous xcorr; n_active 2 = barely-
            # resolved profile).
            "weight_sensitivity":round(w_sensitivity, 5),
            "weight_curvature":  round(w_curvature, 3),
            "weight_n_active":   round(w_active, 2),
            "weight_n_spikes":   round(w_count, 1),
            "outlier":           flagged,
        })

        if not flagged and shift is not None and w_total > 0:
            shifts.append(shift)
            weights.append(w_total)

    if not shifts:
        return None, 0, details

    drift = weighted_median(np.array(shifts), np.array(weights))
    return round(drift, 2), len(shifts), details


# ─────────────────────────────────────────────────────────────────────────────
# Method 2: amplitude-profile cross-correlation
# ─────────────────────────────────────────────────────────────────────────────

def build_profile(spk_idx: np.ndarray,
                  wf_all:  Optional[np.ndarray],
                  clu:     np.ndarray,
                  noise:   set[int],
                  n_sites: int) -> np.ndarray:
    """Mean PTP amplitude per site for all non-noise spikes in a window."""
    if len(spk_idx) == 0:
        return np.zeros(n_sites)
    valid = spk_idx[np.array([clu[i] not in noise for i in spk_idx], dtype=bool)]
    if len(valid) == 0:
        return np.zeros(n_sites)
    if wf_all is not None:
        return ptp(wf_all[valid]).mean(axis=0)
    return np.full(n_sites, float(len(valid)))


def xcorr_shift(ref: np.ndarray, win: np.ndarray, depths: np.ndarray
                ) -> Optional[tuple[float, float]]:
    """
    Sub-sample depth shift (µm) via cross-correlation of amplitude profiles.
    Parabolic interpolation around the peak gives sub-site precision.

    Returns
    -------
    (shift_um, peak_curvature) or None on failure.

    ``peak_curvature`` is the parabolic-fit second-difference at the peak
    (``2*y_peak − y_prev − y_next``) on the *L2-normalised* profiles.
    Range is approximately [0, 1] for typical neural profiles: ≈0 means a
    flat/ambiguous peak (low confidence in the shift estimate); ≈1 means
    a sharp single-site-wide peak (high confidence).  Profiles whose peak
    sits at the cross-correlation boundary, or whose parabolic
    denominator is degenerate, are returned with curvature = 0 so callers
    can downweight them without throwing the estimate away.
    """
    if ref.sum() == 0 or win.sum() == 0 or len(depths) < 3:
        return None
    r = ref / (np.linalg.norm(ref) + 1e-12)
    w = win / (np.linalg.norm(win) + 1e-12)
    try:
        from scipy.signal import correlate
        cc = correlate(r, w, mode="full")
    except ImportError:
        cc = np.correlate(r, w, mode="full")
    n     = len(r)
    lags  = np.arange(-(n - 1), n, dtype=float)
    pk    = int(np.argmax(cc))
    # Parabolic interpolation
    if 1 <= pk <= len(cc) - 2:
        y0, y1, y2 = float(cc[pk - 1]), float(cc[pk]), float(cc[pk + 1])
        denom      = 2 * (2 * y1 - y0 - y2)
        frac       = (y0 - y2) / denom if abs(denom) > 1e-12 else 0.0
        lag        = lags[pk] + frac
        # Curvature: the second-difference at the peak.  This IS the
        # numerator of `denom` divided by 2 (i.e. the negative of the
        # discrete second derivative).  On L2-normalised profiles it is
        # bounded in [0, 1] for any reasonable peak.
        curvature  = max(0.0, 2 * y1 - y0 - y2)
    else:
        # Peak at the boundary — parabolic fit not defined.  Take the
        # integer lag but report curvature 0 so the caller can downweight.
        lag       = float(lags[pk])
        curvature = 0.0
    # Filter NaN depths (null geometry sites) before computing spacing
    valid_depths = depths[np.isfinite(depths)] if len(depths) > 0 else depths
    spacing = float(np.median(np.diff(np.sort(valid_depths)))) \
              if len(valid_depths) > 1 else 50.0
    return round(float(lag * spacing), 2), round(curvature, 4)


# ─────────────────────────────────────────────────────────────────────────────
# Per-shank driver
# ─────────────────────────────────────────────────────────────────────────────

def estimate_shank_drift(
        session: str, group_idx: int, depths: np.ndarray,
        sampling_rate: float, window_sec: float,
        min_units: int, min_spikes: int,
        exclude_noise: bool, outlier_threshold: float,
        n_samp: int = 32,
        weight_mode: str = "geometry",
) -> Optional[dict]:

    res_path = f"{session}.res.{group_idx}"
    clu_path = f"{session}.clu.{group_idx}"
    # Prefer .spkD.N (stderiv pipeline) over .spk.N.  Same on-disk layout —
    # int16 sample-major, full nCG channels (the stderiv transform is
    # in-place, no channel is dropped: see STANDARDIZATION.md §3.3).
    # Channel-dropping happens at .fetD time, not .spkD time, so n_sites
    # is exactly len(depths) for both paths.
    _spkD = f"{session}.spkD.{group_idx}"
    _spk  = f"{session}.spk.{group_idx}"
    spk_path = _spkD if os.path.isfile(_spkD) else _spk

    if not os.path.isfile(res_path) or not os.path.isfile(clu_path):
        return None

    res = read_res(res_path)
    clu = read_clu(clu_path)
    n   = min(len(res), len(clu))
    res, clu = res[:n], clu[:n]
    if n == 0:
        return None

    noise: set[int] = {0, 1} if exclude_noise else set()
    good_units      = sorted(set(clu.tolist()) - noise)

    # Early-skip: no real units in this group's .clu — every spike landed in
    # cluster 0 (artefact) or 1 (MUA).  This is common for groups that
    # operators have triaged down to noise after deciding the shank is bad,
    # or for shanks where the automatic sort never produced anything sortable.
    # Drift cannot be estimated without a population of single units to
    # track, so we return a tombstone entry instead of None: ndm_applydrift
    # then sees the group as *intentionally skipped* (vs. *never processed*)
    # and can fall back to a sibling source group on the same probe instead
    # of failing with the cryptic "source group N not found in .drift" error.
    # The tombstone carries skipReason so downstream tools can surface a
    # clear explanation.
    if len(good_units) == 0:
        clu_unique = sorted(set(clu.tolist()))
        print(f"  [info] group {group_idx}: noise-only .clu "
              f"(ids={clu_unique}); skipping drift estimation",
              file=sys.stderr)
        return {
            "spikeGroup":   group_idx,
            "skipReason":   "noise-only-clu",
            "skipDetail":   (f"all spikes assigned to clusters "
                             f"{sorted(noise & set(clu_unique))} "
                             f"(noise / artefact); no real units to track"),
            "nSpikes":      int(n),
            "nClusters":    len(clu_unique),
            "windows":      [],
        }

    n_sites  = len(depths)
    # n_samp is passed in from the YAML nSamples field for this group.
    # Default 32 is the ndmanager process_extractspikes default.

    # Load waveforms
    has_spk = os.path.isfile(spk_path)
    wf_all: Optional[np.ndarray] = None
    if has_spk:
        try:
            wf_all = read_spk(spk_path, n_sites, n_samp, n)
            k      = wf_all.shape[0]
            if k < n:
                res, clu = res[:k], clu[:k]
        except Exception as exc:
            print(f"  [warn] group {group_idx}: .spk unreadable ({exc})", file=sys.stderr)
            has_spk = False

    # Global CoM and amplitude profile per unit (whole recording)
    global_com:     dict[int, float]      = {}
    global_profile: dict[int, np.ndarray] = {}
    for uid in good_units:
        idx = np.where(clu == uid)[0]
        if len(idx) < min_spikes:
            continue
        if has_spk:
            wf_uid = wf_all[idx]
            global_com[uid]     = unit_com(wf_uid, depths)
            global_profile[uid] = unit_amplitude_profile(wf_uid)
        else:
            global_com[uid]     = float(np.median(depths))
            global_profile[uid] = np.ones(n_sites)

    qualified = [u for u, c in global_com.items() if not math.isnan(c)]
    print(f"  group {group_idx}: {len(qualified)} qualified units", file=sys.stderr)

    win_samp  = int(window_sec * sampling_rate)
    n_windows = max(1, math.ceil(int(res.max()) / win_samp))

    # Find reference window: first window where ≥ min_units fire ≥ min_spikes
    ref_idx:      Optional[int]                = None
    ref_coms:     dict[int, float]             = {}
    ref_profiles: dict[int, np.ndarray]        = {}
    ref_cnts:     dict[int, int]               = {}
    ref_pop_profile: Optional[np.ndarray]      = None

    for w in range(n_windows):
        s0, s1  = w * win_samp, (w + 1) * win_samp
        win_idx = np.where((res >= s0) & (res < s1))[0]
        coms: dict[int, float]      = {}
        profs: dict[int, np.ndarray] = {}
        cnts: dict[int, int]         = {}
        for uid in qualified:
            spk = win_idx[clu[win_idx] == uid]
            if len(spk) < min_spikes:
                continue
            if has_spk:
                wf_uid = wf_all[spk]
                com    = unit_com(wf_uid, depths)
                prof   = unit_amplitude_profile(wf_uid)
            else:
                com  = global_com[uid]
                prof = global_profile[uid]
            if not math.isnan(com):
                coms[uid]  = com
                profs[uid] = prof
                cnts[uid]  = len(spk)
        if len(coms) >= min_units:
            ref_idx         = w
            ref_coms        = coms
            ref_profiles    = profs
            ref_cnts        = cnts
            if has_spk:
                ref_pop_profile = build_profile(win_idx, wf_all, clu, noise, n_sites)
            break

    if ref_idx is None:
        # Same tombstone treatment as the noise-only case above.  This branch
        # fires when the group has real units (good_units > 0) but they fire
        # too sparsely to reach min_units in any single window — typical for
        # groups with one or two low-rate units, or for shanks that drift
        # away from active brain regions partway through.  Drift estimation
        # against <min_units reference sources would be unreliable, so we
        # skip with an explicit tombstone rather than silently dropping the
        # group from .drift.
        print(f"  [info] group {group_idx}: no window with ≥{min_units} units "
              f"({len(qualified)} qualified); skipping drift estimation",
              file=sys.stderr)
        return {
            "spikeGroup":   group_idx,
            "skipReason":   "insufficient-units-per-window",
            "skipDetail":   (f"{len(qualified)} qualified unit(s) globally, "
                             f"but no {window_sec:.0f}s window had "
                             f"≥{min_units} units firing ≥{min_spikes} spikes"),
            "nSpikes":          int(n),
            "nQualifiedUnits":  len(qualified),
            "minUnitsRequired": min_units,
            "windows":          [],
        }

    windows_out: list[dict] = []

    for w in range(n_windows):
        s0, s1  = w * win_samp, (w + 1) * win_samp
        win_idx = np.where((res >= s0) & (res < s1))[0]

        # Build per-unit CoM and amplitude profile for this window
        win_coms:     dict[int, float]      = {}
        win_profiles: dict[int, np.ndarray] = {}
        win_cnts:     dict[int, int]        = {}
        for uid in qualified:
            spk = win_idx[clu[win_idx] == uid]
            if len(spk) < min_spikes:
                continue
            if has_spk:
                wf_uid = wf_all[spk]
                com    = unit_com(wf_uid, depths)
                prof   = unit_amplitude_profile(wf_uid)
            else:
                com  = global_com[uid]
                prof = global_profile[uid]
            if not math.isnan(com):
                win_coms[uid]     = com
                win_profiles[uid] = prof
                win_cnts[uid]     = len(spk)

        # Method 1: per-unit spatial-profile xcorr
        if w == ref_idx:
            d_m1, n_m1, detail = 0.0, len(ref_coms), []
        else:
            d_m1, n_m1, detail = method1_per_unit_xcorr(
                ref_profiles, win_profiles,
                ref_coms,     win_coms,
                ref_cnts,     win_cnts,
                depths,       outlier_threshold,
                weight_mode = weight_mode)

        # Method 2: population xcorr
        d_m2: Optional[float]      = None
        d_m2_curvature: float       = 0.0
        if has_spk and ref_pop_profile is not None:
            win_pop_profile = build_profile(win_idx, wf_all, clu, noise, n_sites)
            res_m2 = xcorr_shift(ref_pop_profile, win_pop_profile, depths)
            if res_m2 is not None:
                d_m2, d_m2_curvature = res_m2

        # Combined estimate
        if d_m1 is not None and n_m1 >= min_units:
            combined = round((d_m1 * n_m1 + (d_m2 or d_m1)) / (n_m1 + 1), 2)
        elif d_m2 is not None:
            combined = d_m2
        else:
            combined = d_m1

        windows_out.append({
            "t_start":  round(w * window_sec, 3),
            "t_end":    round((w + 1) * window_sec, 3),
            "drift_um": combined,
            "per_unit_xcorr": {
                "drift_um":       d_m1,
                "n_inlier_units": n_m1,
                # Each entry: unit id, xcorr shift, CoM delta (cross-check),
                # weight (total + breakdown), outlier flag
                "units": detail,
            },
            "xcorr_population": {
                "drift_um":       d_m2,
                # Population peak curvature is also reported so users can
                # judge fallback-method confidence visually.  Range [0,1].
                "peak_curvature": round(d_m2_curvature, 4),
            },
        })

    valid = [w["drift_um"] for w in windows_out if w["drift_um"] is not None]
    dur_s = int(res.max()) / sampling_rate
    rate  = round((max(valid) - min(valid)) / (dur_s / 3600), 2) \
            if len(valid) >= 2 and dur_s > 0 else None

    return {
        "spikeGroup":          group_idx,
        "nUnitsQualified":     len(qualified),
        "nUnitsInRef":         len(ref_coms),
        "refWindowIndex":      ref_idx,
        "refWindowT_start":    round(ref_idx * window_sec, 1),
        "maxAbsDrift_um":      round(max(abs(d) for d in valid), 2) if valid else None,
        "driftRate_um_per_hr": rate,
        "windows":             windows_out,
    }



# ─────────────────────────────────────────────────────────────────────────────
# Multiprocessing worker (module-level so it's picklable for spawn)
# ─────────────────────────────────────────────────────────────────────────────

from dataclasses import dataclass

@dataclass
class _ShankJob:
    """Self-contained per-shank work item.

    All fields are simple types (str, int, float, np.ndarray) so the
    object pickles cleanly across the spawn boundary.  The depths array
    is NumPy but small (≤ a few hundred floats) — pickling it is
    cheaper than re-resolving probe geometry in each child.
    """
    session:           str
    group_idx:         int
    depths:            np.ndarray
    sampling_rate:     float
    window_sec:        float
    min_units:         int
    min_spikes:        int
    exclude_noise:     bool
    outlier_threshold: float
    n_samp:            int
    weight_mode:       str
    probe_id:          int
    shank_index:       int


def _run_shank(job: _ShankJob) -> Optional[dict]:
    """Worker entrypoint: thin wrapper around ``estimate_shank_drift``.

    Lives at module scope so ``multiprocessing.Pool`` (spawn context)
    can import it in child processes without re-running the
    ``__main__`` block.
    """
    return estimate_shank_drift(
        job.session, job.group_idx, job.depths,
        job.sampling_rate, job.window_sec,
        job.min_units, job.min_spikes,
        job.exclude_noise, job.outlier_threshold,
        n_samp      = job.n_samp,
        weight_mode = job.weight_mode,
    )


# ─────────────────────────────────────────────────────────────────────────────
# Probe / group mapping helpers
# ─────────────────────────────────────────────────────────────────────────────

def build_group_probe_map(param: dict) -> dict:
    """Return {1-based group index: (probe_id, shank_index)}.

    Resolution order (highest to lowest priority):
    1. probeId/shankIndex directly on spikeDetection.channelGroups[g]
       (written by ndm_setupgroups >= neurosuite-3 r2)
    2. Cross-reference via anatomicalDescription.channelGroups[g].probeId/shankIndex
       (anatomical groups written by ndm_setupgroups but spike groups may predate it)
    3. Fallback: probe 0, shankIndex = group_index - 1
       (backward compatibility with pre-ndm_setupgroups YAML files)
    """
    result: dict[int, tuple[int, int]] = {}
    spk_groups  = (param or {}).get("spikeDetection",      {}).get("channelGroups", [])
    anat_groups = (param or {}).get("anatomicalDescription",{}).get("channelGroups", [])
    for i, grp in enumerate(spk_groups):
        gnum = i + 1
        if "probeId" in grp:
            # canonical: probeId/shankIndex written directly on the spike group
            probe_id    = int(grp["probeId"])
            shank_index = int(grp.get("shankIndex", i))
        elif i < len(anat_groups) and "probeId" in anat_groups[i]:
            # cross-reference: anatomical group has the geometry fields
            probe_id    = int(anat_groups[i]["probeId"])
            shank_index = int(anat_groups[i].get("shankIndex", i))
        else:
            # backward compatibility: assume all groups on probe 0
            probe_id    = 0
            shank_index = i
        result[gnum] = (probe_id, shank_index)
    return result


def build_probe_entry_map(param: dict) -> dict:
    """Return {probe_id: probe-entry dict} from the optional top-level
    ``probes`` list in the parameter file.  Returns {0: {}} when the list
    is absent so callers always get a valid entry for probe 0.

    Accepts both "probeId" (canonical, written by ndm_setupgroups) and the
    legacy "id" field used by pre-neurosuite-3 YAML files.
    """
    result: dict[int, dict] = {0: {}}
    for entry in (param or {}).get("probes", []):
        pid = int(entry.get("probeId", entry.get("id", 0)))
        result[pid] = entry
    return result


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────


# ─────────────────────────────────────────────────────────────────────────────
# Binary drift signal output  (session.dat.drift.N)
# ─────────────────────────────────────────────────────────────────────────────

def write_drift_dat_files(session: str,
                          probes_out: list,
                          probe_order: list,
                          sampling_rate: float,
                          n_total_samples: int) -> None:
    """
    Write one binary int16 file per probe: <session>.dat.drift.<probe_idx>

    probe_idx is the 1-based position of the probe in the YAML probes list.
    The file contains n_total_samples int16 values, one per recording sample,
    in µm, relative to the reference window (0 µm at the reference).

    The per-window drift estimates (60-second resolution) are upsampled to
    full sampling rate by piecewise-linear interpolation between window
    midpoints, clamped to the first/last value at the recording boundaries.

    Parameters
    ----------
    session         : session base name (e.g. 'jg05-20120312')
    probes_out      : list of per-probe result dicts (from main())
    probe_order     : ordered list of probeId values matching the YAML
                      probes list (used to derive the 1-based file index)
    sampling_rate   : Hz
    n_total_samples : number of samples in session.dat
    """
    # Build probeId → 1-based position from the ordered list
    probe_idx_map = {pid: i + 1 for i, pid in enumerate(probe_order)}

    for probe_entry in probes_out:
        pid   = probe_entry["probeId"]
        idx   = probe_idx_map.get(pid, pid + 1)   # fallback: probeId + 1
        fname = f"{session}.dat.drift.{idx}"

        # Collect windows from the first shank that has them (shanks of the
        # same probe are co-registered — they share the same rigid-body drift)
        windows = []
        for shank in probe_entry.get("shanks", []):
            wins = shank.get("windows", [])
            if wins:
                windows = wins
                break

        if not windows:
            print(f"  [info] probe {pid}: no drift windows — skipping {fname}",
                  file=sys.stderr)
            continue

        # Build (t_mid, drift_um) pairs, skipping None drift values
        t_mid_list:  list[float] = []
        drift_list:  list[float] = []
        for w in windows:
            d = w.get("drift_um")
            if d is None:
                continue
            t_s = float(w.get("t_start", 0.0))
            t_e = float(w.get("t_end",   t_s + 60.0))
            t_mid_list.append(0.5 * (t_s + t_e))
            drift_list.append(float(d))

        if len(t_mid_list) < 1:
            print(f"  [info] probe {pid}: all windows have null drift — "
                  f"skipping {fname}", file=sys.stderr)
            continue

        t_mid    = np.array(t_mid_list, dtype=np.float64)
        drift_um = np.array(drift_list, dtype=np.float64)

        # Upsample to full sampling rate via piecewise-linear interpolation.
        # np.interp clamps to first/last value outside the boundary — correct.
        sample_times = np.arange(n_total_samples, dtype=np.float64) / sampling_rate
        drift_full   = np.interp(sample_times, t_mid, drift_um)

        # Convert to int16 (µm, round-to-nearest, clamp to ±32767)
        drift_i16 = np.clip(np.round(drift_full), -32767, 32767).astype('<i2')

        drift_i16.tofile(fname)
        lo, hi = int(drift_i16.min()), int(drift_i16.max())
        print(f"  Wrote {fname}  ({n_total_samples} samples, "
              f"range [{lo}, {hi}] µm)", file=sys.stderr)

def main() -> int:
    args          = parse_args()
    exclude_noise = str(args.exclude_noise).lower() not in ("false", "0", "no")

    with open(args.param_file) as f:
        param = yaml.safe_load(f)

    lib_paths = find_probe_library(args.probe_library)
    if (param or {}).get("probeLibraryPath"):
        lib_paths.insert(0, param["probeLibraryPath"])

    g_probe_map   = build_group_probe_map(param)
    p_entry_map   = build_probe_entry_map(param)
    probe_cache: dict[str, Optional[dict]] = {}

    def get_depths(g: int) -> np.ndarray:
        # Priority 1: sitePositions_um embedded directly in spikeDetection group
        spk_groups = (param or {}).get("spikeDetection", {}).get("channelGroups", [])
        if 0 < g <= len(spk_groups):
            inline = spk_groups[g - 1].get("sitePositions_um")
            if inline:
                # Return full-length array (one depth per channel in the group).
                # Null entries become NaN; estimate_shank_drift will skip NaN sites
                # when computing CoM / amplitude profiles.
                # This ensures len(depths) == nChannels in the .spk file.
                depths = np.array(
                    [float(xy[1]) if xy is not None else float("nan")
                     for xy in inline], dtype=float)
                if np.any(np.isfinite(depths)):
                    return depths
        # Priority 2: probe file via probeId/shankIndex
        pid, shk = g_probe_map.get(g, (0, g - 1))
        entry    = p_entry_map.get(pid)
        if entry is None:
            return default_site_depths(8)
        pf = entry.get("probeFile", "")
        if pf not in probe_cache:
            probe_cache[pf] = load_probe_file(pf, lib_paths)
        pd = probe_cache[pf]
        return site_depths_from_probe(pd, shk) if pd else default_site_depths(8)

    all_results: list[tuple[int, int, int, dict]] = []

    source_group = args.source_group  # 0 = all groups
    source_result: Optional[dict] = None

    # Parse per-group nSamples (comma-separated list, 1-based groups)
    n_samp_list: list[int] = []
    if args.n_samples_per_group:
        try:
            n_samp_list = [int(x.strip()) for x in
                           args.n_samples_per_group.split(",")]
        except ValueError:
            print(f"  [warn] --n-samples-per-group parse error; "
                  f"using default 32 for all groups", file=sys.stderr)

    # Determine which shanks to process.
    if source_group:
        shank_ids = [source_group]
    else:
        shank_ids = list(range(1, args.n_groups + 1))

    # Pre-resolve depths and per-group nSamples in the parent — probe-file
    # loading hits a cache, so doing it once here avoids each child
    # repeatedly parsing the same probe YAML.
    shank_jobs: list[_ShankJob] = []
    for g in shank_ids:
        pid, shk = g_probe_map.get(g, (0, g - 1))
        d        = get_depths(g)
        n_samp   = n_samp_list[g - 1] if g <= len(n_samp_list) else 32
        print(f"  Group {g}: probe={pid} shank={shk} sites={len(d)} "
              f"nSamp={n_samp}", file=sys.stderr)
        shank_jobs.append(_ShankJob(
            session             = args.session,
            group_idx           = g,
            depths              = d,
            sampling_rate       = args.sampling_rate,
            window_sec          = args.window_sec,
            min_units           = args.min_units,
            min_spikes          = args.min_spikes,
            exclude_noise       = exclude_noise,
            outlier_threshold   = args.outlier_threshold,
            n_samp              = n_samp,
            weight_mode         = args.weight_mode,
            probe_id            = pid,
            shank_index         = shk,
        ))

    # Resolve worker count.  0 → all CPUs; clamp to job count so we
    # don't spawn idle workers for a single-shank session.
    requested_workers = args.n_workers if args.n_workers != 0 else os.cpu_count() or 1
    n_workers         = max(1, min(requested_workers, len(shank_jobs)))

    if n_workers <= 1 or len(shank_jobs) <= 1:
        # Sequential path — preserves the historical stderr ordering and
        # avoids fork/import overhead for small jobs.
        for job in shank_jobs:
            r = _run_shank(job)
            if r is not None:
                all_results.append((job.probe_id, job.shank_index,
                                    job.group_idx, r))
                if source_group:
                    source_result = r
    else:
        # Parallel path.  Each child process opens its own memmaps and
        # parses its own .res/.clu, so there's no shared state to
        # synchronize.  imap_unordered lets results stream back as soon
        # as each shank finishes — useful when shanks have very
        # unbalanced spike counts.
        from multiprocessing import get_context
        # Use 'spawn' to avoid inheriting open file descriptors from the
        # parent (NumPy + memmap interact poorly with fork on some
        # platforms; spawn is reliably correct everywhere).
        ctx = get_context("spawn")
        with ctx.Pool(processes=n_workers) as pool:
            for job, r in zip(shank_jobs,
                               pool.imap(_run_shank, shank_jobs)):
                if r is not None:
                    all_results.append((job.probe_id, job.shank_index,
                                        job.group_idx, r))
                    if source_group:
                        source_result = r

    # --source-group mode: propagate the source shank's drift windows to
    # all sibling groups on the same probe that were NOT estimated.
    # Skip propagation when the source itself was tombstoned (skipReason
    # set) — we'd just be cloning empty windows everywhere, which gives
    # ndm_applydrift nothing useful to chunk against.
    if (source_group
            and source_result is not None
            and source_result.get("skipReason")):
        # User asked for an explicit source group, but that group could not
        # be estimated.  Surface this loudly so the operator knows the
        # propagation step is being skipped — without this, the .drift file
        # gets a single tombstone entry and ndm_applydrift's confused error
        # message ("source group N not found") only shows up much later.
        print(
            f"  [warn] --source-group {source_group}: shank was skipped "
            f"({source_result['skipReason']}); not propagating to siblings",
            file=sys.stderr)
        print(
            f"  [warn]   re-run with --source-group <g> pointing at a group "
            f"that has real units, or omit --source-group to estimate all "
            f"groups independently",
            file=sys.stderr)
    if (source_group
            and source_result is not None
            and not source_result.get("skipReason")):
        src_pid = g_probe_map.get(source_group, (0, source_group - 1))[0]
        for g in range(1, args.n_groups + 1):
            if g == source_group:
                continue
            pid, shk = g_probe_map.get(g, (0, g - 1))
            if pid != src_pid:
                continue  # different probe — skip
            # Clone the source result under this shank's identity.
            import copy
            sibling = copy.deepcopy(source_result)
            sibling["spikeGroup"]    = g
            sibling["derivedFrom"]   = source_group
            all_results.append((pid, shk, g, sibling))

    probe_ids  = sorted({r[0] for r in all_results})
    probes_out = []
    for pid in probe_ids:
        shanks = [{"shankIndex": shk, **r}
                  for p2, shk, _, r in sorted(all_results, key=lambda x: x[1])
                  if p2 == pid]
        probes_out.append({
            "probeId": pid,
            "label":   p_entry_map.get(pid, {}).get("label", ""),
            "shanks":  shanks,
        })

    doc = {"drift": {
        "format":           "1.0",
        "methods":          ["per_unit_xcorr", "xcorr_population"],
        "primaryMethod":    "per_unit_xcorr",
        "windowSec":        args.window_sec,
        "minUnits":         args.min_units,
        "minSpikes":        args.min_spikes,
        "outlierThreshold": args.outlier_threshold,
        "weightMode":       args.weight_mode,
        "probes":           probes_out,
    }}

    with open(args.output, "w") as f:
        yaml.dump(doc, f, default_flow_style=False, allow_unicode=True, sort_keys=False)

    print(f"Wrote {args.output}", file=sys.stderr)

    # ── Write binary drift signal files ──────────────────────────────────
    dat_path = f"{args.session}.dat"
    if os.path.isfile(dat_path):
        n_total = os.path.getsize(dat_path) // (args.n_bits // 8) // args.n_channels
        # probe_order: probeIds in their YAML list order
        probe_order = [e.get("probeId", i)
                       for i, e in enumerate((param or {}).get("probes", []))]
        # Fallback: if no probes list, use the order they appear in our output
        if not probe_order:
            probe_order = [p["probeId"] for p in probes_out]
        write_drift_dat_files(args.session, probes_out, probe_order,
                               args.sampling_rate, n_total)
    else:
        print(f"  [info] {dat_path} not found — binary drift files not written",
              file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
