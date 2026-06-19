/***************************************************************************
 * realignworker.h
 *
 * Background worker for spike realignment.
 ***************************************************************************/

#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QList>

class KlustersDoc;

class RealignWorker : public QObject
{
    Q_OBJECT

public:
    explicit RealignWorker(KlustersDoc* doc, int clusterId,
                        const QString& args = QString(),
                        QObject* parent = nullptr);

    /** Switch this worker into batch mode: run() then loops realignSpikes over
     *  @p ids back-to-back in this one thread (emitting clusterDone per cluster
     *  and finished once at the end), instead of doing a single cluster.  This
     *  collapses the per-cluster QThread-spawn + GUI-signal round-trip that
     *  otherwise dominates a PCA-Center Align All. */
    void setBatch(const QList<int>& ids) { clusterIds = ids; }

public slots:
    void run();
    void cancel();

signals:
    void logLine(const QString& line, bool isError);

    /** Batch mode only: emitted after each cluster in the list is processed,
     *  so the GUI can update the progress bar / counters without a per-cluster
     *  thread round-trip.
     *  @param pos        1-based index of the cluster just finished.
     *  @param total      total clusters in the batch.
     *  @param clusterId  the cluster id just processed.
     *  @param ok         realignSpikes returned true.
     *  @param nShifted   spikes shifted for this cluster. */
    void clusterDone(int pos, int total, int clusterId, bool ok, int nShifted);

    /** Emitted when the realignment job finishes.
     *  @param ok          true = completed successfully.
     *  @param nShifted    spikes shifted.
     *  @param nSwapped    sort-order corrections.
     *  @param meanBefore  cluster mean waveform before alignment (channel-major float).
     *  @param meanAfter   cluster mean waveform after alignment (channel-major float).
     *  @param backupBase  base path of .realign_bak files (empty if backup failed).
     *  @param nChan       number of channels.
     *  @param nSamp       samples per channel.
     */
    void finished(bool ok, int nShifted, int nSwapped,
                  QVector<float> meanBefore, QVector<float> meanAfter,
                  QString backupBase, int nChan, int nSamp);

private:
    KlustersDoc* doc;
    int          clusterId;
    QString      args;
    bool         cancelRequested;
    QList<int>   clusterIds;   // non-empty → batch mode (see setBatch)
};
