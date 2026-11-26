/***************************************************************************
 * klustersdoc_spkc.cpp -- collision rows shown as their own unit's spike
 *
 * A spike row whose .eap row names two (or more) template classes holds two
 * units' spikes.  Here each such row gets a CORRECTED record -- the raw record
 * less its partners' fitted templates (core/decollide_fit.hpp) -- which both
 * Data layers serve in place of the .spk's (Data::setSpkcOverlay): the
 * waveform view, the mean and median waveforms and everything else that reads
 * a record through a snapshot then see the row as its own unit's spike.
 *
 * The .spk is never touched.  It stays the raw cut of the recording, so realign
 * and nudge re-cut it as before and a row cannot be subtracted twice; the
 * corrected rows are rebuilt from it (rebuildCollisionRows) and written out as
 * the sparse .spkc for other tools (writeSpkc).
 *
 * What the .eap gives is which partners and about where.  The amplitudes and
 * the exact shifts are fitted here, each time, from the records and the
 * templates as they now are.
 *
 *   own unit     the class whose home cluster is the row's cluster
 *                (decollide_fit::homeClusters / splitRow -- read off the
 *                membership, so a renumbering changes nothing; a class with no
 *                row of its own falls back on the cluster the .tcl says it was
 *                made from, while that cluster exists)
 *   templates    the mean record of the unit's rows that are NOT collisions,
 *                the kTemplateSpikes of them nearest in time to the row (in
 *                steps of half that, so neighbouring rows share one)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "klustersdoc.h"
#include <neurosuite/core/fileops.hpp>

#include "configuration.h"
#include "data.h"
#include "klustersdoc_internal.h"
#include "pca_row_projection.h"
#include "spkoverlay.h"

#include "neurosuite/core/custody.hpp"
#include "neurosuite/core/decollide_fit.hpp"
#include "neurosuite/core/neurofileio.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QDate>
#include <QDebug>
#include <cstring>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

namespace {

constexpr int kTemplateSpikes = 300;     ///< non-collision rows a template is the mean of

/** <base>.<type>[.<method>].<group>[.<stage>] next to the open .clu; false when its name does not parse. */
bool sessionAnchor(const QString& cluPath, std::string& base, int& group, std::string& stage)
{
    if (cluPath.isEmpty()) return false;
    const auto a = neurosuite::custody::parseAnchor(QFileInfo(cluPath).fileName().toStdString());
    if (!a.ok) return false;
    base  = (QFileInfo(cluPath).absolutePath() + QLatin1Char('/') + QString::fromStdString(a.base)).toStdString();
    group = a.group;
    stage = a.suffix;
    return true;
}

} // namespace

QString KlustersDoc::stageEapPath(bool* inherited) const
{
    if (inherited) *inherited = false;
    std::string base, stage; int group = 0;
    if (!sessionAnchor(url(), base, group, stage)) return QString();
    const std::string plain = neurosuite::custody::untaggedPath(base, "eap", group);
    if (stage.empty()) return QString::fromStdString(plain);
    const QString staged = QString::fromStdString(plain + "." + stage);
    if (QFileInfo::exists(staged)) return staged;
    if (inherited) *inherited = true;                 // a stage without its own .eap reads the base one
    return QString::fromStdString(plain);
}

QString KlustersDoc::stageOwnEapPath() const
{
    std::string base, stage; int group = 0;
    if (!sessionAnchor(url(), base, group, stage)) return QString();
    std::string own = neurosuite::custody::untaggedPath(base, "eap", group);
    if (!stage.empty()) own += "." + stage;
    return QString::fromStdString(own);
}

QString KlustersDoc::groupTclPath() const
{
    std::string base, stage; int group = 0;
    if (!sessionAnchor(url(), base, group, stage)) return QString();
    return QString::fromStdString(neurosuite::custody::untaggedPath(base, "tcl", group));
}

