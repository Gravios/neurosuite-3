// klustersdoc_hierarchy.cpp — KlustersDoc hierarchical (.clc child) clustering layer.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// child/fiber-layer methods of KlustersDoc — child clustering load/build, the
// fiber edit operations (promote/move/group/dissolve/merge/drop), child-edit undo
// stack, and the undo/redo dispatch that routes between the parent and child
// layers.  Declarations remain in klustersdoc.h; mechanical relocation, no logic
// change.  Carries the same include preamble as klustersdoc.cpp so every symbol
// resolves identically.
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

// ── hierarchical (.clc child) clustering ─────────────────────────────────────

void KlustersDoc::setActiveClustering(bool child){
    if (child && childData){
        activeData       = childData;
        activeColorList  = childColorList;
        childScopeActive = true;
    } else {
        activeData       = clusteringData;
        activeColorList  = clusterColorList;
        childScopeActive = false;
    }
}

void KlustersDoc::buildHierarchyMaps(){
    parentToChildren.clear();
    childToParent.clear();
    if (!clusteringData) return;

    // Preferred source: the .clp child->parent map (binary clu-format, one int32
    // header = nFibers, then nChildren int32 parent-fiber ids indexed by child id;
    // parent[c-1] owns child c, child ids are global 1..nChildren).  Authoritative
    // and O(nChildren) -- no per-spike scan, and correct even if a child id were
    // reused across parents in the per-spike arrays.
    if (!clpSiblingPath.isEmpty()){
        const qint64 bytes = QFileInfo(clpSiblingPath).size();
        if (bytes > 4){
            const int64_t nChildren = (bytes - 4) / 4;            // minus the int32 header
            const neurofileio::CluFile clp =
                neurofileio::readCluBinary(clpSiblingPath.toStdString(), nChildren);
            if (clp.ok && static_cast<int64_t>(clp.ids.size()) == nChildren){
                for (int64_t i = 0; i < nChildren; ++i){
                    const int childId  = static_cast<int>(i + 1);  // child ids are 1-based
                    const int parentId = clp.ids[i];
                    if (parentId <= 0) continue;                   // 0 = noise / unmapped
                    childToParent.insert(childId, parentId);
                    parentToChildren[parentId].append(childId);
                }
                for (auto it = parentToChildren.begin(); it != parentToChildren.end(); ++it){
                    QList<int>& kids = it.value();
                    std::sort(kids.begin(), kids.end());
                    kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
                }
                qDebug() << "[hierarchy] built from .clp:" << childToParent.size()
                         << "children under" << parentToChildren.size() << "parents";
                return;
            }
            qWarning() << "[hierarchy] .clp present but unreadable; falling back to .clu/.clc scan";
        }
    }

    // Fallback: derive from the aligned per-spike parent (.clu == docUrl) and
    // child (.clc) id arrays; both are binary clu-format aligned to the same .res.
    if (clcSiblingPath.isEmpty()) return;
    const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    const neurofileio::CluFile par = neurofileio::readCluBinary(docUrl.toStdString(), nSpikes);
    const neurofileio::CluFile chi = neurofileio::readCluBinary(clcSiblingPath.toStdString(), nSpikes);
    if (!par.ok || !chi.ok || par.ids.size() != chi.ids.size()){
        qWarning() << "[hierarchy] could not read aligned .clu/.clc; hierarchy maps empty";
        return;
    }

    // child -> parent must be a function (nesting invariant): each child id is
    // owned by exactly one parent.  A violation means the .clc is not nested in
    // the .clu; we keep the first-seen owner and warn rather than silently merge.
    bool nestingViolation = false;
    const std::size_t n = par.ids.size();
    for (std::size_t i = 0; i < n; ++i){
        const int p = par.ids[i];
        const int c = chi.ids[i];
        const auto existing = childToParent.constFind(c);
        if (existing != childToParent.constEnd()){
            if (existing.value() != p) nestingViolation = true;
        } else {
            childToParent.insert(c, p);
            parentToChildren[p].append(c);
        }
    }
    if (nestingViolation)
        qWarning() << "[hierarchy] .clc is not nested in .clu (a child id spans "
                      "multiple parents); kept first-seen parent for each child.";

    for (auto it = parentToChildren.begin(); it != parentToChildren.end(); ++it){
        QList<int>& kids = it.value();
        std::sort(kids.begin(), kids.end());
        kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
    }
}

