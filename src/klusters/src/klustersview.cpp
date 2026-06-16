/***************************************************************************
                          klustersview.cpp  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/


#include <QList>
#include <QPixmap>
#include <QMouseEvent>
#include <QEvent>
#include <QCloseEvent>
#include <QAction>
#include <QMenu>
#include <QWidgetAction>
#include <QTimer>

// include files for Qt
#include <QPainter>
#include <QDir>
#include <QLabel>
#include <QDebug>
#include <QHBoxLayout>

// application specific includes
#include "klusters.h"
#include "klustersview.h"
#include "klustersdoc.h"
#include "clusterview.h"
#include "waveformview.h"
#include "errormatrixview.h"
#include "templatematrixview.h"
#include "residualmatrixview.h"
#include "tracewidget.h"
#include "correlationview.h"
#include "viewwidget.h"
#include "baseframe.h"

extern int nbUndo;

const QString KlustersView::DisplayTypeNames[]={QObject::tr("Cluster Display"),
                                                QObject::tr("Waveform Display"),
                                                QObject::tr("Correlation Display"),
                                                QObject::tr("Overview Display"),
                                                QObject::tr("Grouping Assistant Display"),
                                                QObject::tr("Error Matrix Display"),
                                                QObject::tr("Trace Display"),
                                                QObject::tr("Template Matrix Display"),
                                                QObject::tr("Residual Matrix Display")};


KlustersView::KlustersView(KlustersApp& mainWindow,KlustersDoc& pDoc,const QColor& backgroundColor,int initialDimensionX,int initialDimensionY,
                           QList<int>* initialClusterList, DisplayType type, QWidget *parent, const char* name,QStatusBar * statusBar,int timeInterval,int maxAmplitude,
                           QList<int> positions,bool isTimeFrameMode,long start,long timeFrameWidth,long nbSpkToDisplay,bool overLay,bool mean,
                           int binSize, int correlationTimeFrame,Data::ScaleMode scale,bool shoulderLine,long startingTime,long duration,bool labelsDisplay,
                           QList< QList<int>* > undoList, QList< QList<int>* > redoList)
    : DockArea(parent),
      doc(pDoc),
      removedClustersUndoList(undoList),
      removedClustersRedoList(redoList),
      dimensionX(initialDimensionX),
      dimensionY(initialDimensionY),
      currentViewWidget(nullptr),
      numberUndo(undoList.count()),
      inTimeFrameMode(isTimeFrameMode),
      timeWindow(timeFrameWidth),
      startTime(start),
      nbSpkToDisplay(nbSpkToDisplay),
      overLayDisplay(overLay),
      meanDisplay(mean),
      binSize(binSize),
      correlogramTimeFrame(correlationTimeFrame),
      correlationScale(scale),
      shoulderLine(shoulderLine),
      mainWindow(mainWindow),
      traceWidget(nullptr),
      startingTime(startingTime),
      duration(duration),
      labelsDisplay(labelsDisplay)
{
    setObjectName(name);
    setAutoFillBackground(true);
    shownClusters = initialClusterList;
    removedClusters = new QList<int>();

    setAttribute(Qt::WA_DeleteOnClose, true);
    //Create the mainDock — the descriptive window title is set inside each
    //switch case below once the widget type is known.  Leaving the title
    //empty here avoids the session-path title that used to leak when a
    //case forgot to override.
    mainDock = new QDockWidget(QString());
    mainDock->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
    mainDock->setAttribute(Qt::WA_DeleteOnClose, true);
    //If the type of view is a not base one, call the function to call the complex views.
    //If the type of view is a base on, construct the appropriate Widget and assign it as the mainDock widget
    //To add a new base type just add a new case with the appropriate widget (do not to add the include line)
    switch(type){
    case CLUSTERS:
    {
        isThereWaveformView = false;
        isThereClusterView = true;
        isThereCorrelationView = false;
        isThereErrorMatrixView = false;
        isThereTemplateMatrixView = false;
        isThereTraceView = false;
        mainDock->setWidget(new ClusterView(doc,*this,backgroundColor,timeInterval,statusBar,mainDock));
        mainDock->setWindowTitle(tr("Cluster Features"));
        currentViewWidget = dynamic_cast<ViewWidget*>(mainDock->widget());
        viewList.append(currentViewWidget);
        currentViewWidget->installEventFilter(this);//To enable right click popup menu
        mainDock->installEventFilter(this);
        viewCounter.insert("ClusterView",1);
        setConnections(CLUSTERS,currentViewWidget,mainDock);
    }
        break;
    case WAVEFORMS:
    {
        isThereWaveformView = true;
        isThereClusterView = false;
        isThereCorrelationView = false;
        isThereErrorMatrixView = false;
        isThereTemplateMatrixView = false;
        isThereTraceView = false;
        mainDock->setWidget(new WaveformView(doc,*this,backgroundColor,maxAmplitude,positions,statusBar,mainDock,
                                             inTimeFrameMode,startTime,timeWindow,nbSpkToDisplay,overLayDisplay,meanDisplay));
        mainDock->setWindowTitle(tr("Waveforms"));

        currentViewWidget = dynamic_cast<ViewWidget*>(mainDock->widget());
        viewList.append(currentViewWidget);
        currentViewWidget->installEventFilter(this);//To enable right click popup menu
        mainDock->installEventFilter(this);
        viewCounter.insert("WaveformView",1);
        setConnections(WAVEFORMS,currentViewWidget,mainDock);
    }
        break;
    case CORRELATIONS:
    {
        isThereWaveformView = false;
        isThereClusterView = false;
        isThereCorrelationView = true;
        isThereErrorMatrixView = false;
        isThereTemplateMatrixView = false;
        isThereTraceView = false;
        mainDock->setWidget(new CorrelationView(doc,*this,backgroundColor,statusBar,mainDock,correlationScale,
                                                binSize,correlogramTimeFrame,shoulderLine));
        mainDock->setWindowTitle(tr("Auto-correlogram"));

        currentViewWidget = dynamic_cast<ViewWidget*>(mainDock->widget());
        viewList.append(currentViewWidget);
        currentViewWidget->installEventFilter(this);//To enable right click popup menu
        mainDock->installEventFilter(this);
        viewCounter.insert("CorrelationView",1);
        setConnections(CORRELATIONS,currentViewWidget,mainDock);
    }
        break;
    case OVERVIEW:
        isThereWaveformView = true;
        isThereClusterView = true;
        isThereCorrelationView = true;
        isThereErrorMatrixView = false;
        isThereTemplateMatrixView = false;
        isThereTraceView = false;
        createOverview(backgroundColor,statusBar,timeInterval,maxAmplitude,positions);
        break;
    case GROUPING_ASSISTANT_VIEW:
        isThereWaveformView = true;
        isThereClusterView = true;
        isThereCorrelationView = true;
        isThereErrorMatrixView = true;
        isThereTemplateMatrixView = false;
        isThereTraceView = false;
        createGroupingAssistantView(backgroundColor,statusBar,timeInterval,maxAmplitude,positions);
        break;
    case ERROR_MATRIX:
        break;
    case TEMPLATE_MATRIX:
        isThereWaveformView = false;
        isThereClusterView = false;
        isThereCorrelationView = false;
        isThereErrorMatrixView = false;
        isThereTemplateMatrixView = false;
        isThereTraceView = false;
        break;
    case TRACES:
    {
        isThereWaveformView = false;
        isThereClusterView = false;
        isThereCorrelationView = false;
        isThereErrorMatrixView = false;
        isThereTemplateMatrixView = false;
        isThereTraceView = true;
        //Create the providers (data and cluster) if need it
        if(!doc.isTracesProvider()) doc.createProviders();
        QList<int> skippedChannels;
        //the settings are : greyScale, no vertical lines nor rasters and waveforms, no labels displayed, no channel skipped.
        mainDock->setWidget(new TraceWidget(startingTime,duration,true,*doc.getTraceProvider(),false,false,false,
                                            true,labelsDisplay,doc.getCurrentChannels(),doc.getGain(),doc.getAcquisitionGain(),doc.channelColors(),
                                            doc.getDisplayGroupsChannels(),doc.getDisplayChannelsGroups(),offsets,gains,skippedChannels,mainDock,"traces",
                                            backgroundColor,statusBar,5));
        mainDock->setWindowTitle(tr("Traces"));

        traceWidget = dynamic_cast<TraceWidget*>(mainDock->widget());
        //Set the list of the current view as the list of clusters to look up in the ClusterProvider.
        doc.getClustersProvider()->setClusterIdList(shownClusters);

        //Add the cluster provider to the TraceView
        traceWidget->addClusterProvider(doc.getClustersProvider(),doc.getClustersProvider()->getName(),&static_cast<ItemColors&>(doc.clusterColors()),                           true,*shownClusters,doc.getDisplayGroupsClusterFile(),doc.getChannelsSpikeGroups(),
                                        doc.getNbSamplesBeforePeak(),doc.getNbSamplesAfterPeak(),clustersToSkip);

        traceWidget->installEventFilter(this);//To enable right click popup menu
        mainDock->installEventFilter(this);
        viewCounter.insert("TraceView",1);
        setConnections(TRACES,traceWidget,mainDock);
    }
        break;
    }
    addDockWidget(Qt::TopDockWidgetArea,mainDock);

    // Apply the canonical layout for composite views.  For plain OVERVIEW
    // this stacks Cluster Features / Waveforms / Auto-correlogram
    // vertically; for GROUPING_ASSISTANT_VIEW it additionally tabifies the
    // Error Matrix and Template Matrix on the right.
    if (type == OVERVIEW || type == GROUPING_ASSISTANT_VIEW) {
        applyOverviewLayout();
    }

}


KlustersView::~KlustersView()
{
    NS3_DIAG() << "in ~KlustersView(): ";

    // Disconnect all connections on this object before any teardown,
    // including doc → child-ViewWidget connections that were set up
    // in setConnections().  If doc was already deleted, those senders
    // are gone but the ViewWidget receivers still hold dangling entries.
    // disconnect() removes ALL connections to and from *this* as well as
    // from every child widget that is a QObject child of this (via the
    // child-widget disconnect in the loop below).
    disconnect();
    // Also disconnect every sub-view ViewWidget from all senders.
    for (ViewWidget* w : qAsConst(viewList))
        if (w) w->disconnect();

    // Sever the destroyed() → *DockClosed() connections on every sub-view
    // widget before we start tearing down.  Without this, Qt delivers the
    // destroyed() signal for each sub-dock as its QWidget parent chain is
    // unwound, which calls clusterDockClosed / waveformDockClosed / … and
    // those touch mainWindow — which is already mid-destruction when the
    // application exits.  The resulting use-after-free corrupts the heap and
    // produces the "corrupted double-linked list" SIGABRT.
    for (ViewWidget* w : qAsConst(viewList))
        if (w) QObject::disconnect(w, &QObject::destroyed, this, nullptr);
    if (traceWidget)
        QObject::disconnect(traceWidget, &QObject::destroyed, this, nullptr);

    qDeleteAll(removedClustersUndoList);
    removedClustersUndoList.clear();
    qDeleteAll(removedClustersRedoList);
    removedClustersRedoList.clear();

    delete shownClusters;
    delete removedClusters;
}

void KlustersView::createOverview(const QColor& backgroundColor,QStatusBar* statusBar,int timeInterval,int maxAmplitude,QList<int> positions){
    /*OVERVIEW type is the combination of 3 base types:
  CLUSTERS on the left side, WAVEFORMS at the right top and CORRELATIONS in the bottom right
 */
    //The main dock will be the cluster view
    ClusterView* view = new ClusterView(doc,*this,backgroundColor,timeInterval,statusBar,mainDock);
    mainDock->setWidget(view);
    // Descriptive title — the previous doc.documentName() showed the full
    // session path on every frame, which is uninformative when the title
    // bar of the parent window already shows it.
    mainDock->setWindowTitle(tr("Cluster Features"));

    currentViewWidget = view;
    viewList.append(currentViewWidget);
    currentViewWidget->installEventFilter(this);//To enable right click popup menu
    mainDock->installEventFilter(this);
    viewCounter.insert("ClusterView",1);

    setConnections(CLUSTERS,currentViewWidget,mainDock);


    //Create and add the waveforms view
    QDockWidget* waveforms = new QDockWidget(tr("Waveforms"));
    waveforms->setAttribute(Qt::WA_DeleteOnClose, true);
    waveforms->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
    //createDockWidget( "WaveForm", QPixmap(), 0L, doc.documentName(), doc.documentName());
    waveforms->setWidget(new WaveformView(doc,*this,backgroundColor,maxAmplitude,positions,statusBar,waveforms,
                                          inTimeFrameMode,startTime,timeWindow,nbSpkToDisplay,overLayDisplay,meanDisplay));//assign the widget
    ViewWidget* waveformView = dynamic_cast<ViewWidget*>(waveforms->widget());
    viewList.append(waveformView);
    waveformView->installEventFilter(this);//To enable right click popup menu
    waveforms->installEventFilter(this);
    addDockWidget(Qt::BottomDockWidgetArea,waveforms);
    viewCounter.insert("WaveformView",1);
    m_overviewWaveformDock = waveforms;

    setConnections(WAVEFORMS,waveformView,waveforms);

    //Create and add the correlations view
    QDockWidget* correlations = new QDockWidget(tr("Auto-correlogram"));
    correlations->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
    correlations->setAttribute(Qt::WA_DeleteOnClose, true);
            //createDockWidget( "Correlation", QPixmap(), 0L, doc.documentName(), doc.documentName());
    correlations->setWidget(new CorrelationView(doc,*this,backgroundColor,statusBar,correlations,correlationScale,binSize,correlogramTimeFrame,shoulderLine));//assign the widget
    ViewWidget* correlationView = dynamic_cast<ViewWidget*>(correlations->widget());
    viewList.append(correlationView);
    correlationView->installEventFilter(this);//To enable right click popup menu
    correlations->installEventFilter(this);
    addDockWidget(Qt::BottomDockWidgetArea,correlations);
    viewCounter.insert("CorrelationView",1);
    m_overviewCorrelationDock = correlations;

    setConnections(CORRELATIONS,correlationView,correlations);
}

