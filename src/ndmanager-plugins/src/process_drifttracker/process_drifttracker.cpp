/***************************************************************************
 * process_drifttracker.cpp
 *
 * Drift-aware template matching for individual cluster groups.  Computes
 * a time-varying mean waveform per cluster (minimum-variance estimator
 * within a sliding window), smooths each cluster's trajectory through
 * waveform space with a Tikhonov ridge so templates evolve continuously,
 * then merges cluster pairs whose smoothed drift trajectories converge
 * under cyclic-shift subtraction.
 *
 * Conceptual map to KiloKlustaKwik's Phase 6b (mean-subtraction merge):
 *   - KKExp Phase 6b uses ONE global mean per cluster (no time variation)
 *     so a unit that drifts in feature space across the session looks
 *     "different from itself" and gets fragmented.
 *   - This plugin computes per-window means and tracks them through
 *     time, so two clusters that genuinely follow the SAME drifting
 *     unit can be matched window-by-window and merged even when their
 *     global means differ.
 *
 * Algorithm
 * =========
 *
 *   Phase A — Time-windowed mean templates
 *   --------------------------------------
 *   Slide a window of WindowMinutes (W) length across the session with
 *   stride W - OverlapMinutes (O).  For each cluster c and window w,
 *   accumulate the spike waveforms whose timestamps fall in window w
 *   and divide by the count.  The arithmetic mean is the MLE under
 *   Gaussian per-sample noise — the minimum-variance unbiased estimator
 *   of cluster c's mean waveform in that window.
 *
 *   Windows with fewer than MinWindowSpikes for a cluster are left
 *   undefined (alpha[c, w] = 0) so the temporal smoother can interpolate
 *   them from neighbouring windows.
 *
 *   Phase B — Tikhonov temporal smoothing
 *   -------------------------------------
 *   For each cluster and each waveform dimension d (channel × sample),
 *   solve the regularised least-squares system:
 *
 *       smoothed[c, w, d] = argmin sum_w  α[c,w] · (smoothed - mean[c,w,d])²
 *                                       + λ · (smoothed_w - smoothed_{w-1})²
 *
 *   The Euler-Lagrange equations form a symmetric positive-definite
 *   tridiagonal system, solved in O(n_win) per dimension via the Thomas
 *   algorithm.  Tridiagonal layout:
 *
 *       (α[w] + 2λ - λ·boundary) · x[w] - λ · x[w-1] - λ · x[w+1]
 *           = α[w] · mean[c,w,d]
 *
 *   Boundary windows (w=0, w=n_win-1) drop one of the cross-terms.  The
 *   λ knob balances trust in the per-window mean (low λ — templates
 *   follow noise) vs continuity (high λ — templates flatten to a
 *   constant trajectory).  Default 1.0 is a sensible starting point at
 *   typical neural SNR.
 *
 *   For undefined windows (α=0), the diagonal becomes 2λ (or λ at
 *   boundaries), so the smoother purely interpolates from neighbours.
 *   This naturally handles clusters that go silent for stretches of
 *   time — the trajectory glides through the gap rather than dropping
 *   to zero.
 *
 *   Phase C — Drift-aware subtraction merge
 *   ---------------------------------------
 *   For each pair of clusters (c1, c2), compute the drift-aware mean
 *   residual:
 *
 *       D(c1, c2) = mean over windows w where both are alive of
 *                   min over τ ∈ [-K, K] of
 *                       ||shift_τ(smoothed[c1, w]) - smoothed[c2, w]||²
 *                       / max(||smoothed[c1, w]||², ||smoothed[c2, w]||²)
 *
 *   shift_τ is cyclic (wraps the time axis) — safe for typical spike
 *   windows where the peak sits near the centre and the edges are
 *   baseline.  τ is searched independently per window, so a pair of
 *   clusters that drift in different directions across time still gets
 *   the best per-window alignment.  Windows where either cluster is
 *   undefined are excluded.
 *
 *   If at least MinWindowsOverlap windows have both clusters alive AND
 *   the average D is below MergeThresh, the pair is queued for merge.
 *
 *   Phase D — Apply merges, write outputs
 *   -------------------------------------
 *   Resolve transitive merges via union-find (smallest ID wins —
 *   palette stability, matches Phase 6b convention).  Apply to a
 *   working copy of the .clu array and write:
 *     - <session>.clu.<grp>.drift   : merged cluster IDs
 *     - <session>.drift.<grp>.yaml  : human-readable report:
 *         windows, per-cluster member counts, pairwise D values,
 *         applied merges, untouched clusters.
 *
 * Inputs
 * ======
 *   <session>.res.<grp>  binary little-endian int64 timestamps, no header
 *   <session>.clu.<grp>  int32 header (nClusters) + int32 cluster IDs
 *   <session>.spk.<grp>  int16 raw spike waveforms, sample-major
 *                        channel-minor: row layout is
 *                        spike_p[s * nChan + ch] for s in [0, nSamp).
 *                        Pass --spk-suffix spkD for the stderiv variant.
 *
 *   Geometry passed via CLI (taken from .xml/YAML by the bash wrapper):
 *     --n-chan N           number of channels in this group
 *     --n-samp N           samples per spike (typical 32, 42)
 *     --sampling-rate N    Hz (typical 20000)
 *
 * Limitations
 * ===========
 *   - Only handles a single group per invocation; the bash wrapper loops
 *     over groups.
 *   - The temporal smoother is Tikhonov-ridge, not a full Kalman.  For
 *     non-stationary drift (sudden jumps) a state-space model with
 *     adaptive process noise would do better; punted to a follow-up.
 *   - Boundary-aware template refinement (down-weighting spikes near
 *     cluster decision boundaries within a window) is NOT implemented
 *     in v1.  The hard-assigned mean is the MLE under the existing .clu;
 *     it's good enough when the upstream sort is well-separated and
 *     fragile when clusters touch in feature space.  Future work.
 *
 * copyright (C) 2026 neurosuite-3 contributors  GPL-3.0-or-later
 ***************************************************************************/

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <unistd.h>     // patch55: fsync, getpid
#include <sys/stat.h>   // patch55: fsync error reporting
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace {

// ─── CLI / parameters ───────────────────────────────────────────────────────
struct Params {
    std::string sessionBase;        // e.g. /path/to/session
    int         grp           = 0;
    int         nChan         = 0;
    int         nSamp         = 0;
    int         peakSample    = 0;  // unused in v1 except for reporting
    double      samplingRate  = 0;

    // Algorithm knobs
    double      windowMin     = 5.0;   // sliding-window length in minutes
    double      overlapMin    = 2.5;   // overlap between consecutive windows
    int         minWinSpikes  = 30;    // require this many spikes to define a window's template
    double      lambda        = 1.0;   // Tikhonov ridge for temporal smoothing
    double      mergeThresh   = 0.05;  // average D below which pair merges
    int         maxShift      = 3;     // cyclic-shift search half-width
    int         minWinOverlap = 3;     // require this many co-alive windows to even compare a pair

    // Per-channel time-varying gain fit (patch47).  Captures drift physics
    // that pure time-shift can't: a unit moving relative to the array
    // redistributes amplitude across channels.  For each pair (A, B), if
    // gainFit != 0, fit a sequence of per-channel scalar gains a_w ∈ ℝ^nChan
    // such that  a_w ⊙ shift_τ(A_w) ≈ B_w  for all co-alive windows w,
    // with Tikhonov smoothness on a_w − a_{w-1} and a mild prior pulling
    // a_w toward 1 (so we don't degenerate to zeroing channels to "explain
    // away" mismatch).  Channels decouple — nChan independent tridiagonal
    // systems, each solved by Thomas in O(nWin).
    int         gainFit       = 0;     // 0 = off (default); 1 = on
    double      gainSmoothLambda = 10.0; // μ in Σ(a_w − a_{w-1})²; higher = smoother
    double      gainUnityPrior   = 0.1;  // λ_unity in Σ(a_w − 1)²; higher = stays near 1

    // Channel-position-aware drift (patch49).  Each cluster gets a 2D
    // centre-of-mass trajectory through (x, y) channel-position space,
    // computed from amplitude-weighted channel positions on the smoothed
    // per-window mean waveforms.  Trajectories are Tikhonov-smoothed in
    // 2D position space (much cheaper than full waveform space — 2 dims
    // per window vs nChan × nSamp), then pairs are filtered by mean COM
    // distance before merging.
    //
    // If --channel-positions is given, expects a text file with one row
    // per channel: "x y" (μm).  Order matches channels within the group.
    // Otherwise channel INDEX is used as 1D position (x=0, y=index) —
    // works for tetrodes/octrodes where channels are arranged linearly
    // on a shank; the COM is then in units of "channels" rather than μm.
    //
    // positionThresh > 0 enforces an AND-filter on candidate pairs: in
    // addition to passing the waveform residual D < mergeThresh check,
    // a pair's mean per-window COM distance must be ≤ positionThresh.
    // positionThresh == 0 (default) computes the diagnostic but doesn't
    // filter — purely informational.
    std::string channelPositionsFile;   // empty = use channel index
    std::string positionUnits;          // patch51: diagnostic label; empty = auto
    double      positionThresh = 0.0;   // 0 = off; otherwise μm (or channels)
    double      positionSmoothLambda = 5.0;  // Tikhonov ridge on COM(t) smoothness

    // ── Shared population-level drift (patch57) ────────────────────────────
    //
    // When clusters are temporally fragmented — many sortings produce hundreds
    // of clusters whose lifetimes don't span the full session — pairwise
    // per-cluster COM comparison can't directly compare clusters that lived
    // in disjoint windows.  This option estimates a SHARED probe-wide drift
    // signal D(w) from the collective motion of all clusters whose lifetimes
    // happen to overlap each window pair, then subtracts D(w) from every
    // cluster's COM trajectory.  Disjoint clusters that represent the same
    // drifting unit collapse to the same absolute position after correction.
    //
    // Method: for each pair of windows (w_i, w_j) that share ≥ K clusters,
    // compute the mean displacement of those shared clusters' COMs.  This
    // gives a sparse pairwise observation graph.  Solve for D(w) by
    // least squares on the graph Laplacian, anchored at D(0) = 0 (Tikhonov
    // ridge stabilises disconnected windows).  This is the
    // tetrode/octrode analogue of DREDge (Windolf et al., Nature Methods
    // 2025) reduced to a rigid 2D translation per time window.
    bool   sharedDrift              = false;     // --shared-drift  (off by default)
    int    sharedDriftMinClusters   = 5;         // K — min shared clusters per window edge
    double sharedDriftPrior         = 0.01;      // Tikhonov ridge on D(w) magnitude

    // ── Iterative refinement (patch58 — Huber-IRLS outlier downweighting) ──
    //
    // Single-pass D estimate uses uniform per-cluster weights within each
    // edge.  Outlier clusters (units whose own motion isn't part of the
    // shared population drift — dying cells, mis-merged FPs that mimic
    // drift, etc.) bias the mean-based per-edge displacement.  Iterating
    // with per-cluster Huber weighting identifies these and downweights
    // them, sharpening D recovery.
    //
    // - sharedDriftIter == 1 (default): single pass — patch57 behaviour
    // - sharedDriftIter  > 1          : IRLS, max N iterations; early exit
    //                                    when max |ΔD(w)| < sharedDriftTol
    //   * each iteration recomputes per-cluster Huber weights from the
    //     residuals of the previous D estimate
    //   * weight formula: w_c = 1                       if |r_c| ≤ k·MAD(r)
    //                     w_c = k·MAD(r) / |r_c|        else
    //   * k = sharedDriftHuberK (default 1.345 — Huber's 95%-efficiency
    //     choice at normal distribution)
    //
    // For most fragmented-cluster sessions, 2–3 iterations suffice;
    // further iterations refine D by <1% of the change in the first pass.
    int    sharedDriftIter          = 1;         // 1 = single-pass; >1 enables IRLS
    double sharedDriftHuberK        = 1.345;     // MAD-multiples breakpoint
    double sharedDriftTol           = 1e-3;      // convergence in D-units (channels/μm)

    // ── Runaway-chain guardrail (patch59) ──────────────────────────────────
    //
    // Union-find merges are transitive: pair (A,B) + pair (B,C) → group {A,B,C}
    // regardless of whether (A,C) was ever a candidate.  If the user sets a
    // loose threshold or one pathological cluster gets matched repeatedly,
    // the chain can absorb dozens of unrelated units into a single
    // mega-cluster.  This cap rejects any candidate that would grow either
    // root's merged group past maxMergeChain original clusters.  Set to 0
    // to disable.  Default 8 protects against catastrophic runaway while
    // still allowing realistic drift chains.
    int    maxMergeChain            = 8;

    // ── Cross-temporal pairing for fragmented clusters (patch60) ───────────
    //
    // Phase C only compares pairs that share ≥ minWinOverlap co-alive
    // windows.  In sessions with strong drift fragmentation — clusters
    // that live for a few minutes, then disappear as drift moves them off
    // the spike-detector threshold, with a "successor" cluster appearing
    // at the new position — most successor pairs DON'T overlap in time at
    // all and Phase C cannot see them.
    //
    // Phase C2 (this option) handles those: for each pair NOT already a
    // Phase C candidate AND with no sufficient co-alive overlap, identify
    // the boundary between their alive intervals (earlier cluster's last
    // K_a alive windows × later cluster's first K_b alive windows),
    // compute the same cyclic-shift residual + meanCOM averaged over the
    // K_a · K_b pairings, and apply the same --merge-thresh and
    // --position-thresh.  Operates on the same drift-corrected COMs
    // produced by Phase B.6, so disjoint clusters representing the same
    // unit at different probe positions collapse onto each other.
    //
    // - crossTemporal: opt-in (off by default).  Phase C alone is correct
    //   for well-curated sessions; Phase C2 is for the fragmented case.
    // - crossTemporalMaxGap: max number of windows between earlier
    //   cluster's last alive and later cluster's first alive.  Skip pairs
    //   separated by more than this — they're not plausibly the same unit.
    // - crossTemporalNeighborhood: width K of the boundary window range
    //   on each side.  Larger = more comparisons = more robust mean D but
    //   slower.  Default 3 covers typical drift transition periods.
    bool   crossTemporal              = false;
    int    crossTemporalMaxGap        = 4;
    int    crossTemporalNeighborhood  = 3;

    // File-name suffixes (override for stderiv variant: spkD)
    std::string spkSuffix     = "spk";
    std::string outCluTag     = "drift";

    bool        dryRun        = false;
    int         verbose       = 1;
};

void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s <session-base> <grp> [options]\n"
        "\n"
        "Inputs read:\n"
        "  <session-base>.yaml                  session config (auto-detected;\n"
        "                                        supplies geometry + sampling rate)\n"
        "  <session-base>.res.<grp>             int64 spike timestamps\n"
        "  <session-base>.clu.<grp>             int32 header + int32 cluster IDs\n"
        "  <session-base>.<spk-suffix>.<grp>    int16 spike waveforms\n"
        "\n"
        "Outputs written:\n"
        "  <session-base>.clu.<grp>.<out-clu-tag>   merged cluster IDs\n"
        "  <session-base>.drift.<grp>.yaml          diagnostic report\n"
        "\n"
        "Geometry (auto-filled from <session-base>.yaml; CLI overrides):\n"
        "  --n-chan N            channels in this group\n"
        "  --n-samp N            samples per spike\n"
        "  --sampling-rate Hz    (typical 20000)\n"
        "  --peak-sample N       peak position within waveform (0-based)\n"
        "\n"
        "Options:\n"
        "  --window-min     M    sliding window length in minutes (default 5.0)\n"
        "  --overlap-min    M    window overlap in minutes (default 2.5)\n"
        "  --min-win-spikes N    spikes needed to define a window template (default 30)\n"
        "  --lambda         L    Tikhonov ridge weight (default 1.0)\n"
        "  --merge-thresh   D    average residual below which to merge (default 0.05)\n"
        "  --max-shift      K    cyclic-shift search half-width (default 3)\n"
        "  --min-win-overlap N   min co-alive windows to compare a pair (default 3)\n"
        "  --max-merge-chain N   hard cap on transitive-merge chain size — reject\n"
        "                         any candidate that would grow either root's\n"
        "                         merged group past N original clusters (default 8,\n"
        "                         0 = disabled).  Guards against runaway union-find\n"
        "                         chains caused by loose --merge-thresh or one\n"
        "                         pathological cluster matching many partners.\n"
        "\n"
        "Cross-temporal pairing for fragmented clusters (patch60 — off by default):\n"
        "  --cross-temporal       enable Phase C2: pair clusters whose alive\n"
        "                         intervals DON'T overlap, by comparing the\n"
        "                         earlier cluster's last K windows to the later\n"
        "                         cluster's first K windows.  Essential when no\n"
        "                         single cluster spans the whole session (drift\n"
        "                         fragmentation regime).  Honours --merge-thresh\n"
        "                         and --position-thresh on the drift-corrected COMs.\n"
        "  --cross-temporal-max-gap N   max window gap between earlier cluster's\n"
        "                         last alive and later cluster's first alive\n"
        "                         (default 4).  Pairs more than N windows apart\n"
        "                         are skipped — not plausibly the same unit.\n"
        "  --cross-temporal-neighborhood K  width K of the boundary window range\n"
        "                         compared on each side (default 3).\n"
        "\n"
        "Per-channel drift model (patch47 — off by default):\n"
        "  --gain-fit       0|1  fit per-channel time-varying gain on each pair\n"
        "                         before computing the merge residual (default 0).\n"
        "                         Captures drift that's not just time-shift —\n"
        "                         e.g., a unit's amplitude redistributing across\n"
        "                         channels as it moves relative to the array.\n"
        "  --gain-smooth-lambda L  Tikhonov ridge on gain time-smoothness\n"
        "                         μ · Σ(a_w − a_{w-1})² (default 10.0)\n"
        "  --gain-unity-prior   L  prior pulling gain toward 1.0\n"
        "                         λ · Σ(a_w − 1)² (default 0.1)\n"
        "\n"
        "Position-aware drift (patch49):\n"
        "  --channel-positions FILE  text file, one row per channel: \"x y\"\n"
        "                         in μm.  Order matches channels within the\n"
        "                         spike group.  Optional — if absent, channel\n"
        "                         INDEX is used as 1D y-position (x=0).\n"
        "  --position-thresh    D   AND-filter on candidate pairs: mean per-\n"
        "                         window COM distance must be ≤ D (default 0\n"
        "                         = compute diagnostic but don't filter).\n"
        "                         Units match --channel-positions (μm if a\n"
        "                         positions file is loaded, channels otherwise).\n"
        "  --position-smooth-lambda L  Tikhonov ridge on COM(t) smoothness\n"
        "                         (default 5.0)\n"
        "  --position-units    STR  diagnostic units label (default auto:\n"
        "                         'μm' when --channel-positions is given,\n"
        "                         'channels' otherwise).  Useful when the\n"
        "                         positions file uses mm, normalized, or\n"
        "                         non-μm coordinates.\n"
        "\n"
        "Shared population drift (patch57 — for fragmented-cluster sessions):\n"
        "  --shared-drift          estimate a shared probe-wide drift signal\n"
        "                          D(w) from the collective motion of all\n"
        "                          clusters with pairwise window overlap,\n"
        "                          and subtract it from every cluster's COM\n"
        "                          trajectory.  Enables comparison of clusters\n"
        "                          whose lifetimes don't overlap directly —\n"
        "                          essential for sessions with hundreds of\n"
        "                          temporally-fragmented clusters where no\n"
        "                          subset spans the full session.\n"
        "  --shared-drift-min-clusters N  min shared clusters required to\n"
        "                          define a window-pair edge in the drift\n"
        "                          inference graph (default 5).  Lower values\n"
        "                          give a denser graph but noisier per-edge\n"
        "                          displacement estimates.\n"
        "  --shared-drift-prior L  Tikhonov ridge on D(w) magnitude — keeps\n"
        "                          the system well-conditioned for sparse\n"
        "                          graphs and disconnected components\n"
        "                          (default 0.01)\n"
        "  --shared-drift-iter N   number of IRLS refinement iterations\n"
        "                          (default 1 = single-pass).  Values > 1\n"
        "                          enable Huber-weighted iterative\n"
        "                          refinement: each iteration downweights\n"
        "                          clusters whose own motion deviates from\n"
        "                          the shared population drift (outliers).\n"
        "                          2–3 iterations typically converge.\n"
        "  --shared-drift-huber-k K  Huber breakpoint in MAD-multiples\n"
        "                          (default 1.345 — Huber's 95%% efficiency).\n"
        "                          Smaller = more aggressive outlier\n"
        "                          rejection; larger = closer to L2 mean.\n"
        "  --shared-drift-tol T    Early-exit tolerance: stop iterating\n"
        "                          when max |D_new(w) − D_old(w)| < T,\n"
        "                          in the same units as channel positions\n"
        "                          (default 1e-3 channels)\n"
        "  --spk-suffix     ext  spk file suffix (default 'spk'; auto-falls back\n"
        "                         to 'spkD' for stderiv pipelines when .spk is\n"
        "                         absent — pass an explicit value to disable fallback)\n"
        "  --out-clu-tag    tag  output .clu suffix (default 'drift')\n"
        "  --dry-run             read inputs, compute, print summary; don't write outputs\n"
        "  --quiet               suppress per-window log lines\n"
        "  -h, --help\n",
        prog);
}

