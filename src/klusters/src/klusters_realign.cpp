/* klusters_realign.cpp -- KlustersApp spike-realignment UI / batch driver.
 * Split out of klusters.cpp (one KlustersApp class, many .cpp files; the
 * Q_OBJECT lives in klusters.h so moc is unaffected).  Same include set as
 * klusters.cpp; static members are defined once there. */

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
#include <QProgressBar>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
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

void KlustersApp::slotRealignSpikes()
{

    // Don't allow a second realignment while one is running.
    if (realignRunning) {
        // Status bar, not a modal.
        //
        // This guard fires overwhelmingly from the post-mutation automation, which
        // schedules a realignment after every edit and can reach here while the
        // previous one is still running.  A QMessageBox for that is wrong twice
        // over: the user did not ask for the realignment, so being interrupted to
        // acknowledge that it was skipped is noise; and the modal takes focus and
        // does not give it back, which is where the child palette's focus has been
        // going.  The focus trace showed it directly -- two
        // QPushButton[qt_msgbox_buttonbox < QMessageBox] hops and then (none),
        // after which nothing in the palette had focus or selection.
        //
        // A user who invokes a realignment manually while one is running gets the
        // same information without losing their place.
        statusBar()->showMessage(
            tr("A realignment is already running — use Abort Realignment to cancel it."),
            4000);
        return;
    }

    // Use the first cluster currently shown in the active view.
    const QList<int>& shown = activeView()->clusters();
    int clusterId = -1;
    for (int c : shown) {
        if (c > 1) { clusterId = c; break; }
    }
    if (clusterId < 0) {
        QMessageBox::information(this, tr("Realign Spikes"),
            tr("Please select a cluster (cluster > 1) in the active display first."));
        return;
    }

    // Run directly with the current saved settings — no pre-flight dialog.
    startRealignForCluster(clusterId);
}

// ---------------------------------------------------------------------------
// startRealignForCluster
//
// Locks the UI and launches the background realignment worker for one cluster,
// using the current saved settings
// (buildRealignArgs() — post-alignment mode incl. --pca-refine and the top-ch
// gate are all encoded there).  No dialog is shown; this is invoked directly
// by slotRealignSpikes and by the auto-align-after-merge path in
// slotGroupClusters.  Callers must ensure doc != nullptr and
// realignRunning == false.
// ---------------------------------------------------------------------------
void KlustersApp::startRealignForCluster(int clusterId)
{
    realignArgs = buildRealignArgs();   // saved mode + top-ch gate

    // ── Lock UI exactly as reclustering does ─────────────────────────────────
    realignRunning = true;
    slotStateChanged(QStringLiteral("realignState"));

    // ── Launch background worker ──────────────────────────────────────────────
    // Clean up any leftover thread from a previous run.
    if (realignThread) {
        realignThread->quit();
        realignThread->wait(2000);
        delete realignThread;
        realignThread = nullptr;
    }
    if (realignWorker) {
        delete realignWorker;
        realignWorker = nullptr;
    }

    enqueueRealignJob(clusterId, realignArgs);
}

// ---------------------------------------------------------------------------
// enqueueRealignJob
//
// The single-cluster realign runs through a SerialJobQueue as a RealignJob —
// the one and only single-cluster path (the legacy direct startRealignWorker
// spin-up has been removed).
//
// The RealignJob owns the worker/thread lifecycle; the finished callback here
// clears the UI-lock flag and then runs applyRealignResult (the result-handling
// body).  Because the job holds the queue's lane for the whole realign, a
// renumber/matrix step queued after it cannot touch Data until the realignment
// has settled — the serialisation the merge race needed.
//
// The batch path (Align All) is separate: startRealignBatchWorker /
// slotRealignBatchFinished, which still use realignThread/realignWorker.
//
// The legacy guards (realignRunning, autoPostMerge hand-off,
// processWidget->isRunning()) are still in place; retiring them is a separate
// step (the post-merge renumber/matrix work has to move onto the queue first).
// ---------------------------------------------------------------------------
void KlustersApp::enqueueRealignJob(int clusterId, const QString& args)
{
    if (!realignQueue)
        realignQueue = new SerialJobQueue(this);

    // applyRealignResult reads realignClusterId for the view refresh.
    realignClusterId = clusterId;

    auto* job = new RealignJob(
        this, doc, clusterId, args,
        // onFinished: clear the UI-lock flag, then run the result-handling body.
        [this](bool ok, int nShifted, int nSwapped,
               QVector<float> meanBefore, QVector<float> meanAfter,
               QString backupBase, int nChan, int nSamp) {
            realignRunning = false;
            applyRealignResult(ok, nShifted, nSwapped, meanBefore, meanAfter,
                               backupBase, nChan, nSamp);
        });
    job->setVerbose(configuration().getRealignVerbose());

    realignQueue->enqueue(job);
}

