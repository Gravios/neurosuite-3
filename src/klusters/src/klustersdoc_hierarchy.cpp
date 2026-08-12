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
        // The live .clu for the session is the PENDING copy, not docUrl: initPendingFiles() seeds
        // <docUrl>.pending and redirects the clu writer there "for the entire document session", and
        // commitAndRenewPending() only copies it back over docUrl at the very END of saveDocument --
        // long after saveHierarchySiblings() runs, and long after a realign has rewritten it.  Reading
        // docUrl here would see the pre-save / pre-realign labels.
        const QString liveClu = (!pendingCluPath.isEmpty() && QFile::exists(pendingCluPath))
                              ? pendingCluPath : docUrl;
        const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
        const neurofileio::CluFile par = neurofileio::readCluBinary(liveClu.toStdString(), nSpikes);
        const neurofileio::CluFile chi = neurofileio::readCluBinary(clcSiblingPath.toStdString(), nSpikes);
        if (par.ok && chi.ok && par.ids.size() == chi.ids.size()){
            haveScan = true;
            // child -> parent must be a function (nesting invariant): each child id is owned by
            // exactly one parent.  A violation means the .clc is not nested in the .clu; we keep the
            // first-seen owner and warn rather than silently merge.
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
        // saveHierarchySiblings() regenerates the .clp on the next Save.
        if (!clpSiblingPath.isEmpty()){
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
            v->stopAllViewThreads();

    // Second Data over the SAME fet/spk/par, with the .clc as the cluster file.
    // (v1 re-reads the feature/spike arrays; they could later be shared with the
    // parent to halve memory.)
    childData = new Data();
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
// change: the derived map keeps an atom's FIRST-SEEN owner when the atom straddles
// two fibers, silently discarding the rest.  The shadow records the straddle
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


void KlustersDoc::rebuildHierarchyFromData(){
    if (!childData) return;
    parentToChildren.clear();
    childToParent.clear();
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());
    // The nesting invariant (hierarchical-clustering.md): every atom is owned by
    // exactly one fiber -- all spikes carrying a given .clc id carry the same .clu
    // id.  buildHierarchyMaps() already checks this on the LOAD path, but this
    // post-edit rebuild did not: it inserted on first-seen and dropped every later
    // (child,parent) pair on the floor, so an atom left straddling two fibers by an
    // edit (a collapseToSelfChildren that did not fully collapse, or an edit path
    // that never called it) was filed under its first-seen fiber with no trace.
    // That silent absorption is exactly what produced the g5 "5 children span two
    // parents" file.  First-seen still wins -- the maps are a derived cache and the
    // behaviour is unchanged -- but a violation is now reported, with the offending
    // atoms, so the edit that introduced it can be found instead of vanishing.
    QSet<int> offenders;                           // distinct atoms seen under >1 fiber
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
                   << "atom(s) span more than one fiber; kept each atom's first-seen owner. "
                      "Offending atom id(s) (up to 16):" << sample
                   << "-- an edit left straddling atoms uncollapsed; refiberize to re-cut them.";
    }
    for (auto it = parentToChildren.begin(); it != parentToChildren.end(); ++it){
        QList<int>& kids = it.value();
        std::sort(kids.begin(), kids.end());
        kids.erase(std::unique(kids.begin(), kids.end()), kids.end());
    }

    // Shadow comparison, after the derived maps are complete and before any
    // consumer sees them.  Inert unless NS3_SHADOW_HIERARCHY is set.
    deriveStoredMap();
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
// are pointed at the right data already and only need to be told WHICH atoms.
// That is one parent's children, and the parent is the child palette's, which
// lives in KlustersApp; hence the mirror here rather than a lookup.
// ---------------------------------------------------------------------------
void KlustersDoc::setMatrixScopeEnabled(bool enabled)
{
    if (matrixScopeOn == enabled) return;   // no spurious recomputes
    matrixScopeOn = enabled;
    resolveMatrixScope();                   // emits matrixScopeChanged if it moved
}

void KlustersDoc::setPendingChildSelection(const QList<int>& children)
{
    if (!children.isEmpty()) pendingChildSelection = children;
}

QList<int> KlustersDoc::takePendingChildSelection()
{
    QList<int> out = pendingChildSelection;
    pendingChildSelection.clear();
    return out;
}

void KlustersDoc::setCuratedParent(int fiberId)
{
    if (curatedParentId == fiberId) return;   // no spurious recomputes
    curatedParentId = fiberId;
    resolveMatrixScope();                     // emits matrixScopeChanged if it moved
}

void KlustersDoc::resolveMatrixScope()
{
    const bool was = scopeResolvedActive;
    const QList<int> before = scopeResolvedClusters;

    scopeResolvedClusters.clear();
    scopeResolvedActive = false;
    if (matrixScopeOn && childData && curatedParentId >= 0) {
        const QList<int> kids = childrenOf(QList<int>{curatedParentId});
        if (!kids.isEmpty()) {
            scopeResolvedClusters = kids;
            scopeResolvedActive   = true;
        }
    }
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug().noquote() << "[matrixscope] resolved: parent=" << curatedParentId
                           << " on=" << matrixScopeOn
                           << " active=" << scopeResolvedActive
                           << " n=" << scopeResolvedClusters.size();
    if (was != scopeResolvedActive || before != scopeResolvedClusters)
        emit matrixScopeChanged();
}

void KlustersDoc::selectFromMatrix(const QList<int>& ids, const QList<int>& previous)
{
    if (ids.isEmpty()) return;
    if (matrixScopeActive()) {
        // Atom ids: land them in the child palette, which is where the user is
        // working and where the merge will be made.
        emit hierarchyChildSelectionRequested(ids);
        return;
    }
    QList<int> show = ids, prev = previous;
    if (prev.isEmpty()) shownClustersUpdate(show);
    else                shownClustersUpdate(show, prev);
}
Data& KlustersDoc::matrixData() const
{
    // The atom layer when a scope is in force, whatever the rest of the app is
    // currently showing.  See the header for why data() is the wrong answer here.
    return matrixScopeActive() ? *childData : data();
}


bool KlustersDoc::validateHierarchyMaps(const char* where) const {
    // Nothing to compare against before the child layer exists, or in a flat
    // session.  This is a genuine blind spot rather than a safe case: a session
    // curated without ever opening the hierarchical view has no childData, so no
    // repair has run and the .clu may well have drifted from the on-disk .clc.
    // That fault is file-level and is caught by buildHierarchyMaps()'s scan, whose
    // verdict saveHierarchySiblings() consults via hierarchyScanFoundViolation.
    if (!childData || !clusteringData) return true;
    if (childToParent.isEmpty()) return true;

    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());

    // The relation the per-spike arrays actually encode right now.  Atoms that
    // span fibers are counted separately: that is a nesting violation, a
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
    // things: a wrong owner is an edit that moved spikes, a map-only atom is an
    // edit that emptied one, a scan-only atom is an edit that created one.
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
               << wrongOwner.size() << "atom(s) owned by a different fiber than the"
               << "per-spike arrays say," << mapOnly.size() << "in the map but gone from the"
               << "arrays," << scanOnly.size() << "in the arrays but missing from the map."
               << "An edit path mutated a layer without refreshing the maps."
               << "wrong-owner:" << wrongOwner.mid(0, 8)
               << "map-only:"    << mapOnly.mid(0, 8)
               << "scan-only:"   << scanOnly.mid(0, 8);
    if (spanning)
        qWarning() << "[hierarchy] (also" << spanning << "spike(s) whose atom spans fibers -- that is a"
                   << "nesting violation, reported separately by rebuildHierarchyFromData)";
    return false;
}


