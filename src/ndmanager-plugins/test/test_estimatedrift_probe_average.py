"""
End-to-end test: build a 3-group session where 2 groups have real units
and 1 is noise-only, run process_estimatedrift, verify the .drift YAML
contains a probe-level 'drift' entry combining the 2 healthy groups.
"""
import os, sys, tempfile, subprocess, struct, yaml, math
import numpy as np

HERE_DIR = os.path.dirname(os.path.abspath(__file__))
HERE = os.path.normpath(os.path.join(HERE_DIR, ".."))
PLUGIN_DIR = os.path.join(HERE, "scripts")

PARAM_YAML = """\
acquisitionSystem:
  samplingRate: 20000
  nBits: 16
  nChannels: 24
  voltageRange: 20
  amplification: 1000
  offset: 0
spikeDetection:
  channelGroups:
    - channels: [0,1,2,3,4,5,6,7]
      nFeatures: 3
      nSamples: 32
      peakSampleIndex: 16
    - channels: [8,9,10,11,12,13,14,15]
      nFeatures: 3
      nSamples: 32
      peakSampleIndex: 16
    - channels: [16,17,18,19,20,21,22,23]
      nFeatures: 3
      nSamples: 32
      peakSampleIndex: 16
"""

def make_unit_spikes(unit_id, n_spikes, base_time, dt_samples):
    return np.array([base_time + i * dt_samples for i in range(n_spikes)],
                    dtype=np.int64)

def make_session(tmp):
    """Group 1: noise-only.  Groups 2 and 3: each have 4 units firing
    over a 5-minute recording at 20 kHz."""
    sr = 20000
    duration_min = 5
    n_samples = sr * 60 * duration_min  # 6,000,000
    base = os.path.join(tmp, "test")

    # Group 1: noise-only (clusters 0, 1)
    n1 = 500
    res1 = np.linspace(100, n_samples - 100, n1, dtype=np.int64)
    clu1 = np.tile(np.array([0, 1], dtype=np.int32), n1 // 2 + 1)[:n1]
    with open(f"{base}.res.1", "wb") as f: f.write(res1.tobytes())
    with open(f"{base}.clu.1", "wb") as f:
        f.write(struct.pack("<i", 2))
        f.write(clu1.astype(np.int32).tobytes())

    # Groups 2 and 3: 4 units each firing across the recording
    for grp in (2, 3):
        all_res = []
        all_clu = []
        for unit_id in (2, 3, 4, 5):
            # Each unit fires every ~200ms (≈4000 samples), uniformly spread
            n_per = 1500
            spike_times = np.linspace(2000 * unit_id, n_samples - 2000,
                                      n_per, dtype=np.int64)
            all_res.append(spike_times)
            all_clu.append(np.full(n_per, unit_id, dtype=np.int32))
        res = np.concatenate(all_res)
        clu = np.concatenate(all_clu)
        order = np.argsort(res)
        res, clu = res[order], clu[order]
        with open(f"{base}.res.{grp}", "wb") as f: f.write(res.tobytes())
        with open(f"{base}.clu.{grp}", "wb") as f:
            f.write(struct.pack("<i", 6))
            f.write(clu.astype(np.int32).tobytes())

    param_path = os.path.join(tmp, "params.yaml")
    with open(param_path, "w") as f: f.write(PARAM_YAML)
    return base, param_path

def run():
    with tempfile.TemporaryDirectory() as tmp:
        base, param = make_session(tmp)
        out_drift = os.path.join(tmp, "test.drift")
        cmd = [
            "python3", f"{PLUGIN_DIR}/process_estimatedrift.py",
            "--session",       base,
            "--param-file",    param,
            "--n-channels",    "24",
            "--output",        out_drift,
            "--n-groups",      "3",
            "--sampling-rate", "20000",
            "--n-workers",     "1",
            "--window-sec",    "60.0",
            "--min-units",     "3",
            "--min-spikes",    "10",
        ]
        r = subprocess.run(cmd, capture_output=True, text=True, cwd=tmp)

        if not os.path.exists(out_drift):
            print(f"  ✗ .drift file not created (rc={r.returncode})")
            print(f"     stderr: {r.stderr[:600]}")
            return 1

        with open(out_drift) as f:
            doc = yaml.safe_load(f)
        probes = doc["drift"]["probes"]
        if not probes:
            print(f"  ✗ no probes in .drift")
            print(f"     stderr: {r.stderr[:400]}")
            return 1

        # Find probe 0 (or whatever probe groups 2/3 land on)
        probe = probes[0]
        # Per-probe drift trace check
        avg = probe.get("drift")
        if not avg:
            print(f"  ✗ no probe.drift entry")
            print(f"     probe keys: {list(probe.keys())}")
            print(f"     stderr (last 800): {r.stderr[-800:]}")
            return 1

        fails = 0
        def check(label, cond):
            nonlocal fails
            mark = "✓" if cond else "✗"
            print(f"  {mark} {label}")
            if not cond: fails += 1

        check("probe.drift.method = 'weighted-mean-across-shanks'",
              avg.get("method") == "weighted-mean-across-shanks")
        check("probe.drift.nShanks = 2 (groups 2 and 3 only)",
              avg.get("nShanks") == 2)
        check("probe.drift.sourceGroups includes 2 and 3",
              set(avg.get("sourceGroups", [])) == {2, 3})
        check("probe.drift.sourceGroups does NOT include noise-only group 1",
              1 not in avg.get("sourceGroups", []))
        check("probe.drift.windows is non-empty",
              len(avg.get("windows", [])) > 0)

        # Stderr should mention the averaging
        check("stderr announces probe averaging",
              "averaged drift across" in r.stderr)
        return fails

if __name__ == "__main__":
    fails = run()
    print(f"\n{'PASS' if fails == 0 else 'FAIL'} — {fails} failures")
    sys.exit(fails)
