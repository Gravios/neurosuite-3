/***************************************************************************
                   process_extractspikes_sdiff.cpp  v1.1
                   --------------------------------
    copyright  : (C) 2024  Gravios / NeuroSuite-3 contributors

 Architecture
 ============
 Instead of computing the spatial derivative on-the-fly inside modified peak-
 search functions (which requires threading a new computation through the
 complex cross-buffer state machine), this version precomputes a shadow
 buffer sdiff_cur that mirrors cur_buffer in layout — storing the derivative
 value at each channel position.  The proven detection engine functions from
 process_extractspikes (searchSpikeInChannels, lookForMaxNegative, lookForMax,
 getNegativePeakFullId, getPeakFullId) are then used unchanged on the shadow
 buffer.  This eliminates all classes of buffer-overrun and logic bugs that
 result from reimplementing the state machine.

 Waveform extraction (Pass 2) always reads the original unmodified .fil signal.

 Spatial-derivative orders
 -------------------------
  0  SDIFF_NONE       bypass
  1  SDIFF_FIRST      nearest-neighbour  s[i] = x[i] - x[i+1]
  2  SDIFF_LAPLACIAN  discrete Laplacian s[i] = x[i] - 0.5*(x[i-1]+x[i+1])
  3  SDIFF_ALLPAIRS   all-pairwise sum   s[i] = n*x[i] - sum_j(x[j])
                      (= n*(x[i]-mean); default; no probe-order requirement)
 ***************************************************************************/

#define _LARGEFILE_SOURCE64
#define _FILE_OFFSET_BITS 64

#include "process_extractspikes_sdiff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef __linux__
#  include <sys/sysinfo.h>
#endif
#ifdef _OPENMP
#  include <omp.h>
#endif

using namespace std;

static int  buffer_size     = BUFFER_CHANNEL_SIZE; // scaled by nChanTot in main
static bool verbose         = false;
static const char *program_version = "process_extractspikes_sdiff 1.1";

// =========================================================================
// Spatial derivative
// =========================================================================

double computeSDiff(const short *record,
                    const int   *chanList,
                    int          idx,
                    int          nChanGrp,
                    SdiffOrder   order)
{
    const double val = record[chanList[idx]];

    switch(order) {
    case SDIFF_NONE:
        return val;

    case SDIFF_FIRST:
        return (idx < nChanGrp - 1)
               ? val - record[chanList[idx + 1]]
               : val - record[chanList[idx - 1]];

    case SDIFF_LAPLACIAN:
        if(nChanGrp == 1) return val;
        if(idx == 0)            return val - record[chanList[1]];
        if(idx == nChanGrp - 1) return val - record[chanList[nChanGrp - 2]];
        return val - 0.5 * (record[chanList[idx - 1]] +
                             record[chanList[idx + 1]]);

    case SDIFF_ALLPAIRS:
    default: {
        if(nChanGrp == 1) return 0.0;
        double sum = 0.0;
        for(int j = 0; j < nChanGrp; j++) sum += record[chanList[j]];
        // = sum_{j!=i}(x[i]-x[j]) = n*x[i] - sum_j(x[j])
        return (double)nChanGrp * val - sum;
    }
    }
}

// =========================================================================
// fill_sdiff_buffer
// =========================================================================
// Precompute the spatial derivative into a shadow buffer (sdiff) that has
// the same interleaved layout as the raw buffer.
//
// For every time sample t and every group-channel (g, ci):
//   sdiff[t*nChanTot + chanList[g][ci]] = computeSDiff(raw + t*nChanTot, ...)
//   clamped to int16 range.
//
// Non-group channels are copied verbatim from raw (they are never read by
// the detection functions, but having valid values keeps valgrind happy).
//
// nSamples should be (rec_nb / nChanTot) + 1 so the one-record lookahead
// slot used by isRealPeak (buf[i+nChanTot+chan]) is also transformed.
// The caller is responsible for ensuring raw has at least nSamples*nChanTot
// valid elements.
static void fill_sdiff_buffer(const short *raw,
                               short       *sdiff,
                               int          nSamples,
                               int          nChanTot,
                               int          nGroups,
                               int        **channelList,
                               int         *channelNb_grp,
                               SdiffOrder   order)
{
    // Copy whole buffer so non-group channels have valid values.
    memcpy(sdiff, raw, (size_t)nSamples * (size_t)nChanTot * sizeof(short));

    if(order == SDIFF_NONE) return; // bypass: shadow == raw copy

    for(int t = 0; t < nSamples; t++) {
        const short *rawRec = raw   + (size_t)t * nChanTot;
              short *sdRec  = sdiff + (size_t)t * nChanTot;

        for(int g = 0; g < nGroups; g++) {
            const int  nCG   = channelNb_grp[g];
            const int *cList = channelList[g];
            for(int ci = 0; ci < nCG; ci++) {
                double sd = computeSDiff(rawRec, cList, ci, nCG, order);
                // clamp to int16 so the shadow buffer dtype matches raw
                int v = (int)round(sd);
                if(v >  32767) v =  32767;
                if(v < -32768) v = -32768;
                sdRec[cList[ci]] = (short)v;
            }
        }
    }
}

