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
#include "klustersjobpool.h"    // KlustersJobToken (shared with the jobs)
#include "matrixtemplatestrip.h" // MatrixTemplateStrip — shared marked-node template region (§11.5)
#include "matrixviewport.h"      // MatrixViewport — shared pan/zoom state + per-scope swap

#include <memory>

class KlustersDoc;
class KlustersView;
class DriftMatrixThread;
class DriftShiftThread;
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
    /**Non-blocking twin (epoch-snapshot step 6b) — see TemplateMatrixView.*/
    void supersedeRunningThreads();

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
    /** Mark the matrix out of date without naming the edit (undo/redo family,
     *  clusterFeaturesReprojected).  With the visibility gate this doubles as
     *  recompute-on-reveal: showEvent() relaunches when isStale. */
    void markStale();

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

    /**Re-evaluate the slider's enabled state from geometry AND the current
     * cluster-count preference.  Public so applyPreferences() can call it when
     * the limit changes, rather than the change waiting for the next recompute.*/
    void refreshSliderEnabled();

    /**Marked-node template strip (§11.5): the curator's marked lineage nodes
     * pushed from ClusterView via KlustersView::setMatrixTemplateColumns.  Each
     * cell is the drift-shifted xcorr between a cluster mean and a template mean
     * at the current slider µm — ASYMMETRIC like the matrix body: the ROW is the
     * shifted side (a cluster-row shifts +Δ against a template column; a
     * template-row shifts −Δ against a cluster column), greyed where the cluster
     * has no spikes in the node's time window.  Recomputed on every slider step.*/
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
    /// Set the full zoom + pan state from another (cross-connected) matrix
    /// view.  Does not emit viewChanged, so the views can be cross-connected
    /// without a feedback loop.
    void setViewState(double zoom, double px, double py);

protected:
    /**Recompute on reveal if an edit landed while this view was hidden.*/
    void showEvent(QShowEvent* event) override;

    void paintEvent(QPaintEvent*) override;
    void customEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

public Q_SLOTS:
    /**Channel selection committed in the waveform view (empty = all channels).
     * Swaps in a cached matrix when one matches, otherwise recomputes.*/
    void selectedChannelsChanged(const QList<int>& channels);

private Q_SLOTS:
    void driftSliderChanged(int um);
    void driftRangeChanged(int maxUm);

