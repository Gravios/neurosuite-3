/*
  Copyright (c) 2012 Montel Laurent <montel@kde.org>
  
  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License, version 2, as
  published by the Free Software Foundation.
  
  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
  
  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "qextendtabwidget.h"
#include "dockarea.h"

QExtendTabWidget::QExtendTabWidget(QWidget *parent)
    :QTabWidget(parent)
{
}

QExtendTabWidget::~QExtendTabWidget()
{

}

DockArea* QExtendTabWidget::addDockArea(const QString& label)
{
    DockArea* dockArea = new DockArea ( this );
    addTab ( dockArea,label );
    return dockArea;
}

void QExtendTabWidget::addDockArea(DockArea* area, const QString& label)
{
    addTab ( area,label );
}


DockArea* QExtendTabWidget::currentDockArea()
{
    QWidget *w = currentWidget ();
    if(!w)
        return 0;
    return static_cast<DockArea *>(w);
}

