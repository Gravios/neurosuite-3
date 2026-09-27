/***************************************************************************
 * mergerecommendview.cpp
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "mergerecommendview.h"
#include "mergerecommendthread.h"

#include "configuration.h"
#include "errormatrixview.h"
#include "data.h"
#include "klustersview.h"
#include "mergerecommend.h"

#include <QApplication>
#include <QMutexLocker>
#include <QHeaderView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <vector>

MergeRecommendView::MergeRecommendView(QWidget* parent)
    : QWidget(parent),
      jobToken(std::make_shared<KlustersJobToken>())
{
    QVBoxLayout* box = new QVBoxLayout(this);
    box->setContentsMargins(2, 2, 2, 2);
    box->setSpacing(2);

    notice = new QLabel(this);
    notice->setWordWrap(true);
    notice->setAlignment(Qt::AlignCenter);
    box->addWidget(notice);

    tree = new QTreeWidget(this);
    tree->setColumnCount(4);
    tree->setHeaderLabels({ tr("Merge"), tr("Error"), tr("Overlap"), tr("Quality") });
    tree->setRootIsDecorated(false);
    tree->setUniformRowHeights(true);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->header()->setStretchLastSection(true);
    box->addWidget(tree, 1);

    connect(tree, &QTreeWidget::itemActivated,
            this, &MergeRecommendView::onItemActivated);
    connect(tree, &QTreeWidget::itemDoubleClicked,
            this, &MergeRecommendView::onItemActivated);

    setNotice(tr("No recommendations yet."));
}

void MergeRecommendView::setNotice(const QString& text)
{
    notice->setText(text);
    notice->setVisible(!text.isEmpty());
}

void MergeRecommendView::onItemActivated(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    const int a = item->data(0, Qt::UserRole).toInt();
    const int b = item->data(0, Qt::UserRole + 1).toInt();
    if (a < 0 || b < 0) return;
    emit recommendationActivated(QList<int>{ a, b });
}

void MergeRecommendView::refreshFrom(KlustersView* view, Data* data,
                                     const QList<int>& selected)
{
    tree->clear();

    if (!view || !data) {
        setNotice(tr("No active display."));
        return;
    }

    ErrorMatrixView* emv = view->findChild<ErrorMatrixView*>();

    // Only the error matrix is needed now.  The second witness is the waveform
    // envelope overlap, which reads the cached mean/SD the waveform view already
    // uses -- so no residual matrix display has to be open or computed.
    if (!emv) {
        setNotice(tr("Needs an error matrix in this display."));
        return;
    }
    if (!emv->hasComputedData()) {
        setNotice(tr("Compute the error matrix (press U) to get recommendations."));
        return;
    }
    if (emv->isOutOfDate()) {
        setNotice(tr("The error matrix is out of date \u2014 press U to recompute, "
                     "then these refresh."));
        return;
    }

    const Array<double>* E = emv->matrixData();
    if (!E) {
        setNotice(tr("Matrix data unavailable."));
        return;
    }

    const QList<int> eIdsQ = emv->matrixComputedClusterList();
    std::vector<int> eIds;
    eIds.reserve(static_cast<size_t>(eIdsQ.size()));
    for (const int id : eIdsQ) eIds.push_back(id);

    // The Array is 1-based; mrRecommendMerges hands out 0-based indices.
    std::function<double(int,int)> errAt = [E](int i, int j){
        return (*E)(i + 1, j + 1);
    };
    // The envelope overlap itself now runs on the worker (mergerecommendthread),
    // scored against a snapshot of the templates rather than the live cache.  It
    // still searches a lag rather than demanding the two templates line up
    // exactly: spikes are extracted at a detected peak and that alignment is not
    // exact, so a pair split by one sample (30 us at sr=32552) would otherwise
    // score like an unrelated pair -- and that is the pair most worth listing.
    // The winning lag is still discarded: the overlap callback is
    // bool(int,int,double&) and has nowhere to put it.

    // Read the knobs fresh each refresh so a Preferences change lands on the
    // next refresh rather than at the next restart.  Configuration clamps them,
    // so the cast to size_t below cannot underflow into "no cap".
    const int    maxRecs = configuration().getMergeRecommendMax();
    const double eFloor  = configuration().getMergeRecommendErrorFloor();
    const double qFloor  = configuration().getMergeRecommendQualityFloor();

    std::vector<int> restrict;
    restrict.reserve(static_cast<size_t>(selected.size()));
    for (const int id : selected) restrict.push_back(id);

    // ── half 1, here: the O(clusters^2) arithmetic sweep ────────────────────
    // It has to run on this thread because it reads the error matrix live, and
    // ErrorMatrixView frees that matrix wholesale when a new one lands.  It is
    // cheap per pair -- two array reads, an average and a compare -- and it is
    // what lets half 2 be handed off without copying the matrix (610 MB at 8736
    // clusters) or risking a use-after-free on it.
    const std::vector<MergePair> gated = mrGatePairsByError(eIds, errAt, eFloor);

    if (gated.empty()) {
        setNotice(selected.isEmpty()
            ? tr("No pair clears both witnesses right now.\n"
                 "(Waveforms must be computed for the overlap.)")
            : tr("Nothing worth merging with the selected cluster(s).\n"
                 "Clear the selection to see the whole session."));
        return;
    }

    // Snapshot only the templates the surviving pairs actually reference, so the
    // worker never touches Data.  A cluster with no current template is simply
    // absent, which the worker reads as "no opinion" -- the same contract
    // clusterEnvelopeOverlap has when it returns false.
    std::vector<int> tplId;
    std::vector<std::vector<double>> tplMean, tplSd;
    {
        std::vector<int> need;
        need.reserve(gated.size() * 2);
        for (const MergePair& p : gated) { need.push_back(p.a); need.push_back(p.b); }
        std::sort(need.begin(), need.end());
        need.erase(std::unique(need.begin(), need.end()), need.end());
        tplId.reserve(need.size());
        tplMean.reserve(need.size());
        tplSd.reserve(need.size());
        std::vector<double> m, sd;
        for (const int id : need)
            if (data->clusterTemplateFor(id, m, sd)) {
                tplId.push_back(id);
                tplMean.push_back(m);
                tplSd.push_back(sd);
            }
    }

    // ── half 2, on a worker: one envelope IOU with a lag search per pair ─────
    // This is the part that took minutes on the GUI thread at 8736 parents.
    //Supersede any in-flight scan; the replacement job enqueued below
    //captures the new generation.
    jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    computing = true;
    lastRestricted = !selected.isEmpty();

    setNotice(tr("Ranking %1 candidate pair(s)\u2026").arg(static_cast<qulonglong>(gated.size())));

    //Creating the job launches it; the pool owns and deletes it.
    new MergeRecommendThread(
        *this, jobToken, gated, tplId, tplMean, tplSd,
        data->nbSamplesPerWaveform(), data->nbOfChannels(),
        configuration().getMergeRecommendMaxShift(),
        static_cast<std::size_t>(maxRecs), qFloor, restrict);
}

MergeRecommendView::~MergeRecommendView()
{
    goingToDie = true;
    stopThreads();
}

void MergeRecommendView::stopThreads()
{
    //Supersede the in-flight scans (they early-out at their next check) and
    //fence their completion posts: the jobs read only their own snapshots, so
    //nothing waits for them — the pool owns and deletes them, and their
    //results are freed with the events nobody takes.
    jobToken->generation.fetch_add(1, std::memory_order_acq_rel);
    {
        QMutexLocker lock(&jobToken->postMutex);
        jobToken->viewDead = true;
    }
    QApplication::removePostedEvents(this);
}

void MergeRecommendView::customEvent(QEvent* event)
{
    if (event->type() != QEvent::Type(QEvent::User + 605)) return;

    auto* ev = static_cast<MergeRecommendThread::MergeRecommendEvent*>(event);

    //The job retired itself and the result travels in the event, so there is
    //no thread to wait for or delete anymore.
    const bool genMatch = (ev->generation()
                           == jobToken->generation.load(std::memory_order_acquire));
    // Only the generation we are waiting on may paint, and only it clears the
    // badge: a superseded result arriving must not cancel the newer compute that
    // replaced it.
    const bool accepted = (genMatch && ev->completed());
    if (genMatch) computing = false;

    if (goingToDie || !accepted) return;
    populate(std::move(ev->getResults()));
}

void MergeRecommendView::populate(const std::vector<MergeCandidate>& recs)
{
    tree->clear();

    if (recs.empty()) {
        setNotice(lastRestricted
            ? tr("Nothing worth merging with the selected cluster(s).\n"
                 "Clear the selection to see the whole session.")
            : tr("No pair clears both witnesses right now.\n"
                 "(Waveforms must be computed for the overlap.)"));
        return;
    }
    setNotice(QString());

    for (const MergeCandidate& c : recs) {
        QTreeWidgetItem* it = new QTreeWidgetItem(tree);
        it->setText(0, tr("%1 + %2").arg(c.a).arg(c.b));
        it->setText(1, QString::number(c.errorScore,    'f', 3));
        it->setText(2, QString::number(c.overlapScore, 'f', 3));
        it->setText(3, QString::number(c.quality,       'f', 2));
        it->setData(0, Qt::UserRole,     c.a);
        it->setData(0, Qt::UserRole + 1, c.b);
        it->setToolTip(0, tr("Both the error matrix and the envelope overlap rank this pair highly.\n"
                             "Double-click to select it in the main palette."));
    }
    for (int c = 0; c < 3; ++c) tree->resizeColumnToContents(c);
}
