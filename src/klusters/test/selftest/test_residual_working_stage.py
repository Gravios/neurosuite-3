#!/usr/bin/env python3
"""Add Residual Spikes writes a working stage; Save makes its rows the open stage's.

The command used to write a new stage `<stage>_rs` and open it, so every add made one more stage
and the GUI started over.  Now the rows go to a working stage (`<target>_rw`) that opens with the
displays kept; nothing of the target is touched until Save, which gives the working stage's files
the target's names and opens the target.  Closing without saving drops the rows.  The target is
the open stage, `rs` for an untagged session.

The session of test_residual_spikes (cluster 2 marked; with both 2 and 3 marked for the repeat):
the stage the command used to write (`residualSpikes` without "open": `<stage>_rs`) is the
reference for what is saved.

  - untagged session, added from `alt`: the working stage `rs_rw` is open, the title names
    `...rs` as not saved, no `rs` file exists yet; Save: `rs` opens, every file of the old `rs`
    stage is there with the same rows (byte for byte but the .clu, read back as ids), no `_rw`
    file is left, the session's own files are unchanged, and the displays and `alt` are kept;
  - a tagged stage `cur` sharing the session's rows: closed without saving (the question answered
    No): no `cur_rw` file is left and `cur` is unchanged (NEGATIVE CONTROL for the Save); closed
    with Yes: `cur` holds its own rows, the same as the `cur_rs` the command used to write;
  - two adds before the Save (cluster 2, then cluster 3): the second working stage replaces the
    first (`rs_rw2`, no `rs_rw` file left), and the saved `rs` is the stage two adds used to make
    (`rs` then `rs_rs`).
"""
import hashlib
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S                       # noqa: E402
import run as R                           # noqa: E402
import test_mark_collisions as M          # noqa: E402
import test_residual_spikes as T          # noqa: E402

check = M.check
ADD = lambda c: {"residualSpikes": {"clusters": c, "apply": True, "open": True}}   # noqa: E731


def files(d, tag):
    """{name with the stage tag as '<T>': sha1} of the stage's files (.clu: its ids)."""
    out = {}
    for f in sorted(os.listdir(d)):
        if not f.endswith("." + tag) or not f.startswith("s."):
            continue
        p = os.path.join(d, f)
        key = f[: -len(tag)] + "<T>"
        if ".clu." in f:
            out[key] = hashlib.sha1(S.read_clu(p).tobytes()).hexdigest()
        elif ".cur." in f or ".cli." in f:
            continue                                             # the log and the cluster notes: not rows
        else:
            out[key] = hashlib.sha1(open(p, "rb").read()).hexdigest()
    return out


# (the Save also writes what is rebuilt from the saved rows -- .spkc, .spkr, .fetr -- which the stage
# the command used to write did not have: the comparison is over the reference's files)


def any_with(d, part):
    return sorted(f for f in os.listdir(d) if part in f)


def session(d, marked):
    base, clu, plan, before, raw = M.build(d, "s")
    T.add_spaces(base, raw, S.read_res(f"{base}.res.standard.1"))
    ok, _ = M.run(R.binary(), base, [{"markCollisions": {"clusters": marked, "apply": True}}, {"save": True}], d)
    return base, ok


def reference(klusters, d, clu, steps_clusters):
    """The stage(s) the command used to write, from @p clu: each add without "open" from the last."""
    for cl in steps_clusters:
        rep = R.run(klusters, clu, [{"residualSpikes": {"clusters": cl, "apply": True}}], d)
        r = next((x for x in rep.get("results", []) if "residualSpikes" in x.get("step", {})), {})
        if not r.get("written"):
            return None, r
        clu = r["stage"]
    return clu, {}


def gui(s):
    return [(x["type"], tuple(sorted(x["clusters"]))) for x in s.get("displays", [])], s.get("featureSpace")


