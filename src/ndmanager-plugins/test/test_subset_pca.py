"""End-to-end test for the -l/-e subset-PCA flags on process_pca."""
import os, struct, subprocess, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
# ctest passes PROCESS_PCA_BIN; standalone, look in a build/ at the repo root
# (monorepo layout first, then a standalone ndmanager-plugins build).
BIN = os.environ.get("PROCESS_PCA_BIN") or next(
    (p for p in (os.path.join(_ROOT, "build", "ndmanager-plugins", "src", "process_pca", "process_pca"),
                 os.path.join(_ROOT, "build", "src", "process_pca", "process_pca"))
     if os.path.isfile(p)),
    os.path.join(_ROOT, "build", "ndmanager-plugins", "src", "process_pca", "process_pca"))

if not os.path.isfile(BIN):
    print(f"SKIPPED: process_pca not found at {BIN} (set PROCESS_PCA_BIN)")
    raise SystemExit(0)

def make_waveform(kind, nSamples=32, nChan=8, rng=None):
    t = np.arange(nSamples, dtype=np.float32)
    if kind == "interneuron":
        # Narrow biphasic
        shape = -300.0 * np.exp(-((t - 16) ** 2) / 3.0) \
                + 80.0 * np.exp(-((t - 20) ** 2) / 8.0)
        chan_weights = np.array([0.2, 0.5, 0.9, 1.0, 0.9, 0.5, 0.2, 0.1])
    else:
        # Broad monophasic
        shape = -200.0 * np.exp(-((t - 16) ** 2) / 25.0)
        chan_weights = np.array([0.1, 0.3, 0.7, 1.0, 0.7, 0.3, 0.1, 0.05])
    wf = np.outer(shape, chan_weights).astype(np.float32)
    if rng is not None:
        wf += rng.normal(0, 5.0, size=wf.shape).astype(np.float32)
    return np.clip(wf, -32000, 32000).astype(np.int16)

def make_session(tmp, n_in=1500, n_py=200, nC=8, nS=32):
    rng = np.random.default_rng(42)
    spk_path = os.path.join(tmp, "test.spk.7")
    clu_path = os.path.join(tmp, "test.clu.7")
    waveforms, cluIds = [], []
    for _ in range(n_in):
        waveforms.append(make_waveform("interneuron", nS, nC, rng))
        cluIds.append(5)
    for _ in range(n_py):
        waveforms.append(make_waveform("pyramidal", nS, nC, rng))
        cluIds.append(2)
    order = rng.permutation(n_in + n_py)
    waveforms = [waveforms[i] for i in order]
    cluIds = [cluIds[i] for i in order]
    arr = np.empty((len(waveforms), nS, nC), dtype=np.int16)
    for k, w in enumerate(waveforms): arr[k] = w
    with open(spk_path, "wb") as f: f.write(arr.tobytes())
    with open(clu_path, "wb") as f:
        f.write(struct.pack("<i", 6))  # int32 nClusters
        f.write(np.asarray(cluIds, dtype=np.int32).tobytes())
    return spk_path, clu_path, len(waveforms)

def run_pca(tmp, spk_path, fet_name, nC=8, nS=32, nComp=3, extra_args=None):
    """fet_name should match pattern '*.fet.N.tmp' for .pca path derivation
       to pick up correctly."""
    fet_path = os.path.join(tmp, fet_name)
    cmd = [BIN, "-f", fet_path, "-n", str(nC), "-w", str(nS),
           "-d", str(nComp), spk_path]
    if extra_args: cmd[-1:-1] = extra_args
    return subprocess.run(cmd, capture_output=True, text=True), fet_path

def parse_fet(path):
    with open(path, "rb") as f:
        nDim = struct.unpack("<i", f.read(4))[0]
        body = f.read()
    rows = len(body) // (nDim * 8)
    return np.frombuffer(body, dtype="<i8").reshape(rows, nDim), nDim

