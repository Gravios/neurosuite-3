#!/usr/bin/env python3
"""
neuronexus_probes.py — regenerate src/probes/neuronexus/*.probe

Every NeuroNexus design in the library is described here by a few numbers
read off its page of the NeuroNexus "Penetrating Probes" catalogue (August
2025 edition; the PDF page number equals the printed page number), and the
site geometry is generated from them, so the files cannot drift apart from
each other or from the catalogue.

Convention of the generated files (doc/ndmanager-plugins/formats/probe.md):

  - sites.geometry lists the sites shank by shank, left to right, and within
    a shank TOP SITE FIRST, TIP SITE LAST (by height, then left to right).
    Site j belongs to channel channelOffset + j, so on every shank the
    channel numbers ascend as the sites go deeper.  The recording's channels
    are assumed to be in this order already (remapped from the connector).
  - y = height above the shank's tip apex in µm; x = lateral offset from the
    tip axis, plus shank_index × shanks.spacing_um.
  - count_per_shank is a list when the shanks differ (Buzsaki64sp, 5x12).

Where the catalogue gives a dimension label, the label is used (the
drawings are to scale between sites but not always between the tip and the
first site).  Lateral offsets inside an octrode / staggered cluster are not
labelled; they were measured from the drawings (scale from the labelled
vertical step) and are marked "measured" in the notes.

Usage:
    neuronexus_probes.py [OUTDIR]       (default: ../src/probes/neuronexus)
    neuronexus_probes.py --check DIR    exit 1 if DIR differs from the table

copyright (C) 2026 neurosuite-3 contributors  GPL-3.0-or-later
"""

from __future__ import annotations

import argparse
import os
import sys
from dataclasses import dataclass, field
from typing import Callable, Optional

CATALOGUE = "NeuroNexus Penetrating Probes catalogue, August 2025"

# ── Shank cluster builders: list of (x, y) for ONE shank, any order ─────────


def linear(n: int, pitch: float, tip: float) -> Callable[[int], list]:
    return lambda k: [(0.0, tip + pitch * i) for i in range(n)]


def poly2(n: int, step: float, tip: float, half_gap: float, lowest_left: bool):
    """Two columns ±half_gap, consecutive sites alternate columns, each
    `step` above the previous one."""
    def build(k):
        s = -1.0 if lowest_left else 1.0
        return [(s * half_gap * (1 if i % 2 == 0 else -1), tip + step * i) for i in range(n)]
    return build


def tetrodes(centres: list[float], r: float = 17.7):
    """Diamond tetrodes: one site above and below the centre, one each side."""
    def build(k):
        out = []
        for c in centres:
            out += [(0.0, c + r), (-r, c), (r, c), (0.0, c - r)]
        return out
    return build


def cluster(xs: list[float], tip: float, step: float, extra: Optional[dict] = None):
    """Staggered cluster: site i (from the tip) at (xs[i], tip + step*i);
    extra = {shank_index: [(x, y), …]} adds sparse sites to given shanks."""
    def build(k):
        out = [(x, tip + step * i) for i, x in enumerate(xs)]
        return out + list((extra or {}).get(k, []))
    return build


# Octrode used on Buzsaki16/32/32L/64/64L (p33, 70, 71, 101, 104): 8 sites
# 20 µm apart vertically, lowest 22 µm above the apex; lateral offsets
# measured, bottom to top.
OCTRODE_X = [0.0, -8.6, 8.6, -12.7, 12.7, -16.6, 16.6, -20.7]
# "sp" designs (p73, p103, p105): narrower cluster, same vertical pattern.
SP_X = [0.0, -7.2, 7.2, -9.7, 9.7, -12.3, 12.3, -14.8, 14.8, -17.3]
# Sparse sites on the 4th shank of Buzsaki64sp / 64-sp-100 (labelled 500 µm
# apart, measured 497 / 993 / 1491 / 1989 µm above the apex).
SP_SPARSE = {3: [(0.0, 502.0), (0.0, 1002.0), (0.0, 1502.0), (0.0, 2002.0)]}
# Buzsaki5x12 (p102): 12 sites alternating right / left, 10 µm apart
# vertically, lowest 35 µm above the apex (right); the centre shank adds 4
# sparse sites 200 µm apart.
X5X12 = [7.4 if i % 2 == 0 else -10.0 for i in range(12)]
SPARSE_5X12 = {2: [(0.0, 345.0), (0.0, 545.0), (0.0, 745.0), (0.0, 945.0)]}


