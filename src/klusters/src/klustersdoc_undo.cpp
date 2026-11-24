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
#include <neurosuite/core/watershed2d.hpp>
#include "clusterview.h"
#include "klustersdoc.h"
#include "clusterPalette.h"
#include "types.h"
#include "parameteryamlmodifier.h"
#include <neurosuite/core/dipsplit.hpp>
#include "parameteryamlreader.h"
#include "clusteruserinformation.h"

//C, C++ include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <math.h>
#include <climits>


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
    //(levels, not steps: Data::undoLevelCap)
    while(clusterColorListUndoList.count() > Data::undoLevelCap())
        delete clusterColorListUndoList.takeLast();

    //Clear the redoList
    qDeleteAll(clusterColorListRedoList);
    clusterColorListRedoList.clear();

    // The one history both layers share: this parent level, with the child labelling
    // to restore when it is undone (as it stands now: before the edit's re-cut).
    {
        UndoEntry e;
        if (childData) e.childLabels = childData->labelByFeatureRow();
        pushUndoEntry(e);                          // signals the new undo / redo counts
    }
}

void KlustersDoc::prepareUndo(ClusterEditUndo action){
    //Prepare the undo for the cluster palette
    prepareClusterColorUndo();

    //Record the action's descriptor.  Everything rides IN the record — the
    //by-deletion flag and the renumbering maps included — so the historical
    //depth arithmetic is gone, and with it the cap-overflow shift dance the
    //renumbering maps needed and the by-deletion index lists silently got
    //wrong (their indices were never shifted when the oldest entry fell off,
    //leaving every older flag pointing one action too high).
    editUndoList.prepend(std::move(action));

    //if the number of undo has been reached remove the last element in the
    //undo list (first inserted); prepareClusterColorUndo() capped the color
    //stack the same way, keeping the two in lockstep.
    while(editUndoList.count() > Data::undoLevelCap())
        editUndoList.removeLast();

    //Clear the redoList: the actions which could be redone are now lost,
    //their renumbering maps and by-deletion flags with them.
    editRedoList.clear();
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

        int currentNbUndo = clusterColorListUndoList.count();

        //if the current number of undo is bigger than the new number of undo,
        // remove the last elements in the undo lists (first ones inserted).
        //The descriptor stack trims alongside the color stack — the trimmed
        //entries take their renumbering maps and by-deletion flags with them,
        //which is all the depth-shift bookkeeping this loop used to do.
        if(currentNbUndo > newNbUndo){
            while(currentNbUndo > newNbUndo){
                if(!editUndoList.isEmpty()) editUndoList.removeLast();
                delete clusterColorListUndoList.takeAt(currentNbUndo - 1);
                currentNbUndo = clusterColorListUndoList.count();
            }
            //clear the redo lists
            editRedoList.clear();
            qDeleteAll(clusterColorListRedoList);
            clusterColorListRedoList.clear();
        }
        //currentNbUndo < newNbUndo, check the redo list.
        else{
            //number of undo and redo must be <= new number of undo. Remove redo elements if need it.
            int currentNbRedo = clusterColorListRedoList.count();
            if((currentNbRedo + currentNbUndo) > newNbUndo){
                while((currentNbRedo + currentNbUndo) > newNbUndo){
                    if(!editRedoList.isEmpty()) editRedoList.removeLast();
                    delete clusterColorListRedoList.takeAt(currentNbRedo - 1);
                    currentNbRedo = clusterColorListRedoList.count();
                }
            }
        }

        //Make the views clean its internal variables

        for(int i =0; i<viewList->count();++i) {
            KlustersView *view = viewList->at(i);
            view->nbUndoChangedCleaning(newNbUndo);
        }

        // The shared history is as deep as the new depth; what it dropped cannot be redone.
        undoHistory.limitEntries(newNbUndo);
        Data::setUndoLevelsNeeded(0);
        refreshUndoRedoEnable();
    }
}


void KlustersDoc::prepareUndo(){
    //An action which reports no cluster changes.
    prepareUndo(ClusterEditUndo());
}

void KlustersDoc::prepareUndo(int newCluster,QList<int>& deletedClusters){
    ClusterEditUndo action;
    action.added.append(newCluster);
    action.deleted = deletedClusters;
    prepareUndo(std::move(action));
}

void KlustersDoc::prepareUndo(QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion){
    ClusterEditUndo action;
    action.modified = modifiedClusters;
    action.deleted = deletedClusters;
    action.byDeletion = isModifiedByDeletion;
    prepareUndo(std::move(action));
}