// =========================================================================
// Threshold computation
// =========================================================================
void computeSdiffThresholds(FILE       *fp,
                             off_t       startByte,
                             off_t       sizeByte,
                             int         nChanTot,
                             int         nGroups,
                             int       **channelList,
                             int        *channelNb_group,
                             SdiffOrder  order,
                             double      factor,
                             double    **outThresholds)
{
    if(verbose)
        cout << "  [sdiff thresholds] reading "
             << (double)sizeByte / (nChanTot * sizeof(short))
             << " samples from byte " << startByte << endl;

    fseeko(fp, startByte, SEEK_SET);

    const long long nSamples =
        sizeByte / ((long long)nChanTot * (long long)sizeof(short));
    if(nSamples <= 0) {
        cerr << "error: threshold window has zero samples." << endl;
        exit(1);
    }

    // Stream with stride to bound memory at ~32 MB regardless of window size.
    const long long maxKept  = 4000000LL;
    const int       stride   = (nSamples > maxKept)
                               ? (int)(nSamples / maxKept) + 1 : 1;
    const int       chunkSz  = 65536; // samples per I/O chunk
    vector<short>   chunk((size_t)chunkSz * nChanTot);

    vector<vector<vector<double>>> absBuf(nGroups);
    for(int g = 0; g < nGroups; g++) {
        absBuf[g].resize(channelNb_group[g]);
        for(int ci = 0; ci < channelNb_group[g]; ci++)
            absBuf[g][ci].reserve((size_t)(nSamples / stride + 1));
    }

    long long samplesRead = 0, samplesSeen = 0;
    while(samplesRead < nSamples) {
        long long toRead = min((long long)chunkSz, nSamples - samplesRead);
        size_t got = fread(chunk.data(), sizeof(short),
                            (size_t)(toRead * nChanTot), fp);
        long long gotSamples = (long long)got / nChanTot;

        for(long long t = 0; t < gotSamples; t++, samplesSeen++) {
            if(samplesSeen % stride != 0) continue;
            const short *rec = chunk.data() + t * nChanTot;
            for(int g = 0; g < nGroups; g++) {
                const int nCG    = channelNb_group[g];
                const int *cList = channelList[g];
                for(int ci = 0; ci < nCG; ci++)
                    absBuf[g][ci].push_back(
                        fabs(computeSDiff(rec, cList, ci, nCG, order)));
            }
        }
        samplesRead += gotSamples;
        if(gotSamples < toRead) break;
    }

    for(int g = 0; g < nGroups; g++) {
        for(int ci = 0; ci < channelNb_group[g]; ci++) {
            auto &v = absBuf[g][ci];
            if(v.empty()) {
                cerr << "error: no data for group " << g+1
                     << " channel index " << ci << endl;
                exit(1);
            }
            sort(v.begin(), v.end());
            const double med = v[v.size() / 2];
            // Quiroga (2004) threshold: thr = factor × 4 × σ_n
            // where σ_n = median(|x|) / 0.6745  (robust noise-amplitude estimate;
            // 0.6745 = Φ⁻¹(0.75) for a unit Gaussian).
            // factor is the user-supplied -f argument (default 3 in ndm_extractspikes_sdiff);
            // combined with 4 it matches process_medianthreshold's convention of
            // "threshold = factor × 4 × sigma_n".
            outThresholds[g][ci] = factor * 4.0 * med / 0.6745;
            if(verbose)
                cout << "  [sdiff] g=" << g+1
                     << " ch=" << channelList[g][ci]
                     << "  median|sdiff|=" << med
                     << "  thr=" << outThresholds[g][ci] << endl;
        }
    }
}

// =========================================================================
// Detection helpers — verbatim from process_extractspikes
// =========================================================================
// These operate on whatever short* buffer is passed.  During detection that
// is sdiff_cur/sdiff_prev; the functions are otherwise identical to the
// originals and have the same buffer-safety properties.

static bool isRealPeak(const double peak,
                        const double beforePeak,
                        const double afterPeak)
{
    if(peak == 0) {
        if(beforePeak > 0 && afterPeak < 0) return false;
        else if(beforePeak < 0 && afterPeak > 0) return false;
    } else if(peak > 0) {
        if(beforePeak > peak || afterPeak > peak) return false;
    } else if(peak < 0) {
        if(beforePeak < peak || afterPeak < peak) return false;
    }
    return true;
}

static off_t getNegativePeakFullId(const int maxID, const short *cur_buf,
                                    const bool isNegativeMax, const int chanId,
                                    const int prevBuf_maxID, const short *prev_buf,
                                    const bool prevBuffer_isNegativeMax,
                                    const int prevBuf_chanId, const int buf_size,
                                    const int nChanTot, const int nLoops,
                                    const double threshold,
                                    const double prevBuf_threshold,
                                    bool &isMaxInCurBuf)
{
    off_t peakFullId = -1;
    float peakVal, recBeforePeak = 0, recAfterPeak = 0;
    double thr;
    bool isPrevNull = false, isCurNull = false;

    if(maxID == -1 || chanId == -1)             isCurNull  = true;
    if(prevBuf_maxID == -1 || prevBuf_chanId == -1) {
        isMaxInCurBuf = true; isPrevNull = true;
    } else if(isCurNull) {
        isMaxInCurBuf = false;
    } else {
        if(prevBuffer_isNegativeMax &&
           prev_buf[prevBuf_maxID+prevBuf_chanId] <= -abs(prevBuf_threshold)) {
            if(isNegativeMax)
                isMaxInCurBuf = (cur_buf[maxID+chanId] <
                                  prev_buf[prevBuf_maxID+prevBuf_chanId]);
            else
                isMaxInCurBuf = false;
        } else {
            if(isNegativeMax) {
                isMaxInCurBuf = (cur_buf[maxID+chanId] <= -abs(threshold));
            } else {
                isMaxInCurBuf = (cur_buf[maxID+chanId] >
                                  prev_buf[prevBuf_maxID+prevBuf_chanId]);
            }
        }
    }

    if(isPrevNull && isCurNull) return -1;

    if(isMaxInCurBuf) {
        peakFullId = ((off_t)maxID) + ((off_t)nLoops)*((off_t)buf_size);
        peakVal    = cur_buf[maxID+chanId];
        thr        = threshold;
    } else {
        peakFullId = ((off_t)prevBuf_maxID) + ((off_t)(nLoops-1))*((off_t)buf_size);
        peakVal    = prev_buf[prevBuf_maxID+prevBuf_chanId];
        thr        = prevBuf_threshold;
    }

    if(isMaxInCurBuf) {
        recAfterPeak  = cur_buf[chanId+maxID+nChanTot];
        recBeforePeak = (maxID >= nChanTot)
            ? cur_buf[chanId+maxID-nChanTot]
            : prev_buf[buf_size-nChanTot+chanId];
    } else {
        recBeforePeak = prev_buf[prevBuf_chanId+prevBuf_maxID-nChanTot];
        recAfterPeak  = (prevBuf_maxID+nChanTot >= buf_size)
            ? cur_buf[prevBuf_chanId]
            : prev_buf[prevBuf_chanId+prevBuf_maxID+nChanTot];
    }

    if(isRealPeak(peakVal, recBeforePeak, recAfterPeak) &&
       abs(peakVal) >= abs(thr))
        return peakFullId;
    return -1;
}

static off_t getPeakFullId(const int maxID, const short *cur_buf, const int chanId,
                             const int prevBuf_maxID, const short *prev_buf,
                             const int prevBuf_chanId, const int buf_size,
                             const int nChanTot, const int nLoops,
                             const double threshold, const double prevBuf_threshold,
                             bool &isMaxInCurBuf)
{
    off_t  peakFullId = -1;
    float  peakVal, recBeforePeak = 0, recAfterPeak = 0;
    double thr;

    if(prevBuf_chanId > -1 && prevBuf_maxID > -1 &&
       abs(prev_buf[prevBuf_maxID+prevBuf_chanId]) >= abs(cur_buf[maxID+chanId])) {
        isMaxInCurBuf = false;
        peakFullId    = ((off_t)prevBuf_maxID)+((off_t)nLoops-1)*((off_t)buf_size);
        peakVal       = abs(prev_buf[prevBuf_maxID+prevBuf_chanId]);
        thr           = prevBuf_threshold;
    } else {
        isMaxInCurBuf = true;
        peakFullId    = ((off_t)maxID)+((off_t)nLoops)*((off_t)buf_size);
        peakVal       = abs(cur_buf[maxID+chanId]);
        thr           = threshold;
    }

    if(isMaxInCurBuf) {
        recAfterPeak  = abs(cur_buf[chanId+maxID+nChanTot]);
        recBeforePeak = (maxID >= nChanTot)
            ? abs(cur_buf[chanId+maxID-nChanTot])
            : abs(prev_buf[buf_size-nChanTot+chanId]);
    } else {
        recBeforePeak = abs(prev_buf[prevBuf_chanId+prevBuf_maxID-nChanTot]);
        recAfterPeak  = (prevBuf_maxID+nChanTot >= buf_size)
            ? abs(cur_buf[prevBuf_chanId])
            : abs(prev_buf[prevBuf_chanId+prevBuf_maxID+nChanTot]);
    }

    if(isRealPeak(peakVal, recBeforePeak, recAfterPeak) &&
       abs(peakVal) >= abs(thr))
        return peakFullId;
    return -1;
}

