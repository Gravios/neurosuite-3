/*****************************************************************************
 * process_alignspikes_pca.cpp
 *
 * Two-stage per-cluster spike realignment using PCA projection energy.
 *
 * ALGORITHM
 * ─────────
 * For each cluster ID >= 2 in .clu.N:
 *
 *   STAGE 1 — Per-spike alignment to cluster mean (xcorr template method)
 *   ─────────────────────────────────────────────────────────────────────
 *   a. Load the cluster's waveforms from .spk.N (or .spkD.N if --stderiv).
 *   b. Compute the cluster's mean waveform (channel-major int16).
 *   c. For each spike, find the optimal lag δ_i ∈ [−maxShift, +maxShift]
 *      maximizing the normalised cross-correlation between the spike
 *      and the mean template.  Sub-sample shifts of size |δ_i| > 0 with
 *      xcorr score ≥ minScore are applied: spike timestamp updated to
 *      ts + δ_i, waveform re-extracted from raw .fil at the new
 *      position.  Iterate (default 2 times) — after each pass the
 *      template is recomputed from the updated waveforms.
 *
 *   STAGE 2 — Global cluster shift via PCA-projection energy (this is the
 *             new bit beyond Klusters' xcorr realign)
 *   ─────────────────────────────────────────────────────────────────────
 *   d. Recompute the cluster mean from the Stage 1 outputs.
 *   e. For each candidate global shift s ∈ [−maxShiftGlobal, +maxShiftGlobal],
 *      compute the PCA-projection energy of the (shifted) mean waveform
 *      against the precomputed .pca.N / .pcaD.N basis:
 *
 *          E(s) = Σ_ch Σ_k ⟨ϕ_ch,k, μ_shifted(s) − μ̄_ch⟩²
 *
 *      where {ϕ_ch,k} are the PCA eigenvectors per channel and μ̄_ch is
 *      the per-channel basis-centre.  Higher E(s) ⇒ the mean lies more
 *      strongly along the cluster's principal axes of variance.
 *   f. Pick s* = argmax_s E(s).  Apply s* uniformly to ALL spikes in the
 *      cluster: ts_i ← ts_i + s*, re-extract every waveform.  Unlike
 *      Klusters' patch82 (per-spike PCA refine), here every member moves
 *      by the same amount, so the cluster is RIGIDLY translated to its
 *      PC-energy maximum without dispersing internal variance.
 *
 *   This is the key conceptual difference from Klusters' --pca-refine:
 *   that mode shifts each spike independently and can disperse mixture
 *   clusters; this plugin shifts the WHOLE cluster as a unit and is
 *   conservative on cluster boundaries while still correcting systematic
 *   per-cluster mis-alignments.
 *
 * OUTPUTS
 *   .res.N      sorted, updated timestamps after stage 1 + stage 2 shifts
 *   .spk.N      (or .spkD.N) re-extracted waveforms at new timestamps
 *   .fet.N      (or .fetD.N) re-projected features for every shifted spike
 *   .res.N.preAlignPca           backup of original timestamps
 *   .alignspikes_pca.log.N       per-cluster shift summary CSV
 *
 * USAGE
 *   process_alignspikes_pca [options] basename electrodeGroup
 *
 *   -n nTotalChannels       Total channels in .fil
 *   -c channelList          Comma-separated channel IDs for this group
 *   -w nSamples             Samples per spike waveform
 *   -p peakSampleIndex      Target peak position (1-based)
 *   -m maxShift             Per-spike shift bound, samples [default 3]
 *   -M maxShiftGlobal       Global cluster shift bound [default 4]
 *   -i iterations           Stage-1 iterations [default 2]
 *   -t minScore             xcorr score threshold [default 0.70]
 *   --stderiv               Input/output are .spkD/.pcaD/.fetD
 *   --skip-pca              Skip Stage 2 (Stage-1 xcorr realignment only)
 *   --dry-run               Compute shifts, write log, do NOT modify files
 *   --log path              CSV log path [default <basename>.alignspikes_pca.log.<group>]
 *
 * FILE FORMATS (these match Klusters / KlustaKwik conventions exactly)
 *   .res.N   : N × int64 timestamps (sample indices)
 *   .clu.N   : int32 nClusters; N × int32 cluster IDs
 *   .spk.N   : N × nChan × nSamples × int16 (sample-major:
 *              [spike][sample][channel] when interleaved on disk)
 *   .pca.N   : header [nCh, data2use, nComp, isCentered, recShift] (5×int32);
 *              per channel: data2use×double means, then nComp×data2use×double
 *              eigenvectors (col-major)
 *   .fet.N   : int32 nDimensions; N × nDimensions × int64
 *              (last column is timestamp)
 *
 * This file is intentionally self-contained — no link-time dependency
 * on libklustersshared.  Algorithms are factored to be portable; users
 * who need GPU acceleration can replace alignSpikesToTemplate() with a
 * call into XcorrDispatch::compute() and rebuild against libklustersshared.
 *****************************************************************************/

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

// ─── Small helpers ─────────────────────────────────────────────────────────

#include <neurosuite/core/custody.hpp>        // shared chain-of-custody resolver
#include "sdiff_pairs.h"      // shared SDIFF_CUSTOM / _CAR pattern parser
#include <neurosuite/core/pca_projection.hpp> // shared PcaBasis/loadPca/pcaProjectionEnergy
#include <neurosuite/core/spike_extract.hpp>  // shared window cut + stderiv transform

static void die(const std::string& msg) {
    std::fprintf(stderr, "process_alignspikes_pca: ERROR: %s\n", msg.c_str());
    std::exit(2);
}

