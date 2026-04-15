#!/usr/bin/env python3
"""
diagnose_cluster.py — inspect a cluster for data corruption.

Usage:
  python3 diagnose_cluster.py /path/to/session/basename GROUP CLUSTER_ID

Example:
  python3 diagnose_cluster.py /data/jg05-20120316/jg05-20120316 7 55

File formats (from data.cpp):
  .res.N  — int64 LE, one timestamp per spike
  .clu.N  — int32 LE: [nClusters, id0, id1, ...]
  .fet.N  — int32 nDims header, then nSpikes*nDims int64 LE row-major
  .spk.N  — int16 LE, nSpikes * nSamples*nChans (sample-major: s*nChan+ch)
"""
import sys, os, numpy as np, struct

def die(msg): print(f"ERROR: {msg}", file=sys.stderr); sys.exit(1)

if len(sys.argv) < 4: print(__doc__); sys.exit(0)

base, grp, target_clu = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])

res_path = f"{base}.res.{grp}"
clu_path = f"{base}.clu.{grp}"
fet_path = f"{base}.fet.{grp}"
spk_path = f"{base}.spk.{grp}"

for p in [res_path, clu_path]: 
    if not os.path.exists(p): die(f"Not found: {p}")

# ── .res — int64 LE ───────────────────────────────────────────────────────
res = np.fromfile(res_path, dtype='<i8')
N = len(res)
print(f"\n.res.{grp}: {N} spikes")
print(f"  timestamps: min={res.min():,}  max={res.max():,}")
neg_ts = (res < 0).sum()
if neg_ts: print(f"  *** {neg_ts} negative timestamps ***")
unsorted = (np.diff(res) < 0).sum()
if unsorted: print(f"  *** {unsorted} out-of-order timestamps ***")
else: print(f"  timestamps sorted: OK")

# ── .clu — int32 LE: [nClusters, id×N] ───────────────────────────────────
clu_raw = np.fromfile(clu_path, dtype='<i4')
n_clu_declared = int(clu_raw[0])
clu = clu_raw[1:]
print(f"\n.clu.{grp}: {n_clu_declared} clusters declared, {len(clu)} entries")
if len(clu) != N:
    print(f"  *** MISMATCH: .clu has {len(clu)} entries, .res has {N} ***")

spike_idx = np.where(clu == target_clu)[0]   # 0-based global positions
n_spk = len(spike_idx)
print(f"\nCluster {target_clu}: {n_spk} spikes")
if n_spk == 0:
    print("  Cluster is empty or does not exist."); sys.exit(0)

print(f"  positions in file: {spike_idx[0]}..{spike_idx[-1]}")
cl_ts = res[spike_idx]
print(f"  timestamps: min={cl_ts.min():,}  max={cl_ts.max():,}")
if (np.diff(cl_ts) < 0).sum():
    print(f"  *** cluster timestamps out of order ***")

# ── .fet — int32 nDims header + nSpikes*nDims int64 LE ───────────────────
if os.path.exists(fet_path):
    with open(fet_path, 'rb') as f:
        n_dims = struct.unpack('<i', f.read(4))[0]
    expected = 4 + N * n_dims * 8
    actual = os.path.getsize(fet_path)
    print(f"\n.fet.{grp}: nDims={n_dims}, expected={expected} B, actual={actual} B")
    if expected != actual:
        print(f"  *** SIZE MISMATCH: {actual-expected:+d} bytes ***")
    else:
        fet = np.fromfile(fet_path, dtype='<i8', offset=4).reshape(N, n_dims)
        cl_fet = fet[spike_idx]
        print(f"  Feature ranges for cluster {target_clu}:")
        qt_limit = 1_000_000
        any_bad = False
        for d in range(n_dims):
            col = cl_fet[:, d]
            vmin, vmax = int(col.min()), int(col.max())
            bad = int((np.abs(col) > qt_limit).sum())
            label = f"dim{d:3d}" if d < n_dims-1 else f"dim{d:3d}(ts)"
            flag = f"  *** {bad} values exceed Qt ±{qt_limit:,} scatter limit ***" if bad else ""
            print(f"    {label}: [{vmin:>14,} .. {vmax:>14,}]{flag}")
            if bad: any_bad = True
        if any_bad:
            print(f"\n  *** Feature values outside Qt rendering range will crash ClusterView ***")
        else:
            print(f"  All feature values within Qt safe range.")
else:
    n_dims = None
    print(f"\n.fet.{grp}: not found")

# ── .spk — int16 LE, nSpikes * nPts ──────────────────────────────────────
if os.path.exists(spk_path):
    sz = os.path.getsize(spk_path)
    if sz % (N * 2) != 0:
        print(f"\n.spk.{grp}: {sz} bytes — NOT divisible by nSpikes*2")
        print(f"  *** .spk file size is inconsistent with spike count ***")
    else:
        pts = sz // (N * 2)
        print(f"\n.spk.{grp}: {sz} bytes → {pts} int16 pts/spike")
        spk = np.fromfile(spk_path, dtype='<i2').reshape(N, pts)
        cl_spk = spk[spike_idx]
        flat  = np.array([(r.max() == r.min()) for r in cl_spk]).sum()
        zeros = (cl_spk == 0).all(axis=1).sum()
        print(f"  Flat waveforms  : {flat}")
        print(f"  All-zero        : {zeros}")
        if flat == 0:
            print(f"  Waveforms look OK")
        # Show amplitude stats
        amp = cl_spk.max(axis=1).astype(float) - cl_spk.min(axis=1).astype(float)
        print(f"  Amplitude range : {amp.min():.0f} .. {amp.max():.0f} ADC counts")
        if amp.max() > 32000:
            print(f"  *** Saturated waveforms (amplitude > 32000) ***")
else:
    print(f"\n.spk.{grp}: not found")

print("\nDone.")
