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
#include "curationlogger.h"
#include "watershed2d.h"


// include files for QT
#include <QObject>
#include <QVector>
#include <QString>
#include <QPoint>
#include <QFileInfo>
#include <functional>
#include <neurosuite/core/pca_projection.hpp>  // header-only PcaBasis/loadPca/pcaProjectionEnergy
#include <neurosuite/core/stderiv_transform.hpp> // header-only spatialOrder/applyStderivTransform


#include <QList>
#include <QSet>
#include <QMap>
#include <QFile>
#include <QEvent>
#include <QDebug>
#include <vector>
#include <memory>




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

    /** Convenience: cast `parent` to KlustersApp.  Constructed always with
     *  a KlustersApp parent (see ctor in klusters.cpp), so the cast is
     *  unconditional.  Folds 16 occurrences of
     *  `static_cast<KlustersApp*>(parent)->...` across this file. */
    KlustersApp* app() const { return reinterpret_cast<KlustersApp*>(parent); }
    
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
    int openDocument(const QString &url,QString& errorInformation, const char* format=nullptr);
    /**Opens a document using different format, than the one defined for the application, by filename and format.
    * Not Yet implemented.
    */
    bool importDocument(const QString &url, const char* format=nullptr);

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
    int saveDocument(const QString &url, const char* format=nullptr);
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
    /** Returns true if this session uses the stderiv pipeline
     *  (.spkD.N / .fetD.N / .pcaD.N files). */
    /** Returns a snapshot copy of the view list safe to iterate during teardown. */
    QList<KlustersView*> viewListCopy() const
    { return viewList ? *viewList : QList<KlustersView*>(); }
    bool isStderivSession() const
        { return origSpkPath.contains(QStringLiteral(".spkD.")); }

    /**Returns the reference on the list of ClusterColor objects.
    * @return ItemColors containing the information on the clusters and their associated color.
    */
    ItemColors& clusterColors() const {return *(activeColorList ? activeColorList : clusterColorList);}
    /** The PARENT cluster colours, independent of which clustering is active.
     *  The main cluster palette binds to this so it always lists the parent
     *  clustering and is never collapsed to noise by a left-over child-active
     *  scope (the active list is the child list while a child is selected). */
    ItemColors& parentClusterColors() const {return *clusterColorList;}
    /** The CHILD cluster colours, independent of which clustering is active.
     *  The child palette binds to this so its row->index lookups stay valid
     *  even when no child is selected -- at which point the ACTIVE list is the
     *  parent list, which is shorter than the child list, so indexing a child
     *  row position into it would walk off the end.  Falls back to the parent
     *  list when the hierarchy has not been loaded, so the reference is always
     *  valid; the child palette has no items in that state anyway. */
    ItemColors& childClusterColors() const {return *(childColorList ? childColorList : clusterColorList);}

    /**Returns a reference on data (object containing all the information).
    * @return data object.
    */
    /** The clustering the VIEWS currently render.  Normally the parent (.clu)
     *  clustering; while a child (.clc microfiber) is selected in hierarchical
     *  mode it is the child clustering, so every view re-scopes to that child's
     *  spikes -- a strict subset of its parent (the nesting invariant).  Edit
     *  and save paths must use parentData(), never data(). */
    Data& data() const {return *(activeData ? activeData : clusteringData);}
    /** The parent (.clu) clustering, regardless of the hierarchical active state. */
    Data& parentData() const {return *clusteringData;}
    /** The child (.clc) clustering, regardless of the active state.  The child
     *  palette consults this (not data(), which is the active clustering) when
     *  it maps a row to a cluster id: with no child selected, data() is the
     *  PARENT clustering, which does not contain the child ids, so a per-id
     *  lookup there returns nothing and the caller indexes an empty list.
     *  Falls back to the parent clustering when the hierarchy is not loaded so
     *  the reference is always valid; the child palette is empty in that state. */
    Data& childClusterData() const {return *(childData ? childData : clusteringData);}

    /**Manages the color change of a single cluster.
    * Called when the palette is in immediate-update mode (no need to press
    * the Update button to trigger the change).
    * @param clusterId cluster whose color has changed.
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

    // ── cluster masking ──────────────────────────────────────────────────
    //  Masking focuses curation on a SUBSET of clusters.  Masked clusters are
    //  removed from the active cluster list (and dropped from the view
    //  foreground) but stay in the data model: their spikes and — crucially —
    //  their ids remain in clusterInfoMap, so global id allocation
    //  (nextFreeClusterId / highestClusterId) still skips them and merges /
    //  splits keep producing globally-unique ids that never collide with a
    //  masked cluster.  Time-chunk curation is just
    //  setMaskKeeping(clustersInTimeWindow(chunk)).
    /**Masks @p clustersToMask (removes them from the active list/view fg).*/
    void maskClusters(const QList<int>& clustersToMask);
    /**Unmasks @p clustersToUnmask (returns them to the active set).*/
    void unmaskClusters(const QList<int>& clustersToUnmask);
    /**Masks every cluster EXCEPT those in @p clustersToKeep (the focus set).*/
    void setMaskKeeping(const QList<int>& clustersToKeep);
    /**Clears the mask: all clusters become active again.*/
    void clearMask();
    /**True iff @p clusterId is currently masked.*/
    bool isMasked(int clusterId) const {return maskedClusters.contains(clusterId);}

    // ── hierarchical (.clc child) clustering ─────────────────────────────────
    // Optional second clustering: the microfiber / pure-shape children of the
    // parent .clu units, loaded from the .clc sibling and aligned to the same
    // .res.  Views render through data()/clusterColors(), which follow the
    // active pair; pointing them at the child clustering re-scopes every view
    // to a selected child's spikes.
    /** True if a .clc child sibling was detected next to the opened .clu. */
    bool hasChildSibling()    const { return !clcSiblingPath.isEmpty(); }
    /** The two systems are mutually exclusive and the choice is committed at open
     *  from the file set: a session is Hierarchical iff BOTH a .clc child layer and
     *  a .clp parent-map sibling were found next to the opened .clu (the complete
     *  triple that Save regenerates together); otherwise it is a Flat .clu session.
     *  There is no runtime toggle between the two — a .clc without its .clp is a
     *  flat session that ignores the orphan child file. */
    enum class ClusteringMode { Flat, Hierarchical };
    ClusteringMode clusteringMode() const {
        return (!clcSiblingPath.isEmpty() && !clpSiblingPath.isEmpty())
               ? ClusteringMode::Hierarchical : ClusteringMode::Flat;
    }
    /** True iff this is a committed hierarchical session (.clc + .clp present). */
    bool isHierarchicalSession() const { return clusteringMode() == ClusteringMode::Hierarchical; }
    /** True once the child clustering has actually been loaded into memory. */
    bool hasChildClustering() const { return childData != nullptr; }
    /** Lazily build childData/childColorList + the parent<->child maps from the
     *  .clc sibling.  No-op returning true if already loaded; false (with
     *  errorInformation) if the sibling is missing or unreadable. */
    bool loadChildClustering(QString& errorInformation);
    /** Child ids whose parent is one of @p parents (sorted, unique). */
    QList<int> childrenOf(const QList<int>& parents) const;
    /** Parent unit id owning @p childId, or -1 if @p childId is not a child. */
    int parentOfChild(int childId) const { return childToParent.value(childId,-1); }
    /** Point the VIEW-facing data()/clusterColors() at the child (true) or the
     *  parent (false) clustering.  Parent is the default and the only state in
     *  which the document may be edited or saved. */
    void setActiveClustering(bool child);
    bool isChildClusteringActive() const { return childScopeActive; }
    /** Restrict the child palette to @p visibleChildren; the rest are hidden via
     *  isChildScopeHidden(), which ClusterPalette::updateClusterList honours. */
    void setChildScope(const QList<int>& visibleChildren);
    /** True for a child id outside the current child scope (palette filter);
     *  always false for parent ids and when no child clustering is loaded. */
    bool isChildScopeHidden(int clusterId) const;

    // ── hierarchy EDITS (operate on the parent .clu; re-derive maps after) ───
    /** Merge fibers @p fibers into one (reuses groupClusters); their children
     *  union under the kept id.  Returns the kept fiber id, or -1. */
    int mergeParentFibers(const QList<int>& fibers, KlustersView& activeView);
    /** Detach child @p childCluster from its fiber into a new fiber of its own
     *  (the spikes become a new .clu cluster).  Returns the new fiber id, the
     *  existing parent if it was the only child (no-op), or -1. */
    int promoteChild(int childCluster, KlustersView& activeView);
    /** Move child @p childCluster's spikes onto the existing fiber @p targetFiber. */
    bool moveChild(int childCluster, int targetFiber, KlustersView& activeView);
    /** Collect @p children (possibly from different fibers) into one new fiber.
     *  Returns the new fiber id, or -1.  Reuses moveSpikeSubsetToCluster. */
    int groupChildrenIntoFiber(const QList<int>& children, KlustersView& activeView);
    /** Explode @p fiber into its constituent children, each becoming its own
     *  fiber (one keeps @p fiber's id).  Inverse of mergeParentFibers. */
    bool dissolveFiber(int fiber, KlustersView& activeView);
    /** Eject child @p childCluster from its fiber onto the noise cluster (1)
     *  without deleting its spikes. */
    bool dropChildToNoise(int childCluster, KlustersView& activeView);
    // ── atom-layer edit (rewrites .clc; separate child-undo timeline) ────────
    /** Collapse @p children (which MUST share a fiber) into one microfiber.
     *  Mutates childData, pushes a child-undo entry.  Returns the new child id,
     *  or -1 (e.g. on a cross-fiber selection, which is refused). */
    int mergeChildren(const QList<int>& children, KlustersView& activeView);
    /** Undo / redo the most recent atom-layer edit (childData), independent of
     *  the parent Ctrl+Z timeline. */
    bool undoChildEdit(KlustersView& activeView);
    bool redoChildEdit(KlustersView& activeView);
    int childUndoCount() const { return childUndoStack.count(); }
    int childRedoCount() const { return childRedoStack.count(); }
    /** True when the top of the child-undo stack IS the user's most recent
     *  action (so Ctrl+Shift+Z reverts what they just did).  False when the most
     *  recent action was a fiber-layer edit/undo — the app warns in that case. */
    bool childUndoMatchesLastAction() const { return lastEditLayer == EditLayer::Atom; }
    /** Unified undo/redo dispatcher.  Routes Ctrl+Z / Ctrl+Y to the parent
     *  (clusteringData) or atom (childData) timeline by the recorded order of
     *  edits, so one shortcut reverts the single most recent edit regardless of
     *  which layer produced it.  Stale order markers (a layer capped by nbUndo,
     *  or cleared when a parent op re-cut the child layer) are skipped using the
     *  live per-layer stack counts as the source of truth. */
    void undoDispatch();
    void redoDispatch();
    int  parentUndoCount() const { return clusterColorListUndoList.count(); }
    int  parentRedoCount() const { return clusterColorListRedoList.count(); }
    /** Force the atom timeline (Ctrl+Shift+Z and its redo): revert / replay the
     *  most recent atom edit even when a newer parent edit exists, keeping the
     *  unified order timeline consistent. */
    bool undoChildEditDispatch();
    bool redoChildEditDispatch();
    /** Re-derive parentToChildren/childToParent from the live .clu + .clc (a
     *  child's fiber is the .clu label of its spikes).  Cheap pure function of
     *  the data, so it keeps the maps correct across edits AND undo/redo with no
     *  separate map-undo stack. */
    void rebuildHierarchyFromData();
    /** After a PARENT edit that moved spikes across fiber boundaries (a manual
     *  polygon split), carve only the child atoms that now straddle two or more
     *  parents so every parent regains whole atoms; atoms wholly inside one
     *  parent are left intact (a split must not collapse untouched children). */
    void resyncStraddlingAtoms();
    /** Overwrite the .clc and .clp siblings (with .bak) from the current child
     *  layer + child->parent map.  Called by saveDocument when a child
     *  clustering is loaded. */
    bool saveHierarchySiblings();
    /** Ensure childColorList has a deterministic (id-derived) colour for every
     *  current child id, so atom edits/undo/redo never leave a child uncoloured. */
    void syncChildColors();
    /**True iff any cluster is currently masked.*/
    bool hasMask() const {return !maskedClusters.isEmpty();}
    /**Currently masked cluster ids.*/
    QList<int> maskedClusterIds() const;
    /**Currently unmasked (active) cluster ids.*/
    QList<int> unmaskedClusterIds() const;

    // ── time-chunk curation ──────────────────────────────────────────────
    //  Splits the session into uniform chunkMinutes windows (matching
    //  fiber-session) and, for each chunk, masks every cluster except those
    //  present in the chunk and foregrounds the chunk's clusters — so curation
    //  decisions are made on one chunk's clusters at a time.
    /**Enter chunk mode with uniform @p chunkMinutes windows.  @p overlapMinutes
     * extends each window on both sides; a cluster counts as present in a chunk
     * if it has >= @p minSpikes spikes inside the window.*/
    void enterChunkMode(double chunkMinutes, double overlapMinutes = 0.0, int minSpikes = 1);
    /**Leave chunk mode and clear the mask (all clusters active again).*/
    void exitChunkMode();
    /**True iff chunk mode is active.*/
    bool inChunkMode() const {return chunkMode;}
    /**Current chunk index (0-based) and the total number of chunks.*/
    int currentChunk() const {return currentChunkIndex;}
    int chunkCount() const {return nbChunks;}
    /**Step to the next / previous chunk; returns false at the ends.*/
    bool nextChunk();
    bool prevChunk();
    /**Jump to chunk @p index (clamped to [0, chunkCount()-1]).*/
    void gotoChunk(int index);
    /**[t0,t1] of chunk @p index in recording units (samples).*/
    void chunkTimeWindow(int index, long& t0, long& t1) const;
    
    /**Manages the grouping of clusters.
    * @param clustersToGroup list of clusters to be grouped.
    * @param activeView the view in which the change has to be immediate.
    */
    /** Groups the given clusters; returns the id of the resulting merged
     *  cluster (>1), or -1 if nothing was merged. */
    int groupClusters(QList<int> clustersToGroup,KlustersView& activeView);

    /** Moves spikes from @p fromCluster whose 0-based .spk indices are in
     * @p spkFileIndices into @p toCluster. Updates undo/redo and all views. */
    void moveSpikeSubsetToCluster(int fromCluster,
                                   const QVector<int>& spkFileIndices,
                                   int toCluster,
                                   KlustersView& activeView);

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
    * @param dimensionX the dimension used as abscissa to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    */
    void deleteNoise(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**
    * Removes spikes from some clusters and assign them to the cluster 0, the cluster for the artefact.
    * @param region the polygon defining the area containing the spikes corresponding to the noise.
    * @param clustersOfOrigin a list of the cluster numbers (in ascending order) identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as abscissa to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    */
    void deleteArtifact(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**
    * Creates a new cluster out of existing ones.
    * @param region the polygon defining the area containing the spikes for the new cluster.
    * @param clustersOfOrigin a list of the cluster numbers identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as abscissa to display the clusters.
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
    * @param dimensionX the dimension used as abscissa to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    * @return a list of the numbers of the newly created clusters.
    */
    void createNewClusters(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /** DipSplit result summary — returned by dipSplitApply().
     *
     *  Two-cluster commit: dipsplit produces TWO new clusters at the
     *  palette tail and consumes the source.  leftId holds the
     *  label-0 spikes (smaller of the new IDs); rightId = leftId+1
     *  holds the label-1 spikes.
     */
    struct DipSplitResult {
        bool    accepted        = false;  ///< true if the split was committed
        int     sourceId        = 0;      ///< the cluster that was split
        int     leftId          = 0;      ///< new ID for label-0 spikes
        int     rightId         = 0;      ///< new ID for label-1 spikes (= leftId+1)
        int     n0              = 0;      ///< # label-0 spikes (now in leftId)
        int     n1              = 0;      ///< # label-1 spikes (now in rightId)
        int     bestPC          = -1;     ///< which PC (0-2) showed deepest valley
        double  bestDepth       = 0.0;    ///< valley depth in [0..1]
        double  mahal2P90       = 0.0;    ///< 90th-percentile Mahalanobis² of cluster
        double  chi2_90         = 0.0;    ///< reference χ²(d, 0.9)
        double  deltaBIC        = 0.0;    ///< BIC(k=1) - BIC(k=2);  > 0 ⇒ split better
        QString reason;                   ///< "split", "too_small", "not_bloated",
                                          ///< "no_valley", "small_child", "bic_worse",
                                          ///< "cluster_not_found", "bad_features"
    };

    /** Pure-decision output of dipSplitDecide() — captures whether the
     *  cluster should be split and, if so, exactly which spikes go where.
     *  Carries no Qt object identities; safe to keep across event-loop
     *  iterations and inspect for a "preview before commit" UI.
     */
    struct DipSplitDecision {
        bool             accepted     = false;
        int              clusterId    = -1;   ///< source cluster examined
        int              n0           = 0;    ///< size of label-0 half
        int              n1           = 0;    ///< size of label-1 half
        int              bestPC       = -1;
        double           bestDepth    = 0.0;
        double           mahal2P90    = 0.0;
        double           chi2_90      = 0.0;
        double           deltaBIC     = 0.0;
        QString          reason;              ///< same vocabulary as DipSplitResult
        std::vector<int> labels;              ///< 0/1 per cluster member, in
                                              ///< the order returned by spikePositions
        QList<dataType>  rowsByMember;        ///< 1-based feature row for each
                                              ///< entry in labels
    };

    /** Pure algorithm core: tests a cluster for hidden bimodality and
     *  returns a structured decision.  No side effects on KlustersDoc state.
     *  Used by the live-preview path: results are rendered as coloured
     *  discs over the scatter, then committed via dipSplitApply().
     */
    DipSplitDecision dipSplitDecide(int   clusterId,
                                     int   minSize      = 50,
                                     float bloatFactor  = 0.0f,
                                     float valleyThresh = 0.20f);

    /** Apply a pre-computed DipSplitDecision.  Splits the cluster
     *  according to D.labels, allocates a new ID, renames the source
     *  to the palette tail, fires view updates, and writes a curation
     *  log entry.
     *
     *  Used by the live-preview path: dipSplitDecide() runs once at
     *  preview entry, the preview shows the result, and Enter commits
     *  via dipSplitApply without re-running the algorithm.
     *
     *  Parameters minSize / bloatFactor / valleyThresh are passed in
     *  only so they can be recorded in the curation log alongside the
     *  decision metrics.  The decision itself was already made.
     *
     *  @pre  D.accepted must be true; otherwise the call is a no-op
     *        returning a result with accepted=false.
     */
    DipSplitResult dipSplitApply(const DipSplitDecision& D,
                                  int   minSize,
                                  float bloatFactor,
                                  float valleyThresh);

    /** Result of splitClusterByKnnVsReferences (palette + log payload). */
    struct KnnSplitResult {
        int            sourceId       = 0;     ///< source cluster, copied for the caller
        bool           accepted       = false; ///< true iff prepareUndo was called
        QList<int>     newClusters;            ///< new cluster IDs in creation order
        QList<int>     matchedReferences;      ///< parallel: refId each new cluster matched, or -1 for residual
        QList<int>     emptiedClusters;        ///< contains sourceId iff fully consumed
        int            nRefClusters   = 0;     ///< size of the reference pool (info)
        int            nRefSpikes     = 0;     ///< total spikes in the reference pool
        int            nResidual      = 0;     ///< size of the residual new cluster, 0 if absent
        QString        reason;                 ///< user-facing summary or error
    };

    /** N-way split using K-nearest-neighbour majority vote against a
     *  reference pool of well-isolated clusters.  Thin wrapper around
     *  Data::splitClusterByKnnVsReferences: validates the source via
     *  clusterHasMembers, runs the algorithm, then handles UI
     *  plumbing (palette colours, view notifications, doc-side undo,
     *  curation log) exactly the way dipSplitApply does — but
     *  generalised to N new clusters instead of 2.
     *
     *  @param sourceCluster      cluster id to split.
     *  @param K                  neighbours per source spike (>= 2).
     *  @param majorityThreshold  fraction of K votes needed to commit
     *                             a label (0.0–1.0).
     *  @param minNewClusterSize  smallest size for a new cluster;
     *                             smaller per-label groups fold into
     *                             the residual.
     *  @param minRefClusterSize  smallest reference-cluster size
     *                             (≥100 recommended — "well-isolated"). */
    KnnSplitResult splitClusterByKnnVsReferences(int    sourceCluster,
                                                 int    K,
                                                 double majorityThreshold,
                                                 int    minNewClusterSize,
                                                 int    minRefClusterSize);
 
    /**Returns the number of dimensions of the data.*/
    int nbDimensions(){return clusteringData->nbOfDimensions();}

    /** Reverts the last user action.*/
    void undo();

    /** Reverts the last undo action.*/
    void redo();

    /**Returns the temporary file corresponding to the cluster file.*/
    QString temporaryFile() const {return tmpCluFile;}

    /** patch63 — last save error message.  Empty unless the most recent
     *  saveDocument() call returned a non-OK status, in which case this
     *  contains a human-readable description of the failing step (path,
     *  errno).  Surfaced in the SaveDoneEvent for the GUI to show. */
    QString lastSaveError() const { return lastSaveErrorMessage; }

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

        explicit CloseDocumentEvent(const QString &origin):QEvent(QEvent::Type(QEvent::User + 400)),origin(origin){}

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
  * @param newNbUndo the future new number of undo.
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

    /** patch76 — build a mean-subtracted sub-dimensional feature file
     *  for a single cluster.  Each spike's feature vector is centered
     *  on the cluster mean, projected onto the top K eigenvectors of
     *  the residual covariance, and written as int64 to the .fet file
     *  alongside the original timestamp.
     *  Returns one of the standard enum codes (OK / OPEN_ERROR /
     *  CREATION_ERROR).  The actual number of dimensions written (K+1
     *  on success) is returned via the *nDimWritten out-parameter; it
     *  is NOT the function's return value because that would collide
     *  with the enum codes (CREATION_ERROR=8 etc.).
     */
    int createMeanSubtractedSubdimFeatureFile(
        int clusterId, int K,
        const QString& reclusteringFetFileName,
        int* nDimWritten = nullptr,
        QVector<double>* eigvalsOut = nullptr);

    /** Raw-waveform, median-centred variant of the sub-dimensional path.
     *  Pools the spikes of all @p clusterIds before taking the median.
     *  Same return/out-parameter contract as
     *  createMeanSubtractedSubdimFeatureFile. */
    int createMedianWaveformResidualFeatureFile(
        const QList<int>& clusterIds, int K,
        const QString& reclusteringFetFileName,
        int* nDimWritten = nullptr,
        QVector<double>* eigvalsOut = nullptr);

    /** Shift all timestamps for @p clusterId by @p deltaSamples (±1 etc.).
     *  Updates .res and .fet (time feature column). Marks doc modified. */
    bool nudgeClusterTimestamps(int clusterId, int deltaSamples);

    /** True iff @p clusterId is registered in clusterInfoMap with at
     *  least one spike.  Used by slotRecluster as a pre-launch
     *  validation step — the cluster palette reads from
     *  spikesByCluster while createFeatureFile reads from
     *  clusterInfoMap, and these can desync (e.g. after a curation
     *  rollback, an undone merge/split, or an aborted reorder),
     *  producing an empty .fet that triggers a cryptic KK abort.
     *  Nudge does not itself desync the map; if recluster fails right
     *  after a nudge the desync predates it.  See
     *  KlustersApp::slotRecluster. */
    bool clusterHasMembers(int clusterId) const;
    /** Like clusterHasMembers, but queries the ACTIVE clustering (childData in
     *  hierarchical mode) rather than the parent.  The recluster guard uses this
     *  so a child id -- which never exists in the parent clustering -- is
     *  validated against the child clustering that actually owns it. */
    bool activeClusterHasMembers(int clusterId) const;
    /** True while an in-flight recluster targets the child (atom) layer; lets the
     *  exit handler land focus on the new atoms in the child palette instead of
     *  the parent list.  Valid until reclusteringUpdate consumes the pin. */
    bool reclusterTargetIsChild() const { return reclusterTarget && reclusterTarget == childData; }

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
     * @param clusterId   Cluster to realign.
     * @param logOut      Receives human-readable progress / warning messages.
     * @param nShifted    Set to the number of spikes that were shifted.
     * @param nSwapped    Set to the number of sort-order swaps performed.
     * @param liveLog     Optional callback invoked for every progress line so
     *                    the caller can stream output to a UI in real time.
     *                    Second arg is true for warnings/errors, false for
     *                    informational messages.
     * @param args        Extra command-line args passed through to the
     *                    underlying process_refeaturize invocation.
     * @param meanBefore  If non-null, populated with the per-sample mean
     *                    waveform from before the realignment.
     * @param meanAfter   If non-null, populated with the per-sample mean
     *                    waveform from after the realignment.
     * @param backupBase  If non-null, set to the basename of the on-disk
     *                    backup created so the caller can offer a revert.
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
        // Paths are stored doc-level (origSpkPath etc.); only per-batch
        // metadata and the per-spike undo records are needed here.
        int64_t bytesPerSpike = 0;
        int     spkElems      = 0;
        int     timeDim       = 0;
        int     nFeatCols     = 0;
        std::vector<PendingSpkRecord> records;
    };

    /** True when realignment results are waiting to be written to disk. */
    bool hasPendingRealign() const { return !pendingRealign.empty(); }

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

    // ── Curation logging ──────────────────────────────────────────────────
    /** Compute a ClusterSnapshot for @p clusterId from the current in-memory
     *  state.  Returns an invalid snapshot (clusterId == -1) on failure.
     *  @param allCentroids  Precomputed centroid map; pass nullptr to skip
     *                       nearest-cluster metrics. */
    ClusterSnapshot snapshotCluster(int clusterId,
                                     double isiThreshMs = 3.0,
                                     const QMap<int,QVector<double>>* allCentroids = nullptr) const {
        return clusteringData->computeSnapshot(clusterId, isiThreshMs, allCentroids);
    }

    /** Convenience: snapshot each cluster in @p clusterIds.
     *  Computes all cluster centroids once and reuses them across the set.
     *  During a PCA-Center Align All batch the centroids come from the
     *  batch-scoped cache (computed once for the whole run) instead of a fresh
     *  full-dataset pass per call — see beginRealignBatchLog(). */
    QList<ClusterSnapshot> snapshotClusters(const QList<int>& clusterIds,
                                             double isiThreshMs = 3.0) const {
        QList<ClusterSnapshot> snaps;
        if (clusterIds.isEmpty())
            return snaps;
        snaps.reserve(clusterIds.size());
        if (centroidCacheEnabled) {
            const QMap<int,QVector<double>>& centroids = cachedCentroids();
            for (int id : clusterIds)
                snaps.append(clusteringData->computeSnapshot(id, isiThreshMs, &centroids));
            return snaps;
        }
        // Compute all centroids once — amortises the O(N×D) pass across
        // the entire call.  Each cluster then only does an O(K×D) linear
        // scan to find its nearest neighbour.
        const auto centroids = clusteringData->computeAllCentroids();
        for (int id : clusterIds)
            snaps.append(clusteringData->computeSnapshot(id, isiThreshMs, &centroids));
        return snaps;
    }

Q_SIGNALS:
    // ── Document-state signals ───────────────────────────────────────────
    // Emitted whenever a curation action changes the cluster set or
    // its membership.  Connected to slots on KlustersView / ClusterPalette
    // so they can refresh, repaint, and update their internal state lists.
    //
    // Naming convention:
    //   - past-tense verbs (clustersGrouped, newClusterAdded) announce a
    //     completed mutation — listeners refresh from the new state.
    //   - undo* / redo* announce that the corresponding action is being
    //     reverted or replayed — listeners walk the same code path
    //     in reverse.
    //   - removeSpikesFromClusters carries the pre-action source cluster
    //     list so listeners can identify which palette entries to update.
    //
    // The non-const reference parameters (QList<int>&, QMap<int,int>&)
    // are a legacy of the pre-Qt6 API; the data is observational, not
    // for in-place modification by listeners.
    void updateUndoNb(int undoNb);
    void updateRedoNb(int undoNb);
    void clustersGrouped(QList<int>& groupedClusters, int newClusterId);
    /** Emitted after a hierarchy edit (merge/promote/move) or an undo/redo that
     *  changed the fiber<-child maps, so the app can repopulate the child palette. */
    void hierarchyChanged();
    /** Emitted after a child (atom) split mints new sibling atoms, carrying their
     *  ids so the child palette can select and focus them. */
    void hierarchyChildrenCreated(const QList<int>& newChildren);
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

    /** Triggers a recompute of any open error / template / residual
     *  similarity-matrix docks across all views.  A no-op when no matrix is
     *  open; called by the atomic cluster-edit operations so the matrices stay
     *  current with the cluster configuration instead of only being flagged
     *  stale until the user presses U. */
    void updateSimilarityMatrices();

    /**Renumbers the clusters, so the the clusterIds will be consecutive.*/
    void renumberClusters();

    /** Renumber selected clusters to IDs greater than the current global
     *  maximum, so they end up at the tail of the (sorted-by-ID) palette.
     *  Triggered by the palette T shortcut.  Single undo entry covers
     *  the whole batch.  IDs 0 / 1 (artefact / noise) and the
     *  global-max cluster are filtered by the caller. */
    void renumberClustersToEnd(QList<int> clustersToRenumber);

    /** Single primitive for "rename a set of clusters" doc-level updates.
     *  Drives Data::renumberPartial + colour-list rename + view rewrite +
     *  errormatrix/template-matrix signal in the right order.  Used by
     *  renumberClustersToEnd (T key) and is intended to subsume all
     *  future rename callers (full renumber, watershed residual cleanup,
     *  etc.) so the rename logic lives in one place.
     *
     *  @param partialOldToNew  ONLY the renamed clusters (oldId -> newId).
     *  @param fullOldToNewOpt  Optional pre-built covering map (every
     *         existing cluster ID -> post-rename ID, identity for
     *         unchanged).  Pass nullptr to have it built automatically.
     *
     *  Caller is responsible for the doc-level UNDO snapshot
     *  (`prepareUndo` / `prepareReclusteringUndo`) and any
     *  logBefore/logAfter pairs; this helper does only the apply. */
    void applyClusterRename(const QMap<int,int>& partialOldToNew,
                            const QMap<int,int>* fullOldToNewOpt = nullptr);

    /** Reorder clusters so that the cluster IDs supplied in `newOrder`
     *  appear in that sequence starting at ID 2 (preserving 0/1 at the
     *  front).  Internally builds the partial/full old→new maps,
     *  snapshots the undo stack (renumber-specific stack so undo can
     *  classify the action), records a RENUMBER_PARTIAL curation-log
     *  entry, and delegates the actual rename to applyClusterRename.
     *
     *  Used by KlustersApp::slotReorderClustersBySimilarity (Shift+S).
     *  Returns the number of clusters whose ID actually changed (0
     *  means already-in-order; nothing happens, no undo entry pushed).
     *
     *  Rejects `newOrder` entries that are 0/1 or don't currently exist
     *  in the cluster table (returns -1, status bar message left to the
     *  caller). */
    int reorderClustersByPermutation(const QList<int>& newOrder);

    /** Run a 2D density watershed on the *selected* clusters in the
     *  palette using the active scatter view's X/Y feature dimensions,
     *  splitting them into one new cluster per basin.  Triggered by
     *  the W key.  Returns the number of new clusters produced (0 on
     *  failure or if input has fewer than ~50 spikes / produces just
     *  one basin).  Caller-supplied config controls grid size, smoothing,
     *  peak height, and minimum basin size; passing 0 for minPeakHeight
     *  or minBasinSize requests data-driven auto-tuning. */
    int watershedSelectedClusters(const QList<int>& selectedClusters,
                                  const Watershed2D::Config& cfg);

    /**Launchs an autoSave by starting the autoSaveThread.*/
    void launchAutoSave();

private:

    /** Cluster ids currently masked (hidden from the active list and the view
     *  foreground).  Stored here, not in Data, so the data model and global id
     *  allocation stay untouched — masking is a pure curation/view concern. */
    QSet<int> maskedClusters;
    /** Re-apply the current mask: rebuild the palette list (which omits masked
     *  clusters) and drop any masked cluster from the active view's shown set. */
    void applyMask();

    // time-chunk curation state (see enterChunkMode)
    bool chunkMode = false;
    long chunkLenSamples = 0;        // chunk length in recording units (samples)
    long chunkOverlapSamples = 0;    // per-side window extension
    long sessionMaxSamples = 0;      // largest spike timestamp
    int  currentChunkIndex = 0;
    int  nbChunks = 0;
    int  chunkMinSpikes = 1;         // min spikes for a cluster to count as present
    /** Apply the current chunk window: mask every cluster not present in it and
     *  foreground the chunk's clusters. */
    void applyChunkWindow();

    /** Common UI-update tail for any operation that produces ONE new cluster
     *  derived from existing ones (createNewCluster, dipSplitApply, …).
     *
     *  Call after the underlying Data mutator has run successfully.  Performs:
     *    - registers a colour for @p newId in the cluster colour list;
     *    - undo bookkeeping via prepareUndo();
     *    - removes any clusters in @p emptiedClusters from the colour list;
     *    - per-view addNewClusterToView() / updateTraceView();
     *    - emits newClusterAdded();
     *    - resets the colour-changed flag if needed;
     *    - refreshes the active view's child widgets and the cluster palette;
     *    - selects @p newId plus the original visible clusters in the palette.
     *
     *  @param newId            the freshly allocated cluster ID
     *  @param fromClusters     source clusters that contributed spikes (modified
     *                          in-place by Data mutators; passed straight through)
     *  @param emptiedClusters  clusters that ended up empty after the mutation
     *  @param activeView       the currently-active view (may be nullptr)
     */
    void commitClusterCreation(int newId,
                                QList<int>& fromClusters,
                                QList<int>& emptiedClusters,
                                KlustersView* activeView);

    /** Sibling of commitClusterCreation for paths that produce TWO new
     *  clusters from one (or more) source clusters in a single atomic
     *  Data mutation — currently only KlustersDoc::dipSplitApply.
     *
     *  Pushes a single doc-level undo entry covering both new IDs and
     *  the emptied source(s).  Routes view-side updates through
     *  KlustersView::addNewClustersToView (recluster variant) so the
     *  view-side undo also stays as a single entry.  One Ctrl+Z fully
     *  reverts; one Ctrl+Y fully replays.
     */
    void commitTwoClusterCreation(int leftId,
                                   int rightId,
                                   QList<int>& fromClusters,
                                   QList<int>& emptiedClusters,
                                   KlustersView* activeView);

    /** Commit all pending files to the originals, then re-seed the pending
     *  files from the freshly-written originals so the cycle continues.
     *  Called by saveDocument() after a successful write.
     *  Also handles SaveAs: pass the new base paths when the doc URL changed.
     *
     *  patch63 — returns false if any of the four file copies failed and
     *  populates outError with a human-readable description of the first
     *  failure.  Previous behaviour swallowed errors via qWarning() which
     *  meant the user saw "save successful" but the on-disk file was
     *  unchanged when QFile::copy failed (e.g. NTFS permission issues). */
    bool commitAndRenewPending(QString* outError = nullptr);

    /** Seed all four .pending files from their originals.
     *  Redirects spkFileName and tmpCluFile to the pending paths.
     *  Called on open, after commit (save), and after reject. */
    bool initPendingFiles();

    /** Original file paths — set on open, updated on SaveAs. */
    QString origSpkPath;
    QString origResPath;
    QString origFetPath;
    // clu original == docUrl; clu pending == pendingCluPath

    /** Persistent .pending file paths — live for the entire document session. */
    QString pendingSpkPath;
    QString pendingResPath;
    QString pendingFetPath;
    QString pendingCluPath;

    /** patch63 — populated by saveDocument() / commitAndRenewPending() when
     *  any step fails.  Read via lastSaveError() and surfaced in the
     *  SaveDoneEvent so the user sees the actual path / errno / step name
     *  rather than a generic "I/O Error". */
    QString lastSaveErrorMessage;

    /**
    * Removes spikes from some clusters and assign them to the cluster @pdestinationCluster
    * which is either the cluster 0, corresponding to the artefact, or the cluster 1, corresponding to the noise.
    * @param destination the cluster number to assign the spikes contained in the region.
    * @param region the polygon defining the area containing the spikes corresponding to the noise.
    * @param clustersOfOrigin a list of the cluster numbers (in ascending order) identifying the clusters which
    * may contain spikes in the region.
    * @param dimensionX the dimension used as abscissa to display the clusters.
    * @param dimensionY the dimension used as ordinate to display the clusters.
    */
    void deleteSpikesFromClusters(int destination, QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY);

    /**
    * Fills the undo list (clusterColorListUndoList) and clear the redo list
    * (clusterColorListRedoList) to prepare for a future undo.
    * Sets the boolean modified to true as every action on the document implies
    * a call to this function.
    */
    void prepareClusterColorUndo();

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the future undo.
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
    * empty list to prepare the future undo.
    */
    void prepareUndo();

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the future undo. modifiedClustersUndoList will be fill with an empty list.
    * @param newCluster the newly created cluster which will be put in a list and added
    * to the addedClustersUndoList.
    * @param deletedClusters the list of last deleted clusters, the list will be added
    * to the deletedClustersUndoList.
    */
    void prepareUndo(int newCluster,QList<int>& deletedClusters);

    /**
    * Fills the undo lists (addedClustersUndoList, deletedClustersUndoList and modifiedClustersUndoList) to
    * prepare the future undo. addedClustersUndoList will be fill with an empty list.
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
    * prepare the future undo.
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
    * prepare the future undo.
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
    * prepare the future undo. modifiedClustersUndoList will be fill with an empty list.
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

    /** Cached PCA basis for realignSpikes.  A PCA-Center Align All batch shares
     *  one group's basis, so it is read from .pca/.pcaD once and reused for every
     *  cluster instead of re-read per call.  Keyed by path + mtime so it
     *  self-invalidates if the basis file is regenerated. */
    // PcaBasis now lives in libneurosuite-core (header-only); the field layout
    // is identical, the cached member and the inline validating loader populate
    // it unchanged, and realign shares pcaProjectionEnergy with the
    // process_alignspikes_pca plugin and the CUDA kernel.
    using PcaBasis = neurosuite::core::PcaBasis;
    PcaBasis realignPcaCache;
    QString  realignPcaCachePath;
    qint64   realignPcaCacheMtime = -1;

    /** Steady-clock ms at the end of the previous realignSpikes call, used by
     *  NS3_REALIGN_TIMING to report the inter-cluster gap (the batch worker's
     *  per-iteration bookkeeping + clusterDone emit).  -1 = no previous call. */
    long long realignPrevEndMs = -1;
    
    /**The url of the document.*/
    QString docUrl;
    
    /**The url for the temporary saved file.*/
    QString cluFileSaveUrl;
    
    /**The base name of the document. */
    QString baseName;

    /**The path to the YAML parameter file. */
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
    Data* clusteringData = nullptr;

    // ── hierarchical (.clc) child clustering (optional; null unless loaded) ──
    Data*       childData       = nullptr;   // second clustering read from the .clc sibling
    ItemColors* childColorList  = nullptr;   // colours for the child clusters
    Data*       activeData      = nullptr;   // VIEW-facing: == clusteringData, or childData
    ItemColors* activeColorList = nullptr;   // parallels activeData

    // Data pinned for the in-flight recluster.  Set when the temp .fet is built
    // (== data(), i.e. childData in hierarchical mode) and consumed at
    // integrate/UI time, so an async selection change between launch and the
    // KlustaKwik exit cannot retarget the integration.  null when no recluster
    // is in flight.  See createFeatureFile / integrateReclusteredClusters /
    // reclusteringUpdate.
    Data*       reclusterTarget = nullptr;
    QString     clcSiblingPath;              // resolved .clc path ("" if none detected)
    QString     clpSiblingPath;              // resolved .clp child->parent map ("" if none)
    // Atom-layer (childData) edits live on their OWN undo timeline, because the
    // main undo() is hardwired to clusteringData.  Each entry records the child
    // clusters that changed, for childData->undo()/redo() (cache invalidation)
    // and for the deterministic colour re-sync.  rebuildHierarchyFromData() then
    // keeps the fiber maps consistent regardless of which timeline moved.
    struct ChildEdit { QList<int> added; QList<int> modified; QList<int> deleted; };
    QList<ChildEdit> childUndoStack;
    QList<ChildEdit> childRedoStack;
    // Which layer the most recent edit/undo/redo touched, so the atom-undo
    // (Ctrl+Shift+Z) can warn when it would revert an atom edit that is NOT the
    // user's most recent action (their last action was a fiber-layer edit).
    enum class EditLayer { None, Parent, Atom };
    EditLayer lastEditLayer = EditLayer::None;
    // One marker per edit on the unified order timeline.  For a parent edit that
    // also re-cut the child layer (the covering-atom mint after a parent recluster,
    // or resyncStraddlingAtoms after a parent split -- both via moveSpikeSubset,
    // which pushes no childData undo level), childPre holds the child label snapshot
    // taken BEFORE the re-cut, so a parent undo can revert it atomically; childPost
    // is filled at undo time so redo can re-apply it.  Empty for edits with no
    // child re-cut (every atom edit, and parent edits made with no child layer).
    struct EditMarker {
        EditLayer         layer;
        QVector<dataType> childPre;
        QVector<dataType> childPost;
    };
    QList<EditMarker> editOrderUndo;
    QList<EditMarker> editOrderRedo;
    // Record one atom-layer edit: push its ChildEdit + an Atom order marker, and
    // invalidate the atom + unified redo.  The single funnel for the child edits.
    void recordChildEdit(const ChildEdit& e);
    // sibling paths captured at open so the child clustering can be re-read lazily
    QString     siblingFetPath, siblingSpkPath, siblingYamlPath;
    long        siblingSpkFileLength = 0;
    bool        siblingYamlForm = false;
    QMap<int,QList<int>> parentToChildren;   // parent unit id -> its child ids
    QMap<int,int>        childToParent;      // child id -> parent unit id
    QSet<int>            childScopeVisible;  // children currently shown in the child palette
    bool                 childScopeActive = false;  // true while a child is the shown clustering
    void buildHierarchyMaps();               // fill parentToChildren/childToParent from .clu/.clc

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
    int gain = 0;

    /**Acquisition system gain.*/
    int acquisitionGain = 0;

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
    std::vector<PendingRealign> pendingRealign;

    // ── Curation logger ───────────────────────────────────────────────────
    std::unique_ptr<CurationLogger> curationLogger;

    /** Snapshot @p clusterIds and open a "before" log block for @p action.
     *  No-op if the logger is not open.  Returns quietly on empty id list. */
    void logBefore(CurationLogger::ActionType action, const QList<int>& clusterIds);

    /** Snapshot @p clusterIds and write "after" records for the current block.
     *  Must follow a matching logBefore() call for the same action. */
    void logAfter(const QList<int>& clusterIds);

    /// Per-cluster action-touch count for the current session.
    /// Incremented in logBefore() so every cluster snapshot carries a depth.
    QMap<int, int> clusterActionCount;

    /// action_idx of the most recently begun log action — used by undo/redo
    /// to record which preceding action index is being reverted or replayed.
    int lastLoggedActionIdx = -1;

    /// Batch-scoped cache of all cluster centroids.  While enabled (during a
    /// PCA-Center Align All), snapshotClusters() computes the full-dataset
    /// computeAllCentroids() pass once and reuses it, instead of twice per
    /// cluster.  Per-cluster realign logging stays on, so the log still streams
    /// cluster-by-cluster and undo stays per-cluster.  The cached centroids
    /// reflect batch-start geometry; during the batch the snapshots' nearest-
    /// cluster audit metric is therefore relative to that fixed reference (the
    /// realign only shifts spikes within a cluster, so this is a close
    /// approximation and the field is informational only).
    bool                              centroidCacheEnabled = false;
    mutable bool                      centroidCacheValid   = false;
    mutable QMap<int,QVector<double>> centroidCache;

    /// Return all-cluster centroids, populating the batch cache on first use.
    const QMap<int,QVector<double>>& cachedCentroids() const {
        if (!centroidCacheValid) {
            centroidCache      = clusteringData->computeAllCentroids();
            centroidCacheValid = true;
        }
        return centroidCache;
    }

public:
    /** Enable the batch-scoped centroid cache for a PCA-Center Align All run so
     *  every cluster's per-cluster logBefore/logAfter reuses a single
     *  computeAllCentroids() pass.  Per-cluster logging is left ON (the log
     *  streams per cluster and undo stays per-cluster).  Pairs with
     *  endRealignBatchLog().  @p clusterIds is unused (kept for call-site
     *  compatibility). */
    void beginRealignBatchLog(const QList<int>& clusterIds);

    /** Disable and clear the batch-scoped centroid cache opened by
     *  beginRealignBatchLog(). */
    void endRealignBatchLog();

public:
    // (logAnnotation was removed when the J/K/X manual annotation
    //  shortcuts were retired.  Curation status is now inferred
    //  automatically from undo/redo — see CurationLogger::notifyUndo /
    //  notifyRedo, called from KlustersDoc::undo() / redo().)

};

#endif // KCLUSTERSDOC_H
