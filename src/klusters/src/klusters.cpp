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

const QString KlustersApp::INITIAL_WAVEFORM_TIME_WINDOW = "30";
const long KlustersApp::DEFAULT_NB_SPIKES_DISPLAYED = 100;
const QString KlustersApp::INITIAL_CORRELOGRAMS_HALF_TIME_FRAME = "30";
const QString KlustersApp::DEFAULT_BIN_SIZE = "1";


KlustersApp::KlustersApp()
    : QMainWindow(nullptr),
      displayCount(0),
      mainDock(nullptr),
      clusterPanel(nullptr),
      clusterPalette(nullptr),
      tabsParent(nullptr),
      dimensionX(nullptr),
      dimensionY(nullptr),
      isInit(true),
      currentNbUndo(0),
      currentNbRedo(0),
      timeWindow(INITIAL_WAVEFORM_TIME_WINDOW.toLong()),
      validator(this),
      spikesTodisplayStep(20),
      correlogramTimeFrame(INITIAL_CORRELOGRAMS_HALF_TIME_FRAME.toInt() * 2 + 1),
      binSize(DEFAULT_BIN_SIZE.toInt()),
      binSizeValidator(this),
      correlogramsHalfTimeFrameValidator(this),
      prefDialog(nullptr),
      processWidget(nullptr),
      processFinished(true),
      processOutputsFinished(true),
      processKilled(false),
      reclusterRetryTimer(nullptr),
      realignWorker(nullptr),
      realignThread(nullptr),
      realignQueue(nullptr),
      realignRunning(false),
      realignClusterId(-1),
      realignBatchActive(false),
      realignBatchTotal(0),
      realignBatchAccepted(0),
      realignBatchFailed(0),
      realignBatchShiftedTotal(0),
      realignProgressBar(nullptr),
      sortProgressBar(nullptr),
      errorMatrixExists(false),
      templateMatrixExists(false),
      // Null-initialized here because initializePreferences() -> buildRealignArgs()
      // runs in this constructor (below) BEFORE initSelectionBoxes() creates these
      // widgets.  buildRealignArgs() guards with `realignTopChanSpinBox ? ... : 0`,
      // which is only correct if the pointer is genuinely null; an indeterminate
      // (garbage) value passes the test and is then dereferenced, calling
      // SpinBox::value() -> QVariant::toInt() on junk -> SIGSEGV.  The crash is
      // memory-state dependent (garbage==0 survives, !=0 crashes), so it surfaced
      // on some machines and not others.  Mirrors dimensionX/dimensionY above,
      // which are sibling SpinBoxes created in the same initSelectionBoxes().
      realignTopChanSpinBox(nullptr),
      realignTopChanSpinBoxAction(nullptr)
{
    setObjectName("Klusters");

    initView();

    mMainToolBar = new QToolBar(tr("Main Actions"));
    mMainToolBar->setObjectName("Main Actions");

    mActionBar = new QToolBar(tr("Actions"));
    mActionBar->setObjectName("Actions");

    mToolBar = new QToolBar(tr("Tools"));
    mToolBar->setObjectName("Tools");

    mClusterBar = new QToolBar(tr("Clusters Actions"));
    mClusterBar->setObjectName("Clusters Actions");

    //Create a KlustersDoc which will hold the document manipulated by the application.
    doc = new KlustersDoc(this,*clusterPalette,configuration().isCrashRecovery(),configuration().crashRecoveryInterval());


    createMenus();
    createToolBar();

    //Apply the user settings.
    initializePreferences();

    ///////////////////////////////////////////////////////////////////
    // call inits to invoke all other construction parts
    initStatusBar();


    //create the thread which will be used to save the cluster file
    saveThread = new SaveThread(this);


    //Prepare the spineboxes and line edit
    initSelectionBoxes();

    // Push the auto-feature settings onto the toolbar now that the widgets exist.
    // This has to happen HERE and not in initializePreferences(): that runs above,
    // before initSelectionBoxes() has created the spin box and the "time" checkbox,
    // so it has nothing to show or hide.  Without this call the only thing that ever
    // pushed the settings was applyPreferences(), which fires only when the
    // preferences dialog reports an actual CHANGE -- so with auto-select already
    // enabled in the saved settings, the widgets stayed hidden from launch and the
    // only way to see them was to toggle the preference off, apply, on, apply.
    syncAutoFeatureToolbar();

    setMinimumSize(QSize(600,400));


    slotUpdateParameterBar();

    //Disable some actions at startup (see the klustersui.rc file)
    slotStateChanged("initState");
    readSettings();
}

KlustersApp::~KlustersApp()
{
    NS3_DIAG() << "[~KlustersApp] entering";
    //Clear the memory by deleting all the pointers
    delete doc;
    delete saveThread;
    delete processWidget;
    processWidget = nullptr;

    // Realign worker cleanup — stop the thread if still running.
    if (realignThread) {
        if (realignWorker)
            qobject_cast<RealignWorker*>(realignWorker)->cancel();
        realignThread->quit();
        realignThread->wait(3000);
        delete realignThread;
        realignThread = nullptr;
    }
    // realignWorker is owned by the thread and deleted via deleteLater.
}

void KlustersApp::initView()
{
    initClusterPanel();
    QSplitter *splitter = new QSplitter;
    // Stack the main cluster palette over the (hidden) child palette so the
    // hierarchical view appears directly below it.
    QSplitter *clusterStack = new QSplitter(Qt::Vertical);
    clusterStack->setChildrenCollapsible(false);
    clusterStack->addWidget(clusterPanel);
    clusterStack->addWidget(childPanel);
    clusterStack->addWidget(recommendPanel);
    // Main palette gets the top half; the child palette and the recommendations
    // split the bottom half between them.  Sizes are a starting ratio only --
    // the splitter is user-draggable from here.
    clusterStack->setSizes({ 200, 100, 100 });
    splitter->addWidget(clusterStack);
    splitter->setChildrenCollapsible(false);
    tabsParent = new QExtendTabWidget(this);
    // Prevent the QTabWidget frame itself from ever holding keyboard focus.
    // Tab/Shift+Tab should cycle our explicit focus zones (buildFocusZones),
    // not get consumed by the tab bar's internal focus chain.
    tabsParent->setFocusPolicy(Qt::NoFocus);
    tabsParent->tabBar()->setFocusPolicy(Qt::NoFocus);
    splitter->addWidget(tabsParent);
    // App-level event filter so Tab/Shift+Tab cycle display tabs regardless
    // of which child widget (DockArea, ClusterView, ProcessWidget, …) holds focus.
    qApp->installEventFilter(this);
    QList<int> size;
    size <<150<<1000;
    splitter->setSizes(size);

    setCentralWidget(splitter);
}

