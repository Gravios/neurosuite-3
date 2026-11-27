/***************************************************************************
 * klustersdoc_collisionspaces.cpp -- is a collision there in every waveform space?
 *
 * A group's waveforms exist in several domains (the standard .spk, a stderiv
 * .spk).  The stderiv transform is linear: a partner's spike found in a row of
 * the standard .spk is, transformed, in the same row of the stderiv .spk, at
 * the same lag and amplitude.  A "partner" that fits in one domain and removes
 * nothing in the other is no spike -- what the fit explained there (a slow or
 * common-mode deflection, which the temporal derivative and the spatial
 * difference take out) is not that unit's waveform.
 *
 * On jg05-20120312 group 8, 19 % of the rows Mark Collisions had marked with
 * the standard space shown were of that kind, most of them naming a few small
 * clusters (claude/residual-spikes-as-clusters-2026-11-27.md).
 *
 * confirmCollisionsInSpaces() fits each collision in every waveform space of
 * the group, with the fit and the bounds the corrected rows are made with
 * (decollide_fit::fitPartner, its default Params), templates the mean of the
 * cluster's 300 clean rows nearest the row, read from that space's .spk.
 * Mark Collisions keeps only the hits confirmed in every space;
 * unconfirmedCollisionRows() finds the marks already made that are not.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/

#include "klustersdoc.h"
#include "klustersdoc_internal.h"
#include "data.h"

#include "neurosuite/core/custody.hpp"
#include "neurosuite/core/decollide_fit.hpp"
#include "neurosuite/core/neurofileio.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

#include <algorithm>
#include <memory>
#include <numeric>
#include <vector>

using klustersdoc_internal::featureMethod;

namespace {
constexpr int kTemplateSpikes = 300;    ///< clean rows a template is the mean of (as the corrected rows')
constexpr int kMinTemplateRows = 30;    ///< a cluster with fewer clean rows has no template
}

KlustersDoc::CollisionSpaceReport KlustersDoc::confirmCollisionsInSpaces(const std::vector<CollisionCheck>& checks, bool withShown) const
{
    namespace nf = neurofileio;
    namespace df = neurosuite::decollide_fit;
    CollisionSpaceReport R;
    QElapsedTimer timer; timer.start();
    R.confirmed.assign(checks.size(), 1);
    if (!clusteringData || checks.empty()) return R;
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const int nSamp = clusteringData->baseNbSamplesPerWaveform();
    const int nChan = clusteringData->nbOfChannels();
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    const qint64 recBytes = static_cast<qint64>(rec * sizeof(int16_t));

    // ── the waveform spaces: every .spk of this group's rows, once ──
    struct Space { QString method, path; };
    std::vector<Space> spaces;
    for (const FeatureSpace& fs : availableFeatureSpaces()) {
        if (!fs.usable || fs.spkPath.isEmpty()) continue;
        const QString method = fs.spkMethod.isEmpty() ? featureMethod(fs.spkPath) : fs.spkMethod;
        if (neurosuite::custody::supersampleOf(method.toStdString()) > 1) continue;
        if (QFileInfo(fs.spkPath).size() != nSpikes * recBytes) continue;
        if (!withShown && fs.spkPath == activeSpkPath) continue;
        if (std::any_of(spaces.begin(), spaces.end(), [&](const Space& s) { return s.path == fs.spkPath; })) continue;
        spaces.push_back(Space{method, fs.spkPath});
    }
    if (spaces.empty()) return R;

    // ── every cluster's clean rows: those naming no partner ──
    std::vector<char> isCollision(static_cast<std::size_t>(nSpikes), 0);
    {
        const nf::EapFile eap = currentEap(stageEapPath(), nSpikes);
        if (eap.ok && eap.nSpikes == nSpikes && eap.nClasses > 0)
            for (qint64 r = 0; r < nSpikes; ++r) {
                const int8_t* cells = &eap.cells[static_cast<std::size_t>(r) * eap.nClasses];
                int n = 0;
                for (int j = 0; j < eap.nClasses && n < 2; ++j) if (nf::eapPresent(cells[j])) ++n;
                isCollision[static_cast<std::size_t>(r)] = n >= 2;
            }
        for (const CollisionCheck& c : checks)             // a hit not yet marked is no clean row either
            if (c.row >= 0 && c.row < nSpikes) isCollision[static_cast<std::size_t>(c.row)] = 1;
    }
    QHash<int, QVector<int>> cleanOf;
    auto cleanRows = [&](int cluster) -> const QVector<int>& {
        auto it = cleanOf.find(cluster);
        if (it != cleanOf.end()) return it.value();
        QVector<int> rows = clusteringData->clusterSpkIndices(cluster);
        std::sort(rows.begin(), rows.end());
        QVector<int> clean;
        for (const int r : rows) if (r >= 0 && r < nSpikes && !isCollision[static_cast<std::size_t>(r)]) clean.append(r);
        return cleanOf.insert(cluster, clean).value();
    };

    // The checks in row order: each space's file is read forwards.
    std::vector<std::size_t> order(checks.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return checks[a].row < checks[b].row; });

    std::vector<int16_t> buf(rec);
    std::vector<float> w(rec), ownShifted;
    for (const Space& space : spaces) {
        QFile file(space.path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        auto readRow = [&](qint64 row) {
            // reinterpret_cast: the .spk's native int16 records.
            return file.seek(row * recBytes) && file.read(reinterpret_cast<char*>(buf.data()), recBytes) == recBytes;
        };
        QHash<qint64, std::shared_ptr<std::vector<float>>> cache;      // (cluster, stretch) -> template
        auto templateFor = [&](int cluster, qint64 row) -> std::shared_ptr<std::vector<float>> {
            const QVector<int>& rows = cleanRows(cluster);
            if (rows.size() < kMinTemplateRows) return nullptr;
            const int step = kTemplateSpikes / 2;
            const int pos = static_cast<int>(std::lower_bound(rows.begin(), rows.end(), static_cast<int>(row)) - rows.begin());
            const int stretch = pos / step;
            const qint64 key = (static_cast<qint64>(cluster) << 32) | static_cast<qint64>(stretch);
            const auto it = cache.constFind(key);
            if (it != cache.constEnd()) return it.value();
            int lo = std::max(0, stretch * step + step / 2 - kTemplateSpikes / 2);
            const int hi = std::min(static_cast<int>(rows.size()), lo + kTemplateSpikes);
            lo = std::max(0, hi - kTemplateSpikes);
            auto t = std::make_shared<std::vector<float>>(rec, 0.0f);
            int used = 0;
            for (int k = lo; k < hi; ++k) {
                if (!readRow(rows[k])) continue;
                for (std::size_t q = 0; q < rec; ++q) (*t)[q] += buf[q];
                ++used;
            }
            if (used < kMinTemplateRows) t.reset();
            else for (float& v : *t) v /= static_cast<float>(used);
            cache.insert(key, t);
            return t;
        };
        qint64 rejected = 0, checked = 0;
        for (const std::size_t i : order) {
            const CollisionCheck& c = checks[i];
            if (c.row < 0 || c.row >= nSpikes) continue;
            const std::shared_ptr<std::vector<float>> own = templateFor(c.cluster, c.row);
            const std::shared_ptr<std::vector<float>> partner = templateFor(c.partner, c.row);
            if (!own || !partner || !readRow(c.row)) { ++R.unchecked; continue; }       // nothing to say here
            for (std::size_t q = 0; q < rec; ++q) w[q] = static_cast<float>(buf[q]);
            const float* ownT = own->data();
            if (c.ownOffset != 0) { df::shifted(own->data(), nSamp, nChan, c.ownOffset, ownShifted); ownT = ownShifted.data(); }
            const df::Fit f = df::fitPartner(w.data(), ownT, partner->data(), nSamp, nChan, c.tau);
            ++checked;
            if (!f.accepted) { ++rejected; R.confirmed[i] = 0; }
        }
        R.spaces << space.method;
        R.checkedIn.insert(space.method, checked);
        R.rejectedIn.insert(space.method, rejected);
    }
    R.milliseconds = timer.elapsed();
    return R;
}

std::vector<qint64> KlustersDoc::unconfirmedCollisionRows(const QList<int>& clusters, bool children,
                                                          CollisionSpaceReport* report, QHash<int, qint64>* perPartner,
                                                          qint64* marked) const
{
    namespace nf = neurofileio;
    namespace df = neurosuite::decollide_fit;
    std::vector<qint64> out;
    if (marked) *marked = 0;
    if (!clusteringData) return out;
    const std::vector<qint64> rows = collisionRowsOf(clusters, children);
    if (marked) *marked = static_cast<qint64>(rows.size());
    if (rows.empty()) return out;
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const nf::EapFile eap = currentEap(stageEapPath(), nSpikes);
    if (!eap.ok || eap.nSpikes != nSpikes) return out;
    std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
    for (const dataType id : clusteringData->clusterIds())
        for (const int r : clusteringData->clusterSpkIndices(static_cast<int>(id)))
            if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
    const std::vector<int> home = collisionClassHomes(eap, clu);

    // One check per partner of a row; the row stands only if every partner does.
    std::vector<CollisionCheck> checks;
    df::Member own; std::vector<df::Member> partners;
    for (const qint64 row : rows) {
        const int cluster = clu[static_cast<std::size_t>(row)];
        if (cluster <= 1 || !df::splitRow(eap, row, cluster, home, own, partners)) continue;
        for (const df::Member& p : partners) {
            const int partner = (p.classId >= 0 && p.classId < static_cast<int>(home.size())) ? home[static_cast<std::size_t>(p.classId)] : -1;
            if (partner > 1 && partner != cluster) checks.push_back(CollisionCheck{row, cluster, own.offset, partner, p.offset});
        }
    }
    const CollisionSpaceReport R = confirmCollisionsInSpaces(checks, /*withShown=*/true);
    for (std::size_t i = 0; i < checks.size(); ++i) {
        if (R.confirmed[i]) continue;
        if (out.empty() || out.back() != checks[i].row) out.push_back(checks[i].row);
        if (perPartner) ++(*perPartner)[checks[i].partner];
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (report) *report = R;
    QStringList per;
    for (const QString& m : R.spaces) per << QStringLiteral("%1: %2 of %3").arg(m).arg(R.rejectedIn.value(m)).arg(R.checkedIn.value(m));
    qInfo().noquote() << "[mark collisions]" << rows.size() << "marked rows fitted in every waveform space:" << out.size()
                      << "not confirmed in all of them (not accepted in" << per.join(QStringLiteral("; ")) << ");"
                      << R.unchecked << "fits without a template;" << R.milliseconds << "ms";
    return out;
}
