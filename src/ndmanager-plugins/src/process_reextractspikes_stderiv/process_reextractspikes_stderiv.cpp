/***************************************************************************
 * process_reextractspikes_stderiv.cpp
 *
 * Spatial-derivative + temporal first-difference detection pass with mask
 * exclusion.  Mirrors process_extractspikes_stderiv's transform pipeline
 * but uses the simplified mmap-based detection engine from
 * process_reextractspikes and layers in a mask-exclusion predicate.
 *
 * Transform pipeline
 * ------------------
 *   Step 1 (spatial):   sdiff[t, ch]   = spatialDerivative(raw[t, *], order)
 *   Step 2 (temporal):  stderiv[t, ch] = sdiff[t, ch] - sdiff[t-1, ch]
 *
 * Threshold computation
 * ---------------------
 *   Thresholds are computed internally over the window
 *   [threshStartByte, threshStartByte + threshSizeBytes) with the Quiroga
 *   (2004) robust formula
 *       thr = thresholdFactor × 4 × median(|x|) / 0.6745
 *   where x, chosen by -T exactly as in process_extractspikes_stderiv, is
 *       sdiff   (default) the spatial derivative only — the detector's
 *               historical noise base, although it thresholds the stderiv
 *               signal, whose noise is smaller;
 *       stderiv the detection signal itself (rounded, int16-clamped sdiff,
 *               then a clamped first difference; the window's first sample
 *               has no difference).
 *   Both bases are measured the way computeSdiffThresholds measures them
 *   (same window, decimation and median), so for the same -f / -B / -Z / -T
 *   the per-channel thresholds equal the detector's.  The detection signal
 *   is built like fill_sdiff_buffer's (rounded + clamped sdiff, clamped
 *   difference).
 *
 * The detection engine then runs on the stderiv shadow signal, and
 * waveform extraction (Pass 2) reads from the original raw .fil.
 *
 * Mask exclusion is applied to the detected peak timestamp, not to the
 * stderiv signal — so the mask window refers to ORIGINAL spike
 * timestamps, consistent with process_reextractspikes.
 *
 * Written windows follow the reference .spk's seed convention (-S, see the
 * header): sample 0 is sd[0] - sd[-1] with sd[-1] the previous sample's
 * spatial derivative (process_extractspikes_stderiv -R) or 0 (older files).
 ***************************************************************************/

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include "process_reextractspikes_stderiv.h"
#include "sdiff_pairs.h"      // shared SDIFF_CUSTOM pair-pattern parser
#include <neurosuite/core/spike_extract.hpp>   // detectStderivSeeding (header-only)

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

static const char *programVersion =
    "process_reextractspikes_stderiv 1.0 (2026-04)";

// =========================================================================
// Spatial derivative at a single (time-sample, group-channel) position.
// Ported from process_extractspikes_stderiv::computeSDiff, plus SDIFF_CUSTOM.
// =========================================================================
static std::vector<std::vector<int> > g_custom_partner;   // one map per group; set from -P

// The partner map for group g, or an empty map when g carries no custom pattern.
static const std::vector<int>& groupPartner(int g)
{
    static const std::vector<int> none;
    return (g >= 0 && g < (int)g_custom_partner.size()) ? g_custom_partner[g] : none;
}

// Order 5: g_custom_sets[g][ci] is the within-group reference set of channel ci
// (s[ci] = x[ci] - mean(x over the set)); set from -P when the pattern uses '+'.
static std::vector<std::vector<std::vector<int> > > g_custom_sets;

static const std::vector<std::vector<int> >& groupSets(int g)
{
    static const std::vector<std::vector<int> > none;
    return (g >= 0 && g < (int)g_custom_sets.size()) ? g_custom_sets[g] : none;
}

// Order 6: one expression per output channel; set from -P when a group names
// its channels (c0, c1, ...).  Older-form groups of the same spec are converted.
static std::vector<neurosuite::core::ChannelExprSet> g_custom_expr;

static const neurosuite::core::ChannelExprSet* groupExpr(int g)
{
    return (g >= 0 && g < (int)g_custom_expr.size() && !g_custom_expr[g].empty())
           ? &g_custom_expr[g] : nullptr;
}

