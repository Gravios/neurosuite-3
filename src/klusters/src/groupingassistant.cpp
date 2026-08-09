/***************************************************************************
                          groupingassistant.cpp  -  description
                             -------------------
    begin                : Mon Dec 22 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/
/*
 * Parallelism strategy
 * ====================
 * Three computational phases:
 *
 * 1. meanCovarianceComputation  — OpenMP parallel for over clusters.
 *    Each cluster writes to a disjoint row of means/covariances.
 *
 * 2. computeProbabilities (Mahalanobis) — dominant cost.
 *    GPU path  : GpuDispatch::computeProbabilities() routes to whichever of
 *                CUDA / HIP / SYCL was detected at first call.
 *    CPU path  : OpenMP parallel for over the outer cluster loop.
 *                Each thread writes to a disjoint column of probabilities.
 *
 * 3. computeMeanProbabilities (error matrix) — OpenMP parallel for over rows.
 */

#include "groupingassistant.h"
#include <QDebug>
#include "groupingassistant_gpu.h"

#include <QMap>
#include <QHash>
#include <QSet>
#include <QString>
#include <QList>
#include <QElapsedTimer>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "configuration.h"
#include <vector>
#include <utility>

#ifdef _OPENMP
#  include <omp.h>
#endif

// GpuDispatch is defined in groupingassistant_gpu_dispatch.cpp
namespace GpuDispatch {
    bool hasGpu();
    int  computeProbabilities(
        const double*, const double*, const double*, const double*,
        double*, const int*, int, int, int, int);
    int  computeErrorMatrix(
        const double*, const double*, const double*, const double*, const int*,
        const int*, const int*, const int*, double*, int, int, int, int, int);
}

GroupingAssistant::GroupingAssistant()
    : existCluster1(false), initIndex(1), haveToStopComputing(false)
{}

GroupingAssistant::~GroupingAssistant()
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// prependCluster1Indices
//
// Bookkeeping for the synthetic cluster-1 column, shared by the three places
// that insert one.
//
// When the model contains no cluster 1 a zero column is prepended so the matrix
// always has a noise reference at a known position.  Everything addressed by
// POSITION then shifts by one: the cluster and computed lists gain a leading 1,
// every value already in ignoreClusterIndex refers to a column one further
// right, and the base index for subsequent loops becomes 2.
//
// It was written out three times.  The consequence of that was not theoretical:
// cluster1Col1 -- which the underflow rule uses to decide where a spike's
// probability mass goes -- was wrong in both of the full copies and had to be
// fixed in both.  One function means the next correction lands everywhere.
//
// The GPU path shares only this part; it has no probabilities array to widen,
// because the device produced the error matrix directly.  So the array work
// stays at the call sites and only the indexing lives here.
// ---------------------------------------------------------------------------
static void prependCluster1Indices(QList<int>& clusterList,
                                   QList<int>& computedClusterList,
                                   QList<int>& ignoreClusterIndex,
                                   int& initIndex)
{
    clusterList.prepend(1);
    computedClusterList.prepend(1);
    for (int i = 0; i < static_cast<int>(ignoreClusterIndex.size()); ++i)
        ignoreClusterIndex[i] += 1;
    initIndex = 2;
}

Array<double>* GroupingAssistant::computeMeanProbabilities(
        Data& clusteringData,
        QList<int>& clusterList,
        QList<int>& computedClusterList,
        QList<int>& ignoreClusterIndex)
{
    if (haveToStopComputing) return new Array<double>(0, 0);

    QElapsedTimer emxTmg;
    const bool emxTiming = qEnvironmentVariableIntValue("NS3_ERRORMATRIX_TIMING") != 0;
    if (emxTiming) emxTmg.start();
    Array<double>* gpuErrorMatrix = nullptr;
    Array<double>* probabilities =
        computeProbabilities(clusteringData, clusterList,
                             computedClusterList, ignoreClusterIndex,
                             &gpuErrorMatrix);
    const qint64 emxMsProb = emxTiming ? emxTmg.restart() : 0;

    // GPU-side aggregation produced the error matrix directly: no host
    // materialization of the ~15 GB posteriors and no CPU aggregation pass.
    if (gpuErrorMatrix) {
        if (emxTiming)
            fprintf(stderr,
                "[errormatrix-timing] host: probabilities=%lld ms aggregate=0 ms (gpu-agg)\n",
                static_cast<long long>(emxMsProb));
        delete spikesByCluster; delete clusterInfoMap;
        spikesByCluster = nullptr; clusterInfoMap = nullptr;
        return gpuErrorMatrix;
    }

    // Computed once and shared by every consumer below -- see the note in
    // computeMeanProbabilitiesIncremental.
    const QVector<ModelEntry> model = buildModelIndex(clusterInfoMap);

    int nbClusters = clusterList.size();

    // Why an error matrix can come out all zeros, stated so the log distinguishes
    // the three cases instead of them looking identical on screen:
    //
    //   ignored == model    every cluster was skipped.  ignoreClusterIndex is
    //                       appended to when c.nb <= nbDimensions in the covariance
    //                       pass and when cholesky() fails in the model build; an
    //                       ignored cluster is skipped in BOTH the row loop and the
    //                       column loop, so if all of them are ignored every cell
    //                       stays at its fillWithZeros value.  With a scoped model
    //                       of a handful of children and nbDimensions in the tens,
    //                       the spike-count test is the one to suspect first.
    //   model == 0          nothing to compute over; no cell is ever written.
    //   neither             cells were written and the sums were zero, which puts
    //                       the fault upstream in computeProbabilities.
    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug().noquote()
            << "[emx] incremental: model=" << model.size()
            << " clusterList=" << clusterList.size()
            << " nbClusters="  << nbClusters
            << " initIndex="   << initIndex
            << " ignored="     << ignoreClusterIndex.size()
            << " computed="    << computedClusterList.size();

    Array<double>* errorMatrix =
        new Array<double>(static_cast<long>(nbClusters),
                          static_cast<long>(nbClusters));
    errorMatrix->fillWithZeros();

    struct CE { dataType first; dataType nb; int idx; };
    std::vector<CE> entries;
    entries.reserve(static_cast<size_t>(model.size()));
    {
        int ci = initIndex;
        for (const ModelEntry& me : model)
            entries.push_back({ me.first, me.nb, ci++ });
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none) \
    shared(entries, ignoreClusterIndex, probabilities, errorMatrix, nbClusters)
#endif
    for (int ei = 0; ei < static_cast<int>(entries.size()); ++ei) {
        if (haveToStopComputing) continue;
        const CE& e = entries[static_cast<size_t>(ei)];
        if (ignoreClusterIndex.contains(e.idx)) continue;
        dataType last = e.first + e.nb;
        for (int ci2 = initIndex; ci2 <= nbClusters; ++ci2) {
            if (ignoreClusterIndex.contains(ci2)) continue;
            if (haveToStopComputing) break;
            double sum = 0.0;
            for (dataType i = e.first; i < last; ++i)
                sum += (*probabilities)((*spikesByCluster)(1, i), ci2);
            (*errorMatrix)(e.idx, ci2) = sum / static_cast<double>(e.nb);
        }
    }

    for (int ci = 1; ci <= nbClusters; ++ci)
        (*errorMatrix)(ci, ci) = 0.0;

    if (emxTiming)
        fprintf(stderr,
            "[errormatrix-timing] host: probabilities=%lld ms aggregate=%lld ms\n",
            static_cast<long long>(emxMsProb),
            static_cast<long long>(emxTmg.elapsed()));

    delete spikesByCluster;
    delete clusterInfoMap;
    delete probabilities;
    return errorMatrix;
}