void KlustersDoc::collapseToSelfChildren(){
    // The single hierarchy invariant: every fiber is covered by its child atom(s), and a fiber
    // with one child IS that child (the "self child", atom id == fiber id -- the identity the
    // .clc==.clu lift establishes).  Any flat-layer edit -- a parent recluster, an artifact
    // recluster, a manual split, or a child recluster whose new atoms span fibers -- leaves
    // "loose" spikes: in a fiber (real OR reserve) but covered only by an atom belonging to a
    // different fiber, or by an atom that now straddles several fibers.  Collapse every loose spike
    // into its fiber's self child, so each fiber regains a covering child with no new fiber
    // invented.  Atoms that are real (> 1) AND wholly inside one fiber are deliberate
    // sub-structure and are preserved untouched.
    if (!childData) return;
    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    const int n = qMin(cluByRow.size(), childByRow.size());

    // Per real atom, count its spikes in each fiber.  This distinguishes a wholly-
    // inside atom (one fiber) from a straddler, AND picks a straddler's "home" fiber
    // -- the one holding the plurality of its spikes.  A manual polygon split clips
    // some of a fiber's spikes into a new fiber; if that clip crosses a deliberate
    // sub-cluster atom, the old code dissolved that atom into self children on BOTH
    // sides, so the source fiber lost the child.  Keeping the straddler whole on its
    // home fiber -- and collapsing only the clipped-off remainder into the new
    // fiber's self child -- preserves the sub-cluster where it still mostly lives.
    QHash<int, QHash<int,int>> atomFiberCount;               // real atom -> fiber -> spike count
    for (int r = 1; r < n; ++r){
        const int a = static_cast<int>(childByRow[r]);
        if (a <= 1) continue;
        atomFiberCount[a][static_cast<int>(cluByRow[r])] += 1;
    }
    QSet<int> intact;                                        // real atoms wholly inside one fiber
    QHash<int,int> homeFiber;                                // straddler -> fiber to keep it whole on
    for (auto it = atomFiberCount.constBegin(); it != atomFiberCount.constEnd(); ++it){
        const QHash<int,int>& perFiber = it.value();
        if (perFiber.size() == 1){ intact.insert(it.key()); continue; }
        const int a = it.key();
        // A self child (atom id == a fiber id) stays on its own fiber; any other atom
        // stays on the fiber holding the plurality of its spikes (ties -> lower fiber
        // id == the pre-existing source, not the freshly minted split-off fiber).
        if (perFiber.contains(a)){ homeFiber[a] = a; continue; }
        int best = -1, bestN = -1;
        for (auto f = perFiber.constBegin(); f != perFiber.constEnd(); ++f)
            if (f.value() > bestN || (f.value() == bestN && f.key() < best)){ bestN = f.value(); best = f.key(); }
        homeFiber[a] = best;
    }

    // Decide, per row, whether it is loose and which atom it collapses into.
    //
    // The reserve fibers (0 artifact / 1 noise) are NOT skipped.  They used to be --
    // "noise/artifact fibers keep their atoms" -- and that is what made a whole class
    // of straddler permanently unrepairable: an atom whose plurality sits in a real
    // fiber but which has a few spikes in noise has its noise-side rows skipped here
    // (reserve fiber) and its real-side rows skipped below (home fiber), so NOTHING
    // moves and the atom still spans two fibers.  refiberize() is then a no-op on it,
    // which is why the same offender list is re-reported verbatim after every edit
    // while the warning keeps advising a refiberize that cannot help.  The reserve
    // bins get the same self-child treatment as any other fiber -- atom id == fiber
    // id, i.e. spikes dropped to noise are covered by atom 1 and spikes dropped to
    // artifact by atom 0 -- which is exactly the lift flattenHierarchyToClu() already
    // applies to every fiber including the reserve bins, and for the same reason it
    // states there: an atom left in a reserve bin carrying an arbitrary id is a
    // nesting violation waiting to happen.  An atom lying WHOLLY inside a reserve bin
    // is still `intact` and is preserved untouched, so a sub-cluster deliberately
    // dropped to noise as a unit keeps its identity.
    // Decide, per row, whether it is loose and which atom it collapses into.
    //
    // The reserve fibers (0 artifact / 1 noise) are NOT skipped.  They used to be --
    // "noise/artifact fibers keep their atoms" -- and that is what made a whole class
    // of straddler permanently unrepairable: an atom whose plurality sits in a real
    // fiber but which has a few spikes in noise has its noise-side rows skipped here
    // (reserve fiber) and its real-side rows skipped below (home fiber), so NOTHING
    // moves and the atom still spans two fibers.  refiberize() is then a no-op on it,
    // which is why the same offender list is re-reported verbatim after every edit
    // while the warning keeps advising a refiberize that cannot help.  The reserve
    // bins get the same self-child treatment as any other fiber -- atom id == fiber
    // id, i.e. spikes dropped to noise are covered by atom 1 and spikes dropped to
    // artifact by atom 0 -- which is exactly the lift flattenHierarchyToClu() already
    // applies to every fiber including the reserve bins, and for the same reason it
    // states there: an atom left in a reserve bin carrying an arbitrary id is a
    // nesting violation waiting to happen.  An atom lying WHOLLY inside a reserve bin
    // is still `intact` and is preserved untouched, so a sub-cluster deliberately
    // dropped to noise as a unit keeps its identity.

    // An atom is KEPT if at least one of its rows survives the three skip rules
    // below -- it is intact, it is already its fiber's self child, or it is a
    // straddler sitting on its home fiber.  Looseness does not depend on the
    // targets, so this can be settled first, and it is what makes the target
    // choice safe in one pass.
    // Same pass also records, per real fiber, WHICH source atoms feed it loose
    // rows.  A fiber fed by several sources fragments into one descendant per
    // source (see targetFor), so the count has to be complete before any target
    // is handed out.
    QSet<int> keptAtom;
    QHash<int, QSet<int>> looseSources;
    for (int r = 1; r < n; ++r){
        const int F = static_cast<int>(cluByRow[r]);
        const int a = static_cast<int>(childByRow[r]);
        if ((a > 1 && intact.contains(a)) || a == F
                || (a > 1 && homeFiber.value(a, -1) == F))
            keptAtom.insert(a);
        else if (F > 1)
            looseSources[F].insert(a);
    }

    // Fresh atom ids start above every id either layer currently uses, so they
    // cannot collide with a surviving atom or with another fiber's target.
    int nextFreeAtom = 1;
    for (int r = 1; r < n; ++r){
        nextFreeAtom = qMax(nextFreeAtom, static_cast<int>(childByRow[r]));
        nextFreeAtom = qMax(nextFreeAtom, static_cast<int>(cluByRow[r]));
    }

    // (source atom, fiber) -> the atom that pair's loose rows collapse into.
    //
    // Keyed by the SOURCE as well as the fiber, so a cut that crosses several
    // atoms yields one descendant per crossed atom rather than fusing them all
    // into one covering child.  A row's atom is its provenance -- which of the
    // over-split units it came from -- and the fiber it lands in says nothing
    // about that, so collapsing rows from atoms 7, 12 and 30 into a single new
    // atom discards information the atom layer exists to carry.  Smaller children
    // that can be merged deliberately are preferable to a merge performed on the
    // user's behalf, which is not reversible without knowing what was fused.
    //
    // The fiber keeps its own id as the self child only when a SINGLE source
    // feeds it and nothing already carries that id: that is the ordinary split of
    // one fiber in two, and it behaves exactly as before.  When several sources
    // feed it, every descendant takes a fresh id -- the fiber then has more than
    // one atom, so the "a fiber with one child IS that child" convention does not
    // apply to it anyway and the normalisation pass below leaves it alone.
    //
    // Why a fresh id when an atom carrying F survives: if that survivor sits under
    // another fiber, writing F would make it span two -- the repair minting a
    // straddler, which is why one pass was not a fixed point.  If it sits under
    // fiber F itself, the atom would silently absorb spikes that were never part
    // of it, which is the same silent-substitution failure in the atom layer.
    //
    // The reserve bins are excluded from all of this: atom 0 and atom 1 ARE the
    // artifact and noise self children, membership inside them is not curated
    // structure, and fragmenting noise per source atom would scatter it across
    // ever more ids for no gain.
    QHash<int, QHash<int,int>> splitTarget;
    auto targetFor = [&](int a, int F) -> int {
        if (F <= 1) return F;
        QHash<int,int>& perSource = splitTarget[F];
        const auto cached = perSource.constFind(a);
        if (cached != perSource.constEnd()) return cached.value();
        const bool soleSource = (looseSources.value(F).size() == 1);
        const int t = (soleSource && !keptAtom.contains(F)) ? F : ++nextFreeAtom;
        perSource.insert(a, t);
        return t;
    };

    // Build the whole re-cut labelling, then commit it in ONE call.  The previous
    // form issued a moveSpikeSubset per (source atom, target atom) pair, and each of
    // those rebuilds the entire row table, revalidates it and pushes its own undo
    // level.  That is fine when a handful of atoms straddle, but the atom layer this
    // view exists for is deliberately over-split -- fiber_stochastic produces tiny
    // atoms -- so a curation gesture that clips a fiber clips hundreds of distinct
    // atoms, and the pair count follows.  Measured on a synthetic 574,121-spike
    // session with 1,500 fibers and 22,000 atoms: 2,085 rebuilds, ~1.2e9 row writes,
    // in the GUI thread.  setClusterLabels does one rebuild for the whole re-cut --
    // the same primitive flattenHierarchyToClu() already uses, for the same reason
    // -- taking that session to a single 574,121-row pass.
    //
    // It also improves the undo picture rather than harming it.  This function
    // records no ChildEdit, so every Data undo level it pushed was unmatched by an
    // entry on the atom stack; N unmatched levels become one.  refiberize() clears
    // both stacks afterwards regardless.
    // The re-cut decision, in ONE place.
    //
    // It ran twice: once against the labels on entry, and again verbatim against
    // the healed labels after a refusal.  The three skip rules ARE the policy this
    // function implements -- preserve a deliberate sub-cluster, leave a self child
    // alone, keep a straddler whole on its home fiber -- and having them written
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
            if (a == F) continue;                                // already its own self child
            if (a > 1 && homeFiber.value(a, -1) == F) continue;  // straddler whole on its home fiber
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
    // A refusal means the atom layer's map was desynced on entry, so `newLabels` was
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
    // After the collapse a fiber can be left covered by a single atom whose id is not
    // the fiber's own -- e.g. a split moved a fiber's entire self child onto a new
    // fiber, so the source fiber's sole remaining atom is a deliberate sub-cluster
    // while the new fiber's sole atom still carries the source fiber's id.  The maps
    // stay correct (a child's parent is derived from its spikes), but the convention
    // "a fiber with one child IS that child, atom id == fiber id" breaks, and the
    // stray id can later collide with a freshly minted fiber.  Rename each such sole
    // child to its fiber's id so the self-child identity holds again.  (A fiber with
    // two or more atoms is genuine sub-structure and is left untouched.)
    const QVector<dataType> childAfter = childData->labelByFeatureRow();
    const int nA = qMin(cluByRow.size(), childAfter.size());
    QHash<int, QSet<int>> fiberAtoms;                        // fiber -> its covering atoms
    QSet<int> existingAtoms;                                 // every atom id currently present
    for (int r = 1; r < nA; ++r){
        const int a = static_cast<int>(childAfter[r]);
        if (a <= 0) continue;
        existingAtoms.insert(a);
        fiberAtoms[static_cast<int>(cluByRow[r])].insert(a);
    }
    QMap<int,int> renameToSelf;                              // sole non-self child -> fiber (self) id
    for (auto it = fiberAtoms.constBegin(); it != fiberAtoms.constEnd(); ++it){
        const int F = it.key();
        if (F <= 1 || it.value().size() != 1) continue;      // noise/artifact or genuine sub-structure
        const int C = *it.value().constBegin();
        if (C != F) renameToSelf.insert(C, F);
    }
    if (!renameToSelf.isEmpty()){
        // renumberPartial buckets by NEW id, so a target still held by an atom that is
        // NOT itself being renamed away would merge two atoms.  Keep only targets that
        // are free or are themselves sources -- a plain split satisfies this for all of
        // them (a source fiber and its split-off both hand their sole atom back) -- and
        // leave any rarer case with its naming oddity rather than corrupt the layer.
        QMap<int,int> safe;
        bool admitted = true;
        while (admitted){
            admitted = false;
            for (auto it = renameToSelf.constBegin(); it != renameToSelf.constEnd(); ++it){
                if (safe.contains(it.key())) continue;              // already admitted
                if (!existingAtoms.contains(it.value())             // target free
                        || safe.contains(it.value())){              // target vacated by an accepted rename
                    safe.insert(it.key(), it.value());
                    admitted = true;
                }
            }
        }
        if (!safe.isEmpty()) childData->renumberPartial(safe);
    }

    syncChildColors();
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    // Publish what the re-cut decided, so the child-primary map records the SAME
    // answer rather than reconstructing it.
    //
    // splitTarget[F][a] is the target child minted for source atom `a` on fiber F.
    // That is the complete decision -- which side keeps the original id and which
    // gets a fresh one -- and it is made here, with the coverage table in hand.
    // Predicting it from outside means implementing the plurality rule, the
    // self-child rule and the sole-source rule a second time, which is how a shared
    // policy starts drifting.  Consuming it means the map cannot disagree with the
    // split, because it is using the split's own output.

}