static std::vector<int> parseChannelList(const std::string& s) {
    std::vector<int> out;
    std::string tok;
    for (char c : s) {
        if (c == ',' || c == ' ') {
            if (!tok.empty()) { out.push_back(std::stoi(tok)); tok.clear(); }
        } else tok += c;
    }
    if (!tok.empty()) out.push_back(std::stoi(tok));
    return out;
}

// Every other .spk of this group -- <base>.spk.<token>.<group> for any method
// token, and the untagged <base>.spk.<group> -- except `aligned`.  The .res is
// shared by all methods, so once alignment rewrites it these no longer line up
// row for row with it and must be re-extracted.
static std::vector<std::string> otherSpkFiles(const std::string& base, int group,
                                              const std::string& aligned)
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    const fs::path bp(base);
    const fs::path dir = bp.has_parent_path() ? bp.parent_path() : fs::path(".");
    const std::string stem   = bp.filename().string() + ".spk.";
    const std::string suffix = "." + std::to_string(group);
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        bool match = (name == stem + std::to_string(group));            // untagged
        if (!match && name.size() > stem.size() + suffix.size()
            && name.compare(0, stem.size(), stem) == 0
            && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            const std::string tok = name.substr(stem.size(),
                                                name.size() - stem.size() - suffix.size());
            match = (tok.find('.') == std::string::npos);                // <token>.<group>
        }
        if (!match) continue;
        std::error_code eq;
        if (fs::equivalent(it->path(), fs::path(aligned), eq)) continue;
        out.push_back(bp.has_parent_path() ? it->path().string() : name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ─── File I/O ──────────────────────────────────────────────────────────────

// File size and positioning go through the core's 64-bit helpers: fseek/ftell
// take a long, which is 32 bits on Windows, so a .res past 2 GiB (268M spikes)
// or a large .spk would be misread there.
using neurosuite::core::detail::seek64;
using neurosuite::core::detail::sizeOf;

// Read full .res.N (N × int64).
static bool readRes(const std::string& path, std::vector<int64_t>& ts) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const long long bytes = sizeOf(f);
    if (bytes < 0 || bytes % 8 != 0 || seek64(f, 0) != 0) { std::fclose(f); return false; }
    ts.resize(static_cast<size_t>(bytes / 8));
    const size_t n = std::fread(ts.data(), 8, ts.size(), f);
    std::fclose(f);
    return n == ts.size();
}

// Read .clu.N — returns vector<int32> of cluster IDs.  Header (1 int32) is
// the count of distinct cluster IDs; we discard it but verify file shape.
static bool readClu(const std::string& path, std::vector<int32_t>& ids) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    int32_t header = 0;
    if (std::fread(&header, 4, 1, f) != 1) { std::fclose(f); return false; }
    const long long bytes = sizeOf(f);
    if (bytes < 4 || (bytes - 4) % 4 != 0) { std::fclose(f); return false; }
    ids.resize(static_cast<size_t>((bytes - 4) / 4));
    if (seek64(f, 4) != 0) { std::fclose(f); return false; }
    const size_t n = std::fread(ids.data(), 4, ids.size(), f);
    std::fclose(f);
    return n == ids.size();
}

// Replace `path` with the given blocks: written to <path>.aligntmp with every
// fwrite and the fclose checked, then renamed over the original, so a full
// disk or an interrupted run never leaves a truncated .res / .clu behind.
struct Block { const void* data; size_t bytes; };
static bool writeFileAtomic(const std::string& path, std::initializer_list<Block> blocks) {
    const std::string tmp = path + ".aligntmp";
    std::FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = true;
    for (const Block& b : blocks)
        if (ok && b.bytes > 0) ok = (std::fwrite(b.data, 1, b.bytes, f) == b.bytes);
    ok = (std::fclose(f) == 0) && ok;
    if (ok) ok = (std::rename(tmp.c_str(), path.c_str()) == 0);
    if (!ok) std::remove(tmp.c_str());
    return ok;
}

// Read one spike waveform from .spk.N at given 0-based index.  Layout on
// disk is sample-major: [sample][channel] per spike → transpose into
// channel-major [channel][sample] for algorithm use.
static bool readSpkSampleMajor(std::FILE* f, int64_t spikeIdx, int nChan,
                                int nSamp, int16_t* outChannelMajor) {
    const int64_t bytesPerSpike = static_cast<int64_t>(nChan) * nSamp * 2;
    if (seek64(f, spikeIdx * bytesPerSpike) != 0) return false;
    std::vector<int16_t> buf(static_cast<size_t>(nChan * nSamp));
    if (std::fread(buf.data(), 2, buf.size(), f) != buf.size()) return false;
    // Transpose [s][c] → [c][s]
    for (int s = 0; s < nSamp; ++s)
        for (int c = 0; c < nChan; ++c)
            outChannelMajor[c * nSamp + s] = buf[s * nChan + c];
    return true;
}

// Write one spike waveform back to .spk.N (transposes channel-major →
// sample-major before writing).
static bool writeSpkSampleMajor(std::FILE* f, int64_t spikeIdx, int nChan,
                                 int nSamp, const int16_t* channelMajor) {
    const int64_t bytesPerSpike = static_cast<int64_t>(nChan) * nSamp * 2;
    if (seek64(f, spikeIdx * bytesPerSpike) != 0) return false;
    std::vector<int16_t> buf(static_cast<size_t>(nChan * nSamp));
    for (int s = 0; s < nSamp; ++s)
        for (int c = 0; c < nChan; ++c)
            buf[s * nChan + c] = channelMajor[c * nSamp + s];
    return std::fwrite(buf.data(), 2, buf.size(), f) == buf.size();
}

// Custom spatial pattern (-P), empty unless supplied.  Realignment reconstructs
// waveforms from .fil, so it must apply the SAME spatial operator the .spk was
// built with; all-pairs against a custom-pattern session misaligns in exactly the
// space the spikes are clustered in.
static std::vector<int>              g_customPartner;   // order 4
static std::vector<std::vector<int>> g_customSets;      // order 5