void KlustersApp::startRealignBatchWorker(const QList<int>& clusterIds,
                                          const QString& launchArgs)
{
    // One worker, batch mode: it loops realignSpikes over the whole list in
    // this single thread (see RealignWorker::setBatch / run).  The clusterId
    // ctor arg is unused in batch mode.
    auto* worker = new RealignWorker(doc, /*clusterId*/-1, launchArgs);
    worker->setVerbose(configuration().getRealignVerbose());
    worker->setBatch(clusterIds);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    // Per-cluster progress (progress bar + counters) and the one-time finalise.
    connect(worker, &RealignWorker::clusterDone,
            this, &KlustersApp::slotRealignClusterDone,
            Qt::QueuedConnection);
    connect(worker, &RealignWorker::finished,
            this, &KlustersApp::slotRealignBatchFinished,
            Qt::QueuedConnection);

    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::started, worker, &RealignWorker::run);

    realignWorker    = worker;
    realignThread    = thread;
    realignClusterId = -1;
    thread->start();
}

// Per-cluster progress for the single batch worker.  The realignSpikes call
// already streamed this cluster's log lines via logLine; here we only update
// the counters / progress bar and record the cluster for the deferred refresh.
void KlustersApp::slotRealignClusterDone(int pos, int /*total*/, int clusterId,
                                         bool ok, int nShifted)
{
    if (ok) {
        doc->setModified(true);
        realignBatchTouched.append(clusterId);   // refresh deferred to batch end
        realignBatchAccepted++;
        realignBatchShiftedTotal += nShifted;
    } else {
        realignBatchFailed++;
    }
    if (realignProgressBar)
        realignProgressBar->setValue(pos);
    slotStatusMsg(tr("PCA-Center align: %1 / %2 clusters …")
                  .arg(pos).arg(realignBatchTotal));
}

// One-time finalise when the single batch worker has processed the whole list.
void KlustersApp::slotRealignBatchFinished(bool /*ok*/, int /*nShifted*/,
                                           int /*nSwapped*/,
                                           QVector<float> /*meanBefore*/,
                                           QVector<float> /*meanAfter*/,
                                           QString /*backupBase*/,
                                           int /*nChan*/, int /*nSamp*/)
{
    realignRunning = false;

    if (realignThread) {
        realignThread->quit();
        realignThread->wait(2000);
        delete realignThread;
        realignThread = nullptr;
    }
    realignWorker = nullptr;   // already deleteLater'd

    // Defensive: only finalise once (the worker emits finished exactly once).
    if (!realignBatchActive)
        return;
    realignBatchActive = false;

    doc->endRealignBatchLog();    // commit the single batch "after" snapshot
    flushRealignBatchRefresh();   // one deferred view refresh for all clusters

    if (realignProgressBar) realignProgressBar->hide();
    slotStatusMsg(tr("PCA-Center align complete: %1 accepted, %2 failed, "
                     "%3 spike(s) shifted total.")
                  .arg(realignBatchAccepted)
                  .arg(realignBatchFailed)
                  .arg(realignBatchShiftedTotal));
    slotStateChanged(QStringLiteral("noRealignState"));
    // Post-edit auto-realign only: reapply the renumber the inline path would
    // otherwise have done.  A manual PCA-Center Align All does not renumber.
    if (realignPostOpRenumberMatrix) {
        realignPostOpRenumberMatrix = false;
        if (realignPostOpSetChanged && configuration().getAutoRenumberAfterMerge())
            doc->renumberClusters();
    }
    // The batch realign shifted spikes, so waveforms and the reprojected features
    // changed and the error / template / residual matrices are now stale.
    // Recompute them when the "auto-update matrices after cluster edits"
    // preference is on — for a manual PCA-Center Align All as well as a post-op
    // realign, mirroring the inline-edit path.  slotUpdateErrorMatrix() also
    // refreshes the template matrix (view->updateTemplateMatrix) and no-ops for
    // any matrix view that isn't open.
    if (configuration().getAutoUpdateMatricesAfterMerge())
        slotUpdateErrorMatrix();
    // Switch back to the Overview Display so the user can immediately
    // arrow-key through clusters and see updated waveforms.
    if (tabsParent) {
        for (int i = 0; i < tabsParent->count(); ++i) {
            if (tabsParent->tabText(i).contains(tr("Overview"),
                                                Qt::CaseInsensitive)) {
                tabsParent->setCurrentIndex(i);
                break;
            }
        }
    }
    updateUndoRedoDisplay();
    // Authoritative final step: land on the fibers the op produced (ids already
    // translated through the renumber above).  No-op if nothing is pending.
    applyPendingFiberSelection();
}