void KlustersView::createGroupingAssistantView(const QColor& backgroundColor,QStatusBar* statusBar,int timeInterval,int maxAmplitude,QList<int> positions){
    //First create the overview
    createOverview(backgroundColor,statusBar,timeInterval,maxAmplitude,positions);

    //Create and add the errorMatrixView.  Position and tabification are
    //handled later in applyOverviewLayout(); here we just add it so the
    //QMainWindow takes ownership.
    QDockWidget* errorMatrix = new QDockWidget(tr("Error Matrix"));
    errorMatrix->setAttribute(Qt::WA_DeleteOnClose, true);
    errorMatrix->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
    //createDockWidget("ErrorMatrix", QPixmap(), 0L, doc.documentName(), doc.documentName());
    errorMatrix->setWidget(new ErrorMatrixView(doc,*this,backgroundColor,statusBar,errorMatrix));//assign the widget
    ViewWidget* errorMatrixView = dynamic_cast<ViewWidget*>(errorMatrix->widget());
    viewList.append(errorMatrixView);
    errorMatrixView->installEventFilter(this);//To enable right click popup menu
    errorMatrix->installEventFilter(this);
    addDockWidget(Qt::BottomDockWidgetArea,errorMatrix);
    m_overviewErrorMatrixDock = errorMatrix;
    setConnections(ERROR_MATRIX,errorMatrixView,errorMatrix);

    //Create and add the templateMatrixView
    QDockWidget* templateMatrix = new QDockWidget(tr("Template Matrix"));
    templateMatrix->setAttribute(Qt::WA_DeleteOnClose, true);
    templateMatrix->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
    templateMatrix->setWidget(new TemplateMatrixView(doc,*this,backgroundColor,statusBar,templateMatrix));
    TemplateMatrixView* tmView = qobject_cast<TemplateMatrixView*>(templateMatrix->widget());
    isThereTemplateMatrixView = true;
    addDockWidget(Qt::BottomDockWidgetArea,templateMatrix);
    m_overviewTemplateMatrixDock = templateMatrix;
    setConnections(TEMPLATE_MATRIX,tmView,templateMatrix);
}


void KlustersView::applyOverviewLayout(){
    // Idempotent — may be called multiple times on the same view (e.g. once
    // from the constructor before auto-show adds matrices, then a second
    // time after the matrices land).  To make repeat calls robust, the
    // routine first **resets** all tracked docks by re-adding them to a
    // single base dock area, which collapses any prior nested splits, and
    // then builds the canonical layout from scratch.
    //
    // Layout strategy:
    //   1. Reset — move every dock we know about into TopDockWidgetArea.
    //      addDockWidget on a dock already in the QMainWindow moves it,
    //      so this clears any splits left over from a previous call.
    //   2. Carve the right pane.  splitDockWidget(mainDock, rightAnchor,
    //      Horizontal) makes the right pane span the entire height —
    //      crucial that this runs BEFORE the vertical splits, because
    //      after those splits mainDock's space is only a thin top-third
    //      strip and a horizontal split would land the matrices in the
    //      top-right rather than the full right half.
    //   3. Tabify the second matrix on top of the first.
    //   4. Stack the left column vertically: Cluster Features (mainDock)
    //      top, Waveforms middle, Auto-correlogram bottom.
    //
    // Tolerates missing docks — when called from a plain OVERVIEW
    // constructor, no matrices are tracked yet so step 2/3 are skipped
    // and the result is just the vertical-stack left column.

    // Step 1 — reset.
    addDockWidget(Qt::TopDockWidgetArea, mainDock);
    if (m_overviewWaveformDock)
        addDockWidget(Qt::TopDockWidgetArea, m_overviewWaveformDock);
    if (m_overviewCorrelationDock)
        addDockWidget(Qt::TopDockWidgetArea, m_overviewCorrelationDock);
    if (m_overviewErrorMatrixDock)
        addDockWidget(Qt::TopDockWidgetArea, m_overviewErrorMatrixDock);
    if (m_overviewTemplateMatrixDock)
        addDockWidget(Qt::TopDockWidgetArea, m_overviewTemplateMatrixDock);

    // Step 2 — carve the right pane (full window height) BEFORE any
    // vertical splits.  Pick whichever matrix dock exists as the anchor.
    QDockWidget* rightAnchor = nullptr;
    if (m_overviewErrorMatrixDock)
        rightAnchor = m_overviewErrorMatrixDock.data();
    else if (m_overviewTemplateMatrixDock)
        rightAnchor = m_overviewTemplateMatrixDock.data();
    if (rightAnchor) {
        splitDockWidget(mainDock, rightAnchor, Qt::Horizontal);
    }

    // Step 3 — tabify the second matrix on top of the first so they
    // share a single frame with tabs at the bottom.
    if (m_overviewErrorMatrixDock && m_overviewTemplateMatrixDock) {
        tabifyDockWidget(m_overviewErrorMatrixDock,
                         m_overviewTemplateMatrixDock);
        // Front-tab is the Error Matrix — the cluster-quality review
        // workflow reads errors first, then flips to templates.
        m_overviewErrorMatrixDock->raise();
    }

    // Step 4 — stack the left column vertically.  Each call splits the
    // anchor dock's space, so the order builds a top-to-bottom chain:
    //   mainDock  →  waveforms below mainDock
    //   waveforms →  correlations below waveforms
    if (m_overviewWaveformDock) {
        splitDockWidget(mainDock, m_overviewWaveformDock, Qt::Vertical);
    }
    if (m_overviewWaveformDock && m_overviewCorrelationDock) {
        splitDockWidget(m_overviewWaveformDock,
                        m_overviewCorrelationDock,
                        Qt::Vertical);
    }

    // Step 5 — proportions, deferred to the next event-loop iteration
    // because resizeDocks() may not be applied correctly before the
    // QMainWindow's layout is activated (Qt docs warning), and the
    // constructor path is pre-show.  Using `this` as the singleShot
    // context object makes Qt skip the call if the view is destroyed
    // first, so no extra QPointer guard is needed.  Member QPointers are
    // re-checked inside the lambda because a dock could have been closed
    // between scheduling and firing.
    QTimer::singleShot(0, this, [this]() {
        // Horizontal 45/55 split between the left column (anchored on
        // mainDock) and the right matrix pane.  Pixel values are sized
        // close to a typical Klusters window so the relative-weight
        // fallback for the un-allocated space lands on the intended
        // ratio even when child min-sizes consume some of the budget.
        QDockWidget* rDock = nullptr;
        if (m_overviewErrorMatrixDock)
            rDock = m_overviewErrorMatrixDock.data();
        else if (m_overviewTemplateMatrixDock)
            rDock = m_overviewTemplateMatrixDock.data();
        if (rDock) {
            resizeDocks({mainDock, rDock}, {450, 550}, Qt::Horizontal);
        }

        // Equal vertical thirds for the left column.
        QList<QDockWidget*> column;
        column << mainDock;
        if (m_overviewWaveformDock)
            column << m_overviewWaveformDock.data();
        if (m_overviewCorrelationDock)
            column << m_overviewCorrelationDock.data();
        if (column.size() >= 2) {
            QList<int> sizes;
            for (int i = 0; i < column.size(); ++i) sizes << 300;
            resizeDocks(column, sizes, Qt::Vertical);
        }
    });
}


void KlustersView::update(KlustersView* pSender){
    if(pSender != this)
        repaint();
}

void KlustersView::forceClusterRefresh(int clusterId)
{
    // Only refresh if the cluster is actually shown in this view.
    if (!shownClusters->contains(clusterId))
        return;

    // Emitting spikesAddedToCluster forces every sub-view widget to:
    //   WaveformView:    set drawContentsMode = REDRAW, askForWaveformInformation()
    //   CorrelationView: set drawContentsMode = REDRAW, askForCorrelograms()
    //   ClusterView:     addClusterToUpdate() + redraw()
    // This is the same signal fired by grouping/deletion operations, so all
    // the normal cache-miss + thread-launch paths are exercised.
    emit spikesAddedToCluster(clusterId, true);
}

void KlustersView::stopAllViewThreads()
{
    // Standard ViewWidget descendants (WaveformView, etc.) — covered by
    // the ViewWidget::stopRunningThreads virtual.
    const QList<ViewWidget*>& widgets = getViewList();
    for (ViewWidget* w : widgets)
        w->stopRunningThreads();

    // TemplateMatrixView is NOT a ViewWidget — it inherits directly from
    // QWidget and lives only as a dock widget child of this KlustersView,
    // so the iteration above misses it.  Walk the QObject tree to find
    // any TemplateMatrixView instance(s) and quiesce their internal
    // threads (TemplateMatrixThread + PairXcorrThread, both of which
    // fopen/fread .spk.pending directly).  Without this, nudge /
    // realign writes to .spk.pending race with in-flight matrix-thread
    // reads — visible symptom is a subset of cluster spikes appearing
    // corrupted after a nudge.
    const QList<TemplateMatrixView*> tmvs =
        findChildren<TemplateMatrixView*>();
    for (TemplateMatrixView* tmv : tmvs)
        tmv->stopRunningThreadsSync();

    // ResidualMatrixView is likewise a plain QWidget dock child (not a
    // ViewWidget), and its ResidualMatrixThread fopen/freads .spk directly,
    // so it needs the same synchronous quiesce before .spk.pending writes.
    const QList<ResidualMatrixView*> rmvs =
        findChildren<ResidualMatrixView*>();
    for (ResidualMatrixView* rmv : rmvs)
        rmv->stopRunningThreadsSync();
}

