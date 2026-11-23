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
    earlier correction is served;
  - the offsets FOLLOW the moves: after four more nudges of unit 2 (five samples in all, more than
    the fit searches) and two of unit 3, the partner's unit, all 40 rows are still corrected -- the
    search starts from the stored offset less the row's move plus the partner unit's;
  - at Save the fitted offsets are written to the .eap, relative to the saved times, and the
    .spkc is written; rebuilt from the saved files, the 40 rows are corrected with (at least 38 of)
    the fitted offsets exactly where the .eap now says, and no row counts as moved; the saved .spk
    is the raw cut of the recording at the saved times; a partner the fit never placed has its
    offset carried to the saved times as well;
  - FEATURES: a corrected row's feature values in memory are the projection of its corrected
    record onto the space's PCA basis -- close to where the row sat before the partner was added,
    far from where the collision put it; other rows keep the file's values; with the preference
    off the file's values are back, exactly; after the nudges they are the projection of the new
    corrected records; the saved .fet holds the projection of the RAW records (nothing corrected
    is written to it);
  - closed and opened again in one process, with the files rewritten in between (unit 2 realigned
    8 samples later by a tool: times, records, features and .eap offsets), the 40 rows are
    corrected on the new files and none counts as moved -- nothing held for the closed document
    (the times its offsets were for, feature values put aside) reaches the new one;
  - Save As under a new stage name copies the stage's own .eap to the new name (with the
    preference on or off), so the new stage does not fall back to the base .eap;
  - the application rebuilds by itself -- on open, per nudge, at Save, and after an edit of the
    clustering (a core cut); the step only reports, so a missing hook would show.
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
        # the .fet as a pipeline leaves it: the projection of the records the .spk holds
        means, evecs = S.write_pca(os.path.join(d, "again.pca"), m["spk"], nch, S.NSAMP, S.NCOMP)
        proj = lambda w: S.project(np.asarray(w).reshape(-1, S.NSAMP * nch), means, evecs, nch, S.NSAMP, S.NCOMP)   # noqa: E731
        fet_raw = proj(spk16)
        fet_path = f"{base}.fet.standard.1"
        with open(fet_path, "wb") as fh:
            np.array([fet_raw.shape[1] + 1], np.int32).tofile(fh)
            np.concatenate([fet_raw, res[:, None]], axis=1).astype(np.int64).tofile(fh)
        fet_before = proj(before)
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
            {"nudge": [2, 1]}, {"nudge": [2, 1]}, {"nudge": [2, 1]}, {"nudge": [2, 1]},
            {"collisionRows": {"rows": [int(i) for i in coll]}},
            {"nudge": [3, -1]}, {"nudge": [3, -1]},
            {"collisionRows": {"rows": [int(i) for i in coll]}},
            {"save": True},
            {"collisionRows": {}},
            {"coreCut": [3], "onsetFactor": 1.5},
            {"collisionRows": {"rows": [int(i) for i in coll[:5]]}},
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
        saved_spk = np.fromfile(spk_path, np.int16).reshape(-1, S.NSAMP * nch)
        res_s = res.copy(); res_s[clu == 2] += 5; res_s[clu == 3] -= 2
        with open(fet_path, "rb") as fh:
            nd = int(np.fromfile(fh, np.int32, 1)[0]); saved_fet = np.fromfile(fh, np.int64).reshape(-1, nd)
        check(np.array_equal(saved_fet[coll, :-1], proj(saved_spk[coll])),
              "the saved .fet holds the projection of the RAW records for the collision rows: corrected features stay in memory")
        check(np.array_equal(saved_spk, S.cut(fil, res_s, S.CHANS, S.NSAMP, S.PEAK)),
              "the saved .spk is the RAW cut of the recording at the saved times: no corrected record was written into it")

        # features
        f0 = {i: np.array(recs[i]["features"], np.int64) for i in ask}
        check(r0.get("featuresProjected") == 40 and not r0.get("featuresSkipped"), "the 40 corrected rows had their features re-projected",
              str({k: r0.get(k) for k in ("featuresProjected", "featuresSkipped")}))
        check(all(np.array_equal(f0[int(i)], proj(np.array(recs[int(i)]["served"], np.int16))[0]) for i in coll),
              "a corrected row's features are the projection of its corrected record, exactly")
        d_raw = np.array([np.abs(fet_raw[i] - fet_before[i]).sum() for i in coll], float)
        d_cor = np.array([np.abs(f0[int(i)] - fet_before[i]).sum() for i in coll], float)
        print(f"      features: distance to where the row sat before the partner, as a share of the collision's: "
              f"median {np.median(d_cor / d_raw):.3f}, worst {(d_cor / d_raw).max():.3f}")
        check(np.median(d_cor / d_raw) < 0.15 and (d_cor / d_raw).max() < 0.5,
              "... which puts it back near where the row sat before the partner was added")
        check(all(np.array_equal(f0[i], fet_raw[i]) for i in clean + [ghost]),
              "NEGATIVE CONTROL: rows that are not corrected keep the file's feature values")
        r1, r2 = st[1], st[2]
        off = all(x["served"] == x["raw"] for x in r1.get("records", []))
        check(r1.get("enabled") is False and r1.get("active") == 0 and off, "with the preference off the raw records are served",
              str({k: r1.get(k) for k in ("enabled", "active")}))
        check(all(np.array_equal(np.array(x["features"], np.int64), fet_raw[x["row"]]) for x in r1.get("records", []))
              and r1.get("featuresRestored") == 40,
              "... and the feature values are the file's again, exactly (40 rows restored)", str(r1.get("featuresRestored")))
        check(all(x["features"] == recs[x["row"]]["features"] for x in r2.get("records", [])),
              "on again: the corrected rows' features are the projection again")
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

        # five samples on: beyond the fit's reach unless the offsets follow the row
        r9 = st[9]
        res5 = res.copy(); res5[clu == 2] += 5
        want5 = S.cut(fil0, res5, S.CHANS, S.NSAMP, S.PEAK); raw5 = S.cut(fil, res5, S.CHANS, S.NSAMP, S.PEAK)
        rec9 = {r["row"]: r for r in r9.get("records", [])}
        e_raw = np.array([((raw5[i].astype(float) - want5[i]) ** 2).sum() for i in coll])
        e_cor = np.array([((np.array(rec9[int(i)]["served"], float) - want5[i]) ** 2).sum() for i in coll])
        print(f"      five samples on: corrected {r9.get('corrected')}, rows moved {r9.get('rowsMoved')}, "
              f"error share median {np.median(e_cor / e_raw):.3f}, worst {(e_cor / e_raw).max():.3f}")
        check(r9.get("corrected") == 40 and r9.get("rowsMoved") == 41 and r9.get("classesMoved") == 0,
              "after five nudges of unit 2 (5 samples, more than the fit searches) the 40 rows are still corrected",
              str({k: r9.get(k) for k in ("corrected", "rowsMoved", "classesMoved", "offsetDeltas")}))
        check(np.median(e_cor / e_raw) < 0.10, "... to the records the recording held there before the partners were added")
        check(all(np.array_equal(np.array(rec9[int(i)]["features"], np.int64), proj(np.array(rec9[int(i)]["served"], np.int16))[0])
                  for i in coll) and r9.get("featuresProjected") == 40,
              "... and their features are the projection of the NEW corrected records (the nudge had rewritten them raw)")
        r12 = st[12]
        check(r12.get("corrected") == 40 and r12.get("classesMoved") == 1,
              "after two nudges of unit 3 -- the PARTNER's unit -- they are still corrected, the partner's move counted",
              str({k: r12.get(k) for k in ("corrected", "rowsMoved", "classesMoved", "offsetDeltas")}))
        r14 = st[14]
        d14 = r14.get("offsetDeltas") or [0] * 9
        check(r14.get("corrected") == 40 and r14.get("rowsMoved") == 0 and d14[4] >= 38 and sum(d14) == 40,
              "after Save the .eap holds the fitted offsets for the saved times: nothing has moved and the fits are where it says",
              str({k: r14.get(k) for k in ("corrected", "rowsMoved", "classesMoved", "offsetDeltas")}))
        with open(f"{base}.eap.1", "rb") as fh:
            fh.read(32)
            saved = np.fromfile(fh, np.int8).reshape(n, 4)
        want_tau = true_tau - 5 - 2                        # the row 5 later, the partner's unit 2 earlier
        got_tau = saved[coll, 1].astype(int)
        check((np.abs(got_tau - want_tau) <= 1).all() and (got_tau != want_tau).sum() <= 2,
              "the saved .eap: partner offsets = planted - 5 (the row's move) - 2 (the partner unit's), at most 2 of 40 a sample off",
              f"saved {got_tau.tolist()} planted-7 {want_tau.tolist()}")
        check((saved[a, 0] == 0).all() and (saved[b, 1] == 0).all(), "... own classes are untouched")
        check(saved[ghost, 1] == 11 - 5 - 2,
              "... and a partner the fit never placed has its offset carried to the saved times too (11 - 5 - 2)",
              str(int(saved[ghost, 1])))
        # the application rebuilds by itself: the step only reports
        nb = [st[k].get("builds") for k in (0, 4, 9, 12, 14)]
        check(nb[0] == 1 and nb[1] == nb[0] + 2 and nb[2] == nb[1] + 4 and nb[3] == nb[2] + 2 and nb[4] == nb[3] + 1,
              "rebuilds: one on open, one when the preference comes back on, one per nudge, one at Save -- none made by the step",
              str(nb))
        r16 = st[16]
        cut = st[15]
        print(f"      core cut of cluster 3: {str(cut)[:160]}")
        check(r16.get("builds", 0) > nb[4] and r16.get("outdated") is False and r16.get("corrected") == 40,
              "after an EDIT of the clustering (a core cut of the partner's unit) the rows are rebuilt on the new clustering",
              str({k: r16.get(k) for k in ("builds", "outdated", "corrected")}))
        sp = f"{base}.spkc.standard.1"
        check(os.path.exists(sp) and np.array_equal(read_spkc(sp)[4], coll), "Save wrote the sparse .spkc with the 40 rows")

        check(r0.get("spkcWritten") is True and os.path.exists(spkc_path), "the .spkc was written")
        if os.path.exists(spkc_path):
            magic, n_spikes, n_samp, n_ch, rows, wav = read_spkc(spkc_path)
            check(magic == b"SPC\x01" and n_spikes == n and n_samp == S.NSAMP and n_ch == nch, ".spkc header: magic, spike count, geometry")
            check(np.array_equal(rows, coll), ".spkc index: exactly the corrected rows, ascending")
            check(all(np.array_equal(wav[k], np.array(recs[int(i)]["served"], np.int16)) for k, i in enumerate(coll)),
                  ".spkc records: the served records")

        # the first run saved: put the session's files back as they were
        spk16.tofile(spk_path)
        res.tofile(f"{base}.res.standard.1")
        with open(fet_path, "wb") as fh:
            np.array([fet_raw.shape[1] + 1], np.int32).tofile(fh)
            np.concatenate([fet_raw, res[:, None]], axis=1).astype(np.int64).tofile(fh)
        for stale in (f"{base}.spkc.standard.1",):
            if os.path.exists(stale):
                os.remove(stale)

        # Close and open again in one process, the files rewritten in between as a tool would:
        # unit 2 realigned 8 samples later -- its times, its records and features cut again, and
        # the .eap's offsets (relative to the row's time) 8 less.  What Klusters held for the
        # closed document -- the times its offsets were for, by row -- is not of this one: kept,
        # every row of unit 2 would count as moved by 8 and each partner be sought 8 samples off.
        write_eap(f"{base}.eap.1", cells)
        crows = [int(i) for i in coll]
        res8 = res.copy()
        gap = np.append(np.diff(res), 10**6)
        late = a[gap[a] > 8]                             # the rows of unit 2 that stay before the next spike
        res8[late] += 8
        check(bool(np.all(np.diff(res8) >= 0)) and np.isin(coll, late).all() and len(late) > 0.95 * len(a),
              "fixture: the rewritten times are in order, with every collision row and nearly all of unit 2 moved",
              f"{len(late)} of {len(a)}")
        spk8 = S.cut(fil, res8, S.CHANS, S.NSAMP, S.PEAK).reshape(-1, S.NSAMP * nch)
        cells8 = cells.copy()
        cells8[coll, 1] = stored - 8
        cells8[ghost, 1] = 3
        alt = {k: os.path.join(d, "alt." + k) for k in ("res", "spk", "fet", "eap")}
        res8.tofile(alt["res"])
        spk8.astype(np.int16).tofile(alt["spk"])
        with open(alt["fet"], "wb") as fh:
            np.array([fet_raw.shape[1] + 1], np.int32).tofile(fh)
            np.concatenate([proj(spk8), res8[:, None]], axis=1).astype(np.int64).tofile(fh)
        write_eap(alt["eap"], cells8)
        swap = {f"{base}.res.standard.1": alt["res"], spk_path: alt["spk"], fet_path: alt["fet"], f"{base}.eap.1": alt["eap"]}
        repo = R.run(klusters, f"{base}.clu.standard.1",
                     [{"collisionRows": {"rows": crows}}, {"reopen": {"replace": swap}}, {"collisionRows": {"rows": crows}}], d)
        so = (repo.get("results") or []) + [{}] * 3
        o0, o2 = so[0], so[2]
        rec8 = {x["row"]: x for x in o2.get("records", [])}
        check(repo.get("rc") == 0 and o0.get("corrected") == 40 and len(rec8) == 40
              and all(np.array_equal(np.array(rec8[i]["raw"], np.int16), spk8[i]) for i in crows),
              "reopen: the rewritten session is the one open (raw records are the new cut)")
        check(o2.get("corrected") == 40 and o2.get("rowsMoved") == 0 and o2.get("classesMoved") == 0,
              "reopen: the 40 rows are corrected, and none counts as moved",
              str({k: o2.get(k) for k in ("corrected", "partnersSubtracted", "rowsMoved", "classesMoved", "builds")}))
        before8 = S.cut(fil0, res8, S.CHANS, S.NSAMP, S.PEAK).reshape(-1, S.NSAMP * nch).astype(np.float64)
        e8 = np.array([np.abs(np.array(rec8[i]["served"], float) - before8[i]).sum()
                       / np.abs(spk8[i].astype(float) - before8[i]).sum() for i in crows]) if len(rec8) == 40 else np.ones(1)
        print(f"      reopened on the rewritten files: error as a share of the raw row's: median {np.median(e8):.3f}, worst {e8.max():.3f}")
        check(np.median(e8) < 0.10, "reopen: ... to the records the recording held there before the partners were added")
        check(o2.get("featuresRestored") == 0 and o2.get("featuresProjected") == 40
              and all(np.array_equal(np.array(rec8[i]["features"], np.int64), proj(np.array(rec8[i]["served"], np.int16))[0]) for i in crows),
              "reopen: no feature value of the closed document is put into the new one",
              str({k: o2.get(k) for k in ("featuresRestored", "featuresProjected")}))
        # back to the session as it was
        spk16.tofile(spk_path)
        res.tofile(f"{base}.res.standard.1")
        with open(fet_path, "wb") as fh:
            np.array([fet_raw.shape[1] + 1], np.int32).tofile(fh)
            np.concatenate([fet_raw, res[:, None]], axis=1).astype(np.int64).tofile(fh)

        # Save As under a new stage name: the stage's own .eap goes with it.  The session is
        # stage "a" (its own .eap names the partners; the base .eap names none), saved first so
        # the fitted offsets are in the file, then saved as stage "b".
        cells_none = np.full((n, 4), ABSENT, np.int16)
        cells_none[a, 0] = 0
        cells_none[b, 1] = 0
        write_eap(f"{base}.eap.1", cells_none)
        write_eap(f"{base}.eap.1.a", cells)
        clu_a, clu_b = f"{base}.clu.standard.1.a", f"{base}.clu.standard.1.b"
        with open(f"{base}.clu.standard.1", "rb") as src, open(clu_a, "wb") as dst:
            dst.write(src.read())
        for pref in (True, False):
            for stale in (f"{base}.eap.1.b", clu_b):
                if os.path.exists(stale):
                    os.remove(stale)
            reps = R.run(klusters, clu_a, [{"collisionRows": {"prefs": {"enabled": pref}, "refresh": True}}, {"save": True},
                                           {"collisionRows": {}}, {"saveas": clu_b}, {"collisionRows": {"rows": crows}}], d)
            sa = (reps.get("results") or []) + [{}] * 5
            tag = "Save As" if pref else "Save As, preference off"
            same_file = os.path.exists(f"{base}.eap.1.b") and open(f"{base}.eap.1.b", "rb").read() == open(f"{base}.eap.1.a", "rb").read()
            check(reps.get("rc") == 0 and os.path.exists(clu_b) and same_file,
                  f"{tag}: the new stage has its own .eap, byte for byte the one the session read",
                  f"rc {reps.get('rc')} exists {os.path.exists(base + '.eap.1.b')}")
            if pref:
                check(sa[2].get("eap") == os.path.basename(f"{base}.eap.1.a") and sa[2].get("corrected") == 40
                      and sa[4].get("eap") == os.path.basename(f"{base}.eap.1.b") and sa[4].get("corrected") == 40
                      and sa[4].get("rowsMoved") == 0,
                      "Save As: under the new name the 40 rows are corrected from that file",
                      str([{k: x.get(k) for k in ("eap", "corrected", "rowsMoved")} for x in (sa[2], sa[4])]))
        repb = R.run(klusters, clu_b, [{"collisionRows": {"prefs": {"enabled": True}, "refresh": True}}], d)
        sb = (repb.get("results") or [{}])[0]
        check(sb.get("corrected") == 40 and sb.get("eap") == os.path.basename(f"{base}.eap.1.b"),
              "Save As: opened later, stage b corrects the 40 rows (it would read the base .eap, which names none)",
              str({k: sb.get(k) for k in ("eap", "corrected", "collisionRows")}))
        check(open(f"{base}.eap.1", "rb").read()[32:] == cells_none.astype(np.int8).tobytes(), "Save As: the base .eap is untouched")
        for f in (f"{base}.eap.1.a", f"{base}.eap.1.b", clu_a, clu_b):
            if os.path.exists(f):
                os.remove(f)
        spk16.tofile(spk_path)
        res.tofile(f"{base}.res.standard.1")
        with open(fet_path, "wb") as fh:
            np.array([fet_raw.shape[1] + 1], np.int32).tofile(fh)
            np.concatenate([fet_raw, res[:, None]], axis=1).astype(np.int64).tofile(fh)

        # NEGATIVE CONTROL: an .eap that names no partner
        cells2 = np.full((n, 4), ABSENT, np.int16)
        cells2[a, 0] = 0
        cells2[b, 1] = 0
        write_eap(f"{base}.eap.1", cells2)
        rep2 = R.run(klusters, f"{base}.clu.standard.1", [{"collisionRows": {"rows": ask[:2]}}], d)
        s2 = (rep2.get("results") or [{}])[0]
        check(rep2.get("rc") == 0 and s2.get("collisionRows") == 0 and s2.get("active") == 0
              and all(x["served"] == x["raw"] for x in s2.get("records", [{}]) if "served" in x),
              "NEGATIVE CONTROL: an .eap without a partner anywhere corrects nothing",
              str({k: s2.get(k) for k in ("collisionRows", "active", "eapFound", "skipped", "builds")}) + " rc " + str(rep2.get("rc")))
        os.remove(f"{base}.eap.1")
        rep3 = R.run(klusters, f"{base}.clu.standard.1", [{"collisionRows": {}}], d)
        s3 = (rep3.get("results") or [{}])[0]
        check(rep3.get("rc") == 0 and s3.get("eapFound") is False and s3.get("active") == 0, "without an .eap nothing is corrected")

    print("test_collision_rows: " + ("FAIL (%d failed)" % fails if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
