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
#include <neurosuite/core/fileops.hpp>
#include "klustersview.h"
#include "data.h"
#include "pca_row_projection.h"     // klusters::projectSpikeOntoPca — the one projection kernel
#include <neurosuite/core/pca_fit.hpp>      // fitPca: the basis of a space built here
#include <neurosuite/core/supersample.hpp>  // the super-sampled space
#include "spkmemory.h"
#include "klustersdoc_internal.h"   // featureMethod

using klustersdoc_internal::featureMethod;
using klustersdoc_internal::methodIsStderiv;
using klustersdoc_internal::resolveFeature;

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
    // every other space's .spk must match -- per super-sampling factor: a
    // _U<factor> .spk holds factor x the samples.
    const qint64  spkBytes = QFileInfo(origSpkPath).size();
    const int     origFactor = std::max(1, neurosuite::custody::supersampleOf(
                                               neurosuite::custody::methodOf(origSpkPath.toStdString())));
    const qint64  spkBytesBase = spkBytes / origFactor;

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
        {
            neurosuite::core::PcaBasis b;
            const QString pca = klustersdoc_internal::resolveFeature(stem, QStringLiteral("pca"), electrodeGroupID, m);
            s.projection = neurosuite::core::loadPca(pca.toStdString(), b) && b.valid();
        }
        const qint64 rows = fetRowCount(s.fetPath);
        if (rows != nSpikes)
            s.reason = rows < 0 ? tr("unreadable feature file %1").arg(QFileInfo(s.fetPath).fileName())
                                : tr("%1 spikes in its .fet, the open .clu has %2").arg(rows).arg(nSpikes);
        else if (QFileInfo(s.spkPath).size()
                 != spkBytesBase * std::max(1, neurosuite::custody::supersampleOf(s.spkMethod.toStdString())))
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

