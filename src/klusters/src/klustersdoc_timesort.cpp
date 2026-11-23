/***************************************************************************
    klustersdoc_timesort.cpp
    ------------------------
    KlustersDoc full time sort: put a group's spike rows back in time order.

    Realign and nudge rewrite each spike in its own row, so a spike's time may
    pass a neighbour's and the .res drifts out of time order.  This sorts the
    .res by time (stable) and applies the same row permutation to every other
    file indexed by spike row -- each method's .spk and .fet, the .clu and .clc,
    stage-tagged copies included -- so row i is still the same spike in all of
    them.  It works on the saved files: the caller saves first and reopens the
    document afterwards (undo history and other row-indexed state in memory
    would not survive a permutation).

    Copyright (C) 2026  neurosuite-3 contributors
    SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klustersdoc.h"
#include <neurosuite/core/fileops.hpp>
#include <neurosuite/core/neurofileio.h>

#include <QDir>
#include <neurosuite/core/fileio64.hpp>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <stdexcept>
#include <sys/types.h>
#include <vector>

namespace {

/// Working copies and backups are not part of the session's files.
bool isWorkingCopySuffix(const QString& suffix)
{
    return suffix.contains(QLatin1String("pending")) || suffix.contains(QLatin1String("tmp"))
        || suffix.contains(QLatin1String("bak"));
}

/// Types with one entry per spike, each permuted here.
bool isPerSpikeType(const std::string& t)
{
    return t == "res" || t == "spk" || t == "fet" || t == "clu" || t == "clc";
}

/// Per-group types that are not per spike: a basis, a model, a log, noise statistics,
/// child-cluster properties, and the template library's per-template files.
bool isPerClusterType(const QString& t)
{
    static const QStringList types{ "pca", "model", "klg", "nst",
                                    "clp", "tcl", "wti", "wtf", "wtl" };
    return types.contains(t);
}

/// True when @p name is a file of session @p base that carries @p group as a
/// dot-separated token (<base>.<...>.<group>[.<...>]).
bool namesGroup(const QString& name, const QString& base, const QString& group)
{
    if (!name.startsWith(base + QLatin1Char('.'))) return false;
    return name.mid(base.size() + 1).split(QLatin1Char('.')).contains(group);
}

/// Write @p src with its rows in @p order to @p dst (header copied first).
bool permuteRows(const KlustersDoc::RowFile& f, const std::vector<int64_t>& order,
                 const QString& dst)
{
    std::FILE* in  = std::fopen(f.path.toLocal8Bit().constData(), "rb");
    std::FILE* out = std::fopen(dst.toLocal8Bit().constData(), "wb");
    bool ok = in && out;
    std::vector<char> buf(static_cast<size_t>(std::max(f.header, f.record)));
    if (ok && f.header > 0) {
        const size_t h = static_cast<size_t>(f.header);
        ok = std::fread(buf.data(), 1, h, in) == h && std::fwrite(buf.data(), 1, h, out) == h;
    }
    const size_t rec = static_cast<size_t>(f.record);
    int64_t at = 0;                                   // row the read position is on
    for (size_t k = 0; ok && k < order.size(); ++k) {
        const int64_t r = order[k];
        if (r != at)                                  // mostly sorted: seek only on a jump
            ok = neurosuite::core::fseek64(in, static_cast<std::int64_t>(f.header)
                            + static_cast<std::int64_t>(r) * static_cast<std::int64_t>(f.record),
                        SEEK_SET) == 0;
        ok = ok && std::fread(buf.data(), 1, rec, in) == rec
                && std::fwrite(buf.data(), 1, rec, out) == rec;
        at = r + 1;
    }
    if (in) std::fclose(in);
    if (out && std::fclose(out) != 0) ok = false;
    if (!ok) QFile::remove(dst);
    return ok;
}

/// Write the sparse .spkc @p src to @p dst with every row renamed to where
/// @p order puts it (order[k] = the row that moves to row k) and the records in
/// the new ascending order.
bool permuteSparse(const QString& src, const std::vector<int64_t>& order, const QString& dst)
{
    const neurofileio::SpkcFile in = neurofileio::readSpkc(src.toLocal8Bit().constData());
    if (!in.ok || in.nSpikes != static_cast<int64_t>(order.size())) return false;
    std::vector<int64_t> movedTo(order.size());
    for (size_t k = 0; k < order.size(); ++k) movedTo[static_cast<size_t>(order[k])] = static_cast<int64_t>(k);
    std::vector<size_t> by(in.rows.size());
    std::iota(by.begin(), by.end(), size_t{0});
    std::sort(by.begin(), by.end(), [&](size_t a, size_t b) {
        return movedTo[static_cast<size_t>(in.rows[a])] < movedTo[static_cast<size_t>(in.rows[b])];
    });
    neurofileio::SpkcFile out = in;
    const size_t rec = static_cast<size_t>(in.nSamples) * static_cast<size_t>(in.nChannels);
    for (size_t k = 0; k < by.size(); ++k) {
        out.rows[k] = movedTo[static_cast<size_t>(in.rows[by[k]])];
        std::copy_n(in.waveforms.begin() + static_cast<std::ptrdiff_t>(by[k] * rec), rec,
                    out.waveforms.begin() + static_cast<std::ptrdiff_t>(k * rec));
    }
    if (!neurofileio::writeSpkc(dst.toLocal8Bit().constData(), out)) {
        QFile::remove(dst);
        return false;
    }
    return true;
}

} // namespace

bool KlustersDoc::planTimeSort(TimeSortPlan& plan, QString& errorInformation) const
{
    plan = TimeSortPlan{};
    if (!clusteringData || docUrl.isEmpty()) {
        errorInformation = tr("No document is open.");
        return false;
    }
    const int64_t n = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    if (n < 1 || origResPath.isEmpty() || QFileInfo(origResPath).size() != n * 8) {
        errorInformation = tr("The .res file %1 does not hold this group's %2 spikes.")
                               .arg(QFileInfo(origResPath).fileName()).arg(n);
        return false;
    }

    // ── The permutation: a stable sort of the saved .res by time ──────────
    std::vector<int64_t> ts(static_cast<size_t>(n));
    {
        std::FILE* f = std::fopen(origResPath.toLocal8Bit().constData(), "rb");
        const bool ok = f && std::fread(ts.data(), sizeof(int64_t), ts.size(), f) == ts.size();
        if (f) std::fclose(f);
        if (!ok) {
            errorInformation = tr("Cannot read %1.").arg(QFileInfo(origResPath).fileName());
            return false;
        }
    }
    for (size_t i = 1; i < ts.size(); ++i)
        if (ts[i] < ts[i - 1]) ++plan.nOutOfOrder;
    if (plan.nOutOfOrder == 0) return true;           // already in time order
    plan.order.resize(ts.size());
    std::iota(plan.order.begin(), plan.order.end(), int64_t{0});
    std::stable_sort(plan.order.begin(), plan.order.end(),
                     [&](int64_t a, int64_t b) {
                         return ts[static_cast<size_t>(a)] < ts[static_cast<size_t>(b)];
                     });

    // ── Every file of this group indexed by spike row ─────────────────────
    const QDir dir(QFileInfo(docUrl).absolutePath());
    const int group = electrodeGroupID.toInt();
    const QString eapPrefix = baseName + QStringLiteral(".eap.") + electrodeGroupID;
    const QString spkcPrefix = baseName + QStringLiteral(".spkc.");
    const QStringList names = dir.entryList(QStringList{ baseName + QStringLiteral(".*") },
                                            QDir::Files, QDir::Name);
    for (const QString& name : names) {
        // Template-class membership (.eap): one row of nClasses int8 cells per
        // spike after a 32-byte header.  A cell is an offset from the row's own
        // time, so the rows move as they are.
        if (name == eapPrefix || name.startsWith(eapPrefix + QLatin1Char('.'))) {
            if (isWorkingCopySuffix(name.mid(eapPrefix.size()))) continue;
            const QString path = dir.absoluteFilePath(name);
            QFile in(path);
            quint32 head[3] = { 0, 0, 0 };                // magic, nSpikes, nClasses
            RowFile f;
            f.path   = path;
            f.header = 32;
            // reinterpret_cast: QIODevice::read fills raw bytes (the header's first three words).
            const bool read = in.open(QIODevice::ReadOnly)
                && in.read(reinterpret_cast<char*>(head), sizeof(head)) == static_cast<qint64>(sizeof(head));
            f.record = read ? static_cast<qint64>(head[2]) : 0;
            if (read && f.record > 0 && static_cast<int64_t>(head[1]) == n
                && QFileInfo(path).size() == f.header + n * f.record)
                plan.files.push_back(f);
            else
                plan.skipped << name;
            continue;
        }
        // Sparse corrected waveforms (<base>.spkc.<method>.<group>[.<stage>]): the
        // index names rows.
        if (name.startsWith(spkcPrefix)) {
            const QStringList toks = name.mid(spkcPrefix.size()).split(QLatin1Char('.'));
            int gi = -1;
            for (int i = 0; i < toks.size() && gi < 0; ++i) {
                bool isNumber = false;
                toks[i].toInt(&isNumber);
                if (isNumber) gi = i;
            }
            if (gi < 0 || toks[gi] != electrodeGroupID) continue;          // another group's
            if (isWorkingCopySuffix(QStringList(toks.mid(gi + 1)).join(QLatin1Char('.')))) continue;
            const QString path = dir.absoluteFilePath(name);
            const neurofileio::SpkcFile c = neurofileio::readSpkc(path.toLocal8Bit().constData());
            if (c.ok && c.nSpikes == n) {
                RowFile f;
                f.path   = path;
                f.sparse = true;
                plan.files.push_back(f);
            } else {
                plan.skipped << name;
            }
            continue;
        }
        neurosuite::custody::Anchor a;
        try {
            a = neurosuite::custody::parseAnchor(name.toStdString());
        } catch (const std::exception&) {
            continue;                                 // a group-like token out of int range: not ours
        }
        // The decollide collision sidecar (.col) holds a spike index per record:
        // row-indexed, and owned by decollide: reported, not rewritten.
        if (a.ok && a.group == group && a.type == "col"
            && QString::fromStdString(a.base) == baseName
            && !isWorkingCopySuffix(QString::fromStdString(a.suffix))) {
            plan.notReordered << name;
            continue;
        }
        const bool ours = a.ok && a.group == group && QString::fromStdString(a.base) == baseName;
        if (ours && isWorkingCopySuffix(QString::fromStdString(a.suffix)))
            continue;
        if (!ours || !isPerSpikeType(a.type)) {
            // Anything else that names this group (a tool's sidecar, say) is
            // listed so the user can check none of it is indexed by spike.
            const QString typeToken = name.mid(baseName.size() + 1).section(QLatin1Char('.'), 0, 0);
            if (namesGroup(name, baseName, electrodeGroupID)
                && !isWorkingCopySuffix(name.mid(baseName.size()))
                && !isPerClusterType(typeToken))
                plan.otherGroupFiles << name;
            continue;
        }
        const QString path = dir.absoluteFilePath(name);
        const qint64  size = QFileInfo(path).size();
        RowFile f;
        f.path = path;
        bool fits = false;
        if (a.type == "res") {
            f.record = 8;
            fits = size == n * 8;
        } else if (a.type == "clu" || a.type == "clc") {
            f.header = 4;                             // int32 cluster count
            f.record = 4;
            fits = size == 4 + n * 4;
        } else if (a.type == "fet") {
            QFile in(path);
            qint32 nDims = 0;
            // reinterpret_cast: QIODevice::read fills raw bytes (native-endian int32 header).
            if (in.open(QIODevice::ReadOnly)
                && in.read(reinterpret_cast<char*>(&nDims), sizeof(nDims)) == sizeof(nDims)
                && nDims > 0) {
                f.header = 4;
                f.record = static_cast<qint64>(nDims) * 8;
                fits = size == 4 + n * f.record;
            }
        } else if (a.type == "spk") {
            // This group's record size: a stage file with extra rows must not
            // be split at a guessed width.
            f.record = static_cast<qint64>(clusteringData->nbOfChannels())
                     * clusteringData->nbSamplesPerWaveform() * 2;     // int16 samples
            fits = f.record > 0 && size == n * f.record;
        }
        if (fits) plan.files.push_back(f);
        else      plan.skipped << name;
    }
    return true;
}

bool KlustersDoc::applyTimeSort(const TimeSortPlan& plan, QString& logOut)
{
    QTextStream log(&logOut);
    if (plan.order.empty() || plan.files.empty()) {
        log << tr("Nothing to sort.") << "\n";
        return false;
    }

    // ── 1. Write every file in time order beside the original ─────────────
    std::vector<QString> tmps;
    auto dropTmps = [&]() { for (const QString& t : tmps) QFile::remove(t); };
    for (const RowFile& f : plan.files) {
        tmps.push_back(f.path + QStringLiteral(".timesort.tmp"));
        if (!(f.sparse ? permuteSparse(f.path, plan.order, tmps.back())
                       : permuteRows(f, plan.order, tmps.back()))) {
            log << tr("Writing %1 in time order failed; no file was changed.")
                       .arg(QFileInfo(f.path).fileName()) << "\n";
            dropTmps();
            return false;
        }
    }

    // ── 2. Swap them all in; any failure puts every original back ─────────
    auto local = [](const QString& p) { return p.toLocal8Bit(); };
    std::vector<size_t> swapped;
    QStringList stranded;                         // originals a failed rollback left as .bak
    bool ok = true;
    for (size_t i = 0; ok && i < plan.files.size(); ++i) {
        const QString& path = plan.files[i].path;
        const QString  bak  = path + QStringLiteral(".timesort.bak");
        QFile::remove(bak);
        if (!neurosuite::core::replaceFile(local(path).constData(), local(bak).constData())) { ok = false; break; }
        if (!neurosuite::core::replaceFile(local(tmps[i]).constData(), local(path).constData())) {
            if (!neurosuite::core::replaceFile(local(bak).constData(), local(path).constData()))
                stranded << QFileInfo(bak).fileName();
            ok = false;
            break;
        }
        swapped.push_back(i);
    }
    if (!ok) {
        for (auto it = swapped.rbegin(); it != swapped.rend(); ++it) {
            const QString& path = plan.files[*it].path;
            const QString  bak  = path + QStringLiteral(".timesort.bak");
            QFile::remove(path);
            if (!neurosuite::core::replaceFile(local(bak).constData(), local(path).constData()))
                stranded << QFileInfo(bak).fileName();
        }
        dropTmps();
        if (!stranded.isEmpty()) {
            log << tr("Replacing the files failed, and these originals could not be put back; "
                      "rename each to drop \".timesort.bak\" before opening the session:")
                << "\n  " << stranded.join(QStringLiteral("\n  ")) << "\n";
            return false;
        }
        log << tr("Replacing the files failed; every file was put back as it was.") << "\n";
        return false;
    }
    for (const RowFile& f : plan.files)
        QFile::remove(f.path + QStringLiteral(".timesort.bak"));

    log << tr("%1 spike(s) were out of time order; %2 file(s) rewritten in time order:")
               .arg(plan.nOutOfOrder).arg(plan.files.size()) << "\n";
    for (const RowFile& f : plan.files) log << "  " << QFileInfo(f.path).fileName() << "\n";
    return true;
}
