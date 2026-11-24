#!/usr/bin/env python3
"""Preferences can be worked from the keyboard, in two levels (PageKeyNavigator).

  - it opens on the page list; Tab there goes round the list and the dialog's buttons and
    never into the page;
  - Enter on a group opens it on its first page; Enter on a page goes into it;
  - in a page Tab goes round its settings and comes back to the first, Shift+Tab from the
    first goes to the last;
  - Space toggles a check box, and so does Enter; Enter on a number leaves the dialog open;
  - Esc in a page goes back to the list; Esc in the list closes the dialog.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402

fails = 0


def check(ok, what, extra=""):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + str(extra)) if (not ok and extra) else ""))
    fails += not ok


def keys(klusters, seq):
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"prefKeys": seq}], d, timeout=600)
    r = next((x for x in rep.get("results", []) if "trace" in x), None)
    if r is None:
        check(False, "the run finished", rep.get("stderr", "")[-400:])
        return []
    return r["trace"]


def main():
    klusters = R.skip_without_binary()

    # the list level
    t = keys(klusters, ["Tab"] * 12 + ["Backtab"])
    if not t:
        return 1
    check(t[0]["level"] == "list", "it opens on the page list", t[0])
    levels = [x["level"] for x in t[1:13]]
    check("page" not in levels and "list" in levels and "buttons" in levels,
          "Tab goes round the list and the buttons, never into the page", levels)
    n = levels.index("list") + 1                                   # the length of the round
    check(n < 12 and levels[n - 1 + n if 2 * n - 1 < 12 else n - 1] == "list", "and comes back to the list every round", levels)
    check(t[13]["level"] == "buttons" or n == 1, "Shift+Tab goes the other way", t[13])

    # into a group, into a page, round its settings
    t = keys(klusters, ["Return", "Down", "Return"])
    check(t[1]["level"] == "list" and t[1]["page"] != t[0]["page"], "Enter on a group opens it on its first page", t[1])
    check(t[3]["level"] == "page", "Enter on a page goes into it", t[3])
    stops = t[3]["stops"]
    first = t[3]["focus"]
    t = keys(klusters, ["Return", "Down", "Return"] + ["Tab"] * stops + ["Backtab"] + ["Escape", "Escape"])
    walk = [x["focus"] for x in t[3:4 + stops]]
    check(stops >= 3 and len(set(walk[:-1])) == stops, f"Tab stops at each of the page's {stops} settings", walk)
    check(walk[-1] == first and all(x["level"] == "page" for x in t[3:4 + stops]), "and comes back to the first, without leaving the page", walk)
    back = t[4 + stops]
    check(back["focus"] == walk[stops - 1], "Shift+Tab from the first goes to the last", back)
    esc, esc2 = t[5 + stops], t[6 + stops]
    check(esc["level"] == "list" and esc["open"], "Esc in a page goes back to the list", esc)
    check(not esc2["open"], "Esc in the list closes the dialog", esc2)

    # a check box, a number
    kinds = [w.split(":")[0] for w in walk[:-1]]
    if "QCheckBox" in kinds:
        to = ["Tab"] * kinds.index("QCheckBox")
        t = keys(klusters, ["Return", "Down", "Return"] + to + ["Space", "Return"])
        a, b, c = t[-3], t[-2], t[-1]
        check(b["checked"] != a["checked"], "Space toggles a check box", (a, b))
        check(c["checked"] == a["checked"] and c["open"] and c["level"] == "page", "Enter toggles it too, and the dialog stays open", c)
    else:
        check(False, "the page has a check box to try", kinds)
    spin = next((k for k in kinds if k in ("QSpinBox", "QDoubleSpinBox")), None)
    if spin:
        t = keys(klusters, ["Return", "Down", "Return"] + ["Tab"] * kinds.index(spin) + ["Return"])
        check(t[-1]["open"] and t[-1]["level"] == "page" and t[-1]["focus"] == t[-2]["focus"],
              "Enter on a number commits it: the dialog stays open and the focus stays", t[-1])
    else:
        print("  note: the page has no number box; Enter on one was not tried")
    print(f"test_pref_keyboard: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
