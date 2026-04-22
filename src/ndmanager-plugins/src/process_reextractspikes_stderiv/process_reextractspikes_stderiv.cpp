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
 *   Thresholds are computed internally from the stderiv signal over the
 *   window [threshStartByte, threshStartByte + threshSizeBytes) using the
 *   Quiroga (2004) robust formula:
 *       thr = thresholdFactor × 4 × median(|stderiv|) / 0.6745
 *   This matches process_extractspikes_stderiv exactly, so the bash
 *   wrapper supplies the same -f / -B / -Z arguments.
 *
 * The detection engine then runs on the stderiv shadow signal, and
 * waveform extraction (Pass 2) reads from the original raw .fil.
 *
 * Mask exclusion is applied to the detected peak timestamp, not to the
 * stderiv signal — so the mask window refers to ORIGINAL spike
 * timestamps, consistent with process_reextractspikes.
 ***************************************************************************/

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include "process_reextractspikes_stderiv.h"

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
// Ported unchanged from process_extractspikes_stderiv::computeSDiff.
// =========================================================================
static double computeSDiff(const int16_t *rec,
                            const int     *chanList,
                            int ci, int nChanGrp,
                            SdiffOrder order)
{
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
    case SDIFF_ALLPAIRS:
    default: {
        if (nChanGrp == 1) return 0.0;
        double s = 0.0;
        for (int j = 0; j < nChanGrp; ++j) s += rec[chanList[j]];
        return (double)nChanGrp * val - s;
    }
    }
}

// Temporal first-difference at time t: stderivAt(t) = sdiff(t) - sdiff(t-1)
static inline double stderivAt(const int16_t *data,
                                int64_t t, int nChanTot,
                                const int *chans, int ci, int nChanGrp,
                                SdiffOrder order)
{
    const int16_t *recT    = data + t       * nChanTot;
    const int16_t *recTm1  = data + (t - 1) * nChanTot;
    return computeSDiff(recT,   chans, ci, nChanGrp, order)
         - computeSDiff(recTm1, chans, ci, nChanGrp, order);
}

