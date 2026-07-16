/***************************************************************************
                          data.h  -  description
                             -------------------
    begin                : Wed Sep 17 2003
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

#ifndef DATA_H
#define DATA_H

//Include files of the application
#include "array.h"
#include "sortabletable.h"
#include "pair.h"
#include "types.h"
#include "clusteruserinformation.h"
#include "curationlogger.h"   // for ClusterSnapshot return type

//Include files for QT
#include <functional>   // buildMissingClusterTemplates' progress callback

#include <QList>
#include <QSet>
#include <QHash>
#include <QRegion>
#include <QMap>
#include <QFile>
#include <QMutex>
#include <QThread>


#include <stdexcept>
#include <math.h>
#include <vector>
#include <atomic>
using namespace std;

// forward declaration
class MinMaxThread;
class WaveformThread;
class CorrelationThread;


class Data;

/**
  * This class contains and manages the data.
  *@author Lynn Hazan
  */

class Data {


public:
    friend class MinMaxThread;
    friend class WaveformThread;
    friend class CorrelationThread;
    friend class AutoSaveThread;
    friend class GroupingAssistant;
    friend class ClustersProvider;

    Data();
    ~Data();

    // Data owns raw resources (spikesByCluster, clusterInfoMap, the worker
    // threads) and is the single document-model instance — held by pointer
    // (KlustersDoc::clusteringData) and never copied.  Delete the copy
    // operations so an accidental by-value copy is a compile error rather than
    // the implicit shallow copy, which would alias those owned pointers and
    // double-free them.  (Declaring ~Data() already suppresses implicit move
    // generation, so Data is also non-movable, which is what we want.)
    Data(const Data&)            = delete;
    Data& operator=(const Data&) = delete;

    /**
  * Loads the features in data.
  * @param featureFile the .fet file
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the loading succeeded, false otherwise
  */
    bool loadFeatures(QFile &featureFile, QString& errorInformation);

    /**
  * Loads the clusters in spikesByCluster.
  * @param clusterFile the .clu.i file.
  * @param spkFileLength the length of the .spk.i file.
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the loading succeeded, false otherwise.
  */
    bool loadClusters(QFile &clusterFile, long spkFileLength, QString& errorInformation);

    /**
  * Loads the configuration parameters.
  * @param parXFile the .par.i file
  * @param parFile the .par file
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the loading succeeded, false otherwise.
  */
    bool configure(QFile& parXFile,QFile& parFile,QString& errorInformation);

    /**
  * Loads the configuration parameters from the YAML parameter file.
  * @param parFile the YAML parameter file.
  * @param electrodeGroupID the id of the electrode group currently opened.
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the loading succeeded, false otherwise.
  */
    bool configure(QFile& parFile,int electrodeGroupID,QString& errorInformation);

    /**
  * Load the features data, cluster data and configuration data when the cluster file does not exist.
  * Initialize the internal representation of the data.
  * @param featureFile the .fet file
  * @param spkFileLength the length of the .spk.i file
  * @param spkFileName the name of the .spk.i file
  * @param parXFile the .par.i file
  * @param parFile the .par file
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the initialization succeded false otherwise
  */
    bool initialize(QFile& featureFile, long spkFileLength, const QString &spkFileName, QFile& parXFile, QFile& parFile, QString& errorInformation);

    /**
  * Load the features data, cluster data and configuration data.
  * Initialize the internal representation of the data.
  * @param featureFile the .fet file
  * @param clusterFile the .clu.i file
  * @param spkFileLength the length of the .spk.i file
  * @param spkFileName the name of the .spk.i file
  * @param parXFile the .par.i file
  * @param parFile the .par file
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the initialization succeded false otherwise
  */
    bool initialize(QFile& featureFile, QFile& clusterFile, long spkFileLength, const QString &spkFileName, QFile& parXFile, QFile& parFile, QString& errorInformation);

    /**
  * Load the features data, cluster data and configuration data when the cluster file does not exist.
  * Initialize the internal representation of the data.
  * @param featureFile the .fet file
  * @param spkFileLength the length of the .spk.i file
  * @param spkFileName the name of the .spk.i file
  * @param parFile the YAML parameter file
  * @param electrodeGroupID the id of the electrode group currently opened.
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the initialization succeded false otherwise
  */
    bool initialize(QFile& featureFile, long spkFileLength, const QString &spkFileName, QFile& parFile, int electrodeGroupID, QString& errorInformation);

    /**
  * Load the features data, cluster data and configuration data.
  * Initialize the internal representation of the data.
  * @param featureFile the .fet file
  * @param clusterFile the .clu.i file
  * @param spkFileLength the length of the .spk.i file
  * @param spkFileName the name of the .spk.i file
  * @param parFile the YAML parameter file
  * @param electrodeGroupID the id of the electrode group currently opened.
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the initialization succeded false otherwise
  */
    bool initialize(QFile& featureFile,QFile& clusterFile, long spkFileLength, const QString &spkFileName, QFile& parFile, int electrodeGroupID, QString& errorInformation);

    /**Returns true if spikeIndex is valid (1-based, <= nbSpikes).*/
    bool isValidSpikeIndex(dataType spikeIndex) const {
        return spikeIndex >= 1 && spikeIndex <= nbSpikes;
    }

    /**
     * Overwrites the feature values for a single spike in memory.
     * @param spikeIndex  1-based index into the features array (row).
     * @param newValues   Array of (nbDimensions-1) feature values in order.
     *                    The timestamp column (last column) is left unchanged.
     * @return false if spikeIndex is out of range.
     */
    bool updateFeatureRow(dataType spikeIndex, const QList<dataType>& newValues);

    /**
     * Updates only the timestamp column for a spike in memory.
     * @param spikeIndex  1-based spike index.
     * @param newTimestamp New timestamp value (samples from recording start).
     * @return false if spikeIndex is out of range.
     */
    bool updateTimestamp(dataType spikeIndex, dataType newTimestamp);

    /**Swaps all in-memory data (features, spikesByCluster) for two 1-based spike indices.*/
    void swapSpikes(dataType idxA, dataType idxB);

    /**Returns the feature value at (spikeIndex, dimension) — both 1-based.*/
    dataType featureValue(dataType spikeIndex, int dimension) const {
        return features(spikeIndex, dimension);
    }

    /**Returns the number of dimensions (including timestamp as last column).*/
    int nbOfDimensionsTotal() const { return nbDimensions; }

    /**Returns the number of channels.*/
    int nbOfChannels() const { return nbChannels; }

    /**Returns the number of samples per waveform.*/
    int nbSamplesPerWaveform() const { return nbSamplesInWaveform; }

    /**Returns the 0-based peak sample index within a waveform.*/
    int peakSampleIndex() const { return peakPositionInWaveform; }

    /** Set the peak sample position (1-based) used as the alignment
     *  anchor by nudge / realign / trace-overlay rendering.  Updates
     *  the in-memory value only; the parameter file on disk (.par.N or
     *  .yaml peakSampleIndex) is NOT modified.  Reloading the session
     *  will read the disk value again, so the user must persist this
     *  separately if they want it to stick.
     *
     *  Used by the "Detect peak position" tool which measures the
     *  actual peak location in `.spk` and corrects a stale
     *  `peakPositionInWaveform` parameter (typical scenario: an old
     *  ndm_alignspikes binary realigned the waveforms but the .par.N
     *  was never updated to match the new peak position). */
    void setPeakSampleIndex(int v) { peakPositionInWaveform = v; }

    /**Calculate the minimum and maximum for each dimension and store them in
  *dimensionMinima and dimensionMaxima respectively.
  * @param modifiedClusters list of the clusters which have been modified implying
  * the modification of the cluster 0, causing the recalculation of the minima and maxima.
  */
    void minMaxDimensionCalculation(const QList<int>& modifiedClusters);

    /**
  * Creates a new cluster out of existing ones.
  * @param region the polygon defining the area containing the spikes for the new cluster.
  * @param clustersOfOrigin a list of the cluster numbers identifying the clusters which
  * may contain spikes in the region
  * @param dimensionX the dimension used as abscissa to display the clusters
  * @param dimensionY the dimension used as ordinate to display the clusters
  * @param fromClusters an empty list used as a return value, which will be filled
  * with the cluster numbers which really contained spikes in the region
  * @param emptyClusters an empty list used as a return value, which will be filled
  * with the cluster numbers which became empty because all their spikes were put in the new one.
  * @return the number of the newly created cluster or 0 if no cluster have been created (no spikes selected).
  * This is safe as cluster 0 (artifact) can never be created that way.
  */
    dataType createNewCluster(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY, QList <int>& fromClusters,QList <int>& emptyClusters);

    /**
  * Creates a new clusters out of existing ones. If the polygon of selection contains x clusters
  * x new clusters will be created.
  * @param region the polygon defining the area containing the spikes for the new cluster.
  * @param clustersOfOrigin a list of the cluster numbers identifying the clusters which
  * may contain spikes in the region
  * @param dimensionX the dimension used as abscissa to display the clusters
  * @param dimensionY the dimension used as ordinate to display the clusters
  * @param emptyClusters an empty list used as a return value, which will be filled
  * with the cluster numbers which became empty because all their spikes were put in the new one.
  * @return a map where the keys are ids of the clusters which really contained spikes in the region
  * and the values are the ids of the newly created clusters.
  */
    QMap<int,int> createNewClusters(QRegion& region, const QList <int>& clustersOfOrigin, int dimensionX, int dimensionY,QList <int>& emptyClusters);

    /** Apply a per-spike basin labeling using the existing recluster
     *  pipeline, so new clusters are renumbered to start strictly after
     *  the current maximum cluster ID and the source clusters are fully
     *  dissolved.  Mirrors what KlustaKwik output does in the recluster
     *  path, except labels come from a watershed kernel instead of an
     *  external process.
     *
     *  All spikes from `clustersToRecluster` MUST have an entry in
     *  `featureRowToBasin` (basin >= 1).  The caller is responsible for
     *  assigning unlabeled spikes to a "residual" basin so this contract
     *  holds.
     *
     *  Implementation: populates the internal reclusteringSpikesByCluster
     *  table by walking clustersToRecluster, then offsets each basin
     *  label by highestClusterId (matching loadReclusteredClusters), and
     *  finally hands off to integrateReclusteredClusters which rebuilds
     *  the spike table and pushes the undo entry.
     *
     *  @param clustersToRecluster source clusters (will be dissolved).
     *  @param featureRowToBasin   feature-row → basin label (>= 1).
     *  @param newClusterList      out: new cluster IDs, in ascending order.
     *  @return true on success. */
    bool integrateBasinLabeling(QList<int>& clustersToRecluster,
                                const QHash<dataType,int>& featureRowToBasin,
                                QList<int>& newClusterList);

