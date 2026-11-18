/***************************************************************************
 * radialmenu.h — a hierarchical radial (pie / marking) context menu.
 *
 * Proof of concept.  QMenu is strictly linear, so this is a custom popup that
 * lays a QMenu tree out as concentric rings around the cursor:
 *
 *   - ring 0 holds the root menu's actions on the full circle, first item at
 *     12 o'clock, reading clockwise;
 *   - hovering a submenu wedge opens its children as the next ring out,
 *     centred on the parent wedge (widened to a minimum wedge angle, up to
 *     the full circle);
 *   - an open ring is never closed by a cursor that is still MOVING.  A child
 *     ring is wider than its parent wedge, so the straight path to most of its
 *     wedges crosses a neighbouring parent; closing on that crossing forces the
 *     hand to go radially out first and then around.  A move that would close a
 *     ring (onto a different parent wedge, back onto an ancestor, into the hub)
 *     is held until the cursor SETTLES there -- stays within a few pixels for a
 *     short dwell -- and dropped if it reaches the open ring first.  A press, a
 *     release or a wheel step acts on what is under the cursor at once;
 *   - the hub is a dead zone: releasing or clicking there cancels.
 *
 * Two ways to pick, as in Maya-style marking menus:
 *
 *   - press-drag-release: open on a button press, drag out through the rings,
 *     release on a leaf.  The direction is enough, so an experienced user
 *     never waits for the menu to draw;
 *   - click: a press-release without leaving the hub leaves the menu open, and
 *     a later click on a leaf triggers it.
 *
 * The data model is plain QMenu/QAction (menu->actions(), action->menu()), so
 * the radial menu reuses the very actions the menu bar, toolbars and the input
 * registry already drive: enabled/checked state, icons and shortcuts are the
 * live ones, and triggering a wedge is QAction::trigger().  Separators and
 * hidden actions are skipped.  The menu tree is NOT owned.
 *
 * Mode::Embedded makes it an ordinary child widget instead of a popup — a
 * preview (the Preferences wheel editor): hovering opens rings as usual, but
 * nothing is ever picked or adjusted, and leaving the widget falls back to the
 * path set with setOpenPath().
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef RADIALMENU_H
#define RADIALMENU_H

#include <QList>
#include <QHash>
#include <QSet>
#include <QPointer>
#include <QPointF>
#include <QWidget>

class QAction;
class QMenu;
class QTimer;

class RadialMenu : public QWidget
{
    Q_OBJECT

public:
    enum class Mode { Popup, Embedded };
    explicit RadialMenu(QWidget* parent = nullptr, Mode mode = Mode::Popup);

    /** At most @p rings rings are shown (clamped to 1..4); deeper groups still
     *  show their submenu marker but do not open.  Takes effect on setMenu(). */
    void setDepthLimit(int rings);

    /** The tree to show (not owned).  May be changed between popups. */
    void setMenu(QMenu* root);
    QMenu* menu() const;

    /** A shorter wedge label for @p action (the QAction's own text, shown in
     *  the menu bar, is left alone).  An empty @p text clears it. */
    void setLabel(const QAction* action, const QString& text);

    /** Make @p action a value wedge: turning the mouse wheel over it emits
     *  adjusted(), and a click or release on it never picks it (the menu stays
     *  open, so the value can be stepped repeatedly). */
    void setAdjustable(const QAction* action, bool on = true);

    /** Show the menu centred on @p globalPos.  When a mouse button is down at
     *  this moment the press-drag-release gesture is armed. */
    void popup(const QPoint& globalPos);

    /** Embedded mode: the rings shown while the cursor is not over the widget —
     *  each action in @p path is a group wedge to open, from the root ring out
     *  (actions not found, or not groups, end the path). */
    void setOpenPath(const QList<QAction*>& path);

Q_SIGNALS:
    /** A leaf was chosen.  Emitted just before the action is triggered. */
    void triggered(QAction* action);
    /** The wedge under the cursor changed (nullptr: none) — for a status bar. */
    void hovered(QAction* action);
    /** The wheel turned over an adjustable wedge: @p steps notches, positive
     *  for wheel-up / away from the user. */
    void adjusted(QAction* action, int steps);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    /** One visible ring: the actions of one menu laid out on an arc. */
    struct Ring {
        QList<QAction*> items;
        double base   = 90.0;   ///< centre angle of item 0 (degrees, CCW from 3 o'clock)
        double step   = 360.0;  ///< angle per wedge
        double span   = 360.0;  ///< total arc covered (360 = full circle)
        int    hovered = -1;    ///< item under the cursor, or -1
    };
    struct Hit { int ring = -1; int item = -1; };

    static QList<QAction*> visibleActions(const QMenu* m);
    static int  treeDepth(const QMenu* m, int level = 1);
    static QString plainText(const QAction* a);
    QString labelFor(const QAction* a) const;

    Ring  makeRing(const QMenu* m, double parentMid, double parentStep, int ringIndex) const;
    double innerRadius(int ringIndex) const;
    double outerRadius(int ringIndex) const;
    double angleAt(const QPointF& p) const;
    Hit   hitTest(const QPointF& p) const;
    /** Retarget the rings to @p p.  A retarget that would close an open ring is
     *  held until the cursor settles (see the header) unless @p immediate.  True when
     *  the hovered wedge changed. */
    bool  updateHover(const QPointF& p, bool immediate = false);
    bool  wouldCloseRing(const Hit& hit, double radius) const;
    void  dropHeldHover();
    QAction* hoveredAction() const;
    QAction* leafAt(const QPointF& p);
    void  activate(QAction* a);
    void  resetRings();
    void  applyOpenPath();

    QPointer<QMenu> root;
    QList<Ring> rings;
    QPointF centre;             ///< ring centre in widget coordinates
    int  maxDepth = 1;
    int  depthLimit = 4;        ///< setDepthLimit()
    Mode mode = Mode::Popup;
    QList<QPointer<QAction>> openPath;   ///< setOpenPath() (embedded)
    bool gestureArmed = false;  ///< opened during a press: a release may pick
    bool dragged = false;       ///< the cursor has left the hub since opening
    bool clickPressed = false;  ///< click mode: a press landed inside the widget
    QAction* lastHovered = nullptr;
    QHash<const QAction*, QString> labels;   ///< setLabel() overrides
    QSet<const QAction*> adjustable;         ///< setAdjustable() value wedges
    int wheelRemainder = 0;                  ///< sub-notch wheel delta (touchpads)
    QTimer* settleTimer = nullptr;           ///< fires when the cursor has settled on a held retarget
    bool    hoverHeld = false;               ///< a ring-closing retarget is waiting for the cursor to settle
    QPointF heldPos;                         ///< where the cursor is now (the retarget to apply)
    QPointF settleAnchor;                    ///< where the current dwell started
};

#endif // RADIALMENU_H
