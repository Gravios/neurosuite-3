/***************************************************************************
                          waveformthread.h  -  description
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

#ifndef WAVEFORMTHREAD_H
#define WAVEFORMTHREAD_H

#include <atomic>

//include files for the application
#include "waveformview.h"
#include "data.h"

//include files for QT
#include <QThread>


#include <QEvent>
#include <QList>

/**Thread used to retrieve the waveforms and compute the means and standard deviations
 * which will be displayed in the WaveformView.
 * No heavy computation is done is this class, the thread calls the Data object which
 * will do the work.
 *@author Lynn Hazan
 */

class WaveformThread : public QThread {

public: 
    //Only the method getWaveforms of WaveformView has access to the private part of WaveformThread,
    //the constructor of WaveformThread being private, only this method con create a new WaveformThread
    friend WaveformThread* WaveformView::getWaveforms();

    ~WaveformThread(){}

    void getWaveformInformation(int clusterId,WaveformView::PresentationMode mode);
    void getWaveformInformation(const QList<int> &clusterIds, WaveformView::PresentationMode mode);
    void getMean(const QList<int> &clusterIds, WaveformView::PresentationMode mode);
    /**Gets the mean and standard deviation for the cluster or the clusters set previously.
  * @param
  */
    void getMean(WaveformView::PresentationMode mode);

    bool isSingleTriggeringCluster() const {return treatSingleCluster;}
    int triggeringCluster() const {return clusterId;}
    QList<int> triggeringClusters() const {return clusterIds;}
    bool isMeanRequested() const {return  meanRequested;}
    /** Returns the meanPresentation value snapshotted when the thread started.
     *  Use this in customEvent instead of the live view field. */
    bool wasLaunchedWithMeanPresentation() const { return snapMeanPresentation; }
    /** Returns the PresentationMode snapshotted at thread launch. Use in getMean() calls
     *  from customEvent instead of the live presentationMode field. */
    WaveformView::PresentationMode getSnapshotMode() const { return snapPresentationMode; }

    /**Asks the thread to stop his work as soon as possible.*/
    void stopProcessing(){haveToStopProcessing.store(true, std::memory_order_release);}

    class GetWaveformsEvent;
    friend class GetWaveformsEvent;

    GetWaveformsEvent* getWaveformsEvent(){
        return new GetWaveformsEvent(*this);
    }

    /**
  * Internal class use to send information to the WaveformView to inform it that
  * the data requested have been collected.
  */
    class GetWaveformsEvent : public QEvent{
        //Only the method getWaveformsEvent of WaveformThread has access to the private part of GetWaveformsEvent,
        //the constructor of GetWaveformsEvent being private, only this method con create a new GetWaveformsEvent
        friend GetWaveformsEvent* WaveformThread::getWaveformsEvent();

    public:
        WaveformThread* parentThread(){return &waveformThread;}
        ~GetWaveformsEvent(){}

    private:
        GetWaveformsEvent(WaveformThread& thread):QEvent(QEvent::Type(QEvent::User + 200)),waveformThread(thread){}

        WaveformThread& waveformThread;
    };

    class NoWaveformDataEvent;
    friend class NoWaveformDataEvent;

    NoWaveformDataEvent* noWaveformDataEvent(){
        return new NoWaveformDataEvent(*this);
    }

    /**
  * Internal class use to send information to the WaveformView to inform it that
  * there is not data available for the requested cluster. A reason being that the cluster has been suppress
  * after the thread has been launched.
  */
    class NoWaveformDataEvent : public QEvent{
        //Only the method getWaveformsEvent of WaveformThread has access to the private part of GetWaveformsEvent,
        //the constructor of GetWaveformsEvent being private, only this method con create a new GetWaveformsEvent
        friend NoWaveformDataEvent* WaveformThread::noWaveformDataEvent();

    public:
        WaveformThread* parentThread(){return &waveformThread;}
        ~NoWaveformDataEvent(){}

    private:
        NoWaveformDataEvent(WaveformThread& thread):QEvent(QEvent::Type(QEvent::User + 250)),waveformThread(thread){}

        WaveformThread& waveformThread;
    };

protected:
    void run();

private:
    WaveformThread(WaveformView& view,Data& d):waveformView(view),meanRequested(false),data(d),haveToStopProcessing(false),
        snapPresentationMode(WaveformView::SAMPLE),snapNbSpkToDisplay(0),snapStartTime(0),snapEndTime(0),snapMeanPresentation(false){}

    // Snapshot view parameters captured at start()-time so run() never reads
    // waveformView fields directly (they can be modified by the main thread mid-flight).
    void snapshotViewParams(){
        snapPresentationMode  = waveformView.presentationMode;
        snapNbSpkToDisplay    = waveformView.nbSpkToDisplay;
        snapStartTime         = waveformView.startTime;
        snapEndTime           = waveformView.endTime;
        snapMeanPresentation  = waveformView.meanPresentation;
    }

    WaveformView& waveformView;
    int clusterId;
    QList<int> clusterIds;
    bool treatSingleCluster;
    bool meanRequested;
    Data& data;
    WaveformView::PresentationMode mode;
    /**True if the thread has to stop processing, false otherwise.*/
    std::atomic_bool haveToStopProcessing;

    // Snapshots of WaveformView fields captured before the thread starts.
    WaveformView::PresentationMode snapPresentationMode;
    long snapNbSpkToDisplay;
    long snapStartTime;
    long snapEndTime;
    bool snapMeanPresentation;
};

#endif
