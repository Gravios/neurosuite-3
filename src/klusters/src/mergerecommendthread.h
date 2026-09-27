/***************************************************************************
 * mergerecommendthread.h — background worker for the merge-recommendation scan.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MERGERECOMMENDTHREAD_H
#define MERGERECOMMENDTHREAD_H

#include <QRunnable>
#include <QEvent>
#include <QList>
#include <memory>
#include <vector>

#include "mergerecommend.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the view)

class MergeRecommendView;

// ---------------------------------------------------------------------------
// Background thread: ranks every candidate parent-merge pair.
//
// mrRecommendMerges() is O(clusters^2) BY DESIGN and cannot be made otherwise
// without changing what it means: the quality figure is a percentile taken over
// every pair in the session, so restricting the enumeration to the selected
// cluster's own pairs would make "top decile" mean "top decile among this
// cluster's partners" and a cluster with nothing worth merging would still
// present its least-bad partner as a 0.9+ recommendation.  The restriction is
// therefore applied to the OUTPUT, after ranking.
//
// At 8736 parents that is 38.1 M pairs, each costing an envelope-IOU over
// nSamp*nChan samples with a lag search -- minutes of work.  It used to run
// synchronously inside slotRefreshMergeRecommendations(), on the GUI thread,
// on every hierarchyChanged: which is why a single parent merge froze the UI
// for ~3 minutes on one core while every other matrix view sat threaded.
//
// This job owns that scan.  It does NOT touch Data: the view snapshots the
// error-matrix slice and the per-cluster waveform templates on the GUI thread
// before starting it, so a concurrent edit cannot pull the tables out from
// under the worker.  That is the difference from the other matrix jobs,
// which read Data live and rely on stopAllViewThreads() -- a protocol this
// panel is outside of, since it lives in a dock rather than under a
// KlustersView.
//
// Posts MergeRecommendEvent (User+605) when done.
//
// Formerly one QThread per scan; now a QRunnable on the shared worker pool
// (KlustersJobPool -- worker-pool conversion, step 5).  Creating it launches
// the request; the pool owns and deletes it after run().  The view supersedes
// it by bumping its token's request generation, and the ranked result travels
// inside the completion event, moved out of the job as it posts.
// ---------------------------------------------------------------------------
class MergeRecommendThread : public QRunnable {
public:
    friend class MergeRecommendView;

    ~MergeRecommendThread() override {}

    class MergeRecommendEvent : public QEvent {
        friend class MergeRecommendThread;
    public:
        ~MergeRecommendEvent() override {}

        int generation() const { return eventGeneration; }
        /// True when the run completed rather than being superseded part-way.
        bool completed() const { return finishedOk; }
        /// Ranked, capped, selection-filtered result.  Mutable on purpose:
        /// the accepting handler moves it out.
        std::vector<MergeCandidate>& getResults() { return resultsPayload; }
    private:
        explicit MergeRecommendEvent(MergeRecommendThread& job)
            : QEvent(QEvent::Type(QEvent::User + 605)),
              eventGeneration(job.jobGeneration),
              finishedOk(job.finished_ok),
              resultsPayload(std::move(job.results)) {}

        int eventGeneration;
        bool finishedOk;
        std::vector<MergeCandidate> resultsPayload;
    };

    /**Executed by a pool worker; ranks the pairs, posts the completion event
    * and retires the job.*/
    void run() override;

private:
    /**
     * Every argument is a VALUE COPY taken on the GUI thread -- see the class
     * comment.  @p gated is the error-gated pair list from mrGatePairsByError;
     * @p tplMean / @p tplSd hold the template mean / SD of every cluster that
     * appears in it, keyed by cluster id through @p tplId, each of length
     * nSamp*nChan.  A cluster absent from the snapshot means "no opinion",
     * matching clusterEnvelopeOverlap returning false.
     *
     * Creating the job launches the request on the shared pool.  Runs on the
     * GUI thread only.
     */
    MergeRecommendThread(MergeRecommendView& v,
                         const std::shared_ptr<KlustersJobToken>& viewToken,
                         std::vector<MergePair> gated,
                         std::vector<int> tplId,
                         std::vector<std::vector<double>> tplMean,
                         std::vector<std::vector<double>> tplSd,
                         int nSamp, int nChan, int maxShift,
                         std::size_t maxCount, double qualityFloor,
                         std::vector<int> restrictTo);

    /**True once the view has superseded this job's request generation: the
    * scan stops at its next check, exactly where the per-thread stop flag
    * used to be read.*/
    bool cancelled() const {
        return token->generation.load(std::memory_order_acquire) != jobGeneration;
    }

    /**Posts @p event to the view, unless the view is being destroyed
    * (fenced by the token's postMutex/viewDead).*/
    void post(QEvent* event);

    /**The old run() body; split out so run() can retire the job on every path.*/
    void process();

    MergeRecommendView&              view;
    /**Shared cancellation/completion state owned by the view.*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int                              jobGeneration = 0;

    std::vector<MergePair>           gated;      // error-gated pairs to score
    std::vector<int>                 tplId;      // snapshot slot -> cluster id
    std::vector<std::vector<double>> tplMean;    // [idx][nSamp*nChan]
    std::vector<std::vector<double>> tplSd;      // [idx][nSamp*nChan]
    int                              nSamp;
    int                              nChan;
    int                              maxShift;
    std::size_t                      maxCount;
    double                           qualityFloor;
    std::vector<int>                 restrictTo;

    bool                             finished_ok;
    std::vector<MergeCandidate>      results;
};

#endif // MERGERECOMMENDTHREAD_H