    /** Split @p sourceCluster into N new clusters using a K-nearest-
     *  neighbour majority-vote classifier whose reference vocabulary is
     *  built from existing well-isolated clusters.
     *
     *  Reference pool: every cluster c with
     *      c != sourceCluster,  c > 1 (skip artifact + MUA),
     *      clusterInfoMap[c].nbSpikes() >= minRefClusterSize.
     *  Source spikes are NEVER candidates for their own classification —
     *  the algorithm asks "if I had to assign this spike to one of the
     *  existing good units, which one would it be?".
     *
     *  For each source spike s:
     *    1. Find its @p K nearest neighbours in feature space (Euclidean,
     *       dims 1..nbDimensions-1 — timestamp excluded) among the
     *       reference pool only.
     *    2. Tally neighbour cluster IDs; the dominant ID is the label
     *       iff its share of the K votes is >= @p majorityThreshold,
     *       else the spike is marked ambiguous.
     *
     *  Group source spikes by label:
     *    - Each distinct dominant-reference label becomes a new cluster
     *      at the tail of the palette (IDs starting at highestId+1).
     *    - All ambiguous spikes form an additional "residual" new
     *      cluster, IF the ambiguous group meets @p minNewClusterSize.
     *    - Any per-label group below @p minNewClusterSize is folded
     *      into the residual.
     *    - The source cluster remains in place ONLY if all per-label
     *      groups (including residual) are dropped; otherwise the
     *      source is emptied and removed.
     *
     *  Returned:
     *    @param newClusters         new cluster IDs in creation order.
     *    @param matchedReferences   parallel array — reference cluster
     *                                each new cluster matched, or -1
     *                                for the residual group.
     *    @param emptiedClusters     contains sourceCluster iff it was
     *                                fully consumed.
     *    @param errorMessage        non-empty on failure with a human-
     *                                readable cause (no reference pool,
     *                                cluster too small, etc.).
     *
     *  @return true on a committed split (prepareUndo called),
     *           false on any error or no-op (no state mutation). */
    bool splitClusterByKnnVsReferences(int sourceCluster,
                                        int K,
                                        double majorityThreshold,
                                        int minNewClusterSize,
                                        int minRefClusterSize,
                                        QList<int>& newClusters,
                                        QList<int>& matchedReferences,
                                        QList<int>& emptiedClusters,
                                        QString& errorMessage);

  
    /**
  * Removes spikes from some clusters and assign them to the cluster @p destinationCluster
  * which is either the cluster 0, corresponding to the artifact, or the cluster 1, corresponding to the noise.
  * @param region the polygon defining the area containing the spikes corresponding to the noise.
  * @param clustersOfOrigin a list of the cluster numbers (in ascending order) identifying the clusters which
  * may contain spikes in the region
  * @param destinationCluster the cluster number to assign the spikes contained in the region
  * @param fromClusters an empty list used as a return value, which will be filled
  * with the cluster numbers which really contained spikes in the region
  * @param dimensionX the dimension used as abscissa to display the clusters
  * @param dimensionY the dimension used as ordinate to display the clusters
  * @param emptyClusters an empty list used as a return value, which will be filled
  * with the cluster numbers which became empty because all their spikes were put in the new one.
  */
    void deleteSpikesFromClusters(QRegion& region, const QList <int>& clustersOfOrigin, int destinationCluster, int dimensionX, int dimensionY, QList <int>& fromClusters,QList <int>& emptyClusters);

    /**Moves a subset of spikes from @p fromCluster to @p toCluster.
     * @p featureRowSet contains the 1-based feature-file row indices of the spikes to move.
     * Creates @p toCluster if it does not yet exist. */
    void moveSpikeSubset(int fromCluster, const QSet<dataType>& featureRowSet,
                         int toCluster,
                         QList<int>& fromClusters, QList<int>& emptiedClusters);

    /** Inverse of labelByFeatureRow(): rebuild spikesByCluster + clusterInfoMap so
     *  the spike at 1-based feature row r belongs to cluster @p labels[r].  Used to
     *  revert a child-layer re-cut as one atomic step inside a parent undo/redo --
     *  the re-cut runs through moveSpikeSubset, which pushes no Data undo level, so
     *  it cannot be reverted by undo()/redo(); a label snapshot taken before the
     *  re-cut is replayed here instead.  Invalidates the waveform / correlation
     *  caches for every cluster whose membership may have changed. */
    void restoreClusterLabels(const QVector<dataType>& labels);

    /** Atomic two-way split of a single cluster.
     *
     *  Partitions every spike of @p sourceCluster into TWO new clusters
     *  according to two disjoint row-sets, in a single rebuild of
     *  spikesByCluster + clusterInfoMap, pushing exactly ONE Data-side
     *  undo entry.  The source cluster is emptied and removed; both new
     *  IDs land at the palette tail (assuming the caller chose them as
     *  nextFreeClusterId() and nextFreeClusterId()+1).
     *
     *  Used by KlustersDoc::dipSplitApply.
     *
     *  @pre  leftRows ∪ rightRows covers every spike of sourceCluster
     *        exactly once.  No validation is performed here.
     *  @pre  leftId and rightId do not yet exist.  leftId != rightId.
     *  @pre  sourceCluster exists.
     *
     *  @param[out] fromClusters     Set to {sourceCluster}.
     *  @param[out] emptiedClusters  Set to {sourceCluster}.
     *  @param[out] newClusters      Set to {leftId, rightId} ascending.
     */
    void splitClusterTwoWays(int sourceCluster,
                              const QSet<dataType>& leftRows,
                              int leftId,
                              const QSet<dataType>& rightRows,
                              int rightId,
                              QList<int>& fromClusters,
                              QList<int>& emptiedClusters,
                              QList<int>& newClusters);

    /**
  * Deletes the clusters contained in @p clustersToDelete. The correponding spikes are assign to cluster 1 (the noise)
  * @param clustersToDelete a list of the cluster numbers (in ascending order) identifying the clusters to delete.
  */
    void moveClustersToNoise(QList <int>& clustersToDelete);

    /**
  * Deletes the clusters contained in @p clustersToDelete. The correponding spikes are assign to cluster 0 (the artifact)
  * @param clustersToDelete a list of the cluster numbers (in ascending order) identifying the clusters to delete.
  */
    void moveClustersToArtefact(QList <int>& clustersToDelete);

    /**
  * Groups the clusters contained in @p clustersToGroup. The correponding spikes are assign to a new cluster.
  * The user information of the different clusters to be grouped will be concatenated.
  * @param clustersToGroup a list of the cluster numbers (in ascending order) identifying the clusters to group
  * @return the number of the newly created cluster.
  */
    dataType groupClusters(QList <int>& clustersToGroup);

    /**Returns the number of dimensions of the data.*/
    int nbOfDimensions(){return nbDimensions;}

    /** Reverts the last user action.
  * @param addedClusters list of clusters which were added (can be empty).
  * @param updatedClusters list of clusters which were modified (can be empty).
  */
    void undo(QList<int>& addedClusters,QList<int>& updatedClusters);

    /** Reverts the last undo action
  * @param addedClusters list of clusters which were added (can be empty).
  * @param updatedClusters list of clusters which were modified (can be empty).
  * @param deletedClusters list of clusters which were deleted (can be empty).
  */
    void redo(QList<int>& addedClusters,QList<int>& updatedClusters,QList<int>& deletedClusters);

    /**Renumbers the clusters, so the the clusterIds will be consecutive.
  * @param clusterIdsOldNew map between old and new cluster ids.
  * @param clusterIdsNewOld map between new and old cluster ids.
  */
    void renumber(QMap<int,int>& clusterIdsOldNew,QMap<int,int>& clusterIdsNewOld);

    /** Targeted partial rename.  Each (oldId → newId) entry in `oldToNew`
     *  is applied to the spike table, clusterInfoMap, waveform/correlation
     *  caches.  Clusters not present in the map are left unchanged.
     *  Used by the palette T shortcut to renumber selected clusters to
     *  IDs greater than the current maximum, without disturbing the
     *  numbering of unrelated clusters (which is what `renumber` would do).
     *
     *  Preconditions:
     *   - new IDs in `oldToNew` must not collide with any existing
     *     cluster ID OR with any other new ID in the same map;
     *   - oldId must already exist in clusterInfoMap;
     *   - 0 and 1 (artefact / noise) must NOT be remapped.
     *
     *  Pushes the pre-rename spike table onto the undo stack via
     *  prepareUndo so Ctrl+Z reverses the operation.  Caller is
     *  responsible for the matching KlustersDoc-level undo bookkeeping
     *  (clusterColorList snapshot via prepareClusterColorUndo, etc.).
     */
    void renumberPartial(const QMap<int,int>& oldToNew);

    /**Makes all the internal changes due to a modification of the number of undo.
  * @param newNbUndo the future new number of undo.
  */
    void nbUndoChangedCleaning(int newNbUndo);

    class Iterator;
    friend class Iterator;

    /**
  * Creates and returns an iterator on the spikes of the given cluster.
  * @param clusterId the number of the cluster on which spikes the iterator will iterates.
  * @return the iterator on the spikes of the given cluster.
  */
    Iterator iterator(dataType clusterId){
        return Iterator(clusterId, *this);
    }

    /**
  * Specialized iterator which iterate on the features contained in features for
  * the cluster specified in the constructor.
  */

    class Iterator{
        //Only the method iterator of data has access to the private part of Iterator,
        //the constructor of Iterator being private, only this method con create a new Iterator
        friend Iterator Data::iterator(dataType clusterId);

    public:
        ~Iterator(){}
        /**
    * Returns a QPoint for the given dimensions for the current spike for the cluster on which this class iterates
    * Caution: in Qt graphical coordinate system, the Y axis is inverted (increasing downwards),
    * thus a point (x,y) as to be drawn and tested as (x,-y).
    * @param dimensionX the feature used as the abscissa
    * @param dimensionY the feature used as the ordinate
    * @return a QPoint for the couple (dimensionX,dimensionY) taking the Qt graphical
    * coordinate system into consideration, the ordinate coordinate is the opposite of the raw data.
    */
        QPoint operator()(dataType dimensionX, dataType dimensionY) const{
            dataType featuresRowIndex = (*data.spikesByCluster)(1,index);
            return QPoint(data.features(featuresRowIndex,dimensionX),
                          - data.features(featuresRowIndex,dimensionY));
        }
        /**
    * Returns the specified feature for the current spike for the cluster on which this class iterates.
    * @param dimension the feature requested
    * @return the value of the feature.
    */
        dataType operator()(dataType dimension) const{
            return data.features((*data.spikesByCluster)(1,index),dimension);
        }
        /**Increments the iterator.*/
        void next(){index++;}
        /**Check if there is more spikes*/
        bool hasNext(){return (lastIndex >= index);}

    private:
        Iterator(dataType clusterId, const Data& d):data(d),clusterId(clusterId){
            // value() (not operator[]) so an unknown cluster id is not inserted
            // into the map; combined with ClusterInfo's zero-init default this
            // yields an empty, crash-safe span for ids absent from this Data.
            ClusterInfo clusterInfo = data.clusterInfoMap->value(clusterId);
            index = clusterInfo.firstSpikePosition();
            lastIndex = index + clusterInfo.nbSpikes() - 1;
        }
        /**Returns true if the iterator has reach the last spike for the cluster on which it iterates,
      * false otherwise.
      */
        const Data& data;
        dataType clusterId;
        dataType index;
        dataType lastIndex;
    };

    /** Returns the list of cluster Ids.*/
    QList<dataType> clusterIds(){
        return clusterInfoMap->keys();
    }

    /** Returns the largest currently-allocated cluster ID, or 0 if there
     *  are no clusters at all.  Reads the bottom of column 2 in
     *  spikesByCluster (which is the canonical source — it's sorted by
     *  cluster ID, so the last spike's cluster ID is the maximum).
     *
     *  Six call sites used to compute this themselves with the
     *  `(*spikesByCluster)(2, nbSpikes)` idiom; this consolidation
     *  guarantees a uniform definition. */
    dataType highestClusterId() const {
        if (nbSpikes == 0) return 0;
        return (*spikesByCluster)(2, nbSpikes);
    }

