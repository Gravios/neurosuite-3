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
#include "pca_row_projection.h"     // klusters::projectSpikeOntoPca — the one projection kernel

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cstdint>
#include <cstdio>
#include <vector>

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
            // Synced this session (syncFeatureSpace): read the working copies.
            const auto syncedFet = syncedPending.constFind(s.fetPath);
            if (syncedFet != syncedPending.constEnd() && QFileInfo::exists(syncedFet.value())) {
                s.fetPath = syncedFet.value();
                s.synced  = true;
            }
            const auto syncedSpk = syncedPending.constFind(s.spkPath);
            if (syncedSpk != syncedPending.constEnd() && QFileInfo::exists(syncedSpk.value()))
                s.spkPath = syncedSpk.value();
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

// ─────────────────────────────────────────────────────────────────────────────
// Sync: re-extract another space's .spk and re-project its .fet at this
// session's spike times, into that space's .pending working copies.
// ─────────────────────────────────────────────────────────────────────────────

bool KlustersDoc::syncOneSpace(const QString& method, QSet<QString>& methodsDone, QString& logOut)
{
    QTextStream log(&logOut);
    // Realign / nudge keep the .clu's own space current; a space sharing a .spk
    // with one synced earlier in this pass was synced with it.
    if (method == originMethod || methodsDone.contains(method)) return true;

    const QList<FeatureSpace> spaces = availableFeatureSpaces();
    const FeatureSpace* target = nullptr;
    for (const FeatureSpace& s : spaces)
        if (s.method == method) { target = &s; break; }
    if (!target) { log << method << ": no feature file in this group.\n"; return false; }
    if (!target->usable) { log << method << ": skipped — " << target->reason << ".\n"; return false; }

    const QString dir   = QFileInfo(docUrl).absolutePath();
    const std::string stem = (dir + QDir::separator() + baseName).toStdString();
    const int     group = electrodeGroupID.toInt();
    const int     nChan = clusteringData->nbOfChannels();
    const int     nSamp = clusteringData->nbSamplesPerWaveform();
    auto committedSpkOf = [&](const QString& m) {
        return QString::fromStdString(
            neurosuite::custody::resolve(stem, "spk", group, m.toStdString()).path);
    };
    const QString spkCommitted = committedSpkOf(method);
    const bool    ownWaveforms = (spkCommitted == origSpkPath);

    // ── Who is synced together ───────────────────────────────────────────
    // A re-extracted .spk replaces the committed one at Save for EVERY space
    // reading it, so every usable space over it is re-projected in the same
    // pass; otherwise a space left out would pair its old .fet with new
    // waveforms.  A space over the .clu's own waveforms reads the live pending
    // copy, which realign / nudge keep current, so it is synced alone.
    struct Plan {
        QString method, fetSource, fetCommitted, pcaPath;
        neurosuite::core::PcaBasis pca;
        int32_t nDims = 0;
    };
    std::vector<Plan> plans;
    for (const FeatureSpace& s : spaces) {
        if (s.method == originMethod || !s.usable) continue;
        if (s.method != method && (ownWaveforms || committedSpkOf(s.method) != spkCommitted))
            continue;
        Plan p;
        p.method       = s.method;
        p.fetSource    = s.fetPath;
        p.fetCommitted = QString::fromStdString(neurosuite::custody::methodPath(
            stem, "fet", s.method.toStdString(), group));
        p.pcaPath      = QString::fromStdString(neurosuite::custody::methodPath(
            stem, "pca", s.method.toStdString(), group));
        if (s.method == method) plans.insert(plans.begin(), p);
        else                    plans.push_back(p);
    }

    // ── Checks first: nothing is written unless every space can be ────────
    const bool spkIsStderiv = neurosuite::custody::isStderivMethod(
        neurosuite::custody::methodOf(spkCommitted.toStdString()));
    for (Plan& p : plans) {
        const QString who = p.method == method
            ? method
            : tr("%1 (it shares %2 with %3)").arg(method, QFileInfo(spkCommitted).fileName(), p.method);
        const QString pcaName = QFileInfo(p.pcaPath).fileName();
        if (!neurosuite::core::loadPca(p.pcaPath.toStdString(), p.pca) || !p.pca.valid()) {
            log << who << ": not synced — no readable basis " << pcaName << ".\n";
            return false;
        }
        if (p.pca.nCh > nChan || p.pca.recShift < 0 || p.pca.recShift + p.pca.data2use > nSamp) {
            log << who << ": not synced — basis " << pcaName << " does not fit this group's "
                << "waveforms (" << p.pca.nCh << " ch, window " << p.pca.recShift << "+"
                << p.pca.data2use << " of " << nSamp << " samples).\n";
            return false;
        }
        // The basis must have been trained in the waveform file's domain: a
        // stderiv basis projects stderiv waveforms, a standard one raw ones.
        if (neurosuite::core::hasTemporalDiff(p.pca.method) != spkIsStderiv) {
            log << who << ": not synced — basis " << pcaName << " and waveform file "
                << QFileInfo(spkCommitted).fileName() << " are in different domains.\n";
            return false;
        }
        QFile f(p.fetSource);
        // reinterpret_cast: QIODevice::read fills raw bytes (native-endian int32 header).
        if (!f.open(QIODevice::ReadOnly)
            || f.read(reinterpret_cast<char*>(&p.nDims), sizeof(p.nDims)) != sizeof(p.nDims)) {
            log << who << ": not synced — cannot read " << QFileInfo(p.fetSource).fileName() << ".\n";
            return false;
        }
        const int nPca = p.pca.nCh * p.pca.nComp;
        if (p.nDims - 1 - nPca < 0) {                 // last column is the timestamp
            log << who << ": not synced — the basis yields " << nPca
                << " features but the .fet has " << (p.nDims - 1) << ".\n";
            return false;
        }
    }

    // The shown space's files are about to be replaced: every job reading the
    // feature table or the .spk must be gone before any rename.
    bool showsSynced = false;
    for (const Plan& p : plans) showsSynced = showsSynced || p.method == activeMethod;
    if (showsSynced && viewList)
        for (KlustersView* v : *viewList) v->stopAllViewThreads();

    // On any later failure, fall the whole set back to its committed files:
    // a partial set would pair waveforms and features from different times.
    const QString spkPending = spkCommitted + QStringLiteral(".pending");
    auto rollBack = [&]() {
        if (!ownWaveforms) {
            QFile::remove(spkPending);
            syncedPending.remove(spkCommitted);
        }
        for (const Plan& p : plans) {
            QFile::remove(p.fetCommitted + QStringLiteral(".pending"));
            syncedPending.remove(p.fetCommitted);
        }
        log << method << ": nothing synced; the saved files stay in use.\n";
    };

    // ── Waveforms ────────────────────────────────────────────────────────
    QString spkSource;
    if (ownWaveforms) {
        spkSource = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;
    } else {
        QString reLog;
        if (!reextractAllSpikesFromFil(spkPending, reLog)) {
            log << method << ": waveforms not re-extracted — " << reLog << "\n";
            rollBack();
            return false;
        }
        syncedPending.insert(spkCommitted, spkPending);
        log << method << ": " << reLog << "\n";
        spkSource = spkPending;
    }

    // ── Features ─────────────────────────────────────────────────────────
    // Project every waveform through each space's own basis.  A _D lag space's
    // basis is an ordinary PCAE basis on a widened window, so the same kernel
    // serves it.  Extra non-PCA columns are carried over from the current .fet.
    const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    const int     timeDim = clusteringData->timeDimension();
    for (const Plan& p : plans) {
        const QString fetPending = p.fetCommitted + QStringLiteral(".pending");
        const QString fetTmp     = fetPending + QStringLiteral(".tmp");
        std::FILE* oldFet = std::fopen(p.fetSource.toLocal8Bit().constData(), "rb");
        std::FILE* spkIn  = std::fopen(spkSource.toLocal8Bit().constData(), "rb");
        std::FILE* out    = std::fopen(fetTmp.toLocal8Bit().constData(), "wb");
        std::vector<int16_t> wav(static_cast<size_t>(nChan) * static_cast<size_t>(nSamp));
        std::vector<int64_t> row(static_cast<size_t>(p.nDims));
        int32_t nDims = 0;
        bool ok = oldFet && spkIn && out
               && std::fread(&nDims, sizeof(nDims), 1, oldFet) == 1 && nDims == p.nDims
               && std::fwrite(&nDims, sizeof(nDims), 1, out) == 1;
        for (int64_t i = 0; ok && i < nSpikes; ++i) {
            ok = std::fread(wav.data(), sizeof(int16_t), wav.size(), spkIn) == wav.size()
              && std::fread(row.data(), sizeof(int64_t), row.size(), oldFet) == row.size();
            if (!ok) break;
            // .spk records are sample-major [s * nChan + ch]; a stderiv .spk
            // already holds the transformed waveform, projected as-is (as
            // realign does).
            klusters::projectSpikeOntoPca(
                p.pca,
                [&](int ch, int j) {
                    return static_cast<double>(
                        wav[static_cast<size_t>((p.pca.recShift + j) * nChan + ch)]);
                },
                row.data());
            // row[nPca .. nDims-1) keeps the old extra columns.
            row[static_cast<size_t>(nDims - 1)] = static_cast<int64_t>(
                clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim));
            ok = std::fwrite(row.data(), sizeof(int64_t), row.size(), out) == row.size();
        }
        if (oldFet) std::fclose(oldFet);
        if (spkIn)  std::fclose(spkIn);
        if (out && std::fclose(out) != 0) ok = false;
        if (!ok || std::rename(fetTmp.toLocal8Bit().constData(),
                               fetPending.toLocal8Bit().constData()) != 0) {
            QFile::remove(fetTmp);
            log << p.method << ": writing the regenerated features failed.\n";
            rollBack();
            return false;
        }
        syncedPending.insert(p.fetCommitted, fetPending);
        log << p.method << ": " << nSpikes << " feature rows re-projected through "
            << QFileInfo(p.pcaPath).fileName() << ".\n";
    }
    for (const Plan& p : plans) methodsDone.insert(p.method);
    setModified(true);

    // The space being shown read the old copies: reload it.
    if (showsSynced) {
        const QString shown = activeMethod;
        QString err;
        activeMethod.clear();                        // force the reload
        if (!switchFeatureSpace(shown, err)) {
            activeMethod = shown;
            log << shown << ": synced, but reloading it failed — " << err << "\n";
            return false;
        }
    }
    return true;
}

bool KlustersDoc::syncFeatureSpace(const QString& method, QString& logOut)
{
    QSet<QString> methodsDone;
    return syncOneSpace(method, methodsDone, logOut);
}

bool KlustersDoc::syncAllFeatureSpaces(QString& logOut)
{
    QSet<QString> methodsDone;
    bool allOk = true;
    for (const FeatureSpace& s : availableFeatureSpaces()) {
        if (s.method == originMethod) continue;
        if (!s.usable) {                             // cannot be loaded anyway: not a sync failure
            QTextStream(&logOut) << s.method << ": skipped — " << s.reason << ".\n";
            continue;
        }
        allOk = syncOneSpace(s.method, methodsDone, logOut) && allOk;
    }
    return allOk;
}

void KlustersDoc::discardSyncedFeatureSpaces()
{
    for (auto it = syncedPending.constBegin(); it != syncedPending.constEnd(); ++it)
        QFile::remove(it.value());
    syncedPending.clear();
}
