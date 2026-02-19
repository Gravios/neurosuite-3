/*
    This file is part of the KDE Libraries

    Copyright (C) 2006 Tobias Koenig (tokoe@kde.org)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB. If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

/*
Copyright (C) 2012 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
Port to Qt4
*/


#include "qpagewidget.h"
#include "qpagewidget_p.h"

#include "qpagewidgetmodel.h"

QPageWidgetPrivate::QPageWidgetPrivate(QPageWidget *q)
    : QPageViewPrivate(q)
{
}

void QPageWidgetPrivate::_k_slotCurrentPageChanged(const QModelIndex &current, const QModelIndex &before)
{
  QPageWidgetItem *currentItem = nullptr;
  if ( current.isValid() )
    currentItem = model()->item( current );

  QPageWidgetItem *beforeItem = nullptr;
  if ( before.isValid() )
    beforeItem = model()->item( before );

    Q_Q(QPageWidget);
    emit q->currentPageChanged(currentItem, beforeItem);
}

QPageWidget::QPageWidget(QPageWidgetPrivate &dd, QWidget *parent)
    : QPageView(dd, parent)
{
    Q_D(QPageWidget);
    connect(this, &QPageView::currentPageChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &bef){ d_func()->_k_slotCurrentPageChanged(cur, bef); });

    if (!d->QPageViewPrivate::model) {
        setModel(new QPageWidgetModel(this));
    } else {
        Q_ASSERT(qobject_cast<QPageWidgetModel *>(d->QPageViewPrivate::model));
    }

    connect(d->model(), &QPageWidgetModel::toggled,
            this, &QPageWidget::pageToggled);
}

QPageWidget::QPageWidget( QWidget *parent )
    : QPageView(*new QPageWidgetPrivate(this), parent)
{
    Q_D(QPageWidget);
    connect(this, &QPageView::currentPageChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &bef){ d_func()->_k_slotCurrentPageChanged(cur, bef); });

    setModel(new QPageWidgetModel(this));

    connect(d->model(), &QPageWidgetModel::toggled,
            this, &QPageWidget::pageToggled);
}

QPageWidget::~QPageWidget()
{
}

QPageWidgetItem* QPageWidget::addPage( QWidget *widget, const QString &name )
{
    return d_func()->model()->addPage(widget, name);
}

void QPageWidget::addPage( QPageWidgetItem *item )
{
    d_func()->model()->addPage(item);
}

QPageWidgetItem* QPageWidget::insertPage( QPageWidgetItem *before, QWidget *widget, const QString &name )
{
    return d_func()->model()->insertPage(before, widget, name);
}

void QPageWidget::insertPage( QPageWidgetItem *before, QPageWidgetItem *item )
{
    d_func()->model()->insertPage(before, item);
}

QPageWidgetItem* QPageWidget::addSubPage( QPageWidgetItem *parent, QWidget *widget, const QString &name )
{
    return d_func()->model()->addSubPage(parent, widget, name);
}

void QPageWidget::addSubPage( QPageWidgetItem *parent, QPageWidgetItem *item )
{
    d_func()->model()->addSubPage(parent, item);
}

void QPageWidget::removePage( QPageWidgetItem *item )
{
    emit pageRemoved(item); // emit signal before we remove it, because the item will be deleted in the model
    d_func()->model()->removePage(item);
}

void QPageWidget::setCurrentPage( QPageWidgetItem *item )
{
    const QModelIndex index = d_func()->model()->index(item);
  if ( !index.isValid() )
    return;

  QPageView::setCurrentPage( index );
}

QPageWidgetItem* QPageWidget::currentPage() const
{
  const QModelIndex index = QPageView::currentPage();

  if ( !index.isValid() )
    return nullptr;

    return d_func()->model()->item(index);
}

#include "moc_qpagewidget.cpp"