bool parseArgs(int argc, char** argv, Params& p)
{
    if (argc < 3) { usage(argv[0]); return false; }
    p.sessionBase = argv[1];
    p.grp         = std::atoi(argv[2]);
    if (p.grp <= 0) {
        std::fprintf(stderr, "ERROR: group must be a positive integer (got '%s')\n",
                     argv[2]);
        return false;
    }
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](double* dest, const char* opt) -> bool {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "ERROR: %s expects a value\n", opt);
                return false;
            }
            *dest = std::atof(argv[++i]);
            return true;
        };
        auto nexti = [&](int* dest, const char* opt) -> bool {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "ERROR: %s expects a value\n", opt);
                return false;
            }
            *dest = std::atoi(argv[++i]);
            return true;
        };
        auto nexts = [&](std::string* dest, const char* opt) -> bool {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "ERROR: %s expects a value\n", opt);
                return false;
            }
            *dest = argv[++i];
            return true;
        };
        if      (a == "-h" || a == "--help")       { usage(argv[0]); std::exit(0); }
        else if (a == "--n-chan")                  { if (!nexti(&p.nChan,         a.c_str())) return false; }
        else if (a == "--n-samp")                  { if (!nexti(&p.nSamp,         a.c_str())) return false; }
        else if (a == "--sampling-rate")           { if (!next (&p.samplingRate,  a.c_str())) return false; }
        else if (a == "--window-min")              { if (!next (&p.windowMin,     a.c_str())) return false; }
        else if (a == "--overlap-min")             { if (!next (&p.overlapMin,    a.c_str())) return false; }
        else if (a == "--min-win-spikes")          { if (!nexti(&p.minWinSpikes,  a.c_str())) return false; }
        else if (a == "--lambda")                  { if (!next (&p.lambda,        a.c_str())) return false; }
        else if (a == "--merge-thresh")            { if (!next (&p.mergeThresh,   a.c_str())) return false; }
        else if (a == "--max-shift")               { if (!nexti(&p.maxShift,      a.c_str())) return false; }
        else if (a == "--min-win-overlap")         { if (!nexti(&p.minWinOverlap, a.c_str())) return false; }
        else if (a == "--gain-fit")                { if (!nexti(&p.gainFit,            a.c_str())) return false; }
        else if (a == "--gain-smooth-lambda")      { if (!next (&p.gainSmoothLambda,   a.c_str())) return false; }
        else if (a == "--gain-unity-prior")        { if (!next (&p.gainUnityPrior,     a.c_str())) return false; }
        else if (a == "--channel-positions")       { if (!nexts(&p.channelPositionsFile, a.c_str())) return false; }
        else if (a == "--position-thresh")         { if (!next (&p.positionThresh,        a.c_str())) return false; }
        else if (a == "--position-smooth-lambda")  { if (!next (&p.positionSmoothLambda,  a.c_str())) return false; }
        else if (a == "--position-units")          { if (!nexts(&p.positionUnits,         a.c_str())) return false; }
        else if (a == "--shared-drift")            { p.sharedDrift = true; }
        else if (a == "--shared-drift-min-clusters") { if (!nexti(&p.sharedDriftMinClusters, a.c_str())) return false; }
        else if (a == "--shared-drift-prior")      { if (!next (&p.sharedDriftPrior,      a.c_str())) return false; }
        else if (a == "--shared-drift-iter")       { if (!nexti(&p.sharedDriftIter,       a.c_str())) return false; }
        else if (a == "--shared-drift-huber-k")    { if (!next (&p.sharedDriftHuberK,     a.c_str())) return false; }
        else if (a == "--shared-drift-tol")        { if (!next (&p.sharedDriftTol,        a.c_str())) return false; }
        else if (a == "--max-merge-chain")         { if (!nexti(&p.maxMergeChain,         a.c_str())) return false; }
        else if (a == "--cross-temporal")          { p.crossTemporal = true; }
        else if (a == "--cross-temporal-max-gap")  { if (!nexti(&p.crossTemporalMaxGap,       a.c_str())) return false; }
        else if (a == "--cross-temporal-neighborhood") { if (!nexti(&p.crossTemporalNeighborhood, a.c_str())) return false; }
        else if (a == "--peak-sample")             { if (!nexti(&p.peakSample,    a.c_str())) return false; }
        else if (a == "--spk-suffix")              { if (!nexts(&p.spkSuffix,     a.c_str())) return false; }
        else if (a == "--out-clu-tag")             { if (!nexts(&p.outCluTag,     a.c_str())) return false; }
        else if (a == "--dry-run")                 { p.dryRun = true; }
        else if (a == "--quiet")                   { p.verbose = 0; }
        else {
            std::fprintf(stderr, "ERROR: unknown argument '%s'\n", a.c_str());
            usage(argv[0]);
            return false;
        }
    }
    if (p.overlapMin < 0 || p.overlapMin >= p.windowMin) {
        std::fprintf(stderr,
            "ERROR: --overlap-min must be in [0, window-min)\n");
        return false;
    }
    // Geometry (nChan, nSamp, samplingRate, peakSample) is auto-filled
    // from the session YAML — see fillFromYaml() called after this.
    // CLI values are kept (non-zero == explicit user override); zeros
    // get populated from YAML.  Validated by validateGeometry() at the
    // top of main() after the YAML pass.
    return true;
}

// ─── YAML auto-fill ─────────────────────────────────────────────────────────
//
// Reads <sessionBase>.yaml (or .yml) and fills any zero-valued geometry
// fields in `p`.  Non-zero fields (set explicitly via CLI) are left alone —
// CLI wins.  Matches KlustaKwikYaml.cpp's path/schema conventions:
//   - acquisitionSystem.samplingRate
//   - spikeDetection.channelGroups[grp-1].channels.channel  (count → nChan)
//   - spikeDetection.channelGroups[grp-1].nSamples           → nSamp
//   - spikeDetection.channelGroups[grp-1].peakSampleIndex    → peakSample
//
// Also handles the hand-authored variant where `channels` is a flat
// sequence rather than a map with `channel`.
//
// Silent no-op if the YAML file isn't present — callers without a YAML
// can still pass every value via CLI.
void fillFromYaml(Params& p)
{
    auto tryOpen = [&](const std::string& ext) -> std::string {
        std::string path = p.sessionBase + ext;
        std::FILE* f = std::fopen(path.c_str(), "r");
        if (f) { std::fclose(f); return path; }
        return {};
    };
    std::string path = tryOpen(".yaml");
    if (path.empty()) path = tryOpen(".yml");
    if (path.empty()) return;   // no YAML present; CLI must cover

    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        std::fprintf(stderr,
            "[drifttracker] warning: failed to parse '%s': %s\n",
            path.c_str(), e.what());
        return;
    }

    // acquisitionSystem.samplingRate
    try {
        const auto& acq = root["acquisitionSystem"];
        if (acq && acq.IsMap() && acq["samplingRate"] && p.samplingRate <= 0.0)
            p.samplingRate = acq["samplingRate"].as<double>(0.0);
    } catch (...) {}

    // spikeDetection.channelGroups[grp-1] — per-group geometry
    try {
        const auto& sd = root["spikeDetection"];
        if (!sd || !sd.IsMap()) return;
        const auto& groups = sd["channelGroups"];
        if (!groups || !groups.IsSequence()) return;

        const int idx = p.grp - 1;        // YAML 0-based; --grp is 1-based
        if (idx < 0 || idx >= static_cast<int>(groups.size())) {
            std::fprintf(stderr,
                "[drifttracker] warning: group %d requested but '%s' has only "
                "%d spikeDetection groups\n",
                p.grp, path.c_str(), static_cast<int>(groups.size()));
            return;
        }

        const auto& grp = groups[idx];
        if (!grp || !grp.IsMap()) return;

        // channels: ndmanager schema is { channels: { channel: [0,1,...] } };
        // hand-authored YAMLs sometimes use a flat sequence.  Handle both.
        if (grp["channels"] && p.nChan <= 0) {
            const auto& ch = grp["channels"];
            if (ch.IsSequence()) {
                p.nChan = static_cast<int>(ch.size());
            } else if (ch.IsMap() &&
                       ch["channel"] && ch["channel"].IsSequence()) {
                p.nChan = static_cast<int>(ch["channel"].size());
            }
        }
        if (grp["nSamples"] && p.nSamp <= 0)
            p.nSamp = grp["nSamples"].as<int>(0);
        if (grp["peakSampleIndex"] && p.peakSample <= 0)
            p.peakSample = grp["peakSampleIndex"].as<int>(0);
    } catch (...) {}
}

