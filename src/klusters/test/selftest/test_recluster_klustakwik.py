#!/usr/bin/env python3
"""A KlustaKwik recluster from Klusters: the time axis and the chunking preference.

Two clean units (400 spikes each) merged into one cluster, reclustered through Klusters' own slot with
KiloKlustaKwik_cpu (KKK_BIN, else the build tree's; skipped when there is none).

THE TIME AXIS.  KiloKlustaKwik reads spike time at the last SELECTED column and clusters on the columns
before it.  Klusters switches the time column off in its auto-select, channel-variance and residual
modes, so the last chosen feature was dropped from the clustering and read as time: a seven-way split of
two units with all twelve features, and thirty clusters in the median-residual mode, where the "time"
spanned 426 minutes and cut 800 spikes into 24 chunks.  The sorter now always selects the time column:

  - twelve features with the time bit off give what they give with it on: the two units, 381 spikes each;
  - Shift+C (channel variance) the same;
  - Shift+M (median residual) is sorted as one chunk into a handful of clusters, not dozens.

THE CHUNKING PREFERENCE (Preferences > Reclustering > Time chunks):

  - as the sorter decides: no flag; one chunk: -ChunkMinutes 1000000; chunks of N: -ChunkMinutes N;
    off: -ChunkMinutes 0 -- each on the command line Klusters ran, and in the sorter's own report
    (chunked with 1 chunk / with several / "two-phase");
  - a -ChunkMinutes written in the Arguments wins over the preference;
  - the classic KlustaKwik (no such flag) and a -ChunkFile template get nothing added.
"""
import os
import re
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402

T = "%fileBaseName %electrodeGroupID -MinClusters 2 -MaxClusters 12 -UseFeatures "
fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def sorter():
    p = os.environ.get("KKK_BIN", "")
    if p and os.path.exists(p):
        return p
    k = R.binary()
    for up in (3, 2, 4):
        cand = os.path.join(os.path.abspath(os.path.join(os.path.dirname(k), *[".."] * up)), "kiloklustakwik", "KiloKlustaKwik_cpu")
        if os.path.exists(cand):
            return cand
    return ""


def n_chunks(log):
    """The number of chunks the sorter cut the session into, from its "[Stage 1] Chunking (.., N
    chunks, ..)" line; 0 when the line is not there.  Not `"1 chunks" in log`: other lines hold a
    chunk count too (the sorter prints "DipSplit per-chunk: N splits across M chunks" and "noise --
    N spikes across M chunks"), and what they hold differs from run to run of a 3-chunk sort."""
    m = re.search(r"\[Stage 1\] Chunking \([^,]*, (\d+) chunks", log)
    return int(m.group(1)) if m else 0


def recluster(klusters, kkk, mode="", **opts):
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "kk", n_per_unit=400)
        clu = np.full_like(m["clu"], 2)                       # both units in one cluster
        with open(f"{base}.clu.standard.1", "wb") as fh:
            np.array([3], np.int32).tofile(fh)
            clu.astype(np.int32).tofile(fh)
        o = dict(exe=kkk, autoSelect=False)
        o.update(opts)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"select": [2]}, {"recluster": mode, "opts": o}, {"reclusterLog": True}, {"palette": True}], d, timeout=600)
        res = rep.get("results", [])
        lines = next((r["lines"] for r in res if "lines" in r), [])
        pal = next((r for r in res if "order" in r), None)
        sizes = sorted((v for k, v in pal["counts"].items()), reverse=True) if pal else []
        return (lines[0] if lines else ""), "\n".join(lines), sizes


def main():
    klusters = R.binary()
    R.skip_without_binary()
    kkk = sorter()
    if not kkk:
        print("test_recluster_klustakwik: SKIP (no KiloKlustaKwik_cpu: build it or set KKK_BIN)")
        return 0

    # ── the time axis ────────────────────────────────────────────────────────
    _, _, on = recluster(klusters, kkk, args=T + "1111111111111")
    cmd, log, off = recluster(klusters, kkk, args=T + "1111111111110")
    check(on[:2] == [381, 381], "12 features + time: the two units come back (381, 381)", str(on))
    check(off == on, "12 features with the time bit off: the same result as with it on", f"off {off}  on {on}")
    check(n_chunks(log) == 1 and "13 dims" in log, "  ... sorted as one chunk on 13 columns (12 features and the time axis)")
    _, log, ch = recluster(klusters, kkk, mode="channel")
    check(ch[:2] == [381, 381], "Shift+C (channel variance): the two units", str(ch))
    _, log, med = recluster(klusters, kkk, mode="median")
    check(n_chunks(log) == 1 and 2 <= len(med) <= 6, f"Shift+M (median residual): one chunk, {len(med)} clusters (was 24 chunks and 30 clusters)", str(med))

    # ── the chunking preference ─────────────────────────────────────────────
    F = T + "%features"
    cmd, log, _ = recluster(klusters, kkk, args=F, chunking=0)
    check("-ChunkMinutes" not in cmd and "mode: chunked" in log, "as the sorter decides: no flag, chunked")
    cmd, log, sz = recluster(klusters, kkk, args=F, chunking=1)
    check(cmd.endswith("-ChunkMinutes 1000000") and n_chunks(log) == 1 and sz[:2] == [381, 381], "one chunk: -ChunkMinutes 1000000, 1 chunk, the two units", cmd[-60:])
    cmd, log, _ = recluster(klusters, kkk, args=F, chunking=2, chunkMinutes=0.1)
    check(cmd.endswith("-ChunkMinutes 0.1") and "mode: chunked" in log and n_chunks(log) >= 2, "chunks of 0.1 min: the flag, and several chunks over 20 s", cmd[-60:])
    cmd, log, _ = recluster(klusters, kkk, args=F, chunking=3)
    check(cmd.endswith("-ChunkMinutes 0") and "two-phase" in log, "off: -ChunkMinutes 0, the two-phase pass", cmd[-60:])
    cmd, log, _ = recluster(klusters, kkk, args=F + " -ChunkMinutes 0.2", chunking=3)
    check(cmd.count("-ChunkMinutes") == 1 and cmd.endswith("-ChunkMinutes 0.2") and "two-phase" not in log, "a -ChunkMinutes in the Arguments wins over the preference", cmd[-60:])

    # ── the flag itself, without running anything ───────────────────────────
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "kk", n_per_unit=50)
        q = lambda exe, args, mode, minutes=7.0: {"reclusterChunkArgs": {"exe": exe, "args": args, "chunking": mode, "minutes": minutes}}   # noqa: E731
        rep = R.run(klusters, f"{base}.clu.standard.1", [
            q("/usr/bin/KlustaKwik", ["a", "1"], 1), q("KiloKlustaKwik_cuda", ["a", "1", "-ChunkFile", "x"], 3),
            q("/opt/KiloKlustaKwik_hip", ["a"], 2, 12.5), q("kiloklustakwik_cpu", ["a"], 3), q("KiloKlustaKwik_cpu", ["a"], 0)], d)
        got = [r["chunkArgs"] for r in rep.get("results", []) if "chunkArgs" in r]
        check(got == [[], [], ["-ChunkMinutes", "12.5"], ["-ChunkMinutes", "0"], []],
              "flag only for KiloKlustaKwik*: none for the classic program, none over -ChunkFile, N as typed, 0 for off", str(got))

    print(f"test_recluster_klustakwik: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
