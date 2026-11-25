// klustersdoc_hierarchy.cpp — KlustersDoc hierarchical (.clc child) clustering layer.
//
// Part of the klustersdoc.cpp decomposition: this translation unit implements the
// child/parent-layer methods of KlustersDoc — child clustering load/build, the
// parent edit operations (promote/move/group/dissolve/merge/drop), child-edit undo
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
#include <QFileInfo>

// application specific includes
#include "processwidget.h"
#include "klusters.h"
#include "reservedclusters.h"
#include "klustersdoc.h"
#include "configuration.h"
#include "klustersview.h"
#include <neurosuite/core/watershed2d.hpp>
#include "clusterview.h"
#include "klustersdoc.h"
#include "clusterpalette.h"
#include "types.h"
#include "parameteryamlmodifier.h"
#include <neurosuite/core/dipsplit.hpp>
#include "parameteryamlreader.h"
#include "clusteruserinformation.h"
#include "hierarchyfiles.h"

//C, C++ include files
//#define _LARGEFILE_SOURCE already defined in /usr/include/features.h
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <math.h>
#include <climits>


#include <neurosuite/core/neurofileio.h>  // variant-aware input resolution
#include <neurosuite/core/custody.hpp>     // shared chain-of-custody policy

// ── hierarchical (.clc child) clustering ─────────────────────────────────────


namespace {
QVariantList idList(const QList<int>& l) { QVariantList v; for (int x : l) v << x; return v; }
}

void KlustersDoc::setActiveClustering(bool child){
    // Every layer flip, in order, so the log shows which step moved the view.
    if (scopeTraceEnabled() && child != childScopeActive)
        qDebug().noquote() << QStringLiteral("[scope] setActiveClustering %1 -> %2")
                              .arg(childScopeActive ? "child" : "parent")
                              .arg(child ? "child" : "parent");
    if ((child && childData) != childScopeActive) ++layerSwitches;
    if (child && childData){
        activeData       = childData;
        activeColorList  = childColorList;
        childScopeActive = true;
    } else {
        activeData       = clusteringData;
        activeColorList  = clusterColorList;
        childScopeActive = false;
    }
    // The main Undo/Redo follow the shown layer, so re-evaluate their enable state
    // against the now-active stack.
    refreshUndoRedoEnable();
}

void KlustersDoc::buildHierarchyMaps(){
    parentToChildren.clear();
    childToParent.clear();
    hierarchyScanFoundViolation = false;
    if (!clusteringData) return;

    // The per-spike arrays are the TRUTH: .clu (== docUrl) is what curation edits and what the user
    // sees, and .clc is the child layer aligned to the same .res.  The .clp is only a CACHE of the
    // derived child->parent map -- it is rewritten on Save, and the child layer is loaded lazily, so
    // a session curated without ever opening the hierarchical view leaves a .clp that no longer
    // matches its .clu.  NOTE the per-spike .clu to read is the live PENDING copy, not docUrl.  It used to be PREFERRED here and returned early with no cross-check, so a
    // stale cache silently overrode the real mapping.  Scan the per-spike arrays first (one pass over
    // two int32 arrays -- negligible beside the .fet/.spk load); the .clp is consulted only when the
    // per-spike arrays cannot be read, and is otherwise merely cross-checked and reported.
    bool haveScan = false;
    bool nestingViolation = false;
    if (!clcSiblingPath.isEmpty()){
        // The session's scratch .clu is the PENDING copy: initPendingFiles() seeds <docUrl>.pending
        // from docUrl and every save rewrites it with what it saved, so it is read in preference;
        // docUrl stands in when the session runs without a pending set.
        const QString liveClu = (!pendingCluPath.isEmpty() && QFile::exists(pendingCluPath))
                              ? pendingCluPath : docUrl;
        const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
        const neurofileio::CluFile par = neurofileio::readCluBinary(liveClu.toStdString(), nSpikes);
        const neurofileio::CluFile chi = neurofileio::readCluBinary(clcSiblingPath.toStdString(), nSpikes);
        if (par.ok && chi.ok && par.ids.size() == chi.ids.size()){
            haveScan = true;
            // A .clc in the numbering used before 2026-11-21 is read through the same
            // upgrade loadChildClustering applies to the child layer, so the maps name
            // the children the session works with.  Its .clp is keyed by the old ids and
            // is not cross-checked below; the next Save writes both anew.
            const hierarchyfiles::ChildIdUpgrade upgrade = hierarchyfiles::upgradeChildIds(
                std::vector<int>(par.ids.begin(), par.ids.end()), std::vector<int>(chi.ids.begin(), chi.ids.end()));
            clcWasOldNumbering = upgrade.needed;
            // child -> parent must be a function (nesting invariant): each child id is owned by
            // exactly one parent.  A violation means the .clc is not nested in the .clu; we keep the
            // first-seen owner and warn rather than silently merge.
            const std::size_t n = par.ids.size();
            for (std::size_t i = 0; i < n; ++i){
                const int p = par.ids[i];
                const int c = upgrade.needed ? upgrade.ids[i] : chi.ids[i];
                const auto existing = childToParent.constFind(c);
                if (existing != childToParent.constEnd()){
                    if (existing.value() != p) nestingViolation = true;
                } else {
                    childToParent.insert(c, p);
                    parentToChildren[p].append(c);
                }
            }
            if (nestingViolation){
                hierarchyScanFoundViolation = true;
                qWarning() << "[hierarchy] .clc is not nested in .clu (a child id spans "
                              "multiple parents); kept first-seen parent for each child.";
            }
        } else {
            qWarning() << "[hierarchy] could not read aligned .clu/.clc; falling back to the .clp cache";
        }
    }

    if (haveScan){
        // Cross-check the cache against the truth and report drift; the scan wins either way and
        // the next Save regenerates the .clp from the per-spike layers (prepareSave).
        if (!clpSiblingPath.isEmpty() && !clcWasOldNumbering){
            const qint64 bytes = QFileInfo(clpSiblingPath).size();
            if (bytes > 4){
                const int64_t nChildren = (bytes - 4) / 4;      // minus the int32 header
                const neurofileio::CluFile clp =
                    neurofileio::readCluBinary(clpSiblingPath.toStdString(), nChildren);
                if (clp.ok && static_cast<int64_t>(clp.ids.size()) == nChildren){
                    int stale = 0;
                    for (auto it = childToParent.constBegin(); it != childToParent.constEnd(); ++it){
                        const int c = it.key();
                        if (c <= 0 || static_cast<int64_t>(c) > nChildren) continue;
                        if (clp.ids[c - 1] != it.value()) ++stale;
                    }
                    if (stale)
                        qWarning() << "[hierarchy] .clp is STALE:" << stale << "of"
                                   << childToParent.size()
                                   << "children disagree with the per-spike .clu/.clc; using the "
                                      "scan.  The .clp is regenerated on the next Save.";
                }
            }
        }
    } else if (!clpSiblingPath.isEmpty()){
        // No readable per-spike arrays: the cache is all we have.  Binary clu-format, int32 header
        // then nChildren int32 parent ids; parent[c-1] owns child c, child ids are global 1..nChildren.
        const qint64 bytes = QFileInfo(clpSiblingPath).size();
        if (bytes > 4){
            const int64_t nChildren = (bytes - 4) / 4;
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
                qWarning() << "[hierarchy] built from the .clp cache alone (no readable .clu/.clc); "
                              "it may not reflect the current curation.";
            } else {
                qWarning() << "[hierarchy] .clp unreadable and no aligned .clu/.clc; hierarchy maps empty";
                return;
            }
        }
    } else {
        qWarning() << "[hierarchy] no .clc sibling and no .clp; hierarchy maps empty";
        return;
    }

    for (auto it = parentToChildren.begin(); it != parentToChildren.end(); ++it){
        QList<int>& kids = it.value();
        std::sort(kids.begin(), kids.end());
        kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
    }
    qDebug() << "[hierarchy]" << childToParent.size() << "children under"
             << parentToChildren.size() << "parents";
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
    // Stop any in-flight per-view worker threads before touching Data.  Committing
    // the hierarchical session (this call) rebuilds the parent cluster map and
    // reads its .spk over the shared feature/spike arrays; a matrix or waveform
    // worker iterating clusterInfoMap concurrently races that rebuild.  On startup
    // the session is committed immediately after the KlustersView — and thus its
    // just-launched TemplateMatrixThread — is created, so the two overlap and the
    // load crashes intermittently.  This is the same stop-before-mutation guard the
    // merge, realign, and undo paths already take before their Data mutations.
    if (viewList)
        for (KlustersView* v : *viewList)
            v->supersedeAllViewThreads();

    // Second Data over the SAME fet/spk/par, with the .clc as the cluster file.
    // (v1 re-reads the feature/spike arrays; they could later be shared with the
    // parent to halve memory.)
    childData = new Data();
    // The child layer: its ids are children, not clusters (the parent layer protects
    // the reserved chunk clusters, see Data::setProtectsReservedClusters).
    childData->setProtectsReservedClusters(false);
    // Read the SAME .spk the parent currently reads: if a realign earlier this session
    // repointed clusteringData to the pending (realigned) .spk, the child must follow it,
    // otherwise children open showing the original (shifted) waveforms.  Same spike count,
    // so siblingSpkFileLength still applies.
    const QString childSpkPath =
        clusteringData ? clusteringData->getSpkFileName() : siblingSpkPath;
    const bool ok = childData->initialize(
        fetFile, clcFile, siblingSpkFileLength, childSpkPath,
        yamlParFile, electrodeGroupID.toInt(), errorInformation);
    yamlParFile.close(); fetFile.close(); clcFile.close();
    if (!ok){
        delete childData; childData = nullptr;
        return false;
    }
    // The child initialises from the .clu space's siblings; if another feature
    // space is loaded, bring the child into it so both layers match.
    if (isBorrowedSpace()
        && !childData->replaceFeatureSpace(activeFetPath, activeSpkPath, errorInformation)){
        delete childData; childData = nullptr;
        return false;
    }
    // Colour list for the child clusters (same HSV scheme as the parent build).
    // The numbering of children (hierarchyfiles.h): parents are 0 .. 9999 and the reserved
    // chunk ids; a parent's self child is parent + 10000; every other child is 20000 or more.
    for (const dataType id : clusteringData->clusterIds()) {
        if (hierarchyfiles::parentIdFits(id)) continue;
        errorInformation = tr("Cluster %1: the child layer numbers a parent's own child parent + %2, which "
                              "leaves room for parents up to %3. Renumber the clusters, then open the "
                              "hierarchical view again.")
                               .arg(id).arg(hierarchyfiles::kSelfChildBase).arg(hierarchyfiles::kSelfChildBase - 1);
        delete childData; childData = nullptr;
        return false;
    }
    childData->setFreshIdFloor(hierarchyfiles::kFreeChildBase);
    {
        // A .clc written before 2026-11-21 numbers its children like parents.  Bring it to the
        // numbering above; the labels are restored, not edited, so nothing lands on the
        // undo stack, and the session is marked changed so that Save writes the new
        // .clc and .clp over the old pair.
        const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
        const QVector<dataType> childByRow = childData->labelByFeatureRow();
        if (cluByRow.size() == childByRow.size() && childByRow.size() > 1) {
            const std::vector<int> parentIds(cluByRow.constBegin() + 1, cluByRow.constEnd());
            const std::vector<int> childIds(childByRow.constBegin() + 1, childByRow.constEnd());
            const hierarchyfiles::ChildIdUpgrade upgrade = hierarchyfiles::upgradeChildIds(parentIds, childIds);
            if (upgrade.needed) {
                QVector<dataType> labels = childByRow;
                for (std::size_t i = 0; i < upgrade.ids.size(); ++i)
                    labels[static_cast<int>(i) + 1] = static_cast<dataType>(upgrade.ids[i]);
                childData->restoreClusterLabels(labels);
                childIdsUpgraded = static_cast<int>(upgrade.oldToNew.size());
                modified = true;
                qInfo().noquote() << QStringLiteral("[hierarchy] %1 was in the old child numbering: %2 children renumbered "
                                                    "(a parent's own child to parent + %3, the others from %4); Save writes it.")
                                         .arg(QFileInfo(clcSiblingPath).fileName()).arg(childIdsUpgraded)
                                         .arg(hierarchyfiles::kSelfChildBase).arg(hierarchyfiles::kFreeChildBase);
            }
        }
    }
    childColorList = new ItemColors();
    const QList<dataType> kids = childData->clusterIds();
    for (dataType id : kids){
        QColor color;
        if (id == 1) color.setHsv(0,0,220);
        else color.setHsv(static_cast<int>(fmod(static_cast<double>(id)*7,36))*10,200,255);
        childColorList->append(static_cast<int>(id), color);
    }
    buildHierarchyMaps();
    {
        // The names, as every edit leaves them: a parent's only child is its self child.
        // A file written by another tool, or upgraded just above, need not say so yet.
        QMap<int, int> renamed;
        if (normaliseChildIds(&renamed)) {
            childIdsUpgraded = qMax(childIdsUpgraded, static_cast<int>(renamed.size()));
            modified = true;
            syncChildColors();
            rebuildHierarchyFromData();
            qInfo().noquote() << QStringLiteral("[hierarchy] %1 children renamed to the numbering (a parent's only child is parent + %2); Save writes it.")
                                     .arg(renamed.size()).arg(hierarchyfiles::kSelfChildBase);
        }
    }
    // Curation log: the session IS hierarchical from here (SESSION_OPEN was written
    // before the child layer existed).
    noteLogContext({ { QStringLiteral("hierarchical"), true },
                     { QStringLiteral("n_parents"),  static_cast<int>(clusteringData->clusterIds().size()) },
                     { QStringLiteral("n_children"), static_cast<int>(childData->clusterIds().size()) },
                     { QStringLiteral("child_ids_upgraded"), childIdsUpgraded } });
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
// parent<-child maps are then re-derived from the data, so a single Ctrl+Z (which
// reverts clusteringData) plus the same re-derive on undo/redo keeps everything
// consistent without a separate map-undo stack.

