/* klusters_sort.cpp -- KlustersApp cluster sorting and reordering.
 * Split out of klusters.cpp (one KlustersApp class, many .cpp files; the
 * Q_OBJECT lives in klusters.h so moc is unaffected).  Same include set as
 * klusters.cpp; static members are defined once there.
 *
 * Everything the Sort menu drives: the metric sorts (spike count, time,
 * contamination, SNR, amplitude, amplitude by channel, error p-value) and the
 * skeleton they share, the seriations (residual, waveform NN, waveform spectral,
 * reorder-by-similarity) and the single-linkage MST they share, the feature-space
 * and Fiedler orderings, and the status-bar progress bar they all drive.
 */

#include <algorithm>
#include <cmath>
#include <memory>     // std::make_shared — used by Shift+S stale-matrix wait
/***************************************************************************
                          klusters.cpp  -  description
                             -------------------
    begin                : Mon Sep  8 12:06:21 EDT 2003
    copyright            : (C) 2003 by Lynn Hazan
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
#include "config-klusters.h"
// application specific includes
#include "klusters.h"
#include "mergerecommendview.h"
#include "clusterview.h"
#include "klustersdoc.h"
#include <neurosuite/core/custody.hpp>   // shared chain-of-custody type policy (clu/clc/...)
#include "clusterPalette.h"
#include "autoMerge.h"      // patch 0069
#include "savethread.h"
#include "prefdialog.h"
#include "configuration.h"  // class Configuration
#include "processwidget.h"
#include "realignworker.h"
#include "serialjobqueue.h"
#include "realignjob.h"
#include "qhelpviewer.h"
// For the Shift+S reorder action: needs the public accessors + signals
// added to these view classes.
#include "errormatrixview.h"
#include "templatematrixview.h"
#include "residualmatrixview.h"
#include "driftmatrixview.h"
#include "templatematrixthread.h"   // tmReadSpikeFloat (median-waveform NN sort)
#include "array.h"



// include files for QT
#include <QDir>
#include <QTabBar>

#include <QToolTip>
#include <QToolButton>
#include <QString>
#include <QImage>
#include <QSet>
#include <QHash>
#include <QIcon>  
#include <QCursor>
#include <QFileInfo> 
#include <QApplication>
#include <QInputDialog>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QElapsedTimer>
#include <QProgressBar>
#include <QEventLoop>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QGridLayout>
#include <QImage>
#include <QPixmap>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include "plugindialog.h"
#include <QMessageBox>
#include <QLabel>
#include <QPrinter>
#include <QSplitter>
#include <qrecentfileaction.h>
#include <qextendtabwidget.h>
#include <dockarea.h>
#include <QPrintDialog>

#include <QLabel>
#include <QPixmap>
#include <QList>
#include <QEvent>
#include <QKeyEvent>
#include <QAbstractSpinBox>
#include <QPointer>   // QPointer guard for the Shift+S stale-matrix wait
#include "spinbox.h"

#include <QDebug>
#include <QStatusBar>
#include <QProcess>
#include <QMenuBar>
#include <QMessageBox>
#include <QToolBar>
#include <QKeySequence>
#include <QShortcut>  // (kept for potential future use; J/K/X removed 2026-04)
#include <QFileDialog>
#include <QSignalBlocker>
#include <QTime>
#include <QSettings>

extern int nbUndo;

//////////////////////////////////////////////////////////////////////////////
// SortBusyCursor
//
// Busy cursor for the duration of a sort, restored however the slot leaves --
// early return, exception, any of it.  Balancing setOverrideCursor by hand across
// a function with half a dozen exits is how a cursor gets stranded and the whole
// application looks hung until the next one is pushed.
//
// A cursor is NOT redundant with the status-bar progress bar, which is what I
// claimed when I removed the wait cursor from the template build. The bar is gated
// on 200ms elapsed, deliberately, so nothing appears for the first fifth of a
// second; and the sorts that finish under that threshold still are not instant. The
// cursor answers "did my click register" and the bar answers "how long", and those
// are different questions.
//
// restore() exists because it must NOT span a dialog. Sorts prompt (the residual
// gate asks for a spike-count threshold) and report (the reorder warns when a
// permutation is rejected), and a wait cursor sitting over a box asking the curator
// to type a number is telling them the application is busy while it waits for them.
// Where a dialog follows the work, the cursor is dropped first.
//////////////////////////////////////////////////////////////////////////////
namespace {
class SortBusyCursor {
public:
    SortBusyCursor() { QApplication::setOverrideCursor(QCursor(Qt::WaitCursor)); }
    ~SortBusyCursor() { restore(); }
    /** Drop the cursor now.  Idempotent, so the destructor after an explicit call
     *  is a no-op: restoreOverrideCursor() pops a stack, and an unbalanced extra pop
     *  would strip a cursor some outer scope pushed. */
    void restore() {
        if (!active) return;
        active = false;
        QApplication::restoreOverrideCursor();
    }
    SortBusyCursor(const SortBusyCursor&) = delete;
    SortBusyCursor& operator=(const SortBusyCursor&) = delete;
private:
    bool active = true;
};
} // namespace



// ---------------------------------------------------------------------------
// slotSortClustersBySpikeCount
//
// Renumber the non-special clusters so their IDs run by DESCENDING spike count
// (largest cluster becomes 2, next 3, ...).  Clusters 0 (artefact) and 1
// (noise) keep their IDs.  Equal-size clusters keep their current relative
// order (stable sort).  All undo / curation-log / palette / view bookkeeping is
// handled inside reorderClustersByPermutation (same path as the Shift+S
// similarity reorder), so this is a single undoable step.
// ---------------------------------------------------------------------------
void KlustersApp::slotSortClustersBySpikeCount()
{
    QList<int> clusters = clustersToSort(mSortClustersBySpikeCount, tr("spike count"));
    if (clusters.isEmpty()) return;
    SortBusyCursor busy;   // after the prologue: nothing to wait for if it bailed
    auto& d = doc->data();

    // Snapshot the counts once (avoids re-locking Data in the comparator).
    QHash<int, qint64> spikeCount;
    spikeCount.reserve(clusters.size());
    for (int c : clusters)
        spikeCount.insert(c, static_cast<qint64>(d.nbOfSpikes(c)));

    std::stable_sort(clusters.begin(), clusters.end(),
        [&spikeCount](int a, int b){ return spikeCount.value(a) > spikeCount.value(b); });

    applySortedOrder(clusters, tr("spike count"), tr("largest first"));
}

// ---------------------------------------------------------------------------
// slotSortClustersByTime
//
// Renumber non-special clusters by ascending starting-edge time (earliest spike
// first).  Mirrors slotSortClustersBySpikeCount; the per-cluster metric is
// Data::firstSpikeTimes() (a single pass over all spikes), and the reorder /
// undo / log / palette bookkeeping stays inside reorderClustersByPermutation.
// ---------------------------------------------------------------------------
void KlustersApp::slotSortClustersByTime()
{
    QList<int> clusters = clustersToSort(mSortClustersByTime, tr("time"));
    if (clusters.isEmpty()) return;
    SortBusyCursor busy;   // after the prologue: nothing to wait for if it bailed
    auto& d = doc->data();

    // Snapshot each cluster's earliest spike time once (one pass over all spikes).
    const QHash<int,double> firstTs = d.firstSpikeTimes();

    // A cluster with no in-range spikes is absent from the hash; sort those last,
    // deterministically, via a +inf sentinel.
    const double sentinel = std::numeric_limits<double>::max();
    std::stable_sort(clusters.begin(), clusters.end(),
        [&firstTs, sentinel](int a, int b){
            return firstTs.value(a, sentinel) < firstTs.value(b, sentinel);
        });

    applySortedOrder(clusters, tr("starting time"), tr("earliest first"));
}

// ---------------------------------------------------------------------------
// slotSortClustersByContamination
//
// Renumber non-special clusters by descending refractory ISI-violation fraction
// (2 ms window) so the most contaminated cluster becomes 2.  Mirrors
// slotSortClustersByTime; metric = Data::refractoryViolationFractions(2.0).
// ---------------------------------------------------------------------------
void KlustersApp::slotSortClustersByContamination()
{
    QList<int> clusters = clustersToSort(mSortClustersByContamination, tr("contamination"));
    if (clusters.isEmpty()) return;
    SortBusyCursor busy;   // after the prologue: nothing to wait for if it bailed
    auto& d = doc->data();

    // Refractory contamination at a 2 ms window, one pass over all spikes.
    const QHash<int,double> contam = d.refractoryViolationFractions(2.0);

    // Missing clusters (no in-range spikes) have no contamination; sort them last
    // via a -1 sentinel (below any real fraction in [0,1]) under descending order.
    std::stable_sort(clusters.begin(), clusters.end(),
        [&contam](int a, int b){
            return contam.value(a, -1.0) > contam.value(b, -1.0);
        });

    applySortedOrder(clusters, tr("contamination"), tr("most contaminated first"));
}

