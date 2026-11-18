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
#include <limits>
#include <vector>

#include <QDebug>
#include <QElapsedTimer>
#include <QStringList>

#include "klustersdoc.h"
#include "data.h"
#include "clusterlabel.h"
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
                       .arg(clusterLabel(templateCluster)).arg(nTpl);
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
                       .arg(clusterLabel(templateCluster));
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
                       .arg(clusterLabel(templateCluster)).arg(nCand)
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
                   .arg(R.nMatched).arg(R.nCandidates).arg(clusterLabel(templateCluster))
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


// ---------------------------------------------------------------------------
// detectResidualOutliers — the gain-free, on/off-channel split of the strip.
//
// detectWaveformOutliers flags the robust tail of ONE distance: the
// kernel-weighted residual of a spike against its cluster's median waveform,
// with no per-spike gain.  Two things that costs it on a contaminated unit:
//
//   * amplitude counts as distance.  A bursting cell's own small spikes, or a
//     drifting one's, sit in the tail beside genuine intruders.  Measured on
//     the curated interneuron this was built for (jg05-20120312 group 6,
//     cluster 785's parent in exp_psame), amplitude ALONE separates the unit
//     from its siblings at AUC 0.635 — a gain-blind distance is partly an
//     amplitude cut wearing a shape cut's clothes.
//   * the |T| point kernel is blind exactly where an intruder shows itself.
//     It weights each point by the template's own magnitude, so off-template
//     channels — the ones a spatially displaced contaminant lands on — carry
//     almost no weight in the pooled distance.
//
// So: fit a per-spike gain a on the template's own channels, take the residual
// r = x - a*T, and split its power by how much of the template each channel
// carries.  Per cluster, over the document's channel selection:
//
//   T[c,t]  median waveform of the cluster's own spikes        (as above)
//   h[t]    Hamming taper centred on the template's peak sample, 0 outside
//           its window (1 everywhere when the window is off), SQUARED -- the
//           taper is an amplitude weight and every form below is quadratic.
//           CHANNEL-FREE, unlike |T|, so it survives off-template; its job is
//           to drop the baseline flanks, where the residual is pure noise.
//   E_c     = sum_t h T^2                 per-channel template energy
//   u_c     = E_c / max_c E_c in [0,1]    the on-channel weight: the central
//                                         median waveform's energy, per
//                                         channel, normalized to its own
//                                         strongest
//   Eon     = sum_c u_c E_c               the scale everything is read against
//   a       = sum_c u_c sum_t h x T / Eon the gain, fitted ON-channel only so
//                                         an intruder's off-channel energy
//                                         cannot inflate it and shrink its
//                                         own off-residual
//   q_c     = sum_t h (x - a T)^2         per-channel residual power
//   onr     = sum_c u_c       q_c / (a^2 Eon)
//   offr    = sum_c (1 - u_c) q_c / (a^2 Eon)
//
// Both are dimensionless and gain-free: 1 means "residual as large as the
// template", the same convention as detectWaveformOutliers's D.  u_c is a
// WEIGHT, not a hard channel split, so a flank channel at half the energy
// feeds both terms in proportion.  offr is normalized by the ON-channel
// energy on purpose — it asks "how much signal is there where this cell has
// none, relative to this cell's own size", which is the question, and it does
// not blow up on a channel whose own template energy is near zero.
//
// The two terms are COMPLEMENTARY and are thresholded SEPARATELY, their
// flagged sets unioned.  Held out on that unit (template from half of the
// curated cluster, scored on the other half against its siblings), AUC
// conditional on amplitude: on-channel 0.887, off-channel 0.791, half-against-
// half sanity 0.504-0.511.  Per sibling they come apart — one sibling is a
// shape difference (on 0.943, off lower), another is spatially displaced (off
// 0.786 while amplitude is blind at 0.522) — and their SUM scores 0.869,
// BELOW the on-channel term alone.  Summing dilutes whichever term carries
// the signal for that intruder, hence two tails, not one.
//
// Caveat, stated because it decides how to read a run: those are RANKING
// scores against labelled siblings.  This action has no labels — it cuts each
// term's robust tail, so what it flags is the tail of the unit's own
// distribution, which is the contamination only if the contamination is both
// a minority and far enough out.  Judge a run by looking at the product.
//
// Non-mutating, parent scope, same read path and median-template construction
// as detectWaveformOutliers.  (That construction is duplicated rather than
// shared: folding both onto one helper is the obvious follow-up, deliberately
// not done in the same patch that introduces an unproven metric, so a
// calibrated action is not touched by it.)
// ---------------------------------------------------------------------------
namespace {
// Robust one-sided upper tail: median + k * 1.4826 * MAD.  The residual terms
// are non-negative and right-skewed, so a symmetric sigma would misread them
// (the same reason detectWaveformOutliers uses this cut).  Takes its copy by
// value — nth_element reorders.  A degenerate (zero-MAD) distribution has no
// tail to cut and returns infinity, flagging nothing.
static double residRobustTail(std::vector<double> v, double kMad)
{
    if (v.empty()) return std::numeric_limits<double>::infinity();
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    const double med = v[mid];
    for (size_t i = 0; i < v.size(); ++i) v[i] = std::fabs(v[i] - med);
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    const double mad = v[mid];
    if (!(mad > 0.0)) return std::numeric_limits<double>::infinity();
    return med + kMad * 1.4826 * mad;
}

static double residMedian(std::vector<double> v)
{
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

// A median waveform and everything derived from it.  Built from the scored
// cluster's own spikes, or — when the curator designates a SEED, the clean core
// they have already carved — once from that cluster and reused for every scored
// one.  Which it is decides whether the statistic survives a mixture; see the
// breakdown note above detectResidualOutliers.
struct ResidTemplate {
    std::vector<float>  T;           ///< [nSel*nSamp] channel-major median waveform
    std::vector<double> h;           ///< [nSamp] squared amplitude taper
    std::vector<double> u;           ///< [nSel] on-channel weight, in [0,1]
    double Eon       = 0.0;          ///< sum_c u_c E_c — the scale every score is read against
    double offMargin = 0.0;          ///< sum_c (1-u_c) — what the off-channel term has to look at
    long   used      = 0;            ///< waveforms that went into the median
    bool   ok        = false;
};

static ResidTemplate residBuildTemplate(const Data::ClusteringSnapshot& spkSrc,
                                        const SortableTable& pos, long n,
                                        int nCh, int nSamp, const QList<int>& chans,
                                        int windowSamples,
                                        std::vector<int16_t>& raw,
                                        std::vector<float>& wav)
{
    ResidTemplate tpl;
    const int nSel = chans.size();
    const int P    = nSel * nSamp;
    if (nSel <= 0 || nSamp <= 0 || n <= 0) return tpl;

    // ── the per-point median over <=1024 evenly-strided spikes ──
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
    if (used < 8) return tpl;                     // too few readable waveforms
    tpl.used = used;
    tpl.T.assign(static_cast<size_t>(P), 0.0f);
    std::vector<float> col(static_cast<size_t>(used));
    for (int p = 0; p < P; ++p) {
        for (long i = 0; i < used; ++i)
            col[static_cast<size_t>(i)] = tplBuf[static_cast<size_t>(i) * P + p];
        std::nth_element(col.begin(), col.begin() + used / 2, col.end());
        tpl.T[static_cast<size_t>(p)] = col[static_cast<size_t>(used / 2)];
    }

    // ── h[t]: the temporal taper, centred on the template's peak ──
    // Pooled over the selected channels, so it lands on the dominant channel's
    // trough in practice.  Channel-free by construction: the whole point is a
    // weight that does NOT vanish off-template.
    //
    // h holds the taper SQUARED, because the taper belongs in the amplitude
    // domain -- it tapers the waveform, h_amp * x -- and every place it is used
    // below (h x T, h T^2, h r^2) is quadratic in amplitude.  Storing h_amp^2
    // once is the same statistic as tapering x and T and then squaring, which is
    // what the offline measurement this metric comes from did; storing h_amp
    // instead would be a DIFFERENT, untested weighting.
    tpl.h.assign(static_cast<size_t>(nSamp), 1.0);
    if (windowSamples > 0) {
        int tPeak = 0;
        double best = -1.0;
        for (int t = 0; t < nSamp; ++t) {
            double e = 0.0;
            for (int ci = 0; ci < nSel; ++ci) {
                const double v = tpl.T[static_cast<size_t>(ci) * nSamp + t];
                e += v * v;
            }
            if (e > best) { best = e; tPeak = t; }
        }
        constexpr double twoPi = 6.283185307179586;
        const int half = std::max(1, windowSamples / 2);
        for (int t = 0; t < nSamp; ++t) {
            const int d = t - tPeak;
            if (d < -half || d > half) { tpl.h[static_cast<size_t>(t)] = 0.0; continue; }
            const double x   = static_cast<double>(d + half) / (2.0 * half);
            const double amp = 0.54 - 0.46 * std::cos(twoPi * x);
            tpl.h[static_cast<size_t>(t)] = amp * amp;   // amplitude taper, squared
        }
    }

    // ── per-channel template energy E_c -> the on-channel weight u_c ──
    std::vector<double> Ec(static_cast<size_t>(nSel), 0.0);
    double maxEc = 0.0;
    for (int ci = 0; ci < nSel; ++ci) {
        double e = 0.0;
        for (int t = 0; t < nSamp; ++t) {
            const double v = tpl.T[static_cast<size_t>(ci) * nSamp + t];
            e += tpl.h[static_cast<size_t>(t)] * v * v;
        }
        Ec[static_cast<size_t>(ci)] = e;
        maxEc = std::max(maxEc, e);
    }
    if (!(maxEc > 0.0)) return tpl;               // flat template on the selection
    tpl.u.assign(static_cast<size_t>(nSel), 0.0);
    for (int ci = 0; ci < nSel; ++ci) {
        tpl.u[static_cast<size_t>(ci)] = Ec[static_cast<size_t>(ci)] / maxEc;
        tpl.Eon       += tpl.u[static_cast<size_t>(ci)] * Ec[static_cast<size_t>(ci)];
        tpl.offMargin += 1.0 - tpl.u[static_cast<size_t>(ci)];
    }
    if (!(tpl.Eon > 0.0)) return tpl;
    tpl.ok = true;
    return tpl;
}

// Score every spike of one cluster against a built template.  Appends in the
// cluster's own spike order; a spike that cannot be read is skipped entirely
// (no row, no score), exactly as detectWaveformOutliers skips a short read.
static void residScoreCluster(const Data::ClusteringSnapshot& spkSrc,
                              const SortableTable& pos, long n,
                              int nCh, int nSamp, const QList<int>& chans,
                              const ResidTemplate& tpl, double minGain,
                              std::vector<double>& onv, std::vector<double>& offv,
                              std::vector<long>& rows, std::vector<char>& noGain,
                              std::vector<int16_t>& raw, std::vector<float>& wav)
{
    const int nSel = chans.size();
    onv.clear(); offv.clear(); rows.clear(); noGain.clear();
    onv.reserve(static_cast<size_t>(n));
    offv.reserve(static_cast<size_t>(n));
    rows.reserve(static_cast<size_t>(n));
    noGain.reserve(static_cast<size_t>(n));
    for (long s = 0; s < n; ++s) {
        const long row = static_cast<long>(pos(1, s + 1));
        if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav)) continue;

        // a = sum_c u_c sum_t h x T / Eon  (Eon is that same sum with x=T).
        double num = 0.0;
        for (int ci = 0; ci < nSel; ++ci) {
            const float* xw = &wav[static_cast<size_t>(chans[ci]) * nSamp];
            const float* tp = &tpl.T[static_cast<size_t>(ci) * nSamp];
            double na = 0.0;
            for (int t = 0; t < nSamp; ++t)
                na += tpl.h[static_cast<size_t>(t)] * static_cast<double>(xw[t]) * tp[t];
            num += tpl.u[static_cast<size_t>(ci)] * na;
        }
        const double a = num / tpl.Eon;

        rows.push_back(row);
        if (!(a >= minGain)) {
            // No template-shaped signal, or anti-correlated with it: the ratios
            // divide by a^2 and would be meaningless (or infinite).  Flagged
            // outright and counted apart, rather than scored.
            onv.push_back(0.0);
            offv.push_back(0.0);
            noGain.push_back(1);
            continue;
        }
        noGain.push_back(0);

        double qOn = 0.0, qOff = 0.0;
        for (int ci = 0; ci < nSel; ++ci) {
            const float* xw = &wav[static_cast<size_t>(chans[ci]) * nSamp];
            const float* tp = &tpl.T[static_cast<size_t>(ci) * nSamp];
            double qc = 0.0;
            for (int t = 0; t < nSamp; ++t) {
                const double r = static_cast<double>(xw[t]) - a * tp[t];
                qc += tpl.h[static_cast<size_t>(t)] * r * r;
            }
            qOn  += tpl.u[static_cast<size_t>(ci)]         * qc;
            qOff += (1.0 - tpl.u[static_cast<size_t>(ci)]) * qc;
        }
        const double nrm = a * a * tpl.Eon;
        onv.push_back(qOn  / nrm);
        offv.push_back(qOff / nrm);
    }
}
}  // namespace

