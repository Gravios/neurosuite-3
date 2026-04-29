#include <algorithm>
#include <cmath>
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
#include "clusterPalette.h"
#include "savethread.h"
#include "prefdialog.h"
#include "configuration.h"  // class Configuration
#include "processwidget.h"
#include "spikerealigndialog.h"
#include "realignreviewdialog.h"
#include "realignworker.h"
#include "qhelpviewer.h"



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
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QSlider>
#include <QGridLayout>
#include <QImage>
#include <QPixmap>
#include <QFrame>
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
#include <QTime>
#include <QSettings>

extern int nbUndo;

const QString KlustersApp::INITIAL_WAVEFORM_TIME_WINDOW = "30";
const long KlustersApp::DEFAULT_NB_SPIKES_DISPLAYED = 100;
const QString KlustersApp::INITIAL_CORRELOGRAMS_HALF_TIME_FRAME = "30";
const QString KlustersApp::DEFAULT_BIN_SIZE = "1";


KlustersApp::KlustersApp()
    : QMainWindow(0),
      displayCount(0),
      mainDock(0),
      clusterPanel(0),
      clusterPalette(0),
      tabsParent(0),
      dimensionX(0L),
      dimensionY(0L),
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
      prefDialog(0L),
      processWidget(0L),
      processFinished(true),
      processOutputsFinished(true),
      processKilled(false),
      reclusterRetryTimer(nullptr),
      realignWorker(nullptr),
      realignThread(nullptr),
      realignOutputWidget(nullptr),
      realignRunning(false),
      realignClusterId(-1),
      errorMatrixExists(false),
      templateMatrixExists(false)
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
    processWidget = 0L;

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
    // realignOutputWidget is parented to this and will be deleted automatically.
}

