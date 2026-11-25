#!/usr/bin/env python3
"""Super-sample mode asks before it builds waveforms that are not there.

Preferences > Realignment > Super-sampling x4 on a session without `.spk.standard_U4`:
  * the question is asked; Yes (its default) builds the .spk, the basis and the .fet;
  * No builds nothing, turns the preference off in the settings file, and the session carries
    on in `standard`; reopened, it is not asked again;
  * with the .spk there and only the .fet missing nothing is asked: the .fet is fitted;
  * a `.clu.standard_U4` opened without its .spk: No leaves the session in the base clustering.
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import session as S            # noqa: E402
import run as R                # noqa: E402

fails = 0
ASK = "Build them now?"


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n" + extra) if (not ok and extra) else ""))
    fails += not ok


def parts(rep):
    check(rep.get("code") == 0 and rep["rc"] == 0, f"the self-test ran every step (code {rep.get('code')}, rc {rep['rc']})", rep.get("stderr", ""))
    res = rep.get("results", [])
    asked = [r["dialog"] for r in res if ASK in r.get("dialog", "")]
    dumps = [({s["method"]: s for s in r["dump"]["spaces"]}, r["dump"]["active"]) for r in res if "dump" in r]
    return asked, dumps


def mode_in_settings(d):
    path = os.path.join(d, "cfg", "neurosuite-3", "klusters.conf")
    if not os.path.exists(path):
        return None
    for line in open(path, encoding="utf-8"):
        if line.strip().startswith("supersampleMode="):
            return line.strip().split("=", 1)[1]
    return None


def main():
    klusters = R.skip_without_binary()
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "rat")
        spk, fet = f"{base}.spk.standard_U4.1", f"{base}.fet.standard_U4.1"

        print("-- No")
        asked, dumps = parts(R.run(klusters, f"{base}.clu.standard.1", [{"dump": True}, {"reopen": True}, {"dump": True}], d,
                                   supersampleMode=4, supersampleInterp=0, answerNo=[ASK]))
        check(len(asked) == 1, f"the question was asked once, not again at the reopen (asked {len(asked)} times)")
        check(asked and "standard_U4" in asked[0] and "MB" in asked[0], "it names the missing .spk and its size", str(asked))
        check(not os.path.exists(spk) and not os.path.exists(fet), "nothing was built")
        check(mode_in_settings(d) == "0", f"the preference is off in the settings file (supersampleMode={mode_in_settings(d)})")
        check(len(dumps) == 2 and all(a == "standard" and "standard_U4" not in s for s, a in dumps),
              "the session carries on in `standard`, with no super-sampled space listed")

        print("-- Yes (the default)")
        os.remove(os.path.join(d, "cfg", "neurosuite-3", "klusters.conf"))      # the No above is in it
        asked, dumps = parts(R.run(klusters, f"{base}.clu.standard.1", [{"dump": True}], d, supersampleMode=4, supersampleInterp=0))
        check(len(asked) == 1, f"the question was asked (asked {len(asked)} times)")
        check(os.path.exists(spk) and os.path.exists(fet), "the .spk and the .fet were built")
        check(len(dumps) == 1 and dumps[0][1] == "standard" and dumps[0][0].get("standard_U4", {}).get("usable"),
              "`standard` stays shown and standard_U4 is listed and usable")
        check(mode_in_settings(d) != "0", "the preference was not turned off in the settings file")

        print("-- the .spk is there, the .fet is not")
        os.remove(fet)
        asked, dumps = parts(R.run(klusters, f"{base}.clu.standard.1", [{"dump": True}], d, supersampleMode=4, supersampleInterp=0, answerNo=[ASK]))
        check(not asked, f"nothing was asked (asked {len(asked)} times)")
        check(os.path.exists(fet), "the .fet was fitted on the waveforms that were there")

    with tempfile.TemporaryDirectory() as d:
        print("-- a super-sampled .clu without its .spk, No")
        base, _ = S.make_session(d, "rat")
        shutil.copy(f"{base}.clu.standard.1", f"{base}.clu.standard_U4.1")
        asked, dumps = parts(R.run(klusters, f"{base}.clu.standard_U4.1", [{"dump": True}], d, supersampleInterp=0, answerNo=[ASK]))
        check(len(asked) == 1 and "opened through its base clustering" in asked[0], "the question says the session was opened through the base clustering", str(asked))
        check(not os.path.exists(f"{base}.spk.standard_U4.1"), "nothing was built")
        check(len(dumps) == 1 and dumps[0][1] == "standard", "the session stays in `standard`")
        check(mode_in_settings(d) == "0", f"the preference is off in the settings file (supersampleMode={mode_in_settings(d)})")

    print(f"test_supersample_mode_asks: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
