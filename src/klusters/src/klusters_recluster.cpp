/* klusters_recluster.cpp -- KlustersApp reclustering / process execution.
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
#include "reorder_similarity_dispatch.h"
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

void KlustersApp::slotReclusterMedianResidual(){
    // Mirror recluster availability (no document / job in flight); the guard
    // also protects slotRecluster's activeView() access when invoked by key.
    if(!mReCluster->isEnabled()) return;
    reclusterOnce = ReclusterOnce::MedianResidual;
    slotRecluster();
}

void KlustersApp::slotReclusterChannelVariance(){
    if(!mReCluster->isEnabled()) return;
    reclusterOnce = ReclusterOnce::ChannelVariance;
    slotRecluster();
}

void KlustersApp::slotRecluster(){
    // If a recluster job is still in flight, schedule a retry and return.
    // We use a single stoppable QTimer rather than repeated QTimer::singleShot,
    // so we can cancel all pending retries the moment the job finishes — preventing
    // stale timer firings from re-launching KlustaKwik after the job completes.
    if(!(processFinished && processOutputsFinished)) {
        if(!reclusterRetryTimer){
            reclusterRetryTimer = new QTimer(this);
            reclusterRetryTimer->setSingleShot(false);
            reclusterRetryTimer->setInterval(2000);
            connect(reclusterRetryTimer, &QTimer::timeout, this, &KlustersApp::slotRecluster);
        }
        if(!reclusterRetryTimer->isActive())
            reclusterRetryTimer->start();
        return;
    }

    // Job is done. Stop and destroy the retry timer so it cannot fire again.
    if(reclusterRetryTimer){
        reclusterRetryTimer->stop();
        delete reclusterRetryTimer;
        reclusterRetryTimer = nullptr;
    }

    // Consume any one-shot mode requested by Shift+M / Shift+C.  We are now
    // past the busy-retry guard, so a retry preserved the request; clear it
    // here so it applies to exactly this run and a later manual recluster is
    // unaffected.
    const ReclusterOnce reclusterOnceMode = reclusterOnce;
    reclusterOnce = ReclusterOnce::None;

    // Clean up the previous recluster output tab if it still exists.
    if(processWidget != nullptr){
        int tabIndex = tabsParent->indexOf(processWidget);
        if(tabIndex != -1){
            tabsParent->removeTab(tabIndex);
            displayCount--;
        }
        delete processWidget;
        processWidget = nullptr;
    }
    processKilled = false;

    //Get the clusters to recluster (those selected in the active display)
    const QList<int>& currentClusters = activeView()->clusters();
    if(currentClusters.isEmpty()){
        QMessageBox::critical (this,tr("Error !"),tr("No clusters have been selected to be reclustered."));
        return;
    }

    clustersToRecluster.clear();
    QList<int>::const_iterator shownClustersIterator;
    for(shownClustersIterator = currentClusters.begin(); shownClustersIterator != currentClusters.end(); ++shownClustersIterator)
        clustersToRecluster.append(*shownClustersIterator);
    std::sort(clustersToRecluster.begin(), clustersToRecluster.end());

    // ── Pre-launch desync check ──────────────────────────────────────────
    // The cluster palette reads its membership counts from spikesByCluster
    // (a row → cluster table) while createFeatureFile reads from
    // clusterInfoMap.  Under normal operation these are kept in sync, but
    // there are paths where they desync — most notably:
    //
    //   • A curation-log replay that rebuilds spikesByCluster but skips
    //     the clusterInfoMap pass.
    //   • An aborted merge / split / reorder where the row-table commit
    //     landed but the clusterInfoMap rebuild didn't fire.
    //   • A reclustered-cluster IDs collision that consumes a stale key
    //     during integrateReclusteredClusters without restoring it.
    //
    // Note that nudge does NOT directly desync the map — it only touches
    // feature rows and timestamps via updateFeatureRow / updateTimestamp,
    // never the map.  If recluster fails immediately after a nudge, the
    // desync was already present BEFORE the nudge (typically from an
    // earlier merge/split/undo); nudge just happens to be the last
    // operation the user remembers performing.
    //
    // When desynced, createFeatureFile sizes its output from a default-
    // constructed ClusterInfo (nbSpikes=0) and writes a header-only .fet.
    // KK then aborts with "Array::SetSize: n < 1 (n=0, tag=Data
    // (nPoints*nDims))" — a cryptic exception that gives the user no
    // hint why their visibly-1500-spike cluster failed to recluster.
    //
    // Catch the desync here, refuse to launch, and tell the user what
    // recovery action will fix it.  Save+reopen IS safe: saveClusters()
    // writes .clu from spikesByCluster (the source of truth), and the
    // load path rebuilds clusterInfoMap from .clu — so the in-memory
    // desync does not survive a round-trip through disk.
    //
    // Apply to every selected ID including 0 (artifact) and 1 (MUA):
    // both can be legitimately reclustered, and both can in principle
    // desync the same way.  Skipping them would only mask the bug.
    {
        QStringList emptyIds;
        for (int id : clustersToRecluster) {
            if (!doc->activeClusterHasMembers(id))
                emptyIds.append(QString::number(id));
        }
        if (!emptyIds.isEmpty()) {
            QMessageBox::critical(this, tr("Recluster aborted"),
                tr("Cluster(s) %1 appear in the palette but have zero "
                   "spikes registered in the clusterInfoMap — this is a "
                   "desync between the row table and the cluster map. "
                   "It is typically caused by an earlier merge, split, "
                   "or undo that committed partially; nudge is not the "
                   "source even when it is the most recent action.\n\n"
                   "Save the session and re-open before reclustering. "
                   "Saving writes .clu from the row table (the source "
                   "of truth) and reopening rebuilds the cluster map "
                   "from .clu, so the in-memory desync does not "
                   "survive a disk round-trip.").arg(emptyIds.join(", ")));
            return;
        }
    }

    //Build the command line to launch the reclustering

    //Create the fet file name: baseName + -- + -clusterIDs -- + timestamp + .fet + .electrodeGroupID
    QString fileBaseName = doc->documentBaseName();
    fileBaseName.append("--");

    int max = clustersToRecluster.size() - 1;
    int i = 0;
    for(; i < max; ++i){
        fileBaseName.append(QString::number(clustersToRecluster[i]));
        fileBaseName.append("-");
    }
    fileBaseName.append(QString::number(clustersToRecluster[i]));

    QString date = QDate::currentDate().toString("--MM.dd.yyyy-");
    QString time = QTime::currentTime().toString("hh.mm");
    fileBaseName.append(date);
    fileBaseName.append(time);

    // Build the argument list by substituting tokens in reclusteringArgs.
    // We operate on individual QStringList tokens (not a flat string) so that:
    //   1. reclusteringExecutable is cleanly separated for Qt6 QProcess::start()
    //   2. fileBaseName is always a single opaque token even if it contains
    //      characters that would confuse shell-style splitting.
    QString electrodeGroupID = doc->currentElectrodeGroupID();
    if(electrodeGroupID.isEmpty())
        electrodeGroupID = QLatin1String("1");

    // Compute %features value: auto-variance when checkbox on + 1 cluster selected,
    // otherwise original behaviour (PCA cols ON, extra cols OFF, timestamp ON).
    QString features;
    if(reclusteringArgs.contains(QLatin1String("%features"))){
        int totalNbOfPCAs   = doc->totalNbOfPCAs();
        int nbDimensions    = doc->nbDimensions();
        int nbExtraFeatures = nbDimensions - totalNbOfPCAs - 1;
        int nFeatureCols    = nbDimensions - 1; // all cols except timestamp

        bool usedAutoSelect = false;
        if(autoSelectFeatures || reclusterOnceMode==ReclusterOnce::ChannelVariance){
            // Gather the currently-selected clusters from the active view.
            // Works for any number of selected clusters (>= 1).
            QList<int> sel;
            if(activeView())
                sel = activeView()->clusters();

            // Remove noise/artefact pseudo-clusters (0 and 1) — they have no
            // meaningful feature spread and would dilute the variance estimate.
            sel.removeAll(0);
            sel.removeAll(1);

            if(!sel.isEmpty()){
                // Pool spikes across all selected clusters for variance computation.
                QVector<double> variances = (sel.size() == 1)
                    ? doc->computeFeatureVariancesForCluster(sel.first())
                    : doc->computeFeatureVariancesForClusters(sel);

                if(!variances.isEmpty()){
                    // Sort feature indices by descending variance.
                    QVector<QPair<int,double>> iv;
                    iv.reserve(variances.size());
                    for(int i = 0; i < variances.size() && i < nFeatureCols; ++i)
                        iv.append(qMakePair(i, variances[i]));
                    std::sort(iv.begin(), iv.end(),
                        [](const QPair<int,double>& a, const QPair<int,double>& b){
                            return a.second > b.second;
                        });

                    // nSelect is a ceiling, not a target: stop early if variance
                    // drops below 5 % of the top feature (noise-floor trim).
                    int    nSelect  = qBound(1, autoSelectNFeatures, nFeatureCols);
                    double topVar   = iv.isEmpty() ? 0.0 : iv[0].second;
                    double minVar   = topVar * 0.05;

                    QSet<int> selected;

                    // Channel-level selection (patch): rank whole channels by
                    // aggregate feature variance and keep every PCA column of
                    // the top channels, so a high-variance channel's components
                    // are never split across the kept/dropped boundary.
                    // autoSelectNFeatures (the N-features spin box) sets the
                    // number of channels kept.
                    if(configuration().getReclusterChannelVariance()
                       || reclusterOnceMode==ReclusterOnce::ChannelVariance){
                        const int nCh    = doc->nbOfchannels();
                        const int totPca = doc->totalNbOfPCAs();
                        const int featPerCh = (nCh > 0) ? totPca / nCh : 0;
                        if(featPerCh > 0){
                            // Aggregate the per-column variance into per-channel
                            // totals over the PCA columns only (col j → channel
                            // j / featPerCh; channel-major .fet layout).
                            QVector<double> chVar(nCh, 0.0);
                            for(int j = 0; j < variances.size() && j < totPca; ++j)
                                chVar[j / featPerCh] += variances[j];

                            QVector<QPair<int,double>> cv;
                            cv.reserve(nCh);
                            for(int c = 0; c < nCh; ++c)
                                cv.append(qMakePair(c, chVar[c]));
                            std::sort(cv.begin(), cv.end(),
                                [](const QPair<int,double>& a, const QPair<int,double>& b){
                                    return a.second > b.second;
                                });

                            // The N-features spin box (autoSelectNFeatures)
                            // sets the number of channels selected directly:
                            // take exactly the top nChSel channels by variance,
                            // with no noise-floor trim, so the spin box value is
                            // authoritative.
                            const int nChSel = qBound(1, autoSelectNFeatures, nCh);
                            for(int c = 0; c < cv.size() && c < nChSel; ++c){
                                const int ch = cv[c].first;
                                for(int p = 0; p < featPerCh; ++p)
                                    selected.insert(ch * featPerCh + p);
                            }
                        }
                    }

                    // Per-feature-column selection (default / fallback when the
                    // channel-level path produced nothing).
                    if(selected.isEmpty()){
                        for(int k = 0; k < nSelect && k < iv.size(); ++k){
                            if(topVar > 0.0 && iv[k].second < minVar)
                                break;   // remaining features are at noise level
                            selected.insert(iv[k].first);
                        }
                        // Always keep at least the single most-informative feature.
                        if(selected.isEmpty() && !iv.isEmpty())
                            selected.insert(iv[0].first);
                    }

                    // patch75 — Build bit-string; timestamp column is set
                    // OFF when auto-selecting features.  Including the
                    // normalised timestamp as a clustering dimension
                    // makes the reclusterer over-fit to within-session
                    // drift: spikes from the same unit that fired at
                    // different times in the recording get separated by
                    // when they fired rather than by waveform shape.
                    // Auto-select picks variance-ranked PCA features
                    // precisely because they capture shape variance —
                    // tacking the timestamp on undoes that intent.
                    //
                    // The manual / fallback path below still appends '1'
                    // for the timestamp to preserve the historical
                    // default behaviour when auto-select is off.
                    for(int i = 0; i < nFeatureCols; ++i)
                        features.append(selected.contains(i) ? QLatin1Char('1') : QLatin1Char('0'));
                    features.append(QLatin1Char('0'));
                    usedAutoSelect = true;
                }
            }
        }
        if(!usedAutoSelect){
            // Fallback: all PCA dims ON, extra dims OFF, timestamp ON.
            for(int j = 0; j < totalNbOfPCAs; ++j)   features.append(QLatin1Char('1'));
            for(int j = 0; j < nbExtraFeatures; ++j)  features.append(QLatin1Char('0'));
            features.append(QLatin1Char('1'));
        }
    }

    // Split the args template into tokens then substitute each independently.
    // Note: %features is NOT replaced here — its value depends on whether the
    // subdim path below succeeds (which writes a small-nFeatures .fet and
    // needs a matching short UseFeatures string).  Doing the replacement
    // here would commit to the auto-select pattern before the subdim path
    // decides, and the override at the end of the subdim block would then
    // become a no-op (the token would already be gone), causing KK to
    // launch with a long UseFeatures against a short .fet → nDims=0 →
    // "Array::SetSize: n < 1 (n=0, tag=Data (nPoints*nDims))" abort.
    // The single deferred replacement happens at the unified site below.
    QStringList argList = QProcess::splitCommand(reclusteringArgs);
    for(QString &arg : argList){
        arg.replace(QLatin1String("%fileBaseName"),     fileBaseName);
        arg.replace(QLatin1String("%electrodeGroupID"), electrodeGroupID);
    }

    reclusteringFetFileName  = doc->documentDirectory() + QLatin1Char('/');
    reclusteringFetFileName += fileBaseName;
    reclusteringFetFileName += QLatin1String(".fet.");
    reclusteringFetFileName += electrodeGroupID;

    // patch81 — Make the session YAML reachable from the recluster temp
    // basename.
    //
    // KlustaKwikYaml.cpp:34 only looks for "<fileBase>.yaml" (then .yml).
    // Klusters reinvents fileBaseName per-recluster as
    //   <origBaseName>--<clusterIDs>--<MM.dd.yyyy-hh.mm>
    // so the temp basename never has a matching YAML and KKExp falls
    // back to its hard-coded SamplingRate=20000 default.  At 32 kHz
    // that mis-tunes every duration-derived parameter (chunk count,
    // dead time, refractory windows), and at the very least surfaces
    // as:
    //   KlustaKwik: SamplingRate defaulting to 20000 Hz
    //               (no YAML found and -SamplingRate not given)
    //
    // Fix: copy the original session YAML to the temp basename before
    // launching, so KKExp's tryPath("<tempBase>", ".yaml") finds it.
    // The temp YAML is removed by patch81_cleanupTempYaml() at every
    // existing reclusteringFetFileName cleanup site.
    //
    // Order matters: this happens AFTER reclusteringFetFileName is
    // computed (so the path-derivation in cleanupTempYaml stays
    // consistent), but BEFORE the feature-file creation calls, so
    // even if createFeatureFile fails the cleanup logic still finds
    // and removes the staged YAML.
    {
        const QString origYaml = doc->documentDirectory()
                               + QLatin1Char('/')
                               + doc->documentBaseName()
                               + QLatin1String(".yaml");
        const QString tempYaml = doc->documentDirectory()
                               + QLatin1Char('/')
                               + fileBaseName
                               + QLatin1String(".yaml");
        if (QFile::exists(origYaml)) {
            if (!QFile::exists(tempYaml)) {
                if (!QFile::copy(origYaml, tempYaml)) {
                    qWarning() << "[recluster] patch81: could not copy"
                               << origYaml << "to" << tempYaml
                               << "— KlustaKwik will fall back to "
                                  "default sampling rate";
                }
            }
        } else {
            // .yml fallback (KKExp tries both extensions)
            const QString origYml = doc->documentDirectory()
                                  + QLatin1Char('/')
                                  + doc->documentBaseName()
                                  + QLatin1String(".yml");
            const QString tempYml = doc->documentDirectory()
                                  + QLatin1Char('/')
                                  + fileBaseName
                                  + QLatin1String(".yml");
            if (QFile::exists(origYml) && !QFile::exists(tempYml))
                QFile::copy(origYml, tempYml);
        }
    }

    // patch76 — Mean-subtracted sub-dimensional path takes precedence
    // when the user has opted into it AND exactly one non-noise cluster
    // is being reclustered.  Generates a small K-component residual-PCA
    // feature file in place of the canonical full-feature .fet, and
    // overrides %features to match (K '1's + '0' for time).  For all
    // other cases (multi-cluster recluster, or mode off) we fall through
    // to the original createFeatureFile path below.
    //
    // patch78 — return value is now an OpenSaveCreateReturnMessage enum
    // (OK / OPEN_ERROR / CREATION_ERROR); the actual dim count comes
    // back via the out-parameter.  Previously the function returned
    // K+1 directly, which collided with enum codes for many K values
    // (e.g. K=7 → 8 == CREATION_ERROR, falsely tripping the IO Error
    // dialog even though the file had been written successfully).
    bool usedSubdim = false;

    // Median-waveform residual path (raw .spk, per-(channel,sample) median).
    // Pools the spikes of all selected non-noise clusters, takes one median
    // waveform over the pool, and clusters the residuals — so it works on a
    // single cluster or on several pooled together (re-merge then re-split on
    // residual structure).  Takes precedence over the mean-subtracted subdim
    // path when both are enabled.  Writes a small K-component residual-PCA .fet
    // and overrides %features to match.  (clustersToRecluster is sorted, so
    // first() > 1 means the selection contains no noise/artefact pseudo-cluster.)
    if ((reclusterOnceMode==ReclusterOnce::MedianResidual ||
         (reclusterOnceMode==ReclusterOnce::None &&
          configuration().getReclusterMedianWaveformResidual())) &&
        !clustersToRecluster.isEmpty() &&
        clustersToRecluster.first() > 1) {
        const int K = qBound(1, autoSelectNFeatures, doc->nbDimensions() - 1);
        QVector<double> eigvals;
        int dimsWritten = 0;
        const int rc = doc->createMedianWaveformResidualFeatureFile(
            clustersToRecluster, K, reclusteringFetFileName, &dimsWritten, &eigvals);
        if (rc == KlustersDoc::OPEN_ERROR) {
            QMessageBox::critical(this,tr("Error !"),
                tr("The reclustering feature file cannot be created (median-"
                   "waveform residual path). Falling back to standard "
                   "feature file."));
        } else if (rc != KlustersDoc::OK || dimsWritten <= 0) {
            QMessageBox::critical(this,tr("IO Error !"),
                tr("Median-waveform residual feature-file creation failed. "
                   "Falling back to standard feature file."));
        } else {
            QString fMed;
            for (int j = 0; j < dimsWritten - 1; ++j)
                fMed.append(QLatin1Char('1'));
            fMed.append(QLatin1Char('0'));      // timestamp column off
            features = fMed;
            QStringList cidStrs;
            for (int cid : clustersToRecluster) cidStrs << QString::number(cid);
            QString evMsg = QString("[recluster] median-waveform residual: "
                "cluster(s) %1, K=%2 residual-PCA components; eigenvalues:")
                .arg(cidStrs.join(QLatin1Char(','))).arg(dimsWritten - 1);
            for (double e : eigvals)
                evMsg.append(QString(" %1").arg(e, 0, 'g', 4));
            qDebug() << evMsg;
            usedSubdim = true;
        }
    }

    if (reclusterOnceMode==ReclusterOnce::None &&
        configuration().getReclusterMeanSubtractedSubdim() && !usedSubdim &&
        clustersToRecluster.size() == 1 &&
        clustersToRecluster.first() > 1) {
        const int singleCid = clustersToRecluster.first();
        const int K = qBound(1, autoSelectNFeatures, doc->nbDimensions() - 1);
        QVector<double> eigvals;
        int dimsWritten = 0;
        const int rc = doc->createMeanSubtractedSubdimFeatureFile(
            singleCid, K, reclusteringFetFileName, &dimsWritten, &eigvals);
        if (rc == KlustersDoc::OPEN_ERROR) {
            QMessageBox::critical(this,tr("Error !"),
                tr("The reclustering feature file cannot be created (mean-"
                   "subtracted subdim path). Falling back to standard "
                   "feature file."));
            // fall through to the standard path
        } else if (rc != KlustersDoc::OK || dimsWritten <= 0) {
            QMessageBox::critical(this,tr("IO Error !"),
                tr("Mean-subtracted subdim feature-file creation failed. "
                   "Falling back to standard feature file."));
            // fall through
        } else {
            // Success — override %features.  dimsWritten == K + 1; bit
            // string is K '1's (residual PCA components) then '0' for
            // the timestamp column.  The replacement itself is deferred
            // to the unified site after this block so it can't be
            // shadowed by an earlier replacement.
            QString fSubdim;
            for (int j = 0; j < dimsWritten - 1; ++j)
                fSubdim.append(QLatin1Char('1'));
            fSubdim.append(QLatin1Char('0'));
            features = fSubdim;             // overrides the auto-select string
            // Log the eigenvalues so the user can see how the cluster's
            // residual structure decomposes.
            QString evMsg = QString("[recluster] mean-subtracted "
                "subdim: cluster %1, K=%2 residual-PCA components; "
                "eigenvalues:")
                .arg(singleCid).arg(dimsWritten - 1);
            for (double e : eigvals)
                evMsg.append(QString(" %1").arg(e, 0, 'g', 4));
            qDebug() << evMsg;
            usedSubdim = true;
        }
    }

    // ── Deferred %features replacement ────────────────────────────────────
    // Single point of truth for the UseFeatures bit-string.  By this point
    // `features` is either the original auto-select / fallback pattern (if
    // subdim was off or fell through) or the subdim K-ones-plus-trailing-0
    // override (if subdim succeeded).  Either way the .fet on disk and the
    // command-line UseFeatures string now agree on nFeatures, so KK's nDims
    // computation
    //     nDims += (UseFeatures[i] == '1') for i in [0, nFeatures)
    // produces the right answer and Data.SetSize(nPoints * nDims) doesn't
    // get a zero.
    if (!features.isEmpty()) {
        for (QString &arg : argList)
            arg.replace(QLatin1String("%features"), features);
    }

    //Create the feature file for the selected clusters and get its name.
    int returnStatus = usedSubdim
        ? KlustersDoc::OK
        : doc->createFeatureFile(clustersToRecluster,reclusteringFetFileName);
    if(returnStatus == KlustersDoc::OPEN_ERROR){
        QMessageBox::critical (this,tr("Error !"),tr("The reclustering feature file cannot be created (possibly because of insufficient file access permissions).\n Reclustering can not be done."));
        return;
    } else if(returnStatus == KlustersDoc::CREATION_ERROR) {
        QMessageBox::critical (this,tr("IO Error !"),tr("An error happened while creating the reclustering feature file.\n Reclustering can not be done."));
        return;
    }

    if(processWidget == nullptr){

        processWidget = new ProcessWidget(this);
        processWidget->setFocusPolicy(Qt::NoFocus);
        connect(processWidget,&ProcessWidget::finished, this, &KlustersApp::slotProcessExited);
        // slotOutputTreatmentOver is driven by processOutputsFinished (emitted by ProcessWidget
        // after all stdout/stderr has been drained), NOT by finished — connecting it to both
        // caused a double-invocation: slotProcessExited did the integrate+update work, then
        // slotOutputTreatmentOver fired on the same signal and corrupted processOutputsFinished
        // state, causing the next recluster to skip the processWidget cleanup guard and segfault.
        connect(processWidget,&ProcessWidget::processOutputsFinished, this, &KlustersApp::slotOutputTreatmentOver);
        connect(processWidget,&ProcessWidget::processNotStarted, this, &KlustersApp::slotOutputTreatmentOver);
        //Connect the change tab signal to slotTabChange(QWidget* widget) to trigger updates when
        //the active display changes.
        connect(tabsParent, &QTabWidget::currentChanged, this, &KlustersApp::slotTabChange);

        tabsParent->addTab(processWidget,tr("Recluster output"));

        //Keep track of the number of displays
        displayCount++;

    }

    //Rest the different variables.
    clustersFromReclustering.clear();
    processFinished = false;
    processOutputsFinished = false;
    processKilled = false;
    slotStateChanged("reclusterState");

    //Start the process
    bool status;
    status = processWidget->startJob(doc->documentDirectory(), reclusteringExecutable, argList);

    if(!status){
        QMessageBox::critical (this,tr("Error !"),tr("The reclustering program could not be started.\n"
                                                     "One possible reason is that the automatic reclustering program could not be found."));
        processFinished = true;
        processKilled = false;
        slotStateChanged("noReclusterState");
        updateUndoRedoDisplay();
    }
}

void KlustersApp::slotProcessExited(int exitCode, QProcess::ExitStatus status){
    //Check if the process has exited "voluntarily" and if so if it was successful
    if(!(status == QProcess::NormalExit) || (status == QProcess::NormalExit && exitCode)){
        if(status == QProcess::NormalExit || (status != QProcess::NormalExit  && !processKilled))
            QMessageBox::critical (this,tr("Error !"),tr("The reclustering program did not finished normaly.\n"
                                                         "Check the output log for more information."));

        if(!QFile::remove(reclusteringFetFileName))
            QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program."));
        // patch81 — also clean up the staged YAML
        {
            const int dotFet = reclusteringFetFileName.lastIndexOf(
                QLatin1String(".fet."));
            if (dotFet >= 0) {
                const QString base = reclusteringFetFileName.left(dotFet);
                QFile::remove(base + QLatin1String(".yaml"));
                QFile::remove(base + QLatin1String(".yml"));
            }
        }

        processFinished = true;
        processOutputsFinished = true;
        processKilled = false;
        slotTabChange(tabsParent->currentIndex());
        return;
    }

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    // Stop any pending retry timer immediately.  Without this, timers queued
    // while the process was running fire after we set processFinished=true and
    // see processWidget==0L — causing them to fall through and re-launch KlustaKwik.
    if(reclusterRetryTimer){
        reclusterRetryTimer->stop();
        delete reclusterRetryTimer;
        reclusterRetryTimer = nullptr;
    }

    int returnStatus = doc->integrateReclusteredClusters(clustersToRecluster,clustersFromReclustering,reclusteringFetFileName);

    switch(returnStatus){
    case KlustersDoc::DOWNLOAD_ERROR:
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this,tr("Error !"),tr("Could not download the temporary file containing the new clusters."));
        processFinished = true;
        processOutputsFinished = true;
        processKilled = false;
        slotTabChange(tabsParent->currentIndex());
        return;
    case KlustersDoc::OPEN_ERROR:
        QApplication::restoreOverrideCursor();
        QMessageBox::critical (this,tr("Error !"),tr("Could not open the temporary file containing the new clusters."));
        processFinished = true;
        processOutputsFinished = true;
        processKilled = false;
        slotTabChange(tabsParent->currentIndex());
        return;
    case KlustersDoc::INCORRECT_CONTENT:
        QApplication::restoreOverrideCursor();
        QMessageBox::critical (this,tr("Error !"),tr("The temporary file containing the new clusters contains incorrect data."));
        processFinished = true;
        processOutputsFinished = true;
        processKilled = false;
        slotTabChange(tabsParent->currentIndex());
        return;
        return;
    case KlustersDoc::OK:
        break;
    }

    // Log what happened — the ProcessWidget output tab already shows the
    // KlustaKwik run log; a modal dialog here only pumps the event loop
    // while processFinished is still false, which lets stale retry timers
    // re-queue and eventually re-launch KlustaKwik spuriously.
    {
        QString info = tr("Reclustering of ");
        if(clustersToRecluster.size() > 1) info.append("clusters "); else info.append("cluster ");
        QList<int>::iterator it;
        for(it = clustersToRecluster.begin(); it != clustersToRecluster.end(); ++it){
            info.append(QString::number(*it)); info.append(" ");
        }
        info.append("complete. New clusters: ");
        for(it = clustersFromReclustering.begin(); it != clustersFromReclustering.end(); ++it){
            info.append(QString::number(*it)); info.append(" ");
        }
        NS3_DIAG() << info;
    }

    const bool childRecluster = doc->reclusterTargetIsChild();   // capture before the pin is consumed
    // The parent that owns the reclustered child, captured before reclusteringUpdate
    // rebuilds the hierarchy.  slotTabChange (below) clears the parent palette when
    // the recluster-output tab is active, so we re-select this afterward to keep the
    // parent highlighted under the freshly split atoms.  (-1 for a parent recluster.)
    const int reclusteredParent = (childRecluster && !clustersToRecluster.isEmpty())
                                  ? doc->parentOfChild(clustersToRecluster.first()) : -1;
    doc->reclusteringUpdate(clustersToRecluster,clustersFromReclustering);

    // Restore focus to the palette rather than the 2D ClusterView.  After a
    // recluster the user's natural next step is usually to pick one of the
    // new clusters and inspect it — that requires arrow-key navigation in
    // the palette, which needs iconView focus.  focusClusterView() here
    // would silently break arrow-key nav (and force a Tab press to recover).
    if (childRecluster) {
        // Child recluster: parent selection is untouched (reclusteringUpdate's
        // child branch leaves it), so keep the parent selected and land focus on
        // the freshly split atoms in the child palette — clustersFromReclustering
        // are the new child ids, and hierarchyChanged already repopulated the
        // child palette for the parent.
        ClusterPalette* cp = focusedChildPalette() ? focusedChildPalette() : childPalette;
        if (cp) {
            cp->selectItems(clustersFromReclustering);
            cp->setFocusToList();
        }
    } else if (clusterPalette) {
        clusterPalette->setFocusToList();
    }
    processFinished = true;
    processKilled = false;
    // Re-run the full tab-change logic so that every action disabled by
    // reclusterState is restored to the correct enabled/disabled state for
    // whichever views are currently open.  A bare noReclusterState only
    // re-enables mReCluster and leaves everything else locked.
    slotTabChange(tabsParent->currentIndex());
    // slotTabChange's recluster-output branch deselects the parent palette; for a
    // child recluster restore the owning parent's highlight.  selectItems is silent
    // (no scope switch), so the child scope and the new-atom selection in the child
    // palette are preserved.  For a case-3 child recluster (spans >= 2 fibers) the
    // deferred applyPendingFiberSelection later selects the synthesised fiber, which
    // correctly overrides this.
    if (childRecluster && reclusteredParent > 1 && clusterPalette)
        clusterPalette->selectItems({reclusteredParent});
    QApplication::restoreOverrideCursor();
}

void KlustersApp::slotStopRecluster(){
    processWidget->killJob();
    processKilled = true;
    slotStateChanged("stoppedReclusterState");
}

void KlustersApp::slotOutputTreatmentOver(){
    processOutputsFinished = true;
    // Re-run full tab-change logic to restore all actions locked by reclusterState.
    slotTabChange(tabsParent->currentIndex());
}