neurofileio::EapFile KlustersDoc::currentEap(const QString& path, qint64 nSpikes) const
{
    namespace nf = neurofileio;
    nf::EapFile eap = path.isEmpty() ? nf::EapFile{} : nf::readEap(path.toStdString());
    if (pendingMarks.empty()) return eap;
    int columns = 0;
    for (const PendingCell& c : pendingMarks.cells) columns = std::max(columns, c.col + 1);
    for (const PendingClass& c : pendingMarks.classes) columns = std::max(columns, c.col + 1);
    if (!eap.ok) {                                    // no file yet: a table that names nobody
        std::string base, stage; int group = 0;
        sessionAnchor(url(), base, group, stage);
        eap = nf::EapFile{};
        eap.nSpikes = nSpikes;
        eap.nClasses = std::max(128, columns);
        eap.group = group;
        eap.cells.assign(static_cast<std::size_t>(nSpikes) * eap.nClasses, nf::EAP_ABSENT);
        eap.ok = true;
    }
    if (eap.nSpikes != nSpikes) return eap;           // not this session's rows: the caller says so
    if (eap.nClasses < columns) eap = nf::growEap(eap, columns);
    for (const PendingCell& c : pendingMarks.cells)
        if (c.row >= 0 && c.row < eap.nSpikes && c.col >= 0 && c.col < eap.nClasses)
            eap.cells[static_cast<std::size_t>(c.row) * eap.nClasses + c.col] =
                c.value <= -128 ? nf::EAP_ABSENT                  // a class taken out of the row
                                : static_cast<int8_t>(std::clamp(c.value, -127, 127));
    return eap;
}

std::vector<int> KlustersDoc::collisionClassHomes(const neurofileio::EapFile& eap, const std::vector<int>& clu) const
{
    namespace nf = neurofileio;
    namespace df = neurosuite::decollide_fit;
    std::vector<int> home = df::homeClusters(eap, clu);
    QSet<int> exists;
    for (const int c : clu) exists.insert(c);
    std::vector<int> provenance(home.size(), -1);
    const QString tclPath = groupTclPath();
    const nf::TclRegistry tcl = tclPath.isEmpty() ? nf::TclRegistry{} : nf::readTcl(tclPath.toStdString());
    if (tcl.ok)
        for (const nf::TclEntry& t : tcl.entries)
            if (t.col >= 0 && t.col < static_cast<int>(home.size()) && t.provenanceClu > 1
                && exists.contains(t.provenanceClu))
                provenance[static_cast<std::size_t>(t.col)] = t.provenanceClu;
    std::vector<int> stated = provenance;             // what the .tcl on disk says
    // A class made by a mark not yet saved is in no .tcl: the cluster it was made for.
    for (const PendingClass& c : pendingMarks.classes)
        if (c.col >= 0 && c.col < static_cast<int>(home.size()) && c.cluster > 1 && exists.contains(c.cluster))
            provenance[static_cast<std::size_t>(c.col)] = c.cluster;
    // The cluster a class was made from, while it still holds the class's rows -- even when a
    // larger, mixed cluster holds more of them; and for a class of the .tcl with no row of its
    // own, while that cluster exists.
    df::preferProvenance(home, eap, clu, provenance);
    for (std::size_t j = 0; j < home.size(); ++j)
        if (home[j] < 0) home[j] = stated[j];
    return home;
}

bool KlustersDoc::writePendingClasses(int eapColumns, QString* error)
{
    namespace nf = neurofileio;
    if (pendingMarks.classes.empty()) return true;
    auto fail = [&](const QString& why) { if (error) *error = why; return false; };
    const QString path = groupTclPath();
    if (path.isEmpty()) return fail(QStringLiteral("the session's name does not parse"));
    nf::TclRegistry reg = nf::readTcl(path.toStdString());
    if (!reg.ok && QFileInfo::exists(path)) return fail(QStringLiteral("%1 is not a valid .tcl registry").arg(path));
    // As wide as the .eap it goes with (formats/eap.md: the two are widened together), and
    // never narrower than it was.
    int columns = std::max(128, eapColumns);
    for (const PendingClass& c : pendingMarks.classes) columns = std::max(columns, c.col + 1);
    if (!reg.ok) reg = nf::initTcl(columns);
    while (static_cast<int>(reg.entries.size()) < columns) {
        nf::TclEntry t; t.col = static_cast<int>(reg.entries.size()); t.status = nf::TclStatus::Free;
        reg.entries.push_back(t);
    }
    reg.nClasses = static_cast<int>(reg.entries.size());
    for (const PendingClass& c : pendingMarks.classes)
        if (reg.entries[static_cast<std::size_t>(c.col)].status != nf::TclStatus::Free)
            return fail(QStringLiteral("class %1 of %2 was taken since the collisions were marked").arg(c.col).arg(path));
    std::string base, stage; int group = 0;
    sessionAnchor(url(), base, group, stage);
    const std::string today = QDate::currentDate().toString(Qt::ISODate).toStdString();
    for (const PendingClass& c : pendingMarks.classes) {
        nf::TclEntry& t = reg.entries[static_cast<std::size_t>(c.col)];
        t.status = nf::TclStatus::Active;
        t.provenanceClu = c.cluster;
        t.provenanceStage = stage;
        t.created = today;
        t.mergedInto = -1;
    }
    if (!nf::writeTcl(path.toStdString(), reg)) return fail(QStringLiteral("could not write %1").arg(path));
    return true;
}

