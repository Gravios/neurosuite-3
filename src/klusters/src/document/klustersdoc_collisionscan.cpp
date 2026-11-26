/***************************************************************************
 * klustersdoc_collisionscan.cpp -- collisions no sorter named
 *
 * Clusters > Mark Collisions.  The corrected collision rows
 * (klustersdoc_spkc.cpp) are made from what the .eap names; a sorter that did
 * not look for collisions names none, and its clusters then hold rows with a
 * second unit's spike in them as they were cut.
 *
 *   scanCollisions   searches the rows of the selected clusters (or children)
 *                    for another cluster's spike (collisionscan.h), sets the
 *                    threshold on the control and confirms each candidate with
 *                    decollide_fit::fitPartner.  Changes nothing.
 *   markCollisions   names the partner in the row's membership.  The marks are
 *                    held in memory (KlustersDoc::PendingMarks) and applied to
 *                    every read of the .eap, so the rows are corrected at once;
 *                    the file is written at Save, like the clustering.
 *
 * Templates.  A unit's template drifts over a session, so each cluster's rows
 * (those that name no partner) are cut into stretches of about kStretchRows and
 * a template is the mean of up to kTemplateSpikes rows spread over a stretch.
 * A row is searched with its own cluster's stretch at its position, and each
 * partner's stretch at the same position in time.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "klustersdoc.h"

#include "collisionscan.h"
#include "data.h"
#include "reservedclusters.h"

#include "neurosuite/core/decollide_fit.hpp"
#include "neurosuite/core/neurofileio.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QDebug>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr int    kTemplateSpikes  = 300;    ///< rows a stretch's template is the mean of, at most
constexpr int    kStretchRows     = 3000;   ///< rows of a cluster one template stands for, at most
constexpr int    kMinStretchRows  = 100;    ///< ... and at least, when the stretches are cut by time
constexpr int    kMaxStretches    = 64;
constexpr int    kMinPartnerRows  = 200;    ///< a cluster with fewer rows is not tried as a partner
constexpr int    kMinOwnRows      = 30;     ///< ... and with fewer than this has no template of its own
constexpr double kLookAlike       = 0.85;   ///< a partner this like the row's own unit is not tried
constexpr int    kLookAlikeReach  = 3;
constexpr double kMinGain         = 0.20;   ///< share of what the own template leaves a partner must remove
constexpr int    kBatch           = 2048;
constexpr int    kCellAbsent      = -128;   ///< a PendingCell that takes a class OUT of a row (EAP_ABSENT)

/** A cluster's templates along the session. */
struct UnitTemplates {
    int cluster = -1;
    QVector<int> rows;                                   ///< its rows naming no partner, ascending
    std::vector<qint64> times;                           ///< the times of ALL its rows, ascending
    std::vector<std::vector<float>> stretch;             ///< mean records
    std::vector<collisionscan::Candidate> candidate;     ///< the same, with their energy per lag
    int stretchAt(qint64 row) const {
        if (stretch.empty() || rows.isEmpty()) return 0;
        const qint64 pos = std::lower_bound(rows.begin(), rows.end(), static_cast<int>(row)) - rows.begin();
        return static_cast<int>(std::min<qint64>(static_cast<qint64>(stretch.size()) - 1,
                                                 pos * static_cast<qint64>(stretch.size()) / rows.size()));
    }
};

} // namespace

