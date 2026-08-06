#include "templatematrixthread.h"
#include "channelmask.h"
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
float tmDisattenXcorr(const std::vector<float>& a,
                      const std::vector<float>& b,
                      const std::vector<float>& noiseA,
                      const std::vector<float>& noiseB,
                      int maxShift)
{
    const int N = static_cast<int>(a.size());
    if (N == 0) return 0.0f;

    float best = 0.0f;
    for (int lag = -maxShift; lag <= maxShift; ++lag) {
        double sab = 0.0, saa = 0.0, sbb = 0.0, na = 0.0, nb = 0.0;
        int cnt = 0;
        for (int i = 0; i < N; ++i) {
            const int j = i + lag;
            if (j < 0 || j >= N) continue;
            const double ai = a[i], bj = b[j];
            sab += ai * bj; saa += ai * ai; sbb += bj * bj;
            na  += noiseA[i]; nb += noiseB[j];        // noise energy of each mean
            ++cnt;
        }
        if (cnt == 0) continue;
        // Subtract the noise energy of the mean from each norm, but never below
        // 10% of the raw energy (a near-noise mean would otherwise blow up the
        // ratio); the final value is capped at 1.0 below for the colour map.
        const double normA = std::max(saa - na, 0.10 * saa);
        const double normB = std::max(sbb - nb, 0.10 * sbb);
        const double denom = std::sqrt(normA * normB);
        if (denom < 1e-12) continue;
        const float val = static_cast<float>(std::min(1.0, std::abs(sab) / denom));
        if (val > best) best = val;
    }
    return best;
}

