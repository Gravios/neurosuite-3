// klustersdoc_renumber.cpp — KlustersDoc cluster renumbering / reordering.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// cluster id remapping methods of KlustersDoc — renumberClusters, applyClusterRename,
// renumberClustersToEnd, and reorderClustersByPermutation.  Declarations remain in
// klustersdoc.h; mechanical relocation, no logic change.  Carries the same include
// preamble as klustersdoc.cpp so every symbol resolves identically.
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

void KlustersDoc::renumberClusters(){
    //Get the active view.
    KlustersView* activeView = app()->activeView();

    QMap<int,int> clusterIdsOldNew;
    QMap<int,int> clusterIdsNewOld;

    // Quiesce worker threads before mutating the shared cluster layout, mirroring
    // the group/delete/undo/realign paths: renumber rebuilds spikesByCluster and
    // swaps clusterInfoMap, which a concurrent correlogram/matrix reader would
    // torn-read.  (Data::renumber early-returns on already-compact ids, so this is
    // a no-op stop in that common case.)
    for (KlustersView* view : *viewList)
        view->stopAllViewThreads();

    clusteringData->renumber(clusterIdsOldNew,clusterIdsNewOld);

    // A no-op renumber (ids already compact -- the usual case right after a manual
    // split, which appends a contiguous id) comes back with empty maps from
    // Data::renumber.  Nothing changed, so skip the undo entry and, the reason this
    // matters, the updateClusterList() rebuild below: the triggering edit already
    // rebuilt the palette this same event-loop turn, and on a large hierarchical
    // session that second full rebuild is pure redundant cost (and a stray undo step).
    if(clusterIdsOldNew.isEmpty()) return;

    prepareUndo(clusterIdsOldNew,clusterIdsNewOld);

    //Update the clusterColorList: keep each cluster's colour, only relabel its
    //stored id through the old->new map the renumber just produced.
    //
    //This previously walked clusterColorList positionally --
    //  for (i in 0..clusteringData->clusterIds().size())
    //      clusterColorList->changeItemId(i, clusterIds[i]);
    //which assumes the colour list holds exactly one entry per data cluster, in the
    //same order.  In the hierarchy paths the parent colour list can end up with
    //fewer entries than clusteringData has clusters (clusters exist in the data with
    //no colour entry), so the positional walk ran past the end of itemList.  Because
    //ItemColors::changeItemId does an unchecked QList::at(), that dereferenced a
    //garbage pointer -> the SIGSEGV seen via the deferred autoPostClusterEdit ->
    //renumberClusters after grouping two parent fibers.
    //
    //Resolve every colour entry's position by its OLD id up front, then apply the
    //new ids.  All lookups happen by old id BEFORE any mutation, so a freshly written
    //new id can't alias an as-yet-unprocessed old id; the resolved indices stay valid
    //through the apply loop because changeItemId only rewrites the id field, never
    //reorders.  Clusters without a colour entry (and colour entries without a data
    //cluster) are simply skipped instead of crashing.
    {
        QVector<QPair<int,int>> idUpdates;            // (colour-list index, new id)
        idUpdates.reserve(clusterIdsOldNew.size());
        for (auto it = clusterIdsOldNew.constBegin(); it != clusterIdsOldNew.constEnd(); ++it){
            const int colorIndex = clusterColorList->itemIndex(it.key());   // by OLD id
            if (colorIndex >= 0)
                idUpdates.append(qMakePair(colorIndex, it.value()));
        }
        for (const QPair<int,int>& u : idUpdates)
            clusterColorList->changeItemId(u.first, u.second);
    }

    // Translate S-pinned cluster ids through the rename so any
    // pinning the user established before R survives the renumber.
    clusterPalette.renumberPinnedIds(clusterIdsOldNew);
    // Translate a pending post-edit fiber selection through the same map so it still
    // lands on the produced fibers after the ids are compacted.
    renumberPendingFiberSelection(clusterIdsOldNew);

    //Notify all the views of the modification
    const int numberOfView(viewList->count());
    for(int i =0; i<numberOfView;++i)
    {
        KlustersView* view = viewList->at(i);
        if (view != activeView){
            view->renumberClusters(clusterIdsOldNew,false);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,false);
        } else {
            view->renumberClusters(clusterIdsOldNew,true);
            //update the TraceView if any
            view->updateTraceView(electrodeGroupID,clusterColorList,true);
        }
    }

    //Notify the errorMatrixView of the modification
    emit renumber(clusterIdsOldNew);

    //Reset the color status in clusterColors if need it
    if(clusterColorList->isColorChanged()) clusterColorList->resetAllColorStatus();

    activeView->showAllWidgets();

    //Update the palette of cluster
    // FIBER palette, unconditionally: this pipeline renames fibers and knows nothing
    // about atoms, so routing it by the current scope would hand fiber ids to the
    // child palette and match nothing.
    // NEVER activeView->clusters() here.  That is the FIBER selection only while
    // the views are showing fibers; in child scope they show the selected CHILDREN,
    // so it returns ATOM ids, and handing those to the fiber palette selects
    // whatever fibers carry the same numbers.  The child palette repopulates from
    // the fiber selection, so it then switches to an unrelated parent and the user's
    // children vanish -- parentSlotA jumping 1983 -> 39, then 1983 -> 96.
    //
    // Conditioning that on childScopeActive was not enough, and the trace shows why:
    // slotChildSelectionChanged() sets it FALSE whenever the rebuilt child palette
    // comes up with nothing selected, which is exactly the moment the automatic
    // renumber runs.  The flag is false while the views are still showing children,
    // so the guard fell through to the very call it was added to avoid.
    //
    // The fiber palette's own selection is the fiber selection in BOTH scopes, by
    // definition, so take it unconditionally and map it through the rename.  No flag
    // to be wrong about.
    // A renumber changes ids, not which clusters are selected.  The list still has
    // to be refreshed either way -- the ids on it have moved.
    QList<int> activeClusters;
    for (int id : clusterPalette.selectedClusters())
        activeClusters.append(clusterIdsOldNew.value(id, id));
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(activeClusters);
    shownClustersUpdate(activeClusters,*activeView);
    // Hierarchical: the renumber compacts parent ids but leaves child->parent
    // pointing at the old ids, orphaning every renamed parent's atoms.  Re-derive
    // the map from the (renumbered) parent spike labels.  No hierarchyChanged emit
    // here: renumberClusters runs inside the deferred post-edit flow and that signal
    // would re-enter it (see scheduleAutoPostClusterEdit, which deliberately hooks
    // only renumber()).  The child palette refreshes on the post-edit parent
    // selection (applyPendingFiberSelection) or the next user selection.
    if (childData) rebuildHierarchyFromData();
}