bool KlustersDoc::rebuildCollisionRows(CollisionRowsReport* report)
{
    namespace nf = neurofileio;
    namespace df = neurosuite::decollide_fit;
    CollisionRowsReport rep;
    auto finish = [&](bool ok) {
        lastCollisionReport = rep;
        ++collisionRowBuilds;
        // What was built holds for this membership; a later edit replaces the table.
        collisionRowsBuiltOn = clusteringData ? clusteringData->currentSnapshot()->spikesByCluster : nullptr;
        if (report) *report = rep;
        return ok;
    };

    // The clusters that hold a corrected row now: their caches go whatever happens next.
    QList<int> touched = collisionRowClusters;
    auto publish = [&](std::shared_ptr<const SpkOverlay> overlay, const QList<int>& clusters) {
        applyCollisionFeatures(overlay, rep);
        QList<int> all = touched;
        for (const int c : clusters) if (!all.contains(c)) all.append(c);
        collisionRowClusters = clusters;
        clusteringData->setSpkcOverlay(overlay, all);
        if (childData) {
            // The child layer reads the same .spk: the same records, its own clusters' caches.
            QList<int> kids;
            for (const dataType id : childData->clusterIds()) kids.append(static_cast<int>(id));
            childData->setSpkcOverlay(overlay, kids);
        }
    };

    if (!clusteringData) return finish(false);
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const int nSamp = clusteringData->nbSamplesPerWaveform();
    const int nChan = clusteringData->nbOfChannels();
    rep.eapPath = stageEapPath();
    if (clusteringData->supersampleFactor() != 1) {
        rep.skipped = QStringLiteral("a super-sampled waveform space is shown: collision rows are not corrected there");
        publish(nullptr, {});
        return finish(false);
    }
    const nf::EapFile eap = currentEap(rep.eapPath, nSpikes);   // the file, with the marks not yet saved
    if (!eap.ok || eap.nClasses <= 0) {
        publish(nullptr, {});
        return finish(false);                         // no .eap: nothing to correct
    }
    rep.eapFound = true;
    if (eap.nSpikes != nSpikes || nSamp < 4 || nChan < 1) {
        rep.skipped = QStringLiteral("the .eap holds %1 rows, the session %2 spikes").arg(eap.nSpikes).arg(nSpikes);
        publish(nullptr, {});
        return finish(false);
    }
    QElapsedTimer timer; timer.start();

    // ── the cluster of every row, and each cluster's rows in time order ──
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();
    std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
    QHash<int, QVector<int>> rowsOf;
    for (const dataType id : clusteringData->clusterIds()) {
        QVector<int> rows = clusteringData->clusterSpkIndices(static_cast<int>(id));
        std::sort(rows.begin(), rows.end());
        for (const int r : rows)
            if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
        rowsOf.insert(static_cast<int>(id), rows);
    }

    // ── the times the .eap's offsets hold for, and how far each row is from them now ──
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    auto timeNow = [&](qint64 row) { return static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim)); };
    if (collisionRefEap != rep.eapPath || static_cast<qint64>(collisionRefTimes.size()) != nSpikes) {
        collisionRefTimes.resize(static_cast<std::size_t>(nSpikes));
        for (qint64 i = 0; i < nSpikes; ++i) collisionRefTimes[static_cast<std::size_t>(i)] = timeNow(i);
        collisionRefEap = rep.eapPath;
    }
    auto moved = [&](qint64 row) { return timeNow(row) - collisionRefTimes[static_cast<std::size_t>(row)]; };

    // ── which cluster each class is ──
    const std::vector<int> home = collisionClassHomes(eap, clu);

    // ── the collision rows ──
    std::vector<char> isCollision(static_cast<std::size_t>(nSpikes), 0);
    std::vector<qint64> collisions;
    for (qint64 i = 0; i < nSpikes; ++i) {
        const int8_t* row = &eap.cells[static_cast<std::size_t>(i) * eap.nClasses];
        int n = 0;
        for (int j = 0; j < eap.nClasses && n < 2; ++j) if (nf::eapPresent(row[j])) ++n;
        if (n >= 2) { isCollision[static_cast<std::size_t>(i)] = 1; collisions.push_back(i); }
    }
    rep.collisionRows = static_cast<qint64>(collisions.size());

    // ── templates: per class, per stretch of its home cluster's non-collision rows ──
    const qint64 recBytes = static_cast<qint64>(nSamp) * nChan * static_cast<qint64>(sizeof(int16_t));
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    QHash<int, QVector<int>> cleanRowsOf;             // class -> its home cluster's non-collision rows, ascending
    QHash<qint64, std::shared_ptr<std::vector<float>>> templateCache;   // (class, stretch) -> mean record
    std::vector<int16_t> buf(rec);
    auto cleanRows = [&](int cls) -> const QVector<int>& {
        auto it = cleanRowsOf.find(cls);
        if (it != cleanRowsOf.end()) return it.value();
        QVector<int> out;
        const int h = (cls >= 0 && cls < static_cast<int>(home.size())) ? home[static_cast<std::size_t>(cls)] : -1;
        if (h > 1)
            for (const int r : rowsOf.value(h))
                if (!isCollision[static_cast<std::size_t>(r)]) out.append(r);
        return cleanRowsOf.insert(cls, out).value();
    };
    auto templateFor = [&](int cls, qint64 row) -> std::shared_ptr<std::vector<float>> {
        const QVector<int>& rows = cleanRows(cls);
        if (rows.size() < 30) return nullptr;         // too few for a template
        const int step = kTemplateSpikes / 2;
        const int pos = static_cast<int>(std::lower_bound(rows.begin(), rows.end(), static_cast<int>(row)) - rows.begin());
        const int stretch = pos / step;
        const qint64 key = (static_cast<qint64>(cls) << 32) | static_cast<qint64>(stretch);
        auto it = templateCache.find(key);
        if (it != templateCache.end()) return it.value();
        int lo = std::max(0, stretch * step + step / 2 - kTemplateSpikes / 2);
        int hi = std::min(static_cast<int>(rows.size()), lo + kTemplateSpikes);
        lo = std::max(0, hi - kTemplateSpikes);
        auto t = std::make_shared<std::vector<float>>(rec, 0.0f);
        int used = 0;
        for (int k = lo; k < hi; ++k) {
            if (!snap->readSpkRaw(buf.data(), recBytes, static_cast<qint64>(rows[k]) * recBytes)) continue;
            for (std::size_t q = 0; q < rec; ++q) (*t)[q] += buf[q];
            ++used;
        }
        if (used < 30) t.reset();
        else for (float& v : *t) v /= static_cast<float>(used);
        templateCache.insert(key, t);
        return t;
    };

    // How far a class's unit has moved since the .eap was written: the median time change of
    // (up to 301 of) its rows that are not collisions.  Its template moved the other way in the
    // window, so a partner of that class is that much LATER relative to an unmoved row.
    QHash<int, qint64>& classShift = collisionClassShift;
    classShift.clear();
    auto shiftOf = [&](int cls) -> qint64 {
        auto it = classShift.find(cls);
        if (it != classShift.end()) return it.value();
        const QVector<int>& rows = cleanRows(cls);
        qint64 med = 0;
        if (!rows.isEmpty()) {
            std::vector<qint64> d;
            const int step = std::max<int>(1, static_cast<int>(rows.size()) / 301);
            for (int k = 0; k < rows.size(); k += step) d.push_back(moved(rows[k]));
            std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
            med = d[d.size() / 2];
        }
        if (med != 0) ++rep.classesMoved;
        return classShift.insert(cls, med).value();
    };
    collisionFits.clear();

    // ── fit and subtract ──
    auto overlay = std::make_shared<SpkOverlay>();
    QList<int> clusters;
    std::vector<float> w(rec);
    df::Member own; std::vector<df::Member> partners; std::vector<df::Fit> fits;
    for (const qint64 i : collisions) {
        const int c = clu[static_cast<std::size_t>(i)];
        if (!df::splitRow(eap, i, c, home, own, partners)) continue;
        // Where to look for each partner NOW: the row moved, and so may the partner's unit have.
        const qint64 rowMoved = moved(i);
        if (rowMoved != 0) ++rep.rowsMoved;
        for (df::Member& p : partners)
            p.offset = static_cast<int>(std::clamp<qint64>(p.offset - rowMoved + shiftOf(p.classId), -127, 127));
        if (!snap->readSpkRaw(buf.data(), recBytes, i * recBytes)) continue;
        for (std::size_t q = 0; q < rec; ++q) w[q] = static_cast<float>(buf[q]);
        std::map<int, std::vector<float>> templates;
        if (const auto t = templateFor(own.classId, i)) templates.emplace(own.classId, *t);
        else { ++rep.rowsWithoutOwnTemplate; continue; }       // nothing is subtracted without it (correctRow)
        for (const df::Member& p : partners) {
            const auto t = templateFor(p.classId, i);
            if (t) templates.emplace(p.classId, *t);
            else ++rep.partnersWithoutTemplate;
        }
        const int done = df::correctRow(w, nSamp, nChan, own, partners, templates, fits);
        for (std::size_t k = 0; k < fits.size(); ++k) {
            if (templates.count(partners[k].classId)) ++rep.partnersFitted;
            if (fits[k].accepted) {
                ++rep.partnersSubtracted;
                if (fits[k].tau != partners[k].offset) ++rep.offsetsDiffer;
                const int delta = fits[k].tau - partners[k].offset;
                if (delta >= -4 && delta <= 4) ++rep.offsetDeltas[delta + 4];
                collisionFits.push_back(CollisionFit{i, partners[k].classId, fits[k].tau});
            }
        }
        if (done == 0) continue;
        df::toInt16(w, buf.data());
        overlay->records.insert(i * recBytes,
                                std::make_shared<const QByteArray>(reinterpret_cast<const char*>(buf.data()),
                                                                   static_cast<int>(recBytes)));
        ++rep.corrected;
        if (!clusters.contains(c)) clusters.append(c);
    }
    rep.milliseconds = timer.elapsed();
    publish(overlay, clusters);
    qInfo().noquote() << "[collision rows]" << QFileInfo(rep.eapPath).fileName() << ":" << rep.collisionRows
                      << "rows with two or more classes," << rep.corrected << "corrected ("
                      << rep.partnersSubtracted << "of" << rep.partnersFitted << "partners subtracted,"
                      << rep.partnersWithoutTemplate << "without a template;" << rep.rowsWithoutOwnTemplate
                      << "rows left as they are, their own unit having none),"
                      << rep.offsetsDiffer << "fitted offsets differ from where the search started;"
                      << rep.rowsMoved << "rows and" << rep.classesMoved << "partner units moved since the .eap was written;"
                      << rep.milliseconds << "ms";
    return finish(true);
}