bool validateGeometry(const Params& p)
{
    if (p.nChan <= 0 || p.nSamp <= 0 || p.samplingRate <= 0) {
        std::fprintf(stderr,
            "ERROR: geometry incomplete (nChan=%d, nSamp=%d, samplingRate=%.0f).\n"
            "  Run from a directory with <session>.yaml present, or pass\n"
            "  --n-chan / --n-samp / --sampling-rate explicitly.\n",
            p.nChan, p.nSamp, p.samplingRate);
        return false;
    }
    return true;
}

// ─── Binary file I/O ────────────────────────────────────────────────────────

// Read .res.<grp>: little-endian int64 timestamps, no header.  File size in
// bytes / 8 = number of spikes.
bool readResFile(const std::string& path, std::vector<int64_t>& out)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot open %s (errno=%d)\n",
                     path.c_str(), errno);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz % static_cast<long>(sizeof(int64_t)) != 0) {
        std::fprintf(stderr, "ERROR: %s has odd size %ld\n", path.c_str(), sz);
        std::fclose(f);
        return false;
    }
    const size_t n = static_cast<size_t>(sz) / sizeof(int64_t);
    out.resize(n);
    if (std::fread(out.data(), sizeof(int64_t), n, f) != n) {
        std::fprintf(stderr, "ERROR: short read on %s\n", path.c_str());
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return true;
}

// Read .clu.<grp>: int32 header (declared cluster count, ignored here —
// we recompute from the actual ID set) + int32 cluster IDs per spike.
bool readCluFile(const std::string& path, size_t expectedN,
                 std::vector<int32_t>& out, int& headerOut)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot open %s (errno=%d)\n",
                     path.c_str(), errno);
        return false;
    }
    int32_t header = 0;
    if (std::fread(&header, sizeof(int32_t), 1, f) != 1) {
        std::fprintf(stderr, "ERROR: short header read on %s\n", path.c_str());
        std::fclose(f);
        return false;
    }
    headerOut = header;
    out.resize(expectedN);
    if (std::fread(out.data(), sizeof(int32_t), expectedN, f) != expectedN) {
        std::fprintf(stderr,
            "ERROR: %s spike count mismatch (expected %zu)\n",
            path.c_str(), expectedN);
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    return true;
}

// Open spike file and prepare per-spike pread access.  Returned FILE* must
// be closed by the caller.
std::FILE* openSpkFile(const std::string& path)
{
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot open %s (errno=%d)\n",
                     path.c_str(), errno);
    }
    return f;
}

// Read one spike's waveform into `out` (size nChan * nSamp int16s).  Layout
// is sample-major channel-minor: out[s * nChan + ch].
bool readSpkRow(std::FILE* f, size_t spikeIdx, int nChan, int nSamp,
                std::vector<int16_t>& out)
{
    const size_t row = static_cast<size_t>(nChan) * nSamp;
    const long   off = static_cast<long>(spikeIdx * row * sizeof(int16_t));
    if (std::fseek(f, off, SEEK_SET) != 0) return false;
    out.resize(row);
    return std::fread(out.data(), sizeof(int16_t), row, f) == row;
}

// Write merged .clu file: int32 header followed by int32 IDs.  Header is the
// number of distinct cluster IDs (matches neurosuite convention).
//
// patch55 — atomic write semantics: writes to a sibling tempfile
// "<path>.tmp.<pid>", flushes + fsyncs it, then atomically rename()s onto
// the destination.  Guarantees the destination is either the prior content
// (if anything fails before rename) or the FULL new content (if rename
// succeeds) — never a partial / truncated file.
//
// Also: every step's failure is reported with strerror(errno) and the
// tempfile is unlinked, so a failed write doesn't leave debris behind.
//
// fclose() return value is checked too (deferred-write errors surface
// there, not in the fwrite calls).
bool writeCluFile(const std::string& path, const std::vector<int32_t>& ids)
{
    std::set<int32_t> uniq(ids.begin(), ids.end());
    const std::string tmpPath = path + ".tmp." + std::to_string((long)getpid());

    std::FILE* f = std::fopen(tmpPath.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot create tempfile %s: %s\n",
                     tmpPath.c_str(), std::strerror(errno));
        return false;
    }

    auto cleanupAndFail = [&](const char* what) -> bool {
        std::fprintf(stderr, "ERROR: %s on %s: %s\n",
                     what, tmpPath.c_str(), std::strerror(errno));
        std::fclose(f);
        ::unlink(tmpPath.c_str());
        return false;
    };

    const int32_t header = static_cast<int32_t>(uniq.size());
    if (std::fwrite(&header, sizeof(int32_t), 1, f) != 1)
        return cleanupAndFail("fwrite(header) failed");
    if (std::fwrite(ids.data(), sizeof(int32_t), ids.size(), f) != ids.size())
        return cleanupAndFail("fwrite(ids) failed");
    if (std::fflush(f) != 0)
        return cleanupAndFail("fflush failed");
    // fsync the data before closing — guarantees durability so a crash
    // between fclose() and the rename() can't lose the new content.
    if (::fsync(fileno(f)) != 0)
        return cleanupAndFail("fsync failed");
    if (std::fclose(f) != 0) {
        // f is already closed; nothing to clean up on the FILE side.
        std::fprintf(stderr, "ERROR: fclose failed on %s: %s\n",
                     tmpPath.c_str(), std::strerror(errno));
        ::unlink(tmpPath.c_str());
        return false;
    }
    // Atomic rename: POSIX guarantees the destination is replaced
    // in a single inode swap, never half-replaced.
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::fprintf(stderr, "ERROR: rename %s → %s failed: %s\n",
                     tmpPath.c_str(), path.c_str(), std::strerror(errno));
        ::unlink(tmpPath.c_str());
        return false;
    }
    return true;
}

// ─── Windowing ──────────────────────────────────────────────────────────────
//
// Build window boundaries in sample units.  Returns the [start, end) sample
// of each window.  The last window may be shorter than `wSamples` if the
// session ends mid-window.
std::vector<std::pair<int64_t, int64_t>>
buildWindows(int64_t sessionLen,
             int64_t wSamples,    // window length in samples
             int64_t sSamples)    // stride in samples
{
    std::vector<std::pair<int64_t, int64_t>> wins;
    if (wSamples <= 0 || sSamples <= 0 || sessionLen <= 0) return wins;
    int64_t start = 0;
    while (start < sessionLen) {
        int64_t end = std::min(start + wSamples, sessionLen);
        wins.emplace_back(start, end);
        if (end >= sessionLen) break;
        start += sSamples;
    }
    return wins;
}

// ─── Phase A: per-cluster per-window mean waveforms ─────────────────────────
//
// Walks spikes once.  For each window-membership pair (a spike that falls in
// window w and has cluster c), accumulates its waveform into accum[c][w] and
// increments count[c][w].  After the pass, divides by the count (where
// non-zero) to produce the MLE mean.  Spikes near window edges may be
// counted in multiple windows when overlap > 0; this is by design — the
// overlap is what makes the trajectory smooth at window boundaries.
struct PerClusterTemplates {
    // clusterId -> vector of length nWin of mean waveforms.  Each waveform
    // is wElems = nChan * nSamp doubles, sample-major channel-minor.
    std::unordered_map<int32_t, std::vector<std::vector<double>>> mean;
    // Per-cluster per-window spike count (used as α / valid-mask).
    std::unordered_map<int32_t, std::vector<int>>                 count;
};

bool computeWindowedTemplates(
    const Params& p,
    const std::vector<int64_t>& res,
    const std::vector<int32_t>& clu,
    const std::vector<std::pair<int64_t, int64_t>>& windows,
    std::FILE* spkFile,
    PerClusterTemplates& out)
{
    const int wElems = p.nChan * p.nSamp;
    const int nWin   = static_cast<int>(windows.size());
    const size_t N   = res.size();
    if (N != clu.size()) {
        std::fprintf(stderr, "ERROR: res/clu count mismatch (%zu vs %zu)\n",
                     N, clu.size());
        return false;
    }

    // Build a sorted (clusterId, spikeIdx) listing per cluster so we can
    // process clusters one at a time and stream through windows.
    std::unordered_map<int32_t, std::vector<size_t>> bycluster;
    for (size_t i = 0; i < N; ++i) bycluster[clu[i]].push_back(i);

    std::vector<int16_t> row;
    for (auto& kv : bycluster) {
        const int32_t c = kv.first;
        if (c < 2) continue;            // skip noise (0) and MUA (1)
        const auto& members = kv.second;

        auto& meanC  = out.mean[c];
        auto& countC = out.count[c];
        meanC.assign(nWin, std::vector<double>(wElems, 0.0));
        countC.assign(nWin, 0);

        // Member spikes are appended in spike-index order, which is also
        // timestamp order (.res is monotonic).  For each member, find all
        // windows whose [start, end) span this timestamp and accumulate.
        // With overlap=0 there's exactly one window; with overlap > 0
        // there can be up to 2 (when the spike falls in the overlap of
        // two consecutive windows).  Binary search the first eligible
        // window for tight inner loops.
        int firstWin = 0;
        for (size_t idx : members) {
            const int64_t t = res[idx];
            // Advance firstWin past windows whose end ≤ t.
            while (firstWin < nWin && windows[firstWin].second <= t) ++firstWin;
            if (firstWin >= nWin) break;

            // Read the spike waveform once per member (regardless of how
            // many windows it ends up in).
            if (!readSpkRow(spkFile, idx, p.nChan, p.nSamp, row)) {
                // Skip spikes that fail to read rather than abort the whole
                // run — the report will indicate degraded windows.
                continue;
            }

            // Apply to every window whose [start, end) contains t.
            for (int w = firstWin; w < nWin; ++w) {
                const auto& wn = windows[w];
                if (t <  wn.first) break;        // gap (shouldn't happen with overlap)
                if (t >= wn.second) continue;    // past this window
                auto& acc = meanC[w];
                for (int i = 0; i < wElems; ++i)
                    acc[i] += static_cast<double>(row[i]);
                ++countC[w];
                // Could short-circuit after 2 windows since overlap < window,
                // but the check is cheap; let the [start, end) test handle it.
            }
        }

        // Finalise: divide by count, leave undefined windows at 0.
        for (int w = 0; w < nWin; ++w) {
            if (countC[w] <= 0) continue;
            const double inv = 1.0 / countC[w];
            for (auto& v : meanC[w]) v *= inv;
        }
    }

    return true;
}

// ─── Phase B: Tikhonov temporal smoothing ───────────────────────────────────
//
// For each cluster and each waveform dimension d, solve the tridiagonal
// system independently.  System (n_win × n_win):
//
//     A · x = b
//
//   A is symmetric tridiagonal with
//       diag[w]      = α[w] + (1 if w==0 or w==nWin-1 else 2)·λ
//       off-diag[w]  = -λ        (between w and w+1)
//   b[w] = α[w] · mean[c, w, d]
//
// where α[w] = 1 if the window has enough spikes (defined), else 0.
//
// Thomas algorithm (forward elimination + back substitution) — O(nWin) per
// dimension, O(nWin · wElems · nCluster) total.
void solveTridiagonalThomas(
    int    n,
    const double* diag,           // size n
    double  offdiag_const,        // -lambda; constant along the band
    const double* rhs,            // size n
    double* x)                    // size n; output
{
    if (n <= 0) return;
    if (n == 1) { x[0] = rhs[0] / diag[0]; return; }

    // Scratch for the modified diagonal and rhs
    std::vector<double> c_prime(n - 1);
    std::vector<double> d_prime(n);

    c_prime[0] = offdiag_const / diag[0];
    d_prime[0] = rhs[0]        / diag[0];
    for (int i = 1; i < n; ++i) {
        const double denom = diag[i] - offdiag_const * c_prime[i - 1];
        if (i < n - 1) c_prime[i] = offdiag_const / denom;
        d_prime[i] = (rhs[i] - offdiag_const * d_prime[i - 1]) / denom;
    }
    x[n - 1] = d_prime[n - 1];
    for (int i = n - 2; i >= 0; --i)
        x[i] = d_prime[i] - c_prime[i] * x[i + 1];
}

void smoothTemplatesTikhonov(double lambda, PerClusterTemplates& tpl)
{
    if (tpl.mean.empty()) return;
    const int nWin = static_cast<int>(tpl.mean.begin()->second.size());
    if (nWin <= 1) return;

    // Stability guard #1 (patch50): with lambda <= 0 the Tikhonov coupling is
    // off, so each window's value should remain its raw mean.  Naively running
    // Thomas in this regime produces NaN: for any cluster with a transition
    // from defined (alpha=1) to undefined (alpha=0) windows, the elimination
    // step hits 0/0 at the first undefined index and IEEE 754's `0 * NaN = NaN`
    // back-propagates through the entire trajectory — destroying even the
    // defined windows.  Skip the solver entirely; raw means stay as-is and
    // undefined windows stay at their zero-initialised state.
    if (lambda <= 0.0) return;

    const int wElems = static_cast<int>(tpl.mean.begin()->second.front().size());

    std::vector<double> diag(nWin), rhs(nWin), x(nWin);

    for (auto& kv : tpl.mean) {
        const int32_t c = kv.first;
        auto&         M = kv.second;        // [nWin][wElems]
        const auto&   A = tpl.count.at(c);

        // Stability guard #2 (patch50): the Tikhonov system A·x = b is
        // singular when no window has α > 0 — the matrix becomes the pure
        // Laplacian λL whose null space contains the constant vector
        // (Σ rows = 0 for every row).  Thomas elimination hits 0 at the
        // last step; the resulting NaN back-propagates.  Skip such
        // clusters entirely; their means stay at the all-zero initial state.
        bool anyDefined = false;
        for (int w = 0; w < nWin; ++w) {
            if (A[w] > 0) { anyDefined = true; break; }
        }
        if (!anyDefined) continue;

        for (int d = 0; d < wElems; ++d) {
            // Build diag + rhs for this dimension.
            for (int w = 0; w < nWin; ++w) {
                const double alpha = (A[w] > 0) ? 1.0 : 0.0;
                // Interior windows have two cross-terms (w-1, w+1); endpoints
                // have one.  Coefficient on x[w] is alpha + sum-of-lambdas.
                const double nb = ((w > 0)        ? 1.0 : 0.0)
                                + ((w < nWin - 1) ? 1.0 : 0.0);
                diag[w] = alpha + nb * lambda;
                rhs[w]  = alpha * M[w][d];
            }
            // Solve and write back.
            solveTridiagonalThomas(nWin, diag.data(), -lambda, rhs.data(), x.data());
            for (int w = 0; w < nWin; ++w) M[w][d] = x[w];
        }
    }
}