void KlustersView::invalidateClusterDisplay(int clusterId)
{
    if (!shownClusters->contains(clusterId))
        return;

    // Step 1: stop any in-flight WaveformThreads before launching new ones.
    stopAllViewThreads();

    // Step 2: tell ClusterView (scatter) to do a full REDRAW so old ghost
    // points at pre-nudge feature coordinates are erased, then call update()
    // so Qt actually schedules the repaint. Without update(), ClusterView
    // sits in REDRAW mode indefinitely because redraw() never calls update().
    QList<int> tmp{clusterId};
    emit spikesRemovedFromClusters(tmp, false);

    // Force an immediate repaint on every sub-view widget that is now in
    // REDRAW mode — covers ClusterView and any others.
    const QList<ViewWidget*>& widgets = getViewList();
    for (ViewWidget* w : widgets)
        w->update();

    // Step 3: relaunch WaveformThread/CorrelogramThread against updated data.
    emit spikesAddedToCluster(clusterId, true);
}

void KlustersView::print(QPrinter *pPrinter, const QString& filePath, bool whiteBackground)
{
    QPainter printPainter;

    const int width = pPrinter->width();
    const int height = pPrinter->height();
    printPainter.begin(pPrinter);


    QRect textRec = QRect(printPainter.viewport().left() + 5 ,printPainter.viewport().height() - 20,printPainter.viewport().width() - 5,20);
    QFont f("Helvetica",8);

    const int nbViews = viewList.count();
    for(int i = 0; i< nbViews; ++i) {
        if(i > 0)
            pPrinter->newPage();
        ViewWidget* widget = viewList.at(i);
        //Modify the viewport so the view will not draw on the legend
        QRect newViewport = QRect(printPainter.viewport().left(),printPainter.viewport().top(),printPainter.viewport().width(),printPainter.viewport().height());
        newViewport.setBottom(printPainter.viewport().bottom() - 20);
        printPainter.setViewport(newViewport);
        widget->print(printPainter,width,height,whiteBackground);
        printPainter.resetTransform();

        printPainter.setFont(f);
        printPainter.setPen(Qt::black);
        if(qobject_cast<ClusterView*>(widget)){
            ClusterView* clusterView = static_cast<ClusterView*>(widget);
            printPainter.drawText(textRec,Qt::AlignLeft | Qt::AlignVCenter,tr("File: %1      Features: %2,%3").arg(filePath).arg(clusterView->getDimensionX()).arg(clusterView->getDimensionY()));
        } else if(qobject_cast<WaveformView*>(widget)) {
            if(inTimeFrameMode){
                printPainter.drawText(textRec,Qt::AlignLeft | Qt::AlignVCenter,tr("File: %1      Start Time: %2 s, Duration: %3 s").arg(filePath).arg(startTime).arg(timeWindow));
            } else {
                printPainter.drawText(textRec,Qt::AlignLeft | Qt::AlignVCenter,tr("File: %1      Number of Waveforms: %2").arg(filePath).arg(nbSpkToDisplay));
            }
        } else if(qobject_cast<CorrelationView*>(widget)) {
            QString scaleType;
            switch(correlationScale){
            case Data::RAW :
                scaleType = tr("Uniform Scale");
                break;
            case Data::MAX :
                scaleType =tr("Scale by Maximum");
                break;
            case Data::SHOULDER :
                scaleType = tr("Scale by Asymptote");
                break;
            }
            printPainter.drawText(textRec,Qt::AlignLeft | Qt::AlignVCenter,tr("File: %1      %2, Duration: %3 ms, Bin Size: %4 ms").arg(filePath).arg(scaleType).arg(correlogramTimeFrame/2).arg(binSize));
        } else if(qobject_cast<ErrorMatrixView*>(widget)){
            printPainter.drawText(textRec,Qt::AlignLeft | Qt::AlignVCenter,tr("File: %1").arg(filePath));
        }

    }

    //Print the trace view if exists
    if(isThereTraceView){
        pPrinter->newPage();
        printPainter.resetTransform();
        //Print the TraceView
        traceWidget->print(printPainter,width,height,filePath,whiteBackground);
    }

    printPainter.end();
}

void  KlustersView::clusterDockClosed(QObject *clusterView){
    viewList.removeAll(static_cast<ViewWidget*>(clusterView));
    //the clusterView to be removed is the last one
    if(viewCounter["ClusterView"] == 1){
        viewCounter.remove("ClusterView");
        mainWindow.widgetRemovedFromDisplay(CLUSTERS);
        isThereClusterView = false;
        dimensionX = 1;
        dimensionY = 2;
    }
    else viewCounter["ClusterView"]--;

    //Update the spineboxes with the dimensions of the first ClusterView found
    int nbViews = viewList.count();
    for(int i = 0; i< nbViews; i++) {
        ViewWidget* viewWidget = viewList.at(i);

        if(qobject_cast<ClusterView*>(viewWidget)){
            dimensionX = dynamic_cast<ClusterView*>(viewWidget)->getDimensionX();
            dimensionY = dynamic_cast<ClusterView*>(viewWidget)->getDimensionY();
            mainWindow.updateDimensionSpinBoxes(dimensionX,dimensionY);
            QObject::disconnect(this, &KlustersView::updatedDimensions, nullptr, nullptr);
            connect(this,&KlustersView::updatedDimensions,viewWidget, &ViewWidget::updatedDimensions);
            break;
        }
    }
}

void KlustersView::waveformDockClosed(QObject* waveformView){
    QApplication::restoreOverrideCursor();//Clear any previous overrided coming from this function.
    viewList.removeAll(static_cast<ViewWidget*>(waveformView));

    //For the time being only one WaveformView is allowed in a single View, but in the
    //future who knows ;0). This counter will make it easier to allow multiple WaveformView.
    if(viewCounter["WaveformView"] == 1){
        viewCounter.remove("WaveformView");
        mainWindow.widgetRemovedFromDisplay(WAVEFORMS);
        isThereWaveformView = false;
    }
    else
        viewCounter["WaveformView"]--;
}

void KlustersView::correlogramDockClosed(QObject* correlogramView){

    QApplication::restoreOverrideCursor();//Clear any previous overrided coming from this function.
    viewList.removeAll(static_cast<ViewWidget*>(correlogramView));

    if(viewCounter["CorrelationView"] == 1){
        viewCounter.remove("CorrelationView");
        mainWindow.widgetRemovedFromDisplay(CORRELATIONS);
        isThereCorrelationView = false;
        binSize = 1;
        correlogramTimeFrame = 61;
        correlationScale = Data::MAX;
        shoulderLine = true;
    }
    else viewCounter["CorrelationView"]--;

    //Update the correlogramView variables with the variables of the first correlogramView found
    int nbViews = viewList.count();
    for(int i = 0; i< nbViews; i++) {
        ViewWidget* viewWidget = viewList.at(i);
        if(qobject_cast<CorrelationView*>(viewWidget)){
            binSize = static_cast<CorrelationView*>(viewWidget)->getBinSize();
            correlogramTimeFrame = dynamic_cast<CorrelationView*>(viewWidget)->getTimeWindow();
            correlationScale = dynamic_cast<CorrelationView*>(viewWidget)->getScaleMode();
            shoulderLine = dynamic_cast<CorrelationView*>(viewWidget)->isShoulderLine();
            mainWindow.updateCorrelogramViewVariables(binSize,correlogramTimeFrame,shoulderLine,correlationScale);
            updateCorrelogramConnections(viewWidget);
            break;
        }
    }
}

void KlustersView::errorMatrixDockClosed(QObject* errorMatrixView){
    QApplication::restoreOverrideCursor();//Clear any previous overrided coming from this function.

    viewList.removeAll(static_cast<ViewWidget*>(errorMatrixView));
    mainWindow.widgetRemovedFromDisplay(ERROR_MATRIX);
    isThereErrorMatrixView = false;
        isThereTemplateMatrixView = false;
}

void KlustersView::templateMatrixDockClosed(QObject*){
    isThereTemplateMatrixView = false;
}

void KlustersView::residualMatrixDockClosed(QObject*){
    isThereResidualMatrixView = false;
}

void KlustersView::updateTemplateMatrixSliderRange(){
    for(ViewWidget* w : qAsConst(viewList)) {
        TemplateMatrixView* tmv = qobject_cast<TemplateMatrixView*>(w);
        if(tmv) { tmv->updateSliderRange(); return; }
    }
}

void KlustersView::traceDockClosed(QObject *traceWidget){
    if(viewCounter["TraceView"] == 1){
        viewCounter.remove("TraceView");
        mainWindow.widgetRemovedFromDisplay(TRACES);
        traceWidget = nullptr;
        isThereTraceView = false;
    }
    else viewCounter["TraceView"]--;
}

