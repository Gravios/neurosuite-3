/***************************************************************************
                          errormatrixview.h  -  description
                             -------------------
    begin                : Mon Jan 5 2004
    copyright            : (C) 2004 by Lynn Hazan
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

#ifndef ERRORMATRIXVIEW_H
#define ERRORMATRIXVIEW_H

// include files for Qt
#include <vector>
#include <QWidget>
#include <QMap>
#include <QColor>
#include <QList>
#include <QSet>
#include <QSize>

#include <QResizeEvent>

#include <QMouseEvent>
#include <QWheelEvent>



// application specific includes
#include <viewwidget.h>
#include "array.h"
#include "pair.h"

// forward declaration
class KlustersDoc;
class KlustersView;
class ErrorMatrixThread;
class QTimer;

/**
  * View displaying the Error Matrix. Each element in the matrix
  * indicates how likely it is that the two clusters corresponding to the row and column
  * of the element contain spikes from the same neuron. This view is part of the Grouping
  * Assistant View which contains also a ClusterView, a WaveformView and a CorrelationView.
  * A click on one of the elements of the matrix display the corresponding clusters in the others views.
  * This view is not automatically updated when the clusters are changed.
  *@author Lynn Hazan
  * @since klusters 1.1
  */

class ErrorMatrixView : public ViewWidget  {
    Q_OBJECT

public:

    friend class ErrorMatrixThread;

    explicit ErrorMatrixView(KlustersDoc& doc, KlustersView& view, const QColor &backgroundColor, QStatusBar* statusBar, QWidget *parent=nullptr, const char* name=nullptr, int minSize = 50, int maxSize = 4000, int windowTopLeft = -500,
                    int windowBottomRight = 1001, int border = 0);
    ~ErrorMatrixView();

    /**Signals that the widget is about to be deleted.*/
    void willBeKilled() override;

    // ── Read-only accessors used by KlustersApp::slotReorderClustersBySimilarity ──
    // The probability matrix is built by ErrorMatrixThread and stored as
    // a row-major Array<double> indexed 1..nClusters (cluster ID for
    // row/col i is computedClusterList[i-1]).  Higher probability =
    // clusters more likely to belong to the same neuron, so this is a
    // similarity matrix in the reorder algorithm's terminology.

    /// True once at least one matrix computation has completed.
    bool hasComputedData() const { return dataReady; }

    /// True while a display-driving (non-seedOnly) error-matrix compute is in
    /// flight.  Excludes the background incremental cache warmer (a seedOnly
    /// thread), which must never block editing.  Used to gate cluster edits until
    /// the post-edit matrix consolidation has finished.
    bool isComputing() const;
    /// True if the matrix has never been computed OR is currently flagged
    /// stale.  This MUST mirror exactly the predicate drawMatrix() uses to
    /// paint the red "out of date" border:
    ///     !modifiedClusterList.isEmpty() || hasBeenRenumbered || isNotUpToDate
    /// plus !dataReady (never-computed).  Keeping the two in lock-step is
    /// what lets the Shift+S reorder treat "the user sees a red border" and
    /// "the reorder will auto-recompute first" as the same condition — if
    /// this were narrower, a matrix that looks stale (e.g. after an ordinary
    /// group/delete, which sets modifiedClusterList but not isNotUpToDate)
    /// would be reordered on stale data without recomputing.
    bool isOutOfDate() const {
        return !dataReady || isNotUpToDate
               || !modifiedClusterList.isEmpty() || hasBeenRenumbered;
    }
    /// Cluster IDs present in the matrix (including noise/artefact).
    QList<int> matrixClusterList() const { return clusterList; }
    /// Cluster IDs that actually have computed probabilities.
    QList<int> matrixComputedClusterList() const { return computedClusterList; }
    /// Reorder the matrix DISPLAY (rows/cols) by a permutation of matrix indices
    /// [0, nbClusters): order[d] is the 0-based matrix index shown at display
    /// position d.  Clusters are NOT renumbered -- this is view-local and cheap
    /// (a repaint), unlike the Shift+S renumber.  Empty/wrong-sized => identity.
    void setDisplayOrder(const QList<int>& order);
    /// Drop any display permutation (back to matrix/computed order).
    void resetDisplayOrder();
    /// Pointer to the [N x N] probability matrix (1-based; may be null).
    const Array<double>* matrixData() const { return probabilities; }

Q_SIGNALS:
    /// Emitted when the user clicks anywhere in the matrix view.  Used
    /// by KlustersApp to track which matrix view the user most recently
    /// interacted with — Shift+S reorder uses that matrix when both
    /// error and template matrices coexist.
    void viewInteracted();

