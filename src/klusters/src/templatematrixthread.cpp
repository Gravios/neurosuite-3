#include "templatematrixthread.h"
#include "templatematrixview.h"
#include "sortabletable.h"
#include "configuration.h"

#include <QApplication>
#include <cmath>
#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif

// ---------------------------------------------------------------------------
// Shared .spk reader — see templatematrixthread.h.  int16 on disk, channel-major
// float out.  No 2-vs-4-byte branch: the toolchain's extractor writes int16.
// ---------------------------------------------------------------------------
bool tmReadSpikeFloat(FILE* spk, long fileIdx0, int nChan, int nSamp,
                      std::vector<int16_t>& rawScratch,
                      std::vector<float>& out)
{
    const size_t nPts = static_cast<size_t>(nChan) * static_cast<size_t>(nSamp);
    if (nPts == 0) return false;
    if (rawScratch.size() < nPts) rawScratch.resize(nPts);
    if (out.size() < nPts) out.resize(nPts);

    const off_t off = static_cast<off_t>(fileIdx0)
                    * static_cast<off_t>(nPts)
                    * static_cast<off_t>(sizeof(int16_t));
    if (fseeko(spk, off, SEEK_SET) != 0) return false;
    if (std::fread(rawScratch.data(), sizeof(int16_t), nPts, spk) != nPts)
        return false;

    for (int ch = 0; ch < nChan; ++ch)
        for (int sm = 0; sm < nSamp; ++sm)
            out[static_cast<size_t>(ch * nSamp + sm)] =
                static_cast<float>(
                    rawScratch[static_cast<size_t>(sm * nChan + ch)]);
    return true;
}

// ---------------------------------------------------------------------------
// Shared xcorr implementation (also used by PairXcorrThread).
// ---------------------------------------------------------------------------
float tmNormXcorr(const std::vector<float>& a,
                  const std::vector<float>& b,
                  int maxShift,
                  bool meanSubtract)
{
    const int N = static_cast<int>(a.size());
    if (N == 0) return 0.0f;

    float best = 0.0f;
    for (int lag = -maxShift; lag <= maxShift; ++lag) {
        // One pass over the overlapping window: dot product, both squared
        // norms, and (for Pearson) both plain sums so the per-window means can
        // be removed via the centring identity without a second pass.  Using
        // the full-length norms here (the old behaviour) penalised off-centre
        // lags, where the numerator sums over fewer terms.
        double sab = 0.0, saa = 0.0, sbb = 0.0, sa = 0.0, sb = 0.0;
        int cnt = 0;
        for (int i = 0; i < N; ++i) {
            int j = i + lag;
            if (j < 0 || j >= N) continue;
            const double ai = a[i], bj = b[j];
            sab += ai * bj; saa += ai * ai; sbb += bj * bj;
            sa  += ai;      sb  += bj;      ++cnt;
        }
        if (cnt == 0) continue;

        double xcorr = sab, normA = saa, normB = sbb;
        if (meanSubtract) {
            // Pearson: remove the overlap-window mean of each waveform via
            //   Σxy − (Σx)(Σy)/n,  Σx² − (Σx)²/n,  Σy² − (Σy)²/n.
            xcorr = sab - sa * sb / cnt;
            normA = saa - sa * sa / cnt;
            normB = sbb - sb * sb / cnt;
        }
        const double denom = std::sqrt(normA * normB);
        if (denom < 1e-12) continue;
        float val = static_cast<float>(std::abs(xcorr) / denom);
        if (val > best) best = val;
    }
    return best;
}

// ---------------------------------------------------------------------------
// Raw (non-normalised) peak cross-correlation: max over lags of |Σ a_i b_j|.
// No norm division — the value scales with waveform energy/amplitude, so two
// clusters with the same shape but different amplitude score differently
// (unlike cosine/Pearson, which are amplitude-invariant).  Unbounded; the
// matrix thread maps the resulting values onto [0,1] for display.
// ---------------------------------------------------------------------------
float tmRawXcorr(const std::vector<float>& a,
                 const std::vector<float>& b,
                 int maxShift)
{
    const int N = static_cast<int>(a.size());
    if (N == 0) return 0.0f;

    float best = 0.0f;
    for (int lag = -maxShift; lag <= maxShift; ++lag) {
        double sab = 0.0;
        for (int i = 0; i < N; ++i) {
            const int j = i + lag;
            if (j < 0 || j >= N) continue;
            sab += static_cast<double>(a[i]) * static_cast<double>(b[j]);
        }
        const float val = static_cast<float>(std::abs(sab));
        if (val > best) best = val;
    }
    return best;
}

