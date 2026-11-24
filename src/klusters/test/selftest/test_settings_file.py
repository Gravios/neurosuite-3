#!/usr/bin/env python3
"""What a settings file becomes by being read and written again is the snapshot's.

Configuration::read() takes each preference from the settings file -- with its default when the
key is absent, its limits, and the one-time changes of an old default -- and write() stores them
all.  This runs seven cases through read + write and compares the result, key by key, with
settings.golden.json:

  default   no settings file: every default
  changed   every flag flipped, every number moved a little
  low       every number at -7: the lower limits
  high      every number at 100000: the upper limits
  legacy    the values of old defaults with none of the "already migrated" flags
  dialog    the "changed" file, then Preferences opened and OK pressed with nothing touched:
            the pages show and give back what was read (where a page's range is narrower
            than the preference's, the value comes back inside the page's)
  defaults  the "changed" file, then Preferences > Defaults and OK: every setting a page shows
            is its default again (a key that is not is one no page shows -- or a default
            written twice with two values)

A changed default, limit or key is a changed line of that file: `--update` rewrites it, and the
diff is to be read.  Only the groups Configuration writes are compared (General, clusterView,
waveformView); strings and colours are left as they are.
"""
import configparser
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
GOLDEN = os.path.join(HERE, "settings.golden.json")
GROUPS = ("%General", "clusterView", "waveformView")
FLAGS = ("nbUndoRaisedFromOldDefault", "realignThresholdLoweredFromOldDefault", "settingsRevision")
UNBOUND = []          # rows of the preference table that name a control no page has
INT, NUM = re.compile(r"^-?\d+$"), re.compile(r"^-?\d+\.\d+(e-?\d+)?$")


def through(klusters, text, defaults=False, dialog=False):
    """The compared groups of the settings file after one read + write of @p text (None: no file)."""
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        path = os.path.join(d, "cfg", "neurosuite-3", "klusters.conf")
        if text is not None:
            os.makedirs(os.path.dirname(path))
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"configWrite": True, "defaults": defaults, "dialog": dialog}], d, timeout=600)
        for r in rep.get("results", []):
            UNBOUND.extend(r.get("unboundPreferenceRows", []))
        cp = configparser.RawConfigParser(interpolation=None, delimiters=("=",))
        cp.optionxform = str
        with open(path, encoding="utf-8") as fh:
            cp.read_file(fh)
    return {g: dict(cp[g]) for g in GROUPS if cp.has_section(g)}


def as_text(groups):
    return "".join(f"[{g}]\n" + "".join(f"{k}={v}\n" for k, v in kv.items()) + "\n" for g, kv in groups.items())


def moved(groups, how):
    out = {}
    for g, kv in groups.items():
        o = out[g] = {}
        for k, v in kv.items():
            if k in FLAGS:
                o[k] = v
            elif v in ("true", "false"):
                o[k] = ("false" if v == "true" else "true") if how == "changed" else v
            elif INT.match(v):
                o[k] = {"changed": str(int(v) + 1), "low": "-7", "high": "100000"}[how]
            elif NUM.match(v):
                o[k] = {"changed": repr(float(v) * 0.5 + 0.125), "low": "-7.5", "high": "100000.5"}[how]
            else:
                o[k] = v
    return out


def cases(klusters):
    default = through(klusters, None)
    out = {"default": default}
    for how in ("changed", "low", "high"):
        out[how] = through(klusters, as_text(moved(default, how)))
    out["dialog"] = through(klusters, as_text(moved(default, "changed")), dialog=True)
    out["defaults"] = through(klusters, as_text(moved(default, "changed")), defaults=True)
    out["legacy"] = through(klusters, "[%General]\nnbUndo=2\nrealignThreshold=0.7\nautoShowMatricesOnOpen=false\n"
                                       "templateXcorrPearson=true\nreclusteringExecutable=KlustaKwik\n")
    return out


def main():
    klusters = R.skip_without_binary()
    got = cases(klusters)
    if "--update" in sys.argv:
        with open(GOLDEN, "w", encoding="utf-8") as fh:
            json.dump(got, fh, indent=1, sort_keys=True, ensure_ascii=False)
            fh.write("\n")
        print(f"test_settings_file: wrote {GOLDEN} ({sum(len(kv) for kv in got['default'].values())} keys)")
        return 0
    with open(GOLDEN, encoding="utf-8") as fh:
        want = json.load(fh)
    diffs = []
    for case in want:
        for g in GROUPS:
            w, h = want[case].get(g, {}), got.get(case, {}).get(g, {})
            diffs += [f"{case} [{g}] {k}: {w.get(k)!r} -> {h.get(k)!r}" for k in sorted(set(w) | set(h)) if w.get(k) != h.get(k)]
    n = sum(len(kv) for kv in want["default"].values())
    # Values the cases above do not reach: the last entry of a combo box of values, the last
    # radio button, a colour, a number typed with many digits.  Preferences opened and OK
    # pressed must give them back.
    special = ("[%General]\nsupersampleMode=4\nrealignMode=3\nbackgroundColor=#204060\ntsneTheta=0.25\n"
               "tsneSpikeCap=45000\nprojectionScopeMode=1\nreorderMethod=2\nwaveformSortMean=true\n"
               "nbUndoRaisedFromOldDefault=true\nrealignThresholdLoweredFromOldDefault=true\nsettingsRevision=1\n")
    plain, viaDialog = through(klusters, special), through(klusters, special, dialog=True)
    for k in ("supersampleMode", "realignMode", "backgroundColor", "tsneTheta", "tsneSpikeCap", "projectionScopeMode",
              "reorderMethod", "waveformSortMean"):
        a, b = plain["%General"].get(k), viaDialog["%General"].get(k)
        if a != b:
            diffs.append(f"through the dialog {k}: {a!r} -> {b!r}")
    for k, v in (("supersampleMode", "4"), ("realignMode", "3"), ("tsneSpikeCap", "45000"), ("reorderMethod", "2")):
        if plain["%General"].get(k) != v:
            diffs.append(f"read {k}={v}: {plain['%General'].get(k)!r}")
    diffs += [f"the preference table names a control no page has: {u}" for u in sorted(set(UNBOUND))]
    print(("  ok:   " if not diffs else "  FAIL: ") + f"the {n} keys of the settings file are the snapshot's in the {len(want)} cases")
    for x in diffs[:40]:
        print("        " + x)
    print(f"test_settings_file: {'PASS' if not diffs else 'FAIL'} ({len(diffs)} differences)")
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
