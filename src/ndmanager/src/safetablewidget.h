/*
 * SafeTableWidget — QTableWidget subclass that prevents the infinite recursion
 * crash on Qt6 + Wayland when inputMethodEvent re-enters edit() via setFocus().
 *
 * Root cause (confirmed by backtrace with 51,000 frames):
 *   QAbstractItemView::inputMethodEvent()
 *     -> edit() -> creates editor widget -> setFocus() on editor
 *     -> Wayland compositor sends InputMethod event back to the viewport
 *     -> inputMethodEvent() called again -> edit() -> setFocus() -> ...
 *
 * Fix: ignore inputMethodEvent entirely while an editor is already being opened.
 * The guard flag is reset after edit() returns so normal IME composition still
 * works between edits.
 */
#ifndef SAFETABLEWIDGET_H
#define SAFETABLEWIDGET_H

#include <QTableWidget>
#include <QInputMethodEvent>

class SafeTableWidget : public QTableWidget
{
    Q_OBJECT
public:
    explicit SafeTableWidget(QWidget *parent = nullptr)
        : QTableWidget(parent), editingInProgress(false) {}

    explicit SafeTableWidget(int rows, int columns, QWidget *parent = nullptr)
        : QTableWidget(rows, columns, parent), editingInProgress(false) {}

protected:
    void inputMethodEvent(QInputMethodEvent *event) override
    {
        // Re-entrancy guard: if we are already inside edit() (which called setFocus()
        // on the new editor, which caused Wayland to send another InputMethod event
        // back to us), drop the event. Without this guard, the call stack grows by
        // ~4 frames per event until a stack overflow segfault occurs.
        if (editingInProgress) {
            event->accept();
            return;
        }
        editingInProgress = true;
        QTableWidget::inputMethodEvent(event);
        editingInProgress = false;
    }

private:
    bool editingInProgress;
};

#endif // SAFETABLEWIDGET_H