void KlustersApp::initView()
{
    initClusterPanel();
    QSplitter *splitter = new QSplitter;
    splitter->addWidget(clusterPanel);
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
    mRedo->setShortcut(QKeySequence::Redo);
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

    actionMenu->addSeparator();

    mReCluster = actionMenu->addAction(tr("Re&cluster"));
    mReCluster->setShortcut(QKeySequence(Qt::SHIFT  | Qt::Key_R));
    connect(mReCluster,&QAction::triggered, this,&KlustersApp::slotRecluster);

    mAbortReclustering = actionMenu->addAction(tr("&Abort Reclustering"));
    connect(mAbortReclustering, &QAction::triggered, this, &KlustersApp::slotStopRecluster);

    mAbortRealign = actionMenu->addAction(tr("Abort &Realignment"));
    mAbortRealign->setEnabled(false);
    connect(mAbortRealign, &QAction::triggered, this, &KlustersApp::slotAbortRealign);

    actionMenu->addSeparator();

    mRealignSpikes = actionMenu->addAction(tr("R&ealign Spikes…"));
    mRealignSpikes->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_L));
    mRealignSpikes->setToolTip(tr("Re-align spikes in the selected cluster to their true peak, "
                                   "update .res/.spk/.fet files, and swap ordering if needed."));
    connect(mRealignSpikes, &QAction::triggered, this, &KlustersApp::slotRealignSpikes);

    mDipSplit = actionMenu->addAction(tr("&DipSplit Selected Cluster"));
    mDipSplit->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_D));
    mDipSplit->setToolTip(
        tr("Test the selected cluster for hidden bimodality.  If a valley is\n"
           "detected along one of its top-3 principal components AND the two-\n"
           "cluster model beats the single-cluster BIC, split the cluster in two."));
    connect(mDipSplit, &QAction::triggered, this, &KlustersApp::slotDipSplit);

    actionMenu->addSeparator();

    mGenerateProbeDrift = actionMenu->addAction(tr("&Generate Probe Drift…"));
    // Shortcut: Shift+P (P for Probe). Previously Shift+D, which clashed with
    // mDecreaseAmplitudeCorrelation in the Correlations menu — Qt resolved it
    // as an ambiguous overload and dispatched neither, producing
    // "QAction::event: Ambiguous shortcut overload: Shift+D" on every press.
    // The Shift+I/Shift+D Increase/Decrease pair is preserved, in line with
    // Ctrl+I/Ctrl+D (waveforms) and Ctrl+Shift+I/Ctrl+Shift+D (channels).
    mGenerateProbeDrift->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_P));
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

    toolsMenu->addSeparator();
    mWatershed = toolsMenu->addAction(tr("&Watershed Split"));
    mWatershed->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_W));
    mWatershed->setStatusTip(tr(
        "Split the currently-shown clusters into one new cluster per "
        "density basin in the active scatter view."));
    connect(mWatershed, &QAction::triggered, this, &KlustersApp::slotWatershedSplit);



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
    scaleByMax->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_M));
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
    connect(clusterPalette, static_cast<void(ClusterPalette::*)(const QList<int>&)>(&ClusterPalette::groupClusters), this, &KlustersApp::slotGroupClusters);
    connect(clusterPalette, static_cast<void(ClusterPalette::*)(const QList<int>&)>(&ClusterPalette::moveClustersToNoise), this, &KlustersApp::slotMoveClustersToNoise);
    connect(clusterPalette, static_cast<void(ClusterPalette::*)(const QList<int>&)>(&ClusterPalette::moveClustersToArtefact), this, &KlustersApp::slotMoveClustersToArtefact);
    connect(clusterPalette, &ClusterPalette::clusterInformationModified, this, &KlustersApp::slotClusterInformationModified);
    connect(clusterPalette, &ClusterPalette::paletteGainedFocus, this, &KlustersApp::slotShowOverviewForPalette);
    connect(doc, &KlustersDoc::updateUndoNb, this, &KlustersApp::slotUpdateUndoNb);
    connect(doc, &KlustersDoc::updateRedoNb, this, &KlustersApp::slotUpdateRedoNb);
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
    mActionBar->addAction(mGroupeClusters);


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
    if(prefDialog == 0L){
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

    // Rebuild realignArgs from structured prefs (threshold / iterations / maxshift)
    {
        const QString newArgs = QString("--threshold %1 --iterations %2%3")
            .arg(configuration().getRealignThreshold(), 0, 'f', 2)
            .arg(configuration().getRealignIterations())
            .arg(configuration().getRealignMaxShift() > 0
                 ? QString(" --maxshift %1").arg(configuration().getRealignMaxShift())
                 : QString());
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
    if(autoNFeaturesSpinBoxAction){
        // Show the N-feat spinbox whenever autoSelectFeatures is on and a doc is open.
        // It is used at recluster time regardless of which sub-view is currently active,
        // so it must not be gated on the scatter-plot X/Y selectors being visible.
        autoNFeaturesLabelAction->setVisible(autoSelectFeatures);
        autoNFeaturesSpinBoxAction->setVisible(autoSelectFeatures);
        if(!isInit) autoNFeaturesSpinBox->setValue(autoSelectNFeatures);
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
    realignArgs = QString("--threshold %1 --iterations %2%3")
        .arg(configuration().getRealignThreshold(), 0, 'f', 2)
        .arg(configuration().getRealignIterations())
        .arg(configuration().getRealignMaxShift() > 0
             ? QString(" --maxshift %1").arg(configuration().getRealignMaxShift())
             : QString());
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

    // ── DipSplit live-preview mode ──────────────────────────────────────────
    // Mirror of the watershed block above, but simpler: only Enter and
    // Esc are meaningful (no tunables).  All other keys are swallowed
    // while dipPreviewActive is true.
    if (dipPreviewActive &&
        (event->type() == QEvent::ShortcutOverride ||
         event->type() == QEvent::KeyPress))
    {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        const int key  = ke->key();
        const auto mod = ke->modifiers();
        const bool plain = (mod == Qt::NoModifier);

        if (event->type() == QEvent::ShortcutOverride) {
            ke->accept();
            return true;
        }
        if (ke->isAutoRepeat()) return true;

        if (plain) {
            switch (key) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                dipPreviewExit(/*commit=*/true);
                return true;
            case Qt::Key_Escape:
                dipPreviewExit(/*commit=*/false);
                return true;
            }
        }
        // Any other key while preview is active: swallow.
        return true;
    }

    // ── DipSplit live-preview mode ──────────────────────────────────────────
    // Parallel to the watershed block above but with no tunables: only
    // Enter (apply) and Esc (cancel) are honoured.  Any other key is
    // swallowed to prevent stray actions while the overlay is on screen.
    if (dipPreviewActive &&
        (event->type() == QEvent::ShortcutOverride ||
         event->type() == QEvent::KeyPress))
    {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);

        if (event->type() == QEvent::ShortcutOverride) {
            ke->accept();
            return true;
        }
        if (ke->isAutoRepeat()) return true;

        if (ke->modifiers() == Qt::NoModifier) {
            switch (ke->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
                dipPreviewExit(/*commit=*/true);
                return true;
            case Qt::Key_Escape:
                dipPreviewExit(/*commit=*/false);
                return true;
            default:
                break;
            }
        }
        return true;     // swallow everything else
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
        // "1" — new cluster mode
        if(ke->key() == Qt::Key_1 && ke->modifiers() == Qt::NoModifier
           && !isInit && doc && activeView()){
            slotSingleNew();
            return true;
        }
        // "2" — split clusters mode
        if(ke->key() == Qt::Key_2 && ke->modifiers() == Qt::NoModifier
           && !isInit && doc && activeView()){
            slotMultipleNew();
            return true;
        }

        // ── Tab / Shift+Tab ─────────────────────────────────────────────────
        // Cycle: cluster list  →  tab area (single stop, Overview if entering)
        //        →  toolbar fields  →  (wrap)
        if(ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab){
            buildFocusZones();
            if(focusZones.isEmpty()) return QWidget::eventFilter(object, event);

            // Locate which zone currently holds focus.
            QWidget* focused = QApplication::focusWidget();
            int currentZone = -1;

            if(focused){
                for(int z = 0; z < focusZones.size() && currentZone < 0; ++z){
                    QObject* w = focused;
                    while(w){
                        if(w == focusZones[z]){ currentZone = z; break; }
                        w = w->parent();
                    }
                }
            }

            const int n = focusZones.size();
            int next;
            if(ke->key() == Qt::Key_Tab)
                next = (currentZone < 0) ? 0 : (currentZone + 1) % n;
            else
                next = (currentZone < 0) ? n - 1 : (currentZone - 1 + n) % n;

            QWidget* target = focusZones[next];

            // Focus the cluster list or toolbar field directly.
            target->setFocus(Qt::TabFocusReason);
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
    }
    if(event->type() == QEvent::KeyPress){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_S && ke->modifiers() == Qt::NoModifier
           && paletteHasFocus()){
            clusterPalette->toggleCurrentSelection();
            return true;
        }
    }
    // ── T key: palette move-to-end ─────────────────────────────────────────
    // T has no global QAction shortcut (the "Time Frame" QAction shortcut
    // was removed).  When the cluster palette has focus, T re-orders the
    // currently-selected cluster(s) to the end of the palette list — same
    // intercept pattern as Key_S.  The ShortcutOverride claim is retained
    // defensively so any future QAction with T as shortcut still won't
    // win over the palette intercept.
    if(event->type() == QEvent::ShortcutOverride){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_T && ke->modifiers() == Qt::NoModifier
           && paletteHasFocus()){
            ke->accept();
            return true;
        }
    }
    if(event->type() == QEvent::KeyPress){
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if(ke->key() == Qt::Key_T && ke->modifiers() == Qt::NoModifier
           && doc && paletteHasFocus()){
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
    // Tab/Shift+Tab stops:
    //   1. Cluster list (left panel)
    //   2. Toolbar spinboxes / line-edits (left-to-right order)
    // The tab-display area is intentionally excluded so Tab never
    // moves focus inside the waveform/scatter/correlation views.
    focusZones.clear();

    // 1. Cluster list
    if(clusterPanel && clusterPanel->isVisible() && clusterPalette)
        focusZones.append(clusterPalette);

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
        if (w == clusterPalette) return true;
    return false;
}


void KlustersApp::initClusterPanel()
{
    //Creation of the left panel containing the clusters
    clusterPanel = new QDockWidget(tr("The cluster list"),0L);
    clusterPanel->setWindowIcon(QIcon("classnew"));
    //Initialisation of the cluster palette containing the cluster list
    clusterPalette = new ClusterPalette(backgroundColor,clusterPanel,statusBar(),"ClusterPalette");
    //Place the clusterPalette frame in the clusterPanel (the view)
    clusterPanel->setWidget(clusterPalette);
    clusterPanel->setFeatures(QDockWidget::NoDockWidgetFeatures);
    clusterPanel->hide();
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
    correlogramsHalfTimeFrameValidator.setRange(0,static_cast<int>((maximumTime - 1) / 2));
    binSizeValidator.setRange(0,maximumTime);


    //If the setting dialog exists (has already be open once), enable the settings for the channels.
    if(prefDialog != 0L)
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

    KlustersView* view = new KlustersView(*this,*doc,backgroundColor,1,2,clusterList,KlustersView::OVERVIEW,mainDock,0,statusBar(),
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
            view = new KlustersView(*this,*doc,backgroundColor,XDimension,YDimension,clusterList,type,this,0,statusBar(),
                                    displayTimeInterval,waveformsGain,channelPositions,inTimeFrameMode,startingTime,timeFrameWidth,
                                    nbSpkToDisplay,overLay,mean,sizeOfBin,correlogramTimeWindow,scaleMode,line,activeView()->getStartingTime(),activeView()->getDuration(),showHideLabels->isChecked(),activeView()->getUndoList(),activeView()->getRedoList());

        else
            view = new KlustersView(*this,*doc,backgroundColor,XDimension,YDimension,clusterList,type,this,0,statusBar(),
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

        //Disconnect the previous connection
        if(tabsParent != nullptr)
            disconnect(tabsParent,0,0,0);

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
            QMessageBox::critical (this, tr("Error!"), tr("The selected file is invalid, it has to be of the form baseName.clu.n or baseName.fet.n or baseName.par.n"));
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
        } else if(false) { // PARXML_DOWNLOAD_ERROR removed
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
        QString electrodNb;
        if(fileParts.count() < 3)
            electrodNb.clear();
        else
            electrodNb = fileParts[fileParts.count()-1];

        QString baseName = fileParts[0];
        for(uint i = 1;i < fileParts.count()-2; ++i)
            baseName += "." + fileParts[i];
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
    // Returns true for both the recluster output tab (processWidget) and the
    // realign output tab (realignOutputWidget) — both are ProcessWidget instances.
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
    if(doc == 0) return true;
    else{
        if(doc->canCloseView()){
            //Set a waiting cursor in case there is some delay to the ending of the running threads.
            QApplication::restoreOverrideCursor();
            QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
            if(doc->canCloseDocument(this,"queryClose")){
                if(processWidget != 0L && processWidget->isRunning()){
                    processWidget->killJob();
                    processKilled = true;
                }
                if(!(processFinished && processOutputsFinished)){
                    QTimer::singleShot(2000, this, &KlustersApp::close);
                    return false;
                }
                else if(processWidget != 0L){
                    delete processWidget;
                    processWidget = 0L ;
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
bool KlustersApp::queryExit()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    //If the saveThread has not finish, wait until id done
    while(!saveThread->wait()){NS3_DIAG()<<"in queryExit";};
    QApplication::restoreOverrideCursor();

    return true;
}

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
            QMessageBox::critical (this,tr("I/O Error !"), tr("Could not save the current document !"));
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
                                             tr("Feature File (*.fet.*);;Cluster File (*.clu.*);;Spike File (*.spk.*);;Specific Parameter File (*.par.*);;All files (*.*)"));
    if(!url.isEmpty())
    {
        QDir CurrentDir;
        settings.setValue("CurrentDirectory", CurrentDir.absoluteFilePath(url));
        openDocumentFile(url);
    }

    slotStatusMsg(tr("Ready."));
}

void KlustersApp::slotFileClose(){
    if(doc != 0){
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
                        processWidget = 0L;
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
                mainDock = 0L;
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
                processWidget = 0L;
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
                    processWidget = 0L;
                    delete tabsParent->currentWidget();
                } else {
                    if(processWidget->isRunning()){
                        processWidget->killJob();
                        processKilled = true;
                    }
                    if(processFinished && processOutputsFinished){
                        delete tabsParent->currentWidget();
                        processWidget = 0L;
                    } else {
                        mainDock->hide();
                        processWidget->hideWidget();
                        QTimer::singleShot(2000, this, &KlustersApp::slotDisplayClose);
                        return;
                    }
                }
                mainDock = 0L;
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
                    processWidget = 0L;
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
                            processWidget = 0L;
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
                mainDock = 0L;
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

void KlustersApp::slotUndo()
{  
    slotStatusMsg(tr("Reverting last action..."));

    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    doc->undo();
    QApplication::restoreOverrideCursor();

    //Update the browsing possibility of the traceView
    KlustersView* view = activeView();
    if(view->containsTraceView() && !view->clusters().isEmpty()) {
        slotStateChanged("traceViewBrowsingState");
    }
    else
    {
        slotStateChanged("noTraceViewBrowsingState");
    }

    slotStatusMsg(tr("Ready."));
    if (view) view->focusClusterView();
}

void KlustersApp::slotRedo()
{
    slotStatusMsg(tr("Reverting last undo action..."));
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    doc->redo();
    QApplication::restoreOverrideCursor();

    //Update the browsing possibility of the traceView
    KlustersView* view = activeView();
    if(view->containsTraceView() && !view->clusters().isEmpty())
    {
        slotStateChanged("traceViewBrowsingState");
    }
    else  {
        slotStateChanged("noTraceViewBrowsingState");
    }

    slotStatusMsg(tr("Ready."));
    if (view) view->focusClusterView();
}

void KlustersApp::slotUpdateUndoNb(int undoNb){
    currentNbUndo = undoNb;
    if(currentNbUndo > 0) {
        slotStateChanged("undoState");
    }
    else {
        slotStateChanged("emptyUndoState");
    }
}

void KlustersApp::slotUpdateRedoNb(int redoNb){
    currentNbRedo = redoNb;
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


void KlustersApp::viewMenuActivated( int id )
{
    /* QWidget* w = pWorkspace->windowList().at( id );
  if ( w )
    w->setFocus();*/
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
    if (!doc || !activeView() || !clusterPalette) return false;
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
    if (!doc || !activeView()) return;
    //Trigger the action only if the active display does not contain a ProcessWidget
    if(!doesActiveDisplayContainProcessWidget()){
        KlustersView* view = activeView();
        doc->singleColorUpdate(clusterId,*view);
    }
}

void KlustersApp::slotUpdateShownClusters(const QList<int>& selectedClusters){
    //Trigger ths action only if the active display does not contain a ProcessWidget
    if(!activeView())
        return;
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
}

void KlustersApp::slotGroupClusters(QList<int> selectedClusters){
    slotStatusMsg(tr("Grouping clusters..."));
    KlustersView* view = activeView();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    doc->groupClusters(selectedClusters,*view);
    QApplication::restoreOverrideCursor();
    slotStatusMsg(tr("Ready."));
    // Group is a keyboard-driven palette operation (G key from the palette
    // context); keep focus on the palette so the user can keep
    // arrow-navigating without having to Tab back.
    if (clusterPalette) clusterPalette->setFocusToList();
}

void KlustersApp::slotMoveClustersToNoise(QList<int> selectedClusters){
    slotStatusMsg(tr("Delete &noisy cluster(s)..."));
    KlustersView* view = activeView();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    doc->deleteClusters(selectedClusters,*view,1);

    //Update the browsing possibility of the traceView
    if(view->containsTraceView() && view->clusters().size() != 0) {
        slotStateChanged("traceViewBrowsingState");
    }
    else{

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
    // "Delete Noisy" operation.
    if (clusterPalette) clusterPalette->setFocusToList();
}

void KlustersApp::slotMoveClustersToArtefact(QList<int> selectedClusters){
    slotStatusMsg(tr("Delete &artifact cluster(s)..."));
    KlustersView* view = activeView();
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    doc->deleteClusters(selectedClusters,*view,0);

    //Update the browsing possibility of the traceView
    if(view->containsTraceView() && view->clusters().size() != 0) {
        slotStateChanged("traceViewBrowsingState");
    }
    else {
        slotStateChanged("noTraceViewBrowsingState");
    }

    QApplication::restoreOverrideCursor();
    slotStatusMsg(tr("Ready."));
    // Same reasoning as slotMoveClustersToNoise: keep focus on the palette
    // so arrow-key navigation continues on the auto-selected next cluster.
    if (clusterPalette) clusterPalette->setFocusToList();
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
    if (!tabsParent || !doc) return;  // guard against call during teardown
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
    } else {// a ProcessWidget (recluster or realign output tab)
        // Do NOT hide toolbar fields — leave Features, Waveforms, Bin size, Duration
        // in whatever state they were when the user last viewed a real display tab.
        // Only update menu state and cluster palette selection.
        timeFrameMode->setChecked(false);
        overlayPresentation->setChecked(false);
        meanPresentation->setChecked(false);

        // Determine which tab type is active so we highlight the right clusters.
        bool isRealignTab = (widget == realignOutputWidget);
        bool isReclusterTab = !isRealignTab; // (could also be processWidget)

        if (isReclusterTab) {
            //Update the palette of clusters
            if(!processFinished) {
                clusterPalette->selectItems(clustersToRecluster);
            } else {
                QList<int> emptyList;
                clusterPalette->selectItems(emptyList);
            }
            slotStateChanged(QStringLiteral("reclusterViewState"));
        } else {
            // Realign output tab: clear cluster palette selection.
            QList<int> emptyList;
            clusterPalette->selectItems(emptyList);
            slotStateChanged(QStringLiteral("realignViewState"));
        }
    }
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

void KlustersApp::slotUpdateRealignTopChan(int n){
    if(isInit) return;
    // Rebuild realignArgs with the new --topchannels value.  Preserves
    // --threshold, --iterations, and --maxshift from current configuration.
    QString base = QString("--threshold %1 --iterations %2%3")
        .arg(configuration().getRealignThreshold(), 0, 'f', 2)
        .arg(configuration().getRealignIterations())
        .arg(configuration().getRealignMaxShift() > 0
             ? QString(" --maxshift %1").arg(configuration().getRealignMaxShift())
             : QString());
    if (n > 0) base += QString(" --topchannels %1").arg(n);
    realignArgs = base;
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
    if(prefDialog != 0L)
        prefDialog->enableChannelSettings(false);
}

void KlustersApp::slotUpdateCorrelogramsHalfDuration(){
    if(!isInit){
        int halfTimeFrame = (correlogramsHalfDuration->displayText()).toInt();
        if(halfTimeFrame > (maximumTime - binSize) / 2){
            correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg((correlogramTimeFrame - binSize) / 2));
            return;
        }

        float x = (2*static_cast<float>(halfTimeFrame)
                   /static_cast<float>(binSize)-1) * 0.5;
        int k = static_cast<int>(x + 0.5);

        correlogramTimeFrame = (2*k+1)*binSize;
        if(k != x){
            correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(static_cast<int>(correlogramTimeFrame / 2)));
        }
        activeView()->updateBinSizeAndTimeFrame(binSize,correlogramTimeFrame);
    }
}

void KlustersApp::slotUpdateBinSize(){
    if(!isInit){
        binSize = (binSizeBox->displayText()).toInt();
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

    if(mainDock != 0L){
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

    // Clean up the previous recluster output tab if it still exists.
    if(processWidget != 0L){
        int tabIndex = tabsParent->indexOf(processWidget);
        if(tabIndex != -1){
            tabsParent->removeTab(tabIndex);
            displayCount--;
        }
        delete processWidget;
        processWidget = 0L;
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
        if(autoSelectFeatures){
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
                    for(int k = 0; k < nSelect && k < iv.size(); ++k){
                        if(topVar > 0.0 && iv[k].second < minVar)
                            break;   // remaining features are at noise level
                        selected.insert(iv[k].first);
                    }
                    // Always keep at least the single most-informative feature.
                    if(selected.isEmpty() && !iv.isEmpty())
                        selected.insert(iv[0].first);

                    // Build bit-string; timestamp column is always 1.
                    for(int i = 0; i < nFeatureCols; ++i)
                        features.append(selected.contains(i) ? QLatin1Char('1') : QLatin1Char('0'));
                    features.append(QLatin1Char('1'));
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
    QStringList argList = QProcess::splitCommand(reclusteringArgs);
    for(QString &arg : argList){
        arg.replace(QLatin1String("%fileBaseName"),     fileBaseName);
        arg.replace(QLatin1String("%electrodeGroupID"), electrodeGroupID);
        if(!features.isEmpty())
            arg.replace(QLatin1String("%features"),     features);
    }

    reclusteringFetFileName  = doc->documentDirectory() + QLatin1Char('/');
    reclusteringFetFileName += fileBaseName;
    reclusteringFetFileName += QLatin1String(".fet.");
    reclusteringFetFileName += electrodeGroupID;

    //Create the feature file for the selected clusters and get its name.
    int returnStatus = doc->createFeatureFile(clustersToRecluster,reclusteringFetFileName);
    if(returnStatus == KlustersDoc::OPEN_ERROR){
        QMessageBox::critical (this,tr("Error !"),tr("The reclustering feature file cannot be created (possibly because of insufficient file access permissions).\n Reclustering can not be done."));
        return;
    } else if(returnStatus == KlustersDoc::CREATION_ERROR) {
        QMessageBox::critical (this,tr("IO Error !"),tr("An error happened while creating the reclustering feature file.\n Reclustering can not be done."));
        return;
    }

    if(processWidget == 0L){

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
            QMessageBox::critical(0,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program."));

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

    doc->reclusteringUpdate(clustersToRecluster,clustersFromReclustering);

    // Restore focus to the palette rather than the 2D ClusterView.  After a
    // recluster the user's natural next step is usually to pick one of the
    // new clusters and inspect it — that requires arrow-key navigation in
    // the palette, which needs iconView focus.  focusClusterView() here
    // would silently break arrow-key nav (and force a Tab press to recover).
    if (clusterPalette) clusterPalette->setFocusToList();
    processFinished = true;
    processKilled = false;
    // Re-run the full tab-change logic so that every action disabled by
    // reclusterState is restored to the correct enabled/disabled state for
    // whichever views are currently open.  A bare noReclusterState only
    // re-enables mReCluster and leaves everything else locked.
    slotTabChange(tabsParent->currentIndex());
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
            break;
        case KlustersView::TEMPLATE_MATRIX:
            templateMatrixExists = true;
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
    if(view->containsTraceView() && !view->clusters().isEmpty()) {
        slotStateChanged("traceViewBrowsingState");
    }
    else{

        slotStateChanged("noTraceViewBrowsingState");
    }
    if (view) view->focusClusterView();
}

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
        mUpdateDisplay->setEnabled(false);
        mZoomAction->setEnabled(false);
        mUpdateErrorMatrix->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);

        mDeleteNoisy->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mRealignSpikes->setEnabled(false);
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
        mAbortReclustering->setEnabled(false);

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
        mDeleteNoisy->setEnabled(true);
        mDeleteArtifact->setEnabled(true);
        newGroupingAssistantDisplay->setEnabled(true);
        mDeleteArtifactSpikes->setEnabled(true);
        mReCluster->setEnabled(true);
        mRealignSpikes->setEnabled(true);
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
    } else if(state == QLatin1String("noErrorMatrixViewState")) {
        mUpdateErrorMatrix->setEnabled(false);
    } else if(state == QLatin1String("errorMatrixViewState")) {
        mUpdateErrorMatrix->setEnabled(true);
        newGroupingAssistantDisplay->setEnabled(false);
        mDeleteNoisy->setEnabled(true);
        mRenumberClusters->setEnabled(true);

        mDeleteArtifact->setEnabled(true);
        mGroupeClusters->setEnabled(true);

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
        mRealignSpikes->setEnabled(false);
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
        mDeleteArtifact->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mRealignSpikes->setEnabled(false);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
        mRenumberClusters->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mAbortReclustering->setEnabled(true);
        mIncreaseAmplitudeCorrelation->setEnabled(false);
        mDecreaseAmplitudeCorrelation->setEnabled(false);
    } else if(state == QLatin1String("noReclusterState")) {
        mReCluster->setEnabled(true);
        mRealignSpikes->setEnabled(true);
        nudgeMinusAction->setEnabled(true);
        nudgePlusAction->setEnabled(true);
        mAbortReclustering->setEnabled(false);
    } else if(state == QLatin1String("stoppedReclusterState")) {
        mAbortReclustering->setEnabled(false);

    // ── Realignment states — mirror reclusterState locks exactly ─────────────
    } else if(state == QLatin1String("realignState")) {
        // Lock all editing actions for the duration of the realignment job.
        mUndo->setEnabled(false);
        mRedo->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);
        mGroupeClusters->setEnabled(false);
        mDeleteArtifact->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mRealignSpikes->setEnabled(false);
        nudgeMinusAction->setEnabled(false);
        nudgePlusAction->setEnabled(false);
        mRenumberClusters->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mDeleteNoisySpikes->setEnabled(false);
        mAbortReclustering->setEnabled(false);
        mAbortRealign->setEnabled(true);
        mIncreaseAmplitudeCorrelation->setEnabled(false);
        mDecreaseAmplitudeCorrelation->setEnabled(false);
        mSaveAction->setEnabled(false);
        mSaveAsAction->setEnabled(false);
        mRenumberAndSave->setEnabled(false);

    } else if(state == QLatin1String("noRealignState")) {
        // Restore all actions that realignState locked.
        mRealignSpikes->setEnabled(true);
        nudgeMinusAction->setEnabled(true);
        nudgePlusAction->setEnabled(true);
        mAbortRealign->setEnabled(false);
        mSaveAction->setEnabled(true);
        mSaveAsAction->setEnabled(true);
        mRenumberAndSave->setEnabled(true);
        // Restore editing actions (mirror what realignState disabled)
        mUndo->setEnabled(true);
        mRedo->setEnabled(true);
        mNewCluster->setEnabled(true);
        mSplitClusters->setEnabled(true);
        mGroupeClusters->setEnabled(true);
        mDeleteArtifact->setEnabled(true);
        mDeleteArtifactSpikes->setEnabled(true);
        mReCluster->setEnabled(true);
        mRenumberClusters->setEnabled(true);
        mDeleteNoisy->setEnabled(true);
        mDeleteNoisySpikes->setEnabled(true);
        mIncreaseAmplitudeCorrelation->setEnabled(true);
        mDecreaseAmplitudeCorrelation->setEnabled(true);
        // Re-sync with tab state so any tab-specific disabling is reapplied.
        slotTabChange(tabsParent->currentIndex());

    } else if(state == QLatin1String("stoppedRealignState")) {
        mAbortRealign->setEnabled(false);

    } else if(state == QLatin1String("realignViewState")) {
        // Applied by slotTabChange when the realign output tab is the active tab.
        // Mirrors reclusterViewState.
        mZoomAction->setEnabled(false);
        mUpdateErrorMatrix->setEnabled(false);
        mNewCluster->setEnabled(false);
        mSplitClusters->setEnabled(false);
        mDeleteNoisy->setEnabled(false);
        mDeleteNoisySpikes->setEnabled(false);
        mDeleteArtifact->setEnabled(false);
        mDeleteArtifactSpikes->setEnabled(false);
        mReCluster->setEnabled(false);
        mRealignSpikes->setEnabled(false);
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
        shoulderLine->setEnabled(false);
        mIncreaseAmplitude->setEnabled(false);
        mDecreaseAmplitude->setEnabled(false);
        mNextSpike->setEnabled(false);
        mPreviousSpike->setEnabled(false);
        mUndo->setEnabled(false);
        mRedo->setEnabled(false);
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

void KlustersApp::slotRealignSpikes()
{
    if (!doc) {
        QMessageBox::information(this, tr("No document"),
                                 tr("Please open a file first."));
        return;
    }

    // Don't allow a second realignment while one is running.
    if (realignRunning) {
        QMessageBox::information(this, tr("Realignment in progress"),
            tr("A realignment job is already running.\n"
               "Use \"Abort Realignment\" to cancel it first."));
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

    // ── Pre-flight dialog ────────────────────────────────────────────────────
    // Shows cluster info, PCA file status, parameters summary.
    // Does NOT run any computation — user just confirms and clicks Start.
    SpikeRealignDialog dlg(*doc, clusterId, realignArgs, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // ── Create (or recycle) the output tab ───────────────────────────────────
    // Remove any leftover tab from a previous realignment run.
    if (realignOutputWidget) {
        int tabIndex = tabsParent->indexOf(realignOutputWidget);
        if (tabIndex != -1) {
            tabsParent->removeTab(tabIndex);
            displayCount--;
        }
        delete realignOutputWidget;
        realignOutputWidget = nullptr;
    }

    realignOutputWidget = new ProcessWidget(this);
    realignOutputWidget->setFocusPolicy(Qt::NoFocus);
    // ProcessWidget inherits QListWidget and displays coloured log lines.
    // We do NOT call startJob() — output is fed directly via insertStdoutLine /
    // insertStderrLine from the worker's logLine signal.
    tabsParent->addTab(realignOutputWidget, tr("Realign output"));
    displayCount++;
    tabsParent->setCurrentWidget(realignOutputWidget);

    // Add a header line matching the reclustering tab style.
    realignOutputWidget->insertStdoutLine(
        tr("=== Spike realignment — cluster %1 ===").arg(clusterId));

    // Connect tab-change signal so slotTabChange handles the realign tab correctly.
    // (It is already connected from the recluster setup; connecting again is harmless
    // but we guard anyway.)
    connect(tabsParent, &QTabWidget::currentChanged, this, &KlustersApp::slotTabChange,
            Qt::UniqueConnection);

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

    auto* worker = new RealignWorker(doc, clusterId, realignArgs);
    auto* thread = new QThread(this);
    worker->moveToThread(thread);

    // Stream log lines to the output tab (queued — crosses thread boundary).
    connect(worker, &RealignWorker::logLine,
            this, [this](const QString& line, bool isError) {
                if (!realignOutputWidget) return;
                if (isError)
                    realignOutputWidget->insertStderrLine(line);
                else
                    realignOutputWidget->insertStdoutLine(line);
            }, Qt::QueuedConnection);

    // When the worker signals finished, call our slot on the GUI thread.
    connect(worker, &RealignWorker::finished,
            this, &KlustersApp::slotRealignFinished,
            Qt::QueuedConnection);

    // Auto-cleanup: delete the worker object after the thread exits.
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);

    // Start() triggers RealignWorker::run() via QThread::started.
    connect(thread, &QThread::started, worker, &RealignWorker::run);

    realignWorker  = worker;
    realignThread  = thread;
    realignClusterId = clusterId;
    thread->start();
}

void KlustersApp::slotAbortRealign()
{
    if (!realignRunning) return;

    if (realignWorker)
        // Signal the worker to stop (non-blocking — the current realignSpikes()
        // call will still complete; cancel() prevents re-use in batch mode).
        qobject_cast<RealignWorker*>(realignWorker)->cancel();

    if (realignThread) {
        realignThread->quit();
        realignThread->wait(5000);
        delete realignThread;
        realignThread = nullptr;
    }
    realignWorker  = nullptr;   // already scheduled for deleteLater
    realignRunning = false;

    if (realignOutputWidget)
        realignOutputWidget->insertStderrLine(
            tr("--- Realignment aborted by user ---"));

    slotStateChanged(QStringLiteral("stoppedRealignState"));
    // Restore full UI state for whichever tab is currently active.
    slotTabChange(tabsParent->currentIndex());
}

void KlustersApp::slotRealignFinished(bool ok, int nShifted, int nSwapped,
                                       QVector<float> meanBefore,
                                       QVector<float> meanAfter,
                                       QString backupBase,
                                       int nChan, int nSamp)
{
    realignRunning = false;

    // Clean up thread.
    if (realignThread) {
        realignThread->quit();
        realignThread->wait(2000);
        delete realignThread;
        realignThread = nullptr;
    }
    realignWorker = nullptr;   // already deleteLater'd

    // Summary line in the output tab.
    if (realignOutputWidget) {
        if (ok) {
            realignOutputWidget->insertStdoutLine(
                tr("=== Done: %1 spike(s) shifted, %2 sort-order correction(s). ===")
                .arg(nShifted).arg(nSwapped));
        } else {
            realignOutputWidget->insertStderrLine(
                tr("=== Realignment failed — see log above. ==="));
        }
    }

    // Unlock the UI.
    slotStateChanged(QStringLiteral("noRealignState"));

    if (ok) {
        // Show accept/reject review dialog with before/after mean waveforms.
        // backupBase is now always empty (deferred writes — no backup files).
        RealignReviewDialog reviewDlg(realignClusterId, nShifted, nSwapped,
                                      nChan, nSamp,
                                      meanBefore, meanAfter,
                                      QString(), this);
        reviewDlg.exec();

        if (reviewDlg.accepted()) {
            // User accepted: pending files exist, will be flushed on next save.
            // Mark document modified so the save action is enabled.
            doc->setModified(true);

            // Invalidate both caches so views re-read from the pending .spk
            // and recompute correlograms from the updated in-memory timestamps.
            if (realignClusterId >= 0) {
                doc->invalidateWaveformCache(realignClusterId);
                doc->invalidateCorrelogramCache(realignClusterId);
                doc->forceClusterRefresh(realignClusterId);
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
            //
            // Do NOT follow this with activeView()->focusClusterView(): that
            // call would steal focus to the 2D scatter widget the instant
            // after we granted it to the palette, so arrow-key navigation
            // would silently require a Tab press to recover.  The scatter
            // view doesn't need explicit focus here — its repaint was
            // already triggered by forceClusterRefresh() above.
            if (clusterPalette && realignClusterId >= 0) {
                clusterPalette->selectItems(QList<int>{realignClusterId});
                clusterPalette->setFocusToList();
            }

        } else {
            // User rejected: delete pending files, restore original spkFileName,
            // revert in-memory Data.
            doc->rejectLastRealign();

            // Invalidate caches and repaint (back to original state).
            if (realignClusterId >= 0) {
                doc->invalidateWaveformCache(realignClusterId);
                doc->invalidateCorrelogramCache(realignClusterId);
                doc->forceClusterRefresh(realignClusterId);
            }

            // Restore focus to the palette on the rejected cluster so the
            // user can continue arrow-key navigation.  Without this, focus
            // stays on whichever widget had it before the modal review
            // dialog opened (often the 2D scatter, sometimes nothing
            // reliable after a tab switch), silently breaking arrow keys.
            if (clusterPalette && realignClusterId >= 0) {
                clusterPalette->selectItems(QList<int>{realignClusterId});
                clusterPalette->setFocusToList();
            }
        }

        realignClusterId = -1;
    }

    // Restore undo/redo state correctly.
    updateUndoRedoDisplay();
}

// ---------------------------------------------------------------------------
// slotDipSplit  +  dipPreviewEnter / dipPreviewExit / dipRefreshOverlay
//
// Shift+D opens an in-place live preview of the dipsplit decision: the
// algorithm runs once via doc->dipSplitDecide(), the proposed partition
// is rendered as coloured discs over the active scatter view (blue =
// stays in the source cluster, red = moves to a new cluster), and the
// user confirms with Enter or aborts with Esc.  No tunables — dipsplit's
// parameters affect accept/reject but not the labelling, so there's
// nothing for arrow keys to adjust.  Mutually exclusive with the
// watershed live preview (eventFilter rejects Shift+D while wsPreviewActive).
//
// On Enter, doc->dipSplitApply is called with the cached decision; the
// algorithm does not re-run.  This means the preview cannot drift from
// the committed result.
// ---------------------------------------------------------------------------
void KlustersApp::slotDipSplit()
{
    if (dipPreviewActive) {
        // Pressing Shift+D while already in dip preview is a no-op.
        // Use Enter to commit, Esc to cancel.
        return;
    }
    if (wsPreviewActive) {
        // Watershed preview owns the active view; ignore Shift+D until
        // the user resolves the watershed preview first.
        return;
    }
    dipPreviewEnter();
}

bool KlustersApp::dipPreviewEnter()
{
    if (!doc || !activeView() || !clusterPalette) return false;
    if (doesActiveDisplayContainProcessWidget()) return false;

    // Pick the current cluster to test.  Same selection rule the prior
    // synchronous slot used: first non-noise / non-artefact cluster
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
        return false;
    }

    KlustersView* aview = activeView();
    if (!aview->containsClusterView()) {
        statusBar()->showMessage(
            tr("DipSplit: the active display has no scatter view."), 4000);
        return false;
    }

    // Locate the ClusterView the same way wsEnter does.
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
        return false;
    }

    // Stash parameters at entry time.  These are passed verbatim to
    // dipSplitApply on commit so the curation log records the values
    // that were in effect when the decision was made (not whatever
    // the user might fiddle with during the preview).
    dipClusterId    = clusterId;
    dipMinSize      = configuration().getDipSplitMinSize();
    dipBloatFactor  = static_cast<float>(configuration().getDipSplitBloatFactor());
    dipValleyThresh = static_cast<float>(configuration().getDipSplitValleyThresh());

    QApplication::setOverrideCursor(Qt::WaitCursor);
    dipDecision = doc->dipSplitDecide(
        dipClusterId, dipMinSize, dipBloatFactor, dipValleyThresh);
    QApplication::restoreOverrideCursor();

    if (!dipDecision.accepted) {
        // Don't enter preview when the algorithm rejected — there's
        // nothing to show.  Surface the same explanatory message the
        // synchronous slot used.
        const QString why = [&] {
            const QString r = dipDecision.reason;
            if (r == QLatin1String("too_small"))
                return tr("cluster too small (< 2×MinSize)");
            if (r == QLatin1String("not_bloated"))
                return tr("cluster is already unimodal (Mahal²₉₀=%1  χ²₉₀=%2)")
                       .arg(dipDecision.mahal2P90, 0, 'f', 1)
                       .arg(dipDecision.chi2_90,   0, 'f', 1);
            if (r == QLatin1String("no_valley"))
                return tr("no valley deep enough on any of the top 3 principal "
                          "components (best depth=%1)")
                       .arg(dipDecision.bestDepth, 0, 'f', 3);
            if (r == QLatin1String("small_child"))
                return tr("split would produce a child cluster smaller than MinSize");
            if (r == QLatin1String("bic_worse"))
                return tr("single-Gaussian BIC is better than two-cluster BIC (ΔBIC=%1)")
                       .arg(dipDecision.deltaBIC, 0, 'f', 1);
            if (r == QLatin1String("cluster_not_found"))
                return tr("cluster not found");
            if (r == QLatin1String("bad_features"))
                return tr("feature matrix is singular or has too few dimensions");
            return tr("unknown reason (%1)").arg(r);
        }();
        statusBar()->showMessage(
            tr("DipSplit: no split for cluster %1 — %2")
                .arg(dipClusterId).arg(why),
            10000);
        // Reset state — we never entered preview.
        dipDecision    = KlustersDoc::DipSplitDecision{};
        dipClusterId   = -1;
        return false;
    }

    dipScatter = scatter;
    dipView    = aview;
    dipPreviewActive  = true;
    dipRefreshOverlay();
    return true;
}

void KlustersApp::dipPreviewExit(bool commit)
{
    if (!dipPreviewActive) return;

    // Snapshot the commit-relevant state before clearing.
    const KlustersDoc::DipSplitDecision D    = dipDecision;
    const int                           src  = dipClusterId;
    const int                           ms   = dipMinSize;
    const float                         bf   = dipBloatFactor;
    const float                         vt   = dipValleyThresh;
    ClusterView* const                  scat = dipScatter;

    // Clear preview state (also drops the overlay).
    if (scat) scat->clearDipsplitPreview();
    dipPreviewActive    = false;
    dipScatter   = nullptr;
    dipView      = nullptr;
    dipClusterId = -1;
    dipDecision  = KlustersDoc::DipSplitDecision{};

    if (!commit) {
        if (clusterPalette) clusterPalette->setFocusToList();
        statusBar()->showMessage(tr("DipSplit cancelled."), 3000);
        return;
    }

    // Apply the cached decision.  Algorithm does not re-run.
    slotStatusMsg(tr("Applying DipSplit..."));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const KlustersDoc::DipSplitResult r =
        doc->dipSplitApply(D, ms, bf, vt);
    QApplication::restoreOverrideCursor();

    if (r.accepted) {
        const QString line =
            tr("DipSplit: cluster %1 → %2 (%3 spikes) + %4 (%5 spikes)   "
               "PC%6 depth=%7  ΔBIC=%8")
            .arg(src)
            .arg(r.renamedSourceId).arg(r.n0)
            .arg(r.newClusterId).arg(r.n1)
            .arg(r.bestPC)
            .arg(r.bestDepth, 0, 'f', 3)
            .arg(r.deltaBIC,  0, 'f', 1);
        statusBar()->showMessage(line, 10000);
        if (clusterPalette && r.newClusterId > 0) {
            clusterPalette->selectItems(QList<int>{r.newClusterId});
            clusterPalette->setFocusToList();
        }
    } else {
        // dipSplitApply rejected after the live decision said yes — this
        // can happen if moveSpikeSubset returns small_child (e.g. label
        // mapping produced an empty side after row deduplication).
        statusBar()->showMessage(
            tr("DipSplit: commit rejected (%1).").arg(r.reason),
            6000);
        if (clusterPalette) clusterPalette->setFocusToList();
    }
    slotStatusMsg(tr("Ready."));
}

void KlustersApp::dipRefreshOverlay()
{
    if (!dipPreviewActive || !dipScatter || !doc) return;

    // Build per-spike (X, Y, label) lists for the source cluster's
    // members in the active view's current dimensions.  The
    // DipSplitDecision carries one label per cluster member in the
    // order returned by Data::spikePositions; the rowsByMember entry
    // gives the corresponding 1-based feature row, which is the row
    // we feed back into Data::featureValue to read (X, Y).
    const int dimX = dipView->abscissaDimension();
    const int dimY = dipView->ordinateDimension();

    const int N = static_cast<int>(dipDecision.labels.size());
    QVector<double> xs; xs.reserve(N);
    QVector<double> ys; ys.reserve(N);
    QVector<int>    labs; labs.reserve(N);
    for (int i = 0; i < N; ++i) {
        const dataType row = dipDecision.rowsByMember.at(i);
        xs.append (static_cast<double>(doc->data().featureValue(row, dimX)));
        ys.append (static_cast<double>(doc->data().featureValue(row, dimY)));
        labs.append(dipDecision.labels[static_cast<size_t>(i)]);
    }

    const QString hud = tr(
        "DipSplit preview\n"
        "  cluster %1   →   %2 (left, %3 spikes) + new (right, %4 spikes)\n"
        "  best PC = %5    valley depth = %6    ΔBIC = %7\n"
        "  Enter: apply     Esc: cancel")
        .arg(dipClusterId)
        .arg(dipClusterId)
        .arg(dipDecision.n0)
        .arg(dipDecision.n1)
        .arg(dipDecision.bestPC)
        .arg(dipDecision.bestDepth, 0, 'f', 3)
        .arg(dipDecision.deltaBIC,  0, 'f', 1);

    dipScatter->setDipsplitPreview(xs, ys, labs, hud);
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
    if (!doc || doc->url().isEmpty()) return;

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
    if (!doc || doc->url().isEmpty()) return;

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
void KlustersApp::slotNudgeTimestampMinus()
{
    if (nudgeInProgress || isInit || !doc || !activeView()) return;
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
    const bool ok = doc->nudgeClusterTimestamps(id, -1);
    nudgeInProgress = false;
    lastNudgeTimer.restart();
    nudgeMinusAction->setEnabled(true);
    nudgePlusAction->setEnabled(true);
    if (ok)
        statusBar()->showMessage(
            tr("Cluster %1: −1 sample.").arg(id), 2000);
    else
        statusBar()->showMessage(tr("Timestamp nudge failed."), 3000);
    // Nudge is a keyboard-triggered op (PageUp/PageDown or 3/4) initiated
    // from the cluster palette.  Keep palette focus so the user can keep
    // arrow-navigating between clusters without having to Tab back.
    // setFocusToList() targets the palette's inner iconView (which owns
    // arrow-key navigation); a plain setFocus() on the QDockWidget shell
    // would not propagate and arrow keys would still go to the active
    // view widget.
    if (clusterPalette) clusterPalette->setFocusToList();
}

void KlustersApp::slotNudgeTimestampPlus()
{
    if (nudgeInProgress || isInit || !doc || !activeView()) return;
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
    const bool ok = doc->nudgeClusterTimestamps(id, +1);
    nudgeInProgress = false;
    lastNudgeTimer.restart();
    nudgeMinusAction->setEnabled(true);
    nudgePlusAction->setEnabled(true);
    if (ok)
        statusBar()->showMessage(
            tr("Cluster %1: +1 sample.").arg(id), 2000);
    else
        statusBar()->showMessage(tr("Timestamp nudge failed."), 3000);
    // See slotNudgeTimestampMinus for rationale: keep palette focus.
    if (clusterPalette) clusterPalette->setFocusToList();
}

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
    if (!doc || !clusterPalette || !activeView()) return;
    QList<int> sel = clusterPalette->selectedClusters();
    if (sel.isEmpty()) {
        statusBar()->showMessage(
            tr("Select a cluster in the palette first."), 3000);
        return;
    }

    // Filter: drop the global-max cluster and 0/1 (the rename would be a
    // no-op for the max, and remapping 0/1 would corrupt special-cluster
    // semantics).
    const int globalMax = static_cast<int>(doc->data().highestClusterId());
    if (globalMax > 0) sel.removeAll(globalMax);
    sel.removeAll(0);
    sel.removeAll(1);

    if (sel.isEmpty()) {
        statusBar()->showMessage(
            tr("Nothing to move — selection is already at the end."), 3000);
        clusterPalette->setFocusToList();
        return;
    }

    // Keep palette focus through the renumber so arrow-nav resumes
    // immediately.  doc->renumberClustersToEnd handles the data + view
    // notification + palette refresh.
    doc->renumberClustersToEnd(sel);
    clusterPalette->setFocusToList();

    if (sel.size() == 1) {
        statusBar()->showMessage(
            tr("Cluster %1 renumbered to end.").arg(sel.first()), 2500);
    } else {
        statusBar()->showMessage(
            tr("%1 clusters renumbered to end.").arg(sel.size()), 2500);
    }
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
        {"Cluster operations", {
            {"1",              "New Cluster mode \u2014 draw selection polygon"},
            {"2",              "Split Clusters mode \u2014 draw selection polygon"},
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
            {"Shift+P",        "Generate probe-drift estimate (current group)"},
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
                .arg(QLatin1String(sec.title));
        for (const auto& e : sec.entries)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                    .arg(QLatin1String(e.key)).arg(QLatin1String(e.desc));
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
