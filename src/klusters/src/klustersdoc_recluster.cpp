// klustersdoc_recluster.cpp — KlustersDoc reclustering subsystem.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// feature-file creation and reclustering-integration methods of KlustersDoc (the
// declarations remain in klustersdoc.h; this is a mechanical relocation, no logic
// change).  Carries the same include preamble as klustersdoc.cpp so every symbol
// the moved code uses resolves identically.
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

// ─────────────────────────────────────────────────────────────────────────
// Reclustering: feature-file creation + reclustered-cluster integration.
// (moved verbatim from klustersdoc.cpp)
// ─────────────────────────────────────────────────────────────────────────

int KlustersDoc::createFeatureFile(QList<int>& clustersToRecluster,const QString& reclusteringFetFileName){
    QFile fetFile(reclusteringFetFileName);
    if(!fetFile.open(QIODevice::WriteOnly))
        return OPEN_ERROR;

    //Create the file
    // Route through the ACTIVE clustering and pin it for the async integrate.
    // data() == clusteringData when no child is active (parent path unchanged),
    // == childData in hierarchical mode (recluster the selected atom).
    reclusterTarget = &data();
    reclusterTarget->createFeatureFile(clustersToRecluster,fetFile);
    fetFile.close();
    if(fetFile.error() == QFile::NoError)
        return OK;
    else
        return CREATION_ERROR;
}

// patch78 — wrapper for the mean-subtracted sub-dimensional path.
// Returns one of the standard enum values (OK / OPEN_ERROR /
// CREATION_ERROR) to avoid colliding the success path's K+1 dim count
// with the enum codes (CREATION_ERROR=8, SAVE_ERROR=5, UPLOAD_ERROR=6,
// INCORRECT_CONTENT=7, etc.).  The actual number of dimensions written
// is returned via the *nDimWritten out-parameter so the caller can
// build the right %features bit-string.
int KlustersDoc::createMeanSubtractedSubdimFeatureFile(
        int clusterId, int K,
        const QString& reclusteringFetFileName,
        int* nDimWritten,
        QVector<double>* eigvalsOut)
{
    if (nDimWritten) *nDimWritten = 0;
    QFile fetFile(reclusteringFetFileName);
    if (!fetFile.open(QIODevice::WriteOnly)) return OPEN_ERROR;
    reclusterTarget = &data();
    const int nDim = reclusterTarget->createMeanSubtractedSubdimFeatureFile(
        clusterId, K, fetFile, eigvalsOut);
    // createMeanSubtractedSubdimFeatureFile closes fetFile internally
    // (it re-opens via fopen for binary I/O).
    if (nDim <= 0) return CREATION_ERROR;
    if (nDimWritten) *nDimWritten = nDim;
    return OK;
}

// Wrapper for the raw-waveform median-residual path.  Same enum/out-parameter
// contract as createMeanSubtractedSubdimFeatureFile.  Pools the spikes of all
// @p clusterIds before taking the median waveform.
int KlustersDoc::createMedianWaveformResidualFeatureFile(
        const QList<int>& clusterIds, int K,
        const QString& reclusteringFetFileName,
        int* nDimWritten,
        QVector<double>* eigvalsOut)
{
    if (nDimWritten) *nDimWritten = 0;
    QFile fetFile(reclusteringFetFileName);
    if (!fetFile.open(QIODevice::WriteOnly)) return OPEN_ERROR;
    reclusterTarget = &data();
    const int nDim = reclusterTarget->createMedianWaveformResidualFeatureFile(
        clusterIds, K, fetFile, eigvalsOut);
    if (nDim <= 0) return CREATION_ERROR;
    if (nDimWritten) *nDimWritten = nDim;
    return OK;
}

// patch81 — Remove the staged YAML (and .yml fallback) that
// KlustersApp::slotRecluster copied next to the temp .fet so that
// KlustaKwikYaml.cpp's tryPath("<fileBase>", ".yaml") would find it.
//
// The temp YAML lives next to reclusteringFetFileName with the same
// basename but a different extension.  Strip ".fet.<elecID>" off the
// end and try .yaml + .yml.  Failures are silent: the file may not
// exist (orig YAML was missing, or this is a second-attempt cleanup
// where a prior call already removed it), and either case is fine.
static void patch81_cleanupTempYaml(const QString& reclusteringFetFileName)
{
    const int dotFet = reclusteringFetFileName.lastIndexOf(
        QLatin1String(".fet."));
    if (dotFet < 0) return;
    const QString base = reclusteringFetFileName.left(dotFet);
    const QString yamlPath = base + QLatin1String(".yaml");
    const QString ymlPath  = base + QLatin1String(".yml");
    if (QFile::exists(yamlPath)) QFile::remove(yamlPath);
    if (QFile::exists(ymlPath))  QFile::remove(ymlPath);
}

