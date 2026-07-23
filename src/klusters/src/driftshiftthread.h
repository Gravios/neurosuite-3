/***************************************************************************
 * driftshiftthread.h — background recompute of the drift matrix at a new
 * slider position.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef DRIFTSHIFTTHREAD_H
#define DRIFTSHIFTTHREAD_H

#include <QThread>
#include <QEvent>
#include <atomic>
#include <vector>

#include "array.h"

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
// ---------------------------------------------------------------------------
class DriftShiftThread : public QThread {
public:
    friend class DriftMatrixView;

    ~DriftShiftThread() override {}

    void stopProcessing() {
        haveToStopProcessing.store(true, std::memory_order_release);
    }
    int  getGeneration() const { return generation; }
    /// Non-null only on a run that finished; a cancelled run yields nullptr.
    Array<double>* getScores() const { return scores; }
    /// Drift offset this run was computed for, so the view can label it.
    double getDriftUm()   const { return deltaUm; }
    /// True when the result belongs in the selection slot rather than the all slot.
    bool   wasForSelection() const { return forSelection; }

    class DriftShiftEvent : public QEvent {
        friend class DriftShiftThread;
    public:
        DriftShiftThread* parentThread() { return &thread; }
        ~DriftShiftEvent() override {}
    private:
        explicit DriftShiftEvent(DriftShiftThread& t)
            : QEvent(QEvent::Type(QEvent::User + 606)), thread(t) {}
        DriftShiftThread& thread;
    };

protected:
    void run() override;

private:
    DriftShiftThread(DriftMatrixView& v, int gen,
                     std::vector<std::vector<float>> means,
                     std::vector<float> chanDepths,
                     int nChan, int nSamp, int maxShift, double deltaUm,
                     bool forSelection)
        : view(v), generation(gen),
          meanWav(std::move(means)), depths(std::move(chanDepths)),
          nChan(nChan), nSamp(nSamp), maxShift(maxShift), deltaUm(deltaUm),
          forSelection(forSelection),
          haveToStopProcessing(false), scores(nullptr) { start(); }

    DriftMatrixView&                view;
    int                             generation;
    std::vector<std::vector<float>> meanWav;
    std::vector<float>              depths;
    int                             nChan;
    int                             nSamp;
    int                             maxShift;
    double                          deltaUm;
    bool                            forSelection;

    std::atomic_bool                haveToStopProcessing;
    Array<double>*                  scores;   // owned until the view takes it
};

#endif // DRIFTSHIFTTHREAD_H
