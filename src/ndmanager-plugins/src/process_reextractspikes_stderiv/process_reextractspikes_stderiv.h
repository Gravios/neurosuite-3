/***************************************************************************
                   process_reextractspikes_stderiv.h
                   ---------------------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Description
 ===========
 Companion to process_reextractspikes that runs detection on a spatial-
 derivative + temporal first-difference shadow signal, identically to the
 extraction-stage process_extractspikes_stderiv.  Waveform extraction
 (Pass 2) reads from the ORIGINAL unmodified .fil, then writes the
 stderiv-transformed waveform to the .spkD output — bit-identical in
 layout and value space to what process_extractspikes_stderiv produces.
 This keeps the new-spike rows in the same amplitude space as the
 reference rows, so the .pcaD basis (trained on transformed waveforms)
 projects them into the same feature space as the reference data.

 Mask semantics are identical: candidate peaks within ± maskHalfWidth of
 any timestamp listed in the per-group mask .res.N are rejected.

 Spatial derivative orders mirror process_extractspikes_stderiv:
   0 SDIFF_NONE       bypass — temporal first-difference only
   1 SDIFF_FIRST      nearest-neighbour   s[i] = x[i] - x[i+1]
   2 SDIFF_LAPLACIAN  discrete Laplacian  s[i] = x[i] - 0.5*(x[i-1]+x[i+1])
   3 SDIFF_ALLPAIRS   all-pairwise sum    s[i] = n*x[i] - sum_j(x[j])
                      (default; no probe-order requirement)

 Output .spk files use the .spkD extension to match the ndm_extractspikes_
 stderiv convention; downstream tools that consume .spkD transparently
 accept these outputs without modification.
 ***************************************************************************/
#ifndef PROCESS_REEXTRACTSPIKES_STDERIV_H
#define PROCESS_REEXTRACTSPIKES_STDERIV_H

#include <cstdint>
#include <string>
#include <sys/types.h>

#define GROUP_SEPARATOR     ":"
#define CHANNEL_SEPARATOR   ","
#define SPIKE_TIME_OUT_EXT  "res"
#define SPIKE_REC_OUT_EXT   "spkD"

enum SdiffOrder {
    SDIFF_NONE      = 0,
    SDIFF_FIRST     = 1,
    SDIFF_LAPLACIAN = 2,
    SDIFF_ALLPAIRS  = 3,
    SDIFF_CUSTOM    = 4   // per-channel partner map supplied via -P sdiffPairs
};

struct ReextractStderivArgs {
    std::string inputFilePath;
    std::string outputBaseFileName;
    std::string maskBaseFileName;

    int spikeLength       = -1;
    int timeBeforeSpike   = -1;
    int peakSearchLength  = -1;
    int refractoryPeriod  = -1;
    int64_t maskHalfWidth = -1;

    int         totalChannelNumber = 0;
    std::string channelList;

    // Threshold computation (applied to sdiff signal)
    double thresholdFactor = -1.0;
    off_t  threshStartByte = 0;
    off_t  threshSizeBytes = 0;

    SdiffOrder sdiffOrder = SDIFF_ALLPAIRS;

    bool verbose = false;
};

#endif // PROCESS_REEXTRACTSPIKES_STDERIV_H
