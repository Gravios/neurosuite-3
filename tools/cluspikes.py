#!/usr/bin/env python3
"""cluspikes — spikes-vs-noise summary for binary .clu.N files.

Binary .clu (neurofileio): int32 header (nClusters) + one int32 cluster id per
spike.  Cluster 0 = artifact, 1 = noise/MUA (both counted as "noise"); clusters
>= 2 are sorted units ("spikes").

Usage:  cluspikes FILE.clu.N [FILE ...] [-v]
        -v / --per-cluster  also list the per-cluster counts
"""
import os
import sys
import numpy as np

NOISE = (0, 1)


def summarize(path, per_cluster):
    a = np.fromfile(path, dtype="<i4")
    if a.size < 1:
        print(f"{os.path.basename(path)}: empty / not a binary .clu")
        return
    n_clusters = int(a[0])
    ids = a[1:]
    total = ids.size
    if total == 0:
        print(f"{os.path.basename(path)}: 0 spikes (header nClusters={n_clusters})")
        return
    noise = int(np.isin(ids, NOISE).sum())
    spikes = total - noise
    print(f"{os.path.basename(path)}: {total} total | "
          f"spikes {spikes} ({100*spikes/total:.1f}%) | "
          f"noise {noise} ({100*noise/total:.1f}%) | "
          f"units {max(0, n_clusters - len(NOISE))} (header nClusters={n_clusters})")
    if per_cluster:
        vals, counts = np.unique(ids, return_counts=True)
        for v, c in zip(vals.tolist(), counts.tolist()):
            tag = ("artifact" if v == 0 else "noise" if v == 1 else "spike")
            print(f"    clu {v:4d} [{tag:8s}]: {c} ({100*c/total:.1f}%)")


def main():
    args = [a for a in sys.argv[1:] if a not in ("-v", "--per-cluster")]
    per_cluster = any(a in ("-v", "--per-cluster") for a in sys.argv[1:])
    if not args:
        sys.exit("usage: cluspikes FILE.clu.N [...] [-v]")
    for p in args:
        summarize(p, per_cluster)


if __name__ == "__main__":
    main()