@dataclass
class Design:
    model: str
    page: int
    shanks: int
    spacing: Optional[float]      # centre-to-centre, µm
    length_mm: float
    layout: str
    area: float                   # µm²
    pitch: Optional[float]        # sites.spacing_um (vertical step), None for clusters
    build: Callable[[int], list]
    note: str
    thickness: float = 15.0
    thickness_note: str = ""
    counts: Optional[list[int]] = field(default=None)  # filled in by geometry()


THICK_BOTH = "Also made 50 µm thick (p{p})."
THICK_50 = ""
THICK_NONE = "Thickness not stated on p{p}; 15 µm assumed."

D: list[Design] = []


def add(*a, **kw):
    D.append(Design(*a, **kw))


EDGE_NOTE = (" Edge sites sit 3–7 µm off the tip axis toward the shank edge in the"
             " drawing; modelled on the axis.")

# Linear — model, page, shanks, spacing, length, layout, area, pitch, builder, note
for m, p, ns, sp, L, n, pitch, tip, area, th, tn in [
    ("A1x16-10mm-100-177",     21, 1, None, 10, 16, 100, 50, 177, 15, "both"),
    ("A1x16-3mm-25-177",       13, 1, None,  3, 16,  25, 50, 177, 15, "both"),
    ("A1x16-5mm-25-177",       16, 1, None,  5, 16,  25, 50, 177, 15, "both"),
    ("A1x16-5mm-50-177",       17, 1, None,  5, 16,  50, 50, 177, 15, "both"),
    ("A1x32-10mm-100-177",     40, 1, None, 10, 32, 100, 50, 177, 50, ""),
    ("A1x32-5mm-25-177",       34, 1, None,  5, 32,  25, 50, 177, 15, "both"),
    ("A1x32-5mm-50-177",       35, 1, None,  5, 32,  50, 50, 177, 15, "both"),
    ("A1x32-6mm-50-177",       36, 1, None,  6, 32,  50, 50, 177, 15, "both"),
    ("A2x16-10mm-50-500-177",  56, 2,  500, 10, 16,  50, 50, 177, 15, "both"),
    ("A2x32-5mm-100-200-177",  80, 2,  200,  5, 32, 100, 50, 177, 15, ""),
    ("A2x32-5mm-25-200-177",   79, 2,  200,  5, 32,  25, 50, 177, 15, "none"),
    ("A2x8-5mm-50-200-703",    25, 2,  200,  5,  8,  50, 50, 703, 15, ""),
    ("A4x16-3mm-50-200-177",   88, 4,  200,  3, 16,  50, 50, 177, 15, "none"),
    ("A4x32-12mm-100-300-177", 114, 4, 300, 12, 32, 100, 50, 177, 50, ""),
    ("A4x32-5mm-100-400-1250", 113, 4, 400,  5, 32, 100, 75, 1250, 15, ""),
    ("A4x4-3mm-100-125-177",   29, 4,  125,  3,  4, 100, 50, 177, 15, "both"),
    ("A4x4-3mm-50-125-177",    28, 4,  125,  3,  4,  50, 50, 177, 15, ""),
    ("A4x8-5mm-100-200-177",   64, 4,  200,  5,  8, 100, 50, 177, 15, "both"),
    ("A4x8-5mm-50-200-177",    62, 4,  200,  5,  8,  50, 50, 177, 15, ""),
    ("A4x8-7mm-100-200-177",   68, 4,  200,  7,  8, 100, 50, 177, 50, ""),
    ("A8x8-10mm-200-200-177",  98, 8,  200, 10,  8, 200, 50, 177, 50, ""),
    ("A8x8-5mm-200-200-177",   97, 8,  200,  5,  8, 200, 50, 177, 15, "both"),
    ("A8x32-5mm-35-300-160",   127, 8, 300,  5, 32,  35, 22, 160, 15, ""),
]:
    note = (f"{ns} shank{'s' * (ns > 1)}{f' {sp} µm apart' if sp else ''}, {n} sites each"
            f" in one column, {pitch} µm pitch, lowest site {tip} µm above the tip.")
    if m == "A4x8-5mm-100-200-177":
        note += " (The panel captions on p64 read 50-400; the drawing and title are 100-200.)"
    if m == "A8x32-5mm-35-300-160":
        note += " Sites 11 × 15 µm."
    add(m, p, ns, sp, L, "linear", area, pitch, linear(n, pitch, tip), note,
        thickness=th, thickness_note=tn)

