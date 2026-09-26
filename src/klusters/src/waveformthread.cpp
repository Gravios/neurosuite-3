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
    KlustersJobPool::pool()->start(this);
}

void WaveformThread::post(QEvent* event){
    // Fence against view destruction: ~WaveformView sets viewDead under the
    // same mutex, so while we hold it and viewDead is false the view is a
    // valid event receiver (events it already received are flushed by its
    // destructor's removePostedEvents).
    QMutexLocker lock(&token->postMutex);
    if(token->viewDead){
        delete event;
        return;
    }
    QApplication::postEvent(&waveformView,event);
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
    unsigned long sleepingAmount = 1;
    //If the triggering action is not the calculation of the mean and standard variation,
    //get the data and store them in waveformView.waveformInfoMap.
    //wait until the data are available. The status can be READY or IN_PROCESS.
    //In the later case, an other thread in working on the same cluster.
    if(!meanRequested  && !cancelled()){
        if(snapPresentationMode == WaveformView::SAMPLE){
            if(treatSingleCluster){
                if(!cancelled()){
                    Data::Status status = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                    if(status == Data::NOT_AVAILABLE){
                        //Send an event to the waveformView to let it know that the data requested are not available.
                        post(new NoWaveformDataEvent(*this));
                        return;
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(cancelled()) break;
                            QThread::sleep(sleepingAmount);
                            status = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                post(new NoWaveformDataEvent(*this));
                                return;
                            }
                        }
                    }
                }
            }
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!cancelled()){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!cancelled()){
                            Data::Status status = data.getSampleWaveformPoints(*iterator,snapNbSpkToDisplay);
                            //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                            if(status == Data::NOT_AVAILABLE)
                                continue;
                            else if(status == Data::IN_PROCESS)
                                while(!cancelled() && (data.getSampleWaveformPoints(*iterator,snapNbSpkToDisplay) == Data::IN_PROCESS))
                                {
                                    QThread::sleep(sleepingAmount);
                                }
                        } else {
                            break;
                        }
                    }
                }
            }
        }
        else if(snapPresentationMode == WaveformView::TIME_FRAME){
            if(treatSingleCluster){
                if(!cancelled()){
                    Data::Status status = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                    if(status == Data::NOT_AVAILABLE){
                        //Send an event to the waveformView to let it know that the data requested are not available.
                        post(new NoWaveformDataEvent(*this));
                        return;
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(cancelled()) break;
                            QThread::sleep(sleepingAmount);
                            status = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                post(new NoWaveformDataEvent(*this));
                                return;
                            }
                        }
                    }
                }
            }
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!cancelled()){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!cancelled()){
                            Data::Status status = data.getTimeFrameWaveformPoints(*iterator,snapStartTime,snapEndTime);
                            //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                            if(status == Data::NOT_AVAILABLE) continue;
                            else if(status == Data::IN_PROCESS)
                                while(!cancelled() && (data.getTimeFrameWaveformPoints(*iterator,snapStartTime,snapEndTime) == Data::IN_PROCESS))
                                {
                                    QThread::sleep(sleepingAmount);
                                }
                        } else {
                            break;
                        }
                    }
                }
            }
        }
    }
    //Calculate the means and standard deviation if needed
    //wait until the data are available. The status can be READY, IN_PROCESS or NOT_AVAILABLE.
    //In the IN_PROCESS case, an other thread in working on the same cluster,
    //In the NOT_AVAILABLE case, the spikes have not been collected, get the data and
    //ask to calculate the data again.
    if((meanRequested || snapMeanPresentation)  && !cancelled()){
        if(snapPresentationMode == WaveformView::SAMPLE){
            if(treatSingleCluster){
                if(!cancelled()){
                    Data::Status status = data.calculateSampleMean(clusterId,snapNbSpkToDisplay);
                    if(status == Data::NOT_AVAILABLE && !cancelled()){
                        Data::Status dataStatus = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                        if(dataStatus == Data::NOT_AVAILABLE){
                            //Send an event to the waveformView to let it know that the data requested are not available.
                            post(new NoWaveformDataEvent(*this));
                            return;
                        }
                        else if(dataStatus == Data::IN_PROCESS){
                            while(true){
                                if(cancelled()) break;
                                QThread::sleep(sleepingAmount);
                                dataStatus = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                                if(dataStatus == Data::READY) break;
                                else if(dataStatus == Data::NOT_AVAILABLE){
                                    //Send an event to the waveformView to let it know that the data requested are not available.
                                    post(new NoWaveformDataEvent(*this));
                                    return;
                                }
                            }
                        }
                        //Now that the data are available, compute the mean and standard deviation
                        while(true){
                            if(cancelled()) break;
                            status = data.calculateSampleMean(clusterId,snapNbSpkToDisplay);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                post(new NoWaveformDataEvent(*this));
                                return;
                            }
                            QThread::sleep(sleepingAmount);
                        }
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(cancelled()) break;
                            QThread::sleep(sleepingAmount);
                            status = data.calculateSampleMean(clusterId,snapNbSpkToDisplay);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                post(new NoWaveformDataEvent(*this));
                                return;
                            }
                        }
                    }
                }
            } //one cluster
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!cancelled()){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!cancelled()){
                            Data::Status status = data.calculateSampleMean(*iterator,snapNbSpkToDisplay);
                            if(status == Data::NOT_AVAILABLE && !cancelled()){
                                Data::Status dataStatus = data.getSampleWaveformPoints(*iterator,snapNbSpkToDisplay);

                                //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                                if(dataStatus == Data::NOT_AVAILABLE) continue;
                                if(dataStatus == Data::IN_PROCESS || (dataStatus == Data::READY)){
                                    while(true){
                                        if(cancelled()) break;
                                        if(dataStatus == Data::READY){
                                            //Now that the data are available, compute the mean and standard deviation
                                            while(true){
                                                if(cancelled())
                                                    break;
                                                status = data.calculateSampleMean(*iterator,snapNbSpkToDisplay);
                                                if(status == Data::READY || status == Data::NOT_AVAILABLE)
                                                    break;
                                                QThread::sleep(sleepingAmount);
                                            }
                                            break;
                                        }
                                        else{
                                            QThread::sleep(sleepingAmount);
                                            dataStatus = data.getSampleWaveformPoints(*iterator,snapNbSpkToDisplay);
                                            if(dataStatus == Data::NOT_AVAILABLE)
                                                break;
                                        }
                                    }
                                }
                            }
                            else if(status == Data::IN_PROCESS){
                                //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                                while(true){
                                    if(cancelled())
                                        break;
                                    QThread::sleep(sleepingAmount);
                                    status = data.calculateSampleMean(*iterator,snapNbSpkToDisplay);
                                    if(status == Data::READY || status == Data::NOT_AVAILABLE)
                                        break;
                                }
                            }
                        }//Stop processing
                        else break;
                    }//iteration on the clusters
                }//Stop processing
            }//several clusters
        }//Sample
        else if(snapPresentationMode == WaveformView::TIME_FRAME){
            if(treatSingleCluster){
                if(!cancelled()){
                    Data::Status status = data.calculateTimeFrameMean(clusterId,snapStartTime,snapEndTime);
                    if(status == Data::NOT_AVAILABLE && !cancelled()){
                        Data::Status dataStatus = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                        if(dataStatus == Data::NOT_AVAILABLE){
                            //Send an event to the waveformView to let it know that the data requested are not available.
                            post(new NoWaveformDataEvent(*this));
                            return;
                        }
                        else if(dataStatus == Data::IN_PROCESS){
                            while(true){
                                if(cancelled())
                                    break;
                                QThread::sleep(sleepingAmount);
                                dataStatus = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                                if(dataStatus == Data::READY)  {
                                    break;
                                } else if(dataStatus == Data::NOT_AVAILABLE) {
                                    //Send an event to the waveformView to let it know that the data requested are not available.
                                    post(new NoWaveformDataEvent(*this));
                                    return;
                                }
                            }
                        }
                        //Now that the data are available, compute the mean and standard deviation
                        while(true){
                            if(cancelled()) break;
                            status = data.calculateTimeFrameMean(clusterId,snapStartTime,snapEndTime);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                post(new NoWaveformDataEvent(*this));
                                return;
                            }
                            QThread::sleep(sleepingAmount);
                        }
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(cancelled()) break;
                            QThread::sleep(sleepingAmount);
                            status = data.calculateTimeFrameMean(clusterId,snapStartTime,snapEndTime);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                post(new NoWaveformDataEvent(*this));
                                return;
                            }
                        }
                    }
                }//Stop processing
            }//one cluster
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!cancelled()){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!cancelled()){
                            Data::Status status = data.calculateTimeFrameMean(*iterator,snapStartTime,snapEndTime);
                            if(status == Data::NOT_AVAILABLE && !cancelled()){
                                Data::Status dataStatus = data.getTimeFrameWaveformPoints(*iterator,snapStartTime,snapEndTime);
                                //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                                if(dataStatus == Data::NOT_AVAILABLE)
                                    continue;
                                if(dataStatus == Data::IN_PROCESS  || (dataStatus == Data::READY)){
                                    while(true){
                                        if(cancelled()) break;
                                        if(dataStatus == Data::READY){
                                            //Now that the data are available, compute the mean and standard deviation
                                            while(true){
                                                if(cancelled()) break;
                                                status = data.calculateTimeFrameMean(*iterator,snapStartTime,snapEndTime);
                                                if(status == Data::READY || status == Data::NOT_AVAILABLE) break;
                                                QThread::sleep(sleepingAmount);
                                            }
                                            break;
                                        }
                                        else{
                                            QThread::sleep(sleepingAmount);
                                            dataStatus = data.getTimeFrameWaveformPoints(*iterator,snapStartTime,snapEndTime);
                                            if(dataStatus == Data::NOT_AVAILABLE)
                                                break;
                                        }
                                    }
                                }
                            }
                            else if(status == Data::IN_PROCESS){
                                //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                                while(true){
                                    if(cancelled()) break;
                                    QThread::sleep(sleepingAmount);
                                    status = data.calculateTimeFrameMean(*iterator,snapStartTime,snapEndTime);
                                    if(status == Data::READY || status == Data::NOT_AVAILABLE) break;
                                }
                            }
                        }//Stop processing
                        else break;
                    }//iteration on the clusters
                }//Stop processing
            }//several clusters
        }//Time frame
    }//mean

    //Send an event to the waveformView to let it know that the waveform information have been retrieved.
    post(new GetWaveformsEvent(*this));
}