void KlustersDoc::prepareUndo(int newCluster, QList<int>& modifiedClusters,QList<int>& deletedClusters,bool isModifiedByDeletion){
    ClusterEditUndo action;
    action.added.append(newCluster);
    action.modified = modifiedClusters;
    action.deleted = deletedClusters;
    action.byDeletion = isModifiedByDeletion;
    prepareUndo(std::move(action));
}

void KlustersDoc::prepareUndo(QList<int>& newClusters, QList<int>& modifiedClusters,QList<int>& deletedClusters){
    ClusterEditUndo action;
    action.added = newClusters;
    action.modified = modifiedClusters;
    action.deleted = deletedClusters;
    prepareUndo(std::move(action));
}


void KlustersDoc::prepareUndo(QMap<int,int> clusterIdsOldNew,QMap<int,int> clusterIdsNewOld){
    //A renumbering action: the descriptor carries the relabel maps the
    //undo/redo notifications replay (view->undoRenumbering/redoRenumbering).
    ClusterEditUndo action;
    action.renumbering = true;
    action.renumberOldNew = clusterIdsOldNew;
    action.renumberNewOld = clusterIdsNewOld;
    prepareUndo(std::move(action));
}


void KlustersDoc::prepareReclusteringUndo(QList<int>& newClusters,QList<int>& deletedClusters){
    ClusterEditUndo action;
    action.added = newClusters;
    action.deleted = deletedClusters;
    prepareUndo(std::move(action));
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
    const bool didUndo = clusterColorListUndoList.count() > 0;
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
            viewList->at(i)->supersedeAllViewThreads();

        // Must be called inside the guard: the data layer reverts exactly when the
        // doc layer does — an unpaired data undo would desync the published epoch
        // from the color and notification state handled below.
        clusteringData->undo();

        clusterColorListRedoList.prepend(clusterColorList);
        ItemColors* clusterColorListTemp = clusterColorListUndoList.takeAt(0);
        clusterColorList =  clusterColorListTemp;

        NS3_DIAG() << "nbUndo in KlustersDoc::undo: "<<clusterColorListUndoList.count();

        //Move the reverted action's descriptor onto the redo side and dispatch
        //on a local copy: one move per undo keeps the descriptor stack in
        //lockstep with the color stacks, whose counts drive the menus.  (The
        //defensive default preserves the historical both-empty dispatch below
        //for a desynced stack instead of popping a missing entry.)
        ClusterEditUndo action;
        if(!editUndoList.isEmpty()) action = editUndoList.takeFirst();
        editRedoList.prepend(action);

        //If this undo does concern renumbering
        if(action.renumbering){
            // Reverse any S-pin renumbers made by the original action
            // so a pin the user set on the pre-renumber cluster id is
            // restored when undo brings that id back.
            clusterPalette.renumberPinnedIds(action.renumberNewOld);

            //Notify all the views of the undo

            for (KlustersView* view : *viewList) {
                const bool isActive = (view == activeView);
                    view->undoRenumbering(action.renumberNewOld, isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
            }

            //Notify the errorMatrixView of the modification
            emit undoRenumbering(action.renumberNewOld);
        }
        else{
            //Notify all the views of the undo
            if(action.added.size() > 0 && action.modified.size() > 0){
                NS3_DIAG() << "added.size() > 0 && modified.size() > 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undo(action.added,action.modified, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoAdditionModification(action.added,action.modified);
            }
            else if(!action.added.isEmpty() && action.modified.isEmpty()){
                NS3_DIAG() << "added.size() > 0 && modified.size() == 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undoAddedClusters(action.added, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoAddition(action.added);
            }
            else if(action.added.isEmpty() && !action.modified.isEmpty()){
                NS3_DIAG() << "added.size() == 0 && modified.size() > 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                        view->undoModifiedClusters(action.modified, isActive);
                        view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit undoModification(action.modified);
            }
            //////!!!!This last condition should not be reach anymore, to test and remove.!!!!!////
            else if(action.added.size() == 0 && action.modified.size() == 0){
                NS3_DIAG() << "added.size() == 0 && modified.size() == 0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->undo(isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }
            }
        }

        QList<int> clustersToShow = activeView->clusters();

        //Call redraw on the active view
        activeView->showAllWidgets();

        //Update the clusterPalette
        // FIBER palette, unconditionally: clustersToShow comes from
        // activeView->clusters(), i.e. FIBER ids.  undo()/redo() drive
        // clusteringData -- the atom layer has its own undoChildEdit() timeline --
        // so routing by current scope would hand parent ids to the child palette and
        // select nothing.
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);
        // (the undo / redo counts are signalled by the caller, undoDispatch)
    }

    NS3_DIAG()<<"in KlustersDoc::undo 2";

    // Curation log: flip the topmost good entry's status to "bad" so the
    // record reflects that the user reverted this action.  No disk write
    // happens here — the entry stays in the in-memory ring until it
    // either gets pushed out by overflow or is finalised at close().
    // Only when a level was reverted: Ctrl+Z on an empty stack changes nothing, and
    // flipping an entry for it would mark an action undone that still stands.
    if (didUndo && curationLogger && curationLogger->isOpen()) {
        curationLogger->notifyUndo();
    }
    // Hierarchical view: this reverts clusteringData only.  The two layers keep
    // INDEPENDENT undo stacks by design (hierarchical-clustering.md), so the atom
    // re-cut that ran after the edit being undone is not reverted with it -- and
    // that re-cut moved rows into atoms chosen for the post-edit parent layout.
    // Restoring the old parent labels underneath them puts those atoms back in a
    // parent they no longer belong to.
    //
    // Concretely: sending part of a parent to noise makes its clipped rows atom 1,
    // the noise self child; undo returns them to their parent still carrying atom 1,
    // which also covers the session's actual noise, so atom 1 now spans two parents.
    // Re-deriving the maps here only REPORTED that -- rebuildHierarchyFromData
    // warns and keeps the first-seen owner -- which made undo a silent source of
    // exactly the offender lists the user was seeing.
    //
    // So re-cut rather than merely re-derive.  collapseToSelfChildren() ends by
    // calling rebuildHierarchyFromData() and emitting hierarchyChanged(), so this
    // is a strict superset of what was here, and it commits nothing when nothing is
    // loose -- no undo level, no cache invalidation -- so an undo that leaves the
    // layers consistent still costs only one scan.
    // (The child layer is put back by the caller, stepOneEntry.)
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
    const bool didRedo = clusterColorListRedoList.count() > 0;
    if(clusterColorListRedoList.count()>0){
        clusterColorListUndoList.prepend(clusterColorList);
        ItemColors* clusterColorListTemp = clusterColorListRedoList.takeAt(0);
        clusterColorList =  clusterColorListTemp;

        //Move the re-applied action's descriptor back onto the undo side and
        //dispatch on a local copy (the mirror of undo()).
        ClusterEditUndo action;
        if(!editRedoList.isEmpty()) action = editRedoList.takeFirst();
        editUndoList.prepend(action);

        // Stop in-flight view worker threads before the data swap (see the
        // matching comment in undo()): prevents a stale waveform/correlation
        // result from landing after the swap and desyncing the async views from
        // the synchronous feature scatter / cluster list.
        for (int i = 0; i < viewList->count(); ++i)
            viewList->at(i)->supersedeAllViewThreads();

        clusteringData->redo();

        //If this redo does concern renumbering
        NS3_DIAG() << "in KlustersDoc::redo, nbUndo  : "<<clusterColorListUndoList.count();

        if(action.renumbering){
            // Re-apply the original rename to S-pinned ids so a pin
            // restored by undo gets re-translated when redo replays
            // the renumber.
            clusterPalette.renumberPinnedIds(action.renumberOldNew);

            //Notify all the views of the undo
            for (KlustersView* view : *viewList) {
                const bool isActive = (view == activeView);
                    view->redoRenumbering(action.renumberOldNew, isActive);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
            }

            //Notify the errorMatrixView of the modification
            emit redoRenumbering(action.renumberOldNew);
        }
        else{
            const bool isModifiedByDeletion = action.byDeletion;

            //Notify all the views of the undo
            if(action.added.size() > 0 && action.modified.size() > 0){
                NS3_DIAG() << "in KlustersDoc::redo, added.size() > 0 && modified.size()>0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redo(action.added, action.modified, isModifiedByDeletion, isActive, action.deleted);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoAdditionModification(action.added,action.modified,isModifiedByDeletion,action.deleted);
            }
            else if(action.added.size() > 0 && action.modified.size() == 0){
                NS3_DIAG() << "in KlustersDoc::redo, added.size() > 0 && modified.size()==0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redoAddedClusters(action.added, isActive, action.deleted);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoAddition(action.added,action.deleted);
            }
            else if(action.added.size() == 0 && action.modified.size() > 0){
                NS3_DIAG() << "in KlustersDoc::redo, added.size() == 0 && modified.size()>0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redoModifiedClusters(action.modified, isModifiedByDeletion, isActive, action.deleted);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoModification(action.modified,isModifiedByDeletion,action.deleted);
            }
            else if(action.added.size() == 0 && action.modified.size() == 0){
                NS3_DIAG() << "in KlustersDoc::redo, added.size() == 0 && modified.size() ==0";
                for (KlustersView* view : *viewList) {
                    const bool isActive = (view == activeView);
                    view->redo(isActive, action.deleted);
                    view->updateTraceView(electrodeGroupID, clusterColorList, isActive);
                }

                //Notify the errorMatrixView of the modification
                emit redoDeletion(action.deleted);
            }
        }

        NS3_DIAG() << "in KlustersDoc::redo, 2  : ";

        QList<int> clustersToShow = activeView->clusters();

        //Call redraw on the active view
        activeView->showAllWidgets();
        //Update the palette that was actually edited -- undo already routes through
        //the helper; redo was left raw only because its pair was not adjacent.
        NS3_DIAG() << "in KlustersDoc::redo, 3 b : ";

        // FIBER palette, unconditionally: clustersToShow comes from
        // activeView->clusters(), i.e. FIBER ids.  undo()/redo() drive
        // clusteringData -- the atom layer has its own undoChildEdit() timeline --
        // so routing by current scope would hand parent ids to the child palette and
        // select nothing.
        clusterPalette.updateClusterList();
        clusterPalette.selectItems(clustersToShow);

        NS3_DIAG() << "in KlustersDoc::redo, 4  : ";

        NS3_DIAG() << "in KlustersDoc::redo, end  : ";
    }

    // Curation log: flip the topmost bad entry back to "good" — the user
    // restored this action so its on-disk record (when eventually
    // flushed) should not be marked as a reverted decision.
    if (didRedo && curationLogger && curationLogger->isOpen()) {
        curationLogger->notifyRedo();
    }
    // Hierarchical view: same reasoning as undo() -- a redo re-applies the parent
    // edit without re-applying the atom re-cut that accompanied it, so the layers
    // can land inconsistent and re-deriving the maps would only report it.  Measured
    // as harmless in the modelled scenarios, but it is the same asymmetry and the
    // no-op case is free, so repair symmetrically rather than rely on redo happening
    // to be safe.
    // (The child layer is put back by the caller, stepOneEntry.)
}

