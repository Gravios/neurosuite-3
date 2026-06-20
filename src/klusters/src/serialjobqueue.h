#ifndef SERIALJOBQUEUE_H
#define SERIALJOBQUEUE_H

#include <QObject>
#include <QQueue>
#include <QString>
#include <QTimer>
#include <functional>

/**
 * Job -- one unit of curation or compute work.
 *
 * run() performs the work and invokes @p done exactly once when it has
 * finished.  Completion may be:
 *   - synchronous  -- call done() before run() returns (e.g. a renumber or a
 *     matrix-recompute trigger on the GUI thread), or
 *   - asynchronous -- call done() later, typically from a background worker's
 *     finished signal (as the realign worker does).
 * Either way the queue regards the job as active until done() fires, which is
 * exactly what lets fast main-thread work and slow background work share one
 * serialised lane.
 *
 * Contract: run() MUST cause done() to be called exactly once, even on failure
 * or cancellation, otherwise the queue stalls.
 */
class Job
{
public:
    virtual ~Job() = default;
    virtual void run(std::function<void()> done) = 0;
    /// Human-readable label, surfaced in the queue's signals (status bar, logs).
    virtual QString name() const { return QStringLiteral("job"); }
};

/**
 * LambdaJob -- adapts a synchronous callable to Job: runs it, then completes
 * immediately.  Use this to drop existing main-thread operations (renumber,
 * slotUpdateErrorMatrix, a group/merge call) into the queue without writing a
 * dedicated Job subclass.
 */
class LambdaJob : public Job
{
public:
    explicit LambdaJob(std::function<void()> fn,
                       QString label = QStringLiteral("lambda"))
        : fn(std::move(fn)), label(std::move(label)) {}

    void run(std::function<void()> done) override
    {
        if (fn) fn();
        done();
    }
    QString name() const override { return label; }

private:
    std::function<void()> fn;
    QString               label;
};

/**
 * SerialJobQueue -- a serialised job executor (the Active Object pattern).
 *
 * Jobs enqueued here run strictly one at a time, in FIFO order: the next job is
 * started only after the current one signals completion.  This turns "only one
 * job touches Data at a time" into a structural invariant of the type, instead
 * of an invariant every curation path must re-establish by hand with ad-hoc
 * busy flags (realignRunning, autoPostEditPending, processWidget->isRunning(),
 * ...).
 *
 * Threading: the queue lives on and is driven from the GUI thread, so run() and
 * the done callback are always invoked there.  Because only one job is ever
 * active, jobs never need to lock Data against each other -- serialisation IS
 * the exclusivity.  An asynchronous job just forwards its done callback to a
 * worker's finished signal, which Qt delivers back on the GUI thread.
 *
 * Re-entrancy: after a completion the next job is started from the event loop
 * (singleShot(0)), so a chain of synchronous jobs cannot recurse without bound
 * and the UI gets a turn to repaint between jobs.
 *
 * Example -- the merge -> auto-align -> renumber -> matrix sequence that
 * patch 0006 had to serialise by hand becomes four ordered jobs:
 *
 *     queue.enqueue(new LambdaJob([=]{ doc->groupClusters(...); }, "merge"));
 *     queue.enqueue(new RealignJob(doc, clusterId));        // async
 *     queue.enqueue(new LambdaJob([=]{ doc->renumberClusters(); }, "renumber"));
 *     queue.enqueue(new LambdaJob([=]{ slotUpdateErrorMatrix(); }, "matrix"));
 *
 * The realign job (slow, background) completes before renumber starts, so the
 * race that motivated the old autoPostMergePending hand-off cannot occur by
 * construction.  The post-merge step now runs exactly this way (a LambdaJob
 * enqueued behind the RealignJob), and the hand-off flag has been retired.
 */
class SerialJobQueue : public QObject
{
    Q_OBJECT
public:
    explicit SerialJobQueue(QObject* parent = nullptr) : QObject(parent) {}
    ~SerialJobQueue() override { qDeleteAll(pending); delete active; }

    /** Enqueue @p job (ownership transferred).  Starts it if the lane is idle. */
    void enqueue(Job* job)
    {
        if (!job) return;
        pending.enqueue(job);
        emit queueLengthChanged(totalCount());
        if (!active) scheduleStart();
    }

    /** True while any job is active OR waiting -- i.e. the lane is occupied. */
    bool isBusy() const { return active != nullptr || !pending.isEmpty(); }

    /** Jobs waiting behind the active one. */
    int pendingCount() const { return pending.size(); }

    /** Active (0 or 1) plus pending. */
    int totalCount() const { return pending.size() + (active ? 1 : 0); }

    /** Drop all not-yet-started jobs; the active job is left to finish. */
    void clearPending()
    {
        if (pending.isEmpty()) return;
        qDeleteAll(pending);
        pending.clear();
        emit queueLengthChanged(totalCount());
    }

signals:
    void jobStarted(const QString& name);
    void jobFinished(const QString& name);
    void queueLengthChanged(int total);
    void idle();   ///< the last job finished and nothing is pending

private:
    void scheduleStart()
    {
        // Defer to the event loop so consecutive synchronous jobs don't recurse
        // and the UI can repaint between jobs.
        QTimer::singleShot(0, this, [this]{ startNext(); });
    }

    void startNext()
    {
        if (active || pending.isEmpty()) return;
        active = pending.dequeue();
        emit jobStarted(active->name());
        Job* started = active;                  // capture for the completion guard
        started->run([this, started]{ onJobDone(started); });
    }

    void onJobDone(Job* job)
    {
        if (job != active) return;              // ignore a stray / double done()
        const QString finished = active->name();
        delete active;
        active = nullptr;
        emit jobFinished(finished);
        if (pending.isEmpty()) emit idle();
        else                   scheduleStart();
    }

    QQueue<Job*> pending;
    Job*         active = nullptr;
};

#endif // SERIALJOBQUEUE_H
