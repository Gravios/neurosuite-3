/***************************************************************************
 * matrixclick.h — resolve a plain/Ctrl click on a "simple" curation matrix
 * (residual / drift) into a selection action.
 *
 * ResidualMatrixView and DriftMatrixView had a byte-identical select-on-release
 * body: the marked-node strip cell if the strip is shown and hit, else the
 * clicked cluster pair, with Ctrl extending rather than replacing.  This holds
 * that decision in one pure place — the same composition the four matrices
 * already use for pan/zoom state (matrixviewport.h) and the drag glue
 * (matrixnavigator.h).  The ERROR and TEMPLATE matrices keep their own, richer
 * release (selected-pair accumulation for the G-group merge, pair boxes, the
 * Apply/xcorr widgets, Error's display-order remap), so they do not use this.
 *
 * Pure and Qt-light (no document, no widget, no signal): it returns a decision
 * and the caller applies it — emits templateCellActivated / calls the doc /
 * repaints — so the Qt coupling stays in the view and this stays unit-testable.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXCLICK_H
#define MATRIXCLICK_H

#include <functional>
#include <QList>
#include <QPointF>
#include "matrixtemplatecols.h"     // MatrixStripHit
#include "matrixtemplatestrip.h"    // MatrixTemplateStrip (empty()/hitTest())

/** What a click resolved to (pure data; the view applies it). */
struct MatrixPairSelection {
    bool valid       = false;   // false → the click hit nothing selectable (early return)
    bool isStripCell = false;   // true → emit templateCellActivated(clusterId, node) + repaint
    int  clusterId   = 0;
    int  node        = 0;
    QList<int> clustersToShow;  // the cluster pair to show (cluster-cell case)
    bool extend      = false;   // Ctrl held → addFromMatrix, else selectFromMatrix
};

/** Resolve a click at @p posF.  Mirrors the identical residual/drift release logic: the
 *  marked-node template strip cell if the strip is shown and hit, else the clicked cluster
 *  pair (row/col via the view's own cellAtX / cellAtY, passed in since the geometry is
 *  per-view).  Pure — the caller emits the signal / calls the doc / repaints. */
inline MatrixPairSelection matrixResolveClick(
        const QPointF& posF, bool ctrlHeld, bool dataReady,
        const QList<int>& clusterList, const MatrixTemplateStrip& strip,
        const QPointF& stripOrigin, double stripCell,
        const std::function<int(int)>& cellAtX,
        const std::function<int(int)>& cellAtY)
{
    MatrixPairSelection r;
    if (!dataReady || clusterList.isEmpty())
        return r;

    // Marked-node template region (§11.5): a strip cell selects its cluster and overlays
    // its node, checked before the cluster-pair hit-test below.
    if (!strip.empty()) {
        const MatrixStripHit sh =
            strip.hitTest(posF.x(), posF.y(), stripOrigin, stripCell, clusterList);
        if (sh.ok) {
            r.valid = true; r.isStripCell = true;
            r.clusterId = sh.clusterId; r.node = sh.node;
            return r;
        }
    }

    const int col = cellAtX(posF.toPoint().x());
    const int row = cellAtY(posF.toPoint().y());
    if (row < 0 || col < 0)
        return r;

    r.valid = true;
    r.clustersToShow.append(clusterList[row]);
    if (clusterList[col] != clusterList[row])
        r.clustersToShow.append(clusterList[col]);
    r.extend = ctrlHeld;
    return r;
}

#endif // MATRIXCLICK_H
