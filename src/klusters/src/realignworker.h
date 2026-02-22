/***************************************************************************
 * realignworker.h
 *
 * Background worker for spike realignment.
 *
 * KlustersDoc::realignSpikes() does substantial file I/O and computation.
 * Running it on the GUI thread would freeze the main window for large
 * clusters (seconds to tens of seconds).  This worker moves the call to a
 * QThread and streams per-line diagnostic output back to the main thread via
 * Qt signals, mirroring the pattern used by ProcessWidget for reclustering.
 *
 * Usage (from KlustersApp::slotRealignSpikes):
 *   auto* worker = new RealignWorker(doc, clusterId);
 *   auto* thread = new QThread;
 *   worker->moveToThread(thread);
 *   connect(thread, &QThread::started,  worker, &RealignWorker::run);
 *   connect(worker, &RealignWorker::logLine, ...);
 *   connect(worker, &RealignWorker::finished, ...);
 *   thread->start();
 ***************************************************************************/

#pragma once

#include <QObject>
#include <QString>

class KlustersDoc;

class RealignWorker : public QObject
{
    Q_OBJECT

public:
    explicit RealignWorker(KlustersDoc* doc, int clusterId,
                        const QString& args = QString(),
                        QObject* parent = nullptr);

public slots:
    /** Called on the worker thread by QThread::started. */
    void run();

    /** Request cancellation.  The running realignSpikes() call will not be
     *  interrupted mid-way (it holds no cancellation point), but the worker
     *  will not start a new cluster if batch-mode is ever added. */
    void cancel();

signals:
    /** Emitted for each line of diagnostic output (thread-safe via Qt::QueuedConnection). */
    void logLine(const QString& line, bool isError);

    /** Emitted when the realignment job finishes.
     *  @param ok        true = completed successfully.
     *  @param nShifted  number of spikes shifted.
     *  @param nSwapped  number of sort-order corrections performed.
     */
    void finished(bool ok, int nShifted, int nSwapped);

private:
    KlustersDoc* m_doc;
    int          m_clusterId;
    QString      m_args;
    bool         m_cancel;
};
