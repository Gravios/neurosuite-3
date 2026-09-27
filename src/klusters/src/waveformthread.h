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

#include <memory>

//include files for the application
#include "waveformview.h"
#include "data.h"

//include files for QT
#include <QRunnable>

#include <QEvent>
#include <QList>

/**Job used to retrieve the waveforms and compute the means and standard deviations
 * which will be displayed in the WaveformView.
 * No heavy computation is done in this class, the job calls the Data object which
 * will do the work.
 *
 * Formerly one QThread per request; now a QRunnable executed by the shared
 * worker pool (KlustersJobPool — worker-pool conversion, step 2).  Each live
 * QThread cost two file descriptors (its event-dispatcher wakeup pipe), so
 * the number of loadable clusters was bounded by the descriptor budget; pool
 * workers are reused and run no event loop.  The class name and the launch
 * protocol (view.getWaveforms(), then one of the get* methods) are kept so
 * Data's friendship and the call sites stay unchanged.
 *
 * The request lifecycle runs through the view's KlustersJobToken:
 * - enqueueing captures the current request generation and increments the
 *   active count; the pool deletes the job after run() (autoDelete);
 * - the view supersedes in-flight jobs by bumping the generation: each job
 *   polls it where it used to poll its per-thread stop flag, and the
 *   completion events carry the captured generation so the view's
 *   customEvent() can drop stale results;
 * - completion events carry copies of the request parameters instead of a
 *   pointer to the job, which retires itself (decrementing the active count
 *   as its last act — the synchronous quiesce in stopAndClearThreads() and
 *   the document-close pool drain rely on that ordering).
 *@author Lynn Hazan
 */

class WaveformThread : public QRunnable {

public:
    //Only the method getWaveforms of WaveformView has access to the private part of WaveformThread,
    //the constructor of WaveformThread being private, only this method can create a new WaveformThread
    friend WaveformThread* WaveformView::getWaveforms();

    ~WaveformThread() override {}

    void getWaveformInformation(int clusterId,WaveformView::PresentationMode mode);
    void getWaveformInformation(const QList<int> &clusterIds, WaveformView::PresentationMode mode);
    /**Gets the mean and standard deviation for the given cluster or clusters.*/
    void getMean(int clusterId,WaveformView::PresentationMode mode);
    void getMean(const QList<int> &clusterIds, WaveformView::PresentationMode mode);

    /**
  * Internal class use to send information to the WaveformView to inform it that
  * the data requested have been collected.  Carries copies of the request
  * parameters (the job that posted it retires itself) plus the request
  * generation the job was enqueued under, so the view can drop results of a
  * superseded request.
  */
    class GetWaveformsEvent : public QEvent{
        friend class WaveformThread;

    public:
        ~GetWaveformsEvent(){}

        int generation() const {return eventGeneration;}
        bool isSingleTriggeringCluster() const {return single;}
        int triggeringCluster() const {return clusterId;}
        QList<int> triggeringClusters() const {return clusterIds;}
        bool isMeanRequested() const {return meanRequested;}
        /** Returns the meanPresentation value snapshotted when the job was enqueued.
     *  Use this in customEvent instead of the live view field. */
        bool wasLaunchedWithMeanPresentation() const {return launchedWithMean;}
        /** Returns the PresentationMode snapshotted at enqueue time. Use in getMean()
     *  calls from customEvent instead of the live presentationMode field. */
        WaveformView::PresentationMode snapshotMode() const {return snapMode;}

    private:
        explicit GetWaveformsEvent(const WaveformThread& job):QEvent(QEvent::Type(QEvent::User + 200)),
            eventGeneration(job.jobGeneration),single(job.treatSingleCluster),clusterId(job.clusterId),
            clusterIds(job.clusterIds),meanRequested(job.meanRequested),
            launchedWithMean(job.snapMeanPresentation),snapMode(job.snapPresentationMode){}

