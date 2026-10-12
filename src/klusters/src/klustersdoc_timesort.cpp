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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <numeric>
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

/// Per-group types that are not per spike: a basis, a model, colours, a log,
/// child-cluster properties, and the template library's per-template files.
bool isPerClusterType(const QString& t)
{
    static const QStringList types{ "pca", "model", "col", "klg",
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
            ok = fseeko(in, static_cast<off_t>(f.header)
                            + static_cast<off_t>(r) * static_cast<off_t>(f.record),
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
    const QStringList names = dir.entryList(QStringList{ baseName + QStringLiteral(".*") },
                                            QDir::Files, QDir::Name);
    for (const QString& name : names) {
        // Template-class membership (.eap) is row-indexed too, but its layout
        // is owned by the template tools: report it rather than rewrite it.
        if (name == eapPrefix || name.startsWith(eapPrefix + QLatin1Char('.'))) {
            if (!isWorkingCopySuffix(name.mid(eapPrefix.size()))) plan.notReordered << name;
            continue;
        }
        const neurosuite::custody::Anchor a = neurosuite::custody::parseAnchor(name.toStdString());
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
            f.record = size / n;                      // nChan * nSamp * int16
            fits = size > 0 && size % n == 0;
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
        if (!permuteRows(f, plan.order, tmps.back())) {
            log << tr("Writing %1 in time order failed; no file was changed.")
                       .arg(QFileInfo(f.path).fileName()) << "\n";
            dropTmps();
            return false;
        }
    }

    // ── 2. Swap them all in; any failure puts every original back ─────────
    auto local = [](const QString& p) { return p.toLocal8Bit(); };
    std::vector<size_t> swapped;
    bool ok = true;
    for (size_t i = 0; ok && i < plan.files.size(); ++i) {
        const QString& path = plan.files[i].path;
        const QString  bak  = path + QStringLiteral(".timesort.bak");
        QFile::remove(bak);
        if (std::rename(local(path).constData(), local(bak).constData()) != 0) { ok = false; break; }
        if (std::rename(local(tmps[i]).constData(), local(path).constData()) != 0) {
            std::rename(local(bak).constData(), local(path).constData());
            ok = false;
            break;
        }
        swapped.push_back(i);
    }
    if (!ok) {
        for (auto it = swapped.rbegin(); it != swapped.rend(); ++it) {
            const QString& path = plan.files[*it].path;
            QFile::remove(path);
            std::rename(local(path + QStringLiteral(".timesort.bak")).constData(),
                        local(path).constData());
        }
        dropTmps();
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
