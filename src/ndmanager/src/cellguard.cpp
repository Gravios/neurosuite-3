/* cellguard.cpp -- see cellguard.h. */
#include "cellguard.h"

#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolTip>

void CellGuard::remember()
{
    good.clear();
    for (int r = 0; r < table->rowCount(); ++r)
        for (int c = 0; c < table->columnCount(); ++c)
            remember(r, c);
}

void CellGuard::remember(int row, int column)
{
    if (const QTableWidgetItem* item = table->item(row, column))
        good.insert(item, item->text());
}

bool CellGuard::accept(int row, int column)
{
    QTableWidgetItem* item = table->item(row, column);
    if (!item) return true;
    const QString why = check ? check(row, column, item->text()) : QString();
    // A cell never recorded (a row a page added without remember(), an item
    // Qt created for an edit) has no good value to go back to: accept it
    // rather than blank it.
    if (why.isEmpty() || !good.contains(item)) {
        good.insert(item, item->text());
        return true;
    }
    rejection = why;
    {
        const QSignalBlocker blocker(table);
        item->setText(good.value(item));
    }
    QToolTip::showText(table->viewport()->mapToGlobal(table->visualItemRect(item).bottomLeft()),
                       why, table);
    return false;
}
