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

class RadialMenu : public QWidget
{
    Q_OBJECT

public:
    explicit RadialMenu(QWidget* parent = nullptr);

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
    bool  updateHover(const QPointF& p);   ///< true when the hovered wedge changed
    QAction* hoveredAction() const;
    QAction* leafAt(const QPointF& p);
    void  activate(QAction* a);
    void  resetRings();

    QPointer<QMenu> root;
    QList<Ring> rings;
    QPointF centre;             ///< ring centre in widget coordinates
    int  maxDepth = 1;
    bool gestureArmed = false;  ///< opened during a press: a release may pick
    bool dragged = false;       ///< the cursor has left the hub since opening
    bool clickPressed = false;  ///< click mode: a press landed inside the widget
    QAction* lastHovered = nullptr;
    QHash<const QAction*, QString> labels;   ///< setLabel() overrides
    QSet<const QAction*> adjustable;         ///< setAdjustable() value wedges
    int wheelRemainder = 0;                  ///< sub-notch wheel delta (touchpads)
};

#endif // RADIALMENU_H
