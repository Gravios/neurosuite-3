/***************************************************************************
 * klustersdoc_residuals.cpp -- the partner's spike of a collision row
 *
 * The corrected collision rows (klustersdoc_spkc.cpp) show a row as its own
 * unit's spike: the partner is subtracted.  This is the other half -- the
 * partner's spike, with the own unit subtracted -- for looking at the collision
 * itself and for checking that the partner named is the right one
 * (formats/spkr.md).
 *
 *   record(i) = W(t_i + tau)  -  median over j of  W(t_j + tau)
 *
 * W(t) is the window the group's .spk would hold for a spike at time t, cut from
 * the recording in the .spk's own domain (KlustersDoc::spikeCutSpecFor), so the
 * partner is centred and whole, also where it leaves row i's own window.  The j
 * are the row's NEIGHBOURS in its own cluster: of the up to kPool spikes nearest
 * to it in time that are not collisions, the kNearest whose records are closest
 * to the row's own part (its corrected record).  Cut at the same displacement,
 * their windows hold the own unit where it lies in row i's window; the median
 * over them is free of what any one of them holds besides.
 *
 * No detrending.  The recording is high-passed, and what a window keeps of the
 * background is what a .spk record of the partner's unit keeps too; a line
 * fitted over a spike's 1.6 ms would take part of the spike with it.  The
 * report gives the records' edge level beside the same rows' .spk records' so
 * that a trend would be seen.
 *
 * DUPLICATES.  The partner's spike may be a row already.  Every record is given
 * a kind (neurofileio::SpkrKind, residualtwins.h): detected when a row lies
 * within kTwin samples of t_i + tau -- the partner's own by preference, else any
 * cluster's --, repeated when another collision row's residual is the same spike,
 * recovered otherwise.  Nothing here
 * adds a row; the kinds are for the tools that do.
 *
 * The records live in memory (KlustersDoc::residuals), are shown through
 * Data::setResidualRecords -- the waveform tables only -- and are written to the
 * stage's .spkr at Save.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "klustersdoc.h"
#include "klustersdoc_internal.h"

#include "configuration.h"
#include "data.h"
#include "residualtwins.h"
#include "spkoverlay.h"

#include "neurosuite/core/custody.hpp"
#include "neurosuite/core/neurofileio.h"
#include "neurosuite/core/spike_extract.hpp"
#include "pca_row_projection.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace klustersdoc_internal;   // featureMethod, methodIsStderiv

namespace {

constexpr int kPool          = 400;   ///< spikes of the row's cluster nearest in time that are looked at
constexpr int kNearest       = 50;    ///< ... of which this many, the nearest in waveform, make the median
constexpr int kMinNeighbours = 10;    ///< a cluster with fewer clean spikes gives no residual
constexpr int kTwin          = 6;     ///< samples within which two spike times are one spike
constexpr int kChunk         = 64;    ///< rows cut from the recording in one pass
constexpr int kEdge          = 4;     ///< samples at either end of a record that make its edge level
constexpr int kAlign         = 6;     ///< samples a residual may be moved to bring its peak to the template's
constexpr int kTemplateRows  = 200;   ///< rows of the partner's cluster its local template is the mean of

/** |mean| of a record's first and last kEdge samples, over the channels. */
double edgeLevel(const int16_t* rec, int nSamp, int nChan)
{
    const int e = std::min(kEdge, nSamp / 2);
    if (e <= 0) return 0.0;
    double sum = 0.0;
    for (int s = 0; s < e; ++s)
        for (int c = 0; c < nChan; ++c)
            sum += rec[static_cast<std::size_t>(s) * nChan + c] + rec[static_cast<std::size_t>(nSamp - 1 - s) * nChan + c];
    return std::fabs(sum / (2.0 * e * nChan));
}

/** The windows of a set of jobs: per job its target's, then its neighbours'. */
struct WindowSet {
    std::vector<std::size_t> first;      ///< jobs + 1: where each job's windows start
    std::vector<int16_t>     windows;    ///< first.back() records
};

/** Cuts @p times -- per job the target's time, then each neighbour's -- from the recording in
 *  one pass, in time order (the extractor's cursor only moves forward). */
void cutWindows(FILE* fil, const neurosuite::core::SpikeWindowSpec& window, std::size_t rec,
                const std::vector<std::vector<qint64>>& times, WindowSet& ws, int64_t* outOfRange)
{
    ws.first.assign(times.size() + 1, 0);
    for (std::size_t j = 0; j < times.size(); ++j) ws.first[j + 1] = ws.first[j] + times[j].size();
    ws.windows.assign(ws.first.back() * rec, 0);
    struct Request { qint64 time; std::size_t slot; };
    std::vector<Request> requests;
    requests.reserve(ws.first.back());
    for (std::size_t j = 0; j < times.size(); ++j)
        for (std::size_t k = 0; k < times[j].size(); ++k) requests.push_back(Request{times[j][k], ws.first[j] + k});
    std::sort(requests.begin(), requests.end(), [](const Request& a, const Request& b) { return a.time < b.time; });
    neurosuite::core::extractSpikeWindows(
        fil, static_cast<int64_t>(requests.size()), window,
        [&](int64_t i) { return static_cast<int64_t>(requests[static_cast<std::size_t>(i)].time); },
        [&](int64_t i, const int16_t* row) {
            std::copy(row, row + rec, ws.windows.begin() + static_cast<std::ptrdiff_t>(requests[static_cast<std::size_t>(i)].slot * rec));
        },
        outOfRange);
}

/** Job @p job's target window less the median of its neighbours' windows, value by value. */
void targetLessMedian(const WindowSet& ws, std::size_t job, std::size_t rec, int16_t* out)
{
    const std::size_t at = ws.first[job], n = ws.first[job + 1] - at - 1;
    const int16_t* target = &ws.windows[at * rec];
    std::vector<int16_t> column(n);
    for (std::size_t i = 0; i < rec; ++i) {
        double med = 0.0;
        if (n > 0) {
            for (std::size_t k = 0; k < n; ++k) column[k] = ws.windows[(at + 1 + k) * rec + i];
            std::nth_element(column.begin(), column.begin() + static_cast<std::ptrdiff_t>(n / 2), column.end());
            med = column[n / 2];
            if (n % 2 == 0)                                    // the mean of the two middle values
                med = 0.5 * (med + *std::max_element(column.begin(), column.begin() + static_cast<std::ptrdiff_t>(n / 2)));
        }
        out[i] = static_cast<int16_t>(std::clamp<long>(std::lround(target[i] - med), -32768, 32767));
    }
}

