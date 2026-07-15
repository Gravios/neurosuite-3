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

    // ── Read-only accessors (used by KlustersApp residual-gated reorder) ──
    // scores is a row-major Array<double> indexed 1..nClusters; cluster id for
    // row/col i is clusterList[i-1].  LOWER value = closer (a DISTANCE, not a
    // similarity), and the matrix is asymmetric.
    bool hasComputedData() const { return dataReady; }
    bool isOutOfDate()     const { return !dataReady || isStale; }
    QList<int> matrixClusterList() const { return clusterList; }
    const Array<double>* matrixData() const { return scores; }

Q_SIGNALS:
    void viewInteracted();
    void matrixUpdated();

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
    bool           haveSelCache = false;

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
    int            generation;
    double         displayMax;  // cached off-diagonal max for colour scaling

    QList<ResidualMatrixThread*> threadsToBeKill;

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

    // ── helpers ──────────────────────────────────────────────────────────
    ResidualMatrixThread* launchComputeThread();
    void recomputeDisplayMax();
    void updateWindow();
    void drawMatrix(QPainter& painter);
    void drawClusterIds(QPainter& painter);

    QPoint matrixTopLeft() const;
    int    cellAtX(int viewX) const;
    int    cellAtY(int viewY) const;
};

#endif // RESIDUALMATRIXVIEW_H