// ---------------------------------------------------------------------------
// slotSortClustersBySnr
//
// Renumber non-special clusters by descending mean-waveform SNR so the cleanest
// cluster becomes 2.  Mirrors slotSortClustersByContamination; metric =
// Data::clusterWaveformSnrs(), which covers every cluster (templates are built
// on demand).  A cluster whose template could not be built is absent from the
// metric and sorts last via a -1 sentinel.
// ---------------------------------------------------------------------------
void KlustersApp::slotSortClustersBySnr()
{
    QList<int> clusters = clustersToSort(mSortClustersBySnr, tr("SNR"));
    if (clusters.isEmpty()) return;
    SortBusyCursor busy;   // after the prologue: nothing to wait for if it bailed
    auto& d = doc->data();

    // Build templates for every cluster first.  Without this the metric only
    // covers clusters the waveform view happens to be showing, and the rest fall
    // to the -1 sentinel and sort last -- so the sort looked like it worked while
    // ranking a handful of clusters and dumping the others at the end.
    ensureClusterTemplates();

    const QHash<int,double> snr = d.clusterWaveformSnrs();
    if (snr.isEmpty()) {
        slotStatusMsg(tr("Sort by SNR: no cluster template could be built \u2014 "
                         "is the .spk file readable?"));
        return;
    }

    // A cluster whose template could not be built is absent; sort it last via a
    // -1 sentinel (below any real SNR) under descending order.
    std::stable_sort(clusters.begin(), clusters.end(),
        [&snr](int a, int b){
            return snr.value(a, -1.0) > snr.value(b, -1.0);
        });

    applySortedOrder(clusters, tr("SNR"), tr("highest first"));
}

//////////////////////////////////////////////////////////////////////////////
// slotSortClustersByAmplitude
//
// Renumber clusters by descending peak-to-trough of the mean waveform, taken on
// the channel where it is largest.  Mirrors slotSortClustersBySnr; the metric is
// the amplitude itself rather than amplitude over baseline noise.
//////////////////////////////////////////////////////////////////////////////
void KlustersApp::slotSortClustersByAmplitude()
{
    QList<int> clusters = clustersToSort(mSortClustersByAmplitude, tr("amplitude"));
    if (clusters.isEmpty()) return;
    SortBusyCursor busy;   // after the prologue: nothing to wait for if it bailed
    auto& d = doc->data();

    // Build templates for every cluster first; see slotSortClustersBySnr.
    ensureClusterTemplates();

    const QHash<int,double> amp = d.clusterWaveformAmplitudes();
    if (amp.isEmpty()) {
        slotStatusMsg(tr("Sort by amplitude: no cluster template could be built \u2014 "
                         "is the .spk file readable?"));
        return;
    }

    // A cluster whose template could not be built is absent; sort it last via a
    // -1 sentinel (below any real amplitude) under descending order.
    std::stable_sort(clusters.begin(), clusters.end(),
        [&amp](int a, int b){
            return amp.value(a, -1.0) > amp.value(b, -1.0);
        });

    applySortedOrder(clusters, tr("amplitude"), tr("largest first"));
}

//////////////////////////////////////////////////////////////////////////////
// slotSortClustersByAmplitudeByChannel
//
// Tiered: block by the channel carrying each cluster's largest peak-to-trough,
// blocks in channel order, and order each block by descending amplitude.
//////////////////////////////////////////////////////////////////////////////
void KlustersApp::slotSortClustersByAmplitudeByChannel()
{
    QList<int> clusters = clustersToSort(mSortClustersByAmplitudeByChannel, tr("amplitude by channel"));
    if (clusters.isEmpty()) return;
    SortBusyCursor busy;   // after the prologue: nothing to wait for if it bailed
    auto& d = doc->data();

    // Build templates for every cluster first.  This is the sort that showed the
    // problem most plainly: the peak-channel blocks are only meaningful if EVERY
    // cluster has a peak channel, and clusters without a loaded waveform had none,
    // so they all collapsed into the sentinel block past the last channel.
    ensureClusterTemplates();

    const QHash<int,double> amp = d.clusterWaveformAmplitudes();
    const QHash<int,int>    pk  = d.clusterWaveformPeakChannels();
    if (amp.isEmpty()) {
        slotStatusMsg(tr("Sort by amplitude by channel: no cluster template could be "
                         "built \u2014 is the .spk file readable?"));
        return;
    }

    // Primary key: peak channel ascending, so the blocks run down the probe.
    // Secondary: amplitude descending within a block.  A cluster whose template
    // could not be built has no peak channel either; a sentinel past the last
    // channel parks it in a trailing block rather than salting it through
    // channel 0.
    const int noChannel = d.nbOfchannels();
    std::stable_sort(clusters.begin(), clusters.end(),
        [&amp, &pk, noChannel](int a, int b){
            const int ca = pk.value(a, noChannel);
            const int cb = pk.value(b, noChannel);
            if (ca != cb) return ca < cb;
            return amp.value(a, -1.0) > amp.value(b, -1.0);
        });

    applySortedOrder(clusters, tr("amplitude by channel"),
                     tr("per-channel blocks, largest first in each"));
}

//////////////////////////////////////////////////////////////////////////////
// slotRefreshMergeRecommendations
//
// Recompute the recommended parent merges from the active display's error and
// residual matrices.  Cheap (it reads two already-computed matrices), so it is
// wired to every event that can change the answer rather than to a button.
//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
// ensureClusterTemplates
//
// The overlap witness needs a template for EVERY cluster, not just the ones the
// waveform view happens to be showing -- otherwise a pair can only be scored
// after both its clusters have been selected, which is no use for a panel whose
// whole job is to point at pairs you have NOT looked at yet.
//
// Data::buildMissingClusterTemplates() is incremental, so this is cheap after
// the first run: an edit invalidates only the clusters it touched (via
// invalidateWaveformCache), and only those are rebuilt.
//////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////
// singleLinkageLeafOrder
//
// Leaf order of a single-linkage agglomeration over the dense similarity matrix
// @p S (n x n, row-major, higher = closer, diagonal ignored).  Returns node
// indices in [0,n).
//
// Single-linkage is equivalent to the MAXIMUM SPANNING TREE of the similarity
// graph: every merge joins the two components linked by the current largest
// cross-similarity edge, which is exactly an MST edge, and the merge order is the
// MST edges taken in decreasing weight.  So build the MST once with Prim's --
// O(n^2) on a dense matrix -- and replay its edges heaviest first, rather than
// rescanning every live pair every round for a global argmax, which is O(n^3).
//
// Extracted from slotReorderClustersBySimilarity, which had already been moved to
// this algorithm.  slotSortByResidualGated's comment claimed it "mirrors the
// Shift+S CPU path", but it still ran the O(n^3) loop this had replaced: the two
// drifted, and the slow one was the copy.  One definition now, so they cannot.
//
// The leaf order is reproduced exactly: each merge lets the lower-indexed
// component survive with its leaves first (union-find keeps the minimum original
// index as representative), matching the old loop's `bi < bj; leaves[bi] +=
// leaves[bj]`.  For distinct similarities the result is identical; exact ties
// (astronomically unlikely on double-valued matrix entries) may yield a different
// but equally valid single-linkage order.
//////////////////////////////////////////////////////////////////////////////
static std::vector<int> singleLinkageLeafOrder(const std::vector<double>& S, int n)
{
    std::vector<int> orderIdx;
    if (n <= 0) return orderIdx;
    if (n == 1) { orderIdx.push_back(0); return orderIdx; }

    std::vector<std::vector<int>> leaves(n);
    std::vector<bool>             alive(n, true);
    for (int i = 0; i < n; ++i) leaves[i].push_back(i);

    // 1) Prim's maximum spanning tree.  primParent[v]/primKey[v] hold the
    //    heaviest edge attaching v to the tree.  Off-diagonal S entries are all
    //    finite (possibly negative), so every node is reached and receives
    //    exactly one parent edge.
    std::vector<int>    primParent(n, -1);
    std::vector<double> primKey(n, -std::numeric_limits<double>::infinity());
    std::vector<char>   inTree(n, 0);
    primKey[0] = std::numeric_limits<double>::infinity();   // seed at node 0
    for (int iter = 0; iter < n; ++iter) {
        int    u    = -1;
        double best = -std::numeric_limits<double>::infinity();
        for (int v = 0; v < n; ++v)
            if (!inTree[v] && primKey[v] > best) { best = primKey[v]; u = v; }
        if (u < 0) break;                        // no reachable node left
        inTree[u] = 1;
        const double* Su = &S[static_cast<size_t>(u) * n];
        for (int v = 0; v < n; ++v) {
            if (inTree[v]) continue;
            const double w = Su[v];
            if (w > primKey[v]) { primKey[v] = w; primParent[v] = u; }
        }
    }

    // 2) MST edges, heaviest first == single-linkage merge order.  Tie-break by
    //    (smaller endpoint, larger endpoint) ascending, matching the old loop's
    //    lowest-i-then-lowest-j pick on exact ties.
    struct MstEdge { double w; int a; int b; };   // a < b
    std::vector<MstEdge> edges;
    edges.reserve(static_cast<size_t>(n - 1));
    for (int v = 1; v < n; ++v) {
        if (primParent[v] < 0) continue;          // unreached (disconnected)
        int a = primParent[v], b = v;
        if (a > b) std::swap(a, b);
        edges.push_back({ primKey[v], a, b });
    }
    std::sort(edges.begin(), edges.end(),
              [](const MstEdge& x, const MstEdge& y){
                  if (x.w != y.w) return x.w > y.w;
                  if (x.a != y.a) return x.a < y.a;
                  return x.b < y.b;
              });

    // 3) Union-find with the minimum original index as representative, merging
    //    heaviest edge first and concatenating leaves lower-first.
    std::vector<int> ufParent(n);
    for (int i = 0; i < n; ++i) ufParent[i] = i;
    auto findRep = [&ufParent](int x){
        while (ufParent[x] != x) { ufParent[x] = ufParent[ufParent[x]]; x = ufParent[x]; }
        return x;
    };
    for (const MstEdge& e : edges) {
        const int ra = findRep(e.a);
        const int rb = findRep(e.b);
        if (ra == rb) continue;
        const int lo = std::min(ra, rb);          // lower index survives
        const int hi = std::max(ra, rb);
        leaves[lo].insert(leaves[lo].end(), leaves[hi].begin(), leaves[hi].end());
        alive[hi]    = false;
        ufParent[hi] = lo;
    }

    // Recover the leaf order: walk alive[] and concatenate leaves[] in index
    // order.  In a fully-connected matrix only one node is alive; a disconnected
    // matrix leaves several -- append them in order.
    orderIdx.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (!alive[i]) continue;
        for (int leaf : leaves[i]) orderIdx.push_back(leaf);
    }
    return orderIdx;
}