void KlustersDoc::carryEapToSavedName()
{
    const QString from = eapBeforeSaveAs;
    eapBeforeSaveAs.clear();
    if (from.isEmpty() || !QFileInfo::exists(from)) return;
    if (stageEapPath() == from) return;                // the new name reads the same file
    std::string base, stage; int group = 0;
    if (!sessionAnchor(url(), base, group, stage)) return;
    std::string own = neurosuite::custody::untaggedPath(base, "eap", group);
    if (!stage.empty()) own += "." + stage;
    const QString target = QString::fromStdString(own);
    if (target == from) return;
    // Beside the target first, then swapped in: a reader never sees half a table.
    const QString tmp = target + QStringLiteral(".saveas.tmp");
    QFile::remove(tmp);
    if (!QFile::copy(from, tmp)
        || !neurosuite::core::replaceFile(tmp.toLocal8Bit().constData(), target.toLocal8Bit().constData())) {
        QFile::remove(tmp);
        qWarning().noquote() << "[collision rows] could not copy" << QFileInfo(from).fileName()
                             << "to" << QFileInfo(target).fileName();
        return;
    }
    qInfo().noquote() << "[collision rows]" << QFileInfo(from).fileName() << "copied to" << QFileInfo(target).fileName();
    if (collisionRefEap == from) collisionRefEap = target;   // the same table, the same times
}

