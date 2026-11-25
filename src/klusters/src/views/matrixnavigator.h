/***************************************************************************
 * matrixnavigator.h — Ctrl+Left-drag pan state machine shared by the four
 * curation matrices (error / template / residual / drift).
 *
 * Companion to matrixviewport.h.  MatrixViewport holds the persistent pan/zoom
 * STATE + math; this holds the TRANSIENT drag bookkeeping that each view's
 * mouse handlers re-implemented identically — the press anchor, the viewport's
 * pan at press, the "armed but not yet past the threshold" vs "actively panning"
 * flags, and the delta→pan update.  (template-curation-audit.md D1/S5 moved the
 * state into MatrixViewport by COMPOSITION; this finishes the job for the drag
 * glue, same pattern: each view embeds one and keeps what genuinely differs —
 * when it arms, its cursor, its suppress-overlay side effects, and its swallow-
 * vs-select rule on release.)
 *
 * Deliberately NOT folded into MatrixViewport: these fields are transient (a
 * drag in progress), whereas MatrixViewport's panX/panY/zoom are persisted and
 * swapped per scope — mixing the two would have swapForScope() save drag state.
 *
 * Qt-light (QPoint only); operates on a MatrixViewport by reference, so it is
 * trivially unit-testable and adds nothing to the views' include cost.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef MATRIXNAVIGATOR_H
#define MATRIXNAVIGATOR_H

#include <QPoint>
#include "matrixviewport.h"

/** Transient Ctrl+Left-drag pan state for a curation matrix.  See the file header. */
struct MatrixNavigator {
    QPoint anchorPx;              // cursor position where the drag was armed
    double anchorPanX = 0.0;      // MatrixViewport.panX at arm time
    double anchorPanY = 0.0;      // MatrixViewport.panY at arm time
    bool   armed   = false;       // a Ctrl+Left press is being tracked
    bool   panning = false;       // the drag crossed the threshold → actively panning

    /** Arm a pan at the press position, anchoring on the viewport's current pan.
     *  Panning does not engage until drag() crosses the threshold, so a quick
     *  Ctrl-click still reaches the caller's select-on-release path. */
    void begin(const QPoint& pressPos, const MatrixViewport& vp) {
        anchorPx   = pressPos;
        anchorPanX = vp.panX;
        anchorPanY = vp.panY;
        armed      = true;
        panning    = false;
    }

    /** Continue a drag.  Once the move passes `threshold` (Manhattan px) the pan
     *  engages; while engaged, translate the viewport by the drag delta.  Returns
     *  true iff the viewport was moved (panning engaged), so the caller repaints
     *  and emits its viewChanged — a no-op below the threshold, exactly as the
     *  four hand-rolled handlers did. */
    bool drag(const QPoint& movePos, MatrixViewport& vp, int threshold) {
        const QPoint d = movePos - anchorPx;
        if (!panning && d.manhattanLength() >= threshold)
            panning = true;
        if (panning) {
            vp.panX = anchorPanX + d.x();
            vp.panY = anchorPanY + d.y();
            return true;
        }
        return false;
    }

    bool isArmed()   const { return armed; }
    bool isPanning() const { return panning; }

    /** End the gesture: return how far (Manhattan px) the cursor moved since
     *  begin(), and clear the state.  The caller decides, from that distance or
     *  isPanning(), whether the release was navigation (swallow) or a click
     *  (select) — the one piece of policy that genuinely differs between views. */
    int end(const QPoint& releasePos) {
        const int moved = (releasePos - anchorPx).manhattanLength();
        armed   = false;
        panning = false;
        return moved;
    }
};

#endif // MATRIXNAVIGATOR_H