// ─── Phase B.5: position-aware drift tracking (patch49) ─────────────────────
//
// Each cluster gets a 2D centre-of-mass trajectory through (x, y) channel-
// position space.  For each window w and each cluster c, the COM is:
//
//   ptp[ch]  = max(m[w][:, ch]) − min(m[w][:, ch])      (peak-to-peak per channel)
//   com_x[c, w] = Σ_ch ptp[ch] · pos_x[ch] / Σ_ch ptp[ch]
//   com_y[c, w] = Σ_ch ptp[ch] · pos_y[ch] / Σ_ch ptp[ch]
//
// Peak-to-peak amplitude is the standard weighting in modern sorters
// (Kilosort, DARTsort).  When the upstream Tikhonov smoother (Phase B)
// produces a near-zero waveform for some (c, w) — typically because the
// cluster went silent in that window — the per-window COM is flagged
// undefined and interpolated by a second pass of Tikhonov in 2D position
// space.
//
// The drift signal a cluster exhibits is then a 2D vector trajectory
// {com_x[c, w], com_y[c, w]}_w, much lower-dimensional than the full
// waveform trajectory.  Pairs of clusters that follow the same underlying
// drifting unit have COM trajectories that overlap closely; pairs that
// differ in absolute position by more than a few channels diameter are
// unlikely to be the same unit even if their waveform residuals look
// favourable.
struct ClusterCOMTrajectory {
    std::unordered_map<int32_t, std::vector<double>> x;       // [nWin] per cluster
    std::unordered_map<int32_t, std::vector<double>> y;       // [nWin]
    std::unordered_map<int32_t, std::vector<int>>    defined; // [nWin] 0/1 flag
};

// Load channel positions from a 2-column text file (one "x y" pair per
// channel, in group order).  Lines starting with '#' are ignored.  Returns
// false on any parse error; on success fills posX/posY (both size nChan).
//
// When no file is given, falls back to channel index: (x=0, y=index) so
// downstream COM math works in unit-less "channel" coordinates that are
// still monotonic in the natural channel ordering on a linear shank.
bool loadOrDefaultChannelPositions(
    const std::string& path, int nChan,
    std::vector<double>& posX, std::vector<double>& posY)
{
    posX.assign(nChan, 0.0);
    posY.assign(nChan, 0.0);
    if (path.empty()) {
        for (int ch = 0; ch < nChan; ++ch) posY[ch] = static_cast<double>(ch);
        return true;
    }
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot open --channel-positions '%s'\n",
                     path.c_str());
        return false;
    }
    int  idx     = 0;            // data row index (0..nChan-1)
    int  lineno  = 0;            // actual file line number (patch51 — accurate diagnostics)
    std::string line;
    while (std::getline(f, line)) {
        ++lineno;
        // Strip leading whitespace, skip blank/comment lines.
        size_t a = 0;
        while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) ++a;
        if (a >= line.size() || line[a] == '#') continue;
        // Parse "x y" — sscanf returns number of fields read.  Use %n to
        // know how far it advanced, then check the rest of the line for
        // unexpected trailing tokens (patch51 — defensive parsing).
        double x, y;
        int    consumed = 0;
        const int n = std::sscanf(line.c_str() + a, "%lf %lf%n", &x, &y, &consumed);
        if (n != 2) {
            std::fprintf(stderr,
                "ERROR: --channel-positions '%s' line %d: expected 'x y', got '%s'\n",
                path.c_str(), lineno, line.c_str() + a);
            return false;
        }
        // Check for trailing junk beyond y (skipping whitespace).  A third
        // numeric token on the line — easy to type by accident if user
        // meant (x, y, z) — is an outright parse error, not a warning.
        // Comments after y are fine.
        size_t tail = a + consumed;
        while (tail < line.size() && (line[tail] == ' ' || line[tail] == '\t')) ++tail;
        if (tail < line.size() && line[tail] != '#') {
            std::fprintf(stderr,
                "ERROR: --channel-positions '%s' line %d: trailing token after "
                "'x y' — got '%s'.  Format is exactly two columns; use '#' for "
                "inline comments.\n",
                path.c_str(), lineno, line.c_str() + tail);
            return false;
        }
        if (idx >= nChan) {
            std::fprintf(stderr,
                "ERROR: --channel-positions '%s' line %d: more data rows than "
                "the group's nChan=%d\n",
                path.c_str(), lineno, nChan);
            return false;
        }
        posX[idx] = x;
        posY[idx] = y;
        ++idx;
    }
    if (idx != nChan) {
        std::fprintf(stderr,
            "ERROR: --channel-positions '%s' has %d data row(s), expected nChan=%d\n",
            path.c_str(), idx, nChan);
        return false;
    }
    return true;
}

// Compute per-cluster per-window COM trajectories from the smoothed mean
// waveforms.  Windows with zero or near-zero total ptp are flagged
// `defined=0` and left at zero; the smoothing pass fills them by interpolation.
void computeCOMTrajectories(
    const PerClusterTemplates& tpl,
    const std::vector<double>& posX,
    const std::vector<double>& posY,
    int nChan, int nSamp,
    ClusterCOMTrajectory& out)
{
    if (tpl.mean.empty()) return;
    const int nWin = static_cast<int>(tpl.mean.begin()->second.size());

    for (const auto& kv : tpl.mean) {
        const int32_t c = kv.first;
        const auto&   M = kv.second;          // [nWin][nChan * nSamp]
        const auto&   A = tpl.count.at(c);    // [nWin]

        auto& xVec = out.x[c]; xVec.assign(nWin, 0.0);
        auto& yVec = out.y[c]; yVec.assign(nWin, 0.0);
        auto& dVec = out.defined[c]; dVec.assign(nWin, 0);

        for (int w = 0; w < nWin; ++w) {
            if (A[w] <= 0) continue;          // dead window — leave undefined
            const auto& mw = M[w];

            // Per-channel peak-to-peak on the (time-major channel-minor) layout.
            double totalAmp = 0.0;
            double sumX = 0.0, sumY = 0.0;
            for (int ch = 0; ch < nChan; ++ch) {
                double minV = mw[0 * nChan + ch];
                double maxV = minV;
                for (int t = 1; t < nSamp; ++t) {
                    const double v = mw[t * nChan + ch];
                    if (v < minV) minV = v;
                    if (v > maxV) maxV = v;
                }
                const double ptp = maxV - minV;
                totalAmp += ptp;
                sumX     += ptp * posX[ch];
                sumY     += ptp * posY[ch];
            }
            if (totalAmp > 0.0) {
                xVec[w] = sumX / totalAmp;
                yVec[w] = sumY / totalAmp;
                dVec[w] = 1;
            }
        }
    }
}

// Tikhonov-smooth each cluster's COM trajectory (per-axis, decoupled).
// Same banded tridiagonal pattern as smoothTemplatesTikhonov, but in 2 dims
// per window rather than nChan × nSamp.  Undefined windows have α=0 and are
// interpolated from neighbours.
//
// Two stability guards (patch50) — see smoothTemplatesTikhonov above for
// the analysis.  Both produce NaN trajectories without these guards.
void smoothCOMTrajectoriesTikhonov(double lambda, ClusterCOMTrajectory& com)
{
    if (com.x.empty()) return;
    const int nWin = static_cast<int>(com.x.begin()->second.size());
    if (nWin <= 1) return;
    if (lambda <= 0.0) return;          // guard #1: no smoothing, raw values kept

    std::vector<double> diag(nWin), rhs(nWin), x(nWin);

    for (auto& kv : com.x) {
        const int32_t c = kv.first;
        auto& xVec = kv.second;
        auto& yVec = com.y.at(c);
        const auto& dVec = com.defined.at(c);

        // Guard #2: skip clusters with no defined windows (Laplacian-only
        // system is singular; Thomas produces NaN).
        bool anyDefined = false;
        for (int w = 0; w < nWin; ++w) {
            if (dVec[w]) { anyDefined = true; break; }
        }
        if (!anyDefined) continue;

        for (int axis = 0; axis < 2; ++axis) {
            std::vector<double>& v = (axis == 0) ? xVec : yVec;
            for (int w = 0; w < nWin; ++w) {
                const double alpha = static_cast<double>(dVec[w]);
                const double nb = ((w > 0)        ? 1.0 : 0.0)
                                + ((w < nWin - 1) ? 1.0 : 0.0);
                diag[w] = alpha + nb * lambda;
                rhs[w]  = alpha * v[w];
            }
            solveTridiagonalThomas(nWin, diag.data(), -lambda, rhs.data(), x.data());
            for (int w = 0; w < nWin; ++w) v[w] = x[w];
        }
    }
}

// ─── Phase B.6: shared population-level drift (patch57) ─────────────────────
//
// When clusters are temporally fragmented — many sortings produce hundreds
// of clusters whose lifetimes don't span the full session — pairwise
// per-cluster COM comparison can't directly compare clusters that lived
// in disjoint windows.  This phase estimates a SHARED probe-wide drift
// D(w) from the collective motion of all clusters with pairwise window
// overlap, then subtracts D(w) from every cluster's COM trajectory.
// Disjoint clusters representing the same drifting unit then collapse to
// the same absolute position, enabling Phase C to find them as candidate
// merges.
//
// Method (DREDge-inspired, rigid-2D reduction):
//
//   1. For each pair of windows (w_i, w_j) i < j:
//        S = { c : com[c, w_i] defined AND com[c, w_j] defined }
//        skip if |S| < sharedDriftMinClusters
//        observed displacement:
//          d_ij = mean_{c in S}( com[c, w_j] - com[c, w_i] )
//        edge weight w_ij = |S|
//
//   2. Solve for D(w) by weighted least squares on the window-pair graph:
//        minimise sum_{ij} w_ij · | D(j) - D(i) - d_ij |²   per axis
//        anchored at D(0) = 0
//
//      The normal-equation matrix is the graph Laplacian; SPD by
//      construction.  Tikhonov ridge `sharedDriftPrior * I` on the diagonal
//      handles disconnected components (their D collapses to 0 — the
//      minimum-norm solution) and ill-conditioned graphs.
//
//   3. Subtract D(w) from every cluster's COM trajectory in place.
//
// Returns the per-window drift vector + diagnostics.  D[0] is forced to
// (0, 0) as the reference window.

struct SharedDriftResult {
    std::vector<std::pair<double, double>> D;       // (Dx, Dy) per window
    int                                    nEdges        = 0;
    int                                    nDisconnected = 0;
    bool                                   solveOk       = true;
    // patch58 — iteration diagnostics
    int                                    nIters        = 1;    // 1 = single-pass
    int                                    nOutliers     = 0;    // clusters w/ weight < 1 at convergence
    double                                 lastMaxDelta  = 0.0;  // max |ΔD(w)| at the final iter
};

// Cholesky factorise (LLᵀ) of an SPD matrix A stored row-major in flat
// vector of size n×n.  Lower triangle of A (incl. diagonal) is overwritten
// with L.  Upper triangle is read but not written; on return its content
// is meaningless.  Returns false if A is not numerically SPD (encountered
// non-positive pivot).
bool choleskyFactor(std::vector<double>& A, int n)
{
    for (int j = 0; j < n; ++j) {
        double s = A[(size_t)j * n + j];
        for (int k = 0; k < j; ++k) {
            const double v = A[(size_t)j * n + k];
            s -= v * v;
        }
        if (s <= 0.0) return false;
        const double pivot = std::sqrt(s);
        A[(size_t)j * n + j] = pivot;
        const double invPivot = 1.0 / pivot;
        for (int i = j + 1; i < n; ++i) {
            double s2 = A[(size_t)i * n + j];
            for (int k = 0; k < j; ++k)
                s2 -= A[(size_t)i * n + k] * A[(size_t)j * n + k];
            A[(size_t)i * n + j] = s2 * invPivot;
        }
    }
    return true;
}

// Solve L Lᵀ x = b given the lower-triangular L (factored A from above)
// stored in the lower triangle of A.  b is modified in place to hold x.
// Safe to call multiple times for different b vectors with the same factor.
void choleskySolve(const std::vector<double>& A, int n, std::vector<double>& b)
{
    // Forward substitution: L y = b  →  y in b
    for (int i = 0; i < n; ++i) {
        double s = b[(size_t)i];
        for (int k = 0; k < i; ++k) s -= A[(size_t)i * n + k] * b[(size_t)k];
        b[(size_t)i] = s / A[(size_t)i * n + i];
    }
    // Backward substitution: Lᵀ x = y  →  x in b
    for (int i = n - 1; i >= 0; --i) {
        double s = b[(size_t)i];
        for (int k = i + 1; k < n; ++k)
            s -= A[(size_t)k * n + i] * b[(size_t)k];
        b[(size_t)i] = s / A[(size_t)i * n + i];
    }
}

