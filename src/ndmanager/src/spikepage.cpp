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
#include "spikepage.h"
#include <QTableWidget>
#include <QPushButton>
#include <QMessageBox>
#include "tags.h"

// include files for QT
#include <algorithm>
#include <QLabel> 


#include <QEvent>
#include <QVector>
#include <QList>
#include <QDebug>

using namespace ndmanager;

SpikePage::SpikePage(QWidget* parent)
    : SpikeLayout(parent),
      isIncorrectRow(false),
      incorrectRow(0),
      incorrectColumn(0),
      modified(false)
{
    groupTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    //install a filter on the groupTable in order to validate the entries
    groupTable->installEventFilter(this);

    // currentCellChanged fires for keyboard, mouse-click and double-click navigation;
    // the three finer-grained press/click/double-click signals are redundant here.
    connect(groupTable, &QTableWidget::currentCellChanged, this, &SpikePage::slotValidate);
    connect(addGroupButton, &QAbstractButton::clicked, this, &SpikePage::addGroup);
    connect(removeGroupButton, &QAbstractButton::clicked, this, &SpikePage::removeGroup);
    connect(applyToAllButton, &QAbstractButton::clicked, this, &SpikePage::applyToAllGroups);
    connect(groupTable, &QTableWidget::cellChanged, this, &SpikePage::groupChanged);

}


SpikePage::~SpikePage(){}


bool SpikePage::eventFilter(QObject* object,QEvent* event)
{
    QString name = object->objectName();
    if(name.indexOf("groupTable") != -1 && isIncorrectRow) {
        groupTable->selectRow(incorrectRow);
        //groupTable->selectColumn(incorrectColumn);
        groupTable->setCurrentCell(incorrectRow,incorrectColumn);
        return true;
    } else if(name.indexOf("groupTable") != -1 && event->type() == QEvent::Leave) {
        if(groupTable->currentRow() != -1){
            int row = groupTable->currentRow();
            int column = groupTable->currentColumn();
            QWidget* widget = groupTable->cellWidget(row,column);
            if(widget != 0 && widget->metaObject()->className() == QLatin1String("QLineEdit")){
                groupTable->setCellWidget(row,column,widget);
                return true;
            }
            else return QWidget::eventFilter(object,event);
        }
        else return QWidget::eventFilter(object,event);
    }
    else return QWidget::eventFilter(object,event);
}

void SpikePage::setGroups(const QMap<int, QList<int> >& groups,const QMap<int, QMap<QString,QString> >& information){
    //Clean the groupTable, just in case, before creating empty rows.
    groupTable->clearContents();
    groupTable->setRowCount(groups.count());

    // Use an explicit row counter. Group keys from the YAML reader can be
    // non-contiguous if any channelGroups entry is skipped, making key-1
    // larger than rowCount-1 and producing an out-of-bounds write.
    int row = 0;
    QMap<int,QList<int> >::const_iterator iterator;
    for(iterator = groups.begin(); iterator != groups.end(); ++iterator, ++row){
        QList<int> channelIds = iterator.value();
        QList<int>::iterator channelIterator;

        QString group;
        for(channelIterator = channelIds.begin(); channelIterator != channelIds.end(); ++channelIterator){
            group.append(QString::number(*channelIterator));
            group.append(" ");
        }

        groupTable->setItem(row, 0, new QTableWidgetItem(group));

        QMap<QString,QString> groupInformation = information[iterator.key()];
        QMap<QString,QString>::Iterator iterator2;
        for(iterator2 = groupInformation.begin(); iterator2 != groupInformation.end(); ++iterator2){
            if(iterator2.key() == NB_SAMPLES)
                groupTable->setItem(row, 1, new QTableWidgetItem(iterator2.value()));
            else if(iterator2.key() == PEAK_SAMPLE_INDEX)
                groupTable->setItem(row, 2, new QTableWidgetItem(iterator2.value()));
            else if(iterator2.key() == NB_FEATURES)
                groupTable->setItem(row, 3, new QTableWidgetItem(iterator2.value()));
            else if(iterator2.key() == SDIFF_PAIRS)
                groupTable->setItem(row, 4, new QTableWidgetItem(iterator2.value()));
        }
    }//end of groups loop
}



void SpikePage::getGroups(QMap<int, QList<int> >& groups)const{
    int groupId = 1;
    for(int i =0; i<groupTable->rowCount();++i){
        QList<int> channels;
        QTableWidgetItem* it = groupTable->item(i, 0);
        if (!it) { ++groupId; continue; }  // null if row was added but never populated
        QString channelList = it->text().simplified();
        if(channelList == " ")
            continue;
        QStringList channelParts = channelList.split(" ", Qt::SkipEmptyParts);
        for(uint j = 0;j < channelParts.count(); ++j)
            channels.append(channelParts[j].toInt());
        groups.insert(groupId,channels);
        groupId++;
    }
}

