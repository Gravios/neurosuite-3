/***************************************************************************
                          errormatrixthread.h  -  description
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

#ifndef ERRORMATRIXTHREAD_H
#define ERRORMATRIXTHREAD_H

#include <memory>
#include <vector>

//include files for the application
#include "errormatrixview.h"
#include "data.h"
#include "array.h"
#include "groupingassistant.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the view)

//include files for QT
#include <QRunnable>
#include <QSet>


#include <QEvent>
#include <QList>

/**Job used to compute the Error Matrix. Each element in the matrix
  * indicates how likely it is that the two clusters corresponding to the row and column
  * of the element contain spikes from the same neuron.
  *
  * Formerly one QThread per recompute; now a QRunnable on the shared worker
  * pool (KlustersJobPool — worker-pool conversion, step 4b).  Creating it
  * launches the request; the pool owns and deletes it after run().  The view
  * supersedes it by bumping the request generation of the token it was
  * enqueued under (display computes and background cache warmers run on
  * separate tokens so the view can still tell them apart), and the generation
  * check is routed into the GroupingAssistant through its external-stop
  * predicate — the assistant is where the interruptible work happens.  All
  * results travel inside the completion event, the two owned arrays freed by
  * the event when no handler takes them.
  *@author Lynn Hazan
  * @since klusters 1.1
  */

class ErrorMatrixThread : public QRunnable  {
public:

    //Only the method computeMatrix of ErrorMatrixView has access to the private part of ErrorMatrixThread,
    //the constructor of ErrorMatrixThread being private, only this method can create a new ErrorMatrixThread
    friend ErrorMatrixThread* ErrorMatrixView::computeMatrix();
    friend void ErrorMatrixView::launchCacheWarmer();

    ~ErrorMatrixThread() override {}

    /**
  * Internal class use to send information to the ErrorMatrixView to inform it that
  * the matrix has been computed.  Carries the results (the job retires itself)
  * plus the request generation, so the view can drop superseded results.
  * @since klusters 1.1
  */
    class ErrorMatrixEvent : public QEvent{
        friend class ErrorMatrixThread;

    public:
        /**Deletes whatever no handler took (stale generation, or the event
        * was removed unseen by removePostedEvents).*/
        ~ErrorMatrixEvent(){ delete probabilitiesResult; delete newRawResult; }

        int generation() const {return eventGeneration;}
        /**Hands the matrix (and its ownership) to the caller; nullptr when the
        * compute was cancelled or degenerate, or it was already taken.*/
        Array<double>* takeProbabilities(){ Array<double>* p = probabilitiesResult; probabilitiesResult = nullptr; return p; }
        QList<int> getClusterList() const {return clusterListResult;}
        QList<int> getComputedClusterList() const {return computedClusterListResult;}
        QList<int> getIgnoreClusterIndex() const {return ignoreClusterIndexResult;}

        /**Refreshed raw (pre-normalisation) probability cache for the view to
        * keep; ownership transfers to the caller.  nullptr if the incremental
        * path was not used, or it was already taken.*/
        Array<double>* takeNewRaw(){ Array<double>* r = newRawResult; newRawResult = nullptr; return r; }
        QList<int> getNewRawIds() const {return newRawIdsResult;}
        QList<int> getNewRawSizes() const {return newRawSizesResult;}
        int getNewRawDims() const {return newRawDimsResult;}
        bool getUsedIncremental() const {return usedIncrementalResult;}
        /**True for a background cache-warmer job: customEvent() installs only the raw
        * cache it produced and never touches the displayed matrix.*/
        bool getSeedOnly() const {return seedOnlyResult;}

    private:
        /**Takes over the job's results: the owned arrays move into the event
        * (the job posts as its final act and never touches them again).*/
        explicit ErrorMatrixEvent(ErrorMatrixThread& job):QEvent(QEvent::Type(QEvent::User + 600)),
            eventGeneration(job.jobGeneration),
            probabilitiesResult(job.probabilities),
            clusterListResult(job.clusterList),
            computedClusterListResult(job.computedClusterList),
            ignoreClusterIndexResult(job.ignoreClusterIndex),
            newRawResult(job.newRaw),
            newRawIdsResult(job.newRawIds),
            newRawSizesResult(job.newRawSizes),
            newRawDimsResult(job.newRawDims),
            usedIncrementalResult(job.usedIncremental),
            seedOnlyResult(job.seedOnly){ job.probabilities = nullptr; job.newRaw = nullptr; }

