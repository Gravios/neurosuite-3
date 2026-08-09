/***************************************************************************
 * driftmatrixthread.cpp — see driftmatrixthread.h.
 *
 * The mean-waveform pass mirrors TemplateMatrixThread::run() (shared .spk
 * reader tmReadSpikeFloat, one FILE* per cluster, OpenMP over clusters).  The
 * matrix pass uses the pure kernels in driftmatrixkernel.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "driftmatrixthread.h"

#include <functional>
#include "driftmatrixkernel.h"
#include "channelmask.h"
#include "templatematrixthread.h"   // tmReadSpikeFloat (shared .spk reader)
#include "sortabletable.h"

#include <QApplication>
#include <algorithm>
#include <cstdio>
#include <cstdint>

#ifdef _OPENMP
#include <omp.h>
#endif

void DriftMatrixThread::run()
{
    auto post = [this]() {
        QApplication::postEvent(&target, new DriftMatrixEvent(*this));
    };

    if (haveToStopProcessing) { post(); return; }

    // ── 1. Cluster list ──────────────────────────────────────────────────
    {
        const QList<dataType> allIds = data.clusterIds();
        if (!activeClusters.isEmpty()) {
            // Scoped: build the list DIRECTLY as [artefact, noise, children...]
            // rather than building every cluster and filtering after.  The matrix
            // is sized from this list, so what goes in it is the matrix -- a
            // parent with 7 children gives 9x9.
            //
            // Both reserve bins are included even though the unscoped build below
            // skips artefact.  Skipping it there is a judgement about a
            // session-wide matrix, where an artefact row carries no waveform and
            // costs a row out of hundreds.  Here the matrix is nine rows and the
            // artefact bin is one of the places a child can be sent, so its column
            // is part of what the view is for.  Included regardless of spike count,
            // so the shape does not change under the user as bins empty and fill.
            for (dataType id : allIds)
                if (id <= 1) clusterList.append(static_cast<int>(id));
            for (int id : activeClusters)
                if (id > 1 && data.nbOfSpikes(id) > 0) clusterList.append(id);
        } else {
            for (dataType id : allIds)
                if (id >= 1 && data.nbOfSpikes(id) > 0)
                    clusterList.append(static_cast<int>(id));
        }
        std::sort(clusterList.begin(), clusterList.end());
    }

    const int nClusters = clusterList.size();
    if (nClusters < 2) { post(); return; }

    const int nChan    = data.nbOfChannels();
    const int nSamp    = data.nbSamplesPerWaveform();
    const int nPts     = nChan * nSamp;
    const int maxShift = std::max(1, nSamp / 4);
    nChanCached = nChan; nSampCached = nSamp; maxShiftCached = maxShift;

    const QString spkPath = data.getSpkFileName();
    if (spkPath.isEmpty() || nPts <= 0) { post(); return; }

    depthsValid = (static_cast<int>(depths.size()) == nChan);

    // ── 2. Pre-fetch spike file indices (serial, mutex-safe) ────────────────
    std::vector<std::vector<int>> allFileIdx(static_cast<size_t>(nClusters));
    for (int ci = 0; ci < nClusters; ++ci) {
        if (haveToStopProcessing) { post(); return; }
        SortableTable posTable;
        if (!data.spikePositions(clusterList[ci], posTable)) continue;
        const long nSpk = static_cast<long>(data.nbOfSpikes(clusterList[ci]));
        allFileIdx[static_cast<size_t>(ci)].reserve(static_cast<size_t>(nSpk));
        for (long s = 0; s < nSpk; ++s)
            allFileIdx[static_cast<size_t>(ci)].push_back(
                static_cast<int>(posTable(1, s + 1)) - 1);
    }
    if (haveToStopProcessing) { post(); return; }

    // ── 3. Parallel mean waveforms — one FILE* per cluster ──────────────────
    meanWav.assign(static_cast<size_t>(nClusters),
                   std::vector<float>(static_cast<size_t>(nPts), 0.0f));

    const QByteArray spkBytes = spkPath.toLocal8Bit();
    const char*      spkCStr  = spkBytes.constData();

#pragma omp parallel for schedule(dynamic,1) default(none) \
    shared(meanWav, allFileIdx) \
    firstprivate(nClusters, nPts, nChan, nSamp, spkCStr)
    for (int ci = 0; ci < nClusters; ++ci) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;

        const auto& fidx = allFileIdx[static_cast<size_t>(ci)];
        const long  nSpk = static_cast<long>(fidx.size());
        if (nSpk == 0) continue;

        FILE* spk = fopen(spkCStr, "rb");
        if (!spk) continue;

        std::vector<double>  acc(static_cast<size_t>(nPts), 0.0);
        std::vector<int16_t> raw;
        std::vector<float>   sp;
        long valid = 0;

        for (long s = 0; s < nSpk; ++s) {
            if (haveToStopProcessing.load(std::memory_order_relaxed)) break;
            if (!tmReadSpikeFloat(spk, fidx[static_cast<size_t>(s)],
                                  nChan, nSamp, raw, sp))
                continue;
            for (int p = 0; p < nPts; ++p)
                acc[static_cast<size_t>(p)] += sp[static_cast<size_t>(p)];
            ++valid;
        }
        fclose(spk);

        if (valid > 0)
            for (int p = 0; p < nPts; ++p)
                meanWav[static_cast<size_t>(ci)][static_cast<size_t>(p)] =
                    static_cast<float>(acc[static_cast<size_t>(p)] / valid);
    }
    if (haveToStopProcessing) { post(); return; }

    // ── 3b. Restrict to the selected channels ──────────────────────────────
    // Applied AFTER the means are built: the .spk read is the expensive part and
    // is identical either way, so masking here costs a memcpy.  Channels are
    // compacted out rather than zeroed — see channelmask.h.  Depths are
    // compacted in step so the drift shift still interpolates on the right
    // geometry (on a sparse selection that interpolation is necessarily
    // coarser, which is inherent to asking for fewer channels).
    // nChan stays the FILE's channel count (the .spk read above depends on it);
    // effChan is what the maths below sees.
    int effChan = nChan;
    {
        std::vector<int> sel;
        sel.reserve(static_cast<size_t>(selection.size()));
        for (int c : selection) sel.push_back(c);
        const std::vector<int> keep = cmResolveMask(sel, nChan);
        if (!keep.empty()) {
            std::vector<float> tmp;
            for (auto& m : meanWav) {
                cmCompactChannels(m, nChan, nSamp, keep, tmp);
                m.swap(tmp);
            }
            std::vector<float> dTmp;
            cmCompactPerChannel(depths, keep, dTmp);
            depths.swap(dTmp);
            effChan     = static_cast<int>(keep.size());
            nChanCached = effChan;
            depthsValid = (static_cast<int>(depths.size()) == effChan);
        }
    }

    // ── 4. Initial drift-shifted xcorr matrix (view recomputes on slider) ───
    scores = new Array<double>();
    scores->setSize(nClusters, nClusters);

    // Both branches poll the stop flag per ROW.  Neither used to, so
    // stopProcessing() set a flag nothing read and the whole O(clusters^2) ran to
    // completion regardless -- which is what made the next edit's
    // stopAllViewThreads() block the GUI thread in wait() for minutes.
    const std::function<bool()> cancelled = [this]{
        return haveToStopProcessing.load(std::memory_order_relaxed);
    };

    if (depthsValid) {
        dmComputeDriftMatrix(meanWav, depths, effChan, nSamp, maxShift,
                             initialDeltaUm, *scores, cancelled);
    } else {
        // No usable geometry: plain unshifted mean xcorr so the view still
        // shows something (the slider will be disabled by the view).
        //
        // Parallelised to match the geometry branch above.  Row i writes only
        // row i+1 and its mirror column, and dmNormXcorr is a pure read of two
        // mean waveforms, so the rows are independent and the result is
        // order-independent.  This branch is the one a session without probe
        // geometry always takes, and it was the ONLY serial O(clusters^2) left
        // in the matrix code -- one core, minutes, at 8736 clusters.
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
        for (int i = 0; i < nClusters; ++i) {
            if (cancelled()) continue;            // OpenMP: skip, cannot break
            (*scores)(i + 1, i + 1) = 1.0;
            for (int j = i + 1; j < nClusters; ++j) {
                const float s = dmNormXcorr(meanWav[static_cast<size_t>(i)],
                                            meanWav[static_cast<size_t>(j)], maxShift);
                (*scores)(i + 1, j + 1) = s;
                (*scores)(j + 1, i + 1) = s;
            }
        }
    }

    // A cancelled run leaves the matrix partially filled.  Publishing it would
    // paint rows of zeros as though they were real correlations, so drop it and
    // let the view reject the result on scores == nullptr -- the same contract
    // the early-outs above already rely on.
    if (haveToStopProcessing) {
        delete scores;
        scores = nullptr;
    }

    post();
}
