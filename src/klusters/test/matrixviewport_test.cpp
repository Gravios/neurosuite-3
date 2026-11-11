// matrixviewport_test.cpp -- the pan/zoom viewport the four curation matrices share
// (matrixviewport.h): the follow mode and the out-of-view check.
//
//   * centerOnDiagonal(): the centre of the diagonal cell at display position d
//     lands at the centre of the matrix area, at any zoom and any base origin;
//     the zoom is untouched;
//   * gridOutOfView(): false while any part of the n x n grid overlaps the area,
//     true once it is wholly past an edge; an empty grid is never "out";
//   * bringBackIfLost(): a no-op (false) while something is on screen, and the
//     default pan (the upper-left corner, labels included) at the kept zoom
//     once the grid is wholly off screen;
//   * the follow lands the cell in view, so a follow is never followed by a
//     bring-back; swapForScope() keeps the follow pan with its scope.
#include "matrixviewport.h"

#include <cmath>
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", what);
    if (!ok) ++fails;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main()
{
    const QPoint  base(16 + 8, 18);              // LABEL_MARGIN + widthBorder, heightBorder
    const QRectF  area(0, 30, 600, 400);         // a drift-style area: a 30px bar on top
    const double  cell = 12.0;

    // centre on the diagonal at unit zoom
    {
        MatrixViewport vp;
        vp.centerOnDiagonal(10, cell * vp.zoom, base, area);
        const QPointF tl = vp.effTopLeft(base);
        const double cx = tl.x() + 10.5 * cell, cy = tl.y() + 10.5 * cell;
        check(near(cx, 300.0) && near(cy, 230.0), "centerOnDiagonal: cell 10's centre at the area's centre (zoom 1)");
        check(near(vp.zoom, 1.0), "centerOnDiagonal: the zoom is untouched");
    }
    // ... and at a zoom: the effective cell is what the caller passes
    {
        MatrixViewport vp;
        vp.zoom = 3.0;
        vp.centerOnDiagonal(0, cell * vp.zoom, base, area);
        const QPointF tl = vp.effTopLeft(base);
        check(near(tl.x() + 0.5 * 36.0, 300.0) && near(tl.y() + 0.5 * 36.0, 230.0),
              "centerOnDiagonal: cell 0 centred at zoom 3 (effective cell 36px)");
        check(near(vp.zoom, 3.0), "centerOnDiagonal at zoom 3: the zoom is kept");
    }
    // out-of-view: n = 20 cells of 12px = 240px grid from base + pan
    {
        MatrixViewport vp;
        check(!vp.gridOutOfView(20, cell, base, area), "gridOutOfView: default framing is on screen");
        vp.panX = -(base.x() + 240) + 1;          // right edge 1px inside the area
        check(!vp.gridOutOfView(20, cell, base, area), "gridOutOfView: a 1px sliver on screen is not out");
        vp.panX = -(base.x() + 240) - 1;          // wholly left of the area
        check(vp.gridOutOfView(20, cell, base, area), "gridOutOfView: wholly past the left edge is out");
        vp.panX = 0; vp.panY = -(base.y() + 240) + (30 - 1);   // wholly above the area's top (y=30)
        check(vp.gridOutOfView(20, cell, base, area), "gridOutOfView: wholly above the area (under the bar) is out");
        vp.panY = 0; vp.panX = 600 + 5;
        check(vp.gridOutOfView(20, cell, base, area), "gridOutOfView: wholly past the right edge is out");
        check(!vp.gridOutOfView(0, cell, base, area), "gridOutOfView: an empty grid is never out");
    }
    // bring back: a no-op while on screen, the default pan once lost, zoom kept
    {
        MatrixViewport vp;
        vp.zoom = 2.0; vp.panX = -50; vp.panY = 40;
        check(!vp.bringBackIfLost(20, cell * vp.zoom, base, area) && near(vp.panX, -50) && near(vp.panY, 40),
              "bringBackIfLost: on screen -> false, pan untouched");
        vp.panX = -2000;
        check(vp.bringBackIfLost(20, cell * vp.zoom, base, area) && near(vp.panX, 0) && near(vp.panY, 0),
              "bringBackIfLost: lost -> true, pan back to the upper-left corner");
        check(near(vp.zoom, 2.0), "bringBackIfLost: the zoom is kept");
        check(!vp.gridOutOfView(20, cell * vp.zoom, base, area), "bringBackIfLost: the grid is on screen afterwards");
    }
    // a follow is never undone by the check, and swaps with its scope
    {
        MatrixViewport vp;
        vp.panX = -5000;                                       // lost
        vp.centerOnDiagonal(1999, cell, base, area);           // a far cell of a 2000-cluster matrix
        check(!vp.gridOutOfView(2000, cell, base, area) && !vp.bringBackIfLost(2000, cell, base, area),
              "follow then check: the followed cell is on screen, nothing to bring back");
        const double px = vp.panX, py = vp.panY;
        vp.swapForScope(true);                                 // into the child-scoped matrix: fresh framing
        check(near(vp.panX, 0) && near(vp.panY, 0), "swapForScope(child): default framing the first time");
        vp.centerOnDiagonal(3, cell, base, area);
        vp.swapForScope(false);                                // back: the parent follow pan is restored
        check(near(vp.panX, px) && near(vp.panY, py), "swapForScope(parent): the parent matrix's follow pan comes back");
    }

    std::printf("matrixviewport_test: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
