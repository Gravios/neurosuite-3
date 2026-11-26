#!/usr/bin/env python3
"""The residual of a collision row is its partner's spike, whole and centred, and says if it is a duplicate.

The session of test_mark_collisions: a spike of unit 3 (0.9 of its mean waveform) is added to the
RECORDING at 40 spikes of unit 2, 5-8 samples later or earlier, and a spike of unit 2 at 30 spikes
of unit 3.  Those added spikes are rows of no cluster.  Besides, the generator puts both units on
one grid of times, so about 18 spikes of each coincide with a spike of the other that IS a row.
Through the `residuals` step:

  - NEGATIVE CONTROL: before any mark no row has a fitted partner and there is no residual;
  - after Mark Collisions on cluster 2 every marked row has a residual, cut from the .fil and
    centred on the partner: offset = the fitted lag, class = cluster 3's;
  - the record IS the added spike: against 0.9 x unit 3's mean waveform, placed where it was
    planted, the median error is under 15 % of that waveform's energy -- for the lags that put
    part of the partner OUTSIDE the row's own window as for the others (the record is cut around
    the partner, not taken from the row);
  - not detrended, and not in need of it: the records' edge level is no higher than that of the
    same rows' .spk records;
  - DUPLICATES: a planted spike is `recovered` (no row holds it, twin -1); a spike of unit 3 that
    is a row of its own is `detected`, its twin a row of cluster 3 within 6 samples of the
    residual's time.  With both clusters marked the two rows of such a pair name each other;
  - the waveform TABLE (what the view draws) holds the residual for the marked rows and the .spk
    record for every other row while the display is on, and no residual when it is off;
  - the corrected records are untouched by the display;
  - Save writes the .spkr: rows, offsets, classes, kinds, twins and records as reported; opened
    again every record is kept from the file (none recomputed) and is the same;
  - removing the marks drops the residuals;
  - WITHOUT a recording the record is made from the .spk: the row's own window, offset 0 -- the
    partner as far as it lies in that window.
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
import test_mark_collisions as M          # noqa: E402

check = M.check
NCH = len(S.CHANS)


def read_spkr(path):
    b = open(path, "rb").read()
    n_rows, n_samp, n_chan, n_spikes, group, flags = struct.unpack("<IIIIII", b[4:28])
    at = 32
    rows = np.frombuffer(b, "<i8", n_rows, at); at += 8 * n_rows
    offsets = np.frombuffer(b, "<i4", n_rows, at); at += 4 * n_rows
    classes = np.frombuffer(b, "<i4", n_rows, at); at += 4 * n_rows
    kinds = np.frombuffer(b, "<i4", n_rows, at); at += 4 * n_rows
    twins = np.frombuffer(b, "<i8", n_rows, at); at += 8 * n_rows
    wav = np.frombuffer(b, "<i2", n_rows * n_samp * n_chan, at).reshape(n_rows, n_samp * n_chan)
    return dict(magic=b[:4], n_spikes=n_spikes, n_samp=n_samp, n_chan=n_chan, rows=rows, offsets=offsets,
                classes=classes, kinds=kinds, twins=twins, wav=wav, size=len(b), end=at + wav.size * 2)


def shifted(t, s):
    """t (nSamp x nChan) moved s samples later, zero-filled."""
    out = np.zeros_like(t)
    if s >= 0:
        out[s:] = t[:t.shape[0] - s] if s else t
    else:
        out[:s] = t[-s:]
    return out


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "res")
        res = S.read_res(f"{base}.res.standard.1")
        rows2, lag2 = plan[2]
        planted = {int(r): int(k) for r, k in zip(rows2, lag2)}
        add = 0.9 * before[clu == 3].mean(0)                   # what was added at each planted row of cluster 2

        steps = [{"residuals": {"show": True}},
                 {"markCollisions": {"clusters": [2], "apply": True}},
                 {"collisionRows": {}}]
        ok, st = M.run(klusters, base, steps, d)
        check(ok, "the session opened")
        if not ok:
            return 1
        check(st[0].get("wanted") == 0 and st[0].get("count") == 0,
              "NEGATIVE CONTROL: before any mark no row has a fitted partner and there is no residual", str(st[0].get("reason")))
        marked = sorted(h["row"] for h in st[1].get("hits", []))
        taus = {h["row"]: h["tau"] for h in st[1].get("hits", [])}

        steps = [{"markCollisions": {"clusters": [2], "apply": True}},
                 {"collisionRows": {"rows": marked}},
                 {"residuals": {"show": True, "rows": marked, "table": 2, "tableSpikes": 400}},
                 {"collisionRows": {"rows": marked}},
                 {"residuals": {"show": False, "table": 2, "tableSpikes": 400}},
                 {"save": True},
                 {"residuals": {"rows": marked}}]
        ok, st = M.run(klusters, base, steps, d)
        check(ok, "marked, shown, saved")
        if not ok:
            return 1
        r = st[2]
        recs = {x["row"]: x for x in r.get("records", [])}
        print(f"      {r.get('wanted')} rows with a fitted partner, {r.get('computed')} records made from {r.get('source')}; "
              f"recovered {r.get('recovered')}, detected {r.get('detected')}, repeated {r.get('repeated')}; "
              f"edge level {r.get('edgeOfResiduals'):.1f} (their .spk records: {r.get('edgeOfRecords'):.1f})")
        check(r.get("fromRecording") is True and r.get("source", "").endswith(".fil"), "the records are cut from the .fil", str(r.get("source")))
        check(r.get("count") == len(marked) and all(recs[m]["has"] for m in marked), "every marked row has a residual",
              f"{r.get('count')} of {len(marked)}")
        # (the lag the rows are corrected with is fitted again at each rebuild, within a few
        # samples of the one the scan found)
        check(len({recs[m]["class"] for m in marked}) == 1 and all(abs(recs[m]["offset"] - taus[m]) <= 2 for m in marked),
              "each is centred on the partner: offset = the fitted lag, class = cluster 3's",
              str([(recs[m]["offset"], taus[m]) for m in marked if abs(recs[m]["offset"] - taus[m]) > 2]))

        # the record against what was planted, by whether the partner leaves the row's own window
        err = {}
        for m in marked:
            if m not in planted:
                continue
            rec = np.array(recs[m]["record"], float).reshape(S.NSAMP, NCH)
            truth = shifted(add, planted[m] - recs[m]["offset"])
            err[m] = ((rec - truth) ** 2).sum() / (add ** 2).sum()
        far = [e for m, e in err.items() if abs(planted[m]) >= 7]
        near = [e for m, e in err.items() if abs(planted[m]) < 7]
        print(f"      residual against the added spike, error as a share of its energy: median {np.median(list(err.values())):.3f} "
              f"over {len(err)} planted rows (lags 7-8: {np.median(far):.3f}, lags 5-6: {np.median(near):.3f}), worst {max(err.values()):.3f}")
        check(np.median(list(err.values())) < 0.15, "the record is the added spike (median error under 15 % of its energy)")
        check(len(far) >= 8 and np.median(far) < 0.15, "... also where part of the partner lies outside the row's own window")
        check(r.get("edgeOfResiduals") <= 1.5 * r.get("edgeOfRecords") + 2.0,
              "no trend to remove: the records' edge level is no higher than their rows' .spk records'",
              f"{r.get('edgeOfResiduals')} vs {r.get('edgeOfRecords')}")

        # duplicates
        nat = M.build.natural[2]
        kinds = {m: (recs[m]["kind"], recs[m]["twin"]) for m in marked}
        check(all(kinds[m] == (0, -1) for m in err), "a planted spike is recovered: no row holds it")
        seen = [m for m in marked if m in nat]
        ok_twin = [kinds[m][0] == 1 and clu[kinds[m][1]] == 3 and abs(int(res[kinds[m][1]]) - (int(res[m]) + recs[m]["offset"])) <= 6 for m in seen]
        print(f"      duplicates: {len(seen)} marked rows hold a spike of unit 3 that is a row of its own; detected with their twin: {sum(ok_twin)}")
        check(len(seen) >= 10 and sum(ok_twin) >= len(seen) - 1,
              "a spike the partner's cluster already holds is detected, its twin a row of cluster 3 within 6 samples",
              str([kinds[m] for m in seen if not (kinds[m][0] == 1)]))
        check(r.get("recovered") + r.get("detected") + r.get("repeated") == len(marked), "every record has a kind")

        # the table the view draws
        # (the table holds what is drawn: the records with their sign turned, negative up)
        table = -np.array(r.get("tableValues", []), np.int64).reshape(-1, S.NSAMP * NCH)
        rows_of_2 = np.flatnonzero(clu == 2)
        check(table.shape[0] == 400, "the table holds cluster 2's 400 spikes", str(table.shape))
        if table.shape[0] == 400:
            is_marked = np.isin(rows_of_2, marked)
            as_residual = all(np.array_equal(table[i], np.array(recs[int(row)]["record"])) for i, row in enumerate(rows_of_2) if is_marked[i])
            as_spk = all(np.array_equal(table[i], raw[row]) for i, row in enumerate(rows_of_2) if not is_marked[i])
            check(as_residual, "display on: a marked row is drawn as its residual")
            check(as_spk, "display on: every other row is drawn as the .spk holds it")
            off = -np.array(st[4].get("tableValues", []), np.int64).reshape(-1, S.NSAMP * NCH)
            served = {x["row"]: np.array(x["served"]) for x in st[1].get("records", [])}
            check(off.shape[0] == 400 and all(np.array_equal(off[i], served[int(row)]) for i, row in enumerate(rows_of_2) if is_marked[i]),
                  "display off: a marked row is drawn as its corrected record again")
        a = {x["row"]: x["served"] for x in st[1].get("records", [])}
        b = {x["row"]: x["served"] for x in st[3].get("records", [])}
        check(a == b and st[1].get("corrected") == st[3].get("corrected"), "the corrected records are untouched by the display")

        # the file
        path = f"{base}.spkr.standard.1"
        check(os.path.exists(path), "Save wrote the .spkr")
        if not os.path.exists(path):
            return 1
        f = read_spkr(path)
        check(f["magic"] == b"SPR\x01" and f["n_spikes"] == len(clu) and f["n_samp"] == S.NSAMP and f["n_chan"] == NCH and f["end"] == f["size"],
              "its header names the group's geometry and the file is exactly as long as it says")
        check(list(f["rows"]) == marked, "it lists the marked rows, ascending")
        same = all(f["offsets"][i] == recs[m]["offset"] and f["classes"][i] == recs[m]["class"] and f["kinds"][i] == recs[m]["kind"]
                   and f["twins"][i] == recs[m]["twin"] and np.array_equal(f["wav"][i], np.array(recs[m]["record"]))
                   for i, m in enumerate(marked))
        check(same, "offsets, classes, kinds, twins and records are those reported")
        check(not os.path.exists(f"{base}.spkc.standard.1") or open(f"{base}.spkc.standard.1", "rb").read()[:4] == b"SPC\x01",
              "the .spkc beside it is still a .spkc")

        ok, st = M.run(klusters, base, [{"collisionRows": {}}, {"residuals": {"show": True, "rows": marked}},
                                        {"unmarkCollisions": {"clusters": [2]}}, {"residuals": {"rows": marked}}], d)
        check(ok, "opened again")
        if ok:
            r2 = st[1]
            again = {x["row"]: x for x in r2.get("records", [])}
            check(r2.get("kept") == len(marked) and r2.get("computed") == 0, "every record is kept from the file, none made again",
                  str({k: r2.get(k) for k in ("kept", "computed", "wanted")}))
            check(all(again[m]["has"] and again[m]["record"] == recs[m]["record"] and again[m]["kind"] == recs[m]["kind"] for m in marked),
                  "... and is the same record with the same kind")
            check(st[2].get("removed") == len(marked) and st[3].get("count") == 0 and st[3].get("wanted") == 0,
                  "removing the marks drops the residuals", str({k: st[3].get(k) for k in ("count", "wanted")}))

    # ── both clusters marked: the two rows of a detected pair name each other ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "both")
        res = S.read_res(f"{base}.res.standard.1")
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}},
                                        {"markCollisions": {"clusters": [3], "apply": True}},
                                        {"residuals": {"show": True, "rows": [int(i) for i in range(len(clu))]}}], d)
        check(ok, "both clusters marked")
        if ok:
            recs = {x["row"]: x for x in st[2].get("records", []) if x["has"]}
            pairs = [(m, x["twin"]) for m, x in recs.items() if x["kind"] == 1 and clu[m] == 2]
            mutual = [recs.get(t, {}).get("kind") == 1 and recs.get(t, {}).get("twin") == m for m, t in pairs]
            print(f"      both marked: {len(recs)} residuals, {st[2].get('detected')} detected; of cluster 2's {len(pairs)} detected rows, "
                  f"{sum(mutual)} are named back by their twin")
            check(len(pairs) >= 10 and sum(mutual) >= len(pairs) - 3, "the two rows of a collision seen from both sides name each other",
                  f"{sum(mutual)} of {len(pairs)}")
            check(st[2].get("repeated") == 0, "no residual repeats another here (no spike lies in two rows' windows)")

    # ── without a recording ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "nofil")
        rows2, lag2 = plan[2]
        planted = {int(r): int(k) for r, k in zip(rows2, lag2)}
        add = 0.9 * before[clu == 3].mean(0)
        os.remove(f"{base}.fil")
        os.remove(f"{base}.dat")
        # (opening a session without its .dat raises a dialog, reported as a result of its own)
        steps = [{"markCollisions": {"clusters": [2], "apply": True}},
                 {"residuals": {"show": True, "rows": [int(i) for i in np.flatnonzero(clu == 2)]}}]
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        st = [x for x in rep.get("results", []) if "step" in x]
        ok = rep.get("rc") == 0 and len(st) == len(steps)
        marked = sorted(h["row"] for h in st[0].get("hits", [])) if ok else []
        check(ok, "without a recording: marked and shown")
        if ok:
            r = st[1]
            recs = {x["row"]: x for x in r.get("records", []) if x["has"]}
            check(r.get("fromRecording") is False and ".spk.standard.1" in r.get("source", "") and len(recs) == len(marked),
                  "the records are made from the .spk", str(r.get("source")))
            check(all(x["offset"] == 0 for x in recs.values()), "... the row's own window: offset 0")
            err = []
            for m, x in recs.items():
                if m in planted:
                    rec = np.array(x["record"], float).reshape(S.NSAMP, NCH)
                    truth = shifted(add, planted[m])
                    err.append(((rec - truth) ** 2).sum() / (truth ** 2).sum())
            print(f"      from the .spk: error against the part of the added spike inside the row's window, median {np.median(err):.3f}")
            check(np.median(err) < 0.2, "the record is the partner as far as it lies in the row's window")

    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
