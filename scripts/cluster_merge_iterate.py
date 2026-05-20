#!/usr/bin/env python3
"""
cluster_merge_iterate.py — iterative merge driver.

Runs `cluster_waveform_stats.py` + `cluster_merge_recommend.py` in a loop,
promoting the merged output to canonical .clu after each pass.  Stops
when fewer than --min-merges merges happen in one iteration (convergence)
or when --max-iter is hit.

Optional final pass with looser thresholds catches long-drift trajectories
that strict criteria miss.

DIAGNOSTICS captured per iteration:
  - Cluster count before/after
  - Tier counts (AUTO_DRIFT / AUTO_OVERSPLIT / REVIEW_PARTIAL / REVIEW)
  - Merge-mechanism breakdown (complete-link vs clique recovery)
  - Rejected-group count
  - Wall-clock time
  - Full stdout logs from both sub-scripts

OUTPUT LAYOUT:
  <output-dir>/
    iter_1/
      stats/            <- NPZ + summary.txt from waveform_stats
      clu_before        <- snapshot of .clu before this iteration
      merged.rec        <- snapshot of merged.rec produced this iteration
      recommendations.csv
      stats_stdout.txt
      merge_stdout.txt
    iter_2/
      ...
    iter_loose/         <- only if --final-loose-pass
      ...
    summary.csv         <- one row per iteration with all diagnostics
    summary.txt         <- human-readable summary table
  <session>.clu.<group>.preIterate  <- single backup of original .clu

USAGE:
  python3 cluster_merge_iterate.py SESSION GROUP \\
      [--y-spacing 20] [--max-iter 10] [--min-merges 2] \\
      [--final-loose-pass] [--output-dir merge_iterations_g6/]

CONVERGENCE: stop when fewer than --min-merges merges happen in one pass.
Default 2; raise to 5+ for faster termination at the cost of leaving a
few stragglers, or set to 1 to iterate until exactly zero.
"""

import argparse
import csv
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import numpy as np


SCRIPT_DIR = Path(__file__).resolve().parent
STATS_SCRIPT = SCRIPT_DIR / "cluster_waveform_stats.py"
MERGE_SCRIPT = SCRIPT_DIR / "cluster_merge_recommend.py"


def read_clu_count(clu_path: Path) -> int:
    """Distinct cluster IDs in a binary .clu file."""
    if not clu_path.is_file():
        return -1
    arr = np.fromfile(clu_path, dtype=np.int32, offset=4)
    return int(np.unique(arr).size)


def parse_tier_counts(csv_path: Path) -> dict:
    """Tier histogram from the merge recommendations CSV."""
    counts = {"AUTO_DRIFT": 0, "AUTO_OVERSPLIT": 0,
              "REVIEW_PARTIAL_OVERLAP": 0, "REVIEW": 0}
    if not csv_path.is_file():
        return counts
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        for row in reader:
            t = row.get("tier", "REVIEW")
            counts[t] = counts.get(t, 0) + 1
    return counts


def parse_merge_stdout(stdout: str) -> dict:
    """Extract diagnostic counts from merge_recommend stdout.

    Robust to minor formatting changes — uses regex on key phrases.
    """
    out = {
        "filtered_kept": None,
        "filtered_total": None,
        "n_cv_rejected": 0,
        "n_chunks": None,
        "n_coherence_demoted": 0,
        "complete_link_groups": 0,
        "complete_link_clusters": 0,
        "clique_recovery_groups": 0,
        "clique_recovery_clusters": 0,
        "rejected_groups": 0,
        "rejected_clusters": 0,
    }
    patterns = [
        (r"filtered:\s+(\d+)/(\d+) clusters",
         ("filtered_kept", "filtered_total")),
        (r"\((\d+) additional clusters rejected on CV",
         ("n_cv_rejected",)),
        (r"chunk assignment: (\d+) chunks detected",
         ("n_chunks",)),
        (r"demoted (\d+) outliers",
         ("n_coherence_demoted",)),
        (r"(\d+) groups merged via complete-link \((\d+) clusters\)",
         ("complete_link_groups", "complete_link_clusters")),
        (r"(\d+) groups merged via clique recovery \((\d+) clusters\)",
         ("clique_recovery_groups", "clique_recovery_clusters")),
        (r"REJECTED: (\d+) proposed groups containing (\d+) clusters",
         ("rejected_groups", "rejected_clusters")),
    ]
    for pat, keys in patterns:
        m = re.search(pat, stdout)
        if m:
            for i, k in enumerate(keys):
                out[k] = int(m.group(i + 1))
    return out


