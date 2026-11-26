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

double medianOf(std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

} // namespace

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

    // ── which records are still what they were made for ──
    std::vector<Want> todo;
    for (const Want& w : wants) {
        const auto it = residuals.constFind(w.row);
        if (it != residuals.constEnd() && it->bytes && it->time == timeOf(w.row)
            && it->cluster == clu[static_cast<std::size_t>(w.row)] && it->madeFor == w.tau && it->classId == w.classId)
            ++R.kept;
        else
            todo.push_back(w);
    }

    // ── the recording, when it can be cut in the .spk's own domain ──
    const QString spkMethod = featureMethod(clusteringData->getSpkFileName());
    const auto token = neurosuite::custody::parseMethodToken(spkMethod.toStdString());
    const bool cuttable = methodIsStderiv(spkMethod)
                          || (token.family == "standard" && token.kind == '\0' && token.lag == 0);
    const QString recording = rawRecordingPath();
    FILE* fil = (cuttable && !todo.empty() && QFileInfo::exists(recording))
                    ? std::fopen(recording.toLocal8Bit().constData(), "rb") : nullptr;
    SpikeCutSpec cut;
    QString cutLog;
    if (fil) {
        QTextStream log(&cutLog);
        spikeCutSpecFor(spkMethod, nullptr, cut, log);
        if (cut.window.nSamp != nSamp || cut.window.nChan != nChan) { std::fclose(fil); fil = nullptr; }
    }
    R.fromRecording = fil != nullptr;
    R.source = fil ? QFileInfo(recording).fileName() : QFileInfo(clusteringData->getSpkFileName()).fileName();

    // ── the records ──
    std::vector<int16_t> own(rec), other(rec);
    std::vector<float> ownF(rec);
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
            // Near in time: the kPool clean spikes of the cluster around the row.
            const int pos = static_cast<int>(std::lower_bound(clean.begin(), clean.end(), static_cast<int>(w.row)) - clean.begin());
            int lo = std::max(0, pos - kPool / 2);
            const int hi = std::min(static_cast<int>(clean.size()), lo + kPool);
            lo = std::max(0, hi - kPool);
            // Near in waveform: against the row's own part, its corrected record.
            if (!snap->readSpk(own.data(), recBytes, w.row * recBytes)) { residuals.remove(w.row); continue; }
            for (std::size_t i = 0; i < rec; ++i) ownF[i] = static_cast<float>(own[i]);
            std::vector<std::pair<double, int>> byDistance;
            byDistance.reserve(static_cast<std::size_t>(hi - lo));
            for (int k = lo; k < hi; ++k) {
                if (!snap->readSpk(other.data(), recBytes, static_cast<qint64>(clean[k]) * recBytes)) continue;
                double d = 0.0;
                for (std::size_t i = 0; i < rec; ++i) { const double x = ownF[i] - static_cast<float>(other[i]); d += x * x; }
                byDistance.emplace_back(d, clean[k]);
            }
            if (static_cast<int>(byDistance.size()) < kMinNeighbours) { ++R.withoutNeighbours; residuals.remove(w.row); continue; }
            const std::size_t keep = std::min<std::size_t>(byDistance.size(), std::max<std::size_t>(kMinNeighbours, std::min<std::size_t>(kNearest, byDistance.size() / 4)));
            std::partial_sort(byDistance.begin(), byDistance.begin() + static_cast<std::ptrdiff_t>(keep), byDistance.end());
            Job j;
            j.want = w; j.time = timeOf(w.row);
            for (std::size_t k = 0; k < keep; ++k) j.neighbours.push_back(byDistance[k].second);
            jobs.push_back(std::move(j));
        }
        if (jobs.empty()) continue;

        // The windows: the row's and each neighbour's, at the partner's displacement.  From
        // the recording they are cut in one pass, in time order (the extractor's cursor only
        // moves forward); without it they are the .spk's own records, at no displacement.
        std::vector<std::size_t> first(jobs.size() + 1, 0);
        for (std::size_t j = 0; j < jobs.size(); ++j) first[j + 1] = first[j] + 1 + jobs[j].neighbours.size();
        std::vector<int16_t> windows(first.back() * rec, 0);
        if (fil) {
            struct Request { qint64 time; std::size_t slot; };
            std::vector<Request> requests;
            requests.reserve(first.back());
            for (std::size_t j = 0; j < jobs.size(); ++j) {
                requests.push_back(Request{jobs[j].time + jobs[j].want.tau, first[j]});
                for (std::size_t k = 0; k < jobs[j].neighbours.size(); ++k)
                    requests.push_back(Request{timeOf(jobs[j].neighbours[k]) + jobs[j].want.tau, first[j] + 1 + k});
            }
            std::sort(requests.begin(), requests.end(), [](const Request& a, const Request& b) { return a.time < b.time; });
            int64_t outOfRange = 0;
            neurosuite::core::extractSpikeWindows(
                fil, static_cast<int64_t>(requests.size()), cut.window,
                [&](int64_t i) { return static_cast<int64_t>(requests[static_cast<std::size_t>(i)].time); },
                [&](int64_t i, const int16_t* row) {
                    std::copy(row, row + rec, windows.begin() + static_cast<std::ptrdiff_t>(requests[static_cast<std::size_t>(i)].slot * rec));
                },
                &outOfRange);
            R.outOfRecording += outOfRange;
        } else {
            for (std::size_t j = 0; j < jobs.size(); ++j) {
                snap->readSpkRaw(&windows[first[j] * rec], recBytes, jobs[j].want.row * recBytes);
                for (std::size_t k = 0; k < jobs[j].neighbours.size(); ++k)
                    snap->readSpk(&windows[(first[j] + 1 + k) * rec], recBytes, static_cast<qint64>(jobs[j].neighbours[k]) * recBytes);
            }
        }

        std::vector<int16_t> column;
        std::vector<int16_t> out(rec);
        for (std::size_t j = 0; j < jobs.size(); ++j) {
            const std::size_t n = jobs[j].neighbours.size();
            column.resize(n);
            const int16_t* target = &windows[first[j] * rec];
            for (std::size_t i = 0; i < rec; ++i) {
                for (std::size_t k = 0; k < n; ++k) column[k] = windows[(first[j] + 1 + k) * rec + i];
                std::nth_element(column.begin(), column.begin() + static_cast<std::ptrdiff_t>(n / 2), column.end());
                double med = column[n / 2];
                if (n % 2 == 0) {                              // the mean of the two middle values
                    const int16_t below = *std::max_element(column.begin(), column.begin() + static_cast<std::ptrdiff_t>(n / 2));
                    med = 0.5 * (med + below);
                }
                out[i] = static_cast<int16_t>(std::clamp<long>(std::lround(target[i] - med), -32768, 32767));
            }
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
