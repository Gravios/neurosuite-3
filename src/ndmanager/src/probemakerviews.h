/***************************************************************************
 * probemakerviews.h
 *
 * QGraphicsView subclasses for the Probe Maker page.  Both share the
 * same QGraphicsItem subclasses (ConnectorItem / ShankItem /
 * ChannelItem) but lay them out differently: the logical view places
 * items in a tree, the physical view places them at their actual
 * micron coordinates.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#pragma once

#include <QGraphicsView>

class QWheelEvent;

namespace probemaker {

/**
 * @brief Logical-view canvas — connector at the top, shanks in a row
 *        underneath, channel pills under each shank.  Layout is
 *        recomputed after every structural change; users can't drag
 *        items in this view (drag-position has no semantic meaning).
 */
class ProbeLogicalView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ProbeLogicalView(QWidget* parent = nullptr);

protected:
    void wheelEvent(QWheelEvent* e) override;
};

/**
 * @brief Physical-view canvas — connector ribbon header, shanks
 *        positioned in connector coordinates, channel pads parented
 *        to their shank at micron offsets.  Items are draggable
 *        (drag a shank to reposition it on the connector; drag a pad
 *        to reposition it on its shank).
 *
 *        Mouse: wheel zooms (no modifier required); middle-drag pans;
 *        left-drag selects.  Ctrl+wheel also zooms for compatibility
 *        with the old binding.
 */
class ProbePhysicalView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ProbePhysicalView(QWidget* parent = nullptr);

    /** Reset the view to fit the entire scene with a 10% margin.
     *  Calling this clears the userInteracted flag (which controls
     *  whether the page rebuilds auto-fit) so the next rebuild also
     *  re-fits, giving the user a clean reset. */
    void fitAll();

signals:
    /** Emitted the first time the user zooms or pans, signalling that
     *  the page should stop auto-fitting on every scene rebuild and
     *  preserve the user's current view transform instead. */
    void userInteracted();

protected:
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    bool   m_panning   = false;
    QPoint m_lastPan;
};

}  // namespace probemaker