// ---------------------------------------------------------------------------
// KlustersDoc::applyClusterRename
//
// Single primitive for "rename a set of clusters" actions.  Used by:
//   - renumberClustersToEnd (T key)        — partial rename to tail
//   - renumberClusters (full sequential)   — TODO Phase 2.5 plumbing below
//   - watershed residual cleanup           — folded into integrateBasinLabeling
//
// The mutation runs across THREE state tables and the GUI signal bus,
// in the order required for consistency:
//
//   1. Data layer (renumberPartial) — the spike table + clusterInfoMap
//      get the new IDs; old IDs vanish.  This pushes its own data-side
//      undo entry.
//   2. Colour list — each renamed cluster's ItemColor entry is
//      relabelled in place via changeItemId so the colour persists.
//   3. Each KlustersView — shownClusters list rewrites its old IDs to
//      new IDs via changeClusterIds (called inside renumberClusters).
//   4. Errormatrix / template-matrix — receive the rename via the
//      KlustersDoc::renumber() Qt signal.
//
// Caller is responsible for the doc-level UNDO snapshot (`prepareUndo` /
// `prepareReclusteringUndo` / etc.) and any logBefore/logAfter pairs;
// this helper does only the apply.
//
// `partialOldToNew` lists ONLY the renamed clusters.  `fullOldToNew` is
// a covering map (every existing cluster ID maps to its post-rename ID,
// identity for unchanged) — required because KlustersView::renumberClusters
// calls changeClusterIds, which historically expected a covering map
// (since fixed to be partial-safe, but covering callers are still safer).
// Pass nullptr for `fullOldToNew` to have it built automatically.
// ---------------------------------------------------------------------------
void KlustersDoc::applyClusterRename(const QMap<int,int>& partialOldToNew,
                                      const QMap<int,int>* fullOldToNewOpt)
{
    if (partialOldToNew.isEmpty()) return;

    KlustersView* activeView = app()->activeView();
    if (!activeView) return;

    // Build a covering map if the caller didn't supply one.
    QMap<int,int> fullOwned;
    const QMap<int,int>* full = fullOldToNewOpt;
    if (!full) {
        const QList<dataType> existing = clusteringData->clusterIds();
        for (dataType eid : existing) {
            const int iid = static_cast<int>(eid);
            fullOwned.insert(iid, partialOldToNew.value(iid, iid));
        }
        full = &fullOwned;
    }

    // 1. Data layer — pushes its own data-side undo entry.
    clusteringData->renumberPartial(partialOldToNew);

    // 2. Colour list — rename in place so colours persist.
    for (auto it = partialOldToNew.constBegin();
         it != partialOldToNew.constEnd(); ++it) {
        const int oldId = it.key();
        const int newId = it.value();
        const int idx = clusterColorList->itemIndex(oldId);
        if (idx >= 0) clusterColorList->changeItemId(idx, newId);
    }
    // changeItemId mutates IDs in-place without re-ordering the
    // underlying itemList, so a partial rename like the T-key
    // "renumber to end" leaves the renamed entry sitting at its old
    // storage position.  The palette renders in storage order, so the
    // icon would still appear where the OLD ID lived.  Re-sort by
    // itemId so the palette layout matches the new ID ordering.
    //
    // Safe to do here because (a) the renumber-undo snapshot was
    // taken before this — prepareClusterColorUndo deep-copies via
    // ItemColors's copy ctor, and the snapshot is immutable from this
    // point — and (b) Data::renumberPartial now re-packs the spike
    // table in ascending-id order, so highestClusterId() and friends
    // report correct values for all subsequent renumbers.
    clusterColorList->sortByItemId();

    // 2b. Translate S-pinned cluster ids through the rename so a
    // pinned cluster keeps its pin under its new id.  Without this,
    // pins would be silently dropped on the next palette refresh
    // (updateClusterList prunes pinned ids whose cluster no longer
    // exists, and after a rename the OLD id is gone).
    clusterPalette.renumberPinnedIds(partialOldToNew);

    // 3. Each view — rewrites shownClusters; emits its own clustersRenumbered.
    for (KlustersView* v : *viewList) {
        const bool isActive = (v == activeView);
        // Cast the const-ref to a non-const ref for the legacy view API.
        // changeClusterIds inside renumberClusters does not mutate it.
        v->renumberClusters(const_cast<QMap<int,int>&>(*full), isActive);
        v->updateTraceView(electrodeGroupID, clusterColorList, isActive);
    }

    // 4. Errormatrix / template-matrix.
    emit renumber(const_cast<QMap<int,int>&>(*full));

    if (clusterColorList->isColorChanged())
        clusterColorList->resetAllColorStatus();

    activeView->showAllWidgets();

    // Refresh palette.
    // FIBER palette, unconditionally: this pipeline renames fibers and knows nothing
    // about atoms, so routing it by the current scope would hand fiber ids to the
    // child palette and match nothing.
    // NEVER activeView->clusters() here.  That is the FIBER selection only while
    // the views are showing fibers; in child scope they show the selected CHILDREN,
    // so it returns ATOM ids, and handing those to the fiber palette selects
    // whatever fibers carry the same numbers.  The child palette repopulates from
    // the fiber selection, so it then switches to an unrelated parent and the user's
    // children vanish -- parentSlotA jumping 1983 -> 39, then 1983 -> 96.
    //
    // Conditioning that on childScopeActive was not enough, and the trace shows why:
    // slotChildSelectionChanged() sets it FALSE whenever the rebuilt child palette
    // comes up with nothing selected, which is exactly the moment the automatic
    // renumber runs.  The flag is false while the views are still showing children,
    // so the guard fell through to the very call it was added to avoid.
    //
    // The fiber palette's own selection is the fiber selection in BOTH scopes, by
    // definition, so take it unconditionally and map it through the rename.  No flag
    // to be wrong about.
    // A renumber changes ids, not which clusters are selected.  The list still has
    // to be refreshed either way -- the ids on it have moved.
    QList<int> activeClusters;
    for (int id : clusterPalette.selectedClusters())
        activeClusters.append(full->value(id, id));
    clusterPalette.updateClusterList();
    clusterPalette.selectItems(activeClusters);
    // Hierarchical: a parent rename (T-key renumber-to-end, Shift+S reorder,
    // watershed) leaves child->parent pointing at the dead old id, so the renamed
    // parent's atoms orphan and vanish from the child palette.  Re-derive the map
    // from the renamed parent spike labels and refresh the child palette.  Unlike
    // renumberClusters, this path is a direct user action (not the deferred post-edit
    // flow), so emitting hierarchyChanged here cannot re-enter that flow.
    if (childData) {
        rebuildHierarchyFromData();
        emit hierarchyChanged();
    }
}

