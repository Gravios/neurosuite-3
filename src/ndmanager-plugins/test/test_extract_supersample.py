#!/usr/bin/env python3
"""ndm_extractspikes --supersample F: a super-sampled re-extraction through the ndm engines.

The raw window is interpolated on the .fil (margin, smoothing spline) to nSamples x F ticks and
only then transformed, so every derived .spk comes from the interpolated raw signal; the files go
under the _U<F> token and the group's tick .res (<base>.res.<resToken>_U<F>.N, ticks = sample x F +
phase) is written once (phase 0) and read back for the phases by the next space.

  1. --supersample 4 on standard: s.spk.standard_U4.1 holds nSamples x 4 ticks per channel, tick 4k
     is within the noise of raw sample k, the trough of a spike planted between two samples lands on
     a fractional tick, and s.res.standard_U4.1 == res x 4;
  2. --method stderiv_S1 --supersample 4: s.spk.stderiv_S1_U4.1, phases read from the tick .res (not
     rewritten), on the base-rate amplitude scale of s.spk.stderiv_S1.1 and smooth (no tick-to-tick
     rounding jitter), its .nst.<tok>_U4 sized in ticks;
  3. --method stderiv_S1_U4 is the same request (the token form --allMethods passes); a factor that
     disagrees with the token is refused; --supersample on ndm_detectspikes is refused;
  4. the post-extraction filter is not run on a super-sampled re-extraction.

Runs the real ndm_extractspikes with the real engines: ctest passes PROCESS_EXTRACTSPIKES_BIN,
PROCESS_EXTRACTSPIKES_SDIFF_BIN and PROCESS_EXTRACTSPIKES_STDERIV_BIN; otherwise they are looked
up on PATH, and the test is SKIPPED when they are not found.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "..", "scripts")


def find(var, name):
    p = os.environ.get(var)
    return p if p else shutil.which(name)


BINS = {k: find(v, k) for k, v in (
    ("process_extractspikes", "PROCESS_EXTRACTSPIKES_BIN"),
    ("process_extractspikes_sdiff", "PROCESS_EXTRACTSPIKES_SDIFF_BIN"),
    ("process_extractspikes_stderiv", "PROCESS_EXTRACTSPIKES_STDERIV_BIN"))}
if not all(BINS.values()) or not all(os.path.isfile(b) for b in BINS.values()):
    print(f"SKIPPED: extractor binaries not found ({BINS})")
    sys.exit(0)

fails = 0


def check(ok, what):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what)
    fails += not ok


SR, NCH, NS, PEAK = 20000, 4, 32, 11          # peakSampleIndex 11 (1-based) -> timeBefore 10
CH = [0, 1]


def main():
    td = tempfile.mkdtemp()
    try:
        bind = os.path.join(td, "bin"); os.makedirs(bind)
        for name, path in BINS.items():
            os.symlink(os.path.abspath(path), os.path.join(bind, name))
        with open(os.path.join(bind, "xpathReader"), "w") as fh:
            fh.write("#!/bin/bash\necho stub\n")
        os.chmod(os.path.join(bind, "xpathReader"), 0o755)
        sd = os.path.join(td, "s"); os.makedirs(sd)
        base = os.path.join(sd, "s")
        rng = np.random.default_rng(3)
        n = 40000
        fil = rng.normal(0, 30, (n, NCH))
        t = np.arange(-10, 22)
        # spike A with its trough on a sample, spike B with its trough half a sample later
        centres = [(5000, 0.0), (9000, 0.5), (13000, 0.0), (17000, 0.25)]
        for c, frac in centres:
            for ch in CH:
                fil[c - 10:c + 22, ch] += -3000 * np.exp(-0.5 * ((t - frac) / 2.0) ** 2) * (1.0 if ch == 0 else 0.6)
        fil.astype(np.int16).tofile(base + ".fil")
        res = np.array([c for c, _ in centres], np.int64)
        res.tofile(base + ".res.standard.1")
        with open(base + ".yaml", "w") as fh:
            fh.write(f"""acquisitionSystem: {{nBits: 16, nChannels: {NCH}, samplingRate: {SR}}}
spikeDetection:
  channelGroups:
    - channels: [0, 1]
      nSamples: {NS}
      peakSampleIndex: {PEAK}
programs:
  - name: ndm_detectspikes
    parameters:
      - {{name: method, value: standard, status: Optional}}
  - name: ndm_extractspikes
    parameters:
      - {{name: method, value: standard, status: Optional}}
      - {{name: noiseWindows, value: 200, status: Optional}}
      - {{name: dedupSamples, value: 0, status: Optional}}
