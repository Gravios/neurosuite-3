// klustersjobpool.h — the shared worker pool for per-request compute jobs.
//
// One process-wide QThreadPool sized to the machine replaces the
// thread-per-request pattern (worker-pool conversion, step 2).  Pool workers
// are plain threads that execute QRunnables and run no event loop, so they
// never allocate the per-thread event-dispatcher wakeup pipe (two file
// descriptors each) that made thread count — and with it descriptor use —
// scale with cluster count.
//
// Lifetime: created on first use, never destroyed (a static-destruction-order
// race between pool workers and other globals is worse than one idle pool at
// exit).  Jobs reference the document's Data, so KlustersDoc drains the pool
// before deleting Data — the one choke point where job lifetime and document
// lifetime meet.
#ifndef KLUSTERSJOBPOOL_H
#define KLUSTERSJOBPOOL_H

class QThreadPool;

namespace KlustersJobPool {

/**The shared pool.  maxThreadCount = qMax(4, QThread::idealThreadCount()).*/
QThreadPool* pool();

/**Blocks until every queued and running job has finished.  Call before
* destroying state that enqueued jobs reference (KlustersDoc calls it before
* deleting Data).  Jobs are expected to have been superseded already — their
* views bumped the request generation, making them early-out — so this
* normally returns almost at once.*/
void drain();

}

#endif // KLUSTERSJOBPOOL_H