for m, p, ns, sp, L, n, pitch, tip, tn in [
    ("A1x32-Edge-5mm-50-177",     42, 1, None, 5, 32, 50, 50, ""),
    ("A1x64-Edge-6mm-20-177",     77, 1, None, 6, 64, 20, 40, ""),
    ("A8x16-Edge-5mm-50-150-177", 121, 8, 150, 5, 16, 50, 50, ""),
    ("A8x32-Edge-5mm-25-200-177", 129, 8, 200, 5, 32, 25, 50, ""),
    ("A8x8-Edge-5mm-50-150-177",  99, 8, 150, 5, 8, 50, 50, ""),
]:
    note = (f"{ns} shank{'s' * (ns > 1)}{f' {sp} µm apart' if sp else ''}, {n} sites each"
            f" along the shank edge, {pitch} µm pitch, lowest site {tip} µm above the tip."
            + EDGE_NOTE)
    add(m, p, ns, sp, L, "edge", 177, pitch, linear(n, pitch, tip), note, thickness_note=tn)

add("A1x16-Poly2-5mm-50s-177", 23, 1, None, 5, "poly2", 177, 25,
    poly2(16, 25, 75, 21.65, lowest_left=True),
    "Two columns 43.3 µm apart (50 µm diagonal); consecutive sites alternate"
    " columns, 25 µm higher each time; lowest site (left column) 75 µm above the tip.",
    thickness_note="both")
add("A1x32-Poly2-5mm-50s-177", 47, 1, None, 5, "poly2", 177, 25,
    poly2(32, 25, 75, 21.65, lowest_left=True),
    "Two columns 43.3 µm apart (50 µm diagonal); consecutive sites alternate"
    " columns, 25 µm higher each time; lowest site (left column) 75 µm above the tip.",
    thickness_note="both")
add("A4x32-Poly2-5mm-23s-200-177", 115, 4, 200, 5, "poly2", 177, 23,
    poly2(32, 23, 50, 15.0, lowest_left=False),
    "4 shanks 200 µm apart. Two columns 30 µm apart; consecutive sites alternate"
    " columns, 23 µm higher each time; lowest site (right column) 50 µm above the tip.")

add("A2x2-tet-3mm-150-150-121", 24, 2, 150, 3, "tetrode", 121, None,
    tetrodes([78.0, 228.0]),
    "2 shanks 150 µm apart, 2 diamond tetrodes per shank, centres 78 and 228 µm"
    " above the tip (labelled 78; the drawing shows ~68). Sites 11 × 11 µm,"
    " 25 µm apart within a tetrode (measured).", thickness_note="both")
add("A4x1-tet-3mm-150-121", 26, 4, 150, 3, "tetrode", 121, None,
    tetrodes([75.0]),
    "4 shanks 150 µm apart, one diamond tetrode per shank centred 75 µm above"
    " the tip. Sites 11 × 11 µm, 25 µm apart (measured).", thickness_note="both")

OCT = ("8 staggered sites per shank, 20 µm apart vertically, lowest 22 µm above"
       " the tip; lateral offsets measured from the drawing. Sites 11 × 15 µm.")
for m, p, ns, L, tn in [("Buzsaki16", 33, 2, 5, ""), ("Buzsaki32", 70, 4, 5, ""),
                        ("Buzsaki32L", 71, 4, 10, "both"), ("Buzsaki64", 101, 8, 5, ""),
                        ("Buzsaki64L", 104, 8, 10, "both")]:
    add(m, p, ns, 200, L, "octrode", 160, None, cluster(OCTRODE_X, 22, 20),
        f"{ns} shanks 200 µm apart. " + OCT, thickness_note=tn)
add("Buzsaki32sp", 73, 4, 200, 5, "octrode", 160, None, cluster(SP_X[:8], 22, 20),
    "4 shanks 200 µm apart. 8 staggered sites per shank in a narrower cluster,"
    " 20 µm apart vertically, lowest 22 µm above the tip; lateral offsets measured.")
