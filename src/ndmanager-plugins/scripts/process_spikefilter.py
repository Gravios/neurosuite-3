#!/usr/bin/env python3
# ════════════════════════════════════════════════════════════════════════════
#  process_spikefilter.py — post-extraction spike filter for one spike group.
#
#  Called by ndm_extractspikes after a waveform file has been (re-)extracted at
#  the shared spike times.  Works on the .spk as written, in its own transform
#  domain (standard, sdiff or stderiv alike), and removes:
#
#    flat runs   a spike whose waveform has more than --flat-run identical
#                consecutive samples on any channel -- zero-filled gaps (a
#                back-filled DAC ring-buffer overrun), stuck lines, or a
#                transform that maps a flat stretch to zeros;
#    duplicates  a spike positioned within --dedup samples of the previous kept
#                spike (time order; the earlier one is kept; 0 = identical
#                positions only).
#
#  The .res is SHARED by every waveform variant of the group, so dropping rows is
#  never local to one file: the one keep-mask is applied to every live per-spike
#  file of the group that still has the original row count -- the shared .res,
#  every .spk/.fet/.clu/.clc variant, and post-group stages -- so the set stays
#  row-aligned.  A file at the kept count is left (already filtered); a file at
#  any other count is a different spike set and is reported, never touched.
#  Files are deduplicated by their real path, so a symlinked alias is subset once.
#  The shared .res is backed up once as <res>.prefilter.
#
#  Formats (binary, little-endian):
#    .res        int64 per spike
#    .spk        int16, nSpikes x nSamples x nChannels
#    .fet        int32 nColumns, then nSpikes x nColumns int64
#    .clu/.clc   int32 nClusters, then int32 per spike
# ════════════════════════════════════════════════════════════════════════════
import argparse
import glob
import os
import sys

import numpy as np

PERSPIKE_TYPES = ("res", "spk", "fet", "clu", "clc")
# Name tokens after the group that mark a backup / sidecar, never a live file.
SIDECAR = {"bak", "prefilter", "stalebkp", "pending", "tmp", "swp", "autosave",
           "npz", "npy", "gz", "zip", "part"}


def flat_run_mask(spk, max_run, chunk=50000):
    """True for spikes with more than max_run identical consecutive samples on any
    channel.  spk: (n, nSamples, nChannels) int16 (memmap ok)."""
    n = spk.shape[0]
    out = np.zeros(n, bool)
    if max_run <= 0 or spk.shape[1] <= max_run:
        return out
    for a in range(0, n, chunk):
        w = np.asarray(spk[a:a + chunk])
        eq = (w[:, 1:, :] == w[:, :-1, :]).astype(np.int16)        # (m, ns-1, nc)
        # max_run equalities in a row  <=>  max_run+1 identical samples
        c = np.concatenate([np.zeros((eq.shape[0], 1, eq.shape[2]), np.int32),
                            np.cumsum(eq, axis=1, dtype=np.int32)], axis=1)
        win = c[:, max_run:, :] - c[:, :-max_run, :]
        out[a:a + chunk] = (win >= max_run).any(axis=(1, 2))
    return out


def dedup_mask(res, within, exclude):
    """True for spikes positioned within `within` samples of the previous KEPT spike
    (time order, earlier kept), ignoring spikes already in `exclude`."""
    dup = np.zeros(res.size, bool)
    if within < 0:
        return dup
    order = np.argsort(res, kind="stable")
    last = None
    for i in order:
        if exclude[i]:
            continue
        t = int(res[i])
        if last is not None and t - last <= within:
            dup[i] = True
            continue
        last = t
    return dup


def rows_of(path, typ, nsamp, nchan):
    size = os.path.getsize(path)
    if typ == "res":
        return size // 8 if size % 8 == 0 else None
    if typ == "spk":
        rec = 2 * nsamp * nchan
        return size // rec if size % rec == 0 else None
    if typ == "fet":
        with open(path, "rb") as fh:
            hdr = np.frombuffer(fh.read(4), "<i4")
        if hdr.size != 1 or hdr[0] <= 0 or (size - 4) % (8 * int(hdr[0])):
            return None
        return (size - 4) // (8 * int(hdr[0]))
    if typ in ("clu", "clc"):
        return size // 4 - 1 if size % 4 == 0 and size >= 4 else None
    return None


