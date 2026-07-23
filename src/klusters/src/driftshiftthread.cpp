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
#include <functional>

void DriftShiftThread::run()
{
    auto post = [this]() {
        QApplication::postEvent(&view, new DriftShiftEvent(*this));
    };

    const int n = static_cast<int>(meanWav.size());
    if (n < 1 || haveToStopProcessing) { post(); return; }

    scores = new Array<double>();
    scores->setSize(n, n);

    const std::function<bool()> cancelled = [this]{
        return haveToStopProcessing.load(std::memory_order_relaxed);
    };
    dmComputeDriftMatrix(meanWav, depths, nChan, nSamp, maxShift,
                         static_cast<float>(deltaUm), *scores, cancelled);

    // A cancelled run is partial -- dropping it is what lets the view use
    // scores == nullptr as its rejection test, exactly as DriftMatrixThread
    // does.  During a drag almost every run is cancelled by the next one, so
    // this is the common path, not the exceptional one.
    if (haveToStopProcessing) {
        delete scores;
        scores = nullptr;
    }
    post();
}
