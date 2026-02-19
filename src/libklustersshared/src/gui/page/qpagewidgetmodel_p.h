/*  This file is part of the KDE project
    Copyright (C) 2007 Matthias Kretz <kretz@kde.org>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License version 2 as published by the Free Software Foundation.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

*/

#ifndef KPAGEWIDGETMODEL_P_H
#define KPAGEWIDGETMODEL_P_H

#include "qpagemodel_p.h"
#include "qpagewidgetmodel.h"

class PageItem
{
    public:
        explicit PageItem(QPageWidgetItem *pageItem, PageItem *parent = nullptr);
        ~PageItem();

        void appendChild(PageItem *child);
        void insertChild(int row, PageItem *child);
        void removeChild(int row);

        PageItem *child(int row);
        int childCount() const;
        int columnCount() const;
        int row() const;
        PageItem *parent();

        QPageWidgetItem *pageWidgetItem() const;

        PageItem *findChild(const QPageWidgetItem *item);

        void dump(int indent = 0);

    private:
        QPageWidgetItem *mPageWidgetItem;

        QList<PageItem*> mChildItems;
        PageItem *mParentItem;
};

class QPageWidgetModelPrivate : public QPageModelPrivate
{
    Q_DECLARE_PUBLIC(QPageWidgetModel)
    protected:
        QPageWidgetModelPrivate()
            : rootItem(new PageItem(0, 0))
        {
        }

        ~QPageWidgetModelPrivate()
        {
            delete rootItem;
            rootItem = nullptr;
        }

        PageItem *rootItem;

        void _k_itemChanged()
        {
            Q_Q(QPageWidgetModel);
            QPageWidgetItem *item = qobject_cast<QPageWidgetItem*>(q->sender());
            if (!item) {
                return;
            }

            const QModelIndex index = q->index(item);
            if (!index.isValid()) {
                return;
            }

            emit q->dataChanged(index, index);
        }

        void _k_itemToggled(bool checked)
        {
            Q_Q(QPageWidgetModel);
            QPageWidgetItem *item = qobject_cast<QPageWidgetItem*>(q->sender());
            if (!item) {
                return;
            }

            emit q->toggled(item, checked);
        }
};

#endif // KPAGEWIDGETMODEL_P_H
// vim: sw=4 sts=4 et tw=100