// Reload the shown space from its files as they are now (its working copies
// once they exist).  Not by clearing activeMethod: for an untagged session the
// .clu's own token IS "", and switchFeatureSpace would then take the borrowed
// table for the origin's and hold it under its name.
bool KlustersDoc::reloadShownSpace(QString& error)
{
    const QString shown = activeMethod;
    activeMethod = QStringLiteral("\u0001reload");          // never a real token
    if (!switchFeatureSpace(shown, error)) { activeMethod = shown; return false; }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// The working set of a space (SpaceFiles): what a realign or nudge reads and
// rewrites when it runs in that space.
// ─────────────────────────────────────────────────────────────────────────────

// Before a realign or nudge in the shown space: its working copies exist
// (seeded now if not) and the Data reads them, so the in-place rewrite lands in
// what is on screen.  The .clu's own space is always ready (initPendingFiles).
bool KlustersDoc::prepareShownSpaceForRealign(QString* error)
{
    if (!clusteringData) { if (error) *error = tr("No document is open."); return false; }
    if (activeMethod == originMethod) return true;
    if (neurosuite::custody::parseMethodToken(activeMethod.toStdString()).lag >= 1) {
        if (error) *error = tr("Realign and nudge do not run in a lag (_D) feature space; show its waveform space (%1) first.")
                                .arg(QString::fromStdString(neurosuite::custody::waveformToken(activeMethod.toStdString())));
        return false;
    }
    const SpaceFiles ws = workingSetOf(activeMethod, error, /*seed*/ true);
    if (!ws.valid) return false;
    if (activeSpkPath != ws.spkWork || activeFetPath != ws.fetWork) {
        QString err;
        if (!reloadShownSpace(err)) { if (error) *error = err; return false; }
    }
    return activeSpkPath == ws.spkWork && activeFetPath == ws.fetWork;
}

KlustersDoc::SpaceFiles KlustersDoc::workingSetOf(const QString& method, QString* error, bool seed)
{
    SpaceFiles ws;
    auto fail = [&](const QString& why) { if (error) *error = why; ws.valid = false; return ws; };
    if (!clusteringData || docUrl.isEmpty()) return fail(tr("No document is open."));
    ws.method   = method;
    ws.isOrigin = (method == originMethod);
    const QString dir   = QFileInfo(docUrl).absolutePath();
    const QString stemQ = dir + QDir::separator() + baseName;
    const std::string stem = stemQ.toStdString();
    const int group = electrodeGroupID.toInt();

    if (ws.isOrigin) {
        ws.spkCommitted = origSpkPath;
        ws.fetCommitted = origFetPath;
        // The pending triple (initPendingFiles); the originals when the seed failed.
        ws.spkWork = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;
        ws.fetWork = QFileInfo::exists(pendingFetPath) ? pendingFetPath : origFetPath;
        ws.ownsWaveforms = true;
    } else {
        const neurosuite::custody::Resolved spk =
            neurosuite::custody::resolve(stem, "spk", group, method.toStdString());
        if (!spk.found) return fail(tr("no .spk waveform file for %1").arg(method));
        ws.spkCommitted = QString::fromStdString(spk.path);
        ws.fetCommitted = QString::fromStdString(neurosuite::custody::methodPath(stem, "fet", method.toStdString(), group));
        if (!QFileInfo::exists(ws.fetCommitted)) return fail(tr("no feature file for %1 in this group").arg(method));
        ws.ownsWaveforms = (ws.spkCommitted == origSpkPath);
        if (ws.ownsWaveforms) {
            ws.spkWork = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;   // the origin's live copy
        } else {
            const QString copy = syncedPending.value(ws.spkCommitted);
            ws.spkWork = (!copy.isEmpty() && QFileInfo::exists(copy)) ? copy : QString();
        }
        {
            const QString copy = syncedPending.value(ws.fetCommitted);
            ws.fetWork = (!copy.isEmpty() && QFileInfo::exists(copy)) ? copy : QString();
        }
        if (seed && (ws.fetWork.isEmpty() || ws.spkWork.isEmpty())) {
            // No working copies yet.  Current (its features' times are the
            // session's at every spike): a plain copy.  Behind: a full sync.
            bool current = false;
            if (const auto table = featuresInMemory(ws.fetCommitted)) {
                const int64_t n = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
                const int     timeDim = clusteringData->timeDimension();
                const size_t  nd = static_cast<size_t>(table->nFeatures);
                current = table->nSpikes == n;
                // ... and, super-sampled, the session's phases (the loaded or held
                // _U<f> table's; its own when it is the only one, which is current).
                const int factor = std::max(1, neurosuite::custody::supersampleOf(
                                                   neurosuite::custody::methodOf(ws.spkCommitted.toStdString())));
                const std::vector<int64_t> ticks = factor > 1 ? supersampledTicks(factor) : std::vector<int64_t>{};
                const bool withPhase = static_cast<int64_t>(ticks.size()) == n;
                for (int64_t r = 0; r < n && current; ++r) {
                    const int64_t* row = table->values.data() + static_cast<size_t>(r) * nd;
                    const int64_t t = static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(r + 1), timeDim));
                    if (row[nd - 1] != t) current = false;
                    else if (withPhase && t * factor + row[nd - 2] != ticks[static_cast<size_t>(r)]) current = false;
                }
            }
            bool ok = current;
            if (current) {
                if (ws.fetWork.isEmpty()) {
                    const QString copy = ws.fetCommitted + QStringLiteral(".pending");
                    QFile::remove(copy);
                    ok = QFile::copy(ws.fetCommitted, copy);
                    if (ok) {
                        syncedPending.insert(ws.fetCommitted, copy);
                        syncedGeneration.insert(ws.fetCommitted, spikeTimesGeneration.load());
                        if (const auto t = featuresInMemory(ws.fetCommitted)) holdFeatures(copy, t);
                        ws.fetWork = copy;
                    }
                }
                if (ok && ws.spkWork.isEmpty()) {
                    const QString copy = ws.spkCommitted + QStringLiteral(".pending");
                    QFile::remove(copy);
                    ok = QFile::copy(ws.spkCommitted, copy);
                    if (ok) { syncedPending.insert(ws.spkCommitted, copy); ws.spkWork = copy; }
                }
            }
            if (!ok) {
                QString log;
                QSet<QString> done;
                if (!syncOneSpace(method, done, log))
                    return fail(tr("%1 could not be brought to the session's spike times:\n%2").arg(method, log.trimmed()));
                ws.spkWork = ws.ownsWaveforms ? ws.spkWork : syncedPending.value(ws.spkCommitted);
                ws.fetWork = syncedPending.value(ws.fetCommitted);
            }
        }
        if (ws.spkWork.isEmpty()) ws.spkWork = ws.spkCommitted;
        if (ws.fetWork.isEmpty()) ws.fetWork = ws.fetCommitted;
    }
    ws.pcaPath = resolveFeature(stemQ, QStringLiteral("pca"), electrodeGroupID, method);
    ws.factor  = std::max(1, neurosuite::custody::supersampleOf(
                                  neurosuite::custody::methodOf(ws.spkCommitted.toStdString())));
    ws.spkTransformed = methodIsStderiv(featureMethod(ws.spkCommitted));
    ws.fetStderiv     = methodIsStderiv(method);
    ws.valid = QFileInfo::exists(ws.spkWork) && QFileInfo::exists(ws.fetWork);
    if (!ws.valid && error) *error = tr("%1: its working copies are missing").arg(method);
    return ws;
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
    auto committedSpkOf = [&](const QString& m) {
        return QString::fromStdString(
            neurosuite::custody::resolve(stem, "spk", group, m.toStdString()).path);
    };
    const QString spkCommitted = committedSpkOf(method);
    const bool    ownWaveforms = (spkCommitted == origSpkPath);
    // The target's waveform geometry: the group's, times its super-sampling factor
    // (a _U<factor> .spk), whatever space is shown.
    const int     spkFactor = std::max(1, neurosuite::custody::supersampleOf(
                                              neurosuite::custody::methodOf(spkCommitted.toStdString())));
    const int     nSamp = clusteringData->baseNbSamplesPerWaveform() * spkFactor;

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
        bool detached = false;               // no basis: its features stay, its time column follows
        std::shared_ptr<const neurofileio::FetBinaryFile> oldFet;   // held in memory
    };
    std::vector<Plan> plans;
    for (const FeatureSpace& s : spaces) {
        if (s.method == originMethod || !s.usable) continue;
        if (s.method != method && (ownWaveforms || committedSpkOf(s.method) != spkCommitted))
            continue;
        Plan p;
        p.method       = s.method;
        p.detached     = !s.projection;
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
        if (p.detached) {
            // Nothing to check against a basis: its features stay, its time
            // (and phase) column is rewritten at the session's times.
            p.oldFet = featuresInMemory(p.fetSource);
            if (!p.oldFet) {
                log << who << ": not synced — cannot read " << QFileInfo(p.fetSource).fileName() << ".\n";
                return false;
            }
            p.nDims = p.oldFet->nFeatures;
            log << p.method << ": not a projection of the waveforms (no basis " << pcaName
                << "): its features stay; its time column follows the spike times.\n";
            continue;
        }
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
    // The first projection plan leads the re-cut (its basis gives the window and
    // transform); a set of detached spaces alone needs no waveforms at all.
    const Plan* lead = nullptr;
    for (const Plan& p : plans) if (!p.detached) { lead = &p; break; }
    QString spkSource;
    std::vector<int8_t> phases;                   // a super-sampled target's, per spike
    if (!lead) {
        log << method << ": no waveforms to re-cut (no space over " << QFileInfo(spkCommitted).fileName()
            << " is a projection).\n";
    } else if (ownWaveforms) {
        spkSource = QFileInfo::exists(pendingSpkPath) ? pendingSpkPath : origSpkPath;
    } else {
        // The space's own basis gives the spatial order if the .spk's waveform
        // token has none of its own (a space that exists only as _D).
        QString reLog;
        const QString spkStage = spkPending + stage;
        staged.push_back(spkStage);
        // A super-sampled target: each spike's phase comes from the shared tick
        // .res of this factor when it agrees with the session's times (spike
        // times are one thing for every space), else from the space's own column
        // (the last feature before time); carried into the re-cut and into the
        // re-projected table's phase column.
        if (spkFactor > 1) {
            const Plan& p0 = *lead;
            const int64_t nd = p0.nDims;
            const int64_t nS = p0.oldFet->nSpikes;
            const int timeDimL = clusteringData->timeDimension();
            phases.resize(static_cast<size_t>(nS));
            for (int64_t i = 0; i < nS; ++i)
                phases[static_cast<size_t>(i)] = static_cast<int8_t>(
                    p0.oldFet->values[static_cast<size_t>(i) * static_cast<size_t>(nd) + static_cast<size_t>(nd - 2)]);
            const QString tickRes = supersampledResPath(spkFactor);
            const std::vector<int64_t> have = QFileInfo::exists(tickRes)
                ? neurofileio::readResBinary(tickRes.toStdString()) : std::vector<int64_t>{};
            if (static_cast<int64_t>(have.size()) == nS) {
                int64_t taken = 0;
                for (int64_t i = 0; i < nS; ++i) {
                    const int64_t t = static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(i + 1), timeDimL));
                    const int64_t k = have[static_cast<size_t>(i)];
                    if (k / spkFactor == t && k >= t * spkFactor) { phases[static_cast<size_t>(i)] = static_cast<int8_t>(k - t * spkFactor); ++taken; }
                }
                log << method << ": phases of " << taken << " spike(s) from " << QFileInfo(tickRes).fileName() << "\n";
            }
        }
        if (!reextractAllSpikesFromFil(spkStage, reLog, &lead->pca, phases.empty() ? nullptr : &phases)) {
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
    std::shared_ptr<const QByteArray> spk = lead ? SpkMemoryCache::instance().get(spkSource) : nullptr;
    if (lead && (!spk || static_cast<qint64>(spk->size())
                    != nSpikes * static_cast<qint64>(spikeElems * sizeof(int16_t)))) {
        log << method << ": cannot hold " << QFileInfo(spkSource).fileName()
            << " in memory, or it does not match this group.\n";
        dropStaged();
        return false;
    }
    // reinterpret_cast: the held bytes are the .spk's native-endian int16 records.
    const int16_t* waveforms = spk ? reinterpret_cast<const int16_t*>(spk->constData()) : nullptr;
    std::vector<std::shared_ptr<neurofileio::FetBinaryFile>> tables;
    for (const Plan& p : plans) {
        const size_t nDims = static_cast<size_t>(p.nDims);
        auto out = std::make_shared<neurofileio::FetBinaryFile>();
        const QString fetStage = p.fetCommitted + QStringLiteral(".pending") + stage;
        staged.push_back(fetStage);
        bool ok = p.oldFet->nSpikes == nSpikes;
        if (ok && p.detached) {
            // A detached space: the features stay, the time (and phase) column follows.
            out->nFeatures = p.nDims;
            out->nSpikes   = nSpikes;
            out->values    = p.oldFet->values;
            out->ok        = true;
            for (int64_t i = 0; i < nSpikes; ++i) {
                int64_t* row = out->values.data() + static_cast<size_t>(i) * nDims;
                row[nDims - 1] = static_cast<int64_t>(
                    clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim));
                if (!phases.empty() && nDims >= 2) row[nDims - 2] = phases[static_cast<size_t>(i)];
            }
        } else if (ok) {
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
                if (!phases.empty()) row[nDims - 2] = phases[static_cast<size_t>(i)];   // the phase column follows
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
        return neurosuite::core::replaceFile(from.toLocal8Bit().constData(),
                                             to.toLocal8Bit().constData());
    };
    bool promoted = true;
    if (!ownWaveforms && lead) {
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
        if (p.detached) log << p.method << ": " << nSpikes << " rows brought to the session's spike times (features kept).\n";
        else log << p.method << ": " << nSpikes << " feature rows re-projected through "
                 << QFileInfo(p.pcaPath).fileName() << ".\n";
    }
    if (!promoted) {
        // A rename failed part-way (a file-system error): no consistent set
        // remains, so the whole set goes back to its committed files.
        for (const QString& f : staged) QFile::remove(f);
        if (!ownWaveforms && lead) { QFile::remove(spkPending); syncedPending.remove(spkCommitted); }
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
        QString err;
        if (!reloadShownSpace(err))
            log << activeMethod << ": synced, but reloading it failed — " << err
                << "  Switch to another space and back.\n";
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
    movedInMethod = activeMethod;    // realign / nudge write the SHOWN space
}

void KlustersDoc::dropMovedSpikes()
{
    QMutexLocker lk(&movedRowsMutex);
    movedRows.clear();
    movedInMethod.clear();
}

bool KlustersDoc::followMovedSpikes(QString& logOut, const QString& writtenMethod)
{
    std::vector<int64_t> rows;
    QString movedIn;
    {
        QMutexLocker lk(&movedRowsMutex);
        rows.swap(movedRows);
        movedIn.swap(movedInMethod);
    }
    if (rows.empty() || !clusteringData) return true;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    const QString dir     = QFileInfo(docUrl).absolutePath();
    const std::string stem = (dir + QDir::separator() + baseName).toStdString();
    const int     group   = electrodeGroupID.toInt();
    const int     nChan   = clusteringData->nbOfChannels();
    const int     nSampBase = clusteringData->baseNbSamplesPerWaveform();
    const int     timeDim = clusteringData->timeDimension();
    const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    const quint64 now = spikeTimesGeneration.load();
    QTextStream log(&logOut);
    if (clusteringData->getCurrentChannels().size() < nChan || rows.back() >= nSpikes) {
        log << "follow: inconsistent group geometry; nothing followed.\n";
        return false;
    }
    // The space the realign / nudge ran in: its working copies already hold the
    // moved spikes (waveforms re-cut, features re-projected, its phase column
    // when super-sampled).  Every other space follows -- the .clu's own too,
    // when it is not the one written.
    const QString writtenIn = !writtenMethod.isEmpty() ? writtenMethod
                            : !movedIn.isEmpty()       ? movedIn : originMethod;
    const SpaceFiles written = workingSetOf(writtenIn, nullptr, /*seed*/ false);
    if (written.method.isEmpty()) {
        log << "follow: the space written (" << writtenMethod << ") is unknown; nothing followed.\n";
        return false;
    }
    // The loaded table's times are the session's (the written space is the one
    // shown, or the .clu's own with every other held).
    auto timeOf = [&](int64_t row) {
        return static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim));
    };
    std::vector<char> moved(static_cast<size_t>(nSpikes), 0);
    for (int64_t r : rows) moved[static_cast<size_t>(r)] = 1;

    // ── Every other usable space, grouped by the waveform file it reads ───
    struct Space {
        QString method, fetSource, fetCommitted, fetPending, pcaPath;
        neurosuite::core::PcaBasis pca;
        std::shared_ptr<const neurofileio::FetBinaryFile> table;
        bool fits = false, current = false, isOrigin = false;
        bool detached = false;                     // no basis: features stay, the time column follows
        std::vector<int64_t> newRows;              // rows.size() x nFeatures, once projected
    };
    struct Group {
        QString spkCommitted, spkPending;
        int     factor = 1;
        bool    isOrigin = false;                  // the .clu space's own .spk (its pending copy)
        std::vector<Space> members;
    };
    std::map<QString, Group> bySpk;                // committed .spk -> its spaces
    for (const FeatureSpace& s : availableFeatureSpaces()) {
        if (s.method == written.method || !s.usable) continue;     // realign / nudge wrote it
        Space sp;
        sp.method    = s.method;
        sp.isOrigin  = (s.method == originMethod);
        sp.fetSource = s.fetPath;                  // the held table: a working copy when there is one
        sp.fetCommitted = sp.isOrigin ? origFetPath
                        : QString::fromStdString(neurosuite::custody::methodPath(stem, "fet", s.method.toStdString(), group));
        sp.fetPending = sp.isOrigin ? pendingFetPath : sp.fetCommitted + QStringLiteral(".pending");
        sp.pcaPath = resolveFeature(dir + QDir::separator() + baseName, QStringLiteral("pca"), electrodeGroupID, s.method);
        const QString spkCommitted = sp.isOrigin ? origSpkPath
            : QString::fromStdString(neurosuite::custody::resolve(stem, "spk", group, s.method.toStdString()).path);
        Group& g = bySpk[spkCommitted];
        if (g.spkCommitted.isEmpty()) {
            g.spkCommitted = spkCommitted;
            g.isOrigin     = (spkCommitted == origSpkPath);
            g.spkPending   = g.isOrigin ? pendingSpkPath : spkCommitted + QStringLiteral(".pending");
            g.factor       = std::max(1, neurosuite::custody::supersampleOf(
                                             neurosuite::custody::methodOf(spkCommitted.toStdString())));
        }
        const int nSampG = nSampBase * g.factor;
        const bool spkStderiv = neurosuite::custody::isStderivMethod(
            neurosuite::custody::methodOf(spkCommitted.toStdString()));
        sp.table = featuresInMemory(sp.fetSource);
        sp.detached = !s.projection;
        sp.fits = sp.table && sp.table->nSpikes == nSpikes && sp.detached
            && (!sp.isOrigin || (QFileInfo::exists(pendingFetPath) && QFileInfo::exists(pendingSpkPath)));
        sp.fits = sp.fits || (sp.table && sp.table->nSpikes == nSpikes
            && neurosuite::core::loadPca(sp.pcaPath.toStdString(), sp.pca) && sp.pca.valid()
            && sp.pca.nCh <= nChan && sp.pca.recShift >= 0
            && sp.pca.recShift + sp.pca.data2use <= nSampG
            && neurosuite::core::hasTemporalDiff(sp.pca.method) == spkStderiv
            && sp.table->nFeatures - 1 >= sp.pca.nCh * sp.pca.nComp
            // the .clu space's working copies must exist to be written in place
            && (!sp.isOrigin || (QFileInfo::exists(pendingFetPath) && QFileInfo::exists(pendingSpkPath))));
        // Current before these moves: its features were computed at the
        // session's times for every spike that did not move.
        if (sp.fits) {
            const size_t nDims = static_cast<size_t>(sp.table->nFeatures);
            sp.current = true;
            // A super-sampled space of the written space's factor must also
            // carry its phases, not only its times: a sub-sample realign can
            // change a phase and no whole sample (the loaded table is the
            // written space's; a base-rate move changes no phase).
            const int phaseColW = (written.factor > 1 && written.factor == g.factor) ? supersamplePhaseColumn() : -1;
            for (int64_t r = 0; r < nSpikes && sp.current; ++r) {
                if (moved[static_cast<size_t>(r)]) continue;
                const int64_t* row = sp.table->values.data() + static_cast<size_t>(r) * nDims;
                if (row[nDims - 1] != timeOf(r)) sp.current = false;
                else if (phaseColW >= 0 && row[nDims - 2]
                         != static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(r + 1), phaseColW + 1)))
                    sp.current = false;
            }
        }
        // A projection member leads its group (its basis cuts the waveforms).
        if (!sp.detached) g.members.insert(g.members.begin(), std::move(sp));
        else              g.members.push_back(std::move(sp));
    }

    bool allOk = true;
    QStringList fullSync;
    for (auto& entry : bySpk) {
        Group&              g            = entry.second;
        const QString&      spkCommitted = g.spkCommitted;
        std::vector<Space>& members      = g.members;
        const QString       spkName      = QFileInfo(spkCommitted).fileName();
        const int     nSampG     = nSampBase * g.factor;
        const size_t  spikeElems = static_cast<size_t>(nChan) * static_cast<size_t>(nSampG);
        const qint64  spikeBytes = static_cast<qint64>(spikeElems * sizeof(int16_t));
        std::vector<int16_t> wav(rows.size() * spikeElems);

        // ── The group moves together, or not at all ──────────────────────
        bool fits = true, current = true;
        for (const Space& sp : members) { fits = fits && sp.fits; current = current && sp.current; }
        const QString spkMethod = featureMethod(spkCommitted);
        // The written space's waveform file: realign / nudge already re-cut
        // these rows into it (a _D lag space over the same waveforms, say).
        const bool writtenWaveforms = (spkCommitted == written.spkCommitted);
        // Detached spaces alone over this file: no waveform to re-cut, only
        // their time columns follow.
        const bool noWaveforms = members.front().detached;
        // A storage we can re-cut: raw or stderiv waveforms, at the base rate or
        // super-sampled (the _U<factor> is geometry, not a storage).
        const QString storage = QString::fromStdString(neurosuite::custody::baseRateToken(spkMethod.toStdString()));
        if (!noWaveforms && !writtenWaveforms && !methodIsStderiv(storage) && storage != QLatin1String("standard"))
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

        // The phase of a moved spike for this file: the written space's (scaled)
        // when that space is super-sampled, else the member's own column.
        const int writtenPhaseCol = written.factor > 1 ? supersamplePhaseColumn() : -1;
        auto phaseOf = [&](int64_t row, const std::shared_ptr<const neurofileio::FetBinaryFile>& table,
                           size_t nd, int factor) -> int64_t {
            if (writtenPhaseCol >= 0) {
                const int64_t pw = static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(row + 1), writtenPhaseCol + 1));
                return (pw * factor + written.factor / 2) / written.factor;
            }
            return table->values[static_cast<size_t>(row) * nd + nd - 2];
        };
        std::vector<int8_t> phases;

        // ── Moved waveforms, in this file's domain and geometry ──────────
        bool ok = true;
        if (noWaveforms) {
            // nothing to cut
        } else if (writtenWaveforms) {
            QFile f(written.spkWork);
            ok = f.open(QIODevice::ReadOnly);
            for (size_t k = 0; ok && k < rows.size(); ++k)
                // reinterpret_cast: raw int16 records read straight into the buffer.
                ok = f.seek(rows[k] * spikeBytes)
                  && f.read(reinterpret_cast<char*>(wav.data() + k * spikeElems), spikeBytes) == spikeBytes;
        } else {
            SpikeCutSpec cut;
            spikeCutSpecFor(spkMethod, &members.front().pca, cut, log);
            if (g.factor > 1)                                 // the file's own interpolator
                cut.window.interp = supersampleInterpFor(QFileInfo::exists(g.spkPending) ? g.spkPending : spkCommitted,
                                                         spkMethod, g.factor, &members.front().pca);
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
                // A super-sampled file is re-cut at each spike's phase.  Spike
                // times are one thing for every space, so a move made in a
                // super-sampled space hands its phases on (scaled to this file's
                // factor); a move made at the base rate is a whole-sample move
                // and this file keeps its own phases (its last feature column).
                if (g.factor > 1) {
                    const Space& p0 = members.front();
                    const size_t nd = static_cast<size_t>(p0.table->nFeatures);
                    phases.resize(rows.size());
                    for (size_t i = 0; i < rows.size(); ++i)
                        phases[i] = static_cast<int8_t>(phaseOf(rows[byTime[i]], p0.table, nd, g.factor));
                    cut.window.phaseTicks = phases.data();
                }
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
                if (g.factor > 1) row[nDims - 2] = phaseOf(rows[k], sp.table, nDims, g.factor);   // the phase column follows
                if (!sp.detached) {                          // a detached space keeps its features
                    const int16_t* w = wav.data() + k * spikeElems;
                    klusters::projectSpikeOntoPca(
                        sp.pca,
                        [&](int ch, int j) {
                            return static_cast<double>(
                                w[static_cast<size_t>((sp.pca.recShift + j) * nChan + ch)]);
                        },
                        row);
                }
                row[nDims - 1] = timeOf(rows[k]);
            }
        }

        // ── Write the group: .spk rows, then each .fet ───────────────────
        const QString& spkPending = g.spkPending;
        auto rollBackGroup = [&]() {
            // A partial group would pair waveforms and features from different
            // times: the whole group goes back to its saved files.  The .clu
            // space's own working copies hold its unsaved realigns and are never
            // dropped here: it is left behind, marked for a full sync.
            if (g.isOrigin) {
                log << spkName << ": writing the followed spikes into the .clu space's working "
                       "copies failed; it is behind the session's spike times (Sync Feature "
                       "Spaces retries).\n";
                allOk = false;
                return;
            }
            if (!writtenWaveforms && !noWaveforms) {
                QFile::remove(spkPending);
                syncedPending.remove(spkCommitted);
                SpkMemoryCache::instance().markWritten(spkPending);
            }
            for (const Space& sp : members) {
                QFile::remove(sp.fetPending);
                fetMemory.remove(sp.fetPending);
                syncedPending.remove(sp.fetCommitted);
                syncedGeneration.remove(sp.fetCommitted);
            }
            log << spkName << ": writing the followed spikes failed; its spaces are back on "
                << "their saved files (Sync Feature Spaces retries).\n";
            allOk = false;
        };
        if (!writtenWaveforms && !noWaveforms) {
            if (!g.isOrigin && (!syncedPending.contains(spkCommitted) || !QFileInfo::exists(spkPending))) {
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
            if (!g.isOrigin) syncedPending.insert(spkCommitted, spkPending);
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
            const QString& fetPending = sp.fetPending;
            const size_t   nDims      = static_cast<size_t>(sp.table->nFeatures);
            const bool     hadCopy    = sp.isOrigin
                                      ? QFileInfo::exists(fetPending)
                                      : (syncedPending.contains(sp.fetCommitted) && QFileInfo::exists(fetPending));
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
                    fetOk = neurosuite::core::replaceFile(tmp.toLocal8Bit().constData(),
                                                          fetPending.toLocal8Bit().constData());
                }
                if (!fetOk) QFile::remove(tmp);
            }
            if (!fetOk) break;
            if (!sp.isOrigin) syncedPending.insert(sp.fetCommitted, fetPending);
        }
        if (!fetOk) { rollBackGroup(); continue; }

        // ── Written: bring the held tables along ─────────────────────────
        for (Space& sp : members) {
            const QString& fetPending = sp.fetPending;
            const size_t   nDims      = static_cast<size_t>(sp.table->nFeatures);
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
            if (!sp.isOrigin) syncedGeneration.insert(sp.fetCommitted, now);
            log << sp.method << ": " << rows.size() << " moved spike(s) followed"
                << (sp.isOrigin ? " (the .clu's own space)" : sp.detached ? " (time column; a detached space keeps its features)" : "") << ".\n";
        }
        setModified(true);
    }
    log.flush();

    // ── Spaces that were already behind: full sync (each group once) ──────
    QSet<QString> done;
    for (const QString& m : fullSync) {
        if (m == originMethod) {
            // syncOneSpace keeps the .clu's own space current by definition: a
            // realign in another space leaves it behind only when its follow
            // failed earlier.  Reported, not silently skipped.
            log << originMethod << ": the .clu's own space is behind the session's spike times "
                   "and was not followed (Sync Feature Spaces does not rebuild it); save and "
                   "reopen, or undo the realign.\n";
            allOk = false;
            continue;
        }
        allOk = syncOneSpace(m, done, logOut) && allOk;
    }
    return allOk;
}