    /// Emitted from customEvent() each time a freshly computed matrix is
    /// accepted (i.e. dataReady has just gone true and the result was not
    /// superseded by a later updateMatrixContents() call).  Lets external
    /// callers — notably the Shift+S reorder, which auto-recomputes a
    /// stale matrix before reordering — defer work until fresh data is in
    /// hand without polling or blocking.
    void matrixUpdated();

    /// Emitted when the user changes this view's zoom level (wheel, reset).
    /// Used to keep the error and template matrix zooms synchronised.
    void viewChanged(double zoom, double panX, double panY);

public Q_SLOTS:

    /// Set the zoom level from an external source (the synchronised template
    /// matrix view): sets the full zoom + pan state. Does not emit viewChanged, so
    /// the two views can be cross-connected without a loop.
    void setViewState(double zoom, double px, double py);

    /// Channel selection committed in the waveform view (empty = all channels).
    /// Recomputes over just those channels' feature columns.
    void selectedChannelsChanged(const QList<int>& channels);

    /**Enables the caller to know if there is any thread running launch by the Widget.*/
    bool isThreadsRunning() const override;

    /**Synchronously stops every running ErrorMatrixThread.  Overrides the (empty)
     * ViewWidget::stopRunningThreads so KlustersView::stopAllViewThreads() actually
     * quiesces this view's threads before a caller mutates Data (group/merge, undo,
     * realign).  Does NOT set goingToDie, so a fresh matrix can be recomputed after.*/
    void stopRunningThreads() override;

    /**Update the error matrix.*/
    void updateMatrixContents();

private:
    /**The feature dimensions the current channel selection maps to (empty = all).
     * Warns once, via the status bar, about any selected channel that carries no
     * feature columns — on a stderiv session process_pca_stderiv drops the last
     * channel, so selecting it can have no effect here.*/
    std::vector<int> activeFeatureDims();

public Q_SLOTS:

    /**Updates the error matrix drawing by adding a red border
  * if the rearrangement of clusters have modified clusters presented in the matrix.
  * @param groupedClusters list of clusters having been grouped.
  * @param newClusterId the id of the cluster created by the grouping of those specified in @p groupedClusters
  */
    void clustersGrouped(QList<int>& groupedClusters, int newClusterId);

    /**Updates the error matrix drawing by adding a red border
  * if the deletion of clusters have impacted clusters presented in the matrix.
  * @param deletedClusters list of clusters having been deleted
  * @param destinationCluster cluster with which the clusters in @p deletedClusters are merge
  * (cluster 0 or 1 cluster of artefact and cluster of noise respectively).
  */
    void clustersDeleted(QList<int>& deletedClusters,int destinationCluster);

    /**
  * Updates the error matrix drawing by adding a red border if spikes have been
  * removed from clusters presented in the matrix.
  * @param fromClusters list of clusters from which the spikes have been taken.
  * @param destinationClusterId cluster id to which the spikes have been added
  * @param emptiedClusters list clusters numbers which became empty because all their spikes were put in the new one.
  */
    void removeSpikesFromClusters(QList<int>& fromClusters, int destinationClusterId,QList<int>& emptiedClusters);

    /**
  * Updates the error matrix drawing by adding a red border if the clusters presented in the matrix
  * have been modified to create the new cluster.
  * @param fromClusters list of clusters from which the spikes of the new cluster are coming.
  * @param clusterId cluster to add to the clusters already drawn
  * @param emptiedClusters list clusters numbers which became empty because all their spikes were put in the new one.
  */
    void newClusterAdded(QList<int>& fromClusters,int clusterId,QList<int>& emptiedClusters);

    /**
  * Updates the error matrix drawing by adding a red border if the clusters presented in the matrix
  * have been modified to create the new clusters.
  * @param fromToNewClusterIds map where the keys are ids of the clusters which really contained spikes in the region
  * and the values are the ids of the newly created clusters.
  * @param emptiedClusters list clusters numbers which became empty because all their spikes were put in the new one.
  */
    void newClustersAdded(QMap<int,int>& fromToNewClusterIds,QList<int>& emptiedClusters);

    /**
  * Updates the error matrix drawing by adding a red border if the clusters presented in the matrix
  * have been modified to create the new clusters.
  * @param clustersToRecluster list of clusters automatically reclustered.
  */
    void newClustersAdded(QList<int>& clustersToRecluster);

