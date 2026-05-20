#!/usr/bin/env python3
"""
cluster_split_collisions.py — separate collision events from each cluster.

A collision is when a second spike (from another unit) lands within the
extraction window of the first.  The mean waveform of an otherwise-clean
cluster is barely affected — collisions are a small fraction — but the
contaminated spikes inflate per-channel std and degrade merge criteria.

DETECTION: for each spike, compute on the cluster's dominant channel
       energy_peak     = Σ wf[t]²  for t ∈ [peak − halfwin, peak + halfwin]
       energy_surround = Σ wf[t]²  for t outside that window
       collision_score = energy_surround / energy_peak

A clean spike has surround dominated by noise → score ≈ 0.1–0.3.  A
collision has a secondary peak in the surround window → score > 0.5,
often > 1.0.

A spike is flagged as 'collision' if its score exceeds
median + k_mad × MAD of the cluster's scores.  Per-cluster MAD scaling
adapts to each cluster's natural variance; isolated clusters with very
tight scores can still flag outliers correctly.

OUTPUTS:
  <session>.clu.<group>.collisions    — new .clu: collision spikes get
                                        new cluster IDs (starting at
                                        max_existing_id + 1)
  <session>.collision_split_log.g<G>.csv  — per-split summary

WORKFLOW:
  1. python3 cluster_split_collisions.py SESSION GROUP --npz <npz>
  2. Inspect new clusters in Klusters.  Each collision cluster should
     contain spikes with visibly extra activity at offset from the peak.
     If a collision cluster looks like a clean second unit, raise --k-mad
     (current threshold was too aggressive).
  3. Regenerate stats + merge:
       python3 cluster_waveform_stats.py SESSION GROUP \\
           --clu-suffix collisions --output cluster_wf_g<G>_clean/
       python3 cluster_merge_recommend.py SESSION GROUP \\
           --npz cluster_wf_g<G>_clean/...
"""

import argparse
import csv
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
try:
    from footprint_drift_diagnostic import parse_session_params, read_clu
except ImportError:
    sys.stderr.write("ERROR: place next to footprint_drift_diagnostic.py\n")
    sys.exit(1)


def memmap_spk(session: Path, group: int, n_chan: int, n_samples: int,
               use_spk: bool = False):
    ext = "spk" if use_spk else "spkD"
    path = Path(f"{session}.{ext}.{group}")
    if not path.is_file():
        raise FileNotFoundError(
            f"{path} not found.  Pass --use-spk for plain .spk files.")
    bytes_per_spike = n_samples * n_chan * 2
    file_bytes = path.stat().st_size
    if file_bytes % bytes_per_spike != 0:
        raise RuntimeError(
            f"{path} size {file_bytes} not multiple of "
            f"nSamples*nChan*2 = {bytes_per_spike}.")
    n_spikes = file_bytes // bytes_per_spike
    return (np.memmap(path, dtype=np.int16, mode="r",
                      shape=(n_spikes, n_samples, n_chan)),
            str(path))


# ─── collision detection ─────────────────────────────────────────────────


def collision_scores_for_cluster(spikes_NTC, mean_TC, dom_ch, peak_sample,
                                  peak_halfwin):
    """Per-spike mean-subtracted residual energy in the SURROUND region
    of the dominant channel.

    For each spike:
        residual_t = spike_t − mean_t                    (per sample, dom channel)
        score = Σ residual_t²  for t outside [peak − halfwin, peak + halfwin]

    A clean spike has residual ≈ noise on every sample → score is just
    (noise_std² × n_surround_samples), tight distribution across the
    cluster.  A collision contributes a secondary spike's energy to the
    surround region → score is an outlier far above median.

    Returns (N,) array of surround residual energies (absolute units²;
    MAD-based thresholding makes the scale irrelevant).
    """
    T = spikes_NTC.shape[1]
    peak_lo = max(0, peak_sample - peak_halfwin)
    peak_hi = min(T, peak_sample + peak_halfwin + 1)
    # Surround mask: True for samples OUTSIDE the peak window
    surround_mask = np.ones(T, dtype=bool)
    surround_mask[peak_lo:peak_hi] = False
    # Residual on dominant channel
    wf_dom = spikes_NTC[:, :, dom_ch].astype(np.float64)
    mean_dom = mean_TC[:, dom_ch].astype(np.float64)
    residual = wf_dom - mean_dom[None, :]
    return np.sum(residual[:, surround_mask] ** 2, axis=1)


