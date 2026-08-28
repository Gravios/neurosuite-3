/***************************************************************************
                          clusterview.cpp  -  description
                             -------------------
    begin                : Thu Aug 21 2003
    copyright            : (C) 2003 by
    email                :
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

//include files for the application
#include "clusterview.h"
#include "klustersview.h"
#include "klustersdoc.h"
#include "data.h"
#include "itemcolors.h"
#include "configuration.h"

#include "timer.h"
#include <QDebug>
#include <QKeyEvent>
#include <QWheelEvent>

//General C++ include files
#include <math.h>
#include <stdlib.h>

// include files for Qt
#include <QPaintDevice>
#include <QApplication>
#include <QCursor>



#include <QPolygon>
#include <QList>
#include <QMouseEvent>
#include <QEvent>



const QColor ClusterView::NEW_CLUSTER_COLOR(Qt::green);
const QColor ClusterView::DELETE_NOISE_COLOR(220,220,220);
const QColor ClusterView::DELETE_ARTEFACT_COLOR(Qt::red);

ClusterView::ClusterView(KlustersDoc& doc,KlustersView& view,const QColor& backgroundColor,int timeInterval,QStatusBar * statusBar,QWidget* parent, const char* name,
                         int minSize, int maxSize, int windowTopLeft ,int windowBottomRight, int border) :
    ViewWidget(doc,view,backgroundColor,statusBar,parent,name,minSize,maxSize,windowTopLeft,windowBottomRight,border),
    selectionPolygon(0),
    nbSelectionPoints(0),
    polygonClosed(false)
{
    //Set the default mode
    mode = ZOOM;
    pointSize = 2;
    selectionLineWidth = 1;
    setFocusPolicy(Qt::StrongFocus);

    //Initialize internal variables
    timeDimension = doc.data().timeDimension();
    samplingInterval = doc.data().intervalOfSampling();
    setTimeStepInSecond(timeInterval);

    //Update the dimension of the window and the values of dimensionX and dimensionY
    //Qualified (non-virtual) call: ClusterView is the most-derived type in its
    //own constructor, so dispatch can't reach a further override anyway — making
    //the static binding explicit documents that and silences the virtual-call-
    //in-constructor warning.
    ClusterView::updatedDimensions(view.abscissaDimension(),view.ordinateDimension());

    newClusterCursor = QCursor(QPixmap(":/cursors/new_cluster_cursor.png"),0,0);
    newClustersCursor = QCursor(QPixmap(":/cursors/new_clusters_cursor.png"),0,0);
    deleteNoiseCursor = QCursor(QPixmap(":/cursors/delete_noise_cursor.png"),0,0);
    deleteArtefactCursor = QCursor(QPixmap(":/cursors/delete_artefact_cursor.png"),0,0);
    selectTimeCursor = QCursor(QPixmap(":/shared-cursors/select_time_cursor"),0,0);

    //The default tool is the zoom.
    setCursor(zoomCursor);

    //Allowed the mouse tracking to draw the tracking lines and write the mouse coordinates
    setMouseTracking(true) ;
}

ClusterView::~ClusterView(){
}

void ClusterView::drawClusters(QPainter& painter,const QList<int>& clustersList,bool drawCircles){
    //Loop on the clusters to be drawn
    QList<int>::const_iterator clusterIterator;

    ItemColors& clusterColors = doc.clusterColors();
    Data& clusteringData = doc.data();

    // Markers are drawn in VIEWPORT (pixel) space so their size is independent of
    // the axis range.  We reset the world-to-viewport transform, map each spike
    // point ourselves via worldToViewport(), and draw with fixed pixel dimensions.
    const QTransform savedTransform = painter.transform();
    painter.resetTransform();
    const int r = pointSize;           // pixel radius

    for (int clustId : clustersList) {
        const QColor clusterColor = clusterColors.color(clustId);
        painter.setPen(clusterColor);
        //Get the iterator on the spikes of the current cluster
        Data::Iterator spikeIterator = clusteringData.iterator(static_cast<dataType>(clustId));
        //Iterate over the spikes of the cluster and draw them
        if(drawCircles)  {
            painter.setBrush(clusterColor);
            painter.setPen(Qt::NoPen);
            for(;spikeIterator.hasNext();spikeIterator.next())
            {
                QPoint px = worldToViewport(spikeIterator(dimensionX,dimensionY));
                painter.drawEllipse(px.x() - r, px.y() - r, r*2, r*2);
            }
        }
        else  {
            QPen pen(clusterColor);
            pen.setWidth(r > 1 ? r : 1);
            painter.setPen(pen);
            for(;spikeIterator.hasNext();spikeIterator.next()){
                QPoint px = worldToViewport(spikeIterator(dimensionX,dimensionY));
                painter.drawPoint(px);
            }
        }
    }

    painter.setTransform(savedTransform);
    painter.setBrush(Qt::NoBrush);
}

void ClusterView::paintEvent ( QPaintEvent*){
    QPainter p(this);

    // If autoscale is enabled, refit bounds to the current shownClusters
    // projection before sampling `window` below.  Done only for the
    // REDRAW path: UPDATE is an incremental paint of just-changed
    // clusters and must preserve the existing window to avoid jittering
    // the plot on every cluster-set tweak.
    if (autoscaleEnabled && drawContentsMode == REDRAW) {
        autoscaleToVisibleClusters();
    }

    //set the window (part of the word I want to show)
    QRect r((QRect)window);
    if(drawContentsMode == UPDATE || drawContentsMode == REDRAW){
        viewport = contentsRect();
        //Resize the double buffer with the width and the height of the widget(QFrame)
        if (viewport.size() != doublebuffer.size()) {
            if(!doublebuffer.isNull()) {
                QPixmap tmp = QPixmap( viewport.width(),viewport.height() );
                tmp.fill( Qt::white );
                QPainter painter2( &tmp );
                painter2.drawPixmap( 0,0, doublebuffer );
                painter2.end();
                doublebuffer = tmp;
            } else {
                doublebuffer = QPixmap(viewport.width(),viewport.height());
            }
        }

        //Create a painter to paint on the double buffer
        QPainter painter;
        painter.begin(&doublebuffer);

        painter.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function

        if(drawContentsMode == REDRAW){
            //Reset the variables associates with the polygon

            //Resize selectionPolygon to remove all the last selected area, reinitialize nbSelectionPoints accordingly
            selectionPolygon.resize(0);
            nbSelectionPoints = 0;

            //Fill the double buffer with the background

            doublebuffer.fill(palette().color(backgroundRole()));

            //Draw the axes
            drawAxes(painter);

            //Paint all the clusters in the shownClusters list (in the double buffer)
            drawClusters(painter,view.clusters());
        } else if(drawContentsMode == UPDATE){

            //Erase any polygon of selection and reset the associated variables

            //Paint the the clusters to update contain in clusterUpdateList
            if(!clusterUpdateList.isEmpty())
                drawClusters(painter,clusterUpdateList);

            //Clear the update list
            clusterUpdateList.clear();
        }

        //reset transformation due to setWindow
        painter.resetTransform() ;


        //Draw the time axis information if the time is displayed
        drawTimeInformation(painter);

        //Closes the painter on the double buffer
        painter.end();

        //Back to the default
        drawContentsMode = REFRESH;
    }
    //if drawContentsMode == REFRESH, we reuse the double buffer (pixmap)

    //Draw the double buffer (pixmap) by copying it into the paint device.
    p.drawPixmap(0, 0, doublebuffer);



    if(!selectionPolygon.isEmpty()) {
        const QColor color = selectPolygonColor(mode);
        p.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function
        QPen selPen(color);
        selPen.setWidth(selectionLineWidth);
        p.setPen(selPen);
        p.drawPolyline(selectionPolygon);
    }

    // Watershed overlay (Shift+W preview mode).  Drawn last so it sits on
    // top of points and any selection polygon.  Both the overlay image
    // and the HUD text are repainted from scratch every paintEvent —
    // never cached into the doublebuffer — so KlustersApp can re-tune
    // sigma / threshold without forcing a full cluster redraw.
    if (!wsImage.isNull())
        paintWatershedOverlay(p, r);

    // DipSplit post-commit HUD (Shift+D confirm window).  Just a text
    // box at top-left — no scatter overlay, since the split has already
    // happened and the new clusters are visible via normal rendering.
    if (!dsHud.isEmpty())
        paintDipsplitPostCommitHud(p);
}

void ClusterView::setWatershedOverlay(const QImage& img,
                                       double xMin, double xMax,
                                       double yMin, double yMax,
                                       const QString& hud)
{
    wsImage = img;
    wsXMin = xMin; wsXMax = xMax;
    wsYMin = yMin; wsYMax = yMax;
    wsHud  = hud;
    // No drawContentsMode change — overlay is drawn over the existing
    // doublebuffer in REFRESH mode, which is what update() schedules.
    update();
}

void ClusterView::clearWatershedOverlay()
{
    if (wsImage.isNull() && wsHud.isEmpty()) return;
    wsImage = QImage();
    wsHud.clear();
    update();
}

void ClusterView::paintWatershedOverlay(QPainter& p, const QRect& worldRect)
{
    // ── Stretch the basin-coloured image into the watershed grid's world
    // ── rect.  The image rows correspond to feature-Y in the standard
    // ── orientation (small-Y at the bottom, large-Y at the top), but
    // ── ClusterView's draw window has its Y negated (so larger feature-Y
    // ── maps to smaller world-Y, i.e. visual top).  We therefore source
    // ── the image with a vertical flip via QPainter's automatic
    // ── source-rect interpretation:  pass a target rect with top = -yMax
    // ── and bottom = -yMin, which makes image row 0 land at -yMax (top).
    // ── KlustersApp built the image so that row 0 already represents the
    // ── largest feature-Y (the build flips during setPixel).
    p.save();
    p.setWindow(worldRect.left(), worldRect.top(),
                worldRect.width()-1, worldRect.height()-1);

    const QRectF tgt(QPointF(wsXMin, -wsYMax),
                     QPointF(wsXMax, -wsYMin));

    // QPainter::drawImage with a target rect performs both translation
    // and scaling.  Use SmoothPixmapTransform off — the basin colours
    // are flat fills, so nearest-neighbour gives sharp boundaries.
    const bool prevSmooth = p.testRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(tgt, wsImage);
    p.setRenderHint(QPainter::SmoothPixmapTransform, prevSmooth);

    p.restore();

    // ── HUD: drawn in viewport pixels, top-left, with a translucent
    // ── dark background for legibility against arbitrary scatter
    // ── backgrounds.  The painter's transform was already restored above.
    if (!wsHud.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(10);
        p.setFont(f);
        const QFontMetrics fm(p.font());
        const QRect textBounds = fm.boundingRect(
            QRect(0, 0, 600, 200),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            wsHud);
        const QRect bg = textBounds.translated(8, 8).adjusted(-6, -3, 8, 3);
        p.fillRect(bg, QColor(0, 0, 0, 180));
        p.setPen(QColor(255, 255, 255));
        p.drawText(QRect(8, 8, 600, 200),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   wsHud);
    }
}

void ClusterView::setDipsplitPostCommitHud(const QString& hud)
{
    if (hud.isEmpty()) {
        clearDipsplitPostCommitHud();
        return;
    }
    dsHud = hud;
    // Plain update() — the HUD draws on top of the existing doublebuffer
    // contents in REFRESH mode, no buffer rebuild needed.  Same pattern
    // as setWatershedOverlay.
    update();
}

void ClusterView::clearDipsplitPostCommitHud()
{
    if (dsHud.isEmpty()) return;
    dsHud.clear();
    update();
}

void ClusterView::paintDipsplitPostCommitHud(QPainter& p)
{
    // Top-left text block in viewport pixels — same style as the
    // watershed HUD: dark translucent background, white text.  No
    // scatter overlay; the post-commit state is the actual cluster
    // configuration, drawn through normal rendering paths.
    QFont f = p.font();
    f.setPointSize(10);
    p.setFont(f);
    const QFontMetrics fm(p.font());
    const QRect textBounds = fm.boundingRect(
        QRect(0, 0, 600, 200),
        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
        dsHud);
    const QRect bg = textBounds.translated(8, 8).adjusted(-6, -3, 8, 3);
    p.fillRect(bg, QColor(0, 0, 0, 180));
    p.setPen(QColor(255, 255, 255));
    p.drawText(QRect(8, 8, 600, 200),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               dsHud);
}

void ClusterView::eraseTheLastDrawnLine()
{
    //The user did not move since the last left click (no mouseMoveEvent)
    if(nbSelectionPoints == selectionPolygon.size()){
        //Treat the case when we reach the first point of the selection
        if(nbSelectionPoints == 1){
            //Resize selectionPolygon to remove the point from selectionPolygon
            selectionPolygon.resize(0);
            nbSelectionPoints = 0;
        } else {
            //Resize selectionPolygon to remove the last point from selectionPolygon
            selectionPolygon.resize(selectionPolygon.size()-1);
            nbSelectionPoints = selectionPolygon.size();
        }
    }
    //The user moved since the last left click, a line has been drawn in the mouseMoveEvent
    else{
        //Resize selectionPolygon to remove the 2 last points
        //(the last selected and the one set in mouseMoveEvent) from selectionPolygon
        selectionPolygon.resize(selectionPolygon.size()-2);

        nbSelectionPoints = selectionPolygon.size();
    }
}

void ClusterView::eraseTheLastMovingLine()
{
    //The user moved since the last left click, a line has been drawn in the mouseMoveEvent
    if(nbSelectionPoints != selectionPolygon.size()){
        selectionPolygon.resize(selectionPolygon.size()-1);
        nbSelectionPoints = selectionPolygon.size();
    }
}

void ClusterView::addClusterToUpdate(int clusterId){
    //Add the the cluster id to the clusterUpdateList,
    // so it will be updated during the next update
    if(drawContentsMode == REFRESH){
        clusterUpdateList.append(clusterId);
        drawContentsMode = UPDATE;
    }
    else if(drawContentsMode == UPDATE)
        clusterUpdateList.append(clusterId);
}


void ClusterView::worldBoundsFromExtrema(long& aMin, long& aMax,
                                          long& oMin, long& oMax) const {
    Data& clusteringData = doc.data();
    long maxForDimensionX = static_cast<long>(clusteringData.maxDimension(dimensionX));
    long minForDimensionX = static_cast<long>(clusteringData.minDimension(dimensionX));
    long maxForDimensionY = static_cast<long>(clusteringData.maxDimension(dimensionY));
    long minForDimensionY = static_cast<long>(clusteringData.minDimension(dimensionY));

    //The min and max are chosen in a maner that the axis are always visible and superior
    //to -40000000 (due to a Qt limitation in the big negative values).
    long width = maxForDimensionX - minForDimensionX;
    aMin = static_cast<long>(qMin(0L,minForDimensionX)-width*0.05);
    aMin = static_cast<long>(qMax(aMin,-1000000L)); // below this limit, Qt crashes
    aMax = static_cast<long>(qMax(0L,maxForDimensionX)+width*0.05);

    long height = maxForDimensionY - minForDimensionY;
    oMin = static_cast<long>(-qMax(0L,maxForDimensionY)-height*0.05);
    oMax = static_cast<long>(-qMin(0L,minForDimensionY)+height*0.05);
    oMax = static_cast<long>(qMin(oMax,1000000L)); // below -(this limit), Qt crashes
}

void ClusterView::updatedDimensions(int dimensionX, int dimensionY){
    this->dimensionX = dimensionX;
    this->dimensionY = dimensionY;

    worldBoundsFromExtrema(abscissaMin, abscissaMax, ordinateMin, ordinateMax);

    //Update the window in a maner to always see the axis
    window = ZoomWindow(QRect(QPoint(abscissaMin,ordinateMin),QPoint(abscissaMax,ordinateMax)));

    drawContentsMode = REDRAW;

    //reset the information on the polygon to enable a mousetrack in mousemovEvent
    polygonClosed = false;
}

bool ClusterView::recomputeWorldBounds(){
    long aMin = 0, aMax = 0, oMin = 0, oMax = 0;
    worldBoundsFromExtrema(aMin, aMax, oMin, oMax);
    if (aMin == abscissaMin && aMax == abscissaMax
            && oMin == ordinateMin && oMax == ordinateMax)
        return false;   // extrema recompute left the world where it was

    // Capture the old state BEFORE overwriting: a window that differs from
    // the old world is the user's deliberate zoom and must survive.
    const QRect oldWorld(QPoint(abscissaMin, ordinateMin),
                         QPoint(abscissaMax, ordinateMax));
    const QRect current = static_cast<QRect>(window);
    const bool  zoomed  = (current != oldWorld);

    abscissaMin = aMin; abscissaMax = aMax;
    ordinateMin = oMin; ordinateMax = oMax;
    window = ZoomWindow(QRect(QPoint(abscissaMin, ordinateMin),
                              QPoint(abscissaMax, ordinateMax)));
    if (zoomed)
        // Re-apply the user's window, clamped into the new world by
        // ZoomWindow's own correction.  If the zoom is refused (scale
        // limits), the view falls back to the full new world -- everything
        // visible, nothing clipped.
        window.zoom(current.topLeft(), current.bottomRight());
    return true;
}

void ClusterView::dimensionExtremaChanged(){
    // Common case: the recompute confirmed the old bounds; skip the repaint.
    if (!recomputeWorldBounds())
        return;
    drawContentsMode = REDRAW;
    update();
}

void ClusterView::clusterFeaturesReprojected(int /*clusterId*/){
    // The reprojection can WIDEN the dimension extrema (the Data side widens
    // them synchronously on the realign path); redrawing inside the old
    // world clips the shifted points, so the Data fix is invisible without
    // this one.  With autoscale enabled paintEvent refits anyway and simply
    // overwrites this.
    recomputeWorldBounds();
    drawContentsMode = REDRAW;
    update();
}