void KlustersDoc::refiberize(){
    // Deliberate full resync of the atom layer to the fiber layer, independent of the parent
    // and atom undo timelines: collapse loose spikes to self children (above) and reset the
    // atom-edit history since the atom structure has just been re-cut.  rebuildHierarchyFromData
    // (inside collapseToSelfChildren) re-derives childToParent / parentToChildren, which the
    // next Save writes out as the regenerated .clp.
    collapseToSelfChildren();
    if (childData){
        childUndoStack.clear();
        childRedoStack.clear();
    }
}

int KlustersDoc::mergeParentFibers(const QList<int>& fibers, KlustersView& activeView){
    if (fibers.size() < 2) return -1;
    setActiveClustering(false);                   // edits always target the parent
    const int kept = groupClusters(fibers, activeView);   // existing: mutate + undo + views

    // Child-primary: every child of a folded fiber is reparented onto the survivor.
    // No child is split or created -- a fiber merge is a pure map operation, which
    // is the clearest case of the O(children) versus O(nSpikes) difference.
    if (childPrimaryOn)
        for (int f : fibers)
            if (f != kept)
                for (int c : parentToChildren.value(f))
                    shadowChildToParent.insert(c, kept);

    noteModifiedFiber(kept);                              // merged fiber needs realign
    setPendingFiberSelection({kept});                    // land on the merged fiber
    rebuildHierarchyFromData();
    emit hierarchyChanged();
    return kept;
}