KlustersDoc::ResidualOutlierResult
KlustersDoc::detectResidualOutliers(const QList<int>& clusters,
                                    double            kOn,
                                    double            kOff,
                                    double            minGain,
                                    int               windowSamples,
                                    long              minSpikes,
                                    int               templateCluster,
                                    bool              thresholdFromTemplate)
{
    ResidualOutlierResult R;

    Data& layer = parentData();                     // parent scope (see header)
    const int nCh   = layer.nbOfChannels();
    const int nSamp = layer.nbOfSampleInWaveform();
    if (nCh <= 0 || nSamp <= 0) {
        R.reason = tr("The document carries no waveform geometry.");
        return R;
    }
    if (kOn <= 0.0 && kOff <= 0.0) {
        R.reason = tr("Both residual terms are off.  Set a tail multiplier "
                      "above zero for the on-channel term, the off-channel "
                      "term, or both.");
        return R;
    }
    QList<int> chans = selectedChannels();          // validated, sorted, unique
    if (chans.isEmpty())
        for (int c = 0; c < nCh; ++c) chans.append(c);

    const std::shared_ptr<const Data::ClusteringSnapshot> snap = layer.currentSnapshot();
    const Data::ClusteringSnapshot& spkSrc = *snap;

    std::vector<int16_t> raw;
    std::vector<float>   wav;
    std::vector<double>  onv, offv;
    std::vector<long>    rowv;
    std::vector<char>    noGain;
    const double inf = std::numeric_limits<double>::infinity();

    // ── the seed, when one is designated ────────────────────────────────
    // A designated seed changes BOTH halves of the gesture, and the second is
    // the one that matters on a dirty cluster: the template comes from the seed
    // (so the shape is the clean cell's, not the mixture's), and with
    // @p thresholdFromTemplate the CUT comes from the seed's own score
    // distribution too, so a mixture's inflated MAD cannot push the threshold
    // out from under the contamination.
    ResidTemplate seed;
    double seedThrOn = inf, seedThrOff = inf;
    QList<int> scored = clusters;
    if (templateCluster > 1) {
        scored.removeAll(templateCluster);          // the seed is never stripped
        SortableTable spos;
        if (!layer.spikePositions(templateCluster, spos)) {
            R.reason = tr("Template cluster %1 has no spike-position table.")
                           .arg(clusterLabel(templateCluster));
            return R;
        }
        const long sn = static_cast<long>(
            layer.nbOfSpikes(static_cast<dataType>(templateCluster)));
        if (sn < 8) {
            R.reason = tr("Template cluster %1 has only %2 spikes; at least 8 "
                          "are needed for a stable median waveform.")
                           .arg(clusterLabel(templateCluster)).arg(sn);
            return R;
        }
        seed = residBuildTemplate(spkSrc, spos, sn, nCh, nSamp, chans,
                                  windowSamples, raw, wav);
        if (!seed.ok) {
            R.reason = tr("Template cluster %1 gives no usable median waveform "
                          "on the selected channels (flat, or too few readable "
                          "waveforms).").arg(clusterLabel(templateCluster));
            return R;
        }
        if (thresholdFromTemplate) {
            residScoreCluster(spkSrc, spos, sn, nCh, nSamp, chans, seed, minGain,
                              onv, offv, rowv, noGain, raw, wav);
            std::vector<double> sOn, sOff;
            for (size_t i = 0; i < onv.size(); ++i) {
                if (noGain[i]) continue;
                sOn.push_back(onv[i]);
                sOff.push_back(offv[i]);
            }
            seedThrOn  = (kOn  > 0.0) ? residRobustTail(sOn,  kOn)  : inf;
            seedThrOff = (kOff > 0.0) ? residRobustTail(sOff, kOff) : inf;
            R.perCluster.append(
                tr("template %1: %2 spikes, median on=%3 off=%4, FIXED tail "
                   "on>%5 off>%6 applied to every scored cluster")
                    .arg(clusterLabel(templateCluster))
                    .arg(sOn.size())
                    .arg(residMedian(sOn),  0, 'g', 3)
                    .arg(residMedian(sOff), 0, 'g', 3)
                    .arg(seedThrOn  == inf ? QStringLiteral("off")
                                           : QString::number(seedThrOn,  'g', 3))
                    .arg(seedThrOff == inf ? QStringLiteral("off")
                                           : QString::number(seedThrOff, 'g', 3)));
        }
        R.templateId = templateCluster;
        if (seed.offMargin > 0.25) R.anyOffMargin = true;
        if (scored.isEmpty()) {
            R.reason = tr("Nothing to score: cluster %1 is the template and no "
                          "other cluster is selected.")
                           .arg(clusterLabel(templateCluster));
            return R;
        }
    }

    for (int c : scored) {
        if (c < 2) continue;                        // never strip artefact / noise
        SortableTable pos;
        if (!layer.spikePositions(c, pos)) continue;
        const long n = static_cast<long>(layer.nbOfSpikes(static_cast<dataType>(c)));
        if (n < minSpikes) continue;                // MAD too noisy below the floor

        // The seed's template, or the cluster's own.  Without a seed this is
        // exactly the self-referential form: the cluster supplies both the
        // shape it is judged against and the spread it is judged by.
        const ResidTemplate own =
            seed.ok ? ResidTemplate()
                    : residBuildTemplate(spkSrc, pos, n, nCh, nSamp, chans,
                                         windowSamples, raw, wav);
        const ResidTemplate& tpl = seed.ok ? seed : own;
        if (!tpl.ok) continue;
        // Off-channel margin: with only the template's own channel(s) selected
        // every u_c is ~1, offr is identically ~0, and its tail cuts nothing.
        // Recorded so the caller can say so instead of reporting a silent zero.
        if (tpl.offMargin > 0.25) R.anyOffMargin = true;

        residScoreCluster(spkSrc, pos, n, nCh, nSamp, chans, tpl, minGain,
                          onv, offv, rowv, noGain, raw, wav);
        if (rowv.size() < static_cast<size_t>(minSpikes)) continue;

        // ── a robust tail per term, over the SCORED spikes only ──
        // The no-gain spikes are already flagged; leaving them in would drag
        // the median and MAD of distributions they are not members of.
        std::vector<double> onOk, offOk;
        onOk.reserve(onv.size());
        offOk.reserve(offv.size());
        for (size_t i = 0; i < onv.size(); ++i) {
            if (noGain[i]) continue;
            onOk.push_back(onv[i]);
            offOk.push_back(offv[i]);
        }
        const bool fixedTail = (seedThrOn != inf || seedThrOff != inf);
        const double thrOn  = fixedTail ? seedThrOn
                                        : ((kOn  > 0.0) ? residRobustTail(onOk,  kOn)  : inf);
        const double thrOff = fixedTail ? seedThrOff
                                        : ((kOff > 0.0) ? residRobustTail(offOk, kOff) : inf);

        QVector<ResidualFlag> flagged;
        long cOn = 0, cOff = 0, cBoth = 0, cNo = 0;
        for (size_t i = 0; i < rowv.size(); ++i) {
            ResidualFlag f;
            f.index  = static_cast<int>(rowv[i]) - 1;     // -> 0-based .spk index
            f.noGain = (noGain[i] != 0);
            if (!f.noGain) {
                f.on  = onv[i]  > thrOn;
                f.off = offv[i] > thrOff;
            }
            if (!(f.on || f.off || f.noGain)) continue;
            flagged.append(f);
            if (f.noGain)           ++cNo;
            else if (f.on && f.off) ++cBoth;
            else if (f.on)          ++cOn;
            else                    ++cOff;
        }

        R.perCluster.append(
            tr("%1: %2 scored, median on=%3 off=%4, tail on>%5 off>%6 "
               "-> %7 flagged (%8 on, %9 off, %10 both, %11 no-gain)")
                .arg(clusterLabel(c))
                .arg(onOk.size())
                .arg(residMedian(onOk),  0, 'g', 3)
                .arg(residMedian(offOk), 0, 'g', 3)
                .arg(thrOn  == inf ? QStringLiteral("off") : QString::number(thrOn,  'g', 3))
                .arg(thrOff == inf ? QStringLiteral("off") : QString::number(thrOff, 'g', 3))
                .arg(flagged.size()).arg(cOn).arg(cOff).arg(cBoth).arg(cNo));

        if (!flagged.isEmpty()) {
            R.total   += flagged.size();
            R.nOn     += cOn;
            R.nOff    += cOff;
            R.nBoth   += cBoth;
            R.nNoGain += cNo;
            R.byCluster.insert(c, flagged);
        }
        ++R.scored;
    }

    R.ok = true;
    R.offBlind = (kOff > 0.0 && !R.anyOffMargin);
    if (R.scored == 0)
        R.reason = tr("Strip residual outliers: no selected cluster carries "
                      "enough spikes (>= %1) for a stable median waveform.")
                       .arg(minSpikes);
    else if (R.offBlind)
        R.reason = tr("The channel selection carries no off-template margin — "
                      "every selected channel holds nearly all of the "
                      "template's energy, so the off-channel term has nothing "
                      "to look at.  Clear the channel selection (or add the "
                      "quiet channels) to use it.");
    return R;
}



