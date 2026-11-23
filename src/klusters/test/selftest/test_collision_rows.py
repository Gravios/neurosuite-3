#!/usr/bin/env python3
"""Collision rows are served with the .eap partner subtracted; the .spk is never changed.

Two synthetic units of 400 spikes.  At 40 spikes of unit 2 a spike of unit 3 is added to the
RECORDING (unit 3's mean waveform, 0.9 of it, 9-13 samples later) and the .spk is cut again, and
the .eap says so: every row holds its own unit's class at offset 0, the 40 collision rows also the
other unit's class at an offset that is up to 2 samples off the true one.

Through the `collisionRows` step:

  - on open the 40 rows are found and corrected, with no partner left without a template;
  - a collision row is SERVED corrected -- far closer to the record it had before the partner was
    added than its raw record is -- while its RAW record is byte for byte the .spk's;
  - NEGATIVE CONTROLS: rows that are not collisions are served as the .spk holds them; a session
    whose .eap names no partner corrects nothing; a row whose .eap names a partner that is not in
    its record is not corrected;
  - the fit finds the partner where it is, not where the .eap says: the fitted offsets are the
    planted ones (the stored ones were written up to 2 samples off, either way), bar at most 2 of
    40 that land on the neighbouring sample;
  - the .spk on disk is unchanged after the session is closed;
  - with the preference off the raw records are served; on again, the corrected ones;
  - the sparse .spkc written from the session holds exactly the corrected rows and records;
  - after a NUDGE of unit 2 by one sample -- every one of its rows is cut again from the
    recording -- the 40 rows are corrected again, from the new records: each is served as the
    record the recording held there before the partner was added, one sample on; nothing of the
    earlier correction is served.
"""
import os
import struct
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402

fails = 0
ABSENT = -128


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def write_eap(path, cells, group=1):
    n, t = cells.shape
    with open(path, "wb") as fh:
        fh.write(b"EAP\x01" + struct.pack("<IIII", n, t, group, 0) + b"\x00" * 12)
        cells.astype(np.int8).tofile(fh)


def read_spkc(path):
    with open(path, "rb") as fh:
        magic = fh.read(4)
        n_rows, n_samp, n_ch, n_spikes, group, flags = struct.unpack("<IIIIII", fh.read(24))
        fh.read(4)
        rows = np.fromfile(fh, np.int64, n_rows)
        wav = np.fromfile(fh, np.int16).reshape(n_rows, n_samp * n_ch)
    return magic, n_spikes, n_samp, n_ch, rows, wav