double medianOf(std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

/** Rows @p rows (0-based, any order) of the binary .fet @p path, one after the other in
 *  @p out; false when the file is not @p nDims columns by @p nSpikes rows. */
bool readFetRows(const QString& path, const std::vector<qint64>& rows, int nDims, qint64 nSpikes, std::vector<int64_t>& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    qint32 header = 0;
    // reinterpret_cast: the .fet's header is a native int32, its body native int64 rows.
    if (f.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header) || header != nDims || nDims < 2) return false;
    const qint64 rowBytes = static_cast<qint64>(nDims) * static_cast<qint64>(sizeof(int64_t));
    if (f.size() != static_cast<qint64>(sizeof(header)) + nSpikes * rowBytes) return false;
    out.assign(rows.size() * static_cast<std::size_t>(nDims), 0);
    for (std::size_t k = 0; k < rows.size(); ++k) {
        if (rows[k] < 0 || rows[k] >= nSpikes) return false;
        if (!f.seek(static_cast<qint64>(sizeof(header)) + rows[k] * rowBytes)) return false;
        if (f.read(reinterpret_cast<char*>(&out[k * static_cast<std::size_t>(nDims)]), rowBytes) != rowBytes) return false;
    }
    return true;
}

/** The first nCh x nComp columns of @p row: @p record (a .spk record of @p nChan channels)
 *  through @p pca, as a .fet row is made. */
void projectRecord(const neurosuite::core::PcaBasis& pca, const int16_t* record, int nChan, qint64* row)
{
    std::vector<int64_t> coef(static_cast<std::size_t>(pca.nCh) * static_cast<std::size_t>(pca.nComp));
    klusters::projectSpikeOntoPca(pca, [&](int ch, int j) {
        return static_cast<double>(record[static_cast<std::size_t>(pca.recShift + j) * nChan + ch]);
    }, coef.data());
    for (std::size_t k = 0; k < coef.size(); ++k) row[k] = coef[k];
}

/** The feature spaces of @p doc a residual record can be given a row in, the .clu's own
 *  first: every usable space that is a projection of a .spk method in @p spkMethods through
 *  a basis fitting records of @p nChan x @p nSamp, and (when @p withDetached) every usable
 *  space without a basis.  @p bases gets each space's basis (unset for a detached one),
 *  @p skipped the spaces left out and why. */
QList<KlustersDoc::ResidualSpace> residualSpacesOf(const KlustersDoc& doc, const QStringList& spkMethods, int nChan, int nSamp,
                                                   std::vector<neurosuite::core::PcaBasis>& bases, QStringList& skipped,
                                                   bool withDetached = true)
{
    QList<KlustersDoc::ResidualSpace> spaces;
    bases.clear();
    const QString stem = QFileInfo(doc.url()).absolutePath() + QLatin1Char('/') + doc.documentBaseName();
    const QString group = doc.currentElectrodeGroupID();
    for (const KlustersDoc::FeatureSpace& fs : doc.availableFeatureSpaces()) {
        if (!fs.usable) { skipped << QObject::tr("%1: %2").arg(fs.method, fs.reason); continue; }
        KlustersDoc::ResidualSpace space;
        space.method = fs.method;
        // (the method of a stage's own .spk is not read off its name by custody::methodOf:
        // the stage tag follows the group)
        space.spkMethod = fs.spkMethod.isEmpty() ? featureMethod(fs.spkPath) : fs.spkMethod;
        space.fetPath = fs.fetPath;
        space.fetCommitted = fs.method == doc.cluMethod()
                                 ? doc.origFetFilePath()
                                 : doc.ownOfStage(QString::fromStdString(neurosuite::custody::methodPath(
                                       stem.toStdString(), "fet", fs.method.toStdString(), group.toInt())));
        {
            QFile f(fs.fetPath);
            qint32 header = 0;
            // reinterpret_cast: the .fet's header is a native int32.
            if (!f.open(QIODevice::ReadOnly) || f.read(reinterpret_cast<char*>(&header), sizeof(header)) != sizeof(header) || header < 2) {
                skipped << QObject::tr("%1: its .fet cannot be read").arg(fs.method);
                continue;
            }
            space.nDims = header;
        }
        neurosuite::core::PcaBasis pca;
        if (!fs.projection) {
            if (!withDetached) { skipped << QObject::tr("%1: not a projection of the waveforms (no basis)").arg(fs.method); continue; }
            space.detached = true;
        } else {
            const QString pcaPath = resolveFeature(stem, QStringLiteral("pca"), group, fs.method);
            if (!neurosuite::core::loadPca(pcaPath.toStdString(), pca) || !pca.valid()) {
                skipped << QObject::tr("%1: no readable basis %2").arg(fs.method, QFileInfo(pcaPath).fileName());
                continue;
            }
            if (neurosuite::custody::supersampleOf(space.spkMethod.toStdString()) > 1) {
                skipped << QObject::tr("%1: a super-sampled waveform space").arg(fs.method);
                continue;
            }
            if (!spkMethods.contains(space.spkMethod)) {
                skipped << QObject::tr("%1: no record in its waveform method (%2)").arg(fs.method, space.spkMethod);
                continue;
            }
            if (pca.nCh > nChan || pca.recShift < 0 || pca.recShift + pca.data2use > nSamp || pca.nCh * pca.nComp > space.nDims - 1) {
                skipped << QObject::tr("%1: its basis does not fit the records or the .fet").arg(fs.method);
                continue;
            }
            if (neurosuite::core::hasTemporalDiff(pca.method) != methodIsStderiv(space.spkMethod)) {
                skipped << QObject::tr("%1: its basis and its waveforms are in different domains").arg(fs.method);
                continue;
            }
        }
        spaces << space;
        bases.push_back(pca);
    }
    return spaces;
}

} // namespace

QString KlustersDoc::ownOfStage(const QString& shared) const
{
    if (docUrl.isEmpty() || shared.isEmpty()) return shared;
    const neurosuite::custody::Anchor anchor = neurosuite::custody::parseAnchor(QFileInfo(docUrl).fileName().toStdString());
    if (!anchor.ok || anchor.suffix.empty() || !neurosuite::custody::isClusterIdType(anchor.type)) return shared;
    const QString own = shared + QLatin1Char('.') + QString::fromStdString(anchor.suffix);
    return QFileInfo::exists(own) ? own : shared;
}

QString KlustersDoc::spkrPathFor(const QString& method) const
{
    const QString spkc = spkcPathFor(method);          // <base>.spkc.<method>.<group>[.<stage>]
    if (spkc.isEmpty()) return QString();
    const QString marker = QStringLiteral(".spkc.");
    const int at = spkc.lastIndexOf(marker);
    if (at < 0) return QString();
    return spkc.left(at) + QStringLiteral(".spkr.") + spkc.mid(at + marker.size());
}

