/***************************************************************************
 * process_alignspikes.cpp
 *
 * Align spike waveform snippets to their true peak before PCA feature
 * extraction, eliminating shift-induced bimodality in feature space.
 *
 * ALGORITHM
 * ─────────
 * For each spike snippet in .spk.N (or .spkD.N):
 *
 *   1. Compute cross-channel energy E(t) = Σ_c |W[t, c]| over the spike
 *      snippet.  Two paths are supported, switched on alignSigma:
 *
 *      (A) alignSigma > 0  — circular cross-correlation against a stationary
 *          Gaussian template centred at peakSampleIndex.  E is computed over
 *          the FULL nSamples window directly from the .spk.N / .spkD.N
 *          snippet (no .fil read for discovery).  argmax_d xcorr(E, g)[d]
 *          IS the signed shift δ; sub-sample refinement uses parabolic
 *          interpolation on the xcorr around its discrete peak.
 *
 *      (B) alignSigma ≤ 0  — bounded argmax over [target − maxShift,
 *          target + maxShift].  For .spkD samples the temporal first-
 *          difference produces an energy zero-crossing at the original
 *          peak with lobes on either side, so this path re-reads each
 *          spike's window from .fil and applies SDIFF_ALLPAIRS only
 *          (no temporal diff), giving a peak-aligned signal.
 *
 *   2. Find foundSample = argmax E(t).
 *      Sub-sample refinement via parabolic interpolation reduces residual
 *      error from ±0.5 to ~±0.1 samples.
 *
 *   3. shift = foundSample − peakSampleIndex
 *
 *   4. If shift ≠ 0: re-read the spike window from the raw .fil at
 *      extTs = originalTs + shift, extract the group's channels, and
 *      (in --stderiv mode) apply SDIFF_ALLPAIRS on the fly.
 *      Array-rolling is intentionally avoided — re-extraction is the
 *      only artifact-free method.
 *
 *   5. Write the updated .spk.N sorted by extTs.  .res.N is also updated
 *      so .res[i] = extTs[i] = originalTs[i] + shift[i] — i.e. .res points
 *      to the file sample where the peak now lands in the corresponding
 *      .spk[i].  Downstream tools (Klusters' nudge, process_refeaturize,
 *      any code that re-reads .fil at the .res offset) require this
 *      invariant; without it, re-extracted windows are mispositioned by
 *      shift[i] samples.  The pre-alignment .res.N is archived as
 *      .res.N.prealign so the original detection timestamps can be
 *      recovered if needed.
 *
 * USAGE
 * ─────
 *   process_alignspikes [options] basename electrodeGroup
 *
 *   -n  nTotalChannels    Total channels in .fil
 *   -c  channelList       Comma-separated channel IDs for this group
 *   -w  nSamples          Samples per spike waveform
 *   -p  peakSampleIndex   Target peak position (1-based, converted to 0-based)
 *   -m  maxShift          Max shift in samples [default: 3]
 *   --stderiv             Input/output is .spkD.N with SDIFF_ALLPAIRS re-extraction
 *   --min-score  f        Minimum relative energy score [0,1] to accept a shift;
 *                         spikes below threshold are left unshifted [default: 0.0]
 *   --top-channels N      Use only the N highest-amplitude channels per spike
 *                         for alignment energy [default: 0 = all]
 *   --align-sigma σ       Std (samples) of a stationary Gaussian template
 *                         circular-xcorr'd against the per-spike energy
 *                         over the FULL nSamples window.  When > 0, this
 *                         replaces the bounded-argmax discovery and
 *                         operates on the existing .spk.N / .spkD.N
 *                         snippet (no .fil read for discovery).  Set ≤ 0
 *                         to disable and use the legacy bounded-argmax
 *                         path.  [default: 0.0]
 *   -v                    Verbose
 *   -h                    Help
 *
 * OUTPUT FILES
 * ────────────
 *   basename.spk.G          overwritten in-place (aligned snippets, reordered by extTs)
 *   basename.res.G          overwritten in-place (.res[i] = extTs[i] after alignment)
 *   basename.res.G.prealign archived copy of pre-alignment .res
 *   basename.fet.G          caller must delete and re-run process_pca
 *   basename.pca.G          caller must delete and re-run process_pca
 *
 * copyright   (C) 2026 Gravios / NeuroSuite-3 contributors
 * GNU GPL v3 or later.
 ***************************************************************************/

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <climits>
#include <stdexcept>
#include <unistd.h>     // access(F_OK) for prealign archive
#include <neurosuite/core/custody.hpp>   // shared chain-of-custody resolver

// ── SDIFF_ALLPAIRS kernel (same as process_extractspikes_stderiv) ─────────

static double sdiff_allpairs(const short* frame,
                              const int*   chanList,
                              int          idx,
                              int          nChanGrp)
{
    if (nChanGrp <= 1) return 0.0;
    double sum = 0.0;
    for (int j = 0; j < nChanGrp; ++j) sum += frame[chanList[j]];
    return (double)nChanGrp * frame[chanList[idx]] - sum;
}

// ── Helpers ───────────────────────────────────────────────────────────────