// ─────────────────────────────────────────────────────────────────────────────
// Super-sampled spaces (custody token _U<factor>; supersample.hpp)
// ─────────────────────────────────────────────────────────────────────────────

int KlustersDoc::supersamplePhaseColumn() const
{
    if (!clusteringData || clusteringData->supersampleFactor() <= 1) return -1;
    const int nFeat = clusteringData->timeDimension() - 1;      // feature columns before time
    return nFeat >= 1 ? nFeat - 1 : -1;
}

std::vector<int8_t> KlustersDoc::supersamplePhases() const
{
    std::vector<int8_t> ph;
    const int col = supersamplePhaseColumn();
    if (col < 0) return ph;
    const int64_t n = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    ph.resize(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i)
        ph[static_cast<size_t>(i)] = static_cast<int8_t>(
            clusteringData->featureValue(static_cast<dataType>(i + 1), col + 1));
    return ph;
}

bool KlustersDoc::buildSupersampledSpace(int factor, QString* token, QString& logOut, QString* error)
{
    QTextStream log(&logOut);
    auto fail = [&](const QString& why) { if (error) *error = why; return false; };
    if (!clusteringData) return fail(tr("No document is open."));
    if (factor < 2 || factor > 9) return fail(tr("The super-sampling factor must be 2 to 9."));
    const QString dir   = QFileInfo(docUrl).absolutePath();
    const std::string stem = (dir + QDir::separator() + baseName).toStdString();
    const int     group = electrodeGroupID.toInt();
    const int     nChan = clusteringData->nbOfChannels();
    const int     nSampBase = clusteringData->baseNbSamplesPerWaveform();
    const int64_t nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());

    // The shown space's waveforms are the source; the new tokens carry _U<factor>.
    // The shown .spk is a .pending working copy (or a sync's): its token is the
    // file's it stands for -- featureMethod strips the suffix; an untagged legacy
    // .spk is the standard space.  methodOf() on the copy gave "" and a bare _U4.
    const std::string shownSpk = featureMethod(activeSpkPath).toStdString();
    if (neurosuite::custody::supersampleOf(shownSpk) > 1)
        return fail(tr("The shown space (%1) is already super-sampled.").arg(QString::fromStdString(shownSpk)));
    const std::string newTok = neurosuite::custody::supersampledToken(shownSpk, factor);
    if (newTok == shownSpk)
        return fail(tr("Cannot derive a super-sampled token from %1.").arg(QString::fromStdString(shownSpk)));
    const QString spkOut = QString::fromStdString(neurosuite::custody::methodPath(stem, "spk", newTok, group));
    const QString pcaOut = QString::fromStdString(neurosuite::custody::methodPath(stem, "pca", newTok, group));
    const QString fetOut = QString::fromStdString(neurosuite::custody::methodPath(stem, "fet", newTok, group));
    if (QFileInfo::exists(fetOut))
        return fail(tr("The space %1 already exists (%2).  Remove its files to rebuild it.")
                        .arg(QString::fromStdString(newTok), QFileInfo(fetOut).fileName()));
    // A .spk without a .fet was built elsewhere (ndm_extractspikes --supersample):
    // the waveforms are taken as they are, the basis and the .fet fitted on them.
    const bool reuseSpk = QFileInfo::exists(spkOut);
    const int nSampUp = nSampBase * factor;
    const size_t rowElems = static_cast<size_t>(nChan) * static_cast<size_t>(nSampUp);

    // The shown space's basis: its window and component count scale by the factor.
    neurosuite::core::PcaBasis shown;
    {
        const QString shownPca = QString::fromStdString(neurosuite::custody::methodPath(
            stem, "pca", neurosuite::custody::waveformToken(activeMethod.toStdString()), group));
        const QString shownPca2 = QString::fromStdString(neurosuite::custody::methodPath(
            stem, "pca", activeMethod.toStdString(), group));
        // An untagged legacy session (<base>.pca.<group>): its basis is the last resort.
        const QString shownPca3 = QString::fromStdString(neurosuite::custody::untaggedPath(stem, "pca", group));
        if (!neurosuite::core::loadPca(shownPca.toStdString(), shown) || !shown.valid())
            if (!neurosuite::core::loadPca(shownPca2.toStdString(), shown) || !shown.valid())
                if (!neurosuite::core::loadPca(shownPca3.toStdString(), shown) || !shown.valid())
                    shown = neurosuite::core::PcaBasis{};
    }

    // ── 0. the shared tick .res of this factor ────────────────────────────
    // ONE <base>.res.<resToken>_U<f>.N for every _U<f> space (spike times are one
    // thing for all of them).  When another _U<f> space built it already -- and
    // it agrees with the shared .res -- its phases are this space's too: the
    // waveforms are cut at them and the phase column carries them.  Otherwise
    // it is written afresh (phase 0: cut at the .res).
    const int timeDim = clusteringData->timeDimension();
    const QString resOut = supersampledResPath(factor);
    std::vector<int8_t> phases;
    {
        const std::vector<int64_t> have = QFileInfo::exists(resOut)
            ? neurofileio::readResBinary(resOut.toStdString()) : std::vector<int64_t>{};
        if (static_cast<int64_t>(have.size()) == nSpikes) {
            phases.assign(static_cast<size_t>(nSpikes), 0);
            bool agrees = true;
            for (int64_t i = 0; i < nSpikes && agrees; ++i) {
                const int64_t t = static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim));
                const int64_t k = have[static_cast<size_t>(i)];
                if (k / factor != t || k - (k / factor) * factor < 0) agrees = false;
                else phases[static_cast<size_t>(i)] = static_cast<int8_t>(k - t * factor);
            }
            if (!agrees) phases.clear();
            else log << "phases from " << QFileInfo(resOut).fileName() << " (shared with the other x" << factor << " space)\n";
        }
    }

    // ── 1. the waveforms ──────────────────────────────────────────────────
    const QString raw = rawRecordingPath();
    if (reuseSpk) {
        const qint64 want = static_cast<qint64>(nSpikes) * static_cast<qint64>(rowElems * sizeof(int16_t));
        if (QFileInfo(spkOut).size() != want)
            return fail(tr("%1 exists but holds %2 bytes, not the %3 of %4 spikes × %5 channels × %6 ticks.")
                            .arg(QFileInfo(spkOut).fileName()).arg(QFileInfo(spkOut).size()).arg(want)
                            .arg(nSpikes).arg(nChan).arg(nSampUp));
        log << "waveforms: " << QFileInfo(spkOut).fileName() << " exists (built elsewhere); the basis and the .fet are fitted on it\n";
    } else if (QFileInfo::exists(raw)) {
        QString reLog;
        if (!reextractAllSpikesFromFil(spkOut, reLog, shown.valid() ? &shown : nullptr, phases.empty() ? nullptr : &phases)) {
            QFile::remove(spkOut);
            return fail(tr("Re-extracting the waveforms failed: %1").arg(reLog));
        }
        log << reLog << "\n";
    } else {
        // No recording: interpolate the stored windows (the last factor-1 ticks of
        // each window clamp to its final sample).
        const std::shared_ptr<const QByteArray> src = SpkMemoryCache::instance().get(activeSpkPath);
        const size_t srcElems = static_cast<size_t>(nChan) * static_cast<size_t>(nSampBase);
        if (!src || static_cast<qint64>(src->size()) != static_cast<qint64>(nSpikes) * static_cast<qint64>(srcElems * sizeof(int16_t)))
            return fail(tr("No recording (.fil/.dat) and the shown .spk cannot be held in memory."));
        QFile out(spkOut);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(tr("Cannot write %1").arg(spkOut));
        const int16_t* w = reinterpret_cast<const int16_t*>(src->constData());
        std::vector<int16_t> cm(srcElems), up(rowElems), row(rowElems);
        neurosuite::core::SupersampleScratch scratch;
        for (int64_t i = 0; i < nSpikes; ++i) {
            const int16_t* r = w + static_cast<size_t>(i) * srcElems;
            for (int s = 0; s < nSampBase; ++s)
                for (int ch = 0; ch < nChan; ++ch)
                    cm[static_cast<size_t>(ch) * nSampBase + s] = r[static_cast<size_t>(s) * nChan + ch];
            neurosuite::core::supersampleStoredWindow(cm.data(), nChan, nSampBase, factor, supersampleInterp, up.data(), scratch);
            for (int s = 0; s < nSampUp; ++s)
                for (int ch = 0; ch < nChan; ++ch)
                    row[static_cast<size_t>(s) * nChan + ch] = up[static_cast<size_t>(ch) * nSampUp + s];
            if (out.write(reinterpret_cast<const char*>(row.data()), static_cast<qint64>(rowElems * sizeof(int16_t)))
                != static_cast<qint64>(rowElems * sizeof(int16_t))) {
                out.close(); QFile::remove(spkOut);
                return fail(tr("Writing %1 failed.").arg(spkOut));
            }
        }
        out.close();
        log << "no recording: " << nSpikes << " stored windows interpolated x" << factor << " ("
            << QLatin1String(neurosuite::core::interpName(supersampleInterp)) << ")\n";
    }

    // ── 2. the basis, fitted on the new waveforms ─────────────────────────
    const std::shared_ptr<const QByteArray> spk = SpkMemoryCache::instance().get(spkOut);
    if (!spk || static_cast<qint64>(spk->size()) != static_cast<qint64>(nSpikes) * static_cast<qint64>(rowElems * sizeof(int16_t))) {
        if (!reuseSpk) QFile::remove(spkOut);
        return fail(tr("Cannot hold %1 in memory.").arg(QFileInfo(spkOut).fileName()));
    }
    const int16_t* waveforms = reinterpret_cast<const int16_t*>(spk->constData());
    const int nCh      = shown.valid() ? std::min(shown.nCh, nChan) : nChan;
    const int recShift = shown.valid() ? shown.recShift * factor : 0;
    const int data2use = shown.valid() ? std::min(shown.data2use * factor, nSampUp - recShift) : nSampUp;
    const int nComp    = shown.valid() ? shown.nComp : std::max(1, clusteringData->nbOfFeaturesByChannel());
    const int64_t fitN = std::min<int64_t>(nSpikes, 20000);
    const int64_t step = std::max<int64_t>(1, nSpikes / fitN);
    neurosuite::core::PcaBasis pca;
    const bool fitted = neurosuite::core::fitPca(
        static_cast<int>(nSpikes / step), nCh, data2use, recShift, nComp,
        [&](int i, int ch, int s) {
            const int16_t* r = waveforms + static_cast<size_t>(static_cast<int64_t>(i) * step) * rowElems;
            return static_cast<double>(r[static_cast<size_t>(recShift + s) * nChan + ch]);
        },
        pca, shown.valid() ? shown.method : neurosuite::core::Method::Standard,
        shown.valid() ? shown.nInputChannels : 0);
    if (!fitted || !neurosuite::core::writePca(pcaOut.toStdString(), pca)) {
        if (!reuseSpk) QFile::remove(spkOut); QFile::remove(pcaOut);
        return fail(tr("Fitting or writing the basis %1 failed.").arg(QFileInfo(pcaOut).fileName()));
    }
    log << "basis " << QFileInfo(pcaOut).fileName() << ": " << nCh << " ch x " << nComp
        << " components over ticks " << recShift << "+" << data2use << " of " << nSampUp
        << ", fitted on " << (nSpikes / step) << " spikes\n";

    // ── 3. the features: projections, the phase column, time ─────────────
    const int nPca  = nCh * nComp;
    const int nDims = nPca + 2;
    std::vector<int64_t> values(static_cast<size_t>(nSpikes) * static_cast<size_t>(nDims), 0);
    for (int64_t i = 0; i < nSpikes; ++i) {
        const int16_t* r = waveforms + static_cast<size_t>(i) * rowElems;
        int64_t* row = values.data() + static_cast<size_t>(i) * static_cast<size_t>(nDims);
        klusters::projectSpikeOntoPca(
            pca, [&](int ch, int j) { return static_cast<double>(r[static_cast<size_t>(recShift + j) * nChan + ch]); }, row);
        row[static_cast<size_t>(nPca)]     = phases.empty() ? 0 : phases[static_cast<size_t>(i)];   // phase: the shared tick .res's, else cut at the .res
        row[static_cast<size_t>(nPca + 1)] = static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim));
    }
    if (!neurofileio::writeFetBinary(fetOut.toStdString(), nDims, values)) {
        if (!reuseSpk) QFile::remove(spkOut); QFile::remove(pcaOut); QFile::remove(fetOut);
        return fail(tr("Writing %1 failed.").arg(QFileInfo(fetOut).fileName()));
    }
    log << "features " << QFileInfo(fetOut).fileName() << ": " << nSpikes << " rows x " << nDims
        << " (" << nPca << " PCA + phase + time)\n";

    // ── 4. the shared tick .res, when this space is the first at its factor ──
    // (sample x factor + phase 0; the shared base-rate .res is untouched and
    // custody's res resolution never takes a _U res for it).  Every save
    // rewrites it from the times and phases of a _U<f> space.
    if (phases.empty()) {
        std::vector<int64_t> ticks(static_cast<size_t>(nSpikes));
        for (int64_t i = 0; i < nSpikes; ++i)
            ticks[static_cast<size_t>(i)] = static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim))
                                            * static_cast<int64_t>(factor);
        if (!neurofileio::writeResBinary(resOut.toStdString(), ticks)) {
            if (!reuseSpk) QFile::remove(spkOut); QFile::remove(pcaOut); QFile::remove(fetOut); QFile::remove(resOut);
            return fail(tr("Writing %1 failed.").arg(QFileInfo(resOut).fileName()));
        }
        log << "times " << QFileInfo(resOut).fileName() << ": " << nSpikes << " spikes in ticks ("
            << factor << " per sample)\n";
    }
    if (token) *token = QString::fromStdString(newTok);
    return true;
}

