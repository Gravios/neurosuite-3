/***************************************************************************
                          correlationthread.cpp  -  description
                             -------------------
    begin                : Fri Nov 14 2003
    copyright            : (C) 2003 by
    email                :
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
#include "correlationthread.h"
#include "klustersjobpool.h"

//QT include files
#include <QApplication>
#include <QThreadPool>

#include <QList>
#include <QMutexLocker>

CorrelationThread::CorrelationThread(CorrelationView& view,Data& d,const QList<Pair>& pairs,const QList<int>& clusterIds,
                                     const std::shared_ptr<KlustersJobToken>& viewToken)
    :correlationView(view),data(d),clusterPairs(pairs),token(viewToken),
    snapshot(d.currentSnapshot()),
    snapBinSize(view.binSize),snapTimeWindow(view.timeWindow){
    this->clusterIds = clusterIds;
    setAutoDelete(true);
    //The creation of the job launches the request, as the old thread's
    //constructor did with start().
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    KlustersJobPool::pool()->start(this, KlustersJobPool::InteractivePriority);   // correlograms for the shown clusters
}

void CorrelationThread::run(){
    process();
    //Retire: this must be the last touch of any shared state (Data above
    //all).  The synchronous quiesce in CorrelationView::stopRunningThreads()
    //and the document-close pool drain treat active == 0 as "no job is
    //inside a Data call anymore".
    token->active.fetch_sub(1, std::memory_order_acq_rel);
}

void CorrelationThread::process(){
    //One ticket per request: one share for this sweep, one more per parked
    //waiter (added by Data::subscribeCorrelogram).  The completion closure
    //is installed before any share can complete and carries everything the
    //event needs, because it may run from a waiter flush on another worker
    //long after this job object is deleted.  It posts under the view token's
    //postMutex/viewDead fence, as the old post() member did.
    auto ticket = std::make_shared<RequestTicket>();
    {
        auto tok = token;
        auto* viewPtr = &correlationView;
        const int gen = jobGeneration;
        ticket->post = [tok, viewPtr, gen](bool){
            QMutexLocker lock(&tok->postMutex);
            if(tok->viewDead) return;
            QApplication::postEvent(viewPtr, new CorrelationsEvent(gen));
        };
    }

    if(!cancelled()){
        //Convert the miliseconds in recording units.
        double binSizeInRU = static_cast<double>((static_cast<double>(snapBinSize) * 1000.0) / data.samplingInterval);
        double timeWindowInRU = static_cast<double>((static_cast<double>(snapTimeWindow) * 1000.0) / data.samplingInterval);

        //Calculate the number of bins to compute - so there are a total of nBins = 1+2*halfBins bins
        //(halfBins + 1/2 for each half time window)
        int halfBins = ((snapTimeWindow / snapBinSize) - 1) / 2;

        QList<Pair>::iterator pairIterator;
        for(pairIterator = clusterPairs.begin(); pairIterator != clusterPairs.end(); ++pairIterator){
            if(cancelled()) break;
            //Against the snapshot captured at enqueue: its membership and —
            //since the cache moved into the snapshot (epoch-snapshot
            //step 4) — its own correlogram store.  A pair another job owns
            //is subscribed on that computation instead of sleep(1)-polled;
            //NOT_AVAILABLE is a skip, as it always was.  Retry covers the
            //rare drop of the owner mid-subscription (the entry is gone, so
            //the next getCorrelograms call claims and computes it here) —
            //capped, in place of the old unbounded poll.
            bool settled = false;
            for(int attempt = 0; attempt < 4 && !settled && !cancelled(); ++attempt){
                const Data::Status status = data.getCorrelograms(snapshot,*pairIterator,snapBinSize,snapTimeWindow,binSizeInRU,timeWindowInRU,halfBins);
                if(status != Data::IN_PROCESS){ settled = true; break; }   // READY, or NOT_AVAILABLE (skip)
                const Data::CorrelationSubscribe sub =
                    data.subscribeCorrelogram(snapshot, *pairIterator, snapBinSize, snapTimeWindow, ticket);
                if(sub == Data::CorrelationSubscribe::Retry) continue;
                settled = true;                                            // Parked / DoneOk
            }
            if(!settled && !cancelled())
                qWarning("CorrelationThread: correlogram (%d,%d) did not settle after "
                         "4 attempts; giving up on it for this request.",
                         pairIterator->first, pairIterator->second);
        }
    }

    //Complete this sweep's own share.  If nothing was parked, this posts the
    //completion event right here; otherwise the last waiter to complete
    //posts it from the owner's terminal.
    ticket->completeOne();
}