static int lookForMaxNegative(const short *buf, const int start, const int stop,
                               int &chanId, const int nChanGrp,
                               const int *channelList, const int nChanTot,
                               const double *thresholds, const short prevVal[],
                               bool &isNegativeMax)
{
    if(chanId < 0) { cerr << "lookForMaxNegative: negative chanId\n"; exit(1); }
    if(start > stop) { cerr << "lookForMaxNegative: start>stop\n"; exit(1); }

    int  maxID      = start;
    bool isFirstMax = true;
    isNegativeMax   = false;

    for(int c = 0; c < nChanGrp; c++) {
        int    chan      = channelList[c];
        int    chanMaxID = start, chanMinID = start;
        double thr       = abs(thresholds[c]);

        bool isMinPeak = isRealPeak(buf[start+chan], prevVal[chan],
                                     buf[start+nChanTot+chan]);
        bool isMaxPeak = isMinPeak;

        for(int i = start+nChanTot; i <= stop; i += nChanTot) {
            if(buf[i+chan] > buf[chanMaxID+chan] ||
               (!isMaxPeak && buf[i+chan] >= 0)) {
                if(isRealPeak(buf[i+chan], buf[i-nChanTot+chan],
                               buf[i+nChanTot+chan])) {
                    chanMaxID = i; isMaxPeak = true;
                }
            }
            if(buf[i+chan] < buf[chanMinID+chan] ||
               (!isMinPeak && buf[i+chan] <= 0)) {
                if(isRealPeak(buf[i+chan], buf[i-nChanTot+chan],
                               buf[i+nChanTot+chan])) {
                    chanMinID = i; isMinPeak = true;
                }
            }
        }

        if(isMinPeak && buf[chanMinID+chan] <= -thr) {
            if(isFirstMax || buf[chanMinID+chan] < buf[maxID+chanId]) {
                isFirstMax = false; isNegativeMax = true;
                chanId = chan; maxID = chanMinID;
            } else if(buf[chanMinID+chan] == buf[maxID+chanId]
                      && chanMinID < maxID) {
                isFirstMax = false; isNegativeMax = true;
                chanId = chan; maxID = chanMinID;
            }
        } else if(!isNegativeMax && isMaxPeak && buf[chanMaxID+chan] >= thr) {
            if(isFirstMax || buf[chanMaxID+chan] > buf[maxID+chanId]) {
                isFirstMax = false; chanId = chan; maxID = chanMaxID;
            } else if(buf[chanMaxID+chan] == buf[maxID+chanId]
                      && chanMaxID < maxID) {
                isFirstMax = false; chanId = chan; maxID = chanMaxID;
            }
        }
    }

    return isFirstMax ? -1 : maxID;
}

static int lookForMax(const short *buf, const int start, const int stop,
                       int &chanId, const int nChanGrp,
                       const int *channelList, const int nChanTot,
                       const short prevVal[])
{
    if(chanId < 0) { cerr << "lookForMax: negative chanId\n"; exit(1); }
    if(start > stop) { cerr << "lookForMax: start>stop\n"; exit(1); }

    int  maxID    = start;
    bool isMaxPeak = false;

    for(int c = 0; c < nChanGrp; c++) {
        int chan = channelList[c];
        isMaxPeak = isRealPeak(abs(buf[start+chan]), abs(prevVal[chan]),
                                abs(buf[start+nChanTot+chan]));
        for(int i = start+nChanTot; i <= stop; i += nChanTot)
            if(abs(buf[i+chan]) > abs(buf[maxID+chan]) || !isMaxPeak)
                if(isRealPeak(buf[i+chan], buf[i-nChanTot+chan],
                               buf[i+nChanTot+chan])) {
                    maxID = i; chanId = chan; isMaxPeak = true;
                }
    }

    return isMaxPeak ? maxID : -1;
}

static int searchSpikeInChannels(const short *cur_buf, const int nbChanTot,
                                  const int *channelList,
                                  const double *thresholds, bool &isNegValue)
{
    for(int i = 0; i < nbChanTot; i++) {
        if(abs(cur_buf[channelList[i]]) >= thresholds[i]) {
            isNegValue = (cur_buf[channelList[i]] < 0);
            return channelList[i];
        }
    }
    return -1;
}

// =========================================================================
// Utility helpers
// =========================================================================

bool isRefractoryPeriod(off_t lastId, off_t curId, const arguments &args)
{
    if(lastId < 0) return false;
    return (curId - lastId) / (off_t)args.totalChannelNumber
           <= (off_t)args.refractoryPeriod;
}

double getThresholdFromChan(int chanId, int nChanGrp,
                             const int *channelList, const double *thresholds)
{
    for(int i = 0; i < nChanGrp; i++)
        if(channelList[i] == chanId) return thresholds[i];
    cerr << "getThresholdFromChan: channel " << chanId << " not found." << endl;
    return -1.0;
}

int getChannelsFromArg(int *channelNb_group, int **channelList,
                        const arguments &args)
{
    char zero = '0';
    int  nbGroups = 0;
    char *groups  = strdupa(args.channelList);
    char *curGrp  = strsep(&groups, GROUP_SEPARATOR);

    while(curGrp != NULL) {
        channelNb_group[nbGroups] = 0;
        char *chan = strsep(&curGrp, CHANNEL_SEPARATOR);
        while(chan != NULL) {
            if(atoi(chan) < 1 && *chan != zero) {
                if(atoi(chan) < 0 || channelNb_group[nbGroups] > 0) {
                    cerr << "error: negative channel ID." << endl; exit(1);
                } else { break; }
            }
            if(atoi(chan) >= args.totalChannelNumber) {
                cerr << "error: channel " << atoi(chan)
                     << " >= nChannels (" << args.totalChannelNumber << ")." << endl;
                exit(1);
            }
            channelList[nbGroups][channelNb_group[nbGroups]] = atoi(chan);
            chan = strsep(&curGrp, CHANNEL_SEPARATOR);
            if(chan == NULL) { channelNb_group[nbGroups]++; break; }
            channelNb_group[nbGroups]++;
        }
        curGrp = strsep(&groups, GROUP_SEPARATOR);
        if(curGrp == NULL) { nbGroups++; break; }
        nbGroups++;
    }
    return nbGroups;
}

