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
#include "parametertype.h"
#include <parametervaluedelegate.h>

#include <algorithm>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDebug>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>

// The table's columns.  Type, default and hidden have no column: they are
// kept on the name cell (ParameterType::Role) and returned after the status.
namespace {
enum Column { NameColumn = 0, ValueColumn = 1, StatusColumn = 2 };
const int kExtraFields = 3;   // type, default, hidden
}

ParameterPage::ParameterPage(bool expertMode,QWidget *parent)
    : ParameterLayout(parent),
      valueModified(false),
      descriptionModified(false),
      mExpertMode(expertMode),
      guard(parameterTable, [this](int row, int column, const QString& text) {
          return column == ValueColumn ? typeOfRow(row).check(text) : QString();
      })
{
    status<<tr("Mandatory")<<tr("Optional")<<tr("Dynamic");
    ddList.append(StatusColumn);
    parameterTable->horizontalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    //If the export mode is not set, only the value column is editable
    if (!expertMode) {
        // Note: read-only enforcement for non-expert mode is done via item flags
        // in setParameterInformation() — Qt6 crashes if createEditor() returns nullptr.
        addButton->setEnabled(false);
        removeButton->setEnabled(false);
        moveUpButton->setEnabled(false);
        moveDownButton->setEnabled(false);
        nameLineEdit->setReadOnly(true);
    } else {
        connect(addButton, &QAbstractButton::clicked, this, &ParameterPage::addParameter);
        connect(removeButton, &QAbstractButton::clicked, this, &ParameterPage::removeParameter);
        connect(moveUpButton, &QAbstractButton::clicked, this, &ParameterPage::moveParameterUp);
        connect(moveDownButton, &QAbstractButton::clicked, this, &ParameterPage::moveParameterDown);
        connect(nameLineEdit, &QLineEdit::returnPressed, this, &ParameterPage::changeCaption);
        connect(nameLineEdit, &QLineEdit::editingFinished, this, &ParameterPage::changeCaption);
    }

    // Edited like a spreadsheet: double-click, F2, or start typing over the
    // value.  Every cell has an item (the status column a read-only backing
    // item under its combo), so an editor never opens on a null item -- the
    // Qt 6 crash that once restricted this to DoubleClicked.
    parameterTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                    | QAbstractItemView::EditKeyPressed
                                    | QAbstractItemView::AnyKeyPressed);
    // Value cells get an editor that matches the parameter's declared type.
    parameterTable->setItemDelegateForColumn(ValueColumn, new ParameterValueDelegate(parameterTable));

    // Track modifications via itemChanged — works correctly for both mouse and keyboard
    // in Qt6. The old eventFilter + KeyRelease hack consumed events that Qt6 needs
    // internally to commit cell editors, causing a crash on data entry.
    connect(parameterTable, &QTableWidget::itemChanged, this, &ParameterPage::itemModified);
}


ParameterPage::~ParameterPage(){}

ParameterType ParameterPage::typeOfRow(int row) const
{
    const QTableWidgetItem* name = parameterTable->item(row, NameColumn);
    return ParameterType::parse(name ? name->data(ParameterType::TypeRole).toString() : QString());
}

void ParameterPage::describeRow(int row)
{
    QTableWidgetItem* name = parameterTable->item(row, NameColumn);
    QTableWidgetItem* value = parameterTable->item(row, ValueColumn);
    if (!name || !value) return;
    QStringList tip;
    const QString kind = typeOfRow(row).describe();
    if (!kind.isEmpty()) tip << kind;
    const QString def = name->data(ParameterType::DefaultRole).toString();
    if (!def.isEmpty()) tip << tr("default: %1").arg(def);
    const bool wasBlocked = parameterTable->blockSignals(true);
    value->setToolTip(tip.join(QStringLiteral("; ")));
    parameterTable->blockSignals(wasBlocked);
}

void ParameterPage::itemModified(QTableWidgetItem* item){
    if(!item) return;
    const int row = item->row();
    if(item->column() == ValueColumn) {
        // A value the declared type does not take is put back (with the
        // reason beside the cell) rather than saved.
        if(!guard.accept(row, ValueColumn)) return;
        valueModified = true;
        // Look up the name (column 0) for the same row and emit the
        // value-changed signal so the pipeline designer (and any other
        // listener) can react.  Guard against a missing name cell.
        QTableWidgetItem* nameItem = parameterTable->item(row, NameColumn);
        if(nameItem) {
            emit parameterValueChanged(nameItem->text(), item->text());
        }
    } else {
        descriptionModified = true;
    }
}

