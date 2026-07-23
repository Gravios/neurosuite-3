/***************************************************************************
 * mergerecommendthread.h — background worker for the merge-recommendation scan.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MERGERECOMMENDTHREAD_H
#define MERGERECOMMENDTHREAD_H

#include <QThread>
#include <QEvent>
#include <QList>
#include <atomic>
#include <vector>

#include "mergerecommend.h"

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
// At 8736 fibers that is 38.1 M pairs, each costing an envelope-IOU over
// nSamp*nChan samples with a lag search -- minutes of work.  It used to run
// synchronously inside slotRefreshMergeRecommendations(), on the GUI thread,
// on every hierarchyChanged: which is why a single parent merge froze the UI
// for ~3 minutes on one core while every other matrix view sat threaded.
//
// This thread owns that scan.  It does NOT touch Data: the view snapshots the
// error-matrix slice and the per-cluster waveform templates on the GUI thread
// before starting it, so a concurrent edit cannot pull the tables out from
// under the worker.  That is the difference from the other matrix threads,
// which read Data live and rely on stopAllViewThreads() -- a protocol this
// panel is outside of, since it lives in a dock rather than under a
// KlustersView.
//
// Posts MergeRecommendEvent (User+604) when done.
// ---------------------------------------------------------------------------
class MergeRecommendThread : public QThread {
public:
    friend class MergeRecommendView;

    ~MergeRecommendThread() override {}

    void stopProcessing() {
        haveToStopProcessing.store(true, std::memory_order_release);
    }
    int getGeneration() const { return generation; }

    /// Ranked, capped, selection-filtered result. Valid once the event lands.
    std::vector<MergeCandidate> getResults() const { return results; }
    /// True when the run completed rather than being cancelled part-way.
    bool completed() const { return finished_ok; }

    class MergeRecommendEvent : public QEvent {
        friend class MergeRecommendThread;
    public:
        MergeRecommendThread* parentThread() { return &thread; }
        ~MergeRecommendEvent() override {}
    private:
        explicit MergeRecommendEvent(MergeRecommendThread& t)
            : QEvent(QEvent::Type(QEvent::User + 604)), thread(t) {}
        MergeRecommendThread& thread;
    };

protected:
    void run() override;

private:
    /**
     * Every argument is a VALUE COPY taken on the GUI thread -- see the class
     * comment.  @p gated is the error-gated pair list from mrGatePairsByError;
     * @p tplMean / @p tplSd hold the template mean / SD of every cluster that
     * appears in it, keyed by cluster id through @p tplId, each of length
     * nSamp*nChan.  A cluster absent from the snapshot means "no opinion",
     * matching clusterEnvelopeOverlap returning false.
     */
    MergeRecommendThread(MergeRecommendView& v, int gen,
                         std::vector<MergePair> gated,
                         std::vector<int> tplId,
                         std::vector<std::vector<double>> tplMean,
                         std::vector<std::vector<double>> tplSd,
                         int nSamp, int nChan, int maxShift,
                         std::size_t maxCount, double qualityFloor,
                         std::vector<int> restrictTo)
        : view(v), generation(gen),
          gated(std::move(gated)),
          tplId(std::move(tplId)),
          tplMean(std::move(tplMean)), tplSd(std::move(tplSd)),
          nSamp(nSamp), nChan(nChan), maxShift(maxShift),
          maxCount(maxCount), qualityFloor(qualityFloor),
          restrictTo(std::move(restrictTo)),
          haveToStopProcessing(false), finished_ok(false) { start(); }

    MergeRecommendView&              view;
    int                              generation;

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

    std::atomic_bool                 haveToStopProcessing;
    bool                             finished_ok;
    std::vector<MergeCandidate>      results;
};

#endif // MERGERECOMMENDTHREAD_H
