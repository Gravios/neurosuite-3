#ifndef RESIDUALMATRIXTHREAD_H
#define RESIDUALMATRIXTHREAD_H

#include <QRunnable>
#include <QEvent>
#include <QList>
#include <memory>
#include <vector>
#include <cstdint>

#include "array.h"
#include "data.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the view)

class ResidualMatrixView;

// ---------------------------------------------------------------------------
// Background thread: builds the ASYMMETRIC mean-waveform residual matrix.
//
// For every cluster it reads all member waveforms from the .spk file and
// accumulates, per waveform point p (channel-major, length nChan*nSamp), both
// the running sum and sum-of-squares, giving a per-cluster mean waveform
// mean_c[p] and per-point within-cluster variance var_c[p].  No per-spike
// waveform is retained — a single streaming pass per cluster, exactly the
// shape of TemplateMatrixThread's mean pass extended with the second moment.
//
// The matrix cell (row A, col B) is the variance of cluster A's spikes taken
// about cluster B's template, i.e. the mean-squared residual of modelling A's
// waveforms with B's mean:
//
//     M(A,B) = mean_p  E_{s in A}[ (x_s[p] - mean_B[p])^2 ]
//            = mean_p  ( var_A[p] + (mean_A[p] - mean_B[p])^2 )
//
// (the cross term vanishes because E_{s in A}[x_s - mean_A] = 0, so only the
// within-A variance and the squared template gap survive — a bias/variance
// split computable from means + variances alone, no second .spk pass).
//
// It is ASYMMETRIC: the squared-template-gap term is symmetric, but the
// leading var_A term is the reference cluster's own variance, so M(A,B) uses
// var_A while M(B,A) uses var_B.  The diagonal M(A,A) = mean_p var_A[p] is the
// within-cluster noise floor each row is measured against.
//
// The raw values are in (int16 amplitude)^2 units and unbounded; the view
// normalises for colour but keeps the raw matrix (matrixData()) so the
// spike-count-gated reorder can seriate on real residual distances.
//
// Posts ResidualMatrixEvent (User+603) when done.
//
// Formerly one QThread per recompute; now a QRunnable on the shared worker
// pool (KlustersJobPool — worker-pool conversion, step 4b).  Creating it
// launches the request; the pool owns and deletes it after run().  The view
// supersedes it by bumping its token's request generation, and the results
// travel inside the completion event (the matrix freed by the event when no
// handler takes it).
// ---------------------------------------------------------------------------
class ResidualMatrixThread : public QRunnable {
public:
    friend class ResidualMatrixView;

    ~ResidualMatrixThread() override {}

    class ResidualMatrixEvent : public QEvent {
        friend class ResidualMatrixThread;
    public:
        /**Deletes the matrix when no handler took it.*/
        ~ResidualMatrixEvent() { delete scoresResult; }

        int generation() const { return eventGeneration; }
        /**Hands the matrix (and its ownership) to the caller; nullptr when
        * the compute was cancelled or degenerate, or it was already taken.*/
        Array<double>* takeScores() { Array<double>* s = scoresResult; scoresResult = nullptr; return s; }
        QList<int> getClusterList() const { return clusterListResult; }
        /// The channel selection the run was launched for (empty = all channels).
        /// The view files the result in the matching cache slot.
        QList<int> getSelection() const { return selectionResult; }
    private:
        explicit ResidualMatrixEvent(ResidualMatrixThread& job)
            : QEvent(QEvent::Type(QEvent::User + 603)),
              eventGeneration(job.jobGeneration),
              scoresResult(job.scores),
              clusterListResult(job.clusterList),
              selectionResult(job.selection) { job.scores = nullptr; }

        int            eventGeneration;
        Array<double>* scoresResult;
        QList<int>     clusterListResult;
        QList<int>     selectionResult;
    };

    /**Executed by a pool worker; builds the matrix, posts the completion
    * event and retires the job.*/
    void run() override;

private:
    /**
     * @param sel  Channel selection (group-local indices, empty = all channels).
     *             The means and variances are restricted to it before the matrix
     *             is built.
     *
     * Creating the job launches the request on the shared pool (as the old
     * thread constructor's start() did).  Runs on the GUI thread only.
     */
    ResidualMatrixThread(ResidualMatrixView& v, Data& d,
                         const std::shared_ptr<KlustersJobToken>& viewToken,
                         QList<int> sel = QList<int>(),
                         QList<int> clusterScope = QList<int>());

    /**True once the view has superseded this job's request generation: the
    * job stops at its next check, exactly where the per-thread stop flag
    * used to be read.*/
    bool cancelled() const {
        return token->generation.load(std::memory_order_acquire) != jobGeneration;
    }

    /**Posts @p event to the view, unless the view is being destroyed
    * (fenced by the token's postMutex/viewDead).*/
    void post(QEvent* event);

    /**The old run() body; split out so run() can retire the job on every path.*/
    void process();

    ResidualMatrixView&          view;
    Data&                        data;
    /**Shared cancellation/completion state owned by the view.*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int                          jobGeneration = 0;

    Array<double>*               scores;       // [N x N], 1-based, asymmetric
    QList<int>                   clusterList;   // matrix row/col -> cluster id
    std::vector<std::vector<int>> allFileIdx;   // [clusterIdx] -> 0-based .spk rows
    QList<int>                   selection;    // empty = all channels
    QList<int>                   activeClusters;  // empty = all clusters.  After selection so
                                     // member init order matches the ctor list.
};

#endif // RESIDUALMATRIXTHREAD_H
