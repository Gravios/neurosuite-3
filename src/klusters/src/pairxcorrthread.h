#ifndef PAIRXCORRTHREAD_H
#define PAIRXCORRTHREAD_H

#include <QRunnable>
#include <QEvent>
#include <memory>
#include <vector>
#include <utility>

#include "spkreader.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the view)

class TemplateMatrixView;

/**
 * Lightweight on-demand job: computes xcorr of every spike in the
 * source cluster against the target cluster's mean waveform.
 *
 * Launched when the user clicks a cell in the template matrix.
 * Posts a PairXcorrEvent (User+602) to the parent TemplateMatrixView.
 *
 * Results are cached by the view so repeated selections of the same
 * pair are instantaneous.
 *
 * Formerly one QThread per request; now a QRunnable on the shared worker
 * pool (KlustersJobPool — worker-pool conversion, step 4a).  Creating it
 * launches the request; the pool owns and deletes it after run().  The view
 * supersedes it by bumping its pair token's request generation (there is no
 * per-job pointer to keep anymore), and the scores travel inside the event,
 * moved out of the job as it posts.
 */
class PairXcorrThread : public QRunnable {
public:
    friend class TemplateMatrixView;

    ~PairXcorrThread() override {}

    class PairXcorrEvent : public QEvent {
        friend class PairXcorrThread;
    public:
        ~PairXcorrEvent() {}

        int generation()       const { return eventGeneration; }
        int getSourceCluster() const { return sourceCluster; }
        int getTargetCluster() const { return targetCluster; }
        /**Mutable on purpose: the accepting handler moves the scores out.
        * One entry per spike in the source cluster —
        * (0-based .spk fileIdx, xcorr score).*/
        std::vector<std::pair<int,float>>& getScores() { return scoresResult; }

    private:
        explicit PairXcorrEvent(PairXcorrThread& job)
            : QEvent(QEvent::Type(QEvent::User + 602)),
              eventGeneration(job.jobGeneration),
              sourceCluster(job.sourceCluster),
              targetCluster(job.targetCluster),
              scoresResult(std::move(job.scores)) {}

        int eventGeneration;
        int sourceCluster;
        int targetCluster;
        std::vector<std::pair<int,float>> scoresResult;
    };

    /**Executed by a pool worker; scores the spikes, posts the completion
    * event and retires the job.*/
    void run() override;

private:
    /**Creating the job launches the request on the shared pool (as the old
    * thread constructor's start() did).  Runs on the GUI thread only.*/
    PairXcorrThread(TemplateMatrixView& v,
                    int sourceCluster, int targetCluster,
                    const std::vector<int>&   sourceFileIdx,  // 0-based .spk indices
                    const std::vector<float>& targetMean,     // channel-major mean
                    const QString& spkPath,
                    int nChan, int nSamp, bool twoBytes,
                    const std::shared_ptr<KlustersJobToken>& viewToken);

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

    TemplateMatrixView&       view;
    int                       sourceCluster;
    int                       targetCluster;
    std::vector<int>          sourceFileIdx;
    std::vector<float>        targetMean;
    QString                   spkPath;
    /**Positioned-read access for this job (see spkreader.h).*/
    SpkReader spkReaderOwn;
    int                       nChan, nSamp;
    bool                      twoBytes;
    /**Shared cancellation/completion state owned by the view (pair stream).*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int                       jobGeneration = 0;

    std::vector<std::pair<int,float>> scores;
};

#endif // PAIRXCORRTHREAD_H
