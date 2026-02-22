/***************************************************************************
 * spikerealign.h
 *
 * Engine for spike waveform re-alignment within a single electrode group.
 *
 * Algorithm overview
 * ------------------
 * 1. Read all waveforms for the selected cluster from the .spk file.
 * 2. Compute the cluster's mean waveform (per sample per channel).
 * 3. For each spike, find the shift in [-maxShift, +maxShift] samples
 *    that maximises cross-correlation with the mean waveform on the
 *    channel with the largest absolute peak amplitude.
 * 4. For each spike where the optimal shift is non-zero:
 *    a. Update the in-memory features timestamp column by adding the shift.
 *    b. Rewrite the .res file entry for this spike.
 *    c. Re-extract the shifted waveform from the .fil/.dat file and write
 *       it back to the .spk file at the same slot.
 *    d. Re-project the new waveform through the saved PCA eigenvectors
 *       (.evec file) and update the .fet file rows for this spike.
 *    e. If the updated timestamp is now out of chronological order with
 *       neighbouring spikes, swap ALL the on-disk records (res, spk, clu)
 *       and swap the in-memory spikesByCluster row as well.
 *
 * File formats handled
 * --------------------
 *  .res  – one int64 timestamp per line (text)
 *  .spk  – raw int16 waveforms, spike-major, channel-minor (binary)
 *  .fet  – text: first line = nDimensions; then one row per spike
 *  .clu  – text: first line = nClusters; then one cluster-id per spike
 *  .fil  – raw int16, all-channel, sample-major (the high-pass filtered file
 *           used for re-extraction; falls back to .dat if absent)
 *  .evec – binary eigenvector file written by process_pca
 ***************************************************************************/

#pragma once

#include <QString>
#include <QObject>
#include <QVector>
#include <QList>

// Forward declaration to avoid including data.h in every TU
class Data;

/* ---------------------------------------------------------------------------
 * PCA eigenvector data for one electrode group (loaded from .evec file)
 * --------------------------------------------------------------------------- */
struct PcaEigenvectors {
    int nChannels   = 0;
    int data2use    = 0;   // waveform samples used as PCA input
    int nComponents = 0;
    bool isCentered = false;
    int  recShift   = 0;   // first sample index in the full waveform (0 when no -b/-a)

    // means[ch][sample]  – per-channel per-sample mean (subtract before projection)
    QVector<QVector<double>> means;

    // evec[ch]  – column-major (data2use × nComponents)
    // evec[ch][row + col*data2use] = element (row, col)
    QVector<QVector<double>> evec;

    bool valid() const { return nChannels > 0 && data2use > 0 && nComponents > 0; }
};

/* ---------------------------------------------------------------------------
 * RealignResult – summary of what changed after realigning one cluster
 * --------------------------------------------------------------------------- */
struct RealignResult {
    int  nRealigned   = 0;   // spikes whose optimal shift was non-zero
    int  nSwapped     = 0;   // spikes that had to be swapped with a neighbour
    bool success      = true;
    QString errorMessage;
};

/* ---------------------------------------------------------------------------
 * SpikeRealign – the main engine (not a QThread; call from a worker thread)
 * --------------------------------------------------------------------------- */
class SpikeRealign : public QObject
{
    Q_OBJECT

public:
    /**
     * @param data         The loaded Data object (klusters main data store)
     * @param clusterId    Which cluster to realign
     * @param basePath     Full path to session (directory + "/" + basename), e.g. "/data/exp/rec"
     * @param groupId      Electrode group number (e.g. 1)
     * @param maxShift     Maximum allowed shift in samples (default 8)
     * @param parent
     */
    explicit SpikeRealign(Data* data,
                          int   clusterId,
                          const QString& basePath,
                          int   groupId,
                          int   maxShift = 8,
                          QObject* parent = nullptr);

    /** Run the realignment synchronously. Emits progress() during execution. */
    RealignResult run();

    /** Load eigenvectors from a .evec file. Returns an invalid struct on failure. */
    static PcaEigenvectors loadEigenvectors(const QString& evecPath);

signals:
    /** Emitted periodically so a UI progress bar can be updated (0–100). */
    void progress(int percent);

private:
    Data*   m_data;
    int     m_clusterId;
    QString m_basePath;
    int     m_groupId;
    int     m_maxShift;

    // Derived paths
    QString resPath()  const;
    QString spkPath()  const;
    QString fetPath()  const;
    QString cluPath()  const;
    QString filPath()  const;   // .fil first, falls back to .dat
    QString evecPath() const;

    // Internal helpers
    bool readSpkWaveforms(const QVector<long long>& spikeGlobalIndices,
                          QVector<QVector<short>>&  waveforms);

    int  findBestShift(const QVector<short>& spike,
                       const QVector<short>& meanWaveform,
                       int nChan, int nSamples, int peakChan,
                       int maxShift);

    bool rewriteSpkSlot(long long globalIdx1based,
                        const QVector<short>& newWaveform);

    bool projectWaveform(const QVector<short>& waveform,
                         const PcaEigenvectors& pca,
                         QVector<double>& features);

    bool readAllRes(QVector<long long>& timestamps);
    bool writeAllRes(const QVector<long long>& timestamps);

    bool readAllClu(QVector<int>& clusterIds, int& nClustersHeader);
    bool writeAllClu(const QVector<int>& clusterIds, int nClustersHeader);

    // Swap all on-disk data (res/spk/clu) for two global spike indices (1-based).
    // Also swaps their rows in the in-memory spikesByCluster.
    bool swapSpikes(long long idxA, long long idxB,
                    QVector<long long>& allRes,
                    QVector<int>& allClu,
                    int nClustersHeader);
};
