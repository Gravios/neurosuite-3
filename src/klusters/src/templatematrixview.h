#ifndef TEMPLATEMATRIXVIEW_H
#define TEMPLATEMATRIXVIEW_H

#include <algorithm>
#include "rangeslider.h"
#include <QWidget>
#include <QMap>
#include <QColor>
#include <QList>
#include <QPixmap>
#include <QPainter>
#include <QStatusBar>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include <vector>
#include <utility>
#include <memory>

#include "array.h"
#include "pair.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the jobs)
#include "matrixtemplatestrip.h"  // MatrixTemplateStrip — shared marked-node template region (§11.5)
#include "matrixviewport.h"       // MatrixViewport — shared pan/zoom state + per-scope swap
#include "matrixnavigator.h"      // MatrixNavigator — shared Ctrl+Left-drag pan state machine

class KlustersDoc;
class KlustersView;
class TemplateMatrixThread;
class PairXcorrThread;

/**
 * View displaying a waveform template-match matrix.
 *
 * The matrix cells show peak normalised xcorr between cluster mean waveforms
 * (fast, computed once).  Per-spike xcorr against a selected cluster mean is
 * computed on demand when the user clicks a cell, and cached for reuse.
 *
 * Colour: blue (0) → green (0.5) → red (1.0).
 *
 * Workflow:
 *   1. Click cell (row=source, col=target): highlights that cell, shows other
 *      clusters in remaining views, launches per-spike xcorr for the pair.
 *   2. Slider (range from preferences, default 0.75–1.0): once per-spike
 *      scores arrive, shows count of source spikes ≥ threshold.
 *   3. Return/Enter or Apply: moves the above-threshold source spikes to the
 *      target cluster.
 *   4. U key (via KlustersApp) or T shortcut: recomputes everything from scratch.
 */
class TemplateMatrixView : public QWidget {
    Q_OBJECT

public:
    friend class TemplateMatrixThread;
    friend class PairXcorrThread;

    /// Set the zoom level from an external source (e.g. the synchronised error
    /// matrix view): sets the full zoom + pan state. Does not emit
    /// viewChanged, so the two views can be cross-connected without a loop.
    void setViewState(double zoom, double px, double py);

    /// Channel selection committed in the waveform view (empty = all channels).
    /// Swaps in a cached matrix when one matches, otherwise recomputes.
    void selectedChannelsChanged(const QList<int>& channels);

    explicit TemplateMatrixView(KlustersDoc& doc, KlustersView& view,
                                const QColor& backgroundColor,
                                QStatusBar* statusBar,
                                QWidget* parent = nullptr);
    ~TemplateMatrixView();

    void willBeKilled();
    bool isThreadsRunning() const;

    /** Synchronously quiesce all in-flight jobs (matrix computes plus the
     *  active pair xcorr).  Supersedes both request streams by bumping their
     *  token generations, then waits for the active counts to drain, so
     *  callers can safely write to .spk.pending immediately afterward
     *  without worrying about torn reads.
     *
     *  Distinct from `willBeKilled()`: this method does NOT set `goingToDie`,
     *  so the view continues to function and can launch new requests
     *  afterward (e.g. when the user triggers an update).
     *
     *  Distinct from `stopPairThread()`: that method only supersedes (no
     *  wait); this method waits for retirement so the in-flight file reads
     *  have actually finished.
     *
     *  Not an override: TemplateMatrixView inherits from QWidget, not
     *  ViewWidget, so it isn't part of `KlustersView::viewList` and cannot
     *  hook the existing `ViewWidget::stopRunningThreads()` virtual.
     *  Instead, callers walk `findChildren<TemplateMatrixView*>()` and
     *  invoke this method directly — see the call site in
     *  KlustersView::stopAllViewThreads. */
    void stopRunningThreadsSync();
    /**Non-blocking twin (epoch-snapshot step 6b): bump both streams, wait
    * for nothing.  For membership-only edits; the Sync form remains for
    * the .spk writers.*/
    void supersedeRunningThreads();

    void updateMatrixContents();
    void updateSliderRange();

    /** Marked-node templates to append as cluster×template columns/rows (§11.5).
     *  Empty clears the strip.  Recomputes the cells against the current per-cluster
     *  means and repaints. */
    void setTemplateColumns(const std::vector<MatrixTemplateCol>& cols);

