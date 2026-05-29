/***************************************************************************
                          klusters.h  -  description
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

#ifndef KLUSTERS_H
#define KLUSTERS_H

// Set NS3_VERBOSE=1 at compile time (e.g. -DNS3_VERBOSE=1) to enable
// verbose qDebug diagnostic output.  Defaults to off.
#ifndef NS3_VERBOSE
#  define NS3_VERBOSE 0
#endif
#define NS3_DIAG if(NS3_VERBOSE) qDebug

//include files application specific
#include "spinbox.h"
#include "klustersview.h"
#include "klustersdoc.h"   // needed by inline slot methods on KlustersDoc
#include "watershed2d.h"   // for Result struct used in live-preview state


// include files for Qt
#include <QList>
#include <QVector>
#include <QSpinBox> 
#include <QValidator>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QApplication>


#include <QEvent>
#include <QShowEvent>

#include <QDockWidget>
#include <QMainWindow>
#include <QAction>
#include <QTableWidget>
#include <QProcess>
#include <QTimer>
#include <QThread>



// forward declaration of the Klusters classes
class KlustersDoc;
class ClusterPalette;
class ClusterView;     // for watershed live-preview overlay
class SaveThread;
class PrefDialog;
class QShortcut;       // for dipsplit post-commit Esc/Enter shortcuts
class ProcessWidget;
class QProgressBar;
class QRecentFileAction;
class QExtendTabWidget;

/**
  * The Klusters main window and central class. It sets up the main
  * window and reads the config file as well as providing a menubar, toolbar
  * and statusbar. There is only one document open by application.
  * In initClusterPanel(), the palette for the clusters is created.
  * View windows are created in createView().The MDI child is an instance of KlustersView,
  * the document an instance of KlustersDoc.
  * KlustersApp reimplements the methods that DockMainWindow provides for main window handling and supports
  * full session management as well as keyboard accelerator configuration.
  * @author Lynn Hazan
  */
class KlustersApp : public QMainWindow
{
    Q_OBJECT

public:
    /** Construtor of KlustersApp, calls all init functions to create the application.
     * @see initMenuBar initToolBar
     */
    explicit KlustersApp();
    ~KlustersApp();

    /**Opens a file, only one document at the time is allowed.
    * Asking for a new one will open a new instance of the application with it.
    */
    void openDocumentFile(const QString& url=QString());

    /** Imports a file using the old format, only one document at the time is allowed.
    * Asking for a new one will open a new instance of the application with it.
    */
    void importDocumentFile(const QString& url=QString());

    /**Returns the view contains in the active display.
    * @return active view.
    */
    KlustersView* activeView();

    /**Returns true if the active display contains the output of a process, false otherwise.*/
    bool doesActiveDisplayContainProcessWidget();

    /**Adds a new view (ClusterView, WaveformView or CorrelationView) to the active display.
    * @param displayType type of view to add (ClusterView, WaveformView, or CorrelationView).
    */
    void widgetAddToDisplay(KlustersView::DisplayType displayType);

    /**Updates the menu due to the removal of a view (ClusterView, WaveformView or CorrelationView) in the active display.
    * @param displayType type of view to add (ClusterView,WaveformView or CorrelationView).
    */
    void widgetRemovedFromDisplay(KlustersView::DisplayType displayType);

    /**Informs of the existance of an Error Matrix View in the application.
    * @return true if an Error Matrix View exists in the application, false otherwise.
    */
    bool isExistAnErrorMatrix()   const {return errorMatrixExists;}
    bool isExistATemplateMatrix() const {return templateMatrixExists;}

    /**Updates the dimension spin boxes.
    * @param dimensionX abscissa dimension.
    * @param dimensionY ordinate dimension.
    */
    void updateDimensionSpinBoxes(int dimensionX, int dimensionY);

    /**Updates the correlogeramView parameters.
    * @param binSize size of the bins to use to compute the correlograms.
    * @param timeWindow time frame to use to compute the correlograms.
    * @param isShoulderLine boolean indicating if a doted line is drawn at the shoulder level of the correlograms.
    * @param correlationScale  type of scale used to present the correlation data.
    */
    void updateCorrelogramViewVariables(int binSize,int timeWindow,bool isShoulderLine, Data::ScaleMode correlationScale);
    

protected:
    void customEvent (QEvent *event) override;
    void showEvent(QShowEvent* event)override {slotUpdateParameterBar();}

    /** Event filter to catch right click for contextual menu.
    * @param object target object for the event.
    * @param event event sent.
    */
    bool eventFilter(QObject* object,QEvent* event) override;

    void closeEvent(QCloseEvent *event) override;
public Q_SLOTS:
    /** queryClose is called by KDocMainWindow call just before being closed.
     */
    bool queryClose();

    /**Shows or hide the parameters boxes base on the user settings.*/
    void slotUpdateParameterBar();

    /** Executes the preferences dialog.*/
    void executePreferencesDlg();

    /** Updates the widgets so that new user settings take effect.*/
    void applyPreferences();

    /**Initializes some of the variables defined in the settings (preferences).*/
    void initializePreferences();

    void slotStateChanged(const QString& state);

private Q_SLOTS:
    /** Open a file and load it into the document.*/
    void slotFileOpen();
    /** Opens a file in the old format and load it into the document.*/
    void slotFileImport();
    /** Opens a file from the recent files menu */
    void slotFileOpenRecent(const QString& url);
    /** Save the document */
    void slotFileSave();
    /** Renumbers the cluster and save the document.*/
    void slotFileRenumberAndSave();
    /** Save the document with a new filename.*/
    void slotFileSaveAs();
    /** Asks for saving if the file is modified, then closes the actual file and window*/
    void slotFileClose();
    /** Prints the views in the current display, for multiple-view displays, each view is printed on a separate page. */
    void slotFilePrint();
    /** Closes the document and quits the application.*/
    void slotFileQuit();
    /** Reverts the last user action.*/
    void slotUndo();
    /** Reverts the last undo action.*/
    void slotRedo();
    