// ---------------------------------------------------------------------------
// KlustersDoc::promoteChildren
//
// Take children out of their parent and make ONE new fiber from them.  With a
// single child this is the old promoteChild; with several it is the old
// groupChildrenIntoFiber.  They were the same operation written twice -- one
// N-ary, one unary with an extra early return -- so they are one function now.
//
// The only-child case still short-circuits: a child that is its parent's sole
// atom already covers exactly that fiber, so promoting it would move every spike
// to a new id for no change in structure.
// ---------------------------------------------------------------------------
int KlustersDoc::promoteChildren(const QList<int>& children, KlustersView& activeView){
    if (!childData || children.isEmpty()) return -1;

    if (children.size() == 1){
        const int only = children.first();
        if (!childToParent.contains(only)) return -1;
        const int parent = childToParent.value(only);
        if (parentToChildren.value(parent).size() <= 1)
            return parent;                        // already its own fiber
    }

    setActiveClustering(false);
    const int newId = static_cast<int>(clusteringData->nextFreeClusterId());
    int target = -1;
    for (int c : children){
        if (!childToParent.contains(c)) continue;
        const int parent = childToParent.value(c);
        if (parent == newId) continue;                // already pooled
        const QVector<int> spk = childData->clusterSpkIndices(c);
        if (spk.isEmpty()) continue;
        const int dest = (target < 0 ? newId : target);
        moveSpikeSubsetToCluster(parent, spk, dest, activeView);

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
    if (target < 0) return -1;
    clusterPalette.updateClusterList();
    rebuildHierarchyFromData();
    setPendingFiberSelection({target});   // the output is a FIBER; land there
    emit hierarchyChanged();
    return target;
}

bool KlustersDoc::dissolveFiber(int fiber, KlustersView& activeView){
    if (!childData) return false;
    const QList<int> kids = parentToChildren.value(fiber);
    if (kids.size() < 2) return false;                // nothing to explode
    // Each child becomes its own fiber: promoteChildren with a single-element
    // list per child, not one call with all of them, which would pool them into
    // one fiber and be the opposite operation.
    QList<int> produced;
    for (int c : kids)
        produced.append(promoteChildren(QList<int>{c}, activeView));
    setPendingFiberSelection(produced);               // select all the exploded fibers
    return true;
}

bool KlustersDoc::dropChildToNoise(int childCluster, KlustersView& activeView){
    if (!childData || !childToParent.contains(childCluster)) return false;
    const int parent = childToParent.value(childCluster);
    if (parent == 1) return false;                    // already noise
    const QVector<int> spk = childData->clusterSpkIndices(childCluster);
    if (spk.isEmpty()) return false;
    // Siblings that will survive, captured before the move: the landing is one of
    // them, and after the move the atom is gone from the parent's list.
    QList<int> siblings;
    for (int kid : parentToChildren.value(parent))
        if (kid > 1 && kid != childCluster) siblings.append(kid);

    setActiveClustering(false);
    moveSpikeSubsetToCluster(parent, spk, 1, activeView);   // 1 = noise (coloured by the wrapper)

    // Child-primary: the child is RECLASSIFIED, not relabelled.
    //
    // Noise and artefact are parent-layer classifications -- a child is a
    // sub-cluster of a unit and has no signal/noise character of its own -- so
    // dropping a child to noise is exactly "its parent is now 1".  Its spikes keep
    // their child id, which is what the moveSpikeSubsetToCluster above already
    // does: it changes the .clu of those rows and leaves the .clc alone.  So the
    // map edit is one entry, and it says the same thing the relabel does.

    setPendingFiberSelection({parent});   // land on the source fiber (noise isn't selectable)
    rebuildHierarchyFromData();

    // Parked before the emit, for the reason given in mergeChildren: the palette
    // is repopulated inside hierarchyChanged and drains the park there.  Siblings
    // were captured before the move, so this is the surviving list.
    QList<int> land;
    if (!siblings.isEmpty()) {
        std::sort(siblings.begin(), siblings.end());
        int pick = siblings.last();                 // nothing above: nearest below
        for (int kid : siblings)
            if (kid > childCluster) { pick = kid; break; }
        land = QList<int>{ pick };
        setPendingChildSelection(land);
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

    // Child-primary: maintain the stored map directly.
    //
    // A merge does not change any child's PARENT -- the same-fiber guard above
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
    // Session-wide flatten: every fiber ends up covered by exactly ONE atom whose
    // id is the fiber's own id.  This is the "self child" identity the .clc == .clu
    // lift establishes at load, re-applied to the whole layer -- the bulk form of
    // the single-fiber collapse, for a session whose sub-mode structure is no
    // longer wanted (e.g. fiber_stochastic's ~2.5 atoms per fiber after the
    // fibers themselves have been curated).
    //
    // Deliberately DESTRUCTIVE and distinct from collapseToSelfChildren(), which
    // is a repair: that one preserves any atom lying wholly inside one fiber as
    // deliberate sub-structure and only re-cuts what straddles.  This one removes
    // the sub-structure outright, which is the point.
    if (!childData || !clusteringData) return -1;

    const QVector<dataType> cluByRow   = clusteringData->labelByFeatureRow();
    const QVector<dataType> childByRow = childData->labelByFeatureRow();
    // Both layers are built over the same .fet/.res, so the row vectors must be
    // the same length.  If they are not, something is wrong with the pairing and
    // a partial relabel would corrupt the atom layer -- refuse instead of guessing.
    if (cluByRow.size() != childByRow.size() || cluByRow.size() < 2) return -1;

    // The lift is applied to EVERY fiber including the reserve bins (0 artifact /
    // 1 noise).  Skipping them, as collapseToSelfChildren does, would leave their
    // atoms carrying arbitrary ids -- and an atom left in a reserve bin whose id
    // happens to equal a real fiber id would immediately span two fibers, i.e.
    // this button would itself create the nesting violation it is meant to tidy
    // away.  Mapping atom := fiber everywhere makes the result trivially sound:
    // .clc is exactly .clu, so no atom can span two fibers by construction.
    QVector<dataType> newLabels(childByRow.size(), 0);
    QSet<int> collapsedFibers;                    // fibers that actually lost an atom
    for (int r = 1; r < childByRow.size(); ++r){
        const dataType F = cluByRow[r];
        newLabels[r] = F;
        if (childByRow[r] != F) collapsedFibers.insert(static_cast<int>(F));
    }
    if (collapsedFibers.isEmpty()) return 0;      // already one self atom per fiber

    const int atomsBefore = static_cast<int>(childData->clusterIds().size());

    // setClusterLabels pushes exactly one Data undo level, so the matching single
    // ChildEdit below makes Ctrl+Shift+Z revert the entire flatten in one step.
    // (restoreClusterLabels would have been wrong here: it pushes none, and the
    // ChildEdit would then pop an unrelated older snapshot.)
    //
    // It refuses, and pushes no level, when the map was desynced on entry.  Bail out
    // WITHOUT recording a ChildEdit in that case: an unmatched entry would make the
    // next atom undo pop an unrelated older snapshot -- exactly the bug the comment
    // above warns about, arrived at from the other direction.
    if (!childData->setClusterLabels(newLabels)) return -1;

    const QList<dataType> after = childData->clusterIds();
    QList<int> survivors;
    for (dataType id : after) survivors.append(static_cast<int>(id));
    ChildEdit e; e.modified = survivors;
    recordChildEdit(e);

    syncChildColors();
    // Child-primary: a flatten rewrites the whole relation -- every fiber ends up
    // covered by one atom carrying its own id -- so the map is RESEEDED from the
    // arrays rather than edited.  Clearing the seed flag makes the next rebuild do
    // it, which is the same path the load uses and therefore the same code.
    rebuildHierarchyFromData();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    modified = true;
    return atomsBefore - static_cast<int>(after.size());   // atoms removed
}

bool KlustersDoc::undoChildEdit(KlustersView& activeView){
    if (!childData || childUndoStack.isEmpty()) return false;
    ChildEdit e = childUndoStack.takeFirst();
    QList<int> added = e.added;                        // by-ref args; copy so the entry is preserved
    QList<int> mod = e.modified;
    childData->undo(added, mod);                       // reverts childData's tables
    childRedoStack.prepend(e);
    syncChildColors();
    // Child-primary: an undo restores a previous labelling, and the map that goes
    // with it is not recoverable from the ChildEdit -- it records id SETS, not the
    // parentage they had.  So the map is reseeded from the restored arrays.  That
    // is correct but wasteful: recording the map delta alongside the ChildEdit
    // would make undo O(edited) like every other operation, and is the one place
    // this backend still needs a design decision rather than wiring.
    rebuildHierarchyFromData();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    modified = true;
    refreshUndoRedoEnable();
    return true;
}

bool KlustersDoc::redoChildEdit(KlustersView& activeView){
    if (!childData || childRedoStack.isEmpty()) return false;
    ChildEdit e = childRedoStack.takeFirst();
    QList<int> added = e.added, mod = e.modified, deleted = e.deleted;
    childData->redo(added, mod, deleted);             // re-applies the atom edit
    childUndoStack.prepend(e);
    syncChildColors();
    // Child-primary: an undo restores a previous labelling, and the map that goes
    // with it is not recoverable from the ChildEdit -- it records id SETS, not the
    // parentage they had.  So the map is reseeded from the restored arrays.  That
    // is correct but wasteful: recording the map delta alongside the ChildEdit
    // would make undo O(edited) like every other operation, and is the one place
    // this backend still needs a design decision rather than wiring.
    rebuildHierarchyFromData();
    if (childScopeActive) activeView.showAllWidgets();
    emit hierarchyChanged();
    modified = true;
    refreshUndoRedoEnable();
    return true;
}

// ── layer-scoped undo/redo dispatch ──────────────────────────────────────────
// The two layers keep independent undo stacks: the parent stack (clusteringData,
// driven by undo()/redo()) and the atom stack (childData, driven by
// undoChildEdit()/redoChildEdit()).  There is no unified-order timeline -- a
// keystroke reverts the most recent edit IN THE ACTIVE LAYER only, so an atom
// undo can never revert a fiber edit and vice versa; switch scope to undo the
// other layer.  The forced atom undo/redo (Ctrl+Shift+Z / Ctrl+Shift+Y) reaches
// the atom stack regardless of which layer is shown.

void KlustersDoc::refreshUndoRedoEnable(){
    // The main Undo/Redo act on the shown layer, so report that layer's counts.
    emit updateUndoNb(childScopeActive ? childUndoCount() : parentUndoCount());
    emit updateRedoNb(childScopeActive ? childRedoCount() : parentRedoCount());
}

void KlustersDoc::recordChildEdit(const ChildEdit& e){
    childUndoStack.prepend(e);
    childRedoStack.clear();                       // a new edit invalidates atom redo
    refreshUndoRedoEnable();
}

void KlustersDoc::undoDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v) return;
    if (childScopeActive){                        // atom layer shown -> atom stack only
        if (childUndoCount() > 0) undoChildEdit(*v);
    } else {                                      // fiber layer shown -> parent stack only
        if (parentUndoCount() > 0) undo();
    }
}

