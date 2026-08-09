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
#include "pluginregistry.h"  // Plugins menu (discovery + descriptors)
class QMenu;
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
class MergeRecommendView;
class ClusterView;     // for watershed live-preview overlay
class SaveThread;
class PrefDialog;
class QShortcut;       // for dipsplit post-commit Esc/Enter shortcuts
class ProcessWidget;
class QProgressBar;
class QRecentFileAction;
class QExtendTabWidget;
class SerialJobQueue;   // experimental realign-via-queue lane (opt-in)

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

    /**Returns the cluster-sorting actions -- the contents of the "Sort Clusters"
     * submenu -- so other widgets (e.g. a view's right-click menu) can offer the
     * same options.  Each action keeps its own text, enabled state, and
     * triggered() connection, so adding it elsewhere needs no extra dispatch.*/
    QList<QAction*> clusterSortActions() const;

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

    /**Toggles time-chunk curation mode (prompts for chunk length on enable).*/
    void slotChunkModeToggled(bool on);
    /**Step to the next time chunk.*/
    void slotNextChunk();
    /**Step to the previous time chunk.*/
    void slotPrevChunk();

    /** Executes the preferences dialog.*/
    void executePreferencesDlg();

    /** Updates the widgets so that new user settings take effect.*/
    void applyPreferences();
    /**Push the auto-feature-selection settings onto the parameter toolbar: show or
     * hide the N-features label, spin box and "time" checkbox, and sync their
     * values from the configuration.
     *
     * Extracted from applyPreferences() because applyPreferences() runs ONLY when
     * the preferences dialog reports a change, so at startup nothing ever pushed
     * the settings onto the toolbar and the widgets kept the setVisible(false)
     * they were created with.  The constructor calls this once the widgets
     * exist.*/
    void syncAutoFeatureToolbar();

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
    /** View-menu toggle: load (lazily) + show or hide the child (.clc) palette. */
    void slotHierarchicalViewToggled(bool on);
    /** Child palette selection changed: re-scope the views to the selected
     *  child's spikes, or back to the parent unit when nothing is selected. */
    void slotChildSelectionChanged(const QList<int>& childClusters);
    /** Rebuild the child palette with the children of @p parents (no-op unless
     *  hierarchical view is visible). */
    void repopulateChildPalette(const QList<int>& parents);
    /** Hierarchy edits driven from the palettes' current selection. */
    void slotMergeFibers();
    void slotPromoteChildren();
    void slotGroupChildrenIntoFiber();
    void slotDissolveFiber();
    void slotRefiberize();
    void slotDropChildToNoise();
    void slotMergeChildren();
    /** Hierarchy > Flatten Hierarchy: collapse .clc to .clu -- every fiber becomes a single
     *  self atom (atom id == fiber id).  Confirms first -- it discards the whole
     *  sub-mode layer. */
    void slotFlattenHierarchyToClu();
    void slotUndoChildEdit();
    void slotRedoChildEdit();
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
    /** Move every cluster with fewer than N spikes into the noise cluster (1),
     *  after asking for N and a Yes/No confirmation (default Yes). */
    void slotPurgeSmallClusters();
    /** Strip feature-space artefacts: for every real cluster (id >= 2), move any
     *  spike lying more than 5 sigma from that cluster's per-dimension feature
     *  mean (in ANY single feature dimension) into the artefact cluster (0).
     *  Detection is a non-mutating two-pass scan so the exact spike count can be
     *  confirmed before anything moves; the moves are undoable. */
    void slotStripFeatureOutliers();
    /** Renumber clusters so IDs run by descending spike count (largest = 2).
     *  Clusters 0/1 untouched; undoable. */
    void slotSortClustersBySpikeCount();
    /** Renumber clusters by a nearest-neighbour chain over their MEDIAN
     *  waveforms: compute each cluster's per-sample median waveform, then walk a
     *  greedy nearest-neighbour path (Euclidean distance) so waveform-adjacent
     *  clusters get adjacent IDs.  Needs no matrix (reads the .spk file directly).
     *  Clusters 0/1 preserved at the front; undoable. */
    void slotSortByWaveformNN();
    /** Like slotSortByWaveformNN but orders clusters by the Fiedler vector of the
     *  median-waveform similarity Laplacian (spectral seriation) rather than a
     *  greedy chain, so the layout also respects GLOBAL waveform structure, not
     *  just each cluster's nearest neighbour.  Clusters 0/1 preserved; undoable. */
    void slotSortByWaveformSpectral();
    /** Renumber clusters so IDs run by ascending starting-edge time (the
     *  cluster whose earliest spike comes first becomes 2).  Needs no matrix,
     *  just spike timestamps; clusters 0/1 untouched; undoable. */
    void slotSortClustersByTime();
    /** Renumber clusters so IDs run by descending refractory contamination (the
     *  most-contaminated cluster becomes 2, surfacing it for review).  Needs no
     *  matrix, just spike timestamps; clusters 0/1 untouched; undoable. */
    void slotSortClustersByContamination();
    /** Renumber clusters so IDs run by descending mean-waveform SNR (best-SNR
     *  cluster becomes 2).  Requires computed mean waveforms; clusters without a
     *  ready cache sort last.  Clusters 0/1 untouched; undoable. */
    void slotSortClustersBySnr();
    void slotSortClustersByAmplitude();
    void slotSortClustersByAmplitudeByChannel();
    /**Recompute the merge recommendations from the active display's matrices.*/
    void slotRefreshMergeRecommendations();
    /**A recommendation was double-clicked: select that pair in the main palette.*/
    void slotRecommendationActivated(const QList<int>& clusters);
    /** Renumber clusters so IDs run by descending error-matrix merge affinity
     *  (each cluster's strongest off-diagonal probability); the best merge
     *  candidate becomes 2.  Requires a computed, up-to-date error matrix in the
     *  active display.  Clusters 0/1 untouched; undoable. */
    void slotSortClustersByErrorPval();
    /** Reorder non-special clusters by residual-matrix similarity, gated by
     *  spike count (high-count block upper-left, low-count lower-right). */
    void slotSortByResidualGated();
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
    /** Toolbar "time" checkbox: include/exclude the spike timestamp as a clustering
     *  feature when auto-selecting features for reclustering. */
    void slotUpdateIncludeTimeFeature(bool on);
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
     *  lastMatrixUsed below).  Triggered by Shift+S. */
    void slotReorderClustersBySimilarity();
    /** Reorder-by-similarity method 2: order non-special clusters by the first
     *  principal component of their fet-space centroids (no matrix needed). */
    void reorderClustersByFeatureSpace();
    /** Shared by the two median-waveform sorts: fills @p clustersOut with the
     *  non-special clusters (id >= 2) and @p distOut with their N x N Euclidean
     *  distance matrix over per-sample MEDIAN waveforms (read from the .spk file).
     *  Returns false (after a status message) if there is nothing to sort or the
     *  .spk data is unavailable.  Callers set the wait cursor. */
    bool computeMedianWaveformDistances(QList<int>& clustersOut,
                                        std::vector<float>& distOut);

    /** Track which matrix view the user most recently interacted with.
     *  Connected from ErrorMatrixView::viewInteracted /
     *  TemplateMatrixView::viewInteracted (each matrix view's
     *  mouseReleaseEvent emits its signal). */
    void slotErrorMatrixInteracted();
    void slotTemplateMatrixInteracted();
    void slotResidualMatrixInteracted();
    /** Add a ResidualMatrixView dock to the active display (Actions menu). */
    void slotNewResidualMatrix();
    /** Add a DriftMatrixView dock to the active display (Actions menu). */
    void slotNewDriftMatrix();

    /**Select all the clusters.*/
    void slotSelectAll();

    /**Selects all the clusters except the clusters of artefact and noise
   * (clusters 0 and 1 respectively).*/
    void slotSelectAllWO01();

    /**Launchs a separate process to recluster the selected clusters.*/
    void slotRecluster();

    /** Recluster the selected cluster(s) forcing the raw-waveform
     *  median-residual feature path (pooled median) for this run only (Shift+M). */
    void slotReclusterMedianResidual();
    /** Recluster the selection forcing channel-level high-variance feature
     *  selection for this run only (Shift+C). */
    void slotReclusterChannelVariance();

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
     * review dialog, and auto-accepts each result as a pending change.*/
    void slotPcaAlignAllClusters();

    /**Apply a completed single-cluster realignment result (auto-accept, cache
     * invalidation, view refresh, and the deferred post-merge step).  Handed to
     * RealignJob as its finished callback; the job owns the worker/thread
     * teardown.*/
    void applyRealignResult(bool ok, int nShifted, int nSwapped,
                            QVector<float> meanBefore, QVector<float> meanAfter,
                            QString backupBase, int nChan, int nSamp);

    /**Per-cluster progress callback for a single batch worker running
     * PCA-Center Align All (see startRealignBatchWorker / RealignWorker batch
     * mode).  Updates the progress bar and batch counters and records the
     * cluster for the deferred view refresh.*/
    void slotRealignClusterDone(int pos, int total, int clusterId,
                                bool ok, int nShifted);

    /**Called when the single batch worker finishes the whole cluster list.
     * Does the one-time batch finalise (centroid-cache teardown, deferred
     * refresh flush, summary, UI unlock).*/
    void slotRealignBatchFinished(bool ok, int nShifted, int nSwapped,
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

    // Plugins (descriptor discovery; read-only listing in v1).
    PluginRegistry mPluginRegistry;
    QMenu* mPluginsMenu = nullptr;
    QMenu* mClusterSortMenu = nullptr;   ///< the "Sort Clusters" submenu (source of clusterSortActions())
    void populatePluginsMenu();
    void slotReloadPlugins();
    /** Resolve base/group/variant/tag from the open document for a plugin's
     *  <consumes> contract. */
    QMap<QString, QString> pluginContext() const;

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
    /** Hierarchical view: dock + palette listing the children (.clc microfibers)
     *  of the unit(s) selected in the main palette.  Hidden until the View-menu
     *  toggle (mHierarchicalView) enables it. */
    QDockWidget* childPanel = nullptr;
    /** Third section of the palette stack, under the child palette: recommended
     *  PARENT merges, ranked by agreement between the error and residual
     *  matrices.  Shown/hidden with the hierarchical view, like childPanel. */
    QDockWidget*        recommendPanel = nullptr;
    MergeRecommendView* recommendView  = nullptr;
    /**Build any missing per-cluster waveform templates, which is what the
     * recommendation panel scores overlaps from and what the amplitude/SNR sorts
     * rank by.  Synchronous: see Data::buildMissingClusterTemplates() for why it
     * is not threaded.*/
    void ensureClusterTemplates();
    /** Guards ensureClusterTemplates against re-entry.  The cold build pumps the
     *  event loop to paint its progress bar, and although that pump excludes USER
     *  input it still delivers POSTED events -- including the matrix threads',
     *  whose views emit matrixUpdated(), which is wired to
     *  slotRefreshMergeRecommendations() and so back into here.  The builder
     *  mutates the template cache while iterating the cluster tables, so a
     *  re-entrant call would be operating on containers already being written. */
    bool buildingClusterTemplates = false;
    /** The child palette of the hierarchical view, shown in childPanel: it lists
     *  the children of the selected parent and carries its own (child) scope.
     *  childPalette is a NON-owning alias to it, kept so the existing hierarchy
     *  ops that act on "the focused child palette" keep working. */
    ClusterPalette* childPaletteA = nullptr;
    ClusterPalette* childPalette  = nullptr;   // alias -> the child palette (== childPaletteA)
    int parentSlotA = -1;   // parent id shown in the child palette (-1 = unassigned)
    /** Build palette @p pal scoped to the children of @p parentId (or clear it
     *  when parentId < 0).  Used to (re)assign a parent to the child palette. */
    void assignChildSlot(ClusterPalette* pal, int parentId);
    /** The child palette if it currently owns keyboard focus, else nullptr. */
    ClusterPalette* focusedChildPalette() const;

    /** The palette the user is curating, or nullptr when that is the fiber palette.
     *
     *  NOT the same question as focusedChildPalette().  Picking a pair by clicking
     *  a matrix cell puts Qt focus on the MATRIX, so a focus test answers "fiber"
     *  at exactly the moment the child palette is what is being worked in -- which
     *  is how G and T came to do nothing after a matrix click.  A child palette
     *  holding focus still counts; so does the scoped mode being on, since V is
     *  the user saying which layer they are curating.
     */
    ClusterPalette* curationPalette() const;

    /** True while assignChildSlot() is rebuilding a child palette.  The rebuild
     *  empties the list on its way to refilling it, and slotChildSelectionChanged()
     *  must not read that transient emptiness as the user deselecting everything --
     *  doing so drops the clustering scope in the middle of a child edit.
     */
    static bool childPaletteRebuilding;

    /** Sets childPaletteRebuilding for its lifetime.  Scoped so an early return in
     *  assignChildSlot cannot leave the flag stuck on. */
    struct ChildRebuildGuard {
        ChildRebuildGuard()  { KlustersApp::childPaletteRebuilding = true;  }
        ~ChildRebuildGuard() { KlustersApp::childPaletteRebuilding = false; }
    };
    /** The children currently selected in the child palette. */
    QList<int> selectedChildrenAB() const;
    /** Route a Ctrl-modified arrow / M key to the matching hierarchy operation
     *  based on the current parent/child selection, while the child view is
     *  visible.  Returns true if the key was consumed. */
    bool dispatchHierarchyKey(int key, Qt::KeyboardModifiers mods);
    /** Re-enable/disable the atom (child-layer) undo/redo menu items. */
    void refreshChildUndoActions();
    /** Cycle keyboard focus across the palettes / toolbar fields: parent ->
     *  child (when shown) -> toolbar -> parent. */
    void cycleHierarchyFocus(bool forward);
    /**Lock cluster edits if the post-edit matrix consolidation is still running,
     * releasing them (via consolidationPollTimer) once it finishes.*/
    void maybeLockEditsForConsolidation();
    void pollConsolidationUnlock();

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
    //time-chunk curation actions (see slotChunkModeToggled)
    QAction* mHierarchicalView = nullptr;   // View: toggle the child (.clc) palette
    QAction* mMergeFibers = nullptr;        // Hierarchy: merge selected fibers
    QAction* mPromoteChild = nullptr;       // Hierarchy: promote selected child(ren)
    QAction* mGroupChildren = nullptr;      // Hierarchy: group selected children into a new fiber
    QAction* mDissolveFiber = nullptr;      // Hierarchy: explode a fiber into its children
    QAction* mDropChildNoise = nullptr;     // Hierarchy: drop child(ren) to noise
    QAction* mRefiberize = nullptr;         // Hierarchy: re-cut atoms onto the current fibers
    QAction* mMergeChildren = nullptr;      // Hierarchy: merge children (atom layer)
    QAction* mMergeAllChildren = nullptr;   // Hierarchy: flatten every fiber to one self atom
    QAction* mUndoChildEdit = nullptr;      // Hierarchy: undo last atom-layer edit
    QAction* mRedoChildEdit = nullptr;      // Hierarchy: redo last atom-layer edit
    QAction* mChunkMode;
    QAction* mNextChunk;
    QAction* mPrevChunk;
    void updateChunkStatus();
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
    QAction *mPurgeSmallClusters;   // move all clusters below N spikes to noise
    QAction *mStripOutliers;        // move >5-sigma feature-space outliers to artefact(0)
    QAction *mSortClustersBySpikeCount; // renumber clusters by descending spike count
    QAction *mSortClustersByTime;       // renumber clusters by ascending starting-edge time
    QAction *mSortClustersByContamination; // renumber clusters by descending refractory contamination
    QAction *mSortByWaveformNN;         // renumber by nearest-neighbour median-waveform chain
    QAction *mSortByWaveformSpectral;   // renumber by spectral (Fiedler) median-waveform seriation
    QAction *mSortClustersBySnr;         // renumber clusters by descending mean-waveform SNR
    QAction *mSortClustersByAmplitude;   // renumber clusters by descending peak-to-trough amplitude
    QAction *mSortClustersByAmplitudeByChannel; // as above, blocked by peak channel
    QAction *mSortClustersByErrorPval;   // renumber clusters by descending error-matrix affinity
    QAction *mSortByResidualGated;      // residual-matrix sort, gated by spike count
    /** Last spike-count threshold used by slotPurgeSmallClusters (remembered
     *  for the session; the purge dialog is pre-filled with it). */
    int purgeSmallClusterThreshold{10};
    QAction *mUpdateDisplay;
    QAction *mRenumberClusters;
    QAction *mReCluster;
    QAction *mReclusterMedian;
    QAction *mReclusterChannelVar;

    /** One-shot recluster mode requested via a keyboard shortcut (Shift+M /
     *  Shift+C); consumed by the next slotRecluster() that proceeds past the
     *  busy-retry guard, so a retry preserves the request. */
    enum class ReclusterOnce { None, MedianResidual, ChannelVariance };
    ReclusterOnce reclusterOnce = ReclusterOnce::None;

    QAction *mSplitByKnn;
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
    QAction *mNewResidualMatrix;
    QAction *mNewDriftMatrix;
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
        explicit Validator(QObject* parent):QIntValidator(parent){
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
        explicit BinSizeValidator(QObject* parent):QIntValidator(parent){
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
        // Deliberately a plain range check with no side effects.  This used to
        // divide by the number being typed and rewrite the Duration box from
        // inside validate(), which Qt calls on EVERY keystroke -- so a partial
        // entry ("1" on the way to "15") snapped the Duration to fit a bin size
        // the user had not finished typing, and the boxes fought back as you
        // typed.  The (2k+1)*binSize relation is now applied once, on
        // returnPressed, in KlustersApp::slotUpdateBinSize(), which mirrors what
        // slotUpdateCorrelogramsHalfDuration() already did for the other box.
        QValidator::State validate(QString &input,int& pos) const{
            return QIntValidator::validate(input,pos);
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
        explicit CorrelogramsHalfTimeFrameValidator(QObject* parent):QIntValidator(parent){
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

    // ── Realign via SerialJobQueue ───────────────────────────────────────────
    /**Serialised lane for the single-cluster realign path (the only path now;
     * the legacy direct worker spin-up has been removed).  Created lazily on
     * first use.  Holding the lane for the whole realign serialises any
     * follow-on renumber/matrix work after it, which is what the merge race
     * needed.*/
    SerialJobQueue* realignQueue;
    /**Run the single-cluster realignment as a RealignJob on realignQueue, with
     * applyRealignResult as the finished callback; the job owns the worker/
     * thread lifecycle (incl. teardown).*/
    void enqueueRealignJob(int clusterId, const QString& args);
    /**True while a realignment job is running.*/
    bool realignRunning;
    /**The cluster ID currently being realigned (valid while realignRunning).*/
    int realignClusterId;

    // ── PCA-center batch state ───────────────────────────────────────────────
    /**True while slotPcaAlignAllClusters is iterating the cluster list.
     * The batch worker auto-accepts each result; per-cluster progress is
     * reported via slotRealignClusterDone and finalised in
     * slotRealignBatchFinished.*/
    bool realignBatchActive;
    /**When a batch was launched by the post-edit auto-realign (startPostOpRealign),
     * the batch-finish handler runs the renumber + matrix recompute that the inline
     * path would otherwise do.  realignPostOpSetChanged carries the clusterSetChanged
     * flag through to that step.*/
    bool realignPostOpRenumberMatrix = false;

    /**True while a post-edit matrix consolidation is running and cluster edits
     * are locked (see maybeLockEditsForConsolidation).*/
    bool editConsolidationLock = false;
    /**Polls the error-matrix compute so the edit lock is released as soon as the
     * consolidation finishes.  A poll is robust to superseded / no-view computes
     * that never emit matrixUpdated(), which a signal-driven lock would miss.*/
    QTimer* consolidationPollTimer = nullptr;
    bool realignPostOpSetChanged = false;
    /**Total number of clusters scheduled at batch start — used to render
     * the "(i/N)" progress prefix in the output tab.*/
    int realignBatchTotal;
    /**Number of clusters whose realignment has completed successfully
     * and been auto-accepted so far in this batch.*/
    int realignBatchAccepted;
    /**Number of clusters whose worker returned ok=false this batch.*/
    int realignBatchFailed;
    /**Sum of nShifted across all clusters processed in this batch.*/
    int realignBatchShiftedTotal;
    /**Cluster IDs accepted during the current batch whose view refresh
     * (cache invalidation + forceClusterRefresh) has been deferred to batch
     * end.  Per-cluster refresh emits spikesAddedToCluster, which puts every
     * shown sub-view into REDRAW mode and launches waveform/correlogram
     * threads against the (131 GB) .spk.pending — doing that once per cluster
     * dominated the inter-cluster gap, so it is batched and flushed once when
     * the run finishes (or aborts) via flushRealignBatchRefresh().*/
    QList<int> realignBatchTouched;
    /**Fixed args string used for every worker invocation in the current
     * batch (built from realignArgs with --topchannels and --pca-refine
     * normalised).*/
    QString realignBatchArgs;
    /**Status-bar progress bar shown for the duration of a batch.  Lazily
     * created on first batch start (added as a permanent widget so it
     * stays visible regardless of which tab is active) and hidden on
     * batch completion or abort.  Kept across batches to avoid widget
     * churn — only the visibility and range/value are toggled.*/
    QProgressBar* realignProgressBar;

    /** Status-bar progress bar for the waveform cluster sorts (the median-read
     *  and distance phases inside computeMedianWaveformDistances).  Created on
     *  first use as a permanent widget so it survives tab changes, and hidden
     *  when the sort finishes; mirrors realignProgressBar's lifecycle. */
    QProgressBar* sortProgressBar;

    /**Show the shared status-bar progress bar with @p format (a QProgressBar
     * format string, so it should contain %p%) and pump events once so it paints.
     *
     * The three of these exist because more than one long operation needs the
     * bar, and a second copy of the lazy-construct/show/hide dance would be a
     * second chance to leave it stranded on screen.
     *
     * Events are pumped with user input EXCLUDED: the bar repaints, but the user
     * cannot re-enter the operation that is already running.*/
    /**Common prologue for the metric sorts: verify @p action is live and a
     * document and display exist, then collect the non-noise clusters (id >= 2).
     * Reports and returns an EMPTY list when the sort should not proceed, so the
     * caller's whole guard is `if (clusters.isEmpty()) return;`.
     *
     * @p sortName is the human name used in every message ("spike count", "SNR"),
     * so the wording of one sort cannot drift from another's.
     *
     * This exists because the skeleton was copied per sort and the copies had
     * already drifted -- see the two single-linkage implementations that diverged
     * by an entire order of complexity, and the three amplitude/SNR accessors that
     * shared one bug.*/
    QList<int> clustersToSort(const QAction* action, const QString& sortName);

    /**Common epilogue: hand @p order to the doc layer, which owns the undo, log,
     * palette and view bookkeeping, and report the outcome.  @p detail is the
     * parenthetical ("largest first").*/
    void applySortedOrder(const QList<int>& order, const QString& sortName,
                          const QString& detail);

    void beginSortProgress(const QString& format);
    /** Nesting depth for the shared progress bar.  Both long operations that use
     *  it pump the event loop, and that pump can deliver a posted event which
     *  starts the other one (a matrix landing mid-sort reaches
     *  slotRefreshMergeRecommendations and so ensureClusterTemplates).  Without a
     *  depth count the inner operation's endSortProgress() would hide the bar
     *  while the outer one was still running, and the outer would then drive a
     *  bar nobody can see.  The outermost caller owns it. */
    int sortProgressDepth = 0;
    /**Set the bar to @p percent (clamped to 0..100) and pump events.  A no-op if
     * beginSortProgress() was not called.*/
    void updateSortProgress(int percent);
    /**Hide the bar.  Safe to call when it was never shown.*/
    void endSortProgress();

    /**Set up the realign output tab, lock the UI, and launch the realignment
     * worker for a single @p clusterId using the current saved settings
     * (buildRealignArgs()).  No dialog is shown.  Used by slotRealignSpikes
     * and by the auto-align-after-merge path.  Caller must ensure doc is
     * open and no realignment is already running.*/
    void startRealignForCluster(int clusterId);
    /// Run the configured post-merge automation (renumber clusters and/or
    /// recompute matrices) once a merge operation has completed.  No-op unless
    /// the corresponding Preferences > Refinement options are enabled.
    /// Thin wrapper over autoPostClusterEdit(true) — a merge always changes the
    /// cluster set.
    void autoPostMerge();

    /// Generalised post-edit automation, shared by merge and the other
    /// cluster-editing operations.  Renumber runs only when @p clusterSetChanged
    /// (a cluster was added or removed) AND auto-renumber is enabled; the matrix
    /// recompute runs whenever auto-update-matrices is enabled.  Order is
    /// renumber → matrix so the matrices recompute against the final ids.
    void autoPostClusterEdit(bool clusterSetChanged);

    /// Coalesced, deferred entry point for autoPostClusterEdit, connected to the
    /// document's forward cluster-mutation signals.  Defers to the next
    /// event-loop turn (so the triggering mutation completes first — never
    /// re-entrant) and collapses multiple signals from one operation into a
    /// single run.  @p clusterSetChanged is OR-accumulated across the coalesced
    /// signals.
    void scheduleAutoPostClusterEdit(bool clusterSetChanged);
    bool autoPostEditPending    = false;
    bool autoPostEditSetChanged = false;

    /// Coalesced, deferred entry point for slotRefreshMergeRecommendations().
    ///
    /// The refresh is reachable several times over for ONE user action: an edit
    /// emits hierarchyChanged, and then the error and residual matrices each land
    /// their recomputed result and emit matrixUpdated -- three refreshes for one
    /// merge.  Selection is worse: slotUpdateShownClusters() refreshes on every
    /// change, so holding an arrow key down in the palette fires one per
    /// keystroke.  Each refresh costs an O(clusters^2) error-gate sweep on this
    /// thread (~0.5 s at 8736 clusters), so the duplicates are the difference
    /// between a hitch and a stall.
    ///
    /// Collapsing them to one run per event-loop turn is safe because the refresh
    /// is a pure READER -- it recomputes the panel from whatever the current state
    /// is -- so a coalesced run produces exactly what the last of the collapsed
    /// runs would have.  Mirrors scheduleAutoPostClusterEdit above.
    void scheduleRefreshMergeRecommendations();
    bool refreshRecommendPending = false;

    /**Start ONE worker that processes the whole @p clusterIds list back-to-back
     * in a single thread (RealignWorker batch mode), wired to
     * slotRealignClusterDone (progress) and slotRealignBatchFinished (finalise).
     * Replaces the per-cluster startRealignWorker loop for PCA-Center Align All
     * so the ~450ms/cluster orchestration round-trip is paid once.*/
    void startRealignBatchWorker(const QList<int>& clusterIds,
                                 const QString& launchArgs);

    /**Launch a batch realign of the parent fibers an edit just created/modified
     * (drained from KlustersDoc::takeModifiedFibers), locking the change-initiating
     * actions and showing the progress bar.  Sets realignPostOpRenumberMatrix so the
     * batch-finish handler runs renumber + matrix.  Mirrors the Align-All launch but
     * scoped to the given fibers and with no confirm dialog.*/
    void startPostOpRealign(const QList<int>& fibers, bool setChanged);

    /**Apply the doc's pending post-edit fiber selection (KlustersDoc::
     * takePendingFiberSelection): switch to parent scope and select/focus the
     * produced fibers.  Called as the LAST step of the post-edit flow -- after the
     * async realign + renumber -- so it is authoritative regardless of what the
     * realign/refresh/tab-switch left selected.  No-op when nothing is pending
     * (e.g. atom-only ops, which select via hierarchyChildrenCreated instead).*/
    void applyPendingFiberSelection();

    /**Flush the deferred view refresh accumulated in realignBatchTouched:
     * invalidate the waveform/correlogram caches for every touched cluster,
     * then force one refresh pass.  Called once when a PCA-Center Align All
     * batch finishes or is aborted, instead of per cluster.*/
    void flushRealignBatchRefresh();

    /**True if a Error Martix exists, false otherwise.*/
    bool errorMatrixExists;
    bool templateMatrixExists;

    /** Tracks which similarity matrix the user most recently interacted
     *  with (either created or clicked).  Used to disambiguate the
     *  Shift+S reorder when both error and template matrices coexist.
     *  Updated by ERROR_MATRIX/TEMPLATE_MATRIX cases in setConnections,
     *  plus slotErrorMatrixInteracted / slotTemplateMatrixInteracted
     *  (which receive viewInteracted signals on every matrix click). */
    enum class MatrixKind { NONE, ERROR_MATRIX_KIND, TEMPLATE_MATRIX_KIND, RESIDUAL_MATRIX_KIND };
    MatrixKind lastMatrixUsed = MatrixKind::NONE;

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
    QCheckBox *autoNFeaturesTimeCheckBox;        ///< include timestamp as a recluster feature
    QAction   *autoNFeaturesTimeCheckBoxAction;

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
