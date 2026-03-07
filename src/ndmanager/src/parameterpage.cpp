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
#include "parameterpage.h"
#include <QTableWidget>
#include <QLineEdit>
#include <QSpinBox>

// include files for QT
#include <algorithm>
#include <QWidget>

#include <QPushButton>

#include <QLineEdit>

#include <QComboBox>

#include <QEvent>
#include <QVector>
#include <QList>
#include <QDebug>
#include <QStyledItemDelegate>
#include <QAbstractItemView>

ParameterPage::ParameterPage(bool expertMode,QWidget *parent)
    : ParameterLayout(parent),
      valueModified(false),
      descriptionModified(false),
      mExpertMode(expertMode)
{
    status<<tr("Mandatory")<<tr("Optional")<<tr("Dynamic");
    ddList.append(2);
    parameterTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    //If the export mode is not set, only the value column is editable
    if (!expertMode) {
        // Note: read-only enforcement for non-expert mode is done via item flags
        // in setParameterInformation() — Qt6 crashes if createEditor() returns nullptr.
        addButton->setEnabled(false);
        removeButton->setEnabled(false);
        nameLineEdit->setReadOnly(true);
    } else {
        connect(addButton, &QAbstractButton::clicked, this, &ParameterPage::addParameter);
        connect(removeButton, &QAbstractButton::clicked, this, &ParameterPage::removeParameter);
        connect(nameLineEdit, &QLineEdit::returnPressed, this, &ParameterPage::changeCaption);
        connect(nameLineEdit, &QLineEdit::editingFinished, this, &ParameterPage::changeCaption);
    }

    // Restrict edit triggers to DoubleClicked only.
    // The Qt default includes AnyKeyPressed which immediately opens an editor on any
    // keystroke while a row is selected — before the editor is fully initialised,
    // causing a crash in Qt6 when item(row,col) is null or the selection model
    // fires a re-entrant update. DoubleClicked gives controlled, explicit entry.
    parameterTable->setEditTriggers(QAbstractItemView::DoubleClicked);

    // Track modifications via itemChanged — works correctly for both mouse and keyboard
    // in Qt6. The old eventFilter + KeyRelease hack consumed events that Qt6 needs
    // internally to commit cell editors, causing a crash on data entry.
    connect(parameterTable, &QTableWidget::itemChanged, this, &ParameterPage::itemModified);
}


ParameterPage::~ParameterPage(){}

void ParameterPage::itemModified(QTableWidgetItem* item){
    if(!item) return;
    if(item->column() == 1)
        valueModified = true;
    else
        descriptionModified = true;
}

QMap<int, QStringList > ParameterPage::getParameterInformation(){
    QMap<int, QStringList > parameterInformation;
    int paramNb = 0;  // 0-based to match QTableWidget row indices
    for(int i =0; i<parameterTable->rowCount();++i){
        QStringList information;
        QTableWidgetItem* col0 = parameterTable->item(i,0);
        if(!col0) continue;
        QString name = col0->text().simplified();
        if(name == " ")
            continue;
        information.append(name);
        for(int j = 1;j < parameterTable->columnCount(); ++j){
            QString text;
            if(ddList.contains(j)) {
                QComboBox* combo = static_cast<QComboBox*>(parameterTable->cellWidget(i,j));
                if(combo) text = combo->currentText();
            }
            else {
                QTableWidgetItem* cell = parameterTable->item(i,j);
                if(cell) text = cell->text();
            }
            information.append(text.simplified());
        }

        parameterInformation.insert(paramNb,information);
        paramNb++;
    }
    return parameterInformation;
}

