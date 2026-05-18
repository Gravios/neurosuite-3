#!/usr/bin/env python3
"""
footprint_drift_diagnostic.py — stage-2 of the holistic drift model design.

Reads a neurosuite-3 session for one electrode group and visualises the
per-cluster spatial footprint trajectory over time.  The goal is to
answer: do footprint trajectories under probe drift look smooth and
population-coordinated, as the proposed spatial model assumes?

If YES — the model is buildable; proceed to algorithm stages.
If NO  — the model assumption breaks down and we need to rethink.

OUTPUTS (in `--output`):
  cluster_<id>_footprint.png   — per-cluster heatmap: time × channel,
                                 colour = median peak-to-peak amplitude
  cluster_<id>_per_channel.png — per-cluster line plot, one line per
                                 channel, amplitude over time
  population_drift.png         — population-level coordination check:
                                 footprint centre-of-mass shift per chunk,
                                 averaged across top clusters
  footprint_stats.csv          — raw per-cluster per-chunk per-channel
                                 amplitudes for whatever downstream
                                 analysis you want
  summary.txt                  — human-readable run summary

OPTIONAL compare-mode outputs (when --compare <ids> is given):
  compare_footprints.png        — side-by-side normalised footprint bar
                                 chart for the selected clusters (top row
                                 = shape; bottom row = raw amplitude)
  compare_cosine_heatmap.png    — pairwise cosine similarity of normalised
                                 footprints with values overlaid
  compare_trajectory_overlay.png — per-channel trajectory overlay; one
                                 subplot per channel, lines for each
                                 compared cluster
  compare_summary.txt           — verdict and similarity matrix as text

WHAT TO LOOK FOR IN THE OUTPUT:
  - Smooth amplitude trajectories per channel (drift is gradual)
  - Coordinated shifts across clusters in the population plot
    (same direction at the same time = coordinated probe drift)
  - Channel-wise inheritance: as one channel's amplitude drops,
    an adjacent channel's rises (the source is drifting between them)
  - Stationary feature-space variance within a chunk — would manifest
    as similar per-chunk amplitude SD across the recording

WHAT WOULD INVALIDATE THE MODEL:
  - Abrupt, uncorrelated amplitude jumps (would suggest detection
    instability, not spatial drift)
  - Different clusters drifting in different directions at the same
    time (would suggest each unit moves independently — physically
    implausible for a rigid probe)
  - Footprints that change shape (not just position) over time
    (would suggest the unit's biology is changing, not just its
    relative position)
"""

import argparse
import csv
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np

try:
    import matplotlib
    matplotlib.use("Agg")  # no display needed for batch use
    import matplotlib.pyplot as plt
    HAVE_MPL = True
except ImportError:
    HAVE_MPL = False


# ─── readers ─────────────────────────────────────────────────────────────