void KlustersDoc::resetCollisionRows()
{
    collisionFeatureRows.clear();
    collisionRefTimes.clear();
    collisionRefEap.clear();
    collisionClassShift.clear();
    collisionFits.clear();
    pendingMarks.clear();
    collisionRowsBuiltOn.reset();
    lastCollisionReport = CollisionRowsReport();
}

void KlustersDoc::applyCollisionFeatures(const std::shared_ptr<const SpkOverlay>& overlay, CollisionRowsReport& rep)
{
    if (!clusteringData) return;
    const QString method = activeFeatureMethod();
    QHash<qint64, CollisionFeatureRow>& held = collisionFeatureRows[method];
    const int nSamp = clusteringData->nbSamplesPerWaveform(), nChan = clusteringData->nbOfChannels();
    const int nFeat = clusteringData->nbOfDimensionsTotal() - 1;          // every column but the time
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const qint64 recBytes = static_cast<qint64>(nSamp) * nChan * static_cast<qint64>(sizeof(int16_t));

    auto readRow = [&](qint64 row) {
        QVector<dataType> v(nFeat);
        for (int d = 0; d < nFeat; ++d) v[d] = clusteringData->featureValue(static_cast<dataType>(row + 1), d + 1);
        return v;
    };
    QList<int> clusters;
    auto writeRow = [&](qint64 row, const QVector<dataType>& v) {
        const QList<dataType> values(v.begin(), v.end());
        clusteringData->updateFeatureRow(static_cast<dataType>(row + 1), values);
        if (childData) childData->updateFeatureRow(static_cast<dataType>(row + 1), values);
    };

    // The basis of the shown space; without one no feature row is projected (the ones held are put back).
    neurosuite::core::PcaBasis pca;
    bool usable = false;
    if (overlay && !overlay->records.isEmpty()) {
        const QString pcaPath = klustersdoc_internal::resolveFeature(documentDirectory() + QLatin1Char('/') + documentBaseName(),
                                               QStringLiteral("pca"), currentElectrodeGroupID(), method);
        if (!QFileInfo::exists(pcaPath) || !neurosuite::core::loadPca(pcaPath.toStdString(), pca) || !pca.valid())
            rep.featuresSkipped = QStringLiteral("no PCA basis for the shown space");
        else if (pca.nCh > nChan || pca.recShift < 0 || pca.recShift + pca.data2use > nSamp || pca.nCh * pca.nComp > nFeat)
            rep.featuresSkipped = QStringLiteral("the PCA basis does not fit the shown records");
        else
            usable = true;
    }

    // Rows whose features someone else has rewritten since (a realign, a nudge, a reject): what
    // they hold now is their new uncorrected value.
    for (auto it = held.begin(); it != held.end(); ++it) {
        if (it.key() < 0 || it.key() >= nSpikes) continue;
        const QVector<dataType> now = readRow(it.key());
        if (now != it.value().written) it.value().original = now;
    }
    // Rows no longer corrected: their own features back.
    std::vector<qint64> gone;
    for (auto it = held.constBegin(); it != held.constEnd(); ++it)
        if (!usable || !overlay->records.contains(it.key() * recBytes)) gone.push_back(it.key());
    QSet<qint64> touchedRows;
    for (const qint64 row : gone) {
        if (row >= 0 && row < nSpikes && static_cast<int>(held[row].original.size()) == nFeat) {
            writeRow(row, held[row].original);
            touchedRows.insert(row);
            ++rep.featuresRestored;
        }
        held.remove(row);
    }
    // Corrected rows: the projection of the corrected record; the other feature columns as they are.
    if (usable) {
        const int nPca = pca.nCh * pca.nComp;
        std::vector<int64_t> coef(static_cast<std::size_t>(nPca));
        for (auto it = overlay->records.constBegin(); it != overlay->records.constEnd(); ++it) {
            if (it.key() % recBytes != 0 || static_cast<qint64>(it.value()->size()) != recBytes) continue;
            const qint64 row = it.key() / recBytes;
            if (row < 0 || row >= nSpikes) continue;
            const int16_t* rec = reinterpret_cast<const int16_t*>(it.value()->constData());
            klusters::projectSpikeOntoPca(pca, [&](int ch, int j) {
                return static_cast<double>(rec[static_cast<std::size_t>(pca.recShift + j) * nChan + ch]);
            }, coef.data());
            CollisionFeatureRow& h = held[row];
            if (h.original.isEmpty()) h.original = readRow(row);
            QVector<dataType> v = h.original;
            for (int k = 0; k < nPca; ++k) v[k] = static_cast<dataType>(coef[static_cast<std::size_t>(k)]);
            h.written = v;
            writeRow(row, v);
            touchedRows.insert(row);
            ++rep.featuresProjected;
        }
    }
    if (touchedRows.isEmpty()) return;
    // The clusters those rows are in: their extrema, and the views that draw them.
    QHash<qint64, int> clusterOfRow;
    for (const dataType id : clusteringData->clusterIds())
        for (const int r : clusteringData->clusterSpkIndices(static_cast<int>(id)))
            if (touchedRows.contains(r)) clusterOfRow.insert(r, static_cast<int>(id));
    for (auto it = clusterOfRow.constBegin(); it != clusterOfRow.constEnd(); ++it)
        if (!clusters.contains(it.value())) clusters.append(it.value());
    refeaturizeRealignedClusters(clusters);
    for (const int c : clusters) notifyClusterFeaturesReprojected(c);
}

