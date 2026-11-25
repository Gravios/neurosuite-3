// matrixviewbase_test.cpp -- what the four curation matrices share (matrixviewbase.h), on a
// view that is not a widget.
//
// Checked: the cell under a point, with pan and zoom, and outside the grid; the display
// position of a cluster under a display order; the follow -- at once when the cluster is in
// the matrix, deferred while a result is pending, lapsing when the cluster is not there;
// zooming about a point keeps that point's cell; the zoom limits, and the floor lowered for a
// grid too large to fit; a state taken from another view is not announced; reset.
// No session, no QApplication, own main, run via ctest.

#include "matrixviewbase.h"

#include <cmath>
#include <cstdio>
#include <string>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what)
{
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

// A matrix view without a widget: a grid at (16, 20) in a 400 x 300 area.
struct View : MatrixViewBase {
    QList<int> order;                 // display position -> matrix index (empty: identity)
    bool pending = false, fitFloor = false;
    int repaints = 0, announced = 0, zoomed = 0;
    QSizeF size{400, 300};

    View(const QList<int>& ids, int cell) { clusterList = ids; cellWidth = cell; dataReady = true; }
    QPoint matrixTopLeft() const override { return QPoint(LABEL_MARGIN, 20); }
    QRectF matrixArea() const override { return QRectF(QPointF(0, 0), size); }
    int displayToMatrix(int d) const override { return order.isEmpty() ? d : order.at(d); }
    int matrixToDisplay(int m) const override { return order.isEmpty() ? m : static_cast<int>(order.indexOf(m)); }
    bool matrixResultPending() const override { return pending; }
    void matrixRepaint() override { ++repaints; }
    void matrixViewChanged() override { ++announced; }
    double matrixZoomFloor() const override { return fitFloor ? zoomFloorToFitAll() : zoomMin; }
    void matrixZoomed() override { ++zoomed; }
    QColor matrixLabelColor() const override { return Qt::white; }
    // the protected parts under test
    using MatrixViewBase::cellAtX; using MatrixViewBase::cellAtY; using MatrixViewBase::displayPositionOf;
    using MatrixViewBase::zoomAroundPoint; using MatrixViewBase::resetPanZoom; using MatrixViewBase::applyViewState;
    using MatrixViewBase::ensureMatrixInView; using MatrixViewBase::applyPendingFollow; using MatrixViewBase::vp_;
    using MatrixViewBase::followPending_; using MatrixViewBase::dataReady; using MatrixViewBase::clusterList;
    using MatrixViewBase::zoomFloorToFitAll; using MatrixViewBase::effCellSize; using MatrixViewBase::effMatrixTopLeft;
};

int main()
{
    {   // the cell under a point
        View v({ 2, 3, 5, 7 }, 50);
        check(v.cellAtX(16) == 0 && v.cellAtX(65) == 0 && v.cellAtX(66) == 1 && v.cellAtY(20) == 0 && v.cellAtY(169) == 2,
              "cells are 50 px from the grid's corner");
        check(v.cellAtX(15) == -1 && v.cellAtY(19) == -1 && v.cellAtX(216) == -1 && v.cellAtY(220) == -1,
              "outside the grid there is no cell");
        v.zoomAroundPoint(2.0, QPointF(16, 20));                   // about the grid's corner: it stays
        check(v.cellAtX(16) == 0 && v.cellAtX(115) == 0 && v.cellAtX(116) == 1, "at zoom 2 the cells are 100 px");
        check(v.repaints == 1 && v.announced == 1 && v.zoomed == 1, "a zoom repaints, announces and tells the view once");
        View e({}, 50);
        check(e.cellAtX(40) == -1, "an empty matrix has no cell");
        View z({ 2, 3 }, 0);
        check(z.cellAtX(40) == -1, "a zero cell size has no cell");
    }
    {   // zooming about a point keeps what is under it
        View v({ 2, 3, 5, 7, 9, 11 }, 40);
        const QPointF p(150, 130);
        const int cx = v.cellAtX(150), cy = v.cellAtY(130);
        const double fx = (p.x() - v.effMatrixTopLeft().x()) / v.effCellSize();
        v.zoomAroundPoint(3.3, p);
        check(v.cellAtX(150) == cx && v.cellAtY(130) == cy
              && std::fabs((p.x() - v.effMatrixTopLeft().x()) / v.effCellSize() - fx) < 1e-9,
              "the point zoomed about stays on the same place of the same cell");
        v.zoomAroundPoint(1000.0, p);
        check(v.vp_.zoom == MatrixViewBase::zoomMax, "the zoom stops at zoomMax");
        v.zoomAroundPoint(0.001, p);
        check(v.vp_.zoom == MatrixViewBase::zoomMin, "... and at zoomMin");
        v.resetPanZoom();
        check(v.vp_.zoom == 1.0 && v.vp_.panX == 0.0 && v.vp_.panY == 0.0, "reset: zoom 1, no pan");
    }
    {   // a grid too large to fit: the floor comes down, and only then
        QList<int> many;
        for (int i = 0; i < 400; ++i) many.append(i + 2);
        View v(many, 4);
        check(v.zoomFloorToFitAll() < MatrixViewBase::zoomMin, "400 clusters at 4 px do not fit 400 x 300 at zoomMin: a lower floor");
        const double fit = v.zoomFloorToFitAll();
        check(std::fabs(400 * 4 * fit - (300 - 14 - 10)) < 1e-6, "... the one at which the grid spans the room there is");
        v.fitFloor = true;
        v.zoomAroundPoint(0.0001, QPointF(16, 20));
        check(v.vp_.zoom == fit, "a view that uses it can zoom out to it");
        View s({ 2, 3, 5 }, 50);
        check(s.zoomFloorToFitAll() == MatrixViewBase::zoomMin, "a small grid keeps zoomMin");
        s.size = QSizeF(10, 10);
        check(s.zoomFloorToFitAll() == MatrixViewBase::zoomMin, "no room at all: zoomMin");
    }
    {   // the display order
        View v({ 2, 3, 5, 7 }, 50);
        check(v.displayPositionOf(5) == 2 && v.displayPositionOf(4) == -1, "identity order: the matrix index; -1 if absent");
        v.order = { 3, 1, 0, 2 };
        check(v.displayPositionOf(7) == 0 && v.displayPositionOf(2) == 2 && v.displayPositionOf(5) == 3,
              "under a display order: where the order shows it");
    }
    {   // the follow
        View v({ 2, 3, 5, 7, 9, 11, 13, 15, 17, 19 }, 50);
        v.followCluster(15);
        const double cell = v.effCellSize();
        const QPointF c = v.effMatrixTopLeft() + QPointF(7.5 * cell, 7.5 * cell);
        check(!v.followPending_ && std::fabs(c.x() - 200) < 1e-9 && std::fabs(c.y() - 150) < 1e-9,
              "a cluster in the matrix is centred at once: its diagonal cell in the middle of the area");
        check(v.repaints == 1 && v.announced == 0, "... repainted, not announced");
        const double px = v.vp_.panX;
        v.followCluster(4);
        check(v.followPending_ && v.vp_.panX == px, "a cluster that is not in the matrix: the request waits");
        v.applyPendingFollow();
        check(!v.followPending_ && v.vp_.panX == px, "... and lapses at the next result without it");
        v.pending = true;
        v.followCluster(3);
        check(v.followPending_ && v.vp_.panX == px, "while a result is pending nothing moves");
        v.pending = false;
        v.applyPendingFollow();
        check(!v.followPending_ && v.vp_.panX != px, "... and the cluster is centred when it has landed");
        v.followCluster(-1);
        check(!v.followPending_, "following nothing asks for nothing");
        v.dataReady = false;
        v.followCluster(3);
        check(v.followPending_, "without a result the request waits");
    }
    {   // a state from another view; a grid lost off screen
        View v({ 2, 3, 5, 7 }, 50);
        v.applyViewState(3.0, -40.0, 25.0);
        check(v.vp_.zoom == 3.0 && v.vp_.panX == -40.0 && v.vp_.panY == 25.0 && v.repaints == 1 && v.announced == 0 && v.zoomed == 1,
              "a state taken from elsewhere is applied and repainted, not announced");
        v.applyViewState(100.0, 0.0, 0.0);
        check(v.vp_.zoom == MatrixViewBase::zoomMax, "... within the zoom limits");
        v.applyViewState(1.0, -5000.0, -5000.0);
        check(v.ensureMatrixInView() && v.vp_.panX == 0.0 && v.vp_.panY == 0.0, "a grid panned out of sight comes back to the corner");
        check(!v.ensureMatrixInView(), "... and a grid in sight is left alone");
    }
    std::printf("matrixviewbase: %d checks, %d failed\n", g_ran, g_fail);
    return g_fail ? 1 : 0;
}