void KlustersApp::createMenus()
{
    //File Menu
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    mOpenAction = fileMenu->addAction(tr("&Open..."));
    mOpenAction->setIcon(QPixmap(":/shared-icons/document-open"));
    mOpenAction->setShortcut(QKeySequence::Open);
    connect(mOpenAction, &QAction::triggered, this, &KlustersApp::slotFileOpen);

    QSettings settings;
    mFileOpenRecent = new QRecentFileAction(this);
    mFileOpenRecent->setRecentFiles(settings.value(QLatin1String("Recent Files"),QStringList()).toStringList());
    fileMenu->addAction(mFileOpenRecent);
    connect(mFileOpenRecent, &QRecentFileAction::recentFileSelected, this, &KlustersApp::slotFileOpenRecent);
    connect(mFileOpenRecent, &QRecentFileAction::recentFileListChanged, this, &KlustersApp::slotSaveRecentFiles);


    mImportFile = fileMenu->addAction(tr("&Import File"));
    mImportFile->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(mImportFile,&QAction::triggered, this,&KlustersApp::slotFileImport);

    fileMenu->addSeparator();

    mSaveAction = fileMenu->addAction(tr("Save..."));
    mSaveAction->setIcon(QPixmap(":/shared-icons/document-save"));
    mSaveAction->setShortcut(QKeySequence::Save);
    connect(mSaveAction, &QAction::triggered, this, &KlustersApp::slotFileSave);

    mSaveAsAction = fileMenu->addAction(tr("&Save As..."));
    mSaveAsAction->setIcon(QPixmap(":/shared-icons/document-save-as"));
    connect(mSaveAsAction, &QAction::triggered, this, &KlustersApp::slotFileSaveAs);

    mRenumberAndSave = fileMenu->addAction(tr("Re&number and Save"));
    mRenumberAndSave->setIcon(QIcon(QPixmap("filesave.png")));
    mRenumberAndSave->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(mRenumberAndSave,&QAction::triggered, this,&KlustersApp::slotFileRenumberAndSave);

    fileMenu->addSeparator();

    mPrintAction = fileMenu->addAction(tr("Print"));
    mPrintAction->setIcon(QPixmap(":/shared-icons/document-print"));
    mPrintAction->setShortcut(QKeySequence::Print);
    connect(mPrintAction, &QAction::triggered, this, &KlustersApp::slotFilePrint);

    fileMenu->addSeparator();

    mCloseAction = fileMenu->addAction(tr("Close"));
    mCloseAction->setIcon(QPixmap(":/shared-icons/document-close"));
    connect(mCloseAction, &QAction::triggered, this, &KlustersApp::slotFileClose);

    fileMenu->addSeparator();

    mQuitAction = fileMenu->addAction(tr("Quit"));
    mQuitAction->setShortcut(QKeySequence::Quit);
    mQuitAction->setIcon(QPixmap(":/shared-icons/window-close"));
    connect(mQuitAction, &QAction::triggered, this, &KlustersApp::slotFileQuit);

    //Edit Menu
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));

    mUndo = editMenu->addAction(tr("Undo"));
    mUndo->setIcon(QPixmap(":/shared-icons/edit-undo"));
    mUndo->setShortcut(QKeySequence::Undo);
    connect(mUndo, &QAction::triggered, this, &KlustersApp::slotUndo);

    mRedo = editMenu->addAction(tr("Redo"));
    mRedo->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y));   // explicit Ctrl+Y so the
                                                              // Linux default Ctrl+Shift+Z
                                                              // is free for the atom layer
    mRedo->setIcon(QPixmap(":/shared-icons/edit-redo"));
    connect(mRedo, &QAction::triggered, this, &KlustersApp::slotRedo);

    editMenu->addSeparator();

    mSelectAllAction = editMenu->addAction(tr("Select &All"));
    mSelectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(mSelectAllAction, &QAction::triggered, this, &KlustersApp::slotSelectAll);

    editMenu->addSeparator();

    mSelectAllExceptAction = editMenu->addAction(tr("Select All Except 0 and 1"));
    mSelectAllExceptAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
    connect(mSelectAllExceptAction, &QAction::triggered, this, &KlustersApp::slotSelectAllWO01);


    //Actions menu
    QMenu *actionMenu = menuBar()->addMenu(tr("&Actions"));
    mDeleteArtifact = actionMenu->addAction(tr("Delete &Artifact Cluster(s)"));
    mDeleteArtifact->setIcon(QIcon(":/icons/delete_artefact"));
    mDeleteArtifact->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete));
    connect(mDeleteArtifact, &QAction::triggered, clusterPalette, static_cast<void(ClusterPalette::*)()>(&ClusterPalette::moveClustersToArtefact));

    mDeleteNoisy = actionMenu->addAction(tr("Delete &Noisy Cluster(s)"));
    mDeleteNoisy->setIcon(QIcon(":/icons/delete_noise"));
    mDeleteNoisy->setShortcut(Qt::Key_Delete);
    connect(mDeleteNoisy, &QAction::triggered, clusterPalette, static_cast<void(ClusterPalette::*)()>(&ClusterPalette::moveClustersToNoise));

    mGroupeClusters = actionMenu->addAction(tr("&Group Clusters"));
    mGroupeClusters->setIcon(QIcon(":/icons/group"));
    mGroupeClusters->setShortcut(Qt::Key_G);
    connect(mGroupeClusters, &QAction::triggered, clusterPalette, static_cast<void(ClusterPalette::*)()>(&ClusterPalette::groupClusters));

    //Time-chunk curation: step through the session one chunk at a time, with
    //only the chunk's clusters active (masking) so merge decisions are local.
    actionMenu->addSeparator();
    mChunkMode = actionMenu->addAction(tr("Time-&Chunk Mode"));
    mChunkMode->setCheckable(true);
    connect(mChunkMode, &QAction::toggled, this, &KlustersApp::slotChunkModeToggled);

    mPrevChunk = actionMenu->addAction(tr("Previous Chunk"));
    mPrevChunk->setShortcut(Qt::Key_PageUp);
    connect(mPrevChunk, &QAction::triggered, this, &KlustersApp::slotPrevChunk);

    mNextChunk = actionMenu->addAction(tr("Next Chunk"));
    mNextChunk->setShortcut(Qt::Key_PageDown);
    connect(mNextChunk, &QAction::triggered, this, &KlustersApp::slotNextChunk);

    mPrevChunk->setEnabled(false);
    mNextChunk->setEnabled(false);

    // Patch 0069 — Auto-Merge action.
    // Uses the same template-cross-correlation mechanism as KKE; settings
    // come from the Auto-Merge preferences tab (patch 0068).  Shortcut Shift+G
    // by analogy with G for Group.
    mAutoMerge = actionMenu->addAction(tr("&Auto-Merge Similar Clusters..."));
    mAutoMerge->setIcon(QIcon(":/icons/auto_merge_tool"));
    mAutoMerge->setShortcut(Qt::SHIFT | Qt::Key_G);
    connect(mAutoMerge, &QAction::triggered, this, &KlustersApp::slotAutoMerge);

    // Purge: move every cluster smaller than a user-entered spike count into the
    // noise cluster (1).  Bulk analogue of "Delete Noisy Cluster(s)"; asks for N
    // then a Yes/No confirmation (default Yes) before moving.
    mPurgeSmallClusters = actionMenu->addAction(tr("&Purge Small Clusters…"));
    connect(mPurgeSmallClusters, &QAction::triggered, this, &KlustersApp::slotPurgeSmallClusters);

    // Bulk artefact removal: move every spike more than 5 sigma from its cluster's
    // per-dimension feature mean (in any one dimension) into the artefact cluster.
    // Non-mutating scan first, then a Yes/No confirmation showing the exact spike
    // count (default No, since it is destructive); undoable.
    mStripOutliers = actionMenu->addAction(tr("Strip Feature &Outliers (5\u03c3)\u2026"));
    connect(mStripOutliers, &QAction::triggered, this, &KlustersApp::slotStripFeatureOutliers);

    mUpdateDisplay = actionMenu->addAction(tr("&Update Display"));
    mUpdateDisplay->setIcon(QIcon(":/icons/update"));
    connect(mUpdateDisplay,&QAction::triggered, clusterPalette,&ClusterPalette::updateClusters);

    actionMenu->addSeparator();

    mRenumberClusters = actionMenu->addAction(tr("&Renumber Clusters"));
    mRenumberClusters->setShortcut(Qt::Key_R);
    connect(mRenumberClusters,&QAction::triggered, doc,&KlustersDoc::renumberClusters);

    actionMenu->addSeparator();

    mUpdateErrorMatrix = actionMenu->addAction(tr("&Update Error Matrix"));
    mUpdateErrorMatrix->setIcon(QIcon(":/icons/grouping_assistant_update"));
    mUpdateErrorMatrix->setShortcut(Qt::Key_U);
    connect(mUpdateErrorMatrix,&QAction::triggered, this,&KlustersApp::slotUpdateErrorMatrix);

    // Open a mean-waveform residual matrix on the active display.  Diagnostic
    // display (asymmetric: cell(row A,col B) = variance of A's spikes about
    // B's template); also the source matrix for the spike-count-gated sort.
    // Group the cluster-ordering actions under a "Sort Clusters" submenu; they
    // keep their shortcuts, icons, and enabled-state wiring -- only the parent
    // menu changes.
    mClusterSortMenu = actionMenu->addMenu(tr("Sort Clusters"));
    QMenu* sortMenu = mClusterSortMenu;

    mReorderClustersBySimilarity = sortMenu->addAction(tr("Re&order Clusters by Similarity"));
    mReorderClustersBySimilarity->setIcon(QIcon(":/icons/reorder_by_similarity"));
    mReorderClustersBySimilarity->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_S));
    mReorderClustersBySimilarity->setToolTip(
        tr("Renumber clusters so that similar clusters get adjacent IDs, using\n"
           "single-linkage agglomerative clustering on the active similarity matrix.\n"
           "When both error and template matrices exist, uses whichever was most\n"
           "recently created or clicked.  Clusters 0 (artefact) and 1 (noise) are\n"
           "preserved at the start.  Undoable with Ctrl+Z."));
    // Disabled until at least one of the matrices is open.  Re-enabled
    // via slotStateChanged("errorMatrixViewState") and the equivalent
    // template-matrix wiring at the dock-creation site.
    mReorderClustersBySimilarity->setEnabled(false);
    connect(mReorderClustersBySimilarity,&QAction::triggered,
            this,&KlustersApp::slotReorderClustersBySimilarity);

    // Sort by spike count: renumber clusters so IDs run by descending spike
    // count (largest cluster becomes 2).  Needs no matrix, just the cluster
    // sizes, so it follows the document/cluster-op enabled state (mirrors
    // mAutoMerge) rather than the matrix-availability gate above.  Undoable.
    mSortClustersBySpikeCount = sortMenu->addAction(tr("Sort Clusters by Spike &Count"));
    mSortClustersBySpikeCount->setToolTip(
        tr("Renumber clusters so their IDs run from largest to smallest by spike\n"
           "count (the biggest cluster becomes 2).  Clusters 0 (artefact) and 1\n"
           "(noise) are preserved at the start.  Undoable with Ctrl+Z."));
    connect(mSortClustersBySpikeCount,&QAction::triggered,
            this,&KlustersApp::slotSortClustersBySpikeCount);

    // Sort by starting-edge time: renumber clusters so IDs run by ascending time
    // of each cluster's earliest spike (the cluster that fires first becomes 2).
    // Needs no matrix, just spike timestamps, so it follows the same
    // document/cluster-op enabled state as the spike-count sort.  Undoable.
    mSortClustersByTime = sortMenu->addAction(tr("Sort Clusters by &Time"));
    mSortClustersByTime->setToolTip(
        tr("Renumber clusters so their IDs run by ascending starting time — the\n"
           "cluster whose earliest spike comes first becomes 2.  Clusters 0\n"
           "(artefact) and 1 (noise) are preserved at the start.  Undoable with Ctrl+Z."));
    connect(mSortClustersByTime,&QAction::triggered,
            this,&KlustersApp::slotSortClustersByTime);

    // Sort by contamination: renumber clusters by descending refractory ISI-
    // violation fraction (2 ms window), so the most-contaminated cluster becomes
    // 2 and lands at the top of the palette for review.  Needs no matrix, just
    // spike timestamps, so it follows the same enabled state.  Undoable.
    mSortClustersByContamination = sortMenu->addAction(tr("Sort Clusters by C&ontamination"));
    mSortClustersByContamination->setToolTip(
        tr("Renumber clusters by descending refractory contamination — the\n"
           "fraction of inter-spike intervals shorter than 2 ms.  The most\n"
           "contaminated cluster becomes 2 (top of the palette).  Clusters 0\n"
           "(artefact) and 1 (noise) are preserved at the start.  Undoable with Ctrl+Z."));
    connect(mSortClustersByContamination,&QAction::triggered,
            this,&KlustersApp::slotSortClustersByContamination);

    // Sort by SNR: renumber clusters by descending mean-waveform SNR, so the
    // cleanest-waveform cluster becomes 2.  Reads the cached mean waveforms, so
    // it is only meaningful once waveforms are computed; it otherwise follows the
    // same enabled state as the other sorts.  Undoable.
    mSortClustersBySnr = sortMenu->addAction(tr("Sort Clusters by &SNR"));
    mSortClustersBySnr->setToolTip(
        tr("Renumber clusters by descending mean-waveform SNR (peak-to-trough on\n"
           "the best channel over baseline noise).  Best-SNR cluster becomes 2.\n"
           "Requires computed mean waveforms — clusters without one sort last.\n"
           "Clusters 0 (artefact) and 1 (noise) are preserved.  Undoable with Ctrl+Z."));
    connect(mSortClustersBySnr,&QAction::triggered,
            this,&KlustersApp::slotSortClustersBySnr);

    // Sort by amplitude: renumber clusters by descending peak-to-trough of the
    // mean waveform, measured on whichever channel carries it largest, so the
    // biggest unit becomes 2.  Reads the same waveform cache as the SNR sort.
    mSortClustersByAmplitude = sortMenu->addAction(tr("Sort Clusters by Largest &Amplitude"));
    mSortClustersByAmplitude->setToolTip(
        tr("Renumber clusters by descending peak-to-trough amplitude of the mean\n"
           "waveform, taken on the channel where it is largest.  The biggest unit\n"
           "becomes 2.  Requires computed mean waveforms — clusters without one\n"
           "sort last.  Clusters 0 (artefact) and 1 (noise) are preserved.\n"
           "Undoable with Ctrl+Z."));
    connect(mSortClustersByAmplitude,&QAction::triggered,
            this,&KlustersApp::slotSortClustersByAmplitude);

    // Tiered version: block the clusters by which channel carries their largest
    // amplitude (channel order, so the blocks run down the probe), and order each
    // block by descending amplitude.  Groups units by site while keeping the
    // biggest first within a site.
    mSortClustersByAmplitudeByChannel =
        sortMenu->addAction(tr("Sort Clusters by Largest Amplitude by C&hannel"));
    mSortClustersByAmplitudeByChannel->setToolTip(
        tr("Renumber clusters in blocks: one block per channel, in channel order,\n"
           "each holding the clusters whose largest peak-to-trough amplitude falls\n"
           "on that channel, ordered biggest first.  Requires computed mean\n"
           "waveforms — clusters without one sort last.  Clusters 0 (artefact) and\n"
           "1 (noise) are preserved.  Undoable with Ctrl+Z."));
    connect(mSortClustersByAmplitudeByChannel,&QAction::triggered,
            this,&KlustersApp::slotSortClustersByAmplitudeByChannel);

    // Sort by error-matrix p-value: renumber clusters by descending merge
    // affinity read from the active error matrix, so each cluster's strongest
    // merge candidate ends up adjacent and the top pairs land at low ids.
    // Reads a matrix view, so it reports if none is computed; it otherwise
    // follows the same enabled state as the other sorts.  Undoable.
    mSortClustersByErrorPval = sortMenu->addAction(tr("Sort Clusters by Error &p-value"));
    mSortClustersByErrorPval->setToolTip(
        tr("Renumber clusters by descending error-matrix merge affinity — each\n"
           "cluster's highest same-neuron probability against any other cluster.\n"
           "The strongest merge candidate becomes 2.  Requires a computed,\n"
           "up-to-date error matrix in the active display.  Undoable with Ctrl+Z."));
    connect(mSortClustersByErrorPval,&QAction::triggered,
            this,&KlustersApp::slotSortClustersByErrorPval);

    // Sort by residual, gated by spike count.  Partitions clusters into a
    // high-count block (>= a prompted threshold) and a low-count block, places
    // the high block first (upper-left of the matrix / low ids) and the low
    // block last (lower-right), and seriates each block by residual-matrix
    // similarity.  Needs a computed residual matrix, so it follows the matrix-
    // availability gate (enabled when a ResidualMatrixView is created).  Undoable.
    mSortByResidualGated = sortMenu->addAction(tr("Sort by Residual (&Gated by Count)"));
    mSortByResidualGated->setToolTip(
        tr("Renumber clusters using the residual matrix, gated by spike count.\n"
           "Clusters with >= the prompted threshold go to the upper-left (low\n"
           "ids), the rest to the lower-right; each block is seriated by\n"
           "residual similarity.  Requires a computed residual matrix.\n"
           "Clusters 0/1 are preserved at the start.  Undoable with Ctrl+Z."));
    mSortByResidualGated->setEnabled(false);
    connect(mSortByResidualGated,&QAction::triggered,
            this,&KlustersApp::slotSortByResidualGated);

    // Nearest-neighbour median-waveform sort: renumber clusters along a greedy
    // nearest-neighbour chain over their per-sample median waveforms, so that
    // waveform-similar clusters get adjacent IDs.  Reads the .spk file directly,
    // needs no matrix, so (like Sort by Spike Count) it is enabled by default and
    // follows the document/cluster-op state rather than the matrix gate.  Undoable.
    mSortByWaveformNN = sortMenu->addAction(tr("Sort by &Nearest-Neighbour Waveform"));
    mSortByWaveformNN->setToolTip(
        tr("Renumber clusters along a nearest-neighbour chain over their per-sample\n"
           "MEDIAN waveforms (Euclidean distance): each cluster is placed next to\n"
           "its most waveform-similar unused neighbour.  Reads the .spk file; needs\n"
           "no matrix.  Clusters 0/1 preserved at the start.  Undoable with Ctrl+Z."));
    connect(mSortByWaveformNN,&QAction::triggered,
            this,&KlustersApp::slotSortByWaveformNN);

    // Spectral (global) counterpart of the nearest-neighbour waveform sort: same
    // median-waveform distances, but ordered by the Fiedler vector of the
    // similarity Laplacian, so the layout respects global waveform structure and
    // not just local nearest neighbours.  Also matrix-free; enabled by default.
    mSortByWaveformSpectral = sortMenu->addAction(tr("Sort by Waveform (&Global / Spectral)"));
    mSortByWaveformSpectral->setToolTip(
        tr("Renumber clusters by spectral (Fiedler) seriation of their per-sample\n"
           "MEDIAN waveforms: orders clusters so the whole layout respects the\n"
           "GLOBAL waveform structure, not just each cluster's nearest neighbour.\n"
           "Reads the .spk file; needs no matrix.  Clusters 0/1 preserved at the\n"
           "start.  Undoable with Ctrl+Z."));
    connect(mSortByWaveformSpectral,&QAction::triggered,
            this,&KlustersApp::slotSortByWaveformSpectral);

    actionMenu->addSeparator();

    // Recluster + split methods grouped into their own Actions submenu.
    QMenu* reclusterMenu = actionMenu->addMenu(tr("&Recluster"));

    mReCluster = reclusterMenu->addAction(tr("Re&cluster"));
    mReCluster->setShortcut(QKeySequence(Qt::SHIFT  | Qt::Key_R));
    connect(mReCluster,&QAction::triggered, this,&KlustersApp::slotRecluster);

    mReclusterMedian = reclusterMenu->addAction(tr("Recluster (&median-waveform residual)"));
    mReclusterMedian->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
    mReclusterMedian->setToolTip(tr("Recluster the selected cluster(s) on the residuals of their pooled median waveform"));
    connect(mReclusterMedian,&QAction::triggered, this,&KlustersApp::slotReclusterMedianResidual);

    mReclusterChannelVar = reclusterMenu->addAction(tr("Recluster (&channel-variance features)"));
    mReclusterChannelVar->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_C));
    mReclusterChannelVar->setToolTip(tr("Recluster using the highest-variance channels' features"));
    connect(mReclusterChannelVar,&QAction::triggered, this,&KlustersApp::slotReclusterChannelVariance);

    reclusterMenu->addSeparator();

    mSplitByKnn = reclusterMenu->addAction(tr("Split by &KNN voting…"));
    mSplitByKnn->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_K));
    mSplitByKnn->setToolTip(
        tr("Partition the selected cluster into new sub-clusters using "
           "a K-nearest-neighbour vote against existing well-isolated "
           "clusters as references."));
    mSplitByKnn->setEnabled(false);
    connect(mSplitByKnn, &QAction::triggered,
            this, &KlustersApp::slotSplitClusterByKnn);

    
    // Watershed lives on the Actions menu proper, not in the Recluster submenu.
    // Its construction used to sit in the middle of the Tools menu block -- it was
    // added to reclusterMenu 121 lines after that submenu was built and after the
    // Tools menu had been created, so anyone adding a Tools item beside it would
    // have landed in the Recluster submenu instead.
    mWatershed = actionMenu->addAction(tr("&Watershed Split"));
    mWatershed->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_W));
    mWatershed->setStatusTip(tr(
        "Split the currently-shown clusters into one new cluster per "
        "density basin in the active scatter view."));
    connect(mWatershed, &QAction::triggered, this, &KlustersApp::slotWatershedSplit);

    actionMenu->addSeparator();

    mRealignSpikes = actionMenu->addAction(tr("R&ealign Spikes…"));
    mRealignSpikes->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
    mRealignSpikes->setToolTip(tr("Re-align spikes in the selected cluster to their true peak, "
                                   "update .res/.spk/.fet files, and swap ordering if needed."));
    connect(mRealignSpikes, &QAction::triggered, this, &KlustersApp::slotRealignSpikes);

    mPcaAlignAllClusters = actionMenu->addAction(tr("&PCA-Center Align All Clusters (top-N ch)"));
    mPcaAlignAllClusters->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_P));
    mPcaAlignAllClusters->setToolTip(tr(
        "Run PCA-centered spike alignment across every cluster (skipping "
        "noise=0 and artifact=1) using the channel count from the Top-Channels "
        "spin box (0 = all channels). "
        "Each cluster's result is auto-accepted as a pending change; the "
        "batch can be aborted via \"Abort Realignment\"."));
    connect(mPcaAlignAllClusters, &QAction::triggered,
            this, &KlustersApp::slotPcaAlignAllClusters);

    mDipSplit = reclusterMenu->addAction(tr("&DipSplit Selected Cluster"));
    mDipSplit->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_D));
    mDipSplit->setToolTip(
        tr("Test the selected cluster for hidden bimodality.  If a valley is\n"
           "detected along one of its top-3 principal components AND the two-\n"
           "cluster model beats the single-cluster BIC, split the cluster in two."));
    connect(mDipSplit, &QAction::triggered, this, &KlustersApp::slotDipSplit);

    actionMenu->addSeparator();

    mGenerateProbeDrift = actionMenu->addAction(tr("&Generate Probe Drift…"));
    // No keyboard shortcut: Shift+P is owned by "PCA-Center Align All Clusters"
    // (mPcaAlignAllClusters, above).  Both actions used to bind Shift+P, so Qt
    // reported an ambiguous shortcut overload and dispatched neither; probe
    // drift is now reachable from the Actions menu only.
    mGenerateProbeDrift->setToolTip(
        tr("Run ndm_estimatedrift on the current electrode group to estimate probe "
           "displacement over time.  Requires that this group is already curated "
           "(a .clu.N file exists).  Produces SESSION.drift alongside the data files."));
    connect(mGenerateProbeDrift, &QAction::triggered,
            this, &KlustersApp::slotGenerateProbeDrift);

    mApplyDriftSiblings = actionMenu->addAction(tr("Apply Drift + &Recluster Siblings…"));
    mApplyDriftSiblings->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F));
    mApplyDriftSiblings->setToolTip(
        tr("Compute drift-adaptive chunk boundaries from SESSION.drift and optionally "
           "re-run KlustaKwik on the other electrode groups that share this probe.  "
           "Requires SESSION.drift (generate it first with 'Generate Probe Drift')."));
    connect(mApplyDriftSiblings, &QAction::triggered,
            this, &KlustersApp::slotApplyDriftSiblings);


    //Tools menu
    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    mZoomAction = toolsMenu->addAction(tr("Zoom"));
    mZoomAction->setIcon(QIcon(":/icons/zoom_tool.png"));
    mZoomAction->setShortcut(Qt::Key_Z);
    connect(mZoomAction,&QAction::triggered, this,&KlustersApp::slotZoom);

    toolsMenu->addSeparator();

    mNewCluster = toolsMenu->addAction(tr("New Cluster"));
    mNewCluster->setIcon(QIcon(":/icons/new_cluster"));
    // Shortcut removed; reachable via menu / toolbar / "1" key (slotSingleNew).
    connect(mNewCluster,&QAction::triggered, this,&KlustersApp::slotSingleNew);

    mSplitClusters = toolsMenu->addAction(tr("&Split Clusters"));
    mSplitClusters->setIcon(QIcon(":/icons/new_clusters"));
    connect(mSplitClusters,&QAction::triggered, this,&KlustersApp::slotMultipleNew);

    toolsMenu->addSeparator();

    mDeleteArtifactSpikes = toolsMenu->addAction(tr("Delete &Artifact Spikes"));
    mDeleteArtifactSpikes->setIcon(QIcon(":/icons/delete_artefact_tool"));
    // Shortcut removed; reachable via menu / toolbar.
    connect(mDeleteArtifactSpikes,&QAction::triggered, this,&KlustersApp::slotDeleteArtefact);

    mDeleteNoisySpikes = toolsMenu->addAction(tr("Delete &Noisy Spikes"));
    mDeleteNoisySpikes->setIcon(QIcon(":/icons/delete_noise_tool"));
    // Shortcut removed; reachable via menu / toolbar.
    connect(mDeleteNoisySpikes,&QAction::triggered, this,&KlustersApp::slotDeleteNoise);

    toolsMenu->addSeparator();

    mSelectTime = toolsMenu->addAction(tr("Select Time"));
    mSelectTime->setIcon(QIcon(":/icons/time_tool"));
    // Note: Key_W formerly bound here; reassigned to Watershed split.
    // Select Time can still be invoked via the menu / toolbar icon.
    connect(mSelectTime,&QAction::triggered, this,&KlustersApp::slotSelectTime);




    //Waveforms menu
    QMenu *waveFormsMenu = menuBar()->addMenu(tr("&Waveforms"));
    timeFrameMode = waveFormsMenu->addAction(tr("&Time Frame"));
    // Shortcut removed; reachable via menu only.  This frees plain "T"
    // entirely for the palette-focus "renumber-to-end" intercept (which
    // is handled in eventFilter rather than as a QAction).
    timeFrameMode->setCheckable(true);
    connect(timeFrameMode,&QAction::triggered, this,&KlustersApp::slotTimeFrameMode);

    overlayPresentation = waveFormsMenu->addAction(tr("&Overlay"));
    overlayPresentation->setShortcut(Qt::Key_O);
    overlayPresentation->setCheckable(true);
    connect(overlayPresentation,&QAction::triggered, this,&KlustersApp::setOverLayPresentation);

    meanPresentation = waveFormsMenu->addAction(tr("&Mean and Standard Deviation"));
    meanPresentation->setShortcut(Qt::Key_M);
    meanPresentation->setCheckable(true);
    connect(meanPresentation,&QAction::triggered, this,&KlustersApp::slotMeanPresentation);

    waveFormsMenu->addSeparator();


    mIncreaseAmplitude = waveFormsMenu->addAction(tr("&Increase Amplitude"));
    mIncreaseAmplitude->setShortcut(Qt::Key_I);
    connect(mIncreaseAmplitude,&QAction::triggered, this,&KlustersApp::slotIncreaseAmplitude);

    mDecreaseAmplitude = waveFormsMenu->addAction(tr("&Decrease Amplitude"));
    mDecreaseAmplitude->setShortcut(Qt::Key_D);
    connect(mDecreaseAmplitude,&QAction::triggered, this,&KlustersApp::slotDecreaseAmplitude);

    timeFrameMode->setChecked(false);
    overlayPresentation->setChecked(false);
    meanPresentation->setChecked(false);


    //Correlations menu
    QMenu *correlationsMenu = menuBar()->addMenu(tr("&Correlations"));
    scaleByMax = correlationsMenu->addAction(tr("Scale by &Maximum"));

    QActionGroup *grp = new QActionGroup(this);
    grp->addAction(scaleByMax);
    // Shift+M is now the median-residual recluster shortcut; Scale-by-Maximum
    // stays available from the Correlations menu (no accelerator).
    scaleByMax->setCheckable(true);
    connect(scaleByMax,&QAction::triggered, this,&KlustersApp::slotScaleByMax);

    scaleByShouler = correlationsMenu->addAction(tr("Scale by &Asymptote"));
    grp->addAction(scaleByShouler);
    scaleByShouler->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A));
    scaleByShouler->setCheckable(true);
    connect(scaleByShouler,&QAction::triggered, this,&KlustersApp::slotScaleByShouler);

    noScale = correlationsMenu->addAction(tr("&Uniform Scale"));
    grp->addAction(noScale);
    noScale->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_U));
    noScale->setCheckable(true);
    connect(noScale,&QAction::triggered, this,&KlustersApp::slotNoScale);

    correlationsMenu->addSeparator();

    shoulderLine = correlationsMenu->addAction(tr("Asymptote &Line"));
    shoulderLine->setShortcut(Qt::Key_L);
    shoulderLine->setCheckable(true);
    connect(shoulderLine,&QAction::triggered, this,&KlustersApp::slotShoulderLine);

    correlationsMenu->addSeparator();

    //Initialize the presentation mode to scale by maximum.
    scaleByMax->setChecked(true);
    mIncreaseAmplitudeCorrelation = correlationsMenu->addAction(tr("&Increase Amplitude"));
    // Shortcut removed (Shift+I freed for future use; reach via menu only).
    connect(mIncreaseAmplitudeCorrelation,&QAction::triggered, this,&KlustersApp::slotIncreaseCorrelogramsAmplitude);

    mDecreaseAmplitudeCorrelation = correlationsMenu->addAction(tr("&Decrease Amplitude"));
    // Shortcut removed (Shift+D was conflicting with DipSplit; reach via menu only).
    connect(mDecreaseAmplitudeCorrelation,&QAction::triggered, this,&KlustersApp::slotDecreaseCorrelogramsAmplitude);



    //Traces menu
    QMenu *traceMenu = menuBar()->addMenu(tr("T&races"));
    mIncreaseChannelAmplitudes = traceMenu->addAction(tr("&Increase Channel Amplitudes"));
    mIncreaseChannelAmplitudes->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    connect(mIncreaseChannelAmplitudes,&QAction::triggered, this,&KlustersApp::slotIncreaseAllChannelsAmplitude);

    mDecreaseChannelAmplitudes = traceMenu->addAction(tr("&Decrease Channel Amplitudes"));
    mDecreaseChannelAmplitudes->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    connect(mDecreaseChannelAmplitudes,&QAction::triggered, this,&KlustersApp::slotDecreaseAllChannelsAmplitude);


    traceMenu->addSeparator();

    mNextSpike = traceMenu->addAction(tr("&Next Spike"));
    mNextSpike->setIcon(QIcon(":/icons/forwardCluster"));
    mNextSpike->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    connect(mNextSpike,&QAction::triggered, this,&KlustersApp::slotShowNextCluster);

    mPreviousSpike = traceMenu->addAction(tr("&Previous Spike"));
    mPreviousSpike->setIcon(QIcon(":/icons/backCluster"));
    mPreviousSpike->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    connect(mPreviousSpike,&QAction::triggered, this,&KlustersApp::slotShowPreviousCluster);


    traceMenu->addSeparator();

    showHideLabels = traceMenu->addAction(tr("Show &Labels"));
    showHideLabels->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    showHideLabels->setCheckable(true);
    connect(showHideLabels,&QAction::triggered, this,&KlustersApp::slotShowLabels);

    showHideLabels->setChecked(false);



    //Displays menu
    QMenu *displayMenu = menuBar()->addMenu(tr("&Displays"));
    mHierarchicalView = displayMenu->addAction(tr("Hierarchical Session (.clc + .clp)"));
    mHierarchicalView->setCheckable(true);
    mHierarchicalView->setChecked(false);
    mHierarchicalView->setEnabled(false);   // committed at open from the file set; never a runtime toggle
    mHierarchicalView->setToolTip(tr("Checked when the opened clustering has both a .clc child\n"
                                     "and a .clp parent-map sibling — the session is then\n"
                                     "hierarchical.  Determined by the files present at open, not\n"
                                     "toggled at runtime; a flat .clu session is the alternative."));
    // No toggled() connection: the mode is set once in the open path
    // (see slotHierarchicalViewToggled, called directly there).
    displayMenu->addSeparator();

    // Hierarchy edits (enabled only while the child palette is shown): merge the
    // fibers selected in the main palette; promote / move the children selected
    // in the child palette.  Each regenerates .clu/.clc/.clp on Save.
    QMenu* hierarchyMenu = menuBar()->addMenu(tr("&Hierarchy"));
    mMergeFibers = hierarchyMenu->addAction(tr("&Merge Selected Fibers"));
    mPromoteChild = hierarchyMenu->addAction(tr("&Promote Child to New Fiber"));
    mMoveChild = hierarchyMenu->addAction(tr("Move Child to Selected &Fiber"));
    hierarchyMenu->addSeparator();
    mGroupChildren = hierarchyMenu->addAction(tr("&Group Selected Children into New Fiber"));
    mDissolveFiber = hierarchyMenu->addAction(tr("&Dissolve Fiber into Children"));
    mDropChildNoise = hierarchyMenu->addAction(tr("Drop Child to &Noise"));
    mRefiberize = hierarchyMenu->addAction(tr("Re&fiberize (re-cut atoms onto fibers)"));
    mRefiberize->setShortcut(Qt::Key_F);
    mRefiberize->setToolTip(tr("Re-cut child atoms that now straddle more than one fiber so each\n"
                               "atom belongs to a single fiber again, and regenerate the .clp\n"
                               "parent map.  Run after reassigning / consolidating the fibers."));
    hierarchyMenu->addSeparator();
    mMergeChildren = hierarchyMenu->addAction(tr("Merge Selected &Children (atom)"));
    mMergeAllChildren = hierarchyMenu->addAction(tr("Merge &All Children into Self (flatten)"));
    mMergeAllChildren->setToolTip(tr("Walk every fiber and merge its child atoms into one self child\n"
                                     "(atom id == fiber id), so the atom layer becomes a copy of the\n"
                                     "fiber layer.  Discards all sub-mode structure -- undo with\n"
                                     "Ctrl+Shift+Z."));
    mUndoChildEdit = hierarchyMenu->addAction(tr("&Undo Child-Layer Edit"));
    mUndoChildEdit->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z));
    mRedoChildEdit = hierarchyMenu->addAction(tr("&Redo Child-Layer Edit"));
    mRedoChildEdit->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y));
    mMergeFibers->setEnabled(false);
    mPromoteChild->setEnabled(false);
    mMoveChild->setEnabled(false);
    mGroupChildren->setEnabled(false);
    mDissolveFiber->setEnabled(false);
    mDropChildNoise->setEnabled(false);
    mRefiberize->setEnabled(false);
    mMergeChildren->setEnabled(false);
    mMergeAllChildren->setEnabled(false);
    mUndoChildEdit->setEnabled(false);
    mRedoChildEdit->setEnabled(false);
    connect(mMergeFibers, &QAction::triggered, this, &KlustersApp::slotMergeFibers);
    connect(mPromoteChild, &QAction::triggered, this, &KlustersApp::slotPromoteChildren);
    connect(mMoveChild, &QAction::triggered, this, &KlustersApp::slotMoveChildrenToFiber);
    connect(mGroupChildren, &QAction::triggered, this, &KlustersApp::slotGroupChildrenIntoFiber);
    connect(mDissolveFiber, &QAction::triggered, this, &KlustersApp::slotDissolveFiber);
    connect(mDropChildNoise, &QAction::triggered, this, &KlustersApp::slotDropChildToNoise);
    connect(mRefiberize, &QAction::triggered, this, &KlustersApp::slotRefiberize);
    connect(mMergeChildren, &QAction::triggered, this, &KlustersApp::slotMergeChildren);
    connect(mMergeAllChildren, &QAction::triggered, this, &KlustersApp::slotMergeAllChildrenToSelf);
    connect(mUndoChildEdit, &QAction::triggered, this, &KlustersApp::slotUndoChildEdit);
    connect(mRedoChildEdit, &QAction::triggered, this, &KlustersApp::slotRedoChildEdit);
    //viewMenu = new QActionMenu(tr("&Window"), actionCollection(), "window_menu");
    newClusterDisplay = displayMenu->addAction(tr("New C&luster Display"));
    connect(newClusterDisplay,&QAction::triggered, this,&KlustersApp::slotWindowNewClusterDisplay);

    newWaveformDisplay = displayMenu->addAction(tr("New &Waveform Display"));
    connect(newWaveformDisplay,&QAction::triggered, this,&KlustersApp::slotWindowNewWaveformDisplay);

    newCrosscorrelationDisplay = displayMenu->addAction(tr("New C&orrelation Display"));
    connect(newCrosscorrelationDisplay,&QAction::triggered, this,&KlustersApp::slotWindowNewCrosscorrelationDisplay);

    // ???????????????
    newOverViewDisplay = displayMenu->addAction(tr("New &Overview Display"));
    connect(newOverViewDisplay,&QAction::triggered, this,&KlustersApp::slotWindowNewOverViewDisplay);

    newGroupingAssistantDisplay = displayMenu->addAction(tr("New &Grouping Assistant Display"));
    connect(newGroupingAssistantDisplay,&QAction::triggered, this,&KlustersApp::slotWindowNewGroupingAssistantDisplay);


    mNewTraceDisplay = displayMenu->addAction(tr("New &Trace Display"));
    connect(mNewTraceDisplay,&QAction::triggered, this,&KlustersApp::slotNewTraceDisplay);


    mNewResidualMatrix = displayMenu->addAction(tr("New &Residual Matrix Display"));
    mNewResidualMatrix->setToolTip(
        tr("Add a residual-matrix display to the active view.  Cell (row A,\n"
           "col B) is the variance of cluster A's spikes taken about cluster\n"
           "B's mean waveform; the diagonal is A's within-cluster variance.\n"
           "Press U to (re)compute it along with the error/template matrices."));
    connect(mNewResidualMatrix,&QAction::triggered, this,&KlustersApp::slotNewResidualMatrix);

    // Open a drift-shifted correlation matrix on the active display.  Purely
    // diagnostic: it never moves spikes and takes no part in the reorder /
    // residual-gated sort decisions.
    mNewDriftMatrix = displayMenu->addAction(tr("New &Drift Matrix Display"));
    mNewDriftMatrix->setToolTip(
        tr("Add a drift-matrix display to the active view.  Cell (row A,\n"
           "col B) is the correlation between A's mean waveform shifted along\n"
           "the probe depth axis by the slider's µm value and B's mean: +Δ\n"
           "above the diagonal, −Δ below.  A unit pair split by probe drift\n"
           "reads high at the Δ that realigns it.  Needs probe geometry\n"
           "(the session YAML probes: section)."));
    connect(mNewDriftMatrix,&QAction::triggered, this,&KlustersApp::slotNewDriftMatrix);

    displayMenu->addSeparator();

    mRenameActiveDisplay = displayMenu->addAction(tr("&Rename Active Display"));
    mRenameActiveDisplay->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(mRenameActiveDisplay,&QAction::triggered, this,&KlustersApp::renameActiveDisplay);

    displayMenu->addSeparator();

    mCloseActiveDisplay = displayMenu->addAction(tr("&Close Active Display"));
    mCloseActiveDisplay->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    connect(mCloseActiveDisplay,&QAction::triggered, this,&KlustersApp::slotDisplayClose);


    //Settings menu
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));


    viewMainToolBar = settingsMenu->addAction(tr("Show Main Toolbar"));

    viewMainToolBar->setCheckable(true);
    viewMainToolBar->setChecked(true);
    connect(viewMainToolBar,&QAction::triggered, this,&KlustersApp::slotViewMainToolBar);

    viewActionBar = settingsMenu->addAction(tr("Show Actions"));
    viewActionBar->setCheckable(true);
    connect(viewActionBar,&QAction::triggered, this,&KlustersApp::slotViewActionBar);

    viewActionBar->setChecked(true);
    viewToolBar = settingsMenu->addAction(tr("Show Tools"));
    viewToolBar->setCheckable(true);
    connect(viewToolBar,&QAction::triggered, this,&KlustersApp::slotViewToolBar);

    viewToolBar->setChecked(true);
    viewParameterBar = settingsMenu->addAction(tr("Show Parameters"));
    viewParameterBar->setCheckable(true);
    connect(viewParameterBar,&QAction::triggered, this,&KlustersApp::slotViewParameterBar);

    viewParameterBar->setChecked(true);

    viewClusterInfo = settingsMenu->addAction(tr("Show Cluster Info"));
    viewClusterInfo->setCheckable(true);
    connect(viewClusterInfo,&QAction::triggered, this,&KlustersApp::slotViewClusterInfo);

    viewClusterInfo->setChecked(false);



    mViewStatusBar = settingsMenu->addAction(tr("Show StatusBar"));
    mViewStatusBar->setCheckable(true);
    mViewStatusBar->setChecked(true);
    connect(mViewStatusBar,&QAction::triggered, this,&KlustersApp::slotViewStatusBar);

    settingsMenu->addSeparator();

    mIncreasePointSize = settingsMenu->addAction(tr("Increase Point Size"));
    mIncreasePointSize->setShortcuts({QKeySequence(Qt::Key_Equal),
                                      QKeySequence(Qt::SHIFT | Qt::Key_Equal)});
    connect(mIncreasePointSize, &QAction::triggered, this, &KlustersApp::slotIncreasePointSize);

    mDecreasePointSize = settingsMenu->addAction(tr("Decrease Point Size"));
    mDecreasePointSize->setShortcuts({QKeySequence(Qt::Key_Minus),
                                      QKeySequence(Qt::Key_Underscore)});
    connect(mDecreasePointSize, &QAction::triggered, this, &KlustersApp::slotDecreasePointSize);

    settingsMenu->addSeparator();

    mImmediateSelection = settingsMenu->addAction(tr("Immediate Update"));
    grp = new QActionGroup(this);
    grp->addAction(mImmediateSelection);
    mImmediateSelection->setCheckable(true);
    connect(mImmediateSelection,&QAction::triggered, this,&KlustersApp::slotImmediateSelection);

    mDelaySelection = settingsMenu->addAction(tr("Delayed Update"));
    grp->addAction(mDelaySelection);
    mDelaySelection->setCheckable(true);
    connect(mDelaySelection,&QAction::triggered, this,&KlustersApp::slotDelaySelection);

    settingsMenu->addSeparator();
    mPreferenceAction = settingsMenu->addAction(tr("&Preferences"));
    mPreferenceAction->setShortcut(Qt::Key_P);
    mPreferenceAction->setIcon(QIcon(":/shared-icons/configure"));
    connect(mPreferenceAction,&QAction::triggered, this,&KlustersApp::executePreferencesDlg);


    //Initialize the update mode
    mImmediateSelection->setChecked(true);
    settingsMenu->addSeparator();

    mPluginsMenu = menuBar()->addMenu(tr("&Plugins"));
    mPluginsMenu->setToolTipsVisible(true);
    populatePluginsMenu();

    QMenu *helpMenu = menuBar()->addMenu(tr("Help"));

    QAction *shortcuts = helpMenu->addAction(tr("Keyboard Shortcuts…"));
    shortcuts->setShortcut(Qt::Key_H);
    connect(shortcuts, &QAction::triggered, this, &KlustersApp::slotShowShortcutHelp);
    helpMenu->addSeparator();
    QAction *handbook = helpMenu->addAction(tr("Handbook"));
    handbook->setShortcut(Qt::Key_F1);
    connect(handbook,&QAction::triggered, this,&KlustersApp::slotHanbook);

    QAction *about = helpMenu->addAction(tr("About"));
    connect(about,&QAction::triggered, this,&KlustersApp::slotAbout);

    //Custom connections
    connect(clusterPalette, &ClusterPalette::singleChangeColor, this, &KlustersApp::slotSingleColorUpdate);
    connect(clusterPalette, &ClusterPalette::updateShownClusters, this, &KlustersApp::slotUpdateShownClusters);
    connect(childPaletteA, &ClusterPalette::updateShownClusters, this, &KlustersApp::slotChildSelectionChanged);
    connect(clusterPalette, static_cast<void(ClusterPalette::*)(const QList<int>&)>(&ClusterPalette::groupClusters), this, &KlustersApp::slotGroupClusters);
    connect(clusterPalette, static_cast<void(ClusterPalette::*)(const QList<int>&)>(&ClusterPalette::moveClustersToNoise), this, &KlustersApp::slotMoveClustersToNoise);
    connect(clusterPalette, static_cast<void(ClusterPalette::*)(const QList<int>&)>(&ClusterPalette::moveClustersToArtefact), this, &KlustersApp::slotMoveClustersToArtefact);
    connect(clusterPalette, &ClusterPalette::clusterInformationModified, this, &KlustersApp::slotClusterInformationModified);
    connect(clusterPalette, &ClusterPalette::paletteGainedFocus, this, &KlustersApp::slotShowOverviewForPalette);
    connect(doc, &KlustersDoc::updateUndoNb, this, &KlustersApp::slotUpdateUndoNb);
    connect(doc, &KlustersDoc::updateRedoNb, this, &KlustersApp::slotUpdateRedoNb);
    // A renumber (move-to-end, sort/reorder, the deferred auto-renumber after any
    // set-changing edit, or an explicit Renumber) rewrites the shown-cluster ids but
    // drives no view refresh -- so the dependent views (waveform / feature / ...) and
    // the child palette would stay stale until the next selection change.  renumber()
    // is otherwise unhooked (it carries no view mutation, so refreshing here cannot
    // re-enter the doc).  Re-drive the refresh for whatever is currently shown: the
    // child sub-selection if one is active (preserve the hierarchical child view),
    // else the parent selection.
    connect(doc, &KlustersDoc::renumber, this, [this](QMap<int,int>&){
        if(!activeView()) return;
        if(childPanel && childPanel->isVisible()
           && childPaletteA && !childPaletteA->selectedClusters().isEmpty())
            slotChildSelectionChanged({});
        else if(clusterPalette)
            slotUpdateShownClusters(clusterPalette->selectedClusters());
    });
    // hierarchical view: after a hierarchy edit or an undo/redo of one, refresh
    // the child palette for the current parent selection.
    connect(doc, &KlustersDoc::hierarchyChanged, this, [this]{
        if (childPanel && childPanel->isVisible())
            repopulateChildPalette(clusterPalette->selectedClusters());
    });
    // After a child (atom) split, hierarchyChanged has already repopulated the
    // child palette for the unchanged parent; land focus on the new sibling atoms.
    connect(doc, &KlustersDoc::hierarchyChildrenCreated, this,
            [this](const QList<int>& newChildren){
        if (!childPanel || !childPanel->isVisible() || newChildren.isEmpty()) return;
        ClusterPalette* cp = focusedChildPalette() ? focusedChildPalette() : childPalette;
        if (cp) {
            cp->selectItems(newChildren);
            cp->setFocusToList();
        }
    });
    connect(doc, &KlustersDoc::spikesDeleted, this, &KlustersApp::slotSpikesDeleted);

    // After a polygon-driven new-cluster operation in any 2D scatter view,
    // hand keyboard focus back to the cluster palette's iconView so the
    // user can arrow-navigate to (or away from) the freshly-created cluster
    // without first having to Tab or click.  The matching ClusterView
    // setFocus(self) calls were removed in NEW_CLUSTER / NEW_CLUSTERS to
    // stop the scatter from grabbing focus back synchronously after this
    // signal handler runs.  newClustersAdded is overloaded
    // (QMap-and-QList for createNewClusters; bare QList for recluster);
    // we want only the polygon-completion overload here.
    connect(doc, &KlustersDoc::newClusterAdded, this,
        [this](QList<int>&, int, QList<int>&) {
            if (clusterPalette) clusterPalette->setFocusToList();
        });
    connect(doc,
        static_cast<void (KlustersDoc::*)(QMap<int,int>&, QList<int>&)>(
            &KlustersDoc::newClustersAdded),
        this,
        [this](QMap<int,int>&, QList<int>&) {
            if (clusterPalette) clusterPalette->setFocusToList();
        });

    // Extend the post-merge auto-renumber / auto-update-matrices automation
    // (Preferences > Refinement) to the other cluster-editing operations.
    // These hooks are deferred + coalesced (scheduleAutoPostClusterEdit) so they
    // run once, after the triggering mutation completes, never re-entrantly.
    //   set-changing (delete / split / new-cluster / recluster) → renumber + matrix
    //   membership-only (spikesDeleted)                          → matrix only
    // Merge is intentionally NOT hooked here: clustersGrouped drives the
    // dedicated autoPostMerge() path, which must serialise behind auto-align.
    // renumber() is also not hooked, so the renumber these trigger can't recurse.
    connect(doc, &KlustersDoc::clustersDeleted, this,
        [this](QList<int>&, int) { scheduleAutoPostClusterEdit(true); });
    connect(doc, &KlustersDoc::removeSpikesFromClusters, this,
        [this](QList<int>&, int, QList<int>&) { scheduleAutoPostClusterEdit(true); });
    connect(doc, &KlustersDoc::newClusterAdded, this,
        [this](QList<int>&, int, QList<int>&) { scheduleAutoPostClusterEdit(true); });
    connect(doc,
        static_cast<void (KlustersDoc::*)(QMap<int,int>&, QList<int>&)>(
            &KlustersDoc::newClustersAdded),
        this,
        [this](QMap<int,int>&, QList<int>&) { scheduleAutoPostClusterEdit(true); });
    connect(doc,
        static_cast<void (KlustersDoc::*)(QList<int>&)>(
            &KlustersDoc::newClustersAdded),
        this,
        [this](QList<int>&) { scheduleAutoPostClusterEdit(true); });
    connect(doc, &KlustersDoc::spikesDeleted, this,
        [this]() { scheduleAutoPostClusterEdit(false); });
    // Hierarchy-menu ops (fiber merge, promote/move/group/dissolve/drop, child
    // recluster) emit hierarchyChanged rather than the flat signals above.  Route
    // them through the same coalesced post-edit step so each one realigns the
    // fibers it modified (drained from takeModifiedFibers) and refreshes matrices.
    // Atom-only ops modify no fibers, so the drain is empty and only the matrix
    // recompute runs.  (false: these never renumber on their own.)
    connect(doc, &KlustersDoc::hierarchyChanged, this,
        [this]() { scheduleAutoPostClusterEdit(false); });

    // Merging fibers emits hierarchyChanged, which is exactly when the
    // recommendations become wrong -- the merged pair no longer exists, and the
    // survivor's affinities have moved.  Refresh on it.
    connect(doc, &KlustersDoc::hierarchyChanged, this,
        [this]() { slotRefreshMergeRecommendations(); });
}


