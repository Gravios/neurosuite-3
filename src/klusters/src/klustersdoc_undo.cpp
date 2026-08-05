// klustersdoc_undo.cpp — KlustersDoc undo/redo subsystem.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// undo-stack preparation (prepareUndo overloads, prepareClusterColorUndo,
// prepareReclusteringUndo, nbUndoChangedCleaning) and the undo()/redo() dispatch
// of KlustersDoc.  Declarations remain in klustersdoc.h; this is a mechanical
// relocation with no logic change.  Carries the same include preamble as
// klustersdoc.cpp (including `extern int nbUndo;`) so every symbol resolves
// identically.
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

extern int nbUndo;

// ─────────────────────────────────────────────────────────────────────────
// Undo/redo: stack preparation + undo()/redo() dispatch.
// (moved verbatim from klustersdoc.cpp)
// ─────────────────────────────────────────────────────────────────────────

void KlustersDoc::prepareClusterColorUndo(){
    //Update the boolean modified here as every action implies a call to the function
    modified = true;

    //Create a new clusterColors which will hold the new configuration
    ItemColors* clusterColorListTemp = new ItemColors(*clusterColorList);

    //Store the current clusterColors in the undo list and make the temporary become the current one.
    clusterColorListUndoList.prepend(clusterColorList);
    clusterColorList = clusterColorListTemp;

    //if the number of undo has been reach remove the last element in the undo list (first inserted)
    int currentClusterColorsNbUndo = clusterColorListUndoList.count();
    if(currentClusterColorsNbUndo > nbUndo)
        delete clusterColorListUndoList.takeAt(currentClusterColorsNbUndo - 1);

    //Clear the redoList
    qDeleteAll(clusterColorListRedoList);
    clusterColorListRedoList.clear();

    //Signal to klusters the new number of undo and redo
    emit updateUndoNb(clusterColorListUndoList.count());
    emit updateRedoNb(0);
}

void KlustersDoc::prepareUndo(QList<int>* addedClustersTemp,QList<int>* modifiedClustersTemp,QList<int>* deletedClustersTemp,bool isModifiedByDeletion){
    //Prepare the undo for the cluster palette
    prepareClusterColorUndo();

    //Store the current addedClusters in the undo list and make the temporary become the current one.
    addedClustersUndoList.prepend(addedClusters);
    addedClusters = addedClustersTemp;

    //Store the current modifiedClusters in the undo list and make the temporary become the current one.
    modifiedClustersUndoList.prepend(modifiedClusters);
    modifiedClusters = modifiedClustersTemp;

    //Store the current deletedClusters in the undo list and make the temporary become the current one.
    deletedClustersUndoList.prepend(deletedClusters);
    deletedClusters = deletedClustersTemp;

    //The renumbering actions which were redo are now lost
    QList<int>::iterator iterator;
    for(iterator = renumberingRedoList.begin(); iterator != renumberingRedoList.end(); ++iterator){
        clusterIdsOldNewMap.remove(*iterator);
        clusterIdsNewOldMap.remove(*iterator);
    }
    renumberingRedoList.clear();

    //if the number of undo has been reach remove the last element in the undo lists (first inserted)
    int currentNbUndo = addedClustersUndoList.count();
    if(currentNbUndo > nbUndo){
        delete addedClustersUndoList.takeAt(currentNbUndo - 1);
        delete modifiedClustersUndoList.takeAt(currentNbUndo - 1);
        delete deletedClustersUndoList.takeAt(currentNbUndo - 1);
        // removeAll(value) removes list entries whose VALUE equals currentNbUndo.
        // (removeAt(index) would be an out-of-bounds crash when the list is short.)
        modifiedClustersByDeleteUndo.removeAll(currentNbUndo);
        if(isModifiedByDeletion) modifiedClustersByDeleteUndo.append(currentNbUndo - 1);

        //The clusterIdsOldNew and clusterIdsNewOld maps are associated with
        //undo numbers. As the meaning of the numbers change (first undo will not be accessible anymore,
        //and the following ones are shift by one down (2->1, 3->2 etc..)), the maps have to be updated accordingly.
        if(clusterIdsOldNewMap.count() == 1 && clusterIdsOldNewMap.contains(1)){
            clusterIdsOldNewMap.remove(1);
            clusterIdsNewOldMap.remove(1);
        }
        else{
            for(int i = 2; i <= nbUndo; ++i){
                if(!clusterIdsOldNewMap.contains(i)) continue;
                QMap<int,int> clusterIdsOldNew = clusterIdsOldNewMap[i];
                clusterIdsOldNewMap.insert(i-1,clusterIdsOldNew);
                QMap<int,int> clusterIdsNewOld = clusterIdsNewOldMap[i];
                clusterIdsNewOldMap.insert(i-1,clusterIdsNewOld);
            }
            //remove the map entries with the bigger key (has not be taken into account by the previous loop)
            if(!clusterIdsOldNewMap.isEmpty()) {
                QList<int> undoNbs = clusterIdsOldNewMap.keys();
                std::sort(undoNbs.begin(), undoNbs.end());
                int biggerUndo = undoNbs.last();
                clusterIdsOldNewMap.remove(biggerUndo);
                clusterIdsNewOldMap.remove(biggerUndo);
            }
        }
    }
    else if(isModifiedByDeletion) modifiedClustersByDeleteUndo.append(currentNbUndo);

    //Clear the redoLists
    qDeleteAll(addedClustersRedoList);
    addedClustersRedoList.clear();
    qDeleteAll(modifiedClustersRedoList);
    modifiedClustersRedoList.clear();
    qDeleteAll(deletedClustersRedoList);
    deletedClustersRedoList.clear();
}



