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


#ifndef QPAGEWIDGETMODEL_H
#define QPAGEWIDGETMODEL_H

#include "qpagemodel.h"

class QWidget;

class QIcon;

/**
 * QPageWidgetItem is used by @ref QPageWidget and represents
 * a page.
 *
 * <b>Example:</b>\n
 *
 * \code
 *  ColorPage *page = new ColorPage;
 *
 *  QPageWidgetItem *item = new QPageWidgetItem( page, i18n( "Colors" ) );
 *  item->setHeader( i18n( "Colors of Main Window" ) );
 *  item->setIcon( KIcon( "colors" ) );
 *
 *  QPageWidget *pageWidget = new QPageWidget( this );
 *  pageWidget->addPage( item );
 * \endcode
 *
 * @author Tobias Koenig (tokoe@kde.org)
 */
class KLUSTERSSHARED_EXPORT QPageWidgetItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY( QString name READ name WRITE setName )
    Q_PROPERTY( QString header READ header WRITE setHeader )
    Q_PROPERTY( QIcon icon READ icon WRITE setIcon )
    Q_PROPERTY( bool checkable READ isCheckable WRITE setCheckable )
    Q_PROPERTY( bool checked READ isChecked WRITE setChecked )
    /**
     * This property holds whether the item is enabled.
     *
     * It dis-/enables both the widget and the item in the list-/treeview.
     */
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled)

public:
    /**
     * Creates a new page widget item.
     *
     * @param widget The widget that is shown as page in the QPageWidget.
     */
    QPageWidgetItem( QWidget *widget );

    /**
     * Creates a new page widget item.
     *
     * @param widget The widget that is shown as page in the QPageWidget.
     * @param name The localized string that is show in the navigation view
     *             of the QPageWidget.
     */
    QPageWidgetItem( QWidget *widget, const QString &name );

    /**
     * Destroys the page widget item.
     */
    ~QPageWidgetItem();

    /**
     * Returns the widget of the page widget item.
     */
    QWidget* widget() const;

    /**
     * Sets the name of the item as shown in the navigation view of the page
     * widget.
     */
    void setName( const QString &name );

    /**
     * Returns the name of the page widget item.
     */
    QString name() const;

    /**
     * Sets the header of the page widget item.
     *
     * If setHeader(QString()) is used, what is the default if the header
     * does not got set explicit, then the defined name() will also be used
     * for the header. If setHeader("") is used, the header will be hidden
     * even if the @a QPageView::FaceType is something else then Tabbed.
     *
     * @param header Header of the page widget item.
     */
    void setHeader( const QString &header );

    /**
     * Returns the header of the page widget item.
     */
    QString header() const;

    /**
     * Sets the icon of the page widget item.
     * @param icon Icon of the page widget item.
     */
    void setIcon( const QIcon &icon );

    /**
     * Returns the icon of the page widget item.
     */
    QIcon icon() const;

    /**
     * Sets whether the page widget item is checkable in the view.
     * @param checkable True if the page widget is checkable,
     *                  otherwise false.
     */
    void setCheckable( bool checkable );

    /**
     * Returns whether the page widget item is checkable.
     */
    bool isCheckable() const;

    /**
     * Returns whether the page widget item is checked.
     */
    bool isChecked() const;

    /**
     * Returns whether the page widget item is enabled.
     */
    bool isEnabled() const;

public Q_SLOTS:
    /**
         * Sets whether the page widget item is enabled.
         */
    void setEnabled(bool);

    /**
         * Sets whether the page widget item is checked.
         */
    void setChecked( bool checked );

Q_SIGNALS:
    /**
     * This signal is emitted whenever the icon or header
     * is changed.
     */
    void changed();

    /**
     * This signal is emitted whenever the user checks or
     * unchecks the item of @see setChecked() is called.
     */
    void toggled( bool checked );

private:
    class Private;
    Private* const d;
};

class QPageWidgetModelPrivate;

/**
 * This page model is used by @see QPageWidget to provide
 * a hierarchical layout of pages.
 */
class KLUSTERSSHARED_EXPORT QPageWidgetModel : public QPageModel
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(QPageWidgetModel)

public:
    /**
     * Creates a new page widget model.
     *
     * @param parent The parent object.
     */
    explicit QPageWidgetModel( QObject *parent = nullptr );

    /**
     * Destroys the page widget model.
     */
    ~QPageWidgetModel();

    /**
     * Adds a new top level page to the model.
     *
     * @param widget The widget of the page.
     * @param name The name which is displayed in the navigation view.
     *
     * @returns The associated @see QPageWidgetItem.
     */
    QPageWidgetItem* addPage( QWidget *widget, const QString &name );

    /**
     * Adds a new top level page to the model.
     *
     * @param item The @see QPageWidgetItem which describes the page.
     */
    void addPage( QPageWidgetItem *item );

    /**
     * Inserts a new page in the model.
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
     * Inserts a new page in the model.
     *
     * @param before The new page will be insert before this @see QPageWidgetItem
     *               on the same level in hierarchy.
     *
     * @param item The @see QPageWidgetItem which describes the page.
     */
    void insertPage( QPageWidgetItem *before, QPageWidgetItem *item );

    /**
     * Inserts a new sub page in the model.
     *
     * @param parent The new page will be insert as child of this @see QPageWidgetItem.
     * @param widget The widget of the page.
     * @param name The name which is displayed in the navigation view.
     *
     * @returns The associated @see QPageWidgetItem.
     */
    QPageWidgetItem* addSubPage( QPageWidgetItem *parent, QWidget *widget, const QString &name );

    /**
     * Inserts a new sub page in the model.
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
     * These methods are reimplemented from QAbstractItemModel.
     */
    virtual int columnCount( const QModelIndex &parent = QModelIndex() ) const;
    virtual QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const;
    virtual bool setData( const QModelIndex &index, const QVariant &value, int role = Qt::EditRole );
    virtual Qt::ItemFlags flags( const QModelIndex &index ) const;
    virtual QModelIndex index( int row, int column, const QModelIndex &parent = QModelIndex() ) const;
    virtual QModelIndex parent( const QModelIndex &index ) const;
    virtual int rowCount( const QModelIndex &parent = QModelIndex() ) const;

    /**
     * Returns the @see QPageWidgetItem for a given index or 0 if the index is invalid.
     */
    QPageWidgetItem *item(const QModelIndex &index) const;

    /**
     * Returns the index for a given @see QPageWidgetItem. The index is invalid if the
     * item can't be found in the model.
     */
    QModelIndex index( const QPageWidgetItem *item ) const;

Q_SIGNALS:
    /**
     * This signal is emitted whenever a checkable page changes its state. @param checked is true
     * when the @param page is checked, or false if the @param page is unchecked.
     */
    void toggled( QPageWidgetItem *page, bool checked );

private:

};

#endif