void KlustersApp::createToolBar()
{

    mMainToolBar->addAction(mOpenAction);
    mMainToolBar->addAction(mSaveAction);
    mMainToolBar->addAction(mPrintAction);
    mMainToolBar->addSeparator();
    mMainToolBar->addAction(mUndo);
    mMainToolBar->addAction(mRedo);

    addToolBar(mMainToolBar);

    mActionBar->addAction(mDeleteArtifact);
    mActionBar->addAction(mDeleteNoisy);
    mActionBar->addAction(mUpdateDisplay);
    mActionBar->addAction(mUpdateErrorMatrix);
    mActionBar->addAction(mReorderClustersBySimilarity);
    mActionBar->addAction(mGroupeClusters);
    mActionBar->addAction(mAutoMerge);  // patch 0069


    addToolBar(mActionBar);

    mToolBar->addAction(mZoomAction);
    mToolBar->addSeparator();
    mToolBar->addAction(mNewCluster);
    mToolBar->addAction(mSplitClusters);
    mToolBar->addSeparator();
    mToolBar->addAction(mDeleteArtifactSpikes);
    mToolBar->addAction(mDeleteNoisySpikes);
    mToolBar->addSeparator();
    mToolBar->addAction(mSelectTime);
    addToolBar(mToolBar);


    mClusterBar->addAction(mPreviousSpike);
    mClusterBar->addAction(mNextSpike);
    addToolBar(mClusterBar);
}

void KlustersApp::initSelectionBoxes(){
    QFont font("Helvetica",9);


    paramBar = addToolBar(tr("Parameters"));
    paramBar->setObjectName("Parameters");

    //Create and initialize the spin boxes for the dimensions
    dimensionX = new SpinBox(paramBar);
    dimensionX->setObjectName("dimensionX");
    dimensionX->setMinimum(1);
    dimensionX->setMaximum(1);
    dimensionX->setSingleStep(1);
    dimensionX->setFocusPolicy(Qt::StrongFocus);
	 connect(dimensionX, &SpinBox::valueChanged, dimensionX, &SpinBox::deselect, Qt::QueuedConnection);
	 
    dimensionY = new SpinBox(paramBar);
    dimensionY->setObjectName("dimensionY");
    dimensionY->setMinimum(1);
    dimensionY->setMaximum(1);
    dimensionY->setSingleStep(1);
    dimensionY->setFocusPolicy(Qt::StrongFocus);
	 connect(dimensionY, &SpinBox::valueChanged, dimensionY, &SpinBox::deselect, Qt::QueuedConnection);

    //Enable to step the value from the highest value to the lowest value and vice versa
    dimensionX->setWrapping(true);
    dimensionY->setWrapping(true);
    featureXLabel = new QLabel(tr("Features (x,y) "),paramBar);
    featureXLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    featureXLabel->setFont(font);
    //Insert the spine boxes in the main tool bar and make the connections
    featureXLabelAction = paramBar->addWidget(featureXLabel);
    dimensionXAction = paramBar->addWidget(dimensionX);
    dimensionYAction = paramBar->addWidget(dimensionY);
    connect(dimensionX, &SpinBox::valueChanged, this, &KlustersApp::slotUpdateDimensionX);
    connect(dimensionY, &SpinBox::valueChanged, this, &KlustersApp::slotUpdateDimensionY);

    //Create and initialize the spin boxe and lineEdit for the waveforms time frame mode.
    start = new SpinBox(paramBar);
    start->setObjectName("start");
    start->setMinimum(1);
    start->setMaximum(1);
    start->setSingleStep(timeWindow);
    start->setFocusPolicy(Qt::StrongFocus);
    connect(start, &SpinBox::valueChanged, start, &SpinBox::deselect, Qt::QueuedConnection);

    //Enable to step the value from the highest value to the lowest value and vice versa
    start->setWrapping(true);
    duration = new QLineEdit(paramBar);
    duration->setObjectName("INITIAL_WAVEFORM_TIME_WINDOW");
    duration->setMaxLength(5);
    //duration will only accept integers between 0 and a max equal
    //to maximum of time for the current document (set when the document will be opened)
    duration->setValidator(&validator);
    durationLabel = new QLabel("  Duration (s)",paramBar);
    durationLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    durationLabel->setFont(font);
    startLabel = new QLabel("  Start time (s)",paramBar);
    startLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    startLabel->setFont(font);
    startLabelAction = paramBar->addWidget(startLabel);
    start->setMinimumSize(70,start->minimumHeight());
    start->setMaximumSize(70,start->maximumHeight());
    startAction = paramBar->addWidget(start);
    durationLabelAction = paramBar->addWidget(durationLabel);
    duration->setMinimumSize(70,duration->minimumHeight());
    duration->setMaximumSize(70,duration->maximumHeight());
    durationAction = paramBar->addWidget(duration);
    connect(start, &SpinBox::valueChanged, this, &KlustersApp::slotUpdateStartTime);
    connect(duration, &QLineEdit::returnPressed,this, &KlustersApp::slotUpdateDuration);

    //Create and initialize the spin boxe for the waveforms sample mode.
    spikesTodisplay = new SpinBox(paramBar);
    spikesTodisplay->setMinimum(1);
    spikesTodisplay->setMaximum(1);
    spikesTodisplay->setSingleStep(spikesTodisplayStep);
    spikesTodisplay->setFocusPolicy(Qt::StrongFocus);
	 connect(spikesTodisplay, &SpinBox::valueChanged, spikesTodisplay, &SpinBox::deselect, Qt::QueuedConnection);

    spikesTodisplay->setObjectName("spikesTodisplay");
    //Enable to step the value from the highest value to the lowest value and vice versa
    spikesTodisplay->setWrapping(true);
    spikesTodisplayLabel = new QLabel("  Waveforms",paramBar);
    spikesTodisplayLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    spikesTodisplayLabel->setFont(font);
    spikesTodisplayLabelAction = paramBar->addWidget(spikesTodisplayLabel);
    spikesTodisplay->setMinimumSize(70,spikesTodisplay->minimumHeight());
    spikesTodisplay->setMaximumSize(70,spikesTodisplay->maximumHeight());
    spikesTodisplayAction = paramBar->addWidget(spikesTodisplay);
    connect(spikesTodisplay, &SpinBox::valueChanged, this, &KlustersApp::slotSpikesTodisplay);

    //Create and initialize the lineEdit for the correlations.
    binSizeBox = new QLineEdit(paramBar);
    binSizeBox->setObjectName("DEFAULT_BIN_SIZE");
    binSizeBox->setMaxLength(10);
    //binSizeBox will only accept integers between 1 and a max equal
    //to maximum of time for the current document in miliseconds (set when the document will be opened)
    binSizeBox->setValidator(&binSizeValidator);
    binSizeLabel = new QLabel("  Bin size (ms)",paramBar);
    binSizeLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    binSizeLabel->setFont(font);
    binSizeLabelAction = paramBar->addWidget(binSizeLabel);
    binSizeBox->setMinimumSize(30,binSizeBox->minimumHeight());
    binSizeBox->setMaximumSize(30,binSizeBox->maximumHeight());
    binSizeBoxAction = paramBar->addWidget(binSizeBox);
    connect(binSizeBox, &QLineEdit::returnPressed,this, &KlustersApp::slotUpdateBinSize);

    correlogramsHalfDuration = new QLineEdit(paramBar);
    correlogramsHalfDuration->setObjectName("INITIAL_CORRELOGRAMS_HALF_TIME_FRAME");
    correlogramsHalfDuration->setMaxLength(12);
    //correlogramsHalfDuration will only accept integers between 1 and a max equal
    //to half the maximum of time for the current document in miliseconds (set when the document will be opened)
    correlogramsHalfDuration->setValidator(&correlogramsHalfTimeFrameValidator);
    correlogramsHalfDurationLabel = new QLabel("  Duration (ms)",paramBar);
    correlogramsHalfDurationLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    correlogramsHalfDurationLabel->setFont(font);
    correlogramsHalfDurationLabelAction = paramBar->addWidget(correlogramsHalfDurationLabel);
    correlogramsHalfDuration->setMinimumSize(70,correlogramsHalfDuration->minimumHeight());
    correlogramsHalfDuration->setMaximumSize(70,correlogramsHalfDuration->maximumHeight());
    correlogramsHalfDurationAction = paramBar->addWidget(correlogramsHalfDuration);
    connect(correlogramsHalfDuration, &QLineEdit::returnPressed,this, &KlustersApp::slotUpdateCorrelogramsHalfDuration);

    // Auto-select N-features spinbox — appended at end of paramBar,
    // visible only when the feature selectors are visible AND autoSelectFeatures is on.
    autoNFeaturesLabel = new QLabel(tr("  N feat:"), paramBar);
    autoNFeaturesLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    autoNFeaturesLabel->setFont(font);
    autoNFeaturesSpinBox = new SpinBox(paramBar);
    autoNFeaturesSpinBox->setObjectName("autoNFeaturesSpinBox");
    autoNFeaturesSpinBox->setMinimum(1);
    autoNFeaturesSpinBox->setMaximum(25);
    autoNFeaturesSpinBox->setSingleStep(1);
    autoNFeaturesSpinBox->setWrapping(false);
    autoNFeaturesSpinBox->setFocusPolicy(Qt::StrongFocus);
    autoNFeaturesSpinBox->setToolTip(tr("Number of highest-variance features passed to KlustaKwik"));
    connect(autoNFeaturesSpinBox, &SpinBox::valueChanged, autoNFeaturesSpinBox, &SpinBox::deselect, Qt::QueuedConnection);
    connect(autoNFeaturesSpinBox, &SpinBox::valueChanged, this, &KlustersApp::slotUpdateAutoNFeatures);
    autoNFeaturesLabelAction   = paramBar->addWidget(autoNFeaturesLabel);
    autoNFeaturesSpinBoxAction = paramBar->addWidget(autoNFeaturesSpinBox);
    autoNFeaturesLabelAction->setVisible(false);
    autoNFeaturesSpinBoxAction->setVisible(false);

    // "time" checkbox next to the N-feat spin box: include the spike timestamp as a
    // clustering feature when auto-selecting features for reclustering.  Off by
    // default (time over-fits within-session drift); shares the spin box's
    // visibility.  Created hidden; syncAutoFeatureToolbar() decides whether it is
    // shown and syncs its checked state from the configuration.  (This used to say
    // initializePreferences() did that.  It did not, which is why the checkbox never
    // appeared until the preferences dialog reported a change.)
    autoNFeaturesTimeCheckBox = new QCheckBox(tr("time"), paramBar);
    autoNFeaturesTimeCheckBox->setObjectName("autoNFeaturesTimeCheckBox");
    autoNFeaturesTimeCheckBox->setFont(font);
    autoNFeaturesTimeCheckBox->setToolTip(tr(
        "Include the spike timestamp as a clustering feature when auto-selecting\n"
        "features for reclustering.  Off by default: including time makes the\n"
        "reclusterer over-fit to within-session drift (spikes separated by when\n"
        "they fired rather than by waveform shape)."));
    connect(autoNFeaturesTimeCheckBox, &QCheckBox::toggled,
            this, &KlustersApp::slotUpdateIncludeTimeFeature);
    autoNFeaturesTimeCheckBoxAction = paramBar->addWidget(autoNFeaturesTimeCheckBox);
    autoNFeaturesTimeCheckBoxAction->setVisible(false);

    // Realign top-channels spinbox — 0 means "use all channels".
    // When > 0, only the N highest-amplitude channels (by template peak)
    // contribute to alignment, excluding collision/noise on other channels.
    realignTopChanLabel = new QLabel(tr("  Realign top-ch:"), paramBar);
    realignTopChanLabel->setFrameStyle(QFrame::StyledPanel|QFrame::Plain);
    realignTopChanLabel->setFont(font);
    realignTopChanSpinBox = new SpinBox(paramBar);
    realignTopChanSpinBox->setObjectName("realignTopChanSpinBox");
    realignTopChanSpinBox->setMinimum(0);
    realignTopChanSpinBox->setMaximum(64);
    realignTopChanSpinBox->setSingleStep(1);
    realignTopChanSpinBox->setWrapping(false);
    realignTopChanSpinBox->setFocusPolicy(Qt::StrongFocus);
    realignTopChanSpinBox->setToolTip(
        tr("Number of highest-amplitude channels used by spike realignment.\n"
           "0 = use all channels (legacy behaviour).\n"
           "Typical values: 3 for octrode, 2 for tetrode.\n"
           "Limits influence of collisions/noise on non-primary channels."));
    connect(realignTopChanSpinBox, &SpinBox::valueChanged,
            realignTopChanSpinBox, &SpinBox::deselect, Qt::QueuedConnection);
    connect(realignTopChanSpinBox, &SpinBox::valueChanged,
            this, &KlustersApp::slotUpdateRealignTopChan);
    realignTopChanLabelAction   = paramBar->addWidget(realignTopChanLabel);
    realignTopChanSpinBoxAction = paramBar->addWidget(realignTopChanSpinBox);

    //Connect the move function of the parameterBar to slotUpdateParameterBar to always correctly show its contents.
    connect(paramBar, &QToolBar::allowedAreasChanged, this, &KlustersApp::slotUpdateParameterBar);
    connect(paramBar, &QToolBar::orientationChanged, this, &KlustersApp::slotUpdateParameterBar);

    // ── Timestamp nudge buttons (lower-right of toolbar) ─────────────────
    // Shift the selected cluster's spike timestamps by ±1 sample.
    // Useful for correcting sub-sample jitter after extraction.
    paramBar->addSeparator();

    nudgeMinusAction = new QAction(tr("-1 smpl"), this);
    nudgeMinusAction->setToolTip(
        tr("Shift timestamps of selected cluster −1 sample"));
    nudgeMinusAction->setEnabled(false);
    connect(nudgeMinusAction, &QAction::triggered,
            this, &KlustersApp::slotNudgeTimestampMinus);

    nudgePlusAction = new QAction(tr("+1 smpl"), this);
    nudgePlusAction->setToolTip(
        tr("Shift timestamps of selected cluster +1 sample"));
    nudgePlusAction->setEnabled(false);
    connect(nudgePlusAction, &QAction::triggered,
            this, &KlustersApp::slotNudgeTimestampPlus);

    // (Curation quality annotation shortcuts J/K/X were removed in favour
    //  of automatic status inference.  The curation log now marks any
    //  undone action as "bad" and any retained action as "good" without
    //  user intervention — see CurationLogger header doc-comment.)

}

void KlustersApp::executePreferencesDlg(){
    if(prefDialog == nullptr){
        if(mainDock)
            prefDialog = new PrefDialog(this,doc->nbOfchannels());  // create dialog on demand
        else
            prefDialog = new PrefDialog(this);
        // connect to the "settingsChanged" signal
        connect(prefDialog,&PrefDialog::settingsChanged,this,&KlustersApp::applyPreferences);
    }
    else{
        //If the dialog has been open the first time before any document has been open
        //the number of channel is zero. If now a document is open, update the number of channels.
        if(configuration().getNbChannels() == 0 && mainDock){
            prefDialog->resetChannelList(doc->nbOfchannels());
        }
    }

    // update the dialog widgets.
    prefDialog->updateDialog();

    if(prefDialog->exec() == QDialog::Accepted){  // execute the dialog
        //if the user did not click the applyButton, save the new settings.
        if(prefDialog->isApplyEnable()){
            prefDialog->updateConfiguration();        // store settings in config object
            applyPreferences();                      // let settings take effect
        }
    }
}

void KlustersApp::applyPreferences() {  
    configuration().write();
    int newNbUndo = configuration().getNbUndo();
    if(nbUndo != newNbUndo){
        if(mainDock)
            doc->nbUndoChangedCleaning(newNbUndo);
        nbUndo = newNbUndo;
    }

    if(backgroundColor != configuration().getBackgroundColor()){
        backgroundColor = configuration().getBackgroundColor();
        if(mainDock)
            doc->setBackgroundColor(backgroundColor);
        clusterPalette->changeBackgroundColor(backgroundColor);
    }

    // Show/hide the recommend dock live, so the toggle takes effect on Apply
    // rather than at the next restart.  Only meaningful while a hierarchical
    // session is up -- that is the only place it is shown at all -- and showing
    // it triggers a refresh through the normal selection path.
    if(recommendPanel && mHierarchicalView && mHierarchicalView->isChecked()){
        const bool want = configuration().getShowMergeRecommendPanel();
        if(want != recommendPanel->isVisible()){
            recommendPanel->setVisible(want);
            if(want) slotRefreshMergeRecommendations();
        }
    }

    if(waveformsGain != configuration().getGain()){
        waveformsGain = configuration().getGain();
        if(mainDock)doc->setGain(waveformsGain);
    }

    if(displayTimeInterval != configuration().getTimeInterval()){
        displayTimeInterval = configuration().getTimeInterval();
        if(mainDock)
            doc->setTimeStepInSecond(displayTimeInterval);
    }

    if(configuration().isCrashRecovery()){
        if(mainDock)
            doc->updateAutoSavingInterval(configuration().crashRecoveryInterval());
        else
            doc->setAutoSaving(configuration().crashRecoveryInterval());
    }
    else
        doc->stopAutoSaving();

    if(configuration().getNbChannels() != 0 && channelPositions != (*configuration().getChannelPositions())){
        QList<int>* positions = configuration().getChannelPositions();
        channelPositions.clear();
        for(int i = 0; i < static_cast<int>(positions->size()); ++i)
            channelPositions.append((*positions)[i]);
        if(mainDock)
            doc->setChannelPositions(channelPositions);
    }

    if(reclusteringExecutable != configuration().getReclusteringExecutable())
        reclusteringExecutable = configuration().getReclusteringExecutable();

    if(reclusteringArgs != configuration().getReclusteringArguments())
        reclusteringArgs = configuration().getReclusteringArguments();

    if(realignExecutable != configuration().getRealignExecutable())
        realignExecutable = configuration().getRealignExecutable();

    // Rebuild realignArgs from structured prefs + gated post-alignment mode.
    {
        const QString newArgs = buildRealignArgs();
        if (realignArgs != newArgs) realignArgs = newArgs;
    }

    if(markerSize != configuration().getMarkerSize()){
        markerSize = configuration().getMarkerSize();
        if(mainDock) doc->setMarkerSize(markerSize);
    }

    if(selectionLineWidth != configuration().getSelectionLineWidth()){
        selectionLineWidth = configuration().getSelectionLineWidth();
        if(mainDock) doc->setSelectionLineWidth(selectionLineWidth);
    }

    useWhiteColorDuringPrinting = configuration().getUseWhiteColorDuringPrinting();
    autoSelectFeatures  = configuration().getAutoSelectFeatures();
    autoSelectNFeatures = configuration().getAutoSelectNFeatures();
    syncAutoFeatureToolbar();
}

void KlustersApp::syncAutoFeatureToolbar(){
    if(!autoNFeaturesSpinBoxAction) return;      // toolbar not built yet

    // Show the N-feat spinbox whenever autoSelectFeatures is on and a doc is open.
    // It is used at recluster time regardless of which sub-view is currently active,
    // so it must not be gated on the scatter-plot X/Y selectors being visible.
    autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
    autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);

    // Programmatic sync throughout: block the widget's signal rather than gating on
    // isInit.  The old `if(!isInit) setValue(...)` skipped the spin box during
    // startup -- which was invisible while the toolbar itself never appeared at
    // startup, but the moment it does appear the box would read 1 (its minimum)
    // instead of the configured value.  Blocking the signal gets the value in
    // without the valueChanged -> slotUpdateAutoNFeatures round trip that the isInit
    // guard existed to prevent.
    if(autoNFeaturesSpinBox->value() != autoSelectNFeatures){
        const bool prev = autoNFeaturesSpinBox->blockSignals(true);
        autoNFeaturesSpinBox->setValue(autoSelectNFeatures);
        autoNFeaturesSpinBox->blockSignals(prev);
    }

    if(autoNFeaturesTimeCheckBoxAction){
        autoNFeaturesTimeCheckBoxAction->setVisible(autoSelectFeatures);
        const bool inclTime = configuration().getIncludeTimeInAutoSelect();
        if(autoNFeaturesTimeCheckBox->isChecked() != inclTime){
            // Block the toggled() signal so it doesn't re-write the (unchanged)
            // setting on every preferences refresh.
            const bool prev = autoNFeaturesTimeCheckBox->blockSignals(true);
            autoNFeaturesTimeCheckBox->setChecked(inclTime);
            autoNFeaturesTimeCheckBox->blockSignals(prev);
        }
    }
}

void KlustersApp::initializePreferences(){
    nbUndo = configuration().getNbUndo();
    waveformsGain = configuration().getGain();
    displayTimeInterval = configuration().getTimeInterval();
    backgroundColor =  configuration().getBackgroundColor();
    reclusteringExecutable =  configuration().getReclusteringExecutable();
    reclusteringArgs = configuration().getReclusteringArguments();
    realignExecutable = configuration().getRealignExecutable();
    realignArgs = buildRealignArgs();
    markerSize = configuration().getMarkerSize();
    selectionLineWidth = configuration().getSelectionLineWidth();
    useWhiteColorDuringPrinting = configuration().getUseWhiteColorDuringPrinting();
    autoSelectFeatures  = configuration().getAutoSelectFeatures();
    autoSelectNFeatures = configuration().getAutoSelectNFeatures();
    clusterPalette->changeBackgroundColor(backgroundColor);
}

void KlustersApp::initStatusBar()
{
    ///////////////////////////////////////////////////////////////////
    // STATUSBAR
    statusBar()->showMessage(tr("Ready."),1);
}


