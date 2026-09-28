#include "pairxcorrthread.h"
#include "templatematrixview.h"
#include "templatematrixthread.h"   // for tmNormXcorr
#include "configuration.h"

#include <QApplication>
#include <QThreadPool>
#include <QMutexLocker>
#include <cmath>

PairXcorrThread::PairXcorrThread(TemplateMatrixView& v,
                                 int sourceCluster, int targetCluster,
                                 const std::vector<int>&   sourceFileIdx,
                                 const std::vector<float>& targetMean,
                                 const std::shared_ptr<const Data::ClusteringSnapshot>& snap,
                                 int nChan, int nSamp, bool twoBytes,
                                 const std::shared_ptr<KlustersJobToken>& viewToken)
    : view(v),
      sourceCluster(sourceCluster), targetCluster(targetCluster),
      sourceFileIdx(sourceFileIdx), targetMean(targetMean),
      snapshot(snap), nChan(nChan), nSamp(nSamp),
      twoBytes(twoBytes), token(viewToken)
{
    setAutoDelete(true);
    //The creation of the job launches the request, as the old thread's
    //constructor did with start().
    jobGeneration = token->generation.load(std::memory_order_acquire);
    token->active.fetch_add(1, std::memory_order_acq_rel);
    KlustersJobPool::start(this, KlustersJobPool::InteractivePriority);   // a clicked matrix cell, slider preview pending
}

void PairXcorrThread::post(QEvent* event)
{
    // Fence against view destruction: ~TemplateMatrixView sets viewDead under
    // the same mutex, so while we hold it and viewDead is false the view is a
    // valid event receiver.
    QMutexLocker lock(&token->postMutex);
    if (token->viewDead) {
        delete event;
        return;
    }
    QApplication::postEvent(&view, event);
}

void PairXcorrThread::run()
{
    process();
    //Retire: this must be the last touch of any shared state.  The
    //synchronous quiesce in stopRunningThreadsSync() and the document-close
    //pool drain treat active == 0 as "no job is inside a file read anymore".
    token->active.fetch_sub(1, std::memory_order_acq_rel);
}

void PairXcorrThread::process()
{
    const int maxShift = std::max(1, nSamp / 4);
    const bool pearson = configuration().getTemplateXcorrPearson();
    const long nSpk  = static_cast<long>(sourceFileIdx.size());

    scores.reserve(static_cast<size_t>(nSpk));

    if (!snapshot) { post(new PairXcorrEvent(*this)); return; }
    const Data::ClusteringSnapshot& snap = *snapshot;   // this epoch's bytes (overlay + pinned descriptor)

    std::vector<int16_t> raw;
    std::vector<float>   sp;

    for (long s = 0; s < nSpk; ++s) {
        if (cancelled()) break;

        const int  fileIdx0 = sourceFileIdx[static_cast<size_t>(s)];
        const bool ok = tmReadSpikeFloat(snap, fileIdx0, nChan, nSamp, raw, sp);

        float sc = ok ? tmNormXcorr(sp, targetMean, maxShift, pearson) : 0.0f;
        scores.emplace_back(fileIdx0, sc);
    }

    post(new PairXcorrEvent(*this));
}
