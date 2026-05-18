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


def parse_xml(session_path: Path, group: int):
    """Extract nChannelsInGroup, nSamples, peakSampleIndex, samplingRate
    from the session's .xml.  Returns dict with keys nChanGroup,
    nSamples, peakIdx, samplingRate, channelList."""
    xml_path = session_path.with_suffix(".xml")
    if not xml_path.is_file():
        raise FileNotFoundError(f"{xml_path} not found")
    tree = ET.parse(xml_path)
    root = tree.getroot()

    # Sampling rate (acquisitionSystem/samplingRate)
    sr_node = root.find(".//acquisitionSystem/samplingRate")
    sampling_rate = float(sr_node.text) if sr_node is not None else 20000.0

    # Spike detection group params (1-based group index in the XML)
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
    }


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
    """Read .clu.<group>. Auto-detects ASCII (classic Klusters) vs
    binary (some forks) formats.  Returns (nSpikes,) cluster IDs."""
    clu_path = Path(f"{session_path}.clu.{group}")
    if not clu_path.is_file():
        raise FileNotFoundError(f"{clu_path} not found")

    # Try ASCII first: first line is #clusters declared, rest are IDs
    with open(clu_path, "rb") as f:
        head = f.read(1024)
    try:
        # ASCII heuristic: head decodes cleanly and starts with a digit + newline
        text_head = head.decode("ascii")
        first_line = text_head.split("\n", 1)[0]
        int(first_line)  # parse-check
        clu = np.loadtxt(clu_path, dtype=np.int64, skiprows=1)
        return clu
    except (UnicodeDecodeError, ValueError):
        pass

    # Binary fallback: int32 header (#spikes), then int32 cluster IDs
    raw = np.fromfile(clu_path, dtype=np.int32)
    if raw.size > 1 and raw[0] == raw.size - 1:
        return raw[1:].astype(np.int64)
    # Unknown format — just return everything as int64
    return raw.astype(np.int64)


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


def pick_top_clusters(clu: np.ndarray, n_top: int) -> list[int]:
    """Pick the n_top largest clusters, excluding cluster 0 (artifacts)
    and cluster 1 (unsorted) per Klusters convention."""
    ids, counts = np.unique(clu, return_counts=True)
    order = np.argsort(counts)[::-1]
    picked = []
    for i in order:
        cid = int(ids[i])
        if cid <= 1:
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
    args = ap.parse_args()

    session = args.session
    group = args.group
    out = args.output
    out.mkdir(parents=True, exist_ok=True)

    print(f"Reading {session} group {group} ...")
    geom = parse_xml(session, group)
    print(f"  XML: {geom['nChanGroup']} chan × {geom['nSamples']} samples "
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

    top = pick_top_clusters(clu, args.top_clusters)
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
    print("Done.")


if __name__ == "__main__":
    main()
