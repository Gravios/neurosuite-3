#!/usr/bin/env python3
"""Edit > Cancel Running Job.

  - a recluster is stopped: no failure box, the clustering and the undo history are as they were,
    and the same commands are enabled afterwards as before it started;
  - the realign of several clusters that follows an edit (here: the eight clusters a recluster
    left) stops after the cluster in progress -- fewer clusters realigned than without the cancel,
    the session saves, and the editing commands are back;
  - with no job running the command is off and does nothing.

KiloKlustaKwik_cpu is needed for both jobs (KKK_BIN, else the build tree's; skipped without).
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                           # noqa: E402
import session as S                       # noqa: E402
import test_curation_log as T             # noqa: E402
import test_edit_lock as L                # noqa: E402
import test_recluster_klustakwik as K     # noqa: E402

fails = 0
H = lambda **k: {"hierarchy": k}          # noqa: E731
AE = {"actionsEnabled": True}


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def main():
    klusters = R.skip_without_binary()
    kkk = K.sorter()
    if not kkk:
        print("test_cancel_job: SKIP (no KiloKlustaKwik_cpu: build it or set KKK_BIN)")
        return 0
    rc = {"recluster": "", "opts": dict(exe=kkk, autoSelect=False, args=K.T + "1111111111111")}

    # ---- a recluster
    with tempfile.TemporaryDirectory() as d:
        base = L.big_hier(d, "r", 9000)
        rep = R.run(klusters, f"{base}.clu.{L.U.M}.1",
                    [H(), H(showChildren=[20000]), H(showChildren=[]), {"select": [2]}, AE, rc,
                     {"cancelJob": {"when": "process"}, "whileBusy": True}, H(), AE, {"cancelJob": True}],
                    d, timeout=900, autoRenumber=False, autoRealign=False)
        res = rep.get("results", [])
        can = [r for r in res if "cancelJob" in r.get("step", {})]
        hs = [r for r in res if "childParents" in r]
        ae = [r for r in res if "enabled" in r]
        check(len(can) == 2 and can[0].get("cancellable") and can[0].get("job") == "process", "a running recluster can be cancelled", can)
        check(len(hs) >= 2 and hs[-1]["parentSizes"] == hs[0]["parentSizes"] and hs[-1]["childSizes"] == hs[0]["childSizes"]
              and hs[-1].get("undoSteps") == 0, "cancelled: the clustering and the undo history are unchanged",
              hs[-1].get("parentSizes") if hs else None)
        check(not [r for r in res if "dialog" in r], "... without a failure box", [r.get("dialog") for r in res if "dialog" in r])
        check(len(ae) == 2 and set(ae[0]["enabled"]) == set(ae[1]["enabled"]) and "edit.cancelJob" not in ae[1]["enabled"],
              "... and the same commands are enabled as before; Cancel is off again",
              sorted(set(ae[0]["enabled"]) ^ set(ae[1]["enabled"])) if len(ae) == 2 else None)
        check(len(can) == 2 and not can[1].get("cancellable") and not can[1].get("actionEnabled"), "with no job running there is nothing to cancel", can[1:])

    # ---- the realign of several clusters after an edit
    done = {}
    for cancel in (False, True):
        with tempfile.TemporaryDirectory() as d:
            base, _ = S.make_session(d, "a", n_per_unit=9000, method=T.METHOD)
            steps = [{"select": [2]}, rc] + ([{"cancelJob": {"when": "realign"}, "whileBusy": True}] if cancel else []) \
                + [{"palette": True}, AE, {"save": True}]
            rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1", steps, d, timeout=1200, autoRenumber=False, autoRealign=True, autoMatrices=False)
            det = [o for o in T.log_lines(d, "a") if o.get("action") == "REALIGN" and o.get("event") == "ACTION_DETAIL" and o.get("edit") == "realign"]
            done[cancel] = (len({o["action_idx"] for o in det}), rep)
    n_all, _ = done[False]
    n_cut, rep = done[True]
    can = [r for r in rep.get("results", []) if "cancelJob" in r.get("step", {})]
    ae = [r for r in rep.get("results", []) if "enabled" in r]
    check(n_all >= 3, f"(the recluster leaves several clusters to realign: {n_all} realigned without a cancel)")
    check(bool(can) and can[0].get("job") == "realign", "the realign of several clusters can be cancelled", can)
    check(0 < n_cut < n_all, f"it stops after the cluster in progress ({n_cut} of {n_all} realigned)")
    check(rep.get("rc") == 0 and bool(ae) and "edit.undo" in ae[0]["enabled"] and "edit.cancelJob" not in ae[0]["enabled"],
          "the session goes on: editing is back, it saves", rep.get("stderr", "")[-300:])
    print(f"test_cancel_job: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
