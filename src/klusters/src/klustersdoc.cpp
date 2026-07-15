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
#include "klustersdoc_internal.h"  // shared custody path helpers (split TUs)

extern int nbUndo;

// Chain-of-custody path helpers now live in klustersdoc_internal.h so the split
// klustersdoc_*.cpp translation units share one definition instead of each
// carrying a private anonymous-namespace copy.  Pull the three names into this TU.
using klustersdoc_internal::featureMethod;
using klustersdoc_internal::resolveFeature;
using klustersdoc_internal::stripFeatureSuffix;

KlustersDoc::KlustersDoc(QWidget* parent,ClusterPalette& clusterPalette,bool autoSave,int savingInterval)
    : clusterColorListUndoList(),clusterColorListRedoList(),modified(false),docUrl(),parent(parent),clusterPalette(clusterPalette),
    addedClustersUndoList(),addedClustersRedoList(),modifiedClustersUndoList(),modifiedClustersRedoList()
  ,autoSave(autoSave),savingInterval(savingInterval),tracesProvider(nullptr),clustersProvider(nullptr),channelColorList(nullptr)
{
    viewList = new QList<KlustersView*>();
    clusterColorList = nullptr;
    addedClusters = nullptr;
    modifiedClusters = nullptr;
    deletedClusters = nullptr;
    endAutoSaving = false;
    autoSaveThread = nullptr;
}

KlustersDoc::~KlustersDoc(){
    NS3_DIAG() << "~KlustersDoc()";

    // Disconnect all signals between KlustersDoc and KlustersViews before
    // any object starts being destroyed.  Views are parented to Qt widgets
    // and deleted later by the parent-child hierarchy; if doc signals are
    // still connected when those deletions run, Qt dispatches into a dead
    // object and asserts "class destructor may have already run".
    for (KlustersView* v : qAsConst(*viewList)) {
        if (v) {
            QObject::disconnect(this, nullptr, v, nullptr);
            QObject::disconnect(v,    nullptr, this, nullptr);
        }
    }
    delete viewList;

    if(clusterColorList != nullptr){
        delete clusteringData;
        delete clusterColorList;
        delete addedClusters;
        delete modifiedClusters;
        delete deletedClusters;
    }

    //If an autoSaveThread exists and has not finish, wait until it is done
    if(autoSave && autoSaveThread != nullptr){
        if(!autoSaveThread->isRunning()){
            autoSaveThread->removeTmpFile();
            delete autoSaveThread;
            autoSaveThread = nullptr;
        }
        else{
            endAutoSaving = true;
            while(!autoSaveThread->wait()){};
            //Wait that the customEvent has process the AutoSaveEvent and deleted the autoSaveThread
            while(autoSaveThread != nullptr){};
        }
    }
}

void KlustersDoc::addView(KlustersView *view)
{
    viewList->append(view);
}

void KlustersDoc::removeView(KlustersView *view){
    viewList->removeAll(view);
}


bool KlustersDoc::isLastView() {
    return (static_cast<int>(viewList->count()) == 1);
}


void KlustersDoc::updateAllViews(KlustersView *sender){
    for(int i =0; i<viewList->count();++i)
    {
        KlustersView *view = viewList->at(i);
        view->update(sender);
    }

}

void KlustersDoc::refreshAllViews()
{
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* view = viewList->at(i);
        view->update(nullptr);         // repaint scatter + waveform
        view->updateViewContents();    // fires updateDrawing() → askForCorrelograms()
    }
}

void KlustersDoc::forceClusterRefresh(int clusterId)
{
    for (int i = 0; i < viewList->count(); ++i)
        viewList->at(i)->forceClusterRefresh(clusterId);

    // Hierarchical (.clc child layer): a parent-fiber realign shifts spikes that
    // also belong to the fiber's child atoms.  A child-scoped view will NOT
    // refresh on the parent id -- KlustersView::forceClusterRefresh() only acts
    // when the id is in that view's shownClusters, which for a selected child is
    // the child ids -- so it keeps drawing pre-realign waveforms.  Mirror the
    // child propagation already done in invalidateWaveformCache(): refresh the
    // fiber's children, plus the parent fiber when clusterId is itself a child.
    // The per-view guard makes this a no-op for any view not showing the id, so
    // it costs nothing outside hierarchical mode.
    if (childData) {
        for (int kid : childrenOf(QList<int>{ clusterId }))
            for (int i = 0; i < viewList->count(); ++i)
                viewList->at(i)->forceClusterRefresh(kid);
        const int parent = parentOfChild(clusterId);
        if (parent > 0)
            for (int i = 0; i < viewList->count(); ++i)
                viewList->at(i)->forceClusterRefresh(parent);
    }
}