// ---------------------------------------------------------------------------
float tmFastWinXcorr(const std::vector<float>& a,
                     const std::vector<float>& b,
                     int nChan, int nSamp, int peak,
                     int maxShift)
{
    if (nChan <= 0 || nSamp <= 0) return 0.0f;
    const int lo = std::max(0, peak - 8);
    const int hi = std::min(nSamp, peak + 8);
    const int win = hi - lo;
    if (win <= 1) return tmNormXcorr(a, b, maxShift, false);   // window degenerate: fall back

    // Extract the per-channel AP window into reduced channel-major vectors,
    // then reuse the cosine kernel (its global lag now spans only the windowed
    // concatenation, consistent with the full-template lag convention).
    std::vector<float> aw, bw;
    aw.reserve(static_cast<size_t>(nChan * win));
    bw.reserve(static_cast<size_t>(nChan * win));
    for (int ch = 0; ch < nChan; ++ch) {
        const int base = ch * nSamp;
        for (int s = lo; s < hi; ++s) { aw.push_back(a[base + s]); bw.push_back(b[base + s]); }
    }
    return tmNormXcorr(aw, bw, std::max(1, std::min(maxShift, win / 4)), false);
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

    // Scoped matrices: keep only one parent's children.  Applied after the list is
    // built rather than inside the enumeration, because the enumeration also
    // decides what counts as a usable cluster (non-empty, not the artefact bin) and
    // that judgement is independent of scope.  Cluster 1 is kept regardless: the
    // noise bin is what makes "move these spikes back to a real unit" possible from
    // this view, and it is the same carve-out the error path makes.
    if (!activeClusters.isEmpty()) {
        QList<int> scoped;
        for (int id : clusterList)
            if (id <= 1 || activeClusters.contains(id)) scoped.append(id);
        clusterList = scoped;
    }

    const int nClusters = clusterList.size();
    if (nClusters < 2) { post(); return; }

    const int    nChan    = data.nbOfChannels();
    const int    nSamp    = data.nbSamplesPerWaveform();
    const int    nPts     = nChan * nSamp;
    const int    maxShift = std::max(1, nSamp / 4);
    const int    metric    = configuration().getTemplateXcorrMetric(); // 0=cos 1=pear 2=raw 3=disatten 4=fastAP
    const bool   needNoise = (metric == 3);                            // disattenuation needs a variance pass
    const int    peak       = data.peakSampleIndex();                  // 0-based AP-window centre (fast-AP metric)
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
    // Per-point noise energy of each MEAN (within-cluster sample variance / N),
    // filled only when the disattenuated metric needs it.
    std::vector<std::vector<float>> noiseWav;
    if (needNoise)
        noiseWav.assign(static_cast<size_t>(nClusters),
                        std::vector<float>(static_cast<size_t>(nPts), 0.0f));

    const QByteArray spkBytes = spkPath.toLocal8Bit();
    const char*      spkCStr  = spkBytes.constData();

#pragma omp parallel for schedule(dynamic,1) default(none) \
    shared(meanWav, noiseWav, allFileIdx) \
    firstprivate(nClusters, nPts, nChan, nSamp, spkCStr, needNoise)
    for (int ci = 0; ci < nClusters; ++ci) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;

        const auto& fidx = allFileIdx[static_cast<size_t>(ci)];
        const long  nSpk = static_cast<long>(fidx.size());
        if (nSpk == 0) continue;

        FILE* spk = fopen(spkCStr, "rb");
        if (!spk) continue;

        std::vector<double>  acc(static_cast<size_t>(nPts), 0.0);
        std::vector<double>  accsq;
        if (needNoise) accsq.assign(static_cast<size_t>(nPts), 0.0);
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
                acc[static_cast<size_t>(p)] += v;
                if (needNoise) accsq[static_cast<size_t>(p)] += v * v;
            }
            ++valid;
        }
        fclose(spk);

        if (valid > 0)
            for (int p = 0; p < nPts; ++p) {
                const double mean = acc[static_cast<size_t>(p)] / valid;
                meanWav[static_cast<size_t>(ci)][static_cast<size_t>(p)] =
                    static_cast<float>(mean);
                if (needNoise) {
                    const double var = accsq[static_cast<size_t>(p)] / valid - mean * mean;
                    // noise energy of the sample MEAN = sample variance / N
                    noiseWav[static_cast<size_t>(ci)][static_cast<size_t>(p)] =
                        static_cast<float>(std::max(0.0, var) / valid);
                }
            }
    }

    if (haveToStopProcessing) { post(); return; }

    // ── 3b. Restrict the MATRIX to the selected channels ──────────────────
    // Into local copies, deliberately: meanWav is handed to PairXcorrThread by
    // the view together with Data::nbOfChannels(), so the member must keep every
    // channel or that spike scoring would read past a compacted template.  The
    // matrix below uses the compacted copies instead.  Compacted rather than
    // zeroed — see channelmask.h.
    const std::vector<std::vector<float>>* matMean  = &meanWav;
    const std::vector<std::vector<float>>* matNoise = &noiseWav;
    std::vector<std::vector<float>> maskedMean, maskedNoise;
    {
        std::vector<int> sel;
        sel.reserve(static_cast<size_t>(selection.size()));
        for (int c : selection) sel.push_back(c);
        const std::vector<int> keep = cmResolveMask(sel, nChan);
        if (!keep.empty()) {
            maskedMean.resize(meanWav.size());
            for (size_t k = 0; k < meanWav.size(); ++k)
                cmCompactChannels(meanWav[k], nChan, nSamp, keep, maskedMean[k]);
            maskedNoise.resize(noiseWav.size());
            for (size_t k = 0; k < noiseWav.size(); ++k)
                cmCompactChannels(noiseWav[k], nChan, nSamp, keep, maskedNoise[k]);
            matMean  = &maskedMean;
            matNoise = &maskedNoise;
        }
    }
    const std::vector<std::vector<float>>& mMean  = *matMean;
    const std::vector<std::vector<float>>& mNoise = *matNoise;

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
    const bool pearson  = (metric == 1);
    const bool raw      = (metric == 2);
    const bool disatten = (metric == 3);
    const bool fastap   = (metric == 4);

#pragma omp parallel for schedule(dynamic,4) default(none) \
    shared(mMean, mNoise, pairs, scores) \
    firstprivate(nPairs, maxShift, pearson, raw, disatten, fastap, nChan, nSamp, peak)
    for (int pi = 0; pi < nPairs; ++pi) {
        if (haveToStopProcessing.load(std::memory_order_relaxed)) continue;
        const int ci = pairs[static_cast<size_t>(pi)].first;
        const int cj = pairs[static_cast<size_t>(pi)].second;
        float s;
        if (raw)
            s = tmRawXcorr(mMean[static_cast<size_t>(ci)],
                           mMean[static_cast<size_t>(cj)], maxShift);
        else if (disatten)
            s = tmDisattenXcorr(mMean[static_cast<size_t>(ci)],
                                mMean[static_cast<size_t>(cj)],
                                mNoise[static_cast<size_t>(ci)],
                                mNoise[static_cast<size_t>(cj)], maxShift);
        else if (fastap)
            s = tmFastWinXcorr(mMean[static_cast<size_t>(ci)],
                               mMean[static_cast<size_t>(cj)],
                               nChan, nSamp, peak, maxShift);
        else
            s = tmNormXcorr(mMean[static_cast<size_t>(ci)],
                            mMean[static_cast<size_t>(cj)], maxShift, pearson);
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