    /**Arms a Ctrl+Left pan at @p pressPos (viewport coords); invoked by the input
  * registry's matrix.template.pan Gesture.  Drag/commit stay in the mouse handlers.*/
    void matrixBeginPan(const QPoint& pressPos);

    // Stale-marker slots wired from KlustersDoc signals
    void clustersGrouped(QList<int>& groupedClusters, int newClusterId);
    void clustersDeleted(QList<int>& deletedClusters, int destinationCluster);
    void removeSpikesFromClusters(QList<int>& fromClusters, int destinationClusterId,
                                  QList<int>& emptiedClusters);
    void newClusterAdded(QList<int>& fromClusters, int clusterId,
                         QList<int>& emptiedClusters);
    void newClustersAdded(QMap<int,int>& fromToNewClusterIds, QList<int>& emptiedClusters);
    void newClustersAdded(QList<int>& clustersToRecluster);
    void renumber(QMap<int,int>& clusterIdsOldNew);
    /** Mark the matrix out of date without naming the edit.  Wired to the
     *  undo/redo family and clusterFeaturesReprojected, which change the data
     *  this matrix is built from but never fire the edit signals above. */
    void markStale();

    // ── Read-only accessors used by KlustersApp::slotReorderClustersBySimilarity ──
    // The scores matrix is a row-major Array<double> indexed 1..nClusters
    // (cluster ID for row/col i is clusterList[i-1]).  Higher score =
    // closer mean-waveform match, so this is a similarity matrix in the
    // reorder algorithm's terminology.

    /// True once at least one template-matrix computation has completed.
    bool hasComputedData() const { return dataReady; }
    /// True if the matrix has never been computed OR has been invalidated
    /// (isStale) by a cluster-mutating action since the last
    /// updateMatrixContents().  Mirrors ErrorMatrixView::isOutOfDate so
    /// callers (Shift+S reorder) can check both with the same API.
    bool isOutOfDate() const { return !dataReady || isStale; }
    /// Cluster IDs corresponding to matrix rows/columns (1-based mapping).
    QList<int> matrixClusterList() const { return clusterList; }
    /// Pointer to the [N x N] score matrix (1-based; may be null).
    const Array<double>* matrixData() const { return scores; }

Q_SIGNALS:
    /// Emitted when the user clicks anywhere in the matrix view.  Used
    /// by KlustersApp to track which matrix view the user most recently
    /// interacted with — Shift+S reorder uses that matrix when both
    /// error and template matrices coexist.
    void viewInteracted();

    /// Emitted when the user clicks a marked-node template STRIP cell (§11.5):
    /// `clusterId` is the cluster that cell sits on (-1 for the template×template
    /// corner) and `node` is the lineage node it compares against.  KlustersView
    /// selects the cluster and asks the ClusterView to overlay just that node.
    void templateCellActivated(int clusterId, int node);

    /// Emitted when the user changes this view's zoom level (wheel, +/- keys,
    /// reset). Used to keep the error and template matrix zooms synchronised.
    void viewChanged(double zoom, double panX, double panY);

    /// Emitted from customEvent() each time a freshly computed matrix is
    /// accepted.  Symmetric with ErrorMatrixView::matrixUpdated() so
    /// Shift+S can defer the reorder until a stale template matrix has
    /// finished recomputing.
    void matrixUpdated();

protected:
    void paintEvent(QPaintEvent*) override;
    void customEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent*) override {}
    void wheelEvent(QWheelEvent* event) override;   // patch80
    void keyPressEvent(QKeyEvent* event) override;

private Q_SLOTS:
    /**Span moved: @p low is the colour axis floor, @p high is both the colour
     * axis ceiling and the highlight threshold.*/
    void onThresholdChanged(double low, double high);
    void onApplyClicked();
    /// Live metric selector (Cosine / Pearson / Raw) next to the threshold
    /// slider.  Writes configuration().templateXcorrMetric (shared with the
    /// Display preference page) and recomputes the matrix with the new metric.
    void onMetricChanged();