bool checkChanAndThres(int ** /*cList*/, int *cNb, int nbC, int *tNb, int nbT)
{
    if(nbC != nbT) {
        cerr << "error: groups mismatch channels=" << nbC
             << " thresholds=" << nbT << endl;
        return false;
    }
    for(int g = 0; g < nbC; g++) {
        if(cNb[g] != tNb[g]) {
            cerr << "error: group " << g
                 << " channels=" << cNb[g]
                 << " thresholds=" << tNb[g] << endl;
            return false;
        }
    }
    return true;
}

// =========================================================================
// Argument parsing
// =========================================================================

static void usage(const char *name)
{
    cerr << program_version << "\n"
         << "usage: " << name << " [options] basename\n"
         << "       (type '" << name << " -h' for details)\n";
    exit(1);
}

static void help(const char *name)
{
    cout << name << " -- spike detection via spatial-derivative preprocessing\n\n"
         << "usage: " << name << " [options] basename\n\n"
         << "  basename        session file base name (without extension)\n\n"
         << "Detection / waveform (same as process_extractspikes):\n"
         << "  -n nChannels    total number of channels in the file\n"
         << "  -c channels     grouped channel list (e.g. 0,1,2:4,5,7)\n"
         << "  -w waveformLen  samples per extracted waveform\n"
         << "  -p peakSample   position of peak within waveform (1-based)\n"
         << "  -r refractPer   minimum samples between spikes\n"
         << "  -l peakSearch   peak-search window length in samples\n\n"
         << "Threshold (computed internally from sdiff signal):\n"
         << "  -f factor       threshold = factor * 4*sigma  (Quiroga 2004)\n"
         << "  -B startByte    byte offset into .fil for noise window\n"
         << "  -Z sizeBytes    byte length of noise window\n\n"
         << "Spatial derivative:\n"
         << "  -d order        0=none  1=first-diff  2=Laplacian  3=allpairs (default)\n\n"
         << "  -v              verbose\n"
         << "  -h              this message\n\n"
         << "Order 3 (allpairs): s[i] = n*x[i] - sum_j(x[j])\n"
         << "  Proportional to x[i] - mean(x); does not require probe-order\n"
         << "  channel listing.  Provides maximum common-mode rejection.\n";
    exit(0);
}

void parseArgs(int argc, char **argv, arguments &a)
{
    a.totalChannelNumber       = 0;
    a.peakLength               = -1;
    a.refractoryPeriod         = -1;
    a.spikeLength              = -1;
    a.timeBeforeSpike          = -1;
    a.thresholdFactor          = -1.0;
    a.threshStartByte          = 0;
    a.threshSizeBytes          = 0;
    a.sdiffOrder               = SDIFF_ALLPAIRS; // default
    a.isDisableAbs             = true;
    a.isInputFileProvided      = false;
    a.isOutputBaseFileProvided = false;
    a.isPeakLengthProvided         = false;
    a.isRefractoryPeriodProvided   = false;
    a.isTotalChannelNumberProvided = false;
    a.isChannelListProvided        = false;
    a.isSpikeLengthProvided        = false;
    a.isTimeBeforeSpikeProvided    = false;
    a.isThresholdFactorProvided    = false;
    a.isThreshStartByteProvided    = false;
    a.isThreshSizeBytesProvided    = false;
    a.isSdiffOrderProvided         = false;

    if(argc < 2) usage(argv[0]);

    int i = 1;
    for(; i < argc; i++) {
        if(argv[i][0] != '-') break;
        if((int)strlen(argv[i]) < 2) usage(argv[0]);
        switch(argv[i][1]) {
        case 'h': help(argv[0]); break;
        case 'v': verbose = true; break;
        case 'a': a.isDisableAbs = false; break;
        case 'n': a.totalChannelNumber = atoi(argv[++i]);
                  a.isTotalChannelNumberProvided = true; break;
        case 'c': a.channelList = argv[++i];
                  a.isChannelListProvided = true; break;
        case 'w': a.spikeLength = atoi(argv[++i]);
                  a.isSpikeLengthProvided = true; break;
        case 'p': a.timeBeforeSpike = atoi(argv[++i]) - 1;
                  a.isTimeBeforeSpikeProvided = true; break;
        case 'r': a.refractoryPeriod = atoi(argv[++i]);
                  a.isRefractoryPeriodProvided = true; break;
        case 'l': a.peakLength = atoi(argv[++i]);
                  a.isPeakLengthProvided = true; break;
        case 'f': a.thresholdFactor = atof(argv[++i]);
                  a.isThresholdFactorProvided = true; break;
        case 'B': a.threshStartByte = (off_t)atoll(argv[++i]);
                  a.isThreshStartByteProvided = true; break;
        case 'Z': a.threshSizeBytes = (off_t)atoll(argv[++i]);
                  a.isThreshSizeBytesProvided = true; break;
        case 'd': { int o = atoi(argv[++i]);
                    if(o < 0 || o > 3) {
                        cerr << "error: -d must be 0, 1, 2, or 3." << endl;
                        exit(1);
                    }
                    a.sdiffOrder = static_cast<SdiffOrder>(o);
                    a.isSdiffOrderProvided = true; break; }
        default:  cerr << "error: unknown option '" << argv[i] << "'." << endl;
                  exit(1);
        }
    }

    if(i >= argc) { cerr << "error: missing session basename." << endl; exit(1); }
    a.outputBaseFileName = argv[i];
    a.isOutputBaseFileProvided = true;
    a.isInputFileProvided      = true;
    {
        size_t baseLen = strlen(argv[i]);
        a.inputFileName = new char[baseLen + 5]; // +4 for ".fil" +1 for NUL
        memcpy(a.inputFileName, argv[i], baseLen);
        memcpy(a.inputFileName + baseLen, ".fil", 5);
    }

    bool ok = true;
    if(!a.isTotalChannelNumberProvided) { cerr<<"error: missing -n\n"; ok=false; }
    if(!a.isChannelListProvided)        { cerr<<"error: missing -c\n"; ok=false; }
    if(!a.isSpikeLengthProvided)        { cerr<<"error: missing -w\n"; ok=false; }
    if(!a.isTimeBeforeSpikeProvided)    { cerr<<"error: missing -p\n"; ok=false; }
    if(!a.isRefractoryPeriodProvided)   { cerr<<"error: missing -r\n"; ok=false; }
    if(!a.isThresholdFactorProvided)    { cerr<<"error: missing -f\n"; ok=false; }
    if(!a.isThreshSizeBytesProvided)    { cerr<<"error: missing -Z\n"; ok=false; }
    if(!ok) exit(1);

    if(!a.isPeakLengthProvided) {
        cerr << "warning: -l not given; defaulting to waveformLen="
             << a.spikeLength << "\n";
        a.peakLength = a.spikeLength;
    }
}

