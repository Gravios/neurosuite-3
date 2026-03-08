#!/usr/bin/env python3
"""
process_applydrift.py
=====================
Convert a SESSION.drift file into per-group adaptive chunk boundary files
(SESSION.chunks.N) for KlustaKwik.

Workflow
--------
1.  Read the drift timeseries for the *source* spike group from SESSION.drift.
2.  Walk the timeseries and split it into chunks such that the cumulative probe
    displacement within each chunk does not exceed ``--thresh-um`` µm.
    Windows where drift is slow → large chunks; windows with fast drift → small
    chunks.  Chunk boundaries always align to drift-window edges.
3.  Write SESSION.chunks.G (one file per target group) containing the boundary
    times in seconds.  KlustaKwik reads this via ``-ChunkFile SESSION.chunks.G``
    and uses the boundaries in place of its uniform ``-ChunkMinutes`` grid.

Adaptive boundary algorithm
-----------------------------
The drift file has W windows of width ``window_sec``.  Let d[w] be the
cumulative drift in µm relative to the reference window.

We scan forward and cut a new chunk boundary whenever the range of d[] values
seen since the last cut exceeds ``thresh_um``.  This produces tight chunks
during fast-drift episodes and large chunks during stable periods, keeping
intra-chunk positional variance below the threshold.

    boundary at t=0 always.
    for w = 1..W:
        if max(d[last_cut..w]) - min(d[last_cut..w]) >= thresh_um:
            emit boundary at t_start[w]
            last_cut = w
    boundary at t=end always.

Output format  (SESSION.chunks.G)
----------------------------------
Lines starting with ``#`` are comments and ignored by KlustaKwik.
All other lines are boundary times in seconds (float), one per line,
in ascending order.  The first boundary is always 0.0 and the last is
the end of the recording.

Usage
-----
    python3 process_applydrift.py \\
        --session          session_base \\
        --drift-file       session.drift \\
        --source-group     2 \\
        --target-groups    1 3 4 \\
        --sampling-rate    32552.0 \\
        --thresh-um        5.0 \\
        --min-chunk-sec    30.0

Dependencies:  python3 >= 3.10,  pyyaml
Copyright (C) 2025 neurosuite-3 contributors
SPDX-License-Identifier: GPL-3.0-or-later
"""

from __future__ import annotations
import argparse
import sys
import yaml
from pathlib import Path
from typing import Optional


# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Compute adaptive KlustaKwik chunk boundaries from a drift file."
    )
    p.add_argument("--session",        required=True,
                   help="Session base name (used to construct output filenames).")
    p.add_argument("--drift-file",     required=True,
                   help="Path to SESSION.drift YAML produced by ndm_estimatedrift.")
    p.add_argument("--source-group",   type=int, required=True,
                   help="1-based spike group whose drift timeseries to use.")
    p.add_argument("--target-groups",  type=int, nargs="+", default=[],
                   help="1-based spike groups to write .chunks files for. "
                        "When empty, writes for the source group only.")
    p.add_argument("--thresh-um",      type=float, default=5.0,
                   help="Max cumulative drift (µm) allowed within one chunk. "
                        "Smaller values give tighter, more chunks. (default: 5.0)")
    p.add_argument("--min-chunk-sec",  type=float, default=30.0,
                   help="Minimum chunk duration in seconds regardless of drift. "
                        "Prevents pathologically tiny chunks. (default: 30.0)")
    p.add_argument("--sampling-rate",  type=float, default=0.0,
                   help="Acquisition sampling rate (Hz). Used only for the "
                        "header comment in the output file.")
    return p.parse_args()


# ─────────────────────────────────────────────────────────────────────────────
# Drift file reader
# ─────────────────────────────────────────────────────────────────────────────

def load_drift(drift_path: str) -> dict:
    with open(drift_path) as f:
        return yaml.safe_load(f)


def find_shank_for_group(doc: dict, source_group: int) -> Optional[dict]:
    """Return the shank sub-dict that contains data for *source_group*."""
    for probe in doc.get("drift", {}).get("probes", []):
        for shank in probe.get("shanks", []):
            if shank.get("spikeGroup") == source_group:
                return shank
    return None


def extract_windows(shank: dict) -> list[dict]:
    """Return the list of window dicts from a shank entry."""
    return shank.get("windows", [])


# ─────────────────────────────────────────────────────────────────────────────
# Adaptive boundary computation
# ─────────────────────────────────────────────────────────────────────────────

