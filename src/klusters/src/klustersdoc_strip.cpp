// klustersdoc_strip.cpp — KlustersDoc template strip.
//
// stripByTemplate: designate one cluster of the active layer as a waveform
// TEMPLATE and pull, from the other selected clusters of the same layer, every
// spike whose normalized kernel-weighted residual against the template's
// median waveform — restricted to the document's channel selection — is at or
// below a threshold.  The metric and the row collection are the only new
// machinery here: the mutation itself rides the row-named createNewClusters
// path (the lasso's), which already owns thread quiesce, undo on the right
// timeline, the parent-scope curation log, hierarchy refresh and the parked
// landing, and produces one new cluster per source so a joint child scope
// never creates a parent-straddling atom.
//
// Waveform access mirrors TemplateMatrixThread: layer spikePositions() names
// the feature rows, row-1 is the .spk record index, and tmReadSpikeFloat
// de-interleaves one record into a channel-major float buffer.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "klustersdoc.h"
#include "data.h"
#include "klustersview.h"
#include "klusters.h"
#include "templatematrixthread.h"   // tmReadSpikeFloat — the shared .spk reader

KlustersDoc::TemplateStripResult
KlustersDoc::stripByTemplate(int               templateCluster,
                             const QList<int>& sourceClusters,
                             double            maxDistance,
                             bool              onChild)
{
    TemplateStripResult R;
    R.templateId = templateCluster;

    if (onChild && !childData) {
        R.reason = tr("No child (atom) clustering is loaded.");
        return R;
    }
    // The cut goes through createNewClusters, which routes by the ACTIVE
    // clustering; the metric must read the SAME layer.  Refuse a mismatch
    // outright rather than score one layer and cut the other -- the
    // id-collision family again.
    if ((&data() == childData) != onChild) {
        R.reason = tr("The active clustering does not match the requested "
                      "scope any more; re-invoke the strip.");
        return R;
    }
    Data& layer = onChild ? *childData : *clusteringData;

    QList<int> sources = sourceClusters;
    sources.removeAll(templateCluster);
    sources.removeAll(ClusterId::Artefact);
    sources.removeAll(ClusterId::Noise);
    if (sources.isEmpty()) {
        R.reason = tr("No source clusters besides the template (artifact and "
                      "noise are never stripped).");
        return R;
    }

    const long nTpl = static_cast<long>(
        layer.nbOfSpikes(static_cast<dataType>(templateCluster)));
    if (nTpl < 8) {
        R.reason = tr("Template cluster %1 has only %2 spikes; at least 8 are "
                      "needed for a stable median waveform.")
                       .arg(templateCluster).arg(nTpl);
        return R;
    }

    // ── Channel restriction: the document's selection, or all channels ──
    const int nCh   = clusteringData->nbOfChannels();
    const int nSamp = clusteringData->nbOfSampleInWaveform();
    if (nCh <= 0 || nSamp <= 0) {
        R.reason = tr("The document carries no waveform geometry.");
        return R;
    }
    QList<int> chans = selectedChannels();          // validated, sorted, unique
    if (chans.isEmpty())
        for (int c = 0; c < nCh; ++c) chans.append(c);

    FILE* spk = fopen(clusteringData->getSpkFileName().toLocal8Bit().constData(),
                      "rb");
    if (!spk) {
        R.reason = tr("Could not open the spike waveform file.");
        return R;
    }

    // ── Template: per-point median over up to 1024 evenly-strided spikes ─
    SortableTable tplPos;
    if (!layer.spikePositions(templateCluster, tplPos)) {
        fclose(spk);
        R.reason = tr("Template cluster %1 has no spike-position table.")
                       .arg(templateCluster);
        return R;
    }
    const int  nSel = chans.size();
    const int  P    = nSel * nSamp;                 // points in the metric
    const long step = std::max(1L, nTpl / 1024L);
    std::vector<int16_t> raw;
    std::vector<float>   wav;
    std::vector<float>   tplBuf;                    // [spike][P], ci-major
    tplBuf.reserve(static_cast<size_t>(P) * std::min(nTpl, 1024L));
    long used = 0;
    for (long s = 0; s < nTpl; s += step) {
        const long row = static_cast<long>(tplPos(1, s + 1));
        if (!tmReadSpikeFloat(spk, row - 1, nCh, nSamp, raw, wav)) continue;
        for (int ci = 0; ci < nSel; ++ci) {
            const float* src = &wav[static_cast<size_t>(chans[ci]) * nSamp];
            tplBuf.insert(tplBuf.end(), src, src + nSamp);
        }
        ++used;
    }
    if (used < 8) {
        fclose(spk);
        R.reason = tr("Could only read %1 template waveforms from the spike "
                      "file.").arg(used);
        return R;
    }
    std::vector<float> T(static_cast<size_t>(P));
    std::vector<float> col(static_cast<size_t>(used));
    for (int p = 0; p < P; ++p) {
        for (long i = 0; i < used; ++i)
            col[static_cast<size_t>(i)] =
                tplBuf[static_cast<size_t>(i) * P + p];
        std::nth_element(col.begin(), col.begin() + used / 2, col.end());
        T[static_cast<size_t>(p)] = col[static_cast<size_t>(used / 2)];
    }

    // ── Kernel and normalization ────────────────────────────────────────
    std::vector<float> w(static_cast<size_t>(P));
    double W = 0.0, E = 0.0;
    for (int p = 0; p < P; ++p) {
        w[static_cast<size_t>(p)] = std::fabs(T[static_cast<size_t>(p)]);
        W += w[static_cast<size_t>(p)];
        E += static_cast<double>(w[static_cast<size_t>(p)])
             * T[static_cast<size_t>(p)] * T[static_cast<size_t>(p)];
    }
    if (W <= 0.0 || E <= 0.0) {
        fclose(spk);
        R.reason = tr("The template is flat on the selected channels; select "
                      "channels that carry its waveform.");
        return R;
    }
    const double denom = std::sqrt(E / W);

    // ── Score every source spike, collect rows at or below threshold ────
    QSet<dataType> rows;
    long nCand = 0;
    for (int src : sources) {
        SortableTable pos;
        if (!layer.spikePositions(src, pos)) continue;
        const long n = static_cast<long>(
            layer.nbOfSpikes(static_cast<dataType>(src)));
        long matched = 0;
        for (long s = 0; s < n; ++s) {
            const long row = static_cast<long>(pos(1, s + 1));
            if (!tmReadSpikeFloat(spk, row - 1, nCh, nSamp, raw, wav)) continue;
            ++nCand;
            double q = 0.0;
            for (int ci = 0; ci < nSel; ++ci) {
                const float* xw = &wav[static_cast<size_t>(chans[ci]) * nSamp];
                const float* tp = &T[static_cast<size_t>(ci) * nSamp];
                const float* wp = &w[static_cast<size_t>(ci) * nSamp];
                for (int t = 0; t < nSamp; ++t) {
                    const double d = static_cast<double>(xw[t]) - tp[t];
                    q += wp[t] * d * d;
                }
            }
            const double D = std::sqrt(q / W) / denom;
            if (D <= maxDistance) {
                rows.insert(static_cast<dataType>(row));
                ++matched;
            }
        }
        if (matched > 0) R.sources.append(src);
    }
    fclose(spk);

    R.nCandidates = static_cast<int>(nCand);
    R.nMatched    = static_cast<int>(rows.size());
    if (rows.isEmpty()) {
        R.reason = tr("No spikes within distance %1 of template %2 on the "
                      "selected channels (%3 examined).  Raise the threshold "
                      "or check the channel selection.")
                       .arg(maxDistance).arg(templateCluster).arg(nCand);
        return R;
    }

    // ── The cut: the lasso's row-named path, one product per source ─────
    createNewClusters(SpikeSelection(rows, QStringLiteral("template_strip")),
                      R.sources);
    R.accepted = true;
    R.reason = tr("Stripped %1 of %2 examined spikes matching template %3 "
                  "(distance <= %4, %5 channel(s)) out of %6 source "
                  "cluster(s), one new cluster per source.")
                   .arg(R.nMatched).arg(R.nCandidates).arg(templateCluster)
                   .arg(maxDistance).arg(nSel).arg(R.sources.size());
    return R;
}