bool KlustersApp::eventFilter(QObject* object,QEvent* event){
    if(object == paramBar && event->type() == 71){//filter the removal of items from the paramBar
        return true;
    }

    // Keep the childPalette alias pointing at the child palette when it gains
    // focus -- by keyboard (Tab) or mouse.
    if(event->type() == QEvent::FocusIn){
        for(QObject* w = object; w; w = w->parent()){
            if(w == childPaletteA){ childPalette = childPaletteA; break; }
        }
    }

    // ── Watershed live-preview mode ─────────────────────────────────────────
    // When wsPreviewActive is true, four arrow keys + Enter + Esc are claimed
    // exclusively; any other key is swallowed silently to prevent the
    // user from triggering an unrelated action mid-tune.  This block runs
    // before everything else so even ShortcutOverride'd keys can't
    // sneak past.
    if (wsPreviewActive &&
        (event->type() == QEvent::ShortcutOverride ||
         event->type() == QEvent::KeyPress))
    {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        const int key  = ke->key();
        const auto mod = ke->modifiers();
        const bool shift = (mod & Qt::ShiftModifier) != 0;
        // Strip Shift from the modifier set for the equality test —
        // arrows with or without Shift are both wanted.
        const bool plainOrShifted = (mod == Qt::NoModifier) ||
                                    (mod == Qt::ShiftModifier);

        // Always claim the override so QActions don't fire.
        if (event->type() == QEvent::ShortcutOverride) {
            ke->accept();
            return true;
        }

        if (ke->isAutoRepeat()) {
            // Suppress autorepeat — the kernel costs ~50 ms per run, so
            // queued autorepeats would feel sluggish and fire long after
            // the key was released.
            return true;
        }

        if (plainOrShifted) {
            const int step = shift ? 5 : 1;
            switch (key) {
            case Qt::Key_Left:
                wsSigmaCells = qBound(1, wsSigmaCells - step, 32);
                wsRecompute();
                return true;
            case Qt::Key_Right:
                wsSigmaCells = qBound(1, wsSigmaCells + step, 32);
                wsRecompute();
                return true;
            case Qt::Key_Up:
                wsThreshPct = qBound(0, wsThreshPct + step, 50);
                wsRecompute();
                return true;
            case Qt::Key_Down:
                wsThreshPct = qBound(0, wsThreshPct - step, 50);
                wsRecompute();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                wsExit(/*commit=*/true);
                return true;
            case Qt::Key_Escape:
                wsExit(/*commit=*/false);
                return true;
            }
        }
        // Any other key while preview is active: swallow.
        return true;
    }

    // ── Key navigation ──────────────────────────────────────────────────────
    if(event->type() == QEvent::KeyPress){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        const bool ctrlHeld = ke->modifiers() & Qt::ControlModifier;

        // "H" — keyboard shortcut help dialog
        if(ke->key() == Qt::Key_H && ke->modifiers() == Qt::NoModifier){
            slotShowShortcutHelp();
            return true;
        }

        // Esc from the child palette returns focus to the parent palette.
        // (When the temporary child error/template matrices exist, Esc will
        // also dismiss them first -- added with that feature.)
        if(ke->key() == Qt::Key_Escape && ke->modifiers() == Qt::NoModifier
           && focusedChildPalette() != nullptr){
            childPalette = childPaletteA;
            if(clusterPalette) clusterPalette->setFocusToList();
            return true;
        }

        // Hierarchy operations (Ctrl+arrows, G) while the dual child view is up.
        // Runs before the Left/Right tab-cycle handler so Ctrl+Left/Right is
        // claimed for custody transfer when a child pane has focus; otherwise it
        // returns false and the tab handler keeps Ctrl+Left/Right.
        if(childPanel && childPanel->isVisible() && !editConsolidationLock
           && dispatchHierarchyKey(ke->key(), ke->modifiers()))
            return true;
        // Ctrl+1 — new cluster mode
        // Ctrl, not a bare digit: this filter is installed on the application, so
        // a bare "1"/"2" was swallowed here before the focused widget saw it —
        // which made those two digits untypeable in the parameter bar's Bin size
        // and Duration boxes.
        if(ke->key() == Qt::Key_1 && ctrlHeld
           && !isInit && doc && activeView() && !editConsolidationLock){
            slotSingleNew();
            return true;
        }
        // Ctrl+2 — split clusters mode
        if(ke->key() == Qt::Key_2 && ctrlHeld
           && !isInit && doc && activeView() && !editConsolidationLock){
            slotMultipleNew();
            return true;
        }

        // "E" — step through the open matrix tabs (Error → Template →
        // Residual → Drift → Error)
        if(ke->key() == Qt::Key_E && ke->modifiers() == Qt::NoModifier
           && !isInit && doc && activeView()){
            activeView()->toggleMatrixTab();
            return true;
        }

        // ── Tab / Shift+Tab — move focus between windows & fields ───────────
        // Tab advances (Shift+Tab reverses) across the focus-zone ring: cluster
        // palette, the child palette (while the hierarchical view is shown), and
        // the toolbar spinboxes / line-edits.  This is the default focus mover.
        // Guarded so it never hijacks Tab inside a modal dialog or any widget
        // outside the main window, where Tab must keep ordinary field-to-field
        // traversal.  Placed before the Left/Right tab-display handler.
        if((ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab)
           && !(ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier
                                   | Qt::MetaModifier))
           && !isInit && doc && activeView()
           && !QApplication::activeModalWidget()){
            QWidget* fw = QApplication::focusWidget();
            if(fw && fw->window() == this){
                cycleHierarchyFocus(ke->key() == Qt::Key_Tab);
                return true;
            }
        }
        // Ctrl+Shift+Left/Right cycles the same focus ring (kept as an
        // alternative; Ctrl+Left/Right is the hierarchy custody transfer, so
        // plain Tab is now the primary focus mover).  Placed before the
        // Left/Right tab-display handler so it is not swallowed as a tab switch.
        if((ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right)
           && (ke->modifiers() & Qt::ControlModifier)
           && (ke->modifiers() & Qt::ShiftModifier)){
            cycleHierarchyFocus(ke->key() == Qt::Key_Right);
            return true;
        }

        // ── Left / Right (plain or Ctrl) — cycle display tabs ───────────────
        // Plain Left/Right switches tabs ONLY when focus is on the tab bar
        // itself (i.e. the user tabbed there or just clicked a tab handle) —
        // never when focus is inside a tab page such as ClusterView,
        // WaveformView, etc.  Otherwise arrow keys used for cluster
        // navigation inside those views would steal the event and switch
        // tabs instead.
        //
        // Ctrl+Left/Right works from anywhere: from outside the tab area,
        // first jumps to Overview; from inside the tab bar, cycles tabs.
        if((ke->key() == Qt::Key_Left || ke->key() == Qt::Key_Right) &&
           tabsParent && tabsParent->isVisible() && tabsParent->count() > 0){

            QWidget* focused = QApplication::focusWidget();
            bool focusOnTabBar = false;
            if(focused){
                // The QTabBar is a child of QTabWidget; walk up only until
                // we hit the QTabBar (NOT any ancestor — in particular not
                // the QTabWidget as a whole, which would include tab pages).
                QObject* w = focused;
                while(w){
                    if(qobject_cast<QTabBar*>(w)){
                        focusOnTabBar = true;
                        break;
                    }
                    w = w->parent();
                }
            }

            if(ctrlHeld && !focusOnTabBar){
                // Ctrl+arrow from outside the tab bar: jump to Overview.
                int overviewIdx = 0;
                for(int i = 0; i < tabsParent->count(); ++i){
                    if(tabsParent->tabText(i).contains(tr("Overview"),
                                                        Qt::CaseInsensitive)){
                        overviewIdx = i; break;
                    }
                }
                tabsParent->setCurrentIndex(overviewIdx);
                focusTabPage(tabsParent->widget(overviewIdx));
                return true;
            }

            if(focusOnTabBar){
                // Focus is on the tab bar: Left/Right cycles tabs (with or
                // without Ctrl).
                const int n    = tabsParent->count();
                const int cur  = tabsParent->currentIndex();
                const int next = ke->key() == Qt::Key_Right
                    ? (cur + 1) % n
                    : (cur - 1 + n) % n;
                tabsParent->setCurrentIndex(next);
                focusTabPage(tabsParent->widget(next));
                return true;
            }

            // Focus is inside a tab page — do NOT consume; let the page
            // (ClusterView, WaveformView, etc.) handle the arrow normally
            // for cluster navigation, polygon nudge, etc.
        }
    }
    // ── S key: palette cluster toggle ──────────────────────────────────────
    // Qt::Key_S — palette cluster toggle.  When the cluster palette has
    // focus, S toggles the current selection instead of being routed to
    // QListWidget's default handler (which would do nothing useful for S).
    // Intercepting at ShortcutOverride first ensures no future global
    // QAction with shortcut S could swallow the key before the palette
    // sees it.
    if(event->type() == QEvent::ShortcutOverride){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_S && ke->modifiers() == Qt::NoModifier
           && paletteHasFocus()){
            ke->accept(); // claim the shortcut so the QAction doesn't fire
            return true;
        }
        // G is the flat group-clusters action globally, but the adaptive merge in
        // the dual child view when a palette has focus; claim it there so the
        // group-clusters QAction doesn't fire.  (M used to be claimed here for the
        // merge; it is now left to the mean-presentation toggle.)
        if(ke->key() == Qt::Key_G && ke->modifiers() == Qt::NoModifier
           && childPanel && childPanel->isVisible() && paletteHasFocus()){
            ke->accept();
            return true;
        }
    }
    if(event->type() == QEvent::KeyPress){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_S && ke->modifiers() == Qt::NoModifier
           && paletteHasFocus()){
            // s marks the focused palette's current item: parents in the main
            // palette, children when the child palette holds focus.
            ClusterPalette* target = focusedChildPalette();
            if(!target) target = clusterPalette;
            target->toggleCurrentSelection();
            return true;
        }
    }
    // ── T key: palette move-to-end ─────────────────────────────────────────
    // T has no global QAction shortcut (the "Time Frame" QAction shortcut
    // was removed).  Trigger the renumber-to-end whenever the user has a
    // palette selection, regardless of which view currently holds focus
    // (cluster view, correlation matrix, error matrix, template matrix —
    // any of these are normal palette companions).  Only suppress when
    // focus is in a text-entry control (spinbox / line-edit) so the user
    // can still type letters.  Earlier the gate required strict
    // paletteHasFocus(), which silently dropped T after a click on the
    // correlation matrix — counter-intuitive, since the palette's
    // selection ring stays visible across that focus change.
    if(event->type() == QEvent::ShortcutOverride){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_T && ke->modifiers() == Qt::NoModifier
           && !focusIsInTextInput()){
            ke->accept();
            return true;
        }
    }
    if(event->type() == QEvent::KeyPress){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_T && ke->modifiers() == Qt::NoModifier
           && doc && !focusIsInTextInput()){
            slotMoveSelectedClustersToEnd();
            return true;
        }
    }
    // ── PageUp / PageDown — timestamp nudge (±1 sample) ──────────────────
    // Intercept at both ShortcutOverride and KeyPress so the cluster palette
    // QListWidget never receives these keys for its own scroll navigation.
    // Autorepeat is suppressed: for large clusters the synchronous nudge loop
    // (fread × N + PCA projection) can take long enough that multiple autorepeat
    // events queue up and fire immediately on return, causing a ×2 (or more)
    // movement per apparent single press.
    if(event->type() == QEvent::ShortcutOverride ||
       event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if((ke->key() == Qt::Key_PageUp || ke->key() == Qt::Key_PageDown)
           && ke->modifiers() == Qt::NoModifier
           && !isInit && doc) {
            // Always claim ShortcutOverride so QListWidget never scrolls.
            if(event->type() == QEvent::ShortcutOverride) {
                ke->accept();
                return true;
            }
            // Suppress autorepeat and any event arriving within
            // 300 ms of the last nudge completing.  Without the
            // elapsed-time guard, queued autorepeats that accumulated
            // during a long (multi-second) nudge loop fire one-by-one
            // once the event queue drains — the singleShot(0) only
            // blocks the FIRST one.
            if(ke->isAutoRepeat())
                return true;
            if(nudgeInProgress)
                return true;
            if(lastNudgeTimer.isValid() && lastNudgeTimer.elapsed() < 300)
                return true;
            if(ke->key() == Qt::Key_PageUp)
                slotNudgeTimestampPlus();
            else
                slotNudgeTimestampMinus();
            return true;
        }
    }

    return QWidget::eventFilter(object,event);    // standard event processing
}

void KlustersApp::buildFocusZones()
{
    // Tab / Ctrl+Shift+Left/Right focus ring:
    //   1. Cluster (parent) palette
    //   2. Child palette (when the hierarchical view is open)
    //   3. Toolbar spinboxes / line-edits (left-to-right order)
    // The tab-display area is excluded so focus never lands inside the
    // waveform/scatter/correlation views.
    focusZones.clear();

    // 1. Cluster list
    if(clusterPanel && clusterPanel->isVisible() && clusterPalette)
        focusZones.append(clusterPalette);

    // 1b. Child palette, only while the hierarchical view is open.
    if(childPanel && childPanel->isVisible() && childPaletteA)
        focusZones.append(childPaletteA);

    // 2. Toolbar fields only
    if(paramBar){
        const QList<QAction*> actions = paramBar->actions();
        for(QAction* a : actions){
            QWidget* w = paramBar->widgetForAction(a);
            if(!w) continue;
            if(!(qobject_cast<QAbstractSpinBox*>(w) || qobject_cast<QLineEdit*>(w)))
                continue;
            if(a->isVisible() && w->isVisible() && w->isEnabled())
                focusZones.append(w);
        }
    }
}

void KlustersApp::focusTabPage(QWidget* page)
{
    // Give keyboard focus to the most appropriate widget inside a tab page.
    // Tries the page's own focusProxy first, then walks Qt's focus chain to
    // find the first child that actually accepts keys.
    if(!page) return;
    QWidget* focusable = page->focusProxy();
    if(!focusable){
        QWidget* candidate = page->nextInFocusChain();
        for(int tries = 0; candidate && tries < 50; ++tries){
            QObject* p = candidate->parent();
            bool inside = false;
            while(p){ if(p == page){ inside = true; break; } p = p->parent(); }
            if(inside && candidate->focusPolicy() != Qt::NoFocus){
                focusable = candidate; break;
            }
            candidate = candidate->nextInFocusChain();
            if(candidate == page->nextInFocusChain()) break;
        }
    }
    if(focusable)
        focusable->setFocus(Qt::OtherFocusReason);
    else
        page->setFocus(Qt::OtherFocusReason);
}

bool KlustersApp::paletteHasFocus() const
{
    // Walk up from the currently-focused widget; clusterPalette is a
    // QWidget container, and its inner iconView (a QListWidget) is what
    // actually receives the keypresses.  Any ancestor of the focus widget
    // matching clusterPalette means the palette tree owns focus.
    if (!clusterPalette) return false;
    for (QWidget* w = QApplication::focusWidget(); w; w = w->parentWidget())
        if (w == clusterPalette || w == childPaletteA) return true;
    return false;
}

bool KlustersApp::focusIsInTextInput() const
{
    // Returns true when the currently-focused widget is a text-entry
    // control (QAbstractSpinBox covers QSpinBox and QDoubleSpinBox; the
    // separate QLineEdit check covers the duration / bin-size / etc. line
    // edits in the parameter toolbar).  Used by the T-key intercept (and
    // potentially other future single-letter palette shortcuts) so the
    // user can still type letters into spinboxes / line-edits without
    // triggering palette actions.  We walk up the ancestor chain because
    // spinboxes embed an internal QLineEdit that is the actual focus
    // widget; testing only the leaf would miss it for QAbstractSpinBox-
    // derived controls.
    for (QWidget* w = QApplication::focusWidget(); w; w = w->parentWidget()) {
        if (qobject_cast<QAbstractSpinBox*>(w)) return true;
        if (qobject_cast<QLineEdit*>(w))       return true;
    }
    return false;
}


void KlustersApp::initClusterPanel()
{
    //Creation of the left panel containing the clusters
    clusterPanel = new QDockWidget(tr("The cluster list"),nullptr);
    clusterPanel->setWindowIcon(QIcon("classnew"));
    //Initialisation of the cluster palette containing the cluster list
    clusterPalette = new ClusterPalette(backgroundColor,clusterPanel,statusBar(),"ClusterPalette");
    //Place the clusterPalette frame in the clusterPanel (the view)
    clusterPanel->setWidget(clusterPalette);
    clusterPanel->setFeatures(QDockWidget::NoDockWidgetFeatures);
    clusterPanel->hide();

    // Hierarchical view: a second palette listing the children (.clc microfibers)
    // of the unit(s) selected in the main palette.  Hidden until the View-menu
    // toggle enables it; stacked directly below the main palette in initView().
    childPanel = new QDockWidget(tr("Child clusters (.clc)"),nullptr);
    // Single child palette listing the children (.clc microfibers) of the parent
    // selected in the main palette; scope-bound to that parent's children.
    childPaletteA = new ClusterPalette(backgroundColor,childPanel,statusBar(),"ChildClusterPaletteA");
    childPaletteA->setShowsChildScope(true);
    childPalette = childPaletteA;          // alias: the child palette
    childPanel->setWidget(childPaletteA);
    childPanel->setFeatures(QDockWidget::NoDockWidgetFeatures);
    childPanel->hide();

    // Third section of the palette stack: recommended PARENT merges, ranked by
    // agreement between the error and residual matrices.  A reader only -- it
    // selects a pair in the main palette and leaves the merge to the existing
    // hierarchy ops, so undo and colour handling stay in one place.
    recommendPanel = new QDockWidget(tr("Recommended merges"),nullptr);
    recommendView  = new MergeRecommendView(recommendPanel);
    recommendPanel->setWidget(recommendView);
    recommendPanel->setFeatures(QDockWidget::NoDockWidgetFeatures);
    recommendPanel->hide();
    connect(recommendView,&MergeRecommendView::recommendationActivated,
            this,&KlustersApp::slotRecommendationActivated);
}

void KlustersApp::initDisplay(){
    isInit = true; //prevent the spine boxes or the lineedit and the editline to trigger during initialisation
    timeFrameMode->setChecked(false);
    overlayPresentation->setChecked(false);
    meanPresentation->setChecked(false);
    scaleByMax->setChecked(true);
    dimensionXAction->setVisible(true);
    dimensionYAction->setVisible(true);
    featureXLabelAction->setVisible(true);
    autoNFeaturesSpinBox->setValue(autoSelectNFeatures);
    autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
    autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);
    spikesTodisplayAction->setVisible(true);
    spikesTodisplayLabelAction->setVisible(true);
    correlogramsHalfDuration->setText(INITIAL_CORRELOGRAMS_HALF_TIME_FRAME);
    correlogramsHalfDurationAction->setVisible(true);
    correlogramsHalfDurationLabelAction->setVisible(true);
    binSizeBox->setText(DEFAULT_BIN_SIZE);
    binSizeBoxAction->setVisible(true);
    binSizeLabelAction->setVisible(true);
    shoulderLine->setChecked(true);

    //Set the range value of the spine boxes
    dimensionX->setRange(1,doc->nbDimensions());
    dimensionY->setRange(1,doc->nbDimensions());
    maximumTime = doc->maxTime();
    start->setRange(0,maximumTime);
    validator.setRange(0,maximumTime);
    long totalNbSpikes = doc->totalNbOfSpikes();
    spikesTodisplay->setRange(1,totalNbSpikes);
    maximumTime *= 1000;
    // Lower bound 1, not 0: both boxes feed divisions (nbBins = timeWindow /
    // binSize), and the comments where these boxes are built already promise
    // "integers between 1 and a max".  Accepting 0 made typing one reachable and
    // crashed the correlation view with SIGFPE.
    correlogramsHalfTimeFrameValidator.setRange(1,static_cast<int>((maximumTime - 1) / 2));
    binSizeValidator.setRange(1,static_cast<int>(maximumTime));


    //If the setting dialog exists (has already be open once), enable the settings for the channels.
    if(prefDialog != nullptr)
        prefDialog->enableChannelSettings(true);

    // Pre-select the last real cluster (skip noise=0 and artefact=1)
    // so the waveform view shows something useful on startup.
    QList<int>* clusterList = new QList<int>();
    {
        const QList<dataType> ids = doc->data().clusterIds();
        // ids is sorted ascending (QMap keys); find last id > 1
        for (int i = static_cast<int>(ids.size()) - 1; i >= 0; --i) {
            if (ids[i] > 1) { clusterList->append(static_cast<int>(ids[i])); break; }
        }
    }
    
    //Update the dimension and start spine boxes
    dimensionX->setValue(1);
    dimensionY->setValue(2);

    isInit = false; //now a change in a spine box or the lineedit will trigger an update of the display

    //If 2 documents, opened one after the other, do not have the same number of channels
    //discard any settings concerning the positions of the channels.
    if(configuration().getNbChannels() != 0 && configuration().getNbChannels() != doc->nbOfchannels())
        channelPositions.clear();

    KlustersView* view = new KlustersView(*this,*doc,backgroundColor,1,2,clusterList,KlustersView::OVERVIEW,mainDock,nullptr,statusBar(),
                                          displayTimeInterval,waveformsGain,channelPositions,false,0,timeWindow,DEFAULT_NB_SPIKES_DISPLAYED,
                                          false,false,DEFAULT_BIN_SIZE.toInt(),INITIAL_CORRELOGRAMS_HALF_TIME_FRAME.toInt() * 2 + 1,Data::MAX);

    mainDock = view;
    tabsParent->addDockArea(view,tr("Overview Display"));
    tabsParent->show();

    view->installEventFilter(this);

    //Keep track of the number of displays
    displayCount ++;

    //Update the document's list of view
    doc->addView(view);

    // Apply current display preferences to the new view's ClusterViews
    doc->setMarkerSize(markerSize);
    doc->setSelectionLineWidth(selectionLineWidth);
    //Create the cluster list and select the clusters which will be drawn
    clusterPalette->createClusterList(doc);
    clusterPalette->selectItems(*clusterList);

    // Hierarchical view: enable the toggle when the opened document has a .clc
    // child sibling (auto-detected in KlustersDoc::openDocument), and turn it on
    // by default so the child palette is immediately available.
    // Hierarchical vs flat is committed here, once, from the file set: a complete
    // .clu + .clc + .clp triple opens a hierarchical session, anything else is a
    // flat .clu session.  The menu entry is a disabled indicator of that choice,
    // not a runtime toggle, so the two systems never mix within a session.
    if(childPanel) childPanel->hide();
    if(recommendPanel) recommendPanel->hide();
    if(childPaletteA) childPaletteA->reset();
    if(mHierarchicalView){
        const bool hier = doc->isHierarchicalSession();
        {
            const QSignalBlocker block(mHierarchicalView);
            mHierarchicalView->setChecked(hier);
            mHierarchicalView->setEnabled(false);   // mode is fixed for the session
        }
        if(hier)
            slotHierarchicalViewToggled(true);   // commit hierarchical: load + show the child layer
        else
            doc->setActiveClustering(false);     // flat: parent clustering active (child panel already hidden above)
    }

    // Once the view is shown and the WaveformThread has loaded the first
    // cluster, auto-scale the waveform amplitude. A 350 ms delay is enough
    // for the thread to complete on a warm cache; on a cold cache the
    // thread will still be running and autoFitAmplitude() will be a no-op
    // (no spikes available yet) — acceptable, user can press U to refresh.
    if (!clusterList->isEmpty()) {
        KlustersView* v = view; // capture for lambda
        QPointer<KlustersView> vp = v;
        QTimer::singleShot(350, this, [vp]{
            if (vp) vp->autoFitWaveformsAmplitude();
        });
    }

    //forbit docking abilities of clusterPanel itself
    clusterPanel->setAllowedAreas(Qt::NoDockWidgetArea);

    clusterPalette->show();
    clusterPanel->show();
    //Update the Time frame and sample related widgets
    spikesTodisplay->setValue(DEFAULT_NB_SPIKES_DISPLAYED);
    start->setValue(0);
    start->setSingleStep(timeWindow);
    duration->setText(INITIAL_WAVEFORM_TIME_WINDOW);
    if(timeFrameMode->isChecked()){
        durationAction->setVisible(true);
        durationLabelAction->setVisible(true);
        startAction->setVisible(true);
        startLabelAction->setVisible(true);
        spikesTodisplayAction->setVisible(false);
        spikesTodisplayLabelAction->setVisible(false);
    }
    else{
        durationAction->setVisible(false);
        durationLabelAction->setVisible(false);
        startAction->setVisible(false);
        startLabelAction->setVisible(false);
        spikesTodisplayAction->setVisible(true);
        spikesTodisplayLabelAction->setVisible(true);
    }

    //Enable some actions now that a document is open (see the klustersui.rc file)
    slotStateChanged("documentState");

    // Auto-show error & template matrices on document open.
    //
    // The user-facing intent: when this preference is on, opening a
    // document should produce the same workflow-ready layout every
    // time — a single Overview Display tab containing Cluster Features /
    // Waveforms / Auto-correlogram stacked vertically on the left and
    // the Error Matrix / Template Matrix tabified together on the right.
    // Saves a manual sequence of:
    //   Display → New Grouping Assistant Display
    //   (within GA) add Error Matrix dock
    //   (within GA) add Template Matrix dock
    //   Actions → Update Error Matrix
    // on every document open.
    //
    // The matrices are added directly to the Overview Display rather
    // than to a separate Grouping Assistant Display tab — the latter
    // tab is the historical home of the matrix views but is no longer
    // needed when the matrices live in the Overview itself.  The user
    // can still open one manually via Display → New Grouping Assistant
    // Display if they want a second view of the same data.
    //
    // Defer to the next event-loop iteration so the Overview window's
    // threads (WaveformThread, CorrelationThread) have time to finish
    // their initial loads before the matrix computations start queuing
    // additional CPU work.  QTimer::singleShot(0, ...) processes pending
    // events before firing.
    if (configuration().getAutoShowMatricesOnOpen()) {
        QTimer::singleShot(0, this, [this]() {
// document was closed mid-defer
            // The Overview Display is the active view at this point —
            // widgetAddToDisplay routes through view->addView() which
            // adds the matrix docks to it.  Added in the same canonical
            // order KlustersView::matrixDocks() reports, so ERROR_MATRIX
            // ends up the front tab and "E" steps through them in that
            // order; applyOverviewLayout tabifies the rest onto it.
            widgetAddToDisplay(KlustersView::ERROR_MATRIX);
            widgetAddToDisplay(KlustersView::TEMPLATE_MATRIX);
            widgetAddToDisplay(KlustersView::RESIDUAL_MATRIX);
            widgetAddToDisplay(KlustersView::DRIFT_MATRIX);
            // Now arrange the dock layout — splits the left column
            // vertically and positions the matrices on the right.
            if (KlustersView* view = activeView()) {
                view->applyOverviewLayout();
            }
            // Populate every matrix (slotUpdateErrorMatrix emits the
            // compute signal for all four; Qt drops the ones not open).
            slotUpdateErrorMatrix();
        });
    }
}

void KlustersApp::createDisplay(KlustersView::DisplayType type)
{
    if(mainDock){
        QString displayName = (doc->documentName()).append(QString::number(static_cast<int>(type)));
        QString displayType = KlustersView::DisplayTypeNames[type];

        //Check if the active display contains a ProcessWidget
        bool isProcessWidget = doesActiveDisplayContainProcessWidget();

        //Present the clusters of the current display in the new display (if it was not a processing display).
        QList<int>* clusterList = new QList<int>();
        if(!isProcessWidget){
            const QList<int>& currentClusters = activeView()->clusters();

            QList<int>::const_iterator shownClustersIterator;
            for(shownClustersIterator = currentClusters.begin(); shownClustersIterator != currentClusters.end(); ++shownClustersIterator )
                clusterList->append(*shownClustersIterator);
        }

        //Use the current dimensions for the new display
        int XDimension = dimensionX->value();
        int YDimension = dimensionY->value();

        //Use the same scale, bin size, time frame and shoulder line for the new correlation view
        //as the existing one if it exists.
        Data::ScaleMode scaleMode = Data::MAX;
        int sizeOfBin = DEFAULT_BIN_SIZE.toInt();
        int correlogramTimeWindow = INITIAL_CORRELOGRAMS_HALF_TIME_FRAME.toInt() * 2 + 1;
        bool line = true;
        if(!isProcessWidget && activeView()->containsCorrelationView()){
            scaleMode = activeView()->scaleMode();
            sizeOfBin = binSize;
            correlogramTimeWindow = correlogramTimeFrame;
            line = activeView()->isShoulderLine();
        }

        //Use the same waveform presentation mode for the new display (will be apply only if
        //the new display contains a waveform view). The values of the associated widgets
        //(start and duration or spikesTodisplay) are the same as the activeDisplay ones.
        //The default for a new display is sample mode without mean and overlay.
        bool overLay = false;
        bool mean = false;
        bool inTimeFrameMode = false;
        long startingTime = 0;
        long timeFrameWidth = INITIAL_WAVEFORM_TIME_WINDOW.toLong();
        long nbSpkToDisplay = DEFAULT_NB_SPIKES_DISPLAYED;
        if(!isProcessWidget && activeView()->containsWaveformView()){
            overLay = activeView()->isOverLayPresentation();
            mean = activeView()->isMeanPresentation();

            if(activeView()->isInTimeFrameMode()){
                inTimeFrameMode = true;
                startingTime = startTime;
                timeFrameWidth = timeWindow;
            } else
                nbSpkToDisplay = spikesTodisplay->value();
        }

        KlustersView* view;

        if(!isProcessWidget)
            view = new KlustersView(*this,*doc,backgroundColor,XDimension,YDimension,clusterList,type,this,nullptr,statusBar(),
                                    displayTimeInterval,waveformsGain,channelPositions,inTimeFrameMode,startingTime,timeFrameWidth,
                                    nbSpkToDisplay,overLay,mean,sizeOfBin,correlogramTimeWindow,scaleMode,line,activeView()->getStartingTime(),activeView()->getDuration(),showHideLabels->isChecked(),activeView()->getUndoList(),activeView()->getRedoList());

        else
            view = new KlustersView(*this,*doc,backgroundColor,XDimension,YDimension,clusterList,type,this,nullptr,statusBar(),
                                    displayTimeInterval,waveformsGain,channelPositions,inTimeFrameMode,startingTime,timeFrameWidth,
                                    nbSpkToDisplay,overLay,mean,sizeOfBin,correlogramTimeWindow,scaleMode,line,activeView()->getStartingTime(),activeView()->getDuration(),showHideLabels->isChecked());

        view->setWindowTitle(displayName);
        tabsParent->addDockArea(view,displayType);
        view->installEventFilter(this);

        //Update the document's list of view
        doc->addView(view);

        // Apply current display preferences to the new view's ClusterViews
        doc->setMarkerSize(markerSize);
        doc->setSelectionLineWidth(selectionLineWidth);

        //Disconnect the previous connection.  tabsParent is created
        //unconditionally in the constructor and never cleared, so it is always
        //valid here; the following connect() relies on that too.  (disconnect()
        //on an object with no matching connections is a harmless no-op.)
        disconnect(tabsParent,nullptr,nullptr,nullptr);

        //Connect the change tab signal to slotTabChange(QWidget* widget) to trigger updates when
        //the active display change.
        connect(tabsParent, &QTabWidget::currentChanged, this, &KlustersApp::slotTabChange);

        //Keep track of the number of displays
        displayCount ++;

    }
}