void KlustersDoc::nbUndoChangedCleaning(int newNbUndo){
    // Keep the curation logger's in-memory ring buffer the same size as
    // the data-level undo capacity so every still-undoable action has a
    // tentative log entry.  Shrinking the buffer flushes the oldest
    // entries to disk with their current status.
    if (curationLogger && curationLogger->isOpen()) {
        curationLogger->setMaxBufferEntries(newNbUndo);
    }

    //if the new number of possible undo is smaller than the current one,
    // clean the undo/redo related variables.
    if(newNbUndo < nbUndo){
        //Make data clean its internal variables
        clusteringData->nbUndoChangedCleaning(newNbUndo);

        //Process the renumbering variables. All the undo indices in renumberingRedoList which
        //are bigger than newNbUndo will not be accesible any more, delete them.
        QList<int>::iterator iterator;
        QList<int> suppressIndices;
        for(iterator = renumberingRedoList.begin(); iterator != renumberingRedoList.end(); ++iterator){
            if(*iterator > newNbUndo){
                clusterIdsOldNewMap.remove(*iterator);
                clusterIdsNewOldMap.remove(*iterator);
                suppressIndices.append(*iterator);
            }
        }
        for(iterator = suppressIndices.begin(); iterator != suppressIndices.end(); ++iterator)
            renumberingRedoList.removeAll(*iterator);

        int currentNbUndo = clusterColorListUndoList.count();

        //if the current number of undo is bigger than the new number of undo,
        // remove the last elements in the undo lists (first ones inserted).
        if(currentNbUndo > newNbUndo){
            while(currentNbUndo > newNbUndo){
                delete addedClustersUndoList.takeAt(currentNbUndo - 1);
                delete modifiedClustersUndoList.takeAt(currentNbUndo - 1);
                delete deletedClustersUndoList.takeAt(currentNbUndo - 1);
                delete clusterColorListUndoList.takeAt(currentNbUndo - 1);
                modifiedClustersByDeleteUndo.removeAll(currentNbUndo);

                //The clusterIdsOldNew and clusterIdsNewOld maps are associated with
                //undo numbers. As the meaning of the numbers change (first undo will not be accessible anymore,
                //and the following ones are shift by one down (2->1, 3->2 etc..)), the maps have to be updated accordingly.
                if(clusterIdsOldNewMap.count() == 1 && clusterIdsOldNewMap.contains(1)){
                    clusterIdsOldNewMap.remove(1);
                    clusterIdsNewOldMap.remove(1);
                }
                else{
                    for(int i = 2; i <= currentNbUndo; ++i){
                        if(!clusterIdsOldNewMap.contains(i)) continue;
                        QMap<int,int> clusterIdsOldNew = clusterIdsOldNewMap[i];
                        clusterIdsOldNewMap.insert(i-1,clusterIdsOldNew);
                        QMap<int,int> clusterIdsNewOld = clusterIdsNewOldMap[i];
                        clusterIdsNewOldMap.insert(i-1,clusterIdsNewOld);
                    }
                    //remove the map entries with the bigger key (has not be taken into account by the previous loop)
                    QList<int> undoNbs = clusterIdsOldNewMap.keys();
                    std::sort(undoNbs.begin(), undoNbs.end());
                    int biggerUndo = undoNbs.last();
                    clusterIdsOldNewMap.remove(biggerUndo);
                    clusterIdsNewOldMap.remove(biggerUndo);
                }

                currentNbUndo = clusterColorListUndoList.count();
            }
            //clear the redo lists
            qDeleteAll(addedClustersRedoList);
            addedClustersRedoList.clear();
            qDeleteAll(modifiedClustersRedoList);
            modifiedClustersRedoList.clear();
            qDeleteAll(deletedClustersRedoList);
            deletedClustersRedoList.clear();
            qDeleteAll(clusterColorListRedoList);
            clusterColorListRedoList.clear();
        }
        //currentNbUndo < newNbUndo, check the redo list.
        else{
            //number of undo and redo must be <= new number of undo. Remove redo elements if need it.
            int currentNbRedo = clusterColorListRedoList.count();
            if((currentNbRedo + currentNbUndo) > newNbUndo){
                while((currentNbRedo + currentNbUndo) > newNbUndo){
                    delete addedClustersRedoList.takeAt(currentNbRedo - 1);
                    delete modifiedClustersRedoList.takeAt(currentNbRedo - 1);
                    delete deletedClustersRedoList.takeAt(currentNbRedo - 1);
                    delete clusterColorListRedoList.takeAt(currentNbRedo - 1);
                    modifiedClustersByDeleteRedo.removeAll(currentNbRedo);

                    currentNbRedo = clusterColorListRedoList.count();
                }
            }
        }

        //Make the views clean its internal variables

        for(int i =0; i<viewList->count();++i) {
            KlustersView *view = viewList->at(i);
            view->nbUndoChangedCleaning(newNbUndo);
        }

        //Signal to klusters the new number of undo and redo
        emit updateUndoNb(clusterColorListUndoList.count());
        emit updateRedoNb(clusterColorListRedoList.count());
    }
}


