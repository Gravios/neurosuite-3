#!/usr/bin/env python3
"""A super-sampled space is read when it is first shown, not when the document opens.

Session: the .clu in `standard`.
  * built by hand (`supersample 4`, then `supersample 2`): `standard` stays shown, neither _U
    space is held in memory after the build;
  * `show standard_U4`: it is shown and held; standard_U2, never shown, is still not held;
  * `show standard`: standard_U4 stays held (switching back reads nothing), standard_U2 not;
  * super-sample MODE x4 on a fresh session: the space is built at open, `standard` stays
    shown and standard_U4 is not held until it is shown.
"""
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


def dumps_of(rep):
    check(rep.get("code") == 0 and rep["rc"] == 0,
          f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", ""))
    results = [r for r in rep.get("results", []) if "step" in r]
    for r in results:
        if not r.get("ok"):
            print("   step failed:", r)
    return [({s["method"]: s for s in r["dump"]["spaces"]}, r["dump"]["active"]) for r in results if "dump" in r]


def by_hand(klusters):
    print("-- built by hand")
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        steps = [{"dump": True}, {"supersample": 4}, {"supersample": 2}, {"dump": True},
                 {"show": "standard_U4"}, {"dump": True}, {"show": "standard"}, {"dump": True}]
        dumps = dumps_of(R.run(klusters, f"{base}.clu.standard.1", steps, d, supersampleInterp=0))
        if len(dumps) != 4:
            check(False, f"four dumps (got {len(dumps)})")
            return
        (s0, a0), (s1, a1), (s2, a2), (s3, a3) = dumps
        check(a0 == "standard" and s0["standard"]["held"], "at open `standard` is shown and held")
        check(a1 == "standard", f"after the builds `standard` is still shown (got {a1!r})")
        check({"standard_U4", "standard_U2"} <= set(s1) and s1["standard_U4"]["usable"] and s1["standard_U2"]["usable"],
              "both super-sampled spaces are listed and usable")
        check(not s1["standard_U4"]["held"] and not s1["standard_U2"]["held"],
              "neither super-sampled space is held in memory after its build")
        check(a2 == "standard_U4" and s2["standard_U4"]["held"], "shown, standard_U4 is held")
        check(s2["standard"]["held"], "... and `standard` (the .clu's own) stays held")
        check(not s2["standard_U2"]["held"], "... and standard_U2, never shown, is still not held")
        check(a3 == "standard" and s3["standard_U4"]["held"], "back in `standard`, standard_U4 stays held")
        check(not s3["standard_U2"]["held"], "... and standard_U2 is still not held")


def by_mode(klusters):
    print("-- super-sample mode x4")
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        steps = [{"dump": True}, {"show": "standard_U4"}, {"dump": True}]
        dumps = dumps_of(R.run(klusters, f"{base}.clu.standard.1", steps, d, supersampleMode=4, supersampleInterp=0))
        if len(dumps) != 2:
            check(False, f"two dumps (got {len(dumps)})")
            return
        (s0, a0), (s1, a1) = dumps
        check(os.path.exists(f"{base}.spk.standard_U4.1") and os.path.exists(f"{base}.fet.standard_U4.1"),
              "the mode built .spk.standard_U4.1 and .fet.standard_U4.1 at open")
        check(a0 == "standard", f"the session opened in `standard` (got {a0!r})")
        check("standard_U4" in s0 and s0["standard_U4"]["usable"] and not s0["standard_U4"]["held"],
              "standard_U4 is listed, usable and not held")
        check(a1 == "standard_U4" and s1["standard_U4"]["held"], "shown, it is held")


def main():
    klusters = R.skip_without_binary()
    by_hand(klusters)
    by_mode(klusters)
    print(f"test_supersampled_on_demand: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