SharedDriftResult
estimateSharedDrift(const ClusterCOMTrajectory& com, int nWin, const Params& p)
{
    SharedDriftResult r;
    r.D.assign((size_t)nWin, {0.0, 0.0});
    if (nWin <= 1 || com.x.empty()) return r;

    // ── (1) Inverted index: clusters defined in each window ──
    std::vector<std::vector<int32_t>> clustersInWin((size_t)nWin);
    for (const auto& kv : com.defined) {
        const int32_t c = kv.first;
        const auto& dV = kv.second;
        for (int w = 0; w < nWin && w < (int)dV.size(); ++w)
            if (dV[(size_t)w]) clustersInWin[(size_t)w].push_back(c);
    }

    // ── (2) Edges: enumerate window pairs.  For each edge, KEEP THE
    //              PER-CLUSTER DISPLACEMENT OBSERVATIONS, not just the
    //              aggregated mean.  patch58 needs them for IRLS
    //              re-weighting; patch57's averaging happens inside each
    //              iteration.  Memory: at most W²·avgShared doubles ≈
    //              same order as the smoothed templates.
    struct EdgeObs {
        int                  i, j;
        std::vector<int32_t> clusters;
        std::vector<double>  dx, dy;       // per cluster, same order
    };
    std::vector<EdgeObs> edges;
    edges.reserve((size_t)nWin * (nWin - 1) / 2);
    for (int i = 0; i < nWin; ++i) {
        const auto& Si = clustersInWin[(size_t)i];
        if ((int)Si.size() < p.sharedDriftMinClusters) continue;
        std::unordered_set<int32_t> setI(Si.begin(), Si.end());
        for (int j = i + 1; j < nWin; ++j) {
            const auto& Sj = clustersInWin[(size_t)j];
            if ((int)Sj.size() < p.sharedDriftMinClusters) continue;
            EdgeObs e;
            e.i = i; e.j = j;
            e.clusters.reserve(std::min(Si.size(), Sj.size()));
            for (int32_t c : Sj) {
                if (!setI.count(c)) continue;
                e.clusters.push_back(c);
                e.dx.push_back(com.x.at(c)[(size_t)j] - com.x.at(c)[(size_t)i]);
                e.dy.push_back(com.y.at(c)[(size_t)j] - com.y.at(c)[(size_t)i]);
            }
            if ((int)e.clusters.size() < p.sharedDriftMinClusters) continue;
            edges.push_back(std::move(e));
        }
    }
    r.nEdges = (int)edges.size();
    if (edges.empty()) return r;

    // Disconnected-window count is a STRUCTURAL property of the edge graph,
    // independent of cluster weights — compute it once.
    {
        std::vector<int> degree((size_t)nWin, 0);
        for (const auto& e : edges) { degree[(size_t)e.i] += 1; degree[(size_t)e.j] += 1; }
        // Window 0 is the anchor; not "disconnected" even if it has no edges.
        for (int w = 1; w < nWin; ++w) if (degree[(size_t)w] == 0) r.nDisconnected += 1;
    }

    // ── (3) IRLS outer loop ──
    // Per-cluster Huber weight, updated each iteration.  Initial: all 1.0.
    std::unordered_map<int32_t, double> clusterWeight;
    for (const auto& kv : com.defined) clusterWeight[kv.first] = 1.0;

    const int    maxIter = std::max(1, p.sharedDriftIter);
    const int    n       = nWin - 1;      // reduced size after anchoring D[0] = 0
    const double prior   = std::max(p.sharedDriftPrior, 1e-9);
    auto idx = [&](int i, int j) -> size_t { return (size_t)i * n + j; };

    std::vector<std::pair<double, double>> Dprev((size_t)nWin, {0.0, 0.0});
    std::vector<std::pair<double, double>> Dcurr((size_t)nWin, {0.0, 0.0});

    for (int iter = 0; iter < maxIter; ++iter) {
        // Aggregate per-edge weighted means.
        struct EdgeAgg { int i, j; double dx, dy, weight; };
        std::vector<EdgeAgg> aggEdges;
        aggEdges.reserve(edges.size());
        for (const auto& e : edges) {
            double sw = 0.0, sx = 0.0, sy = 0.0;
            for (size_t k = 0; k < e.clusters.size(); ++k) {
                const double w = clusterWeight[e.clusters[k]];
                if (w <= 0.0) continue;
                sw += w;
                sx += w * e.dx[k];
                sy += w * e.dy[k];
            }
            if (sw < 1e-9) continue;
            aggEdges.push_back({e.i, e.j, sx / sw, sy / sw, sw});
        }
        if (aggEdges.empty()) { r.solveOk = false; break; }

        // Build Laplacian + RHS.
        std::vector<double> L((size_t)n * n, 0.0);
        std::vector<double> rx((size_t)n, 0.0), ry((size_t)n, 0.0);
        for (const auto& e : aggEdges) {
            const double w = e.weight;
            if (e.i > 0) {
                L[idx(e.i - 1, e.i - 1)] += w;
                rx[(size_t)(e.i - 1)] -= w * e.dx;
                ry[(size_t)(e.i - 1)] -= w * e.dy;
            }
            if (e.j > 0) {
                L[idx(e.j - 1, e.j - 1)] += w;
                rx[(size_t)(e.j - 1)] += w * e.dx;
                ry[(size_t)(e.j - 1)] += w * e.dy;
            }
            if (e.i > 0 && e.j > 0) {
                L[idx(e.i - 1, e.j - 1)] -= w;
                L[idx(e.j - 1, e.i - 1)] -= w;
            }
        }
        for (int i = 0; i < n; ++i) L[idx(i, i)] += prior;

        std::vector<double> Lcopy = L;
        if (!choleskyFactor(Lcopy, n)) { r.solveOk = false; break; }
        std::vector<double> dx = rx, dy = ry;
        choleskySolve(Lcopy, n, dx);
        choleskySolve(Lcopy, n, dy);

        Dprev = Dcurr;
        Dcurr[0] = {0.0, 0.0};
        for (int i = 0; i < n; ++i) Dcurr[(size_t)(i + 1)] = {dx[(size_t)i], dy[(size_t)i]};
        r.nIters = iter + 1;

        // Convergence check (after first iteration).
        if (iter > 0) {
            double maxChange = 0.0;
            for (int w = 0; w < nWin; ++w) {
                const double ddx = Dcurr[(size_t)w].first  - Dprev[(size_t)w].first;
                const double ddy = Dcurr[(size_t)w].second - Dprev[(size_t)w].second;
                const double m   = std::sqrt(ddx * ddx + ddy * ddy);
                if (m > maxChange) maxChange = m;
            }
            r.lastMaxDelta = maxChange;
            if (maxChange < p.sharedDriftTol) break;
        }
        if (iter == maxIter - 1) break;

        // ── Re-weight clusters via Huber on per-cluster residuals ──
        // For each cluster c, average its per-edge residual magnitude:
        //   r_c = mean over c's edges of |(observed dx, dy) - (pred dx, pred dy)|
        // where pred = D[j] - D[i].  Then Huber-weight w_c against MAD(r).
        std::unordered_map<int32_t, double> sumRes, cntRes;
        sumRes.reserve(clusterWeight.size());
        cntRes.reserve(clusterWeight.size());
        for (const auto& e : edges) {
            const double dxPred = Dcurr[(size_t)e.j].first  - Dcurr[(size_t)e.i].first;
            const double dyPred = Dcurr[(size_t)e.j].second - Dcurr[(size_t)e.i].second;
            for (size_t k = 0; k < e.clusters.size(); ++k) {
                const double rx_c = e.dx[k] - dxPred;
                const double ry_c = e.dy[k] - dyPred;
                const double rmag = std::sqrt(rx_c * rx_c + ry_c * ry_c);
                sumRes[e.clusters[k]] += rmag;
                cntRes[e.clusters[k]] += 1.0;
            }
        }

        std::vector<int32_t> cList;
        std::vector<double>  rPer;
        cList.reserve(sumRes.size());
        rPer.reserve(sumRes.size());
        for (const auto& kv : sumRes) {
            cList.push_back(kv.first);
            rPer.push_back(kv.second / cntRes.at(kv.first));
        }
        if (rPer.empty()) break;

        // Robust scale: 1.4826 · median(|r_c − median(r_c)|).
        std::vector<double> tmp = rPer;
        std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
        const double medR = tmp[tmp.size() / 2];
        std::vector<double> dev(rPer.size());
        for (size_t k = 0; k < rPer.size(); ++k)
            dev[k] = std::fabs(rPer[k] - medR);
        std::nth_element(dev.begin(), dev.begin() + dev.size() / 2, dev.end());
        const double mad   = std::max(dev[dev.size() / 2], 1e-12);
        const double scale = 1.4826 * mad;
        const double cutoff = p.sharedDriftHuberK * scale;

        // Update per-cluster weights.
        int nOut = 0;
        for (size_t k = 0; k < cList.size(); ++k) {
            const int32_t c = cList[k];
            const double r_c = rPer[k];
            if (r_c <= cutoff) {
                clusterWeight[c] = 1.0;
            } else {
                clusterWeight[c] = cutoff / r_c;   // Huber falloff: weight < 1
                if (clusterWeight[c] < 0.999) ++nOut;
            }
        }
        r.nOutliers = nOut;
    }

    r.D = Dcurr;
    return r;
}

// Subtract a per-window drift signal from every cluster's COM trajectory
// in place.  Skips clusters whose 'defined' flag is 0 at a given window
// (their values are meaningless and shouldn't shift).
void subtractSharedDrift(ClusterCOMTrajectory& com,
                         const std::vector<std::pair<double, double>>& D)
{
    if (com.x.empty()) return;
    const int nWin = (int)D.size();
    for (auto& kv : com.x) {
        const int32_t c = kv.first;
        auto& xV = kv.second;
        auto& yV = com.y.at(c);
        const int len = std::min((int)xV.size(), nWin);
        for (int w = 0; w < len; ++w) {
            xV[(size_t)w] -= D[(size_t)w].first;
            yV[(size_t)w] -= D[(size_t)w].second;
        }
    }
}

// ─── Phase C: drift-aware subtraction merge ─────────────────────────────────
//
// For each pair of clusters, compute the cyclic-shift L2 residual at each
// co-alive window and take the mean.  Uses the SMOOTHED templates from
// Phase B.  Returns the list of candidate pairs (D below threshold) sorted
// ascending by D, plus the per-pair best shift histogram for the report.
struct MergePair {
    int32_t                    a, b;       // a < b
    double                     D;          // mean over co-alive windows
    int                        nWinsUsed;
    std::vector<int>           bestShifts; // best τ per window
    // Per-channel time-varying gain diagnostic (patch47).  Empty when gainFit
    // is off.  meanAbsGainDev = mean over (co-alive window, channel) of
    // |a_w[ch] − 1|, summarising how much per-channel scaling was needed
    // to align A onto B.  Large values flag pairs whose drift was
    // amplitude-redistribution (electrode moved relative to the unit),
    // not just time-shift.
    double                     meanAbsGainDev = 0.0;
    // Position-aware diagnostic (patch49).  meanCOMDistance = mean over
    // co-alive windows of √((Δx)² + (Δy)²).  Units match channel positions
    // (μm if --channel-positions loaded, else "channels").  comDriftA /
    // comDriftB record how much each cluster's COM moved across the
    // session — useful for flagging pairs that "merge because they both
    // drifted in opposite directions through the same spot".
    double                     meanCOMDistance = -1.0;     // -1 = not computed
    double                     comDriftA       = 0.0;
    double                     comDriftB       = 0.0;
};

// One window's normalised L2 residual with cyclic-shift search.  miSrc is
// the cluster-A template, mjDst is cluster-B template.  Returns minD and
// the corresponding τ.
void pairwiseResidualOneWindow(
    const std::vector<double>& mi,
    const std::vector<double>& mj,
    int nChan, int nSamp, int maxShift,
    double& outD, int& outShift)
{
    // Pre-compute energies once.
    double ei = 0.0, ej = 0.0;
    for (double v : mi) ei += v * v;
    for (double v : mj) ej += v * v;
    const double denom = std::max(ei, ej);
    if (!(denom > 0.0)) { outD = std::numeric_limits<double>::infinity(); outShift = 0; return; }

    double bestD = std::numeric_limits<double>::infinity();
    int    bestT = 0;
    for (int tau = -maxShift; tau <= maxShift; ++tau) {
        double rss = 0.0;
        for (int t = 0; t < nSamp; ++t) {
            // Cyclic source row: (t + tau) wrapped into [0, nSamp).
            int tSrc = t + tau;
            while (tSrc <    0)   tSrc += nSamp;
            while (tSrc >= nSamp) tSrc -= nSamp;
            const int rowSrc = tSrc * nChan;
            const int rowDst = t    * nChan;
            for (int ch = 0; ch < nChan; ++ch) {
                const double d = mi[rowSrc + ch] - mj[rowDst + ch];
                rss += d * d;
            }
        }
        const double D = rss / denom;
        if (D < bestD) { bestD = D; bestT = tau; }
    }
    outD = bestD;
    outShift = bestT;
}

// ─── Per-channel time-varying gain fit (patch47) ────────────────────────────
//
// Given two cluster trajectories Ma[w] and Mb[w] (each [nWin][wElems] doubles
// in time-major channel-minor layout), plus a per-window best cyclic shift
// τ_w from pairwiseResidualOneWindow, fit a per-channel gain sequence
// gain[w, ch] minimising:
//
//   Σ_w α_w · ||gain[w, :] ⊙ shift_{τ_w}(Ma[w])  −  Mb[w]||²
//   +  μ     · Σ_w (gain[w, ch] − gain[w-1, ch])²       (smoothness)
//   +  λ_u   · Σ_w (gain[w, ch] − 1)²                   (unity prior)
//
// where α_w = 1 if window w is co-alive (both clusters have counts), else 0.
// The channels decouple — nChan independent symmetric positive-definite
// tridiagonal systems, each solved by Thomas in O(nWin).
//
// For channel ch, expanding ||gain · A − B||² and setting d/d gain_v = 0
// per window v gives:
//
//   a_v · (α_v · ||A_v[ch]||² + nb·μ + λ_u)  −  μ · a_{v-1}  −  μ · a_{v+1}
//   = α_v · ⟨A_v[ch], B_v[ch]⟩  +  λ_u
//
// where nb = 1 for boundary windows, 2 for interior. The off-diagonal is
// constant −μ along the band, matching solveTridiagonalThomas's interface.
//
// Returns the post-fit residual D averaged over co-alive windows, using
// the gain-corrected energy in the denominator:
//
//   D_w  =  ||gain_w ⊙ shift_τw(Ma_w)  −  Mb_w||² /
//           max(||gain_w ⊙ shift_τw(Ma_w)||²,  ||Mb_w||²)
//
// gainOut is filled with [nWin][nChan] doubles; meanAbsGainDevOut is the
// summary used for the report banner.
struct GainFitResult {
    double meanD;            // gain-corrected, averaged over co-alive windows
    double meanAbsGainDev;   // mean |gain - 1| over (alive window × channel)
    std::vector<std::vector<double>> gain;   // [nWin][nChan]
};

