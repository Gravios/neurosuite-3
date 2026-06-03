#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════
#  extract_fil.py
#
#  Pull one shank's continuous .fil trace over a time-span into an npz, for the
#  interneuron-subtraction / per-chunk-whitener steps that need the filtered
#  voltage record (not the extracted .spkD).
#
#  .fil is the high-pass/median-filtered broadband trace: headerless, int16,
#  interleaved, nChannels per frame (same layout as .dat).  Frame t, channel c
#  lives at flat offset t*nChannels + c.  This reads ONLY the group's channels
#  via a lazy memmap, so the other channels are never loaded.
#
#  Size: full-rate 8-channel × 10 min ≈ 312 MB — fine to keep on the rig, far
#  over the 30 MB share limit.  Use --seconds for a short full-rate slice to
#  send (spike-resolution), or --decimate for a long low-rate slice (LFP only;
#  decimation destroys spike shape — never use it for spike inspection).
# ════════════════════════════════════════════════════════════════════════════

import argparse
import os
import numpy as np


def read_clu(path):
    a = np.fromfile(path, dtype="<i4")
    return a[1:]


def read_res(path):
    return np.fromfile(path, dtype="<i8")


def group_channels(yaml_path, group):
    import yaml
    cfg = yaml.safe_load(open(yaml_path))
    g = cfg["anatomicalDescription"]["channelGroups"][group - 1]["channels"]
    return [c["id"] for c in g]


def group_geometry(probe_path, chans):
    import yaml
    geo = yaml.safe_load(open(probe_path))["probeFile"]["sites"]["geometry"]
    return np.array([geo[c] for c in chans], float)


def main():
    ap = argparse.ArgumentParser(description="Extract one shank's .fil trace over a window")
    ap.add_argument("--fil", required=True)
    ap.add_argument("--total-channels", type=int, default=96, dest="nchan_total")
    ap.add_argument("--sr", type=float, default=32552.0)
    ap.add_argument("--minutes", required=True, help="window 'a,b' in minutes")
    ap.add_argument("--seconds", type=float, default=0.0,
                    help="take only the first N seconds of the window (full-rate, sendable)")
    ap.add_argument("--decimate", type=int, default=1,
                    help="anti-aliased downsample factor (LFP inspection only; NOT for spikes)")
    # channel selection: either explicit, or resolved from yaml group
    ap.add_argument("--channels", help='explicit global channel ids, e.g. "32,33,34,35,36,37,38,39"')
    ap.add_argument("--yaml-config")
    ap.add_argument("--group", type=int, default=5)
    ap.add_argument("--probe", help="probe geometry (.probe) to store site coords")
    # optional bundling of spike times for the subtraction step
    ap.add_argument("--res")
    ap.add_argument("--clu")
    ap.add_argument("--out", default="fil_chunk.npz")
    args = ap.parse_args()

    if args.channels:
        chans = [int(x) for x in args.channels.split(",")]
    elif args.yaml_config:
        chans = group_channels(args.yaml_config, args.group)
    else:
        ap.error("need --channels or --yaml-config")
    chans = np.asarray(chans, int)
    print(f"group {args.group}: global channels {chans.tolist()}")

    # lazy memmap of the whole file as (n_frames, n_total) — no full load
    itemsize = 2
    nbytes = os.path.getsize(args.fil)
    if nbytes % (itemsize * args.nchan_total):
        print(f"WARNING: file size {nbytes} not a multiple of "
              f"{itemsize*args.nchan_total} (n_total may be wrong)")
    n_frames = nbytes // (itemsize * args.nchan_total)
    mm = np.memmap(args.fil, dtype="<i2", mode="r", shape=(n_frames, args.nchan_total))
    dur_min = n_frames / args.sr / 60.0
    print(f"file: {n_frames} frames = {dur_min:.1f} min at {args.sr} Hz, "
          f"{args.nchan_total} channels")

    a, b = (float(x) for x in args.minutes.split(","))
    lo = int(a * 60.0 * args.sr)
    hi = int(b * 60.0 * args.sr)
    if args.seconds > 0:
        hi = min(hi, lo + int(args.seconds * args.sr))
    lo = max(lo, 0); hi = min(hi, n_frames)
    print(f"window [{lo},{hi}) samples = {(hi-lo)/args.sr:.1f} s")

    # read ONLY the group's columns over the span (this is the only big read)
    trace = np.asarray(mm[lo:hi, chans], dtype=np.int16)        # (n, n_ch)

    # sanity: per-channel RMS + saturation, to verify the column pick
    rms = trace.astype(np.float64).std(0)
    sat = int((np.abs(trace) >= 32767).sum())
    print("per-channel RMS:", np.round(rms, 1).tolist())
    print(f"saturated samples: {sat} ({100*sat/trace.size:.4f}%)")

    if args.decimate > 1:
        from scipy.signal import decimate as _dec
        print(f"DECIMATING by {args.decimate} (anti-aliased) — LFP inspection only, "
              "spike shape destroyed")
        trace = _dec(trace.astype(np.float64), args.decimate, axis=0,
                     ftype="fir").astype(np.float32)
        eff_sr = args.sr / args.decimate
    else:
        eff_sr = args.sr

    xy = group_geometry(args.probe, chans) if args.probe else np.zeros((len(chans), 2))

    # optional spike times within the window (absolute sample units)
    res_w = clu_w = None
    if args.res and args.clu:
        res = read_res(args.res); clu = read_clu(args.clu)
        n = min(len(res), len(clu)); res, clu = res[:n], clu[:n]
        m = (res >= lo) & (res < hi)
        res_w = res[m].astype(np.int64); clu_w = clu[m].astype(np.int32)
        print(f"bundled spikes in window: {res_w.size} ({np.unique(clu_w).size} clusters)")

    meta = dict(fil=os.path.basename(args.fil), group=args.group, sr=args.sr,
                eff_sr=eff_sr, n_total_channels=args.nchan_total,
                channels=chans.tolist(), lo_sample=int(lo), hi_sample=int(hi),
                decimate=args.decimate, minutes=args.minutes,
                layout="headerless int16 interleaved, frame=t*nChan+c, .fil high-pass filtered")
    save = dict(trace=trace, channels=chans.astype(np.int32),
                xy=xy.astype(np.float64), meta=np.array(str(meta)))
    if res_w is not None:
        save["res"] = res_w; save["clu"] = clu_w
    np.savez_compressed(args.out, **save)
    mb = os.path.getsize(args.out) / 1e6
    print(f"wrote {args.out} ({mb:.1f} MB)")
    if mb > 28:
        print("  NOTE: >28 MB — use --seconds for a short full-rate slice, or "
              "--decimate for LFP-only, to fit the 30 MB share limit")


if __name__ == "__main__":
    main()
