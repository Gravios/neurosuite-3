#ifndef REALIGNJOB_H
#define REALIGNJOB_H

#include "serialjobqueue.h"
#include "realignworker.h"

#include <QObject>
#include <QThread>
#include <QString>
#include <QVector>
#include <QList>
#include <functional>

class KlustersDoc;

/**
 * RealignJob -- adapts the asynchronous RealignWorker (and its QThread) to the
 * Job interface, so a realignment runs as one item on a SerialJobQueue.
 *
 * It owns the worker/thread lifecycle for the single-cluster realign: create the
 * worker, move it to a fresh thread, stream logLine / clusterDone to the supplied
 * callbacks (delivered on the GUI thread via @p guiContext), and -- when the
 * worker emits finished -- invoke the finished callback (where the app applies the
 * result), tear the thread down, and only THEN call the queue's done().
 *
 * Because done() is the LAST thing to fire, the curation lane stays occupied for
 * the entire realign.  That is precisely the serialisation the merge race needed:
 * a renumber/matrix job queued after this one cannot start -- and therefore cannot
 * touch Data -- until the realignment has fully settled.
 *
 * Threading note: the worker emits its signals on the worker thread; every
 * callback here is connected with @p guiContext as the receiver, so Qt delivers
 * them on the GUI thread (queued).  The adapter therefore touches Data only from
 * the GUI thread.
 *
 * Decoupling note: the adapter deliberately knows nothing about KlustersApp.
 * The finished callback is the app's applyRealignResult body; single-cluster vs
 * batch auto-accept stays entirely on the app side.
 */
class RealignJob : public Job
{
public:
    using FinishedFn = std::function<void(bool ok, int nShifted, int nSwapped,
                                          QVector<float> meanBefore,
                                          QVector<float> meanAfter,
                                          QString backupBase, int nChan, int nSamp)>;
    using LogFn      = std::function<void(const QString& line, bool isError)>;
    using ClusterFn  = std::function<void(int pos, int total, int clusterId,
                                          bool ok, int nShifted)>;

    /** Single-cluster realignment.
     *  @param guiContext  a GUI-thread QObject (the app) used as the receiver for
     *                     every worker-signal callback, so they run on the GUI
     *                     thread.  Must outlive the job. */
    RealignJob(QObject* guiContext, KlustersDoc* doc, int clusterId, QString args,
               FinishedFn onFinished, LogFn onLog = {}, ClusterFn onCluster = {})
        : context(guiContext), doc(doc), clusterId(clusterId), args(std::move(args)),
          finishedFn(std::move(onFinished)), logFn(std::move(onLog)),
          clusterFn(std::move(onCluster)) {}

    /** Batch realignment: the worker loops realignSpikes over @p ids internally
     *  (one thread, clusterDone per cluster, finished once), mirroring setBatch. */
    void setBatch(const QList<int>& ids) { batchIds = ids; }

    /** Mirror RealignWorker::setVerbose — stream per-spike detail to stderr. */
    void setVerbose(bool v) { verbose = v; }

    QString name() const override
    {
        return batchIds.isEmpty()
            ? QStringLiteral("realign(cluster %1)").arg(clusterId)
            : QStringLiteral("realign(batch x%1)").arg(batchIds.size());
    }

    void run(std::function<void()> done) override
    {
        auto* worker = new RealignWorker(doc, clusterId, args);
        worker->setVerbose(verbose);
        if (!batchIds.isEmpty())
            worker->setBatch(batchIds);

        auto* thread = new QThread();
        worker->moveToThread(thread);

        // Progress / log output -> app callbacks, delivered on the GUI thread
        // (context is the receiver, worker lives on another thread => queued).
        if (logFn) {
            LogFn lf = logFn;
            QObject::connect(worker, &RealignWorker::logLine, context,
                             [lf](const QString& line, bool isError) { lf(line, isError); });
        }
        if (clusterFn) {
            ClusterFn cf = clusterFn;
            QObject::connect(worker, &RealignWorker::clusterDone, context,
                             [cf](int pos, int total, int cid, bool ok, int n) {
                                 cf(pos, total, cid, ok, n);
                             });
        }

        // Completion: apply/review the result, tear the thread down, then advance
        // the queue.  done() is intentionally the last statement.
        FinishedFn ff = finishedFn;
        QObject::connect(worker, &RealignWorker::finished, context,
            [ff, thread, done](bool ok, int nShifted, int nSwapped,
                               QVector<float> meanBefore, QVector<float> meanAfter,
                               QString backupBase, int nChan, int nSamp) {
                if (ff)
                    ff(ok, nShifted, nSwapped, meanBefore, meanAfter,
                       backupBase, nChan, nSamp);
                thread->quit();
                thread->wait(2000);
                thread->deleteLater();
                done();
            });

        // Standard worker/thread idiom:
        QObject::connect(thread, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(thread, &QThread::started,  worker, &RealignWorker::run);
        thread->start();
    }

private:
    QObject*    context;
    KlustersDoc* doc;
    int          clusterId;
    QString      args;
    FinishedFn   finishedFn;
    LogFn        logFn;
    ClusterFn    clusterFn;
    QList<int>   batchIds;   // non-empty => batch mode
    bool         verbose{false};
};

#endif // REALIGNJOB_H