    /**
  * Updates the error matrix drawing by adding a red border due to an renumbering of the cluster ids.
  * @param clusterIdsOldNew map given for each old clusterId the new clusterId.
  */
    void renumber(QMap<int,int>& clusterIdsOldNew);
    /**A nudge/realign reprojected clusterId's features (membership unchanged).
     * Mark it modified so it enters changedIds and its incremental row is
     * refreshed rather than reused stale.*/
    void clusterFeaturesReprojected(int clusterId);

    /**Updates the error matrix drawing due to the reversion of the last renumbering action.
  * @param clusterIdsNewOld map given for each new clusterId the old clusterId.
  */
    void undoRenumbering(QMap<int,int>& clusterIdsNewOld);

    /**Updates the error matrix drawing due to reversion of the last user action.
  * @param addedClusters list of clusters which were added.
  * @param updatedClusters list of clusters which were modified.
  */
    void undoAdditionModification(QList<int>& addedClusters,QList<int>& updatedClusters);

    /**Updates the error matrix drawing due to reversion of the last user action.
  * @param addedClusters list of clusters which were added.
  */
    void undoAddition(QList<int>& addedClusters);

    /**Updates the error matrix drawing due to reversion of the last user action.
  * @param updatedClusters list of clusters which were modified.
  */
    void undoModification(QList<int>& updatedClusters);

    /** Updates the error matrix drawing by adding a red border.
  * @param clusterIdsOldNew map given for each old clusterId the new clusterId.
  */
    void redoRenumbering(QMap<int,int>& clusterIdsOldNew);

    /**Updates the error matrix drawing by adding a red border if the reversion of
  * the last undo action has modified any of the clusters presented in the error matrix.
  * @param addedClusters list of clusters which were added
  * @param modifiedClusters list of clusters which were modified
  * @param isModifiedByDeletion true if the clusters of @p updatedClusters have been modified
  * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
  * @param deletedClusters list of clusters which were deleted.
  */
    void redoAdditionModification(QList<int>& addedClusters,QList<int>& modifiedClusters,bool isModifiedByDeletion,QList<int>& deletedClusters);

    /**Updates the error matrix drawing by adding a red border if the reversion of
  * the last undo action has modified any of the clusters presented in the error matrix.
  * @param addedClusters list of clusters which were added
  * @param deletedClusters list of clusters which were deleted.
  */
    void redoAddition(QList<int>& addedClusters,QList<int>& deletedClusters);

    /**Updates the error matrix drawing by adding a red border if the reversion of
  * the last undo action has modified any of the clusters presented in the error matrix.
  * @param updatedClusters list of clusters which were modified
  * @param isModifiedByDeletion true if the clusters of @p updatedClusters have been modified
  * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
  * @param deletedClusters list of clusters which were deleted.
  */
    void redoModification(QList<int>& updatedClusters,bool isModifiedByDeletion,QList<int>& deletedClusters);

    /**Updates the error matrix drawing by adding a red border if the reversion of
  * the last undo action has modified any of the clusters presented in the error matrix.
  * @param deletedClusters list of clusters which were deleted.
  */
    void redoDeletion(QList<int>& deletedClusters);

    /**Prints the currently display information on a printer via the painter @p printPainter.
  * @param printPainter painter on a printer.
  * @param metrics object providing information about the printer.
  * @param whiteBackground true if the printed background has to be white, false otherwise.
  */
    void print(QPainter& printPainter,int width,int height, bool whiteBackground) override;

protected:
    /**
  * Draws the contents of the frame
  * @param p painter used to draw the contents
  */
    void paintEvent ( QPaintEvent*) override;
    void recomputeCellWidth();
    /**Treat the events sent by the groupAssistantThread instances*/
    void customEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override {
        ViewWidget::resizeEvent(event);
        if (!clusterList.isEmpty()) {
            updateWindow();  // recompute cellWidth for new size
            update();
        }
    }

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    /**Color map use to represent the probabilities of the error matrix.*/
    QMap<int,QColor> colorMap;
    /**List of the clusters present in the error matrix.*/
    QList<int> clusterList;
    /**List of the clusters present in the error matrix which have a probability calculated.*/
    QList<int> computedClusterList;
    /**List of the clusters present in the error matrix which do not have a probability calculated.*/
    QList<int> ignoreClusterIndex;
    /**Error matrix.*/
    Array<double>* probabilities;
    /**Display permutation: display position -> 0-based matrix index.  Empty =
     * identity (matrix/computed order).  Set by setDisplayOrder (Shift+S display-
     * only reorder); reset on every recompute.  Renders reordered rows/cols
     * without touching probabilities/clusterList, so no cluster renumber.*/
    QList<int> displayOrder;
    /**0-based display position -> 0-based matrix index (identity if unset).*/
    int displayToMatrix(int d) const {
        return (displayOrder.size() == clusterList.size() && d >= 0 && d < displayOrder.size())
               ? displayOrder[d] : d;
    }
    /**0-based matrix index -> 0-based display position (identity if unset).*/
    int matrixToDisplay(int m) const {
        return (displayOrder.size() == clusterList.size()) ? displayOrder.indexOf(m) : m;
    }

