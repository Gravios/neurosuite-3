/***************************************************************************
                          klustersdoc.h  -  description
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

#ifndef KLUSTERSDOC_H
#define KLUSTERSDOC_H

#include "itemcolors.h"

//include files for the application
#include "data.h"
#include "tracesprovider.h"
#include <klustersshared/channelcolors.h>
#include "clustersprovider.h"


// include files for QT
#include <QObject>
#include <QVector>
#include <QString>
#include <QPoint>
#include <QFileInfo>
#include <functional>


#include <QList>
#include <QFile>
#include <QEvent>
#include <QDebug>
#include <vector>




// forward declaration
class KlustersView;
class KlustersApp;
class AutoSaveThread;
class ClusterPalette;
/**
  * The KlustersDoc class provides a document object that can be used in conjunction with the classes
  * KlustersApp and KlustersView to create a document-view model for MDI (Multiple Document Interface)
  * based on KApplication and KDockMainWindow as main classes.
  * Thereby, the document object is created by the KlustersApp instance and contains
  * the document structure with the according methods for manipulating the document
  * data by KlustersView objects. Also, KlustersDoc contains the methods for serialization of the document data
  * from and to files.
  * @author Lynn Hazan
  */
class KlustersDoc : public QObject
{
    Q_OBJECT

    friend class KlustersView;

public:

    /**Information retun after a call to openFile/saveDocument/createFeatureFile*/
    enum OpenSaveCreateReturnMessage {OK=0,OPEN_ERROR=1,DOWNLOAD_ERROR=3,INCORRECT_FILE=4,SAVE_ERROR=5,
                                      UPLOAD_ERROR=6,INCORRECT_CONTENT=7,CREATION_ERROR=8,SPK_DOWNLOAD_ERROR=9,FET_DOWNLOAD_ERROR=10,
                                      PAR_DOWNLOAD_ERROR=11,PARX_DOWNLOAD_ERROR=12,NOT_WRITABLE=14,PARSE_ERROR=15};
    
    /** Constructs a document.
    * @param parent the parent QWidget.
    * @param clusterPalette a reference to the cluster Palette.
    * @param autoSave boolean indicating if a crash and recovery save should be performed.
    * @param savingInterval initial time interval between 2 crash and recovery saves.
     */
    KlustersDoc(QWidget* parent,ClusterPalette& clusterPalette,bool autoSave,int savingInterval);
    /** Destructor for the fileclass of the application */
    ~KlustersDoc();

    /** Adds a view to the document which represents the document contents. Usually this is your main view. */
    void addView(KlustersView* view);
    /** Removes a view from the list of currently connected views. */
    void removeView(KlustersView* view);
    
    /** Returns the first view instance. */
    KlustersView* firstView(){return viewList->isEmpty() ? 0 : viewList->first();}
    
    /**Returns true, if the requested view is the last view of the document. */
    bool isLastView();
    
    /** This method gets called when the user is about to close the last view. If the document is
    * modified, the user gets asked if he wants to save the document.
    */
    bool canCloseView();
    /** Sets the modified flag for the document after a modifying action on a view connected to the document.*/
    void setModified(bool m = true){ modified=m; }
    /** Returns if the document is modified or not. Use this to determine if your document needs saving by the user on closing.*/
    bool isModified(){ return modified; }
    /** Opens the document by filename and format.
    * @return an OpenSaveCreateReturnMessage enum giving the open status.
    */
    int openDocument(const QString &url,QString& errorInformation, const char* format=0);
    /**Opens a document using different format, than the one defined for the application, by filename and format.
    * Not Yet implemented.
    */
    bool importDocument(const QString &url, const char* format=0);

    /** Closes the actual document.*/
    void closeDocument();

    /**Verifies if the document can be close.
    * @param mainWindow the main window calling this method.
    * @param callingMethod the mainWindow's method which call this method.
    * @return true if the document can be close, false if there still thread running and
    * the document could not be close.
    */
    bool canCloseDocument(KlustersApp* mainWindow, const QString &callingMethod);
    