def parse_session_params(session_path: Path, group: int):
    """Extract nChannelsInGroup, nSamples, peakSampleIndex, samplingRate
    from the session YAML (canonical) or .xml (legacy fallback).
    Returns dict with keys nChanGroup, nSamples, peakIdx, samplingRate,
    channelList.

    YAML schema (see src/libklustersshared/src/klustersshared/
    parameteryamlreader.cpp):
        acquisitionSystem:
          nChannels: <int>            # total channels in .fil
          samplingRate: <float>
        spikeDetection:
          channelGroups:
            - channels: [<int>, ...]  # group-1 channel IDs
              nSamples: <int>
              peakSampleIndex: <int>  # 0-based per YAML convention
              nFeatures: <int>
            - ...                     # group 2, 3, ...

    `group` is 1-based on the CLI (matches Klusters convention)."""
    yaml_path = session_path.with_suffix(".yaml")
    xml_path = session_path.with_suffix(".xml")

    if yaml_path.is_file():
        try:
            import yaml
        except ImportError:
            raise RuntimeError(
                f"{yaml_path} exists but PyYAML not installed.  "
                "pip install pyyaml"
            )
        with open(yaml_path) as f:
            doc = yaml.safe_load(f)

        acq = doc.get("acquisitionSystem", {}) or {}
        sampling_rate = float(acq.get("samplingRate", 20000.0))

        groups = (doc.get("spikeDetection", {}) or {}).get("channelGroups", []) or []
        if group < 1 or group > len(groups):
            raise ValueError(
                f"group {group} out of range (YAML has {len(groups)} "
                f"spikeDetection groups)"
            )
        g = groups[group - 1]
        chans = list(g.get("channels", []) or [])
        n_samples = int(g.get("nSamples", 32))
        peak_idx = int(g.get("peakSampleIndex", 16))

        return {
            "nChanGroup": len(chans),
            "nSamples": n_samples,
            "peakIdx": peak_idx,
            "samplingRate": sampling_rate,
            "channelList": chans,
            "schemaSource": str(yaml_path),
        }

    if xml_path.is_file():
        tree = ET.parse(xml_path)
        root = tree.getroot()
        sr_node = root.find(".//acquisitionSystem/samplingRate")
        sampling_rate = float(sr_node.text) if sr_node is not None else 20000.0
        sd_groups = root.findall(".//spikeDetection/channelGroups/group")
        if group < 1 or group > len(sd_groups):
            raise ValueError(
                f"group {group} out of range (XML has {len(sd_groups)} groups)"
            )
        g = sd_groups[group - 1]
        chans = [int(c.text) for c in g.findall("./channels/channel")]
        n_samples_node = g.find("./nSamples")
        peak_node = g.find("./peakSampleIndex")
        n_samples = int(n_samples_node.text) if n_samples_node is not None else 32
        peak_idx = int(peak_node.text) if peak_node is not None else 16
        return {
            "nChanGroup": len(chans),
            "nSamples": n_samples,
            "peakIdx": peak_idx,
            "samplingRate": sampling_rate,
            "channelList": chans,
            "schemaSource": str(xml_path),
        }

    raise FileNotFoundError(
        f"Neither {yaml_path} nor {xml_path} found — session params unavailable"
    )


# Back-compat alias (callers using the old name still work)
parse_xml = parse_session_params


def read_spkD(session_path: Path, group: int, n_chan: int, n_samples: int) -> np.ndarray:
    """Read .spkD.<group> as (nSpikes, nSamples, nChan) int16.
    Sample-major layout: dst[s * nChan + c] per spike."""
    spk_path = Path(f"{session_path}.spkD.{group}")
    if not spk_path.is_file():
        raise FileNotFoundError(f"{spk_path} not found")
    raw = np.fromfile(spk_path, dtype=np.int16)
    spike_size = n_samples * n_chan
    if raw.size % spike_size != 0:
        raise ValueError(
            f"{spk_path}: {raw.size} int16s not divisible by {spike_size}"
        )
    n_spikes = raw.size // spike_size
    return raw.reshape(n_spikes, n_samples, n_chan)


def read_res(session_path: Path, group: int) -> np.ndarray:
    """Read .res.<group> — raw little-endian int64, no header."""
    res_path = Path(f"{session_path}.res.{group}")
    if not res_path.is_file():
        raise FileNotFoundError(f"{res_path} not found")
    return np.fromfile(res_path, dtype=np.int64)


def read_clu(session_path: Path, group: int) -> np.ndarray:
    """Read .clu.<group>. Binary only — this fork uses no ASCII path.

    Mirrors Data::loadClusters at src/klusters/src/data.cpp:241
    (reader) and Data::saveClusters at data.cpp:3490 (writer):

        int32_t  nClusters     # count of distinct cluster IDs in file;
                               # canonical reader validates 0..65536
        nSpikes × int32_t      # cluster IDs in timestamp order

    Returns (nSpikes,) int64 array.  Caller is responsible for length
    matching with spk/res — this function does not enforce that.
    """
    clu_path = Path(f"{session_path}.clu.{group}")
    if not clu_path.is_file():
        raise FileNotFoundError(f"{clu_path} not found")

    file_bytes = clu_path.stat().st_size
    if file_bytes < 4 or (file_bytes - 4) % 4 != 0:
        raise RuntimeError(
            f"{clu_path}: size {file_bytes} not compatible with int32 "
            f"header + int32 cluster ID array (canonical .clu format)"
        )

    with open(clu_path, "rb") as f:
        nClu = int(np.frombuffer(f.read(4), dtype=np.int32, count=1)[0])

    if not (0 <= nClu <= 65536):
        raise RuntimeError(
            f"{clu_path}: header nClusters={nClu} out of canonical "
            f"range [0, 65536] — file is not a valid binary .clu"
        )

    ids = np.fromfile(clu_path, dtype=np.int32, offset=4)
    return ids.astype(np.int64)


# ─── footprint computation ───────────────────────────────────────────────