bool KlustersDoc::residualRecord(qint64 row, std::vector<int16_t>& record, int* offset, int* classId,
                                 int* kind, qint64* twin) const
{
    const auto it = residuals.constFind(row);
    if (it == residuals.constEnd() || !it->bytes) return false;
    record.resize(static_cast<std::size_t>(it->bytes->size()) / sizeof(int16_t));
    std::memcpy(record.data(), it->bytes->constData(), record.size() * sizeof(int16_t));
    if (offset) *offset = it->offset;
    if (classId) *classId = it->classId;
    if (kind) *kind = it->kind;
    if (twin) *twin = it->twin;
    return true;
}

std::vector<int> KlustersDoc::residualNeighbours(qint64 row, const QVector<int>& clean) const
{
    std::vector<int> out;
    if (!clusteringData || clean.isEmpty()) return out;
    const std::size_t rec = static_cast<std::size_t>(clusteringData->nbSamplesPerWaveform()) * clusteringData->nbOfChannels();
    const qint64 recBytes = static_cast<qint64>(rec * sizeof(int16_t));
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();
    // Near in time: the kPool clean spikes of the cluster around the row.
    const int pos = static_cast<int>(std::lower_bound(clean.begin(), clean.end(), static_cast<int>(row)) - clean.begin());
    int lo = std::max(0, pos - kPool / 2);
    const int hi = std::min(static_cast<int>(clean.size()), lo + kPool);
    lo = std::max(0, hi - kPool);
    // Near in waveform: against the row's own part, its corrected record.
    std::vector<int16_t> own(rec), other(rec);
    if (!snap->readSpk(own.data(), recBytes, row * recBytes)) return out;
    std::vector<std::pair<double, int>> byDistance;
    byDistance.reserve(static_cast<std::size_t>(hi - lo));
    for (int k = lo; k < hi; ++k) {
        if (clean[k] == row || !snap->readSpk(other.data(), recBytes, static_cast<qint64>(clean[k]) * recBytes)) continue;
        double d = 0.0;
        for (std::size_t i = 0; i < rec; ++i) { const double x = static_cast<double>(own[i]) - other[i]; d += x * x; }
        byDistance.emplace_back(d, clean[k]);
    }
    if (static_cast<int>(byDistance.size()) < kMinNeighbours) return out;
    const std::size_t keep = std::min<std::size_t>(byDistance.size(),
        std::max<std::size_t>(kMinNeighbours, std::min<std::size_t>(kNearest, byDistance.size() / 4)));
    std::partial_sort(byDistance.begin(), byDistance.begin() + static_cast<std::ptrdiff_t>(keep), byDistance.end());
    for (std::size_t k = 0; k < keep; ++k) out.push_back(byDistance[k].second);
    return out;
}