// ── undo / redo: one history for both layers ────────────────────────────────
// The entries and their steps are UndoHistory (undohistory.h); here they are pushed and
// applied.  undo() / redo() above are the PARENT workers -- one level of clusteringData,
// the colours, the views, the log -- and are reached through stepOneEntry only.

int KlustersDoc::childUndoCount() const { return undoHistory.childEntriesOnTop(UndoHistory::Undo); }
int KlustersDoc::childRedoCount() const { return undoHistory.childEntriesOnTop(UndoHistory::Redo); }
int KlustersDoc::undoStepCount() const { return undoHistory.steps(UndoHistory::Undo); }
int KlustersDoc::redoStepCount() const { return undoHistory.steps(UndoHistory::Redo); }

void KlustersDoc::refreshUndoRedoEnable(){
    emit updateUndoNb(undoStepCount());
    emit updateRedoNb(redoStepCount());
}

void KlustersDoc::pushUndoEntry(const UndoEntry& e){
    undoHistory.push(e, nbUndo, Data::maxUndoLevels());
    // The level histories (Data, the colours, the edit descriptors, the views) keep the
    // levels of these steps, and one more for the push that comes next.
    Data::setUndoLevelsNeeded(undoHistory.parentLevels() + 1);
    // ... and the curation log keeps their entries, whose status an undo still flips.
    if (curationLogger && curationLogger->isOpen())
        curationLogger->setMaxBufferEntries(undoHistory.entryCount(UndoHistory::Undo) + qMax(1, nbUndo));
    refreshUndoRedoEnable();
}

