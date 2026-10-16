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
#include "anatomypage.h"
#include "tags.h"
#include <QTableWidget>
#include <QPushButton>

// include files for QT
#include <algorithm>
#include <QList>
#include <QStringList>


#include <QEvent>
#include <QVector>
#include <QDebug>
#include <QRegularExpression>
#include <QMessageBox>
#include <QSignalBlocker>

// The attribute table's column headers are user-facing labels ("Skip", set in
// anatomylayout.ui); the attribute map exchanged with ParameterView and the
// YAML reader/writer is keyed by the document key, ndmanager::SKIP ("skip").
// Matching on the header text kept the two apart: skip flags loaded blank and
// every save wrote skip: 0.  This is the one place the two names meet.
static QString attributeKey(const QString& header)
{
    if (header.compare(ndmanager::SKIP, Qt::CaseInsensitive) == 0)
        return ndmanager::SKIP;
    return header;
}

// The channel ids listed in a group cell ("0 1 2 ").
static QList<int> channelIds(const QString& text)
{
    QList<int> ids;
    const QStringList parts = text.simplified().split(" ", Qt::SkipEmptyParts);
    for (const QString& part : parts)
        ids.append(part.toInt());
    return ids;
}

// A group cell holds only digits and whitespace, and (once the channel count
// is known) only ids of existing channels.
static bool isValidGroup(const QString& text, int nbChannels)
{
    if (text.contains(QRegularExpression("[^\\d\\s]")))
        return false;
    if (nbChannels > 0) {
        const QList<int> ids = channelIds(text);
        for (int id : ids)
            if (id >= nbChannels)
                return false;
    }
    return true;
}

// The ids of row @p row's group that also appear in another row of column 0.
static QStringList channelsInOtherRows(const QTableWidget* table, int row)
{
    QStringList shared;
    const QTableWidgetItem* cell = table->item(row, 0);
    if (!cell)
        return shared;
    const QList<int> ids = channelIds(cell->text());
    for (int r = 0; r < table->rowCount(); ++r) {
        const QTableWidgetItem* other = r == row ? nullptr : table->item(r, 0);
        if (!other)
            continue;
        const QList<int> otherIds = channelIds(other->text());
        for (int id : ids)
            if (otherIds.contains(id) && !shared.contains(QString::number(id)))
                shared << QString::number(id);
    }
    return shared;
}

AnatomyPage::AnatomyPage(QWidget* parent)
    : AnatomyLayout(parent),
      guard(groupTable, [this](int, int, const QString& text) -> QString {
          return isValidGroup(text, nbChannels) ? QString()
              : tr("Give the group's channels as ids of existing channels (0 to %1), "
                   "separated by spaces.").arg(qMax(0, nbChannels - 1));
      }),
      attributeGuard(attributesTable, [this](int, int column, const QString& text) -> QString {
          // The skip flag is a number; the other attributes are free text.
          const QTableWidgetItem* header = attributesTable->horizontalHeaderItem(column);
          if (!header || attributeKey(header->text()) != ndmanager::SKIP || text.isEmpty())
              return QString();
          bool ok = false;
          text.toInt(&ok);
          return ok ? QString() : tr("Skip is 0 or 1.");
      }),
      modified(false),
      nbChannels(0)
{
    attributesTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    groupTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );

    connect(addGroupButton, &QAbstractButton::clicked, this, &AnatomyPage::addGroup);
    connect(removeGroupButton, &QAbstractButton::clicked, this, &AnatomyPage::removeGroup);
    // currentCellChanged fires for keyboard, mouse-click and double-click navigation;
    // the three finer-grained press/click/double-click signals are redundant here.
    connect(groupTable, &QTableWidget::cellChanged, this, &AnatomyPage::groupChanged);

    connect(attributesTable, &QTableWidget::cellChanged, this, &AnatomyPage::attributeChanged);
}

AnatomyPage::~AnatomyPage(){}


void AnatomyPage::setAttributes(const QMap<QString, QMap<int,QString> >& attributes){
    // Filling the table is not an edit (and a loaded value is not re-validated).
    const QSignalBlocker blocker(attributesTable);
    for(int i =0; i<attributesTable->columnCount();++i){
        //the headers have been set in the ui file; the map is keyed by attributeKey()
        const QString header = attributesTable->horizontalHeaderItem(i)->text();
        const QString key = attributeKey(header);
        const QMap<int,QString> values = attributes.contains(key) ? attributes.value(key)
                                                                  : attributes.value(header);
        //insert the values in the table and set the line headers
        for(int j = 0;j<attributesTable->rowCount();++j){
            attributesTable->setItem(j,i,new QTableWidgetItem(values.value(j)));
            attributesTable->setVerticalHeaderItem(j,new QTableWidgetItem(QString::number(j)));
        }
        //attributesTable->adjustColumn(i);
    }
    attributeGuard.remember();
}