// ---------------------------------------------------------------------------
// KlustersDoc::updateHierarchyShadow
//
// Maintain the child-primary shadow and compare it against the derived map.
//
// Additive and inert unless NS3_SHADOW_HIERARCHY is set.  The point is evidence:
// the child-primary model is only worth adopting if it tracks the current one
// exactly across real curation, and that is a measurement rather than an
// argument.  A divergence names the edit path that produced it.
//
// The two differ in one respect BY DESIGN, and it is the whole reason for the
// change: the derived map keeps a child's FIRST-SEEN owner when the child straddles
// two parents, silently discarding the rest.  The shadow records the straddle
// instead.  So a mismatch here means either a genuine straddler -- which the
// derived map is hiding -- or a real divergence.  Both are worth knowing and the
// message distinguishes them.
// ---------------------------------------------------------------------------
void KlustersDoc::deriveStoredMap()
{
    if (!childPrimaryOn || !childData || !clusteringData) return;
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());
    shadowChildToParent.clear();
    for (int r = 1; r < n; ++r){
        const int c = static_cast<int>(childByRow[r]);
        if (c <= 0) continue;
        if (!shadowChildToParent.contains(c))
            shadowChildToParent.insert(c, static_cast<int>(cluByRow[r]));
    }
}

void KlustersDoc::updateHierarchyShadow(const QVector<dataType>& cluByRow,
                                        const QVector<dataType>& childByRow, int n)
{
    const bool wanted = childPrimaryOn
                     || qEnvironmentVariableIsSet("NS3_SHADOW_HIERARCHY");
    if (!wanted) return;

    // What this measures, now that the map is derived rather than maintained.
    //
    // Comparing the map against a re-derivation would compare the derivation with
    // itself -- both read these same two arrays -- and report agreement
    // unconditionally.  That mistake has already been made twice in this session;
    // making it a third time would produce a reassuring log and no information.
    //
    // The question worth asking is the one the representation exists for: does
    // every child sit under exactly one parent?  A straddling child is a state the
    // child-primary map cannot express, so the straddler count is the distance
    // between what the arrays hold and what the model can represent -- and driving
    // it to zero is what the migration has to achieve.
    QElapsedTimer timer; timer.start();
    QMap<int,int> firstSeen;
    QSet<int> straddlers;
    for (int r = 1; r < n; ++r){
        const int c = static_cast<int>(childByRow[r]);
        if (c <= 0) continue;
        const int f = static_cast<int>(cluByRow[r]);
        const auto it = firstSeen.constFind(c);
        if (it == firstSeen.constEnd()) firstSeen.insert(c, f);
        else if (it.value() != f)       straddlers.insert(c);
    }

    qDebug().noquote() << (childPrimaryOn ? "[childprimary]" : "[shadow]")
                       << (straddlers.isEmpty() ? "NESTED" : "STRADDLE")
                       << " children=" << firstSeen.size()
                       << " straddlers=" << straddlers.size()
                       << " derive=" << (timer.nsecsElapsed()/1000) << "us";
    if (!straddlers.isEmpty()){
        QList<int> sample(straddlers.constBegin(), straddlers.constEnd());
        std::sort(sample.begin(), sample.end());
        if (sample.size() > 8) sample = sample.mid(0, 8);
        qWarning() << "[childprimary] children under more than one parent:"
                   << straddlers.size() << "e.g." << sample
                   << "-- not representable by the child-primary map; these are what"
                      " collapseToSelfChildren re-cuts.";
    }
}


bool KlustersDoc::compactAllClusterIds(){
    KlustersView* activeView = app()->activeView();

    // ── Parents ──────────────────────────────────────────────────────────
    // Whole-cloth reuse: renumberClusters() carries the colour relabelling,
    // the undo entry, the curation-log placeholder, the per-view relabel and
    // the palette rebuild, and it no-ops when the parents are already compact.
    const QList<dataType> parentsBefore = clusteringData->clusterIds();
    renumberClusters();
    const bool parentsMoved = (clusteringData->clusterIds() != parentsBefore);

    if (!childData) {
        if (parentsMoved && activeView) activeView->showAllWidgets();
        return parentsMoved;
    }

    // ── Atoms ────────────────────────────────────────────────────────────
    // Same Data::renumber, so the two layers cannot drift into different
    // notions of "compact".  Threads are quiesced for the same reason the
    // parent path quiesces them: renumber rebuilds the row table underneath
    // any reader.
    for (KlustersView* view : *viewList)
        view->supersedeAllViewThreads();

    QMap<int,int> childOldNew, childNewOld;
    const QVector<dataType> childrenBefore = childData->labelByFeatureRow();
    childData->renumber(childOldNew, childNewOld);
    const bool childrenMoved = !childOldNew.isEmpty();
    if (parentsMoved || childrenMoved) {
        // The parent half already placed its undo placeholder (renumberClusters);
        // this names the command and the child map.
        QVariantMap childMap;
        for (auto it = childOldNew.constBegin(); it != childOldNew.constEnd(); ++it)
            childMap.insert(QString::number(it.key()), it.value());
        beginGesture(QStringLiteral("compactAllClusterIds"),
                     {{ QStringLiteral("parents_moved"), parentsMoved }, { QStringLiteral("atoms_moved"), childrenMoved },
                      { QStringLiteral("atom_old_to_new"), childMap }});
        endGesture();
    }

    if (childrenMoved) {
        // The parent<->child maps are a DERIVED cache: re-deriving them from
        // the spike labels is both simpler and safer than translating every
        // entry through the map, and it is the same call every child edit
        // already makes.  It also re-reports a straddling child if one exists,
        // which a hand translation would have carried silently into the new
        // numbering.
        rebuildHierarchyFromData();

        // Atom colours are keyed by id, so they must be relabelled or every child
        // changes colour under the user.  changeItemId takes the entry's INDEX,
        // not its old id -- passing the id renames whichever entry happens to sit
        // at that position, which leaves TWO entries carrying the same id (with
        // different colours) and the palette then draws that child twice.  The
        // child-renumber path a few hundred lines down has always done this
        // correctly; this one did not, and the doubled children in the child
        // palette were the result.  Same shape as that path, including the
        // re-sort: changeItemId does not reorder, and the palette renders in
        // storage order, so without it a renamed child draws where its old id sat.
        if (childColorList) {
            for (auto it = childOldNew.constBegin(); it != childOldNew.constEnd(); ++it) {
                const int idx = childColorList->itemIndex(it.key());
                if (idx >= 0) childColorList->changeItemId(idx, it.value());
            }
            childColorList->sortByItemId();
        }
        syncChildColors();

        // The child undo stack records edits by ATOM ID, and those ids have
        // just moved.  Replaying one now would name the wrong children, so the
        // stack is dropped rather than silently misapplied -- the caller says
        // so in the status bar.  (The parent undo entry that renumberClusters
        // pushed is unaffected: it carries its own old<->new maps.)
        // (The child entries of the undo history are not dropped any more: each holds the
        // labelling to restore, ids and all, and the parent renumbering entry above them
        // holds the labelling from before this renumbering.)

        // The joint working set also names children, but unlike the undo stack it
        // is only ids, so it translates instead of dropping: childOldNew is the
        // very map its ids just moved through.  Left untranslated, a held old
        // id would name whichever child INHERITED the number after compaction
        // -- the wrong-family failure shape this patch exists to close.
        for (int& c : jointChildren)
            if (childOldNew.contains(c)) c = childOldNew.value(c);
        for (int& c : scopeSortOrder)                     // same id space, same map
            if (childOldNew.contains(c)) c = childOldNew.value(c);

        // With the parents renumbered too, their entry already holds the children from before.
        if (!parentsMoved) recordChildChange(childrenBefore);
        emit hierarchyChanged();
        setModified(true);
    }

    if ((parentsMoved || childrenMoved) && activeView)
        activeView->showAllWidgets();
    return parentsMoved || childrenMoved;
}

void KlustersDoc::rebuildHierarchyFromData(){
    if (!childData) return;
    parentToChildren.clear();
    childToParent.clear();
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());
    // The nesting invariant (hierarchical-clustering.md): every child is owned by
    // exactly one parent -- all spikes carrying a given .clc id carry the same .clu
    // id.  buildHierarchyMaps() already checks this on the LOAD path, but this
    // post-edit rebuild did not: it inserted on first-seen and dropped every later
    // (child,parent) pair on the floor, so a child left straddling two parents by an
    // edit (a collapseToSelfChildren that did not fully collapse, or an edit path
    // that never called it) was filed under its first-seen parent with no trace.
    // That silent absorption is exactly what produced the g5 "5 children span two
    // parents" file.  First-seen still wins -- the maps are a derived cache and the
    // behaviour is unchanged -- but a violation is now reported, with the offending
    // children, so the edit that introduced it can be found instead of vanishing.
    QSet<int> offenders;                           // distinct children seen under >1 parent
    for (int r = 1; r < n; ++r){                  // feature rows are 1-based
        const int c = static_cast<int>(childByRow[r]);
        if (c <= 0) continue;
        const int f = static_cast<int>(cluByRow[r]);
        const auto existing = childToParent.constFind(c);
        if (existing != childToParent.constEnd()){
            if (existing.value() != f) offenders.insert(c);   // straddler: keep first-seen, flag it
        } else {
            childToParent.insert(c, f);
            parentToChildren[f].append(c);
        }
    }
    if (!offenders.isEmpty()){
        QList<int> sample(offenders.constBegin(), offenders.constEnd());
        std::sort(sample.begin(), sample.end());
        if (sample.size() > 16) sample = sample.mid(0, 16);
        qWarning() << "[hierarchy] nesting invariant broken after edit:" << offenders.size()
                   << "atom(s) span more than one parent; kept each atom's first-seen owner. "
                      "Offending atom id(s) (up to 16):" << sample
                   << "-- an edit left straddling atoms uncollapsed; repairNesting to re-cut them.";
    }
    for (auto it = parentToChildren.begin(); it != parentToChildren.end(); ++it){
        QList<int>& kids = it.value();
        std::sort(kids.begin(), kids.end());
        kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
    }

    // Shadow comparison, after the derived maps are complete and before any
    // consumer sees them.  Inert unless NS3_SHADOW_HIERARCHY is set.
    updateHierarchyShadow(cluByRow, childByRow, n);

    // The maps this scope is derived from have just been refilled; re-resolve so
    // no consumer can observe the empty window that clear() opens.
    resolveMatrixScope();
}

// ---------------------------------------------------------------------------
// Scoped matrices: which clusters the error / template / residual / drift views
// should compare while the child palette is driving.
//
// The views are constructed with a KlustersDoc& and already read doc.data(),
// which is the ATOM layer whenever a child is the shown clustering -- so they
// are pointed at the right data already and only need to be told WHICH children.
// That is one parent's children, and the parent is the child palette's, which
// lives in KlustersApp; hence the mirror here rather than a lookup.
// ---------------------------------------------------------------------------
void KlustersDoc::setMatrixScopeEnabled(bool enabled)
{
    if (matrixScopeOn == enabled) return;   // no spurious recomputes
    matrixScopeOn = enabled;
    // Leaving the child view through V is one of the two exits that drop the
    // held child-scope sort order (the other is a curated-parent change).
    if (!enabled) clearScopeSortOrder();
    resolveMatrixScope();                   // emits matrixScopeChanged if it moved
}

void KlustersDoc::setPendingChildSelection(const QList<int>& children)
{
    if (!children.isEmpty()) { pendingChildSelection = children; childLandingParked = true; }
}

bool KlustersDoc::takeChildLandingParked()
{
    const bool was = childLandingParked;
    childLandingParked = false;
    return was;
}

QList<int> KlustersDoc::takePendingChildSelection()
{
    QList<int> out = pendingChildSelection;
    pendingChildSelection.clear();
    return out;
}

