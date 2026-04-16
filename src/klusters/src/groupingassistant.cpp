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
#include "groupingassistant_gpu.h"

#include <QMap>
#include <QList>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

// GpuDispatch is defined in groupingassistant_gpu_dispatch.cpp
namespace GpuDispatch {
    bool hasGpu();
    int  computeProbabilities(
        const double*, const double*, const double*, const double*,
        double*, const int*, int, int, int, int);
}

GroupingAssistant::GroupingAssistant()
    : existCluster1(false), initIndex(1), haveToStopComputing(false)
{}

GroupingAssistant::~GroupingAssistant()
{}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

Array<double>* GroupingAssistant::computeMeanProbabilities(
        Data& clusteringData,
        QList<int>& clusterList,
        QList<int>& computedClusterList,
        QList<int>& ignoreClusterIndex)
{
    if (haveToStopComputing) return new Array<double>(0, 0);

    Array<double>* probabilities =
        computeProbabilities(clusteringData, clusterList,
                             computedClusterList, ignoreClusterIndex);

    int nbClusters = clusterList.size();
    Array<double>* errorMatrix =
        new Array<double>(static_cast<long>(nbClusters),
                          static_cast<long>(nbClusters));
    errorMatrix->fillWithZeros();

    struct CE { dataType first; dataType nb; int idx; };
    std::vector<CE> entries;
    entries.reserve(static_cast<size_t>(clusterInfoMap->count()));
    {
        Data::ClusterInfoMap::Iterator it;
        int ci = initIndex;
        for (it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it, ++ci)
            entries.push_back({ it.value().firstSpikePosition(),
                                it.value().nbSpikes(), ci });
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

    delete spikesByCluster;
    delete clusterInfoMap;
    delete probabilities;
    return errorMatrix;
}

// ---------------------------------------------------------------------------
// computeProbabilities
// ---------------------------------------------------------------------------

Array<double>* GroupingAssistant::computeProbabilities(
        Data& clusteringData,
        QList<int>& clusterList,
        QList<int>& computedClusterList,
        QList<int>& ignoreClusterIndex)
{
    dataType nbSpikes     = clusteringData.totalNbOfSpikes();
    // Use the actual number of PCA features loaded from the .fetD file
    // (nbDimensions - 1, excluding the timestamp column) rather than
    // totalNbOfPCAs() = nbChannels × nbFeatPerChannel, which over-counts
    // by 1 channel for stderiv sessions (PCA is on nChan-1 channels).
    int      nbDimensions = clusteringData.nbOfDimensionsTotal() - 1;

    clusteringData.duplicate(spikesByCluster, clusterInfoMap);
    if (clusterInfoMap->contains(0)) clusterInfoMap->remove(0);
    int nbClusters = clusterInfoMap->count();

    if (haveToStopComputing) return new Array<double>(0, 0);

    meanCovarianceComputation(nbClusters, nbDimensions, nbSpikes,
                              clusteringData, ignoreClusterIndex);

    Array<double>* probabilities = new Array<double>(nbSpikes, nbClusters);
    probabilities->fillWithZeros();

    double piTerm = log(2.0 * M_PI) * nbDimensions / 2.0;

    // ------------------------------------------------------------------
    // Per-cluster serial precomputation: Cholesky + logTerm + packed L.
    // ------------------------------------------------------------------
    struct ClusterData {
        int      clusterId;
        dataType nbSpikes;
        bool     ignore;
        std::vector<double> L;   // col-major lower-tri, 0-based
        double   logTerm;
    };

    std::vector<ClusterData> cdata;
    cdata.reserve(static_cast<size_t>(nbClusters));

    {
        Data::ClusterInfoMap::Iterator it;
        int ci = 1;
        for (it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it, ++ci) {
            ClusterData cd;
            cd.clusterId = static_cast<int>(it.key());
            cd.nbSpikes  = it.value().nbSpikes();
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
        }
    }

    if (haveToStopComputing) return probabilities;

    // ------------------------------------------------------------------
    // GPU path via dispatcher (CUDA / HIP / SYCL — first available).
    // ------------------------------------------------------------------
    bool usedGpu = false;

    if (GpuDispatch::hasGpu()) {
        std::vector<double> h_features(
            static_cast<size_t>(nbSpikes) * nbDimensions);
        for (dataType s = 1; s <= nbSpikes; ++s)
            for (int d = 1; d <= nbDimensions; ++d)
                h_features[static_cast<size_t>((s-1)*nbDimensions + (d-1))] =
                    clusteringData.features(s, d);

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

        std::vector<double> h_prob(
            static_cast<size_t>(nbSpikes) * nbClusters, 0.0);

        int cluster1Col = 0;
        if (existCluster1) {
            int ci = 0;
            Data::ClusterInfoMap::Iterator it;
            for (it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it, ++ci)
                if (it.key() == 1) { cluster1Col = ci; break; }
        }

        int rc = GpuDispatch::computeProbabilities(
            h_features.data(), h_chol.data(), h_means.data(),
            h_logTerms.data(), h_prob.data(), h_ignore.data(),
            static_cast<int>(nbSpikes), nbClusters, nbDimensions, cluster1Col);

        if (rc == 0) {
            for (dataType s = 1; s <= nbSpikes; ++s)
                for (int c = 1; c <= nbClusters; ++c)
                    (*probabilities)(s, c) =
                        h_prob[static_cast<size_t>((s-1)*nbClusters + (c-1))];
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

    // ------------------------------------------------------------------
    // CPU / OpenMP path.
    // Each cluster ci writes exclusively to column (ci+1) of probabilities.
    // ------------------------------------------------------------------
    struct CSpan { dataType first; dataType last; };
    std::vector<CSpan> spans;
    spans.reserve(static_cast<size_t>(nbClusters));
    {
        Data::ClusterInfoMap::Iterator it;
        for (it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it)
            spans.push_back({ it.value().firstSpikePosition(),
                              it.value().firstSpikePosition() + it.value().nbSpikes() });
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
                double root[64], mahal = 0.0;
                for (int d = 0; d < nbDimensions; ++d) {
                    double s = clusteringData.features(featRow, d + 1)
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
        clusterList.prepend(1);
        computedClusterList.prepend(1);
        for (int i = 0; i < static_cast<int>(ignoreClusterIndex.size()); ++i)
            ignoreClusterIndex[i] += 1;
        ++nbClusters;
        initIndex = 2;
    }

    // Row-wise normalization.
    int cluster1Col1 = initIndex;
    if (existCluster1) {
        int ci = 1;
        Data::ClusterInfoMap::Iterator it;
        for (it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it, ++ci)
            if (it.key() == 1) { cluster1Col1 = ci; break; }
    }

    int clusterIndex = initIndex;
    Data::ClusterInfoMap::Iterator it;
    for (it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it, ++clusterIndex) {
        if (haveToStopComputing) return probabilities;
        if (ignoreClusterIndex.contains(clusterIndex)) continue;
        dataType first = it.value().firstSpikePosition();
        dataType last  = first + it.value().nbSpikes();
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
        Data::ClusterInfoMap::Iterator it;
        int ci = 1;
        for (it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it, ++ci)
            cinfo.push_back({ it.value().firstSpikePosition(),
                              it.value().nbSpikes(), ci });
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

        for (dataType i = c.first; i < last; ++i) {
            dataType featRow = (*spikesByCluster)(1, i);
            for (int j = 1; j <= nbDimensions; ++j)
                means(c.idx, j) +=
                    static_cast<double>(clusteringData.features(featRow, j));
        }
        for (int j = 1; j <= nbDimensions; ++j)
            means(c.idx, j) /= static_cast<double>(c.nb);

        for (dataType i = c.first; i < last; ++i) {
            dataType featRow = (*spikesByCluster)(1, i);
            double dmm[64];
            for (int j = 1; j <= nbDimensions; ++j)
                dmm[j-1] = static_cast<double>(clusteringData.features(featRow, j))
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