        /**Field form, for the ticket closure: the completion may fire from a
        * waiter flush long after the job object is gone (epoch-snapshot
        * step 3a).*/
        GetWaveformsEvent(int gen,bool single,int clusterId,const QList<int>& clusterIds,
                          bool meanRequested,bool launchedWithMean,WaveformView::PresentationMode m)
            :QEvent(QEvent::Type(QEvent::User + 200)),
            eventGeneration(gen),single(single),clusterId(clusterId),clusterIds(clusterIds),
            meanRequested(meanRequested),launchedWithMean(launchedWithMean),snapMode(m){}

        int eventGeneration;
        bool single;
        int clusterId;
        QList<int> clusterIds;
        bool meanRequested;
        bool launchedWithMean;
        WaveformView::PresentationMode snapMode;
    };

    /**
  * Internal class use to send information to the WaveformView to inform it that
  * there is no data available for the requested cluster. A reason being that the cluster
  * has been suppressed after the job has been enqueued.  The job retires itself,
  * so the event only carries the request generation.
  */
    class NoWaveformDataEvent : public QEvent{
        friend class WaveformThread;

    public:
        ~NoWaveformDataEvent(){}

        int generation() const {return eventGeneration;}

    private:
        explicit NoWaveformDataEvent(const WaveformThread& job):QEvent(QEvent::Type(QEvent::User + 250)),
            eventGeneration(job.jobGeneration){}

        /**Field form, for the ticket closure (see GetWaveformsEvent).*/
        explicit NoWaveformDataEvent(int gen):QEvent(QEvent::Type(QEvent::User + 250)),
            eventGeneration(gen){}

        int eventGeneration;
    };

    /**Executed by a pool worker; fetches the waveforms (and computes the means
    * if requested), posts the completion event and retires the job.*/
    void run() override;

private:
    WaveformThread(WaveformView& view,Data& d,const std::shared_ptr<KlustersJobToken>& viewToken)
        :waveformView(view),meanRequested(false),data(d),token(viewToken),
        snapshot(d.currentSnapshot()),
        snapPresentationMode(WaveformView::SAMPLE),snapNbSpkToDisplay(0),snapStartTime(0),snapEndTime(0),snapMeanPresentation(false){
        setAutoDelete(true);
    }

    // Snapshot view parameters captured at enqueue time (on the GUI thread) so
    // run() never reads waveformView fields directly (they can be modified by
    // the main thread mid-flight).
    void snapshotViewParams(){
        snapPresentationMode  = waveformView.presentationMode;
        snapNbSpkToDisplay    = waveformView.nbSpkToDisplay;
        snapStartTime         = waveformView.startTime;
        snapEndTime           = waveformView.endTime;
        snapMeanPresentation  = waveformView.meanPresentation;
    }

    /**Captures the current request generation, counts this job as active and
    * hands it to the shared pool.  Called on the GUI thread only, as the last
    * step of each get* method.*/
    void enqueue();

    /**True once the view has superseded this job's request generation
    * (stopAndClearThreads()/willBeKilled() bump it): the job stops at its next
    * check, exactly where the per-thread stop flag used to be read.*/
    bool cancelled() const {
        return token->generation.load(std::memory_order_acquire) != jobGeneration;
    }

    /**The fetch and mean sweep.  Since the subscribe-don't-wait conversion
    * (epoch-snapshot step 3a) it never sleeps: a computation another job
    * owns is subscribed to through the request ticket instead of polled,
    * and the completion event fires when the last outstanding share —
    * this sweep, or a parked waiter — completes.*/
    void process();

    WaveformView& waveformView;
    int clusterId = 0;
    QList<int> clusterIds;
    bool treatSingleCluster = false;
    bool meanRequested;
    Data& data;
    /**Shared cancellation/completion state owned by the view.*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int jobGeneration = 0;
    /**The membership epoch captured at creation (epoch-snapshot step 1):
    * pins the epoch's tables, its .spk reader, and its waveform store for
    * the duration of the job — every Data waveform call in process() runs
    * against this snapshot (step 3b).*/
    std::shared_ptr<const Data::ClusteringSnapshot> snapshot;

    // Snapshots of WaveformView fields captured before the job is enqueued.
    WaveformView::PresentationMode snapPresentationMode;
    long snapNbSpkToDisplay;
    long snapStartTime;
    long snapEndTime;
    bool snapMeanPresentation;
};

#endif
