#include "parametertree.h"
#include <QTreeWidget>
#include <QKeyEvent>

ParameterTreeItem::ParameterTreeItem(QTreeWidget *parent)
    : QTreeWidgetItem(parent), mWidget(0)
{
}

ParameterTreeItem::ParameterTreeItem(QTreeWidgetItem *parent)
    : QTreeWidgetItem(parent), mWidget(0)
{

}

ParameterTreeItem::~ParameterTreeItem()
{

}

void ParameterTreeItem::setWidget(QWidget *w)
{
    mWidget = w;
}

QWidget *ParameterTreeItem::widget() const
{
    return mWidget;
}


ParameterTree::ParameterTree(QWidget *parent)
    : QTreeWidget(parent)
{
    connect(this, &QTreeWidget::itemClicked, this, &ParameterTree::slotItemClicked);
    connect(this, &QTreeWidget::itemPressed, this, &ParameterTree::slotItemClicked);
    connect(this, &QTreeWidget::itemSelectionChanged, this, &ParameterTree::slotSelectionChanged);
}

ParameterTree::~ParameterTree()
{
}

QTreeWidgetItem* ParameterTree::addPage(const QString &icon, const QString &name, QWidget *page)
{
    ParameterTreeItem *item = new ParameterTreeItem(this);
    item->setIcon(0,QIcon(icon));
    item->setText(0,name);
    item->setWidget(page);
    return item;
}

QTreeWidgetItem* ParameterTree::addSubPage(QTreeWidgetItem *parentItem, const QString &name, QWidget *page)
{
    ParameterTreeItem *item = new ParameterTreeItem(parentItem);
    item->setText(0,name);
    item->setWidget(page);
    return item;
}

void ParameterTree::slotItemClicked(QTreeWidgetItem*item,int)
{
    if (item) {
        ParameterTreeItem *parameterItem = static_cast<ParameterTreeItem*>(item);
        Q_EMIT showWidgetPage(parameterItem->widget());
    }
}

void ParameterTree::slotSelectionChanged()
{
    QTreeWidgetItem *item = currentItem();
    if (item) {
        ParameterTreeItem *parameterItem = static_cast<ParameterTreeItem*>(item);
        Q_EMIT showWidgetPage(parameterItem->widget());
    }
}

void ParameterTree::keyPressEvent(QKeyEvent *event)
{
    // Map PageUp / PageDown to "previous / next top-level section".
    // The tree only nests one level deep (Plugins -> per-plugin subpages),
    // so the navigation rule is simple: find the top-level ancestor of the
    // current item and step to its sibling.  When no item is selected we
    // pick the first / last top-level entry as a starting point so the
    // shortcut works on a freshly-opened window with nothing focused yet.
    if (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_PageUp) {
        const int n = topLevelItemCount();
        if (n == 0) {
            QTreeWidget::keyPressEvent(event);
            return;
        }

        // Resolve the current top-level ancestor.  QTreeWidgetItem::parent()
        // returns null for top-level entries, so walk upward until we hit
        // one — that guarantees we step between General Information /
        // Acquisition System / ... even when the user is sitting on a
        // Plugin subpage, instead of stepping to a sibling Plugin entry.
        int currentTopIdx = -1;
        if (QTreeWidgetItem *cur = currentItem()) {
            QTreeWidgetItem *top = cur;
            while (top->parent())
                top = top->parent();
            currentTopIdx = indexOfTopLevelItem(top);
        }

        int nextIdx;
        if (event->key() == Qt::Key_PageDown) {
            // No selection -> first; otherwise next, clamped at last.
            nextIdx = (currentTopIdx < 0) ? 0
                                          : qMin(currentTopIdx + 1, n - 1);
        } else {
            // No selection -> last; otherwise prev, clamped at first.
            nextIdx = (currentTopIdx < 0) ? n - 1
                                          : qMax(currentTopIdx - 1, 0);
        }

        if (nextIdx != currentTopIdx) {
            QTreeWidgetItem *target = topLevelItem(nextIdx);
            if (target) {
                // setCurrentItem fires itemSelectionChanged, which routes
                // through slotSelectionChanged and emits showWidgetPage —
                // so the central QStackedWidget swaps to the new section
                // without any extra wiring here.
                setCurrentItem(target);
            }
        }
        event->accept();
        return;
    }

    QTreeWidget::keyPressEvent(event);
}
