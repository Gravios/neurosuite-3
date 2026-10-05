/***************************************************************************
 * matrixtemplatecols.h — marked-node templates pushed onto the curation
 * matrices as extra cluster×template columns/rows.
 *
 * When a PRIMARY template class is marked (the palette ★) and one or more of
 * its lineage nodes are marked (edit-mode context menu, see ClusterView), each
 * marked node's mean waveform becomes a "template column" appended at the edge
 * of every curation matrix, separated by a gap.  A cell (cluster, template)
 * shows that matrix's own statistic between the cluster's spikes/mean and the
 * template, and is GREYED when the cluster has no spikes within the node's time
 * window [a,b] (comparing a cluster to a template built from a time region it
 * never fired in is meaningless).
 *
 * ONE STRUCT + ONE STRIP RENDERER FOR THE FOUR VIEWS (the error, template,
 * residual and drift matrices share no render base — matrixgrid.h explains
 * why), so the geometry is defined once here.  Each view supplies the per-cell
 * VALUE (its own stat) and GREY flags; this header lays out and paints the
 * strip from the view's existing (effMatrixTopLeft, effCellSize).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXTEMPLATECOLS_H
#define MATRIXTEMPLATECOLS_H

#include <vector>
#include <QColor>
#include <QPainter>
#include <QPointF>
#include <QRectF>

/** A marked lineage-node template offered to a curation matrix.  `mean` is
 *  CHANNEL-MAJOR (ch*nSamp + smp), length nChan*nSamp; [a,b] is the node's time
 *  window in SECONDS (used for the spike-in-window greying). */
struct MatrixTemplateCol {
    int   node    = -1;
    int   classId = -1;
    double a = 0.0, b = 0.0;
    std::vector<float> mean;        // channel-major, nChan*nSamp
};

/** Cells of gap between the N-cluster block and the M-template strip. */
inline constexpr int kTemplateStripGapCells = 1;

/** Draw the M-template strip (right columns + bottom rows + the template×template
 *  corner) for an N-cluster matrix already drawn at (oriF, eff).
 *
 *  valueGrey(i, j, &grey) returns the display VALUE for template i against
 *  LOGICAL column/row j — where j in [0,N) is cluster j and j in [N,N+M) is
 *  template (j-N) — and sets `grey` true when the cell should be greyed.
 *  colourFor maps a value to a QColor; greyed cells use `greyColour`.
 *
 *  The strip starts one gap-cell past the N block on each axis.  M is small
 *  (a handful of marked nodes), so per-cell fillRect is cheap. */
template <class ValueGreyFn, class ColourFn>
inline void drawMatrixTemplateStrip(QPainter& p, const QPointF& oriF, double eff,
                                    int N, int M,
                                    ValueGreyFn valueGrey, ColourFn colourFor,
                                    const QColor& greyColour = QColor(70, 70, 70),
                                    const QColor& sepColour  = QColor(200, 200, 90))
{
    if (M <= 0 || eff <= 0.0) return;
    const double gap   = kTemplateStripGapCells * eff;
    const double colX0 = oriF.x() + N * eff + gap;   // template columns start (x)
    const double rowY0 = oriF.y() + N * eff + gap;   // template rows start (y)

    auto cell = [&](double x, double y, double value, bool grey){
        p.fillRect(QRectF(x, y, eff, eff), grey ? greyColour : colourFor(value));
    };

    p.save();
    p.setPen(Qt::NoPen);
    // Right columns: template i (x = colX0 + i*eff) vs every cluster row j in [0,N),
    // then the template×template rows (the corner) below the gap.
    for (int i = 0; i < M; ++i) {
        const double x = colX0 + i * eff;
        for (int j = 0; j < N; ++j) {
            bool grey = false; const double v = valueGrey(i, j, grey);
            cell(x, oriF.y() + j * eff, v, grey);
        }
        for (int u = 0; u < M; ++u) {               // corner: template i vs template u
            bool grey = false; const double v = valueGrey(i, N + u, grey);
            cell(x, rowY0 + u * eff, v, grey);
        }
    }
    // Bottom rows: template i (y = rowY0 + i*eff) vs every cluster column j in [0,N).
    for (int i = 0; i < M; ++i) {
        const double y = rowY0 + i * eff;
        for (int j = 0; j < N; ++j) {
            bool grey = false; const double v = valueGrey(i, j, grey);
            cell(oriF.x() + j * eff, y, v, grey);
        }
    }
    // A separator line in the gap so the strip reads as appended, not part of the grid.
    QPen sep(sepColour); sep.setCosmetic(true); sep.setWidth(0);
    p.setPen(sep);
    const double x = oriF.x() + N * eff + gap * 0.5;
    const double y = oriF.y() + N * eff + gap * 0.5;
    const double spanX = (N + M) * eff + gap;
    const double spanY = (N + M) * eff + gap;
    p.drawLine(QPointF(x, oriF.y()), QPointF(x, oriF.y() + spanY));
    p.drawLine(QPointF(oriF.x(), y), QPointF(oriF.x() + spanX, y));
    p.restore();
}

#endif // MATRIXTEMPLATECOLS_H
