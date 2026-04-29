/***************************************************************************
                          spinbox.cpp  -  description
                             -------------------
    begin                : Tue Jul  30 12:06:21 EDT 2013
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

#include "spinbox.h"
#include <QKeyEvent>
#include <QLineEdit>

void SpinBox::deselect() {
    lineEdit()->deselect();
}

// ---------------------------------------------------------------------------
// SpinBox::spinBoxOwnsKey
//
// Single source of truth for "does the spinbox own this key?".  See the
// header doc-comment for the full policy.  The answer drives both
// ShortcutOverride routing (event()) and the defensive keyPressEvent
// filter, so the two layers can never disagree.
// ---------------------------------------------------------------------------
bool SpinBox::spinBoxOwnsKey(const QKeyEvent* ke)
{
    const int                    key  = ke->key();
    const Qt::KeyboardModifiers  mods = ke->modifiers();

    // ── 1. Bare modifier keys ───────────────────────────────────────────
    // The user hasn't pressed a real key yet; nothing to decide.
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return true;
    default:
        break;
    }

    // ── 2. Modified keys ────────────────────────────────────────────────
    // Anything with Ctrl, Alt, or Meta is a global-shortcut candidate
    // EXCEPT the line-edit standard shortcuts, which the inner QLineEdit
    // expects (cursor / selection / clipboard / line-edit undo).  Letting
    // these through makes the user's text-editing experience inside the
    // spinbox feel native.  The list is intentionally minimal — these
    // are the only Ctrl-shortcuts QLineEdit defines as standard.
    const bool hasActionMod = mods & (Qt::ControlModifier | Qt::AltModifier
                                      | Qt::MetaModifier);
    if (hasActionMod) {
        if (mods == Qt::ControlModifier) {
            switch (key) {
            case Qt::Key_A:    // select-all
            case Qt::Key_C:    // copy
            case Qt::Key_V:    // paste
            case Qt::Key_X:    // cut
            case Qt::Key_Z:    // line-edit undo (per-spinbox, not app undo)
            case Qt::Key_Y:    // line-edit redo
                return true;
            default:
                break;
            }
        }
        // Any Shift+Ctrl, Alt+anything, Meta+anything — global shortcut.
        // (Note: app-level undo/redo Ctrl+Z and Ctrl+Y are NOT routed to
        //  the app while a spinbox is focused.  This is intentional —
        //  the user is mid-edit; their undo expectation is line-edit
        //  undo, not "undo the last cluster operation".  Click outside
        //  the spinbox to use the app-level undo.)
        return false;
    }

    // ── 3. Plain or Shift-only modifier ─────────────────────────────────
    // From here we're handling keys with no Ctrl/Alt/Meta.  Shift may or
    // may not be down (Shift is needed for some digit / +/- variants on
    // some keyboard layouts, e.g. US layout's Shift+= for +).

    // Digit keys (top row, both 0..9 and the corresponding numpad).
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return true;

    // Step the value.
    if (key == Qt::Key_Up || key == Qt::Key_Down) return true;

    // Text-editing keys.
    switch (key) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Insert:
        return true;
    default:
        break;
    }

    // Numeric input punctuation.
    switch (key) {
    case Qt::Key_Plus:
    case Qt::Key_Minus:
    case Qt::Key_Period:
    case Qt::Key_Comma:
    case Qt::Key_Equal:    // many keyboards: Shift+= produces +
        return true;
    default:
        break;
    }

    // Commit.
    if (key == Qt::Key_Return || key == Qt::Key_Enter) return true;

    // Focus traversal — Qt manages this before keyPressEvent normally,
    // but we acknowledge ownership defensively.
    if (key == Qt::Key_Tab || key == Qt::Key_Backtab) return true;

    // ── 4. Everything else ──────────────────────────────────────────────
    // Letter keys (G, R, U, …), F-keys, Page keys, Esc, etc. all
    // belong to the application.  PageUp/PageDown explicitly: the
    // KlustersApp event filter uses them for ±1-sample timestamp nudge,
    // so we MUST NOT consume them.
    return false;
}

// ---------------------------------------------------------------------------
// SpinBox::event — the canonical routing decision point.
//
// QEvent::ShortcutOverride is sent to the focused widget BEFORE Qt
// dispatches a global QAction shortcut.  Accepting the override
// suppresses the shortcut and routes the upcoming KeyPress to the
// widget; ignoring the override lets the global shortcut fire and the
// widget never sees the KeyPress.
//
// We accept iff the key is one we own.  Net effect: digits stay in the
// spinbox; G / Shift+W / Shift+L / Page Up / etc. fire their global
// handlers exactly as if the spinbox didn't have focus.
//
// We deliberately DO NOT call QSpinBox::event() for ShortcutOverride —
// the only thing that event type produces is an accept/ignore state on
// the event itself, which we control here.  Skipping the base call
// ensures QSpinBox can't accept on a key we want to release.
// ---------------------------------------------------------------------------
bool SpinBox::event(QEvent* e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(e);
        if (spinBoxOwnsKey(ke)) {
            ke->accept();
            return true;
        }
        ke->ignore();
        return false;
    }
    return QSpinBox::event(e);
}

// ---------------------------------------------------------------------------
// SpinBox::keyPressEvent — defensive backstop.
//
// In normal flow this only sees keys we own (digits / spin / edit /
// commit / line-edit shortcuts) because the ShortcutOverride logic in
// event() above filters out everything else.  But Qt's event delivery
// path is complex and there are edge cases (synthetic events, tablet
// inputs, IME composition, etc.) where a non-owned KeyPress can still
// arrive.  Ignoring it here makes the parent widget's keyPressEvent
// fire next, which gives qApp's event filter
// (KlustersApp::eventFilter) the chance to handle eventFilter-only
// bindings like H, 1, 2.
// ---------------------------------------------------------------------------
void SpinBox::keyPressEvent(QKeyEvent* event)
{
    if (spinBoxOwnsKey(event)) {
        QSpinBox::keyPressEvent(event);
        return;
    }
    // Forward to parent widget by ignoring.  Qt then invokes the
    // parent's keyPressEvent, where qApp's event filter sees it.
    event->ignore();
}
