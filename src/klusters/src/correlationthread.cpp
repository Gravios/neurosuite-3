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
#include <QThread>
#include <QThreadPool>

#include <QList>
#include <QMutexLocker>

CorrelationThread::CorrelationThread(CorrelationView& view,Data& d,const QList<Pair>& pairs,const QList<int>& clusterIds,
                                     const std::shared_ptr<KlustersJobToken>& viewToken)
    :correlationView(view),data(d),clusterPairs(pairs),token(viewToken),
    snapBinSize(view.binSize),snapTimeWindow(view.timeWindow){
    this->clusterIds = clusterIds;
    setAutoDelete(true);
    //The creation of the job launches the request, as the old thread's
    //constructor did with start().
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    KlustersJobPool::pool()->start(this);
}

void CorrelationThread::post(QEvent* event){
    // Fence against view destruction: ~CorrelationView sets viewDead under
    // the same mutex, so while we hold it and viewDead is false the view is a
    // valid event receiver (events it already received are flushed by its
    // destructor's removePostedEvents).
    QMutexLocker lock(&token->postMutex);
    if(token->viewDead){
        delete event;
        return;
    }
    QApplication::postEvent(&correlationView,event);
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
    if(!cancelled()){
        //Convert the miliseconds in recording units.
        double binSizeInRU = static_cast<double>((static_cast<double>(snapBinSize) * 1000.0) / data.samplingInterval);
        double timeWindowInRU = static_cast<double>((static_cast<double>(snapTimeWindow) * 1000.0) / data.samplingInterval);

        //Calculate the number of bins to compute - so there are a total of nBins = 1+2*halfBins bins
        //(halfBins + 1/2 for each half time window)
        int halfBins = ((snapTimeWindow / snapBinSize) - 1) / 2;

        QList<Pair>::iterator pairIterator;
        for(pairIterator = clusterPairs.begin(); pairIterator != clusterPairs.end(); ++pairIterator){
            if(!cancelled()){
                Data::Status status = data.getCorrelograms(*pairIterator,snapBinSize,snapTimeWindow,binSizeInRU,timeWindowInRU,halfBins);
                if(status == Data::NOT_AVAILABLE)
                    continue;
                else if(status == Data::IN_PROCESS) {
                    while(!cancelled() && (data.getCorrelograms(*pairIterator,snapBinSize,snapTimeWindow,binSizeInRU,timeWindowInRU,halfBins) == Data::IN_PROCESS))
                    {
                        QThread::sleep(1);
                    }
                }
            }
        }
    }

    //Send an event to the CorrelationView to let it know that the data requested are available.
    post(new CorrelationsEvent(*this));
}
