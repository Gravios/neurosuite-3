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

/**
  * Utility class, used to make sure mouse wheel events do not select the qlineedit inside the spinbox
  * @author Lynn Hazan
  */
class SpinBox : public QSpinBox
{
    Q_OBJECT
    
public:
    SpinBox(QWidget * parent = 0) : QSpinBox(parent) {}

public Q_SLOTS:
	void deselect();

protected:
    /** Only consume keystrokes the spinbox genuinely needs:
     *  digits, +/-, Up/Down arrows, PageUp/Down, Home/End, Backspace/Delete,
     *  Tab, Return/Enter.  Everything else (e.g. Shift+R, Ctrl+Z) is
     *  forwarded to the parent widget so KlustersApp shortcuts still fire. */
    void keyPressEvent(QKeyEvent* event) override;
};

#endif // SPINBOX_H