void AnatomyPage::getAttributes(QMap<QString, QMap<int,QString> >& attributesMap)const
{
    for(int i =0; i<attributesTable->columnCount();++i){
        QMap<int,QString> values;
        for(int j = 0;j<attributesTable->rowCount();++j){
            QTableWidgetItem* it = attributesTable->item(j,i);
            if (!it) continue;  // cell is null if setItem was never called for it
            QString attribut = it->text().simplified();
            if(!attribut.isEmpty())
                values.insert(j,attribut);
        }
        attributesMap.insert(attributeKey(attributesTable->horizontalHeaderItem(i)->text()),values);
    }
}

void AnatomyPage::setGroups(const QMap<int, QList<int> >& groups)
{
    // Filling the table is not an edit: no validation, no duplicate warning.
    const QSignalBlocker blocker(groupTable);
    groupTable->clearContents();
    groupTable->setRowCount(groups.count());

    // Use an explicit row counter rather than (key - 1).  Group keys from the
    // YAML reader are normally 1..N, but if any group entry is skipped (e.g.
    // a group with no channels) the reader increments its counter without
    // inserting a key, producing gaps.  Using key-1 as the row index then
    // writes past the end of the table, corrupting memory or crashing.
    int row = 0;
    QMap<int,QList<int> >::const_iterator iterator;
    for (iterator = groups.begin(); iterator != groups.end(); ++iterator, ++row) {
        QList<int> channelIds = iterator.value();
        QList<int>::iterator channelIterator;

        QString group;
        for(channelIterator = channelIds.begin(); channelIterator != channelIds.end(); ++channelIterator){
            group.append(QString::number(*channelIterator));
            group.append(" ");
        }

        groupTable->setItem(row, 0, new QTableWidgetItem(group));
    }//end of groups loop
    guard.remember();
}

void AnatomyPage::getGroups(QMap<int, QList<int> >& groups)const{

    int groupId = 1;
    for(int i =0; i<groupTable->rowCount();++i){
        QList<int> channels;
        QTableWidgetItem* it = groupTable->item(i, 0);
        if (!it) continue;  // null if row was added but never populated: a blank row
        QString channelList = it->text().simplified();
        if(channelList.isEmpty())
            continue;  // a blank row is not a group
        const QStringList channelParts = channelList.split(" ", Qt::SkipEmptyParts);

        for(uint j = 0;j < channelParts.count(); ++j) {
            channels.append(channelParts.at(j).toInt());
        }

        groups.insert(groupId,channels);
        groupId++;
    }
}

void AnatomyPage::removeGroup()
{
    modified = true;
    const QList<QTableWidgetSelectionRange> range = groupTable->selectedRanges();
    if (!range.isEmpty()) {
        QList<int> lst;
        for (const QTableWidgetSelectionRange&r : range) {
            const int nbRows = r.bottomRow() - r.topRow() + 1;
            for(int i = 0; i < nbRows;++i){
                int val = (r.topRow() + i);
                if(!lst.contains(val)) {
                    lst<< val;
                }
            }
        }
        std::sort(lst.begin(), lst.end());
        for(int i = lst.count()-1; i>=0; --i) {
            groupTable->removeRow(lst.at(i));
        }
    }
    guard.remember();
}

void AnatomyPage::addGroup(){
    modified = true;
    groupTable->insertRow(groupTable->rowCount());
    groupTable->setItem(groupTable->rowCount() - 1,0,new QTableWidgetItem());
    guard.remember();
}

void AnatomyPage::groupChanged(int row,int column){
    // An invalid entry is put back to its last good value with the reason
    // shown at the cell; nothing holds the focus (CellGuard).
    if(!guard.accept(row, column))
        return;
    modified = true;
    const QStringList shared = channelsInOtherRows(groupTable, row);
    if(!shared.isEmpty())
        QMessageBox::warning(this, tr("Channel in several groups"),
                             tr("Channel(s) %1 also belong to another anatomical group.").arg(shared.join(", ")));
}

void AnatomyPage::attributeChanged(int row,int column){
    if(!attributeGuard.accept(row, column))
        return;
    modified = true;
}


void AnatomyPage::setNbChannels(int nbChannels){
    this->nbChannels = nbChannels;
    // Clear all rows at once. The old loop called removeRow(i) while iterating
    // 0..rowCount-1: each removal shrinks the table, so odd rows were silently
    // skipped and the final removeRow could write past the end of the table.
    attributesTable->setRowCount(0);
    attributesTable->setRowCount(nbChannels);
}