    /** Toggles the main tool bar.*/
    void slotViewMainToolBar();
    /** Toggles the status bar.*/
    void slotViewStatusBar();
    /** Toggles the bar for the actions.*/
    void slotViewActionBar();
    /** Toggles the bar for the tools.*/
    void slotViewToolBar();
    /** Toggles the bar for the parameters.*/
    void slotViewParameterBar();
    /** Toggles the user cluster information in the cluster palette.*/
    void slotViewClusterInfo();
    
    /** Creates a new cluster display for the document and adds the new display to the
     * list of displays the document maintains.
     */
    void slotWindowNewClusterDisplay();
    /** Creates a new waveform display for the document and adds the new display to the
     * list of displays the document maintains.
     */
    void slotWindowNewWaveformDisplay();
    /** Creates a new crosscorrelation display for the document and adds the new display to the
     * list of displays the document maintains.
     */
    void slotWindowNewCrosscorrelationDisplay();
    /** Creates a new overview display for the document and adds the new display to the
     * list of displays the document maintains.
     */
    void slotWindowNewOverViewDisplay();
    
    /** Creates a new grouping assistant display for the document and adds the new display to the
     * list of displays the document maintains.
     */
    void slotWindowNewGroupingAssistantDisplay();

    /** Creates a new trace display for the document and adds the new display to the
     * list of displays the document maintains.
     */
    void slotNewTraceDisplay();
    
    /** Changes the statusbar contents for the standard label permanently, used to indicate current actions.
     * @param text the text that is displayed in the statusbar
     */
    void slotStatusMsg(const QString &text);
    /*Slots for the tools menu.*/
    /**Changes to a mode enabling the creation of a single cluster by selecting an area.*/
    void slotSingleNew();
    /**Changes to a mode enabling the creation of a multiple clusters by selecting an area.*/
    void slotMultipleNew();
    /**Changes to a mode enabling the deletion of spikes from a cluster and move them to the cluster (number 1) containing the poorly isolated cells.*/
    void slotDeleteNoise();
    /**Changes to a mode enabling the deletion of spikes from a cluster and move them to the cluster (number 0) containing the artefacts.*/
    void slotDeleteArtefact();
    /**Changes to a mode enabling the user to zoom.*/
    void slotZoom();
    /**Increases scatter plot point size (+ key).*/
    void slotIncreasePointSize();
    /**Decreases scatter plot point size (- key).*/
    void slotDecreasePointSize();
    
    /**Chooses the selection time tool, enabling the user to select a time frame
    * for which the traces are going to be displayed in the TraceView. This slot is accessible only if a TraceView and a ClusterView are present
    * and if one of the dimensions in the ClusterView is the time.*/
    void slotSelectTime();

    /** Run the 2D density-watershed splitter on the currently-shown
     *  clusters in the active scatter view.  Bound to W. */
    void slotWatershedSplit();
    
    
    /*Slots for the actions menu.*/

    /**Redraws a cluster because his color has been changed.
    * @param clusterId id of the cluster to redraw.
    */
    void slotSingleColorUpdate(int clusterId);
    /**Draws the clusters contain in @p selectedClusters list.
    * @param selectedClusters list of clusters which have been selected to be shown.
    */
    void slotUpdateShownClusters(const QList<int> &selectedClusters);
    /**Groups the clusters contain in @p selectedClusters list and trigger the update of the displays.
    * @param selectedClusters list of clusters which have been selected to be grouped.
    */
    void slotGroupClusters(QList<int> selectedClusters);
    /** Auto-merge action: template-cross-correlation between clusters; pairs
     *  scoring above threshold are grouped via the same path as Group
     *  Clusters.  Settings come from PrefAutoMerge / configuration().
     *  Patch 0069. */
    void slotAutoMerge();
    /**Calls the document to move the clusters contain in @p selectedClusters list
    * to the cluster of noise (cluster 1) and trigger the update of the displays.
    * @param selectedClusters list of clusters which have been selected to be moved
    */
    void slotMoveClustersToNoise(QList<int> selectedClusters);
    /**Calls the document to move the clusters contain in @p selectedClusters list
    * to the cluster of artefact (cluster 0) and trigger the update of the displays.
    * @param selectedClusters list of clusters which have been selected to be moved
    */
    void slotMoveClustersToArtefact(QList<int> selectedClusters);
    /**Sets the selection mode to immediate, disenabling the update action.*/
    void slotImmediateSelection();
    /**Set the selection mode to delay, enabling the update action.*/
    void slotDelaySelection();
    /**Updates the palette and the spine boxes when the active display changes.*/
    void slotTabChange(int index);
    /**Triggers an update of the dimensions due to a change of the abscissa dimension.*/
    void slotUpdateDimensionX(int dimensionX);
    void slotUpdateAutoNFeatures(int n);
    void slotUpdateRealignTopChan(int n);
    void slotDipSplit();
    /** Shift timestamps of the selected cluster by ±1 sample. */
    void slotNudgeTimestampMinus();
    void slotNudgeTimestampPlus();
    /** Move currently-selected palette cluster(s) to the end of the
     *  cluster-palette display order.  Bound to T when the palette has
     *  focus; undo-able. */
    void slotMoveSelectedClustersToEnd();