KlustersDoc::ResidualReport KlustersDoc::rebuildResiduals()
{
    namespace nf = neurofileio;
    ResidualReport R;
    QElapsedTimer timer; timer.start();
    if (!clusteringData) { R.reason = tr("No session is open."); return R; }
    if (clusteringData->supersampleFactor() != 1) {
        R.reason = tr("A super-sampled waveform space is shown: residuals are made in the session's own.");
        return R;
    }
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const int nSamp = clusteringData->nbSamplesPerWaveform();
    const int nChan = clusteringData->nbOfChannels();
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    const qint64 recBytes = static_cast<qint64>(rec * sizeof(int16_t));
    if (nSpikes < 1 || rec == 0) { R.reason = tr("The document carries no waveforms."); return R; }
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    auto timeOf = [&](qint64 row) { return static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim)); };
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();

    // ── the cluster of every row, each cluster's rows ──
    std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
    QHash<int, QVector<int>> rowsOfCluster;
    for (const dataType id : clusteringData->clusterIds()) {
        QVector<int> rows = clusteringData->clusterSpkIndices(static_cast<int>(id));
        std::sort(rows.begin(), rows.end());
        for (const int r : rows)
            if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
        rowsOfCluster.insert(static_cast<int>(id), rows);
    }

    // ── the stage's .spkr, once: the records a Save left ──
    if (!residualsRead) {
        residualsRead = true;
        const QString path = spkrPathFor(activeFeatureMethod());
        const nf::SpkrFile f = path.isEmpty() ? nf::SpkrFile{} : nf::readSpkr(path.toStdString());
        if (f.ok && f.nSpikes == nSpikes && f.nSamples == nSamp && f.nChannels == nChan) {
            for (std::size_t k = 0; k < f.rows.size(); ++k) {
                Residual r;
                r.time = timeOf(f.rows[k]);                 // the file was written for the saved times
                r.cluster = clu[static_cast<std::size_t>(f.rows[k])];
                r.offset = f.offsets[k]; r.madeFor = f.offsets[k];
                r.classId = f.classes[k]; r.kind = f.kinds[k]; r.twin = f.twins[k];
                // reinterpret_cast: the int16 records as the bytes the overlay serves.
                r.bytes = std::make_shared<const QByteArray>(reinterpret_cast<const char*>(&f.waveforms[k * rec]),
                                                             static_cast<qsizetype>(recBytes));
                residuals.insert(f.rows[k], r);
            }
        }
    }

    // ── the rows wanted: each with a fitted partner (the first of the row) ──
    struct Want { qint64 row; int classId; int tau; };
    std::vector<Want> wants;
    {
        QSet<qint64> seen;
        for (const CollisionFit& f : collisionFits)
            if (f.row >= 0 && f.row < nSpikes && !seen.contains(f.row)) { seen.insert(f.row); wants.push_back(Want{f.row, f.classId, f.tau}); }
        std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) { return a.row < b.row; });
    }
    R.wanted = static_cast<qint64>(wants.size());
    {
        QSet<qint64> keep;
        for (const Want& w : wants) keep.insert(w.row);
        for (auto it = residuals.begin(); it != residuals.end();)
            if (!keep.contains(it.key())) { it = residuals.erase(it); ++R.dropped; } else ++it;
        if (R.dropped > 0) ++residualsEpoch;
    }
    if (wants.empty()) {
        if (collisionFits.empty() && !configuration().getCorrectCollisionRows())
            R.reason = tr("Preferences > Cluster view > Subtract the partner from collision rows is off: no partner is fitted, so there is none to show.");
        R.ok = true;
        R.milliseconds = timer.elapsed();
        return R;
    }
    std::vector<char> isCollision(static_cast<std::size_t>(nSpikes), 0);
    for (const Want& w : wants) isCollision[static_cast<std::size_t>(w.row)] = 1;
    QHash<int, QVector<int>> cleanOf;                      // per cluster: its rows that are not collisions
    auto cleanRows = [&](int cluster) -> const QVector<int>& {
        auto it = cleanOf.find(cluster);
        if (it != cleanOf.end()) return it.value();
        QVector<int> rows;
        for (const int r : rowsOfCluster.value(cluster))
            if (!isCollision[static_cast<std::size_t>(r)]) rows.append(r);
        return cleanOf.insert(cluster, rows).value();
    };

    // ── the recording, when it can be cut in the .spk's own domain ──
    const QString spkMethod = featureMethod(clusteringData->getSpkFileName());
    const auto token = neurosuite::custody::parseMethodToken(spkMethod.toStdString());
    const bool cuttable = methodIsStderiv(spkMethod)
                          || (token.family == "standard" && token.kind == '\0' && token.lag == 0);
    const QString recording = rawRecordingPath();
    const bool recordingThere = cuttable && QFileInfo::exists(recording);

    // ── which records are still what they were made for ──
    std::vector<Want> todo;
    for (const Want& w : wants) {
        const auto it = residuals.constFind(w.row);
        // (a record made from the .spk -- centred on the row, not on the partner -- is made
        // again once there is a recording to cut it from)
        if (it != residuals.constEnd() && it->bytes && it->time == timeOf(w.row)
            && it->cluster == clu[static_cast<std::size_t>(w.row)] && it->madeFor == w.tau && it->classId == w.classId
            && !(recordingThere && it->offset != it->madeFor))
            ++R.kept;
        else
            todo.push_back(w);
    }

    FILE* fil = (recordingThere && !todo.empty())
                    ? std::fopen(recording.toLocal8Bit().constData(), "rb") : nullptr;
    SpikeCutSpec cut;
    QString cutLog;
    if (fil) {
        QTextStream log(&cutLog);
        spikeCutSpecFor(spkMethod, nullptr, cut, log);
        if (cut.window.nSamp != nSamp || cut.window.nChan != nChan) { std::fclose(fil); fil = nullptr; }
    }
    R.fromRecording = fil != nullptr || (recordingThere && todo.empty());
    R.source = fil ? QFileInfo(recording).fileName() : QFileInfo(clusteringData->getSpkFileName()).fileName();

    // ── the records ──
    std::vector<int16_t> own(rec);
    std::vector<double> edgeNew, edgeOld;
    struct Job { Want want; std::vector<int> neighbours; qint64 time; };
    for (std::size_t c0 = 0; c0 < todo.size(); c0 += kChunk) {
        const std::size_t c1 = std::min(todo.size(), c0 + kChunk);
        std::vector<Job> jobs;
        for (std::size_t q = c0; q < c1; ++q) {
            const Want& w = todo[q];
            const int cluster = clu[static_cast<std::size_t>(w.row)];
            const QVector<int>& clean = cleanRows(cluster);
            if (cluster <= 1 || clean.size() < kMinNeighbours) { ++R.withoutNeighbours; residuals.remove(w.row); continue; }
            // Near in time, and of those near in waveform.
            Job j;
            j.want = w; j.time = timeOf(w.row);
            j.neighbours = residualNeighbours(w.row, clean);
            if (static_cast<int>(j.neighbours.size()) < kMinNeighbours) { ++R.withoutNeighbours; residuals.remove(w.row); continue; }
            jobs.push_back(std::move(j));
        }
        if (jobs.empty()) continue;

        // The windows: the row's and each neighbour's, at the partner's displacement.  From
        // the recording they are cut in one pass; without it they are the .spk's own records,
        // at no displacement.
        WindowSet ws;
        if (fil) {
            std::vector<std::vector<qint64>> times(jobs.size());
            for (std::size_t j = 0; j < jobs.size(); ++j) {
                times[j].push_back(jobs[j].time + jobs[j].want.tau);
                for (const int nb : jobs[j].neighbours) times[j].push_back(timeOf(nb) + jobs[j].want.tau);
            }
            int64_t outOfRange = 0;
            cutWindows(fil, cut.window, rec, times, ws, &outOfRange);
            R.outOfRecording += outOfRange;
        } else {
            ws.first.assign(jobs.size() + 1, 0);
            for (std::size_t j = 0; j < jobs.size(); ++j) ws.first[j + 1] = ws.first[j] + 1 + jobs[j].neighbours.size();
            ws.windows.assign(ws.first.back() * rec, 0);
            for (std::size_t j = 0; j < jobs.size(); ++j) {
                snap->readSpkRaw(&ws.windows[ws.first[j] * rec], recBytes, jobs[j].want.row * recBytes);
                for (std::size_t k = 0; k < jobs[j].neighbours.size(); ++k)
                    snap->readSpk(&ws.windows[(ws.first[j] + 1 + k) * rec], recBytes, static_cast<qint64>(jobs[j].neighbours[k]) * recBytes);
            }
        }

        std::vector<int16_t> out(rec);
        for (std::size_t j = 0; j < jobs.size(); ++j) {
            targetLessMedian(ws, j, rec, out.data());
            Residual r;
            r.time = jobs[j].time;
            r.cluster = clu[static_cast<std::size_t>(jobs[j].want.row)];
            r.offset = fil ? jobs[j].want.tau : 0;             // where the record is centred
            r.madeFor = jobs[j].want.tau;                      // the fitted offset it was made for
            r.classId = jobs[j].want.classId;
            // reinterpret_cast: the int16 record as the bytes the overlay serves.
            r.bytes = std::make_shared<const QByteArray>(reinterpret_cast<const char*>(out.data()), static_cast<qsizetype>(recBytes));
            residuals.insert(jobs[j].want.row, r);
            ++R.computed;
            edgeNew.push_back(edgeLevel(out.data(), nSamp, nChan));
            if (snap->readSpkRaw(own.data(), recBytes, jobs[j].want.row * recBytes)) edgeOld.push_back(edgeLevel(own.data(), nSamp, nChan));
        }
    }
    if (fil) std::fclose(fil);
    R.edgeOfResiduals = medianOf(edgeNew);
    R.edgeOfRecords = medianOf(edgeOld);
    if (R.computed > 0 || R.withoutNeighbours > 0) ++residualsEpoch;

    // ── is the residual spike a row already? (every record, every time: it depends on the others) ──
    {
        const nf::EapFile eap = currentEap(stageEapPath(), nSpikes);
        const std::vector<int> home = (eap.ok && eap.nSpikes == nSpikes) ? collisionClassHomes(eap, clu) : std::vector<int>();
        QHash<int, std::vector<std::pair<long long, long long>>> timesOf;  // per cluster: (time, row), ascending
        auto times = [&](int cluster) -> const std::vector<std::pair<long long, long long>>& {
            auto it = timesOf.find(cluster);
            if (it != timesOf.end()) return it.value();
            std::vector<std::pair<long long, long long>> v;
            for (const int r : rowsOfCluster.value(cluster)) v.emplace_back(timeOf(r), r);
            std::sort(v.begin(), v.end());
            return timesOf.insert(cluster, v).value();
        };
        std::vector<residualtwins::Spike> spikes;
        for (const Want& w : wants) {
            if (!residuals.contains(w.row)) continue;
            const int partner = (w.classId >= 0 && w.classId < static_cast<int>(home.size())) ? home[static_cast<std::size_t>(w.classId)] : -1;
            spikes.push_back(residualtwins::Spike{timeOf(w.row) + w.tau, w.row, partner});
        }
        residualtwins::Rows allRows;                       // every row of the group, by time
        allRows.reserve(static_cast<std::size_t>(nSpikes));
        for (qint64 i = 0; i < nSpikes; ++i) allRows.emplace_back(timeOf(i), i);
        std::sort(allRows.begin(), allRows.end());
        const std::vector<residualtwins::Verdict> verdicts = residualtwins::classify(
            spikes, [&](int cluster) -> const residualtwins::Rows& { return times(cluster); }, allRows, kTwin);
        for (std::size_t i = 0; i < spikes.size(); ++i) {
            Residual& r = residuals[spikes[i].row];
            r.kind = verdicts[i].kind;
            r.twin = verdicts[i].twin;
        }
        for (auto it = residuals.constBegin(); it != residuals.constEnd(); ++it) {
            if (it->kind == static_cast<int>(nf::SpkrKind::Detected)) ++R.detected;
            else if (it->kind == static_cast<int>(nf::SpkrKind::Repeated)) ++R.repeated;
            else ++R.recovered;
        }
    }

    R.ok = true;
    R.milliseconds = timer.elapsed();
    qInfo().noquote() << "[residuals]" << R.wanted << "rows with a fitted partner:" << R.computed << "records made from" << R.source
                      << (R.fromRecording ? "(centred on the partner)," : "(the row's own window: no recording to cut),")
                      << R.kept << "kept," << R.dropped << "dropped," << R.withoutNeighbours << "without neighbours;"
                      << R.recovered << "recovered," << R.detected << "already a row of the partner's," << R.repeated << "repeated;"
                      << "edge level" << R.edgeOfResiduals << "(their .spk records:" << R.edgeOfRecords << ");" << R.milliseconds << "ms";
    return R;
}

