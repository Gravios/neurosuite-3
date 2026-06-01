#!/usr/bin/env python3
"""ascii2bin_cluress — convert legacy ASCII .clu.N / .res.N files to the
binary format the neurosuite-3 pipeline reads (neurofileio).

Binary layout (little-endian, matching neurosuite-core/neurofileio):
    .res.N   raw int64 array, one timestamp per spike, NO header
    .clu.N   int32 header (nClusters) followed by one int32 cluster id per spike

Usage:
    ascii2bin_cluress FILE [FILE ...] [-o OUT | --suffix S] [--type clu|res]

By default each FILE is converted IN PLACE (written atomically via a temp file
+ rename), since the pipeline reads .res.N/.clu.N regardless of ASCII-vs-binary
encoding.  Pass --suffix to write FILE+S instead (e.g. --suffix .bin), or -o for
a single explicit output path.  The type is auto-detected from the name
(".clu." / ".res.") and can be forced with --type.
"""
import argparse
import os
import re
import sys
import numpy as np


def _read_ints(path):
    """Fast read of a whitespace/newline-separated integer text file."""
    with open(path, "rb") as fh:
        data = fh.read()
    if not data.strip():
        return np.empty(0, dtype=np.int64)
    return np.array(data.split(), dtype=np.int64)


def detect_type(path, forced):
    if forced:
        return forced
    base = os.path.basename(path)
    # match a 'clu'/'res' token delimited by '.' or '_' (or string ends),
    # tolerating the dotted (.res.N) and other separators
    if re.search(r"(?:^|[._])clu(?:[._]|$)", base):
        return "clu"
    if re.search(r"(?:^|[._])res(?:[._]|$)", base):
        return "res"
    raise ValueError(f"cannot infer type from '{base}'; pass --type clu|res")


def convert_res(vals):
    """ASCII .res -> binary: raw little-endian int64, no header."""
    if vals.size and (vals < 0).any():
        raise ValueError("negative timestamp in .res")
    return vals.astype("<i8").tobytes()


def convert_clu(vals):
    """ASCII .clu -> binary: int32 header (nClusters) + int32 ids.

    The first ASCII value is the cluster count (header); the rest are per-spike
    ids.  Both are written as int32 (cluster ids are small)."""
    if vals.size == 0:
        raise ValueError("empty .clu file (expected a header line at minimum)")
    header = vals[0]
    ids = vals[1:]
    if ids.size:
        n_distinct = np.unique(ids).size
        if header != n_distinct:
            sys.stderr.write(
                f"  WARNING: .clu header says {header} clusters but {n_distinct} "
                f"distinct ids present (header written as-is)\n")
    if (vals < np.iinfo(np.int32).min).any() or (vals > np.iinfo(np.int32).max).any():
        raise ValueError("cluster value out of int32 range")
    return vals.astype("<i4").tobytes()


def write_atomic(out_path, payload):
    tmp = out_path + ".tmp.ascii2bin"
    with open(tmp, "wb") as fh:
        fh.write(payload)
    os.replace(tmp, out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--output", default=None,
                    help="explicit output path (only with a single input)")
    ap.add_argument("--suffix", default=None,
                    help="write to FILE+SUFFIX instead of in place")
    ap.add_argument("--type", choices=["clu", "res"], default=None,
                    help="force the file type (default: infer from name)")
    args = ap.parse_args()

    if args.output and len(args.files) != 1:
        ap.error("-o/--output requires exactly one input file")

    rc = 0
    for path in args.files:
        try:
            ftype = detect_type(path, args.type)
            vals = _read_ints(path)
            payload = convert_clu(vals) if ftype == "clu" else convert_res(vals)
            if args.output:
                out_path = args.output
            elif args.suffix:
                out_path = path + args.suffix
            else:
                out_path = path
            write_atomic(out_path, payload)
            n = vals.size - 1 if ftype == "clu" else vals.size
            print(f"  {os.path.basename(path)}: {ftype} -> "
                  f"{os.path.basename(out_path)} ({n} spikes, {len(payload)} bytes)")
        except Exception as e:                                   # noqa: BLE001
            sys.stderr.write(f"  ERROR converting {path}: {e}\n")
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
