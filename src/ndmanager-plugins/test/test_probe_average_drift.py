"""
End-to-end tests for the probe-level averaged drift trace path.

  1. process_estimatedrift produces probes[i].drift when ≥2 shanks healthy.
  2. process_applydrift prefers probe-level over per-shank by default.
  3. --shank-specific opts back to per-shank windows.
  4. Probe-level fallback is used for tombstoned source groups when
     probes[i].drift exists, INSTEAD of the sibling-shank fallback.
  5. Weighted mean math is correct (heavier shank pulls the average).
  6. Drift-below-threshold ergonomic message appears for stable probe.
"""
import os, sys, tempfile, subprocess, struct, yaml, math
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PLUGIN_DIR = os.path.normpath(os.path.join(HERE, "..", "scripts"))

# ─── helpers ───────────────────────────────────────────────────────────────
def make_shank_entry(group, shankIdx, drift_per_window, n_units_in_ref=20,
                     window_sec=60.0):
    return {
        "spikeGroup":     group,
        "shankIndex":     shankIdx,
        "nUnitsInRef":    n_units_in_ref,
        "nUnitsQualified": n_units_in_ref,
        "refWindowIndex":  0,
        "windows": [
            {"t_start": w * window_sec,
             "t_end":  (w + 1) * window_sec,
             "drift_um": d} for w, d in enumerate(drift_per_window)
        ],
    }

def make_drift(path, probes_spec, window_sec=60.0):
    """probes_spec = list of (probeId, [(group, shankIdx, drift_seq, n_units), ...])"""
    probes = []
    for pid, shanks_spec in probes_spec:
        shanks = []
        for grp, shk, drift, n_units in shanks_spec:
            if drift is None:  # tombstone
                shanks.append({
                    "spikeGroup":  grp, "shankIndex": shk,
                    "skipReason":  "noise-only-clu",
                    "skipDetail":  "test fixture",
                    "nSpikes": 0, "nClusters": 2, "windows": [],
                })
            else:
                shanks.append(make_shank_entry(grp, shk, drift, n_units, window_sec))

        # Compute probe-level average inline — same formula as
        # average_drift_across_shanks
        healthy = [s for s in shanks if not s.get("skipReason") and s.get("windows")]
        probe_entry = {"probeId": pid, "label": f"probe{pid}"}
        if healthy:
            n_w = max(len(s["windows"]) for s in healthy)
            weights = [s.get("nUnitsInRef", 1) for s in healthy]
            avg_windows = []
            for w in range(n_w):
                drifts, ws = [], []
                for s, weight in zip(healthy, weights):
                    if w < len(s["windows"]):
                        d = s["windows"][w].get("drift_um")
                        if d is not None:
                            drifts.append(d); ws.append(weight)
                if drifts:
                    mean = sum(d * wt for d, wt in zip(drifts, ws)) / sum(ws)
                    sd = math.sqrt(sum((d - mean) ** 2 for d in drifts) / len(drifts)) \
                         if len(drifts) > 1 else 0.0
                    avg_windows.append({"t_start": w * window_sec,
                                        "t_end": (w + 1) * window_sec,
                                        "drift_um": round(mean, 3),
                                        "n_contributing_shanks": len(drifts),
                                        "stddev_um": round(sd, 3)})
                else:
                    avg_windows.append({"t_start": w * window_sec,
                                        "t_end": (w + 1) * window_sec,
                                        "drift_um": None,
                                        "n_contributing_shanks": 0,
                                        "stddev_um": None})
            probe_entry["drift"] = {
                "method": "weighted-mean-across-shanks",
                "weightField": "nUnitsInRef",
                "sourceGroups": [s["spikeGroup"] for s in healthy],
                "sourceWeights": weights,
                "nShanks": len(healthy),
                "windows": avg_windows,
            }
        probe_entry["shanks"] = shanks
        probes.append(probe_entry)

    doc = {"drift": {
        "format": "1.0", "windowSec": window_sec,
        "minUnits": 3, "minSpikes": 10, "outlierThreshold": 50.0,
        "weightMode": "geometry", "probes": probes,
    }}
    with open(path, "w") as f:
        yaml.dump(doc, f, sort_keys=False)

def run_applydrift(args):
    cmd = ["python3", f"{PLUGIN_DIR}/process_applydrift.py"] + args
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr

# ─── tests ─────────────────────────────────────────────────────────────────
fails = 0
def check(label, cond, detail=""):
    global fails
    mark = "✓" if cond else "✗"
    print(f"  {mark} {label}")
    if not cond:
        fails += 1
        if detail: print(f"     {detail}")

print("=== Test 1: probe-level average preferred over per-shank by default ===")
with tempfile.TemporaryDirectory() as tmp:
    drift = os.path.join(tmp, "test.drift")
    make_drift(drift, [(0, [
        (1, 0, [0.0, 1.0, 5.0],  20),
        (2, 1, [0.0, 1.5, 4.5],  30),
        (3, 2, [0.0, 1.2, 5.5],  25),
    ])])
    rc, out, err = run_applydrift([
        "--session", os.path.join(tmp, "test"),
        "--drift-file", drift,
        "--source-group", "1",
        "--sampling-rate", "20000",
        "--thresh-um", "3.0",
        "--min-chunk-sec", "1.0",
    ])
    check("rc == 0", rc == 0, err[:300])
    check("uses probe-level average",
          "probe-level averaged drift" in err,
          err[:400])
    check("lists source groups [1, 2, 3]",
          "[1, 2, 3]" in err)