static double computeSDiff(const int16_t *rec,
                            const int     *chanList,
                            int ci, int nChanGrp,
                            SdiffOrder order,
                            int g)
{
    const std::vector<int>& customPartner = groupPartner(g);
    const double val = rec[chanList[ci]];
    switch (order) {
    case SDIFF_NONE:
        return val;
    case SDIFF_FIRST:
        return (ci < nChanGrp - 1)
               ? val - rec[chanList[ci + 1]]
               : val - rec[chanList[ci - 1]];
    case SDIFF_LAPLACIAN:
        if (nChanGrp == 1)           return val;
        if (ci == 0)                 return val - rec[chanList[1]];
        if (ci == nChanGrp - 1)      return val - rec[chanList[nChanGrp - 2]];
        return val - 0.5 * (rec[chanList[ci - 1]] + rec[chanList[ci + 1]]);
    case SDIFF_CUSTOM:
        if (nChanGrp > 1 && (int)customPartner.size() == nChanGrp)
            return val - rec[chanList[customPartner[ci]]];
        [[fallthrough]];
    case SDIFF_CUSTOM_CAR: {
        // Byte-identical to process_extractspikes_stderiv's SDIFF_CUSTOM_CAR,
        // including its fall-through to all-pairs when the map does not fit.
        const std::vector<std::vector<int> >& sets = groupSets(g);
        if (order == SDIFF_CUSTOM_CAR && nChanGrp > 1 && (int)sets.size() == nChanGrp
            && !sets[ci].empty()) {
            double sum = 0.0;
            for (size_t k = 0; k < sets[ci].size(); ++k) sum += rec[chanList[sets[ci][k]]];
            return val - sum / (double)sets[ci].size();
        }
        [[fallthrough]];
    }
    case SDIFF_EXPR: {
        // Same as process_extractspikes_stderiv's SDIFF_EXPR.
        const neurosuite::core::ChannelExprSet* ex = groupExpr(g);
        if (order == SDIFF_EXPR && ex && nChanGrp > 1 && ex->fits(nChanGrp))
            return ex->eval(ci, [&](int i) { return (double)rec[chanList[i]]; });
        [[fallthrough]];
    }
    case SDIFF_ALLPAIRS:
    default: {
        if (nChanGrp == 1) return 0.0;
        double s = 0.0;
        for (int j = 0; j < nChanGrp; ++j) s += rec[chanList[j]];
        return (double)nChanGrp * val - s;
    }
    }
}

// Saturating round to int16, as process_extractspikes_stderiv's
// fill_sdiff_buffer stores the spatial derivative and its difference.
static inline int clamp16(double v)
{
    long iv = std::lround(v);
    if (iv >  32767) iv =  32767;
    if (iv < -32768) iv = -32768;
    return (int)iv;
}

// Temporal first-difference at time t: stderivAt(t) = sdiff(t) - sdiff(t-1),
// built like the detector's signal (fill_sdiff_buffer): each sdiff rounded and
// int16-clamped, the difference clamped.
static inline double stderivAt(const int16_t *data,
                                int64_t t, int nChanTot,
                                const int *chans, int ci, int nChanGrp,
                                SdiffOrder order,
                                int g)
{
    const int16_t *recT    = data + t       * nChanTot;
    const int16_t *recTm1  = data + (t - 1) * nChanTot;
    return (double)clamp16((double)clamp16(computeSDiff(recT,   chans, ci, nChanGrp, order, g))
                         - (double)clamp16(computeSDiff(recTm1, chans, ci, nChanGrp, order, g)));
}

// =========================================================================
// Per-group / per-channel thresholds — process_extractspikes_stderiv's
// computeSdiffThresholds on the mapped file: the window starts at
// startByte, holds sizeBytes / frame samples (cut at the end of the file),
// is decimated with the same stride, and both noise bases are measured:
//   |sdiff|   unrounded spatial derivative, every kept sample;
//   |stderiv| clamped difference of the rounded, clamped sdiff, every kept
//             sample but the window's first (its predecessor is not used).
// `noise` picks the base; both are reported in verbose mode.
// =========================================================================
static void computeThresholds(const int16_t *data,
                               int64_t nSamples,
                               int nChanTot,
                               off_t startByte, off_t sizeBytes,
                               const vector<vector<int>> &chGroups,
                               SdiffOrder order,
                               double factor,
                               ThresholdNoise noise,
                               vector<vector<double>> &outThresh,
                               bool verbose)
{
    const int64_t frame       = (int64_t)nChanTot * (int64_t)sizeof(int16_t);
    const int64_t startSample = startByte / frame;
    const int64_t nWinSamples = sizeBytes / frame;
    const int64_t tLo = std::max<int64_t>(0, startSample);
    const int64_t tHi = std::min<int64_t>(nSamples, tLo + nWinSamples);
    if (nWinSamples <= 0 || tHi <= tLo) {
        cerr << "error: threshold window has no samples "
                "(startByte=" << startByte << " sizeBytes=" << sizeBytes
             << " nSamples=" << nSamples << ")" << endl;
        std::exit(1);
    }

    // Decimate to keep memory bounded (~32 MB cap) — the detector's stride.
    const int64_t maxKept = 4000000;
    const int     stride  = (nWinSamples > maxKept)
                            ? (int)(nWinSamples / maxKept) + 1
                            : 1;

    const int nG = (int)chGroups.size();
    outThresh.assign(nG, {});
    vector<vector<vector<double>>> absSd(nG);
    vector<vector<vector<double>>> absTd(nG);
    vector<vector<int>>            prevSd(nG);
    for (int g = 0; g < nG; ++g) {
        absSd[g].resize(chGroups[g].size());
        absTd[g].resize(chGroups[g].size());
        prevSd[g].assign(chGroups[g].size(), 0);
        outThresh[g].resize(chGroups[g].size(), 0.0);
    }

    for (int64_t t = tLo; t < tHi; ++t) {
        const int64_t seen  = t - tLo;
        const bool    keep  = (seen % stride == 0);
        const bool    hasTd = (seen > 0);
        const int16_t *rec  = data + t * nChanTot;
        for (int g = 0; g < nG; ++g) {
            const int nCG = (int)chGroups[g].size();
            for (int ci = 0; ci < nCG; ++ci) {
                const double v   = computeSDiff(rec, chGroups[g].data(), ci, nCG, order, g);
                const int    sdi = clamp16(v);
                if (keep) {
                    absSd[g][ci].push_back(std::fabs(v));
                    if (hasTd)
                        absTd[g][ci].push_back(
                            std::fabs((double)clamp16((double)sdi - prevSd[g][ci])));
                }
                prevSd[g][ci] = sdi;
            }
        }
    }

    for (int g = 0; g < nG; ++g) {
        double minK = 1e300, maxK = 0.0;
        for (size_t ci = 0; ci < chGroups[g].size(); ++ci) {
            auto &vs = absSd[g][ci];
            auto &vt = absTd[g][ci];
            if (vs.empty() || vt.empty()) {
                cerr << "error: empty threshold window for g=" << g+1
                     << " ci=" << ci << endl;
                std::exit(1);
            }
            std::sort(vs.begin(), vs.end());
            std::sort(vt.begin(), vt.end());
            const double medSd = vs[vs.size() / 2];
            const double medTd = vt[vt.size() / 2];
            const double med   = (noise == THRNOISE_STDERIV) ? medTd : medSd;
            // Quiroga (2004): thr = factor × 4 × σ_n,   σ_n = median(|x|)/0.6745
            outThresh[g][ci] = factor * 4.0 * med / 0.6745;
            const double k = (medTd > 0) ? outThresh[g][ci] / (medTd / 0.6745) : 0.0;
            minK = std::min(minK, k); maxK = std::max(maxK, k);
            if (verbose)
                cerr << "  [noise] g=" << (g+1)
                     << " ch=" << chGroups[g][ci]
                     << "  median|sdiff|=" << medSd
                     << "  median|stderiv|=" << medTd
                     << "  thr=" << outThresh[g][ci]
                     << "  (= " << k << " sigma of the detection signal)" << endl;
        }
        if (!chGroups[g].empty())
            cout << "  group " << (g+1) << " noise base: "
                 << (noise == THRNOISE_STDERIV ? "stderiv" : "sdiff (historical)")
                 << "; effective threshold " << minK << ".." << maxK
                 << " sigma of the detection signal (factor " << factor
                 << " x 4 = " << factor * 4.0 << ")" << endl;
    }
}