    /** Saves the document under the file name containes in @p url.
    * @return an OpenRetunMessage enum giving the open status
    */
    int saveDocument(const QString &url, const char* format=0);
    /**Returns the QString of the document. */
    const QString& url() const{return docUrl;}
    /**Sets the URL of the document. */
    void setURL(const QString& url){docUrl=url;}
    /**Sends back the full name of the document with the electrode group Id append.*/
    QString documentName() const;

    /**Returns the base name of the document (common name for all the files). */
    QString documentBaseName() const;

    /**Sends back the directory where is store the document.*/
    QString documentDirectory() const;

    /**Returns the reference on the list of ClusterColor objects.
    * @return ItemColors containing the information on the clusters and their associated color.
    */
    ItemColors& clusterColors() const {return *clusterColorList;}

    /**Returns a reference on data (object containing all the information).
    * @return data object.
    */
    Data& data() const {return *clusteringData;}

    /**Manages the color change of a single cluster.
    * Method call when the palette is in immediat mode (no need to press the update buton to trigger the change)
    * @param clusterId cluster having is color changed.
    * @param activeView the view in which the change has to be immediate.
    */
    void singleColorUpdate(int clusterId,KlustersView& activeView);

    /**Manages the update in the selection of clusters to be shown.
    * @param clustersToShow list of clusters to be drawn.
    * @param activeView the view in which the change has to be immediate.
    */
    void shownClustersUpdate(const QList<int>& clustersToShow,KlustersView& activeView);

    /**Updates the selection of clusters to be shown in the active view due to
    * a selection in the error matrix.
    * @param clustersToShow list of clusters to be drawn.
    */
    void shownClustersUpdate(const QList<int>& clustersToShow);

    /**Updates the selection of clusters to be shown in the active view due to
    * a selection in the error matrix.
    * @param clustersToShow list of clusters to be drawn.
    * @param previousSelectedClusterPairs list of clusters corresponding to the clusters previous selected in the error matrix.
    */
    void shownClustersUpdate(const QList<int>& clustersToShow,const QList<int>& previousSelectedClusterPairs);

    /**Updates the selection of clusters to be shown in the active view due to
    * a selection in the error matrix.
    * @param clustersToShow list of clusters to be drawn in addition to those already shown.
    */
    void addClustersToActiveView(const QList<int>& clustersToShow);

    /**Updates the selection of clusters to be shown by showing all the clusters
    * except those contained in @p clustersToHide.
    * @param clustersToHide list of clusters to not show.
    */
    void showAllClustersExcept(const QList<int>& clustersToHide);
    
    /**Manages the grouping of clusters.
    * @param clustersToGroup list of clusters to be grouped.
    * @param activeView the view in which the change has to be immediate.
    */
    void groupClusters(QList<int> clustersToGroup,KlustersView& activeView);

    /**Manages the deletion of clusters, if @p clusterId is 0, the clusters are moved to cluster 0 (cluster of artefact)
    * if @p clusterId is , the clusters are moved to cluster 1 (cluster of noise).
    * @param clustersToDelete list of clusters to be deleted.
    * @param activeView the view in which the change has to be immediate.
    * @param clusterId the id of the cluster to where the clusteres in clustersToDelete will be moved.
    */
    void deleteClusters(QList<int> clustersToDelete,KlustersView& activeView,int clusterId);

