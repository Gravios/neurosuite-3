#!/usr/bin/env python3
"""A child recluster is one step of the undo history, undone and redone whole.

The recluster records its edit and then re-cuts the children onto the parents; the undo history
holds the child labelling from before the edit, so the undo does not depend on which of the two
changed what.  One parent with two children (700 and 100 spikes), the larger reclustered with
KiloKlustaKwik_cpu (KKK_BIN, else the build tree's; skipped when there is none).
"""
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                           # noqa: E402
import session as S                       # noqa: E402
import test_recluster_klustakwik as K     # noqa: E402

fails = 0
H = lambda **k: {"hierarchy": k}          # noqa: E731


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def main():
    klusters = R.skip_without_binary()
    kkk = K.sorter()
    if not kkk:
        print("test_child_recluster_undo: SKIP (no KiloKlustaKwik_cpu: build it or set KKK_BIN)")
        return 77        # ctest: skipped, not passed
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "kk", n_per_unit=400)
        n = m["clu"].size
        for kind, ids, hdr in (("clu", np.full(n, 2), 3),
                               ("clc", np.where(np.arange(n) % 8 == 0, 20001, 20000), 2),
                               ("clp", np.r_[np.zeros(19999), 2, 2], 20001)):
            with open(f"{base}.{kind}.standard.1", "wb") as fh:
                np.array([hdr], np.int32).tofile(fh)
                np.asarray(ids).astype(np.int32).tofile(fh)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [H(), H(showChildren=[20000]),
                     {"recluster": "", "opts": dict(exe=kkk, autoSelect=False, args=K.T + "1111111111111")},
                     H(), H(undo=True), H(redo=True), H(undo=True)], d, timeout=600)
        res = [r for r in rep.get("results", []) if "childSizes" in r]
        check(len(res) == 6, "the run finished", rep.get("stderr", "")[-400:])
        if len(res) == 6:
            start, _, after, u, rd, u2 = (r["childSizes"] for r in res)
            check(after != start and sum(after.values()) == 800 and "20000" not in after,
                  "the recluster replaced child 20000", after)
            check(res[2].get("undoSteps") == 1, "... as one step", res[2].get("undoSteps"))
            check(u == start and res[3]["childParents"] == res[0]["childParents"], "Undo puts child 20000 back", u)
            check(rd == after and res[4]["childParents"] == res[2]["childParents"], "Redo makes the recluster again", rd)
            check(u2 == start, "... and it undoes again", u2)
    print(f"test_child_recluster_undo: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
