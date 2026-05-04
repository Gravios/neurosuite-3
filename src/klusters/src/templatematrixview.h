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

protected:
    void paintEvent(QPaintEvent*) override;
    void customEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    QSize sizeHint() const override;
    void mousePressEvent(QMouseEvent*) override {}
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent*) override {}
    void keyPressEvent(QKeyEvent* event) override;

private Q_SLOTS:
    void onThresholdChanged(int sliderValue);
    void onApplyClicked();

private:
    // ── doc / view ──────────────────────────────────────────────────────────
    KlustersDoc& doc;
    KlustersView& view;
    QStatusBar*   statusBar;

    // ── matrix data (from main thread) ──────────────────────────────────────
    Array<double>* scores;        // [nClusters × nClusters], 1-based mean xcorr
    QList<int>     clusterList;
    bool           dataReady;
    bool           goingToDie;
    bool           isStale;
    int            m_generation;

    // Stored for PairXcorrThread construction
    std::vector<std::vector<float>> m_meanWav;    // [clusterIdx] channel-major mean
    std::vector<std::vector<int>>   m_allFileIdx; // [clusterIdx] 0-based .spk indices

    QList<TemplateMatrixThread*> threadsToBeKill;

    // ── per-pair xcorr (on-demand) ───────────────────────────────────────────
    PairXcorrThread* m_pairThread;      // currently running pair thread (or null)
    int              m_pairGeneration;  // incremented on each new pair request

    // Cache: (sourceClusterId, targetClusterId) → per-spike scores
    // Cleared when the matrix is recomputed (updateMatrixContents).
    QMap<QPair<int,int>, std::vector<std::pair<int,float>>> m_pairCache;

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

    // ── drawing ──────────────────────────────────────────────────────────────
    QPixmap doublebuffer;
    QRect   matrixViewport;
    QList<Pair> selectedPairs;

    // ── threshold UI ─────────────────────────────────────────────────────────
    QSlider*     thresholdSlider;
    QLabel*      thresholdLabel;
    QLabel*      countLabel;
    QPushButton* applyButton;
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