// ---------------------------------------------------------------------------
// startPostOpRealign
//
// Launch a batch realign of the parent fibers an edit just created/modified.
// Mirrors the slotPcaAlignAllClusters launch (args derivation, progress widget,
// batch-state init, realign lock, worker start) but scoped to the given fibers
// and with no confirm dialog.  Sets realignPostOpRenumberMatrix so the batch
// finish handler runs the renumber + matrix recompute.
//
// Preconditions (checked by the caller, autoPostClusterEdit): doc valid, fibers
// non-empty and already filtered to existing members with id>1, and the realign
// lane idle (!realignRunning && !realignBatchActive).
// ---------------------------------------------------------------------------
void KlustersApp::startPostOpRealign(const QList<int>& fibers, bool setChanged)
{
    if (fibers.isEmpty()) return;
    // Refresh the saved-mode args (top-ch gate + --pca-refine) like the
    // single-cluster and Align-All paths, then derive the batch args: strip any
    // top-ch / pca-refine tokens and re-add for the current top-channel count.
    realignArgs = buildRealignArgs();
    const int topCh = realignTopChanSpinBox ? realignTopChanSpinBox->value() : 2;
    {
        const QStringList toks =
            realignArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QStringList kept;
        for (qsizetype i = 0; i < toks.size(); ++i) {
            const QString& tok = toks[i];
            if (tok == QStringLiteral("--topchannels") || tok == QStringLiteral("-k")) {
                ++i;                  // also skip the value
                continue;
            }
            if (tok == QStringLiteral("--pca-refine") || tok == QStringLiteral("-p")) {
                continue;
            }
            kept << tok;
        }
        realignBatchArgs = kept.join(QLatin1Char(' ')).trimmed();
        if (!realignBatchArgs.isEmpty()) realignBatchArgs += QLatin1Char(' ');
        realignBatchArgs +=
            QStringLiteral("--topchannels %1 --pca-refine").arg(topCh);
    }
    // Status-bar progress widget (the same one Align-All uses).
    if (!realignProgressBar) {
        realignProgressBar = new QProgressBar(this);
        realignProgressBar->setObjectName(QStringLiteral("realignProgress"));
        realignProgressBar->setTextVisible(true);
        realignProgressBar->setMaximumWidth(280);
        statusBar()->addPermanentWidget(realignProgressBar);
    }
    realignProgressBar->setRange(0, fibers.size());
    realignProgressBar->setValue(0);
    realignProgressBar->setFormat(tr("Auto-realign: %v / %m fiber(s)"));
    realignProgressBar->show();
    // Clean up any leftover thread / worker from a prior single-cluster run.
    if (realignThread) {
        realignThread->quit();
        realignThread->wait(2000);
        delete realignThread;
        realignThread = nullptr;
    }
    if (realignWorker) {
        delete realignWorker;
        realignWorker = nullptr;
    }
    // Initialise batch state and launch ONE worker for the whole list.
    realignBatchActive          = true;
    realignBatchTotal           = fibers.size();
    realignBatchAccepted        = 0;
    realignBatchFailed          = 0;
    realignBatchShiftedTotal    = 0;
    realignBatchTouched.clear();
    realignPostOpRenumberMatrix = true;       // run renumber+matrix on batch finish
    realignPostOpSetChanged     = setChanged;
    doc->beginRealignBatchLog(fibers);
    realignRunning = true;
    slotStateChanged(QStringLiteral("realignState"));
    slotStatusMsg(tr("Auto-realign: 0 / %1 fiber(s) …").arg(fibers.size()));
    startRealignBatchWorker(fibers, realignBatchArgs);
}

