#!/usr/bin/env python3
"""Round-trip test for ndm_plx2dat (Plexon .plx / .pl2 → .dat + YAML).

Builds synthetic PLX and PL2 files with an independent writer (this file
re-encodes both layouts from the format notes, so it is a second witness,
separate from the reader), converts them with process_plxconvert, merges the
metadata with process_plxyaml.py, and checks:

  1. the .dat holds exactly the written int16 samples, interleaved, with the
     recording pause zero-filled at its true position;
  2. the same recording in PLX and PL2 gives byte-identical .dat files;
  3. channel selection, empty-channel handling and pad-to-zero;
  4. metadata: header fields, per-channel scale, spike/event counts, device
     settings, gaps — in the session YAML and the sidecar;
  5. negative controls: truncated file, bad header record type, mixed rates,
     an off-grid block, and a non-Plexon file are all reported, not silently
     converted.

The binary is taken from $PROCESS_PLXCONVERT_BIN, else PATH; when absent (a
source-only checkout) the test says SKIPPED and exits 0.

Exits nonzero on failure (repo test convention).
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.normpath(os.path.join(HERE, "..", "scripts"))
MERGE = os.path.join(SCRIPTS, "process_plxyaml.py")
BIN = os.environ.get("PROCESS_PLXCONVERT_BIN") or shutil.which("process_plxconvert")

fails = 0


def check(name, cond):
    global fails
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}")
    if not cond:
        fails += 1


# ── synthetic recording ────────────────────────────────────────────────────
CLOCK = 40000
WB_RATE = 40000
FP_RATE = 1000
N_WB = 4                       # WB03 is enabled but never records (no data)
SEG = [(120, 6000), (9000, 4000)]   # (first tick, n samples) — a pause between
BLOCK = 997                    # odd block size: blocks do not align to anything
rng = np.random.default_rng(7)
WB = {i: [rng.integers(-30000, 30000, n, dtype=np.int16) for _, n in SEG]
      for i in range(N_WB) if i != 2}
FP = {i: [rng.integers(-2000, 2000, n // 40, dtype=np.int16) for _, n in SEG]
      for i in range(2)}
SPIKES = [(500 + 37 * k, k % 3, k % 2) for k in range(60)]   # (tick, channel, unit)
EVENTS = [(1000 + 400 * k, 0, 1) for k in range(10)] + [(2222, 1, 0x8001)]
WF = 8
THRESH = -120
UPC_V = 1.52587890625e-07      # volts per count (PL2)


def expected_dat(pad_to_zero=False, chans=range(N_WB)):
    t0 = 0 if pad_to_zero else SEG[0][0]
    total = SEG[-1][0] + SEG[-1][1] - t0
    out = np.zeros((total, len(chans)), np.int16)
    for j, i in enumerate(chans):
        if i not in WB:
            continue
        for (t, n), d in zip(SEG, WB[i]):
            out[t - t0:t - t0 + n, j] = d
    return out


def blocks(seg_data, rate):
    """[(tick, samples)] split into BLOCK-sized pieces."""
    tps = CLOCK // rate
    out = []
    for (t, _), d in zip(SEG, seg_data):
        for a in range(0, len(d), BLOCK):
            out.append((t + a * tps, d[a:a + BLOCK]))
    return out


# ── PLX writer (Plexon 1 layout, version 107) ──────────────────────────────
def _name(s, n=32):
    return s.encode().ljust(n, b"\0")


def write_plx(path, *, truncate=0, bad_rate=False):
    hdr = bytearray(7504)
    hdr[0:4] = b"PLEX"
    struct.pack_into("<i", hdr, 4, 107)
    hdr[8:8 + 11] = b"synthetic 1"
    struct.pack_into("<14i", hdr, 136, CLOCK, 3, 2, N_WB + 2, WF, 2,
                     2024, 9, 4, 15, 11, 11, 0, WB_RATE)
    struct.pack_into("<d", hdr, 192, 13000.0)
    struct.pack_into("<4B", hdr, 200, 1, 1, 16, 16)
    struct.pack_into("<3H", hdr, 204, 5000, 5000, 1000)
    hdr[210:210 + 8] = b"OmniPlex"
    out = bytearray(hdr)
    for c in range(3):
        b = bytearray(1020)
        b[0:32] = _name(f"SPK{c + 1:02d}")
        struct.pack_into("<9i", b, 64, c, WB_RATE, c, 0, 32, 0, THRESH, 1, 2)
        out += b
    for c in range(2):
        b = bytearray(296)
        b[0:32] = _name(["EVT01", "Strobed"][c])
        struct.pack_into("<i", b, 32, c)
        out += b
    for i in range(N_WB):
        b = bytearray(296)
        b[0:32] = _name(f"WB{i + 1:02d}")
        rate = 30000 if (bad_rate and i == 1) else WB_RATE
        struct.pack_into("<6i", b, 32, i, rate, 1, 1, 1000, -1)
        out += b
    for i in range(2):
        b = bytearray(296)
        b[0:32] = _name(f"FP{i + 1:02d}")
        struct.pack_into("<6i", b, 32, 100 + i, FP_RATE, 1, 1, 1000, -1)
        out += b

    def blk(btype, ts, ch, unit, data):
        n = len(data)
        return (struct.pack("<hHIhHhh", btype, ts >> 32, ts & 0xFFFFFFFF, ch, unit,
                            1 if n else 0, n) + np.asarray(data, "<i2").tobytes())

    recs = []
    for i, segs in WB.items():
        for t, d in blocks(segs, WB_RATE):
            recs.append((t, 0, blk(5, t, i, 0, d)))
    for i, segs in FP.items():
        for t, d in blocks(segs, FP_RATE):
            recs.append((t, 1, blk(5, t, 100 + i, 0, d)))
    for t, ch, u in SPIKES:
        recs.append((t, 2, blk(1, t, ch, u, np.arange(WF, dtype=np.int16))))
    for t, ch, v in EVENTS:
        recs.append((t, 3, blk(4, t, ch, v, [])))
    recs.sort(key=lambda r: (r[0], r[1]))
    for _, _, b in recs:
        out += b
    if truncate:
        out = out[:-truncate]
    Path(path).write_bytes(bytes(out))


# ── PL2 writer (layout of OpenReadout docs/formats/plexon.md) ──────────────
def _rec(kind, src, words, body_head, body):
    head = struct.pack("<BBH", kind, src, words & 0xFFFF) + body_head
    head = head.ljust(16, b"\0")
    pad = (-len(body)) % 16
    return head + body + b"\0" * pad


SETTINGS_XML = (b"<Settings>\r\n\t<m_highpassFreq>300</m_highpassFreq>\r\n"
                b"\t<m_inputSamplingRate>40000</m_inputSamplingRate>\r\n</Settings>\r\n"
                b"<Settings CompactMode=\"1\"><CommandBars Schema=\"34\"/></Settings>")


def write_pl2(path, *, bad_header=False, off_grid=False, truncate=0, bad_counts=False,
              reverse=False):
    n_spk, n_ana, n_dig = 3, N_WB + 2, 2
    hdr = bytearray(0x480)
    hdr[0] = 0xFE
    hdr[10:16] = b"PLEXON"
    hdr[0xE0:0xE0 + 11] = b"synthetic 2"
    hdr[0x1E0:0x1E0 + 8] = b"OmniPlex"
    hdr[0x220:0x220 + 6] = b"1.11.0"
    struct.pack_into("<9I", hdr, 0x230, 11, 11, 15, 4, 8, 124, 3, 247, 1)
    struct.pack_into("<d", hdr, 0x258, float(CLOCK))
    struct.pack_into("<2I", hdr, 0x260, n_spk + n_ana + n_dig, n_spk)
    struct.pack_into("<I", hdr, 0x26C, n_ana)
    struct.pack_into("<I", hdr, 0x274, n_dig)
    heads = bytearray()
    for c in range(n_spk):
        b = bytearray(2592)
        b[0], b[1] = (0xD4 if (bad_header and c == 1) else 0xD5), 6
        b[16:80] = _name(f"SPK{c + 1:02d}", 64)
        struct.pack_into("<4I", b, 0x50, 6, c + 1, 1, 1)
        b[0x60:0x65] = b"Volts"
        struct.pack_into("<2d", b, 0x70, float(WB_RATE), UPC_V)
        struct.pack_into("<IiI", b, 0x80, WF, THRESH, 2)
        units = np.bincount([u for _, ch, u in SPIKES if ch == c], minlength=2)
        for u, n in enumerate(units):
            struct.pack_into("<Q", b, 0xA0 + 8 * u, int(n) + (1 if bad_counts and c == 0 else 0))
        heads += b
    analog = [(f"WB{i + 1:02d}", 3, i + 1, WB_RATE) for i in range(N_WB)] + \
             [(f"FP{i + 1:02d}", 7, i + 1, FP_RATE) for i in range(2)]
    for name, src, ch, rate in analog:
        b = bytearray(512)
        b[0], b[1] = 0xD4, src
        b[16:80] = _name(name, 64)
        struct.pack_into("<4I", b, 0x50, src, ch, 1, 1)
        b[0x60:0x65] = b"Volts"
        struct.pack_into("<2d", b, 0x70, float(rate), UPC_V)
        b[0xD8:0xD8 + 19] = b"Plexon Mini DigiAmp"
        b[0x118:0x118 + 28] = b"Generic N-channel Electrodes"
        heads += b
    for c in range(n_dig):
        b = bytearray(368)
        b[0], b[1] = 0xD6, 9
        b[16:80] = _name(["EVT01", "Strobed"][c], 64)
        struct.pack_into("<4I", b, 0x50, 9, c + 1, 1, 1)
        heads += b
    headers_end = 0x480 + len(heads)
    settings = SETTINGS_XML + b"\0" * ((-len(SETTINGS_XML)) % 16)
    data_start = headers_end + len(settings)

    recs = []
    for i, segs in WB.items():
        for t, d in blocks(segs, WB_RATE):
            n = len(d)
            recs.append((t, 0, _rec(0x42, 3, n, struct.pack("<HHQ", i + 1, n, t),
                                    d.astype("<i2").tobytes())))
    for i, segs in FP.items():
        for t, d in blocks(segs, FP_RATE):
            if off_grid and i == 0 and t > SEG[0][0]:
                t += 13              # 13 ticks = 0.325 samples at 1 kHz: off the grid
            n = len(d)
            recs.append((t, 1, _rec(0x42, 7, n, struct.pack("<HHQ", i + 1, n, t),
                                    d.astype("<i2").tobytes())))
    for c in range(n_spk):
        sp = [(t, u) for t, ch, u in SPIKES if ch == c]
        if not sp:
            continue
        ts = np.array([t for t, _ in sp], "<u8")
        us = np.array([u for _, u in sp], "<u2")
        wf = np.tile(np.arange(WF, dtype="<i2"), len(sp))
        body = ts.tobytes() + us.tobytes() + wf.tobytes()
        recs.append((int(ts[0]), 2, _rec(0x31, 6, 0xBEEF, struct.pack("<HHI", c + 1, WF, len(sp)),
                                         body)))
    for c in range(n_dig):
        ev = [(t, v) for t, ch, v in EVENTS if ch == c]
        ts = np.array([t for t, _ in ev], "<u8")
        vs = np.array([v for _, v in ev], "<u2")
        recs.append((int(ts[0]), 3, _rec(0x5A, 9, 0, struct.pack("<HH", c + 1, len(ev)),
                                         ts.tobytes() + vs.tobytes())))
    recs.sort(key=lambda r: (r[0], r[1]), reverse=reverse)
    data = b"".join(r for _, _, r in recs)
    duration = SEG[-1][0] + SEG[-1][1]
    end = _rec(0x59, 0, 10, b"", struct.pack("<QQ", 0, duration) + b"\0" * 4)   # 48 bytes
    footer_start = data_start + len(data) + len(end)
    footer = _rec(0xDE, 0, 8, b"", b"\0" * 16)
    struct.pack_into("<6Q", hdr, 0x20, headers_end, data_start, footer_start,
                     footer_start, 0, duration)
    out = bytes(hdr) + bytes(heads) + settings + data + bytes(end) + footer
    if truncate:
        out = out[:footer_start - truncate]
    Path(path).write_bytes(out)


# ── harness ─────────────────────────────────────────────────────────────────
SESSION_YAML = """\
# session file — this comment must survive (ruamel) the update
parameters:
  version: '1.0'