    /** Returns the next cluster ID that's guaranteed not to collide with
     *  any existing cluster — i.e. `highestClusterId() + 1`.  This is
     *  the canonical "where do new clusters go" policy: always at the
     *  tail, never filling interior gaps.  Used by every create-cluster
     *  path so the palette ordering is predictable. */
    dataType nextFreeClusterId() const {
        return highestClusterId() + 1;
    }

    /** Per-feature-row cluster label: returns a vector of size nbSpikes+1 where
     *  result[r] is the cluster id of the spike at 1-based feature row r (index 0
     *  unused).  Read-only; used by the hierarchical view to re-derive the
     *  fiber<-child maps from the live clustering after an edit or undo. */
    QVector<dataType> labelByFeatureRow() const {
        QVector<dataType> out(static_cast<int>(nbSpikes) + 1, 0);
        for (auto it = clusterInfoMap->begin(); it != clusterInfoMap->end(); ++it) {
            const dataType cid = it.key();
            const dataType f = it.value().firstSpikePosition();
            for (dataType i = f; i < f + it.value().nbSpikes(); ++i) {
                const dataType row1 = (*spikesByCluster)(1, i);
                if (row1 >= 1 && row1 <= nbSpikes) out[static_cast<int>(row1)] = cid;
            }
        }
        return out;
    }

    /** 0-based .spk indices of the spikes in @p cluster (i.e. 1-based feature row
     *  minus one), the form KlustersDoc::moveSpikeSubsetToCluster expects.  Empty
     *  if the cluster is absent.  Read-only. */
    QVector<int> clusterSpkIndices(int cluster) const {
        QVector<int> out;
        const auto it = clusterInfoMap->constFind(static_cast<dataType>(cluster));
        if (it == clusterInfoMap->constEnd()) return out;
        const dataType f = it.value().firstSpikePosition();
        out.reserve(static_cast<int>(it.value().nbSpikes()));
        for (dataType i = f; i < f + it.value().nbSpikes(); ++i)
            out.append(static_cast<int>((*spikesByCluster)(1, i)) - 1);
        return out;
    }

    /** True iff `clusterInfoMap` has an entry for `clusterId` with a
     *  non-zero spike count.  createFeatureFile / integrateBasinLabeling
     *  / integrateReclusteredClusters all read this map; if the key is
     *  missing, Qt's QMap::operator[] silently inserts a default
     *  ClusterInfo (nbSpikes=0), causing the recluster temp .fet to be
     *  written with a valid header but zero data rows — KK then aborts
     *  on the very first SetSize.  KlustersApp::slotRecluster uses this
     *  predicate to refuse such launches with a useful error message. */
    bool clusterHasMembers(int clusterId) const {
        if (!clusterInfoMap) return false;
        const auto it = clusterInfoMap->constFind(
            static_cast<dataType>(clusterId));
        return it != clusterInfoMap->constEnd() && it.value().nbSpikes() > 0;
    }

    /**Returns the maximum for the dimension
  * @param dimension for which the maximum is requested. Numbering starts at 1
  * @return maximum of the dimension
  */
    dataType maxDimension(int dimension) const {return dimensionMaxima(dimension,1);}

    /**Returns the minimum for the dimension
  * @param dimension for which the minimum is requested. Numbering starts at 1
  * @return minimum of the dimension
  */
    dataType minDimension(int dimension) const {return dimensionMinima(dimension,1);}

    /**Saves the clusters information to file
  * @param clusterFile the .clu.i file
  * @return true if the data have been successfully saved to file, false otherwise.
  */
    bool saveClusters(FILE* clusterFile);

    /**Returns the number of points used to describe a waveform. Each point
  correspond to a diffrent instant in time.*/
    int nbOfSampleInWaveform()const{return nbSamplesInWaveform;}

    /**Returns the position of the peak among the points decribing the waveform.*/
    int positionOfPeakInWaveform()const{return peakPositionInWaveform;}

    /**Returns the number of channels used.*/
    int nbOfchannels()const{return nbChannels;}

    /**Returns the total number of PCAs used
  * (number of channels times number of PCA by channel).*/
    int totalNbOfPCAs()const{return (nbChannels*nbFeaturesbyChannel);}
    /**Number of PCA components per channel (the YAML's per-group nFeatures).
     * Needed to map a .fet column back to its channel: the PCA block is written
     * channel-major with this stride.  NB totalNbOfPCAs() above assumes the PCA
     * covers every channel, which is not true on stderiv sessions.*/
    int nbOfFeaturesByChannel()const{return nbFeaturesbyChannel;}

    /** Per-feature sample variance for all spikes belonging to @p clusterId.
     *  Returned vector has length nbDimensions-1 (timestamp column excluded).
     *  Returns empty vector when cluster has fewer than 2 spikes. */
    QVector<double> featureVariancesForCluster(int clusterId) const;

    /** Per-feature variance pooled across all spikes belonging to any cluster
     *  in @p clusterIds.  Returns an empty vector if fewer than 2 spikes are
     *  found in total across all listed clusters. */
    QVector<double> featureVariancesForClusters(const QList<int>& clusterIds) const;

    /** Compute a full ClusterSnapshot for @p clusterId from the current
     *  in-memory state.  All metrics (ISI violations, firing rate, feature
     *  variances, anisotropy) are derived from the live features / spikesByCluster
     *  tables so the snapshot is always consistent with the current curation state.
     *
     *  @param clusterId       Cluster to snapshot (1-based, must exist).
     *  @param isiThreshMs     Refractory period threshold in milliseconds (default 3 ms).
     *  @param allCentroids    Optional precomputed centroid map from computeAllCentroids().
     *                         When provided, nearest-cluster metrics are filled in.
     *                         Pass nullptr to skip nearest-cluster computation.
     *  @return                Filled ClusterSnapshot; clusterId == -1 on failure.
     */
    ClusterSnapshot computeSnapshot(int clusterId,
                                    double isiThreshMs = 3.0,
                                    const QMap<int, QVector<double>>* allCentroids = nullptr,
                                    bool liteIsolation = false) const;

    /** Compute the feature-space centroid for every cluster currently in memory.
     *  Returns a map: clusterId → centroid vector of length (nbDimensions-1).
     *  Used by callers that snapshot multiple clusters so centroid computation
     *  is amortised across the full call set rather than repeated per cluster.
     */
    QMap<int, QVector<double>> computeAllCentroids() const;

    /** Diagnostic (read-only).  Verifies the invariant the centroid/snapshot passes
     *  rely on: features has exactly nbSpikes rows, and every spikesByCluster(1,·)
     *  feature-row index lies in [1, features rows].  qWarning()s the first violation,
     *  tagged with @p where, so the operation that breaks it can be localised at its
     *  own boundary instead of surfacing later in a curation-log snapshot.  Returns
     *  true when the invariant holds. */
    bool checkSpikeFeatureInvariant(const char* where) const;

    /**Read-only diagnostic: verifies clusterInfoMap (the cluster map) agrees
     * with spikesByCluster (the row table) — per-cluster counts match and every
     * row-table cluster has a map entry. Warns @p where on the first violation.
     * Localises the partial-commit op that desyncs the two structures.*/
    bool checkClusterInfoMapInvariant(const char* where) const;

    /**Rebuilds clusterInfoMap (the cluster map) from spikesByCluster (the row
     * table — the source of truth), the in-memory equivalent of save+reopen.
     * Repairs a partial-commit desync where a cluster present in the row table
     * has a stale 0 count or no map entry, without a disk round-trip.*/
    void resyncClusterInfoMapFromRowTable();

    /**Loud in-memory self-heal: if the derived clusterInfoMap disagrees with the
     * row table on entry, rebuild it (and normalise the row-table layout) from the
     * authoritative per-spike cluster assignments, then re-check.  Called at the top
     * of every tiling committer so it builds a consistent table and the edit
     * proceeds rather than being dropped by prepareUndo.  Returns final consistency.*/
    bool healClusterInfoMapIfDesynced(const char* where);

    /**Validates the feature-row (Class A) invariant on a CANDIDATE row table
     * before it is installed by prepareUndo: it must have at least nbSpikes
     * columns and row 1 must be a permutation of the feature rows — every index
     * in [1, features.nbOfRows()] and each used at most once (no in-range
     * duplicate).  Unlike checkSpikeFeatureInvariant, which inspects the
     * already-installed table, this inspects a temp table handed to prepareUndo,
     * so an internally-inconsistent table (e.g. a zeroed/short tail from a short
     * recluster/basin integrate, or a double-written source block) can be REFUSED
     * before it is committed and later saved.  Returns true if the table is safe
     * to install (or if the feature matrix is not loaded, in which case it cannot
     * judge).*/
    bool candidateSpikeTableValid(const SortableTable* candidate) const;

    /**Returns the sampling interval (time between two samples) in second.*/
    double intervalOfSampling()const{return samplingInterval;}

    /**Returns the dimension for the time.*/
    int timeDimension()const{return nbDimensions;}
    /**Returns the maximum value for the time dimension in seconds.*/
    long maxTime(){
        double maximumTimeInRecordingUnits = static_cast<double>(maxDimension(nbDimensions));
        //the cast takes the non floating part, to include the last record we add 1.
        double maxTimeInS = static_cast<double>(maximumTimeInRecordingUnits * samplingInterval) / static_cast<double>(1000000);

        return static_cast<long>(floor(0.5 + maxTimeInS));
    }

    /** Cluster ids with at least @p minSpikes spikes whose timestamp (the last
     *  feature dimension, in recording units / samples) falls within [t0,t1].
     *  Backs time-chunk curation: the set of clusters present in one chunk. */
    QList<int> clustersInTimeWindow(long t0, long t1, int minSpikes = 1) const;

    /** Largest spike timestamp in recording units (samples). */
    long maxTimeInRecordingUnits() const {return static_cast<long>(maxDimension(nbDimensions));}


    /**Gives information on how the data were recorded. True if the data where recording using a 12 or 16 bits recording system which
  * gives data coded on 2 bytes, false otherwise, (the recording is then assume to be 32 bits
  * and then the data are coded on 4 bytes).*/
    bool isRecordingTwoBytes(){return isTwoBytesRecording;}

    /**Gets a onr row SortableTable with the spike positions for the cluster @p clusterId.
  * @param clusterId d of the cluster for which the spike position are search.
  * @param subsetTable the array where the result of the search will be store.
  * @return true if the cluster exist and the data have been retreive, false otherwise.
  */
    bool spikePositions(int clusterId,SortableTable& subsetTable);
    /**Like spikePositions() but also atomically checks isClusterModified() under the same lock.
     * Returns false if the cluster is gone OR was modified by the main thread. */
    bool spikePositionsNotModified(int clusterId,SortableTable& subsetTable);

    /**Invalidates the in-memory waveform cache for @p clusterId so that the
     * next WaveformThread request re-reads waveforms from the .spk file.
     * Must be called after the .spk file has been modified in-place (e.g.
     * after spike realignment) to ensure the waveform view shows fresh data.
     * Thread-safe: uses the internal mutex.
     */
    void invalidateWaveformCache(int clusterId);

    /** Invalidates the cached auto/cross-correlogram data for @p clusterId
     *  so the next CorrelationThread request recomputes from the (updated)
     *  in-memory spike timestamps.  Call after updating timestamps in memory.
     *  Thread-safe: uses the internal mutex.
     */
    void invalidateCorrelogramCache(int clusterId);

    /** Redirect waveform reads to @p path.
     *  Use this to point the waveform viewer at a pending .spk file before
     *  the original has been overwritten.  Call invalidateWaveformCache()
     *  for the affected cluster afterwards.
     */
    void setSpkFileName(const QString& path) { spkFileName = path; }
    QString getSpkFileName() const { return spkFileName; }