// The shared tick .res of @p factor as it is on disk (empty when there is none).
std::vector<int64_t> KlustersDoc::sharedTicksOnDisk(int factor) const
{
    const QString path = supersampledResPath(factor);
    if (path.isEmpty() || !QFileInfo::exists(path)) return {};
    return neurofileio::readResBinary(path.toStdString());
}

// The shared tick .res of this group at @p factor: <base>.res.<resToken>_U<f>.N,
// one for every _U<f> space, named after the shared base-rate .res
// (custody::supersampledRes); empty for factor 1.
QString KlustersDoc::supersampledResPath(int factor) const
{
    if (!clusteringData || factor <= 1) return QString();
    const QString dir = QFileInfo(docUrl).absolutePath();
    const std::string stem = (dir + QDir::separator() + baseName).toStdString();
    return QString::fromStdString(neurosuite::custody::supersampledRes(
        stem, electrodeGroupID.toInt(), factor, originMethod.toStdString()).path);
}

// The spike times at @p factor in ticks: a _U<f> space's time column x factor +
// its phase column -- the shown space's when it is one, else the .clu's own
// (held) table's, else the first usable _U<f> space's (they agree: the follow
// carries one set of phases).  Empty when no _U<f> space's table is at hand.
std::vector<int64_t> KlustersDoc::supersampledTicks(int factor)
{
    std::vector<int64_t> ticks;
    if (!clusteringData || factor <= 1) return ticks;
    const int64_t n = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    auto factorOf = [](const QString& m) { return neurosuite::custody::supersampleOf(m.toStdString()); };
    if (factorOf(activeMethod) == factor) {
        const int phaseCol = supersamplePhaseColumn();
        if (phaseCol < 0) return ticks;
        const int timeDim = clusteringData->timeDimension();
        ticks.resize(static_cast<size_t>(n));
        for (int64_t i = 0; i < n; ++i) {
            const dataType row = static_cast<dataType>(i + 1);
            ticks[static_cast<size_t>(i)] =
                static_cast<int64_t>(clusteringData->featureValue(row, timeDim)) * factor
                + static_cast<int64_t>(clusteringData->featureValue(row, phaseCol + 1));
        }
        return ticks;
    }
    QString fetPath;
    for (const FeatureSpace& s : availableFeatureSpaces()) {      // the .clu's own space is listed first
        if (!s.usable || factorOf(s.method) != factor) continue;
        fetPath = s.fetPath;                                      // the working copy when there is one
        break;
    }
    const std::shared_ptr<const neurofileio::FetBinaryFile> t = fetPath.isEmpty() ? nullptr : featuresInMemory(fetPath);
    if (!t || t->nSpikes != n || t->nFeatures < 3) return ticks;
    const size_t nd = static_cast<size_t>(t->nFeatures);
    ticks.resize(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
        const int64_t* row = t->values.data() + static_cast<size_t>(i) * nd;
        ticks[static_cast<size_t>(i)] = row[nd - 1] * factor + row[nd - 2];
    }
    return ticks;
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
