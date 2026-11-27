#!/usr/bin/env python3
"""A residual spike's row is re-cut from its extended record (.spkx), never from the recording.

A row Add Residual Spikes makes is the window at its time LESS the collision row's own unit: the
recording does not hold it.  Every tool that moves a spike cut it again from the recording, so a
realign of the new cluster stopped at its raw-source check (a residual row is no window of the
recording), and a nudge or a Re-extract on Save would have put back the mixed waveform.  The stage
now carries a .spkx: each new row's record over the window and half a window either side.

The session of test_residual_spikes (cluster 2 marked, its residual spikes of unit 3 added as
cluster 4 in the stage <name>.rs):

  - the stage's .spkx names the new rows, with their times and the rows they were found in, a
    margin of half a window, and the middle of each record is the row's .spk record;
  - NEGATIVE CONTROL: without the .spkx a realign of cluster 4 stops at the raw-source check and
    changes nothing;
  - a nudge of cluster 4 by 3 samples: each new row is its extended record 3 samples on, the old
    rows are as they were;
  - a realign of cluster 4 then passes the check, says the rows were re-cut from their extended
    record, and brings them back to where they were made (within a sample); each is its extended
    record at its new time;
  - Re-extract on Save: the new rows are still their extended records -- NOT the recording's
    windows at their times (NEGATIVE CONTROL: those differ by far) -- the old rows the recording's;
  - Sort Spikes by Time, after a nudge that puts new rows past the rows they were found in,
    reorders the .spkx with the rows: each entry still names a row of cluster 4 whose record is
    its extended record at the row's time, and its source a row of cluster 2.
"""
import os
import shutil
import struct
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402
import test_mark_collisions as M          # noqa: E402

check = M.check
NCH = len(S.CHANS)
REC = S.NSAMP * NCH


def read_spkx(path):
    b = open(path, "rb").read()
    assert b[:4] == b"SPX\x01", "bad magic"
    n_rows, n_samp, n_chan, n_spikes, group, margin, _flags = struct.unpack_from("<7I", b, 4)
    at = 32
    rows = np.frombuffer(b, np.int64, n_rows, at); at += 8 * n_rows
    times = np.frombuffer(b, np.int64, n_rows, at); at += 8 * n_rows
    sources = np.frombuffer(b, np.int64, n_rows, at); at += 8 * n_rows
    ext = n_samp + 2 * margin
    body = np.frombuffer(b, np.int16, n_rows * ext * n_chan, at).reshape(n_rows, ext, n_chan)
    return dict(rows=rows, times=times, sources=sources, nSamples=n_samp, nChannels=n_chan, nSpikes=n_spikes,
                group=group, margin=margin, body=body)


def slice_at(x, k, t):
    """Entry k's .spk record for its row at time t (None beyond the margin)."""
    d = int(t) - int(x["times"][k])
    if abs(d) > x["margin"]:
        return None
    return x["body"][k, d + x["margin"]:d + x["margin"] + x["nSamples"]].reshape(-1)


def stage(base, tag):
    return dict(clu=S.read_clu(f"{base}.clu.standard.1.{tag}"), res=S.read_res(f"{base}.res.standard.1.{tag}"),
                spk=S.read_spk(f"{base}.spk.standard.1.{tag}", S.NSAMP, NCH))


def as_extended(x, g):
    """How many entries' rows are their extended record at the row's time, of how many."""
    good = 0
    for k, r in enumerate(x["rows"]):
        s = slice_at(x, k, g["res"][r])
        good += s is not None and np.array_equal(g["spk"][r], s)
    return good, len(x["rows"])


def run_on(klusters, clu, steps, d, **prefs):
    rep = R.run(klusters, clu, steps, d, **prefs)
    st = rep.get("results", [])
    ok = rep.get("rc") == 0 and len(st) == len(steps)
    if not ok:
        print(rep.get("stderr", "")[-3000:])
    return ok, st