void KlustersDoc::prepareUndo(){
    //Create a new empty list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();

    //Create a new empty list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();

    //Create a new empty list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}

void KlustersDoc::prepareUndo(int newCluster,QList<int>& deletedClusters){
    //Create a new list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    addedClustersTemp->append(newCluster);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}

void KlustersDoc::prepareUndo(QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion){
    //Create a new empty list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = modifiedClusters.begin(); iterator != modifiedClusters.end(); ++iterator)
        modifiedClustersTemp->append(*iterator);

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp,isModifiedByDeletion);
}

void KlustersDoc::prepareUndo(int newCluster, QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion){
    //Create a new empty list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    addedClustersTemp->append(newCluster);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = modifiedClusters.begin(); iterator != modifiedClusters.end(); ++iterator)
        modifiedClustersTemp->append(*iterator);

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp,isModifiedByDeletion);
}

void KlustersDoc::prepareUndo(QList<int>& newClusters, QList<int>& modifiedClusters,QList<int>& deletedClusters){
    //Create a new list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    QList<int>::iterator iterator;
    for (iterator = newClusters.begin(); iterator != newClusters.end(); ++iterator)
        addedClustersTemp->append(*iterator);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();
    for (iterator = modifiedClusters.begin(); iterator != modifiedClusters.end(); ++iterator)
        modifiedClustersTemp->append(*iterator);

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (iterator = deletedClusters.begin(); iterator != deletedClusters.end(); ++iterator)
        deletedClustersTemp->append(*iterator);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}


void KlustersDoc::prepareUndo(QMap<int,int> clusterIdsOldNew,QMap<int,int> clusterIdsNewOld){
    prepareUndo();

    //Update the renumbering lists
    int currentNbUndo = clusterColorListUndoList.count();
    NS3_DIAG()<<"currentNbUndo in KlustersDoc::prepareUndo: "<<currentNbUndo;
    clusterIdsOldNewMap.insert(currentNbUndo,clusterIdsOldNew);
    clusterIdsNewOldMap.insert(currentNbUndo,clusterIdsNewOld);
}


void KlustersDoc::prepareReclusteringUndo(QList<int>& newClusters,QList<int>& deletedClusters){
    //Create a new list of created clusters
    QList<int>* addedClustersTemp = new QList<int>();
    for (int v : newClusters)
        addedClustersTemp->append(v);

    //Create a new list of modified clusters
    QList<int>* modifiedClustersTemp = new QList<int>();

    //Create a new list of deleted clusters
    QList<int>* deletedClustersTemp = new QList<int>();
    for (int v : deletedClusters)
        deletedClustersTemp->append(v);

    prepareUndo(addedClustersTemp, modifiedClustersTemp,deletedClustersTemp);
}

