#!/usr/bin/env python3
"""A realign that cannot run says why in its log.  realignSpikes writes the reason and returns; the line
used to stay in a buffer that was dropped, so the curator saw a realign that did nothing and no reason.
Two refusals of different depth: a cluster that does not exist, and a .res too short for the cluster's
rows (found only when the times are read).  Neither may change a file.
"""
import hashlib
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S            # noqa: E402
import run as R                # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n" + extra) if (not ok and extra) else ""))
    fails += not ok


def digest(base):
    return {s: hashlib.sha1(open(f"{base}.{s}.standard.1", "rb").read()).hexdigest() for s in ("res", "spk", "fet", "clu")}


def realign_log(rep):
    return next((r for r in rep.get("results", []) if "realignDone" in r), {})


def main():
    klusters = R.skip_without_binary()

    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        before = digest(base)
        rep = R.run(klusters, f"{base}.clu.standard.1", [{"realign": 99}], d, realignFromAnySpace=True)
        r = realign_log(rep)
        check(r.get("realignDone") is False, "a cluster that does not exist: the realign is not done")
        check("ERROR: cluster 99 not found." in r.get("log", ""), "... and the log says the cluster was not found",
              r.get("log", "")[-600:])
        check(digest(base) == before, "... and no file changed")

    with tempfile.TemporaryDirectory() as d:
        base, made = S.make_session(d, "rat")
        with open(f"{base}.res.standard.1", "r+b") as fh:
            fh.truncate(8 * 100)                      # the cluster's rows go far beyond row 100
        before = digest(base)
        rep = R.run(klusters, f"{base}.clu.standard.1", [{"realign": 2}], d, realignFromAnySpace=True)
        r = realign_log(rep)
        check(r.get("realignDone") is False, "a .res too short: the realign is not done")
        check("ERROR: cannot read .res at spike" in r.get("log", ""), "... and the log names the row that could not be read",
              r.get("log", "")[-600:])
        check(digest(base) == before, "... and no file changed")

    print(f"test_realign_failure_is_logged: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
