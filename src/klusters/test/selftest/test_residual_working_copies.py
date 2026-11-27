#!/usr/bin/env python3
"""Add Residual Spikes' working stage is opened without copying it again.

An open copies the .spk, .res, .fet and .clu it reads to `.pending` working copies, so that
nothing it edits reaches the files before Save.  The working stage Add Residual Spikes writes is
itself disposable (Save renames it, a close without saving removes it), so the copy only doubled
the writing of every add -- a full .spk of the shown method again.  It is now opened without
them; a realign or nudge, which write into the working copies, makes them when it starts.

The session of test_residual_spikes (cluster 2 marked):

  - NEGATIVE CONTROL: an ordinary open has its `.pending` copies on disk;
  - after Add Residual Spikes the working stage `rs_rw` is open with no `.pending` file of it;
  - a realign of cluster 3 in the working stage runs (the copies made when it started) and Save
    puts it in `rs`: the realign moved spikes, and `rs`'s .res differs from the one the add wrote
    exactly when it did; no `_rw` file is left.
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
import test_residual_working_stage as W   # noqa: E402

check = M.check


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, ok = W.session(d, [2])
        check(ok, "cluster 2 marked and saved")
        cluf = f"{base}.clu.standard.1"
        pend = lambda rep: next((x for x in rep.get("results", []) if "dump" in x.get("step", {})), {}).get("dump", {}).get("pendingFiles")  # noqa: E731
        rep = R.run(klusters, cluf, [{"dump": True}], d)
        check(bool(pend(rep)) and all(".1.pending" in f for f in pend(rep)),
              "NEGATIVE CONTROL: an ordinary open makes its .pending copies", str(pend(rep)))

        rep = R.run(klusters, cluf, [W.ADD([2]), {"dump": True}], d)
        r = next((x for x in rep.get("results", []) if "residualSpikes" in x.get("step", {})), {})
        check(r.get("opened") is True and r.get("working", {}).get("work") == "rs_rw"
              and bool(W.any_with(d, ".rs_rw")) and pend(rep) is not None
              and not [f for f in pend(rep) if "rs_rw" in f],
              "the working stage rs_rw is open with no .pending copy of it",
              str({"opened": r.get("opened"), "pending": pend(rep)}))
        added = S.read_res(f"{base}.res.standard.1.rs_rw").copy()
        for f in W.any_with(d, "rs_rw"):
            os.remove(os.path.join(d, f))

        rep = R.run(klusters, cluf, [W.ADD([2]), {"realign": 3}, {"save": True}, {"dump": True, "cluster": 3}], d)
        res = rep.get("results", [])
        rl = next((x for x in res if "realign" in x.get("step", {})), {})
        dump = next((x for x in res if "dump" in x.get("step", {})), {}).get("dump", {})
        saved = S.read_res(f"{base}.res.standard.1.rs") if os.path.exists(f"{base}.res.standard.1.rs") else None
        moved = saved is not None and saved.size == added.size and int((saved != added).sum())
        check(rl.get("ok") is True and dump.get("clu", "").endswith(".1.rs") and saved is not None and moved > 0
              and not W.any_with(d, "_rw"),
              "a realign in the working stage runs and Save puts it in rs (its .res differs where the realign moved spikes)",
              str({"realign": {k: rl.get(k) for k in ("ok", "started", "error")}, "clu": dump.get("clu"),
                   "moved": moved, "rw": W.any_with(d, "_rw"), "stderr": rep.get("stderr", "")[-400:]}))
        if saved is not None and saved.size == added.size:
            ids = S.read_clu(f"{base}.clu.standard.1.rs")
            check(set(np.unique(ids[saved != added]).tolist()) <= {3},
                  "only cluster 3's rows moved", str(np.unique(ids[saved != added]).tolist()))
    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
