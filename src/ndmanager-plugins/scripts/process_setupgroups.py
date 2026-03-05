#!/usr/bin/env python3
"""
process_setupgroups.py
======================
Populate anatomicalDescription.channelGroups and spikeDetection.channelGroups
in the session YAML from the probe library entries declared in probes:.

Algorithm
---------
For each entry in probes[]:

  1.  Locate the .probe file by searching (lowest → highest priority):
        /usr/share/neurosuite/probes/
        /usr/local/share/neurosuite/probes/
        ~/.local/share/neurosuite/probes/
        $NEUROSUITE_PROBE_PATH          (colon-separated)
        --probe-library argument
        SESSION_DIR/probes/

  2.  Extract from the .probe file:
        n_shanks          = probeFile.shanks.count
        n_per_shank       = probeFile.sites.count_per_shank
        channelMap.map    (optional list of n_shanks sublists)

      If channelMap.map is null, channels are assigned sequentially:
        shank i → [offset + i*n, …, offset + (i+1)*n − 1]
      where offset = probes[].channelOffset.

      If channelMap.map is provided it is a flat list of n_shanks × n_per_shank
      hardware channel indices in shank-major order; channelOffset is added to
      each entry.

  3.  Assign group IDs starting from 1 (or from the highest existing group ID
      + 1 if --overwrite false and groups already exist).

      Record probeId / shankIndex metadata on each group.

  4.  Build the anatomicalDescription.channelGroups sequence:
        - channels: [{id: <ch>, skip: 0}, ...]
          probeId: <probe.id>
          shankIndex: <i>

  5.  Build the spikeDetection.channelGroups sequence mirroring anat:
        - channels: [<ch>, ...]          # plain ints, same order
          nSamples: <param>
          peakSampleIndex: <param>
          nFeatures: <param>

  6.  Update probes[].anatomicalGroups with the new group ID list.

  7.  Write the updated document back to the YAML file (in-place).

Existing groups are removed and replaced when --overwrite true (default: false).
If --overwrite false and groups already exist the script aborts with a clear
error message.
"""

import argparse
import os
import sys
from pathlib import Path

import yaml


# ---------------------------------------------------------------------------
# Probe library search
# ---------------------------------------------------------------------------

SYSTEM_PROBE_DIRS = [
    Path("/usr/share/neurosuite/probes"),
    Path("/usr/local/share/neurosuite/probes"),
    Path.home() / ".local/share/neurosuite/probes",
]


def probe_search_dirs(probe_library_arg: str | None, session_dir: Path) -> list[Path]:
    """Return ordered list of directories to search for .probe files."""
    dirs: list[Path] = list(SYSTEM_PROBE_DIRS)

    env_path = os.environ.get("NEUROSUITE_PROBE_PATH", "")
    if env_path:
        for p in env_path.split(":"):
            if p:
                dirs.append(Path(p))

    if probe_library_arg:
        dirs.append(Path(probe_library_arg))

    dirs.append(session_dir / "probes")
    return dirs


def find_probe_file(probe_rel: str, search_dirs: list[Path]) -> Path | None:
    """Locate probe_rel in search_dirs (highest-priority last wins)."""
    found = None
    for d in search_dirs:
        candidate = d / probe_rel
        if candidate.exists():
            found = candidate
    return found


# ---------------------------------------------------------------------------
# Probe file parsing
# ---------------------------------------------------------------------------

def parse_probe(probe_path: Path, channel_offset: int) -> list[list[int]]:
    """
    Return a list-of-lists: one sublist per shank, each containing the
    physical (ADC) channel indices for that shank after applying channel_offset.
    """
    with open(probe_path) as fh:
        pf = yaml.safe_load(fh)

    root = pf.get("probeFile", {})

    shanks_cfg = root.get("shanks", {})
    sites_cfg  = root.get("sites",  {})

    n_shanks     = int(shanks_cfg.get("count",          1))
    n_per_shank  = int(sites_cfg.get( "count_per_shank", 1))

    ch_map_cfg = root.get("channelMap", {}) or {}
    explicit_map = ch_map_cfg.get("map")  # None or list

    shanks: list[list[int]] = []

    if explicit_map and isinstance(explicit_map, list):
        # Expect n_shanks sublists each of length n_per_shank
        # Also accept a flat list of n_shanks*n_per_shank entries
        if isinstance(explicit_map[0], (int, float)):
            # flat → reshape
            flat = [int(c) for c in explicit_map]
            if len(flat) != n_shanks * n_per_shank:
                raise ValueError(
                    f"{probe_path}: channelMap.map length {len(flat)} "
                    f"!= n_shanks({n_shanks}) × n_per_shank({n_per_shank})"
                )
            for i in range(n_shanks):
                shanks.append([channel_offset + flat[i * n_per_shank + j]
                                for j in range(n_per_shank)])
        else:
            # list of sublists
            for sub in explicit_map:
                shanks.append([channel_offset + int(c) for c in sub])
    else:
        # Sequential assignment
        for i in range(n_shanks):
            base = channel_offset + i * n_per_shank
            shanks.append(list(range(base, base + n_per_shank)))

    return shanks


