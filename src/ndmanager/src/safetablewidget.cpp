// safetablewidget.cpp — explicit translation unit so automoc generates
// the vtable and meta-object for SafeTableWidget. Header-only Q_OBJECT
// classes with no .cpp counterpart produce "undefined reference to vtable"
// at link time because the compiler emits the vtable in the first translation
// unit that includes the header, but automoc only runs on .cpp files.
#include "safetablewidget.h"

#include <QHeaderView>
#include <QPair>

bool SafeTableWidget::atEdge(bool forward)
{
    const QModelIndex current = currentIndex();
    if (!current.isValid()) return false;
    // Ask the table where Tab would go.  It skips hidden, disabled and
    // unselectable cells and wraps around at the end; a wrap (or nowhere to
    // go) means the current cell is the last one Tab visits in that direction.
    const QModelIndex next = QTableWidget::moveCursor(forward ? MoveNext : MovePrevious, Qt::NoModifier);
    if (!next.isValid() || next == current) return true;
    auto pos = [this](const QModelIndex& i) {
        return qMakePair(verticalHeader()->visualIndex(i.row()),
                         horizontalHeader()->visualIndex(i.column()));
    };
    return forward ? pos(next) < pos(current) : pos(current) < pos(next);
}

QModelIndex SafeTableWidget::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    // At the edge, no next cell: QAbstractItemView then lets the Tab go to
    // the next widget in the focus chain instead of wrapping around.
    if ((cursorAction == MoveNext || cursorAction == MovePrevious) && tabKeyNavigation()
            && atEdge(cursorAction == MoveNext))
        return QModelIndex();
    return QTableWidget::moveCursor(cursorAction, modifiers);
}

void SafeTableWidget::closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint)
{
    const bool next = hint == QAbstractItemDelegate::EditNextItem;
    const bool previous = hint == QAbstractItemDelegate::EditPreviousItem;
    const bool leave = (next || previous) && tabKeyNavigation()
                       && atEdge(next);
    QTableWidget::closeEditor(editor, leave ? QAbstractItemDelegate::NoHint : hint);
    if (leave)
        QWidget::focusNextPrevChild(next);
}
