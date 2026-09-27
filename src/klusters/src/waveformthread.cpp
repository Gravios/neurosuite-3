/***************************************************************************
                          waveformthread.cpp  -  description
                             -------------------
    begin                : Fri Oct 24 2003
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
#include "waveformthread.h"
#include "waveformview.h"
#include "klustersjobpool.h"

//QT include files
#include <QApplication>
#include <QThread>
#include <QThreadPool>

#include <QList>
#include <QMutexLocker>

void WaveformThread::getWaveformInformation(int clusterId,WaveformView::PresentationMode mode){
    Q_UNUSED(mode); //run() works on the snapshot taken below.
    this->clusterId = clusterId;
    treatSingleCluster = true;
    snapshotViewParams();
    enqueue();
}

void WaveformThread::getWaveformInformation(const QList<int>& clusterIds,WaveformView::PresentationMode mode){
    Q_UNUSED(mode);
    this->clusterIds = clusterIds;
    treatSingleCluster = false;
    snapshotViewParams();
    enqueue();
}

void WaveformThread::getMean(int clusterId,WaveformView::PresentationMode mode){
    Q_UNUSED(mode);
    this->clusterId = clusterId;
    treatSingleCluster = true;
    meanRequested = true;
    snapshotViewParams();
    enqueue();
}

void WaveformThread::getMean(const QList<int>& clusterIds,WaveformView::PresentationMode mode){
    Q_UNUSED(mode);
    this->clusterIds = clusterIds;
    treatSingleCluster = false;
    meanRequested = true;
    snapshotViewParams();
    enqueue();
}

void WaveformThread::enqueue(){
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    KlustersJobPool::pool()->start(this, KlustersJobPool::InteractivePriority);   // the user is watching the waveform pane fill
}

void WaveformThread::run(){
    process();
    //Retire: this must be the last touch of any shared state (Data above
    //all).  The synchronous quiesce in WaveformView::stopAndClearThreads()
    //and the document-close pool drain treat active == 0 as "no job is
    //inside a Data call anymore".
    token->active.fetch_sub(1, std::memory_order_acq_rel);
}

void WaveformThread::process(){
    //One ticket per request: one share for this sweep, one more per parked
    //waiter (added by Data::subscribeWaveform).  The completion closure is
    //installed before any share can complete and carries everything the
    //event needs, because it may run from a waiter flush on another worker
    //long after this job object is deleted.
    auto ticket = std::make_shared<WaveformRequestTicket>();
    {
        auto tok  = token;
        auto* viewPtr = &waveformView;
        const int  gen    = jobGeneration;
        const bool single = treatSingleCluster;
        const int  cid    = clusterId;
        const QList<int> cids = clusterIds;
        const bool meanReq = meanRequested;
        const bool lwm     = snapMeanPresentation;
        const WaveformView::PresentationMode m = snapPresentationMode;
        ticket->post = [tok, viewPtr, gen, single, cid, cids, meanReq, lwm, m](bool failed){
            QMutexLocker lock(&tok->postMutex);
            if(tok->viewDead) return;
            if(failed)
                QApplication::postEvent(viewPtr, new NoWaveformDataEvent(gen));
            else
                QApplication::postEvent(viewPtr, new GetWaveformsEvent(gen, single, cid, cids,
                                                                       meanReq, lwm, m));
        };
    }

    const Data::WaveformMode mode =
        (snapPresentationMode == WaveformView::SAMPLE) ? Data::SAMPLE : Data::TIME_FRAME;
    const dataType p1 = (mode == Data::SAMPLE) ? snapNbSpkToDisplay : snapStartTime;
    const dataType p2 = (mode == Data::SAMPLE) ? 0                  : snapEndTime;

    auto fetchOnce = [&](int id) -> Data::Status {
        return (mode == Data::SAMPLE)
            ? data.getSampleWaveformPoints(id, snapNbSpkToDisplay)
            : data.getTimeFrameWaveformPoints(id, snapStartTime, snapEndTime);
    };
    auto calcOnce = [&](int id) -> Data::Status {
        return (mode == Data::SAMPLE)
            ? data.calculateSampleMean(id, snapNbSpkToDisplay)
            : data.calculateTimeFrameMean(id, snapStartTime, snapEndTime);
    };

    const QList<int> sweep = treatSingleCluster ? (QList<int>() << clusterId) : clusterIds;
    //Single-cluster requests treat an unavailable cluster as fatal (the view
    //gets the no-data event); multi-cluster requests skip it.
    const bool failFatal = treatSingleCluster;
    bool singleFailed = false;

    //── Phase 1: make the spikes of every requested cluster available ──────
    //Mirrors the old fetch loops, with the IN_PROCESS sleep(1) polls
    //replaced by subscription: the owner of the overlapping computation
    //completes our share from its terminal.
    if(!meanRequested && !cancelled()){
        for(int id : sweep){
            if(cancelled()) break;
            const Data::Status st = fetchOnce(id);
            if(st == Data::READY) continue;
            if(st == Data::NOT_AVAILABLE){
                if(failFatal){ singleFailed = true; break; }
                continue;                       // multi: skip this cluster
            }
            //IN_PROCESS: subscribe on the owner's spike terminal.
            const Data::WaveformSubscribe sub =
                data.subscribeWaveform(id, mode, /*wantsMean*/false, p1, p2, ticket, failFatal);
            if(sub == Data::WaveformSubscribe::DoneFail && failFatal){ singleFailed = true; break; }
            //Parked / DoneOk / DoneFail-on-multi: nothing more to do here.
        }
    }

    //── Phase 2: means and standard deviations, if this request wants them ──
    //Mirrors the old calculate/fetch dance without its poll loops: a mean
    //someone else owns is subscribed on the mean terminal, spikes in flight
    //are subscribed with wantsMean (the spike owner then computes the mean
    //with OUR parameters before completing us), and the missing-data case
    //fetches once and retries the calculation, with a small cap in place of
    //the old unbounded sleep loops.
    if((meanRequested || snapMeanPresentation) && !cancelled() && !singleFailed){
        for(int id : sweep){
            if(cancelled()) break;
            bool resolved = false;
            for(int attempt = 0; attempt < 4 && !resolved && !cancelled(); ++attempt){
                const Data::Status st = calcOnce(id);
                if(st == Data::READY){ resolved = true; break; }
                if(st == Data::IN_PROCESS){
                    const Data::WaveformSubscribe sub =
                        data.subscribeWaveform(id, mode, /*wantsMean*/true, p1, p2, ticket, failFatal);
                    if(sub == Data::WaveformSubscribe::Retry) continue;   // mean landed meanwhile? recalc
                    if(sub == Data::WaveformSubscribe::DoneFail && failFatal) singleFailed = true;
                    resolved = true;                                      // Parked / DoneOk / DoneFail
                    break;
                }
                //NOT_AVAILABLE: the spikes are missing or mismatched — make
                //them available, then loop to recalculate.
                const Data::Status fst = fetchOnce(id);
                if(fst == Data::READY) continue;
                if(fst == Data::IN_PROCESS){
                    const Data::WaveformSubscribe sub =
                        data.subscribeWaveform(id, mode, /*wantsMean*/true, p1, p2, ticket, failFatal);
                    if(sub == Data::WaveformSubscribe::Retry) continue;
                    if(sub == Data::WaveformSubscribe::DoneFail && failFatal) singleFailed = true;
                    resolved = true;
                    break;
                }
                //fetch NOT_AVAILABLE: the cluster is gone (or wedged).
                if(failFatal) singleFailed = true;
                resolved = true;
                break;
            }
            if(!resolved && !cancelled())
                qWarning("WaveformThread: mean of cluster %d did not settle after "
                         "4 attempts; giving up on it for this request.", id);
            if(singleFailed) break;
        }
    }

    if(singleFailed)
        ticket->failed.store(true, std::memory_order_release);
    //Complete this sweep's own share.  If nothing was parked, this posts the
    //completion event right here; otherwise the last waiter to complete
    //posts it from the owner's terminal.
    ticket->completeOne();
}
