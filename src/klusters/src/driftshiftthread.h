/***************************************************************************
 * driftshiftthread.h — background recompute of the drift matrix at a new
 * slider position.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef DRIFTSHIFTTHREAD_H
#define DRIFTSHIFTTHREAD_H

#include <QRunnable>
#include <QEvent>
#include <memory>
#include <vector>

#include "array.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the view)

class DriftMatrixView;

// ---------------------------------------------------------------------------
// Background thread: rebuilds the drift matrix from ALREADY-CACHED mean
// waveforms at a new drift offset.
//
// This is the slider's half of the work, and it is deliberately NOT
// DriftMatrixThread.  That one exists to read the .spk file and build the mean
// waveforms; the slider never needs any of that, because the means it shifts
// are already sitting in the view's cache.  All this does is step 4 --
// dmComputeDriftMatrix over cached means -- which is the whole cost of a slider
// step and all of it O(clusters^2).
//
// It used to run synchronously inside DriftMatrixView::recomputeAtCurrentDrift(),
// on the GUI thread, once per QSlider::valueChanged.  Dragging the slider
// therefore issued one full n^2 pass per pixel of travel, each blocking the UI
// until it finished.
//
// The means and depths are COPIED in rather than referenced: the view replaces
// its cache wholesale when a fresh DriftMatrixThread result lands, which can
// happen mid-drag.  At the cluster counts the slider is enabled for the copy is
// small (a few MB) and it removes the lifetime question entirely.
//
// The result is a NEW matrix, never a write into the one being painted.  The
// old code wrote in place into *scores, which is only safe because it held the
// GUI thread for the duration; off-thread it would tear under paintEvent.
//
// Posts DriftShiftEvent (User+606) when done.
//
// Formerly one QThread per slider step; now a QRunnable on the shared worker
// pool (KlustersJobPool — worker-pool conversion, step 4b).  Creating it
// launches the request; the pool owns and deletes it after run().  The view
// supersedes it by bumping its shift token's request generation — a drag
// issues one job per valueChanged and each new position supersedes the one
// before, so almost every run is superseded mid-flight.  The result travels
// inside the event (freed by the event when no handler takes it).
// ---------------------------------------------------------------------------
class DriftShiftThread : public QRunnable {
public:
    friend class DriftMatrixView;

    ~DriftShiftThread() override {}

    class DriftShiftEvent : public QEvent {
        friend class DriftShiftThread;
    public:
        /**Deletes the matrix when no handler took it.*/
        ~DriftShiftEvent() override { delete scoresResult; }

        int generation() const { return eventGeneration; }
        /// Non-null only on a run that finished (and was not already taken);
        /// a cancelled run yields nullptr.
        Array<double>* takeScores() { Array<double>* s = scoresResult; scoresResult = nullptr; return s; }
        /// Drift offset the run was computed for, so the view can label it.
        double getDriftUm() const { return driftUm; }
        /// True when the result belongs in the selection slot rather than the all slot.
        bool wasForSelection() const { return selectionSlot; }
    private:
        explicit DriftShiftEvent(DriftShiftThread& job)
            : QEvent(QEvent::Type(QEvent::User + 606)),
              eventGeneration(job.jobGeneration),
              scoresResult(job.scores),
              driftUm(job.deltaUm),
              selectionSlot(job.forSelection) { job.scores = nullptr; }

        int            eventGeneration;
        Array<double>* scoresResult;
        double         driftUm;
        bool           selectionSlot;
    };

    /**Executed by a pool worker; rebuilds the matrix at the new offset,
    * posts the completion event and retires the job.*/
    void run() override;

private:
    /**Creating the job launches the request on the shared pool.  Runs on the
    * GUI thread only.*/
    DriftShiftThread(DriftMatrixView& v,
                     const std::shared_ptr<KlustersJobToken>& viewToken,
                     std::vector<std::vector<float>> means,
                     std::vector<float> chanDepths,
                     int nChan, int nSamp, int maxShift, double deltaUm,
                     bool forSelection);

    /**True once the view has superseded this job's request generation (the
    * next slider position, or a teardown): the job stops at its next check.*/
    bool cancelled() const {
        return token->generation.load(std::memory_order_acquire) != jobGeneration;
    }

    /**Posts @p event to the view, unless the view is being destroyed
    * (fenced by the token's postMutex/viewDead).*/
    void post(QEvent* event);

    /**The old run() body; split out so run() can retire the job on every path.*/
    void process();

    DriftMatrixView&                view;
    /**Shared cancellation/completion state owned by the view (shift stream).*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int                             jobGeneration = 0;
    std::vector<std::vector<float>> meanWav;
    std::vector<float>              depths;
    int                             nChan;
    int                             nSamp;
    int                             maxShift;
    double                          deltaUm;
    bool                            forSelection;

    Array<double>*                  scores;   // owned by the job, then the event
};

#endif // DRIFTSHIFTTHREAD_H
