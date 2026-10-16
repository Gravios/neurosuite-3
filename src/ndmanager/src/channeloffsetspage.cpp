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
#include "channeloffsetspage.h"
#include <QTableWidget>

// include files for QT
#include <QWidget>

#include <QList>

ChannelOffsetsPage::ChannelOffsetsPage(QWidget* parent)
    : ChannelOffsetsLayout(parent),nbChannels(0),modified(false)
{
    offsetTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    connect(offsetTable, &QTableWidget::itemChanged, this, &ChannelOffsetsPage::propertyModified);
    connect(offsetTable, &QTableWidget::itemDoubleClicked, this, &ChannelOffsetsPage::propertyModified);

}

ChannelOffsetsPage::~ChannelOffsetsPage(){}

void ChannelOffsetsPage::getOffsets(QMap<int,int>& offsets){
    // A cell setOffsets never filled (the session lists no offset for that
    // channel, or none at all) is null: it is the default offset, 0.
    for(int i =0; i<nbChannels && i<offsetTable->rowCount();++i){
        const QTableWidgetItem* cell = offsetTable->item(i,0);
        offsets.insert(i,cell ? cell->text().toInt() : 0);
    }
}

void ChannelOffsetsPage::setOffsets(const QMap<int,int>& offsets){
    // Row i is channel i (setNbChannels labels the rows); ids outside the
    // table are ignored.
    QMap<int,int>::ConstIterator iterator;
    //The iterator gives the keys sorted.
    for(iterator = offsets.constBegin(); iterator != offsets.constEnd(); ++iterator){
        const int channelId = iterator.key();
        if(channelId < 0 || channelId >= offsetTable->rowCount())
            continue;
        offsetTable->setItem(channelId,0,new QTableWidgetItem(QString::number(iterator.value())));
    }
}



void ChannelOffsetsPage::setNbChannels(int nbChannels){
    this->nbChannels = nbChannels;

    // Rebuild the table: one row per channel, labelled with its id and set
    // to the default offset, 0.  (A forward removeRow loop only removed
    // every other row.)
    offsetTable->setRowCount(0);
    offsetTable->setRowCount(nbChannels);
    QStringList labels;
    for(int i = 0; i < nbChannels; ++i){
        labels << QString::number(i);
        offsetTable->setItem(i,0,new QTableWidgetItem(QStringLiteral("0")));
    }
    offsetTable->setVerticalHeaderLabels(labels);
}
