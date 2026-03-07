#include "parametertree.h"
#include <QTreeWidget>

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