void KlustersDoc::pushResiduals()
{
    if (!clusteringData) return;
    std::shared_ptr<SpkOverlay> overlay;
    if (residualsOn && !residuals.isEmpty()) {
        const qint64 recBytes = static_cast<qint64>(clusteringData->nbSamplesPerWaveform()) * clusteringData->nbOfChannels()
                                * static_cast<qint64>(sizeof(int16_t));
        overlay = std::make_shared<SpkOverlay>();
        for (auto it = residuals.constBegin(); it != residuals.constEnd(); ++it)
            if (it->bytes && static_cast<qint64>(it->bytes->size()) == recBytes) overlay->records.insert(it.key() * recBytes, it->bytes);
    }
    clusteringData->setResidualRecords(overlay);
    if (childData) childData->setResidualRecords(overlay);
    for (const dataType id : clusteringData->clusterIds()) forceClusterRefresh(static_cast<int>(id));
}

KlustersDoc::ResidualReport KlustersDoc::showResiduals(bool on)
{
    ResidualReport R;
    residualsOn = on;
    if (on) R = rebuildResiduals();
    else R.ok = true;
    pushResiduals();
    return R;
}

bool KlustersDoc::writeSpkr(const QString& path, QString* error) const
{
    namespace nf = neurofileio;
    auto fail = [&](const QString& why) { if (error) *error = why; return false; };
    if (!clusteringData) return fail(QStringLiteral("no session"));
    const int nSamp = clusteringData->nbSamplesPerWaveform(), nChan = clusteringData->nbOfChannels();
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    nf::SpkrFile f;
    f.nSpikes = static_cast<int64_t>(clusteringData->totalNbOfSpikes());
    f.nSamples = nSamp; f.nChannels = nChan;
    f.group = currentElectrodeGroupID().toInt();
    QList<qint64> rows = residuals.keys();
    std::sort(rows.begin(), rows.end());
    for (const qint64 row : rows) {
        const Residual& r = residuals[row];
        if (!r.bytes || static_cast<std::size_t>(r.bytes->size()) != rec * sizeof(int16_t)) continue;
        f.rows.push_back(row);
        f.offsets.push_back(r.offset);
        f.classes.push_back(r.classId);
        f.kinds.push_back(r.kind);
        f.twins.push_back(r.twin);
        const std::size_t at = f.waveforms.size();
        f.waveforms.resize(at + rec);
        std::memcpy(&f.waveforms[at], r.bytes->constData(), rec * sizeof(int16_t));
    }
    if (!nf::writeSpkr(path.toStdString(), f)) return fail(QStringLiteral("could not write %1").arg(path));
    return true;
}

QString KlustersDoc::fetrPathFor(const QString& method) const
{
    const QString spkc = spkcPathFor(method);          // <base>.spkc.<method>.<group>[.<stage>]
    if (spkc.isEmpty()) return QString();
    const QString marker = QStringLiteral(".spkc.");
    const int at = spkc.lastIndexOf(marker);
    if (at < 0) return QString();
    return spkc.left(at) + QStringLiteral(".fetr.") + spkc.mid(at + marker.size());
}