// ---------------------------------------------------------------------------
// computeMeanProbabilitiesIncremental
//
// Opt-in incremental variant of computeMeanProbabilities.  The posteriors are
// row-normalised PER SPIKE across ALL clusters, so a single edit shifts every
// cell's value via the denominator — the reuse therefore happens at the RAW
// (pre-normalisation) per-cluster-column level, NOT at the error-matrix cell
// level.  For each cluster we either recompute its raw column (cluster in
// changedIds, or new, or size-mismatched vs the cache) or copy the cached raw
// column verbatim (membership unchanged => model unchanged => identical column).
// The (cheap, O(nbClusters*nbSpikes)) row-normalisation and error-matrix
// aggregation are then redone IN FULL, so the denominator is always correct and
// the result is numerically identical to the full path.
//
// Model building (meanCovarianceComputation + cholesky) and every numeric loop
// below are the SAME code the full path uses; only the raw fill is scoped.  On
// any precondition miss this returns nullptr and the caller falls back to the
// full path.  CPU-only by construction: the GPU path returns already-normalised
// probabilities, so it exposes no raw columns to cache.
// ---------------------------------------------------------------------------
Array<double>* GroupingAssistant::computeMeanProbabilitiesIncremental(
        Data& clusteringData,
        QList<int>& clusterList, QList<int>& computedClusterList,
        QList<int>& ignoreClusterIndex,
        const Array<double>* prevRaw, const QList<int>& prevRawIds,
        const QList<int>& prevRawSizes, int prevNbDimensions,
        const QSet<int>& changedIds,
        Array<double>** outRaw, QList<int>* outRawIds, QList<int>* outRawSizes,
        int* outNbReused, bool verifyReuse)
{
    if (outRaw)      *outRaw = nullptr;
    if (outRawIds)   outRawIds->clear();
    if (outRawSizes) outRawSizes->clear();
    if (outNbReused) *outNbReused = 0;
    if (haveToStopComputing) return nullptr;

    const dataType nbSpikes     = clusteringData.totalNbOfSpikes();
    // Model dimensionality: the active feature subspace (all dimensions unless
    // setActiveDimensions() restricted it).  The incremental cache's
    // prevNbDimensions check below then invalidates it automatically whenever
    // the selection changes, since the model geometry changed.
    const int      nbDimensions =
        activeDimCount(clusteringData.nbOfDimensionsTotal() - 1);

    // A cache is USABLE for reuse only if its geometry matches the current
    // session.  When it does not (cold start, session reload, dimensionality
    // change) we still run this path but reuse nothing — every column is
    // recomputed and the fresh raw array is emitted to seed the cache.  This
    // path returns nullptr (=> caller falls back to the full path) only on a
    // hard error below (stop requested, or no clusters).
    const bool cacheUsable =
        prevRaw != nullptr
        && prevRaw->nbOfRows()    == static_cast<long>(nbSpikes)
        && prevRaw->nbOfColumns() == static_cast<long>(prevRawIds.size())
        && prevRawSizes.size()    == prevRawIds.size()
        && prevNbDimensions       == nbDimensions;

    QHash<int,int> prevCol, prevSize;
    if (cacheUsable) {
        for (int c = 0; c < prevRawIds.size(); ++c) {
            prevCol.insert(prevRawIds[c], c + 1);      // 1-based col in prevRaw
            prevSize.insert(prevRawIds[c], prevRawSizes[c]);
        }
    }

    clusteringData.duplicate(spikesByCluster, clusterInfoMap);
    // The model index, computed ONCE for this compute and used everywhere below.
    //
    // It was previously rebuilt at every use -- eighteen calls across four
    // functions, each walking clusterInfoMap and allocating a fresh vector.  The
    // cost was the lesser problem: nothing guaranteed the eighteen results agreed,
    // so two loops indexing the same model could silently disagree if
    // clusterInfoMap or activeClusters moved between them.  That is the same class
    // of fault as the dropped loop counters, and one value shared by every consumer
    // removes it by construction rather than by discipline.
    const QVector<ModelEntry> model = buildModelIndex(clusterInfoMap);

    if (clusterInfoMap->contains(0)) clusterInfoMap->remove(0);
    const int nbClustersReal = model.size();
    if (nbClustersReal < 1 || haveToStopComputing) {
        delete spikesByCluster; delete clusterInfoMap;
        spikesByCluster = nullptr; clusterInfoMap = nullptr;
        return nullptr;
    }

    // Models for every cluster (identical to computeProbabilities; cheap).
    meanCovarianceComputation(model, nbClustersReal, nbDimensions, nbSpikes,
                              clusteringData, ignoreClusterIndex);
    if (haveToStopComputing) {
        delete spikesByCluster; delete clusterInfoMap;
        spikesByCluster = nullptr; clusterInfoMap = nullptr;
        return nullptr;
    }

    const double piTerm = log(2.0 * M_PI) * nbDimensions / 2.0;
    existCluster1 = false;
    initIndex     = 1;

    struct Col { int id; dataType first; dataType nb; bool ignore; std::vector<double> L; double logTerm; };
    std::vector<Col> cols; cols.reserve(static_cast<size_t>(nbClustersReal));
    {
        int ci = 1;
        for (const ModelEntry& me : model) {
            Col cd;
            cd.id     = static_cast<int>(me.id);
            cd.first  = me.first;
            cd.nb     = me.nb;
            // Scope is NOT applied here.  buildModelIndex() has already excluded
            // out-of-scope clusters, so cd.id is in scope by construction and this
            // test can only ever be false -- but ignoreClusterIndex holds POSITIONAL
            // indices into the model, and the model is now the compacted one, so
            // adding a second filter on top of the first is how rows got marked
            // ignored that the caller never asked to ignore.  One place decides
            // membership: the producer.
            cd.ignore = (ignoreClusterIndex.contains(ci) != 0);
            cd.logTerm = 0.0;
            if (cd.id == 1) existCluster1 = true;
            clusterList.append(cd.id);
            if (!cd.ignore) {
                Array<double> chol; chol.setSize(nbDimensions, nbDimensions);
                if (cholesky(chol, nbDimensions, ci)) {
                    ignoreClusterIndex.append(ci); cd.ignore = true;
                } else {
                    computedClusterList.append(cd.id);
                    double logRootDet = 0.0;
                    for (int d = 1; d <= nbDimensions; ++d) logRootDet += log(chol(d, d));
                    double weight = log(static_cast<double>(cd.nb) / static_cast<double>(nbSpikes));
                    cd.logTerm = logRootDet - weight + piTerm;
                    cd.L.assign(static_cast<size_t>(nbDimensions * nbDimensions), 0.0);
                    for (int row = 1; row <= nbDimensions; ++row)
                        for (int col = 1; col <= row; ++col)
                            cd.L[static_cast<size_t>((row-1) + (col-1)*nbDimensions)] = chol(row, col);
                }
            }
            cols.push_back(std::move(cd));
            ++ci;
        }
    }

    // Raw (pre-normalisation) probabilities: reuse cached columns for unchanged
    // clusters, recompute the rest with the exact CPU-path Mahalanobis fill.
    Array<double>* raw = new Array<double>(nbSpikes, nbClustersReal);
    raw->fillWithZeros();

    std::vector<std::pair<dataType,dataType>> spans; spans.reserve(static_cast<size_t>(nbClustersReal));
    for (const Col& c : cols) spans.push_back({ c.first, c.first + c.nb });

    // Spikes belonging to CHANGED clusters may have had their .fet features
    // rewritten in place (an auto-realign of the just-merged cluster rewrites its
    // feature rows via Data::updateFeatureRow).  A changed cluster's own column is
    // recomputed below, but its spikes ALSO appear as rows against every OTHER
    // (reused) column — and a reused column is copied wholesale from the cache, so
    // those spikes' entries there are stale.  Collect the changed clusters' spike
    // spans once so each reused column can refresh exactly those spikes (the
    // realigned cluster's ROW), leaving the rest of the column as the valid copy.
    std::vector<std::pair<dataType,dataType>> changedSpans;
    for (const Col& c : cols)
        if (changedIds.contains(c.id))
            changedSpans.push_back({ c.first, c.first + c.nb });

    // Self-check accumulator (verify mode only): clusters whose reused column was
    // found stale — i.e. their spikes' features moved without the cluster entering
    // changedIds.  A correctly wired feature-changing op (merge, nudge, realign)
    // always registers, so a non-empty set at the end means an operation forgot to.
    QSet<int> unregisteredReuse;

    int nbReused = 0;
    // Parallelise the raw-column computation across cores.  Each iteration writes
    // exclusively to its own column ci1 of `raw` (distinct per cjIdx), exactly like
    // the full path's per-column loop, so the fills do not race.  This is the
    // dominant cost of a cold seed (every column recomputed with zero reuse), so
    // spreading it over all cores is what lets the background cache seed finish
    // quickly.  default(shared) is used deliberately: every per-iteration value is
    // declared inside the loop (hence private), the only cross-iteration writes are
    // nbReused (reduction) and, under verify, unregisteredReuse (critical), and
    // everything else is read-only shared.  break is not allowed on a parallel for,
    // so the stop check skips the iteration instead.
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(shared) reduction(+:nbReused)
#endif
    for (int cjIdx = 0; cjIdx < nbClustersReal; ++cjIdx) {
        if (haveToStopComputing) continue;
        const Col& cj = cols[static_cast<size_t>(cjIdx)];
        const int  ci1 = cjIdx + 1;                        // 1-based column
        if (cj.ignore) continue;                           // stays all-zero (matches full path)

        // Per-iteration (hence per-thread) Mahalanobis scratch, sized to the
        // actual feature dimensionality.  Replaces a fixed double[64] that
        // overran the stack for high-density probes (nbDimensions = nCh*nComp
        // can exceed 64); reused across this column's inner spike loops.
        std::vector<double> root(static_cast<size_t>(nbDimensions));

        const bool reusable =
            !changedIds.contains(cj.id)
            && prevCol.contains(cj.id)
            && prevSize.value(cj.id) == static_cast<int>(cj.nb);
        if (reusable) {
            const int srcCol = prevCol.value(cj.id);
            for (dataType s = 1; s <= nbSpikes; ++s)
                (*raw)(s, ci1) = (*prevRaw)(s, srcCol);
            ++nbReused;
            // Correct the copied column for the changed clusters' spikes, whose
            // features may have moved (realign): recompute those rows under this
            // reused column's own (unchanged) model.  Same Mahalanobis as the full
            // fill below; only the set of spikes is scoped to changedSpans.
            const double* L    = cj.L.data();
            const double  logT = cj.logTerm;
            for (const auto& span : changedSpans) {
                for (dataType si = span.first; si < span.second; ++si) {
                    const dataType featRow = (*spikesByCluster)(1, si);
                    double mahal = 0.0;
                    for (int d = 0; d < nbDimensions; ++d) {
                        double sv = clusteringData.features(featRow, dimCol(d + 1)) - means(ci1, d + 1);
                        for (int j = 0; j < d; ++j)
                            sv -= L[static_cast<size_t>(d + j*nbDimensions)] * root[j];
                        root[d] = sv / L[static_cast<size_t>(d + d*nbDimensions)];
                        mahal  += root[d] * root[d];
                    }
                    (*raw)(featRow, ci1) = exp(-0.5 * (mahal + logT));
                }
            }

            if (verifyReuse) {
                // A reused column must be bit-identical to a fresh recompute for
                // every spike whose cluster is NOT in changedIds (unchanged model
                // AND unchanged features).  Any divergence means that spike's
                // features moved without its cluster being registered as changed —
                // a feature-changing op forgot to emit clusterFeaturesReprojected /
                // mark it modified, and its row would be served stale.  changedIds
                // clusters are skipped: their rows were just refreshed above, so
                // they match fresh by construction.
                for (int cc = 0; cc < nbClustersReal; ++cc) {
                    if (cols[static_cast<size_t>(cc)].ignore) continue;
                    if (changedIds.contains(cols[static_cast<size_t>(cc)].id)) continue;
                    const dataType f0 = spans[static_cast<size_t>(cc)].first;
                    const dataType f1 = spans[static_cast<size_t>(cc)].second;
                    bool flagged = false;
                    for (dataType si = f0; si < f1 && !flagged; ++si) {
                        const dataType featRow = (*spikesByCluster)(1, si);
                        double mahal = 0.0;
                        for (int d = 0; d < nbDimensions; ++d) {
                            double sv = clusteringData.features(featRow, dimCol(d + 1)) - means(ci1, d + 1);
                            for (int j = 0; j < d; ++j)
                                sv -= L[static_cast<size_t>(d + j*nbDimensions)] * root[j];
                            root[d] = sv / L[static_cast<size_t>(d + d*nbDimensions)];
                            mahal  += root[d] * root[d];
                        }
                        const double fresh = exp(-0.5 * (mahal + logT));
                        double diff = fresh - (*raw)(featRow, ci1);
                        if (diff < 0) diff = -diff;
                        if (diff > 1e-9) {              // one stale spike flags the cluster
#ifdef _OPENMP
#pragma omp critical
#endif
                            { unregisteredReuse.insert(cols[static_cast<size_t>(cc)].id); }
                            flagged = true;
                        }
                    }
                }
            }
            continue;
        }

        const double* L    = cj.L.data();
        const double  logT = cj.logTerm;
        for (int ci2 = 0; ci2 < nbClustersReal; ++ci2) {
            if (haveToStopComputing) break;
            if (cols[static_cast<size_t>(ci2)].ignore) continue;
            const dataType first = spans[static_cast<size_t>(ci2)].first;
            const dataType last  = spans[static_cast<size_t>(ci2)].second;
            for (dataType si = first; si < last; ++si) {
                const dataType featRow = (*spikesByCluster)(1, si);
                double mahal = 0.0;
                for (int d = 0; d < nbDimensions; ++d) {
                    double sv = clusteringData.features(featRow, dimCol(d + 1)) - means(ci1, d + 1);
                    for (int j = 0; j < d; ++j)
                        sv -= L[static_cast<size_t>(d + j*nbDimensions)] * root[j];
                    root[d] = sv / L[static_cast<size_t>(d + d*nbDimensions)];
                    mahal  += root[d] * root[d];
                }
                (*raw)(featRow, ci1) = exp(-0.5 * (mahal + logT));
            }
        }
    }
    if (haveToStopComputing) {
        delete raw; delete spikesByCluster; delete clusterInfoMap;
        spikesByCluster = nullptr; clusterInfoMap = nullptr;
        return nullptr;
    }
    if (outNbReused) *outNbReused = nbReused;

    if (verifyReuse && !unregisteredReuse.isEmpty()) {
        QString ids;
        for (int id : unregisteredReuse) ids += QString::number(id) + ' ';
        fprintf(stderr,
            "[errormatrix-incremental] SELF-CHECK FAILED: %d cluster(s) had .fet "
            "feature changes NOT registered in changedIds, so their reused columns "
            "were stale: %s. A feature-changing operation on these clusters did not "
            "notify the error matrix (expected clusterFeaturesReprojected or an edit "
            "signal).\n",
            static_cast<int>(unregisteredReuse.size()),
            ids.trimmed().toUtf8().constData());
    }

    // Hand the raw array (pre cluster-1 prepend) + its id/size lists back for
    // caching, keyed by ascending real cluster id == raw column order.
    if (outRaw)      *outRaw = new Array<double>(*raw);
    if (outRawIds)   *outRawIds = clusterList;
    if (outRawSizes) for (const Col& c : cols) outRawSizes->append(static_cast<int>(c.nb));

    // ---- Post-processing: identical to computeProbabilities + computeMeanProbabilities ----
    int nbClusters = nbClustersReal;
    Array<double>* probabilities = raw;

    // (a) synthetic cluster-1 column when cluster 1 is absent.
    if (!existCluster1) {
        Array<double>* tmp = new Array<double>(nbSpikes, nbClusters + 1);
        tmp->fillWithZeros();
        tmp->copyAndPrependColumn(*probabilities);
        delete probabilities; probabilities = tmp;
        prependCluster1Indices(clusterList, computedClusterList,
                               ignoreClusterIndex, initIndex);
        ++nbClusters;
    }

    // (b) row-wise normalisation (verbatim from computeProbabilities).
    // The column holding cluster 1.
    //
    // When cluster 1 is absent a synthetic all-zero column is PREPENDED, so it
    // sits at column 1 and initIndex becomes 2.  Initialising this to initIndex
    // therefore pointed at column 2 -- the first REAL cluster -- and the
    // underflow rule below assigns a spike's entire probability mass to
    // cluster1Col1.  Every spike whose posteriors underflow, which at 32
    // dimensions is routine, was being handed to whichever cluster happened to be
    // first in the model instead of to noise.
    //
    // Unscoped this never fired: the fiber layer always contains cluster 1, so
    // existCluster1 is true and the search below runs.  A scoped model is the
    // first case in which the model has no cluster 1, which is why this surfaced
    // only with the child-scoped matrices and why the same code was correct for
    // years before them.
    int cluster1Col1 = existCluster1 ? initIndex : 1;
    if (existCluster1) {
        int ci = 1;
        for (const ModelEntry& me : model) {
            if (me.id == 1) { cluster1Col1 = ci; break; }
            ++ci;
        }
    }
    {
        int clusterIndex = initIndex;
        for (const ModelEntry& me : model) {
            const int thisIndex = clusterIndex++;
            if (haveToStopComputing) break;
            if (ignoreClusterIndex.contains(thisIndex)) continue;
            dataType first = me.first;
            dataType last  = first + me.nb;
            for (dataType si = first; si < last; ++si) {
                dataType featRow = (*spikesByCluster)(1, si);
                double sum = 0.0;
                for (int ci2 = initIndex; ci2 <= nbClusters; ++ci2)
                    sum += (*probabilities)(featRow, ci2);
                if (sum == 0.0) { sum = 1.0; (*probabilities)(featRow, cluster1Col1) = 1.0; }
                double inv = 1.0 / sum;
                for (int ci2 = initIndex; ci2 <= nbClusters; ++ci2)
                    (*probabilities)(featRow, ci2) *= inv;
            }
        }
    }
    if (haveToStopComputing) {
        delete probabilities; delete spikesByCluster; delete clusterInfoMap;
        spikesByCluster = nullptr; clusterInfoMap = nullptr;
        return nullptr;
    }

    // (c) error-matrix aggregation (verbatim from computeMeanProbabilities).
    Array<double>* errorMatrix = new Array<double>(nbClusters, nbClusters);
    errorMatrix->fillWithZeros();
    struct CE { dataType first; dataType nb; int idx; };
    std::vector<CE> entries; entries.reserve(static_cast<size_t>(model.size()));
    {
        int ci = initIndex;
        for (const ModelEntry& me : model)
            entries.push_back({ me.first, me.nb, ci++ });
    }
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none) \
    shared(entries, ignoreClusterIndex, probabilities, errorMatrix, nbClusters)