    /**
    * Removes spikes from some clusters and assign them to the cluster 1, the cluster for the noise.
    * @param region the polygon defining the area containing the spikes corresponding to the noise.
    * @param clustersOfOrigin a list of the cluster numbers (in ascending order) identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as absciss to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    */
    void deleteNoise(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**
    * Removes spikes from some clusters and assign them to the cluster 0, the cluster for the artefact.
    * @param region the polygon defining the area containing the spikes corresponding to the noise.
    * @param clustersOfOrigin a list of the cluster numbers (in ascending order) identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as absciss to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    */
    void deleteArtifact(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**
    * Creates a new cluster out of existing ones.
    * @param region the polygon defining the area containing the spikes for the new cluster.
    * @param clustersOfOrigin a list of the cluster numbers identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as absciss to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    * @return the number of the newly created cluster.
    */
    void createNewCluster(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**
    * Creates a new clusters out of existing ones. If the polygon of selection contains x clusters
    * x new clusters will be created.
    * @param region the polygon defining the area containing the spikes for the new cluster.
    * @param clustersOfOrigin a list of the cluster numbers identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as absciss to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    * @return a list of the numbers of the newly created clusters.
    */
    void createNewClusters(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**Returns the number of dimensions of the data.*/
    int nbDimensions(){return clusteringData->nbOfDimensions();}

    /** Reverts the last user action.*/
    void undo();

    /** Reverts the last undo action.*/
    void redo();

    /**Returns the temporary file corresponding to the cluster file.*/
    QString temporaryFile() const {return tmpCluFile;}

    /**Returns the temporary file corresponding to the spike file.*/
    QString getSpikeFileName() const {return tmpSpikeFile;}

    /**Returns the maximum value for the time dimension in second.*/
    long maxTime() const{return clusteringData->maxTime();}

    /**Returns the total number of spikes of the current document.
    * @return the total number of spikes.
    */
    long totalNbOfSpikes() const { return clusteringData->totalNbOfSpikes();}

    class CloseDocumentEvent;
    friend class CloseDocumentEvent;

    CloseDocumentEvent* getCloseDocumentEvent(const QString& origin){
        return new CloseDocumentEvent(origin);
    }

    /**
    * Internal class use to send information to the main window to inform it that
    * the document could not be closed has there still have thread running.
    */
    class CloseDocumentEvent : public QEvent{
        //Only the method getCloseDocumentEvent of KlustersDoc has access to the private part of CloseDocumentEvent,
        //the constructor of CloseDocumentEvent being private, only this method con create a new CloseDocumentEvent
        friend CloseDocumentEvent* KlustersDoc::getCloseDocumentEvent(const QString& origin);

    public:
        QString methodOfOrigin(){return origin;}
        ~CloseDocumentEvent(){}

        CloseDocumentEvent(const QString &origin):QEvent(QEvent::Type(QEvent::User + 400)),origin(origin){}

        QString origin;
    };

    /**Sets the auto saving on for the future documents to be opened.
    * @param interval saving interval.
    */
    void setAutoSaving(int interval){
        savingInterval = interval;
        endAutoSaving = false;
        autoSave = true;
    }
    
    /**Updates the time interval use for the auto saving of the document. Starts the auto saving if it was off.
    * @param interval saving interval.
    */
    void updateAutoSavingInterval(int interval);
    
    /**Stops the auto saving of the document.
   * @param currentDocument true if the auto saving is stopped only for the currently open document, false if it is
   * a change in the settings which triggered this call and therefore the auto saving is disabled completely.
   * @return true if the autoSaving thread has been delete false otherwise.
   */
    bool stopAutoSaving(bool currentDocument = false);

    void customEvent (QEvent* event) override;
    /**Sets the acquisition system gain.
    * @param acquisitionGain acquisition system gain.
    */
    void setGain(int acquisitionGain);
    
    /**Updates the time interval between time lines drawn in the cluster views for the time dimension.
  * @param step the interval to use in second.
  */
    void setTimeStepInSecond(int step);

    /**Initialize the position of the channels in the waveform views.
  * @param positions positions of the channels to use in the view set by the user in the settings dialog.
  */
    void setChannelPositions(QList<int>& positions);

    /**Returns the number of channels used.*/
    int nbOfchannels() const{return clusteringData->nbOfchannels();}

    /**Returns the total number of PCAs used
  * (number of channels times number of PCA by channel).*/
    int totalNbOfPCAs() const{return clusteringData->totalNbOfPCAs();}
    /** Per-feature variance for spikes in @p clusterId (delegates to Data). */
    QVector<double> computeFeatureVariancesForCluster(int clusterId) const {
        return clusteringData->featureVariancesForCluster(clusterId);
    }
    /** Per-feature variance pooled across all spikes in @p clusterIds. */
    QVector<double> computeFeatureVariancesForClusters(const QList<int>& clusterIds) const {
        return clusteringData->featureVariancesForClusters(clusterIds);
    }
    
    /**Makes all the internal changes due to a modification of the number of undo.
  * @param newNbUndo the futur new number of undo.
  */
    void nbUndoChangedCleaning(int newNbUndo);

    /**Updates the background color used in the views.
  * @param backgroundColor color of the new background.
 */
    void setBackgroundColor(const QColor& backgroundColor);

    /**Sets the scatter plot marker size on all ClusterViews.*/
    void setMarkerSize(int size);

    /**Sets the selection polygon line width on all ClusterViews.*/
    void setSelectionLineWidth(int w);

    /**Creates the feature file to automatically recluster the clusters contained in @p clustersToRecluster.
  * @param clustersToRecluster list of clusters to recluster.
  * @param reclusteringFetFileName name for the reclustering fet file.
  * @return the creation status as a OpenSaveCreateReturnMessage enum.
  */
    int createFeatureFile(QList<int>& clustersToRecluster, const QString &reclusteringFetFileName);

    /**
     * Re-aligns the spikes of @p clusterId to their true peak position.
     *
     * For each spike: reads the waveform from .spk.N, finds the true peak,
     * computes the shift, re-extracts the waveform at the corrected offset
     * from the raw .dat file, updates .res.N / .spk.N in place, and calls
     * process_refeaturize to recompute features using the saved .pca.N file.
     * If a timestamp change would violate sorted order, the affected pair
     * of entries in .res/.spk/.clu/.fet is swapped.  The in-memory arrays
     * (features, spikesByCluster) are updated accordingly.
     *
     * @param clusterId  Cluster to realign.
     * @param logOut     Receives human-readable progress / warning messages.
     * @param nShifted   Set to the number of spikes that were shifted.
     * @param nSwapped   Set to the number of sort-order swaps performed.
     * @return true on success.
     */
    bool realignSpikes(int clusterId, QString& logOut, int& nShifted, int& nSwapped,
                       std::function<void(const QString&,bool)> liveLog = nullptr,
                       const QString& args = QString(),
                       QVector<float>* meanBefore = nullptr,
                       QVector<float>* meanAfter  = nullptr,
                       QString*        backupBase  = nullptr);

    /**Invalidates the in-memory waveform cache for @p clusterId.
     * Call this after any in-place modification of the .spk file (e.g. after
     * spike realignment) so the waveform viewer re-reads from disk.
     */
    void invalidateWaveformCache(int clusterId);
    void invalidateCorrelogramCache(int clusterId);

    // ── Pending realignment ────────────────────────────────────────────────
    /** One modified spike record, held in memory until saveDocument() writes
     *  it to disk.  Original values are stored so rejectLastRealign() can
     *  undo the in-memory Data changes without touching disk. */
    struct PendingSpkRecord {
        int64_t              destPos;    // 0-based global spike position in file
        std::vector<int16_t> spkRow;    // sample-major waveform for .spk
        int64_t              ts;        // new timestamp for .res
        std::vector<int64_t> fetRow;   // new feature row for .fet (length timeDim)
        // --- originals for reject ---
        int64_t              origTs;
        QList<dataType>      origFet;  // original feature values (length nFeatCols)
    };

    struct PendingRealign {
        // Paths are stored doc-level (m_origSpkPath etc.); only per-batch
        // metadata and the per-spike undo records are needed here.
        int64_t bytesPerSpike = 0;
        int     spkElems      = 0;
        int     timeDim       = 0;
        int     nFeatCols     = 0;
        std::vector<PendingSpkRecord> records;
    };

    /** True when realignment results are waiting to be written to disk. */
    bool hasPendingRealign() const { return !m_pendingRealign.empty(); }

    /** Discards the most recent pending realignment and restores the
     *  in-memory Data to its state before that realignment was run.
     *  Called when the user hits Reject in the review dialog. */
    void rejectLastRealign();

    /**Integrates in the data the clusters obtained by automatic reclustering.
  * Suppress the reclustered ones and add the newly created ones.
  * @param clustersToRecluster list of clusters reclustered.
  * @param reclusteredClusterList output parameter, the list of the newly created clusters.
  * The list will be empty if the integration is not successful.
  * @param reclusteringFetFileName name of the reclustering fet file.
  * @return the integration status as a OpenSaveCreateReturnMessage enum.
  */
    int integrateReclusteredClusters(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList,QString reclusteringFetFileName);

    /** Returns the id of the electrode group correponding to the current document.
  * @return the id of the electrode group.
  */
    QString currentElectrodeGroupID() const {return electrodeGroupID;}

    /**Returns the list of spike group IDs that share the same probeId as
     * electrodeGroupID in the YAML parameter file.  Used by the drift-sibling
     * workflow to identify which groups to reprocess when probe drift is
     * estimated from a single curated shank.
     * When no probeId fields are present, all groups default to probe 0 so
     * every group is a sibling of every other group.
     */
    QList<int> getSiblingElectrodeGroups(int electrodeGroupID) const;

    /**Updates the views to take into account the clusters obtained by automatic reclustering.
  * Suppress the reclustered ones and add the newly created ones.
  * @param clustersToRecluster list of clusters reclustered.
  * @param reclusteredClusterList output parameter, the list of the newly created clusters.
  */
    void reclusteringUpdate(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList);

    /**
  * Informs if the variables need it by the traceView are available. Those variables are retrieve only from
  * the YAML parameter file.
  * @return true if the variables are available, false otherwise.*/
    bool isTraceViewVariablesAvailable()const {return clusteringData->isTraceViewVariablesAvailable();}

    /**
  * Informs if the data need it by the traceView are available.
  * @return true if the data are available, false otherwise.*/
    bool areTraceDataAvailable()const {
        QFileInfo docInfo(docUrl);
        const QString datUrl = docInfo.absolutePath() + "/" + docInfo.baseName() +".dat";
        QFileInfo datFileInfo = QFileInfo(datUrl);
        if(!datFileInfo.exists()) return false;
        else return true;
    }

    /**
  * Informs if a TracesProvider exits.
  * @return true if the provider exists, false otherwise.*/
    bool isTracesProvider() const{
        if(tracesProvider == nullptr) return false;
        else return true;
    }

    /**Creates providers which will provide data to the TraceView. A TracesProvider which will provide the channel data and
  * a TracesProvider which will provide the cluster data.*/
    void createProviders();
    
    /**Gets the acquisition system gain.
  * @return current acquisition gain.
  */
    int getAcquisitionGain()const{return acquisitionGain;}

    /**Gets the current gain based on the screen gain and the acquisition system gain.
  * @return current gain.
  */
    int getGain()const{return gain;}

    /**Returns a pointer on the list of ChannelColors objects used to represent the channel colors used in TraceView.
  * @return ChannelColors containing the information on the channels and their associated color.
  */
    ChannelColors* channelColors() const {return channelColorList;}

    /**Returns a reference on the Map given the correspondance between the channel ids and the display group ids.*/
    QMap<int,int>* getDisplayChannelsGroups() {return &displayChannelsGroups;}

    /**Returns a reference on the map given th correspondance between the display group ids and the channel ids.
  */
    QMap<int, QList<int> >* getDisplayGroupsChannels() {return &displayGroupsChannels;}

    /**Returns a reference on the Map given the correspondance between the channel ids and the spike group ids.
  */
    QMap<int,int>* getChannelsSpikeGroups() {return &channelsSpikeGroups;}

    /**Returns a reference on the map given th correspondance between the spike group ids and the channel ids.
  */
    QMap<int, QList<int> >* getSpikeGroupsChannels() {return &spikeGroupsChannels;}

    /**Returns the list of channels of the current electrode.*/
    QList<int>& getCurrentChannels() {return clusteringData->getCurrentChannels();}

    /**Returns a map given the list of cluster file containing data for a given display group.
  * This is used in the TraceView.*/
    QMap<int, QList<int> >* getDisplayGroupsClusterFile() {return &displayGroupsClusterFile;}

    /**Returns a pointer to the TraceProvider.*/
     TracesProvider* getTraceProvider()const{return tracesProvider;}

    /**Returns a pointer to the ClustersProvider.*/
    ClustersProvider* getClustersProvider()const{return clustersProvider;}

    /**Returns the number of samples in a waveform before the peak.*/
    int getNbSamplesBeforePeak()const{return (clusteringData->getPeakPositionInWaveform() - 1);}

    /**Returns the number of samples in a waveform after the peak.*/
    int getNbSamplesAfterPeak()const{return (clusteringData->getNbSamplesInWaveform() - clusteringData->getPeakPositionInWaveform());}

    /** Shows in the cluster palette the user cluster information, that is show a modified cluster palette presenting the cluster ids and the user cluster information.*/
    void showUserClusterInformation();

    /**Sets the modified status of the current opend document to true .*/
    void clusterInformationModified(){modified = true;}

Q_SIGNALS:
    void updateUndoNb(int undoNb);
    void updateRedoNb(int undoNb);
    void clustersGrouped(QList<int>& groupedClusters, int newClusterId);
    void clustersDeleted(QList<int>& deletedClusters,int destinationCluster);
    void removeSpikesFromClusters(QList<int>& fromClusters, int destinationClusterId,QList<int>& emptiedClusters);
    void newClusterAdded(QList<int>& fromClusters,int clusterId,QList<int>& emptiedClusters);
    void newClustersAdded(QMap<int,int>& fromToNewClusterIds,QList<int>& emptiedClusters);
    void renumber(QMap<int,int>& clusterIdsOldNew);
    void undoRenumbering(QMap<int,int>& clusterIdsNewOld);
    void undoAdditionModification(QList<int>& addedClusters,QList<int>& updatedClusters);
    void undoAddition(QList<int>& addedClusters);
    void undoModification(QList<int>& updatedClusters);
    void redoRenumbering(QMap<int,int>& clusterIdsOldNew);
    void redoAdditionModification(QList<int>& addedClusters,QList<int>& modifiedClusters,bool isModifiedByDeletion,QList<int>& deletedClusters);
    void redoAddition(QList<int>& addedClusters,QList<int>& deletedClusters);
    void redoModification(QList<int>& updatedClusters,bool isModifiedByDeletion,QList<int>& deletedClusters);
    void redoDeletion(QList<int>& deletedClusters);
    void newClustersAdded(QList<int>& clustersToRecluster);
    void spikesDeleted();
    
public Q_SLOTS:
    /** Calls repaint() on all views connected to the document object and is called by the view by which the document has been changed.
     * As this view normally repaints itself, it is excluded from the paintEvent.
     */
    void updateAllViews(KlustersView* sender);
    /** Repaint all views AND trigger correlogram recomputation by firing
     *  updateDrawing() on each view.  Call after realignment so that the
     *  scatter, waveform, and correlation views all refresh. */
    void refreshAllViews();

    /** Forces all views to discard cached data for @p clusterId and re-fetch.
     *  Equivalent to emitting spikesAddedToCluster on each KlustersView. */
    void forceClusterRefresh(int clusterId);

    /**Renumbers the clusters, so the the clusterIds will be consecutive.*/
    void renumberClusters();

    /**Launchs an autoSave by starting the autoSaveThread.*/
    void launchAutoSave();

private:

    /** Commit all pending files to the originals, then re-seed the pending
     *  files from the freshly-written originals so the cycle continues.
     *  Called by saveDocument() after a successful write.
     *  Also handles SaveAs: pass the new base paths when the doc URL changed. */
    void commitAndRenewPending();

    /** Seed all four .pending files from their originals.
     *  Redirects spkFileName and tmpCluFile to the pending paths.
     *  Called on open, after commit (save), and after reject. */
    bool initPendingFiles();

    /** Original file paths — set on open, updated on SaveAs. */
    QString m_origSpkPath;
    QString m_origResPath;
    QString m_origFetPath;
    // clu original == docUrl; clu pending == m_pendingCluPath

    /** Persistent .pending file paths — live for the entire document session. */
    QString m_pendingSpkPath;
    QString m_pendingResPath;
    QString m_pendingFetPath;
    QString m_pendingCluPath;

    /**
    * Removes spikes from some clusters and assign them to the cluster @pdestinationCluster
    * which is either the cluster 0, corresponding to the artefact, or the cluster 1, corresponding to the noise.
    * @param destination the cluster number to assign the spikes contained in the region.
    * @param region the polygon defining the area containing the spikes corresponding to the noise.
    * @param clustersOfOrigin a list of the cluster numbers (in ascending order) identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as absciss to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    */
    void deleteSpikesFromClusters(int destination, QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**
    * Fills the undo list (clusterColorListUndoList) and clear the redo list
    * (clusterColorListRedoList) to prepare for a futur undo.
    * Sets the boolean modified to true as every action on the document implies
    * a call to this function.
    */
    void prepareClusterColorUndo();

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the futur undo.
    * @param addedClustersTemp the list of newly created clusters which will be added
    * to the addedClustersUndoList.
    * @param modifiedClustersTemp the list of last modified clusters, the list will be added
    * to the modifiedClustersUndoList.
    * @param deletedClustersTemp the list of last deleted clusters, the list will be added
    * to the deletedClustersUndoList.
    * @param isModifiedByDeletion true if the clusters of @p modifiedClusters have been modified
    * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact abd cluster of noise respectively).
    */
    void prepareUndo(QList<int>* addedClustersTemp,QList<int>* modifiedClustersTemp,QList<int>* deletedClustersTemp,bool isModifiedByDeletion = false);

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) with
    * empty list to prepare the futur undo.
    */
    void prepareUndo();

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the futur undo. modifiedClustersUndoList will be fill with an empty list.
    * @param newCluster the newly created cluster which will be put in a list and added
    * to the addedClustersUndoList.
    * @param deletedClusters the list of last deleted clusters, the list will be added
    * to the deletedClustersUndoList.
    */
    void prepareUndo(int newCluster,QList<int>& deletedClusters);

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the futur undo. addedClustersUndoList will be fill with an empty list.
    * @param modifiedClusters the list of last modified clusters, the list will be added
    * to the modifiedClustersUndoList.
    * @param deletedClusters the list of last deleted clusters, the list will be added
    * to the deletedClustersUndoList.
    * @param isModifiedByDeletion true if the clusters of @p modifiedClusters have been modified
    * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
   */
    void prepareUndo(QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion = false);

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the futur undo.
    * @param newCluster the newly created cluster which will be put in a list and added
    * to the addedClustersUndoList.
    * @param modifiedClusters the list of last modified clusters, the list will be added
    * to the modifiedClustersUndoList.
    * @param deletedClusters the list of last deleted clusters, the list will be added
    * to the deletedClustersUndoList.
    * @param isModifiedByDeletion true if the clusters of @p modifiedClusters have been modified
    * by the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
    */
    void prepareUndo(int newCluster, QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion = false);

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the futur undo.
    * @param newClusters the list of newly created clusters which will be added
    * to the addedClustersUndoList.
    * @param modifiedClusters the list of last modified clusters, the list will be added
    * to the modifiedClustersUndoList.
    * @param deletedClusters the list of last deleted clusters, the list will be added
    * to the deletedClustersUndoList.
    */
    void prepareUndo(QList<int>& newClusters, QList<int>& modifiedClusters,QList<int>& deletedClusters);

    /**
    * Clears the different undo and redo lists as no undo/redo is possible after a renumbering
    * except on the renumbering itself.
    * @param clusterIdsOldNew map giving the correspondence between the old numbering and the new numbering of the clusters.
    * @param clusterIdsNewOld map giving the correspondence between the new numbering and the old numbering of the clusters.
    */
    void prepareUndo(QMap<int,int> clusterIdsOldNew,QMap<int,int> clusterIdsNewOld);
    
    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the futur undo. modifiedClustersUndoList will be fill with an empty list.
    * @param newClusters the list of clusters created  by the automaatic reclustering which will be added
    * to the addedClustersUndoList.
    * @param deletedClusters the list of automatically reclustered clusters  which will be deleted, the list will be added
    * to the deletedClustersUndoList.
    */
    void prepareReclusteringUndo(QList<int>& newClusters,QList<int>& deletedClusters);

    //Members

    /**Represents a the list of clusters with their associated color and status.*/
    ItemColors* clusterColorList;

    /**Represents a list of list of clusters with their associated color and status
    * use to enable undo action.
    */
    QList<ItemColors*> clusterColorListUndoList;

    /**Represents a list of list of clusters with their associated color and status
    * use to enable redo action.
    */
    QList<ItemColors*> clusterColorListRedoList;
    
    /**The modified flag of the current document. */
    bool modified;
    
    /**The url of the document.*/
    QString docUrl;
    
    /**The url for the temporary saved file.*/
    QString cluFileSaveUrl;
    
    /**The base name of the document. */
    QString baseName;

    /**The path to the xml þarameter file. */
    QString parameterFile;

    /**The electrode number*/
    QString electrodeGroupID;
    
    /**Temporary file corresponding to the cluster file.*/
    QString tmpCluFile;

    /**Temporary file corresponding to the spike file.*/
    QString tmpSpikeFile;
    
    /**The list of the views currently connected to the document. */
    QList<KlustersView*>* viewList;

    /** Class containing all the data for the clusters cuting.*/
    Data* clusteringData;

    /**Pointer on the parent widget (main window).*/
    QWidget* parent;
    
    /**Reference on the clusterPalette.*/
    ClusterPalette& clusterPalette;

    /**List of current added clusters. */
    QList<int>* addedClusters;
    /**List of current modified clusters. */
    QList<int>* modifiedClusters;
    /**List of current deleted clusters. */
    QList<int>* deletedClusters;
    
    /**Represents a list of list of added clusters use to enable undo action.
    */
    QList< QList<int>* > addedClustersUndoList;

    /**Represents a list of list of added  clusters use to enable redo action.
    */
    QList< QList<int>* > addedClustersRedoList;

    /**Represents a list of list of modified clusters use to enable undo action.
    */
    QList< QList<int>* > modifiedClustersUndoList;

    /**Represents a list of list of modified clusters use to enable redo action.
    */
    QList< QList<int>* > modifiedClustersRedoList;

    /**Represents a list of list of deleted clusters use to enable undo action.
    */
    QList< QList<int>* > deletedClustersUndoList;

    /**Represents a list of list of deleted clusters use to enable redo action.
    */
    QList< QList<int>* > deletedClustersRedoList;
    
    /**List of the undo numbers where the modification of clusters has been due to
    * the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
    * This list is used to reduce the number of cluster to redraw whenever possible.
    */
    QList<int> modifiedClustersByDeleteUndo;

    /**List of the redo numbers where the modification of clusters has been due to
    the deletion of spikes (moved to cluster 0 or 1, cluster of artefact and cluster of noise respectively).
    * This list is used to reduce the number of cluster to redraw whenever possible.
    */
    QList<int> modifiedClustersByDeleteRedo;

    /**Map with keys equal to the do/undo indices and values equal to a map
   *giving the correspondence between the old numbering and the new numbering of the clusters.*/
    QMap<int, QMap<int,int> > clusterIdsOldNewMap ;

    /**Map with keys equal to the do/undo indices and values equal to a map
   * giving the correspondence between the new numbering and the old numbering of the clusters.*/
    QMap<int, QMap<int,int> > clusterIdsNewOldMap;

    /**List given the undo indices corresponding to a renumbering which can be redo.*/
    QList<int> renumberingRedoList;

    /**Thread responsible for the autosaving of the document.*/
    AutoSaveThread* autoSaveThread;

    /**Boolean indicating if an auto save have to be made on the document.*/
    bool autoSave;

    /**Time interval between two auto saving.*/
    int savingInterval;

    /**Boolean indicating if the auto saving has to be stopped.*/
    bool endAutoSaving;

    /**Provider of the channels data.*/
    TracesProvider* tracesProvider;

    /**Provider of the cluster data for the TraceView.*/
    ClustersProvider* clustersProvider;

    /**Represents a the list of channels with their associated color and status.*/
    ChannelColors* channelColorList;

    /**Gain which takes the screen gain into account.*/
    int gain;

    /**Acquisition system gain.*/
    int acquisitionGain;

    /**Map given the correspondance between the channel ids and the display group ids.*/
    QMap<int,int> displayChannelsGroups;

    /**Map given the correspondance between the display group ids and the channel ids.*/
    QMap<int, QList<int> > displayGroupsChannels;

    /**Map given the correspondance between the channel ids and the spike group ids.*/
    QMap<int,int> channelsSpikeGroups;

    /**Map given the correspondance between the spike group ids and the channel ids.*/
    QMap<int, QList<int> > spikeGroupsChannels;

    /**
 * This assumes that the cluster file names contain the identifier of
 * the spike group used to create them (myFile.clu.1 correspond to the
 * spike group 1).
 */
    QMap<int, QList<int> > displayGroupsClusterFile;

    /** Realignment writes pending until saveDocument() is called. */
    std::vector<PendingRealign> m_pendingRealign;

};

#endif // KCLUSTERSDOC_H