void ClusterView::setMode(BaseFrame::Mode selectedMode){
    statusBar->clearMessage();
    selectionPolygon.clear();
    nbSelectionPoints = 0;
    mode = selectedMode;

    //set the cursor according to the selected mode.
    switch(mode){
    case DELETE_NOISE:
        setCursor(deleteNoiseCursor);
        break;
    case DELETE_ARTEFACT:
        setCursor(deleteArtefactCursor);
        break;
    case NEW_CLUSTER:
        setCursor(newClusterCursor);
        break;
    case NEW_CLUSTERS:
        setCursor(newClustersCursor);
        break;
    case ZOOM:
        setCursor(zoomCursor);
        break;
    case SELECT_TIME:
        setCursor(selectTimeCursor);
        break;
    }
    drawContentsMode = REFRESH;
    update();
}


void ClusterView::mousePressEvent(QMouseEvent* e){
    // Ctrl+Left arms a pan and takes precedence over every selection / zoom mode
    // (it is a navigation gesture).  Don't forward to the base, so no rubber-band
    // is started.
    if((e->button() == Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        ctrlPanArmed       = true;
        ctrlPanning        = false;
        ctrlPanAnchorPx    = e->position().toPoint();
        const QPoint w     = viewportToWorld(ctrlPanAnchorPx.x(), ctrlPanAnchorPx.y());
        ctrlPanPressWorldX = w.x();
        ctrlPanPressWorldY = w.y();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    //Defining a time window t oupdate the Traceview
    if(mode == SELECT_TIME){
        QPoint current = viewportToWorld(e->position().toPoint().x(),e->position().toPoint().y());
        if(dimensionX == timeDimension){
            dataType time = static_cast<dataType>(current.x() * samplingInterval / 1000.0);
            emit moveToTime(time);
        }
        else if(dimensionY == timeDimension){
            dataType time = -static_cast<dataType>(current.y() * samplingInterval / 1000.0);
            emit moveToTime(time);
        }
    }

    //The parent implementation takes care of the mode ZOOM
    //(rubber band and calculation of the firstClick)
    ViewWidget::mousePressEvent(e);

    //If there is a polygon to draw (one of the selection modes)
    if(mode == DELETE_NOISE || mode == DELETE_ARTEFACT || mode == NEW_CLUSTER || mode == NEW_CLUSTERS){
        //Erase the last line drawn
        if(e->button() == Qt::RightButton){
            if(selectionPolygon.isEmpty())
                return;

            //Erase the last drawn line by drawing into the buffer
            eraseTheLastDrawnLine();
            drawContentsMode = REFRESH;
            update();
        }

        //Close the polygon of selection and trigger the right action depending on the mode
        if(e->button() == Qt::MiddleButton && !selectionPolygon.isEmpty()){
            //If, once the last moving line erase, the polygon exists and has at least 3 points, draw it
            if(selectionPolygon.size()>2){
                //erase the last line drawn if the user moved since the last click
                eraseTheLastMovingLine();
                polygonClosed = true;

                //Send an event to inform that the data have to be recompute accordingly to the selection polygon.
                //This asynchronous event will allow the widget to close the polygon
                //before asking the document to compute the data.
                ComputeEvent* event = getComputeEvent(selectionPolygon);
                QApplication::postEvent(this,event);

            }
            drawContentsMode = REFRESH;
            update();
            statusBar->clearMessage();
        }

        if (e->button() == Qt::LeftButton){
            // Ensure this widget has keyboard focus so Enter/Return keyPressEvent
            // is delivered here and not consumed by a parent widget or dialog.
            setFocus(Qt::MouseFocusReason);
            QPoint selectedPoint = viewportToWorld(e->position().toPoint().x(),e->position().toPoint().y());

            if(nbSelectionPoints == 0)
                selectionPolygon.putPoints(0, 1, selectedPoint.x(),selectedPoint.y());
            //If the array is not empty, the last point has been put into the array in mouseMoveEvent
            nbSelectionPoints = selectionPolygon.size();
            drawContentsMode = REFRESH;
            update();
        }
    }
}

void ClusterView::mouseReleaseEvent(QMouseEvent* event){
    // End a Ctrl+drag pan.  Ctrl+Left is fully owned by the pan gesture (its
    // press was intercepted, so the base never started a rubber-band / click-zoom)
    // — consume the release whether or not a drag actually occurred.
    if(ctrlPanArmed){
        ctrlPanArmed = false;
        ctrlPanning  = false;
        unsetCursor();
        event->accept();
        return;
    }
    //Trigger parent event
    ViewWidget::mouseReleaseEvent(event);
    statusBar->clearMessage();
}

// Ctrl + wheel zooms toward the cursor, keeping the world point under the
// pointer fixed (factor>1 enlarges, factor<1 zooms out).  ZoomWindow::zoom
// re-centres on the point it is given, so pass an adjusted centre that leaves the
// cursor's world point at the same screen fraction instead of jumping it to the
// middle.  Without Ctrl the event is handed to the base so existing wheel
// behaviour is unchanged.
void ClusterView::wheelEvent(QWheelEvent* e){
    if(!(e->modifiers() & Qt::ControlModifier)){
        ViewWidget::wheelEvent(e);
        return;
    }
    const int delta = e->angleDelta().y();
    if(delta == 0){ e->accept(); return; }
    const float  factor = (delta > 0) ? ctrlWheelZoomStep : (1.0f / ctrlWheelZoomStep);
    const QPoint p  = viewportToWorld(e->position().toPoint().x(),
                                      e->position().toPoint().y());
    const QRect  wr = (QRect)window;
    const double W = wr.width(), H = wr.height();
    if(W > 0.0 && H > 0.0){
        // Cursor's fraction within the current window == its screen fraction.
        const double fx = (static_cast<double>(p.x()) - wr.left()) / W;
        const double fy = (static_cast<double>(p.y()) - wr.top())  / H;
        // New window size; centre that keeps the cursor point at the same fraction.
        const double Wn = W / static_cast<double>(factor);
        const double Hn = H / static_cast<double>(factor);
        const double cx = static_cast<double>(p.x()) + Wn * (0.5 - fx);
        const double cy = static_cast<double>(p.y()) + Hn * (0.5 - fy);
        if(window.zoom(factor, static_cast<float>(cx), static_cast<float>(cy))){
            drawContentsMode = REDRAW;
            update();
        }
    }
    e->accept();
}

void ClusterView::keyPressEvent(QKeyEvent* e){
    // Enter or Return closes the selection polygon, same as middle mouse button
    if((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
       (mode == DELETE_NOISE || mode == DELETE_ARTEFACT ||
        mode == NEW_CLUSTER  || mode == NEW_CLUSTERS) &&
       selectionPolygon.size() > 2)
    {
        eraseTheLastMovingLine();
        polygonClosed = true;
        ComputeEvent* event = getComputeEvent(selectionPolygon);
        QApplication::postEvent(this, event);
        drawContentsMode = REFRESH;
        update();
        statusBar->clearMessage();
        return;
    }

    // 'F' toggles autoscale-to-visible-clusters mode.  When enabled, the
    // view refits bounds to the current shownClusters projection each
    // time paintEvent redraws the double buffer — so moving dimensions,
    // adding/removing clusters, or running ops automatically rescales.
    // Press F again to return to manual zoom (bounds stay at whatever
    // the last autoscale produced, then persist under normal zoom ops).
    if (e->key() == Qt::Key_F && e->modifiers() == Qt::NoModifier) {
        autoscaleEnabled = !autoscaleEnabled;
        if (autoscaleEnabled) {
            autoscaleToVisibleClusters();
            drawContentsMode = REDRAW;
            update();
            statusBar->showMessage(tr("Autoscale: on (press F to disable)"), 3000);
        } else {
            statusBar->showMessage(tr("Autoscale: off"), 3000);
        }
        return;
    }

    ViewWidget::keyPressEvent(e);
}

void ClusterView::autoscaleToVisibleClusters()
{
    const QList<int> shown = view.clusters();
    if (shown.isEmpty()) return;    // nothing to fit to — leave bounds alone

    Data& clusteringData = doc.data();

    bool haveAny = false;
    long xMin = 0, xMax = 0, yMin = 0, yMax = 0;

    // Iterate every spike of every shown cluster at the current
    // (dimensionX, dimensionY) projection.  spikeIterator(dx, dy) yields
    // a QPoint with the two coordinates; we reduce to min/max.  The cost
    // is O(totalShownSpikes); same order as a single paintEvent pass, so
    // adding this on autoscale toggle is not a performance concern.
    for (int clustId : shown) {
        Data::Iterator it = clusteringData.iterator(static_cast<dataType>(clustId));
        for (; it.hasNext(); it.next()) {
            const QPoint p = it(dimensionX, dimensionY);
            if (!haveAny) {
                xMin = xMax = p.x();
                yMin = yMax = p.y();
                haveAny = true;
            } else {
                if (p.x() < xMin) xMin = p.x();
                if (p.x() > xMax) xMax = p.x();
                if (p.y() < yMin) yMin = p.y();
                if (p.y() > yMax) yMax = p.y();
            }
        }
    }

    if (!haveAny) return;   // shownClusters non-empty but no spikes (edge case)

    // Margin fraction per side, pulled from general preferences (default 5%).
    // The Configuration setter clamps to [0, 50] %, so the value is safe to
    // use directly without re-validating here.
    const double marginFrac = configuration().getAutoscaleMarginPercent() / 100.0;

    // Keep axes visible + clamp to the Qt-safe range used throughout the
    // view (matches updatedDimensions).
    //
    // Iterator::operator()(dx, dy) returns a QPoint with the ordinate
    // ALREADY NEGATED (Qt graphical orientation, see data.h:344–348), so
    // yMin/yMax are in the same screen-orientation space that
    // abscissaMin/Max / ordinateMin/Max live in — no extra flip needed.
    // The qMin(0L, …)/qMax(0L, …) widening keeps the origin (axes) in
    // view even when the data cluster sits entirely on one side.
    const long width  = xMax - xMin;
    const long height = yMax - yMin;

    abscissaMin = static_cast<long>(qMin(0L, xMin) - width  * marginFrac);
    abscissaMin = qMax(abscissaMin, -1000000L);
    abscissaMax = static_cast<long>(qMax(0L, xMax) + width  * marginFrac);

    ordinateMin = static_cast<long>(qMin(0L, yMin) - height * marginFrac);
    ordinateMax = static_cast<long>(qMax(0L, yMax) + height * marginFrac);
    ordinateMax = qMin(ordinateMax,  1000000L);

    window = ZoomWindow(QRect(QPoint(abscissaMin, ordinateMin),
                              QPoint(abscissaMax, ordinateMax)));
}

void ClusterView::mouseMoveEvent(QMouseEvent* e){
    // Ctrl+drag pan: keep the world point grabbed at press under the cursor.
    // Re-centre the window each move (size unchanged) — zoom(1.0, c) recentres
    // and clamps to the full extent.  Computed against the current window so it
    // does not drift as the view moves.
    if(ctrlPanArmed && (e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        const QPoint dpx = e->position().toPoint() - ctrlPanAnchorPx;
        if(!ctrlPanning && (qAbs(dpx.x()) + qAbs(dpx.y()) >= ctrlPanDragThreshold))
            ctrlPanning = true;
        if(ctrlPanning){
            const QPoint cw = viewportToWorld(e->position().toPoint().x(),
                                              e->position().toPoint().y());
            const QRect  wr = (QRect)window;
            const double curCx = wr.left() + wr.width()  / 2.0;
            const double curCy = wr.top()  + wr.height() / 2.0;
            const double newCx = curCx - static_cast<double>(cw.x() - ctrlPanPressWorldX);
            const double newCy = curCy - static_cast<double>(cw.y() - ctrlPanPressWorldY);
            if(window.zoom(1.0f, static_cast<float>(newCx), static_cast<float>(newCy))){
                drawContentsMode = REDRAW;
                update();
            }
        }
        e->accept();
        return;
    }

    //Write the current coordinates in the statusbar.
    QPoint current = viewportToWorld(e->position().toPoint().x(),e->position().toPoint().y());

    if(dimensionX == timeDimension){
        int timeInS = static_cast<int>(current.x() * samplingInterval / 1000000.0);
        statusBar->showMessage("Coordinates: (" + QString::fromLatin1("%1").arg(timeInS) + ", " + QString::fromLatin1("%1").arg(-current.y()) + ")");
    }
    else if(dimensionY == timeDimension){
        int timeInS = static_cast<int>(current.y() * samplingInterval / 1000000.0);
        statusBar->showMessage("Coordinates: (" + QString::number(current.x()) + ", " + QString::fromLatin1("%1").arg(-timeInS) + ")");
    }
    else
        statusBar->showMessage("Coordinates: (" + QString::number(current.x()) + ", " + QString::fromLatin1("%1").arg(-current.y()) + ")");



    //The parent implementation takes care of the rubber band
    ViewWidget::mouseMoveEvent(e);

    //If the user is closing the polygon do not take mousemove event into account
    if(!polygonClosed){
        //In one of the selection modes we draw the tracking line
        if(mode == DELETE_NOISE || mode == DELETE_ARTEFACT || mode == NEW_CLUSTER || mode == NEW_CLUSTERS){


            //If there is no selection point, do not draw a tracking line
            if(selectionPolygon.isEmpty())
                return;
            //First mouseMoveEvent after the last mousePressEvent
            if(nbSelectionPoints == selectionPolygon.size()){
                //Add the current point to the array
                selectionPolygon.putPoints(selectionPolygon.size(), 1, current.x(),current.y());
            }
            else{
                selectionPolygon.setPoint(selectionPolygon.size()-1,current);
            }
            drawContentsMode = REFRESH;
            update();
        }
    }
}

void ClusterView::customEvent(QEvent* event){
    if(event->type() == QEvent::User + 700){
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        ComputeEvent* computeEvent = (ComputeEvent*) event;
        //Get the polygon
        QPolygon polygon = computeEvent->polygon();
        QRegion selectionArea;
        QPolygon reviewPolygon;
        long Xdimension = 0;
        long Ydimension = 0;

        //The QRegion uses rectangles to define its area and the number of rectangles
        //increases with the height of the region (y axis). The more rectangles the longer
        //the search of one point in the region will take. With a dimension like the time
        //the height has an order of the millon (at least 5 going to 80 or more) given a huge amount
        //of rectangles. A way of speeding the search of points is to reduce the number of rectangles.
        //To do so, if the y dimension is the time, x and y axis are inverted.
        //Caution: in Qt graphical coordinate system, the Y axis is inverted (increasing downwards),
        //thus a point (x,y) is drawn as (x,-y), before creating the region the points are reset to there raw value (x,y).

        if(view.ordinateDimension() != timeDimension){
            for(uint i = 0; i< polygon.size();++i){
                reviewPolygon.putPoints(i, 1,polygon.point(i).x(),-polygon.point(i).y());
                Xdimension = dimensionX;
                Ydimension = dimensionY;
            }
        }
        else{
            for(uint i = 0; i< polygon.size();++i){
                reviewPolygon.putPoints(i, 1,-polygon.point(i).y(),polygon.point(i).x());

                Xdimension = dimensionY;
                Ydimension = dimensionX;
            }
        }
        //Create a QRegion with the new selection area in order to use the research facilities offer by a QRegion.
        selectionArea = QRegion(reviewPolygon);
        if(!selectionArea.isEmpty()){
            //Call any appropriate method
            switch(mode){
            case DELETE_NOISE:
                doc.deleteNoise(selectionArea,view.clusters(),Xdimension,Ydimension);
                break;
            case DELETE_ARTEFACT:
                doc.deleteArtifact(selectionArea,view.clusters(),Xdimension,Ydimension);
                break;
            case NEW_CLUSTER:
                doc.createNewCluster(selectionArea,view.clusters(),Xdimension,Ydimension);
                setFocus(Qt::OtherFocusReason);
                break;
            case NEW_CLUSTERS:
                doc.createNewClusters(selectionArea,view.clusters(),Xdimension,Ydimension);
                setFocus(Qt::OtherFocusReason);
                break;
            case ZOOM:
                break; //nothing to do
            }
        }
        QApplication::restoreOverrideCursor();
    }
}

void ClusterView::drawAxes(QPainter& painter){
    painter.setPen(QColor(60,60,60));    //set the color for the lines
    painter.drawLine(abscissaMin,0,abscissaMax,0); // draw line
    painter.drawLine(0,ordinateMin,0,ordinateMax); // draw line
}

void ClusterView::drawTimeInformation(QPainter& painter){
    if(dimensionX == timeDimension){
        painter.setPen(QColor(60,60,60)); //set the color for the lines
        int markHeight = static_cast<int>(abs(ordinateMax - ordinateMin));
        QFont f("Helvetica",9);
        painter.setFont(f);
        long time = 0;
        QRect r((QRect)window);
        long legendOrdinate = worldToViewportOrdinate(r.top()) + 20;


        for(long i=0; i < abscissaMax; i += timeStepInRecordingUnit){
            QPoint topInViewport = worldToViewport(i,-markHeight);
            QPoint bottomInViewport = worldToViewport(i,markHeight);

            painter.drawLine(topInViewport,bottomInViewport);
            painter.drawText(topInViewport.x() + 1,legendOrdinate,QString::fromLatin1("%1").arg(time));

            time += timeStepInSecond;
        }
    }
    else{
        if(dimensionY == timeDimension){
            painter.setPen(QColor(60,60,60)); //set the color for the lines
            QFont f("Helvetica",9);
            painter.setFont(f);
            long time = 0;
            QRect r((QRect)window);
            long legendAbsciss = worldToViewportAbscissa(r.left());


            int markHeight = static_cast<int>(abs(abscissaMax - abscissaMin));
            //ordinateMin is used because in QT coordinate system the ordinate axis in downwards oriented
            //see the function updatedDimensions for detail.
            for(long i=0; i < abs(ordinateMin); i += timeStepInRecordingUnit){
                QPoint topInViewport = worldToViewport(-markHeight,-i);
                QPoint bottomInViewport = worldToViewport(markHeight,-i);

                painter.drawLine(topInViewport,bottomInViewport);
                painter.drawText(legendAbsciss,topInViewport.y(),QString::fromLatin1("%1").arg(time));

                time += timeStepInSecond;
            }
        }
    }
}


void ClusterView::print(QPainter& printPainter,int width,int height, bool whiteBackground){
    //Draw the double buffer (pixmap) by copying it into the printer device throught the painter.
    QRect viewportOld = QRect(viewport.left(),viewport.top(),viewport.width(),viewport.height());

    viewport = QRect(printPainter.viewport().left(),printPainter.viewport().top(),printPainter.viewport().width(),printPainter.viewport().height());

    QRect r = ((QRect)window);

    //Set the window (part of the world I want to show)
    printPainter.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function

    //Fill the background with the background color
    QRect back = QRect(r.left(),r.top(),r.width(),r.height());

    QColor colorLegendTmp = colorLegend;
    QColor background= palette().color(backgroundRole());
    if(whiteBackground){
        colorLegend = Qt::black;
        QPalette palette;
        palette.setColor(backgroundRole(), Qt::white);
        setPalette(palette);
    }

    printPainter.fillRect(back,palette().color(backgroundRole()));
    printPainter.setClipRect(back);

    //Draw the axes
    drawAxes(printPainter);

    //Paint all the clusters in the shownClusters list (in the double buffer)
    drawClusters(printPainter,view.clusters(),true);

    //reset transformation due to setWindow and setViewport
    printPainter.resetTransform();

    //Draw the time axis information if the time is displayed
    drawTimeInformation(printPainter);

    printPainter.setClipping(false);

    //Restore the colors.
    if(whiteBackground){
        colorLegend = colorLegendTmp;
        QPalette palette;
        palette.setColor(backgroundRole(), background);
        setPalette(palette);
    }

    //Restore the previous state
    viewport = QRect(viewportOld.left(),viewportOld.top(),viewportOld.width(),viewportOld.height());
}
