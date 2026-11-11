#!/usr/bin/env python3
"""Insert the sub-sample PHASE column into the .fet of a super-sampled (_U<factor>) space.

Klusters' layout of a _U space's .fet is [features ..., phase, time]: the phase (0 .. factor-1,
in ticks) is the last feature column before the time, and it is what the realign, nudge and sync
of that space read and move.  ndm_pca writes [features ..., time] (process_mergefeatures); this
step rewrites the file with the phase column inserted before the time, the phase of each spike
being tick - sample x factor from the group's tick .res (<base>.res.<resToken>_U<factor>.N,
ticks = sample x factor + phase, which ndm_extractspikes --supersample writes).

  process_fet_phase.py --fet <base>.fet.<tok>_U4.N --res <shared .res> --ticks <tick .res> --factor 4

A .fet that already carries the phase column (one more column than the features + time) is
left alone.  The .res, the tick .res and the .fet must hold the same number of spikes.
"""
import argparse
import os
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--fet", required=True)
    ap.add_argument("--res", required=True, help="the shared base-rate .res the .fet's time column holds")
    ap.add_argument("--ticks", required=True, help="the group's tick .res (sample x factor + phase)")
    ap.add_argument("--factor", required=True, type=int)
    ap.add_argument("--nfeatures", type=int, default=0,
                    help="feature columns before the time, to tell a .fet that carries the phase already (0 = infer: "
                         "the phase column is added unless a column of values in 0..factor-1 precedes the time)")
    a = ap.parse_args()
    nd = int(np.fromfile(a.fet, np.int32, 1)[0])
    F = np.fromfile(a.fet, np.int64, offset=4).reshape(-1, nd)
    res = np.fromfile(a.res, np.int64)
    ticks = np.fromfile(a.ticks, np.int64)
    if not (F.shape[0] == res.size == ticks.size):
        sys.exit(f"process_fet_phase: {os.path.basename(a.fet)} has {F.shape[0]} rows, {os.path.basename(a.res)} "
                 f"{res.size} spikes, {os.path.basename(a.ticks)} {ticks.size} -- not one set of spikes")
    if not np.array_equal(F[:, -1], res):
        sys.exit(f"process_fet_phase: the time column of {os.path.basename(a.fet)} is not {os.path.basename(a.res)}")
    phase = ticks - res * a.factor
    if phase.min() < 0 or phase.max() >= a.factor:
        sys.exit(f"process_fet_phase: {os.path.basename(a.ticks)} does not agree with {os.path.basename(a.res)} "
                 f"(ticks - sample x {a.factor} outside 0..{a.factor - 1})")
    has = (a.nfeatures > 0 and nd == a.nfeatures + 2) or \
          (a.nfeatures == 0 and nd >= 3 and F[:, -2].min() >= 0 and F[:, -2].max() < a.factor
           and np.array_equal(F[:, -2], phase))
    if has:
        print(f"[fet-phase] {os.path.basename(a.fet)}: the phase column is there already ({nd} columns)")
        return 0
    out = np.column_stack([F[:, :-1], phase, F[:, -1]]).astype(np.int64)
    tmp = a.fet + ".tmp"
    with open(tmp, "wb") as fh:
        np.array([out.shape[1]], np.int32).tofile(fh)
        out.tofile(fh)
    os.replace(tmp, a.fet)
    print(f"[fet-phase] {os.path.basename(a.fet)}: phase column inserted ({nd} -> {out.shape[1]} columns; "
          f"{int((phase != 0).sum())} of {phase.size} spikes with a non-zero phase)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
