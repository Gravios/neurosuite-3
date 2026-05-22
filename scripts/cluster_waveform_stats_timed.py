#!/usr/bin/env python3
"""
cluster_waveform_stats_timed.py — per-time-percentile cluster stats.

Companion to cluster_waveform_stats.  By default emits overall stats
only (mirroring cluster_waveform_stats).  When `--time-chunks N` is
passed (N >= 2), it additionally partitions each cluster's spikes into
N equal-population time bins and recomputes ptpDom / snrDom /
minKurtDom per bin, then auto-tags each cluster.

The per-bin trajectory disambiguates two failure modes that look
identical in pooled stats:

  - **Drift**: ptpDom changes monotonically across bins on a stable
    dominant channel; pooled minKurtDom is bimodal (negative) but
    minKurtDom *within each bin* is unimodal.  → fixable by drift
    correction (ndm_estimatedrift + ndm_applydrift).

  - **Mixture**: ptpDom is flat across bins; minKurtDom is uniformly
    negative across all bins.  → not a drift problem; needs DipSplit
    or KNN-split (patch99 GUI, patch100 KKE Phase 2b.5).

  - **Stable**: ptpDom flat; minKurtDom flat and non-negative.  → no
    action needed.

**The time-chunking pass is meaningful only for already-curated .clu
files**, where most clusters are real single units and the question is
"is this remaining suspect-split cluster drift, mixture, or stable?".
On raw KKE output (where every borderline cluster is a mixture
regardless of time), per-bin analysis just confirms "mixture
everywhere" and adds no information beyond pooled minKurtDom.  Run
without `--time-chunks` on uncurated data; add the flag after manual
clustering or after the cluster_merge_iterate.py pipeline has
converged.

Outputs
-------
  <session>.cluster_waveforms_timed.g<N>.summary.txt
  <session>.cluster_waveforms_timed.g<N>.npz

Without `--time-chunks`, the .npz holds the same per-cluster arrays
as cluster_waveform_stats.  With `--time-chunks N`, it adds:
    means_timed   (N, nSamples, nChan, nClusters) float32
    ptp_timed     (N, nChan, nClusters)           float32
    snr_timed     (N, nChan, nClusters)           float32
    minKurt_timed (N, nClusters)                  float32
    t_bin_edges_s (nClusters, N+1)                float32
    n_per_bin     (N, nClusters)                  int32
    drift_score   (nClusters,)                    float32
    mixture_frac  (nClusters,)                    float32
    tag           (nClusters,)                    <U16
"""

from __future__ import annotations
import argparse
import sys
from pathlib import Path

import numpy as np

# Reuse readers from footprint_drift_diagnostic.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from footprint_drift_diagnostic import (  # noqa: E402
    parse_session_params,
    read_spkD,
    read_res,
    read_clu,
)


# ── stat primitives ──────────────────────────────────────────────────────────

def excess_kurtosis(x: np.ndarray, axis: int = 0) -> np.ndarray:
    """Sample excess kurtosis (Fisher), unbiased-ish.  Returns 0 for
    constant input rather than NaN."""
    x = np.asarray(x, dtype=np.float64)
    mu = x.mean(axis=axis, keepdims=True)
    d  = x - mu
    var = (d * d).mean(axis=axis)
    var = np.where(var <= 0, 1.0, var)
    m4  = (d ** 4).mean(axis=axis)
    return (m4 / (var * var)) - 3.0


