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

// application specific includes
#include "spinbox.h"
#include <QLineEdit>

void SpinBox::deselect() {
	lineEdit()->deselect();
}

#include <QKeyEvent>

void SpinBox::keyPressEvent(QKeyEvent* event)
{
    const int key = event->key();
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Keys the spinbox legitimately owns:
    //   - digits and numpad digits (with or without Shift/NumLock)
    //   - +/-/. for entering numbers
    //   - Up/Down arrows (increment/decrement)
    //   - PageUp/PageDown forwarded UP so KlustersApp nudge shortcuts fire
    //   - Home/End, Backspace/Delete/Clear (text editing)
    //   - Return/Enter (commit value)
    //   - Tab/Backtab (focus navigation)
    //   - plain modifier keys themselves
    const bool isDigit      = (key >= Qt::Key_0 && key <= Qt::Key_9)
                           || (key >= Qt::Key_0 && key <= Qt::Key_9);
    const bool isNumpadDigit= (key >= Qt::Key_0 && key <= Qt::Key_9);
    const bool isSpin       = (key == Qt::Key_Up   || key == Qt::Key_Down);
    const bool isEdit       = (key == Qt::Key_Backspace || key == Qt::Key_Delete
                             || key == Qt::Key_Home    || key == Qt::Key_End
                             || key == Qt::Key_Left    || key == Qt::Key_Right
                             || key == Qt::Key_Return  || key == Qt::Key_Enter
                             || key == Qt::Key_Plus    || key == Qt::Key_Minus
                             || key == Qt::Key_Period  || key == Qt::Key_Comma);
    const bool isNav        = (key == Qt::Key_Tab || key == Qt::Key_Backtab);
    const bool isModOnly    = (key == Qt::Key_Shift || key == Qt::Key_Control
                             || key == Qt::Key_Alt   || key == Qt::Key_Meta);

    // PageUp/PageDown: always forward to parent (nudge shortcut)
    if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
        event->ignore();
        return;
    }

    // If a modifier other than Shift/NumLock is held, and this isn't a digit
    // or spinbox-specific key, let the parent handle it (e.g. Ctrl+Z, Shift+R).
    const bool hasActionMod = mods & (Qt::ControlModifier | Qt::AltModifier
                                    | Qt::MetaModifier);
    const bool hasShiftOnly = (mods == Qt::ShiftModifier);

    if (hasActionMod) {
        // Ctrl+A (select-all in line edit) is fine; everything else propagates.
        if (key == Qt::Key_A && (mods == Qt::ControlModifier)) {
            QSpinBox::keyPressEvent(event);
            return;
        }
        event->ignore();
        return;
    }

    if (isModOnly || isDigit || isSpin || isEdit || isNav
        || (hasShiftOnly && (isDigit || isEdit))) {
        QSpinBox::keyPressEvent(event);
        return;
    }

    // All other plain keys (letters, F-keys, etc.) — propagate.
    event->ignore();
}