    /**Returns the number of points corresponding to a spike. This equals to:
  * nbChannels * nbSamplesInWaveform
  */
    int nbPtsBySpike(){return nbChannels * nbSamplesInWaveform;}

    /**Returns the number of spikes of the cluster
  * @param clusterId id of the cluster for which the number of spikes is requested.
  * @return the number of spikes of the cluster @p clusterId.
  */
    dataType nbOfSpikes(dataType clusterId){
        // value() is a const, non-detaching read: unlike operator[] it neither
        // copy-on-write-detaches nor inserts a default entry.  This accessor is
        // called lock-free from the correlogram / template-matrix / residual-
        // matrix worker threads; operator[]'s detach mutates the shared QMap's
        // control block, so two of those threads reading at once raced and freed
        // each other's tree nodes (heap-use-after-free).  A pure const read does
        // not touch the control block, so concurrent reads are safe.
        ClusterInfo currentClusterInfo = clusterInfoMap->value(clusterId);
        return currentClusterInfo.nbSpikes();
    }

    /**Returns the total number of spikes.
  * @return the total number of spikes.
  */
    dataType totalNbOfSpikes() const{return nbSpikes;}

    /** Earliest spike timestamp (feature-file time column) for every cluster,
     *  computed in a single pass over spikesByCluster.  Used by the "Sort
     *  Clusters by Time" action to order clusters by their starting edge; the
     *  value is monotonic in real time, which is all the sort needs.  Spikes
     *  whose feature row is out of range (stale spikesByCluster) are skipped so
     *  a desync degrades the ordering instead of reading past the matrix. */
    QHash<int,double> firstSpikeTimes() const;

    /** Fraction of consecutive-spike ISIs shorter than @p refractoryMs, per
     *  cluster -- the refractory contamination estimate used by "Sort Clusters
     *  by Contamination".  One pass to bucket timestamps, then a per-cluster
     *  sort; stale feature rows are skipped.  Empty if samplingRate is unset. */
    QHash<int,double> refractoryViolationFractions(double refractoryMs) const;

    /** Per-cluster waveform SNR (peak-to-trough on the best channel / 2x baseline
     *  RMS), read from the cached mean waveform -- the same computation
     *  computeSnapshot performs.  Only clusters whose mean-waveform cache is
     *  READY appear; others are absent (they sort last).  Used by "Sort Clusters
     *  by SNR". */
    QHash<int,double> clusterWaveformSnrs() const;

    /**Peak-to-trough amplitude of each cluster's mean waveform, taken on the
     * channel where that amplitude is largest.  Keyed by cluster id; clusters
     * whose mean waveform is not computed yet are absent.
     *
     * NB this is the MEAN waveform — the same per-cluster template the waveform
     * view draws and clusterWaveformSnrs() measures.  Klusters caches no median
     * template.*/
    QHash<int,double> clusterWaveformAmplitudes() const;

    /**The channel carrying each cluster's largest peak-to-trough amplitude
     * (group-local index).  Same keys as clusterWaveformAmplitudes().*/
    QHash<int,int> clusterWaveformPeakChannels() const;

    /**Overlap of two clusters' waveform envelopes: intersection-over-union of
     * their mean +/- SD bands, accumulated over every sample and channel.  1 =
     * the envelopes coincide, 0 = they never touch.  Returns false when either
     * cluster's mean waveform is not computed yet.
     *
     * This is the same band the waveform view draws, and it is symmetric by
     * construction — unlike the error and residual matrices, which are
     * asymmetric and force a choice about which direction to believe.*/
    bool clusterEnvelopeOverlap(int clusterA, int clusterB, double& iou) const;

    /**Compact per-cluster template: the mean and SD of every sample on every
     * channel, laid out exactly like the waveform cache (sample * nChan + ch).
     *
     * Kept SEPARATE from waveformDict on purpose.  That cache is display-driven:
     * it holds every displayed spike for the handful of clusters the waveform
     * view is showing, and it only exists for clusters somebody selected.  This
     * one is small (mean+SD only, ~5 KB per cluster on an 8x42 octrode), covers
     * EVERY cluster, and survives selection changes -- which is what a matrix of
     * pairwise overlaps needs.*/
    struct ClusterTemplate {
        std::vector<double> mean;
        std::vector<double> sd;
        long nSpikes = 0;
    };

    /**Build templates for every cluster that lacks a current one, in a single
     * sequential pass over the .spk file.  One pass, not one seek per cluster:
     * the file is read front to back and each record accumulated into its own
     * cluster's sums, so the cost is the file size rather than the cluster count.
     *
     * Incremental: clusters whose template is already current are skipped, and
     * only those clusters' spikes are visited, so the first call after a session
     * opens pays the read and an edit costs only what it touched.
     *
     * GUI-THREAD ONLY.  It reads clusterInfoMap and spikesByCluster and writes
     * clusterTemplates, none of which are locked, and an edit mutates all three.
     * Running it on a worker would race both against edits and against
     * clusterEnvelopeOverlap reading the cache.  The codebase's answer to that
     * elsewhere is stopAllViewThreads() before an edit; this is not part of that
     * machinery, so it stays synchronous rather than pretending to be safe.
     *
     * @p progress, if set, is called periodically with (spikes done, spikes to
     * do) so a caller can drive a progress bar.  It is a plain callback rather
     * than a signal or a widget because the cold build is the only slow part and
     * Data has no business knowing what a status bar is.
     *
     * @return the number of templates built (0 = everything was current).*/
    int buildMissingClusterTemplates(
        const std::function<void(int,int)>& progress = std::function<void(int,int)>());

    /**Drop @p clusterId's template so the next build recomputes it.  Empty
     * clusterId list = drop all.*/
    void invalidateClusterTemplate(int clusterId);
    void invalidateAllClusterTemplates();

    /**How many clusters currently have a template, and how many exist.*/
    int clusterTemplateCount() const { return clusterTemplates.size(); }

private:
    /**Peak-to-trough amplitude of cluster @p clusterId's mean waveform and the
     * channel it occurs on.  Returns false when that cluster has no mean
     * waveform ready.  Shared by the SNR, amplitude and peak-channel
     * accessors so the three cannot disagree about what "best channel" means.*/
    bool clusterBestChannelAmplitude(int clusterId, double& amplitude,
                                     int& channel) const;
public:

    /**
  * String indicating in scale mode the user is using (raw, scale by the maximum,
  * scale by the shoulder) in the correlationView.
  */
    enum ScaleMode{RAW=1,MAX=2,SHOULDER=3};

    /**Returns the current number of clusters.
  * @return the number of clusters.
  */
    int nbOfClusters(){
        mutex.lock();
        int nbClusters = clusterInfoMap->count();
        mutex.unlock();

        return nbClusters;
    }

    /**Assignes to the cluster the information given by the user.
  * @param clusterId id of the cluster for which the information are given.
  * @param structure structure in which the cluster is supposed to be.
  * @param type type of the cluster.
  * @param ID Isolation distance of the cluster.
  * @param quality quality of the cluster.
  * @param notes notes of any type on the cluster.
  */
    void setUserClusterInformation(int clusterId, const QString& structure,
                                          const QString&	type,const QString& ID, const QString&	quality, const QString& notes){
        if((*clusterInfoMap).contains(static_cast<dataType>(clusterId))){
            ClusterInfo currentClusterInfo = clusterInfoMap->value(static_cast<dataType>(clusterId));

            currentClusterInfo.setStructure(structure);
            currentClusterInfo.setType(type);
            currentClusterInfo.setId(ID);
            currentClusterInfo.setQuality(quality);
            currentClusterInfo.setNotes(notes);

            clusterInfoMap->insert(static_cast<dataType>(clusterId),currentClusterInfo);
        }
    }

    /**Gest the cluster the information given by the user.
    * @param clusterId id of the cluster for which the information are given.
    * @return array of QString containing the following information (in the following order) :
    * structure in which the cluster is supposed to be.
    * type of the cluster.
    * Isolation distance of the cluster.
    * quality of the cluster.
    * notes of any type on the cluster.
    */
    void getUserClusterInformation(int clusterId,QList<QString>& clusterInformation){

        if((*clusterInfoMap).contains(static_cast<dataType>(clusterId))){
            ClusterInfo currentClusterInfo = clusterInfoMap->value(static_cast<dataType>(clusterId));

            clusterInformation.append(currentClusterInfo.getStructure());
            clusterInformation.append(currentClusterInfo.getType());
            clusterInformation.append(currentClusterInfo.getId());
            clusterInformation.append(currentClusterInfo.getQuality());
            clusterInformation.append(currentClusterInfo.getNotes());
        }
    }

    /** Gets the map of cluster user information.
     * @param pGroup the current electrod group id.
     * @param clusterUserInformationMap map given the cluster user information, the key is the cluster id and the value an instance of ClusterUserInformation.
  */
    void getClusterUserInformation (int pGroup,QMap<int,ClusterUserInformation>& clusterUserInformationMap)const;

    /**Creates the feature file to automatically recluster the clusters contained in @p clustersToRecluster.
  * @param clustersToRecluster list of clusters to recluster.
  * @param fetFile file to which the data will be saved.
  */
    void createFeatureFile(QList<int>& clustersToRecluster,QFile& fetFile);

    /** patch76 — Build a mean-subtracted sub-dimensional feature file for
     *  ONE cluster.  For each spike, subtract the cluster's mean feature
     *  vector to get the residual, then project the residuals onto the
     *  top K eigenvectors of the residual covariance matrix.  The output
     *  .fet has K residual-PCA dimensions plus the original timestamp
     *  column (so KKExp's chunk-by-time machinery still works).
     *  Returns the number of dimensions actually written (K + 1), or 0
     *  on failure.
     *  @param clusterId  ID of the single cluster to recluster.
     *  @param K          desired number of residual-PCA components.
     *  @param fetFile    output .fet file (will be re-opened in binary).
     *  @param eigvals    optional out: top-K eigenvalues in descending order.
     */
    int createMeanSubtractedSubdimFeatureFile(int clusterId, int K,
                                              QFile& fetFile,
                                              QVector<double>* eigvals = nullptr);

    /** Like createMeanSubtractedSubdimFeatureFile, but operates in raw
     *  waveform space and centres on the per-point MEDIAN of the spikes
     *  pooled across all @p clusterIds.  Reads each spike's waveform from the
     *  .spk file, subtracts the per-(channel,sample) median template (robust
     *  to drift/outliers), PCA-projects the residual waveforms, and writes the
     *  top-K residual components as the recluster .fet.  Returns K+1 or 0.
     *  @param clusterIds one or more clusters whose spikes are pooled.
     *  @param K          desired number of residual-PCA components.
     *  @param fetFile    output .fet file (will be re-opened in binary).
     *  @param eigvals    optional out: top-K eigenvalues in descending order.
     */
    int createMedianWaveformResidualFeatureFile(const QList<int>& clusterIds,
                                                int K, QFile& fetFile,
                                                QVector<double>* eigvals = nullptr);

    /**Integrates the clusters obtained by automatic reclustering.
  * Suppress the reclustered ones and add the newly created ones.
  * @param clustersToRecluster list of clusters reclustered.
  * @param reclusteredClusterList output parameter, the list of the newly created clusters.
  * The list will be empty if the integration is not successful.
  * @param clusterFile cluster file created by the automatic reclustering program.
  * @return true if the integration is successful, false otherwise.
  */
    bool integrateReclusteredClusters(QList<int>& clustersToRecluster, QList<int>& reclusteredClusterList, QFile &clusterFile);