GainFitResult fitPerChannelGainTrajectory(
    const std::vector<std::vector<double>>& Ma,   // [nWin][wElems]
    const std::vector<std::vector<double>>& Mb,
    const std::vector<int>& Aa,                   // per-window counts, 0 = dead
    const std::vector<int>& Ab,
    const std::vector<int>& bestShifts,           // length = co-alive windows; in window-order
    int nChan, int nSamp,
    double mu, double lambdaUnity)
{
    GainFitResult out;
    const int nWin = static_cast<int>(Ma.size());
    out.gain.assign(nWin, std::vector<double>(nChan, 1.0));   // identity init

    // Pre-compute, per (window, channel):
    //   normA_w[ch]  = ||shift_τ(A_w)[ch, :]||²
    //   innerAB_w[ch] = ⟨shift_τ(A_w)[ch, :], B_w[ch, :]⟩
    //   normB_w[ch]   = ||B_w[ch, :]||²
    // Use 0 for windows that aren't co-alive — α=0 zeros their contribution.
    //
    // bestShifts is indexed in co-alive order; we need a per-window τ map.
    // Build a co-alive walker so we don't have to mirror that logic here.
    std::vector<double> normA (nWin * nChan, 0.0);
    std::vector<double> innerAB(nWin * nChan, 0.0);
    std::vector<double> normB (nWin * nChan, 0.0);
    std::vector<int>    alpha (nWin, 0);

    int coAliveIdx = 0;
    for (int w = 0; w < nWin; ++w) {
        if (Aa[w] <= 0 || Ab[w] <= 0) continue;
        alpha[w] = 1;
        const int tau = (coAliveIdx < (int)bestShifts.size())
                        ? bestShifts[coAliveIdx] : 0;
        ++coAliveIdx;
        for (int t = 0; t < nSamp; ++t) {
            int tSrc = t + tau;
            while (tSrc <    0)   tSrc += nSamp;
            while (tSrc >= nSamp) tSrc -= nSamp;
            const int rowSrc = tSrc * nChan;
            const int rowDst = t    * nChan;
            for (int ch = 0; ch < nChan; ++ch) {
                const double aV = Ma[w][rowSrc + ch];
                const double bV = Mb[w][rowDst + ch];
                normA  [w * nChan + ch] += aV * aV;
                innerAB[w * nChan + ch] += aV * bV;
                normB  [w * nChan + ch] += bV * bV;
            }
        }
    }

    // Solve nChan independent tridiagonal systems.  Stability guard (patch50):
    // when mu + lambdaUnity == 0 AND a channel has zero amplitude in some
    // window (normA[w*nChan+ch] == 0 either because alpha[w]==0 or because
    // the channel itself was dead), diag[w] = 0 and Thomas hits 0/0.  The
    // default lambdaUnity = 0.1 prevents this in normal use, but a user
    // explicitly disabling both regularisers should not crash.  Clamp the
    // effective diag-only constant to a tiny epsilon when both are zero.
    const bool noReg     = (mu <= 0.0 && lambdaUnity <= 0.0);
    const double diagEps = noReg ? 1e-12 : 0.0;
    std::vector<double> diag(nWin), rhs(nWin), x(nWin);
    for (int ch = 0; ch < nChan; ++ch) {
        for (int w = 0; w < nWin; ++w) {
            const double nb = ((w > 0)        ? 1.0 : 0.0)
                            + ((w < nWin - 1) ? 1.0 : 0.0);
            diag[w] = static_cast<double>(alpha[w]) * normA[w * nChan + ch]
                    + nb * mu
                    + lambdaUnity
                    + diagEps;
            rhs[w]  = static_cast<double>(alpha[w]) * innerAB[w * nChan + ch]
                    + lambdaUnity;
        }
        solveTridiagonalThomas(nWin, diag.data(), -mu, rhs.data(), x.data());
        for (int w = 0; w < nWin; ++w) out.gain[w][ch] = x[w];
    }

    // Compute the gain-corrected per-window residual and the mean-abs-dev
    // summary.  Walk co-alive windows again with their τ.
    double sumD = 0.0;
    int    nUsed = 0;
    double sumAbsDev = 0.0;
    long   nDevTerms = 0;
    coAliveIdx = 0;
    for (int w = 0; w < nWin; ++w) {
        if (!alpha[w]) continue;
        const int tau = (coAliveIdx < (int)bestShifts.size())
                        ? bestShifts[coAliveIdx] : 0;
        ++coAliveIdx;

        // Per-channel gain at this window
        const double* g = out.gain[w].data();

        // Accumulate gain-corrected RSS + per-channel gain-energy + B-energy
        double rss = 0.0, gainEnergy = 0.0, bEnergy = 0.0;
        for (int t = 0; t < nSamp; ++t) {
            int tSrc = t + tau;
            while (tSrc <    0)   tSrc += nSamp;
            while (tSrc >= nSamp) tSrc -= nSamp;
            const int rowSrc = tSrc * nChan;
            const int rowDst = t    * nChan;
            for (int ch = 0; ch < nChan; ++ch) {
                const double gA = g[ch] * Ma[w][rowSrc + ch];
                const double bV = Mb[w][rowDst + ch];
                const double d  = gA - bV;
                rss        += d   * d;
                gainEnergy += gA  * gA;
                bEnergy    += bV  * bV;
            }
        }
        const double denom = std::max(gainEnergy, bEnergy);
        if (denom > 0.0) {
            sumD += rss / denom;
            ++nUsed;
        }
        for (int ch = 0; ch < nChan; ++ch) {
            sumAbsDev += std::fabs(g[ch] - 1.0);
            ++nDevTerms;
        }
    }
    out.meanD          = (nUsed > 0)      ? sumD / nUsed
                                          : std::numeric_limits<double>::infinity();
    out.meanAbsGainDev = (nDevTerms > 0)  ? sumAbsDev / nDevTerms : 0.0;
    return out;
}