// ---------------------------------------------------------------------------
// applyPendingFiberSelection
//
// Last step of the post-edit flow: land the selection on the fibers the
// operation produced/kept, in parent scope.  Runs after the async realign +
// renumber, so the doc's pending ids have already been translated through the
// renumber map and point at the final fibers.  Mirrors the selection path
// renumberClusters uses (palette selectItems + shownClustersUpdate).
//
// Atom-only ops leave the pending set empty and select their new atoms via the
// hierarchyChildrenCreated handler at op time instead.
// ---------------------------------------------------------------------------
void KlustersApp::applyPendingFiberSelection()
{
    if (!activeView()) return;
    const QList<int> pending = doc->takePendingFiberSelection();
    if (pending.isEmpty()) return;
    QList<int> fibers;
    for (int f : pending)
        if (f > 1 && doc->clusterHasMembers(f))
            fibers.append(f);
    if (fibers.isEmpty()) return;
    doc->setActiveClustering(false);                 // parent scope
    if (clusterPalette) {
        clusterPalette->selectItems(fibers);         // first id is the primary
        clusterPalette->setFocusToList();
    }
    doc->shownClustersUpdate(fibers, *activeView());
}

// ---------------------------------------------------------------------------
// flushRealignBatchRefresh
//
// One-shot view refresh for all clusters accepted during a PCA-Center Align
// All batch.  During the batch each cluster's refresh is deferred (only its id
// is recorded) because forceClusterRefresh emits spikesAddedToCluster, which
// puts every shown sub-view into REDRAW and launches waveform/correlogram
// threads against the .spk.pending — doing that per cluster across ~1000
// clusters was the dominant inter-cluster cost.  Invalidate all caches first,
// then fire one refresh pass so the views re-read the committed data once.
// ---------------------------------------------------------------------------
void KlustersApp::flushRealignBatchRefresh()
{
    if (realignBatchTouched.isEmpty())
        return;
    for (int id : realignBatchTouched) {
        doc->invalidateWaveformCache(id);
        doc->invalidateCorrelogramCache(id);
        // Same as the single-cluster path: each batch-realigned cluster's
        // features were reprojected, so mark it changed for the error matrix.
        doc->notifyClusterFeaturesReprojected(id);
    }
    for (int id : realignBatchTouched)
        doc->forceClusterRefresh(id);
    realignBatchTouched.clear();
}