// ---------------------------------------------------------------------------
// KlustersDoc::renumberClustersToEnd
//
// Targeted rename: each cluster in `clustersToRenumber` gets a new ID
// greater than the current global maximum, so they end up at the end of
// the (sorted-by-ID) palette.  Triggered by the palette T shortcut.
//
// Selection ordering: clusters are renamed in ascending order so the
// lowest-numbered selected cluster gets the lowest new ID
// (currentMax+1), preserving relative order at the tail.
//
// Edge cases handled by the caller (slotMoveSelectedClustersToEnd):
//   - global-max cluster filtered out (already at end)
//   - 0 / 1 (artefact / noise) filtered out
//
// Single undo entry covers the whole batch — Ctrl+Z reverts all renames
// in one step, matching the user's mental model of T as a single action.
// ---------------------------------------------------------------------------
void KlustersDoc::renumberClustersToEnd(QList<int> clustersToRenumber)
{
    if (clustersToRenumber.isEmpty()) return;
    if (!app()->activeView()) return;

    const QList<dataType> existing = clusteringData->clusterIds();
    if (existing.isEmpty()) return;
    int nextNewId = static_cast<int>(clusteringData->nextFreeClusterId());

    // Build the partial old→new map AND a full coverage map.
    QMap<int,int> partialOldToNew;
    QMap<int,int> fullOldToNew;
    QMap<int,int> fullNewToOld;
    for (dataType eid : existing) {
        fullOldToNew.insert(static_cast<int>(eid), static_cast<int>(eid));
        fullNewToOld.insert(static_cast<int>(eid), static_cast<int>(eid));
    }
    std::sort(clustersToRenumber.begin(), clustersToRenumber.end());
    for (int oldId : clustersToRenumber) {
        if (oldId == 0 || oldId == 1) continue;            // never rename specials
        if (!fullOldToNew.contains(oldId))     continue;   // skip missing
        const int newId = nextNewId++;
        partialOldToNew.insert(oldId, newId);
        fullOldToNew.insert(oldId, newId);
        fullNewToOld.insert(newId, oldId);
        fullNewToOld.remove(oldId);
    }
    if (partialOldToNew.isEmpty()) return;

    // Quiesce worker threads before mutating the shared cluster layout, mirroring
    // the group/delete/undo/realign paths: the id-remap rebuilds spikesByCluster
    // and swaps clusterInfoMap, which a concurrent correlogram/matrix reader would
    // torn-read.
    for (KlustersView* view : *viewList)
        view->stopAllViewThreads();

    // ── Curation log: this is a renumber, not a group ──
    logBefore(CurationLogger::ActionType::RENUMBER_PARTIAL, clustersToRenumber);

    // ── Doc-level undo snapshot (uses the renumber-specific stack so
    //    KlustersDoc::undo can detect this as a renumber action).
    prepareUndo(fullOldToNew, fullNewToOld);

    // ── Apply.
    applyClusterRename(partialOldToNew, &fullOldToNew);

    // Log post-rename.
    QList<int> newIds;
    for (int oldId : clustersToRenumber)
        newIds.append(partialOldToNew.value(oldId, oldId));
    logAfter(newIds);
}