std::vector<MergePair>
driftAwareSubtractionMerge(
    const Params& p,
    const PerClusterTemplates& tpl,
    const ClusterCOMTrajectory* com)   // nullptr ⇒ position filter disabled
{
    std::vector<MergePair> candidates;
    std::vector<int32_t>   clusters;
    clusters.reserve(tpl.mean.size());
    for (const auto& kv : tpl.mean) clusters.push_back(kv.first);
    std::sort(clusters.begin(), clusters.end());

    for (size_t a = 0; a < clusters.size(); ++a) {
        const int32_t ca = clusters[a];
        const auto&   Ma = tpl.mean.at(ca);
        const auto&   Aa = tpl.count.at(ca);
        const int     nWin = static_cast<int>(Ma.size());

        for (size_t b = a + 1; b < clusters.size(); ++b) {
            const int32_t cb = clusters[b];
            const auto&   Mb = tpl.mean.at(cb);
            const auto&   Ab = tpl.count.at(cb);

            std::vector<int> shifts;
            shifts.reserve(nWin);
            double sumD = 0.0;
            int    nUsed = 0;
            for (int w = 0; w < nWin; ++w) {
                if (Aa[w] <= 0 || Ab[w] <= 0) continue;   // not co-alive
                double D; int tau;
                pairwiseResidualOneWindow(
                    Ma[w], Mb[w], p.nChan, p.nSamp, p.maxShift, D, tau);
                if (!std::isfinite(D)) continue;
                sumD += D;
                ++nUsed;
                shifts.push_back(tau);
            }
            if (nUsed < p.minWinOverlap) continue;
            double finalD     = sumD / nUsed;
            double gainDevOut = 0.0;

            // Per-channel time-varying gain refinement.  Cyclic τ already
            // picked per window above; gain fit now corrects amplitude
            // redistribution across channels.  Replaces the shift-only D
            // with a strictly-no-larger gain-corrected D (the gain fit
            // can always pick gain=1 to recover the shift-only solution).
            if (p.gainFit != 0) {
                GainFitResult gfr = fitPerChannelGainTrajectory(
                    Ma, Mb, Aa, Ab, shifts,
                    p.nChan, p.nSamp,
                    p.gainSmoothLambda, p.gainUnityPrior);
                if (std::isfinite(gfr.meanD)) {
                    finalD     = gfr.meanD;
                    gainDevOut = gfr.meanAbsGainDev;
                }
            }

            // Position-aware diagnostic + optional AND-filter (patch49).
            // Always compute when COM data is available; only apply the
            // filter when positionThresh > 0.  Pairs that pass the
            // waveform residual but disagree on COM position by more than
            // positionThresh are filtered out — likely two different units
            // that look superficially similar in waveform space.
            double meanCOM = -1.0;
            double driftA  = 0.0;
            double driftB  = 0.0;
            if (com != nullptr) {
                const auto& xA = com->x.at(ca);
                const auto& yA = com->y.at(ca);
                const auto& dA = com->defined.at(ca);
                const auto& xB = com->x.at(cb);
                const auto& yB = com->y.at(cb);
                const auto& dB = com->defined.at(cb);
                double sumDist = 0.0;
                int    nDist   = 0;
                // Per-cluster drift magnitude over the session: distance
                // from earliest-defined to latest-defined COM.
                int firstA = -1, lastA = -1, firstB = -1, lastB = -1;
                for (int w = 0; w < nWin; ++w) {
                    if (dA[w]) { if (firstA < 0) firstA = w; lastA = w; }
                    if (dB[w]) { if (firstB < 0) firstB = w; lastB = w; }
                    if (Aa[w] <= 0 || Ab[w] <= 0) continue;
                    const double dx = xA[w] - xB[w];
                    const double dy = yA[w] - yB[w];
                    sumDist += std::sqrt(dx * dx + dy * dy);
                    ++nDist;
                }
                if (nDist > 0) meanCOM = sumDist / nDist;
                if (firstA >= 0 && lastA > firstA) {
                    const double dx = xA[lastA] - xA[firstA];
                    const double dy = yA[lastA] - yA[firstA];
                    driftA = std::sqrt(dx * dx + dy * dy);
                }
                if (firstB >= 0 && lastB > firstB) {
                    const double dx = xB[lastB] - xB[firstB];
                    const double dy = yB[lastB] - yB[firstB];
                    driftB = std::sqrt(dx * dx + dy * dy);
                }
                // AND-filter
                if (p.positionThresh > 0.0 && meanCOM > p.positionThresh)
                    continue;
            }

            if (finalD < p.mergeThresh) {
                MergePair mp;
                mp.a              = ca;
                mp.b              = cb;
                mp.D              = finalD;
                mp.nWinsUsed      = nUsed;
                mp.bestShifts     = std::move(shifts);
                mp.meanAbsGainDev = gainDevOut;
                mp.meanCOMDistance = meanCOM;
                mp.comDriftA      = driftA;
                mp.comDriftB      = driftB;
                candidates.push_back(std::move(mp));
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const MergePair& x, const MergePair& y) { return x.D < y.D; });
    return candidates;
}

// ─── Phase C2: cross-temporal pairing (patch60) ─────────────────────────────
//
// For sessions where most clusters live for a few-window block and are
// then "replaced" by a successor cluster at a different probe position
// (caused by drift moving the unit on/off the spike-detection threshold),
// Phase C cannot find the successor relationship — Phase C requires
// minWinOverlap co-alive windows, which the disjoint clusters don't have.
//
// Phase C2 enumerates pairs whose alive intervals DON'T overlap (or
// barely do), checks the time gap is within maxGap windows, and compares
// the earlier cluster's last K alive windows to the later cluster's
// first K alive windows.  Uses the same residual function and the same
// merge/position thresholds as Phase C.  Operates on the
// shared-drift-corrected COMs so disjoint clusters at "the same absolute
// position after drift correction" get paired.
//
// Skips pairs that Phase C already produced (looked up via the supplied
// existingCandidates list).  The returned candidates are appended to
// Phase C's list before Phase D's union-find.

std::vector<MergePair>
crossTemporalMerge(
    const Params& p,
    const PerClusterTemplates& tpl,
    const ClusterCOMTrajectory* com,
    const std::vector<MergePair>& existing)
{
    std::vector<MergePair> additional;
    if (!p.crossTemporal) return additional;

    std::vector<int32_t> clusters;
    clusters.reserve(tpl.mean.size());
    for (const auto& kv : tpl.mean) clusters.push_back(kv.first);
    std::sort(clusters.begin(), clusters.end());
    if (clusters.empty()) return additional;
    const int nWin = (int)tpl.mean.at(clusters[0]).size();

    // Index of pairs already in 'existing' to avoid duplicate candidates.
    auto pairKey = [](int32_t a, int32_t b) -> int64_t {
        const int32_t lo = std::min(a, b), hi = std::max(a, b);
        return ((int64_t)lo << 32) | (uint32_t)hi;
    };
    std::unordered_set<int64_t> seen;
    seen.reserve(existing.size() * 2);
    for (const auto& mp : existing) seen.insert(pairKey(mp.a, mp.b));

    // Pre-compute each cluster's sorted list of alive window indices.
    std::unordered_map<int32_t, std::vector<int>> aliveWins;
    aliveWins.reserve(clusters.size() * 2);
    for (int32_t c : clusters) {
        const auto& Av = tpl.count.at(c);
        std::vector<int>& v = aliveWins[c];
        for (int w = 0; w < (int)Av.size(); ++w) if (Av[w] > 0) v.push_back(w);
    }

    const int K      = std::max(1, p.crossTemporalNeighborhood);
    const int maxGap = std::max(0, p.crossTemporalMaxGap);

    for (size_t i = 0; i < clusters.size(); ++i) {
        const int32_t ca = clusters[i];
        const auto& aliveA = aliveWins.at(ca);
        if (aliveA.empty()) continue;
        const auto& Ma = tpl.mean.at(ca);

        for (size_t j = i + 1; j < clusters.size(); ++j) {
            const int32_t cb = clusters[j];
            if (seen.count(pairKey(ca, cb))) continue;
            const auto& aliveB = aliveWins.at(cb);
            if (aliveB.empty()) continue;
            const auto& Mb = tpl.mean.at(cb);

            // Co-alive count: skip if Phase C should have handled this.
            int coAlive = 0;
            {
                size_t ia = 0, ib = 0;
                while (ia < aliveA.size() && ib < aliveB.size()) {
                    if (aliveA[ia] == aliveB[ib]) { ++coAlive; ++ia; ++ib; }
                    else if (aliveA[ia] < aliveB[ib]) ++ia;
                    else ++ib;
                }
            }
            if (coAlive >= p.minWinOverlap) continue;

            // Determine temporal ordering: which cluster's alive interval
            // ends FIRST?  That's the "earlier" one.  We want
            //   earlier.lastAlive < later.firstAlive   (no overlap, or
            //   marginal overlap below minWinOverlap).
            const int firstA = aliveA.front(), lastA = aliveA.back();
            const int firstB = aliveB.front(), lastB = aliveB.back();
            int32_t cE, cL;
            const std::vector<int>* aliveE_p = nullptr;
            const std::vector<int>* aliveL_p = nullptr;
            const std::vector<std::vector<double>>* ME_p = nullptr;
            const std::vector<std::vector<double>>* ML_p = nullptr;
            int earlierLast = -1, laterFirst = -1;

            if (lastA <= firstB) {
                cE = ca; cL = cb;
                aliveE_p = &aliveA; aliveL_p = &aliveB;
                ME_p = &Ma; ML_p = &Mb;
                earlierLast = lastA; laterFirst = firstB;
            } else if (lastB <= firstA) {
                cE = cb; cL = ca;
                aliveE_p = &aliveB; aliveL_p = &aliveA;
                ME_p = &Mb; ML_p = &Ma;
                earlierLast = lastB; laterFirst = firstA;
            } else {
                // Intervals overlap but co-alive count is below minWinOverlap.
                // Pick the cluster that starts earlier as "earlier".
                if (firstA <= firstB) {
                    cE = ca; cL = cb;
                    aliveE_p = &aliveA; aliveL_p = &aliveB;
                    ME_p = &Ma; ML_p = &Mb;
                    earlierLast = lastA; laterFirst = firstB;
                } else {
                    cE = cb; cL = ca;
                    aliveE_p = &aliveB; aliveL_p = &aliveA;
                    ME_p = &Mb; ML_p = &Ma;
                    earlierLast = lastB; laterFirst = firstA;
                }
            }

            // Gap test: skip pairs too far apart in time.
            const int gap = laterFirst - earlierLast;
            if (gap > maxGap) continue;

            // Boundary window neighbourhoods: last K alive of earlier,
            // first K alive of later.
            const auto& aliveE = *aliveE_p;
            const auto& aliveL = *aliveL_p;
            const auto& ME     = *ME_p;
            const auto& ML     = *ML_p;
            const int Ka = std::min(K, (int)aliveE.size());
            const int Kb = std::min(K, (int)aliveL.size());

            // Compute mean residual + COM distance over all Ka·Kb pairings.
            double sumD = 0.0, sumCOM = 0.0;
            int    nPairs = 0;
            std::vector<int> shifts;
            shifts.reserve(Ka * Kb);
            for (int u = 0; u < Ka; ++u) {
                const int we = aliveE[(size_t)((int)aliveE.size() - Ka + u)];
                for (int v = 0; v < Kb; ++v) {
                    const int wl = aliveL[(size_t)v];
                    double D; int tau;
                    pairwiseResidualOneWindow(ME[(size_t)we], ML[(size_t)wl],
                                              p.nChan, p.nSamp, p.maxShift,
                                              D, tau);
                    if (!std::isfinite(D)) continue;
                    sumD += D;
                    shifts.push_back(tau);
                    ++nPairs;
                    if (com != nullptr) {
                        const auto& xE = com->x.at(cE);
                        const auto& yE = com->y.at(cE);
                        const auto& xL = com->x.at(cL);
                        const auto& yL = com->y.at(cL);
                        const double dx = xE[(size_t)we] - xL[(size_t)wl];
                        const double dy = yE[(size_t)we] - yL[(size_t)wl];
                        sumCOM += std::sqrt(dx * dx + dy * dy);
                    }
                }
            }
            if (nPairs == 0) continue;
            const double meanD   = sumD   / nPairs;
            const double meanCOM = (com != nullptr) ? sumCOM / nPairs : -1.0;

            // Position filter (same as Phase C).
            if (com != nullptr && p.positionThresh > 0.0 && meanCOM > p.positionThresh)
                continue;
            if (meanD >= p.mergeThresh) continue;

            // Per-cluster session-wide drift magnitude (same as Phase C).
            double driftE = 0.0, driftL = 0.0;
            if (com != nullptr) {
                const auto& xE = com->x.at(cE);
                const auto& yE = com->y.at(cE);
                const auto& xL = com->x.at(cL);
                const auto& yL = com->y.at(cL);
                {
                    const double dx = xE[(size_t)aliveE.back()] - xE[(size_t)aliveE.front()];
                    const double dy = yE[(size_t)aliveE.back()] - yE[(size_t)aliveE.front()];
                    driftE = std::sqrt(dx * dx + dy * dy);
                }
                {
                    const double dx = xL[(size_t)aliveL.back()] - xL[(size_t)aliveL.front()];
                    const double dy = yL[(size_t)aliveL.back()] - yL[(size_t)aliveL.front()];
                    driftL = std::sqrt(dx * dx + dy * dy);
                }
            }

            MergePair mp;
            mp.a               = std::min(cE, cL);
            mp.b               = std::max(cE, cL);
            mp.D               = meanD;
            mp.nWinsUsed       = nPairs;       // boundary-pair count, not co-alive
            mp.bestShifts      = std::move(shifts);
            mp.meanAbsGainDev  = 0.0;          // gain-fit not applied in Phase C2
            mp.meanCOMDistance = meanCOM;
            mp.comDriftA       = (mp.a == cE) ? driftE : driftL;
            mp.comDriftB       = (mp.a == cE) ? driftL : driftE;
            additional.push_back(std::move(mp));
        }
    }
    std::sort(additional.begin(), additional.end(),
              [](const MergePair& x, const MergePair& y) { return x.D < y.D; });
    return additional;
}

// ─── Phase D: apply merges, write outputs ───────────────────────────────────

// Resolve transitive merges via union-find on the candidate list (smallest
// ID always wins so cluster IDs stay close to their original layout).
//
// patch59 — chain-size cap.  If maxChain > 0, reject any candidate that
// would push either root's accumulated group size past maxChain original
// clusters.  Guards against runaway chains caused by loose --merge-thresh
// or by one pathological cluster matching many partners.
//
// On return, *outRejectedByCap receives the number of candidates that were
// rejected by the cap (for verbose / YAML diagnostics).  Pass nullptr to
// ignore.
std::unordered_map<int32_t, int32_t>
resolveMerges(const std::vector<MergePair>& cands,
              const std::vector<int32_t>&   clusters,
              int                           maxChain,
              int*                          outRejectedByCap = nullptr)
{
    std::unordered_map<int32_t, int32_t> parent;
    std::unordered_map<int32_t, int32_t> groupSize;     // size of each root's group
    for (int32_t c : clusters) {
        parent[c] = c;
        groupSize[c] = 1;
    }
    std::function<int32_t(int32_t)> findRoot = [&](int32_t x) -> int32_t {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    int rejected = 0;
    for (const auto& mp : cands) {
        const int32_t ra = findRoot(mp.a);
        const int32_t rb = findRoot(mp.b);
        if (ra == rb) continue;
        // Cap check: would merging exceed maxChain on either side?
        if (maxChain > 0 && (groupSize[ra] + groupSize[rb]) > maxChain) {
            ++rejected;
            continue;
        }
        const int32_t keep = std::min(ra, rb);
        const int32_t drop = std::max(ra, rb);
        parent[drop]      = keep;
        groupSize[keep]  += groupSize[drop];
        groupSize.erase(drop);   // tidiness; drop's slot won't be queried again
    }
    if (outRejectedByCap) *outRejectedByCap = rejected;

    std::unordered_map<int32_t, int32_t> out;
    for (int32_t c : clusters) out[c] = findRoot(c);
    return out;
}

// Write the YAML report: window definitions, per-cluster counts per window,
// applied merges + their per-window τ histories, untouched clusters.
bool writeReport(const Params& p,
                 const std::vector<std::pair<int64_t,int64_t>>& wins,
                 const PerClusterTemplates& tpl,
                 const ClusterCOMTrajectory& com,
                 const std::vector<MergePair>& cands,
                 const std::unordered_map<int32_t,int32_t>& merges,
                 const std::vector<std::pair<double, double>>& sharedDriftD,
                 const std::string& path)
{
    // patch55 — write to a sibling tempfile, then atomic rename onto
    // the destination.  Same rationale as writeCluFile: never leave a
    // partial YAML report on disk if anything fails mid-write.
    const std::string tmpPath = path + ".tmp." + std::to_string((long)getpid());
    std::ofstream f(tmpPath);
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot create tempfile %s: %s\n",
                     tmpPath.c_str(), std::strerror(errno));
        return false;
    }
    f << "# process_drifttracker report\n";
    f << "session: " << p.sessionBase << "\n";
    f << "group: "   << p.grp         << "\n";
    f << "geometry:\n";
    f << "  n_chan: "         << p.nChan        << "\n";
    f << "  n_samp: "         << p.nSamp        << "\n";
    f << "  sampling_rate: "  << p.samplingRate << "\n";
    f << "params:\n";
    f << "  window_min: "     << p.windowMin     << "\n";
    f << "  overlap_min: "    << p.overlapMin    << "\n";
    f << "  min_win_spikes: " << p.minWinSpikes  << "\n";
    f << "  lambda: "         << p.lambda        << "\n";
    f << "  merge_thresh: "   << p.mergeThresh   << "\n";
    f << "  max_shift: "      << p.maxShift      << "\n";
    f << "  min_win_overlap: " << p.minWinOverlap << "\n";
    f << "  max_merge_chain: " << p.maxMergeChain << "\n";
    f << "  cross_temporal: "  << (p.crossTemporal ? "true" : "false") << "\n";
    if (p.crossTemporal) {
        f << "  cross_temporal_max_gap: "       << p.crossTemporalMaxGap << "\n";
        f << "  cross_temporal_neighborhood: "  << p.crossTemporalNeighborhood << "\n";
    }
    f << "  gain_fit: "        << p.gainFit       << "\n";
    if (p.gainFit != 0) {
        f << "  gain_smooth_lambda: " << p.gainSmoothLambda << "\n";
        f << "  gain_unity_prior: "   << p.gainUnityPrior   << "\n";
    }
    f << "  position_thresh: "        << p.positionThresh       << "\n";
    f << "  position_smooth_lambda: " << p.positionSmoothLambda << "\n";
    f << "  shared_drift: "            << (p.sharedDrift ? "true" : "false") << "\n";
    if (p.sharedDrift) {
        f << "  shared_drift_min_clusters: " << p.sharedDriftMinClusters << "\n";
        f << "  shared_drift_prior: "        << p.sharedDriftPrior       << "\n";
        f << "  shared_drift_iter: "         << p.sharedDriftIter        << "\n";
        if (p.sharedDriftIter > 1) {
            f << "  shared_drift_huber_k: "  << p.sharedDriftHuberK      << "\n";
            f << "  shared_drift_tol: "      << p.sharedDriftTol         << "\n";
        }
    }
    f << "  channel_positions_file: \""
      << (p.channelPositionsFile.empty() ? "(channel index)" : p.channelPositionsFile)
      << "\"\n";
    {
        // patch51: emit the units label, exactly as it appears in the banner
        const std::string posUnitsDefault =
            p.channelPositionsFile.empty() ? "channels" : "μm";
        const std::string& posUnits =
            p.positionUnits.empty() ? posUnitsDefault : p.positionUnits;
        f << "  position_units: \"" << posUnits << "\"\n";
    }

    f << "windows:\n";
    for (size_t i = 0; i < wins.size(); ++i) {
        const double t0 = wins[i].first  / p.samplingRate;
        const double t1 = wins[i].second / p.samplingRate;
        f << "  - { i: " << i << ", t0_sec: " << t0
          << ", t1_sec: " << t1 << " }\n";
    }

    // ── Shared population drift trajectory (patch57; empty when off) ──
    if (!sharedDriftD.empty()) {
        f << "shared_drift:  # per-window probe drift D(w) subtracted from COM\n";
        for (size_t w = 0; w < sharedDriftD.size(); ++w) {
            f << "  - { w: " << w
              << ", dx: " << sharedDriftD[w].first
              << ", dy: " << sharedDriftD[w].second << " }\n";
        }
    }

    f << "cluster_counts:  # per-window member count, per cluster\n";
    std::vector<int32_t> sortedClus;
    for (const auto& kv : tpl.count) sortedClus.push_back(kv.first);
    std::sort(sortedClus.begin(), sortedClus.end());
    for (int32_t c : sortedClus) {
        f << "  " << c << ": [";
        const auto& counts = tpl.count.at(c);
        for (size_t i = 0; i < counts.size(); ++i)
            f << (i ? ", " : "") << counts[i];
        f << "]\n";
    }

    // Per-cluster COM trajectories (smoothed).  Two arrays per cluster:
    // {x: [...], y: [...]} indexed by window.  Units = μm if --channel-
    // positions loaded, else channel index.
    f << "com_trajectories:  # smoothed 2D position over time, per cluster\n";
    for (int32_t c : sortedClus) {
        auto itX = com.x.find(c);
        auto itY = com.y.find(c);
        if (itX == com.x.end() || itY == com.y.end()) continue;
        f << "  " << c << ":\n    x: [";
        for (size_t i = 0; i < itX->second.size(); ++i)
            f << (i ? ", " : "") << itX->second[i];
        f << "]\n    y: [";
        for (size_t i = 0; i < itY->second.size(); ++i)
            f << (i ? ", " : "") << itY->second[i];
        f << "]\n";
    }

    f << "merges_applied:  # smaller cluster ID wins\n";
    int nMerged = 0;
    for (const auto& mp : cands) {
        if (merges.at(mp.a) == merges.at(mp.b)) {
            f << "  - { keep: " << std::min(mp.a, mp.b)
              << ", drop: "     << std::max(mp.a, mp.b)
              << ", D: "        << mp.D
              << ", n_wins: "   << mp.nWinsUsed;
            if (mp.meanAbsGainDev > 0.0)
                f << ", mean_abs_gain_dev: " << mp.meanAbsGainDev;
            if (mp.meanCOMDistance >= 0.0) {
                f << ", mean_com_distance: " << mp.meanCOMDistance
                  << ", com_drift_a: "       << mp.comDriftA
                  << ", com_drift_b: "       << mp.comDriftB;
            }
            f << ", taus: [";
            for (size_t i = 0; i < mp.bestShifts.size(); ++i)
                f << (i ? ", " : "") << mp.bestShifts[i];
            f << "] }\n";
            ++nMerged;
        }
    }
    f << "merges_applied_count: " << nMerged << "\n";

    f << "merges_rejected_candidates:  # pairs where union-find collapsed them away\n";
    for (const auto& mp : cands) {
        if (merges.at(mp.a) != merges.at(mp.b)) {
            f << "  - { a: " << mp.a << ", b: " << mp.b
              << ", D: "     << mp.D
              << ", n_wins: " << mp.nWinsUsed;
            if (mp.meanCOMDistance >= 0.0)
                f << ", mean_com_distance: " << mp.meanCOMDistance;
            f << " }\n";
        }
    }
    f.close();
    if (!f) {
        std::fprintf(stderr, "ERROR: close failed on %s: %s\n",
                     tmpPath.c_str(), std::strerror(errno));
        ::unlink(tmpPath.c_str());
        return false;
    }
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::fprintf(stderr, "ERROR: rename %s → %s failed: %s\n",
                     tmpPath.c_str(), path.c_str(), std::strerror(errno));
        ::unlink(tmpPath.c_str());
        return false;
    }
    return true;
}

} // namespace