void KlustersApp::openDocumentFile(const QString& url)
{    
    slotStatusMsg(tr("Opening file..."));

    filePath = url;
    QFileInfo file(filePath);
    if(!file.exists()){
        QString title = tr("File not found: %1").arg(filePath);
        int answer = QMessageBox::question(this,title, tr("The selected file no longer exists, do you want to remove it from the list?"),QMessageBox::Yes|QMessageBox::No);
        if(answer == QMessageBox::Yes) {
            mFileOpenRecent->removeRecentFile(url);
        } else  {
            mFileOpenRecent->addRecentFile(url); //hack, unselect the item
        }
        filePath.clear();
        slotStatusMsg(tr("Ready."));
        return;
    }

    //Check if the file exists
    if(!file.exists()){
        QMessageBox::critical (this, tr("Error!"),tr("The selected file does not exist."));
        return;

    }
    slotStatusMsg(tr("Ready."));
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    
    //If no document is open already open the document asked.
    if(!mainDock){
        displayCount = 0;
        currentNbUndo = 0;
        currentNbRedo = 0;

        mFileOpenRecent->addRecentFile(url);

        // Open the file (that will also initialize the doc)
        QString errorInformation;
        int returnStatus = doc->openDocument(url,errorInformation);
        if(returnStatus == KlustersDoc::INCORRECT_FILE)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this, tr("Error!"), tr("The selected file is invalid, it has to be of the form baseName.clu.n, baseName.clc.n, baseName.fet.n, or baseName.par.n — optionally followed by an experiment tag (e.g. baseName.clu.n.stack)."));
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();

            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        } else if(returnStatus == KlustersDoc::DOWNLOAD_ERROR)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this,tr("Error!"),tr("Could not get the cluster file (base.clu.n)") );
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();
            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        } else if(returnStatus == KlustersDoc::SPK_DOWNLOAD_ERROR)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this,tr("Error!"),tr("Could not get the spike file (base.spk.n)"));
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();
            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        } else if(returnStatus == KlustersDoc::FET_DOWNLOAD_ERROR)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this,tr("Error!"),tr("Could not get the feature file (base.fet.n)"));
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();
            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        } else if(returnStatus == KlustersDoc::PAR_DOWNLOAD_ERROR)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this,tr("Error!"),tr("Could not get the general parameter file (base.par)"));
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();
            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        } else if(returnStatus == KlustersDoc::PARX_DOWNLOAD_ERROR)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this,tr("Error!"), tr("Could not get the specific parameter file (base.par.n)") );
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();
            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        } else if(returnStatus == KlustersDoc::OPEN_ERROR)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this,tr("Error!"),tr("Could not open the files") );
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();
            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        } else if(returnStatus == KlustersDoc::INCORRECT_CONTENT)
        {
            QApplication::restoreOverrideCursor();
            QMessageBox::critical (this,tr("Error!"),errorInformation);
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //close the document
            doc->closeDocument();
            resetState();
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        }

        setWindowTitle(doc->documentName());
        initDisplay();

        //A traceView is possible only if the variables it needs are available (provided in the new parameter file) and
        //the .dat file exists.
        if(doc->areTraceDataAvailable() && doc->isTraceViewVariablesAvailable()) {
            slotStateChanged("traceDisplayState");
        }

        QApplication::restoreOverrideCursor();
    }
    // check, if this document is already open. If yes, do not do anything
    else{
        QString docName = doc->documentName();
        QFileInfo urlFileInfo(url);
        QStringList fileParts = urlFileInfo.fileName().split(".", Qt::SkipEmptyParts);

        // Parse via the shared custody policy so a tagged anchor
        // (<base>.<type>[.<method>].<grp>[.<suffix>], including .clc children)
        // maps to the same canonical `baseName-group` document name as
        // KlustersDoc::openDocument and the already-open check succeeds.  The
        // method token and any post-group suffix are not part of the name.
        const neurosuite::custody::Anchor anchor =
            neurosuite::custody::parseAnchor(urlFileInfo.fileName().toStdString());
        QString electrodNb;
        QString baseName;
        if (anchor.ok) {
            baseName   = QString::fromStdString(anchor.base);
            electrodNb = QString::number(anchor.group);
        } else {
            // Fall back to legacy behaviour (no recognised type token) so an
            // invalid filename still doesn't crash; the slotFileOpenRecent
            // path will print the usual "invalid filename" error downstream.
            if (fileParts.count() < 3)
                electrodNb.clear();
            else
                electrodNb = fileParts[fileParts.count()-1];
            baseName = fileParts[0];
            for (qsizetype i = 1; i < fileParts.count()-2; ++i)
                baseName += "." + fileParts[i];
        }
        QString name = urlFileInfo.absolutePath() + QDir::separator() + baseName + "-" + electrodNb;

        if(docName == name){
            mFileOpenRecent->addRecentFile(url); //hack, unselect the item
            QApplication::restoreOverrideCursor();
            slotStatusMsg(tr("Ready."));
            return;
        }
        //If the document asked is not the already open. Open a new instance of the application with it.
        else{
            mFileOpenRecent->removeRecentFile(url);
            filePath = url;


            QStringList command;
            command <<filePath;
            QProcess::startDetached("klusters", QStringList()<<command);


            QApplication::restoreOverrideCursor();
        }
    }

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::importDocumentFile(const QString& url)
{
    slotStatusMsg(tr("Importing file..."));

    //If no document is open already open the document ask.
    if(!mainDock){
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        // Open the file (that will also initialize the doc)
        if(!doc->importDocument(url))
        {
            QMessageBox::critical (this,tr("Error !"),tr("Could not import document !"));
            //close the document
            doc->closeDocument();
            QApplication::restoreOverrideCursor();
            return;
        }
        mFileOpenRecent->addRecentFile(url);
        initDisplay();
        QApplication::restoreOverrideCursor();
    }
    // check, if this document is already open. If yes, do not do anything
    else if(doc->url()==url){
        QApplication::restoreOverrideCursor();
        return;
    }

    //If the document asked is not the already open. Open a new instance of the application with it.
    //Only one document at the time is allowed.
    else
    {
        // Only one document at a time is supported.  Opening a second session
        // should launch a new application instance, but this is not yet
        // implemented; restore the cursor and fall through silently for now.
        QApplication::restoreOverrideCursor();
    }

    slotStatusMsg(tr("Ready."));
}

bool KlustersApp::doesActiveDisplayContainProcessWidget(){
    QWidget *widget = tabsParent->currentWidget();
    // Returns true for the recluster output tab (processWidget), a ProcessWidget.
    return qobject_cast<ProcessWidget*>(widget);
}

KlustersView* KlustersApp::activeView(){
    DockArea* area = tabsParent->currentDockArea();
    KlustersView *view = static_cast<KlustersView*>(area);
    return view;
}

//TO implement , see documentation
bool KlustersApp::queryClose()
{  
    //call when the kDockMainWindow will be close
    //implement to ask the user to save if necessary before closing
    if(doc == nullptr) return true;
    else{
        if(doc->canCloseView()){
            //Set a waiting cursor in case there is some delay to the ending of the running threads.
            QApplication::restoreOverrideCursor();
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            if(doc->canCloseDocument(this,"queryClose")){
                if(processWidget != nullptr && processWidget->isRunning()){
                    processWidget->killJob();
                    processKilled = true;
                }
                if(!(processFinished && processOutputsFinished)){
                    QTimer::singleShot(2000, this, &KlustersApp::close);
                    return false;
                }
                else if(processWidget != nullptr){
                    delete processWidget;
                    processWidget = nullptr ;
                }
                doc->closeDocument();
                QApplication::restoreOverrideCursor();
                return true;
            } else
                return false;
        } else
            return false;
    }
}

//TO implement , see documentation
void KlustersApp::customEvent (QEvent* event){

    //Event sent by the SaveThread
    if(event->type() == QEvent::User + 100){
        slotStatusMsg(tr("Save file done."));
        SaveThread::SaveDoneEvent* saveEvent = (SaveThread::SaveDoneEvent*) event;
        if(saveEvent->isSaveOk()){
            //The cluster data have been saved to a local temporary file if the original file was a remote one
            //(otherwise the so call temporary file is the orignal one).
            //If the requested save location is remote, the temporary file needs now
            //to be uploaded. The save process is made in a thread and it seams that
            //the KDE upload can not be call asynchronously, so the upload is call after the end of the thread.
            if(saveEvent->isItSaveAs()){
                mFileOpenRecent->addRecentFile(doc->url());
                setWindowTitle(doc->documentName());
            }
        } else {
            // patch63 — show the actual error rather than a generic "I/O Error"
            // that left the user with no information about what failed.
            const QString detail = saveEvent->error();
            const QString msg = detail.isEmpty()
                ? tr("Could not save the current document.")
                : detail;
            QMessageBox::critical(this, tr("Save failed"), msg);
        }

        slotStatusMsg(tr("Ready."));
        slotStateChanged("SavingDoneState");
    }
    //Event sent by klusterDoc to advice that there is some threads still running.
    if(event->type() == QEvent::User + 400){
        KlustersDoc::CloseDocumentEvent* closeEvent = (KlustersDoc::CloseDocumentEvent*) event;
        QString origin = closeEvent->methodOfOrigin();

        //Try to close the document again
        if(doc->canCloseDocument(this,origin)){
            doc->closeDocument();

            //Execute what is need it after the close depending on the callingMethod
            if(origin == "queryClose"){
                QApplication::restoreOverrideCursor();
                close();
            }
            else if(origin == "fileClose" || origin == "displayClose"){
                slotFileClose();
                QApplication::restoreOverrideCursor();
                slotStatusMsg(tr("Ready."));
            }
        }
    }

    //Event sent by the parameterBar to advice that it has been resized.
    if(event->type() == QEvent::User + 1000){
        slotUpdateParameterBar();
    }
}

/////////////////////////////////////////////////////////////////////
// SLOT IMPLEMENTATION
/////////////////////////////////////////////////////////////////////


