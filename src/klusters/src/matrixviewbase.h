/***************************************************************************
 *   matrixviewbase.h -- what the four curation matrices share.
 *
 *   The error, template, residual and drift matrices are four widgets over
 *   the same picture: an N x N grid of cells, one row and one column per
 *   cluster, with the ids along the top and the left, panned and zoomed by
 *   the same gestures and centred on a cluster on request.  This class holds
 *   that: the clusters, the pan / zoom state, the hit test, the follow, the
 *   id labels.  It is not a QObject and not a widget: a view derives from its
 *   widget class AND from this, and supplies what differs (where the grid
 *   sits in the widget, the display order, how to repaint, its signal).
 *
 *   What a view still has for itself: its data and its compute thread, the
 *   cells' colours, its bars and controls, what a click means.
 *
 *   Geometry: cell (row r, column c) of the display is drawn at
 *   effMatrixTopLeft() + (c, r) * effCellSize(), where effCellSize() is
 *   cellWidth * zoom and effMatrixTopLeft() is matrixTopLeft() moved by the
 *   pan (MatrixViewport).  A display position d shows matrix row
 *   displayToMatrix(d): the views can be sorted without their data moving.
 *
 *   Copyright (C) 2026 neurosuite-3 contributors
 *   SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXVIEWBASE_H
#define MATRIXVIEWBASE_H

#include <QColor>
#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRectF>

#include "matrixviewport.h"      // MatrixViewport: the pan / zoom state
#include "matrixnavigator.h"     // MatrixNavigator: the Ctrl+Left-drag pan

class QPainter;

class MatrixViewBase {
public:
    virtual ~MatrixViewBase() = default;

    /** Centre cluster @p id on the diagonal: now when the matrix shown holds it, else when
     *  the result on its way lands (and not at all if that one does not hold it either). */
    void followCluster(int id);

    static constexpr double zoomMin{0.5};    ///< zoom-out floor (a view may lower it: matrixZoomFloor)
    static constexpr double zoomMax{20.0};
    static constexpr double zoomStep{1.15};  ///< zoom factor of one wheel or key step
    static constexpr int    LABEL_MARGIN = 16;   ///< width of the strip of row ids

protected:
    // ── state ──────────────────────────────────────────────────────────────
    QList<int> clusterList;           ///< the clusters of the matrix shown, in matrix order
    bool dataReady = false;           ///< a result is shown
    int  cellWidth = 0;               ///< cell size in pixels at zoom 1
    MatrixViewport  vp_;              ///< pan and zoom
    MatrixNavigator nav_;             ///< the pan drag in progress
    int  followId_ = -1;              ///< cluster to centre at the next chance
    bool followPending_ = false;      ///< a followCluster() not yet applied

    // ── geometry ───────────────────────────────────────────────────────────
    double  effCellSize() const { return cellWidth * vp_.zoom; }
    QPointF effMatrixTopLeft() const { return vp_.effTopLeft(matrixTopLeft()); }
    /// The display column / row under a widget coordinate, -1 outside the grid.
    int cellAtX(int viewX) const;
    int cellAtY(int viewY) const;
    /// 0-based display position of cluster @p id on the diagonal, -1 if absent.
    int displayPositionOf(int id) const;

    // ── navigation ─────────────────────────────────────────────────────────
    /// Nothing of the grid on screen: back to the upper-left corner.  True when it moved.
    bool ensureMatrixInView();
    /// Centre followId_ when it is in the matrix; clears the pending request.
    void applyPendingFollow();
    void resetPanZoom();
    /// Zoom to @p newZoom keeping the point @p pivot of the widget where it is.
    void zoomAroundPoint(double newZoom, const QPointF& pivot);
    /** Take a zoom and pan as given (the state of another matrix linked to this one, or a
     *  saved one).  Does not announce it: a linked view would send it straight back. */
    void applyViewState(double newZoom, double panX, double panY);
    /** The floor that lets the whole grid fit the view: at zoom Z the grid spans
     *  n * cellWidth * Z pixels, and with many clusters cellWidth bottoms out, so the grid
     *  no longer fits at zoomMin.  Never above zoomMin: small grids are unaffected. */
    double zoomFloorToFitAll() const;

    // ── drawing ────────────────────────────────────────────────────────────
    /// The ids along the top and the left of the grid.
    void drawClusterIds(QPainter& painter);

    // ── what a view supplies ───────────────────────────────────────────────
    /// Where the grid starts in the widget at zoom 1 without pan.
    virtual QPoint matrixTopLeft() const = 0;
    /// The widget rectangle the grid is drawn in (the view less its bars).
    virtual QRectF matrixArea() const = 0;
    /// Display position -> matrix index, and back.
    virtual int displayToMatrix(int d) const = 0;
    virtual int matrixToDisplay(int m) const = 0;
    /// A result is on its way or held back: the rows may move when it lands.
    virtual bool matrixResultPending() const = 0;
    /// Paint again.
    virtual void matrixRepaint() = 0;
    /// Emit the view's viewChanged(zoom, panX, panY).
    virtual void matrixViewChanged() = 0;
    /// The lowest zoom allowed now (a view with many clusters lowers the floor to fit them).
    virtual double matrixZoomFloor() const { return zoomMin; }
    /// The zoom changed (before the repaint).
    virtual void matrixZoomed() {}
    virtual QColor matrixLabelColor() const = 0;
    /// Top of the strip of column ids (below a control bar, when the view has one above the grid).
    virtual int matrixLabelTop() const { return 0; }
};

#endif // MATRIXVIEWBASE_H
