/* ============================================================================
 * klusters_realign.cpp
 *
 * Implementation of the klusters-faithful per-spike realignment algorithm
 * for KiloKlustaKwik.  See header for design notes.
 *
 * Faithfulness check vs src/klusters/src/spikerealign.cpp:
 *
 *   spikerealign.cpp:489-499  →  build_cluster_mean_double()
 *   spikerealign.cpp:505-525  →  PreAlignTemplate()
 *   spikerealign.cpp:539-543  →  pack_template_channel_major()
 *   spikerealign.cpp:545-555  →  pack_waveforms_channel_major()
 *   spikerealign.cpp:562-566  →  XcorrDispatch::compute() call
 *
 * Differences from klusters:
 *   • klusters operates on Data*, this module operates on plain buffers.
 *   • klusters does .fil re-extraction inline; here we just compute shifts
 *     and let TimeShiftFinalize handle re-extraction at the end of the run.
 *   • klusters re-projects through .evec to update .fet rows immediately;
 *     here KKE's PCA re-projection happens via RefeaturizeFromShifts in
 *     TimeShiftFinalize, using KKE's own internal eigvecs.
 * ========================================================================== */

#include "klusters_realign.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "../libklustersshared/src/xcorr/realign_xcorr.h"

namespace KlustersRealign {

// ---------------------------------------------------------------------------
// PreAlignTemplate
// ---------------------------------------------------------------------------
int PreAlignTemplate(int16_t* meanWv,
                     int nChan, int nSamples, int peakPos)
{
    if (!meanWv || nChan <= 0 || nSamples <= 0) return 0;
    if (peakPos < 0 || peakPos >= nSamples)     return 0;

    // ── Find true peak via Σ_ch |amp|.  Klusters spikerealign.cpp:506-513.
    int    meanPeakSamp = peakPos;
    double bestAmp      = -1.0;
    for (int s = 0; s < nSamples; ++s) {
        double amp = 0.0;
        for (int ch = 0; ch < nChan; ++ch)
            amp += std::abs(static_cast<double>(meanWv[s * nChan + ch]));
        if (amp > bestAmp) { bestAmp = amp; meanPeakSamp = s; }
    }

    const int tmplShift = peakPos - meanPeakSamp;
    if (tmplShift == 0) return 0;

    // ── Roll the template by tmplShift samples.  Out-of-range samples
    //    are zero-filled (matches klusters spikerealign.cpp:516-522).
    const size_t nPts = static_cast<size_t>(nChan) * nSamples;
    std::vector<int16_t> shifted(nPts, 0);
    for (int s = 0; s < nSamples; ++s) {
        int src = s - tmplShift;
        if (src < 0 || src >= nSamples) continue;
        for (int ch = 0; ch < nChan; ++ch)
            shifted[s * nChan + ch] = meanWv[src * nChan + ch];
    }
    std::memcpy(meanWv, shifted.data(), nPts * sizeof(int16_t));
    return tmplShift;
}

// ---------------------------------------------------------------------------
// Internal: build double-precision cluster mean from sample-major spikes.
// ---------------------------------------------------------------------------
static void build_cluster_mean(
    const int16_t* waveforms, int nSpikes,
    int nChan, int nSamples,
    std::vector<int16_t>& meanWv)
{
    const size_t nPts = static_cast<size_t>(nChan) * nSamples;
    std::vector<double> meanD(nPts, 0.0);
    for (int si = 0; si < nSpikes; ++si) {
        const int16_t* wv = waveforms + static_cast<ptrdiff_t>(si) * nPts;
        for (size_t p = 0; p < nPts; ++p)
            meanD[p] += static_cast<double>(wv[p]);
    }
    const double invN = (nSpikes > 0) ? 1.0 / nSpikes : 0.0;
    meanWv.resize(nPts);
    for (size_t p = 0; p < nPts; ++p)
        meanWv[p] = static_cast<int16_t>(std::lround(meanD[p] * invN));
}

// ---------------------------------------------------------------------------
// ComputeClusterShiftsFlat — flat-buffer entry point.
// ---------------------------------------------------------------------------
bool ComputeClusterShiftsFlat(
    const int16_t* waveforms, int nSpikes,
    int nChan, int nSamples, int peakPos, int maxShift,
    std::vector<int>&   outShifts,
    std::vector<float>& outScores)
{
    if (!waveforms || nSpikes <= 0 || nChan <= 0 || nSamples <= 0) return false;
    if (peakPos < 0 || peakPos >= nSamples)                        return false;
    if (maxShift < 0 || maxShift >= nSamples / 2)                  return false;

    const size_t nPts = static_cast<size_t>(nChan) * nSamples;

    // ── Build cluster mean (sample-major).
    std::vector<int16_t> meanWv;
    build_cluster_mean(waveforms, nSpikes, nChan, nSamples, meanWv);

    // ── Pre-align template so its peak sits at peakPos.
    PreAlignTemplate(meanWv.data(), nChan, nSamples, peakPos);

    // ── Re-pack template to channel-major for the dispatcher.
    //    tmplBuf[ch*nSamples + s]
    std::vector<int16_t> tmplBuf(nPts);
    for (int ch = 0; ch < nChan; ++ch)
        for (int s = 0; s < nSamples; ++s)
            tmplBuf[static_cast<size_t>(ch) * nSamples + s] =
                meanWv[s * nChan + ch];

    // ── Re-pack waveforms to channel-major per-spike, contiguous overall.
    //    waveBuf[si][ch*nSamples + s]
    std::vector<int16_t> waveBuf(static_cast<size_t>(nSpikes) * nPts);
    for (int si = 0; si < nSpikes; ++si) {
        const int16_t* src = waveforms + static_cast<ptrdiff_t>(si) * nPts;
        int16_t* dst = waveBuf.data() + static_cast<ptrdiff_t>(si) * nPts;
        for (int ch = 0; ch < nChan; ++ch)
            for (int s = 0; s < nSamples; ++s)
                dst[ch * nSamples + s] = src[s * nChan + ch];
    }

    // ── Output buffers.
    outShifts.assign(static_cast<size_t>(nSpikes), 0);
    outScores.assign(static_cast<size_t>(nSpikes), 0.0f);

    // ── Run the xcorr dispatcher with minScore=0; pre-aligned template
    //    ensures lag=0 wins naturally for well-aligned spikes.
    const int rc = XcorrDispatch::compute(
        waveBuf.data(), tmplBuf.data(),
        nSpikes, nChan, nSamples,
        maxShift, /*minScore=*/0.0f,
        outShifts.data(), outScores.data());

    return rc == 0;
}

// ---------------------------------------------------------------------------
// ComputeShiftsAgainstTemplateFlat — like ComputeClusterShiftsFlat but the
// template is supplied by the caller (not built from the waveforms).
// ---------------------------------------------------------------------------
bool ComputeShiftsAgainstTemplateFlat(
    const int16_t* waveforms, int nSpikes,
    const int16_t* tmplSampleMajor,
    int nChan, int nSamples, int peakPos, int maxShift,
    std::vector<int>&   outShifts,
    std::vector<float>& outScores)
{
    if (!waveforms || !tmplSampleMajor || nSpikes <= 0
        || nChan <= 0 || nSamples <= 0)                           return false;
    if (peakPos < 0 || peakPos >= nSamples)                       return false;
    if (maxShift < 0 || maxShift >= nSamples / 2)                 return false;

    const size_t nPts = static_cast<size_t>(nChan) * nSamples;

    // Copy the supplied template (sample-major), pre-align it so its peak
    // sits at peakPos — exactly as ComputeClusterShiftsFlat does for the
    // internally-built template.
    std::vector<int16_t> tmplWv(tmplSampleMajor, tmplSampleMajor + nPts);
    PreAlignTemplate(tmplWv.data(), nChan, nSamples, peakPos);

    // Re-pack template to channel-major for the dispatcher.
    std::vector<int16_t> tmplBuf(nPts);
    for (int ch = 0; ch < nChan; ++ch)
        for (int s = 0; s < nSamples; ++s)
            tmplBuf[static_cast<size_t>(ch) * nSamples + s] =
                tmplWv[s * nChan + ch];

    // Re-pack waveforms to channel-major per-spike.
    std::vector<int16_t> waveBuf(static_cast<size_t>(nSpikes) * nPts);
    for (int si = 0; si < nSpikes; ++si) {
        const int16_t* src = waveforms + static_cast<ptrdiff_t>(si) * nPts;
        int16_t* dst = waveBuf.data() + static_cast<ptrdiff_t>(si) * nPts;
        for (int ch = 0; ch < nChan; ++ch)
            for (int s = 0; s < nSamples; ++s)
                dst[ch * nSamples + s] = src[s * nChan + ch];
    }

    outShifts.assign(static_cast<size_t>(nSpikes), 0);
    outScores.assign(static_cast<size_t>(nSpikes), 0.0f);

    const int rc = XcorrDispatch::compute(
        waveBuf.data(), tmplBuf.data(),
        nSpikes, nChan, nSamples,
        maxShift, /*minScore=*/0.0f,
        outShifts.data(), outScores.data());

    return rc == 0;
}

// ---------------------------------------------------------------------------
// ComputeClusterShifts — vector-of-vector entry point, delegates to Flat.
// ---------------------------------------------------------------------------
bool ComputeClusterShifts(
    const std::vector<std::vector<int16_t>>& waveforms,
    int nChan, int nSamples, int peakPos, int maxShift,
    std::vector<int>&   outShifts,
    std::vector<float>& outScores)
{
    const int nSpikes = static_cast<int>(waveforms.size());
    if (nSpikes <= 0 || nChan <= 0 || nSamples <= 0) return false;

    const size_t nPts = static_cast<size_t>(nChan) * nSamples;

    // Flatten into a contiguous buffer.
    std::vector<int16_t> flat(static_cast<size_t>(nSpikes) * nPts);
    for (int si = 0; si < nSpikes; ++si) {
        if (waveforms[static_cast<size_t>(si)].size() != nPts) return false;
        std::memcpy(flat.data() + static_cast<ptrdiff_t>(si) * nPts,
                    waveforms[static_cast<size_t>(si)].data(),
                    nPts * sizeof(int16_t));
    }
    return ComputeClusterShiftsFlat(flat.data(), nSpikes,
                                    nChan, nSamples, peakPos, maxShift,
                                    outShifts, outScores);
}

// ---------------------------------------------------------------------------
// BuildClusterMedianWaveform — per-sample median across nSpikes waveforms.
// Self-contained: gathers each (channel,sample) point across spikes into a
// scratch column and selects the middle element via std::nth_element.
// ---------------------------------------------------------------------------
void BuildClusterMedianWaveform(
    const int16_t* waveforms, int nSpikes,
    int nChan, int nSamples,
    std::vector<int16_t>& medianWv)
{
    const size_t nPts = static_cast<size_t>(nChan) * nSamples;
    medianWv.resize(nPts);
    if (nSpikes <= 0) {
        std::fill(medianWv.begin(), medianWv.end(), static_cast<int16_t>(0));
        return;
    }
    std::vector<int16_t> col(static_cast<size_t>(nSpikes));
    const int midIdx = nSpikes / 2;
    for (size_t p = 0; p < nPts; ++p) {
        for (int si = 0; si < nSpikes; ++si)
            col[static_cast<size_t>(si)] =
                waveforms[static_cast<ptrdiff_t>(si) * nPts + p];
        std::nth_element(col.begin(), col.begin() + midIdx, col.end());
        medianWv[static_cast<size_t>(p)] = col[static_cast<size_t>(midIdx)];
    }
}

}  // namespace KlustersRealign
