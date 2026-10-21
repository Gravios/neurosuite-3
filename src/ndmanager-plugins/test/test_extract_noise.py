"""-R -N: the three extractors write each group's noise statistics (.nst).

On a synthetic .fil (two 4-channel groups, Gaussian noise, detected spikes,
UNDETECTED transients on one channel and a zero-filled gap), for each of
process_extractspikes (raw), _sdiff and _stderiv:

  * -N leaves the .spk byte-identical to a run without it;
  * every group gets a valid NSTE v1 .nst with the group's geometry;
  * the gap is rejected as flat and the transients as outliers;
  * the covariance matches the one computed here from clean spike-free windows
    in the same transform (raw / all-pairs / all-pairs + temporal difference),
    which is what a noise-only row of that .spk looks like;
  * -N without -R, and a bad -E / -G value, are refused.

ctest passes the binaries as PROCESS_EXTRACTSPIKES_BIN,
PROCESS_EXTRACTSPIKES_SDIFF_BIN and PROCESS_EXTRACTSPIKES_STDERIV_BIN.
"""
import os
import struct
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
TB = P - 1
GROUPS = [[0, 1, 2, 3], [4, 5, 6, 7]]
GAP = (150000, 153000)
fails = 0


def check(ok, what):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what)
    fails += not ok


def make_fil(path):
    rng = np.random.default_rng(11)
    T = 20 * 20000
    x = rng.normal(0, 30, (T, NCH))
    t = np.arange(-10, 22)
    w = -np.exp(-0.5 * (t / 2.0) ** 2) + 0.4 * np.exp(-0.5 * ((t - 7) / 4.0) ** 2)
    spikes = []
    for depth, n in (([700, 400, 150, 50, 0, 0, 0, 0], 600), ([0, 0, 0, 0, 600, 600, 200, 0], 700)):
        for s in np.sort(rng.integers(100, T - 100, n)):
            x[s - 10:s + 22] += np.outer(w, depth)
            spikes.append(int(s))
    trans = np.arange(1000, T - 1000, 1601)          # undetected, channel 1 and 5 only
    x[trans, 1] += 900
    x[trans, 5] += 900
    x[GAP[0]:GAP[1]] = 0
    fil = np.clip(np.rint(x), -32768, 32767).astype("<i2")
    fil.tofile(path)
    return fil, np.array(sorted(set(spikes)), "<i8"), trans


def clamp(v):
    return np.clip(np.where(v >= 0, np.floor(v + 0.5), np.ceil(v - 0.5)), -32768, 32767).astype(np.int64)


def allpairs(rows):
    n = rows.shape[-1]
    return clamp(n * rows.astype(np.float64) - rows.sum(axis=-1, keepdims=True))


def window(fil, t, chans, kind):
    ws = int(t) - TB
    if kind == "raw":
        return fil[ws:ws + NS][:, chans].astype(np.int64)
    if kind == "sdiff":
        return allpairs(fil[ws:ws + NS][:, chans])
    sd = allpairs(fil[ws - 1:ws + NS][:, chans])
    return np.clip(np.diff(sd, axis=0), -32768, 32767)


def read_nst(path):
    b = open(path, "rb").read()
    h = struct.unpack("<8i", b[:32])
    cnt = struct.unpack("<5q", b[32:72])
    zmax, seed = struct.unpack("<2d", b[72:88])
    ns, nc = h[2], h[3]
    D = ns * nc
    body = np.frombuffer(b, "<f8", offset=88)
    ok = body.size == nc + D + D * D
    sig = body[:nc]
    mean = body[nc:nc + D]
    cov = body[nc + D:].reshape(D, D) if ok else None
    return dict(magic=h[0], version=h[1], nsamp=ns, nchan=nc, peak=h[4], guard=h[5], passes=h[6],
                requested=cnt[0], drawn=cnt[1], kept=cnt[2], flat=cnt[3], outlier=cnt[4],
                zmax=zmax, sigma=sig, mean=mean, cov=cov, size_ok=ok)


def run(bin_, args, cwd):
    return subprocess.run([bin_] + args + ["s"], cwd=cwd, capture_output=True, text=True)