PCAE_MAGIC = 0x50434145  # "PCAE"

def parse_pca(path):
    """.pca format (PCAE, neurosuite/core/pca_projection.hpp): int32 header
       magic, version, nC, data2use, nComp, recShift, isCentered, and from
       version 2 method, nInputChannels; then per-channel data2use doubles
       (means), then per-channel data2use * nComp doubles (eigenvectors,
       col-major).  The pre-PCAE 5-int header (nC, data2use, nComp, isCent,
       recShift) is still read, for old files."""
    with open(path, "rb") as f:
        first = struct.unpack("<i", f.read(4))[0]
        if first == PCAE_MAGIC:
            version, nC, d2u, nComp, rsh, isCent = struct.unpack("<iiiiii", f.read(24))
            if version >= 2:
                f.read(8)   # method, nInputChannels
        else:
            d2u, nComp, isCent, rsh = struct.unpack("<iiii", f.read(16))
            nC = first
        means = np.frombuffer(f.read(nC * d2u * 8), dtype="<f8").reshape(nC, d2u)
        evecs_flat = np.frombuffer(f.read(nC * d2u * nComp * 8), dtype="<f8")
        evecs = evecs_flat.reshape(nC, nComp, d2u)  # col-major: shape (chan,comp,sample)
    return evecs, means, (nC, d2u, nComp, isCent, rsh)

fails = 0
def check(label, cond, detail=""):
    global fails
    mark = "✓" if cond else "✗"
    print(f"  {mark} {label}")
    if not cond:
        fails += 1
        if detail: print(f"     {detail}")