// The spatial operator in the form the shared core transform
// (neurosuite/core/stderiv_transform.hpp) takes.  Chosen exactly as
// process_extractspikes_stderiv's computeSDiff dispatches: reference sets, then
// partner map, then all-pairs -- a pattern that does not fit the group falls
// back to all-pairs there, and so it does here.
struct SpatialOperator {
    neurosuite::core::SdiffOrder order = neurosuite::core::SdiffOrder::AllPairs;
    std::vector<int> partner, setOff, setMem;
};

static SpatialOperator makeSpatialOperator(int nChan)
{
    SpatialOperator op;
    if (nChan > 1 && (int)g_customSets.size() == nChan) {
        op.order = neurosuite::core::SdiffOrder::CustomCar;
        op.setOff.push_back(0);
        for (const std::vector<int>& set : g_customSets) {
            op.setMem.insert(op.setMem.end(), set.begin(), set.end());
            op.setOff.push_back((int)op.setMem.size());
        }
    } else if (nChan > 1 && (int)g_customPartner.size() == nChan) {
        op.order   = neurosuite::core::SdiffOrder::Custom;
        op.partner = g_customPartner;
    }
    return op;
}

// Read the raw .fil window of one spike starting at startSample, keep the
// group's channels (channel-major out), and -- with spec.applyTransform --
// apply the stderiv transform with the extractor's exact arithmetic (shared
// core: spatial derivative rounded and clamped, temporal first difference
// seeded per spec.seedFromPrevious with the sample before the window).  This is
// the per-spike form of neurosuite::core::extractSpikeWindows, so a re-cut
// spike is byte-identical to what ndm_extractspikes writes at that time.
static bool readFilWindow(std::FILE* fil, int64_t startSample,
                          const neurosuite::core::SpikeWindowSpec& spec,
                          int16_t* outChannelMajor)
{
    const int nChan = spec.nChan, nSamp = spec.nSamp, nTotalChan = spec.totalNbChannels;
    const int64_t lead =
        (spec.applyTransform && spec.seedFromPrevious && startSample >= 1) ? 1 : 0;
    const long long off = static_cast<long long>(startSample - lead)
                        * nTotalChan * static_cast<long long>(sizeof(int16_t));
    if (seek64(fil, off) != 0) return false;
    std::vector<int16_t> frame(static_cast<size_t>((nSamp + lead) * nTotalChan));
    if (std::fread(frame.data(), 2, frame.size(), fil) != frame.size())
        return false;
    const int16_t* win = frame.data() + static_cast<size_t>(lead * nTotalChan);
    // Channel subset, sample-major -> channel-major
    for (int t = 0; t < nSamp; ++t)
        for (int c = 0; c < nChan; ++c)
            outChannelMajor[c * nSamp + t] =
                win[t * nTotalChan + spec.groupChannels[c]];
    if (spec.applyTransform) {
        std::vector<int16_t> seed;
        if (lead) {
            std::vector<int16_t> leadRow(static_cast<size_t>(nChan));
            for (int c = 0; c < nChan; ++c)
                leadRow[static_cast<size_t>(c)] = frame[static_cast<size_t>(spec.groupChannels[c])];
            seed.resize(static_cast<size_t>(nChan));
            neurosuite::core::stderivSeed(spec.order, leadRow.data(), nChan, seed.data(),
                                          spec.partner, spec.setOff, spec.setMem);
        }
        neurosuite::core::applyStderivTransform(spec.order, outChannelMajor, nChan, nSamp,
                                                outChannelMajor, spec.partner,
                                                seed.empty() ? nullptr : seed.data(),
                                                spec.setOff, spec.setMem);
    }
    return true;
}

// PCA basis loader.  File format (matches Klusters/KlustaKwik exactly):
// PcaBasis, loadPca and pcaProjectionEnergy now live in libneurosuite-core
// (header-only): src/libneurosuite-core/src/neurosuite/core/pca_projection.hpp.
// Bring them into scope so the call sites below are unchanged.
using neurosuite::core::PcaBasis;
using neurosuite::core::loadPca;
using neurosuite::core::pcaProjectionEnergy;

// ─── Algorithm core ────────────────────────────────────────────────────────

// Channel-major mean waveform over a set of spikes.
static void computeMean(const std::vector<int16_t>& wavBuf,
                         int64_t nSpikes, int spkElems,
                         std::vector<int16_t>& tmpl)
{
    tmpl.assign(static_cast<size_t>(spkElems), 0);
    std::vector<double> acc(static_cast<size_t>(spkElems), 0.0);
    for (int64_t i = 0; i < nSpikes; ++i)
        for (int e = 0; e < spkElems; ++e)
            acc[e] += wavBuf[static_cast<size_t>(i) * spkElems + e];
    const double inv = nSpikes > 0 ? 1.0 / nSpikes : 0.0;
    for (int e = 0; e < spkElems; ++e)
        tmpl[e] = static_cast<int16_t>(std::lround(acc[e] * inv));
}

