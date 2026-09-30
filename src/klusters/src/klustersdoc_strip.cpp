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
#include <cstring>          // memcpy — overlay patch in the coalesced sweep
#include <vector>

#include <QDebug>
#include <QElapsedTimer>

#include "klustersdoc.h"
#include "data.h"
#include "klustersview.h"
#include "klusters.h"
#include "templatematrixthread.h"   // tmReadSpikeFloat — the shared .spk reader

KlustersDoc::TemplateStripResult
KlustersDoc::stripByTemplate(int               templateCluster,
                             const QList<int>& sourceClusters,
                             double            maxDistance,
                             double            minAmplitudeRatio,
                             double            maxAmplitudeRatio,
                             double            maxChannelDistance,
                             bool              onePerSource,
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

    // One shared pread descriptor serves the whole strip (see spkreader.h);
    // a missing file surfaces as zero readable template waveforms below.
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = clusteringData->currentSnapshot();
    const Data::ClusteringSnapshot& spkSrc = *snap;   // this epoch's bytes (overlay + descriptor)

    // ── Template: per-point median over up to 1024 evenly-strided spikes ─
    SortableTable tplPos;
    if (!layer.spikePositions(templateCluster, tplPos)) {
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
        if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav)) continue;
        for (int ci = 0; ci < nSel; ++ci) {
            const float* src = &wav[static_cast<size_t>(chans[ci]) * nSamp];
            tplBuf.insert(tplBuf.end(), src, src + nSamp);
        }
        ++used;
    }
    if (used < 8) {
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
        R.reason = tr("The template is flat on the selected channels; select "
                      "channels that carry its waveform.");
        return R;
    }
    const double denom = std::sqrt(E / W);

    // Per-channel normalization for the uniformity gate: the same
    // construction restricted to one channel.  Channels carrying < 5% of the
    // template's kernel energy cannot be normalized meaningfully (a flank
    // with no template signal would gate on noise), so they take part in the
    // pooled D only.
    std::vector<double> Wc(static_cast<size_t>(nSel), 0.0);
    std::vector<double> Ec(static_cast<size_t>(nSel), 0.0);
    std::vector<double> denomC(static_cast<size_t>(nSel), 0.0);
    double maxEc = 0.0;
    for (int ci = 0; ci < nSel; ++ci) {
        for (int t = 0; t < nSamp; ++t) {
            const size_t p = static_cast<size_t>(ci) * nSamp + t;
            Wc[static_cast<size_t>(ci)] += w[p];
            Ec[static_cast<size_t>(ci)] += static_cast<double>(w[p]) * T[p] * T[p];
        }
        maxEc = std::max(maxEc, Ec[static_cast<size_t>(ci)]);
    }
    std::vector<bool> gateCh(static_cast<size_t>(nSel), false);
    for (int ci = 0; ci < nSel; ++ci) {
        const bool carries = Ec[static_cast<size_t>(ci)] >= 0.05 * maxEc
                             && Wc[static_cast<size_t>(ci)] > 0.0;
        gateCh[static_cast<size_t>(ci)] = carries;
        if (carries)
            denomC[static_cast<size_t>(ci)] =
                std::sqrt(Ec[static_cast<size_t>(ci)] / Wc[static_cast<size_t>(ci)]);
    }
    const bool ampGate  = minAmplitudeRatio > 0.0 || maxAmplitudeRatio < 9.99;
    const bool chanGate = maxChannelDistance > 0.0;

    // ── Score every source spike, collect rows at or below threshold ────
    // File-order sweep through coalesced reads.  The historical per-source
    // loops issued ONE positioned read per record; a whole-scope strip reads
    // nearly every record of the .spk, so that was millions of syscalls per
    // gesture.  The sources' spikes are gathered into one row-sorted
    // worklist first; runs of strictly consecutive records — which dominate
    // when most of the file is in scope, since only the template's and the
    // reserve bins' spikes break the sequence — are read in single readSpk
    // calls of a few MB, and each record of a run is then overlay-patched
    // under readSpk's own hit rule (an entry at exactly the record's offset
    // with exactly its size), so a pending realign/nudge rewrite is
    // honoured record for record exactly as the per-record path honoured
    // it.  Scattered rows degrade to the historical one-read-per-record
    // (tmReadSpikeFloat) — never more I/O volume, only fewer calls.
    QSet<dataType> rows;
    QHash<int, QSet<dataType>> rowsBySource;
    QHash<int, long> matchedBySource;
    long nCand = 0;

    struct Cand { long row; int src; };
    std::vector<Cand> work;
    {
        size_t total = 0;
        for (int src : sources) {
            const dataType n = layer.nbOfSpikes(static_cast<dataType>(src));
            if (n > 0) total += static_cast<size_t>(n);
        }
        work.reserve(total);
    }
    for (int src : sources) {
        SortableTable pos;
        if (!layer.spikePositions(src, pos)) continue;
        const long n = static_cast<long>(
            layer.nbOfSpikes(static_cast<dataType>(src)));
        for (long s = 0; s < n; ++s)
            work.push_back({ static_cast<long>(pos(1, s + 1)), src });
    }
    std::sort(work.begin(), work.end(),
              [](const Cand& a, const Cand& b){ return a.row < b.row; });

    const size_t nPts        = static_cast<size_t>(nCh) * nSamp;
    const qint64 recordBytes = static_cast<qint64>(nPts)
                             * static_cast<qint64>(sizeof(int16_t));
    const long   runCap      = std::max<long>(
        1, static_cast<long>((4LL << 20) / recordBytes));   // ~4 MB per read
    std::vector<int16_t> block;

    QElapsedTimer sweepTimer;
    sweepTimer.start();
    long nRuns = 0;

    size_t i = 0;
    while (i < work.size()) {
        size_t j = i + 1;                    // maximal consecutive run, capped
        while (j < work.size()
               && work[j].row == work[j - 1].row + 1
               && static_cast<long>(j - i) < runCap)
            ++j;
        const long   nRec   = static_cast<long>(j - i);
        const qint64 runOff = (static_cast<qint64>(work[i].row) - 1) * recordBytes;
        ++nRuns;

        bool blockOk = false;
        if (nRec > 1) {
            block.resize(static_cast<size_t>(nRec) * nPts);
            blockOk = spkSrc.readSpk(block.data(), nRec * recordBytes, runOff);
            if (blockOk && spkSrc.spkOverlay) {
                for (long r = 0; r < nRec; ++r) {
                    const qint64 off = runOff + r * recordBytes;
                    const auto it = spkSrc.spkOverlay->records.constFind(off);
                    if (it != spkSrc.spkOverlay->records.constEnd()
                        && static_cast<qint64>((*it)->size()) == recordBytes)
                        memcpy(block.data() + static_cast<size_t>(r) * nPts,
                               (*it)->constData(),
                               static_cast<size_t>(recordBytes));
                }
            }
        }

        for (size_t k = i; k < j; ++k) {
            const long row = work[k].row;
            const int  src = work[k].src;
            if (blockOk) {
                const int16_t* rec = block.data()
                    + static_cast<size_t>(row - work[i].row) * nPts;
                if (wav.size() < nPts) wav.resize(nPts);
                // De-interleave exactly as tmReadSpikeFloat does.
                for (int ch = 0; ch < nCh; ++ch)
                    for (int sm = 0; sm < nSamp; ++sm)
                        wav[static_cast<size_t>(ch) * nSamp + sm] =
                            static_cast<float>(
                                rec[static_cast<size_t>(sm) * nCh + ch]);
            }
            else if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav))
                continue;                     // short read: skip, as before
            ++nCand;
            double q = 0.0, xtw = 0.0, worstChan = 0.0;
            for (int ci = 0; ci < nSel; ++ci) {
                const float* xw = &wav[static_cast<size_t>(chans[ci]) * nSamp];
                const float* tp = &T[static_cast<size_t>(ci) * nSamp];
                const float* wp = &w[static_cast<size_t>(ci) * nSamp];
                double qc = 0.0;
                for (int t = 0; t < nSamp; ++t) {
                    const double d = static_cast<double>(xw[t]) - tp[t];
                    qc  += wp[t] * d * d;
                    xtw += static_cast<double>(wp[t]) * xw[t] * tp[t];
                }
                q += qc;
                if (chanGate && gateCh[static_cast<size_t>(ci)])
                    worstChan = std::max(worstChan,
                        std::sqrt(qc / Wc[static_cast<size_t>(ci)])
                            / denomC[static_cast<size_t>(ci)]);
            }
            const double D = std::sqrt(q / W) / denom;
            if (D > maxDistance) continue;
            if (ampGate) {
                const double g = xtw / E;      // kernel-weighted matched gain
                if (g < minAmplitudeRatio || g > maxAmplitudeRatio) {
                    ++R.nRejectedAmplitude;
                    continue;
                }
            }
            if (chanGate && worstChan > maxChannelDistance) {
                ++R.nRejectedChannel;
                continue;
            }
            rows.insert(static_cast<dataType>(row));
            rowsBySource[src].insert(static_cast<dataType>(row));
            ++matchedBySource[src];
        }
        i = j;
    }
    qDebug().noquote()
        << QStringLiteral("[strip] scored %1 records through %2 coalesced "
                          "reads in %3 ms")
               .arg(nCand).arg(nRuns).arg(sweepTimer.elapsed());
    //R.sources in the original selection order, as the per-source loops
    //reported it (a source counts once it contributed at least one match).
    for (int src : sources)
        if (matchedBySource.value(src, 0) > 0)
            R.sources.append(src);

    R.nCandidates = static_cast<int>(nCand);
    R.nMatched    = static_cast<int>(rows.size());
    if (rows.isEmpty()) {
        R.reason = tr("No spikes accepted for template %1 on the selected "
                      "channels (%2 examined; %3 within distance %4 but "
                      "rejected by the amplitude window, %5 by channel "
                      "uniformity).  Raise the threshold, widen the gates, or "
                      "check the channel selection.")
                       .arg(templateCluster).arg(nCand)
                       .arg(R.nRejectedAmplitude).arg(maxDistance)
                       .arg(R.nRejectedChannel);
        return R;
    }

    // ── The cut: the lasso's row-named path ─────────────────────────────
    int nProducts = 0;
    QList<int> products;                       // the strip's landing, below
    if (onePerSource) {
        products = createNewClusters(SpikeSelection(rows,
                       QStringLiteral("template_strip")), R.sources);
        nProducts = products.size();
    } else {
        // Combined product(s): one per parent in scope, so a joint child
        // scope cannot mint a parent-straddling atom; the parent scope is a
        // single group.  Each group is one singular createNewCluster -- the
        // lasso's own path -- so undo, log and landing stay per-product.
        QMap<int, QPair<QSet<dataType>, QList<int>>> groups;
        for (int src : R.sources) {
            const int grp = onChild ? parentOfChild(src) : 0;
            auto& g = groups[grp];
            g.first.unite(rowsBySource.value(src));
            g.second.append(src);
        }
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            const int product =
                createNewCluster(SpikeSelection(it.value().first,
                                     QStringLiteral("template_strip")),
                                 it.value().second);
            if (product > 0) { products.append(product); ++nProducts; }
        }
    }
    // Land on the products ALONE, overriding the ride path's split landing
    // (products + surviving sources).  That landing is right for a lasso —
    // a handful of pieces judged against each other — but a whole-scope
    // strip has hundreds of surviving donors, and the post-edit apply
    // (applyPendingParentSelection, or the child palette's drain) both
    // SELECTS and SHOWS the parked list, burying the product under a
    // selection the views struggle to draw.  The strip's judgement object
    // is the product; the donors keep their places.
    if (!products.isEmpty()) {
        std::sort(products.begin(), products.end());
        if (onChild) setPendingChildSelection(products);
        else         setPendingParentSelection(products);
    }
    R.accepted = true;
    R.reason = tr("Stripped %1 of %2 examined spikes matching template %3 "
                  "(distance <= %4, %5 channel(s)) out of %6 source "
                  "cluster(s) into %7 new cluster(s)%8.")
                   .arg(R.nMatched).arg(R.nCandidates).arg(templateCluster)
                   .arg(maxDistance).arg(nSel).arg(R.sources.size())
                   .arg(nProducts)
                   .arg(onePerSource ? tr(" (one per source)")
                                     : tr(" (combined per parent)"));
    if (R.nRejectedAmplitude > 0 || R.nRejectedChannel > 0)
        R.reason += tr("  Gates rejected %1 within-distance spike(s): %2 by "
                       "the amplitude window, %3 by channel uniformity.")
                        .arg(R.nRejectedAmplitude + R.nRejectedChannel)
                        .arg(R.nRejectedAmplitude).arg(R.nRejectedChannel);
    return R;
}