void KlustersDoc::recordChildEdit(const ChildEdit& eIn){
    UndoEntry u;
    u.child = true;
    u.edit = eIn;
    u.edit.logIdx = childEditLogIdx;               // the log entry this edit was written as, if any
    childEditLogIdx = -1;
    // The state to go back to: what childData held before this edit, which is the level
    // the edit itself just pushed (every caller records straight after its one commit).
    if (childData) u.childLabels = childData->labelsOfLastUndoLevel();
    pushUndoEntry(u);
}

bool KlustersDoc::setClusterAnnotation(bool childLayer, int id, const UnitAnnotation& annotation){
    Data* layer = childLayer ? childData : clusteringData;
    if (!layer) return false;
    QList<QString> fields;
    layer->getUserClusterInformation(id, fields);
    if (fields.isEmpty()) return false;            // no such cluster
    const UnitAnnotation before = UnitAnnotation::fromFields(fields);
    if (before == annotation) return false;
    layer->setUserClusterAnnotation(id, annotation);
    clusterInformationModified();
    UndoEntry u;
    u.child = childLayer;
    u.annotation = true;
    u.annotatedCluster = id;
    u.annotationToRestore = before;
    pushUndoEntry(u);
    return true;
}

bool KlustersDoc::recordChildChange(const QVector<dataType>& before){
    if (!childData || before.isEmpty() || childData->labelByFeatureRow() == before) return false;
    UndoEntry u;
    u.child = true;
    u.childLabels = before;
    pushUndoEntry(u);
    return true;
}

