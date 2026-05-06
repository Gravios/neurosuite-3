"""
Mini integration test for the tombstone path in process_estimatedrift.py.

Creates a minimal session with one shank whose .clu contains only clusters
0 and 1 (the noise + artefact bins under neurosuite convention) and verifies
that ndm_estimatedrift writes a tombstoned entry with skipReason set,
instead of failing or silently dropping the group from .drift.

Run from anywhere:
    python3 src/ndmanager-plugins/test/test_estimatedrift_tombstone.py
"""
import os, sys, tempfile, subprocess, struct, yaml
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_DIR = os.path.normpath(os.path.join(HERE, "..", "scripts"))
SCRIPT = os.path.join(PLUGIN_DIR, "process_estimatedrift.py")

# Minimal session yaml — process_estimatedrift only reads probe geometry
# and acquisitionSystem fields from this file.  A handful of mandatory
# blocks is enough to satisfy the parser without bringing in actual probes.
PARAM_YAML = """\
acquisitionSystem:
  samplingRate: 20000
  nBits: 16
  nChannels: 8
  voltageRange: 20
  amplification: 1000
  offset: 0
spikeDetection:
  channelGroups:
    - channels: [0, 1, 2, 3, 4, 5, 6, 7]
      nFeatures: 3
      nSamples: 32
      peakSampleIndex: 16
"""


def make_minimal_session(tmp, group_idx, clu_ids, n_spikes=1000, sr=20000):
    """Write .res, .clu, and .yaml for a single group into *tmp*."""
    base = os.path.join(tmp, "test")
    # .res: int64 LE timestamps, no header
    res = np.linspace(100, 100 + n_spikes * 100, n_spikes, dtype=np.int64)
    with open(f"{base}.res.{group_idx}", "wb") as f:
        f.write(res.tobytes())
    # .clu: int32 nClusters header + int32 IDs
    clu = np.array(clu_ids, dtype=np.int32)
    if len(clu) < n_spikes:
        clu = np.tile(clu, (n_spikes // len(clu)) + 1)[:n_spikes]
    with open(f"{base}.clu.{group_idx}", "wb") as f:
        f.write(struct.pack("<i", int(clu.max()) + 1))
        f.write(clu.astype(np.int32).tobytes())
    # Param file
    param_path = os.path.join(tmp, "params.yaml")
    with open(param_path, "w") as f:
        f.write(PARAM_YAML)
    return base, param_path


def test_noise_only_tombstone():
    print("=== Test: noise-only .clu produces tombstone entry ===")
    with tempfile.TemporaryDirectory() as tmp:
        base, param = make_minimal_session(tmp, group_idx=1, clu_ids=[0, 1])
        out_drift = os.path.join(tmp, "test.drift")
        cmd = [
            "python3", SCRIPT,
            "--session", base,
            "--param-file", param,
            "--n-channels", "8",
            "--output", out_drift,
            "--n-groups", "1",
            "--source-group", "1",
            "--sampling-rate", "20000",
            "--n-workers", "1",
        ]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)
        if r.returncode != 0:
            print(f"  ✗ ndm_estimatedrift failed (rc={r.returncode})")
            print(f"     stderr: {r.stderr[:500]}")
            return False
        if not os.path.exists(out_drift):
            print(f"  ✗ no .drift file produced")
            return False

        with open(out_drift) as f:
            doc = yaml.safe_load(f)

        shanks = doc["drift"]["probes"][0]["shanks"]
        if not shanks:
            print(f"  ✗ no shank entries written — group 1 silently dropped")
            return False
        s = shanks[0]
        checks = [
            ("skipReason='noise-only-clu'",
             s.get("skipReason") == "noise-only-clu"),
            ("spikeGroup=1",
             s.get("spikeGroup") == 1),
            ("skipDetail mentions noise",
             "noise / artefact" in s.get("skipDetail", "")),
            ("stderr explains skip",
             "noise-only .clu" in r.stderr),
        ]
        ok = True
        for label, val in checks:
            mark = "✓" if val else "✗"
            print(f"  {mark} {label}")
            ok = ok and val
        return ok


if __name__ == "__main__":
    sys.exit(0 if test_noise_only_tombstone() else 1)
