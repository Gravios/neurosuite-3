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

#include <QRunnable>
#include <QEvent>
#include <QList>
#include <QObject>
#include <memory>
#include <vector>

#include "array.h"
#include "data.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the view)

/**
 * @brief Computes cluster mean waveforms + the initial drift-xcorr matrix.
 *
 * Formerly one QThread per recompute; now a QRunnable on the shared worker
 * pool (KlustersJobPool — worker-pool conversion, step 4b).  Creating it
 * launches the request; the pool owns and deletes it after run().  The view
 * supersedes it by bumping its compute token's request generation, and the
 * results travel inside the completion event (the matrix as an owning
 * pointer, the heavy vectors moved in).
 */
class DriftMatrixThread : public QRunnable {
public:
    ~DriftMatrixThread() override {}

    class DriftMatrixEvent : public QEvent {
        friend class DriftMatrixThread;
    public:
        /**Deletes the matrix when no handler took it.*/
        ~DriftMatrixEvent() override { delete scoresResult; }

        int generation() const { return eventGeneration; }
        /**Hands the matrix (and its ownership) to the caller; nullptr when
        * the compute was cancelled or degenerate, or it was already taken.*/
        Array<double>* takeScores() { Array<double>* s = scoresResult; scoresResult = nullptr; return s; }
        QList<int> getClusterList() const { return clusterListResult; }
        /**Mutable on purpose: the accepting handler moves these out.*/
        std::vector<std::vector<float>>& getMeanWav() { return meanWavResult; }
        std::vector<float>&              getDepths()  { return depthsResult; }
        int  getNbChannels() const { return nChanResult; }
        int  getNbSamples()  const { return nSampResult; }
        int  getMaxShift()   const { return maxShiftResult; }
        bool geometryOk()    const { return depthsValidResult; }
        /// The channel selection the run was launched for (empty = all channels).
        /// The view uses it to file the result in the right cache slot.
        QList<int> getSelection() const { return selectionResult; }
    private:
        explicit DriftMatrixEvent(DriftMatrixThread& job)
            : QEvent(QEvent::Type(QEvent::User + 604)),
              eventGeneration(job.jobGeneration),
              scoresResult(job.scores),
              clusterListResult(job.clusterList),
              meanWavResult(std::move(job.meanWav)),
              depthsResult(std::move(job.depths)),
              nChanResult(job.nChanCached),
              nSampResult(job.nSampCached),
              maxShiftResult(job.maxShiftCached),
              depthsValidResult(job.depthsValid),
              selectionResult(job.selection) { job.scores = nullptr; }

        int                             eventGeneration;
        Array<double>*                  scoresResult;
        QList<int>                      clusterListResult;
        std::vector<std::vector<float>> meanWavResult;
        std::vector<float>              depthsResult;
        int                             nChanResult;
        int                             nSampResult;
        int                             maxShiftResult;
        bool                            depthsValidResult;
        QList<int>                      selectionResult;
    };

    /**
     * @param view      QObject to post the completion event to (DriftMatrixView).
     * @param d         Session data (spikes, clusters, waveform dimensions).
     * @param chanDepths  Per-channel site depth (µm) in the group's channel
     *                    order; empty disables the shift (falls back to plain
     *                    mean xcorr).
     * @param deltaUm   Initial drift shift for the first matrix.
     * @param viewToken The view's compute-stream token (stale events are
     *                  dropped by its generation guard).
     */
    /**
     * @param sel  Channel selection (group-local indices, empty = all channels).
     *             The mean waveforms and depths are compacted to it, so every
     *             result this job exposes is already restricted; the view's
     *             drift-slider recompute needs no further masking.
     *
     * Creating the job launches the request on the shared pool.  Runs on the
     * GUI thread only.
     */
    DriftMatrixThread(QObject& view, Data& d, std::vector<float> chanDepths,
                      float deltaUm,
                      const std::shared_ptr<KlustersJobToken>& viewToken,
                      QList<int> sel = QList<int>(),
                      QList<int> clusterScope = QList<int>());

    /**Executed by a pool worker; builds the matrix, posts the completion
    * event and retires the job.*/
    void run() override;

private:
    /**True once the view has superseded this job's request generation: the
    * job stops at its next check, exactly where the per-thread stop flag
    * used to be read.*/
    bool cancelled() const {
        return token->generation.load(std::memory_order_acquire) != jobGeneration;
    }

    /**Posts @p event to the view, unless the view is being destroyed
    * (fenced by the token's postMutex/viewDead).*/
    void post(QEvent* event);

    /**The old run() body; split out so run() can retire the job on every path.*/
    void process();

    QObject&                        target;
    Data&                           data;
    std::vector<float>              depths;          // per group channel (µm, y)
    float                           initialDeltaUm;
    /**Shared cancellation/completion state owned by the view (compute stream).*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int                             jobGeneration = 0;

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