void KlustersDoc::setCuratedParent(int parentId)
{
    if (scopeTraceEnabled() && parentId != curatedParentId)
        qDebug().noquote() << QStringLiteral("[scope] curatedParent %1 -> %2  matrixScope=%3")
                              .arg(curatedParentId).arg(parentId)
                              .arg(matrixScopeOn ? "on" : "off");
    if (curatedParentId == parentId) return;   // no spurious recomputes
    curatedParentId = parentId;
    // A new curated parent is the other exit that drops the held child-scope
    // sort order (re-selecting the SAME parent early-returns above, so an
    // incidental re-click keeps it).
    clearScopeSortOrder();
    resolveMatrixScope();                     // emits matrixScopeChanged if it moved
}

void KlustersDoc::setCuratedParents(const QList<int>& parents)
{
    // Fewer than two parents is the single-parent mode; hold an empty list so
    // resolveMatrixScope() has exactly one fact to test.  The list is the
    // palette's multi-selection verbatim: selection order is block order.  A
    // reserved chunk cluster has no place in it (no children of it reach a matrix),
    // so it does not count toward the two.
    const QList<int> curatable = reservedclusters::withoutReserved(parents);
    const QList<int> held = (curatable.size() >= 2) ? curatable : QList<int>();
    if (curatedParents == held) return;        // no spurious recomputes
    if (scopeTraceEnabled())
        qDebug().noquote() << QStringLiteral("[scope] curatedParents %1 -> %2  matrixScope=%3")
                              .arg(curatedParents.size()).arg(held.size())
                              .arg(matrixScopeOn ? "on" : "off");
    curatedParents = held;
    // A changed joint selection invalidates the working set the matrices built
    // for the OLD selection: the next joint palette starts empty again.  (An
    // identical re-selection early-returns above, so an incidental re-click
    // cannot empty the palette mid-inspection.)
    jointChildren.clear();
    clearScopeSortOrder();    // the sort was of the OLD scope's children
    resolveMatrixScope();                      // emits matrixScopeChanged if it moved
}

void KlustersDoc::adoptJointChildren(const QList<int>& ids)
{
    // The working set learns a drained landing so an operation's output is
    // LISTABLE in the joint palette -- and stays listed across the deferred
    // rebuilds that follow, which drain no landing of their own.  Appended,
    // not replaced: adopting extends what the user built, in the order it was
    // built.  Meaningless outside a joint selection.
    if (curatedParents.size() < 2) return;
    for (int id : ids)
        if (!jointChildren.contains(id)) jointChildren.append(id);
}

void KlustersDoc::setScopeSortOrder(const QList<int>& orderedIds)
{
    // Meaningful only while a scope is on: an unscoped sort is a renumber and
    // needs no held order.  Replaces, not merges -- each sort states the whole
    // order it wants.
    if (!matrixScopeActive()) return;
    if (scopeSortOrder == orderedIds) return;
    scopeSortOrder = orderedIds;
    emit scopeSortOrderChanged();
}

void KlustersDoc::clearScopeSortOrder()
{
    if (scopeSortOrder.isEmpty()) return;
    scopeSortOrder.clear();
    emit scopeSortOrderChanged();
}

void KlustersDoc::resolveMatrixScope()
{
    const bool was = scopeResolvedActive;
    const QList<int> before = scopeResolvedClusters;

    scopeResolvedClusters.clear();
    scopeResolvedParents.clear();
    scopeResolvedActive = false;
    if (matrixScopeOn && childData) {
        // Joint scope first: every selected parent contributes its children,
        // gathered per parent so the resolved list is grouped in block order
        // (the matrix threads re-sort ids for display; parentOfChild() is the
        // authoritative cluster->parent map for any consumer that needs the
        // grouping back).  A parent without children contributes nothing and is
        // left out of scopeResolvedParents, so the parent bands drawn from it
        // never name a parent with no rows.
        QList<int> parents;
        if (curatedParents.size() >= 2)   parents = curatedParents;
        else if (curatedParentId >= 0)    parents << curatedParentId;
        for (int p : parents) {
            // A reserved parent (a chunk a fiber-kit chunk run left unsorted) is not a
            // unit; its children take no part in the matrices.
            if (isReservedClusterId(p)) continue;
            const QList<int> kids = childrenOf(QList<int>{p});
            if (kids.isEmpty()) continue;
            scopeResolvedClusters += kids;
            scopeResolvedParents  << p;
        }
        scopeResolvedActive = !scopeResolvedClusters.isEmpty();
        if (!scopeResolvedActive) scopeResolvedParents.clear();
    }
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug().noquote() << "[matrixscope] resolved: parent=" << curatedParentId
                           << " joint=" << curatedParents.size()
                           << " on=" << matrixScopeOn
                           << " active=" << scopeResolvedActive
                           << " n=" << scopeResolvedClusters.size();
    if (was != scopeResolvedActive || before != scopeResolvedClusters) {
        if (matrixHoldDepth > 0) scopeEmitDeferred = true;   // one emission at the release
        else emit matrixScopeChanged();
    }
}

void KlustersDoc::beginMatrixHold()
{
    ++matrixHoldDepth;
}

void KlustersDoc::endMatrixHold()
{
    if (matrixHoldDepth <= 0) return;
    if (--matrixHoldDepth > 0) return;
    const bool scopeMoved = scopeEmitDeferred;
    const bool wanted     = matrixUpdateDeferred;
    scopeEmitDeferred = matrixUpdateDeferred = false;
    // One launch, with the scope as it now stands.  A held scope change goes out
    // as the signal (the app recomputes all four matrices, deferred past this
    // event-loop turn); otherwise the edit-driven refresh runs once.
    if (scopeMoved) emit matrixScopeChanged();
    else if (wanted) updateSimilarityMatrices();
}

namespace {
struct DepthGuard {
    int& d;
    explicit DepthGuard(int& depth) : d(depth) { ++d; }
    ~DepthGuard() { --d; }
};
}

void KlustersDoc::selectFromMatrix(const QList<int>& ids, const QList<int>& previous)
{
    if (ids.isEmpty()) return;
    const DepthGuard fromMatrix(matrixSelectionDepth);
    if (matrixScopeActive()) {
        // Atom ids: land them in the child palette, which is where the user is
        // working and where the merge will be made.
        //
        // Under a JOINT scope the palette starts EMPTY and the matrices are the
        // only way children enter it (jointChildrenList()): a cell click REPLACES
        // the working set with that cell's children, and the parked landing +
        // refresh below is the same path every operation's output uses.  No
        // curated-parent retarget: the palette slot keeps its meaning and its
        // value, so the "exactly two situations" contract on curatedParentId
        // stays whole -- and since the scope resolves from curatedParents, a
        // click cannot recompute the matrices either.
        if (scopeResolvedParents.size() >= 2) {
            jointChildren = ids;
            setPendingChildSelection(ids);
            emit childPaletteRefreshRequested();
            return;
        }
        emit hierarchyChildSelectionRequested(ids);
        return;
    }
    if (childScopeActive) emit parentViewRequestedFromMatrix();   // a parent matrix: leave the child view
    QList<int> show = ids, prev = previous;
    if (prev.isEmpty()) shownClustersUpdate(show);
    else                shownClustersUpdate(show, prev);
}
void KlustersDoc::addFromMatrix(const QList<int>& ids)
{
    if (ids.isEmpty()) return;
    const DepthGuard fromMatrix(matrixSelectionDepth);
    if (matrixScopeActive()) {
        // The child palette's own selection is the accumulator, exactly as
        // addClustersToActiveView() uses the cluster palette's.  The doc cannot
        // read that palette (it lives in KlustersApp), so the union is made
        // where the landing is made.
        //
        // Under a JOINT scope a Ctrl-click EXTENDS the working set (a plain
        // click replaces it -- selectFromMatrix above).  Refresh first, so the
        // palette gains the new children while its prior selection survives the
        // redraw (repopulateChildPalette's priorSelection path), then
        // extend-select the added children through the same signal the
        // single-parent branch uses: the palette's own selection stays the
        // accumulator either way.  Both emissions are direct connections, so
        // the refresh has completed before the extension selects.
        if (scopeResolvedParents.size() >= 2) {
            for (int id : ids)
                if (!jointChildren.contains(id)) jointChildren.append(id);
            emit childPaletteRefreshRequested();
            emit hierarchyChildSelectionExtendRequested(ids);
            return;
        }
        emit hierarchyChildSelectionExtendRequested(ids);
        return;
    }
    if (childScopeActive) emit parentViewRequestedFromMatrix();   // as selectFromMatrix
    addClustersToActiveView(ids);
}

Data& KlustersDoc::matrixData() const
{
    // The child layer when a scope is in force; otherwise the PARENT layer --
    // never data().  See the header for why data() is the wrong answer when a
    // scope IS active; it is equally wrong when one is not.  data() follows the
    // child palette's selection, so leaving child view with children still
    // selected returned childData with no scope, and an unscoped matrix over
    // the child layer compares EVERY ATOM in the session.  That is the reported
    // "switching back computes all clusters": parent view means the parent
    // clusters, so it must name clusteringData explicitly.
    return matrixScopeActive() ? *childData : *clusteringData;
}


bool KlustersDoc::validateHierarchyMaps(const char* where) const {
    // Nothing to compare against before the child layer exists, or in a flat
    // session.  This is a genuine blind spot rather than a safe case: a session
    // curated without ever opening the hierarchical view has no childData, so no
    // repair has run and the .clu may well have drifted from the on-disk .clc.
    // That fault is file-level: buildHierarchyMaps()'s scan reports it on load, and
    // prepareSave() repeats the scan on its own snapshot before writing a .clp.
    if (!childData || !clusteringData) return true;
    if (childToParent.isEmpty()) return true;

    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());

    // The relation the per-spike arrays actually encode right now.  Atoms that
    // span parents are counted separately: that is a nesting violation, a
    // different fault from map staleness, and rebuildHierarchyFromData() already
    // reports it -- conflating the two here would misdirect the diagnosis.
    QHash<int,int> scanned;
    scanned.reserve(childToParent.size() * 2 + 16);
    int spanning = 0;
    for (int r = 1; r < n; ++r){
        const int c = static_cast<int>(childByRow[r]);
        if (c <= 0) continue;
        const int f = static_cast<int>(cluByRow[r]);
        const auto it = scanned.constFind(c);
        if (it == scanned.constEnd()) scanned.insert(c, f);
        else if (it.value() != f) ++spanning;
    }

    // Three ways the map can disagree, kept apart because they mean different
    // things: a wrong owner is an edit that moved spikes, a map-only child is an
    // edit that emptied one, a scan-only child is an edit that created one.
    QList<int> wrongOwner, mapOnly, scanOnly;
    for (auto it = childToParent.constBegin(); it != childToParent.constEnd(); ++it){
        const auto s = scanned.constFind(it.key());
        if (s == scanned.constEnd())      mapOnly.append(it.key());
        else if (s.value() != it.value()) wrongOwner.append(it.key());
    }
    for (auto it = scanned.constBegin(); it != scanned.constEnd(); ++it)
        if (!childToParent.contains(it.key())) scanOnly.append(it.key());

    if (wrongOwner.isEmpty() && mapOnly.isEmpty() && scanOnly.isEmpty()) return true;

    // Sort so two runs over the same drift produce the same message.
    std::sort(wrongOwner.begin(), wrongOwner.end());
    std::sort(mapOnly.begin(),    mapOnly.end());
    std::sort(scanOnly.begin(),   scanOnly.end());
    qWarning() << "[hierarchy] derived maps are STALE at" << where << "--"
               << wrongOwner.size() << "atom(s) owned by a different parent than the"
               << "per-spike arrays say," << mapOnly.size() << "in the map but gone from the"
               << "arrays," << scanOnly.size() << "in the arrays but missing from the map."
               << "An edit path mutated a layer without refreshing the maps."
               << "wrong-owner:" << wrongOwner.mid(0, 8)
               << "map-only:"    << mapOnly.mid(0, 8)
               << "scan-only:"   << scanOnly.mid(0, 8);
    if (spanning)
        qWarning() << "[hierarchy] (also" << spanning << "spike(s) whose atom spans parents -- that is a"
                   << "nesting violation, reported separately by rebuildHierarchyFromData)";
    return false;
}