        int eventGeneration;
        Array<double>* probabilitiesResult;
        QList<int> clusterListResult;
        QList<int> computedClusterListResult;
        QList<int> ignoreClusterIndexResult;
        Array<double>* newRawResult;
        QList<int> newRawIdsResult;
        QList<int> newRawSizesResult;
        int newRawDimsResult;
        bool usedIncrementalResult;
        bool seedOnlyResult;
    };

    /**Executed by a pool worker; computes the matrix, posts the completion
    * event and retires the job.*/
    void run() override;

private:

    /**Creating the job launches the request on the shared pool (as the old
    * thread constructor's start() did).  Runs on the GUI thread only.*/
    ErrorMatrixThread(ErrorMatrixView& view,Data& d,
                      const std::shared_ptr<KlustersJobToken>& viewToken,
                      bool incremental, bool verify,
                      const Array<double>* prevRaw, const QList<int>& prevRawIds,
                      const QList<int>& prevRawSizes, int prevNbDimensions,
                      const QSet<int>& changedIds, bool seedOnly = false,
                      std::vector<int> activeDims = std::vector<int>(),
                      QList<int> activeClusters = QList<int>());

    /**True once the view has superseded this job's request generation: the
    * job (and, through the external-stop predicate, its assistant) stops at
    * the next check, exactly where the stop flags used to be read.*/
    bool cancelled() const {
        return token->generation.load(std::memory_order_acquire) != jobGeneration;
    }

    /**Posts @p event to the view, unless the view is being destroyed
    * (fenced by the token's postMutex/viewDead).*/
    void post(QEvent* event);

    /**The old run() body; split out so run() can retire the job on every path.*/
    void process();

        ErrorMatrixView& errorMatrixView;
    Data& data;
    /**Shared cancellation/completion state owned by the view (the display
    * stream's token for computeMatrix() jobs, the warmer stream's for
    * launchCacheWarmer() jobs).*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int jobGeneration = 0;
    /**The membership epoch captured at creation (epoch-snapshot step 1):
    * pins the epoch's tables for the duration of the job, and the assistant
    * reads them directly (step 5) — no per-compute table copies, no Data
    * mutex.  The .fet features are still the shared Data table, protected
    * by the edit-path quiesce until the plan's overlay step.*/
    std::shared_ptr<const Data::ClusteringSnapshot> snapshot;
    Array<double>* probabilities;
    QList<int> clusterList;
    QList<int> computedClusterList;
    QList<int> ignoreClusterIndex;
    GroupingAssistant assistant;

    // ── Incremental error-matrix support (opt-in) ───────────────────────────
    bool incremental;                 // use the incremental path when true
    bool verify;                      // also run the full path and log max|delta|
    const Array<double>* prevRaw;     // cached raw (pre-normalisation) columns
    QList<int> prevRawIds;            // cluster id per cached raw column
    QList<int> prevRawSizes;          // nbSpikes per cached raw column
    int prevNbDimensions;             // dims the cache was built with
    QSet<int> changedIds;             // ids whose membership changed since cache
    // Outputs for the view to store as the refreshed cache:
    Array<double>* newRaw;
    QList<int> newRawIds;
    QList<int> newRawSizes;
    int newRawDims;
    int nbReused;                     // columns reused (diagnostic)
    bool usedIncremental;             // true if the incremental path produced the result
    bool seedOnly;                    // background cache warmer: install raw cache only, no display
    /**0-based feature dimensions the model is restricted to (see featuremask.h);
     * empty = every dimension.  Declared last: the ctor initialises it last, and
     * -Wall warns when the init order and the declaration order disagree.*/
    std::vector<int> activeDims;
    QList<int> activeClusters;   ///< cluster subset; empty = all.  Declared
                                 ///< after activeDims so member init order
                                 ///< matches the initialiser list.

};

#endif
