/* klusters_state.cpp -- KlustersApp action-state machine (slotStateChanged).
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

void KlustersApp::slotStateChanged(const QString& state)
{
    if(state == QLatin1String("initState")) {
        viewClusterInfo->setEnabled(false);
        mDeleteNoisySpikes->setEnabled(false);
        mOpenAction->setEnabled(true);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
        mFileOpenRecent->setEnabled(false);
        mSaveAction->setEnabled(false);
        mSaveAsAction->setEnabled(false);
        mRenumberAndSave->setEnabled(false);
        mPrintAction->setEnabled(false);
        mCloseAction->setEnabled(false);
        mImportFile->setEnabled(false);
        mUndo->setEnabled(false);
        mRedo->setEnabled(false);
        mSelectAllAction->setEnabled(false);
        mSelectAllExceptAction->setEnabled(false);
        mNewCluster->setEnabled(false);
        newClusterDisplay->setEnabled(false);
        newGroupingAssistantDisplay->setEnabled(false);
        newWaveformDisplay->setEnabled(false);
        newOverViewDisplay->setEnabled(false);
        mNewTraceDisplay->setEnabled(false);
        newCrosscorrelationDisplay->setEnabled(false);

        mIncreaseAmplitudeCorrelation->setEnabled(false);
        mDecreaseAmplitudeCorrelation->setEnabled(false);
        mCloseActiveDisplay->setEnabled(false);
        mRenameActiveDisplay->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mDeleteArtifact->setEnabled(false);

        mGroupeClusters->setEnabled(false);

        mAutoMerge->setEnabled(false);
        mChunkMode->setEnabled(false);

        mSortClustersBySpikeCount->setEnabled(false);
        mSortClustersByTime->setEnabled(false);
        mSortClustersByContamination->setEnabled(false);
        mSortClustersBySnr->setEnabled(false);
        mSortClustersByAmplitude->setEnabled(false);
        mSortClustersByAmplitudeByChannel->setEnabled(false);
        mSortClustersByErrorPval->setEnabled(false);

        mPurgeSmallClusters->setEnabled(false);
        mUpdateDisplay->setEnabled(false);
        mZoomAction->setEnabled(false);
        mUpdateErrorMatrix->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);

        mDeleteNoisy->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mReclusterMedian->setEnabled(false);
        mReclusterChannelVar->setEnabled(false);
        mSplitByKnn->setEnabled(false);	
        mRealignSpikes->setEnabled(false);
        mPcaAlignAllClusters->setEnabled(false);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
        mGenerateProbeDrift->setEnabled(false);
        mApplyDriftSiblings->setEnabled(false);
        scaleByShouler->setEnabled(false);
        timeFrameMode->setEnabled(false);
        mRenumberClusters->setEnabled(false);
        overlayPresentation->setEnabled(false);
        meanPresentation->setEnabled(false);

        noScale->setEnabled(false);

        scaleByMax->setEnabled(false);

        shoulderLine->setEnabled(false);

        mSelectTime->setEnabled(false);

        mIncreaseAmplitude->setEnabled(false);
        mDecreaseAmplitude->setEnabled(false);

        mIncreaseChannelAmplitudes->setEnabled(false);
        mDecreaseChannelAmplitudes->setEnabled(false);

        mNextSpike->setEnabled(false);
        mPreviousSpike->setEnabled(false);

        showHideLabels->setEnabled(false);
        mOpenAction->setEnabled(true);
        mFileOpenRecent->setEnabled(true);

    } else if(state == QLatin1String("documentState")) {
        viewClusterInfo->setEnabled(true);
        mIncreaseAmplitudeCorrelation->setEnabled(true);
        mDecreaseAmplitudeCorrelation->setEnabled(true);

        mDeleteNoisySpikes->setEnabled(true);
        mSaveAction->setEnabled(true);
        mSaveAsAction->setEnabled(true);
        mPrintAction->setEnabled(true);
        mRenumberAndSave->setEnabled(true);
        mCloseAction->setEnabled(true);
        mSelectAllAction->setEnabled(true);
        mSelectAllExceptAction->setEnabled(true);
        mZoomAction->setEnabled(true);
        newClusterDisplay->setEnabled(true);
        mCloseActiveDisplay->setEnabled(true);
        mNewCluster->setEnabled(true);
        mSplitClusters->setEnabled(true);
        mGroupeClusters->setEnabled(true);
        mAutoMerge->setEnabled(true);
        mChunkMode->setEnabled(true);
        mSortClustersBySpikeCount->setEnabled(true);
        mSortClustersByTime->setEnabled(true);
        mSortClustersByContamination->setEnabled(true);
        mSortClustersBySnr->setEnabled(true);
        mSortClustersByAmplitude->setEnabled(true);
        mSortClustersByAmplitudeByChannel->setEnabled(true);
        mSortClustersByErrorPval->setEnabled(true);
        mPurgeSmallClusters->setEnabled(true);
        mDeleteNoisy->setEnabled(true);
        mDeleteArtifact->setEnabled(true);
        newGroupingAssistantDisplay->setEnabled(true);
        mDeleteArtifactSpikes->setEnabled(true);
        mReCluster->setEnabled(true);
        mReclusterMedian->setEnabled(true);
        mReclusterChannelVar->setEnabled(true);
        mSplitByKnn->setEnabled(true);	
        mRealignSpikes->setEnabled(true);
        mPcaAlignAllClusters->setEnabled(true);
        nudgeMinusAction->setEnabled(true);
        nudgePlusAction->setEnabled(true);
        mGenerateProbeDrift->setEnabled(true);
        mApplyDriftSiblings->setEnabled(true);
        scaleByShouler->setEnabled(true);
        timeFrameMode->setEnabled(true);
        mDeleteNoisy->setEnabled(true);
        mRenumberClusters->setEnabled(true);
        newCrosscorrelationDisplay->setEnabled(true);
        overlayPresentation->setEnabled(true);
        newWaveformDisplay->setEnabled(true);
        noScale->setEnabled(true);
        newOverViewDisplay->setEnabled(true);
        meanPresentation->setEnabled(true);

        scaleByMax->setEnabled(true);
        shoulderLine->setEnabled(true);

        mIncreaseAmplitude->setEnabled(true);
        mDecreaseAmplitude->setEnabled(true);
        mRenameActiveDisplay->setEnabled(true);
        nudgeMinusAction->setEnabled(true);
        nudgePlusAction->setEnabled(true);
    } else if(state == QLatin1String("traceDisplayState")) {
        mNewTraceDisplay->setEnabled(true);
    } else if(state == QLatin1String("noTraceDisplayState")) {
        mNewTraceDisplay->setEnabled(false);
    } else if(state == QLatin1String("immediateSelectionState")) {
        mUpdateDisplay->setEnabled(false);
    } else if(state == QLatin1String("delaySelectionState")) {
        mUpdateDisplay->setEnabled(true);
    } else if(state == QLatin1String("SavingState")) {
        mSaveAction->setEnabled(false);
        mSaveAsAction->setEnabled(false);
        mRenumberAndSave->setEnabled(false);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
    } else if(state == QLatin1String("SavingDoneState")) {
        mSaveAction->setEnabled(true);
        mSaveAsAction->setEnabled(true);
        mRenumberAndSave->setEnabled(true);
        nudgeMinusAction->setEnabled(true);
        nudgePlusAction->setEnabled(true);
    } else if(state == QLatin1String("undoState")) {
        mUndo->setEnabled(true);
        mRedo->setEnabled(true);
    } else if(state == QLatin1String("emptyRedoState")) {
        mRedo->setEnabled(false);
    } else if(state == QLatin1String("emptyUndoState")) {
        mUndo->setEnabled(false);
        mRedo->setEnabled(true);
    } else if(state == QLatin1String("noWaveformsViewState")) {
        timeFrameMode->setEnabled(false);
        overlayPresentation->setEnabled(false);
        mDecreaseAmplitude->setEnabled(false);
        mIncreaseAmplitude->setEnabled(false);
        meanPresentation->setEnabled(false);

    } else if(state == QLatin1String("waveformsViewState")) {
        mDeleteArtifact->setEnabled(true);
        timeFrameMode->setEnabled(true);
        mRenumberClusters->setEnabled(true);
        overlayPresentation->setEnabled(true);
        mIncreaseAmplitude->setEnabled(true);
        meanPresentation->setEnabled(true);

        mDecreaseAmplitude->setEnabled(true);

        mDeleteNoisy->setEnabled(true);

        mGroupeClusters->setEnabled(true);

        mAutoMerge->setEnabled(true);
        mChunkMode->setEnabled(true);

        mSortClustersBySpikeCount->setEnabled(true);
        mSortClustersByTime->setEnabled(true);
        mSortClustersByContamination->setEnabled(true);
        mSortClustersBySnr->setEnabled(true);
        mSortClustersByAmplitude->setEnabled(true);
        mSortClustersByAmplitudeByChannel->setEnabled(true);
        mSortClustersByErrorPval->setEnabled(true);

        mPurgeSmallClusters->setEnabled(true);
    } else if(state == QLatin1String("noClusterViewState")) {
        mZoomAction->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mSelectTime->setEnabled(false);

    } else if(state == QLatin1String("clusterViewState")) {
        mZoomAction->setEnabled(true);
        mDeleteNoisy->setEnabled(true);
        mNewCluster->setEnabled(true);
        mSplitClusters->setEnabled(true);
        mDeleteArtifact->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(true);
        mRenumberClusters->setEnabled(true);
        mDeleteNoisy->setEnabled(true);
        mGroupeClusters->setEnabled(true);
        mAutoMerge->setEnabled(true);
        mChunkMode->setEnabled(true);
        mSortClustersBySpikeCount->setEnabled(true);
        mSortClustersByTime->setEnabled(true);
        mSortClustersByContamination->setEnabled(true);
        mSortClustersBySnr->setEnabled(true);
        mSortClustersByAmplitude->setEnabled(true);
        mSortClustersByAmplitudeByChannel->setEnabled(true);
        mSortClustersByErrorPval->setEnabled(true);
        mPurgeSmallClusters->setEnabled(true);
    } else if(state == QLatin1String("noCorrelationViewState")) {
        scaleByMax->setEnabled(false);
        scaleByShouler->setEnabled(false);
        noScale->setEnabled(false);
        mIncreaseAmplitudeCorrelation->setEnabled(false);
        mDecreaseAmplitudeCorrelation->setEnabled(false);
        shoulderLine->setEnabled(false);
    } else if(state == QLatin1String("correlationViewState")) {
        mIncreaseAmplitudeCorrelation->setEnabled(true);
        mDecreaseAmplitudeCorrelation->setEnabled(true);
        shoulderLine->setEnabled(true);
        mDeleteArtifact->setEnabled(true);
        scaleByMax->setEnabled(true);
        scaleByShouler->setEnabled(true);
        noScale->setEnabled(true);
        mDeleteNoisy->setEnabled(true);
        mRenumberClusters->setEnabled(true);
        mGroupeClusters->setEnabled(true);
        mAutoMerge->setEnabled(true);
        mChunkMode->setEnabled(true);
        mSortClustersBySpikeCount->setEnabled(true);
        mSortClustersByTime->setEnabled(true);
        mSortClustersByContamination->setEnabled(true);
        mSortClustersBySnr->setEnabled(true);
        mSortClustersByAmplitude->setEnabled(true);
        mSortClustersByAmplitudeByChannel->setEnabled(true);
        mSortClustersByErrorPval->setEnabled(true);
        mPurgeSmallClusters->setEnabled(true);
    } else if(state == QLatin1String("noErrorMatrixViewState")) {
        mUpdateErrorMatrix->setEnabled(false);
    } else if(state == QLatin1String("errorMatrixViewState")) {
        mUpdateErrorMatrix->setEnabled(true);
        newGroupingAssistantDisplay->setEnabled(false);
        mDeleteNoisy->setEnabled(true);
        mRenumberClusters->setEnabled(true);

        mDeleteArtifact->setEnabled(true);
        mGroupeClusters->setEnabled(true);
        mAutoMerge->setEnabled(true);
        mChunkMode->setEnabled(true);
        mSortClustersBySpikeCount->setEnabled(true);
        mSortClustersByTime->setEnabled(true);
        mSortClustersByContamination->setEnabled(true);
        mSortClustersBySnr->setEnabled(true);
        mSortClustersByAmplitude->setEnabled(true);
        mSortClustersByAmplitudeByChannel->setEnabled(true);
        mSortClustersByErrorPval->setEnabled(true);
        mPurgeSmallClusters->setEnabled(true);

    } else if(state == QLatin1String("groupingAssistantDisplayExists")) {
        newGroupingAssistantDisplay->setEnabled(false);
    } else if(state == QLatin1String("groupingAssistantDisplayNotExists")) {
        newGroupingAssistantDisplay->setEnabled(true);
    } else if(state == QLatin1String("reclusterViewState")) {
        mZoomAction->setEnabled(false);
        mUpdateErrorMatrix->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mDeleteNoisySpikes->setEnabled(false);
        mDeleteArtifact->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mReclusterMedian->setEnabled(false);
        mReclusterChannelVar->setEnabled(false);
        mRealignSpikes->setEnabled(false);
        mPcaAlignAllClusters->setEnabled(false);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
        scaleByShouler->setEnabled(false);
        timeFrameMode->setEnabled(false);
        noScale->setEnabled(false);
        meanPresentation->setEnabled(false);
        overlayPresentation->setEnabled(false);
        mRenumberClusters->setEnabled(false);

        scaleByMax->setEnabled(false);
        mGroupeClusters->setEnabled(false);
        mAutoMerge->setEnabled(false);
        mChunkMode->setEnabled(false);
        mSortClustersBySpikeCount->setEnabled(false);
        mSortClustersByTime->setEnabled(false);
        mSortClustersByContamination->setEnabled(false);
        mSortClustersBySnr->setEnabled(false);
        mSortClustersByAmplitude->setEnabled(false);
        mSortClustersByAmplitudeByChannel->setEnabled(false);
        mSortClustersByErrorPval->setEnabled(false);
        mPurgeSmallClusters->setEnabled(false);
        shoulderLine->setEnabled(false);
        mIncreaseAmplitude->setEnabled(false);
        mDecreaseAmplitude->setEnabled(false);

        mNextSpike->setEnabled(false);
        mPreviousSpike->setEnabled(false);
        mUndo->setEnabled(false);
        mRedo->setEnabled(false);

    } else if(state == QLatin1String("reclusterState")) {
        mUndo->setEnabled(false);
        mRedo->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);
        mGroupeClusters->setEnabled(false);
        mAutoMerge->setEnabled(false);
        mChunkMode->setEnabled(false);
        mSortClustersBySpikeCount->setEnabled(false);
        mSortClustersByTime->setEnabled(false);
        mSortClustersByContamination->setEnabled(false);
        mSortClustersBySnr->setEnabled(false);
        mSortClustersByAmplitude->setEnabled(false);
        mSortClustersByAmplitudeByChannel->setEnabled(false);
        mSortClustersByErrorPval->setEnabled(false);
        mPurgeSmallClusters->setEnabled(false);
        mDeleteArtifact->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mReclusterMedian->setEnabled(false);
        mReclusterChannelVar->setEnabled(false);
        mRealignSpikes->setEnabled(false);
        mPcaAlignAllClusters->setEnabled(false);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
        mRenumberClusters->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mIncreaseAmplitudeCorrelation->setEnabled(false);
        mDecreaseAmplitudeCorrelation->setEnabled(false);
    } else if(state == QLatin1String("noReclusterState")) {
        mReCluster->setEnabled(true);
        mReclusterMedian->setEnabled(true);
        mReclusterChannelVar->setEnabled(true);
        mRealignSpikes->setEnabled(true);
        mPcaAlignAllClusters->setEnabled(true);
        nudgeMinusAction->setEnabled(true);
        nudgePlusAction->setEnabled(true);

    // ── Realignment states — mirror reclusterState locks exactly ─────────────
    } else if(state == QLatin1String("realignState")) {
        // Lock all editing actions for the duration of the realignment job.
        mUndo->setEnabled(false);
        mRedo->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);
        mGroupeClusters->setEnabled(false);
        mAutoMerge->setEnabled(false);
        mChunkMode->setEnabled(false);
        mSortClustersBySpikeCount->setEnabled(false);
        mSortClustersByTime->setEnabled(false);
        mSortClustersByContamination->setEnabled(false);
        mSortClustersBySnr->setEnabled(false);
        mSortClustersByAmplitude->setEnabled(false);
        mSortClustersByAmplitudeByChannel->setEnabled(false);
        mSortClustersByErrorPval->setEnabled(false);
        mPurgeSmallClusters->setEnabled(false);
        mDeleteArtifact->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mReclusterMedian->setEnabled(false);
        mReclusterChannelVar->setEnabled(false);
        mRealignSpikes->setEnabled(false);
        mPcaAlignAllClusters->setEnabled(false);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
        mRenumberClusters->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mDeleteNoisySpikes->setEnabled(false);
        mIncreaseAmplitudeCorrelation->setEnabled(false);
        mDecreaseAmplitudeCorrelation->setEnabled(false);
        mSaveAction->setEnabled(false);
        mSaveAsAction->setEnabled(false);
        mRenumberAndSave->setEnabled(false);
        // Hierarchy-menu mutators initiate parent/child membership changes too, so
        // they must be locked for the duration of any realign (manual or post-edit);
        // otherwise a GUI-thread edit could race the background realign worker.
        if (mMergeParents)    mMergeParents->setEnabled(false);
        if (mPromoteChild)   mPromoteChild->setEnabled(false);
        if (mGroupChildren)  mGroupChildren->setEnabled(false);
        if (mDissolveParent)  mDissolveParent->setEnabled(false);
        if (mDropChildNoise) mDropChildNoise->setEnabled(false);
        if (mRepairNesting)     mRepairNesting->setEnabled(false);
        if (mMergeChildren)  mMergeChildren->setEnabled(false);
        if (mUndoChildEdit)  mUndoChildEdit->setEnabled(false);
        if (mRedoChildEdit)  mRedoChildEdit->setEnabled(false);

    } else if(state == QLatin1String("noRealignState")) {
        // Restore all actions that realignState locked.
        mRealignSpikes->setEnabled(true);
        mPcaAlignAllClusters->setEnabled(true);
        nudgeMinusAction->setEnabled(true);
        nudgePlusAction->setEnabled(true);
        mSaveAction->setEnabled(true);
        mSaveAsAction->setEnabled(true);
        mRenumberAndSave->setEnabled(true);
        // Restore editing actions (mirror what realignState disabled)
        mUndo->setEnabled(true);
        mRedo->setEnabled(true);
        mNewCluster->setEnabled(true);
        mSplitClusters->setEnabled(true);
        mGroupeClusters->setEnabled(true);
        mAutoMerge->setEnabled(true);
        mChunkMode->setEnabled(true);
        mSortClustersBySpikeCount->setEnabled(true);
        mSortClustersByTime->setEnabled(true);
        mSortClustersByContamination->setEnabled(true);
        mSortClustersBySnr->setEnabled(true);
        mSortClustersByAmplitude->setEnabled(true);
        mSortClustersByAmplitudeByChannel->setEnabled(true);
        mSortClustersByErrorPval->setEnabled(true);
        mPurgeSmallClusters->setEnabled(true);
        mDeleteArtifact->setEnabled(true);
        mDeleteArtifactSpikes->setEnabled(true);
        mReCluster->setEnabled(true);
        mReclusterMedian->setEnabled(true);
        mReclusterChannelVar->setEnabled(true);
        mRenumberClusters->setEnabled(true);
        mDeleteNoisy->setEnabled(true);
        mDeleteNoisySpikes->setEnabled(true);
        mIncreaseAmplitudeCorrelation->setEnabled(true);
        mDecreaseAmplitudeCorrelation->setEnabled(true);
        // Restore the hierarchy-menu mutators locked by realignState.
        if (mMergeParents)    mMergeParents->setEnabled(true);
        if (mPromoteChild)   mPromoteChild->setEnabled(true);
        if (mGroupChildren)  mGroupChildren->setEnabled(true);
        if (mDissolveParent)  mDissolveParent->setEnabled(true);
        if (mDropChildNoise) mDropChildNoise->setEnabled(true);
        if (mRepairNesting)     mRepairNesting->setEnabled(true);
        if (mMergeChildren)  mMergeChildren->setEnabled(true);
        if (mUndoChildEdit)  mUndoChildEdit->setEnabled(true);
        if (mRedoChildEdit)  mRedoChildEdit->setEnabled(true);
        // Re-sync with tab state so any tab-specific disabling is reapplied.
        slotTabChange(tabsParent->currentIndex());

    } else if(state == QLatin1String("traceViewState")) {
        mIncreaseChannelAmplitudes->setEnabled(true);
        mDecreaseChannelAmplitudes->setEnabled(true);
        showHideLabels->setEnabled(true);
    } else if(state == QLatin1String("noTraceViewState")) {
        mIncreaseChannelAmplitudes->setEnabled(false);
        mDecreaseChannelAmplitudes->setEnabled(false);
        showHideLabels->setEnabled(false);
        mNextSpike->setEnabled(false);
        mPreviousSpike->setEnabled(false);
    } else if(state == QLatin1String("traceViewClusterViewState")) {
        mSelectTime->setEnabled(true);
    } else if(state == QLatin1String("traceViewBrowsingState")) {
        mNextSpike->setEnabled(true);
        mPreviousSpike->setEnabled(true);
    } else if(state == QLatin1String("noTraceViewBrowsingState")) {
        mNextSpike->setEnabled(false);
        mPreviousSpike->setEnabled(false);
    } else {
        qWarning() <<" State unknown :"<<state;
    }

}
