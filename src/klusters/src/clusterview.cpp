/***************************************************************************
                          clusterview.cpp  -  description
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

//include files for the application
#include "clusterview.h"
#include "klustersview.h"
#include "klustersdoc.h"
#include "waveformview.h"          // residual preview is pushed to the sibling view
#include "data.h"
#include "itemcolors.h"
#include "configuration.h"

#include "timer.h"
#include <QDebug>
#include <QKeyEvent>
#include <QWheelEvent>

//General C++ include files
#include <math.h>
#include <stdlib.h>

// include files for Qt
#include <QPaintDevice>
#include <QApplication>
#include <QCursor>



#include <QPolygon>
#include <QList>
#include <QMouseEvent>
#include <QEvent>
#include <QRandomGenerator>
#include <random>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <QSet>
#include <QThread>
#include <QPointer>
#include <QElapsedTimer>
#include <memory>

#include "tsne_embed.h"
#include "oblique_embed.h"
#include "configuration.h"

#include "neurosuite/core/neurofileio.h"   // readWti / readTcl for the projection scope
#include "neurosuite/core/custody.hpp"     // parseAnchor / untaggedPath for the .wti/.tcl paths
#include <QFileInfo>



const QColor ClusterView::NEW_CLUSTER_COLOR(Qt::green);
const QColor ClusterView::DELETE_NOISE_COLOR(220,220,220);
const QColor ClusterView::DELETE_ARTEFACT_COLOR(Qt::red);

ClusterView::ClusterView(KlustersDoc& doc,KlustersView& view,const QColor& backgroundColor,int timeInterval,QStatusBar * statusBar,QWidget* parent, const char* name,
                         int minSize, int maxSize, int windowTopLeft ,int windowBottomRight, int border) :
    ViewWidget(doc,view,backgroundColor,statusBar,parent,name,minSize,maxSize,windowTopLeft,windowBottomRight,border),
    selectionPolygon(0),
    nbSelectionPoints(0),
    polygonClosed(false)
{
    //Set the default mode
    mode = ZOOM;
    pointSize = 2;
    selectionLineWidth = 1;
    setFocusPolicy(Qt::StrongFocus);

    //Initialize internal variables
    timeDimension = doc.data().timeDimension();
    samplingInterval = doc.data().intervalOfSampling();
    setTimeStepInSecond(timeInterval);
    loadEapCollisions();                 // precompute the .eap collision set for this stage

    //Update the dimension of the window and the values of dimensionX and dimensionY
    //Qualified (non-virtual) call: ClusterView is the most-derived type in its
    //own constructor, so dispatch can't reach a further override anyway — making
    //the static binding explicit documents that and silences the virtual-call-
    //in-constructor warning.
    ClusterView::updatedDimensions(view.abscissaDimension(),view.ordinateDimension());

    newClusterCursor = QCursor(QPixmap(":/cursors/new_cluster_cursor.png"),0,0);
    newClustersCursor = QCursor(QPixmap(":/cursors/new_clusters_cursor.png"),0,0);
    deleteNoiseCursor = QCursor(QPixmap(":/cursors/delete_noise_cursor.png"),0,0);
    deleteArtefactCursor = QCursor(QPixmap(":/cursors/delete_artefact_cursor.png"),0,0);
    selectTimeCursor = QCursor(QPixmap(":/shared-cursors/select_time_cursor"),0,0);

    //The default tool is the zoom.
    setCursor(zoomCursor);

    //Allowed the mouse tracking to draw the tracking lines and write the mouse coordinates
    setMouseTracking(true) ;
}

ClusterView::~ClusterView(){
    // A t-SNE worker may still be embedding; it only reads its own copies,
    // but it must not outlive the widget it reports back to.
    tsneCancel = true;
    if (tsneThread) tsneThread->wait();
}

// ── t-SNE alternate presentation ────────────────────────────────────────────

void ClusterView::tsneDropIfActive(){
    // Our own lasso edit is not an invalidation: it changes membership, not
    // positions, and applyTsneLasso() recolours in place afterwards.
    if (tsneApplyingLasso)
        return;
    if (tsneMode || tsneComputing || tsneThread)
        exitTsne(tr("t-SNE dropped: clusters changed"));
}

void ClusterView::tsneInvalidate(){
    if (!tsneMode) { tsneDropIfActive(); return; }   // in-flight run: drop
    tsneRelabelFromDoc();
    drawContentsMode = REDRAW;
    update();
}

void ClusterView::exitTsne(const QString& reason){
    clearPendingLasso();                 // a pending residual preview does not outlive the embedding
    tsneCancel = true;
    // Do NOT wait here.  The flag is polled inside the neighbour and bandwidth
    // phases now, so the worker stops promptly -- but promptly is not
    // instantly, and this runs on the GUI thread, where waiting froze the
    // application for as long as the abandoned phase took.  Abandon the thread
    // instead: it deletes itself on finished(), and the run-id below makes its
    // result identify itself as stale when it arrives.
    ++tsneRunId;
    tsneThread = nullptr;
    tsneComputing = false;
    tsneProgressText.clear();
    resetSelectionPolygon();
    if (tsneMode) {
        tsneMode = false;
        obliqueMode = false;        // obliqueMode implies tsneMode; clear together
        obliqueCond = 0.0;
        drawContentsMode = REDRAW;
        update();
    }
    if (!reason.isEmpty() && statusBar) statusBar->showMessage(reason, 3000);
}

void ClusterView::adjustTsnePerplexity(int direction){
    if (!tsneMode) return;                       // arrows are normal keys otherwise
    if (obliqueMode) return;                     // oblique has no perplexity to step
    if (tsneComputing) {                         // one embedding at a time
        if (statusBar) statusBar->showMessage(
            tr("t-SNE: still computing — wait, or press F to cancel"), 3000);
        return;
    }
    const int step = qMax(1, configuration().getTsnePerplexityStep());
    // Upper bound is the engine's own clamp: the perplexity must stay below
    // (N-1)/3, since that is the neighbour count its bandwidth search uses.
    const double hi = qMax(2.0, (tsneSpikeCount - 1) / 3.0);
    const double want = qBound(2.0, tsnePerplexity + direction * step, hi);
    if (qFuzzyCompare(want, tsnePerplexity)) {
        if (statusBar) statusBar->showMessage(
            direction > 0
              ? tr("t-SNE: perplexity already at the maximum for %1 spikes (%2)")
                    .arg(tsneSpikeCount).arg(hi, 0, 'f', 0)
              : tr("t-SNE: perplexity already at the minimum (2)"), 3000);
        return;
    }
    // Recompute on the same selection; the current embedding stays visible
    // until the new one lands, so the arrows read as a continuous control.
    startTsne(want);
}

void ClusterView::startTsne(double perplexityOverride){
    if (tsneComputing || tsneThread) {          // second F while computing = cancel
        exitTsne(tr("t-SNE cancelled"));
        return;
    }
    const QList<int> shown = view.clusters();
    if (shown.isEmpty()) {
        if (statusBar) statusBar->showMessage(tr("t-SNE: no clusters selected"), 3000);
        return;
    }
    // ACTIVE layer.  With children selected in the child palette the document
    // points data()/clusterColors() at the child clustering and the view's
    // cluster list holds ATOM ids, so the embedding follows the user's scope
    // without asking: the same call embeds parents or children.
    Data& d = doc.data();
    tsneChildLayer = doc.isChildClusteringActive();
    // Every feature dimension except time, or the first N of them when the
    // preference caps it.  Embedding fewer, cleaner dimensions is often a
    // better answer than tuning the optimiser.
    const int allDims = d.nbOfDimensionsTotal() - 1;
    const int dimCap  = configuration().getTsneMaxDimensions();
    const int D = (dimCap > 0) ? qMin(dimCap, allDims) : allDims;
    if (D < 2) {
        if (statusBar) statusBar->showMessage(tr("t-SNE: not enough feature dimensions"), 3000);
        return;
    }
    const int cap = configuration().getTsneSpikeCap();

    // Gather rows + labels; refuse past the cap BEFORE copying features.
    QList<QPair<int, SortableTable*>> tables;   // owned below
    qint64 total = 0;
    for (int id : shown) {
        auto* t = new SortableTable();
        if (!d.spikePositions(id, *t)) { delete t; continue; }
        total += t->nbOfColumns();
        tables.append(qMakePair(id, t));
        if (total > cap) break;
    }
    // Over the cap: refuse, or draw a random subsample of cap size across the
    // whole selection.  Refusing is the safe default -- an embedding of a
    // sample is a picture of the sample -- but on a big cluster it blocks you
    // exactly when the structure is worth seeing, so the choice is the user's.
    const bool subsample = configuration().getTsneSubsampleOverCap();
    if ((total > cap && !subsample) || tables.isEmpty() || total < 8) {
        for (auto& pr : tables) delete pr.second;
        if (statusBar) statusBar->showMessage(
            total > cap
              ? tr("t-SNE refused: %1 spikes selected, cap is %2 "
                   "(raise it, or enable subsampling, in Preferences)").arg(total).arg(cap)
              : tr("t-SNE: too few spikes selected"), 5000);
        return;
    }

    // Seed first: it drives the subsample as well as the embedding, so a
    // repeatable run is repeatable end to end.
    const unsigned seed = configuration().getTsneRandomSeed()
        ? QRandomGenerator::global()->generate()
        : 42u;

    // When subsampling, decide WHICH spikes before copying any features.
    QSet<qint64> keep;
    const bool sampled = (total > cap);
    if (sampled) {
        std::vector<qint64> idx(static_cast<size_t>(total));
        std::iota(idx.begin(), idx.end(), 0);
        std::shuffle(idx.begin(), idx.end(), std::mt19937(seed));
        idx.resize(static_cast<size_t>(cap));
        for (qint64 v : idx) keep.insert(v);
    }

    const int N = sampled ? cap : static_cast<int>(total);
    auto X      = std::make_shared<std::vector<double>>(static_cast<size_t>(N) * D);
    auto labels = std::make_shared<QList<int>>();
    labels->reserve(N);
    // The lasso needs to name spikes, not just colour them: keep each embedded
    // point's 0-based .spk index (feature row - 1), the form the document's
    // explicit-spike-list primitive takes.
    auto rows = std::make_shared<QVector<int>>();
    rows->reserve(N);
    int r = 0;
    qint64 seen = 0;
    for (auto& pr : tables) {
        SortableTable& t = *pr.second;
        const dataType n = t.nbOfColumns();
        for (dataType i = 1; i <= n; ++i, ++seen) {
            if (sampled && !keep.contains(seen))
                continue;
            const dataType row = t(1, i);
            for (int dim = 1; dim <= D; ++dim)
                (*X)[static_cast<size_t>(r) * D + (dim - 1)] =
                    static_cast<double>(d.featureValue(row, dim));
            labels->append(pr.first);
            rows->append(static_cast<int>(row) - 1);
            ++r;
        }
        delete pr.second;
    }

    TsneParams params;
    params.perplexity = (perplexityOverride > 0.0)
        ? qBound(2.0, perplexityOverride, qMax(2.0, (N - 1) / 3.0))
        : qMin(configuration().getTsneStartPerplexity(), qMax(2.0, (N - 1) / 3.0));
    params.nIter    = configuration().getTsneIterations();
    params.theta    = configuration().getTsneTheta();
    params.eta      = configuration().getTsneLearningRate();
    params.exag     = configuration().getTsneExaggeration();
    params.exagIter = qMin(configuration().getTsneExaggerationIterations(), params.nIter);
    params.seed       = seed;                    // fixed unless the user asked otherwise
    const double perp = params.perplexity;
    const int nClusters = shown.size();

    tsneCancel    = false;
    tsneComputing = true;
    const int runId = ++tsneRunId;
    tsneProgressText = tr("t-SNE: starting…");
    drawContentsMode = REFRESH;
    update();
    if (sampled && statusBar)
        statusBar->showMessage(
            tr("t-SNE: %1 spikes selected, embedding a random sample of %2 (cap)…")
                .arg(total).arg(N), 6000);
    else if (statusBar) statusBar->showMessage(
        tsneChildLayer
          ? tr("t-SNE: embedding %1 spikes from %2 atom(s), perplexity %3…")
                .arg(N).arg(nClusters).arg(params.perplexity, 0, 'f', 0)
          : tr("t-SNE: embedding %1 spikes from %2 cluster(s), perplexity %3…")
                .arg(N).arg(nClusters).arg(params.perplexity, 0, 'f', 0));

    QPointer<ClusterView> guard(this);
    std::atomic<bool>* cancel = &tsneCancel;
    QThread* th = QThread::create([guard, X, labels, rows, N, D, params, perp,
                                   nClusters, cancel, runId]() {
        QElapsedTimer timer; timer.start();
        auto out = std::make_shared<std::vector<double>>();
        std::string err;
        int lastPct = -1;
        QString lastPhase;
        const bool ok = tsneEmbed2D(*X, N, D, *out, params,
            [&](const char* phase, int done, int totalIt) {
                const int pct = totalIt > 0 ? done * 100 / totalIt : 0;
                // Phase changes always report; within a phase, every ~5%.
                const QString ph = QString::fromLatin1(phase);
                if (ph != lastPhase || pct / 5 != lastPct / 5) {
                    lastPhase = ph;
                    lastPct   = pct;
                    QMetaObject::invokeMethod(guard, [guard, ph, pct, runId]() {
                        if (!guard || guard->tsneRunId != runId) return;
                        guard->tsneProgressText =
                            ClusterView::tr("t-SNE: %1 %2%…").arg(ph).arg(pct);
                        if (guard->statusBar)
                            guard->statusBar->showMessage(guard->tsneProgressText);
                        guard->drawContentsMode = REFRESH;
                        guard->update();
                    }, Qt::QueuedConnection);
                }
            }, cancel, &err);
        const qint64 ms = timer.elapsed();
        QMetaObject::invokeMethod(guard,
            [guard, ok, err = QString::fromStdString(err), out, labels, rows, N,
             nClusters, perp, ms, runId]() {
                if (guard)
                    guard->onTsneFinished(runId, ok, err, std::move(*out), *labels,
                                          *rows, N, nClusters, perp, ms);
            }, Qt::QueuedConnection);
    });
    QObject::connect(th, &QThread::finished, th, &QObject::deleteLater);
    tsneThread = th;
    th->start();
}

void ClusterView::onTsneFinished(int runId, bool ok, const QString& err,
                                 std::vector<double> xy, QList<int> labels,
                                 QVector<int> spikeRows,
                                 int nSpikes, int nClusters, double perp,
                                 qint64 ms){
    // A run abandoned by exitTsne (or superseded by a perplexity step) still
    // lands here when its thread notices the cancel; it is not this view's
    // current run and must not touch its state.
    if (runId != tsneRunId)
        return;
    tsneThread    = nullptr;    // finished; deleteLater will reap it
    tsneComputing = false;
    tsneProgressText.clear();
    if (!ok) {
        if (statusBar) statusBar->showMessage(
            err == QLatin1String("cancelled")
                ? tr("t-SNE cancelled")
                : tr("t-SNE failed: %1").arg(err), 5000);
        return;
    }
    tsneXY          = std::move(xy);
    tsneRowCluster  = std::move(labels);
    tsneRowSpike    = std::move(spikeRows);
    resetSelectionPolygon();
    {   // capture the bounding box once: paint and hit-test must agree
        const int n = static_cast<int>(tsneXY.size() / 2);
        tsneMinX = tsneMaxX = tsneMinY = tsneMaxY = 0.0;
        if (n > 0) {
            tsneMinX = tsneMaxX = tsneXY[0];
            tsneMinY = tsneMaxY = tsneXY[1];
            for (int i = 1; i < n; ++i) {
                tsneMinX = qMin(tsneMinX, tsneXY[2 * i]);
                tsneMaxX = qMax(tsneMaxX, tsneXY[2 * i]);
                tsneMinY = qMin(tsneMinY, tsneXY[2 * i + 1]);
                tsneMaxY = qMax(tsneMaxY, tsneXY[2 * i + 1]);
            }
            const double mx = (tsneMaxX - tsneMinX) * 0.05 + 1e-9;
            const double my = (tsneMaxY - tsneMinY) * 0.05 + 1e-9;
            tsneMinX -= mx; tsneMaxX += mx; tsneMinY -= my; tsneMaxY += my;
        }
    }
    tsneSpikeCount  = nSpikes;
    tsneClusterCount= nClusters;
    tsnePerplexity  = perp;
    tsneMode        = true;
    drawContentsMode = REDRAW;
    update();
    if (statusBar) statusBar->showMessage(
        (tsneChildLayer
           ? tr("t-SNE: %1 spikes, %2 atom(s), perplexity %3, %4 s — "
                "↑/↓ change perplexity, F returns")
           : tr("t-SNE: %1 spikes, %2 cluster(s), perplexity %3, %4 s — "
                "↑/↓ change perplexity, F returns"))
            .arg(nSpikes).arg(nClusters).arg(perp, 0, 'f', 0)
            .arg(ms / 1000.0, 0, 'f', 1), 8000);
}

QPoint ClusterView::tsneViewportPos(int i) const {
    const QRect vp = contentsRect();
    const double sx = vp.width()  / qMax(1e-12, tsneMaxX - tsneMinX);
    const double sy = vp.height() / qMax(1e-12, tsneMaxY - tsneMinY);
    return QPoint(vp.left() + static_cast<int>((tsneXY[2 * i]     - tsneMinX) * sx),
                  vp.top()  + static_cast<int>((tsneXY[2 * i + 1] - tsneMinY) * sy));
}

void ClusterView::tsneRelabelFromDoc(){
    // Only ever from the layer the embedding was computed on: parent ids and
    // atom ids are different namespaces, and reading the wrong one would
    // recolour every point with an unrelated cluster's colour.
    if (doc.isChildClusteringActive() != tsneChildLayer) {
        exitTsne(tr("t-SNE dropped: clustering scope changed"));
        return;
    }
    // Positions are untouched by a membership edit, so re-read the ids and
    // recolour rather than discarding a minute of computation.
    const QVector<dataType> labelByRow = doc.data().labelByFeatureRow();
    for (int i = 0; i < tsneRowSpike.size() && i < tsneRowCluster.size(); ++i) {
        const int row1 = tsneRowSpike.at(i) + 1;      // .spk index -> feature row
        if (row1 > 0 && row1 < labelByRow.size())
            tsneRowCluster[i] = static_cast<int>(labelByRow.at(row1));
    }
}

void ClusterView::resetSelectionPolygon(){
    // The scatter clears these on its REDRAW path in paintEvent; the embedding
    // paints without that double buffer, so it clears them here.  One place,
    // so a half-drawn lasso can never survive into the next gesture.
    selectionPolygon.resize(0);
    nbSelectionPoints = 0;
    polygonClosed = false;
}

void ClusterView::cancelSelectionPolygon(){
    if (selectionPolygon.isEmpty())
        return;
    resetSelectionPolygon();
    // REFRESH re-blits the double buffer without the overlay in the scatter,
    // and repaints the embedding wholesale; neither needs the clusters redrawn.
    drawContentsMode = REFRESH;
    update();
    if (statusBar) statusBar->showMessage(tr("Selection discarded"), 2000);
}

void ClusterView::closeSelectionPolygon(){
    if (selectionPolygon.size() <= 2) {
        // Fewer than three vertices is not a polygon; the scatter refreshes and
        // leaves what was drawn, so do exactly that.
        drawContentsMode = REFRESH;
        update();
        if (statusBar) statusBar->clearMessage();
        return;
    }
    //erase the last line drawn if the user moved since the last click
    eraseTheLastMovingLine();
    polygonClosed = true;

    // Queue the work rather than doing it here, so the closed polygon is on
    // screen before the document is asked to compute -- the scatter achieves
    // this by posting a ComputeEvent, and the embedding by queueing its own
    // apply.  The only difference between the two lassos.
    if (tsneMode)
        QMetaObject::invokeMethod(this, [this]{ applyTsneLasso(); },
                                  Qt::QueuedConnection);
    else
        QApplication::postEvent(this, getComputeEvent(selectionPolygon));

    drawContentsMode = REFRESH;
    update();
    if (statusBar) statusBar->clearMessage();
}

void ClusterView::applyTsneLasso(){
    if (selectionPolygon.size() < 3) {
        resetSelectionPolygon();
        return;
    }

    // The scope must still be the one the embedding was computed in: a switch
    // between the parent and child palettes swaps the id namespace under it.
    if (doc.isChildClusteringActive() != tsneChildLayer) {
        resetSelectionPolygon();
        exitTsne(tr("t-SNE dropped: clustering scope changed — press F to re-embed"));
        return;
    }

    // All four modes work on both layers now: the create modes route through
    // data() and carry their child branch, and the delete modes' child-scope
    // translation (shown atoms -> the exact rows -> the parents those rows are
    // in) lands the spikes in the parent reserve bins with the atom layer
    // re-cut behind them, which is what sending an atom's spikes to noise has
    // to mean.

    // Hit-test in viewport pixels through the SAME mapping paintTsne uses.
    const QRegion area(selectionPolygon);   // viewport pixels in this view
    QSet<dataType> rows;                    // 1-based feature rows
    const bool scopeGate = projScopeActive();   // temporally-restricted: exclude out-of-scope
    const int n = qMin(static_cast<int>(tsneXY.size() / 2), tsneRowSpike.size());
    for (int i = 0; i < n; ++i) {
        if (!area.contains(tsneViewportPos(i))) continue;
        if (scopeGate) {
            const double t = static_cast<double>(doc.data().featureValue(
                static_cast<dataType>(tsneRowSpike.at(i)) + 1, timeDimension));
            if (!spikeTimeInScope(t)) continue;     // out of scope -> not selectable
        }
        rows.insert(static_cast<dataType>(tsneRowSpike.at(i)) + 1);
    }

    if (rows.isEmpty()) {
        if (statusBar) statusBar->showMessage(tr("t-SNE lasso: no spikes inside"), 3000);
        resetSelectionPolygon();
        drawContentsMode = REFRESH;
        update();
        return;
    }

    // Sources come from the LIVE clustering, never from the embedding's cached
    // labels.  A renumber or any edit since the embedding was computed shifts
    // ids, and the builders only touch spikes that are actually in the clusters
    // they are given -- a stale id silently drops those spikes from the cut,
    // which is exactly how a lasso produced a cluster missing half its points.
    const QVector<dataType> labelByRow = doc.data().labelByFeatureRow();
    QList<int> sources;
    for (dataType r : rows) {
        if (r <= 0 || r >= labelByRow.size()) continue;
        const int cid = static_cast<int>(labelByRow.at(static_cast<int>(r)));
        if (!sources.contains(cid)) sources.append(cid);
    }
    if (sources.isEmpty()) {
        if (statusBar) statusBar->showMessage(
            tsneChildLayer
              ? tr("t-SNE lasso: the selected spikes are no longer in the embedded "
                   "atoms — press F twice to re-embed")
              : tr("t-SNE lasso: the selected spikes are no longer in the embedded "
                   "clusters — press F twice to re-embed"), 5000);
        resetSelectionPolygon();
        drawContentsMode = REFRESH;
        update();
        return;
    }

    const int nSelected = rows.size();
    resetSelectionPolygon();

    // CREATE modes defer: capture the selection, show a residual preview in the
    // waveform view, and wait for the curator to confirm (Enter) or cancel (Esc).
    // DELETE modes (to a reserve bin) apply at once, as before — there is no
    // prospective cluster to inspect.
    if (mode == NEW_CLUSTER || mode == NEW_CLUSTERS) {
        clearPendingLasso();                 // supersede any earlier pending preview
        pendingRows_    = rows;
        pendingSources_ = sources;
        pendingMode_    = static_cast<int>(mode);
        pendingNSel_    = nSelected;
        pendingLasso_   = true;
        showLassoResidualPreview();
        drawContentsMode = REDRAW;            // repaint the embedding without the polygon
        update();
        if (statusBar) statusBar->showMessage(
            tr("t-SNE lasso: %1 spikes — Enter to apply, Esc to cancel "
               "(residual shown in the waveform view)").arg(nSelected), 0);
        return;
    }

    applyLassoSelection(rows, sources, static_cast<int>(mode), nSelected);
}

// ---------------------------------------------------------------------------
// applyLassoSelection / showLassoResidualPreview / confirm / cancel — the
// deferred-lasso path.  A closed create-mode embedding lasso shows the residual
// of the would-be cut against the pinned oblique basis (or the lassoed spikes'
// own mean) in the waveform view; the cut lands only once the curator confirms.
// ---------------------------------------------------------------------------
void ClusterView::applyLassoSelection(const QSet<dataType>& rows, const QList<int>& sources,
                                      int lassoMode, int nSelected){
    // The shared apply path: the SAME builders the scatter's polygon uses, with
    // the selection named by row (colour registration, the creation notice every
    // view and the palette need, the create-flavoured undo entry and the
    // curation-log detail all come with them — not the move primitive, which
    // would land spikes in an id no view/colour knew about).
    const SpikeSelection selection(rows);
    tsneApplyingLasso = true;               // our own edit: do not self-drop
    switch (static_cast<BaseFrame::Mode>(lassoMode)) {
    case DELETE_ARTEFACT: doc.deleteArtifact(selection, sources);    break;
    case DELETE_NOISE:    doc.deleteNoise(selection, sources);       break;
    case NEW_CLUSTER:     doc.createNewCluster(selection, sources);  break;
    case NEW_CLUSTERS:    doc.createNewClusters(selection, sources); break;
    default:              break;
    }
    tsneApplyingLasso = false;

    // Positions are untouched by a membership edit, so recolour in place.
    tsneRelabelFromDoc();
    drawContentsMode = REDRAW;
    update();

    if (statusBar) statusBar->showMessage(
        tsneChildLayer
          ? tr("t-SNE lasso: %1 spikes from %2 atom(s) applied")
                .arg(nSelected).arg(sources.size())
          : tr("t-SNE lasso: %1 spikes from %2 cluster(s) applied")
                .arg(nSelected).arg(sources.size()), 6000);
}

void ClusterView::showLassoResidualPreview(){
    // Basis = the pinned oblique basis if set; empty => residual to own mean.
    const KlustersDoc::ResidualPreview p = doc.computeResidualPreview(pendingRows_, obliqueBasis);
    for (ViewWidget* w : view.getViewList())
        if (WaveformView* wv = qobject_cast<WaveformView*>(w))
            wv->setResidualPreview(p.nChan, p.nSamp, p.meanWave, p.fit, p.resid, p.verdict);
}

void ClusterView::clearPendingLasso(){
    pendingLasso_ = false;
    pendingRows_.clear();
    pendingSources_.clear();
    pendingMode_ = -1;
    pendingNSel_ = 0;
    for (ViewWidget* w : view.getViewList())
        if (WaveformView* wv = qobject_cast<WaveformView*>(w))
            wv->clearResidualPreview();
}

void ClusterView::confirmPendingLasso(){
    if (!pendingLasso_) return;
    const QSet<dataType> rows    = pendingRows_;
    const QList<int>     sources = pendingSources_;
    const int            m       = pendingMode_;
    const int            nSel    = pendingNSel_;
    clearPendingLasso();                     // also clears the waveform preview
    applyLassoSelection(rows, sources, m, nSel);
}

void ClusterView::cancelPendingLasso(){
    if (!pendingLasso_) return;
    clearPendingLasso();
    drawContentsMode = REDRAW;
    update();
    if (statusBar) statusBar->showMessage(tr("t-SNE lasso cancelled"), 3000);
}

void ClusterView::paintTsneProgress(QPainter& painter){
    if (tsneProgressText.isEmpty())
        return;
    const QRect vp = contentsRect();
    QFontMetrics fm(painter.font());
    const QString text = tsneProgressText + tr("   (F cancels)");
    const int w = fm.horizontalAdvance(text) + 24;
    const int h = fm.height() + 12;
    const QRect box(vp.left() + (vp.width() - w) / 2, vp.top() + 12, w, h);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 180));
    painter.drawRect(box);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(box, Qt::AlignCenter, text);
}

bool ClusterView::tsneEmbeddingPoints(QVector<double>& xs, QVector<double>& ys,
                                      QVector<int>& spikeRows) const
{
    if (!tsneMode) return false;
    const int n = qMin(static_cast<int>(tsneXY.size() / 2), tsneRowSpike.size());
    if (n <= 0) return false;
    xs.resize(n); ys.resize(n); spikeRows.resize(n);
    for (int i = 0; i < n; ++i) {
        xs[i]        = tsneXY[2 * i];
        ys[i]        = tsneXY[2 * i + 1];
        spikeRows[i] = tsneRowSpike.at(i);
    }
    return true;
}

QPoint ClusterView::tsneViewportPosFor(double x, double y) const
{
    const QRect vp = contentsRect();
    const double sx = vp.width()  / qMax(1e-12, tsneMaxX - tsneMinX);
    const double sy = vp.height() / qMax(1e-12, tsneMaxY - tsneMinY);
    return QPoint(vp.left() + static_cast<int>((x - tsneMinX) * sx),
                  vp.top()  + static_cast<int>((y - tsneMinY) * sy));
}

void ClusterView::paintWatershedOverlayEmbedded(QPainter& p)
{
    if (wsImage.isNull()) return;
    // The scatter's version stretches the image into a WORLD rect and relies on
    // that window's negated Y.  The embedding has no world and no negation: its
    // points go through tsneViewportPos, so the overlay goes through the same
    // mapping, which is the only way the basins can land on the blobs they were
    // computed from.
    const QPoint tl = tsneViewportPosFor(wsXMin, wsYMin);
    const QPoint br = tsneViewportPosFor(wsXMax, wsYMax);
    const QRectF tgt(QPointF(qMin(tl.x(), br.x()), qMin(tl.y(), br.y())),
                     QPointF(qMax(tl.x(), br.x()), qMax(tl.y(), br.y())));
    const bool prevSmooth = p.testRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(tgt, wsImage.mirrored(false, true));   // image row 0 is max-Y
    p.setRenderHint(QPainter::SmoothPixmapTransform, prevSmooth);

    if (!wsHud.isEmpty()) {
        const QRect vp = contentsRect();
        QFontMetrics fm(p.font());
        const int w = fm.horizontalAdvance(wsHud) + 20;
        const QRect box(vp.left() + 8, vp.bottom() - fm.height() - 20,
                        w, fm.height() + 10);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 180));
        p.drawRect(box);
        p.setPen(QColor(255, 255, 255));
        p.drawText(box, Qt::AlignCenter, wsHud);
    }
}

void ClusterView::paintTsne(QPainter& painter){
    const QRect vp = contentsRect();
    painter.fillRect(vp, palette().color(QPalette::Window));
    const int N = static_cast<int>(tsneXY.size() / 2);
    if (N == 0) return;
    ItemColors& colors = doc.clusterColors();
    painter.setPen(Qt::NoPen);
    const int r = qMax(1, pointSize);
    const QColor unknown(160, 160, 160);
    // Same temporally-restricted gate as the scatter: grey (or hide) embedded
    // points whose spike time falls outside the pinned basis classes' scope.
    const bool gate    = projScopeActive();
    const bool hideOOS = gate && configuration().getProjectionOutOfScopeHidden();
    const QColor greyOOS(110, 110, 110);
    for (int i = 0; i < N; ++i) {
        const int id = tsneRowCluster.at(i);
        bool oos = false;
        if (gate && i < tsneRowSpike.size()) {
            const double t = static_cast<double>(doc.data().featureValue(
                static_cast<dataType>(tsneRowSpike.at(i) + 1), timeDimension));
            oos = !spikeTimeInScope(t);
            if (oos && hideOOS) continue;
        }
        // A lasso can send spikes to a cluster the palette has not coloured
        // yet; fall back rather than asking ItemColors for a missing id.
        painter.setBrush(oos ? greyOOS
                             : (colors.contains(id) ? colors.color(id) : unknown));
        const QPoint p = tsneViewportPos(i);
        painter.drawEllipse(p.x() - r, p.y() - r, 2 * r, 2 * r);
    }
    // EAP overlays: amber ring on collision points, cyan ring on the highlighted
    // class's members (a point can carry both).
    const bool ovColl = configuration().getShowEapCollisions()   && !eapCollision.empty();
    const bool ovMem  = configuration().getShowEapClassMembers() && highlightClassCol >= 0
                        && !eapHighlightMember.empty();
    if (ovColl || ovMem) {
        painter.setBrush(Qt::NoBrush);
        QPen collPen(QColor(255, 210, 0)); collPen.setWidth(1);
        QPen memPen (QColor(0, 200, 255)); memPen.setWidth(1);
        const int rc = r + 2, rm = r + 4;
        for (int i = 0; i < N && i < tsneRowSpike.size(); ++i) {
            const long s0  = static_cast<long>(tsneRowSpike.at(i));
            const bool coll = ovColl && spikeIsCollision(s0);
            const bool mem  = ovMem  && spikeIsHighlightMember(s0);
            if (!coll && !mem) continue;
            if (hideOOS) {                               // don't ring a hidden out-of-scope spike
                const double t = static_cast<double>(doc.data().featureValue(
                    static_cast<dataType>(s0) + 1, timeDimension));
                if (!spikeTimeInScope(t)) continue;
            }
            const QPoint p = tsneViewportPos(i);
            if (mem)  { painter.setPen(memPen);  painter.drawEllipse(p.x() - rm, p.y() - rm, 2*rm, 2*rm); }
            if (coll) { painter.setPen(collPen); painter.drawEllipse(p.x() - rc, p.y() - rc, 2*rc, 2*rc); }
        }
        painter.setPen(Qt::NoPen);
    }
    if (!selectionPolygon.isEmpty()) {
        // Same overlay as the scatter: the mode's colour and the configured
        // line width, so the polygon tells the curator which action is armed.
        // The tracking vertex is already the polygon's last point, exactly as
        // in the scatter, so no separate rubber line is drawn.
        painter.setBrush(Qt::NoBrush);
        QPen selPen(selectPolygonColor(mode));
        // The embedding draws in viewport pixels, so this pen is already in
        // device units -- cosmetic anyway, stated so the two overlays read the
        // same and neither drifts if its transform changes.
        selPen.setCosmetic(true);
        selPen.setWidth(selectionLineWidth);
        painter.setPen(selPen);
        painter.drawPolyline(selectionPolygon);
    }
    painter.setPen(palette().color(QPalette::WindowText));
    if (obliqueMode) {
        // The condition number is the headline: it tells the curator when the
        // selected templates are genuinely separable (~1-3) versus fusing (large),
        // which is the whole point of looking at a near-collinear pair this way.
        painter.drawText(vp.left() + 8, vp.top() + 18,
            (tsneChildLayer
               ? tr("oblique  —  %1 spikes, %2 atom template(s), condition %3   "
                    "(Shift+O returns to features)")
               : tr("oblique  —  %1 spikes, %2 template(s), condition %3   "
                    "(Shift+O returns to features)"))
                .arg(tsneSpikeCount).arg(tsneClusterCount).arg(obliqueCond, 0, 'f', 1));
    } else {
        painter.drawText(vp.left() + 8, vp.top() + 18,
            (tsneChildLayer
               ? tr("t-SNE  —  %1 spikes, %2 atom(s) of the child layer, perplexity %3   "
                    "(↑/↓ perplexity — F returns to features)")
               : tr("t-SNE  —  %1 spikes, %2 cluster(s), perplexity %3   "
                    "(↑/↓ perplexity — F returns to features)"))
                .arg(tsneSpikeCount).arg(tsneClusterCount).arg(tsnePerplexity, 0, 'f', 0));
    }
}

void ClusterView::drawClusters(QPainter& painter,const QList<int>& clustersList,bool drawCircles){
    //Loop on the clusters to be drawn
    QList<int>::const_iterator clusterIterator;

    ItemColors& clusterColors = doc.clusterColors();
    Data& clusteringData = doc.data();

    // Markers are drawn in VIEWPORT (pixel) space so their size is independent of
    // the axis range.  We reset the world-to-viewport transform, map each spike
    // point ourselves via worldToViewport(), and draw with fixed pixel dimensions.
    const QTransform savedTransform = painter.transform();
    painter.resetTransform();
    const int r = pointSize;           // pixel radius

    // Temporally-restricted projection: out-of-scope spikes (time not in the pinned
    // basis classes' .wti coverage) are greyed, or hidden if the pref says so.  The
    // gate is off (fast path) unless restricted mode is on AND a scope is pinned.
    const bool gate   = projScopeActive();
    const bool hideOOS = gate && configuration().getProjectionOutOfScopeHidden();
    const QColor greyOOS(110, 110, 110);

    for (int clustId : clustersList) {
        const QColor clusterColor = clusterColors.color(clustId);
        painter.setPen(clusterColor);
        //Get the iterator on the spikes of the current cluster
        Data::Iterator spikeIterator = clusteringData.iterator(static_cast<dataType>(clustId));
        //Iterate over the spikes of the cluster and draw them
        if(drawCircles)  {
            painter.setBrush(clusterColor);
            painter.setPen(Qt::NoPen);
            for(;spikeIterator.hasNext();spikeIterator.next())
            {
                if (gate) {
                    const bool oos = !spikeTimeInScope(
                        static_cast<double>(spikeIterator(static_cast<dataType>(timeDimension))));
                    if (oos && hideOOS) continue;
                    painter.setBrush(oos ? greyOOS : clusterColor);
                }
                QPoint px = worldToViewport(spikeIterator(dimensionX,dimensionY));
                painter.drawEllipse(px.x() - r, px.y() - r, r*2, r*2);
            }
        }
        else  {
            QPen pen(clusterColor);
            pen.setWidth(r > 1 ? r : 1);
            QPen greyPen(greyOOS);
            greyPen.setWidth(r > 1 ? r : 1);
            painter.setPen(pen);
            for(;spikeIterator.hasNext();spikeIterator.next()){
                if (gate) {
                    const bool oos = !spikeTimeInScope(
                        static_cast<double>(spikeIterator(static_cast<dataType>(timeDimension))));
                    if (oos && hideOOS) continue;
                    painter.setPen(oos ? greyPen : pen);
                }
                QPoint px = worldToViewport(spikeIterator(dimensionX,dimensionY));
                painter.drawPoint(px);
            }
        }
    }

    // EAP overlays, in a second pass so the hot point loop stays untouched when
    // off: an amber ring on collision spikes (>= 2 .eap classes) and a cyan ring
    // on the highlighted class's members.  A spike can carry both (nested rings).
    const bool ovColl = configuration().getShowEapCollisions()   && !eapCollision.empty();
    const bool ovMem  = configuration().getShowEapClassMembers() && highlightClassCol >= 0
                        && !eapHighlightMember.empty();
    if (ovColl || ovMem) {
        painter.setBrush(Qt::NoBrush);
        QPen collPen(QColor(255, 210, 0)); collPen.setWidth(1);   // amber: collision
        QPen memPen (QColor(0, 200, 255)); memPen.setWidth(1);    // cyan: highlighted-class member
        const int rc = r + 2, rm = r + 4;
        for (int clustId : clustersList) {
            Data::Iterator it = clusteringData.iterator(static_cast<dataType>(clustId));
            for (; it.hasNext(); it.next()) {
                const long s0  = static_cast<long>(it.featureRow()) - 1;
                const bool coll = ovColl && spikeIsCollision(s0);
                const bool mem  = ovMem  && spikeIsHighlightMember(s0);
                if (!coll && !mem) continue;
                if (hideOOS && !spikeTimeInScope(            // don't ring a hidden out-of-scope spike
                        static_cast<double>(it(static_cast<dataType>(timeDimension))))) continue;
                const QPoint px = worldToViewport(it(dimensionX,dimensionY));
                if (mem)  { painter.setPen(memPen);  painter.drawEllipse(px.x() - rm, px.y() - rm, rm*2, rm*2); }
                if (coll) { painter.setPen(collPen); painter.drawEllipse(px.x() - rc, px.y() - rc, rc*2, rc*2); }
            }
        }
    }

    painter.setTransform(savedTransform);
    painter.setBrush(Qt::NoBrush);
}

void ClusterView::paintEvent ( QPaintEvent*){
    QPainter p(this);

    // Alternate presentation: the 2-D embedding replaces the scatter wholesale
    // (no world window, no axes, no time HUD -- embedding space is its own).
    if (tsneMode) {
        paintTsne(p);
        if (!wsImage.isNull())      // watershed preview, in embedding space
            paintWatershedOverlayEmbedded(p);
        if (tsneComputing)          // a re-embed at a new perplexity
            paintTsneProgress(p);
        drawContentsMode = REFRESH;
        return;
    }

    // If autoscale is enabled, refit bounds to the current shownClusters
    // projection before sampling `window` below.  Done only for the
    // REDRAW path: UPDATE is an incremental paint of just-changed
    // clusters and must preserve the existing window to avoid jittering
    // the plot on every cluster-set tweak.
    if (autoscaleEnabled && drawContentsMode == REDRAW) {
        autoscaleToVisibleClusters();
    }

    //set the window (part of the word I want to show)
    QRect r((QRect)window);
    if(drawContentsMode == UPDATE || drawContentsMode == REDRAW){
        viewport = contentsRect();
        //Resize the double buffer with the width and the height of the widget(QFrame)
        if (viewport.size() != doublebuffer.size()) {
            if(!doublebuffer.isNull()) {
                QPixmap tmp = QPixmap( viewport.width(),viewport.height() );
                tmp.fill( Qt::white );
                QPainter painter2( &tmp );
                painter2.drawPixmap( 0,0, doublebuffer );
                painter2.end();
                doublebuffer = tmp;
            } else {
                doublebuffer = QPixmap(viewport.width(),viewport.height());
            }
        }

        //Create a painter to paint on the double buffer
        QPainter painter;
        painter.begin(&doublebuffer);

        painter.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function

        if(drawContentsMode == REDRAW){
            //Reset the variables associates with the polygon

            //Resize selectionPolygon to remove all the last selected area, reinitialize nbSelectionPoints accordingly
            selectionPolygon.resize(0);
            nbSelectionPoints = 0;

            //Fill the double buffer with the background

            doublebuffer.fill(palette().color(backgroundRole()));

            //Draw the axes
            drawAxes(painter);

            //Paint all the clusters in the shownClusters list (in the double buffer)
            drawClusters(painter,view.clusters());
        } else if(drawContentsMode == UPDATE){

            //Erase any polygon of selection and reset the associated variables

            //Paint the the clusters to update contain in clusterUpdateList
            if(!clusterUpdateList.isEmpty())
                drawClusters(painter,clusterUpdateList);

            //Clear the update list
            clusterUpdateList.clear();
        }

        //reset transformation due to setWindow
        painter.resetTransform() ;


        //Draw the time axis information if the time is displayed
        drawTimeInformation(painter);

        //Closes the painter on the double buffer
        painter.end();

        //Back to the default
        drawContentsMode = REFRESH;
    }
    //if drawContentsMode == REFRESH, we reuse the double buffer (pixmap)

    //Draw the double buffer (pixmap) by copying it into the paint device.
    p.drawPixmap(0, 0, doublebuffer);



    // Computing over the scatter: the first embedding of a selection runs with
    // the feature view still showing, and without this the only sign that F did
    // anything is a status line that scrolls away.
    if (tsneComputing)
        paintTsneProgress(p);

    if(!selectionPolygon.isEmpty()) {
        const QColor color = selectPolygonColor(mode);
        p.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function
        QPen selPen(color);
        // COSMETIC: setWindow above makes the painter's units WORLD units, so a
        // plain pen width is a width in feature space -- it scales with the zoom
        // instead of staying the thickness the preference asks for.  Zoomed out
        // over a session-wide window that is a small fraction of a pixel and the
        // lasso all but disappears; zoomed into a cluster the same pen draws a
        // fat band.  A cosmetic pen is measured in device pixels whatever the
        // transform, which is what a UI overlay wants.
        selPen.setCosmetic(true);
        selPen.setWidth(selectionLineWidth);
        p.setPen(selPen);
        p.drawPolyline(selectionPolygon);
    }

    // Watershed overlay (Shift+W preview mode).  Drawn last so it sits on
    // top of points and any selection polygon.  Both the overlay image
    // and the HUD text are repainted from scratch every paintEvent —
    // never cached into the doublebuffer — so KlustersApp can re-tune
    // sigma / threshold without forcing a full cluster redraw.
    if (!wsImage.isNull())
        paintWatershedOverlay(p, r);

    // DipSplit post-commit HUD (Shift+D confirm window).  Just a text
    // box at top-left — no scatter overlay, since the split has already
    // happened and the new clusters are visible via normal rendering.
    if (!dsHud.isEmpty())
        paintDipsplitPostCommitHud(p);
}

void ClusterView::setWatershedOverlay(const QImage& img,
                                       double xMin, double xMax,
                                       double yMin, double yMax,
                                       const QString& hud)
{
    wsImage = img;
    wsXMin = xMin; wsXMax = xMax;
    wsYMin = yMin; wsYMax = yMax;
    wsHud  = hud;
    // No drawContentsMode change — overlay is drawn over the existing
    // doublebuffer in REFRESH mode, which is what update() schedules.
    update();
}

void ClusterView::clearWatershedOverlay()
{
    if (wsImage.isNull() && wsHud.isEmpty()) return;
    wsImage = QImage();
    wsHud.clear();
    update();
}

void ClusterView::paintWatershedOverlay(QPainter& p, const QRect& worldRect)
{
    // ── Stretch the basin-coloured image into the watershed grid's world
    // ── rect.  The image rows correspond to feature-Y in the standard
    // ── orientation (small-Y at the bottom, large-Y at the top), but
    // ── ClusterView's draw window has its Y negated (so larger feature-Y
    // ── maps to smaller world-Y, i.e. visual top).  We therefore source
    // ── the image with a vertical flip via QPainter's automatic
    // ── source-rect interpretation:  pass a target rect with top = -yMax
    // ── and bottom = -yMin, which makes image row 0 land at -yMax (top).
    // ── KlustersApp built the image so that row 0 already represents the
    // ── largest feature-Y (the build flips during setPixel).
    p.save();
    p.setWindow(worldRect.left(), worldRect.top(),
                worldRect.width()-1, worldRect.height()-1);

    const QRectF tgt(QPointF(wsXMin, -wsYMax),
                     QPointF(wsXMax, -wsYMin));

    // QPainter::drawImage with a target rect performs both translation
    // and scaling.  Use SmoothPixmapTransform off — the basin colours
    // are flat fills, so nearest-neighbour gives sharp boundaries.
    const bool prevSmooth = p.testRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    p.drawImage(tgt, wsImage);
    p.setRenderHint(QPainter::SmoothPixmapTransform, prevSmooth);

    p.restore();

    // ── HUD: drawn in viewport pixels, top-left, with a translucent
    // ── dark background for legibility against arbitrary scatter
    // ── backgrounds.  The painter's transform was already restored above.
    if (!wsHud.isEmpty()) {
        QFont f = p.font();
        f.setPointSize(10);
        p.setFont(f);
        const QFontMetrics fm(p.font());
        const QRect textBounds = fm.boundingRect(
            QRect(0, 0, 600, 200),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
            wsHud);
        const QRect bg = textBounds.translated(8, 8).adjusted(-6, -3, 8, 3);
        p.fillRect(bg, QColor(0, 0, 0, 180));
        p.setPen(QColor(255, 255, 255));
        p.drawText(QRect(8, 8, 600, 200),
                   Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                   wsHud);
    }
}

void ClusterView::setDipsplitPostCommitHud(const QString& hud)
{
    if (hud.isEmpty()) {
        clearDipsplitPostCommitHud();
        return;
    }
    dsHud = hud;
    // Plain update() — the HUD draws on top of the existing doublebuffer
    // contents in REFRESH mode, no buffer rebuild needed.  Same pattern
    // as setWatershedOverlay.
    update();
}

void ClusterView::clearDipsplitPostCommitHud()
{
    if (dsHud.isEmpty()) return;
    dsHud.clear();
    update();
}

void ClusterView::paintDipsplitPostCommitHud(QPainter& p)
{
    // Top-left text block in viewport pixels — same style as the
    // watershed HUD: dark translucent background, white text.  No
    // scatter overlay; the post-commit state is the actual cluster
    // configuration, drawn through normal rendering paths.
    QFont f = p.font();
    f.setPointSize(10);
    p.setFont(f);
    const QFontMetrics fm(p.font());
    const QRect textBounds = fm.boundingRect(
        QRect(0, 0, 600, 200),
        Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
        dsHud);
    const QRect bg = textBounds.translated(8, 8).adjusted(-6, -3, 8, 3);
    p.fillRect(bg, QColor(0, 0, 0, 180));
    p.setPen(QColor(255, 255, 255));
    p.drawText(QRect(8, 8, 600, 200),
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               dsHud);
}

void ClusterView::eraseTheLastDrawnLine()
{
    //The user did not move since the last left click (no mouseMoveEvent)
    if(nbSelectionPoints == selectionPolygon.size()){
        //Treat the case when we reach the first point of the selection
        if(nbSelectionPoints == 1){
            //Resize selectionPolygon to remove the point from selectionPolygon
            selectionPolygon.resize(0);
            nbSelectionPoints = 0;
        } else {
            //Resize selectionPolygon to remove the last point from selectionPolygon
            selectionPolygon.resize(selectionPolygon.size()-1);
            nbSelectionPoints = selectionPolygon.size();
        }
    }
    //The user moved since the last left click, a line has been drawn in the mouseMoveEvent
    else{
        //Resize selectionPolygon to remove the 2 last points
        //(the last selected and the one set in mouseMoveEvent) from selectionPolygon
        selectionPolygon.resize(selectionPolygon.size()-2);

        nbSelectionPoints = selectionPolygon.size();
    }
}

void ClusterView::eraseTheLastMovingLine()
{
    //The user moved since the last left click, a line has been drawn in the mouseMoveEvent
    if(nbSelectionPoints != selectionPolygon.size()){
        selectionPolygon.resize(selectionPolygon.size()-1);
        nbSelectionPoints = selectionPolygon.size();
    }
}

void ClusterView::addClusterToUpdate(int clusterId){
    //Add the the cluster id to the clusterUpdateList,
    // so it will be updated during the next update
    if(drawContentsMode == REFRESH){
        clusterUpdateList.append(clusterId);
        drawContentsMode = UPDATE;
    }
    else if(drawContentsMode == UPDATE)
        clusterUpdateList.append(clusterId);
}


void ClusterView::worldBoundsFromExtrema(long& aMin, long& aMax,
                                          long& oMin, long& oMax) const {
    Data& clusteringData = doc.data();
    long maxForDimensionX = static_cast<long>(clusteringData.maxDimension(dimensionX));
    long minForDimensionX = static_cast<long>(clusteringData.minDimension(dimensionX));
    long maxForDimensionY = static_cast<long>(clusteringData.maxDimension(dimensionY));
    long minForDimensionY = static_cast<long>(clusteringData.minDimension(dimensionY));

    //The min and max are chosen in a maner that the axis are always visible and superior
    //to -40000000 (due to a Qt limitation in the big negative values).
    long width = maxForDimensionX - minForDimensionX;
    aMin = static_cast<long>(qMin(0L,minForDimensionX)-width*0.05);
    aMin = static_cast<long>(qMax(aMin,-1000000L)); // below this limit, Qt crashes
    aMax = static_cast<long>(qMax(0L,maxForDimensionX)+width*0.05);

    long height = maxForDimensionY - minForDimensionY;
    oMin = static_cast<long>(-qMax(0L,maxForDimensionY)-height*0.05);
    oMax = static_cast<long>(-qMin(0L,minForDimensionY)+height*0.05);
    oMax = static_cast<long>(qMin(oMax,1000000L)); // below -(this limit), Qt crashes
}

void ClusterView::updatedDimensions(int dimensionX, int dimensionY){
    this->dimensionX = dimensionX;
    this->dimensionY = dimensionY;

    worldBoundsFromExtrema(abscissaMin, abscissaMax, ordinateMin, ordinateMax);

    //Update the window in a maner to always see the axis
    window = ZoomWindow(QRect(QPoint(abscissaMin,ordinateMin),QPoint(abscissaMax,ordinateMax)));

    drawContentsMode = REDRAW;

    //reset the information on the polygon to enable a mousetrack in mousemovEvent
    polygonClosed = false;
}

bool ClusterView::recomputeWorldBounds(){
    long aMin = 0, aMax = 0, oMin = 0, oMax = 0;
    worldBoundsFromExtrema(aMin, aMax, oMin, oMax);
    if (aMin == abscissaMin && aMax == abscissaMax
            && oMin == ordinateMin && oMax == ordinateMax)
        return false;   // extrema recompute left the world where it was

    // Capture the old state BEFORE overwriting: a window that differs from
    // the old world is the user's deliberate zoom and must survive.
    const QRect oldWorld(QPoint(abscissaMin, ordinateMin),
                         QPoint(abscissaMax, ordinateMax));
    const QRect current = static_cast<QRect>(window);
    const bool  zoomed  = (current != oldWorld);

    abscissaMin = aMin; abscissaMax = aMax;
    ordinateMin = oMin; ordinateMax = oMax;
    window = ZoomWindow(QRect(QPoint(abscissaMin, ordinateMin),
                              QPoint(abscissaMax, ordinateMax)));
    if (zoomed)
        // Re-apply the user's window, clamped into the new world by
        // ZoomWindow's own correction.  If the zoom is refused (scale
        // limits), the view falls back to the full new world -- everything
        // visible, nothing clipped.
        window.zoom(current.topLeft(), current.bottomRight());
    return true;
}

void ClusterView::dimensionExtremaChanged(){
    tsneDropIfActive();   // extrema moved => features moved => embedding stale
    // Common case: the recompute confirmed the old bounds; skip the repaint.
    if (!recomputeWorldBounds())
        return;
    drawContentsMode = REDRAW;
    update();
}

void ClusterView::clusterFeaturesReprojected(int /*clusterId*/){
    tsneDropIfActive();   // features rewrote under the embedding
    // The reprojection can WIDEN the dimension extrema (the Data side widens
    // them synchronously on the realign path); redrawing inside the old
    // world clips the shifted points, so the Data fix is invisible without
    // this one.  With autoscale enabled paintEvent refits anyway and simply
    // overwrites this.
    recomputeWorldBounds();
    drawContentsMode = REDRAW;
    update();
}