KlustersDoc::CollisionScan KlustersDoc::scanCollisions(const QList<int>& clusters, bool children,
                                                       const CollisionScanOptions& options)
{
    double falseRate = options.falseRate;
    namespace cs = collisionscan;
    namespace df = neurosuite::decollide_fit;
    namespace nf = neurofileio;
    CollisionScan R;
    QElapsedTimer timer; timer.start();
    if (!clusteringData) { R.reason = tr("No session is open."); return R; }
    Data* layer = children ? childData : clusteringData;
    if (!layer) { R.reason = tr("The session has no child clustering."); return R; }
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const int nSamp = clusteringData->nbSamplesPerWaveform();
    const int nChan = clusteringData->nbOfChannels();
    if (clusteringData->supersampleFactor() != 1) {
        R.reason = tr("A super-sampled waveform space is shown: collisions are searched in the session's own.");
        return R;
    }
    if (nSamp < 8 || nChan < 1 || nSpikes < 1) { R.reason = tr("The document carries no waveforms."); return R; }
    const int peak = std::clamp(clusteringData->getPeakPositionInWaveform() - 1, 0, nSamp - 1);
    const cs::Params params;
    int firstLag = 0, lastLag = 0;
    cs::lagRange(nSamp, peak, params.edge, firstLag, lastLag);

    // ── the cluster of every row, and the rows that name a partner already ──
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();
    std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
    QHash<int, QVector<int>> rowsOfCluster;
    for (const dataType id : clusteringData->clusterIds()) {
        QVector<int> rows = clusteringData->clusterSpkIndices(static_cast<int>(id));
        std::sort(rows.begin(), rows.end());
        for (const int r : rows)
            if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
        rowsOfCluster.insert(static_cast<int>(id), rows);
    }
    std::vector<char> named(static_cast<std::size_t>(nSpikes), 0);
    {
        const nf::EapFile eap = currentEap(stageEapPath(), nSpikes);
        if (eap.ok && eap.nSpikes == nSpikes && eap.nClasses > 0)
            for (qint64 i = 0; i < nSpikes; ++i) {
                const int8_t* row = &eap.cells[static_cast<std::size_t>(i) * eap.nClasses];
                int n = 0;
                for (int j = 0; j < eap.nClasses && n < 2; ++j) if (nf::eapPresent(row[j])) ++n;
                if (n >= 2) named[static_cast<std::size_t>(i)] = 1;
            }
    }

    // ── the rows to search, by the selected cluster (or child) they are in ──
    struct Job { qint64 row; int group; };
    std::vector<Job> jobs;
    for (const int id : clusters) {
        if (id <= 1 || (!children && isReservedClusterId(static_cast<dataType>(id)))) continue;
        qint64 n = 0;
        for (const int r : layer->clusterSpkIndices(id)) {
            if (r < 0 || r >= nSpikes || clu[static_cast<std::size_t>(r)] <= 1) continue;
            if (named[static_cast<std::size_t>(r)]) { ++R.alreadyNamed; continue; }
            jobs.push_back(Job{r, id});
            ++n;
        }
        R.rowsOf.insert(id, n);
    }
    if (jobs.empty()) { R.reason = tr("The selection holds no row to search."); return R; }
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.row < b.row; });

    // ── when each row is, and each cluster's spikes ──
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    std::vector<qint64> timeOf(static_cast<std::size_t>(nSpikes));
    for (qint64 i = 0; i < nSpikes; ++i)
        timeOf[static_cast<std::size_t>(i)] = static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim));
    const double rate = clusteringData->getSamplingRate() > 0.0 ? clusteringData->getSamplingRate() : 1.0;
    const qint64 presence = options.presenceSeconds > 0.0 ? static_cast<qint64>(options.presenceSeconds * rate) : 0;

    // ── templates ──
    const qint64 recBytes = static_cast<qint64>(nSamp) * nChan * static_cast<qint64>(sizeof(int16_t));
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    std::vector<int16_t> buf(rec);
    auto build = [&](int cluster, int minRows) {
        UnitTemplates u;
        u.cluster = cluster;
        for (const int r : rowsOfCluster.value(cluster))
            if (!named[static_cast<std::size_t>(r)]) u.rows.append(r);
        if (u.rows.size() < minRows) return u;
        for (const int r : rowsOfCluster.value(cluster)) u.times.push_back(timeOf[static_cast<std::size_t>(r)]);
        std::sort(u.times.begin(), u.times.end());
        const double span = u.times.empty() ? 0.0 : static_cast<double>(u.times.back() - u.times.front()) / rate;
        const int k = cs::stretchCount(u.rows.size(), span, kStretchRows, options.stretchSeconds, kMinStretchRows, kMaxStretches);
        R.stretchesOf.insert(cluster, k);
        u.stretch.reserve(static_cast<std::size_t>(k));
        for (int s = 0; s < k; ++s) {
            const qint64 lo = static_cast<qint64>(u.rows.size()) * s / k, hi = static_cast<qint64>(u.rows.size()) * (s + 1) / k;
            const qint64 step = std::max<qint64>(1, (hi - lo) / kTemplateSpikes);
            std::vector<float> t(rec, 0.0f);
            int used = 0;
            for (qint64 q = lo; q < hi; q += step) {
                if (!snap->readSpkRaw(buf.data(), recBytes, static_cast<qint64>(u.rows[static_cast<int>(q)]) * recBytes)) continue;
                for (std::size_t i = 0; i < rec; ++i) t[i] += buf[i];
                ++used;
            }
            if (used > 0) for (float& v : t) v /= static_cast<float>(used);
            u.stretch.push_back(std::move(t));
        }
        for (const std::vector<float>& t : u.stretch)
            u.candidate.push_back(cs::candidate(cluster, t.data(), nSamp, nChan, firstLag, lastLag));
        return u;
    };
    QHash<int, int> unitIndex;                            // cluster -> index into units
    std::vector<UnitTemplates> units;
    for (auto it = rowsOfCluster.constBegin(); it != rowsOfCluster.constEnd(); ++it) {
        const int c = it.key();
        if (c <= 1 || isReservedClusterId(static_cast<dataType>(c))) continue;
        UnitTemplates u = build(c, kMinOwnRows);
        if (u.stretch.empty()) continue;
        unitIndex.insert(c, static_cast<int>(units.size()));
        units.push_back(std::move(u));
    }
    std::vector<char> isPartner(units.size(), 0);
    for (std::size_t k = 0; k < units.size(); ++k)
        if (units[k].rows.size() >= kMinPartnerRows) { isPartner[k] = 1; ++R.partnerUnits; }

    // Per own cluster: the partners not tried because they are too like it (a look-alike takes
    // the row's own spike for itself wherever the own template fits a little off).
    QHash<int, std::vector<char>> skipOf;
    auto skipFor = [&](int cluster) -> const std::vector<char>& {
        auto it = skipOf.find(cluster);
        if (it != skipOf.end()) return it.value();
        std::vector<char> skip(units.size(), 1);
        const int own = unitIndex.value(cluster, -1);
        if (own >= 0) {
            // At any time of the session: a cluster that is this one drifted (the same cell, cut
            // in two by a sort) is like it where the two meet, not in the middle of each.
            const UnitTemplates& mine = units[static_cast<std::size_t>(own)];
            QList<int> alike;
            for (std::size_t k = 0; k < units.size(); ++k) {
                if (static_cast<int>(k) == own || !isPartner[k]) continue;
                double most = -1.0;
                for (const std::vector<float>& a : mine.stretch)
                    for (const std::vector<float>& b : units[k].stretch)
                        most = std::max(most, cs::likeness(a.data(), b.data(), nSamp, nChan, kLookAlikeReach));
                if (most >= kLookAlike) alike.append(units[k].cluster);
                else skip[k] = 0;
            }
            std::sort(alike.begin(), alike.end());
            if (!alike.isEmpty()) R.lookAlikes.insert(cluster, alike);
        } else if (!R.withoutTemplate.contains(cluster)) {
            R.withoutTemplate.append(cluster);
        }
        return skipOf.insert(cluster, skip).value();
    };
    for (const Job& j : jobs) skipFor(clu[static_cast<std::size_t>(j.row)]);   // filled before the threads read it

    // ── the search ──
    struct Out { float gain = 0.0f, control = 0.0f; int unit = -1; int stretch = 0; int lag = 0; int absent = 0; bool searched = false; };
    std::vector<Out> out(jobs.size());
    std::vector<int16_t> block(static_cast<std::size_t>(kBatch) * rec);
    for (std::size_t b0 = 0; b0 < jobs.size(); b0 += kBatch) {
        const int nb = static_cast<int>(std::min<std::size_t>(kBatch, jobs.size() - b0));
        std::vector<char> readOk(static_cast<std::size_t>(nb), 0);
        for (int i = 0; i < nb; ++i)
            readOk[static_cast<std::size_t>(i)] =
                snap->readSpkRaw(&block[static_cast<std::size_t>(i) * rec], recBytes, jobs[b0 + static_cast<std::size_t>(i)].row * recBytes) ? 1 : 0;
        #pragma omp parallel
        {
            std::vector<float> w(rec), r;
            #pragma omp for schedule(dynamic, 32)
            for (int i = 0; i < nb; ++i) {
                if (!readOk[static_cast<std::size_t>(i)]) continue;
                const qint64 row = jobs[b0 + static_cast<std::size_t>(i)].row;
                const int cluster = clu[static_cast<std::size_t>(row)];
                const int own = unitIndex.value(cluster, -1);
                if (own < 0) continue;
                const std::vector<char>& skip = *skipOf.constFind(cluster);
                const int16_t* src = &block[static_cast<std::size_t>(i) * rec];
                for (std::size_t q = 0; q < rec; ++q) w[q] = static_cast<float>(src[q]);
                const UnitTemplates& mine = units[static_cast<std::size_t>(own)];
                cs::ownResidual(w.data(), mine.stretch[static_cast<std::size_t>(mine.stretchAt(row))].data(), nSamp, nChan, params, r);
                cs::Found f;
                int bestStretch = 0, absent = 0;
                const qint64 when = timeOf[static_cast<std::size_t>(row)];
                for (std::size_t k = 0; k < units.size(); ++k) {
                    if (skip[k]) continue;
                    // A cluster with no spike of its own near this time is not there to collide
                    // with: its template from another hour would only be fitted to noise.
                    if (presence > 0 && !cs::presentNear(units[k].times, when, presence)) { ++absent; continue; }
                    const int s = units[k].stretchAt(row);
                    const int before = f.candidate;
                    const double gainBefore = f.gain;
                    cs::tryCandidate(r.data(), units[k].candidate[static_cast<std::size_t>(s)], static_cast<int>(k),
                                     nSamp, nChan, firstLag, lastLag, params, f);
                    if (f.candidate != before || f.gain != gainBefore) bestStretch = s;
                }
                Out& o = out[b0 + static_cast<std::size_t>(i)];
                o.searched = true;
                o.gain = static_cast<float>(f.gain); o.control = static_cast<float>(f.control);
                o.unit = f.candidate; o.stretch = bestStretch; o.lag = f.lag; o.absent = absent;
            }
        }
    }

    // ── what chance gives ──
    // Two units firing independently share a window now and then.  For a row at time t and a
    // partner firing at rate r around t, the chance that the partner's peak falls at one of the
    // lags searched is r x (the lags' duration); summed over the rows searched, that is the
    // number of collisions with this partner that need no explaining.  r is the partner's own
    // spikes within the presence window of t (the whole session with the window off).  A
    // partner named far more often than this is more likely a misfit than a partner.
    {
        const double lagSeconds = static_cast<double>(lastLag - firstLag + 1) / rate;
        std::vector<double> expected(units.size(), 0.0);
        // The window is cut at the ends of the session: there is no recording beyond them.
        const auto ends = std::minmax_element(timeOf.begin(), timeOf.end());
        const qint64 sessionStart = *ends.first, sessionEnd = *ends.second;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!out[i].searched) continue;
            const qint64 row = jobs[i].row;
            const std::vector<char>& skip = *skipOf.constFind(clu[static_cast<std::size_t>(row)]);
            const qint64 when = timeOf[static_cast<std::size_t>(row)];
            for (std::size_t k = 0; k < units.size(); ++k) {
                if (skip[k]) continue;
                const std::vector<qint64>& times = units[k].times;
                if (times.empty()) continue;
                double perSecond = 0.0;
                if (presence > 0) {
                    const auto lo = std::lower_bound(times.begin(), times.end(), when - presence);
                    const auto hi = std::upper_bound(lo, times.end(), when + presence);
                    const qint64 from = std::max(when - presence, sessionStart), to = std::min(when + presence, sessionEnd);
                    perSecond = to > from ? static_cast<double>(hi - lo) / (static_cast<double>(to - from) / rate) : 0.0;
                } else {
                    const double span = static_cast<double>(times.back() - times.front()) / rate;
                    perSecond = span > 0.0 ? static_cast<double>(times.size()) / span : 0.0;
                }
                expected[k] += perSecond * lagSeconds;
            }
        }
        for (std::size_t k = 0; k < units.size(); ++k)
            if (expected[k] > 0.0) R.expectedOf.insert(units[k].cluster, expected[k]);
    }

    // ── the threshold: what the control reaches in falseRate of the rows ──
    std::vector<float> control;
    control.reserve(out.size());
    for (const Out& o : out) if (o.searched) { control.push_back(o.control); R.notPresent += o.absent; }
    R.scanned = static_cast<qint64>(control.size());
    if (control.empty()) { R.reason = tr("None of the selected clusters has a template of its own (too few rows)."); return R; }
    falseRate = std::clamp(falseRate, 0.0001, 0.5);
    const std::size_t at = std::min(control.size() - 1,
                                    static_cast<std::size_t>(std::floor((1.0 - falseRate) * static_cast<double>(control.size()))));
    std::nth_element(control.begin(), control.begin() + static_cast<std::ptrdiff_t>(at), control.end());
    R.threshold = control[at];

    // ── confirm each candidate with the fit the corrected rows are made with ──
    df::Params fit;
    fit.radius = 2; fit.ampMin = params.ampMin; fit.ampMax = params.ampMax; fit.minGain = kMinGain;
    std::vector<float> w(rec);
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        const Out& o = out[i];
        if (!o.searched) continue;
        if (o.control > R.threshold) { ++R.controlOver; ++R.controlOf[jobs[i].group]; }
        if (o.unit < 0 || !(o.gain > R.threshold) || !(o.gain > o.control)) continue;
        ++R.candidates;
        const qint64 row = jobs[i].row;
        const int cluster = clu[static_cast<std::size_t>(row)];
        const UnitTemplates& mine = units[static_cast<std::size_t>(unitIndex.value(cluster))];
        const UnitTemplates& theirs = units[static_cast<std::size_t>(o.unit)];
        if (!snap->readSpkRaw(buf.data(), recBytes, row * recBytes)) continue;
        for (std::size_t q = 0; q < rec; ++q) w[q] = static_cast<float>(buf[q]);
        const df::Fit f = df::fitPartner(w.data(), mine.stretch[static_cast<std::size_t>(mine.stretchAt(row))].data(),
                                         theirs.stretch[static_cast<std::size_t>(o.stretch)].data(), nSamp, nChan, o.lag, fit);
        if (!f.accepted) continue;
        R.hits.push_back(CollisionHit{row, cluster, theirs.cluster, f.tau, f.amp, f.residBefore - f.residAfter});
        ++R.hitsOf[jobs[i].group];
    }
    R.ok = true;
    R.milliseconds = timer.elapsed();
    qInfo().noquote() << "[mark collisions]" << R.scanned << "rows searched against" << R.partnerUnits << "clusters,"
                      << R.candidates << "over the threshold (the control:" << R.controlOver << "),"
                      << R.hits.size() << "confirmed;" << R.notPresent << "row-cluster pairs not tried, the cluster"
                      << "having no spike within" << options.presenceSeconds << "s;" << R.milliseconds << "ms";
    return R;
}

