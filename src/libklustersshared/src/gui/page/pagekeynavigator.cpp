// pagekeynavigator.cpp -- see pagekeynavigator.h.

#include "pagekeynavigator.h"

#include "qpagedialog.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QScrollArea>
#include <QTextEdit>
#include <QTimer>
#include <QTreeView>

namespace {

// An editor that records the keys pressed in it: Enter and Esc are its to take.
bool recordsKeys(const QWidget* w)
{
    for (; w; w = w->parentWidget()) {
        if (qobject_cast<const QKeySequenceEdit*>(w)) return true;
        const QByteArray cls = w->metaObject()->className();
        if (cls == "ButtonChordEdit" || cls == "WheelChordEdit") return true;
        if (w->isWindow()) break;
    }
    return false;
}

// Qt's own rule for a set of exclusive radio buttons: Tab stops at the checked one
// (the arrows move within the set), or at each when none is checked.
bool isUncheckedOfACheckedSet(const QWidget* w)
{
    const QRadioButton* rb = qobject_cast<const QRadioButton*>(w);
    if (!rb || !rb->autoExclusive() || rb->isChecked() || !rb->parentWidget()) return false;
    const QList<QRadioButton*> set = rb->parentWidget()->findChildren<QRadioButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (const QRadioButton* other : set)
        if (other != rb && other->autoExclusive() && other->isChecked()) return true;
    return false;
}

// The widgets under @p root that Tab stops at, in the order of Qt's focus chain.
QList<QWidget*> stopsUnder(QWidget* root)
{
    QList<QWidget*> out;
    if (!root) return out;
    QWidget* w = root;
    do {
        if (w != root && root->isAncestorOf(w) && w->isVisibleTo(root) && w->isEnabled()
            && (w->focusPolicy() & Qt::TabFocus) && !w->focusProxy() && !isUncheckedOfACheckedSet(w)
            && !qobject_cast<QScrollArea*>(w))            // a scrolling frame is not a setting
            out.append(w);
        w = w->nextInFocusChain();
    } while (w && w != root);
    return out;
}

} // namespace

PageKeyNavigator::PageKeyNavigator(QPageDialog* dlg, bool withHint)
    : QObject(dlg), dialog(dlg)
{
    // The application's filter: Tab and Enter are taken by a setting before the dialog
    // would see them.  Only keys whose receiver is this dialog's focus widget are looked at.
    qApp->installEventFilter(this);
    if (withHint) {
        if (QBoxLayout* box = qobject_cast<QBoxLayout*>(dialog->layout())) {
            QLabel* hint = new QLabel(tr("Enter: into the page  ·  Tab / Shift+Tab: next / previous setting  ·  "
                                         "Space: toggle  ·  Esc: back to the list"), dialog);
            hint->setObjectName(QStringLiteral("pageKeyHint"));
            hint->setEnabled(false);                 // the muted text colour
            QFont f = hint->font();
            f.setPointSizeF(f.pointSizeF() * 0.9);
            hint->setFont(f);
            const int at = dialog->findChild<QDialogButtonBox*>() ? box->count() - 1 : box->count();
            box->insertWidget(qMax(0, at), hint);
        }
    }
}

QAbstractItemView* PageKeyNavigator::pageList() const
{
    const QList<QAbstractItemView*> views = dialog->findChildren<QAbstractItemView*>();
    for (QAbstractItemView* v : views) {
        // KDEPrivate::QPageTreeView and its three siblings (qpageview_p.h); a list inside a page is not one.
        if (QByteArray(v->metaObject()->className()).startsWith("KDEPrivate::QPage")) return v;
    }
    return nullptr;
}

QWidget* PageKeyNavigator::pageRoot() const
{
    QPageWidgetItem* item = dialog->currentPage();
    return item ? item->widget() : nullptr;
}

bool PageKeyNavigator::inPage(const QWidget* w) const
{
    const QWidget* root = pageRoot();
    return root && w && (w == root || root->isAncestorOf(w));
}

QList<QWidget*> PageKeyNavigator::pageStops() const { return stopsUnder(pageRoot()); }

QList<QWidget*> PageKeyNavigator::listStops() const
{
    QList<QWidget*> out;
    if (QAbstractItemView* list = pageList()) out.append(list);
    if (QDialogButtonBox* buttons = dialog->findChild<QDialogButtonBox*>()) out += stopsUnder(buttons);
    return out;
}

