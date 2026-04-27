/***************************************************************************
 * probemakerviews.cpp
 *
 * QGraphicsView subclass implementations.  Behavioural notes:
 *
 *  - Both views use AnchorUnderMouse so wheel-zoom keeps the cursor
 *    over the same scene point.
 *  - Both views set RubberBandDrag so left-drag in empty space draws
 *    a selection rectangle (matches QGraphicsScene's default in
 *    QGraphicsView::RubberBandDrag mode).
 *  - The physical view supports middle-button pan; the logical view
 *    doesn't (vertical scrolling handles its needs).
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "probemakerviews.h"

#include <QApplication>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>

namespace probemaker {

// ═══════════════════════════════════════════════════════════════════════════
// ProbeLogicalView
// ═══════════════════════════════════════════════════════════════════════════

ProbeLogicalView::ProbeLogicalView(QWidget* parent)
    : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing,        true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setBackgroundBrush(QColor(0x0a, 0x0f, 0x18));
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
}

void ProbeLogicalView::wheelEvent(QWheelEvent* e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        // Ctrl+wheel: zoom around cursor.  Match the Pipeline
        // Designer's wheel sensitivity (1.15× per notch).
        const qreal factor = (e->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
        scale(factor, factor);
        e->accept();
        return;
    }
    QGraphicsView::wheelEvent(e);
}

// ═══════════════════════════════════════════════════════════════════════════
// ProbePhysicalView
// ═══════════════════════════════════════════════════════════════════════════

ProbePhysicalView::ProbePhysicalView(QWidget* parent)
    : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing,        true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setBackgroundBrush(QColor(0x12, 0x18, 0x24));
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    // µm coords go 0..1500 typically; Y inverts so positive Y points
    // toward the tip (down in screen coords).  Match the YAML schema
    // convention (y = depth from tip = 0 at top of substrate).
    // We don't flip the transform; the scene populator places shanks
    // with the head at top.
}

void ProbePhysicalView::fitAll()
{
    if (scene() && !scene()->itemsBoundingRect().isEmpty()) {
        fitInView(scene()->itemsBoundingRect().marginsAdded(QMarginsF(40, 40, 40, 40)),
                  Qt::KeepAspectRatio);
    }
}

void ProbePhysicalView::wheelEvent(QWheelEvent* e)
{
    // Bare wheel zooms — no modifier required.  Holding Ctrl still works
    // (it has the same effect) for compatibility with the old binding
    // and with users who reflexively hold Ctrl when zooming.
    const qreal factor = (e->angleDelta().y() > 0) ? 1.15 : 1.0 / 1.15;
    scale(factor, factor);
    emit userInteracted();
    e->accept();
}

void ProbePhysicalView::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPan = e->pos();
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        emit userInteracted();
        e->accept();
        return;
    }
    QGraphicsView::mousePressEvent(e);
}

void ProbePhysicalView::mouseMoveEvent(QMouseEvent* e)
{
    if (m_panning) {
        const QPoint d = e->pos() - m_lastPan;
        m_lastPan = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        e->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void ProbePhysicalView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        QApplication::restoreOverrideCursor();
        e->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

}  // namespace probemaker
