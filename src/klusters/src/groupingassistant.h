/***************************************************************************
                          groupingassistant.h  -  description
                             -------------------
    begin                : Mon Dec 22 2003
    copyright            : (C) 2003 by Lynn Hazan
    email                : lynn.hazan@myrealbox.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef GROUPINGASSISTANT_H
#define GROUPINGASSISTANT_H

//include files for the application
#include <vector>
#include "data.h"
#include "array.h"
#include "types.h"

#include <QList>
#include <QSet>


/**
 * Evaluates the fit of the CEM algorithm by computing the probability
 * of missclassification.
 * All the features are use, all PCs except the time.
 * Classifies points in the features as belonging to a mixture of Gaussians
 * which are specified by the example data features and list of clusters.
 *@author Lynn Hazan
 * @since klusters 1.1
 */

class GroupingAssistant {
public:
    explicit GroupingAssistant();
    ~GroupingAssistant();

    /**
 * Evaluates the fit of the CEM algorithm by computing the probability
 * of missclassification.
 * Computes the mean probabilities that spike of cluster c1 actually belongs to c2.
 * @param clusteringData object containing all the document data.
 * @param clusterList output paramater to return the list of clusters computed sorted
 * like in the return array.
 * @param computedClusterList output paramater to return the list of actually computed clusters sorted
 * like in the return array.
 * @param ignoreClusterIndex list of the indexes of the clusters which where not computed, either
 * because they do not have enough spikes or their determinant could not be calculated (their covariance matrix is not positive definite.)
 * @return nbSpikes x nbClusters array giving the posterior
 * probabilities of belonging to each cluster of Fet2, for each point of Fet1.
 */
    Array<double>* computeMeanProbabilities(Data& clusteringData,QList<int>& clusterList,QList<int>& computedClusterList,
                                            QList<int>& ignoreClusterIndex);

    /**Opt-in incremental variant of computeMeanProbabilities.  Reuses the RAW
     * (pre-normalisation) per-cluster probability columns for clusters whose
     * membership is unchanged since the cache (prevRaw / prevRawIds / prevRawSizes),
     * recomputing raw columns only for clusters in changedIds (or absent from the
     * cache, or size-mismatched), then redoing the full per-spike normalisation and
     * error-matrix aggregation.  Because the posteriors are normalised per spike
     * across ALL clusters, the reuse is at the raw-column level, never the cell
     * level; the result is numerically identical to the full path.  Returns the
     * error matrix and, via outRaw / outRawIds / outRawSizes, the new raw array for
     * the caller to cache; outNbReused reports how many columns were reused.
     * Returns nullptr (caller should fall back to computeMeanProbabilities) on any
     * precondition miss.  CPU-only: the GPU path yields no raw columns to cache.*/
    Array<double>* computeMeanProbabilitiesIncremental(
        Data& clusteringData, QList<int>& clusterList, QList<int>& computedClusterList,
        QList<int>& ignoreClusterIndex,
        const Array<double>* prevRaw, const QList<int>& prevRawIds,
        const QList<int>& prevRawSizes, int prevNbDimensions,
        const QSet<int>& changedIds,
        Array<double>** outRaw, QList<int>* outRawIds, QList<int>* outRawSizes,
        int* outNbReused, bool verifyReuse);

    /**Asks the GroupingAssistant to stop his work as soon as possible.*/
    inline void stopComputing(){haveToStopComputing = true;}

private:
    /**Array containing the covariances of the clusters computed.*/
    Array<double> covariances;

    /**Array containing the means of the clusters computed.*/
    Array<double> means;

    /**
  * Copy of the @ref Data::spikesByCluster, a two line array which contains sorted by cluster numbers:
  * the row index of the spike in features array.
  * the id of the cluster.
  */
    SortableTable* spikesByCluster = nullptr;

    /**Copy of the @ref Data::clusterInfoMap, contains ClusterInfo(s)
  * key: cluster number
  * value: a ClusterInfo (which gives:
  * the index of the first spike in spikesByCluster and the number of spikes for a given cluster).
  */
    Data::ClusterInfoMap* clusterInfoMap = nullptr;

    /**True if cluster 1 (noise/unsorted) is present in the clusterInfoMap being computed.
     * When false, a synthetic all-zero column is prepended to the probabilities array
     * after computation so the caller always receives cluster 1 at column 1.*/
    bool existCluster1;

    /**First 1-based column index into the probabilities array that corresponds to
     * a real cluster.  Normally 1, but set to 2 when cluster 1 was absent and a
     * synthetic zero column was prepended (existCluster1 == false after computation).
     * The row-normalisation loop uses initIndex as its lower bound so it skips the
     * synthetic column.*/
    int initIndex;

    /**True if has been asked to stop the computation, false otherwise.*/
    bool haveToStopComputing;

