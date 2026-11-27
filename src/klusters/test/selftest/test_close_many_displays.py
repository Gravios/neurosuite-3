#!/usr/bin/env python3
"""File > Close with three displays or more does not crash.

The close removed the display tabs one by one; each removal moved the current tab, and
slotTabChange then ran on a display still there while the matrix docks of the Overview, already
deleted, were reached (followMatrixSelection: SIGSEGV).  An open makes two displays (the Overview
and the Template Library), so one more display was enough.  The tabs now go with their signals
blocked.

  - a session opened, a cluster display and a waveform display added (four in all), then closed
    and opened again: the run ends normally and the document is open.
    (Before the fix this run ended on SIGSEGV in the close -- checked by hand on the build before
    it; there is no negative control in the run itself.)
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import session as S                # noqa: E402


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "cl", n_per_unit=200)
        rep = R.run(klusters, f"{base}.clu.standard.1",
                    [{"select": [2, 3]}, {"keyCommand": "display.newCluster"}, {"keyCommand": "display.newWaveform"},
                     {"reopen": True}, {"dump": True, "cluster": 2}], d, timeout=600)
        res = rep.get("results", [])
        dump = next((x for x in res if "dump" in x.get("step", {})), {})
        ok = rep.get("rc") == 0 and len(res) == 5 and all(x.get("ok", True) is not False for x in res) and bool(dump)
        print(("  ok:   " if ok else "  FAIL: ") + "four displays, closed and opened again: no crash, the document open"
              + ("" if ok else f"\n        rc {rep.get('rc')}, {len(res)} step(s) reported\n        {rep.get('stderr', '')[-400:]}"))
    print("PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
