#!/usr/bin/env python3
"""Canonical reader/writer for the .adapt model artifact.

This module is the single Python-side definition of the .adapt binary format
produced by adaptmodel.py and consumed by the C++ process_reassignspikes
plugin.  The C++ side mirrors it in
    src/process_reassignspikes/process_reassignspikes.{h,cpp}
-- ADAPT_VERSION here and ADAPT_VERSION there MUST be bumped together, and any
field-layout change has to land on both sides in the same commit.

Layout (little-endian):

    char[4]  "ADPT"
    int32    version
    int32    nUnits
    per unit:
        int32    group, cluster, nSamples, nChan
        float64  tau_f_ms, u_f, tau_s_ms, u_s
        float32  templates[3 * nSamples * nChan]   (C0, C1, C2 row-major)

A unit is represented as a plain dict with keys: group, cluster, n_samples,
n_chan (ints); tau_f_ms, u_f, tau_s_ms, u_s (floats); templates (a flat array
of 3*n_samples*n_chan float32, blocks C0, C1, C2 contiguous, row-major).
"""
import numpy as np

ADAPT_MAGIC = b"ADPT"
ADAPT_VERSION = 1

_I4 = "<i4"
_F8 = "<f8"
_F4 = "<f4"

# Per-unit fixed-part field order; the byte layout is defined by these.
UNIT_INT_KEYS = ("group", "cluster", "n_samples", "n_chan")
UNIT_FLOAT_KEYS = ("tau_f_ms", "u_f", "tau_s_ms", "u_s")


def write_adapt(path, units):
    """Write `units` (iterable of unit dicts) to `path`.

    `.adapt` is appended to `path` if not already present.  Returns the path
    actually written.  Raises ValueError if a unit's template count does not
    match 3 * n_samples * n_chan.
    """
    units = list(units)
    path = str(path)
    if not path.endswith(".adapt"):
        path = path + ".adapt"
    with open(path, "wb") as fh:
        fh.write(ADAPT_MAGIC)
        np.array([ADAPT_VERSION, len(units)], dtype=_I4).tofile(fh)
        for m in units:
            ns, nc = int(m["n_samples"]), int(m["n_chan"])
            np.array([m[k] for k in UNIT_INT_KEYS], dtype=_I4).tofile(fh)
            np.array([m[k] for k in UNIT_FLOAT_KEYS], dtype=_F8).tofile(fh)
            t = np.asarray(m["templates"], dtype=_F4)
            expected = 3 * ns * nc
            if t.size != expected:
                raise ValueError(
                    f"unit g{m['group']}c{m['cluster']}: templates has "
                    f"{t.size} floats, expected 3*{ns}*{nc} = {expected}")
            t.ravel(order="C").tofile(fh)
    return path


def read_adapt(path):
    """Inverse of write_adapt.  Returns (version, [unit dicts]).

    Mirrors the C++ readModel() read sequence exactly so the two stay testable
    against each other.  Raises ValueError on a bad magic or a truncated unit.
    """
    with open(path, "rb") as fh:
        magic = fh.read(4)
        if magic != ADAPT_MAGIC:
            raise ValueError(f"{path}: not an .adapt model (magic={magic!r})")
        head = np.fromfile(fh, dtype=_I4, count=2)
        if head.size != 2:
            raise ValueError(f"{path}: truncated header")
        version, n_units = int(head[0]), int(head[1])
        units = []
        for u in range(n_units):
            ints = np.fromfile(fh, dtype=_I4, count=4)
            flts = np.fromfile(fh, dtype=_F8, count=4)
            if ints.size != 4 or flts.size != 4:
                raise ValueError(f"{path}: truncated model at unit {u}")
            g, c, ns, nc = (int(x) for x in ints)
            tf, uf, ts, us = (float(x) for x in flts)
            templ = np.fromfile(fh, dtype=_F4, count=3 * ns * nc)
            if templ.size != 3 * ns * nc:
                raise ValueError(f"{path}: truncated model at unit {u}")
            units.append(dict(group=g, cluster=c, n_samples=ns, n_chan=nc,
                              tau_f_ms=tf, u_f=uf, tau_s_ms=ts, u_s=us,
                              templates=templ))
    return version, units