static void die(const char* msg)
{
    fprintf(stderr, "process_alignspikes: %s\n", msg);
    exit(1);
}

static FILE* xfopen(const char* path, const char* mode)
{
    FILE* f = fopen(path, mode);
    if (!f) {
        fprintf(stderr, "process_alignspikes: cannot open '%s'\n", path);
        exit(1);
    }
    return f;
}

// Parse comma-separated integer list into vector.
static std::vector<int> parseIntList(const char* s)
{
    std::vector<int> v;
    char* buf = strdup(s);
    char* p   = buf;
    char* tok;
    while ((tok = strsep(&p, ",")) != nullptr) {
        if (*tok) v.push_back(atoi(tok));
    }
    free(buf);
    return v;
}

// ── Shift estimation ──────────────────────────────────────────────────────

/** Compute optimal alignment shift for one spike snippet (legacy path).
 *
 *  Bounded argmax of cross-channel energy E(t) = Σ_c |W[t,c]| over the
 *  search window [target − maxShift, target + maxShift], with parabolic
 *  sub-sample refinement.  Used when alignSigma ≤ 0.  For alignSigma > 0
 *  see computeShiftXcorr.
 *
 *  @param wav      Snippet data: layout [sample * nChan + chan], nSamp * nChan shorts.
 *  @param nChan    Channels in snippet (may be derivative channels for spkD).
 *  @param nSamp    Samples per snippet.
 *  @param target   Target peak sample index (0-based).
 *  @param maxShift Maximum allowed shift in either direction.
 *  @param score    Output: quality metric ∈ [0,1].  1 = energy all at target.
 *  @return         Integer shift δ (foundPeak − target).  0 if no improvement.
 */
static int computeShift(const short* wav,
                         int nChan, int nSamp, int target, int maxShift,
                         int nTopChan,
                         double* score)
{
    int lo = std::max(0, target - maxShift);
    int hi = std::min(nSamp - 1, target + maxShift);
    int wSz = hi - lo + 1;

    // ── Select top-K channels by absolute peak amplitude within the
    // search window.  Limiting the energy computation to the dominant
    // channels for this spike excludes collision/residual activity on
    // non-primary channels that would otherwise pull the alignment peak
    // away from the target unit's true peak position.
    //
    // nTopChan <= 0 or >= nChan disables the filter (uses all channels).
    std::vector<uint8_t> chanUse(nChan, 1);
    if (nTopChan > 0 && nTopChan < nChan) {
        std::vector<std::pair<int64_t,int>> chanAmp(nChan);
        for (int c = 0; c < nChan; ++c) {
            int64_t pk = 0;
            for (int t = lo; t <= hi; ++t) {
                const int64_t v = std::abs((int)wav[t * nChan + c]);
                if (v > pk) pk = v;
            }
            chanAmp[c] = {pk, c};
        }
        std::partial_sort(chanAmp.begin(),
                          chanAmp.begin() + nTopChan,
                          chanAmp.end(),
                          [](const auto& a, const auto& b){
                              return a.first > b.first;
                          });
        std::fill(chanUse.begin(), chanUse.end(), 0);
        for (int k = 0; k < nTopChan; ++k)
            chanUse[chanAmp[k].second] = 1;
    }

    // Energy per sample in window (dominant channels only)
    std::vector<int64_t> energy(wSz, 0);
    for (int t = lo; t <= hi; ++t) {
        int64_t e = 0;
        for (int c = 0; c < nChan; ++c)
            if (chanUse[c])
                e += std::abs((int)wav[t * nChan + c]);
        energy[t - lo] = e;
    }

    // Find peak
    int best = 0;
    for (int k = 1; k < wSz; ++k)
        if (energy[k] > energy[best]) best = k;

    // Parabolic sub-sample refinement
    double subPeak = lo + best;
    if (best > 0 && best < wSz - 1) {
        double a0 = (double)energy[best - 1];
        double a1 = (double)energy[best];
        double a2 = (double)energy[best + 1];
        double denom = 2.0 * (2.0*a1 - a0 - a2);
        if (denom > 1e-6) {
            double refined = (lo + best) - (a2 - a0) / denom;
            // Only accept if within bounds
            if (refined >= lo && refined <= hi)
                subPeak = refined;
        }
    }

    int intShift = (int)std::round(subPeak) - target;

    // Clamp to maxShift (parabolic refinement can only widen by rounding)
    if (intShift >  maxShift) intShift =  maxShift;
    if (intShift < -maxShift) intShift = -maxShift;

    // Quality score: fraction of total window energy at the found peak
    // normalised by the maximum possible (all energy at one sample).
    int64_t totalEnergy = 0;
    for (auto e : energy) totalEnergy += e;
    if (score) {
        if (totalEnergy > 0)
            *score = (double)energy[best] / (double)totalEnergy;
        else
            *score = 0.0;
    }

    return intShift;
}

