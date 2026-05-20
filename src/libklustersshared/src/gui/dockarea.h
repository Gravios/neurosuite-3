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

#ifndef DOCKAREA_H
#define DOCKAREA_H


#include "libklustersshared_export.h"

//Qt includes
#include <QMainWindow>
#include <QPointer>
#include <QMap>
#include <QScrollArea>



/**
* Dock area where dockwidget can be added. An overall scrollarea is used to allow a scollbar when there are to many widgets in the area.
* @author Lynn Hazan <lynn.hazan@lsce.ipsl.fr>
*/
class KLUSTERSSHARED_EXPORT DockArea : public QScrollArea {
    Q_OBJECT
  public:
    DockArea(QWidget *parent = nullptr);

    virtual ~DockArea();
    /**
     * Add a widget.
     * @param pName name of the widget.
     * @param pAllowedAreas allowed areas for the widget.
     * @param pArea position for the widget.
     * @param pWidget widget to be added.
     *
     * */
    void addWidget (QWidget * pWidget, const QString &pName, Qt::DockWidgetArea pAllowedAreas = Qt::AllDockWidgetAreas,
             Qt::DockWidgetArea pArea = Qt::RightDockWidgetArea );

    /**
    * Add the given dockwidget to the specified area.
     * @param pArea area where to add the dockwidget.
     * @param pDockwidget dockwidget to be added.
    */
    void addDockWidget ( Qt::DockWidgetArea pArea, QDockWidget * pDockwidget );
   
    void removeDockWidget ( QDockWidget * dockwidget );

    /** Split the space covered by @p pFirst into two parts and place
     *  @p pSecond into the second part.  Thin wrapper around the inner
     *  QMainWindow's splitDockWidget so callers can arrange child docks
     *  without needing access to the private main window.
     */
    void splitDockWidget ( QDockWidget * pFirst, QDockWidget * pSecond,
                           Qt::Orientation pOrientation );

    /** Move @p pSecond to be tabified with @p pFirst.  Thin wrapper
     *  around the inner QMainWindow's tabifyDockWidget.
     */
    void tabifyDockWidget ( QDockWidget * pFirst, QDockWidget * pSecond );

    /** Resize the given dock widgets along @p pOrientation
     *  (Qt::Horizontal for widths, Qt::Vertical for heights).  Sizes are
     *  pixel hints — any difference between their sum and the available
     *  space is distributed across the docks by relative weight, so the
     *  ratio between values is what dominates in practice
     *  (e.g. {300, 300, 300} delivers equal thirds; {450, 550} delivers
     *  a 45/55 split).  Min/max-size constraints are respected.
     *  Thin wrapper around the inner QMainWindow's resizeDocks.
     */
    void resizeDocks ( const QList<QDockWidget *> & pDocks,
                       const QList<int> & pSizes,
                       Qt::Orientation pOrientation );

  public slots:

    /**Change the background color.*/
    void slotChangeBackgroundColor ( const QColor& pColor ) {emit sig_backgroundColorChanged ( pColor );}


    /** Show or hide the dockWidget corresponding to widgetName.
    * @param pWidgetName name of the dockWidget to show or hide.
    * @param show show the dockWidget if true, hide otherwise.
    */
    void showDockWidget ( const QString& pWidgetName, bool pShow );



    /** Return the names of the dockwidgets contained in this area.*/
    inline QList<QString> dockWidgetNames() {
      //The names (map keys) are ordered in ascending order
      return  mDockWidgetByNameMap.keys();
    }

    /** Return the list of widgets having @p pName as a name in this area.*/
    QList<QWidget *> widgets ( const QString& pName );
    
    /** Return a map of list of widgets by name in this area.*/
    QMap< QString,QList<QWidget *> > widgetsByName ( );
    
    /** Delete the list of widgets having @p pName as a name in this area.*/
    void deleteWidgets ( const QString& pName );
		    
  signals:
    /** Signal emited when a new dockWidget is added.
    * @param  pWidgetName name of the dockWidget added.
     */
    void dockWidgetAdded ( const QString& pWidgetName );

    void contentToUpdate();
    void sig_backgroundColorChanged( const QColor&);

  private:

    void setupConnection ( QWidget * pWidget );

    QMap<QString, QList< QPointer<QDockWidget> > > mDockWidgetByNameMap;    

    /** This map contains widget display information which are applicable at the dock are level.
    This mean that the information has an impact on all the widgets contained in the dock area.*/
    QMap<QString,QVariant> mDisplayInfo;   
    
    /**The main window allows to use dockwidgets*/
    QMainWindow mMainWindow;
};

#endif
