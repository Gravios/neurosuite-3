// klustersjobpool.cpp — see klustersjobpool.h for the design rationale.
#include "klustersjobpool.h"

#include <QThread>
#include <QThreadPool>

QThreadPool* KlustersJobPool::pool()
{
    static QThreadPool* p = []{
        auto* pool = new QThreadPool;
        // idealThreadCount keeps every core busy; the floor keeps small
        // machines from serialising all loaders behind one or two workers.
        // Note jobs that sleep-poll a Data status (waiting out a concurrent
        // computation of the same cluster) occupy a worker while they wait,
        // which the floor also cushions.
        pool->setMaxThreadCount(qMax(4, QThread::idealThreadCount()));
        return pool;
    }();
    return p;
}

QThreadPool* KlustersJobPool::interactivePool()
{
    static QThreadPool* p = []{
        auto* pool = new QThreadPool;
        // Full-size, deliberately: alone, the interactive lane fills waveform
        // and correlogram panes on every core exactly as the single pool did;
        // only an overlap with running batch computes oversubscribes, and
        // there the OS timeslicing IS the design — batch yields for the
        // seconds the burst lasts (see the header preamble).
        pool->setMaxThreadCount(qMax(4, QThread::idealThreadCount()));
        return pool;
    }();
    return p;
}

void KlustersJobPool::start(QRunnable* job, Priority priority)
{
    if (priority == InteractivePriority)
        interactivePool()->start(job);
    else
        pool()->start(job, priority);
}

void KlustersJobPool::drain()
{
    // One pass per lane retires everything: every enqueue happens on the GUI
    // thread, so no draining job can put a new job on either lane.
    pool()->waitForDone();
    interactivePool()->waitForDone();
}
