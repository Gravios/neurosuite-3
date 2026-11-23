#!/usr/bin/env python3
"""Sort Spikes by Time carries the .eap and the sparse .spkc with the rows.

A session with 40 planted collision rows (as test_collision_rows) is opened once in time order:
the corrected rows are written as its .spkc and the records served are noted.  Then 60 rows --
collision rows among them -- are moved out of time order in EVERY row-indexed file (res, clu, spk,
fet, eap, and the .spkc's index), and the `timeSort` step is run on that session:

  - the plan lists the .eap and the .spkc among the files rewritten (before, the .eap was only
    named as "not reordered" and the .spkc was not known at all);
  - afterwards every file is byte for byte the time-ordered one, the .eap and the .spkc included;
  - reopened by the sort, the same 40 rows are corrected and served as before the shuffle;
  - NEGATIVE CONTROLS: an .eap of a stage with another spike count and a .spkc over another spike
    count are reported as skipped and left byte for byte; another group's .spkc is not touched
    and not listed;
  - the unsorted session, before the sort, does not hold the time-ordered files (the test would
    pass on a sort that did nothing otherwise).
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


def write_spkc(path, rows, wav, n_spikes, n_samp, n_ch, group=1):
    with open(path, "wb") as fh:
        fh.write(b"SPC\x01" + struct.pack("<IIIIII", len(rows), n_samp, n_ch, n_spikes, group, 0) + b"\x00" * 4)
        np.asarray(rows, np.int64).tofile(fh)
        np.asarray(wav, np.int16).tofile(fh)


def read_spkc(path):
    with open(path, "rb") as fh:
        fh.read(4)
        n_rows, n_samp, n_ch, n_spikes, group, flags = struct.unpack("<IIIIII", fh.read(24))
        fh.read(4)
        rows = np.fromfile(fh, np.int64, n_rows)
        wav = np.fromfile(fh, np.int16).reshape(n_rows, n_samp * n_ch)
    return n_spikes, rows, wav


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        nch = len(S.CHANS)
        base, m = S.make_session(d, "ts", n_per_unit=400)
        clu, res = m["clu"], m["res"]
        n = len(clu)
        spk = m["spk"].reshape(-1, S.NSAMP, nch).astype(np.float64)
        a, b = np.flatnonzero(clu == 2), np.flatnonzero(clu == 3)
        rng = np.random.default_rng(11)
        coll = np.sort(rng.choice(a, 40, replace=False))
        tau = rng.integers(9, 14, len(coll))
        fil = m["fil"].astype(np.float64)
        for i, k in zip(coll, tau):
            s0 = int(res[i]) - (S.PEAK - 1) + int(k)
            fil[s0:s0 + S.NSAMP, S.CHANS] += 0.9 * spk[b].mean(0)
        fil = np.clip(np.rint(fil), -32768, 32767).astype(np.int16)
        fil.tofile(base + ".fil")
        fil.tofile(base + ".dat")
        spk16 = S.cut(fil, res, S.CHANS, S.NSAMP, S.PEAK).reshape(n, -1)
        means, evecs = S.write_pca(os.path.join(d, "again.pca"), m["spk"], nch, S.NSAMP, S.NCOMP)
        fet = np.concatenate([S.project(spk16, means, evecs, nch, S.NSAMP, S.NCOMP), res[:, None]], axis=1).astype(np.int64)
        cells = np.full((n, 4), ABSENT, np.int16)
        cells[a, 0] = 0
        cells[b, 1] = 0
        cells[coll, 1] = tau
        P = {k: f"{base}.{k}.standard.1" for k in ("res", "clu", "spk", "fet", "spkc")}
        P["eap"] = f"{base}.eap.1"

        def write_all(order):
            res[order].astype(np.int64).tofile(P["res"])
            with open(P["clu"], "wb") as fh:
                np.array([int(clu.max()) + 1], np.int32).tofile(fh)
                clu[order].astype(np.int32).tofile(fh)
            spk16[order].astype(np.int16).tofile(P["spk"])
            with open(P["fet"], "wb") as fh:
                np.array([fet.shape[1]], np.int32).tofile(fh)
                fet[order].tofile(fh)
            write_eap(P["eap"], cells[order])

        ident = np.arange(n)
        write_all(ident)

        # ── 1. in time order: the corrected rows, written as the session's .spkc ──────────────
        ask = [int(i) for i in coll]
        out = R.run(klusters, f"{base}.clu.standard.1", [{"collisionRows": {"rows": ask, "writeSpkc": P["spkc"]}}], d)
        r0 = (out.get("results") or [{}])[0]
        served0 = {x["row"]: x["served"] for x in r0.get("records", [])}
        check(r0.get("corrected") == 40 and os.path.exists(P["spkc"]), "in time order: 40 rows corrected, .spkc written",
              str({k: r0.get(k) for k in ("corrected", "collisionRows")}))
        n_sp, rows0, wav0 = read_spkc(P["spkc"])
        check(n_sp == n and np.array_equal(rows0, coll), "... its index is the 40 collision rows")

        # ── 2. 60 rows out of time order, in every file ───────────────────────────────────────
        order = ident.copy()
        movers = np.concatenate([coll[::3], rng.choice(np.setdiff1d(ident, coll), 60 - len(coll[::3]), replace=False)])
        order = np.concatenate([np.setdiff1d(ident, movers), rng.permutation(movers)])
        order = np.concatenate([order[:300], order[-60:], order[300:-60]])     # the movers in the middle, shuffled
        moved_to = np.empty(n, np.int64)
        moved_to[order] = ident
        write_all(order)
        by = np.argsort(moved_to[rows0])
        write_spkc(P["spkc"], moved_to[rows0][by], wav0[by], n, S.NSAMP, nch)
        # what a stable sort by time makes of it (spikes at the same sample keep the order they are in)
        final = order[np.argsort(res[order], kind="stable")]
        at = np.empty(n, np.int64)
        at[final] = ident
        unsorted = {k: open(p, "rb").read() for k, p in P.items()}
        write_all(final)
        fby = np.argsort(at[rows0])
        write_spkc(P["spkc"], at[rows0][fby], wav0[fby], n, S.NSAMP, nch)
        sorted_bytes = {k: open(p, "rb").read() for k, p in P.items()}
        for k, p in P.items():
            with open(p, "wb") as fh:
                fh.write(unsorted[k])
        check(all(unsorted[k] != sorted_bytes[k] for k in P),
              "NEGATIVE CONTROL: out of order, none of the six files is the time-ordered one")
        # files the sort must leave alone
        other_eap = f"{base}.eap.1.other"
        write_eap(other_eap, cells[:n - 7])
        other_spkc = f"{base}.spkc.standard.1.other"
        write_spkc(other_spkc, [3, 5], wav0[:2], n + 9, S.NSAMP, nch)
        group2 = f"{base}.spkc.standard.2"
        write_spkc(group2, rows0, wav0, n, S.NSAMP, nch, group=2)
        alone = {p: open(p, "rb").read() for p in (other_eap, other_spkc, group2)}

        out = R.run(klusters, f"{base}.clu.standard.1", [{"timeSort": {}}, {"collisionRows": {"rows": [int(at[i]) for i in coll]}}], d)
        t, r1 = (out.get("results") or [{}, {}])[:2]
        files = t.get("files", [])
        print(f"      out of order: {t.get('outOfOrder')}; rewritten: {len(files)} file(s); skipped: {t.get('skipped')}; "
              f"not reordered: {t.get('notReordered')}")
        check(t.get("sorted") == 1 and t.get("outOfOrder", 0) > 0, "the sort ran", str(t.get("log")))
        check(os.path.basename(P["eap"]) in files and os.path.basename(P["spkc"]) in files,
              "the plan rewrites the .eap and the .spkc with the rows", str(files))
        check(not t.get("notReordered"), "nothing of this session is left as 'not reordered'")
        for k in ("res", "clu", "spk", "fet", "eap", "spkc"):
            check(open(P[k], "rb").read() == sorted_bytes[k], f"after the sort the .{k} is the time-ordered file, byte for byte")
        check(sorted(t.get("skipped", [])) == sorted(os.path.basename(p) for p in (other_eap, other_spkc)),
              "NEGATIVE CONTROL: the .eap and the .spkc over another spike count are reported as skipped", str(t.get("skipped")))
        check(all(open(p, "rb").read() == v for p, v in alone.items()) and os.path.basename(group2) not in files,
              "NEGATIVE CONTROL: ... and are byte for byte as they were; another group's .spkc is not touched")
        check(r1.get("corrected") == 40 and all(x["served"] == served0[int(final[x["row"]])] for x in r1.get("records", []))
              and len(r1.get("records", [])) == 40,
              "reopened by the sort: the same 40 rows are corrected and served as before the shuffle",
              str({k: v for k, v in r1.items() if k != "records"}))
        check(not [f for f in os.listdir(d) if "timesort" in f], "no working copy or backup of the sort is left")

    print(f"test_time_sort_collision_files: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
