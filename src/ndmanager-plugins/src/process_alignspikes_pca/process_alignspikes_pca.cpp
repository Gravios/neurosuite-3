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
#include <fstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

// ─── Small helpers ─────────────────────────────────────────────────────────

#include <neurosuite/core/custody.hpp>       // shared chain-of-custody resolver
#include <neurosuite/core/pca_projection.hpp> // shared PcaBasis/loadPca/pcaProjectionEnergy

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

// ─── File I/O ──────────────────────────────────────────────────────────────

// Read full .res.N (N × int64).
static bool readRes(const std::string& path, std::vector<int64_t>& ts) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (bytes % 8 != 0) { std::fclose(f); return false; }
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
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    if (bytes < 4 || (bytes - 4) % 4 != 0) { std::fclose(f); return false; }
    ids.resize(static_cast<size_t>((bytes - 4) / 4));
    std::fseek(f, 4, SEEK_SET);
    const size_t n = std::fread(ids.data(), 4, ids.size(), f);
    std::fclose(f);
    return n == ids.size();
}

// Read one spike waveform from .spk.N at given 0-based index.  Layout on
// disk is sample-major: [sample][channel] per spike → transpose into
// channel-major [channel][sample] for algorithm use.
static bool readSpkSampleMajor(std::FILE* f, int64_t spikeIdx, int nChan,
                                int nSamp, int16_t* outChannelMajor) {
    const int64_t bytesPerSpike = static_cast<int64_t>(nChan) * nSamp * 2;
    if (std::fseek(f, spikeIdx * bytesPerSpike, SEEK_SET) != 0) return false;
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
    if (std::fseek(f, spikeIdx * bytesPerSpike, SEEK_SET) != 0) return false;
    std::vector<int16_t> buf(static_cast<size_t>(nChan * nSamp));
    for (int s = 0; s < nSamp; ++s)
        for (int c = 0; c < nChan; ++c)
            buf[s * nChan + c] = channelMajor[c * nSamp + s];
    return std::fwrite(buf.data(), 2, buf.size(), f) == buf.size();
}