# ---------------------------------------------------------------------------
# YAML in-place update helpers
# ---------------------------------------------------------------------------

def build_anat_group(channels: list[int], probe_id: int, shank_index: int) -> dict:
    return {
        "channels": [{"id": ch, "skip": 0} for ch in channels],
        "probeId":    probe_id,
        "shankIndex": shank_index,
    }


def build_spike_group(channels: list[int],
                      n_samples: int,
                      peak_sample_index: int,
                      n_features: int) -> dict:
    return {
        "channels":        channels,
        "nSamples":        n_samples,
        "peakSampleIndex": peak_sample_index,
        "nFeatures":       n_features,
    }


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description="Populate channel groups from probe library")
    ap.add_argument("--param-file",        required=True)
    ap.add_argument("--n-samples",         type=int,   default=52)
    ap.add_argument("--peak-sample-index", type=int,   default=26)
    ap.add_argument("--n-features",        type=int,   default=3)
    ap.add_argument("--probe-library",     default=None)
    ap.add_argument("--overwrite",         default="false")
    args = ap.parse_args()

    overwrite = args.overwrite.lower() in ("true", "1", "yes")

    param_file   = Path(args.param_file)
    session_dir  = param_file.parent
    search_dirs  = probe_search_dirs(args.probe_library, session_dir)

    with open(param_file) as fh:
        doc = yaml.safe_load(fh) or {}

    # ------------------------------------------------------------------
    # Guard: refuse to clobber existing groups unless --overwrite
    # ------------------------------------------------------------------
    existing_anat = (doc.get("anatomicalDescription") or {}).get("channelGroups") or []
    existing_spk  = (doc.get("spikeDetection") or {}).get("channelGroups") or []
    if (existing_anat or existing_spk) and not overwrite:
        print(
            "ERROR: anatomicalDescription and/or spikeDetection groups already exist.\n"
            "       Re-run with overwrite: true to replace them.",
            file=sys.stderr,
        )
        sys.exit(1)

    probes_list = doc.get("probes") or []
    if not probes_list:
        print("ERROR: No probes: entries found in the parameter file.", file=sys.stderr)
        sys.exit(1)

    # ------------------------------------------------------------------
    # Build groups
    # ------------------------------------------------------------------
    anat_groups:  list[dict] = []
    spike_groups: list[dict] = []
    next_group_id = 1

    for probe_entry in probes_list:
        probe_id     = int(probe_entry.get("id", 0))
        probe_rel    = str(probe_entry.get("probeFile") or "")
        channel_off  = int(probe_entry.get("channelOffset", 0))

        if not probe_rel:
            print(f"WARNING: probe entry id={probe_id} has no probeFile, skipping.",
                  file=sys.stderr)
            continue

        probe_path = find_probe_file(probe_rel, search_dirs)
        if probe_path is None:
            print(
                f"ERROR: probe file '{probe_rel}' not found.\n"
                f"       Searched: {[str(d) for d in search_dirs]}",
                file=sys.stderr,
            )
            sys.exit(1)

        print(f"  probe {probe_id}: {probe_path.name}  offset={channel_off}")

        try:
            shanks = parse_probe(probe_path, channel_off)
        except Exception as exc:
            print(f"ERROR parsing {probe_path}: {exc}", file=sys.stderr)
            sys.exit(1)

        assigned_group_ids: list[int] = []

        for shank_idx, channels in enumerate(shanks):
            gid = next_group_id
            next_group_id += 1
            assigned_group_ids.append(gid)

            print(f"    shank {shank_idx} → group {gid}  channels [{channels[0]}–{channels[-1]}]"
                  f"  ({len(channels)} ch)")

            anat_groups.append(build_anat_group(channels, probe_id, shank_idx))
            spike_groups.append(build_spike_group(
                channels,
                args.n_samples,
                args.peak_sample_index,
                args.n_features,
            ))

        # Update probes[].anatomicalGroups in-place
        probe_entry["anatomicalGroups"] = assigned_group_ids

    if not anat_groups:
        print("ERROR: No groups were generated — check probe entries.", file=sys.stderr)
        sys.exit(1)

    # ------------------------------------------------------------------
    # Write back into doc
    # ------------------------------------------------------------------
    if "anatomicalDescription" not in doc or doc["anatomicalDescription"] is None:
        doc["anatomicalDescription"] = {}
    doc["anatomicalDescription"]["channelGroups"] = anat_groups

    if "spikeDetection" not in doc or doc["spikeDetection"] is None:
        doc["spikeDetection"] = {}
    doc["spikeDetection"]["channelGroups"] = spike_groups

    # ------------------------------------------------------------------
    # Dump back preserving structure as closely as possible
    # ------------------------------------------------------------------
    with open(param_file, "w") as fh:
        yaml.dump(
            doc,
            fh,
            default_flow_style=False,
            allow_unicode=True,
            sort_keys=False,
        )

    n = len(anat_groups)
    print(f"  Wrote {n} anatomical + {n} spikeDetection groups to {param_file.name}")


if __name__ == "__main__":
    main()
