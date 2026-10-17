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

    // Clockwise from 12 o'clock.  A short root (7 wedges) keeps each direction
    // distinct enough to flick without looking.
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
    if (mClusterSortMenu) clusters->addMenu(mClusterSortMenu);   // third ring

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

    QMenu* displays = mRadialMenuTree->addMenu(tr("Displays"));
    add(displays, newClusterDisplay);
    add(displays, newWaveformDisplay);
    add(displays, newCrosscorrelationDisplay);
    add(displays, mNewTraceDisplay);
}

void KlustersApp::showRadialMenu(const QPoint& globalPos)
{
    if (!mRadialMenu) {
        buildRadialMenuTree();
        mRadialMenu = new RadialMenu(this);
        mRadialMenu->setMenu(mRadialMenuTree);
        // The wedge labels are short; the status bar carries the full name + shortcut.
        connect(mRadialMenu, &RadialMenu::hovered, this, [this](QAction* a) {
            if (!a) { statusBar()->clearMessage(); return; }
            QString msg = a->text().remove(QLatin1Char('&'));
            if (!a->shortcut().isEmpty())
                msg += QStringLiteral("    (%1)").arg(a->shortcut().toString(QKeySequence::NativeText));
            if (!a->isEnabled()) msg += tr("  — not available now");
            statusBar()->showMessage(msg);
        });
    }
    mRadialMenu->popup(globalPos);
}
