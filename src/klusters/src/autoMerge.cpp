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

// Normalised xcorr with bounded shift.  Matches templatematrixthread.cpp
// :tmNormXcorr verbatim — pull-extract to a shared helper is a future
// cleanup.  Returns the maximum |xcorr| / sqrt(|a|^2 * |b|^2) across
// lags in [-maxShift, +maxShift]; sign-insensitive on purpose so
// inverted-polarity duplicates also match.
float normXcorr(const std::vector<float>& a,
                const std::vector<float>& b,
                int maxShift)
{
    const int N = static_cast<int>(a.size());
    if (N == 0) return 0.0f;
    double normA = 0.0, normB = 0.0;
    for (int i = 0; i < N; ++i) {
        normA += static_cast<double>(a[i]) * a[i];
        normB += static_cast<double>(b[i]) * b[i];
    }
    const double denom = std::sqrt(normA * normB);
    if (denom < 1e-12) return 0.0f;
    float best = 0.0f;
    for (int lag = -maxShift; lag <= maxShift; ++lag) {
        double xc = 0.0;
        for (int i = 0; i < N; ++i) {
            int j = i + lag;
            if (j < 0 || j >= N) continue;
            xc += static_cast<double>(a[i]) * b[j];
        }
        float val = static_cast<float>(std::abs(xc) / denom);
        if (val > best) best = val;
    }
    return best;
}

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

    const int    nChan    = data.nbOfChannels();
    const int    nSamp    = data.nbSamplesPerWaveform();
    const int    nPts     = nChan * nSamp;
    const int    maxShift = (settings.maxShift > 0)
                          ? settings.maxShift
                          : std::max(1, nSamp / 4);
    const bool   twoBytes = data.isRecordingTwoBytes();
    const int    sBytes   = twoBytes ? 2 : 4;
    const QString spkPath = data.getSpkFileName();
    if (spkPath.isEmpty() || nPts <= 0) return result;

    QProgressDialog progress(
        QObject::tr("Auto-Merge: reading waveforms..."),
        QObject::tr("Cancel"),
        0, nClusters + 2, parent);
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
        std::vector<std::vector<int16_t>> spikeWavs;
        std::vector<double>               sumAcc(static_cast<size_t>(nPts), 0.0);
        std::vector<uint8_t>              okRow;  // median mode only
        if (useMedian) {
            spikeWavs.assign(static_cast<size_t>(M),
                             std::vector<int16_t>(static_cast<size_t>(nPts), 0));
            okRow.assign(static_cast<size_t>(M), 0);
        }

        std::vector<int16_t> buf16(static_cast<size_t>(nPts));
        std::vector<int32_t> buf32(static_cast<size_t>(nPts));
        long valid = 0;
        for (int i = 0; i < M; ++i) {
            const int s = readSpikes[static_cast<size_t>(i)];
            const off_t off = static_cast<off_t>(fidx[static_cast<size_t>(s)])
                            * static_cast<off_t>(nPts)
                            * static_cast<off_t>(sBytes);
            if (fseeko(spk, off, SEEK_SET) != 0) continue;
            bool ok = false;
            if (twoBytes) {
                ok = (std::fread(buf16.data(), 2,
                                 static_cast<size_t>(nPts), spk)
                      == static_cast<size_t>(nPts));
                if (ok) {
                    for (int ch = 0; ch < nChan; ++ch)
                        for (int sm = 0; sm < nSamp; ++sm) {
                            const int p = ch*nSamp + sm;
                            const int16_t v =
                                buf16[static_cast<size_t>(sm*nChan + ch)];
                            if (useMedian)
                                spikeWavs[static_cast<size_t>(i)][static_cast<size_t>(p)] = v;
                            else
                                sumAcc[static_cast<size_t>(p)] += v;
                        }
                }
            } else {
                ok = (std::fread(buf32.data(), 4,
                                 static_cast<size_t>(nPts), spk)
                      == static_cast<size_t>(nPts));
                if (ok) {
                    for (int ch = 0; ch < nChan; ++ch)
                        for (int sm = 0; sm < nSamp; ++sm) {
                            const int p = ch*nSamp + sm;
                            const int32_t v =
                                buf32[static_cast<size_t>(sm*nChan + ch)];
                            if (useMedian)
                                spikeWavs[static_cast<size_t>(i)][static_cast<size_t>(p)] =
                                    static_cast<int16_t>(v);
                            else
                                sumAcc[static_cast<size_t>(p)] += v;
                        }
                }
            }
            if (ok) {
                ++valid;
                if (useMedian) okRow[static_cast<size_t>(i)] = 1;
            }
        }
        std::fclose(spk);
        if (valid < 1) continue;

        std::vector<float> tmpl(static_cast<size_t>(nPts), 0.0f);
        if (useMedian) {
            // Per-element median across the rows that read successfully.
            std::vector<int16_t> col(static_cast<size_t>(valid));
            for (int p = 0; p < nPts; ++p) {
                size_t j = 0;
                for (int i = 0; i < M; ++i)
                    if (okRow[static_cast<size_t>(i)])
                        col[j++] =
                            spikeWavs[static_cast<size_t>(i)][static_cast<size_t>(p)];
                std::nth_element(col.begin(),
                                 col.begin() + valid/2,
                                 col.begin() + valid);
                tmpl[static_cast<size_t>(p)] =
                    static_cast<float>(col[static_cast<size_t>(valid/2)]);
            }
        } else {
            for (int p = 0; p < nPts; ++p)
                tmpl[static_cast<size_t>(p)] =
                    static_cast<float>(sumAcc[static_cast<size_t>(p)] / valid);
        }
        if (settings.taperSamples > 0)
            applyHannTaper(tmpl, nChan, nSamp, settings.taperSamples);
        templates[static_cast<size_t>(ci)] = std::move(tmpl);
    }

    if (progress.wasCanceled()) return result;

    // ── 4. Pairwise normalised xcorr; collect pairs at or above threshold.
    progress.setLabelText(QObject::tr("Auto-Merge: scoring pairs..."));
    progress.setValue(nClusters);
    QApplication::processEvents();

    std::vector<std::tuple<int,int,float>> highPairs;
    const float thr = static_cast<float>(settings.scoreThreshold);
    for (int i = 0; i < nClusters; ++i) {
        if (templates[static_cast<size_t>(i)].empty()) continue;
        for (int j = i + 1; j < nClusters; ++j) {
            if (templates[static_cast<size_t>(j)].empty()) continue;
            const float s = normXcorr(templates[static_cast<size_t>(i)],
                                      templates[static_cast<size_t>(j)],
                                      maxShift);
            if (s >= thr) highPairs.emplace_back(i, j, s);
        }
    }

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

    progress.setValue(nClusters + 2);
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