private:
    KlustersDoc&  doc;
    KlustersView& view;
    QStatusBar*   statusBar;

    // ── pan + zoom (shared helper, audit D1/S5; same transform as the others) ──
    // This view keeps its own matrixTopLeft()/mouse handling; it has no adaptive
    // effZoomMin(), so it clamps to the plain zoomMin.
    MatrixViewport vp_;
    void swapViewStateForScope(bool scopeActive) { vp_.swapForScope(scopeActive); }
    bool    panning{false};
    QPoint  panAnchorPx;
    double  panAnchorX{0.0};
    double  panAnchorY{0.0};
    static constexpr double zoomMin{0.5};
    static constexpr double zoomMax{20.0};
    static constexpr double zoomStep{1.15};
    static constexpr int    panDragThreshold{3};

    inline double effCellSize() const { return cellWidth * vp_.zoom; }
    inline QPointF effMatrixTopLeft() const { return vp_.effTopLeft(matrixTopLeft()); }
    void  zoomAroundPoint(double newZoom, const QPointF& pivot);
    void  resetPanZoom();

    // ── matrix data ──────────────────────────────────────────────────────
    /**One computed matrix and everything the view needs to keep using it.
     * The drift slider rebuilds the matrix from meanWav/depths, so a cached
     * matrix is only usable together with the means it was computed from —
     * caching the Array alone would leave the slider recomputing a
     * differently-masked matrix.*/
    /**Cluster count above which the drift slider is disabled, read fresh from
     * Preferences > Display > Drift Matrix on each check so a change applies
     * without reopening the session.  0 means always disabled.
     *
     * A slider step is a full O(clusters^2) pass, so the control degrades from
     * interactive to unusable well before the matrix itself becomes unreadable.
     * It is also only meaningful once the sort has been consolidated -- its job
     * is linking a unit to itself across a period of drift, a question about a
     * few hundred real units rather than thousands of unmerged fragments.*/
    int driftSliderClusterCap() const;

    struct Cache {
        Array<double>*                  scores = nullptr;   // owned
        std::vector<std::vector<float>> meanWav;
        std::vector<float>              depths;
        int                             nChan = 0;
        bool                            geometryOk = false;
        bool                            valid = false;
    };
    /**All-channel result and the result for cachedSelection.  Keeping both is
     * what makes flicking the channel selection on and off an instant swap
     * instead of a recompute.*/
    Cache      cacheAll;
    Cache      cacheSel;

    /**Cancellation/completion state for the jobs rebuilding the matrix at a
     * new slider position.  Separate from computeToken (full recomputes)
     * because the two are superseded independently: a drag supersedes only
     * other drags.  Replaces the shiftThreads list and shiftGeneration.*/
    std::shared_ptr<KlustersJobToken> shiftToken;
    /**True when the slider is disabled purely because of the cluster count, so
     * the tooltip can say so rather than blaming missing geometry.*/
    bool                     sliderCappedByClusterCount = false;
    /**Supersede and quiesce every slider job.*/
    void stopShiftThreads();
    /**A recompute is in flight: the view keeps painting whatever it has and
     * overlays a small badge instead of blanking the frame.*/
    bool       computing = false;
    QList<int> cachedSelection;   // the selection cacheSel was computed for

    /**Make @p c the displayed matrix (no recompute).*/
    void activateCache(const Cache& c);
    /**Drop both cached results (the spikes themselves changed).*/
    void invalidateCaches();
    /**Start a compute for the document's current channel selection WITHOUT
     * touching the caches.  A selection change must not discard the other
     * slot — keeping it is what makes swapping back free.*/
    void launchCompute();

    Array<double>* scores;        // [N x N], 1-based, xcorr in [0,1] (NON-owning:
                                  // aliases cacheAll.scores or cacheSel.scores)
    QList<int>     clusterList;
    bool           dataReady;
    bool           goingToDie;
    bool           isStale;

    // Cached from the worker so the slider can recompute without touching .spk.
    std::vector<std::vector<float>> meanWav;
    std::vector<float>              depths;      // per channel (µm, y)
    int   nChanCached{0};
    int   nSampCached{0};
    int   maxShiftCached{1};
    bool  geometryOk{false};
    QString geometryError;   // why depths were unavailable (shown when disabled)
    int   currentDriftUm{0};

    /**Cancellation/completion state shared with the full-recompute jobs.
    * Replaces the threadsToBeKill ownership list and the generation counter:
    * the token's own generation is the request generation now.*/
    std::shared_ptr<KlustersJobToken> computeToken;

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
    QString   infoText;      // unelided text; infoLabel shows an elided copy

    /// Set the bottom info line.  Stores the full text and displays an elided
    /// copy, so the label never demands the width of its whole string.
    void setInfoText(const QString& text);
    /// Re-elide infoText to the label's current width.
    void updateInfoElide();

    // ── marked-node template region (§11.5) ───────────────────────────────
    MatrixTemplateStrip strip_;   // columns + shade + geometry (shared helper)
    /**Per (template t, cluster j) drift-shifted xcorr, both off-diagonal
     * directions kept separately because the drift matrix is asymmetric:
     *  tplClusterRow_[t][j] — cell (row=cluster j, col=template t): cluster +Δ
     *  tplTemplateRow_[t][j] — cell (row=template t, col=cluster j): template −Δ
     * tplCorner_[t][u] is the template×template block (row t shifted +Δ if t<u,
     * −Δ if t>u, 1 on the diagonal).  The per-cell shade (value / dim / grey)
     * lives in strip_.  Sized M×N (M×M corner).*/
    std::vector<std::vector<double>> tplClusterRow_;
    std::vector<std::vector<double>> tplTemplateRow_;
    std::vector<std::vector<double>> tplCorner_;
    /**Recompute the strip cells from the cached means at the current drift µm
     * (cheap: a handful of templates × clusters).  Called on setTemplateColumns
     * and on every accepted matrix/slider result (customEvent).*/
    void recomputeTemplateStripCells();
    void drawTemplateStrip(QPainter& painter);

    // ── helpers ──────────────────────────────────────────────────────────
    void launchComputeThread();
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
