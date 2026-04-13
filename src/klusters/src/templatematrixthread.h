#ifndef TEMPLATEMATRIXTHREAD_H
#define TEMPLATEMATRIXTHREAD_H

#include <QThread>
#include <QEvent>
#include <QList>
#include <atomic>
#include <vector>

#include "array.h"
#include "data.h"

class TemplateMatrixView;

// ---------------------------------------------------------------------------
// Shared xcorr helper used by both thread classes.
// Peak normalised cross-correlation over lags [-maxShift, +maxShift],
// linear (zero-padded) indexing.  Both vectors channel-major, length nPts.
// ---------------------------------------------------------------------------
float tmNormXcorr(const std::vector<float>& a,
                  const std::vector<float>& b,
                  int maxShift);

// ---------------------------------------------------------------------------
// Main background thread: reads all cluster waveforms, computes means, and
// builds the pairwise mean-vs-mean xcorr matrix.
// Does NOT compute per-spike xcorr — that is deferred to PairXcorrThread.
// Posts TemplateMatrixEvent (User+601) when done.
// ---------------------------------------------------------------------------
class TemplateMatrixThread : public QThread {
public:
    friend class TemplateMatrixView;

    ~TemplateMatrixThread() {}

    void stopProcessing() {
        haveToStopProcessing.store(true, std::memory_order_release);
    }
    int generation() const { return m_generation; }

    // Results exposed to TemplateMatrixView after thread finishes
    Array<double>*                      getScores()      const { return scores; }
    QList<int>                          getClusterList() const { return clusterList; }
    const std::vector<std::vector<float>>& getMeanWav()  const { return meanWav; }
    const std::vector<std::vector<int>>&   getAllFileIdx()const { return allFileIdx; }

    class TemplateMatrixEvent : public QEvent {
        friend class TemplateMatrixThread;
    public:
        TemplateMatrixThread* parentThread() { return &thread; }
        ~TemplateMatrixEvent() {}
    private:
        explicit TemplateMatrixEvent(TemplateMatrixThread& t)
            : QEvent(QEvent::Type(QEvent::User + 601)), thread(t) {}
        TemplateMatrixThread& thread;
    };

protected:
    void run() override;

private:
    TemplateMatrixThread(TemplateMatrixView& v, Data& d, int gen)
        : view(v), data(d), m_generation(gen),
          haveToStopProcessing(false), scores(nullptr) { start(); }

    TemplateMatrixView&          view;
    Data&                        data;
    int                          m_generation;
    std::atomic_bool             haveToStopProcessing;

    Array<double>*               scores;
    QList<int>                   clusterList;
    std::vector<std::vector<float>> meanWav;    // [clusterIdx] → channel-major mean
    std::vector<std::vector<int>>   allFileIdx; // [clusterIdx] → 0-based .spk indices
};

#endif // TEMPLATEMATRIXTHREAD_H