def run_subprocess(cmd: list, label: str, log_path: Path = None) -> str:
    """Run a subprocess, capture stdout, optionally tee to disk.  Raises
    on non-zero exit with the captured stderr appended to the message.
    """
    print(f"  [{label}] {' '.join(str(c) for c in cmd[2:])}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if log_path is not None:
        log_path.write_text(result.stdout + (
            "\n\n=== STDERR ===\n" + result.stderr if result.stderr else ""))
    if result.returncode != 0:
        sys.stderr.write(
            f"\n[{label}] FAILED (returncode {result.returncode})\n"
            f"stdout:\n{result.stdout}\n"
            f"stderr:\n{result.stderr}\n")
        raise RuntimeError(f"{label} failed")
    return result.stdout


def run_one_iteration(session: Path, group: int, iter_dir: Path,
                      merge_extra_args: list) -> dict:
    """Single iteration: stats → merge → diagnostics.

    Side effect: produces session.clu.<group>.merged.rec in the working
    dir (canonical location), plus copies into iter_dir for traceability.
    Does NOT promote .merged.rec to canonical .clu — caller decides.
    """
    stats_dir = iter_dir / "stats"
    stats_dir.mkdir(exist_ok=True, parents=True)

    # 1. Run waveform_stats
    t_stats = time.time()
    stats_cmd = [sys.executable, str(STATS_SCRIPT),
                  str(session), str(group),
                  "--output", str(stats_dir)]
    stats_stdout = run_subprocess(
        stats_cmd, "stats", iter_dir / "stats_stdout.txt")
    t_stats = time.time() - t_stats
    npz = stats_dir / f"{session.name}.cluster_waveforms.g{group}.npz"
    if not npz.is_file():
        raise RuntimeError(f"NPZ not produced at {npz}")

    # 2. Run merge_recommend
    t_merge = time.time()
    merge_cmd = ([sys.executable, str(MERGE_SCRIPT),
                   str(session), str(group), "--npz", str(npz)]
                  + merge_extra_args)
    merge_stdout = run_subprocess(
        merge_cmd, "merge", iter_dir / "merge_stdout.txt")
    t_merge = time.time() - t_merge

    # Copy outputs into iter_dir for traceability
    merged_canonical = Path(f"{session}.clu.{group}.merged.rec")
    csv_canonical = Path(f"{session}.merge_recommendations.g{group}.csv")
    merged_iter = iter_dir / "merged.rec"
    csv_iter = iter_dir / "recommendations.csv"
    shutil.copy2(merged_canonical, merged_iter)
    shutil.copy2(csv_canonical, csv_iter)

    # 3. Diagnostics
    n_before = read_clu_count(iter_dir / "clu_before")
    n_after = read_clu_count(merged_iter)
    tier_counts = parse_tier_counts(csv_iter)
    merge_diag = parse_merge_stdout(merge_stdout)

    return {
        "n_before": n_before,
        "n_after": n_after,
        "n_merged": n_before - n_after if n_before > 0 else 0,
        **tier_counts,
        **merge_diag,
        "time_stats_s": round(t_stats, 1),
        "time_merge_s": round(t_merge, 1),
        "merged_rec_path": str(merged_iter),
    }


def format_row(label: str, r: dict) -> str:
    """One-line summary of an iteration row."""
    return (f"{label:>8} {r['n_before']:>5} → {r['n_after']:>5} "
            f"({r['n_merged']:>+4} merged) | "
            f"AUTO_D={r['AUTO_DRIFT']:>4}  OS={r['AUTO_OVERSPLIT']:>3}  "
            f"PARTIAL={r['REVIEW_PARTIAL_OVERLAP']:>4}  REV={r['REVIEW']:>5} | "
            f"cl-link={r['complete_link_groups']:>3}  "
            f"clique={r['clique_recovery_groups']:>3}  "
            f"rej={r['rejected_groups']:>2} | "
            f"t={r['time_stats_s'] + r['time_merge_s']:>5.1f}s")


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("session", type=Path)
    ap.add_argument("group", type=int)
    ap.add_argument("--y-spacing", type=float, default=20.0)
    ap.add_argument("--max-iter", type=int, default=10,
                    help="Hard cap on iterations (default 10)")
    ap.add_argument("--min-merges", type=int, default=2,
                    help="Stop when fewer than this many merges happen "
                         "in a single iteration (default 2 — slightly "
                         "above noise floor)")
    ap.add_argument("--output-dir", type=Path, default=None,
                    help="Directory for per-iteration logs and stats "
                         "(default: merge_iterations_g<GROUP>/)")
    ap.add_argument("--final-loose-pass", action="store_true",
                    help="After convergence, run one more pass with "
                         "looser thresholds to catch long-drift "
                         "trajectories.  NOT auto-promoted; user "
                         "inspects and decides.")
    ap.add_argument("--no-promote", action="store_true",
                    help="Don't overwrite canonical .clu between "
                         "iterations — only report what WOULD happen. "
                         "Useful for previewing convergence behaviour.")
    ap.add_argument("--max-cluster-cv", type=float, default=0.3,
                    help="Pass-through to merge_recommend")
    ap.add_argument("--min-spikes", type=int, default=50,
                    help="Pass-through to merge_recommend")
    args = ap.parse_args()

    canonical_clu = Path(f"{args.session}.clu.{args.group}")
    if not canonical_clu.is_file():
        sys.stderr.write(f"ERROR: {canonical_clu} not found\n")
        sys.exit(1)

    output_dir = (args.output_dir
                  or Path(f"merge_iterations_g{args.group}"))
    output_dir.mkdir(exist_ok=True, parents=True)

    # Single global backup before iteration begins
    global_backup = Path(f"{args.session}.clu.{args.group}.preIterate")
    if not global_backup.exists() and not args.no_promote:
        shutil.copy2(canonical_clu, global_backup)
        print(f"Global backup: {canonical_clu} → {global_backup}")

    # Args passed through to merge_recommend on every iteration
    merge_extra_strict = [
        "--y-spacing", str(args.y_spacing),
        "--max-cluster-cv", str(args.max_cluster_cv),
        "--min-spikes", str(args.min_spikes),
    ]

    n_initial = read_clu_count(canonical_clu)
    print(f"Starting cluster count: {n_initial}")
    print(f"Loop: stats → merge_recommend → promote → repeat")
    print(f"Convergence: fewer than {args.min_merges} merges in one pass")
    print()

    summary_rows = []
    for iteration in range(1, args.max_iter + 1):
        print(f"─── Iteration {iteration} " + "─" * 50)
        iter_dir = output_dir / f"iter_{iteration}"
        iter_dir.mkdir(exist_ok=True)
        # Snapshot the .clu we're about to feed into stats+merge
        shutil.copy2(canonical_clu, iter_dir / "clu_before")

        try:
            row = run_one_iteration(args.session, args.group, iter_dir,
                                     merge_extra_strict)
        except RuntimeError as e:
            print(f"\nABORTING: {e}")
            break
        row["iter"] = iteration
        summary_rows.append(row)
        print(format_row(f"iter {iteration}", row))

        if row["n_merged"] < args.min_merges:
            print(f"\nConverged at iteration {iteration} "
                  f"({row['n_merged']} merges < threshold {args.min_merges})")
            break

        # Promote merged.rec → canonical .clu for next iteration
        if not args.no_promote:
            shutil.copy2(row["merged_rec_path"], canonical_clu)
        else:
            print("  (--no-promote: canonical .clu unchanged)")
            break
    else:
        print(f"\nReached --max-iter {args.max_iter} without converging")

    # Optional loose pass after strict convergence
    if args.final_loose_pass:
        print(f"\n─── Loose final pass " + "─" * 50)
        loose_dir = output_dir / "iter_loose"
        loose_dir.mkdir(exist_ok=True)
        shutil.copy2(canonical_clu, loose_dir / "clu_before")
        merge_extra_loose = merge_extra_strict + [
            "--cosw-thresh", "0.92",
            "--cosfp-thresh", "0.92",
            "--xcorr-thresh", "0.95",
            "--drift-max-um", "60",
            "--alpha-spread-max", "8",
            "--std-ratio-max", "4",
        ]
        try:
            row = run_one_iteration(args.session, args.group, loose_dir,
                                     merge_extra_loose)
        except RuntimeError as e:
            print(f"\nLoose pass FAILED: {e}")
        else:
            row["iter"] = "loose"
            summary_rows.append(row)
            print(format_row("loose", row))
            print(f"\nLoose-pass merged.rec at: {row['merged_rec_path']}")
            print(f"NOT auto-promoted.  To accept:")
            print(f"  cp {row['merged_rec_path']} {canonical_clu}")

    # Summary outputs
    summary_csv = output_dir / "summary.csv"
    if summary_rows:
        # Field order: iter first, then standard columns
        all_fields = ["iter", "n_before", "n_after", "n_merged",
                       "AUTO_DRIFT", "AUTO_OVERSPLIT",
                       "REVIEW_PARTIAL_OVERLAP", "REVIEW",
                       "complete_link_groups", "complete_link_clusters",
                       "clique_recovery_groups", "clique_recovery_clusters",
                       "rejected_groups", "rejected_clusters",
                       "filtered_kept", "filtered_total",
                       "n_cv_rejected", "n_chunks", "n_coherence_demoted",
                       "time_stats_s", "time_merge_s"]
        with open(summary_csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=all_fields, extrasaction="ignore")
            w.writeheader()
            w.writerows(summary_rows)
        print(f"\nWrote {summary_csv}")

    # Final pretty summary
    print(f"\n{'═' * 80}")
    print(f"FINAL SUMMARY")
    print(f"{'═' * 80}")
    n_final = summary_rows[-1]["n_after"] if summary_rows else n_initial
    total_merged = n_initial - n_final
    print(f"  starting clusters:  {n_initial}")
    print(f"  final clusters:     {n_final}")
    print(f"  total merged:       {total_merged}  "
          f"({total_merged/max(n_initial,1)*100:.1f}%)")
    print(f"  iterations:         {len(summary_rows)}")
    print()
    print(f"  Per-iteration:")
    for r in summary_rows:
        print(f"  {format_row(str(r['iter']), r)}")
    # Also write a human-readable summary.txt
    summary_txt = output_dir / "summary.txt"
    with open(summary_txt, "w") as f:
        f.write(f"Iterative merge summary for {args.session.name} group {args.group}\n")
        f.write(f"  starting clusters: {n_initial}\n")
        f.write(f"  final clusters:    {n_final}\n")
        f.write(f"  total merged:      {total_merged}\n")
        f.write(f"  iterations:        {len(summary_rows)}\n\n")
        for r in summary_rows:
            f.write(format_row(str(r["iter"]), r) + "\n")
    print(f"\n  Diagnostics: {output_dir}/")
    print(f"  Original .clu backed up to: {global_backup}")


if __name__ == "__main__":
    main()