def main():
    with tempfile.TemporaryDirectory() as d:
        fil, spikes, trans = make_fil(os.path.join(d, "s.fil"))
        N = len(fil)
        common = ["-n", str(NCH), "-c", "0,1,2,3:4,5,6,7", "-w", str(NS), "-p", str(P)]
        # clean reference times: clear of spikes, transients and the gap (+ guard)
        grid = np.arange(TB + 1 + 64, N - NS - 64, 37)
        bad = np.concatenate([spikes, trans, np.arange(GAP[0], GAP[1], 16)])
        bad.sort()
        lo = np.searchsorted(bad, grid - 2 * NS - 2)
        hi = np.searchsorted(bad, grid + 2 * NS + 2)
        clean = grid[hi == lo]

        for name, extra, kind in (("process_extractspikes", [], "raw"),
                                  ("process_extractspikes_sdiff", ["-d", "3"], "sdiff"),
                                  ("process_extractspikes_stderiv", ["-d", "3"], "stderiv")):
            print(f"{name} -R -N:")
            outs = {}
            for tag, flags in (("plain", []), ("noise", ["-N", "4000"])):
                rd = os.path.join(d, f"{name}.{tag}")
                os.mkdir(rd)
                os.symlink(os.path.join(d, "s.fil"), os.path.join(rd, "s.fil"))
                for g in (1, 2):
                    spikes.tofile(os.path.join(rd, f"s.res.{g}"))
                r = run(BINS[name], common + extra + ["-R"] + flags, rd)
                check(r.returncode == 0, f"runs ({tag})")
                if r.returncode:
                    print(r.stdout[-1500:], r.stderr[-1500:])
                outs[tag] = rd
            for g, chans in enumerate(GROUPS, 1):
                a = open(os.path.join(outs["plain"], f"s.spk.{g}"), "rb").read()
                b = open(os.path.join(outs["noise"], f"s.spk.{g}"), "rb").read()
                check(a == b, f"group {g}: -N leaves the .spk byte-identical")
                check(not os.path.exists(os.path.join(outs["plain"], f"s.nst.{g}")),
                      f"group {g}: no .nst without -N")
                p = os.path.join(outs["noise"], f"s.nst.{g}")
                if not os.path.exists(p):
                    check(False, f"group {g}: .nst written")
                    continue
                s = read_nst(p)
                check(s["magic"] == 0x4E535445 and s["version"] == 1 and s["size_ok"],
                      f"group {g}: NSTE v1, size consistent")
                check((s["nsamp"], s["nchan"], s["peak"]) == (NS, 4, TB),
                      f"group {g}: geometry {s['nsamp']}x{s['nchan']}, peak {s['peak']}")
                check(s["requested"] == 4000 and s["drawn"] == 4000,
                      f"group {g}: {s['drawn']} of {s['requested']} windows drawn")
                check(s["flat"] > 0, f"group {g}: zero-filled gap rejected as flat ({s['flat']})")
                check(s["outlier"] > 0, f"group {g}: transients rejected as outliers ({s['outlier']})")
                W = np.stack([window(fil, t, chans, kind) for t in clean]).reshape(len(clean), -1)
                C = np.cov(W.astype(np.float64), rowvar=False)
                dv = np.abs(np.diag(s["cov"]) / np.diag(C) - 1.0)
                rc = s["cov"] / np.sqrt(np.outer(np.diag(s["cov"]), np.diag(s["cov"])))
                rr = C / np.sqrt(np.outer(np.diag(C), np.diag(C)))
                check(np.median(dv) < 0.05 and dv.max() < 0.2,
                      f"group {g}: {kind} variances match clean windows "
                      f"(median {np.median(dv):.3f}, max {dv.max():.3f})")
                # ~4,000 windows: one correlation's sampling SD is ~0.016, so the max over
                # the ~8,000 pairs sits near 0.07-0.09 by chance alone.
                dr = np.abs(rc - rr)[np.triu_indices(len(rc), 1)]
                check(dr.mean() < 0.02 and dr.max() < 0.12,
                      f"group {g}: {kind} correlations match clean windows "
                      f"(mean diff {dr.mean():.4f}, max {dr.max():.3f})")
                if kind != "raw":   # the comparison can tell a wrong transform apart
                    Wr = np.stack([window(fil, t, chans, "raw") for t in clean]).reshape(len(clean), -1)
                    Cr = np.corrcoef(Wr.astype(np.float64), rowvar=False)
                    check(np.abs(rc - Cr)[np.triu_indices(len(rc), 1)].max() > 0.3,
                          f"group {g}: the raw transform's correlations do NOT match ({kind} .nst)")

        print("refusals:")
        rd = os.path.join(d, "refuse")
        os.mkdir(rd)
        os.symlink(os.path.join(d, "s.fil"), os.path.join(rd, "s.fil"))
        for g in (1, 2):
            spikes.tofile(os.path.join(rd, f"s.res.{g}"))
        for name in BINS:
            check(run(BINS[name], common + ["-N", "100"], rd).returncode != 0, f"{name}: -N without -R refused")
            check(run(BINS[name], common + ["-R", "-N", "100", "-E", "0"], rd).returncode != 0,
                  f"{name}: -E 0 refused")
            check(run(BINS[name], common + ["-R", "-N", "100", "-G", "x"], rd).returncode != 0,
                  f"{name}: -G x refused")

    print(f"\ntest_extract_noise: {'FAIL' if fails else 'PASS'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