void KlustersDoc::redoDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v) return;
    if (childScopeActive){
        if (childRedoCount() > 0) redoChildEdit(*v);
    } else {
        if (parentRedoCount() > 0) redo();
    }
}

bool KlustersDoc::undoChildEditDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v || childUndoCount() == 0) return false;
    return undoChildEdit(*v);          // forced atom undo, regardless of active scope
}

bool KlustersDoc::redoChildEditDispatch(){
    KlustersView* v = app() ? app()->activeView() : nullptr;
    if (!v || childRedoCount() == 0) return false;
    return redoChildEdit(*v);          // forced atom redo, regardless of active scope
}

bool KlustersDoc::saveHierarchySiblings(){
    if (clcSiblingPath.isEmpty()) return true;                 // not a hierarchical session
    // The child layer is loaded lazily, so a session curated WITHOUT ever opening the hierarchical
    // view has an empty map here while the .clu we just wrote has changed -- which is exactly how the
    // .clp went stale.  Rebuild the map from the freshly-written per-spike arrays in that case.
    if (childToParent.isEmpty()) buildHierarchyMaps();
    if (childToParent.isEmpty()) return true;                  // nothing derivable -> leave siblings alone

    // Last gate before the map reaches disk.  The .clp below is written straight
    // out of childToParent, so a map left stale by an edit path is not merely a
    // wrong child palette for the rest of the session -- it is what gets SAVED,
    // and the next open reports it as drift against the per-spike arrays.
    //
    // Rebuild rather than write known-bad data, but report first.  Rebuilding
    // silently would hide the path that left it stale, and naming that path is
    // the whole point of the check: four edit paths mutated a layer without
    // refreshing the maps until recently, and nothing here would have noticed.
    // If a report ever appears, the gesture that preceded the save is the lead.
    if (!validateHierarchyMaps("saveHierarchySiblings")) {
        rebuildHierarchyFromData();
        if (!validateHierarchyMaps("saveHierarchySiblings/after-rebuild"))
            qWarning() << "[hierarchy] the rebuild did not settle the maps; the .clp about to be"
                       << "written may not agree with the .clu/.clc it ships with.";
    }
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
    // .clc: only the loaded child layer can rewrite it.  Curation does not change the child ids, so
    // when the child view was never opened the on-disk .clc is already correct and is left alone.
    if (childData){
        backup(clcSiblingPath);
        if (FILE* f = fopen(qPrintable(clcSiblingPath), "wb")){
            ok &= childData->saveClusters(f);
            fclose(f);
        } else ok = false;
    }

    if (!clpSiblingPath.isEmpty()){
        // Do not write a .clp that is known to contradict the triple it ships with.
        //
        // Every hierarchy repair in this file is guarded by `if (childData)`, and
        // childData is built lazily by loadChildClustering() -- only once the
        // hierarchical view has actually been opened.  So a session curated WITHOUT
        // ever opening that view gets no re-cut at all: the .clu moves, the on-disk
        // .clc does not, and atoms end up under more than one fiber at the file
        // level.  buildHierarchyMaps() sees exactly that on its scan, warns, and
        // keeps the first-seen parent for each child -- which is a reasonable way to
        // finish BUILDING a map, and a bad thing to then SAVE, because the arbitrary
        // half of each straddle becomes the recorded answer.
        //
        // The .clc is not rewritten in this situation either (its write is
        // childData-guarded above), so persisting the .clp would leave a triple whose
        // three files disagree, with the .clp the only one asserting a nesting that
        // is false.  Keep the previous .clp instead and say what to do: opening the
        // hierarchical view loads the atom layer and refiberize() re-cuts the
        // straddlers, after which a save writes a consistent triple.
        //
        // validateHierarchyMaps() does not cover this: it compares the in-memory map
        // against the loaded layers, and there is no loaded child layer here.
        if (hierarchyScanFoundViolation && !childData){
            qWarning() << "[hierarchy] REFUSING to write" << clpSiblingPath
                       << "-- the per-spike .clu/.clc disagree about which fiber owns at"
                       << "least one atom, and with no child layer loaded nothing has"
                       << "re-cut them, so the map would record an arbitrary owner for"
                       << "each straddle.  The previous .clp is left in place.  Open the"
                       << "hierarchical view (which loads the atom layer and re-cuts on"
                       << "the next edit, or use Refiberize) and save again.";
            return ok;
        }
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
