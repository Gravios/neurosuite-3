/***************************************************************************
    klustersdoc_featurespace.cpp
    ----------------------------
    KlustersDoc feature-space switching: list the method spaces a group has on
    disk (<base>.fet.<method>.<group>) and load another space's .fet/.spk while
    keeping the cluster assignment.

    Every per-group file of a session is indexed by the same spike row — .res,
    each method's .spk and .fet, the .clu — and realign/nudge rewrite each spike
    in its own row, never another.  So the in-memory cluster tables, which index
    feature rows, stay valid when another method's features are loaded under them; only the
    feature values, the waveform source and what depends on them change.

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klustersdoc.h"
#include "klustersview.h"
#include "data.h"
#include "pca_row_projection.h"     // klusters::projectSpikeOntoPca — the one projection kernel
#include "spkmemory.h"
#include "klustersdoc_internal.h"   // featureMethod

using klustersdoc_internal::featureMethod;
using klustersdoc_internal::methodIsStderiv;

#include <neurosuite/core/neurofileio.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <cstdint>
#include <numeric>
#include <map>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <new>
#include <utility>
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
                s.syncStale = syncedGeneration.value(s.fetPath) != spikeTimesGeneration.load();
                s.fetPath   = syncedFet.value();
                s.synced    = true;
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
    // A pending (unsaved) realignment does not stop a switch: it lives in the
    // .clu space's working copies, which the switch holds and restores.

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

    // Tables come from memory (preloadFeatureSpaces), read once if not held.
    const std::shared_ptr<const neurofileio::FetBinaryFile> fet = featuresInMemory(target.fetPath);
    if (!fet) {
        errorInformation = tr("Cannot read %1.").arg(QFileInfo(target.fetPath).fileName());
        return false;
    }

    // Leaving the .clu's own space: hold its live table (realign / nudge keep
    // it current) so that switching back reads nothing from disk.
    if (activeMethod == originMethod && !activeFetPath.isEmpty())
        holdFeatures(activeFetPath, clusteringData->featureTableCopy());

    // The feature table is reallocated: every job that reads it, or the .spk,
    // must be gone first (the blocking quiesce, not the superseding one).
    if (viewList)
        for (KlustersView* v : *viewList) v->stopAllViewThreads();
    const QString prevFet = activeFetPath;
    const QString prevSpk = activeSpkPath;
    if (!clusteringData->replaceFeatureSpace(*fet, target.fetPath, target.spkPath, errorInformation))
        return false;
    if (childData && !childData->replaceFeatureSpace(*fet, target.fetPath, target.spkPath,
                                                     errorInformation)) {
        // Keep both layers in one space: put the parent back (the active paths
        // are exactly what was loaded, pending copies included).
        QString rollbackError;
        const std::shared_ptr<const neurofileio::FetBinaryFile> prev = featuresInMemory(prevFet);
        if (!(prev ? clusteringData->replaceFeatureSpace(*prev, prevFet, prevSpk, rollbackError)
                   : clusteringData->replaceFeatureSpace(prevFet, prevSpk, rollbackError)))
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
        std::shared_ptr<const neurofileio::FetBinaryFile> oldFet;   // held in memory
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
        // The current table (held by preloadFeatureSpaces) supplies the extra
        // non-PCA columns.
        p.oldFet = featuresInMemory(p.fetSource);
        if (!p.oldFet) {
            log << who << ": not synced — cannot read " << QFileInfo(p.fetSource).fileName() << ".\n";
            return false;
        }
        p.nDims = p.oldFet->nFeatures;
        const int nPca = p.pca.nCh * p.pca.nComp;
        if (p.nDims - 1 - nPca < 0) {                 // last column is the timestamp
            log << who << ": not synced — the basis yields " << nPca
                << " features but the .fet has " << (p.nDims - 1) << ".\n";
            return false;
        }
    }

    // Everything is built under staging names first (".pending.sync"); the
    // working copies are replaced only once every file of the set is ready, so
    // a failure leaves the previous state -- committed files or an earlier
    // sync -- exactly as it was.
    const QString spkPending = spkCommitted + QStringLiteral(".pending");
    const QString stage      = QStringLiteral(".sync");
    std::vector<QString> staged;                    // staging files written so far
    auto dropStaged = [&]() {
        for (const QString& f : staged) QFile::remove(f);
        log << method << ": nothing synced; the files in use are unchanged.\n";
    };

    // ── Waveforms ────────────────────────────────────────────────────────
    QString spkSource;
    if (ownWaveforms) {
        spkSource = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;
    } else {
        // The space's own basis gives the spatial order if the .spk's waveform
        // token has none of its own (a space that exists only as _D).
        QString reLog;
        const QString spkStage = spkPending + stage;
        staged.push_back(spkStage);
        if (!reextractAllSpikesFromFil(spkStage, reLog, &plans.front().pca)) {
            log << method << ": waveforms not re-extracted — " << reLog << "\n";
            dropStaged();
            return false;
        }
        log << method << ": " << reLog << "\n";
        spkSource = spkStage;
    }

    // ── Features ─────────────────────────────────────────────────────────
    // Project every waveform through each space's own basis.  A _D lag space's
    // basis is an ordinary PCAE basis on a widened window, so the same kernel
    // serves it.  Extra non-PCA columns are carried over from the current .fet.
    const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    const int     timeDim = clusteringData->timeDimension();
    const size_t  spikeElems = static_cast<size_t>(nChan) * static_cast<size_t>(nSamp);
    std::shared_ptr<const QByteArray> spk = SpkMemoryCache::instance().get(spkSource);
    if (!spk || static_cast<qint64>(spk->size())
                    != nSpikes * static_cast<qint64>(spikeElems * sizeof(int16_t))) {
        log << method << ": cannot hold " << QFileInfo(spkSource).fileName()
            << " in memory, or it does not match this group.\n";
        dropStaged();
        return false;
    }
    // reinterpret_cast: the held bytes are the .spk's native-endian int16 records.
    const int16_t* waveforms = reinterpret_cast<const int16_t*>(spk->constData());
    std::vector<std::shared_ptr<neurofileio::FetBinaryFile>> tables;
    for (const Plan& p : plans) {
        const size_t nDims = static_cast<size_t>(p.nDims);
        auto out = std::make_shared<neurofileio::FetBinaryFile>();
        const QString fetStage = p.fetCommitted + QStringLiteral(".pending") + stage;
        staged.push_back(fetStage);
        bool ok = p.oldFet->nSpikes == nSpikes;
        if (ok) {
            out->nFeatures = p.nDims;
            out->nSpikes   = nSpikes;
            out->values    = p.oldFet->values;     // row[nPca .. nDims-1) keeps the extras
            out->ok        = true;
            for (int64_t i = 0; i < nSpikes; ++i) {
                const int16_t* wav = waveforms + static_cast<size_t>(i) * spikeElems;
                int64_t*       row = out->values.data() + static_cast<size_t>(i) * nDims;
                // .spk records are sample-major [s * nChan + ch]; a stderiv .spk
                // already holds the transformed waveform, projected as-is (as
                // realign does).
                klusters::projectSpikeOntoPca(
                    p.pca,
                    [&](int ch, int j) {
                        return static_cast<double>(
                            wav[static_cast<size_t>((p.pca.recShift + j) * nChan + ch)]);
                    },
                    row);
                row[nDims - 1] = static_cast<int64_t>(
                    clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim));
            }
        }
        // Written for Save (the working copy); the table itself stays in memory.
        if (!ok || !neurofileio::writeFetBinary(fetStage.toStdString(), out->nFeatures, out->values)) {
            log << p.method << ": writing the regenerated features failed.\n";
            dropStaged();
            return false;
        }
        tables.push_back(std::move(out));
    }

    // ── Swap the set in ──────────────────────────────────────────────────
    // The shown space's files are about to be replaced: every job reading the
    // feature table or the .spk must be gone before any rename.
    bool showsSynced = false;
    for (const Plan& p : plans) showsSynced = showsSynced || p.method == activeMethod;
    if (showsSynced && viewList)
        for (KlustersView* v : *viewList) v->stopAllViewThreads();

    auto promote = [](const QString& from, const QString& to) {
        QFile::remove(to);
        return std::rename(from.toLocal8Bit().constData(), to.toLocal8Bit().constData()) == 0;
    };
    bool promoted = true;
    if (!ownWaveforms) {
        promoted = promote(spkPending + stage, spkPending);
        if (promoted) {
            syncedPending.insert(spkCommitted, spkPending);
            SpkMemoryCache::instance().moved(spkPending + stage, spkPending);   // keep it held
        }
    }
    for (size_t k = 0; promoted && k < plans.size(); ++k) {
        const Plan&   p          = plans[k];
        const QString fetPending = p.fetCommitted + QStringLiteral(".pending");
        promoted = promote(fetPending + stage, fetPending);
        if (!promoted) break;
        holdFeatures(fetPending, tables[k]);
        syncedPending.insert(p.fetCommitted, fetPending);
        syncedGeneration.insert(p.fetCommitted, spikeTimesGeneration.load());
        log << p.method << ": " << nSpikes << " feature rows re-projected through "
            << QFileInfo(p.pcaPath).fileName() << ".\n";
    }
    if (!promoted) {
        // A rename failed part-way (a file-system error): no consistent set
        // remains, so the whole set goes back to its committed files.
        for (const QString& f : staged) QFile::remove(f);
        if (!ownWaveforms) { QFile::remove(spkPending); syncedPending.remove(spkCommitted); }
        for (const Plan& p : plans) {
            QFile::remove(p.fetCommitted + QStringLiteral(".pending"));
            syncedPending.remove(p.fetCommitted);
            syncedGeneration.remove(p.fetCommitted);
        }
        log << method << ": replacing the working copies failed; the space is back on its "
                         "saved files.\n";
    }
    if (promoted) {
        for (const Plan& p : plans) methodsDone.insert(p.method);
        setModified(true);
    }

    // The space being shown read the old copies (or, after a failed swap, the
    // ones just removed): reload it.  A reload failure does not undo the sync.
    if (showsSynced) {
        const QString shown = activeMethod;
        QString err;
        activeMethod.clear();                        // force the reload
        if (!switchFeatureSpace(shown, err)) {
            activeMethod = shown;
            log << shown << ": synced, but reloading it failed — " << err
                << "  Switch to another space and back.\n";
        }
    }
    return promoted;
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

// ─────────────────────────────────────────────────────────────────────────────
// Follow: after realign / nudge, bring every other space to the new spike times
// for the moved spikes only.
// ─────────────────────────────────────────────────────────────────────────────

void KlustersDoc::recordMovedRows(const std::vector<int64_t>& rows)
{
    if (rows.empty()) return;
    QMutexLocker lk(&movedRowsMutex);
    movedRows.insert(movedRows.end(), rows.begin(), rows.end());
}

void KlustersDoc::dropMovedSpikes()
{
    QMutexLocker lk(&movedRowsMutex);
    movedRows.clear();
}

bool KlustersDoc::followMovedSpikes(QString& logOut)
{
    std::vector<int64_t> rows;
    {
        QMutexLocker lk(&movedRowsMutex);
        rows.swap(movedRows);
    }
    if (rows.empty() || !clusteringData) return true;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    const QString dir     = QFileInfo(docUrl).absolutePath();
    const std::string stem = (dir + QDir::separator() + baseName).toStdString();
    const int     group   = electrodeGroupID.toInt();
    const int     nChan   = clusteringData->nbOfChannels();
    const int     nSamp   = clusteringData->nbSamplesPerWaveform();
    const int     timeDim = clusteringData->timeDimension();
    const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    const size_t  spikeElems = static_cast<size_t>(nChan) * static_cast<size_t>(nSamp);
    const qint64  spikeBytes = static_cast<qint64>(spikeElems * sizeof(int16_t));
    const quint64 now = spikeTimesGeneration.load();
    QTextStream log(&logOut);
    if (clusteringData->getCurrentChannels().size() < nChan || rows.back() >= nSpikes) {
        log << "follow: inconsistent group geometry; nothing followed.\n";
        return false;
    }
    auto timeOf = [&](int64_t row) {
        return static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim));
    };
    std::vector<char> moved(static_cast<size_t>(nSpikes), 0);
    for (int64_t r : rows) moved[static_cast<size_t>(r)] = 1;

    // ── Every other usable space, grouped by the waveform file it reads ───
    struct Space {
        QString method, fetSource, fetCommitted, pcaPath;
        neurosuite::core::PcaBasis pca;
        std::shared_ptr<const neurofileio::FetBinaryFile> table;
        bool fits = false, current = false;
        std::vector<int64_t> newRows;              // rows.size() x nFeatures, once projected
    };
    std::map<QString, std::vector<Space>> bySpk;     // committed .spk -> its spaces
    for (const FeatureSpace& s : availableFeatureSpaces()) {
        if (s.method == originMethod || !s.usable) continue;     // realign / nudge wrote it
        Space sp;
        sp.method       = s.method;
        sp.fetSource    = s.fetPath;
        sp.fetCommitted = QString::fromStdString(neurosuite::custody::methodPath(
            stem, "fet", s.method.toStdString(), group));
        sp.pcaPath = QString::fromStdString(neurosuite::custody::methodPath(
            stem, "pca", s.method.toStdString(), group));
        const QString spkCommitted = QString::fromStdString(
            neurosuite::custody::resolve(stem, "spk", group, s.method.toStdString()).path);
        const bool spkStderiv = neurosuite::custody::isStderivMethod(
            neurosuite::custody::methodOf(spkCommitted.toStdString()));
        sp.table = featuresInMemory(sp.fetSource);
        sp.fits = sp.table && sp.table->nSpikes == nSpikes
            && neurosuite::core::loadPca(sp.pcaPath.toStdString(), sp.pca) && sp.pca.valid()
            && sp.pca.nCh <= nChan && sp.pca.recShift >= 0
            && sp.pca.recShift + sp.pca.data2use <= nSamp
            && neurosuite::core::hasTemporalDiff(sp.pca.method) == spkStderiv
            && sp.table->nFeatures - 1 >= sp.pca.nCh * sp.pca.nComp;
        // Current before these moves: its features were computed at the
        // session's times for every spike that did not move.
        if (sp.fits) {
            const size_t nDims = static_cast<size_t>(sp.table->nFeatures);
            sp.current = true;
            for (int64_t r = 0; r < nSpikes && sp.current; ++r)
                if (!moved[static_cast<size_t>(r)]
                    && sp.table->values[static_cast<size_t>(r) * nDims + nDims - 1] != timeOf(r))
                    sp.current = false;
        }
        bySpk[spkCommitted].push_back(std::move(sp));
    }

    bool allOk = true;
    QStringList fullSync;
    std::vector<int16_t> wav(rows.size() * spikeElems);
    for (auto& entry : bySpk) {
        const QString&      spkCommitted = entry.first;
        std::vector<Space>& members      = entry.second;
        const QString       spkName      = QFileInfo(spkCommitted).fileName();

        // ── The group moves together, or not at all ──────────────────────
        bool fits = true, current = true;
        for (const Space& sp : members) { fits = fits && sp.fits; current = current && sp.current; }
        const QString spkMethod = featureMethod(spkCommitted);
        const bool ownWaveforms = (spkCommitted == origSpkPath);
        if (!ownWaveforms && !methodIsStderiv(spkMethod) && spkMethod != QLatin1String("standard"))
            fits = false;                                    // a storage we cannot re-cut
        if (!fits) {
            log << spkName << ": its spaces were not followed (a basis, its features or the "
                << "waveform format do not fit); they keep the old spike times.\n";
            allOk = false;
            continue;
        }
        if (!current) {                                       // behind already: full sync
            fullSync << members.front().method;
            continue;
        }

        // ── Moved waveforms, in this file's domain ───────────────────────
        bool ok = true;
        if (ownWaveforms) {
            // Realign / nudge already rewrote these rows of the working copy.
            QFile f(QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath);
            ok = f.open(QIODevice::ReadOnly);
            for (size_t k = 0; ok && k < rows.size(); ++k)
                // reinterpret_cast: raw int16 records read straight into the buffer.
                ok = f.seek(rows[k] * spikeBytes)
                  && f.read(reinterpret_cast<char*>(wav.data() + k * spikeElems), spikeBytes) == spikeBytes;
        } else {
            SpikeCutSpec cut;
            spikeCutSpecFor(spkMethod, &members.front().pca, cut, log);
            // Moved spikes are scattered over the recording: read one window at
            // a time instead of refilling the default 8 MB buffer per spike.
            cut.window.bufferBytes = static_cast<size_t>(cut.window.nSamp)
                                   * static_cast<size_t>(cut.window.totalNbChannels) * sizeof(int16_t);
            std::FILE* fil = std::fopen(rawRecordingPath().toLocal8Bit().constData(), "rb");
            ok = fil != nullptr;
            if (ok) {
                std::vector<size_t> byTime(rows.size());
                std::iota(byTime.begin(), byTime.end(), size_t{0});
                std::stable_sort(byTime.begin(), byTime.end(),
                                 [&](size_t x, size_t y) { return timeOf(rows[x]) < timeOf(rows[y]); });
                int64_t outOfRange = 0;
                int64_t emitted = 0;
                const int64_t ret = neurosuite::core::extractSpikeWindows(
                    fil, static_cast<int64_t>(rows.size()), cut.window,
                    [&](int64_t i) { return timeOf(rows[byTime[static_cast<size_t>(i)]]); },
                    [&](int64_t i, const int16_t* row) {
                        std::copy(row, row + spikeElems,
                                  wav.data() + byTime[static_cast<size_t>(i)] * spikeElems);
                        ++emitted;
                    },
                    &outOfRange);
                std::fclose(fil);
                ok = ret >= 0 && emitted == static_cast<int64_t>(rows.size());
                if (outOfRange) log << spkName << ": " << outOfRange
                                    << " moved window(s) off the recording → zeroed\n";
            }
        }
        if (!ok) {
            log << spkName << ": the moved waveforms could not be read; its spaces keep "
                << "the old spike times.\n";
            allOk = false;
            continue;
        }

        // ── Project every member's moved rows (tables untouched yet) ─────
        for (Space& sp : members) {
            const size_t nDims = static_cast<size_t>(sp.table->nFeatures);
            sp.newRows.resize(rows.size() * nDims);
            for (size_t k = 0; k < rows.size(); ++k) {
                int64_t* row = sp.newRows.data() + k * nDims;
                const int64_t* old = sp.table->values.data() + static_cast<size_t>(rows[k]) * nDims;
                std::copy(old, old + nDims, row);           // keeps the extra columns
                const int16_t* w = wav.data() + k * spikeElems;
                klusters::projectSpikeOntoPca(
                    sp.pca,
                    [&](int ch, int j) {
                        return static_cast<double>(
                            w[static_cast<size_t>((sp.pca.recShift + j) * nChan + ch)]);
                    },
                    row);
                row[nDims - 1] = timeOf(rows[k]);
            }
        }

        // ── Write the group: .spk rows, then each .fet ───────────────────
        const QString spkPending = spkCommitted + QStringLiteral(".pending");
        auto rollBackGroup = [&]() {
            // A partial group would pair waveforms and features from different
            // times: the whole group goes back to its saved files.
            if (!ownWaveforms) {
                QFile::remove(spkPending);
                syncedPending.remove(spkCommitted);
                SpkMemoryCache::instance().markWritten(spkPending);
            }
            for (const Space& sp : members) {
                const QString fp = sp.fetCommitted + QStringLiteral(".pending");
                QFile::remove(fp);
                fetMemory.remove(fp);
                syncedPending.remove(sp.fetCommitted);
                syncedGeneration.remove(sp.fetCommitted);
            }
            log << spkName << ": writing the followed spikes failed; its spaces are back on "
                << "their saved files (Sync Feature Spaces retries).\n";
            allOk = false;
        };
        if (!ownWaveforms) {
            if (!syncedPending.contains(spkCommitted) || !QFileInfo::exists(spkPending)) {
                QFile::remove(spkPending);
                ok = QFile::copy(spkCommitted, spkPending);    // first change: start the copy
            }
            QFile out(spkPending);
            ok = ok && out.open(QIODevice::ReadWrite);
            for (size_t k = 0; ok && k < rows.size(); ++k)
                // reinterpret_cast: raw int16 records written straight from the buffer.
                ok = out.seek(rows[k] * spikeBytes)
                  && out.write(reinterpret_cast<const char*>(wav.data() + k * spikeElems),
                               spikeBytes) == spikeBytes;
            out.close();
            if (!ok) { rollBackGroup(); continue; }
            syncedPending.insert(spkCommitted, spkPending);
            // Keep the held bytes in step; a file some reader is on is re-read
            // instead of written under it.
            const bool live = (spkPending == clusteringData->getSpkFileName())
                           || (childData && spkPending == childData->getSpkFileName());
            for (size_t k = 0; k < rows.size(); ++k) {
                if (live) { SpkMemoryCache::instance().markWritten(spkPending); break; }
                SpkMemoryCache::instance().patch(spkPending, rows[k] * spikeBytes,
                                                 wav.data() + k * spikeElems, spikeBytes);
            }
        }
        bool fetOk = true;
        for (Space& sp : members) {
            const QString fetPending = sp.fetCommitted + QStringLiteral(".pending");
            const size_t  nDims      = static_cast<size_t>(sp.table->nFeatures);
            const bool    hadCopy    = syncedPending.contains(sp.fetCommitted)
                                       && QFileInfo::exists(fetPending);
            if (hadCopy) {
                QFile out(fetPending);
                fetOk = out.open(QIODevice::ReadWrite);
                const qint64 rowBytes = static_cast<qint64>(nDims * sizeof(int64_t));
                for (size_t k = 0; fetOk && k < rows.size(); ++k)
                    // reinterpret_cast: the .fet row's int64 values written as raw bytes.
                    fetOk = out.seek(4 + rows[k] * rowBytes)
                         && out.write(reinterpret_cast<const char*>(sp.newRows.data() + k * nDims),
                                      rowBytes) == rowBytes;
            } else {
                // First change: the whole table, with the new rows, as the copy.
                neurofileio::FetBinaryFile full(*sp.table);
                for (size_t k = 0; k < rows.size(); ++k)
                    std::copy(sp.newRows.begin() + static_cast<std::ptrdiff_t>(k * nDims),
                              sp.newRows.begin() + static_cast<std::ptrdiff_t>((k + 1) * nDims),
                              full.values.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(rows[k]) * nDims));
                const QString tmp = fetPending + QStringLiteral(".tmp");
                fetOk = neurofileio::writeFetBinary(tmp.toStdString(), full.nFeatures, full.values);
                if (fetOk) {
                    QFile::remove(fetPending);
                    fetOk = std::rename(tmp.toLocal8Bit().constData(),
                                        fetPending.toLocal8Bit().constData()) == 0;
                }
                if (!fetOk) QFile::remove(tmp);
            }
            if (!fetOk) break;
            syncedPending.insert(sp.fetCommitted, fetPending);
        }
        if (!fetOk) { rollBackGroup(); continue; }

        // ── Written: bring the held tables along ─────────────────────────
        for (Space& sp : members) {
            const QString fetPending = sp.fetCommitted + QStringLiteral(".pending");
            const size_t  nDims      = static_cast<size_t>(sp.table->nFeatures);
            // In place only for a table that is already the working copy's;
            // the committed file's table must keep its own rows.
            std::shared_ptr<neurofileio::FetBinaryFile> t =
                (sp.fetSource == fetPending)
                    ? std::const_pointer_cast<neurofileio::FetBinaryFile>(sp.table)
                    : std::make_shared<neurofileio::FetBinaryFile>(*sp.table);
            for (size_t k = 0; k < rows.size(); ++k)
                std::copy(sp.newRows.begin() + static_cast<std::ptrdiff_t>(k * nDims),
                          sp.newRows.begin() + static_cast<std::ptrdiff_t>((k + 1) * nDims),
                          t->values.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(rows[k]) * nDims));
            holdFeatures(fetPending, std::move(t));
            syncedGeneration.insert(sp.fetCommitted, now);
            log << sp.method << ": " << rows.size() << " moved spike(s) followed.\n";
        }
        setModified(true);
    }
    log.flush();

    // ── Spaces that were already behind: full sync (each group once) ──────
    QSet<QString> done;
    for (const QString& m : fullSync)
        allOk = syncOneSpace(m, done, logOut) && allOk;
    return allOk;
}

std::shared_ptr<const neurofileio::FetBinaryFile> KlustersDoc::featuresInMemory(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) return nullptr;
    const auto it = fetMemory.constFind(path);
    if (it != fetMemory.constEnd() && it->size == info.size() && it->modified == info.lastModified())
        return it->fet;
    std::shared_ptr<neurofileio::FetBinaryFile> fet;
    try {
        fet = std::make_shared<neurofileio::FetBinaryFile>(
            neurofileio::readFetBinary(path.toStdString()));
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
    if (!fet->ok) return nullptr;
    holdFeatures(path, fet);
    return fet;
}

void KlustersDoc::holdFeatures(const QString& path,
                               std::shared_ptr<const neurofileio::FetBinaryFile> fet)
{
    const QFileInfo info(path);
    FetInMemory& m = fetMemory[path];
    m.size     = info.size();
    m.modified = info.lastModified();
    m.fet      = std::move(fet);
}

qint64 KlustersDoc::preloadFeatureSpaces()
{
    if (!clusteringData) return 0;
    SpkMemoryCache& spkMemory = SpkMemoryCache::instance();
    const QString liveSpk = clusteringData->getSpkFileName();
    QStringList keepSpk{ liveSpk }, keepFet;
    if (childData) keepSpk << childData->getSpkFileName();
    for (const FeatureSpace& s : availableFeatureSpaces()) {
        if (!s.usable) continue;
        keepSpk << s.spkPath;
        keepFet << s.fetPath;
        // The shown space is loaded already: its table lives in Data and its
        // reader holds its .spk (re-reading it after each realign / nudge
        // would cost a full file read per edit).
        if (s.method != activeMethod) featuresInMemory(s.fetPath);
        if (s.spkPath != liveSpk)     spkMemory.get(s.spkPath);
    }
    spkMemory.keepOnly(keepSpk);
    for (auto it = fetMemory.begin(); it != fetMemory.end();)
        it = keepFet.contains(it.key()) ? std::next(it) : fetMemory.erase(it);

    qint64 held = spkMemory.bytesHeld();
    for (const FetInMemory& m : std::as_const(fetMemory)) held += m.size;
    return held;
}

void KlustersDoc::releaseFeatureSpaces()
{
    SpkMemoryCache::instance().clear();
    fetMemory.clear();
}

void KlustersDoc::discardSyncedFeatureSpaces()
{
    for (auto it = syncedPending.constBegin(); it != syncedPending.constEnd(); ++it)
        QFile::remove(it.value());
    syncedPending.clear();
    syncedGeneration.clear();
}
