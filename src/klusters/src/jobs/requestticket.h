// requestticket.h — aggregate-completion ticket for one view request
// (subscribe-don't-wait: epoch-snapshot step 3a for waveforms, step 4 for
// correlograms).
//
// A job used to sleep(1)-poll Data whenever another job was already
// computing something it needed — a cluster's waveforms, a pair's
// correlogram — occupying a pool worker for the wait.  Now the job REGISTERS
// a waiter with the computation's store and retires; whoever finishes the
// computation completes the waiter.  The ticket is the aggregation point: it
// carries the request's completion count — one share for the job's own sweep
// plus one per parked waiter — and posts the request's single completion
// event (through the closure the job installed, which carries the view-death
// fence) when the last share completes.  Shared ownership: the job holds it
// through run(), the store's waiter registry holds it while parked.
//
// Failure semantics are the requester's to define: for a single-cluster
// waveform request, a failed share (cluster gone) marks the ticket failed
// and the closure posts the no-data event instead of the completion event;
// multi-cluster waveform requests and correlogram requests treat failures
// as skips, and the completion event fires regardless.
#ifndef REQUESTTICKET_H
#define REQUESTTICKET_H

#include <atomic>
#include <functional>
#include <memory>

struct RequestTicket {
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

#endif // REQUESTTICKET_H
