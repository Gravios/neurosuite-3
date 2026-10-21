#!/usr/bin/env python3
"""process_butterworth against scipy, across chunkings and precisions, and its
GPU path against its CPU path.

  1. CPU, double precision: equals scipy.signal.sosfiltfilt (same Butterworth
     design, odd-reflected edges) within 1 LSB, high-pass and band-pass;
  2. the output does not depend on the chunk (-b) or block (-B) size;
  3. single precision is within 1 LSB of double on almost every sample;
  4. when the build has a GPU path (-h lists -c), the GPU output in single
     precision is within 1 LSB of the CPU's single-precision output, and with
     -p double within 1 LSB of the CPU's double (an Apple GPU has no double,
     so there -p double runs on the CPU and is identical).

Rounding to int16 can turn the last bits of a different but equally valid
order of float operations (fused multiply-add) into 1 LSB, hence the
tolerance.  The binary is taken from $PROCESS_BUTTERWORTH_BIN, else PATH;
when absent the test says SKIPPED.  Exits nonzero on failure.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np

BIN = os.environ.get("PROCESS_BUTTERWORTH_BIN") or shutil.which("process_butterworth")
FS = 20000.0
fails = 0


def check(name, ok, detail=""):
    global fails
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}{'' if ok else ': ' + detail}")
    if not ok:
        fails += 1


def run(tmp, x, args):
    src = os.path.join(tmp, "in.dat")
    dst = os.path.join(tmp, "out.fil")
    x.tofile(src)
    r = subprocess.run([BIN, "-n", str(x.shape[1]), "-s", str(FS)] + args + [src, dst],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=300)
    if r.returncode != 0:
        return None, r.stderr.strip()[-300:]
    return np.fromfile(dst, np.int16).reshape(x.shape).astype(np.int64), ""


def compare(a, b):
    d = np.abs(a - b)
    return int(d.max()), float((d > 0).mean())


def main():
    if not BIN or not os.path.exists(BIN):
        print("SKIPPED: process_butterworth not found (set PROCESS_BUTTERWORTH_BIN)")
        return 0
    try:
        from scipy import signal
    except ImportError:
        print("SKIPPED: scipy is not installed")
        return 0
    helptext = subprocess.run([BIN, "-h"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True).stdout
    has_gpu = "force the CPU" in helptext
    cpu = ["-c"] if has_gpu else []
    print(f"  ....  {BIN}: GPU path {'built' if has_gpu else 'not built'}")

    rng = np.random.default_rng(3)
    T, n = 60000, 4
    x = (np.cumsum(rng.normal(0, 30, (T, n)), 0) + rng.normal(0, 200, (T, n))).clip(
        -30000, 30000).astype(np.int16)

    with tempfile.TemporaryDirectory() as tmp:
        for label, filt, sos in (
                ("high-pass 800 Hz", ["-l", "800"],
                 signal.butter(3, 800, "highpass", fs=FS, output="sos")),
                ("band-pass 600-6000 Hz", ["-l", "600", "-u", "6000"],
                 np.vstack([signal.butter(3, 600, "highpass", fs=FS, output="sos"),
                            signal.butter(3, 6000, "lowpass", fs=FS, output="sos")]))):
            want = np.round(signal.sosfiltfilt(sos, x.astype(float), axis=0,
                                               padtype="odd", padlen=3000)).clip(-32768, 32767)
            dbl, err = run(tmp, x, filt + cpu + ["-p", "double"])
            if dbl is None:
                check(f"{label}: CPU double runs", False, err)
                continue
            m, f = compare(dbl, want)
            check(f"{label}: CPU double equals scipy sosfiltfilt within 1 LSB", m <= 1,
                  f"max {m}, {f:.2%} of samples differ")

            chunked, err = run(tmp, x, filt + cpu + ["-p", "double", "-b", "40000", "-B", "1000"])
            check(f"{label}: chunk and block sizes do not change the output",
                  chunked is not None and np.array_equal(chunked, dbl), err or "outputs differ")

            sgl, err = run(tmp, x, filt + cpu + ["-p", "single"])
            m, f = compare(sgl, dbl) if sgl is not None else (99, 1.0)
            check(f"{label}: CPU single within 1 LSB of double (on < 0.5% of samples)",
                  m <= 1 and f < 0.005, err or f"max {m}, {f:.2%} of samples differ")

            if has_gpu:
                gpu, err = run(tmp, x, filt + ["-p", "single"])
                m, f = compare(gpu, sgl) if gpu is not None else (99, 1.0)
                print(f"  ....  {label}: GPU single vs CPU single: max {m} LSB, {f:.4%} of samples differ")
                check(f"{label}: GPU single within 1 LSB of CPU single (on < 0.5% of samples)",
                      m <= 1 and f < 0.005, err or f"max {m}, {f:.2%} of samples differ")
                gpud, err = run(tmp, x, filt + ["-p", "double"])
                m, f = compare(gpud, dbl) if gpud is not None else (99, 1.0)
                check(f"{label}: GPU -p double within 1 LSB of CPU double (on < 0.5% of samples)",
                      m <= 1 and f < 0.005, err or f"max {m}, {f:.2%} of samples differ")

    print("FAILED" if fails else "OK", f"({fails} failure{'s' * (fails != 1)})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