// =========================================================================
// Shared helpers (duplicated small bodies — intentional, keeps this TU
// independent of process_reextractspikes object files for CMake simplicity).
// =========================================================================
static bool isRealPeak(double peak, double before, double after)
{
    if (peak == 0.0) {
        if ((before > 0 && after < 0) || (before < 0 && after > 0)) return false;
    } else if (peak > 0) {
        if (before > peak || after > peak) return false;
    } else {
        if (before < peak || after < peak) return false;
    }
    return true;
}

static bool isMasked(const std::vector<int64_t> &m, int64_t t, int64_t hw)
{
    if (m.empty() || hw < 0) return false;
    const int64_t lo = t - hw, hi = t + hw;
    auto it = std::lower_bound(m.begin(), m.end(), lo);
    return (it != m.end() && *it <= hi);
}

template <class T>
static vector<vector<T>> splitGroups(const string &s, T(*cvt)(const char*))
{
    vector<vector<T>> out;
    std::stringstream ss(s); string cur;
    while (std::getline(ss, cur, ':')) {
        vector<T> g; std::stringstream gs(cur); string tok;
        while (std::getline(gs, tok, ','))
            if (!tok.empty()) g.push_back(cvt(tok.c_str()));
        out.push_back(std::move(g));
    }
    return out;
}

static int asInt(const char *s) { return std::atoi(s); }

static vector<int64_t> loadMaskGroup(const string &base, int grp)
{
    std::ostringstream p;
    p << base << "." << SPIKE_TIME_OUT_EXT << "." << grp;
    FILE *f = std::fopen(p.str().c_str(), "rb");
    if (!f) return {};
    fseeko(f, 0, SEEK_END);
    const off_t sz = ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (sz <= 0 || sz % (off_t)sizeof(int64_t) != 0) { std::fclose(f); return {}; }
    vector<int64_t> v((size_t)(sz / (off_t)sizeof(int64_t)));
    if (std::fread(v.data(), sizeof(int64_t), v.size(), f) != v.size()) {
        std::fclose(f); return {};
    }
    std::fclose(f);
    std::sort(v.begin(), v.end());
    return v;
}