bool KlustersView::eventFilter(QObject* object,QEvent* event){

    if((event->type() == QEvent::MouseButtonPress) && (!qobject_cast<KlustersView*>(object))){
        //Check if the user has selected a dockWidget containing a ClusterView. If so
        //update the dimension spin boxes to reflect the current ClusterView dimensions
        // and make the ClusterView the only view connected to the signal of update of the spin boxes.
        if(qobject_cast<QDockWidget*>(object)){
            QWidget* widget = dynamic_cast<QDockWidget*>(object)->widget();
            if(qobject_cast<ClusterView*>(widget)){
                int nbViews = viewList.count();
                for(int i = 0; i< nbViews; i++) {
                    ViewWidget* viewWidget = viewList.at(i);
                    if(qobject_cast<ClusterView*>(viewWidget) && (widget == viewWidget)){
                        dimensionX = dynamic_cast<ClusterView*>(viewWidget)->getDimensionX();
                        dimensionY = dynamic_cast<ClusterView*>(viewWidget)->getDimensionY();
                        mainWindow.updateDimensionSpinBoxes(dimensionX,dimensionY);
                        QObject::disconnect(this, &KlustersView::updatedDimensions, nullptr, nullptr);
                        connect(this,&KlustersView::updatedDimensions,viewWidget, &ViewWidget::updatedDimensions);
                        return QWidget::eventFilter(object,event);
                    }
                }
            }
            //Check if the user has selected a dockWidget containing a CorrelationView. If so
            //update the bin size and duration boxes to reflect the current CorrelationView values and make the CorrelationView
            // the only view connected to the signal of update of the boxes.
            if(qobject_cast<CorrelationView*>(widget)){
                int nbViews = viewList.count();
                for(int i = 0; i< nbViews; i++) {
                    ViewWidget* viewWidget = viewList.at(i);
                    if(qobject_cast<CorrelationView*>(viewWidget) && (widget == viewWidget)){
                        binSize = dynamic_cast<CorrelationView*>(viewWidget)->getBinSize();
                        correlogramTimeFrame = dynamic_cast<CorrelationView*>(viewWidget)->getTimeWindow();
                        correlationScale = dynamic_cast<CorrelationView*>(viewWidget)->getScaleMode();
                        shoulderLine = dynamic_cast<CorrelationView*>(viewWidget)->isShoulderLine();
                        mainWindow.updateCorrelogramViewVariables(binSize,correlogramTimeFrame,shoulderLine,correlationScale);
                        updateCorrelogramConnections(viewWidget);
                        return QWidget::eventFilter(object,event);
                    }
                }
            }
        }

        //If the view on which the user has clicked is a ClusterView do the following:
        // * update the dimension spin boxes to reflect the current ClusterView dimensions
        //   and make the ClusterView the only view connected to the signal of update of the spin boxes.
        // * if a polygon is been drawn do not interpret the right click as an
        //   inquiery for the add View popupmenu.
        if(qobject_cast<ClusterView*>(object)){
            int nbViews = viewList.count();
            for(int i = 0; i< nbViews; i++) {
                ViewWidget* widget = viewList.at(i);

                if(qobject_cast<ClusterView*>(widget) && (object == widget)){
                    dimensionX = dynamic_cast<ClusterView*>(widget)->getDimensionX();
                    dimensionY = dynamic_cast<ClusterView*>(widget)->getDimensionY();
                    mainWindow.updateDimensionSpinBoxes(dimensionX,dimensionY);
                    QObject::disconnect(this, &KlustersView::updatedDimensions, nullptr, nullptr);
                    connect(this,&KlustersView::updatedDimensions,widget, &ViewWidget::updatedDimensions);
                    bool inProcess = dynamic_cast<ClusterView*>(widget)->isASelectionInProcess();
                    if(inProcess) return QWidget::eventFilter(object,event);
                }
            }
        }

        //Check if the user has selected a dockWidget containing a CorrelationView. If so
        //update the bin size and duration boxes to reflect the current CorrelationView values and make the CorrelationView
        // the only view connected to the signal of update of the boxes.
        if(qobject_cast<CorrelationView*>(object)){
            int nbViews = viewList.count();
            for(int i = 0; i< nbViews; i++) {
                ViewWidget* viewWidget = viewList.at(i);
                if(qobject_cast<CorrelationView*>(viewWidget) && (object == viewWidget)){
                    binSize = dynamic_cast<CorrelationView*>(viewWidget)->getBinSize();
                    correlogramTimeFrame = dynamic_cast<CorrelationView*>(viewWidget)->getTimeWindow();
                    correlationScale = dynamic_cast<CorrelationView*>(viewWidget)->getScaleMode();
                    shoulderLine = dynamic_cast<CorrelationView*>(viewWidget)->isShoulderLine();
                    mainWindow.updateCorrelogramViewVariables(binSize,correlogramTimeFrame,shoulderLine,correlationScale);
                    updateCorrelogramConnections(viewWidget);
                }
            }
        }
        //QWidget* widget;
        if(!qobject_cast<QDockWidget*>(object)) {
            //widget = dynamic_cast<QDockWidget*>(object)->widget();
        } else if(qobject_cast<ClusterView*>(object) ||
                  qobject_cast<WaveformView*>(object) ||
                  qobject_cast<CorrelationView*>(object) ||
                  qobject_cast<ErrorMatrixView*>(object) ||
                  qobject_cast<TraceWidget*>(object)) {
            //widget = dynamic_cast<QWidget*>(object);
        //if the object is a TraceView take its container the TraceWidget
        } else if(qobject_cast<TraceView*>(object)) {
            //widget = traceWidget;
        } else {
            return QWidget::eventFilter(object,event);    // standard event processing
        }

        QMouseEvent* mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if(mouseEvent->button() == Qt::RightButton){
            //Create the popmenu
            QMenu menu(tr("Add a View"),this);
            QWidgetAction *act = new QWidgetAction(&menu);
            QLabel *lab = new QLabel(tr("Add a View"));
            lab->setAlignment(Qt::AlignCenter);
            act->setDefaultWidget(lab);
            menu.addAction(act);
            menu.addSeparator();
            QAction* clusterView = menu.addAction(tr("Add a ClusterView"));
            QAction* waveformView = menu.addAction(tr("Add a WaveformView"));
            QAction* correlationView = menu.addAction(tr("Add a CorrelationView"));
            QAction* errorMatrixView    = menu.addAction(tr("Add an ErrorMatrixView"));
            QAction* templateMatrixView = menu.addAction(tr("Add a Template Matrix View"));
            QAction* traceView = menu.addAction(tr("Add a TraceView"));

            //A traceView is possible only if the variables it needs are available (provided in the new parameter file) and
            //the .dat file exists.
            if(!doc.areTraceDataAvailable() || !doc.isTraceViewVariablesAvailable())
                traceView->setEnabled(false);

            //For the moment only one WaveformView and TraceView are allowed per View.
            if(viewCounter.contains("WaveformView"))
                waveformView->setEnabled(false);

            if(viewCounter.contains("CorrelationView"))
                correlationView->setEnabled(true);

            if(viewCounter.contains("TraceView"))
                traceView->setEnabled(false);

            //Only one ErrorMatrixView is allowed for the whole application.
            if(mainWindow.isExistAnErrorMatrix())
                errorMatrixView->setEnabled(false);

            // Only one TemplateMatrixView per application.
            if(mainWindow.isExistATemplateMatrix())
                templateMatrixView->setEnabled(false);

            menu.setMouseTracking(true);
            QAction* id = menu.exec(QCursor::pos());

            if(id == clusterView){
                mainWindow.widgetAddToDisplay(CLUSTERS);
                return true;
            }
            else if(id == waveformView){
                mainWindow.widgetAddToDisplay(WAVEFORMS);
                return true;
            }
            else if(id == correlationView){
                mainWindow.widgetAddToDisplay(CORRELATIONS);
                return true;
            }
            else if(id == errorMatrixView){
                mainWindow.widgetAddToDisplay(ERROR_MATRIX);
                return true;
            }
            else if(id == templateMatrixView){
                mainWindow.widgetAddToDisplay(TEMPLATE_MATRIX);
                return true;
            }
            else if(id == traceView){
                mainWindow.widgetAddToDisplay(TRACES);
                return true;
            }
            else return QWidget::eventFilter(object,event);    // standard event processing
        }
        else return QWidget::eventFilter(object,event);    // standard event processing
    }
    else return QWidget::eventFilter(object,event);    // standard event processing

}

void KlustersView::closeEvent(QCloseEvent* e){

    // DO NOT CALL QWidget::closeEvent(e) here !!
    // This will accept the closing by QCloseEvent::accept() by default.
    // The installed eventFilter() in KlustersApp takes care for closing the widget
    // or ignoring the close event

}

