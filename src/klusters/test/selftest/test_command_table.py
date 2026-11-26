#!/usr/bin/env python3
"""The commands of the menus are what `commands.golden.json` says.

Every command of the menu bar, in menu order -- its id, menu, text, keys, tips, whether it is
checkable, checked, enabled, and whether the edit lock holds it while a job runs -- and the
binding registry's commands by id (their order is not shown anywhere), in two states: a flat session just opened, and a
hierarchical session in the child view.

A change of a menu, a key, a tip or a command id is a change of that file: run this with
`--update`, read the diff, commit it with the change.  It is the check under any rewrite of how
the commands are declared (the command table, 2026-11-24): the snapshot must not move.

The snapshot was taken on Linux; elsewhere the keys are not compared (the standard ones differ).
Quit's key is not compared anywhere: QKeySequence::Quit is what the Qt version and the platform
theme make of it (none on Qt 6.4 without a desktop theme, the Exit key on a later Qt).
Commands of the Plugins menu and recluster engines that plugins add are not compared: they
depend on what is installed.
"""
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402
import test_undo_pairing as U      # noqa: E402

GOLDEN = os.path.join(HERE, "commands.golden.json")
LINUX = sys.platform.startswith("linux")       # the snapshot's keys were taken there
H = lambda **k: {"hierarchy": k}   # noqa: E731
KEYS = ("commands", "registry", "editLocked")
PLATFORM_KEY = {"file.quit"}       # QKeySequence::Quit: differs with the Qt version and the platform theme


def dump(klusters):
    out = {}
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        rep = R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"commandDump": True}], d, timeout=600, autoRealign=False, autoRenumber=False)
        r = next(x for x in rep["results"] if "commands" in x)
        out["flat_open"] = {k: r[k] for k in KEYS} | {"replacedIds": r.get("replacedIds", [])}
    with tempfile.TemporaryDirectory() as d:
        base = U.hier(d, "h")
        rep = R.run(klusters, f"{base}.clu.{U.M}.1",
                    [H(), {"select": [2]}, {"childPalette": {"show": True}}, {"childPalette": {"click": 20000}}, {"commandDump": True}],
                    d, timeout=600)
        r = next(x for x in rep["results"] if "commands" in x)
        out["hier_child_view"] = {k: r[k] for k in KEYS} | {"replacedIds": r.get("replacedIds", [])}
    return out


def write_golden(got):
    """One command a line, so that a change of one command is a change of one line."""
    one = lambda o: json.dumps(o, sort_keys=True, ensure_ascii=False)     # noqa: E731
    with open(GOLDEN, "w", encoding="utf-8") as fh:
        fh.write("{\n")
        for si, state in enumerate(sorted(got)):
            fh.write(f' "{state}": {{\n')
            for ki, key in enumerate(KEYS):
                rows = got[state][key]
                fh.write(f'  "{key}": [\n' + ",\n".join("   " + one(r) for r in rows) + "\n  ]" + ("," if ki < len(KEYS) - 1 else "") + "\n")
            fh.write(" }" + ("," if si < len(got) - 1 else "") + "\n")
        fh.write("}\n")


def installed(c):
    return c["menu"].startswith("Plugins") or (c["menu"].endswith("Engine") and c["id"].startswith("menu:"))


def differences(got, want):
    diffs0 = [f"{state}: command id registered twice (the later replaced the earlier): {got[state]['replacedIds']}"
              for state in got if got[state].get("replacedIds")]
    diffs = diffs0
    for state in want:
        g, w = got.get(state, {}), want[state]
        gc = [c for c in g.get("commands", []) if not installed(c)]
        wc = [c for c in w["commands"] if not installed(c)]
        if [c["id"] for c in gc] != [c["id"] for c in wc]:
            gi, wi = [c["id"] for c in gc], [c["id"] for c in wc]
            diffs.append(f"{state}: commands or their order: gone {sorted(set(wi) - set(gi))}, new {sorted(set(gi) - set(wi))}"
                         + ("" if set(gi) != set(wi) else ", order changed"))
        wd = {c["id"]: c for c in wc}
        for c in gc:
            for k, v in c.items():
                if k == "keys" and (not LINUX or c["id"] in PLATFORM_KEY):
                    continue                    # the standard keys (Open, Quit, Undo) are the platform's
                if c["id"] in wd and wd[c["id"]].get(k) != v:
                    diffs.append(f"{state}: {c['id']}: {k}: {wd[c['id']].get(k)!r} -> {v!r}")
        if sorted(g.get("registry", []), key=lambda c: c["id"]) != sorted(w["registry"], key=lambda c: c["id"]):
            gi, wi = [c["id"] for c in g.get("registry", [])], [c["id"] for c in w["registry"]]
            # By id, not by order: Preferences > Input sorts the commands by category and
            # label, so the order they were registered in is nothing a curator can see.
            if sorted(gi) != sorted(wi):
                diffs.append(f"{state}: registry ids: gone {sorted(set(wi) - set(gi))}, new {sorted(set(gi) - set(wi))}")
            wr = {c["id"]: c for c in w["registry"]}
            for c in list(wr.values()) + g.get("registry", []):
                if not LINUX or c["id"] in PLATFORM_KEY:
                    c.pop("default", None)
            diffs += [f"{state}: registry {c['id']}: {wr[c['id']]} -> {c}" for c in g.get("registry", []) if c["id"] in wr and wr[c["id"]] != c]
        if sorted(g.get("editLocked", [])) != sorted(w["editLocked"]):
            diffs.append(f"{state}: edit lock: gone {sorted(set(w['editLocked']) - set(g.get('editLocked', [])))}, "
                         f"new {sorted(set(g.get('editLocked', [])) - set(w['editLocked']))}")
    return diffs


def main():
    klusters = R.skip_without_binary()
    got = dump(klusters)
    if "--update" in sys.argv:
        write_golden(got)
        print(f"test_command_table: wrote {GOLDEN} ({len(got['flat_open']['commands'])} commands)")
        return 0
    with open(GOLDEN, encoding="utf-8") as fh:
        want = json.load(fh)
    diffs = differences(got, want)
    n = len(want["flat_open"]["commands"])
    print(("  ok:   " if not diffs else "  FAIL: ") + f"the {n} commands, the registry and the edit lock's set are the snapshot's, in both states")
    for line in diffs[:40]:
        print("        " + line)
    if len(diffs) > 40:
        print(f"        ... and {len(diffs) - 40} more")
    print(f"test_command_table: {'PASS' if not diffs else 'FAIL'} ({len(diffs)} differences)")
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
