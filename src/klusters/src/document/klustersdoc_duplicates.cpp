/***************************************************************************
 * klustersdoc_duplicates.cpp -- one spike held by more than one row
 *
 * Clusters > Remove Duplicate Spikes.  Two rows of one cluster closer in time
 * than a neuron fires twice are one spike detected more than once: the detector
 * fired twice on it, or a realignment brought two detections to the same peak.
 * Each such row counts as a spike in the unit's rate, its correlograms and its
 * mean waveform.
 *
 * scanDuplicateSpikes finds them (duplicatespikes.h) and chooses, of each run,
 * the row to keep: the one whose record is most like the cluster's template
 * around it (the mean of up to kTemplateSpikes rows of the cluster nearest in
 * time, the run's own left out), the lowest row when they tie -- as they do when
 * the rows have the very same time and so the very same record.  The others are
 * the EXTRAS; the action moves them to the artefact cluster (KlustersApp::
 * slotRemoveDuplicateSpikes), which is an ordinary, undoable edit.
 *
 * Rows this near in time in two DIFFERENT clusters are counted and not touched:
 * that is either one spike sorted twice or a collision of two units, and which
 * row is the wrong one is not for this to say.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "klustersdoc.h"

#include "data.h"
#include "duplicatespikes.h"
#include "reservedclusters.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {
constexpr int kTemplateSpikes = 300;   ///< rows the template a run is judged against is the mean of, at most
constexpr int kMinTemplate    = 30;    ///< with fewer the lowest row of a run is kept
}

KlustersDoc::DuplicateScan KlustersDoc::scanDuplicateSpikes(const QList<int>& clusters, bool children, double toleranceMs)
{
    DuplicateScan R;
    if (!clusteringData) { R.reason = tr("No session is open."); return R; }
    Data* layer = children ? childData : clusteringData;
    if (!layer) { R.reason = tr("The session has no child clustering."); return R; }
    if (clusteringData->supersampleFactor() != 1) {
        R.reason = tr("A super-sampled waveform space is shown: duplicates are looked for in the session's own.");
        return R;
    }
    const qint64 nSpikes = static_cast<qint64>(clusteringData->totalNbOfSpikes());
    const int nSamp = clusteringData->nbSamplesPerWaveform();
    const int nChan = clusteringData->nbOfChannels();
    const std::size_t rec = static_cast<std::size_t>(nSamp) * nChan;
    const qint64 recBytes = static_cast<qint64>(rec * sizeof(int16_t));
    const double rate = clusteringData->getSamplingRate() > 0.0 ? clusteringData->getSamplingRate() : 1.0;
    R.toleranceSamples = std::max<qint64>(0, std::llround(toleranceMs * rate / 1000.0));
    const int timeDim = clusteringData->nbOfDimensionsTotal();
    auto timeOf = [&](qint64 row) { return static_cast<qint64>(clusteringData->featureValue(static_cast<dataType>(row + 1), timeDim)); };
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();

    std::vector<int16_t> buf(rec);
    std::vector<double> tpl(rec);
    for (const int id : clusters) {
        if (id <= 1 || (!children && isReservedClusterId(static_cast<dataType>(id)))) continue;
        std::vector<std::pair<qint64, qint64>> byTime;          // (time, row)
        for (const int r : layer->clusterSpkIndices(id))
            if (r >= 0 && r < nSpikes) byTime.emplace_back(timeOf(r), r);
        std::sort(byTime.begin(), byTime.end());
        R.scanned += static_cast<qint64>(byTime.size());
        std::vector<qint64> times(byTime.size());
        for (std::size_t i = 0; i < byTime.size(); ++i) times[i] = byTime[i].first;
        const auto found = duplicatespikes::runs<qint64>(times, R.toleranceSamples);
        std::vector<char> inRun(byTime.size(), 0);
        for (const auto& run : found)
            for (std::size_t i = run.first; i < run.second; ++i) inRun[i] = 1;
        for (const auto& run : found) {
            // The template around the run: rows of the cluster on either side, no run's rows.
            std::fill(tpl.begin(), tpl.end(), 0.0);
            int used = 0;
            for (std::size_t step = 1; used < kTemplateSpikes && (run.first >= step || run.second - 1 + step < byTime.size()); ++step) {
                for (const bool before : {true, false}) {
                    if (before && run.first < step) continue;
                    const std::size_t at = before ? run.first - step : run.second - 1 + step;
                    if (at >= byTime.size() || inRun[at]) continue;
                    if (!snap->readSpk(buf.data(), recBytes, byTime[at].second * recBytes)) continue;
                    for (std::size_t i = 0; i < rec; ++i) tpl[i] += buf[i];
                    ++used;
                }
            }
            qint64 keep = byTime[run.first].second;
            if (used >= kMinTemplate) {
                double tt = 0.0;
                for (const double v : tpl) tt += v * v;
                double best = -2.0;
                for (std::size_t i = run.first; i < run.second; ++i) {
                    if (!snap->readSpk(buf.data(), recBytes, byTime[i].second * recBytes)) continue;
                    double xt = 0.0, xx = 0.0;
                    for (std::size_t q = 0; q < rec; ++q) { xt += buf[q] * tpl[q]; xx += static_cast<double>(buf[q]) * buf[q]; }
                    const double cosine = (xx > 0.0 && tt > 0.0) ? xt / std::sqrt(xx * tt) : -1.0;
                    // The lowest row wins a tie (rows at one time have one record).
                    if (cosine > best + 1e-12 || (std::fabs(cosine - best) <= 1e-12 && byTime[i].second < keep)) { best = cosine; keep = byTime[i].second; }
                }
            } else {
                for (std::size_t i = run.first; i < run.second; ++i) keep = std::min(keep, byTime[i].second);
            }
            QList<qint64> rows;
            rows.append(keep);
            bool oneTime = true;
            for (std::size_t i = run.first; i < run.second; ++i) {
                if (byTime[i].first != byTime[run.first].first) oneTime = false;
                if (byTime[i].second == keep) continue;
                rows.append(byTime[i].second);
                R.extras.insert(static_cast<dataType>(byTime[i].second + 1));
                ++R.extrasOf[id];
            }
            R.runRows.append(rows);
            ++R.runs;
            if (oneTime) ++R.atOneTime;
        }
    }

    // ── the same time in two different clusters: counted, for the selected ones ──
    {
        QSet<int> chosen;                                       // as parent clusters
        for (const int id : clusters) chosen.insert(children ? parentOfChild(id) : id);
        std::vector<int> clu(static_cast<std::size_t>(nSpikes), 0);
        for (const dataType id : clusteringData->clusterIds())
            for (const int r : clusteringData->clusterSpkIndices(static_cast<int>(id)))
                if (r >= 0 && r < nSpikes) clu[static_cast<std::size_t>(r)] = static_cast<int>(id);
        std::vector<std::pair<qint64, int>> real;               // (time, cluster) of every row in a real cluster
        real.reserve(static_cast<std::size_t>(nSpikes));
        for (qint64 i = 0; i < nSpikes; ++i)
            if (clu[static_cast<std::size_t>(i)] > 1) real.emplace_back(timeOf(i), clu[static_cast<std::size_t>(i)]);
        std::sort(real.begin(), real.end());
        std::vector<qint64> times(real.size());
        std::vector<int> labels(real.size());
        for (std::size_t i = 0; i < real.size(); ++i) { times[i] = real[i].first; labels[i] = real[i].second; }
        for (const auto& pair : duplicatespikes::acrossLabels<qint64, int>(times, labels, R.toleranceSamples)) {
            const int a = std::min(labels[pair.first], labels[pair.second]), b = std::max(labels[pair.first], labels[pair.second]);
            if (!chosen.contains(a) && !chosen.contains(b)) continue;
            ++R.acrossClusters;
            ++R.acrossPairs[QStringLiteral("%1+%2").arg(a).arg(b)];
        }
    }
    R.ok = true;
    return R;
}
