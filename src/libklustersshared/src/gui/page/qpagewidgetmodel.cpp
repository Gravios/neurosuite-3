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


#include "qpagewidgetmodel.h"
#include "qpagewidgetmodel_p.h"

#include <QPointer>
#include <QWidget>

#include <qicon.h>

class QPageWidgetItem::Private
{
  public:
    Private()
            : checkable(false), checked(false), enabled(true)
    {
    }

    ~Private()
    {
      delete widget;
      widget = 0;
    }

    QString name;
    QString header;
    QIcon icon;
    QPointer<QWidget> widget;
    bool checkable : 1;
    bool checked : 1;
    bool enabled : 1;
};

QPageWidgetItem::QPageWidgetItem( QWidget *widget )
  : QObject( 0 ), d( new Private )
{
  d->widget = widget;

  /**
   * Hide the widget, otherwise when the widget has this QPageView as
   * parent the widget is shown outside the QStackedWidget if the page
   * was not selected ( and reparented ) yet.
   */
  if ( d->widget )
    d->widget->hide();
}

QPageWidgetItem::QPageWidgetItem( QWidget *widget, const QString &name )
  : QObject( 0 ), d( new Private )
{
  d->widget = widget;
  d->name = name;

  /**
   * Hide the widget, otherwise when the widget has this QPageView as
   * parent the widget is shown outside the QStackedWidget if the page
   * was not selected ( and reparented ) yet.
   */
  if ( d->widget )
    d->widget->hide();
}

QPageWidgetItem::~QPageWidgetItem()
{
  delete d;
}

void QPageWidgetItem::setEnabled(bool enabled)
{
    d->enabled = enabled;
    if (d->widget) {
        d->widget->setEnabled(enabled);
    }
    emit changed();
}

bool QPageWidgetItem::isEnabled() const
{
    return d->enabled;
}

QWidget* QPageWidgetItem::widget() const
{
  return d->widget;
}

void QPageWidgetItem::setName( const QString &name )
{
  d->name = name;

  emit changed();
}

QString QPageWidgetItem::name() const
{
  return d->name;
}

void QPageWidgetItem::setHeader( const QString &header )
{
  d->header = header;

  emit changed();
}

QString QPageWidgetItem::header() const
{
  return d->header;
}

void QPageWidgetItem::setIcon( const QIcon &icon )
{
  d->icon = icon;

  emit changed();
}

QIcon QPageWidgetItem::icon() const
{
  return d->icon;
}

void QPageWidgetItem::setCheckable( bool checkable )
{
  d->checkable = checkable;

  emit changed();
}

bool QPageWidgetItem::isCheckable() const
{
  return d->checkable;
}

void QPageWidgetItem::setChecked( bool checked )
{
  d->checked = checked;

  emit toggled( checked );
  emit changed();
}

bool QPageWidgetItem::isChecked() const
{
  return d->checked;
}

PageItem::PageItem( QPageWidgetItem *pageWidgetItem, PageItem *parent )
  : mPageWidgetItem( pageWidgetItem ), mParentItem( parent )
{
}

PageItem::~PageItem()
{
  delete mPageWidgetItem;
  mPageWidgetItem = 0;

  qDeleteAll(mChildItems);
}

void PageItem::appendChild( PageItem *item )
{
  mChildItems.append( item );
}

void PageItem::insertChild( int row, PageItem *item )
{
  mChildItems.insert( row, item );
}

void PageItem::removeChild( int row )
{
  mChildItems.removeAt( row );
}

PageItem *PageItem::child( int row )
{
  return mChildItems.value( row );
}

int PageItem::childCount() const
{
  return mChildItems.count();
}

int PageItem::columnCount() const
{
  return 1;
}

PageItem *PageItem::parent()
{
  return mParentItem;
}

int PageItem::row() const
{
  if ( mParentItem )
    return mParentItem->mChildItems.indexOf( const_cast<PageItem*>(this) );

  return 0;
}

QPageWidgetItem* PageItem::pageWidgetItem() const
{
  return mPageWidgetItem;
}

PageItem *PageItem::findChild( const QPageWidgetItem *item )
{
  if ( mPageWidgetItem == item )
    return this;

  for ( int i = 0; i < mChildItems.count(); ++i ) {
    PageItem *pageItem = mChildItems[ i ]->findChild( item );
    if ( pageItem )
      return pageItem;
  }

  return 0;
}

