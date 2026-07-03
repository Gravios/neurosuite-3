/***************************************************************************
                          autoMerge.cpp  -  description
                             -------------------
    Patch 0069 — see autoMerge.h.

    Implementation notes
    --------------------
    The pipeline mirrors TemplateMatrixThread::run() which has the proven
    spike-file read pattern for klusters (one FILE* per cluster, channel
    de-interleave, channel-major output layout matching KKE's meanWav).
    Differences:
      * median mode subsamples up to medianK spikes per cluster (cost
        bound) and takes the per-element median across them
      * optional Hann taper on each template before scoring
      * union-find on score >= threshold pairs builds merge groups
      * synchronous with progress dialog (vs Template's async QThread)
        — async would be a follow-up if all-active mode on big sessions
        is slow.

    For documentation of the normalised xcorr math, see
    templatematrixthread.cpp:tmNormXcorr — the same function is duplicated
    here under an anonymous namespace.  A future cleanup would extract
    it to a shared header.
 ***************************************************************************/

#include "autoMerge.h"

#include "klustersdoc.h"
#include "data.h"
#include "sortabletable.h"
#include "configuration.h"
#include "templatematrixthread.h"   // tmReadSpikeFloat
#include "groupingassistant.h"      // computeMeanProbabilities (error-matrix criterion)
#include "array.h"                  // Array<double> error matrix

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressDialog>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <random>
#include <tuple>

namespace AutoMerge {

namespace {

// Hann taper applied per sample, broadcast across channels.  taperLen is
// the number of samples on each end that are tapered; clamped to nSamp/2.
void applyHannTaper(std::vector<float>& tmpl, int nChan, int nSamp, int taperLen)
{
    if (taperLen <= 0) return;
    const int tl = std::min(taperLen, nSamp / 2);
    if (tl <= 0) return;
    std::vector<float> w(static_cast<size_t>(nSamp), 1.0f);
    constexpr double PI = 3.14159265358979323846;
    for (int s = 0; s < tl; ++s) {
        const float v = static_cast<float>(
            0.5 * (1.0 - std::cos(PI * (s + 1) / (tl + 1))));
        w[static_cast<size_t>(s)] = v;
        w[static_cast<size_t>(nSamp - 1 - s)] = v;
    }
    for (int ch = 0; ch < nChan; ++ch)
        for (int s = 0; s < nSamp; ++s)
            tmpl[static_cast<size_t>(ch*nSamp + s)] *= w[static_cast<size_t>(s)];
}

// Path-compressing union-find for resolving transitive merges.
struct UnionFind {
    std::vector<int> parent;
    explicit UnionFind(int n) : parent(static_cast<size_t>(n)) {
        for (int i = 0; i < n; ++i) parent[static_cast<size_t>(i)] = i;
    }
    int find(int x) {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] =
                parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    }
    void unite(int a, int b) {
        const int ra = find(a), rb = find(b);
        if (ra != rb) parent[static_cast<size_t>(ra)] = rb;
    }
};

// Error-matrix merge criterion (alternative to the template xcorr in
// computeProposals).  Scores each candidate pair by the posterior-confusion
// matrix the Error Matrix view shows -- GroupingAssistant::computeMeanProbabilities,
// whose cell (i,j) is the mean over cluster i's spikes of their posterior under
// cluster j -- and proposes a merge when the larger of the two directed
// probabilities reaches the threshold (the matrix is not symmetric).  The
// union-find / group emission mirrors the template path exactly.  Note:
// computeMeanProbabilities is a single blocking call (seconds on a large
// session) and, unlike the template path, is not cancellable mid-computation.
QList<MergeGroup> computeProposalsByErrorMatrix(
    Data& data, const Settings& settings, const QList<int>& candidates,
    QWidget* parent)
{
    QList<MergeGroup> result;
    const int nClusters = candidates.size();

    QProgressDialog progress(
        QObject::tr("Auto-Merge: computing error matrix..."),
        QString(), 0, 0, parent);           // busy indicator; the call below is atomic
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(100);
    progress.setValue(0);
    QApplication::processEvents();

    // clusterList (out) gives the cluster id at each 1-based row/column of the
    // returned [nClusters x nClusters] matrix.
    GroupingAssistant assistant;
    QList<int> clusterList, computedClusterList, ignoreClusterIndex;
    Array<double>* err = assistant.computeMeanProbabilities(
        data, clusterList, computedClusterList, ignoreClusterIndex);
    if (err == nullptr) return result;

    // Map each cluster id to its 1-based index in the matrix.
    std::map<int,int> rowOf;
    for (int k = 0; k < clusterList.size(); ++k)
        rowOf[clusterList[k]] = k + 1;

    // A candidate pair merges when the larger directed confusion probability
    // reaches the threshold; both directions are read because P(i->j) != P(j->i).
    const double thr = settings.errorProbThreshold;
    std::vector<std::tuple<int,int,float>> highPairs;
    for (int a = 0; a < nClusters; ++a) {
        const auto ita = rowOf.find(candidates[a]);
        if (ita == rowOf.end()) continue;              // cluster absent from the matrix
        for (int b = a + 1; b < nClusters; ++b) {
            const auto itb = rowOf.find(candidates[b]);
            if (itb == rowOf.end()) continue;
            const double pij = (*err)(ita->second, itb->second);
            const double pji = (*err)(itb->second, ita->second);
            const double sc  = std::max(pij, pji);
            if (sc >= thr)
                highPairs.emplace_back(a, b, static_cast<float>(sc));
        }
    }
    delete err;

    // Union-find on the score graph -> groups of size >= 2 (mirrors section 5).
    UnionFind uf(nClusters);
    for (const auto& p : highPairs) uf.unite(std::get<0>(p), std::get<1>(p));

    std::map<int, MergeGroup> groupsByRoot;
    for (int i = 0; i < nClusters; ++i) {
        const int r = uf.find(i);
        groupsByRoot[r].clusters.append(candidates[i]);
        groupsByRoot[r].totalSpikes +=
            static_cast<int>(data.nbOfSpikes(candidates[i]));
    }
    for (const auto& p : highPairs) {
        const int r = uf.find(std::get<0>(p));
        const float sc = std::get<2>(p);
        if (static_cast<double>(sc) > groupsByRoot[r].maxPairScore)
            groupsByRoot[r].maxPairScore = static_cast<double>(sc);
    }
    for (auto& kv : groupsByRoot) {
        MergeGroup& g = kv.second;
        if (g.clusters.size() < 2) continue;
        std::sort(g.clusters.begin(), g.clusters.end());
        result.append(g);
    }
    return result;
}

}  // anonymous namespace


