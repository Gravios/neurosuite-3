/***************************************************************************
 * process_reextractspikes.cpp
 *
 * Re-extraction spike detector with .res-based masking and per-group
 * user-supplied thresholds.  See process_reextractspikes.h for the pipeline
 * contract and I/O conventions.
 *
 * Detection engine
 * ================
 * Unlike process_extractspikes, we do not stream the .fil through a chunked
 * buffer state machine.  Instead, we mmap the .fil and treat it as a
 * zero-copy const int16 array of shape [nSamples][nChan].  The detection
 * loop per group is then:
 *
 *   for t in [peakSample, nSamples - (spikeLength - peakSample)):
 *     if (t - lastAccepted) < refractoryPeriod:  skip
 *     over = firstChannelOverThreshold(t, grp)
 *     if over == none:                           skip
 *     peak = localExtremum(over, [t, t + peakSearchLength))
 *     if !isRealPeak(peak):                      skip
 *     if isMasked(peak.t, maskHalfWidth):        skip and advance
 *     accept peak.t, update lastAccepted = peak.t, advance past refractory
 *
 * Groups are processed in parallel via OpenMP.  The mmap'd buffer is
 * strictly read-only during the scan, so no synchronisation is required
 * (per-group state — lastAccepted, output vector — is owned by one thread).
 *
 * Waveform extraction
 * ===================
 * After detection completes for all groups, each group's accepted
 * timestamps are already sorted chronologically (produced by a serial
 * forward scan).  For each timestamp we extract a spikeLength-sample
 * window centered peakSample samples after the start, picking off only
 * the group's channels into the destination buffer.  One pass per group;
 * mmap makes seeks free.
 *
 * On file-boundary robustness
 * ===========================
 * The detection loop restricts t to the range where a full peakSearchLength
 * window and a full [t - timeBeforeSpike, t - timeBeforeSpike + spikeLength)
 * waveform both fit within the file.  This trades a few spikes at each
 * end of the file for a dramatic simplification over the original
 * cross-buffer state machine.
 *
 * isRealPeak behavior mirrors process_extractspikes exactly to keep
 * semantic compatibility with downstream tools.
 ***************************************************************************/

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include "process_reextractspikes.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
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

using std::string;
using std::vector;
using std::cerr;
using std::cout;
using std::endl;

static const char *programVersion = "process_reextractspikes 1.0 (2026-04)";

// =========================================================================
// Detection helpers
// =========================================================================
bool isRealPeak(double peak, double beforePeak, double afterPeak)
{
    // Semantic-equivalent port of process_extractspikes::isRealPeak.
    if (peak == 0.0) {
        if ((beforePeak > 0 && afterPeak < 0) ||
            (beforePeak < 0 && afterPeak > 0)) return false;
    } else if (peak > 0) {
        if (beforePeak > peak || afterPeak > peak) return false;
    } else {
        if (beforePeak < peak || afterPeak < peak) return false;
    }
    return true;
}

bool isMasked(const std::vector<int64_t> &sortedMask,
              int64_t t, int64_t halfWidth)
{
    if (sortedMask.empty() || halfWidth < 0) return false;
    // lower_bound — first element >= t - halfWidth.  If that element is
    // also <= t + halfWidth, we are within the mask window.
    const int64_t lo = t - halfWidth;
    const int64_t hi = t + halfWidth;
    auto it = std::lower_bound(sortedMask.begin(), sortedMask.end(), lo);
    return (it != sortedMask.end() && *it <= hi);
}

