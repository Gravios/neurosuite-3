/***************************************************************************
                   process_reextractspikes.h
                   -------------------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Description
 ===========
 Second-pass spike detection engine that (a) excludes any candidate peak
 falling within a user-specified mask window around spikes already listed
 in per-group .res.N files, and (b) applies a reduced threshold to recover
 low-amplitude events.  Output .res and .spk files carry the -reextr-tmp
 stem so they do not clobber the original session files.  The downstream
 shadow-cluster assignment step (process_shadowcluster) merges them back
 with semantic cluster IDs.

 Pipeline position
 -----------------
   After ndm_extractspikes + ndm_pca + ndm_klustakwik (or klusters) have
   produced an initial clustering.  Before process_shadowcluster.

 I/O model
 ---------
   Reads .fil via mmap so a streaming state machine is unnecessary; the
   detection loop is a straightforward per-sample scan with
   peak-search-window local-extremum refinement.

 File layout conventions (neurosuite-3)
 --------------------------------------
   .fil / .dat : int16 sample-major, channel-interleaved [t*nChan + c]
   .res.N      : int64 spike-peak timestamps, chronological, no header
   .spk.N      : int16 waveform samples, sample-major within each
                 waveform:  wav[s * nChanGroup + c]; no header

 Notes
 -----
   Thresholds are supplied by the calling bash wrapper (-t flag), mirroring
   process_extractspikes.  The wrapper is responsible for running
   process_medianthreshold (or an equivalent robust noise estimator) and
   scaling the per-channel baseline by reextractThresholdFactor.  We do
   NOT recompute thresholds inside this binary — the bash wrapper owns
   that policy choice.
 ***************************************************************************/
#ifndef PROCESS_REEXTRACTSPIKES_H
#define PROCESS_REEXTRACTSPIKES_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/types.h>
#include <vector>

#define GROUP_SEPARATOR     ":"
#define CHANNEL_SEPARATOR   ","
#define SPIKE_TIME_OUT_EXT  "res"
#define SPIKE_REC_OUT_EXT   "spk"

struct ReextractArgs {
    // I/O ------------------------------------------------------------------
    std::string inputFilePath;        // .fil to scan
    std::string outputBaseFileName;   // stem for .res.N / .spk.N outputs
    std::string maskBaseFileName;     // stem for .res.N mask inputs

    // Waveform geometry -----------------------------------------------------
    int spikeLength       = -1;       // samples per waveform
    int timeBeforeSpike   = -1;       // peak position within waveform
    int peakSearchLength  = -1;       // local-extremum search window

    // Detection timing ------------------------------------------------------
    int   refractoryPeriod = -1;      // minimum samples between accepted spikes
    int64_t maskHalfWidth  = -1;      // reject candidate if within ± of any
                                      // masked timestamp (defaults to refractory)

    // Channel layout --------------------------------------------------------
    int         totalChannelNumber = 0;
    std::string channelList;
    std::string thresList;

    bool verbose = false;
};

// Detection helpers ---------------------------------------------------------
// Local peak (same semantics as process_extractspikes::isRealPeak).
bool isRealPeak(double peak, double beforePeak, double afterPeak);

// Binary-search: is |t - any mask entry| <= halfWidth?
bool isMasked(const std::vector<int64_t> &sortedMask,
              int64_t t, int64_t halfWidth);

#endif // PROCESS_REEXTRACTSPIKES_H