// ---------------------------------------------------------------------------
// computeResidualPreview — waveform-space residual of a prospective lasso cut.
//
// Companion to the post-lasso preview.  For the feature rows just lassoed in the
// embedding view, read their .spk waveforms (sampled to maxSpikes) and fit each
// to the basis spanned by the basis clusters' MEAN waveforms, a = (BᵀB)⁻¹Bᵀx
// (ridge-stabilised).  The residual x − B·a is what the basis cannot explain: a
// mixture or single of the basis cells collapses to the noise floor (flat mean
// residual), a distinct unit keeps its own template.  Empty basis -> residual to
// the lassoed spikes' OWN mean (reports the group's spread).  Same snapshot +
// tmReadSpikeFloat read path as detectWaveformOutliers; parent scope; traces are
// over ALL channels (channel-major) so the waveform view overlays them 1:1.
// ---------------------------------------------------------------------------
namespace {
// Invert a small K×K matrix in place by Gauss–Jordan (K = basis size, tiny).
static bool smallInverse(std::vector<double>& A, int K) {
    std::vector<double> I(static_cast<size_t>(K) * K, 0.0);
    for (int i = 0; i < K; ++i) I[static_cast<size_t>(i) * K + i] = 1.0;
    for (int c = 0; c < K; ++c) {
        const double piv = A[static_cast<size_t>(c) * K + c];
        if (std::fabs(piv) < 1e-12) return false;
        const double inv = 1.0 / piv;
        for (int j = 0; j < K; ++j) { A[static_cast<size_t>(c)*K+j] *= inv; I[static_cast<size_t>(c)*K+j] *= inv; }
        for (int r = 0; r < K; ++r) {
            if (r == c) continue;
            const double f = A[static_cast<size_t>(r)*K+c];
            if (f == 0.0) continue;
            for (int j = 0; j < K; ++j) {
                A[static_cast<size_t>(r)*K+j] -= f * A[static_cast<size_t>(c)*K+j];
                I[static_cast<size_t>(r)*K+j] -= f * I[static_cast<size_t>(c)*K+j];
            }
        }
    }
    A.swap(I);
    return true;
}
} // namespace

