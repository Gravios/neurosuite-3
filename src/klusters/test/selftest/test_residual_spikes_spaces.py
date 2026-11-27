#!/usr/bin/env python3
"""Add Residual Spikes and Rebuild Extended Records work from any shown feature space.

Both run in the .clu's own feature space: the residuals are made there and the stage written from
its files.  They used to refuse in any other space ("works in the .clu's own feature space; switch
back to it first").  Now they show the .clu's own space first, say so, and come back to the shown
one when nothing new is opened.

The session of test_residual_spikes (cluster 2 marked), with a projection space `alt` over the same
waveforms, and a super-sampled space standard_U4 built in the run:

  - NEGATIVE CONTROL: in standard_U4 the residuals themselves are not made ("A super-sampled waveform
    space is shown"): what the actions would meet without the switch;
  - from `alt`, from standard_U4 and from the local PCA of the selection, Add Residual Spikes writes
    the same stage, file for file, as from the .clu's own space; it reports the space it switched
    from, and that space is shown again afterwards;
  - Rebuild Extended Records on the stage with its .spkx removed, from `alt`: the .spkx Add Residual
    Spikes wrote, byte for byte, and `alt` shown again.  (Not from standard_U4: the stage has no rows
    of its own in it -- a super-sampled .spk is not cut from the recording -- so it is not shown there.)
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


def stage_files(d):
    return {f: hashlib.sha1(open(os.path.join(d, f), "rb").read()).hexdigest()
            for f in sorted(os.listdir(d)) if f.endswith(".rs") and ".pending" not in f}


def last(rep, key):
    return next((x for x in reversed(rep.get("results", [])) if key in x.get("step", {})), {})


def main():
    klusters = R.skip_without_binary()
    U4 = [{"supersample": 4}, {"show": "standard_U4"}]
    runs = {}
    for mode, pre, shown in (("own", [], ""), ("alt", [{"show": "alt"}], "alt"), ("U4", U4, "standard_U4"),
                             ("localPca", [{"select": [2, 3]}, {"keyCommand": "action.localPcaSpace"}], None)):
        with tempfile.TemporaryDirectory() as d:
            base, clu, plan, before, raw = M.build(d, "sp")
            T.add_spaces(base, raw, S.read_res(f"{base}.res.standard.1"))
            ok, _ = M.run(klusters, base, [{"markCollisions": {"clusters": [2], "apply": True}}, {"save": True}], d)
            if not ok:
                check(False, f"{mode}: cluster 2 marked and saved")
                continue
            if mode == "U4":
                rep = R.run(klusters, f"{base}.clu.standard.1", U4 + [{"residuals": {}}], d)
                r = last(rep, "residuals")
                check(r.get("ok") is False and "super-sampled" in r.get("reason", ""),
                      "NEGATIVE CONTROL: in standard_U4 the residuals themselves are not made", str(r.get("reason")))
            if mode == "U4":
                pre = [{"show": "standard_U4"}]                      # built by the run above
            rep = R.run(klusters, f"{base}.clu.standard.1", pre + [{"residualSpikes": {"clusters": [2], "apply": True}}], d)
            r = last(rep, "residualSpikes")
            before_step = [x for x in rep.get("results", []) if "residualSpikes" not in x.get("step", {})]
            pre_ok = all(x.get("ok", True) is not False for x in before_step)
            want_shown = shown or "standard"           # (the local PCA: back to the space it was entered from)
            ok_switch = r.get("switchedFrom") == ("standard" if mode == "localPca" else shown)
            check(pre_ok and r.get("written") is True and ok_switch and r.get("activeAfter") == want_shown,
                  f"from {'the .clu\'s own space' if mode == 'own' else mode}: the stage is written"
                  + ("" if mode == "own" else f", the action switched from {shown or 'the local PCA'} and came back"),
                  str({k: r.get(k) for k in ("written", "switchedFrom", "activeAfter", "reason")}))
            runs[mode] = stage_files(d)
            if mode == "U4" and r.get("written"):
                spkx = f"{base}.spkx.standard.1.rs"
                keep = open(spkx, "rb").read()
                os.remove(spkx)
                rep = R.run(klusters, f"{base}.clu.standard.1.rs", [{"show": "alt"}, {"rebuildExtended": {"apply": True}}], d)
                # (alt: the stage grew it; standard_U4 it did not -- a super-sampled .spk is not cut from the recording)
                rb = last(rep, "rebuildExtended")
                if not rb:
                    print("      (the run:", [{k: x.get(k) for k in ("step", "ok", "error", "token", "active")} for x in rep.get("results", [])],
                          rep.get("stderr", "")[-600:], ")")
                check(rb.get("ok") is True and rb.get("switchedFrom") == "alt" and rb.get("activeAfter") == "alt"
                      and os.path.exists(spkx) and open(spkx, "rb").read() == keep,
                      "Rebuild Extended Records from alt, in the stage: the stage's .spkx, byte for byte, and alt shown again",
                      str({k: rb.get(k) for k in ("ok", "reason", "found", "switchedFrom", "activeAfter", "error")}))
    own = runs.get("own", {})
    check(bool(own) and any(f.startswith("sp.spkx.") for f in own), "the own-space run wrote a stage (with its .spkx)")
    for mode in ("alt", "U4", "localPca"):
        got = runs.get(mode, {})
        # standard_U4's own files belong to that run only (the space was built there)
        diff = sorted(f for f in set(own) | set(got) if "_U4" not in f and own.get(f) != got.get(f))
        check(bool(got) and not diff, f"from {mode}: the same stage, file for file, as from the .clu's own space", str(diff))
    print("FAILED" if M.fails else "PASSED", f"({M.fails} failure(s))" if M.fails else "")
    return 1 if M.fails else 0


if __name__ == "__main__":
    sys.exit(main())
