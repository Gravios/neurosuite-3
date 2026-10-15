#!/usr/bin/env python3
"""xcorr_shift gives the same depth shift whatever order a group lists its sites in.

A unit's amplitude profile over depth is shifted by a known amount between the
reference and the current window.  The shift is estimated with the sites listed
tip-first (depth ascending), top-first (depth descending, the NeuroScope
convention) and in a staggered octrode order, and must agree in sign and size.
Exits nonzero on failure (repo test convention).
"""
import importlib.util
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "process_estimatedrift", os.path.join(HERE, "..", "scripts", "process_estimatedrift.py"))
ed = importlib.util.module_from_spec(spec)
sys.modules["process_estimatedrift"] = ed     # dataclasses look the module up here
spec.loader.exec_module(ed)

fails = 0


def check(name, cond):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        fails += 1


depths = 22.0 + 20.0 * np.arange(8)          # Buzsaki octrode, tip-first


def profile(centre):
    return np.exp(-0.5 * ((depths - centre) / 25.0) ** 2)


ref = profile(80.0)
win = profile(100.0)                            # unit moved 20 µm up the shank
base = ed.xcorr_shift(ref, win, depths)
check("tip-first: a shift is found", base is not None)
b = base[0]
check("tip-first: |shift| is one site step (20 µm)", abs(abs(b) - 20.0) < 1.0)

rev = np.arange(8)[::-1]                        # top-first, NeuroScope convention
r = ed.xcorr_shift(ref[rev], win[rev], depths[rev])
check("top-first listing gives the same signed shift", r is not None and abs(r[0] - b) < 1e-9)

octo = np.array([7, 5, 3, 1, 0, 2, 4, 6])       # arbitrary listing order
o = ed.xcorr_shift(ref[octo], win[octo], depths[octo])
check("arbitrary listing gives the same signed shift", o is not None and abs(o[0] - b) < 1e-9)

nan_depths = depths.copy()
nan_depths[3] = np.nan                          # a site without geometry
n = ed.xcorr_shift(ref, win, nan_depths)
check("a site without a depth is dropped, not fatal", n is not None and np.sign(n[0]) == np.sign(b))

print(f"\n{'FAILED' if fails else 'OK'}: {fails} failure(s)")
sys.exit(1 if fails else 0)