/** Compute optimal alignment shift via circular cross-correlation against a
 *  stationary Gaussian template (alignSigma > 0 path).
 *
 *  Operates on the full nSamples window of the already-loaded spk/spkD
 *  snippet — does NOT re-read from .fil — and so saves the per-spike random
 *  read from disk that the legacy stderiv path needs.  The Gaussian template
 *  g[k] = exp(−(k − target)² / 2σ²) is precomputed once per group; the
 *  per-spike work is one O(nSamp²) circular dot product (≈1600 ops for
 *  nSamp = 40, trivial), one parabolic sub-sample fit, and a clamp to
 *  ±maxShift.
 *
 *  Why circular xcorr instead of weighted argmax: with a wide enough Gaussian
 *  the xcorr smooths over the bimodal energy distribution that the temporal
 *  first-difference produces in .spkD samples (energy lobes on either side of
 *  the original peak), so the discovered shift correctly tracks the raw
 *  peak position even when working in derivative space.  σ ≳ 1.5 samples is
 *  recommended for stderiv; smaller σ may discover a shift biased toward one
 *  of the lobes.
 *
 *  @param wav        Snippet [sample * nChan + chan], nSamp * nChan shorts
 *  @param nChan      Channels in snippet (may be derivative channels for spkD)
 *  @param nSamp      Samples per snippet
 *  @param target     Target peak sample index (Gaussian centre, 0-based)
 *  @param maxShift   Output clamp; |shift| ≤ maxShift  (set ≤ 0 to disable)
 *  @param nTopChan   Use only the N highest-amplitude channels for energy
 *                    (0 or >= nChan: all channels)
 *  @param gauss      Precomputed Gaussian template, length nSamp
 *  @param score      Output: peak xcorr / total xcorr ∈ [0,1] — depends on σ;
 *                    tune minScore alongside alignSigma.
 *  @return           Integer shift δ
 */
static int computeShiftXcorr(const short* wav,
                              int nChan, int nSamp, int target,
                              int maxShift, int nTopChan,
                              const double* gauss,
                              double* score)
{
    // `target` is the centre of `gauss` (precomputed by caller); not used
    // inside this function — kept in the signature for symmetry with
    // computeShift.
    (void)target;

    // Top-K channel selection — uses the full window for amplitude ranking
    // (no maxShift bound here; the Gaussian σ controls the effective window).
    std::vector<uint8_t> chanUse(nChan, 1);
    if (nTopChan > 0 && nTopChan < nChan) {
        std::vector<std::pair<int64_t,int>> chanAmp(nChan);
        for (int c = 0; c < nChan; ++c) {
            int64_t pk = 0;
            for (int t = 0; t < nSamp; ++t) {
                const int64_t v = std::abs((int)wav[t * nChan + c]);
                if (v > pk) pk = v;
            }
            chanAmp[c] = {pk, c};
        }
        std::partial_sort(chanAmp.begin(),
                          chanAmp.begin() + nTopChan,
                          chanAmp.end(),
                          [](const auto& a, const auto& b){
                              return a.first > b.first;
                          });
        std::fill(chanUse.begin(), chanUse.end(), 0);
        for (int k = 0; k < nTopChan; ++k)
            chanUse[chanAmp[k].second] = 1;
    }

    // Energy across the FULL window (selected channels only)
    std::vector<double> energy(nSamp, 0.0);
    for (int t = 0; t < nSamp; ++t) {
        int64_t e = 0;
        for (int c = 0; c < nChan; ++c)
            if (chanUse[c]) e += std::abs((int)wav[t * nChan + c]);
        energy[t] = (double)e;
    }

    // Circular cross-correlation R[d] = Σ_t E[t] · g[(t − d) mod N].
    // gauss is centred at `target`, so g[(t−d) mod N] is a Gaussian centred
    // (in the t-axis) at t = target + d.  R[d] is therefore maximised at the
    // d* that places the Gaussian peak over E's dominant mass:
    //     d* = (sample where E is concentrated) − target = signed shift.
    std::vector<double> R(nSamp, 0.0);
    for (int d = 0; d < nSamp; ++d) {
        double r = 0.0;
        for (int t = 0; t < nSamp; ++t) {
            int idx = t - d;
            if (idx < 0) idx += nSamp;     // guaranteed nSamp + idx > 0 since d < nSamp
            r += energy[t] * gauss[idx];
        }
        R[d] = r;
    }

    // Argmax; if R is all-zero (silent snippet) return 0 — no information.
    double rMax = R[0];
    int dStar = 0;
    for (int d = 1; d < nSamp; ++d)
        if (R[d] > rMax) { rMax = R[d]; dStar = d; }
    if (rMax <= 0.0) {
        if (score) *score = 0.0;
        return 0;
    }

    // Parabolic refinement using circular neighbours of dStar.  Shift is
    // signed in (−N/2, +N/2]; for d in (N/2, N) we treat as (d − N).
    int dPrev = (dStar - 1 + nSamp) % nSamp;
    int dNext = (dStar + 1) % nSamp;
    double a0 = R[dPrev], a1 = R[dStar], a2 = R[dNext];
    double delta = 0.0;
    double denom = 2.0 * (2.0*a1 - a0 - a2);
    if (denom > 1e-9) {
        double off = (a2 - a0) / denom;
        if (off > -1.0 && off < 1.0) delta = off;
    }
    double dContinuous = (double)dStar + delta;
    if      (dContinuous >= (double)nSamp) dContinuous -= nSamp;
    else if (dContinuous <  0.0)           dContinuous += nSamp;

    double signedShift = (dContinuous > nSamp / 2.0)
        ? dContinuous - (double)nSamp
        : dContinuous;

    int outShift = (int)std::round(signedShift);
    if (maxShift > 0) {
        if (outShift >  maxShift) outShift =  maxShift;
        if (outShift < -maxShift) outShift = -maxShift;
    }

    // Score: peak xcorr / total xcorr.  The absolute scale of this depends on
    // σ (sharper Gaussian ⇒ higher peak fraction even on flat energy), so
    // recalibrate minScore when alignSigma changes.
    if (score) {
        double rTotal = 0.0;
        for (auto v : R) rTotal += v;
        *score = (rTotal > 0.0) ? (R[dStar] / rTotal) : 0.0;
    }
    return outShift;
}

