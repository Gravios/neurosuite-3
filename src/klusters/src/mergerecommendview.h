/***************************************************************************
 * mergerecommendview.h — panel listing recommended parent-cluster merges.
 *
 * Sits in the third section of the palette stack, under the main palette and
 * the child palette.  It reads the error matrix and the residual matrix of the
 * active display and lists the pairs both agree on; the ranking itself lives in
 * mergerecommend.h, which is Qt-free and unit-tested.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MERGERECOMMENDVIEW_H
#define MERGERECOMMENDVIEW_H

#include <QLabel>
#include <QList>
#include <QTreeWidget>
#include <QWidget>

class KlustersView;

/**Lists the top parent-merge candidates, best first (cap and thresholds come
 * from Preferences -> Sorting -> Recommended Merges).
 *
 * The panel is a READER: it never moves spikes and never edits the document.
 * Double-clicking a row selects that pair in the main palette, leaving the
 * merge itself to the existing hierarchy ops so undo, hierarchy rebuild and
 * colour handling stay in one place.*/
class MergeRecommendView : public QWidget {
    Q_OBJECT

public:
    explicit MergeRecommendView(QWidget* parent = nullptr);

    // The three knobs live in Preferences -> Sorting -> Recommended Merges and
    // are read from Configuration at each refresh, so a change applies to the
    // next refresh without restarting.
public Q_SLOTS:
    /**Recompute the list from @p view's matrices.  Safe to call with nullptr
     * (clears), or when either matrix is missing, uncomputed or stale — the
     * panel then says why rather than showing numbers it cannot stand behind.
     *
     * @p selected restricts the list to pairs involving those clusters (the
     * palette selection); empty lists the whole session's best.  The restriction
     * filters the OUTPUT only: quality is always ranked over every pair, so it
     * means the same thing whatever happens to be selected.*/
    void refreshFrom(KlustersView* view, const QList<int>& selected = QList<int>());

Q_SIGNALS:
    /**A row was activated: select these clusters in the main palette.*/
    void recommendationActivated(const QList<int>& clusters);

private Q_SLOTS:
    void onItemActivated(QTreeWidgetItem* item, int column);

private:
    void setNotice(const QString& text);

    QTreeWidget* tree   = nullptr;
    QLabel*      notice = nullptr;
};

#endif // MERGERECOMMENDVIEW_H