void KlustersDoc::updateSimilarityMatrices()
{
    // updateErrorMatrix()/updateTemplateMatrix()/updateResidualMatrix() each emit
    // the view's compute signal; Qt delivers it only when a matrix dock is
    // connected, so this is a no-op when none is open and a full recompute when
    // one is.  Same effect as slotUpdateErrorMatrix (the U key) but across all
    // views and driven by the atomic cluster edits rather than a manual keypress.
    for (int i = 0; i < viewList->count(); ++i) {
        KlustersView* view = viewList->at(i);
        view->updateErrorMatrix();
        view->updateTemplateMatrix();
        view->updateResidualMatrix();
        view->updateDriftMatrix();
    }
}

void KlustersDoc::setSelectedChannels(const QList<int>& channels)
{
    // Normalise first: callers hand us raw click order, possibly with repeats,
    // and a stale index if the group changed under them.  Comparing normalised
    // lists is what makes an unchanged commit free -- the waveform view commits
    // on every Ctrl release, whether or not anything actually moved.
    const int nbChannels = clusteringData ? clusteringData->nbOfchannels() : 0;
    QList<int> normalised;
    for (int c : channels) {
        if (c >= 0 && c < nbChannels && !normalised.contains(c))
            normalised.append(c);
    }
    std::sort(normalised.begin(), normalised.end());

    if (normalised == channelSelection)
        return;

    channelSelection = normalised;
    emit selectedChannelsChanged(channelSelection);
}

QList<int> KlustersDoc::getSiblingElectrodeGroups(int groupId) const
{
    QList<int> result;
    if (parameterFile.isEmpty()) return result;

    ParameterYamlReader reader;
    if (!reader.parseFile(parameterFile)) return result;
    return reader.getSiblingElectrodeGroups(groupId);
}

