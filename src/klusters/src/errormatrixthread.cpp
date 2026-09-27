/***************************************************************************
                          errormatrixthread.cpp  -  description
                             -------------------
    begin                : Mon Jan 12 2004
    copyright            : (C) 2004 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
//include files for the application
#include "errormatrixthread.h"

//QT include files
#include <QApplication>
#include <QThreadPool>
#include <QMutexLocker>
#include <QElapsedTimer>
#include <cmath>
#include <algorithm>
#include <cstdio>

ErrorMatrixThread::ErrorMatrixThread(ErrorMatrixView& view,Data& d,
                                     const std::shared_ptr<const Data::ClusteringSnapshot>& snap,
                                     const std::shared_ptr<KlustersJobToken>& viewToken,
                                     bool incremental, bool verify,
                                     const Array<double>* prevRaw, const QList<int>& prevRawIds,
                                     const QList<int>& prevRawSizes, int prevNbDimensions,
                                     const QSet<int>& changedIds, bool seedOnly,
                                     std::vector<int> activeDims,
                                     QList<int> activeClusters)
    : errorMatrixView(view),data(d),token(viewToken),
      snapshot(snap),
      probabilities(nullptr),
      incremental(incremental),verify(verify),
      prevRaw(prevRaw),prevRawIds(prevRawIds),prevRawSizes(prevRawSizes),
      prevNbDimensions(prevNbDimensions),changedIds(changedIds),
      newRaw(nullptr),newRawDims(-1),nbReused(0),usedIncremental(false),
      seedOnly(seedOnly),activeDims(std::move(activeDims)),
      activeClusters(std::move(activeClusters)){
    setAutoDelete(true);
    // Restrict the model to the selected channels' feature columns before
    // anything is computed; empty = every dimension, i.e. unchanged.
    assistant.setActiveDimensions(this->activeDims);
    // And to one parent's children when the child palette is driving; empty =
    // every cluster, i.e. unchanged.  Both restrictions must be set before the
    // job is enqueued, since run() computes immediately.
    assistant.setActiveClusters(this->activeClusters);
    //Route the request-generation check into the assistant's own stop
    //polling: it is the only way to interrupt a compute call in progress.
    assistant.setExternalStop([this]{ return cancelled(); });
    //The creation of the job launches the request, as the old thread's
    //constructor did with start().
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    //A display compute is batch work the user sees land; the seedOnly cache
    //warmer is background work nobody is watching.
    KlustersJobPool::pool()->start(this, seedOnly ? KlustersJobPool::BackgroundPriority
                                                  : KlustersJobPool::BatchPriority);
}

void ErrorMatrixThread::post(QEvent* event){
    // Fence against view destruction: ~ErrorMatrixView sets viewDead under
    // the same mutex, so while we hold it and viewDead is false the view is a
    // valid event receiver.  A refused event deletes itself — and with it the
    // arrays it owns.
    QMutexLocker lock(&token->postMutex);
    if(token->viewDead){
        delete event;
        return;
    }
    QApplication::postEvent(&errorMatrixView,event);
}

void ErrorMatrixThread::run(){
    process();
    //Retire: this must be the last touch of any shared state (Data above
    //all).  The synchronous quiesce in ErrorMatrixView::stopRunningThreads()
    //and the document-close pool drain treat active == 0 as "no job is
    //inside a Data call anymore".
    token->active.fetch_sub(1, std::memory_order_acq_rel);
}

void ErrorMatrixThread::process(){
    if(!cancelled()){
        Array<double>* result = nullptr;
        QElapsedTimer diagTimer; diagTimer.start();

        if(incremental){
            //Against the snapshot captured at enqueue: the assistant reads
            //its membership tables directly (epoch-snapshot step 5); the
            //features themselves are still the shared Data table, protected
            //by the edit-path quiesce.
            result = assistant.computeMeanProbabilitiesIncremental(
                data, snapshot, clusterList, computedClusterList, ignoreClusterIndex,
                prevRaw, prevRawIds, prevRawSizes, prevNbDimensions, changedIds,
                &newRaw, &newRawIds, &newRawSizes, &nbReused, verify);

            if(result != nullptr){
                usedIncremental = true;
                newRawDims = data.nbOfDimensionsTotal() - 1;

                // Optional self-verification: recompute the FULL matrix with a
                // fresh assistant and log the largest absolute cell discrepancy.
                // A logic error surfaces as an O(0.1-1) delta; a tiny residual
                // (~1e-9) is expected GPU/CPU summation drift when the full path
                // runs on the GPU.  Verification never changes the returned
                // result — it only reports.
                if(verify && !cancelled()){
                    GroupingAssistant fullAssistant;
                    QList<int> cl, ccl, ici;
                    Array<double>* full =
                        fullAssistant.computeMeanProbabilities(data, snapshot, cl, ccl, ici);
                    if(full != nullptr){
                        double maxAbs = 0.0;
                        const long rr = std::min(result->nbOfRows(),    full->nbOfRows());
                        const long cc = std::min(result->nbOfColumns(), full->nbOfColumns());
                        const bool sameShape = (result->nbOfRows()==full->nbOfRows()
                                                && result->nbOfColumns()==full->nbOfColumns());
                        for(long i=1;i<=rr;++i)
                            for(long j=1;j<=cc;++j){
                                double d = std::fabs((*result)(i,j) - (*full)(i,j));
                                if(d>maxAbs) maxAbs=d;
                            }
                        fprintf(stderr,
                            "[errormatrix-incremental] reused %d/%d columns; verify "
                            "max|delta|=%.3e%s\n",
                            nbReused, static_cast<int>(clusterList.size()), maxAbs,
                            sameShape ? "" : " (SHAPE MISMATCH vs full!)");
                        delete full;
                    }
                }
            } else {
                // Incremental hit a hard precondition miss: discard any partial
                // outputs and fall through to the full path below.
                clusterList.clear(); computedClusterList.clear(); ignoreClusterIndex.clear();
                delete newRaw; newRaw = nullptr; newRawIds.clear(); newRawSizes.clear();
                usedIncremental = false;
            }
        }

        if(result == nullptr && !cancelled()){
            // Full recompute — the default path and the incremental fallback.
            result = assistant.computeMeanProbabilities(
                data, snapshot, clusterList, computedClusterList, ignoreClusterIndex);
        }

        probabilities = result;

        // Per-update path report: which computation actually ran, and how it went.
        // Gated by NS3_ERRORMATRIX_DIAG so it costs nothing unless requested, and
        // — unlike the verify line — does NOT trigger the expensive full recompute,
        // so it is safe to leave on while curating.  It answers "full or
        // incremental?" on every update, with the reuse count and wall time that
        // make the difference obvious (incremental should be markedly faster and
        // report reused ≈ clusters).
        if(qEnvironmentVariableIntValue("NS3_ERRORMATRIX_DIAG") != 0){
            const int nCl = static_cast<int>(clusterList.size());
            const long long ms = static_cast<long long>(diagTimer.elapsed());
            if(!incremental)
                fprintf(stderr,
                    "[errormatrix] FULL (incremental disabled) — %d clusters, %lld ms\n",
                    nCl, ms);
            else if(usedIncremental)
                fprintf(stderr,
                    "[errormatrix] INCREMENTAL — reused %d/%d columns, %d changed, %lld ms\n",
                    nbReused, nCl, static_cast<int>(changedIds.size()), ms);
            else
                fprintf(stderr,
                    "[errormatrix] FULL (incremental requested, fell back — precondition "
                    "miss) — %d clusters, %d changed, %lld ms\n",
                    nCl, static_cast<int>(changedIds.size()), ms);
        }
    }

    //Send an event to the ErrorMatrixView to let it know that the computation is finish.
    post(new ErrorMatrixEvent(*this));
}