#endif
    for (int ei = 0; ei < static_cast<int>(entries.size()); ++ei) {
        if (haveToStopComputing) continue;
        const CE& e = entries[static_cast<size_t>(ei)];
        if (ignoreClusterIndex.contains(e.idx)) continue;
        dataType last = e.first + e.nb;
        for (int ci2 = initIndex; ci2 <= nbClusters; ++ci2) {
            if (ignoreClusterIndex.contains(ci2)) continue;
            if (haveToStopComputing) break;
            double sum = 0.0;
            for (dataType i = e.first; i < last; ++i)
                sum += (*probabilities)((*spikesByCluster)(1, i), ci2);
            (*errorMatrix)(e.idx, ci2) = sum / static_cast<double>(e.nb);
        }
    }
    for (int ci = 1; ci <= nbClusters; ++ci)
        (*errorMatrix)(ci, ci) = 0.0;

    delete spikesByCluster; delete clusterInfoMap; delete probabilities;
    spikesByCluster = nullptr; clusterInfoMap = nullptr;
    return errorMatrix;
}

// ---------------------------------------------------------------------------
// computeProbabilities
// ---------------------------------------------------------------------------

Array<double>* GroupingAssistant::computeProbabilities(
        Data& clusteringData,
        QList<int>& clusterList,
        QList<int>& computedClusterList,
        QList<int>& ignoreClusterIndex,
        Array<double>** errorMatrixOut)
{
    dataType nbSpikes     = clusteringData.totalNbOfSpikes();
    // Use the actual number of PCA features loaded from the .fetD file
    // (nbDimensions - 1, excluding the timestamp column) rather than
    // totalNbOfPCAs() = nbChannels × nbFeatPerChannel, which over-counts
    // by 1 channel for stderiv sessions (PCA is on nChan-1 channels).
    int      nbDimensions =
        activeDimCount(clusteringData.nbOfDimensionsTotal() - 1);

    QElapsedTimer pt;
    const bool pTiming = qEnvironmentVariableIntValue("NS3_ERRORMATRIX_TIMING") != 0;
    qint64 t_dup=0, t_mean=0, t_alloc=0, t_chol=0, t_feat=0, t_gpu=0;
    if (pTiming) pt.start();

    clusteringData.duplicate(spikesByCluster, clusterInfoMap);
    // The model index, computed ONCE for this compute and used everywhere below.
    //
    // It was previously rebuilt at every use -- eighteen calls across four
    // functions, each walking clusterInfoMap and allocating a fresh vector.  The
    // cost was the lesser problem: nothing guaranteed the eighteen results agreed,
    // so two loops indexing the same model could silently disagree if
    // clusterInfoMap or activeClusters moved between them.  That is the same class
    // of fault as the dropped loop counters, and one value shared by every consumer
    // removes it by construction rather than by discipline.
    const QVector<ModelEntry> model = buildModelIndex(clusterInfoMap);

    if (clusterInfoMap->contains(0)) clusterInfoMap->remove(0);
    int nbClusters = model.size();
    if (pTiming) t_dup = pt.restart();

    if (haveToStopComputing) return new Array<double>(0, 0);

    meanCovarianceComputation(model, nbClusters, nbDimensions, nbSpikes,
                              clusteringData, ignoreClusterIndex);
    if (pTiming) t_mean = pt.restart();

    // probabilities (the ~15 GB nSpikes x nClusters intermediate) is allocated
    // lazily: the GPU-side aggregation path below never materializes it; only the
    // full-posteriors GPU path and the CPU fallback allocate it.
    Array<double>* probabilities = nullptr;

    double piTerm = log(2.0 * M_PI) * nbDimensions / 2.0;

    // ------------------------------------------------------------------
    // Per-cluster serial precomputation: Cholesky + logTerm + packed L.
    // ------------------------------------------------------------------
    struct ClusterData {
        int      clusterId = 0;
        dataType nbSpikes  = 0;
        bool     ignore    = false;
        std::vector<double> L;   // col-major lower-tri, 0-based (default-empty)
        double   logTerm   = 0.0;
    };

    std::vector<ClusterData> cdata;
    cdata.reserve(static_cast<size_t>(nbClusters));

    {
        int ci = 1;
        for (const ModelEntry& me : model) {
            ClusterData cd;
            cd.clusterId = static_cast<int>(me.id);
            cd.nbSpikes  = me.nb;
            // See the incremental path: membership is the producer's job alone.
            cd.ignore    = (ignoreClusterIndex.contains(ci) != 0);

            if (cd.clusterId == 1) existCluster1 = true;
            clusterList.append(cd.clusterId);

            if (!cd.ignore) {
                Array<double> choleskyDecomp;
                choleskyDecomp.setSize(nbDimensions, nbDimensions);
                if (cholesky(choleskyDecomp, nbDimensions, ci)) {
                    ignoreClusterIndex.append(ci);
                    cd.ignore = true;
                } else {
                    computedClusterList.append(cd.clusterId);
                    double logRootDet = 0.0;
                    for (int d = 1; d <= nbDimensions; ++d)
                        logRootDet += log(choleskyDecomp(d, d));
                    double weight = log(static_cast<double>(cd.nbSpikes) /
                                       static_cast<double>(nbSpikes));
                    cd.logTerm = logRootDet - weight + piTerm;

                    cd.L.assign(static_cast<size_t>(nbDimensions * nbDimensions), 0.0);
                    for (int row = 1; row <= nbDimensions; ++row)
                        for (int col = 1; col <= row; ++col)
                            cd.L[static_cast<size_t>((row-1) + (col-1)*nbDimensions)] =
                                choleskyDecomp(row, col);
                }
            }
            cdata.push_back(std::move(cd));
            ++ci;
        }
    }

    if (qEnvironmentVariableIsSet("NS3_VERBOSE"))
        qDebug().noquote()
            << "[emx] full: model="  << model.size()
            << " clusterList="       << clusterList.size()
            << " nbClusters="        << nbClusters
            << " nbDimensions="      << nbDimensions
            << " ignored="           << ignoreClusterIndex.size()
            << " computed="          << computedClusterList.size();

    if (haveToStopComputing) return new Array<double>(0, 0);

    // ------------------------------------------------------------------
    // GPU path via dispatcher (CUDA / HIP / SYCL — first available).
    // ------------------------------------------------------------------
    if (pTiming) t_chol = pt.restart();
    bool usedGpu = false;

    if (GpuDispatch::hasGpu()) {
        std::vector<double> h_features(
            static_cast<size_t>(nbSpikes) * nbDimensions);
        for (dataType s = 1; s <= nbSpikes; ++s)
            for (int d = 1; d <= nbDimensions; ++d)
                h_features[static_cast<size_t>((s-1)*nbDimensions + (d-1))] =
                    clusteringData.features(s, dimCol(d));

        std::vector<double> h_chol(
            static_cast<size_t>(nbClusters) * nbDimensions * nbDimensions, 0.0);
        std::vector<double> h_means(
            static_cast<size_t>(nbClusters) * nbDimensions, 0.0);
        std::vector<double> h_logTerms(static_cast<size_t>(nbClusters), 0.0);
        std::vector<int>    h_ignore(static_cast<size_t>(nbClusters), 1);

        for (int ci = 0; ci < static_cast<int>(cdata.size()); ++ci) {
            const ClusterData& cd = cdata[static_cast<size_t>(ci)];
            if (cd.ignore) continue;
            h_ignore[static_cast<size_t>(ci)]   = 0;
            h_logTerms[static_cast<size_t>(ci)] = cd.logTerm;
            for (int d = 1; d <= nbDimensions; ++d)
                h_means[static_cast<size_t>(ci*nbDimensions + (d-1))] =
                    means(ci + 1, d);
            memcpy(h_chol.data() + ci * nbDimensions * nbDimensions,
                   cd.L.data(),
                   static_cast<size_t>(nbDimensions * nbDimensions) * sizeof(double));
        }

        int cluster1Col = 0;
        if (existCluster1) {
            int ci = 0;
            for (const ModelEntry& me : model) {
                if (me.id == 1) { cluster1Col = ci; break; }
                ++ci;
            }
        }

        // === GPU-side aggregation (fused posteriors + reduction) ==========
        // Compute posteriors AND reduce them into the nbClusters^2 error matrix
        // on the device; copy back only ~99 MB, never the ~15 GB intermediate.
        // Requested via errorMatrixOut; falls through to the full host path if
        // unavailable (HIP/SYCL) or on GPU failure.
        if (errorMatrixOut) {
            QElapsedTimer at; if (pTiming) at.start();
            std::vector<int> h_featRow(static_cast<size_t>(nbSpikes));
            for (dataType p = 1; p <= nbSpikes; ++p)
                h_featRow[static_cast<size_t>(p - 1)] =
                    static_cast<int>((*spikesByCluster)(1, p)) - 1;   // 0-based feature row
            std::vector<int> h_first(static_cast<size_t>(nbClusters));
            std::vector<int> h_nb   (static_cast<size_t>(nbClusters));
            {
                int c0 = 0;
                for (const ModelEntry& me : model) {
                    h_first[static_cast<size_t>(c0)] =
                        static_cast<int>(me.first) - 1;  // 0-based
                    h_nb[static_cast<size_t>(c0)] =
                        static_cast<int>(me.nb);
                    ++c0;
                }
            }
            std::vector<double> errGpu(static_cast<size_t>(nbClusters) * nbClusters);
            const qint64 aPrep = pTiming ? at.restart() : 0;

            // FP32 (fast, default) vs FP64 (exact) is user-selectable; the
            // NS3_ERRORMATRIX_LOWPRECISION env var overrides the preference.
            const bool lowPrecision =
                qEnvironmentVariableIsSet("NS3_ERRORMATRIX_LOWPRECISION")
                    ? (qEnvironmentVariableIntValue("NS3_ERRORMATRIX_LOWPRECISION") != 0)
                    : configuration().getErrorMatrixLowPrecision();
            int rcAgg = GpuDispatch::computeErrorMatrix(
                h_features.data(), h_chol.data(), h_means.data(), h_logTerms.data(),
                h_ignore.data(), h_featRow.data(), h_first.data(), h_nb.data(),
                errGpu.data(),
                static_cast<int>(nbSpikes), nbClusters, nbDimensions, cluster1Col,
                lowPrecision ? 1 : 0);
            const qint64 aGpu = pTiming ? at.restart() : 0;

            if (rcAgg == 0) {
                // Place the GPU-space [count x count] block into the final matrix,
                // prepending a synthetic zero row/col for cluster 1 when absent
                // (initIndex = 2), else mapping directly (initIndex = 1).
                int initLocal = 1;
                if (!existCluster1) {
                    prependCluster1Indices(clusterList, computedClusterList,
                                           ignoreClusterIndex, initLocal);
                }
                initIndex = initLocal;
                const int dim = clusterList.size();
                Array<double>* errorMatrix = new Array<double>(dim, dim);
                errorMatrix->fillWithZeros();
                for (int i0 = 0; i0 < nbClusters; ++i0)
                    for (int j0 = 0; j0 < nbClusters; ++j0)
                        (*errorMatrix)(initLocal + i0, initLocal + j0) =
                            errGpu[static_cast<size_t>(i0) * nbClusters + j0];
                for (int ci = 1; ci <= dim; ++ci) (*errorMatrix)(ci, ci) = 0.0;
                if (pTiming)
                    fprintf(stderr,
                        "[errormatrix-timing] agg-prep: duplicate=%lld meanCov=%lld "
                        "cholesky=%lld hostArrays=%lld gpu=%lld ms\n",
                        static_cast<long long>(t_dup),  static_cast<long long>(t_mean),
                        static_cast<long long>(t_chol), static_cast<long long>(aPrep),
                        static_cast<long long>(aGpu));
                *errorMatrixOut = errorMatrix;
                return nullptr;
            }
            fprintf(stderr, "[klusters] GPU error-matrix aggregation failed - "
                            "falling back to full posteriors + host aggregation.\n");
        }

        // Full-posteriors GPU path (also used when errorMatrixOut is null).
        probabilities = new Array<double>(nbSpikes, nbClusters);
        if (pTiming) { t_alloc = pt.restart(); t_feat = pt.restart(); }
        int rc = GpuDispatch::computeProbabilities(
            h_features.data(), h_chol.data(), h_means.data(),
            h_logTerms.data(), probabilities->data(), h_ignore.data(),
            static_cast<int>(nbSpikes), nbClusters, nbDimensions, cluster1Col);
        if (pTiming) t_gpu = pt.restart();

        if (rc == 0) {
            if (pTiming)
                fprintf(stderr,
                    "[errormatrix-timing] prep: duplicate=%lld meanCov=%lld alloc=%lld "
                    "cholesky=%lld hostArrays=%lld gpu=%lld ms\n",
                    static_cast<long long>(t_dup),  static_cast<long long>(t_mean),
                    static_cast<long long>(t_alloc),static_cast<long long>(t_chol),
                    static_cast<long long>(t_feat), static_cast<long long>(t_gpu));
            // The GPU wrote directly into the probabilities buffer: probOut is
            // row-major [spike][cluster] and Array::operator()(s,c) maps to
            // array[(s-1)*nbClusters + (c-1)] — the same layout — so no host
            // copy-back is needed.  (The old element-wise copy over ~nSpikes x
            // nClusters cells ran single-threaded and dominated the compute.)
            usedGpu = true;

            if (!existCluster1) {
                // Cluster 1 (noise/unsorted) was absent from clusterInfoMap.
                // The downstream ErrorMatrixView always expects column 1 to hold
                // cluster 1's probabilities, so prepend a synthetic all-zero column
                // and shift all other indices up by 1.  The row-normalisation loop
                // is skipped for the GPU path (probabilities are already normalised
                // by the kernel), so initIndex only affects computeMeanProbabilities.
                Array<double>* tmp = new Array<double>(nbSpikes, nbClusters + 1);
                tmp->fillWithZeros();
                tmp->copyAndPrependColumn(*probabilities);
                delete probabilities;
                probabilities = tmp;
                clusterList.prepend(1);
                computedClusterList.prepend(1);
                for (int i = 0; i < static_cast<int>(ignoreClusterIndex.size()); ++i)
                    ignoreClusterIndex[i] += 1;
                initIndex = 2;
            }
            return probabilities;
        }
        // rc != 0: GPU failed, fall through to CPU path.
        fprintf(stderr, "[klusters] GPU computation failed — falling back to OpenMP CPU.\n");
        probabilities->fillWithZeros();
    }

    (void)usedGpu;

    if (!probabilities) {
        probabilities = new Array<double>(nbSpikes, nbClusters);
        probabilities->fillWithZeros();
    }

    // ------------------------------------------------------------------
    // CPU / OpenMP path.
    // Each cluster ci writes exclusively to column (ci+1) of probabilities.
    // ------------------------------------------------------------------
    struct CSpan { dataType first; dataType last; };
    std::vector<CSpan> spans;
    spans.reserve(static_cast<size_t>(nbClusters));
    {
        for (const ModelEntry& me : model)
            spans.push_back({ me.first, me.first + me.nb });
    }

    int nbClustersInt = nbClusters;

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none) \
    shared(cdata, spans, probabilities, spikesByCluster, clusteringData, \
           nbClustersInt, nbDimensions)
