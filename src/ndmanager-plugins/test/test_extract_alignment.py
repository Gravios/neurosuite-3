"""Every .spk row is the waveform at its .res time.

Runs the three spike extractors on a small synthetic .fil (two 4-channel
groups, spikes dense enough that windows overlap within and across groups)
and checks, row by row:

  process_extractspikes        a detection run: row i is the raw window at
                               res[i] - (p-1).  (Overlapping windows used to be
                               read late: 2026-10-17.)
  all three, -R                one row per .res time, also for times whose
                               window is not inside the .fil (zeros); inside,
                               the raw window (plain), its spatial derivative
                               (sdiff), or the spatial derivative's temporal
                               difference seeded with the sample before
                               (stderiv), for the all-pairs order.
                               (Out-of-file times used to be skipped.)

ctest passes the binaries as PROCESS_EXTRACTSPIKES_BIN,
PROCESS_EXTRACTSPIKES_SDIFF_BIN and PROCESS_EXTRACTSPIKES_STDERIV_BIN.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))


def find(var, name):
    p = os.environ.get(var)
    if p:
        return os.path.abspath(p)
    for c in (os.path.join(_ROOT, "build", "ndmanager-plugins", "src", name, name),
              os.path.join(_ROOT, "build", "src", name, name)):
        if os.path.isfile(c):
            return c
    return None


BINS = {k: find(v, k) for k, v in (
    ("process_extractspikes", "PROCESS_EXTRACTSPIKES_BIN"),
    ("process_extractspikes_sdiff", "PROCESS_EXTRACTSPIKES_SDIFF_BIN"),
    ("process_extractspikes_stderiv", "PROCESS_EXTRACTSPIKES_STDERIV_BIN"))}
if not all(BINS.values()) or not all(os.path.isfile(b) for b in BINS.values()):
    print(f"SKIPPED: extractor binaries not found ({BINS})")
    raise SystemExit(0)

NCH, NS, P = 8, 32, 11            # channels, samples per waveform, -p (1-based)
TB = P - 1                        # samples before the peak
GROUPS = [[0, 1, 2, 3], [4, 5, 6, 7]]
fails = 0


def check(ok, what):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what)
    fails += not ok


def make_fil(path):
    rng = np.random.default_rng(7)
    T = 20 * 20000
    x = rng.normal(0, 30, (T, NCH))
    t = np.arange(-10, 22)
    w = -np.exp(-0.5 * (t / 2.0) ** 2) + 0.4 * np.exp(-0.5 * ((t - 7) / 4.0) ** 2)
    for depth, n in (([700, 400, 150, 50, 0, 0, 0, 0], 600), ([0, 100, 500, 800, 0, 0, 0, 0], 500),
                     ([0, 0, 0, 0, 600, 600, 200, 0], 700), ([0, 0, 0, 0, 0, 150, 450, 900], 400)):
        for s in np.sort(rng.integers(100, T - 100, n)):
            x[s - 10:s + 22] += np.outer(w, depth)
    fil = np.clip(np.rint(x), -32768, 32767).astype("<i2")
    fil.tofile(path)
    return fil


def clamp(v):
    return np.clip(np.where(v >= 0, np.floor(v + 0.5), np.ceil(v - 0.5)), -32768, 32767).astype(np.int64)


def allpairs(rows):                 # rows: (..., nChan) raw -> clamped spatial derivative
    n = rows.shape[-1]
    return clamp(n * rows.astype(np.float64) - rows.sum(axis=-1, keepdims=True))


def run(bin_, args, cwd):
    r = subprocess.run([bin_] + args + ["s"], cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
    return r.returncode


def main():
    with tempfile.TemporaryDirectory() as d:
        fil = make_fil(os.path.join(d, "s.fil"))
        N = len(fil)
        common = ["-n", str(NCH), "-c", "0,1,2,3:4,5,6,7", "-w", str(NS), "-p", str(P)]

        print("process_extractspikes, detection run:")
        check(run(BINS["process_extractspikes"], common + ["-r", "16", "-l", "16",
                  "-t", "100,100,100,100:100,100,100,100"], d) == 0, "runs")
        for g, chans in enumerate(GROUPS, 1):
            res = np.fromfile(os.path.join(d, f"s.res.{g}"), "<i8")
            spk = np.fromfile(os.path.join(d, f"s.spk.{g}"), "<i2").reshape(-1, NS, 4)
            want = np.stack([fil[t - TB:t - TB + NS][:, chans] for t in res])
            gaps = np.diff(np.sort(np.concatenate(
                [np.fromfile(os.path.join(d, f"s.res.{k}"), "<i8") for k in (1, 2)])))
            check(len(spk) == len(res) and (spk == want).all(),
                  f"group {g}: all {len(res)} rows are the window at their .res time "
                  f"({int((gaps < NS).sum())} windows overlap the previous one)")

        # -R on a .res with times at both ends of the file
        times = np.array([3, TB, TB + 1, 5000, 90000, N - NS + TB, N - NS + TB + 1, N - 2], "<i8")
        inside = (times - TB >= 0) & (times - TB + NS <= N)
        for name, extra, kind in (("process_extractspikes", [], "raw"),
                                  ("process_extractspikes_sdiff", ["-d", "3"], "sdiff"),
                                  ("process_extractspikes_stderiv", ["-d", "3"], "stderiv")):
            print(f"{name} -R:")
            rd = os.path.join(d, name)
            os.mkdir(rd)
            os.symlink(os.path.join(d, "s.fil"), os.path.join(rd, "s.fil"))
            for g in (1, 2):
                times.tofile(os.path.join(rd, f"s.res.{g}"))
            check(run(BINS[name], common + extra + ["-R"], rd) == 0, "runs")
            for g, chans in enumerate(GROUPS, 1):
                spk = np.fromfile(os.path.join(rd, f"s.spk.{g}"), "<i2").reshape(-1, NS, 4)
                check(len(spk) == len(times), f"group {g}: {len(spk)} rows for {len(times)} times")
                if len(spk) != len(times):
                    continue
                check(not spk[~inside].any(), f"group {g}: the {int((~inside).sum())} windows "
                      "outside the .fil are zeros")
                ok = True
                for i in np.flatnonzero(inside):
                    ws = int(times[i]) - TB
                    if kind == "raw":
                        want = fil[ws:ws + NS][:, chans]
                    elif kind == "sdiff":
                        want = allpairs(fil[ws:ws + NS][:, chans])
                    else:
                        lo = max(ws - 1, 0)
                        sd = allpairs(fil[lo:ws + NS][:, chans])
                        prev = sd[0] if ws >= 1 else np.zeros(4, np.int64)
                        cur = sd[1:] if ws >= 1 else sd
                        want = np.clip(np.diff(np.vstack([prev, cur]), axis=0), -32768, 32767)
                    ok &= bool((spk[i] == want).all())
                check(ok, f"group {g}: the windows inside are the {kind} waveform at their time")

    print(f"\ntest_extract_alignment: {'FAIL' if fails else 'PASS'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