void KlustersDoc::setGain(int acquisitionGain){
    //Notify all the views of the modification
    for(int i =0; i<viewList->count();++i) {
        KlustersView *view = viewList->at(i);
        view->setGain(acquisitionGain);
    }

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::setBackgroundColor(const QColor &backgroundColor){
    //Notify all the views of the modification
    for(int i =0; i<viewList->count();++i) {
        KlustersView *view = viewList->at(i);
        view->updateBackgroundColor(backgroundColor);
    }

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::setMarkerSize(int size){
    for(int i = 0; i < viewList->count(); ++i){
        const QList<ViewWidget*>& views = viewList->at(i)->getViewList();
        for(ViewWidget* w : views){
            ClusterView* cv = qobject_cast<ClusterView*>(w);
            if(cv) cv->setPointSize(size);
        }
    }
}

void KlustersDoc::setSelectionLineWidth(int w){
    for(int i = 0; i < viewList->count(); ++i){
        const QList<ViewWidget*>& views = viewList->at(i)->getViewList();
        for(ViewWidget* vw : views){
            ClusterView* cv = qobject_cast<ClusterView*>(vw);
            if(cv) cv->setSelectionLineWidth(w);
        }
    }
}

void KlustersDoc::setTimeStepInSecond(int step){
    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //Notify all the views of the modification
    for (KlustersView* view : *viewList)
        view->setTimeStepInSecond(step, view == activeView);

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::setChannelPositions(QList<int>& positions){
    //Notify all the views of the modification

    for(int i =0; i<viewList->count();++i) {
        KlustersView *view = viewList->at(i);
        view->setChannelPositions(positions);
    }

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //Ask the active view to take the modification into account immediately
    activeView->showAllWidgets();
}

void KlustersDoc::singleColorUpdate(int clusterId,KlustersView& activeView){
    //Notify all the views of the modification

    for (KlustersView* view : *viewList)
        view->singleColorUpdate(clusterId, view == &activeView);

    //Ask the active view to take the modification into account immediately
    activeView.showAllWidgets();
}

void KlustersDoc::shownClustersUpdate(const QList<int>& clustersToShow,KlustersView& activeView){
    // Use the colours of whichever clustering is active (parent or child).
    ItemColors* colors = activeColorList ? activeColorList : clusterColorList;
    if(colors->isColorChanged()){
        //Notify all the views of the modification

        for (KlustersView* view : *viewList)
            view->updateColors(view == &activeView);

        //Reset the color status in clusterColors
        colors->resetAllColorStatus();

        //Update the palette of clusters -- only for the parent clustering; the
        //child palette is rebuilt explicitly by the app, never from here (which
        //would otherwise overwrite the parent palette with child data).
        if(!childScopeActive){
            clusterPalette.updateClusterList();
            clusterPalette.selectItems(clustersToShow);
        }
    }

    //The new selection of clusters only means for the active view
    activeView.shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView.updateTraceView(electrodeGroupID,colors,true);
}

void KlustersDoc::shownClustersUpdate(const QList<int>& clustersToShow){
    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

// ── cluster masking ─────────────────────────────────────────────────────────
void KlustersDoc::maskClusters(const QList<int>& clustersToMask){
    for(int id : clustersToMask)
        maskedClusters.insert(id);
    applyMask();
}

void KlustersDoc::unmaskClusters(const QList<int>& clustersToUnmask){
    for(int id : clustersToUnmask)
        maskedClusters.remove(id);
    applyMask();
}

void KlustersDoc::setMaskKeeping(const QList<int>& clustersToKeep){
    const QSet<int> keep(clustersToKeep.constBegin(),clustersToKeep.constEnd());
    maskedClusters.clear();
    const QList<dataType> all = clusteringData->clusterIds();
    for(dataType id : all){
        const int cid = static_cast<int>(id);
        if(!keep.contains(cid))
            maskedClusters.insert(cid);
    }
    applyMask();
}

void KlustersDoc::clearMask(){
    if(maskedClusters.isEmpty())
        return;
    maskedClusters.clear();
    applyMask();
}

QList<int> KlustersDoc::maskedClusterIds() const{
    return maskedClusters.values();
}

QList<int> KlustersDoc::unmaskedClusterIds() const{
    QList<int> out;
    const QList<dataType> all = clusteringData->clusterIds();
    out.reserve(all.size());
    for(dataType id : all){
        const int cid = static_cast<int>(id);
        if(!maskedClusters.contains(cid))
            out.append(cid);
    }
    return out;
}

void KlustersDoc::applyMask(){
    //Palette: rebuild so the active list omits masked clusters.
    clusterPalette.updateClusterList();

    //Views: drop any masked cluster that is currently shown (foreground only
    //unmasked clusters).  We do NOT force-show the rest — focusing the view on
    //a subset (e.g. a time chunk) is the caller's decision via shownClustersUpdate.
    KlustersView* activeView = app() ? app()->activeView() : nullptr;
    if(activeView){
        const QList<int> shown = activeView->clusters();
        QList<int> kept;
        kept.reserve(shown.size());
        for(int id : shown)
            if(!maskedClusters.contains(id))
                kept.append(id);
        if(kept.size() != shown.size())
            shownClustersUpdate(kept);
    }
}

// ── time-chunk curation ─────────────────────────────────────────────────────
void KlustersDoc::enterChunkMode(double chunkMinutes, double overlapMinutes, int minSpikes){
    const double fs = clusteringData->getSamplingRate();   // Hz (samples per second)
    if(chunkMinutes <= 0.0 || fs <= 0.0)
        return;
    const double samplesPerMinute = 60.0 * fs;
    chunkLenSamples     = static_cast<long>(chunkMinutes   * samplesPerMinute);
    chunkOverlapSamples = static_cast<long>(overlapMinutes * samplesPerMinute);
    chunkMinSpikes      = (minSpikes < 1) ? 1 : minSpikes;
    sessionMaxSamples   = clusteringData->maxTimeInRecordingUnits();
    nbChunks = (chunkLenSamples > 0)
               ? static_cast<int>((sessionMaxSamples + chunkLenSamples - 1) / chunkLenSamples)
               : 0;
    if(nbChunks < 1)
        nbChunks = 1;
    chunkMode = true;
    currentChunkIndex = 0;
    applyChunkWindow();
}

void KlustersDoc::exitChunkMode(){
    if(!chunkMode)
        return;
    chunkMode = false;
    clearMask();
}

bool KlustersDoc::nextChunk(){
    if(!chunkMode || currentChunkIndex + 1 >= nbChunks)
        return false;
    ++currentChunkIndex;
    applyChunkWindow();
    return true;
}

bool KlustersDoc::prevChunk(){
    if(!chunkMode || currentChunkIndex <= 0)
        return false;
    --currentChunkIndex;
    applyChunkWindow();
    return true;
}

void KlustersDoc::gotoChunk(int index){
    if(!chunkMode)
        return;
    if(index < 0)
        index = 0;
    if(index > nbChunks - 1)
        index = nbChunks - 1;
    currentChunkIndex = index;
    applyChunkWindow();
}

void KlustersDoc::chunkTimeWindow(int index, long& t0, long& t1) const{
    t0 = static_cast<long>(index) * chunkLenSamples - chunkOverlapSamples;
    t1 = static_cast<long>(index + 1) * chunkLenSamples + chunkOverlapSamples;
    if(t0 < 0)
        t0 = 0;
}

void KlustersDoc::applyChunkWindow(){
    long t0, t1;
    chunkTimeWindow(currentChunkIndex, t0, t1);
    const QList<int> present = clusteringData->clustersInTimeWindow(t0, t1, chunkMinSpikes);
    setMaskKeeping(present);        // mask everything not in this chunk (rebuilds palette, drops masked from view)
    shownClustersUpdate(present);   // foreground the chunk's clusters
}

void KlustersDoc::shownClustersUpdate(const QList<int>& clustersToShow,const QList<int>& previousSelectedClusterPairs){
    //Get the clusters currently selected
    QList<int> currentShownClusters = clusterPalette.selectedClusters();

    //Add the clusters which were shown and not part of the previous selected cluster pairs
    QList<int> mergedClusters = clustersToShow;
    for (int c : currentShownClusters)
        if (!previousSelectedClusterPairs.contains(c)) mergedClusters.append(c);

    //Update the palette of cluster
    clusterPalette.selectItems(mergedClusters);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(mergedClusters);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::showAllClustersExcept(const QList<int>& clustersToHide){

    QList<dataType> clusterList = clusteringData->clusterIds();
    QList<int> clustersToShow;

    QList<dataType>::iterator clustersToAdd;
    for(clustersToAdd = clusterList.begin(); clustersToAdd != clusterList.end(); ++clustersToAdd ){
        if(!clustersToHide.contains(static_cast<int>(*clustersToAdd))) clustersToShow.append(static_cast<int>(*clustersToAdd));
    }

    //Update the palette of cluster
    clusterPalette.selectItems(clustersToShow);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(clustersToShow);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::addClustersToActiveView(const QList<int>& clustersToShow){
    //Get the clusters currently selected
    QList<int> currentShownClusters = clusterPalette.selectedClusters();

    QList<int> mergedClusters = clustersToShow;
    for (int v : currentShownClusters)
        mergedClusters.append(v);

    //Update the palette of cluster
    clusterPalette.selectItems(mergedClusters);

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //The new selection of clusters only means for the active view
    activeView->shownClustersUpdate(mergedClusters);

    //update the TraceView if any
    activeView->updateTraceView(electrodeGroupID,clusterColorList,true);
}

void KlustersDoc::createProviders(){
    QFileInfo docInfo(docUrl);
    const QString datUrl = docInfo.absolutePath() + "/" + docInfo.baseName() +".dat";

    int resolution = clusteringData->getResolution();
    int voltageRange = clusteringData->getVoltageRange();
    double samplingRate = clusteringData->getSamplingRate();
    int channelNb = clusteringData->getTotalNbChannels();

    //Create the tracesProviders
    tracesProvider = new TracesProvider(datUrl,channelNb,
                                        resolution,samplingRate,clusteringData->getOffset());


    acquisitionGain = static_cast<int>(0.5 +
                                       static_cast<float>(pow(static_cast<double>(2),static_cast<double>(resolution))
                                                          / static_cast<float>(voltageRange * 1000))
                                       * clusteringData->getAmplification());

    //the screen grain is fixed to 0.2
    float screenGain = 0.2;
    gain = static_cast<int>(0.5 + screenGain * static_cast<float>(acquisitionGain));

    //Create the colorlist
    //Constructs the channelColorList, assign to all the channels the same blue color.
    //Put all the channels of the spike group corresponding to the open file in the same group(the electrodeGroupID)
    channelColorList = new ChannelColors();
    QColor color;
    QList<int> group;
    color.setHsv(210,200,255);

    QList<int>& currentChannels =  clusteringData->getCurrentChannels();
    QList<int>::const_iterator iterator;
    for(iterator = currentChannels.begin(); iterator != currentChannels.end(); ++iterator){
        channelColorList->append(*iterator,color);
        displayChannelsGroups.insert(*iterator,electrodeGroupID.toInt());
        channelsSpikeGroups.insert(*iterator,electrodeGroupID.toInt());
        group.append(*iterator);
    }

    displayGroupsChannels.insert(electrodeGroupID.toInt(),group);
    spikeGroupsChannels.insert(electrodeGroupID.toInt(),group);

    ////Put all the other channels in the trash group (group 0).
    QList<int> trashGroup;
    for(int i = 0; i < channelNb; ++i){
        if(!currentChannels.contains(i)){
            channelColorList->append(i,color);
            displayChannelsGroups.insert(i,0);
            channelsSpikeGroups.insert(i,0);
            trashGroup.append(i);
        }
    }

    displayGroupsChannels.insert(0,trashGroup);
    spikeGroupsChannels.insert(0,trashGroup);

    clustersProvider = new ClustersProvider(docUrl,samplingRate,samplingRate,*clusteringData,tracesProvider->getTotalNbSamples());

    //The current cluster file contains the data for the unique display group.
    QList<int> list;
    list.append(electrodeGroupID.toInt());
    displayGroupsClusterFile.insert(electrodeGroupID.toInt(),list);
}


void KlustersDoc::showUserClusterInformation(){
    clusterPalette.showUserClusterInformation(electrodeGroupID.toInt());
}

// ============================================================================
// Curation logger helpers
// ============================================================================

void KlustersDoc::logBefore(CurationLogger::ActionType action,
                             const QList<int>& clusterIds)
{
    if (!curationLogger || !curationLogger->isOpen() || clusterIds.isEmpty())
        return;

    QList<ClusterSnapshot> snaps = snapshotClusters(clusterIds);

    // Stamp each snapshot with this cluster's prior action count, then increment.
    for (ClusterSnapshot& s : snaps) {
        s.actionHistoryDepth = clusterActionCount.value(s.clusterId, 0);
        clusterActionCount[s.clusterId]++;
    }

    lastLoggedActionIdx = curationLogger->beginAction(action, snaps);
}

void KlustersDoc::logAfter(const QList<int>& clusterIds)
{
    if (!curationLogger || !curationLogger->isOpen() || clusterIds.isEmpty())
        return;

    QList<ClusterSnapshot> snaps = snapshotClusters(clusterIds);
    // Preserve action_history_depth for result clusters (they were just created
    // or modified, so their count is the depth inherited from the action).
    for (ClusterSnapshot& s : snaps)
        s.actionHistoryDepth = clusterActionCount.value(s.clusterId, 0);

    curationLogger->commitAction(snaps);
}

void KlustersDoc::beginRealignBatchLog(const QList<int>& /*clusterIds*/)
{
    // Enable the batch-scoped centroid cache.  Per-cluster realign logging stays
    // ON; each cluster's logBefore/logAfter reuses one computeAllCentroids()
    // pass (populated lazily on the first snapshot, inside the realign worker)
    // instead of recomputing the full-dataset centroids twice per cluster.
    centroidCache.clear();
    centroidCacheValid   = false;
    centroidCacheEnabled = true;
}

void KlustersDoc::endRealignBatchLog()
{
    // Tear down the batch cache so subsequent snapshots are exact again.
    centroidCacheEnabled = false;
    centroidCacheValid   = false;
    centroidCache.clear();
}