def subset(path, typ, keep, nsamp, nchan):
    """Rewrite `path` keeping rows `keep`, via a temp file replaced atomically onto the
    REAL path (a symlink stays a symlink)."""
    real = os.path.realpath(path)
    tmp = real + ".filter-tmp"
    if typ == "res":
        np.fromfile(real, "<i8")[keep].tofile(tmp)
    elif typ == "spk":
        m = np.memmap(real, "<i2", "r").reshape(-1, nsamp, nchan)
        np.asarray(m[keep]).tofile(tmp)
        del m
    elif typ == "fet":
        raw = np.fromfile(real, np.uint8)
        ncol = int(np.frombuffer(raw[:4].tobytes(), "<i4")[0])
        body = np.frombuffer(raw[4:].tobytes(), "<i8").reshape(-1, ncol)[keep]
        with open(tmp, "wb") as fh:
            fh.write(raw[:4].tobytes())
            fh.write(np.ascontiguousarray(body).tobytes())
    elif typ in ("clu", "clc"):
        raw = np.fromfile(real, "<i4")
        np.concatenate([raw[:1], raw[1:][keep]]).astype("<i4").tofile(tmp)
    os.replace(tmp, real)


def siblings(base, group):
    """Live per-spike files of `group`: <base>.<type>[.<method>].<group>[.<stage>...]."""
    bname = os.path.basename(base)
    found = []
    for typ in PERSPIKE_TYPES:
        for path in sorted(glob.glob(glob.escape(base) + "." + typ + ".*")):
            tail = os.path.basename(path)[len(bname) + 1:].split(".")
            if not tail or tail[0] != typ:
                continue
            gi = next((i for i in range(1, len(tail)) if tail[i].isdigit()), None)
            if gi is None or tail[gi] != str(group):
                continue
            stage = tail[gi + 1:]
            if any(t.isdigit() or t in SIDECAR or t.endswith("bkp") for t in stage):
                continue
            found.append((path, typ))
    return found


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0] if __doc__ else None)
    ap.add_argument("--base", required=True, help="session base path")
    ap.add_argument("--group", required=True, type=int)
    ap.add_argument("--spk", required=True, help="the freshly extracted .spk to inspect")
    ap.add_argument("--res", required=True, help="the shared .res it was extracted at")
    ap.add_argument("--nsamples", required=True, type=int)
    ap.add_argument("--nchannels", required=True, type=int)
    ap.add_argument("--flat-run", type=int, default=0,
                    help="reject > N identical consecutive samples on any channel (0 = off)")
    ap.add_argument("--dedup", type=int, default=-1,
                    help="drop a spike within N samples of the previous kept one (-1 = off)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    res = np.fromfile(a.res, "<i8")
    spk = np.memmap(a.spk, "<i2", "r")
    rec = a.nsamples * a.nchannels
    if spk.size % rec:
        sys.exit(f"process_spikefilter: {a.spk} is not a whole number of "
                 f"{a.nsamples}x{a.nchannels} waveforms")
    spk = spk.reshape(-1, a.nsamples, a.nchannels)
    n = res.size
    if spk.shape[0] != n:
        sys.exit(f"process_spikefilter: {a.spk} has {spk.shape[0]} waveforms but "
                 f"{a.res} has {n} spike times -- refusing to filter a misaligned pair")

    flat = flat_run_mask(spk, a.flat_run)
    dup = dedup_mask(res, a.dedup, flat)
    keep = ~(flat | dup)
    nkeep = int(keep.sum())
    print(f"[spikefilter] group {a.group}: {n} spikes, {int(flat.sum())} with a flat run "
          f"> {a.flat_run} samples, {int(dup.sum())} duplicate(s) within "
          f"{max(a.dedup, 0)} samples -> {nkeep} kept")
    if nkeep == n:
        return 0
    if a.dry_run:
        print("[spikefilter] dry run: no file changed")
        return 0
    del spk

    backup = os.path.realpath(a.res) + ".prefilter"
    if not os.path.exists(backup):
        res.tofile(backup)
        print(f"[spikefilter] backed up the shared spike times -> {os.path.basename(backup)}")

    seen, changed, done, other = set(), [], [], []
    for path, typ in siblings(a.base, a.group):
        real = os.path.realpath(path)
        if real in seen:
            continue                      # a symlinked alias of a file already handled
        seen.add(real)
        r = rows_of(real, typ, a.nsamples, a.nchannels)
        if r == n:
            subset(real, typ, keep, a.nsamples, a.nchannels)
            changed.append(os.path.basename(path))
        elif r == nkeep:
            done.append(os.path.basename(path))
        else:
            other.append((os.path.basename(path), r))
    if changed:
        print(f"[spikefilter] subset to {nkeep} rows: " + ", ".join(changed))
    if done:
        print("[spikefilter] already at the kept count: " + ", ".join(done))
    for name, r in other:
        print(f"[spikefilter] WARNING: {name} has {r} rows (neither {n} nor {nkeep}); "
              f"left untouched -- it belongs to a different spike set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