// =========================================================================
// Seed convention of the reference .spk (see the header, -S).
// =========================================================================
// The reference rows are <maskBase>.spk.N paired row-for-row with
// <maskBase>.res.N (int64).  Sample 0 of up to 1024 leading rows is handed to
// neurosuite::core::detectStderivSeeding, which recomputes it both ways from
// the .fil.  `why` explains an Unknown result.
static neurosuite::core::StderivSeeding
detectReferenceSeeding(const string &maskBase, int grp, const string &filPath,
                       int nChanTot, const vector<int> &chans,
                       int spikeLength, int timeBeforeSpike,
                       SdiffOrder order, int g, string &why)
{
    using neurosuite::core::StderivSeeding;
    const int nChan = (int)chans.size();
    std::ostringstream pr, ps;
    pr << maskBase << "." << SPIKE_TIME_OUT_EXT << "." << grp;
    ps << maskBase << ".spk." << grp;
    FILE *fr = std::fopen(pr.str().c_str(), "rb");
    FILE *fs = std::fopen(ps.str().c_str(), "rb");
    if (!fr || !fs) {
        why = "no reference " + (fr ? ps.str() : pr.str());
        if (fr) std::fclose(fr);
        if (fs) std::fclose(fs);
        return StderivSeeding::Unknown;
    }
    fseeko(fr, 0, SEEK_END); const off_t resBytes = ftello(fr); fseeko(fr, 0, SEEK_SET);
    fseeko(fs, 0, SEEK_END); const off_t spkBytes = ftello(fs); fseeko(fs, 0, SEEK_SET);
    const off_t rowBytes = (off_t)spikeLength * nChan * (off_t)sizeof(int16_t);
    const int64_t nRes = (int64_t)(resBytes / (off_t)sizeof(int64_t));
    if (resBytes % (off_t)sizeof(int64_t) != 0 || rowBytes <= 0
        || spkBytes != (off_t)nRes * rowBytes) {
        why = ps.str() + " does not hold one " + std::to_string(spikeLength) + "x"
            + std::to_string(nChan) + " row per spike of " + pr.str();
        std::fclose(fr); std::fclose(fs);
        return StderivSeeding::Unknown;
    }
    const int64_t nCheck = std::min<int64_t>(nRes, 1024);
    vector<int64_t> ts((size_t)nCheck);
    vector<int16_t> s0((size_t)nCheck * nChan);
    vector<int16_t> row((size_t)spikeLength * nChan);
    int64_t got = 0;
    for (; got < nCheck; ++got) {
        if (std::fread(&ts[(size_t)got], sizeof(int64_t), 1, fr) != 1) break;
        if (std::fread(row.data(), sizeof(int16_t), row.size(), fs) != row.size()) break;
        std::copy(row.begin(), row.begin() + nChan, s0.begin() + (size_t)got * nChan);
    }
    std::fclose(fr); std::fclose(fs);

    // The engine's own derivative (computeSDiff) in core terms.  A custom map
    // that does not fit the group falls back to all-pairs in computeSDiff, so
    // it does here too.
    using CoreOrder = neurosuite::core::SdiffOrder;
    CoreOrder co = static_cast<CoreOrder>((int)order);
    const vector<int> &partner = groupPartner(g);
    const vector<vector<int> > &sets = groupSets(g);
    vector<int> setOff, setMem;
    if (order == SDIFF_CUSTOM && !(nChan > 1 && (int)partner.size() == nChan))
        co = CoreOrder::AllPairs;
    if (order == SDIFF_CUSTOM_CAR) {
        if (nChan > 1 && (int)sets.size() == nChan) {
            setOff.push_back(0);
            for (int ci = 0; ci < nChan; ++ci) {
                setMem.insert(setMem.end(), sets[ci].begin(), sets[ci].end());
                setOff.push_back((int)setMem.size());
            }
        } else co = CoreOrder::AllPairs;
    }
    const neurosuite::core::ChannelExprSet* ex = groupExpr(g);
    if (order == SDIFF_EXPR && !(nChan > 1 && ex && ex->fits(nChan)))
        co = CoreOrder::AllPairs;
    neurosuite::core::SpikeWindowSpec spec;
    spec.nSamp = spikeLength; spec.peakSample = timeBeforeSpike;
    spec.totalNbChannels = nChanTot;
    spec.groupChannels = chans.data(); spec.nChan = nChan;
    spec.applyTransform = true; spec.order = co;
    spec.partner = (co == CoreOrder::Custom) ? partner.data() : nullptr;
    spec.setOff  = setOff.empty() ? nullptr : setOff.data();
    spec.setMem  = setMem.empty() ? nullptr : setMem.data();
    spec.expr    = (co == CoreOrder::Expr) ? ex : nullptr;
    FILE *fil = std::fopen(filPath.c_str(), "rb");
    if (!fil) { why = "cannot open " + filPath; return StderivSeeding::Unknown; }
    const auto r = neurosuite::core::detectStderivSeeding(
        fil, got, spec,
        [&](int64_t i) { return ts[(size_t)i]; },
        [&](int64_t i, int ci) { return s0[(size_t)i * nChan + ci]; });
    std::fclose(fil);
    if (r == StderivSeeding::Unknown)
        why = "no clear majority in " + std::to_string(got) + " reference rows";
    return r;
}

// =========================================================================
// Argument parsing
// =========================================================================
static void usage(const char *name)
{
    cerr << programVersion << endl;
    cerr << "usage: " << name
         << " -n nCh -c channels -w wLen -p peak -l pslen -r ref"
            " -f factor -B startByte -Z sizeBytes [-T sdiff|stderiv]"
            " [-d sdiffOrder] [-P g1:g2:...] [-S auto|prev|zero] [-m maskBase] [-M maskHalf]"
            " [-i in.fil] [-v] basename" << endl;
    cerr << "  -T  noise base of the threshold, as process_extractspikes_stderiv -T:\n"
            "      sdiff (default) = median|spatial derivative|, stderiv = median|\n"
            "      detection signal|; thr = factor * 4 * median / 0.6745" << endl;
    cerr << "  -S  seed of each written window's temporal difference: the previous\n"
            "      sample's spatial derivative (prev, process_extractspikes_stderiv -R),\n"
            "      0 (zero, older extractor), or auto (default): follow the reference\n"
            "      <maskBase>.spk.N / .res.N, previous sample when undetermined." << endl;
}