// Normalised xcorr at lag τ.  Both inputs channel-major.  No wraparound:
// out-of-bounds samples treated as 0.
// Compute argmax over lags in [-maxShift, +maxShift] and return (bestLag,
// bestScore).
static void xcorrBestLag(const int16_t* spike, const int16_t* tmpl,
                         int nChan, int nSamp, int maxShift,
                         int& outLag, float& outScore)
{
    // Pre-compute template norm
    double tmplNorm = 0.0;
    for (int c = 0; c < nChan; ++c)
        for (int s = 0; s < nSamp; ++s) {
            const double v = tmpl[c * nSamp + s];
            tmplNorm += v * v;
        }
    tmplNorm = std::sqrt(std::max(tmplNorm, 1.0));

    double bestScore = -1.0;
    int bestLag = 0;
    for (int tau = -maxShift; tau <= maxShift; ++tau) {
        double dot = 0.0, spkNorm = 0.0;
        for (int c = 0; c < nChan; ++c) {
            for (int s = 0; s < nSamp; ++s) {
                const int ss = s + tau;
                if (ss < 0 || ss >= nSamp) continue;
                const double v = spike[c * nSamp + ss];
                const double t = tmpl[c * nSamp + s];
                dot += v * t;
                spkNorm += v * v;
            }
        }
        spkNorm = std::sqrt(std::max(spkNorm, 1.0));
        const double score = dot / (spkNorm * tmplNorm);
        if (score > bestScore) {
            bestScore = score;
            bestLag = tau;
        }
    }
    outLag = bestLag;
    outScore = static_cast<float>(bestScore);
}

// pcaProjectionEnergy moved to libneurosuite-core (see the using-declaration
// above).  Stage 2 below calls it on the cluster mean at each candidate global
// shift; the global-mean argmax policy stays local to this plugin.

// ─── Main ──────────────────────────────────────────────────────────────────

struct Args {
    std::string basename;
    int groupId = 0;
    int nTotalChan = 0;
    std::vector<int> groupChannels;
    int nSamp = 32;
    int peakSamp = 16;          // 1-based from CLI; converted to 0-based on use
    int maxShift = 3;
    int maxShiftGlobal = 4;
    int iterations = 2;
    float minScore = 0.70f;
    bool stderivMode = false;       // derived from method below
    std::string method = "standard";  // chain-of-custody method tag
    bool skipPca = false;
    bool dryRun = false;
    bool verbose = false;
    std::string logPath;
};

static void usage() {
    std::fprintf(stderr,
        "Usage: process_alignspikes_pca [opts] basename electrodeGroup\n"
        "  -n nTotalChan       (required)\n"
        "  -c channelList      comma-separated 0-based channel IDs\n"
        "  -w nSamples         samples per spike (default 32)\n"
        "  -p peakSampleIndex  1-based peak sample (default 16)\n"
        "  -m maxShift         per-spike shift bound (default 3)\n"
        "  -M maxShiftGlobal   global cluster shift bound (default 4)\n"
        "  -i iterations       Stage-1 iterations (default 2)\n"
        "  -t minScore         xcorr threshold (default 0.70)\n"
        "  --method M          method token of the files (default standard)\n"
        "  -P PATTERN          the group's sdiffPairs: 'a-b,...' (order 4) or\n"
        "                      'a-b+c,...' (order 5); required for a _C method\n"
        "  --stderiv           input/output are .spkD/.pcaD/.fetD\n"
        "  --skip-pca          Stage-1 only, no PCA refinement\n"
        "  --dry-run           compute shifts, don't modify files\n"
        "  --log path          per-cluster log CSV\n"
        "  -v, --verbose       verbose per-cluster progress reporting\n");
}

static Args parseArgs(int argc, char** argv) {
    Args a;
    int pos = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        auto next = [&](const char* name) -> const char* {
            if (i + 1 >= argc) die(std::string("missing value for ") + name);
            return argv[++i];
        };
        if      (s == "-n") a.nTotalChan = std::atoi(next("-n"));
        else if (s == "-c") a.groupChannels = parseChannelList(next("-c"));
        else if (s == "-w") a.nSamp = std::atoi(next("-w"));
        else if (s == "-p") a.peakSamp = std::atoi(next("-p"));
        else if (s == "-m") a.maxShift = std::atoi(next("-m"));
        else if (s == "-M") a.maxShiftGlobal = std::atoi(next("-M"));
        else if (s == "-i") a.iterations = std::atoi(next("-i"));
        else if (s == "-t") a.minScore = static_cast<float>(std::atof(next("-t")));
        else if (s == "--method") a.method = next("--method");
        else if (s == "-P") {
            // Custom spatial pattern for this group; same grammar as the
            // extractor's -P ("a-b,..." order 4, "a-b+c,..." order 5).
            const std::string pat = next("-P");
            if (sdiffSpecUsesSets(pat.c_str())) g_customSets    = parseSdiffSets(pat.c_str());
            else                                g_customPartner = parseSdiffPairs(pat.c_str());
        }
        else if (s == "--stderiv") a.method = "stderiv";  // deprecated alias
        else if (s == "--skip-pca") a.skipPca = true;
        else if (s == "--dry-run") a.dryRun = true;
        else if (s == "--log")  a.logPath = next("--log");
        else if (s == "-v" || s == "--verbose") a.verbose = true;
        else if (s == "-h" || s == "--help") { usage(); std::exit(0); }
        else if (s[0] == '-')   die("unknown option " + s);
        else if (pos == 0) { a.basename = s; ++pos; }
        else if (pos == 1) { a.groupId = std::atoi(argv[i]); ++pos; }
        else die("unexpected positional argument " + s);
    }
    if (a.basename.empty() || pos < 2) { usage(); std::exit(2); }
    if (a.nTotalChan <= 0)              die("-n nTotalChan required");
    if (a.groupChannels.empty())         die("-c channelList required");
    if (a.peakSamp < 1 || a.peakSamp > a.nSamp)
        die("peakSampleIndex out of range");
    // Family test, not equality -- stderiv_S3 is still the stderiv family, and the
    // literal comparison reported it as raw.  readFilWindow applies all-pairs or
    // the group's -P pattern; first-difference and Laplacian cannot be reproduced
    // here, so refuse rather than realign against a transform the waveforms
    // never had.  A _C method needs its -P (ndm_alignspikes_pca passes the
    // group's sdiffPairs); a bare stderiv token applies -P when one is given.
    {
        const neurosuite::custody::MethodSpec ms =
            neurosuite::custody::parseMethodToken(a.method);
        a.stderivMode = (ms.family == "stderiv");
        if (a.stderivMode && ms.kind == 'C'
            && g_customPartner.empty() && g_customSets.empty())
            die("method " + a.method + " applies a custom sdiffPairs pattern but no -P "
                "was given; .fil would be read with all-pairs and the waveforms "
                "would be misaligned in the space they are clustered in");
        if (a.stderivMode && ms.kind == 'C') {
            const int impliedOrder = g_customSets.empty() ? 4 : 5;
            if (ms.order != impliedOrder)
                die("method " + a.method + " states order " + std::to_string(ms.order)
                    + " but the -P pattern implies order " + std::to_string(impliedOrder));
        }
        if (a.stderivMode && ms.kind == 'S' && ms.order != 3)
            die("method " + a.method + " is spatial order " + std::to_string(ms.order)
                + "; this aligner reproduces all-pairs (order 3) or a -P pattern only");
    }
    return a;
}