//////////////////////////////////////////////////////////////////////////////
// slotRecommendationActivated
//
// A recommendation was double-clicked.  Select the pair in the MAIN palette and
// leave the merge to the existing op: the panel is a reader, so undo, hierarchy
// rebuild and colour handling all stay where they already work.
//////////////////////////////////////////////////////////////////////////////


// ---------------------------------------------------------------------------
// slotSortClustersByErrorPval
//
// Renumber non-special clusters by descending error-matrix merge affinity: each
// cluster's summary is the maximum symmetrised off-diagonal probability (its
// strongest same-neuron candidate).  Reads the active ErrorMatrixView's
// probability matrix directly (matrixData / matrixComputedClusterList); the
// order then goes through reorderClustersByPermutation like the other sorts.
// Unlike the timestamp/SNR sorts it needs a computed, up-to-date matrix, so it
// reports rather than sorting on missing or stale data.
// ---------------------------------------------------------------------------
void KlustersApp::slotSortClustersByErrorPval()
{
    if (!mSortClustersByErrorPval->isEnabled()) return;
    KlustersView* view = activeView();
    if (!view) return;

    ErrorMatrixView* emv = view->findChild<ErrorMatrixView*>();
    if (!emv || !emv->hasComputedData()) {
        QMessageBox::information(this, tr("Sort by Error p-value"),
            tr("No computed error matrix in the active display.\n"
               "Open an error matrix and press U to compute it, then try again."));
        return;
    }
    if (emv->isOutOfDate()) {
        slotStatusMsg(tr("Sort by Error p-value: error matrix is out of date; "
                         "press U to recompute it, then retry."));
        return;
    }

    const Array<double>* M    = emv->matrixData();
    const QList<int>     cids = emv->matrixComputedClusterList();
    const int N = cids.size();
    if (!M || N < 2) {
        slotStatusMsg(tr("Sort by Error p-value: matrix too small to sort."));
        return;
    }

    SortBusyCursor busy;   // every bail above is a message, not work

    // Per-cluster summary = max symmetrised off-diagonal probability.  cids[k] is
    // matrix row/col k+1 (the Array is 1-based).
    QHash<int,double> affinity;
    affinity.reserve(N);
    for (int i = 0; i < N; ++i) {
        double best = -1.0;
        for (int j = 0; j < N; ++j) {
            if (j == i) continue;
            const double sij = 0.5 * ((*M)(i + 1, j + 1) + (*M)(j + 1, i + 1));
            if (sij > best) best = sij;
        }
        affinity.insert(cids[i], best);
    }

    // Order every non-noise cluster by descending affinity; clusters absent from
    // the matrix's computed list sort last via a -1 sentinel.
    QList<int> clusters;
    const auto ids = doc->data().clusterIds();
    for (const auto id : ids)
        if (id >= 2) clusters.append(static_cast<int>(id));
    if (clusters.size() < 2) {
        slotStatusMsg(tr("Sort by Error p-value: fewer than 2 non-noise clusters; nothing to sort."));
        return;
    }
    std::stable_sort(clusters.begin(), clusters.end(),
        [&affinity](int a, int b){
            return affinity.value(a, -1.0) > affinity.value(b, -1.0);
        });

    applySortedOrder(clusters, tr("error p-value"), tr("strongest merge candidate first"));
}


