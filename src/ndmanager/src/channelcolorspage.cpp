/***************************************************************************
 *   Copyright (C) 2004 by Lynn Hazan                                      *
 *   lynn.hazan@myrealbox.com                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
//include files for the application
#include "channelcolorspage.h"

// include files for QT
#include <QWidget>

#include <QList>
#include <QColor>

#include <QColorDialog>

#include <QMouseEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QDebug>

ChannelColorsPage::ChannelColorsPage(QWidget* parent)
    : ChannelColorsLayout(parent)
    ,modified(false)
{
    colorTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    connect(colorTable, SIGNAL(cellChanged(int,int)),this, SLOT(propertyModified()));
    connect(colorTable, SIGNAL(cellDoubleClicked(int,int)),this, SLOT(propertyModified()));
    connect(colorTable, SIGNAL(modified()),SLOT(propertyModified()));
}

ChannelColorsPage::~ChannelColorsPage(){}

void ChannelColorsPage::getColors(QList<ChannelColors>& colors){
    // Guard against null items: cells may be uninitialised if the session YAML
    // had no neuroscope/channels/colors section (new sessions, xml2yaml output,
    // template files).  Null cells produce a segfault on ->text(); fall back to
    // the same default colour used by nbChannelsModified().
    static const QString kDefault = QStringLiteral("#0080ff");
    for(int i = 0; i < colorTable->rowCount(); ++i) {
        ChannelColors channelColors;
        channelColors.setId(i);
        QTableWidgetItem* c0 = colorTable->item(i, 0);
        QTableWidgetItem* c1 = colorTable->item(i, 1);
        QTableWidgetItem* c2 = colorTable->item(i, 2);
        channelColors.setColor(           c0 ? c0->text() : kDefault);
        channelColors.setGroupColor(      c1 ? c1->text() : kDefault);
        channelColors.setSpikeGroupColor( c2 ? c2->text() : kDefault);
        colors.append(channelColors);
    }
}

void ChannelColorsPage::setColors(const QList<ChannelColors>& colors){
    QList<ChannelColors>::ConstIterator iterator;
    for(iterator = colors.constBegin(); iterator != colors.constEnd(); ++iterator){
        const int id = (*iterator).getId();
        colorTable->setItem(id,0,new QTableWidgetItem((*iterator).getColor().name()));
        colorTable->setItem(id,1,new QTableWidgetItem((*iterator).getGroupColor().name()));
        colorTable->setItem(id,2,new QTableWidgetItem((*iterator).getSpikeGroupColor().name()));
    }
}

void ChannelColorsPage::setNbChannels(int nbChannels){
    // Use setRowCount(0) instead of a forward removeRow() loop.
    // The loop `for(i=0; i<rowCount(); ++i) removeRow(i)` only removes half
    // the rows because rowCount() shrinks as rows are deleted.
    colorTable->setRowCount(0);
    colorTable->setRowCount(nbChannels);
    // Pre-populate every cell with the default colour so that getColors()
    // always sees valid QTableWidgetItem* regardless of what setColors() does.
    static const QString kDefault = QStringLiteral("#0080ff");
    for(int i = 0; i < nbChannels; ++i){
        colorTable->setItem(i, 0, new QTableWidgetItem(kDefault));
        colorTable->setItem(i, 1, new QTableWidgetItem(kDefault));
        colorTable->setItem(i, 2, new QTableWidgetItem(kDefault));
    }
}


