/***************************************************************************
 * driftmatrixthread.h
 *
 * Background worker for the drift matrix.  Reads every cluster's spikes and
 * computes their mean waveforms (the expensive pass), then builds the initial
 * drift-shifted cross-correlation matrix at the requested µm shift using the
 * pure kernels in driftmatrixkernel.h.  The mean waveforms and channel depths
 * are exposed so the view can recompute the matrix cheaply as the drift slider
 * moves, without re-reading the .spk file.
 *
 * Posts DriftMatrixEvent (User+604) to the target QObject when finished.  The
 * thread is deliberately decoupled from DriftMatrixView (it only needs a
 * QObject to post to), so the compute backend can be built and reviewed ahead
 * of the view.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef DRIFTMATRIXTHREAD_H
#define DRIFTMATRIXTHREAD_H

#include <QThread>
#include <QEvent>
#include <QList>
#include <QObject>
#include <atomic>
#include <vector>

#include "array.h"
#include "data.h"

/**
 * @brief Computes cluster mean waveforms + the initial drift-xcorr matrix.
 */
class DriftMatrixThread : public QThread {
public:
    ~DriftMatrixThread() override {}

    void stopProcessing() {
        haveToStopProcessing.store(true, std::memory_order_release);
    }
    int getGeneration() const { return generation; }

    // Results, valid after the thread has posted its completion event.
    Array<double>*                         getScores()      const { return scores; }
    QList<int>                             getClusterList() const { return clusterList; }
    const std::vector<std::vector<float>>& getMeanWav()     const { return meanWav; }
    const std::vector<float>&              getDepths()      const { return depths; }
    int                                    getNbChannels()  const { return nChanCached; }
    int                                    getNbSamples()   const { return nSampCached; }
    int                                    getMaxShift()    const { return maxShiftCached; }
    bool                                   geometryOk()     const { return depthsValid; }
    /// The channel selection this run was launched for (empty = all channels).
    /// The view uses it to file the result in the right cache slot.
    QList<int>                             getSelection()   const { return selection; }

    class DriftMatrixEvent : public QEvent {
        friend class DriftMatrixThread;
    public:
        DriftMatrixThread* parentThread() { return &thread; }
        ~DriftMatrixEvent() override {}
    private:
        explicit DriftMatrixEvent(DriftMatrixThread& t)
            : QEvent(QEvent::Type(QEvent::User + 604)), thread(t) {}
        DriftMatrixThread& thread;
    };

    /**
     * @param view      QObject to post the completion event to (DriftMatrixView).
     * @param d         Session data (spikes, clusters, waveform dimensions).
     * @param chanDepths  Per-channel site depth (µm) in the group's channel
     *                    order; empty disables the shift (falls back to plain
     *                    mean xcorr).
     * @param deltaUm   Initial drift shift for the first matrix.
     * @param gen       Generation counter (stale events are ignored by the view).
     */
    /**
     * @param sel  Channel selection (group-local indices, empty = all channels).
     *             The mean waveforms and depths are compacted to it, so every
     *             result this thread exposes is already restricted; the view's
     *             drift-slider recompute needs no further masking.
     */
    DriftMatrixThread(QObject& view, Data& d, std::vector<float> chanDepths,
                      float deltaUm, int gen, QList<int> sel = QList<int>(),
                      QList<int> clusterScope = QList<int>())
        : target(view), data(d), depths(std::move(chanDepths)),
          initialDeltaUm(deltaUm), generation(gen),
          haveToStopProcessing(false), scores(nullptr),
          selection(std::move(sel)),
          activeClusters(std::move(clusterScope)) { start(); }

protected:
    void run() override;

private:
    QObject&                        target;
    Data&                           data;
    std::vector<float>              depths;          // per group channel (µm, y)
    float                           initialDeltaUm;
    int                             generation;
    std::atomic_bool                haveToStopProcessing;

    Array<double>*                  scores;          // nClusters x nClusters
    QList<int>                      clusterList;
    std::vector<std::vector<float>> meanWav;         // [clusterIdx] -> channel-major mean
    int                             nChanCached   = 0;
    int                             nSampCached   = 0;
    int                             maxShiftCached = 1;
    bool                            depthsValid   = false;
    QList<int>                      selection;       // empty = all channels
    QList<int>                      activeClusters;  // empty = all clusters.  After selection so
                                     // member init order matches the ctor list.
};

#endif // DRIFTMATRIXTHREAD_H
