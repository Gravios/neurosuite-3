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
#include <cmath>
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

/** How a strip cell is shaded:
 *   Value — full-alpha stat colour (the cluster fired within the node's window);
 *   Dim   — the SAME stat colour at half alpha (the stat is still shown, but the
 *           cluster has no spikes in the node's [a,b], so the comparison is across
 *           non-overlapping time — dimmed so it reads as out-of-window);
 *   Grey  — solid grey, no value (a cell with no waveform statistic at all, e.g.
 *           every error-matrix cell, or a defensive size mismatch). */
enum MatrixStripShade { MatrixStripValue = 0, MatrixStripDim = 1, MatrixStripGrey = 2 };

/** Alpha applied to a Dim cell's stat colour. */
inline constexpr double kTemplateStripDimAlpha = 0.5;

/** Draw the M-template strip (right columns + bottom rows + the template×template
 *  corner) for an N-cluster matrix already drawn at (oriF, eff).
 *
 *  `value(r, c, &shade)` returns the display value for the EXTENDED cell at logical
 *  row r, column c — where an index in [0,N) is a cluster and in [N,N+M) is template
 *  (index-N) — and sets `shade` (MatrixStripShade) to how the cell is drawn: a
 *  full-alpha value, the same value at half alpha when the cluster/template don't
 *  overlap in time (Dim), or solid grey when there is no value (Grey).  Only cells
 *  with r>=N or c>=N are drawn (the N×N cluster block is already on screen).  Taking
 *  full (r,c) rather than (template,cluster) lets ASYMMETRIC matrices (residual,
 *  drift) give the two off-diagonal directions distinct values.  colourFor maps a
 *  value to a QColor; Grey cells use `greyColour`.
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
            MatrixStripShade shade = MatrixStripValue;
            const double v = value(r, c, shade);
            QColor fill;
            if (shade == MatrixStripGrey) fill = greyColour;                 // no value
            else {                                                            // value, full or dimmed
                fill = colourFor(v);
                if (shade == MatrixStripDim) fill.setAlphaF(kTemplateStripDimAlpha);
            }
            p.fillRect(QRectF(oriF.x() + at(c), y, eff, eff), fill);
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

// ── Strip hit-testing (§11.5) ──────────────────────────────────────────────
// Clicking a strip cell selects the cell's cluster and overlays the cell's node
// template; the geometry has to match drawMatrixTemplateStrip exactly, so it is
// defined here next to the renderer rather than duplicated in each view.

/** Map a 1-D offset `coord` (pixels from the strip origin along one axis) to an
 *  extended index in [0, N+M): [0,N) is a cluster cell, [N,N+M) is one of the M
 *  template cells after the gap.  Returns -1 for the gap or outside the grid.
 *  Mirrors the `at()` layout drawMatrixTemplateStrip paints with. */
inline int matrixStripIndexAt(double coord, double eff, int N, int M)
{
    if (eff <= 0.0 || coord < 0.0) return -1;
    if (coord < N * eff) { const int k = static_cast<int>(coord / eff); return (k < N) ? k : -1; }
    const double gap = kTemplateStripGapCells * eff;
    const double stripStart = N * eff + gap;
    if (coord < stripStart) return -1;                       // inside the gap
    const int t = static_cast<int>((coord - stripStart) / eff);
    return (t >= 0 && t < M) ? (N + t) : -1;
}

/** What a strip-cell click resolves to: the cluster whose row/column the cell sits
 *  on and the template node it compares against.  `clusterId` is -1 for the
 *  template×template corner (no cluster). */
struct MatrixStripHit { bool ok = false; int clusterId = -1; int classId = -1; int node = -1; };

/** Resolve a widget-space click (px,py) against a strip drawn at (oriF, eff) for an
 *  N-cluster matrix with the templates `tpl`.  ok=false when the click is outside
 *  the strip — the gap, the N×N cluster block, or off-grid.  `clusters` is the
 *  view's cluster-id list (operator[] -> int), one id per matrix row/column. */
template <class ClusterListT>
inline MatrixStripHit matrixStripHitTest(double px, double py,
                                         const QPointF& oriF, double eff,
                                         const ClusterListT& clusters,
                                         const std::vector<MatrixTemplateCol>& tpl)
{
    MatrixStripHit h;
    const int N = static_cast<int>(clusters.size());
    const int M = static_cast<int>(tpl.size());
    if (M <= 0 || N <= 0) return h;
    const int r = matrixStripIndexAt(py - oriF.y(), eff, N, M);
    const int c = matrixStripIndexAt(px - oriF.x(), eff, N, M);
    if (r < 0 || c < 0) return h;
    if (r < N && c < N) return h;                            // the cluster block, not the strip
    const MatrixTemplateCol* t = nullptr;
    if (r < N && c >= N)      { h.clusterId = clusters[r]; t = &tpl[c - N]; }  // cluster row × template col
    else if (r >= N && c < N) { h.clusterId = clusters[c]; t = &tpl[r - N]; }  // template row × cluster col
    else                      { t = &tpl[r - N]; }                            // template × template corner
    h.classId = t->classId; h.node = t->node; h.ok = true;
    return h;
}

#endif // MATRIXTEMPLATECOLS_H
