/***************************************************************************
 * mergerecommendview.cpp
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "mergerecommendview.h"

#include "configuration.h"
#include "errormatrixview.h"
#include "data.h"
#include "klustersview.h"
#include "mergerecommend.h"

#include <QHeaderView>
#include <QVBoxLayout>

#include <functional>
#include <vector>

MergeRecommendView::MergeRecommendView(QWidget* parent)
    : QWidget(parent)
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
    std::function<bool(int,int,double&)> overlapOf = [data](int a, int b, double& iou){
        return data->clusterEnvelopeOverlap(a, b, iou);
    };

    // Read the knobs fresh each refresh so a Preferences change lands on the
    // next refresh rather than at the next restart.  Configuration clamps them,
    // so the cast to size_t below cannot underflow into "no cap".
    const int    maxRecs = configuration().getMergeRecommendMax();
    const double eFloor  = configuration().getMergeRecommendErrorFloor();
    const double qFloor  = configuration().getMergeRecommendQualityFloor();

    std::vector<int> restrict;
    restrict.reserve(static_cast<size_t>(selected.size()));
    for (const int id : selected) restrict.push_back(id);

    const std::vector<MergeCandidate> recs =
        mrRecommendMerges(eIds, errAt, overlapOf,
                          static_cast<size_t>(maxRecs), eFloor, qFloor, restrict);

    if (recs.empty()) {
        setNotice(selected.isEmpty()
            ? tr("No pair clears both witnesses right now.\n"
                 "(Waveforms must be computed for the overlap.)")
            : tr("Nothing worth merging with the selected cluster(s).\n"
                 "Clear the selection to see the whole session."));
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
