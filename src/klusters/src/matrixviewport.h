/***************************************************************************
 * matrixviewport.h — pan/zoom state + per-scope view swap shared by the four
 * curation matrices (error / template / residual / drift).
 *
 * Those views share no render base (matrixgrid.h explains why), so each had its
 * own copy of the SAME pan/zoom state (panX/panY/zoom), the zoom-around-pivot
 * math, the cross-connect push, and the per-scope save/restore — the duplication
 * claude/template-curation-audit.md calls D1/S5.  This struct (recommendation 4)
 * holds that shared state and math in one place, by COMPOSITION: each view embeds
 * one and keeps what genuinely differs — its grid origin matrixTopLeft(), its zoom
 * floor (the adaptive effZoomMin() for the error/template matrices, the plain
 * zoomMin for the residual/drift matrices), its colour map, and its mouse handling
 * — passing them in where the shared math needs them.
 *
 * Qt-light (QPoint/QPointF only); no document or widget dependency, so it is
 * trivially unit-testable and adds nothing to the views' include cost.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXVIEWPORT_H
#define MATRIXVIEWPORT_H

#include <algorithm>
#include <QPoint>
#include <QPointF>

/** Shared pan/zoom viewport for the four curation matrices.  See the file header. */
struct MatrixViewport {
    double panX = 0.0, panY = 0.0, zoom = 1.0;

    /** The effective (pan-translated) grid top-left, given the view's origin. */
    QPointF effTopLeft(QPoint base) const { return QPointF(base.x() + panX, base.y() + panY); }

    /** Zoom to `newZoom` (clamped to [minZoom, maxZoom]) keeping the matrix point
     *  under `pivot` fixed.  `base` is the view's matrixTopLeft(); `minZoom` is the
     *  view's floor — effZoomMin() for the error/template matrices, the plain
     *  zoomMin for the residual/drift matrices.  The view repaints / emits after.
     *  (Algebraically identical to both historical forms: panX += (pivot−base−panX)
     *  ·(1−r) and the matrix-space-point form differ only in spelling; the cellWidth
     *  cancels in r = newZoom/zoom.)  The zoom>0 guard is unreachable in practice —
     *  zoom starts at 1 and is clamped to minZoom>0 on every change — and only
     *  avoids a divide-by-zero on the pan update. */
    void zoomAround(double newZoom, const QPointF& pivot, QPoint base,
                    double minZoom, double maxZoom) {
        newZoom = std::clamp(newZoom, minZoom, maxZoom);
        if (zoom > 0.0) {
            const double ratio = newZoom / zoom;
            panX += (pivot.x() - base.x() - panX) * (1.0 - ratio);
            panY += (pivot.y() - base.y() - panY) * (1.0 - ratio);
        }
        zoom = newZoom;
    }

    /** Full (zoom+pan) push from a cross-connected matrix: clamp the zoom to
     *  [minZoom, maxZoom], take the pan verbatim (the layouts match at equal size). */
    void setState(double newZoom, double px, double py, double minZoom, double maxZoom) {
        zoom = std::clamp(newZoom, minZoom, maxZoom);
        panX = px;
        panY = py;
    }

    /** Back to the default framing (no pan, unit zoom). */
    void reset() { panX = panY = 0.0; zoom = 1.0; }

    // ── per-scope saved view (parent matrix vs child-scoped matrix) ─────────
    // The parent and the child-scoped matrix are different matrices (thousands of
    // clusters vs a handful), so a zoom framed in one is meaningless in the other.
    // Each scope keeps its own view, swapped when the scope changes.
    struct State { double panX = 0.0, panY = 0.0, zoom = 1.0; bool valid = false; };
    State parentScope, childScope;
    bool  lastScopeActive = false;

    /** Stash the current pan/zoom under the scope being left and restore the one
     *  being entered (default framing the first time a scope is seen).  Identical
     *  logic in all four matrices; called from each view's paint path. */
    void swapForScope(bool scopeActive) {
        if (scopeActive == lastScopeActive) return;
        State& leaving  = lastScopeActive ? childScope : parentScope;
        State& entering = scopeActive     ? childScope : parentScope;
        leaving.panX = panX; leaving.panY = panY; leaving.zoom = zoom; leaving.valid = true;
        if (entering.valid) { panX = entering.panX; panY = entering.panY; zoom = entering.zoom; }
        else                { panX = 0.0; panY = 0.0; zoom = 1.0; }
        lastScopeActive = scopeActive;
    }
};

#endif // MATRIXVIEWPORT_H