// ---------------------------------------------------------------------------
// normaliseChildIds  --  the children' NAMES brought to the numbering
//
// The numbering (hierarchyfiles.h) keeps 10002 .. 19999 for self children:
// parent p's is p + 10000.  Edits leave children whose name no longer says that:
//   - a parent is renumbered, sorted or merged into another: its self child
//     still carries the old parent's number;
//   - a split moves a parent's whole self child onto a new parent;
//   - a parent is left with one child of the free block (its children were
//     merged back into one).
// This renames, in ONE simultaneous map (the layer is relabelled row by row, so
// chains and swaps are fine as long as the result is one-to-one):
//   1. per real parent p, the child that becomes p's self child: the one already
//      named so; else the lowest misnamed child of the block under p; else p's
//      only child;
//   2. every other child with an id below 20000 that is not its parent's self
//      child takes a fresh id of the free block.
// Atoms that straddle parents keep their name (collapseToSelfChildren re-cuts
// them first), as do children of the free block that are one of several, the
// bins' own children 0 and 1, and reserved ids.  Membership never changes.
//
// NOT AN EDIT.  The labels are restored (Data::restoreClusterLabels), not
// edited: nothing lands on the child layer's undo stack, so the stack stays one
// level per ChildEdit.  Undoing the edit that made a name stale restores the
// table that edit started from, names included; a redo brings the stale name
// back, and undoChildEdit / redoChildEdit run this again.
//
// The ids held outside the Data -- the joint palette's children, the scope's sort
// order, the visible scope, the colours -- follow the same map.  Emits nothing
// and does not re-derive the parent<-child maps: the callers do, once.
// ---------------------------------------------------------------------------
bool KlustersDoc::normaliseChildIds(QMap<int, int>* renamed)
{
    if (renamed) renamed->clear();
    if (!childData || !clusteringData) return false;
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());

    QMap<int, int>        parentOf;          // child -> its parent (-1: straddles)
    QMap<int, QList<int>> childrenOfParent;           // parent -> its children, ascending
    for (int r = 1; r < n; ++r) {
        const int a = static_cast<int>(childByRow[r]);
        const int F = static_cast<int>(cluByRow[r]);
        const auto it = parentOf.find(a);
        if (it == parentOf.end()) parentOf.insert(a, F);
        else if (it.value() != F) it.value() = -1;
    }
    for (auto it = parentOf.constBegin(); it != parentOf.constEnd(); ++it)
        if (it.value() >= 0) childrenOfParent[it.value()].append(it.key());

    const auto renamable = [](int a) { return a > 1 && !isReservedClusterId(static_cast<dataType>(a)); };
    const auto misnamed  = [&](int a, int F) {          // an id below the free block that is not F's self child
        return renamable(a) && a < hierarchyfiles::kFreeChildBase && a != static_cast<int>(hierarchyfiles::selfChildOf(F));
    };

    QMap<int, int> rename;                    // child -> new id
    QSet<int>      selfTaken;                 // self ids that stay or are handed out
    // 1. who is each parent's self child
    for (auto it = childrenOfParent.constBegin(); it != childrenOfParent.constEnd(); ++it) {
        const int F = it.key();
        if (!hierarchyfiles::hasOffsetSelfChild(F)) continue;
        const int self = static_cast<int>(hierarchyfiles::selfChildOf(F));
        const QList<int>& children = it.value();
        if (children.contains(self)) { selfTaken.insert(self); continue; }
        // the id must be free once the map is applied: not held, or held by a child that is renamed away
        const auto holder = parentOf.constFind(self);
        if (holder != parentOf.constEnd() && holder.value() < 0) continue;       // held by a straddler
        int chosen = -1;
        for (const int a : children)
            if (misnamed(a, F)) { chosen = a; break; }                           // ascending: the lowest
        if (chosen < 0 && children.size() == 1 && renamable(children.first())) chosen = children.first();
        if (chosen < 0) continue;
        rename.insert(chosen, self);
        selfTaken.insert(self);
    }
    // 2. every other misnamed child leaves the blocks it has no claim to
    int next = static_cast<int>(childData->highestIdForAllocation());
    for (auto it = parentOf.constBegin(); it != parentOf.constEnd(); ++it) {
        const int a = it.key(), F = it.value();
        if (F < 0 || rename.contains(a) || !misnamed(a, F)) continue;
        rename.insert(a, ++next);
    }
    if (rename.isEmpty()) return false;

    // One-to-one, and onto ids no child that stays put holds: otherwise two children would be fused.
    QSet<int> targets;
    for (auto it = rename.constBegin(); it != rename.constEnd(); ++it) {
        const bool heldByStayer = parentOf.contains(it.value()) && !rename.contains(it.value());
        if (targets.contains(it.value()) || heldByStayer) {
            qWarning() << "[hierarchy] normaliseChildIds: the rename of child" << it.key() << "to" << it.value()
                       << "would meet another child; nothing was renamed.";
            return false;
        }
        targets.insert(it.value());
    }
    QVector<dataType> labels = childByRow;
    for (int r = 1; r < labels.size(); ++r) {
        const auto it = rename.constFind(static_cast<int>(labels[r]));
        if (it != rename.constEnd()) labels[r] = static_cast<dataType>(it.value());
    }
    childData->restoreClusterLabels(labels);
    if (renamed) *renamed = rename;

    const auto follow = [&rename](int id) { return rename.value(id, id); };
    if (childPrimaryOn) {                     // the stored child->parent map is keyed by child id
        QMap<int, int> rekeyed;
        for (auto it = shadowChildToParent.constBegin(); it != shadowChildToParent.constEnd(); ++it)
            rekeyed.insert(follow(it.key()), it.value());
        shadowChildToParent = rekeyed;
    }
    for (int& c : jointChildren)  c = follow(c);
    for (int& c : scopeSortOrder) c = follow(c);
    QSet<int> visible;
    for (const int c : std::as_const(childScopeVisible)) visible.insert(follow(c));
    childScopeVisible = visible;
    if (childColorList) {
        // Colours follow their child.  A swap needs every id moved before any lands, so the
        // colours are read first and written under the new ids after.
        QMap<int, QColor> moved;
        for (auto it = rename.constBegin(); it != rename.constEnd(); ++it) {
            if (!childColorList->contains(it.key())) continue;
            moved.insert(it.value(), childColorList->color(it.key()));      // by item id
            childColorList->remove(it.key());
        }
        for (auto it = moved.constBegin(); it != moved.constEnd(); ++it) {
            if (childColorList->contains(it.key())) childColorList->remove(it.key());
            childColorList->append(it.key(), it.value());
        }
        childColorList->sortByItemId();
    }
    return true;
}

void KlustersDoc::collapseToSelfChildren(){
    // The single hierarchy invariant: every parent is covered by its children, and a parent
    // with one child IS that child (the "self child", numbered parent + 10000 -- hierarchyfiles.h;
    // the bins' own children keep 0 and 1 -- the identity the
    // .clc==.clu lift establishes).  Any flat-layer edit -- a parent recluster, an artifact
    // recluster, a manual split, or a child recluster whose new children span parents -- leaves
    // "loose" spikes: in a parent (real OR reserve) but covered only by a child belonging to a
    // different parent, or by a child that now straddles several parents.  Collapse every loose spike
    // into its parent's self child, so each parent regains a covering child with no new parent
    // invented.  Atoms that are real (> 1) AND wholly inside one parent are deliberate
    // sub-structure and are preserved untouched.
    if (!childData) return;
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());

    // Per real child, count its spikes in each parent.  This distinguishes a wholly-
    // inside child (one parent) from a straddler, AND picks a straddler's "home" parent
    // -- the one holding the plurality of its spikes.  A manual polygon split clips
    // some of a parent's spikes into a new parent; if that clip crosses a deliberate
    // sub-cluster child, the old code dissolved that child into self children on BOTH
    // sides, so the source parent lost the child.  Keeping the straddler whole on its
    // home parent -- and collapsing only the clipped-off remainder into the new
    // parent's self child -- preserves the sub-cluster where it still mostly lives.
    const auto selfOf = [](int parent) { return static_cast<int>(hierarchyfiles::selfChildOf(parent)); };
    QHash<int, QHash<int,int>> childParentCount;               // real child -> parent -> spike count
    for (int r = 1; r < n; ++r){
        const int a = static_cast<int>(childByRow[r]);
        if (a <= 1) continue;
        childParentCount[a][static_cast<int>(cluByRow[r])] += 1;
    }
    QSet<int> intact;                                        // real children wholly inside one parent
    QHash<int,int> homeParent;                                // straddler -> parent to keep it whole on
    for (auto it = childParentCount.constBegin(); it != childParentCount.constEnd(); ++it){
        const QHash<int,int>& perParent = it.value();
        if (perParent.size() == 1){ intact.insert(it.key()); continue; }
        const int a = it.key();
        // A self child (child id == a parent id) stays on its own parent; any other child
        // stays on the parent holding the plurality of its spikes (ties -> lower parent
        // id == the pre-existing source, not the freshly minted split-off parent).
        const int own = static_cast<int>(hierarchyfiles::parentOfSelfChild(a));
        if (own >= 0 && perParent.contains(own)){ homeParent[a] = own; continue; }
        int best = -1, bestN = -1;
        for (auto f = perParent.constBegin(); f != perParent.constEnd(); ++f)
            if (f.value() > bestN || (f.value() == bestN && f.key() < best)){ bestN = f.value(); best = f.key(); }
        homeParent[a] = best;
    }

    // Decide, per row, whether it is loose and which child it collapses into.
    //
    // The reserve parents (0 artifact / 1 noise) are NOT skipped.  They used to be --
    // "noise/artifact parents keep their children" -- and that is what made a whole class
    // of straddler permanently unrepairable: a child whose plurality sits in a real
    // parent but which has a few spikes in noise has its noise-side rows skipped here
    // (reserve parent) and its real-side rows skipped below (home parent), so NOTHING
    // moves and the child still spans two parents.  repairNesting() is then a no-op on it,
    // which is why the same offender list is re-reported verbatim after every edit
    // while the warning keeps advising a repairNesting that cannot help.  The reserve
    // bins get the same self-child treatment as any other parent -- child id == parent
    // id, i.e. spikes dropped to noise are covered by child 1 and spikes dropped to
    // artifact by child 0 -- which is exactly the lift flattenHierarchyToClu() already
    // applies to every parent including the reserve bins, and for the same reason it
    // states there: a child left in a reserve bin carrying an arbitrary id is a
    // nesting violation waiting to happen.  A child lying WHOLLY inside a reserve bin
    // is still `intact` and is preserved untouched, so a sub-cluster deliberately
    // dropped to noise as a unit keeps its identity.
    // Decide, per row, whether it is loose and which child it collapses into.
    //
    // The reserve parents (0 artifact / 1 noise) are NOT skipped.  They used to be --
    // "noise/artifact parents keep their children" -- and that is what made a whole class
    // of straddler permanently unrepairable: a child whose plurality sits in a real
    // parent but which has a few spikes in noise has its noise-side rows skipped here
    // (reserve parent) and its real-side rows skipped below (home parent), so NOTHING
    // moves and the child still spans two parents.  repairNesting() is then a no-op on it,
    // which is why the same offender list is re-reported verbatim after every edit
    // while the warning keeps advising a repairNesting that cannot help.  The reserve
    // bins get the same self-child treatment as any other parent -- child id == parent
    // id, i.e. spikes dropped to noise are covered by child 1 and spikes dropped to
    // artifact by child 0 -- which is exactly the lift flattenHierarchyToClu() already
    // applies to every parent including the reserve bins, and for the same reason it
    // states there: a child left in a reserve bin carrying an arbitrary id is a
    // nesting violation waiting to happen.  A child lying WHOLLY inside a reserve bin
    // is still `intact` and is preserved untouched, so a sub-cluster deliberately
    // dropped to noise as a unit keeps its identity.

    // A child is KEPT if at least one of its rows survives the three skip rules
    // below -- it is intact, it is already its parent's self child, or it is a
    // straddler sitting on its home parent.  Looseness does not depend on the
    // targets, so this can be settled first, and it is what makes the target
    // choice safe in one pass.
    // Same pass also records, per real parent, WHICH source children feed it loose
    // rows.  A parent fed by several sources fragments into one descendant per
    // source (see targetFor), so the count has to be complete before any target
    // is handed out.
    QSet<int> keptChild;
    QHash<int, QSet<int>> looseSources;
    for (int r = 1; r < n; ++r){
        const int F = static_cast<int>(cluByRow[r]);
        const int a = static_cast<int>(childByRow[r]);
        if ((a > 1 && intact.contains(a)) || a == selfOf(F)
                || (a > 1 && homeParent.value(a, -1) == F))
            keptChild.insert(a);
        else if (F > 1)
            looseSources[F].insert(a);
    }

    // Fresh child ids start above every id either layer currently uses, so they
    // cannot collide with a surviving child or with another parent's target --
    // except the reserved chunk ids, which sit at the top of the float-exact range:
    // counting them would number fresh children past 2^24.  Below the block, a fresh
    // id meets a reserved one only after 16 million children.
    int nextFreeChild = 1;
    for (int r = 1; r < n; ++r){
        if (!isReservedClusterId(childByRow[r]))
            nextFreeChild = qMax(nextFreeChild, static_cast<int>(childByRow[r]));
        if (!isReservedClusterId(cluByRow[r]))
            nextFreeChild = qMax(nextFreeChild, static_cast<int>(cluByRow[r]));
    }

    // (source child, parent) -> the child that pair's loose rows collapse into.
    //
    // Keyed by the SOURCE as well as the parent, so a cut that crosses several
    // children yields one descendant per crossed child rather than fusing them all
    // into one covering child.  A row's child is its provenance -- which of the
    // over-split units it came from -- and the parent it lands in says nothing
    // about that, so collapsing rows from children 7, 12 and 30 into a single new
    // child discards information the child layer exists to carry.  Smaller children
    // that can be merged deliberately are preferable to a merge performed on the
    // user's behalf, which is not reversible without knowing what was fused.
    //
    // The parent keeps its own id as the self child only when a SINGLE source
    // feeds it and nothing already carries that id: that is the ordinary split of
    // one parent in two, and it behaves exactly as before.  When several sources
    // feed it, every descendant takes a fresh id -- the parent then has more than
    // one child, so the "a parent with one child IS that child" convention does not
    // apply to it anyway and the normalisation pass below leaves it alone.
    //
    // Why a fresh id when a child carrying F survives: if that survivor sits under
    // another parent, writing F would make it span two -- the repair minting a
    // straddler, which is why one pass was not a fixed point.  If it sits under
    // parent F itself, the child would silently absorb spikes that were never part
    // of it, which is the same silent-substitution failure in the child layer.
    //
    // The reserve bins are excluded from all of this: child 0 and child 1 ARE the
    // artifact and noise self children, membership inside them is not curated
    // structure, and fragmenting noise per source child would scatter it across
    // ever more ids for no gain.
    // ... and never below the block of ids kept for children that are not self children.
    nextFreeChild = qMax(nextFreeChild, hierarchyfiles::kFreeChildBase - 1);
    QHash<int, QHash<int,int>> splitTarget;
    auto targetFor = [&](int a, int F) -> int {
        if (F <= 1) return F;
        QHash<int,int>& perSource = splitTarget[F];
        const auto cached = perSource.constFind(a);
        if (cached != perSource.constEnd()) return cached.value();
        const bool soleSource = (looseSources.value(F).size() == 1);
        const int self = selfOf(F);
        const int t = (soleSource && !keptChild.contains(self)) ? self : ++nextFreeChild;
        perSource.insert(a, t);
        return t;
    };

    // Build the whole re-cut labelling, then commit it in ONE call.  The previous
    // form issued a moveSpikeSubset per (source child, target child) pair, and each of
    // those rebuilds the entire row table, revalidates it and pushes its own undo
    // level.  That is fine when a handful of children straddle, but the child layer this
    // view exists for is deliberately over-split -- fiber_stochastic produces tiny
    // children -- so a curation gesture that clips a parent clips hundreds of distinct
    // children, and the pair count follows.  Measured on a synthetic 574,121-spike
    // session with 1,500 parents and 22,000 children: 2,085 rebuilds, ~1.2e9 row writes,
    // in the GUI thread.  setClusterLabels does one rebuild for the whole re-cut --
    // the same primitive flattenHierarchyToClu() already uses, for the same reason
    // -- taking that session to a single 574,121-row pass.
    //
    // It also improves the undo picture rather than harming it.  This function
    // records no ChildEdit, so every Data undo level it pushed was unmatched by an
    // entry on the child stack; N unmatched levels become one.  repairNesting() clears
    // both stacks afterwards regardless.
    // The re-cut decision, in ONE place.
    //
    // It ran twice: once against the labels on entry, and again verbatim against
    // the healed labels after a refusal.  The three skip rules ARE the policy this
    // function implements -- preserve a deliberate sub-cluster, leave a self child
    // alone, keep a straddler whole on its home parent -- and having them written
    // out twice means a change to any of them has to be made in both, with nothing
    // to say so.  That is how the cluster-1 column bug survived in the grouping
    // assistant, in a file where the stakes are lower than here.
    //
    // Source and bound differ between the two calls; nothing else does, which was
    // confirmed by normalising the two loops and comparing them.
    const auto buildRecut = [&](const QVector<dataType>& source, int upTo,
                                QVector<dataType>& out) -> bool {
        out = source;
        bool any = false;
        for (int r = 1; r < upTo; ++r){
            const int F = static_cast<int>(cluByRow[r]);
            const int a = static_cast<int>(source[r]);
            if (a > 1 && intact.contains(a)) continue;           // deliberate sub-cluster, preserved
            if (a == selfOf(F)) continue;                        // already its own self child
            if (a > 1 && homeParent.value(a, -1) == F) continue;  // straddler whole on its home parent
            const int t = targetFor(a, F);                       // this source's share of the cut
            if (t != a){ out[r] = static_cast<dataType>(t); any = true; }
        }
        return any;
    };

    QVector<dataType> newLabels;
    const bool changed = buildRecut(childByRow, n, newLabels);
    // Nothing loose: do not commit.  setClusterLabels would push an undo level and
    // invalidate every waveform/correlogram cache for a no-op re-cut, and this
    // function runs on paths that call it speculatively.
    //
    // A refusal means the child layer's map was desynced on entry, so `newLabels` was
    // derived from an incomplete view; setClusterLabels has repaired the map, and the
    // labelling has to be rebuilt against it.  Re-derive once and retry -- the second
    // read is against a map that has just been made consistent, so this cannot loop.
    if (changed && !childData->setClusterLabels(newLabels)) {
        qWarning() << "[hierarchy] collapseToSelfChildren: the atom layer's cluster map was"
                   << "desynced; it has been repaired and the re-cut is being redone against"
                   << "the repaired view.";
        const QVector<dataType> healed = childData->labelByFeatureRow();
        const int m = qMin(cluByRow.size(), healed.size());
        QVector<dataType> retry;
        if (buildRecut(healed, m, retry)) childData->setClusterLabels(retry);
    }

    // ── Normalize the self-child naming ────────────────────────────────────────
    // After the collapse a parent can be left covered by a single child whose id is not
    // the parent's own -- e.g. a split moved a parent's entire self child onto a new
    // parent, so the source parent's sole remaining child is a deliberate sub-cluster
    // while the new parent's sole child still carries the source parent's id.  The maps
    // stay correct (a child's parent is derived from its spikes), but the convention
    // "a parent with one child IS that child, child id == parent + 10000" breaks, and the
    // stray id can later collide with a freshly minted parent.  Rename each such sole
    // child to its parent's id so the self-child identity holds again.  (A parent with
    // two or more children is genuine sub-structure and is left untouched.)
    // The names: every parent's self child carries parent + 10000, and no other child
    // carries an id of that block (normaliseChildIds).
    normaliseChildIds();
    if (changed) {
        beginGesture(QStringLiteral("collapseToSelfChildren"), {{ QStringLiteral("rows"), n }});
        endGesture();
    }

    syncChildColors();
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    // Publish what the re-cut decided, so the child-primary map records the SAME
    // answer rather than reconstructing it.
    //
    // splitTarget[F][a] is the target child minted for source child `a` on parent F.
    // That is the complete decision -- which side keeps the original id and which
    // gets a fresh one -- and it is made here, with the coverage table in hand.
    // Predicting it from outside means implementing the plurality rule, the
    // self-child rule and the sole-source rule a second time, which is how a shared
    // policy starts drifting.  Consuming it means the map cannot disagree with the
    // split, because it is using the split's own output.

}