bool checkInputs(const arguments &a, int buf_sz, const FILE *fp)
{
    if(!fp) {
        cerr << "error: cannot open '" << a.inputFileName << "'" << endl;
        return false;
    }
    if(a.peakLength < 1) {
        cerr << "error: peak search length < 1" << endl; return false;
    }
    if(a.peakLength * a.totalChannelNumber > buf_sz) {
        cerr << "error: peak search length > buffer" << endl; return false;
    }
    if(a.spikeLength > buf_sz / 2) {
        cerr << "error: waveform length > buffer/2" << endl; return false;
    }
    if(a.timeBeforeSpike > a.spikeLength) {
        cerr << "error: peak position > waveform length" << endl; return false;
    }
    if(a.timeBeforeSpike < 0) {
        cerr << "error: peak position == 0" << endl; return false;
    }
    if(a.timeBeforeSpike > a.peakLength) {
        cerr << "error: peak position > peak search length" << endl; return false;
    }
    if(a.refractoryPeriod < 0) {
        cerr << "error: negative refractory period" << endl; return false;
    }
    return true;
}

// =========================================================================
// main()
// =========================================================================
int main(int argc, char *argv[])
{
    arguments args;
    parseArgs(argc, argv, args);
    buffer_size *= args.totalChannelNumber;

    FILE *inputFile = fopen(args.inputFileName, "rb");
    if(!checkInputs(args, buffer_size, inputFile)) exit(1);

    if(verbose) {
        cout << "\n" << program_version << "\n"
             << "Input file   : " << args.inputFileName       << "\n"
             << "nChannels    : " << args.totalChannelNumber  << "\n"
             << "waveformLen  : " << args.spikeLength         << "\n"
             << "peakPos      : " << args.timeBeforeSpike+1   << "\n"
             << "peakSearch   : " << args.peakLength          << "\n"
             << "refractory   : " << args.refractoryPeriod    << "\n"
             << "threshFactor : " << args.thresholdFactor     << "\n"
             << "threshStart  : " << args.threshStartByte     << " B\n"
             << "threshSize   : " << args.threshSizeBytes     << " B\n"
             << "sdiffOrder   : " << (int)args.sdiffOrder     << "\n\n";
    }

    // ── channel layout ────────────────────────────────────────────────────
    int **channelList   = new int*[MAX_CHANNO];
    int  *channelNb_grp = new int[MAX_CHANNO];
    for(int i = 0; i < MAX_CHANNO; i++) {
        channelList[i]   = new int[MAX_CHANNO];
        channelNb_grp[i] = -1;
        for(int j = 0; j < MAX_CHANNO; j++) channelList[i][j] = -1;
    }
    const int nbGroups = getChannelsFromArg(channelNb_grp, channelList, args);

    // ── per-group thresholds (sdiff domain) ───────────────────────────────
    double **thresList   = new double*[MAX_CHANNO];
    int     *thresNb_grp = new int[MAX_CHANNO];
    for(int i = 0; i < MAX_CHANNO; i++) {
        thresList[i]    = new double[MAX_CHANNO];
        thresNb_grp[i]  = -1;
        for(int j = 0; j < MAX_CHANNO; j++) thresList[i][j] = -1.0;
    }

    computeSdiffThresholds(inputFile,
                            args.threshStartByte, args.threshSizeBytes,
                            args.totalChannelNumber,
                            nbGroups, channelList, channelNb_grp,
                            args.sdiffOrder, args.thresholdFactor, thresList);

    for(int g = 0; g < nbGroups; g++) thresNb_grp[g] = channelNb_grp[g];

    if(!checkChanAndThres(channelList, channelNb_grp, nbGroups,
                           thresNb_grp, nbGroups)) {
        cerr << "error: channel/threshold mismatch.\n"; exit(1);
    }

    for(int g = 0; g < nbGroups; g++) {
        cout << "Group " << g+1 << " sdiff thresholds: ";
        for(int ci = 0; ci < channelNb_grp[g]; ci++) {
            if(ci) cout << ",";
            cout << thresList[g][ci];
        }
        cout << "\n";
    }

    // ── buffer allocation ─────────────────────────────────────────────────
    const int timeAfterSpike = args.spikeLength - args.timeBeforeSpike - 1;

    // Raw signal buffers — used for Pass 2 waveform extraction.
    short *cur_buffer  = new short[buffer_size + args.totalChannelNumber];
    short *prev_buffer = new short[buffer_size];
    short *nextRec     = new short[args.totalChannelNumber];

    // Spatial-derivative shadow buffers — used for Pass 1 detection.
    // Same allocation sizes as raw buffers; fill_sdiff_buffer keeps them in
    // sync with the raw buffers every iteration.
    short *sdiff_cur  = new short[buffer_size + args.totalChannelNumber];
    short *sdiff_prev = new short[buffer_size];

    memset(prev_buffer, 0, sizeof(short) * buffer_size);
    memset(sdiff_prev,  0, sizeof(short) * buffer_size);

    // ── per-group detection state (mirrors process_extractspikes) ─────────
    int  *spkChanId              = new int[nbGroups];
    int  *prevBuffer_spkChanId   = new int[nbGroups];
    bool *isNegativeMax          = new bool[nbGroups];
    bool *prevBuffer_isNegMax    = new bool[nbGroups];
    bool *isMaxInCurBuf          = new bool[nbGroups];
    int  *maxId                  = new int[nbGroups];
    int  *prevBuffer_maxID       = new int[nbGroups];
    int  *recInPrevBuffer        = new int[nbGroups];
    int  *ignoredInNextBuffer    = new int[nbGroups];
    off_t*lastSpikeFullId        = new off_t[nbGroups];
    unsigned int *nSpikeTot      = new unsigned int[nbGroups];

    for(int g = 0; g < nbGroups; g++) {
        spkChanId[g]            = -1;
        prevBuffer_spkChanId[g] = -1;
        isNegativeMax[g]        = false;
        prevBuffer_isNegMax[g]  = false;
        isMaxInCurBuf[g]        = true;
        maxId[g]                = -1;
        prevBuffer_maxID[g]     = -1;
        recInPrevBuffer[g]      = 0;
        ignoredInNextBuffer[g]  = 0;
        lastSpikeFullId[g]      = -1;
        nSpikeTot[g]            = 0;
    }

    vector<vector<int64_t>> resTimestamps(nbGroups);
    {
        fseeko(inputFile, 0, SEEK_END);
        const long long fileSamples =
            ftello(inputFile) / (args.totalChannelNumber * (long long)sizeof(short));
        fseeko(inputFile, 0, SEEK_SET);
        const size_t maxSpikes = (args.refractoryPeriod > 0)
            ? (size_t)(fileSamples / args.refractoryPeriod) + 1
            : (size_t)fileSamples;
        for(int g = 0; g < nbGroups; g++) resTimestamps[g].reserve(maxSpikes);
    }

    vector<string> resFileNames(nbGroups);
    for(int g = 0; g < nbGroups; g++) {
        if(channelNb_grp[g] == 0) continue;
        ostringstream oss;
        oss << args.outputBaseFileName << "." << SPIKE_TIME_OUT_EXT << "." << (g+1);
        resFileNames[g] = oss.str();
    }

    // =======================================================================
    // PASS 1 — spike detection on the spatial-derivative shadow buffer
    // =======================================================================
    if(verbose) cout << "\n<<----- Pass 1: spike detection (sdiff shadow buffer)\n\n";

    fseeko(inputFile, 0, SEEK_SET);
    bool isLastLoop = false;
    unsigned long long rec_nb = 0, nbLoops = 0;

    while(!feof(inputFile) && !isLastLoop) {

        // ── fill raw buffer (identical to process_extractspikes) ──────────
        if(nbLoops == 0) {
            size_t _r = fread(nextRec, sizeof(short),
                               args.totalChannelNumber, inputFile);
            (void)_r;
        } else {
            memcpy(prev_buffer, cur_buffer, sizeof(short) * buffer_size);
            memcpy(sdiff_prev,  sdiff_cur,  sizeof(short) * buffer_size);
        }

        rec_nb = fread(cur_buffer + args.totalChannelNumber,
                        sizeof(short), buffer_size, inputFile);
        memcpy(cur_buffer, nextRec, sizeof(short) * args.totalChannelNumber);

        if(rec_nb < (unsigned long long)buffer_size || feof(inputFile)) {
            isLastLoop = true;
            nextRec    = nullptr;
            rec_nb    += args.totalChannelNumber;
        } else {
            memcpy(nextRec, cur_buffer + buffer_size,
                    sizeof(short) * args.totalChannelNumber);
        }

        // ── precompute sdiff shadow buffer ────────────────────────────────
        // Transform rec_nb/nChanTot samples plus the one lookahead record
        // (needed by isRealPeak's buf[i+nChanTot+chan] access).
        // cur_buffer is allocated buffer_size + nChanTot so the +1 is safe.
        const int nSamplesThisChunk =
            (int)((rec_nb + args.totalChannelNumber - 1) / args.totalChannelNumber);

        fill_sdiff_buffer(cur_buffer, sdiff_cur,
                           nSamplesThisChunk,
                           args.totalChannelNumber,
                           nbGroups, channelList, channelNb_grp,
                           args.sdiffOrder);

        // ── detection loop on sdiff buffers (verbatim from process_extractspikes)
#ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic)
#endif
        for(int grp = 0; grp < nbGroups; grp++) {
            if(channelNb_grp[grp] == 0) continue;

            const int nChanGrp = channelNb_grp[grp];
            const int *cList   = channelList[grp];
            const double *thr  = thresList[grp];

            int i = 0;
            if(nbLoops == 0)
                i = args.timeBeforeSpike * args.totalChannelNumber;
            else
                i = ignoredInNextBuffer[grp] * args.totalChannelNumber;

            ignoredInNextBuffer[grp] = 0;

            while(i < (int)rec_nb) {

                if(spkChanId[grp] == -1) {
                    spkChanId[grp] = searchSpikeInChannels(
                        &sdiff_cur[i], nChanGrp, cList, thr,
                        isNegativeMax[grp]);
                }

                if(spkChanId[grp] > -1) {

                    int maxEndSpike = i
                        + (args.peakLength - recInPrevBuffer[grp] - 1)
                        * args.totalChannelNumber;

                    // ── peak window overflows to next buffer ──────────────
                    if(maxEndSpike >= (int)rec_nb) {

                        std::vector<short> prevVals(args.totalChannelNumber);
                        if(i >= args.totalChannelNumber)
                            memcpy(prevVals.data(),
                                   sdiff_cur + i - args.totalChannelNumber,
                                   args.totalChannelNumber * sizeof(short));
                        else
                            memcpy(prevVals.data(),
                                   sdiff_prev + buffer_size - args.totalChannelNumber,
                                   args.totalChannelNumber * sizeof(short));

                        if(args.isDisableAbs) {
                            maxId[grp] = lookForMaxNegative(
                                sdiff_cur, i, (int)rec_nb - 1,
                                spkChanId[grp], nChanGrp, cList,
                                args.totalChannelNumber, thr,
                                prevVals.data(), isNegativeMax[grp]);
                            prevBuffer_isNegMax[grp] = isNegativeMax[grp];
                            isNegativeMax[grp] = false;
                        } else {
                            maxId[grp] = lookForMax(
                                sdiff_cur, i, (int)rec_nb - 1,
                                spkChanId[grp], nChanGrp, cList,
                                args.totalChannelNumber, prevVals.data());
                        }

                        if(maxId[grp] != -1) {
                            prevBuffer_maxID[grp]     = maxId[grp];
                            prevBuffer_spkChanId[grp] = spkChanId[grp];
                        }

                        recInPrevBuffer[grp] =
                            ((int)rec_nb - i) / args.totalChannelNumber;
                        maxId[grp] = -1;
                        i = buffer_size;

                    // ── peak window fully in current buffer ───────────────
                    } else {

                        std::vector<short> prevVals(args.totalChannelNumber);
                        if(i >= args.totalChannelNumber)
                            memcpy(prevVals.data(),
                                   sdiff_cur + i - args.totalChannelNumber,
                                   args.totalChannelNumber * sizeof(short));
                        else
                            memcpy(prevVals.data(),
                                   sdiff_prev + buffer_size - args.totalChannelNumber,
                                   args.totalChannelNumber * sizeof(short));

                        off_t  maxFullId     = -1;
                        bool   isKeepPrevMax = false;

                        if(args.isDisableAbs) {
                            maxId[grp] = lookForMaxNegative(
                                sdiff_cur, i, maxEndSpike,
                                spkChanId[grp], nChanGrp, cList,
                                args.totalChannelNumber, thr,
                                prevVals.data(), isNegativeMax[grp]);

                            double thr2 = getThresholdFromChan(
                                spkChanId[grp], nChanGrp, cList, thr);
                            double prevThr2 = (prevBuffer_spkChanId[grp] != -1)
                                ? getThresholdFromChan(prevBuffer_spkChanId[grp],
                                                        nChanGrp, cList, thr)
                                : -1.0;

                            maxFullId = getNegativePeakFullId(
                                maxId[grp], sdiff_cur,
                                isNegativeMax[grp], spkChanId[grp],
                                prevBuffer_maxID[grp], sdiff_prev,
                                prevBuffer_isNegMax[grp], prevBuffer_spkChanId[grp],
                                buffer_size, args.totalChannelNumber,
                                (int)nbLoops, thr2, prevThr2,
                                isMaxInCurBuf[grp]);
                        } else {
                            maxId[grp] = lookForMax(
                                sdiff_cur, i, maxEndSpike,
                                spkChanId[grp], nChanGrp, cList,
                                args.totalChannelNumber, prevVals.data());

                            double thr2 = getThresholdFromChan(
                                spkChanId[grp], nChanGrp, cList, thr);
                            double prevThr2 = (prevBuffer_spkChanId[grp] != -1)
                                ? getThresholdFromChan(prevBuffer_spkChanId[grp],
                                                        nChanGrp, cList, thr)
                                : -1.0;

                            maxFullId = getPeakFullId(
                                maxId[grp], sdiff_cur, spkChanId[grp],
                                prevBuffer_maxID[grp], sdiff_prev,
                                prevBuffer_spkChanId[grp],
                                buffer_size, args.totalChannelNumber,
                                (int)nbLoops, thr2, prevThr2,
                                isMaxInCurBuf[grp]);
                        }

                        if(maxFullId > -1) {
                            if(!isRefractoryPeriod(lastSpikeFullId[grp],
                                                    maxFullId, args)) {
                                if(!(isLastLoop && isMaxInCurBuf[grp] &&
                                     (maxId[grp] + timeAfterSpike *
                                      args.totalChannelNumber) >= (int)rec_nb)) {
                                    resTimestamps[grp].push_back(
                                        (int64_t)(maxFullId /
                                                   args.totalChannelNumber));
                                    lastSpikeFullId[grp] = maxFullId;
                                    nSpikeTot[grp]++;
                                }
                            }

                            if(isMaxInCurBuf[grp]) {
                                maxEndSpike = maxId[grp]
                                    + (args.peakLength - args.timeBeforeSpike - 1)
                                    * args.totalChannelNumber;
                                if(maxEndSpike >= buffer_size)
                                    ignoredInNextBuffer[grp] = 1
                                        + (maxEndSpike - buffer_size)
                                        / args.totalChannelNumber;
                            } else {
                                maxEndSpike = prevBuffer_maxID[grp]
                                    + (args.peakLength - args.timeBeforeSpike - 1)
                                    * args.totalChannelNumber;
                                if((maxEndSpike + args.totalChannelNumber)
                                   >= buffer_size) {
                                    maxEndSpike -= buffer_size;
                                    isKeepPrevMax = false;
                                } else {
                                    // Tail still in prev buffer — scan for
                                    // next spike in prev buffer context.
                                    int x = maxEndSpike + args.totalChannelNumber;
                                    maxId[grp]             = -1;
                                    isNegativeMax[grp]     = false;
                                    prevBuffer_spkChanId[grp] = -1;
                                    prevBuffer_maxID[grp]  = -1;
                                    spkChanId[grp]         = -1;

                                    while(x < buffer_size && spkChanId[grp] == -1) {
                                        spkChanId[grp] = searchSpikeInChannels(
                                            &sdiff_prev[x], nChanGrp, cList, thr,
                                            isNegativeMax[grp]);
                                        x += args.totalChannelNumber;
                                    }

                                    if(spkChanId[grp] != -1) {
                                        x -= args.totalChannelNumber;
                                        std::vector<short> pv2(args.totalChannelNumber);
                                        memcpy(pv2.data(),
                                               sdiff_prev + x - args.totalChannelNumber,
                                               args.totalChannelNumber * sizeof(short));

                                        if(args.isDisableAbs) {
                                            maxId[grp] = lookForMaxNegative(
                                                sdiff_prev, x, buffer_size - 1,
                                                spkChanId[grp], nChanGrp, cList,
                                                args.totalChannelNumber, thr,
                                                pv2.data(), isNegativeMax[grp]);
                                            prevBuffer_isNegMax[grp] = isNegativeMax[grp];
                                        } else {
                                            maxId[grp] = lookForMax(
                                                sdiff_prev, x, buffer_size - 1,
                                                spkChanId[grp], nChanGrp, cList,
                                                args.totalChannelNumber, pv2.data());
                                        }

                                        if(maxId[grp] != -1) {
                                            double pv3 = sdiff_prev[maxId[grp]
                                                - args.totalChannelNumber
                                                + spkChanId[grp]];
                                            double nv3;
                                            if((maxId[grp] + args.totalChannelNumber)
                                               == buffer_size)
                                                nv3 = sdiff_cur[spkChanId[grp]];
                                            else
                                                nv3 = sdiff_prev[maxId[grp]
                                                    + args.totalChannelNumber
                                                    + spkChanId[grp]];
                                            if(isRealPeak(
                                                sdiff_prev[maxId[grp]+spkChanId[grp]],
                                                pv3, nv3)) {
                                                prevBuffer_maxID[grp]     = maxId[grp];
                                                prevBuffer_spkChanId[grp] = spkChanId[grp];
                                            }
                                        }
                                        recInPrevBuffer[grp] =
                                            (buffer_size - x) / args.totalChannelNumber;
                                        isKeepPrevMax = true;
                                    }

                                    maxEndSpike = -args.totalChannelNumber;
                                }
                            }
                        }

                        i = maxEndSpike;

                        maxId[grp]          = -1;
                        isNegativeMax[grp]  = false;
                        isMaxInCurBuf[grp]  = true;

                        if(!isKeepPrevMax) {
                            spkChanId[grp]            = -1;
                            prevBuffer_spkChanId[grp] = -1;
                            recInPrevBuffer[grp]      = 0;
                            prevBuffer_maxID[grp]     = -1;
                            prevBuffer_isNegMax[grp]  = false;
                        }
                    }
                }

                i += args.totalChannelNumber;
            } // while i
        } // for grp

        nbLoops++;
    } // while !feof

    // ── write .res files ──────────────────────────────────────────────────
    for(int g = 0; g < nbGroups; g++) {
        if(channelNb_grp[g] == 0) continue;
        FILE *rf = fopen(resFileNames[g].c_str(), "wb");
        if(!rf) {
            fprintf(stderr, "error: cannot write '%s'\n",
                    resFileNames[g].c_str());
            exit(1);
        }
        if(!resTimestamps[g].empty())
            fwrite(resTimestamps[g].data(), sizeof(int64_t),
                    resTimestamps[g].size(), rf);
        fclose(rf);
    }

    cout << "Number of spikes (sdiff detection):\n";
    for(int g = 0; g < nbGroups; g++)
        cout << "  Group " << g+1 << " = " << nSpikeTot[g] << "\n";

    // free detection-pass buffers
    delete[] cur_buffer;   delete[] prev_buffer;
    delete[] sdiff_cur;    delete[] sdiff_prev;
    delete[] spkChanId;    delete[] prevBuffer_spkChanId;
    delete[] isNegativeMax; delete[] prevBuffer_isNegMax;
    delete[] isMaxInCurBuf;
    delete[] maxId;        delete[] prevBuffer_maxID;
    delete[] recInPrevBuffer; delete[] ignoredInNextBuffer;
    delete[] lastSpikeFullId; delete[] nSpikeTot;

    // =======================================================================
    // PASS 2 — waveform extraction from the ORIGINAL .fil signal
    // =======================================================================
    // Detection was in the sdiff domain; extracted waveforms are always
    // raw ADC amplitudes so downstream PCA/clustering sees unmodified data.
    if(verbose) cout << "\n<<----- Pass 2: waveform extraction (original signal)\n\n";

    const char *_memCapEnv = getenv("PROCESS_EXTRACTSPIKES_MEM_CAP_GB");
    const long long MEM_CAP =
        _memCapEnv ? (long long)(atof(_memCapEnv) * 1073741824LL)
                   : 2LL * 1073741824LL;

    struct SpikeEvent { off_t fileOffset; int grp; int origIdx; };

    vector<vector<short>> allWaveforms(nbGroups);
    vector<bool>          useInMemory(nbGroups, false);
    vector<string>        spkFileNames(nbGroups);
    vector<FILE*>         streamFiles(nbGroups, nullptr);

    size_t totalSpikes = 0;
    for(int g = 0; g < nbGroups; g++) totalSpikes += resTimestamps[g].size();
    vector<SpikeEvent> allEvents;
    allEvents.reserve(totalSpikes);

    for(int grp = 0; grp < nbGroups; grp++) {
        if(channelNb_grp[grp] == 0) continue;
        const int nCG    = channelNb_grp[grp];
        const int wavLen = args.spikeLength * nCG;
        const int nSpk   = (int)resTimestamps[grp].size();
        const long long wBytes = (long long)nSpk * wavLen * (long long)sizeof(short);

        ostringstream oss;
        oss << args.outputBaseFileName << "." << SPIKE_REC_OUT_EXT
            << "." << (grp+1);
        spkFileNames[grp] = oss.str();

        if(wBytes > MEM_CAP) {
            useInMemory[grp] = false;
            streamFiles[grp] = fopen(spkFileNames[grp].c_str(), "wb");
            if(!streamFiles[grp]) {
                fprintf(stderr,"error: cannot open '%s'\n",
                        spkFileNames[grp].c_str()); exit(1);
            }
        } else {
            useInMemory[grp] = true;
            allWaveforms[grp].resize((size_t)nSpk * wavLen);
        }

        for(int s = 0; s < nSpk; s++) {
            SpikeEvent ev;
            ev.fileOffset =
                ((off_t)(resTimestamps[grp][s] - args.timeBeforeSpike)
                 * (off_t)args.totalChannelNumber * (off_t)sizeof(short));
            ev.grp     = grp;
            ev.origIdx = s;
            allEvents.push_back(ev);
        }
    }

    sort(allEvents.begin(), allEvents.end(),
         [](const SpikeEvent &a, const SpikeEvent &b){
             return a.fileOffset < b.fileOffset; });

    {
        const int rawLen = args.spikeLength * args.totalChannelNumber;
        vector<short> frameBuf(rawLen);

        FILE *seqF = fopen(args.inputFileName, "rb");
        if(!seqF) {
            fprintf(stderr,"error: cannot reopen '%s'\n",
                    args.inputFileName); exit(1);
        }

        off_t filePos = 0;
        for(const SpikeEvent &ev : allEvents) {
            const int grp  = ev.grp;
            const int nCG  = channelNb_grp[grp];
            const int wLen = args.spikeLength * nCG;

            if(ev.fileOffset > filePos) {
                // skip forward
                off_t gap = ev.fileOffset - filePos;
                vector<char> skip(65536);
                while(gap > 0) {
                    size_t chunk = (gap < 65536) ? (size_t)gap : 65536;
                    size_t got2  = fread(skip.data(), 1, chunk, seqF);
                    if(got2 == 0) break;
                    gap     -= (off_t)got2;
                    filePos += (off_t)got2;
                }
            }

            size_t got = fread(frameBuf.data(), sizeof(short), rawLen, seqF);
            if((int)got != rawLen) break;
            filePos += (off_t)rawLen * (off_t)sizeof(short);

            const int *cL = channelList[grp];
            if(useInMemory[grp]) {
                short *dst = allWaveforms[grp].data()
                             + (size_t)ev.origIdx * wLen;
                for(int s = 0; s < args.spikeLength; s++) {
                    const short *fr = frameBuf.data() + s * args.totalChannelNumber;
                    for(int c = 0; c < nCG; c++)
                        dst[s*nCG+c] = fr[cL[c]];
                }
            } else {
                for(int s = 0; s < args.spikeLength; s++) {
                    const short *fr = frameBuf.data() + s * args.totalChannelNumber;
                    for(int c = 0; c < nCG; c++)
                        fwrite(&fr[cL[c]], sizeof(short), 1, streamFiles[grp]);
                }
            }
        }
        fclose(seqF);
    }

    for(int grp = 0; grp < nbGroups; grp++)
        if(streamFiles[grp]) fclose(streamFiles[grp]);

    for(int grp = 0; grp < nbGroups; grp++) {
        if(channelNb_grp[grp] == 0 || !useInMemory[grp]) continue;
        const int nCG    = channelNb_grp[grp];
        const int wavLen = args.spikeLength * nCG;
        const int nSpk   = (int)(allWaveforms[grp].size() / wavLen);
        FILE *sf = fopen(spkFileNames[grp].c_str(), "wb");
        if(!sf) {
            fprintf(stderr,"error: cannot open '%s'\n",
                    spkFileNames[grp].c_str()); exit(1);
        }
        if(nSpk > 0)
            fwrite(allWaveforms[grp].data(), sizeof(short),
                    (size_t)nSpk * wavLen, sf);
        fclose(sf);
    }

    if(verbose) cout << "\n End of process_extractspikes_sdiff ----->>]\n\n";

    fclose(inputFile);
    for(int i = 0; i < MAX_CHANNO; i++) {
        delete[] channelList[i];
        delete[] thresList[i];
    }
    delete[] channelList; delete[] channelNb_grp;
    delete[] thresList;   delete[] thresNb_grp;
    return 0;
}
