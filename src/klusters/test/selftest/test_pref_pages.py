#!/usr/bin/env python3
"""The Preferences dialog's pages scroll instead of being squeezed.

The Cluster view page is laid out for over 900 px.  In a dialog that gets less -- a 1080-line screen
leaves it about 780 -- the page used to be squeezed into what there was: its groups collapsed onto one
another and the lowest could not be read.  Every sub page now sits in a scroll area and keeps the
height it was laid out for.  With the dialog at 1000 x 700 (the `prefPage` step):

  * the Cluster view page is in a scroll area, is drawn at no less than its own height, has a scroll
    range, and scrolled to the end its last group ("Spike under the cursor") lies inside the dialog;
  * a page that fits (Splitting, Waveform view, Display) is in a scroll area too, with no range to scroll
    and its last group in view without scrolling;
  * the dialog no longer needs the tallest page's height.
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
    print(("  ok:   " if ok else "  FAIL: ") + what + (("\n        " + extra) if (not ok and extra) else ""))
    fails += not ok


def main():
    klusters = R.skip_without_binary()
    pages = ["PrefClusterView", "PrefSplitting", "PrefWaveformView", "PrefDisplay"]
    with tempfile.TemporaryDirectory() as d:
        base, _ = S.make_session(d, "pp")
        out = os.environ.get("PREF_PAGES_DIR", d)
        steps = [{"prefPage": c, "dialogSize": [1000, 700], "scrollToEnd": True,
                  "dialogSaveTo": os.path.join(out, f"{c}.png")} for c in pages]
        rep = R.run(klusters, f"{base}.clu.standard.1", steps, d)
        res = [r for r in rep.get("results", []) if "found" in r]
        check(rep.get("rc") == 0 and len(res) == len(pages), "the session ran every step",
              f"rc {rep.get('rc')}, {len(res)} of {len(pages)} results\n{rep.get('stderr', '')[-1500:]}")
        if len(res) != len(pages):
            return 1
        for c, r in zip(pages, res):
            keys = ('pageHeight', 'pageDrawnHeight', 'dialogHeight', 'scrollRange', 'lastGroup', 'lastGroupBottom', 'lastGroupBottomScrolled')
            what = str({k: r.get(k) for k in keys})
            check(r.get("found") and r.get("pageShown") and r.get("scrolls"), f"{c}: the page is in a scroll area")
            check(r.get("pageDrawnHeight", 0) >= r.get("pageHeight", 1e9), f"{c}: it is drawn at no less than the height it was laid out for", what)
            check(r.get("dialogHeight") == 700, f"{c}: the dialog takes the 700 px it is given ({r.get('dialogHeight')})")
            check(0 < r.get("lastGroupBottomScrolled", -1) <= r.get("dialogHeight", 0),
                  f"{c}: scrolled to the end, the last group ({r.get('lastGroup')!r}) is inside the dialog", what)
        cv = res[0]
        check(cv.get("pageHeight", 0) > 700 and cv.get("scrollRange", 0) > 0 and cv.get("lastGroupBottom", 0) > 700,
              f"Cluster view is taller than the dialog and scrolls ({cv.get('pageHeight')} px, range {cv.get('scrollRange')})")
        check(cv.get("lastGroup") == "Spike under the cursor", f"its last group is the hover one ({cv.get('lastGroup')!r})")
        for c, r in zip(pages[1:], res[1:]):
            check(r.get("scrollRange") == 0 and r.get("lastGroupBottom") == r.get("lastGroupBottomScrolled"),
                  f"{c} fits: nothing to scroll")
        check(cv.get("dialogMinHeight", 1e9) < 500, f"the dialog's minimum height is no longer the tallest page's ({cv.get('dialogMinHeight')} px)")
    print(f"test_pref_pages: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