def compute_footprints(
    spk: np.ndarray,          # (nSpikes, nSamples, nChan) int16
    res: np.ndarray,          # (nSpikes,) int64
    clu: np.ndarray,          # (nSpikes,) cluster ID
    sampling_rate: float,
    chunk_minutes: float,
    min_spikes_per_chunk: int,
    cluster_ids: list[int],
) -> dict:
    """For each cluster, compute (nChunks, nChan) median peak-to-peak
    amplitude.  Returns {cluster_id: footprint_array}.  Chunks span the
    whole recording at chunk_minutes resolution; chunks with too few
    spikes for a cluster get NaN."""
    n_spikes, n_samples, n_chan = spk.shape
    assert res.shape == (n_spikes,)
    assert clu.shape == (n_spikes,)

    res_sec = res.astype(np.float64) / sampling_rate
    t_end = res_sec.max() if res_sec.size else 0.0
    chunk_sec = chunk_minutes * 60.0
    n_chunks = max(1, int(np.ceil(t_end / chunk_sec)))
    chunk_edges = np.arange(0, (n_chunks + 1) * chunk_sec, chunk_sec)
    chunk_id = np.searchsorted(chunk_edges, res_sec, side="right") - 1
    chunk_id = np.clip(chunk_id, 0, n_chunks - 1)

    out: dict[int, np.ndarray] = {}
    for cid in cluster_ids:
        spike_mask = clu == cid
        if not spike_mask.any():
            continue
        spk_c = spk[spike_mask]      # (nSpikesInCluster, nSamples, nChan)
        chunk_c = chunk_id[spike_mask]

        # ptp per spike per channel: (nSpikesInCluster, nChan)
        ptp_c = spk_c.max(axis=1).astype(np.int32) - spk_c.min(axis=1).astype(np.int32)

        fp = np.full((n_chunks, n_chan), np.nan, dtype=np.float64)
        for k in range(n_chunks):
            mk = chunk_c == k
            if mk.sum() < min_spikes_per_chunk:
                continue
            fp[k, :] = np.median(ptp_c[mk, :], axis=0)
        out[cid] = fp

    return out, chunk_edges, n_chunks


def pick_top_clusters(clu: np.ndarray, n_top: int,
                      skip_ids: tuple = ()) -> list[int]:
    """Pick the n_top largest clusters.

    By default, includes all cluster IDs.  Pass skip_ids=(0,) to exclude
    artefacts, or skip_ids=(0, 1) to also exclude the Klusters MUA pool,
    when you know the data has been sorted past those conventions.
    Default is unfiltered because in a fresh (post-extractspikes,
    pre-cluster) state, every spike sits in cluster 1 and excluding it
    would discard the entire population."""
    ids, counts = np.unique(clu, return_counts=True)
    order = np.argsort(counts)[::-1]
    picked = []
    skip = set(skip_ids)
    for i in order:
        cid = int(ids[i])
        if cid in skip:
            continue
        picked.append(cid)
        if len(picked) >= n_top:
            break
    return picked


# ─── plotting ────────────────────────────────────────────────────────────