    // (slotAnnotateGood/Uncertain/Bad were removed; curation status is
    //  now inferred automatically from undo/redo behaviour — see
    //  CurationLogger::notifyUndo / notifyRedo.)

    /**Triggers an update of the dimensions due to a change of the ordinate dimension.*/
    void slotUpdateDimensionY(int dimensionYs);
    /** Closes the display and if it is the last one asks for saving, then closes the actual file and window.*/
    void slotDisplayClose();
    /**Updates the number of undo*/
    void slotUpdateUndoNb(int undoNb);
    /**Updates the number of redo*/
    void slotUpdateRedoNb(int redoNb);

    /**Informs the active display to present the waveforms for an updated time frame.*/
    void slotUpdateStartTime(int start);

    /**Informs the active display to present the waveforms for an updated time frame.*/
    void slotUpdateDuration();

    /**Sets the presentation mode of the waveform view of the active display. It can be either the sample mode
   * or the time frame mode. In the sample mode, for each shown cluster, only one out of the number of spikes
   * to be displayed will be shown, the time frame mode, for each shown cluster, only the spikes
   * within the current time frame will be shown.
   */
    void slotTimeFrameMode();

    /**Sets the waveforms of each cluster in the active display to overlay or be side by side.
   */
    void setOverLayPresentation(){
        if(overlayPresentation->isChecked())activeView()->setOverLayPresentation();
        else activeView()->setSideBySidePresentation();
    }

    /**Sets the way of presenting the information concerning the waveforms selected in the active display.
   * It can be either the mean presentation or the normal presentation. In the mean presentation,
   * there is only the waveforms of the mean and the standard deviation. In the normal presentation all the
   * waveforms for the selected (depending on the sample/time frame mode) spikes are shown.
   */
    void slotMeanPresentation(){
        if(meanPresentation->isChecked())activeView()->setMeanPresentation();
        else activeView()->setAllWaveformsPresentation();
    }

    /**Triggers the increase of the amplitude of the waveforms in the waveform view.
   */
    void slotIncreaseAmplitude(){activeView()->increaseWaveformsAmplitude();}

    /**Triggers the decrease of the amplitude of the waveforms in the waveform view.
   */
    void slotDecreaseAmplitude(){activeView()->decreaseWaveformsAmplitude();}


    /**Informs the active display to present an updated number of waveforms when the
   * waveformView is in sample mode.
   */
    void slotSpikesTodisplay(int nbSpikes){
        if(!isInit){
            activeView()->setDisplayNbSpikes(static_cast<long>(nbSpikes));
        }
    }

    /**Informs the active display to present the correlations with a new time frame.*/
    void slotUpdateCorrelogramsHalfDuration();

    /**Informs the active display to present the correlations with a new size for the bins.*/
    void slotUpdateBinSize();

    /**Triggers the increase of the amplitude of the correlograms in the correlation view.
   */
    void slotIncreaseCorrelogramsAmplitude(){activeView()->increaseCorrelogramsAmplitude();}

    /**Triggers the decrease of the amplitude of the correlograms in the correlation view.
   */
    void slotDecreaseCorrelogramsAmplitude(){activeView()->decreaseCorrelogramsAmplitude();}

    /**Present the correlograms with the raw data without appling any scale.
   */
    void slotNoScale(){activeView()->setNoScale();}

    /**Presents the correlograms scaling the data by the maximum value.
   */
    void slotScaleByMax(){activeView()->setScaleByMax();}

    /**Present the correlograms scaling the data by the shoulder value.
   */
    void slotScaleByShouler(){activeView()->setScaleByShouler();}

    /**Informs the active display to update the drawing of a doted line
   * at the shoulder level on the correlograms.*/
    void slotShoulderLine(){
        activeView()->updateShoulderLine(shoulderLine->isChecked());
    }

    /**Triggers the update of the errorMatrix view in the grouping assistant view.
   */
    void slotUpdateErrorMatrix();
    void slotShowShortcutHelp();

    /** Reorders and renumbers clusters by similarity, using whichever of
     *  the error matrix or template matrix is currently relevant (see
     *  m_lastMatrixUsed below).  Triggered by Shift+S. */
    void slotReorderClustersBySimilarity();

    /** Track which matrix view the user most recently interacted with.
     *  Connected from ErrorMatrixView::viewInteracted /
     *  TemplateMatrixView::viewInteracted (each matrix view's
     *  mouseReleaseEvent emits its signal). */
    void slotErrorMatrixInteracted();
    void slotTemplateMatrixInteracted();

    /**Select all the clusters.*/
    void slotSelectAll();

    /**Selects all the clusters except the clusters of artefact and noise
   * (clusters 0 and 1 respectively).*/
    void slotSelectAllWO01();

    /**Launchs a separate process to recluster the selected clusters.*/
    void slotRecluster();

    /** Splits the currently selected single cluster into N new clusters
     *  using a K-nearest-neighbour majority vote against a reference
     *  pool of existing well-isolated clusters (those with at least
     *  minRefClusterSize spikes, excluding artifact / MUA / source).
     *  See KlustersDoc::splitClusterByKnnVsReferences for the algorithm. */
    void slotSplitClusterByKnn();
  
    /**Opens the spike realignment pre-flight dialog and, if confirmed,
     * launches realignment on a background thread with output in a tab.*/
    void slotRealignSpikes();

