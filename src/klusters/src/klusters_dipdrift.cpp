/* klusters_dipdrift.cpp -- KlustersApp dip-split, probe-drift, timestamp-nudge.
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

// ---------------------------------------------------------------------------
// slotDipSplit  +  dipPostCommitDismiss  +  dipPostCommitUndo
//
// Shift+D runs dipsplit on the currently-selected cluster: the algorithm
// runs once via doc->dipSplitDecide(), and if accepted the split commits
// immediately via doc->dipSplitApply().  Both new clusters get selected
// in the palette.
//
// Post-commit, a confirm HUD is drawn over the active scatter view
// reporting the metrics (BIC, valley depth, spike counts).  Two QShortcuts
// installed on the active view handle:
//   Esc:    revert via doc->undo() — pops the single combined undo entry,
//           bringing back the source cluster intact.
//   Return: dismiss the HUD, keep the split.
//
// Any other curation action also dismisses the HUD silently (the
// shortcuts are scoped to the view widget; switching views or invoking
// other actions deletes them via the dipPostCommit* helpers).
//
// No live-preview overlay; no two-phase commit.  Decision and commit
// happen atomically.  The cached decision approach is gone — there's
// nothing to "drift" from.
// ---------------------------------------------------------------------------
void KlustersApp::slotDipSplit()
{
    if (wsPreviewActive) {
        // Watershed preview owns the active view; ignore Shift+D until
        // the user resolves the watershed preview first.
        return;
    }
    if (dipPostCommitActive) {
        // Pressing Shift+D while the post-commit confirm is up is a
        // no-op.  Use Enter to dismiss or Esc to undo.
        return;
    }
    if (!activeView() || !clusterPalette) return;
    if (doesActiveDisplayContainProcessWidget()) return;

    // Pick the current cluster: first non-noise / non-artefact cluster
    // shown in the active view.
    const QList<int>& shown = activeView()->clusters();
    int clusterId = -1;
    for (int c : shown) {
        if (c > 1) { clusterId = c; break; }
    }
    if (clusterId < 0) {
        statusBar()->showMessage(
            tr("DipSplit: select a cluster (cluster > 1) in the active display first."),
            4000);
        return;
    }

    KlustersView* aview = activeView();
    if (!aview->containsClusterView()) {
        statusBar()->showMessage(
            tr("DipSplit: the active display has no scatter view."), 4000);
        return;
    }

    // Locate the ClusterView so we can show the post-commit HUD on it.
    ClusterView* scatter = nullptr;
    {
        QList<ViewWidget*> vws = aview->getViewList();
        for (ViewWidget* vw : vws) {
            if ((scatter = qobject_cast<ClusterView*>(vw))) break;
        }
    }
    if (!scatter) {
        statusBar()->showMessage(
            tr("DipSplit: could not locate the scatter view."), 4000);
        return;
    }

    const int   minSize      = configuration().getDipSplitMinSize();
    const float bloatFactor  = static_cast<float>(configuration().getDipSplitBloatFactor());
    const float valleyThresh = static_cast<float>(configuration().getDipSplitValleyThresh());

    // ── Run the decision ─────────────────────────────────────────────────
    slotStatusMsg(tr("Running DipSplit..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const KlustersDoc::DipSplitDecision D =
        doc->dipSplitDecide(clusterId, minSize, bloatFactor, valleyThresh);
    QApplication::restoreOverrideCursor();

    if (!D.accepted) {
        // Decision rejected — surface the reason and stop.
        const QString why = [&] {
            const QString r = D.reason;
            if (r == QLatin1String("too_small"))
                return tr("cluster too small (< 2×MinSize)");
            if (r == QLatin1String("not_bloated"))
                return tr("cluster is already unimodal (Mahal²₉₀=%1  χ²₉₀=%2)")
                       .arg(D.mahal2P90, 0, 'f', 1)
                       .arg(D.chi2_90,   0, 'f', 1);
            if (r == QLatin1String("no_valley"))
                return tr("no valley deep enough on any of the top 3 principal "
                          "components (best depth=%1)")
                       .arg(D.bestDepth, 0, 'f', 3);
            if (r == QLatin1String("small_child"))
                return tr("split would produce a child cluster smaller than MinSize");
            if (r == QLatin1String("bic_worse"))
                return tr("single-Gaussian BIC is better than two-cluster BIC (ΔBIC=%1)")
                       .arg(D.deltaBIC, 0, 'f', 1);
            if (r == QLatin1String("cluster_not_found"))
                return tr("cluster not found");
            if (r == QLatin1String("bad_features"))
                return tr("feature matrix is singular or has too few dimensions");
            return tr("unknown reason (%1)").arg(r);
        }();
        statusBar()->showMessage(
            tr("DipSplit: no split for cluster %1 — %2").arg(clusterId).arg(why),
            10000);
        slotStatusMsg(tr("Ready."));
        return;
    }

    // ── Commit the decision ──────────────────────────────────────────────
    slotStatusMsg(tr("Applying DipSplit..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const KlustersDoc::DipSplitResult R =
        doc->dipSplitApply(D, minSize, bloatFactor, valleyThresh);
    QApplication::restoreOverrideCursor();

    if (!R.accepted) {
        // Commit rejected (degenerate split after row resolution).
        statusBar()->showMessage(
            tr("DipSplit: commit rejected (%1).").arg(R.reason), 6000);
        slotStatusMsg(tr("Ready."));
        return;
    }

    // ── Post-commit: select both new clusters in the palette and put up
    //    the confirm HUD on the active scatter view. ──────────────────────
    if (clusterPalette) {
        clusterPalette->selectItems(QList<int>{ R.leftId, R.rightId });
        clusterPalette->setFocusToList();
    }

    const QString hud = tr(
        "DipSplit committed\n"
        "  cluster %1   →   %2 (left, %3 spikes) + %4 (right, %5 spikes)\n"
        "  best PC = %6    valley depth = %7    ΔBIC = %8\n"
        "  Enter: keep     Esc: undo")
        .arg(R.sourceId)
        .arg(R.leftId).arg(R.n0)
        .arg(R.rightId).arg(R.n1)
        .arg(R.bestPC)
        .arg(R.bestDepth, 0, 'f', 3)
        .arg(R.deltaBIC,  0, 'f', 1);

    scatter->setDipsplitPostCommitHud(hud);

    // Also surface a one-line summary in the status bar (in case the
    // HUD is missed).
    statusBar()->showMessage(
        tr("DipSplit: %1 → %2 + %3 (Esc to undo, Enter to keep)")
            .arg(R.sourceId).arg(R.leftId).arg(R.rightId), 10000);

    // Install scoped Esc/Enter shortcuts on the active view.  These
    // self-clean when the post-commit window ends.
    dipPostCommitScatter = scatter;
    dipPostCommitActive  = true;
    dipInstallPostCommitShortcuts(aview);

    slotStatusMsg(tr("Ready."));
}

// Helper: install Esc/Enter QShortcuts on the active view widget for
// the duration of the post-commit confirm window.  Window-context so
// they fire regardless of focus within the main window.
void KlustersApp::dipInstallPostCommitShortcuts(KlustersView* hostView)
{
    // Defensive: clean up any stale shortcuts from a prior session.
    dipClearPostCommitShortcuts();

    if (!hostView) return;

    dipPostCommitEscShortcut =
        new QShortcut(QKeySequence(Qt::Key_Escape), hostView);
    dipPostCommitEscShortcut->setContext(Qt::WindowShortcut);
    connect(dipPostCommitEscShortcut, &QShortcut::activated,
            this, &KlustersApp::dipPostCommitUndo);

    dipPostCommitEnterShortcut =
        new QShortcut(QKeySequence(Qt::Key_Return), hostView);
    dipPostCommitEnterShortcut->setContext(Qt::WindowShortcut);
    connect(dipPostCommitEnterShortcut, &QShortcut::activated,
            this, &KlustersApp::dipPostCommitDismiss);

    // QKeySequence(Qt::Key_Return) only matches Return; numpad Enter is
    // a separate key code.  Bind a second shortcut for it.
    dipPostCommitEnterShortcut2 =
        new QShortcut(QKeySequence(Qt::Key_Enter), hostView);
    dipPostCommitEnterShortcut2->setContext(Qt::WindowShortcut);
    connect(dipPostCommitEnterShortcut2, &QShortcut::activated,
            this, &KlustersApp::dipPostCommitDismiss);
}

void KlustersApp::dipClearPostCommitShortcuts()
{
    if (dipPostCommitEscShortcut) {
        dipPostCommitEscShortcut->deleteLater();
        dipPostCommitEscShortcut = nullptr;
    }
    if (dipPostCommitEnterShortcut) {
        dipPostCommitEnterShortcut->deleteLater();
        dipPostCommitEnterShortcut = nullptr;
    }
    if (dipPostCommitEnterShortcut2) {
        dipPostCommitEnterShortcut2->deleteLater();
        dipPostCommitEnterShortcut2 = nullptr;
    }
}

// Esc handler: undo the dipsplit (one combined entry — split + view
// reattach all happen via the standard undo path).
void KlustersApp::dipPostCommitUndo()
{
    if (!dipPostCommitActive) return;
    dipDismissPostCommitHud();
    if (doc) {
        slotStatusMsg(tr("Reverting DipSplit..."));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        doc->undoDispatch();
        QApplication::restoreOverrideCursor();
        // Refresh traceView state same as slotUndo would.
        KlustersView* view = activeView();
        if (view) {
            if (view->containsTraceView() && !view->clusters().isEmpty()) {
                slotStateChanged("traceViewBrowsingState");
            } else {
                slotStateChanged("noTraceViewBrowsingState");
            }
        }
        // Return focus to the palette — same convention as the other
        // curation slots (slotMoveClustersToNoise, slotMoveClustersToArtefact).
        // The user invoked dipsplit from the palette; they should land
        // back there after undoing.  slotUndo's focusClusterView() is
        // wrong here because it targets the scatter widget.
        if (clusterPalette) clusterPalette->setFocusToList();
        slotStatusMsg(tr("Ready."));
        statusBar()->showMessage(tr("DipSplit undone."), 4000);
    }
}

// Enter handler: dismiss the HUD, keep the split.
void KlustersApp::dipPostCommitDismiss()
{
    if (!dipPostCommitActive) return;
    dipDismissPostCommitHud();
    // Return focus to the palette so arrow-key navigation continues on
    // the auto-selected new clusters (left/right halves are already
    // selected by commitTwoClusterCreation's selectItems call).
    if (clusterPalette) clusterPalette->setFocusToList();
    statusBar()->showMessage(tr("DipSplit kept."), 3000);
}

// Shared cleanup: clear HUD on the scatter and release shortcuts.
void KlustersApp::dipDismissPostCommitHud()
{
    if (dipPostCommitScatter) {
        dipPostCommitScatter->clearDipsplitPostCommitHud();
        dipPostCommitScatter = nullptr;
    }
    dipPostCommitActive = false;
    dipClearPostCommitShortcuts();
}

// ---------------------------------------------------------------------------
// slotGenerateProbeDrift
//
// Runs ndm_estimatedrift for the current electrode group only.  The tool
// writes SESSION.drift in the session directory.  Progress streams into a
// new "Drift estimation" process tab (same ProcessWidget used by Recluster).
// ---------------------------------------------------------------------------
void KlustersApp::slotGenerateProbeDrift()
{
    if (doc->url().isEmpty()) return;

    const QString dir      = doc->documentDirectory();
    const QString session  = doc->documentBaseName();
    const QString groupId  = doc->currentElectrodeGroupID();

    // Sanity: the .clu file must exist (curation must have happened).
    const QString cluPath = dir + QStringLiteral("/") + session
                          + QStringLiteral(".clu.") + groupId;
    if (!QFile::exists(cluPath)) {
        QMessageBox::warning(this, tr("Generate Probe Drift"),
            tr("No curated cluster file found:\n  %1\n\n"
               "Please save your curation (Ctrl+S) before generating drift.").arg(cluPath));
        return;
    }

    // If SESSION.drift already exists, ask whether to overwrite.
    const QString driftPath = dir + QStringLiteral("/") + session + QStringLiteral(".drift");
    if (QFile::exists(driftPath)) {
        int ret = QMessageBox::question(this, tr("Generate Probe Drift"),
            tr("%1 already exists.\nOverwrite it?").arg(driftPath),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) return;
        QFile::remove(driftPath);
    }

    // Clean up any old process tab.
    if (processWidget) {
        int idx = tabsParent->indexOf(processWidget);
        if (idx != -1) { tabsParent->removeTab(idx); displayCount--; }
        delete processWidget;
        processWidget = nullptr;
    }

    processWidget = new ProcessWidget(this);
    processWidget->setFocusPolicy(Qt::NoFocus);
    connect(processWidget, &ProcessWidget::processOutputsFinished,
            this, &KlustersApp::slotOutputTreatmentOver);
    connect(processWidget, &ProcessWidget::processNotStarted,
            this, &KlustersApp::slotOutputTreatmentOver);
    connect(tabsParent, &QTabWidget::currentChanged,
            this, &KlustersApp::slotTabChange);
    tabsParent->addTab(processWidget, tr("Drift estimation"));
    displayCount++;

    processFinished        = false;
    processOutputsFinished = false;
    processKilled          = false;

    // Invoke ndm_estimatedrift with --source-group so only this shank is
    // estimated; siblings are cloned from the result inside process_estimatedrift.
    QStringList args;
    args << session                          // positional: parameter file / session
         << QStringLiteral("sourceGroup=") + groupId;  // ndm_estimatedrift reads this

    // ndm_estimatedrift takes the session name; the bash script resolves the YAML.
    bool ok = processWidget->startJob(dir, QStringLiteral("ndm_estimatedrift"), args);
    if (!ok) {
        QMessageBox::critical(this, tr("Generate Probe Drift"),
            tr("Could not start ndm_estimatedrift.\n"
               "Ensure ndm_estimatedrift is on PATH and ndmanager-plugins is installed."));
        processFinished        = true;
        processOutputsFinished = true;
    }
}

// ---------------------------------------------------------------------------
// slotApplyDriftSiblings
//
// Shows a checklist of sibling electrode groups (same probeId).  For the
// selected groups, runs ndm_applydrift which:
//   1. Reads SESSION.drift
//   2. Computes adaptive chunk boundaries
//   3. Writes SESSION.chunks.N
//   4. Optionally re-runs KlustaKwik (controlled by the parameter file)
// ---------------------------------------------------------------------------
void KlustersApp::slotApplyDriftSiblings()
{
    if (doc->url().isEmpty()) return;

    const QString dir     = doc->documentDirectory();
    const QString session = doc->documentBaseName();
    const QString groupId = doc->currentElectrodeGroupID();
    const int     gid     = groupId.toInt();

    // SESSION.drift must exist.
    const QString driftPath = dir + QStringLiteral("/") + session + QStringLiteral(".drift");
    if (!QFile::exists(driftPath)) {
        QMessageBox::warning(this, tr("Apply Drift + Recluster Siblings"),
            tr("Drift file not found:\n  %1\n\n"
               "Run 'Generate Probe Drift' first (Actions → Generate Probe Drift…).").arg(driftPath));
        return;
    }

    // Get sibling groups from the YAML.
    const QList<int> siblings = doc->getSiblingElectrodeGroups(gid);
    if (siblings.isEmpty()) {
        QMessageBox::information(this, tr("Apply Drift + Recluster Siblings"),
            tr("No sibling electrode groups found for group %1.\n\n"
               "Sibling groups share the same probeId in the parameter file.\n"
               "Add probeId fields to spikeDetection.channelGroups to define probe membership.\n"
               "When probeId is absent, all groups default to probe 0 and are all siblings.").arg(gid));
        return;
    }

    // Build checklist dialog.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Apply Drift + Recluster Siblings"));
    QVBoxLayout *vlay = new QVBoxLayout(&dlg);
    vlay->addWidget(new QLabel(
        tr("Select sibling groups to update with drift from group %1.\n"
           "Adaptive chunk boundaries will be computed and written as\n"
           "SESSION.chunks.N.  KlustaKwik will re-run if 'runKlustaKwik'\n"
           "is set in the ndm_applydrift section of the parameter file.").arg(gid)));

    QListWidget *list = new QListWidget(&dlg);
    for (int s : siblings) {
        QListWidgetItem *item = new QListWidgetItem(
            tr("Group %1").arg(s), list);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, s);
        list->addItem(item);
    }
    vlay->addWidget(list);

    QDialogButtonBox *bbox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vlay->addWidget(bbox);

    if (dlg.exec() != QDialog::Accepted) return;

    QStringList targetGroups;
    for (int i = 0; i < list->count(); i++) {
        QListWidgetItem *it = list->item(i);
        if (it->checkState() == Qt::Checked)
            targetGroups << QString::number(it->data(Qt::UserRole).toInt());
    }
    if (targetGroups.isEmpty()) return;

    // Clean up any old process tab.
    if (processWidget) {
        int idx = tabsParent->indexOf(processWidget);
        if (idx != -1) { tabsParent->removeTab(idx); displayCount--; }
        delete processWidget;
        processWidget = nullptr;
    }

    processWidget = new ProcessWidget(this);
    processWidget->setFocusPolicy(Qt::NoFocus);
    connect(processWidget, &ProcessWidget::processOutputsFinished,
            this, &KlustersApp::slotOutputTreatmentOver);
    connect(processWidget, &ProcessWidget::processNotStarted,
            this, &KlustersApp::slotOutputTreatmentOver);
    connect(tabsParent, &QTabWidget::currentChanged,
            this, &KlustersApp::slotTabChange);
    tabsParent->addTab(processWidget, tr("Apply drift"));
    displayCount++;

    processFinished        = false;
    processOutputsFinished = false;
    processKilled          = false;

    // ndm_applydrift  session  source_group  target_group1  [target_group2 ...]
    QStringList args;
    args << session << groupId;
    args << targetGroups;

    bool ok = processWidget->startJob(dir, QStringLiteral("ndm_applydrift"), args);
    if (!ok) {
        QMessageBox::critical(this, tr("Apply Drift + Recluster Siblings"),
            tr("Could not start ndm_applydrift.\n"
               "Ensure ndm_applydrift is on PATH and ndmanager-plugins is installed."));
        processFinished        = true;
        processOutputsFinished = true;
    }
}

// ---------------------------------------------------------------------------
// Timestamp nudge — shift selected cluster's spike timestamps by ±1 sample
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// nudgeSelectedSingleCluster
//
// Shared body for slotNudgeTimestampMinus / Plus.  Both slots
// (PageUp / PageDown / 3 / 4) shift the active cluster's timestamps by
// ±1 sample and surface an identical UI flow: validate selection, show
// "please wait", run the doc-level mutation, restore palette focus.
// The only difference between the two slots is the sign of the delta.
// ---------------------------------------------------------------------------
void KlustersApp::nudgeSelectedSingleCluster(int deltaSamples)
{
    if (nudgeInProgress || isInit || !activeView()) return;
    const QList<int>& shown = activeView()->clusters();
    if (shown.size() != 1) {
        statusBar()->showMessage(
            tr("Select exactly one cluster first."), 3000);
        return;
    }
    const int id = shown.first();
    nudgeInProgress = true;
    nudgeMinusAction->setEnabled(false);
    nudgePlusAction->setEnabled(false);
    statusBar()->showMessage(tr("Nudging cluster %1… please wait").arg(id));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const bool ok = doc->nudgeClusterTimestamps(id, deltaSamples);
    nudgeInProgress = false;
    lastNudgeTimer.restart();
    nudgeMinusAction->setEnabled(true);
    nudgePlusAction->setEnabled(true);
    if (ok) {
        const QString sign = (deltaSamples >= 0) ? QStringLiteral("+") : QStringLiteral("");
        statusBar()->showMessage(
            tr("Cluster %1: %2%3 sample.").arg(id).arg(sign).arg(deltaSamples), 2000);
    } else {
        statusBar()->showMessage(tr("Timestamp nudge failed."), 3000);
    }
    // Nudge is a keyboard-triggered op (PageUp/PageDown or 3/4) initiated
    // from the cluster palette.  Keep palette focus so the user can keep
    // arrow-navigating between clusters without having to Tab back.
    // setFocusToList() targets the palette's inner iconView (which owns
    // arrow-key navigation); a plain setFocus() on the QDockWidget shell
    // would not propagate and arrow keys would still go to the active
    // view widget.
    if (clusterPalette) clusterPalette->setFocusToList();
}

void KlustersApp::slotNudgeTimestampMinus() { nudgeSelectedSingleCluster(-1); }
void KlustersApp::slotNudgeTimestampPlus()  { nudgeSelectedSingleCluster(+1); }

// ---------------------------------------------------------------------------
// Move-selected-clusters-to-end (palette T key)
//
// Renumbers each selected cluster to maxId+1, maxId+2, ... so they end up
// at the end of the palette (palette is sorted by cluster ID).  This is a
// pure rename — no spike data changes, no merging.  Single undo entry
// covers the whole operation.
//
// Edge cases:
//   - the highest-numbered cluster is filtered out (already at the end)
//   - 0 / 1 (artefact / noise) are filtered (special-cased throughout)
//   - empty selection or only-already-at-end → status message, no-op
// ---------------------------------------------------------------------------
void KlustersApp::slotMoveSelectedClustersToEnd()
{
    if (!clusterPalette || !activeView()) return;

    // T follows the palette the user is actually in.  When a child palette holds
    // focus the selection is ATOM ids, and renumbering them through the fiber path
    // would rewrite fiber shownClusters through an atom map -- so dispatch to the
    // atom-layer path instead.  focusedChildPalette() is the same test the child
    // hierarchy actions use, so T lands in the same place they do.
    if (ClusterPalette* cp = focusedChildPalette()) {
        const QList<int> kids = cp->selectedClusters();
        if (kids.isEmpty()) {
            statusBar()->showMessage(
                tr("Select a child cluster in the palette first."), 3000);
            return;
        }
        doc->renumberChildrenToEnd(kids);
        statusBar()->showMessage(
            kids.size() == 1
                ? tr("Child cluster %1 moved to end.").arg(kids.first())
                : tr("%1 child clusters moved to end.").arg(kids.size()),
            2500);
        return;
    }

    QList<int> sel = clusterPalette->selectedClusters();
    if (sel.isEmpty()) {
        statusBar()->showMessage(
            tr("Select a cluster in the palette first."), 3000);
        return;
    }

    // Track whether the user's selection included the global-max cluster
    // before filtering — we want a different message when the selection
    // was *only* the max (already at the end, nothing else to do) vs. a
    // mix that includes other movable clusters.
    const int globalMax = static_cast<int>(doc->data().highestClusterId());
    const bool selIncludedMax = (globalMax > 0) && sel.contains(globalMax);
    const int  selSizeBefore  = sel.size();

    // Filter: drop the global-max cluster and 0/1 (the rename would be a
    // no-op for the max, and remapping 0/1 would corrupt special-cluster
    // semantics).
    if (globalMax > 0) sel.removeAll(globalMax);
    sel.removeAll(0);
    sel.removeAll(1);

    if (sel.isEmpty()) {
        // The selection was non-empty before filtering but everything got
        // filtered out.  If the only thing selected was the max cluster,
        // T is a no-op — but that's not a "failure," it's a correct
        // identity operation.  Use a calm, non-alarming status message
        // ("already at end") rather than the louder "Nothing to move"
        // wording, which sounded like the action had failed.
        if (selIncludedMax && selSizeBefore == 1) {
            statusBar()->showMessage(
                tr("Cluster %1 is already at the end of the list.")
                    .arg(globalMax),
                2500);
        } else {
            // Mixed case: user selected only protected clusters (0/1)
            // or some other unmovable combination.
            statusBar()->showMessage(
                tr("Selected cluster(s) cannot be renumbered "
                   "(reserved 0/1 or already at end)."),
                3000);
        }
        clusterPalette->setFocusToList();
        return;
    }

    // Keep palette focus through the renumber so arrow-nav resumes
    // immediately.  doc->renumberClustersToEnd handles the data + view
    // notification + palette refresh.
    doc->renumberClustersToEnd(sel);
    clusterPalette->setFocusToList();

    // Treat "move to end" as a set-changing cluster edit, like delete/split/new:
    // run the configured post-edit automation (Preferences > Refinement) —
    // auto-renumber to close the id gap the move leaves (a contiguous renumber
    // preserves the new tail order, so the cluster stays at the end) and/or
    // auto-update the matrices.  renumberClustersToEnd emits no hooked signal of
    // its own, so this is wired explicitly.  Deferred + coalesced like the other
    // hooks; the renumber it may trigger emits only renumber() (not hooked), so
    // it cannot recurse.
    scheduleAutoPostClusterEdit(true);

    if (sel.size() == 1) {
        statusBar()->showMessage(
            tr("Cluster %1 renumbered to end.").arg(sel.first()), 2500);
    } else {
        statusBar()->showMessage(
            tr("%1 clusters renumbered to end.").arg(sel.size()), 2500);
    }
}
