#!/usr/bin/env python3
"""Standalone check for scripts/adaptmodel.py.

Runs the fitter's built-in synthetic-recovery selftest (which plants known
adaptation constants, fits them back, and asserts recovery + the trust gates)
plus a small CLI smoke test.  Exits nonzero on failure, matching the other
test_*.py in this directory.  Override the script path with $ADAPTMODEL.
"""
import os
import subprocess
import sys

HERE   = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.environ.get(
    "ADAPTMODEL",
    os.path.normpath(os.path.join(HERE, "..", "scripts", "adaptmodel.py")))

fails = 0


def check(name, cond):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        fails += 1


def main():
    if not os.path.exists(SCRIPT):
        print(f"  [FAIL] adaptmodel.py not found at {SCRIPT}")
        return 1

    # 1) synthetic-recovery selftest must pass
    r = subprocess.run([sys.executable, SCRIPT, "selftest"],
                       capture_output=True, text=True)
    check("selftest exits 0", r.returncode == 0)
    check("selftest reports PASS", "SELFTEST PASS" in r.stdout)

    # 2) CLI smoke: both subcommands are exposed
    h = subprocess.run([sys.executable, SCRIPT, "--help"],
                       capture_output=True, text=True)
    check("--help exits 0", h.returncode == 0)
    check("exposes diagnose + selftest",
          "diagnose" in h.stdout and "selftest" in h.stdout)

    print(f"\n{fails} failure(s)")
    return fails


if __name__ == "__main__":
    sys.exit(main())