def shift_rows(t, k):
    """t (nsamp, nch) moved k samples later, zero-filled."""
    out = np.zeros_like(t)
    if k > 0:
        out[k:] = t[:-k]
    elif k < 0:
        out[:k] = t[-k:]
    else:
        out[:] = t
    return out


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        nch = len(S.CHANS)
        base, m = S.make_session(d, "col", n_per_unit=400)
        clu = m["clu"]
        spk = m["spk"].reshape(-1, S.NSAMP, nch).astype(np.float64)
        n = len(clu)
        a, b = np.flatnonzero(clu == 2), np.flatnonzero(clu == 3)
        rng = np.random.default_rng(11)
        tb = spk[b].mean(0)
        coll = np.sort(rng.choice(a, 40, replace=False))
        true_tau = rng.integers(9, 14, len(coll))
        stored = true_tau + rng.integers(-2, 3, len(coll))
        before = spk.copy()
        res = m["res"]
        fil0 = m["fil"].copy()                           # the recording without the partners
        fil = m["fil"].astype(np.float64)
        add = 0.9 * tb
        for i, k in zip(coll, true_tau):
            s0 = int(res[i]) - (S.PEAK - 1) + int(k)
            fil[s0:s0 + S.NSAMP, S.CHANS] += add
        fil = np.clip(np.rint(fil), -32768, 32767).astype(np.int16)
        fil.tofile(base + ".fil")
        fil.tofile(base + ".dat")
        ghost = int(np.setdiff1d(a, coll)[5])            # the .eap names a partner; the record has none
        spk16 = S.cut(fil, res, S.CHANS, S.NSAMP, S.PEAK).reshape(-1, S.NSAMP, nch)
        untouched = np.setdiff1d(np.arange(n), coll)
        far = [i for i in untouched if np.array_equal(spk16[i], before[i].astype(np.int16))]
        spk_path = f"{base}.spk.standard.1"
        spk16.tofile(spk_path)
        on_disk = open(spk_path, "rb").read()

        cells = np.full((n, 4), ABSENT, np.int16)
        cells[a, 0] = 0
        cells[b, 1] = 0
        cells[coll, 1] = stored
        cells[ghost, 1] = 11
        write_eap(f"{base}.eap.1", cells)

        if ghost not in far:
            ghost = int([i for i in far if clu[i] == 2][5])
        clean = [int([i for i in far if clu[i] == 2 and i != ghost][0]),
                 int([i for i in far if clu[i] == 3][0]), int([i for i in far if clu[i] == 3][-1])]
        ask = [int(i) for i in coll] + [ghost] + clean
        spkc_path = os.path.join(d, "out.spkc")
        steps = [
            {"collisionRows": {"rows": ask, "writeSpkc": spkc_path}},
            {"collisionRows": {"prefs": {"enabled": False}, "refresh": True, "rows": ask[:3]}},
            {"collisionRows": {"prefs": {"enabled": True}, "refresh": True, "rows": ask[:3]}},
            {"nudge": [2, 1]},
            {"collisionRows": {"rows": [int(i) for i in coll]}},
        ]
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        st = rep.get("results", [])
        if rep.get("rc") != 0 or len(st) != len(steps):
            print(rep.get("stderr", "")[-3000:])
        check(rep.get("rc") == 0 and len(st) == len(steps), "the session opened and every step ran")
        if len(st) != len(steps):
            return 1
        r0 = st[0]
        check(r0.get("eap") == "col.eap.1" and r0.get("eapFound") and not r0.get("skipped"), "the stage's .eap was read",
              str({k: r0.get(k) for k in ("eap", "eapFound", "skipped")}))
        check(r0.get("collisionRows") == 41, "41 rows name two classes (40 collisions and the row with a partner that is not there)",
              str(r0.get("collisionRows")))
        check(r0.get("partnersWithoutTemplate") == 0 and r0.get("partnersFitted") == 41, "every partner had a template and was fitted",
              str({k: r0.get(k) for k in ("partnersWithoutTemplate", "partnersFitted")}))
        check(r0.get("corrected") == 40 and r0.get("partnersSubtracted") == 40 and r0.get("active") == 40,
              "the 40 collisions are corrected and served; the row without a partner in it is not",
              str({k: r0.get(k) for k in ("corrected", "partnersSubtracted", "active")}))
        recs = {r["row"]: r for r in r0.get("records", [])}
        raw_ok = all(np.array_equal(np.array(recs[i]["raw"], np.int16), spk16[i].ravel()) for i in ask)
        check(raw_ok, "the raw record of every row is the .spk's, byte for byte")
        err_raw = np.array([((spk16[i].ravel() - before[i].ravel()) ** 2).sum() for i in coll])
        err_cor = np.array([((np.array(recs[int(i)]["served"], float) - before[i].ravel()) ** 2).sum() for i in coll])
        ratio = err_cor / err_raw
        print(f"      corrected rows: error against the record before the partner was added, as a share of the raw "
              f"row's: median {np.median(ratio):.3f}, worst {ratio.max():.3f}")
        check(np.median(ratio) < 0.10 and ratio.max() < 0.35, "a collision row is served corrected (median error under a tenth of the raw row's)")
        same = all(recs[i]["served"] == recs[i]["raw"] for i in clean + [ghost])
        check(same, "NEGATIVE CONTROL: rows that are not collisions, and the row whose partner is not there, are served raw")
        want = [int(((true_tau - stored) == k).sum()) for k in range(-4, 5)]
        got = r0.get("offsetDeltas") or [0] * 9
        # one row moving to the neighbouring sample changes two bins: allow two such rows of 40
        check(sum(got) == 40 and sum(abs(g - w) for g, w in zip(got, want)) <= 4,
              "the fit places the partner where it IS, whatever the .eap said (up to 2 samples off, either way): "
              "at most 2 of 40 fitted offsets are not the planted one",
              f"fitted - stored, -4..+4: {r0.get('offsetDeltas')}, planted: {want}")
        check(open(spk_path, "rb").read() == on_disk, "the .spk on disk is unchanged (the nudge writes a pending copy; nothing was saved)")

        r1, r2 = st[1], st[2]
        off = all(x["served"] == x["raw"] for x in r1.get("records", []))
        check(r1.get("enabled") is False and r1.get("active") == 0 and off, "with the preference off the raw records are served",
              str({k: r1.get(k) for k in ("enabled", "active")}))
        on = all(x["served"] == recs[x["row"]]["served"] for x in r2.get("records", []))
        check(r2.get("enabled") is True and r2.get("active") == 40 and on, "on again: the same corrected records",
              str({k: r2.get(k) for k in ("enabled", "active")}))

        r4 = st[4]
        check(st[3].get("ok") is True and r4.get("corrected") == 40 and r4.get("active") == 40,
              "after a nudge of unit 2 by one sample the 40 rows are corrected again",
              str({k: r4.get(k) for k in ("corrected", "active", "partnersSubtracted")}))
        rec4 = {r["row"]: r for r in r4.get("records", [])}
        res1 = res.copy(); res1[clu == 2] += 1
        raw1 = S.cut(fil, res1, S.CHANS, S.NSAMP, S.PEAK)           # what the recording holds one sample on
        want1 = S.cut(fil0, res1, S.CHANS, S.NSAMP, S.PEAK)         # ... and held before the partners were added
        check(all(np.array_equal(np.array(rec4[int(i)]["raw"], np.int16), raw1[i]) for i in coll),
              "... their raw records are the recording's at the new times")
        e_raw = np.array([((raw1[i].astype(float) - want1[i]) ** 2).sum() for i in coll])
        e_cor = np.array([((np.array(rec4[int(i)]["served"], float) - want1[i]) ** 2).sum() for i in coll])
        e_old = np.array([((np.array(recs[int(i)]["served"], float) - want1[i]) ** 2).sum() for i in coll])
        print(f"      after the nudge: error as a share of the raw row's: median {np.median(e_cor / e_raw):.3f}, "
              f"worst {(e_cor / e_raw).max():.3f}; the records served BEFORE the nudge would be at {np.median(e_old / e_raw):.2f}")
        check(np.median(e_cor / e_raw) < 0.10 and (e_cor / e_raw).max() < 0.35,
              "... and they are served corrected from the NEW records")
        check(np.median(e_old / e_cor) > 3.0, "... not as corrected before the nudge (those records are several times further off)")

        check(r0.get("spkcWritten") is True and os.path.exists(spkc_path), "the .spkc was written")
        if os.path.exists(spkc_path):
            magic, n_spikes, n_samp, n_ch, rows, wav = read_spkc(spkc_path)
            check(magic == b"SPC\x01" and n_spikes == n and n_samp == S.NSAMP and n_ch == nch, ".spkc header: magic, spike count, geometry")
            check(np.array_equal(rows, coll), ".spkc index: exactly the corrected rows, ascending")
            check(all(np.array_equal(wav[k], np.array(recs[int(i)]["served"], np.int16)) for k, i in enumerate(coll)),
                  ".spkc records: the served records")

        # NEGATIVE CONTROL: an .eap that names no partner
        cells2 = np.full((n, 4), ABSENT, np.int16)
        cells2[a, 0] = 0
        cells2[b, 1] = 0
        write_eap(f"{base}.eap.1", cells2)
        rep2 = R.run(klusters, f"{base}.clu.standard.1", [{"collisionRows": {"rows": ask[:2]}}], d)
        s2 = (rep2.get("results") or [{}])[0]
        check(rep2.get("rc") == 0 and s2.get("collisionRows") == 0 and s2.get("active") == 0
              and all(x["served"] == x["raw"] for x in s2.get("records", [{}]) if "served" in x),
              "NEGATIVE CONTROL: an .eap without a partner anywhere corrects nothing")
        os.remove(f"{base}.eap.1")
        rep3 = R.run(klusters, f"{base}.clu.standard.1", [{"collisionRows": {}}], d)
        s3 = (rep3.get("results") or [{}])[0]
        check(rep3.get("rc") == 0 and s3.get("eapFound") is False and s3.get("active") == 0, "without an .eap nothing is corrected")

    print("test_collision_rows: " + ("FAIL (%d failed)" % fails if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