bool KlustersDoc::writeCollisionOffsets(int* changed, QString* error, bool marksOnly)
{
    namespace nf = neurofileio;
    if (changed) *changed = 0;
    auto fail = [&](const QString& why) { if (error) *error = why; return false; };
    if (!clusteringData || collisionRefEap.isEmpty()) return false;
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const bool marks = !pendingMarks.empty();
    if (marksOnly && !marks) return true;
    nf::EapFile eap = currentEap(collisionRefEap, nSpikes);   // the file, with the marks not yet saved
    if (!eap.ok || eap.nSpikes != nSpikes) return fail(QStringLiteral("%1 no longer fits the session").arg(collisionRefEap));
    int n = 0;
    for (const CollisionFit& f : collisionFits) {
        if (marksOnly) break;                        // the fits are of a rebuild the preference has since turned off
        if (f.row < 0 || f.row >= nSpikes || f.classId < 0 || f.classId >= eap.nClasses) continue;
        int8_t& cell = eap.cells[static_cast<std::size_t>(f.row) * eap.nClasses + f.classId];
        const int8_t v = static_cast<int8_t>(std::clamp(f.tau, -127, 127));
        if (nf::eapPresent(cell) && cell != v) { cell = v; ++n; }
    }
    // The stage's own file: a stage that read the base .eap gets its own, as the template
    // classes do (TemplateClassStore::save).
    std::string base, stage; int group = 0;
    if (!sessionAnchor(url(), base, group, stage)) return fail(QStringLiteral("the session's name does not parse"));
    std::string target = neurosuite::custody::untaggedPath(base, "eap", group);
    if (!stage.empty()) target += "." + stage;
    // A partner no fit placed still has its offset for the OLD times: bring it to the times being
    // saved, the way a rebuild looks for it -- less the row's move, plus its unit's.
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    auto timeNow = [&](qint64 row) { return static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim)); };
    std::vector<char> fitted(static_cast<std::size_t>(nSpikes), 0);
    for (const CollisionFit& f : collisionFits) if (f.row >= 0 && f.row < nSpikes) fitted[static_cast<std::size_t>(f.row)] = 1;
    if (!marksOnly && static_cast<qint64>(collisionRefTimes.size()) == nSpikes) {
        for (qint64 i = 0; i < nSpikes; ++i) {
            if (fitted[static_cast<std::size_t>(i)]) continue;
            const qint64 moved = timeNow(i) - collisionRefTimes[static_cast<std::size_t>(i)];
            int8_t* row = &eap.cells[static_cast<std::size_t>(i) * eap.nClasses];
            int present = 0, own = -1;
            for (int j = 0; j < eap.nClasses; ++j)
                if (nf::eapPresent(row[j])) {
                    ++present;
                    if (own < 0 || std::abs(row[j]) < std::abs(row[own])) own = j;   // the class nearest the row's own time
                }
            if (present < 2) continue;                       // a row with one class: its own, at its own time
            for (int j = 0; j < eap.nClasses; ++j) {
                if (j == own || !nf::eapPresent(row[j])) continue;
                const qint64 delta = collisionClassShift.value(j, 0) - moved;
                if (delta == 0) continue;
                row[j] = static_cast<int8_t>(std::clamp<qint64>(row[j] + delta, -127, 127));
                ++n;
            }
        }
    }
    // Nothing differs from the file: it is left alone (a stage reading the base .eap does not get
    // a copy for nothing), and the times it holds for stay the ones it was read at.
    if (n == 0 && !marks) return true;
    // The classes first: a table naming a class the registry does not hold is worse than marks
    // that stay pending.
    if (marks) {
        QString why;
        if (!writePendingClasses(eap.nClasses, &why)) return fail(why);
    }
    if (!nf::writeEap(target, eap.nSpikes, eap.nClasses, eap.group, eap.flags, eap.cells))
        return fail(QStringLiteral("could not write %1").arg(QString::fromStdString(target)));
    if (marks) {
        qInfo().noquote() << "[collision rows]" << pendingMarks.rows.size() << "marked rows written to"
                          << QFileInfo(QString::fromStdString(target)).fileName();
        pendingMarks.clear();
    }
    // The offsets on disk now hold for the times as they are -- unless only the marks were
    // written: those were stored for the times the file already holds for.
    if (!marksOnly) {
        collisionRefTimes.resize(static_cast<std::size_t>(nSpikes));
        for (qint64 i = 0; i < nSpikes; ++i) collisionRefTimes[static_cast<std::size_t>(i)] = timeNow(i);
    }
    collisionRefEap = QString::fromStdString(target);
    if (changed) *changed = n;
    return true;
}

