// waveformticket.h — aggregate-completion ticket for one waveform request
// (epoch-snapshot step 3a: subscribe-don't-wait).
//
// A waveform job used to sleep(1)-poll Data whenever another job was already
// computing the same cluster, occupying a pool worker for the wait.  Now the
// job REGISTERS a waiter with Data and retires; whoever finishes the
// computation completes the waiter.  The ticket is the aggregation point: it
// carries the request's completion count — one share for the job's own sweep
// plus one per parked waiter — and posts the request's single completion
// event (through the closure the job installed, which carries the view-death
// fence) when the last share completes.  Shared ownership: the job holds it
// through run(), Data's waiter registry holds it while parked.
//
// Failure semantics mirror the old code: for a single-cluster request, a
// failed share (cluster gone) marks the ticket failed and the closure posts
// the no-data event instead of the completion event; for a multi-cluster
// request failures are skips, and the completion event fires regardless.
#ifndef WAVEFORMTICKET_H
#define WAVEFORMTICKET_H

#include <atomic>
#include <functional>
#include <memory>

struct WaveformRequestTicket {
    /**Outstanding completion shares: starts at 1 (the requesting job's own
    * sweep); each parked waiter adds one under Data's mutex.*/
    std::atomic_int  remaining{1};
    /**Set when a share failed AND the request's semantics make that fatal
    * (single-cluster requests).*/
    std::atomic_bool failed{false};
    std::atomic_bool posted{false};
    /**Posts the request's completion (or no-data) event to the view, under
    * the view token's post fence.  Installed by the job before any share
    * can complete; runs on whichever thread completes the last share.*/
    std::function<void(bool failed)> post;

    void completeOne() {
        if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            bool expected = false;
            if (posted.compare_exchange_strong(expected, true) && post)
                post(failed.load(std::memory_order_acquire));
        }
    }
};

#endif // WAVEFORMTICKET_H