// ---------------------------------------------------------------------------
// slotSortByResidualGated
//
// Reorder the non-special clusters using the residual matrix, gated by spike
// count.  Clusters with >= a prompted threshold form the "high" block placed
// first (upper-left of the matrix / low ids); the rest form the "low" block
// placed last (lower-right).  Each block is seriated by residual SIMILARITY so
// alike clusters sit adjacent.  The matrix cell is a DISTANCE (low = similar)
// and asymmetric, so the seriation uses the symmetrised distance
//   d(i,j) = (M(i,j)+M(j,i))/2,  similarity s = dmax - d,
// then single-linkage agglomeration (same as the Shift+S reorder) gives the
// leaf order.  All undo / log / palette / view bookkeeping is handled inside
// reorderClustersByPermutation, so this is one undoable step.
// ---------------------------------------------------------------------------
void KlustersApp::slotSortByResidualGated()
{
    if (!mSortByResidualGated->isEnabled()) return;
    KlustersView* view = activeView();
    if (!view) return;

    ResidualMatrixView* rmv = view->findChild<ResidualMatrixView*>();
    if (!rmv || !rmv->hasComputedData()) {
        QMessageBox::information(this, tr("Sort by Residual (gated)"),
            tr("No computed residual matrix in the active display.\n"
               "Add one via Actions \u2192 New Residual Matrix Display, then\n"
               "press U to compute it, and try again."));
        return;
    }

    // If the matrix is stale, recompute then re-invoke once it is fresh — the
    // same one-shot pattern as the Shift+S reorder.  QPointer guards against
    // the view being destroyed while we wait; the connection self-disconnects.
    if (rmv->isOutOfDate()) {
        statusBar()->showMessage(
            tr("Sort by Residual: matrix out of date; recomputing then sorting\u2026"), 3000);
        QPointer<ResidualMatrixView> guarded(rmv);
        auto* conn = new QMetaObject::Connection;
        *conn = connect(rmv, &ResidualMatrixView::matrixUpdated, this,
            [this, conn, guarded]() {
                QObject::disconnect(*conn); delete conn;
                if (guarded) slotSortByResidualGated();
            });
        view->updateResidualMatrix();
        return;
    }

    const Array<double>* M = rmv->matrixData();
    const QList<int>     cids = rmv->matrixClusterList();
    const int N = cids.size();
    if (!M || N < 2) {
        slotStatusMsg(tr("Sort by Residual: matrix too small to sort."));
        return;
    }

    // Snapshot spike counts for the matrix clusters; default threshold = median.
    QHash<int, qint64> spikeCount;
    spikeCount.reserve(N);
    QList<qint64> counts;
    for (int cid : cids) {
        const qint64 c = static_cast<qint64>(doc->data().nbOfSpikes(cid));
        spikeCount.insert(cid, c);
        if (cid >= 2) counts.append(c);
    }
    if (counts.size() < 2) {
        slotStatusMsg(tr("Sort by Residual: fewer than 2 non-noise clusters; nothing to sort."));
        return;
    }
    std::sort(counts.begin(), counts.end());
    const int defThr = static_cast<int>(std::min<qint64>(
        counts[counts.size()/2], static_cast<qint64>(std::numeric_limits<int>::max())));

    bool ok = false;
    const int thr = QInputDialog::getInt(
        this, tr("Sort by Residual (gated by count)"),
        tr("Spike-count threshold.\n\nClusters with \u2265 this many spikes go to the\n"
           "upper-left (low ids); the rest to the lower-right.  Each block is\n"
           "seriated by residual similarity."),
        defThr, 0, std::numeric_limits<int>::max(), 1, &ok);
    if (!ok) return;

    // The cursor starts HERE, after the prompt.  Spinning it over a dialog asking
    // the curator to type a threshold would claim the application is busy while it
    // is in fact waiting for them.
    SortBusyCursor busy;

    // Partition matrix indices (0-based) by spike count, skipping specials 0/1
    // (reorderClustersByPermutation preserves them at the front).
    QVector<int> hi, lo;
    for (int i = 0; i < N; ++i) {
        const int cid = cids[i];
        if (cid < 2) continue;
        if (spikeCount.value(cid) >= thr) hi.append(i); else lo.append(i);
    }
    if (hi.size() + lo.size() < 2) {
        slotStatusMsg(tr("Sort by Residual: nothing to reorder."));
        return;
    }

    // Seriate one block of matrix indices by single-linkage on residual
    // similarity; returns the block's cluster ids in leaf order.
    //
    // No progress bar here any more.  It was added to make an O(n^3)
    // global-argmax agglomeration bearable; that loop is gone, and the O(n^2) MST
    // replacing it finishes in milliseconds even on the largest block this gate
    // produces, so a bar would only flicker.  Fixing the algorithm removed the
    // reason for the feedback, rather than the feedback excusing the wait.
    auto seriate = [&](const QVector<int>& blk) -> QList<int> {
        const int n = blk.size();
        QList<int> order;
        if (n <= 0) return order;
        if (n <= 2) { for (int x : blk) order.append(cids[x]); return order; }

        std::vector<double> D(static_cast<size_t>(n) * n, 0.0);
        double dmax = 0.0;
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b) {
                const double d = 0.5 * ((*M)(blk[a]+1, blk[b]+1) + (*M)(blk[b]+1, blk[a]+1));
                D[static_cast<size_t>(a)*n + b] = d;
                D[static_cast<size_t>(b)*n + a] = d;
                dmax = std::max(dmax, d);
            }
        // similarity (higher = closer); diagonal 0
        std::vector<double> S(static_cast<size_t>(n) * n, 0.0);
        for (int a = 0; a < n; ++a)
            for (int b = 0; b < n; ++b)
                S[static_cast<size_t>(a)*n + b] = (a == b) ? 0.0 : (dmax - D[static_cast<size_t>(a)*n + b]);

        // Single-linkage leaf order, shared with the Shift+S reorder.  This was
        // an inline global-argmax loop -- n rounds, each rescanning every live
        // pair, O(n^3) -- under a comment claiming it mirrored the Shift+S path.
        // It did not: Shift+S had already been moved to Prim's MST and this copy
        // was left on the algorithm that move replaced.
        //
        // The count gate splits at the MEDIAN spike count, so a block is about
        // half the session: at ~1300 clusters that was a few hundred million
        // operations per block and is now a few hundred thousand.
        std::vector<int> leafIdx = singleLinkageLeafOrder(S, n);
        if (static_cast<int>(leafIdx.size()) != n) {     // belt and braces
            leafIdx.clear();
            leafIdx.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i) leafIdx.push_back(i);
        }
        for (int leaf : leafIdx) order.append(cids[blk[leaf]]);
        return order;
    };

    QList<int> targetOrder = seriate(hi);
    targetOrder += seriate(lo);

    applySortedOrder(targetOrder, tr("residual"),
                     tr("%1 high / %2 low, threshold %3 spikes")
                         .arg(hi.size()).arg(lo.size()).arg(thr));
}




// ---------------------------------------------------------------------------
// slotReorderClustersBySimilarity — Shift+S
//
// Picks the active similarity matrix (error or template), runs single-
// linkage agglomerative clustering on it to produce a 1-D ordering where
// similar clusters end up adjacent, then renumbers the clusters so that
// the post-rename palette layout matches that ordering.  Clusters 0
// (artefact) and 1 (noise) are preserved at the front.
//
// Matrix selection
// ----------------
//   - If only one of {error, template} matrix view exists, use it.
//   - If both exist, use lastMatrixUsed (set on creation and on click).
//   - If neither exists, the action is disabled (mReorderClustersBySimilarity
//     ->setEnabled(false) at construction; enabled when the first matrix
//     dock is created in setConnections).  Guard here anyway.
//
// Algorithm: single-linkage agglomerative clustering
// --------------------------------------------------
// 1. Build a working similarity matrix S over the matrix's cluster list,
//    EXCLUDING clusters 0 and 1 (which stay pinned at the front).
// 2. While more than one alive node remains:
//      - find (i, j) with highest S[i, j] among alive nodes
//      - merge: leaves[i] ← leaves[i] + leaves[j]
//      - kill node j
//      - update S[i, k] ← max(S[i, k], S[j, k]) for every alive k
//        (single-linkage: similarity to merged node = max of constituents)
// 3. The single surviving node's leaves[] list is the leaf order.
//
// This is O(N³) on the active cluster count N, which is trivial for the
// typical N ≤ 200 we see in a single electrode group (≤ 8 MOPS).  Cluster
// IDs in the leaf order get renamed to (2, 3, 4, ...) in order, so
// neighbouring IDs after the rename are the most-similar pairs.
//
// Apply via the established applyClusterRename pipeline:
//   logBefore(RENUMBER_PARTIAL) → prepareUndo → applyClusterRename → logAfter
// — identical to the T-key (renumberClustersToEnd) flow, so the undo
// stack, curation log, palette refresh, and dock-side renumber signals
// all work without further wiring.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// reorderSpectralFiedlerOrder -- spectral seriation of the cluster similarity
// matrix.  Orders the N nodes by the Fiedler vector (the eigenvector of the
// second-smallest eigenvalue) of the similarity Laplacian L = D - W, laying
// clusters out along the dominant similarity gradient -- typically a smoother
// heatmap ordering than single-linkage, which tends to chain.
//
// W is the input matrix S clamped to non-negative affinities: template
// correlations can be negative, and a negative affinity is meaningless for a
// Laplacian, so anti-correlated pairs contribute 0.  The Fiedler vector is found
// by shifted power iteration on M = cI - L, with c = 2*max_i d_i + 1 an upper
// bound on lambda_max(L) (Gershgorin).  M's largest eigenpair is the constant
// vector (deflated out every step by subtracting the mean), so the iteration
// converges to the second -- the Fiedler vector.  Each step is one dense mat-vec
// (W*x), parallelised with OpenMP; the pragmas are unknown-pragma-safe, so this
// degrades to a correct serial computation when OpenMP is not enabled.
//
// Returns a permutation of [0, N) in Fiedler order, or an empty vector on a
// degenerate input so the caller falls back to the identity order.
static std::vector<int> reorderSpectralFiedlerOrder(const std::vector<double>& S, int N)
{
    if (N < 2) return {};

    // Non-negative degrees and the Gershgorin shift c >= lambda_max(L).
    std::vector<double> deg(static_cast<size_t>(N), 0.0);
    double maxDeg = 0.0;
    #pragma omp parallel for reduction(max:maxDeg) schedule(static)
    for (int i = 0; i < N; ++i) {
        const double* Si = &S[static_cast<size_t>(i) * N];
        double d = 0.0;
        for (int j = 0; j < N; ++j) {
            if (j == i) continue;
            const double w = Si[j];
            if (w > 0.0) d += w;
        }
        deg[i] = d;
        if (d > maxDeg) maxDeg = d;
    }
    const double c = 2.0 * maxDeg + 1.0;

    auto normalise = [&](std::vector<double>& v) -> double {
        double s = 0.0;
        for (int i = 0; i < N; ++i) s += v[i] * v[i];
        s = std::sqrt(s);
        if (s < 1e-300) return 0.0;
        const double inv = 1.0 / s;
        for (int i = 0; i < N; ++i) v[i] *= inv;
        return s;
    };

    // Deterministic, non-constant, already-mean-zero seed (a linear ramp).
    std::vector<double> x(static_cast<size_t>(N)), y(static_cast<size_t>(N));
    const double mid = 0.5 * (N - 1);
    for (int i = 0; i < N; ++i) x[i] = static_cast<double>(i) - mid;
    normalise(x);

    const int    maxIters = 1000;
    const double tol      = 1e-9;
    double prevEig = 0.0;
    for (int iter = 0; iter < maxIters; ++iter) {
        // Project x orthogonal to the all-ones vector (deflate the constant
        // eigenvector so the iteration lands on the Fiedler vector, not 1).
        double mean = 0.0;
        for (int i = 0; i < N; ++i) mean += x[i];
        mean /= N;
        for (int i = 0; i < N; ++i) x[i] -= mean;

        // y = M x = (c - deg).*x + W x   (W = S clamped to non-negative)
        #pragma omp parallel for schedule(static)
        for (int i = 0; i < N; ++i) {
            const double* Si = &S[static_cast<size_t>(i) * N];
            double acc = 0.0;
            for (int j = 0; j < N; ++j) {
                if (j == i) continue;
                const double w = Si[j];
                if (w > 0.0) acc += w * x[j];
            }
            y[i] = (c - deg[i]) * x[i] + acc;
        }

        const double eig = normalise(y);   // ~ (c - lambda_2) once converged
        x.swap(y);
        if (iter > 0 && std::fabs(eig - prevEig) <= tol * std::max(1.0, std::fabs(eig)))
            break;
        prevEig = eig;
    }

    // Order nodes by ascending Fiedler component; stable tie-break by index so
    // the result is deterministic.
    std::vector<int> order(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b){ return x[a] < x[b]; });
    return order;
}