static bool parseArgs(int argc, char **argv, ReextractStderivArgs &a)
{
    string inputOverride;
    bool havePattern = false;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        if      (arg == "-h") { usage(argv[0]); std::exit(0); }
        else if (arg == "-v") a.verbose = true;
        else if (arg == "-n" && i+1 < argc) a.totalChannelNumber = std::atoi(argv[++i]);
        else if (arg == "-c" && i+1 < argc) a.channelList        = argv[++i];
        else if (arg == "-w" && i+1 < argc) a.spikeLength        = std::atoi(argv[++i]);
        else if (arg == "-p" && i+1 < argc) a.timeBeforeSpike    = std::atoi(argv[++i]) - 1;
        else if (arg == "-l" && i+1 < argc) a.peakSearchLength   = std::atoi(argv[++i]);
        else if (arg == "-r" && i+1 < argc) a.refractoryPeriod   = std::atoi(argv[++i]);
        else if (arg == "-f" && i+1 < argc) a.thresholdFactor    = std::atof(argv[++i]);
        else if (arg == "-B" && i+1 < argc) a.threshStartByte    = (off_t)std::atoll(argv[++i]);
        else if (arg == "-Z" && i+1 < argc) a.threshSizeBytes    = (off_t)std::atoll(argv[++i]);
        else if (arg == "-d" && i+1 < argc) a.sdiffOrder         = (SdiffOrder)std::atoi(argv[++i]);
        else if (arg == "-P" && i+1 < argc) {
            // Same dispatch as process_extractspikes_stderiv: '+' selects order 5.
            const char *pat = argv[++i];
            if (sdiffSpecIsExpression(pat)) { g_custom_expr = parseSdiffExprsPerGroup(pat);
                                              a.sdiffOrder = SDIFF_EXPR; }
            else if (sdiffSpecUsesSets(pat)) { g_custom_sets = parseSdiffSetsPerGroup(pat);
                                          a.sdiffOrder = SDIFF_CUSTOM_CAR; }
            else                        { g_custom_partner = parseSdiffPairsPerGroup(pat);
                                          a.sdiffOrder = SDIFF_CUSTOM; }
            havePattern = true;
        }
        else if (arg == "-T" && i+1 < argc) {
            const string v = argv[++i];
            if      (v == "sdiff")   a.thresholdNoise = THRNOISE_SDIFF;
            else if (v == "stderiv") a.thresholdNoise = THRNOISE_STDERIV;
            else { cerr << "error: -T must be sdiff or stderiv" << endl; return false; }
        }
        else if (arg == "-S" && i+1 < argc) {
            const string v = argv[++i];
            if      (v == "auto") a.seedMode = SEED_AUTO;
            else if (v == "prev") a.seedMode = SEED_PREV;
            else if (v == "zero") a.seedMode = SEED_ZERO;
            else { cerr << "error: -S must be auto, prev or zero" << endl; return false; }
        }
        else if (arg == "-m" && i+1 < argc) a.maskBaseFileName   = argv[++i];
        else if (arg == "-M" && i+1 < argc) a.maskHalfWidth      = std::atoll(argv[++i]);
        else if (arg == "-i" && i+1 < argc) inputOverride        = argv[++i];
        else if (!arg.empty() && arg[0] != '-') a.outputBaseFileName = arg;
        else { cerr << "error: unknown option '" << arg << "'" << endl; return false; }
    }
    if (a.outputBaseFileName.empty()) return false;
    if ((int)a.sdiffOrder < 0 || (int)a.sdiffOrder > 6) {
        cerr << "error: -d must be 0-3 (4, 5 and 6 come from -P)" << endl; return false;
    }
    if ((a.sdiffOrder == SDIFF_CUSTOM || a.sdiffOrder == SDIFF_CUSTOM_CAR
         || a.sdiffOrder == SDIFF_EXPR) && !havePattern) {
        // Without the map the engine would silently difference all pairs.
        cerr << "error: sdiffOrder " << (int)a.sdiffOrder
             << " needs the sdiffPairs pattern (-P)" << endl; return false;
    }
    if (a.maskBaseFileName.empty()) a.maskBaseFileName = a.outputBaseFileName;
    a.inputFilePath = !inputOverride.empty() ? inputOverride
                                             : a.outputBaseFileName + ".fil";
    if (a.maskHalfWidth < 0) a.maskHalfWidth = a.refractoryPeriod;
    if (a.totalChannelNumber <= 0 || a.channelList.empty() ||
        a.spikeLength <= 0 || a.timeBeforeSpike < 0 ||
        a.peakSearchLength <= 0 || a.refractoryPeriod < 0 ||
        a.thresholdFactor <= 0 || a.threshSizeBytes <= 0) {
        cerr << "error: missing mandatory argument(s)" << endl;
        return false;
    }
    return true;
}