#endif
    for (int ci = 0; ci < static_cast<int>(cdata.size()); ++ci) {
        if (haveToStopComputing) continue;
        const ClusterData& cd = cdata[static_cast<size_t>(ci)];
        if (cd.ignore) continue;

        // Per-iteration (hence per-thread) Mahalanobis scratch, sized to
        // nbDimensions; replaces a fixed double[64] that overran for
        // nbDimensions > 64.
        std::vector<double> root(static_cast<size_t>(nbDimensions));

        const double* L    = cd.L.data();
        double        logT = cd.logTerm;
        int           ci1  = ci + 1;

        for (int ci2 = 0; ci2 < nbClustersInt; ++ci2) {
            if (haveToStopComputing) break;
            if (cdata[static_cast<size_t>(ci2)].ignore) continue;
            dataType first = spans[static_cast<size_t>(ci2)].first;
            dataType last  = spans[static_cast<size_t>(ci2)].last;
            for (dataType si = first; si < last; ++si) {
                dataType featRow = (*spikesByCluster)(1, si);
                double mahal = 0.0;
                for (int d = 0; d < nbDimensions; ++d) {
                    double s = clusteringData.features(featRow, dimCol(d + 1))
                               - means(ci1, d + 1);
                    for (int j = 0; j < d; ++j)
                        s -= L[static_cast<size_t>(d + j*nbDimensions)] * root[j];
                    root[d] = s / L[static_cast<size_t>(d + d*nbDimensions)];
                    mahal  += root[d] * root[d];
                }
                (*probabilities)(featRow, ci1) = exp(-0.5 * (mahal + logT));
            }
        }
    }

    if (haveToStopComputing) return probabilities;

    // Same cluster-1 prepend as the GPU path above.  When cluster 1 was absent,
    // insert a synthetic zero column so the probabilities array has the same
    // layout regardless of whether the session has a noise cluster.
    // initIndex is set to 2 so the row-normalisation loop below skips column 1.
    if (!existCluster1) {
        Array<double>* tmp = new Array<double>(nbSpikes, nbClusters + 1);
        tmp->fillWithZeros();
        tmp->copyAndPrependColumn(*probabilities);
        delete probabilities;
        probabilities = tmp;
        prependCluster1Indices(clusterList, computedClusterList,
                               ignoreClusterIndex, initIndex);
        ++nbClusters;
    }

    // Row-wise normalization.
    // The column holding cluster 1.
    //
    // When cluster 1 is absent a synthetic all-zero column is PREPENDED, so it
    // sits at column 1 and initIndex becomes 2.  Initialising this to initIndex
    // therefore pointed at column 2 -- the first REAL cluster -- and the
    // underflow rule below assigns a spike's entire probability mass to
    // cluster1Col1.  Every spike whose posteriors underflow, which at 32
    // dimensions is routine, was being handed to whichever cluster happened to be
    // first in the model instead of to noise.
    //
    // Unscoped this never fired: the fiber layer always contains cluster 1, so
    // existCluster1 is true and the search below runs.  A scoped model is the
    // first case in which the model has no cluster 1, which is why this surfaced
    // only with the child-scoped matrices and why the same code was correct for
    // years before them.
    int cluster1Col1 = existCluster1 ? initIndex : 1;
    if (existCluster1) {
        int ci = 1;
        for (const ModelEntry& me : model) {
            if (me.id == 1) { cluster1Col1 = ci; break; }
            ++ci;
        }
    }

    int clusterIndex = initIndex;
    for (const ModelEntry& me : model) {
        const int thisIndex = clusterIndex++;
        if (haveToStopComputing) return probabilities;
        if (ignoreClusterIndex.contains(thisIndex)) continue;
        dataType first = me.first;
        dataType last  = first + me.nb;
        for (dataType si = first; si < last; ++si) {
            dataType featRow = (*spikesByCluster)(1, si);
            double sum = 0.0;
            for (int ci2 = initIndex; ci2 <= nbClusters; ++ci2)
                sum += (*probabilities)(featRow, ci2);
            if (sum == 0.0) { sum = 1.0; (*probabilities)(featRow, cluster1Col1) = 1.0; }
            double inv = 1.0 / sum;
            for (int ci2 = initIndex; ci2 <= nbClusters; ++ci2)
                (*probabilities)(featRow, ci2) *= inv;
        }
    }

    return probabilities;
}