bool KlustersDoc::loadChildClustering(QString& errorInformation){
    if (childData) return true;                        // already loaded
    if (clcSiblingPath.isEmpty() || !siblingYamlForm){
        errorInformation = tr("Hierarchical view needs a YAML-form .clc child file "
                              "next to the opened .clu.");
        return false;
    }
    QFile fetFile(siblingFetPath);
    QFile clcFile(clcSiblingPath);
    QFile yamlParFile(siblingYamlPath);
    if (!fetFile.open(QIODevice::ReadOnly) || !clcFile.open(QIODevice::ReadOnly)
        || !yamlParFile.open(QIODevice::ReadOnly)){
        errorInformation = tr("Cannot open the .clc child siblings.");
        if (fetFile.isOpen())     fetFile.close();
        if (clcFile.isOpen())     clcFile.close();
        if (yamlParFile.isOpen()) yamlParFile.close();
        return false;
    }
    // Second Data over the SAME fet/spk/par, with the .clc as the cluster file.
    // (v1 re-reads the feature/spike arrays; they could later be shared with the
    // parent to halve memory.)
    childData = new Data();
    const bool ok = childData->initialize(
        fetFile, clcFile, siblingSpkFileLength, siblingSpkPath,
        yamlParFile, electrodeGroupID.toInt(), errorInformation);
    yamlParFile.close(); fetFile.close(); clcFile.close();
    if (!ok){
        delete childData; childData = nullptr;
        return false;
    }
    // Colour list for the child clusters (same HSV scheme as the parent build).
    childColorList = new ItemColors();
    const QList<dataType> kids = childData->clusterIds();
    for (dataType id : kids){
        QColor color;
        if (id == 1) color.setHsv(0,0,220);
        else color.setHsv(static_cast<int>(fmod(static_cast<double>(id)*7,36))*10,200,255);
        childColorList->append(static_cast<int>(id), color);
    }
    buildHierarchyMaps();
    return true;
}