int KlustersDoc::integrateReclusteredClusters(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList,QString reclusteringFetFileName){

    // Capture cluster state before KlustaKwik's output is integrated
    logBefore(CurationLogger::ActionType::RECLUSTER, clustersToRecluster);

    QString cluFileName(reclusteringFetFileName);
    NS3_DIAG()<<"reclusteringFetFileName "<<reclusteringFetFileName;
    cluFileName.replace(".fet.",".clu.");

    QString cluFileUrl(cluFileName);
    QString cluFilePath = cluFileUrl;
    if(!QFile::exists(cluFileUrl)) {
        QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        return DOWNLOAD_ERROR;
    }

    NS3_DIAG()<<" cluFilePath"<<cluFilePath;
    QFile cluFile(cluFilePath);

    if(!cluFile.open(QIODevice::ReadOnly)){
        if(!QFile::remove(reclusteringFetFileName))
            QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
        if(!QFile::remove(cluFileName))
            QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        patch81_cleanupTempYaml(reclusteringFetFileName);
        return OPEN_ERROR;
    }

    //Actually integrate the new clusters.
    // Use the Data pinned when the .fet was built (childData in hierarchical
    // mode), not the current active one -- selection may have changed during the
    // async KlustaKwik run.  integrateReclusteredClusters offsets the new ids by
    // this Data's highestClusterId(), so child atoms land above the highest
    // child id and cannot collide with parent (or existing child) ids.
    Data* rt = reclusterTarget ? reclusterTarget : clusteringData;
    if(!rt->integrateReclusteredClusters(clustersToRecluster,reclusteredClusterList,cluFile)){
        cluFile.close();
        if(!QFile::remove(reclusteringFetFileName))
            QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
        if(!QFile::remove(cluFileName))
            QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
        patch81_cleanupTempYaml(reclusteringFetFileName);
        return INCORRECT_CONTENT;
    }
    cluFile.close();

    //Suppress the fet and clu files.
    if(!QFile::remove(reclusteringFetFileName))
        QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary feature file used by the reclustering program.") );
    if(!QFile::remove(cluFileName))
        QMessageBox::critical(nullptr,tr("Warning !"),tr("Could not delete the temporary cluster file used by the reclustering program.") );
    patch81_cleanupTempYaml(reclusteringFetFileName);

    // Log the newly created clusters — reclusteredClusterList is populated by
    // integrateReclusteredClusters() above and contains the KlustaKwik outputs.
    logAfter(reclusteredClusterList);

    return OK;
}

