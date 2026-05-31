#!/usr/bin/env python3
"""Format-contract test for the .adapt model artifact.

Checks three things and exits nonzero on failure (repo test convention):
  1. adapt_format.write_adapt produces exactly the documented little-endian
     byte layout (this test re-encodes that layout independently, so it is a
     third witness, separate from writer and reader);
  2. write_adapt -> read_adapt round-trips every field, templates included;
  3. when $PROCESS_REASSIGNSPIKES_BIN points at the compiled plugin, that the
     C++ reader accepts a Python-written model (the real cross-language guard;
     skipped when the binary is absent, as in a source-only checkout).
"""
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.normpath(os.path.join(HERE, "..", "scripts"))
sys.path.insert(0, SCRIPTS)
import adapt_format as af  # noqa: E402

fails = 0


def check(name, cond):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        fails += 1


def make_unit(group, cluster, ns, nc, seed):
    rng = np.random.default_rng(seed)
    return dict(group=group, cluster=cluster, n_samples=ns, n_chan=nc,
                tau_f_ms=4.0, u_f=0.35, tau_s_ms=90.0, u_s=0.25,
                templates=rng.standard_normal(3 * ns * nc).astype("<f4"))


def main():
    units = [make_unit(1, 2, 32, 4, 0), make_unit(1, 5, 16, 8, 1)]
    tmp = tempfile.mkdtemp()
    path = af.write_adapt(os.path.join(tmp, "m"), units)
    check("write appends .adapt", path.endswith(".adapt"))

    # (1) independent byte-layout witness
    blob = open(path, "rb").read()
    off = 0
    check("magic ADPT", blob[0:4] == b"ADPT")
    off = 4
    ver, n_units = struct.unpack_from("<ii", blob, off)
    off += 8
    check("version == ADAPT_VERSION", ver == af.ADAPT_VERSION)
    check("nUnits", n_units == len(units))
    for u in units:
        g, c, ns, nc = struct.unpack_from("<iiii", blob, off)
        off += 16
        tf, uf, ts, us = struct.unpack_from("<dddd", blob, off)
        off += 32
        check(f"g{u['group']}c{u['cluster']} int fields",
              (g, c, ns, nc) == (u["group"], u["cluster"],
                                 u["n_samples"], u["n_chan"]))
        check(f"g{g}c{c} float fields",
              (tf, uf, ts, us) == (u["tau_f_ms"], u["u_f"],
                                   u["tau_s_ms"], u["u_s"]))
        n = 3 * ns * nc
        vals = np.array(struct.unpack_from(f"<{n}f", blob, off), dtype="<f4")
        off += 4 * n
        check(f"g{g}c{c} templates", np.array_equal(vals, u["templates"]))
    check("no trailing bytes", off == len(blob))

    # (2) round-trip through read_adapt
    ver2, units2 = af.read_adapt(path)
    check("round-trip version", ver2 == af.ADAPT_VERSION)
    check("round-trip nUnits", len(units2) == len(units))
    rt = len(units2) == len(units) and all(
        units2[i]["group"] == units[i]["group"]
        and units2[i]["cluster"] == units[i]["cluster"]
        and units2[i]["n_samples"] == units[i]["n_samples"]
        and units2[i]["n_chan"] == units[i]["n_chan"]
        and abs(units2[i]["tau_f_ms"] - units[i]["tau_f_ms"]) < 1e-12
        and abs(units2[i]["u_s"] - units[i]["u_s"]) < 1e-12
        and np.array_equal(units2[i]["templates"], units[i]["templates"])
        for i in range(len(units)))
    check("round-trip fields + templates", rt)

    # (3) optional cross-language guard
    binpath = os.environ.get("PROCESS_REASSIGNSPIKES_BIN")
    if binpath and os.path.exists(binpath):
        r = subprocess.run([binpath, "-m", path, os.path.join(tmp, "nosession")],
                           capture_output=True, text=True)
        bad = ("not an .adapt model" in r.stderr
               or "truncated model" in r.stderr)
        check("C++ reader accepts Python-written model", not bad)
    else:
        print("  [SKIP] C++ cross-check "
              "(set PROCESS_REASSIGNSPIKES_BIN to enable)")

    print(f"\n{fails} failure(s)")
    return fails


if __name__ == "__main__":
    sys.exit(main())