    /**
  * Informs if the the variables need it by the traceView are available. Those variables are retrieve only from
  * the YAML parameter file (the current for
  * @return true if the variables are available, false otherwise.*/
    bool isTraceViewVariablesAvailable()const {return traceViewVariablesAvailable;}

    /**
  *Returns the acquisition system resolution.
  */
    int getResolution()const{return nbBits;}
    /**Returns the total number of channels used during the recording.*/
    int getTotalNbChannels()const{return totalNbChannels;}
    /**Returns the sampling rate in Hz.*/
    double getSamplingRate()const{return samplingRate;}
    /**Returns the acquisition system voltage range.*/
    int getVoltageRange()const{return voltageRange;}
    /**Returns the acquisition system offset.*/
    int getOffset()const{return initialOffset;}
    /**Returns the acquisition system amplification.*/
    int getAmplification()const{return amplification;}
    /**Returns the number of samples used to describe a waveform.*/
    int getNbSamplesInWaveform()const{return nbSamplesInWaveform;}
    /**Returns the sample index of the peak.*/
    int getPeakPositionInWaveform()const{return peakPositionInWaveform;}
    /**Returns the list of channels of the current electrode.*/
    QList<int>& getCurrentChannels(){return currentChannels;}

private:

    /** Native-width implementation of createMedianWaveformResidualFeatureFile.
     *  T is the acquisition sample type (int16 for two-byte recordings,
     *  int32 otherwise); the in-memory waveform store uses T so an exact
     *  per-point median costs half the memory of a double store. */
    template <class T>
    int medianWaveformResidualImpl(const QList<int>& clusterIds, int K,
                                   QFile& fetFile, QVector<double>* eigvalsOut);

    /**
  * String indicating what is the status of the processing of the waveform information.
  */
    enum Status{NOT_AVAILABLE=1,IN_PROCESS=2,READY=3};

    MinMaxThread* minMaxThread;

    int nbChannels = 0;
    int nbSamplesInWaveform = 0;
    int peakPositionInWaveform = 0;
    QList<int> channelIds;
    int nbRefactorySample = 0;
    int RMSIntWindowLength = 0;
    float firingRate = 0.0f;
    int nbSampleBeforePeak = 0;
    int nbSampleAfterPeak = 0;
    int windowLengthToRealign = 0;
    int peakPositionToRealign = 0;
    int nbFeaturesbyChannel = 0;
    int nbSamplesByPCA = 0;
    float HighPassFilterFreq = 0.0f;
    int nbDimensions = 0;
    long nbSpikes;
    int lowPassFilterFreq = 0;
    /**Sampling rate (time between two samples) in micro seconds.*/
    double samplingInterval = 0.0;
    double samplingRate = 0.0;
    int nbTotalElectrodes = 0;
    int nbBits = 0;
    QString spkFileName;
    int voltageRange = 0;
    int amplification = 0;
    int initialOffset = 0;
    int totalNbChannels = 0;
    bool traceViewVariablesAvailable;
    QList<int> currentChannels;

    /**
  * A array which contains the coefficients  to apply the spike samples in
  * order to upsample the waveform.
  * the principal componants
  * the Valley to peak amplitude
  * the peak to valley amplitude
  * the max of the to previous data
  * the width of the spike
  * the time of the spike
  */
    // Array<dataType> coeff;

    /**
  * A array which contains the features for each spike:
  * the principal componants
  * the Valley to peak amplitude
  * the peak to valley amplitude
  * the max of the to previous data
  * the width of the spike
  * the time of the spike
  */

    Array<dataType> features;
    /**
  * A two line array which contains sorted by cluster numbers and then by time:
  * the row index of the spike in features
  * the id of the cluster
  */
    SortableTable* spikesByCluster;

    /**Represents a list of clusterInfoMap
  * use to enable undo action.
  */
    QList<SortableTable*> spikesByClusterUndoList;

    /**Represents a list of clusterInfoMap
  * use to enable redo action.
  */
    QList<SortableTable*> spikesByClusterRedoList;

    /**
  * Represents information on a cluster:
  * the index of the first spike of a given cluster number in spikesByCluster
  * the number of spikes of the cluster
  * and optionnally information added by the user:
  * the structure where the cluster is located
  * the type of the unit
  * the isolation distance
  * the quality
  * notes
  */
    class ClusterInfo {

    public:
        explicit ClusterInfo(const QString& pStructure = QString(), const QString& pType = QString(),const QString& pID = QString(),const QString& pQuality = QString(),const QString& pNotes = QString())
            :structure(pStructure),type(pType),ID(pID),quality(pQuality),notes(pNotes){}
        ClusterInfo(dataType position, dataType nb,const QString& pStructure = QString(),const QString& pType = QString(),const QString& pID = QString(),const QString& pQuality = QString(),const QString& pNotes = QString())
            :position(position),spikeNb(nb),structure(pStructure),type(pType),ID(pID),quality(pQuality),notes(pNotes){}
        ~ClusterInfo(){}
         dataType firstSpikePosition() const {return position;}
         dataType nbSpikes() const {return spikeNb;}
         void setNbSpikes(dataType nbSpikes){spikeNb = nbSpikes;}
         void setFirstSpikePosition(dataType position){this->position = position;}

         QString getStructure() const { return structure; }
         QString getType() const { return type; }
         QString getId() const { return ID; }
         QString getQuality() const { return quality; }
         QString getNotes() const { return notes; }

         void setStructure(const QString& pStructure) { structure = pStructure; }
         void setType(const QString& pType) { type = pType; }
         void setId(const QString& pId) { ID = pId; }
         void setQuality(const QString& pQuality) { quality = pQuality; }
         void setNotes(const QString& pNotes) { notes = pNotes; }

    private:
        // Zero-initialised so a default-constructed ClusterInfo — e.g. the one
        // QMap returns/inserts for a missing cluster id — has a benign empty
        // span (firstSpikePosition 0, nbSpikes 0) instead of garbage.  Without
        // this, iterating a cluster id absent from the active clustering (which
        // happens transiently when the parent<->child active clustering is
        // switched) reads spikesByCluster/features out of bounds and segfaults.
        dataType position = 0;
        dataType spikeNb = 0;

        QString		structure;
        QString		type;
        /**Isolation Distance*/
        QString		ID;
        QString		quality;
        QString		notes;
    } ;

    typedef QMap<dataType,ClusterInfo> ClusterInfoMap;

    /**Contains ClusterInfo
  * key: cluster number
  * value: a ClusterInfo (which gives:
  * the index of the first spike in spikesByCluster and the number of spikes for a given cluster)
  */
    ClusterInfoMap* clusterInfoMap;

    /**Represents a list of clusterInfoMap
  * use to enable undo action.
  */
    QList<ClusterInfoMap*> clusterInfoMapUndoList;

    /**Represents a list of clusterInfoMap
  * use to enable redo action.
  */
    QList<ClusterInfoMap*> clusterInfoMapRedoList;

    /**List of the maximum of each dimension*/
    Array<dataType> dimensionMaxima;
    /**List of the minimum of each dimension*/
    Array<dataType> dimensionMinima;

    /**List of the clusters giving the maximum of each dimension (sorted by dimension)*/
    QList<int> clustersGivingMaximum;

    /**List of the clusters giving the minimum of each dimension (sorted by dimension)*/
    QList<int> clustersGivingMinimum;

    /**QT object providing access serialization between threads*/
    mutable QMutex mutex;  // mutable: locked in const methods called from worker threads

    /**List containing the the dimension change status of each action of the undo list*/
    QList<bool> dimensionChangedUndo;

    /**List containing the the dimension change status of each action of the redo list*/
    QList<bool> dimensionChangedRedo;

    /**True is the data where recording using a 12 or 16 bits recording system which
  * gives data coded on 2 bytes, false otherwise (the recording is then assume to be 32 bits
  * and then the data are coded on 4 bytes.*/
    bool isTwoBytesRecording = false;

    /**
  * String indicating in which presentation mode the user is (sample, time frame).
  */
    enum WaveformMode{SAMPLE=1,TIME_FRAME=2};

    /** Per-cluster waveform-cache status flags.
     *  Tracks whether each of the four cached views (sample-form raw,
     *  time-frame-form raw, sample-form mean, time-frame-form mean) is
     *  currently AVAILABLE, NOT_AVAILABLE, or IN_PROGRESS for a given
     *  cluster, plus a `clusterModified` dirty bit set when any spike
     *  belonging to the cluster has been moved/added/removed since the
     *  last cache build.  Owned by Waveforms; one instance per cluster.
     */
    class WaveformStatus{
    public:
        explicit WaveformStatus(Status sample = NOT_AVAILABLE,Status timeFrame = NOT_AVAILABLE,Status sampleMean = NOT_AVAILABLE,Status timeFrameMean = NOT_AVAILABLE )
            :sample(sample),timeFrame(timeFrame),sampleMean(sampleMean),timeFrameMean(timeFrameMean){
            clusterModified = false;
        }
        WaveformStatus(const WaveformStatus& s):sample(s.sample),timeFrame(s.timeFrame),sampleMean(s.sampleMean),timeFrameMean(s.timeFrameMean), clusterModified(s.clusterModified){}
        ~WaveformStatus(){}
        void setSampleStatus(Status status){sample = status;}
        Status sampleStatus() const {return sample;}
        void setTimeFrameStatus(Status status){timeFrame = status;}
        Status timeFrameStatus() const {return timeFrame;}
        void setSampleMeanStatus(Status status){sampleMean = status;}
        Status sampleMeanStatus() const {return sampleMean;}
        void setTimeFrameMeanStatus(Status status){timeFrameMean = status;}
        Status timeFrameMeanStatus() const {return timeFrameMean;}
        bool isInProcess() const {
            if(sample == IN_PROCESS || timeFrame == IN_PROCESS || sampleMean == IN_PROCESS || timeFrameMean == IN_PROCESS) return true;
            else return false;
        }
        void setClusterModified(bool modified){clusterModified = modified;}
        bool isClusterModified()  const {return clusterModified;}
    private:
        Status sample;
        Status timeFrame;
        Status sampleMean;
        Status timeFrameMean;
        bool clusterModified;
    };

    class Waveforms;
    friend class Waveforms;

    /**
  * Represents waveform data for a cluster:
  * This class is the purely virtual parent.
  */
    class Waveforms{

    public:
        virtual ~Waveforms(){}
        dataType indexOfTimeEnd() const {return timeEndIndex;}
        void setIndexOfTimeEnd(dataType index){timeEndIndex = index;}
        dataType startTime() const {return timeStart;}
        void setStartTime(dataType time){timeStart = time;}
        dataType endTime() const {return timeEnd;}
        void setEndTime(dataType time){timeEnd = time;}
        dataType nbOfSpikes(WaveformMode waveformMode){
            mode = waveformMode;
            if(waveformMode == SAMPLE) return nbSampleSpikes;
            else return nbTimeFrameSpikes;
        }
        dataType nbOfSpikesAsked() const {return nbSpikesAsked;}
        void setNbOfSpikesAsked(dataType nb) {nbSpikesAsked = nb;}
        void setMode(WaveformMode waveformMode){mode = waveformMode;}

        virtual void setSize(dataType size,WaveformMode waveformMode) = 0;
        virtual dataType getSample(dataType index) const = 0;
        virtual dataType getTimeFrame(dataType index) const  = 0;
        virtual dataType getSampleMean(dataType index) const  = 0;
        virtual dataType getTimeFrameMean(dataType index) const  = 0;
        virtual dataType getSampleStDeviation(dataType index) const  = 0;
        virtual dataType getTimeFrameStDeviation(dataType index) const  = 0;