void KlustersDoc::reclusteringUpdate(QList<int>& clustersToRecluster,QList<int>& reclusteredClusterList){
    // Consume the pin set when the .fet was built.  If the recluster targeted the
    // child (atom) layer, the new sub-atoms were already integrated into childData
    // by integrateReclusteredClusters (ids offset above the highest child id ->
    // no parent/child collision; childData->prepareUndo captured the edit on its
    // own stack).  Refresh the child layer with the same known-good sequence the
    // other hierarchy edits use and return: rebuildHierarchyFromData re-derives
    // each new atom's parent (their spikes still belong to the original fiber),
    // and hierarchyChanged repopulates the child palette.  The parent-oriented
    // body below (undo, parent palette/colours/views) must NOT run for a child
    // recluster.
    const bool reclusterWasChild = (reclusterTarget && reclusterTarget == childData);
    reclusterTarget = nullptr;
    if (reclusterWasChild){
        // integrateReclusteredClusters self-snapshotted childData once -> one
        // ChildEdit so Ctrl+Shift+Z reverts the child recluster: the new atoms are
        // removed and the reclustered source atoms restored from the snapshot.
        ChildEdit e; e.added = reclusteredClusterList; e.deleted = clustersToRecluster;
        recordChildEdit(e);

        // The recluster pooled the selected atoms' spikes and re-split them, so a new atom can
        // span several fibers.  Rather than invent a parent for it (which created spurious new
        // fibers), nest each new atom under the fiber its spikes belong to: collapseToSelfChildren
        // keeps an atom that lands wholly inside one fiber and collapses any straddling portion
        // into that fiber's self child.  No new fiber, and every fiber keeps a covering child.
        collapseToSelfChildren();
        emit hierarchyChildrenCreated(reclusteredClusterList);   // select the new atoms after the rebuild
        modified = true;
        return;
    }

    //Prepare the undo
    prepareReclusteringUndo(reclusteredClusterList,clustersToRecluster);
    // Parent-scope recluster: the freshly created fibers need realign.  (The child branch
    // returned above, nesting its new atoms under the existing fibers via collapseToSelfChildren.)
    for (int nf : reclusteredClusterList) noteModifiedFiber(nf);
    setPendingFiberSelection(reclusteredClusterList);   // land on the reclustered fibers

    //Check if the active view is a ProcessWidget
    bool isProcessWidget = dynamic_cast<KlustersApp*>(parent)->doesActiveDisplayContainProcessWidget();

    if(!isProcessWidget){
        //Get the active view.
        KlustersView* activeView = app()->activeView();

        QList<int> clustersToShow;
        QList<int>::const_iterator iterator;
        QList<int> const clusters = activeView->clusters();
        for(iterator = clusters.begin(); iterator != clusters.end(); ++iterator)
            clustersToShow.append(*iterator);

        //Add the new clusters in clusterColors and clustersToShow.
        QColor color;
        QList<int>::iterator clustersToCreate;
        for(clustersToCreate = reclusteredClusterList.begin(); clustersToCreate != reclusteredClusterList.end(); ++clustersToCreate ){
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,200,255);
            clusterColorList->append(*clustersToCreate,color);
            clustersToShow.append(*clustersToCreate);
        }

        //Remove all the reclustered clusters from clusterColors and clustersToShow.
        QList<int>::iterator clustersToRemove;
        for (clustersToRemove = clustersToRecluster.begin(); clustersToRemove != clustersToRecluster.end(); ++clustersToRemove ){
            clusterColorList->remove(*clustersToRemove);
            clustersToShow.removeAll(*clustersToRemove);
        }

        //Notify all the views of the modification
        for (KlustersView* view : *viewList) {
            const bool isActive = (view == activeView);
                view->addNewClustersToView(clustersToRecluster,reclusteredClusterList, isActive);
                view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
        }

        //Notify the errorMatrixView of the modification
        emit newClustersAdded(clustersToRecluster);

        activeView->showAllWidgets();

        //Update the palette of cluster
        refreshActivePalette(clustersToShow);
    }
    else{//processWidget
        //Add the new clusters in clusterColors.
        QColor color;
        QList<int>::iterator clustersToCreate;
        for(clustersToCreate = reclusteredClusterList.begin(); clustersToCreate != reclusteredClusterList.end(); ++clustersToCreate ){
            color.setHsv(static_cast<int>(fmod(static_cast<float>(*clustersToCreate)*7,36))*10,200,255);
            clusterColorList->append(*clustersToCreate,color);
        }

        //Remove all the reclustered clusters from clusterColors and clustersToShow.
        QList<int>::iterator clustersToRemove;
        for (clustersToRemove = clustersToRecluster.begin(); clustersToRemove != clustersToRecluster.end(); ++clustersToRemove ){
            clusterColorList->remove(*clustersToRemove);
        }

        //Notify all the views of the modification
        for(int i =0; i<viewList->count();++i) {
	    KlustersView* view = viewList->at(i);
            if(!qobject_cast<ProcessWidget*>(view)){
                view->addNewClustersToView(clustersToRecluster,reclusteredClusterList,false);
                //update the TraceView if any
                view->updateTraceView(electrodeGroupID,clusterColorList,false);
            }
	}

        //Notify the errorMatrixView of the modification
        emit newClustersAdded(clustersToRecluster);

        //Update the palette of cluster
        QList<int> emptyList;
        refreshActivePalette(emptyList);
    }

    // Hierarchical mode: a PARENT recluster re-draws the parent's spikes across the new
    // fibers without touching the atom layer, so the source fiber's atom(s) now straddle them
    // -- and a recluster of the artifact bin leaves the new fibers carrying only the reserve
    // atom.  refiberize re-cuts the straddlers AND mints a covering atom per new fiber (incl.
    // the artifact-derived ones), so each new fiber gets its own microfiber child instead of
    // several fibers sharing one atom (or none).  It also rebuilds child->parent and emits
    // hierarchyChanged to repopulate the child palette.  childData-guarded; flat sessions are
    // untouched.
    if (childData)
        refiberize();
}