QList<int> KlustersDoc::childrenOf(const QList<int>& parents) const{
    QList<int> out;
    for (int p : parents){
        const auto it = parentToChildren.constFind(p);
        if (it != parentToChildren.constEnd()) out += it.value();
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void KlustersDoc::setChildScope(const QList<int>& visibleChildren){
    childScopeVisible = QSet<int>(visibleChildren.begin(), visibleChildren.end());
}

bool KlustersDoc::isChildScopeHidden(int clusterId) const{
    if (!childData) return false;
    if (!childToParent.contains(clusterId)) return false;   // parent id: never hidden here
    return !childScopeVisible.contains(clusterId);
}

// ── hierarchy edits ──────────────────────────────────────────────────────────
// All three operate on the parent clustering (clusteringData) through the
// existing, proven, undoable doc primitives — merge via groupClusters, promote
// and move via moveSpikeSubsetToCluster (which creates the target if new).  The
// fiber<-child maps are then re-derived from the data, so a single Ctrl+Z (which
// reverts clusteringData) plus the same re-derive on undo/redo keeps everything
// consistent without a separate map-undo stack.

void KlustersDoc::rebuildHierarchyFromData(){
    if (!childData) return;
    parentToChildren.clear();
    childToParent.clear();
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());
    for (int r = 1; r < n; ++r){                  // feature rows are 1-based
        const int c = static_cast<int>(childByRow[r]);
        if (c <= 0) continue;
        if (!childToParent.contains(c)){
            const int f = static_cast<int>(cluByRow[r]);
            childToParent.insert(c, f);
            parentToChildren[f].append(c);
        }
    }
    for (auto it = parentToChildren.begin(); it != parentToChildren.end(); ++it){
        QList<int>& kids = it.value();
        std::sort(kids.begin(), kids.end());
        kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
    }
}

void KlustersDoc::refiberize(){
    // Explicit bridge between the two otherwise-independent layers.  The user
    // reassigns and consolidates the fibers as a flat .clu workflow (split / move /
    // merge), which leaves the child atoms where they were -- so an atom whose
    // spikes now span more than one parent fiber straddles the nesting invariant.
    // refiberize re-cuts ONLY those straddling atoms: the first parent keeps the
    // original atom id and each other parent's portion becomes a fresh atom.  Atoms
    // wholly within one fiber are left untouched, so existing sub-structure
    // survives.  moveSpikeSubset rebuilds spikesByCluster + clusterInfoMap together
    // (no zero-spike phantom) and nextFreeClusterId keeps new atom ids in the
    // child-id space (above the highest child id; no parent/child collision).
    if (!childData) return;
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());
    QHash<int, QHash<int, QSet<dataType>>> atomParentRows;   // atom -> parent -> rows
    for (int r = 1; r < n; ++r){
        const int atom = static_cast<int>(childByRow[r]);
        if (atom <= 0) continue;                              // noise/unassigned: no atom to carve
        const int parent = static_cast<int>(cluByRow[r]);
        atomParentRows[atom][parent].insert(static_cast<dataType>(r));
    }
    for (auto a = atomParentRows.constBegin(); a != atomParentRows.constEnd(); ++a){
        const QHash<int, QSet<dataType>>& parentRows = a.value();
        if (parentRows.size() < 2) continue;                 // wholly inside one parent -> intact
        bool first = true;
        for (auto p = parentRows.constBegin(); p != parentRows.constEnd(); ++p){
            if (first){ first = false; continue; }           // first parent keeps the atom id
            const int newAtom = static_cast<int>(childData->nextFreeClusterId());
            QList<int> fromC, emptied;
            childData->moveSpikeSubset(a.key(), p.value(), newAtom, fromC, emptied);
        }
    }
    // refiberize is its own deliberate resync, independent of the parent and atom
    // undo timelines: it does NOT couple to a parent undo (no childPre snapshot --
    // a parent undo no longer silently reverts the atom layer), and it resets the
    // atom-edit history since the atom structure has just been re-cut.
    // rebuildHierarchyFromData re-derives childToParent / parentToChildren from the
    // new labels, which the next Save writes out as the regenerated .clp.
    childUndoStack.clear();
    childRedoStack.clear();
    syncChildColors();
    rebuildHierarchyFromData();
    emit hierarchyChanged();
}

int KlustersDoc::mergeParentFibers(const QList<int>& fibers, KlustersView& activeView){
    if (fibers.size() < 2) return -1;
    setActiveClustering(false);                   // edits always target the parent
    const int kept = groupClusters(fibers, activeView);   // existing: mutate + undo + views
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    return kept;
}

int KlustersDoc::promoteChild(int childCluster, KlustersView& activeView){
    if (!childData || !childToParent.contains(childCluster)) return -1;
    const int parent = childToParent.value(childCluster);
    if (parentToChildren.value(parent).size() <= 1)
        return parent;                            // only child == already its own fiber
    const QVector<int> spk = childData->clusterSpkIndices(childCluster);
    if (spk.isEmpty()) return -1;
    setActiveClustering(false);
    const int newId = static_cast<int>(clusteringData->nextFreeClusterId());
    moveSpikeSubsetToCluster(parent, spk, newId, activeView);   // creates newId
    // moveSpikeSubsetToCluster only colours the noise cluster; give the new fiber
    // its own colour.  Appended AFTER the call so it is NOT in that op's undo
    // snapshot -> a subsequent Ctrl+Z removes the colour with the cluster.
    if (!clusterColorList->contains(newId)){
        QColor color;
        color.setHsv(static_cast<int>(fmod(static_cast<double>(newId) * 7, 36)) * 10, 200, 255);
        clusterColorList->append(newId, color);
        clusterPalette.updateClusterList();
    }
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    return newId;
}