        /** Read up to @p nbSpkToDisplay waveform records from @p spikeFile
         *  for the spikes whose row indices are listed in @p positionOfSpikes.
         *  Stores the samples into the implementation's internal buffer
         *  (sample-mode storage; see setSize / getSample for layout).
         *
         *  Used for "all spikes" mode where the caller wants every member
         *  of the cluster up to a display cap.  @p nbSpikesOfCluster is
         *  the total cluster size (a hint for buffer sizing).
         */
        virtual void read(SortableTable& positionOfSpikes,dataType nbSpikesOfCluster,FILE* spikeFile,dataType nbSpkToDisplay) = 0;

        /** Read the contiguous range [@p currentSpikeIndex .. @p end] of
         *  spike records from @p spikeFile into the implementation's
         *  time-frame buffer.  Used for time-frame display where only
         *  spikes within a time window are shown; @p currentSpikeIndex
         *  is updated on return to point past the last record read so
         *  the caller can resume scanning.
         */
        virtual void read(SortableTable& positionOfSpikes,dataType nbSpikesOfCluster,FILE* spikeFile,dataType& currentSpikeIndex,dataType end) = 0;

        virtual void calculateMean(WaveformMode waveformMode) = 0;

    protected:
        Waveforms(Data& d,dataType nbSampleSpikes = 0,dataType nbTimeFrameSpikes = 0,dataType index = 0,dataType startTime = 0,dataType endTime = 0)
            :data(d),
             timeEndIndex(index),
             timeStart(startTime),
             timeEnd(endTime),
             nbPtsBySpike(data.nbChannels * data.nbSamplesInWaveform),
             nbSpikesAsked(0){
        }

    protected:
        Data& data;
        dataType timeEndIndex;
        dataType timeStart;
        dataType timeEnd;
        dataType nbSampleSpikes;
        dataType nbTimeFrameSpikes;
        WaveformMode mode = SAMPLE;
        int nbPtsBySpike;
        dataType nbSpikesAsked;
    } ;

    template <class T>
    class WaveformData;

    friend class WaveformData<class T>;

    /**
  * Represents waveform data for a cluster. Has the spike information format
  * relyes on the recording system (either a 12 or 16 bits recording system),
  * this class is a template to allow the internal use type to be a short or a long int.
  */
    template <class T>
    class WaveformData : public Waveforms {

    public:
        explicit WaveformData(Data& d,dataType nbSampleSpikes = 0,dataType nbTimeFrameSpikes = 0,dataType index = 0,dataType startTime = 0,dataType endTime = 0):
            Waveforms(d,nbSampleSpikes,nbTimeFrameSpikes,index,startTime,endTime){
            // std::vector members are default-constructed empty
        }
        ~WaveformData(){
            // std::vector members clean up automatically
        }
        /**Specifies the number of spikes which can be store.*/
        void setSize(dataType size,WaveformMode waveformMode = SAMPLE);
        dataType getSample(dataType index) const override {
            return static_cast<dataType>(sampleSpikesTable[index]);
        }
        dataType getTimeFrame(dataType index) const override {
            return static_cast<dataType>(timeFrameSpikesTable[index]);
        }
        dataType getSampleMean(dataType index) const override {
            return static_cast<dataType>(sampleMeanTable[index]);
        }
        dataType getTimeFrameMean(dataType index) const override {
            return static_cast<dataType>(timeFrameMeanTable[index]);
        }
        dataType getSampleStDeviation(dataType index) const override {
            return static_cast<dataType>(sampleStDeviationTable[index]);
        }
        dataType getTimeFrameStDeviation(dataType index) const override {
            return static_cast<dataType>(timeFrameStDeviationTable[index]);
        }
        void read(SortableTable& positionOfSpikes,dataType currentSpikeIndex,FILE* spikeFile,dataType nbSpkToDisplay) override;
        void read(SortableTable& positionOfSpikes,dataType nbSpikesOfCluster,FILE* spikeFile,dataType& currentSpikeIndex,dataType end) override;
        void calculateMean(WaveformMode waveformMode = SAMPLE);
    private:
        std::vector<T> sampleSpikesTable;
        std::vector<T> timeFrameSpikesTable;
        std::vector<T> sampleMeanTable;
        std::vector<T> timeFrameMeanTable;
        std::vector<T> sampleStDeviationTable;
        std::vector<T> timeFrameStDeviationTable;
    } ;


    /**
  * Map containing the waveform status by cluster. Only the clusters
  * for which information have been asked are present in this map.
  */
    QMap<int,WaveformStatus> waveformStatusMap;

    /**
  * Dictionary containing the waveform data by cluster. Only the clusters
  * for which data have been asked are present in this dictionary.
  */
    QHash<QString, Waveforms*> waveformDict;

    /**Compact mean/SD template per cluster; see buildMissingClusterTemplates().
     * Independent of waveformDict, which only covers displayed clusters.*/
    QHash<int, ClusterTemplate> clusterTemplates;

    /**Boolean use to inform the MinMaxThread that an undo or a redo is in process and that it has to stop.*/
    std::atomic_bool undoRedoInProcess;

    /**Boolean use to inform the MinMaxThread that the cluster 0 has changed and that it has to stop.*/
    std::atomic_bool clusterZeroJustModified;

    /**
  * This class stores the information to know which cluster has
  * correlations in process.
  */
    class CorrelationsInProcess{

    public:
        CorrelationsInProcess(){}
        ~CorrelationsInProcess(){}
        void addProcess(dataType clusterId){
            if(clusters.contains(clusterId)) clusters[clusterId]++;
            else {
                clusters.insert(clusterId,1);
                clustersModified.insert(clusterId,false);
            }
        }
        void removeProcess(dataType clusterId){
            if(clusters.contains(clusterId) && clusters[clusterId] > 1)clusters[clusterId]--;
            else if(clusters.contains(clusterId) && clusters[clusterId] == 1){
                clusters.remove(clusterId);
                clustersModified.remove(clusterId);
            }
        }
        void removeCluster(dataType clusterId){
            clusters.remove(clusterId);
            clustersModified.remove(clusterId);
        }
        bool contains(dataType clusterId) const {return clusters.contains(clusterId);}

        void setClusterModified(dataType clusterId,bool modified){clustersModified[clusterId] = modified;}
        bool isClusterModified(dataType clusterId)  const {return clustersModified[clusterId];}

    private:
        QMap<dataType,int> clusters;
        QMap<dataType,bool> clustersModified;
    } ;

    /**Stores the information to know which cluster has
 * correlations in process.*/
    CorrelationsInProcess correlationsInProcess;

    class Correlation;
    friend class Correlation;

    /**
  * Represents correlation data for a pair of clusters.
  */
    class Correlation{

    public:
        explicit Correlation(Data& d):data(d){
            reset();
        }
        Correlation(Data& d,int size,int timeWindow):data(d),binSize(size),timeFrame(timeWindow),status(IN_PROCESS){
            max = 0;
            asymptote = 0;
            nbBins = 0;
            firingRate = 0;
        }
        ~Correlation(){
        }
        void reset(){
            max = 0;
            asymptote = 0;
            binSize = 0;
            timeFrame = 0;
            nbBins = 0;
            firingRate = 0;
        }
        void setStatus(Status s){status = s;}
        Status getStatus() const {return status;}
        Status getStatus(int size,int timeWindow) const {
            if(binSize != size || timeFrame != timeWindow) return NOT_AVAILABLE;
            else return status;
        }
        /**Returns the size of a bin in miliseconds*/
        int getBinSize() const {return binSize;}
        void setBinSize(int size){binSize = size;}
        /**Returns the size of the time window in miliseconds*/
        int getTimeWindow() const {return timeFrame;}
        void setTimeWindow(int timeWindow){timeFrame = timeWindow;}
        uint getMaximum() const {return max;}
        void setMaximum(uint m){max = m;}
        float getShoulder() const {return asymptote;}
        void setShoulder(float s){asymptote = s;}
        /** Compute the cross-correlogram (or auto-correlogram) of the
         *  spike-time sequences in @p spikesOfCluster1 and @p spikesOfCluster2.
         *
         *  Bins time-lag offsets between every spike pair into 2*halfBins+1
         *  buckets of width @p binSizeInRU recording units, restricted to
         *  lags in [-timeWindowInRU, +timeWindowInRU].  Counts are stored
         *  in `values`; firing rate is updated.
         *
         *  @param spikesOfCluster1   Reference (centre-of-window) spike train.
         *  @param spikesOfCluster2   Probe spike train.  Same as cluster 1
         *                            for an autocorrelogram.
         *  @param binSizeInRU        Bin width in recording sample units.
         *  @param timeWindowInRU     Half-width of the histogram in recording units.
         *  @param halfBins           Number of bins per side (so total bins = 2*halfBins+1).
         *  @param autoCorrelogram    If true, the centre bin is zeroed
         *                            (suppress trivial lag-zero self-pairs).
         */
        void calculateCorrelation(SortableTable& spikesOfCluster1,SortableTable& spikesOfCluster2,double binSizeInRU,double timeWindowInRU,int halfBins,bool autoCorrelogram);
        int getNbBins(){return nbBins;}
        void setNbBins(int nb){nbBins = nb;}
        uint getValue(int index){return values[index];}
        float getFiringRate() const {return firingRate;}

    private:
        Data& data;
        std::vector<uint> values;
        Status status = NOT_AVAILABLE;
        int binSize;
        int timeFrame;
        uint max;
        float asymptote;
        int nbBins;
        float firingRate;
    } ;

    /**Dict containing the correlations.
  * Key: a string representing the pair of clusters (id1-id2). The first value of the pair is always the bigger (the correlograms
  * are calculated stored only for (A,B) with A > B and not for (B,A).
  * value: a qdict containing a pair as a key and a Correlation object as a value. The pair represent the the bin size and the time window of the Correlation object.
  */
    QHash< QString, QHash<QString, Correlation*>* > correlationDict;

    /**Excerpt of spikesByCluster for the clusters selected to be recluster.*/
    SortableTable reclusteringSpikesByCluster;

    /** Map given the cluster user information, the key is the cluster id and the value an instance of ClusterUserInformation.
  This map is only used to store the data read from or write to the parameter file (YAML).
  */
    QMap<int,ClusterUserInformation> clusterUserInformationMap;

    //Methods
    /**
  * Fills the undo lists (spikesByClusterUndoList,clusterInfoMapUndoList) to prepare for a future undo.
  * @param spikesByClusterTemp the newly created spikesByCluster array
  * @param clusterInfoMapTemp the newly created ClusterInfoMap map
  */
    void prepareUndo(SortableTable* spikesByClusterTemp,ClusterInfoMap* clusterInfoMapTemp, bool dimensionChanged = false);

    /**
  * Moves the clusters contained in @p clustersToDelete to a the cluster @p destinationId. The correponding spikes are assign to cluster @p destinationId
  * @param clustersToDelete a list of the cluster numbers (in ascending order) identifying the clusters to delete.
  * @param spikesByClusterTemp the new spikesByCluster which will contain the new distribution of the spikes among the clusters
  * @param clusterInfoMapTemp the new clusterInfoMap which will contain the information on the new clusters
  * @param upperInsertionIndex the starting position in spikesByCluster where to insert the spikes which are moved
  * @param nbSpikesInNewCluster the current number of spikes in the new cluster
  * @param destinationId the cluster id of destination (0 for artifact or 1for noise)
  * @param positions list containing, for each cluster to delete,
  * the position of its first spike in the new cluster's spikes (starting from the first spike of the new cluster).
  * @param nbOfspikes list containing, for each cluster to delete, its number of spikes.
  */
    void moveClusters(QList<int>& clustersToDelete,SortableTable* spikesByClusterTemp,ClusterInfoMap* clusterInfoMapTemp,long upperInsertionIndex,long& nbSpikesInNewCluster,int destinationId,QList<long>& positions,QList<long>& nbOfspikes);

