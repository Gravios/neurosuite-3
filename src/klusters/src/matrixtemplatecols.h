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
 *  window in SECONDS (used for the spike-in-window greying).  `std` is the
 *  node's per-point standard deviation in the SAME layout and length (its square
 *  is the template's own noise floor — the residual matrix needs it for the
 *  template-row direction); it may be empty when the node carries no std. */
struct MatrixTemplateCol {
    int   node    = -1;
    int   classId = -1;
    double a = 0.0, b = 0.0;
    std::vector<float> mean;        // channel-major, nChan*nSamp
    std::vector<float> std;         // channel-major, nChan*nSamp (empty if unknown)
};

/** Cells of gap between the N-cluster block and the M-template strip. */
inline constexpr int kTemplateStripGapCells = 1;

/** Draw the M-template strip (right columns + bottom rows + the template×template
 *  corner) for an N-cluster matrix already drawn at (oriF, eff).
 *
 *  `value(r, c, &grey)` returns the display value for the EXTENDED cell at logical
 *  row r, column c — where an index in [0,N) is a cluster and in [N,N+M) is template
 *  (index-N) — and sets `grey` true when the cell should be greyed.  Only cells with
 *  r>=N or c>=N are drawn (the N×N cluster block is already on screen).  Taking full
 *  (r,c) rather than (template,cluster) lets ASYMMETRIC matrices (residual, drift)
 *  give the two off-diagonal directions distinct values.  colourFor maps a value to
 *  a QColor; greyed cells use `greyColour`.
 *
 *  M is small (a handful of marked nodes), so the ~2NM+M² per-cell fillRects are cheap. */
template <class ValueFn, class ColourFn>
inline void drawMatrixTemplateStrip(QPainter& p, const QPointF& oriF, double eff,
                                    int N, int M,
                                    ValueFn value, ColourFn colourFor,
                                    const QColor& greyColour = QColor(70, 70, 70),
                                    const QColor& sepColour  = QColor(200, 200, 90))
{
    if (M <= 0 || eff <= 0.0) return;
    const double gap = kTemplateStripGapCells * eff;
    auto at = [&](int k){ return (k < N) ? k * eff : (N * eff + gap + (k - N) * eff); };

    p.save();
    p.setPen(Qt::NoPen);
    for (int r = 0; r < N + M; ++r) {
        const double y = oriF.y() + at(r);
        for (int c = 0; c < N + M; ++c) {
            if (r < N && c < N) continue;        // the cluster block is already drawn
            bool grey = false; const double v = value(r, c, grey);
            p.fillRect(QRectF(oriF.x() + at(c), y, eff, eff), grey ? greyColour : colourFor(v));
        }
    }
    // A separator line in the gap so the strip reads as appended, not part of the grid.
    QPen sep(sepColour); sep.setCosmetic(true); sep.setWidth(0);
    p.setPen(sep);
    const double x  = oriF.x() + N * eff + gap * 0.5;
    const double yy = oriF.y() + N * eff + gap * 0.5;
    const double span = (N + M) * eff + gap;
    p.drawLine(QPointF(x, oriF.y()), QPointF(x, oriF.y() + span));
    p.drawLine(QPointF(oriF.x(), yy), QPointF(oriF.x() + span, yy));
    p.restore();
}

#endif // MATRIXTEMPLATECOLS_H
