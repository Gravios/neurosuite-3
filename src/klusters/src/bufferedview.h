/***************************************************************************
 * bufferedview.h — shared double-buffer paint lifecycle for the classic
 * Klusters views (Tier 2 of the paint refactor; see
 * claude/paint-refactor-plan.md).
 *
 * The cluster, waveform, error-matrix, correlation and trace views all drew
 * into an off-screen pixmap through the same hand-copied skeleton: resize the
 * buffer, set the world window, fill + draw on REDRAW, incrementally repaint on
 * UPDATE, reset the level to REFRESH, blit.  BufferedView owns that skeleton
 * once as a template method (paintEvent) and calls a handful of hooks the view
 * overrides instead of re-implementing the whole paintEvent:
 *
 *   paintBuffer()            — the CACHED layer (scatter / waveforms): the view
 *                              fills the background and draws its content on
 *                              REDRAW, and repaints just what changed on UPDATE.
 *   paintBufferDeviceLayer() — device-space content baked into the buffer after
 *                              the world transform is reset (axis legends, ids).
 *   paintWidgetOverlays()    — LIVE overlays drawn straight onto the widget after
 *                              the buffer is blitted, so they reflect the current
 *                              state on every paint (the lineage overlay, HUDs).
 *   paintDirect()            — a view that paints itself wholesale and skips the
 *                              buffer (the t-SNE / oblique embedding) returns true.
 *   renderReady / beforeRedraw / setupWorldTransform / afterPaint — per-view knobs.
 *
 * Non-buffered frames do not derive from BufferedView; the newer matrices
 * (Residual / Drift / Template) are plain QWidgets and keep their own
 * MatrixViewport-based buffering.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef BUFFEREDVIEW_H
#define BUFFEREDVIEW_H

#include "baseframe.h"
#include <QPixmap>
#include <QPainter>

class BufferedView : public BaseFrame {
public:
    /** Same signature as BaseFrame, forwarded verbatim: BufferedView is a
     *  transparent intermediate, so ViewWidget and TraceView call this in place
     *  of BaseFrame in their initialiser lists with the identical arguments. */
    BufferedView(int Xborder, int Yborder, QWidget* parent = nullptr,
                 const QString& name = QString(), const QColor& backgroundColor = Qt::black,
                 int minSize = 500, int maxSize = 4000,
                 int windowTopLeft = -500, int windowBottomRight = 1001, int border = 0)
        : BaseFrame(Xborder, Yborder, parent, name, backgroundColor,
                    minSize, maxSize, windowTopLeft, windowBottomRight, border) {}

protected:
    /** Off-screen buffer for the cached layer — moved here from the per-view
     *  bases (ViewWidget, TraceView) so the lifecycle has a single owner. */
    QPixmap doublebuffer;

    // ── hooks (the defaults suit the simplest buffered view) ─────────────────
    /** Gate the whole buffered repaint (e.g. a view not yet holding data). */
    virtual bool renderReady() const { return true; }
    /** Return true after painting the widget wholesale, to bypass the buffer
     *  entirely (an alternate presentation with its own coordinate space). */
    virtual bool paintDirect(QPainter& /*widget*/) { return false; }
    /** Run just before a REDRAW samples the world window (e.g. autoscale). */
    virtual void beforeRedraw() {}
    /** Install the world→device transform on the buffer painter. */
    virtual void setupWorldTransform(QPainter& buf);
    /** Draw the cached layer: fill the background and paint content on REDRAW;
     *  repaint just the changed items on UPDATE. */
    virtual void paintBuffer(QPainter& /*buf*/, DrawContentsMode /*level*/) {}
    /** Device-space content baked into the buffer, after the transform reset. */
    virtual void paintBufferDeviceLayer(QPainter& /*buf*/) {}
    /** Live overlays drawn onto the widget after the buffer is blitted. */
    virtual void paintWidgetOverlays(QPainter& /*widget*/) {}
    /** Runs at the very end of every paint (e.g. restore the tool cursor). */
    virtual void afterPaint(QPainter& /*widget*/) {}

    /** Resize the buffer to the current viewport, preserving what was drawn. */
    void ensureDoubleBuffer();

    /** The shared skeleton.  Views implement the hooks above, not paintEvent. */
    void paintEvent(QPaintEvent*) override;
};

#endif // BUFFEREDVIEW_H
