#!/usr/bin/env python3
"""
cluster_waveform_stats.py — per-cluster mean, std, and kurtosis tensors.

For every cluster in a sort, computes (over spikes, per time-sample and
per channel):

    mean(t, c) = ⟨ wf(t, c) ⟩
    std (t, c) = √⟨ (wf(t, c) − mean)² ⟩          (population, ddof=0)
    kurt(t, c) = ⟨ (wf(t, c) − mean)⁴ ⟩ / std⁴ − 3   (excess kurtosis)

Why kurtosis: it's the diagnostic with discriminating power for merge
quality.

    kurt ≈  0   →   approximately Gaussian — the (mean, std) summary
                    represents the cluster well, no hidden split.
    kurt <  0   →   platykurtic / bimodal — two amplitude populations are
                    being averaged together.  Strongly negative (≲ −0.5) at
                    one (t, c) cell is direct evidence the cluster should
                    be split, with t and c localising the discriminating
                    waveform sample and channel.
    kurt >  0   →   leptokurtic / heavy-tailed — outlier spikes or a small
                    contaminating second unit.

A merge algorithm that doesn't meet your standards should leave a
characteristic signature on this tensor: many surviving clusters with
min(kurt) ≪ 0 on the dominant channel near the peak sample.  If those
exist, the merge criterion is too forgiving along the amplitude direction
and you can quantify exactly how much by ranking clusters by their
minimum kurtosis.

OUTPUTS:
    <output>/<session_name>.cluster_waveforms.g<group>.npz
        means        (nSamples, nChan, nClusters) float32
                       Index as means[:, :, k] for cluster k's template.
        stds         (nSamples, nChan, nClusters) float32
        kurts        (nSamples, nChan, nClusters) float32
                       excess kurtosis (Gaussian = 0).  Set to 0 where
                       std == 0 (constant channel / single-spike cluster).
        clusters     (nClusters,) int32  — cluster IDs, sorted ascending.
        nspikes      (nClusters,) int32  — spike count per cluster.
        ptp_mean     (nChan, nClusters) float32  — peak-to-peak of mean
                       per channel.  ptp_mean[:, k] is cluster k's per-
                       channel amplitude footprint.
        snr_per_ch   (nChan, nClusters) float32  — ptp_mean / median(std).
        min_kurt_dom (nClusters,) float32  — minimum kurtosis on the
                       dominant channel within the [peak−3, peak+3] sample
                       window.  Single-number triage statistic: clusters
                       with min_kurt_dom ≲ −0.5 are prime candidates for
                       missed splits.
        channel_list (nChan,) int32  — session-level hardware IDs.
        sampling_rate float32
        peak_sample  int32
        source       str
        session      str
        group        int32

    <output>/<session_name>.cluster_waveforms.g<group>.summary.txt
        Per-cluster summary table, sorted by min_kurt_dom ascending —
        most-suspect clusters at the top for fast triage.

NOTES:
  * Cluster IDs 0 (artefact) and 1 (MUA) are included but flagged.
  * Spikes are assumed already aligned (extraction did this).  No
    realignment before averaging.
  * Streaming: large clusters use a two-pass algorithm that computes
    central moments directly from (x − μ) deviations — stable for higher
    moments (raw-moment cancellation would lose precision on kurtosis).
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from footprint_drift_diagnostic import (
        parse_session_params, read_res, read_clu,
    )
except ImportError:
    sys.stderr.write(
        "ERROR: place this next to footprint_drift_diagnostic.py "
        "(needed for the YAML + .res + .clu readers).\n")
    sys.exit(1)

try:
    import yaml as _yaml
except ImportError:
    sys.stderr.write("ERROR: PyYAML required (`pip install pyyaml`)\n")
    sys.exit(1)


# ─── probe geometry loader (mirrors libklustersshared/parameteryamlreader_probes) ─


def load_probe_geometry(session_path: Path, group: int):
    """Load per-channel (x, y) coordinates in µm for the given spike group.

    Mirrors libklustersshared/parameteryamlreader_probes.cpp:
      1. Read session YAML.  Group's channels come from
         `spikeDetection.channelGroups[group-1].channels` (1-indexed group
         number — matches Klusters .spk.N/.clu.N/.res.N file naming).
      2. Find the probe owning this group by matching `channelOffset` to
         the group's first channel (probes[].channelOffset is the first
         hardware ID covered by that probe).
      3. Load the referenced .probe file (path resolution tries
         probeLibraryPath, session directory, and the path as-given).
      4. Map each group channel → site_index via channelMap (sequential
         if null) → (x, y) from probeFile.sites.geometry.

    Returns dict with keys:
      x_um, y_um         (C,) float arrays — channel coordinates
      probe_label        str — for diagnostic logging
      probe_file         str — absolute path of the .probe file used
      site_indices       (C,) int — site index per channel within the probe
    or None if geometry can't be resolved (probe section absent or .probe
    file missing) — caller falls back to channel-index ordering with a
    warning.
    """
    yaml_path = Path(f"{session_path}.yaml")
    if not yaml_path.is_file():
        return None
    with open(yaml_path) as f:
        sess = _yaml.safe_load(f)

    # 1. group channels from spikeDetection
    sd = (sess or {}).get("spikeDetection", {}).get("channelGroups", [])
    if not sd or group <= 0 or group > len(sd):
        return None
    grp_entry = sd[group - 1]
    grp_channels = grp_entry.get("channels", [])
    if not grp_channels:
        return None
    # channels may be plain int list (current schema) or list of {id: int} dicts
    if isinstance(grp_channels[0], dict):
        grp_channels = [c["id"] for c in grp_channels]
    grp_channels = [int(c) for c in grp_channels]

    # 2. find the probe entry that owns these channels (largest channelOffset
    # such that offset <= min(grp_channels))
    probes = sess.get("probes", [])
    if not probes:
        return None
    first_ch = min(grp_channels)
    probe_entry = None
    for p in probes:
        off = int(p.get("channelOffset", 0))
        if off <= first_ch and (probe_entry is None
                                 or off > probe_entry.get("channelOffset", 0)):
            probe_entry = p
    if probe_entry is None:
        return None

    # 3. resolve the probe file path
    probe_file = probe_entry.get("probeFile", "")
    if not probe_file:
        return None
    library_path = sess.get("probeLibraryPath", "")
    candidates = []
    if library_path:
        candidates.append(Path(library_path) / probe_file)
    candidates.append(session_path.parent / probe_file)
    candidates.append(Path(probe_file))
    probe_path = next((p for p in candidates if p.is_file()), None)
    if probe_path is None:
        return None

    with open(probe_path) as f:
        probe_doc = _yaml.safe_load(f)
    pf = (probe_doc or {}).get("probeFile", {})
    sites = pf.get("sites", {})
    geometry = sites.get("geometry")            # list of [x, y]
    if not geometry:
        return None

    # 4. map channels to site coords via channelMap
    channel_map_block = pf.get("channelMap", {}) or {}
    channel_map = channel_map_block.get("map")  # None → sequential
    offset = int(probe_entry.get("channelOffset", 0))

    x_um = np.zeros(len(grp_channels), dtype=np.float64)
    y_um = np.zeros(len(grp_channels), dtype=np.float64)
    site_indices = np.zeros(len(grp_channels), dtype=np.int32)
    for i, ch in enumerate(grp_channels):
        sidx = ch - offset
        if channel_map is not None:
            if 0 <= sidx < len(channel_map):
                sidx = int(channel_map[sidx])
            else:
                sidx = -1
        if 0 <= sidx < len(geometry):
            xy = geometry[sidx]
            x_um[i] = float(xy[0])
            y_um[i] = float(xy[1])
            site_indices[i] = sidx
        else:
            x_um[i] = float("nan")
            y_um[i] = float("nan")
            site_indices[i] = -1

    return {
        "x_um": x_um,
        "y_um": y_um,
        "probe_label": str(probe_entry.get("label", "")),
        "probe_file": str(probe_path),
        "site_indices": site_indices,
    }


# ─── waveform readers ────────────────────────────────────────────────────


def memmap_spk(session: Path, group: int, n_chan: int, n_samples: int,
               use_spk: bool):
    """Return a (nSpikes, nSamples, nChan) memmap into the waveform file.

    Format (per neurosuite-3 STANDARDIZATION.md §3):
        int16, no header, sample-major within spike, channel-interleaved
        within each sample.  Layout: spike S → samples 0..nSamples-1, each
        sample → channels 0..nChan-1.

    Path resolution uses string concatenation (NOT Path.with_suffix), to
    match the canonical readers in footprint_drift_diagnostic.py.  Sessions
    can have dots in their basenames; with_suffix would strip part of the
    name.
    """
    ext = "spk" if use_spk else "spkD"
    path = Path(f"{session}.{ext}.{group}")
    if not path.is_file():
        raise FileNotFoundError(
            f"{path} not found.  Pass --use-spk for plain .spk files, "
            f"or drop it for .spkD (default, stderiv pipeline)."
        )
    bytes_per_spike = n_samples * n_chan * 2  # int16
    file_bytes = path.stat().st_size
    if file_bytes % bytes_per_spike != 0:
        raise RuntimeError(
            f"{path} size {file_bytes} is not a multiple of "
            f"nSamples*nChan*2 = {bytes_per_spike}; check geometry."
        )
    n_spikes = file_bytes // bytes_per_spike
    mm = np.memmap(path, dtype=np.int16, mode="r",
                   shape=(n_spikes, n_samples, n_chan))
    return mm, str(path)


# ─── per-cluster time statistics from .res.N ─────────────────────────────


def compute_time_stats(clu, res, clusters, sampling_rate):
    """Per-cluster spike-time statistics.

    Each cluster typically occupies a single temporal span in the
    recording (CEM with chunked structure creates chunk-local clusters).
    For merge diagnostics: two clusters that are the SAME unit at
    different drift positions should be TEMPORALLY ADJACENT (low
    temporal overlap), while two clusters that are concurrent must
    represent different units.

    Returns dict with keys (all (nClusters,) arrays):
      t_mean_s        mean spike time, seconds from start
      t_std_s         std of spike times, seconds (≈ half-span)
      t_min_s, t_max_s  min/max spike time, seconds
      t_range_s       max - min
      t_chunk_density spikes per second within [t_min, t_max] — for
                      sparse-vs-dense diagnostics
    """
    n = min(len(clu), len(res))
    clu = clu[:n].astype(np.int64); res = res[:n].astype(np.int64)
    K = len(clusters)
    sr = float(sampling_rate)
    t_mean = np.zeros(K, dtype=np.float64)
    t_std  = np.zeros(K, dtype=np.float64)
    t_min  = np.zeros(K, dtype=np.float64)
    t_max  = np.zeros(K, dtype=np.float64)
    t_dens = np.zeros(K, dtype=np.float64)
    for i, cid in enumerate(clusters):
        mask = (clu == cid)
        if not mask.any():
            continue
        r = res[mask].astype(np.float64) / sr
        t_mean[i] = float(r.mean())
        t_std[i]  = float(r.std())
        t_min[i]  = float(r.min())
        t_max[i]  = float(r.max())
        span = max(t_max[i] - t_min[i], 1e-6)
        t_dens[i] = float(mask.sum()) / span
    return {
        "t_mean_s":  t_mean.astype(np.float32),
        "t_std_s":   t_std.astype(np.float32),
        "t_min_s":   t_min.astype(np.float32),
        "t_max_s":   t_max.astype(np.float32),
        "t_range_s": (t_max - t_min).astype(np.float32),
        "t_chunk_density": t_dens.astype(np.float32),
    }


# ─── stats ───────────────────────────────────────────────────────────────


def compute_cluster_stats(spk, clu, dtype=np.float32):
    """Per-cluster mean, std, and excess kurtosis of waveforms.

    spk: memmap or array of shape (nSpikes, nSamples, nChan)
    clu: int array of shape (nSpikes,)

    Returns (clusters, nspikes, means, stds, kurts) where:
      clusters: sorted unique cluster IDs (nClusters,)
      nspikes:  spike count per cluster (nClusters,)
      means:    (nClusters, nSamples, nChan) float32
      stds:     (nClusters, nSamples, nChan) float32  — population (ddof=0)
      kurts:    (nClusters, nSamples, nChan) float32  — excess kurtosis
                (E[(x-μ)⁴] / σ⁴ − 3); 0 where σ==0.

    Returns cluster axis FIRST internally; the caller is responsible for
    transposing to (T, C, K) at save time if desired.

    Higher-moment numerical strategy:
      * Small clusters (≤ 4·BATCH spikes): materialise the (nSpikes,
        nSamples, nChan) array, compute everything from (x − μ) deviations
        in one shot.  Exact within float32.
      * Large clusters: two-pass batched algorithm.  Pass 1 streams
        sum(x); Pass 2 streams sum((x − μ)²) and sum((x − μ)⁴) using the
        Pass-1 mean.  Avoids raw-moment cancellation that destroys
        precision in any "Σx⁴ − 4μ·Σx³ + ..." formulation.
    """
    clusters = np.unique(clu).astype(np.int32)
    n_clusters = len(clusters)
    n_samples, n_chan = spk.shape[1], spk.shape[2]

    nspikes = np.zeros(n_clusters, dtype=np.int32)
    means   = np.zeros((n_clusters, n_samples, n_chan), dtype=dtype)
    stds    = np.zeros((n_clusters, n_samples, n_chan), dtype=dtype)
    kurts   = np.zeros((n_clusters, n_samples, n_chan), dtype=dtype)

    BATCH = 4096
    SMALL_THRESHOLD = BATCH * 4

    for i, cid in enumerate(clusters):
        idx = np.flatnonzero(clu == cid)
        nspikes[i] = len(idx)
        if len(idx) == 0:
            continue

        if len(idx) <= SMALL_THRESHOLD:
            # Fast path: full materialisation
            wf = spk[idx].astype(dtype)
            mu = wf.mean(axis=0)
            dev = wf - mu                     # (n, nSamples, nChan)
            var = (dev * dev).mean(axis=0)
            m4  = (dev * dev * dev * dev).mean(axis=0)
            means[i] = mu
            # var → std with non-neg guard
            np.clip(var, 0.0, None, out=var)
            std = np.sqrt(var)
            stds[i] = std
            # kurt = m4/var² - 3; safe where var==0 → kurt=0
            var2 = var * var
            safe = var2 > 0
            kurt = np.zeros_like(var)
            kurt[safe] = m4[safe] / var2[safe] - 3.0
            kurts[i] = kurt
        else:
            # Two-pass batched path for large clusters
            # Pass 1: mean
            sum_ = np.zeros((n_samples, n_chan), dtype=np.float64)
            for b0 in range(0, len(idx), BATCH):
                wf = spk[idx[b0:b0 + BATCH]].astype(np.float64)
                sum_ += wf.sum(axis=0)
            n = float(len(idx))
            mu64 = sum_ / n

            # Pass 2: central second + fourth moments
            sum_d2 = np.zeros((n_samples, n_chan), dtype=np.float64)
            sum_d4 = np.zeros((n_samples, n_chan), dtype=np.float64)
            for b0 in range(0, len(idx), BATCH):
                wf = spk[idx[b0:b0 + BATCH]].astype(np.float64)
                d = wf - mu64
                d2 = d * d
                sum_d2 += d2.sum(axis=0)
                sum_d4 += (d2 * d2).sum(axis=0)

            var64 = sum_d2 / n
            np.clip(var64, 0.0, None, out=var64)
            m4_64 = sum_d4 / n
            var2_64 = var64 * var64
            safe = var2_64 > 0
            kurt64 = np.zeros_like(var64)
            kurt64[safe] = m4_64[safe] / var2_64[safe] - 3.0

            means[i] = mu64.astype(dtype)
            stds[i]  = np.sqrt(var64).astype(dtype)
            kurts[i] = kurt64.astype(dtype)

    return clusters, nspikes, means, stds, kurts


def ptp_and_snr(means, stds):
    """ptp_mean[i, c] = max(means[i, :, c]) - min(means[i, :, c])
       snr_per_ch[i, c] = ptp_mean[i, c] / median(stds[i, :, c])
       (median std avoids the high-std region around the peak inflating
       the denominator and underselling the SNR)."""
    ptp_mean = means.max(axis=1) - means.min(axis=1)              # (nC, ch)
    med_std  = np.median(stds, axis=1)                            # (nC, ch)
    med_std_safe = np.where(med_std > 0, med_std, np.inf)
    snr = ptp_mean / med_std_safe
    return ptp_mean.astype(np.float32), snr.astype(np.float32)


def min_kurt_on_dominant(kurts, ptp_mean, peak_sample, half_window=3):
    """Per-cluster: min(kurtosis) on the dominant channel within
    [peak−half_window, peak+half_window] samples.  Single-number triage
    statistic: strongly negative ≈ bimodal split candidate.

    kurts:    (nClusters, nSamples, nChan)
    ptp_mean: (nClusters, nChan)
    Returns:  (nClusters,) float32
    """
    n_clusters, n_samples, _ = kurts.shape
    t0 = max(0, peak_sample - half_window)
    t1 = min(n_samples, peak_sample + half_window + 1)
    out = np.zeros(n_clusters, dtype=np.float32)
    for i in range(n_clusters):
        ch_dom = int(np.argmax(ptp_mean[i]))
        window = kurts[i, t0:t1, ch_dom]
        out[i] = float(window.min()) if window.size else 0.0
    return out


# ─── summary text ────────────────────────────────────────────────────────


def write_summary(out_path, clusters, nspikes, ptp_mean, snr_per_ch,
                  min_kurt_dom, channel_list, sampling_rate, source,
                  session_basename, group):
    """Per-cluster table sorted by min_kurt_dom ascending — most-suspect
    (most-negative) clusters at the top for fast triage."""
    n_chan = ptp_mean.shape[1]
    # Sort order: most negative min_kurt first; tie-break by larger nspikes
    order = np.lexsort((-nspikes, min_kurt_dom))

    with open(out_path, "w") as f:
        f.write("cluster_waveform_stats — summary "
                "(sorted by min_kurt_dom ascending)\n")
        f.write("=" * 88 + "\n")
        f.write(f"session       : {session_basename}\n")
        f.write(f"group         : {group}\n")
        f.write(f"source        : {source}\n")
        f.write(f"sampling rate : {sampling_rate:.1f} Hz\n")
        f.write(f"n clusters    : {len(clusters)}\n")
        f.write(f"n channels    : {n_chan}\n")
        if nspikes.size:
            f.write(f"spikes        : total {int(nspikes.sum())}  "
                    f"min/median/max per cluster {int(nspikes.min())}/"
                    f"{int(np.median(nspikes))}/{int(nspikes.max())}\n")

        # Suspect-split summary at top
        suspects = int(np.sum(min_kurt_dom < -0.5))
        strong   = int(np.sum(min_kurt_dom < -1.0))
        f.write(f"\nMerge-quality triage:\n")
        f.write(f"  min_kurt_dom < −0.5  (suspect splits)        : "
                f"{suspects} / {len(clusters)}\n")
        f.write(f"  min_kurt_dom < −1.0  (strong split evidence) : "
                f"{strong} / {len(clusters)}\n")
        f.write("\n")

        f.write(f"{'clu':>5s} {'nspk':>7s} {'domCh':>6s} {'ptpDom':>8s} "
                f"{'snrDom':>7s} {'minKurtDom':>11s}  ptp_per_channel\n")
        f.write("-" * 88 + "\n")
        for ix in order:
            cid = int(clusters[ix])
            if nspikes[ix] == 0:
                f.write(f"{cid:>5d} {0:>7d}  (no spikes)\n")
                continue
            ch_dom = int(np.argmax(ptp_mean[ix]))
            ptp_dom = float(ptp_mean[ix, ch_dom])
            snr_dom = float(snr_per_ch[ix, ch_dom])
            mk = float(min_kurt_dom[ix])
            flag = ""
            if cid == 0:   flag = " [artefact]"
            elif cid == 1: flag = " [MUA]"
            if mk < -1.0:  flag += " ⚠ strong-split"
            elif mk < -0.5: flag += " ⚠ suspect-split"
            ptp_line = " ".join(f"{p:6.0f}" for p in ptp_mean[ix])
            f.write(f"{cid:>5d} {int(nspikes[ix]):>7d} "
                    f"{ch_dom:>6d} {ptp_dom:>8.0f} {snr_dom:>7.2f} "
                    f"{mk:>11.3f}  {ptp_line}{flag}\n")
        f.write("\n")
        f.write("Column legend\n")
        f.write("-" * 40 + "\n")
        f.write("  domCh        : group-local channel index with largest ptp of mean\n")
        f.write("  ptpDom       : peak-to-peak of mean waveform on dominant channel\n")
        f.write("  snrDom       : ptpDom / median(std) on dominant channel\n")
        f.write("  minKurtDom   : min excess kurtosis at samples [peak−3..peak+3]\n")
        f.write("                 on dominant channel.  Bimodal → very negative.\n")
        f.write("  ptp_per_channel : ptp of mean on each group-local channel\n")


# ─── main ────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path, help="session base path")
    ap.add_argument("group", type=int, help="electrode group (1-based)")
    ap.add_argument("--output", type=Path, default=Path("."),
                    help="output directory (default: cwd)")
    ap.add_argument("--use-spk", action="store_true",
                    help="read .spk (default reads .spkD)")
    ap.add_argument("--no-summary", action="store_true",
                    help="skip the human-readable .summary.txt")
    args = ap.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    session_basename = args.session.name

    print(f"cluster_waveform_stats — {session_basename} group {args.group}")

    # Geometry
    geom = parse_session_params(args.session, args.group)
    n_chan = geom["nChanGroup"]
    n_samples = geom["nSamples"]
    sampling_rate = geom["samplingRate"]
    channel_list = np.asarray(geom["channelList"], dtype=np.int32)
    peak_sample = int(geom.get("peakIdx", n_samples // 2))
    print(f"  geometry: {n_chan} chan × {n_samples} samples, "
          f"sr={sampling_rate:.1f} Hz, peak at sample {peak_sample}")

    # Memmap waveforms
    spk, spk_path = memmap_spk(args.session, args.group,
                                n_chan, n_samples, args.use_spk)
    print(f"  waveforms: {len(spk):,} spikes from {spk_path}")

    # Clusters
    clu = read_clu(args.session, args.group)
    n = min(len(spk), len(clu))
    if len(spk) != len(clu):
        print(f"  WARNING: spk ({len(spk)}) and clu ({len(clu)}) "
              f"length mismatch, truncating to {n}")
    spk_view = spk[:n]
    clu = clu[:n]
    print(f"  cluster ids: {np.unique(clu).size} unique, "
          f"range [{int(clu.min())}, {int(clu.max())}]")

    # Probe geometry from YAML → .probe (libklustersshared schema)
    geometry = load_probe_geometry(args.session, args.group)
    if geometry is not None:
        x_um = geometry["x_um"]; y_um = geometry["y_um"]
        n_with_coords = int(np.sum(~np.isnan(y_um)))
        y_span = float(np.nanmax(y_um) - np.nanmin(y_um))
        # estimate uniform-spacing if applicable
        y_sorted = np.sort(y_um[~np.isnan(y_um)])
        diffs = np.diff(y_sorted) if len(y_sorted) >= 2 else np.array([0.0])
        print(f"  geometry: {geometry['probe_label'] or 'probe'} "
              f"({n_with_coords}/{n_chan} channels mapped, "
              f"y span {y_span:.0f} µm, median Δy {float(np.median(diffs)):.1f} µm)")
        print(f"            probe file: {geometry['probe_file']}")
    else:
        x_um = np.full(n_chan, np.nan, dtype=np.float64)
        y_um = np.full(n_chan, np.nan, dtype=np.float64)
        print(f"  geometry: NOT FOUND (no probes section, or .probe file "
              f"missing).  NPZ will record NaN coordinates; downstream "
              f"tools must fall back to channel-index ordering.")

    # Read .res.N for per-cluster temporal stats — used by merge
    # recommender for drift comparison (same-unit-under-drift pairs
    # should be TEMPORALLY ADJACENT, concurrent pairs OVERLAPPING).
    try:
        res = read_res(args.session, args.group)
        if len(res) < n:
            print(f"  WARNING: .res ({len(res)}) shorter than spk/clu ({n}); "
                  f"using min")
        n_res = min(n, len(res))
    except FileNotFoundError:
        res = None
        print(f"  .res file not found — skipping per-cluster time stats")

    # Compute waveform stats
    print("  computing per-cluster mean, std, and kurtosis...")
    clusters, nspikes, means, stds, kurts = compute_cluster_stats(spk_view, clu)
    ptp_mean, snr_per_ch = ptp_and_snr(means, stds)
    min_kurt_dom = min_kurt_on_dominant(kurts, ptp_mean, peak_sample,
                                         half_window=3)

    # Empirical per-cluster peak sample: each cluster's actual trough on
    # its dominant channel.  Replaces the global YAML peak_sample for
    # downstream metric computation (CV at peak, α-windows, collision
    # surround mask), which need to be evaluated AT the spike body of
    # each cluster — not at a single sample dictated by config.  Klusters'
    # batch realign optimizes PC-space variance, not trough position, so
    # trough samples naturally distribute across ±2 samples around the
    # global value; per-cluster anchoring handles this correctly.
    K_total, _, _ = means.shape          # means is (K, T, C) here
    dom_ch_per_cluster = np.argmax(ptp_mean, axis=1)                  # (K,)
    peak_sample_per_cluster = np.zeros(K_total, dtype=np.int32)
    for k in range(K_total):
        peak_sample_per_cluster[k] = int(
            np.argmin(means[k, :, dom_ch_per_cluster[k]]))
    print(f"  {len(clusters)} clusters processed")
    print(f"  spike-count range: [{int(nspikes.min())}, "
          f"{int(nspikes.max())}]  median {int(np.median(nspikes))}")
    n_suspect = int(np.sum(min_kurt_dom < -0.5))
    n_strong  = int(np.sum(min_kurt_dom < -1.0))
    print(f"  triage: {n_suspect}/{len(clusters)} clusters with "
          f"min_kurt_dom < −0.5 (suspect split); "
          f"{n_strong} with < −1.0 (strong)")
    # Per-cluster trough distribution: how much do empirical peaks drift
    # from the YAML peak_sample?  A tight distribution means alignment
    # is uniform; spread means tools should use per-cluster peak.
    p_med = int(np.median(peak_sample_per_cluster))
    p_off = int(np.sum(peak_sample_per_cluster != peak_sample))
    print(f"  per-cluster trough: median sample {p_med}, "
          f"{p_off}/{K_total} clusters offset from YAML peak={peak_sample}")

    # Per-cluster time stats
    time_stats = None
    if res is not None:
        time_stats = compute_time_stats(clu[:n_res], res[:n_res], clusters,
                                         sampling_rate)
        # Diagnostic: how time-localized are the clusters?
        ranges = time_stats["t_range_s"]
        rec_span = max(time_stats["t_max_s"].max() -
                        time_stats["t_min_s"].min(), 1.0)
        compact = float(np.median(ranges) / rec_span)
        print(f"  time stats: recording span {rec_span:.0f}s; "
              f"per-cluster median range {np.median(ranges):.0f}s "
              f"({compact:.1%} of session — small = chunk-localised)")

    # Transpose to user's preferred layout for saving: cluster is the
    # trailing axis, so means[:, :, k] is cluster k's full (T,C) template
    # and ptp_mean[:, k] is its per-channel amplitude footprint.
    means_save      = np.ascontiguousarray(means.transpose(1, 2, 0))   # (T,C,K)
    stds_save       = np.ascontiguousarray(stds .transpose(1, 2, 0))
    kurts_save      = np.ascontiguousarray(kurts.transpose(1, 2, 0))
    ptp_mean_save   = np.ascontiguousarray(ptp_mean.T)                 # (C,K)
    snr_per_ch_save = np.ascontiguousarray(snr_per_ch.T)

    # Save NPZ
    out_npz = args.output / (
        f"{session_basename}.cluster_waveforms.g{args.group}.npz")
    meta = json.dumps({
        "session": session_basename,
        "group": int(args.group),
        "source": spk_path,
        "n_chan": int(n_chan),
        "n_samples": int(n_samples),
        "sampling_rate": float(sampling_rate),
        "peak_sample": int(peak_sample),
        "channel_list": channel_list.tolist(),
        "probe_label": geometry["probe_label"] if geometry else "",
        "probe_file":  geometry["probe_file"]  if geometry else "",
        "layout": "means/stds/kurts: (nSamples, nChan, nClusters); "
                  "ptp_mean/snr_per_ch: (nChan, nClusters); "
                  "min_kurt_dom: (nClusters,); "
                  "peak_sample_per_cluster: (nClusters,) int32; "
                  "x_um/y_um: (nChan,) µm; "
                  "t_*_s: (nClusters,) seconds",
        "min_kurt_window": "[peak_sample - 3, peak_sample + 3]",
    })
    payload = dict(
        means=means_save, stds=stds_save, kurts=kurts_save,
        clusters=clusters, nspikes=nspikes,
        ptp_mean=ptp_mean_save, snr_per_ch=snr_per_ch_save,
        min_kurt_dom=min_kurt_dom,
        channel_list=channel_list,
        x_um=x_um.astype(np.float32),
        y_um=y_um.astype(np.float32),
        sampling_rate=np.float32(sampling_rate),
        peak_sample=np.int32(peak_sample),
        peak_sample_per_cluster=peak_sample_per_cluster,
        source=spk_path, session=session_basename,
        group=np.int32(args.group),
        meta=meta,
    )
    if time_stats is not None:
        payload.update(time_stats)
    np.savez_compressed(out_npz, **payload)
    npz_size = out_npz.stat().st_size
    print(f"  wrote {out_npz} ({npz_size/1024:.1f} KB)")

    # Summary text — uses the (K, ...) internal layout, so we pass the
    # non-transposed ptp_mean / snr_per_ch arrays.
    if not args.no_summary:
        out_txt = args.output / (
            f"{session_basename}.cluster_waveforms.g{args.group}.summary.txt")
        write_summary(out_txt, clusters, nspikes, ptp_mean, snr_per_ch,
                       min_kurt_dom, channel_list, sampling_rate, spk_path,
                       session_basename, args.group)
        print(f"  wrote {out_txt}")

    print("Done.")


if __name__ == "__main__":
    main()