bool KlustersView::addView(DisplayType displayType, const QColor &backgroundColor, QStatusBar* statusBar, int timeInterval, int maxAmplitude, QList<int> positions){

    //Enable docking abilities
    QDockWidget* clusters;
    QDockWidget* waveforms;
    QDockWidget* correlations;
    QDockWidget* errorMatrix;
    QDockWidget* templateMatrix;
    QDockWidget* residualMatrix;
    ViewWidget* clusterView;
    ViewWidget* waveformView;
    ViewWidget* correlationView;
    ViewWidget* errorMatrixView;
    QDockWidget* traces;
    QList<int> skippedChannels;

    bool newViewType = false;
    QString count;

    switch(displayType){
    case CLUSTERS:
    {
        if(!isThereClusterView){
            newViewType = true;
            viewCounter.insert("ClusterView",1);
        }
        else viewCounter["ClusterView"]++;

        isThereClusterView = true;
        count = QString::number(viewCounter["ClusterView"]);

        clusters = new QDockWidget(tr("Cluster Features"));
        clusters->setAttribute(Qt::WA_DeleteOnClose, true);
        clusters->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
                //createDockWidget(count.prepend("ClusterView"), QPixmap(), 0L, doc.documentName(), doc.documentName());
        clusters->setWidget(new ClusterView(doc,*this,backgroundColor,timeInterval,statusBar,clusters));
        clusterView = static_cast<ViewWidget*>(clusters->widget());
        viewList.append(clusterView);
        clusterView->installEventFilter(this);//To enable right click popup menu
        clusters->installEventFilter(this);
        addDockWidget(Qt::BottomDockWidgetArea,clusters);
        //Make the new ClusterView the only view connected to the signal of update of the spin boxes.
        //To do so disconnect all the other clusterViews connected, the actual connection for the current view is done in setConnections.
        QObject::disconnect(this, &KlustersView::updatedDimensions, nullptr, nullptr);

        setConnections(CLUSTERS,clusterView,clusters);

        //Give to the new view the same mode than the other clusterviews
        if(!newViewType){
            int nbViews = viewList.count();
            for(int i = 0; i< nbViews; i++) {
                ViewWidget* viewWidget = viewList.at(i);
                if(qobject_cast<ClusterView*>(viewWidget)){
                    clusterView->setMode(static_cast<ClusterView*>(viewWidget)->getMode());

                    break;
                }
            }
        }
    }
        break;
    case WAVEFORMS:
        if(!isThereWaveformView){
            newViewType = true;
            viewCounter.insert("WaveformView",1);
        } else {
            viewCounter["WaveformView"]++;
        }

        isThereWaveformView = true;
        count = QString::number(viewCounter["WaveformView"]);

        waveforms = new QDockWidget(tr("Waveforms"));
        waveforms->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        waveforms->setAttribute(Qt::WA_DeleteOnClose, true);
                //createDockWidget(count.prepend("WaveformView"), QPixmap(), 0L, doc.documentName(), doc.documentName());
        waveforms->setWidget(new WaveformView(doc,*this,backgroundColor,maxAmplitude,positions,statusBar,waveforms,
                                              inTimeFrameMode,startTime,timeWindow,nbSpkToDisplay,overLayDisplay,meanDisplay));//assign the widget
        waveformView = dynamic_cast<ViewWidget*>(waveforms->widget());
        viewList.append(waveformView);
        waveformView->installEventFilter(this);//To enable right click popup menu
        waveforms->installEventFilter(this);
        addDockWidget(Qt::BottomDockWidgetArea,waveforms);
        setConnections(WAVEFORMS,waveformView,waveforms);
        break;
    case CORRELATIONS:
        if(!isThereCorrelationView){
            newViewType = true;
            viewCounter.insert("CorrelationView",1);
        }
        else  viewCounter["CorrelationView"]++;

        isThereCorrelationView = true;
        count = QString::number(viewCounter["CorrelationView"]);

        correlations = new QDockWidget(tr("Auto-correlogram"));
        correlations->setAttribute(Qt::WA_DeleteOnClose, true);
        correlations->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);

                //createDockWidget(count.prepend("CorrelationView"), QPixmap(), 0L, doc.documentName(), doc.documentName());
        correlations->setWidget(new CorrelationView(doc,*this,backgroundColor,statusBar,correlations,correlationScale,binSize,correlogramTimeFrame,shoulderLine));//assign the widget
        correlationView = dynamic_cast<ViewWidget*>(correlations->widget());
        viewList.append(correlationView);
        correlationView->installEventFilter(this);//To enable right click popup menu
        correlations->installEventFilter(this);
        addDockWidget(Qt::BottomDockWidgetArea,correlations);
        //Make the new CorrelationView the only view connected to the signals.
        //To do so disconnect all the other CorrelationView connected, the actual connection for the current view is done in setConnections.
        QObject::disconnect(this, &KlustersView::updatedBinSizeAndTimeFrame, nullptr, nullptr);
        QObject::disconnect(this, &KlustersView::noScale, nullptr, nullptr);
        QObject::disconnect(this, &KlustersView::maxScale, nullptr, nullptr);
        QObject::disconnect(this, &KlustersView::shoulderScale, nullptr, nullptr);
        QObject::disconnect(this, &KlustersView::increaseAmplitudeofCorrelograms, nullptr, nullptr);
        QObject::disconnect(this, &KlustersView::decreaseAmplitudeofCorrelograms, nullptr, nullptr);
        QObject::disconnect(this, &KlustersView::setShoulderLine, nullptr, nullptr);

        setConnections(CORRELATIONS,correlationView,correlations);
        break;
    case ERROR_MATRIX:
        newViewType = true;
        isThereErrorMatrixView = true;

        errorMatrix = new QDockWidget(tr("Error Matrix"));
        errorMatrix->setAttribute(Qt::WA_DeleteOnClose, true);
        errorMatrix->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        //createDockWidget("ErrorMatrix", QPixmap(), 0L, doc.documentName(), doc.documentName());
        errorMatrix->setWidget(new ErrorMatrixView(doc,*this,backgroundColor,statusBar,errorMatrix));//assign the widget
        errorMatrixView = dynamic_cast<ViewWidget*>(errorMatrix->widget());
        viewList.append(errorMatrixView);
        errorMatrixView->installEventFilter(this);//To enable right click popup menu
        errorMatrix->installEventFilter(this);
        // Matrices live on the right side and are tabified when both exist.
        // If a TemplateMatrix dock is already present, tabify with it so
        // the two share one tabbed pane on the right.
        addDockWidget(Qt::RightDockWidgetArea,errorMatrix);
        m_overviewErrorMatrixDock = errorMatrix;
        if (m_overviewTemplateMatrixDock) {
            tabifyDockWidget(m_overviewTemplateMatrixDock, errorMatrix);
        }
        setConnections(ERROR_MATRIX,errorMatrixView,errorMatrix);
        break;
    case TEMPLATE_MATRIX:
        newViewType = true;
        isThereTemplateMatrixView = true;
        templateMatrix = new QDockWidget(tr("Template Matrix"));
        templateMatrix->setAttribute(Qt::WA_DeleteOnClose, true);
        templateMatrix->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        templateMatrix->setWidget(new TemplateMatrixView(doc,*this,backgroundColor,statusBar,templateMatrix));
        templateMatrix->installEventFilter(this);
        addDockWidget(Qt::RightDockWidgetArea,templateMatrix);
        m_overviewTemplateMatrixDock = templateMatrix;
        if (m_overviewErrorMatrixDock) {
            tabifyDockWidget(m_overviewErrorMatrixDock, templateMatrix);
            // Keep the Error Matrix as the front tab — it's the one most
            // workflows start from (the U-shortcut updates both, but the
            // user reads from Error first then flips to Template).
            m_overviewErrorMatrixDock->raise();
        }
        setConnections(TEMPLATE_MATRIX,qobject_cast<TemplateMatrixView*>(templateMatrix->widget()),templateMatrix);
        break;
    case RESIDUAL_MATRIX:
        newViewType = true;
        isThereResidualMatrixView = true;
        residualMatrix = new QDockWidget(tr("Residual Matrix"));
        residualMatrix->setAttribute(Qt::WA_DeleteOnClose, true);
        residualMatrix->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        residualMatrix->setWidget(new ResidualMatrixView(doc,*this,backgroundColor,statusBar,residualMatrix));
        residualMatrix->installEventFilter(this);
        addDockWidget(Qt::RightDockWidgetArea,residualMatrix);
        m_overviewResidualMatrixDock = residualMatrix;
        // Tabify with whichever matrix dock already exists so the three share
        // one pane on the right; keep Error (or Template) as the front tab.
        if (m_overviewErrorMatrixDock) {
            tabifyDockWidget(m_overviewErrorMatrixDock, residualMatrix);
            m_overviewErrorMatrixDock->raise();
        } else if (m_overviewTemplateMatrixDock) {
            tabifyDockWidget(m_overviewTemplateMatrixDock, residualMatrix);
            m_overviewTemplateMatrixDock->raise();
        }
        setConnections(RESIDUAL_MATRIX,qobject_cast<ResidualMatrixView*>(residualMatrix->widget()),residualMatrix);
        break;
    case TRACES:
        if(!isThereTraceView){
            newViewType = true;
            viewCounter.insert("TraceView",1);
            if(!doc.isTracesProvider()) doc.createProviders();
        }
        else viewCounter["TraceView"]++;

        isThereTraceView = true;
        count = QString::number(viewCounter["TraceView"]);

        traces = new QDockWidget(tr("Traces"));
        traces->setAttribute(Qt::WA_DeleteOnClose, true);
        traces->setFeatures(QDockWidget::DockWidgetClosable|QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
                //createDockWidget(count.prepend("TraceView"), QPixmap(), 0L, doc.documentName(), doc.documentName());
        //the settings are : greyScale, no vertical lines nor rasters and waveforms, no labels displayed, no channel skipped.
        traces->setWidget(new TraceWidget(startingTime,duration,true,*doc.getTraceProvider(),false,false,false,
                                          true,labelsDisplay,doc.getCurrentChannels(),doc.getGain(),doc.getAcquisitionGain(),doc.channelColors(),
                                          doc.getDisplayGroupsChannels(),doc.getDisplayChannelsGroups(),offsets,gains,skippedChannels,traces,"traces",
                                          backgroundColor,statusBar,5));//assign the widget


        traceWidget = dynamic_cast<TraceWidget*>(traces->widget());

        //Set the list of the current view as the list of clusters to look up in the ClusterProvider.
        doc.getClustersProvider()->setClusterIdList(shownClusters);

        //Add the cluster provider to the TraceView
        traceWidget->addClusterProvider(doc.getClustersProvider(),doc.getClustersProvider()->getName(),&static_cast<ItemColors&>(doc.clusterColors()),                           true,*shownClusters,doc.getDisplayGroupsClusterFile(),doc.getChannelsSpikeGroups(),
                                        doc.getNbSamplesBeforePeak(),doc.getNbSamplesAfterPeak(),clustersToSkip);

        traces->installEventFilter(this);//To enable right click popup menu
        traceWidget->installEventFilter(this);
        addDockWidget(Qt::BottomDockWidgetArea,traces);
        setConnections(TRACES,traceWidget,traces);
        break;
    case OVERVIEW:
        break;
    case GROUPING_ASSISTANT_VIEW:
        break;
    }

    return newViewType;
}

void KlustersView::updateDimensions(int dimensionX,int dimensionY){  
    this->dimensionX = dimensionX;
    this->dimensionY = dimensionY;
    //Signal a change to the Widgets
    emit updatedDimensions(dimensionX,dimensionY);
}


void KlustersView::shownClustersUpdate(const QList<int>& clustersToShow){
    //Try to minimize the number of clusters to draw
    QVector<int> clustersToRemove;
    
    //If a cluster already shown is not requested, remove it from the view
    QList<int>::iterator shownClustersIterator;
    for(shownClustersIterator = shownClusters->begin(); shownClustersIterator != shownClusters->end(); ++shownClustersIterator ){
        if(clustersToShow.contains(*shownClustersIterator) == 0){
            clustersToRemove.push_back(*shownClustersIterator);
        }
    }

    //Remove the clusters found previously from shownClusters
    removeClustersFromView(clustersToRemove,true);

    //If there is a cluster in clustersToShow which is not in shownClusters, add it to the view
    QList<int>::const_iterator clustersToShowIterator;
    for(clustersToShowIterator = clustersToShow.begin(); clustersToShowIterator != clustersToShow.end(); ++clustersToShowIterator ){
        if(shownClusters->contains(*clustersToShowIterator) == 0)
            addClusterToView(*clustersToShowIterator,true);
    }

    //Show all the enclosed widgets of the dockWindows.
    showAllWidgets();
}

void KlustersView::updateColors(bool active){
    ItemColors& clusterColors = doc.clusterColors();
    if(clusterColors.isColorChanged()){
        QList<int> colorChangedClusterList = clusterColors.colorChangedItemList();
        QList<int>::iterator iterator;
        for(iterator = colorChangedClusterList.begin(); iterator != colorChangedClusterList.end(); ++iterator ){
            if(shownClusters->contains(*iterator) != 0)
                singleColorUpdate(*iterator,active);
        }
    }
}

void KlustersView::groupedClustersUpdate(QList<int>& groupedClusters, int newClusterId,bool active){  
    bool isGroupedClustersInShownList = false;

    //If a cluster of the groupedClusters is in shownClusters list, remove it
    isGroupedClustersInShownList = clustersDeletionUpdate(groupedClusters,active);
    
    //If at least on cluster of the groupedClusters was in shownClusters list, add the new cluster to the list.
    if(isGroupedClustersInShownList){
        addClusterToView(newClusterId,active);
    }

    //Check if some clusters have had their color changed.
    // If so, update clusterUpdateList if needed
    updateColors(active);
}


bool KlustersView::clustersDeletionUpdate(QList<int>& deletedClusters,int destinationCluster,bool active){
    bool isAClusterRemoved = clustersDeletionUpdate(deletedClusters,active);

    //If the view contains the destinationCluster emit a notice of modification
    if(shownClusters->contains(destinationCluster) != 0){
        QList<int> modifiedcluster;
        modifiedcluster.append(destinationCluster);
        emit modifiedClusters(modifiedcluster,active,true);
    }

    return isAClusterRemoved;
}

bool KlustersView::clustersDeletionUpdate(QList<int>& deletedClusters,bool active){  
    QList<int> inView = clustersInView(deletedClusters);
    bool isAClusterRemoved = false;

    //the removedClustersUndoList have to be updated
    prepareUndo(inView);

    //If deletedClusters in not empty, this view is concerned by the modification
    if(!inView.isEmpty()){
        isAClusterRemoved = true;

        //If one of the clusters in deletedClusters is present in clustersShown list, remove it
        // and call removeClusterFromView on all the widgets
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = inView.begin(); clustersToRemoveIterator != inView.end(); ++clustersToRemoveIterator){
            removeClusterFromView(*clustersToRemoveIterator,active);
        }
    }

    //Check if some clusters have had their color changed.
    // If so, update clusterUpdateList if needed
    updateColors(active);

    return isAClusterRemoved;
}

void KlustersView::removeClusterFromView(int clusterId,bool active){
    shownClusters->removeAll(clusterId);
    emit clusterRemovedFromView(clusterId,active);
}

void KlustersView::removeClustersFromView(const QVector<int>& clusterIds, bool active){
    int size = clusterIds.size();
    for(int i = 0; i<size; ++i){
        shownClusters->removeAll(clusterIds[i]);
        emit clusterRemovedFromView(clusterIds[i],active);
    }
}

void KlustersView::addClusterToView(int clusterId,bool active){
    //Add the clusterId to the list of clusters shown and call addClusterToView on all the widgets
    shownClusters->append(clusterId);
    emit clusterAddedToView(clusterId,active);
}

void KlustersView::addNewClusterToView(QList<int>& fromClusters,int clusterId,QList<int>& emptiedClusters,bool active){  
    //List containing the clusters of this view which contained spikes of the newly created cluster
    QList<int> fromClustersInView = clustersInView(fromClusters);

    //the removedClustersUndoList have to be updated
    QList<int> emptiedClustersInView = clustersInView(emptiedClusters);
    prepareUndo(emptiedClustersInView);


    //If fromClustersInView in not empty, this view is concerned by the modification
    if(!fromClustersInView.isEmpty()){
        if(!emptiedClusters.isEmpty()){
            QList<int>::iterator clustersToRemoveIterator;
            for(clustersToRemoveIterator = emptiedClusters.begin(); clustersToRemoveIterator != emptiedClusters.end(); ++clustersToRemoveIterator ){
                removeClusterFromView(*clustersToRemoveIterator,active);
            }
        }

        shownClusters->append(clusterId);
        emit newClusterAddedToView(fromClustersInView,clusterId,active);
    }

    //Check if some clusters have had their color changed.
    // If so, update clusterUpdateList if needed
    updateColors(active);
}

