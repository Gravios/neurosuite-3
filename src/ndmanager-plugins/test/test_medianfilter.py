#!/usr/bin/env python3
"""process_medianfilter against an independent reference, and its GPU path
against its CPU path.

The filter (process_medianfilter.cpp):

    out[t] = x[t] - median(x[t - w .. t + w])   per channel, w = -w option,

with x = 0 outside the recording, the difference wrapped into int16, and a
trailing partial frame copied through unchanged.  The data are filtered in
chunks (-b bytes), so the checks run with one chunk, chunks of a few frames
and a chunk smaller than one frame: the output must not depend on the chunk
size.

When the build has a GPU path (its -h lists -c, "force CPU path"), the GPU
output must equal the CPU output (-c) byte for byte.

The binary is taken from $PROCESS_MEDIANFILTER_BIN, else PATH; when absent
the test says SKIPPED and exits 0.  Exits nonzero on failure.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

BIN = os.environ.get("PROCESS_MEDIANFILTER_BIN") or shutil.which("process_medianfilter")

fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}{'' if ok else ': ' + detail}")
    if not ok:
        fails += 1


def reference(x, w):
    """x: (frames, channels) int16 -> filtered int16, as documented above."""
    xi = x.astype(np.int64)
    pad = np.zeros((x.shape[0] + 2 * w, x.shape[1]), np.int64)
    pad[w:w + x.shape[0]] = xi
    med = np.median(sliding_window_view(pad, 2 * w + 1, axis=0), axis=-1).astype(np.int64)
    return ((xi - med + 32768) % 65536 - 32768).astype(np.int16)


def run(tmp, data_bytes, n, w, b, cpu):
    src = os.path.join(tmp, "in.dat")
    dst = os.path.join(tmp, "out.fil")
    with open(src, "wb") as fh:
        fh.write(data_bytes)
    cmd = [BIN, "-n", str(n), "-w", str(w), "-b", str(b)] + (["-c"] if cpu else []) + [src, dst]
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                           timeout=120)
    except subprocess.TimeoutExpired:
        return None, "timed out after 120 s"
    if r.returncode != 0:
        return None, r.stderr.strip()[-300:]
    with open(dst, "rb") as fh:
        return fh.read(), ""


def main():
    if not BIN or not os.path.exists(BIN):
        print(f"SKIPPED: process_medianfilter not found (set PROCESS_MEDIANFILTER_BIN)")
        return 0
    helptext = subprocess.run([BIN, "-h"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True).stdout
    has_gpu = "force CPU" in helptext
    print(f"  ....  {BIN}: GPU path {'built' if has_gpu else 'not built'}")

    rng = np.random.default_rng(7)
    cases = [
        # (name, frames, channels, w, extra tail bytes)
        ("4 ch, w 5", 5000, 4, 5, 0),
        ("1 ch, w 10", 3001, 1, 10, 0),
        ("7 ch, w 3, odd frame count", 2999, 7, 3, 0),
        ("32 ch, w 10", 4000, 32, 10, 0),
        ("w 0 (median of one sample)", 500, 3, 0, 0),
        ("recording shorter than the window", 6, 2, 10, 0),
        ("trailing partial frame", 1000, 4, 5, 3),
        ("int16 extremes (wrapping)", 2000, 3, 4, 0),
    ]
    with tempfile.TemporaryDirectory() as tmp:
        for name, frames, n, w, tail in cases:
            if "extremes" in name:
                x = rng.choice(np.array([-32768, -32767, -1, 0, 1, 32766, 32767], np.int16),
                               size=(frames, n))
            else:
                x = (rng.normal(0, 300, (frames, n)) + rng.integers(-50, 50, n)).astype(np.int16)
            tail_bytes = bytes(rng.integers(0, 256, tail, dtype=np.uint8))
            data = x.tobytes() + tail_bytes
            want = reference(x, w).tobytes() + tail_bytes
            frame = 2 * n
            outputs = {}
            for label, b in (("one chunk", 1 << 30), ("chunks of 37 frames", 37 * frame),
                             ("chunks of 1 frame", frame), ("chunk smaller than a frame", 1)):
                got, err = run(tmp, data, n, w, b, cpu=True)
                ok = got == want
                detail = err
                if got is not None and not ok:
                    g = np.frombuffer(got[:len(want) - tail], np.int16).reshape(-1, n) \
                        if len(got) == len(want) else None
                    if g is None:
                        detail = f"{len(got)} bytes, want {len(want)}"
                    else:
                        bad = np.nonzero((g != reference(x, w)).any(axis=1))[0]
                        detail = f"{len(bad)} frames differ, first at {bad[:5].tolist()}"
                check(f"{name}, {label}: CPU output equals the reference", ok, detail)
                outputs[label] = got
            if has_gpu:
                for label, b in (("one chunk", 1 << 30), ("chunks of 37 frames", 37 * frame)):
                    got, err = run(tmp, data, n, w, b, cpu=False)
                    check(f"{name}, {label}: GPU output equals the CPU output",
                          got is not None and got == outputs[label], err or "outputs differ")

    print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
