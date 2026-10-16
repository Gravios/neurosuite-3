#!/usr/bin/env python3
"""
process_setupgroups.py
======================
Populate anatomicalDescription.channelGroups and spikeDetection.channelGroups
in the session YAML from the probe library entries declared in probes:.

Algorithm
---------
For each entry in probes[]:

  1.  Locate the .probe file: an absolute probeFile is used as is; a
      relative one is looked up in these directories, first match wins
      (the order ndmanager's Probes tab uses):
        SESSION_DIR/                    (ndmanager's <session>.probe.<id>.probe)
        SESSION_DIR/probes/
        --probe-library argument
        probeLibraryPath                (session YAML, top level)
        $NEUROSUITE_PROBE_PATH          (colon-separated, in order)
        $NS3_PROBE_LIBRARY_PATH         (legacy name, colon-separated)
        $XDG_DATA_HOME/neurosuite/probes/        (~/.local/share/...)
        $XDG_DATA_DIRS/neurosuite/probes/        (/usr/local/share/..., /usr/share/...)

  2.  Extract from the .probe file (rule shared with the other plugins and
      Klusters, see probe_geometry.py):
        n_shanks          = probeFile.shanks.count
        count_per_shank   = probeFile.sites.count_per_shank (int, or a list
                            when shanks differ)
        channelMap.map    (optional)

      Sites are listed shank by shank in channel order (the library files list
      the top site first): site j belongs to channel offset + j, where
      offset = probes[].channelOffset -- an absolute channel, as every other
      reader (ndmanager, Klusters, the drift tools) takes it.  An entry
      without channelOffset follows the previous probe (its offset plus its
      channel count).

      If channelMap.map is provided (flat list, or one sublist per shank),
      site j belongs to channel offset + map[j]; its position is still
      geometry[j].

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

# Shared channel → site binding (probe_geometry.py, next to this script).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_geometry  # noqa: E402


# ---------------------------------------------------------------------------
# Probe library search
# ---------------------------------------------------------------------------

def data_dirs() -> list[Path]:
    """Generic data directories, as QStandardPaths::GenericDataLocation lists
    them on Linux: $XDG_DATA_HOME (~/.local/share), then $XDG_DATA_DIRS
    (/usr/local/share:/usr/share)."""
    home = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local/share")
    system = os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share"
    return [Path(home)] + [Path(p) for p in system.split(":") if p]


def probe_search_dirs(probe_library_arg: str | None, session_dir: Path,
                      doc_library: str | None = None) -> list[Path]:
    """Directories to search for a relative probeFile, first match wins."""
    dirs: list[Path] = [session_dir, session_dir / "probes"]
    if probe_library_arg:
        dirs.append(Path(probe_library_arg))
    if doc_library:
        dirs.append(Path(doc_library))
    for var in ("NEUROSUITE_PROBE_PATH", "NS3_PROBE_LIBRARY_PATH"):
        for p in os.environ.get(var, "").split(":"):
            if p:
                dirs.append(Path(p))
    dirs += [d / "neurosuite/probes" for d in data_dirs()]
    out: list[Path] = []
    for d in dirs:
        if d not in out:
            out.append(d)
    return out


def find_probe_file(probe_rel: str, search_dirs: list[Path]) -> Path | None:
    """Locate probe_rel: an absolute path as is, else the first of search_dirs
    that holds it."""
    if os.path.isabs(probe_rel):
        p = Path(probe_rel)
        return p if p.is_file() else None
    for d in search_dirs:
        candidate = d / probe_rel
        if candidate.is_file():
            return candidate
    return None


# ---------------------------------------------------------------------------
# Probe file parsing
# ---------------------------------------------------------------------------

def parse_probe(probe_path: Path,
                channel_offset: int) -> tuple[list[list[int]], list[list[list[float]]]]:
    """
    Return (shanks, geometries):
      shanks     : list of lists of physical (ADC) channel indices per shank,
                   in file order (top site first in the library files)
      geometries : list of lists of [x_um, y_um] per site per shank,
                   parallel to shanks.  None entries where geometry is absent.

    Site j of the file belongs to channel channel_offset + j, or
    channel_offset + channelMap.map[j] when a map is given; its position is
    sites.geometry[j] either way (probe_geometry.py).  count_per_shank may
    differ between shanks (a list).
    """
    with open(probe_path) as fh:
        pf = yaml.safe_load(fh)
    probe = pf.get("probeFile", {}) or {}
    shanks = probe_geometry.shank_channels(probe, channel_offset)
    geometries = [[list(p) if p is not None else None for p in sh]
                  for sh in probe_geometry.shank_positions(probe)]
    return shanks, geometries


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
                      n_features: int,
                      probe_id: int = -1,
                      shank_index: int = 0,
                      site_positions: list | None = None) -> dict:
    """Build a spikeDetection.channelGroups entry.

    probeId and shankIndex are written alongside the waveform parameters
    so that KlustaKwik, process_estimatedrift, and process_localise can
    resolve the electrode site geometry for this group without needing
    to cross-reference the anatomicalDescription section.
    """
    grp: dict = {
        "channels":        channels,
        "nSamples":        n_samples,
        "peakSampleIndex": peak_sample_index,
        "nFeatures":       n_features,
    }
    if probe_id >= 0:
        grp["probeId"]    = probe_id
        grp["shankIndex"] = shank_index
    if site_positions is not None:
        # Inline electrode geometry: [[x_um, y_um], ...] parallel to channels[].
        # None entries mark sites whose position is absent from the probe file.
        # Consumers (process_estimatedrift, process_localise, KlustaKwik) read this
        # field if present, falling back to the probe file for backward compat.
        grp["sitePositions_um"] = site_positions
    return grp


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
    session_dir  = param_file.resolve().parent

    with open(param_file) as fh:
        doc = yaml.safe_load(fh) or {}

    doc_library = doc.get("probeLibraryPath") or None
    if doc_library:
        doc_library = str(session_dir / os.path.expanduser(str(doc_library)))
    search_dirs  = probe_search_dirs(args.probe_library, session_dir, doc_library)

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

    running_offset = 0   # end of the preceding probe: its offset + its channel count

    for probe_entry in probes_list:
        probe_id     = int(probe_entry.get("probeId",
                           probe_entry.get("id", 0)))
        probe_rel    = str(probe_entry.get("probeFile") or "")
        # probes[].channelOffset is the probe's first channel, absolute -- the
        # value ndmanager writes and every other reader uses.  An entry
        # without one follows the preceding probe.
        stored_off   = probe_entry.get("channelOffset")
        channel_off  = int(stored_off) if stored_off is not None else running_offset

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

        # Normalise the key: always write 'probeId' (canonical) in the output
        # regardless of whether the input used 'id' or 'probeId'.
        probe_entry["probeId"] = probe_id
        probe_entry.pop("id", None)  # remove legacy 'id' key if present
        # Record the offset used, so every reader (which takes a missing
        # channelOffset as 0) binds the same channels.
        probe_entry["channelOffset"] = channel_off
        print(f"  probe {probe_id}: {probe_path}  channelOffset={channel_off}"
              + ("" if stored_off is not None else " (follows the previous probe)"))

        try:
            shanks, geometries = parse_probe(probe_path, channel_off)
        except Exception as exc:
            print(f"ERROR parsing {probe_path}: {exc}", file=sys.stderr)
            sys.exit(1)

        # The next probe without a channelOffset starts after this one's
        # highest channel.
        used = [c for s in shanks for c in s]
        running_offset = max(used) + 1 if used else channel_off

        assigned_group_ids: list[int] = []

        for shank_idx, channels in enumerate(shanks):
            gid = next_group_id
            next_group_id += 1
            assigned_group_ids.append(gid)

            print(f"    shank {shank_idx} → group {gid}  channels [{channels[0]}–{channels[-1]}]"
                  f"  ({len(channels)} ch)")

            anat_groups.append(build_anat_group(channels, probe_id, shank_idx))
            site_pos = geometries[shank_idx] if shank_idx < len(geometries) else None
            spike_groups.append(build_spike_group(
                channels,
                args.n_samples,
                args.peak_sample_index,
                args.n_features,
                probe_id=probe_id,
                shank_index=shank_idx,
                site_positions=site_pos,
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
