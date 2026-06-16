#include "residualmatrixthread.h"
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

    // ── 1. Cluster list (skip artefact 0; keep noise 1 so it can be read) ──
    {
        const QList<dataType> allIds = data.clusterIds();
        for (dataType id : allIds)
            if (id >= 1 && data.nbOfSpikes(id) > 0)
                clusterList.append(static_cast<int>(id));
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

    // ── 4. Asymmetric residual matrix ─────────────────────────────────────
    //   M(i,j) = mean_p ( var_i[p] + (mean_i[p] − mean_j[p])^2 )
    // Diagonal M(i,i) = mean_p var_i[p] (within-cluster noise floor).
    scores = new Array<double>();
    scores->setSize(nClusters, nClusters);

    const double invPts = 1.0 / static_cast<double>(nPts);

    // Per-cluster mean variance (the diagonal, and the var_i offset added to
    // every cell in row i).
    std::vector<double> meanVar(static_cast<size_t>(nClusters), 0.0);
    for (int ci = 0; ci < nClusters; ++ci) {
        double s = 0.0;
        const auto& v = varWav[static_cast<size_t>(ci)];
        for (int p = 0; p < nPts; ++p) s += v[static_cast<size_t>(p)];
        meanVar[static_cast<size_t>(ci)] = s * invPts;
    }
    for (int i = 0; i < nClusters; ++i)
        (*scores)(i + 1, i + 1) = meanVar[static_cast<size_t>(i)];

    // Symmetric squared-template-gap per unordered pair, computed once; the
    // two directed cells differ only by the var_i / var_j offset.
    std::vector<std::pair<int,int>> pairs;
    pairs.reserve(static_cast<size_t>(nClusters * (nClusters - 1) / 2));
    for (int i = 0; i < nClusters; ++i)
        for (int j = i + 1; j < nClusters; ++j)
            pairs.emplace_back(i, j);
    const int nPairs = static_cast<int>(pairs.size());

#pragma omp parallel for schedule(dynamic,4) default(none) \
    shared(meanWav, meanVar, pairs, scores) \
    firstprivate(nPairs, nPts, invPts)
    for (int pi = 0; pi < nPairs; ++pi) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;
        const int i = pairs[static_cast<size_t>(pi)].first;
        const int j = pairs[static_cast<size_t>(pi)].second;
        const auto& mi = meanWav[static_cast<size_t>(i)];
        const auto& mj = meanWav[static_cast<size_t>(j)];
        double gap = 0.0;
        for (int p = 0; p < nPts; ++p) {
            const double d = static_cast<double>(mi[static_cast<size_t>(p)])
                           - static_cast<double>(mj[static_cast<size_t>(p)]);
            gap += d * d;
        }
        gap *= invPts;                               // mean_p (mean_i − mean_j)^2
        (*scores)(i + 1, j + 1) = meanVar[static_cast<size_t>(i)] + gap;  // var_i + gap
        (*scores)(j + 1, i + 1) = meanVar[static_cast<size_t>(j)] + gap;  // var_j + gap
    }

    post();
}