KlustersDoc::FetrReport KlustersDoc::writeFetrFiles()
{
    namespace nf = neurofileio;
    FetrReport F;
    if (!clusteringData) return F;
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const int nSamp = clusteringData->nbSamplesPerWaveform();
    const int nChan = clusteringData->nbOfChannels();
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    auto timeOf = [&](qint64 row) { return static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim)); };

    // ── the records, as the .spkr lists them ──
    std::vector<qint64> rows;
    {
        QList<qint64> keys = residuals.keys();
        std::sort(keys.begin(), keys.end());
        for (const qint64 row : keys) {
            const Residual& r = residuals[row];
            if (r.bytes && static_cast<std::size_t>(r.bytes->size()) == rec * sizeof(int16_t)) rows.push_back(row);
        }
    }
    F.rows = static_cast<qint64>(rows.size());

    // ── the .spk methods a record can be had in: the shown one, and -- when the records are
    // centred on the partner, cut from the recording -- every one the recording can be cut in ──
    const QString shownMethod = featureMethod(clusteringData->getSpkFileName());
    const QString recording = rawRecordingPath();
    bool fromRecording = QFileInfo::exists(recording);
    for (const qint64 row : rows) fromRecording = fromRecording && residuals[row].offset == residuals[row].madeFor;
    QHash<QString, SpikeCutSpec> cutOf;
    QStringList methods{shownMethod};
    if (fromRecording) {
        for (const FeatureSpace& fs : availableFeatureSpaces()) {
            const QString m = fs.spkMethod.isEmpty() ? featureMethod(fs.spkPath) : fs.spkMethod;
            if (!fs.usable || methods.contains(m)) continue;
            const auto token = neurosuite::custody::parseMethodToken(m.toStdString());
            if (!(methodIsStderiv(m) || (token.family == "standard" && token.kind == '\0' && token.lag == 0))) continue;
            SpikeCutSpec cut;
            QString cutLog;
            QTextStream log(&cutLog);
            spikeCutSpecFor(m, nullptr, cut, log);
            if (cut.window.nSamp != nSamp || cut.window.nChan != nChan) continue;
            cutOf.insert(m, cut);
            methods << m;
        }
    }
    std::vector<neurosuite::core::PcaBasis> bases;
    const QList<ResidualSpace> spaces = residualSpacesOf(*this, methods, nChan, nSamp, bases, F.skipped, /*withDetached=*/false);

    // A space that gets no .fetr keeps none from before: it would be read as current.
    for (const QString& line : F.skipped) {
        const QString path = fetrPathFor(line.left(line.indexOf(QLatin1Char(':'))));
        if (!path.isEmpty() && QFileInfo::exists(path)) QFile::remove(path);
        fetrEpochs.remove(path);
    }

    // ── the records of a method: those in memory for the shown one, cut again for another ──
    QHash<QString, std::vector<int16_t>> recordsOf;
    auto records = [&](const QString& m) -> const std::vector<int16_t>* {
        const auto have = recordsOf.constFind(m);
        if (have != recordsOf.constEnd()) return &have.value();
        std::vector<int16_t> out(rows.size() * rec);
        if (m == shownMethod) {
            for (std::size_t k = 0; k < rows.size(); ++k) std::memcpy(&out[k * rec], residuals[rows[k]].bytes->constData(), rec * sizeof(int16_t));
        } else {
            // The same neighbours and the same displacement as the record in memory, the
            // windows in this method's domain.
            std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
            QHash<int, QVector<int>> cleanOf;
            for (const dataType id : clusteringData->clusterIds()) {
                QVector<int> rowsOfIt = clusteringData->clusterSpkIndices(static_cast<int>(id));
                std::sort(rowsOfIt.begin(), rowsOfIt.end());
                QVector<int> clean;
                for (const int r : rowsOfIt) {
                    if (r < 0 || r >= nSpikes) continue;
                    clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
                    if (!residuals.contains(r)) clean.append(r);
                }
                cleanOf.insert(static_cast<int>(id), clean);
            }
            FILE* fil = std::fopen(recording.toLocal8Bit().constData(), "rb");
            if (!fil) return nullptr;
            for (std::size_t c0 = 0; c0 < rows.size(); c0 += kChunk) {
                const std::size_t c1 = std::min(rows.size(), c0 + kChunk);
                std::vector<std::vector<qint64>> times(c1 - c0);
                for (std::size_t k = c0; k < c1; ++k) {
                    const int offset = residuals[rows[k]].offset;
                    times[k - c0].push_back(timeOf(rows[k]) + offset);
                    for (const int nb : residualNeighbours(rows[k], cleanOf.value(clu[static_cast<std::size_t>(rows[k])])))
                        times[k - c0].push_back(timeOf(nb) + offset);
                }
                WindowSet ws;
                int64_t outOfRange = 0;
                cutWindows(fil, cutOf[m].window, rec, times, ws, &outOfRange);
                for (std::size_t k = c0; k < c1; ++k) targetLessMedian(ws, k - c0, rec, &out[k * rec]);
            }
            std::fclose(fil);
            F.recut << m;
        }
        return &recordsOf.insert(m, std::move(out)).value();
    };

    for (qsizetype q = 0; q < spaces.size(); ++q) {
        const ResidualSpace& space = spaces[q];
        const QString path = fetrPathFor(space.method);
        if (path.isEmpty()) continue;
        if (fetrEpochs.value(path, 0) == residualsEpoch && QFileInfo::exists(path)) { F.current << space.method; continue; }
        const std::size_t nd = static_cast<std::size_t>(space.nDims);
        nf::FetrFile f;
        f.nSpikes = nSpikes; f.nFeatures = space.nDims; f.group = currentElectrodeGroupID().toInt();
        f.rows.assign(rows.begin(), rows.end());
        // The columns the basis does not give are the row's own in this space.
        const std::vector<int16_t>* recs = rows.empty() ? nullptr : records(space.spkMethod);
        if (!rows.empty() && (!recs || !readFetRows(space.fetPath, rows, space.nDims, nSpikes, f.values))) {
            F.skipped << tr("%1: its records or its .fet could not be read").arg(space.method);
            continue;
        }
        std::vector<qint64> row(nd);
        for (std::size_t k = 0; k < rows.size(); ++k) {
            for (std::size_t d = 0; d < nd; ++d) row[d] = f.values[k * nd + d];
            projectRecord(bases[static_cast<std::size_t>(q)], &(*recs)[k * rec], nChan, row.data());
            row[nd - 1] = timeOf(rows[k]) + residuals[rows[k]].offset;
            for (std::size_t d = 0; d < nd; ++d) f.values[k * nd + d] = row[d];
        }
        if (!nf::writeFetr(path.toStdString(), f)) { F.skipped << tr("%1: could not write %2").arg(space.method, QFileInfo(path).fileName()); continue; }
        fetrEpochs.insert(path, residualsEpoch);
        F.written << space.method;
    }
    qInfo().noquote() << "[residuals]" << F.rows << "records in feature space: .fetr written for" << (F.written.isEmpty() ? QStringLiteral("none") : F.written.join(QStringLiteral(", ")))
                      << (F.current.isEmpty() ? QString() : QStringLiteral("; current: ") + F.current.join(QStringLiteral(", ")))
                      << (F.recut.isEmpty() ? QString() : QStringLiteral("; records cut again in: ") + F.recut.join(QStringLiteral(", ")))
                      << (F.skipped.isEmpty() ? QString() : QStringLiteral("; none for: ") + F.skipped.join(QStringLiteral("; ")));
    return F;
}