    /**
 * Computes an array giving the posterior probabilities of belonging
 * to each cluster, for each spike.
 * @param clusteringData object containing all the document data.
 * @param clusterList output paramater to return the list of clusters computed sorted
 * like in the return array.
 * @param computedClusterList output paramater to return the list of actually computed clusters sorted
 * like in the return array.
 * @param ignoreClusterIndex list of the indexes of the clusters which where not computed, either
 * because they do not have enough spikes or their determinant could not be calculated (their covariance matrix is not positive definite.)
 * @return nbSpikes x nbClusters array giving the posterior
 * probabilities of belonging to each cluster of Fet2, for each point of Fet1.
 */
    Array<double>* computeProbabilities(Data& clusteringData,QList<int>& clusterList,QList<int>& computedClusterList,
                                        QList<int>& ignoreClusterIndex,
                                        Array<double>** errorMatrixOut = nullptr);

    /**
  * Computes a Cholesky Decomposition.
  * The part of the covariances array for the cluster @p clusterIndex,
  * provides the upper triangle of input matrix (in(clusterIndex,i*dimension + j) >0 if j>=i)
  * which is the top half of a symmetric matrix.
  * @param out provides lower triange of output matrix (out(i,j) >0 if j<=i)
  * such that out' * out = in.
  * @param nbDimensions number of dimensions.
  * @param clusterIndex index of the current cluster.
  * @return 0 if OK, 1 if matrix is not positive definite.
  */
    int cholesky(Array<double>& out,int nbDimensions,int clusterIndex);

public:
    /**Restrict every computation to these 0-based feature dimensions (see
     * featuremask.h).  Empty — the default — means use every dimension, so
     * existing callers are unaffected.
     *
     * The means and covariances are then built over the selected dimensions
     * only, which makes them the marginal model over that subspace: the
     * covariance of a subset of dimensions IS the corresponding submatrix of
     * the full covariance, so the Cholesky and the Mahalanobis distance below
     * stay exactly as they are and simply see a smaller model.
     *
     * Must be set before computeMeanProbabilities* is called.*/
    void setActiveDimensions(const std::vector<int>& dims) { activeDims = dims; }
    const std::vector<int>& activeDimensions() const { return activeDims; }

    /**The MODEL INDEX contract.
     *
     * buildModelIndex() below is the only thing IN THIS FILE that walks
     * clusterInfoMap.  Every
     * loop that needs a per-cluster counter iterates the vector it returns, and
     * every array sized per cluster is sized from that vector's size().  A cluster's
     * POSITION in it is simultaneously
     *
     *   - the row AND column of that cluster in means, cholesky and errorMatrix,
     *   - the value stored in and tested against ignoreClusterIndex,
     *   - the index into the entries/cdata vectors handed to the kernels.
     *
     * One number, three meanings, no type separating them.  Change the order or the
     * membership seen by one consumer and not another and the matrices are still
     * produced, still plausible, and wrong.
     *
     * This was previously twelve independent walks, each incrementing its own
     * counter in its for-header -- eight in the default build and four more inside
     * the CUDA/HIP/SYCL and OpenMP guards, which no single-configuration parse can
     * see.  Agreement between them was a property of each happening to be written
     * the same way, and nothing enforced it.  Converting them to range-for dropped
     * the increment in three, leaving `ci` frozen so every cluster was modelled from
     * cluster 1's statistics -- a fully populated, entirely plausible, wrong matrix,
     * which is the failure mode this contract exists to describe.
     *
     * Two rules follow, and both are load-bearing:
     *
     *   - The index is built ONCE per compute and shared.  Rebuilding it per use
     *     reopens the possibility of two consumers disagreeing.
     *   - Membership is decided ONLY inside buildModelIndex().  Filtering again at
     *     a consumer -- for instance by also setting cd.ignore from clusterInScope
     *     -- marks rows the caller never asked to ignore, because ignoreClusterIndex
     *     holds positions in the very index being filtered.  That produced an
     *     all-zero error matrix and is why setActiveClusters is applied here and
     *     nowhere else.
     */
    /**1-based cluster ids to include; empty — the default — means every cluster,
     * so existing callers are unaffected.  Mirrors setActiveDimensions above: an
     * opt-in restriction applied where the model is enumerated, not a filter over
     * a result that was computed in full.
     *
     * Used for the scoped matrices, which compare one parent's children rather
     * than every atom in the session.  Restricting here rather than afterwards is
     * the point: the cost of these matrices is quadratic in the cluster count, and
     * a session has thousands of atoms where a parent has tens.
     *
     * Must be set before computeMeanProbabilities* is called.*/
    void setActiveClusters(const QList<int>& ids) { activeClusters = ids; }
    const QList<int>& activeClusterIds() const { return activeClusters; }

private:
    /**0-based feature dimensions to use; empty = all.*/
    std::vector<int> activeDims;

    /**Cluster ids to include; empty = all.*/
    QList<int> activeClusters;

    /**One entry per cluster the model covers, in clusterInfoMap order.*/
    struct ModelEntry { dataType id; dataType first; dataType nb; };