    /**Run PCA-centered spike realignment over every cluster (skipping
     * noise=0 and artifact=1) using the top 2 channels per cluster.
     * Reuses the same RealignWorker as slotRealignSpikes() but iterates
     * through the cluster list sequentially, suppresses the per-cluster
     * review dialog, and auto-accepts each result as a pending change.
     * Aborts cleanly via slotAbortRealign() if the user cancels.*/
    void slotPcaAlignAllClusters();

    /**Abort a running realignment job.*/
    void slotAbortRealign();

    /**Called when the realignment worker thread finishes.*/
    void slotRealignFinished(bool ok, int nShifted, int nSwapped,
                             QVector<float> meanBefore, QVector<float> meanAfter,
                             QString backupBase, int nChan, int nSamp);

    /**Run ndm_estimatedrift on the current electrode group to produce
     * SESSION.drift.  Only the current group is used as the source;
     * the result can then be propagated to siblings via slotApplyDriftSiblings.*/
    void slotGenerateProbeDrift();

    /**Open the Drift Siblings dialog, let the user choose which sibling
     * spike groups to reprocess, then invoke ndm_applydrift (which computes
     * adaptive chunk boundaries and optionally re-runs KlustaKwik).*/
    void slotApplyDriftSiblings();

    /**Stops the separate process which is reclustering some clusters.*/
    void slotStopRecluster();

    /**Triggers the update of data incorporating the new data from the reclustering.
     *  Wired to QProcess::finished, so the parameters match Qt's signal: an
     *  exit code (unused) and an exit-status enum indicating whether the
     *  process terminated normally or crashed.
     */
    void slotProcessExited(int, QProcess::ExitStatus);

    /**Updates internal state indicating that the outputs of the separate process, which
   * has been killed, is finished.*/
    void slotOutputTreatmentOver();

    /**Launches a dialog to enable the user to change the tab label of the active display.*/
    void renameActiveDisplay();

    /**Triggers the increase of the amplitude of all the channels.
   */
    void slotIncreaseAllChannelsAmplitude(){activeView()->increaseAllChannelsAmplitude();}

    /**Triggers the decrease of the amplitude of all the channels.
   */
    void slotDecreaseAllChannelsAmplitude(){activeView()->decreaseAllChannelsAmplitude();}

    /**Enables or disables the display of labels next to the traces.*/
    void slotShowLabels();

    /**Retrieves the next cluster.*/
    void slotShowNextCluster();

    /**Retrieves the previous cluster.*/
    void slotShowPreviousCluster();

    /**Updates, if needed, the browsing possibility of the traceView.*/
    void slotSpikesDeleted();

    /**Updates the status modified of the current opend document.*/
    void slotClusterInformationModified();
    /** Raised when the cluster palette widget gains focus: switches to the
     *  first Overview Display tab and returns focus to the palette list. */
    void slotShowOverviewForPalette();

    void slotAbout();

    void slotHanbook();

    void slotSaveRecentFiles();

private:
    void readSettings();
    void initView();
    void createMenus();

    void createToolBar();

    /**Initializes the different parameter widgets.*/
    void initSelectionBoxes();
    
    /** Sets up the statusbar for the main window by initialzing a statuslabel.*/
    void initStatusBar();

    /** Creates the palette of clusters (left tool view).*/
    void initClusterPanel();

    /** Initialize the first display (create the mainDockWidget).*/
    void initDisplay();

    /**Rebuilds the ordered list of Tab-cycle focus zones from currently visible widgets.*/
    void buildFocusZones();
    /** Give keyboard focus to the most appropriate widget inside a tab page. */
    void focusTabPage(QWidget* page);

    /** Returns true when the cluster palette (or any of its descendants —
     *  the inner QListWidget, the dock-widget shell, etc.) currently holds
     *  keyboard focus.  Used by the palette-context shortcut handlers
     *  (S, T, PageUp, PageDown) to decide whether to claim the key. */
    bool paletteHasFocus() const;

    /** Returns true when the currently-focused widget is a text-entry
     *  control (QAbstractSpinBox or QLineEdit, including their internal
     *  embedded line-edits).  Used to gate single-letter shortcuts so
     *  the user can still type letters into spinboxes / line-edits in
     *  the parameter toolbar without triggering palette actions. */
    bool focusIsInTextInput() const;

    /** Shared implementation of slotNudgeTimestampMinus / Plus.  Both
     *  slots only differ in the sign of @p deltaSamples; everything else
     *  (selection guard, busy flag, status messages, palette refocus) is
     *  identical so it lives here. */
    void nudgeSelectedSingleCluster(int deltaSamples);

    /** Shared implementation of slotMoveClustersToNoise / ToArtefact.
     *  Both slots delete the selected clusters into a reserved ID
     *  (1 = noise, 0 = artefact); the status string and the reserved
     *  ID are the only differences. */
    void moveSelectedClustersToReservedId(const QList<int>& selectedClusters,
                                           int reservedId,
                                           const QString& busyMessage);

    /** Shared implementation of slotUndo / slotRedo.  Both invoke the
     *  corresponding KlustersDoc operation, refresh the traceView state,
     *  and restore focus.  @p op is a pointer-to-member of KlustersDoc;
     *  @p busyMessage is the status-bar text shown during the call. */
    void runUndoOrRedo(void (KlustersDoc::*op)(), const QString& busyMessage);

