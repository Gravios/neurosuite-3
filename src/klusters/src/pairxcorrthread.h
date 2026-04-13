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

    int sourceCluster() const { return m_sourceCluster; }
    int targetCluster() const { return m_targetCluster; }
    int generation()    const { return m_generation; }

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
          m_sourceCluster(sourceCluster), m_targetCluster(targetCluster),
          m_generation(generation),
          m_sourceFileIdx(sourceFileIdx), m_targetMean(targetMean),
          m_spkPath(spkPath), m_nChan(nChan), m_nSamp(nSamp),
          m_twoBytes(twoBytes), haveToStopProcessing(false)
    { start(); }

    TemplateMatrixView&       view;
    int                       m_sourceCluster;
    int                       m_targetCluster;
    int                       m_generation;
    std::vector<int>          m_sourceFileIdx;
    std::vector<float>        m_targetMean;
    QString                   m_spkPath;
    int                       m_nChan, m_nSamp;
    bool                      m_twoBytes;
    std::atomic_bool          haveToStopProcessing;

    std::vector<std::pair<int,float>> scores;
};

#endif // PAIRXCORRTHREAD_H
