/*
 *   Copyright (C) 1997  Michael Roth <mroth@wirlweb.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#ifndef KLUSTERSEPARATOR_H
#define KLUSTERSEPARATOR_H

#include <QFrame>

/**
 * Standard horizontal or vertical separator.
 *
 * \image html kseparator-horizontal.png "KDE Separator with horizontal orientation"
 * \image html kseparator-vertical.png "KDE Separator with vertical orientation"
 *
 * @author Michael Roth <mroth@wirlweb.de>
 */
class KLusterSeparator : public QFrame
{
    Q_OBJECT
    Q_PROPERTY( Qt::Orientation orientation READ orientation WRITE setOrientation )

public:
    /**
   * Constructor.
   * @param parent parent object.
   * @param f extra QWidget flags.
   **/
    explicit KLusterSeparator(QWidget* parent=nullptr, Qt::WindowFlags f=Qt::WindowFlags());

    /**
   * Constructor.
   * @param orientation Set the orientation of the separator.
   * Possible values are Horizontal or Vertical.
   * @param parent parent object.
   * @param f extra QWidget flags.
   **/
    explicit KLusterSeparator(Qt::Orientation orientation, QWidget* parent=nullptr, Qt::WindowFlags f=Qt::WindowFlags());

    /**
   * Returns the orientation of the separator.
   * @return int Possible values Horizontal or Vertical.
   **/
    Qt::Orientation orientation() const;

    /**
   * Set the orientation of the separator to @p orientation
   *
   * @param orientation Possible values are Vertical and Horizontal.
   */
    void setOrientation(Qt::Orientation orientation);

private:
    class KLusterSeparatorPrivate* d;
};


#endif // KSEPARATOR_H