// =========================================================================
// Compute per-group / per-channel thresholds from the stderiv signal.
// =========================================================================
static void computeThresholds(const int16_t *data,
                               int64_t nSamples,
                               int nChanTot,
                               off_t startByte, off_t sizeBytes,
                               const vector<vector<int>> &chGroups,
                               SdiffOrder order,
                               double factor,
                               vector<vector<double>> &outThresh,
                               bool verbose)
{
    const int64_t startSample = startByte / ((int64_t)nChanTot * (int64_t)sizeof(int16_t));
    int64_t       nWinSamples = sizeBytes / ((int64_t)nChanTot * (int64_t)sizeof(int16_t));
    if (startSample < 1) {
        // We need t-1 for temporal diff — skip sample 0.
        nWinSamples = std::max<int64_t>(0, nWinSamples - (1 - startSample));
    }
    const int64_t tLo = std::max<int64_t>(1, startSample);
    const int64_t tHi = std::min<int64_t>(nSamples, tLo + nWinSamples);
    if (tHi <= tLo) {
        cerr << "error: threshold window has no samples "
                "(startByte=" << startByte << " sizeBytes=" << sizeBytes
             << " nSamples=" << nSamples << ")" << endl;
        std::exit(1);
    }

    // Decimate to keep memory bounded (~32 MB cap).
    const int64_t maxKept = 4000000;
    const int     stride  = (tHi - tLo > maxKept)
                            ? (int)((tHi - tLo) / maxKept) + 1
                            : 1;

    const int nG = (int)chGroups.size();
    outThresh.assign(nG, {});
    vector<vector<vector<double>>> absBuf(nG);
    for (int g = 0; g < nG; ++g) {
        absBuf[g].resize(chGroups[g].size());
        outThresh[g].resize(chGroups[g].size(), 0.0);
    }

    for (int64_t t = tLo; t < tHi; ++t) {
        if ((t - tLo) % stride != 0) continue;
        for (int g = 0; g < nG; ++g) {
            const int nCG = (int)chGroups[g].size();
            for (int ci = 0; ci < nCG; ++ci)
                absBuf[g][ci].push_back(
                    std::fabs(stderivAt(data, t, nChanTot,
                                         chGroups[g].data(), ci, nCG, order)));
        }
    }

    for (int g = 0; g < nG; ++g) {
        for (size_t ci = 0; ci < chGroups[g].size(); ++ci) {
            auto &v = absBuf[g][ci];
            if (v.empty()) {
                cerr << "error: empty threshold window for g=" << g+1
                     << " ci=" << ci << endl;
                std::exit(1);
            }
            std::sort(v.begin(), v.end());
            const double med = v[v.size() / 2];
            // Quiroga (2004): thr = factor × 4 × σ_n,   σ_n = median(|x|)/0.6745
            outThresh[g][ci] = factor * 4.0 * med / 0.6745;
            if (verbose)
                cerr << "  [sdiff thr] g=" << (g+1)
                     << " ch=" << chGroups[g][ci]
                     << "  median|stderiv|=" << med
                     << "  thr=" << outThresh[g][ci] << endl;
        }
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
// Argument parsing
// =========================================================================
static void usage(const char *name)
{
    cerr << programVersion << endl;
    cerr << "usage: " << name
         << " -n nCh -c channels -w wLen -p peak -l pslen -r ref"
            " -f factor -B startByte -Z sizeBytes"
            " [-d sdiffOrder] [-m maskBase] [-M maskHalf] [-i in.fil] [-v]"
            " basename" << endl;
}

static bool parseArgs(int argc, char **argv, ReextractStderivArgs &a)
{
    string inputOverride;
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
        else if (arg == "-m" && i+1 < argc) a.maskBaseFileName   = argv[++i];
        else if (arg == "-M" && i+1 < argc) a.maskHalfWidth      = std::atoll(argv[++i]);
        else if (arg == "-i" && i+1 < argc) inputOverride        = argv[++i];
        else if (!arg.empty() && arg[0] != '-') a.outputBaseFileName = arg;
        else { cerr << "error: unknown option '" << arg << "'" << endl; return false; }
    }
    if (a.outputBaseFileName.empty()) return false;
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
                      chGroups, a.sdiffOrder, a.thresholdFactor,
                      thr, a.verbose);

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
            if (t - lastAccepted < refPer) continue;

            int hitCi = -1; bool hitNeg = false;
            for (int ci = 0; ci < nChanG; ++ci) {
                const double v  = stderivAt(data, t, nChanT, chans.data(),
                                             ci, nChanG, ord);
                if (std::fabs(v) >= std::fabs(tg[ci])) {
                    hitCi = ci; hitNeg = (v < 0.0); break;
                }
            }
            if (hitCi < 0) continue;

            // Local extremum on the stderiv signal.
            int64_t peakT = t;
            double  peakV = stderivAt(data, t, nChanT, chans.data(),
                                       hitCi, nChanG, ord);
            const int64_t pEnd = std::min<int64_t>(t + psl, nSamples);
            for (int64_t s = t + 1; s < pEnd; ++s) {
                const double v = stderivAt(data, s, nChanT, chans.data(),
                                            hitCi, nChanG, ord);
                if ((hitNeg && v < peakV) || (!hitNeg && v > peakV)) {
                    peakV = v; peakT = s;
                }
            }
            if (peakT <= 1 || peakT >= nSamples - 1) continue;

            const double pb = stderivAt(data, peakT - 1, nChanT,
                                         chans.data(), hitCi, nChanG, ord);
            const double pa = stderivAt(data, peakT + 1, nChanT,
                                         chans.data(), hitCi, nChanG, ord);
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
    // first-difference with sdiff[-1]=0 boundary), and the .pcaD basis is
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
    for (int g = 0; g < nGroups; ++g) {
        const vector<int> &chans = chGroups[g];
        const int nChanG = (int)chans.size();
        const auto &res  = resPerGroup[g];

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
                                                  ci, nChanG, a.sdiffOrder);
                        int iv = (int)std::llround(sd);
                        if (iv >  32767) iv =  32767;
                        if (iv < -32768) iv = -32768;
                        sdWav[(size_t)s * nChanG + ci] = (int16_t)iv;
                    }
                }

                // Step 2 — temporal first-difference, in-place into buf[].
                // Boundary: sdiff[-1] = 0, matching process_extractspikes_
                // stderiv::Pass 2 exactly.  Walking prev[] forward preserves
                // the unclamped sdiff for the next iteration's subtrahend
                // (do not use the already-clamped output).
                {
                    vector<int16_t> prev((size_t)nChanG, 0);
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
    for (int g = 0; g < nGroups; ++g)
        cout << "  group " << (g+1)
             << "  masked=" << maskBy[g].size()
             << "  rejected-by-mask=" << nMasked[g]
             << "  accepted=" << nAccepted[g] << endl;
    return 0;
}