void PageKeyNavigator::step(const QList<QWidget*>& stops, QWidget* from, int by)
{
    if (stops.isEmpty()) return;
    qsizetype at = -1;
    for (QWidget* w = from; w && at < 0; w = w->parentWidget()) {     // a part of a setting counts as the setting
        at = stops.indexOf(w);
        if (w->isWindow()) break;
    }
    const qsizetype n = stops.size();
    const qsizetype next = at < 0 ? (by > 0 ? 0 : n - 1) : ((at + by) % n + n) % n;
    QWidget* w = stops.at(next);
    w->setFocus(by > 0 ? Qt::TabFocusReason : Qt::BacktabFocusReason);
    for (QWidget* p = w->parentWidget(); p; p = p->parentWidget()) {
        if (QScrollArea* area = qobject_cast<QScrollArea*>(p)) { area->ensureWidgetVisible(w); break; }
        if (p->isWindow()) break;
    }
}

bool PageKeyNavigator::enterPage()
{
    const QList<QWidget*> stops = pageStops();
    if (stops.isEmpty()) return false;
    step(stops, nullptr, +1);
    return true;
}

void PageKeyNavigator::leavePage()
{
    if (QAbstractItemView* list = pageList()) list->setFocus(Qt::OtherFocusReason);
}

bool PageKeyNavigator::handleKey(QWidget* focus, int key, Qt::KeyboardModifiers mods)
{
    const bool plain = !(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool tab = plain && (key == Qt::Key_Tab || key == Qt::Key_Backtab);
    const int by = (key == Qt::Key_Backtab || (mods & Qt::ShiftModifier)) ? -1 : +1;
    const bool enter = plain && (key == Qt::Key_Return || key == Qt::Key_Enter);
    const bool escape = plain && key == Qt::Key_Escape;

    if (inPage(focus)) {
        if (tab) { step(pageStops(), focus, by); return true; }
        if (recordsKeys(focus)) return false;                     // Enter and Esc are keys it can record
        if (escape) { leavePage(); return true; }
        if (!enter) return false;
        // Enter acts on the setting and never reaches the dialog's default button.
        for (QWidget* w = focus; w && inPage(w); w = w->parentWidget()) {
            if (QAbstractButton* b = qobject_cast<QAbstractButton*>(w)) { b->click(); return true; }
            if (QAbstractSpinBox* s = qobject_cast<QAbstractSpinBox*>(w)) { s->interpretText(); s->selectAll(); return true; }
            if (QComboBox* c = qobject_cast<QComboBox*>(w)) {
                if (c->isEditable()) return false;                // its line edit commits the text
                c->showPopup();
                return true;
            }
            if (qobject_cast<QAbstractItemView*>(w) || qobject_cast<QTextEdit*>(w) || qobject_cast<QPlainTextEdit*>(w))
                return false;                                     // a list activates its row, a text takes the line
        }
        return true;
    }

    QAbstractItemView* list = pageList();
    const bool inList = list && (focus == list || list->isAncestorOf(focus));
    const QList<QWidget*> round = listStops();
    bool onRound = inList;
    for (QWidget* w : round) onRound = onRound || w == focus;
    if (!onRound) return false;                                   // a help button, a splitter: Qt's own
    if (tab) { step(round, inList ? list : focus, by); return true; }
    if (inList && enter) {
        if (enterPage()) return true;
        // A group without settings: open it and go to its first page.
        const QModelIndex cur = list->currentIndex();
        const QAbstractItemModel* model = list->model();
        if (cur.isValid() && model && model->rowCount(cur) > 0) {
            if (QTreeView* tree = qobject_cast<QTreeView*>(list)) tree->expand(cur);
            list->setCurrentIndex(model->index(0, 0, cur));
        }
        return true;
    }
    return false;
}

bool PageKeyNavigator::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == dialog && event->type() == QEvent::Show) {
        // The dialog opens on its page list -- after QDialog has given the focus to its default button.
        QTimer::singleShot(0, this, &PageKeyNavigator::leavePage);
        return false;
    }
    if (event->type() != QEvent::KeyPress) return false;
    QWidget* w = qobject_cast<QWidget*>(watched);
    if (!w || w->window() != dialog || w != dialog->focusWidget() || !dialog->isVisible()) return false;
    QKeyEvent* ke = static_cast<QKeyEvent*>(event);
    return handleKey(w, ke->key(), ke->modifiers());
}