generalInfo:
  date: ~
  description: ~
acquisitionSystem:
  nBits: 16
  nChannels: ~
  samplingRate: ~
  voltageRange: 20
  amplification: 1000
  offset: 0
programs:
  - name: ndm_plx2dat
    parameters:
      - {name: plexonFile, value: rec.pl2, status: Mandatory}
"""


def run(args):
    p = subprocess.run([BIN] + args, capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def convert(d, src, base="sess", extra=()):
    yml = Path(d, f"{base}.yaml")
    yml.write_text(SESSION_YAML)
    meta = Path(d, f"{base}.plexon.yaml")
    rc, out = run([*extra, "--chunk", "1500", "--meta", str(meta), str(src), str(Path(d, base))])
    if rc == 0:
        p = subprocess.run([sys.executable, "-I", MERGE, "--param-file", str(yml), "--meta", str(meta)],
                           capture_output=True, text=True)
        rc, out = p.returncode, out + p.stdout + p.stderr
    dat = Path(d, f"{base}.dat")
    arr = None
    if dat.exists():
        raw = np.fromfile(dat, "<i2")
        nch = yaml.safe_load(yml.read_text())["acquisitionSystem"]["nChannels"]
        arr = raw.reshape(-1, nch)
    return rc, out, arr, yml


def main():
    if not BIN or not os.path.isfile(BIN):
        print("SKIPPED: process_plxconvert not found (set PROCESS_PLXCONVERT_BIN)")
        return 0
    with tempfile.TemporaryDirectory() as d:
        plx, pl2 = Path(d, "rec.plx"), Path(d, "rec.pl2")
        write_plx(plx)
        write_pl2(pl2)

        print("PLX → .dat")
        rc, out, arr, yml = convert(d, plx, "a")
        check("exit 0", rc == 0)
        exp = expected_dat()
        check("samples, interleave and gap position", arr is not None and arr.shape == exp.shape
              and np.array_equal(arr, exp))
        doc = yaml.safe_load(yml.read_text())
        acq = doc["acquisitionSystem"]
        check("acquisitionSystem nChannels/samplingRate",
              acq["nChannels"] == N_WB and acq["samplingRate"] == WB_RATE)
        # PLX v107: µV/count = maxMV / (0.5·2^bits·gain·preamp) · 1e3
        uv = 5000 / (0.5 * 2 ** 16 * 1 * 1000) * 1e3
        check("bitResolution_uV from PLX gain formula",
              abs(acq["bitResolution_uV"] - uv) < 1e-9)
        check("amplification consistent with resolution",
              abs(20e6 / (65536 * acq["amplification"]) - uv) / uv < 0.01)
        sr = doc["sourceRecording"]
        check("PLX header version/clock/date", sr["header"]["version"] == 107
              and sr["header"]["timestampClock_hz"] == CLOCK
              and sr["header"]["recordedAt"] == "2024-09-04T15:11:11")
        check("generalInfo.date filled", str(doc["generalInfo"]["date"]) == "2024-09-04")
        ex = sr["export"]
        check("t0 and gap listed", ex["firstSampleTimestamp_ticks"] == SEG[0][0]
              and ex["nSegments"] == 2 and ex["gaps"][0]["afterSample"] == SEG[0][1]
              and ex["gaps"][0]["nSamples"] == SEG[1][0] - SEG[0][0] - SEG[0][1])
        check("empty channel kept and flagged", ex["channels"][2]["hasData"] is False
              and not arr[:, 2].any())
        spk = {c["name"]: c for c in sr["spikeChannels"]}
        check("spike counts per unit", spk["SPK01"]["nSpikes"] == 20
              and sum(spk["SPK02"]["unitCounts"].values()) == 20)
        check("event counts", {c["name"]: c["nEvents"] for c in sr["eventChannels"]}
              == {"EVT01": 10, "Strobed": 1})
        check("comment preserved (ruamel) or noted",
              "this comment must survive" in yml.read_text() or "ruamel" in out)
        check("programs block kept", doc["programs"][0]["name"] == "ndm_plx2dat")
        check("sidecar written", Path(d, "a.plexon.yaml").exists())

        print("PL2 → .dat")
        rc, out, arr2, yml2 = convert(d, pl2, "b")
        check("exit 0", rc == 0)
        check("PL2 .dat byte-identical to PLX .dat",
              Path(d, "a.dat").read_bytes() == Path(d, "b.dat").read_bytes())
        sr2 = yaml.safe_load(yml2.read_text())["sourceRecording"]
        check("PL2 application/version/date", sr2["header"]["application"] == "OmniPlex"
              and sr2["header"]["applicationVersion"] == "1.11.0"
              and sr2["header"]["recordedAt"] == "2024-09-04T15:11:11")
        check("PL2 scale from units_per_count",
              abs(sr2["export"]["channels"][0]["uvPerCount"] - UPC_V * 1e6) < 1e-9)
        check("PL2 device/electrode text", sr2["export"]["channels"][0]["device"]
              == "Plexon Mini DigiAmp")
        check("PL2 spike counts match header (no findings)", "findings" not in sr2)
        check("PL2 spike threshold in µV",
              abs(sr2["spikeChannels"][0]["threshold_uV"] - THRESH * UPC_V * 1e6) < 1e-3)
        check("PL2 device settings scraped, GUI layout ignored",
              sr2.get("deviceSettings") == [{"m_highpassFreq": 300, "m_inputSamplingRate": 40000}])
        check("PL2 event counts", {c["name"]: c["nEvents"] for c in sr2["eventChannels"]}
              == {"EVT01": 10, "Strobed": 1})

        rev = Path(d, "rev.pl2")
        write_pl2(rev, reverse=True)
        rc, _, _, _ = convert(d, rev, "r")
        check("records out of time order give the same .dat",
              rc == 0 and Path(d, "r.dat").read_bytes() == Path(d, "a.dat").read_bytes())

        print("options")
        rc, _, arr, _ = convert(d, pl2, "c", ["--pad-to-zero", "true"])
        check("pad-to-zero aligns sample 0 with tick 0",
              rc == 0 and np.array_equal(arr, expected_dat(pad_to_zero=True)))
        rc, _, arr, _ = convert(d, pl2, "e", ["--channels", "4,WB02"])
        check("channel spec (index + name, in given order)",
              rc == 0 and np.array_equal(arr, expected_dat(chans=[3, 1])))
        rc, _, arr, _ = convert(d, pl2, "f", ["--keep-empty", "false"])
        check("keep-empty false drops the empty channel",
              rc == 0 and np.array_equal(arr, expected_dat(chans=[0, 1, 3])))
        rc, _, arr, ymlf = convert(d, plx, "g", ["--stream", "FP"])
        exp_fp = np.zeros(((SEG[-1][0] + SEG[-1][1] - SEG[0][0]) // 40, 2), np.int16)
        for i in range(2):
            for (t, _), x in zip(SEG, FP[i]):
                a = (t - SEG[0][0]) // 40
                exp_fp[a:a + len(x), i] = x
        check("FP stream at 1 kHz", rc == 0 and np.array_equal(arr, exp_fp)
              and yaml.safe_load(ymlf.read_text())["acquisitionSystem"]["samplingRate"] == FP_RATE)
        rc, out = run(["--dry-run", str(pl2)])
        check("dry-run summary", rc == 0 and "WB" in out and "FP" in out)
        rc, out = run([str(pl2), str(Path(d, "a"))])
        check("refuses to overwrite an existing .dat", rc == 3)

        print("negative controls")
        bad = Path(d, "trunc.plx")
        write_plx(bad, truncate=500)
        rc, out, arr, ymlt = convert(d, bad, "t1")
        sr = yaml.safe_load(ymlt.read_text())["sourceRecording"]
        check("truncated PLX: converts whole blocks, reports truncation",
              rc == 0 and any("truncated" in f for f in sr.get("findings", [])))
        bad = Path(d, "trunc.pl2")
        write_pl2(bad, truncate=40)
        rc, out, arr, ymlt = convert(d, bad, "t2")
        sr = yaml.safe_load(ymlt.read_text())["sourceRecording"]
        check("truncated PL2: reports cut record / missing end record",
              rc == 0 and any("cut off" in f or "end-of-recording" in f
                              for f in sr.get("findings", [])))
        bad = Path(d, "badhdr.pl2")
        write_pl2(bad, bad_header=True)
        rc, out = run(["--dry-run", str(bad)])
        check("wrong channel-header record type is an error", rc != 0 and "0xd4" in out)
        bad = Path(d, "mixed.plx")
        write_plx(bad, bad_rate=True)
        rc, out, _, _ = convert(d, bad, "t3")
        check("mixed sampling rates in a stream refused", rc != 0 and "mix sampling rates" in out)
        bad = Path(d, "offgrid.pl2")
        write_pl2(bad, off_grid=True)
        rc, out, _, ymlo = convert(d, bad, "t4", ["--stream", "FP"])
        sr = yaml.safe_load(ymlo.read_text())["sourceRecording"]
        check("off-grid block timestamps reported",
              any("off the sample grid" in f for f in sr.get("findings", [])))
        bad = Path(d, "counts.pl2")
        write_pl2(bad, bad_counts=True)
        rc, out, _, ymlc = convert(d, bad, "t5")
        sr = yaml.safe_load(ymlc.read_text())["sourceRecording"]
        check("header vs record spike-count mismatch reported",
              any("SPK01: header counts" in f for f in sr.get("findings", [])))
        bad = Path(d, "noise.bin")
        bad.write_bytes(os.urandom(4096))
        rc, out = run(["--dry-run", str(bad)])
        check("non-Plexon file rejected", rc != 0 and "neither a PLX nor a PL2" in out)

    print(f"\n{'FAILED' if fails else 'OK'}: {fails} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
