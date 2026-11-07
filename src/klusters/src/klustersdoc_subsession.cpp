// klustersdoc_subsession.cpp -- the recluster SUB-SESSION: the selected clusters' spikes written as
// a session of their own, so that a fiber-kit plan (or any engine that reads a neurosuite session)
// runs on them unmodified and Klusters reads the final .clu back by spike order.
//
// This is the Klusters side of the contract fiber-kit documents in docs/pipeline.md "Reclustering a
// selection"; fiber_subsession.export() there is the reference implementation of the same files:
//
//   <tmp>.res[.<v>].<g>        the selected spikes' times -- a ROW SUBSET of every <base>.res[.<v>].<g>
//                              (absolute times: the real .fil stays valid)
//   <tmp>.spk.<v>.<g>          row subsets of every waveform variant on disk
//   <tmp>.fet.<v>.<g>          row subsets of every feature variant on disk
//   <tmp>.fet.klusters.<g>     the open clustering's features (what the built-in recluster writes; its
//                              name is what the finish path turns into <tmp>.clu.klusters.<g>, the final
//                              .clu the engine is asked to deliver; `klusters` is no variant a stage reads)
//   <tmp>.clu.<v>.<g>[.<tag>]  the SEED: the selection's current ids from the ACTIVE layer
//   <tmp>.yaml / .yml / .xml   copies of the parameter files
//   <tmp>.fil .dat .nst.* .pca.* .eap.* .tcl.*   SYMLINKS to the session's (nothing large is copied)
//
// The rows are in time order (Data::createFeatureFileSorted fills the recluster table that way), the
// same in every file, so the sub-session's .res is sorted as the stages require and the engine's final
// .clu maps back to the selection by position through integrateReclusteredClusters, exactly as a
// KlustaKwik result does.  Everything the engine writes under <tmp>.* is removed afterwards
// (removeReclusterSubsession).  The file operations are subsessionfiles.{h,cpp} (QtCore only, tested).
#include "klustersdoc.h"
#include "data.h"
#include "subsessionfiles.h"

#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QMap>

#include <cstdint>

