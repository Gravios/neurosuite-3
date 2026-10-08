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

#include "process_extractspikes_stderiv.h"
#include "progressbar.h"      // BlockProgress - defrag-style stage progress
#include "sdiff_pairs.h"      // shared SDIFF_CUSTOM pair-pattern parser

#include <algorithm>
#include <numeric>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#ifdef __linux__
#  include <sys/sysinfo.h>
#endif
#ifdef _OPENMP
#  include <omp.h>
#endif

// patch86: for -R mode (mmap .fil)
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace std;

static int  buffer_size     = BUFFER_CHANNEL_SIZE; // scaled by nChanTot in main
static bool verbose         = false;

// Per-run cross-buffer state for temporal first-difference.
// Stores the last time-sample's *spatial* derivative from the previous
// chunk so the t=0 temporal difference at the next chunk is continuous.
static std::vector<short> g_prev_sdiff;

// SDIFF_CUSTOM per-group, per-channel partner maps.  g_custom_partner[grp][ci]
// is the within-group index that output channel ci of group grp is differenced
// against, so s[ci] = x[ci] - x[partner[ci]].  Built once from the colon-separated
// -P spec (one segment per group, aligned with -c).  A group with an empty
// segment (or a map whose size != its channel count) has an empty/mismatched map
// and falls back to SDIFF_ALLPAIRS, so mixed group sizes and probes are safe.
static std::vector<std::vector<int> > g_custom_partner;

// The partner map for group g, or an empty map when g carries no custom pattern.
static const std::vector<int>& groupPartner(int g)
{
    static const std::vector<int> none;
    return (g >= 0 && g < (int)g_custom_partner.size()) ? g_custom_partner[g] : none;
}

// SDIFF_CUSTOM_CAR per-group reference sets.  g_custom_partner_sets[grp][ci] is the
// set of within-group indices output channel ci is referenced against, so
// s[ci] = x[ci] - mean(x over the set).  Selected when -P uses "a-b+c..." syntax.
// Full rank in general (no dropped root), but SDIFF_PASS still drops the last
// channel downstream, so the least-informative channel should be placed last.
static std::vector<std::vector<std::vector<int> > > g_custom_partner_sets;

static const std::vector<std::vector<int> >& groupPartnerSet(int g)
{
    static const std::vector<std::vector<int> > none;
    return (g >= 0 && g < (int)g_custom_partner_sets.size()) ? g_custom_partner_sets[g] : none;
}

static const char *program_version = "process_extractspikes_stderiv 1.0";

// =========================================================================
// Spatial derivative
// =========================================================================

