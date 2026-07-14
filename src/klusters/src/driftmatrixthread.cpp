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
#include "driftmatrixkernel.h"
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

    // ── 1. Cluster list (skip artefact 0, keep noise 1) — as TemplateMatrix ──
    {
        const QList<dataType> allIds = data.clusterIds();
        for (dataType id : allIds)
            if (id >= 1 && data.nbOfSpikes(id) > 0)
                clusterList.append(static_cast<int>(id));
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

    // ── 4. Initial drift-shifted xcorr matrix (view recomputes on slider) ───
    scores = new Array<double>();
    scores->setSize(nClusters, nClusters);

    if (depthsValid) {
        dmComputeDriftMatrix(meanWav, depths, nChan, nSamp, maxShift,
                             initialDeltaUm, *scores);
    } else {
        // No usable geometry: plain unshifted mean xcorr so the view still
        // shows something (the slider will be disabled by the view).
        for (int i = 0; i < nClusters; ++i) {
            (*scores)(i + 1, i + 1) = 1.0;
            for (int j = i + 1; j < nClusters; ++j) {
                const float s = dmNormXcorr(meanWav[static_cast<size_t>(i)],
                                            meanWav[static_cast<size_t>(j)], maxShift);
                (*scores)(i + 1, j + 1) = s;
                (*scores)(j + 1, i + 1) = s;
            }
        }
    }

    post();
}