void KlustersView::addNewClustersToView(QMap<int,int>& fromToNewClusterIds,QList<int>& emptiedClusters,bool active){
    //List containing the clusters of this view which contained spikes of the newly created cluster
    QList<int> fromClusters = fromToNewClusterIds.keys();
    QList<int> fromClustersInView = clustersInView(fromClusters);

    //the removedClustersUndoList have to be updated
    QList<int> emptiedClustersInView = clustersInView(emptiedClusters);
    prepareUndo(emptiedClustersInView);


    //If fromClustersInView in not empty, this view is concerned by the modification
    if(!fromClustersInView.isEmpty()){

        if(!emptiedClusters.isEmpty()){
            QList<int>::iterator clustersToRemoveIterator;
            for(clustersToRemoveIterator = emptiedClusters.begin(); clustersToRemoveIterator != emptiedClusters.end(); ++clustersToRemoveIterator ){
                removeClusterFromView(*clustersToRemoveIterator,active);
            }
        }

        emit modifiedClusters(fromClustersInView,active);

        QList<int>::iterator fromClusterIterator;
        for (fromClusterIterator = fromClustersInView.begin(); fromClusterIterator != fromClustersInView.end(); ++fromClusterIterator){
            int newClusterId = fromToNewClusterIds[*fromClusterIterator];
            shownClusters->append(newClusterId);
            emit newClusterAddedToView(newClusterId,active);
        }

    }

    //Check if some clusters have had their color changed.
    // If so, update clusterUpdateList if needed
    updateColors(active);
}


void KlustersView::addNewClustersToView(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList,bool active){

    //List containing the clusters of this view which contained recluster clusters
    QList<int> inView = clustersInView(clustersToRecluster);

    //the removedClustersUndoList have to be updated
    prepareUndo(inView);

    //If inView in not empty, this view is concerned by the modification
    if(!inView.isEmpty()){
        //prepareUndo(clustersToRecluster);

        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = clustersToRecluster.begin(); clustersToRemoveIterator != clustersToRecluster.end(); ++clustersToRemoveIterator ){
            removeClusterFromView(*clustersToRemoveIterator,active);
        }

        QList<int>::iterator iterator;
        for (iterator = reclusteredClusterList.begin(); iterator != reclusteredClusterList.end(); ++iterator){
            shownClusters->append(*iterator);
            emit newClusterAddedToView(*iterator,active);
        }
    }
}


void KlustersView::removeSpikesFromClustersInView(QList<int>& fromClusters, int destinationClusterId,QList<int>& emptiedClusters,bool active){    
    //List containing the clusters of this view which contained spikes which were removed
    QList<int> fromClustersInView = clustersInView(fromClusters);

    //the removedClustersUndoList have to be updated
    QList<int> emptiedClustersInView = clustersInView(emptiedClusters);
    prepareUndo(emptiedClustersInView);

    //If fromClustersInView in not empty, this view is concerned by the modification
    if(!fromClustersInView.isEmpty()){

        if(!emptiedClusters.isEmpty()){
            QList<int>::iterator clustersToRemoveIterator;
            for(clustersToRemoveIterator = emptiedClusters.begin(); clustersToRemoveIterator != emptiedClusters.end(); ++clustersToRemoveIterator ){
                removeClusterFromView(*clustersToRemoveIterator,active);
            }
        }

        emit spikesRemovedFromClusters(fromClustersInView,active);
        if(shownClusters->contains(destinationClusterId) != 0)
            emit spikesAddedToCluster(destinationClusterId,active);
    }

    //If the view contains the destinationCluster emit a notice of modification
    else if(shownClusters->contains(destinationClusterId) != 0){
        emit spikesAddedToCluster(destinationClusterId,active);
    }

    //Check if some clusters have had their color changed.
    // If so, update clusterUpdateList if needed
    updateColors(active);
}

QList<int> KlustersView::clustersInView(QList<int>& clusterlist){
    //subset of clusterlist with the clusters of this view
    QList<int> clustersInViewList;

    QList<int>::iterator iterator;
    for (iterator = clusterlist.begin(); iterator != clusterlist.end(); ++iterator){
        if(shownClusters->contains(*iterator) != 0) 
            clustersInViewList.append(*iterator);
    }
    return clustersInViewList;
}

void KlustersView::prepareUndo(QList<int>* removedClustersTemp){
    //Store the current removedClusters in the undo list and make the temporary become the current one.
    removedClustersUndoList.prepend(removedClusters);
    removedClusters = removedClustersTemp;

    ++numberUndo;

    //if the number of undo has been reach remove the last elements in the undo list (first ones inserted)
    if(numberUndo > nbUndo){
        delete removedClustersUndoList.takeAt(numberUndo - 1);
        --numberUndo;
    }

    //Clear the redoList
    qDeleteAll(removedClustersRedoList);
    removedClustersRedoList.clear();
}

void KlustersView::prepareUndo(QList<int>& newlyRemovedClusters){   
    //Create a new shownClusters which will hold the new configuration
    QList<int>* removedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = newlyRemovedClusters.begin(); iterator != newlyRemovedClusters.end(); ++iterator)
        removedClustersTemp->append(*iterator);

    prepareUndo(removedClustersTemp);
}

void KlustersView::nbUndoChangedCleaning(int newNbUndo){
    //if the new number of possible undo is smaller than the current one,
    // clean the undo/redo related variables.
    if(newNbUndo < nbUndo){
        //if the current number of undo is bigger than the new number of undo,
        // remove the last elements in the undo lists (first ones inserted).
        if(numberUndo > newNbUndo){
            while(numberUndo > newNbUndo){
                delete removedClustersUndoList.takeAt(numberUndo - 1);
                --numberUndo;
            }
            //Clear the redoList (was incorrectly clearing undoList -- copy-paste bug)
            qDeleteAll(removedClustersRedoList);
            removedClustersRedoList.clear();
        }
        //currentNbUndo < newNbUndo, check the redo list.
        else{
            //number of undo and redo must be <= new number of undo. Remove redo elements if need it.
            int currentNbRedo = removedClustersRedoList.count();
            if((currentNbRedo + numberUndo) > newNbUndo){
                while((currentNbRedo + numberUndo) > newNbUndo){
                    delete removedClustersRedoList.takeAt(currentNbRedo - 1);
                    currentNbRedo = removedClustersRedoList.count();
                }
            }
        }
    }
}

void KlustersView::addRemovedClusters(bool active){
    //If removedClustersUndoList is not empty, make the current removedClusters become the first element
    //of the removedClustersRedoList and the first element of the removedClustersUndoList become the current removedClusters.
    if(!removedClustersUndoList.isEmpty()){
        if(!removedClusters->isEmpty()){
            QList<int>::iterator newClusterIterator;
            for(newClusterIterator = removedClusters->begin(); newClusterIterator != removedClusters->end(); ++newClusterIterator){
                shownClusters->append(*newClusterIterator);
                emit newClusterAddedToView(*newClusterIterator,active);
            }
        }
        removedClustersRedoList.prepend(removedClusters);
        QList<int>* removedClustersTemp = removedClustersUndoList.takeAt(0);
        removedClusters =  removedClustersTemp;
    }
}

void KlustersView::undo(bool active){
    //add back the removed clusters
    addRemovedClusters(active);
    --numberUndo;
}

void KlustersView::undoAddedClusters(QList<int>& addedClusters,bool active){
    //If any of the clusters in addedClusters are present, remove them
    //add back the removed clusters
    addRemovedClusters(active);

    //List containing the clusters of this view which have to be removed
    QList<int> inView = clustersInView(addedClusters);

    if(!inView.isEmpty()){
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = inView.begin(); clustersToRemoveIterator != inView.end(); ++clustersToRemoveIterator){
            removeClusterFromView(*clustersToRemoveIterator,active);
        }
    }
    --numberUndo;
}

void KlustersView::undoModifiedClusters(QList<int>& updatedClusters,bool active){
    //add back the removed clusters
    addRemovedClusters(active);

    //If any of the clusters in modifiedClusters are present, update them

    //List containing the clusters of this view which have to be updated
    QList<int> inView = clustersInView(updatedClusters);
    if(!inView.isEmpty()){
        emit modifiedClustersUndo(inView,active);
    }
    --numberUndo;
}

void KlustersView::undo(QList<int>& addedClusters,QList<int>& updatedClusters,bool active){  
    //If any of the clusters in addedClusters are present, remove them.

    //List containing the clusters of this view which have to be removed
    QList<int> inView = clustersInView(addedClusters);
    if(!inView.isEmpty()){
        QList<int>::iterator clustersToRemoveIterator;
        for(clustersToRemoveIterator = inView.begin(); clustersToRemoveIterator != inView.end(); ++clustersToRemoveIterator){
            removeClusterFromView(*clustersToRemoveIterator,active);
        }
    }

    //add back the removed clusters
    addRemovedClusters(active);

    //If any of the clusters in modifiedClusters are present, update them
    //List containing the clusters of this view which have to be updated
    inView = clustersInView(updatedClusters);
    if(!inView.isEmpty()) emit modifiedClustersUndo(inView,active);

    --numberUndo;
}


bool KlustersView::removeUndoAddedClusters(bool active){
    bool isClustersRemoved = false;

    //If removedClustersRedoList is not empty, make the current removedClusters become the first element
    //of the removedClustersUndoList and the first element of the removedClustersRedoList become the current removedClusters.
    if(!removedClustersRedoList.isEmpty()){
        removedClustersUndoList.prepend(removedClusters);
        QList<int>* removedClustersTemp = removedClustersRedoList.takeAt(0);
        removedClusters =  removedClustersTemp;

        //List containing the clusters of this view which have to be removed
        QList<int> clustersToRemoveInView = clustersInView(*removedClusters);

        if(!clustersToRemoveInView.isEmpty()){
            isClustersRemoved = true;
            QList<int>::iterator deleteClusterIterator;
            for(deleteClusterIterator = clustersToRemoveInView.begin(); deleteClusterIterator != clustersToRemoveInView.end(); ++deleteClusterIterator){
                removeClusterFromView(*deleteClusterIterator,active);
            }
        }
    }
    return isClustersRemoved;
}


void KlustersView::removeDeletedClusters(bool active,QList<int>& clustersToDelete){
    //List containing the clusters of this view which have to be removed
    QList<int> clustersToRemoveInView = clustersInView(clustersToDelete);

    if(!clustersToRemoveInView.isEmpty()){
        QList<int>::iterator deleteClusterIterator;
        for(deleteClusterIterator = clustersToRemoveInView.begin(); deleteClusterIterator != clustersToRemoveInView.end(); ++deleteClusterIterator){
            removeClusterFromView(*deleteClusterIterator,active);
        }
    }
}

void KlustersView::redo(bool active,QList<int>& deletedClusters){
    removeDeletedClusters(active,deletedClusters);
    removeUndoAddedClusters(active);

    numberUndo++;
}

void KlustersView::redoAddedClusters(QList<int>& addedClusters,bool active,QList<int>& deletedClusters){     
    bool isClustersRemoved = removeUndoAddedClusters(active);
    removeDeletedClusters(active,deletedClusters);

    //Add back all the clusters contained in addedClusters if the view have had initially
    //clusters removed to enable the addition of clusters.
    if(isClustersRemoved){
        QList<int>::iterator newClusterIterator;
        for(newClusterIterator = addedClusters.begin(); newClusterIterator != addedClusters.end(); ++newClusterIterator){
            shownClusters->append(*newClusterIterator);

            emit newClusterAddedToView(*newClusterIterator,active);
        }
    }
    numberUndo++;
}