// ─── main ───────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    Params p;
    if (!parseArgs(argc, argv, p)) return 2;

    // YAML auto-fill: populate any geometry fields the user didn't pass
    // explicitly on the CLI.  CLI values (non-zero in p after parseArgs)
    // take precedence over YAML.  If neither CLI nor YAML supplies a
    // required field, validateGeometry() fails below.
    fillFromYaml(p);
    if (!validateGeometry(p)) return 2;

    // Build file paths.
    auto path = [&](const std::string& ext) {
        return p.sessionBase + "." + ext + "." + std::to_string(p.grp);
    };
    const std::string resPath = path("res");
    const std::string cluPath = path("clu");

    // Resolve the spike-file path with .spk → .spkD fallback (canonical
    // first, matching KiloKlustaKwik's pickInputPath convention).  Some
    // pipelines (process_extractspikes_stderiv) emit .spkD only; others
    // emit .spk only; either is valid input.
    //
    // If the user passed --spk-suffix explicitly, honour it exactly (no
    // fallback) — that's the override path.  Otherwise (default suffix
    // "spk"), try .spk first, then .spkD, then error.
    std::string spkPath = path(p.spkSuffix);
    std::string resolvedSpkSuffix = p.spkSuffix;
    {
        auto fileExists = [](const std::string& q) -> bool {
            std::FILE* f = std::fopen(q.c_str(), "rb");
            if (f) { std::fclose(f); return true; }
            return false;
        };
        if (!fileExists(spkPath) && p.spkSuffix == "spk") {
            const std::string altPath = path("spkD");
            if (fileExists(altPath)) {
                if (p.verbose) {
                    std::fprintf(stderr,
                        "[drifttracker]   spike file: %s not found; "
                        "using stderiv variant %s\n",
                        spkPath.c_str(), altPath.c_str());
                }
                spkPath           = altPath;
                resolvedSpkSuffix = "spkD";
            }
        }
    }

    const std::string outClu  = cluPath + "." + p.outCluTag;
    const std::string outYaml = p.sessionBase + ".drift." +
                                std::to_string(p.grp) + ".yaml";

    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker] session=%s grp=%d  nChan=%d nSamp=%d Hz=%.0f\n"
            "[drifttracker]   window=%.2fmin overlap=%.2fmin "
            "lambda=%.3f thresh=%.4f maxShift=±%d  spk=.%s\n",
            p.sessionBase.c_str(), p.grp,
            p.nChan, p.nSamp, p.samplingRate,
            p.windowMin, p.overlapMin, p.lambda, p.mergeThresh, p.maxShift,
            resolvedSpkSuffix.c_str());
    }

    // ── Read inputs ────────────────────────────────────────────────────────
    std::vector<int64_t> res;
    if (!readResFile(resPath, res)) return 1;

    int cluHeader = 0;
    std::vector<int32_t> clu;
    if (!readCluFile(cluPath, res.size(), clu, cluHeader)) return 1;

    std::FILE* spkFile = openSpkFile(spkPath);
    if (!spkFile) return 1;

    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   inputs: %zu spikes, clu-header=%d, spk OK\n",
            res.size(), cluHeader);
    }
    if (res.empty()) {
        std::fprintf(stderr, "[drifttracker] empty session; nothing to do\n");
        std::fclose(spkFile);
        return 0;
    }

    // ── Build windows ──────────────────────────────────────────────────────
    const int64_t sessionLen = res.back() + 1;
    const int64_t wSamples = static_cast<int64_t>(p.windowMin   * 60.0 * p.samplingRate);
    const int64_t oSamples = static_cast<int64_t>(p.overlapMin  * 60.0 * p.samplingRate);
    const int64_t stride   = wSamples - oSamples;
    auto wins = buildWindows(sessionLen, wSamples, stride);
    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   session length %.1f min  →  %zu windows of %.2f min "
            "(stride %.2f min)\n",
            (double)sessionLen / p.samplingRate / 60.0,
            wins.size(),
            (double)wSamples / p.samplingRate / 60.0,
            (double)stride   / p.samplingRate / 60.0);
    }
    if (wins.empty()) {
        std::fprintf(stderr, "[drifttracker] no windows produced; check window-min\n");
        std::fclose(spkFile);
        return 1;
    }

    // ── Phase A: per-window means ──────────────────────────────────────────
    PerClusterTemplates tpl;
    if (!computeWindowedTemplates(p, res, clu, wins, spkFile, tpl)) {
        std::fclose(spkFile);
        return 1;
    }
    std::fclose(spkFile);

    // Apply min-win-spikes gate by zeroing counts (templates remain in place
    // but α=0 in the smoother makes them interpolated rather than trusted).
    int nDefinedTotal = 0, nWindowsTotal = 0;
    for (auto& kv : tpl.count) {
        for (int& cnt : kv.second) {
            ++nWindowsTotal;
            if (cnt < p.minWinSpikes) cnt = 0;
            else                      ++nDefinedTotal;
        }
    }
    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   Phase A: %zu clusters × %zu wins, "
            "%d defined / %d total cells (%.1f%%)\n",
            tpl.count.size(), wins.size(),
            nDefinedTotal, nWindowsTotal,
            100.0 * nDefinedTotal / std::max(1, nWindowsTotal));
    }

    // ── Phase B.5a: COMs from RAW templates (must precede Phase B) ────────
    // Phase B's per-cluster template smoothing (λ ≥ 1) attenuates per-window
    // template differences and bleeds the per-window position signal across
    // adjacent windows.  For accurate drift recovery — both per-cluster
    // (Phase B.5) and shared-population (Phase B.6) — compute COMs from
    // RAW (un-smoothed) templates first.  Per-cluster jitter is denoised
    // later by `smoothCOMTrajectoriesTikhonov` on the COM trajectories
    // themselves (operating on a 1D scalar per axis is far less
    // information-destructive than smoothing a full (nChan × nSamp) template).
    std::vector<double> chPosX, chPosY;
    if (!loadOrDefaultChannelPositions(p.channelPositionsFile, p.nChan, chPosX, chPosY))
        return 1;
    ClusterCOMTrajectory com;
    computeCOMTrajectories(tpl, chPosX, chPosY, p.nChan, p.nSamp, com);

    // ── Phase B: Tikhonov smoothing of templates (for Phase C shape match) ─
    smoothTemplatesTikhonov(p.lambda, tpl);
    if (p.verbose)
        std::fprintf(stderr, "[drifttracker]   Phase B: Tikhonov smoothing applied (λ=%.3f)\n",
                     p.lambda);

    // ── Phase B.6: shared population-level drift (patch57) ────────────────
    // When enabled, estimate a probe-wide drift D(w) from clusters with
    // pairwise window overlap, and subtract it from every cluster's COM
    // trajectory.  This collapses temporally-disjoint clusters that
    // represent the same drifting unit onto a consistent absolute
    // position, letting Phase C find them as merge candidates.
    //
    // ORDER MATTERS: B.6 must run on RAW (unsmoothed) COMs because the
    // per-cluster Tikhonov smoother (lambda ≥ 1) suppresses fast drift
    // signal before B.6 can see it.  We estimate + subtract drift first,
    // THEN smooth the residual — the smoother now only denoises per-unit
    // jitter, not the population-level motion.
    std::vector<std::pair<double, double>> sharedDriftD;  // empty unless run
    if (p.sharedDrift) {
        const int nWin = (int)com.x.begin()->second.size();
        SharedDriftResult sr = estimateSharedDrift(com, nWin, p);
        if (!sr.solveOk) {
            std::fprintf(stderr,
                "[drifttracker]   Phase B.6: Cholesky factorisation failed — "
                "graph too degenerate; --shared-drift disabled for this run\n");
        } else if (sr.nEdges == 0) {
            if (p.verbose) std::fprintf(stderr,
                "[drifttracker]   Phase B.6: no window pairs share "
                "≥ %d clusters; --shared-drift had nothing to estimate\n",
                p.sharedDriftMinClusters);
        } else {
            sharedDriftD = sr.D;
            subtractSharedDrift(com, sharedDriftD);
            if (p.verbose) {
                const std::string posUnitsDefault =
                    p.channelPositionsFile.empty() ? "ch" : "μm";
                const std::string& posUnits =
                    p.positionUnits.empty() ? posUnitsDefault : p.positionUnits;
                double maxMag = 0.0;
                int    maxW   = 0;
                for (int w = 0; w < (int)sharedDriftD.size(); ++w) {
                    const double dx = sharedDriftD[(size_t)w].first;
                    const double dy = sharedDriftD[(size_t)w].second;
                    const double m  = std::sqrt(dx * dx + dy * dy);
                    if (m > maxMag) { maxMag = m; maxW = w; }
                }
                std::fprintf(stderr,
                    "[drifttracker]   Phase B.6: shared drift from "
                    "%d window-pair edge(s) (≥ %d shared clusters), "
                    "max |D(w)| = %.2f %s @ w=%d, %d disconnected window(s)\n",
                    sr.nEdges, p.sharedDriftMinClusters, maxMag,
                    posUnits.c_str(), maxW, sr.nDisconnected);
                if (p.sharedDriftIter > 1) {
                    std::fprintf(stderr,
                        "[drifttracker]   Phase B.6: IRLS converged in "
                        "%d iter(s), final max |ΔD| = %.4f %s, "
                        "%d outlier cluster(s) (weight < 1)\n",
                        sr.nIters, sr.lastMaxDelta, posUnits.c_str(),
                        sr.nOutliers);
                }
            }
        }
    }

    smoothCOMTrajectoriesTikhonov(p.positionSmoothLambda, com);
    if (p.verbose) {
        // patch51: user override > auto-detect from positions file presence
        const std::string posUnitsDefault =
            p.channelPositionsFile.empty() ? "channels" : "μm";
        const std::string& posUnits =
            p.positionUnits.empty() ? posUnitsDefault : p.positionUnits;
        // Per-cluster session-wide COM drift magnitude (one number per
        // cluster), useful as the headline diagnostic.
        std::vector<int32_t> sortedClus;
        for (const auto& kv : com.x) sortedClus.push_back(kv.first);
        std::sort(sortedClus.begin(), sortedClus.end());
        std::fprintf(stderr,
            "[drifttracker]   Phase B.5: COM trajectories computed (%s; "
            "%zu clusters tracked)\n",
            posUnits.c_str(), sortedClus.size());
        for (int32_t c : sortedClus) {
            const auto& dV = com.defined.at(c);
            int first = -1, last = -1;
            for (int w = 0; w < (int)dV.size(); ++w) {
                if (dV[w]) { if (first < 0) first = w; last = w; }
            }
            if (first < 0 || last <= first) continue;
            const double dx = com.x.at(c)[last] - com.x.at(c)[first];
            const double dy = com.y.at(c)[last] - com.y.at(c)[first];
            const double mag = std::sqrt(dx * dx + dy * dy);
            std::fprintf(stderr,
                "[drifttracker]     cluster %d  COM drift = %.2f %s "
                "(w%d → w%d)\n",
                c, mag, posUnits.c_str(), first, last);
        }
    }

    // ── Phase C: drift-aware subtraction merge ────────────────────────────
    auto cands = driftAwareSubtractionMerge(p, tpl, &com);
    if (p.verbose) {
        // patch51: user override > short auto label
        const std::string posUnitsDefault =
            p.channelPositionsFile.empty() ? "ch" : "μm";
        const std::string& posUnits =
            p.positionUnits.empty() ? posUnitsDefault : p.positionUnits;
        std::fprintf(stderr,
            "[drifttracker]   Phase C: %zu candidate pair(s) under D=%.4f, "
            "cyclic ±%d%s%s\n",
            cands.size(), p.mergeThresh, p.maxShift,
            p.gainFit != 0 ? ", gain-fit ON" : "",
            p.positionThresh > 0.0 ? ", position-filter ON" : "");
        for (const auto& mp : cands) {
            std::fprintf(stderr,
                "[drifttracker]     candidate %d ↔ %d  D=%.4f  over %d co-alive windows",
                mp.a, mp.b, mp.D, mp.nWinsUsed);
            if (p.gainFit != 0)
                std::fprintf(stderr, "  mean|gain−1|=%.3f", mp.meanAbsGainDev);
            if (mp.meanCOMDistance >= 0.0)
                std::fprintf(stderr,
                    "  meanCOM=%.2f%s  driftA=%.2f  driftB=%.2f",
                    mp.meanCOMDistance, posUnits.c_str(), mp.comDriftA, mp.comDriftB);
            std::fprintf(stderr, "\n");
        }
    }

    // ── Phase C2: cross-temporal pairing (patch60) ────────────────────────
    // Find merges between clusters whose alive intervals DON'T overlap
    // enough for Phase C.  Operates on drift-corrected COMs.  Appends to
    // the candidate list, which then goes through Phase D's union-find
    // (with chain cap) together.
    auto extraCands = crossTemporalMerge(p, tpl, &com, cands);
    if (p.crossTemporal && p.verbose) {
        const std::string posUnitsDefault =
            p.channelPositionsFile.empty() ? "ch" : "μm";
        const std::string& posUnits =
            p.positionUnits.empty() ? posUnitsDefault : p.positionUnits;
        std::fprintf(stderr,
            "[drifttracker]   Phase C2: %zu cross-temporal candidate pair(s) "
            "under D=%.4f (max-gap=%d, neighbourhood=±%d)\n",
            extraCands.size(), p.mergeThresh,
            p.crossTemporalMaxGap, p.crossTemporalNeighborhood);
        for (const auto& mp : extraCands) {
            std::fprintf(stderr,
                "[drifttracker]     cross-temp %d ↔ %d  D=%.4f  over %d boundary pairs",
                mp.a, mp.b, mp.D, mp.nWinsUsed);
            if (mp.meanCOMDistance >= 0.0)
                std::fprintf(stderr,
                    "  meanCOM=%.2f%s  driftA=%.2f  driftB=%.2f",
                    mp.meanCOMDistance, posUnits.c_str(), mp.comDriftA, mp.comDriftB);
            std::fprintf(stderr, "\n");
        }
    }
    // Append Phase C2 candidates AFTER Phase C's — D-sorted order within
    // Phase C still holds, and Phase D processes Phase C first then C2
    // (preserving the priority of well-supported co-alive matches).
    cands.insert(cands.end(),
                 std::make_move_iterator(extraCands.begin()),
                 std::make_move_iterator(extraCands.end()));

    // ── Phase D: apply merges, write outputs ──────────────────────────────
    std::vector<int32_t> clusters;
    for (const auto& kv : tpl.mean) clusters.push_back(kv.first);
    std::sort(clusters.begin(), clusters.end());

    int nRejectedByCap = 0;
    auto rootMap = resolveMerges(cands, clusters, p.maxMergeChain, &nRejectedByCap);

    int nDistinctMerges = 0;
    for (const auto& kv : rootMap) if (kv.first != kv.second) ++nDistinctMerges;

    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   Phase D: %d cluster(s) merged into peers\n",
            nDistinctMerges);
        if (p.maxMergeChain > 0 && nRejectedByCap > 0) {
            std::fprintf(stderr,
                "[drifttracker]   Phase D: %d candidate(s) REJECTED by "
                "--max-merge-chain=%d (chain would have exceeded cap)\n",
                nRejectedByCap, p.maxMergeChain);
        }
    }

    // Rewrite cluster IDs in the working clu copy.
    std::vector<int32_t> mergedClu = clu;
    for (int32_t& cid : mergedClu) {
        auto it = rootMap.find(cid);
        if (it != rootMap.end()) cid = it->second;
    }

    if (p.dryRun) {
        std::fprintf(stderr, "[drifttracker] --dry-run: outputs not written\n");
        return 0;
    }

    if (!writeCluFile(outClu, mergedClu)) return 1;
    if (!writeReport(p, wins, tpl, com, cands, rootMap, sharedDriftD, outYaml)) return 1;

    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   wrote %s\n"
            "[drifttracker]   wrote %s\n",
            outClu.c_str(), outYaml.c_str());
    }
    return 0;
}
