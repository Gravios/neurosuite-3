#!/usr/bin/env python3
"""
process_localise.py
===================
Point-source inverse model for spike localisation from extracellular recordings.

For each spike in SESSION.spk.N, this script estimates the position of the
source neuron in the plane of the probe shank by fitting the monopole model:

    V_i = A / sqrt((x_i - x_s)^2 + (y_i - y_s)^2 + z_s^2)

where
    (x_i, y_i)  — known electrode site positions (µm) from probe geometry
    (x_s, y_s)  — estimated source position in the shank plane (µm)
    z_s         — estimated source distance perpendicular to shank face (µm)
    A           — unsigned amplitude proportional to I·ρ/(4π)

The observation vector is the per-channel peak-to-peak (PTP) amplitude of
each spike.  The fit minimises the sum of squared residuals between the
measured PTP vector and the model prediction, in log space to stabilise the
scale of A relative to position.

MODEL NOTES
-----------
The monopole model is a good approximation when:
  - The electrode is within ~100 µm of the source (far-field; kappa*r >> 1)
  - The recording is wideband or high-pass filtered
  - The medium is isotropic (homogeneous resistivity)

For tetrodes where all sites are at y=0, z_s is the dominant free parameter
and captures the distance of the neuron from the wire tips.  x_s reflects
lateral offset within the bundle cross-section.

OUTPUT FORMAT
-------------
SESSION.loc.N — binary float32, little-endian, NO HEADER:
  [x_um, y_um, z_um, amplitude, residual_rms]  × n_spikes
  (5 floats per spike; n_spikes from SESSION.spk.N)

  x_um, y_um   Source position in probe plane (µm from shank tip at (0,0))
  z_um         Perpendicular distance from shank face (µm); always > 0
  amplitude    Fitted amplitude A (ADC-count × µm; unsigned)
  residual_rms RMS of (V_model - V_observed) in normalised units; 0 = perfect

SESSION.loc.yaml — metadata sidecar:
  group, method, n_spikes, n_sites, probe_file, site_positions_um, etc.

Spikes whose .clu label is noise (≤1) are localised but flagged with
residual_rms = NaN so downstream tools can filter them.

DEPENDENCIES
------------
  python >= 3.10, numpy, scipy, pyyaml

Copyright (C) 2025 neurosuite-3 contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations
import argparse, math, os, sys, warnings
from pathlib import Path
from typing import Optional

import numpy as np
import yaml

try:
    from scipy.optimize import least_squares
    HAS_SCIPY = True
except ImportError:
    HAS_SCIPY = False
    print("WARNING: scipy not available; falling back to gradient descent",
          file=sys.stderr)


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--session",        required=True)
    p.add_argument("--param-file",     required=True)
    p.add_argument("--group",          type=int,   required=True)
    p.add_argument("--n-sites",        type=int,   required=True)
    p.add_argument("--n-samples",      type=int,   default=32)
    p.add_argument("--sampling-rate",  type=float, default=32552.0)
    p.add_argument("--method",         default="monopole",
                   choices=["monopole", "dipole", "com"],
                   help="Inverse model (monopole=default, dipole=not-yet, com=fast centre-of-mass)")
    p.add_argument("--cluster-filter", default="good",
                   choices=["all", "good", "single"],
                   help="Which spikes to localise: all / good (clu>1) / single (isolated)")
    p.add_argument("--n-bootstrap",    type=int,   default=0,
                   help="Bootstrap replicates per spike for uncertainty (0=disabled)")
    p.add_argument("--max-dist-um",    type=float, default=200.0,
                   help="Reject fit if source is >this distance from nearest site (µm)")
    p.add_argument("--probe-library",  default="")
    p.add_argument("--output",         required=True,
                   help="Path for SESSION.loc.N binary output")
    return p.parse_args()


# ─────────────────────────────────────────────────────────────────────────────
# Probe geometry
# ─────────────────────────────────────────────────────────────────────────────

def probe_library_paths(override: str = "") -> list[str]:
    paths: list[str] = []
    if override:
        paths.append(override)
    env = os.environ.get("NEUROSUITE_PROBE_PATH", "")
    if env:
        paths.append(env)
    paths += [
        str(Path.home() / ".local/share/neurosuite/probes"),
        "/usr/local/share/neurosuite/probes",
        "/usr/share/neurosuite/probes",
        "probes",
    ]
    return paths


def load_probe(probe_file: str, library_paths: list[str]) -> Optional[dict]:
    for lib in ["", *library_paths]:
        cand = os.path.join(lib, probe_file) if lib else probe_file
        if os.path.isfile(cand):
            with open(cand) as f:
                doc = yaml.safe_load(f)
            return doc.get("probeFile") if doc else None
    return None


def site_positions_from_probe(probe: dict, n_sites: int,
                               shank_index: int = 0) -> np.ndarray:
    """
    Return (n_sites, 2) array of [x_um, y_um] positions for this shank.
    Coordinate origin is the shank tip; y increases toward the base.
    """
    sites   = probe.get("sites", {})
    geom    = sites.get("geometry")
    if geom and isinstance(geom, list) and len(geom) >= n_sites:
        return np.array(geom[:n_sites], dtype=float)

    # Geometry groups (compact format used by some probes)
    gg = sites.get("geometry_groups")
    if gg and isinstance(gg, dict):
        ng  = gg.get("n_groups", 1)
        gs  = gg.get("group_spacing_um", 500)
        wg  = gg.get("within_group", [])
        pts = [[s[0] + shank_index * gs, s[1] + grp * gs]
               for grp in range(ng) for s in wg]
        if pts:
            return np.array(pts[:n_sites], dtype=float)

    # Fallback: linear array with stated or default spacing
    sp = sites.get("spacing_um") or 50.0
    return np.column_stack([np.zeros(n_sites), np.arange(n_sites) * sp])


def site_positions_from_yaml(params: dict, group_idx: int) -> Optional[np.ndarray]:
    """
    Try to read probe geometry from spikeDetection.channelGroups[g].
    probeId + shankIndex are cross-referenced to the probes: section.
    Returns (n_sites, 2) or None.
    """
    try:
        groups = params["spikeDetection"]["channelGroups"]
        grp    = groups[group_idx]
        channels = grp.get("channels", [])
        n_sites  = len(channels)
        probe_id    = grp.get("probeId")
        shank_index = grp.get("shankIndex", 0)
        if probe_id is None:
            return None
        probes = params.get("probes", [])
        probe_entry = next((p for p in probes if p.get("id") == probe_id), None)
        if probe_entry is None:
            return None
        return probe_entry, n_sites, int(shank_index)
    except (KeyError, IndexError, TypeError):
        return None


# ─────────────────────────────────────────────────────────────────────────────
# File I/O
# ─────────────────────────────────────────────────────────────────────────────

def read_spk(path: str, n_sites: int, n_samp: int) -> np.ndarray:
    """
    Read .spk.N binary (int16, sample-major: [n_spikes, n_samp, n_sites]).
    Returns (n_spikes, n_samp, n_sites) int16 array.
    """
    raw  = np.fromfile(path, dtype="<i2")
    nelem = n_samp * n_sites
    if raw.size % nelem != 0:
        # Truncate to last complete spike
        raw = raw[:raw.size - raw.size % nelem]
    n_spk = raw.size // nelem
    return raw.reshape(n_spk, n_samp, n_sites)


def read_clu(path: str) -> Optional[np.ndarray]:
    if not os.path.isfile(path):
        return None
    with open(path) as f:
        lines = [l.strip() for l in f if l.strip()]
    if len(lines) < 2:
        return np.array([], dtype=np.int32)
    return np.array([int(l) for l in lines[1:]], dtype=np.int32)


# ─────────────────────────────────────────────────────────────────────────────
# Monopole forward model
# ─────────────────────────────────────────────────────────────────────────────

def monopole_forward(xy_sites: np.ndarray, x_s: float, y_s: float,
                     z_s: float, amp: float) -> np.ndarray:
    """
    Predicted PTP amplitude at each site from a monopole source.

    xy_sites : (n_sites, 2) [x_um, y_um]
    x_s, y_s : source position in shank plane (µm)
    z_s      : perpendicular distance from shank face (µm)  — always > 0
    amp      : unsigned amplitude (ADC-count × µm)
    Returns  : (n_sites,) float
    """
    dx   = xy_sites[:, 0] - x_s
    dy   = xy_sites[:, 1] - y_s
    dist = np.sqrt(dx**2 + dy**2 + z_s**2)
    dist = np.maximum(dist, 1.0)          # prevent division by zero (< 1 µm)
    return amp / dist


def monopole_residuals_log(params: np.ndarray, xy_sites: np.ndarray,
                            obs_log: np.ndarray) -> np.ndarray:
    """
    Residuals in log-amplitude space for least_squares().
    params = [x_s, y_s, log(z_s), log(amp)]
    """
    x_s, y_s, log_z, log_a = params
    z_s = math.exp(log_z)
    amp = math.exp(log_a)
    pred = monopole_forward(xy_sites, x_s, y_s, z_s, amp)
    pred = np.maximum(pred, 1e-9)
    return np.log(pred) - obs_log


# ─────────────────────────────────────────────────────────────────────────────
# Centre-of-mass model (fast fallback)
# ─────────────────────────────────────────────────────────────────────────────

def com_localise(ptp: np.ndarray, xy_sites: np.ndarray) -> tuple:
    """
    Amplitude-weighted centre of mass in the shank plane.
    Returns (x_um, y_um, z_um=NaN, amplitude, residual_rms=NaN).
    z_um is not estimated by this method.
    """
    w     = np.maximum(ptp, 0.0)
    total = w.sum()
    if total < 1e-9:
        centroid = xy_sites.mean(axis=0)
        return centroid[0], centroid[1], float("nan"), 0.0, float("nan")
    x_s = float((w * xy_sites[:, 0]).sum() / total)
    y_s = float((w * xy_sites[:, 1]).sum() / total)
    return x_s, y_s, float("nan"), float(total), float("nan")


# ─────────────────────────────────────────────────────────────────────────────
# Per-spike monopole fit
# ─────────────────────────────────────────────────────────────────────────────

def monopole_fit(ptp: np.ndarray, xy_sites: np.ndarray,
                 max_dist_um: float) -> tuple[float, float, float, float, float]:
    """
    Fit monopole model to a single spike's per-channel PTP amplitudes.

    Returns (x_s, y_s, z_s, amplitude, residual_rms_normalised)
    All units in µm / ADC counts.  residual_rms is in log-amplitude space.
    Returns NaN tuple on failure.
    """
    n_sites = len(ptp)
    eps     = np.maximum(ptp, 1.0)   # log needs positive values
    obs_log = np.log(eps.astype(float))

    # Initial guess: centre of mass for (x_s, y_s), z_s = 25 µm, amp = max*25
    w      = np.maximum(ptp, 0.0)
    total  = w.sum()
    if total < 1e-9:
        return float("nan"), float("nan"), float("nan"), 0.0, float("nan")
    x0 = float((w * xy_sites[:, 0]).sum() / total)
    y0 = float((w * xy_sites[:, 1]).sum() / total)
    z0 = 25.0
    a0 = float(ptp.max()) * z0

    p0 = np.array([x0, y0, math.log(z0), math.log(max(a0, 1.0))])

    # Bounds: x,y free within ±max_dist_um of array extent; z in [1, 500]; amp > 0
    x_lo, x_hi = xy_sites[:, 0].min() - max_dist_um, xy_sites[:, 0].max() + max_dist_um
    y_lo, y_hi = xy_sites[:, 1].min() - max_dist_um, xy_sites[:, 1].max() + max_dist_um

    lo = [x_lo, y_lo, math.log(1.0),    math.log(0.1)]
    hi = [x_hi, y_hi, math.log(max_dist_um), math.log(1e9)]

    if HAS_SCIPY:
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore")
                result = least_squares(
                    monopole_residuals_log, p0,
                    args=(xy_sites, obs_log),
                    bounds=(lo, hi),
                    method="trf",
                    ftol=1e-6, xtol=1e-6, gtol=1e-6,
                    max_nfev=200,
                )
            if not result.success and result.cost > 1e6:
                return float("nan"), float("nan"), float("nan"), 0.0, float("nan")
            x_s, y_s, log_z, log_a = result.x
            z_s = math.exp(log_z)
            amp = math.exp(log_a)
            rms = float(np.sqrt(np.mean(result.fun**2)))
        except Exception:
            return float("nan"), float("nan"), float("nan"), 0.0, float("nan")
    else:
        # Simple gradient descent fallback (slower, less accurate)
        x_s, y_s, log_z, log_a = p0
        lr = 0.01
        for _ in range(500):
            res = monopole_residuals_log(
                np.array([x_s, y_s, log_z, log_a]), xy_sites, obs_log)
            z_s = math.exp(log_z); amp = math.exp(log_a)
            # Numerical gradient
            g = np.zeros(4)
            for j, delta in enumerate([1e-1, 1e-1, 1e-3, 1e-3]):
                pp = np.array([x_s, y_s, log_z, log_a]); pp[j] += delta
                g[j] = (monopole_residuals_log(pp, xy_sites, obs_log).mean()
                        - res.mean()) / delta
            x_s   -= lr * g[0]
            y_s   -= lr * g[1]
            log_z -= lr * g[2]
            log_a -= lr * g[3]
            log_z  = np.clip(log_z, lo[2], hi[2])
            log_a  = np.clip(log_a, lo[3], hi[3])
            x_s    = np.clip(x_s,   lo[0], hi[0])
            y_s    = np.clip(y_s,   lo[1], hi[1])
        z_s = math.exp(log_z); amp = math.exp(log_a)
        res = monopole_residuals_log(np.array([x_s, y_s, log_z, log_a]),
                                     xy_sites, obs_log)
        rms = float(np.sqrt(np.mean(res**2)))

    # Reject if source is implausibly far from any electrode site
    dist_to_sites = np.sqrt(((xy_sites - np.array([x_s, y_s]))**2).sum(axis=1))
    if dist_to_sites.min() > max_dist_um:
        return float("nan"), float("nan"), float("nan"), 0.0, float("nan")

    return float(x_s), float(y_s), float(z_s), float(amp), rms


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main() -> int:
    args = parse_args()

    # ── Load session YAML ─────────────────────────────────────────────────────
    with open(args.param_file) as f:
        params = yaml.safe_load(f)

    session  = args.session
    g        = args.group          # 1-based group index
    n_sites  = args.n_sites
    n_samp   = args.n_samples

    # ── Resolve probe geometry ────────────────────────────────────────────────
    lib_paths    = probe_library_paths(args.probe_library)
    xy_sites     = None
    probe_file_used = "none"

    result = site_positions_from_yaml(params, g - 1)  # 0-based in params
    if result is not None:
        probe_entry, ns_yaml, shank_idx = result
        probe_path = probe_entry.get("probeFile", "")
        if probe_path:
            probe = load_probe(probe_path, lib_paths)
            if probe:
                xy_sites = site_positions_from_probe(probe, n_sites, shank_idx)
                probe_file_used = probe_path

    if xy_sites is None or len(xy_sites) < n_sites:
        # Fallback: read raw geometry from YAML spikeDetection block if
        # probeFile is not set (manually configured groups).
        print(f"  Group {g}: probe file not found; "
              f"checking YAML spikeDetection geometry...", file=sys.stderr)
        try:
            groups = params["spikeDetection"]["channelGroups"]
            grp    = groups[g - 1]
            # ndmanager writes per-site geometry into the YAML after ndm_setupgroups
            geom_raw = grp.get("geometry")
            if geom_raw and len(geom_raw) >= n_sites:
                xy_sites = np.array(geom_raw[:n_sites], dtype=float)
                probe_file_used = "yaml-inline"
        except (KeyError, IndexError, TypeError):
            pass

    if xy_sites is None or len(xy_sites) < n_sites:
        # Last resort: uniform linear array at 50 µm pitch
        print(f"  Group {g}: no geometry found; "
              f"using uniform linear array (50 µm pitch)", file=sys.stderr)
        xy_sites = np.column_stack([np.zeros(n_sites),
                                    np.arange(n_sites) * 50.0])
        probe_file_used = "default-linear-50um"

    xy_sites = xy_sites[:n_sites].astype(float)

    # ── Load spike waveforms ──────────────────────────────────────────────────
    spk_path = f"{session}.spk.{g}"
    if not os.path.isfile(spk_path):
        print(f"ERROR: {spk_path} not found", file=sys.stderr)
        return 1

    spk = read_spk(spk_path, n_sites, n_samp)
    n_spk = spk.shape[0]
    print(f"  Group {g}: {n_spk} spikes, {n_sites} sites, {n_samp} samples/spike",
          file=sys.stderr)

    # ── Load cluster assignments (optional) ───────────────────────────────────
    clu_path = f"{session}.clu.{g}"
    clu      = read_clu(clu_path)
    if clu is None:
        print(f"  No .clu.{g} found; localising all spikes", file=sys.stderr)
        clu = np.full(n_spk, 2, dtype=np.int32)   # treat all as 'good'
    elif len(clu) < n_spk:
        # clu shorter than spk — pad with noise label
        pad = np.ones(n_spk - len(clu), dtype=np.int32)
        clu = np.concatenate([clu, pad])
    elif len(clu) > n_spk:
        clu = clu[:n_spk]

    # Build noise mask (cluster ≤ 1 = noise/unsorted)
    noise_mask = clu <= 1

    # ── Per-channel PTP amplitude ─────────────────────────────────────────────
    # spk layout: (n_spk, n_samp, n_sites) — sample-major
    ptp_all = (spk.max(axis=1) - spk.min(axis=1)).astype(np.float32)
    # (n_spk, n_sites)

    # ── Localise each spike ───────────────────────────────────────────────────
    results = np.full((n_spk, 5), float("nan"), dtype=np.float32)
    # Columns: x_um, y_um, z_um, amplitude, residual_rms

    method = args.method
    n_done = 0
    n_fail = 0

    for i in range(n_spk):
        ptp_i = ptp_all[i]

        if noise_mask[i]:
            # Localise noise spikes too but mark with NaN residual
            if method == "monopole":
                x_s, y_s, z_s, amp, _ = monopole_fit(ptp_i, xy_sites,
                                                       args.max_dist_um)
                results[i] = [x_s, y_s, z_s, amp, float("nan")]
            elif method == "com":
                x_s, y_s, z_s, amp, rms = com_localise(ptp_i, xy_sites)
                results[i] = [x_s, y_s, z_s, amp, float("nan")]
            continue

        if method == "monopole":
            x_s, y_s, z_s, amp, rms = monopole_fit(ptp_i, xy_sites,
                                                     args.max_dist_um)
        elif method == "com":
            x_s, y_s, z_s, amp, rms = com_localise(ptp_i, xy_sites)
        else:
            print(f"Unknown method '{method}'", file=sys.stderr)
            return 1

        results[i] = [x_s, y_s, z_s, amp, rms]

        if math.isnan(x_s):
            n_fail += 1
        else:
            n_done += 1

        if (i + 1) % 1000 == 0 or (i + 1) == n_spk:
            pct = 100.0 * (i + 1) / n_spk
            print(f"\r  {i+1:6d}/{n_spk}  ({pct:.1f}%)  "
                  f"ok={n_done} fail={n_fail}",
                  end="", file=sys.stderr)

    print(file=sys.stderr)
    n_noise = int(noise_mask.sum())
    print(f"  Done: {n_done} localised, {n_fail} failed, "
          f"{n_noise} noise (clu≤1)", file=sys.stderr)

    # ── Write binary output ───────────────────────────────────────────────────
    out_path = args.output
    results.tofile(out_path)
    print(f"  Written: {out_path}  ({os.path.getsize(out_path)/1024:.1f} KB)",
          file=sys.stderr)

    # ── Write YAML sidecar ────────────────────────────────────────────────────
    yaml_path = f"{session}.loc.yaml"
    # Load existing if present (other groups may have already written it)
    if os.path.isfile(yaml_path):
        with open(yaml_path) as f:
            meta = yaml.safe_load(f) or {"localisation": {"groups": []}}
    else:
        meta = {"localisation": {
            "format":  "1.0",
            "method":  method,
            "session": session,
            "columns": ["x_um", "y_um", "z_um", "amplitude", "residual_rms"],
            "dtype":   "float32-little-endian",
            "notes":   ("x/y: source position in shank plane (µm from tip). "
                        "z: perpendicular distance from shank face (µm). "
                        "amplitude: fitted A = I·rho/(4pi) (ADC·µm). "
                        "residual_rms: log-space RMS fit error (0=perfect)."),
            "groups":  [],
        }}

    good_mask = ~noise_mask & ~np.isnan(results[:, 0])
    meta["localisation"]["groups"] = [
        grp for grp in meta["localisation"].get("groups", [])
        if grp.get("group") != g
    ]
    meta["localisation"]["groups"].append({
        "group":           g,
        "n_spikes":        int(n_spk),
        "n_sites":         int(n_sites),
        "n_localised":     int(n_done),
        "n_failed":        int(n_fail),
        "n_noise":         int(n_noise),
        "probe_file":      probe_file_used,
        "site_positions_um": xy_sites.tolist(),
        "output_file":     os.path.basename(out_path),
        "stats": {
            "x_um":  {
                "mean": float(np.nanmean(results[good_mask, 0])),
                "std":  float(np.nanstd(results[good_mask, 0])),
            },
            "y_um":  {
                "mean": float(np.nanmean(results[good_mask, 1])),
                "std":  float(np.nanstd(results[good_mask, 1])),
            },
            "z_um":  {
                "mean": float(np.nanmean(results[good_mask, 2])),
                "std":  float(np.nanstd(results[good_mask, 2])),
            },
        },
    })
    meta["localisation"]["groups"].sort(key=lambda grp: grp["group"])

    with open(yaml_path, "w") as f:
        yaml.dump(meta, f, default_flow_style=False, sort_keys=False)
    print(f"  Metadata: {yaml_path}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
