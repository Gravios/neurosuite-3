#ifndef RESIDUALMATRIXVIEW_H
#define RESIDUALMATRIXVIEW_H

#include <QWidget>
#include <QMap>
#include <QColor>
#include <QList>
#include <QPixmap>
#include <QStatusBar>
#include <QLabel>

#include "array.h"
#include "klustersjobpool.h"    // KlustersJobToken (shared with the jobs)
#include "matrixtemplatestrip.h" // MatrixTemplateStrip — shared marked-node template region (§11.5)

#include <memory>

class KlustersDoc;
class KlustersView;
class ResidualMatrixThread;

/**
 * Read-only view of the asymmetric mean-waveform residual matrix.
 *
 * Cell (row A, col B) = variance of cluster A's spikes taken about cluster B's
 * mean waveform = mean_p ( var_A[p] + (mean_A[p] − mean_B[p])^2 ).  Lower =
 * B's template explains A's spikes well (merge candidate); the diagonal is
 * A's own within-cluster variance (the noise floor each row is measured
 * against).  The matrix is asymmetric — upper-right is A-vs-B, lower-left is
 * B-vs-A — because each row carries its own reference variance.
 *
 * Colour: warm (red) = small residual (similar) → cool (blue) = large residual
 * (distinct); the diagonal is drawn black.  Hover shows the raw value and the
 * cluster pair in the status line.  Unlike the template matrix this view does
 * not move spikes — it is a diagnostic display, plus the source matrix for the
 * spike-count-gated reorder (matrixData()/matrixClusterList()).
 *
 * Pan: Ctrl + Left-drag.  Zoom: Ctrl + Wheel, or +/- ; 0 resets.
 */
class ResidualMatrixView : public QWidget {
    Q_OBJECT

public:
    friend class ResidualMatrixThread;

    explicit ResidualMatrixView(KlustersDoc& doc, KlustersView& view,
                                const QColor& backgroundColor,
                                QStatusBar* statusBar,
                                QWidget* parent = nullptr);
    ~ResidualMatrixView();

    void willBeKilled();
    bool isThreadsRunning() const;

    /** Synchronously stop in-flight ResidualMatrixThread instances and wait
     *  for run() to return, so callers may write .spk.pending afterward with
     *  no torn reads.  Same contract as TemplateMatrixView::stopRunningThreadsSync;
     *  invoked from KlustersView::stopAllViewThreads via findChildren. */
    void stopRunningThreadsSync();
    /**Non-blocking twin (epoch-snapshot step 6b) — see TemplateMatrixView.*/
    void supersedeRunningThreads();

    void updateMatrixContents();

    // Stale-marker slots wired from KlustersDoc signals (mirror TemplateMatrixView).
    void clustersGrouped(QList<int>& groupedClusters, int newClusterId);
    void clustersDeleted(QList<int>& deletedClusters, int destinationCluster);
    void removeSpikesFromClusters(QList<int>& fromClusters, int destinationClusterId,
                                  QList<int>& emptiedClusters);
    void newClusterAdded(QList<int>& fromClusters, int clusterId,
                         QList<int>& emptiedClusters);
    void newClustersAdded(QMap<int,int>& fromToNewClusterIds, QList<int>& emptiedClusters);
    void newClustersAdded(QList<int>& clustersToRecluster);
    void renumber(QMap<int,int>& clusterIdsOldNew);
    /** Mark the matrix out of date without naming the edit (undo/redo family,
     *  clusterFeaturesReprojected). */
    void markStale();

    // ── Read-only accessors (used by KlustersApp residual-gated reorder) ──
    // scores is a row-major Array<double> indexed 1..nClusters; cluster id for
    // row/col i is clusterList[i-1].  LOWER value = closer (a DISTANCE, not a
    // similarity), and the matrix is asymmetric.
    bool hasComputedData() const { return dataReady; }
    bool isOutOfDate()     const { return !dataReady || isStale; }
    QList<int> matrixClusterList() const { return clusterList; }
    const Array<double>* matrixData() const { return scores; }

