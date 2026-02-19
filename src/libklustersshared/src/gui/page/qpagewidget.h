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


#ifndef QPAGEWIDGET_H
#define QPAGEWIDGET_H

#include <qpagewidgetmodel.h>

#include "qpageview.h"

class QPageWidgetPrivate;
/**
 * @short Page widget with many layouts (faces).
 * @see QPageView with hierarchical page model.
 *
 * @author Tobias Koenig (tokoe@kde.org)
 */
class KLUSTERSSHARED_EXPORT QPageWidget : public QPageView
{
  Q_OBJECT
    Q_DECLARE_PRIVATE(QPageWidget)

  public:
    /**
     * Creates a new page widget.
     *
     * @param parent The parent widget.
     */
    explicit QPageWidget( QWidget *parent = nullptr );

    /**
     * Destroys the page widget.
     */
    ~QPageWidget();

    /**
     * Adds a new top level page to the widget.
     *
     * @param widget The widget of the page.
     * @param name The name which is displayed in the navigation view.
     *
     * @returns The associated @see QPageWidgetItem.
     */
    QPageWidgetItem* addPage( QWidget *widget, const QString &name );

    /**
     * Adds a new top level page to the widget.
     *
     * @param item The @see QPageWidgetItem which describes the page.
     */
    void addPage( QPageWidgetItem *item );

    /**
     * Inserts a new page in the widget.
     *
     * @param before The new page will be insert before this @see QPageWidgetItem
     *               on the same level in hierarchy.
     * @param widget The widget of the page.
     * @param name The name which is displayed in the navigation view.
     *
     * @returns The associated @see QPageWidgetItem.
     */
    QPageWidgetItem* insertPage( QPageWidgetItem *before, QWidget *widget, const QString &name );

    /**
     * Inserts a new page in the widget.
     *
     * @param before The new page will be insert before this @see QPageWidgetItem
     *               on the same level in hierarchy.
     *
     * @param item The @see QPageWidgetItem which describes the page.
     */
    void insertPage( QPageWidgetItem *before, QPageWidgetItem *item );

    /**
     * Inserts a new sub page in the widget.
     *
     * @param parent The new page will be insert as child of this @see QPageWidgetItem.
     * @param widget The widget of the page.
     * @param name The name which is displayed in the navigation view.
     *
     * @returns The associated @see QPageWidgetItem.
     */
    QPageWidgetItem* addSubPage( QPageWidgetItem *parent, QWidget *widget, const QString &name );

    /**
     * Inserts a new sub page in the widget.
     *
     * @param parent The new page will be insert as child of this @see QPageWidgetItem.
     *
     * @param item The @see QPageWidgetItem which describes the page.
     */
    void addSubPage( QPageWidgetItem *parent, QPageWidgetItem *item );

    /**
     * Removes the page associated with the given @see QPageWidgetItem.
     */
    void removePage( QPageWidgetItem *item );

    /**
     * Sets the page which is associated with the given @see QPageWidgetItem to
     * be the current page and emits the currentPageChanged() signal.
     */
    void setCurrentPage( QPageWidgetItem *item );

    /**
     * Returns the @see QPageWidgetItem for the current page or 0 if there is no
     * current page.
     */
    QPageWidgetItem* currentPage() const;

  Q_SIGNALS:
    /**
     * This signal is emitted whenever the current page has changed.
     *
     * @param item The new current page or 0 if no current page is available.
     */
    void currentPageChanged( QPageWidgetItem *current, QPageWidgetItem *before );

    /**
     * This signal is emitted whenever a checkable page changes its state. @param checked is true
     * when the @param page is checked, or false if the @param page is unchecked.
     */
    void pageToggled( QPageWidgetItem *page, bool checked );
    
    /**
     * This signal is emitted when a page is removed.
     * @param page The page which is removed
     * */
    void pageRemoved( QPageWidgetItem *page );

    protected:
        QPageWidget(QPageWidgetPrivate &dd, QWidget *parent);

    private:
        Q_PRIVATE_SLOT(d_func(), void _k_slotCurrentPageChanged(const QModelIndex &, const QModelIndex &))
};

#endif
