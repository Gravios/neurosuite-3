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

void KlustersJobPool::drain()
{
    pool()->waitForDone();
}