void KlustersDoc::repairNesting(){
    // Deliberate full resync of the child layer to the parent layer, independent of the parent
    // and child undo timelines: collapse loose spikes to self children (above) and reset the
    // child-edit history since the child structure has just been re-cut.  rebuildHierarchyFromData
    // (inside collapseToSelfChildren) re-derives childToParent / parentToChildren, which the
    // next Save writes out as the regenerated .clp.
    collapseToSelfChildren();
    // The undo history is kept.  It used to be dropped here -- silently, on every parent
    // split, delete, watershed and recluster -- because the re-cut pushes a level on
    // childData's own history that no child entry stood for.  Entries now restore the
    // labelling they hold, so that level is nobody's concern.
}

int KlustersDoc::mergeParents(const QList<int>& parentsIn, KlustersView& activeView){
    // Left out before anything reads the list: a reserved chunk parent folded into
    // the survivor below would have its children reparented without its spikes.
    QList<int> parents = parentsIn;
    leaveOutReserved(parents, tr("Merge parents"));
    if (parents.size() < 2) return -1;
    setActiveClustering(false);                   // edits always target the parent
    const int kept = groupClusters(parents, activeView);   // existing: mutate + undo + views
    if (kept < 0) return -1;

    // Child-primary: every child of a folded parent is reparented onto the survivor.
    // No child is split or created -- a parent merge is a pure map operation, which
    // is the clearest case of the O(children) versus O(nSpikes) difference.
    if (childPrimaryOn)
        for (int f : parents)
            if (f != kept)
                for (int c : parentToChildren.value(f))
                    shadowChildToParent.insert(c, kept);

    noteModifiedParent(kept);                              // merged parent needs realign
    setPendingParentSelection({kept});                    // land on the merged parent
    refreshChildrenAfterParentMerge();
    return kept;
}

void KlustersDoc::refreshChildrenAfterParentMerge(){
    if (!childData) return;
    normaliseChildIds();                                  // the folded parents' self children are no longer anyone's
    syncChildColors();
    rebuildHierarchyFromData();
    emit hierarchyChanged();
}

// ---------------------------------------------------------------------------
// KlustersDoc::promoteChildren
//
// Take children out of their parent and make ONE new parent from them.  With a
// single child this is the old promoteChild; with several it is the old
// groupChildrenIntoFiber.  They were the same operation written twice -- one
// N-ary, one unary with an extra early return -- so they are one function now.
//
// The only-child case still short-circuits: a child that is its parent's sole
// child already covers exactly that parent, so promoting it would move every spike
// to a new id for no change in structure.
// ---------------------------------------------------------------------------
int KlustersDoc::promoteChildren(const QList<int>& childrenIn, KlustersView& activeView){
    if (!childData || childrenIn.isEmpty()) return -1;

    // The children of a reserved chunk parent stay where they are: taking them out
    // would take spikes out of that parent other than by reclustering or a
    // feature-space selection.
    QList<int> children = childrenIn;
    leaveOutChildrenOfReserved(children, tr("Take atoms out of their parent"));
    if (children.isEmpty()) return -1;

    if (children.size() == 1){
        const int only = children.first();
        if (!childToParent.contains(only)) return -1;
        const int parentId = childToParent.value(only);
        if (parentToChildren.value(parentId).size() <= 1)
            return parentId;                        // already its own parent
    }

    setActiveClustering(false);
    const int newId = static_cast<int>(clusteringData->nextFreeClusterId());
    // Inside another command (Dissolve, Break Up) the promotion is that command's
    // mechanics, not a judgement on this child.
    const bool ownDecision = !(curationLogger && curationLogger->isOpen() && curationLogger->inGesture());
    beginGesture(QStringLiteral("promoteChildren"), {{ QStringLiteral("children"), idList(children) },
                                                      { QStringLiteral("new_parent"), newId }});
    if (ownDecision) logChildSeparation(children);
    int target = -1;
    for (int c : children){
        if (!childToParent.contains(c)) continue;
        const int parentId = childToParent.value(c);
        if (parentId == newId) continue;                // already pooled
        const QVector<int> spk = childData->clusterSpkIndices(c);
        if (spk.isEmpty()) continue;
        const int dest = (target < 0 ? newId : target);
        moveSpikeSubsetToCluster(parentId, spk, dest, activeView);

        // Child-primary: one map entry per promoted child.  Their spikes keep their
        // child ids -- only the parent changes -- so this is a reparent, matching
        // what the relabel above does to the .clu of those rows.

        if (target < 0){
            target = newId;
            // Coloured after the first move so a Ctrl+Z reverting that move also
            // drops the colour.
            if (!clusterColorList->contains(newId)){
                QColor color;
                color.setHsv(static_cast<int>(fmod(static_cast<double>(newId) * 7, 36)) * 10, 200, 255);
                clusterColorList->append(newId, color);
            }
        }
    }
    if (target < 0) { endGesture({{ QStringLiteral("outcome"), QStringLiteral("nothing moved") }}); return -1; }
    clusterPalette.updateClusterList();
    normaliseChildIds();                    // a lone promoted child becomes the new parent's self child
    syncChildColors();
    rebuildHierarchyFromData();
    setPendingParentSelection({target});   // the output is a FIBER; land there
    emit hierarchyChanged();
    endGesture({{ QStringLiteral("produced"), target }});
    return target;
}

bool KlustersDoc::dissolveParent(int parentId, KlustersView& activeView){
    if (!childData) return false;
    QList<int> asked{ parentId };
    if (leaveOutReserved(asked, tr("Dissolve parent")) > 0) return false;
    const QList<int> kids = parentToChildren.value(parentId);
    if (kids.size() < 2) return false;                // nothing to explode
    // Each child becomes its own parent: promoteChildren with a single-element
    // list per child, not one call with all of them, which would pool them into
    // one parent and be the opposite operation.
    beginGesture(QStringLiteral("dissolveParent"), {{ QStringLiteral("parent"), parentId },
                                                     { QStringLiteral("children"), idList(kids) }});
    QList<int> produced;
    for (int c : kids)
        produced.append(promoteChildren(QList<int>{c}, activeView));   // each a nested gesture
    setPendingParentSelection(produced);               // select all the exploded parents
    endGesture({{ QStringLiteral("produced"), idList(produced) }});
    return true;
}

bool KlustersDoc::dropChildToNoise(int childCluster, KlustersView& activeView){
    return dropChildrenToBin(QList<int>{ childCluster }, 1, activeView);
}