KlustersDoc::CollisionMarks KlustersDoc::markCollisions(const std::vector<CollisionHit>& hits)
{
    namespace nf = neurofileio;
    CollisionMarks M;
    if (!clusteringData) { M.error = tr("No session is open."); return M; }
    if (hits.empty()) { M.ok = true; return M; }
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const QString path = stageEapPath();
    if (path.isEmpty()) { M.error = tr("The session's name does not parse: there is no place for its .eap."); return M; }
    nf::EapFile eap = currentEap(path, nSpikes);
    if (!eap.ok) {                                        // no membership yet: a table that names nobody
        eap = nf::EapFile{};
        eap.nSpikes = nSpikes; eap.nClasses = 128;
        eap.cells.assign(static_cast<std::size_t>(nSpikes) * 128, nf::EAP_ABSENT);
        eap.ok = true;
    }
    if (eap.nSpikes != nSpikes) {
        M.error = tr("%1 holds %2 rows, the session %3 spikes.").arg(QFileInfo(path).fileName()).arg(eap.nSpikes).arg(nSpikes);
        return M;
    }
    const QString tclPath = groupTclPath();
    const nf::TclRegistry tcl = nf::readTcl(tclPath.toStdString());
    if (!tcl.ok && QFileInfo::exists(tclPath)) {
        M.error = tr("%1 is not a valid .tcl registry.").arg(QFileInfo(tclPath).fileName());
        return M;
    }

    // ── the cluster of every row, each cluster's rows, each class's cluster ──
    std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
    QHash<int, QVector<int>> rowsOfCluster;
    for (const dataType id : clusteringData->clusterIds()) {
        QVector<int> rows = clusteringData->clusterSpkIndices(static_cast<int>(id));
        std::sort(rows.begin(), rows.end());
        for (const int r : rows)
            if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
        rowsOfCluster.insert(static_cast<int>(id), rows);
    }
    std::vector<int> home = collisionClassHomes(eap, clu);
    auto classesIn = [&](qint64 row, int& only) {         // how many classes the row names, and one of them
        const int8_t* cells = &eap.cells[static_cast<std::size_t>(row) * eap.nClasses];
        int n = 0;
        for (int j = 0; j < eap.nClasses; ++j) if (nf::eapPresent(cells[j])) { only = j; ++n; }
        return n;
    };

    // ── the times the stored offsets hold for ──
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    auto timeNow = [&](qint64 row) { return static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim)); };
    if (collisionRefEap != path || static_cast<qint64>(collisionRefTimes.size()) != nSpikes) {
        collisionRefTimes.resize(static_cast<std::size_t>(nSpikes));
        for (qint64 i = 0; i < nSpikes; ++i) collisionRefTimes[static_cast<std::size_t>(i)] = timeNow(i);
        collisionRefEap = path;
    }
    auto moved = [&](qint64 row) { return timeNow(row) - collisionRefTimes[static_cast<std::size_t>(row)]; };
    QHash<int, qint64> unitMoved;                         // per cluster: the median time change of its rows
    auto movedUnit = [&](int cluster) -> qint64 {
        auto it = unitMoved.find(cluster);
        if (it != unitMoved.end()) return it.value();
        const QVector<int>& rows = rowsOfCluster[cluster];
        qint64 med = 0;
        if (!rows.isEmpty()) {
            std::vector<qint64> d;
            const int step = std::max<int>(1, static_cast<int>(rows.size()) / 301);
            for (int k = 0; k < rows.size(); k += step) d.push_back(moved(rows[k]));
            std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
            med = d[d.size() / 2];
        }
        return unitMoved.insert(cluster, med).value();
    };

    // ── a class for a cluster: the one that is it now, else a new one ──
    std::vector<char> columnUsed(static_cast<std::size_t>(eap.nClasses), 0);
    for (qint64 i = 0; i < nSpikes; ++i) {
        const int8_t* cells = &eap.cells[static_cast<std::size_t>(i) * eap.nClasses];
        for (int j = 0; j < eap.nClasses; ++j) if (nf::eapPresent(cells[j])) columnUsed[static_cast<std::size_t>(j)] = 1;
    }
    for (const PendingClass& c : pendingMarks.classes)
        if (c.col >= 0 && c.col < static_cast<int>(columnUsed.size())) columnUsed[static_cast<std::size_t>(c.col)] = 1;
    auto freeColumn = [&]() {
        for (int j = 0; j < eap.nClasses; ++j) {
            if (columnUsed[static_cast<std::size_t>(j)]) continue;
            if (tcl.ok && j < static_cast<int>(tcl.entries.size())
                && tcl.entries[static_cast<std::size_t>(j)].status != nf::TclStatus::Free) continue;
            return j;
        }
        const int j = std::max(eap.nClasses, tcl.ok ? tcl.nClasses : 0);
        eap = nf::growEap(eap, j + 64);
        columnUsed.resize(static_cast<std::size_t>(eap.nClasses), 0);
        home.resize(static_cast<std::size_t>(eap.nClasses), -1);
        return j;
    };
    PendingMarks add;
    QHash<int, int> classOfCluster;
    auto classFor = [&](int cluster) -> int {
        auto it = classOfCluster.find(cluster);
        if (it != classOfCluster.end()) return it.value();
        // Of the classes whose home it is, the one most of its rows name alone.
        QHash<int, qint64> votes;
        QVector<int> classless;
        for (const int r : rowsOfCluster.value(cluster)) {
            int only = -1;
            const int n = classesIn(r, only);
            if (n == 0) classless.append(r);
            else if (n == 1 && only < static_cast<int>(home.size()) && home[static_cast<std::size_t>(only)] == cluster) ++votes[only];
        }
        int best = -1;
        for (auto v = votes.constBegin(); v != votes.constEnd(); ++v)
            if (best < 0 || v.value() > votes.value(best) || (v.value() == votes.value(best) && v.key() < best)) best = v.key();
        if (best < 0)
            for (std::size_t j = 0; j < home.size(); ++j)
                if (home[j] == cluster) { best = static_cast<int>(j); break; }
        if (best < 0 && classless.size() >= kMinOwnRows) {
            // A new class, named by every row of the cluster that names none: the membership
            // then says which cluster it is, whatever the clusters are renumbered to.
            best = freeColumn();
            columnUsed[static_cast<std::size_t>(best)] = 1;
            home[static_cast<std::size_t>(best)] = cluster;
            add.classes.push_back(PendingClass{best, cluster});
            for (const int r : classless) {
                eap.cells[static_cast<std::size_t>(r) * eap.nClasses + best] = 0;
                add.cells.push_back(PendingCell{r, best, 0});
            }
            ++M.classesCreated;
        }
        return classOfCluster.insert(cluster, best).value();
    };

    // ── the marks ──
    for (const CollisionHit& h : hits) {
        if (h.row < 0 || h.row >= nSpikes || clu[static_cast<std::size_t>(h.row)] != h.cluster || h.partner <= 1
            || !rowsOfCluster.contains(h.partner) || h.partner == h.cluster) continue;
        const int own = classFor(h.cluster), partner = classFor(h.partner);
        if (own < 0 || partner < 0 || own == partner) { ++M.withoutClass; continue; }
        // The row as it is: it may name its own cluster's class (any of them, after a merge),
        // or none -- or ONE class of another cluster.  That row was put in this cluster after
        // its class was written (a sorter names every spike's class; nothing renames it when
        // the spike is moved), and the .clu is the authority on whose spike it is: the old
        // class is taken out and the row gets its cluster's.  The scan leaves out rows naming
        // two classes or more, so a hit never names more than one.
        int8_t* cells = &eap.cells[static_cast<std::size_t>(h.row) * eap.nClasses];
        bool ownNamed = false;
        int strays = 0;
        for (int j = 0; j < eap.nClasses; ++j) {
            if (!nf::eapPresent(cells[j])) continue;
            if (home[static_cast<std::size_t>(j)] == h.cluster && j != partner) { ownNamed = true; continue; }
            ++strays;
            if (j == partner) continue;                       // it gets the partner's offset below
            cells[j] = nf::EAP_ABSENT;
            add.cells.push_back(PendingCell{h.row, j, kCellAbsent});
        }
        if (strays > 0) ++M.strayRows;
        if (!ownNamed) {
            eap.cells[static_cast<std::size_t>(h.row) * eap.nClasses + own] = 0;
            add.cells.push_back(PendingCell{h.row, own, 0});
        }
        // Stored for the times the table holds for: a rebuild looks for the partner at
        //     stored - (the row's time change) + (the partner unit's).
        const int stored = static_cast<int>(std::clamp<qint64>(h.tau + moved(h.row) - movedUnit(h.partner), -127, 127));
        eap.cells[static_cast<std::size_t>(h.row) * eap.nClasses + partner] = static_cast<int8_t>(stored);
        add.cells.push_back(PendingCell{h.row, partner, stored});
        add.rows.push_back(h.row);
        ++M.marked;
    }
    if (M.marked == 0) { M.ok = true; return M; }         // nothing to hold: a class made for nothing is dropped
    pendingMarks.cells.insert(pendingMarks.cells.end(), add.cells.begin(), add.cells.end());
    pendingMarks.classes.insert(pendingMarks.classes.end(), add.classes.begin(), add.classes.end());
    pendingMarks.rows.insert(pendingMarks.rows.end(), add.rows.begin(), add.rows.end());
    setModified(true);
    refreshCollisionRows();
    qInfo().noquote() << "[mark collisions]" << M.marked << "rows marked," << M.classesCreated << "classes made;"
                      << M.strayRows << "of the rows named another cluster's class and were given their own;"
                      << M.withoutClass << "hits without a class left as they are";
    M.ok = true;
    return M;
}

