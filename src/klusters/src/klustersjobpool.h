// klustersjobpool.h — the shared worker pools for per-request compute jobs.
//
// Two process-wide QThreadPools sized to the machine replace the
// thread-per-request pattern (worker-pool conversion, step 2; the interactive
// lane is epoch-snapshot step 9).  Pool workers are plain threads that
// execute QRunnables and run no event loop, so they never allocate the
// per-thread event-dispatcher wakeup pipe (two file descriptors each) that
// made thread count — and with it descriptor use — scale with cluster count.
//
// Why two lanes and why both are FULL-SIZE: queue priorities order who runs
// NEXT but never preempt a running job, so a machine's worth of minutes-long
// matrix computes used to make a fresh waveform click wait for a worker to
// retire.  A static split (N-2 batch / 2 interactive) would fix that by
// capping the common case instead — an idle-batch session would fill
// waveform panes on 2 workers rather than all of them.  So each lane is
// sized to the machine: alone, either lane uses every core; when they
// overlap, the OS timeslices the ~2x oversubscription, which degrades the
// batch computes for the seconds an interactive burst lasts — exactly the
// yield the priorities could not provide.  Workers are plain threads with no
// event loop, so the second lane costs no descriptors.
//
// Lifetime: created on first use, never destroyed (a static-destruction-order
// race between pool workers and other globals is worse than one idle pool at
// exit).  Jobs reference the document's Data, so KlustersDoc drains the pool
// before deleting Data — the one choke point where job lifetime and document
// lifetime meet.
#ifndef KLUSTERSJOBPOOL_H
#define KLUSTERSJOBPOOL_H

#include <QMutex>
#include <atomic>

class QRunnable;
class QThreadPool;

namespace KlustersJobPool {

/**Job classes for start(job, priority).  Interactive jobs run on their own
* lane (interactivePool()), so their latency is independent of batch
* occupancy; Batch and Background share the bulk lane, where the higher
* value runs first when a worker frees up and equal priorities keep FIFO
* order.
*
* Interactive: work the user is actively waiting on to see something —
* waveform/correlogram loads for the shown clusters, a clicked matrix cell's
* per-spike scores, a drift-slider step.
* Batch: the full matrix computes — O(clusters^2) scans that can run for
* minutes at large cluster counts.
* Background: work nobody is watching — the error-matrix cache warmer, the
* merge-recommendation scan.*/
enum Priority {
    BackgroundPriority  = -1,
    BatchPriority       = 0,
    InteractivePriority = 1
};

/**The bulk lane (Batch + Background).
* maxThreadCount = qMax(4, QThread::idealThreadCount()).*/
QThreadPool* pool();

/**The interactive lane — same sizing as the bulk lane (see the file
* preamble for why it is not a small reserved slice).*/
QThreadPool* interactivePool();

/**Enqueue @p job on the lane its class belongs to: InteractivePriority on
* interactivePool() (FIFO — interactive jobs were always equal-priority
* peers), everything else on pool() with the class as the queue priority.
* The one enqueue spelling every thread uses.*/
void start(QRunnable* job, Priority priority);

/**Blocks until every queued and running job of BOTH lanes has finished.
* Call before destroying state that enqueued jobs reference (KlustersDoc
* calls it before deleting Data).  Jobs are expected to have been superseded
* already — their views bumped the request generation, making them
* early-out — so this normally returns almost at once.*/
void drain();

}

/**Cancellation and completion state shared between a view and the jobs it
 * enqueues on the pool.  The view owns it through a shared_ptr and hands each
 * job a copy, so the state outlives whichever side dies first.
 */
struct KlustersJobToken {
    /**Fences completion posts against view destruction: the view's destructor
    * sets viewDead under this mutex, and a job posts its completion event only
    * while it is false (under the same mutex).*/
    QMutex postMutex;
    bool viewDead = false;
    /**Request generation.  The view bumps it to supersede every in-flight
    * job at once: a job whose captured generation no longer matches stops at
    * its next cancellation check, and the view's customEvent() drops its
    * completion event.  This replaces both the per-thread stop flags and the
    * threadsToBeKill ownership lists.*/
    std::atomic_int generation{0};
    /**Number of jobs enqueued and not yet retired.  A job decrements it as
    * the very last act of run(), so active == 0 means no job of this view is
    * inside a Data call anymore — the synchronous quiesce contract that the
    * views' stop methods offer their callers, and what isThreadsRunning()
    * reports.*/
    std::atomic_int active{0};
};

#endif // KLUSTERSJOBPOOL_H
