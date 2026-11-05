#!/usr/bin/env python3
"""
ndm_list.py — what a session holds, for `ndm list`

  ndm_list.py probes [session]       probes: entries, the file each one resolves to,
                                     its model, shanks and sites, and the groups on it
  ndm_list.py spikegroups [session]  spike groups: channels, window, features, the
                                     probe and shank, and where the site positions
                                     come from
  ndm_list.py programs [session]     the steps under programs:, with the values set

The session is the YAML given (with or without its extension), or else the one
named after the current folder, as for every ndm_* step.  Probe files are found
the way ndm_setupgroups finds them (session folder, probes/, probeLibraryPath,
$NEUROSUITE_PROBE_PATH, the data directories).

copyright (C) 2026 neurosuite-3 contributors  GPL-3.0-or-later
"""

from __future__ import annotations

import os
import signal
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_geometry  # noqa: E402
from process_setupgroups import find_probe_file, probe_search_dirs  # noqa: E402

import yaml  # noqa: E402


def die(msg: str, code: int = 2) -> None:
    print(f"ndm list: {msg}", file=sys.stderr)
    sys.exit(code)


def session_file(arg: str | None) -> Path:
    """ndm_resolve_session: the YAML given, else <folder>/<folder>.yaml."""
    if arg:
        base = arg
        for ext in (".yaml", ".yml", ".xml"):
            if base.endswith(ext):
                base = base[: -len(ext)]
        if os.path.isdir(arg):
            base = os.path.join(arg, os.path.basename(os.path.abspath(arg)))
        for ext in (".yaml", ".yml"):
            if os.path.isfile(base + ext):
                return Path(base + ext)
        if os.path.isfile(base + ".xml"):
            die(f"{base}.xml is a legacy XML parameter file: convert it with ndm xml2yaml")
        die(f"no {base}.yaml")
    folder = os.path.basename(os.getcwd())
    for ext in (".yaml", ".yml"):
        if os.path.isfile(folder + ext):
            return Path(folder + ext)
    die(f"no session given and no {folder}.yaml in this folder")
    raise AssertionError


def ranges(chans: list[int]) -> str:
    """0,1,2,3,7 -> 0-3,7"""
    out, i = [], 0
    while i < len(chans):
        j = i
        while j + 1 < len(chans) and chans[j + 1] == chans[j] + 1:
            j += 1
        out.append(str(chans[i]) if j == i else f"{chans[i]}-{chans[j]}")
        i = j + 1
    return ",".join(out)


def table(rows: list[list[str]], head: list[str]) -> str:
    w = [max(len(str(r[k])) for r in [head, *rows]) for k in range(len(head))]
    fmt = lambda r: "  " + "  ".join(str(c).ljust(w[k]) for k, c in enumerate(r)).rstrip()  # noqa: E731
    return "\n".join([fmt(head), fmt(["-" * x for x in w]), *map(fmt, rows)])


def load(path: Path) -> dict:
    try:
        with open(path) as fh:
            return yaml.safe_load(fh) or {}
    except yaml.YAMLError as e:
        die(f"{path}: not valid YAML ({e})", 1)
        raise


def probe_entries(doc: dict, sdir: Path) -> list[dict]:
    dirs = probe_search_dirs(None, sdir, doc.get("probeLibraryPath"))
    out = []
    for pe in doc.get("probes") or []:
        pid = pe.get("probeId", pe.get("id"))
        rel = str(pe.get("probeFile") or "")
        path = find_probe_file(rel, dirs) if rel else None
        probe = None
        if path is not None:
            try:
                probe = (yaml.safe_load(open(path)) or {}).get("probeFile")
            except (OSError, yaml.YAMLError):
                probe = None
        out.append(dict(id=pid, rel=rel, path=path, probe=probe, offset=int(pe.get("channelOffset", 0) or 0),
                        label=pe.get("label"), anat=pe.get("anatomicalGroups") or []))
    return out


def kind(probe: dict | None, shank: int) -> str:
    if not probe:
        return ""
    est = probe.get("estimate") or {}
    if isinstance(est, dict) and (shank in est or str(shank) in est):
        e = est.get(shank) or est.get(str(shank)) or {}
        return f"estimated {e.get('date', '')}".strip()
    if ((probe.get("substrate") or {}).get("material")) == "wire-bundle" and probe.get("vendor") != "estimated":
        return "template"
    return ""


def groups_of(doc: dict) -> list[dict]:
    return ((doc.get("spikeDetection") or {}).get("channelGroups")) or []