void KlustersDoc::undo(){

    NS3_DIAG()<<"in KlustersDoc::undo 1";

    //Update the boolean modified here as every undo action implies a call to the function.
    //The user can save and make an undo just behind, in that case the document is modified.
    modified = true;

    //Get the active view.
    KlustersView* activeView = app()->activeView();

    if(!activeView)
        return;

    //If clusterColorListUndoList is not empty, make the current clusterColorList become the first element
    //of the clusterColorListRedoList and the first element of the clusterColorListUndoList become the current clusterColorList
    //do the same for the addedClusters and modifiedClusters Lists.
    if(clusterColorListUndoList.count()>0){
        // Quiesce every view's worker threads BEFORE the data layer swaps
        // spikesByCluster/clusterInfoMap.  A WaveformThread (or CorrelationThread)
        // in flight here would otherwise read across the swap, or finish and post
        // a stale per-cluster result that the view applies afterwards — leaving
        // the async waveform/correlation views showing pre-undo data while the
        // synchronous feature scatter and cluster list already show the new
        // state (the reported desync).  Stopping clears each view's
        // threadsToBeKill, so any already-posted stale result is dropped by the
        // event guards; the post-swap view->undo()/refresh below recomputes from
        // the new data, so all views end up consistent.
        for (int i = 0; i < viewList->count(); ++i)
            viewList->at(i)->stopAllViewThreads();

        // Must be called after the guard: if the undo list is empty there is nothing
        // to revert at the data layer either, and calling it unconditionally can leave
        // addedClusters/modifiedClusters in an inconsistent state (null after takeAt on
        // an empty list) which later causes a crash through the waveform cleanup path.
        clusteringData->undo(*addedClusters,*modifiedClusters);

        clusterColorListRedoList.prepend(clusterColorList);
        ItemColors* clusterColorListTemp = clusterColorListUndoList.takeAt(0);
        clusterColorList =  clusterColorListTemp;

        int nbUndo = clusterColorListUndoList.count();

        NS3_DIAG() << "nbUndo in KlustersDoc::undo: "<<nbUndo;

        //If this undo does concern renumbering
        if(clusterIdsNewOldMap.contains(nbUndo + 1)){
            NS3_DIAG() << "renumber in KlustersDoc::undo, nbUndo + 1 : "<<nbUndo + 1;
            //Add the current undo indice to the renumberingRedoList
            renumberingRedoList.append(nbUndo + 1);

            // Reverse any S-pin renumbers made by the original action
            // so a pin the user set on the pre-renumber cluster id is
            // restored when undo brings that id back.
            clusterPalette.renumberPinnedIds(clusterIdsNewOldMap[nbUndo + 1]);

            //Notify all the views of the undo

            for (KlustersView* view : *viewList) {
                const bool isActive = (view == activeView);
                    view->undoRenumbering(clusterIdsNewOldMap[nbUndo + 1], isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
            }

            //Notify the errorMatrixView of the modification
            emit undoRenumbering(clusterIdsNewOldMap[nbUndo + 1]);
        }
        else{
            if(modifiedClustersByDeleteUndo.contains(nbUndo + 1) != 0){
                modifiedClustersByDeleteUndo.removeAll(nbUndo + 1);
                int nbRedo = clusterColorListRedoList.count();
                modifiedClustersByDeleteRedo.append(nbRedo);
            }

            //Notify all the views of the undo
            if(addedClusters->size() > 0 && modifiedClusters->size() > 0){
                NS3_DIAG() << "addedClusters->size() > 0 && modifiedClusters->size() > 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undo(*addedClusters,*modifiedClusters, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoAdditionModification(*addedClusters,*modifiedClusters);
            }
            else if(!addedClusters->isEmpty() && modifiedClusters->isEmpty()){
                NS3_DIAG() << "addedClusters->size() > 0 && modifiedClusters->size() == 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undoAddedClusters(*addedClusters, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoAddition(*addedClusters);
            }
            else if(addedClusters->isEmpty() && !modifiedClusters->isEmpty()){
                NS3_DIAG() << "addedClusters->size() == 0 && modifiedClusters->size() > 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undoModifiedClusters(*modifiedClusters, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoModification(*modifiedClusters);
            }
            //////!!!!This last condition should not be reach anymore, to test and remove.!!!!!////
            else if(addedClusters->size() == 0 && modifiedClusters->size() == 0){
                NS3_DIAG() << "addedClusters->size() == 0 && modifiedClusters->size() == 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->undo(isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }
            }
        }
        addedClustersRedoList.prepend(addedClusters);
        QList<int>* addedClustersTemp = addedClustersUndoList.isEmpty()
                                        ? new QList<int>()
                                        : addedClustersUndoList.takeAt(0);
        addedClusters =  addedClustersTemp;

        modifiedClustersRedoList.prepend(modifiedClusters);
        QList<int>* modifiedClustersTemp = modifiedClustersUndoList.isEmpty()
                                           ? new QList<int>()
                                           : modifiedClustersUndoList.takeAt(0);
        modifiedClusters =  modifiedClustersTemp;

        deletedClustersRedoList.prepend(deletedClusters);
        QList<int>* deletedClustersTemp = deletedClustersUndoList.isEmpty()
                                          ? new QList<int>()
                                          : deletedClustersUndoList.takeAt(0);
        deletedClusters =  deletedClustersTemp;

        QList<int> clustersToShow = activeView->clusters();

        //Call redraw on the active view
        activeView->showAllWidgets();

        //Update the clusterPalette
        refreshActivePalette(clustersToShow);

        //Signal to klusters the new number of undo and redo
        emit updateUndoNb(clusterColorListUndoList.count());
        emit updateRedoNb(clusterColorListRedoList.count());
    }

    NS3_DIAG()<<"in KlustersDoc::undo 2";

    // Curation log: flip the topmost good entry's status to "bad" so the
    // record reflects that the user reverted this action.  No disk write
    // happens here — the entry stays in the in-memory ring until it
    // either gets pushed out by overflow or is finalised at close().
    if (curationLogger && curationLogger->isOpen()) {
        curationLogger->notifyUndo();
    }
    // Hierarchical view: this reverts clusteringData only.  The two layers keep
    // INDEPENDENT undo stacks by design (hierarchical-clustering.md), so the atom
    // re-cut that ran after the edit being undone is not reverted with it -- and
    // that re-cut moved rows into atoms chosen for the post-edit fiber layout.
    // Restoring the old fiber labels underneath them puts those atoms back in a
    // fiber they no longer belong to.
    //
    // Concretely: sending part of a fiber to noise makes its clipped rows atom 1,
    // the noise self child; undo returns them to their fiber still carrying atom 1,
    // which also covers the session's actual noise, so atom 1 now spans two fibers.
    // Re-deriving the maps here only REPORTED that -- rebuildHierarchyFromData
    // warns and keeps the first-seen owner -- which made undo a silent source of
    // exactly the offender lists the user was seeing.
    //
    // So re-cut rather than merely re-derive.  collapseToSelfChildren() ends by
    // calling rebuildHierarchyFromData() and emitting hierarchyChanged(), so this
    // is a strict superset of what was here, and it commits nothing when nothing is
    // loose -- no undo level, no cache invalidation -- so an undo that leaves the
    // layers consistent still costs only one scan.
    if (childData) collapseToSelfChildren();
}


void KlustersDoc::redo(){
    //Get the active view.
    KlustersView* activeView = app()->activeView();

    //Update the boolean modified here as every redo action implies a call to the function.
    //The user can save and make an redo just behind, in that case the document is modified.
    modified = true;

    //If clusterColorListRedoList is not empty, make the current clusterColorList become the first element
    //of the clusterColorListUndoList and the first element of the clusterColorListRedoList become the current clusterColorList
    //do the same for the addedClusters and modifiedClusters Lists.
    if(clusterColorListRedoList.count()>0){
        clusterColorListUndoList.prepend(clusterColorList);
        ItemColors* clusterColorListTemp = clusterColorListRedoList.takeAt(0);
        clusterColorList =  clusterColorListTemp;

        addedClustersUndoList.prepend(addedClusters);
        QList<int>* addedClustersTemp = addedClustersRedoList.takeAt(0);
        addedClusters =  addedClustersTemp;

        modifiedClustersUndoList.prepend(modifiedClusters);
        QList<int>* modifiedClustersTemp = modifiedClustersRedoList.takeAt(0);
        modifiedClusters =  modifiedClustersTemp;

        deletedClustersUndoList.prepend(deletedClusters);
        QList<int>* deletedClustersTemp = deletedClustersRedoList.takeAt(0);
        deletedClusters =  deletedClustersTemp;

        // Stop in-flight view worker threads before the data swap (see the
        // matching comment in undo()): prevents a stale waveform/correlation
        // result from landing after the swap and desyncing the async views from
        // the synchronous feature scatter / cluster list.
        for (int i = 0; i < viewList->count(); ++i)
            viewList->at(i)->stopAllViewThreads();

        clusteringData->redo(*addedClusters,*modifiedClusters,*deletedClusters);

        //If this redo does concern renumbering
        int nbUndo = clusterColorListUndoList.count();

        NS3_DIAG() << "in KlustersDoc::redo, nbUndo  : "<<nbUndo;

        if(clusterIdsOldNewMap.contains(nbUndo)){
            NS3_DIAG() << "renumber in KlustersDoc::redo, nbUndo  : "<<nbUndo;
            //remove the current undo indice from the renumberingRedoList
            renumberingRedoList.removeAll(nbUndo);

            // Re-apply the original rename to S-pinned ids so a pin
            // restored by undo gets re-translated when redo replays
            // the renumber.
            clusterPalette.renumberPinnedIds(clusterIdsOldNewMap[nbUndo]);

            //Notify all the views of the undo
            for (KlustersView* view : *viewList) {
                const bool isActive = (view == activeView);
                    view->redoRenumbering(clusterIdsOldNewMap[nbUndo], isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
            }

            //Notify the errorMatrixView of the modification
            emit redoRenumbering(clusterIdsOldNewMap[nbUndo]);
        }
        else{
            int nbRedo = clusterColorListRedoList.count();
            bool isModifiedByDeletion = false;
            if(modifiedClustersByDeleteRedo.contains(nbRedo + 1) != 0){
                isModifiedByDeletion = true;
                modifiedClustersByDeleteRedo.removeAll(nbRedo + 1);
                int nbUndo = clusterColorListUndoList.count();
                modifiedClustersByDeleteUndo.append(nbUndo);
            }

            //Notify all the views of the undo
            if(addedClusters->size() > 0 && modifiedClusters->size() > 0){
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() > 0 && modifiedClusters->size()>0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redo(*addedClusters, *modifiedClusters, isModifiedByDeletion, isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoAdditionModification(*addedClusters,*modifiedClusters,isModifiedByDeletion,*deletedClusters);
            }
            else if(addedClusters->size() > 0 && modifiedClusters->size() == 0){
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() > 0 && modifiedClusters->size()==0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redoAddedClusters(*addedClusters, isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoAddition(*addedClusters,*deletedClusters);
            }
            else if(addedClusters->size() == 0 && modifiedClusters->size() > 0){
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() == 0 && modifiedClusters->size()>0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redoModifiedClusters(*modifiedClusters, isModifiedByDeletion, isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoModification(*modifiedClusters,isModifiedByDeletion,*deletedClusters);
            }
            else if(addedClusters->size() == 0 && modifiedClusters->size() == 0){
                NS3_DIAG() << "in KlustersDoc::redo, nbUndo  addedClusters->size() == 0 && modifiedClusters->size() ==0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redo(isActive, *deletedClusters);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoDeletion(*deletedClusters);
            }
        }

        NS3_DIAG() << "in KlustersDoc::redo, 2  : ";

        QList<int> clustersToShow = activeView->clusters();

        //Call redraw on the active view
        activeView->showAllWidgets();
        //Update the palette that was actually edited -- undo already routes through
        //the helper; redo was left raw only because its pair was not adjacent.
        NS3_DIAG() << "in KlustersDoc::redo, 3 b : ";

        refreshActivePalette(clustersToShow);

        NS3_DIAG() << "in KlustersDoc::redo, 4  : ";

        //Signal to klusters the new number of undo and redo
        emit updateUndoNb(clusterColorListUndoList.count());
        emit updateRedoNb(clusterColorListRedoList.count());

        NS3_DIAG() << "in KlustersDoc::redo, end  : ";
    }

    // Curation log: flip the topmost bad entry back to "good" — the user
    // restored this action so its on-disk record (when eventually
    // flushed) should not be marked as a reverted decision.
    if (curationLogger && curationLogger->isOpen()) {
        curationLogger->notifyRedo();
    }
    // Hierarchical view: same reasoning as undo() -- a redo re-applies the parent
    // edit without re-applying the atom re-cut that accompanied it, so the layers
    // can land inconsistent and re-deriving the maps would only report it.  Measured
    // as harmless in the modelled scenarios, but it is the same asymmetry and the
    // no-op case is free, so repair symmetrically rather than rely on redo happening
    // to be safe.
    if (childData) collapseToSelfChildren();
}