bool ParameterPage::setParameterValue(const QString& parameterName,
                                       const QString& newValue,
                                       bool createIfMissing){
    // Linear scan — the parameter table is short (typically <30 rows).
    for(int row = 0; row < parameterTable->rowCount(); ++row) {
        QTableWidgetItem* nameItem = parameterTable->item(row, NameColumn);
        if(!nameItem || nameItem->text() != parameterName) continue;
        QTableWidgetItem* valueItem = parameterTable->item(row, ValueColumn);
        if(!valueItem) {
            valueItem = new QTableWidgetItem();
            parameterTable->setItem(row, ValueColumn, valueItem);
        }
        if(valueItem->text() == newValue) return false;
        // The pipeline inspector edits plain text: a value this row's type
        // does not take is not written (it would be saved, and then become
        // the value a later bad edit is put back to).
        const QString why = typeOfRow(row).check(newValue);
        if(!why.isEmpty()) {
            qWarning() << "ParameterPage:" << getProgramName() << parameterName
                       << "refused" << newValue << "-" << why;
            return false;
        }
        // Block the itemChanged → itemModified → parameterValueChanged
        // signal chain so we don't bounce back to whoever called us.
        const bool wasBlocked = parameterTable->blockSignals(true);
        valueItem->setText(newValue);
        parameterTable->blockSignals(wasBlocked);
        guard.remember(row, ValueColumn);
        valueModified = true;
        return true;
    }
    if(!createIfMissing) return false;
    // Append a new optional row.  Match the layout addParameter() uses.
    descriptionModified = true;
    const bool wasBlocked = parameterTable->blockSignals(true);
    const int row = appendRow(parameterName, newValue, QStringLiteral("Optional"));
    parameterTable->blockSignals(wasBlocked);
    guard.remember(row, ValueColumn);
    valueModified = true;
    return true;
}

QMap<int, QStringList > ParameterPage::getParameterInformation(){
    QMap<int, QStringList > parameterInformation;
    int paramNb = 0;  // 0-based to match QTableWidget row indices
    for(int i =0; i<parameterTable->rowCount();++i){
        QStringList information;
        QTableWidgetItem* col0 = parameterTable->item(i, NameColumn);
        if(!col0) continue;
        QString name = col0->text().simplified();
        if(name.isEmpty())
            continue;  // a blank row is not a parameter
        information.append(name);
        for(int j = 1;j < parameterTable->columnCount(); ++j){
            QString text;
            if(ddList.contains(j)) {
                QComboBox* combo = qobject_cast<QComboBox*>(parameterTable->cellWidget(i,j));
                if(combo) text = combo->currentText();
            }
            else {
                QTableWidgetItem* cell = parameterTable->item(i,j);
                if(cell) text = cell->text();
            }
            information.append(text.simplified());
        }
        // type, default, hidden: positional, so only up to the last one set
        // (a description without them reads back exactly as it was loaded).
        QStringList extra;
        int last = -1;
        for(int k = 0; k < kExtraFields; ++k){
            extra << col0->data(ParameterType::TypeRole + k).toString();
            if(!extra.at(k).isEmpty()) last = k;
        }
        for(int k = 0; k <= last; ++k) information.append(extra.at(k));

        parameterInformation.insert(paramNb,information);
        paramNb++;
    }
    return parameterInformation;
}

int ParameterPage::appendRow(const QString& name, const QString& value, const QString& statusText,
                             const QStringList& extra)
{
    const int row = parameterTable->rowCount();
    parameterTable->insertRow(row);

    QTableWidgetItem* nameItem = new QTableWidgetItem(name);
    for(int k = 0; k < kExtraFields && k < extra.size(); ++k)
        nameItem->setData(ParameterType::TypeRole + k, extra.at(k));
    QTableWidgetItem* valueItem = new QTableWidgetItem(value);
    // In non-expert mode only the value is editable.  Enforced through the
    // item flags: returning nullptr from createEditor crashes Qt 6.
    if(!mExpertMode) nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    parameterTable->setItem(row, NameColumn, nameItem);
    parameterTable->setItem(row, ValueColumn, valueItem);

    // The status column: a non-editable backing item under the combo.
    // Qt 6's selection machinery calls item(row, col) for every column, so it
    // must never be null; and were it editable, the default delegate would
    // open a line edit over the combo on double-click.
    QTableWidgetItem* statusItem = new QTableWidgetItem();
    statusItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    parameterTable->setItem(row, StatusColumn, statusItem);
    QComboBox* combo = createCombobox();
    const int idx = combo->findText(statusText);
    if(idx >= 0) combo->setCurrentIndex(idx);
    parameterTable->setCellWidget(row, StatusColumn, combo);

    // hidden: shown in expert mode, where the description is edited.
    if(!mExpertMode && ParameterType::isTrue(extra.value(2)))
        parameterTable->setRowHidden(row, true);
    describeRow(row);
    return row;
}