void KlustersDoc::afterChildLabelsRestored(KlustersView& activeView){
    syncChildColors();
    rebuildHierarchyFromData();
    // A safety net, not the mechanism: the restored labelling was consistent with the
    // parents when it was taken, so this finds nothing to re-cut.
    collapseToSelfChildren();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    modified = true;
}

// Put the entry's annotation back and keep the one it replaced, for the opposite direction.
void KlustersDoc::swapAnnotation(UndoEntry& e){
    Data* layer = e.child ? childData : clusteringData;
    if (!layer) return;
    QList<QString> fields;
    layer->getUserClusterInformation(e.annotatedCluster, fields);
    if (fields.isEmpty()) return;                  // the cluster is gone: nothing to put back
    const UnitAnnotation now = UnitAnnotation::fromFields(fields);
    layer->setUserClusterAnnotation(e.annotatedCluster, e.annotationToRestore);
    e.annotationToRestore = now;
    clusterInformationModified();
    emit clusterAnnotationRestored(e.child, e.annotatedCluster);
}

bool KlustersDoc::stepOneEntry(UndoHistory::Direction d, KlustersView& activeView){
    const bool isRedo = (d == UndoHistory::Redo);
    const UndoHistory::Direction other = isRedo ? UndoHistory::Undo : UndoHistory::Redo;
    if (undoHistory.isEmpty(d)) return false;
    UndoEntry e = undoHistory.take(d);
    if (e.annotation) { swapAnnotation(e); undoHistory.put(other, e); return true; }
    const QVector<dataType> now = childData ? childData->labelByFeatureRow() : QVector<dataType>();
    if (!e.child) {
        // No level left for it (the depth was lowered): the entry is dropped.
        if ((isRedo ? parentRedoCount() : parentUndoCount()) == 0) return false;
        if (isRedo) redo(); else undo();           // one parent level: data, colours, views, log
    }
    if (childData && !e.childLabels.isEmpty() && e.childLabels != now) {
        childData->restoreClusterLabels(e.childLabels);
        afterChildLabelsRestored(activeView);
    } else if (childData && !e.child) {
        afterChildLabelsRestored(activeView);      // the parents moved under the children: re-derive
    }
    if (e.child) logChildUndoRedo(isRedo, e.edit.added, e.edit.modified, e.edit.deleted, e.edit.logIdx);
    e.childLabels = now;                           // what the opposite step of this entry restores
    undoHistory.put(other, e);
    return true;
}

void KlustersDoc::stepDispatch(UndoHistory::Direction d){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v || undoHistory.isEmpty(d)) return;
    undoHistory.closeGroups();                     // no command can be open while the user undoes
    // One step: the newest entry, and with it every entry of the same command.
    const int g = undoHistory.newest(d).group;
    do {
        if (!stepOneEntry(d, *v)) break;
    } while (g != 0 && !undoHistory.isEmpty(d) && undoHistory.newest(d).group == g);
    refreshUndoRedoEnable();
}

// The forced child undo / redo (Ctrl+Shift+Z / Y): the newest entry when it is a child
// edit.  A newer parent edit is undone first, with Ctrl+Z -- stepping over it would put
// the children back under parents that have since changed.
bool KlustersDoc::undoChildEdit(KlustersView& activeView){
    if (!childData || childUndoCount() == 0) return false;
    const bool ok = stepOneEntry(UndoHistory::Undo, activeView);
    refreshUndoRedoEnable();
    return ok;
}
bool KlustersDoc::redoChildEdit(KlustersView& activeView){
    if (!childData || childRedoCount() == 0) return false;
    const bool ok = stepOneEntry(UndoHistory::Redo, activeView);
    refreshUndoRedoEnable();
    return ok;
}
bool KlustersDoc::undoChildEditDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    return v ? undoChildEdit(*v) : false;
}
bool KlustersDoc::redoChildEditDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    return v ? redoChildEdit(*v) : false;
}
