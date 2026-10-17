/***************************************************************************
    klusters_radialmenu.cpp
    -----------------------
    KlustersApp radial context menu (proof of concept).  Right-click in any
    view opens a RadialMenu (radialmenu.h) centred on the cursor; KlustersView's
    event filter dispatches the press, and the lasso undo-vertex and the
    lineage overlay keep their own right-click.  The command is
    app.radialMenu, so it is listed and rebindable in Preferences ▸ Input and
    shown in Help ▸ Shortcuts like every other binding.

    The menu is a curated tree of the actions the menu bar already owns —
    nothing is duplicated: enabled / checked state, icons and shortcuts are
    the live ones, and a wedge simply triggers its QAction.  Hidden actions
    (and submenus left empty by them) drop out of the rings.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klusters.h"
#include "radialmenu.h"

#include <QKeySequence>
#include <QMenu>
#include <QStatusBar>

void KlustersApp::buildRadialMenuTree()
{
    // A container only: QMenu::addAction(QAction*) does not take ownership, and
    // addMenu(QMenu*) adds the existing submenu's own menuAction, so every entry
    // below is the very QAction the menu bar shows.
    mRadialMenuTree = new QMenu(this);
    auto add = [](QMenu* m, QAction* a) { if (a) m->addAction(a); };

    // Clockwise from 12 o'clock.  A short root (9 wedges, 40° each) keeps each
    // direction distinct enough to flick without looking.
    QMenu* tools = mRadialMenuTree->addMenu(tr("Tools"));
    add(tools, mNewCluster);
    add(tools, mSplitClusters);
    add(tools, mMultiLasso);
    add(tools, mApplyMultiLasso);
    add(tools, mDeleteArtifactSpikes);
    add(tools, mDeleteNoisySpikes);

    QMenu* clusters = mRadialMenuTree->addMenu(tr("Clusters"));
    add(clusters, mDeleteArtifact);
    add(clusters, mDeleteNoisy);
    add(clusters, mGroupeClusters);
    add(clusters, mPurgeSmallClusters);

    QMenu* recluster = mRadialMenuTree->addMenu(tr("Recluster"));
    add(recluster, mReCluster);
    add(recluster, mReclusterMedian);
    add(recluster, mSplitByKnn);
    add(recluster, mDipSplit);
    add(recluster, mWatershed);
    add(recluster, mStripOutliers);
    add(recluster, mStripWaveformOutliers);
    add(recluster, mRealignSpikes);

    add(mRadialMenuTree, mUndo);
    add(mRadialMenuTree, mRedo);

    QMenu* hierarchy = mRadialMenuTree->addMenu(tr("Hierarchy"));
    add(hierarchy, mMergeParents);
    add(hierarchy, mPromoteChild);
    add(hierarchy, mGroupChildren);
    add(hierarchy, mDissolveParent);
    add(hierarchy, mDropChildNoise);

    // The Actions ▸ Sort Clusters entries under short wedge labels (the menu
    // bar keeps the full names; the status bar shows them on hover).  Hidden
    // or missing entries drop out like any other.
    QMenu* sort = mRadialMenuTree->addMenu(tr("Sort"));
    const struct { QAction* action; const char* label; } sortEntries[] = {
        { mReorderClustersBySimilarity,       QT_TR_NOOP("Similarity") },
        { mSortClustersBySpikeCount,          QT_TR_NOOP("Count") },
        { mSortClustersByTime,                QT_TR_NOOP("Time") },
        { mSortClustersByContamination,       QT_TR_NOOP("Contam.") },
        { mSortClustersBySnr,                 QT_TR_NOOP("SNR") },
        { mSortClustersByAmplitude,           QT_TR_NOOP("Amplitude") },
        { mSortClustersByAmplitudeByChannel,  QT_TR_NOOP("Amp. / channel") },
        { mSortClustersByErrorPval,           QT_TR_NOOP("Error p") },
        { mSortByResidualGated,               QT_TR_NOOP("Residual") },
        { mSortByWaveformNN,                  QT_TR_NOOP("Waveform NN") },
        { mSortByWaveformSpectral,            QT_TR_NOOP("Spectral") },
    };
    for (const auto& e : sortEntries) {
        if (!e.action) continue;
        sort->addAction(e.action);
        mRadialMenu->setLabel(e.action, tr(e.label));
    }

    // The feature-space axes as value wedges: hover one and turn the wheel.  These
    // two actions exist only here (the toolbar spinboxes are the menu-bar-side
    // control); triggering them does nothing, and RadialMenu never picks them.
    mRadialDimX = new QAction(tr("X axis feature dimension"), mRadialMenuTree);
    mRadialDimY = new QAction(tr("Y axis feature dimension"), mRadialMenuTree);
    for (QAction* a : { mRadialDimX, mRadialDimY }) {
        mRadialMenuTree->addAction(a);
        mRadialMenu->setAdjustable(a);
    }
}

void KlustersApp::refreshRadialDimensions()
{
    if (!mRadialDimX || !dimensionX || !dimensionY) return;
    // Live only where the toolbar shows the spinboxes (a display with a cluster view).
    const bool live = dimensionXAction && dimensionXAction->isVisible()
                   && dimensionX->isEnabled() && dimensionX->maximum() > 1;
    mRadialDimX->setEnabled(live);
    mRadialDimY->setEnabled(live);
    mRadialMenu->setLabel(mRadialDimX, tr("X: %1").arg(dimensionX->value()));
    mRadialMenu->setLabel(mRadialDimY, tr("Y: %1").arg(dimensionY->value()));
}

void KlustersApp::showRadialStatus(QAction* a)
{
    // The wedge labels are short; the status bar carries the full name + shortcut.
    if (!a) { statusBar()->clearMessage(); return; }
    if (a == mRadialDimX || a == mRadialDimY) {
        const SpinBox* spin = (a == mRadialDimX) ? dimensionX : dimensionY;
        statusBar()->showMessage(a->isEnabled()
            ? tr("%1: %2 of %3 — scroll to change")
                  .arg(a == mRadialDimX ? tr("X axis") : tr("Y axis"))
                  .arg(spin->value()).arg(spin->maximum())
            : tr("Feature dimensions — no cluster view in this display"));
        return;
    }
    QString msg = a->text().remove(QLatin1Char('&'));
    if (!a->shortcut().isEmpty())
        msg += QStringLiteral("    (%1)").arg(a->shortcut().toString(QKeySequence::NativeText));
    if (!a->isEnabled()) msg += tr("  — not available now");
    statusBar()->showMessage(msg);
}

void KlustersApp::showRadialMenu(const QPoint& globalPos)
{
    if (!mRadialMenu) {
        mRadialMenu = new RadialMenu(this);
        buildRadialMenuTree();               // also sets the Sort wedges' short labels
        mRadialMenu->setMenu(mRadialMenuTree);
        connect(mRadialMenu, &RadialMenu::hovered, this, &KlustersApp::showRadialStatus);
        // Wheel over X / Y: step the toolbar spinbox (it wraps, like the spinbox's own
        // wheel); its valueChanged runs the usual slotUpdateDimensionX/Y, so the
        // active cluster view redraws behind the menu.
        connect(mRadialMenu, &RadialMenu::adjusted, this, [this](QAction* a, int steps) {
            SpinBox* spin = (a == mRadialDimX) ? dimensionX
                          : (a == mRadialDimY) ? dimensionY : nullptr;
            if (!spin) return;
            spin->stepBy(steps);
            refreshRadialDimensions();
            showRadialStatus(a);
        });
    }
    refreshRadialDimensions();
    mRadialMenu->popup(globalPos);
}
