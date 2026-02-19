/*
    This file is part of the KDE Libraries

    Copyright (C) 2006 Tobias Koenig (tokoe@kde.org)
    Copyright (C) 2007 Rafael Fernández López (ereslibre@kde.org)

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


#ifndef QPAGEVIEW_P_H
#define QPAGEVIEW_P_H

#include "qpageview.h"

#include <QAbstractItemDelegate>
#include <QGridLayout>
#include <QStackedWidget>
#include <QAbstractProxyModel>
#include <QListView>
#include <QTabBar>
#include <QTreeView>
//#include <ktitlewidget.h>

class QPageStackedWidget : public QStackedWidget
{
  public:
    QPageStackedWidget( QWidget *parent = nullptr )
      : QStackedWidget( parent )
    {
    }

    void setMinimumSize( const QSize& size )
    {
      mMinimumSize = size;
    }

    virtual QSize minimumSizeHint () const
    {
      return mMinimumSize.expandedTo( QStackedWidget::minimumSizeHint() );
    }

  private:
    QSize mMinimumSize;
};

class QPageViewPrivate
{
    Q_DECLARE_PUBLIC(QPageView)
    protected:
        QPageViewPrivate(QPageView *);

        QPageView* q_ptr;

        // data
        QAbstractItemModel *model;
        QPageView::FaceType faceType;

        // gui
        QGridLayout *layout;
        QPageStackedWidget *stack;
        //KTitleWidget *titleWidget;
        QWidget *defaultWidget;

        QAbstractItemView *view;

        void updateTitleWidget(const QModelIndex& index);

        void updateSelection();
        void cleanupPages();
        QList<QWidget*> collectPages(const QModelIndex &parent = QModelIndex());
        QPageView::FaceType detectAutoFace() const;

        // private slots
        void _k_rebuildGui();
        void _k_modelChanged();
        void _k_dataChanged(const QModelIndex&, const QModelIndex&);
        void _k_pageSelected(const QItemSelection&, const QItemSelection&);

    private:
        void init();
};

namespace KDEPrivate {

class QPageListViewDelegate;
class QPageListViewProxy;

class QPagePlainView : public QAbstractItemView
{
  public:
    QPagePlainView( QWidget *parent = nullptr );

    virtual QModelIndex indexAt( const QPoint &point ) const;
    virtual void scrollTo( const QModelIndex &index, ScrollHint hint = EnsureVisible );
    virtual QRect visualRect( const QModelIndex & index ) const;

  protected:
    virtual QModelIndex moveCursor( QAbstractItemView::CursorAction, Qt::KeyboardModifiers );
    virtual int horizontalOffset() const;
    virtual int verticalOffset() const;
    virtual bool isIndexHidden( const QModelIndex& ) const;
    virtual void setSelection( const QRect&, QFlags<QItemSelectionModel::SelectionFlag> );
    virtual QRegion visualRegionForSelection( const QItemSelection& ) const;
};

class QPageListView : public QListView
{
  Q_OBJECT

  public:
    QPageListView( QWidget *parent = nullptr );
    virtual ~QPageListView();

    virtual void setModel( QAbstractItemModel *model );

  private Q_SLOTS:
    void updateWidth();
};

class QPageTreeView : public QTreeView
{
  Q_OBJECT

  public:
    QPageTreeView( QWidget *parent = nullptr );

    virtual void setModel( QAbstractItemModel *model );

  private Q_SLOTS:
    void updateWidth();

  private:
    void expandItems( const QModelIndex &index = QModelIndex() );
};

class QPageTabbedView : public QAbstractItemView
{
  Q_OBJECT

  public:
    QPageTabbedView( QWidget *parent = nullptr );
    virtual ~QPageTabbedView();

    virtual void setModel( QAbstractItemModel *model );

    virtual QModelIndex indexAt( const QPoint &point ) const;
    virtual void scrollTo( const QModelIndex &index, ScrollHint hint = EnsureVisible );
    virtual QRect visualRect( const QModelIndex & index ) const;

    virtual QSize minimumSizeHint() const;

  protected:
    virtual QModelIndex moveCursor( QAbstractItemView::CursorAction, Qt::KeyboardModifiers );
    virtual int horizontalOffset() const;
    virtual int verticalOffset() const;
    virtual bool isIndexHidden( const QModelIndex& ) const;
    virtual void setSelection( const QRect&, QFlags<QItemSelectionModel::SelectionFlag> );
    virtual QRegion visualRegionForSelection( const QItemSelection& ) const;

  private Q_SLOTS:
    void currentPageChanged( int );
    void layoutChanged();
    virtual void dataChanged( const QModelIndex&, const QModelIndex& );

  private:
    QTabWidget *mTabWidget;
};

class QPageListViewDelegate : public QAbstractItemDelegate
{
  Q_OBJECT

  public:
    QPageListViewDelegate( QObject *parent = nullptr );

    virtual void paint( QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index ) const;
    virtual QSize sizeHint( const QStyleOptionViewItem &option, const QModelIndex &index ) const;

  //private Q_SLOTS:
    //void iconSettingsChanged( int group );

  private:
    void drawFocus( QPainter*, const QStyleOptionViewItem&, const QRect& ) const;
    int mIconSize;
};

/**
 * We need this proxy model to map the leaves of a tree-like model
 * to a one-level list model.
 */
class QPageListViewProxy : public QAbstractProxyModel
{
  Q_OBJECT

  public:
    QPageListViewProxy( QObject *parent = nullptr );
    virtual ~QPageListViewProxy();

    virtual int rowCount( const QModelIndex &parent = QModelIndex() ) const;
    virtual int columnCount( const QModelIndex &parent = QModelIndex() ) const;
    virtual QModelIndex index( int row, int column, const QModelIndex &parent = QModelIndex() ) const;
    virtual QModelIndex parent( const QModelIndex& ) const;
    virtual QVariant data( const QModelIndex &index, int role ) const;
    virtual QModelIndex mapFromSource( const QModelIndex &index ) const;
    virtual QModelIndex mapToSource( const QModelIndex &index ) const;

  public Q_SLOTS:
    void rebuildMap();

  private:
    void addMapEntry( const QModelIndex& );

    QList<QModelIndex> mList;
};

class SelectionModel : public QItemSelectionModel
{
  Q_OBJECT

  public:
    SelectionModel( QAbstractItemModel *model, QObject *parent );

  public Q_SLOTS:
    virtual void clear();
    virtual void select( const QModelIndex &index, QItemSelectionModel::SelectionFlags command );
    virtual void select( const QItemSelection &selection, QItemSelectionModel::SelectionFlags command );
};

}

#endif
