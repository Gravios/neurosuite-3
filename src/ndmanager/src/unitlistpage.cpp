/***************************************************************************
 *   Copyright (C) 2006 by Michael Zugaro                                  *
 *   michael.zugaro@college-de-france.fr                                   *
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
#include "unitlistpage.h"
#include "unittable.h"
#include <QPushButton>


// include files for QT
#include <algorithm>
#include <QList>
#include <QStringList>


#include <QEvent>
#include <QVector>
#include <QHeaderView>
#include <QRegularExpression>
#include <QSignalBlocker>

UnitListPage::UnitListPage(QWidget* parent) :
    UnitListLayout(parent),
    guard(unitTable, [](int, int column, const QString& unit) -> QString {
        // group and cluster are whole numbers, the isolation distance a number
        if((column == 0 || column == 1) && unit.contains(QRegularExpression("[^\\d]")))
            return tr("Only a whole number is allowed here.");
        if(column == 4 && unit.contains(QRegularExpression("[^\\d.]")))
            return tr("Only a number is allowed here.");
        return QString();
    }),
    modified(false)
{
    unitTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    unitTable->setSortingEnabled(true);

    connect(unitTable, &QTableWidget::cellChanged, this, &UnitListPage::unitChanged);

    connect(addUnitButton, &QAbstractButton::clicked, this, &UnitListPage::addUnit);
    connect(removeUnitButton, &QAbstractButton::clicked, this, &UnitListPage::removeUnit);
}

UnitListPage::~UnitListPage(){}



void UnitListPage::setUnits(const QMap<int, QStringList >& units)
{
    // Fill with sorting off.  With it on, every setItem in the sort column
    // re-sorts the table, so later cells of a unit land in rows that have
    // already moved: units came out scrambled and rows were left with null
    // cells.  Loading is not an edit, so the cell-changed validation is not
    // run and the page is left unmodified.
    const bool sorting = unitTable->isSortingEnabled();
    unitTable->setSortingEnabled(false);
    {
        const QSignalBlocker blocker(unitTable);
        unitTable->clearContents();
        unitTable->setRowCount(units.count());

        // A row counter, not the map key: the reader's keys are 0-based
        // but nothing guarantees that.
        int row = 0;
        QMap<int,QStringList >::const_iterator iterator;
        //The iterator gives the keys sorted.
        for (iterator = units.constBegin(); iterator != units.constEnd(); ++iterator, ++row)
        {
            const QStringList info = iterator.value();
            // Every cell gets an item, even past a short entry, so getUnits
            // never meets a null cell.
            for (int i = 0; i < unitTable->columnCount(); ++i)
                unitTable->setItem(row, i, new UnitTableItem(i < info.count() ? info.at(i) : QString()));
        }//end of units loop
    }
    unitTable->setSortingEnabled(sorting);
    modified = false;
    guard.remember();
}

void UnitListPage::getUnits(QMap<int, QStringList >& units)const
{ 
    int unitId = 1;
    for(int i =0; i<unitTable->rowCount();++i)
    {
        QStringList info;
        for(int j = 0;j < unitTable->columnCount(); ++j)
        {
            const QTableWidgetItem* cell = unitTable->item(i,j);
            info.append(cell ? cell->text().simplified() : QString());
        }
        units.insert(unitId,info);
        unitId++;
    }
}

void UnitListPage::removeUnit()
{
    modified = true;
    const QList<QTableWidgetSelectionRange> range = unitTable->selectedRanges();
    if(!range.isEmpty()) {
        QList<int> lst;
        for (const QTableWidgetSelectionRange&r : range) {
            const int nbRows = r.bottomRow() - r.topRow()+1;
            for(int i = 0; i < nbRows;++i){
                const int val = (r.topRow() + i);
                if(!lst.contains(val)) {
                    lst<< val;
                }
            }
        }
        std::sort(lst.begin(), lst.end());
        for(int i = lst.count()-1; i>=0; --i) {
            unitTable->removeRow(lst.at(i));
        }
    }
    guard.remember();
}

void UnitListPage::setNbUnits(int nbUnits)
{
    for(int i =0; i<unitTable->rowCount();++i)
        unitTable->removeRow(i);
    unitTable->setRowCount(nbUnits);
}

void UnitListPage::addUnit()
{
    modified = true;
    unitTable->insertRow(unitTable->rowCount());

    for(int i=0;i<unitTable->columnCount();++i) {
        UnitTableItem* item = new UnitTableItem("");
        unitTable->setItem(unitTable->rowCount() - 1,i,item);
    }
    guard.remember();
}


void UnitListPage::unitChanged(int row,int column)
{
    // An invalid entry is put back to its last good value with the reason
    // shown at the cell; nothing holds the focus (CellGuard).
    if(!guard.accept(row, column))
        return;
    // An edited cell is a change to the session (setUnits blocks this slot
    // while loading).
    modified = true;
}