void PageItem::dump( int indent )
{
  QString prefix;
  for ( int i = 0; i < indent; ++i )
    prefix.append( " " );

  const QString name = ( mPageWidgetItem ? mPageWidgetItem->name() : "root" );
  qDebug( "%s (%p)", qPrintable( QString( "%1%2" ).arg( prefix, name ) ), (void*)this );
  for ( int i = 0; i < mChildItems.count(); ++i )
    mChildItems[ i ]->dump( indent + 2 );
}

QPageWidgetModel::QPageWidgetModel( QObject *parent )
    : QPageModel(*new QPageWidgetModelPrivate, parent)
{
}

QPageWidgetModel::~QPageWidgetModel()
{
}

int QPageWidgetModel::columnCount( const QModelIndex& ) const
{
  return 1;
}

QVariant QPageWidgetModel::data( const QModelIndex &index, int role ) const
{
  if ( !index.isValid() )
    return QVariant();

  PageItem *item = static_cast<PageItem*>( index.internalPointer() );

  if ( role == Qt::DisplayRole )
    return QVariant( item->pageWidgetItem()->name() );
  else if ( role == Qt::DecorationRole )
    return QVariant( item->pageWidgetItem()->icon() );
  else if ( role == HeaderRole )
    return QVariant( item->pageWidgetItem()->header() );
  else if ( role == WidgetRole )
    return QVariant::fromValue( item->pageWidgetItem()->widget() );
  else if ( role == Qt::CheckStateRole ) {
    if ( item->pageWidgetItem()->isCheckable() ) {
      return ( item->pageWidgetItem()->isChecked() ? Qt::Checked : Qt::Unchecked );
    } else
      return QVariant();
  } else
    return QVariant();
}

bool QPageWidgetModel::setData( const QModelIndex &index, const QVariant &value, int role )
{
  if ( !index.isValid() )
    return false;

  if ( role != Qt::CheckStateRole )
    return false;

  PageItem *item = static_cast<PageItem*>( index.internalPointer() );
  if ( !item )
    return false;

  if ( !item->pageWidgetItem()->isCheckable() )
    return false;

  if ( value.toInt() == Qt::Checked )
    item->pageWidgetItem()->setChecked( true );
  else
    item->pageWidgetItem()->setChecked( false );

  return true;
}

Qt::ItemFlags QPageWidgetModel::flags( const QModelIndex &index ) const
{
  if ( !index.isValid() )
    return Qt::NoItemFlags;

  Qt::ItemFlags flags = Qt::ItemIsSelectable;

  PageItem *item = static_cast<PageItem*>( index.internalPointer() );
  if ( item->pageWidgetItem()->isCheckable() )
    flags |= Qt::ItemIsUserCheckable;
  if (item->pageWidgetItem()->isEnabled()) {
      flags |= Qt::ItemIsEnabled;
  }

  return flags;
}

QModelIndex QPageWidgetModel::index( int row, int column, const QModelIndex &parent ) const
{
  PageItem *parentItem;

  if ( parent.isValid() )
    parentItem = static_cast<PageItem*>( parent.internalPointer() );
  else
        parentItem = d_func()->rootItem;

  PageItem *childItem = parentItem->child( row );
  if ( childItem )
    return createIndex( row, column, childItem );
  else
    return QModelIndex();
}

QModelIndex QPageWidgetModel::parent( const QModelIndex &index ) const
{
  if ( !index.isValid() )
    return QModelIndex();

  PageItem *item = static_cast<PageItem*>( index.internalPointer() );
  PageItem *parentItem = item->parent();

    if ( parentItem == d_func()->rootItem )
    return QModelIndex();
  else
    return createIndex( parentItem->row(), 0, parentItem );
}

int QPageWidgetModel::rowCount( const QModelIndex &parent ) const
{
  PageItem *parentItem;

  if ( !parent.isValid() )
        parentItem = d_func()->rootItem;
  else
    parentItem = static_cast<PageItem*>( parent.internalPointer() );

  return parentItem->childCount();
}

QPageWidgetItem* QPageWidgetModel::addPage( QWidget *widget, const QString &name )
{
  QPageWidgetItem *item = new QPageWidgetItem( widget, name );

  addPage( item );

  return item;
}

void QPageWidgetModel::addPage( QPageWidgetItem *item )
{
  emit layoutAboutToBeChanged();

    Q_D(QPageWidgetModel);
    connect(item, &QPageWidgetItem::changed, this, [this]() { d_func()->_k_itemChanged(); });
    connect(item, &QPageWidgetItem::toggled, this, [this](bool checked) { d_func()->_k_itemToggled(checked); });

  // The row to be inserted
  int row = d->rootItem->childCount();

  beginInsertRows(QModelIndex(), row, row);

  PageItem *pageItem = new PageItem( item, d->rootItem );
  d->rootItem->appendChild( pageItem );

  endInsertRows();

  emit layoutChanged();
}