void KlustersApp::slotReorderClustersBySimilarity()
{
    KlustersView* view = activeView();
    if (!view) return;

    // Feature-space method (Preferences -> Refinement): order clusters by their
    // fet-space centroids instead of the similarity matrix.  Needs no matrix and
    // avoids the N x N Fiedler power iteration that scales poorly at large N.
    if (configuration().getReorderMethod() == 2) {
        reorderClustersByFeatureSpace();
        return;
    }

    // ── Pick the source matrix view ─────────────────────────────────────
    ErrorMatrixView*    emv = view->findChild<ErrorMatrixView*>();
    TemplateMatrixView* tmv = view->findChild<TemplateMatrixView*>();
    const bool emvReady = (emv && emv->hasComputedData());
    const bool tmvReady = (tmv && tmv->hasComputedData());

    QList<int>           clusterIdsInMatrix;
    const Array<double>* simMatrix = nullptr;
    const char*          matrixLabel = "?";

    if (emvReady && !tmvReady) {
        clusterIdsInMatrix = emv->matrixComputedClusterList();
        simMatrix          = emv->matrixData();
        matrixLabel        = "error";
    } else if (tmvReady && !emvReady) {
        clusterIdsInMatrix = tmv->matrixClusterList();
        simMatrix          = tmv->matrixData();
        matrixLabel        = "template";
    } else if (emvReady && tmvReady) {
        // Both ready — defer to last-created-or-clicked.
        if (lastMatrixUsed == MatrixKind::TEMPLATE_MATRIX_KIND) {
            clusterIdsInMatrix = tmv->matrixClusterList();
            simMatrix          = tmv->matrixData();
            matrixLabel        = "template (last interacted)";
        } else {
            // ERROR_MATRIX_KIND or NONE (treat NONE → error as the more
            // commonly used matrix; matches the U-key default).
            clusterIdsInMatrix = emv->matrixComputedClusterList();
            simMatrix          = emv->matrixData();
            matrixLabel        = "error (last interacted)";
        }
    } else {
        QMessageBox::information(
            this, tr("Reorder Clusters by Similarity"),
            tr("Neither the error matrix nor the template matrix has been\n"
               "computed yet.  Press U to compute the error matrix (and/or\n"
               "open the template matrix), then try Shift+S again."));
        return;
    }

    if (!simMatrix) return;

    SortBusyCursor busy;   // every bail above is a message; the work starts here

    // ── Auto-update if the chosen matrix is stale ─────────────────────────
    // Reordering uses the similarity matrix as ground truth for the
    // single-linkage merge.  If the matrix is out of date (red-bordered
    // ErrorMatrixView, or isStale TemplateMatrixView) the reorder would
    // operate on cluster IDs and similarities that no longer match the
    // current cluster state — producing a wrong rename in the best case
    // and a renumberPartial reject (nRenamed < 0) in the worst.
    //
    // Strategy: hook a one-shot connection on the matrix's matrixUpdated()
    // signal that re-invokes this same slot, then trigger the update and
    // return.  On the second invocation the matrix is fresh and we fall
    // through to the algorithm.  QPointer guards against the view being
    // destroyed while we wait (dock close, document close).  The
    // single-shot connection self-disconnects so a later natural
    // matrixUpdated() (e.g. user presses U) won't accidentally re-fire
    // the reorder.
    QObject* chosenMatrixView = nullptr;
    bool     chosenIsStale    = false;
    if (emvReady && !tmvReady) {
        chosenMatrixView = emv;
        chosenIsStale    = emv->isOutOfDate();
    } else if (tmvReady && !emvReady) {
        chosenMatrixView = tmv;
        chosenIsStale    = tmv->isOutOfDate();
    } else if (emvReady && tmvReady) {
        if (lastMatrixUsed == MatrixKind::TEMPLATE_MATRIX_KIND) {
            chosenMatrixView = tmv;
            chosenIsStale    = tmv->isOutOfDate();
        } else {
            chosenMatrixView = emv;
            chosenIsStale    = emv->isOutOfDate();
        }
    }
    if (chosenIsStale && chosenMatrixView) {
        statusBar()->showMessage(
            tr("Reorder: %1 matrix is out of date; recomputing then "
               "reordering…").arg(QString::fromLatin1(matrixLabel)),
            3000);
        QPointer<QObject> guarded(chosenMatrixView);
        // Use a self-disconnecting lambda rather than a Qt::SingleShotConnection
        // for portability — SingleShotConnection requires Qt 6.0+, and
        // disconnect-by-Connection-handle works on every Qt version the
        // project supports.
        auto conn = std::make_shared<QMetaObject::Connection>();
        auto callback = [this, guarded, conn]() {
            QObject::disconnect(*conn);
            if (guarded)  // matrix view may have been closed while we waited
                this->slotReorderClustersBySimilarity();
        };
        if (auto* emvP = qobject_cast<ErrorMatrixView*>(chosenMatrixView)) {
            *conn = connect(emvP, &ErrorMatrixView::matrixUpdated,
                            this, callback);
            emvP->updateMatrixContents();
        } else if (auto* tmvP = qobject_cast<TemplateMatrixView*>(chosenMatrixView)) {
            *conn = connect(tmvP, &TemplateMatrixView::matrixUpdated,
                            this, callback);
            tmvP->updateMatrixContents();
        }
        return;
    }

    // ── Build the working order, excluding 0 and 1 (pinned at the front) ──
    // matrixCidToRow gives, for each cluster ID c in the matrix, its
    // 1-based row/column index in simMatrix (Array<T>::operator()(i,j) is
    // 1-based).  We skip cluster 0 and cluster 1 — they keep their IDs
    // and don't participate in the reorder (Data::renumberPartial would
    // reject any map that touched them).
    const int nClustersInMatrix = clusterIdsInMatrix.size();
    QMap<int,int> matrixCidToRow;
    QList<int>    nodeCids;   // cluster IDs that participate (>= 2)
    nodeCids.reserve(nClustersInMatrix);
    for (int i = 0; i < nClustersInMatrix; ++i) {
        const int cid = clusterIdsInMatrix[i];
        matrixCidToRow.insert(cid, i + 1);   // 1-based for Array<>::operator()
        if (cid >= 2) nodeCids.append(cid);
    }
    const int N = nodeCids.size();
    if (N < 2) {
        statusBar()->showMessage(
            tr("Reorder: fewer than 2 non-noise clusters in %1 matrix; "
               "nothing to reorder.").arg(QString::fromLatin1(matrixLabel)),
            3000);
        return;
    }

    // ── Local working similarity matrix S[i][j] over node indices ──────
    // Allocated as a flat vector for cache locality; small (N ≤ a few
    // hundred typically).
    std::vector<double> S(static_cast<size_t>(N) * N, 0.0);
    for (int i = 0; i < N; ++i) {
        const int row_i = matrixCidToRow.value(nodeCids[i]);
        for (int j = 0; j < N; ++j) {
            if (i == j) continue;
            const int row_j = matrixCidToRow.value(nodeCids[j]);
            // Symmetrise: max of upper/lower entries.  Either matrix is
            // computed symmetrically by its thread, but being defensive
            // here costs nothing.
            const double a = (*simMatrix)(row_i, row_j);
            const double b = (*simMatrix)(row_j, row_i);
            S[static_cast<size_t>(i) * N + j] = std::max(a, b);
        }
    }

    // ── Turn the similarity matrix into an order over node indices ──────
    // Method chosen in Preferences -> Refinement -> "Reorder-by-similarity
    // method": 0 = single-linkage (MST leaf order, default), 1 = spectral
    // (sort by the Fiedler vector of the similarity Laplacian).  Both consume
    // the same working matrix S and produce orderIdx over node indices.
    std::vector<int> orderIdx;
    if (configuration().getReorderMethod() == 1) {
        orderIdx = reorderSpectralFiedlerOrder(S, N);   // parallel power iteration
    } else {
        orderIdx = singleLinkageLeafOrder(S, N);        // O(N^2) Prim's MST
    }

    if (static_cast<int>(orderIdx.size()) != N) {
        // Belt and braces: if the chosen method produced a short or invalid
        // order, fall back to the original order rather than corrupting the
        // rename.
        orderIdx.clear();
        for (int i = 0; i < N; ++i) orderIdx.push_back(i);
    }

    // Translate leaf indices → cluster IDs in target order, then hand
    // off to the doc-layer method that owns all the undo/log/palette/
    // view bookkeeping (mirrors the renumberClustersToEnd entry point).
    QList<int> targetOrder;
    targetOrder.reserve(N);
    for (int leaf : orderIdx) targetOrder.append(nodeCids[leaf]);

    // Display-only fast path (Preferences -> Refinement -> "Reorder matrix
    // display only"): rearrange the error-matrix rows/columns in the view
    // WITHOUT renumbering clusters -- no per-spike label rewrite, no undo
    // snapshot, no matrix recompute, so it is near-instant at large N.  The
    // matrix rows include the specials (0/1); orderIdx covers only the >= 2
    // clusters, so build a display permutation over ALL rows: specials keep
    // their positions at the front, the >= 2 rows follow in similarity order.
    // Only the error matrix carries a display map today; a template/residual
    // reorder falls through to the renumber below.
    if (configuration().getReorderDisplayOnly() && emv && chosenMatrixView == emv) {
        QList<int> displayPerm;
        displayPerm.reserve(nClustersInMatrix);
        for (int i = 0; i < nClustersInMatrix; ++i)
            if (clusterIdsInMatrix[i] <= 1) displayPerm.append(i);          // specials first
        for (int leaf : orderIdx)
            displayPerm.append(matrixCidToRow.value(nodeCids[leaf]) - 1);   // 0-based matrix row
        emv->setDisplayOrder(displayPerm);
        // Mirror the same order onto the PARENT cluster palette (view-only, ids
        // unchanged) so the cluster list follows the sort too.  targetOrder is the
        // >= 2 cluster ids in similarity order; the palette pins the specials 0/1
        // and appends any unlisted cluster.  Child palettes are left untouched.
        if(clusterPalette) clusterPalette->setSimilarityOrder(targetOrder);
        statusBar()->showMessage(
            tr("Reorder: rearranged the error-matrix display (%1 rows) by "
               "similarity -- clusters were not renumbered.").arg(displayPerm.size()),
            4000);
        return;
    }

    const int nRenamed = doc->reorderClustersByPermutation(targetOrder);
    if (nRenamed < 0) {
        // Drop the cursor before the box: this dialog is the curator's to read,
        // and a wait cursor over it claims the application is still working.
        busy.restore();
        QMessageBox::warning(this, tr("Reorder Clusters by Similarity"),
            tr("Could not apply the reorder — the cluster table changed\n"
               "between the matrix computation and now, or an invalid\n"
               "permutation was produced.  No clusters were renamed."));
        return;
    }
    if (nRenamed == 0) {
        statusBar()->showMessage(
            tr("Reorder: clusters already in similarity order (%1 matrix); "
               "nothing to do.").arg(QString::fromLatin1(matrixLabel)),
            3000);
        return;
    }

    statusBar()->showMessage(
        tr("Reorder: renamed %1 clusters by %2-matrix similarity.")
            .arg(nRenamed)
            .arg(QString::fromLatin1(matrixLabel)),
        4000);

    // After renaming, both the error matrix and the template matrix are
    // keyed to stale cluster IDs.  slotUpdateErrorMatrix() recomputes the
    // error matrix and unconditionally also emits the template-matrix
    // refresh (see comment at slotUpdateErrorMatrix definition) — same
    // contract as the U key — so any visible matrix dock catches up
    // automatically.  Cheap if neither dock is open: the slot calls
    // view->updateErrorMatrix()/updateTemplateMatrix() which short-circuit
    // when their respective Qt-connected listeners are gone.
    slotUpdateErrorMatrix();
}