void ParameterPage::setParameterInformation(const QMap<int, QStringList >& parameters){
    // Block signals during repopulation to prevent itemChanged firing on intermediate
    // states (e.g. clearContents leaving null items that itemModified might see).
    parameterTable->blockSignals(true);

    // Clear selection and reset current index BEFORE clearing.
    // Qt6 crashes if the current index points to a cell with a null item when
    // an editor is subsequently opened.
    parameterTable->clearSelection();
    parameterTable->setCurrentIndex(QModelIndex());

    // Remove all cellWidgets explicitly — clearContents() does NOT remove them in Qt6,
    // leaving orphaned widgets that mismatch the new backing items on reload.
    for(int r = 0; r < parameterTable->rowCount(); ++r)
        for(int c = 0; c < parameterTable->columnCount(); ++c)
            if(parameterTable->cellWidget(r,c))
                parameterTable->removeCellWidget(r,c);
    parameterTable->clearContents();
    parameterTable->setRowCount(0);

    // Rows in key order (QMap iterates sorted); the keys themselves are not
    // row numbers (some callers number from 1).
    for(auto it = parameters.constBegin(); it != parameters.constEnd(); ++it){
        const QStringList& info = it.value();
        appendRow(info.value(0), info.value(1), info.value(2), info.mid(3));
    }

    parameterTable->blockSignals(false);
    guard.remember();
}


void ParameterPage::addParameter(){
    descriptionModified = true;
    const bool wasBlocked = parameterTable->blockSignals(true);
    const int row = appendRow(QString(), QString(), status.value(0));
    parameterTable->blockSignals(wasBlocked);
    guard.remember(row, ValueColumn);
    // Straight into the new name, so the row can be typed in at once.
    parameterTable->setCurrentCell(row, NameColumn);
    parameterTable->editItem(parameterTable->item(row, NameColumn));
}

QComboBox *ParameterPage::createCombobox()
{
    QComboBox *combo = new QComboBox;
    combo->addItems(status);
    combo->setEnabled(mExpertMode);
    // Every status combo, whether loaded or added, marks the page modified.
    connect(combo, QOverload<int>::of(&QComboBox::activated), this, &ParameterPage::slotValueModified);
    return combo;
}

void ParameterPage::removeParameter(){
    const QList<QTableWidgetSelectionRange> range = parameterTable->selectedRanges();
    if(range.isEmpty()) return;
    QList<int> lst;
    for (const QTableWidgetSelectionRange&r : range) {
        for(int val = r.topRow(); val <= r.bottomRow(); ++val)
            if(!lst.contains(val)) lst << val;
    }
    std::sort(lst.begin(), lst.end());
    descriptionModified = true;
    for(int i = lst.count()-1; i>=0; --i)
        parameterTable->removeRow(lst.at(i));
}

void ParameterPage::moveParameterUp(){ moveCurrentRow(-1); }
void ParameterPage::moveParameterDown(){ moveCurrentRow(+1); }

bool ParameterPage::moveCurrentRow(int step)
{
    const int from = parameterTable->currentRow();
    const int to = from + step;
    if(from < 0 || to < 0 || to >= parameterTable->rowCount()) return false;

    // Swap the items (each keeps its type/default/hidden and its last good
    // value in the guard) and the two combos' choices; the combos stay put.
    const bool wasBlocked = parameterTable->blockSignals(true);
    for(int c = 0; c < parameterTable->columnCount(); ++c){
        QTableWidgetItem* a = parameterTable->takeItem(from, c);
        QTableWidgetItem* b = parameterTable->takeItem(to, c);
        parameterTable->setItem(from, c, b);
        parameterTable->setItem(to, c, a);
    }
    QComboBox* ca = qobject_cast<QComboBox*>(parameterTable->cellWidget(from, StatusColumn));
    QComboBox* cb = qobject_cast<QComboBox*>(parameterTable->cellWidget(to, StatusColumn));
    if(ca && cb){
        const int ia = ca->currentIndex();
        ca->setCurrentIndex(cb->currentIndex());
        cb->setCurrentIndex(ia);
    }
    const bool hiddenFrom = parameterTable->isRowHidden(from);
    parameterTable->setRowHidden(from, parameterTable->isRowHidden(to));
    parameterTable->setRowHidden(to, hiddenFrom);
    parameterTable->blockSignals(wasBlocked);

    parameterTable->setCurrentCell(to, std::max(0, parameterTable->currentColumn()));
    descriptionModified = true;
    return true;
}

void ParameterPage::changeCaption()
{
    const QString name = nameLineEdit->text();
    if(name.isEmpty() && !name.contains("New Script-"))
        emit nameChanged(tr("Unknown"));
    else
        emit nameChanged(name);
}
