"""
End-to-end test for the noise-only / fallback flow.
Builds a fake .drift YAML with a tombstoned source group and a healthy
sibling, runs process_applydrift via subprocess, and checks the output.

Run from anywhere:
    python3 src/ndmanager-plugins/test/test_applydrift_fallback.py
"""
import os, sys, tempfile, subprocess, yaml

PLUGIN_DIR = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))

def make_drift_yaml(path, scenarios):
    """scenarios = list of (probe_id, shank_index, group, kind) where kind ∈
    {'good', 'noise-only', 'absent'}."""
    probes = {}
    for pid, shk, g, kind in scenarios:
        if kind == 'absent': continue
        if kind == 'good':
            entry = {
                "shankIndex": shk, "spikeGroup": g,
                "windows": [
                    {"index": 0, "t_start": 0.0,  "t_end": 60.0, "drift": 0.0},
                    {"index": 1, "t_start": 60.0, "t_end": 120.0, "drift": 3.5},
                    {"index": 2, "t_start": 120.0,"t_end": 180.0, "drift": 7.1},
                ],
            }
        elif kind == 'noise-only':
            entry = {
                "shankIndex": shk, "spikeGroup": g,
                "skipReason": "noise-only-clu",
                "skipDetail": "all spikes assigned to clusters [0, 1]",
                "nSpikes": 12345, "nClusters": 2, "windows": [],
            }
        probes.setdefault(pid, {"probeId": pid, "label": "probe%d" % pid,
                                "shanks": []})
        probes[pid]["shanks"].append(entry)
    doc = {"drift": {
        "format": "1.0", "windowSec": 60.0, "minUnits": 3, "minSpikes": 10,
        "outlierThreshold": 50.0, "weightMode": "geometry",
        "probes": list(probes.values()),
    }}
    with open(path, "w") as f:
        yaml.dump(doc, f, sort_keys=False)

def run(args):
    cmd = ["python3", f"{PLUGIN_DIR}/process_applydrift.py"] + args
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr

def test(label, scenarios, source_group, expected_rc, expected_stderr_contains):
    with tempfile.TemporaryDirectory() as tmp:
        drift = os.path.join(tmp, "test.drift")
        make_drift_yaml(drift, scenarios)
        rc, out, err = run([
            "--session",       os.path.join(tmp, "test"),
            "--drift-file",    drift,
            "--source-group",  str(source_group),
            "--sampling-rate", "32552",
            "--thresh-um",     "5.0",
            "--min-chunk-sec", "30.0",
        ])
        ok_rc = (rc == expected_rc)
        ok_msg = expected_stderr_contains in err
        mark = "✓" if (ok_rc and ok_msg) else "✗"
        print(f"  {mark} {label}: rc={rc} (want {expected_rc}); "
              f"stderr-contains '{expected_stderr_contains[:40]}...': {ok_msg}")
        if not (ok_rc and ok_msg):
            print(f"     === stderr ===")
            for line in err.splitlines()[:20]:
                print(f"     {line}")
        # Check output file exists when success expected
        if expected_rc == 0:
            chunks_path = os.path.join(tmp, f"test.chunks.{source_group}")
            ok_file = os.path.exists(chunks_path)
            mark2 = "✓" if ok_file else "✗"
            print(f"     {mark2} output file {os.path.basename(chunks_path)} created: {ok_file}")
            if ok_file:
                with open(chunks_path) as f:
                    boundaries = [l for l in f if not l.startswith("#") and l.strip()]
                print(f"        ({len(boundaries)} boundary lines)")
        return ok_rc and ok_msg

print("=== Test 1: noise-only group 1 with healthy sibling group 2 on same probe ===")
test("fallback to sibling",
     [(0, 0, 1, 'noise-only'), (0, 1, 2, 'good'), (0, 2, 3, 'good')],
     source_group=1, expected_rc=0,
     expected_stderr_contains="falling back to group 2")

print("\n=== Test 2: requested group entirely absent, sibling on same probe ===")
test("absent → fallback",
     [(0, 0, 1, 'absent'), (0, 1, 2, 'good')],
     source_group=1, expected_rc=0,
     expected_stderr_contains="not present in .drift")

print("\n=== Test 3: ALL groups tombstoned — must error out cleanly ===")
test("everything dead",
     [(0, 0, 1, 'noise-only'), (0, 1, 2, 'noise-only'), (0, 2, 3, 'noise-only')],
     source_group=1, expected_rc=1,
     expected_stderr_contains="no usable source shank")

print("\n=== Test 4: requested group is healthy — fallback path NOT triggered ===")
test("healthy source",
     [(0, 0, 1, 'good'), (0, 1, 2, 'good')],
     source_group=1, expected_rc=0,
     expected_stderr_contains="3 drift windows")

print("\n=== Test 5: same-probe preference — group 5 noise-only on probe 0; ===")
print("===          sibling group 6 good on probe 1 (cross-probe fallback)  ===")
test("cross-probe fallback when same probe is dead",
     [(0, 0, 1, 'noise-only'), (0, 1, 5, 'noise-only'),
      (1, 0, 6, 'good')],
     source_group=5, expected_rc=0,
     expected_stderr_contains="different probe")

print("\n=== Test 6: closest-shank preference — group 3 noise-only between ===")
print("===          good groups 1 and 5 on the same probe                   ===")
import io
with tempfile.TemporaryDirectory() as tmp:
    drift = os.path.join(tmp, "test.drift")
    # Group 3 (shankIndex 2) is dead; siblings at shankIndex 0 (group 1) and 4 (group 5).
    make_drift_yaml(drift, [
        (0, 0, 1, 'good'),          # shankIdx 0, distance 2
        (0, 2, 3, 'noise-only'),    # the dead one
        (0, 4, 5, 'good'),          # shankIdx 4, distance 2
        (0, 1, 2, 'good'),          # shankIdx 1, distance 1 — should win
    ])
    rc, out, err = run([
        "--session", os.path.join(tmp, "test"),
        "--drift-file", drift,
        "--source-group", "3",
        "--sampling-rate", "32552",
        "--thresh-um", "5.0",
        "--min-chunk-sec", "30.0",
    ])
    ok = "falling back to group 2" in err
    mark = "✓" if ok else "✗"
    print(f"  {mark} closest shankIndex (group 2 at shankIdx=1) chosen over groups 1,5: {ok}")
    if not ok:
        print("     === stderr ===")
        for line in err.splitlines()[:15]: print(f"     {line}")