std::vector<qint64> KlustersDoc::collisionRowsOf(const QList<int>& clusters, bool children) const
{
    namespace nf = neurofileio;
    std::vector<qint64> out;
    Data* layer = children ? childData : clusteringData;
    if (!clusteringData || !layer) return out;
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const nf::EapFile eap = currentEap(stageEapPath(), nSpikes);
    if (!eap.ok || eap.nSpikes != nSpikes || eap.nClasses <= 0) return out;
    for (const int id : clusters)
        for (const int r : layer->clusterSpkIndices(id)) {
            if (r < 0 || r >= nSpikes) continue;
            const int8_t* cells = &eap.cells[static_cast<std::size_t>(r) * eap.nClasses];
            int n = 0;
            for (int j = 0; j < eap.nClasses && n < 2; ++j) if (nf::eapPresent(cells[j])) ++n;
            if (n >= 2) out.push_back(r);
        }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int KlustersDoc::removeCollisionMarks(const std::vector<qint64>& rows)
{
    namespace nf = neurofileio;
    namespace df = neurosuite::decollide_fit;
    if (!clusteringData || rows.empty()) return 0;
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const QString path = stageEapPath();
    const nf::EapFile eap = currentEap(path, nSpikes);
    if (!eap.ok || eap.nSpikes != nSpikes) return 0;
    std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
    for (const dataType id : clusteringData->clusterIds())
        for (const int r : clusteringData->clusterSpkIndices(static_cast<int>(id)))
            if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
    const std::vector<int> home = collisionClassHomes(eap, clu);
    // The times the table holds for are kept as they are: nothing is stored here but absences.
    if (collisionRefEap != path || static_cast<qint64>(collisionRefTimes.size()) != nSpikes) {
        const int timeDim = clusteringData->nbOfDimensionsTotal();
        collisionRefTimes.resize(static_cast<std::size_t>(nSpikes));
        for (qint64 i = 0; i < nSpikes; ++i)
            collisionRefTimes[static_cast<std::size_t>(i)] =
                static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(i + 1), timeDim));
        collisionRefEap = path;
    }
    int changed = 0;
    df::Member own; std::vector<df::Member> partners;
    for (const qint64 row : rows) {
        if (row < 0 || row >= nSpikes) continue;
        // The same split the corrected rows are made with: what is "own" there stays.
        if (!df::splitRow(eap, row, clu[static_cast<std::size_t>(row)], home, own, partners)) continue;
        for (const df::Member& p : partners) pendingMarks.cells.push_back(PendingCell{row, p.classId, kCellAbsent});
        ++changed;
    }
    if (changed == 0) return 0;
    setModified(true);
    refreshCollisionRows();
    qInfo().noquote() << "[mark collisions]" << changed << "rows no longer name a partner";
    return changed;
}