// Read raw .fil window for a single spike.  Extracts only the group's
// channels and stores channel-major.  Applies stderiv transform if requested.
static bool readFilWindow(std::FILE* fil, int64_t startSample,
                            int nTotalChan, int nSamp,
                            const std::vector<int>& groupChannels,
                            bool stderivTransform,
                            int16_t* outChannelMajor)
{
    const int nChan = static_cast<int>(groupChannels.size());
    const int64_t off = startSample * nTotalChan * 2;
    if (std::fseek(fil, off, SEEK_SET) != 0) return false;
    std::vector<int16_t> frame(static_cast<size_t>(nSamp * nTotalChan));
    if (std::fread(frame.data(), 2, frame.size(), fil) != frame.size())
        return false;
    // Channel subset, sample-major → channel-major
    for (int t = 0; t < nSamp; ++t)
        for (int c = 0; c < nChan; ++c)
            outChannelMajor[c * nSamp + t] =
                frame[t * nTotalChan + groupChannels[c]];
    if (stderivTransform) {
        std::vector<int16_t> sdPrev(static_cast<size_t>(nChan), 0);
        for (int t = 0; t < nSamp; ++t) {
            int64_t sum = 0;
            for (int c = 0; c < nChan; ++c)
                sum += outChannelMajor[c * nSamp + t];
            for (int c = 0; c < nChan; ++c) {
                const int v = outChannelMajor[c * nSamp + t];
                const int sd = nChan * v - static_cast<int>(sum);
                const int16_t sdCl = static_cast<int16_t>(
                    std::max(-32768, std::min(32767, sd)));
                const int diff = static_cast<int>(sdCl)
                               - static_cast<int>(sdPrev[c]);
                sdPrev[c] = sdCl;
                outChannelMajor[c * nSamp + t] = static_cast<int16_t>(
                    std::max(-32768, std::min(32767, diff)));
            }
        }
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
    // literal comparison reported it as raw.  readFilWindow applies SDIFF_ALLPAIRS,
    // so a custom pattern or a non-allpairs order cannot be reproduced here; refuse
    // rather than realign against a transform the waveforms never had.
    {
        const neurosuite::custody::MethodSpec ms =
            neurosuite::custody::parseMethodToken(a.method);
        a.stderivMode = (ms.family == "stderiv");
        if (a.stderivMode && ms.kind == 'C')
            die("method " + a.method + " applies a custom sdiffPairs pattern; this "
                "aligner reads .fil with SDIFF_ALLPAIRS only and cannot reproduce it");
        if (a.stderivMode && ms.kind == 'S' && ms.order != 3)
            die("method " + a.method + " is spatial order " + std::to_string(ms.order)
                + "; this aligner reads .fil with SDIFF_ALLPAIRS (order 3) only");
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
    const std::string resPath = neurosuite::custody::resolve(a.basename, "res", a.groupId, a.method).path;
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
    std::fseek(filFp, 0, SEEK_END);
    const int64_t filBytes = std::ftell(filFp);
    const int64_t totalSamples = filBytes / (a.nTotalChan * 2);

    // Open .spk for read/write
    std::FILE* spkFp = std::fopen(spkPath.c_str(),
                                   a.dryRun ? "rb" : "r+b");
    if (!spkFp) die("cannot open " + spkPath);

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
                    // the new position to avoid wraparound artifacts.
                    const int newCum = cumShift[i] + lag;
                    const int64_t newStart = resTs[i] + newCum - peakSamp0;
                    if (newStart < 0 ||
                        newStart + a.nSamp > totalSamples) continue;
                    // Cannot share filFp across threads — guard with
                    // critical for now.  (Per-thread FILE handles are
                    // the obvious next optimisation.)
                    bool ok;
                    #pragma omp critical(filRead)
                    {
                        ok = readFilWindow(filFp, newStart, a.nTotalChan,
                                            a.nSamp, a.groupChannels,
                                            a.stderivMode,
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
                    if (newStart < 0 ||
                        newStart + a.nSamp > totalSamples) continue;
                    if (!readFilWindow(filFp, newStart, a.nTotalChan,
                                        a.nSamp, a.groupChannels,
                                        a.stderivMode, per.data())) continue;
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
                    if (newStart < 0 ||
                        newStart + a.nSamp > totalSamples) continue;
                    if (readFilWindow(filFp, newStart, a.nTotalChan,
                                       a.nSamp, a.groupChannels,
                                       a.stderivMode,
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
                writeSpkSampleMajor(spkFp, i, nChan, a.nSamp,
                                     wavBuf.data() + k * spkElems);
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
    std::fclose(spkFp);
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
        // Write .res
        std::FILE* rf = std::fopen(resPath.c_str(), "wb");
        if (!rf) die("cannot write " + resPath);
        std::fwrite(resOut.data(), 8, resOut.size(), rf);
        std::fclose(rf);
        // Write .clu  (header is count of distinct cluster IDs)
        std::vector<int32_t> uniq(cluOut.begin(), cluOut.end());
        std::sort(uniq.begin(), uniq.end());
        uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
        std::FILE* cf = std::fopen(cluPath.c_str(), "wb");
        if (!cf) die("cannot write " + cluPath);
        const int32_t nDistinct = static_cast<int32_t>(uniq.size());
        std::fwrite(&nDistinct, 4, 1, cf);
        std::fwrite(cluOut.data(), 4, cluOut.size(), cf);
        std::fclose(cf);
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
                if (std::fseek(sfIn, perm[k] * bytesPerSpike, SEEK_SET) != 0)
                    die("spk seek failed during resort");
                if (std::fread(buf.data(), 1, buf.size(), sfIn) != buf.size())
                    die("spk read failed during resort");
                if (std::fwrite(buf.data(), 1, buf.size(), sfOut) != buf.size())
                    die("spk write failed during resort");
            }
            std::fclose(sfIn);
            std::fclose(sfOut);
            std::rename((spkPath + ".tmp").c_str(), spkPath.c_str());
        }
        std::printf("  wrote: %s, %s%s\n", resPath.c_str(), cluPath.c_str(),
                    needsResort ? " (and resorted .spk)" : "");
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
