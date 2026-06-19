#ifndef RESIDUALMATRIXTHREAD_H
#define RESIDUALMATRIXTHREAD_H

#include <QThread>
#include <QEvent>
#include <QList>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstdint>

#include "array.h"
#include "data.h"

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
// ---------------------------------------------------------------------------
class ResidualMatrixThread : public QThread {
public:
    friend class ResidualMatrixView;

    ~ResidualMatrixThread() {}

    void stopProcessing() {
        haveToStopProcessing.store(true, std::memory_order_release);
    }
    int getGeneration() const { return generation; }

    // Results exposed to ResidualMatrixView after the thread finishes.
    Array<double>* getScores()      const { return scores; }
    QList<int>     getClusterList() const { return clusterList; }

    class ResidualMatrixEvent : public QEvent {
        friend class ResidualMatrixThread;
    public:
        ResidualMatrixThread* parentThread() { return &thread; }
        ~ResidualMatrixEvent() {}
    private:
        explicit ResidualMatrixEvent(ResidualMatrixThread& t)
            : QEvent(QEvent::Type(QEvent::User + 603)), thread(t) {}
        ResidualMatrixThread& thread;
    };

protected:
    void run() override;

private:
    ResidualMatrixThread(ResidualMatrixView& v, Data& d, int gen)
        : view(v), data(d), generation(gen),
          haveToStopProcessing(false), scores(nullptr) { start(); }

    ResidualMatrixView&          view;
    Data&                        data;
    int                          generation;
    std::atomic_bool             haveToStopProcessing;

    Array<double>*               scores;       // [N x N], 1-based, asymmetric
    QList<int>                   clusterList;   // matrix row/col -> cluster id
    std::vector<std::vector<int>> allFileIdx;   // [clusterIdx] -> 0-based .spk rows
};

#endif // RESIDUALMATRIXTHREAD_H
