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
    Array<double>* scores;        // [N x N], 1-based, asymmetric raw residual
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