print("\n=== Test 2: --shank-specific forces per-shank ===")
with tempfile.TemporaryDirectory() as tmp:
    drift = os.path.join(tmp, "test.drift")
    make_drift(drift, [(0, [
        (1, 0, [0.0, 1.0, 5.0],  20),
        (2, 1, [0.0, 1.5, 4.5],  30),
    ])])
    rc, out, err = run_applydrift([
        "--session", os.path.join(tmp, "test"),
        "--drift-file", drift, "--source-group", "1",
        "--sampling-rate", "20000", "--thresh-um", "3.0",
        "--shank-specific",
    ])
    check("rc == 0", rc == 0, err[:300])
    check("does NOT use probe-level average",
          "probe-level averaged drift" not in err)
    check("uses per-shank source",
          "Source: group 1" in err, err[:300])

print("\n=== Test 3: tombstoned source + probe-level average → use probe avg, ===")
print("===          NOT sibling-fallback                                       ===")
with tempfile.TemporaryDirectory() as tmp:
    drift = os.path.join(tmp, "test.drift")
    make_drift(drift, [(0, [
        (1, 0, None,             0),     # tombstoned
        (2, 1, [0.0, 1.5, 4.5], 30),
        (3, 2, [0.0, 1.2, 5.5], 25),
    ])])
    rc, out, err = run_applydrift([
        "--session", os.path.join(tmp, "test"),
        "--drift-file", drift, "--source-group", "1",
        "--sampling-rate", "20000", "--thresh-um", "3.0",
    ])
    check("rc == 0", rc == 0, err[:300])
    check("recognises tombstone",
          "skipped by ndm_estimatedrift" in err)
    check("uses probe-level average instead of sibling-fallback",
          "using probe-level averaged drift" in err
          and "falling back to group" not in err,
          err[:600])

print("\n=== Test 4: tombstoned source + --shank-specific → sibling fallback ===")
with tempfile.TemporaryDirectory() as tmp:
    drift = os.path.join(tmp, "test.drift")
    make_drift(drift, [(0, [
        (1, 0, None,             0),
        (2, 1, [0.0, 1.5, 4.5], 30),
        (3, 2, [0.0, 1.2, 5.5], 25),
    ])])
    rc, out, err = run_applydrift([
        "--session", os.path.join(tmp, "test"),
        "--drift-file", drift, "--source-group", "1",
        "--sampling-rate", "20000", "--thresh-um", "3.0",
        "--shank-specific",
    ])
    check("rc == 0", rc == 0, err[:300])
    check("uses sibling-fallback (NOT probe-average) under --shank-specific",
          "falling back to group" in err
          and "probe-level averaged drift" not in err)

print("\n=== Test 5: weighted mean math — heavy shank pulls the average ===")
with tempfile.TemporaryDirectory() as tmp:
    drift = os.path.join(tmp, "test.drift")
    # group 1 (20 units): drift = 0
    # group 2 (80 units): drift = 10
    # weighted mean = (0*20 + 10*80) / 100 = 8.0
    make_drift(drift, [(0, [
        (1, 0, [0.0, 0.0],   20),
        (2, 1, [0.0, 10.0],  80),
    ])])
    with open(drift) as f:
        doc = yaml.safe_load(f)
    avg = doc["drift"]["probes"][0]["drift"]
    w1_drift = avg["windows"][1]["drift_um"]
    expected = (0.0 * 20 + 10.0 * 80) / 100
    check(f"weighted mean = {expected:.1f}, got {w1_drift:.3f}",
          abs(w1_drift - expected) < 1e-6)
    check("stddev around weighted mean (sqrt(((0-8)²+(10-8)²)/2) = 5.83)",
          abs(avg["windows"][1]["stddev_um"] - 5.831) < 0.01,
          f"got {avg['windows'][1]['stddev_um']}")

print("\n=== Test 6: drift-below-threshold ergonomic message ===")
with tempfile.TemporaryDirectory() as tmp:
    drift = os.path.join(tmp, "test.drift")
    # All windows have drift in [0, 0.5], threshold 5 — won't chunk
    make_drift(drift, [(0, [
        (1, 0, [0.0, 0.3, 0.5, 0.2, 0.4],  20),
        (2, 1, [0.0, 0.4, 0.5, 0.3, 0.4],  30),
    ])])
    rc, out, err = run_applydrift([
        "--session", os.path.join(tmp, "test"),
        "--drift-file", drift, "--source-group", "1",
        "--sampling-rate", "20000", "--thresh-um", "5.0",
        "--min-chunk-sec", "30.0",
    ])
    check("rc == 0", rc == 0, err[:300])
    check("ergonomic 'drift below threshold' message present",
          "drift below threshold" in err
          and "chunked CEM is effectively" in err,
          err[:500])
    check("suggests --thresh-um override",
          "--thresh-um <smaller value>" in err)

print(f"\n{'PASS' if fails == 0 else 'FAIL'} — {fails} failures")
sys.exit(fails)