def find_collisions(scores, k_mad, score_floor):
    """MAD-based outlier flagging.  A spike is a collision if its
    residual surround energy exceeds median + k_mad × MAD.  The floor
    parameter is now expressed as a MULTIPLIER on the median (not an
    absolute), so it scales with cluster noise level.
    """
    med = float(np.median(scores))
    mad = float(np.median(np.abs(scores - med)))
    # Floor: threshold cannot be less than score_floor × median.  This
    # protects extremely tight clusters where MAD ≈ 0 might otherwise
    # flag the normal noise tail.
    threshold = max(med + k_mad * mad, score_floor * med)
    return scores > threshold, threshold, med, mad


# ─── main ────────────────────────────────────────────────────────────────


def main():
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path)
    ap.add_argument("group", type=int)
    ap.add_argument("--npz", type=Path, required=True,
                    help="cluster_waveforms NPZ for cluster means + dom ch")
    ap.add_argument("--use-spk", action="store_true",
                    help="use .spk (plain) instead of .spkD (default, "
                         "stderiv pipeline output)")
    ap.add_argument("--k-mad", type=float, default=5.0,
                    help="collision threshold: median + k_mad × MAD of "
                         "per-cluster residual surround energies "
                         "(default 5.0; tighter than 3 because residual "
                         "scores have a narrow MAD relative to median)")
    ap.add_argument("--score-floor", type=float, default=2.0,
                    help="multiplier floor: threshold cannot be less "
                         "than score_floor × median (default 2.0).  "
                         "Protects extremely tight clusters where "
                         "MAD ≈ 0 would otherwise flag the noise tail.")
    ap.add_argument("--peak-halfwin", type=int, default=6,
                    help="half-window around peak_sample defining the "
                         "spike body.  Default 6 → body samples "
                         "[peak−6, peak+6] (13 samples for 32-sample "
                         "windows).  Surround = outside, where "
                         "collisions show as elevated residual energy "
                         "after mean subtraction.")
    ap.add_argument("--min-spikes", type=int, default=50,
                    help="skip clusters with fewer spikes (no robust "
                         "MAD estimate possible)")
    ap.add_argument("--min-tail-count", type=int, default=5,
                    help="don't split off if fewer than this many "
                         "spikes flagged as collisions (default 5)")
    ap.add_argument("--min-tail-fraction", type=float, default=0.005,
                    help="don't split off if collision fraction below "
                         "this (default 0.5%%)")
    ap.add_argument("--max-tail-fraction", type=float, default=0.50,
                    help="if collision fraction exceeds this, the "
                         "cluster is suspect — skip splitting and flag "
                         "in log (default 50%%; cluster is likely "
                         "fundamentally contaminated, needs re-sort)")
    ap.add_argument("--output-clu", type=Path, default=None,
                    help="output .clu path (default: "
                         "<session>.clu.<group>.collisions). Ignored "
                         "if --in-place is set.")
    ap.add_argument("--output-log", type=Path, default=None,
                    help="output CSV log path (default: "
                         "<session>.collision_split_log.g<group>.csv)")
    ap.add_argument("--in-place", action="store_true",
                    help="Backup the original .clu.<group> to "
                         "<session>.clu.<group>.preCollisions, then "
                         "overwrite the canonical path with the split "
                         "result.  Downstream tools (waveform_stats, "
                         "merge_recommend) then pick up the cleaned "
                         ".clu without any flag changes.  Refuses if "
                         "a .preCollisions backup already exists.")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    print(f"cluster_split_collisions — {args.session.name} group {args.group}")
    print(f"  thresholds: k_mad={args.k_mad}, score_floor={args.score_floor}, "
          f"peak_halfwin=±{args.peak_halfwin}")

    # Geometry from session yaml
    geom = parse_session_params(args.session, args.group)
    n_chan      = geom["nChanGroup"]
    n_samples   = geom["nSamples"]
    sampling_rate = geom["samplingRate"]

    # NPZ for cluster means + dominant channels
    d = np.load(args.npz, allow_pickle=True)
    means = d["means"]              # (T, C, K_npz)
    ptp_mean = d["ptp_mean"]        # (C, K_npz)
    clusters_npz = d["clusters"]    # (K_npz,)
    peak_sample = int(d["peak_sample"])
    print(f"  NPZ: K={len(clusters_npz)}, peak at sample {peak_sample}")
    # Map cluster_id → npz index for fast lookup
    cid_to_npz = {int(cid): i for i, cid in enumerate(clusters_npz)}
    # Per-cluster dominant channel (peak ptp)
    ch_dom_per_npz = np.argmax(ptp_mean, axis=0)

    # Read .spk + .clu
    spk, spk_path = memmap_spk(args.session, args.group, n_chan, n_samples,
                                use_spk=args.use_spk)
    clu = read_clu(args.session, args.group).astype(np.int32)
    n_spikes = min(len(spk), len(clu))
    if len(spk) != len(clu):
        print(f"  WARNING: spk has {len(spk)} entries, clu has {len(clu)}; "
              f"using {n_spikes}")
        clu = clu[:n_spikes]
    print(f"  loaded {n_spikes} spikes from {spk_path}")

    # Per-cluster collision detection + split
    new_clu = clu.copy()
    next_new_id = int(clu.max()) + 1
    split_log = []
    unique_cids = np.unique(clu).tolist()
    n_processed = 0
    n_total_collisions = 0
    n_skipped_too_few = 0
    n_skipped_no_npz = 0
    n_skipped_tail_too_large = 0

    for cid in unique_cids:
        if cid <= 1:
            continue                 # leave artifact (0) and MUA (1) alone
        mask = np.where(clu == cid)[0]
        n_in_cluster = len(mask)
        if n_in_cluster < args.min_spikes:
            n_skipped_too_few += 1
            continue
        if cid not in cid_to_npz:
            n_skipped_no_npz += 1
            continue
        npz_idx = cid_to_npz[cid]
        dom_ch = int(ch_dom_per_npz[npz_idx])
        # Cluster mean from NPZ: means is (T, C, K_npz)
        mean_TC = means[:, :, npz_idx]

        # Per-spike collision scores (mean-subtracted surround residual energy)
        spikes_NTC = np.asarray(spk[mask])     # materialize to RAM
        scores = collision_scores_for_cluster(
            spikes_NTC, mean_TC, dom_ch, peak_sample, args.peak_halfwin)
        is_collision, threshold, med, mad = find_collisions(
            scores, args.k_mad, args.score_floor)
        n_collisions = int(is_collision.sum())
        tail_frac = n_collisions / n_in_cluster

        # Decide whether to actually split off
        if n_collisions < args.min_tail_count or tail_frac < args.min_tail_fraction:
            continue          # not enough to bother splitting

        if tail_frac > args.max_tail_fraction:
            # Cluster is likely fundamentally bad; flag but don't split.
            split_log.append(dict(
                original_id=int(cid), n_total=n_in_cluster,
                n_core=n_in_cluster, tail_id=-1, n_tail=n_collisions,
                tail_fraction=tail_frac, dom_ch=dom_ch,
                threshold=threshold, score_median=med, score_mad=mad,
                action="SKIPPED_TAIL_TOO_LARGE",
            ))
            n_skipped_tail_too_large += 1
            continue

        # Split: collision spikes get a new cluster ID
        tail_spike_indices = mask[is_collision]
        new_clu[tail_spike_indices] = next_new_id
        split_log.append(dict(
            original_id=int(cid), n_total=n_in_cluster,
            n_core=n_in_cluster - n_collisions, tail_id=next_new_id,
            n_tail=n_collisions, tail_fraction=tail_frac,
            dom_ch=dom_ch, threshold=threshold,
            score_median=med, score_mad=mad,
            action="SPLIT",
        ))
        next_new_id += 1
        n_processed += 1
        n_total_collisions += n_collisions

    # Summary
    n_eligible = sum(1 for c in unique_cids if c > 1)
    print(f"\n  processed {n_eligible} eligible clusters (id > 1)")
    print(f"     {n_processed:>4d} clusters had collisions split off")
    print(f"     {n_total_collisions:>4d} total spikes moved to new "
          f"collision clusters")
    if n_skipped_too_few:
        print(f"     {n_skipped_too_few:>4d} skipped (<{args.min_spikes} spikes)")
    if n_skipped_no_npz:
        print(f"     {n_skipped_no_npz:>4d} skipped (not in NPZ)")
    if n_skipped_tail_too_large:
        print(f"     {n_skipped_tail_too_large:>4d} flagged but NOT split "
              f"(>{args.max_tail_fraction:.0%} tail fraction; "
              f"likely needs full re-sort)")

    # Write outputs
    if not args.dry_run:
        # --in-place: backup original .clu.<group> to .preCollisions, then
        # overwrite canonical path.  Refuses if backup already exists, to
        # avoid silently destroying a previous backup.
        if args.in_place:
            import shutil
            canonical_clu = Path(f"{args.session}.clu.{args.group}")
            backup_clu = Path(f"{args.session}.clu.{args.group}.preCollisions")
            if not canonical_clu.is_file():
                sys.stderr.write(
                    f"ERROR: --in-place requires canonical .clu file "
                    f"at {canonical_clu} (not found)\n")
                sys.exit(1)
            if backup_clu.exists():
                sys.stderr.write(
                    f"ERROR: backup file already exists at {backup_clu}.\n"
                    f"To re-run --in-place, first restore from the existing "
                    f"backup:\n"
                    f"  cp {backup_clu} {canonical_clu}\n"
                    f"  rm {backup_clu}\n"
                    f"Or pass --output-clu explicitly to write to a different "
                    f"path.\n")
                sys.exit(1)
            shutil.copy2(canonical_clu, backup_clu)
            print(f"\n  backed up:  {canonical_clu} → {backup_clu}")
            out_clu = canonical_clu
        else:
            out_clu = args.output_clu or Path(
                f"{args.session}.clu.{args.group}.collisions")

        out_log = args.output_log or Path(
            f"{args.session}.collision_split_log.g{args.group}.csv")

        n_clu_distinct = int(np.unique(new_clu).size)
        with open(out_clu, "wb") as f:
            np.array([n_clu_distinct], dtype=np.int32).tofile(f)
            new_clu.astype(np.int32).tofile(f)
        print(f"  wrote     {out_clu}"
              f"{'   (overwrote canonical)' if args.in_place else ''}")
        print(f"  {n_clu_distinct} distinct cluster IDs (was "
              f"{len(unique_cids)}); +{n_clu_distinct - len(unique_cids)} "
              f"new collision clusters")

        if split_log:
            fieldnames = list(split_log[0].keys())
            with open(out_log, "w", newline="") as f:
                w = csv.DictWriter(f, fieldnames=fieldnames)
                w.writeheader()
                w.writerows(split_log)
            print(f"Wrote {out_log}")
            print(f"  {len(split_log)} split-log entries")

    print("\nDone.")


if __name__ == "__main__":
    main()