void ClusterView::setMode(BaseFrame::Mode selectedMode){
    statusBar->clearMessage();
    selectionPolygon.clear();
    nbSelectionPoints = 0;
    mode = selectedMode;

    //set the cursor according to the selected mode.
    switch(mode){
    case DELETE_NOISE:
        setCursor(deleteNoiseCursor);
        break;
    case DELETE_ARTEFACT:
        setCursor(deleteArtefactCursor);
        break;
    case NEW_CLUSTER:
        setCursor(newClusterCursor);
        break;
    case NEW_CLUSTERS:
        setCursor(newClustersCursor);
        break;
    case ZOOM:
        setCursor(zoomCursor);
        break;
    case SELECT_TIME:
        setCursor(selectTimeCursor);
        break;
    }
    drawContentsMode = REFRESH;
    update();
}


void ClusterView::mousePressEvent(QMouseEvent* e){
    if (tsneMode) {
        // The embedding's lasso is the SCATTER's lasso: the selection-mode
        // block at the end of this handler runs for both, and the two differ
        // only in selectionPoint()'s coordinate space and in what
        // closeSelectionPolygon() does with the result.  A second gesture
        // implementation here is what made the two feel different -- and it
        // drifted immediately (no tracking vertex, its own undo arithmetic,
        // its own pen).
        if (mode != DELETE_NOISE && mode != DELETE_ARTEFACT &&
            mode != NEW_CLUSTER  && mode != NEW_CLUSTERS) {
            // Zoom / time modes have no meaning here: the axes are not features.
            if (statusBar) statusBar->showMessage(
                tr("t-SNE view: use Ctrl+1 / Ctrl+2 / Delete / Shift+Delete to lasso, "
                   "F to return"), 3000);
            return;
        }
        // fall through to the shared polygon block below
    }

    // ── Scatter-only preamble: pan, time picking, rubber-band zoom ────────
    // All three speak feature-world coordinates, so none of them applies to
    // the embedding.
    if (!tsneMode) {
    // Ctrl+Left arms a pan and takes precedence over every selection / zoom mode
    // (it is a navigation gesture).  Don't forward to the base, so no rubber-band
    // is started.
    if((e->button() == Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        ctrlPanArmed       = true;
        ctrlPanning        = false;
        ctrlPanAnchorPx    = e->position().toPoint();
        const QPoint w     = viewportToWorld(ctrlPanAnchorPx.x(), ctrlPanAnchorPx.y());
        ctrlPanPressWorldX = w.x();
        ctrlPanPressWorldY = w.y();
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    //Defining a time window t oupdate the Traceview
    if(mode == SELECT_TIME){
        QPoint current = viewportToWorld(e->position().toPoint().x(),e->position().toPoint().y());
        if(dimensionX == timeDimension){
            dataType time = static_cast<dataType>(current.x() * samplingInterval / 1000.0);
            emit moveToTime(time);
        }
        else if(dimensionY == timeDimension){
            dataType time = -static_cast<dataType>(current.y() * samplingInterval / 1000.0);
            emit moveToTime(time);
        }
    }

    //The parent implementation takes care of the mode ZOOM
    //(rubber band and calculation of the firstClick)
    ViewWidget::mousePressEvent(e);
    }   // end scatter-only preamble

    //If there is a polygon to draw (one of the selection modes)
    if(mode == DELETE_NOISE || mode == DELETE_ARTEFACT || mode == NEW_CLUSTER || mode == NEW_CLUSTERS){
        //Erase the last line drawn
        if(e->button() == Qt::RightButton){
            if(selectionPolygon.isEmpty())
                return;

            //Erase the last drawn line by drawing into the buffer
            eraseTheLastDrawnLine();
            drawContentsMode = REFRESH;
            update();
        }

        //Close the polygon of selection and trigger the right action depending on the mode
        if(e->button() == Qt::MiddleButton && !selectionPolygon.isEmpty()){
            closeSelectionPolygon();
        }

        if (e->button() == Qt::LeftButton){
            // Ensure this widget has keyboard focus so Enter/Return keyPressEvent
            // is delivered here and not consumed by a parent widget or dialog.
            setFocus(Qt::MouseFocusReason);
            QPoint selectedPoint = selectionPoint(e->position().toPoint());

            if(nbSelectionPoints == 0)
                selectionPolygon.putPoints(0, 1, selectedPoint.x(),selectedPoint.y());
            //If the array is not empty, the last point has been put into the array in mouseMoveEvent
            nbSelectionPoints = selectionPolygon.size();
            drawContentsMode = REFRESH;
            update();
        }
    }
}

void ClusterView::mouseReleaseEvent(QMouseEvent* event){
    if (tsneMode) return;
    // End a Ctrl+drag pan.  Ctrl+Left is fully owned by the pan gesture (its
    // press was intercepted, so the base never started a rubber-band / click-zoom)
    // — consume the release whether or not a drag actually occurred.
    if(ctrlPanArmed){
        ctrlPanArmed = false;
        ctrlPanning  = false;
        unsetCursor();
        event->accept();
        return;
    }
    //Trigger parent event
    ViewWidget::mouseReleaseEvent(event);
    statusBar->clearMessage();
}

// Ctrl + wheel zooms toward the cursor, keeping the world point under the
// pointer fixed (factor>1 enlarges, factor<1 zooms out).  ZoomWindow::zoom
// re-centres on the point it is given, so pass an adjusted centre that leaves the
// cursor's world point at the same screen fraction instead of jumping it to the
// middle.  Without Ctrl the event is handed to the base so existing wheel
// behaviour is unchanged.
void ClusterView::wheelEvent(QWheelEvent* e){
    if (tsneMode) return;
    if(!(e->modifiers() & Qt::ControlModifier)){
        ViewWidget::wheelEvent(e);
        return;
    }
    const int delta = e->angleDelta().y();
    if(delta == 0){ e->accept(); return; }
    const float  factor = (delta > 0) ? ctrlWheelZoomStep : (1.0f / ctrlWheelZoomStep);
    const QPoint p  = viewportToWorld(e->position().toPoint().x(),
                                      e->position().toPoint().y());
    const QRect  wr = (QRect)window;
    const double W = wr.width(), H = wr.height();
    if(W > 0.0 && H > 0.0){
        // Cursor's fraction within the current window == its screen fraction.
        const double fx = (static_cast<double>(p.x()) - wr.left()) / W;
        const double fy = (static_cast<double>(p.y()) - wr.top())  / H;
        // New window size; centre that keeps the cursor point at the same fraction.
        const double Wn = W / static_cast<double>(factor);
        const double Hn = H / static_cast<double>(factor);
        const double cx = static_cast<double>(p.x()) + Wn * (0.5 - fx);
        const double cy = static_cast<double>(p.y()) + Hn * (0.5 - fy);
        if(window.zoom(factor, static_cast<float>(cx), static_cast<float>(cy))){
            drawContentsMode = REDRAW;
            update();
        }
    }
    e->accept();
}

void ClusterView::keyPressEvent(QKeyEvent* e){
    // Enter or Return closes the selection polygon, same as middle mouse button
    if((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
       (mode == DELETE_NOISE || mode == DELETE_ARTEFACT ||
        mode == NEW_CLUSTER  || mode == NEW_CLUSTERS) &&
       selectionPolygon.size() > 2)
    {
        closeSelectionPolygon();   // same in the scatter and the embedding
        return;
    }

    // F (t-SNE) and A (autoscale) are NOT handled here: the application-wide
    // filter dispatches them to this widget through toggleTsnePresentation()
    // / toggleAutoscale(), so they work from palette focus too.  A local copy
    // of that policy would be unreachable code that drifts.
    if (tsneMode) {
        // Enter is handled above, by the same branch the scatter uses.
        // Everything else stays inert: zoom, dimension picking and the
        // scatter's own keys all speak feature-world coordinates.
        if (statusBar) statusBar->showMessage(
            tr("t-SNE view: lasso with Ctrl+1 / Ctrl+2 / Delete / Shift+Delete, "
               "↑/↓ perplexity, F returns"), 2000);
        return;
    }

    ViewWidget::keyPressEvent(e);
}

void ClusterView::toggleTsnePresentation(){
    if (tsneMode) exitTsne(tr("t-SNE off"));
    else          startTsne();
}

void ClusterView::toggleObliquePresentation(){
    if (obliqueMode) exitTsne(tr("oblique off"));
    else             startOblique();
}

bool ClusterView::setObliqueBasis(const QList<int>& ids, QString* message){
    // Empty -> clear the pin and restore the default (basis == the selection).
    if (ids.isEmpty()) {
        obliqueBasis.clear();
        refreshProjectionScope();               // no basis -> empty scope (gate off)
        if (message) *message =
            tr("Oblique basis cleared — Shift+O again uses the selected clusters as the axes.");
        return true;
    }
    QList<int> clean;                               // de-duplicate, preserve order
    for (int id : ids) {
        if (id <= 1) {                              // 0 artefact / 1 noise carry no template
            if (message) *message = tr("Oblique basis: cluster %1 is a reserve bin "
                                       "(artefact/noise) and has no template.").arg(id);
            return false;
        }
        if (!clean.contains(id)) clean.append(id);
    }
    if (clean.size() < 2) {
        if (message) *message = tr("Oblique basis: give at least two distinct clusters.");
        return false;
    }
    // Validate against the ACTIVE layer (the layer startOblique projects on) and
    // require spikes -- the engine averages each basis cluster's own rows.
    Data& d = doc.data();
    for (int id : clean)
        if (d.nbOfSpikes(static_cast<dataType>(id)) <= 0) {
            if (message) *message = tr("Oblique basis: cluster %1 does not exist or "
                                       "has no spikes in the active view.").arg(id);
            return false;
        }
    obliqueBasis = clean;
    refreshProjectionScope();                   // rebuild the temporal scope from the new basis
    QStringList s; for (int id : clean) s << QString::number(id);
    if (message) *message = tr("Oblique basis pinned to cluster(s) %1 — select a cluster "
                               "to examine and press Shift+O to project it onto these axes.")
                               .arg(s.join(QStringLiteral(", ")));
    return true;
}

bool ClusterView::projScopeActive() const {
    // Read the mode live so a Preferences toggle takes effect on the next repaint;
    // the intervals (projScopeRU) only change when the basis/stage does.  An empty
    // scope leaves the gate off (never hide every spike) — see gateActive().
    return (configuration().getProjectionScopeMode() == 1) && !projScopeRU.empty();
}

bool ClusterView::spikeTimeInScope(double tRecordingUnits) const {
    return neurosuite::projectionscope::inScope(projScopeRU, tRecordingUnits);
}

bool ClusterView::resolveSessionPaths(std::string& base, int& group, std::string& tag) const {
    // base/group/stage of the open document, from its .clu file name (custody).
    const QString cluPath = doc.url();
    if (cluPath.isEmpty()) return false;
    const auto a = neurosuite::custody::parseAnchor(QFileInfo(cluPath).fileName().toStdString());
    if (!a.ok) return false;
    const QString dir = QFileInfo(cluPath).absolutePath() + QLatin1Char('/');
    base  = (dir + QString::fromStdString(a.base)).toStdString();
    group = a.group;
    tag   = a.suffix;                               // "" = untagged stage
    return true;
}

void ClusterView::loadEapCollisions(){
    namespace nf  = neurofileio;
    namespace cst = neurosuite::custody;
    eapCollision.clear();
    std::string base, tag; int group = 0;
    if (!resolveSessionPaths(base, group, tag)) return;
    // .eap is method-less + per-stage: <base>.eap.<group>[.<tag>].
    std::string eapPath = cst::untaggedPath(base, "eap", group);
    if (!tag.empty()) eapPath += "." + tag;
    const nf::EapFile e = nf::readEap(eapPath);
    if (!e.ok || e.nSpikes <= 0 || e.nClasses <= 0) return;
    eapCollision.assign(static_cast<std::size_t>(e.nSpikes), 0);
    for (int64_t i = 0; i < e.nSpikes; ++i) {
        const std::size_t rowBase = static_cast<std::size_t>(i) * e.nClasses;
        int cnt = 0;
        for (int j = 0; j < e.nClasses; ++j)
            if (nf::eapPresent(e.cells[rowBase + j]) && ++cnt >= 2) break;   // stop at 2
        if (cnt >= 2) eapCollision[static_cast<std::size_t>(i)] = 1;
    }
}

void ClusterView::setHighlightClass(int col){
    namespace nf  = neurofileio;
    namespace cst = neurosuite::custody;
    highlightClassCol = col;
    eapHighlightMember.clear();
    if (col >= 0) {
        std::string base, tag; int group = 0;
        if (resolveSessionPaths(base, group, tag)) {
            std::string eapPath = cst::untaggedPath(base, "eap", group);
            if (!tag.empty()) eapPath += "." + tag;
            const nf::EapFile e = nf::readEap(eapPath);
            if (e.ok && col < e.nClasses && e.nSpikes > 0) {
                eapHighlightMember.assign(static_cast<std::size_t>(e.nSpikes), 0);
                for (int64_t i = 0; i < e.nSpikes; ++i)
                    if (nf::eapPresent(e.cells[static_cast<std::size_t>(i) * e.nClasses + col]))
                        eapHighlightMember[static_cast<std::size_t>(i)] = 1;
            }
        }
    }
    drawContentsMode = REDRAW;   // repaint with the new (or cleared) member highlight
    update();
}

void ClusterView::refreshProjectionScope(){
    namespace ps  = neurosuite::projectionscope;
    namespace nf  = neurofileio;
    namespace cst = neurosuite::custody;
    projScopeRU.clear();

    // Only the pinned oblique basis defines a scope.
    std::vector<int> basis;
    for (int id : obliqueBasis) basis.push_back(id);
    if (basis.empty() || samplingInterval <= 0.0) return;

    std::string base, tag; int group = 0;
    if (!resolveSessionPaths(base, group, tag)) return;

    // .wti is method-less, per-stage; .tcl is method-less, stage-independent.
    std::string wtiPath = cst::untaggedPath(base, "wti", group);
    if (!tag.empty()) wtiPath += "." + tag;
    const std::string tclPath = cst::untaggedPath(base, "tcl", group);

    const nf::WtiIndex   wti = nf::readWti(wtiPath);
    const nf::TclRegistry reg = nf::readTcl(tclPath);
    if (!wti.ok || !reg.ok) return;

    const std::vector<ps::Interval> sec = ps::scopeIntervals(wti, reg, basis);
    if (sec.empty()) return;

    // seconds -> recording units (the feature table's time column):
    //   ru = s * 1e6 / samplingInterval   (samplingInterval is microseconds/sample)
    // Multiplying by a positive constant preserves the sorted, non-overlapping order
    // scopeIntervals produced, so inScope()'s binary search stays valid.
    const double k = 1000000.0 / samplingInterval;
    projScopeRU.reserve(sec.size());
    for (const ps::Interval& iv : sec)
        projScopeRU.push_back(ps::Interval{ iv.a * k, iv.b * k });
}

void ClusterView::startOblique(){
    if (tsneComputing || tsneThread) {           // a t-SNE run owns the scatter
        if (statusBar) statusBar->showMessage(
            tr("oblique: a t-SNE run is computing — press F to cancel it first"), 3000);
        return;
    }
    if (tsneMode) exitTsne();                     // replace any showing embedding
    Data& d = doc.data();
    const bool childLayer = doc.isChildClusteringActive();

    const QList<int> shown = view.clusters();
    // Resolve the pinned basis (Set Oblique Basis…) against the ACTIVE layer:
    // keep only ids that still own spikes here.  Two or more survivors -> the
    // projection uses THEM as the fixed template axes and the selection is what
    // gets examined against them; otherwise fall back to the default (basis ==
    // the selection), which still needs two selected clusters.
    QList<int> pinned;
    for (int id : obliqueBasis)
        if (id > 1 && !pinned.contains(id)
            && d.nbOfSpikes(static_cast<dataType>(id)) > 0) pinned.append(id);
    const bool usePinned = pinned.size() >= 2;
    if (!obliqueBasis.isEmpty() && !usePinned && statusBar)
        statusBar->showMessage(
            tr("oblique: the pinned basis is no longer valid here — using the selected clusters"), 4000);
    if (!usePinned && shown.size() < 2) {
        if (statusBar) statusBar->showMessage(
            tr("oblique: select at least two clusters — they are the template axes "
               "(or pin a basis with Actions ▸ Set Oblique Basis…)"), 4000);
        return;
    }
    const int allDims = d.nbOfDimensionsTotal() - 1;         // every dim except time
    const int dimCap  = configuration().getTsneMaxDimensions();
    const int D = (dimCap > 0) ? qMin(dimCap, allDims) : allDims;
    if (D < 2) {
        if (statusBar) statusBar->showMessage(tr("oblique: not enough feature dimensions"), 3000);
        return;
    }

    // GLOBAL per-dimension mean / inv-std over ALL spikes in the group.  This is
    // the standardising reference the projection needs: centering over only the
    // selected clusters would collapse the basis (see oblique_embed.h).  Two
    // inline passes over the feature table -- a few M reads, below a frame.
    const dataType nAll = d.totalNbOfSpikes();
    if (nAll < 2) {
        if (statusBar) statusBar->showMessage(tr("oblique: no spikes in this group"), 3000);
        return;
    }
    std::vector<double> gMean(D, 0.0), gInv(D, 0.0);
    for (dataType row = 1; row <= nAll; ++row)
        for (int dim = 1; dim <= D; ++dim)
            gMean[dim - 1] += static_cast<double>(d.featureValue(row, dim));
    for (int k = 0; k < D; ++k) gMean[k] /= static_cast<double>(nAll);
    for (dataType row = 1; row <= nAll; ++row)
        for (int dim = 1; dim <= D; ++dim) {
            const double v = static_cast<double>(d.featureValue(row, dim)) - gMean[dim - 1];
            gInv[dim - 1] += v * v;
        }
    for (int k = 0; k < D; ++k) {
        const double var = gInv[k] / std::max<double>(1.0, static_cast<double>(nAll - 1));
        gInv[k] = (var > 1e-12) ? 1.0 / std::sqrt(var) : 0.0;   // dead dim -> dropped
    }

    // Gather the feature rows + labels + .spk rows to project.  Two shapes: the
    // DEFAULT (basis == selection) mirrors startTsne's gather exactly; the PINNED
    // shape adds the basis clusters to the pool (the engine builds each basis
    // template from its own gathered rows) and protects the examined clusters'
    // render budget against a high-rate basis.
    std::vector<double> X;
    std::vector<int>    labelsVec;
    QVector<int>        rows;
    std::vector<int>    basis;
    int N = 0;
    const int cap = configuration().getTsneSpikeCap();
    const bool subsample = configuration().getTsneSubsampleOverCap();

    if (!usePinned) {
        // ---- default: basis == selection (unchanged from the original) ----
        QList<QPair<int, SortableTable*>> tables;
        qint64 total = 0;
        for (int id : shown) {
            auto* t = new SortableTable();
            if (!d.spikePositions(id, *t)) { delete t; continue; }
            total += t->nbOfColumns();
            tables.append(qMakePair(id, t));
            if (total > cap) break;
        }
        if ((total > cap && !subsample) || tables.isEmpty() || total < 8) {
            for (auto& pr : tables) delete pr.second;
            if (statusBar) statusBar->showMessage(
                total > cap
                  ? tr("oblique refused: %1 spikes selected, cap is %2 "
                       "(raise it, or enable subsampling, in Preferences)").arg(total).arg(cap)
                  : tr("oblique: too few spikes selected"), 5000);
            return;
        }
        QSet<qint64> keep;
        const bool sampled = (total > cap);
        if (sampled) {
            std::vector<qint64> idx(static_cast<size_t>(total));
            std::iota(idx.begin(), idx.end(), 0);
            std::shuffle(idx.begin(), idx.end(), std::mt19937(42u));
            idx.resize(static_cast<size_t>(cap));
            for (qint64 v : idx) keep.insert(v);
        }
        N = sampled ? cap : static_cast<int>(total);
        X.assign(static_cast<size_t>(N) * D, 0.0);
        labelsVec.reserve(N);
        rows.reserve(N);
        int r = 0;
        qint64 seen = 0;
        for (auto& pr : tables) {
            SortableTable& t = *pr.second;
            const dataType n = t.nbOfColumns();
            for (dataType i = 1; i <= n; ++i, ++seen) {
                if (sampled && !keep.contains(seen)) continue;
                const dataType row = t(1, i);
                for (int dim = 1; dim <= D; ++dim)
                    X[static_cast<size_t>(r) * D + (dim - 1)] =
                        static_cast<double>(d.featureValue(row, dim));
                labelsVec.push_back(pr.first);
                rows.append(static_cast<int>(row) - 1);
                ++r;
            }
            delete pr.second;
        }
        basis.reserve(shown.size());
        for (int id : shown) basis.push_back(id);
    } else {
        // ---- pinned: project the selection onto the pinned basis axes ----
        // One strided pull per cluster.  Basis clusters are capped to a reference
        // sample (basisRef) so a fast-spiking basis cannot eat the budget; the
        // examined (non-basis) clusters take the rest and are subsampled only if
        // THEY overflow.  Basis rows are always kept -- their mean IS the axis.
        const int K = pinned.size();
        const int basisRef = qBound(50, cap / (2 * (K + 1)), 4000);
        struct Ent { dataType row; int cid; };
        std::vector<Ent> basisEnts, thirdEnts;
        auto pull = [&](int id, bool basisCl){
            SortableTable t;
            if (!d.spikePositions(id, t)) return;
            const long nn = static_cast<long>(t.nbOfColumns());
            if (nn <= 0) return;
            const long take = basisCl ? qMin<long>(nn, basisRef) : nn;
            const long step = qMax<long>(1, basisCl ? nn / qMax<long>(1, take) : 1);
            std::vector<Ent>& dst = basisCl ? basisEnts : thirdEnts;
            for (long i = 1; i <= nn; i += step)
                dst.push_back(Ent{ t(1, static_cast<dataType>(i)), id });
        };
        for (int id : pinned) pull(id, true);
        for (int id : shown) if (!pinned.contains(id)) pull(id, false);

        const qint64 nBasis = static_cast<qint64>(basisEnts.size());
        const qint64 nThird = static_cast<qint64>(thirdEnts.size());
        if (nBasis == 0 || (nBasis + nThird) < 8) {
            if (statusBar) statusBar->showMessage(
                tr("oblique: too few spikes to project onto the pinned basis"), 5000);
            return;
        }
        const qint64 thirdBudget = qMax<qint64>(0, static_cast<qint64>(cap) - nBasis);
        if (nThird > thirdBudget && !subsample) {
            if (statusBar) statusBar->showMessage(
                tr("oblique refused: %1 spikes (%2 basis + %3 examined), cap is %4 "
                   "(raise it, or enable subsampling, in Preferences)")
                    .arg(nBasis + nThird).arg(nBasis).arg(nThird).arg(cap), 6000);
            return;
        }
        QSet<qint64> keepThird;
        const bool sampled = (nThird > thirdBudget);
        if (sampled) {
            std::vector<qint64> idx(static_cast<size_t>(nThird));
            std::iota(idx.begin(), idx.end(), 0);
            std::shuffle(idx.begin(), idx.end(), std::mt19937(42u));
            idx.resize(static_cast<size_t>(thirdBudget));
            for (qint64 v : idx) keepThird.insert(v);
        }
        N = static_cast<int>(nBasis + (sampled ? thirdBudget : nThird));
        X.assign(static_cast<size_t>(N) * D, 0.0);
        labelsVec.reserve(N);
        rows.reserve(N);
        int r = 0;
        auto addRow = [&](const Ent& e){           // not named 'emit' -- Qt macro
            for (int dim = 1; dim <= D; ++dim)
                X[static_cast<size_t>(r) * D + (dim - 1)] =
                    static_cast<double>(d.featureValue(e.row, dim));
            labelsVec.push_back(e.cid);
            rows.append(static_cast<int>(e.row) - 1);
            ++r;
        };
        for (const Ent& e : basisEnts) addRow(e);
        qint64 ti = 0;
        for (const Ent& e : thirdEnts) { if (!sampled || keepThird.contains(ti)) addRow(e); ++ti; }
        basis.reserve(K);
        for (int id : pinned) basis.push_back(id);
    }

    std::vector<double> xy;
    double cond = 0.0;
    std::string err;
    if (!obliqueProject(X, N, D, labelsVec, basis, gMean, gInv, xy, cond, &err)) {
        if (statusBar) statusBar->showMessage(
            tr("oblique failed: %1").arg(QString::fromStdString(err)), 5000);
        return;
    }

    // Hand the result to the shared embedding buffers and show it exactly as the
    // t-SNE path does (same paint, same lasso, same bbox mapping).
    tsneChildLayer = childLayer;
    tsneXY = std::move(xy);
    QList<int> labels; labels.reserve(N);
    for (int v : labelsVec) labels.append(v);
    tsneRowCluster = std::move(labels);
    tsneRowSpike   = std::move(rows);
    resetSelectionPolygon();
    {   // capture the bounding box once: paint and hit-test must agree
        const int n = static_cast<int>(tsneXY.size() / 2);
        tsneMinX = tsneMaxX = tsneMinY = tsneMaxY = 0.0;
        if (n > 0) {
            tsneMinX = tsneMaxX = tsneXY[0];
            tsneMinY = tsneMaxY = tsneXY[1];
            for (int i = 1; i < n; ++i) {
                tsneMinX = qMin(tsneMinX, tsneXY[2 * i]);
                tsneMaxX = qMax(tsneMaxX, tsneXY[2 * i]);
                tsneMinY = qMin(tsneMinY, tsneXY[2 * i + 1]);
                tsneMaxY = qMax(tsneMaxY, tsneXY[2 * i + 1]);
            }
            const double mx = (tsneMaxX - tsneMinX) * 0.05 + 1e-9;
            const double my = (tsneMaxY - tsneMinY) * 0.05 + 1e-9;
            tsneMinX -= mx; tsneMaxX += mx; tsneMinY -= my; tsneMaxY += my;
        }
    }
    tsneSpikeCount   = N;
    if (usePinned) {
        QSet<int> distinctClusters;                 // basis + examined, as projected
        for (int v : labelsVec) distinctClusters.insert(v);
        tsneClusterCount = distinctClusters.size();
    } else {
        tsneClusterCount = shown.size();
    }
    obliqueCond      = cond;
    obliqueMode      = true;
    tsneMode         = true;
    drawContentsMode = REDRAW;
    update();
    if (statusBar) {
        if (usePinned) {
            QStringList b; for (int id : pinned) b << QString::number(id);
            statusBar->showMessage(
                tr("oblique: %1 spikes on the pinned basis [%2], condition %3 — "
                   "lasso to cut, Shift+O returns")
                    .arg(N).arg(b.join(QStringLiteral(","))).arg(cond, 0, 'f', 1), 8000);
        } else {
            statusBar->showMessage(
                (childLayer
                   ? tr("oblique: %1 spikes, %2 atom template(s), condition %3 — lasso to cut, Shift+O returns")
                   : tr("oblique: %1 spikes, %2 template(s), condition %3 — lasso to cut, Shift+O returns"))
                    .arg(N).arg(shown.size()).arg(cond, 0, 'f', 1), 8000);
        }
    }
}

void ClusterView::toggleAutoscale(){
    // When enabled, the view refits bounds to the current shownClusters
    // projection each time paintEvent redraws the double buffer — so moving
    // dimensions, adding/removing clusters, or running ops automatically
    // rescales.  Toggling off returns to manual zoom (bounds stay at whatever
    // the last autoscale produced, then persist under normal zoom ops).
    autoscaleEnabled = !autoscaleEnabled;
    if (!autoscaleEnabled) {
        // Self-heal a session that already collapsed its world under the old
        // behaviour: rebuilding from the extrema restores the room to pan and
        // zoom out, and keeps whatever the user is currently looking at.
        recomputeWorldBounds();
        drawContentsMode = REDRAW;
        update();
    }
    if (autoscaleEnabled) {
        autoscaleToVisibleClusters();
        drawContentsMode = REDRAW;
        update();
        if (statusBar) statusBar->showMessage(tr("Autoscale: on (press A to disable)"), 3000);
    } else {
        if (statusBar) statusBar->showMessage(tr("Autoscale: off"), 3000);
    }
}

void ClusterView::autoscaleToVisibleClusters()
{
    const QList<int> shown = view.clusters();
    if (shown.isEmpty()) return;    // nothing to fit to — leave bounds alone

    Data& clusteringData = doc.data();

    bool haveAny = false;
    long xMin = 0, xMax = 0, yMin = 0, yMax = 0;

    // Iterate every spike of every shown cluster at the current
    // (dimensionX, dimensionY) projection.  spikeIterator(dx, dy) yields
    // a QPoint with the two coordinates; we reduce to min/max.  The cost
    // is O(totalShownSpikes); same order as a single paintEvent pass, so
    // adding this on autoscale toggle is not a performance concern.
    for (int clustId : shown) {
        Data::Iterator it = clusteringData.iterator(static_cast<dataType>(clustId));
        for (; it.hasNext(); it.next()) {
            const QPoint p = it(dimensionX, dimensionY);
            if (!haveAny) {
                xMin = xMax = p.x();
                yMin = yMax = p.y();
                haveAny = true;
            } else {
                if (p.x() < xMin) xMin = p.x();
                if (p.x() > xMax) xMax = p.x();
                if (p.y() < yMin) yMin = p.y();
                if (p.y() > yMax) yMax = p.y();
            }
        }
    }

    if (!haveAny) return;   // shownClusters non-empty but no spikes (edge case)

    // Margin fraction per side, pulled from general preferences (default 5%).
    // The Configuration setter clamps to [0, 50] %, so the value is safe to
    // use directly without re-validating here.
    const double marginFrac = configuration().getAutoscaleMarginPercent() / 100.0;

    // Keep axes visible + clamp to the Qt-safe range used throughout the
    // view (matches updatedDimensions).
    //
    // Iterator::operator()(dx, dy) returns a QPoint with the ordinate
    // ALREADY NEGATED (Qt graphical orientation, see data.h:344–348), so
    // yMin/yMax are in the same screen-orientation space that
    // abscissaMin/Max / ordinateMin/Max live in — no extra flip needed.
    // The qMin(0L, …)/qMax(0L, …) widening keeps the origin (axes) in
    // view even when the data cluster sits entirely on one side.
    const long width  = xMax - xMin;
    const long height = yMax - yMin;

    // LOCALS, not the abscissa/ordinate members: those are the WORLD, and
    // recomputeWorldBounds() below rewrites them from the extrema.  Writing the
    // fit into them was the same mistake in miniature -- it made the fit the
    // world, which is exactly what this function must stop doing.
    long fitLeft  = static_cast<long>(qMin(0L, xMin) - width  * marginFrac);
    fitLeft       = qMax(fitLeft, -1000000L);
    long fitRight = static_cast<long>(qMax(0L, xMax) + width  * marginFrac);

    long fitTop    = static_cast<long>(qMin(0L, yMin) - height * marginFrac);
    long fitBottom = static_cast<long>(qMax(0L, yMax) + height * marginFrac);
    fitBottom      = qMin(fitBottom,  1000000L);

    // Apply the fit as a ZOOM inside the world, never as a new world.
    //
    // Constructing a ZoomWindow sets its INITIAL bounds, and correctWindow()
    // keeps every later window inside those: so assigning the fitted rect here
    // made the fit the world.  From then on the window already WAS the full
    // extent, which leaves panning nothing to shift into and zooming out
    // nothing to expand into -- both simply stop responding, and toggling
    // autoscale off does not undo it because the flag only stops the refitting.
    // One press of A therefore disabled drag and wheel for the rest of the
    // session.  The world belongs to the dimension extrema; a fit is a view of
    // it.
    recomputeWorldBounds();          // world from the extrema, zoom preserved
    window.zoom(QPoint(fitLeft, fitTop), QPoint(fitRight, fitBottom));
}

void ClusterView::mouseMoveEvent(QMouseEvent* e){
    if (tsneMode) {
        // Same tracking-vertex behaviour as the scatter, in embedding pixels:
        // the polygon carries the point under the cursor and it is committed
        // by the next click.  No coordinate read-out -- embedding axes have no
        // units to report.
        if(!polygonClosed &&
           (mode == DELETE_NOISE || mode == DELETE_ARTEFACT ||
            mode == NEW_CLUSTER  || mode == NEW_CLUSTERS) &&
           !selectionPolygon.isEmpty()){
            const QPoint current = selectionPoint(e->position().toPoint());
            if(nbSelectionPoints == selectionPolygon.size())
                selectionPolygon.putPoints(selectionPolygon.size(), 1, current.x(), current.y());
            else
                selectionPolygon.setPoint(selectionPolygon.size()-1, current);
            drawContentsMode = REFRESH;
            update();
        }
        return;
    }
    // Ctrl+drag pan: keep the world point grabbed at press under the cursor.
    // Re-centre the window each move (size unchanged) — zoom(1.0, c) recentres
    // and clamps to the full extent.  Computed against the current window so it
    // does not drift as the view moves.
    if(ctrlPanArmed && (e->buttons() & Qt::LeftButton) && (e->modifiers() & Qt::ControlModifier)){
        const QPoint dpx = e->position().toPoint() - ctrlPanAnchorPx;
        if(!ctrlPanning && (qAbs(dpx.x()) + qAbs(dpx.y()) >= ctrlPanDragThreshold))
            ctrlPanning = true;
        if(ctrlPanning){
            const QPoint cw = viewportToWorld(e->position().toPoint().x(),
                                              e->position().toPoint().y());
            const QRect  wr = (QRect)window;
            const double curCx = wr.left() + wr.width()  / 2.0;
            const double curCy = wr.top()  + wr.height() / 2.0;
            const double newCx = curCx - static_cast<double>(cw.x() - ctrlPanPressWorldX);
            const double newCy = curCy - static_cast<double>(cw.y() - ctrlPanPressWorldY);
            if(window.zoom(1.0f, static_cast<float>(newCx), static_cast<float>(newCy))){
                drawContentsMode = REDRAW;
                update();
            }
        }
        e->accept();
        return;
    }

    //Write the current coordinates in the statusbar.
    QPoint current = viewportToWorld(e->position().toPoint().x(),e->position().toPoint().y());

    if(dimensionX == timeDimension){
        int timeInS = static_cast<int>(current.x() * samplingInterval / 1000000.0);
        statusBar->showMessage("Coordinates: (" + QString::fromLatin1("%1").arg(timeInS) + ", " + QString::fromLatin1("%1").arg(-current.y()) + ")");
    }
    else if(dimensionY == timeDimension){
        int timeInS = static_cast<int>(current.y() * samplingInterval / 1000000.0);
        statusBar->showMessage("Coordinates: (" + QString::number(current.x()) + ", " + QString::fromLatin1("%1").arg(-timeInS) + ")");
    }
    else
        statusBar->showMessage("Coordinates: (" + QString::number(current.x()) + ", " + QString::fromLatin1("%1").arg(-current.y()) + ")");



    //The parent implementation takes care of the rubber band
    ViewWidget::mouseMoveEvent(e);

    //If the user is closing the polygon do not take mousemove event into account
    if(!polygonClosed){
        //In one of the selection modes we draw the tracking line
        if(mode == DELETE_NOISE || mode == DELETE_ARTEFACT || mode == NEW_CLUSTER || mode == NEW_CLUSTERS){


            //If there is no selection point, do not draw a tracking line
            if(selectionPolygon.isEmpty())
                return;
            //First mouseMoveEvent after the last mousePressEvent
            if(nbSelectionPoints == selectionPolygon.size()){
                //Add the current point to the array
                selectionPolygon.putPoints(selectionPolygon.size(), 1, current.x(),current.y());
            }
            else{
                selectionPolygon.setPoint(selectionPolygon.size()-1,current);
            }
            drawContentsMode = REFRESH;
            update();
        }
    }
}

void ClusterView::customEvent(QEvent* event){
    if(event->type() == QEvent::User + 700){
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        ComputeEvent* computeEvent = (ComputeEvent*) event;
        //Get the polygon
        QPolygon polygon = computeEvent->polygon();
        QRegion selectionArea;
        QPolygon reviewPolygon;
        long Xdimension = 0;
        long Ydimension = 0;

        //The QRegion uses rectangles to define its area and the number of rectangles
        //increases with the height of the region (y axis). The more rectangles the longer
        //the search of one point in the region will take. With a dimension like the time
        //the height has an order of the millon (at least 5 going to 80 or more) given a huge amount
        //of rectangles. A way of speeding the search of points is to reduce the number of rectangles.
        //To do so, if the y dimension is the time, x and y axis are inverted.
        //Caution: in Qt graphical coordinate system, the Y axis is inverted (increasing downwards),
        //thus a point (x,y) is drawn as (x,-y), before creating the region the points are reset to there raw value (x,y).

        if(view.ordinateDimension() != timeDimension){
            for(uint i = 0; i< polygon.size();++i){
                reviewPolygon.putPoints(i, 1,polygon.point(i).x(),-polygon.point(i).y());
                Xdimension = dimensionX;
                Ydimension = dimensionY;
            }
        }
        else{
            for(uint i = 0; i< polygon.size();++i){
                reviewPolygon.putPoints(i, 1,-polygon.point(i).y(),polygon.point(i).x());

                Xdimension = dimensionY;
                Ydimension = dimensionX;
            }
        }
        //Create a QRegion with the new selection area in order to use the research facilities offer by a QRegion.
        selectionArea = QRegion(reviewPolygon);
        if(!selectionArea.isEmpty()){
            // Route through the SpikeSelection form so the temporally-restricted
            // scope (when active) marks out-of-scope spikes non-selectable:
            // selectionContains ANDs the region test with the time-window test.
            // Identical to the QRegion overloads otherwise (they build the same
            // SpikeSelection, just without a scope).
            SpikeSelection selection(selectionArea,
                                     static_cast<int>(Xdimension), static_cast<int>(Ydimension));
            if(projScopeActive()){
                std::vector<std::pair<double,double> > win;
                win.reserve(projScopeRU.size());
                for(const neurosuite::projectionscope::Interval& iv : projScopeRU)
                    win.emplace_back(iv.a, iv.b);
                selection.setTimeScope(win, timeDimension);
            }
            //Call any appropriate method
            switch(mode){
            case DELETE_NOISE:
                doc.deleteNoise(selection,view.clusters());
                break;
            case DELETE_ARTEFACT:
                doc.deleteArtifact(selection,view.clusters());
                break;
            case NEW_CLUSTER:
                doc.createNewCluster(selection,view.clusters());
                setFocus(Qt::OtherFocusReason);
                break;
            case NEW_CLUSTERS:
                doc.createNewClusters(selection,view.clusters());
                setFocus(Qt::OtherFocusReason);
                break;
            case ZOOM:
                break; //nothing to do
            }
        }
        QApplication::restoreOverrideCursor();
    }
}

void ClusterView::drawAxes(QPainter& painter){
    painter.setPen(QColor(60,60,60));    //set the color for the lines
    painter.drawLine(abscissaMin,0,abscissaMax,0); // draw line
    painter.drawLine(0,ordinateMin,0,ordinateMax); // draw line
}

void ClusterView::drawTimeInformation(QPainter& painter){
    if(dimensionX == timeDimension){
        painter.setPen(QColor(60,60,60)); //set the color for the lines
        int markHeight = static_cast<int>(abs(ordinateMax - ordinateMin));
        QFont f("Helvetica",9);
        painter.setFont(f);
        long time = 0;
        QRect r((QRect)window);
        long legendOrdinate = worldToViewportOrdinate(r.top()) + 20;


        for(long i=0; i < abscissaMax; i += timeStepInRecordingUnit){
            QPoint topInViewport = worldToViewport(i,-markHeight);
            QPoint bottomInViewport = worldToViewport(i,markHeight);

            painter.drawLine(topInViewport,bottomInViewport);
            painter.drawText(topInViewport.x() + 1,legendOrdinate,QString::fromLatin1("%1").arg(time));

            time += timeStepInSecond;
        }
    }
    else{
        if(dimensionY == timeDimension){
            painter.setPen(QColor(60,60,60)); //set the color for the lines
            QFont f("Helvetica",9);
            painter.setFont(f);
            long time = 0;
            QRect r((QRect)window);
            long legendAbsciss = worldToViewportAbscissa(r.left());


            int markHeight = static_cast<int>(abs(abscissaMax - abscissaMin));
            //ordinateMin is used because in QT coordinate system the ordinate axis in downwards oriented
            //see the function updatedDimensions for detail.
            for(long i=0; i < abs(ordinateMin); i += timeStepInRecordingUnit){
                QPoint topInViewport = worldToViewport(-markHeight,-i);
                QPoint bottomInViewport = worldToViewport(markHeight,-i);

                painter.drawLine(topInViewport,bottomInViewport);
                painter.drawText(legendAbsciss,topInViewport.y(),QString::fromLatin1("%1").arg(time));

                time += timeStepInSecond;
            }
        }
    }
}


void ClusterView::print(QPainter& printPainter,int width,int height, bool whiteBackground){
    //Draw the double buffer (pixmap) by copying it into the printer device throught the painter.
    QRect viewportOld = QRect(viewport.left(),viewport.top(),viewport.width(),viewport.height());

    viewport = QRect(printPainter.viewport().left(),printPainter.viewport().top(),printPainter.viewport().width(),printPainter.viewport().height());

    QRect r = ((QRect)window);

    //Set the window (part of the world I want to show)
    printPainter.setWindow(r.left(),r.top(),r.width()-1,r.height()-1);//hack because Qt QRect is used differently in this function

    //Fill the background with the background color
    QRect back = QRect(r.left(),r.top(),r.width(),r.height());

    QColor colorLegendTmp = colorLegend;
    QColor background= palette().color(backgroundRole());
    if(whiteBackground){
        colorLegend = Qt::black;
        QPalette palette;
        palette.setColor(backgroundRole(), Qt::white);
        setPalette(palette);
    }

    printPainter.fillRect(back,palette().color(backgroundRole()));
    printPainter.setClipRect(back);

    //Draw the axes
    drawAxes(printPainter);

    //Paint all the clusters in the shownClusters list (in the double buffer)
    drawClusters(printPainter,view.clusters(),true);

    //reset transformation due to setWindow and setViewport
    printPainter.resetTransform();

    //Draw the time axis information if the time is displayed
    drawTimeInformation(printPainter);

    printPainter.setClipping(false);

    //Restore the colors.
    if(whiteBackground){
        colorLegend = colorLegendTmp;
        QPalette palette;
        palette.setColor(backgroundRole(), background);
        setPalette(palette);
    }

    //Restore the previous state
    viewport = QRect(viewportOld.left(),viewportOld.top(),viewportOld.width(),viewportOld.height());
}