def cluster_overall_stats(waves: np.ndarray, peak_idx: int):
    """waves: (n, nSamples, nChan) int16.
    Returns dict with mean (nSamples, nChan), std, ptp (nChan,),
    minKurtDom (scalar), domCh, snr (nChan,)."""
    f = waves.astype(np.float32, copy=False)
    mean = f.mean(axis=0)                              # (nSamples, nChan)
    std  = f.std(axis=0)                               # (nSamples, nChan)
    ptp  = mean.max(axis=0) - mean.min(axis=0)         # (nChan,)
    domCh = int(np.argmax(ptp))
    med_std_dom = float(np.median(std[:, domCh])) if std.shape[0] > 0 else 0.0
    snr_dom = float(ptp[domCh]) / med_std_dom if med_std_dom > 0 else 0.0
    snr_per_ch = np.zeros(ptp.shape, dtype=np.float32)
    for c in range(ptp.shape[0]):
        m = float(np.median(std[:, c]))
        snr_per_ch[c] = float(ptp[c]) / m if m > 0 else 0.0
    nSamples = f.shape[1]
    lo = max(0, peak_idx - 3)
    hi = min(nSamples, peak_idx + 4)
    kdom = excess_kurtosis(f[:, lo:hi, domCh], axis=0)   # (≤7,)
    minKurt = float(kdom.min()) if kdom.size > 0 else 0.0
    return {
        "mean":   mean.astype(np.float32),
        "std":    std.astype(np.float32),
        "ptp":    ptp.astype(np.float32),
        "snr":    snr_per_ch,
        "snrDom": snr_dom,
        "domCh":  domCh,
        "minKurtDom": minKurt,
    }


def per_bin_stats(waves: np.ndarray, peak_idx: int, domCh: int,
                  bin_indices: list[np.ndarray]):
    """Compute per-bin ptp/snr/minKurtDom on the fixed dominant channel.
    Returns dict of arrays length nBins."""
    nBins = len(bin_indices)
    nSamples = waves.shape[1]
    nChan    = waves.shape[2]
    means    = np.full((nBins, nSamples, nChan), np.nan, dtype=np.float32)
    ptps     = np.full((nBins, nChan), np.nan, dtype=np.float32)
    snrs     = np.full((nBins, nChan), np.nan, dtype=np.float32)
    minKurts = np.full(nBins, np.nan, dtype=np.float32)
    lo = max(0, peak_idx - 3)
    hi = min(nSamples, peak_idx + 4)
    for b, idx in enumerate(bin_indices):
        if idx.size == 0:
            continue
        f = waves[idx].astype(np.float32, copy=False)
        m = f.mean(axis=0)                        # (nSamples, nChan)
        s = f.std(axis=0)
        means[b] = m
        ptps[b]  = m.max(axis=0) - m.min(axis=0)
        for c in range(nChan):
            med = float(np.median(s[:, c]))
            snrs[b, c] = (float(ptps[b, c]) / med) if med > 0 else 0.0
        # Kurtosis on dom channel only — requires ≥ 30 spikes for stable
        # 4th moment; smaller bins get NaN (will not penalise the cluster).
        if idx.size >= 30:
            k = excess_kurtosis(f[:, lo:hi, domCh], axis=0)
            minKurts[b] = float(k.min())
    return means, ptps, snrs, minKurts


# ── drift / mixture classifier ───────────────────────────────────────────────

def classify(ptp_dom_bins: np.ndarray, minkurt_bins: np.ndarray,
             overall_ptp_dom: float, overall_minkurt: float):
    """ptp_dom_bins, minkurt_bins: 1-D length nBins (NaN if bin empty).
    Returns (drift_score, mixture_frac, tag)."""
    valid_ptp = ptp_dom_bins[np.isfinite(ptp_dom_bins)]
    if valid_ptp.size < 2 or overall_ptp_dom <= 0:
        drift = 0.0
    else:
        # Fractional change first → last (sign-preserved).
        drift = (valid_ptp[-1] - valid_ptp[0]) / overall_ptp_dom
    valid_kurt = minkurt_bins[np.isfinite(minkurt_bins)]
    if valid_kurt.size == 0:
        mixfrac = 0.0
    else:
        mixfrac = float((valid_kurt < -0.5).sum()) / float(valid_kurt.size)

    # Classification thresholds chosen to match the existing
    # min_kurt_dom < -0.5 "suspect-split" cutoff in cluster_waveform_stats.
    if overall_minkurt >= -0.5:
        tag = "stable"
    elif abs(drift) >= 0.30 and mixfrac < 0.5:
        tag = "drift"
    elif abs(drift) < 0.20 and mixfrac >= 0.75:
        tag = "mixture"
    elif abs(drift) >= 0.20 and mixfrac >= 0.5:
        tag = "drift+mix"
    else:
        tag = "ambiguous"
    return float(drift), mixfrac, tag