// ---------------------------------------------------------------------------
// KlustersDoc::renumberChildrenToEnd
//
// The child-palette T: give each selected atom an id above the current atom
// maximum so it sorts to the end of the id-ordered child palette.
//
// This is deliberately NOT applyClusterRename() with a different pointer.  That
// pipeline shares only about three of its eight steps with an atom rename, and
// the rest would actively corrupt: it drives every view's renumberClusters()
// (rewriting FIBER shownClusters through an ATOM map), renames entries in the
// parent clusterColorList, and renumbers the parent palette's S-pins.  None of
// those describe what happened when an atom changes id.  Parameterising the
// shared pipeline would have meant guarding most of its body -- a shared policy
// re-expressed with exceptions, which is how this codebase's worst bugs start.
//
// What an atom rename actually needs is: the atom layer's own renumber, its own
// colour list kept in step, the fiber<-atom map re-derived because the rename
// changes childToParent's KEYS, and the child palette told to refresh and land
// on the moved atoms.
// ---------------------------------------------------------------------------
void KlustersDoc::renumberChildrenToEnd(QList<int> atomsToRenumber)
{
    if (!childData || atomsToRenumber.isEmpty()) return;

    // Reserve atoms are the artifact/noise self children; renaming them would
    // break the same special-cluster semantics the parent path protects.
    atomsToRenumber.removeAll(0);
    atomsToRenumber.removeAll(1);
    if (atomsToRenumber.isEmpty()) return;

    const QList<dataType> existing = childData->clusterIds();
    if (existing.isEmpty()) return;

    // Ascending, so a multi-atom selection keeps its relative order at the end.
    std::sort(atomsToRenumber.begin(), atomsToRenumber.end());
    int nextNewId = static_cast<int>(childData->nextFreeClusterId());

    QMap<int,int> partialOldToNew;
    for (int oldId : atomsToRenumber) {
        if (!existing.contains(static_cast<dataType>(oldId))) continue;
        partialOldToNew.insert(oldId, nextNewId++);
    }
    if (partialOldToNew.isEmpty()) return;

    // renumberPartial refuses a non-injective map and one touching 0/1; the two
    // filters above and the strictly increasing nextNewId satisfy both, so a
    // refusal here would mean the atom layer's map was already inconsistent.
    childData->renumberPartial(partialOldToNew);

    // Colours follow their atom, then re-sort: changeItemId mutates ids in place
    // without reordering, and the palette renders in storage order, so without the
    // sort a renamed atom would still draw where its OLD id sat.
    ItemColors& colors = childClusterColors();
    for (auto it = partialOldToNew.constBegin(); it != partialOldToNew.constEnd(); ++it) {
        const int idx = colors.itemIndex(it.key());
        if (idx >= 0) colors.changeItemId(idx, it.value());
    }
    colors.sortByItemId();

    // The rename changes childToParent's keys, so the derived maps must be
    // re-derived rather than patched; hierarchyChanged then makes the app
    // repopulate the child palette for the current parent.
    rebuildHierarchyFromData();
    emit hierarchyChanged();

    // Land on the atoms that just moved, at their new ids.
    QList<int> moved;
    for (auto it = partialOldToNew.constBegin(); it != partialOldToNew.constEnd(); ++it)
        moved.append(it.value());
    emit hierarchyChildSelectionRequested(moved);

    setModified(true);
}