def plot_cluster_footprint(
    cid: int,
    fp: np.ndarray,        # (nChunks, nChan)
    chunk_edges: np.ndarray,
    channel_list: list[int],
    out_path: Path,
):
    """Heatmap: rows = chunks, cols = channels, colour = ptp amplitude."""
    if not HAVE_MPL:
        return
    n_chunks, n_chan = fp.shape
    fig, ax = plt.subplots(figsize=(8, max(4, n_chunks * 0.15)))
    im = ax.imshow(
        fp,
        aspect="auto",
        origin="lower",
        cmap="viridis",
        extent=[-0.5, n_chan - 0.5, chunk_edges[0] / 60, chunk_edges[-1] / 60],
    )
    ax.set_xlabel("channel (group-local index)")
    ax.set_ylabel("time (minutes)")
    ax.set_xticks(range(n_chan))
    ax.set_xticklabels([str(c) for c in channel_list], fontsize=8)
    ax.set_title(
        f"Cluster {cid} — footprint over time\n"
        f"(median peak-to-peak per channel per chunk, NaN = too few spikes)"
    )
    plt.colorbar(im, ax=ax, label="median ptp (raw int16 units)")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_cluster_per_channel(
    cid: int,
    fp: np.ndarray,
    chunk_edges: np.ndarray,
    channel_list: list[int],
    out_path: Path,
):
    """Line plot: one line per channel, x = time, y = amplitude.  Same
    data as the heatmap but with crossings between channel lines
    visible — that's the inheritance signature."""
    if not HAVE_MPL:
        return
    n_chunks, n_chan = fp.shape
    t_minutes = (chunk_edges[:-1] + chunk_edges[1:]) / 2 / 60
    fig, ax = plt.subplots(figsize=(10, 5))
    for c in range(n_chan):
        ax.plot(
            t_minutes, fp[:, c],
            marker="o", markersize=3, linewidth=1.0,
            label=f"ch{channel_list[c]}",
        )
    ax.set_xlabel("time (minutes)")
    ax.set_ylabel("median ptp amplitude (raw int16)")
    ax.set_title(
        f"Cluster {cid} — per-channel amplitude trajectory\n"
        f"(crossings between channels indicate spatial drift)"
    )
    ax.legend(ncol=min(8, n_chan), fontsize=8, loc="best")
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_population_drift(
    footprints: dict[int, np.ndarray],
    chunk_edges: np.ndarray,
    out_path: Path,
):
    """Population coordination check.  For each cluster, compute the
    centre-of-mass of its footprint along channels at each chunk; plot
    all clusters' COM trajectories on one axis.  Coordinated drift =
    parallel trajectories.  Independent movement = random walk."""
    if not HAVE_MPL:
        return
    t_minutes = (chunk_edges[:-1] + chunk_edges[1:]) / 2 / 60
    n_chunks = len(t_minutes)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5))

    # Per-cluster COM trajectories
    com_traj = {}
    for cid, fp in footprints.items():
        # COM = Σ(c * a_c) / Σ a_c, per chunk
        com = np.full(n_chunks, np.nan)
        for k in range(n_chunks):
            row = fp[k]
            valid = np.isfinite(row) & (row > 0)
            if not valid.any():
                continue
            chans = np.arange(len(row))[valid]
            amps = row[valid]
            com[k] = np.sum(chans * amps) / np.sum(amps)
        com_traj[cid] = com
        ax1.plot(t_minutes, com, marker=".", alpha=0.7, linewidth=0.9,
                 label=f"cluster {cid}")

    ax1.set_xlabel("time (minutes)")
    ax1.set_ylabel("amplitude centre-of-mass (channel units)")
    ax1.set_title("Per-cluster footprint COM trajectory")
    ax1.legend(ncol=2, fontsize=8)
    ax1.grid(alpha=0.3)

    # Mean-subtracted COM (highlights coordinated drift)
    for cid, com in com_traj.items():
        com_centered = com - np.nanmean(com)
        ax2.plot(t_minutes, com_centered, marker=".", alpha=0.7, linewidth=0.9,
                 label=f"cluster {cid}")
    ax2.axhline(0, color="black", linewidth=0.5)
    ax2.set_xlabel("time (minutes)")
    ax2.set_ylabel("COM − mean(COM) (channel units)")
    ax2.set_title("Mean-subtracted COM\n(parallel trajectories = coordinated drift)")
    ax2.legend(ncol=2, fontsize=8)
    ax2.grid(alpha=0.3)

    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def save_footprint_csv(
    footprints: dict[int, np.ndarray],
    chunk_edges: np.ndarray,
    channel_list: list[int],
    out_path: Path,
):
    """One row per (cluster, chunk, channel) with the median ptp."""
    with open(out_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cluster", "chunk", "t_start_min", "t_end_min",
                    "channel_local", "channel_global", "median_ptp"])
        for cid, fp in footprints.items():
            n_chunks, n_chan = fp.shape
            for k in range(n_chunks):
                for c in range(n_chan):
                    val = fp[k, c]
                    if not np.isfinite(val):
                        continue
                    w.writerow([
                        cid, k,
                        chunk_edges[k] / 60, chunk_edges[k + 1] / 60,
                        c, channel_list[c] if c < len(channel_list) else -1,
                        f"{val:.2f}",
                    ])


# ─── cluster comparison (same-unit fragmentation diagnostic) ─────────────