    // ── Incremental error-matrix cache (opt-in; default off) ────────────────
    /**Cached RAW (pre-normalisation) per-cluster probability columns from the
     * last successful incremental compute.  Reused for clusters whose membership
     * is unchanged.  Owned here; deleted on refresh / invalidation / teardown.*/
    Array<double>* rawProbCache = nullptr;
    /**Cluster id per rawProbCache column, and nbSpikes per column (reuse guard).*/
    QList<int> rawProbCacheIds;
    QList<int> rawProbCacheSizes;
    /**Feature dimensionality the cache was built with, and whether it is valid.*/
    int  rawProbCacheDims = -1;
    bool rawProbCacheValid = false;
    /**Which clustering the cached raw columns were computed against: false =
     * the parent clustering, true = a child's.  KlustersDoc::data() follows the
     * ACTIVE clustering, and the two share their spikes and their .fet, so the
     * cache's own geometry checks (rows, columns, sizes, dimensions) all pass
     * across a scope switch and cannot tell the columns apart.  Without this,
     * a parent cluster whose id and spike count happen to match a cached child
     * cluster silently reuses P(spike | child N) as P(spike | parent N).*/
    bool rawProbCacheChildScope = false;
    /**Set by the forward renumber slot when it has remapped rawProbCacheIds in
     * place through the old->new map, so the compute launch can skip the
     * defensive hasBeenRenumbered invalidate; consumed (reset) each launch.*/
    bool rawCacheRenumberRemapped = false;
    /**Discards the raw cache (e.g. after a renumber or session change).*/
    void invalidateRawProbCache(const char* reason = "");
    /**Builds the set of cluster ids whose membership changed since the cache,
     * from modifiedClusterList and the merge/delete targets in deletedMap.*/
    QSet<int> changedClusterIdsSinceCache() const;

    /**List of the clusters which have been modified since the last computation of the errror matrix.*/
    QList<int> modifiedClusterList;

    /**Map of the clusters deleted.*/
    QMap<int, QList<int> > deletedMap;

    /**Minimal abscissa in window coordinate*/
    long abscissaMin;

    /**Maximal abscissa in window coordinate*/
    long abscissaMax;

    /**Minimal ordinate in window coordinate*/
    long ordinateMin;

    /**Maximal ordinate in window coordinate*/
    long ordinateMax;

    /**The width border*/
    uint widthBorder;

    /**The height border*/
    uint heightBorder;

    /**The width of a cell of the error matrix.*/
    int cellWidth;

    // ── pan / zoom interaction (pixel model — copied from TemplateMatrixView) ──
    // The matrix is rendered at an effective top-left of matrixTopLeft()+(panX,panY)
    // with each cell at effective size cellWidth*zoom.  Pan is in widget pixels;
    // zoom is a multiplier clamped to [effZoomMin(), zoomMax].  Ctrl+Left-drag pans;
    // Ctrl+Wheel zooms around the cursor; double-click resets.  A quick Ctrl-click
    // that never crosses the drag threshold still reaches Ctrl-add pair-selection.
    static constexpr int    CELL_WIDTH   = 50;
    static constexpr int    LABEL_MARGIN = 16;
    static constexpr int    CONTROLS_H   = 0;   // ErrorMatrixView has no control bar
    double  panX{0.0};
    double  panY{0.0};
    double  zoom{1.0};
    bool    panArmed{false};     // Ctrl+press seen; awaiting drag threshold
    bool    panning{false};      // drag threshold crossed → actively panning
    QPoint  panAnchorPx;         // mouse position where Ctrl-drag started
    double  panAnchorX{0.0};     // panX at drag start
    double  panAnchorY{0.0};     // panY at drag start
    static constexpr int    panDragThreshold{3};       // px before press → pan
    static constexpr int    selectionSuppressMove{2};  // px of Ctrl-drag that cancels the cell selection on release
    static constexpr double zoomMin{0.5};    // baseline zoom-out floor; effZoomMin() lowers it to fit large grids
    static constexpr double zoomMax{20.0};
    static constexpr double zoomStep{1.15};  // wheel zoom multiplier per tick