// =========================================================================
// Argument parsing — same flag set as process_extractspikes plus -m / -M
// =========================================================================
static void usage(const char *name, bool full = false)
{
    cerr << programVersion << endl;
    cerr << "usage: " << name << " [options] basename" << endl;
    if (!full) {
        cerr << "       (type '" << name << " -h' for details)" << endl;
        return;
    }
    cerr << "  basename        session stem for .fil input and .res/.spk output" << endl;
    cerr << "  -n nChannels    total channels in .fil" << endl;
    cerr << "  -c channels     per-group channel lists  (e.g. 0,1,2:4,5,7)" << endl;
    cerr << "  -t thresholds   per-group thresholds     (same shape as -c)" << endl;
    cerr << "  -w length       samples per waveform" << endl;
    cerr << "  -p peak         peak sample index within waveform (1-based)" << endl;
    cerr << "  -l length       peak-search window length (samples)" << endl;
    cerr << "  -r samples      refractory period (samples)" << endl;
    cerr << "  -m maskBase     stem for .res.N mask files (default: basename)" << endl;
    cerr << "  -M halfWidth    mask half-width in samples (default: refractory)" << endl;
    cerr << "  -i inputFile    input .fil (default: basename.fil)" << endl;
    cerr << "  -v              verbose" << endl;
    cerr << "  -h              show this help" << endl;
}

static bool parseArgs(int argc, char **argv, ReextractArgs &a)
{
    string inputOverride;
    for (int i = 1; i < argc; ++i) {
        const string arg = argv[i];
        if (arg == "-h" || arg == "--help") { usage(argv[0], true); exit(0); }
        else if (arg == "-v") a.verbose = true;
        else if (arg == "-n" && i+1 < argc) a.totalChannelNumber = atoi(argv[++i]);
        else if (arg == "-c" && i+1 < argc) a.channelList        = argv[++i];
        else if (arg == "-t" && i+1 < argc) a.thresList          = argv[++i];
        else if (arg == "-w" && i+1 < argc) a.spikeLength        = atoi(argv[++i]);
        else if (arg == "-p" && i+1 < argc) a.timeBeforeSpike    = atoi(argv[++i]) - 1;
        else if (arg == "-l" && i+1 < argc) a.peakSearchLength   = atoi(argv[++i]);
        else if (arg == "-r" && i+1 < argc) a.refractoryPeriod   = atoi(argv[++i]);
        else if (arg == "-m" && i+1 < argc) a.maskBaseFileName   = argv[++i];
        else if (arg == "-M" && i+1 < argc) a.maskHalfWidth      = atoll(argv[++i]);
        else if (arg == "-i" && i+1 < argc) inputOverride        = argv[++i];
        else if (!arg.empty() && arg[0] != '-') a.outputBaseFileName = arg;
        else { cerr << "error: unknown option '" << arg << "'" << endl; return false; }
    }

    if (a.outputBaseFileName.empty()) {
        cerr << "error: missing session basename" << endl; return false;
    }
    if (a.maskBaseFileName.empty())
        a.maskBaseFileName = a.outputBaseFileName;
    a.inputFilePath = !inputOverride.empty()
                      ? inputOverride
                      : a.outputBaseFileName + ".fil";
    if (a.maskHalfWidth < 0)
        a.maskHalfWidth = a.refractoryPeriod;

    if (a.totalChannelNumber <= 0 || a.channelList.empty() ||
        a.thresList.empty() || a.spikeLength <= 0 ||
        a.timeBeforeSpike < 0 || a.peakSearchLength <= 0 ||
        a.refractoryPeriod < 0) {
        cerr << "error: missing mandatory argument(s)" << endl;
        return false;
    }
    return true;
}

// Split "a,b,c:d,e:f" into a vector of vectors.
template <class T>
static vector<vector<T>> splitGroups(const string &s, T(*cvt)(const char*))
{
    vector<vector<T>> out;
    string cur;
    std::stringstream ss(s);
    while (std::getline(ss, cur, ':')) {
        vector<T> g;
        std::stringstream gs(cur);
        string tok;
        while (std::getline(gs, tok, ','))
            if (!tok.empty()) g.push_back(cvt(tok.c_str()));
        out.push_back(std::move(g));
    }
    return out;
}

static int   asInt   (const char *s) { return atoi(s); }
static double asDouble(const char *s){ return atof(s); }