    /**Creates a new thread to calculate the min and max of the dimensions when the cluster 0 is modified.*/
    MinMaxThread* minMaxCalculator();

    /**
  * Gets the waveform points for cluster @p clusterId in the sample mode.
  * Take a sample of the spikes evenly distributed on all the recording.
  * @param clusterId id of the cluster to get waveform information for.
  * @param nbSpkToDisplay number of spikes to display.
  * @return the status, READY if the data have already been collected or the current collection is finish,
  * and IN_PROCESS if an other thread is already treating @p clusterId.
  */
    Status getSampleWaveformPoints(int clusterId,dataType nbSpkToDisplay);

    /**
  * Gets the waveform points for cluster @p clusterId in time frame mode.
  * Take all the spikes in a given time frame.
  * @param clusterId id of the cluster to get waveform information for.
  * @param start starting time in second
  * @param end ending time in second.
  * @return the status, READY if the data have already been collected or the current collection is finish,
  * and IN_PROCESS if an other thread is already treating that cluster.
  */
    Status getTimeFrameWaveformPoints(int clusterId,dataType start,dataType end);

    /**
  * Calculates the mean and the standard deviation for cluster @p clusterId in the sample mode.
  * In that mode, the spikes used are a sample of the spikes evenly distributed on all the recording.
  * @param clusterId id of the cluster to calculate the data for.
  * @param nbSpkToDisplay number of spikes diplayed.
  * @return the status, READY if the data have already been calculated or the current calculation is finish,
  * IN_PROCESS if an other thread is already treating that cluster and NOT_AVAILABLE
  * if the spikes have not been collected yet.
  */
    Status calculateSampleMean(int clusterId,dataType nbSpkToDisplay);

    /**
  * Calculates the mean and the standard deviation for cluster @p clusterId in the time frame mode.
  * In that mode, all the spikes in a given time frame are selected.
  * @param clusterId id of the cluster to calculate the data for.
  * @param start starting time in second
  * @param end ending time in second.
  * @return the status, READY if the data have already been calculated or the current calculation is finish,
  * IN_PROCESS if an other thread is already treating that cluster and NOT_AVAILABLE
  * if the spikes have not been collected yet.
  */
    Status calculateTimeFrameMean(int clusterId,dataType start,dataType end);

    /**
  * Remove all the correlations link to the cluster @p clusterId. This mean remove the
  * corresponding entries from correlationMap.
  * @param clusterId id of the cluster for which the cleaning has been asked.
  * @param currentClusterList list of the clusters to look for cleaning.
  * @param cleanProcess true if the cluster has to be remove from correlationInProcess false otherwise.
  * The default is false.
  */
    void cleanCorrelation(dataType clusterId,const QList<dataType>& currentClusterList,bool cleanProcess = false);

    /**
  * Renumber all the correlations.
  * @param clusterIdsOldNew map between old and new cluster ids.
  */
    void renumberCorrelation(QMap<int,int>& clusterIdsOldNew);

    /**Returns the time corresponding to a spike.
  * @param spikesOfCluster one row SortableTable corresponding to the position of the cluser's spikes in
  * features sorted by position.
  * @param spike position.
  */
    double spikeTime(SortableTable& spikesOfCluster,dataType spike){
        // Guard the snapshot-column index against the clusterInfoMap/row-table
        // count desync.  A stale clusterInfoMap[id].nbSpikes()==0 makes
        // spikePositions() build the snapshot with subset(.,1,first,first-1) --
        // a zero-length subset (a 1x0 SortableTable whose buffer is null) -- yet
        // still return true, so an empty snapshot reaches calculateCorrelation.
        // It then calls this with spike==cluster2NbSpikes==0 (the break test) and
        // spike==1 (the autocorrelogram time), and spikesOfCluster(1,spike) runs
        // Array::operator() with an out-of-range column: array[(0)*0 + (spike-1)]
        // == array[-1] on a null buffer -> segfault, on the background
        // CorrelationThread.  Array::operator()'s assert is a release no-op so it
        // is not caught.  0074 clamped the *feature-row* index below but left this
        // *snapshot-column* index unguarded, which is why the correlogram crash
        // survived it.  Clamp into range; an empty snapshot has no spike time, so
        // return 0.0 (the same bail the featRows<1 branch takes).  Hot per-spike
        // read path, so no qWarning; the desync is surfaced by the
        // checkClusterInfoMapInvariant / checkSpikeFeatureInvariant probes.
        const dataType nbSnapshotSpikes = static_cast<dataType>(spikesOfCluster.nbOfColumns());
        if(nbSnapshotSpikes < 1) return 0.0;
        if(spike < 1)                     spike = 1;
        else if(spike > nbSnapshotSpikes) spike = nbSnapshotSpikes;
        dataType currentPositionInFeatures = spikesOfCluster(1,spike);
        // Guard the feature-row index against the spikesByCluster/features
        // desync (a stale spikesByCluster(1,*) value that exceeds the feature
        // matrix) which also crashes the centroid and snapshot paths.  Array::
        // operator()'s assert is a release no-op, so an out-of-range row reads
        // past the matrix and segfaults this reader — fatal here because it runs
        // on a background CorrelationThread.  Clamp into range: a desynced spike
        // then lands in a slightly-wrong correlogram bin instead of crashing.
        // The desync itself is surfaced by checkSpikeFeatureInvariant /
        // checkClusterInfoMapInvariant; this is the hot read path, so no warn.
        const dataType featRows = static_cast<dataType>(features.nbOfRows());
        if(featRows < 1) return 0.0;
        if(currentPositionInFeatures < 1)             currentPositionInFeatures = 1;
        else if(currentPositionInFeatures > featRows) currentPositionInFeatures = featRows;
        return static_cast<double>(features(currentPositionInFeatures,nbDimensions));
    }

    /**Sorts by time the spikes of a newly created cluster created from other clusters, knowing
  * that the spikes from the other clusters are already sorted.
  * @param clusterInfoMapTemp the new clusterInfoMap which will contain the information on the new clusters.
  * @param spikesByClusterTemp the new spikesByCluster which will contain the new distribution of the spikes among the clusters.
  * @param clusterId the id of the cluster to sort.
  * @param positions list containing, for each contributing cluster,
  * the position of either its last spike (if @p fromTop is false) or its first spike (if @p fromTop is true)
  * in the new cluster's spikes.
  * @param nbOfspikes list containing, for each contributing cluster, its number of spikes.
  * @param step equals -1 if the contributing clusters are sorted from the bigger to the smaller
  * and equals 1 if the contributing clusters are sorted from the smaller to the bigger.
  * @param fromTop true if the position is given starting from the first spike of the new cluster, false if
  * it is given starting from the last spike of the new cluster. The default is false.
  */
    void sortCluster(ClusterInfoMap* clusterInfoMapTemp,SortableTable* spikesByClusterTemp,dataType clusterId,QList<dataType> positions,QList<dataType> nbOfspikes,int step,bool fromTop = false);

    /**
  * Sorts by time the spikes of a newly created cluster created from other clusters.
  * The function is an overload of the previous one for the cases where the cluster to sort is
  * either the cluster 0 or the cluster 1. In that case the spikes coming from those clusters
  * are sorted from the smaller time to the bigger whereas the other clusters still
  * sorted from the bigger to the smaller.
  * @param clusterInfoMapTemp the new clusterInfoMap which will contain the information on the new clusters.
  * @param spikesByClusterTemp the new spikesByCluster which will contain the new distribution of the spikes among the clusters.
  * @param clusterId the id of the cluster to sort.
  * @param lastPositions list containing, for each contributing cluster,
  * the position of its last spike in the new cluster's spikes (starting from the last spike
  * of the new cluster).
  * @param nbOfspikes list containing, for each contributing cluster, tits number of spikes.e
  * @param firstPosition position of the first spike of the cluster 0 or 1.
  * @param number number of spikes of either the cluster 0 or 1.
  */
    void sortCluster(ClusterInfoMap* clusterInfoMapTemp,SortableTable* spikesByClusterTemp,dataType clusterId,QList<dataType> lastPositions,QList<dataType> nbOfspikes,dataType firstPosition,dataType number);


    /**
  * Finds the closest spike to a given time @p time among the pikes contain in @p spikesOfCluster.
  * @param time the time look up.
  * @param spikesOfCluster array of sorted spikes in which to look up.
  */
    long findSpikePosition(double time,SortableTable& spikesOfCluster);

    /**
  * Makes a copy of the internal variables, spikesOfCluster and clusterInfoMap, used to store
  * insformation about the clusters.
  * @param spikesOfClusterTemp pointer on a SortableTable which will point on a copy of the internal variable spikesOfCluster.
  * @param clusterInfoMapTemp pointer on a ClusterInfoMap which will point on a copy of the internal variable clusterInfoMap.
  */
    void duplicate(SortableTable* & spikesOfClusterTemp,ClusterInfoMap* & clusterInfoMapTemp);

public:

    /**This class a wrapper to waveform information (spikes, mean value and standard deviation).
  */
    class WaveformIterator{
    public:
        virtual ~WaveformIterator(){}
        void setSpikesAvailable(bool available){spikesAvailable = available;}
        bool areSpikesAvailable(){return spikesAvailable;}
        void setMeanAvailable(bool available){meanAvailable = available;}
        bool isMeanAvailable(){return meanAvailable;}
        virtual dataType nextSpike(){return 0;}
        virtual dataType nextMeanValue(){return 0;}
        virtual dataType nextStDeviationValue(){return 0;}
        virtual dataType nbOfSpikes() const{return 0;}

        WaveformIterator(){init();}

    protected:
        explicit WaveformIterator(Waveforms* waveformsData){
            init();
            waveforms = waveformsData;
        }
        void init(){
            waveforms = nullptr;
            spikesIndex = -1;
            meanIndex = -1;
            stDeviationIndex = -1;
            spikesAvailable = false;
            meanAvailable = false;
        };

        Waveforms* waveforms;
        dataType spikesIndex;
        dataType meanIndex;
        dataType stDeviationIndex;
        bool spikesAvailable;
        bool meanAvailable;
    };



    class SampleWaveformIterator;
    friend class SampleWaveformIterator;

    /**
  * Creates and returns an sampleWaveformIterator on the spikes of the given cluster with the specified @p nbSampleSpikes number of spikes.
  * This iterator iterates on the spikes selected for the sample mode presentation of the waveforms.
  * In that mode, only a sample of the spikes evenly distributed on all the recording are available.
  * @param clusterId the number of the cluster on which spikes the iterator will iterates.
  * @param nbSampleSpikes number of spikes selected for the sample mode presentation.
  * @return the sampleWaveformIterator on the spikes of the given cluster.
  */
    SampleWaveformIterator* sampleWaveformIterator(dataType clusterId,dataType nbSampleSpikes){
        QString clusterIdString = QString::fromLatin1("%1").arg(clusterId);
        int clusterIdInt = static_cast<int>(clusterId);
        SampleWaveformIterator* waveformIterator;

        if(waveformStatusMap.contains(clusterIdInt)){
            Waveforms* waveforms = waveformDict[clusterIdString];
            waveformIterator = new SampleWaveformIterator(waveforms);
            if(waveformStatusMap[clusterIdInt].sampleMeanStatus() == READY)
                waveformIterator->setMeanAvailable(true);
            if(waveformStatusMap[clusterIdInt].sampleStatus() == READY){
                waveformIterator->setSpikesAvailable(true);
                waveformIterator->updateStatus(nbSampleSpikes);
            }
        }
        else{
            //No data are available, create any of the iterator (they will have their
            //data availability booleans to false).
            //The case is for security reason but should never be reach.
            waveformIterator = new SampleWaveformIterator();
        }
        return waveformIterator;
    }

