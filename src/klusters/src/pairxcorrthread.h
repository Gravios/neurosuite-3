#ifndef PAIRXCORRTHREAD_H
#define PAIRXCORRTHREAD_H

#include <QThread>
#include <QEvent>
#include <atomic>
#include <vector>
#include <utility>

class TemplateMatrixView;

/**
 * Lightweight on-demand thread: computes xcorr of every spike in the
 * source cluster against the target cluster's mean waveform.
 *
 * Launched when the user clicks a cell in the template matrix.
 * Posts a PairXcorrEvent (User+602) to the parent TemplateMatrixView.
 *
 * Results are cached by the view so repeated selections of the same
 * pair are instantaneous.
 */
class PairXcorrThread : public QThread {
public:
    friend class TemplateMatrixView;

    ~PairXcorrThread() {}

    void stopProcessing() {
        haveToStopProcessing.store(true, std::memory_order_release);
    }

    int getSourceCluster() const { return sourceCluster; }
    int getTargetCluster() const { return targetCluster; }
    int getGeneration()    const { return generation; }

    // Result: one entry per spike in source cluster — (0-based .spk fileIdx, xcorr score)
    const std::vector<std::pair<int,float>>& getScores() const { return scores; }

    class PairXcorrEvent : public QEvent {
        friend class PairXcorrThread;
    public:
        PairXcorrThread* parentThread() { return &thread; }
        ~PairXcorrEvent() {}
    private:
        explicit PairXcorrEvent(PairXcorrThread& t)
            : QEvent(QEvent::Type(QEvent::User + 602)), thread(t) {}
        PairXcorrThread& thread;
    };

protected:
    void run() override;

private:
    PairXcorrThread(TemplateMatrixView& v,
                    int sourceCluster, int targetCluster,
                    const std::vector<int>&   sourceFileIdx,  // 0-based .spk indices
                    const std::vector<float>& targetMean,     // channel-major mean
                    const QString& spkPath,
                    int nChan, int nSamp, bool twoBytes,
                    int generation)
        : view(v),
          sourceCluster(sourceCluster), targetCluster(targetCluster),
          generation(generation),
          sourceFileIdx(sourceFileIdx), targetMean(targetMean),
          spkPath(spkPath), nChan(nChan), nSamp(nSamp),
          twoBytes(twoBytes), haveToStopProcessing(false)
    { start(); }

    TemplateMatrixView&       view;
    int                       sourceCluster;
    int                       targetCluster;
    int                       generation;
    std::vector<int>          sourceFileIdx;
    std::vector<float>        targetMean;
    QString                   spkPath;
    int                       nChan, nSamp;
    bool                      twoBytes;
    std::atomic_bool          haveToStopProcessing;

    std::vector<std::pair<int,float>> scores;
};

#endif // PAIRXCORRTHREAD_H
