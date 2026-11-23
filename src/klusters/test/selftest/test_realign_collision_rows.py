#!/usr/bin/env python3
"""Realign measures a collision row on its corrected record.

Two synthetic units; every .res time is jittered by up to 2 samples, so a realign has work to do
and the true time of every spike is known.  At 60 spikes of unit 2 a spike of unit 3 (1.2 of its
mean) is added to the recording 3 to 7 samples before or after, and the .eap says so.  With the
partner in the record, the lag that fits unit 2's mean best is pulled toward the partner.

Unit 2 is realigned twice from the same files:

  - preference ON (collision rows corrected): the realign reports the 60 rows as measured on
    their corrected record, and they end at their true time about as often as the rows without
    a partner do;
  - preference OFF (NEGATIVE CONTROL, the realign as it was): no row is measured corrected, and
    the collision rows end off their true time far more often;
  - in both runs the rows WITHOUT a partner end the same (the change touches nothing else), and
    the .spk records written for the collision rows are raw cuts of the recording at the new
    times (the corrected record is measured, never written).

"True time" is up to the one offset a realign leaves on the whole cluster (taken as the median of
the rows without a partner).
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


def main():
    klusters = R.skip_without_binary()
    out = {}
    for pref in (True, False):
        with tempfile.TemporaryDirectory() as d:
            nch = len(S.CHANS)
            base, m = S.make_session(d, "ra", n_per_unit=400)
            clu, res, truth = m["clu"], m["res"], m["truth"]
            n = len(clu)
            spk = m["spk"].reshape(-1, S.NSAMP, nch).astype(np.float64)
            a, b = np.flatnonzero(clu == 2), np.flatnonzero(clu == 3)
            rng = np.random.default_rng(3)
            coll = np.sort(rng.choice(a, 60, replace=False))
            d_true = rng.integers(3, 8, len(coll)) * rng.choice([-1, 1], len(coll))
            fil = m["fil"].astype(np.float64)
            add = 1.2 * spk[b].mean(0)
            for i, k in zip(coll, d_true):
                s0 = int(truth[i]) - (S.PEAK - 1) + int(k)
                fil[s0:s0 + S.NSAMP, S.CHANS] += add
            fil = np.clip(np.rint(fil), -32768, 32767).astype(np.int16)
            fil.tofile(base + ".fil")
            fil.tofile(base + ".dat")
            spk16 = S.cut(fil, res, S.CHANS, S.NSAMP, S.PEAK).reshape(n, -1)
            spk16.tofile(f"{base}.spk.standard.1")
            means, evecs = S.write_pca(os.path.join(d, "again.pca"), m["spk"], nch, S.NSAMP, S.NCOMP)
            fet = np.concatenate([S.project(spk16, means, evecs, nch, S.NSAMP, S.NCOMP), res[:, None]], axis=1).astype(np.int64)
            with open(f"{base}.fet.standard.1", "wb") as fh:
                np.array([fet.shape[1]], np.int32).tofile(fh)
                fet.tofile(fh)
            cells = np.full((n, 4), ABSENT, np.int16)
            cells[a, 0] = 0
            cells[b, 1] = 0
            cells[coll, 1] = (truth[coll] + d_true) - res[coll]      # where the partner is, from the row's listed time
            write_eap(f"{base}.eap.1", cells)

            rep = R.run(klusters, f"{base}.clu.standard.1",
                        [{"collisionRows": {"prefs": {"enabled": pref}, "refresh": True}}, {"realign": 2},
                         {"dump": True, "cluster": 2}, {"save": True}, {"dump": True, "cluster": 2}], d)
            rs = rep.get("results", [])
            first = rs[0] if rs else {}
            done = next((r for r in rs if "realignDone" in r), {})
            dump = next((r["dump"]["cluster"] for r in rs if "dump" in r and "cluster" in r.get("dump", {})), {})
            rows = np.array(dump.get("rows", []), np.int64)
            times = np.array(dump.get("times", []), np.int64)
            ok = rep.get("rc") == 0 and len(rows) == len(a) and np.array_equal(rows, a)
            err = np.zeros(len(a), np.int64)
            if ok:
                off = times - truth[rows]
                is_c = np.isin(rows, coll)
                err = off - int(np.median(off[~is_c]))
            saved_res = np.fromfile(f"{base}.res.standard.1", np.int64)
            saved_spk = np.fromfile(f"{base}.spk.standard.1", np.int16).reshape(n, -1)
            raw_cut = np.array_equal(saved_spk[coll], S.cut(fil, saved_res, S.CHANS, S.NSAMP, S.PEAK).reshape(n, -1)[coll])
            out[pref] = dict(ok=ok, corrected=first.get("corrected", 0), measured=done.get("measuredCorrected", -1),
                             done=done.get("realignDone"), err_c=err[np.isin(a, coll)], err_o=err[~np.isin(a, coll)],
                             raw_cut=raw_cut, log=done.get("log", "")[-1500:], stderr=rep.get("stderr", "")[-800:])

    on, off = out[True], out[False]
    for name, o in (("ON ", on), ("OFF", off)):
        print(f"      preference {name}: rows corrected {o['corrected']}, measured corrected {o['measured']}; off their true time: "
              f"collision rows {int((o['err_c'] != 0).sum())} of {len(o['err_c'])} (by 2 or more: {int((abs(o['err_c']) >= 2).sum())}), "
              f"other rows {int((o['err_o'] != 0).sum())} of {len(o['err_o'])}")
    check(on["ok"] and off["ok"] and on["done"] and off["done"], "both realigns ran", on["stderr"] + off["stderr"])
    check(on["corrected"] == 60 and on["measured"] == 60, "preference on: the 60 collision rows are measured on their corrected record",
          f"corrected {on['corrected']}, measured {on['measured']}")
    check("measured on their corrected record" in on["log"], "... and the realign's log says so")
    check(off["measured"] == 0 and "measured on their corrected record" not in off["log"],
          "NEGATIVE CONTROL: preference off, no row is measured corrected")
    bad_on, bad_off = int((on["err_c"] != 0).sum()), int((off["err_c"] != 0).sum())
    rate_o = float((on["err_o"] != 0).mean())
    check(bad_off >= 15, "NEGATIVE CONTROL: measured raw, many collision rows end off their true time", str(bad_off))
    check(bad_on <= bad_off // 3 and bad_on <= max(3, int(np.ceil(3 * rate_o * 60))),
          "measured corrected, the collision rows end at their true time about as often as the other rows",
          f"{bad_on} of 60 against {bad_off} raw; other rows off: {rate_o:.1%}")
    check(np.array_equal(on["err_o"], off["err_o"]) or float((on["err_o"] != off["err_o"]).mean()) < 0.02,
          "the rows without a partner end the same in both runs", f"{int((on['err_o'] != off['err_o']).sum())} differ")
    check(on["raw_cut"] and off["raw_cut"], "the records saved for the collision rows are raw cuts at the new times (nothing corrected is written)")

    print(f"test_realign_collision_rows: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
