/***************************************************************************
 * realignworker.h
 *
 * Background worker for spike realignment.
 ***************************************************************************/

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class KlustersDoc;

class RealignWorker : public QObject
{
    Q_OBJECT

public:
    explicit RealignWorker(KlustersDoc* doc, int clusterId,
                        const QString& args = QString(),
                        QObject* parent = nullptr);

public slots:
    void run();
    void cancel();

signals:
    void logLine(const QString& line, bool isError);

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
    KlustersDoc* m_doc;
    int          m_clusterId;
    QString      m_args;
    bool         m_cancel;
};