bool KlustersDoc::moveChild(int childCluster, int targetFiber, KlustersView& activeView){
    if (!childData || !childToParent.contains(childCluster)) return false;
    const int parent = childToParent.value(childCluster);
    if (parent == targetFiber) return false;      // already there
    const QVector<int> spk = childData->clusterSpkIndices(childCluster);
    if (spk.isEmpty()) return false;
    setActiveClustering(false);
    moveSpikeSubsetToCluster(parent, spk, targetFiber, activeView);   // targetFiber exists
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    return true;
}

int KlustersDoc::groupChildrenIntoFiber(const QList<int>& children, KlustersView& activeView){
    if (!childData || children.isEmpty()) return -1;
    setActiveClustering(false);
    const int newId = static_cast<int>(clusteringData->nextFreeClusterId());
    int target = -1;
    for (int c : children){
        if (!childToParent.contains(c)) continue;
        const int parent = childToParent.value(c);
        if (parent == newId) continue;                // already pooled
        const QVector<int> spk = childData->clusterSpkIndices(c);
        if (spk.isEmpty()) continue;
        moveSpikeSubsetToCluster(parent, spk, (target < 0 ? newId : target), activeView);
        if (target < 0){
            target = newId;
            // colour the new fiber (appended after the first move so a Ctrl+Z
            // that reverts that move also drops the colour).
            if (!clusterColorList->contains(newId)){
                QColor color;
                color.setHsv(static_cast<int>(fmod(static_cast<double>(newId) * 7, 36)) * 10, 200, 255);
                clusterColorList->append(newId, color);
            }
        }
    }
    if (target < 0) return -1;
    clusterPalette.updateClusterList();
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    return target;
}

bool KlustersDoc::dissolveFiber(int fiber, KlustersView& activeView){
    if (!childData) return false;
    const QList<int> kids = parentToChildren.value(fiber);
    if (kids.size() < 2) return false;                // nothing to explode
    // promoteChild detaches each child into its own fiber; the final child is
    // the only one left under `fiber` so its promote no-ops and it keeps the id.
    for (int c : kids)
        promoteChild(c, activeView);                  // each re-derives + emits
    return true;
}

bool KlustersDoc::dropChildToNoise(int childCluster, KlustersView& activeView){
    if (!childData || !childToParent.contains(childCluster)) return false;
    const int parent = childToParent.value(childCluster);
    if (parent == 1) return false;                    // already noise
    const QVector<int> spk = childData->clusterSpkIndices(childCluster);
    if (spk.isEmpty()) return false;
    setActiveClustering(false);
    moveSpikeSubsetToCluster(parent, spk, 1, activeView);   // 1 = noise (coloured by the wrapper)
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    return true;
}

void KlustersDoc::syncChildColors(){
    if (!childData || !childColorList) return;
    const QList<dataType> ids = childData->clusterIds();
    for (dataType id : ids){
        if (!childColorList->contains(static_cast<int>(id))){
            QColor color;
            if (id == 1) color.setHsv(0, 0, 220);
            else color.setHsv(static_cast<int>(fmod(static_cast<double>(id) * 7, 36)) * 10, 200, 255);
            childColorList->append(static_cast<int>(id), color);
        }
    }
}

int KlustersDoc::mergeChildren(const QList<int>& children, KlustersView& activeView){
    if (!childData || children.size() < 2) return -1;
    // Same-fiber guard: merging atoms from different fibers would create a
    // microfiber whose spikes straddle two fibers, which the nesting invariant
    // forbids.  Refuse rather than silently re-parent.
    const int parent = childToParent.value(children.first(), -1);
    for (int c : children)
        if (childToParent.value(c, -2) != parent) return -1;

    QList<int> grp = children;                         // groupClusters takes a non-const ref
    const int newId = static_cast<int>(childData->groupClusters(grp));   // mutates childData, self-snapshots
    ChildEdit e; e.added = { newId }; e.deleted = children;
    recordChildEdit(e);

    syncChildColors();
    rebuildHierarchyFromData();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    lastEditLayer = EditLayer::Atom;
    modified = true;
    return newId;
}