// ---------------------------------------------------------------------------
// reorderClustersByFeatureSpace  (reorder-by-similarity method 2)
//
// Orders the non-special clusters along the first principal component of their
// fet-space centroids, then renumbers them so feature-space-adjacent clusters
// get adjacent IDs.  Unlike the matrix methods this needs no error/template
// matrix, and unlike the spectral (Fiedler) method it does no N x N eigen-
// iteration: the only per-cluster-pair-free costs are one pass over each
// cluster's spikes (centroids, O(total_spikes * D)) and a D x D scatter/power
// iteration (D ~= tens), so it stays fast for N in the thousands where the
// N x N Fiedler power iteration does not.  PC1 is a linear seriation (the
// dominant axis of between-cluster variation); it will not block-diagonalise
// arbitrarily complex structure, but it is deterministic, connected (no zero-
// similarity graph to fragment), and cheap.
// ---------------------------------------------------------------------------
void KlustersApp::reorderClustersByFeatureSpace()
{
    if (!activeView()) return;
    Data& d = doc->data();

    // Non-special clusters (0 = artefact, 1 = noise stay pinned at the front).
    QList<int> clusters;
    const auto ids = d.clusterIds();
    for (const auto id : ids)
        if (id >= 2) clusters.append(static_cast<int>(id));
    const int N = clusters.size();
    if (N < 2) {
        slotStatusMsg(tr("Reorder (feature-space): fewer than 2 non-noise clusters; nothing to do."));
        return;
    }

    // nbOfDimensionsTotal() counts the timestamp as its last column, so the fet
    // (PCA) dimensions are 1 .. D with D = total - 1.
    const int D = d.nbOfDimensionsTotal() - 1;
    if (D < 1) {
        slotStatusMsg(tr("Reorder (feature-space): no feature dimensions available."));
        return;
    }

    SortBusyCursor busy;

    // Per-cluster centroid in fet space -- one pass over each cluster's spikes.
    // This is the method's dominant cost (the only pass over all spikes) and is
    // embarrassingly parallel over clusters: each k writes a disjoint cent[] slice
    // through its own iterator, and the feature store is read-only (Array's const
    // operator() is a plain indexed read, and Data::iterator only reads
    // clusterInfoMap), so there is no shared mutable state and no reduction.
    // schedule(dynamic,1) balances the uneven per-cluster spike counts (bursty /
    // high-rate units) by handing clusters out one at a time.  Everything after
    // this loop (D x D scatter, PC1 power iteration, projection) is O(N*D^2) with
    // D ~ tens, so it stays serial.
    std::vector<double> cent(static_cast<size_t>(N) * D, 0.0);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int k = 0; k < N; ++k) {
        double* ck = &cent[static_cast<size_t>(k) * D];
        Data::Iterator it = d.iterator(clusters[k]);
        long cnt = 0;
        while (it.hasNext()) {
            for (int dim = 0; dim < D; ++dim)
                ck[dim] += static_cast<double>(it(dim + 1));   // 1-based fet dims
            ++cnt;
            it.next();
        }
        if (cnt > 0) {
            const double inv = 1.0 / static_cast<double>(cnt);
            for (int dim = 0; dim < D; ++dim) ck[dim] *= inv;
        }
    }

    // Centre the centroids so PC1 is the dominant axis of between-cluster spread.
    std::vector<double> mean(static_cast<size_t>(D), 0.0);
    for (int k = 0; k < N; ++k) {
        const double* ck = &cent[static_cast<size_t>(k) * D];
        for (int dim = 0; dim < D; ++dim) mean[dim] += ck[dim];
    }
    for (int dim = 0; dim < D; ++dim) mean[dim] /= static_cast<double>(N);
    for (int k = 0; k < N; ++k) {
        double* ck = &cent[static_cast<size_t>(k) * D];
        for (int dim = 0; dim < D; ++dim) ck[dim] -= mean[dim];
    }

    // D x D scatter matrix over the centred centroids (D small).
    std::vector<double> cov(static_cast<size_t>(D) * D, 0.0);
    for (int k = 0; k < N; ++k) {
        const double* ck = &cent[static_cast<size_t>(k) * D];
        for (int a = 0; a < D; ++a) {
            const double va = ck[a];
            if (va == 0.0) continue;
            double* row = &cov[static_cast<size_t>(a) * D];
            for (int b = 0; b < D; ++b) row[b] += va * ck[b];
        }
    }

    // PC1 = leading eigenvector of the scatter matrix by power iteration.  D x D
    // is tiny, so this is negligible (the point of the method: no N x N iterate).
    std::vector<double> v(static_cast<size_t>(D), 1.0 / std::sqrt(static_cast<double>(D)));
    std::vector<double> t(static_cast<size_t>(D), 0.0);
    for (int iter = 0; iter < 256; ++iter) {
        for (int a = 0; a < D; ++a) {
            const double* row = &cov[static_cast<size_t>(a) * D];
            double acc = 0.0;
            for (int b = 0; b < D; ++b) acc += row[b] * v[b];
            t[a] = acc;
        }
        double nrm = 0.0;
        for (int dim = 0; dim < D; ++dim) nrm += t[dim] * t[dim];
        nrm = std::sqrt(nrm);
        if (nrm < 1e-300) break;                       // degenerate: centroids coincide
        const double inv = 1.0 / nrm;
        double dot = 0.0;
        for (int dim = 0; dim < D; ++dim) { t[dim] *= inv; dot += t[dim] * v[dim]; }
        v.swap(t);
        if (std::fabs(std::fabs(dot) - 1.0) < 1e-12) break;   // converged
    }

    // Project each centroid onto PC1 -> per-cluster scalar; stable-sort ascending.
    QHash<int,double> proj;
    proj.reserve(N);
    for (int k = 0; k < N; ++k) {
        const double* ck = &cent[static_cast<size_t>(k) * D];
        double pp = 0.0;
        for (int dim = 0; dim < D; ++dim) pp += ck[dim] * v[dim];
        proj.insert(clusters[k], pp);
    }
    std::stable_sort(clusters.begin(), clusters.end(),
        [&proj](int a, int b){ return proj.value(a) < proj.value(b); });

    const int nRenamed = doc->reorderClustersByPermutation(clusters);
    if (nRenamed < 0)
        slotStatusMsg(tr("Reorder (feature-space): reorder rejected (cluster set changed?)."));
    else
        slotStatusMsg(tr("Reordered %1 clusters by feature-space layout (PC1 of fet centroids).").arg(nRenamed));
}