void KlustersDoc::refreshCollisionRows()
{
    if (!clusteringData) return;
    QList<int> before = collisionRowClusters;
    if (configuration().getCorrectCollisionRows()) {
        rebuildCollisionRows();
    } else if (!collisionRowClusters.isEmpty() || clusteringData->spkcIsStale()
               || clusteringData->spkcActiveRecordCount() > 0) {
        const QList<int> held = collisionRowClusters;
        collisionRowClusters.clear();
        lastCollisionReport = CollisionRowsReport();
        applyCollisionFeatures(nullptr, lastCollisionReport);
        clusteringData->setSpkcOverlay(nullptr, held);
        if (childData) {
            QList<int> kids;
            for (const dataType id : childData->clusterIds()) kids.append(static_cast<int>(id));
            childData->setSpkcOverlay(nullptr, kids);
        }
    }
    for (const int c : collisionRowClusters) if (!before.contains(c)) before.append(c);
    for (const int c : before) forceClusterRefresh(c);
}

bool KlustersDoc::collisionRowsOutdated() const
{
    if (!clusteringData || !lastCollisionReport.eapFound || !configuration().getCorrectCollisionRows()) return false;
    return clusteringData->currentSnapshot()->spikesByCluster != collisionRowsBuiltOn;
}

qint64 KlustersDoc::collisionRowsActive() const
{
    return clusteringData ? clusteringData->spkcActiveRecordCount() : 0;
}