private:
    // ── doc / view ──────────────────────────────────────────────────────────
    KlustersDoc& doc;
    KlustersView& view;
    QStatusBar*   statusBar;

    // ── patch80: pan + zoom state ───────────────────────────────────────────
    // The matrix is rendered at an effective top-left of
    //   matrixTopLeft() + (panX, panY)
    // with each cell drawn at effective size
    //   cellWidth * zoom
    // Pan is in widget pixels; zoom is a unitless multiplier clamped to
    // [zoomMin, zoomMax].  Hit-testing (cellAtX/Y) and drawing
    // (drawMatrix, drawClusterIds) consult these via effCellSize() and
    // effMatrixTopLeft() so the same transform applies to both.
    //
    // Activation:
    //   Ctrl + Left-drag      → pan
    //   Ctrl + Mouse Wheel    → zoom around the cursor position
    //   +/=  /  -             → zoom in / zoom out (around the centre)
    //   0                     → reset pan & zoom
    // Pan/zoom state + the per-scope view swap (shared helper, audit D1/S5).  This
    // view keeps its own matrixTopLeft()/effZoomMin()/mouse handling below.
    MatrixViewport vp_;
    void swapViewStateForScope(bool scopeActive) { vp_.swapForScope(scopeActive); }
    MatrixNavigator nav_;       // Ctrl+Left-drag pan state (shared helper)
    static constexpr double zoomMin{0.5};    // baseline zoom-out floor; effZoomMin() lowers it to fit large grids
    static constexpr double zoomMax{20.0};
    static constexpr double zoomStep{1.15};  // wheel/key zoom multiplier per tick
    static constexpr int    panDragThreshold{3};  // px before press → pan

    /// Adaptive minimum zoom (maximum zoom-out).  At zoom Z the matrix spans
    /// n*cellWidth*Z pixels; for large cluster counts cellWidth bottoms out at 4px
    /// and the grid no longer fits the viewport even at the 0.5 floor.  Lower the
    /// floor just enough to let the whole matrix fit on screen (so all clusters are
    /// visible at ~10000), but never above the historical 0.5, so small grids are
    /// unaffected.
    double  effZoomMin() const;

    inline double effCellSize() const { return cellWidth * vp_.zoom; }
    inline QPointF effMatrixTopLeft() const { return vp_.effTopLeft(matrixTopLeft()); }
    void  zoomAroundPoint(double newZoom, const QPointF& pivot);
    void  resetPanZoom();

    // ── matrix data (from main thread) ──────────────────────────────────────
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

    Array<double>* scores;        // [nClusters × nClusters] (NON-owning alias)
    QList<int>     clusterList;
    bool           dataReady;
    bool           goingToDie;
    bool           isStale;

    // Stored for PairXcorrThread construction
    std::vector<std::vector<float>> meanWav;    // [clusterIdx] channel-major mean
    std::vector<std::vector<int>>   allFileIdx; // [clusterIdx] 0-based .spk indices
    // Per-mean noise energy (sample variance / N), channel-major like meanWav —
    // filled by the matrix thread ONLY under the disattenuated metric, empty
    // otherwise.  The strip disattenuates its cluster×template cells against it.
    std::vector<std::vector<float>> noiseWav_;
    // ln-domain interval mapping the raw metric onto [0,1] (see tmRawLogNorm),
    // computed by the matrix thread over its off-diagonal distribution and shipped
    // in the event, so the strip's raw cells use the SAME scale as the matrix.
    // Both zero for every other metric.
    double rawLogLo_ = 0.0;
    double rawLogHi_ = 0.0;

    // ── marked-node template region (§11.5) ─────────────────────────────────
    MatrixTemplateStrip              strip_;     // columns + shade + geometry (shared helper)
    std::vector<std::vector<double>> tplVal_;    // [tpl][cluster] cluster×template xcorr
    std::vector<std::vector<double>> tplCorner_; // [tpl][tpl]     template×template xcorr
    /** Recompute tplVal_ / strip_ shade / tplCorner_ from the current means + the metric
     *  (cheap: a handful of templates × clusters).  Called on setTemplateColumns, on a
     *  fresh matrix (means changed) and on a metric change. */
    void recomputeTemplateCells();
    /** Paint the template strip (right columns + bottom rows + corner) over the grid. */
    void drawTemplateStrip(QPainter& p);
    /** xcorr(a,b) under the current template metric, matching TemplateMatrixThread's
     *  matrix build for every mode: 0 cosine, 1 pearson, 2 raw (log-normalised onto
     *  [0,1] via the matrix's shared ln-domain interval), 3 noise-disattenuated,
     *  4 fast-AP-windowed, 5 inter-channel profile.  The disattenuated metric (3) needs
     *  the per-point noise energy of each operand's mean; pass @p noiseA / @p noiseB
     *  (channel-major, same length as a / b).  When they are missing or mis-sized the
     *  disattenuated metric falls back to plain cosine; every other metric ignores them. */
    double templatePairXcorr(const std::vector<float>& a, const std::vector<float>& b,
                             const std::vector<float>& noiseA = std::vector<float>(),
                             const std::vector<float>& noiseB = std::vector<float>()) const;

    /**Cancellation/completion state shared with the jobs this view enqueues
    * on the worker pool — one token per request stream, because a pair
    * request must be supersedable without orphaning a matrix compute (and
    * vice versa).  These replace the threadsToBeKill list, the pairThread
    * pointer and the generation/pairGeneration counters: the tokens' own
    * generations are the request generations now.*/
    std::shared_ptr<KlustersJobToken> matrixToken;
    std::shared_ptr<KlustersJobToken> pairToken;

    // Cache: (sourceClusterId, targetClusterId) → per-spike scores
    // Cleared when the matrix is recomputed (updateMatrixContents).
    QMap<QPair<int,int>, std::vector<std::pair<int,float>>> pairCache;

    // ── selected pair ────────────────────────────────────────────────────────
    int selectedA;   // column (target)  cluster id, -1 = none
    int selectedB;   // row    (source) cluster id

    // ── geometry ─────────────────────────────────────────────────────────────
    int cellWidth;
    int widthBorder, heightBorder;

    // ── colour ───────────────────────────────────────────────────────────────
    static constexpr int NB_COLORS = 100;
    QMap<int, QColor> colorMap;
    void initializeColorMap();
    // Painted-text colour (cluster ids, overlay messages): black or white,
    // chosen from the background luminance so it stays legible on either.
    QColor textColor;

    // ── drawing ──────────────────────────────────────────────────────────────
    QPixmap doublebuffer;
    QRect   matrixViewport;
    QList<Pair> selectedPairs;

    // ── threshold / colour-axis UI ───────────────────────────────────────────
    /**Two-handle span.  The span IS the colour-scale axis: scores at or below
     * colourLow draw as the bottom of the ramp, at or above colourHigh as the
     * top, so narrowing it stretches the ramp over the interesting range.  The
     * UPPER handle doubles as the highlight threshold (currentThreshold), which
     * is what the white cell outlines and the Apply spike-move both key off, so
     * the top of the colour scale and the "interesting" cut are the same edge.*/
    RangeSlider* thresholdSlider;
    QLabel*      thresholdLabel;
    QLabel*      countLabel;
    QPushButton* applyButton;
    // ── metric selector (Cosine / Pearson / Raw / Disatten. / Fast-AP),
    //    shares configuration().templateXcorrMetric; index == metric int.
    QComboBox* metricCombo;
    double       currentThreshold;   // == the span's UPPER handle
    double       colourLow;         // == the span's LOWER handle
    double       sliderMin;
    double       sliderMax;

    // ── helpers ──────────────────────────────────────────────────────────────
    void launchComputeThread();
    void launchPairXcorr(int sourceCluster, int targetCluster);
    /**Supersedes the in-flight pair-xcorr job, if any: bumps the pair token's
    * request generation, so the job early-outs and its result is dropped by
    * the generation guard (a superseded PARTIAL score list must never enter
    * the cache).*/
    void stopPairThread();
    void updateWindow();
    void drawMatrix(QPainter& painter);
    void drawClusterIds(QPainter& painter);
    void updateSliderPreview();
    /**Refresh the label showing the colour axis and the threshold.*/
    void updateThresholdLabel();

    QPoint matrixTopLeft() const;
    int    cellAtX(int viewX) const;
    int    cellAtY(int viewY) const;

    /**Map a score to a colour index using the span as the axis.  Degenerate
     * spans (both handles together) collapse to a single step rather than
     * dividing by zero.*/
    int colourIndexFor(double score, int nbColors) const {
        const double lo = colourLow, hi = currentThreshold;
        double f = (hi > lo) ? (score - lo) / (hi - lo) : (score >= hi ? 1.0 : 0.0);
        f = std::max(0.0, std::min(1.0, f));
        return std::max(0, std::min(nbColors - 1,
                                    static_cast<int>(f * (nbColors - 1) + 0.5)));
    }
};

#endif // TEMPLATEMATRIXVIEW_H
