#include "residualmatrixthread.h"
#include "channelmask.h"
#include "residualmatrixview.h"
#include "templatematrixthread.h"   // shared tmReadSpikeFloat
#include "sortabletable.h"

#include <QApplication>
#include <cmath>
#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif

// ---------------------------------------------------------------------------
void ResidualMatrixThread::run()
{
    auto post = [this]() {
        QApplication::postEvent(&view, new ResidualMatrixEvent(*this));
    };

    if (haveToStopProcessing) { post(); return; }

    // ── 1. Cluster list ──────────────────────────────────────────────────
    {
        const QList<dataType> allIds = data.clusterIds();
        if (!activeClusters.isEmpty()) {
            // Scoped: build the list DIRECTLY as [artefact, noise, children...]
            // rather than building every cluster and filtering after.  The matrix
            // is sized from this list, so what goes in it is the matrix -- a
            // parent with 7 children gives 9x9.
            //
            // Both reserve bins are included even though the unscoped build below
            // skips artefact.  Skipping it there is a judgement about a
            // session-wide matrix, where an artefact row carries no waveform and
            // costs a row out of hundreds.  Here the matrix is nine rows and the
            // artefact bin is one of the places a child can be sent, so its column
            // is part of what the view is for.  Included regardless of spike count,
            // so the shape does not change under the user as bins empty and fill.
            for (dataType id : allIds)
                if (id <= 1) clusterList.append(static_cast<int>(id));
            for (int id : activeClusters)
                if (id > 1 && data.nbOfSpikes(id) > 0) clusterList.append(id);
        } else {
            for (dataType id : allIds)
                if (id >= 1 && data.nbOfSpikes(id) > 0)
                    clusterList.append(static_cast<int>(id));
        }
        std::sort(clusterList.begin(), clusterList.end());
    }


    const int nClusters = clusterList.size();
    if (nClusters < 2) { post(); return; }

    const int     nChan   = data.nbOfChannels();
    const int     nSamp   = data.nbSamplesPerWaveform();
    const int     nPts    = nChan * nSamp;
    const QString spkPath = data.getSpkFileName();
    if (spkPath.isEmpty() || nPts <= 0) { post(); return; }

    // ── 2. Pre-fetch .spk file indices (serial, mutex-safe) ───────────────
    allFileIdx.resize(static_cast<size_t>(nClusters));
    for (int ci = 0; ci < nClusters; ++ci) {
        if (haveToStopProcessing) { post(); return; }
        SortableTable posTable;
        if (!data.spikePositions(clusterList[ci], posTable)) continue;
        const long nSpk = static_cast<long>(data.nbOfSpikes(clusterList[ci]));
        allFileIdx[static_cast<size_t>(ci)].reserve(static_cast<size_t>(nSpk));
        for (long s = 0; s < nSpk; ++s)
            allFileIdx[static_cast<size_t>(ci)].push_back(
                static_cast<int>(posTable(1, s + 1)) - 1);
    }
    if (haveToStopProcessing) { post(); return; }

    // ── 3. Per-cluster mean + within-cluster variance (one streaming pass) ─
    // mean_c[p] = (1/N) Σ x_s[p];  var_c[p] = (1/N) Σ x_s[p]^2 − mean_c[p]^2.
    std::vector<std::vector<float>> meanWav(
        static_cast<size_t>(nClusters),
        std::vector<float>(static_cast<size_t>(nPts), 0.0f));
    std::vector<std::vector<float>> varWav(
        static_cast<size_t>(nClusters),
        std::vector<float>(static_cast<size_t>(nPts), 0.0f));

    const QByteArray spkBytes = spkPath.toLocal8Bit();
    const char*      spkCStr  = spkBytes.constData();

#pragma omp parallel for schedule(dynamic,1) default(none) \
    shared(meanWav, varWav, allFileIdx) \
    firstprivate(nClusters, nPts, nChan, nSamp, spkCStr)
    for (int ci = 0; ci < nClusters; ++ci) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;

        const auto& fidx = allFileIdx[static_cast<size_t>(ci)];
        const long  nSpk = static_cast<long>(fidx.size());
        if (nSpk == 0) continue;

        FILE* spk = fopen(spkCStr, "rb");
        if (!spk) continue;

        std::vector<double>  acc (static_cast<size_t>(nPts), 0.0);
        std::vector<double>  acc2(static_cast<size_t>(nPts), 0.0);
        std::vector<int16_t> raw;
        std::vector<float>   sp;
        long valid = 0;

        for (long s = 0; s < nSpk; ++s) {
            if (haveToStopProcessing.load(std::memory_order_relaxed)) break;
            if (!tmReadSpikeFloat(spk, fidx[static_cast<size_t>(s)],
                                  nChan, nSamp, raw, sp))
                continue;
            for (int p = 0; p < nPts; ++p) {
                const double v = sp[static_cast<size_t>(p)];
                acc [static_cast<size_t>(p)] += v;
                acc2[static_cast<size_t>(p)] += v * v;
            }
            ++valid;
        }
        fclose(spk);

        if (valid > 0) {
            const double inv = 1.0 / static_cast<double>(valid);
            for (int p = 0; p < nPts; ++p) {
                const double m  = acc[static_cast<size_t>(p)] * inv;
                const double m2 = acc2[static_cast<size_t>(p)] * inv;
                meanWav[static_cast<size_t>(ci)][static_cast<size_t>(p)] =
                    static_cast<float>(m);
                // Clamp tiny negatives from round-off to 0.
                varWav[static_cast<size_t>(ci)][static_cast<size_t>(p)] =
                    static_cast<float>(std::max(0.0, m2 - m * m));
            }
        }
    }
    if (haveToStopProcessing) { post(); return; }

    // ── 4. Asymmetric separability matrix ─────────────────────────────────────
    //   noise_i  = mean_p var_i[p]                    (within-cluster floor)
    //   gap(i,j) = mean_p (mean_i[p] − mean_j[p])^2     (squared template gap)
    //   M(i,j)   = gap(i,j) / (noise_i + gap(i,j))     in [0,1)
    //
    // M is the fraction of the residual of i's spikes about j's template that is
    // SYSTEMATIC (template difference) rather than noise -- a bounded
    // discriminability index: 0 => i is indistinguishable from j's template given
    // i's own noise (a merge candidate), ->1 => clearly distinct.  The raw residual
    // noise_i + gap is the expected squared residual, but it is dominated by gap
    // for distinct clusters, so a single far pair set the colour scale and squashed
    // every mergeable pair into the first bin.  Bounding makes distinct pairs
    // saturate near 1, freeing the low end for merge candidates.  M(i,i) keeps the
    // raw within-cluster variance (noise_i): the diagonal is drawn black and
    // excluded from the colour scale, so the value is kept only for the hover.
    scores = new Array<double>();
    scores->setSize(nClusters, nClusters);

    // ── Restrict to the selected channels ──────────────────────────────────
    // After the means/variances are built: the .spk read is the expensive part
    // and identical either way, so this costs a memcpy.  meanWav/varWav are
    // locals here (nothing outside this thread sees them), so compact in place.
    // Compacted out rather than zeroed — see channelmask.h.
    int effChan = nChan;
    {
        std::vector<int> sel;
        sel.reserve(static_cast<size_t>(selection.size()));
        for (int c : selection) sel.push_back(c);
        const std::vector<int> keep = cmResolveMask(sel, nChan);
        if (!keep.empty()) {
            std::vector<float> tmp;
            for (auto& m : meanWav) { cmCompactChannels(m, nChan, nSamp, keep, tmp); m.swap(tmp); }
            for (auto& v : varWav)  { cmCompactChannels(v, nChan, nSamp, keep, tmp); v.swap(tmp); }
            effChan = static_cast<int>(keep.size());
        }
    }
    const int effPts = effChan * nSamp;
    if (effPts <= 0) { post(); return; }

    const double invPts = 1.0 / static_cast<double>(effPts);

    // Per-cluster mean variance (the diagonal, and the var_i offset added to
    // every cell in row i).
    std::vector<double> meanVar(static_cast<size_t>(nClusters), 0.0);
    for (int ci = 0; ci < nClusters; ++ci) {
        double s = 0.0;
        const auto& v = varWav[static_cast<size_t>(ci)];
        for (int p = 0; p < effPts; ++p) s += v[static_cast<size_t>(p)];
        meanVar[static_cast<size_t>(ci)] = s * invPts;
    }
    for (int i = 0; i < nClusters; ++i)
        (*scores)(i + 1, i + 1) = meanVar[static_cast<size_t>(i)];

    // Squared template gap is symmetric, computed once per unordered pair; the
    // two directed cells share it but each normalises by its own row floor.
    std::vector<std::pair<int,int>> pairs;
    pairs.reserve(static_cast<size_t>(nClusters * (nClusters - 1) / 2));
    for (int i = 0; i < nClusters; ++i)
        for (int j = i + 1; j < nClusters; ++j)
            pairs.emplace_back(i, j);
    const int nPairs = static_cast<int>(pairs.size());

#pragma omp parallel for schedule(dynamic,4) default(none) \
    shared(meanWav, meanVar, pairs, scores) \
    firstprivate(nPairs, effPts, invPts)
    for (int pi = 0; pi < nPairs; ++pi) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;
        const int i = pairs[static_cast<size_t>(pi)].first;
        const int j = pairs[static_cast<size_t>(pi)].second;
        const auto& mi = meanWav[static_cast<size_t>(i)];
        const auto& mj = meanWav[static_cast<size_t>(j)];
        double gap = 0.0;
        for (int p = 0; p < effPts; ++p) {
            const double d = static_cast<double>(mi[static_cast<size_t>(p)])
                           - static_cast<double>(mj[static_cast<size_t>(p)]);
            gap += d * d;
        }
        gap *= invPts;                               // mean_p (mean_i − mean_j)^2
        const double di = meanVar[static_cast<size_t>(i)] + gap;
        const double dj = meanVar[static_cast<size_t>(j)] + gap;
        (*scores)(i + 1, j + 1) = (di > 0.0) ? gap / di : 0.0;   // systematic fraction, row i
        (*scores)(j + 1, i + 1) = (dj > 0.0) ? gap / dj : 0.0;   // systematic fraction, row j
    }

    post();
}
