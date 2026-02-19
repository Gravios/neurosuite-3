/***************************************************************************
*   Copyright (C) 2009 by Lynn Hazan  					   *
*   lynn.hazan@lsce.ipsl.fr   						   *
*   									   *
*   This software is governed by the CeCILL license under French law and   *
*   abiding by the rules of distribution of free software.  You can  use,  *
*   modify and/ or redistribute the software under the terms of the CeCILL *
*   license as circulated by CEA, CNRS and INRIA at the following URL      *
*   "http://www.cecill.info". 						   *
*   									   *
*   As a counterpart to the access to the source code and  rights to copy, *
*   modify and redistribute granted by the license, users are provided     *
*   only with a limited warranty  and the software's author,  the holder   *
*   of the economic rights,  and the successive licensors  		   *
*   have only limitedliability. 					   *
*   									   *
*   In this respect, the user's attention is drawn to the risks associated *
*   with loading,  using,  modifying and/or developing or reproducing the  *
*   software by the user in light of its specific status of free software, *
*   that may mean  that it is complicated to manipulate,  and  that  also  *
*   therefore means  that it is reserved for developers  and  experienced  *
*   professionals having in-depth computer knowledge. Users are therefore  *
*   encouraged to load and test the software's suitability as regards      *
*   their requirements in conditions enabling the security of their  	   *
*   systems and/or data to be ensured and,  more generally, to use         *
*   and operate it in the same conditions as regards security. 		   *
*   									   *
*   The fact that you are presently reading this means that you have had   *
*   knowledge of the CeCILL license and that you accept its terms	   *
 ***************************************************************************/
//Application inludes
#include "dockarea.h"

//QT includes
#include <QDockWidget>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QList>

DockArea::DockArea (QWidget * parent )
    : QScrollArea ( parent )
{

    //Set the flags (so the QMainWindow does not act like a window) and dockoptions
    mMainWindow.setDockOptions ( QMainWindow::AllowNestedDocks | QMainWindow::AnimatedDocks |QMainWindow::AllowTabbedDocks );
    mMainWindow.setWindowFlags ( Qt::Widget );

    QScrollArea::setWidgetResizable(true);
    QScrollArea::setWidget(&mMainWindow);
    setFocusPolicy(Qt::NoFocus);

}

DockArea::~DockArea()
{
}


void DockArea::addWidget ( QWidget * pWidget,const QString& pName,Qt::DockWidgetArea pAllowedAreas,
                           Qt::DockWidgetArea pArea )
{
    QDockWidget * vDock = new QDockWidget (  pName , this );
    vDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    vDock->setAllowedAreas ( pAllowedAreas );
    vDock->setWidget ( pWidget );

    addDockWidget ( pArea,vDock );
}


void DockArea::addDockWidget ( Qt::DockWidgetArea pArea, QDockWidget * pDockwidget ) {
    //Specify the object name of the doc using the title.
    pDockwidget->setObjectName ( pDockwidget->windowTitle() );

    //Add the dockWidget to the list
    if ( mDockWidgetByNameMap.contains (pDockwidget->windowTitle()  ) ) {
        QList< QPointer<QDockWidget> > & vList = mDockWidgetByNameMap[pDockwidget->windowTitle() ];
        vList.append ( pDockwidget );
    }
    else {
        QList< QPointer<QDockWidget> > vList = QList< QPointer<QDockWidget> >();
        vList.append ( pDockwidget );
        mDockWidgetByNameMap.insert ( pDockwidget->windowTitle(),vList );
    }

    setupConnection ( pDockwidget->widget() ) ;

    //Call the parent implementation
    mMainWindow.addDockWidget ( pArea,pDockwidget );
}

void DockArea::setupConnection ( QWidget * pWidget ) {

    //connect ( this, SIGNAL ( contentToUpdate ( ) ),pWidget, SLOT ( updateContent () ) );
    //connect ( this, SIGNAL ( sig_backgroundColorChanged ( QColor ) ),pWidget, SLOT ( slotChangeBackgroundColor ( QColor ) ) );
}

void DockArea::showDockWidget ( const QString& pWidgetName, bool pShow ) {
    QList< QPointer<QDockWidget> > vDockWidgetList = mDockWidgetByNameMap.value ( pWidgetName );
    for ( int i = 0;i < vDockWidgetList.size();++i ) {
        vDockWidgetList.at ( i )->setVisible(pShow);
    }
}

void DockArea::removeDockWidget ( QDockWidget * dockwidget ) {
    mMainWindow.removeDockWidget ( dockwidget );
}

void DockArea::deleteWidgets ( const QString& pName ){
    QList< QPointer<QDockWidget> > vDockWidgetList = mDockWidgetByNameMap.take ( pName );

    for ( int i = 0;i < vDockWidgetList.size();++i ) {
        QPointer<QDockWidget> vDockWidget = vDockWidgetList.takeAt ( i );

        delete vDockWidget;
    }
}

QList<QWidget *> DockArea::widgets (const QString &pName )
{
    QList<QWidget *> vList = QList<QWidget *>();

    QMapIterator<QString, QList< QPointer<QDockWidget> > > vIterator ( mDockWidgetByNameMap );
    while ( vIterator.hasNext() ) {
        vIterator.next();
        if ( vIterator.key() == pName ) {
            QList< QPointer<QDockWidget> > vDockWidgetList = vIterator.value();

            for ( int i = 0;i < vDockWidgetList.size();++i ) {
                vList.append ( vDockWidgetList.at ( i )->widget() );
            }
        }
    }

    return vList;
}

QMap< QString,QList<QWidget *> > DockArea::widgetsByName ( )
{
    QMap< QString,QList<QWidget *> > vResultMap = QMap< QString,QList<QWidget *> >();

    QMapIterator<QString, QList< QPointer<QDockWidget> > > vIterator ( mDockWidgetByNameMap );
    while ( vIterator.hasNext() ) {
        vIterator.next();
        QList<QWidget *> vList = QList<QWidget *>();

        QList< QPointer<QDockWidget> > vDockWidgetList = vIterator.value();

        for ( int i = 0;i < vDockWidgetList.size();++i ) {
            vList.append ( vDockWidgetList.at ( i )->widget() );
        }

        vResultMap.insert ( vIterator.key(),vList );
    }

    return vResultMap;
}