bool KlustersDoc::dropChildrenToBin(const QList<int>& childrenIn, int binId, KlustersView& activeView){
    if (!childData || (binId != 0 && binId != 1)) return false;
    const QString opName = (binId == 1) ? tr("Drop child to noise") : tr("Drop child to artifact");

    // Group the children by parent.  The reserve children (0, 1) are the self children
    // of the bins and never move; a child whose parent IS the bin is already there.
    QMap<int, QList<int>> byParent;
    for (int c : childrenIn) {
        if (c <= 1) continue;
        const int p = childToParent.value(c, -1);
        if (p < 0 || p == binId) continue;
        if (!byParent[p].contains(c)) byParent[p].append(c);
    }
    {
        QList<int> parents = byParent.keys();
        leaveOutReserved(parents, opName);               // a reserved chunk cluster keeps its children
        for (int p : byParent.keys())
            if (!parents.contains(p)) byParent.remove(p);
    }
    if (byParent.isEmpty()) return false;

    // The landing is computed BEFORE the move, from the maps as they stand: the
    // anchor is the parent of the lowest dropped child; its surviving siblings are
    // the candidates in the child palette; its neighbour among the parents that
    // are not themselves being emptied is the fallback when it goes away.
    QList<int> dropped;
    for (const QList<int>& kids : byParent) dropped += kids;
    std::sort(dropped.begin(), dropped.end());
    const int anchorChild   = dropped.first();
    const int anchorParent = childToParent.value(anchorChild);
    // The surviving siblings in the order the child palette LISTS them: the held
    // child-scope sort order when one is in force (a sorted list's "next" is the
    // next row, not the next id), else by id.  The landing is the next survivor
    // after the anchor in that order; when the anchor was the last row, the one
    // before it -- so a delete at the end of the list lands on the new last row.
    QList<int> siblings;
    for (int kid : parentToChildren.value(anchorParent))
        if (kid > 1 && !dropped.contains(kid)) siblings.append(kid);
    std::sort(siblings.begin(), siblings.end());
    auto rowOf = [&](int id) -> int {                    // position in the listed order
        const int i = scopeSortOrder.indexOf(id);
        return i >= 0 ? i : scopeSortOrder.size() + id;  // unsorted ids follow, by id
    };
    if (!scopeSortOrder.isEmpty())
        std::sort(siblings.begin(), siblings.end(), [&](int a, int b) { return rowOf(a) < rowOf(b); });
    int siblingLanding = -1;
    if (!siblings.isEmpty()) {
        const int anchorRow = scopeSortOrder.isEmpty() ? anchorChild : rowOf(anchorChild);
        siblingLanding = siblings.last();                // nothing after: the nearest before
        for (int kid : siblings) {
            const int row = scopeSortOrder.isEmpty() ? kid : rowOf(kid);
            if (row > anchorRow) { siblingLanding = kid; break; }
        }
    }
    // Parent neighbour: nearest above anchorParent, else nearest below, among the
    // parents that keep at least one child after this drop.
    int parentLanding = -1;
    {
        QList<int> survivors;
        for (dataType f : clusteringData->clusterIds()) {
            const int id = static_cast<int>(f);
            if (id <= 1 || id == anchorParent) continue;
            if (byParent.contains(id)) {                 // emptied if every child of it goes
                bool keeps = false;
                for (int kid : parentToChildren.value(id))
                    if (!dropped.contains(kid)) { keeps = true; break; }
                if (!keeps) continue;
            }
            survivors.append(id);
        }
        if (!survivors.isEmpty()) {
            std::sort(survivors.begin(), survivors.end());
            parentLanding = survivors.last();
            for (int id : survivors)
                if (id > anchorParent) { parentLanding = id; break; }
        }
    }

    setActiveClustering(false);                          // the move is a PARENT-layer edit
    QVariantMap parentsDetail;
    for (auto it = byParent.constBegin(); it != byParent.constEnd(); ++it)
        parentsDetail.insert(QString::number(it.key()), idList(it.value()));
    const bool ownDecision = !(curationLogger && curationLogger->isOpen() && curationLogger->inGesture());
    beginGesture(QStringLiteral("dropChildrenToBin"), {{ QStringLiteral("children"), idList(dropped) },
                                                        { QStringLiteral("bin"), binId },
                                                        { QStringLiteral("parents"), parentsDetail }});
    if (ownDecision) logChildSeparation(dropped);
    int nMoved = 0;
    for (auto it = byParent.constBegin(); it != byParent.constEnd(); ++it) {
        QVector<int> spk;
        for (int c : it.value()) spk += childData->clusterSpkIndices(c);
        if (spk.isEmpty()) continue;
        // Child-primary: the children are RECLASSIFIED, not relabelled.  Noise and
        // artifact are parent-layer classifications -- a child is a sub-cluster of
        // a unit and has no signal/noise character of its own -- so dropping it is
        // exactly "its parent is now the bin".  moveSpikeSubsetToCluster changes the
        // .clu of those rows and leaves the .clc alone, which says the same thing.
        moveSpikeSubsetToCluster(it.key(), spk, binId, activeView);
        nMoved += spk.size();
    }
    endGesture({{ QStringLiteral("n_spikes"), nMoved },
                { QStringLiteral("sibling_landing"), siblingLanding },
                { QStringLiteral("parent_landing"), parentLanding }});
    if (nMoved == 0) return false;

    rebuildHierarchyFromData();

    // Land.  The parent palette's landing is parked for applyPendingParentSelection
    // (the settled point after the deferred realign/renumber), the child palette's
    // for repopulateChildPalette, which drains it on whichever rebuild turns out
    // to be last.  A parked child landing also tells applyPendingParentSelection
    // to leave the shown clustering and the focus with the child palette.
    bool parentAlive = false;
    for (dataType f : clusteringData->clusterIds())
        if (static_cast<int>(f) == anchorParent) { parentAlive = true; break; }
    QList<int> land;
    if (parentAlive) {
        setPendingParentSelection({anchorParent});       // stay on the source parent
        if (siblingLanding > 1) {
            land = QList<int>{ siblingLanding };
            setPendingChildSelection(land);
        }
    } else if (parentLanding > 1) {
        // The last child went and the parent with it: one level up, to the parent's
        // neighbour.  The curated parent has just ceased to exist -- the one
        // situation besides a user selection in which it may move -- so it follows,
        // and the child palette and the matrices land there together instead of
        // pointing at a dead parent.  A joint scope drops the dead parent instead:
        // nextFreeClusterId() can hand its number to a future parent, which a held
        // stale entry would silently adopt.
        setPendingParentSelection({parentLanding});
        if (curatedParent() == anchorParent) setCuratedParent(parentLanding);
        if (curatedParents.contains(anchorParent)) {
            QList<int> held = curatedParents;
            held.removeAll(anchorParent);
            setCuratedParents(held);
        }
    }

    emit hierarchyChanged();

    // Belt and braces when no rebuild runs.
    if (!land.isEmpty()) emit hierarchyChildSelectionRequested(land);
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

namespace {

/**Pearson correlation between two equal-length waveform vectors.  Pearson, not
* a distance: an orphan is the SAME unit as its parent if its shape matches,
* whatever its amplitude -- a spike caught on the edge of a drifting cluster is
* smaller, not different.  Returns -2 when either side is flat (no opinion).*/
double waveformCorrelation(const std::vector<double>& a,
                           const std::vector<double>& b)
{
    if (a.size() != b.size() || a.size() < 2) return -2.0;
    const size_t n = a.size();
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= static_cast<double>(n);
    mb /= static_cast<double>(n);
    double num = 0.0, da = 0.0, db = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double x = a[i] - ma, y = b[i] - mb;
        num += x * y; da += x * x; db += y * y;
    }
    if (da <= 0.0 || db <= 0.0) return -2.0;
    return num / std::sqrt(da * db);
}

}   // namespace

int KlustersDoc::mergeOrphanChildren(int parentId, int minSpikes,
                                     double minCorrelation,
                                     KlustersView& activeView,
                                     int* matchedOut, int* strayOut)
{
    if (matchedOut) *matchedOut = 0;
    if (strayOut)   *strayOut   = 0;
    if (!childData || parentId < 0 || minSpikes < 1)
        return -1;

    const QList<int> kids = childrenOf(QList<int>{ parentId });
    if (kids.size() < 2)
        return -1;

    // Templates are the mean waveform per cluster, built in one sequential pass
    // and kept incrementally; this is the same cache the merge-recommendation
    // scan reads, so an orphan sweep costs nothing extra after the first call.
    childData->buildMissingClusterTemplates();

    // The orphans are the children up for merging; every other child of the
    // parent is ESTABLISHED, and they all go into the reference.
    QList<int> orphans, established;
    for (int c : kids) {
        const dataType n = childData->nbSpikesInCluster(c);
        if (n > 0 && n < minSpikes) orphans.append(c);
        else if (n > 0)             established.append(c);
    }
    if (orphans.isEmpty() || established.isEmpty())
        return -1;      // nothing to gather, or nothing to judge against

    // The reference: a per-point MEDIAN across the established children's mean
    // waveforms.  Median across siblings, not a mean over all the parent's
    // spikes -- one sibling that is actually a different unit (the usual reason
    // a parent was split in the first place) would drag a mean toward itself,
    // and the orphans are then judged against a shape that belongs to nobody.
    // With a single established child its own mean IS the median.
    std::vector<std::vector<double>> refs;
    std::vector<double> sd;
    for (int c : established) {
        std::vector<double> mean;
        if (childData->clusterTemplateFor(c, mean, sd) && !mean.empty())
            refs.push_back(std::move(mean));
    }
    if (refs.empty())
        return -1;
    const size_t len = refs.front().size();
    for (const auto& r : refs)
        if (r.size() != len) return -1;
    std::vector<double> reference(len, 0.0);
    std::vector<double> column(refs.size(), 0.0);
    for (size_t i = 0; i < len; ++i) {
        for (size_t j = 0; j < refs.size(); ++j) column[j] = refs[j][i];
        std::sort(column.begin(), column.end());
        const size_t m = column.size() / 2;
        reference[i] = (column.size() % 2) ? column[m]
                                           : 0.5 * (column[m - 1] + column[m]);
    }

    // Score each orphan against the reference.  An orphan with no template --
    // it can happen if its spikes were not readable -- is left out of both
    // counts: no waveform, no opinion.
    QList<int> matched;
    int stray = 0;
    QVariantList scored;
    for (int c : orphans) {
        std::vector<double> mean;
        if (!childData->clusterTemplateFor(c, mean, sd) || mean.size() != len)
            continue;
        const double r = waveformCorrelation(mean, reference);
        if (r <= -2.0) continue;
        if (r >= minCorrelation) matched.append(c);
        else                     ++stray;
        scored << QVariantMap{{ QStringLiteral("orphan"), c }, { QStringLiteral("r"), r },
                              { QStringLiteral("spikes"), static_cast<int>(childData->nbSpikesInCluster(c)) }};
    }
    if (matchedOut) *matchedOut = static_cast<int>(matched.size());
    if (strayOut)   *strayOut   = stray;
    beginGesture(QStringLiteral("mergeOrphanChildren"),
                 {{ QStringLiteral("parent"), parentId }, { QStringLiteral("min_spikes"), minSpikes },
                  { QStringLiteral("min_correlation"), minCorrelation },
                  { QStringLiteral("established"), idList(established) }, { QStringLiteral("orphans"), scored },
                  { QStringLiteral("matched"), idList(matched) }, { QStringLiteral("stray"), stray }});

    // The matching orphans are merged into ONE child (mergeChildren: one child
    // undo step); a group of one is already its own child, so merging it would
    // be a no-op edit.  The orphans that do not match are not merged at all:
    // each stays its own child for the curator to judge.  Merging them together
    // would make one child of every shape that failed the test, and the stricter
    // the threshold, the more of them -- at r 0.99 nearly all the orphans.
    if (matched.size() < 2 || mergeChildren(matched, activeView) < 0) {
        endGesture({{ QStringLiteral("outcome"), QStringLiteral("nothing merged") }});
        return 0;
    }
    endGesture({{ QStringLiteral("merged"), static_cast<int>(matched.size()) }});
    return static_cast<int>(matched.size());
}

