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
#include "filepage.h"
#include <QTableWidget>
#include <QLineEdit>
#include <QSpinBox>

// include files for QT
#include <algorithm>
#include <QPushButton>


#include <QEvent>
#include <QVector>
#include <QList>
#include <QTableWidgetItem>
#include <QRegularExpression>
#include <QVariant>

FilePage::FilePage(QWidget *parent)
    : FileLayout(parent),
      guard(mappingTable, [](int, int, const QString& text) -> QString {
          return text.contains(QRegularExpression("[^\\d\\s]"))
              ? tr("Give the channels as numbers separated by spaces.") : QString();
      }),
      modified(false),
      isInit(true)
{
    mappingTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    connect(extensionLineEdit, &QLineEdit::returnPressed, this, &FilePage::changeCaption);
    connect(extensionLineEdit, &QLineEdit::editingFinished, this, &FilePage::changeCaption);

    connect(addChannelButton, &QAbstractButton::clicked, this, &FilePage::addChannel);
    connect(removeChannelButton, &QAbstractButton::clicked, this, &FilePage::removeChannel);

    connect(extensionLineEdit, &QLineEdit::textChanged, this, &FilePage::propertyModified);
    connect(samplingRateLineEdit, &QLineEdit::textChanged, this, &FilePage::propertyModified);

    //For validation
    connect(mappingTable, &QTableWidget::cellChanged, this, &FilePage::mappingChanged);

}


FilePage::~FilePage(){
    extensionLineEdit->clearFocus();
    samplingRateLineEdit->clearFocus();
}


void FilePage::addChannel(){
    modified = true;

    const int id = nextChannelId();
    const int row = mappingTable->rowCount();
    mappingTable->setRowCount(row + 1);
    QTableWidgetItem* item = new QTableWidgetItem();
    item->setData(ChannelIdRole, id);
    mappingTable->setItem(row,0,item);
    mappingTable->setVerticalHeaderItem(row,new QTableWidgetItem(QString::number(id)));
    guard.remember();
}

int FilePage::nextChannelId() const{
    // One past the highest id held by a row; ids are 1-based when there are none.
    int next = 1;
    for(int i = 0; i < mappingTable->rowCount(); ++i){
        const QTableWidgetItem* item = mappingTable->item(i,0);
        if(!item)
            continue;
        const QVariant id = item->data(ChannelIdRole);
        if(id.isValid())
            next = std::max(next, id.toInt() + 1);
    }
    return next;
}

void FilePage::removeChannel(){
    modified = true;

    QList<QTableWidgetSelectionRange> range = mappingTable->selectedRanges ();
    if(!range.isEmpty()) {
        QList<int> rowsToRemove;
        for (const QTableWidgetSelectionRange& selection : range) {
            int nbRows = selection.bottomRow() - selection.topRow() + 1;
            for(int i = 0; i < nbRows;++i){
                rowsToRemove.append(selection.topRow() + i);
            }
        }
        std::sort(rowsToRemove.begin(), rowsToRemove.end());
        //Actually remove the rows
        for(int i = rowsToRemove.count()-1; i>=0;--i) {
            mappingTable->removeRow(rowsToRemove.at(i));
        }

    }
    guard.remember();
}

void FilePage::setChannelMapping(const QMap<int, QList<int> >& channels){
    mappingTable->clearContents();
    mappingTable->setRowCount(channels.count());

    // Rows are filled with a counter and each row keeps its channel id (the
    // map key) as item data, shown in the row header.  Placing a row at
    // key - 1 left null rows, and wrote past the table, for 0-based or
    // gapped ids, and getChannelMapping then crashed on the null rows and
    // renumbered the rest.
    int row = 0;
    QMap<int,QList<int> >::const_iterator iterator;
    //The iterator gives the keys sorted.
    for(iterator = channels.begin(); iterator != channels.end(); ++iterator, ++row){
        QList<int> channelIds = iterator.value();
        QList<int>::iterator channelIterator;

        //create the string containing the channel ids
        QString newChannel;
        for(channelIterator = channelIds.begin(); channelIterator != channelIds.end(); ++channelIterator){
            newChannel.append(QString::number(*channelIterator));
            newChannel.append(" ");
        }

        QTableWidgetItem* item = new QTableWidgetItem(newChannel);
        item->setData(ChannelIdRole, iterator.key());
        mappingTable->setItem(row,0,item);
        mappingTable->setVerticalHeaderItem(row,new QTableWidgetItem(QString::number(iterator.key())));
    }//end of groups loop
    mappingTable->resizeColumnToContents(0);
}

QMap<int, QList<int> > FilePage::getChannelMapping()const{
    QMap<int, QList<int> > channelMapping;

    // Each row is written under the id it was loaded or added with; a row
    // without one (or with a duplicate) gets the next free id.
    int nextId = nextChannelId();
    for(int i =0; i<mappingTable->rowCount();++i){
        QList<int> channels;
        const QTableWidgetItem* item = mappingTable->item(i,0);
        if(!item)
            continue;
        const QString channelList = item->text().simplified();
        if(channelList.isEmpty())
            continue;

        const QVariant storedId = item->data(ChannelIdRole);
        int channelId = storedId.isValid() ? storedId.toInt() : nextId++;
        if(channelMapping.contains(channelId))
            channelId = nextId++;

        QStringList channelParts = channelList.split(" ", Qt::SkipEmptyParts);
        for(int j = 0;j < channelParts.count(); ++j)
            channels.append(channelParts.at(j).toInt());
        channelMapping.insert(channelId,channels);
    }

    return channelMapping;
}


/**Validates the current entry in the mapping table.*/
void FilePage::mappingChanged(int row,int column){
    // setChannelMapping fills the table while the page is initialised:
    // that is the loaded session, not an edit, and it is not re-validated.
    if(isInit){
        guard.remember(row, column);
        return;
    }
    // An invalid entry is put back to its last good value with the reason
    // shown at the cell; nothing holds the focus (CellGuard).
    if(!guard.accept(row, column))
        return;
    modified = true;
    mappingTable->resizeColumnToContents(column);
}


void FilePage::changeCaption(){
    if(extensionLineEdit->text() != extension){
        extension = extensionLineEdit->text();
        emit extensionChanged(extension,this);
    }
}

