#!/usr/bin/env python3
"""Probe geometry is bound to channels by channel number, the same way everywhere.

Site j of a .probe file belongs to channel channelOffset + j (or
channelOffset + channelMap.map[j]) and sits at sites.geometry[j].  Checks
probe_geometry.py itself, and that ndm_setupgroups, ndm_driftcorrect,
ndm_estimatedrift and ndm_localise follow it when count_per_shank differs
per shank, when a channel map is given, when a group lists its channels in
another order than the file, and for shanks other than the first.
Exits nonzero on failure (repo test convention).
"""
import importlib.util
import math
import os
import sys
import tempfile
from pathlib import Path

import numpy as np
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "..", "scripts")
sys.path.insert(0, SCRIPTS)


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(SCRIPTS, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod          # dataclasses look the module up here
    spec.loader.exec_module(mod)
    return mod


pg = load("probe_geometry")
sg = load("process_setupgroups")
dc = load("process_driftcorrect")
ed = load("process_estimatedrift")
lo = load("process_localise")

fails = 0


def check(name, cond):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        fails += 1


# Two shanks 200 µm apart, 3 sites each, top site first (channels ascend as
# depth increases).
two = {"shanks": {"count": 2, "spacing_um": 200},
       "sites": {"count_per_shank": 3,
                 "geometry": [[0, 100], [0, 75], [0, 50],
                              [200, 100], [200, 75], [200, 50]]}}

# Shanks with different site counts.
uneven = {"shanks": {"count": 2, "spacing_um": 200},
          "sites": {"count_per_shank": [2, 3],
                    "geometry": [[0, 40], [0, 20],
                                 [200, 60], [200, 40], [200, 20]]}}

# Channel map: site j is channel map[j].
mapped = {"shanks": {"count": 1},
          "sites": {"count_per_shank": 4,
                    "geometry": [[0, 300], [0, 200], [0, 100], [0, 0]]},
          "channelMap": {"map": [2, 0, 3, 1]}}

print("probe_geometry")
check("count list", pg.shank_counts(uneven) == [2, 3])
check("count int", pg.shank_counts(two) == [3, 3])
# No count_per_shank: a one-shank probe holds every geometry entry (the rule
# ndmanager's Probe Maker and Probes tab share); several shanks hold none.
nocount1 = {"shanks": {"count": None},
            "sites": {"count_per_shank": None, "geometry": [[0, 100], [10, 80], [0, 60]]}}
nocount2 = {"shanks": {"count": 2},
            "sites": {"geometry": [[0, 100], [0, 80], [200, 100], [200, 80]]}}
check("no count, one shank", pg.shank_counts(nocount1) == [3])
check("no count, one shank: channels", pg.shank_channels(nocount1, 4) == [[4, 5, 6]])
check("no count, two shanks", pg.shank_counts(nocount2) == [0, 0])
check("shank channels, offset 8", pg.shank_channels(two, 8) == [[8, 9, 10], [11, 12, 13]])
check("uneven shank channels", pg.shank_channels(uneven, 0) == [[0, 1], [2, 3, 4]])
check("mapped channel 0 at geometry[1]",
      pg.positions_for_channels(mapped, 0, [0])[0] == (0.0, 200.0))
check("mapped depths",
      pg.depths_for_channels(mapped, 10, [10, 11, 12, 13]) == [200.0, 0.0, 300.0, 100.0])
nested = dict(mapped, channelMap={"map": [[2, 0, 3, 1]]})
check("nested map flattened", pg.site_channels(nested) == [2, 0, 3, 1])
try:
    pg.site_channels(dict(mapped, channelMap={"map": [0, 1]}))
    check("short map rejected", False)
except ValueError:
    check("short map rejected", True)
check("unknown channel is NaN", math.isnan(pg.depths_for_channels(two, 0, [99])[0]))
check("group_channels dict ids",
      pg.group_channels({"spikeDetection": {"channelGroups": [
          {"channels": [{"id": 5}, 4]}]}}, 1) == [5, 4])

print("ndm_setupgroups")
with tempfile.TemporaryDirectory() as td:
    p = Path(td) / "m.probe"
    p.write_text(yaml.safe_dump({"probeFile": mapped}))
    shanks, geoms = sg.parse_probe(p, 0)
    pos = {c: tuple(xy) for c, xy in zip(shanks[0], geoms[0])}
    check("mapped: channel map[j] gets geometry[j]",
          pos == {2: (0, 300), 0: (0, 200), 3: (0, 100), 1: (0, 0)})
    p.write_text(yaml.safe_dump({"probeFile": uneven}))
    shanks, geoms = sg.parse_probe(p, 0)
    check("uneven: per-shank counts", [len(s) for s in shanks] == [2, 3])
    check("uneven: shank 1 geometry", geoms[1] == [[200, 60], [200, 40], [200, 20]])


def depth_param(channels_g2):
    return {"spikeDetection": {"channelGroups": [
        {"channels": [0, 1, 2]}, {"channels": channels_g2}]}}


entry = {"probeFile": "two.probe", "channelOffset": 0}
for name, mod, fn in (("ndm_driftcorrect", dc, "get_group_depths"),
                      ("ndm_estimatedrift", ed, None)):
    print(name)
    d = mod.group_depths_from_probe(two, entry, [5, 4, 3], 1)
    check("shank 1, reversed channels", list(d) == [50.0, 75.0, 100.0])
    d = mod.group_depths_from_probe(two, entry, [], 1)
    check("no channels: shank 1 file order", list(d) == [100.0, 75.0, 50.0])
    d = mod.site_depths_from_probe(uneven, 1)
    check("uneven shank 1", list(d) == [60.0, 40.0, 20.0])
    if fn:
        d = getattr(mod, fn)(2, depth_param([5, 3, 4]), {2: (1, 1)}, {1: entry},
                             {"two.probe": two}, [])
        check("get_group_depths binds by channel", list(d) == [50.0, 100.0, 75.0])

print("ndm_localise")
xy = lo.site_positions_from_probe(two, 3, 1)
check("shank 1 positions", xy.tolist() == [[200, 100], [200, 75], [200, 50]])
xy = lo.site_positions_from_probe(uneven, 3, 1)
check("uneven shank 1 positions", xy.tolist() == [[200, 60], [200, 40], [200, 20]])
res = lo.site_positions_from_yaml(
    {"probes": [{"probeId": 1, "probeFile": "two.probe", "channelOffset": 0}],
     "spikeDetection": {"channelGroups": [{"probeId": 1, "shankIndex": 1,
                                           "channels": [{"id": 5}, 3, 4]}]}}, 0)
check("yaml fallback returns the group's channels",
      isinstance(res, tuple) and res[1] == [5, 3, 4] and res[2] == 1)
pos = np.array(pg.positions_for_channels(two, 0, [5, 3, 4]), dtype=float)
check("positions for [5, 3, 4]", pos.tolist() == [[200, 50], [200, 100], [200, 75]])

print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
sys.exit(1 if fails else 0)