def compute_normalised_footprint(fp: np.ndarray) -> np.ndarray:
    """Collapse a (nChunks, nChan) footprint to a single nChan vector
    by taking the chunk-median, then normalise to unit L2 norm.

    Robust to (a) chunks where the cluster had no spikes (NaN rows
    ignored) and (b) overall amplitude scaling — the normalised
    footprint encodes the *shape* of the spatial profile, not its
    magnitude.  Two same-unit fragments with different amplitudes
    should yield near-identical normalised footprints."""
    valid_rows = ~np.all(np.isnan(fp), axis=1)
    if not valid_rows.any():
        return np.zeros(fp.shape[1])
    median = np.nanmedian(fp[valid_rows], axis=0)
    median = np.nan_to_num(median, nan=0.0)
    norm = np.linalg.norm(median)
    return median / norm if norm > 0 else median


def cosine_similarity_matrix(vecs: dict) -> tuple:
    """Pairwise cosine similarity between normalised footprint vectors.
    Returns (cluster_ids_sorted, similarity_matrix).
    Clamped to [-1, 1] to absorb floating-point rounding (self-similarity
    can otherwise come back as 1.0 + 2e-16 and trip downstream checks)."""
    ids = sorted(vecs.keys())
    n = len(ids)
    sim = np.zeros((n, n))
    for i, ci in enumerate(ids):
        vi = vecs[ci]
        ni = np.linalg.norm(vi) or 1.0
        for j, cj in enumerate(ids):
            vj = vecs[cj]
            nj = np.linalg.norm(vj) or 1.0
            s = float(np.dot(vi, vj) / (ni * nj))
            sim[i, j] = max(-1.0, min(1.0, s))
    return ids, sim