with tempfile.TemporaryDirectory() as tmp:
    print("=== Build synthetic session ===")
    spk, clu, nSpk = make_session(tmp)
    print(f"  built {nSpk} spikes (1500 cluster=5 + 200 cluster=2)")

    print("\n=== Test 1: baseline PCA (no exclusion) ===")
    r, fet_a = run_pca(tmp, spk, "test_a.fet.7.tmp")
    check("rc == 0", r.returncode == 0, r.stderr[:300])
    if r.returncode == 0:
        rows_a, nDim_a = parse_fet(fet_a)
        check(f"rows == {nSpk}", rows_a.shape[0] == nSpk, f"got {rows_a.shape[0]}")
        check("nDim == 8*3 = 24", nDim_a == 24, f"got {nDim_a}")
        pca_a = os.path.join(tmp, "test_a.pca.7")
        check(".pca.7 exists", os.path.exists(pca_a))
        if os.path.exists(pca_a):
            evecs_a, means_a, hdr_a = parse_pca(pca_a)

    print("\n=== Test 2: subset PCA (-e 5) ===")
    r, fet_b = run_pca(tmp, spk, "test_b.fet.7.tmp",
                       extra_args=["-l", clu, "-e", "5"])
    check("rc == 0", r.returncode == 0, r.stderr[:400])
    out = r.stdout + r.stderr
    check("announces exclusion", "Subset PCA: excluding clusters {5}" in out)
    check("reports fit fraction (200/1700)", "fitting on 200/1700" in out,
          out[:600])
    if r.returncode == 0:
        rows_b, nDim_b = parse_fet(fet_b)
        check(f"rows UNCHANGED ({nSpk})", rows_b.shape[0] == nSpk,
              f"got {rows_b.shape[0]}")
        check("nDim unchanged", nDim_b == nDim_a)
        pca_b = os.path.join(tmp, "test_b.pca.7")
        if os.path.exists(pca_b):
            evecs_b, means_b, hdr_b = parse_pca(pca_b)

    print("\n=== Test 3: eigenvectors differ between baseline and subset ===")
    if 'evecs_a' in dir() and 'evecs_b' in dir():
        # First PC of channel 3 (peak channel) — direction differs because
        # the basis is fit on different waveform shapes.
        pc1_a = evecs_a[3, 0]
        pc1_b = evecs_b[3, 0]
        pc1_a = pc1_a / (np.linalg.norm(pc1_a) + 1e-12)
        pc1_b = pc1_b / (np.linalg.norm(pc1_b) + 1e-12)
        cos = abs(float(np.dot(pc1_a, pc1_b)))
        check(f"PC1 differs (cos sim = {cos:.4f}, expect < 0.99)",
              cos < 0.99,
              "PCs are nearly identical — exclusion did not change basis")

    print("\n=== Test 4: excluding nonexistent cluster keeps full set ===")
    r, fet_d = run_pca(tmp, spk, "test_d.fet.7.tmp",
                       extra_args=["-l", clu, "-e", "999"])
    check("rc == 0", r.returncode == 0, r.stderr[:300])
    if r.returncode == 0:
        rows_d, _ = parse_fet(fet_d)
        check(f"rows == {nSpk}", rows_d.shape[0] == nSpk)
        pca_d = os.path.join(tmp, "test_d.pca.7")
        if os.path.exists(pca_d):
            evecs_d, _, _ = parse_pca(pca_d)
            pc1_a = evecs_a[3, 0] / (np.linalg.norm(evecs_a[3, 0]) + 1e-12)
            pc1_d = evecs_d[3, 0] / (np.linalg.norm(evecs_d[3, 0]) + 1e-12)
            cos = abs(float(np.dot(pc1_a, pc1_d)))
            check(f"basis matches baseline (cos sim = {cos:.4f}, expect > 0.999)",
                  cos > 0.999, f"got {cos}")

    print("\n=== Test 5: -e without -l errors ===")
    r, _ = run_pca(tmp, spk, "test_e.fet.7.tmp", extra_args=["-e", "5"])
    check("rc != 0", r.returncode != 0)
    check("explains the requirement",
          "must be given together" in (r.stdout + r.stderr))

    print("\n=== Test 6: -l without -e errors ===")
    r, _ = run_pca(tmp, spk, "test_f.fet.7.tmp", extra_args=["-l", clu])
    check("rc != 0", r.returncode != 0)

    print("\n=== Test 7: -e with non-integer errors ===")
    r, _ = run_pca(tmp, spk, "test_g.fet.7.tmp",
                   extra_args=["-l", clu, "-e", "5,foo"])
    check("rc != 0", r.returncode != 0)
    check("names the bad token", "foo" in r.stderr)

    print("\n=== Test 8: missing .clu file errors ===")
    r, _ = run_pca(tmp, spk, "test_h.fet.7.tmp",
                   extra_args=["-l", os.path.join(tmp, "nope.clu"), "-e", "5"])
    check("rc != 0", r.returncode != 0)
    check("mentions the missing file", "open" in r.stderr.lower())

    print("\n=== Test 9: the header records the channels the transform consumed ===")
    # The spatial orders that drop a channel upstream (1, 3, 4, 5:
    # custody::sdiffDropsLastChannel) leave a basis of nC channels made from
    # nC + 1 inputs.  --pca-method 7/8 (custom pairs / sets) once recorded nC.
    for m, n_in in ((0, 8), (4, 9), (5, 8), (6, 9), (7, 9), (8, 9), (9, 8)):
        r, _ = run_pca(tmp, spk, f"test_m{m}.fet.7.tmp", extra_args=["--pca-method", str(m)])
        pca_m = os.path.join(tmp, f"test_m{m}.pca.7")
        if r.returncode != 0 or not os.path.exists(pca_m):
            check(f"--pca-method {m} writes a basis", False, r.stderr[:300])
            continue
        with open(pca_m, "rb") as f:
            hdr = struct.unpack("<9i", f.read(36))
        check(f"--pca-method {m}: method {hdr[7]}, nInputChannels {hdr[8]} == {n_in}",
              hdr[7] == m and hdr[8] == n_in)

print(f"\n{'PASS' if fails == 0 else 'FAIL'} — {fails} failures")
import sys; sys.exit(fails)