// ---------------------------------------------------------------------------
// KlustersDoc::planBreakUpParents / breakUpParents
//
// Break a parent up and send each child, whole, to the large parent whose
// template its own mean waveform matches best.  A parent that holds children of
// several units -- the usual reason to break it up -- gives each child back to
// the unit it belongs to; the children that match no large parent become parents
// of their own, as Dissolve Parent makes them, the largest keeping the id.
// The targets are the parents with MORE than the given spike count, other than
// those being broken up: a small parent is no reliable unit to hand children to.
// ---------------------------------------------------------------------------
QList<KlustersDoc::BreakUpMove> KlustersDoc::planBreakUpParents(
        const QList<int>& parentsIn, int minTargetSpikes, double minCorrelation, int* targetsOut)
{
    if (targetsOut) *targetsOut = 0;
    QList<BreakUpMove> plan;
    if (!clusteringData || !childData)
        return plan;
    const auto inPlay = [](int c) { return c > 1 && !isReservedClusterId(c); };
    QList<int> parents;                                  // ascending, whatever the selection order
    for (int p : parentsIn)
        if (inPlay(p) && !parents.contains(p) && !childrenOf(QList<int>{ p }).isEmpty())
            parents.append(p);
    if (parents.isEmpty())
        return plan;
    std::sort(parents.begin(), parents.end());

    // The same template caches the orphan commands read, on both layers.
    clusteringData->buildMissingClusterTemplates();
    childData->buildMissingClusterTemplates();

    QMap<int, std::vector<double>> templates;           // ascending id
    std::vector<double> sd;
    for (dataType id : clusteringData->clusterIds()) {
        const int c = static_cast<int>(id);
        if (!inPlay(c) || parents.contains(c)) continue;
        if (clusteringData->nbSpikesInCluster(c) <= static_cast<dataType>(std::max(0, minTargetSpikes))) continue;
        std::vector<double> mean;
        if (clusteringData->clusterTemplateFor(c, mean, sd) && !mean.empty())
            templates.insert(c, std::move(mean));
    }
    if (targetsOut) *targetsOut = static_cast<int>(templates.size());

    for (int p : parents) {
        QList<BreakUpMove> mine;
        for (int c : childrenOf(QList<int>{ p })) {
            BreakUpMove m;
            m.child = c;
            m.parent = p;
            m.spikes = static_cast<int>(childData->nbSpikesInCluster(c));
            std::vector<double> mean;
            if (childData->clusterTemplateFor(c, mean, sd) && !mean.empty()) {
                for (auto it = templates.constBegin(); it != templates.constEnd(); ++it) {
                    const double r = waveformCorrelation(mean, it.value());
                    if (r > m.r) { m.r = r; m.best = it.key(); }
                }
            }
            if (m.best >= 0 && m.r >= minCorrelation)
                m.target = m.best;
            mine.append(m);
        }
        int keeper = -1;
        for (int i = 0; i < mine.size(); ++i)
            if (mine[i].target < 0 && (keeper < 0 || mine[i].spikes > mine[keeper].spikes))
                keeper = i;
        if (keeper >= 0)
            mine[keeper].keepsParentId = true;
        plan += mine;
    }
    return plan;
}

QList<int> KlustersDoc::breakUpParents(const QList<BreakUpMove>& plan, KlustersView& activeView)
{
    QList<int> landed;
    if (!childData || plan.isEmpty())
        return landed;
    QList<int> order;                                    // parents, in plan order
    for (const BreakUpMove& m : plan)
        if (!order.contains(m.parent)) order.append(m.parent);
    {   // the plan, move by move: child, its parent, the best-matching parent, the
        // correlation, and where it goes -- the template decision the log would
        // otherwise only show as unexplained MOVE_SPIKES
        QVariantList moves;
        for (const BreakUpMove& m : plan)
            moves << QVariantMap{{ QStringLiteral("child"), m.child }, { QStringLiteral("parent"), m.parent },
                                 { QStringLiteral("spikes"), m.spikes }, { QStringLiteral("best"), m.best },
                                 { QStringLiteral("r"), m.r }, { QStringLiteral("target"), m.target },
                                 { QStringLiteral("keeps_parent_id"), m.keepsParentId }};
        beginGesture(QStringLiteral("breakUpParents"), {{ QStringLiteral("parents"), idList(order) },
                                                         { QStringLiteral("moves"), moves }});
    }
    for (int p : order) {
        QList<int> asked{ p };
        if (leaveOutReserved(asked, tr("Break up parent")) > 0) continue;
        // One move per target: the spikes of every child going there, which keep
        // their child ids (moveSpikeSubsetToCluster relabels the parent layer only,
        // and whole children leave nothing straddling).
        QMap<int, QVector<int>> spikesByTarget;
        QList<int> lone;
        for (const BreakUpMove& m : plan) {
            if (m.parent != p) continue;
            if (m.target >= 0) spikesByTarget[m.target] += childData->clusterSpkIndices(m.child);
            else if (!m.keepsParentId) lone.append(m.child);
        }
        setActiveClustering(false);
        for (auto it = spikesByTarget.constBegin(); it != spikesByTarget.constEnd(); ++it) {
            if (it.value().isEmpty()) continue;
            moveSpikeSubsetToCluster(p, it.value(), it.key(), activeView);
            if (!landed.contains(it.key())) landed.append(it.key());
        }
        // The children without a target, but the one keeping the parent's id, each
        // become a parent: promoteChildren with one child at a time, as Dissolve
        // Parent does.  The keeper stays, so the parent never runs out under them.
        for (int c : lone) {
            const int made = promoteChildren(QList<int>{ c }, activeView);
            if (made >= 0 && !landed.contains(made)) landed.append(made);
        }
        if (clusteringData->nbSpikesInCluster(p) > 0 && !landed.contains(p))
            landed.append(p);
    }
    rebuildHierarchyFromData();
    setPendingParentSelection(landed);
    emit hierarchyChanged();
    endGesture({{ QStringLiteral("landed"), idList(landed) }});
    return landed;
}

// ---------------------------------------------------------------------------
// KlustersDoc::planOrphanParents / mergeOrphanParents
//
// The parent-layer counterpart of mergeOrphanChildren.  A parent has no parent
// to judge its tiny siblings against, so each tiny parent is matched to the
// templates of the parents that are NOT being merged, and the ones matching the
// same parent are merged into one new parent BESIDE it: that parent is not
// touched, so a wrong match costs nothing but an undo, and folding the new
// parent into it is the curator's call.  A tiny parent matching none stays as
// it is.  Planned and applied in two steps so the app can show the plan before
// anything changes.
// ---------------------------------------------------------------------------
QList<KlustersDoc::OrphanParentGroup> KlustersDoc::planOrphanParents(
        const QList<int>& candidates, int minSpikes, double minCorrelation,
        int* orphansOut, int* templatesOut, QList<OrphanMatch>* unmatchedOut)
{
    if (orphansOut)   *orphansOut   = 0;
    if (templatesOut) *templatesOut = 0;
    if (unmatchedOut) unmatchedOut->clear();
    QList<OrphanParentGroup> plan;
    if (!clusteringData || minSpikes < 1)
        return plan;

    // Not the artifact and noise clusters, and not a reserved chunk cluster: a
    // chunk's unsorted spikes are no unit to match against, and never merged.
    const auto inPlay = [](int c) { return c > 1 && !isReservedClusterId(c); };
    const dataType threshold = static_cast<dataType>(minSpikes);
    const QList<dataType> ids = clusteringData->clusterIds();

    // The ORPHANS -- the parents being merged -- first: they decide what the
    // templates are.
    QList<int> pool = candidates;
    if (pool.isEmpty())
        for (dataType id : ids) pool.append(static_cast<int>(id));
    std::sort(pool.begin(), pool.end());
    pool.erase(std::unique(pool.begin(), pool.end()), pool.end());
    QList<int> orphans;
    QSet<int> isOrphan;
    for (int c : pool) {
        if (!inPlay(c)) continue;
        const dataType n = clusteringData->nbSpikesInCluster(c);
        if (n < 1 || n >= threshold) continue;
        orphans.append(c);
        isOrphan.insert(c);
    }
    if (orphansOut) *orphansOut = static_cast<int>(orphans.size());
    if (orphans.isEmpty())
        return plan;

    // The same incremental template cache mergeOrphanChildren reads, on the
    // parent layer: one sequential .spk pass the first time, nothing after.
    clusteringData->buildMissingClusterTemplates();

    // Every parent that is not being merged is a template, whatever its size
    // and whether it is selected: an orphan is compared with all of them, and
    // never with another orphan.
    QMap<int, std::vector<double>> templates;           // ascending id
    std::vector<double> sd;
    for (dataType id : ids) {
        const int c = static_cast<int>(id);
        if (!inPlay(c) || isOrphan.contains(c) || clusteringData->nbSpikesInCluster(c) < 1) continue;
        std::vector<double> mean;
        if (clusteringData->clusterTemplateFor(c, mean, sd) && !mean.empty())
            templates.insert(c, std::move(mean));
    }
    if (templatesOut) *templatesOut = static_cast<int>(templates.size());

    QMap<int, OrphanParentGroup> matched;                // keyed by template, ascending
    for (int c : orphans) {
        std::vector<double> mean;
        if (!clusteringData->clusterTemplateFor(c, mean, sd) || mean.empty())
            continue;                                    // no waveform, no opinion
        OrphanMatch m;
        m.orphan = c;
        for (auto it = templates.constBegin(); it != templates.constEnd(); ++it) {
            if (it.value().size() != mean.size()) continue;
            const double r = waveformCorrelation(mean, it.value());
            if (r > m.r) { m.r = r; m.best = it.key(); }  // a tie keeps the lower id
        }
        if (m.best >= 0 && m.r >= minCorrelation) {
            OrphanParentGroup& g = matched[m.best];
            g.templateParent = m.best;
            g.members.append(m);
        } else if (unmatchedOut) {
            unmatchedOut->append(m);                     // reported, never merged
        }
    }

    for (const OrphanParentGroup& g : matched)
        plan.append(g);
    return plan;
}

QList<int> KlustersDoc::mergeOrphanParents(const QList<OrphanParentGroup>& plan,
                                           KlustersView& activeView)
{
    QList<int> made;
    {
        QVariantList groups;
        for (const OrphanParentGroup& g : plan) {
            QVariantList mem;
            for (const OrphanMatch& m : g.members)
                mem << QVariantMap{{ QStringLiteral("orphan"), m.orphan }, { QStringLiteral("best"), m.best },
                                   { QStringLiteral("r"), m.r }};
            groups << QVariantMap{{ QStringLiteral("template_parent"), g.templateParent },
                                  { QStringLiteral("members"), mem }};
        }
        beginGesture(QStringLiteral("mergeOrphanParents"), {{ QStringLiteral("groups"), groups }});
    }
    for (const OrphanParentGroup& g : plan) {
        // A group of one is already its own parent: merging it would only give
        // it a new id.  A group without a template is no group to merge.
        if (g.templateParent < 0 || g.members.size() < 2) continue;
        QList<int> orphans;
        for (const OrphanMatch& m : g.members) orphans.append(m.orphan);
        const int kept = mergeParents(orphans, activeView);
        if (kept > 0) made.append(kept);
    }
    // mergeParents parks each merge's own parent; land on all of them instead.
    if (!made.isEmpty()) setPendingParentSelection(made);
    endGesture({{ QStringLiteral("made"), idList(made) }});
    return made;
}

