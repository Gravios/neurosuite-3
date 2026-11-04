#!/usr/bin/env python3
"""process_estimategeometry.py on a synthetic mixed session.

Group 1 is a silicon-like shank with a KNOWN geometry (sitePositions_um); group
2 is a packed octrode of 15 µm wires with none.  Sources are monopoles around
each array (a few sortable units and many small distant ones for the
background), plus a common LFP and independent thermal noise.

Checks:
  - group 2 is estimated, group 1 is not (it has a geometry) but calibrates
    the weights; the session YAML is unchanged;
  - the estimate matches the true octrode within a few µm (rotation and
    reflection removed), and finds most of the touching pairs;
  - the probe file binds every channel to its estimated site through
    probe_geometry (the reader every tool uses);
  - estimateKnown: 1 checks group 1 against its geometry (Spearman > 0.8);
  - a flat channel is left out of its group;
  - NEGATIVE CONTROL: the same session with the octrode's sources given
    gain profiles unrelated to position gives a much worse estimate.
Exits nonzero on failure (repo test convention); SKIPPED without numpy/scipy/yaml.
"""
import os
import subprocess
import sys
import tempfile

try:
    import numpy as np
    import yaml
    __import__("scipy.signal")
except ImportError:
    print("SKIPPED: numpy, scipy and pyyaml are needed")
    sys.exit(0)

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "..", "scripts")
sys.path.insert(0, SCRIPTS)
import probe_geometry  # noqa: E402
import process_estimategeometry as pe  # noqa: E402

fails = 0


def check(name, cond, detail=""):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}{': ' + detail if detail else ''}")
    if not cond:
        fails += 1


WIRE = 15.0
OCT = (np.array([[0, 0], [1, 0], [2, 0], [0.5, 0.866], [1.5, 0.866], [2.5, 0.866], [1, 1.732], [2, 1.732]])
       - [1.25, 0.866]) * WIRE
SHANK = np.array([[(-11 if j % 2 else 11), 140 - 20 * j] for j in range(8)], float)


def make(d, spatial=True, flat=None, seed=0, sr=20000, dur=40.0):
    rng = np.random.default_rng(seed)
    T = int(sr * dur)
    X = np.zeros((T, 16), np.float32)
    t = np.arange(-16, 32) / sr
    tmpl = -np.exp(-0.5 * (t / 0.00018) ** 2) + 0.35 * np.exp(-0.5 * ((t - 0.0005) / 0.0003) ** 2)
    for cols, sites, plane_xz in ((slice(0, 8), SHANK, True), (slice(8, 16), OCT, False)):
        def gains(S):
            if plane_xz:          # shank in the x-y plane: sites (x, y, 0), sources off the face
                P3 = np.c_[sites, np.zeros(len(sites))]
            else:                 # wire tips in the cross-section plane z = 0
                P3 = np.c_[sites, np.zeros(len(sites))]
            r = np.linalg.norm(P3[None] - S[:, None], axis=2)
            if not spatial and not plane_xz:
                r = rng.permuted(r, axis=1)
            return 1.0 / np.maximum(r, 5.0)
        for m, rmin, rmax, rate, amp in ((12, 25, 90, 4.0, 4000.0), (400, 60, 400, 3.0, 2500.0)):
            u = rng.standard_normal((m, 3)); u /= np.linalg.norm(u, axis=1, keepdims=True)
            S = u * rng.uniform(rmin, rmax, m)[:, None] + np.r_[sites.mean(0), 0]
            G = amp * gains(S)
            for k in range(m):
                ts = rng.integers(len(t), T - len(t), rng.poisson(rate * dur))
                idx = (ts[:, None] + np.arange(len(t))[None, :]).ravel()
                blk = np.repeat(tmpl[None, :, None] * G[k][None, None, :], ts.size, 0).reshape(-1, 8)
                sub = X[:, cols]
                np.add.at(sub, idx, blk)
                X[:, cols] = sub
    tt = np.arange(T) / sr
    X += (300 * np.sin(2 * np.pi * 8 * tt))[:, None].astype(np.float32)
    X += 6.0 * rng.standard_normal(X.shape).astype(np.float32)
    if flat is not None:
        X[:, flat] = 0
    X.astype(np.int16).tofile(os.path.join(d, "s.dat"))
    doc = dict(acquisitionSystem=dict(nChannels=16, samplingRate=sr, nBits=16),
               spikeDetection=dict(channelGroups=[
                   dict(channels=list(range(8)), sitePositions_um=SHANK.tolist()),
                   dict(channels=list(range(8, 16)))]))
    with open(os.path.join(d, "s.yaml"), "w") as fh:
        yaml.safe_dump(doc, fh)


def run(d, *extra):
    cmd = [sys.executable, os.path.join(SCRIPTS, "process_estimategeometry.py"), "--param-file",
           os.path.join(d, "s.yaml"), "--session", os.path.join(d, "s"), "--n-segments", "30",
           "--segment-sec", "1", "--n-bootstrap", "5", "--restarts", "12", *extra]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
    return r


def estimate(d, g=2):
    rep = yaml.safe_load(open(os.path.join(d, f"s.bundle.{g}.yaml")))
    chans = rep["channels"]
    return rep, np.array([rep["sites_um"][c] for c in chans]), chans