for m, p, sp in [("Buzsaki64sp", 105, 200), ("Buzsaki64-sp-100", 103, 100)]:
    add(m, p, 6, sp, 5, "octrode", 160, None, cluster(SP_X, 22, 20, SP_SPARSE),
        f"6 shanks {sp} µm apart, 10 staggered sites each, 20 µm apart vertically,"
        " lowest 22 µm above the tip (lateral offsets measured); the 4th shank also"
        " carries 4 single sites 500 µm apart, 502–2002 µm above its tip.")
add("Buzsaki5x12", 102, 5, 200, 5, "octrode", 160, None,
    cluster(X5X12, 35, 10, SPARSE_5X12),
    "5 shanks 200 µm apart, 12 sites each alternating right / left of the axis,"
    " 10 µm apart vertically, lowest (right) 35 µm above the tip (lateral offsets"
    " measured); the centre shank also carries 4 single sites 200 µm apart,"
    " 345–945 µm above its tip.")
add("Buzsaki256", 136, 8, 300, 5, "linear", 160, 50, linear(32, 50, 6),
    "8 shanks 300 µm apart, 32 sites each in one column, 50 µm pitch; lowest site"
    " ~6 µm above the tip (measured, not labelled). The page warns up to 15% of"
    " sites may be irregular on an assembled device.")


# ── Output ────────────────────────────────────────────────────────────────────


def _num(v: float) -> str:
    v = round(float(v), 2)
    return str(int(v)) if v == int(v) else f"{v:g}"


def geometry(d: Design) -> list[list[tuple[float, float]]]:
    shanks = []
    for k in range(d.shanks):
        pts = sorted(d.build(k), key=lambda p: (-p[1], p[0]))     # top first
        off = (d.spacing or 0.0) * k
        shanks.append([(x + off, y) for x, y in pts])
    d.counts = [len(s) for s in shanks]
    return shanks


def render(d: Design) -> str:
    shanks = geometry(d)
    total = sum(d.counts)
    cps = d.counts[0] if len(set(d.counts)) == 1 else "[" + ", ".join(map(str, d.counts)) + "]"
    tn = {"both": THICK_BOTH, "none": THICK_NONE, "": ""}[d.thickness_note].format(p=d.page)
    notes = d.note + (" " + tn if tn else "")
    lines = [
        f"# NeuroNexus {d.model} — neurosuite-3 probe configuration",
        f"# {CATALOGUE}, page {d.page}.",
        "# Generated by src/nphys-data/tools/neuronexus_probes.py — edit the table there.",
        "#   geometry: [x_um, y_um] per site, shank by shank, TOP SITE FIRST;",
        "#   y = height above the shank tip, x = offset from the tip axis",
        "#   + shank index × shanks.spacing_um.  Site j is channel channelOffset + j,",
        "#   so channel numbers ascend with depth on every shank.",
        "probeFile:",
        "  version: '1.0'",
        "  vendor: NeuroNexus",
        f"  model: {d.model}",
        f"  catalogPage: {d.page}",
        f"  totalChannels: {total}",
        "  substrate:",
        "    material: silicon",
        f"    thickness_um: {_num(d.thickness)}",
        "  shanks:",
        f"    count: {d.shanks}",
        f"    spacing_um: {_num(d.spacing) if d.spacing else 'null'}",
        f"    length_mm: {float(d.length_mm)}",
        "  sites:",
        f"    count_per_shank: {cps}",
        f"    layout: {d.layout}",
        f"    area_um2: {_num(d.area)}",
        f"    spacing_um: {_num(d.pitch) if d.pitch else 'null'}",
        "    geometry:",
    ]
    for k, sh in enumerate(shanks):
        lines.append(f"    # shank {k}")
        lines += [f"    - [{_num(x)}, {_num(y)}]" for x, y in sh]
    lines += [
        "  channelMap:",
        "    description: null",
        "    map: null",
        f"  notes: '{notes.replace(chr(39), chr(39) * 2)}'",
        "",
    ]
    return "\n".join(lines)


def main(argv=None) -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("outdir", nargs="?", default=os.path.join(here, "..", "src", "probes", "neuronexus"))
    ap.add_argument("--check", action="store_true", help="compare instead of writing")
    a = ap.parse_args(argv)
    bad = 0
    for d in D:
        text = render(d)
        path = os.path.join(a.outdir, d.model + ".probe")
        if a.check:
            try:
                same = open(path, encoding="utf-8").read() == text
            except OSError:
                same = False
            if not same:
                print(f"differs: {path}")
                bad += 1
        else:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
    if not a.check:
        print(f"wrote {len(D)} files to {a.outdir}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