# ── main ─────────────────────────────────────────────────────────────────────

def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[1],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="The --time-chunks flag is meaningful only on well-curated "
               ".clu files where the residual question is drift vs "
               "remaining mixture.  Do not pass it on raw KKE output.")
    p.add_argument("session", help="session path (without extension)")
    p.add_argument("--group", "-g", type=int, required=True,
                   help="group number (1-based, Klusters convention)")
    p.add_argument("--time-chunks", "-T", type=int, default=0, metavar="N",
                   help="If N >= 2: partition each cluster's spikes into N "
                        "equal-population time bins and auto-tag clusters "
                        "as drift / mixture / drift+mix / stable / ambiguous "
                        "based on the per-bin ptpDom and minKurtDom "
                        "trajectories.  Default 0 (disabled — emit overall "
                        "stats only).  Intended for well-curated cluster "
                        "files; meaningless on raw KKE output.")
    p.add_argument("--output-dir", "-o", default=".",
                   help="output directory (default current)")
    p.add_argument("--min-spikes", type=int, default=8,
                   help="skip clusters with fewer spikes (default 8)")
    args = p.parse_args(argv)

    if 0 < args.time_chunks < 2:
        sys.stderr.write("--time-chunks must be 0 (off) or >= 2\n"); return 2
    do_timed = args.time_chunks >= 2
    nBins    = args.time_chunks if do_timed else 0

    session = Path(args.session)
    grp     = args.group
    outdir  = Path(args.output_dir); outdir.mkdir(parents=True, exist_ok=True)

    # ── load session params ─────────────────────────────────────────────────
    params = parse_session_params(session, grp)
    nChan    = params["nChanGroup"]
    nSamples = params["nSamples"]
    peakIdx  = params["peakIdx"]
    sr       = params["samplingRate"]
    chans    = list(params["channelList"])
    sys.stderr.write(
        f"session: {session.name}, group {grp}: nChan={nChan} "
        f"nSamples={nSamples} peakIdx={peakIdx} sr={sr:g} Hz\n"
        f"channelList={chans}\n")

    # ── load .spkD / .res / .clu ────────────────────────────────────────────
    spk = read_spkD(session, grp, nChan, nSamples)        # (nSpk, nSamples, nChan) int16
    res = read_res (session, grp)                          # (nSpk,) int64
    clu = read_clu (session, grp)                          # (nSpk,) int
    if not (spk.shape[0] == res.shape[0] == clu.shape[0]):
        sys.stderr.write(
            f"length mismatch: spk={spk.shape[0]} res={res.shape[0]} "
            f"clu={clu.shape[0]}\n")
        return 1
    nSpk = spk.shape[0]
    sys.stderr.write(f"loaded {nSpk} spikes\n")

    # ── per-cluster pass ───────────────────────────────────────────────────
    clu_ids = np.unique(clu)
    clu_ids = clu_ids[clu_ids >= 0]
    nClu = clu_ids.size

    nBins = args.time_chunks if do_timed else 0
    overall = []                       # list of dicts
    perbin  = []                       # populated only if do_timed
    keep_ids = []
    for cid in clu_ids:
        idx = np.flatnonzero(clu == cid)
        if idx.size < args.min_spikes:
            continue
        ts = res[idx]
        order = np.argsort(ts, kind="stable")
        idx_sorted = idx[order]
        ts_sorted  = ts[order]
        waves = spk[idx_sorted]                            # (n_c, nSamples, nChan)
        st = cluster_overall_stats(waves, peakIdx)

        if do_timed:
            edges_pos = np.linspace(0, idx_sorted.size, nBins + 1).astype(int)
            bin_idx_local = [np.arange(edges_pos[b], edges_pos[b+1])
                             for b in range(nBins)]
            means_b, ptps_b, snrs_b, mk_b = per_bin_stats(
                waves, peakIdx, st["domCh"], bin_idx_local)
            t_edges = np.array(
                [ts_sorted[edges_pos[b]] / sr if edges_pos[b] < idx_sorted.size
                 else ts_sorted[-1] / sr for b in range(nBins + 1)],
                dtype=np.float32)
            n_per_bin = np.array([len(b) for b in bin_idx_local], dtype=np.int32)
            drift_score, mix_frac, tag = classify(
                ptps_b[:, st["domCh"]], mk_b,
                st["ptp"][st["domCh"]], st["minKurtDom"])
            st["drift_score"] = drift_score
            st["mixture_frac"] = mix_frac
            st["tag"] = tag
            perbin.append((means_b, ptps_b, snrs_b, mk_b, t_edges, n_per_bin))
        else:
            st["drift_score"] = 0.0
            st["mixture_frac"] = 0.0
            st["tag"] = ""

        overall.append(st)
        keep_ids.append(int(cid))

    if not keep_ids:
        sys.stderr.write("no clusters meet min-spikes — nothing to emit\n")
        return 0

    nClu = len(keep_ids)
    sys.stderr.write(f"processed {nClu} clusters\n")

    # ── pack into arrays ───────────────────────────────────────────────────
    nspikes_arr = np.array(
        [(clu == c).sum() for c in keep_ids], dtype=np.int32)
    domCh_arr   = np.array([st["domCh"] for st in overall], dtype=np.int32)
    ptp_arr     = np.stack([st["ptp"] for st in overall], axis=1).astype(np.float32)
    snr_arr     = np.stack([st["snr"] for st in overall], axis=1).astype(np.float32)
    minKurt_arr = np.array([st["minKurtDom"] for st in overall], dtype=np.float32)
    drift_arr   = np.array([st["drift_score"] for st in overall], dtype=np.float32)
    mix_arr     = np.array([st["mixture_frac"] for st in overall], dtype=np.float32)
    tag_arr     = np.array([st["tag"] for st in overall])

    stem = f"{session.name}.cluster_waveforms_timed.g{grp}"
    npz_path = outdir / f"{stem}.npz"

    npz_kwargs = dict(
        clusters=np.array(keep_ids, dtype=np.int32),
        nspikes=nspikes_arr,
        domCh=domCh_arr,
        ptp_overall=ptp_arr,
        snr_overall=snr_arr,
        minKurt_overall=minKurt_arr,
        n_bins=np.int32(nBins),
        peak_sample=np.int32(peakIdx),
        sampling_rate=np.float32(sr),
    )

    if do_timed:
        means_timed   = np.stack([pb[0] for pb in perbin], axis=3).astype(np.float32)
        ptp_timed     = np.stack([pb[1] for pb in perbin], axis=2).astype(np.float32)
        snr_timed     = np.stack([pb[2] for pb in perbin], axis=2).astype(np.float32)
        minKurt_timed = np.stack([pb[3] for pb in perbin], axis=1).astype(np.float32)
        t_bin_edges   = np.stack([pb[4] for pb in perbin], axis=0).astype(np.float32)
        n_per_bin_arr = np.stack([pb[5] for pb in perbin], axis=1).astype(np.int32)
        npz_kwargs.update(
            means_timed=means_timed,
            ptp_timed=ptp_timed,
            snr_timed=snr_timed,
            minKurt_timed=minKurt_timed,
            t_bin_edges_s=t_bin_edges,
            n_per_bin=n_per_bin_arr,
            drift_score=drift_arr,
            mixture_frac=mix_arr,
            tag=tag_arr,
        )

    np.savez(npz_path, **npz_kwargs)

    # ── text summary ───────────────────────────────────────────────────────
    txt_path = outdir / f"{stem}.summary.txt"
    with open(txt_path, "w") as out:
        header = ("cluster_waveform_stats_timed — summary "
                  + (f"(nBins={nBins}, sorted by minKurt_overall ascending)\n"
                     if do_timed
                     else "(time chunking disabled, sorted by minKurt_overall ascending)\n"))
        out.write(header)
        out.write("=" * 110 + "\n")
        out.write(f"session       : {session.name}\n")
        out.write(f"group         : {grp}\n")
        out.write(f"sampling rate : {sr:g} Hz\n")
        out.write(f"n clusters    : {nClu}\n")
        if do_timed:
            out.write(f"time bins     : {nBins} (equal-population per cluster)\n\n")
            from collections import Counter
            cnt = Counter(tag_arr.tolist())
            out.write("Auto-tag distribution:\n")
            for t in ("stable", "drift", "mixture", "drift+mix", "ambiguous"):
                out.write(f"  {t:12s}: {cnt.get(t, 0)} / {nClu}\n")
            out.write("\n")
        else:
            out.write("time bins     : disabled (pass --time-chunks N to enable)\n\n")

        order = np.argsort(minKurt_arr)

        if not do_timed:
            # Overall-only summary — mirrors cluster_waveform_stats format.
            out.write(f"  clu    nspk  domCh   ptpDom  snrDom  minKurtDom\n")
            out.write("-" * 60 + "\n")
            for i in order:
                cid = keep_ids[i]
                n   = int(nspikes_arr[i])
                dc  = int(domCh_arr[i])
                ptp = float(ptp_arr[dc, i])
                snr = float(snr_arr[dc, i])
                mk  = float(minKurt_arr[i])
                out.write(f"  {cid:>3}  {n:>6}  {dc:>5}  {ptp:>7.0f}  "
                          f"{snr:>6.2f}  {mk:>10.3f}\n")
        else:
            out.write("Per-cluster trajectory "
                      "(dominant-channel ptp / snr / minKurt across bins):\n")
            out.write("-" * 110 + "\n")
            for i in order:
                cid = keep_ids[i]
                n   = int(nspikes_arr[i])
                dc  = int(domCh_arr[i])
                ptp = float(ptp_arr[dc, i])
                snr = float(snr_arr[dc, i])
                mk  = float(minKurt_arr[i])
                ds  = float(drift_arr[i])
                mf  = float(mix_arr[i])
                tg  = tag_arr[i]
                out.write(f"\nclu {cid:3d}  nspk={n:>6}  domCh={dc}  "
                          f"ptpDom={ptp:.0f}  snrDom={snr:.2f}  "
                          f"minKurtDom={mk:+.3f}  drift={ds:+.2f}  "
                          f"mixfrac={mf:.2f}  [{tg}]\n")
                out.write("  bin   t_start(s)   t_end(s)    n   ptpDom   "
                          "snrDom   minKurtDom\n")
                for b in range(nBins):
                    t0 = float(t_bin_edges[i, b])
                    t1 = float(t_bin_edges[i, b + 1])
                    nb = int(n_per_bin_arr[b, i])
                    p  = float(ptp_timed[b, dc, i])
                    s  = float(snr_timed[b, dc, i])
                    k  = float(minKurt_timed[b, i])
                    kstr = f"{k:+.3f}" if np.isfinite(k) else "  n/a "
                    out.write(f"  {b:>3}   {t0:>9.0f}   {t1:>8.0f}  {nb:>4}  "
                              f"{p:>7.0f}  {s:>6.2f}  {kstr}\n")

            out.write("\n" + "-" * 110 + "\n")
            out.write("Tag legend:\n")
            out.write("  stable     overall minKurtDom >= -0.5 — pooled stats already pass\n")
            out.write("  drift      pooled minKurtDom < -0.5 BUT ptpDom changes monotonically\n")
            out.write("             across bins on the same dominant channel (|drift| >= 0.30)\n")
            out.write("             AND per-bin minKurtDom is unimodal in most bins.\n")
            out.write("             → candidate for ndm_estimatedrift / ndm_applydrift cleanup.\n")
            out.write("  mixture    ptpDom flat (|drift| < 0.20) AND per-bin minKurtDom\n")
            out.write("             negative in >= 75%% of bins — uniformly bimodal across time.\n")
            out.write("             → candidate for DipSplit or KNN-split (patch99 / patch100).\n")
            out.write("  drift+mix  both signals strong — split AND drift; expect drift\n")
            out.write("             correction to reveal real sub-units, then DipSplit/KNN-split.\n")
            out.write("  ambiguous  neither signal strong enough — manual inspection.\n")

    print(f"wrote {txt_path}")
    print(f"wrote {npz_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