double computeSDiff(const short *record,
                    const int   *chanList,
                    int          idx,
                    int          nChanGrp,
                    SdiffOrder   order,
                    const std::vector<int>& customPartner,
                    const std::vector<std::vector<int> >& customSets =
                        std::vector<std::vector<int> >())
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

    case SDIFF_CUSTOM:
        if(nChanGrp > 1 && (int)customPartner.size() == nChanGrp)
            return val - record[chanList[customPartner[idx]]];
        // pattern does not fit this group -> fall through to ALLPAIRS
        [[fallthrough]];

    case SDIFF_CUSTOM_CAR:
        if(nChanGrp > 1 && (int)customSets.size() == nChanGrp
           && !customSets[idx].empty()) {
            double sum = 0.0;
            for(size_t k = 0; k < customSets[idx].size(); k++)
                sum += record[chanList[customSets[idx][k]]];
            return val - sum / (double)customSets[idx].size();
        }
        // set-map does not fit this group -> fall through to ALLPAIRS
        [[fallthrough]];

    case SDIFF_ALLPAIRS:
    default: {
        // Single-channel group: no neighbours, all-pairwise is undefined.
        // Detection will find nothing; use order 0 for single-channel groups.
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
// fill_sdiff_buffer: spatial derivative THEN temporal first-difference.
//
//   step 1: sdiff[t, ch]   = spatialDerivative(raw[t, ch], ...)
//   step 2: out[t, ch]     = sdiff[t, ch] - sdiff[t-1, ch]
//
// The temporal difference sharpens spike onsets, removes slow drift, and
// adds a second stage of common-mode rejection.  g_prev_sdiff carries the
// last time-sample's spatial derivative across buffer boundaries so the
// difference is continuous at chunk edges.
//
// Non-group channels are copied verbatim from raw (never touched by the
// detector, but present in the buffer layout).
static void fill_sdiff_buffer(const short *raw,
                               short       *out,
                               int          nSamples,
                               int          nChanTot,
                               int          nGroups,
                               int        **channelList,
                               int         *channelNb_grp,
                               SdiffOrder   order)
{
    // Step 1: compute spatial derivative into a temporary buffer sd[].
    // Copy everything first so non-group channels have valid values.
    std::vector<short> sd((size_t)nSamples * nChanTot);
    memcpy(sd.data(), raw, (size_t)nSamples * (size_t)nChanTot * sizeof(short));

    for(int t = 0; t < nSamples; t++) {
        const short *rawRec = raw      + (size_t)t * nChanTot;
              short *sdRec  = sd.data() + (size_t)t * nChanTot;

        for(int g = 0; g < nGroups; g++) {
            const int  nCG   = channelNb_grp[g];
            const int *cList = channelList[g];
            for(int ci = 0; ci < nCG; ci++) {
                double v = (order == SDIFF_NONE)
                             ? (double)rawRec[cList[ci]]
                             : computeSDiff(rawRec, cList, ci, nCG, order, groupPartner(g), groupPartnerSet(g));
                int iv = (int)round(v);
                if(iv >  32767) iv =  32767;
                if(iv < -32768) iv = -32768;
                sdRec[cList[ci]] = (short)iv;
            }
        }
    }

    // Step 2: temporal first-difference — work from t=0 upward so we can
    // update g_prev_sdiff at the end without clobbering values we still need.
    // Copy full buffer first so non-group channels pass through.
    memcpy(out, sd.data(), (size_t)nSamples * (size_t)nChanTot * sizeof(short));

    for(int t = 0; t < nSamples; t++) {
        const short *sdCur  = sd.data() + (size_t)t * nChanTot;
        const short *sdPrev = (t > 0)
                            ? sd.data() + (size_t)(t-1) * nChanTot
                            : (g_prev_sdiff.empty() ? nullptr
                                                    : g_prev_sdiff.data());
              short *outRec = out + (size_t)t * nChanTot;

        for(int g = 0; g < nGroups; g++) {
            const int  nCG   = channelNb_grp[g];
            const int *cList = channelList[g];
            for(int ci = 0; ci < nCG; ci++) {
                const int ch = cList[ci];
                int diff = (int)sdCur[ch] - (sdPrev ? (int)sdPrev[ch] : 0);
                if(diff >  32767) diff =  32767;
                if(diff < -32768) diff = -32768;
                outRec[ch] = (short)diff;
            }
        }
    }

    // Save last time-sample's spatial derivative for the next buffer's t=0.
    g_prev_sdiff.resize((size_t)nChanTot);
    memcpy(g_prev_sdiff.data(),
           sd.data() + (size_t)(nSamples - 1) * nChanTot,
           (size_t)nChanTot * sizeof(short));
}

// =========================================================================
// fill_car_buffer  (hybrid detection, raw arm)
// =========================================================================
// Shadow buffer of the group-referenced raw signal: out[t, ch] =
// round(x[t, ch] - mean over the channel's group of x[t, .]), int16-clamped.
// No temporal difference.  Non-group channels are copied verbatim.
static void fill_car_buffer(const short *raw,
                            short       *out,
                            int          nSamples,
                            int          nChanTot,
                            int          nGroups,
                            int        **channelList,
                            int         *channelNb_grp)
{
    memcpy(out, raw, (size_t)nSamples * (size_t)nChanTot * sizeof(short));
    for(int t = 0; t < nSamples; t++) {
        const short *rawRec = raw + (size_t)t * nChanTot;
              short *outRec = out + (size_t)t * nChanTot;
        for(int g = 0; g < nGroups; g++) {
            const int  nCG   = channelNb_grp[g];
            const int *cList = channelList[g];
            if(nCG <= 0) continue;
            double mean = 0.0;
            for(int ci = 0; ci < nCG; ci++) mean += rawRec[cList[ci]];
            mean /= nCG;
            for(int ci = 0; ci < nCG; ci++) {
                long v = lround(rawRec[cList[ci]] - mean);
                if(v >  32767) v =  32767;
                if(v < -32768) v = -32768;
                outRec[cList[ci]] = (short)v;
            }
        }
    }
}

// =========================================================================
// Threshold computation
// =========================================================================
// The noise window is read once and two robust noise levels are measured per
// channel: median|sdiff| (spatial derivative only, the historical threshold
// base) and median|stderiv| (spatial + temporal first-difference — the signal
// the detector thresholds, built exactly as fill_sdiff_buffer builds it:
// rounded + int16-clamped sdiff, then a clamped first difference).  `noise`
// picks which one sets the threshold; both are reported so a run states the
// effective threshold on the detection signal either way.
// When outCarSigma is given, the robust sigma (median|x|/0.6745) of the
// group-referenced raw signal x[c] - mean_group(x) is also returned per channel
// (used by raw re-centring).
void computeSdiffThresholds(FILE          *fp,
                             off_t          startByte,
                             off_t          sizeByte,
                             int            nChanTot,
                             int            nGroups,
                             int          **channelList,
                             int           *channelNb_group,
                             SdiffOrder     order,
                             double         factor,
                             ThresholdNoise noise,
                             double       **outThresholds,
                             vector<vector<double>> *outCarSigma)
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

    vector<vector<vector<double>>> absBuf(nGroups);   // |sdiff|   (historical base)
    vector<vector<vector<float>>>  absTd(nGroups);    // |stderiv| (detection signal)
    vector<vector<int>>            prevSd(nGroups);   // last rounded sdiff, per channel
    vector<vector<vector<float>>>  absCar(nGroups);   // |x - group mean| (if asked)
    const bool wantCar = (outCarSigma != nullptr);
    if(wantCar) outCarSigma->assign((size_t)nGroups, vector<double>());
    for(int g = 0; g < nGroups; g++) {
        absBuf[g].resize(channelNb_group[g]);
        absTd[g].resize(channelNb_group[g]);
        if(wantCar) {
            absCar[g].resize(channelNb_group[g]);
            for(int ci = 0; ci < channelNb_group[g]; ci++)
                absCar[g][ci].reserve((size_t)(nSamples / stride + 1));
        }
        prevSd[g].assign(channelNb_group[g], 0);
        for(int ci = 0; ci < channelNb_group[g]; ci++) {
            absBuf[g][ci].reserve((size_t)(nSamples / stride + 1));
            absTd[g][ci].reserve((size_t)(nSamples / stride + 1));
        }
    }

    auto clamp16 = [](long v) -> int {
        return (int)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
    };

    long long samplesRead = 0, samplesSeen = 0;
    while(samplesRead < nSamples) {
        long long toRead = min((long long)chunkSz, nSamples - samplesRead);
        size_t got = fread(chunk.data(), sizeof(short),
                            (size_t)(toRead * nChanTot), fp);
        long long gotSamples = (long long)got / nChanTot;

        for(long long t = 0; t < gotSamples; t++, samplesSeen++) {
            // The temporal difference needs EVERY sample's sdiff, so the
            // derivative is carried on all samples; only the kept (strided)
            // samples enter the medians.
            const bool keep  = (samplesSeen % stride == 0);
            const bool hasTd = (samplesSeen > 0);
            const short *rec = chunk.data() + t * nChanTot;
            for(int g = 0; g < nGroups; g++) {
                const int nCG    = channelNb_group[g];
                const int *cList = channelList[g];
                if(wantCar && keep && nCG > 0) {
                    double mean = 0.0;
                    for(int ci = 0; ci < nCG; ci++) mean += rec[cList[ci]];
                    mean /= nCG;
                    for(int ci = 0; ci < nCG; ci++)
                        absCar[g][ci].push_back((float)fabs(rec[cList[ci]] - mean));
                }
                for(int ci = 0; ci < nCG; ci++) {
                    const double v = computeSDiff(rec, cList, ci, nCG, order, groupPartner(g), groupPartnerSet(g));
                    const int    sdi = clamp16(lround(v));
                    if(keep) {
                        absBuf[g][ci].push_back(fabs(v));
                        if(hasTd)
                            absTd[g][ci].push_back((float)abs(clamp16((long)sdi - prevSd[g][ci])));
                    }
                    prevSd[g][ci] = sdi;
                }
            }
        }
        samplesRead += gotSamples;
        if(gotSamples < toRead) break;
    }

    auto medianOf = [](auto &v) -> double {
        nth_element(v.begin(), v.begin() + (long)(v.size() / 2), v.end());
        return (double)v[v.size() / 2];
    };

    for(int g = 0; g < nGroups; g++) {
        double minK = 1e300, maxK = 0.0;
        for(int ci = 0; ci < channelNb_group[g]; ci++) {
            auto &v = absBuf[g][ci];
            if(v.empty() || absTd[g][ci].empty()) {
                cerr << "error: no data for group " << g+1
                     << " channel index " << ci << endl;
                exit(1);
            }
            sort(v.begin(), v.end());
            const double medSd = v[v.size() / 2];
            const double medTd = medianOf(absTd[g][ci]);
            const double med   = (noise == THRNOISE_STDERIV) ? medTd : medSd;
            // Quiroga (2004) threshold: thr = factor × 4 × σ_n
            // where σ_n = median(|x|) / 0.6745  (robust noise-amplitude estimate;
            // 0.6745 = Φ⁻¹(0.75) for a unit Gaussian).
            // factor is the user-supplied -f argument (default 3 in ndm_extractspikes_sdiff);
            // combined with 4 it matches process_medianthreshold's convention of
            // "threshold = factor × 4 × sigma_n".
            outThresholds[g][ci] = factor * 4.0 * med / 0.6745;
            // Effective threshold in noise units OF THE DETECTION SIGNAL.
            const double k = (medTd > 0) ? outThresholds[g][ci] / (medTd / 0.6745) : 0.0;
            minK = min(minK, k); maxK = max(maxK, k);
            if(verbose)
                cout << "  [noise] g=" << g+1
                     << " ch=" << channelList[g][ci]
                     << "  median|sdiff|=" << medSd
                     << "  median|stderiv|=" << medTd
                     << "  thr=" << outThresholds[g][ci]
                     << "  (= " << k << " sigma of the detection signal)" << endl;
        }
        if(wantCar) {
            (*outCarSigma)[g].assign(channelNb_group[g], 1.0);
            for(int ci = 0; ci < channelNb_group[g]; ci++) {
                const double s = medianOf(absCar[g][ci]) / 0.6745;
                (*outCarSigma)[g][ci] = (s > 0) ? s : 1.0;
            }
        }
        if(channelNb_group[g] > 0)
            cout << "Group " << g+1 << " noise base: "
                 << (noise == THRNOISE_STDERIV ? "stderiv" : "sdiff (historical)")
                 << "; effective threshold " << minK << ".." << maxK
                 << " sigma of the detection signal (factor " << factor
                 << " x 4 = " << factor * 4.0 << ")\n";
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
         << "Threshold (computed internally from the noise window):\n"
         << "  -f factor       threshold = factor * 4*sigma  (Quiroga 2004)\n"
         << "  -B startByte    byte offset into .fil for noise window\n"
         << "  -Z sizeBytes    byte length of noise window\n"
         << "  -T sdiff|stderiv  signal sigma is measured on.  sdiff (default,\n"
         << "                  historical) = spatial derivative only, although the\n"
         << "                  detector thresholds the temporally differenced signal,\n"
         << "                  whose noise is smaller -- the factor is effectively\n"
         << "                  inflated.  stderiv = the detection signal itself, so\n"
         << "                  factor*4 is the threshold in its noise units.\n\n"
         << "Hybrid detection:\n"
         << "  -H              add a second detection pass on the group-referenced\n"
         << "                  raw signal (x - group mean, no temporal difference)\n"
         << "                  and merge it with the stderiv pass (refractory rule).\n"
         << "                  Each pass thresholds at factor*4 sigma of its own\n"
         << "                  signal (implies -T stderiv; refuses -T sdiff, -R).\n\n"
         << "Clean-up (detection runs only):\n"
         << "  -F N            reject a spike if any channel of its raw window has more\n"
         << "                  than N identical consecutive samples (zero-filled gaps,\n"
         << "                  stuck lines).  0 = off (default).\n"
         << "  -U N            after re-centring, drop a spike within N samples of the\n"
         << "                  previous kept spike of its group (0 = identical positions\n"
         << "                  only).  -1 = off (default).\n\n"
         << "Re-centring (where each detection is placed before .res is written):\n"
         << "  -C stderiv|raw  stderiv (default, historical) = max summed |stderiv|\n"
         << "                  over the group; raw = max negative deflection of the\n"
         << "                  group-referenced raw signal (x - group mean), summed in\n"
         << "                  per-channel noise units -- the physical trough.\n"
         << "  -W halfWidth    search +-halfWidth samples around the detection\n"
         << "                  (0 = no re-centring).  Unset = historical window\n"
         << "                  [-(p-1), l-p] samples.\n\n"
         << "Spatial derivative:\n"
         << "  -d order        0=none  1=first-diff  2=Laplacian  3=allpairs (default)\n"
         << "  -P g1:g2:...    custom per-group difference patterns.  Each group is\n"
         << "                  a-b,c-d,... of 0-based within-group positions (output\n"
         << "                  channel a = x[a]-x[b]); groups are colon-separated and\n"
         << "                  aligned with -c.  Set syntax \"a-b+c+d\" (order 5) makes\n"
         << "                  channel a = x[a]-mean(x[b],x[c],x[d]) — local geometry\n"
         << "                  with common-mode rejection; every channel needs a set.\n"
         << "                  An empty group segment (or a size that\n"
         << "                  does not match the pattern) uses order 3 for that group.\n\n"
         << "Re-extract mode (patch86):\n"
         << "  -R              skip detection; read basename.res.<grp> instead\n"
         << "                  and re-extract waveforms at those exact timestamps,\n"
         << "                  applying the same spatial+temporal derivative\n"
         << "                  transform as the detection path.  Ignores -r/-f/-l/-Z.\n\n"
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
    a.thresholdNoise           = THRNOISE_SDIFF; // historical default
    a.recenterMode             = RECENTER_STDERIV; // historical default
    a.recenterHalfWidth        = -1;               // historical window
    a.isThresholdNoiseProvided = false;
    a.hybrid                   = false;
    a.flatRunMax               = 0;    // off
    a.dedupSamples             = -1;   // off
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
    a.useExistingRes               = false;  // patch86

    if(argc < 2) usage(argv[0]);

    int i = 1;
    for(; i < argc; i++) {
        if(argv[i][0] != '-') break;
        if((int)strlen(argv[i]) < 2) usage(argv[0]);
        switch(argv[i][1]) {
        case 'h': help(argv[0]); break;
        case 'v': verbose = true; break;
        case 'a': a.isDisableAbs = false; break;
        case 'R': a.useExistingRes = true; break;  // patch86
        case 'H': a.hybrid = true; break;
        case 'F': a.flatRunMax = atoi(argv[++i]);
                  if(a.flatRunMax < 0) { cerr << "error: -F must be >= 0." << endl; exit(1); }
                  break;
        case 'U': a.dedupSamples = atoi(argv[++i]);
                  if(a.dedupSamples < -1) { cerr << "error: -U must be >= -1." << endl; exit(1); }
                  break;
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
        case 'T': { const string v = argv[++i];
                    a.isThresholdNoiseProvided = true;
                    if(v == "sdiff")        a.thresholdNoise = THRNOISE_SDIFF;
                    else if(v == "stderiv") a.thresholdNoise = THRNOISE_STDERIV;
                    else {
                        cerr << "error: -T must be 'sdiff' or 'stderiv'." << endl;
                        exit(1);
                    }
                    break; }
        case 'C': { const string v = argv[++i];
                    if(v == "stderiv")  a.recenterMode = RECENTER_STDERIV;
                    else if(v == "raw") a.recenterMode = RECENTER_RAW;
                    else {
                        cerr << "error: -C must be 'stderiv' or 'raw'." << endl;
                        exit(1);
                    }
                    break; }
        case 'W': { const int w = atoi(argv[++i]);
                    if(w < 0) {
                        cerr << "error: -W must be >= 0." << endl;
                        exit(1);
                    }
                    a.recenterHalfWidth = w; break; }
        case 'd': { int o = atoi(argv[++i]);
                    if(o < 0 || o > 3) {
                        cerr << "error: -d must be 0, 1, 2, or 3." << endl;
                        exit(1);
                    }
                    a.sdiffOrder = static_cast<SdiffOrder>(o);
                    a.isSdiffOrderProvided = true; break; }
        case 'P': {
                    const char *pat = argv[++i];
                    if(sdiffSpecUsesSets(pat)) {
                        g_custom_partner_sets = parseSdiffSetsPerGroup(pat);
                        a.sdiffOrder = SDIFF_CUSTOM_CAR;   // order 5: reference-set mean
                    } else {
                        g_custom_partner = parseSdiffPairsPerGroup(pat);
                        a.sdiffOrder = SDIFF_CUSTOM;        // order 4: single partner
                    }
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
    // patch86: detection-only args are skipped in -R mode
    if(!a.useExistingRes) {
        if(!a.isRefractoryPeriodProvided)   { cerr<<"error: missing -r\n"; ok=false; }
        if(!a.isThresholdFactorProvided)    { cerr<<"error: missing -f\n"; ok=false; }
        if(!a.isThreshSizeBytesProvided)    { cerr<<"error: missing -Z\n"; ok=false; }
    }
    if(!ok) exit(1);

    if(a.hybrid) {
        if(a.useExistingRes) {
            cerr << "error: -H (hybrid detection) cannot be combined with -R.\n"; exit(1);
        }
        if(a.isThresholdNoiseProvided && a.thresholdNoise != THRNOISE_STDERIV) {
            cerr << "error: -H thresholds each arm on its own detection signal; "
                    "it cannot be combined with -T sdiff.\n";
            exit(1);
        }
        a.thresholdNoise = THRNOISE_STDERIV;
        if(a.recenterHalfWidth < 0 || 2 * a.recenterHalfWidth >= a.refractoryPeriod)
            cerr << "warning: -H with " << (a.recenterHalfWidth < 0 ? "the historical re-centring window"
                                                                     : "a re-centring half-width >= refractory/2")
                 << ": merged events closer than the window can be re-centred onto the same\n"
                    "         sample (duplicate .res times).  Use -W < refractoryPeriod/2 (e.g. -C raw -W 8).\n";
    }

    if(!a.isPeakLengthProvided) {
        if(!a.useExistingRes)
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

// patch86 ---------------------------------------------------------------
// runFromRes (stderiv variant): re-extract waveforms at timestamps from
// existing .res files, applying the same SDIFF + temporal first-difference
// transform as the detection path.
//
// Per group, per timestamp ts:
//   1. raw window [ts - timeBefore, ts - timeBefore + spikeLength)
//   2. one extra sample at (ts - timeBefore - 1) for the temporal-diff
//      continuity at s=0 (matches fill_sdiff_buffer's chunk-boundary
//      behaviour with g_prev_sdiff carrying the prior chunk's last sample)
//   3. compute SDIFF on all spikeLength+1 samples
//   4. temporal first-diff: out[s, c] = sd[s, c] - sd[s-1, c]
//   5. write to .spk.<grp+1> (untagged temp; wrapper tags final)
//
// Returns 0 on success, non-zero on I/O error.
static int runFromRes(const arguments &args,
                      int **channelList,
                      const int *channelNb_grp,
                      int nbGroups)
{
    const int nChanTot   = args.totalChannelNumber;
    const int spikeLen   = args.spikeLength;
    const int timeBefore = args.timeBeforeSpike;
    const SdiffOrder ord = args.sdiffOrder;

    int fd = open(args.inputFileName, O_RDONLY);
    if(fd < 0) {
        fprintf(stderr, "[-R] cannot open '%s' for reading: %s\n",
                args.inputFileName, strerror(errno));
        return 1;
    }
    struct stat st;
    if(fstat(fd, &st) < 0) {
        fprintf(stderr, "[-R] fstat failed: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    const size_t filBytes       = (size_t)st.st_size;
    const size_t bytesPerSample = (size_t)nChanTot * sizeof(short);
    if(bytesPerSample == 0) { close(fd); return 1; }
    const int64_t nSamples = (int64_t)(filBytes / bytesPerSample);

    const short *fil = (const short *)mmap(nullptr, filBytes,
                                           PROT_READ, MAP_PRIVATE, fd, 0);
    if(fil == MAP_FAILED) {
        fprintf(stderr, "[-R] mmap failed on '%s': %s\n",
                args.inputFileName, strerror(errno));
        close(fd);
        return 1;
    }

    if(verbose) {
        cout << "[-R] re-extract mode (stderiv) active" << endl;
        cout << "[-R]   .fil          = " << args.inputFileName
             << " (" << nSamples << " samples, " << nChanTot << " ch)" << endl;
        cout << "[-R]   output stem   = " << args.outputBaseFileName << endl;
        cout << "[-R]   spikeLength   = " << spikeLen
             << ", peakSampleIndex (timeBefore+1) = " << (timeBefore + 1) << endl;
        cout << "[-R]   sdiff order   = " << (int)ord << endl;
    }

    int errors      = 0;
    int64_t totalSpikes = 0;
    int64_t totalSkipped = 0;

    BlockProgress prog(args.outputBaseFileName);

    for(int grp = 0; grp < nbGroups; grp++) {
        const int nCG = channelNb_grp[grp];
        if(nCG <= 0) continue;

        ostringstream resPath;
        resPath << args.outputBaseFileName << "." << SPIKE_TIME_OUT_EXT
                << "." << (grp + 1);
        FILE *resFp = fopen(resPath.str().c_str(), "rb");
        if(!resFp) {
            fprintf(stderr, "[-R] cannot open '%s' for reading: %s\n",
                    resPath.str().c_str(), strerror(errno));
            errors++;
            continue;
        }
        fseeko(resFp, 0, SEEK_END);
        const off_t resBytes = ftello(resFp);
        fseeko(resFp, 0, SEEK_SET);
        const size_t nResSpikes = (size_t)(resBytes / (off_t)sizeof(int64_t));
        vector<int64_t> resTs(nResSpikes);
        if(nResSpikes > 0) {
            size_t r = fread(resTs.data(), sizeof(int64_t), nResSpikes, resFp);
            if(r != nResSpikes) {
                fprintf(stderr, "[-R] short read on '%s' (%zu/%zu)\n",
                        resPath.str().c_str(), r, nResSpikes);
                fclose(resFp);
                errors++;
                continue;
            }
        }
        fclose(resFp);

        ostringstream spkPath;
        spkPath << args.outputBaseFileName << "." << SPIKE_REC_OUT_EXT
                << "." << (grp + 1);
        FILE *spkFp = fopen(spkPath.str().c_str(), "wb");
        if(!spkFp) {
            fprintf(stderr, "[-R] cannot open '%s' for writing: %s\n",
                    spkPath.str().c_str(), strerror(errno));
            errors++;
            continue;
        }

        // sd holds the spatial derivative at samples [ws-1 .. ws+spikeLen-1]
        // (spikeLen + 1 rows), packed by group channels only (nCG wide).
        vector<short> sd((size_t)(spikeLen + 1) * (size_t)nCG);
        // out is the temporal first-diff, sd[s+1] - sd[s], rows [0..spikeLen)
        vector<short> waveBuf((size_t)spikeLen * (size_t)nCG);

        int64_t nWritten   = 0;
        int64_t nSkippedBd = 0;

        prog.beginStage("G" + std::to_string(grp + 1), (long long)nResSpikes);
        for(size_t i = 0; i < nResSpikes; i++) {
            prog.setPosition((long long)i);
            const int64_t ts = resTs[i];
            const int64_t ws = ts - (int64_t)timeBefore;
            // need samples in [ws-1, ws+spikeLen) for temporal-diff continuity
            if(ws < 1 || ws + spikeLen > nSamples) {
                nSkippedBd++;
                continue;
            }

            // step 1: SDIFF over spikeLen+1 raw samples starting at (ws-1)
            for(int s = 0; s < spikeLen + 1; s++) {
                const short *raw =
                    fil + (size_t)(ws - 1 + s) * (size_t)nChanTot;
                short *sdRow = sd.data() + (size_t)s * (size_t)nCG;
                for(int ci = 0; ci < nCG; ci++) {
                    double v;
                    if(ord == SDIFF_NONE) {
                        v = (double)raw[ channelList[grp][ci] ];
                    } else {
                        v = computeSDiff(raw, channelList[grp], ci, nCG, ord, groupPartner(grp), groupPartnerSet(grp));
                    }
                    int iv = (int)round(v);
                    if(iv >  32767) iv =  32767;
                    if(iv < -32768) iv = -32768;
                    sdRow[ci] = (short)iv;
                }
            }

            // step 2: temporal first-difference into waveBuf
            for(int s = 0; s < spikeLen; s++) {
                const short *sdCur  = sd.data() + (size_t)(s + 1) * (size_t)nCG;
                const short *sdPrev = sd.data() + (size_t)s       * (size_t)nCG;
                short *outRow = waveBuf.data() + (size_t)s * (size_t)nCG;
                for(int ci = 0; ci < nCG; ci++) {
                    int diff = (int)sdCur[ci] - (int)sdPrev[ci];
                    if(diff >  32767) diff =  32767;
                    if(diff < -32768) diff = -32768;
                    outRow[ci] = (short)diff;
                }
            }

            if(fwrite(waveBuf.data(), sizeof(short),
                      (size_t)spikeLen * (size_t)nCG, spkFp)
               != (size_t)spikeLen * (size_t)nCG) {
                fprintf(stderr, "[-R] short write on '%s'\n",
                        spkPath.str().c_str());
                errors++;
                break;
            }
            nWritten++;
        }
        fclose(spkFp);
        prog.endStage();

        totalSpikes  += nWritten;
        totalSkipped += nSkippedBd;

        if(verbose)
            cout << "[-R] group " << (grp + 1) << ": "
                 << nResSpikes << " timestamps -> wrote " << nWritten
                 << " spikes to " << spkPath.str()
                 << " (skipped " << nSkippedBd << " at .fil boundary)" << endl;
    }

    prog.finish();

    munmap((void*)fil, filBytes);
    close(fd);

    if(verbose)
        cout << "[-R] DONE. Total: " << totalSpikes << " spikes written, "
             << totalSkipped << " skipped (out-of-range), "
             << errors << " errors." << endl;

    return errors;
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
    // patch86: skip checkInputs in -R mode (refractoryPeriod stays -1)
    if(!args.useExistingRes && !checkInputs(args, buffer_size, inputFile)) exit(1);
    if(args.useExistingRes && !inputFile) {
        cerr << "error: cannot open '" << args.inputFileName << "'" << endl;
        exit(1);
    }

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
             << "sdiffOrder   : " << (int)args.sdiffOrder     << "\n"
             << "noiseBase    : " << (args.thresholdNoise == THRNOISE_STDERIV
                                      ? "stderiv" : "sdiff") << "\n"
             << "recenter     : " << (args.recenterMode == RECENTER_RAW ? "raw" : "stderiv")
             << " halfWidth " << args.recenterHalfWidth
             << (args.recenterHalfWidth < 0 ? " (historical window)" : "") << "\n"
             << "hybrid       : " << (args.hybrid ? "yes (stderiv + raw arm)" : "no") << "\n"
             << "flatRunMax   : " << args.flatRunMax << (args.flatRunMax ? "" : " (off)") << "\n"
             << "dedup        : " << args.dedupSamples << (args.dedupSamples < 0 ? " (off)" : " samples") << "\n";
        if(args.useExistingRes) cout << "mode         : RE-EXTRACT (-R)\n";
        cout << "\n";
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

    // patch86: in -R mode, dispatch to mmap-based re-extract and return.
    // The detection state machine + threshold setup below is skipped.
    if(args.useExistingRes) {
        if(inputFile) { fclose(inputFile); inputFile = nullptr; }
        const int rc = runFromRes(args, channelList, channelNb_grp, nbGroups);
        for(int i = 0; i < MAX_CHANNO; i++) delete[] channelList[i];
        delete[] channelList;
        delete[] channelNb_grp;
        return rc;
    }

    // ── per-group thresholds (noise base chosen by -T) ────────────────────
    // carSigma: per-channel sigma of the group-referenced raw signal, measured
    // only when raw re-centring or the hybrid raw arm needs it.
    vector<vector<double>> carSigma;
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
                            args.sdiffOrder, args.thresholdFactor,
                            args.thresholdNoise, thresList,
                            (args.recenterMode == RECENTER_RAW || args.hybrid)
                                ? &carSigma : nullptr);

    for(int g = 0; g < nbGroups; g++) thresNb_grp[g] = channelNb_grp[g];

    if(!checkChanAndThres(channelList, channelNb_grp, nbGroups,
                           thresNb_grp, nbGroups)) {
        cerr << "error: channel/threshold mismatch.\n"; exit(1);
    }

    for(int g = 0; g < nbGroups; g++) {
        cout << "Group " << g+1 << " thresholds: ";
        for(int ci = 0; ci < channelNb_grp[g]; ci++) {
            if(ci) cout << ",";
            cout << thresList[g][ci];
        }
        cout << "\n";
    }

    // Hybrid: raw-arm thresholds, factor * 4 sigma of the group-referenced raw
    // signal (sigma measured in the same noise window).
    vector<vector<double>> carThresStore;
    vector<double*>        carThresList;
    if(args.hybrid) {
        carThresStore.resize((size_t)nbGroups);
        carThresList.assign((size_t)nbGroups, nullptr);
        for(int g = 0; g < nbGroups; g++) {
            const size_t gi = static_cast<size_t>(g);
            carThresStore[gi].assign((size_t)max(1, channelNb_grp[g]), 0.0);
            for(int ci = 0; ci < channelNb_grp[g]; ci++)
                carThresStore[gi][(size_t)ci] = args.thresholdFactor * 4.0 * carSigma[gi][(size_t)ci];
            carThresList[gi] = carThresStore[gi].data();
            if(channelNb_grp[g] <= 0) continue;
            cout << "Group " << g+1 << " raw-arm thresholds: ";
            for(int ci = 0; ci < channelNb_grp[g]; ci++) {
                if(ci) cout << ",";
                cout << carThresList[gi][ci];
            }
            cout << "\n";
        }
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

    BlockProgress prog(args.outputBaseFileName);
    long long progTotalSamples = 0;
    if(args.isInputFileProvided) {
        off_t cur = ftello(inputFile);
        fseeko(inputFile, 0, SEEK_END);
        progTotalSamples = ftello(inputFile)
            / (args.totalChannelNumber * (long long)sizeof(short));
        fseeko(inputFile, cur, SEEK_SET);
    }

    // One full detection pass over the file on one shadow signal: the stderiv
    // signal (carArm == false; the historical, and only, pass without -H) or the
    // group-referenced raw signal (carArm == true; the -H raw arm).  The state
    // machine below is unchanged; it is wrapped so it can run once per arm, with
    // its per-group state, carry buffers and reader reset at the start of a pass.
    auto runDetectPass = [&](bool carArm, double *const *armThr,
                             vector<vector<int64_t>> &outTs, const char *stage) {
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
    g_prev_sdiff.clear();
    memset(prev_buffer, 0, sizeof(short) * buffer_size);
    memset(sdiff_prev,  0, sizeof(short) * buffer_size);
    if(!nextRec) nextRec = new short[args.totalChannelNumber];

    fseeko(inputFile, 0, SEEK_SET);
    bool isLastLoop = false;
    unsigned long long rec_nb = 0, nbLoops = 0;
    long long progSamplesDone = 0;
    if(args.isInputFileProvided) prog.beginStage(stage, progTotalSamples);

    // ── Double-buffered reader (file input only) ───────────────────────────
    // Background thread prefetches the next raw block from the .fil into one of
    // two shared buffers while the main thread runs the (OpenMP, per-group)
    // detection on the spatial+temporal-derivative signal.  Only the raw read
    // moves off the critical path; the derivative and carry-record assembly are
    // unchanged, so output is byte-identical to the serial path.
    const bool dblBuf = args.isInputFileProvided;
    short* dbSlot[2]  = { nullptr, nullptr };
    size_t dbRecNb[2] = { 0, 0 };
    std::mutex dbMtx;
    std::condition_variable dbFilled, dbFree;
    int  dbFilledCount = 0, dbWriteIdx = 0, dbReadIdx = 0;
    bool dbStop = false, dbReaderStarted = false;
    std::thread dbReader;
    if(dblBuf) {
        dbSlot[0] = new short[buffer_size];
        dbSlot[1] = new short[buffer_size];
    }
    auto dbReaderFn = [&]() {
        for(;;) {
            int idx;
            {
                std::unique_lock<std::mutex> lk(dbMtx);
                dbFree.wait(lk, [&]{ return dbFilledCount < 2 || dbStop; });
                if(dbStop) return;
                idx = dbWriteIdx;
            }
            size_t got = fread(dbSlot[idx], sizeof(short), buffer_size, inputFile);
            bool eof = (got < (size_t)buffer_size);
            {
                std::lock_guard<std::mutex> lk(dbMtx);
                dbRecNb[idx] = got;
                dbWriteIdx  ^= 1;
                dbFilledCount++;
                dbFilled.notify_one();
            }
            if(eof) return;
        }
    };

    while(!isLastLoop && (dblBuf || !feof(inputFile))) {

        // ── fill raw buffer (identical to process_extractspikes) ──────────
        if(nbLoops == 0) {
            size_t _r = fread(nextRec, sizeof(short),
                               args.totalChannelNumber, inputFile);
            (void)_r;
        } else {
            memcpy(prev_buffer, cur_buffer, sizeof(short) * buffer_size);
            memcpy(sdiff_prev,  sdiff_cur,  sizeof(short) * buffer_size);
        }

        if(dblBuf) {
            if(!dbReaderStarted) { dbReader = std::thread(dbReaderFn); dbReaderStarted = true; }
            std::unique_lock<std::mutex> lk(dbMtx);
            dbFilled.wait(lk, [&]{ return dbFilledCount > 0; });
            const int idx = dbReadIdx;
            rec_nb = dbRecNb[idx];
            memcpy(cur_buffer + args.totalChannelNumber, dbSlot[idx],
                   sizeof(short) * rec_nb);
            dbReadIdx ^= 1;
            dbFilledCount--;
            dbFree.notify_one();
        } else {
            rec_nb = fread(cur_buffer + args.totalChannelNumber,
                            sizeof(short), buffer_size, inputFile);
        }
        memcpy(cur_buffer, nextRec, sizeof(short) * args.totalChannelNumber);

        if(rec_nb < (unsigned long long)buffer_size || (!dblBuf && feof(inputFile))) {
            isLastLoop = true;
            delete[] nextRec;
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

        if(carArm)
            fill_car_buffer(cur_buffer, sdiff_cur,
                            nSamplesThisChunk,
                            args.totalChannelNumber,
                            nbGroups, channelList, channelNb_grp);
        else
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
            const double *thr  = armThr[grp];

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
                                    outTs[grp].push_back(
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

        progSamplesDone += rec_nb / args.totalChannelNumber;
        nbLoops++;
        if(args.isInputFileProvided) prog.setPosition(progSamplesDone);
    } // while !feof

    if(dblBuf) {
        { std::lock_guard<std::mutex> lk(dbMtx); dbStop = true; }
        dbFree.notify_all();
        if(dbReader.joinable()) dbReader.join();
        delete[] dbSlot[0];
        delete[] dbSlot[1];
    }

    if(args.isInputFileProvided) prog.endStage();
    }; // runDetectPass

    runDetectPass(false, thresList, resTimestamps, "DETECT");

    if(args.hybrid) {
        // ── raw arm + merge ───────────────────────────────────────────────
        vector<unsigned int> nStd(nSpikeTot, nSpikeTot + nbGroups);
        vector<vector<int64_t>> carTs(nbGroups);
        runDetectPass(true, carThresList.data(), carTs, "DETECT-RAW");
        cout << "Hybrid detection (stderiv + raw arm, refractory " << args.refractoryPeriod
             << " samples):\n";
        for(int g = 0; g < nbGroups; g++) {
            if(channelNb_grp[g] == 0) continue;
            const vector<int64_t> &a = resTimestamps[g];
            const vector<int64_t> &b = carTs[g];
            // Union in time order; on equal times the stderiv event comes first.
            vector<pair<int64_t,int>> u;
            u.reserve(a.size() + b.size());
            for(int64_t t : a) u.emplace_back(t, 0);
            for(int64_t t : b) u.emplace_back(t, 1);
            sort(u.begin(), u.end());
            // Keep an event only if it lies more than refractoryPeriod samples after
            // the last kept one -- the rule the single-signal pass applies.
            vector<int64_t> merged;
            merged.reserve(u.size());
            size_t rawOnly = 0;
            int64_t last = -1;
            for(const auto &e : u) {
                if(last >= 0 && e.first - last <= (int64_t)args.refractoryPeriod) continue;
                merged.push_back(e.first);
                last = e.first;
                if(e.second == 1) rawOnly++;
            }
            cout << "  Group " << g+1 << ": stderiv " << nStd[(size_t)g]
                 << ", raw " << b.size() << " -> merged " << merged.size()
                 << " (" << rawOnly << " added by the raw arm)\n";
            resTimestamps[g] = std::move(merged);
            nSpikeTot[g] = (unsigned int)resTimestamps[g].size();
        }
    }

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

    cout << "Number of spikes (stderiv detection):\n";
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
    // PASS 2 — waveform extraction in spatial+temporal derivative space
    // =======================================================================
    // Raw waveforms are read from .fil, then the same spatial+temporal
    // derivative used during detection is applied per extracted waveform.
    // The .spk file therefore contains transformed waveforms, consistent
    // with the detection domain and with ndm_pca_stderiv's expectations.
    if(verbose) cout << "\n<<----- Pass 2: waveform extraction (stderiv space)\n\n";

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

    // Per-group rejection sets: origIdx values of waveforms to discard
    // (all-zero or constant channel after stderiv transform).
    vector<unordered_set<int>> rejectedOrigIdx(static_cast<size_t>(nbGroups));
    vector<int> flatRunRejected(static_cast<size_t>(nbGroups), 0);   // -F
    vector<int> dupRejected(static_cast<size_t>(nbGroups), 0);       // -U

    {
        const int rawLen = args.spikeLength * args.totalChannelNumber;
        vector<short> frameBuf(rawLen);

        FILE *seqF = fopen(args.inputFileName, "rb");
        if(!seqF) {
            fprintf(stderr,"error: cannot reopen '%s'\n",
                    args.inputFileName); exit(1);
        }

        // Peak-refinement window: half the peak-search length.
        // After reading the nominal extraction frame centred on the SDIFF
        // timestamp, search the RAW signal within ±halfSearch samples to
        // find the actual maximum-amplitude sample across all group channels.
        // Re-centering on the raw peak eliminates the systematic jitter
        // between the sdiff peak (detection) and the voltage peak (raw signal)
        // that causes spreading in the waveform display and degrades PCA.
        //
        // The refinement frame is read wide enough to cover both the nominal
        // extraction window AND the ±halfSearch offset:  frameLen = spikeLength
        // + 2*halfSearch.  The final extraction copies spikeLength samples
        // starting at the refined peak offset within the wide frame.
        //
        // -W sets the search half-width explicitly (then halfSearch == -W, a
        // symmetric +-W search); unset keeps the historical peakLength/2 frame
        // margin and asymmetric window.
        const bool histWindow = (args.recenterHalfWidth < 0);
        const int halfSearch = histWindow ? args.peakLength / 2
                                          : args.recenterHalfWidth;
        const int wideSpikeLen = args.spikeLength + 2 * halfSearch;
        const int wideRawLen   = wideSpikeLen * args.totalChannelNumber;
        vector<short> wideFrame(wideRawLen);

        // isCutoutOrFlat: returns true if any channel of the waveform is
        // all-zero (hardware dropout) or constant (stuck ADC line).
        // Layout: sdWav[t * nCG + ci]  (sample-major, compact group channels)
        // hasFlatRun: true if any channel holds more than maxRun identical
        // consecutive samples.  Layout: wav[t * nCG + ci] (raw .fil values).
        auto hasFlatRun = [](const short *wav, int nCG, int nSamples, int maxRun) -> bool {
            for (int ci = 0; ci < nCG; ci++) {
                int run = 1;
                for (int s = 1; s < nSamples; s++) {
                    run = (wav[s * nCG + ci] == wav[(s - 1) * nCG + ci]) ? run + 1 : 1;
                    if (run > maxRun) return true;
                }
            }
            return false;
        };

        auto isCutoutOrFlat = [](const short *wav, int nCG, int nSamples) -> bool {
            for (int ci = 0; ci < nCG; ci++) {
                bool allZero = true;
                int64_t sum = 0, sumSq = 0;
                for (int s = 0; s < nSamples; s++) {
                    const int64_t v = wav[s * nCG + ci];
                    if (v != 0) allZero = false;
                    sum   += v;
                    sumSq += v * v;
                }
                if (allZero) return true;  // all-zero: hardware dropout
                // variance*N² = N*sumSq - sum² == 0 means constant channel
                if ((int64_t)nSamples * sumSq - sum * sum == 0) return true;
            }
            return false;
        };

        const off_t progFileBytes = (off_t)progTotalSamples
            * args.totalChannelNumber * (off_t)sizeof(short);
        if(args.isInputFileProvided)
            prog.beginStage("EXTRACT", progFileBytes);

        for(const SpikeEvent &ev : allEvents) {
            if(args.isInputFileProvided) prog.setPosition((long long)ev.fileOffset);
            const int grp  = ev.grp;
            const int nCG  = channelNb_grp[grp];
            const int wLen = args.spikeLength * nCG;
            const int *cL  = channelList[grp];

            // Wide extraction: seek to halfSearch samples before the nominal window.
            // We use fseeko rather than a sequential skip-forward reader because
            // the wide frame starts BEFORE the nominal offset, which means
            // consecutive spikes (separated by < halfSearch samples) would require
            // seeking backward — impossible with a forward-only reader.
            const off_t wideOffset = ev.fileOffset
                - (off_t)halfSearch * args.totalChannelNumber * (off_t)sizeof(short);
            const off_t safeOffset = (wideOffset >= 0) ? wideOffset : 0;
            const int   skipHead   = (wideOffset >= 0) ? 0
                : (int)((-wideOffset) / (args.totalChannelNumber * (off_t)sizeof(short)));

            // Zero-fill wide frame (handles boundary clips).
            std::fill(wideFrame.begin(), wideFrame.end(), short(0));
            if(fseeko(seqF, safeOffset, SEEK_SET) != 0) continue;
            const int readLen = wideRawLen - skipHead * args.totalChannelNumber;
            size_t got = fread(wideFrame.data() + skipHead * args.totalChannelNumber,
                               sizeof(short), readLen, seqF);
            if((int)got <= 0) continue;

            // ── Stderiv-domain peak refinement ────────────────────────────
            // The detection timestamp is the peak of the stderiv signal.
            // We search in the stderiv domain (not raw amplitude) so the
            // extraction window is centred on the actual derivative peak.
            // Searching raw amplitudes would land on the raw voltage peak
            // (the inflection point of the derivative), which is near-zero
            // in the stderiv representation and produces flat waveforms.
            //
            // -C raw instead centres on the group-referenced raw trough (see
            // RecenterMode).  The stderiv waveform extracted later at that time
            // then carries its extremum a few samples BEFORE peakSampleIndex
            // (the steepest fall precedes the trough) -- a fixed per-unit lag,
            // inside any PCA window that spans the spike.
            int refinedPeakInWide = halfSearch + args.timeBeforeSpike; // nominal
            double bestAmp = 0.0;
            const int searchStart = histWindow ? halfSearch
                                               : refinedPeakInWide - halfSearch;
            const int searchEnd   = histWindow ? std::min(halfSearch + args.peakLength - 1,
                                                          wideSpikeLen - 1)
                                               : refinedPeakInWide + halfSearch;
            const bool rawRecenter = (args.recenterMode == RECENTER_RAW);
            // Compute stderiv amplitude at each candidate time sample.
            // sdiff[t-1] is needed for the temporal difference; carry it
            // across the search window using a running prev buffer.
            std::vector<double> sdPrevSearch(static_cast<size_t>(nCG), 0.0);
            if(!rawRecenter && searchStart > 0) {
                // Prime sdPrev with the spatial derivative one sample before
                // the search window so the first temporal diff is correct.
                const short *frPrev = wideFrame.data()
                    + (searchStart - 1) * args.totalChannelNumber;
                for(int ci = 0; ci < nCG; ci++) {
                    double v = frPrev[cL[ci]];
                    double sd = 0.0;
                    switch(args.sdiffOrder) {
                    case SDIFF_NONE: sd = v; break;
                    case SDIFF_FIRST: {
                        // Need full row — reread from wideFrame
                        double xPrev[64] = {};
                        for(int jj=0;jj<nCG;jj++) xPrev[jj]=frPrev[cL[jj]];
                        sd = (ci<nCG-1) ? xPrev[ci]-xPrev[ci+1]
                                        : xPrev[ci]-xPrev[ci-1]; break;
                    }
                    case SDIFF_LAPLACIAN: {
                        double xPrev[64] = {};
                        for(int jj=0;jj<nCG;jj++) xPrev[jj]=frPrev[cL[jj]];
                        if(nCG==1){ sd=xPrev[0]; break; }
                        if(ci==0)         sd = xPrev[0]-xPrev[1];
                        else if(ci==nCG-1) sd = xPrev[ci]-xPrev[nCG-2];
                        else               sd = xPrev[ci]-0.5*(xPrev[ci-1]+xPrev[ci+1]);
                        break;
                    }
                    case SDIFF_CUSTOM:
                        if((int)groupPartner(grp).size() == nCG) {
                            double xPrev[64] = {};
                            for(int jj=0;jj<nCG;jj++) xPrev[jj]=frPrev[cL[jj]];
                            sd = xPrev[ci] - xPrev[groupPartner(grp)[ci]]; break;
                        }
                        [[fallthrough]];
                    case SDIFF_CUSTOM_CAR:
                        if((int)groupPartnerSet(grp).size()==nCG && !groupPartnerSet(grp)[ci].empty()) {
                            const std::vector<int>& st=groupPartnerSet(grp)[ci];
                            double xp[64]={}, sm=0.0;
                            for(int jj=0;jj<nCG;jj++) xp[jj]=frPrev[cL[jj]];
                            for(size_t k=0;k<st.size();k++) sm+=xp[st[k]];
                            sd = xp[ci] - sm/(double)st.size(); break;
                        }
                        [[fallthrough]];
                    case SDIFF_ALLPAIRS: default: {
                        double sum=0.0;
                        for(int jj=0;jj<nCG;jj++) sum+=frPrev[cL[jj]];
                        sd = nCG*v - sum; break;
                    }
                    }
                    sdPrevSearch[static_cast<size_t>(ci)] = sd;
                }
            }
            for(int s = searchStart; s <= searchEnd; s++) {
                const short *fr = wideFrame.data() + s * args.totalChannelNumber;
                // Build compact row for this group
                double row[64] = {};
                for(int ci=0; ci<nCG; ci++) row[ci] = fr[cL[ci]];
                double amp = 0.0;
                if(rawRecenter) {
                    // Group-referenced raw deflection in per-channel noise
                    // units; negative-going only unless -a (abs) was given.
                    double mean = 0.0;
                    for(int ci = 0; ci < nCG; ci++) mean += row[ci];
                    mean /= nCG;
                    const vector<double> &sg = carSigma[static_cast<size_t>(grp)];
                    for(int ci = 0; ci < nCG; ci++) {
                        const double d = (row[ci] - mean) / sg[static_cast<size_t>(ci)];
                        amp += args.isDisableAbs ? (d < 0.0 ? -d : 0.0) : std::abs(d);
                    }
                    if(amp > bestAmp) { bestAmp = amp; refinedPeakInWide = s; }
                    continue;
                }
                for(int ci = 0; ci < nCG; ci++) {
                    double sd = 0.0;
                    switch(args.sdiffOrder) {
                    case SDIFF_NONE: sd = row[ci]; break;
                    case SDIFF_FIRST:
                        sd = (ci<nCG-1) ? row[ci]-row[ci+1] : row[ci]-row[ci-1]; break;
                    case SDIFF_LAPLACIAN:
                        if(nCG==1){ sd=row[ci]; break; }
                        if(ci==0)         sd = row[0]-row[1];
                        else if(ci==nCG-1) sd = row[ci]-row[nCG-2];
                        else               sd = row[ci]-0.5*(row[ci-1]+row[ci+1]);
                        break;
                    case SDIFF_CUSTOM:
                        if((int)groupPartner(grp).size() == nCG) {
                            sd = row[ci] - row[groupPartner(grp)[ci]]; break;
                        }
                        [[fallthrough]];
                    case SDIFF_CUSTOM_CAR:
                        if((int)groupPartnerSet(grp).size()==nCG && !groupPartnerSet(grp)[ci].empty()) {
                            const std::vector<int>& st=groupPartnerSet(grp)[ci];
                            double sm=0.0;
                            for(size_t k=0;k<st.size();k++) sm+=row[st[k]];
                            sd = row[ci] - sm/(double)st.size(); break;
                        }
                        [[fallthrough]];
                    case SDIFF_ALLPAIRS: default: {
                        double sum=0.0;
                        for(int j=0;j<nCG;j++) sum+=row[j];
                        sd = nCG*row[ci] - sum; break;
                    }
                    }
                    // Temporal difference
                    double td = sd - sdPrevSearch[static_cast<size_t>(ci)];
                    sdPrevSearch[static_cast<size_t>(ci)] = sd;
                    amp += std::abs(td);
                }
                if(amp > bestAmp) { bestAmp = amp; refinedPeakInWide = s; }
            }

            // Extract spikeLength samples centred on the refined peak.
            const int extractStart = refinedPeakInWide - args.timeBeforeSpike;
            const int copyStart = (extractStart < 0 ||
                                   extractStart + args.spikeLength > wideSpikeLen)
                                ? halfSearch    // clipped: nominal offset
                                : extractStart; // refined peak

            // Update .res timestamp to the REFINED peak position.
            // resTimestamps currently holds the detection timestamp (stderiv
            // peak). The waveform is extracted with the peak at timeBeforeSpike
            // samples from copyStart, so the refined recording position is:
            //   refinedTs = nominalTs - halfSearch + copyStart
            // Using the detection ts for nudging would shift by
            // (1 + per-spike refinement offset) instead of exactly 1 sample.
            {
                const int64_t nominalTs = resTimestamps[ev.grp][ev.origIdx];
                resTimestamps[ev.grp][ev.origIdx] =
                    nominalTs - (int64_t)halfSearch + (int64_t)copyStart;
            }

            // Step 1: extract raw nCG-channel waveform into compact buffer.
            std::vector<short> rawWav(static_cast<size_t>(args.spikeLength * nCG));
            for(int s = 0; s < args.spikeLength; s++) {
                const short *fr = wideFrame.data()
                    + (copyStart + s) * args.totalChannelNumber;
                for(int c = 0; c < nCG; c++) rawWav[s*nCG+c] = fr[cL[c]];
            }

            // Step 2: spatial derivative across channels at each time sample.
            // Compact layout: buf[t * nCG + ci].
            std::vector<short> sdWav(static_cast<size_t>(args.spikeLength * nCG));
            for(int s = 0; s < args.spikeLength; s++) {
                const short *rt = rawWav.data() + s * nCG;
                      short *st = sdWav.data()  + s * nCG;
                for(int ci = 0; ci < nCG; ci++) {
                    const double val = rt[ci];
                    double sd = 0.0;
                    switch(args.sdiffOrder) {
                    case SDIFF_NONE: sd = val; break;
                    case SDIFF_FIRST:
                        sd = (ci<nCG-1) ? val-rt[ci+1] : val-rt[ci-1]; break;
                    case SDIFF_LAPLACIAN:
                        if(nCG==1){ sd=val; break; }
                        if(ci==0)        sd = val - rt[1];
                        else if(ci==nCG-1) sd = val - rt[nCG-2];
                        else             sd = val - 0.5*(rt[ci-1]+rt[ci+1]);
                        break;
                    case SDIFF_CUSTOM:
                        if((int)groupPartner(grp).size() == nCG) {
                            sd = val - rt[groupPartner(grp)[ci]]; break;
                        }
                        [[fallthrough]];
                    case SDIFF_CUSTOM_CAR:
                        if((int)groupPartnerSet(grp).size()==nCG && !groupPartnerSet(grp)[ci].empty()) {
                            const std::vector<int>& st=groupPartnerSet(grp)[ci];
                            double sm=0.0;
                            for(size_t k=0;k<st.size();k++) sm+=rt[st[k]];
                            sd = val - sm/(double)st.size(); break;
                        }
                        [[fallthrough]];
                    case SDIFF_ALLPAIRS: default: {
                        double sum=0.0;
                        for(int j=0;j<nCG;j++) sum+=rt[j];
                        sd = nCG*val - sum; break;
                    }
                    }
                    int iv=(int)round(sd);
                    if(iv> 32767) iv= 32767;
                    if(iv<-32768) iv=-32768;
                    st[ci]=(short)iv;
                }
            }

            // Step 3: temporal first-difference in-place.
            // Boundary condition: sdiff[-1] = 0 (waveform starts at baseline).
            {
                std::vector<short> prev(static_cast<size_t>(nCG), 0);
                for(int s = 0; s < args.spikeLength; s++) {
                    short *st = sdWav.data() + s * nCG;
                    for(int ci = 0; ci < nCG; ci++) {
                        int diff = (int)st[ci] - (int)prev[static_cast<size_t>(ci)];
                        prev[static_cast<size_t>(ci)] = st[ci];
                        if(diff> 32767) diff= 32767;
                        if(diff<-32768) diff=-32768;
                        st[ci] = (short)diff;
                    }
                }
            }

            // Reject waveforms with all-zero or constant channels.
            // These arise from hardware dropouts or stuck ADC lines and
            // produce degenerate PCA features and corrupt cluster models.
            // -F: also reject a RAW run of more than flatRunMax identical
            // samples on any channel (a back-filled gap inside the window).
            const bool flatRun = (args.flatRunMax > 0)
                && hasFlatRun(rawWav.data(), nCG, args.spikeLength, args.flatRunMax);
            if (flatRun || isCutoutOrFlat(sdWav.data(), nCG, args.spikeLength)) {
                rejectedOrigIdx[static_cast<size_t>(grp)].insert(ev.origIdx);
                if (flatRun) ++flatRunRejected[static_cast<size_t>(grp)];
                // The streamed .spk must keep one row per spike (origIdx order):
                // the compaction below reads it back that way and drops rejected
                // rows.  Skipping the row here shifted every later spike by one.
                if (!useInMemory[grp]) {
                    const std::vector<short> zeros(static_cast<size_t>(args.spikeLength * nCG), 0);
                    fwrite(zeros.data(), sizeof(short), zeros.size(), streamFiles[grp]);
                }
                continue;  // do not write this waveform
            }

            // Write transformed waveform.
            if(useInMemory[grp]) {
                short *dst = allWaveforms[grp].data() + (size_t)ev.origIdx * wLen;
                memcpy(dst, sdWav.data(),
                       static_cast<size_t>(args.spikeLength * nCG) * sizeof(short));
            } else {
                fwrite(sdWav.data(), sizeof(short),
                       static_cast<size_t>(args.spikeLength * nCG),
                       streamFiles[grp]);
            }
        }
        fclose(seqF);
        if(args.isInputFileProvided) { prog.endStage(); prog.finish(); }
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

    // ── De-duplicate re-centred positions (-U) ─────────────────────────────
    // Re-centring can move two detections onto the same (or nearly the same)
    // sample.  Among the surviving spikes of a group, in time order, drop any
    // whose re-centred position is within dedupSamples of the previous KEPT one;
    // the earlier spike is kept.  Done here, before compaction, so the dropped
    // spikes leave .res and .spk together like any other rejection.
    if(args.dedupSamples >= 0) {
        for(int grp = 0; grp < nbGroups; grp++) {
            if(channelNb_grp[grp] == 0) continue;
            auto &rej = rejectedOrigIdx[static_cast<size_t>(grp)];
            const vector<int64_t> &ts = resTimestamps[grp];
            vector<int> order;
            order.reserve(ts.size());
            for(int s = 0; s < (int)ts.size(); s++)
                if(!rej.count(s)) order.push_back(s);
            std::stable_sort(order.begin(), order.end(),
                             [&ts](int x, int y){ return ts[(size_t)x] < ts[(size_t)y]; });
            int64_t last = 0; bool have = false;
            for(int s : order) {
                if(have && ts[(size_t)s] - last <= (int64_t)args.dedupSamples) {
                    rej.insert(s);
                    ++dupRejected[static_cast<size_t>(grp)];
                    continue;
                }
                last = ts[(size_t)s]; have = true;
            }
        }
    }
    for(int grp = 0; grp < nbGroups; grp++) {
        if(channelNb_grp[grp] == 0) continue;
        if(args.flatRunMax > 0 || args.dedupSamples >= 0)
            cout << "  Group " << grp+1 << ": removed "
                 << flatRunRejected[static_cast<size_t>(grp)] << " spike(s) with a flat run > "
                 << args.flatRunMax << " samples, "
                 << dupRejected[static_cast<size_t>(grp)] << " duplicate(s) within "
                 << max(args.dedupSamples, 0) << " samples\n";
    }

    // ── Compact .res and .spk: remove rejected (flat/cutout) spikes ─────────
    // For each group, filter resTimestamps to exclude rejected origIdx values,
    // rewrite .res, and compact the in-memory waveform buffer or rebuild .spk
    // from disk for the streaming case.
    int totalRejected = 0;
    for(int grp = 0; grp < nbGroups; grp++) {
        if(channelNb_grp[grp] == 0) continue;
        const auto& rej = rejectedOrigIdx[static_cast<size_t>(grp)];
        if(rej.empty()) continue;
        const int nCG  = channelNb_grp[grp];
        const int wavLen = args.spikeLength * nCG;

        // Build compacted timestamp list
        vector<int64_t> newTs;
        newTs.reserve(resTimestamps[grp].size());
        for(int s = 0; s < (int)resTimestamps[grp].size(); s++)
            if(!rej.count(s)) newTs.push_back(resTimestamps[grp][static_cast<size_t>(s)]);

        cerr << "  Group " << grp+1 << ": removed " << rej.size()
             << " rejected spike(s) (flat/cutout/duplicate) out of " << resTimestamps[grp].size() << "\n";
        totalRejected += static_cast<int>(rej.size());

        // Rewrite .res
        FILE *rf = fopen(resFileNames[static_cast<size_t>(grp)].c_str(), "wb");
        if(rf) {
            if(!newTs.empty())
                fwrite(newTs.data(), sizeof(int64_t), newTs.size(), rf);
            fclose(rf);
        }
        resTimestamps[grp] = std::move(newTs);

        if(useInMemory[grp]) {
            // allWaveforms was written with gaps (rejected entries were
            // skipped), so compact it: copy non-rejected waveforms in order.
            vector<short>& wbuf = allWaveforms[grp];
            int writeIdx = 0;
            for(int s = 0; s < (int)(wbuf.size() / wavLen); s++) {
                if(rej.count(s)) continue;
                if(writeIdx != s) {
                    const short *src = wbuf.data() + (size_t)s       * wavLen;
                          short *dst = wbuf.data() + (size_t)writeIdx * wavLen;
                    memmove(dst, src, (size_t)wavLen * sizeof(short));
                }
                writeIdx++;
            }
            wbuf.resize(static_cast<size_t>(writeIdx) * wavLen);
            // The .spk file was written before this rejection pass and still holds
            // a zeroed slot for each rejected spike; rewrite it from the compacted
            // buffer so .spk and .res stay in lockstep even when the sort pass below
            // is skipped (freshly extracted spikes are already in time order).
            FILE *csf = fopen(spkFileNames[static_cast<size_t>(grp)].c_str(), "wb");
            if(csf) {
                if(!wbuf.empty())
                    fwrite(wbuf.data(), sizeof(short), wbuf.size(), csf);
                fclose(csf);
            }
        } else {
            // Streaming path: .spk was written to disk; read back, compact,
            // and rewrite.
            FILE *sf = fopen(spkFileNames[static_cast<size_t>(grp)].c_str(), "rb");
            if(sf) {
                const int nSpkOld = (int)resTimestamps[grp].size() + (int)rej.size();
                vector<short> tmpBuf(static_cast<size_t>(nSpkOld) * wavLen);
                { size_t _r = fread(tmpBuf.data(), sizeof(short),
                                    static_cast<size_t>(nSpkOld) * wavLen, sf); (void)_r; }
                fclose(sf);
                sf = fopen(spkFileNames[static_cast<size_t>(grp)].c_str(), "wb");
                if(sf) {
                    for(int s = 0; s < nSpkOld; s++) {
                        if(rej.count(s)) continue;
                        fwrite(tmpBuf.data() + (size_t)s * wavLen,
                               sizeof(short), wavLen, sf);
                    }
                    fclose(sf);
                }
            }
        }
    }
    // ── Sort spikes by refined timestamp ──────────────────────────────────
    // Peak refinement may have shifted per-spike timestamps by up to
    // ±halfSearch samples. Re-sort both timestamps and waveforms together
    // so the output .res and .spk files are in ascending time order.
    for(int grp = 0; grp < nbGroups; grp++) {
        if(channelNb_grp[grp] == 0) continue;
        const int nCG    = channelNb_grp[grp];
        const int wavLen = args.spikeLength * nCG;
        vector<int64_t>& ts = resTimestamps[grp];
        const int n = (int)ts.size();
        if(n < 2) continue;

        // Build sort permutation
        vector<int> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(),
                  [&ts](int a, int b){ return ts[a] < ts[b]; });

        // Check if already sorted
        bool sorted = true;
        for(int i = 0; i < n && sorted; i++) sorted = (idx[i] == i);
        if(sorted) continue;

        // Apply permutation to timestamps
        vector<int64_t> tsSorted(n);
        for(int i = 0; i < n; i++) tsSorted[i] = ts[idx[i]];
        ts = std::move(tsSorted);

        // Write sorted .res
        FILE *rf = fopen(resFileNames[static_cast<size_t>(grp)].c_str(), "wb");
        if(rf) {
            fwrite(ts.data(), sizeof(int64_t), (size_t)n, rf);
            fclose(rf);
        }

        // Apply same permutation to waveforms
        if(useInMemory[grp]) {
            vector<short>& wbuf = allWaveforms[grp];
            vector<short> wSorted((size_t)n * wavLen);
            for(int i = 0; i < n; i++)
                std::copy(wbuf.data() + (size_t)idx[i] * wavLen,
                          wbuf.data() + (size_t)idx[i] * wavLen + wavLen,
                          wSorted.data() + (size_t)i * wavLen);
            wbuf = std::move(wSorted);
            // Write sorted waveforms back to the output .spk file (untagged temp)
            FILE *sf = fopen(spkFileNames[static_cast<size_t>(grp)].c_str(), "wb");
            if(sf) {
                fwrite(wbuf.data(), sizeof(short), (size_t)n * wavLen, sf);
                fclose(sf);
            }
        } else {
            // Re-read, permute, rewrite .spk
            FILE *sf = fopen(spkFileNames[static_cast<size_t>(grp)].c_str(), "rb");
            if(sf) {
                vector<short> raw((size_t)n * wavLen);
                { size_t _r = fread(raw.data(), sizeof(short),
                                    (size_t)n * wavLen, sf); (void)_r; }
                fclose(sf);
                sf = fopen(spkFileNames[static_cast<size_t>(grp)].c_str(), "wb");
                if(sf) {
                    for(int i = 0; i < n; i++)
                        fwrite(raw.data() + (size_t)idx[i] * wavLen,
                               sizeof(short), wavLen, sf);
                    fclose(sf);
                }
            }
        }
    }

    // ── Persist the re-centred timestamps ─────────────────────────────────
    // The two passes above rewrite .res only when a spike was rejected or the
    // re-centring reordered spikes.  With a narrow window (-W) neither need
    // happen, and .res would keep the PASS-1 detection times while every spike
    // was re-centred -- so the final, compacted, sorted timestamps are always
    // written here.  (With the historical wide window a reorder virtually always
    // occurs, so this matches what those runs already wrote.)
    for(int grp = 0; grp < nbGroups; grp++) {
        if(channelNb_grp[grp] == 0) continue;
        FILE *rf = fopen(resFileNames[static_cast<size_t>(grp)].c_str(), "wb");
        if(!rf) {
            fprintf(stderr, "error: cannot write '%s'\n",
                    resFileNames[static_cast<size_t>(grp)].c_str());
            exit(1);
        }
        if(!resTimestamps[grp].empty())
            fwrite(resTimestamps[grp].data(), sizeof(int64_t),
                   resTimestamps[grp].size(), rf);
        fclose(rf);
    }

    if(totalRejected > 0)
        cerr << "  Total rejected spikes removed: " << totalRejected << "\n";

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
