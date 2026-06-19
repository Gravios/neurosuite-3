#ifndef TEMPLATEMATRIXVIEW_H
#define TEMPLATEMATRIXVIEW_H

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
#include <QRadioButton>
#include <QButtonGroup>
#include <vector>
#include <utility>

#include "array.h"
#include "pair.h"

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

    explicit TemplateMatrixView(KlustersDoc& doc, KlustersView& view,
                                const QColor& backgroundColor,
                                QStatusBar* statusBar,
                                QWidget* parent = nullptr);
    ~TemplateMatrixView();

    void willBeKilled();
    bool isThreadsRunning() const;

    /** Synchronously stop all in-flight threads (TemplateMatrixThread instances
     *  in `threadsToBeKill` plus the active PairXcorrThread).  Waits for each
     *  thread to actually return from `run()` before returning, so callers can
     *  safely write to .spk.pending immediately afterward without worrying
     *  about torn reads.
     *
     *  Distinct from `willBeKilled()`: this method does NOT set `goingToDie`,
     *  so the view continues to function and can launch new threads
     *  afterward (e.g. when the user triggers an update).
     *
     *  Distinct from `stopPairThread()`: that method is asynchronous (sets
     *  the stop flag and abandons the pointer); this method waits for
     *  termination so the underlying file handle is actually closed.
     *
     *  Not an override: TemplateMatrixView inherits from QWidget, not
     *  ViewWidget, so it isn't part of `KlustersView::viewList` and cannot
     *  hook the existing `ViewWidget::stopRunningThreads()` virtual.
     *  Instead, callers walk `findChildren<TemplateMatrixView*>()` and
     *  invoke this method directly — see the call site in
     *  KlustersView::stopAllViewThreads. */
    void stopRunningThreadsSync();

    void updateMatrixContents();
    void updateSliderRange();

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
    void onThresholdChanged(int sliderValue);
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
    double  panX{0.0};
    double  panY{0.0};
    double  zoom{1.0};
    bool    panning{false};
    QPoint  panAnchorPx;        // mouse position where Ctrl-drag started
    double  panAnchorX{0.0};    // panX at drag start
    double  panAnchorY{0.0};    // panY at drag start
    static constexpr double zoomMin{0.5};
    static constexpr double zoomMax{20.0};
    static constexpr double zoomStep{1.15};  // wheel/key zoom multiplier per tick
    static constexpr int    panDragThreshold{3};  // px before press → pan

    inline double effCellSize() const { return cellWidth * zoom; }
    inline QPointF effMatrixTopLeft() const {
        const QPoint b = matrixTopLeft();
        return QPointF(b.x() + panX, b.y() + panY);
    }
    void  zoomAroundPoint(double newZoom, const QPointF& pivot);
    void  resetPanZoom();

    // ── matrix data (from main thread) ──────────────────────────────────────
    Array<double>* scores;        // [nClusters × nClusters], 1-based mean xcorr
    QList<int>     clusterList;
    bool           dataReady;
    bool           goingToDie;
    bool           isStale;
    int            generation;

    // Stored for PairXcorrThread construction
    std::vector<std::vector<float>> meanWav;    // [clusterIdx] channel-major mean
    std::vector<std::vector<int>>   allFileIdx; // [clusterIdx] 0-based .spk indices

    QList<TemplateMatrixThread*> threadsToBeKill;

    // ── per-pair xcorr (on-demand) ───────────────────────────────────────────
    PairXcorrThread* pairThread;      // currently running pair thread (or null)
    int              pairGeneration;  // incremented on each new pair request

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

    // ── threshold UI ─────────────────────────────────────────────────────────
    QSlider*     thresholdSlider;
    QLabel*      thresholdLabel;
    QLabel*      countLabel;
    QPushButton* applyButton;
    // ── metric selector (Cosine / Pearson / Raw), shares configuration().templateXcorrMetric
    QRadioButton* metricCosRadio;
    QRadioButton* metricPearsonRadio;
    QRadioButton* metricRawRadio;
    QRadioButton* metricDisattenRadio;
    QRadioButton* metricFastRadio;
    double       currentThreshold;
    double       sliderMin;
    double       sliderMax;

    // ── helpers ──────────────────────────────────────────────────────────────
    TemplateMatrixThread* launchComputeThread();
    void launchPairXcorr(int sourceCluster, int targetCluster);
    void stopPairThread();
    void updateWindow();
    void drawMatrix(QPainter& painter);
    void drawClusterIds(QPainter& painter);
    void updateSliderPreview();

    QPoint matrixTopLeft() const;
    int    cellAtX(int viewX) const;
    int    cellAtY(int viewY) const;

    double sliderToThreshold(int v) const {
        return sliderMin + (sliderMax - sliderMin) * v / 100.0;
    }
    int thresholdToSlider(double t) const {
        if (sliderMax <= sliderMin) return 0;
        return static_cast<int>((t - sliderMin) / (sliderMax - sliderMin) * 100.0 + 0.5);
    }
};

#endif // TEMPLATEMATRIXVIEW_H