def plot_compare_footprints(
    cluster_ids: list[int],
    normalised: dict,
    raw_footprints: dict,
    channel_list: list[int],
    out_path: Path,
):
    """Side-by-side bar plot of normalised footprints + raw-amplitude
    bar plot.  Same-unit fragments: normalised bars look identical,
    raw bars scale together."""
    if not HAVE_MPL:
        return
    n_clusters = len(cluster_ids)
    if n_clusters == 0:
        return

    fig, axes = plt.subplots(2, n_clusters, figsize=(3.5 * n_clusters, 7),
                              sharey="row")
    if n_clusters == 1:
        axes = axes[:, None]

    n_chan = len(channel_list)
    chan_pos = np.arange(n_chan)

    for col, cid in enumerate(cluster_ids):
        if cid not in normalised:
            continue
        norm_fp = normalised[cid]
        raw_fp = raw_footprints[cid]
        chunk_median = np.nanmedian(raw_fp, axis=0)
        chunk_median = np.nan_to_num(chunk_median, nan=0.0)

        # Row 0: normalised footprint (shape)
        axes[0, col].bar(chan_pos, norm_fp, color="C0", alpha=0.85)
        axes[0, col].set_xticks(chan_pos)
        axes[0, col].set_xticklabels([str(c) for c in channel_list], fontsize=7)
        axes[0, col].set_title(f"cluster {cid}\nnormalised footprint", fontsize=9)
        axes[0, col].set_xlabel("channel" if col == 0 else "")
        if col == 0:
            axes[0, col].set_ylabel("normalised amplitude")
        axes[0, col].grid(alpha=0.3, axis="y")

        # Row 1: raw amplitude (median across chunks)
        axes[1, col].bar(chan_pos, chunk_median, color="C3", alpha=0.85)
        axes[1, col].set_xticks(chan_pos)
        axes[1, col].set_xticklabels([str(c) for c in channel_list], fontsize=7)
        axes[1, col].set_title(f"cluster {cid}\nraw median ptp", fontsize=9)
        axes[1, col].set_xlabel("channel")
        if col == 0:
            axes[1, col].set_ylabel("median ptp (raw int16)")
        axes[1, col].grid(alpha=0.3, axis="y")

    fig.suptitle(
        "Footprint comparison — same-unit fragments should have "
        "identical normalised bars (top) and proportional raw bars (bottom)",
        fontsize=10,
    )
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_cosine_heatmap(
    cluster_ids: list[int],
    sim: np.ndarray,
    out_path: Path,
):
    """Cosine similarity heatmap with values overlaid in each cell."""
    if not HAVE_MPL:
        return
    n = len(cluster_ids)
    if n == 0:
        return

    fig, ax = plt.subplots(figsize=(1.0 + 0.6 * n, 1.0 + 0.6 * n))
    im = ax.imshow(sim, cmap="RdYlGn", vmin=0.0, vmax=1.0, aspect="equal")
    ax.set_xticks(range(n))
    ax.set_yticks(range(n))
    ax.set_xticklabels([str(c) for c in cluster_ids], fontsize=9)
    ax.set_yticklabels([str(c) for c in cluster_ids], fontsize=9)
    for i in range(n):
        for j in range(n):
            colour = "black" if sim[i, j] > 0.5 else "white"
            ax.text(j, i, f"{sim[i, j]:.3f}", ha="center", va="center",
                    color=colour, fontsize=8)
    ax.set_title("Normalised-footprint cosine similarity\n"
                 "(>0.95 → same spatial source; <0.7 → different units)",
                 fontsize=10)
    plt.colorbar(im, ax=ax, label="cosine similarity")
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def plot_compare_trajectory_overlay(
    cluster_ids: list[int],
    footprints: dict,
    chunk_edges: np.ndarray,
    channel_list: list[int],
    out_path: Path,
):
    """Per-channel trajectory overlay: one subplot per channel, each
    subplot shows all compared clusters' amplitude on that channel
    over time.

    Same-unit fragmentation: trajectories will either (a) be parallel
    (constant scaling between clusters), or (b) be temporally
    segregated (cluster A occupies early chunks, cluster B occupies
    later chunks → strong evidence of drift-driven fragmentation).

    Truly different units: trajectories will be independent and
    non-parallel."""
    if not HAVE_MPL:
        return
    if not cluster_ids:
        return
    n_chan = len(channel_list)
    n_clusters = len(cluster_ids)

    t_minutes = (chunk_edges[:-1] + chunk_edges[1:]) / 2 / 60

    n_cols = min(4, n_chan)
    n_rows = int(np.ceil(n_chan / n_cols))
    fig, axes = plt.subplots(n_rows, n_cols,
                              figsize=(4 * n_cols, 2.6 * n_rows),
                              sharex=True)
    axes = np.atleast_2d(axes)
    if axes.shape[0] == 1 and n_rows > 1:
        axes = axes.T
    axes_flat = axes.ravel()

    cmap = plt.cm.tab10
    for ax_idx, ch in enumerate(range(n_chan)):
        ax = axes_flat[ax_idx]
        for k, cid in enumerate(cluster_ids):
            if cid not in footprints:
                continue
            ch_traj = footprints[cid][:, ch]
            ax.plot(t_minutes, ch_traj,
                    marker=".", markersize=3, alpha=0.8,
                    color=cmap(k % 10),
                    linewidth=1.0,
                    label=f"cl {cid}")
        ax.set_title(f"channel {channel_list[ch]}", fontsize=9)
        ax.grid(alpha=0.3)
        if ax_idx == 0:
            ax.legend(fontsize=7, loc="best", ncol=min(4, n_clusters))
        if ax_idx % n_cols == 0:
            ax.set_ylabel("ptp")

    # Hide unused subplots
    for ax_idx in range(n_chan, len(axes_flat)):
        axes_flat[ax_idx].set_visible(False)

    # Bottom-row x labels
    for ax in axes_flat[-n_cols:]:
        if ax.get_visible():
            ax.set_xlabel("time (minutes)")

    fig.suptitle(
        f"Per-channel trajectory overlay: clusters {cluster_ids}\n"
        "Parallel curves → same source, scale-shifted.  "
        "Temporally segregated → drift-driven fragmentation.  "
        "Independent → different units.",
        fontsize=11,
    )
    fig.tight_layout()
    fig.savefig(out_path, dpi=110)
    plt.close(fig)


