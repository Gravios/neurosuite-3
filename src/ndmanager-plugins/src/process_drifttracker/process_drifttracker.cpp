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
 * Conceptual map to KlustaKwikExp's Phase 6b (mean-subtraction merge):
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
bool writeCluFile(const std::string& path, const std::vector<int32_t>& ids)
{
    std::set<int32_t> uniq(ids.begin(), ids.end());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot write %s (errno=%d)\n",
                     path.c_str(), errno);
        return false;
    }
    int32_t header = static_cast<int32_t>(uniq.size());
    if (std::fwrite(&header, sizeof(int32_t), 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    if (std::fwrite(ids.data(), sizeof(int32_t), ids.size(), f) != ids.size()) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
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
    const int wElems = static_cast<int>(tpl.mean.begin()->second.front().size());

    std::vector<double> diag(nWin), rhs(nWin), x(nWin);

    for (auto& kv : tpl.mean) {
        const int32_t c = kv.first;
        auto&         M = kv.second;        // [nWin][wElems]
        const auto&   A = tpl.count.at(c);

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

    // Solve nChan independent tridiagonal systems.
    std::vector<double> diag(nWin), rhs(nWin), x(nWin);
    for (int ch = 0; ch < nChan; ++ch) {
        for (int w = 0; w < nWin; ++w) {
            const double nb = ((w > 0)        ? 1.0 : 0.0)
                            + ((w < nWin - 1) ? 1.0 : 0.0);
            diag[w] = static_cast<double>(alpha[w]) * normA[w * nChan + ch]
                    + nb * mu
                    + lambdaUnity;
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
driftAwareSubtractionMerge(const Params& p, const PerClusterTemplates& tpl)
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
            if (finalD < p.mergeThresh) {
                MergePair mp;
                mp.a              = ca;
                mp.b              = cb;
                mp.D              = finalD;
                mp.nWinsUsed      = nUsed;
                mp.bestShifts     = std::move(shifts);
                mp.meanAbsGainDev = gainDevOut;
                candidates.push_back(std::move(mp));
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const MergePair& x, const MergePair& y) { return x.D < y.D; });
    return candidates;
}

// ─── Phase D: apply merges, write outputs ───────────────────────────────────

// Resolve transitive merges via union-find on the candidate list (smallest
// ID always wins so cluster IDs stay close to their original layout).
std::unordered_map<int32_t, int32_t>
resolveMerges(const std::vector<MergePair>& cands,
              const std::vector<int32_t>&   clusters)
{
    std::unordered_map<int32_t, int32_t> parent;
    for (int32_t c : clusters) parent[c] = c;
    std::function<int32_t(int32_t)> findRoot = [&](int32_t x) -> int32_t {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (const auto& mp : cands) {
        const int32_t ra = findRoot(mp.a);
        const int32_t rb = findRoot(mp.b);
        if (ra == rb) continue;
        const int32_t keep = std::min(ra, rb);
        const int32_t drop = std::max(ra, rb);
        parent[drop] = keep;
    }
    // Materialise: for each cluster, the resolved root.
    std::unordered_map<int32_t, int32_t> out;
    for (int32_t c : clusters) out[c] = findRoot(c);
    return out;
}

// Write the YAML report: window definitions, per-cluster counts per window,
// applied merges + their per-window τ histories, untouched clusters.
bool writeReport(const Params& p,
                 const std::vector<std::pair<int64_t,int64_t>>& wins,
                 const PerClusterTemplates& tpl,
                 const std::vector<MergePair>& cands,
                 const std::unordered_map<int32_t,int32_t>& merges,
                 const std::string& path)
{
    std::ofstream f(path);
    if (!f) {
        std::fprintf(stderr, "ERROR: cannot write %s\n", path.c_str());
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
    f << "  gain_fit: "        << p.gainFit       << "\n";
    if (p.gainFit != 0) {
        f << "  gain_smooth_lambda: " << p.gainSmoothLambda << "\n";
        f << "  gain_unity_prior: "   << p.gainUnityPrior   << "\n";
    }

    f << "windows:\n";
    for (size_t i = 0; i < wins.size(); ++i) {
        const double t0 = wins[i].first  / p.samplingRate;
        const double t1 = wins[i].second / p.samplingRate;
        f << "  - { i: " << i << ", t0_sec: " << t0
          << ", t1_sec: " << t1 << " }\n";
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
              << ", n_wins: " << mp.nWinsUsed << " }\n";
        }
    }
    return f.good();
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
    // first, matching KlustaKwikExp's pickInputPath convention).  Some
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

    // ── Phase B: Tikhonov smoothing ────────────────────────────────────────
    smoothTemplatesTikhonov(p.lambda, tpl);
    if (p.verbose)
        std::fprintf(stderr, "[drifttracker]   Phase B: Tikhonov smoothing applied (λ=%.3f)\n",
                     p.lambda);

    // ── Phase C: drift-aware subtraction merge ────────────────────────────
    auto cands = driftAwareSubtractionMerge(p, tpl);
    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   Phase C: %zu candidate pair(s) under D=%.4f, "
            "cyclic ±%d%s\n",
            cands.size(), p.mergeThresh, p.maxShift,
            p.gainFit != 0 ? ", gain-fit ON" : "");
        for (const auto& mp : cands) {
            if (p.gainFit != 0) {
                std::fprintf(stderr,
                    "[drifttracker]     candidate %d ↔ %d  D=%.4f  over %d co-alive windows  "
                    "mean|gain−1|=%.3f\n",
                    mp.a, mp.b, mp.D, mp.nWinsUsed, mp.meanAbsGainDev);
            } else {
                std::fprintf(stderr,
                    "[drifttracker]     candidate %d ↔ %d  D=%.4f  over %d co-alive windows\n",
                    mp.a, mp.b, mp.D, mp.nWinsUsed);
            }
        }
    }

    // ── Phase D: apply merges, write outputs ──────────────────────────────
    std::vector<int32_t> clusters;
    for (const auto& kv : tpl.mean) clusters.push_back(kv.first);
    std::sort(clusters.begin(), clusters.end());

    auto rootMap = resolveMerges(cands, clusters);

    int nDistinctMerges = 0;
    for (const auto& kv : rootMap) if (kv.first != kv.second) ++nDistinctMerges;

    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   Phase D: %d cluster(s) merged into peers\n",
            nDistinctMerges);
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
    if (!writeReport(p, wins, tpl, cands, rootMap, outYaml)) return 1;

    if (p.verbose) {
        std::fprintf(stderr,
            "[drifttracker]   wrote %s\n"
            "[drifttracker]   wrote %s\n",
            outClu.c_str(), outYaml.c_str());
    }
    return 0;
}
