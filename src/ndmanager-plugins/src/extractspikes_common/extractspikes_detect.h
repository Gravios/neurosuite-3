// extractspikes_detect.h — what process_extractspikes_sdiff and
// process_extractspikes_stderiv share: the peak search on a detection buffer,
// the refractory rule, the -c channel list, and the argument checks.
//
// Each tool used to carry its own copy of these (about 340 lines, identical
// in the two, themselves "verbatim from process_extractspikes").  The
// functions are unchanged; the three that read the tool's `arguments` struct
// are templates over it, so the two structs need not be the same type.
//
// The peak helpers work on an interleaved int16 buffer (nChanTot values per
// sample) and the previous buffer, so a peak at a buffer boundary is found in
// either.  An index into the buffer is sample*nChanTot (+ channel).
#ifndef EXTRACTSPIKES_DETECT_H
#define EXTRACTSPIKES_DETECT_H

#include <sys/types.h>   // off_t
#include <cmath>         // abs(float), abs(double)
#include <cstdlib>       // abs(int), atoi, exit
#include <cstring>       // strsep, strdupa
#include <cstdio>
#include <iostream>

#ifndef GROUP_SEPARATOR
#  define GROUP_SEPARATOR   ":"
#endif
#ifndef CHANNEL_SEPARATOR
#  define CHANNEL_SEPARATOR ","
#endif

namespace extractspikes {

using std::cerr;
using std::endl;
// All the std::abs overloads, as under the tools' `using namespace std`: a
// float peak value (getNegativePeakFullId, getPeakFullId) must not go through
// ::abs(int), which truncates it and changes which spikes pass the threshold.
using std::abs;

// ── Peak search ─────────────────────────────────────────────────────────────

inline bool isRealPeak(const double peak,
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

inline off_t getNegativePeakFullId(const int maxID, const short *cur_buf,
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

inline off_t getPeakFullId(const int maxID, const short *cur_buf, const int chanId,
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

inline int lookForMaxNegative(const short *buf, const int start, const int stop,
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

inline int lookForMax(const short *buf, const int start, const int stop,
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

inline int searchSpikeInChannels(const short *cur_buf, const int nbChanTot,
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

// ── Arguments ───────────────────────────────────────────────────────────────
// Args is the tool's `arguments` struct; only the fields named are read.

template <class Args>
inline bool isRefractoryPeriod(off_t lastId, off_t curId, const Args &args)
{
    if(lastId < 0) return false;
    return (curId - lastId) / (off_t)args.totalChannelNumber
           <= (off_t)args.refractoryPeriod;
}

inline double getThresholdFromChan(int chanId, int nChanGrp,
                             const int *channelList, const double *thresholds)
{
    for(int i = 0; i < nChanGrp; i++)
        if(channelList[i] == chanId) return thresholds[i];
    cerr << "getThresholdFromChan: channel " << chanId << " not found." << endl;
    return -1.0;
}

template <class Args>
inline int getChannelsFromArg(int *channelNb_group, int **channelList,
                              const Args &args)
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

inline bool checkChanAndThres(int ** /*cList*/, int *cNb, int nbC, int *tNb, int nbT)
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

template <class Args>
inline bool checkInputs(const Args &a, int buf_sz, const FILE *fp)
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

} // namespace extractspikes

#endif // EXTRACTSPIKES_DETECT_H