void SpikePage::getGroupInformation(QMap<int,  QMap<QString,QString> >& groupInformation)const{
    int groupId = 1;
    for(int i =0; i<groupTable->rowCount();++i){
        QMap<QString,QString> information;
        QTableWidgetItem* col0 = groupTable->item(i, 0);
        if (!col0) { ++groupId; continue; }  // unpopulated row
        QString channelList = col0->text().simplified();
        if(channelList == " ")
            continue;
        //The positions of the information in the table are hard coded
        for(int j = 1;j < groupTable->columnCount(); ++j){
            QTableWidgetItem* cell = groupTable->item(i, j);
            if (!cell) continue;  // manually added rows may have null cells
            const QString infoItem = cell->text().simplified();
            if(infoItem == " ")
                continue;
            if(j == 1)
                information.insert(NB_SAMPLES,infoItem);
            else if(j == 2)
                information.insert(PEAK_SAMPLE_INDEX,infoItem);
            else if(j == 3)
                information.insert(NB_FEATURES,infoItem);
            else if(j == 4)
                information.insert(SDIFF_PAIRS,infoItem);
        }
        groupInformation.insert(groupId,information);
        groupId++;
    }
}

void SpikePage::removeGroup(){
    if(isIncorrectRow)
        return;
    modified = true;
    const QList<QTableWidgetSelectionRange> range = groupTable->selectedRanges();
    if(!range.isEmpty()) {
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
    emit nbGroupsModified(groupTable->rowCount());
}

void SpikePage::applyToAllGroups(){
    const int nRows = groupTable->rowCount();
    if(nRows < 2)
        return;                       // no other group to copy to

    const int srcRow = groupTable->currentRow();
    if(srcRow < 0 || srcRow >= nRows){
        QMessageBox::information(this, tr("Apply to all groups"),
            tr("Select a group first; its number of samples, peak sample index, "
               "feature count and difference pattern are then copied to every "
               "other group."));
        return;
    }

    // Capture the selected group's parameter columns (1..last).  Column 0 is the
    // channel list, unique to each group and never copied.  The difference-pattern
    // column rides along, so identical shanks can be configured once and applied.
    // An empty source cell is skipped so it can never blank that column elsewhere.
    const int nCol = groupTable->columnCount();
    QVector<QString> src(nCol);
    bool haveValue = false;
    for(int col = 1; col < nCol; ++col){
        QTableWidgetItem* item = groupTable->item(srcRow, col);
        const QString text = item ? item->text().simplified() : QString();
        if(!text.isEmpty()){ src[col] = text; haveValue = true; }
    }
    if(!haveValue)
        return;

    // Rewrite the other rows without firing cellChanged for each cell; the values
    // come from an already-validated row, so mark the page modified once.
    const bool blocked = groupTable->blockSignals(true);
    for(int row = 0; row < nRows; ++row){
        if(row == srcRow)
            continue;
        QTableWidgetItem* col0 = groupTable->item(row, 0);
        if(!col0 || col0->text().simplified().isEmpty())
            continue;                 // skip placeholder / blank rows
        for(int col = 1; col < nCol; ++col){
            if(src[col].isEmpty())
                continue;
            QTableWidgetItem* cell = groupTable->item(row, col);
            if(cell)
                cell->setText(src[col]);
            else
                groupTable->setItem(row, col, new QTableWidgetItem(src[col]));
        }
    }
    groupTable->blockSignals(blocked);
    modified = true;
}

void SpikePage::groupChanged(int row,int column){
    modified = true;
    QString group = groupTable->item(row,column)->text();

    if(isIncorrectRow){
        QWidget* widget = groupTable->cellWidget(incorrectRow,incorrectColumn);
        QString incorrectGroup;
        if(widget != 0 && widget->metaObject()->className() == QLatin1String("QLineEdit"))
            incorrectGroup = static_cast<QLineEdit*>(widget)->text();
        else if(widget == 0)
            incorrectGroup = groupTable->item(incorrectRow,incorrectColumn)->text();
        if(incorrectGroup.contains(QRegularExpression("[^\\d\\s]")) != 0){
            groupTable->selectRow(incorrectRow);
            groupTable->setCurrentCell(incorrectRow,incorrectColumn);
            return;
        }
    }

    isIncorrectRow = false;
    incorrectRow = 0;
    incorrectColumn = column;
    //groupTable->adjustRow(row);

    //the group entry should only contain digits and whitespaces
    if(group.contains(QRegularExpression("[^\\d\\s]")) != 0){
        isIncorrectRow = true;
        incorrectRow = row;
        incorrectColumn = column;
        groupTable->selectRow(incorrectRow);
        groupTable->setCurrentCell(incorrectRow,incorrectColumn);
    }
}

void SpikePage::slotValidate(){
    modified = true;
    if(isIncorrectRow){
        groupTable->selectRow(incorrectRow);
        // groupTable->selectColumn(incorrectColumn);
        groupTable->setCurrentCell(incorrectRow,incorrectColumn);
    }
}

void SpikePage::addGroup(){
    if(isIncorrectRow)
        return;
    modified = true;
    groupTable->insertRow(groupTable->rowCount());
    for(int i = 0;i<groupTable->columnCount();++i){
        groupTable->setItem(groupTable->rowCount()-1,i,new QTableWidgetItem());
        groupTable->update();
    }
    emit nbGroupsModified(groupTable->rowCount());
}


