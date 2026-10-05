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
#include <map>
#include <set>

#include "matrixtemplatecols.h"   // MatrixTemplateCol — marked-node template columns
#include <QImage>


#include <QResizeEvent>
#include <QMouseEvent>
#include <QEvent>

//include files for the application
#include "zoomwindow.h"
#include "viewwidget.h"
#include "types.h"

#include "neurosuite/core/projectionscope.hpp"   // temporally-restricted projection scope
#include "templatelineagestore.h"                 // manual-lineage overlay model (plan §11)


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

    /** Toggles the OBLIQUE (non-orthogonal dual-basis) alternate presentation of
     *  the selected clusters -- a direct linear projection onto the per-cluster
     *  template axes, for characterising near-collinear pairs (e.g. coupled
     *  interneurons) with explicit per-cell coordinates.  Shares the embedding
     *  display + lasso with the t-SNE presentation but is computed synchronously
     *  (no worker, no perplexity).  Public for the same reason as the others:
     *  the Shift+O key is dispatched by the application filter. */
    void toggleObliquePresentation();

    /** Pins @p ids as the oblique basis (the template axes): a later
     *  toggleObliquePresentation() then projects the current SELECTION onto these
     *  fixed axes instead of onto the selection itself, so a third cluster can be
     *  examined against a chosen pair.  @p ids must be >= 2 existing parent
     *  clusters that own spikes (artefact/noise 0,1 rejected); an EMPTY list
     *  clears the pin and restores the default (basis == selection).  The pin
     *  persists across view toggles until re-set, cleared, or invalidated by a
     *  later edit.  Returns false with *message set on a bad id, true with a
     *  confirming *message otherwise.  Public for the same reason as the toggle:
     *  it is driven from the Actions menu (Set Oblique Basis…). */
    bool setObliqueBasis(const QList<int>& ids, QString* message);
    /** The currently pinned oblique basis (empty when none), so the dialog can
     *  pre-fill it. */
    QList<int> obliqueBasisClusters() const { return obliqueBasis; }

    /** Recompute the temporally-restricted projection scope from the pinned
     *  oblique basis (each basis cluster -> its template class via .tcl
     *  provenance -> the union of that class's .wti drift windows), converted to
     *  recording-unit time.  Called when the basis changes; also safe to call to
     *  refresh after a preferences change.  No-op bookkeeping when nothing pins. */
    void refreshProjectionScope();

    /** Highlight template class @p col's .eap members in the feature views (reads
     *  the stage .eap for that column); @p col < 0 clears the highlight.  Pushed in
     *  by KlustersApp on behalf of the template-library view's class selection. */
    void setHighlightClass(int col);

    /** A closed create-mode embedding lasso defers its cut and shows the residual
     *  preview in the waveform view; these commit (Enter) or discard (Esc) it.
     *  Public because the confirm keys are dispatched by the application filter. */
    bool hasPendingLasso() const { return pendingLasso_; }
    /** The feature rows captured by the pending lasso (1-based .spk record ids),
     *  as a plain int list — consumed by the decollide-against-basis commit. */
    QList<int> pendingRows() const {
        QList<int> r; r.reserve(pendingRows_.size());
        for (dataType x : pendingRows_) r.append(static_cast<int>(x));
        return r;
    }
    void confirmPendingLasso();
    void cancelPendingLasso();

    /** Toggles autoscale-to-visible-clusters.  Public for the same reason:
     *  the A key is dispatched by the application filter. */
    void toggleAutoscale();

    /** Toggle the manual-lineage overlay (Shift+E, dispatched by the app key
     *  filter like Shift+O).  When on, the open group+stage's `.wtl` forest + the
     *  session drift partition are drawn over the feature scatter: one node per
     *  median at its spikes' centroid in the CURRENT projection, child +
     *  drift-trajectory edges, and — when X is the time dimension — the region
     *  boundaries as vertical lines.  Read-only in this cut (plan §11.1); the
     *  Shift+E edit mode and direct manipulation are §11.3. */
    void toggleLineageOverlay();
    bool lineageOverlayActive() const { return lineageOverlay_; }
    /** The template class the lineage overlay edits (auto-seed + new regions).  Set
     *  by the template palette when a class is selected; defaults to 0.  When the
     *  overlay is engaged, switching class tiles that class and repaints. */
    void setLineageActiveClass(int classId);
    int  lineageActiveClass() const { return lineageActiveClass_; }
    /** Review-mode single-node overlay (§11.5): clicking a curation-matrix template
     *  strip cell asks the waveform view to show ONLY that one node's mean±std band
     *  (instead of the whole primary class's bands).  `node` is a lineage node id;
     *  pass -1 to return to the full primary-class band.  The override is cleared
     *  whenever the primary changes, edit mode is toggled, or the marks change. */
    void overlaySingleNode(int node);
    /** Drop a deleted template class's nodes from the overlay (the palette tombstoned
     *  it in .eap/.tcl; the .wtl forest is separate), persist the pruned source, and
     *  repaint.  Resets the active class if it was the deleted one. */
    void removeLineageClass(int classId);
    /** Template-preview scale: best-fit (false) vs absolute data gain (true).  Sets
     *  the mode future pushes use and updates any live WaveformView preview. */
    void setTemplateScaleAbsolute(bool absolute);
    bool templateScaleAbsolute() const { return lineageScaleAbsolute_; }

    /** True while a selection polygon is part-drawn, in either view.  The
     *  application filter asks before deciding what Escape means. */
    bool hasOpenSelectionPolygon() const {return !selectionPolygon.isEmpty();}

    /** Discards a part-drawn selection polygon and repaints.  The armed mode
     *  is left alone: Escape cancels the shape, not the intent to cut. */
    void cancelSelectionPolygon();

    /** True while the t-SNE presentation is showing or being computed.  The
     *  application filter gates the arrow keys on this so palette navigation
     *  is untouched everywhere else. */
    bool isTsneActive() const {return tsneMode || tsneComputing;}

    /** True only while the embedding is actually ON SCREEN.  Distinct from
     *  isTsneActive(), which also covers a run in flight over the scatter: an
     *  operation that needs the feature projection visible must ask this. */
    bool isTsneShowing() const {return tsneMode;}

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

    /** The embedding's points, for an operation that wants to work on what the
     *  curator is actually looking at.  @p spikeRows carries each point's
     *  0-based .spk index so a result computed here can name spikes without
     *  going back through feature space.  False when no embedding is showing.*/
    bool tsneEmbeddingPoints(QVector<double>& xs, QVector<double>& ys,
                             QVector<int>& spikeRows) const;

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

    /** A renumber renamed the clusters.  It moves no spike, so the embedding
     *  stays valid: re-read the ids and recolour instead of discarding it.
     *  (Dropping here also left the cached labels stale for the next lasso.)
     *  No-op outside t-SNE. */
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
    // Overridden for the lineage overlay (§11.3): a left double-click on a region
    // boundary starts a double-left-drag to move it; everything else defers to the
    // base.  Non-overlay behaviour is unchanged.
    void mouseDoubleClickEvent(QMouseEvent* event) override;
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
    QVector<int>         tsneRowSpike;            ///< per-point 0-based .spk index
    // Embedding bounding box captured once when the run lands, so painting and
    // lasso hit-testing share ONE mapping.  Recomputing it per paint would be
    // equivalent today and would silently diverge the day either side changes.
    double               tsneMinX = 0, tsneMaxX = 0, tsneMinY = 0, tsneMaxY = 0;
    bool                 tsneApplyingLasso = false; ///< suppress our own drop
    /// Which layer the embedding names: false = parent clusters, true = child
    /// atoms.  Recorded at gather time because the two id spaces are disjoint
    /// in meaning -- relabelling or cutting with the other layer's ids would
    /// silently address unrelated spikes.
    bool                 tsneChildLayer = false;
    /// Bumped on every start.  A worker carries the id it was started with and
    /// its result is dropped if it no longer matches, which is what lets
    /// exitTsne() stop waiting for the thread on the GUI thread: a late result
    /// from an abandoned run identifies itself and is ignored.
    int                  tsneRunId = 0;
    /// What the worker last reported, painted over the scatter while it runs.
    QString              tsneProgressText;
    int                  tsneSpikeCount = 0;
    int                  tsneClusterCount = 0;
    double               tsnePerplexity = 30.0;
    // Oblique presentation reuses every embedding buffer above (tsneXY,
    // tsneRowCluster, tsneRowSpike, the bbox) and its display + lasso; these two
    // only distinguish the KIND so the label reads "oblique", the perplexity
    // arrows stay inert, and exitTsne() clears it.  Invariant: obliqueMode implies
    // tsneMode (an embedding is on screen) and never tsneComputing (synchronous).
    bool                 obliqueMode = false;
    double               obliqueCond = 0.0;   ///< Gram condition number of the basis
    // Pinned oblique basis (Set Oblique Basis…): the cluster ids whose templates
    // are the fixed axes.  EMPTY means the default -- the basis is the selection.
    // When non-empty, startOblique projects the selection onto these axes and
    // gathers the basis clusters too (the engine builds each basis template from
    // its gathered rows).  Unlike obliqueMode/obliqueCond this is NOT cleared by
    // exitTsne(): the pin is a persistent choice so successive thirds can be
    // examined against the same pair; startOblique re-validates it each time.
    QList<int>           obliqueBasis;

    // Temporally-restricted projection scope (claude/eap-template-class-design §7),
    // the union of the pinned basis classes' .wti drift windows, stored in RECORDING
    // units (the feature table's time column), recomputed by refreshProjectionScope().
    // The mode/hidden flags are read live from Configuration at draw time, so a prefs
    // toggle takes effect on the next repaint without a signal; these intervals change
    // only when the basis/stage does.  `spikeTimeInScope` tests a spike's time column.
    std::vector<neurosuite::projectionscope::Interval> projScopeRU;
    bool projScopeActive() const;                 ///< restricted mode AND a non-empty scope
    bool spikeTimeInScope(double tRecordingUnits) const;

    // EAP collision overlay (claude/eap-template-class-design §8): a per-spike flag
    // (indexed by 0-based .spk/.eap row) set when that spike's .eap row carries two
    // or more template classes.  Loaded once for the open stage; the feature views
    // ring these spikes when the Configuration overlay pref is on.  Empty when no
    // .eap exists for the stage.
    std::vector<char> eapCollision;
    bool resolveSessionPaths(std::string& base, int& group, std::string& tag) const;
    void loadEapCollisions();                     ///< read the stage .eap, fill eapCollision
    bool spikeIsCollision(long spk0) const {      ///< 0-based spike row -> is a collision
        return spk0 >= 0 && spk0 < static_cast<long>(eapCollision.size()) && eapCollision[spk0];
    }

    // ── manual-lineage overlay (template-curation plan §11.1, read-only) ────────
    // Drawn over the feature scatter when `lineageOverlay_` is on: the open
    // group+stage `.wtl` forest + session partition, loaded like eapCollision.
    // Each node's WORLD position is the centroid of its spikes in the current
    // (dimensionX, dimensionY) projection (an empty placeholder is placed at its
    // region's mid-time on X); cached and recomputed when the projection changes,
    // then mapped with worldToViewport every repaint.
    struct LineageNodeDraw { int node; int classId; int parent; bool drift; bool empty; QPoint world; double a; double b; };
    bool                         lineageOverlay_ = false;
    int                          lineageActiveClass_ = -1;  ///< the PRIMARY template class (palette ★); -1 = none.
                                                            ///< Drives the waveform band's visibility and gates edit mode.
    std::set<int>                markedNodes_;              ///< node ids marked (edit-mode context menu) for the matrices;
                                                            ///< cleared whenever the primary changes / is unset.
    int                          lineageSingleNode_ = -1;   ///< review-mode single-node overlay (§11.5): when >=0 the
                                                            ///< waveform band shows only this node, not the whole class;
                                                            ///< set by a matrix strip-cell click, cleared on primary
                                                            ///< change / edit toggle / marks change.
    bool                         lineageLoaded_ = false;    ///< the .wtl forest is loaded for the open stage.  Loaded
                                                            ///< once (lazily) and kept, so switching primary never
                                                            ///< reloads and discards uncommitted in-memory edits.
    TemplateLineageStore         lineageStore_;
    std::vector<LineageNodeDraw> lineageDraw_;
    void loadLineageOverlay();                    ///< (re)load the store for the open stage
    void recomputeLineagePositions();             ///< node centroids in the current projection
    void paintLineageOverlay(QPainter& p);        ///< draw nodes / edges / boundaries on top
    QColor lineageClassColor(int classId) const;  ///< stable per-class colour
    // On entry the overlay forces the projection to time (X) × the energy-ladder
    // feature (Y) so drift reads left→right and the amplitude ladder reads
    // vertically; the prior projection is saved and restored on exit (§11.2).
    int  bestEnergyLadderDim() const;             ///< non-time feature dim with the widest spread
    int  savedDimX_ = -1, savedDimY_ = -1;        ///< projection saved on overlay entry
    bool dimsForced_ = false;                     ///< true while the overlay forced time×amplitude
    // Hit-testing (groundwork for the §11.3 double-left-drag + context menus): map
    // a viewport point to the nearest node / interior partition boundary.
    int  lineageNodeAt(const QPoint& vp, int pxTol = 8);       ///< node under vp (ribbon bar / leaf disc), or -1
    int  lineageBoundaryAt(const QPoint& vp, int pxTol = 5);   ///< nearest interior boundary index, or -1
    // Screen position per node, computed exactly as paintLineageOverlay draws them
    // (drift roots on the top ribbon, leaves stacked below their root; world point
    // when X is not time).  Shared by the painter and the hit-test so clicks land
    // where the nodes are actually drawn.
    std::map<int, QPoint> lineageScreenPositions();
    // Interaction (§11.3): double-left-drag moves a boundary; right-click opens a
    // context menu.  Both gated by lineageOverlay_ and injected as guarded early-
    // returns, so the mode-driven lasso / zoom / pan is untouched when off.
    int    lineageDragBoundary_  = -1;        ///< interior boundary being double-drag-moved, or -1
    double lineageDragBoundaryT_ = 0.0;       ///< live preview time (s) while dragging
    // Left-click node marking + rectangular node lasso (§11.5).  In the overlay with
    // the default ZOOM tool a plain Left press no longer zooms: a click toggles the
    // node under the cursor, a drag rubber-bands a rectangle that marks every node
    // inside it.  (Zoom stays on Ctrl+wheel and double-click-reset.)
    bool   lineageLassoActive_  = false;      ///< a plain-Left press/drag is in progress in the overlay
    bool   lineageLassoDragged_ = false;      ///< moved past the click→drag threshold (it is a rectangle)
    QPoint lineageLassoAnchor_;               ///< press point (viewport px)
    QRect  lineageLassoRect_;                 ///< current lasso rectangle (viewport px, normalized)
    void   applyModeCursor();                 ///< set the resting cursor: a pointer in the overlay, else the tool's
    void   toggleLineageNodeMark(int node);   ///< mark/unmark one node (shared by click + the context menu path)
    void   markNodesInLineageRect(const QRect& rect);  ///< mark every populated node whose marker is inside rect
    void   showLineageContextMenu(const QPoint& vp);
    void   commitLineageOverlay();            ///< render the model (.mti/.mtf) from the forest
    // Push the ACTIVE class's populated nodes/leaves to the WaveformView(s) as a
    // translucent mean±std band, straight from the stored node summaries (no commit
    // / render needed).  Called on class-select, after an edit, on overlay-on and
    // after a commit, so the band tracks what the curator is building live.
    void   pushActiveLineageBands();
    void   clearTemplatePreviewOnViews();
    bool   lineageScaleAbsolute_ = true;      ///< WaveformView template scale: absolute (default, matches the
                                              ///< clusters' data gain) vs best-fit (normalised per template)
    void   lineageEdited();                   ///< auto-save .wtl + recompute + repaint + hint
    void   lineageUndo();                     ///< step the template edit history back (store undo + persist + repaint)
    void   lineageRedo();                     ///< step the template edit history forward
    double timeAtViewport(const QPoint& vp);  ///< viewport X -> seconds (valid when X is time)
    std::vector<int64_t> shownClusterSpikes() const;   ///< displayed clusters' 0-based .spk ids

    // Highlighted template class: its .eap members are ringed in the feature views
    // (claude/eap-template-class-design §8).  The class is chosen in the template-
    // library view and pushed in via setHighlightClass (KlustersApp forwards it);
    // eapHighlightMember is the per-spike (0-based row) membership of that column.
    int highlightClassCol = -1;
    std::vector<char> eapHighlightMember;
    bool spikeIsHighlightMember(long spk0) const {
        return spk0 >= 0 && spk0 < static_cast<long>(eapHighlightMember.size()) && eapHighlightMember[spk0];
    }

    // A create-mode embedding lasso, closed and awaiting confirmation: its cut is
    // deferred while the residual preview shows in the waveform view (Enter
    // applies, Esc cancels).  pendingMode_ holds the BaseFrame::Mode (as int).
    bool                 pendingLasso_ = false;
    QSet<dataType>       pendingRows_;
    QList<int>           pendingSources_;
    int                  pendingMode_  = -1;
    int                  pendingNSel_  = 0;

    /** @p perplexityOverride > 0 pins the perplexity (the arrow-key path);
     *  0 means "pick the default for this N". */
    void startTsne(double perplexityOverride = 0.0);
    /** Computes and shows the oblique projection of the selected clusters
     *  synchronously (no worker).  Refuses with a status message on < 2 clusters,
     *  < 2 feature dims, or a degenerate basis. */
    void startOblique();
    void exitTsne(const QString& reason = QString());
    void tsneDropIfActive();
    void onTsneFinished(int runId, bool ok, const QString& err,
                        std::vector<double> xy, QList<int> labels,
                        QVector<int> spikeRows,
                        int nSpikes, int nClusters, double perp, qint64 ms);

    /** Draws the "computing" banner over the scatter.  A status-bar line is
     *  easy to miss, and missing it makes a working key look dead -- which is
     *  exactly how this feature has been experienced on a large selection. */
    void paintTsneProgress(QPainter& painter);
    void paintTsne(QPainter& painter);

    /** Viewport position of embedded point @p i under the captured bounding
     *  box.  The single mapping used by both paintTsne and the lasso. */
    QPoint tsneViewportPos(int i) const;

    /** Same mapping for an arbitrary embedding coordinate, so an overlay
     *  computed in embedding space lands on the points it describes. */
    QPoint tsneViewportPosFor(double x, double y) const;

    /** Draws the watershed preview over the embedding.  Separate from the
     *  scatter's version because that one works in world coordinates with a
     *  negated Y, neither of which the embedding has. */
    void paintWatershedOverlayEmbedded(QPainter& p);

    /** The coordinate space selection vertices are recorded in: feature-world
     *  for the scatter, viewport pixels for the embedding (whose axes are not
     *  features).  The ONLY thing the two lassos do differently while the
     *  polygon is being drawn -- everything else is the same code. */
    QPoint selectionPoint(const QPoint& viewportPx) {
        return tsneMode ? viewportPx
                        : viewportToWorld(viewportPx.x(), viewportPx.y());
    }

    /** Closes the polygon and triggers the mode's action, in whichever view is
     *  showing.  Mirrors the scatter: the moving line is dropped, the polygon
     *  is marked closed and painted, and the work is queued so the closed
     *  shape is on screen before the document is asked to compute. */
    void closeSelectionPolygon();

    /** Clears the selection polygon and its tracking state. */
    void resetSelectionPolygon();

    /** Closes the lasso: hit-tests every embedded point, groups the hits by
     *  their CURRENT cluster and applies the active selection mode through
     *  the document's explicit-spike-list primitive. */
    void applyTsneLasso();
    /** Apply a built embedding-lasso selection through the scatter's builders —
     *  shared by the immediate delete path and the confirmed create path.
     *  @p lassoMode is a BaseFrame::Mode value. */
    void applyLassoSelection(const QSet<dataType>& rows, const QList<int>& sources,
                             int lassoMode, int nSelected);
    /** Compute the pending lasso's residual (against the pinned oblique basis, or
     *  the lassoed spikes' own mean when none) and push it to the sibling
     *  waveform view(s) as a preview. */
    void showLassoResidualPreview();
    /** Drop any pending lasso and clear the waveform preview. */
    void clearPendingLasso();

    /** Re-reads the embedded spikes' cluster ids from the document after an
     *  edit.  The positions are still valid -- features did not change, only
     *  membership -- so the embedding is recoloured in place instead of being
     *  thrown away, which is what makes iterative curation possible here. */
    void tsneRelabelFromDoc();

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
    /** The set of marked lineage nodes changed (mark/unmark, or a primary/edit that
     *  cleared them).  KlustersApp refreshes the curation matrices' template columns. */
    void lineageMarksChanged();

public:
    /** Marked nodes of the PRIMARY class as curation-matrix template columns
     *  (classId, window [a,b] seconds, channel-major mean waveform) — the data the
     *  augmented matrices need for their cluster×template cells.  Empty when there
     *  is no primary or no marks. */
    std::vector<MatrixTemplateCol> markedTemplates() const;

private:
    /** Rebuild the matrix template columns from the current marks and push them to
     *  this display's curation matrices (via KlustersView).  Wired to
     *  lineageMarksChanged in the ctor. */
    void pushMarkedTemplatesToMatrices();

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