bool KlustersDoc::undoChildEdit(KlustersView& activeView){
    if (!childData || childUndoStack.isEmpty()) return false;
    ChildEdit e = childUndoStack.takeFirst();
    QList<int> added = e.added;                        // by-ref args; copy so the entry is preserved
    QList<int> mod = e.modified;
    childData->undo(added, mod);                       // reverts childData's tables
    childRedoStack.prepend(e);
    syncChildColors();
    rebuildHierarchyFromData();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    lastEditLayer = EditLayer::Atom;
    modified = true;
    emit updateUndoNb(parentUndoCount());
    emit updateRedoNb(parentRedoCount());
    return true;
}

bool KlustersDoc::redoChildEdit(KlustersView& activeView){
    if (!childData || childRedoStack.isEmpty()) return false;
    ChildEdit e = childRedoStack.takeFirst();
    QList<int> added = e.added, mod = e.modified, deleted = e.deleted;
    childData->redo(added, mod, deleted);             // re-applies the atom edit
    childUndoStack.prepend(e);
    syncChildColors();
    rebuildHierarchyFromData();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    lastEditLayer = EditLayer::Atom;
    modified = true;
    // Refresh the main Undo/Redo enable: the slots OR the child counts, so emit
    // the parent counts to drive a combined re-evaluation.
    emit updateUndoNb(parentUndoCount());
    emit updateRedoNb(parentRedoCount());
    return true;
}

// ── unified undo/redo dispatcher ─────────────────────────────────────────────
// Two real undo timelines exist -- the parent stack (clusteringData, driven by
// undo()/redo()) and the atom stack (childData, driven by undoChildEdit()/
// redoChildEdit()).  editOrderUndo/editOrderRedo record, newest-first, which
// layer each edit touched, so one keystroke reverts the single most recent edit.
// The live per-layer counts are authoritative: a marker whose layer stack was
// capped (parent nbUndo) or bulk-cleared (a parent op re-cutting the child layer)
// is stale and skipped, so the timeline needs no cap/clear bookkeeping of its own.

void KlustersDoc::recordChildEdit(const ChildEdit& e){
    childUndoStack.prepend(e);
    childRedoStack.clear();                       // a new edit invalidates atom redo
    editOrderUndo.prepend(EditMarker{EditLayer::Atom, {}, {}});       // unified-order marker
    editOrderRedo.clear();                        // ... and the unified redo
    // Make the main Undo enable pick up this atom-only edit (slots OR child count).
    emit updateUndoNb(parentUndoCount());
    emit updateRedoNb(parentRedoCount());
}

void KlustersDoc::undoDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v) return;
    while (!editOrderUndo.isEmpty()){
        const EditLayer layer = editOrderUndo.first().layer;
        if (layer == EditLayer::Atom){
            if (childUndoCount() > 0){
                editOrderRedo.prepend(editOrderUndo.takeFirst());
                undoChildEdit(*v);
                return;
            }
        } else {                                  // Parent (None treated as parent)
            if (parentUndoCount() > 0){
                EditMarker m = editOrderUndo.takeFirst();
                // Couple the child-layer re-cut this parent edit triggered: revert
                // childData to its pre-re-cut snapshot (saving the post-re-cut state
                // for redo) so undo()'s tail rebuildHierarchyFromData sees BOTH layers
                // at their pre-op state.  Empty childPre == this parent edit touched
                // only the parent layer, so nothing to couple.
                if (childData && !m.childPre.isEmpty()){
                    m.childPost = childData->labelByFeatureRow();
                    childData->restoreClusterLabels(m.childPre);
                    syncChildColors();
                }
                editOrderRedo.prepend(m);
                undo();
                return;
            }
        }
        editOrderUndo.removeFirst();              // stale marker (its stack is empty): discard
    }
    // No usable marker (e.g. an edit made before the timeline existed): fall back
    // to whichever timeline still has something to revert, parent first.
    if (parentUndoCount() > 0)      undo();
    else if (childUndoCount() > 0)  undoChildEdit(*v);
}

