/* cellguard.h -- keeps a table's cells valid without holding the focus.
 *
 * The group, file-mapping and unit tables used to answer an invalid entry by
 * trapping the curator in the cell: an event filter swallowed every event on
 * the table (Tab included) and every navigation snapped back until the cell
 * was fixed, and on the spike page a valid pattern once froze the table that
 * way.  CellGuard answers it instead by putting the cell back to its last good
 * value and saying why next to the cell; focus and Tab are never held.
 *
 * Use: give it the table and a check that returns an empty string for a valid
 * text and the reason otherwise.  Call accept(row, column) from the table's
 * cellChanged handler before acting on the edit (it returns false when it put
 * the cell back), and remember() after filling, adding or removing rows.
 */
#ifndef NDMANAGER_CELLGUARD_H
#define NDMANAGER_CELLGUARD_H

#include <QHash>
#include <QString>
#include <functional>

class QTableWidget;
class QTableWidgetItem;

class CellGuard {
public:
    using Check = std::function<QString(int row, int column, const QString& text)>;

    CellGuard(QTableWidget* table, Check check) : table(table), check(std::move(check)) {}

    /** Records every cell's current text as its last good value. */
    void remember();
    /** Records one cell's current text as its last good value. */
    void remember(int row, int column);

    /** Validates the cell just edited.  Valid: remembers it, returns true.
     *  Invalid: puts the last good text back (without emitting cellChanged),
     *  shows the reason next to the cell, returns false.  A cell with no
     *  remembered value is accepted (there is nothing to put back). */
    bool accept(int row, int column);

    /** The reason the last rejected entry was refused (empty if none). */
    QString lastRejection() const { return rejection; }

private:
    QTableWidget* table;
    Check check;
    QHash<const QTableWidgetItem*, QString> good;
    QString rejection;
};

#endif // NDMANAGER_CELLGUARD_H