    // ── Watershed live-preview mode (Shift+W) ───────────────────────────
    // The live preview replaces the pre-2026-04 modal dialog.  Pressing
    // Shift+W enters preview mode: the selected clusters' (X, Y) feature
    // points are extracted once, the kernel runs against the active
    // scatter view's current X/Y dimensions, and a coloured-basin overlay
    // is painted on top of the scatter via ClusterView::setWatershedOverlay.
    // While in preview:
    //   ←/→     adjust smoothing sigma  (1..32 cells, ±1; ±5 with Shift)
    //   ↑/↓     adjust peak threshold   (0..50% of grid max, ±1; ±5 with Shift)
    //   Enter   commit the partition (calls watershedSelectedClusters)
    //   Esc     cancel and restore the view
    // All other keys are blocked while preview is active so the user
    // doesn't accidentally trigger another action mid-tune.
    bool                wsPreviewActive   = false;
    QList<int>          wsSel;
    QVector<double>     wsXs;
    QVector<double>     wsYs;
    int                 wsDimX     = 0;
    int                 wsDimY     = 0;
    int                 wsSigmaCells = 4;     // current sigma slider analogue
    int                 wsThreshPct  = 5;     // current threshold (% of grid max)
    double              wsGridMax    = 1.0;   // last-discovered absolute gridMax
    int                 wsCachedSigma = -1;   // sigma at which gridMax was discovered
    Watershed2D::Result wsResult;
    ClusterView*        wsScatter  = nullptr; // borrowed pointer into the active view
    KlustersView*       wsView     = nullptr; // active KlustersView at preview-entry time

    bool wsEnter();              // returns false if preconditions not met
    void wsExit(bool commit);
    void wsRecompute();          // re-runs kernel with current params, refreshes overlay
    void wsRefreshOverlay();     // builds the basin-coloured QImage and pushes to ClusterView

    // ── DipSplit (Shift+D) post-commit confirm ──────────────────────────
    // No live preview: dipSplit runs the decision and (if accepted)
    // commits immediately.  After commit, both new clusters are selected
    // in the palette and a HUD is drawn over the active scatter view
    // showing the metrics and the available next actions.  Two
    // QShortcuts on the active view widget handle the keys:
    //   Esc:    dipPostCommitUndo  → doc->undo() (single combined entry)
    //   Enter:  dipPostCommitDismiss → clears HUD, keeps the split
    // Switching views or invoking other curation actions also dismisses
    // the HUD — the shortcuts are scoped to the host view widget.
    bool                       dipPostCommitActive       = false;
    ClusterView*               dipPostCommitScatter      = nullptr;
    QShortcut*                 dipPostCommitEscShortcut    = nullptr;
    QShortcut*                 dipPostCommitEnterShortcut  = nullptr;
    QShortcut*                 dipPostCommitEnterShortcut2 = nullptr;  // numpad Enter

    void dipInstallPostCommitShortcuts(KlustersView* hostView);
    void dipClearPostCommitShortcuts();
    void dipDismissPostCommitHud();

private Q_SLOTS:
    void dipPostCommitUndo();
    void dipPostCommitDismiss();

private:
    
    /** Creates a new display.
     * @param type enum representing the type of view to be created.
     */
    void createDisplay(KlustersView::DisplayType type);

    /**Updates the active display due to a change of one of the dimensions.
    * @param dimensionX abscissa dimension.
    * @param dimensionX ordinate dimension.
    */
    void updateDimensions(int dimensionX, int dimensionY);

    /**Resets the state of the application to a none document open state.*/
    void resetState();

    /**Updates the acess to the undo/redo mechanism*/
    void updateUndoRedoDisplay();

    //Members
    
    /** A counter that gets increased each time the user creates a new display of the document with "Displays"->"New ...".*/
    int displayCount;

    /** mainDock is the main DockWidget to which all other dockWidget will be dock. Inititalized in
     * initDisplay()
     */
    DockArea* mainDock;

    /** clustersPanel is the DockWidget containing the ClusterPalette. Inititalized in initClusterPanel()
     */
    QDockWidget* clusterPanel;

    /** ClusterPalette is the Widget containing the cluster list. Inititalized in initClusterPanel()
    */
    ClusterPalette* clusterPalette;

    /**
    * Represents the document on which the application works
    */
    KlustersDoc* doc;

    /** tabsParent groups all the tabs, it is updated eache time a display is added.
    * It is null when there is only one display open. It enables to get the active tab.
    */
    QExtendTabWidget* tabsParent;

    QToolBar* paramBar;

    /**Ordered list of top-level focus zones cycled by Tab/Shift+Tab.
     * Rebuilt whenever the UI changes (panels shown/hidden, toolbar fields change).
     * Each entry is the widget that should receive setFocus() when entering that zone.*/
    QVector<QWidget*> focusZones;
    QToolBar* mActionBar;
    QToolBar* mToolBar;
    QToolBar* mClusterBar;
    QToolBar* mMainToolBar;

    QRecentFileAction* mFileOpenRecent;
    
    //Action and toolbar pointers
    QAction* viewMainToolBar;
    //QActionMenu* viewMenu;
    QAction* newClusterDisplay;
    QAction* newWaveformDisplay;
    QAction* newCrosscorrelationDisplay;
    QAction* newOverViewDisplay;
    QAction* newGroupingAssistantDisplay;
    QAction* viewActionBar;
    QAction* viewToolBar;
    QAction* viewParameterBar;
    QAction* viewClusterInfo;
    QAction* timeFrameMode;
    QAction* overlayPresentation;
    QAction* meanPresentation;
    QAction* noScale;
    QAction* scaleByMax;
    QAction* scaleByShouler;
    QAction* shoulderLine;
    QAction* showHideLabels;

