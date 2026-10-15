"""
probe_geometry.py — site positions of a .probe file, bound to channels

One rule, shared by ndm_setupgroups, ndm_estimatedrift, ndm_driftcorrect and
ndm_localise (and the same rule Klusters' driftgeometry.cpp applies):

  probeFile.sites.geometry lists every site as [x_um, y_um], shank by shank,
  in CHANNEL ORDER: site j belongs to channel  channelOffset + j,  or, when
  probeFile.channelMap.map is a flat list, to  channelOffset + map[j].

  y is the height above the shank tip (µm); x is lateral (µm), shank k
  centred near k × shanks.spacing_um.  Within a shank the library files list
  the top site first and the tip site last, so channel numbers ascend as
  depth increases (doc/ndmanager-plugins/formats/probe.md).

  sites.count_per_shank is one integer (every shank alike) or a list with one
  entry per shank (designs whose shanks differ, e.g. Buzsaki64sp).

A position is looked up by channel NUMBER, never by where a channel happens to
sit in a group's list, so a group may list its channels in any order.

copyright (C) 2026 neurosuite-3 contributors  GPL-3.0-or-later
"""

from __future__ import annotations

import math
from typing import Optional

Position = tuple[float, float]


def shank_counts(probe: dict) -> list[int]:
    """Sites on each shank, in file order."""
    n_shanks = int(((probe or {}).get("shanks") or {}).get("count") or 1)
    cps = ((probe or {}).get("sites") or {}).get("count_per_shank")
    if isinstance(cps, list):
        return [int(c) for c in cps]
    return [int(cps or 0)] * n_shanks


def _geometry(probe: dict) -> list:
    return list(((probe or {}).get("sites") or {}).get("geometry") or [])


def site_channels(probe: dict) -> list[int]:
    """Channel (relative to channelOffset) of every site, in file order.

    Identity unless channelMap.map is given: a flat list maps site j to
    channel map[j]; a nested list gives each shank's channels, site by site.
    """
    n_sites = sum(shank_counts(probe))
    m = (((probe or {}).get("channelMap") or {}).get("map"))
    if isinstance(m, list) and m:
        if all(isinstance(v, list) for v in m):
            flat = [int(c) for sub in m for c in sub]
        else:
            flat = [int(c) for c in m]
        if len(flat) != n_sites:
            raise ValueError(f"channelMap.map has {len(flat)} entries, the probe has {n_sites} sites")
        return flat
    return list(range(n_sites))


def shank_channels(probe: dict, channel_offset: int = 0) -> list[list[int]]:
    """Absolute channels of each shank, in file order (top site first)."""
    chans = site_channels(probe)
    out, k = [], 0
    for n in shank_counts(probe):
        out.append([channel_offset + c for c in chans[k:k + n]])
        k += n
    return out


def shank_positions(probe: dict) -> list[list[Optional[Position]]]:
    """[x, y] of each shank's sites, in file order; None where geometry is absent."""
    geom = _geometry(probe)
    out, k = [], 0
    for n in shank_counts(probe):
        sl = []
        for j in range(k, k + n):
            sl.append((float(geom[j][0]), float(geom[j][1])) if j < len(geom) and geom[j] is not None else None)
        out.append(sl)
        k += n
    return out


def channel_positions(probe: dict, channel_offset: int = 0) -> dict[int, Position]:
    """{absolute channel: (x, y)} for every site that has geometry."""
    out: dict[int, Position] = {}
    for chans, pos in zip(shank_channels(probe, channel_offset), shank_positions(probe)):
        for c, p in zip(chans, pos):
            if p is not None:
                out[c] = p
    return out


def positions_for_channels(probe: dict, channel_offset: int,
                           channels: list[int]) -> list[Optional[Position]]:
    """Position of each channel in `channels` (any order); None if unknown."""
    cp = channel_positions(probe, channel_offset)
    return [cp.get(int(c)) for c in channels]


def depths_for_channels(probe: dict, channel_offset: int, channels: list[int]) -> list[float]:
    """Height above the tip of each channel in `channels`; NaN if unknown."""
    return [p[1] if p is not None else math.nan
            for p in positions_for_channels(probe, channel_offset, channels)]


def group_channels(param: dict, g: int) -> list[int]:
    """Channels of 1-based spike group g, in the order the YAML lists them."""
    groups = ((param or {}).get("spikeDetection") or {}).get("channelGroups") or []
    if not 0 < g <= len(groups):
        return []
    out = []
    for c in groups[g - 1].get("channels") or []:
        out.append(int(c["id"]) if isinstance(c, dict) else int(c))
    return out
