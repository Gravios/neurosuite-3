#include "residualmatrixthread.h"
#include "channelmask.h"
#include "residualmatrixview.h"
#include "templatematrixthread.h"   // shared tmReadSpikeFloat
#include "sortabletable.h"

#include <QApplication>
#include <QThreadPool>
#include <QMutexLocker>
#include <cmath>
#include <algorithm>

#ifdef _OPENMP
#include <omp.h>
#endif

// ---------------------------------------------------------------------------
ResidualMatrixThread::ResidualMatrixThread(ResidualMatrixView& v, Data& d,
                                           const std::shared_ptr<KlustersJobToken>& viewToken,
                                           QList<int> sel, QList<int> clusterScope)
    : view(v), data(d), token(viewToken), snapshot(d.currentSnapshot()), scores(nullptr),
      selection(std::move(sel)),
      activeClusters(std::move(clusterScope))
{
    setAutoDelete(true);
    //The creation of the job launches the request, as the old thread's
    //constructor did with start().
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    KlustersJobPool::pool()->start(this, KlustersJobPool::BatchPriority);   // full matrix compute
}

void ResidualMatrixThread::post(QEvent* event)
{
    // Fence against view destruction: ~ResidualMatrixView sets viewDead under
    // the same mutex, so while we hold it and viewDead is false the view is a
    // valid event receiver.  A refused event deletes itself — and with it the
    // matrix it owns.
    QMutexLocker lock(&token->postMutex);
    if (token->viewDead) {
        delete event;
        return;
    }
    QApplication::postEvent(&view, event);
}

void ResidualMatrixThread::run()
{
    process();
    //Retire: this must be the last touch of any shared state (Data above
    //all).  The synchronous quiesce in stopRunningThreadsSync() and the
    //document-close pool drain treat active == 0 as "no job is inside a
    //Data call anymore".
    token->active.fetch_sub(1, std::memory_order_acq_rel);
}

void ResidualMatrixThread::process()
{
    auto postDone = [this]() {
        post(new ResidualMatrixEvent(*this));
    };

    if (cancelled()) { postDone(); return; }

    // ── 1. Cluster list ──────────────────────────────────────────────────
    {
        const QList<dataType> allIds = snapshot->clusterIds();
        if (!activeClusters.isEmpty()) {
            // Scoped: build the list DIRECTLY as [noise, children...] rather than
            // building every cluster and filtering after.  The matrix is sized from
            // this list, so what goes in it is the matrix -- a parent with 7
            // children gives 8x8.
            //
            // Noise is kept and artefact is not, matching the unscoped build below.
            // Noise earns its row: comparing a child against it is how spikes get
            // judged and moved back out, so it is a destination the curator uses.
            // Artefact has no waveform, so its row would be empty in every one of
            // these three views, which compare templates.
            //
            // Noise is included regardless of spike count so the shape does not
            // change under the user as the bin empties and fills; children still
            // need spikes, since a child with none has nothing to compare.
            for (dataType id : allIds)
                if (id == ClusterId::Noise) clusterList.append(static_cast<int>(id));
            for (int id : activeClusters)
                if (id > 1 && snapshot->nbOfSpikes(id) > 0) clusterList.append(id);
        } else {
            for (dataType id : allIds)
                if (id >= 1 && snapshot->nbOfSpikes(id) > 0)
                    clusterList.append(static_cast<int>(id));
        }
        std::sort(clusterList.begin(), clusterList.end());
    }


    const int nClusters = clusterList.size();
    if (nClusters < 2) { postDone(); return; }

    const int     nChan   = data.nbOfChannels();
    const int     nSamp   = data.nbSamplesPerWaveform();
    const int     nPts    = nChan * nSamp;
    const QString spkPath = data.getSpkFileName();
    if (spkPath.isEmpty() || nPts <= 0) { postDone(); return; }

    // ── 2. Pre-fetch .spk file indices (serial, mutex-safe) ───────────────
    allFileIdx.resize(static_cast<size_t>(nClusters));
    for (int ci = 0; ci < nClusters; ++ci) {
        if (cancelled()) { postDone(); return; }
        SortableTable posTable;
        if (!snapshot->spikePositions(clusterList[ci], posTable)) continue;
        const long nSpk = static_cast<long>(snapshot->nbOfSpikes(clusterList[ci]));
        allFileIdx[static_cast<size_t>(ci)].reserve(static_cast<size_t>(nSpk));
        for (long s = 0; s < nSpk; ++s)
            allFileIdx[static_cast<size_t>(ci)].push_back(
                static_cast<int>(posTable(1, s + 1)) - 1);
    }
    if (cancelled()) { postDone(); return; }

    // ── 3. Per-cluster mean + within-cluster variance (one streaming pass) ─
    // mean_c[p] = (1/N) Σ x_s[p];  var_c[p] = (1/N) Σ x_s[p]^2 − mean_c[p]^2.
    std::vector<std::vector<float>> meanWav(
        static_cast<size_t>(nClusters),
        std::vector<float>(static_cast<size_t>(nPts), 0.0f));
    std::vector<std::vector<float>> varWav(
        static_cast<size_t>(nClusters),
        std::vector<float>(static_cast<size_t>(nPts), 0.0f));

    SpkReader& spk = data.spkReader();   // one shared pread descriptor

#pragma omp parallel for schedule(dynamic,1) default(none) \
    shared(meanWav, varWav, allFileIdx, spk) \
    firstprivate(nClusters, nPts, nChan, nSamp)
    for (int ci = 0; ci < nClusters; ++ci) {
        if (cancelled()) continue;

        const auto& fidx = allFileIdx[static_cast<size_t>(ci)];
        const long  nSpk = static_cast<long>(fidx.size());
        if (nSpk == 0) continue;

        std::vector<double>  acc (static_cast<size_t>(nPts), 0.0);
        std::vector<double>  acc2(static_cast<size_t>(nPts), 0.0);
        std::vector<int16_t> raw;
        std::vector<float>   sp;
        long valid = 0;

        for (long s = 0; s < nSpk; ++s) {
            if (cancelled()) break;
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
    if (cancelled()) { postDone(); return; }

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
    if (effPts <= 0) { postDone(); return; }

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
        if (cancelled()) continue;
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

    postDone();
}
