/***************************************************************************
    klusters_featurespace.cpp
    -------------------------
    KlustersApp feature-space switcher: the combo at the far right of the
    Parameters toolbar.  It shows the method space whose .fet/.spk are loaded
    (standard, stderiv_C5, a _D lag space, ...) and lists every space the open
    group has on disk; picking another loads it under the same .clu, so one
    curation can be examined through several feature spaces.  The .clu is
    always saved under the method it was opened with (KlustersDoc::cluMethod).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "klustersdoc.h"
#include "klustersview.h"
#include "data.h"
#include "configuration.h"

#include <QApplication>
#include <QComboBox>
#include <QEventLoop>
#include <QFileInfo>
#include <QFont>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStatusBar>

#include <algorithm>

void KlustersApp::populateFeatureSpaceCombo()
{
    if (!featureSpaceCombo) return;
    const QSignalBlocker block(featureSpaceCombo);
    featureSpaceCombo->clear();
    if (!doc || doc->url().isEmpty()) return;

    auto* model = qobject_cast<QStandardItemModel*>(featureSpaceCombo->model());
    for (const KlustersDoc::FeatureSpace& s : doc->availableFeatureSpaces()) {
        QString label = s.method;
        if (s.method == doc->cluMethod()) label += tr("  (.clu)");
        featureSpaceCombo->addItem(label, s.method);
        const int row = featureSpaceCombo->count() - 1;
        QString tip = s.usable
            ? tr("Features: %1\nWaveforms: %2")
                  .arg(QFileInfo(s.fetPath).fileName(), QFileInfo(s.spkPath).fileName())
            : tr("Not available: %1").arg(s.reason);
        if (s.synced) tip += tr("\nSynced to the current spike times this session (written by Save).");
        featureSpaceCombo->setItemData(row, tip, Qt::ToolTipRole);
        if (!s.usable && model)
            if (QStandardItem* item = model->item(row)) item->setEnabled(false);
    }
    const int current = featureSpaceCombo->findData(doc->activeFeatureMethod());
    if (current >= 0) featureSpaceCombo->setCurrentIndex(current);

    // Bold while another method's space is loaded: a visible reminder that the
    // scatter is not the .clu's own feature space.
    QFont f = featureSpaceCombo->font();
    f.setBold(doc->isBorrowedSpace());
    featureSpaceCombo->setFont(f);
}

void KlustersApp::slotFeatureSpaceActivated(int index)
{
    if (!doc || !featureSpaceCombo) return;
    const QString method = featureSpaceCombo->itemData(index).toString();
    if (method.isEmpty() || method == doc->activeFeatureMethod()) return;

    if (realignRunning || realignBatchActive || nudgeInProgress) {
        statusBar()->showMessage(tr("Wait for the running realignment to finish before "
                                    "switching feature space."), 5000);
        populateFeatureSpaceCombo();
        return;
    }

    const int  oldDims     = doc->nbDimensions();
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // Preference: bring the target space to the current spike times first.  A
    // failed sync still switches (to the space's current files) and says why.
    QString syncLog;
    bool syncFailed = false;
    if (configuration().getSyncFeatureSpaceOnSwitch() && method != doc->cluMethod()) {
        slotStatusMsg(tr("Syncing the %1 feature space to the current spike times…").arg(method));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        syncFailed = !doc->syncFeatureSpace(method, syncLog);
    }
    const bool ok = doc->switchFeatureSpace(method, err);
    QApplication::restoreOverrideCursor();
    if (syncFailed)
        QMessageBox::warning(this, tr("Feature space"),
                             tr("Could not sync %1 to the current spike times; showing its "
                                "existing files.\n\n%2").arg(method, syncLog.trimmed()));
    if (!ok) {
        QMessageBox::warning(this, tr("Feature space"), err);
        populateFeatureSpaceCombo();               // back to the loaded space
        return;
    }

    refreshAfterFeatureSpaceSwitch(oldDims);
    populateFeatureSpaceCombo();

    // Locks on (borrowed) or each action back to what the state machine last
    // asked for (leaving a borrowed space).
    applyFeatureSpaceLocks();

    slotStatusMsg(doc->isBorrowedSpace()
        ? tr("Showing the %1 feature space (%2 dimensions).  The .clu stays %3; "
             "realign, nudge and Save As are off until you switch back.")
              .arg(method).arg(doc->nbDimensions() - 1).arg(doc->cluMethod())
        : tr("Back in the .clu's own %1 feature space.").arg(method));
}

void KlustersApp::refreshAfterFeatureSpaceSwitch(int oldNbDimensions)
{
    const int newDims = doc->nbDimensions();

    // Keep every display's projection valid.  The last dimension is time in
    // every space, so an axis on time stays on time; an axis past the new
    // feature count falls back to the first features.
    auto remap = [&](int d) -> int {
        if (d == oldNbDimensions) return newDims;                // time
        return (d >= 1 && d < newDims) ? d : 0;                  // 0 = needs a default
    };
    for (KlustersView* v : doc->viewListCopy()) {
        if (!v || !v->containsClusterView()) continue;
        const int x = v->abscissaDimension(), y = v->ordinateDimension();
        int nx = remap(x), ny = remap(y);
        if (nx == 0) nx = 1;
        if (ny == 0) ny = (nx == 2 || newDims < 3) ? 1 : 2;
        // Always: rebuilds the scatter from the new features and resets its
        // world to the new space's extrema (the old zoom means nothing here).
        v->updateDimensions(nx, ny);
    }

    // Spin boxes: new range, values of the active display (no re-trigger).
    isInit = true;
    dimensionX->setRange(1, newDims);
    dimensionY->setRange(1, newDims);
    if (KlustersView* av = activeView()) {
        dimensionX->setValue(av->abscissaDimension());
        dimensionY->setValue(av->ordinateDimension());
    }
    isInit = false;

    // Every cluster's features changed (membership did not).  The reprojection
    // notice marks each cluster changed for the error matrix, stales the
    // template / residual / drift matrices and drops a t-SNE embedding.
    const QList<dataType> ids = doc->data().clusterIds();
    for (dataType id : ids)
        doc->notifyClusterFeaturesReprojected(static_cast<int>(id));

    // Waveforms come from the newly installed .spk: reload every display's
    // waveform and correlogram views in one pass (the active display now, the
    // others when they are next shown).  The data layer's caches were already
    // emptied by the reader change.
    KlustersView* active = activeView();
    for (KlustersView* v : doc->viewListCopy())
        if (v) v->reloadWaveformsAndCorrelograms(v == active);

    // Refresh automatically on a swap, whatever the "auto-update matrices after
    // edits" preference says: every matrix value depends on the space.
    slotUpdateErrorMatrix();
    scheduleRefreshMergeRecommendations();
}

void KlustersApp::applyFeatureSpaceLocks()
{
    const QList<QAction*> actions{ mRealignSpikes, mPcaAlignAllClusters,
                                   nudgeMinusAction, nudgePlusAction, mSaveAsAction };
    if (!featureSpaceTracking) {
        // First call (startup, initState): from now on record every enable /
        // disable the state machine makes, so the lock can be undone exactly.
        featureSpaceTracking = true;
        for (QAction* a : actions) {
            if (!a) continue;
            featureSpaceDesiredEnabled.insert(a, a->isEnabled());
            connect(a, &QAction::changed, this, [this, a]() {
                if (!featureSpaceLocking) featureSpaceDesiredEnabled[a] = a->isEnabled();
            });
        }
    }
    const bool borrowed = doc && doc->isBorrowedSpace();
    featureSpaceLocking = true;
    for (QAction* a : actions)
        if (a) a->setEnabled(!borrowed && featureSpaceDesiredEnabled.value(a, a->isEnabled()));
    featureSpaceLocking = false;
}

bool KlustersApp::refuseInBorrowedSpace(const QString& what, bool silent)
{
    if (!doc || !doc->isBorrowedSpace()) return false;
    if (!silent)
        statusBar()->showMessage(
            tr("%1 works in the .clu's own %2 feature space; switch back to it first "
               "(the %3 space is loaded).")
                .arg(what, doc->cluMethod(), doc->activeFeatureMethod()), 6000);
    return true;
}

void KlustersApp::slotSyncFeatureSpaces()
{
    if (!doc || doc->url().isEmpty()) return;
    // The combo is locked during save / realign / recluster: same busy test.
    if ((featureSpaceCombo && !featureSpaceCombo->isEnabled())
        || realignRunning || realignBatchActive || nudgeInProgress) {
        statusBar()->showMessage(tr("Wait for the running job to finish before syncing "
                                    "feature spaces."), 5000);
        return;
    }
    const int oldDims = doc->nbDimensions();
    const bool shownWasBorrowed = doc->isBorrowedSpace();
    QString log;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    slotStatusMsg(tr("Syncing feature spaces to the current spike times…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    const bool ok = doc->syncAllFeatureSpaces(log);
    QApplication::restoreOverrideCursor();

    // A borrowed space being shown was reloaded by the sync.
    if (shownWasBorrowed) refreshAfterFeatureSpaceSwitch(oldDims);
    populateFeatureSpaceCombo();

    const QString summary = log.trimmed().isEmpty()
        ? tr("This group has no other feature space.") : log.trimmed();
    if (ok) {
        slotStatusMsg(tr("Feature spaces synced to the current spike times (written by Save)."));
        QMessageBox::information(this, tr("Sync Feature Spaces"), summary);
    } else {
        QMessageBox::warning(this, tr("Sync Feature Spaces"),
                             tr("Some spaces were not synced:\n\n%1").arg(summary));
    }
}