def touching(P):
    D = pe.untriu(pe.pair_dist(P), len(P)); np.fill_diagonal(D, np.inf)
    return {(i, j) for i in range(len(P)) for j in range(i + 1, len(P)) if D[i, j] < 1.1 * D.min()}


with tempfile.TemporaryDirectory() as d:
    make(d)
    before = open(os.path.join(d, "s.yaml")).read()
    r = run(d)
    check("runs", r.returncode == 0, r.stderr[-400:] if r.returncode else "")
    check("the known group is not estimated", not os.path.exists(os.path.join(d, "s.bundle.1.yaml")))
    check("the YAML is unchanged", open(os.path.join(d, "s.yaml")).read() == before)
    rep, P, chans = estimate(d)
    check("weights come from the known group", rep["weightsFrom"].startswith("calibration groups 1"),
          rep["weightsFrom"])
    _, rms = pe.procrustes(P, OCT)
    check("octrode recovered within 4 µm RMS", rms < 4.0, f"{rms:.1f} µm")
    t, e = touching(OCT), touching(P)
    check("most touching pairs found", len(t & e) >= 10, f"{len(t & e)} of {len(t)}")
    check("no two wires overlap", pe.pair_dist(P).min() > 0.9 * WIRE, f"{pe.pair_dist(P).min():.1f}")
    probe = yaml.safe_load(open(os.path.join(d, "s.bundle.2.probe")))["probeFile"]
    pos = probe_geometry.positions_for_channels(probe, rep["attachWithChannelOffset"], chans)
    check("the probe file binds every channel to its site",
          all(p is not None for p in pos) and np.allclose(np.array(pos), P, atol=0.11))
    check("the probe file says the plane", probe["sites"]["plane"] == "cross-section")

    r = run(d, "--estimate-known", "1", "--layout", "free", "--overwrite", "1")
    k = yaml.safe_load(open(os.path.join(d, "s.bundle.1.yaml")))
    rho = k["validation"]["spearmanDissimilarityVsDistance"]
    check("estimateKnown checks the known shank", rho is not None and rho > 0.8, str(rho))
    check("... without calibrating on its own layout", k["weightsFrom"] == "split-half reliability",
          k["weightsFrom"])
    check("... while the octrode still calibrates on it",
          yaml.safe_load(open(os.path.join(d, "s.bundle.2.yaml")))["weightsFrom"] == "calibration groups 1")
    check("estimateKnown writes no probe file for it", not os.path.exists(os.path.join(d, "s.bundle.1.probe")))

    # a group named on the command line is estimated whatever geometry it has (it may be a
    # placeholder), gets a probe file, and does not calibrate itself
    os.remove(os.path.join(d, "s.bundle.1.yaml"))
    r = run(d, "--groups", "1", "--layout", "free")
    k = yaml.safe_load(open(os.path.join(d, "s.bundle.1.yaml")))
    check("a named group with a geometry is estimated, with a probe file",
          os.path.exists(os.path.join(d, "s.bundle.1.probe")) and "validation" in k)
    check("... and is not its own calibration", k["weightsFrom"] == "split-half reliability", k["weightsFrom"])
    check("... the other group is left alone", "group(s) [1]" in r.stderr)

# a wire-bundle TEMPLATE attached to the octrode (drawn, not measured) does not stop the estimate
with tempfile.TemporaryDirectory() as d:
    make(d)
    tmpl = dict(probeFile=dict(vendor="general", model="octrode-template", totalChannels=8,
                               substrate=dict(material="wire-bundle"), shanks=dict(count=1),
                               sites=dict(count_per_shank=8, geometry=[[6 * j - 21, 0.5 * j] for j in range(8)])))
    yaml.safe_dump(tmpl, open(os.path.join(d, "oct.probe"), "w"))
    doc = yaml.safe_load(open(os.path.join(d, "s.yaml")))
    doc["probes"] = [dict(probeId=0, probeFile="oct.probe", channelOffset=8)]
    doc["spikeDetection"]["channelGroups"][1].update(probeId=0, shankIndex=0)
    yaml.safe_dump(doc, open(os.path.join(d, "s.yaml"), "w"))
    r = run(d)
    rep, P, _ = estimate(d)
    check("a wire-bundle template is estimated over", os.path.exists(os.path.join(d, "s.bundle.2.probe"))
          and rep["validation"]["nominal"] is True)
    check("... while the shank still calibrates", rep["weightsFrom"].startswith("calibration groups 1"),
          rep["weightsFrom"])

with tempfile.TemporaryDirectory() as d:
    make(d, flat=15)
    run(d)
    rep, P, chans = estimate(d)
    check("a flat channel is left out", rep["flatChannelsLeftOut"] == [15] and 15 not in chans and len(P) == 7,
          str(rep["flatChannelsLeftOut"]))

with tempfile.TemporaryDirectory() as d:
    make(d, spatial=False)
    run(d)
    _, P, _ = estimate(d)
    _, rms_neg = pe.procrustes(P, OCT)
    check("negative control: no spatial structure, no recovery", rms_neg > 2.5 * max(rms, 1.0),
          f"{rms_neg:.1f} µm vs {rms:.1f} µm")

print("OK (0 failures)" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