def write_compare_summary(
    cluster_ids: list[int],
    sim: np.ndarray,
    cluster_sizes: dict,
    out_path: Path,
):
    """Human-readable compare summary with verdict."""
    n = len(cluster_ids)
    with open(out_path, "w") as f:
        f.write("Cluster comparison — normalised-footprint cosine similarity\n")
        f.write("=" * 70 + "\n")
        f.write(f"\nCompared clusters: {cluster_ids}\n")
        f.write("Cluster sizes (spikes):\n")
        for cid in cluster_ids:
            f.write(f"  {cid:5d}: {cluster_sizes.get(cid, 0):8d}\n")
        f.write("\nCosine similarity matrix:\n")
        header = "       " + " ".join(f"{c:>7d}" for c in cluster_ids) + "\n"
        f.write(header)
        for i, ci in enumerate(cluster_ids):
            row = f"  {ci:5d}: " + " ".join(f"{sim[i, j]:7.4f}"
                                             for j in range(n))
            f.write(row + "\n")

        f.write("\nInterpretation:\n")
        # Off-diagonal stats
        if n > 1:
            off_diag = sim[~np.eye(n, dtype=bool)]
            f.write(f"  off-diagonal similarity: min={off_diag.min():.4f}, "
                    f"max={off_diag.max():.4f}, mean={off_diag.mean():.4f}\n")
            if off_diag.min() > 0.95:
                f.write("  VERDICT: all clusters have near-identical normalised\n"
                        "           footprints.  Strong evidence of same-unit\n"
                        "           fragmentation; merging is appropriate.\n")
            elif off_diag.min() > 0.85:
                f.write("  VERDICT: highly similar footprints; LIKELY same unit\n"
                        "           with amplitude or timing variation.  Inspect\n"
                        "           the trajectory overlay before merging.\n")
            elif off_diag.max() < 0.5:
                f.write("  VERDICT: distinct footprints; clusters appear to be\n"
                        "           biologically different units.\n")
            else:
                f.write("  VERDICT: mixed — some pairs look like same-unit,\n"
                        "           others look distinct.  Inspect each pair\n"
                        "           in the cosine-heatmap and trajectory plots.\n")


