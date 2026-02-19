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


/*
Copyright (C) 2012 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
Port to Qt4
*/

#ifndef PAGED_KPAGEDIALOG_P_H
#define PAGED_KPAGEDIALOG_P_H

#include "qpagedialog.h"
#include "qextenddialog_p.h"
#include <QLayout>

class KLUSTERSSHARED_EXPORT QPageDialogPrivate : public QExtendDialogPrivate
{
    Q_DECLARE_PUBLIC(QPageDialog)
    protected:
        QPageDialogPrivate()
            : mPageWidget(0)
        {
        }

        QPageWidget *mPageWidget;

        void init()
        {
            Q_Q(QPageDialog);
            if (mPageWidget) {
                QObject::connect(mPageWidget, &QPageWidget::currentPageChanged,
                        q, &QPageDialog::currentPageChanged);
                QObject::connect(mPageWidget, &QPageWidget::pageRemoved,
                        q, &QPageDialog::pageRemoved);
            }
            q->setMainWidget(mPageWidget);
        }
};

#endif // PAGED_KPAGEDIALOG_P_H
// vim: sw=4 sts=4 et tw=100