    /**THE single producer of the model index.
     *
     * See the MODEL INDEX contract above for why this is the single producer and
     * what depends on it.  Sizing comes from its size(), so the loops that run
     * 1..nbClusters over the same space move with it rather than needing to be
     * found separately.
     *
     * With activeClusters empty -- every existing caller -- this returns every
     * cluster in map order and each counter takes exactly the values it always
     * did.  That identity is what makes the change reviewable.*/
    QVector<ModelEntry> buildModelIndex(const Data::ClusterInfoMap* m) const {
        QVector<ModelEntry> out;
        if (!m) return out;
        out.reserve(m->count());
        for (Data::ClusterInfoMap::ConstIterator it = m->constBegin(); it != m->constEnd(); ++it)
            if (clusterInScope(static_cast<int>(it.key())))
                out.append({ it.key(), it.value().firstSpikePosition(), it.value().nbSpikes() });
        return out;
    }

    /**True when @p id is in scope.  Cluster 1 is always kept: it is the noise
     * reference the matrix prepends and the probabilities are relative to it, so
     * dropping it would change what every remaining cell means -- and it is a
     * destination the curator uses, which is why the other three matrices keep it
     * too.  Cluster 0 is NOT carved out: the artefact bin is not a comparison
     * target, and keeping it would put a row in the scoped matrix that the other
     * three views do not have.*/
    inline bool clusterInScope(int id) const {
        return activeClusters.isEmpty() || id == ClusterId::Noise || activeClusters.contains(id);
    }

    /**Map a 1-based model dimension to the 1-based .fet column it reads.
     * Identity when unrestricted.*/
    inline int dimCol(int modelDim1Based) const {
        return activeDims.empty()
                   ? modelDim1Based
                   : activeDims[static_cast<size_t>(modelDim1Based - 1)] + 1;
    }
    /**Model dimensionality: the selection size, or @p fullNbDims when
     * unrestricted.*/
    inline int activeDimCount(int fullNbDims) const {
        return activeDims.empty() ? fullNbDims
                                  : static_cast<int>(activeDims.size());
    }


    /**Computes the means and the covariances.
  * @param nbClusters number of clusters.
  * @param nbDimensions number of dimensions.
  * @param nbSpikes total number of spikes.
  * @param clusteringData object containing all the document data.
  * @param ignoreClusterIndex list of the indexes of the clusters which where not computed, either
  * because they do not have enough spikes or their determinant could not be calculated (their covariance matrix is not positive definite.)
  */
    /**Fits the per-cluster mean and covariance.  Takes the model index rather than
     * rebuilding it: its caller has one already, and two independently built
     * indices could disagree -- means()/covariances() are addressed by position in
     * this index, so a disagreement puts every cluster's statistics in the wrong
     * row.*/
    /**Average the posteriors down each cluster's spikes into the error matrix.
     *
     * cell(row A, col B) is the mean, over A's spikes, of the probability that
     * the spike belongs to B -- so the matrix is asymmetric, and a high value in
     * row A says A's spikes look like B rather than the reverse.  The diagonal is
     * zeroed last because a cluster's agreement with itself carries no
     * information for the curator.
     *
     * Indices are POSITIONS in the model, not cluster ids.  entries[k].idx counts
     * from initIndex over the model, and the column loop runs the same range, so
     * the matrix, the ignore list and the posterior columns all share one
     * numbering.  That is the contract the file rests on, and it held in two
     * separate copies of this loop until one of them was edited.
     *
     * Returns the matrix; the caller owns it.*/
    Array<double>* aggregateErrorMatrix(const Array<double>* probabilities,
                                        const QVector<ModelEntry>& model,
                                        const QList<int>& ignoreClusterIndex,
                                        int nbClusters);

    /**Turn each spike's raw Gaussian likelihoods into posteriors that sum to 1,
     * in place, and resolve the noise column while doing it.
     *
     * Before this runs, probabilities(spike, cluster) holds an unnormalised
     * density; after it, the row reads as "probability this spike belongs to that
     * cluster", which is what the error-matrix aggregation averages.
     *
     * A row that sums to exactly zero -- every density underflowed, routine at 32
     * dimensions for a spike far from every centre -- has no posterior to compute,
     * so its whole mass goes to the noise column: the spike belongs to nothing.
     * Which column that is depends on whether cluster 1 was in the model or a
     * synthetic one was prepended, and getting it wrong hands every underflowing
     * spike to an arbitrary cluster.  That bug existed in both copies of this code
     * and had to be fixed in both; resolving the column here means it can only be
     * wrong in one place.
     *
     * @returns false if haveToStopComputing was set part-way.  The two callers
     * abort differently -- one breaks and falls into its own cleanup, the other
     * returns the partially normalised array -- so the decision stays with them.*/
    bool normaliseRowsToPosteriors(Array<double>* probabilities,
                                   const QVector<ModelEntry>& model,
                                   const QList<int>& ignoreClusterIndex,
                                   int nbClusters);

    void meanCovarianceComputation(const QVector<ModelEntry>& model,
                                   int nbClusters,int nbDimensions,dataType nbSpikes,
                                   Data& clusteringData,QList<int>& ignoreClusterIndex);

};

#endif
