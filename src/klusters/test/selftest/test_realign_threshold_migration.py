#!/usr/bin/env python3
"""The realign threshold saved while the default was 0.70 becomes the default of today, once.

The default is 0.10 since 2026-11-24.  Every Configuration::write() stores the threshold, so
settings written before hold a 0.70 nobody chose.  A stored 0.70 becomes 0.10 the first time;
any other stored value is kept, and so is a 0.70 stored after that (the flag
realignThresholdLoweredFromOldDefault).
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run as R                    # noqa: E402
import test_curation_log as T      # noqa: E402

fails = 0


def threshold(klusters, general):
    with tempfile.TemporaryDirectory() as d:
        base, _ = T.session(d, "f")
        if general is not None:
            os.makedirs(os.path.join(d, "cfg", "neurosuite-3"))
            with open(os.path.join(d, "cfg", "neurosuite-3", "klusters.conf"), "w") as fh:
                fh.write("[%General]\n" + general + "\n")
        return R.run(klusters, f"{base}.clu.{T.METHOD}.1", [{"palette": True}], d,
                     timeout=600).get("realignThreshold")


def main():
    global fails
    klusters = R.skip_without_binary()
    for general, want, what in (
            (None, 0.10, "no settings: the default"),
            ("realignThreshold=0.7", 0.10, "a stored 0.70 from the old default is lowered"),
            ("realignThreshold=0.5", 0.50, "a stored 0.50 was chosen: kept"),
            ("realignThreshold=0.7\nrealignThresholdLoweredFromOldDefault=true", 0.70,
             "a 0.70 stored after the change was chosen: kept")):
        got = threshold(klusters, general)
        ok = got is not None and abs(got - want) < 1e-6
        print(("  ok:   " if ok else "  FAIL: ") + f"{what} ({got})")
        fails += not ok
    print(f"test_realign_threshold_migration: {'PASS' if not fails else 'FAIL'} ({fails} failed)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
