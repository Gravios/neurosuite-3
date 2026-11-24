/***************************************************************************
 * realign_pass.h -- the realign's shift computation, on plain buffers.
 *
 * Given the waveforms of ONE cluster, find for each spike the lag that best
 * aligns it with the cluster's mean waveform, over a few passes:
 *
 *   1. template = the mean of the spikes as currently aligned (the int64 sum
 *      divided by n, truncated toward zero);
 *   2. the template is rolled (circularly) so that the sample where its
 *      summed |amplitude| is largest lands on `peakSample`;
 *   3. each spike is scored against it at every lag in [-maxShift, maxShift]
 *      by normalised cross-correlation (realign_xcorr.h), under the
 *      correlation window if one is asked for and on the `topChannels`
 *      largest channels of the template if asked for; a spike whose best
 *      score is below `minScore` is not moved in that pass;
 *   4. the moved spikes are rolled (circularly) by their lag and the lag is
 *      added to their cumulative shift; the next pass starts from 1.
 *      A pass that moves nothing ends the computation.
 *
 * No file, no cluster model, no Qt: Klusters' realignSpikes calls it and then
 * re-extracts the spikes at their new times, and anything else that has a
 * cluster's waveforms in memory can call it too.  It was a part of
 * KlustersDoc::realignSpikes until 2026-11-24; realign_pass_test.cpp pins its
 * results to that code's.
 *
 * Layout: channel-major, spike i at waveforms + i * nChan * nSamp, sample s of
 * channel ch at [ch * nSamp + s].  A positive shift means the spike's peak is
 * late: its new time is its old time + shift.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef REALIGN_PASS_H
#define REALIGN_PASS_H

#include <cstdint>
#include <vector>

namespace realign_pass {

/// The correlation window (the collision guard): which samples of the window count.
enum class Kernel { None, Gauss, Hann, Template };

struct Options {
    int    peakSample  = 0;      ///< 0-based sample the template's peak is put on
    int    maxShift    = 1;      ///< lags searched in a pass; the total over the passes can exceed it
    float  minScore    = 0.10f;  ///< a spike scoring lower in a pass is not moved in it
    int    iterations  = 2;      ///< passes at most
    int    topChannels = 0;      ///< > 0 and < nChan: only the template's largest channels count
    Kernel kernel      = Kernel::None;
    int    kernelWidth = 1;      ///< half-width in samples (Gauss, Hann)
};

struct Result {
    std::vector<int>   shift;    ///< per spike, summed over the passes
    std::vector<float> score;    ///< per spike, of the last pass that moved it (0: never moved)
    std::vector<int>   moved;    ///< per pass run, the spikes it moved
    int                error = 0;///< the xcorr back end's code when clusterShifts returns false
};

/** The shifts of one cluster's spikes.  @p waveforms is rolled in place: on return each
 *  spike is as aligned.  False when the xcorr back end failed (Result::error). */
bool clusterShifts(int16_t* waveforms, int64_t nSpikes, int nChan, int nSamp,
                   const Options& options, Result& result);

/** The per-sample weights of the correlation window, in the template's frame (its peak at
 *  @p peakSample): gauss = exp(-((s-p)/(w/2))^2 / 2), hann = the raised cosine over
 *  [p-w, p+w] and zero outside, template = the channel-summed energy of @p tmpl
 *  (channel-major), 3-sample box smoothed, scaled to a maximum of 1 and floored at 0.02.
 *  Empty for Kernel::None and for a zero template. */
std::vector<float> kernelWeights(Kernel kernel, int kernelWidth, int peakSample,
                                 int nSamp, int nChan, const int16_t* tmpl);

/// Roll one spike circularly: new[t] = old[(t + shift) mod nSamp], on every channel.
void rollSpike(int16_t* spike, int nChan, int nSamp, int shift);

} // namespace realign_pass

#endif // REALIGN_PASS_H