// ── Main ──────────────────────────────────────────────────────────────────

static void usage(const char* prog)
{
    fprintf(stderr,
        "Usage: %s [options] basename electrodeGroup\n"
        "  -n nTotalChannels   Total channels in .fil\n"
        "  -c channelList      Comma-separated channel IDs (e.g. 0,1,2,3)\n"
        "  -w nSamples         Samples per spike waveform\n"
        "  -p peakSampleIndex  Target peak position (1-based)\n"
        "  -m maxShift         Maximum shift in samples [default: 3]\n"
        "  --stderiv           Process .spkD.N with SDIFF_ALLPAIRS re-extraction\n"
        "  --min-score f       Minimum energy score to accept shift [default: 0.0]\n"
        "  --top-channels N    Use only the N highest-amplitude channels per spike\n"
        "                      for alignment energy (excludes collision/noise on\n"
        "                      low-amplitude channels) [default: 0 = use all]\n"
        "  --align-sigma s     Std (in samples) of a stationary Gaussian template;\n"
        "                      when > 0, alignment shift is found by circular\n"
        "                      cross-correlation of the per-spike energy with\n"
        "                      this Gaussian over the FULL nSamples window,\n"
        "                      operating on the existing .spk.N / .spkD.N\n"
        "                      snippet (no .fil read for discovery).  σ ≳ 1.5\n"
        "                      bridges the bimodal energy of .spkD waveforms.\n"
        "                      ≤ 0 falls back to the legacy bounded-argmax path\n"
        "                      [default: 0.0]\n"
        "  -v                  Verbose\n"
        "  -h                  This help\n",
        prog);
}

