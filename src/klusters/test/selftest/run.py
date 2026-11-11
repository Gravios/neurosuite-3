#!/usr/bin/env python3
"""Run the built Klusters headless over a self-test spec (klusters_selftest.cpp).

run(klusters, clu, steps, workdir, **prefs) writes the spec, runs the binary with
QT_QPA_PLATFORM=offscreen and NS3_KLUSTERS_SELFTEST, and returns the report dict
(code, results).  KLUSTERS_BIN names the binary; the tests skip (exit 77) without it.
"""
import json
import os
import subprocess
import sys


def binary():
    b = os.environ.get("KLUSTERS_BIN", "")
    return b if b and os.path.isfile(b) and os.access(b, os.X_OK) else ""


def run(klusters, clu, steps, workdir, timeout=1800, **prefs):
    spec = {"clu": clu, "report": os.path.join(workdir, "report.json"), "steps": steps}
    spec.update(prefs)
    spec_path = os.path.join(workdir, "spec.json")
    with open(spec_path, "w") as fh:
        json.dump(spec, fh)
    env = dict(os.environ)
    env["QT_PLUGIN_PATH"] = env.get("QT_PLUGIN_PATH", "")
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["NS3_KLUSTERS_SELFTEST"] = spec_path
    env["XDG_RUNTIME_DIR"] = workdir
    env.setdefault("HOME", workdir)                    # a private QSettings, never the user's
    env["XDG_CONFIG_HOME"] = os.path.join(workdir, "cfg")
    r = subprocess.run([klusters], cwd=workdir, env=env, capture_output=True, text=True, timeout=timeout)
    rep = {}
    if os.path.exists(spec["report"]):
        with open(spec["report"]) as fh:
            rep = json.load(fh)
    rep["rc"] = r.returncode
    rep["stderr"] = r.stderr[-6000:]
    return rep


def skip_without_binary():
    if not binary():
        print("SKIP: KLUSTERS_BIN is not set to a built klusters binary")
        sys.exit(77)
    return binary()