    double  effZoomMin() const;              // adaptive max-zoom-out to fit large grids
    QPoint  matrixTopLeft() const;           // fixed grid origin (label strips reserved)
    inline double  effCellSize() const { return cellWidth * zoom; }
    inline QPointF effMatrixTopLeft() const {
        const QPoint b = matrixTopLeft();
        return QPointF(b.x() + panX, b.y() + panY);
    }
    int  cellAtX(int viewX) const;
    int  cellAtY(int viewY) const;
    void zoomAroundPoint(double newZoom, const QPointF& pivot);
    void resetPanZoom();

    /// While zooming or panning, the selected-pair overlay is suppressed: redrawing
    /// its boxes every interaction frame is costly (an O(nbClusters) clusterList
    /// index lookup per pair, per repaint) and makes the gesture stutter.  A
    /// single-shot timer, restarted on each wheel/pan event, clears the flag and
    /// repaints the overlay once the gesture settles.
    bool    suppressPairBoxes{false};
    QTimer* pairBoxSettleTimer{nullptr};
    static constexpr int pairBoxSettleMs{140};

    /**List of pointers on the threads which have to be suppress when this object is destroy.*/
    QList<ErrorMatrixThread*> threadsToBeKill;

    /**True if the probabilities are available.*/
    bool dataReady;

    /**Number of colors used for the errror matrix.*/
    int nbColors;

    /**Highest probability for which there will be a distinct color.*/
    float cutoffProbability;

    /**True if it is the first computation.*/
    bool init;

    /**True if the clusters have been renumbered, false otherwise.*/
    bool hasBeenRenumbered;

    /**Number of action made between 2 update of the error matrix.*/
    int nbActions;

    /**Number of redo action possible (equals to the number of undo action made
  * after the update of the matrix).*/
    int nbRedo;

    /**True if the matrix is not up to date due to an action made before the update of the matrix.*/
    bool isNotUpToDate;

    /**Number of undo action possible (equals to the number of action made
  * before the update of the matrix).*/
    int nbPreviousUndo;

    /**Number of redo action possible (equals to the number of undo action made
  * before the update of the matrix).*/
    int nbPreviousRedo;

    /**True if the widget is about to be deleted, false otherwise.*/
    /**A recompute is in flight: overlay a small badge rather than leave the
     * user guessing whether the matrix on screen is current.*/
    bool computing = false;
    bool goingToDie;

    /**Monotonically increasing counter, bumped each time updateMatrixContents() is called.
     * Each ErrorMatrixThread stores the generation at the time it was created.
     * customEvent() discards results whose generation != generation, preventing
     * a superseded (pre-renumber) thread from overwriting a more recent result.*/
    int generation;

    /**List of the selected pairs.*/
    QList<Pair> selectedPairs;

    /**Map keepipng track of the renumbering done on the data.*/
    QMap<int,bool> renumbering;

    //Methods

    /**Launches a ErrorMatrixThread to comput the error matrix.*/
    ErrorMatrixThread* computeMatrix();

    /**Launches a background, display-less ErrorMatrixThread that recomputes the raw
     * probability columns (parallel cold-seed) purely to populate rawProbCache after
     * a full/GPU compute left it empty — so the first edit is a fast incremental
     * update instead of a cold seed.  Carries the current generation, so an edit that
     * supersedes it discards its result and cold-seeds itself.  No-op unless the
     * incremental path is enabled.*/
    void launchCacheWarmer();

    /**Updates the dimensions of the window.*/
    void updateWindow();

    /**
  * Draws the error matrix on the given painter.
  * @param painter painter on which to draw the error matrix
  */
    void drawMatrix(QPainter& painter);

    /**Draws the clusters identifiers.
  * @param painter painter on which to draw the information
  */
    void drawClusterIds(QPainter& painter);

    /**Initialize the internal colorMap use to represents the probabilities of the error matrix.*/
    void initializeColorMap();
};

#endif
