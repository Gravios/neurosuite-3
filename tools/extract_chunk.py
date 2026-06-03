#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════
#  extract_chunk.py
#
#  Window a time-slice of a neurosuite-3 group (.spkD / .res / .clu) into a
#  single compact .npz, small enough to move off the rig for direct inspection.
#
#  .spkD is the stderiv-transformed spike file (sdiffOrderN), stored int16,
#  sample-major (flat index within a spike = sample*nCh + ch) with no header —
#  same layout as .spk, but the values are spatial-derivative features, not raw
#  voltage.  This tool does not interpret them; it copies the windowed subset
#  verbatim plus res/clu and the probe geometry, so the analysis side sees
#  exactly what the sorter produced.
#
#  A 5–10 min chunk on an 8-channel group is ~20k spikes ≈ 10–12 MB compressed.
# ════════════════════════════════════════════════════════════════════════════

import argparse
import os
import numpy as np


# ── neurosuite-3 loaders (binary, modern standard) ──────────────────────────
def read_clu(path):
    """int32 nClusters header + int32 ids; header dropped."""
    a = np.fromfile(path, dtype="<i4")
    if a.size < 1:
        raise ValueError(f"{path}: empty .clu")
    return a[1:]


def read_res(path):
    """Spike sample times: little-endian int64, no header."""
    return np.fromfile(path, dtype="<i8")


def read_spk(path, n_samp, n_ch):
    """int16, sample-major, no header -> (n_spikes, n_samp, n_ch)."""
    raw = np.fromfile(path, dtype="<i2")
    per = n_samp * n_ch
    if raw.size % per:
        # tolerate a trailing partial record; warn via return of usable count
        raw = raw[: (raw.size // per) * per]
    nsp = raw.size // per
    return raw.reshape(nsp, n_samp, n_ch)


def load_group_geometry(probe_path, yaml_path, group):
    """(n_ch, 2) site coords for a clu group, via yaml channelGroups + probe."""
    import yaml
    cfg = yaml.safe_load(open(yaml_path))
    chans = [c["id"] for c in
             cfg["anatomicalDescription"]["channelGroups"][group - 1]["channels"]]
    geo = yaml.safe_load(open(probe_path))["probeFile"]["sites"]["geometry"]
    return np.array([geo[c] for c in chans], float)


def main():
    ap = argparse.ArgumentParser(description="Extract a time-windowed chunk to npz")
    ap.add_argument("--spk", required=True, help=".spkD (stderiv) file")
    ap.add_argument("--res", required=True)
    ap.add_argument("--clu", required=True)
    ap.add_argument("--nch", type=int, default=8)
    ap.add_argument("--nsamp", type=int, default=32)
    ap.add_argument("--sr", type=float, default=32552.0)
    ap.add_argument("--minutes", required=True, help="window 'a,b' in minutes")
    ap.add_argument("--group", type=int, default=5)
    ap.add_argument("--sdiff-order", type=int, default=3, dest="sdiff_order",
                    help="stderiv order the .spkD was built with (metadata only)")
    ap.add_argument("--probe", help="probe geometry file (.probe yaml)")
    ap.add_argument("--yaml-config", help="session .yaml")
    ap.add_argument("--out", default="chunk.npz")
    ap.add_argument("--max-spikes", type=int, default=0,
                    help="optional cap (0 = all in window); stratified by time")
    args = ap.parse_args()

    clu = read_clu(args.clu)
    res = read_res(args.res)
    spk = read_spk(args.spk, args.nsamp, args.nch)
    n = min(len(clu), len(res), len(spk))
    if not (len(clu) == len(res) == len(spk)):
        print(f"WARNING: length mismatch clu={len(clu)} res={len(res)} "
              f"spk={len(spk)} — trimming to {n}")
    clu, res, spk = clu[:n], res[:n], spk[:n]

    a, b = (float(x) for x in args.minutes.split(","))
    lo, hi = a * 60.0 * args.sr, b * 60.0 * args.sr
    idx = np.flatnonzero((res >= lo) & (res < hi))
    if args.max_spikes and idx.size > args.max_spikes:
        keep = np.linspace(0, idx.size - 1, args.max_spikes).astype(int)
        idx = idx[keep]
        print(f"capping to {idx.size} spikes (stratified by time)")
    print(f"window [{lo:.0f},{hi:.0f}) samples ({a}-{b} min): "
          f"{idx.size} of {n} spikes")

    ids, counts = np.unique(clu[idx], return_counts=True)
    print(f"clusters in window: {ids.size}  (e.g. "
          f"{', '.join(f'{c}:{n_}' for c, n_ in zip(ids[:6], counts[:6]))}...)")

    xy = np.zeros((args.nch, 2))
    if args.probe and args.yaml_config:
        xy = load_group_geometry(args.probe, args.yaml_config, args.group)

    meta = dict(session=os.path.basename(args.clu), group=args.group,
                sr=args.sr, n_samp=args.nsamp, n_ch=args.nch,
                sdiff_order=args.sdiff_order, minutes=args.minutes,
                lo_sample=float(lo), hi_sample=float(hi), n_spikes=int(idx.size),
                spkD_note="stderiv-transformed (sdiffOrder%d), int16, sample-major "
                          "(idx=sample*nCh+ch)" % args.sdiff_order)

    np.savez_compressed(
        args.out,
        spkD=spk[idx].astype(np.int16),       # (n, n_samp, n_ch)
        res=res[idx].astype(np.int64),        # absolute sample times
        clu=clu[idx].astype(np.int32),
        xy=xy.astype(np.float64),             # (n_ch, 2) site coords µm
        meta=np.array(str(meta)),
    )
    mb = os.path.getsize(args.out) / 1e6
    print(f"wrote {args.out} ({mb:.1f} MB)")
    if mb > 28:
        print("  NOTE: >28 MB — narrow --minutes or set --max-spikes to fit the "
              "30 MB upload limit")


if __name__ == "__main__":
    main()
