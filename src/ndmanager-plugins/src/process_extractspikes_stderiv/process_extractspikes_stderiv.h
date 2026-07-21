/***************************************************************************
                   process_extractspikes_stderiv.h
                   -----------------------------
    copyright  : (C) 2024  Gravios / NeuroSuite-3 contributors
    description: Spike detection using spatial-derivative preprocessing.

 Architecture
 ============
 A spatial derivative is precomputed into a shadow buffer that mirrors the
 raw signal buffer in layout.  The proven detection engine from
 process_extractspikes then runs unchanged on the shadow buffer, inheriting
 all buffer-management and cross-buffer peak consolidation logic exactly.
 Waveform extraction (Pass 2) always reads the original unmodified signal.
 ***************************************************************************/
#ifndef PROCESS_EXTRACTSPIKES_STDERIV_H
#define PROCESS_EXTRACTSPIKES_STDERIV_H

#define BUFFER_CHANNEL_SIZE 50000  // time-samples per read chunk (per channel)
#define MAX_CHANNO          512    // maximum number of channels
#define RECORD_BYTE_SIZE    2      // bytes per sample (int16)
#define GROUP_SEPARATOR     ":"
#define CHANNEL_SEPARATOR   ","
#define SPIKE_TIME_OUT_EXT  "res"
// The engine writes UNtagged temp records (<base>.spk.<grp>); the
// ndm_extractspikes wrapper applies the chain-of-custody method tag to the
// final names (<session>.spk.stderiv.<grp>).  Keeping the binary method-
// agnostic lets one dispatcher own naming for all extraction methods.
#define SPIKE_REC_OUT_EXT   "spk"

#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/types.h>

// Spatial-derivative orders
//
//  0  SDIFF_NONE       bypass — behaves identically to process_extractspikes
//  1  SDIFF_FIRST      nearest-neighbour difference  s[i] = x[i] - x[i+1]
//                      (last channel uses x[i] - x[i-1])
//  2  SDIFF_LAPLACIAN  discrete Laplacian  s[i] = x[i] - 0.5*(x[i-1]+x[i+1])
//                      (edge channels use one-sided differences)
//  3  SDIFF_ALLPAIRS   sum of all pairwise differences (default)
//                      s[i] = sum_{j} (x[i] - x[j])  =  n*x[i] - sum(x[j])
//                           = n * (x[i] - mean(x))
//                      Does not require channels to be in probe order.
//                      Maximum common-mode rejection for any group geometry.
//  4  SDIFF_CUSTOM     per-channel partner map (see -P sdiffPairs).  Output
//                      channel i is x[i] - x[partner[i]], the partner taken from
//                      an explicit "a-b" pair list of 0-based within-group
//                      positions.  Lets a geometry-matched, physically local
//                      difference pattern be extracted directly.  A group whose
//                      size does not match the pattern falls back to order 3.
enum SdiffOrder {
    SDIFF_NONE      = 0,
    SDIFF_FIRST     = 1,
    SDIFF_LAPLACIAN = 2,
    SDIFF_ALLPAIRS  = 3,  // default
    SDIFF_CUSTOM    = 4   // per-channel partner map supplied via -P sdiffPairs
};

struct arguments {
    // I/O
    char *inputFileName;
    char *outputBaseFileName;
    bool  isInputFileProvided;
    bool  isOutputBaseFileProvided;

    // Waveform geometry
    int  spikeLength;
    int  timeBeforeSpike;
    int  peakLength;
    bool isSpikeLengthProvided;
    bool isTimeBeforeSpikeProvided;
    bool isPeakLengthProvided;

    // Detection timing
    int  refractoryPeriod;
    bool isRefractoryPeriodProvided;

    // Channel layout
    int   totalChannelNumber;
    char *channelList;
    bool  isTotalChannelNumberProvided;
    bool  isChannelListProvided;

    // Threshold computation (internal, applied to sdiff signal)
    double thresholdFactor;
    off_t  threshStartByte;
    off_t  threshSizeBytes;
    bool   isThresholdFactorProvided;
    bool   isThreshStartByteProvided;
    bool   isThreshSizeBytesProvided;

    // Spatial derivative
    SdiffOrder sdiffOrder;
    bool       isSdiffOrderProvided;

    // Detection mode (true = prefer negative peak, same default as original)
    bool isDisableAbs;

    // patch86: when true, read per-group .res files instead of detecting,
    // then re-extract waveforms at those exact timestamps (with the same
    // spatial+temporal derivative transform as the detection path).  The
    // thresholdFactor/refractoryPeriod/peakLength args are ignored.
    bool useExistingRes;
};

// ── Function declarations ─────────────────────────────────────────────────
double computeSDiff(const short *record, const int *chanList,
                    int idx, int nChanGrp, SdiffOrder order);

void computeSdiffThresholds(FILE *fp, off_t startByte, off_t sizeByte,
                             int nChanTot, int nGroups,
                             int **channelList, int *channelNb_group,
                             SdiffOrder order, double factor,
                             double **outThresholds);

int  getChannelsFromArg(int *channelNb_group, int **channelList,
                         const arguments &args);
bool checkChanAndThres(int **cList, int *cNb, int nbC, int *tNb, int nbT);
bool checkInputs(const arguments &args, int buf_sz, const FILE *fp);
void parseArgs(int argc, char **argv, arguments &args);

bool   isRefractoryPeriod(off_t lastId, off_t curId, const arguments &args);
double getThresholdFromChan(int chanId, int nChanGrp,
                             const int *chanList, const double *thresholds);

#endif // PROCESS_EXTRACTSPIKES_STDERIV_H
