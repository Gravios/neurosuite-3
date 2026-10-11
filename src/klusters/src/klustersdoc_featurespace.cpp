/***************************************************************************
    klustersdoc_featurespace.cpp
    ----------------------------
    KlustersDoc feature-space switching: list the method spaces a group has on
    disk (<base>.fet.<method>.<group>) and load another space's .fet/.spk while
    keeping the cluster assignment.

    Every per-group file of a session is indexed by the same spike row — .res,
    each method's .spk and .fet, the .clu — and realign/nudge never reorder rows
    (spikeorder.h).  So the in-memory cluster tables, which index feature rows,
    stay valid when another method's features are loaded under them; only the
    feature values, the waveform source and what depends on them change.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klustersdoc.h"
#include "klustersview.h"
#include "data.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cstdint>

namespace {

/// Number of spike rows in a binary .fet (int32 nDims header, then int64 rows),
/// or -1 when the file is unreadable or its size does not divide evenly.
qint64 fetRowCount(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return -1;
    qint32 nDims = 0;
    // reinterpret_cast: QIODevice::read fills raw bytes; the .fet header is a
    // native-endian int32, read straight into it (as neurofileio does).
    if (f.read(reinterpret_cast<char*>(&nDims), sizeof(nDims)) != sizeof(nDims) || nDims < 2)
        return -1;                                   // need a feature column besides time
    const qint64 body = f.size() - static_cast<qint64>(sizeof(qint32));
    const qint64 row  = static_cast<qint64>(nDims) * static_cast<qint64>(sizeof(qint64));
    if (body < 0 || body % row != 0) return -1;
    return body / row;
}

} // namespace

QList<KlustersDoc::FeatureSpace> KlustersDoc::availableFeatureSpaces() const
{
    QList<FeatureSpace> spaces;
    if (!clusteringData || docUrl.isEmpty()) return spaces;

    const QString dir   = QFileInfo(docUrl).absolutePath();
    const QString stem  = dir + QDir::separator() + baseName;
    const int     group = electrodeGroupID.toInt();
    const qint64  nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    // The .clu's own .spk was validated at open, so its size is the geometry
    // every other space's .spk must match.
    const qint64  spkBytes = QFileInfo(origSpkPath).size();

    // Methods with a .fet for this group.  Strict MethodSpecific naming, so the
    // token in the file name IS the space; suffixed (stage-tagged) names are not
    // spaces of their own.
    QStringList methods;
    const QStringList names = QDir(dir).entryList(
        QStringList{ baseName + QStringLiteral(".fet.*.") + electrodeGroupID }, QDir::Files);
    for (const QString& n : names) {
        const neurosuite::custody::Anchor a = neurosuite::custody::parseAnchor(n.toStdString());
        if (!a.ok || a.type != "fet" || a.group != group || !a.suffix.empty()
            || a.method.empty() || QString::fromStdString(a.base) != baseName)
            continue;
        const QString m = QString::fromStdString(a.method);
        if (m != originMethod && !methods.contains(m)) methods << m;
    }
    methods.sort();
    methods.prepend(originMethod);                   // the .clu's own space first

    for (const QString& m : methods) {
        FeatureSpace s;
        s.method = m;
        if (m == originMethod) {
            // The pending copies carry this session's unsaved realign / nudge.
            s.fetPath   = QFileInfo::exists(pendingFetPath) ? pendingFetPath : origFetPath;
            s.spkPath   = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;
            s.spkMethod = QString::fromStdString(neurosuite::custody::methodOf(origSpkPath.toStdString()));
        } else {
            s.fetPath = QString::fromStdString(neurosuite::custody::methodPath(
                stem.toStdString(), "fet", m.toStdString(), group));
            const neurosuite::custody::Resolved spk = neurosuite::custody::resolve(
                stem.toStdString(), "spk", group, m.toStdString());
            s.spkPath   = QString::fromStdString(spk.path);
            s.spkMethod = QString::fromStdString(spk.method);
            // A space that reads the .clu space's own waveforms (a _D lag space
            // over the same token, or the standard fallback) must read its live
            // pending copy, which carries this session's realign / nudge.
            if (s.spkPath == origSpkPath && QFileInfo::exists(pendingSpkPath))
                s.spkPath = pendingSpkPath;
            if (!spk.found) {
                s.reason = tr("no .spk waveform file for %1").arg(m);
                spaces << s;
                continue;
            }
        }
        const qint64 rows = fetRowCount(s.fetPath);
        if (rows != nSpikes)
            s.reason = rows < 0 ? tr("unreadable feature file %1").arg(QFileInfo(s.fetPath).fileName())
                                : tr("%1 spikes in its .fet, the open .clu has %2").arg(rows).arg(nSpikes);
        else if (QFileInfo(s.spkPath).size() != spkBytes)
            s.reason = tr("its .spk (%1) does not match this group's spike count or geometry")
                           .arg(QFileInfo(s.spkPath).fileName());
        else
            s.usable = true;
        spaces << s;
    }
    return spaces;
}

bool KlustersDoc::switchFeatureSpace(const QString& method, QString& errorInformation)
{
    if (!clusteringData) {
        errorInformation = tr("No document is open.");
        return false;
    }
    if (method == activeMethod) return true;
    if (hasPendingRealign()) {
        errorInformation = tr("Save (or undo) the pending realignment before switching feature "
                              "space: undoing it would write %1 features into the loaded space.")
                           .arg(originMethod);
        return false;
    }

    FeatureSpace target;
    bool found = false;
    for (const FeatureSpace& s : availableFeatureSpaces())
        if (s.method == method) { target = s; found = true; break; }
    if (!found) {
        errorInformation = tr("No feature file for method %1 in this group.").arg(method);
        return false;
    }
    if (!target.usable) {
        errorInformation = tr("Cannot load %1: %2.").arg(method, target.reason);
        return false;
    }

    // The feature table is reallocated: every job that reads it, or the .spk,
    // must be gone first (the blocking quiesce, not the superseding one).
    if (viewList)
        for (KlustersView* v : *viewList) v->stopAllViewThreads();

    const QString prevFet = activeFetPath;
    const QString prevSpk = activeSpkPath;
    if (!clusteringData->replaceFeatureSpace(target.fetPath, target.spkPath, errorInformation))
        return false;
    if (childData && !childData->replaceFeatureSpace(target.fetPath, target.spkPath, errorInformation)) {
        // Keep both layers in one space: put the parent back (the active paths
        // are exactly what was loaded, pending copies included).
        QString rollbackError;
        if (!clusteringData->replaceFeatureSpace(prevFet, prevSpk, rollbackError))
            errorInformation += tr("\nRestoring the %1 space also failed: %2  "
                                   "Close and reopen the document.")
                                    .arg(activeMethod, rollbackError);
        return false;
    }
    activeMethod  = method;
    activeFetPath = target.fetPath;
    activeSpkPath = target.spkPath;
    return true;
}