// ---------------------------------------------------------------------------
// slotSortByWaveformNN  --  nearest-neighbour median-waveform sort
//
// Renumbers the non-special clusters along a greedy nearest-neighbour chain over
// their per-sample MEDIAN waveforms, so waveform-similar clusters get adjacent
// ids.  Unlike the matrix reorders this reads no error/template matrix -- it
// pulls each cluster's spikes straight from the .spk file and takes a robust
// per-sample median (resistant to the odd artefact spike, which a mean is not).
//
// Cost: the read + median is O(total_spikes * nPts) -- the dominant, disk-bound
// term, parallelised over clusters with per-thread FILE handles (mirrors the
// residual thread) -- while the N x N median-waveform distance matrix and the
// chain are O(N^2 * nPts) / O(N^2), fine for moderate N and only the slow part
// at very large N.  Each worker holds one cluster's spikes at a time to take the
// per-sample median.  All under a wait cursor.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// computeMedianWaveformDistances  --  shared by the two median-waveform sorts
//
// clustersOut <- non-special clusters (id >= 2); distOut <- their N x N Euclidean
// distance matrix over per-sample MEDIAN waveforms (read from the .spk file,
// robust to the odd artefact spike that a mean would follow).  Returns false,
// after a status message, if there is nothing to sort or the .spk data is
// unavailable.  Callers wrap the call in a wait cursor.  Cost: the read/median is
// O(total_spikes * nPts) (disk-bound, parallelised over clusters with per-worker
// FILE handles, mirroring the residual thread); the distance matrix O(N^2 * nPts);
// each worker holds one cluster's spikes at a time for the per-sample median.
// ---------------------------------------------------------------------------
//////////////////////////////////////////////////////////////////////////////
// clustersToSort / applySortedOrder
//
// The metric sorts (spike count, time, contamination, SNR, amplitude, amplitude
// by channel) were the same fourteen lines each, wrapped around three that
// differed: gather the non-noise clusters, bail if there are fewer than two,
// stable_sort by some metric, hand the order to the doc, report. The copies had
// already drifted -- the doc/view guard is spelled two ways, and elsewhere in this
// file two copies of single-linkage diverged by a whole order of complexity while
// one claimed to mirror the other. Shared skeleton, so there is one thing to fix
// when it is wrong and one wording to read when it speaks.
//
// What stays in each slot is what actually differs: the metric and its order.
//////////////////////////////////////////////////////////////////////////////
QList<int> KlustersApp::clustersToSort(const QAction* action, const QString& sortName)
{
    QList<int> clusters;
    if (action && !action->isEnabled()) return clusters;
    if (!activeView()) return clusters;

    const auto ids = doc->data().clusterIds();
    for (const auto id : ids)
        if (id >= 2) clusters.append(static_cast<int>(id));   // 0/1 are noise/artefact

    if (clusters.size() < 2) {
        slotStatusMsg(tr("Sort by %1: fewer than 2 non-noise clusters; nothing to sort.")
                          .arg(sortName));
        clusters.clear();
        return clusters;
    }
    return clusters;
}

void KlustersApp::applySortedOrder(const QList<int>& order, const QString& sortName,
                                   const QString& detail)
{
    if (order.isEmpty()) return;
    const int nRenamed = doc->reorderClustersByPermutation(order);
    if (nRenamed < 0)
        slotStatusMsg(tr("Sort by %1: reorder rejected (cluster set changed?).")
                          .arg(sortName));
    else
        slotStatusMsg(tr("Sorted %1 clusters by %2 (%3).")
                          .arg(nRenamed).arg(sortName, detail));
}

