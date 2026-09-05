/***************************************************************************
                          custerview.h  -  description
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

#ifndef CLUSTERVIEW_H
#define CLUSTERVIEW_H

// include files for QT
#include <QPainter>
#include <QStyle>
#include <QPixmap>
#include <QPolygon>
#include <QTimer>
#include <QRegion>
#include <QList>
#include <atomic>
#include <vector>
#include <QImage>


#include <QResizeEvent>
#include <QMouseEvent>
#include <QEvent>

//include files for the application
#include "zoomwindow.h"
#include "viewwidget.h"
#include "types.h"


class KlustersDoc;
class KlustersView;
class QCursor;

/**
  * View displaying spikes in the PCA feature space.
  * Through this view the user can act upon the clusters by selecting spikes.
  * All modification request is sent to the KlustersDoc object and the view
  * is automatically updated via KlustersView when the clusters have been changed.
  *@author Lynn Hazan
  */

class ClusterView : public ViewWidget  {
    Q_OBJECT

public:
    ClusterView(KlustersDoc& doc, KlustersView& view, const QColor &backgroundColor, int timeInterval, QStatusBar* statusBar, QWidget* parent=nullptr, const char* name=nullptr,
                int minSize = 50, int maxSize = 4000,
                int windowTopLeft = -500, int windowBottomRight = 1001, int border = 0);
    ~ClusterView();

    /** Toggles the t-SNE alternate presentation of the selected clusters
     *  (cancels instead while a computation is in flight).  Public because
     *  the application-wide key filter owns the F key: view-local handlers
     *  are unreachable from palette focus, which is where focus lives after
     *  almost every operation. */
    void toggleTsnePresentation();

    /** Toggles autoscale-to-visible-clusters.  Public for the same reason:
     *  the A key is dispatched by the application filter. */
    void toggleAutoscale();

    /** True while the t-SNE presentation is showing or being computed.  The
     *  application filter gates the arrow keys on this so palette navigation
     *  is untouched everywhere else. */
    bool isTsneActive() const {return tsneMode || tsneComputing;}

    /** Steps the perplexity by the configured increment (@p direction is +1
     *  for up, -1 for down) and recomputes the embedding on the same
     *  selection.  The current embedding stays on screen until the new one
     *  lands.  Refuses while a computation is already in flight. */
    void adjustTsnePerplexity(int direction);

    /**Informs if the user is currently making a selection.
  * @return true if a selection is in process, false othewise.
  */
    bool isASelectionInProcess() const{
        if(selectionPolygon.isEmpty())
            return false;
        else
            return true;
    }

    /**Returns the current abscissa dimension.
  */
    int getDimensionX() const{return dimensionX;}

    int getPointSize() const{return pointSize;}
    void setPointSize(int size){pointSize = qBound(1, size, 10); redraw();}

    int getSelectionLineWidth() const{return selectionLineWidth;}
    void setSelectionLineWidth(int w){selectionLineWidth = qBound(1, w, 10); update();}

    // ── Watershed preview overlay ────────────────────────────────────────
    // Used by KlustersApp during the "Shift+W" interactive watershed
    // preview mode.  KlustersApp computes a coloured ARGB image of the
    // basin labelling (one colour per basin, unassigned cells fully
    // transparent), passes it here together with the world-coordinate
    // bounds the watershed grid spans, and the view paints the image
    // stretched into those bounds on top of the cluster scatter.  The
    // overlay is drawn fresh in every paintEvent (never cached into the
    // doublebuffer) so the basin colouring can be re-tuned without
    // forcing a full cluster redraw.
    //
    // @param img      ARGB image; basin colours with alpha, transparent
    //                 elsewhere.  May be of any size; will be stretched
    //                 to the world rect on draw.  Pass an empty image
    //                 to clear (or call clearWatershedOverlay()).
    // @param xMin,xMax  World feature-X span the watershed grid covers
    //                   (raw, unflipped).
    // @param yMin,yMax  Same for Y (raw, unflipped — the view's
    //                   negate-Y convention is applied internally).
    // @param hud      Short status text drawn at top-left in viewport
    //                 pixels.  Pass empty string to suppress.
    void setWatershedOverlay(const QImage& img,
                             double xMin, double xMax,
                             double yMin, double yMax,
                             const QString& hud);
    void clearWatershedOverlay();
    bool hasWatershedOverlay() const { return !wsImage.isNull(); }

