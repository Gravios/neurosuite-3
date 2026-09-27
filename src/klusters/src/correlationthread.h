/***************************************************************************
                          correlationthread.h  -  description
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

#ifndef CORRELATIONTHREAD_H
#define CORRELATIONTHREAD_H

#include <memory>

//include files for the application
#include "correlationview.h"
#include "data.h"
#include "pair.h"

//include files for QT
#include <QRunnable>

#include <QEvent>
#include <QList>


/** Job used to compute the correlograms displayed in the CorrelationView.
 * No heavy computation is done in this class, the job calls the Data object which
 * will do the work.
 *
 * Formerly one QThread per recompute; now a QRunnable on the shared worker
 * pool (KlustersJobPool — worker-pool conversion, step 3), following the
 * WaveformThread template: creation still launches the request (as the old
 * constructor's start() did), the view's KlustersJobToken carries the request
 * generation the job polls where it used to poll its per-thread stop flag,
 * the completion event carries that generation instead of a thread pointer,
 * and the job retires itself (the pool deletes it; the active count drops as
 * run()'s last act).  The pair list is now held by value, and the bin size /
 * time window are snapshotted at enqueue time on the GUI thread — the old
 * thread read the live view fields mid-run.
 *@author Lynn Hazan
 */

class CorrelationThread : public QRunnable {


public:
    //Only the method getCorrelations of CorrelationView has access to the private part of CorrelationThread,
    //the constructor of CorrelationThread being private, only this method can create a new CorrelationThread
    friend CorrelationThread* CorrelationView::getCorrelations(const QList<Pair>& pairsToCompute,const QList<int>& clusterIds);

    ~CorrelationThread() override {}

    /**
  * Internal class use to send information to the CorrelationView to inform it that
  * the data requested have been collected.  The job retires itself, so the
  * event only carries the request generation it was enqueued under, letting
  * the view drop results of a superseded request.
  * @author Lynn Hazan
  */
    class CorrelationsEvent : public QEvent{
        friend class CorrelationThread;

    public:
        ~CorrelationsEvent(){}

        int generation() const {return eventGeneration;}

    private:
        explicit CorrelationsEvent(const CorrelationThread& job)
            :QEvent(QEvent::Type(QEvent::User + 300)),eventGeneration(job.jobGeneration){}

        int eventGeneration;
    };

    /**Executed by a pool worker; computes the correlograms, posts the
    * completion event and retires the job.*/
    void run() override;

private:
    /**Snapshots the view parameters, captures the request generation, counts
    * the job as active and hands it to the shared pool — creating the job
    * launches the request, as the old thread's constructor did.  Runs on the
    * GUI thread only.*/
    CorrelationThread(CorrelationView& view,Data& d,const QList<Pair>& pairs,const QList<int>& clusterIds,
                      const std::shared_ptr<KlustersJobToken>& viewToken);

    /**True once the view has superseded this job's request generation
    * (stopRunningThreads()/willBeKilled() bump it): the job stops at its next
    * check, exactly where the per-thread stop flag used to be read.*/
    bool cancelled() const {
        return token->generation.load(std::memory_order_acquire) != jobGeneration;
    }

    /**Posts @p event to the view, unless the view is being destroyed
    * (fenced by the token's postMutex/viewDead, see ~CorrelationView()).*/
    void post(QEvent* event);

    /**The old run() body: the correlogram loop, posting CorrelationsEvent at
    * the end.  Split out so run() can retire the job on every path.*/
    void process();

    CorrelationView& correlationView;
    Data& data;
    QList<Pair> clusterPairs;
    QList<int> clusterIds;
    /**Shared cancellation/completion state owned by the view.*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int jobGeneration = 0;

    // Snapshots of CorrelationView fields captured before the job is enqueued.
    int snapBinSize;
    int snapTimeWindow;
};


#endif