QList<MergeGroup> computeProposals(
    KlustersDoc*       /*doc*/,
    Data&              data,
    const Settings&    settings,
    const QList<int>&  candidatesIn,
    QWidget*           parent)
{
    QList<MergeGroup> result;

    // ── 1. Filter candidates: skip 0 (artefact), 1 (noise), and clusters
    //       below minClusterSize.
    QList<int> candidates;
    for (int c : candidatesIn) {
        if (c <= 1) continue;
        if (static_cast<int>(data.nbOfSpikes(c)) < settings.minClusterSize) continue;
        candidates.append(c);
    }
    std::sort(candidates.begin(), candidates.end());

    const int nClusters = candidates.size();
    if (nClusters < 2) return result;

    // Error-matrix criterion: score pairs by posterior confusion instead of
    // template cross-correlation.  Self-contained; the template path is untouched.
    if (settings.useErrorMatrix)
        return computeProposalsByErrorMatrix(data, settings, candidates, parent);

    const int    nChan    = data.nbOfChannels();
    const int    nSamp    = data.nbSamplesPerWaveform();
    const int    nPts     = nChan * nSamp;
    const int    maxShift = (settings.maxShift > 0)
                          ? settings.maxShift
                          : std::max(1, nSamp / 4);
    // Match the template-matrix metric EXACTLY so the auto-merge thresholds the
    // same scores the user sees in the matrix (0=cosine 1=Pearson 2=raw
    // 3=noise-disattenuated 4=fast-AP).  Reading only the Pearson bool here made
    // metrics 2/3/4 silently fall back to cosine -> auto-merge scored pairs
    // differently from the displayed matrix.
    const int    metric    = configuration().getTemplateXcorrMetric();
    const bool   needNoise = (metric == 3);                  // disatten needs a per-point noise template
    const int    peak      = data.peakSampleIndex();         // 0-based AP centre (fast-AP metric)
    const QString spkPath = data.getSpkFileName();
    if (spkPath.isEmpty() || nPts <= 0) return result;

    QProgressDialog progress(
        QObject::tr("Auto-Merge: reading waveforms..."),
        QObject::tr("Cancel"),
        0, 2 * nClusters + 2, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(100);
    progress.setValue(0);

    // ── 2. Pre-fetch spike-file indices (the SortableTable read must be
    //       serial — Data's internal mutex doesn't allow parallel reads).
    std::vector<std::vector<int>> allFileIdx(static_cast<size_t>(nClusters));
    for (int ci = 0; ci < nClusters; ++ci) {
        if (progress.wasCanceled()) return result;
        SortableTable pos;
        if (!data.spikePositions(candidates[ci], pos)) continue;
        const long nSpk = static_cast<long>(data.nbOfSpikes(candidates[ci]));
        allFileIdx[static_cast<size_t>(ci)].reserve(static_cast<size_t>(nSpk));
        for (long s = 0; s < nSpk; ++s)
            allFileIdx[static_cast<size_t>(ci)].push_back(
                static_cast<int>(pos(1, s + 1)) - 1);
    }

    // ── 3. Build templates (mean or median) per cluster.
    const bool useMedian = (settings.algorithm == 1);
    const int  K         = std::max(1, settings.medianK);
    std::vector<std::vector<float>> templates(static_cast<size_t>(nClusters));
    // Per-point noise energy of each template (sample variance / N), only built
    // for the disatten metric; tmDisattenXcorr subtracts it from each norm.
    std::vector<std::vector<float>> noiseWav;
    if (needNoise)
        noiseWav.assign(static_cast<size_t>(nClusters), std::vector<float>());

    const QByteArray spkBytes = spkPath.toLocal8Bit();
    const char*      spkCStr  = spkBytes.constData();

    // Stable RNG seed → reproducible previews across runs of the same data.
    std::mt19937 rng(0x4d525142u);  // 'MRQB'

    for (int ci = 0; ci < nClusters; ++ci) {
        if (progress.wasCanceled()) return result;
        progress.setValue(ci);
        QApplication::processEvents();

        const auto& fidx = allFileIdx[static_cast<size_t>(ci)];
        const long  nSpk = static_cast<long>(fidx.size());
        if (nSpk == 0) continue;

        // Median mode: subsample up to K spikes for bounded compute/memory.
        std::vector<int> readSpikes;
        if (useMedian && nSpk > K) {
            std::vector<int> idx(static_cast<size_t>(nSpk));
            std::iota(idx.begin(), idx.end(), 0);
            std::shuffle(idx.begin(), idx.end(), rng);
            readSpikes.assign(idx.begin(), idx.begin() + K);
            std::sort(readSpikes.begin(), readSpikes.end());
        } else {
            readSpikes.resize(static_cast<size_t>(nSpk));
            std::iota(readSpikes.begin(), readSpikes.end(), 0);
        }
        const int M = static_cast<int>(readSpikes.size());
        if (M < 1) continue;

        FILE* spk = std::fopen(spkCStr, "rb");
        if (!spk) continue;

        // Median mode keeps every spike's wave; mean mode keeps only a
        // running sum.  Channel-major output ([ch*nSamp+sm]) matches the
        // KKE meanWav layout so the same xcorr formula works.
        // Per-spike waveforms (median mode) are stored as float, matching the
        // shared reader; mean mode accumulates into a double.  The .spk is int16
        // on disk so float is exact, and median/mean stay consistent.
        std::vector<std::vector<float>> spikeWavs;
        std::vector<double>             sumAcc(static_cast<size_t>(nPts), 0.0);
        std::vector<double>             sqAcc;   // sum of squares, disatten metric only
        if (needNoise) sqAcc.assign(static_cast<size_t>(nPts), 0.0);
        std::vector<uint8_t>            okRow;  // median mode only
        if (useMedian) {
            spikeWavs.assign(static_cast<size_t>(M),
                             std::vector<float>(static_cast<size_t>(nPts), 0.0f));
            okRow.assign(static_cast<size_t>(M), 0);
        }

        std::vector<int16_t> raw;
        std::vector<float>   sp;
        long valid = 0;
        for (int i = 0; i < M; ++i) {
            const int s = readSpikes[static_cast<size_t>(i)];
            if (!tmReadSpikeFloat(spk, fidx[static_cast<size_t>(s)],
                                  nChan, nSamp, raw, sp))
                continue;
            if (useMedian) {
                spikeWavs[static_cast<size_t>(i)] = sp;
                okRow[static_cast<size_t>(i)] = 1;
            } else {
                for (int p = 0; p < nPts; ++p)
                    sumAcc[static_cast<size_t>(p)] += sp[static_cast<size_t>(p)];
            }
            if (needNoise) {                                 // mean+var needed for the noise template
                if (useMedian)
                    for (int p = 0; p < nPts; ++p)
                        sumAcc[static_cast<size_t>(p)] += sp[static_cast<size_t>(p)];
                for (int p = 0; p < nPts; ++p)
                    sqAcc[static_cast<size_t>(p)] +=
                        static_cast<double>(sp[static_cast<size_t>(p)]) * sp[static_cast<size_t>(p)];
            }
            ++valid;
        }
        std::fclose(spk);
        if (valid < 1) continue;

        std::vector<float> tmpl(static_cast<size_t>(nPts), 0.0f);
        if (useMedian) {
            // Per-element median across the rows that read successfully.
            std::vector<float> col(static_cast<size_t>(valid));
            for (int p = 0; p < nPts; ++p) {
                size_t j = 0;
                for (int i = 0; i < M; ++i)
                    if (okRow[static_cast<size_t>(i)])
                        col[j++] =
                            spikeWavs[static_cast<size_t>(i)][static_cast<size_t>(p)];
                std::nth_element(col.begin(),
                                 col.begin() + valid/2,
                                 col.begin() + valid);
                tmpl[static_cast<size_t>(p)] = col[static_cast<size_t>(valid/2)];
            }
        } else {
            for (int p = 0; p < nPts; ++p)
                tmpl[static_cast<size_t>(p)] =
                    static_cast<float>(sumAcc[static_cast<size_t>(p)] / valid);
        }
        if (settings.taperSamples > 0)
            applyHannTaper(tmpl, nChan, nSamp, settings.taperSamples);
        if (needNoise) {                                     // per-point noise energy of the mean = var / N
            std::vector<float> nz(static_cast<size_t>(nPts), 0.0f);
            for (int p = 0; p < nPts; ++p) {
                const double m   = sumAcc[static_cast<size_t>(p)] / valid;
                const double var = sqAcc[static_cast<size_t>(p)] / valid - m * m;
                nz[static_cast<size_t>(p)] = static_cast<float>(std::max(0.0, var) / valid);
            }
            noiseWav[static_cast<size_t>(ci)] = std::move(nz);
        }
        templates[static_cast<size_t>(ci)] = std::move(tmpl);
    }

    if (progress.wasCanceled()) return result;

    // ── 4. Pairwise normalised xcorr; collect pairs at or above threshold.
    progress.setLabelText(QObject::tr("Auto-Merge: scoring pairs..."));
    progress.setValue(nClusters);
    QApplication::processEvents();

    std::vector<std::tuple<int,int,float>> highPairs;
    const float thr = static_cast<float>(settings.scoreThreshold);
    std::vector<std::tuple<int,int,float>> allPairs;   // every scored pair (raw needs a global-max pass)
    for (int i = 0; i < nClusters; ++i) {
        // Step 4 is O(nClusters^2); on big all-active runs it can dominate, so
        // keep the dialog responsive and cancellable between rows (the inner
        // j-loop is the unit of cancel granularity).
        if (progress.wasCanceled()) return result;
        progress.setValue(nClusters + i);
        QApplication::processEvents();
        if (templates[static_cast<size_t>(i)].empty()) continue;
        for (int j = i + 1; j < nClusters; ++j) {
            if (templates[static_cast<size_t>(j)].empty()) continue;
            float s;
            if (metric == 2)
                s = tmRawXcorr(templates[static_cast<size_t>(i)],
                               templates[static_cast<size_t>(j)], maxShift);
            else if (metric == 3)
                s = tmDisattenXcorr(templates[static_cast<size_t>(i)],
                                    templates[static_cast<size_t>(j)],
                                    noiseWav[static_cast<size_t>(i)],
                                    noiseWav[static_cast<size_t>(j)], maxShift);
            else if (metric == 4)
                s = tmFastWinXcorr(templates[static_cast<size_t>(i)],
                                   templates[static_cast<size_t>(j)],
                                   nChan, nSamp, peak, maxShift);
            else
                s = tmNormXcorr(templates[static_cast<size_t>(i)],
                                templates[static_cast<size_t>(j)], maxShift, metric == 1);
            allPairs.emplace_back(i, j, s);
        }
    }
    // Raw xcorr is unbounded; the template matrix maps it to [0,1] by dividing
    // every off-diagonal cell by the largest, so the same threshold applies.
    if (metric == 2) {
        float gmax = 0.0f;
        for (const auto& p : allPairs) gmax = std::max(gmax, std::get<2>(p));
        if (gmax > 0.0f)
            for (auto& p : allPairs) std::get<2>(p) /= gmax;
    }
    for (const auto& p : allPairs)
        if (std::get<2>(p) >= thr) highPairs.push_back(p);

    // ── 5. Union-find on the score-graph; emit groups of size >= 2.
    UnionFind uf(nClusters);
    for (const auto& p : highPairs) uf.unite(std::get<0>(p), std::get<1>(p));

    std::map<int, MergeGroup> groupsByRoot;
    for (int i = 0; i < nClusters; ++i) {
        const int r = uf.find(i);
        groupsByRoot[r].clusters.append(candidates[i]);
        groupsByRoot[r].totalSpikes +=
            static_cast<int>(data.nbOfSpikes(candidates[i]));
    }
    for (const auto& p : highPairs) {
        const int r = uf.find(std::get<0>(p));
        const float s = std::get<2>(p);
        if (static_cast<double>(s) > groupsByRoot[r].maxPairScore)
            groupsByRoot[r].maxPairScore = static_cast<double>(s);
    }
    for (auto& kv : groupsByRoot) {
        MergeGroup& g = kv.second;
        if (g.clusters.size() < 2) continue;
        std::sort(g.clusters.begin(), g.clusters.end());
        result.append(g);
    }

    progress.setValue(2 * nClusters + 2);
    return result;
}


QList<MergeGroup> promptPreview(
    const QList<MergeGroup>& proposals,
    QWidget*                 parent)
{
    if (proposals.isEmpty()) return proposals;

    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Auto-Merge: confirm proposed groups"));
    dlg.setModal(true);

    QVBoxLayout* outer = new QVBoxLayout(&dlg);
    outer->addWidget(new QLabel(QObject::tr(
        "%1 merge group(s) proposed.  Uncheck a group to skip it.\n"
        "OK applies all checked groups; Cancel applies none.")
        .arg(proposals.size()), &dlg));

    QScrollArea* scroll = new QScrollArea(&dlg);
    scroll->setWidgetResizable(true);
    QWidget*     inner  = new QWidget;
    QVBoxLayout* iv     = new QVBoxLayout(inner);

    QList<QCheckBox*> boxes;
    for (const MergeGroup& g : proposals) {
        QStringList ids;
        for (int c : g.clusters) ids << QString::number(c);
        const QString lbl = QObject::tr(
            "[%1]   best score %2   spikes %3")
            .arg(ids.join(QLatin1String(", ")))
            .arg(QString::number(g.maxPairScore, 'f', 3))
            .arg(g.totalSpikes);
        QCheckBox* cb = new QCheckBox(lbl, inner);
        cb->setChecked(true);
        boxes.append(cb);
        iv->addWidget(cb);
    }
    iv->addStretch(1);
    scroll->setWidget(inner);
    outer->addWidget(scroll, 1);

    QDialogButtonBox* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(bb);

    dlg.resize(580, 480);
    if (dlg.exec() != QDialog::Accepted) return {};

    QList<MergeGroup> kept;
    for (int i = 0; i < proposals.size(); ++i)
        if (boxes[i]->isChecked()) kept.append(proposals[i]);
    return kept;
}

}  // namespace AutoMerge