bool KlustersDoc::collisionRowsStale() const
{
    return clusteringData && clusteringData->spkcIsStale();
}

bool KlustersDoc::writeSpkc(const QString& path, QString* error) const
{
    namespace nf = neurofileio;
    auto fail = [&](const QString& why) { if (error) *error = why; return false; };
    if (!clusteringData) return fail(QStringLiteral("no session"));
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();
    const int nSamp = clusteringData->nbSamplesPerWaveform(), nChan = clusteringData->nbOfChannels();
    const qint64 recBytes = static_cast<qint64>(nSamp) * nChan * static_cast<qint64>(sizeof(int16_t));
    nf::SpkcFile f;
    f.nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    f.nSamples = nSamp; f.nChannels = nChan;
    f.group = currentElectrodeGroupID().toInt();
    if (snap->spkcOverlay) {
        std::vector<qint64> offsets;
        offsets.reserve(static_cast<std::size_t>(snap->spkcOverlay->records.size()));
        for (auto it = snap->spkcOverlay->records.constBegin(); it != snap->spkcOverlay->records.constEnd(); ++it)
            if (static_cast<qint64>(it.value()->size()) == recBytes && it.key() % recBytes == 0) offsets.push_back(it.key());
        std::sort(offsets.begin(), offsets.end());
        const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
        f.rows.reserve(offsets.size());
        f.waveforms.resize(offsets.size() * rec);
        for (std::size_t k = 0; k < offsets.size(); ++k) {
            f.rows.push_back(offsets[k] / recBytes);
            std::memcpy(&f.waveforms[k * rec], snap->spkcOverlay->records.value(offsets[k])->constData(),
                        static_cast<std::size_t>(recBytes));
        }
    }
    if (!nf::writeSpkc(path.toStdString(), f)) return fail(QStringLiteral("could not write %1").arg(path));
    return true;
}

bool KlustersDoc::spikeRecord(qint64 row, std::vector<int16_t>& served, std::vector<int16_t>& raw) const
{
    if (!clusteringData || row < 0 || row >= static_cast<qint64>(clusteringData->totalNbOfSpikes())) return false;
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();
    const std::size_t rec = static_cast<std::size_t>(clusteringData->nbSamplesPerWaveform()) * clusteringData->nbOfChannels();
    const qint64 recBytes = static_cast<qint64>(rec * sizeof(int16_t));
    served.resize(rec); raw.resize(rec);
    return snap->readSpk(served.data(), recBytes, row * recBytes) && snap->readSpkRaw(raw.data(), recBytes, row * recBytes);
}

QVector<double> KlustersDoc::spikeFeatures(qint64 row) const
{
    QVector<double> v;
    if (!clusteringData || row < 0 || row >= static_cast<qint64>(clusteringData->totalNbOfSpikes())) return v;
    const int nFeat = clusteringData->nbOfDimensionsTotal() - 1;
    for (int d = 0; d < nFeat; ++d)
        v.append(static_cast<double>(clusteringData->featureValue(static_cast<dataType>(row + 1), d + 1)));
    return v;
}

QString KlustersDoc::spkcPathFor(const QString& method) const
{
    std::string base, stage; int group = 0;
    if (!sessionAnchor(url(), base, group, stage)) return QString();
    return QString::fromStdString(neurosuite::custody::stagePath(base, "spkc", method.toStdString(), group, stage));
}