void KlustersView::redoModifiedClusters(QList<int>& updatedClusters,bool isModifiedByDeletion,bool active,QList<int>& deletedClusters){
    removeDeletedClusters(active,deletedClusters);
    removeUndoAddedClusters(active);

    //If any of the clusters in modifiedClusters are present, update them

    //List containing the clusters of this view which have to be updated
    QList<int> inView = clustersInView(updatedClusters);
    if(!inView.isEmpty())
        emit modifiedClusters(inView,active,isModifiedByDeletion);

    numberUndo++;
}

void KlustersView::redo(QList<int>& addedClusters,QList<int>& updatedClusters,bool isModifiedByDeletion,bool active,QList<int>& deletedClusters){
    removeDeletedClusters(active,deletedClusters);
    removeUndoAddedClusters(active);

    //if there are as many clusters added than updated,
    //the action was the creation of several clusters. In that case add back ony the clusters
    //corresponding to the clusters updated existing in the view.
    //The 2 lists are the keys and values of a previous map fromClusterId-new clusterId,
    //so they are order in correspondance.
    if(addedClusters.size() == updatedClusters.size()){
        QList<int> inView;
        QList<int>::iterator iterator;
        int index = 0;
        for (iterator = updatedClusters.begin(); iterator != updatedClusters.end(); ++iterator){
            if(shownClusters->contains(*iterator) != 0){
                inView.append(*iterator);
                shownClusters->append(addedClusters[index]);
                emit newClusterAddedToView(addedClusters[index],active);
            }
            ++index;
        }
        //If any of the clusters in modifiedClusters are present, update them
        if(!inView.isEmpty())
            emit modifiedClusters(inView,active,isModifiedByDeletion);
    }
    else{
        //If any of the clusters in modifiedClusters are present, update them
        //List containing the clusters of this view which have to be updated
        QList<int> inView = clustersInView(updatedClusters);
        if(!inView.isEmpty()) {
            //Add back all the clusters contained in addedClusters
            QList<int>::iterator newClusterIterator;
            for(newClusterIterator = addedClusters.begin(); newClusterIterator != addedClusters.end(); ++newClusterIterator){
                //If the clusters have been modified by deletion, that means that the clusters to add
                //can only be cluster 0 or cluster 1 which were added because they did not exit already.
                //In that case we do not want to add them back to the view.
                if(isModifiedByDeletion) 
                   continue;
                shownClusters->append(*newClusterIterator);
                emit newClusterAddedToView(*newClusterIterator,active);
            }
            emit modifiedClusters(inView,active,isModifiedByDeletion);
        }
    }
    numberUndo++;
}

void KlustersView::changeClusterIds(QMap<int,int>& clusterIds){
    QList<int>* shownClustersTemp = new QList<int>();

    // Update the clusterIds.  Use value(key, key) so a cluster id not
    // present in the map maps to itself (identity).  The previous
    // operator[] form returned 0 for missing keys AND mutated the
    // caller's map by inserting the missing key with a default value
    // — both of which were silent bugs for partial-rename callers
    // (any cluster not in the map would become cluster 0 = artefact).
    // Existing full-map callers (renumber / undoRenumbering build a
    // covering map) are unaffected by this change.
    for (int cid : *shownClusters)
        shownClustersTemp->append(clusterIds.value(cid, cid));

    delete shownClusters;
    shownClusters = shownClustersTemp;
}

void KlustersView::renumberClusters(QMap<int,int>& clusterIdsOldNew,bool active){
    //renumber the clusters
    changeClusterIds(clusterIdsOldNew);

    QList<int>* removedClustersTemp = new QList<int>();
    prepareUndo(removedClustersTemp);

    emit clustersRenumbered(active);
}



void KlustersView::undoRenumbering(QMap<int,int>& clusterIdsNewOld,bool active){
    //renumber the clusters
    changeClusterIds(clusterIdsNewOld);

    //add back the removed clusters
    addRemovedClusters(active);


    numberUndo--;

    emit clustersRenumbered(active);
}

void KlustersView::redoRenumbering(QMap<int,int>& clusterIdsOldNew,bool active){
    //renumber the clusters
    changeClusterIds(clusterIdsOldNew);

    removeUndoAddedClusters(active);

    ++numberUndo;

    emit clustersRenumbered(active);
}

bool KlustersView::isThreadsRunning() const{
    bool threadsRunning = false;
    int nbViews = viewList.count();
    for(int i = 0; i< nbViews; i++) {
        ViewWidget* widget = viewList.at(i);
        if(widget->isThreadsRunning()){
            threadsRunning = true;
            widget->willBeKilled();
        }
    }

    if(threadsRunning) return true;
    else return false;
}

QList< QList<int>* > KlustersView::getUndoList(){
    QList< QList<int>* > undoList;
    for(qsizetype i = 0; i<removedClustersUndoList.count();++i) {
        QList<int>* undoCopy = new QList<int>();
        const QList<int>* lst = removedClustersUndoList.at(i);
        for(int j= 0; j<lst->count();++j) {
            undoCopy->append(lst->at(j));
        }
        undoList.append(undoCopy);
    }

    return  undoList;
}

QList< QList<int>* >  KlustersView::getRedoList(){
    QList< QList<int>* > redoList;
    for(qsizetype i = 0; i<removedClustersRedoList.count();++i) {
        QList<int>* redoCopy = new QList<int>();
        const QList<int>* lst = removedClustersRedoList.at(i);
        for(int j= 0; j<lst->count();++j) {
            redoCopy->append(lst->at(j));
        }
        redoList.append(redoCopy);
    }

    return  redoList;
}

