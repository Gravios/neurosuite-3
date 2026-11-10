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

#include <algorithm>
#include <cstdint>

int KlustersDoc::createReclusterSubsession(QList<int>& clustersToRecluster, const QString& tmpBase,
                                           const QString& fetPath, const QString& variant, const QString& tag,
                                           QStringList* written, QString* error, bool refine)
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
    if (refine && !childData) return fail(tr("a refine plugin needs a hierarchical session (.clc/.clp)"));
    reclusterTarget = refine ? childData : &data();     // refine: the atom layer, whatever the scope
    const QVector<dataType> rowsT = reclusterTarget->createFeatureFileSorted(clustersToRecluster, fetPath);
    if (rowsT.isEmpty())
        return fail(tr("no spikes in the selection (or a cluster-map desync)"));
    w << fetPath;
    reclusterSubsessionRows = rowsT;                 // for integrateReclusteredChildren / carryBackSubsessionFeatures
    reclusterSubsessionFets.clear();
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
        const QString fv = variantOf(p, baseName, QStringLiteral("fet"), group);
        const QString q = withVariant(tmpBase, QStringLiteral("fet"), fv, group);
        if (!copyRecords(p, q, 4, 8LL * nDim, nTotal, rows, &err)) return fail(err);
        w << q;
        reclusterSubsessionFets << fv;
    }

    // 3. the seed .clu: the selection's current ids on the ACTIVE layer (refine: the atoms' PARENTS, so
    //    the engine sees each unit whole; the atoms go to a seed .clc/.clp beside it)
    {
        const QVector<dataType> labels = (refine ? clusteringData : reclusterTarget)->labelByFeatureRow();
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
        if (refine) {
            const QVector<dataType> atoms = childData->labelByFeatureRow();
            QVector<int32_t> aids;
            aids.reserve(rows.size());
            int32_t maxAtom = 0;
            for (dataType r : rowsT) {
                const int32_t a = (r >= 1 && r < atoms.size()) ? static_cast<int32_t>(atoms[static_cast<int>(r)]) : 0;
                aids.append(a);
                maxAtom = std::max(maxAtom, a);
            }
            // the .clp: entry i = atom i+1 -> parent, every atom id up to the highest in the selection
            // (an atom outside the selection keeps its real parent; the engine only reads the map)
            QVector<int32_t> clp(maxAtom, 0);
            for (int32_t a = 1; a <= maxAtom; ++a) clp[a - 1] = static_cast<int32_t>(std::max(0, parentOfChild(a)));
            for (const QString& q : seeds) {
                if (q.isEmpty()) continue;
                QString c = q, pth = q;
                c.replace(QLatin1String(".clu."), QLatin1String(".clc."));
                pth.replace(QLatin1String(".clu."), QLatin1String(".clp."));
                if (!writeClu(c, aids, &err)) return fail(err);
                w << c;
                QFile f(pth);
                if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return fail(tr("cannot write %1").arg(pth));
                f.write(reinterpret_cast<const char*>(&maxAtom), 4);
                f.write(reinterpret_cast<const char*>(clp.constData()), 4LL * maxAtom);
                f.close();
                w << pth;
            }
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

bool KlustersDoc::stageRefinedChildren(const QString& fetPath, QString* error)
{
    QString clu = fetPath, clc = fetPath;
    clu.replace(QLatin1String(".fet."), QLatin1String(".clu."));
    clc.replace(QLatin1String(".fet."), QLatin1String(".clc."));
    if (!QFile::exists(clc)) {
        if (error) *error = tr("the refine plugin delivered no %1 (its children)").arg(QFileInfo(clc).fileName());
        return false;
    }
    QVector<int32_t> ids; QString err;
    if (!subsession::readClu(clc, &ids, &err)) { if (error) *error = err; return false; }
    if (ids.size() != reclusterSubsessionRows.size()) {
        if (error) *error = tr("%1: %2 atoms for %3 spikes").arg(QFileInfo(clc).fileName()).arg(ids.size()).arg(reclusterSubsessionRows.size());
        return false;
    }
    QFile::remove(clu);
    if (!QFile::copy(clc, clu)) { if (error) *error = tr("cannot stage %1 as %2").arg(clc, clu); return false; }
    return true;
}

int KlustersDoc::removeReclusterSubsession(const QString& tmpBase)
{
    return subsession::removeAll(tmpBase);
}

int KlustersDoc::carryBackSubsessionFeatures(const QString& tmpBase, QStringList* created, QString* error)
{
    using namespace subsession;
    if (reclusterSubsessionRows.isEmpty() || !clusteringData) return 0;
    const QString dir = QFileInfo(tmpBase).absolutePath();
    const QString tmpName = QFileInfo(tmpBase).fileName();
    const QString group = currentElectrodeGroupID();
    const QString base = documentDirectory() + QLatin1Char('/') + documentBaseName();
    const qint64 nTotal = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    QVector<int64_t> rows;
    rows.reserve(reclusterSubsessionRows.size());
    for (dataType r : reclusterSubsessionRows) rows.append(static_cast<int64_t>(r));
    // the time column: the session's spike times, as the open space holds them (the live .res)
    QVector<int64_t> times(static_cast<int>(nTotal));
    const int nbDim = clusteringData->nbOfDimensions();
    for (qint64 r = 1; r <= nTotal; ++r)
        times[static_cast<int>(r - 1)] = static_cast<int64_t>(clusteringData->featureValue(static_cast<dataType>(r), nbDim));
    int n = 0;
    for (const QString& p : groupFiles(dir, tmpName, QStringLiteral("fet"), group)) {
        const QString v = variantOf(p, tmpName, QStringLiteral("fet"), group);
        if (v.isEmpty() || v == QLatin1String("klusters") || reclusterSubsessionFets.contains(v)) continue;
        const QString dst = withVariant(base, QStringLiteral("fet"), v, group);
        QString err;
        if (!scatterFetRows(p, dst, nTotal, rows, &times, &err)) { if (error) *error = err; return -1; }
        if (created) *created << dst;
        ++n;
    }
    return n;
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
