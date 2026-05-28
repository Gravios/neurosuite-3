/* ============================================================================
 * klusters_realign.h
 *
 * Klusters-faithful per-spike waveform realignment for KlustaKwikExp.
 * Ports the algorithm from src/klusters/src/spikerealign.cpp into a form
 * usable inside KKE's clustering pipeline.
 *
 * Why this exists
 * ---------------
 * The legacy KKE TimeShiftAlignPhase chooses one shift per cluster (or
 * per-spike via variance minimisation across candidate shifts) by picking
 * the candidate δ that minimises per-dimension variance after projecting
 * through pre-shifted PCA bases.  That algorithm is good at compensating
 * for slow drift but produces visibly "loose" per-spike alignment in the
 * final klusters display — peaks don't all sit on the same sample.
 *
 * The klusters interactive realignment ("Realign top-ch" toolbar button)
 * does something different and visibly better: per-spike normalised
 * cross-correlation against a pre-aligned cluster mean template, using
 * the same XcorrDispatch library (CUDA/HIP/SYCL/OpenMP) that KKE already
 * links against.  That algorithm is what users see when they hit the
 * realign button and get a beautifully tight cluster.
 *
 * This module ports that algorithm so KKE can apply it automatically as
 * a final phase before TimeShiftFinalize commits all shifts to .spk/.fet.
 *
 * Algorithm (from spikerealign.cpp:437-566)
 * ------------------------------------------
 *  1. For one cluster, gather its spike waveforms (sample-major .spk
 *     layout: a[s*nChan + ch]).
 *  2. Compute the mean waveform across all spikes in the cluster.
 *  3. Pre-align the mean: find the sample where Σ_ch |meanWv[s, ch]| is
 *     maximal (the "true" peak), then roll the mean so that sample lands
 *     at peakPos.  Without this step, the xcorr shifts each spike *away*
 *     from the true peak rather than toward it.
 *  4. Re-pack to channel-major (a[ch*nSamp + s]) and call
 *     XcorrDispatch::compute() with minScore=0 to get the optimal per-
 *     spike shift in [-maxShift, +maxShift].  Pre-alignment of the
 *     template ensures lag=0 wins when the spike is already well aligned.
 *
 * What's NOT in this module
 * -------------------------
 *  • Reading from disk (caller passes waveforms; KK provides
 *    TimeShiftReadSpikeWave wrapping the mmap or fread path).
 *  • Committing shifts to m_cumShift (caller does this; the module is
 *    pure function of its inputs).
 *  • .fil re-extraction (TimeShiftFinalize already does this at the end
 *    of the run, using m_cumShift).
 *
 * Cost
 * ----
 *  Per cluster of N spikes:
 *      Template build:    O(N · nChan · nSamples)        [CPU]
 *      Template prealign: O(nChan · nSamples)            [CPU]
 *      xcorr:             O(N · nChan · nSamples · maxShift)  [GPU if avail]
 *  At nChan=8, nSamples=32, maxShift=8 the GPU kernel processes ~1M spikes
 *  per second on RTX 5070 Ti; CPU path ~150k/sec.  Even on CPU, a full
 *  pass on 189k spikes finishes in ~1-2 seconds.
 * ========================================================================== */

#pragma once

#include <cstdint>
#include <vector>

