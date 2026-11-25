// matrixviewbase.cpp -- see matrixviewbase.h.

#include "matrixviewbase.h"

#include "clusterlabel.h"

#include <QFont>
#include <QPainter>

#include <algorithm>
#include <cmath>

int MatrixViewBase::cellAtX(int viewX) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double mx = effMatrixTopLeft().x();
    const int ci = static_cast<int>(std::floor((viewX - mx) / eff));
    return (ci >= 0 && ci < clusterList.size()) ? ci : -1;
}

int MatrixViewBase::cellAtY(int viewY) const
{
    const double eff = effCellSize();
    if (eff <= 0.0) return -1;
    const double my = effMatrixTopLeft().y();
    const int ri = static_cast<int>(std::floor((viewY - my) / eff));
    return (ri >= 0 && ri < clusterList.size()) ? ri : -1;
}

int MatrixViewBase::displayPositionOf(int id) const
{
    const int m = clusterList.indexOf(id);
    return m < 0 ? -1 : matrixToDisplay(m);
}

bool MatrixViewBase::ensureMatrixInView()
{
    if (!dataReady) return false;
    if (!vp_.bringBackIfLost(clusterList.size(), effCellSize(), matrixTopLeft(), matrixArea()))
        return false;
    matrixRepaint();
    return true;
}

void MatrixViewBase::followCluster(int id)
{
    followId_ = id;
    followPending_ = id >= 0;
    // A result on its way (or held back) will move the rows: centre on it when
    // it lands.  Otherwise the matrix shown is the one the selection refers to
    // -- unless the cluster is not in it (a parent picked while the child matrix
    // is up): then the request waits for the next result, which may be the
    // parent matrix coming back, and lapses with it if it is not.
    if (matrixResultPending()) return;
    if (dataReady && displayPositionOf(id) >= 0) applyPendingFollow();
}

void MatrixViewBase::applyPendingFollow()
{
    if (!followPending_ || !dataReady) return;
    followPending_ = false;
    const int d = displayPositionOf(followId_);
    if (d < 0) return;                 // not in this matrix (another scope): lapses
    vp_.centerOnDiagonal(d, effCellSize(), matrixTopLeft(), matrixArea());
    matrixRepaint();
}

void MatrixViewBase::resetPanZoom()
{
    vp_.reset();
    matrixRepaint();
    matrixViewChanged();
}

void MatrixViewBase::zoomAroundPoint(double newZoom, const QPointF& pivot)
{
    vp_.zoomAround(newZoom, pivot, matrixTopLeft(), matrixZoomFloor(), zoomMax);
    matrixZoomed();
    matrixRepaint();
    matrixViewChanged();
}

void MatrixViewBase::applyViewState(double newZoom, double panX, double panY)
{
    vp_.setState(newZoom, panX, panY, matrixZoomFloor(), zoomMax);
    matrixZoomed();
    matrixRepaint();
}

double MatrixViewBase::zoomFloorToFitAll() const
{
    const int n = clusterList.size();
    if (n <= 0 || cellWidth <= 0) return zoomMin;
    const QRectF area = matrixArea();
    const int matH   = std::max(static_cast<int>(area.height()), 1);
    const int availW = static_cast<int>(area.width()) - LABEL_MARGIN - 10;
    const int availH = matH - 14 - 10;
    const int avail  = std::min(availW, availH);
    if (avail <= 0) return zoomMin;
    const double fitAll = static_cast<double>(avail)
                        / (static_cast<double>(n) * cellWidth);
    return std::min(zoomMin, fitAll);
}

void MatrixViewBase::drawClusterIds(QPainter& p)
{
    const int n      = clusterList.size();
    const QPoint  base = matrixTopLeft();
    const QPointF oriF = effMatrixTopLeft();
    const double  eff  = effCellSize();
    const int     w    = std::max(1, static_cast<int>(std::round(eff)));
    const int fontSize = std::max(5, std::min(14,
                            static_cast<int>(std::round(eff / 5.0))));
    const int top = matrixLabelTop();
    QFont f("Helvetica", fontSize);
    p.setFont(f);
    p.setPen(matrixLabelColor());

    // Column ids: the strip above the grid, x following the columns.
    for (int col = 0; col < n; ++col) {
        const int px = static_cast<int>(std::round(oriF.x() + col * eff));
        p.drawText(QRect(px, top, w, base.y() - top),
                   Qt::AlignHCenter | Qt::AlignBottom,
                   clusterLabel(clusterList[displayToMatrix(col)]));
    }
    // Row ids: the strip left of the grid, y following the rows.
    for (int row = 0; row < n; ++row) {
        const int py = static_cast<int>(std::round(oriF.y() + row * eff));
        p.drawText(QRect(0, py, LABEL_MARGIN - 2, w),
                   Qt::AlignRight | Qt::AlignVCenter,
                   clusterLabel(clusterList[displayToMatrix(row)]));
    }
}