""")
        env = dict(os.environ)
        env["PATH"] = bind + os.pathsep + os.path.abspath(SCRIPTS) + os.pathsep + env.get("PATH", "")

        def run(script, *args):
            r = subprocess.run(["bash", os.path.join(SCRIPTS, script), "s"] + list(args), cwd=sd,
                               capture_output=True, text=True, env=env, timeout=300)
            return r.returncode, r.stdout + r.stderr

        # the base-rate extraction first (standard and stderiv_S1), as a session would have
        rc, out = run("ndm_extractspikes")
        check(rc == 0 and os.path.exists(base + ".spk.standard.1"), "base-rate standard extraction")
        rc, out = run("ndm_extractspikes", "--method", "stderiv_S1")
        check(rc == 0 and os.path.exists(base + ".spk.stderiv_S1.1"), "base-rate stderiv_S1 extraction")
        std = np.fromfile(base + ".spk.standard.1", np.int16).reshape(-1, NS, 2)
        sdB = np.fromfile(base + ".spk.stderiv_S1.1", np.int16).reshape(-1, NS, 2)

        # 1. --supersample 4 on standard
        rc, out = run("ndm_extractspikes", "--supersample", "4")
        check(rc == 0, f"ndm_extractspikes --supersample 4 exits 0\n{out[-1500:] if rc else ''}")
        check("Super-sampling : x4 (smooth), waveforms written as standard_U4" in out, "the run says what it writes")
        spkU = base + ".spk.standard_U4.1"; resU = base + ".res.standard_U4.1"
        check(os.path.exists(spkU) and os.path.exists(resU), "s.spk.standard_U4.1 and s.res.standard_U4.1 written")
        check(not os.path.exists(base + ".spk.standard.1.pending") and np.array_equal(
            np.fromfile(base + ".spk.standard.1", np.int16).reshape(-1, NS, 2), std), "the base-rate .spk is untouched")
        up = np.fromfile(spkU, np.int16).reshape(-1, NS * 4, 2)
        check(up.shape[0] == len(res), "one super-sampled row per .res time")
        ticks = np.fromfile(resU, np.int64)
        check(np.array_equal(ticks, res * 4), "the tick .res is sample x 4 (phase 0)")
        dev = np.abs(up[:, ::4, :].astype(float) - std.astype(float)).max()
        check(dev < 3 * 30, f"tick 4k is within the noise of raw sample k (max deviation {dev:.0f} at noise SD 30)")
        am = [int(np.argmin(up[i, :, 0])) for i in range(len(res))]
        check(am[0] == 40 and am[1] in (41, 42, 43) and am[3] in (40, 41, 42),
              f"the trough lands on the fractional tick (troughs at ticks {am}; peak tick 40, B half a sample later)")
        check(os.path.exists(base + ".nst.standard_U4.1"), "the _U4 noise statistics are written beside it")
        check("Post-extraction filter : not run on a super-sampled re-extraction" in out,
              "the post-extraction filter is not run on a super-sampled re-extraction")

        # 2. the derived space from the interpolated raw: stderiv_S1 x4
        rc, out = run("ndm_extractspikes", "--method", "stderiv_S1", "--supersample", "4")
        check(rc == 0, f"--method stderiv_S1 --supersample 4 exits 0\n{out[-1500:] if rc else ''}")
        check("phases from s.res.standard_U4.1" in out, "the phases come from the tick .res the standard space wrote")
        check(np.array_equal(np.fromfile(resU, np.int64), res * 4), "the tick .res is not rewritten")
        spkD = base + ".spk.stderiv_S1_U4.1"
        check(os.path.exists(spkD), "s.spk.stderiv_S1_U4.1 written")
        sdU = np.fromfile(spkD, np.int16).reshape(-1, NS * 4, 2)
        pB = np.abs(sdB[:, :, 0]).max(1).astype(float); pU = np.abs(sdU[:, :, 0]).max(1).astype(float)
        ratio = pU / pB
        check(np.all(ratio > 0.8) and np.all(ratio < 1.25), f"stderiv_S1_U4 is on the base-rate amplitude scale (peak ratios {np.round(ratio, 2)})")
        d2 = np.abs(np.diff(sdU[:, :, 0].astype(float), 2, axis=1)).max(1)
        check(np.all(d2 < 0.12 * pU), f"smooth: the largest tick-to-tick curvature is under 12% of the peak ({np.round(d2 / pU, 3)})")
        nst = np.fromfile(base + ".nst.stderiv_S1_U4.1", np.int32, 8)
        check(nst[2] == NS * 4 and nst[3] == 2, f"the _U4 .nst counts ticks ({nst[2]} x {nst[3]})")

        # 3. the token form, and the refusals
        os.remove(spkD)
        rc, out = run("ndm_extractspikes", "--method", "stderiv_S1_U4")
        check(rc == 0 and os.path.exists(spkD) and "waveforms written as stderiv_S1_U4" in out,
              "--method stderiv_S1_U4 is the same request (the form --allMethods passes)")
        rc, out = run("ndm_extractspikes", "--method", "stderiv_S1_U2", "--supersample", "4")
        check(rc != 0 and "super-sampled x2 but --supersample says 4" in out, "a factor that disagrees with the token is refused")
        rc, out = run("ndm_extractspikes", "--supersample", "12")
        check(rc != 0 and "supersample must be 1" in out, "a factor outside 2-9 is refused")
        rc, out = run("ndm_extractspikes", "--supersample", "4", "--interp", "cubic")
        check(rc != 0 and "interp must be" in out, "an unknown interpolator is refused")
        rc, out = run("ndm_detectspikes", "--supersample", "4")
        check(rc != 0 and "extraction only" in out, "--supersample on ndm_detectspikes is refused")

        # 4. the exact spline, on request, and the x2 space sharing nothing with x4
        rc, out = run("ndm_extractspikes", "--supersample", "2", "--interp", "spline")
        check(rc == 0 and os.path.exists(base + ".spk.standard_U2.1") and os.path.exists(base + ".res.standard_U2.1")
              and "x2 (spline)" in out, "--supersample 2 --interp spline: its own .spk and tick .res")
        u2 = np.fromfile(base + ".spk.standard_U2.1", np.int16).reshape(-1, NS * 2, 2)
        check(np.array_equal(u2[:, ::2, :], std), "the exact spline reproduces the raw samples at tick 2k")
    finally:
        shutil.rmtree(td, ignore_errors=True)
    print(f"test_extract_supersample: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