    /**Marked-node template strip (§11.5): the curator's marked lineage nodes
     * pushed from ClusterView via KlustersView::setMatrixTemplateColumns.  Each
     * cell is the SAME bounded separability index M = gap/(noise+gap) the matrix
     * body uses, so the strip's colours read on the same scale — ASYMMETRIC like
     * the body: a cluster-row cell normalises by the cluster's own noise floor, a
     * template-row cell by the template node's noise floor (std²).  Greyed where
     * the cluster has no spikes in the node's time window.  Computed from the
     * per-cluster means the thread already built (carried in its event).*/
    void setTemplateColumns(const std::vector<MatrixTemplateCol>& cols);

Q_SIGNALS:
    void viewInteracted();
    void matrixUpdated();

    /// Emitted when the user clicks one of the extra marked-node template
    /// rows/columns (§11.5): `clusterId` is the cluster that cell sits on (-1 for
    /// the template×template corner) and `node` is the lineage node it compares
    /// against.  KlustersView selects the cluster and overlays just that node.
    void templateCellActivated(int clusterId, int node);

    /// Emitted when the user changes this view's zoom or pan (wheel, drag,
    /// reset).  Keeps every matrix view's zoom/pan synchronised.
    void viewChanged(double zoom, double panX, double panY);

public Q_SLOTS:
    /**Channel selection committed in the waveform view (empty = all channels).
     * Swaps in a cached matrix when one matches, otherwise recomputes.*/
    void selectedChannelsChanged(const QList<int>& channels);

    /// Set the full zoom + pan state from another (cross-connected) matrix
    /// view.  Does not emit viewChanged, so the views can be cross-connected
    /// without a feedback loop.
    void setViewState(double zoom, double px, double py);

protected:
    void paintEvent(QPaintEvent*) override;
    void customEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    KlustersDoc&  doc;
    KlustersView& view;
    QStatusBar*   statusBar;

    // ── pan + zoom (same transform as TemplateMatrixView) ────────────────
    double  panX{0.0};
    double  panY{0.0};
    double  zoom{1.0};

    /** Zoom and pan, remembered PER SCOPE.
     *
     *  The parent matrix and the child-scoped matrix are different matrices: one
     *  is ~1984 clusters, the other the handful of children under the curated
     *  parent.  A zoom that frames a region of the first is meaningless in the
     *  second, so carrying one state across the V toggle threw away wherever the
     *  user had navigated to and replaced it with a position from a matrix of a
     *  different size.
     *
     *  Two states, swapped when the scope changes: leaving the child scope stashes
     *  its view and restores the parent's, and returning restores the child's.
     *  Each is remembered until the view is destroyed, so toggling back and forth
     *  returns to exactly where you were in each.
     */
    struct ViewState { double panX{0.0}, panY{0.0}, zoom{1.0}; bool valid{false}; };
    ViewState parentScopeView;
    ViewState childScopeView;
    bool      lastScopeActive{false};
    void      swapViewStateForScope(bool scopeActive);
    bool    panning{false};
    QPoint  panAnchorPx;
    double  panAnchorX{0.0};
    double  panAnchorY{0.0};
    static constexpr double zoomMin{0.5};
    static constexpr double zoomMax{20.0};
    static constexpr double zoomStep{1.15};
    static constexpr int    panDragThreshold{3};

    inline double effCellSize() const { return cellWidth * zoom; }
    inline QPointF effMatrixTopLeft() const {
        const QPoint b = matrixTopLeft();
        return QPointF(b.x() + panX, b.y() + panY);
    }
    void  zoomAroundPoint(double newZoom, const QPointF& pivot);
    void  resetPanZoom();

