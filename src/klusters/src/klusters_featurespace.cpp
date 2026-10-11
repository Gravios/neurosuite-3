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
#include "configuration.h"
#include "data.h"

#include <QApplication>
#include <QComboBox>
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
        const QString tip = s.usable
            ? tr("Features: %1\nWaveforms: %2")
                  .arg(QFileInfo(s.fetPath).fileName(), QFileInfo(s.spkPath).fileName())
            : tr("Not available: %1").arg(s.reason);
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
    const bool ok = doc->switchFeatureSpace(method, err);
    QApplication::restoreOverrideCursor();
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
        if (nx != x || ny != y) v->updateDimensions(nx, ny);
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

    // Every cluster's features and waveforms changed (membership did not):
    // the same per-cluster refresh the batch realign uses.  Correlograms depend
    // only on spike times, which every space shares, so they stay.
    const QList<dataType> ids = doc->data().clusterIds();
    for (dataType id : ids) {
        doc->invalidateWaveformCache(static_cast<int>(id));
        doc->notifyClusterFeaturesReprojected(static_cast<int>(id));
    }
    for (dataType id : ids)
        doc->forceClusterRefresh(static_cast<int>(id));

    if (configuration().getAutoUpdateMatricesAfterMerge())
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