def list_probes(doc: dict, sfile: Path) -> None:
    entries = probe_entries(doc, sfile.parent)
    if not entries:
        print(f"{sfile.name}: no probes: entries"
              + (" (groups set by hand)" if groups_of(doc) else ""))
        return
    users: dict = {}
    for gi, g in enumerate(groups_of(doc), 1):
        if g.get("probeId") is not None:
            users.setdefault(g.get("probeId"), []).append(f"{gi}" + (f"/{g.get('shankIndex', 0)}"))
    rows = []
    for e in entries:
        pr = e["probe"] or {}
        model = " ".join(str(x) for x in (pr.get("vendor"), pr.get("model")) if x) or "-"
        nsh = int(((pr.get("shanks") or {}).get("count")) or 1) if pr else 0
        nsites = sum(probe_geometry.shank_counts(pr)) if pr else 0
        sites = pr.get("sites") or {}
        status = "not found" if e["path"] is None else ("unreadable" if not pr else "")
        flags = [k for k in {kind(pr, s) for s in range(nsh)} if k]
        if "geometry_backup" in sites:
            flags.append("geometry_backup")
        rows.append([str(e["id"]), e["rel"] or "-", str(e["offset"]),
                     status or os.path.relpath(e["path"], sfile.parent) if e["path"] else status,
                     model, str(nsh) if pr else "-", str(nsites) if pr else "-",
                     str(sites.get("layout") or "-"), str(((pr.get("substrate") or {}).get("material")) or "-"),
                     ", ".join(users.get(e["id"], []))
                     or ("anatomical " + ranges(sorted(int(a) for a in e["anat"])) if e["anat"] else "-"),
                     "; ".join(flags)])
    print(f"{sfile.name}: {len(entries)} probe(s)\n")
    print(table(rows, ["id", "probeFile", "offset", "found at", "model", "shanks", "sites", "layout",
                       "material", "groups/shank", "notes"]))


def list_spikegroups(doc: dict, sfile: Path) -> None:
    gs = groups_of(doc)
    if not gs:
        print(f"{sfile.name}: no spike groups (spikeDetection.channelGroups); ndm setupgroups builds them")
        return
    probes = {e["id"]: e for e in probe_entries(doc, sfile.parent)}
    rows = []
    for gi, g in enumerate(gs, 1):
        chans = probe_geometry.group_channels(doc, gi)
        pid, sh = g.get("probeId"), int(g.get("shankIndex", 0) or 0)
        inline = g.get("sitePositions_um")
        e = probes.get(pid) if pid is not None else None
        if inline and all(p is not None for p in inline) and len(inline) == len(chans):
            geom = "sitePositions_um"
        elif e is None:
            geom = "none" if pid is None else f"probeId {pid} not in probes:"
        elif e["probe"] is None:
            geom = f"{e['rel']} not found"
        else:
            pos = probe_geometry.positions_for_channels(e["probe"], e["offset"], chans)
            missing = sum(p is None for p in pos)
            geom = "probe file" if not missing else f"probe file, {missing} channel(s) without a site"
        k = kind(e["probe"], sh) if e else ""
        if k:
            geom += f" ({k})"
        skip = [c["id"] for c in (g.get("channels") or []) if isinstance(c, dict) and c.get("skip")]
        rows.append([str(gi), ranges(chans), str(len(chans)), str(g.get("nSamples", "-")),
                     str(g.get("peakSampleIndex", "-")), str(g.get("nFeatures", "-")),
                     "-" if pid is None else f"{pid}/{sh}", geom,
                     ",".join(map(str, skip)) or "-"])
    print(f"{sfile.name}: {len(gs)} spike group(s)\n")
    print(table(rows, ["group", "channels", "n", "nSamples", "peak", "nFeatures", "probe/shank",
                       "site positions", "skipped"]))


def list_programs(doc: dict, sfile: Path) -> None:
    progs = doc.get("programs") or []
    if not progs:
        print(f"{sfile.name}: no programs: entries")
        return
    print(f"{sfile.name}: {len(progs)} step(s) under programs:\n")
    for p in progs:
        name = p.get("name", "?")
        params = [(q.get("name"), q.get("value")) for q in (p.get("parameters") or []) if isinstance(q, dict)]
        set_ = [(n, v) for n, v in params if v not in (None, "", "~")]
        print(f"  {name}" + (f"  ({len(set_)} of {len(params)} parameters set)" if params else ""))
        for n, v in set_:
            v = str(v).replace("\n", " ")
            print(f"      {n} = {v if len(v) <= 70 else v[:67] + '...'}")


def main(argv: list[str]) -> int:
    if hasattr(signal, "SIGPIPE"):                       # quiet under `| head`
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__.split("copyright")[0].rstrip())
        return 0
    what, rest = argv[0], argv[1:]
    if len(rest) > 1:
        die(f"usage: ndm list {what} [session]")
    sfile = session_file(rest[0] if rest else None)
    doc = load(sfile)
    {"probes": list_probes, "spikegroups": list_spikegroups, "programs": list_programs}.get(
        what, lambda d, f: die(f"cannot list '{what}': probes, spikegroups, programs or steps"))(doc, sfile)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
