#!/usr/bin/env python3
"""A stored binding of hierarchy.dissolveParent moves to the key command's own id, once.

Until 2026-11-24 the child-view key command "Dissolve parent into children" (Ctrl+Shift+Down)
had the id of the Hierarchy menu's command and replaced it in the registry, so a binding stored
under hierarchy.dissolveParent was made for the key command.  It becomes
hierarchy.dissolveParentKey the first time; a binding stored after that (the flag
dissolveParentKeySplit) is the menu command's and stays.
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402

fails = 0
OLD, NEW = "hierarchy.dissolveParent", "hierarchy.dissolveParentKey"


def overrides(klusters, flag):
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        os.makedirs(os.path.join(d, "cfg", "neurosuite-3"))
        with open(os.path.join(d, "cfg", "neurosuite-3", "klusters.conf"), "w") as fh:
            fh.write("[inputBindings]\n" + ("dissolveParentKeySplit=true\n" if flag else "")
                     + "overrides\\size=2\n"
                     + f"overrides\\1\\id={OLD}\noverrides\\1\\chord=Ctrl+J\n"
                     + "overrides\\2\\id=action.groupClusters\noverrides\\2\\chord=Ctrl+G\n")
        return R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"palette": True}], d,
                     timeout=600).get("inputBindingOverrides")


def check(ok, what):
    global fails
    print(("  ok:   " if ok else "  FAIL: ") + what)
    fails += not ok


def main():
    klusters = R.skip_without_binary()
    got = overrides(klusters, False) or {}
    check(got.get(NEW) == "Ctrl+J" and OLD not in got, f"a binding stored before moves to the key command ({got})")
    check(got.get("action.groupClusters") == "Ctrl+G", "another binding is untouched")
    got = overrides(klusters, True) or {}
    check(got.get(OLD) == "Ctrl+J" and NEW not in got, f"a binding stored after is the menu command's and stays ({got})")
    print(f"test_dissolve_key_binding_migration: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