// ---------------------------------------------------------------------------
// cholesky
// ---------------------------------------------------------------------------
int GroupingAssistant::cholesky(Array<double>& out, int nbDimensions, int clusterIndex)
{
    for (int i = 1; i <= nbDimensions; ++i) {
        for (int j = i; j <= nbDimensions; ++j) {
            double sum = covariances(clusterIndex, (i-1)*nbDimensions + j);
            for (int k = i-1; k >= 1; --k) sum -= out(i,k) * out(j,k);
            if (i == j) {
                if (sum <= 0.0) return 1;
                out(i,i) = sqrt(sum);
            } else {
                out(j,i) = sum / out(i,i);
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// meanCovarianceComputation — OpenMP parallel over clusters
// ---------------------------------------------------------------------------
void GroupingAssistant::meanCovarianceComputation(
        const QVector<ModelEntry>& model,
        int nbClusters, int nbDimensions, dataType /*nbSpikes*/,
        Data& clusteringData, QList<int>& ignoreClusterIndex)
{
    means.setSize(nbClusters, nbDimensions);
    means.fillWithZeros();
    covariances.setSize(nbClusters, nbDimensions * nbDimensions);
    covariances.fillWithZeros();

    struct CInfo { dataType first; dataType nb; int idx; };
    std::vector<CInfo> cinfo;
    cinfo.reserve(static_cast<size_t>(nbClusters));
    {
        int ci = 1;
        for (const ModelEntry& me : model)
            cinfo.push_back({ me.first, me.nb, ci++ });
    }

#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none) \
    shared(cinfo, ignoreClusterIndex, clusteringData, nbDimensions)
#endif
    for (int ei = 0; ei < static_cast<int>(cinfo.size()); ++ei) {
        if (haveToStopComputing) continue;
        const CInfo& c = cinfo[static_cast<size_t>(ei)];

        if (c.nb <= static_cast<dataType>(nbDimensions)) {
#ifdef _OPENMP
#pragma omp critical
#endif
            ignoreClusterIndex.append(c.idx);
            continue;
        }

        dataType last = c.first + c.nb;

        // Per-cluster (hence per-thread) mean-subtracted feature scratch,
        // sized to nbDimensions; replaces a fixed double[64] that overran
        // for nbDimensions > 64.
        std::vector<double> dmm(static_cast<size_t>(nbDimensions));

        for (dataType i = c.first; i < last; ++i) {
            dataType featRow = (*spikesByCluster)(1, i);
            for (int j = 1; j <= nbDimensions; ++j)
                means(c.idx, j) +=
                    static_cast<double>(clusteringData.features(featRow, dimCol(j)));
        }
        for (int j = 1; j <= nbDimensions; ++j)
            means(c.idx, j) /= static_cast<double>(c.nb);

        for (dataType i = c.first; i < last; ++i) {
            dataType featRow = (*spikesByCluster)(1, i);
            for (int j = 1; j <= nbDimensions; ++j)
                dmm[j-1] = static_cast<double>(clusteringData.features(featRow, dimCol(j)))
                           - means(c.idx, j);
            for (int ii = 1; ii <= nbDimensions; ++ii)
                for (int jj = 1; jj <= nbDimensions; ++jj)
                    covariances(c.idx, (ii-1)*nbDimensions + jj) += dmm[ii-1] * dmm[jj-1];
        }
        double norm = 1.0 / static_cast<double>(c.nb - 1);
        for (int ii = 1; ii <= nbDimensions; ++ii)
            for (int jj = 1; jj <= nbDimensions; ++jj)
                covariances(c.idx, (ii-1)*nbDimensions + jj) *= norm;
    }
}