int main(int argc, char** argv) {
    Args a = parseArgs(argc, argv);
    const int peakSamp0 = a.peakSamp - 1;
    const int nChan     = static_cast<int>(a.groupChannels.size());
    const int spkElems  = nChan * a.nSamp;

    // File paths (chain-of-custody: <base>.<type>.<method>.<grp>).
    const std::string grpStr = std::to_string(a.groupId);
    const std::string spkPath = neurosuite::custody::resolve(a.basename, "spk", a.groupId, a.method).path;
    // Spike times are method-independent: ONE .res per group serves every method,
    // and detection may have run under a different token than this alignment
    // (detect at stderiv, cluster+align at stderiv_C5).  resolve() only walks
    // method -> standard -> untagged and would miss it; resolveAny scans every
    // method-tagged copy, exactly as process_alignspikes does.
    const std::string resPath = neurosuite::custody::resolveAny(a.basename, "res", a.groupId, a.method).path;
    const std::string cluPath = neurosuite::custody::methodPath(a.basename, "clu", a.method, a.groupId);
    const std::string pcaPath = neurosuite::custody::methodPath(a.basename, "pca", a.method, a.groupId);
    const std::string fetPath = neurosuite::custody::methodPath(a.basename, "fet", a.method, a.groupId);
    const std::string filPath_dat = a.basename + ".dat";
    const std::string filPath_fil = a.basename + ".fil";
    const std::string filPath = (std::ifstream(filPath_fil).good()
                                 ? filPath_fil : filPath_dat);
    const std::string logPath = a.logPath.empty()
                                ? a.basename + ".alignspikes_pca.log." + grpStr
                                : a.logPath;
    const std::string resBackup = resPath + ".preAlignPca";

    std::printf("process_alignspikes_pca: %s group %d\n",
                a.basename.c_str(), a.groupId);
    std::printf("  files: %s, %s, %s, %s, %s\n",
                resPath.c_str(), cluPath.c_str(), spkPath.c_str(),
                pcaPath.c_str(), fetPath.c_str());
    std::printf("  raw:   %s\n", filPath.c_str());

    // Read .clu, .res
    std::vector<int32_t> clu;
    if (!readClu(cluPath, clu)) die("cannot read " + cluPath);
    std::vector<int64_t> resTs;
    if (!readRes(resPath, resTs)) die("cannot read " + resPath);
    if (clu.size() != resTs.size())
        die("clu/res size mismatch: " + std::to_string(clu.size())
            + " vs " + std::to_string(resTs.size()));
    const int64_t nSpikes = static_cast<int64_t>(clu.size());
    std::printf("  %lld spikes\n", static_cast<long long>(nSpikes));

    // Load PCA basis
    PcaBasis pca;
    bool pcaOk = loadPca(pcaPath, pca);
    if (a.skipPca) std::printf("  --skip-pca: Stage-2 disabled\n");
    else if (!pcaOk) std::printf("  WARNING: cannot load %s — "
                                  "Stage-2 will be skipped\n", pcaPath.c_str());
    else std::printf("  PCA basis: %d ch × %d comp, data2use=%d, "
                      "recShift=%d, centered=%d\n",
                      pca.nCh, pca.nComp, pca.data2use, pca.recShift,
                      static_cast<int>(pca.centered));

    // Backup original .res
    if (!a.dryRun) {
        std::ifstream src(resPath, std::ios::binary);
        std::ofstream dst(resBackup, std::ios::binary | std::ios::trunc);
        dst << src.rdbuf();
        std::printf("  backup: %s\n", resBackup.c_str());
    }

    // Open .fil for raw re-extraction (only needed when writing changes)
    std::FILE* filFp = std::fopen(filPath.c_str(), "rb");
    if (!filFp) die("cannot open raw file " + filPath);
    const int64_t filBytes = sizeOf(filFp);
    const int64_t totalSamples = filBytes / (a.nTotalChan * 2);

    // Open .spk for read/write
    std::FILE* spkFp = std::fopen(spkPath.c_str(),
                                   a.dryRun ? "rb" : "r+b");
    if (!spkFp) die("cannot open " + spkPath);

    // The .spk must hold one row per spike time.  A short .spk is what
    // ndm_extractspikes (-R) leaves when spike windows start before sample 1 or
    // run past the end of the .fil: it writes no row for those, and the
    // remaining rows can no longer be matched to the .res.
    {
        const long long rowBytes = static_cast<long long>(spkElems) * 2;
        const long long spkBytes = sizeOf(spkFp);
        if (spkBytes != nSpikes * rowBytes) {
            std::string why = spkPath + " holds " + std::to_string(spkBytes) + " bytes, expected "
                + std::to_string(nSpikes * rowBytes) + " (" + std::to_string(nSpikes)
                + " spikes x " + std::to_string(a.nSamp) + " samples x "
                + std::to_string(nChan) + " channels x 2 bytes)";
            if (spkBytes >= 0 && spkBytes < nSpikes * rowBytes && spkBytes % rowBytes == 0)
                why += "; it has " + std::to_string(spkBytes / rowBytes) + " rows for "
                    + std::to_string(nSpikes) + " spike times.  ndm_extractspikes (-R) "
                    "writes no row for a spike whose window starts before sample 1 or "
                    "runs past the end of the .fil.  Remove those spike times from "
                    + resPath + " (and the .clu) or re-detect, and re-extract";
            die(why);
        }
    }

    // How a spike is re-cut from .fil: window geometry and transform exactly as
    // the extractor applies them (see readFilWindow).
    const SpatialOperator op = makeSpatialOperator(nChan);
    neurosuite::core::SpikeWindowSpec cut;
    cut.nSamp           = a.nSamp;
    cut.peakSample      = peakSamp0;
    cut.totalNbChannels = a.nTotalChan;
    cut.groupChannels   = a.groupChannels.data();
    cut.nChan           = nChan;
    cut.applyTransform  = a.stderivMode;
    cut.order           = op.order;
    cut.partner         = op.partner.empty() ? nullptr : op.partner.data();
    cut.setOff          = op.setOff.empty()  ? nullptr : op.setOff.data();
    cut.setMem          = op.setMem.empty()  ? nullptr : op.setMem.data();

    // A stderiv .spk exists in two conventions that differ only in sample 0 of
    // each window: seeded with the spatial derivative of the sample before the
    // window (ndm_extractspikes -R today) or with 0 (the detection pass and
    // older extractors).  Stage 1 averages stored and re-cut waveforms into one
    // template, and unshifted spikes keep the file's form, so every re-cut
    // spike must follow the file's convention.
    if (a.stderivMode) {
        int64_t cachedIdx = -1;
        std::vector<int16_t> row0(static_cast<size_t>(nChan), 0);
        const long long bytesPerSpike = static_cast<long long>(spkElems) * 2;
        const neurosuite::core::StderivSeeding conv =
            neurosuite::core::detectStderivSeeding(
                filFp, nSpikes, cut,
                [&](int64_t i) { return resTs[static_cast<size_t>(i)]; },
                [&](int64_t i, int ci) -> int16_t {
                    if (i != cachedIdx) {
                        cachedIdx = i;
                        if (seek64(spkFp, i * bytesPerSpike) != 0
                            || std::fread(row0.data(), 2, row0.size(), spkFp) != row0.size())
                            die("spk read failed for spike " + std::to_string(i));
                    }
                    return row0[static_cast<size_t>(ci)];   // sample 0, channel ci
                });
        cut.seedFromPrevious = (conv != neurosuite::core::StderivSeeding::Zero);
        std::printf("  stderiv .spk seed convention: %s\n",
                    conv == neurosuite::core::StderivSeeding::Previous
                        ? "previous sample"
                    : conv == neurosuite::core::StderivSeeding::Zero
                        ? "zero (older extractor)"
                        : "undetermined, using previous sample (ndm_extractspikes)");
    }

    // Cumulative per-spike shift (initially 0).  shifts[i] is the TOTAL
    // sample shift applied to spike i across all stages.
    std::vector<int> cumShift(static_cast<size_t>(nSpikes), 0);
    std::vector<float> finalScore(static_cast<size_t>(nSpikes), 0.0f);

    // Open log
    std::FILE* logFp = std::fopen(logPath.c_str(), "w");
    if (!logFp) die("cannot open log " + logPath);
    std::fprintf(logFp,
        "cluster,nSpikes,n_stage1_shifted,median_score,"
        "stage2_global_shift,stage2_pca_gain_pct,"
        "n_total_shifted,frac_total_shifted\n");

    // Per-cluster bucket
    std::vector<std::vector<int64_t>> bucket;
    {
        int32_t maxId = 0;
        for (auto c : clu) maxId = std::max(maxId, c);
        bucket.assign(static_cast<size_t>(maxId + 1), {});
        for (int64_t i = 0; i < nSpikes; ++i)
            bucket[static_cast<size_t>(clu[i])].push_back(i);
    }

    // Scratch buffers
    std::vector<int16_t> wavBuf, tmpl, scratch(static_cast<size_t>(spkElems));

    // ─── Per-cluster loop ──────────────────────────────────────────────
    int64_t totalShifted = 0;
    int nClustersProcessed = 0;

    for (size_t cid = 2; cid < bucket.size(); ++cid) {
        const auto& idxs = bucket[cid];
        const int64_t N = static_cast<int64_t>(idxs.size());
        if (N < 5) continue;                 // not enough spikes for a template

        // Load all cluster waveforms (channel-major).  Read from .spk
        // at the spike's current location — for the first iteration this
        // is the on-disk waveform; subsequent iterations get refreshed
        // from .fil after a shift is applied.
        wavBuf.assign(static_cast<size_t>(N) * spkElems, 0);
        for (int64_t k = 0; k < N; ++k) {
            const int64_t i = idxs[static_cast<size_t>(k)];
            if (!readSpkSampleMajor(spkFp, i, nChan, a.nSamp,
                                     wavBuf.data() + k * spkElems))
                die("spk read failed for spike "
                    + std::to_string(idxs[static_cast<size_t>(k)]));
        }

        // ─── STAGE 1: per-spike xcorr alignment to template ────────────
        int nStage1Shifted = 0;
        std::vector<float> scores(static_cast<size_t>(N), 0.0f);
        for (int iter = 0; iter < a.iterations; ++iter) {
            computeMean(wavBuf, N, spkElems, tmpl);
            int shiftedThisIter = 0;
            #pragma omp parallel for reduction(+:shiftedThisIter)              \
                schedule(static)
            for (int64_t k = 0; k < N; ++k) {
                const int64_t i = idxs[static_cast<size_t>(k)];
                int lag = 0; float score = 0.0f;
                xcorrBestLag(wavBuf.data() + k * spkElems,
                              tmpl.data(), nChan, a.nSamp, a.maxShift,
                              lag, score);
                scores[k] = score;
                if (lag != 0 && score >= a.minScore) {
                    // Tentative shift; re-extract waveform from .fil at
                    // the new position to avoid wraparound artifacts.  A
                    // window that would start before sample 1 or run past
                    // the end is not taken: ndm_extractspikes (-R) writes no
                    // row for it (it needs the sample before the window), so
                    // the aligned .res could not be re-extracted row for row.
                    const int newCum = cumShift[i] + lag;
                    const int64_t newStart = resTs[i] + newCum - peakSamp0;
                    if (newStart < 1 ||
                        newStart + a.nSamp > totalSamples) continue;
                    // Cannot share filFp across threads — guard with
                    // critical for now.  (Per-thread FILE handles are
                    // the obvious next optimisation.)
                    bool ok;
                    #pragma omp critical(filRead)
                    {
                        ok = readFilWindow(filFp, newStart, cut,
                                            wavBuf.data() + k * spkElems);
                    }
                    if (!ok) continue;
                    cumShift[i] = newCum;
                    finalScore[i] = score;
                    ++shiftedThisIter;
                }
            }
            nStage1Shifted += shiftedThisIter;
            if (shiftedThisIter == 0) break;  // converged
        }

        // ─── STAGE 2: global cluster shift via PCA energy ──────────────
        int globalShift = 0;
        double pcaBefore = 0.0, pcaAfter = 0.0;
        if (!a.skipPca && pcaOk) {
            // Compute mean from current wavBuf (already updated by Stage 1)
            computeMean(wavBuf, N, spkElems, tmpl);
            pcaBefore = pcaProjectionEnergy(tmpl.data(), nChan, a.nSamp, pca);
            // Evaluate mean at each candidate global shift.  For each
            // shift s, re-extract every cluster spike's mean from .fil
            // — but we don't want to do that for every s (expensive).
            // Approximation: compute the mean RAW waveform at each
            // candidate shift via a single representative spike, then
            // project.  More accurate alternative: compute the mean over
            // ALL spikes per shift, which is O(N × (2M+1)) re-extractions.
            //
            // We do the accurate version: re-extract every spike at each
            // candidate shift, average to a mean, project.  For typical
            // cluster sizes (50-2000 spikes) and maxShiftGlobal ≤ 4,
            // this is N × 9 = ~5k–18k frame reads per cluster, fast
            // enough on SSD.
            double bestEnergy = pcaBefore;
            std::vector<int16_t> meanShifted(spkElems);
            std::vector<int16_t> per(spkElems);
            for (int s = -a.maxShiftGlobal; s <= a.maxShiftGlobal; ++s) {
                if (s == 0) continue;
                std::vector<double> accD(spkElems, 0.0);
                int validN = 0;
                for (int64_t k = 0; k < N; ++k) {
                    const int64_t i = idxs[static_cast<size_t>(k)];
                    const int newCum = cumShift[i] + s;
                    const int64_t newStart = resTs[i] + newCum - peakSamp0;
                    if (newStart < 1 ||
                        newStart + a.nSamp > totalSamples) continue;
                    if (!readFilWindow(filFp, newStart, cut, per.data())) continue;
                    for (int e = 0; e < spkElems; ++e) accD[e] += per[e];
                    ++validN;
                }
                if (validN < N / 2) continue;       // too many edge clamps
                for (int e = 0; e < spkElems; ++e)
                    meanShifted[e] = static_cast<int16_t>(
                        std::lround(accD[e] / validN));
                const double energy = pcaProjectionEnergy(
                    meanShifted.data(), nChan, a.nSamp, pca);
                if (energy > bestEnergy) {
                    bestEnergy = energy;
                    globalShift = s;
                }
            }
            pcaAfter = bestEnergy;
            if (globalShift != 0) {
                // Apply global shift uniformly to every cluster spike.
                // Re-extract waveforms at new positions and store back
                // into wavBuf for the final disk-write below.
                for (int64_t k = 0; k < N; ++k) {
                    const int64_t i = idxs[static_cast<size_t>(k)];
                    const int newCum = cumShift[i] + globalShift;
                    const int64_t newStart = resTs[i] + newCum - peakSamp0;
                    if (newStart < 1 ||
                        newStart + a.nSamp > totalSamples) continue;
                    if (readFilWindow(filFp, newStart, cut,
                                       wavBuf.data() + k * spkElems))
                        cumShift[i] = newCum;
                }
            }
        }

        // ─── Write updated waveforms back to .spk ──────────────────────
        if (!a.dryRun) {
            for (int64_t k = 0; k < N; ++k) {
                const int64_t i = idxs[static_cast<size_t>(k)];
                if (cumShift[i] == 0) continue;
                if (!writeSpkSampleMajor(spkFp, i, nChan, a.nSamp,
                                         wavBuf.data() + k * spkElems))
                    die("spk write failed for spike " + std::to_string(i));
            }
        }

        // Update timestamps for moved spikes
        int nMovedThisCluster = 0;
        for (int64_t k = 0; k < N; ++k) {
            const int64_t i = idxs[static_cast<size_t>(k)];
            if (cumShift[i] != 0) ++nMovedThisCluster;
        }
        totalShifted += nMovedThisCluster;

        // Median score (for the log)
        std::vector<float> sCopy(scores);
        std::nth_element(sCopy.begin(),
                         sCopy.begin() + sCopy.size() / 2, sCopy.end());
        const float medScore = sCopy[sCopy.size() / 2];

        const double pcaGain = (pcaBefore > 0)
            ? (pcaAfter / pcaBefore - 1.0) * 100.0 : 0.0;
        std::fprintf(logFp,
            "%zu,%lld,%d,%.3f,%d,%.2f,%d,%.2f\n",
            cid, static_cast<long long>(N), nStage1Shifted, medScore,
            globalShift, pcaGain, nMovedThisCluster,
            static_cast<double>(nMovedThisCluster) / N);

        ++nClustersProcessed;
        if (a.verbose ||
            (nClustersProcessed % 50 == 0) ||
            (nMovedThisCluster > 0 && globalShift != 0))
            std::printf("  [%zu] N=%lld stage1=%d glob=%+d gain=%+.1f%% "
                        "moved=%d/%lld\n",
                        cid, static_cast<long long>(N), nStage1Shifted,
                        globalShift, pcaGain, nMovedThisCluster,
                        static_cast<long long>(N));
    }

    std::fclose(filFp);
    if (std::fclose(spkFp) != 0 && !a.dryRun)
        die("write error on " + spkPath);
    std::fclose(logFp);

    std::printf("  processed %d clusters, %lld spikes shifted total\n",
                nClustersProcessed, static_cast<long long>(totalShifted));

    // ─── Write updated .res, sort by timestamp ─────────────────────────
    if (!a.dryRun) {
        // Apply cumShift to timestamps
        for (int64_t i = 0; i < nSpikes; ++i)
            resTs[i] += cumShift[i];
        // Sort by ts.  We need to keep clu, res in sync.  .spk is also
        // sorted to match (the spike at index k in the new order should
        // have its waveform at .spk's k-th slot).
        std::vector<int64_t> perm(static_cast<size_t>(nSpikes));
        for (int64_t i = 0; i < nSpikes; ++i) perm[i] = i;
        std::sort(perm.begin(), perm.end(),
                  [&](int64_t a, int64_t b) { return resTs[a] < resTs[b]; });
        bool needsResort = false;
        for (int64_t i = 1; i < nSpikes; ++i)
            if (perm[i] != i) { needsResort = true; break; }
        std::vector<int64_t> resOut(static_cast<size_t>(nSpikes));
        std::vector<int32_t> cluOut(static_cast<size_t>(nSpikes));
        for (int64_t i = 0; i < nSpikes; ++i) {
            resOut[i] = resTs[perm[i]];
            cluOut[i] = clu[perm[i]];
        }
        // Write .res (temp file + rename, every write checked)
        if (!writeFileAtomic(resPath, {{resOut.data(), resOut.size() * 8}}))
            die("cannot write " + resPath);
        // Write .clu  (header is count of distinct cluster IDs)
        std::vector<int32_t> uniq(cluOut.begin(), cluOut.end());
        std::sort(uniq.begin(), uniq.end());
        uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
        const int32_t nDistinct = static_cast<int32_t>(uniq.size());
        if (!writeFileAtomic(cluPath, {{&nDistinct, 4},
                                       {cluOut.data(), cluOut.size() * 4}}))
            die("cannot write " + cluPath);
        if (needsResort) {
            // Reorder .spk on disk: open original, write to .spk.tmp
            // sorted by perm, then replace.  Large I/O cost — but it
            // happens at most once.
            std::printf("  resorting .spk by timestamp...\n");
            std::FILE* sfIn  = std::fopen(spkPath.c_str(), "rb");
            std::FILE* sfOut = std::fopen((spkPath + ".tmp").c_str(), "wb");
            if (!sfIn || !sfOut) die("spk resort open failed");
            const int64_t bytesPerSpike =
                static_cast<int64_t>(nChan) * a.nSamp * 2;
            std::vector<char> buf(static_cast<size_t>(bytesPerSpike));
            for (int64_t k = 0; k < nSpikes; ++k) {
                if (seek64(sfIn, perm[k] * bytesPerSpike) != 0)
                    die("spk seek failed during resort");
                if (std::fread(buf.data(), 1, buf.size(), sfIn) != buf.size())
                    die("spk read failed during resort");
                if (std::fwrite(buf.data(), 1, buf.size(), sfOut) != buf.size())
                    die("spk write failed during resort");
            }
            std::fclose(sfIn);
            if (std::fclose(sfOut) != 0)
                die("spk write failed during resort");
            if (std::rename((spkPath + ".tmp").c_str(), spkPath.c_str()) != 0)
                die("cannot replace " + spkPath + " with the resorted copy");
        }
        std::printf("  wrote: %s, %s%s\n", resPath.c_str(), cluPath.c_str(),
                    needsResort ? " (and resorted .spk)" : "");
        if (totalShifted > 0) {
            const std::vector<std::string> stale = otherSpkFiles(a.basename, a.groupId, spkPath);
            if (!stale.empty()) {
                std::string list;
                for (const std::string& f : stale) list += (list.empty() ? "" : ", ") + f;
                std::fprintf(stderr, "process_alignspikes_pca: WARNING: the shared %s was "
                                     "rewritten; these .spk no longer line up with it, "
                                     "re-extract them: %s\n",
                             resPath.c_str(), list.c_str());
            }
        }
    }

    std::printf("Done.  Log: %s\n", logPath.c_str());

    if (!a.skipPca && !pcaOk)
        std::fprintf(stderr,
            "NOTE: PCA basis was not loaded; .fet re-projection skipped.\n"
            "      Run ndm_pca after this to regenerate features.\n");
    else if (!a.dryRun)
        std::fprintf(stderr,
            "NOTE: .fet/.fetD is now STALE for shifted spikes.\n"
            "      Re-run process_refeaturize (or process_refeaturize_stderiv)\n"
            "      to regenerate features from the realigned waveforms.\n");
    return 0;
}