// =========================================================================
// main
// =========================================================================
int main(int argc, char **argv)
{
    ReextractStderivArgs a;
    if (!parseArgs(argc, argv, a)) { usage(argv[0]); return 1; }

    vector<vector<int>> chGroups = splitGroups<int>(a.channelList, &asInt);
    const int nGroups = (int)chGroups.size();
    for (int g = 0; g < nGroups; ++g)
        for (int c : chGroups[g])
            if (c < 0 || c >= a.totalChannelNumber) {
                cerr << "error: channel " << c << " out of range" << endl;
                return 1;
            }
    // Expressions name channels by position: a group they do not fit is refused
    // (process_extractspikes_stderiv does the same), never silently differenced
    // another way.
    if (a.sdiffOrder == SDIFF_EXPR)
        for (int g = 0; g < nGroups; ++g) {
            std::string why;
            if (groupExpr(g) && !groupExpr(g)->fits((int)chGroups[g].size(), &why)) {
                cerr << "error: group " << g + 1 << " sdiffPairs: " << why << "." << endl;
                return 1;
            }
        }

    // A spatial derivative needs neighbours.  On a one-channel group order 1
    // would read past the channel list, and orders 3-5 (all-pairs, and the
    // custom patterns that fall back to it) are identically zero, so the
    // threshold is zero and every refractory period yields a "spike".  The
    // detector refuses such a group (commit 9f2cf1b4); here the group is
    // skipped with a warning and no output, because ndm_reextractspikes passes
    // every spike group in -c (active or not, so positions line up) and an
    // inactive one-channel group must not break the run.
    vector<char> skip(nGroups, 0);
    vector<vector<int>> detGroups = chGroups;   // emptied for skipped groups
    if (a.sdiffOrder != SDIFF_NONE) {
        for (int g = 0; g < nGroups; ++g) {
            if (chGroups[g].size() != 1) continue;
            cerr << "warning: group " << g+1 << " has a single channel ("
                 << chGroups[g][0] << "); a spatial derivative (order "
                 << (int)a.sdiffOrder << ") needs at least two channels -- "
                    "skipping the group, no output written.\n"
                 << "         Re-extract it with the standard method, or group the "
                    "channel with its neighbours." << endl;
            skip[g] = 1;
            detGroups[g].clear();
        }
    }

    const int fd = ::open(a.inputFilePath.c_str(), O_RDONLY);
    if (fd < 0) {
        cerr << "error: open '" << a.inputFilePath << "': "
             << std::strerror(errno) << endl; return 1;
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0) { ::close(fd); return 1; }
    const off_t fileBytes = st.st_size;
    const int64_t bps = (int64_t)a.totalChannelNumber * (int64_t)sizeof(int16_t);
    if (fileBytes <= 0 || fileBytes % bps != 0) {
        cerr << "error: bad .fil size" << endl; ::close(fd); return 1;
    }
    const int64_t nSamples = fileBytes / bps;
    void *mapped = ::mmap(nullptr, (size_t)fileBytes, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        cerr << "error: mmap: " << std::strerror(errno) << endl;
        ::close(fd); return 1;
    }
    ::madvise(mapped, (size_t)fileBytes, MADV_SEQUENTIAL);
    const int16_t *data = static_cast<const int16_t *>(mapped);

    // Compute thresholds from stderiv signal over requested window.
    vector<vector<double>> thr;
    computeThresholds(data, nSamples, a.totalChannelNumber,
                      a.threshStartByte, a.threshSizeBytes,
                      detGroups, a.sdiffOrder, a.thresholdFactor,
                      a.thresholdNoise, thr, a.verbose);

    // A zero threshold (flat detection signal over the noise window: a dead
    // or constant channel) would accept a spike every refractory period, as
    // above.  The detector refuses it; skip the group here, for the same
    // reason as a one-channel group.
    for (int g = 0; g < nGroups; ++g) {
        if (skip[g]) continue;
        for (size_t ci = 0; ci < detGroups[g].size(); ++ci) {
            if (thr[g][ci] > 0.0) continue;
            cerr << "warning: group " << g+1 << " channel " << detGroups[g][ci]
                 << ": the detection signal is flat over the threshold window "
                    "(noise median 0) -- skipping the group, no output written."
                 << endl;
            skip[g] = 1;
            break;
        }
    }

    // Load per-group masks.
    vector<vector<int64_t>> maskBy(nGroups);
    for (int g = 0; g < nGroups; ++g)
        maskBy[g] = loadMaskGroup(a.maskBaseFileName, g + 1);

    if (a.verbose) {
        cerr << programVersion << endl
             << "input            : " << a.inputFilePath
             << " (" << nSamples << " samples × " << a.totalChannelNumber
             << " channels)" << endl
             << "output base      : " << a.outputBaseFileName << endl
             << "mask base        : " << a.maskBaseFileName << endl
             << "sdiffOrder       : " << (int)a.sdiffOrder << endl
             << "threshold factor : " << a.thresholdFactor << endl
             << "noise base       : " << (a.thresholdNoise == THRNOISE_STDERIV
                                          ? "stderiv" : "sdiff (historical)") << endl
             << "mask half-width  : " << a.maskHalfWidth << " samples" << endl;
        for (int g = 0; g < nGroups; ++g)
            cerr << "  group " << g+1 << ": "
                 << maskBy[g].size() << " masked ts" << endl;
    }

    // Safe t range for detection.
    const int64_t safeLeft  = std::max<int64_t>(1, a.timeBeforeSpike); // need t-1 for temporal diff
    const int64_t safeRight = std::max(a.peakSearchLength - 1,
                                        a.spikeLength - a.timeBeforeSpike - 1);
    const int64_t tStart = safeLeft;
    const int64_t tEnd   = nSamples - safeRight;

    vector<vector<int64_t>> resPerGroup(nGroups);
    vector<int64_t> nAccepted(nGroups, 0), nMasked(nGroups, 0);

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (int g = 0; g < nGroups; ++g) {
        if (skip[g]) continue;
        const vector<int>    &chans  = chGroups[g];
        const vector<double> &tg     = thr[g];
        const vector<int64_t>&mask   = maskBy[g];
        const int nChanG  = (int)chans.size();
        const int nChanT  = a.totalChannelNumber;
        const int psl     = a.peakSearchLength;
        const int refPer  = a.refractoryPeriod;
        const int64_t hw  = a.maskHalfWidth;
        const SdiffOrder ord = a.sdiffOrder;

        vector<int64_t> out;
        out.reserve((size_t)(nSamples / std::max(refPer, 1) + 1));
        int64_t lastAccepted = INT64_MIN / 2;

        for (int64_t t = tStart; t < tEnd; ++t) {
            // Refractory as process_extractspikes_stderiv (isRefractoryPeriod,
            // hybrid merge): an event within <= refractoryPeriod samples of
            // the last one is dropped; the peak found below is >= t.
            if (t - lastAccepted <= refPer) continue;

            int hitCi = -1; bool hitNeg = false;
            for (int ci = 0; ci < nChanG; ++ci) {
                const double v  = stderivAt(data, t, nChanT, chans.data(),
                                             ci, nChanG, ord, g);
                if (std::fabs(v) >= std::fabs(tg[ci])) {
                    hitCi = ci; hitNeg = (v < 0.0); break;
                }
            }
            if (hitCi < 0) continue;

            // Local extremum on the stderiv signal.
            int64_t peakT = t;
            double  peakV = stderivAt(data, t, nChanT, chans.data(),
                                       hitCi, nChanG, ord, g);
            const int64_t pEnd = std::min<int64_t>(t + psl, nSamples);
            for (int64_t s = t + 1; s < pEnd; ++s) {
                const double v = stderivAt(data, s, nChanT, chans.data(),
                                            hitCi, nChanG, ord, g);
                if ((hitNeg && v < peakV) || (!hitNeg && v > peakV)) {
                    peakV = v; peakT = s;
                }
            }
            if (peakT <= 1 || peakT >= nSamples - 1) continue;

            const double pb = stderivAt(data, peakT - 1, nChanT,
                                         chans.data(), hitCi, nChanG, ord, g);
            const double pa = stderivAt(data, peakT + 1, nChanT,
                                         chans.data(), hitCi, nChanG, ord, g);
            if (!isRealPeak(peakV, pb, pa)) continue;
            if (std::fabs(peakV) < std::fabs(tg[hitCi])) continue;

            if (isMasked(mask, peakT, hw)) {
                ++nMasked[g];
                lastAccepted = peakT; t = peakT;
                continue;
            }

            const int64_t wavStart = peakT - a.timeBeforeSpike;
            const int64_t wavEnd   = wavStart + a.spikeLength;
            if (wavStart < 0 || wavEnd > nSamples) continue;

            out.push_back(peakT);
            lastAccepted = peakT; t = peakT;
            ++nAccepted[g];
        }
        resPerGroup[g] = std::move(out);
    }

    // Write .res.N (int64) and .spkD.N (int16) using the STDERIV-TRANSFORMED
    // signal.  process_extractspikes_stderiv writes the transformed waveform
    // to .spkD (spatial derivative across group channels + temporal
    // first-difference, seeded as described below), and the .pcaD basis is
    // trained on these transformed waveforms.  If Pass-2 here wrote raw
    // waveforms, the new-spike rows in .spkD would be in a different
    // amplitude space than the reference rows and would project through
    // .pcaD basis into bogus features — shadowcluster then mis-assigns
    // them all to a single Mahalanobis shadow, which is exactly the
    // "green haze at the far right of cluster 5" symptom observed in
    // Klusters.
    //
    // Reuse computeSDiff() (ported unchanged from extractspikes_stderiv)
    // to keep the transform bit-identical between the two programs.
    //
    // Sample 0's seed follows the reference .spk (-S): sd[-1] is the clamped
    // spatial derivative of the sample before the window (what
    // process_extractspikes_stderiv -R writes) or 0 (the older extractor).
    for (int g = 0; g < nGroups; ++g) {
        if (skip[g]) continue;          // skipped above: no output at all
        const vector<int> &chans = chGroups[g];
        const int nChanG = (int)chans.size();
        const auto &res  = resPerGroup[g];

        bool seedPrev = (a.seedMode != SEED_ZERO);
        if (a.seedMode == SEED_AUTO && !res.empty()) {
            string why;
            const auto conv = detectReferenceSeeding(
                a.maskBaseFileName, g + 1, a.inputFilePath, a.totalChannelNumber,
                chans, a.spikeLength, a.timeBeforeSpike, a.sdiffOrder, g, why);
            seedPrev = (conv != neurosuite::core::StderivSeeding::Zero);
            cout << "  group " << (g+1) << ": reference .spk seed convention: "
                 << (conv == neurosuite::core::StderivSeeding::Previous ? "previous sample"
                     : conv == neurosuite::core::StderivSeeding::Zero
                           ? "zero (older extractor)"
                           : "undetermined (" + why + "), using previous sample")
                 << endl;
        }

        std::ostringstream pRes, pSpk;
        pRes << a.outputBaseFileName << "." << SPIKE_TIME_OUT_EXT << "." << (g+1);
        pSpk << a.outputBaseFileName << "." << SPIKE_REC_OUT_EXT  << "." << (g+1);

        FILE *fRes = std::fopen(pRes.str().c_str(), "wb");
        if (!fRes) { cerr << "error: cannot write " << pRes.str() << endl;
                     ::munmap(mapped, (size_t)fileBytes); ::close(fd); return 1; }
        if (!res.empty())
            std::fwrite(res.data(), sizeof(int64_t), res.size(), fRes);
        std::fclose(fRes);

        FILE *fSpk = std::fopen(pSpk.str().c_str(), "wb");
        if (!fSpk) { cerr << "error: cannot write " << pSpk.str() << endl;
                     ::munmap(mapped, (size_t)fileBytes); ::close(fd); return 1; }
        if (!res.empty()) {
            const size_t wLen = (size_t)a.spikeLength * (size_t)nChanG;
            // Two buffers: sdWav holds the intermediate spatial derivative
            // for all samples (needed for the in-place temporal pass), and
            // buf is the output written to disk.  Layout is sample-major
            // [s * nChanG + ci], matching process_extractspikes_stderiv's
            // .spkD layout exactly.
            vector<int16_t> sdWav(wLen);
            vector<int16_t> buf(wLen);
            for (int64_t ts : res) {
                const int64_t start = ts - a.timeBeforeSpike;

                // Step 1 — spatial derivative per sample.
                // computeSDiff takes a frame pointer and a chanList of
                // full-probe channel indices; it returns the sdiff value
                // for channel `ci` within the group.
                for (int s = 0; s < a.spikeLength; ++s) {
                    const int16_t *frame =
                        data + (int64_t)(start + s) * a.totalChannelNumber;
                    for (int ci = 0; ci < nChanG; ++ci) {
                        double sd = computeSDiff(frame, chans.data(),
                                                  ci, nChanG, a.sdiffOrder, g);
                        int iv = (int)std::llround(sd);
                        if (iv >  32767) iv =  32767;
                        if (iv < -32768) iv = -32768;
                        sdWav[(size_t)s * nChanG + ci] = (int16_t)iv;
                    }
                }

                // Step 2 — temporal first-difference, in-place into buf[].
                // Boundary: sdiff[-1] = the clamped spatial derivative of the
                // sample before the window when seedPrev (process_extract-
                // spikes_stderiv -R), else 0 (its detection pass / the older
                // extractor); a window at sample 0 has no predecessor and is
                // seeded with 0 either way.  Walking prev[] forward preserves
                // the sdiff (not the clamped output) for the next subtrahend.
                {
                    vector<int16_t> prev((size_t)nChanG, 0);
                    if (seedPrev && start >= 1) {
                        const int16_t *frame =
                            data + (int64_t)(start - 1) * a.totalChannelNumber;
                        for (int ci = 0; ci < nChanG; ++ci) {
                            int iv = (int)std::llround(computeSDiff(frame, chans.data(),
                                                       ci, nChanG, a.sdiffOrder, g));
                            if (iv >  32767) iv =  32767;
                            if (iv < -32768) iv = -32768;
                            prev[(size_t)ci] = (int16_t)iv;
                        }
                    }
                    for (int s = 0; s < a.spikeLength; ++s) {
                        int16_t       *dst = buf.data()   + (size_t)s * nChanG;
                        const int16_t *sd  = sdWav.data() + (size_t)s * nChanG;
                        for (int ci = 0; ci < nChanG; ++ci) {
                            int diff = (int)sd[ci] - (int)prev[(size_t)ci];
                            prev[(size_t)ci] = sd[ci];
                            if (diff >  32767) diff =  32767;
                            if (diff < -32768) diff = -32768;
                            dst[ci] = (int16_t)diff;
                        }
                    }
                }

                std::fwrite(buf.data(), sizeof(int16_t), wLen, fSpk);
            }
        }
        std::fclose(fSpk);
    }

    ::munmap(mapped, (size_t)fileBytes);
    ::close(fd);

    cout << "process_reextractspikes_stderiv: groups=" << nGroups
         << "  sdiffOrder=" << (int)a.sdiffOrder << endl;
    for (int g = 0; g < nGroups; ++g) {
        if (skip[g]) { cout << "  group " << (g+1) << "  skipped" << endl; continue; }
        cout << "  group " << (g+1)
             << "  masked=" << maskBy[g].size()
             << "  rejected-by-mask=" << nMasked[g]
             << "  accepted=" << nAccepted[g] << endl;
    }
    return 0;
}