    // ── DipSplit post-commit HUD ─────────────────────────────────────────
    // Used by KlustersApp after a Shift+D dipsplit commits.  Draws a
    // short multi-line status block at top-left in viewport pixels
    // (dark translucent background, white text) reporting the metrics
    // of the just-committed split and the available next actions
    // ("Esc: undo   Enter: keep").  No scatter overlay — the split
    // already happened, so the source cluster is gone and both new
    // clusters are visible via normal scatter rendering.
    //
    // KlustersApp owns the lifecycle:
    //   setDipsplitPostCommitHud(text)  — draw the HUD
    //   clearDipsplitPostCommitHud()    — drop it
    void setDipsplitPostCommitHud(const QString& hud);
    void clearDipsplitPostCommitHud();
    bool hasDipsplitPostCommitHud() const { return !dsHud.isEmpty(); }

    /**Returns the current ordinate dimension.
  */
    int getDimensionY() const{return dimensionY;}

    BaseFrame::Mode getMode() const {return mode;}

public Q_SLOTS:

    /** A cluster's features were recomputed: refresh the world bounds and
     *  redraw the whole view.
     *
     *  NOT addClusterToUpdate().  That queues an INCREMENTAL paint, which draws the
     *  cluster's points on top of what is already there -- correct when a cluster
     *  gains or loses spikes, wrong when reprojection moves every point it has,
     *  because the old positions are never erased.
     *
     *  The world refresh matters because the reprojection can WIDEN the
     *  dimension extrema (Data widens them synchronously on the realign
     *  path): redrawing inside the old world clips the shifted points.
     *  Implemented in the .cpp -- it reads the document's Data. */
    void clusterFeaturesReprojected(int clusterId);

    /** The dimension-extrema recompute finished (membership edits crossing
     *  cluster 0, and their undo/redo, run it on Data's worker thread).
     *  Refresh the world bounds; repaint only when they actually moved. */
    void dimensionExtremaChanged();

    /** Drop a live or in-flight t-SNE presentation (renumber and the other
     *  relabels connect here): the embedding's cached cluster ids and rows no
     *  longer describe the data.  No-op outside t-SNE. */
    void tsneInvalidate();

    /**
  * Takes into  account the update of the dimension used to present the clusters.
  * @param dimensionX
  * @param dimensionY
  */
    virtual void updatedDimensions(int dimensionX, int dimensionY) override;

    /**Updates the view only for one cluster for which the color has been changed
  * @param clusterId cluster Id for which the color have changed.
  * @param active true if the view is the active one, false otherwise.
  */
    virtual void singleColorUpdate(int clusterId,bool active) override {
        addClusterToUpdate(clusterId);
    }

    /**
  * Draws an additional cluster to those already shown.
  * This method aims to reduce the number of clusters to draw.
  * @param clusterId cluster Id to add to the clusters already drawn
  * @param active true if the view is the active one, false otherwise.
  */
    void addClusterToView(int clusterId,bool active) override {
        tsneDropIfActive();
        addClusterToUpdate(clusterId);
    }

    /**
  * Removes a cluster from those already shown. Which impose to redraw everything
  * @param clusterId cluster Id to remove.
  * @param active true if the view is the active one, false otherwise.
  */
    void removeClusterFromView(int clusterId,bool active) override {tsneDropIfActive(); redraw();}

    /**
  * Adds a newly created cluster to those already shown.
  * This method aims to reduce the number of clusters to draw.
  * @param fromClusters list of clusters from which the spikes of the new cluster are coming.
  * @param clusterId cluster Id to add to the clusters already drawn
  * @param active true if the view is the active one, false otherwise.
  */
    void addNewClusterToView(QList<int>& fromClusters,int clusterId,bool active) override {
        tsneDropIfActive();
        addClusterToUpdate(clusterId);
    }

    /**
  * Adds a newly created cluster to those already shown.
  * This method aims to reduce the number of clusters to draw.
  * @param clusterId cluster Id to add to the clusters already drawn
  * @param active true if the view is the active one, false otherwise.
  */
    void addNewClusterToView(int clusterId,bool active) override {
        tsneDropIfActive();
        addClusterToUpdate(clusterId);
    }

    /**
  * Update the content of the widget due to the removal of spikes in a cluster.
  * This method aims to reduce the number of clusters to draw.
  * @param fromClusters list of clusters from which the spikes have been taken.
  * @param active true if the view is the active one, false otherwise.
  */
    void spikesRemovedFromClusters(QList<int>& fromClusters,bool active) override {tsneDropIfActive(); redraw();}

    /**
  * Update the content of the widget due to the addition of spikes in a cluster.
  * This method aims to reduce the number of clusters to draw.
  * @param clusterId cluster Id to which the spikes have been added
  * @param active true if the view is the active one, false otherwise.
  */
    void spikesAddedToCluster(int clusterId,bool active) override {
        tsneDropIfActive();
        addClusterToUpdate(clusterId);
    }