//////////////////////////////////////////////////////////////////////////////
// beginSortProgress / updateSortProgress / endSortProgress
//
// One status-bar progress bar, shared by every long operation that wants one.
// Extracted from computeMedianWaveformDistances, which was the only caller and
// therefore the only place that knew how to build the thing.
//
// processEvents() with ExcludeUserInputEvents is the load-bearing detail: the
// bar repaints, but keyboard and mouse events are NOT delivered, so the user
// cannot re-enter the running operation through the menu or a shortcut while it
// pumps. Without that exclusion these calls would turn every long operation into
// a re-entrancy hazard.
//////////////////////////////////////////////////////////////////////////////
void KlustersApp::beginSortProgress(const QString& format)
{
    // The OUTERMOST caller owns the bar.  A nested begin (see sortProgressDepth)
    // must not relabel it or reset it to 0%, and its matching end must not hide
    // it out from under the operation that is really running.
    if (sortProgressDepth++ > 0) return;

    if (!sortProgressBar) {
        sortProgressBar = new QProgressBar(this);
        sortProgressBar->setObjectName(QStringLiteral("sortProgress"));
        sortProgressBar->setTextVisible(true);
        sortProgressBar->setMaximumWidth(280);
        statusBar()->addPermanentWidget(sortProgressBar);
    }
    sortProgressBar->setRange(0, 100);
    sortProgressBar->setFormat(format);
    sortProgressBar->setValue(0);
    sortProgressBar->show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void KlustersApp::updateSortProgress(int percent)
{
    if (!sortProgressBar) return;
    if (sortProgressDepth > 1) return;      // a nested operation does not drive the bar
    sortProgressBar->setValue(qBound(0, percent, 100));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void KlustersApp::endSortProgress()
{
    if (sortProgressDepth > 0) --sortProgressDepth;
    if (sortProgressDepth > 0) return;      // an outer operation still owns it
    if (!sortProgressBar) return;
    sortProgressBar->hide();
}

bool KlustersApp::computeMedianWaveformDistances(QList<int>& clustersOut,
                                                 std::vector<float>& distOut)
{
    if (!activeView()) return false;
    Data& d = doc->data();

    // Non-special clusters (0 = artefact, 1 = noise stay pinned at the front).
    QList<int> clusters;
    const auto ids = d.clusterIds();
    for (const auto id : ids)
        if (id >= 2) clusters.append(static_cast<int>(id));
    const int N = clusters.size();
    if (N < 2) {
        slotStatusMsg(tr("Sort by waveform: fewer than 2 non-noise clusters; nothing to do."));
        return false;
    }

    const int nChan = d.nbOfChannels();
    const int nSamp = d.nbSamplesPerWaveform();
    const int nPts  = nChan * nSamp;
    if (nPts < 1) {
        slotStatusMsg(tr("Sort by waveform: no waveform samples available."));
        return false;
    }
    const QString spkPath = d.getSpkFileName();
    if (spkPath.isEmpty()) {
        slotStatusMsg(tr("Sort by waveform: no .spk file available."));
        return false;
    }


    // Pre-fetch each cluster's 0-based .spk file indices (serial; Data access is
    // not thread-safe).  posTable(1,s+1) is the 1-based .spk position.
    std::vector<std::vector<int>> allFileIdx(static_cast<size_t>(N));
    for (int k = 0; k < N; ++k) {
        SortableTable posTable;
        if (!d.spikePositions(clusters[k], posTable)) continue;
        const long nSpk = static_cast<long>(d.nbOfSpikes(clusters[k]));
        allFileIdx[static_cast<size_t>(k)].reserve(static_cast<size_t>(nSpk));
        for (long s = 0; s < nSpk; ++s)
            allFileIdx[static_cast<size_t>(k)].push_back(
                static_cast<int>(posTable(1, s + 1)) - 1);
    }

    // Per-cluster median waveform (channel-major, length nPts).  Parallel over
    // clusters, each worker with its own FILE handle.
    std::vector<std::vector<float>> medWav(
        static_cast<size_t>(N), std::vector<float>(static_cast<size_t>(nPts), 0.0f));
    const QByteArray spkBytes = spkPath.toLocal8Bit();
    const char*      spkCStr  = spkBytes.constData();

    // Progress bar for the dominant, disk-bound per-cluster median-read phase.
    // Clusters are read in chunks (chunk >= the OpenMP width, so each chunk
    // still fills the workers) and the bar advances between chunks; only the
    // cross-chunk load-steal is traded away for the progress feedback.
    beginSortProgress(tr("Sorting by waveform… %p%"));

#ifdef _OPENMP
    const int nThreads = std::max(1, omp_get_max_threads());
#else
    const int nThreads = 1;
#endif
    const int chunkSize = std::max(nThreads, (N + 23) / 24);   // <= ~24 ticks
    for (int cstart = 0; cstart < N; cstart += chunkSize) {
        const int cend = std::min(cstart + chunkSize, N);
    #pragma omp parallel for schedule(dynamic, 1)
    for (int k = cstart; k < cend; ++k) {
        const auto& fidx = allFileIdx[static_cast<size_t>(k)];
        const long  nSpk = static_cast<long>(fidx.size());
        if (nSpk == 0) continue;

        FILE* spk = fopen(spkCStr, "rb");
        if (!spk) continue;

        // Pack valid spikes contiguously as [valid][nPts]; then median each column.
        std::vector<float>   buf(static_cast<size_t>(nSpk) * nPts);
        std::vector<int16_t> raw;
        std::vector<float>   sp;
        long valid = 0;
        for (long s = 0; s < nSpk; ++s) {
            if (!tmReadSpikeFloat(spk, fidx[static_cast<size_t>(s)], nChan, nSamp, raw, sp))
                continue;
            std::copy(sp.begin(), sp.begin() + nPts,
                      buf.begin() + static_cast<size_t>(valid) * nPts);
            ++valid;
        }
        fclose(spk);
        if (valid == 0) continue;

        std::vector<float> colv(static_cast<size_t>(valid));
        const size_t mid = static_cast<size_t>(valid) / 2;
        for (int p = 0; p < nPts; ++p) {
            for (long s = 0; s < valid; ++s)
                colv[static_cast<size_t>(s)] = buf[static_cast<size_t>(s) * nPts + p];
            std::nth_element(colv.begin(), colv.begin() + mid, colv.end());
            medWav[static_cast<size_t>(k)][static_cast<size_t>(p)] = colv[mid];
        }
    }
        updateSortProgress((90 * cend) / N);          // median read -> [0,90]
    }

    // N x N Euclidean distance matrix over the median waveforms (parallel rows).
    std::vector<float> dist(static_cast<size_t>(N) * N, 0.0f);
    #pragma omp parallel for schedule(dynamic, 8)
    for (int a = 0; a < N; ++a) {
        const float* wa = medWav[static_cast<size_t>(a)].data();
        for (int b = a + 1; b < N; ++b) {
            const float* wb = medWav[static_cast<size_t>(b)].data();
            double acc = 0.0;
            for (int p = 0; p < nPts; ++p) {
                const double diff = static_cast<double>(wa[p]) - static_cast<double>(wb[p]);
                acc += diff * diff;
            }
            const float dd = static_cast<float>(std::sqrt(acc));
            dist[static_cast<size_t>(a) * N + b] = dd;
            dist[static_cast<size_t>(b) * N + a] = dd;
        }
    }

    updateSortProgress(100);                  // distance phase done
    endSortProgress();

    clustersOut = clusters;
    distOut     = std::move(dist);
    return true;
}

// ---------------------------------------------------------------------------
// slotSortByWaveformNN  --  greedy nearest-neighbour median-waveform sort (LOCAL)
//
// Places each cluster next to its most waveform-similar unused neighbour.  Each
// step is locally optimal, but there is no global objective, so when a local
// neighbourhood is exhausted the chain can jump.  For a sort that also respects
// GLOBAL waveform structure, see slotSortByWaveformSpectral.
// ---------------------------------------------------------------------------
void KlustersApp::slotSortByWaveformNN()
{
    QList<int>         clusters;
    std::vector<float> dist;
    // These two already set a wait cursor by hand and popped it at each exit.  Same
    // cursor, now scoped, so a new early return cannot forget to pop it.
    SortBusyCursor busy;
    if (!computeMedianWaveformDistances(clusters, dist)) return;
    const int N = clusters.size();

    // Greedy nearest-neighbour chain: start at the first cluster, then repeatedly
    // append the nearest not-yet-placed cluster.  O(N^2), deterministic.
    std::vector<char> visited(static_cast<size_t>(N), 0);
    QList<int> ordered;
    ordered.reserve(N);
    int cur = 0;
    visited[0] = 1;
    ordered.append(clusters[0]);
    for (int step = 1; step < N; ++step) {
        const float* dc = &dist[static_cast<size_t>(cur) * N];
        int   best  = -1;
        float bestD = std::numeric_limits<float>::max();
        for (int j = 0; j < N; ++j) {
            if (visited[static_cast<size_t>(j)]) continue;
            if (dc[j] < bestD) { bestD = dc[j]; best = j; }
        }
        if (best < 0) break;
        visited[static_cast<size_t>(best)] = 1;
        ordered.append(clusters[best]);
        cur = best;
    }
    // Belt and braces: append anything the chain somehow missed.
    if (ordered.size() != N)
        for (int k = 0; k < N; ++k)
            if (!visited[static_cast<size_t>(k)]) ordered.append(clusters[k]);

    const int nRenamed = doc->reorderClustersByPermutation(ordered);
    if (nRenamed < 0)
        slotStatusMsg(tr("Reorder (waveform NN): reorder rejected (cluster set changed?)."));
    else
        slotStatusMsg(tr("Reordered %1 clusters by nearest-neighbour median-waveform chain.").arg(nRenamed));
}

// ---------------------------------------------------------------------------
// slotSortByWaveformSpectral  --  spectral (Fiedler) median-waveform sort (GLOBAL)
//
// Same median-waveform distances as the nearest-neighbour sort, but orders the
// clusters by the Fiedler vector of the waveform-SIMILARITY Laplacian instead of
// a greedy chain.  The Fiedler ordering minimises a GLOBAL objective -- the
// similarity-weighted sum of squared position differences -- so the whole layout
// respects the global waveform structure (which groups of clusters sit where),
// not just each cluster's nearest neighbour, and it avoids the greedy chain's
// jumps.  Similarity is (dmax - distance), diagonal 0, matching the matrix
// reorders; the Fiedler solve reuses the same routine as the Shift+S spectral
// path.  Cost adds the N x N Fiedler power iteration on top of the shared median
// pass; both are under the wait cursor.
// ---------------------------------------------------------------------------
void KlustersApp::slotSortByWaveformSpectral()
{
    QList<int>         clusters;
    std::vector<float> dist;
    // These two already set a wait cursor by hand and popped it at each exit.  Same
    // cursor, now scoped, so a new early return cannot forget to pop it.
    SortBusyCursor busy;
    if (!computeMedianWaveformDistances(clusters, dist)) return;
    const int N = clusters.size();

    // Distance -> similarity (dmax - d, diagonal 0), then Fiedler seriation.
    float dmax = 0.0f;
    for (const float x : dist) if (x > dmax) dmax = x;
    std::vector<double> S(static_cast<size_t>(N) * N, 0.0);
    for (int i = 0; i < N; ++i) {
        const float* di = &dist[static_cast<size_t>(i) * N];
        double*      Si = &S   [static_cast<size_t>(i) * N];
        for (int j = 0; j < N; ++j)
            Si[j] = (i == j) ? 0.0 : static_cast<double>(dmax - di[j]);
    }

    const std::vector<int> order = reorderSpectralFiedlerOrder(S, N);
    QList<int> ordered;
    ordered.reserve(N);
    if (static_cast<int>(order.size()) == N)
        for (const int idx : order) ordered.append(clusters[idx]);
    else                                   // Fiedler failed -> leave order unchanged
        ordered = clusters;

    const int nRenamed = doc->reorderClustersByPermutation(ordered);
    if (nRenamed < 0)
        slotStatusMsg(tr("Reorder (waveform spectral): reorder rejected (cluster set changed?)."));
    else
        slotStatusMsg(tr("Reordered %1 clusters by spectral (Fiedler) median-waveform seriation.").arg(nRenamed));
}