// =========================================================================
// Mask loader — reads .res.N int64 timestamps; missing file = empty mask.
// =========================================================================
static vector<int64_t> loadMaskGroup(const string &maskBase, int grp1Based)
{
    std::ostringstream p;
    p << maskBase << "." << SPIKE_TIME_OUT_EXT << "." << grp1Based;
    const string path = p.str();

    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return {}; // no mask — caller gets empty vector, all candidates pass

    fseeko(f, 0, SEEK_END);
    const off_t sz = ftello(f);
    fseeko(f, 0, SEEK_SET);
    if (sz <= 0 || sz % (off_t)sizeof(int64_t) != 0) {
        fclose(f);
        cerr << "warning: mask file '" << path
             << "' has invalid size (" << sz << " bytes); ignoring" << endl;
        return {};
    }
    vector<int64_t> out((size_t)(sz / (off_t)sizeof(int64_t)));
    const size_t n = (size_t)(sz / (off_t)sizeof(int64_t));
    if (fread(out.data(), sizeof(int64_t), n, f) != n) {
        fclose(f);
        cerr << "warning: short read on mask file '" << path << "'" << endl;
        return {};
    }
    fclose(f);
    std::sort(out.begin(), out.end()); // should already be sorted, defensive
    return out;
}

// =========================================================================
// main
// =========================================================================
int main(int argc, char **argv)
{
    ReextractArgs args;
    if (!parseArgs(argc, argv, args)) { usage(argv[0]); return 1; }

    // ── parse channel / threshold lists ───────────────────────────────────
    vector<vector<int>>    chGroups  = splitGroups<int>   (args.channelList, &asInt);
    vector<vector<double>> thrGroups = splitGroups<double>(args.thresList,   &asDouble);
    const int nGroups = (int)chGroups.size();
    if ((int)thrGroups.size() != nGroups) {
        cerr << "error: #channel groups (" << chGroups.size()
             << ") != #threshold groups (" << thrGroups.size() << ")" << endl;
        return 1;
    }
    for (int g = 0; g < nGroups; ++g) {
        if (chGroups[g].size() != thrGroups[g].size()) {
            cerr << "error: group " << g+1
                 << " has " << chGroups[g].size() << " channels vs "
                 << thrGroups[g].size() << " thresholds" << endl;
            return 1;
        }
        for (int c : chGroups[g])
            if (c < 0 || c >= args.totalChannelNumber) {
                cerr << "error: channel id " << c
                     << " out of range [0," << args.totalChannelNumber-1
                     << "]" << endl;
                return 1;
            }
    }

    // ── mmap input ────────────────────────────────────────────────────────
    const int fd = ::open(args.inputFilePath.c_str(), O_RDONLY);
    if (fd < 0) {
        cerr << "error: cannot open '" << args.inputFilePath
             << "': " << std::strerror(errno) << endl;
        return 1;
    }
    struct stat st{};
    if (fstat(fd, &st) != 0) {
        cerr << "error: fstat failed on '" << args.inputFilePath << "'" << endl;
        ::close(fd); return 1;
    }
    const off_t fileBytes = st.st_size;
    const int64_t bytesPerSample =
        (int64_t)args.totalChannelNumber * (int64_t)sizeof(int16_t);
    if (fileBytes <= 0 || fileBytes % bytesPerSample != 0) {
        cerr << "error: '" << args.inputFilePath << "' size (" << fileBytes
             << ") is not a multiple of nChan*2 (" << bytesPerSample << ")" << endl;
        ::close(fd); return 1;
    }
    const int64_t nSamples = fileBytes / bytesPerSample;
    void *mapped = ::mmap(nullptr, (size_t)fileBytes, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        cerr << "error: mmap failed on '" << args.inputFilePath
             << "': " << std::strerror(errno) << endl;
        ::close(fd); return 1;
    }
    const int16_t *data = static_cast<const int16_t *>(mapped);
    // Advise kernel of sequential read pattern to enable prefetching.
    ::madvise(mapped, (size_t)fileBytes, MADV_SEQUENTIAL);

    if (args.verbose) {
        cerr << programVersion << endl
             << "input            : " << args.inputFilePath
             << " (" << nSamples << " samples × " << args.totalChannelNumber
             << " channels)" << endl
             << "output base      : " << args.outputBaseFileName << endl
             << "mask base        : " << args.maskBaseFileName << endl
             << "mask half-width  : " << args.maskHalfWidth << " samples" << endl
             << "refractory       : " << args.refractoryPeriod << " samples" << endl
             << "peak search      : " << args.peakSearchLength << " samples" << endl
             << "waveform length  : " << args.spikeLength << " samples" << endl
             << "time before peak : " << (args.timeBeforeSpike + 1) << endl
             << "groups           : " << nGroups << endl;
    }

    // ── load per-group masks ──────────────────────────────────────────────
    vector<vector<int64_t>> maskByGroup(nGroups);
    for (int g = 0; g < nGroups; ++g) {
        maskByGroup[g] = loadMaskGroup(args.maskBaseFileName, g + 1);
        if (args.verbose) {
            cerr << "  group " << g+1
                 << ": loaded " << maskByGroup[g].size()
                 << " masked timestamps from "
                 << args.maskBaseFileName << "." << SPIKE_TIME_OUT_EXT
                 << "." << (g+1) << endl;
        }
    }

    // ── detection loop, per-group parallel ────────────────────────────────
    // Safe range for t:
    //   t must allow a peak-search window ahead and a full waveform behind.
    //   - waveform starts at t - timeBeforeSpike; needs timeBeforeSpike ≥ 0
    //     samples before t.
    //   - waveform ends at t - timeBeforeSpike + spikeLength - 1; needs
    //     spikeLength - timeBeforeSpike - 1 samples past t.
    //   - peak search looks forward up to peakSearchLength - 1 samples.
    // Take the stricter bound on each side:
    const int64_t safeLeft  = (int64_t)args.timeBeforeSpike;
    const int64_t safeRight = (int64_t)std::max(
        args.peakSearchLength - 1,
        args.spikeLength - args.timeBeforeSpike - 1);
    const int64_t tStart = safeLeft;
    const int64_t tEnd   = nSamples - safeRight;

    vector<vector<int64_t>> resPerGroup(nGroups);
    vector<int64_t>         nAccepted(nGroups, 0);
    vector<int64_t>         nMasked  (nGroups, 0);

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (int g = 0; g < nGroups; ++g) {
        const vector<int>    &chans   = chGroups [g];
        const vector<double> &thresh  = thrGroups[g];
        const vector<int64_t>&mask    = maskByGroup[g];
        const int             nChanG  = (int)chans.size();
        const int             nChanTot= args.totalChannelNumber;
        const int64_t         halfW   = args.maskHalfWidth;
        const int             refPer  = args.refractoryPeriod;
        const int             psl     = args.peakSearchLength;

        vector<int64_t> out;
        out.reserve((size_t)(nSamples / std::max(refPer, 1) + 1));

        int64_t lastAccepted = INT64_MIN / 2; // impossibly far in the past

        for (int64_t t = tStart; t < tEnd; ++t) {
            if (t - lastAccepted < refPer) continue;

            // Identify first channel crossing threshold at time t.
            int   hitCh    = -1;   // index into chans[]
            bool  hitNeg   = false;
            for (int ci = 0; ci < nChanG; ++ci) {
                const int    ch = chans[ci];
                const double v  = (double)data[t * nChanTot + ch];
                const double thr= std::fabs(thresh[ci]);
                if (std::fabs(v) >= thr) {
                    hitCh  = ci;
                    hitNeg = (v < 0.0);
                    break;
                }
            }
            if (hitCh < 0) continue;

            // Find local extremum on same channel within [t, t+psl).
            const int    absCh = chans[hitCh];
            int64_t      peakT = t;
            int16_t      peakV = data[t * nChanTot + absCh];
            const int64_t pEnd = std::min<int64_t>(t + psl, nSamples);
            for (int64_t s = t + 1; s < pEnd; ++s) {
                const int16_t v = data[s * nChanTot + absCh];
                if (( hitNeg && v < peakV) ||
                    (!hitNeg && v > peakV)) {
                    peakV = v;
                    peakT = s;
                }
            }

            // Peak must be a local extremum (matches extractspikes semantics).
            if (peakT <= 0 || peakT >= nSamples - 1) continue;
            const double pv = (double)data[peakT * nChanTot + absCh];
            const double pb = (double)data[(peakT - 1) * nChanTot + absCh];
            const double pa = (double)data[(peakT + 1) * nChanTot + absCh];
            if (!isRealPeak(pv, pb, pa)) continue;

            // Peak must still clear threshold at its extremum position.
            if (std::fabs(pv) < std::fabs(thresh[hitCh])) continue;

            // Peak must not fall within ± maskHalfWidth of any existing spike.
            if (isMasked(mask, peakT, halfW)) {
                ++nMasked[g];
                // Advance past the peak so we don't re-trigger on the same
                // over-threshold sample chain during the masked event.
                lastAccepted = peakT;
                t = peakT;
                continue;
            }

            // Peak must have a full waveform window within the file.
            const int64_t wavStart = peakT - args.timeBeforeSpike;
            const int64_t wavEnd   = wavStart + args.spikeLength;
            if (wavStart < 0 || wavEnd > nSamples) continue;

            out.push_back(peakT);
            lastAccepted = peakT;
            t = peakT;                 // refractory advances covers the rest
            ++nAccepted[g];
        }

        resPerGroup[g] = std::move(out);
    }

    // ── write per-group .res.N (int64) and .spk.N (int16) ─────────────────
    for (int g = 0; g < nGroups; ++g) {
        const vector<int>    &chans  = chGroups[g];
        const int             nChanG = (int)chans.size();
        const vector<int64_t>&res    = resPerGroup[g];

        std::ostringstream pRes, pSpk;
        pRes << args.outputBaseFileName << "." << SPIKE_TIME_OUT_EXT << "." << (g+1);
        pSpk << args.outputBaseFileName << "." << SPIKE_REC_OUT_EXT  << "." << (g+1);

        // .res — binary int64 timestamps, no header.
        FILE *fRes = fopen(pRes.str().c_str(), "wb");
        if (!fRes) {
            cerr << "error: cannot write " << pRes.str() << endl;
            ::munmap(mapped, (size_t)fileBytes); ::close(fd); return 1;
        }
        if (!res.empty())
            fwrite(res.data(), sizeof(int64_t), res.size(), fRes);
        fclose(fRes);

        // .spk — sample-major within each waveform: dst[s*nChanG + c].
        FILE *fSpk = fopen(pSpk.str().c_str(), "wb");
        if (!fSpk) {
            cerr << "error: cannot write " << pSpk.str() << endl;
            ::munmap(mapped, (size_t)fileBytes); ::close(fd); return 1;
        }
        if (!res.empty()) {
            const size_t wavLen = (size_t)args.spikeLength * (size_t)nChanG;
            vector<int16_t> buf(wavLen);
            for (int64_t ts : res) {
                const int64_t startSample = ts - args.timeBeforeSpike;
                for (int s = 0; s < args.spikeLength; ++s) {
                    const int16_t *frame =
                        data + (int64_t)(startSample + s) * args.totalChannelNumber;
                    for (int c = 0; c < nChanG; ++c)
                        buf[(size_t)s * nChanG + c] = frame[chans[c]];
                }
                fwrite(buf.data(), sizeof(int16_t), wavLen, fSpk);
            }
        }
        fclose(fSpk);
    }

    ::munmap(mapped, (size_t)fileBytes);
    ::close(fd);

    // ── report ────────────────────────────────────────────────────────────
    cout << "process_reextractspikes: groups=" << nGroups << endl;
    for (int g = 0; g < nGroups; ++g) {
        cout << "  group " << (g+1)
             << "  masked=" << maskByGroup[g].size()
             << "  rejected-by-mask=" << nMasked[g]
             << "  accepted=" << nAccepted[g] << endl;
    }
    return 0;
}
