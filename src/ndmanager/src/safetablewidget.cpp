// safetablewidget.cpp — explicit translation unit so automoc generates
// the vtable and meta-object for SafeTableWidget. Header-only Q_OBJECT
// classes with no .cpp counterpart produce "undefined reference to vtable"
// at link time because the compiler emits the vtable in the first translation
// unit that includes the header, but automoc only runs on .cpp files.
#include "safetablewidget.h"

#include <QHeaderView>

QModelIndex SafeTableWidget::edgeIndex(bool last) const
{
    auto pick = [last](const QHeaderView* h, auto hidden) {
        const int n = h->count();
        for (int i = 0; i < n; ++i) {
            const int logical = h->logicalIndex(last ? n - 1 - i : i);
            if (!hidden(logical)) return logical;
        }
        return -1;
    };
    const int column = pick(horizontalHeader(), [this](int c) { return isColumnHidden(c); });
    const int row = pick(verticalHeader(), [this](int r) { return isRowHidden(r); });
    if (row < 0 || column < 0) return QModelIndex();
    return model()->index(row, column);
}

QModelIndex SafeTableWidget::moveCursor(CursorAction cursorAction, Qt::KeyboardModifiers modifiers)
{
    if ((cursorAction == MoveNext || cursorAction == MovePrevious) && tabKeyNavigation()) {
        const QModelIndex current = currentIndex();
        // At the edge, no next cell: QAbstractItemView then lets the Tab go to
        // the next widget in the focus chain.
        if (current.isValid() && current == edgeIndex(cursorAction == MoveNext))
            return QModelIndex();
    }
    return QTableWidget::moveCursor(cursorAction, modifiers);
}

void SafeTableWidget::closeEditor(QWidget* editor, QAbstractItemDelegate::EndEditHint hint)
{
    const bool next = hint == QAbstractItemDelegate::EditNextItem;
    const bool previous = hint == QAbstractItemDelegate::EditPreviousItem;
    const bool leave = (next || previous) && tabKeyNavigation()
                       && currentIndex() == edgeIndex(next);
    QTableWidget::closeEditor(editor, leave ? QAbstractItemDelegate::NoHint : hint);
    if (leave)
        QWidget::focusNextPrevChild(next);
}