KlustersDoc::ResidualSpikes KlustersDoc::alignedResidualSpikes(const QList<int>& clusters, bool children, const QStringList& spkMethods)
{
    namespace nf = neurofileio;
    ResidualSpikes S;
    if (!clusteringData) { S.reason = tr("No session is open."); return S; }
    Data* layer = children ? childData : clusteringData;
    if (!layer) { S.reason = tr("The session has no child clustering."); return S; }
    const ResidualReport built = rebuildResiduals();
    if (!built.ok) { S.reason = built.reason; return S; }
    if (residuals.isEmpty()) {
        S.reason = !built.reason.isEmpty() ? built.reason : tr("No row has a fitted partner (Clusters > Mark Collisions finds them).");
        return S;
    }
    const QString recording = rawRecordingPath();
    if (!QFileInfo::exists(recording)) {
        S.reason = tr("There is no recording (%1) to cut the spikes from.").arg(QFileInfo(recording).fileName());
        return S;
    }
    S.source = QFileInfo(recording).fileName();
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const int nSamp = clusteringData->nbSamplesPerWaveform();
    const int nChan = clusteringData->nbOfChannels();
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    const qint64 recBytes = static_cast<qint64>(rec * sizeof(int16_t));
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    const int nFeat = timeDim - 1;
    auto timeOf = [&](qint64 row) { return static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim)); };
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();
    const int peak = std::clamp(clusteringData->getPeakPositionInWaveform() - 1, 0, nSamp - 1);

    // ── every method must be one the recording can be cut in ──
    const QString shownMethod = featureMethod(clusteringData->getSpkFileName());
    QStringList methods = spkMethods;
    if (!methods.contains(shownMethod)) methods.prepend(shownMethod);
    QHash<QString, SpikeCutSpec> cutOf;
    for (const QString& m : methods) {
        const auto token = neurosuite::custody::parseMethodToken(m.toStdString());
        const bool cuttable = methodIsStderiv(m) || (token.family == "standard" && token.kind == '\0' && token.lag == 0);
        SpikeCutSpec cut;
        QString cutLog;
        QTextStream log(&cutLog);
        if (cuttable) spikeCutSpecFor(m, nullptr, cut, log);
        if (!cuttable || cut.window.nSamp != nSamp || cut.window.nChan != nChan) {
            if (m != shownMethod) {
                S.methodsSkipped << tr("%1: cannot be cut from the recording").arg(m);
                continue;
            }
            S.reason = tr("The shown .spk (method %1) cannot be cut from the recording (only the standard and the stderiv "
                          "spaces can), so the new rows would have no record.").arg(m);
            return S;
        }
        cutOf.insert(m, cut);
    }
    methods = cutOf.keys();
    methods.sort();
    S.methods = methods;

    // ── the cluster of every row; the rows that are collisions; every row by time ──
    std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
    QHash<int, QVector<int>> rowsOfCluster;
    for (const dataType id : clusteringData->clusterIds()) {
        QVector<int> rows = clusteringData->clusterSpkIndices(static_cast<int>(id));
        std::sort(rows.begin(), rows.end());
        for (const int r : rows)
            if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
        rowsOfCluster.insert(static_cast<int>(id), rows);
    }
    QHash<int, QVector<int>> cleanOf;
    auto cleanRows = [&](int cluster) -> const QVector<int>& {
        auto it = cleanOf.find(cluster);
        if (it != cleanOf.end()) return it.value();
        QVector<int> rows;
        for (const int r : rowsOfCluster.value(cluster))
            if (!residuals.contains(r)) rows.append(r);
        return cleanOf.insert(cluster, rows).value();
    };
    residualtwins::Rows allRows;
    allRows.reserve(static_cast<std::size_t>(nSpikes));
    for (qint64 i = 0; i < nSpikes; ++i) allRows.emplace_back(timeOf(i), i);
    std::sort(allRows.begin(), allRows.end());
    const nf::EapFile eap = currentEap(stageEapPath(), nSpikes);
    const std::vector<int> home = (eap.ok && eap.nSpikes == nSpikes) ? collisionClassHomes(eap, clu) : std::vector<int>();

    // ── the partner's template around a row: where its peak is, on which channel, with which sign ──
    struct Peak { int sample = 0; int channel = 0; int sign = -1; bool ok = false; };
    QHash<QPair<int, int>, Peak> peakOf;                       // (cluster, stretch of kTemplateRows) -> peak
    std::vector<int16_t> buf(rec);
    auto templatePeak = [&](int cluster, qint64 row) -> Peak {
        const QVector<int>& rows = cleanRows(cluster);
        if (rows.size() < kMinNeighbours) return Peak();
        const int pos = static_cast<int>(std::lower_bound(rows.begin(), rows.end(), static_cast<int>(row)) - rows.begin());
        const QPair<int, int> key(cluster, pos / kTemplateRows);
        const auto it = peakOf.constFind(key);
        if (it != peakOf.constEnd()) return it.value();
        int lo = std::max(0, key.second * kTemplateRows - kTemplateRows / 2);
        const int hi = std::min(static_cast<int>(rows.size()), lo + 2 * kTemplateRows);
        lo = std::max(0, hi - 2 * kTemplateRows);
        std::vector<double> mean(rec, 0.0);
        int used = 0;
        const int step = std::max(1, (hi - lo) / kTemplateRows);
        for (int k = lo; k < hi; k += step) {
            if (!snap->readSpk(buf.data(), recBytes, static_cast<qint64>(rows[k]) * recBytes)) continue;
            for (std::size_t i = 0; i < rec; ++i) mean[i] += buf[i];
            ++used;
        }
        Peak p;
        if (used > 0) {
            double best = -1.0;
            for (int s = std::max(0, peak - kAlign); s <= std::min(nSamp - 1, peak + kAlign); ++s)
                for (int c = 0; c < nChan; ++c) {
                    const double v = mean[static_cast<std::size_t>(s) * nChan + c];
                    if (std::fabs(v) > best) { best = std::fabs(v); p.sample = s; p.channel = c; p.sign = v < 0.0 ? -1 : 1; }
                }
            p.ok = best > 0.0;
        }
        peakOf.insert(key, p);
        return p;
    };

    // ── the candidates: recovered residuals of the selection, each aligned at its peak ──
    struct Candidate { qint64 row; qint64 time; int partner; int displacement; int shift; };
    std::vector<Candidate> candidates;
    for (const int id : clusters) {
        for (const int r : layer->clusterSpkIndices(id)) {
            const auto it = residuals.constFind(r);
            if (it == residuals.constEnd() || !it->bytes) continue;
            ++S.candidates;
            if (it->kind != static_cast<int>(nf::SpkrKind::Recovered) || it->offset != it->madeFor) { ++S.known; continue; }
            const int partner = (it->classId >= 0 && it->classId < static_cast<int>(home.size())) ? home[static_cast<std::size_t>(it->classId)] : -1;
            if (partner <= 1) { ++S.unmatched; continue; }
            const Peak p = templatePeak(partner, r);
            int shift = 0;
            if (p.ok) {
                // reinterpret_cast: the stored bytes as the int16 record they are.
                const int16_t* record = reinterpret_cast<const int16_t*>(it->bytes->constData());
                double best = 0.0;
                bool any = false;
                for (int s = std::max(0, p.sample - kAlign); s <= std::min(nSamp - 1, p.sample + kAlign); ++s) {
                    const double v = p.sign * static_cast<double>(record[static_cast<std::size_t>(s) * nChan + p.channel]);
                    if (!any || v > best) { best = v; shift = s - p.sample; any = true; }
                }
            }
            candidates.push_back(Candidate{r, timeOf(r) + it->offset + shift, partner, it->offset + shift, shift});
        }
    }
    // Nothing is made that a row already holds: not a row at the aligned time, and one spike once.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.partner != b.partner ? a.partner < b.partner : (a.time != b.time ? a.time < b.time : a.row < b.row);
    });
    std::vector<Candidate> kept;
    for (const Candidate& c : candidates) {
        if (residualtwins::nearestRow(allRows, c.time, kTwin, c.row) >= 0) { ++S.alreadyRow; continue; }
        if (!kept.empty() && kept.back().partner == c.partner && c.time - kept.back().time <= kTwin) { ++S.repeated; continue; }
        kept.push_back(c);
    }
    std::sort(kept.begin(), kept.end(), [](const Candidate& a, const Candidate& b) { return a.time != b.time ? a.time < b.time : a.row < b.row; });

    // ── their neighbours, once (the same for every method) ──
    struct Job { Candidate c; std::vector<int> neighbours; };
    std::vector<Job> jobs;
    for (const Candidate& c : kept) {
        Job j;
        j.c = c;
        j.neighbours = residualNeighbours(c.row, cleanRows(clu[static_cast<std::size_t>(c.row)]));
        if (static_cast<int>(j.neighbours.size()) < kMinNeighbours) { ++S.withoutNeighbours; continue; }
        jobs.push_back(std::move(j));
    }
    S.spikes.resize(jobs.size());
    for (std::size_t j = 0; j < jobs.size(); ++j) {
        ResidualSpike& out = S.spikes[j];
        out.sourceRow = jobs[j].c.row; out.time = jobs[j].c.time; out.partner = jobs[j].c.partner; out.shift = jobs[j].c.shift;
        ++S.perPartner[out.partner];
        if (out.shift != 0) ++S.moved;
    }

    // ── the records, in every method: the window at the aligned time less the own unit there ──
    for (const QString& m : methods) {
        FILE* fil = std::fopen(recording.toLocal8Bit().constData(), "rb");
        if (!fil) { S.reason = tr("Could not open %1.").arg(recording); S.spikes.clear(); return S; }
        for (std::size_t c0 = 0; c0 < jobs.size(); c0 += kChunk) {
            const std::size_t c1 = std::min(jobs.size(), c0 + kChunk);
            std::vector<std::vector<qint64>> times(c1 - c0);
            for (std::size_t j = c0; j < c1; ++j) {
                times[j - c0].push_back(jobs[j].c.time);
                for (const int nb : jobs[j].neighbours) times[j - c0].push_back(timeOf(nb) + jobs[j].c.displacement);
            }
            WindowSet ws;
            int64_t outOfRange = 0;
            cutWindows(fil, cutOf[m].window, rec, times, ws, &outOfRange);
            for (std::size_t j = c0; j < c1; ++j) {
                std::vector<int16_t>& record = S.spikes[j].records[m];
                record.resize(rec);
                targetLessMedian(ws, j - c0, rec, record.data());
            }
        }
        std::fclose(fil);
    }

    // ── a feature row each, in EVERY space: the projection of the record of the space's waveforms ──
    std::vector<neurosuite::core::PcaBasis> bases;
    S.spaces = residualSpacesOf(*this, methods, nChan, nSamp, bases, S.spacesSkipped);
    if (S.spaces.isEmpty() || S.spaces.front().method != cluMethod()) {
        S.reason = tr("The session's own feature space (%1) has no basis that fits its records: the new rows would have no features.%2")
                       .arg(cluMethod(), S.spacesSkipped.isEmpty() ? QString() : QStringLiteral("  ") + S.spacesSkipped.join(QStringLiteral("; ")));
        S.spikes.clear();
        return S;
    }
    {
        std::vector<qint64> sourceRows;
        for (const ResidualSpike& spike : S.spikes) sourceRows.push_back(spike.sourceRow);
        for (qsizetype q = 0; q < S.spaces.size(); ++q) {
            const ResidualSpace& space = S.spaces[q];
            const std::size_t nd = static_cast<std::size_t>(space.nDims);
            // The columns a projection does not give (any the space adds after the components)
            // are the source row's: they describe the same stretch of recording.
            std::vector<int64_t> rowsOfSource;
            if (!readFetRows(space.fetPath, sourceRows, space.nDims, nSpikes, rowsOfSource)) {
                S.reason = tr("Could not read the rows of %1.").arg(QFileInfo(space.fetPath).fileName());
                S.spikes.clear();
                return S;
            }
            for (std::size_t j = 0; j < S.spikes.size(); ++j) {
                ResidualSpike& spike = S.spikes[j];
                std::vector<qint64>& row = spike.features[space.method];
                row.assign(rowsOfSource.begin() + static_cast<std::ptrdiff_t>(j * nd), rowsOfSource.begin() + static_cast<std::ptrdiff_t>((j + 1) * nd));
                if (!space.detached) projectRecord(bases[static_cast<std::size_t>(q)], spike.records[space.spkMethod].data(), nChan, row.data());
                row[nd - 1] = spike.time;
            }
        }
    }
    S.ok = true;
    qInfo().noquote() << "[residual spikes]" << S.candidates << "residuals in the selection:" << S.spikes.size() << "spikes ready ("
                      << S.moved << "moved by the alignment);" << S.known << "known already," << S.unmatched << "without a partner cluster,"
                      << S.alreadyRow << "on a row once aligned," << S.repeated << "repeated," << S.withoutNeighbours << "without neighbours";
    return S;
}
