#!/usr/bin/env python3
"""Check the undo of a realign on a COPY of a real electrode group.

    KLUSTERS_BIN=<built klusters> python3 check_realign_undo_on_session.py <session>.clu[.<method>].<group> <cluster> [<cluster> ...]

The group's files are copied to a temporary directory (the recording itself, .fil / .dat, is linked, not
copied); nothing of the session is written.  For each cluster given, on a fresh copy:

  realign, undo, save          must leave the .res / .spk / .fet / .clu of every space as they were;
  realign, undo, redo, save    must leave what "realign, save" leaves.

Prints which files differ, if any.  Needs room for the group's files, three times per cluster.
Not a ctest: it needs a session.  The synthetic counterpart is test_realign_undo.py.
"""
import glob
import hashlib
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as R                # noqa: E402

KINDS = ("res", "spk", "fet", "clu", "clc")


def copy_group(clu, dest):
    src_dir = os.path.dirname(os.path.abspath(clu))
    name = os.path.basename(clu)
    base = name.split(".clu")[0]
    group = name.rsplit(".", 1)[1]
    for f in glob.glob(os.path.join(src_dir, base + ".*")):
        fn = os.path.basename(f)
        if fn.endswith((".pending", ".autosave", ".bak", ".saving")) or os.path.isdir(f):
            continue
        ext = fn[len(base) + 1:]
        if ext in ("fil", "dat", "lfp", "eeg"):
            os.symlink(f, os.path.join(dest, fn))            # the recording: read only, never copied
        elif ext.rsplit(".", 1)[-1] == group or "." not in ext:
            shutil.copy2(f, os.path.join(dest, fn))          # the group's files; the parameter files
    return os.path.join(dest, name), base


def digest(d, base):
    out = {}
    for f in sorted(glob.glob(os.path.join(d, base + ".*"))):
        fn = os.path.basename(f)
        if os.path.islink(f) or fn.endswith((".pending", ".autosave", ".bak")):
            continue
        if any(("." + k + ".") in fn for k in KINDS):
            h = hashlib.sha1()
            with open(f, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 24), b""):
                    h.update(chunk)
            out[fn] = h.hexdigest()
    return out


def run(klusters, clu, steps):
    with tempfile.TemporaryDirectory(dir=os.environ.get("TMPDIR")) as d:
        work, base = copy_group(clu, d)
        before = digest(d, base)
        rep = R.run(klusters, work, steps, d, realignFromAnySpace=True, autoRealign=False, autoRenumber=False,
                    autoMatrices=False)
        ok = rep.get("code") == 0 and rep.get("rc") == 0
        moved = [r.get("nShifted") for r in rep.get("results", []) if "realignDone" in r]
        return ok, digest(d, base), before, moved, rep.get("stderr", "")[-800:]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    klusters = R.skip_without_binary()
    clu, clusters = sys.argv[1], [int(x) for x in sys.argv[2:]]
    bad = 0
    for c in clusters:
        ok1, realigned, orig, moved, err = run(klusters, clu, [{"realign": c}, {"save": True}])
        ok2, undone, _, _, _ = run(klusters, clu, [{"realign": c}, {"undo": True}, {"wait": 500}, {"save": True}])
        ok3, redone, _, _, _ = run(klusters, clu, [{"realign": c}, {"undo": True}, {"wait": 500}, {"redo": True},
                                                  {"wait": 500}, {"save": True}])
        d_undo = sorted(k for k in set(orig) | set(undone) if orig.get(k) != undone.get(k))
        d_redo = sorted(k for k in set(realigned) | set(redone) if realigned.get(k) != redone.get(k))
        good = ok1 and ok2 and ok3 and realigned != orig and not d_undo and not d_redo
        bad += not good
        print(f"cluster {c}: {moved[0] if moved else '?'} spike(s) shifted by the realign"
              f"{'' if realigned != orig else '  (the realign changed no file: nothing was tested)'}")
        print(f"  realign, undo, save        {'identical to the session' if not d_undo else 'DIFFERS in ' + ', '.join(d_undo)}")
        print(f"  realign, undo, redo, save  {'identical to realign, save' if not d_redo else 'DIFFERS in ' + ', '.join(d_redo)}")
        if not (ok1 and ok2 and ok3):
            print("  a run failed:", err)
    print("PASS" if not bad else f"FAIL ({bad} cluster(s))")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
