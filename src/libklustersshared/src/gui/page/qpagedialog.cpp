/*
 *  This file is part of the KDE Libraries
 *  Copyright (C) 1999-2001 Mirko Boehm (mirko@kde.org) and
 *  Espen Sand (espen@kde.org)
 *  Holger Freyther <freyther@kde.org>
 *  2005-2006 Olivier Goffart <ogoffart at kde.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 *
 */

/*
Copyright (C) 2012 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
Port to Qt4
*/


#include "qpagedialog.h"
#include "qpagedialog_p.h"

#include <QTimer>
#include <QLayout>

QPageDialog::QPageDialog( QWidget *parent, Qt::WindowFlags flags )
    : QExtendDialog(*new QPageDialogPrivate, parent, flags)
{
    Q_D(QPageDialog);
  d->mPageWidget = new QPageWidget( this );

  d->init();
}

QPageDialog::QPageDialog( QPageWidget *widget, QWidget *parent, Qt::WindowFlags flags )
    : QExtendDialog(*new QPageDialogPrivate, parent, flags)
{
    Q_D(QPageDialog);
    Q_ASSERT(widget);
    widget->setParent(this);
  d->mPageWidget = widget;

  d->init();
}

QPageDialog::QPageDialog(QPageDialogPrivate &dd, QPageWidget *widget, QWidget *parent, Qt::WindowFlags flags)
    : QExtendDialog(dd, parent, flags)
{
    Q_D(QPageDialog);
    if (widget) {
        widget->setParent(this);
        d->mPageWidget = widget;
    } else {
        d->mPageWidget = new QPageWidget(this);
    }
    d->init();
}

QPageDialog::~QPageDialog()
{
}

void QPageDialog::setFaceType( FaceType faceType )
{
    d_func()->mPageWidget->setFaceType(static_cast<QPageWidget::FaceType>(faceType));
}

QPageWidgetItem* QPageDialog::addPage( QWidget *widget, const QString &name )
{
    return d_func()->mPageWidget->addPage(widget, name);
}

void QPageDialog::addPage( QPageWidgetItem *item )
{
    d_func()->mPageWidget->addPage(item);
}

QPageWidgetItem* QPageDialog::insertPage( QPageWidgetItem *before, QWidget *widget, const QString &name )
{
    return d_func()->mPageWidget->insertPage(before, widget, name);
}

void QPageDialog::insertPage( QPageWidgetItem *before, QPageWidgetItem *item )
{
    d_func()->mPageWidget->insertPage(before, item);
}

QPageWidgetItem* QPageDialog::addSubPage( QPageWidgetItem *parent, QWidget *widget, const QString &name )
{
    return d_func()->mPageWidget->addSubPage(parent, widget, name);
}

void QPageDialog::addSubPage( QPageWidgetItem *parent, QPageWidgetItem *item )
{
    d_func()->mPageWidget->addSubPage(parent, item);
}

void QPageDialog::removePage( QPageWidgetItem *item )
{
    d_func()->mPageWidget->removePage(item);
}

void QPageDialog::setCurrentPage( QPageWidgetItem *item )
{
    d_func()->mPageWidget->setCurrentPage(item);
}

QPageWidgetItem* QPageDialog::currentPage() const
{
    return d_func()->mPageWidget->currentPage();
}

QPageWidget* QPageDialog::pageWidget()
{
    return d_func()->mPageWidget;
}

void QPageDialog::setPageWidget(QPageWidget *widget)
{
    delete d_func()->mPageWidget;
    d_func()->mPageWidget = widget;
    d_func()->init();
}

const QPageWidget* QPageDialog::pageWidget() const
{
    return d_func()->mPageWidget;
}

#include "moc_qpagedialog.cpp"
