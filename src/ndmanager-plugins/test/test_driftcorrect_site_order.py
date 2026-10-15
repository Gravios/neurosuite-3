#!/usr/bin/env python3
"""correct_waveform_batch keeps every site's waveform on its own site,
whatever order the sites are listed in.

With zero drift the output must equal the input; with a drift of exactly
one site spacing each site must take its neighbour's waveform — for sites
listed tip first, top first (the probe library order) and shuffled, with a
site of unknown depth passed through.  Exits nonzero on failure (repo test
convention).
"""
import importlib.util
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "scripts"))
spec = importlib.util.spec_from_file_location(
    "process_driftcorrect", os.path.join(HERE, "..", "scripts", "process_driftcorrect.py"))
dc = importlib.util.module_from_spec(spec)
sys.modules["process_driftcorrect"] = dc
spec.loader.exec_module(dc)

fails = 0


def check(name, cond):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        fails += 1


depth_of_site = {0: 0.0, 1: 20.0, 2: 40.0, 3: 60.0, 4: 80.0}   # site id -> depth (µm)
n_samp = 6
for label, order in (("tip first", [0, 1, 2, 3, 4]),
                     ("top first", [4, 3, 2, 1, 0]),
                     ("shuffled", [2, 4, 0, 3, 1])):
    z = np.array([depth_of_site[s] for s in order])
    # waveform of a site = its id + 1, constant over time
    wf = np.zeros((1, n_samp, len(order)), dtype=np.int16)
    for k, s in enumerate(order):
        wf[0, :, k] = 10 * (s + 1)
    same = dc.correct_waveform_batch(wf, z, np.array([0.0]))
    check(f"{label}: zero drift leaves every site unchanged", np.array_equal(same, wf.astype(np.float32)))
    # probe 20 µm deeper: the site at depth z now sees what was recorded at z - 20
    moved = dc.correct_waveform_batch(wf, z, np.array([20.0]))
    want = np.array([10 * (max(s - 1, 0) + 1) for s in order], dtype=np.float32)
    check(f"{label}: one-spacing drift takes the neighbour's waveform", np.allclose(moved[0, 0, :], want))

z = np.array([80.0, np.nan, 40.0, 20.0, 0.0])
wf = np.arange(5, dtype=np.int16)[None, None, :].repeat(n_samp, axis=1) * 10 + 10
out = dc.correct_waveform_batch(wf, z, np.array([0.0]))
check("unknown-depth site passes through, others unchanged", np.array_equal(out, wf.astype(np.float32)))

print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
sys.exit(1 if fails else 0)