    // ── matrix data ──────────────────────────────────────────────────────
    /**All-channel result and the result for cachedSelection.  Both are owned;
     * `scores` is a NON-owning pointer at whichever is displayed.  Keeping both
     * makes flicking the channel selection on and off an instant swap instead of
     * a recompute.*/
    Array<double>* scoresAll = nullptr;
    Array<double>* scoresSel = nullptr;
    QList<int>     cachedSelection;
    /**Whether each slot's matrix is CURRENT.  Kept separate from the pointers
     * because an out-of-date matrix is still worth displaying while its
     * replacement computes — it stays allocated and on screen, but these flags
     * stop it being swapped in as though it were current.*/
    bool           haveAllCache = false;
    bool           haveSelCache = false;
    /**A recompute is in flight: the view keeps painting whatever it has and
     * overlays a small badge instead of blanking the frame.*/
    bool           computing = false;

    /**Drop both cached results (the spikes themselves changed).*/
    void invalidateCaches();
    /**Start a compute for the document's current selection WITHOUT touching the
     * caches — a selection change must keep the other slot.*/
    void launchCompute();

    Array<double>* scores;        // [N x N], 1-based (NON-owning alias)
    QList<int>     clusterList;
    bool           dataReady;
    bool           goingToDie;
    bool           isStale;
    double         displayMax;  // cached off-diagonal max for colour scaling

    /**Cancellation/completion state shared with the jobs this view enqueues
    * on the worker pool.  Replaces the threadsToBeKill ownership list and
    * the generation counter: the token's own generation is the request
    * generation now.*/
    std::shared_ptr<KlustersJobToken> jobToken;

    // ── geometry / colour / drawing ──────────────────────────────────────
    int cellWidth;
    int widthBorder, heightBorder;
    static constexpr int NB_COLORS = 100;
    QMap<int, QColor> colorMap;
    void initializeColorMap();
    QColor textColor;
    QPixmap doublebuffer;
    QRect   matrixViewport;

    QLabel* infoLabel;            // bottom status line: hovered pair + raw value
    QString infoText;             // unelided text; infoLabel shows an elided copy

    /// Set the bottom info line.  Stores the full text and displays an elided
    /// copy, so the label never demands the width of its whole string.
    void setInfoText(const QString& text);
    /// Re-elide infoText to the label's current width.
    void updateInfoElide();

    // ── marked-node template region (§11.5) ───────────────────────────────
    MatrixTemplateStrip strip_;   // columns + shade + geometry (shared helper)
    /**Per-cluster compacted means and noise floors the matrix was built from,
     * kept PER CACHE SLOT (all-channel vs selection) so a channel-selection swap
     * — which swaps the displayed matrix without recomputing — keeps the strip
     * consistent with whichever matrix is shown.  recomputeTemplateStripCells()
     * reads the slot `scores` currently aliases.*/
    std::vector<std::vector<float>> meanAll_, meanSel_;
    std::vector<double>             noiseAll_, noiseSel_;
    std::vector<int>                keepAll_,  keepSel_;    // surviving channels (empty = all)
    int                             nChanFull_ = 0, nSampFull_ = 0;
    /**Per (template t, cluster j) separability index, both off-diagonal
     * directions kept separately (the matrix is asymmetric):
     *  tplClusterRow_[t][j] — cell (row=cluster j, col=template t), floor=cluster noise
     *  tplTemplateRow_[t][j] — cell (row=template t, col=cluster j), floor=template noise
     * tplCorner_[t][u] is the template×template block (row t's floor).  The
     * per-cell shade (value / dim / grey) lives in strip_.*/
    std::vector<std::vector<double>> tplClusterRow_;
    std::vector<std::vector<double>> tplTemplateRow_;
    std::vector<std::vector<double>> tplCorner_;
    void recomputeTemplateStripCells();
    void drawTemplateStrip(QPainter& painter);

    // ── helpers ──────────────────────────────────────────────────────────
    void launchComputeThread();
    void recomputeDisplayMax();
    void updateWindow();
    void drawMatrix(QPainter& painter);
    void drawClusterIds(QPainter& painter);

    QPoint matrixTopLeft() const;
    int    cellAtX(int viewX) const;
    int    cellAtY(int viewY) const;
};

#endif // RESIDUALMATRIXVIEW_H
