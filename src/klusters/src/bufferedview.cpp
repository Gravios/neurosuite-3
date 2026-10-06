/***************************************************************************
 * bufferedview.cpp — the shared double-buffer paint skeleton.
 * See bufferedview.h and claude/paint-refactor-plan.md (Tier 2).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "bufferedview.h"

void BufferedView::ensureDoubleBuffer(){
    // Track the widget's content rectangle and keep the buffer the same size,
    // preserving the old contents on a resize (so an UPDATE paint over the old
    // buffer stays valid).  Sized exactly to the viewport — no padding — so the
    // size-equality check below actually skips the realloc when nothing changed.
    viewport = contentsRect();
    if (doublebuffer.size() == viewport.size())
        return;
    if (!doublebuffer.isNull()) {
        QPixmap tmp(viewport.size());
        tmp.fill(Qt::white);
        QPainter p(&tmp);
        p.drawPixmap(0, 0, doublebuffer);
        p.end();
        doublebuffer = tmp;
    } else {
        doublebuffer = QPixmap(viewport.size());
    }
}

void BufferedView::setupWorldTransform(QPainter& buf){
    // The historical "-1" is kept verbatim: Qt's setWindow treats the rect's
    // width/height differently than the rest of this code uses QRect, so the
    // views have always shaved one off here.
    const QRect r(static_cast<QRect>(window));
    buf.setWindow(r.left(), r.top(), r.width() - 1, r.height() - 1);
    buf.setViewport(viewport);
}

void BufferedView::paintEvent(QPaintEvent*){
    QPainter widget(this);

    // A view with its own coordinate space (the embedding) paints the widget
    // itself and tells us to skip the buffered path entirely.
    if (paintDirect(widget)) {
        afterPaint(widget);
        return;
    }

    if ((drawContentsMode == UPDATE || drawContentsMode == REDRAW) && renderReady()) {
        ensureDoubleBuffer();
        if (drawContentsMode == REDRAW)
            beforeRedraw();                 // e.g. autoscale, before the window is sampled

        QPainter buf(&doublebuffer);
        setupWorldTransform(buf);
        paintBuffer(buf, drawContentsMode); // cached layer: fill + content (REDRAW) / incremental (UPDATE)
        buf.resetTransform();
        paintBufferDeviceLayer(buf);        // device-space content baked into the buffer
        buf.end();

        drawContentsMode = REFRESH;         // consume: the one legitimate lowering of the level
    }
    // REFRESH (or a skipped guard) reuses the buffer as-is.
    widget.drawPixmap(0, 0, doublebuffer);
    paintWidgetOverlays(widget);            // live overlays, every paint, on top of the blit
    afterPaint(widget);
}