// ---------------------------------------------------------------------------
// slotPcaAlignAllClusters
//
// Iterates every cluster ID > 1 (skipping noise=0 and artifact=1) and runs
// PCA-centered spike realignment on each, sequentially, using the channel count
// from the Top-Channels spin box (--topchannels N; 0 = all channels).
// A single batch worker loops over the list (startRealignBatchWorker); here we
// only set up state and launch it.
// ---------------------------------------------------------------------------
void KlustersApp::slotPcaAlignAllClusters()
{

    if (realignRunning) {
        // Status bar, not a modal.
        //
        // This guard fires overwhelmingly from the post-mutation automation, which
        // schedules a realignment after every edit and can reach here while the
        // previous one is still running.  A QMessageBox for that is wrong twice
        // over: the user did not ask for the realignment, so being interrupted to
        // acknowledge that it was skipped is noise; and the modal takes focus and
        // does not give it back, which is where the child palette's focus has been
        // going.  The focus trace showed it directly -- two
        // QPushButton[qt_msgbox_buttonbox < QMessageBox] hops and then (none),
        // after which nothing in the palette had focus or selection.
        //
        // A user who invokes a realignment manually while one is running gets the
        // same information without losing their place.
        statusBar()->showMessage(
            tr("A realignment is already running — use Abort Realignment to cancel it."),
            4000);
        return;
    }

    // Build the cluster list — skip 0 (noise) and 1 (artifact).  clusterIds()
    // returns a QMap key list, so it's already sorted ascending; we iterate in
    // that order to give the user a predictable progression in the log.
    QList<int> clusters;
    {
        const QList<dataType> ids = doc->data().clusterIds();
        for (dataType id : ids) {
            if (id > 1) clusters.append(static_cast<int>(id));
        }
    }
    if (clusters.isEmpty()) {
        QMessageBox::information(this, tr("PCA-Center Align All Clusters"),
            tr("No clusters with id > 1 found in the current document."));
        return;
    }

    // Channel count comes from the Top-Channels spin box (shared with the
    // single-cluster realign).  0 = use all channels.
    const int topCh = realignTopChanSpinBox ? realignTopChanSpinBox->value() : 2;
    const QString chanDesc = (topCh > 0)
        ? tr("the top %1 channel(s)").arg(topCh)
        : tr("all channels");

    // Confirm — this commits a pending change to every cluster.  No per-cluster
    // review dialog is shown during the batch, so we want the user to opt in
    // up front rather than discover the commit mid-flight.
    const QString question = tr(
        "Run PCA-centered spike alignment on %1 cluster(s) using %2 "
        "per cluster?\n\n"
        "Each cluster's result is auto-accepted as a pending change. "
        "Save the document to commit or close without saving to discard "
        "the batch.  Use \"Abort Realignment\" to stop mid-batch.")
        .arg(clusters.size())
        .arg(chanDesc);
    if (QMessageBox::question(this, tr("PCA-Center Align All Clusters"),
                              question,
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes) {
        return;
    }

    // Build the args string for every worker invocation in this batch.  Start
    // from realignArgs (carries the user's threshold / iterations / maxshift
    // preferences) and normalise --topchannels and --pca-refine: the channel
    // count comes from the Top-Channels spin box (topCh, 0 = all channels) and
    // refine is forced on (this action is defined as PCA-centred).  Stripping
    // any pre-existing instance before appending avoids duplicate tokens that
    // the parser would silently last-write-wins.
    {
        const QStringList toks =
            realignArgs.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        QStringList kept;
        for (qsizetype i = 0; i < toks.size(); ++i) {
            const QString& tok = toks[i];
            if (tok == QStringLiteral("--topchannels") || tok == QStringLiteral("-k")) {
                ++i;                  // also skip the value
                continue;
            }
            if (tok == QStringLiteral("--pca-refine") || tok == QStringLiteral("-p")) {
                continue;
            }
            kept << tok;
        }
        realignBatchArgs = kept.join(QLatin1Char(' ')).trimmed();
        if (!realignBatchArgs.isEmpty()) realignBatchArgs += QLatin1Char(' ');
        realignBatchArgs +=
            QStringLiteral("--topchannels %1 --pca-refine").arg(topCh);
    }

    // ── Status-bar progress widget ───────────────────────────────────────────
    // Always-visible feedback so the user can see batch progress while on the
    // Overview display, watching waveforms update in place as each cluster
    // completes its forceClusterRefresh.
    if (!realignProgressBar) {
        realignProgressBar = new QProgressBar(this);
        realignProgressBar->setObjectName(QStringLiteral("realignProgress"));
        realignProgressBar->setTextVisible(true);
        realignProgressBar->setMaximumWidth(280);
        // Permanent widget on the right of the status bar so it isn't
        // displaced by transient slotStatusMsg() updates.
        statusBar()->addPermanentWidget(realignProgressBar);
    }
    realignProgressBar->setRange(0, clusters.size());
    realignProgressBar->setValue(0);
    realignProgressBar->setFormat(tr("PCA align: %v / %m clusters"));
    realignProgressBar->show();

    // Clean up any leftover thread / worker from a prior single-cluster run.
    if (realignThread) {
        realignThread->quit();
        realignThread->wait(2000);
        delete realignThread;
        realignThread = nullptr;
    }
    if (realignWorker) {
        delete realignWorker;
        realignWorker = nullptr;
    }

    // Initialise batch state and launch ONE worker for the whole list.  The
    // worker loops realignSpikes internally (RealignWorker batch mode) and
    // reports per-cluster progress via slotRealignClusterDone, so the
    // per-cluster thread-spawn + GUI round-trip is paid once for the batch.
    realignBatchActive       = true;
    realignBatchTotal        = clusters.size();
    realignBatchAccepted     = 0;
    realignBatchFailed       = 0;
    realignBatchShiftedTotal = 0;
    realignBatchTouched.clear();

    // Enable the batch-scoped centroid cache for the run: each cluster's
    // per-cluster realign logBefore/logAfter then reuses one computeAllCentroids()
    // pass instead of recomputing the full-dataset centroids twice per cluster.
    doc->beginRealignBatchLog(clusters);

    realignRunning = true;
    slotStateChanged(QStringLiteral("realignState"));
    slotStatusMsg(tr("PCA-Center align: 0 / %1 clusters …").arg(clusters.size()));
    startRealignBatchWorker(clusters, realignBatchArgs);
}

void KlustersApp::applyRealignResult(bool ok, int nShifted, int nSwapped,
                                     QVector<float> meanBefore,
                                     QVector<float> meanAfter,
                                     QString backupBase,
                                     int nChan, int nSamp)
{
    // (Batch / PCA-Center Align All completes via slotRealignClusterDone +
    // slotRealignBatchFinished, not through here; applyRealignResult is the
    // single-cluster result handler.)

    // Unlock the UI.
    slotStateChanged(QStringLiteral("noRealignState"));

    if (ok) {
        // Auto-accept the realignment.  The accept/reject review popup has
        // been removed: the pending .spk/.res files are kept and flushed on
        // the next Save (deferred writes).  To discard a realignment, use
        // Undo, or File > Close without saving.
        doc->setModified(true);

        // Invalidate both caches so views re-read from the pending .spk
        // and recompute correlograms from the updated in-memory timestamps.
        if (realignClusterId >= 0) {
            doc->invalidateWaveformCache(realignClusterId);
            doc->invalidateCorrelogramCache(realignClusterId);
            doc->forceClusterRefresh(realignClusterId);
            // Realign reprojected this cluster's .fet features (membership
            // unchanged), so no group/split/renumber signal fires for it.  Tell
            // the error matrix so the incremental path treats it as changed and
            // refreshes its row instead of reusing stale probabilities.
            doc->notifyClusterFeaturesReprojected(realignClusterId);
        }

        // Switch to the Overview tab so the user immediately sees the
        // updated waveforms and auto-correlogram.
        if (tabsParent) {
            for (int i = 0; i < tabsParent->count(); ++i) {
                if (tabsParent->tabText(i).contains(tr("Overview"),
                                                    Qt::CaseInsensitive)) {
                    tabsParent->setCurrentIndex(i);
                    break;
                }
            }
        }

        // Select the realigned cluster in the palette and put focus there
        // so the user can immediately use arrow keys for further work.
        // (No activeView()->focusClusterView() — that would steal focus
        // from the palette and silently break arrow-key navigation.)
        if (clusterPalette && realignClusterId >= 0) {
            clusterPalette->selectItems(QList<int>{realignClusterId});
            clusterPalette->setFocusToList();
        }

        realignClusterId = -1;
    }

    // Restore undo/redo state correctly.
    updateUndoRedoDisplay();
}