// ---------------------------------------------------------------------------
void TemplateMatrixThread::run()
{
    auto post = [this]() {
        QApplication::postEvent(&view, new TemplateMatrixEvent(*this));
    };

    if (haveToStopProcessing) { post(); return; }

    // ── 1. Build cluster list (skip noise 0 and artefact 1) ──────────────
    {
        const QList<dataType> allIds = data.clusterIds();
        for (dataType id : allIds)
            // Include noise cluster (id=1) so its spikes can be compared
            // and selectively moved back to real unit clusters.
            // Skip artefact cluster (id=0) — no meaningful waveform.
            if (id >= 1 && data.nbOfSpikes(id) > 0)
                clusterList.append(static_cast<int>(id));
        std::sort(clusterList.begin(), clusterList.end());
    }

    const int nClusters = clusterList.size();
    if (nClusters < 2) { post(); return; }

    const int    nChan    = data.nbOfChannels();
    const int    nSamp    = data.nbSamplesPerWaveform();
    const int    nPts     = nChan * nSamp;
    const int    maxShift = std::max(1, nSamp / 4);
    const QString spkPath = data.getSpkFileName();

    if (spkPath.isEmpty() || nPts <= 0) { post(); return; }

    // ── 2. Pre-fetch spike file indices (serial, mutex-safe) ─────────────
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

    // ── 3. Parallel waveform reading — one FILE* per cluster/thread ──────
    // Compute mean waveforms only.  Per-spike waveforms are NOT retained;
    // they are re-read on demand by PairXcorrThread when the user selects a cell.
    meanWav.assign(static_cast<size_t>(nClusters),
                   std::vector<float>(static_cast<size_t>(nPts), 0.0f));

    const QByteArray spkBytes = spkPath.toLocal8Bit();
    const char*      spkCStr  = spkBytes.constData();

#pragma omp parallel for schedule(dynamic,1) default(none) \
    shared(meanWav, allFileIdx) \
    firstprivate(nClusters, nPts, nChan, nSamp, spkCStr)
    for (int ci = 0; ci < nClusters; ++ci) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;

        const auto& fidx = allFileIdx[static_cast<size_t>(ci)];
        const long  nSpk = static_cast<long>(fidx.size());
        if (nSpk == 0) continue;

        FILE* spk = fopen(spkCStr, "rb");
        if (!spk) continue;

        std::vector<double>  acc(static_cast<size_t>(nPts), 0.0);
        std::vector<int16_t> raw;
        std::vector<float>   sp;
        long valid = 0;

        for (long s = 0; s < nSpk; ++s) {
            if (haveToStopProcessing.load(std::memory_order_relaxed)) break;
            if (!tmReadSpikeFloat(spk, fidx[static_cast<size_t>(s)],
                                  nChan, nSamp, raw, sp))
                continue;
            for (int p = 0; p < nPts; ++p)
                acc[static_cast<size_t>(p)] += sp[static_cast<size_t>(p)];
            ++valid;
        }
        fclose(spk);

        if (valid > 0)
            for (int p = 0; p < nPts; ++p)
                meanWav[static_cast<size_t>(ci)][static_cast<size_t>(p)] =
                    static_cast<float>(acc[static_cast<size_t>(p)] / valid);
    }

    if (haveToStopProcessing) { post(); return; }

    // ── 4. Parallel pairwise mean xcorr matrix ────────────────────────────
    scores = new Array<double>();
    scores->setSize(nClusters, nClusters);
    for (int i = 1; i <= nClusters; ++i)
        for (int j = 1; j <= nClusters; ++j)
            (*scores)(i, j) = (i == j) ? 1.0 : 0.0;

    std::vector<std::pair<int,int>> pairs;
    pairs.reserve(static_cast<size_t>(nClusters * (nClusters-1) / 2));
    for (int ci = 0; ci < nClusters; ++ci)
        for (int cj = ci+1; cj < nClusters; ++cj)
            pairs.emplace_back(ci, cj);
    const int nPairs = static_cast<int>(pairs.size());
    const int metric  = configuration().getTemplateXcorrMetric(); // 0=cos 1=pearson 2=raw
    const bool pearson = (metric == 1);
    const bool raw     = (metric == 2);

#pragma omp parallel for schedule(dynamic,4) default(none) \
    shared(meanWav, pairs, scores) firstprivate(nPairs, maxShift, pearson, raw)
    for (int pi = 0; pi < nPairs; ++pi) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;
        const int ci = pairs[static_cast<size_t>(pi)].first;
        const int cj = pairs[static_cast<size_t>(pi)].second;
        const float s = raw
            ? tmRawXcorr(meanWav[static_cast<size_t>(ci)],
                         meanWav[static_cast<size_t>(cj)], maxShift)
            : tmNormXcorr(meanWav[static_cast<size_t>(ci)],
                          meanWav[static_cast<size_t>(cj)], maxShift, pearson);
        (*scores)(ci+1, cj+1) = s;
        (*scores)(cj+1, ci+1) = s;
    }

    // Raw xcorr is unbounded (scales with waveform energy) while the colour map
    // and threshold slider assume [0,1].  Map onto that scale by dividing every
    // off-diagonal cell by the largest off-diagonal value: the most similar
    // pair reads 1.0 and the amplitude-weighted ordering is preserved.  The
    // diagonal stays 1.0.  Serial pass — the parallel fill is already done.
    if (raw && !haveToStopProcessing.load(std::memory_order_relaxed)) {
        double gmax = 0.0;
        for (int pi = 0; pi < nPairs; ++pi) {
            const int ci = pairs[static_cast<size_t>(pi)].first;
            const int cj = pairs[static_cast<size_t>(pi)].second;
            gmax = std::max(gmax, (*scores)(ci+1, cj+1));
        }
        if (gmax > 0.0) {
            const double inv = 1.0 / gmax;
            for (int pi = 0; pi < nPairs; ++pi) {
                const int ci = pairs[static_cast<size_t>(pi)].first;
                const int cj = pairs[static_cast<size_t>(pi)].second;
                const double v = (*scores)(ci+1, cj+1) * inv;
                (*scores)(ci+1, cj+1) = v;
                (*scores)(cj+1, ci+1) = v;
            }
        }
    }

    post();
}