def realign_log(rep):
    return next((r for r in rep.get("results", []) if "realignDone" in r), {})


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "rx")
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}}, {"save": True}], d)
        ok2, st2 = M.run(klusters, base, [{"residualSpikes": {"clusters": [2], "apply": True}}], d) if ok else (False, [])
        check(ok and ok2 and st2[0].get("written") is True, "cluster 2 marked, its residual spikes added as the stage .rs")
        if not (ok and ok2):
            return 1
        spikes = st2[0]["spikes"]
        stage_clu = f"{base}.clu.standard.1.rs"
        spkx = f"{base}.spkx.standard.1.rs"
        g0 = stage(base, "rs")
        new = np.flatnonzero(g0["clu"] == 4)
        old = np.flatnonzero(g0["clu"] != 4)

        # ── the .spkx the stage carries ──
        check(os.path.exists(spkx), "the stage carries a .spkx")
        if not os.path.exists(spkx):
            return 1
        x = read_spkx(spkx)
        src = {s["time"]: s["sourceRow"] for s in spikes}
        check(np.array_equal(x["rows"], new) and x["nSpikes"] == len(g0["clu"]) and x["nSamples"] == S.NSAMP
              and x["nChannels"] == NCH and x["margin"] == S.NSAMP // 2 and x["group"] == 1,
              "it names the new rows, of the stage's rows and geometry, a margin of half a window",
              str((len(x["rows"]), len(new), x["nSpikes"], x["nSamples"], x["nChannels"], x["margin"])))
        check(np.array_equal(x["times"], g0["res"][new]), "... each with its row's time")
        check(all(int(old[src[int(t)]]) == int(s) for t, s in zip(x["times"], x["sources"])),
              "... and the row it was found in (in the stage's numbering)")
        good, tot = as_extended(x, g0)
        check(good == tot == len(new), "the middle of each record is the row's .spk record", f"{good} of {tot}")
        print(f"      {len(new)} new rows (cluster 4), margin {x['margin']} samples")

        # ── NEGATIVE CONTROL: without it a realign stops at the raw-source check ──
        hidden = spkx + ".hidden"
        shutil.move(spkx, hidden)
        snap = {p: open(p, "rb").read() for p in (f"{base}.res.standard.1.rs", f"{base}.spk.standard.1.rs")}
        rep = R.run(klusters, stage_clu, [{"realign": 4}], d, realignFromAnySpace=True)
        r = realign_log(rep)
        check(r.get("realignDone") is False and "raw source verification FAILED" in r.get("log", ""),
              "NEGATIVE CONTROL: without the .spkx the realign of cluster 4 stops at the raw-source check",
              r.get("log", "")[-500:])
        check(all(open(p, "rb").read() == v for p, v in snap.items()), "... and changes nothing")
        shutil.move(hidden, spkx)

        # ── a nudge of cluster 4: from the extended records ──
        ok, _ = run_on(klusters, stage_clu, [{"nudge": [4, 3]}, {"save": True}], d)
        g1 = stage(base, "rs")
        check(ok and np.array_equal(g1["res"][new], g0["res"][new] + 3) and np.array_equal(g1["res"][old], g0["res"][old]),
              "a nudge of cluster 4 by 3 samples moves its rows, and only them")
        good, tot = as_extended(x, g1)
        check(good == tot, "each new row is its extended record 3 samples on", f"{good} of {tot}")
        check(np.array_equal(g1["spk"][old], g0["spk"][old]), "the old rows are as they were")

        # ── a realign of cluster 4: past the check, back to where they were made ──
        rep = R.run(klusters, stage_clu, [{"realign": 4}, {"save": True}], d, realignFromAnySpace=True)
        r = realign_log(rep)
        log = r.get("log", "")
        g2 = stage(base, "rs")
        back = np.abs(g2["res"][new] - g0["res"][new])
        print(f"      realign: {int((g2['res'][new] != g1['res'][new]).sum())} of {len(new)} rows moved; "
              f"distance from where they were made: median {np.median(back):.0f}, max {back.max()}")
        check(r.get("realignDone") is True and "verification FAILED" not in log and "Raw source verified" in log,
              "the realign of cluster 4 passes the raw-source check", log[-600:])
        check("re-cut from their extended record" in log, "... and says its rows are re-cut from their extended record")
        check(np.median(back) <= 1, "... and brings them back to where they were made (within a sample)", str(np.bincount(back)))
        good, tot = as_extended(x, g2)
        check(good == tot, "each is its extended record at its new time", f"{good} of {tot}")
        check(np.array_equal(g2["spk"][old], g0["spk"][old]) and np.array_equal(g2["res"][old], g0["res"][old]),
              "the old rows are as they were")

        # ── Re-extract on Save ──
        ok, _ = run_on(klusters, stage_clu, [{"nudge": [3, 1]}, {"save": True}], d, reextractOnSave=True)
        g3 = stage(base, "rs")
        filraw = np.fromfile(base + ".fil", np.int16).reshape(-1, S.NTOT)
        cut = S.cut(filraw, g3["res"], S.CHANS, S.NSAMP, S.PEAK).reshape(len(g3["res"]), -1)
        good, tot = as_extended(x, g3)
        check(ok and good == tot, "Re-extract on Save: the new rows are still their extended records", f"{good} of {tot}")
        far = [float(((cut[i].astype(float) - g3["spk"][i]) ** 2).sum() / max(1.0, (g3["spk"][i].astype(float) ** 2).sum()))
               for i in new]
        check(np.median(far) > 0.5, "NEGATIVE CONTROL: the recording's windows at their times are far from them",
              f"median relative difference {np.median(far):.2f}")
        check(np.array_equal(g3["spk"][old], cut[old]), "... and the old rows are the recording's windows")

        # ── Sort Spikes by Time ──
        ok, _ = run_on(klusters, stage_clu, [{"nudge": [4, 12]}, {"save": True}], d)
        g4 = stage(base, "rs")
        disorder = int((np.diff(g4["res"]) < 0).sum())
        ok2, st = run_on(klusters, stage_clu, [{"timeSort": {}}], d) if ok else (False, [])
        ts = st[0] if st else {}
        check(ok and disorder > 0 and ok2 and ts.get("sorted") == 1 and os.path.basename(spkx) in ts.get("files", []),
              "a nudge past the rows they were found in, then Sort Spikes by Time rewrites the .spkx with the rows",
              str((disorder, {k: ts.get(k) for k in ("sorted", "files", "skipped", "error")})))
        g5 = stage(base, "rs")
        x5 = read_spkx(spkx)
        good, tot = as_extended(x5, g5)
        check(bool(np.all(np.diff(g5["res"]) >= 0)) and tot == len(new) and np.array_equal(x5["rows"], np.flatnonzero(g5["clu"] == 4))
              and good == tot, "after the sort each entry names a row of cluster 4 whose record is its extended record at its time",
              f"{good} of {tot}")
        check(bool(np.all(g5["clu"][x5["sources"]] == 2)), "... and its source a row of cluster 2")

        # ── a stage made from this one carries its extended records ──
        ok, st = run_on(klusters, stage_clu, [{"markCollisions": {"clusters": [3], "apply": True}}, {"save": True},
                                             {"residualSpikes": {"clusters": [3], "apply": True}}], d)
        made = st[2] if ok else {}
        check(ok and made.get("written") is True and os.path.exists(f"{base}.spkx.standard.1.rs_rs"),
              "residual spikes of cluster 3 added from the stage: the stage .rs_rs, with a .spkx", str(made.get("reason")))
        if ok and os.path.exists(f"{base}.spkx.standard.1.rs_rs"):
            g6 = dict(clu=S.read_clu(f"{base}.clu.standard.1.rs_rs"), res=S.read_res(f"{base}.res.standard.1.rs_rs"),
                      spk=S.read_spk(f"{base}.spk.standard.1.rs_rs", S.NSAMP, NCH))
            x6 = read_spkx(f"{base}.spkx.standard.1.rs_rs")
            fresh = made.get("newClusters", {}).get("2")
            carried = np.flatnonzero(g6["clu"] == 4)
            added = np.flatnonzero(g6["clu"] == fresh) if fresh else np.array([], int)
            good, tot = as_extended(x6, g6)
            print(f"      .rs_rs: {len(carried)} carried rows of cluster 4, {len(added)} new rows of cluster {fresh}; "
                  f"{tot} entries in its .spkx")
            check(len(added) > 0 and np.array_equal(x6["rows"], np.sort(np.concatenate([carried, added])))
                  and good == tot, "its .spkx holds the carried rows (renumbered) and the new ones, each its extended record",
                  f"{good} of {tot}")

    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