void KlustersApp::slotFileOpen()
{
    slotStatusMsg(tr("Opening file..."));

    QSettings settings;
    const QString url=QFileDialog::getOpenFileName(this, tr("Open File..."), settings.value("CurrentDirectory").toString(),
                                             tr("Feature File (*.fet.*);;Cluster File (*.clu.*);;Cluster Children File (*.clc.*);;Spike File (*.spk.*);;Specific Parameter File (*.par.*);;All files (*.*)"));
    if(!url.isEmpty())
    {
        QDir CurrentDir;
        settings.setValue("CurrentDirectory", CurrentDir.absoluteFilePath(url));
        openDocumentFile(url);
    }

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotFileClose(){
    if(doc != nullptr){
        while(!saveThread->wait())
        {
        };

        if(doc->canCloseView()){
            QApplication::restoreOverrideCursor();
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //try to close the document
            if(doc->canCloseDocument(this,"fileClose")){
                if(processWidget) {
                    if(processWidget->isRunning()){
                        processWidget->killJob();
                        processKilled = true;
                    }
                    if(processFinished && processOutputsFinished){
                        processWidget = nullptr;
                    } else{
                        QTimer::singleShot(2000, this, &KlustersApp::slotFileClose);
                        return;
                    }
                }
                while(true){
                    DockArea* current = static_cast<DockArea*>(tabsParent->widget(0));
                    tabsParent->removeTab(tabsParent->indexOf(current));
                    delete current;
                    if(tabsParent->count()==0)
                        break;

                }
                //reset the cluster palette and hide the cluster panel
                clusterPalette->reset();
                clusterPanel->hide();
                mainDock = nullptr;
                doc->closeDocument();
                resetState();
                QApplication::restoreOverrideCursor();
            }
        }
    }
}

void KlustersApp::slotFileImport(){
    slotStatusMsg(tr("Importing file..."));

    QSettings settings;
    const QString url=QFileDialog::getOpenFileName(this, tr("Import File..."), settings.value("CurrentDirectory").toString(),
                                             tr("All files (*.*)"));
    if(!url.isEmpty())
    {
        QDir CurrentDir;
        settings.setValue("CurrentDirectory", CurrentDir.absoluteFilePath(url));
        importDocumentFile(url);
    }

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotFileOpenRecent(const QString& url)
{
    slotStatusMsg(tr("Opening file..."));

    openDocumentFile(url);

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotFileSave()
{
    slotStatusMsg(tr("Saving file..."));

    slotStateChanged("SavingState");
    saveThread->save(doc->url(),doc,false);
}

void KlustersApp::slotFileRenumberAndSave(){
    slotStatusMsg(tr("Renumbering and saving..."));
    slotStateChanged("SavingState");
    doc->renumberClusters();
    slotFileSave();
}

void KlustersApp::slotFileSaveAs()
{
    slotStatusMsg(tr("Saving file with a new filename..."));
    QString url=QFileDialog::getSaveFileName(this,tr("Save as..."),QDir::currentPath(),
                                             tr("All files (*.*)") );
    if(!url.isEmpty()){
        slotStateChanged("SavingState");
        saveThread->save(url,doc,true);
    }
}

void KlustersApp::slotDisplayClose()
{
    DockArea* current = static_cast<DockArea*>(tabsParent->currentWidget());

    slotStatusMsg(tr("Closing display..."));

    //Get the active tab
    if(tabsParent->count()>1){
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
        //Remove the display from the group of tabs
        tabsParent->removeTab(tabsParent->indexOf(current));
        displayCount --;

        //Remove the view from the document list if need it
        if(qobject_cast<KlustersView*>(current)){
            KlustersView* view = dynamic_cast<KlustersView*>(current);
            doc->removeView(view);

            //Update the Displays menu if the current display is a grouping assistant.
            if(view->containsErrorMatrixView()){
                slotStateChanged("groupingAssistantDisplayNotExists");
                errorMatrixExists = false;
            }
            if(view->containsTemplateMatrixView()){
                templateMatrixExists = false;
            }
            //Delete the view
            delete current;
        } else {
            if(processFinished && processOutputsFinished){
                delete current;
                processWidget = nullptr;
            } else {
                processWidget->hideWidget();
            }
        }
        QApplication::restoreOverrideCursor();
    }
    //or the active window if there is only one display (which can only be the mainDock)
    else {
        //If a save is already in process, wait until it is done
        if(saveThread->isRunning()){
            QApplication::restoreOverrideCursor();
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            while(!saveThread->wait()){};

            //reset the cluster palette and hide the cluster panel
            clusterPalette->reset();
            clusterPanel->hide();
            //try to close the document
            if(doc->canCloseDocument(this,"displayClose")){
                doc->closeDocument();
                //Delete the view
                if(qobject_cast<KlustersView*>(tabsParent->currentWidget())){
                    delete processWidget;
                    processWidget = nullptr;
                    delete tabsParent->currentWidget();
                } else {
                    if(processWidget->isRunning()){
                        processWidget->killJob();
                        processKilled = true;
                    }
                    if(processFinished && processOutputsFinished){
                        delete tabsParent->currentWidget();
                        processWidget = nullptr;
                    } else {
                        mainDock->hide();
                        processWidget->hideWidget();
                        QTimer::singleShot(2000, this, &KlustersApp::slotDisplayClose);
                        return;
                    }
                }
                mainDock = nullptr;
                QApplication::restoreOverrideCursor();
            }
        }
        //Ask the user if wants to save the document if need it before closing and confirm the closing
        else if(doc->canCloseView()){
            QApplication::restoreOverrideCursor();
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            //reset the cluster palette and hide the cluster panel
            clusterPalette->reset();
            clusterPanel->hide();
            //try to close the document
            if(doc->canCloseDocument(this,"displayClose")){
                doc->closeDocument();
                //Delete the view
                if(qobject_cast<KlustersView*>(tabsParent->currentWidget())){
                    delete processWidget;
                    processWidget = nullptr;
                    delete tabsParent->currentWidget();
                }
                else{
                    if(processWidget) {
                        if(processWidget->isRunning()){
                            processWidget->killJob();
                            processKilled = true;
                        }
                        if(processFinished && processOutputsFinished){
                            delete tabsParent->currentWidget();
                            processWidget = nullptr;
                        } else {
                            mainDock->hide();
                            processWidget->hideWidget();
                            QTimer::singleShot(2000, this, &KlustersApp::slotDisplayClose);
                            return;
                        }
                    }
                }
                tabsParent->removeTab(0);
                tabsParent->hide();
                mainDock = nullptr;
                QApplication::restoreOverrideCursor();
            }
            resetState();
        }
    }
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotFilePrint()
{
    slotStatusMsg(tr("Printing..."));
    QPrinter printer;
    printer.setPageOrientation(QPageLayout::Landscape);
    printer.setColorMode(QPrinter::Color);

    QPrintDialog dialog(&printer, this);
    if (dialog.exec())
    {
        if(!doesActiveDisplayContainProcessWidget()){
            KlustersView* view = activeView();
            if(useWhiteColorDuringPrinting)
                view->print(&printer,filePath,true);
            else
                view->print(&printer,filePath,false);
        }
        else{
            QDockWidget* dock = static_cast<QDockWidget*>(tabsParent->currentWidget());
            ProcessWidget* view = static_cast<ProcessWidget*>(dock->widget());
            view->print(&printer,filePath);
        }
    }

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotFileQuit()
{
    slotStatusMsg(tr("Exiting..."));

    if (!queryClose()) {
        slotStatusMsg(tr("Ready."));
        return;
    }
    close();
}

void KlustersApp::readSettings()
{
    QSettings settings;
    settings.beginGroup("geometry");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    settings.endGroup();
}

void KlustersApp::closeEvent(QCloseEvent *event)
{
    if (!queryClose()) {
        event->ignore();
        slotStatusMsg(tr("Ready."));
        return;
    }
    QSettings settings;
    settings.beginGroup("geometry");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.endGroup();
    settings.sync();

    // Disconnect ALL signals to/from this object and every KlustersView
    // BEFORE QMainWindow::closeEvent triggers widget destruction.
    // This must happen here — by the time ~KlustersApp runs, Qt's
    // destruction machinery has already begun.
    NS3_DIAG() << "[closeEvent] disconnecting all";
    disconnect();
    if (doc) {
        // Disconnect all doc<->view and doc<->view-child connections.
        const QList<KlustersView*> views = doc->viewListCopy();
        for (KlustersView* v : views) {
            if (!v) continue;
            QObject::disconnect(doc, nullptr, v, nullptr);
            QObject::disconnect(v, nullptr, doc, nullptr);
            v->disconnectAllChildren();
            v->disconnect();
        }
    }

    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
// runUndoOrRedo
//
// Shared body for slotUndo / slotRedo.  Both run the corresponding doc-
// level operation under a wait cursor, refresh the traceView browsing
// state, and restore focus.  The only differences are the status message
// and which member function (KlustersDoc::undo or ::redo) is invoked.
// ---------------------------------------------------------------------------
void KlustersApp::runUndoOrRedo(void (KlustersDoc::*op)(),
                                 const QString& busyMessage)
{
    slotStatusMsg(busyMessage);
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    (doc->*op)();
    QApplication::restoreOverrideCursor();

    // Update the browsing possibility of the traceView
    KlustersView* view = activeView();
    if (view && view->containsTraceView() && !view->clusters().isEmpty()) {
        slotStateChanged("traceViewBrowsingState");
    } else {
        slotStateChanged("noTraceViewBrowsingState");
    }

    slotStatusMsg(tr("Ready."));
    if (view) view->focusClusterView();
}

void KlustersApp::slotUndo()
{
    runUndoOrRedo(&KlustersDoc::undoDispatch,  tr("Reverting last action..."));
}

void KlustersApp::slotRedo()
{
    runUndoOrRedo(&KlustersDoc::redoDispatch,  tr("Reverting last undo action..."));
}

void KlustersApp::slotUpdateUndoNb(int undoNb){
    currentNbUndo = undoNb;
    // Layer-scoped undo: the count is already the shown layer's (parent or atom),
    // so the main Undo reflects that layer alone.
    if(currentNbUndo > 0) {
        slotStateChanged("undoState");
    }
    else {
        slotStateChanged("emptyUndoState");
    }
}

void KlustersApp::slotUpdateRedoNb(int redoNb){
    currentNbRedo = redoNb;
    // Layer-scoped: disable Redo when the shown layer's redo stack is exhausted.
    if(currentNbRedo == 0) {
        slotStateChanged("emptyRedoState");
    }
}

void KlustersApp::slotViewMainToolBar()
{
    slotStatusMsg(tr("Toggle the main toolbar..."));

    mMainToolBar->setVisible(viewMainToolBar->isChecked());
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotViewActionBar(){

    slotStatusMsg(tr("Toggle the action..."));

    // turn Toolbar on or off

    mActionBar->setVisible(viewActionBar->isChecked());
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotViewParameterBar(){
    slotStatusMsg(tr("Toggle the parameters..."));
    // turn Toolbar on or off
    if(!viewParameterBar->isChecked())
    {
        paramBar->hide();
    }
    else
    {
        paramBar->show();
        slotUpdateParameterBar();
    }
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotViewToolBar()
{
    slotStatusMsg(tr("Toggle the tools..."));

    // turn Toolbar on or off
    mToolBar->setVisible(viewToolBar->isChecked());

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotViewStatusBar()
{
    slotStatusMsg(tr("Toggle the statusbar..."));
    ///////////////////////////////////////////////////////////////////
    //turn Statusbar on or off
    statusBar()->setVisible(mViewStatusBar->isChecked());

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotViewClusterInfo(){
    slotStatusMsg(tr("Toggle the presentation of the cluster information in the cluster palette..."));

    // turn the user cluster information on or off
    if(!viewClusterInfo->isChecked())
    {
        clusterPalette->hideUserClusterInformation();
    }
    else
    {
        doc->showUserClusterInformation();
    }

    slotStatusMsg(tr("Ready."));

}


void KlustersApp::slotWindowNewClusterDisplay()
{
    slotStatusMsg(tr("Opening a new cluster view..."));

    createDisplay(KlustersView::CLUSTERS);
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotWindowNewWaveformDisplay()
{
    slotStatusMsg(tr("Opening a new waveform view..."));

    createDisplay(KlustersView::WAVEFORMS);
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotWindowNewCrosscorrelationDisplay()
{
    slotStatusMsg(tr("Opening a new correlation view..."));

    createDisplay(KlustersView::CORRELATIONS);
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotWindowNewOverViewDisplay()
{
    slotStatusMsg(tr("Opening a new over view..."));

    createDisplay(KlustersView::OVERVIEW);
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotWindowNewGroupingAssistantDisplay()
{
    slotStatusMsg(tr("Opening a new grouping assistant view..."));

    createDisplay(KlustersView::GROUPING_ASSISTANT_VIEW);
    slotStateChanged("groupingAssistantDisplayExists");
    errorMatrixExists = true;

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotNewTraceDisplay(){
    slotStatusMsg(tr("Opening a new grouping assistant view..."));

    createDisplay(KlustersView::TRACES);

    slotStatusMsg(tr("Ready."));
}


void KlustersApp::slotStatusMsg(const QString &text)
{
    ///////////////////////////////////////////////////////////////////
    // change status message permanently
    statusBar()->clearMessage();
    statusBar()->showMessage(text);
}


/*Slots for the actions menu*/
/**Creates a single cluster by selecting an area*/
void KlustersApp::slotSingleNew(){
    slotStatusMsg(tr("Create new cluster..."));

    //If we are in delay mode, update the display, if need it, before triggering the tool change
    if(mDelaySelection->isChecked()){
        clusterPalette->updateClusters();
    }

    KlustersView* view = activeView();
    view->setMode(ViewWidget::NEW_CLUSTER);
    slotStatusMsg(tr("Ready."));
}
/**Creates a multiple clusters by selecting an area*/
void KlustersApp::slotMultipleNew(){
    slotStatusMsg(tr("Split clusters..."));

    //If we are in delay mode, update the display, if need it, before triggering the tool change
    if(mDelaySelection->isChecked()){
        clusterPalette->updateClusters();
    }

    KlustersView* view = activeView();
    view->setMode(ViewWidget::NEW_CLUSTERS);
    slotStatusMsg(tr("Ready."));
}
/**Deletes spikes from a cluster and move them to the cluster (number 1) containing the poorly isolated cells*/
void KlustersApp::slotDeleteNoise(){
    slotStatusMsg(tr("Delete noise..."));

    //If we are in delay mode, update the display, if need it, before triggering the tool change
    if(mDelaySelection->isChecked()){
        clusterPalette->updateClusters();
    }

    KlustersView* view = activeView();
    view->setMode(ViewWidget::DELETE_NOISE);
    slotStatusMsg(tr("Ready."));
}
/**Deletes spikes from a cluster and move them to the cluster (number 0) containing the artifacts*/
void KlustersApp::slotDeleteArtefact(){
    slotStatusMsg(tr("Delete artifact..."));

    //If we are in delay mode, update the display, if need it, before triggering the tool change
    if(mDelaySelection->isChecked()){
        clusterPalette->updateClusters();
    }

    KlustersView* view = activeView();
    view->setMode(ViewWidget::DELETE_ARTEFACT);
    slotStatusMsg(tr("Ready."));
}
/**Zooms*/

void KlustersApp::slotIncreasePointSize(){
    if(!activeView()) return;
    const QList<ViewWidget*>& views = activeView()->getViewList();
    for(ViewWidget* w : views){
        ClusterView* cv = qobject_cast<ClusterView*>(w);
        if(cv) cv->setPointSize(cv->getPointSize() + 1);
    }
}

void KlustersApp::slotDecreasePointSize(){
    if(!activeView()) return;
    const QList<ViewWidget*>& views = activeView()->getViewList();
    for(ViewWidget* w : views){
        ClusterView* cv = qobject_cast<ClusterView*>(w);
        if(cv) cv->setPointSize(cv->getPointSize() - 1);
    }
}

void KlustersApp::slotZoom(){
    slotStatusMsg(tr("Zooming..."));

    //If we are in delay mode, update the display, if need it, before triggering the tool change
    if(mDelaySelection->isChecked()){
        clusterPalette->updateClusters();
    }

    KlustersView* view = activeView();
    view->setMode(BaseFrame::ZOOM);
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotSelectTime(){
    slotStatusMsg(tr("Selecting time..."));

    //If we are in delay mode, update the display, if need it, before triggering the tool change
    if(mDelaySelection->isChecked()){
        clusterPalette->updateClusters();
    }

    KlustersView* view = activeView();
    view->setMode(ViewWidget::SELECT_TIME);

    slotStatusMsg(tr("Ready."));
}


// ---------------------------------------------------------------------------
// Watershed live-preview mode (Shift+W)
//
// Replaces the pre-2026-04 modal dialog with an in-place overlay drawn
// directly on the active scatter view.  Pressing Shift+W enters
// preview: per-cluster (X, Y) feature points are extracted once, the
// watershed kernel runs against the active view's current X/Y
// dimensions, and the resulting basin partition is rendered as a
// translucent coloured overlay on top of the scatter via
// ClusterView::setWatershedOverlay.  A small HUD at top-left shows the
// current parameters and key bindings.
//
// While in preview:
//   ←/→     adjust smoothing sigma  (1..32 cells, ±1 per press, ±5 with Shift)
//   ↑/↓     adjust peak threshold   (0..50% of grid max, ±1 per press, ±5 with Shift)
//   Enter   commit the partition
//   Esc     cancel
//
// Other keys are blocked while preview is active (intercepted in
// eventFilter, see the wsPreviewActive branch there) so the user doesn't
// accidentally trigger an unrelated action mid-tune.
// ---------------------------------------------------------------------------
void KlustersApp::slotWatershedSplit()
{
    if (wsPreviewActive) {
        // Pressing Shift+W while already in preview is a no-op.  Use
        // Enter to commit, Esc to cancel.
        return;
    }
    wsEnter();
}

bool KlustersApp::wsEnter()
{
    if (!activeView() || !clusterPalette) return false;
    if (doesActiveDisplayContainProcessWidget()) return false;

    QList<int> sel = clusterPalette->selectedClusters();
    sel.removeAll(0);
    sel.removeAll(1);
    if (sel.isEmpty()) {
        statusBar()->showMessage(
            tr("Watershed: select one or more clusters in the palette first."), 4000);
        return false;
    }

    KlustersView* aview = activeView();
    if (!aview->containsClusterView()) {
        statusBar()->showMessage(
            tr("Watershed: the active display has no scatter view."), 4000);
        return false;
    }

    // Locate the ClusterView widget.  It is the active KlustersView's
    // currentViewWidget when containsClusterView() is true; cast through
    // the ViewWidget base.
    ClusterView* scatter = nullptr;
    {
        QList<ViewWidget*> vws = aview->getViewList();
        for (ViewWidget* vw : vws) {
            if ((scatter = qobject_cast<ClusterView*>(vw))) break;
        }
    }
    if (!scatter) {
        statusBar()->showMessage(
            tr("Watershed: could not locate the scatter view."), 4000);
        return false;
    }

    const int dimX = aview->abscissaDimension();
    const int dimY = aview->ordinateDimension();

    // Extract (X, Y) coordinates for every spike of every selected cluster.
    QVector<double> xs;
    QVector<double> ys;
    {
        size_t totalEst = 0;
        for (int cid : sel) totalEst += doc->data().nbOfSpikes(cid);
        xs.reserve(static_cast<int>(totalEst));
        ys.reserve(static_cast<int>(totalEst));
    }
    for (int cid : sel) {
        SortableTable subset;
        if (!doc->data().spikePositions(cid, subset)) continue;
        const int n = static_cast<int>(subset.nbOfColumns());
        for (int i = 1; i <= n; ++i) {
            const auto row = subset(1, i);
            xs.append(static_cast<double>(doc->data().featureValue(row, dimX)));
            ys.append(static_cast<double>(doc->data().featureValue(row, dimY)));
        }
    }
    if (xs.size() < 50) {
        statusBar()->showMessage(
            tr("Watershed: not enough spikes (need at least 50)."), 4000);
        return false;
    }

    // Stash state.  Initial sigma/threshold values are remembered from
    // the previous invocation (wsSigmaCells / wsThreshPct are
    // persistent members) so re-tuning a similar partition is fast.
    wsSel       = sel;
    wsXs        = std::move(xs);
    wsYs        = std::move(ys);
    wsDimX      = dimX;
    wsDimY      = dimY;
    wsScatter   = scatter;
    wsView      = aview;
    wsPreviewActive    = true;
    wsCachedSigma = -1;     // force gridMax probe on first recompute

    // First run: snap sigma to auto-tune so the user starts from a
    // sensible default.  We discover what the kernel chose, then write
    // it back into wsSigmaCells.
    {
        Watershed2D::Config cfg;
        cfg.keepGrid = true;
        wsResult = Watershed2D::run(
            std::vector<double>(wsXs.begin(), wsXs.end()),
            std::vector<double>(wsYs.begin(), wsYs.end()),
            cfg);
    }
    if (!wsResult.ok) {
        statusBar()->showMessage(
            tr("Watershed: input degenerate (all spikes share an X or Y feature)."), 5000);
        wsPreviewActive = false;
        wsScatter = nullptr;
        wsView = nullptr;
        wsSel.clear();
        wsXs.clear();
        wsYs.clear();
        return false;
    }
    wsSigmaCells   = qBound(1, static_cast<int>(std::round(wsResult.effSigma)), 32);
    wsThreshPct    = 5;     // auto-tune is 5% of gridMax
    wsGridMax      = wsResult.effPeakHeight / 0.05;
    wsCachedSigma  = wsSigmaCells;

    wsRefreshOverlay();
    return true;
}

void KlustersApp::wsExit(bool commit)
{
    if (!wsPreviewActive) return;

    // Snapshot the bits we still need before clearing state.
    const QList<int>          sel    = wsSel;
    const int                 sigma  = wsSigmaCells;
    const int                 thresh = wsThreshPct;
    const double              gridMax = wsGridMax;
    ClusterView* const        scatter = wsScatter;

    // Clear state (also drops the overlay on the view).
    if (scatter) scatter->clearWatershedOverlay();
    wsPreviewActive  = false;
    wsScatter = nullptr;
    wsView    = nullptr;
    wsSel.clear();
    wsXs.clear();
    wsYs.clear();
    wsResult = Watershed2D::Result{};

    if (!commit) {
        if (clusterPalette) clusterPalette->setFocusToList();
        statusBar()->showMessage(tr("Watershed cancelled."), 3000);
        return;
    }

    // Apply.  Build the same Config the live-preview was using.
    Watershed2D::Config cfg;
    cfg.gridSize      = 256;
    cfg.smoothSigma   = sigma;
    cfg.minPeakHeight = (thresh / 100.0) * gridMax;
    if (cfg.minPeakHeight < 1e-6) cfg.minPeakHeight = 0;
    cfg.minBasinSize  = 0;     // auto

    slotStatusMsg(tr("Applying watershed split..."));
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    const int nNew = doc->watershedSelectedClusters(sel, cfg);
    QApplication::restoreOverrideCursor();

    if (nNew == 0) {
        statusBar()->showMessage(tr(
            "Watershed: no split — only one density basin found at the chosen settings."), 5000);
    } else {
        statusBar()->showMessage(
            tr("Watershed: %1 new clusters created from %2 source%3.")
                .arg(nNew).arg(sel.size()).arg(sel.size() == 1 ? "" : "s"),
            4000);
    }
    if (clusterPalette) clusterPalette->setFocusToList();
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::wsRecompute()
{
    if (!wsPreviewActive) return;

    // If sigma changed, the histogram + smoothing change too, so the
    // grid maximum changes — re-discover it via a probe pass with the
    // auto-tune sentinel.  If sigma is unchanged, reuse the cached
    // gridMax to halve the work per arrow press.
    Watershed2D::Config cfg;
    cfg.gridSize      = 256;
    cfg.smoothSigma   = wsSigmaCells;
    cfg.minBasinSize  = 0;     // auto
    cfg.keepGrid      = true;

    if (wsCachedSigma != wsSigmaCells) {
        Watershed2D::Config probeCfg = cfg;
        probeCfg.minPeakHeight = 0;     // auto = 5% of max — used to learn max
        const Watershed2D::Result probe = Watershed2D::run(
            std::vector<double>(wsXs.begin(), wsXs.end()),
            std::vector<double>(wsYs.begin(), wsYs.end()),
            probeCfg);
        if (!probe.ok) return;
        wsGridMax     = probe.effPeakHeight / 0.05;
        wsCachedSigma = wsSigmaCells;
    }
    cfg.minPeakHeight = (wsThreshPct / 100.0) * wsGridMax;
    if (cfg.minPeakHeight < 1e-6) cfg.minPeakHeight = 0;     // 0% slider → auto

    wsResult = Watershed2D::run(
        std::vector<double>(wsXs.begin(), wsXs.end()),
        std::vector<double>(wsYs.begin(), wsYs.end()),
        cfg);
    wsRefreshOverlay();
}

void KlustersApp::wsRefreshOverlay()
{
    if (!wsPreviewActive || !wsScatter) return;

    // Build the basin-coloured QImage.  Row 0 of the image corresponds
    // to the LARGEST feature-Y so it lands at the visual top of the
    // overlay (matches ClusterView's negate-Y convention; see
    // ClusterView::paintWatershedOverlay).  The kernel's grid is row-
    // major with grid-row 0 = smallest feature-Y, so we flip during
    // setPixel via (W-1-y).
    const int W = wsResult.gridSize;
    QImage img;
    if (W > 0 && !wsResult.cellLabels.empty()) {
        img = QImage(W, W, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);

        // ── Contrast-aware basin coloring ───────────────────────────────
        // The overlay sits on top of the cluster scatter at alpha ≈ 110/255
        // (~43% opacity), so the basin fill blends with the underlying
        // spike colour.  If a basin happens to share a hue with the
        // cluster it covers, the boundary becomes invisible — basin red
        // over cluster red still reads as red.  To keep the partition
        // legible regardless of which clusters the user has selected,
        // we compute the set of hues currently used by visible clusters
        // (i.e. forbidden hues), then pick basin hues at least 25° away
        // from any of them.
        //
        // Cluster colours are assigned by KlustersDoc with the formula
        //     hue = (clusterId * 7 mod 36) * 10            (s=200, v=255)
        // so they live in the 36-bucket grid {0, 10, 20, …, 350}.  That
        // leaves plenty of room for the basin overlay to find a clear
        // hue almost always — in the worst case (very many clusters
        // shown) we degrade gracefully to the rotation with maximum
        // clearance instead of giving up.
        QSet<int> forbiddenHues;
        if (wsView) {
            const QList<int>& shown = wsView->clusters();
            for (int cid : shown) {
                if (cid <= 1) continue;     // 0/1 = artefact/noise, drawn black/grey
                forbiddenHues.insert(
                    static_cast<int>(std::fmod(cid * 7.0, 36.0)) * 10);
            }
        }

        // Minimum angular distance from any forbidden hue, in degrees.
        // 25° = roughly two-and-a-half buckets of the 10°-quantized
        // cluster palette — enough that the basin fill reads as a
        // distinct hue even when alpha-blended with a cluster point.
        constexpr int kRequiredClearance = 25;

        auto hueClearance = [&forbiddenHues](int candidate) -> int {
            if (forbiddenHues.isEmpty()) return 360;
            int minDist = 360;
            for (int f : forbiddenHues) {
                int d = std::abs(candidate - f);
                if (d > 180) d = 360 - d;     // wrap around the colour wheel
                if (d < minDist) minDist = d;
            }
            return minDist;
        };

        // Cache per-label hue choices.  With up to ~30 basins and 256²
        // cells, computing the rotation per cell would be 65k×30 ops;
        // caching reduces it to one rotation per basin.
        QHash<int, int> labelToHue;
        auto basinHue = [&](int label) -> int {
            auto it = labelToHue.constFind(label);
            if (it != labelToHue.constEnd()) return it.value();

            const int seed = (label * 47) % 360;
            int chosen = seed;
            if (!forbiddenHues.isEmpty() &&
                hueClearance(seed) < kRequiredClearance) {
                // Rotate in 30° steps; pick the first rotation with
                // sufficient clearance, falling back to the rotation
                // with maximum clearance if none clear the threshold.
                int bestHue = seed;
                int bestClearance = hueClearance(seed);
                for (int step = 30; step <= 330; step += 30) {
                    const int candidate = (seed + step) % 360;
                    const int cl = hueClearance(candidate);
                    if (cl >= kRequiredClearance) {
                        bestHue = candidate;
                        bestClearance = cl;
                        break;
                    }
                    if (cl > bestClearance) {
                        bestHue = candidate;
                        bestClearance = cl;
                    }
                }
                chosen = bestHue;
            }
            labelToHue.insert(label, chosen);
            return chosen;
        };

        for (int y = 0; y < W; ++y) {
            for (int x = 0; x < W; ++x) {
                const int lab = wsResult.cellLabels[y * W + x];
                if (lab <= 0) continue;
                QColor c;
                c.setHsv(basinHue(lab), 200, 220);
                c.setAlpha(110);
                img.setPixel(x, W - 1 - y, c.rgba());
            }
        }
    }

    // Count unassigned spikes for the HUD.
    int unassigned = 0;
    for (int lab : wsResult.pointLabels)
        if (lab == 0) ++unassigned;
    const int N = static_cast<int>(wsResult.pointLabels.size());

    const QString hud = tr(
        "Watershed preview\n"
        "  σ = %1 cells     thr = %2%%   →  %3 basins, %4 peaks\n"
        "  %5 / %6 spikes outside any basin\n"
        "  ←/→ σ     ↑/↓ thr     Shift+arrow ×5\n"
        "  Enter: apply     Esc: cancel")
        .arg(wsSigmaCells)
        .arg(wsThreshPct)
        .arg(wsResult.numBasins)
        .arg(wsResult.numPeaks)
        .arg(unassigned).arg(N);

    wsScatter->setWatershedOverlay(img,
                                     wsResult.xMin, wsResult.xMax,
                                     wsResult.yMin, wsResult.yMax,
                                     hud);
}




void KlustersApp::slotSingleColorUpdate(int clusterId){
    if (!activeView()) return;
    //Trigger the action only if the active display does not contain a ProcessWidget
    if(!doesActiveDisplayContainProcessWidget()){
        KlustersView* view = activeView();
        doc->singleColorUpdate(clusterId,*view);
    }
}

void KlustersApp::slotUpdateShownClusters(const QList<int>& selectedClusters){
    slotRefreshMergeRecommendations();   // recommendations follow the selection
    //Trigger ths action only if the active display does not contain a ProcessWidget
    if(!activeView())
        return;
    // A parent selection always returns the views to the parent clustering; any
    // child sub-selection is re-applied afterwards via the child palette.
    if(doc) doc->setActiveClustering(false);
    if(!doesActiveDisplayContainProcessWidget()){

        //Update the browsing possibility of the traceView
        if(activeView()->containsTraceView() && !selectedClusters.isEmpty()) {
            slotStateChanged("traceViewBrowsingState");
        }
        else{
            slotStateChanged("noTraceViewBrowsingState");
        }

        KlustersView* view = activeView();
        doc->shownClustersUpdate(selectedClusters,*view);
    }
    // hierarchical view: refresh the child palette for the new parent selection
    if(childPanel && childPanel->isVisible())
        repopulateChildPalette(selectedClusters);
}


void KlustersApp::slotChunkModeToggled(bool on){
    // No `if (!doc)` guard here: doc is created in the constructor and destroyed
    // in the destructor, never nulled, so that test can never be true.  It read
    // like the no-document protection and was not one -- the action simply stayed
    // enabled at startup and this ran on an empty document, popping the dialog
    // below before any session existed.  The protection is now where the other 67
    // actions keep theirs: slotStateChanged disables mChunkMode until a document
    // is open, and re-disables it while a recluster or realign owns the data.
    if(on){
        bool ok = false;
        const double minutes = QInputDialog::getDouble(
            this, tr("Time-Chunk Mode"), tr("Chunk length (minutes):"),
            12.0, 0.1, 100000.0, 1, &ok);
        if(!ok){
            mChunkMode->setChecked(false);
            return;
        }
        doc->enterChunkMode(minutes);
    } else {
        doc->exitChunkMode();
    }
    updateChunkStatus();
}

void KlustersApp::slotNextChunk(){
    if(!doc->inChunkMode())
        return;
    doc->nextChunk();
    updateChunkStatus();
}

void KlustersApp::slotPrevChunk(){
    if(!doc->inChunkMode())
        return;
    doc->prevChunk();
    updateChunkStatus();
}

void KlustersApp::updateChunkStatus(){
    if(!doc->inChunkMode()){
        mPrevChunk->setEnabled(false);
        mNextChunk->setEnabled(false);
        return;
    }
    const int c = doc->currentChunk();
    const int n = doc->chunkCount();
    long t0 = 0, t1 = 0;
    doc->chunkTimeWindow(c, t0, t1);
    const double fs = doc->data().getSamplingRate();
    const double t0min = (fs > 0.0) ? (static_cast<double>(t0) / fs / 60.0) : 0.0;
    const double t1min = (fs > 0.0) ? (static_cast<double>(t1) / fs / 60.0) : 0.0;
    mPrevChunk->setEnabled(c > 0);
    mNextChunk->setEnabled(c + 1 < n);
    statusBar()->showMessage(tr("Chunk %1/%2  [%3 - %4 min]")
                                 .arg(c + 1).arg(n)
                                 .arg(t0min, 0, 'f', 1).arg(t1min, 0, 'f', 1));
}

void KlustersApp::slotGroupClusters(QList<int> selectedClusters){
    slotStatusMsg(tr("Grouping clusters..."));
    KlustersView* view = activeView();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    const int mergedClusterId = doc->groupClusters(selectedClusters,*view);
    QApplication::restoreOverrideCursor();
    slotStatusMsg(tr("Ready."));
    // Group is a keyboard-driven palette operation (G key from the palette
    // context); keep focus on the palette so the user can keep
    // arrow-navigating without having to Tab back.
    if (clusterPalette) clusterPalette->setFocusToList();

    // Post-merge automation, fully serialised (no concurrent Data access).
    //
    // A merge can drive up to three opt-in operations: auto-align (realign the
    // merged cluster), auto-renumber, and auto-update-matrices.  The realign
    // worker mutates Data on a background thread, while renumber and the matrix
    // recompute touch Data on the main thread / their own threads — running
    // them together races and crashes.
    //
    // If auto-align is enabled, run it FIRST on the just-merged cluster, then
    // run autoPostMerge() (renumber → matrix) strictly after.  Ordering is by
    // the realign job queue: the realign runs as a RealignJob and the post-merge
    // step is enqueued right behind it as a LambdaJob, so it cannot start — and
    // cannot touch Data — until the realignment has fully settled.  Renumber
    // then runs on the realigned cluster (its id is still valid since nothing
    // renamed it yet) and the matrices recompute against the final, realigned,
    // renumbered state.  If auto-align is off (or can't run), do the renumber +
    // matrix update immediately.
    bool deferredForRealign = false;
    if (configuration().getAutoRealignAfterMerge()
            && mergedClusterId > 1 && !realignRunning && !realignBatchActive
            && doc->clusterHasMembers(mergedClusterId)) {
        startRealignForCluster(mergedClusterId);   // RealignJob on realignQueue
        realignQueue->enqueue(new LambdaJob(       // post-merge, strictly after it
            [this]() { autoPostMerge(); },
            QStringLiteral("post-merge renumber+matrix")));
        deferredForRealign = true;
    }
    if (!deferredForRealign)
        autoPostMerge();
}

// ---------------------------------------------------------------------------
// slotAutoMerge — patch 0069
//
// Pulls the Auto-Merge settings from configuration() (patch 0068), assembles
// the candidate cluster list per the scope toggle, runs
// AutoMerge::computeProposals (template xcorr + union-find — same mechanism
// as KKE), optionally pops the preview dialog, and applies each accepted
// group via doc->groupClusters so the merge integrates with klusters'
// undo/redo.
// ---------------------------------------------------------------------------
void KlustersApp::slotAutoMerge()
{
    slotStatusMsg(tr("Auto-merge: computing proposals..."));

    AutoMerge::Settings s;
    s.algorithm          = configuration().getAutoMergeAlgorithm();
    s.medianK            = configuration().getAutoMergeMedianK();
    s.scoreThreshold     = configuration().getAutoMergeScoreThreshold();
    s.useErrorMatrix     = configuration().getAutoMergeUseErrorMatrix();
    s.errorProbThreshold = configuration().getAutoMergeErrorProbThreshold();
    s.maxShift           = configuration().getAutoMergeMaxShift();
    s.taperSamples       = configuration().getAutoMergeTaperSamples();
    s.minClusterSize     = configuration().getAutoMergeMinClusterSize();
    s.scope              = configuration().getAutoMergeScope();
    s.previewBeforeApply = configuration().getAutoMergePreviewBeforeApply();

    // Resolve scope into a candidate list.  ScopeSelected (0): palette
    // selection.  ScopeAllActive (1): every cluster id from Data
    // (computeProposals strips 0 and 1 internally).
    QList<int> candidates;
    if (s.scope == 0) {
        if (clusterPalette) candidates = clusterPalette->selectedClusters();
        if (candidates.size() < 2) {
            QMessageBox::information(this, tr("Auto-Merge"),
                tr("Select at least 2 clusters in the palette, or switch "
                   "the Auto-Merge scope to 'All active clusters' in "
                   "preferences."));
            slotStatusMsg(tr("Ready."));
            return;
        }
    } else {
        const QList<dataType> allIds = doc->data().clusterIds();
        for (dataType id : allIds) candidates.append(static_cast<int>(id));
    }

    QList<AutoMerge::MergeGroup> proposals =
        AutoMerge::computeProposals(doc, doc->data(), s, candidates, this);

    if (proposals.isEmpty()) {
        QMessageBox::information(this, tr("Auto-Merge"),
            tr("No merge groups found above threshold %1.")
                .arg(QString::number(s.scoreThreshold, 'f', 3)));
        slotStatusMsg(tr("Ready."));
        return;
    }

    if (s.previewBeforeApply) {
        proposals = AutoMerge::promptPreview(proposals, this);
        if (proposals.isEmpty()) {
            slotStatusMsg(tr("Auto-merge cancelled."));
            return;
        }
    }

    KlustersView* view = activeView();
    if (!view) { slotStatusMsg(tr("Ready.")); return; }

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    int applied = 0;
    for (const AutoMerge::MergeGroup& g : proposals) {
        if (g.clusters.size() < 2) continue;
        doc->groupClusters(g.clusters, *view);
        ++applied;
    }
    QApplication::restoreOverrideCursor();

    slotStatusMsg(tr("Auto-merge: %1 group(s) applied.").arg(applied));
    if (applied > 0) autoPostMerge();
    if (clusterPalette) clusterPalette->setFocusToList();
}

// ---------------------------------------------------------------------------
// autoPostMerge — run the configured post-merge automation once a merge
// operation (interactive Group Clusters or batch Auto-Merge) has completed.
// Renumbering and matrix recomputation are each opt-in (Preferences >
// Refinement, default off), so this is a no-op unless the user enabled them.
// Order matters: renumber first so the matrices recompute against the final
// cluster IDs.  slotUpdateErrorMatrix refreshes the error / template / residual
// matrices and is internally a no-op when no matrix view is open.
// ---------------------------------------------------------------------------
void KlustersApp::autoPostMerge()
{
    // A merge always reduces the cluster set, so renumber is appropriate.
    autoPostClusterEdit(true);
}

void KlustersApp::maybeLockEditsForConsolidation(){
    if(editConsolidationLock) return;
    KlustersView* view = activeView();
    if(!view || !view->errorMatrixConsolidating()) return;
    editConsolidationLock = true;
    // Reuse the realign lock's action set to grey out every editing command for
    // the duration of the consolidation.  A post-edit consolidation and a realign
    // are mutually exclusive (autoPostClusterEdit takes the realign branch OR the
    // matrix branch that calls this), so realignRunning is false here and the
    // noRealignState restore in pollConsolidationUnlock() is safe.
    slotStateChanged(QStringLiteral("realignState"));
    if(!consolidationPollTimer){
        consolidationPollTimer = new QTimer(this);
        consolidationPollTimer->setInterval(100);
        connect(consolidationPollTimer, &QTimer::timeout,
                this, &KlustersApp::pollConsolidationUnlock);
    }
    consolidationPollTimer->start();
    slotStatusMsg(tr("Consolidating previous edit \u2014 editing locked\u2026"));
}

void KlustersApp::pollConsolidationUnlock(){
    KlustersView* view = activeView();
    if(view && view->errorMatrixConsolidating()) return;   // still computing
    if(consolidationPollTimer) consolidationPollTimer->stop();
    if(!editConsolidationLock) return;
    editConsolidationLock = false;
    // Only restore if a real realign has not meanwhile taken the lock.
    if(!realignRunning && !realignBatchActive)
        slotStateChanged(QStringLiteral("noRealignState"));
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::autoPostClusterEdit(bool clusterSetChanged)
{
    // Drain the fibers this operation created/modified.  Always clear the set (so it
    // never leaks across turns); realign only when the option is on and the realign
    // lane is idle.  The realign-state lock means no edit can land while a job runs,
    // so a busy lane here is only a manual realign in flight — fall back to inline.
    const QList<int> dirty = doc->takeModifiedFibers();
    if (configuration().getAutoRealignAfterMerge()
            && !realignRunning && !realignBatchActive) {
        QList<int> fibers;
        for (int f : dirty)
            if (f > 1 && doc->clusterHasMembers(f))
                fibers.append(f);
        if (!fibers.isEmpty()) {
            // Async: lock change-initiating actions, show progress, realign the
            // modified fibers as one batch, then renumber + matrix on batch finish.
            startPostOpRealign(fibers, clusterSetChanged);
            return;
        }
    }
    // No realign (option off, lane busy, or nothing realignable): renumber first
    // (only when the set actually changed and the option is on), then recompute
    // matrices against the final ids.
    if (clusterSetChanged && configuration().getAutoRenumberAfterMerge())
        doc->renumberClusters();
    if (configuration().getAutoUpdateMatricesAfterMerge()){
        slotUpdateErrorMatrix();
        // The matrix recompute runs on a background thread; lock further edits
        // until it finishes so the next edit's stopAllViewThreads() cannot stall
        // on the in-flight (un-interruptible) GPU kernel.
        maybeLockEditsForConsolidation();
    }
    // Last step (mirrors the batch-finish path): land on the produced fibers.  For
    // atom-only ops the pending set is empty and selection was handled at op time
    // via hierarchyChildrenCreated.
    applyPendingFiberSelection();
}

void KlustersApp::scheduleAutoPostClusterEdit(bool clusterSetChanged)
{
    // OR-accumulate the "set changed" flag across every signal that fires for
    // this operation, then schedule a single deferred run.
    autoPostEditSetChanged = autoPostEditSetChanged || clusterSetChanged;
    if (autoPostEditPending) return;          // already scheduled this turn
    autoPostEditPending = true;

    // Defer past the current event-loop turn: the triggering mutation
    // (createNewClusters / deleteClusters / …) emits its signal mid-operation,
    // so running renumber/matrix inline would re-enter the doc.  By the time
    // this fires the mutation has completed.  renumberClusters() emits only
    // renumber() — which is deliberately NOT hooked here — so it cannot recurse.
    QTimer::singleShot(0, this, [this]() {
        autoPostEditPending = false;
        const bool setChanged = autoPostEditSetChanged;
        autoPostEditSetChanged = false;
        autoPostClusterEdit(setChanged);
    });
}

// ---------------------------------------------------------------------------
// moveSelectedClustersToReservedId
//
// Shared body for slotMoveClustersToNoise / slotMoveClustersToArtefact.
// Both slots delete the selected clusters into a reserved ID (1 = noise,
// 0 = artefact); everything else (status messages, traceView state
// transitions, palette refocus) is identical.
// ---------------------------------------------------------------------------
void KlustersApp::moveSelectedClustersToReservedId(const QList<int>& selectedClusters,
                                                    int reservedId,
                                                    const QString& busyMessage)
{
    slotStatusMsg(busyMessage);
    KlustersView* view = activeView();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    doc->deleteClusters(selectedClusters, *view, reservedId);

    // Update the browsing possibility of the traceView
    if (view->containsTraceView() && view->clusters().size() != 0) {
        slotStateChanged("traceViewBrowsingState");
    } else {
        slotStateChanged("noTraceViewBrowsingState");
    }

    QApplication::restoreOverrideCursor();
    slotStatusMsg(tr("Ready."));
    // Restore focus to the cluster palette rather than the ClusterView:
    // deleteClusters() already auto-selects the next cluster in the list
    // (klustersdoc.cpp via selectItems()), so the user's natural next
    // action is to continue arrow-key navigation — which requires focus
    // on the palette's iconView, not the 2D scatter.  focusClusterView()
    // would steal that focus and silently break arrow-key nav after a
    // delete operation.
    if (clusterPalette) clusterPalette->setFocusToList();
}

void KlustersApp::slotMoveClustersToNoise(QList<int> selectedClusters)
{
    moveSelectedClustersToReservedId(selectedClusters, /*noise=*/1,
                                      tr("Delete &noisy cluster(s)..."));
}

void KlustersApp::slotMoveClustersToArtefact(QList<int> selectedClusters)
{
    moveSelectedClustersToReservedId(selectedClusters, /*artefact=*/0,
                                      tr("Delete &artifact cluster(s)..."));
}

// ---------------------------------------------------------------------------
// slotPurgeSmallClusters
//
// Move every cluster whose spike count is strictly below a user-entered
// threshold N into the noise cluster (reserved id 1).  The reserved clusters
// 0 (artefact) and 1 (noise) are never purged.  Asks for N (pre-filled with
// the last value used this session), then a Yes/No confirmation that defaults
// to Yes, before performing the move through the same path as "Delete Noisy
// Cluster(s)" (so curation logging and a single undo step come for free).
// ---------------------------------------------------------------------------
void KlustersApp::slotPurgeSmallClusters()
{
    if (!mPurgeSmallClusters->isEnabled()) return;   // mirror cluster-op guard
    if (!activeView()) return;

    bool ok = false;
    const int n = QInputDialog::getInt(
        this, tr("Purge Small Clusters"),
        tr("Move every cluster with fewer than this many spikes\n"
           "into the noise cluster (1):"),
        purgeSmallClusterThreshold, 1, 1000000000, 1, &ok);
    if (!ok) return;                                 // user cancelled
    purgeSmallClusterThreshold = n;

    // Collect clusters below N, skipping the reserved artefact(0)/noise(1).
    auto& d = doc->data();
    QList<int> small;
    const auto ids = d.clusterIds();
    for (const auto id : ids) {
        if (id <= 1) continue;
        if (d.nbOfSpikes(id) < n)
            small.append(static_cast<int>(id));
    }

    if (small.isEmpty()) {
        slotStatusMsg(tr("Purge: no clusters smaller than %1 spikes.").arg(n));
        return;
    }
    std::sort(small.begin(), small.end());

    QMessageBox box(QMessageBox::Question, tr("Purge Small Clusters"),
        tr("Move %1 cluster(s) with fewer than %2 spikes into the noise cluster?")
            .arg(small.size()).arg(n),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::Yes);          // Enter / focus = Yes
    if (box.exec() != QMessageBox::Yes) return;

    moveSelectedClustersToReservedId(small, /*noise=*/1,
        tr("Purging %1 small cluster(s) into noise...").arg(small.size()));
}

// ---------------------------------------------------------------------------
// slotStripFeatureOutliers  --  bulk feature-space artefact removal
//
// For every real cluster (id >= 2), flag any spike lying more than kSigma
// standard deviations from that cluster's mean in ANY single feature dimension,
// and move all flagged spikes into the artefact cluster (0).
//
// The criterion is per-DIMENSION, not Mahalanobis, and that is deliberate: in
// D ~ 24 feature dimensions the typical Mahalanobis distance of an ordinary in-
// cluster spike is ~sqrt(D) ~ 4.9, so a raw "5 sigma" Mahalanobis cut would flag
// roughly HALF of every cluster -- catastrophic for a destructive op.  The
// per-dimension max-z cut keeps the familiar one-dimensional "5 sigma" tail
// (P ~ 6e-7 per dim, ~1.4e-5 across 24 dims), so only genuine outliers are
// caught and, in practice, only the few largest clusters contribute any.
// (kSigma is a named constant here; promoting it to a preference later is easy.)
//
// Detection is a non-mutating two-pass scan (per-dimension mean/variance, then
// max-z) so the user is shown the exact spike count and confirms before anything
// moves; the moves themselves go through the tested, undoable
// moveSpikeSubsetToCluster (one undoable step per contributing source cluster).
// ---------------------------------------------------------------------------
void KlustersApp::slotStripFeatureOutliers()
{
    if (!activeView()) return;
    Data& d = doc->data();

    // nbOfDimensionsTotal() counts the timestamp as its last column, so the fet
    // dimensions are 1 .. D with D = total - 1.
    const int D = d.nbOfDimensionsTotal() - 1;
    if (D < 1) {
        slotStatusMsg(tr("Strip outliers: no feature dimensions available."));
        return;
    }
    constexpr double kSigma = 5.0;

    // Detection pass (no mutation).  Collect, per cluster, the 0-based .spk file
    // indices of its outlier spikes.  moveSpikeSubsetToCluster expects 0-based
    // .spk indices, and (.spk index) == (1-based feature row) - 1.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QMap<int, QVector<int>> outliersByCluster;
    long long totalOutliers = 0;
    const auto ids = d.clusterIds();
    for (const auto idRaw : ids) {
        const int c = static_cast<int>(idRaw);
        if (c < 2) continue;                        // skip artefact(0) / noise(1)

        SortableTable pos;
        if (!d.spikePositions(c, pos)) continue;
        const int n = static_cast<int>(pos.nbOfColumns());
        if (n < 2) continue;                        // need >= 2 spikes for a variance

        // Pass 1: per-dimension mean + variance via streaming sum / sum-of-squares.
        std::vector<double> sum(static_cast<size_t>(D), 0.0);
        std::vector<double> sumsq(static_cast<size_t>(D), 0.0);
        for (int i = 1; i <= n; ++i) {
            const dataType row = pos(1, i);         // 1-based feature row
            for (int dim = 0; dim < D; ++dim) {
                const double v = static_cast<double>(d.featureValue(row, dim + 1));
                sum[dim]   += v;
                sumsq[dim] += v * v;
            }
        }
        std::vector<double> mean(static_cast<size_t>(D), 0.0);
        std::vector<double> sd(static_cast<size_t>(D), 0.0);
        const double invN = 1.0 / static_cast<double>(n);
        for (int dim = 0; dim < D; ++dim) {
            mean[dim] = sum[dim] * invN;
            const double var = sumsq[dim] * invN - mean[dim] * mean[dim];
            sd[dim] = (var > 0.0) ? std::sqrt(var) : 0.0;
        }

        // Pass 2: flag spikes exceeding kSigma in any non-degenerate dimension.
        QVector<int> flagged;
        for (int i = 1; i <= n; ++i) {
            const dataType row = pos(1, i);
            bool outlier = false;
            for (int dim = 0; dim < D; ++dim) {
                if (sd[dim] <= 0.0) continue;       // constant dimension: nothing to exceed
                const double z = std::fabs(static_cast<double>(d.featureValue(row, dim + 1)) - mean[dim]) / sd[dim];
                if (z > kSigma) { outlier = true; break; }
            }
            if (outlier) flagged.append(static_cast<int>(row) - 1);   // -> 0-based .spk index
        }
        if (!flagged.isEmpty()) {
            totalOutliers += flagged.size();
            outliersByCluster.insert(c, flagged);
        }
    }
    QApplication::restoreOverrideCursor();

    if (totalOutliers == 0) {
        slotStatusMsg(tr("Strip outliers: no spikes beyond %1 sigma in any cluster.")
                          .arg(kSigma));
        return;
    }

    // Destructive and spans every cluster: confirm with the exact counts first,
    // defaulting to No.
    QMessageBox box(QMessageBox::Question, tr("Strip Feature Outliers"),
        tr("Move %1 spike(s) lying more than %2 sigma (in any feature dimension) "
           "from their cluster centre into the artefact cluster (0)?\n\n"
           "%3 cluster(s) contribute outliers.  This can be undone.")
            .arg(totalOutliers).arg(kSigma).arg(outliersByCluster.size()),
        QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;

    // Apply: one undoable move per contributing source cluster.  Feature rows are
    // stable spike identities, so a cluster's flagged rows stay in that cluster
    // regardless of earlier moves in this loop.
    int movedClusters = 0;
    for (auto it = outliersByCluster.constBegin(); it != outliersByCluster.constEnd(); ++it) {
        KlustersView* view = activeView();
        if (!view) break;                           // view could close mid-loop
        doc->moveSpikeSubsetToCluster(it.key(), it.value(), /*artefact=*/0, *view);
        ++movedClusters;
    }

    slotStatusMsg(tr("Stripped %1 outlier spike(s) from %2 cluster(s) into the artefact cluster.")
                      .arg(totalOutliers).arg(movedClusters));
}

// ---------------------------------------------------------------------------
// clusterSortActions
//
// The "Sort Clusters" submenu's actions, exposed so other widgets (a view's
// right-click menu) can present the same sorting options.  Returning the
// submenu's own actions keeps this in sync automatically as options are added
// or removed.
// ---------------------------------------------------------------------------
QList<QAction*> KlustersApp::clusterSortActions() const
{
    return mClusterSortMenu ? mClusterSortMenu->actions() : QList<QAction*>();
}
void KlustersApp::ensureClusterTemplates()
{

    // Re-entrancy guard: the cold build below pumps the event loop to paint its
    // progress bar, and that pump delivers posted events even though it excludes
    // user input -- a matrix thread finishing mid-build emits matrixUpdated(),
    // which lands in slotRefreshMergeRecommendations() and comes straight back
    // here.  The builder is iterating the cluster tables and writing the template
    // cache at that point, so re-entering it would corrupt both.
    if (buildingClusterTemplates) return;

    // Synchronous on purpose.  buildMissingClusterTemplates() reads the cluster
    // tables and writes the template cache, none of them locked, and an edit
    // mutates all of them -- so a worker would race both the edit and the panel
    // reading the cache.  It is affordable because it is incremental: the first
    // call sweeps the .spk once, and after that an edit only invalidates the
    // clusters it touched, so only those are re-read.
    Data& d = doc->data();

    // Show the bar on ELAPSED TIME, not on whether the cache is cold.
    //
    // Gating on cold was wrong. This is called from slotUpdateShownClusters among
    // others, so the first cluster CLICK after a session opens does the whole .spk
    // sweep -- the bar flashed there, unwatched, and every later build was "not
    // cold" and silent no matter how long it took. Time is the thing actually
    // worth reacting to: below the threshold nobody wants a flicker, above it
    // everybody wants a bar, and it does not matter which call happened to be
    // first.
    //
    // This is QProgressDialog::setMinimumDuration's idea, done by hand because the
    // bar lives in the status bar rather than a dialog.
    buildingClusterTemplates = true;

    QElapsedTimer timer;
    timer.start();
    bool shown = false;

    const int built = d.buildMissingClusterTemplates(
        [this, &timer, &shown](int done, int total) {
            if (!shown && timer.elapsed() >= 200) {
                beginSortProgress(tr("Reading waveform templates\u2026 %p%"));
                shown = true;
            }
            if (shown && total > 0)
                updateSortProgress(static_cast<int>((100LL * done) / total));
        });

    if (shown) endSortProgress();
    buildingClusterTemplates = false;

    // Only speak up when work was actually done; this runs on every selection
    // change, and "Read waveform templates for 0 clusters" on each click would be
    // noise.
    if (built > 0)
        slotStatusMsg(tr("Read waveform templates for %1 clusters.").arg(built));
}

void KlustersApp::slotRefreshMergeRecommendations()
{
    if (!recommendView) return;
    if (!recommendPanel || !recommendPanel->isVisible()) return;

    // Templates are what the overlap is scored from, and they are only rebuilt
    // for clusters an edit invalidated, so asking on every refresh is cheap.
    ensureClusterTemplates();

    // The matrices recompute on worker threads, so the panel must also wake when
    // a result LANDS, not only when an edit is requested -- otherwise a refresh
    // that arrives mid-compute reads a stale matrix, reports it, and never comes
    // back.  Both views emit matrixUpdated() on accepting a fresh result;
    // Qt::UniqueConnection keeps repeated refreshes from stacking duplicates.
    if (KlustersView* v = activeView()) {
        if (ErrorMatrixView* emv = v->findChild<ErrorMatrixView*>())
            connect(emv, &ErrorMatrixView::matrixUpdated,
                    this, &KlustersApp::slotRefreshMergeRecommendations,
                    static_cast<Qt::ConnectionType>(Qt::AutoConnection | Qt::UniqueConnection));
        if (ResidualMatrixView* rmv = v->findChild<ResidualMatrixView*>())
            connect(rmv, &ResidualMatrixView::matrixUpdated,
                    this, &KlustersApp::slotRefreshMergeRecommendations,
                    static_cast<Qt::ConnectionType>(Qt::AutoConnection | Qt::UniqueConnection));
    }

    // Restrict to the palette selection: the question a curator is asking with a
    // cluster selected is "what should merge with THIS", not "what is best in the
    // session".  Empty selection falls back to the session-wide list.
    recommendView->refreshFrom(activeView(),
                               doc ? &doc->data() : nullptr,
                               clusterPalette ? clusterPalette->selectedClusters()
                                              : QList<int>());
}
void KlustersApp::slotRecommendationActivated(const QList<int>& clusters)
{
    if (!clusterPalette || clusters.isEmpty()) return;
    clusterPalette->selectItems(clusters);
    slotStatusMsg(tr("Selected clusters %1 and %2 \u2014 merge them from the palette.")
                      .arg(clusters.first()).arg(clusters.last()));
}
void KlustersApp::slotImmediateSelection(){
    //Disable the update action (see the klustersui.rc file)
    slotStateChanged("immediateSelectionState");
    clusterPalette->setImmediateMode();
    clusterPalette->updateClusters();
}
void KlustersApp::slotDelaySelection(){
    //Enable the update action (see the klustersui.rc file)
    slotStateChanged("delaySelectionState");
    clusterPalette->setDelayMode();
}

void KlustersApp::slotTabChange(int index){
    if (!tabsParent) return;  // guard against call during teardown
    QWidget *widget = tabsParent->widget(index);
    DockArea *area = dynamic_cast<DockArea*>(widget);
    if(area) {

        KlustersView* activeView = qobject_cast<KlustersView *>(area);
        if(activeView) {
            //Update the content of the view contains in active display.
            activeView->updateViewContents();

            isInit = true; //prevent the spine boxes or the lineedit and the editline to trigger during initialisation

            //The select time tool is useful only if both a clusterView and a traceView are present
            if(activeView->containsClusterView() && activeView->containsTraceView()) {
                slotStateChanged("traceViewClusterViewState");
            }

            if(activeView->containsClusterView()){
                //Update the dimension spine boxes
                int x =  activeView->abscissaDimension();
                int y =  activeView->ordinateDimension();
                dimensionX->setValue(x);
                dimensionY->setValue(y);
                slotStateChanged("clusterViewState");
                dimensionXAction->setVisible(true);
                dimensionYAction->setVisible(true);
                featureXLabelAction->setVisible(true);
                autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
                autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);
            }
            else{
                slotStateChanged("noClusterViewState");
                dimensionXAction->setVisible(false);
                dimensionYAction->setVisible(false);
                featureXLabelAction->setVisible(false);
                // N feat spinbox stays visible whenever auto-select is on,
                // even when no scatter plot sub-view is active.
                autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
                autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);
            }

            if(activeView->containsWaveformView()){
                slotStateChanged("waveformsViewState");
                if(activeView->isOverLayPresentation()) overlayPresentation->setChecked(true);
                else overlayPresentation->setChecked(false);
                if(activeView->isMeanPresentation()) meanPresentation->setChecked(true);
                else meanPresentation->setChecked(false);

                if(activeView->isInTimeFrameMode()){
                    timeFrameMode->setChecked(true);
                    timeWindow = activeView->timeFrameWidth();
                    startTime =  activeView->timeFrameStart();
                    start->setValue(startTime);
                    start->setSingleStep(timeWindow);
                    duration->setText(QString::fromLatin1("%1").arg(timeWindow));
                    durationAction->setVisible(true);
                    durationLabelAction->setVisible(true);
                    startAction->setVisible(true);
                    startLabelAction->setVisible(true);
                    spikesTodisplayAction->setVisible(false);
                    spikesTodisplayLabelAction->setVisible(false);
                }
                else{
                    timeFrameMode->setChecked(false);
                    durationAction->setVisible(false);
                    durationLabelAction->setVisible(false);
                    startAction->setVisible(false);
                    startLabelAction->setVisible(false);
                    spikesTodisplay->setValue(activeView->displayedNbSpikes());
                    spikesTodisplayAction->setVisible(true);
                    spikesTodisplayLabelAction->setVisible(true);
                }
            }
            else{
                timeFrameMode->setChecked(false);
                overlayPresentation->setChecked(false);
                meanPresentation->setChecked(false);
                slotStateChanged("noWaveformsViewState");
                durationAction->setVisible(false);
                durationLabelAction->setVisible(false);
                startAction->setVisible(false);
                startLabelAction->setVisible(false);
                spikesTodisplayAction->setVisible(false);
                spikesTodisplayLabelAction->setVisible(false);
            }

            if(activeView->containsCorrelationView()){
                slotStateChanged("correlationViewState");
                Data::ScaleMode correlationScale = activeView->scaleMode();
                switch(correlationScale){
                case Data::RAW :
                    noScale->setChecked(true);
                    break;
                case Data::MAX :
                    scaleByMax->setChecked(true);
                    break;
                case Data::SHOULDER :
                    scaleByShouler->setChecked(true);
                    break;
                }

                //Update the lineEdit
                correlogramTimeFrame = activeView->correlationTimeFrameWidth();
                binSize = activeView->sizeOfBin();
                correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(correlogramTimeFrame / 2));
                binSizeBox->setText(QString::fromLatin1("%1").arg(binSize));
                correlogramsHalfDurationAction->setVisible(true);
                correlogramsHalfDurationLabelAction->setVisible(true);
                binSizeBoxAction->setVisible(true);
                binSizeLabelAction->setVisible(true);
                //Update the shoulder line menu entry
                shoulderLine->setChecked(activeView->isShoulderLine());
            }
            else{
                slotStateChanged("noCorrelationViewState");
                correlogramsHalfDurationAction->setVisible(false);
                correlogramsHalfDurationLabelAction->setVisible(false);
                binSizeBoxAction->setVisible(false);
                binSizeLabelAction->setVisible(false);
            }

            if(activeView->containsErrorMatrixView()) {
                slotStateChanged("errorMatrixViewState");
            } else {
                slotStateChanged("noErrorMatrixViewState");
            }

            if(activeView->containsTraceView()){
                showHideLabels->setChecked(activeView->getLabelStatus());
                activeView->updateClustersProvider();
                slotStateChanged("traceViewState");

                //Update the browsing possibility of the traceView
                if(!activeView->clusters().isEmpty()) {
                    slotStateChanged("traceViewBrowsingState");
                } else {
                    slotStateChanged("noTraceViewBrowsingState");
                }
            }
            else{

                slotStateChanged("noTraceViewState");
            }

            isInit = false; //now a change in a spine box  or the lineedit
            //will trigger an update of the display

            //Update the cluster palette
            clusterPalette->selectItems(activeView->clusters());


            //Check if a reclustering process is working in order to correctly set up the menus
            if(processFinished){
                slotStateChanged("noReclusterState");
                // If realignment is running, re-apply its locks over the now-restored state.
                if (realignRunning)
                    slotStateChanged("realignState");
                updateUndoRedoDisplay();
            } else {
                slotStateChanged("reclusterState");
            }
        }
    } else {// a ProcessWidget (the recluster output tab)
        // Do NOT hide toolbar fields — leave Features, Waveforms, Bin size, Duration
        // in whatever state they were when the user last viewed a real display tab.
        // Only update menu state and cluster palette selection.
        timeFrameMode->setChecked(false);
        overlayPresentation->setChecked(false);
        meanPresentation->setChecked(false);

        //Update the palette of clusters
        if(!processFinished) {
            clusterPalette->selectItems(clustersToRecluster);
        } else {
            QList<int> emptyList;
            clusterPalette->selectItems(emptyList);
        }
        slotStateChanged(QStringLiteral("reclusterViewState"));
    }

    // Each display has its own matrices, so the recommendations belong to the
    // display being switched to, not the one left behind.
    slotRefreshMergeRecommendations();
}

void KlustersApp::slotUpdateDimensionX(int dimensionXValue){
    if(!isInit)updateDimensions(dimensionXValue,dimensionY->value());
}

void KlustersApp::slotUpdateAutoNFeatures(int n){
    if(isInit) return;
    autoSelectNFeatures = n;
    configuration().setAutoSelectNFeatures(n);
    configuration().write();
    // Keep the preferences spinbox in sync if the dialog is open.
    if(prefDialog) prefDialog->syncAutoNFeatures(n);
}

void KlustersApp::slotUpdateIncludeTimeFeature(bool on){
    if(isInit) return;
    configuration().setIncludeTimeInAutoSelect(on);
    configuration().write();
}

void KlustersApp::slotUpdateRealignTopChan(int n){
    if(isInit) return;
    Q_UNUSED(n);  // current value is read from the spinbox by buildRealignArgs()
    realignArgs = buildRealignArgs();
}

// Assemble realign args from structured prefs + saved post-alignment mode.
// Gate: when the Realign top-ch spinbox is non-zero, emit --topchannels and
// suppress the PCA/RMS mode (default plain xcorr), because neither post-pass
// honours the top-channel mask; otherwise honour the saved radio-group mode.
QString KlustersApp::buildRealignArgs(){
    QString a = QString("--threshold %1 --iterations %2")
        .arg(configuration().getRealignThreshold(), 0, 'f', 2)
        .arg(configuration().getRealignIterations());
    if (configuration().getRealignMaxShift() > 0)
        a += QString(" --maxshift %1").arg(configuration().getRealignMaxShift());
    const int topCh = realignTopChanSpinBox ? realignTopChanSpinBox->value() : 0;
    if (topCh > 0) {
        a += QString(" --topchannels %1").arg(topCh);
    } else {
        switch (configuration().getRealignMode()) {
        case 1:  a += QStringLiteral(" --pca-refine");   break;
        case 2:  a += QStringLiteral(" --recenter-rms"); break;
        default: break;
        }
    }
    return a;
}

void KlustersApp::slotUpdateDimensionY(int dimensionYValue){
    if(!isInit)updateDimensions(dimensionX->value(),dimensionYValue);
}

void KlustersApp::updateDimensions(int dimensionXValue, int dimensionYValue){
    activeView()->updateDimensions(dimensionXValue,dimensionYValue);
    activeView()->showAllWidgets();
}

void KlustersApp::slotUpdateDuration(){
    if(!isInit){
        timeWindow =  (duration->displayText()).toLong();
        start->setSingleStep(timeWindow);
        activeView()->updateTimeFrame(startTime,timeWindow);
    }
}

void KlustersApp::slotTimeFrameMode(){
    if(!isInit){
        if(timeFrameMode->isChecked()){
            timeWindow = activeView()->timeFrameWidth();
            startTime =  activeView()->timeFrameStart();
            start->setValue(startTime);
            start->setSingleStep(timeWindow);
            duration->setText(QString::fromLatin1("%1").arg(timeWindow));
            durationAction->setVisible(true);
            durationLabelAction->setVisible(true);
            startAction->setVisible(true);
            startLabelAction->setVisible(true);
            spikesTodisplayAction->setVisible(false);
            spikesTodisplayLabelAction->setVisible(false);
            activeView()->setTimeFrameMode();
        }
        else{
            spikesTodisplay->setValue(activeView()->displayedNbSpikes());
            spikesTodisplayAction->setVisible(true);
            spikesTodisplayLabelAction->setVisible(true);
            durationAction->setVisible(false);
            durationLabelAction->setVisible(false);
            startAction->setVisible(false);
            startLabelAction->setVisible(false);
            activeView()->setSampleMode();
        }
    }
}

void KlustersApp::slotClusterInformationModified(){
    doc->clusterInformationModified();
}

void KlustersApp::slotShowOverviewForPalette()
{
    // Switch the tab widget to the first Overview Display tab, then return
    // keyboard focus to the cluster palette so arrow-key navigation continues
    // without the user needing to click the palette again.
    if (!tabsParent)
        return;

    for (int i = 0; i < tabsParent->count(); ++i) {
        if (tabsParent->tabText(i).contains(tr("Overview"), Qt::CaseInsensitive)) {
            tabsParent->setCurrentIndex(i);
            break;
        }
    }

    // Return focus to the palette widget itself (not the outer ClusterPalette
    // QWidget, which just holds the layout — setFocus() on it does nothing).
    if (clusterPalette)
        clusterPalette->setFocusToList();
}

void KlustersApp::resetState(){
    isInit = true; //prevent the spine boxes or the lineedit and the editline to trigger during initialisation
    timeFrameMode->setChecked(false);
    durationAction->setVisible(false);
    durationLabelAction->setVisible(false);
    startAction->setVisible(false);
    startLabelAction->setVisible(false);
    dimensionXAction->setVisible(false);
    dimensionYAction->setVisible(false);
    featureXLabelAction->setVisible(false);
    autoNFeaturesLabelAction->setVisible(false);
    autoNFeaturesSpinBoxAction->setVisible(false);
    spikesTodisplayAction->setVisible(false);
    spikesTodisplayLabelAction->setVisible(false);
    correlogramsHalfDurationAction->setVisible(false);
    correlogramsHalfDurationLabelAction->setVisible(false);
    binSizeBoxAction->setVisible(false);
    binSizeLabelAction->setVisible(false);
    shoulderLine->setChecked(true);
    binSize = DEFAULT_BIN_SIZE.toInt();
    correlogramTimeFrame = INITIAL_CORRELOGRAMS_HALF_TIME_FRAME.toInt() * 2 + 1;
    startTime = 0;
    timeWindow = INITIAL_WAVEFORM_TIME_WINDOW.toLong();
    showHideLabels->setChecked(false);

    isInit = false; //now a change in a spine box  or the lineedit
    //will trigger an update of the view contain in the display.

    displayCount = 0;
    errorMatrixExists = false;
    filePath.clear();

    //Disable some actions when no document is open (see the klustersui.rc file)
    slotStateChanged("initState");
    //A traceView is possible only if the variables it needs are available (provided in the new parameter file) and
    //the .dat file exists. Therefore disable the menu entry by default.
    slotStateChanged("noTraceDisplayState");

    setWindowTitle(QString());

    //If the a setting dialog exists (has already be open once), disable the settings for the channels.
    if(prefDialog != nullptr)
        prefDialog->enableChannelSettings(false);
}

void KlustersApp::slotUpdateCorrelogramsHalfDuration(){
    if(!isInit){
        const int halfTimeFrame = (correlogramsHalfDuration->displayText()).toInt();
        // Symmetric with slotUpdateBinSize: refuse a non-positive Duration and
        // put the last good value back.  The validator's minimum is 1, but an
        // empty box still reads back as 0, and setText() (used below and by
        // slotUpdateBinSize) does not run the validator at all.
        if(halfTimeFrame <= 0){
            correlogramsHalfDuration->setText(
                QString::fromLatin1("%1").arg(std::max(1, correlogramTimeFrame / 2)));
            return;
        }
        if(halfTimeFrame > (maximumTime - binSize) / 2){
            correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg((correlogramTimeFrame - binSize) / 2));
            return;
        }

        if(binSize <= 0) return;   // nothing sensible to snap to; see slotUpdateBinSize
        float x = (2*static_cast<float>(halfTimeFrame)
                   /static_cast<float>(binSize)-1) * 0.5;
        int k = static_cast<int>(x + 0.5);
        if(k < 0) k = 0;

        // See slotUpdateBinSize: keep the written half-duration at or above the
        // box's own minimum, which setText() would otherwise bypass.
        if((2*k+1)*binSize < 2) k = 1;
        correlogramTimeFrame = (2*k+1)*binSize;
        if(static_cast<float>(k) != x){
            correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(std::max(1, correlogramTimeFrame / 2)));
        }
        activeView()->updateBinSizeAndTimeFrame(binSize,correlogramTimeFrame);
    }
}

void KlustersApp::slotUpdateBinSize(){
    if(!isInit){
        const int sizeOfBin = (binSizeBox->displayText()).toInt();
        // A zero bin size divides by zero all the way down (nbBins =
        // timeWindow / binSize, and the correlogram's own hover readout), which
        // is a SIGFPE rather than a misdraw.  Refuse it and put the last good
        // value back.
        if(sizeOfBin <= 0){
            binSizeBox->setText(QString::fromLatin1("%1").arg(binSize));
            return;
        }
        binSize = sizeOfBin;

        // correlogramTimeFrame (2 * correlogramsHalfDuration) has to be
        // (2k + 1) * binSize.  Applied here, once, rather than from inside the
        // validator on every keystroke.
        const int halfTimeFrame = (correlogramsHalfDuration->displayText()).toInt();
        const float x = (2*static_cast<float>(halfTimeFrame)
                         /static_cast<float>(binSize)-1)*0.5;
        int k = static_cast<int>(x + 0.5);
        if(k < 0) k = 0;
        // (2k+1)*binSize is an ODD multiple, so halving it truncates: binSize 1
        // with k 0 would write 0 into a box whose minimum is 1.  k >= 1 there
        // keeps the written value at or above the minimum (and a 1-bin
        // correlogram was never useful anyway).
        if((2*k+1) * binSize < 2) k = 1;
        correlogramTimeFrame = (2*k+1) * binSize;
        if(static_cast<float>(k) != x)
            correlogramsHalfDuration->setText(
                QString::fromLatin1("%1").arg(std::max(1, correlogramTimeFrame / 2)));

        activeView()->updateBinSizeAndTimeFrame(binSize,correlogramTimeFrame);
    }
}

void KlustersApp::slotUpdateParameterBar(){  
    durationAction->setVisible(false);
    durationLabelAction->setVisible(false);
    startAction->setVisible(false);
    startLabelAction->setVisible(false);
    dimensionXAction->setVisible(false);
    dimensionYAction->setVisible(false);
    featureXLabelAction->setVisible(false);
    autoNFeaturesLabelAction->setVisible(false);
    autoNFeaturesSpinBoxAction->setVisible(false);
    spikesTodisplayAction->setVisible(false);
    spikesTodisplayLabelAction->setVisible(false);
    correlogramsHalfDurationAction->setVisible(false);
    correlogramsHalfDurationLabelAction->setVisible(false);
    binSizeBoxAction->setVisible(false);
    binSizeLabelAction->setVisible(false);

    if(mainDock != nullptr){
        KlustersView* currentView = activeView();

        if(currentView->containsClusterView()){
            dimensionXAction->setVisible(true);
            dimensionYAction->setVisible(true);
            featureXLabelAction->setVisible(true);
            autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
            autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);
        }

        if(currentView->containsWaveformView()){
            if(currentView->isInTimeFrameMode()){
                durationAction->setVisible(true);
                durationLabelAction->setVisible(true);
                startAction->setVisible(true);
                startLabelAction->setVisible(true);
            }
            else{
                spikesTodisplayAction->setVisible(true);
                spikesTodisplayLabelAction->setVisible(true);
            }
        }

        if(currentView->containsCorrelationView()){
            correlogramsHalfDurationAction->setVisible(true);
            correlogramsHalfDurationLabelAction->setVisible(true);
            binSizeBoxAction->setVisible(true);
            binSizeLabelAction->setVisible(true);
        }
    }
}

void KlustersApp::slotUpdateErrorMatrix(){
    KlustersView* view = activeView();
    if (!view) return;
    view->updateErrorMatrix();
    // Always emit the template-matrix update.  Qt auto-disconnects the
    // computeTemplateMatrix → TemplateMatrixView::updateMatrixContents
    // wiring when the template view is destroyed, so emitting with no
    // listener is a no-op.  Gating on isThereATemplateMatrixView() was
    // fragile: the flag is cleared in several teardown paths (notably
    // errorMatrixDockClosed) that don't actually destroy the template
    // dock, leaving a visible template view orphaned from the U key.
    view->updateTemplateMatrix();
    // Same rationale for the residual matrix: emit unconditionally, Qt makes
    // it a no-op when no ResidualMatrixView is open.
    view->updateResidualMatrix();
    // Likewise the drift matrix.
    view->updateDriftMatrix();
}

// ---------------------------------------------------------------------------
// Last-interacted matrix-view tracking
//
// Each matrix view (ErrorMatrixView, TemplateMatrixView) emits
// viewInteracted() from its mouseReleaseEvent.  We connect those signals
// at dock-creation time (in the ERROR_MATRIX / TEMPLATE_MATRIX cases of
// setConnections) so subsequent clicks land here, updating
// lastMatrixUsed.  Combined with the same field being set at dock
// creation, this implements the "last created or last clicked" semantics
// the Shift+S reorder uses to disambiguate when both matrices exist.
// ---------------------------------------------------------------------------
void KlustersApp::slotErrorMatrixInteracted()
{
    lastMatrixUsed = MatrixKind::ERROR_MATRIX_KIND;
}

void KlustersApp::slotTemplateMatrixInteracted()
{
    lastMatrixUsed = MatrixKind::TEMPLATE_MATRIX_KIND;
}

void KlustersApp::slotResidualMatrixInteracted()
{
    lastMatrixUsed = MatrixKind::RESIDUAL_MATRIX_KIND;
}

// ---------------------------------------------------------------------------
// slotNewResidualMatrix — add a ResidualMatrixView dock to the active display.
// Mirrors the auto-show path for the error/template matrices but is user-
// initiated from the Actions menu, so a residual matrix can be opened on
// demand (it is not part of the default auto-show layout).
// ---------------------------------------------------------------------------
void KlustersApp::slotNewResidualMatrix()
{
    if (!activeView()) return;
    if (activeView()->containsResidualMatrixView()) {
        slotStatusMsg(tr("This display already contains a residual matrix."));
        return;
    }
    widgetAddToDisplay(KlustersView::RESIDUAL_MATRIX);
    activeView()->updateResidualMatrix();
}

// slotNewDriftMatrix — add a DriftMatrixView dock to the active display.
void KlustersApp::slotNewDriftMatrix()
{
    if (!activeView()) return;
    if (activeView()->containsDriftMatrixView()) {
        slotStatusMsg(tr("This display already contains a drift matrix."));
        return;
    }
    widgetAddToDisplay(KlustersView::DRIFT_MATRIX);
    activeView()->updateDriftMatrix();
}
void KlustersApp::slotSelectAll(){
    //Trigger the action only if the active display does not contain a ProcessWidget
    if(!doesActiveDisplayContainProcessWidget()){
        doc->showAllClustersExcept(QList<int>());
    }
}

void KlustersApp::slotSelectAllWO01(){
    //Trigger the action only if the active display does not contain a ProcessWidget
    if(!doesActiveDisplayContainProcessWidget()){
        QList<int> clustersToHide;
        clustersToHide.append(0);
        clustersToHide.append(1);
        doc->showAllClustersExcept(clustersToHide);
    }
}


void KlustersApp::updateUndoRedoDisplay(){
    if(currentNbUndo > 0) {
        slotStateChanged("undoState");
    }
    else{

        slotStateChanged("emptyUndoState");
    }
    if(currentNbRedo == 0) {
        slotStateChanged("emptyRedoState");
    }
}

void KlustersApp::widgetAddToDisplay(KlustersView::DisplayType displayType){
    KlustersView* view = activeView();
    bool newWidgetType = view->addView(displayType,backgroundColor,statusBar(),displayTimeInterval,waveformsGain,channelPositions);

    isInit = true; //prevent the spine boxes or the lineedit and the editline to trigger during initialisation.

    if(newWidgetType)
        switch(displayType){
        case KlustersView::CLUSTERS:
            //Update the dimension spine boxes with the initial values.
            dimensionX->setValue(1);
            dimensionY->setValue(2);
            slotStateChanged("clusterViewState");
            dimensionXAction->setVisible(true);
            dimensionYAction->setVisible(true);
            featureXLabelAction->setVisible(true);
            autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
            autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);
            break;
        case KlustersView::WAVEFORMS:
            slotStateChanged("waveformsViewState");
            overlayPresentation->setChecked(false);
            meanPresentation->setChecked(false);
            timeFrameMode->setChecked(false);
            durationAction->setVisible(false);
            durationLabelAction->setVisible(false);
            startAction->setVisible(false);
            startLabelAction->setVisible(false);
            spikesTodisplay->setValue(DEFAULT_NB_SPIKES_DISPLAYED);
            spikesTodisplayAction->setVisible(true);
            spikesTodisplayLabelAction->setVisible(true);
            break;
        case KlustersView::CORRELATIONS:
            slotStateChanged("correlationViewState");
            //Update the lineEdit
            scaleByMax->setChecked(true);
            correlogramTimeFrame = INITIAL_CORRELOGRAMS_HALF_TIME_FRAME.toInt() * 2 + 1;
            binSize = DEFAULT_BIN_SIZE.toInt();
            correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(correlogramTimeFrame / 2));
            binSizeBox->setText(QString::fromLatin1("%1").arg(binSize));
            correlogramsHalfDurationAction->setVisible(true);
            correlogramsHalfDurationLabelAction->setVisible(true);
            binSizeBoxAction->setVisible(true);
            binSizeLabelAction->setVisible(true);
            //Update the shoulder line menu entry
            shoulderLine->setChecked(true);
            break;
        case KlustersView::ERROR_MATRIX :
            slotStateChanged("errorMatrixViewState");
            slotStateChanged("groupingAssistantDisplayExists");
            errorMatrixExists = true;
            // Track most-recently-created matrix for Shift+S reorder.
            lastMatrixUsed = MatrixKind::ERROR_MATRIX_KIND;
            // Enable Shift+S reorder now that at least one matrix exists.
            if (mReorderClustersBySimilarity)
                mReorderClustersBySimilarity->setEnabled(true);
            // Wire the per-click last-interacted signal — qobject_cast
            // because ViewWidget is the base type used at this call site.
            if (auto* emv = qobject_cast<ErrorMatrixView*>(view)) {
                connect(emv, &ErrorMatrixView::viewInteracted,
                        this, &KlustersApp::slotErrorMatrixInteracted,
                        Qt::UniqueConnection);
            }
            break;
        case KlustersView::TEMPLATE_MATRIX:
            templateMatrixExists = true;
            // Track most-recently-created matrix for Shift+S reorder.
            lastMatrixUsed = MatrixKind::TEMPLATE_MATRIX_KIND;
            // Enable Shift+S reorder now that at least one matrix exists.
            if (mReorderClustersBySimilarity)
                mReorderClustersBySimilarity->setEnabled(true);
            if (auto* tmv = qobject_cast<TemplateMatrixView*>(view)) {
                connect(tmv, &TemplateMatrixView::viewInteracted,
                        this, &KlustersApp::slotTemplateMatrixInteracted,
                        Qt::UniqueConnection);
            }
            break;
        case KlustersView::RESIDUAL_MATRIX:
            // Track most-recently-created matrix for the residual-gated sort
            // and the Shift+S reorder's both-matrices tie-break.
            lastMatrixUsed = MatrixKind::RESIDUAL_MATRIX_KIND;
            if (mReorderClustersBySimilarity)
                mReorderClustersBySimilarity->setEnabled(true);
            if (mSortByResidualGated)
                mSortByResidualGated->setEnabled(true);
            if (auto* rmv = qobject_cast<ResidualMatrixView*>(view)) {
                connect(rmv, &ResidualMatrixView::viewInteracted,
                        this, &KlustersApp::slotResidualMatrixInteracted,
                        Qt::UniqueConnection);
            }
            break;
        case KlustersView::DRIFT_MATRIX:
            // Diagnostic only: deliberately does NOT set lastMatrixUsed and does
            // not enable the reorder / residual-gated sort, so opening it never
            // changes which matrix those actions operate on.
            break;
        case KlustersView::OVERVIEW:
            break;
        case KlustersView::GROUPING_ASSISTANT_VIEW:
            break;
        case KlustersView::TRACES:
            slotStateChanged("traceViewState");
            showHideLabels->setChecked(view->getLabelStatus());
            //Update the browsing possibility of the traceView
            if(!view->clusters().isEmpty()) {
                slotStateChanged("traceViewBrowsingState");
            }
            break;
        }

    isInit = false; //now a change in a spine box  or the lineedit
    //will trigger an update of the view contains in the acative display.

    //The select time tool is useful only if both a clusterView and a traceView are present
    if(view->containsClusterView() && view->containsTraceView()){
        slotStateChanged("traceViewClusterViewState");
    }

    // A matrix may have just appeared.  Without this the recommendations panel
    // only learned about views that existed when it last refreshed: adding an
    // error or residual matrix afterwards left it reporting them missing
    // forever, because its matrixUpdated() hookup is made during a refresh and
    // so was never armed for a view that did not yet exist.
    slotRefreshMergeRecommendations();
}

void KlustersApp::widgetRemovedFromDisplay(KlustersView::DisplayType displayType){
    switch(displayType){
    case KlustersView::CLUSTERS:
        slotStateChanged("noClusterViewState");
        dimensionXAction->setVisible(false);
        dimensionYAction->setVisible(false);
        featureXLabelAction->setVisible(false);
        autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
        autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);
        break;
    case KlustersView::WAVEFORMS:
        timeFrameMode->setChecked(false);
        overlayPresentation->setChecked(false);
        meanPresentation->setChecked(false);
        slotStateChanged("noWaveformsViewState");
        durationAction->setVisible(false);
        durationLabelAction->setVisible(false);
        startAction->setVisible(false);
        startLabelAction->setVisible(false);
        spikesTodisplayAction->setVisible(false);
        spikesTodisplayLabelAction->setVisible(false);
        break;
    case KlustersView::CORRELATIONS:
        slotStateChanged("noCorrelationViewState");
        correlogramsHalfDurationAction->setVisible(false);
        correlogramsHalfDurationLabelAction->setVisible(false);
        binSizeBoxAction->setVisible(false);
        binSizeLabelAction->setVisible(false);
        break;
    case KlustersView::ERROR_MATRIX :
        slotStateChanged("noErrorMatrixViewState");
        slotStateChanged("groupingAssistantDisplayNotExists");
        errorMatrixExists = false;
        break;
    case KlustersView::TEMPLATE_MATRIX:
        templateMatrixExists = false;
        break;
    case KlustersView::OVERVIEW:
        break;
    case KlustersView::GROUPING_ASSISTANT_VIEW:
        break;
    case KlustersView::TRACES:
        slotStateChanged("noTraceViewState");
        break;
    }
}

void KlustersApp::updateDimensionSpinBoxes(int dimensionX, int dimensionY){
    isInit = true; //prevent the spine boxes or the lineedit and the editline to trigger during initialisation

    //Update the dimension spine boxes
    this->dimensionX->setValue(dimensionX);
    this->dimensionY->setValue(dimensionY);
    this->dimensionXAction->setVisible(true);
    this->dimensionYAction->setVisible(true);
    autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
    autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);

    isInit = false; //now a change in a spine box  or the lineedit
    //will trigger an update of the view contains in the active display.
}

void KlustersApp::renameActiveDisplay(){
    bool ok;
    const int currentIndex = tabsParent->currentIndex();
    QString newLabel = QInputDialog::getText(this,tr("New Display label"),tr("Type in the new display label"),QLineEdit::Normal, tabsParent->tabText(currentIndex), &ok);
    if(!newLabel.isEmpty() && ok){
        tabsParent->setTabText(currentIndex,newLabel);
    }
}

void KlustersApp::slotShowLabels(){
    KlustersView* currentView = activeView();
    if(currentView->containsTraceView())
        currentView->showLabelsUpdate(showHideLabels->isChecked());
}

void KlustersApp::updateCorrelogramViewVariables(int binSize,int timeWindow,bool isShoulderLine, Data::ScaleMode correlationScale){
    isInit = true; //prevent the boxes or menu entry to trigger during initialisation

    //Update the boxes
    this->binSize = binSize;
    binSizeBox->setText(QString::fromLatin1("%1").arg(binSize));
    //timeWindow is the time frame use to compute the correlograms, the lineedit correlogramsHalfDuration contains half of it.
    correlogramTimeFrame = timeWindow;
    //int k = ((static_cast<float>(correlogramTimeFrame) / static_cast<float>(binSize)) - 1) / 2;
    //correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(k * binSize));
    correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(static_cast<int>(correlogramTimeFrame / 2)));
    shoulderLine->setChecked(isShoulderLine);

    switch(correlationScale){
    case Data::RAW :
        noScale->setChecked(true);
        break;
    case Data::MAX :
        scaleByMax->setChecked(true);
        break;
    case Data::SHOULDER :
        scaleByShouler->setChecked(true);
        break;
    }

    isInit = false; //now a change in the boxes or menu entry
    //will trigger an update of the view contains in the active display.
}

void KlustersApp::slotShowNextCluster(){
    //Trigger ths action only if the active display does not contain a ProcessWidget
    if(!doesActiveDisplayContainProcessWidget()){
        KlustersView* view = activeView();
        view->showNextCluster();
    }
}

void KlustersApp::slotShowPreviousCluster(){
    //Trigger ths action only if the active display does not contain a ProcessWidget
    if(!doesActiveDisplayContainProcessWidget()){
        KlustersView* view = activeView();
        view->showPreviousCluster();
    }
}

void KlustersApp::slotSpikesDeleted(){
    //Update the browsing possibility of the traceView
    KlustersView* view = activeView();
    if(view && view->containsTraceView() && !view->clusters().isEmpty()) {
        slotStateChanged("traceViewBrowsingState");
    }
    else{

        slotStateChanged("noTraceViewBrowsingState");
    }
    if (view) view->focusClusterView();
}


void KlustersApp::slotAbout()
{
    QMessageBox::about(this,tr("About - Klusters"),tr("Manual clustering of neuronal action potential\n(c) 2003-2004-2005-2007 Lynn Hazan"));
}

void KlustersApp::slotHanbook()
{
    QHelpViewer *helpDialog = new QHelpViewer(this);
    helpDialog->setHtml(KLUSTER_DOC_PATH + QLatin1String("index.html"));
    helpDialog->setAttribute( Qt::WA_DeleteOnClose );
    helpDialog->show();
}

void KlustersApp::slotSaveRecentFiles()
{
    QSettings settings;
    settings.setValue(QLatin1String("Recent Files"),mFileOpenRecent->recentFiles());
}

/**Informs the active display to present the waveforms for an updated time frame.*/
void KlustersApp::slotUpdateStartTime(int start)
{
    NS3_DIAG()<<" void KlustersApp::slotUpdateStartTime(int start)"<<start;
    if(!isInit){
        startTime = start;
        activeView()->updateTimeFrame(static_cast<long>(start),timeWindow);
    }
}

// ---------------------------------------------------------------------------
// KlustersApp::slotSplitClusterByKnn
// ---------------------------------------------------------------------------
// Action handler for "Split by KNN voting…".  Validates that exactly one
// non-special (id > 1) cluster is selected, prompts the user for
// algorithm parameters, runs the doc-side wrapper, and reports the
// resulting partition.
//
// Parameter defaults (K=10, threshold=0.5, minNew=5, minRef=100) follow
// the convention used in the cluster_merge_recommend.py pipeline:
//   - K=10 balances locality vs noise tolerance for typical sessions
//   - threshold=0.5 demands a simple majority of K
//   - minNew=5 filters bins too small to inspect meaningfully
//   - minRef=100 matches the user's "good cluster" threshold elsewhere
//     in the pipeline (cluster_waveform_stats classes).
// ---------------------------------------------------------------------------
void KlustersApp::slotSplitClusterByKnn()
{
    KlustersView* view = activeView();
    if (!view) {
        QMessageBox::information(this, tr("No view"),
                                 tr("Open a cluster view first."));
        return;
    }
    const QList<int> selected = view->clusters();
    if (selected.size() != 1) {
        QMessageBox::information(this, tr("Split by KNN voting"),
            tr("Select exactly one cluster to split (currently %1 "
               "selected).").arg(selected.size()));
        return;
    }
    const int sourceCluster = selected.first();
    if (sourceCluster <= 1) {
        QMessageBox::information(this, tr("Split by KNN voting"),
            tr("Cluster %1 (%2) is not eligible for KNN-split.  Pick a "
               "real cluster (id ≥ 2).")
                .arg(sourceCluster)
                .arg(sourceCluster == 0 ? tr("artifact") : tr("MUA")));
        return;
    }

    // ── Parameter dialog ─────────────────────────────────────────────────
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Split cluster %1 by KNN voting").arg(sourceCluster));
    QVBoxLayout* outer = new QVBoxLayout(&dlg);
    QLabel* intro = new QLabel(tr(
        "<p>For each spike in cluster <b>%1</b>, find its K nearest "
        "neighbours in feature space — restricted to spikes from "
        "well-isolated existing clusters (≥ <i>min reference size</i> "
        "spikes each).  Group spikes by which reference cluster they "
        "most resemble.  Each group becomes a <b>new</b> cluster (no "
        "spike is moved into an existing cluster).</p>")
            .arg(sourceCluster), &dlg);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    QFormLayout* form = new QFormLayout();
    // Prefill from the persisted user preferences (Settings → Preferences →
    // General → KNN voting split).  On accept we write the values back so
    // the dialog also acts as a "remember last-used" knob — matches what
    // the user expects when the same action is invoked repeatedly during a
    // session.
    QSpinBox*       kBox        = new QSpinBox(&dlg);
    kBox->setRange(2, 200);            kBox->setValue(configuration().getKnnK());
    QDoubleSpinBox* thrBox      = new QDoubleSpinBox(&dlg);
    thrBox->setRange(0.0, 1.0);        thrBox->setSingleStep(0.05);
    thrBox->setDecimals(2);            thrBox->setValue(configuration().getKnnThreshold());
    QSpinBox*       minNewBox   = new QSpinBox(&dlg);
    minNewBox->setRange(1, 10000);     minNewBox->setValue(configuration().getKnnMinNew());
    QSpinBox*       minRefBox   = new QSpinBox(&dlg);
    minRefBox->setRange(10, 100000);   minRefBox->setValue(configuration().getKnnMinRef());
    form->addRow(tr("K (neighbours per spike):"),                kBox);
    form->addRow(tr("Majority threshold (fraction of K):"),       thrBox);
    form->addRow(tr("Min new-cluster size:"),                     minNewBox);
    form->addRow(tr("Min reference-cluster size (\"good\"):"),    minRefBox);
    outer->addLayout(form);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    const int    K           = kBox->value();
    const double thr         = thrBox->value();
    const int    minNew      = minNewBox->value();
    const int    minRef      = minRefBox->value();

    // Persist the user's choices so the next Shift+K invocation prefills
    // them (and so the values surface in Settings → Preferences).  Done
    // unconditionally on accept, before any chance of an early return
    // from the actual split — even a "no useful split" result is
    // informative about the user's intent for the next attempt.
    configuration().setKnnK(K);
    configuration().setKnnThreshold(thr);
    configuration().setKnnMinNew(minNew);
    configuration().setKnnMinRef(minRef);
    configuration().write();

    // ── Run the split ────────────────────────────────────────────────────
    QApplication::setOverrideCursor(Qt::WaitCursor);
    KlustersDoc::KnnSplitResult R = doc->splitClusterByKnnVsReferences(
        sourceCluster, K, thr, minNew, minRef);
    QApplication::restoreOverrideCursor();

    // ── Report result ────────────────────────────────────────────────────
    if (!R.accepted) {
        QMessageBox::warning(this, tr("Split by KNN voting"),
            tr("No split was committed.\n\n%1").arg(R.reason));
        return;
    }

    // Build a per-cluster summary so the user can see which reference
    // each new cluster was matched against (and whether anything ended
    // up in the residual bin).
    QString summary;
    summary += tr("Cluster %1 was split into %2 new cluster(s)")
                .arg(R.sourceId).arg(R.newClusters.size());
    if (R.emptiedClusters.contains(R.sourceId))
        summary += tr(" — source fully consumed");
    summary += QStringLiteral(":\n\n");
    for (int i = 0; i < R.newClusters.size(); ++i) {
        const int newId  = R.newClusters[i];
        const int refId  = R.matchedReferences.value(i, 0);
        const long nSpk  = static_cast<long>(
            doc->data().nbOfSpikes(static_cast<dataType>(newId)));
        if (refId == -1)
            summary += tr("  cluster %1 (%2 spikes) — residual / ambiguous\n")
                            .arg(newId).arg(nSpk);
        else
            summary += tr("  cluster %1 (%2 spikes) — matches reference %3\n")
                            .arg(newId).arg(nSpk).arg(refId);
    }
    summary += QStringLiteral("\nUndo with Ctrl+Z if the partition isn't useful.");
    QMessageBox::information(this, tr("Split by KNN voting — done"), summary);
}





// ---------------------------------------------------------------------------
// Keyboard shortcut help dialog
// ---------------------------------------------------------------------------
void KlustersApp::slotShowShortcutHelp()
{
    struct Entry { const char* key; const char* desc; };
    struct Section { const char* title; std::initializer_list<Entry> entries; };
    static const Section kSections[] = {
        {"Cluster palette", {
            {"Arrow keys",     "Navigate cluster palette"},
            {"S",              "Toggle current selection (palette focus)"},
            {"T",              "Move selected cluster(s) to end of palette (palette focus)"},
            {"Page Up / Page Down", "Nudge selected cluster timestamps \u00b11 sample"},
            {"H",              "Show this keyboard shortcut reference"},
        }},
        {"Display tabs", {
            {"\u2190 / \u2192",           "Cycle display tabs \u2014 only while the tab bar itself has focus (click a tab handle); inside a view the arrows stay cluster navigation"},
            {"Ctrl+\u2190 / Ctrl+\u2192",   "From inside a view: jump to the Overview tab.  From the tab bar: cycle tabs (prev / next, wrapping)"},
            {"Tab / Shift+Tab", "Move focus between the cluster palette and the toolbar fields (Ctrl+Shift+\u2190/\u2192 does the same ring)"},
            {"E",              "Switch between the Error Matrix and Template Matrix tabs (matrix panel)"},
        }},
        {"Cluster operations", {
            {"Ctrl+1",         "New Cluster mode \u2014 draw selection polygon"},
            {"Ctrl+2",         "Split Clusters mode \u2014 draw selection polygon"},
            {"Shift+D",        "DipSplit (live preview \u2014 Enter apply, Esc cancel)"},
            {"Shift+W",        "Watershed split (live preview \u2014 \u2190/\u2192 \u03c3, \u2191/\u2193 thr, Enter apply, Esc cancel)"},
            {"G",              "Group selected clusters"},
            {"R",              "Renumber clusters"},
            {"Shift+R",        "Recluster selected (KlustaKwik)"},
            {"Shift+L",        "Realign spikes for selected cluster"},
            {"Delete",         "Delete noisy cluster (move whole cluster to 1)"},
            {"Shift+Delete",   "Delete artefact cluster (move whole cluster to 0)"},
            {"Z",              "Zoom mode"},
            {"U",              "Update error matrix (+ template matrix if open)"},
            {"F",              "Toggle autoscale in cluster view"},
            {"Enter / Return", "Close selection polygon (New / Split modes)"},
            {"Shift+P",        "PCA-center align all clusters (top-N channels)"},
            {"Shift+F",        "Apply drift to sibling sessions"},
        }},
        {"File", {
            {"Ctrl+O",         "Open"},
            {"Ctrl+S",         "Save"},
            {"Ctrl+Shift+S",   "Renumber and save"},
            {"Ctrl+I",         "Import file"},
            {"Ctrl+P",         "Print"},
            {"Ctrl+Q",         "Quit"},
        }},
        {"Edit / Selection", {
            {"Ctrl+Z",         "Undo"},
            {"Ctrl+Y",         "Redo"},
            {"Ctrl+A",         "Select all clusters"},
            {"Ctrl+Shift+A",   "Select all except 0/1 (artefact / noise)"},
        }},
        {"Waveform display", {
            {"O",              "Overlay presentation"},
            {"M",              "Mean and standard deviation"},
            {"I / D",          "Increase / decrease waveform amplitude"},
            {"Ctrl+Shift+I / Ctrl+Shift+D",
                               "Increase / decrease per-channel amplitudes"},
            {"L",              "Show shoulder-line"},
            {"Shift+M / Shift+A / Shift+U",
                               "Scale by max / shoulder / no scale"},
        }},
        {"Correlograms", {
            {"(no shortcuts)", "Use Correlations menu to adjust amplitude"},
            {"Ctrl+Shift+F / Ctrl+Shift+B",
                               "Next / previous spike (in trace view)"},
        }},
        {"Hierarchical view (.clc child layer)", {
            {"Tab / Shift+Tab", "Cycle focus: parent palette \u2192 child palette \u2192 toolbar fields"},
            {"Ctrl+Shift+\u2190 / Ctrl+Shift+\u2192", "Cycle focus (alternative to Tab)"},
            {"S",              "Mark focused palette's item (parent or child)"},
            {"Esc",            "Return focus from the child palette to the parent"},
            {"G",              "Merge (adaptive): children \u2192 one child; else fold fiber / parents"},
            {"Ctrl+\u2191",        "New fiber from selected children"},
            {"Ctrl+\u2193",        "Group selected parent fibers"},
            {"Ctrl+Shift+\u2193",  "Dissolve selected fiber into its children"},
            {"Ctrl+Shift+Z / Ctrl+Shift+Y", "Undo / redo atom (child-layer) edit"},
        }},
    };

    QString html =
        QStringLiteral("<style>"
            "body{font-family:sans-serif}"
            "h3{margin:14px 0 4px 0;color:#a0c0ff;font-size:11pt}"
            "table{border-collapse:collapse;min-width:520px;margin:0 0 4px 0}"
            "th{background:#2a2a2a;color:#e0e0e0;padding:5px 12px;text-align:left}"
            "td{padding:3px 12px;border-bottom:1px solid #3a3a3a}"
            "td:first-child{font-family:monospace;font-weight:bold;white-space:nowrap;min-width:140px}"
            "</style>");
    for (const auto& sec : kSections) {
        html += QStringLiteral("<h3>%1</h3><table>")
                .arg(QString::fromUtf8(sec.title));
        for (const auto& e : sec.entries)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                    .arg(QString::fromUtf8(e.key)).arg(QString::fromUtf8(e.desc));
        html += QStringLiteral("</table>");
    }

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Keyboard Shortcuts"));
    dlg.resize(640, 720);
    QVBoxLayout* vl = new QVBoxLayout(&dlg);
    QScrollArea* scroll = new QScrollArea(&dlg);
    QLabel* lbl = new QLabel(html);
    lbl->setTextFormat(Qt::RichText);
    lbl->setMargin(8);
    scroll->setWidget(lbl);
    scroll->setWidgetResizable(true);
    vl->addWidget(scroll);
    QDialogButtonBox* bb = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    vl->addWidget(bb);
    dlg.exec();
}

// ---------------------------------------------------------------------------
// Plugins menu (descriptor discovery; read-only listing in v1 — the parameter
// dialog + process runner arrive in later phases, see docs/PLUGIN_API.md).
// ---------------------------------------------------------------------------
void KlustersApp::populatePluginsMenu()
{
    if (!mPluginsMenu)
        return;
    mPluginsMenu->clear();
    mPluginRegistry.reload();
    const QList<KlustersPlugin>& plugins = mPluginRegistry.plugins();

    if (plugins.isEmpty()) {
        QAction* none = mPluginsMenu->addAction(tr("(no plugins found)"));
        none->setEnabled(false);
    } else {
        for (const KlustersPlugin& p : plugins) {
            const QString kind = p.kind.isEmpty() ? tr("plugin") : p.kind;
            QAction* act = mPluginsMenu->addAction(QStringLiteral("%1  [%2]").arg(p.name, kind));
            const QString tip = p.help.section(QLatin1Char('\n'), 0, 0);
            act->setToolTip(tip);
            act->setStatusTip(tip);
            const KlustersPlugin info = p;   // capture by value for the dialog
            connect(act, &QAction::triggered, this, [this, info]() {
                if (doc->url().isEmpty()) {
                    QMessageBox::information(this, tr("Plugins"),
                        tr("Open a clustering before running a plugin."));
                    return;
                }
                PluginDialog dlg(info, this);
                if (dlg.exec() != QDialog::Accepted)
                    return;
                const QMap<QString, QString> params = dlg.values();
                const QMap<QString, QString> ctx = pluginContext();
                const QList<int> sel = clusterPalette ? clusterPalette->selectedClusters()
                                                      : QList<int>();
                const QStringList argv = PluginRegistry::buildArgv(info, params, ctx, sel);
                // v1: dry-run preview.  The process runner (ProcessWidget) and the
                // <integration> dispatch land in the next phase.
                QMessageBox box(this);
                box.setWindowTitle(tr("Plugin command (preview): %1").arg(info.name));
                box.setIcon(QMessageBox::Information);
                box.setText(argv.join(QLatin1Char(' ')));
                box.setInformativeText(
                    tr("This is the command Klusters will run once the process-runner "
                       "phase lands.  Integration on success: %1.")
                        .arg(info.integration.isEmpty() ? tr("none") : info.integration));
                box.exec();
            });
        }
    }
    mPluginsMenu->addSeparator();
    QAction* reload = mPluginsMenu->addAction(tr("&Reload Plugins"));
    connect(reload, &QAction::triggered, this, &KlustersApp::slotReloadPlugins);
}

void KlustersApp::slotReloadPlugins()
{
    populatePluginsMenu();
    const int n = mPluginRegistry.plugins().size();
    statusBar()->showMessage(tr("Reloaded plugins: %1 descriptor(s) found.").arg(n), 4000);
}

QMap<QString, QString> KlustersApp::pluginContext() const
{
    QMap<QString, QString> ctx;
    const QString url = doc->url();
    const QFileInfo fi(url);
    // Session base = the path up to the ".clu" token (e.g.
    // /p/sirotaA-...-20120312.clu.stderiv.5.microfiber -> /p/sirotaA-...-20120312).
    const QString full = fi.absoluteFilePath();
    const int i = full.indexOf(QStringLiteral(".clu"));
    ctx.insert(QStringLiteral("base"),
               (i > 0) ? full.left(i)
                       : fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName());
    ctx.insert(QStringLiteral("group"), doc->currentElectrodeGroupID());
    // variant/tag are parsed from the filename in a later pass; the starter
    // descriptors do not consume them.
    ctx.insert(QStringLiteral("variant"), QString());
    ctx.insert(QStringLiteral("tag"), QString());
    return ctx;
}