def main():
    klusters = R.skip_without_binary()

    # ── untagged: Save makes `rs` ──
    with tempfile.TemporaryDirectory() as d:
        base, ok = session(d, [2])
        check(ok, "cluster 2 marked and saved")
        cluf = f"{base}.clu.standard.1"
        ref, why = reference(klusters, d, cluf, [[2]])
        want = files(d, "rs")
        for f in any_with(d, ".1.rs"):
            os.remove(os.path.join(d, f))
        own = {f: hashlib.sha1(open(os.path.join(d, f), "rb").read()).hexdigest() for f in os.listdir(d)
               if f.startswith("s.") and (f.endswith(".1") or f.endswith(".yaml")) and ".cur." not in f}   # (the log grows with every open)
        check(bool(want) and ref is not None, "the reference: the stage `rs` the command used to write", str(why))
        rep = R.run(klusters, cluf, [{"show": "alt"}, {"select": [2, 3]}, {"guiState": {"newDisplay": "waveforms"}},
                                     {"select": [3]}, {"guiState": {}}, ADD([2]), {"guiState": {}}, {"save": True},
                                     {"dump": True, "cluster": 2}, {"guiState": {}}], d)
        res = rep.get("results", [])
        r = next((x for x in res if "residualSpikes" in x.get("step", {})), {})
        w = r.get("working", {})
        g = [x for x in res if "guiState" in x.get("step", {})]
        dump = next((x for x in res if "dump" in x.get("step", {})), {}).get("dump", {})
        check(r.get("opened") is True and w.get("work") == "rs_rw" and w.get("target") == "rs"
              and "s.clu.standard.1.rs (residual spikes added, not saved)" in w.get("title", ""),
              "untagged: the working stage rs_rw is open, the title names s.clu.standard.1.rs as not saved",
              str({k: r.get(k) for k in ("opened", "working", "summary", "error")}))
        g = g[-3:]                                               # before the add, after it, after the Save
        check(len(g) == 3 and gui(g[0]) == gui(g[1]), "the displays and alt kept into the working stage",
              str([gui(x) for x in g]))
        got = files(d, "rs")
        check(dump.get("clu", "").endswith("s.clu.standard.1.rs") and not dump.get("working")
              and gui(g[-1]) == gui(g[0]), "Save: the stage rs is open, no working stage, the displays and alt kept",
              str({"clu": dump.get("clu"), "working": dump.get("working"), "gui": gui(g[-1]) if g else None}))
        diff = sorted(k for k in want if want[k] != got.get(k))
        check(bool(got) and not diff, "the saved rs: every file of the stage the command used to write, the same rows", str(diff))
        check(not any_with(d, "_rw"), "no working-stage file is left", str(any_with(d, "_rw")))
        now = {f: hashlib.sha1(open(os.path.join(d, f), "rb").read()).hexdigest() for f in own}
        check(now == own, "the session's own files are unchanged",
              str(sorted(f for f in own if own[f] != now.get(f))))

    # ── a tagged stage sharing the session's rows: No drops the rows, Yes makes them the stage's ──
    with tempfile.TemporaryDirectory() as d:
        base, ok = session(d, [2])
        cur = f"{base}.clu.standard.1.cur"
        shutil.copy(f"{base}.clu.standard.1", cur)
        ref, why = reference(klusters, d, cur, [[2]])
        want = files(d, "cur_rs")
        for f in any_with(d, ".cur_rs"):
            os.remove(os.path.join(d, f))
        before = files(d, "cur")
        rep = R.run(klusters, cur, [ADD([2]), {"close": True}], d, answerNo=["not saved"])
        c = next((x for x in rep.get("results", []) if "close" in x.get("step", {})), {})
        check(c.get("closed") is True and c.get("workingBefore", {}).get("target") == "cur",
              "cur: the working stage open, then closed answering No", str(c or rep.get("stderr", "")[-300:]))
        check(not any_with(d, "_rw") and files(d, "cur") == before and not any_with(d, "stalebkp"),
              "NEGATIVE CONTROL: closed without saving, no cur_rw file is left and cur is unchanged",
              str((any_with(d, "_rw"), any_with(d, "stalebkp"))))
        rep = R.run(klusters, cur, [ADD([2]), {"close": True}, {"open": cur}, {"dump": True, "cluster": 2}], d)
        dump = next((x for x in rep.get("results", []) if "dump" in x.get("step", {})), {}).get("dump", {})
        got = files(d, "cur")
        diff = sorted(k for k in want if want[k] != got.get(k))
        check(bool(want) and dump.get("clu") == cur and not diff and not any_with(d, "_rw"),
              "closed with Yes: cur holds its own rows, the same as the cur_rs the command used to write",
              str({"diff": diff, "clu": dump.get("clu"), "rw": any_with(d, "_rw"), "why": why}))

    # ── two adds, one Save ──
    with tempfile.TemporaryDirectory() as d:
        base, ok = session(d, [2, 3])
        cluf = f"{base}.clu.standard.1"
        ref, why = reference(klusters, d, cluf, [[2], [3]])
        want = files(d, "rs_rs")
        for f in any_with(d, ".1.rs"):
            os.remove(os.path.join(d, f))
        rep = R.run(klusters, cluf, [ADD([2]), ADD([3]), {"save": True}, {"dump": True, "cluster": 2}], d)
        adds = [x for x in rep.get("results", []) if "residualSpikes" in x.get("step", {})]
        dump = next((x for x in rep.get("results", []) if "dump" in x.get("step", {})), {}).get("dump", {})
        works = [x.get("working", {}).get("work") for x in adds]
        got = files(d, "rs")
        diff = sorted(k for k in want if want[k] != got.get(k))
        check(works == ["rs_rw", "rs_rw2"] and all(x.get("working", {}).get("target") == "rs" for x in adds),
              "two adds: the second working stage (rs_rw2) replaces the first, both for rs", str(works))
        check(bool(want) and dump.get("clu", "").endswith(".1.rs") and not diff and not any_with(d, "_rw"),
              "Save: rs is the stage two adds used to make (rs, then rs_rs), no working file left",
              str({"diff": diff, "clu": dump.get("clu"), "rw": any_with(d, "_rw"), "why": why}))
    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