// ---------------------------------------------------------------------------
// KlustersDoc::reorderClustersByPermutation
//
// Drives the rename pipeline (logBefore + prepareUndo + applyClusterRename +
// logAfter) from an externally-supplied cluster order.  Used by the Shift+S
// "Reorder Clusters by Similarity" action — the slot computes the order via
// agglomerative clustering on the active similarity matrix and hands it
// here, so all the undo/log/palette/view bookkeeping stays inside the doc
// layer where it belongs (mirrors the renumberClustersToEnd pattern).
//
// New IDs start at 2; clusters 0 (artefact) and 1 (noise) are untouched and
// stay at the front of the palette.  Any newOrder entry referencing 0, 1,
// or a missing cluster makes the whole call a no-op (returns -1) so we never
// half-apply a rename.
// ---------------------------------------------------------------------------
int KlustersDoc::reorderClustersByPermutation(const QList<int>& newOrder)
{
    if (newOrder.isEmpty()) return 0;
    if (!app()->activeView()) return -1;

    const QList<dataType> existing = clusteringData->clusterIds();
    if (existing.isEmpty()) return -1;

    QSet<int> existingSet;
    for (dataType eid : existing) existingSet.insert(static_cast<int>(eid));

    // Validate: every newOrder entry must be a real, non-special cluster.
    QSet<int> seen;
    for (int cid : newOrder) {
        if (cid <= 1) return -1;
        if (!existingSet.contains(cid)) return -1;
        if (seen.contains(cid)) return -1;       // duplicates not allowed
        seen.insert(cid);
    }

    // Build maps.  We rename the listed clusters to consecutive IDs
    // starting at 2; any existing cluster NOT in newOrder keeps its
    // current ID — that's an artefact of the slot only handing us
    // clusters present in the similarity matrix, and is fine because
    // applyClusterRename uses Data::renumberPartial which preserves
    // unlisted IDs.
    QMap<int,int> partialOldToNew;
    QMap<int,int> fullOldToNew;
    QMap<int,int> fullNewToOld;
    for (dataType eid : existing) {
        const int iid = static_cast<int>(eid);
        fullOldToNew.insert(iid, iid);
        fullNewToOld.insert(iid, iid);
    }
    int nextNewId = 2;
    QList<int> renamedClusters;
    for (int oldId : newOrder) {
        const int newId = nextNewId++;
        if (oldId == newId) continue;          // already in target slot
        partialOldToNew.insert(oldId, newId);
        fullOldToNew.insert(oldId, newId);
        fullNewToOld.remove(oldId);
        fullNewToOld.insert(newId, oldId);
        renamedClusters.append(oldId);
    }

    if (partialOldToNew.isEmpty()) return 0;   // already in order

    // Reject if the rename would map two old IDs to the same new ID.
    // This shouldn't happen given the per-newOrder uniqueness check
    // above PLUS the consecutive-from-2 assignment, but applyClusterRename
    // doesn't validate this, so we defend before mutating state.
    QSet<int> targets;
    for (auto it = fullOldToNew.constBegin();
         it != fullOldToNew.constEnd(); ++it)
    {
        if (targets.contains(it.value())) return -1;
        targets.insert(it.value());
    }

    // Curation log + undo snapshot use the same pattern as
    // renumberClustersToEnd, so undo/redo behaviour and log replay
    // both treat this as a partial-renumber action.
    // Quiesce worker threads before mutating the shared cluster layout, mirroring
    // the group/delete/undo/realign paths: the id-remap rebuilds spikesByCluster
    // and swaps clusterInfoMap, which a concurrent correlogram/matrix reader would
    // torn-read.
    for (KlustersView* view : *viewList)
        view->stopAllViewThreads();

    logBefore(CurationLogger::ActionType::RENUMBER_PARTIAL, renamedClusters);
    prepareUndo(fullOldToNew, fullNewToOld);
    applyClusterRename(partialOldToNew, &fullOldToNew);

    QList<int> newIds;
    newIds.reserve(renamedClusters.size());
    for (int oldId : renamedClusters)
        newIds.append(partialOldToNew.value(oldId, oldId));
    logAfter(newIds);

    return partialOldToNew.size();
}
