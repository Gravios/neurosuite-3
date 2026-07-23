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
#include <unordered_map>

void MergeRecommendThread::run()
{
    // Cluster id -> snapshot slot.  Built here rather than passed in so the GUI
    // thread's share of the work stays the sweep and the copy, nothing more.
    std::unordered_map<int,int> slot;
    slot.reserve(tplId.size() * 2);
    for (std::size_t i = 0; i < tplId.size(); ++i)
        slot.emplace(tplId[i], static_cast<int>(i));

    auto cancelled = [this]{
        return haveToStopProcessing.load(std::memory_order_acquire);
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
                               restrictTo, cancelled);
    finished_ok = !cancelled();

    // The view owns acceptance: it checks the generation and discards a result
    // that a newer refresh has already superseded.
    QApplication::postEvent(&view, new MergeRecommendEvent(*this));
}