void KlustersView::setConnections(DisplayType displayType, QWidget* view,QDockWidget* dockWidget){
    //Connection(s) common to all widgets.
    connect(this, &KlustersView::updateContents, view, [view](){ view->update(); });
    
    //Connections common to ClusterView, WaveformView and CorrelationView
    if((displayType == CLUSTERS) || (displayType == WAVEFORMS) || (displayType == CORRELATIONS)){
        connect(this, &KlustersView::singleColorUpdated, qobject_cast<ViewWidget*>(view), &ViewWidget::singleColorUpdate);
        connect(this, &KlustersView::clusterRemovedFromView, qobject_cast<ViewWidget*>(view), &ViewWidget::removeClusterFromView);
        connect(this, &KlustersView::clusterAddedToView, qobject_cast<ViewWidget*>(view), &ViewWidget::addClusterToView);
        connect(this, static_cast<void(KlustersView::*)(QList<int>&,int,bool)>(&KlustersView::newClusterAddedToView), qobject_cast<ViewWidget*>(view), static_cast<void(ViewWidget::*)(QList<int>&,int,bool)>(&ViewWidget::addNewClusterToView));
        connect(this, static_cast<void(KlustersView::*)(int,bool)>(&KlustersView::newClusterAddedToView), qobject_cast<ViewWidget*>(view), static_cast<void(ViewWidget::*)(int,bool)>(&ViewWidget::addNewClusterToView));
        connect(this, &KlustersView::spikesRemovedFromClusters, qobject_cast<ViewWidget*>(view), &ViewWidget::spikesRemovedFromClusters);
        connect(this, &KlustersView::modeToSet, qobject_cast<BaseFrame*>(view), &BaseFrame::setMode);
        connect(this, &KlustersView::spikesAddedToCluster, qobject_cast<ViewWidget*>(view), &ViewWidget::spikesAddedToCluster);
        connect(this, &KlustersView::modifiedClusters, qobject_cast<ViewWidget*>(view), &ViewWidget::updateClusters);
        connect(this, &KlustersView::modifiedClustersUndo, qobject_cast<ViewWidget*>(view), &ViewWidget::undoUpdateClusters);
        connect(this, &KlustersView::updateDrawing, qobject_cast<BaseFrame*>(view), &BaseFrame::updateDrawing);
        connect(this, &KlustersView::changeBackgroundColor, qobject_cast<BaseFrame*>(view), &BaseFrame::changeBackgroundColor);
    }

    if(displayType == CLUSTERS){ //Connections for ClusterViews
        connect(this, &KlustersView::changeTimeInterval, qobject_cast<ClusterView*>(view), static_cast<void(ClusterView::*)(int,bool)>(&ClusterView::setTimeStepInSecond));
        connect(this, &KlustersView::updatedDimensions, qobject_cast<ClusterView*>(view), &ClusterView::updatedDimensions);
        connect(this, &KlustersView::emptySelection, qobject_cast<ClusterView*>(view), &ClusterView::emptySelection);
        connect(view, &QObject::destroyed, this, &KlustersView::clusterDockClosed);

        //Connect the clusterView to a possible TraceView
        if(isThereTraceView){
            connect(qobject_cast<ClusterView*>(view), &ClusterView::moveToTime, traceWidget, &TraceWidget::moveToTime);
        }
    } else if(displayType == WAVEFORMS) { //Connections for WaveformViews
        connect(this, &KlustersView::updatedTimeFrame, qobject_cast<WaveformView*>(view), &WaveformView::setTimeFrame);
        connect(this, &KlustersView::sampleMode, qobject_cast<WaveformView*>(view), &WaveformView::setSampleMode);
        connect(this, &KlustersView::timeFrameMode, qobject_cast<WaveformView*>(view), &WaveformView::setTimeFrameMode);
        connect(this, &KlustersView::meanPresentation, qobject_cast<WaveformView*>(view), &WaveformView::setMeanPresentation);
        connect(this, &KlustersView::allWaveformsPresentation, qobject_cast<WaveformView*>(view), &WaveformView::setAllWaveformsPresentation);
        connect(this, &KlustersView::overLayPresentation, qobject_cast<WaveformView*>(view), &WaveformView::setOverLayPresentation);
        connect(this, &KlustersView::sideBySidePresentation, qobject_cast<WaveformView*>(view), &WaveformView::setSideBySidePresentation);
        connect(this, &KlustersView::increaseAmplitude, qobject_cast<WaveformView*>(view), &WaveformView::increaseAmplitude);
        connect(this, &KlustersView::decreaseAmplitude, qobject_cast<WaveformView*>(view), &WaveformView::decreaseAmplitude);
        connect(this, &KlustersView::updateDisplayNbSpikes, qobject_cast<WaveformView*>(view), &WaveformView::setDisplayNbSpikes);
        connect(this, &KlustersView::changeGain, qobject_cast<WaveformView*>(view), &WaveformView::setGain);
        connect(this, &KlustersView::autoFitAmplitude, qobject_cast<WaveformView*>(view), &WaveformView::autoFitAmplitude);
        connect(this, &KlustersView::changeChannelPositions, qobject_cast<WaveformView*>(view), &WaveformView::setChannelPositions);
        connect(this, &KlustersView::clustersRenumbered, qobject_cast<WaveformView*>(view), &WaveformView::clustersRenumbered);
        connect(view, &QObject::destroyed, this, &KlustersView::waveformDockClosed);
    } else if(displayType == CORRELATIONS){ //Connections for CorrelationViews
        connect(this, &KlustersView::updatedBinSizeAndTimeFrame, qobject_cast<CorrelationView*>(view), &CorrelationView::setBinSizeAndTimeWindow);
        connect(this, &KlustersView::noScale, qobject_cast<CorrelationView*>(view), &CorrelationView::setNoScale);
        connect(this, &KlustersView::maxScale, qobject_cast<CorrelationView*>(view), &CorrelationView::setMaximumScale);
        connect(this, &KlustersView::shoulderScale, qobject_cast<CorrelationView*>(view), &CorrelationView::setShoulderScale);
        connect(this, &KlustersView::increaseAmplitudeofCorrelograms, qobject_cast<CorrelationView*>(view), &CorrelationView::increaseAmplitude);
        connect(this, &KlustersView::decreaseAmplitudeofCorrelograms, qobject_cast<CorrelationView*>(view), &CorrelationView::decreaseAmplitude);
        connect(this, &KlustersView::setShoulderLine, qobject_cast<CorrelationView*>(view), &CorrelationView::setShoulderLine);
        connect(this, &KlustersView::clustersRenumbered, qobject_cast<CorrelationView*>(view), &CorrelationView::clustersRenumbered);
        connect(view, &QObject::destroyed, this, &KlustersView::correlogramDockClosed);
    } else if(displayType == ERROR_MATRIX){ //Connections for ErrorMatrixViews
        connect(this, &KlustersView::computeProbabilities, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::updateMatrixContents);
        connect(view, &QObject::destroyed, this, &KlustersView::errorMatrixDockClosed);
        //connection with the document
        connect(&doc, &KlustersDoc::clustersGrouped, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::clustersGrouped);
        connect(&doc, &KlustersDoc::clustersDeleted, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::clustersDeleted);
        connect(&doc, &KlustersDoc::removeSpikesFromClusters, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::removeSpikesFromClusters);
        connect(&doc, &KlustersDoc::newClusterAdded, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::newClusterAdded);
        connect(&doc, static_cast<void(KlustersDoc::*)(QMap<int,int>&,QList<int>&)>(&KlustersDoc::newClustersAdded), qobject_cast<ErrorMatrixView*>(view), static_cast<void(ErrorMatrixView::*)(QMap<int,int>&,QList<int>&)>(&ErrorMatrixView::newClustersAdded));
        connect(&doc, &KlustersDoc::renumber, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::renumber);
        connect(&doc, &KlustersDoc::undoRenumbering, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::undoRenumbering);
        connect(&doc, &KlustersDoc::undoAdditionModification, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::undoAdditionModification);
        connect(&doc, &KlustersDoc::undoAddition, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::undoAddition);
        connect(&doc, &KlustersDoc::undoModification, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::undoModification);
        connect(&doc, &KlustersDoc::redoRenumbering, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::redoRenumbering);
        connect(&doc, &KlustersDoc::redoAdditionModification, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::redoAdditionModification);
        connect(&doc, &KlustersDoc::redoAddition, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::redoAddition);
        connect(&doc, &KlustersDoc::redoModification, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::redoModification);
        connect(&doc, &KlustersDoc::redoDeletion, qobject_cast<ErrorMatrixView*>(view), &ErrorMatrixView::redoDeletion);
        connect(&doc, static_cast<void(KlustersDoc::*)(QList<int>&)>(&KlustersDoc::newClustersAdded), qobject_cast<ErrorMatrixView*>(view), static_cast<void(ErrorMatrixView::*)(QList<int>&)>(&ErrorMatrixView::newClustersAdded));
        connect(this, &KlustersView::changeBackgroundColor, qobject_cast<BaseFrame*>(view), &BaseFrame::changeBackgroundColor);
    } else if(displayType == TEMPLATE_MATRIX){
        TemplateMatrixView* tmv = qobject_cast<TemplateMatrixView*>(view);
        connect(this, &KlustersView::computeTemplateMatrix, tmv, &TemplateMatrixView::updateMatrixContents);
        connect(view, &QObject::destroyed, this, &KlustersView::templateMatrixDockClosed);
        connect(&doc, &KlustersDoc::clustersGrouped,          tmv, &TemplateMatrixView::clustersGrouped);
        connect(&doc, &KlustersDoc::clustersDeleted,          tmv, &TemplateMatrixView::clustersDeleted);
        connect(&doc, &KlustersDoc::removeSpikesFromClusters, tmv, &TemplateMatrixView::removeSpikesFromClusters);
        connect(&doc, &KlustersDoc::newClusterAdded,          tmv, &TemplateMatrixView::newClusterAdded);
        connect(&doc, static_cast<void(KlustersDoc::*)(QMap<int,int>&,QList<int>&)>(&KlustersDoc::newClustersAdded),
                tmv,  static_cast<void(TemplateMatrixView::*)(QMap<int,int>&,QList<int>&)>(&TemplateMatrixView::newClustersAdded));
        connect(&doc, static_cast<void(KlustersDoc::*)(QList<int>&)>(&KlustersDoc::newClustersAdded),
                tmv,  static_cast<void(TemplateMatrixView::*)(QList<int>&)>(&TemplateMatrixView::newClustersAdded));
        connect(&doc, &KlustersDoc::renumber,                 tmv, &TemplateMatrixView::renumber);
        connect(this, &KlustersView::changeBackgroundColor, view, [view](const QColor& c){
            QPalette pal = view->palette(); pal.setColor(QPalette::Window, c);
            view->setPalette(pal); view->update(); });
    } else if(displayType == RESIDUAL_MATRIX){
        ResidualMatrixView* rmv = qobject_cast<ResidualMatrixView*>(view);
        connect(this, &KlustersView::computeResidualMatrix, rmv, &ResidualMatrixView::updateMatrixContents);
        connect(view, &QObject::destroyed, this, &KlustersView::residualMatrixDockClosed);
        connect(&doc, &KlustersDoc::clustersGrouped,          rmv, &ResidualMatrixView::clustersGrouped);
        connect(&doc, &KlustersDoc::clustersDeleted,          rmv, &ResidualMatrixView::clustersDeleted);
        connect(&doc, &KlustersDoc::removeSpikesFromClusters, rmv, &ResidualMatrixView::removeSpikesFromClusters);
        connect(&doc, &KlustersDoc::newClusterAdded,          rmv, &ResidualMatrixView::newClusterAdded);
        connect(&doc, static_cast<void(KlustersDoc::*)(QMap<int,int>&,QList<int>&)>(&KlustersDoc::newClustersAdded),
                rmv,  static_cast<void(ResidualMatrixView::*)(QMap<int,int>&,QList<int>&)>(&ResidualMatrixView::newClustersAdded));
        connect(&doc, static_cast<void(KlustersDoc::*)(QList<int>&)>(&KlustersDoc::newClustersAdded),
                rmv,  static_cast<void(ResidualMatrixView::*)(QList<int>&)>(&ResidualMatrixView::newClustersAdded));
        connect(&doc, &KlustersDoc::renumber,                 rmv, &ResidualMatrixView::renumber);
        connect(this, &KlustersView::changeBackgroundColor, view, [view](const QColor& c){
            QPalette pal = view->palette(); pal.setColor(QPalette::Window, c);
            view->setPalette(pal); view->update(); });
    } else if(displayType == TRACES){ //Connections for TraceViews
    
        connect(this, &KlustersView::updateContents, qobject_cast<TraceWidget*>(view), &TraceWidget::updateContents);
        connect(this, &KlustersView::singleColorUpdated, qobject_cast<TraceWidget*>(view), &TraceWidget::updateDrawing);
        connect(this, &KlustersView::updateClusters, qobject_cast<TraceWidget*>(view), &TraceWidget::updateClusters);

        connect(this, &KlustersView::updateDrawing, qobject_cast<BaseFrame*>(view), &BaseFrame::updateDrawing);
        connect(this, &KlustersView::changeBackgroundColor, qobject_cast<BaseFrame*>(view), &BaseFrame::changeBackgroundColor);
        connect(view, &QObject::destroyed, this, &KlustersView::traceDockClosed);
        connect(this, &KlustersView::increaseAllAmplitude, qobject_cast<TraceWidget*>(view), &TraceWidget::increaseAllChannelsAmplitude);
        connect(this, &KlustersView::decreaseAllAmplitude, qobject_cast<TraceWidget*>(view), &TraceWidget::decreaseAllChannelsAmplitude);
        connect(qobject_cast<TraceWidget*>(view), &TraceWidget::updateStartAndDuration, this, &KlustersView::setStartAndDuration);
        connect(this, &KlustersView::showLabels, qobject_cast<TraceWidget*>(view), &TraceWidget::showLabels);
        connect(this,&KlustersView::nextCluster,traceWidget,&TraceWidget::showNextCluster);
        connect(this,&KlustersView::previousCluster,traceWidget,&TraceWidget::showPreviousCluster);

        //Connect the TraceView to possible clusterViews
        if(isThereClusterView){
            int nbViews = viewList.count();
            for(int i = 0; i< nbViews; i++) {
                ViewWidget* viewWidget = viewList.at(i);
                if(qobject_cast<ClusterView*>(viewWidget)){
                    connect(qobject_cast<ClusterView*>(viewWidget), &ClusterView::moveToTime, qobject_cast<TraceWidget*>(view), &TraceWidget::moveToTime);
                }
            }
        }
    }
}

void KlustersView::updateTraceView(QString name,ItemColors* clusterColors,bool active){     
    //Set the list of the current clusters as the list of clusters to look up in the ClusterProvider.
    if(doc.getClustersProvider()  )
        doc.getClustersProvider()->setClusterIdList(shownClusters);

    emit updateClusters(name,*shownClusters,clusterColors,active);
}

void KlustersView::updateClustersProvider(){     
    //Set the list of the current view as the list of clusters to look up in the ClusterProvider.
    if(doc.getClustersProvider()  )
        doc.getClustersProvider()->setClusterIdList(shownClusters);
}

void KlustersView::updateCorrelogramConnections(ViewWidget* viewWidget){     
    //First disconnect the view
    QObject::disconnect(this, &KlustersView::updatedBinSizeAndTimeFrame, nullptr, nullptr);
    QObject::disconnect(this, &KlustersView::noScale, nullptr, nullptr);
    QObject::disconnect(this, &KlustersView::maxScale, nullptr, nullptr);
    QObject::disconnect(this, &KlustersView::shoulderScale, nullptr, nullptr);
    QObject::disconnect(this, &KlustersView::increaseAmplitudeofCorrelograms, nullptr, nullptr);
    QObject::disconnect(this, &KlustersView::decreaseAmplitudeofCorrelograms, nullptr, nullptr);
    QObject::disconnect(this, &KlustersView::setShoulderLine, nullptr, nullptr);

    //Connect the viewWidget
    connect(this, &KlustersView::updatedBinSizeAndTimeFrame, qobject_cast<CorrelationView*>(viewWidget), &CorrelationView::setBinSizeAndTimeWindow);
    connect(this, &KlustersView::noScale, qobject_cast<CorrelationView*>(viewWidget), &CorrelationView::setNoScale);
    connect(this, &KlustersView::maxScale, qobject_cast<CorrelationView*>(viewWidget), &CorrelationView::setMaximumScale);
    connect(this, &KlustersView::shoulderScale, qobject_cast<CorrelationView*>(viewWidget), &CorrelationView::setShoulderScale);
    connect(this, &KlustersView::increaseAmplitudeofCorrelograms, qobject_cast<CorrelationView*>(viewWidget), &CorrelationView::increaseAmplitude);
    connect(this, &KlustersView::decreaseAmplitudeofCorrelograms, qobject_cast<CorrelationView*>(viewWidget), &CorrelationView::decreaseAmplitude);
    connect(this, &KlustersView::setShoulderLine, qobject_cast<CorrelationView*>(viewWidget), &CorrelationView::setShoulderLine);
}

void KlustersView::updateTimeFrame(long start,long timeFrameWidth)
{
    startTime = start;
    timeWindow = timeFrameWidth;
    NS3_DIAG()<<" void KlustersView::updateTimeFrame(long start,long timeFrameWidth)";
    emit updatedTimeFrame(start,timeFrameWidth);
}

void KlustersView::focusClusterView()
{
    for (ViewWidget* w : qAsConst(viewList))
        if (qobject_cast<ClusterView*>(w)) {
            w->setFocus(Qt::OtherFocusReason);
            return;
        }
}

void KlustersView::disconnectAllChildren()
{
    for (ViewWidget* w : qAsConst(viewList))
        if (w) w->disconnect();
}
