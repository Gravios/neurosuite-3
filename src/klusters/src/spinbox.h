/***************************************************************************
                          spinbox.h  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
    copyright            : (C) 2013 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef SPINBOX_H
#define SPINBOX_H

// include files for Qt
#include <QSpinBox>

class QKeyEvent;

/**
 * @brief QSpinBox subclass with a strict, documented key-routing policy.
 *
 * In Klusters, the toolbar spinboxes (dimensionX, dimensionY, start, etc.)
 * are nested inside the QMainWindow.  A naive QSpinBox claims every
 * keystroke while focused, blocking the application's letter-key
 * shortcuts (G = group, R = renumber, Shift+L = realign, etc.) — a
 * frustrating UX where the user has to click outside the spinbox just
 * to issue a curation command.
 *
 * The policy implemented here is:
 *
 *   The spinbox owns ONLY the keys it genuinely needs:
 *     - digit keys 0..9 (numeric input)
 *     - +, -, ., ,                            (numeric input)
 *     - Up, Down                              (step value)
 *     - Backspace, Delete, Home, End          (text editing)
 *     - Left, Right                           (cursor movement)
 *     - Enter, Return                         (commit)
 *     - Tab, Backtab                          (focus traversal)
 *     - Ctrl+A, Ctrl+C, Ctrl+V, Ctrl+X, Ctrl+Z (line-edit standard)
 *     - bare modifier keys (Shift, Ctrl, …)
 *
 *   Everything else propagates so KlustersApp shortcuts fire normally.
 *   This means letter keys (G, R, U, Z), letter-with-shift (Shift+R,
 *   Shift+W, Shift+L), letter-with-ctrl (other than the standard line-
 *   edit shortcuts above), F-keys, Page keys, etc. all reach the
 *   application even while a spinbox is being edited.
 *
 * The decision is made in two places:
 *   1. event(QEvent*) override — handles ShortcutOverride.  Accepting
 *      the override suppresses the global QAction shortcut and routes
 *      the subsequent KeyPress to the spinbox; rejecting (default)
 *      lets the global shortcut fire.  This is the canonical decision
 *      point because it directly answers Qt's "should the shortcut
 *      win?" question.
 *   2. keyPressEvent() override — defensive layer.  In normal flow,
 *      KeyPress events for non-owned keys never reach here (the
 *      ShortcutOverride logic ensures the global shortcut consumes
 *      them first).  But if anything slips through, ignore() it so the
 *      parent widget's keyPressEvent is invoked, which lets qApp's
 *      event filter (KlustersApp::eventFilter) handle eventFilter-only
 *      bindings like H, 1, 2.
 *
 * Both layers consult the same `spinBoxOwnsKey()` helper so the policy
 * is defined exactly once.
 */
class SpinBox : public QSpinBox
{
    Q_OBJECT

public:
    SpinBox(QWidget* parent = nullptr) : QSpinBox(parent) {}

public Q_SLOTS:
    void deselect();

protected:
    /** Override Qt's event() to claim or release ShortcutOverride
     *  events according to spinBoxOwnsKey().  Other event types fall
     *  through to QSpinBox::event(). */
    bool event(QEvent* e) override;

    /** Defensive KeyPress filter.  See class comment. */
    void keyPressEvent(QKeyEvent* event) override;

private:
    /** Single source of truth for the routing policy.  Returns true
     *  iff the spinbox should consume @p ke (numeric input, editing,
     *  spin step, line-edit standard shortcut, etc.); false to route
     *  the key to the application. */
    static bool spinBoxOwnsKey(const QKeyEvent* ke);
};

#endif // SPINBOX_H