int KlustersDoc::createReclusterSubsession(QList<int>& clustersToRecluster, const QString& tmpBase,
                                           const QString& fetPath, const QString& variant, const QString& tag,
                                           QStringList* written, QString* error)
{
    using namespace subsession;
    QStringList sink;
    QStringList& w = written ? *written : sink;
    QString err;
    const QString dir = documentDirectory();
    const QString baseName = documentBaseName();
    const QString base = dir + QLatin1Char('/') + baseName;
    const QString group = currentElectrodeGroupID();
    const qint64 nTotal = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    auto fail = [&](const QString& why) { if (error) *error = why; return static_cast<int>(CREATION_ERROR); };

    // 1. the recluster table in time order + the open clustering's features (the built-in recluster's
    //    .fet, whose name the finish path turns into the final .clu's)
    reclusterTarget = &data();
    const QVector<dataType> rowsT = reclusterTarget->createFeatureFileSorted(clustersToRecluster, fetPath);
    if (rowsT.isEmpty())
        return fail(tr("no spikes in the selection (or a cluster-map desync)"));
    w << fetPath;
    reclusterSubsessionRows = rowsT;                 // for integrateReclusteredChildren
    QVector<int64_t> rows;
    rows.reserve(rowsT.size());
    for (dataType r : rowsT) rows.append(static_cast<int64_t>(r));

    // 2. row subsets: .res (int64, no header), .spk (int16 rows), .fet (int32 header + int64 rows)
    for (const QString& p : groupFiles(dir, baseName, QStringLiteral("res"), group)) {
        const QString q = withVariant(tmpBase, QStringLiteral("res"), variantOf(p, baseName, QStringLiteral("res"), group), group);
        if (!copyRecords(p, q, 0, 8, nTotal, rows, &err)) return fail(err);
        w << q;
    }
    for (const QString& p : groupFiles(dir, baseName, QStringLiteral("spk"), group)) {
        const qint64 size = QFileInfo(p).size();
        if (nTotal <= 0 || size % nTotal)
            return fail(tr("%1: %2 bytes is not %3 spikes of int16 rows").arg(QFileInfo(p).fileName()).arg(size).arg(nTotal));
        const QString q = withVariant(tmpBase, QStringLiteral("spk"), variantOf(p, baseName, QStringLiteral("spk"), group), group);
        if (!copyRecords(p, q, 0, size / nTotal, nTotal, rows, &err)) return fail(err);
        w << q;
    }
    for (const QString& p : groupFiles(dir, baseName, QStringLiteral("fet"), group)) {
        QFile f(p);
        int32_t nDim = 0;
        if (!f.open(QIODevice::ReadOnly) || f.read(reinterpret_cast<char*>(&nDim), 4) != 4 || nDim <= 0)
            return fail(tr("%1: no .fet header").arg(QFileInfo(p).fileName()));
        f.close();
        const QString q = withVariant(tmpBase, QStringLiteral("fet"), variantOf(p, baseName, QStringLiteral("fet"), group), group);
        if (!copyRecords(p, q, 4, 8LL * nDim, nTotal, rows, &err)) return fail(err);
        w << q;
    }

    // 3. the seed .clu: the selection's current ids on the ACTIVE layer
    {
        const QVector<dataType> labels = reclusterTarget->labelByFeatureRow();
        QVector<int32_t> ids;
        ids.reserve(rows.size());
        for (dataType r : rowsT)
            ids.append((r >= 1 && r < labels.size()) ? static_cast<int32_t>(labels[static_cast<int>(r)]) : 0);
        const QStringList seeds{ withVariant(tmpBase, QStringLiteral("clu"), variant, group),
                                 tag.isEmpty() ? QString() : withVariant(tmpBase, QStringLiteral("clu"), variant, group, tag) };
        for (const QString& q : seeds) {
            if (q.isEmpty()) continue;
            if (!writeClu(q, ids, &err)) return fail(err);
            w << q;
        }
    }

    // 4. parameter files: copies.  5. the recording and the per-group models: symlinks.
    for (const char* ext : {".yaml", ".yml", ".xml"}) {
        const QString src = base + QLatin1String(ext), dst = tmpBase + QLatin1String(ext);
        if (!QFile::exists(src)) continue;
        QFile::remove(dst);
        if (QFile::copy(src, dst)) w << dst;
    }
    QStringList linkSources{ base + QLatin1String(".fil"), base + QLatin1String(".dat") };
    for (const char* kind : {"nst", "pca", "eap", "tcl"})
        linkSources << groupFiles(dir, baseName, QLatin1String(kind), group);
    for (const QString& src : linkSources) {
        if (!QFileInfo::exists(src)) continue;
        const QString dst = tmpBase + QFileInfo(src).fileName().mid(baseName.size());
        QFile::remove(dst);
        if (QFile::link(QFileInfo(src).fileName(), dst)) w << dst;   // relative: within the directory
        else qWarning() << "[recluster] sub-session: could not link" << dst;
    }
    return OK;
}

int KlustersDoc::removeReclusterSubsession(const QString& tmpBase)
{
    return subsession::removeAll(tmpBase);
}

int KlustersDoc::integrateReclusteredChildren(const QString& clcPath, QString* error)
{
    if (!childData || reclusterTargetIsChild() || reclusterSubsessionRows.isEmpty()) return 0;
    if (!QFile::exists(clcPath)) return 0;
    QVector<int32_t> ids; QString err;
    if (!subsession::readClu(clcPath, &ids, &err)) { if (error) *error = err; return -1; }
    if (ids.size() != reclusterSubsessionRows.size()) {
        if (error) *error = tr("%1: %2 atoms for %3 spikes").arg(QFileInfo(clcPath).fileName()).arg(ids.size())
                                .arg(reclusterSubsessionRows.size());
        return -1;
    }
    // Renumber above the atom layer's highest id, compactly, in order of first appearance (the clc's
    // 0 and 1 are the bins' self children in the sub-session; here they are atoms like any other, as
    // the .clu path treats KlustaKwik's noise cluster).
    QVector<dataType> labels = childData->labelByFeatureRow();
    QMap<int32_t, dataType> remap;
    dataType next = childData->highestClusterId();
    for (int i = 0; i < ids.size(); ++i) {
        const dataType r = reclusterSubsessionRows[i];
        if (r < 1 || r >= labels.size()) continue;
        auto it = remap.find(ids[i]);
        if (it == remap.end()) it = remap.insert(ids[i], ++next);
        labels[static_cast<int>(r)] = it.value();
    }
    if (!childData->setClusterLabels(labels)) {
        // the map was desynced and has been repaired: redo against the repaired view
        labels = childData->labelByFeatureRow();
        for (int i = 0; i < ids.size(); ++i) {
            const dataType r = reclusterSubsessionRows[i];
            if (r >= 1 && r < labels.size()) labels[static_cast<int>(r)] = remap.value(ids[i]);
        }
        childData->setClusterLabels(labels);
    }
    reclusterSubsessionRows.clear();
    return remap.size();
}