    QAction *mRenameActiveDisplay;
    QAction *mCloseActiveDisplay;
    QAction *mNewTraceDisplay;
    QAction *mDeleteArtifact;
    QAction *mDeleteNoisy;
    QAction *mGroupeClusters;
    QAction *mAutoMerge;            // patch 0069 — Auto-Merge action
    QAction *mUpdateDisplay;
    QAction *mRenumberClusters;
    QAction *mReCluster;
    QAction *mSplitByKnn;
    QAction *mAbortReclustering;
    QAction *mAbortRealign;
    QAction *mRealignSpikes;
    /** PCA-centered batch realignment across every cluster (skipping
     *  noise=0 and artifact=1), using the top 2 channels per cluster.
     *  Runs sequentially in the background and auto-accepts each result
     *  as a pending change (no per-cluster review dialog).  The user can
     *  abort mid-batch via the existing "Abort Realignment" action and
     *  commit (Save) or discard (close without saving) the batch as a
     *  whole.*/
    QAction *mPcaAlignAllClusters;
    QAction *mDipSplit;
    QAction *mGenerateProbeDrift;
    QAction *mApplyDriftSiblings;
    QAction *mZoomAction;
    QAction *mIncreasePointSize;
    QAction *mDecreasePointSize;
    QAction *mNewCluster;
    QAction *mSplitClusters;
    QAction *mDeleteArtifactSpikes;
    QAction *mDeleteNoisySpikes;
    QAction *mSelectTime;
    QAction *mWatershed;
    QAction *mSelectAllAction;
    QAction *mSelectAllExceptAction;
    QAction *mSaveAction;
    QAction *mSaveAsAction;
    QAction *mCloseAction;
    QAction *mImportFile;
    QAction *mIncreaseAmplitude;
    QAction *mDecreaseAmplitude;
    QAction *mDecreaseAmplitudeCorrelation;
    QAction *mIncreaseChannelAmplitudes;
    QAction *mDecreaseChannelAmplitudes;
    QAction *mNextSpike;
    QAction *mPreviousSpike;
    QAction *mPrintAction;
    QAction *mImmediateSelection;
    QAction *mDelaySelection;
    QAction *mUndo;
    QAction *mIncreaseAmplitudeCorrelation;
    QAction *mQuitAction;
    QAction *mOpenAction;
    QAction *mRedo;
    QAction *mRenumberAndSave;
    QAction *mUpdateErrorMatrix;
    QAction *mReorderClustersBySimilarity;
    QAction *mPreferenceAction;

    QAction *mViewStatusBar;
    /**Spine box enabling to choose the abscissa dimension*/
    SpinBox* dimensionX;

    /**Spine box enabling to choose the ordinate dimension*/
    SpinBox* dimensionY;

    /**Boolean used to prevent the trigger of the spin box update during initialization.*/
    bool isInit;
    /** True while a nudge is executing OR within the post-nudge suppression
     *  window.  Prevents queued autorepeat KeyPress events from firing
     *  additional nudges after a long (multi-second) nudge loop. */
    bool nudgeInProgress{false};
    /** Timestamp of when the last nudge COMPLETED, used to suppress
     *  autorepeat events that were queued during the nudge loop. */
    QElapsedTimer lastNudgeTimer;

    /**The current number of undo used to enable/disable the the undo action.*/
    int currentNbUndo;

    /**The current number of redo used to enable/disable the the redo action.*/
    int currentNbRedo;

    /**Thread used to save a document as it can take 5 seconds. The thread is used only
    * when using the save menu, on quit if a save is need it the thread is not call.
    */
    SaveThread* saveThread;

    /**Amount of time used when looking for the spikes when the presentation mode is time frame.
    * This amount is in second and the default is 30.
    */
    long timeWindow;

    /**Starting time when looking for the spikes when the presentation mode is time frame.
    * This amount is in second and the default is 0.
    */
    long startTime;

    /**Spine box enabling to choose the start time used to display the waveforms while
    * in time frame mode.
    */
    SpinBox* start;

    /**Small box where the user can enter the width of the time frame to use
    * to display the waveforms while in time frame mode.*/
    QLineEdit* duration;

    class Validator;
    friend class Validator;

    /**
    * Represents a validator for the duration lineEdit which fix any bad entry
    * by setting the last correct value.
    */
    class Validator: public QIntValidator{

    public:
        Validator(QObject* parent):QIntValidator(parent){
            klusters = dynamic_cast<KlustersApp*>(parent);
        }
        Validator(int minimum,int maximum,QObject* parent):
            QIntValidator(minimum,maximum,parent){
            klusters = dynamic_cast<KlustersApp*>(parent);
        }
        ~Validator(){}
        void fixup (QString& input) const{
            input = QString::fromLatin1("%1").arg(klusters->timeWindow);
        }
    private:
        KlustersApp* klusters;
    };
    
    /**A validator for the time frame to use to display the waveforms while
    * in time frame mode (@see duration). The limits are zero and the maximun of time for the current document.
    */
    Validator validator;

    QLabel* featureXLabel;
    QLabel* durationLabel;
    QLabel* startLabel;

    /**Spine box enabling to choose the number of spikes to display in
    * the waveform view, if any, while that view is in sample mode.
    */
    SpinBox* spikesTodisplay;

    /**The step used to increase or decrease the number of spikes to display
    * in the waveform view, if any, while that view is in sample mode. The default
    * is 20;
    */
    long spikesTodisplayStep;
    
    QLabel* spikesTodisplayLabel;
    
    static const QString INITIAL_WAVEFORM_TIME_WINDOW;
    static const long DEFAULT_NB_SPIKES_DISPLAYED;
    /**The size of half the time frame for the correlations has to be k*bineSize,
    * as the total number of bins = 2*halfbins + 1 (halfBins.5 for each half time frame).
    */
    static const QString INITIAL_CORRELOGRAMS_HALF_TIME_FRAME;
    static const QString DEFAULT_BIN_SIZE;