    /**Method call when no spikes have been found in a polygon of selection
  */
    void emptySelection() override {drawContentsMode = UPDATE;}

    /**Change the current mode, call by a selection of a tool
  * @param selectedMode new mode of drawing (selection or zoom)
  */
    void setMode(BaseFrame::Mode selectedMode) override;

    /**
  * Update the clusters which have been modified by the suppression of spikes
  * (used to create a new cluster or simply move to the cluster of noise or artefact).
  * This method aims to reduce the number of clusters to draw. The view is redraw
  * only if @p isModifiedByDeletion is true.
  * @param modifiedClusters list of clusters from which spikes were taken from.
  * @param active true if the view is the active one, false otherwise.
  * @param isModifiedByDeletion true if the clusters of @p modifiedClusters have been modified
  * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
  */
    void updateClusters(QList<int>& modifiedClusters,bool active,bool isModifiedByDeletion) override {
        tsneDropIfActive();
        if(isModifiedByDeletion) redraw();
    }

    /**
  * Update the clusters which have been modified by the suppression of spikes
  * (used to create a new cluster or simply move to the cluster of noise or artefact).
  * This method is call only during an undo otherwise the updateClusters is call.
  * There are 2 functions in order to reduce the number of clusters to draw whenever possible.
  * @param modifiedClusters list of clusters from which spikes were taken from.
  * @param active true if the view is the active one, false otherwise.
  */
    void undoUpdateClusters(QList<int>& modifiedClusters,bool active) override {tsneDropIfActive(); redraw();}

    /**Updates the time interval in second and in recording unit using @p step given in second.
  * @param step the interval to use in second.
  */
    void setTimeStepInSecond(int step){
        timeStepInSecond = step;
        timeStepInRecordingUnit =  static_cast<long>((static_cast<double>(timeStepInSecond) * 1000000.0) / samplingInterval);
    }

    /**Updates the time interval between time lines. The update is made both in second and
  * in recording unit using @p step given in second. This is an overloaded member function to be called when the user changes the settings.
  * @param step the interval to use in second.
  * @param active true if the view is the active one, false otherwise.
  */
    void setTimeStepInSecond(int step,bool active){
        timeStepInSecond = step;
        timeStepInRecordingUnit =  static_cast<long>((static_cast<double>(timeStepInSecond) * 1000000.0) / samplingInterval);
        if(active)redraw();
    }

