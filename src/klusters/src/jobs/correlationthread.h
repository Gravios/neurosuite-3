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
 *
 * Since the subscribe-don't-wait conversion (epoch-snapshot step 4) the job
 * never sleeps: a pair another job is already computing is subscribed to
 * through a RequestTicket instead of sleep(1)-polled, and the completion
 * event fires when the last outstanding share — this sweep, or a parked
 * waiter — completes, from whichever thread completes it.
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

        /**Field form, for the ticket closure: the completion may fire from
        * a waiter flush on another worker long after the job object is
        * gone (epoch-snapshot step 4).*/
        explicit CorrelationsEvent(int gen)
            :QEvent(QEvent::Type(QEvent::User + 300)),eventGeneration(gen){}

        int eventGeneration;
    };

    /**Executed by a pool worker; runs the correlogram sweep and retires
    * the job (the completion event fires when the request's last share
    * completes — right here when nothing was parked).*/
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

    /**The correlogram sweep.  Since the subscribe-don't-wait conversion
    * (epoch-snapshot step 4) it never sleeps: a computation another job
    * owns is subscribed to through the request ticket instead of polled,
    * and the completion event fires when the last outstanding share —
    * this sweep, or a parked waiter — completes.  Split out so run() can
    * retire the job on every path.*/
    void process();

    CorrelationView& correlationView;
    Data& data;
    QList<Pair> clusterPairs;
    QList<int> clusterIds;
    /**Shared cancellation/completion state owned by the view.*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int jobGeneration = 0;
    /**The membership epoch captured at creation (epoch-snapshot step 1):
    * pins the epoch's tables and its correlogram store for the duration of
    * the job — every Data correlogram call in process() runs against this
    * snapshot (step 4).*/
    std::shared_ptr<const Data::ClusteringSnapshot> snapshot;

    // Snapshots of CorrelationView fields captured before the job is enqueued.
    int snapBinSize;
    int snapTimeWindow;
};


#endif
