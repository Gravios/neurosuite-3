#!/usr/bin/env python3
"""
process_driftcorrect.py
========================
Geometric refeaturization of spike waveforms to a reference probe position.

For each spikeDetection group, reads the per-spike waveform file
(``.spkD.N`` preferred, ``.spk.N`` fallback), looks up the per-spike
probe drift from the per-probe binary signal ``SESSION.dat.drift.P``
(produced by ``ndm_estimatedrift``), and writes a corrected waveform
file (``.spkCD.N`` or ``.spkC.N`` depending on input).

The correction
--------------
Let probe offset at time t be ``d(t)`` µm (positive = probe deeper into
tissue, relative to a chosen reference window).  Site k is at fixed
offset ``z_k`` from the probe origin.  When the probe is at offset
``d_i`` for spike i, site k physically sits at depth ``z_k + d_i`` and
records the local field at that depth.

We want the waveform that *would have been recorded by site k if the
probe were at the reference position*.  That is the value of the local
field at depth ``z_k`` at time t — interpolated from the recorded values
at depths ``{z_j + d_i}_j`` with measurements ``{s_j(t)}_j``.

Equivalently, in the reference-grid frame::

    s_k_corrected(t) = interp(z_k − d_i, {z_j}_j, {s_j(t)}_j)

For drifts smaller than one site spacing this is well-conditioned and
sub-µm accurate.  For larger drifts (rare in practice) we clamp to the
boundary site values (``np.interp`` semantics).

This plugin reads NO ``.clu`` file
----------------------------------
The correction is purely geometric.  Whether a spike was labelled
artifact (cluster 0), MUA (cluster 1), or assigned to a real unit
makes no difference: the recorded waveform is the same physical
signal, and so is the corrected waveform.

This decoupling is what makes the iterative-refinement loop work.
A group whose spikes all landed in 0/1 in iteration N can recover
real units in iteration N+1 after refeaturization, because the
refeaturization didn't filter on the (provisional) cluster labels.

The drift signal is absolute, not incremental
---------------------------------------------
Each iteration of the refinement loop writes a new
``SESSION.dat.drift.P`` representing the *cumulative* offset between
the probe and the reference.  ``process_driftcorrect.py`` always
reads the original ``.spk(D)`` and writes ``.spkC(D)`` from scratch,
so successive iterations don't compound interpolation error.

Edge cases
----------
* Source group has only noise/artifact in .clu →
  ``ndm_estimatedrift`` returned None for that group → no
  ``dat.drift.P`` for that probe → group is skipped here with a
  clear message; downstream tooling falls back to ``.spk(D)``.
* Target group has only noise/artifact in .clu, but the probe drift
  signal exists from a sibling shank → refeaturize anyway (this is
  the loop case Gravio asked about).
* Drift signal exists but ``max(|d|) < passthrough_thresh_um`` →
  fast-path: copy unchanged (avoids useless interpolation).
* Spike timestamp out of ``dat.drift.P`` bounds → clamp.
* NaN-depth sites in ``sitePositions_um`` → those sites pass through
  unchanged on output AND are excluded from the interpolant.
* Drift exceeds array span → boundary-clamp; warn if any sample
  exceeds half the array span.
* No probe geometry available (no ``sitePositions_um``, no probeFile,
  default 50 µm spacing also unusable) → skip with info message.

Dependencies:  python3 ≥ 3.10,  pyyaml,  numpy
Copyright (C) 2025 neurosuite-3 contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations
import argparse
import math
import os
import shutil
import sys
import yaml
import numpy as np
from pathlib import Path
from typing import Optional


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--session",                 required=True)
    p.add_argument("--param-file",              required=True)
    p.add_argument("--sampling-rate",           type=float, required=True)
    p.add_argument("--n-channels",              type=int,   required=True)
    p.add_argument("--n-bits",                  type=int,   default=16)
    p.add_argument("--n-groups",                type=int,   required=True)
    p.add_argument("--n-samples-per-group",     default="",
                   help="Comma-separated list of nSamples per group "
                        "(1-based). Falls back to 32 when absent.")
    p.add_argument("--passthrough-thresh-um",   type=float, default=0.25,
                   help="If max(|drift|) over the whole signal is below "
                        "this, a fast-path file copy replaces interpolation.")
    p.add_argument("--batch-size",              type=int,   default=100_000,
                   help="Number of spikes processed per batch (memory cap).")
    p.add_argument("--overwrite",               default="false",
                   help="If true, overwrite existing .spkC(D) outputs.")
    p.add_argument("--probe-library",           default="")
    return p.parse_args()


# ─────────────────────────────────────────────────────────────────────────────
# Probe geometry — copied verbatim from process_estimatedrift.py so the two
# plugins agree on what 'site depth' means.
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


def build_group_probe_map(param: dict) -> dict:
    """Return {1-based group index: (probe_id, shank_index)}."""
    result: dict[int, tuple[int, int]] = {}
    spk_groups  = (param or {}).get("spikeDetection",      {}).get("channelGroups", [])
    anat_groups = (param or {}).get("anatomicalDescription", {}).get("channelGroups", [])
    for i, grp in enumerate(spk_groups):
        gnum = i + 1
        if "probeId" in grp:
            probe_id    = int(grp["probeId"])
            shank_index = int(grp.get("shankIndex", i))
        elif i < len(anat_groups) and "probeId" in anat_groups[i]:
            probe_id    = int(anat_groups[i]["probeId"])
            shank_index = int(anat_groups[i].get("shankIndex", i))
        else:
            probe_id    = 0
            shank_index = i
        result[gnum] = (probe_id, shank_index)
    return result


def build_probe_entry_map(param: dict) -> dict:
    """Return {probe_id: probe-entry dict}."""
    result: dict[int, dict] = {0: {}}
    for entry in (param or {}).get("probes", []):
        pid = int(entry.get("probeId", entry.get("id", 0)))
        result[pid] = entry
    return result


def build_probe_index_map(param: dict) -> dict:
    """Return {probe_id: 1-based position in the YAML probes list}.

    Mirrors the convention used by ``process_estimatedrift.py`` when
    naming ``SESSION.dat.drift.P`` files.  Probe 0 falls back to index 1
    when the probes list is empty.
    """
    result: dict[int, int] = {}
    probes = (param or {}).get("probes", [])
    for i, entry in enumerate(probes):
        pid = int(entry.get("probeId", entry.get("id", i)))
        result[pid] = i + 1
    if not result:
        result[0] = 1
    return result


def get_group_depths(g: int,
                     param: dict,
                     g_probe_map: dict,
                     p_entry_map: dict,
                     probe_cache: dict,
                     lib_paths: list[str]) -> Optional[np.ndarray]:
    """Resolve the per-site depth array for spike group g.

    Priority 1: inline ``sitePositions_um`` on the spike-detection group
    (NaN entries preserved — those sites pass through unchanged).
    Priority 2: probe file via probeId / shankIndex.
    Returns None when neither yields a usable geometry.
    """
    spk_groups = (param or {}).get("spikeDetection", {}).get("channelGroups", [])
    if 0 < g <= len(spk_groups):
        inline = spk_groups[g - 1].get("sitePositions_um")
        if inline:
            depths = np.array(
                [float(xy[1]) if xy is not None else float("nan")
                 for xy in inline], dtype=float)
            if np.any(np.isfinite(depths)):
                return depths

    pid, shk = g_probe_map.get(g, (0, g - 1))
    entry    = p_entry_map.get(pid)
    if entry is None:
        return None
    pf = entry.get("probeFile", "")
    if pf not in probe_cache:
        probe_cache[pf] = load_probe_file(pf, lib_paths)
    probe = probe_cache[pf]
    if probe is None:
        return None
    return site_depths_from_probe(probe, shk)


# ─────────────────────────────────────────────────────────────────────────────
# File I/O
# ─────────────────────────────────────────────────────────────────────────────

def read_res(path: str) -> np.ndarray:
    return np.fromfile(path, dtype="<i8")


def resolve_spk_path(session: str, group_idx: int) -> Optional[tuple[str, bool]]:
    """Return (path, is_stderiv) or None when neither variant exists."""
    spkD = f"{session}.spkD.{group_idx}"
    spk  = f"{session}.spk.{group_idx}"
    if os.path.isfile(spkD):
        return spkD, True
    if os.path.isfile(spk):
        return spk, False
    return None


def output_spk_path(session: str, group_idx: int, is_stderiv: bool) -> str:
    """``.spkC.N`` / ``.spkCD.N`` matching the input variant."""
    suffix = "spkCD" if is_stderiv else "spkC"
    return f"{session}.{suffix}.{group_idx}"


def read_drift_signal(path: str) -> np.ndarray:
    """Read SESSION.dat.drift.P — int16 µm, one sample per recording sample."""
    return np.fromfile(path, dtype="<i2")


# ─────────────────────────────────────────────────────────────────────────────
# Geometric correction
# ─────────────────────────────────────────────────────────────────────────────

def correct_waveform_batch(wf:        np.ndarray,
                            z_ref:     np.ndarray,
                            d_batch:   np.ndarray,
                            ) -> np.ndarray:
    """
    Apply per-spike drift correction to a batch of waveforms.

    Parameters
    ----------
    wf       : (b, n_samp, n_sites) int16 waveforms.  Modified into a
               float32 copy and returned (caller writes back as int16).
    z_ref    : (n_sites,) reference site depths in µm.  NaN entries are
               passthrough (output equals input for those site indices)
               and are excluded from the interpolant.
    d_batch  : (b,) per-spike drift in µm.  Positive = probe was deeper
               than reference at the spike's timestamp.

    Returns
    -------
    out : (b, n_samp, n_sites) float32, ready to be int16-cast and
          written.
    """
    b, n_samp, n_sites = wf.shape
    out = wf.astype(np.float32, copy=True)

    valid_mask = np.isfinite(z_ref)
    n_valid    = int(valid_mask.sum())
    if n_valid < 2:
        # Not enough geometry to interpolate — passthrough.
        return out

    z_v       = z_ref[valid_mask]
    valid_idx = np.where(valid_mask)[0]

    # Sort interpolant grid by depth so np.searchsorted works.
    order      = np.argsort(z_v)
    z_sorted   = z_v[order]
    valid_idx_sorted = valid_idx[order]

    # Vectorise across spikes: z_query has shape (b, n_valid),
    # bracket indices are (b, n_valid).
    z_query = z_v[None, :] - d_batch[:, None]   # (b, n_valid)

    idx_right = np.searchsorted(z_sorted, z_query, side="right")
    idx_right = np.clip(idx_right, 1, n_valid - 1)
    idx_left  = idx_right - 1

    z_l   = z_sorted[idx_left]                  # (b, n_valid)
    z_r   = z_sorted[idx_right]
    denom = z_r - z_l
    safe_denom = np.where(denom == 0.0, 1.0, denom)
    frac  = ((z_query - z_l) / safe_denom).astype(np.float32)   # (b, n_valid)

    # Gather samples.  wf_v: (b, n_samp, n_valid) along the sorted-valid axis.
    wf_v = wf[:, :, valid_idx_sorted].astype(np.float32)

    # take_along_axis lets us index per-spike along the site axis;
    # broadcast (b, n_valid) selectors over the n_samp axis.
    idx_left_  = idx_left[:, None, :]   # (b, 1, n_valid)
    idx_right_ = idx_right[:, None, :]
    wf_left  = np.take_along_axis(wf_v, idx_left_,  axis=2)   # (b, n_samp, n_valid)
    wf_right = np.take_along_axis(wf_v, idx_right_, axis=2)

    interp = wf_left + frac[:, None, :] * (wf_right - wf_left)

    # Boundary clamp: queries below z_sorted[0] use the leftmost site;
    # queries above z_sorted[-1] use the rightmost.
    out_left  = z_query < z_sorted[0]    # (b, n_valid)
    out_right = z_query > z_sorted[-1]
    if out_left.any() or out_right.any():
        # Per-spike, per-target-site mask.  Replace interp[b, :, k] with
        # wf_v[b, :, 0] / wf_v[b, :, -1] respectively.
        wf_bound_left  = wf_v[:, :, 0:1]            # (b, n_samp, 1)
        wf_bound_right = wf_v[:, :, -1:]
        # Broadcast the 2D (b, n_valid) mask over the sample axis.
        if out_left.any():
            mask = out_left[:, None, :]              # (b, 1, n_valid)
            interp = np.where(mask, wf_bound_left,  interp)
        if out_right.any():
            mask = out_right[:, None, :]
            interp = np.where(mask, wf_bound_right, interp)

    # Scatter back into the (n_sites,) layout via valid_idx_sorted —
    # NaN-depth sites in `out` were already passthrough from the copy.
    out[:, :, valid_idx_sorted] = interp
    return out


# ─────────────────────────────────────────────────────────────────────────────
# Per-group driver
# ─────────────────────────────────────────────────────────────────────────────

def process_group(session:               str,
                  group_idx:             int,
                  drift_path:            str,
                  z_ref:                 np.ndarray,
                  n_samp:                int,
                  sampling_rate:         float,
                  n_total_samples:       int,
                  passthrough_thresh_um: float,
                  batch_size:            int,
                  overwrite:             bool,
                  ) -> int:
    """Refeaturize one spike group.  Returns 0 on success / skip, 1 on error."""
    spk_resolved = resolve_spk_path(session, group_idx)
    if spk_resolved is None:
        print(f"  group {group_idx}: no .spk or .spkD — skipping",
              file=sys.stderr)
        return 0
    spk_path, is_stderiv = spk_resolved
    out_path = output_spk_path(session, group_idx, is_stderiv)

    if os.path.isfile(out_path) and not overwrite:
        print(f"  group {group_idx}: {out_path} exists — skipping "
              "(set overwrite=true to refresh)", file=sys.stderr)
        return 0

    # ── Read drift signal ──────────────────────────────────────────────
    if not os.path.isfile(drift_path):
        print(f"  group {group_idx}: drift signal {drift_path} missing — "
              "skipping (no estimate for this probe)", file=sys.stderr)
        return 0
    drift = read_drift_signal(drift_path).astype(np.float32)
    if drift.size == 0:
        print(f"  group {group_idx}: drift signal {drift_path} empty — "
              "skipping", file=sys.stderr)
        return 0

    # Sanity: drift signal length should match recording sample count.
    if n_total_samples > 0 and drift.size != n_total_samples:
        print(f"  group {group_idx}: drift length {drift.size} != "
              f".dat sample count {n_total_samples} — proceeding with clamp",
              file=sys.stderr)

    max_abs = float(np.max(np.abs(drift)))

    # ── Fast path: drift is essentially zero everywhere ────────────────
    if max_abs < passthrough_thresh_um:
        shutil.copyfile(spk_path, out_path)
        print(f"  group {group_idx}: max|drift|={max_abs:.2f} µm < "
              f"{passthrough_thresh_um} µm — passthrough copy "
              f"({os.path.basename(spk_path)} → {os.path.basename(out_path)})",
              file=sys.stderr)
        return 0

    # ── Read .res to align spikes with drift signal ────────────────────
    res_path = f"{session}.res.{group_idx}"
    if not os.path.isfile(res_path):
        print(f"  group {group_idx}: missing {res_path} — cannot align "
              "drift to spike timestamps", file=sys.stderr)
        return 1
    res = read_res(res_path)
    n_spikes = res.size

    # ── Read .spk(D) ───────────────────────────────────────────────────
    n_sites = z_ref.size
    raw     = np.fromfile(spk_path, dtype=np.int16)
    stride  = n_samp * n_sites
    n_spk_in_file = raw.size // stride
    if n_spk_in_file == 0:
        print(f"  group {group_idx}: {spk_path} contains no spikes — "
              "skipping", file=sys.stderr)
        return 0
    if n_spk_in_file != n_spikes:
        # Truncate to the smaller — same convention as
        # process_estimatedrift.py.
        n_use = min(n_spk_in_file, n_spikes)
        print(f"  group {group_idx}: .spk has {n_spk_in_file} spikes, "
              f".res has {n_spikes} — using {n_use}", file=sys.stderr)
        n_spikes = n_use
        res = res[:n_use]
    wf_all = raw[:n_spikes * stride].reshape(n_spikes, n_samp, n_sites)

    # ── Look up per-spike drift, clamp timestamp to drift array bounds ─
    ts = np.clip(res.astype(np.int64), 0, drift.size - 1)
    d_per_spike = drift[ts]   # int16 → float32 already

    # ── Sanity warning: drift exceeds half the array span ─────────────
    z_valid   = z_ref[np.isfinite(z_ref)]
    if z_valid.size >= 2:
        array_span = float(z_valid.max() - z_valid.min())
        large      = np.abs(d_per_spike) > 0.5 * array_span
        if large.any():
            print(f"  group {group_idx}: WARN {large.sum()} spike(s) have "
                  f"|drift| > {0.5*array_span:.0f} µm (half array span) — "
                  "boundary clamp will dominate; consider reviewing "
                  ".dat.drift signal for outliers", file=sys.stderr)

    # ── Process in batches ─────────────────────────────────────────────
    # Write directly to disk in int16 to bound RAM at ~batch * stride * 2 B.
    with open(out_path, "wb") as fp_out:
        for start in range(0, n_spikes, batch_size):
            end       = min(start + batch_size, n_spikes)
            wf_batch  = wf_all[start:end]                       # int16 view
            d_batch   = d_per_spike[start:end]
            corrected = correct_waveform_batch(wf_batch, z_ref, d_batch)
            # Round-to-nearest, clip into int16 range.
            np.clip(np.round(corrected), -32768, 32767,
                    out=corrected).astype("<i2").tofile(fp_out)

    print(f"  group {group_idx}: wrote {out_path}  "
          f"({n_spikes} spikes, max|drift|={max_abs:.2f} µm, "
          f"{'stderiv' if is_stderiv else 'raw'} pipeline)",
          file=sys.stderr)
    return 0


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main() -> int:
    args = parse_args()

    overwrite = str(args.overwrite).lower() not in ("false", "0", "no")

    with open(args.param_file) as f:
        param = yaml.safe_load(f)

    lib_paths = find_probe_library(args.probe_library)
    if (param or {}).get("probeLibraryPath"):
        lib_paths.insert(0, param["probeLibraryPath"])

    g_probe_map     = build_group_probe_map(param)
    p_entry_map     = build_probe_entry_map(param)
    probe_index_map = build_probe_index_map(param)
    probe_cache: dict[str, Optional[dict]] = {}

    # Per-group nSamples list (1-based).
    n_samp_list: list[int] = []
    if args.n_samples_per_group:
        try:
            n_samp_list = [int(x.strip()) for x in
                           args.n_samples_per_group.split(",")]
        except ValueError:
            print("  [warn] --n-samples-per-group parse error; "
                  "using 32 for all groups", file=sys.stderr)

    # Determine recording sample count from .dat (best effort) so we can
    # sanity-check drift signal length.  If .dat is absent we just proceed
    # and clamp timestamps without comparison.
    n_total_samples = 0
    dat_path = f"{args.session}.dat"
    if os.path.isfile(dat_path):
        bytes_per_sample = (args.n_bits // 8) * args.n_channels
        if bytes_per_sample > 0:
            n_total_samples = os.path.getsize(dat_path) // bytes_per_sample

    n_processed = 0
    n_skipped   = 0

    for g in range(1, args.n_groups + 1):
        # Resolve probe → drift file.
        pid, _ = g_probe_map.get(g, (0, g - 1))
        probe_idx = probe_index_map.get(pid, pid + 1)
        drift_path = f"{args.session}.dat.drift.{probe_idx}"

        # Resolve geometry.
        z_ref = get_group_depths(g, param, g_probe_map, p_entry_map,
                                  probe_cache, lib_paths)
        if z_ref is None or z_ref.size < 2:
            print(f"  group {g}: no probe geometry available — skipping",
                  file=sys.stderr)
            n_skipped += 1
            continue

        n_samp = n_samp_list[g - 1] if g <= len(n_samp_list) else 32

        rc = process_group(
            session               = args.session,
            group_idx             = g,
            drift_path            = drift_path,
            z_ref                 = z_ref,
            n_samp                = n_samp,
            sampling_rate         = args.sampling_rate,
            n_total_samples       = n_total_samples,
            passthrough_thresh_um = args.passthrough_thresh_um,
            batch_size            = args.batch_size,
            overwrite             = overwrite,
        )
        if rc != 0:
            return rc
        n_processed += 1

    print(f"Drift correction summary: {n_processed} group(s) processed, "
          f"{n_skipped} skipped", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
