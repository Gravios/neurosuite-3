/***************************************************************************
 * driftshiftthread.cpp — see driftshiftthread.h.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "driftshiftthread.h"
#include "driftmatrixview.h"
#include "driftmatrixkernel.h"

#include <QApplication>
#include <QThreadPool>
#include <QMutexLocker>
#include <functional>

DriftShiftThread::DriftShiftThread(DriftMatrixView& v,
                                   const std::shared_ptr<KlustersJobToken>& viewToken,
                                   std::vector<std::vector<float>> means,
                                   std::vector<float> chanDepths,
                                   int nChan, int nSamp, int maxShift, double deltaUm,
                                   bool forSelection)
    : view(v), token(viewToken),
      meanWav(std::move(means)), depths(std::move(chanDepths)),
      nChan(nChan), nSamp(nSamp), maxShift(maxShift), deltaUm(deltaUm),
      forSelection(forSelection), scores(nullptr)
{
    setAutoDelete(true);
    //The creation of the job launches the request, as the old thread's
    //constructor did with start().
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    KlustersJobPool::start(this, KlustersJobPool::InteractivePriority);   // a slider step mid-drag
}

void DriftShiftThread::post(QEvent* event)
{
    // Fence against view destruction: ~DriftMatrixView sets viewDead under
    // the same mutex, so while we hold it and viewDead is false the view is a
    // valid event receiver.  A refused event deletes itself — and with it the
    // matrix it owns.
    QMutexLocker lock(&token->postMutex);
    if (token->viewDead) {
        delete event;
        return;
    }
    QApplication::postEvent(&view, event);
}

void DriftShiftThread::run()
{
    process();
    //Retire: the last touch of any shared state.  The synchronous quiesce in
    //DriftMatrixView::stopShiftThreads()/stopRunningThreadsSync() and the
    //document-close pool drain treat active == 0 as "no job is running".
    token->active.fetch_sub(1, std::memory_order_acq_rel);
}

void DriftShiftThread::process()
{
    const int n = static_cast<int>(meanWav.size());
    if (n < 1 || cancelled()) { post(new DriftShiftEvent(*this)); return; }

    scores = new Array<double>();
    scores->setSize(n, n);

    const std::function<bool()> stopPoll = [this]{
        return cancelled();
    };
    dmComputeDriftMatrix(meanWav, depths, nChan, nSamp, maxShift,
                         static_cast<float>(deltaUm), *scores, stopPoll);

    // A cancelled run is partial -- dropping it is what lets the view use
    // a null matrix as its rejection test, exactly as DriftMatrixThread
    // does.  During a drag almost every run is superseded by the next one, so
    // this is the common path, not the exceptional one.
    if (cancelled()) {
        delete scores;
        scores = nullptr;
    }
    post(new DriftShiftEvent(*this));
}