def compute_chunk_boundaries(
    windows: list[dict],
    thresh_um: float,
    min_chunk_sec: float,
) -> list[float]:
    """Return a sorted list of chunk boundary times in seconds.

    Always starts at 0.0.  Always ends at the last window's t_end.
    Cuts a new boundary whenever the cumulative drift range since the
    previous cut reaches *thresh_um* — but never sooner than *min_chunk_sec*
    after the previous boundary.

    If the drift file has no usable windows (e.g. no units were tracked),
    falls back to a single chunk covering the full session.
    """
    if not windows:
        return [0.0]

    # Extract drift values; use None for windows with missing estimates.
    drift_vals: list[Optional[float]] = [w.get("drift_um") for w in windows]
    t_starts:   list[float]           = [float(w.get("t_start", 0.0)) for w in windows]
    t_ends:     list[float]           = [float(w.get("t_end",   0.0)) for w in windows]
    session_end = t_ends[-1]

    boundaries: list[float] = [0.0]
    last_cut_sec = 0.0
    # Track the running range of drift values since the last cut.
    window_drift_min: float =  1e9
    window_drift_max: float = -1e9

    for w, (d, t0, t1) in enumerate(zip(drift_vals, t_starts, t_ends)):
        if d is None:
            # Gap in drift estimate — treat as zero displacement for this window.
            d = 0.0

        window_drift_min = min(window_drift_min, d)
        window_drift_max = max(window_drift_max, d)
        range_um = window_drift_max - window_drift_min

        chunk_dur_so_far = t1 - last_cut_sec

        # Cut if: drift range >= threshold AND we have met the minimum duration.
        if range_um >= thresh_um and chunk_dur_so_far >= min_chunk_sec:
            boundaries.append(t0)
            last_cut_sec      = t0
            window_drift_min  = d
            window_drift_max  = d

    # Always close at the session end.
    if boundaries[-1] < session_end:
        boundaries.append(session_end)

    return boundaries


# ─────────────────────────────────────────────────────────────────────────────
# Output writer
# ─────────────────────────────────────────────────────────────────────────────

def write_chunks_file(
    path: str,
    boundaries: list[float],
    session: str,
    group: int,
    source_group: int,
    drift_file: str,
    thresh_um: float,
    sampling_rate: float,
) -> None:
    n_chunks = max(1, len(boundaries) - 1)
    with open(path, "w") as f:
        f.write(f"# KlustaKwik adaptive chunk boundaries\n")
        f.write(f"# session:      {session}\n")
        f.write(f"# group:        {group}\n")
        f.write(f"# source_group: {source_group}  (drift reference shank)\n")
        f.write(f"# drift_file:   {drift_file}\n")
        f.write(f"# thresh_um:    {thresh_um}\n")
        f.write(f"# n_chunks:     {n_chunks}\n")
        if sampling_rate > 0:
            f.write(f"# sampling_rate: {sampling_rate}\n")
        f.write(f"#\n")
        f.write(f"# Pass to KlustaKwik via:  -ChunkFile {path}\n")
        f.write(f"# (overrides -ChunkMinutes for this group)\n")
        f.write(f"#\n")
        for t in boundaries:
            f.write(f"{t:.6f}\n")


# ─────────────────────────────────────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────────────────────────────────────

def main() -> int:
    args = parse_args()

    # Load drift file.
    if not Path(args.drift_file).exists():
        print(f"[error] drift file not found: {args.drift_file}", file=sys.stderr)
        return 1

    doc = load_drift(args.drift_file)
    shank = find_shank_for_group(doc, args.source_group)
    if shank is None:
        print(
            f"[error] source group {args.source_group} not found in {args.drift_file}.\n"
            f"        Run ndm_estimatedrift with --source-group {args.source_group} first.",
            file=sys.stderr,
        )
        return 1

    windows = extract_windows(shank)
    if not windows:
        print(
            f"[warn] no drift windows found for group {args.source_group} "
            f"in {args.drift_file} — writing single-chunk boundary file.",
            file=sys.stderr,
        )

    # Pull window_sec from the file header if available.
    window_sec = float(doc.get("drift", {}).get("windowSec", 60.0))
    print(
        f"  Source group {args.source_group}: {len(windows)} drift windows "
        f"({window_sec}s each)",
        file=sys.stderr,
    )

    # Compute boundaries once — they are probe-level, same for all siblings.
    boundaries = compute_chunk_boundaries(
        windows,
        thresh_um     = args.thresh_um,
        min_chunk_sec = args.min_chunk_sec,
    )
    n_chunks = max(1, len(boundaries) - 1)
    print(
        f"  Adaptive boundaries: {len(boundaries)} cuts → {n_chunks} chunks "
        f"(thresh={args.thresh_um} µm, min={args.min_chunk_sec}s)",
        file=sys.stderr,
    )

    # Determine which groups to write files for.
    target_groups = list(args.target_groups)
    if not target_groups:
        target_groups = [args.source_group]
    if args.source_group not in target_groups:
        target_groups = [args.source_group] + target_groups

    for g in target_groups:
        out_path = f"{args.session}.chunks.{g}"
        write_chunks_file(
            path          = out_path,
            boundaries    = boundaries,
            session       = args.session,
            group         = g,
            source_group  = args.source_group,
            drift_file    = args.drift_file,
            thresh_um     = args.thresh_um,
            sampling_rate = args.sampling_rate,
        )
        print(f"  Wrote {out_path}  ({n_chunks} chunks)", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