    /**Length of the recording in miliseconds.*/
    long maximumTime;
    
    /**Small box where the user can enter the width of half the time frame to use
    * to compute the correlograms.*/
    QLineEdit* correlogramsHalfDuration;

    /**Small box where the user can enter the size of the bins to use
    * to compute the correlograms.*/
    QLineEdit* binSizeBox;

    /**Time frame to use to compute the correlograms.*/
    int correlogramTimeFrame;
    
    /**Size of the bins to use to compute the correlograms.*/
    int binSize;

    QLabel* correlogramsHalfDurationLabel;
    QLabel* binSizeLabel;

    class BinSizeValidator;
    friend class BinSizeValidator;

    /**
    * Represents a validator for the binSize lineEdit which fix any bad entry
    * by setting the last correct value and update the correlogramsHalfDuration
    * to ensure the relation correlogramsTimeFrame = (2k + 1) * binSize.
    */
    class BinSizeValidator: public QIntValidator{

    public:
        BinSizeValidator(QObject* parent):QIntValidator(parent){
            klusters = dynamic_cast<KlustersApp*>(parent);
        }
        BinSizeValidator(int minimum,int maximum,QObject* parent):
            QIntValidator(minimum,maximum,parent){
            klusters = dynamic_cast<KlustersApp*>(parent);
        }
        ~BinSizeValidator(){}
        void fixup (QString& input) const{
            //If the state determine in validate was invalid, fix by setting back the last correct value.
            input = QString::fromLatin1("%1").arg(klusters->binSize);
        }
        QValidator::State validate(QString &input,int& pos) const{
            QValidator::State state = QIntValidator::validate(input,pos);
            //Let the QIntValidator validates the value as to know if it is a correct integer (within the range).
            if(state != QValidator::Acceptable) return state;
            //If the value is a correct integer, update the correlogramsHalfDuration if need it.
            // correlogramTimeFrame (2 * correlogramsHalfDuration) has to be (2k + 1) * binSize.
            else{
                int sizeOfBin = input.toInt();
                int halfTimeFrame = (klusters->correlogramsHalfDuration->displayText()).toInt();

                float x = (2*static_cast<float>(halfTimeFrame)/static_cast<float>(sizeOfBin)-1)*0.5;
                int k = static_cast<int>(x + 0.5);

                klusters->correlogramTimeFrame = (2*k+1) * sizeOfBin;
                if(k != x){
                    // klusters->correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(k * sizeOfBin));
                    klusters->correlogramsHalfDuration->setText(QString::fromLatin1("%1").arg(static_cast<int>(klusters->correlogramTimeFrame / 2)));
                }
                return state;
            }
        }
    private:
        KlustersApp* klusters;
    };

    /**A validator for the bin size of the correlations.
    * The range is between 0-1 and the maximun of time for the current document in miliseconds.
    */
    BinSizeValidator binSizeValidator;

    class CorrelogramsHalfTimeFrameValidator;
    friend class CorrelogramsHalfTimeFrameValidator;

    /**
    * Represents a validator for the correlogramTimeFrame lineEdit which fix any bad entry
    * by setting the last correct value and update it
    * to ensure the relation correlogramsTimeFrame = k * binSize.
    */
    class CorrelogramsHalfTimeFrameValidator: public QIntValidator{

    public:
        CorrelogramsHalfTimeFrameValidator(QObject* parent):QIntValidator(parent){
            klusters = dynamic_cast<KlustersApp*>(parent);
        }
        CorrelogramsHalfTimeFrameValidator(int minimum,int maximum,QObject* parent):
            QIntValidator(minimum,maximum,parent){
            klusters = dynamic_cast<KlustersApp*>(parent);
        }
        ~CorrelogramsHalfTimeFrameValidator(){}
        void fixup (QString& input) const{
            int halfTimeFrame = input.toInt();

            //The value entered was not an integer
            if(halfTimeFrame == 0){
                input = QString::fromLatin1("%1").arg((klusters->correlogramTimeFrame - klusters->binSize) / 2);
                return;
            }
        }

    private:
        KlustersApp* klusters;
    };

    /**A validator for the time frame used to compute the correlations.
    */
    CorrelogramsHalfTimeFrameValidator correlogramsHalfTimeFrameValidator;

    /**Settings dialog.*/
    PrefDialog* prefDialog;

    /**Time interval between 2 lines drawn in the cluster views when the time dimension is selected.*/
    int displayTimeInterval;
    
    /**Initial gain used to display the waveforms in the waveform views.*/
    int waveformsGain;

    /**Position of the channels in the waveform views.*/
    QList<int> channelPositions;

    /**Background color for the views.*/
    QColor backgroundColor;

    /**Widget launching external process and displaying its output.*/
    ProcessWidget* processWidget;

    /**True if the external process has exited, false otherwise.*/
    bool processFinished;

    /**True if all the outputs of the external process have been printed, false otherwise.*/
    bool processOutputsFinished;

    /**Timer used to poll for process completion when slotRecluster is invoked while
     * a recluster is already in progress.  Using a stoppable QTimer (rather than
     * repeated QTimer::singleShot) lets us cancel all pending retries the moment
     * the process finishes, preventing stale timer firings from re-launching KlustaKwik.*/
    QTimer* reclusterRetryTimer;

    /**List of the clusters to recluster.*/
    QList<int> clustersToRecluster;

    /**List of the clusters created by the reclustering.*/
    QList<int> clustersFromReclustering;

