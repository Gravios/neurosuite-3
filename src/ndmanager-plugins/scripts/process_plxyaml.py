#!/usr/bin/env python3
"""
process_plxyaml.py — merge process_plxconvert metadata into the session YAML

Reads the ``sourceRecording`` block that process_plxconvert writes to
``<base>.plexon.yaml`` and merges it into the session parameter file:

  - acquisitionSystem   set from sourceRecording.export.acquisitionSystem
                        (nBits, nChannels, samplingRate, voltageRange,
                        amplification, offset, bitResolution_uV, nSamples,
                        duration_s); every change is printed
  - generalInfo         date (from the recording date) and description (from
                        the Plexon file comment), only when still empty
  - files               one {samplingRate, extension} entry per extra stream
                        file NeuroScope can read (.pxai, .pxfp, …); an entry
                        with the same extension is updated, others are kept
  - sourceRecording     the whole block, replacing an earlier one

With ruamel.yaml installed the session YAML keeps its comments and layout;
otherwise PyYAML rewrites it (comments are lost, and the script says so).
The file is replaced atomically.

Usage:
    process_plxyaml.py --param-file SESSION.yaml --meta SESSION.plexon.yaml

copyright (C) 2026 neurosuite-3 contributors  GPL-3.0-or-later
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

import yaml


def _backend():
    try:
        from ruamel.yaml import YAML  # comment-preserving round trip
        y = YAML()
        y.preserve_quotes = True
        y.indent(mapping=2, sequence=4, offset=2)
        y.width = 4096
        return "ruamel", y
    except ImportError:
        return "pyyaml", yaml


def _load(kind, y, path: Path):
    with open(path) as fh:
        doc = y.load(fh) if kind == "ruamel" else y.safe_load(fh)
    return doc if doc is not None else {}


def _dump(kind, y, doc, path: Path) -> None:
    tmp = path.with_name(path.name + ".nstmp")
    with open(tmp, "w") as fh:
        if kind == "ruamel":
            y.dump(doc, fh)
        else:
            y.safe_dump(doc, fh, default_flow_style=False, sort_keys=False, allow_unicode=True)
    os.replace(tmp, path)


def merge(param_file: Path, meta_file: Path) -> list[str]:
    with open(meta_file) as fh:
        meta = (yaml.safe_load(fh) or {}).get("sourceRecording")
    if not isinstance(meta, dict):
        raise SystemExit(f"error: {meta_file} holds no sourceRecording block")
    acq_new = (meta.get("export") or {}).get("acquisitionSystem") or {}

    kind, y = _backend()
    doc = _load(kind, y, param_file)
    changes = []

    acq = doc.get("acquisitionSystem")
    if not isinstance(acq, dict):
        acq = {}
        doc["acquisitionSystem"] = acq
    for k, v in acq_new.items():
        if v is None:
            continue
        if acq.get(k) != v:
            changes.append(f"acquisitionSystem.{k}: {acq.get(k)} -> {v}")
            acq[k] = v

    gi = doc.get("generalInfo")
    if not isinstance(gi, dict):
        gi = {}
        doc["generalInfo"] = gi
    hdr = meta.get("header") or {}
    fill = {"date": (hdr.get("recordedAt") or "")[:10] or None,
            "description": hdr.get("comment") or None}
    for k, v in fill.items():
        # "~" is how ndmanager builds before the null fix wrote an empty field
        if v and (not gi.get(k) or gi.get(k) == "~"):
            gi[k] = v
            changes.append(f"generalInfo.{k}: {v}")

    files_new = (meta.get("export") or {}).get("sessionFiles") or []
    if files_new:
        files = doc.get("files")
        if not isinstance(files, list):
            files = []
            doc["files"] = files
        for entry in files_new:
            ext, rate = entry.get("extension"), entry.get("samplingRate")
            hit = next((f for f in files if isinstance(f, dict) and f.get("extension") == ext), None)
            if hit is None:
                files.append({"samplingRate": rate, "extension": ext})
                changes.append(f"files: + {ext} @ {rate} Hz")
            elif hit.get("samplingRate") != rate:
                changes.append(f"files: {ext} samplingRate {hit.get('samplingRate')} -> {rate}")
                hit["samplingRate"] = rate

    doc["sourceRecording"] = meta
    changes.append("sourceRecording: written")
    _dump(kind, y, doc, param_file)
    if kind != "ruamel":
        changes.append("note: ruamel.yaml not installed — comments in the YAML were not preserved")
    return changes


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--param-file", required=True, type=Path)
    ap.add_argument("--meta", required=True, type=Path)
    a = ap.parse_args(argv)
    for p in (a.param_file, a.meta):
        if not p.is_file():
            print(f"error: {p} not found", file=sys.stderr)
            return 2
    for line in merge(a.param_file, a.meta):
        print(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