int main(int argc, char* argv[])
{
    // ── Parse arguments ───────────────────────────────────────────────────
    int         nTotalChannels = 0;
    std::string channelListStr;
    int         nSamples       = 0;
    int         peakSampleIdx  = -1;  // 0-based after conversion
    int         maxShift       = 3;
    std::string method         = "standard";  // chain-of-custody method tag
    double      minScore       = 0.0;
    int         nTopChan       = 0;   // 0 = use all channels (legacy behaviour)
    double      alignSigma     = 0.0; // ≤ 0 = legacy bounded-argmax; > 0 = circular xcorr
    bool        verbose        = false;
    std::string basename;
    int         electrodeGroup = 0;

    for (int i = 1; i < argc; ) {
        const char* a = argv[i];
        if (strcmp(a, "-h") == 0) { usage(argv[0]); return 0; }
        else if (strcmp(a, "-n") == 0 && i+1 < argc)
            nTotalChannels = atoi(argv[++i]);
        else if (strcmp(a, "-c") == 0 && i+1 < argc)
            channelListStr = argv[++i];
        else if (strcmp(a, "-w") == 0 && i+1 < argc)
            nSamples = atoi(argv[++i]);
        else if (strcmp(a, "-p") == 0 && i+1 < argc)
            peakSampleIdx = atoi(argv[++i]) - 1; // convert 1-based to 0-based
        else if (strcmp(a, "-m") == 0 && i+1 < argc)
            maxShift = atoi(argv[++i]);
        else if (strcmp(a, "--method") == 0 && i+1 < argc)
            method = argv[++i];
        else if (strcmp(a, "--stderiv") == 0)
            method = "stderiv";          // deprecated alias for --method stderiv
        else if (strcmp(a, "--min-score") == 0 && i+1 < argc)
            minScore = atof(argv[++i]);
        else if (strcmp(a, "--top-channels") == 0 && i+1 < argc)
            nTopChan = atoi(argv[++i]);
        else if (strcmp(a, "--align-sigma") == 0 && i+1 < argc)
            alignSigma = atof(argv[++i]);
        else if (strcmp(a, "-v") == 0)
            verbose = true;
        else if (a[0] != '-') {
            if (basename.empty()) basename = a;
            else electrodeGroup = atoi(a);
        }
        ++i;
    }

    // ── Validate ──────────────────────────────────────────────────────────
    if (basename.empty())       die("missing session basename");
    if (electrodeGroup <= 0)    die("missing or invalid electrode group number");
    if (nTotalChannels <= 0)    die("missing or invalid -n (total channel count)");
    if (channelListStr.empty()) die("missing -c (channel list)");
    if (nSamples <= 0)          die("missing or invalid -w (samples per waveform)");
    if (peakSampleIdx < 0)      die("missing or invalid -p (peak sample index)");
    if (peakSampleIdx >= nSamples) die("-p peak index >= waveform length");

    std::vector<int> chanList = parseIntList(channelListStr.c_str());
    const int nChanGrp = (int)chanList.size();
    if (nChanGrp == 0) die("empty channel list");
    for (int c : chanList)
        if (c < 0 || c >= nTotalChannels) die("channel ID out of range");

    if (maxShift <= 0) die("-m maxShift must be > 0");
    if (maxShift >= nSamples) die("-m maxShift >= nSamples; waveform too short");

    // File paths (chain-of-custody: <base>.<type>.<method>.<grp>).
    // `stderiv` gates the SDIFF_ALLPAIRS re-extraction on transformed waveforms.
    const bool stderiv          = (method == "stderiv");
    const std::string resPath   = neurosuite::custody::resolve(basename, "res", electrodeGroup, method).path;
    const std::string spkPath   = neurosuite::custody::resolve(basename, "spk", electrodeGroup, method).path;
    const std::string filPath   = neurosuite::custody::sessionPath(basename, "fil");

    if (verbose) {
        fprintf(stderr, "process_alignspikes:\n");
        fprintf(stderr, "  session       = %s\n", basename.c_str());
        fprintf(stderr, "  group         = %d\n", electrodeGroup);
        fprintf(stderr, "  stderiv       = %s\n", stderiv ? "yes" : "no");
        fprintf(stderr, "  nTotalChan    = %d\n", nTotalChannels);
        fprintf(stderr, "  nChanGrp      = %d\n", nChanGrp);
        fprintf(stderr, "  nSamples      = %d\n", nSamples);
        fprintf(stderr, "  peakSampleIdx = %d (0-based)\n", peakSampleIdx);
        fprintf(stderr, "  maxShift      = %d samples\n", maxShift);
        fprintf(stderr, "  minScore      = %.2f\n", minScore);
        fprintf(stderr, "  nTopChan      = %d%s\n", nTopChan,
                (nTopChan <= 0 || nTopChan >= static_cast<int>(chanList.size()))
                    ? " (all channels)" : "");
        fprintf(stderr, "  alignSigma    = %.3f%s\n", alignSigma,
                (alignSigma > 0.0)
                    ? " (circular xcorr on snippets)"
                    : " (legacy bounded argmax)");
        fprintf(stderr, "  spk file      = %s\n", spkPath.c_str());
        fprintf(stderr, "  res file      = %s\n", resPath.c_str());
        fprintf(stderr, "  fil file      = %s\n", filPath.c_str());
    }

    // ── Read .res.N ───────────────────────────────────────────────────────
    FILE* resF = xfopen(resPath.c_str(), "rb");
    fseeko(resF, 0, SEEK_END);
    const off_t resSize  = ftello(resF);
    const int   nSpikes  = (int)(resSize / sizeof(int64_t));
    fseeko(resF, 0, SEEK_SET);

    std::vector<int64_t> resTs(nSpikes);
    if (nSpikes > 0 && fread(resTs.data(), sizeof(int64_t), nSpikes, resF) != (size_t)nSpikes)
        die("short read on .res file");
    fclose(resF);

    if (nSpikes == 0) {
        fprintf(stderr, "process_alignspikes: group %d has 0 spikes — nothing to do\n",
                electrodeGroup);
        return 0;
    }
    if (verbose)
        fprintf(stderr, "  nSpikes       = %d\n", nSpikes);

    // ── Read .spk.N into memory ───────────────────────────────────────────
    const int   elemsPerSpike = nSamples * nChanGrp;
    const off_t spkExpected   = (off_t)nSpikes * elemsPerSpike * sizeof(short);

    FILE* spkF = xfopen(spkPath.c_str(), "rb");
    fseeko(spkF, 0, SEEK_END);
    const off_t spkSize = ftello(spkF);
    if (spkSize != spkExpected) {
        fprintf(stderr,
                "process_alignspikes: %s size %lld does not match expected %lld "
                "(%d spikes × %d samp × %d chan × 2 bytes)\n",
                spkPath.c_str(), (long long)spkSize, (long long)spkExpected,
                nSpikes, nSamples, nChanGrp);
        fclose(spkF);
        exit(1);
    }
    fseeko(spkF, 0, SEEK_SET);

    std::vector<short> spkBuf((size_t)nSpikes * elemsPerSpike);
    if (fread(spkBuf.data(), sizeof(short), spkBuf.size(), spkF) != spkBuf.size())
        die("short read on .spk file");
    fclose(spkF);

    // ── Compute shifts ────────────────────────────────────────────────────
    //
    // Discovery has two paths, switched on alignSigma:
    //
    //   (A) alignSigma > 0   "xcorr on snippet" path.
    //       Spikes are already coarsely aligned by detection, so the true
    //       peak is somewhere inside the existing nSamples window.  We
    //       compute per-sample energy E(t) = Σ_c |W[t,c]| over the FULL
    //       nSamples window directly from spkBuf, then circular-xcorr E
    //       against a stationary Gaussian g[k] = exp(−(k−target)²/2σ²)
    //       precomputed once.  argmax_d R[d] (with parabolic sub-sample
    //       refinement) IS the signed shift.  No .fil read for either mode.
    //
    //       For .spkD samples the temporal first-difference produces energy
    //       lobes on either side of the original peak with a dip in the
    //       middle; a wide enough Gaussian (σ ≳ 1.5 samples) bridges the
    //       lobes and the xcorr peak still lands at the original peak.
    //       Tighter σ may bias toward one of the lobes.
    //
    //   (B) alignSigma ≤ 0   legacy bounded-argmax path.
    //       Raw mode: argmax over [target−maxShift, target+maxShift] on the
    //       spkBuf snippet directly.
    //       Stderiv mode: re-read each spike's window from .fil, apply
    //       SDIFF_ALLPAIRS only (no temporal diff) to produce a peak-aligned
    //       signal, then bounded argmax.  Avoids the .spkD double-lobe
    //       problem at the cost of one random .fil read per spike.
    //
    // After discovery, the re-extraction phase (below) reads the .fil at
    // extTs = resTs + shift and writes new spk/spkD samples using exactly
    // the same waveform-formation logic as ndm_extractspikes /
    // ndm_extractspikes_stderiv (raw passthrough or SDIFF_ALLPAIRS + temporal
    // first-difference, respectively).
    std::vector<int>    shifts(nSpikes, 0);
    std::vector<double> scores(nSpikes, 0.0);
    int nToRextract = 0;

    // Precompute the stationary Gaussian template for path (A).
    std::vector<double> gauss;
    if (alignSigma > 0.0) {
        gauss.resize((size_t)nSamples);
        const double inv2s2 = 1.0 / (2.0 * alignSigma * alignSigma);
        for (int k = 0; k < nSamples; ++k) {
            const double dk = (double)(k - peakSampleIdx);
            gauss[(size_t)k] = std::exp(-dk * dk * inv2s2);
        }
    }

    if (alignSigma > 0.0) {
        // Path A: circular xcorr on existing spk/spkD snippets — no .fil read.
        for (int i = 0; i < nSpikes; ++i) {
            const short* wav = spkBuf.data() + (size_t)i * elemsPerSpike;
            double sc = 0.0;
            int sh = computeShiftXcorr(wav, nChanGrp, nSamples, peakSampleIdx,
                                       maxShift, nTopChan, gauss.data(), &sc);
            scores[i] = sc;
            if (sc >= minScore) {
                shifts[i] = sh;
                if (sh != 0) ++nToRextract;
            }
        }
    } else if (!stderiv) {
        // Path B (raw): bounded argmax on spkBuf directly.
        for (int i = 0; i < nSpikes; ++i) {
            const short* wav = spkBuf.data() + (size_t)i * elemsPerSpike;
            double sc = 0.0;
            int  sh = computeShift(wav, nChanGrp, nSamples, peakSampleIdx,
                                    maxShift, nTopChan, &sc);
            scores[i] = sc;
            if (sc >= minScore) {
                shifts[i] = sh;
                if (sh != 0) ++nToRextract;
            }
        }
    } else {
        // Path B (stderiv): read .fil and apply SDIFF_ALLPAIRS only.
        FILE* filShift = xfopen(filPath.c_str(), "rb");
        fseeko(filShift, 0, SEEK_END);
        const off_t filShiftSize = ftello(filShift);
        fseeko(filShift, 0, SEEK_SET);

        const int   rawElems  = nSamples * nTotalChannels;
        const off_t rawBytes  = (off_t)rawElems * sizeof(short);
        std::vector<short> rawWin(rawElems);
        std::vector<short> sdiffWin((size_t)nSamples * nChanGrp, 0);
        off_t filShiftPos = 0;

        std::vector<int> order(nSpikes);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b){
            return resTs[a] < resTs[b];
        });

        for (int oi = 0; oi < nSpikes; ++oi) {
            const int i = order[oi];
            const int64_t winStart = resTs[i] - peakSampleIdx;
            if (winStart < 0) continue;
            const off_t fileOff = winStart * (off_t)nTotalChannels * (off_t)sizeof(short);
            if (fileOff + rawBytes > filShiftSize) continue;

            if (fileOff != filShiftPos) {
                fseeko(filShift, fileOff, SEEK_SET);
                filShiftPos = fileOff;
            }
            if (fread(rawWin.data(), sizeof(short), rawElems, filShift) != (size_t)rawElems)
                continue;
            filShiftPos += rawBytes;

            // Apply SDIFF_ALLPAIRS only (no temporal diff) to get sdiff signal
            for (int s = 0; s < nSamples; ++s) {
                const short* frame = rawWin.data() + s * nTotalChannels;
                for (int ci = 0; ci < nChanGrp; ++ci) {
                    double sd = sdiff_allpairs(frame, chanList.data(), ci, nChanGrp);
                    if      (sd >  32767.0) sd =  32767.0;
                    else if (sd < -32768.0) sd = -32768.0;
                    sdiffWin[(size_t)s * nChanGrp + ci] = (short)sd;
                }
            }

            double sc = 0.0;
            int sh = computeShift(sdiffWin.data(), nChanGrp, nSamples,
                                  peakSampleIdx, maxShift, nTopChan, &sc);
            scores[i] = sc;
            if (sc >= minScore) {
                shifts[i] = sh;
                if (sh != 0) ++nToRextract;
            }
        }
        fclose(filShift);
    }

    if (verbose)
        fprintf(stderr, "  spikes to re-extract = %d / %d (%.1f%%)\n",
                nToRextract, nSpikes, 100.0 * nToRextract / nSpikes);

    // ── Re-extract shifted spikes from .fil ───────────────────────────────
    if (nToRextract > 0) {
        // Build event list sorted by file offset for near-sequential .fil access
        struct ReEvent { off_t off; int idx; };
        std::vector<ReEvent> relist;
        relist.reserve(nToRextract);

        for (int i = 0; i < nSpikes; ++i) {
            if (shifts[i] == 0) continue;
            const int64_t extTs   = resTs[i] + shifts[i];
            const int64_t winStart = extTs - peakSampleIdx;
            if (winStart < 0) {
                // Window extends before start of recording — skip
                if (verbose)
                    fprintf(stderr,
                            "  warning: spike %d at ts=%lld shift=%d would "
                            "require reading before file start; left unshifted\n",
                            i, (long long)resTs[i], shifts[i]);
                shifts[i] = 0;
                continue;
            }
            const off_t fileOff = winStart * (off_t)nTotalChannels * (off_t)sizeof(short);
            relist.push_back({fileOff, i});
        }
        std::sort(relist.begin(), relist.end(),
                  [](const ReEvent& a, const ReEvent& b){ return a.off < b.off; });

        FILE* filF = xfopen(filPath.c_str(), "rb");
        fseeko(filF, 0, SEEK_END);
        const off_t filSize = ftello(filF);
        fseeko(filF, 0, SEEK_SET);

        const int rawElems     = nSamples * nTotalChannels;
        const off_t rawBytes   = (off_t)rawElems * sizeof(short);
        std::vector<short> rawFrame(rawElems);

        // Scratch buffer for per-spike derivative output (stderiv mode)
        std::vector<short> derivFrame(elemsPerSpike);

        off_t filePos = 0;

        for (const ReEvent& ev : relist) {
            // Clamp at end of file
            if (ev.off + rawBytes > filSize) {
                if (verbose)
                    fprintf(stderr,
                            "  warning: spike %d re-extraction window extends past "
                            "end of .fil; left unshifted\n", ev.idx);
                shifts[ev.idx] = 0;
                continue;
            }

            // Seek to window start (use fseeko for sparse jumps between shifted spikes)
            if (ev.off != filePos) {
                if (fseeko(filF, ev.off, SEEK_SET) != 0)
                    die("fseeko failed on .fil");
                filePos = ev.off;
            }

            if (fread(rawFrame.data(), sizeof(short), rawElems, filF) != (size_t)rawElems)
                die("short read from .fil during re-extraction");
            filePos += rawBytes;

            short* dst = spkBuf.data() + (size_t)ev.idx * elemsPerSpike;

            if (!stderiv) {
                // Raw pipeline: extract group channels directly
                // Layout: [sample * nChanGrp + chan]
                for (int s = 0; s < nSamples; ++s) {
                    const short* frame = rawFrame.data() + s * nTotalChannels;
                    for (int c = 0; c < nChanGrp; ++c)
                        dst[s * nChanGrp + c] = frame[chanList[c]];
                }
            } else {
                // Stderiv pipeline: apply SDIFF_ALLPAIRS + temporal first-difference.
                //
                // ndm_extractspikes_stderiv applies two transforms:
                //   step 1: sdiff[t,c]   = ALLPAIRS(raw[t,c])
                //   step 2: stderiv[t,c] = sdiff[t,c] - sdiff[t-1,c]
                //
                // We must match this exactly so re-extracted spikes are in the same
                // signal space as spikes that were not realigned.
                //
                // For the temporal diff at t=0 the previous sample is outside the
                // extracted window and is unknown — use zero as the baseline
                // (sdiff[t=-1,c] = 0), which matches what ndm_extractspikes_stderiv
                // does at the very first sample of the recording (g_prev_sdiff is
                // zero-initialised before each detection pass).
                std::vector<double> prevSdiff(nChanGrp, 0.0);
                for (int s = 0; s < nSamples; ++s) {
                    const short* frame = rawFrame.data() + s * nTotalChannels;
                    for (int ci = 0; ci < nChanGrp; ++ci) {
                        const double sd = sdiff_allpairs(frame, chanList.data(), ci, nChanGrp);
                        double stderiv  = sd - prevSdiff[ci];
                        prevSdiff[ci]   = sd;
                        if      (stderiv >  32767.0) stderiv =  32767.0;
                        else if (stderiv < -32768.0) stderiv = -32768.0;
                        dst[s * nChanGrp + ci] = (short)stderiv;
                    }
                }
            }
        }
        fclose(filF);
    }

    // ── Sort output order by extTs = resTs[i] + shifts[i] ─────────────────
    // The .spk.N file must be in ascending temporal order matching .res.N.
    // If any shift is non-zero the order may differ from the original.
    std::vector<int> sortOrder(nSpikes);
    std::iota(sortOrder.begin(), sortOrder.end(), 0);
    std::stable_sort(sortOrder.begin(), sortOrder.end(),
        [&](int a, int b) {
            return (resTs[a] + shifts[a]) < (resTs[b] + shifts[b]);
        });

    // Count reordered spikes
    int nReordered = 0;
    for (int j = 0; j < nSpikes; ++j)
        if (sortOrder[j] != j) ++nReordered;

    // ── Write updated .spk.N atomically (write to .tmp, rename) ───────────
    const std::string spkTmp = spkPath + ".aligntmp";
    FILE* outF = xfopen(spkTmp.c_str(), "wb");
    for (int j = 0; j < nSpikes; ++j) {
        const int i = sortOrder[j];
        if (fwrite(spkBuf.data() + (size_t)i * elemsPerSpike,
                   sizeof(short), elemsPerSpike, outF) != (size_t)elemsPerSpike) {
            fclose(outF);
            remove(spkTmp.c_str());
            die("write error on .spk output");
        }
    }
    fclose(outF);

    if (rename(spkTmp.c_str(), spkPath.c_str()) != 0) {
        remove(spkTmp.c_str());
        die("rename of .spk.aligntmp failed");
    }

    // ── Archive original .res, then write updated .res atomically ─────────
    // Klusters' nudge — and any other tool that re-reads .fil at the .res
    // offset to reconstruct the spike window — assumes
    //
    //     .spk[i] peak  ≡  .fil at file-sample .res[i]
    //
    // i.e. .res[i] is the file sample where the peak of .spk[i] resides.
    // Before alignment this holds (canonical extractspikes writes the
    // refined-peak position to .res; see process_extractspikes_stderiv
    // line 1456).  After alignment the .spk[i] peak is at extTs[i] =
    // resTs[i] + shifts[i], so .res must be updated to match — otherwise
    // every downstream tool that re-extracts from .fil reads a window
    // that is shifts[i] samples off from the actual peak.
    //
    // The pre-alignment .res is archived to .res.G.prealign so the
    // original detection-threshold timestamps can be recovered.  This
    // mirrors how the script archives stale .fet/.pca to .prealign.
    {
        const std::string resPrealign = resPath + ".prealign";
        // Best-effort copy: ignore failure (the file may already exist
        // from a previous invocation; we don't want to clobber the
        // original archive).
        if (access(resPrealign.c_str(), F_OK) != 0) {
            FILE* aIn  = fopen(resPath.c_str(),     "rb");
            FILE* aOut = fopen(resPrealign.c_str(), "wb");
            if (aIn && aOut) {
                std::vector<char> copyBuf(1 << 16);
                size_t n;
                while ((n = fread(copyBuf.data(), 1, copyBuf.size(), aIn)) > 0)
                    if (fwrite(copyBuf.data(), 1, n, aOut) != n) break;
            }
            if (aIn)  fclose(aIn);
            if (aOut) fclose(aOut);
        }

        const std::string resTmp = resPath + ".aligntmp";
        FILE* resOutF = xfopen(resTmp.c_str(), "wb");
        for (int j = 0; j < nSpikes; ++j) {
            const int     i     = sortOrder[j];
            const int64_t newTs = resTs[i] + (int64_t)shifts[i];
            if (fwrite(&newTs, sizeof(int64_t), 1, resOutF) != 1) {
                fclose(resOutF);
                remove(resTmp.c_str());
                die("write error on .res output");
            }
        }
        fclose(resOutF);

        if (rename(resTmp.c_str(), resPath.c_str()) != 0) {
            remove(resTmp.c_str());
            die("rename of .res.aligntmp failed");
        }
    }

    // ── Statistics ────────────────────────────────────────────────────────
    int nShifted = 0;
    int64_t sumAbsShift = 0;
    int minSh = INT_MAX, maxSh = INT_MIN;

    for (int i = 0; i < nSpikes; ++i) {
        if (shifts[i] != 0) {
            ++nShifted;
            sumAbsShift += std::abs(shifts[i]);
            if (shifts[i] < minSh) minSh = shifts[i];
            if (shifts[i] > maxSh) maxSh = shifts[i];
        }
    }

    fprintf(stderr,
            "process_alignspikes group %d: %d/%d spikes shifted "
            "(%.1f%%)  mean-abs=%.2f samp",
            electrodeGroup, nShifted, nSpikes,
            nSpikes > 0 ? 100.0 * nShifted / nSpikes : 0.0,
            nShifted > 0 ? (double)sumAbsShift / nShifted : 0.0);
    if (nShifted > 0)
        fprintf(stderr, "  range=[%+d,%+d]", minSh, maxSh);
    if (nReordered > 0)
        fprintf(stderr, "  reordered=%d", nReordered);
    fprintf(stderr, "\n");
    fprintf(stderr,
            "  NOTE: delete %s.fet%s%d and %s.pca%s%d then re-run "
            "process_pca%s\n",
            basename.c_str(), stderiv ? "D." : ".", electrodeGroup,
            basename.c_str(), stderiv ? "D." : ".", electrodeGroup,
            stderiv ? "_stderiv" : "");

    return 0;
}