int KlustersDoc::mergeChildren(const QList<int>& children, KlustersView& activeView){
    if (!childData || children.size() < 2) return -1;
    // Same-parent guard: merging children from different parents would create a
    // microfiber whose spikes straddle two parents, which the nesting invariant
    // forbids.  Refuse rather than silently re-parent.
    const int parentId = childToParent.value(children.first(), -1);
    for (int c : children)
        if (childToParent.value(c, -2) != parentId) return -1;

    QList<int> grp = children;                         // groupClusters takes a non-const ref
    logChildBefore(CurationLogger::ActionType::HIERARCHY_EDIT, children);
    // What the merge rests on, measured before it: the pair record a parent GROUP
    // gets, on the child layer.  All pairs up to six children; above that each
    // child against the largest (a merge of 55 would be 1,485 pairs).
    if (curationLogger && curationLogger->isOpen() && logActionOpen) {
        QList<int> sorted = children; std::sort(sorted.begin(), sorted.end());
        const bool allPairs = sorted.size() <= 6;
        int largest = sorted.first(); long largestN = -1;
        for (int c : sorted) {
            const long n = static_cast<long>(childData->nbOfSpikes(c));
            if (n > largestN) { largestN = n; largest = c; }
        }
        const double dwell = (QDateTime::currentMSecsSinceEpoch() - consideredSinceMs) / 1000.0;
        const bool wasShown = consideredChildLayer && sorted == consideredSel;
        auto record = [&](int a, int b) {
            QMap<QString, QVariant> d = childData->pairEvidence(a, b);
            d.insert(QStringLiteral("detail"), QStringLiteral("pair"));
            d.insert(QStringLiteral("layer"), QStringLiteral("child"));
            d.insert(QStringLiteral("parent"), parentId);
            d.insert(QStringLiteral("pairing"), allPairs ? QStringLiteral("all") : QStringLiteral("vs_largest"));
            d.insert(QStringLiteral("n_merged"), static_cast<int>(sorted.size()));
            d.insert(QStringLiteral("dwell_s"), dwell);
            d.insert(QStringLiteral("selection_matches"), wasShown);
            curationLogger->recordActionDetails(d);
        };
        if (allPairs) {
            for (int i = 0; i < sorted.size(); ++i)
                for (int j = i + 1; j < sorted.size(); ++j) record(sorted[i], sorted[j]);
        } else {
            for (int c : sorted) if (c != largest) record(largest, c);
        }
    }
    consideredActed = true;                            // the children on screen became this merge
    int newId = static_cast<int>(childData->groupClusters(grp));         // mutates childData, self-snapshots
    {
        // A parent whose children were all merged is left with ONE child: it takes the
        // parent's self-child id.  Settled here, before the log and the views hear of the
        // new child, so that everything downstream names it once.
        QMap<int, int> renamed;
        if (normaliseChildIds(&renamed)) newId = renamed.value(newId, newId);
    }
    if (curationLogger && curationLogger->isOpen() && logActionOpen)
        curationLogger->recordActionDetails({{ QStringLiteral("edit"), QStringLiteral("mergeChildren") },
                                             { QStringLiteral("parent"), parentId },
                                             { QStringLiteral("children"), idList(children) },
                                             { QStringLiteral("new_atom"), newId }});
    logChildAfter(QList<int>{ newId });

    // Refresh the views, exactly as the parent-layer merge does.
    //
    // KlustersDoc::groupClusters ends by walking viewList and calling
    // groupedClustersUpdate on each, which is what makes the feature view redraw
    // with the merged cluster.  This function calls Data::groupClusters directly --
    // one layer down, on childData -- so none of that ran, and a child merge left
    // the feature view showing the two source clusters as they were.  A nudge
    // refreshed correctly because it goes through clusterFeaturesReprojected
    // instead, which is why the two behaved differently.
    {
        QList<int> merged = grp;   // groupedClustersUpdate takes a non-const ref
        for (KlustersView* view : *viewList) {
            const bool isActive = (view == &activeView);
            view->groupedClustersUpdate(merged, newId, isActive);
            view->updateTraceView(electrodeGroupID, childColorList ? childColorList
                                                                   : clusterColorList, isActive);
        }
    }
    ChildEdit e; e.added = { newId }; e.deleted = children;
    recordChildEdit(e);

    // Child-primary: maintain the stored map directly.
    //
    // A merge does not change any child's PARENT -- the same-parent guard above
    // already established that all of them share one -- so the map edit is: the
    // merged id inherits that parent, the consumed ids leave.  No spike is
    // reparented, which is why this operation is O(children) under the new model
    // and a full 422k-row re-derivation under the old one.
    //
    // Applied before rebuildHierarchyFromData() so that, with the backend off,
    // the rebuild overwrites it from the arrays and the shadow comparison that
    // follows is a genuine check of this edit rather than a copy of it.
    syncChildColors();
    rebuildHierarchyFromData();
    // Park the landing BEFORE hierarchyChanged is emitted.
    //
    // That signal is a DIRECT connection: the app repopulates the child palette
    // inside the emit, and repopulateChildPalette drains the pending selection
    // right there.  Parked on the line after -- as it was -- the drain always saw
    // an empty park and fell back to the prior selection, which is the two
    // children that no longer exist, so the merge ended with nothing selected.
    //
    // Parked first, every rebuild sees it: the synchronous one inside this emit,
    // and any deferred one the post-edit automation schedules afterwards.
    setPendingChildSelection(QList<int>{ newId });

    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();

    // Park the landing on the merged child.
    //
    // "Operations performed in this mode should return focus to the output
    // clusters or merged clusters within the children palette" -- this is that
    // sentence, for a merge.  The merge is the whole point of the scoped
    // matrices: the cell names a pair, the click selects it, G merges it, and the
    // result is what the user then judges.  Leaving the selection on two ids that
    // no longer exist means the next gesture starts from nothing.
    //
    // Parked rather than emitted once: the automation scheduled off
    // hierarchyChanged repopulates the palette again afterwards with no landing of
    // its own, so a single emission is applied and then wiped by the next rebuild.
    // repopulateChildPalette drains this after whichever rebuild is last.
    emit hierarchyChildSelectionRequested(QList<int>{ newId });

    modified = true;
    return newId;
}

int KlustersDoc::flattenHierarchyToClu(KlustersView& activeView){
    // Session-wide flatten: every parent ends up covered by exactly ONE child whose
    // id is the parent's own id.  This is the "self child" identity the .clc == .clu
    // lift establishes at load, re-applied to the whole layer -- the bulk form of
    // the single-parent collapse, for a session whose sub-mode structure is no
    // longer wanted (e.g. fiber_stochastic's ~2.5 children per parent after the
    // parents themselves have been curated).
    //
    // Deliberately DESTRUCTIVE and distinct from collapseToSelfChildren(), which
    // is a repair: that one preserves any child lying wholly inside one parent as
    // deliberate sub-structure and only re-cuts what straddles.  This one removes
    // the sub-structure outright, which is the point.
    if (!childData || !clusteringData) return -1;

    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    // Both layers are built over the same .fet/.res, so the row vectors must be
    // the same length.  If they are not, something is wrong with the pairing and
    // a partial relabel would corrupt the child layer -- refuse instead of guessing.
    if (cluByRow.size() != childByRow.size() || cluByRow.size() < 2) return -1;

    // The lift is applied to EVERY parent including the reserve bins (0 artifact /
    // 1 noise).  Skipping them, as collapseToSelfChildren does, would leave their
    // children carrying arbitrary ids -- and a child left in a reserve bin whose id
    // happens to equal a real parent id would immediately span two parents, i.e.
    // this button would itself create the nesting violation it is meant to tidy
    // away.  Mapping child := parent everywhere makes the result trivially sound:
    // .clc is exactly .clu, so no child can span two parents by construction.
    QVector<dataType> newLabels(childByRow.size(), 0);
    QSet<int> collapsedParents;                    // parents that actually lost a child
    for (int r = 1; r < childByRow.size(); ++r){
        const dataType F = cluByRow[r];
        const dataType self = static_cast<dataType>(hierarchyfiles::selfChildOf(F));
        newLabels[r] = self;
        if (childByRow[r] != self) collapsedParents.insert(static_cast<int>(F));
    }
    if (collapsedParents.isEmpty()) return 0;      // already one self child per parent

    const int childrenBefore = static_cast<int>(childData->clusterIds().size());

    // setClusterLabels pushes exactly one Data undo level, so the matching single
    // ChildEdit below makes Ctrl+Shift+Z revert the entire flatten in one step.
    // (restoreClusterLabels would have been wrong here: it pushes none, and the
    // ChildEdit would then pop an unrelated older snapshot.)
    //
    // It refuses, and pushes no level, when the map was desynced on entry.  Bail out
    // WITHOUT recording a ChildEdit in that case: an unmatched entry would make the
    // next child undo pop an unrelated older snapshot -- exactly the bug the comment
    // above warns about, arrived at from the other direction.
    if (!childData->setClusterLabels(newLabels)) return -1;

    const QList<dataType> after = childData->clusterIds();
    QList<int> survivors;
    for (dataType id : after) survivors.append(static_cast<int>(id));
    ChildEdit e; e.modified = survivors;
    recordChildEdit(e);
    beginGesture(QStringLiteral("flattenHierarchyToClu"),
                 {{ QStringLiteral("atoms_before"), childrenBefore }, { QStringLiteral("atoms_after"), static_cast<int>(after.size()) },
                  { QStringLiteral("parents_collapsed"), static_cast<int>(collapsedParents.size()) }});
    endGesture();

    syncChildColors();
    // Child-primary: a flatten rewrites the whole relation -- every parent ends up
    // covered by one child carrying its own id -- so the map is RESEEDED from the
    // arrays rather than edited.  Clearing the seed flag makes the next rebuild do
    // it, which is the same path the load uses and therefore the same code.
    rebuildHierarchyFromData();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    modified = true;
    return childrenBefore - static_cast<int>(after.size());   // children removed
}

bool KlustersDoc::repairNestingAsStep(){
    if (!childData) return false;
    const QVector<dataType> before = childData->labelByFeatureRow();
    repairNesting();
    return recordChildChange(before);
}

// ── promotion: flat .clu -> hierarchical triple ─────────────────────────────

bool KlustersDoc::canPromoteToHierarchy(QString* why) const {
    auto no = [why](const QString& w){ if (why) *why = w; return false; };
    if (!clusteringData || docUrl.isEmpty())
        return no(tr("No clustering is open."));
    if (isHierarchicalSession())
        return no(tr("This session is already hierarchical."));
    const QString name = QFileInfo(docUrl).fileName();
    if (!name.contains(QStringLiteral(".clu.")))
        return no(tr("Only a cluster file named <base>.clu.<group> (with an optional method "
                     "and suffix) can be promoted: its .clc and .clp are named after it.  "
                     "\"%1\" is not one.").arg(name));
    // loadChildClustering() reads the child layer with the YAML parameter file.
    if (!siblingYamlForm)
        return no(tr("A hierarchical session needs the YAML parameter file (%1.yaml); this "
                     "session was opened from .par files.").arg(baseName));
    if (!QFileInfo::exists(docUrl))
        return no(tr("%1 has not been saved yet.  Save the clustering first.").arg(name));
    return true;
}

bool KlustersDoc::promoteToHierarchy(QString* message){
    QString why;
    if (!canPromoteToHierarchy(&why)) { *message = why; return false; }

    const std::string cluPath = QFile::encodeName(docUrl).toStdString();
    const std::string clcPath = hierarchyfiles::siblingPath(cluPath, "clc");
    const std::string clpPath = hierarchyfiles::siblingPath(cluPath, "clp");
    const QString cluName = QFileInfo(docUrl).fileName();
    const QString clcName = QFileInfo(QFile::decodeName(QByteArray::fromStdString(clcPath))).fileName();
    const QString clpName = QFileInfo(QFile::decodeName(QByteArray::fromStdString(clpPath))).fileName();
    const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());

    // The saved .clu, not the in-memory clustering: the two new files must nest
    // in the file they ship with.  The caller saves first when there are edits.
    const neurofileio::CluFile parents = neurofileio::readCluBinary(cluPath, nSpikes);
    if (!parents.ok) {
        *message = tr("Could not read %1 as a cluster file of %2 spikes.").arg(cluName).arg(nSpikes);
        return false;
    }

    std::string e;
    if (QFileInfo::exists(QFile::decodeName(QByteArray::fromStdString(clcPath)))) {
        // A .clc without its .clp opens flat and is ignored; promotion adopts it
        // when it fits, rather than overwriting someone's child layer.
        const neurofileio::CluFile children = neurofileio::readCluBinary(clcPath, nSpikes);
        if (!children.ok) {
            *message = tr("%1 already exists but is not a child layer of this clustering (it "
                          "holds %2 entries for %3 spikes).  Move it aside to promote from "
                          "%4 alone.").arg(clcName).arg(children.nInFile).arg(nSpikes).arg(cluName);
            return false;
        }
        const hierarchyfiles::NestingScan scan = hierarchyfiles::scanNesting(parents.ids, children.ids);
        if (!scan.straddlers.empty()) {
            *message = tr("%1 already exists but is not nested in %2: %n of its atom(s) span "
                          "more than one unit.  Move it aside to promote from %2 alone.", "",
                          static_cast<int>(scan.straddlers.size())).arg(clcName, cluName);
            return false;
        }
        const std::vector<int32_t> clp = hierarchyfiles::clpFromMap(scan.childToParent);
        if (!hierarchyfiles::writeIdFile(clpPath, static_cast<int32_t>(clp.size()), clp, true, &e)) {
            *message = tr("Could not write %1:\n%2").arg(clpName, QString::fromStdString(e));
            return false;
        }
        *message = tr("Kept the existing child layer %1 (%n atom(s), nested in %2) and wrote "
                      "its parent map %3.", "", hierarchyfiles::distinctCount(children.ids))
                       .arg(clcName, cluName, clpName);
        return true;
    }

    // No child layer yet: one child per unit, with the unit's id.
    const hierarchyfiles::Promotion p = hierarchyfiles::promoteFlat(parents.ids);
    if (!hierarchyfiles::stageIdFile(clcPath, p.clcCount, p.clc, &e)
        || !hierarchyfiles::stageIdFile(clpPath, static_cast<int32_t>(p.clp.size()), p.clp, &e)) {
        hierarchyfiles::discardStaged(clcPath);
        hierarchyfiles::discardStaged(clpPath);
        *message = tr("Could not write the new child layer:\n%1").arg(QString::fromStdString(e));
        return false;
    }
    if (!hierarchyfiles::installStaged(clcPath, true, &e)) {
        hierarchyfiles::discardStaged(clpPath);
        *message = tr("Could not write %1:\n%2").arg(clcName, QString::fromStdString(e));
        return false;
    }
    if (!hierarchyfiles::installStaged(clpPath, true, &e)) {
        // Leave the session flat, as it was: the .clc alone is ignored on open,
        // but it was not there before, so take it away again.
        QFile::remove(QFile::decodeName(QByteArray::fromStdString(clcPath)));
        *message = tr("Could not write %1:\n%2").arg(clpName, QString::fromStdString(e));
        return false;
    }
    *message = tr("Wrote %1 and %2: each of the %n unit(s) of %3 is now one atom with the same id.",
                  "", p.clcCount).arg(clcName, clpName, cluName);
    beginGesture(QStringLiteral("promoteToHierarchy"),
                 {{ QStringLiteral("atoms"), static_cast<int>(p.clcCount) }, { QStringLiteral("clc"), clcName },
                  { QStringLiteral("clp"), clpName }});
    endGesture();
    return true;
}