// ---------------------------------------------------------------------------
// detectWaveformOutliers — the inverse of the template strip.
//
// stripByTemplate PULLS matching spikes toward a designated template; this
// PUSHES each cluster's own worst-fitting spikes out.  It scores a cluster's
// spikes against the cluster's OWN median waveform with the identical
// kernel-weighted residual D, then flags the robust tail
// median(D) + kMad*1.4826*MAD(D).  Non-mutating: the caller confirms the count
// and moves the flagged rows to the artefact cluster through the same undoable
// moveSpikeSubsetToCluster the feature-outlier strip uses.  Parent scope only,
// matching that mutation path (idsBelongTo(parentData(), ...)).
//
// Only a handful of selected clusters are ever in scope for one gesture, so
// the read is the straightforward one-record-per-spike tmReadSpikeFloat rather
// than stripByTemplate's whole-scope coalesced sweep.
// ---------------------------------------------------------------------------
KlustersDoc::WaveformOutlierResult
KlustersDoc::detectWaveformOutliers(const QList<int>& clusters,
                                    double            kMad,
                                    long              minSpikes)
{
    WaveformOutlierResult R;

    Data& layer = parentData();                     // parent scope (see header)
    const int nCh   = layer.nbOfChannels();
    const int nSamp = layer.nbOfSampleInWaveform();
    if (nCh <= 0 || nSamp <= 0) {
        R.reason = tr("The document carries no waveform geometry.");
        return R;
    }
    QList<int> chans = selectedChannels();          // validated, sorted, unique
    if (chans.isEmpty())
        for (int c = 0; c < nCh; ++c) chans.append(c);
    const int nSel = chans.size();
    const int P    = nSel * nSamp;

    const std::shared_ptr<const Data::ClusteringSnapshot> snap = layer.currentSnapshot();
    const Data::ClusteringSnapshot& spkSrc = *snap;

    std::vector<int16_t> raw;
    std::vector<float>   wav;

    for (int c : clusters) {
        if (c < 2) continue;                        // never strip artefact / noise
        SortableTable pos;
        if (!layer.spikePositions(c, pos)) continue;
        const long n = static_cast<long>(layer.nbOfSpikes(static_cast<dataType>(c)));
        if (n < minSpikes) continue;                // MAD too noisy below the floor

        // ── the cluster's own median template (<=1024 evenly-strided) ──
        const long step = std::max(1L, n / 1024L);
        std::vector<float> tplBuf;
        tplBuf.reserve(static_cast<size_t>(P) * std::min(n, 1024L));
        long used = 0;
        for (long s = 0; s < n; s += step) {
            const long row = static_cast<long>(pos(1, s + 1));
            if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav)) continue;
            for (int ci = 0; ci < nSel; ++ci) {
                const float* src = &wav[static_cast<size_t>(chans[ci]) * nSamp];
                tplBuf.insert(tplBuf.end(), src, src + nSamp);
            }
            ++used;
        }
        if (used < 8) continue;                      // too few readable waveforms
        std::vector<float> T(static_cast<size_t>(P));
        std::vector<float> col(static_cast<size_t>(used));
        for (int p = 0; p < P; ++p) {
            for (long i = 0; i < used; ++i)
                col[static_cast<size_t>(i)] = tplBuf[static_cast<size_t>(i) * P + p];
            std::nth_element(col.begin(), col.begin() + used / 2, col.end());
            T[static_cast<size_t>(p)] = col[static_cast<size_t>(used / 2)];
        }

        // ── kernel + normalization (identical to stripByTemplate) ──
        std::vector<float> w(static_cast<size_t>(P));
        double W = 0.0, E = 0.0;
        for (int p = 0; p < P; ++p) {
            w[static_cast<size_t>(p)] = std::fabs(T[static_cast<size_t>(p)]);
            W += w[static_cast<size_t>(p)];
            E += static_cast<double>(w[static_cast<size_t>(p)])
                 * T[static_cast<size_t>(p)] * T[static_cast<size_t>(p)];
        }
        if (W <= 0.0 || E <= 0.0) continue;          // flat template
        const double denom = std::sqrt(E / W);

        // ── D for every spike of the cluster (kept with its row) ──
        std::vector<double> D;    D.reserve(static_cast<size_t>(n));
        std::vector<long>   rows; rows.reserve(static_cast<size_t>(n));
        for (long s = 0; s < n; ++s) {
            const long row = static_cast<long>(pos(1, s + 1));
            if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav)) continue;
            double q = 0.0;
            for (int ci = 0; ci < nSel; ++ci) {
                const float* xw = &wav[static_cast<size_t>(chans[ci]) * nSamp];
                const float* tp = &T[static_cast<size_t>(ci) * nSamp];
                const float* wp = &w[static_cast<size_t>(ci) * nSamp];
                for (int t = 0; t < nSamp; ++t) {
                    const double dd = static_cast<double>(xw[t]) - tp[t];
                    q += wp[t] * dd * dd;
                }
            }
            D.push_back(std::sqrt(q / W) / denom);
            rows.push_back(row);
        }
        if (D.size() < static_cast<size_t>(minSpikes)) continue;

        // ── robust one-sided threshold: median + kMad * 1.4826 * MAD ──
        std::vector<double> tmp(D);
        const size_t mid = tmp.size() / 2;
        std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
        const double med = tmp[mid];
        for (size_t i = 0; i < tmp.size(); ++i) tmp[i] = std::fabs(D[i] - med);
        std::nth_element(tmp.begin(), tmp.begin() + mid, tmp.end());
        const double mad = tmp[mid];
        if (mad <= 0.0) continue;                     // degenerate: nothing to separate
        const double thr = med + kMad * 1.4826 * mad;

        QVector<int> flagged;
        for (size_t i = 0; i < D.size(); ++i)
            if (D[i] > thr)
                flagged.append(static_cast<int>(rows[i]) - 1);   // -> 0-based .spk index
        if (!flagged.isEmpty()) {
            R.total += flagged.size();
            R.byCluster.insert(c, flagged);
        }
        ++R.scored;
    }

    R.ok = true;
    if (R.scored == 0)
        R.reason = tr("Strip waveform outliers: no selected cluster carries "
                      "enough spikes (>= %1) for a stable median waveform.")
                       .arg(minSpikes);
    return R;
}