void ParameterPage::setParameterInformation(const QMap<int, QStringList >& parameters){
    // Block signals during repopulation to prevent itemChanged firing on intermediate
    // states (e.g. clearContents leaving null items that itemModified might see).
    parameterTable->blockSignals(true);

    // Clear selection and reset current index BEFORE clearContents.
    // Qt6 crashes if the current index points to a cell with a null item when
    // an editor is subsequently opened — clearContents nulls all items but
    // leaves the current index pointing at the old position.
    parameterTable->clearSelection();
    parameterTable->setCurrentIndex(QModelIndex());

    // Remove all cellWidgets explicitly — clearContents() does NOT remove them in Qt6,
    // leaving orphaned widgets that mismatch the new backing items on reload.
    for(int r = 0; r < parameterTable->rowCount(); ++r)
        for(int c = 0; c < parameterTable->columnCount(); ++c)
            if(parameterTable->cellWidget(r,c))
                parameterTable->removeCellWidget(r,c);

    parameterTable->clearContents();
    parameterTable->setRowCount(parameters.count());

    // Use a 0-based row counter rather than iterator.key() — keys from
    // getParameterInformation() are 1-based, which would leave row 0 uninitialised
    // and write beyond the last row, causing a crash in Qt6.
    int row = 0;
    QMap<int,QStringList >::ConstIterator iterator;
    //The iterator gives the keys sorted.
    for(iterator = parameters.constBegin(); iterator != parameters.constEnd(); ++iterator, ++row){
        const QStringList parameterInfo = iterator.value();

        for(int i=0;i<(int)parameterInfo.count() && i<parameterTable->columnCount();++i){
            if(ddList.contains(i)){
                // Set a backing item first so item(row,i) is never null.
                // Qt6's selection model dereferences item() for every column during
                // selection/double-click events, even for cellWidget columns.
                // Backing item for combo column is ALWAYS non-editable —
                // interaction is via the cellWidget (QComboBox), not the delegate.
                // If the backing item were editable, the default QStyledItemDelegate
                // would open a QLineEdit on top of the combo on double-click,
                // then crash when committing (model vs cellWidget conflict).
                QTableWidgetItem* backing = new QTableWidgetItem();
                backing->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
                parameterTable->setItem(row, i, backing);
                QComboBox *combo = createCombobox();
                // Set combo to the loaded status value, not just index 0.
                int idx = combo->findText(parameterInfo[i]);
                if(idx >= 0) combo->setCurrentIndex(idx);
                parameterTable->setCellWidget(row,i,combo);
                connect(combo, QOverload<int>::of(&QComboBox::activated), this, &ParameterPage::slotValueModified);
            } else {
                QTableWidgetItem *item = new QTableWidgetItem(parameterInfo[i]);
                // In non-expert mode, only the value column (1) is editable.
                // Enforce this via item flags — returning nullptr from createEditor
                // crashes Qt6, so we can't use a delegate for this purpose.
                if(!mExpertMode && i != 1){
                    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                }
                parameterTable->setItem(row,i,item);
            }
        }
    }//end of parameters loop

    parameterTable->blockSignals(false);
}


void ParameterPage::addParameter(){
    descriptionModified = true;
    // Capture row index once — rowCount() increments after insertRow.
    int row = parameterTable->rowCount();
    parameterTable->insertRow(row);

    parameterTable->setItem(row, 0, new QTableWidgetItem());
    parameterTable->setItem(row, 1, new QTableWidgetItem());

    // Col 2: set a backing QTableWidgetItem (non-editable) alongside the cellWidget.
    // Qt6's selection/mime machinery calls item(row,col) for every column during
    // selection updates triggered by double-click — if item() returns nullptr it crashes.
    QTableWidgetItem* statusItem = new QTableWidgetItem();
    statusItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    parameterTable->setItem(row, 2, statusItem);
    parameterTable->setCellWidget(row, 2, createCombobox());
}

QComboBox *ParameterPage::createCombobox()
{
    QComboBox *combo = new QComboBox;
    combo->addItems(status);
    combo->setEnabled(mExpertMode);
    return combo;
}

void ParameterPage::removeParameter(){
    descriptionModified = true;
    const QList<QTableWidgetSelectionRange> range = parameterTable->selectedRanges();
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
            parameterTable->removeRow(lst.at(i));
        }
    }
}

void ParameterPage::changeCaption()
{
    const QString name = nameLineEdit->text();
    if(name.isEmpty() && !name.contains("New Script-"))
        emit nameChanged(tr("Unknown"));
    else
        emit nameChanged(name);
}



