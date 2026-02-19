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


#ifndef QPAGEMODEL_H
#define QPAGEMODEL_H

#include "libklustersshared_export.h"

#include <QtCore/QAbstractItemModel>

class QPageModelPrivate;

/**
 *  @short A base class for a model used by QPageView.
 *
 *  This class is an abstract base class which must be used to
 *  implement custom models for QPageView. Additional to the standard
 *  Qt::ItemDataRoles it provides the two roles
 *
 *    @li HeaderRole
 *    @li WidgetRole
 *
 *  which are used to return a header string for a page and a QWidget
 *  pointer to the page itself.
 *
 *  <b>Example:</b>\n
 *
 *  \code
 *    QPageView *view = new QPageView( this );
 *    QPageModel *model = new MyPageModel( this );
 *
 *    view->setModel( model );
 *  \endcode
 *
 *  @see QPageView
 *  @author Tobias Koenig <tokoe@kde.org>
 */
class KLUSTERSSHARED_EXPORT QPageModel : public QAbstractItemModel
{
  Q_OBJECT
    Q_DECLARE_PRIVATE(QPageModel)

  public:
        /**
         * Additional roles that QPageView uses.
         */
        enum Role {
            /**
             * A string to be rendered as page header.
             */
            HeaderRole = Qt::UserRole + 1,
            /**
             * A pointer to the page widget. This is the widget that is shown when the item is
             * selected.
             *
             * You can make QVariant take a QWidget using
             * \code
             * QWidget *myWidget = new QWidget;
             * QVariant v = QVariant::fromValue(myWidget);
             * \endcode
             */
            WidgetRole
        };

    /**
     * Constructs a page model with the given parent.
     */
    explicit QPageModel( QObject *parent = nullptr );

    /**
     * Destroys the page model.
     */
    virtual ~QPageModel();

    protected:
        QPageModel(QPageModelPrivate &dd, QObject *parent);
        QPageModelPrivate *const d_ptr;
};

#endif
