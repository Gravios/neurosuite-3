#!/usr/bin/env python3
"""A realign keeps the .res in time order.

A realign moves spike times and never rows, so the .res stays in order only if no spike is moved
past the spike of the row before or after it.  One that would be has slipped -- the alignment
locked onto another unit's spike colliding with it -- and is left where it was.

The session of session.py: two units on one grid of times, so some spikes of the two coincide,
and every .res time jittered by -2..+2 samples around the true peak.  Where a spike of unit 2 and
one of unit 3 are within the jitter, the two rows can be in either order, and moving the unit-2
spike to its true peak passes the unit-3 row: those are the rows a realign of cluster 2 must not
move past their neighbour.

  - NEGATIVE CONTROL: the unit-2 rows whose true peak is past the row before or after them exist,
    and the realigned .res with them moved to their true peak would be out of order;
  - the realign of cluster 2 (saved) moves spikes, and the .res is in order (no row before the one
    before it);
  - rows whose true peak is past a neighbour are where they were, and the realign's log says so;
  - a nudge of cluster 2 by +6 samples: NEGATIVE CONTROL, all of it moved would be out of order;
    the .res after it is in order, and the spikes left where they were are the ones it counts.
"""
import os
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402

fails = 0


def check(ok, what, detail=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + ("" if ok else f"\n        {detail}"))
    fails += not ok


def inversions(t):
    return int(np.count_nonzero(np.diff(np.asarray(t, np.int64)) < 0))


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "ro", n_per_unit=400)
        res, clu, truth = m["res"], m["clu"], m["truth"]
        n = res.size
        two = np.flatnonzero(clu == 2)
        prev = np.r_[np.iinfo(np.int64).min, res[:-1]]
        nxt = np.r_[res[1:], np.iinfo(np.int64).max]
        past = two[(truth[two] < prev[two]) | (truth[two] > nxt[two])]       # the slips a free move would make
        check(inversions(res) == 0, "the session's .res is in order to start with")
        rep = R.run(klusters, f"{base}.clu.standard.1", [{"realign": 2}, {"save": True}], d,
                    realignFromAnySpace=True, autoRealign=False, autoMatrices=False)
        done = next((x for x in rep.get("results", []) if "realignDone" in x), {})
        log = done.get("log", "")
        after = S.read_res(f"{base}.res.standard.1")
        moved = np.flatnonzero(after != res)
        free = after.copy()
        free[past] = truth[past]
        check(past.size > 0 and inversions(free) > 0,
              "NEGATIVE CONTROL: unit-2 rows whose true peak is past a neighbour exist, and moved there the .res "
              "would be out of order", f"{past.size} rows, {inversions(free)} inversions")
        check(done.get("realignDone") is True and after.size == n and moved.size > 0 and set(clu[moved].tolist()) <= {2},
              "the realign of cluster 2 moved spikes of cluster 2 only",
              str({"done": done.get("realignDone"), "moved": int(moved.size), "log": log[-600:]}))
        check(inversions(after) == 0, "the realigned .res is in order", f"{inversions(after)} inversions")
        kept = past[after[past] == res[past]]
        check(kept.size > 0, "rows whose true peak is past a neighbour were left where they were",
              f"{kept.size} of {past.size} kept; moved to {list(zip(res[past].tolist(), after[past].tolist()))[:6]}")
        check("would have moved past the spike of the row before or after" in log,
              "the realign's log says how many were left, and which rows", log[-600:])

    # ── a nudge: the same rule ──
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "ro", n_per_unit=400)
        res, clu = m["res"], m["clu"]
        two = np.flatnonzero(clu == 2)
        free = res.copy()
        free[two] += 6
        rep = R.run(klusters, f"{base}.clu.standard.1", [{"nudge": [2, 6]}, {"save": True}], d,
                    realignFromAnySpace=True, autoRealign=False, autoMatrices=False)
        r = next((x for x in rep.get("results", []) if "nudge" in x.get("step", {})), {})
        after = S.read_res(f"{base}.res.standard.1")
        stayed = two[after[two] == res[two]]
        check(inversions(free) > 0, "NEGATIVE CONTROL: cluster 2 moved by +6 samples, all of it, would be out of order",
              f"{inversions(free)} inversions")
        check(r.get("ok") is True and inversions(after) == 0 and np.count_nonzero(after[two] == res[two] + 6) > 0,
              "a nudge of cluster 2 by +6: its spikes moved, and the .res is in order",
              str({"ok": r.get("ok"), "inversions": inversions(after), "error": r.get("error")}))
        check(stayed.size > 0 and int(r.get("heldBack", -1)) == stayed.size and set(clu[after != res].tolist()) <= {2},
              "the spikes that would have passed a neighbour stayed, and the nudge counts them",
              str({"heldBack": r.get("heldBack"), "stayed": int(stayed.size)}))
    print("FAILED" if fails else "PASSED", f"({fails} failure(s))" if fails else "")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
