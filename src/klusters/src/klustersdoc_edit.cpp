// klustersdoc_edit.cpp — KlustersDoc cluster-editing operations.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// interactive cluster mutations of KlustersDoc — group/merge, move-spike-subset,
// delete (clusters / artifact / noise / spikes-to-cluster), and the new-cluster
// creation paths (createNewCluster(s) and their commit helpers).  Declarations
// remain in klustersdoc.h; mechanical relocation, no logic change.  Carries the
// same include preamble as klustersdoc.cpp so every symbol resolves identically.
//
#include <algorithm>
#include <functional>
#include <numeric>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cerrno>          // patch63 — saveDocument errno diagnostics
#include <cstring>         // patch63 — strerror
#include <vector>
#include <stdint.h>
#ifdef _OPENMP
#include <omp.h>            // CPU-fallback realign parallelisation
#endif
#include <QElapsedTimer>    // opt-in per-phase realign timing
#include <chrono>           // inter-cluster gap timestamp (steady_clock)
/***************************************************************************
                          klustersdoc.cpp  -  description
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

// include files for Qt
#include <QDir>
#include <QFile>
#include <QWidget>
#include <QStringList>
#include <QString>
#include <QTimer>
#include <QDateTime>
#include <QApplication>

#include <QList>

#include <QEvent>
#include <QMessageBox>
#include <QDebug>
#include <QAction>
#include <QUrl>
#include <QRegularExpression>
#include <QTextStream>

// application specific includes
#include "processwidget.h"
#include "klusters.h"
#include "klustersdoc.h"
#include "configuration.h"
#include "klustersview.h"
#include "watershed2d.h"
#include "clusterview.h"
#include "klustersdoc.h"
#include "clusterPalette.h"
#include "types.h"
#include "autosavethread.h"
#include "parameteryamlmodifier.h"
#include "dipsplit.h"
#include "parameteryamlreader.h"
#include "clusteruserinformation.h"

//C, C++ include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <math.h>
#include <climits>

#include "timer.h"

#include <neurosuite/core/neurofileio.h>  // variant-aware input resolution
#include <neurosuite/core/custody.hpp>     // shared chain-of-custody policy

int KlustersDoc::groupClusters(QList<int> clustersToGroup,KlustersView& activeView){
    //Call data to group the clusters
    logBefore(CurationLogger::ActionType::GROUP, clustersToGroup);

    // Quiesce every background view thread BEFORE mutating Data.  groupClusters
    // renumbers/reassigns spikes in clusteringData in place; a WaveformView,
    // CorrelationView or matrix thread reading the old cluster layout at that
    // instant gets a torn read or a stale array index -> the non-deterministic
    // segfault seen on grouping (including grouping two parent fibers, which
    // routes here via mergeParentFibers).  This is the same guard the undo
    // (klustersdoc_undo) and realign (klustersdoc_realign) paths already take
    // before their in-place Data mutations; group was the one mutating primitive
    // that omitted it.
    for (KlustersView* view : *viewList)
        view->stopAllViewThreads();

    float newClusterId = clusteringData->groupClusters(clustersToGroup);
    int newClusterIdint = static_cast<int>(newClusterId);

    //Prepare the undo
    prepareUndo(newClusterIdint,clustersToGroup);

    //Add the cluster in clusterColors.
    QColor color;
    color.setHsv(static_cast<int>(fmod(newClusterId*7,36))*10,200,255);
    clusterColorList->append(newClusterIdint,color);

    //Remove the clusters which were grouped
    QList<int>::iterator clustersToRemove;
    QList<int>::iterator clustersToRemoveEnd(clustersToGroup.end());
    for (clustersToRemove = clustersToGroup.begin(); clustersToRemove != clustersToRemoveEnd; ++clustersToRemove ){
        clusterColorList->remove(*clustersToRemove);
    }

    //Notify all the views of the modification

    for (KlustersView* view : *viewList) {
        const bool isActive = (view == &activeView);
        view->groupedClustersUpdate(clustersToGroup, newClusterIdint, isActive);
        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    //Notify the errorMatrixView of the modification
    emit clustersGrouped(clustersToGroup,newClusterIdint);
    updateSimilarityMatrices();   // recompute open error/template/residual matrices

    //Reset the color status in clusterColors if need it
    if(clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    //Ask the active view to take the modification into account immediately
    activeView.showAllWidgets();

    //Update the palette of cluster
    clusterPalette.updateClusterList();
    QList<int> clustersToShow;
    clustersToShow.append(newClusterIdint);
    clusterPalette.selectItems(clustersToShow);
    logAfter(clustersToShow);
    return newClusterIdint;
}


void KlustersDoc::noteModifiedFiber(int clusterId)
{
    if (clusterId > 1 && !modifiedFibers.contains(clusterId))
        modifiedFibers.append(clusterId);
}

QList<int> KlustersDoc::takeModifiedFibers()
{
    QList<int> out = modifiedFibers;
    modifiedFibers.clear();
    return out;
}

void KlustersDoc::setPendingFiberSelection(const QList<int>& fibers)
{
    pendingFiberSelection.clear();
    for (int f : fibers)
        if (f > 1 && !pendingFiberSelection.contains(f))
            pendingFiberSelection.append(f);   // keep order; first stays primary
}

QList<int> KlustersDoc::takePendingFiberSelection()
{
    QList<int> out = pendingFiberSelection;
    pendingFiberSelection.clear();
    return out;
}

void KlustersDoc::renumberPendingFiberSelection(const QMap<int,int>& oldNew)
{
    for (int& id : pendingFiberSelection)
        if (oldNew.contains(id)) id = oldNew.value(id);
}

void KlustersDoc::moveSpikeSubsetToCluster(int fromCluster,
                                            const QVector<int>& spkFileIndices,
                                            int toCluster,
                                            KlustersView& activeView)
{
    if (spkFileIndices.isEmpty()) return;

    logBefore(CurationLogger::ActionType::MOVE_SPIKES,
              QList<int>{ fromCluster, toCluster });

    // Convert 0-based .spk indices to 1-based feature-row indices.
    QSet<dataType> featureRowSet;
    featureRowSet.reserve(spkFileIndices.size());
    for (int idx : spkFileIndices)
        featureRowSet.insert(static_cast<dataType>(idx + 1));

    QList<int> fromClusters, emptiedClusters;
    clusteringData->moveSpikeSubset(fromCluster, featureRowSet,
                                     toCluster, fromClusters, emptiedClusters);

    if (fromClusters.isEmpty()) {
        activeView.showAllWidgets();
        return;
    }

    QList<int> updatedClusters = {fromCluster, toCluster};

    // Ensure cluster 1 (noise) has its grey colour when first receiving spikes.
    if (toCluster == 1 && !clusterColorList->contains(1)) {
        QColor grey;
        grey.setHsv(0, 0, 220);
        if (clusterColorList->contains(0)) clusterColorList->insert(1, grey, 1);
        else                               clusterColorList->insert(1, grey, 0);
    }

    prepareUndo(updatedClusters, emptiedClusters, true);

    QList<int> clustersToShow = {fromCluster, toCluster};
    for (int cid : emptiedClusters) {
        clusterColorList->remove(cid);
        clustersToShow.removeAll(cid);
    }

    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        const bool isActive = (v == &activeView);
        v->removeSpikesFromClustersInView(fromClusters, toCluster, emptiedClusters, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    emit removeSpikesFromClusters(fromClusters, toCluster, emptiedClusters);
    // This always mutates the parent (clusteringData), so from/to are fibers: mark
    // them for the post-edit auto-realign (noise/artifact and emptied ids are
    // filtered out downstream by id>1 + clusterHasMembers).
    noteModifiedFiber(fromCluster);
    noteModifiedFiber(toCluster);
    updateSimilarityMatrices();   // recompute open error/template/residual matrices

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    activeView.showAllWidgets();
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);
    logAfter(clustersToShow);
}


void KlustersDoc::deleteClusters(QList<int> clustersToDelete,KlustersView& activeView,int clusterId){
    // Log before the spikes move so we capture the source cluster characteristics
    logBefore(clusterId == 1 ? CurationLogger::ActionType::DELETE_NOISE
                              : CurationLogger::ActionType::DELETE_ARTEFACT,
              clustersToDelete);

    QList<int> modifiedcluster;
    modifiedcluster.append(clusterId);

    //If only one cluster has been deleted, the following cluster on the list, if any, will be selected.
    //Find that following cluster.
    int clusterToSelect;
    bool existNextCluster = false;
    if(clustersToDelete.size() == 1){
        int clusterToDelete =  clustersToDelete[0];
        bool previous = false;
        QList<dataType> clusters = clusteringData->clusterIds();
        QList<dataType>::iterator clustersIterator;
        for(clustersIterator = clusters.begin(); clustersIterator != clusters.end(); ++clustersIterator){
            if(previous){
                clusterToSelect = static_cast<int>(*clustersIterator);
                existNextCluster = true;
                break;
            }
            if(*clustersIterator == clusterToDelete) previous = true;
        }
    }

    //case where the clusters are moved to the cluster 0 (artefact)
    if(clusterId == 0){
        //Call data to move the clusters
        clusteringData->moveClustersToArtefact(clustersToDelete);
        //Update clusterColors, add cluster 0 if need it
        if(!clusterColorList->contains(0)){
            //Prepare the undo
            prepareUndo(0,modifiedcluster,clustersToDelete);
            QColor color(Qt::red); //Cluster 01 is always red
            clusterColorList->insert(static_cast<int>(0),color,0);
        }
        else
            //Prepare the undo
            prepareUndo(modifiedcluster,clustersToDelete);
    }
    //case where the clusters are moved to the cluster 1 (noise)
    if(clusterId == 1){
        //Call data to move the clusters
        clusteringData->moveClustersToNoise(clustersToDelete);
        //Update clusterColors, add cluster 1 if need it
        if(!clusterColorList->contains(1)){
            //Prepare the undo
            prepareUndo(1,modifiedcluster,clustersToDelete);
            QColor color;
            color.setHsv(0,0,220);//Cluster 1 is always gray
            if(clusterColorList->contains(0)) clusterColorList->insert(static_cast<int>(1),color,1);
            else clusterColorList->insert(static_cast<int>(1),color,0);
        }
        else
            //Prepare the undo
            prepareUndo(modifiedcluster,clustersToDelete);
    }

    //Update clusterColors,remove the clusters which were deleted
    QList<int>::iterator clustersToRemove;
    for (clustersToRemove = clustersToDelete.begin(); clustersToRemove != clustersToDelete.end(); ++clustersToRemove ){
        if(*clustersToRemove == clusterId) continue;
        clusterColorList->remove(*clustersToRemove);
    }

    //Notify all the views of the modification

    for (KlustersView* view : *viewList) {
        const bool isActive = (view == &activeView);
            view->clustersDeletionUpdate(clustersToDelete,clusterId, isActive);
            view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    //Notify the errorMatrixView of the modification
    emit clustersDeleted(clustersToDelete,clusterId);
    // Parent-scope delete: the destination fiber that absorbed the spikes changed
    // membership (child scope deletes atoms, so guard on the scope).
    if (!childScopeActive) noteModifiedFiber(clusterId);
    // Deleted clusters are gone; land the selection on the surviving destination.
    if (!childScopeActive) setPendingFiberSelection({clusterId});
    updateSimilarityMatrices();   // recompute open error/template/residual matrices

    //Reset the color status in clusterColors if need it
    if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

    //Ask the active view to take the modification into account immediately
    activeView.showAllWidgets();

    //Update the palette of cluster
    clusterPalette.updateClusterList();

    //If only one cluster has been deleted, select the following cluster on the list if any.
    if(existNextCluster){
        QList<int> clusters;
        clusters.append(clusterToSelect);

        //Update the cluster palette
        clusterPalette.selectItems(clusters);
        activeView.shownClustersUpdate(clusters);

        //update the TraceView if any
        activeView.updateTraceView(electrodeGroupID,clusterColorList,true);
    }
    // Log the resulting state of the destination cluster (noise=1, artefact=0)
    logAfter(QList<int>{ clusterId });
}

void KlustersDoc::deleteArtifact(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    logBefore(CurationLogger::ActionType::DELETE_REGION_ARTEFACT,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));
    deleteSpikesFromClusters(0,region,clustersOfOrigin,dimensionX,dimensionY);
    logAfter(QList<int>{ 0 });
}


void KlustersDoc::deleteNoise(QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    logBefore(CurationLogger::ActionType::DELETE_REGION_NOISE,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));
    deleteSpikesFromClusters(1,region,clustersOfOrigin,dimensionX,dimensionY);
    logAfter(QList<int>{ 1 });
}

void KlustersDoc::deleteSpikesFromClusters(int destination, QRegion& region,const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    QList<int> clustersToShow(clustersOfOrigin);

    clusteringData->deleteSpikesFromClusters(region,clustersOfOrigin,destination,dimensionX,dimensionY,fromClusters,emptyClusters);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //check if any spikes have been selected
    if(fromClusters.isEmpty()){
        activeView->selectionIsEmpty();
        activeView->showAllWidgets();
    }
    else{
        QList<int> updatedClusters = QList<int>(fromClusters);
        updatedClusters.append(destination);

        //Update clusterColors, add cluster 1 if need it
        if(destination == 1 && !clusterColorList->contains(1)){
            //Prepare the undo
            prepareUndo(1,updatedClusters,emptyClusters,true);
            QColor color;
            color.setHsv(0,0,220);//Cluster 1 is always gray
            if(clusterColorList->contains(0)) clusterColorList->insert(static_cast<int>(1),color,1);
            else clusterColorList->insert(static_cast<int>(1),color,0);
        }
        //Update clusterColors, add cluster 0 if need it
        else if(destination == 0 && !clusterColorList->contains(0)){
            //Prepare the undo
            prepareUndo(0,updatedClusters,emptyClusters,true);
            QColor color(Qt::red); //Cluster 01 is always red
            clusterColorList->insert(static_cast<int>(0),color,0);
        }
        else
            //Prepare the undo
            prepareUndo(updatedClusters,emptyClusters,true);

        //Remove all the empty clusters from clusterColors and clustersToShow
        if(!emptyClusters.isEmpty()){
            QList<int>::iterator clustersToRemove;
            for (clustersToRemove = emptyClusters.begin(); clustersToRemove != emptyClusters.end(); ++clustersToRemove ){
                clusterColorList->remove(*clustersToRemove);
                clustersToShow.removeAll(*clustersToRemove);
            }
        }

        //Notify all the views of the modification

        for (KlustersView* view : *viewList) {
            const bool isActive = (view == activeView);
                view->removeSpikesFromClustersInView(fromClusters,destination,emptyClusters, isActive);
                view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
        }

        //Notify the errorMatrixView of the modification
        emit removeSpikesFromClusters(fromClusters,destination,emptyClusters);
        updateSimilarityMatrices();   // recompute open error/template/residual matrices

        //Reset the color status in clusterColors if need it
        if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

        activeView->showAllWidgets();

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);

        //Notify the application that spikes have been deleted.
        emit spikesDeleted();
    }
}


// ---------------------------------------------------------------------------
// KlustersDoc::commitClusterCreation
//
// Shared post-mutation UI plumbing for any operation that produces ONE new
// cluster derived from existing ones.  Used by createNewCluster (polygon
// selection) and dipSplitApply (bimodality split commit).  Keeping this in
// one place ensures these paths can't drift apart and re-introduce bugs
// like the missing palette refresh that hid DipSplit's freshly-created
// cluster.
// ---------------------------------------------------------------------------
void KlustersDoc::commitClusterCreation(int newId,
                                         QList<int>& fromClusters,
                                         QList<int>& emptiedClusters,
                                         KlustersView* activeView)
{
    // Undo bookkeeping FIRST — before mutating clusterColorList.
    //
    // prepareUndo() deep-copies the current clusterColorList onto the undo
    // stack and swaps in a fresh copy as the new "current".  If we append
    // the new cluster's colour BEFORE this snapshot, both the snapshot AND
    // the new current contain the new cluster — so when the user later
    // undoes the action, the colour-list rolls back to a state that still
    // has the new cluster's entry, even though Data::undo has rolled the
    // spike table back to a state where the new cluster has no spikes.
    // The palette then renders an icon for a cluster that no longer
    // exists in Data; clicking it dereferences nothing → segfault.
    //
    // The matching pattern in groupClusters (line 1032) gets this right:
    // prepareUndo is called BEFORE clusterColorList->append.
    prepareUndo(newId, fromClusters, emptiedClusters);

    // Register colour AFTER the snapshot is captured — this puts newId
    // into the post-action clusterColorList, which is what the view
    // notifications below need to look up its colour.
    QColor color;
    color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10, 200, 255);
    clusterColorList->append(newId, color);

    // Build clustersToShow: active view's current set + new cluster −
    // emptied clusters.
    QList<int> clustersToShow;
    if (activeView) {
        const QList<int>& cur = activeView->clusters();
        for (int c : cur) clustersToShow.append(c);
    }
    if (!clustersToShow.contains(newId)) clustersToShow.append(newId);

    // Drop emptied clusters from the colour list and to-show list.
    for (int cid : emptiedClusters) {
        clusterColorList->remove(cid);
        clustersToShow.removeAll(cid);
    }

    // Per-view notification.
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* view = viewList->at(i);
        const bool isActive = (view == activeView);
        view->addNewClusterToView(fromClusters, newId,
                                  emptiedClusters, isActive);
        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    // Notify the error-matrix / template-matrix views.
    emit newClusterAdded(fromClusters, newId, emptiedClusters);
    updateSimilarityMatrices();   // recompute open error/template/residual matrices

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    // Refresh active view widgets + cluster palette.
    if (activeView) activeView->showAllWidgets();
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);

    // Invalidate waveform + correlogram caches for every source cluster
    // that lost spikes — the cached mean waveform / correlogram histogram
    // no longer matches the on-disk spike list.  Also tell every view to
    // re-draw these clusters so any cached pixmaps are dropped and the
    // worker threads re-launch.  The new cluster itself has no caches yet,
    // so invalidation isn't required for it; its waveform thread is
    // launched by addNewClusterToView above.
    //
    // Data::createNewCluster does equivalent invalidation internally;
    // Data::moveSpikeSubset does not, so DipSplit needs it here.  Running
    // both for createNewCluster's path is idempotent — invalidate on an
    // empty cache is a no-op.
    for (int cid : fromClusters) {
        if (emptiedClusters.contains(cid)) continue;   // already removed
        invalidateWaveformCache(cid);
        invalidateCorrelogramCache(cid);
    }
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        for (int cid : fromClusters) {
            if (emptiedClusters.contains(cid)) continue;
            v->invalidateClusterDisplay(cid);
        }
    }
}

// ---------------------------------------------------------------------------
// KlustersDoc::commitTwoClusterCreation
//
// Sibling of commitClusterCreation for paths that produce TWO new clusters
// from one (or more) source clusters in a single atomic Data mutation.
// Used by dipSplitApply.
//
// Architecturally this is the recluster pattern (as used by watershed):
//   - prepareReclusteringUndo(newClusterList, inputs)  — single doc-side
//     undo entry covering N source clusters dissolving into M new
//     clusters.
//   - addNewClustersToView(inputs, newClusterList, …) — single view-side
//     undo entry; the recluster-variant primitive does all source-removal
//     and new-cluster registration in one bookkeeping pass.
//   - emit newClustersAdded(inputs)                   — recluster-shaped
//     signal for matrix views (single, takes only the dissolved-sources
//     list).
//
// Three-stack symmetry: KlustersDoc undo, Data undo (already pushed by
// splitClusterTwoWays), and KlustersView undo all get exactly ONE entry.
// One Ctrl+Z fully reverts; one Ctrl+Y fully replays.  Matches the
// watershed precedent verbatim.
// ---------------------------------------------------------------------------
void KlustersDoc::commitTwoClusterCreation(int leftId,
                                            int rightId,
                                            QList<int>& fromClusters,
                                            QList<int>& emptiedClusters,
                                            KlustersView* activeView)
{
    QList<int> newClusterList;
    newClusterList.append(leftId);
    newClusterList.append(rightId);

    // ── Doc-level undo: prepareReclusteringUndo treats the operation
    // ── as N→M (sources dissolving, new clusters appearing).  Single
    // ── doc-side entry.
    prepareReclusteringUndo(newClusterList, emptiedClusters);

    // ── Colour palette update: build clustersToShow exactly like
    // ── watershed's main branch: start from "currently shown except
    // ── the dissolved sources", then append the new IDs.
    QList<int> clustersToShow;
    if (activeView) {
        const QList<int> currentlyShown = activeView->clusters();
        for (int c : currentlyShown)
            if (!emptiedClusters.contains(c)) clustersToShow.append(c);
    }
    QColor color;
    for (int newId : newClusterList) {
        color.setHsv(static_cast<int>(std::fmod(newId * 7.0, 36.0)) * 10,
                     200, 255);
        clusterColorList->append(newId, color);
        clustersToShow.append(newId);
    }
    for (int oldId : emptiedClusters)
        clusterColorList->remove(oldId);

    // ── View notification: one call per view, recluster-variant
    // ── primitive — single view-side undo entry per view.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == activeView);
        v->addNewClustersToView(emptiedClusters, newClusterList, isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }
    // Recluster-shaped matrix-view notification: takes only the
    // sources-dissolved list, not per-cluster from-list.
    emit newClustersAdded(emptiedClusters);
    updateSimilarityMatrices();   // recompute open error/template/residual matrices

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    if (activeView) activeView->showAllWidgets();

    clusterPalette.updateClusterList();
    clusterPalette.selectItems(clustersToShow);

    // Caches: source clusters' caches are stale (they're gone), but the
    // dissolved-cluster entries vanish with the cluster itself.  No-op
    // loop kept for symmetry with commitClusterCreation in case a
    // future caller passes a non-emptied modifier in fromClusters.
    for (int cid : fromClusters) {
        if (emptiedClusters.contains(cid)) continue;
        invalidateWaveformCache(cid);
        invalidateCorrelogramCache(cid);
    }
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* v = viewList->at(i);
        for (int cid : fromClusters) {
            if (emptiedClusters.contains(cid)) continue;
            v->invalidateClusterDisplay(cid);
        }
    }
}


void KlustersDoc::createNewCluster(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    //Get the active view.
    KlustersView* activeView = app()->activeView();

    // Snapshot source clusters before the data mutation
    logBefore(CurationLogger::ActionType::SPLIT,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));

    // Route through the ACTIVE clustering: childData when a child is selected (the
    // split becomes a new sibling atom under the same parent), clusteringData
    // otherwise -- data() == clusteringData then, so the parent path is unchanged.
    Data& targetData = data();
    const bool onChild = (&targetData == childData);
    float newClusterId = targetData.createNewCluster(region,clustersOfOrigin,dimensionX,dimensionY,fromClusters,emptyClusters);

    //Check if a new cluster has been created
    if(newClusterId == 0){
        activeView->selectionIsEmpty();
        activeView->showAllWidgets();
    }
    else if(onChild){
        // Child split: the drawn spikes are a subset of one child atom, so the new
        // atom is a sibling under the SAME parent.  Refresh the child layer and let
        // the child palette select + focus the new sibling.
        const int newAtom = static_cast<int>(newClusterId);
        // Child split is one self-snapshotting childData edit (Data::createNewCluster
        // calls prepareUndo once) -> one ChildEdit on the atom-undo timeline, so
        // Ctrl+Shift+Z reverts it: 'added' is removed and the sources restored.
        ChildEdit e; e.added = { newAtom }; e.modified = fromClusters; e.deleted = emptyClusters;
        recordChildEdit(e);
        syncChildColors();
        rebuildHierarchyFromData();
        emit hierarchyChanged();
        emit hierarchyChildrenCreated(QList<int>{newAtom});
        modified = true;
        logAfter(QList<int>{newAtom});
    }
    else{
        const int newClusterIdint = static_cast<int>(newClusterId);

        // All UI plumbing — colour registration, undo, view notifications,
        // palette refresh — is in the shared helper.
        commitClusterCreation(newClusterIdint, fromClusters, emptyClusters,
                              activeView);
        // Parent-scope split: the new fiber and its source fibers changed membership.
        noteModifiedFiber(newClusterIdint);
        for (int src : fromClusters) noteModifiedFiber(src);
        setPendingFiberSelection({newClusterIdint});   // land on the split-off fiber

        // Hierarchical mode: the split leaves the child atoms where they were, so the new
        // fiber has no covering child and the source fiber's atom now straddles both.  Re-
        // establish the invariant immediately -- the new fiber gets its self child and any
        // straddler collapses -- instead of leaving the layers inconsistent until a manual
        // refiberize.  childData-guarded; flat sessions are untouched.
        if (childData) refiberize();

        // Log after: surviving source clusters + the new cluster
        QList<int> resultIds;
        for (int id : fromClusters)
            if (!emptyClusters.contains(id))
                resultIds.append(id);
        resultIds.append(newClusterIdint);

        // Manual-split detail: label this polygon split like the algorithmic
        // ones (KNN/watershed) and, crucially, preserve the projection
        // (dimensionX, dimensionY) the curator drew it in — the discriminating
        // view that separated the sub-units, otherwise lost.
        if (curationLogger && curationLogger->isOpen()) {
            QStringList srcList;
            for (int id : fromClusters) srcList << QString::number(id);
            QMap<QString, QVariant> details;
            details.insert(QStringLiteral("algorithm"),     QStringLiteral("manual_polygon"));
            details.insert(QStringLiteral("status"),        QStringLiteral("accepted"));
            details.insert(QStringLiteral("source_cluster"),
                           fromClusters.size() == 1 ? fromClusters.first() : -1);
            details.insert(QStringLiteral("source_clusters"), srcList.join(QLatin1Char(',')));
            details.insert(QStringLiteral("n_source_clusters"), static_cast<int>(fromClusters.size()));
            details.insert(QStringLiteral("new_cluster"),    newClusterIdint);
            details.insert(QStringLiteral("n_new_clusters"),  1);
            details.insert(QStringLiteral("dimension_x"),     dimensionX);
            details.insert(QStringLiteral("dimension_y"),     dimensionY);
            details.insert(QStringLiteral("n_emptied_sources"), static_cast<int>(emptyClusters.size()));
            curationLogger->recordActionDetails(details);
        }
        logAfter(resultIds);
    }
}

void KlustersDoc::createNewClusters(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY){
    //list which will contain the clusters really having spikes in the region of selection.
    QList <int> fromClusters;
    //list which will contain the clusters which became empty because all their spikes were in the region of selection.
    QList <int> emptyClusters;
    QList<int> clustersToShow(clustersOfOrigin);
    //Get the active view.
    KlustersView* activeView = app()->activeView();

    logBefore(CurationLogger::ActionType::SPLIT_N,
              QList<int>(clustersOfOrigin.begin(), clustersOfOrigin.end()));

    QList <int> newClusters;
    // Route through the ACTIVE clustering (childData when a child is selected ->
    // the new clusters are sibling atoms under the same parent; clusteringData
    // otherwise, unchanged parent path).
    Data& targetData = data();
    const bool onChild = (&targetData == childData);
    QMap<int,int> fromToNewClusterIds = targetData.createNewClusters(region,clustersOfOrigin,dimensionX,dimensionY,emptyClusters);
    newClusters = fromToNewClusterIds.values();
    fromClusters = fromToNewClusterIds.keys();

    //Check if at least one new cluster has been created
    if(newClusters.size() == 0){
        activeView->selectionIsEmpty();
        activeView->showAllWidgets();
    }
    else if(onChild){
        // Child split: the new atoms are siblings under the same parent(s).
        // Refresh the child layer and let the child palette select + focus them.
        std::sort(newClusters.begin(), newClusters.end());
        // Child split: one self-snapshotting childData edit (Data::createNewClusters
        // calls prepareUndo once, even for several new atoms) -> one ChildEdit, so
        // Ctrl+Shift+Z reverts the whole split in a single step.
        ChildEdit e; e.added = newClusters; e.modified = fromClusters; e.deleted = emptyClusters;
        recordChildEdit(e);
        syncChildColors();
        rebuildHierarchyFromData();
        emit hierarchyChanged();
        emit hierarchyChildrenCreated(newClusters);
        modified = true;
        logAfter(newClusters);
    }
    else{
        //Prepare the undo
        prepareUndo(newClusters,fromClusters,emptyClusters);

        //Add the clusters in clusterColors and clustersToShow.
        QColor color;
        QList<int>::iterator clustersToCreate;
        std::sort(newClusters.begin(), newClusters.end());
        for (clustersToCreate = newClusters.begin(); clustersToCreate != newClusters.end(); ++clustersToCreate ){
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,200,255);
            clusterColorList->append(*clustersToCreate,color);
            clustersToShow.append(*clustersToCreate);
        }
        //Remove all the empty clusters
        if(!emptyClusters.isEmpty()){
            QList<int>::iterator clustersToRemove;
            for (clustersToRemove = emptyClusters.begin(); clustersToRemove != emptyClusters.end(); ++clustersToRemove ){
                clusterColorList->remove(*clustersToRemove);
                clustersToShow.removeAll(*clustersToRemove);
            }
        }

        //Notify all the views of the modification

        for (KlustersView* view : *viewList) {
            const bool isActive = (view == activeView);
                view->addNewClustersToView(fromToNewClusterIds,emptyClusters, isActive);
                view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
        }

        //Notify the errorMatrixView of the modification
        emit newClustersAdded(fromToNewClusterIds,emptyClusters);
        // Parent-scope multi-split: the new fibers (values) and their sources (keys)
        // changed membership.  In child scope these are atoms, so guard on the scope.
        if (!childScopeActive) {
            for (int nf : fromToNewClusterIds.values()) noteModifiedFiber(nf);
            for (int sf : fromToNewClusterIds.keys())   noteModifiedFiber(sf);
            setPendingFiberSelection(fromToNewClusterIds.values());   // land on the new fibers
        }
        updateSimilarityMatrices();   // recompute open error/template/residual matrices


        //Reset the color status in clusterColors if need it
        if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

        activeView->showAllWidgets();

        //Update the palette of cluster
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);

        // Hierarchical mode: the split leaves the child atoms where they were, so each new
        // fiber has no covering child and the source fibers' atoms now straddle.  Re-establish
        // the invariant immediately -- every new fiber gets its self child and straddlers
        // collapse -- instead of leaving the layers inconsistent until a manual refiberize.
        // childData-guarded; flat sessions are untouched.
        if (childData) refiberize();

        // Log after: surviving sources + all newly created clusters
        {
            QList<int> resultIds;
            for (int id : fromClusters)
                if (!emptyClusters.contains(id))
                    resultIds.append(id);
            for (int id : newClusters)
                resultIds.append(id);

            // Manual SPLIT_N detail: per-source→new mapping and the projection
            // the curator drew it in (see createNewCluster for rationale).
            if (curationLogger && curationLogger->isOpen()) {
                QStringList srcList, newList, pairList;
                for (int id : fromClusters) srcList << QString::number(id);
                for (int id : newClusters)  newList << QString::number(id);
                for (auto it = fromToNewClusterIds.constBegin();
                          it != fromToNewClusterIds.constEnd(); ++it)
                    pairList << (QString::number(it.key()) + QLatin1Char(':')
                                 + QString::number(it.value()));
                QMap<QString, QVariant> details;
                details.insert(QStringLiteral("algorithm"),       QStringLiteral("manual_polygon_n"));
                details.insert(QStringLiteral("status"),          QStringLiteral("accepted"));
                details.insert(QStringLiteral("source_clusters"), srcList.join(QLatin1Char(',')));
                details.insert(QStringLiteral("n_source_clusters"), static_cast<int>(fromClusters.size()));
                details.insert(QStringLiteral("new_clusters"),    newList.join(QLatin1Char(',')));
                details.insert(QStringLiteral("n_new_clusters"),  static_cast<int>(newClusters.size()));
                details.insert(QStringLiteral("from_to"),         pairList.join(QLatin1Char(',')));
                details.insert(QStringLiteral("dimension_x"),     dimensionX);
                details.insert(QStringLiteral("dimension_y"),     dimensionY);
                details.insert(QStringLiteral("n_emptied_sources"), static_cast<int>(emptyClusters.size()));
                curationLogger->recordActionDetails(details);
            }
            logAfter(resultIds);
        }
    }
}