    /**Prints the currently-displayed contents to a printer via @p printPainter.
  * @param printPainter painter on a printer.
  * @param width width of the printable area in printer pixels.
  * @param height height of the printable area in printer pixels.
  * @param whiteBackground true if the printed background has to be white, false otherwise.
  */
    void print(QPainter& printPainter,int width,int height, bool whiteBackground) override;

protected:
    /** Repaints the view: blits the doublebuffer to the screen, then
     *  draws the current selection polygon and any active live-preview
     *  overlay (watershed or dipsplit) on top.
     */
    void paintEvent ( QPaintEvent*) override;
    virtual void resizeEvent(QResizeEvent* event) override {
        //Trigger parent event
        ViewWidget::resizeEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    /**Ctrl+wheel zooms toward the cursor (Ctrl+drag pans).  Without Ctrl the
     * event defers to the base ViewWidget/BaseFrame handling.*/
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    virtual  void mouseDoubleClickEvent(QMouseEvent* event) override {
        //Trigger parent event
        ViewWidget::mouseDoubleClickEvent(event);
    }
    /**Treat the events informing that it is time to compute the new data
  * due to the selection polygon.
  */
    virtual void customEvent(QEvent* event) override;

private:
    /**
  * Fit the view bounds to the currently shown clusters in the current
  * (dimensionX, dimensionY) projection.  Scans spike coordinates through
  * Data::Iterator for each shown cluster, computes global min/max with
  * a 5% margin, and rewrites abscissaMin/Max, ordinateMin/Max, and the
  * zoom window — matching the bounds convention from updatedDimensions()
  * but restricted to visible clusters only.
  *
  * When called while shownClusters is empty, the method is a no-op
  * (leaves existing bounds intact — nothing to fit to).
  */
    void autoscaleToVisibleClusters();

    /** Computes the world rectangle for the current (dimensionX, dimensionY)
     *  projection from Data's dimension extrema -- the arithmetic
     *  updatedDimensions() has always used, extracted so the world can be
     *  rebuilt WITHOUT resetting the projection or the zoom. */
    void worldBoundsFromExtrema(long& aMin, long& aMax,
                                long& oMin, long& oMax) const;

    /** Rebuilds the world from the (possibly changed) Data extrema for the
     *  current projection.  Unlike updatedDimensions() it PRESERVES the
     *  user's zoom: a window that differed from the old world is re-applied,
     *  clamped into the new world; an unzoomed window follows the world.
     *  @return true when the world actually changed (callers repaint then). */
    bool recomputeWorldBounds();

    /**
  * When true, autoscaleToVisibleClusters() is called automatically in
  * paintEvent before redrawing.  Toggled by the 'A' key in
  * keyPressEvent.  When false, the view uses whatever bounds were last
  * set manually (via zoom or updatedDimensions).
  */
    bool autoscaleEnabled = false;

    // ── t-SNE alternate presentation (T key) ────────────────────────────────
    // The embedding is computed on a worker thread from a COPY of the selected
    // clusters' feature rows (all dimensions except time), so mutations can
    // proceed while it runs -- any membership/feature change simply drops the
    // presentation (tsneDropIfActive).  Interactions are display-only in this
    // state: the selection/zoom machinery works in feature-world coordinates,
    // which the embedding does not share.
    bool                 tsneMode      = false;   ///< drawing the embedding
    bool                 tsneComputing = false;   ///< worker in flight
    std::atomic<bool>    tsneCancel{false};
    QThread*             tsneThread    = nullptr;
    std::vector<double>  tsneXY;                  ///< N*2 embedding
    QList<int>           tsneRowCluster;          ///< per-point cluster id
    int                  tsneSpikeCount = 0;
    int                  tsneClusterCount = 0;
    double               tsnePerplexity = 30.0;

    /** @p perplexityOverride > 0 pins the perplexity (the arrow-key path);
     *  0 means "pick the default for this N". */
    void startTsne(double perplexityOverride = 0.0);
    void exitTsne(const QString& reason = QString());
    void tsneDropIfActive();
    void onTsneFinished(bool ok, const QString& err,
                        std::vector<double> xy, QList<int> labels,
                        int nSpikes, int nClusters, double perp, qint64 ms);
    void paintTsne(QPainter& painter);

    // ── Ctrl+wheel zoom / Ctrl+drag pan (drives the inherited BaseFrame
    //    ZoomWindow directly, like the rubber-band zoom).  Ctrl distinguishes
    //    navigation from the selection / click-zoom modes. ──
    bool   ctrlPanArmed{false};     // Ctrl+Left seen; awaiting drag threshold
    bool   ctrlPanning{false};      // threshold crossed → actively panning
    QPoint ctrlPanAnchorPx;         // viewport pixel where the Ctrl-drag began
    long   ctrlPanPressWorldX{0};   // world point under the cursor at press
    long   ctrlPanPressWorldY{0};   // (kept fixed so the grab point tracks the cursor)
    static constexpr int   ctrlPanDragThreshold{3};   // px before a press becomes a pan
    static constexpr float ctrlWheelZoomStep{1.25f};  // zoom factor per wheel notch

Q_SIGNALS:
    void moveToTime(long startTime);

private:

    //Color for the different selection modes
    static const QColor NEW_CLUSTER_COLOR;
    static const QColor DELETE_NOISE_COLOR;
    static const QColor DELETE_ARTEFACT_COLOR;

    /**
  * Draws the spikes of the clusters in the list @p clustersList on the given painter
  * @param painter painter on which to draw the spikes
  * @param clustersList list of clusters to draw
  */
    void drawClusters(QPainter& painter,const QList<int>& clustersList,bool drawCircles = false);

    /**
  * Returns the color associated with one of the selection mode. This color will
  * be use to draw the polygon of selection.
  * @return color to be used
  */
    QColor selectPolygonColor(Mode mode){
        switch(mode){
        case DELETE_NOISE:
            return DELETE_NOISE_COLOR;
        case DELETE_ARTEFACT:
            return DELETE_ARTEFACT_COLOR;
        case NEW_CLUSTER:
            return NEW_CLUSTER_COLOR;
        case NEW_CLUSTERS:
            return NEW_CLUSTER_COLOR;
        case ZOOM:
            break; //nothing to do
        case SELECT_TIME:
            break; //nothing to do
        }
        //never reach
        return QColor(0,0,0);
    }


    /** Erases the last segment of the selection polygon by overdrawing
     *  it in the doublebuffer.  Called from mousePressEvent when the
     *  user backs out of the most recently committed polygon vertex.
     */
    void eraseTheLastDrawnLine();

    /** Erases the last segment drawn by mouseMoveEvent (the rubber-band
     *  preview line that follows the cursor before a vertex is
     *  committed).  Same overdraw mechanism as eraseTheLastDrawnLine.
     */
    void eraseTheLastMovingLine();

    /**
  * Adds a cluster to the list of clusters to update
  * @param clusterId id of the cluster to update.
  */
    void addClusterToUpdate(int clusterId);


    /**Draws the axis for the current dimensions
  * @param painter painter on which to draw the axes
  */
    void drawAxes(QPainter& painter);

    /**Set of instructions need it in order to enable a correct redraw when the drawContents
  is called*/
    void redraw(){
        drawContentsMode = REDRAW;

        //Clear the update list
        clusterUpdateList.clear();

        //reset the information on the polygon to enable a mousetrack in mousemovEvent
        polygonClosed = false;
    }

    /**Draws information on the time axis.
  * @param painter painter on which to draw the information
  */
    void drawTimeInformation(QPainter& painter);

    //Members

    /**
  * Points defining the selection polygon.
  */
    QPolygon selectionPolygon;

    /**
  * Number of points defining the selection polygon.
  */
    uint nbSelectionPoints;

    /**Boolean used to know if there is a closing line for the polygon and so if it is necessary to remove it*/
    bool polygonClosed;

    /**Minimal abscissa  in window coordinate*/
    long abscissaMin;

    /**Maximal abscissa in window coordinate*/
    long abscissaMax;

    /**Minimal ordinate in window coordinate*/
    long ordinateMin;

    /**Maximal ordinate in window coordinate*/
    long ordinateMax;

    /**The abscissa dimension*/
    int dimensionX;

    /**The ordinate dimension*/
    int dimensionY;

    /**The dimension of the time.*/
    int timeDimension;

    /**Sampling rate (time between two samples) in micro second.*/
    double samplingInterval;

    /**The step, in second, used to draw information mark on the time axis.
  * The default is 60 second.
  */
    int timeStepInSecond;

    /**The step, in recording unit, used to draw information mark on the time axis.*/
    long timeStepInRecordingUnit;

    /**Size of scatter plot points in pixels (default: 2, range 1-10).*/
    int pointSize;
    /**Width of the selection polygon line in pixels (default: 1, range 1-10).*/
    int selectionLineWidth;

    QCursor newClusterCursor;
    QCursor newClustersCursor;
    QCursor deleteNoiseCursor;
    QCursor deleteArtefactCursor;
    /**A cursor to represent the selection of time state.*/
    QCursor selectTimeCursor;


    class ComputeEvent;
    friend class ComputeEvent;

    /**Returns a new ComputeEvent.*/
    ComputeEvent* getComputeEvent(QPolygon polygon){
        return new ComputeEvent(polygon);
    }

    /**
  * Internal class use to inform the Cluster View that it is time to compute the new data
  * corresponding to the polygon of selection. The aim of this event is to allow the view
  * to close the polygon of selection before asking for the computation.
  *@author Lynn Hazan
  */
    class ComputeEvent : public QEvent{
        //Only the method getComputeEvent of ClusterView has access to the private part of ComputeEvent,
        //the constructor of ComputeEvent being private, only this method con create a new ComputeEvent
        friend ComputeEvent* ClusterView::getComputeEvent(QPolygon selectionPolygon);

    public:
        ~ComputeEvent(){}
        QPolygon polygon(){return selectionPolygon;}

    private:
        explicit ComputeEvent(QPolygon polygon):QEvent(QEvent::Type(QEvent::User + 700)),selectionPolygon(polygon){}

        QPolygon selectionPolygon;
    };

private:
    // Watershed preview overlay state (see setWatershedOverlay).
    QImage  wsImage;
    double  wsXMin = 0.0, wsXMax = 0.0;
    double  wsYMin = 0.0, wsYMax = 0.0;
    QString wsHud;

    // DipSplit post-commit HUD state (see setDipsplitPostCommitHud).
    // Just a text string drawn over the doublebuffer in viewport pixels.
    QString         dsHud;

    // Helper called from paintEvent after the doublebuffer blit.  Draws
    // the overlay image stretched into the world rect, then writes the
    // HUD text in viewport pixels.
    void paintWatershedOverlay(QPainter& p, const QRect& worldRect);

    // Helper called from paintEvent after the doublebuffer blit.  Draws
    // the dipsplit post-commit HUD text at top-left in viewport pixels.
    void paintDipsplitPostCommitHud(QPainter& p);

};

#endif
