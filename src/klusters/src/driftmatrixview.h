/***************************************************************************
 * driftmatrixview.h
 *
 * Read-only view of the drift-shifted cross-correlation matrix.
 *
 * Cell (row A, col B) = peak normalised xcorr between A's mean waveform,
 * resampled along the probe depth axis by the slider's µm value, and B's mean
 * waveform.  The upper triangle shifts A by +Δ µm (deeper), the lower triangle
 * by −Δ µm (shallower), so one neuron split into two units by probe drift
 * lights up on the side and at the Δ that realigns the pair.
 *
 * Colour: blue (0) → red (1); red = high xcorr = same-shape at that drift =
 * merge candidate.  The diagonal is drawn black.  Hover shows the pair, the
 * signed shift and the value.  Like the residual matrix this view is purely
 * diagnostic — it never moves spikes.
 *
 * The drift slider recomputes the matrix from the cached mean waveforms
 * (DriftMatrixThread caches them), so dragging never re-reads the .spk file.
 *
 * Pan: Ctrl + Left-drag.  Zoom: Ctrl + Wheel, or +/- ; 0 resets.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef DRIFTMATRIXVIEW_H
#define DRIFTMATRIXVIEW_H

#include <QWidget>
#include <QMap>
#include <QColor>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QStatusBar>
#include <QLabel>
#include <vector>

#include "array.h"

class KlustersDoc;
class KlustersView;
class DriftMatrixThread;
class QSlider;
class QSpinBox;

class DriftMatrixView : public QWidget {
    Q_OBJECT

public:
    friend class DriftMatrixThread;

    explicit DriftMatrixView(KlustersDoc& doc, KlustersView& view,
                             const QColor& backgroundColor,
                             QStatusBar* statusBar,
                             QWidget* parent = nullptr);
    ~DriftMatrixView() override;

    void willBeKilled();
    bool isThreadsRunning() const;

    /** Synchronously stop in-flight DriftMatrixThread instances and wait for
     *  run() to return.  Same contract as ResidualMatrixView::stopRunningThreadsSync;
     *  invoked from KlustersView::stopAllViewThreads via findChildren. */
    void stopRunningThreadsSync();

    void updateMatrixContents();

    // Stale-marker slots wired from KlustersDoc signals (mirror ResidualMatrixView).
    void clustersGrouped(QList<int>& groupedClusters, int newClusterId);
    void clustersDeleted(QList<int>& deletedClusters, int destinationCluster);
    void removeSpikesFromClusters(QList<int>& fromClusters, int destinationClusterId,
                                  QList<int>& emptiedClusters);
    void newClusterAdded(QList<int>& fromClusters, int clusterId,
                         QList<int>& emptiedClusters);
    void newClustersAdded(QMap<int,int>& fromToNewClusterIds, QList<int>& emptiedClusters);
    void newClustersAdded(QList<int>& clustersToRecluster);
    void renumber(QMap<int,int>& clusterIdsOldNew);

    // ── Read-only accessors ──────────────────────────────────────────────
    // scores is a row-major Array<double> indexed 1..nClusters; the cluster id
    // for row/col i is clusterList[i-1].  Values are xcorr in [0,1]; HIGHER =
    // more similar at the current drift.  Asymmetric by construction (the upper
    // triangle is shifted +Δ, the lower −Δ).
    bool hasComputedData() const { return dataReady; }
    bool isOutOfDate()     const { return !dataReady || isStale; }
    QList<int> matrixClusterList() const { return clusterList; }
    const Array<double>* matrixData() const { return scores; }

    /// Current drift magnitude in µm (upper triangle +, lower −).
    int driftUm() const { return currentDriftUm; }

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

private Q_SLOTS:
    void driftSliderChanged(int um);
    void driftRangeChanged(int maxUm);

private:
    KlustersDoc&  doc;
    KlustersView& view;
    QStatusBar*   statusBar;

    // ── pan + zoom (same transform as ResidualMatrixView) ────────────────
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
    Array<double>* scores;        // [N x N], 1-based, xcorr in [0,1]
    QList<int>     clusterList;
    bool           dataReady;
    bool           goingToDie;
    bool           isStale;
    int            generation;

    // Cached from the worker so the slider can recompute without touching .spk.
    std::vector<std::vector<float>> meanWav;
    std::vector<float>              depths;      // per channel (µm, y)
    int   nChanCached{0};
    int   nSampCached{0};
    int   maxShiftCached{1};
    bool  geometryOk{false};
    QString geometryError;   // why depths were unavailable (shown when disabled)
    int   currentDriftUm{0};

    QList<DriftMatrixThread*> threadsToBeKill;

    // ── geometry / colour / drawing ──────────────────────────────────────
    int cellWidth;
    int widthBorder, heightBorder;
    static constexpr int NB_COLORS = 100;
    QMap<int, QColor> colorMap;
    void initializeColorMap();
    QColor textColor;
    QPixmap doublebuffer;

    QSlider*  driftSlider;
    QSpinBox* maxUmSpin;
    QLabel*   driftLabel;
    QLabel*   infoLabel;

    // ── helpers ──────────────────────────────────────────────────────────
    DriftMatrixThread* launchComputeThread();
    /// Recompute every cell from the cached means at currentDriftUm.
    void recomputeAtCurrentDrift();
    void updateWindow();
    void drawMatrix(QPainter& painter);
    void drawClusterIds(QPainter& painter);

    QPoint matrixTopLeft() const;
    int    cellAtX(int viewX) const;
    int    cellAtY(int viewY) const;
};

#endif // DRIFTMATRIXVIEW_H