namespace KlustersRealign {

/* ---------------------------------------------------------------------------
 * RealignStats — aggregated counters for one full RealignAllClusters pass.
 * --------------------------------------------------------------------------- */
struct RealignStats {
    int    nClustersProcessed = 0;
    int    nClustersSkipped   = 0;   // too-small, degenerate template, etc.
    int    nSpikesEvaluated   = 0;
    int    nSpikesRealigned   = 0;   // shift != 0
    int    nSpikesReadFailed  = 0;
    int    maxAbsShift        = 0;
    double meanAbsShift       = 0.0;
};

/* ---------------------------------------------------------------------------
 * PreAlignTemplate — shift a cluster mean waveform so its peak (defined as
 *   Σ_ch |amp|, like klusters does) lands at sample peakPos.
 *
 * meanWv layout: sample-major, [s*nChan + ch], length nChan*nSamples.
 * Modifies meanWv in place.  Out-of-window samples after shift are zero-
 * filled.  Returns the applied shift (positive = template was rolled
 * later, negative = rolled earlier).
 *
 * This is the step that makes the per-spike xcorr in step 4 produce
 * lag=0 for already-aligned spikes.  Klusters does this at
 * spikerealign.cpp:505-525.
 * --------------------------------------------------------------------------- */
int PreAlignTemplate(int16_t* meanWv,
                     int nChan, int nSamples, int peakPos);

/* ---------------------------------------------------------------------------
 * ComputeClusterShifts — main entry point for one cluster.
 *
 * Inputs:
 *   waveforms:   nSpikes × (nChan · nSamples) int16, sample-major layout
 *                  (a[s*nChan + ch], matches .spk on disk).
 *   nChan:       channels per spike
 *   nSamples:    samples per channel per spike
 *   peakPos:     sample index where the peak should land (typically nSamples/2)
 *   maxShift:    search radius in samples (klusters default = 8;
 *                  KKE default suggestion = nSamples/4)
 *
 * Outputs:
 *   outShifts:   length nSpikes, in [-maxShift, +maxShift]
 *   outScores:   length nSpikes, normalised xcorr at the best lag, in [-1, 1]
 *
 * Side effects: none.  Module is pure; caller decides whether to commit
 *   shifts to m_cumShift or any other state.
 *
 * Returns false on size/argument errors, true otherwise.
 *
 * Internal flow (mirrors spikerealign.cpp:478-566):
 *   1. Build double-precision cluster mean, round to int16.
 *   2. PreAlignTemplate(mean, ...).
 *   3. Re-pack mean to channel-major scratch buffer (tmplBuf).
 *   4. Re-pack waveforms to channel-major scratch buffer (waveBuf).
 *   5. XcorrDispatch::compute(...) — fills shifts + scores.
 *
 * The two scratch buffers are sized internally; on a 64 GB system you can
 * realign up to ~5M spikes per cluster before the std::vector for waveBuf
 * exceeds 2 GB (typical clusters are far smaller).
 * --------------------------------------------------------------------------- */
bool ComputeClusterShifts(
    const std::vector<std::vector<int16_t>>& waveforms,
    int nChan, int nSamples, int peakPos, int maxShift,
    std::vector<int>&   outShifts,
    std::vector<float>& outScores);

/* ---------------------------------------------------------------------------
 * Same as above but with a pre-flattened waveform buffer.  Avoids the cost
 * of building a vector-of-vectors when the caller already has a contiguous
 * block (e.g. KK::Data row major slabs).  waveforms is sample-major per
 * spike: waveforms[s_idx * (nChan*nSamples) + s*nChan + ch].
 * --------------------------------------------------------------------------- */
bool ComputeClusterShiftsFlat(
    const int16_t* waveforms, int nSpikes,
    int nChan, int nSamples, int peakPos, int maxShift,
    std::vector<int>&   outShifts,
    std::vector<float>& outScores);

/* ---------------------------------------------------------------------------
 * BuildClusterMedianWaveform — public median-template builder.
 *
 * Computes per-sample median across nSpikes waveforms (sample-major
 * layout) and writes the result as int16 into outMedian.  Exposed for
 * modules (e.g. KK::ClusterWaveformVariance) that need the median
 * template.  waveforms: [nSpikes × (nChan × nSamples)] int16,
 * sample-major.  outMedian resized to (nChan × nSamples).
 * O(nSpikes · nChan · nSamples).
 * --------------------------------------------------------------------------- */
void BuildClusterMedianWaveform(
    const int16_t* waveforms, int nSpikes,
    int nChan, int nSamples,
    std::vector<int16_t>& outMedian);

}  // namespace KlustersRealign