    /** Returns an iterator on the latest waveform data stored by a request of
   * a waveformTread for a given cluster. The data correspond to the sample display mode
   * (only a sample of the spikes evenly distributed on all the recording were collected).
   * Caution: in Qt graphical coordinate system, the Y axis is inverted (increasing downwards),
   * thus a point (x,y) as to be drawn and tested as (x,-y).
   * The value returns by the iterator is the spike sample value taking the Qt graphical
    * coordinate system into consideration, the ordinate coordinate is the opposite of the raw data.
  */
    class SampleWaveformIterator: public WaveformIterator{
        //Only the method iterator of data has access to the protected part of Iterator,
        //the constructor of Iterator being private, only this method con create a new Iterator
        friend SampleWaveformIterator* Data::sampleWaveformIterator(dataType clusterId,dataType nbSampleSpikes);

    public:
        ~SampleWaveformIterator(){}
        dataType nextSpike() override {
            ++spikesIndex;
            return - static_cast<dataType>(waveforms->getSample(spikesIndex));
        }
        dataType nextMeanValue() override {
            ++meanIndex;
            return - static_cast<dataType>(waveforms->getSampleMean(meanIndex));
        }
        dataType nextStDeviationValue() override {
            ++stDeviationIndex;
            return - static_cast<dataType>(waveforms->getSampleStDeviation(stDeviationIndex));
        }
        dataType nbOfSpikes() const override {
            return waveforms->nbOfSpikes(SAMPLE);
        }
    private:
        SampleWaveformIterator(): WaveformIterator(){}
        explicit SampleWaveformIterator(Waveforms* waveformsData): WaveformIterator(waveformsData){}
        void updateStatus(dataType nbSampleSpikes){
            if(waveforms->nbOfSpikesAsked() != nbSampleSpikes){
                setSpikesAvailable(false);
                setMeanAvailable(false);
            }
        }

    };

    class TimeFrameWaveformIterator;
    friend class TimeFrameWaveformIterator;

    /**
  * Creates and returns an TimeFrameWaveformIterator on the spikes of the given cluster.
  * This iterator iterates on the spikes selected for the time frame mode presentation of the waveforms.
  * In that mode, all the spikes in a given time frame are available.
  * @param clusterId the number of the cluster on which spikes the iterator will iterates.
  * @param startTime starting time selected for the time frame mode.
  * @param endTime ending time selected for the time frame mode.
  * @return the TimeFrameWaveformIterator on the spikes of the given cluster.
  */
    TimeFrameWaveformIterator* timeFrameWaveformIterator(dataType clusterId,dataType startTime,dataType endTime){
        QString clusterIdString = QString::fromLatin1("%1").arg(clusterId);
        int clusterIdInt = static_cast<int>(clusterId);
        TimeFrameWaveformIterator* waveformIterator;

        if(waveformStatusMap.contains(clusterIdInt)){
            Waveforms* waveforms = waveformDict[clusterIdString];
            waveformIterator = new TimeFrameWaveformIterator(waveforms);
            if(waveformStatusMap[clusterIdInt].timeFrameMeanStatus() == READY) waveformIterator->setMeanAvailable(true);
            if(waveformStatusMap[clusterIdInt].timeFrameStatus() == READY){
                waveformIterator->setSpikesAvailable(true);
                waveformIterator->updateStatus(startTime,endTime);
            }
        }
        else{
            //No data are available, create any of the iterator (they will have their
            //data availability booleans to false).
            //The case is for security reason but should never be reach.
            waveformIterator = new TimeFrameWaveformIterator();
        }
        return waveformIterator;
    }

    /** Returns an iterator on the latest waveform data stored by a request of
   * a waveformTread for a given cluster. The data correspond to the time frame display mode
   * (ll the spikes in a given time frame are available).
   * Caution: in Qt graphical coordinate system, the Y axis is inverted (increasing downwards),
   * thus a point (x,y) as to be drawn and tested as (x,-y).
   * The value returns by the iterator is the spike sample value taking the Qt graphical
    * coordinate system into consideration, the ordinate coordinate is the opposite of the raw data.
  */
    class TimeFrameWaveformIterator: public WaveformIterator{
        //Only the method iterator of data has access to the protected part of Iterator,
        //the constructor of Iterator being private, only this method con create a new Iterator
        friend TimeFrameWaveformIterator* Data::timeFrameWaveformIterator(dataType clusterId,dataType startTime,dataType endTime);

    public:
        ~TimeFrameWaveformIterator(){}
        dataType nextSpike() override {
            ++spikesIndex;
            return - static_cast<dataType>(waveforms->getTimeFrame(spikesIndex));
        }
        dataType nextMeanValue() override {
            ++meanIndex;
            return - static_cast<dataType>(waveforms->getTimeFrameMean(meanIndex));
        }
        dataType nextStDeviationValue() override {
            ++stDeviationIndex;
            return - static_cast<dataType>(waveforms->getTimeFrameStDeviation(stDeviationIndex));
        }
        dataType nbOfSpikes() const override {
            return waveforms->nbOfSpikes(TIME_FRAME);
        }

    private:
        TimeFrameWaveformIterator(): WaveformIterator(){}
        explicit TimeFrameWaveformIterator(Waveforms* waveformsData): WaveformIterator(waveformsData){}
        void updateStatus(dataType start,dataType end){
            if(waveforms->startTime() != start || waveforms->endTime() != end){
                setSpikesAvailable(false);
                setMeanAvailable(false);
            }
        }

    };
    /**
  * Calculates the correlograms for each pair of clusters given in pairs.
  * @param pair pair of clusters for which a correlogram has to be compute.
  * @param binSize size of the bins to compute given in miliseconds.
  * @param timeWindow time frame use to compute the correlograms, given in miliseconds.
  * @param binSizeInRU size of the bins to compute given in recording units.
  * @param timeWindowInRU half of the time frame use to compute the correlograms, given in recording units.
  * @param halfBins  the number of bins to compute are so there are a total of nBins = 1+2*halfBins bins
  * (halfBins.5 for each halfTimeWindow).
  * @return the status, READY if the data have already been calculated or the asked computation is finish,
  * and IN_PROCESS if an other thread is already treating the pairs the thread has to do.
  */
    Status getCorrelograms(Pair& pair,int binSize,int timeWindow,double binSizeInRU,float timeWindowInRU,int halfBins);

    class CorrelogramIterator;
    friend class CorrelogramIterator;

    /**
  * Creates and returns an iterator on the correlogram data of the given pair of clusters (@p pair).
  * @param pair the pair of clusters on which correlogram data the iterator will iterates.
  * @param scale the scale to applied to the correlogram's data (maximum value, shoulder value or no scale).
  * @param binSize size of the bin used to compute the correlogram.
  * @param timeframe time frame used to compute the correlogram.
  * @return the iterator on the correlogram data of the given pair.
  */
    CorrelogramIterator correlogramIterator(Pair pair,ScaleMode scale,int binSize,int timeframe){
        return CorrelogramIterator(*this,pair,scale,binSize,timeframe);
    }

    /** Specialized iterator on the latest correlation data stored by a request of
   * a correlationTread for a given pair of clusters. The data correspond to the binSize and
   * timeFrame specified by the correlationTread.
   * Caution: in Qt graphical coordinate system, the Y axis is inverted (increasing downwards),
   * thus a point (x,y) as to be drawn and tested as (x,-y).
   * The value returns by the iterator is either the raw correlogram value or the values scaled
   * either the maximum of the correlogram or the shoulder. The values return take the Qt graphical
   * coordinate system into consideration, the ordinate coordinate is the opposite of the raw data.
  */
    class CorrelogramIterator{
        //Only the method correlogramIterator of data has access to the private part of CorrelogramIterator,
        //the constructor of CorrelogramIterator being private, only this method con create a new CorrelogramIterator
        friend CorrelogramIterator Data::correlogramIterator(Pair pair,ScaleMode scale,int binSize,int timeframe);

    public:
        ~CorrelogramIterator(){}
        /**Returns the current value and increments the iterator.*/
        float next(){
            float value =  - (static_cast<float>(correlation->getValue(index)) / static_cast<float>(scale));
            index++;
            return value;
        }
        /**Check if there is more values.*/
        bool hasNext(){return (lastIndex >= index);}
        /**Returns true if there is data corresponding to the parameters given
    * in the iterator constructor.
    */
        bool isDataAvailable(){return dataAvailable;}

        float getShoulder() const {return - correlation->getShoulder();}

        float getScaledShoulder() const {return - correlation->getShoulder() / static_cast<float>(scale);}

        float getFiringRate() const {return correlation->getFiringRate(); }
    private:
        CorrelogramIterator(const Data& d,Pair pair,ScaleMode scaleMode,int binSize,int timeframe):data(d){
            index = 0;
            lastIndex = -1;
            QHash<QString, Correlation*>* dict = data.correlationDict[pairKey(pair)];
            if(dict == nullptr) dataAvailable = false;
            else{
                correlation = (*dict)[pairKey(binSize, timeframe)];
                if(correlation == nullptr) dataAvailable = false;
                else{
                    if(correlation->getStatus(binSize,timeframe) == READY){
                        dataAvailable = true;
                        lastIndex = correlation->getNbBins() - 1;
                        switch(scaleMode){
                        case RAW:
                            scale = 1;
                            break;
                        case MAX:
                            scale = static_cast<float>(correlation->getMaximum());
                            break;
                        case SHOULDER:
                            scale = correlation->getShoulder();
                            break;
                        }
                    }
                    else dataAvailable = false;
                }
            }
        };
        /**Returns true if the iterator has reach the last spike for the cluster on which it iterates,
      * false otherwise.
      */
        const Data& data;
        long index;
        long lastIndex;
        bool dataAvailable;
        Data::Correlation* correlation;
        float scale;
    };

    /**
  * Loads the clusters created by the automatic reclustering program.
  * @param clusterFile the cluster file created by the automatic reclustering program.
  * @return true if the loading succeeded, false otherwise
  */
    bool loadReclusteredClusters(QFile &clusterFile);

    /**
  * Load the features data and configuration data when the cluster file does not exist.
  * Initialize the internal representation of the data.
  * @param featureFile the .fet file
  * @param spkFileLength the length of the .spk.i file
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the initialization succeded false otherwise
  */
    bool initialize(QFile &featureFile, long spkFileLength, QString& errorInformation);

    /**
  * Load the features data, cluster data and configuration data.
  * Initialize the internal representation of the data.
  * @param featureFile the .fet file
  * @param clusterFile the .clu.i file
  * @param spkFileLength the length of the .spk.i file
  * @param errorInformation string which, in case of an error, will contain detail about it.
  * @return true if the initialization succeded false otherwise
  */
    bool initialize(QFile &featureFile, QFile &clusterFile, long spkFileLength, QString& errorInformation);

};

#endif