# ─── main ────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path,
                    help="session base path (no extension), e.g. "
                    "/data/.../sirotaA-jg-000005-20120312")
    ap.add_argument("group", type=int, help="electrode group (1-based)")
    ap.add_argument("--chunk-minutes", type=float, default=10.0)
    ap.add_argument("--top-clusters", type=int, default=8)
    ap.add_argument("--min-spikes-per-chunk", type=int, default=10)
    ap.add_argument("--output", type=Path, default=Path("footprint_diag"))
    ap.add_argument(
        "--skip-clusters", type=str, default="",
        help="Comma-separated cluster IDs to exclude from top-cluster "
             "selection (e.g. '0' to skip artefacts, '0,1' to also skip "
             "Klusters MUA pool).  Default empty: include all clusters."
    )
    ap.add_argument(
        "--compare", type=str, default="",
        help="Comma-separated cluster IDs to compare in detail "
             "(e.g. '227,228,229,230').  Outputs normalised-footprint "
             "side-by-side, cosine-similarity matrix, and trajectory "
             "overlay.  Use to test whether suspected same-unit "
             "fragments are actually the same unit by spatial footprint."
    )
    args = ap.parse_args()

    session = args.session
    group = args.group
    out = args.output
    out.mkdir(parents=True, exist_ok=True)

    print(f"Reading {session} group {group} ...")
    geom = parse_session_params(session, group)
    schema_basename = Path(geom["schemaSource"]).name
    print(f"  {schema_basename}: {geom['nChanGroup']} chan × {geom['nSamples']} samples "
          f"(peak at idx {geom['peakIdx']}, sr={geom['samplingRate']:.1f} Hz)")
    print(f"  channelList: {geom['channelList']}")

    spk = read_spkD(session, group, geom["nChanGroup"], geom["nSamples"])
    res = read_res(session, group)
    clu = read_clu(session, group)

    if not (len(spk) == len(res) == len(clu)):
        print(f"  WARN: spk={len(spk)}, res={len(res)}, clu={len(clu)} — mismatch",
              file=sys.stderr)
        n = min(len(spk), len(res), len(clu))
        spk, res, clu = spk[:n], res[:n], clu[:n]
        print(f"  truncated to {n} spikes")

    print(f"  {len(spk)} spikes total")

    # Quick sanity report on the .clu read
    unique_ids, id_counts = np.unique(clu, return_counts=True)
    print(f"  .clu: {len(unique_ids)} unique IDs, "
          f"range [{int(unique_ids.min())}, {int(unique_ids.max())}]")
    if unique_ids.max() <= 1:
        print(f"  note: data is unsorted (all spikes in clusters 0/1) — "
              f"diagnostic will treat cluster 1 as the MUA pool and plot "
              f"its population-wide footprint trajectory")

    skip_tuple = tuple(int(x) for x in args.skip_clusters.split(",")
                       if x.strip()) if args.skip_clusters else ()
    top = pick_top_clusters(clu, args.top_clusters, skip_ids=skip_tuple)
    print(f"  top {len(top)} clusters by size: {top}")

    footprints, chunk_edges, n_chunks = compute_footprints(
        spk, res, clu,
        sampling_rate=geom["samplingRate"],
        chunk_minutes=args.chunk_minutes,
        min_spikes_per_chunk=args.min_spikes_per_chunk,
        cluster_ids=top,
    )
    print(f"  {n_chunks} chunks of {args.chunk_minutes} min each "
          f"(recording length ≈ {chunk_edges[-1] / 60:.1f} min)")

    # Plots
    if HAVE_MPL:
        for cid, fp in footprints.items():
            plot_cluster_footprint(
                cid, fp, chunk_edges, geom["channelList"],
                out / f"cluster_{cid:04d}_footprint.png",
            )
            plot_cluster_per_channel(
                cid, fp, chunk_edges, geom["channelList"],
                out / f"cluster_{cid:04d}_per_channel.png",
            )
        plot_population_drift(
            footprints, chunk_edges, out / "population_drift.png",
        )
        print(f"  plots written to {out}/")
    else:
        print("  matplotlib not available — skipping plots", file=sys.stderr)

    # CSV
    save_footprint_csv(
        footprints, chunk_edges, geom["channelList"],
        out / "footprint_stats.csv",
    )

    # Summary
    with open(out / "summary.txt", "w") as f:
        f.write(f"footprint_drift_diagnostic — {session} group {group}\n")
        f.write(f"{'=' * 70}\n")
        f.write(f"recording length : {chunk_edges[-1] / 60:.1f} minutes\n")
        f.write(f"chunks           : {n_chunks} @ {args.chunk_minutes} min\n")
        f.write(f"total spikes     : {len(spk)}\n")
        f.write(f"top clusters     : {top}\n\n")
        f.write("per-cluster sizes:\n")
        for cid in top:
            n = int((clu == cid).sum())
            f.write(f"  cluster {cid:5d}: {n:8d} spikes\n")
        f.write(f"\nchannelList (group-local idx → global ch): "
                f"{geom['channelList']}\n")

    print(f"  CSV: {out / 'footprint_stats.csv'}")
    print(f"  summary: {out / 'summary.txt'}")

    # ── Cluster comparison (same-unit fragmentation diagnostic) ─────────
    if args.compare.strip():
        try:
            compare_ids = [int(x.strip()) for x in args.compare.split(",")
                           if x.strip()]
        except ValueError as e:
            print(f"  ERROR parsing --compare: {e}", file=sys.stderr)
            compare_ids = []

        if compare_ids:
            print(f"\nComparing clusters: {compare_ids}")
            # Compute footprints for these specifically (they may not be in `top`)
            compare_fps, _, _ = compute_footprints(
                spk, res, clu,
                sampling_rate=geom["samplingRate"],
                chunk_minutes=args.chunk_minutes,
                min_spikes_per_chunk=args.min_spikes_per_chunk,
                cluster_ids=compare_ids,
            )

            present = [cid for cid in compare_ids if cid in compare_fps]
            missing = [cid for cid in compare_ids if cid not in compare_fps]
            if missing:
                print(f"  WARN: no spikes for clusters {missing} — skipping",
                      file=sys.stderr)

            if len(present) >= 1:
                normalised = {cid: compute_normalised_footprint(compare_fps[cid])
                              for cid in present}
                ids_sorted, sim = cosine_similarity_matrix(normalised)
                cluster_sizes = {cid: int((clu == cid).sum())
                                 for cid in present}

                if HAVE_MPL:
                    plot_compare_footprints(
                        ids_sorted, normalised, compare_fps,
                        geom["channelList"],
                        out / "compare_footprints.png",
                    )
                    plot_cosine_heatmap(
                        ids_sorted, sim,
                        out / "compare_cosine_heatmap.png",
                    )
                    plot_compare_trajectory_overlay(
                        ids_sorted, compare_fps, chunk_edges,
                        geom["channelList"],
                        out / "compare_trajectory_overlay.png",
                    )
                write_compare_summary(
                    ids_sorted, sim, cluster_sizes,
                    out / "compare_summary.txt",
                )

                # Print the cosine matrix to stdout for immediate feedback
                print("  cosine similarity matrix:")
                hdr = "         " + " ".join(f"{c:>7d}" for c in ids_sorted)
                print(hdr)
                for i, ci in enumerate(ids_sorted):
                    row = f"    {ci:5d}: " + " ".join(f"{sim[i, j]:7.4f}"
                                                       for j in range(len(ids_sorted)))
                    print(row)
                print(f"  compare outputs in {out}/compare_*")

    print("Done.")


if __name__ == "__main__":
    main()