QPageWidgetItem* QPageWidgetModel::insertPage( QPageWidgetItem *before, QWidget *widget, const QString &name )
{
  QPageWidgetItem *item = new QPageWidgetItem( widget, name );

  insertPage( before, item );

  return item;
}

void QPageWidgetModel::insertPage( QPageWidgetItem *before, QPageWidgetItem *item )
{
    PageItem *beforePageItem = d_func()->rootItem->findChild(before);
  if ( !beforePageItem ) {
    qDebug( "Invalid QPageWidgetItem passed!" );
    return;
  }

  emit layoutAboutToBeChanged();

    connect(item, &QPageWidgetItem::changed, this, [this]() { d_func()->_k_itemChanged(); });
    connect(item, &QPageWidgetItem::toggled, this, [this](bool checked) { d_func()->_k_itemToggled(checked); });

  PageItem *parent = beforePageItem->parent();
  // The row to be inserted
  int row = beforePageItem->row();

  QModelIndex index;
  if (parent != d_func()->rootItem) {
      index = createIndex( parent->row(), 0, parent );
  }

  beginInsertRows(index, row, row);

  PageItem *newPageItem = new PageItem( item, parent );
  parent->insertChild( row, newPageItem );

  endInsertRows();

  emit layoutChanged();
}

QPageWidgetItem* QPageWidgetModel::addSubPage( QPageWidgetItem *parent, QWidget *widget, const QString &name )
{
  QPageWidgetItem *item = new QPageWidgetItem( widget, name );

  addSubPage( parent, item );

  return item;
}

void QPageWidgetModel::addSubPage( QPageWidgetItem *parent, QPageWidgetItem *item )
{
    PageItem *parentPageItem = d_func()->rootItem->findChild(parent);
  if ( !parentPageItem ) {
    qDebug( "Invalid QPageWidgetItem passed!" );
    return;
  }

  emit layoutAboutToBeChanged();

    connect(item, &QPageWidgetItem::changed, this, [this]() { d_func()->_k_itemChanged(); });
    connect(item, &QPageWidgetItem::toggled, this, [this](bool checked) { d_func()->_k_itemToggled(checked); });

  // The row to be inserted
  int row = parentPageItem->childCount();

  QModelIndex index;
  if (parentPageItem != d_func()->rootItem) {
      index = createIndex( parentPageItem->row(), 0, parentPageItem );
  }

  beginInsertRows(index, row, row);

  PageItem *newPageItem = new PageItem( item, parentPageItem );
  parentPageItem->appendChild( newPageItem );

  endInsertRows();

  emit layoutChanged();
}

void QPageWidgetModel::removePage( QPageWidgetItem *item )
{
  if ( !item )
    return;

  Q_D(QPageWidgetModel);

  PageItem *pageItem = d->rootItem->findChild( item );
  if ( !pageItem ) {
    qDebug( "Invalid QPageWidgetItem passed!" );
    return;
  }

  emit layoutAboutToBeChanged();

    disconnect(item, &QPageWidgetItem::changed, this, nullptr);
    disconnect(item, &QPageWidgetItem::toggled, this, nullptr);

  PageItem *parentPageItem = pageItem->parent();
  int row = parentPageItem->row();

  QModelIndex index;
  if ( parentPageItem != d->rootItem )
    index = createIndex( row, 0, parentPageItem );

    beginRemoveRows(index, pageItem->row(), pageItem->row());

  parentPageItem->removeChild( pageItem->row() );
  delete pageItem;

  endRemoveRows();

  emit layoutChanged();
}

QPageWidgetItem *QPageWidgetModel::item(const QModelIndex &index) const
{
  if ( !index.isValid() )
    return 0;

  PageItem *item = static_cast<PageItem*>( index.internalPointer() );
  if ( !item )
    return 0;

  return item->pageWidgetItem();
}

QModelIndex QPageWidgetModel::index( const QPageWidgetItem *item ) const
{
  if ( !item )
    return QModelIndex();

    const PageItem *pageItem = d_func()->rootItem->findChild(item);
  if ( !pageItem ) {
    return QModelIndex();
  }

  return createIndex( pageItem->row(), 0, (void*)pageItem );
}

#include "moc_qpagewidgetmodel.cpp"