KlustersDoc::ResidualPreview
KlustersDoc::computeResidualPreview(const QSet<dataType>& featureRows,
                                    const QList<int>&     basisClusters,
                                    long                  maxSpikes)
{
    ResidualPreview R;
    Data& layer = parentData();
    const int nCh   = layer.nbOfChannels();
    const int nSamp = layer.nbOfSampleInWaveform();
    if (nCh <= 0 || nSamp <= 0 || featureRows.isEmpty()) {
        R.verdict = tr("Residual preview: no waveform geometry or no spikes selected.");
        return R;
    }
    const int P = nCh * nSamp;                          // ALL channels, channel-major
    const std::shared_ptr<const Data::ClusteringSnapshot> snap = layer.currentSnapshot();
    const Data::ClusteringSnapshot& spkSrc = *snap;
    std::vector<int16_t> raw;
    std::vector<float>   wav;

    // ---- basis templates B (K columns, each P long): mean .spk waveform ----
    std::vector<std::vector<double>> B;
    for (int bc : basisClusters) {
        if (bc <= 1) continue;                          // reserve bins carry no template
        SortableTable pos;
        if (!layer.spikePositions(bc, pos)) continue;
        const long n = static_cast<long>(layer.nbOfSpikes(static_cast<dataType>(bc)));
        if (n <= 0) continue;
        const long step = std::max(1L, n / 1024L);
        std::vector<double> acc(static_cast<size_t>(P), 0.0);
        long used = 0;
        for (long s = 0; s < n; s += step) {
            const long row = static_cast<long>(pos(1, s + 1));
            if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav)) continue;
            for (int p = 0; p < P; ++p) acc[static_cast<size_t>(p)] += wav[static_cast<size_t>(p)];
            ++used;
        }
        if (used < 1) continue;
        for (int p = 0; p < P; ++p) acc[static_cast<size_t>(p)] /= static_cast<double>(used);
        B.push_back(std::move(acc));
    }
    int K = static_cast<int>(B.size());

    // ---- Gram inverse (K×K, ridge-stabilised) for the least-squares fit ----
    std::vector<double> Ginv;
    if (K >= 1) {
        std::vector<double> G(static_cast<size_t>(K) * K, 0.0);
        double trace = 0.0;
        for (int a = 0; a < K; ++a)
            for (int b = 0; b < K; ++b) {
                double s = 0.0;
                for (int p = 0; p < P; ++p) s += B[a][p] * B[b][p];
                G[static_cast<size_t>(a)*K+b] = s;
                if (a == b) trace += s;
            }
        const double ridge = 1e-6 * (trace / std::max(1, K)) + 1e-9;
        for (int a = 0; a < K; ++a) G[static_cast<size_t>(a)*K+a] += ridge;
        if (smallInverse(G, K)) Ginv = std::move(G);
    }
    const bool basisOK = (K >= 1) && !Ginv.empty();
    if (!basisOK) { B.clear(); K = 0; }

    // ---- the lassoed rows, sampled to maxSpikes ----
    std::vector<dataType> rows(featureRows.begin(), featureRows.end());
    const long nL = static_cast<long>(rows.size());
    const long lstep = std::max(1L, nL / std::max(1L, maxSpikes));

    // Own-mean fallback needs the lassoed mean before it can fit; prepass for it.
    std::vector<double> ownMean;
    if (!basisOK) {
        std::vector<double> sx(static_cast<size_t>(P), 0.0); long u = 0;
        for (long i = 0; i < nL; i += lstep) {
            const long row = static_cast<long>(rows[static_cast<size_t>(i)]);
            if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav)) continue;
            for (int p = 0; p < P; ++p) sx[static_cast<size_t>(p)] += wav[static_cast<size_t>(p)];
            ++u;
        }
        if (u < 1) { R.verdict = tr("Residual preview: no readable waveforms."); return R; }
        ownMean.resize(static_cast<size_t>(P));
        for (int p = 0; p < P; ++p) ownMean[static_cast<size_t>(p)] = sx[static_cast<size_t>(p)] / static_cast<double>(u);
    }

    std::vector<double> sumX(P,0.0), sumFit(P,0.0), sumR(P,0.0), sumR2(P,0.0);
    std::vector<double> rnorms; rnorms.reserve(static_cast<size_t>(maxSpikes));
    long nUsed = 0;
    for (long i = 0; i < nL; i += lstep) {
        const long row = static_cast<long>(rows[static_cast<size_t>(i)]);
        if (!tmReadSpikeFloat(spkSrc, row - 1, nCh, nSamp, raw, wav)) continue;
        std::vector<double> fit(static_cast<size_t>(P), 0.0);
        if (basisOK) {
            std::vector<double> bt(static_cast<size_t>(K), 0.0);
            for (int k = 0; k < K; ++k) {
                double s = 0.0;
                for (int p = 0; p < P; ++p) s += B[k][p] * wav[static_cast<size_t>(p)];
                bt[static_cast<size_t>(k)] = s;
            }
            for (int k = 0; k < K; ++k) {
                double a = 0.0;
                for (int j = 0; j < K; ++j) a += Ginv[static_cast<size_t>(k)*K+j] * bt[static_cast<size_t>(j)];
                for (int p = 0; p < P; ++p) fit[static_cast<size_t>(p)] += a * B[k][p];
            }
        } else {
            double num = 0.0, den = 0.0;
            for (int p = 0; p < P; ++p) { num += ownMean[static_cast<size_t>(p)]*wav[static_cast<size_t>(p)]; den += ownMean[static_cast<size_t>(p)]*ownMean[static_cast<size_t>(p)]; }
            const double a = (den > 0.0) ? num/den : 0.0;
            for (int p = 0; p < P; ++p) fit[static_cast<size_t>(p)] = a * ownMean[static_cast<size_t>(p)];
        }
        double rn2 = 0.0;
        for (int p = 0; p < P; ++p) {
            const double x = wav[static_cast<size_t>(p)], r = x - fit[static_cast<size_t>(p)];
            sumX[static_cast<size_t>(p)]+=x; sumFit[static_cast<size_t>(p)]+=fit[static_cast<size_t>(p)]; sumR[static_cast<size_t>(p)]+=r; sumR2[static_cast<size_t>(p)]+=r*r;
            rn2 += r*r;
        }
        rnorms.push_back(std::sqrt(rn2));
        ++nUsed;
    }
    if (nUsed < 1) { R.verdict = tr("Residual preview: no readable waveforms."); return R; }

    R.meanWave.resize(static_cast<size_t>(P));
    R.fit.resize(static_cast<size_t>(P));
    R.resid.resize(static_cast<size_t>(P));
    R.residRms.resize(static_cast<size_t>(P));
    for (int p = 0; p < P; ++p) {
        R.meanWave[static_cast<size_t>(p)] = static_cast<float>(sumX[static_cast<size_t>(p)]  / nUsed);
        R.fit[static_cast<size_t>(p)]      = static_cast<float>(sumFit[static_cast<size_t>(p)] / nUsed);
        R.resid[static_cast<size_t>(p)]    = static_cast<float>(sumR[static_cast<size_t>(p)]  / nUsed);
        R.residRms[static_cast<size_t>(p)] = static_cast<float>(std::sqrt(sumR2[static_cast<size_t>(p)] / nUsed));
    }
    std::sort(rnorms.begin(), rnorms.end());
    R.medResidNorm = rnorms[rnorms.size() / 2];
    for (int p = 0; p < P; ++p) {
        R.peakWave  = std::max(R.peakWave,  std::fabs(static_cast<double>(R.meanWave[static_cast<size_t>(p)])));
        R.peakResid = std::max(R.peakResid, std::fabs(static_cast<double>(R.resid[static_cast<size_t>(p)])));
    }
    R.ok = true; R.nChan = nCh; R.nSamp = nSamp; R.nUsed = nUsed;
    for (int bc : basisClusters) if (bc > 1 && basisOK) R.basis.append(bc);

    const double frac = (R.peakWave > 0.0) ? R.peakResid / R.peakWave : 0.0;
    if (!basisOK) {
        R.verdict = tr("Residual preview: %1 spikes, no basis — residual to own mean; "
                       "median spread %2 (tightness of the prospective cluster) — "
                       "Enter to apply, Esc to cancel")
                        .arg(nUsed).arg(R.medResidNorm, 0, 'f', 0);
    } else {
        const QStringList b = clusterLabels(R.basis);
        const QString read = (frac < 0.10)
            ? tr("explained by the basis (~mix/single, low residual)")
            : (frac < 0.25) ? tr("partly explained, some residual structure")
                            : tr("DISTINCT structure, a separate unit");
        R.verdict = tr("Residual vs basis [%1]: %2  (peak residual %3% of the waveform, "
                       "%4 spikes) — Enter to apply, Esc to cancel")
                        .arg(b.join(QStringLiteral(",")))
                        .arg(read)
                        .arg(frac * 100.0, 0, 'f', 0)
                        .arg(nUsed);
    }
    return R;
}