void KlustersDoc::redoDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v) return;
    while (!editOrderRedo.isEmpty()){
        const EditLayer layer = editOrderRedo.first().layer;
        if (layer == EditLayer::Atom){
            if (childRedoCount() > 0){
                editOrderUndo.prepend(editOrderRedo.takeFirst());
                redoChildEdit(*v);
                return;
            }
        } else {
            if (parentRedoCount() > 0){
                EditMarker m = editOrderRedo.takeFirst();
                if (childData && !m.childPost.isEmpty()){
                    childData->restoreClusterLabels(m.childPost);   // re-apply the child re-cut
                    syncChildColors();
                }
                editOrderUndo.prepend(m);
                redo();
                return;
            }
        }
        editOrderRedo.removeFirst();
    }
    if (parentRedoCount() > 0)     redo();
    else if (childRedoCount() > 0) redoChildEdit(*v);
}

bool KlustersDoc::undoChildEditDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v || childUndoCount() == 0) return false;
    // Forced atom undo (Ctrl+Shift+Z): remove the most-recent Atom marker from the
    // unified order -- wherever it sits, a newer parent edit may be above it -- and
    // move it to the redo side so redoDispatch replays it in order.
    EditMarker m{EditLayer::Atom, {}, {}};
    for (int i = 0; i < editOrderUndo.size(); ++i)
        if (editOrderUndo.at(i).layer == EditLayer::Atom){ m = editOrderUndo.takeAt(i); break; }
    editOrderRedo.prepend(m);
    return undoChildEdit(*v);
}

bool KlustersDoc::redoChildEditDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v || childRedoCount() == 0) return false;
    EditMarker m{EditLayer::Atom, {}, {}};
    for (int i = 0; i < editOrderRedo.size(); ++i)
        if (editOrderRedo.at(i).layer == EditLayer::Atom){ m = editOrderRedo.takeAt(i); break; }
    editOrderUndo.prepend(m);
    return redoChildEdit(*v);
}

bool KlustersDoc::saveHierarchySiblings(){
    if (!childData || clcSiblingPath.isEmpty()) return true;   // nothing to write
    // .clc — the per-spike child layer (unchanged by merge/promote/move, but
    // rewritten so the triple is regenerated together); .clp — the edited
    // child->parent map.  Overwrite in place with a .bak backup of each.
    auto backup = [](const QString& p){
        if (QFile::exists(p)){
            const QString b = p + QStringLiteral(".bak");
            QFile::remove(b);
            QFile::copy(p, b);
        }
    };
    bool ok = true;
    backup(clcSiblingPath);
    if (FILE* f = fopen(qPrintable(clcSiblingPath), "wb")){
        ok &= childData->saveClusters(f);
        fclose(f);
    } else ok = false;

    if (!clpSiblingPath.isEmpty()){
        int nChildren = 0;
        for (auto it = childToParent.constBegin(); it != childToParent.constEnd(); ++it)
            nChildren = qMax(nChildren, it.key());
        backup(clpSiblingPath);
        if (FILE* f = fopen(qPrintable(clpSiblingPath), "wb")){
            const int32_t hdr = nChildren;                 // header ignored by readers (count derived from size)
            ok &= (fwrite(&hdr, sizeof(int32_t), 1, f) == 1);
            for (int c = 1; c <= nChildren && ok; ++c){
                const int32_t p = childToParent.value(c, 0);
                ok &= (fwrite(&p, sizeof(int32_t), 1, f) == 1);
            }
            fclose(f);
        } else ok = false;
    }
    if (!ok) qWarning() << "[hierarchy] failed to write .clc/.clp siblings";
    return ok;
}
