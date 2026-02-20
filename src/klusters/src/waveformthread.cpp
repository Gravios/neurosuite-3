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

//QT include files
#include <QApplication>

#include <QList>
#include <QDebug>

//Unix include file
#include <unistd.h>

void WaveformThread::getWaveformInformation(int clusterId,WaveformView::PresentationMode mode){
    this->clusterId = clusterId;
    treatSingleCluster = true;
    this->mode = mode;
    snapshotViewParams();
    start();
}

void WaveformThread::getWaveformInformation(const QList<int>& clusterIds,WaveformView::PresentationMode mode){
    this->clusterIds = clusterIds;
    treatSingleCluster = false;
    this->mode = mode;
    snapshotViewParams();
    start();
}


void WaveformThread::run(){
    int sleepingAmount = 1;
    //If the triggering action is not the calculation of the mean and standard variation,
    //get the data and store them in waveformView.waveformInfoMap.
    //wait until the data are available. The status can be READY or IN_PROCESS.
    //In the later case, an other thread in working on the same cluster.
    if(!meanRequested  && !haveToStopProcessing){
        if(snapPresentationMode == WaveformView::SAMPLE){
            if(treatSingleCluster){
                if(!haveToStopProcessing){
                    Data::Status status = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                    if(status == Data::NOT_AVAILABLE){
                        //Send an event to the waveformView to let it know that the data requested are not available.
                        NoWaveformDataEvent* event = noWaveformDataEvent();
                        QApplication::postEvent(&waveformView,event);
                        return;
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(haveToStopProcessing) break;
                            sleep(sleepingAmount);
                            status = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                NoWaveformDataEvent* event = noWaveformDataEvent();
                                QApplication::postEvent(&waveformView,event);
                                return;
                            }
                        }
                    }
                }
            }
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!haveToStopProcessing){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!haveToStopProcessing){
                            Data::Status status = data.getSampleWaveformPoints(*iterator,snapNbSpkToDisplay);
                            //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                            if(status == Data::NOT_AVAILABLE)
                                continue;
                            else if(status == Data::IN_PROCESS)
                                while(!haveToStopProcessing && (data.getSampleWaveformPoints(*iterator,snapNbSpkToDisplay) == Data::IN_PROCESS))
                                {
                                    sleep(sleepingAmount);
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
                if(!haveToStopProcessing){
                    Data::Status status = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                    if(status == Data::NOT_AVAILABLE){
                        //Send an event to the waveformView to let it know that the data requested are not available.
                        NoWaveformDataEvent* event = noWaveformDataEvent();
                        QApplication::postEvent(&waveformView,event);
                        return;
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(haveToStopProcessing) break;
                            sleep(sleepingAmount);
                            status = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                NoWaveformDataEvent* event = noWaveformDataEvent();
                                QApplication::postEvent(&waveformView,event);
                                return;
                            }
                        }
                    }
                }
            }
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!haveToStopProcessing){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!haveToStopProcessing){
                            Data::Status status = data.getTimeFrameWaveformPoints(*iterator,snapStartTime,snapEndTime);
                            //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                            if(status == Data::NOT_AVAILABLE) continue;
                            else if(status == Data::IN_PROCESS)
                                while(!haveToStopProcessing && (data.getTimeFrameWaveformPoints(*iterator,snapStartTime,snapEndTime) == Data::IN_PROCESS))
                                {
                                    sleep(sleepingAmount);
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
    if((meanRequested || snapMeanPresentation)  && !haveToStopProcessing){
        if(snapPresentationMode == WaveformView::SAMPLE){
            if(treatSingleCluster){
                if(!haveToStopProcessing){
                    Data::Status status = data.calculateSampleMean(clusterId,snapNbSpkToDisplay);
                    if(status == Data::NOT_AVAILABLE && !haveToStopProcessing){
                        Data::Status dataStatus = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                        if(dataStatus == Data::NOT_AVAILABLE){
                            //Send an event to the waveformView to let it know that the data requested are not available.
                            NoWaveformDataEvent* event = noWaveformDataEvent();
                            QApplication::postEvent(&waveformView,event);
                            return;
                        }
                        else if(dataStatus == Data::IN_PROCESS){
                            while(true){
                                if(haveToStopProcessing) break;
                                sleep(sleepingAmount);
                                dataStatus = data.getSampleWaveformPoints(clusterId,snapNbSpkToDisplay);
                                if(dataStatus == Data::READY) break;
                                else if(dataStatus == Data::NOT_AVAILABLE){
                                    //Send an event to the waveformView to let it know that the data requested are not available.
                                    NoWaveformDataEvent* event = noWaveformDataEvent();
                                    QApplication::postEvent(&waveformView,event);
                                    return;
                                }
                            }
                        }
                        //Now that the data are available, compute the mean and standard deviation
                        while(true){
                            if(haveToStopProcessing) break;
                            status = data.calculateSampleMean(clusterId,snapNbSpkToDisplay);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                NoWaveformDataEvent* event = noWaveformDataEvent();
                                QApplication::postEvent(&waveformView,event);
                                return;
                            }
                            sleep(sleepingAmount);
                        }
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(haveToStopProcessing) break;
                            sleep(sleepingAmount);
                            status = data.calculateSampleMean(clusterId,snapNbSpkToDisplay);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                NoWaveformDataEvent* event = noWaveformDataEvent();
                                QApplication::postEvent(&waveformView,event);
                                return;
                            }
                        }
                    }
                }
            } //one cluster
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!haveToStopProcessing){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!haveToStopProcessing){
                            Data::Status status = data.calculateSampleMean(*iterator,snapNbSpkToDisplay);
                            if(status == Data::NOT_AVAILABLE && !haveToStopProcessing){
                                Data::Status dataStatus = data.getSampleWaveformPoints(*iterator,snapNbSpkToDisplay);

                                //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                                if(dataStatus == Data::NOT_AVAILABLE) continue;
                                if(dataStatus == Data::IN_PROCESS || (dataStatus == Data::READY)){
                                    while(true){
                                        if(haveToStopProcessing) break;
                                        if(dataStatus == Data::READY){
                                            //Now that the data are available, compute the mean and standard deviation
                                            while(true){
                                                if(haveToStopProcessing)
                                                    break;
                                                status = data.calculateSampleMean(*iterator,snapNbSpkToDisplay);
                                                if(status == Data::READY || status == Data::NOT_AVAILABLE)
                                                    break;
                                                sleep(sleepingAmount);
                                            }
                                            break;
                                        }
                                        else{
                                            sleep(sleepingAmount);
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
                                    if(haveToStopProcessing)
                                        break;
                                    sleep(sleepingAmount);
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
                if(!haveToStopProcessing){
                    Data::Status status = data.calculateTimeFrameMean(clusterId,snapStartTime,snapEndTime);
                    if(status == Data::NOT_AVAILABLE && !haveToStopProcessing){
                        Data::Status dataStatus = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                        if(dataStatus == Data::NOT_AVAILABLE){
                            //Send an event to the waveformView to let it know that the data requested are not available.
                            NoWaveformDataEvent* event = noWaveformDataEvent();
                            QApplication::postEvent(&waveformView,event);
                            return;
                        }
                        else if(dataStatus == Data::IN_PROCESS){
                            while(true){
                                if(haveToStopProcessing)
                                    break;
                                sleep(sleepingAmount);
                                dataStatus = data.getTimeFrameWaveformPoints(clusterId,snapStartTime,snapEndTime);
                                if(dataStatus == Data::READY)  {
                                    break;
                                } else if(dataStatus == Data::NOT_AVAILABLE) {
                                    //Send an event to the waveformView to let it know that the data requested are not available.
                                    NoWaveformDataEvent* event = noWaveformDataEvent();
                                    QApplication::postEvent(&waveformView,event);
                                    return;
                                }
                            }
                        }
                        //Now that the data are available, compute the mean and standard deviation
                        while(true){
                            if(haveToStopProcessing) break;
                            status = data.calculateTimeFrameMean(clusterId,snapStartTime,snapEndTime);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                NoWaveformDataEvent* event = noWaveformDataEvent();
                                QApplication::postEvent(&waveformView,event);
                                return;
                            }
                            sleep(sleepingAmount);
                        }
                    }
                    else if(status == Data::IN_PROCESS){
                        while(true){
                            if(haveToStopProcessing) break;
                            sleep(sleepingAmount);
                            status = data.calculateTimeFrameMean(clusterId,snapStartTime,snapEndTime);
                            if(status == Data::READY) break;
                            else if(status == Data::NOT_AVAILABLE){
                                //Send an event to the waveformView to let it know that the data requested are not available.
                                NoWaveformDataEvent* event = noWaveformDataEvent();
                                QApplication::postEvent(&waveformView,event);
                                return;
                            }
                        }
                    }
                }//Stop processing
            }//one cluster
            //iterate on all the clusters contained in clusterIds before returning
            else{
                if(!haveToStopProcessing){
                    QList<int>::iterator iterator;
                    QList<int>::iterator end(clusterIds.end());
                    for(iterator = clusterIds.begin(); iterator != end; ++iterator){
                        if(!haveToStopProcessing){
                            Data::Status status = data.calculateTimeFrameMean(*iterator,snapStartTime,snapEndTime);
                            if(status == Data::NOT_AVAILABLE && !haveToStopProcessing){
                                Data::Status dataStatus = data.getTimeFrameWaveformPoints(*iterator,snapStartTime,snapEndTime);
                                //If the data for one cluster is not available, skip it (do not send an event to the waveformView)
                                if(dataStatus == Data::NOT_AVAILABLE)
                                    continue;
                                if(dataStatus == Data::IN_PROCESS  || (dataStatus == Data::READY)){
                                    while(true){
                                        if(haveToStopProcessing) break;
                                        if(dataStatus == Data::READY){
                                            //Now that the data are available, compute the mean and standard deviation
                                            while(true){
                                                if(haveToStopProcessing) break;
                                                status = data.calculateTimeFrameMean(*iterator,snapStartTime,snapEndTime);
                                                if(status == Data::READY || status == Data::NOT_AVAILABLE) break;
                                                sleep(sleepingAmount);
                                            }
                                            break;
                                        }
                                        else{
                                            sleep(sleepingAmount);
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
                                    if(haveToStopProcessing) break;
                                    sleep(sleepingAmount);
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
    GetWaveformsEvent* event = getWaveformsEvent();
    QApplication::postEvent(&waveformView,event);
}

void WaveformThread::getMean(WaveformView::PresentationMode mode){
    meanRequested = true;
    this->mode = mode;
    snapshotViewParams();
    start();
}

void WaveformThread::getMean(const QList<int>& clusterIds,WaveformView::PresentationMode mode){

    qDebug()<<"in  WaveformThread::getMean";

    meanRequested = true;
    this->clusterIds = clusterIds;
    treatSingleCluster = false;
    this->mode = mode;
    snapshotViewParams();
    start();
}

