#!/usr/bin/env python3
"""
cluster_waveform_stats.py — per-cluster mean + std waveform tensors.

For every cluster in a sort, computes:
    mean(t, c) = mean over spikes of waveform[t, c]
    std (t, c) = stdev over spikes of waveform[t, c]

The output is a single NPZ file containing both tensors stacked across all
clusters, plus metadata.  Designed for offline diagnostic inspection of
merge behaviour:

  * Two clusters that should have been merged → their means are nearly
    identical and their std envelopes overlap on every channel.
  * One cluster that should have been split → its std is much larger than
    a clean unit's std on the discriminating channel(s); equivalently the
    mean is a poor representative.
  * Drift-affected clusters → mean is a blurry average of two
    well-separated templates; std spikes only on the channels where the
    source migrated.

INPUTS (positional):
    session                base path of the session (no extension)
    group                  electrode group number (1-based)

OUTPUTS:
    <output>/<session_name>.cluster_waveforms.g<group>.npz
        means        (nClusters, nSamples, nChan) float32
        stds         (nClusters, nSamples, nChan) float32
        clusters     (nClusters,) int32  — cluster IDs (sorted ascending)
        nspikes      (nClusters,) int32  — spike count per cluster
        ptp_mean     (nClusters, nChan) float32 — peak-to-peak of mean
        snr_per_ch   (nClusters, nChan) float32 — ptp_mean / median(std)
        channel_list (nChan,) int32  — session-level hardware IDs
        sampling_rate float32
        peak_sample  int32
        source       str  — '.spkD.N' or '.spk.N' (which file was read)
        session      str  — the input session basename
        group        int32

    <output>/<session_name>.cluster_waveforms.g<group>.summary.txt
        Human-readable summary: per-cluster spike count, mean PTP per
        channel, dominant channel.  Useful for at-a-glance review.

NOTES:
  * Cluster IDs 0 (artefact) and 1 (MUA) are included but flagged in the
    summary — their per-channel statistics aren't biologically meaningful.
  * Spikes are assumed already aligned (extraction does this).  No
    realignment is performed before averaging.
  * Streams cluster-by-cluster — peak memory is one cluster's waveforms,
    not the full session.  Scales to >1000 clusters and >1M spikes.
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


# ─── stats ───────────────────────────────────────────────────────────────


def compute_cluster_stats(spk, clu, dtype=np.float32):
    """Per-cluster mean and std of waveforms.

    spk: memmap or array of shape (nSpikes, nSamples, nChan)
    clu: int32 array of shape (nSpikes,)

    Returns (clusters, nspikes, means, stds) where:
      clusters: sorted unique cluster IDs (nClusters,)
      nspikes:  spike count per cluster (nClusters,)
      means:    (nClusters, nSamples, nChan) float32
      stds:     (nClusters, nSamples, nChan) float32  — population std (ddof=0)
    """
    clusters = np.unique(clu).astype(np.int32)
    n_clusters = len(clusters)
    n_samples, n_chan = spk.shape[1], spk.shape[2]

    nspikes = np.zeros(n_clusters, dtype=np.int32)
    means   = np.zeros((n_clusters, n_samples, n_chan), dtype=dtype)
    stds    = np.zeros((n_clusters, n_samples, n_chan), dtype=dtype)

    for i, cid in enumerate(clusters):
        idx = np.flatnonzero(clu == cid)
        nspikes[i] = len(idx)
        if len(idx) == 0:
            continue
        # Pull the cluster's waveforms in chunks to avoid materialising a
        # huge int16 → float32 array for very large clusters.
        BATCH = 4096
        # Welford's online algorithm: numerically stable, single pass.
        # But for waveforms (int16, bounded) a two-pass numpy is fine and
        # 5× faster — we have RAM for one cluster at a time.
        if len(idx) <= BATCH * 4:
            wf = spk[idx].astype(dtype)
            means[i] = wf.mean(axis=0)
            stds[i]  = wf.std(axis=0)         # population std (ddof=0)
        else:
            # Batched two-pass for very large clusters
            sum_   = np.zeros((n_samples, n_chan), dtype=np.float64)
            sumsq_ = np.zeros((n_samples, n_chan), dtype=np.float64)
            for b0 in range(0, len(idx), BATCH):
                wf = spk[idx[b0:b0 + BATCH]].astype(np.float64)
                sum_   += wf.sum(axis=0)
                sumsq_ += (wf * wf).sum(axis=0)
            n = float(len(idx))
            means[i] = (sum_ / n).astype(dtype)
            var = sumsq_ / n - (sum_ / n) ** 2
            np.clip(var, 0.0, None, out=var)   # guard against -0 from roundoff
            stds[i]  = np.sqrt(var).astype(dtype)

    return clusters, nspikes, means, stds


def ptp_and_snr(means, stds):
    """ptp_mean[i, c] = max(means[i, :, c]) - min(means[i, :, c])
       snr_per_ch[i, c] = ptp_mean[i, c] / median(stds[i, :, c])
       (median std avoids the high-std region around the peak inflating
       the denominator and underselling the SNR)."""
    ptp_mean = means.max(axis=1) - means.min(axis=1)              # (nC, ch)
    med_std  = np.median(stds, axis=1)                            # (nC, ch)
    # avoid div0 on dead channels / empty clusters
    med_std_safe = np.where(med_std > 0, med_std, np.inf)
    snr = ptp_mean / med_std_safe
    return ptp_mean.astype(np.float32), snr.astype(np.float32)


# ─── summary text ────────────────────────────────────────────────────────


def write_summary(out_path, clusters, nspikes, ptp_mean, snr_per_ch,
                  channel_list, sampling_rate, source, session_basename,
                  group):
    n_chan = ptp_mean.shape[1]
    with open(out_path, "w") as f:
        f.write(f"cluster_waveform_stats — summary\n")
        f.write("=" * 72 + "\n")
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
        f.write("\n")

        # Per-cluster lines: id, nspikes, dominant channel + ptp on that
        # channel, second-best channel, SNR on dominant channel
        ch_hdr = "  ".join(f"ch{c:3d}" for c in channel_list)
        f.write(f"{'clu':>5s} {'nspk':>7s} {'domCh':>6s} {'ptpDom':>8s} "
                f"{'snrDom':>7s}  ptp_per_channel\n")
        f.write("-" * 72 + "\n")
        for i, cid in enumerate(clusters):
            if nspikes[i] == 0:
                f.write(f"{int(cid):>5d} {0:>7d}  (no spikes)\n")
                continue
            ch_dom = int(np.argmax(ptp_mean[i]))
            ptp_dom = float(ptp_mean[i, ch_dom])
            snr_dom = float(snr_per_ch[i, ch_dom])
            flag = ""
            if cid == 0:   flag = " [artefact]"
            elif cid == 1: flag = " [MUA]"
            ptp_line = " ".join(f"{p:6.0f}" for p in ptp_mean[i])
            f.write(f"{int(cid):>5d} {int(nspikes[i]):>7d} "
                    f"{ch_dom:>6d} {ptp_dom:>8.0f} {snr_dom:>7.2f}  "
                    f"{ptp_line}{flag}\n")
        f.write("\n")
        f.write("Column legend\n")
        f.write("-" * 40 + "\n")
        f.write("  domCh   : group-local channel index with largest ptp of mean\n")
        f.write("  ptpDom  : peak-to-peak of mean waveform on the dominant channel\n")
        f.write("  snrDom  : ptpDom / median(std) on the dominant channel\n")
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

    # Compute
    print("  computing per-cluster mean and std...")
    clusters, nspikes, means, stds = compute_cluster_stats(spk_view, clu)
    ptp_mean, snr_per_ch = ptp_and_snr(means, stds)
    print(f"  {len(clusters)} clusters processed")
    print(f"  spike-count range: [{int(nspikes.min())}, "
          f"{int(nspikes.max())}]  median {int(np.median(nspikes))}")

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
    })
    np.savez_compressed(
        out_npz,
        means=means, stds=stds,
        clusters=clusters, nspikes=nspikes,
        ptp_mean=ptp_mean, snr_per_ch=snr_per_ch,
        channel_list=channel_list,
        sampling_rate=np.float32(sampling_rate),
        peak_sample=np.int32(peak_sample),
        source=spk_path, session=session_basename,
        group=np.int32(args.group),
        meta=meta,
    )
    npz_size = out_npz.stat().st_size
    print(f"  wrote {out_npz} ({npz_size/1024:.1f} KB)")

    # Summary text
    if not args.no_summary:
        out_txt = args.output / (
            f"{session_basename}.cluster_waveforms.g{args.group}.summary.txt")
        write_summary(out_txt, clusters, nspikes, ptp_mean, snr_per_ch,
                       channel_list, sampling_rate, spk_path,
                       session_basename, args.group)
        print(f"  wrote {out_txt}")

    print("Done.")


if __name__ == "__main__":
    main()
