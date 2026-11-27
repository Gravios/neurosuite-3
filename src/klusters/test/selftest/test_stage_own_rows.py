#!/usr/bin/env python3
"""A stage that holds its own rows opens against its own .spk / .fet / .res.

Most stages are another labelling of the group's rows and share its .spk, .fet and .res.  A stage
that ADDS rows (decollide's <stage>_dc, Add Residual Spikes' <stage>_rs) writes its own under its
tag, and its .clu has more entries than the shared .spk has records.

  - NEGATIVE CONTROL: a stage that is a .clu alone (`relabel`) opens on the shared files, with
    the group's rows;
  - a stage with its own .res / .spk / .fet (`grown`, ten rows more) opens on those: every row is
    there, the ten new ones with the times its own .res gives them;
  - the untagged session beside both still opens on the shared files.
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


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (f"  [{extra}]" if extra and not ok else ""))
    if not ok:
        fails += 1


def scanned(klusters, clu_path, d):
    """Rows of clusters 2 and 3 as the opened document holds them, and their duplicate runs."""
    rep = R.run(klusters, clu_path, [{"duplicates": {"clusters": [2, 3]}}], d, timeout=120)
    st = [x for x in rep.get("results", []) if "step" in x]
    if rep.get("rc") != 0 or len(st) != 1:
        print(rep.get("stderr", "")[-2000:])
        return None
    return st[0]


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, m = S.make_session(d, "own")
        clu, res, fil = m["clu"], m["res"], m["fil"]
        n = len(clu)
        nch = len(S.CHANS)

        # `relabel`: a .clu alone
        with open(f"{base}.clu.standard.1.relabel", "wb") as fh:
            np.array([2], np.int32).tofile(fh)
            clu.tofile(fh)

        # `grown`: ten rows more -- the first ten spikes again, 3 samples later (a duplicate each)
        extra = np.arange(10)
        order = np.argsort(np.concatenate([res, res[extra] + 3]), kind="stable")
        res_g = np.concatenate([res, res[extra] + 3])[order]
        clu_g = np.concatenate([clu, clu[extra]])[order]
        spk_g = S.cut(fil, res_g, S.CHANS, S.NSAMP, S.PEAK)
        fet_g = np.concatenate([m["fet"], m["fet"][extra]])[order]
        fet_g[:, -1] = res_g
        res_g.tofile(f"{base}.res.standard.1.grown")
        spk_g.tofile(f"{base}.spk.standard.1.grown")
        with open(f"{base}.clu.standard.1.grown", "wb") as fh:
            np.array([2], np.int32).tofile(fh)
            clu_g.astype(np.int32).tofile(fh)
        with open(f"{base}.fet.standard.1.grown", "wb") as fh:
            np.array([fet_g.shape[1]], np.int32).tofile(fh)
            fet_g.astype(np.int64).tofile(fh)

        try:
            a = scanned(klusters, f"{base}.clu.standard.1.relabel", d)
            check(a is not None and a.get("scanned") == n and a.get("runs") == 0,
                  "NEGATIVE CONTROL: a stage that is a .clu alone opens on the shared files", str(a and a.get("scanned")))
            g = scanned(klusters, f"{base}.clu.standard.1.grown", d)
            check(g is not None and g.get("scanned") == n + 10, "a stage with its own rows opens: every row is there",
                  str(g and g.get("scanned")))
            check(g is not None and g.get("runs") == 10,
                  "... with the times of its own .res (the ten added rows are found 3 samples from their originals)",
                  str(g and g.get("runs")))
            u = scanned(klusters, f"{base}.clu.standard.1", d)
            check(u is not None and u.get("scanned") == n and u.get("runs") == 0, "the untagged session still opens on the shared files",
                  str(u and u.get("scanned")))
        except Exception as e:                           # a refused open is a dialog: the run times out
            check(False, "opened without a dialog", repr(e)[:200])

    print("FAILED" if fails else "PASSED", f"({fails} failure(s))" if fails else "")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