    /**Path to the reclustering executable.*/
    QString reclusteringExecutable;

    /**Arguments for the reclustering executable.*/
    QString reclusteringArgs;

    /**Path to the realignment executable (currently unused — realignment is internal).*/
    QString realignExecutable;

    /**Arguments for the realignment executable (currently unused).*/
    QString realignArgs;

    /**Assemble the realign args string from the structured preferences
       (threshold / iterations / maxshift) plus the saved post-alignment mode.
       The mode token is gated by the Realign top-ch spinbox: when top-ch > 0
       the top-channel restriction is emitted and the saved PCA/RMS mode is
       suppressed (default plain xcorr), since neither post-pass honours the
       top-channel mask.*/
    QString buildRealignArgs();

    /**Scatter plot marker size in pixels — kept in sync with configuration.*/
    int markerSize;

    /**Selection polygon line width in pixels — kept in sync with configuration.*/
    int selectionLineWidth;

    /**Name of the reclustering fet file.*/
    QString reclusteringFetFileName;

    /**True if the process has been killed through Klusters.*/
    bool processKilled;

    // ── Realign worker state ─────────────────────────────────────────────────
    /**Worker object that runs realignSpikes() off the GUI thread.*/
    QObject*  realignWorker;   // RealignWorker* — stored as QObject* to avoid
                               // including realignworker.h in this header.
    /**The thread on which realignWorker runs.*/
    QThread*  realignThread;
    /**ProcessWidget tab showing realignment diagnostics.*/
    ProcessWidget* realignOutputWidget;
    /**True while a realignment job is running.*/
    bool realignRunning;
    /**The cluster ID currently being realigned (valid while realignRunning).*/
    int realignClusterId;

    // ── PCA-center batch state ───────────────────────────────────────────────
    /**True while slotPcaAlignAllClusters is iterating the cluster list.
     * Makes slotRealignFinished skip the per-cluster review dialog and
     * auto-accept the result instead.*/
    bool m_realignBatchActive;
    /**Remaining cluster IDs to process in the current batch (FIFO).*/
    QList<int> m_realignBatchQueue;
    /**Total number of clusters scheduled at batch start — used to render
     * the "(i/N)" progress prefix in the output tab.*/
    int m_realignBatchTotal;
    /**Number of clusters whose realignment has completed successfully
     * and been auto-accepted so far in this batch.*/
    int m_realignBatchAccepted;
    /**Number of clusters whose worker returned ok=false this batch.*/
    int m_realignBatchFailed;
    /**Sum of nShifted across all clusters processed in this batch.*/
    int m_realignBatchShiftedTotal;
    /**Fixed args string used for every worker invocation in the current
     * batch (built from realignArgs with --topchannels and --pca-refine
     * normalised).*/
    QString m_realignBatchArgs;
    /**Status-bar progress bar shown for the duration of a batch.  Lazily
     * created on first batch start (added as a permanent widget so it
     * stays visible regardless of which tab is active) and hidden on
     * batch completion or abort.  Kept across batches to avoid widget
     * churn — only the visibility and range/value are toggled.*/
    QProgressBar* m_realignProgressBar;

    /**Launch a single RealignWorker for @p clusterId with @p launchArgs.
     * Encapsulates the worker / thread / signal-wiring boilerplate that
     * both slotRealignSpikes and the batch driver need.  Caller is
     * responsible for the output widget and UI lock state.*/
    void startRealignWorker(int clusterId, const QString& launchArgs);

    /**True if a Error Martix exists, false otherwise.*/
    bool errorMatrixExists;
    bool templateMatrixExists;

    /** Tracks which similarity matrix the user most recently interacted
     *  with (either created or clicked).  Used to disambiguate the
     *  Shift+S reorder when both error and template matrices coexist.
     *  Updated by ERROR_MATRIX/TEMPLATE_MATRIX cases in setConnections,
     *  plus slotErrorMatrixInteracted / slotTemplateMatrixInteracted
     *  (which receive viewInteracted signals on every matrix click). */
    enum class MatrixKind { NONE, ERROR_MATRIX_KIND, TEMPLATE_MATRIX_KIND };
    MatrixKind m_lastMatrixUsed = MatrixKind::NONE;

    /**The path of the currently open document.*/
    QString filePath;

    bool useWhiteColorDuringPrinting;
    bool autoSelectFeatures;  ///< mirrors Configuration setting
    int  autoSelectNFeatures; ///< number of top-variance features to use


    QAction *featureXLabelAction;
    QAction *dimensionXAction;
    QAction *dimensionYAction;
    QLabel  *autoNFeaturesLabel;
    SpinBox *autoNFeaturesSpinBox;
    QAction *autoNFeaturesLabelAction;
    QAction *autoNFeaturesSpinBoxAction;

    QLabel  *realignTopChanLabel;
    SpinBox *realignTopChanSpinBox;
    QAction *realignTopChanLabelAction;
    QAction *realignTopChanSpinBoxAction;

    QAction *nudgeMinusAction;  ///< shift timestamps −1 sample
    QAction *nudgePlusAction;   ///< shift timestamps +1 sample
    QAction *startLabelAction;
    QAction *startAction;
    QAction *durationLabelAction;
    QAction *durationAction;
    QAction *spikesTodisplayLabelAction;
    QAction *spikesTodisplayAction;
    QAction *binSizeLabelAction;
    QAction *binSizeBoxAction;
    QAction *correlogramsHalfDurationLabelAction;
    QAction *correlogramsHalfDurationAction;


};

#endif // KCLUSTERS_H
