/***************************************************************************
 * mergerecommendthread.cpp
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "mergerecommendthread.h"
#include "mergerecommendview.h"
#include "waveformiou.h"

#include <QApplication>
#include <QThreadPool>
#include <QMutexLocker>
#include <unordered_map>

MergeRecommendThread::MergeRecommendThread(MergeRecommendView& v,
                                           const std::shared_ptr<KlustersJobToken>& viewToken,
                                           std::vector<MergePair> gated,
                                           std::vector<int> tplId,
                                           std::vector<std::vector<double>> tplMean,
                                           std::vector<std::vector<double>> tplSd,
                                           int nSamp, int nChan, int maxShift,
                                           std::size_t maxCount, double qualityFloor,
                                           std::vector<int> restrictTo)
    : view(v), token(viewToken),
      gated(std::move(gated)),
      tplId(std::move(tplId)),
      tplMean(std::move(tplMean)), tplSd(std::move(tplSd)),
      nSamp(nSamp), nChan(nChan), maxShift(maxShift),
      maxCount(maxCount), qualityFloor(qualityFloor),
      restrictTo(std::move(restrictTo)),
      finished_ok(false)
{
    setAutoDelete(true);
    //The creation of the job launches the request, as the old thread's
    //constructor did with start().
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    KlustersJobPool::pool()->start(this, KlustersJobPool::BackgroundPriority);   // nobody is watching the dock fill
}

void MergeRecommendThread::post(QEvent* event)
{
    // Fence against view destruction: ~MergeRecommendView sets viewDead under
    // the same mutex, so while we hold it and viewDead is false the view is a
    // valid event receiver.
    QMutexLocker lock(&token->postMutex);
    if (token->viewDead) {
        delete event;
        return;
    }
    QApplication::postEvent(&view, event);
}

void MergeRecommendThread::run()
{
    process();
    //Retire: the job reads only its own snapshots, but the document-close
    //pool drain still treats active == 0 as "no job is running".
    token->active.fetch_sub(1, std::memory_order_acq_rel);
}

void MergeRecommendThread::process()
{
    // Cluster id -> snapshot slot.  Built here rather than passed in so the GUI
    // thread's share of the work stays the sweep and the copy, nothing more.
    std::unordered_map<int,int> slot;
    slot.reserve(tplId.size() * 2);
    for (std::size_t i = 0; i < tplId.size(); ++i)
        slot.emplace(tplId[i], static_cast<int>(i));

    auto stopPoll = [this]{
        return cancelled();
    };

    // Same contract as Data::clusterEnvelopeOverlap, scored against the snapshot
    // instead of the live template cache: absent template -> false -> the pair is
    // dropped as "no opinion" rather than guessed at.
    const std::function<bool(int,int,double&)> overlapOf =
        [this, &slot](int a, int b, double& iou) -> bool {
            const auto ia = slot.find(a);
            const auto ib = slot.find(b);
            if (ia == slot.end() || ib == slot.end()) return false;
            const std::vector<double>& ma = tplMean[static_cast<std::size_t>(ia->second)];
            const std::vector<double>& sa = tplSd  [static_cast<std::size_t>(ia->second)];
            const std::vector<double>& mb = tplMean[static_cast<std::size_t>(ib->second)];
            const std::vector<double>& sb = tplSd  [static_cast<std::size_t>(ib->second)];
            const std::size_t nTotal =
                static_cast<std::size_t>(nSamp) * static_cast<std::size_t>(nChan);
            if (ma.size() != nTotal || mb.size() != nTotal) return false;
            if (sa.size() != nTotal || sb.size() != nTotal) return false;

            iou = wfEnvelopeIouBestShift(
                [&ma](int i){ return ma[static_cast<std::size_t>(i)]; },
                [&sa](int i){ return sa[static_cast<std::size_t>(i)]; },
                [&mb](int i){ return mb[static_cast<std::size_t>(i)]; },
                [&sb](int i){ return sb[static_cast<std::size_t>(i)]; },
                nSamp, nChan, maxShift, 1.0, nullptr);
            return true;
        };

    results = mrRankGatedPairs(gated, overlapOf, maxCount, qualityFloor,
                               restrictTo, stopPoll);
    finished_ok = !cancelled();

    // The view owns acceptance: it checks the generation and discards a result
    // that a newer refresh has already superseded.
    post(new MergeRecommendEvent(*this));
}
