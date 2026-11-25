/***************************************************************************
                          matrixgrid.h  -  gridlines over a square matrix view
                             -------------------
    begin                : 2026
    copyright            : (C) 2026 by the neurosuite-3 authors
 ***************************************************************************/

#ifndef MATRIXGRID_H
#define MATRIXGRID_H

#include <QPainter>
#include <QPointF>
#include <QPen>
#include <QColor>
#include <QList>
#include <QRectF>

/**Draw gridlines over an n x n matrix whose top-left cell corner is @p topLeft and
 * whose cells are @p cellSize on a side (both already in device coordinates, i.e.
 * zoom applied).
 *
 * Full-span lines only: n+1 verticals across the matrix height and n+1 horizontals
 * across its width.  Boxing each cell individually would draw every interior edge
 * twice -- 2n^2 segments instead of 2n+2 -- for exactly the same picture, and at
 * the cluster counts these views reach (matrices past 1000 a side) that is the
 * difference between a cheap overlay and a slow one.
 *
 * ONE DEFINITION FOR THE FOUR VIEWS.  ErrorMatrixView derives from ViewWidget while
 * the template, residual and drift views derive from QWidget, so there is no common
 * base to hang this on -- but all four already agree on effMatrixTopLeft() and
 * effCellSize(), which is all the geometry a grid needs.  Four copies of it would be
 * four chances to drift, which is not hypothetical in this codebase: two copies of
 * single-linkage diverged by an entire order of complexity, and one wrong
 * amplitude/SNR definition shipped in three collectors at once.
 *
 * The pen is COSMETIC, so the line stays one physical pixel whatever transform or
 * device pixel ratio is in force; a width-1 non-cosmetic pen would fatten under a
 * scaled painter and thicken on a HiDPI screen, which is not what "one pixel" means.
 *
 * @p minCellSize is the point below which the grid is skipped entirely.  It has to
 * exist: these views floor cellWidth at 4px when zoomed out over a large session, and
 * a dashed line every 4px covers a quarter of the matrix -- the grid would BE the
 * picture, hiding the very cells it is meant to separate.  A grid you cannot see
 * between is worse than no grid, so below the threshold the data is left alone.
 */
inline void drawMatrixGrid(QPainter& p, const QPointF& topLeft, double cellSize,
                           int n, const QColor& colour = QColor(128, 128, 128),
                           double minCellSize = 6.0)
{
    if (n <= 0 || cellSize <= 0.0) return;
    if (cellSize < minCellSize) return;      // see minCellSize above

    p.save();
    QPen pen(colour);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(0);            // cosmetic: exactly one device pixel
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const double x0 = topLeft.x();
    const double y0 = topLeft.y();
    const double span = n * cellSize;

    // n+1 lines each way: both outer edges included, so the matrix is closed.
    for (int i = 0; i <= n; ++i) {
        const double x = x0 + i * cellSize;
        p.drawLine(QPointF(x, y0), QPointF(x, y0 + span));
    }
    for (int i = 0; i <= n; ++i) {
        const double y = y0 + i * cellSize;
        p.drawLine(QPointF(x0, y), QPointF(x0 + span, y));
    }
    p.restore();
}

/**Parent identity bands + block boundaries for a JOINT-scoped matrix.
 *
 * When the scope spans several parents, each row/col belongs to one parent and
 * the matrix is only readable if that ownership is visible.  Two marks, both
 * driven by the same per-display-cell parent list:
 *   - a colour band along the top and left edges, one strip per CELL, in the
 *     owning PARENT's palette colour.  Per cell, not per run: the views' display
 *     order (id-sorted by the threads, Shift+S reordered, or permuted) owes the
 *     parents no contiguity, and a per-cell strip is correct under every
 *     ordering while still reading as solid blocks wherever runs are contiguous.
 *   - a solid full-span boundary line wherever two ADJACENT cells belong to
 *     different parents, so contiguous blocks get the separator the dashed cell
 *     grid cannot provide.  Under a permuted order the boundaries multiply,
 *     which is itself legible: it shows the order is not parent-grouped.
 *
 * ONE DEFINITION FOR THE FOUR VIEWS, for the same reason as drawMatrixGrid
 * above.  Geometry is the views' shared (effMatrixTopLeft, effCellSize) pair.
 * parentPerCell[i] is the parent owning display cell i (-1 = none: noise, or an
 * unparented id -- no band, and a parent<->none adjacency still draws a
 * boundary); colourPerCell[i] is its band colour (invalid = skip).  Bands sit
 * OUTSIDE the matrix rect, in the label margin, so no matrix pixel is covered
 * -- but the margin is where drawClusterIds paints the ids, and in all four
 * views it paints AFTER this, on top.  The strips are therefore drawn
 * translucent (alpha, not full colour): the ids stay legible over a per-parent
 * tint instead of fighting a saturated palette colour at full strength.
 */
inline void drawMatrixParentBands(QPainter& p, const QPointF& topLeft, double cellSize,
                                  const QList<int>& parentPerCell,
                                  const QList<QColor>& colourPerCell,
                                  double thickness = 5.0,
                                  const QColor& boundary = QColor(32, 32, 32))
{
    const int n = parentPerCell.size();
    if (n <= 0 || cellSize <= 0.0 || colourPerCell.size() != n) return;

    p.save();
    p.setPen(Qt::NoPen);
    const double x0 = topLeft.x();
    const double y0 = topLeft.y();
    const double span = n * cellSize;
    for (int i = 0; i < n; ++i) {
        const QColor& c = colourPerCell[i];
        if (!c.isValid()) continue;
        QColor band = c;
        band.setAlpha(170);     // ids paint on top of the strip; keep them legible
        p.setBrush(band);
        p.drawRect(QRectF(x0 + i * cellSize, y0 - thickness - 1.0, cellSize, thickness));
        p.drawRect(QRectF(x0 - thickness - 1.0, y0 + i * cellSize, thickness, cellSize));
    }
    QPen pen(boundary);
    pen.setWidth(0);
    pen.setCosmetic(true);      // one device pixel, exactly as the grid above
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    for (int i = 1; i < n; ++i) {
        if (parentPerCell[i] == parentPerCell[i - 1]) continue;
        const double x = x0 + i * cellSize;
        const double y = y0 + i * cellSize;
        p.drawLine(QPointF(x, y0 - thickness - 1.0), QPointF(x, y0 + span));
        p.drawLine(QPointF(x0 - thickness - 1.0, y), QPointF(x0 + span, y));
    }
    p.restore();
}

#endif // MATRIXGRID_H
