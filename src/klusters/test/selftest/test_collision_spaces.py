#!/usr/bin/env python3
"""A collision is marked only when it is there in every waveform space.

A partner's spike is in the row whatever the waveform space (the stderiv transform is linear).
The session of test_mark_collisions, with a SECOND waveform space `alt` (.spk.alt.1 with its
.fet and .pca).  In `alt` the planted partner is taken out of every other planted row of
cluster 2: there the standard space shows a partner and `alt` none -- what a fit that explains
something that is no spike looks like.

  - NEGATIVE CONTROL: with `alt` identical to the standard waveforms the scan finds what it
    finds without the check, and nothing is left out;
  - with the partner missing from `alt` in those rows, the scan leaves exactly them out (and
    says how many, and that `alt` was the space); with the check off they are still found;
  - marks made without the check: Remove Collision Marks Not in Every Space finds exactly those
    rows, counted against `alt` and none against the standard space; applied, they name no
    partner any more and every other mark stands;
  - a session with ONE waveform space: nothing to confirm in, nothing is called unconfirmed.
"""
import os
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


def add_alt(base, records, res):
    """The waveform space `alt`: its own .spk, a basis and a .fet over it."""
    records = np.ascontiguousarray(records.reshape(len(res), -1).astype(np.int16))
    records.tofile(f"{base}.spk.alt.1")
    means, evecs = S.write_pca(f"{base}.pca.alt.1", records, NCH, S.NSAMP, S.NCOMP)
    fet = np.concatenate([S.project(records, means, evecs, NCH, S.NSAMP, S.NCOMP), res[:, None]], axis=1).astype(np.int64)
    with open(f"{base}.fet.alt.1", "wb") as fh:
        np.array([fet.shape[1]], np.int32).tofile(fh)
        fet.tofile(fh)


def hits_of(st):
    return sorted(h["row"] for h in st.get("hits", []))


def main():
    klusters = R.skip_without_binary()

    # ── NEGATIVE CONTROL: the second space holds the same waveforms ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "same")
        res = S.read_res(f"{base}.res.standard.1")
        add_alt(base, raw, res)
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "everySpace": False}},
                                        {"markCollisions": {"clusters": [2]}}], d)
        check(ok, "a session with two waveform spaces holding the same records")
        if ok:
            check(st[1].get("otherSpaces") == "alt", "the scan confirms in the other space, alt", str(st[1].get("otherSpaces")))
            check(hits_of(st[1]) == hits_of(st[0]) and st[1].get("notInEverySpace") == 0 and len(hits_of(st[0])) > 40,
                  "NEGATIVE CONTROL: the same hits as without the check, none left out",
                  f"{len(hits_of(st[1]))} vs {len(hits_of(st[0]))}, left out {st[1].get('notInEverySpace')}")

    # ── the partner is missing from `alt` in every other planted row ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "diff")
        res = S.read_res(f"{base}.res.standard.1")
        rows2 = [int(r) for r in plan[2][0]]
        gone = rows2[::2]
        alt = raw.reshape(len(res), S.NSAMP, NCH).copy()
        alt[gone] = np.rint(before[gone]).astype(np.int16)            # the records before the partner was added
        add_alt(base, alt, res)
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "everySpace": False}},
                                        {"markCollisions": {"clusters": [2]}}], d)
        check(ok, "a session whose second space lacks the partner in every other planted row")
        if not ok:
            return 1
        loose, strict = hits_of(st[0]), hits_of(st[1])
        missing = sorted(set(loose) & set(gone))
        print(f"      without the check {len(loose)} hits, {len(missing)} of them in rows where alt holds no partner; "
              f"with it {len(strict)} hits, {st[1].get('notInEverySpace')} left out")
        check(len(missing) >= 15, "(the standard space shows a partner in those rows)", str(len(missing)))
        check(sorted(set(loose) - set(strict)) == missing and set(strict) <= set(loose),
              "the scan leaves out exactly the rows whose partner is not there in alt",
              str(sorted(set(loose) - set(strict))[:8]))
        check(st[1].get("notInEverySpace") == len(missing), "... and counts them", str(st[1].get("notInEverySpace")))

        # marks made without the check, then cleaned
        steps = [{"markCollisions": {"clusters": [2], "everySpace": False, "apply": True}},
                 {"unconfirmedMarks": {"clusters": [2, 3]}},
                 {"unconfirmedMarks": {"clusters": [2, 3], "apply": True}},
                 {"collisionRows": {}},
                 {"unconfirmedMarks": {"clusters": [2, 3]}},
                 {"save": True}]
        ok, st = M.run(klusters, base, steps, d)
        check(ok, "marked without the check, then cleaned")
        if ok:
            u = st[1]
            check(u.get("marked") == len(loose) and u.get("spaces") == ["standard", "alt"], "every marked row is fitted in both spaces",
                  str((u.get("marked"), u.get("spaces"))))
            check(sorted(u.get("rows", [])) == missing, "the unconfirmed marks are exactly those rows", str(sorted(u.get("rows", []))[:8]))
            check(u.get("rejectedIn") == {"standard": 0, "alt": len(missing)}, "all of them not accepted in alt, none in the standard space",
                  str(u.get("rejectedIn")))
            check(u.get("modified") is True and st[2].get("removed") == len(missing), "applied: those rows lose their partner")
            check(st[3].get("collisionRows") == len(loose) - len(missing), "every other mark stands", str(st[3].get("collisionRows")))
            check(st[4].get("rows") == [] and st[4].get("marked") == len(loose) - len(missing), "asked again, none is unconfirmed")
            magic, cells = M.read_eap(f"{base}.eap.1")
            two = (cells != -128).sum(1) >= 2
            check(not two[missing].any() and int(two.sum()) == len(loose) - len(missing), "Save writes it: the .eap names no partner in those rows")

    # ── one waveform space ──
    with tempfile.TemporaryDirectory() as d:
        base, clu, plan, before, raw = M.build(d, "one")
        ok, st = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}}, {"unconfirmedMarks": {"clusters": [2]}}], d)
        check(ok and st[0].get("otherSpaces") == "" and st[0].get("notInEverySpace") == 0 and st[1].get("spaces") == ["standard"]
              and st[1].get("rows") == [], "one waveform space: nothing to confirm in, nothing is called unconfirmed",
              str((st[0].get("otherSpaces"), st[1].get("spaces"), len(st[1].get("rows", [])))) if ok else "")

    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
