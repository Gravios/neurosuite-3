#ifndef TEMPLATEMATRIXTHREAD_H
#define TEMPLATEMATRIXTHREAD_H

#include <QRunnable>
#include <QEvent>
#include <QList>
#include <memory>
#include <vector>
#include <cstdint>

class SpkReader;

#include "array.h"
#include "data.h"
#include "klustersjobpool.h"   // KlustersJobToken (shared with the view)

class TemplateMatrixView;

// ---------------------------------------------------------------------------
// Shared .spk reader used by all template/xcorr consumers.
// Reads one spike's waveform through the document's SpkReader (one shared
// pread descriptor — see spkreader.h) into a channel-major float buffer
// (out, length nChan*nSamp), de-interleaving the on-disk sample-major layout
// [sm*nChan+ch] -> [ch*nSamp+sm].  Spike files are int16 throughout the
// toolchain (the extractor writes int16 regardless of acquisition nBits), so
// the sample width is fixed here — no 2-vs-4-byte branch.  rawScratch is a
// caller-owned reusable int16 buffer (resized as needed) to avoid per-call
// allocation in hot loops.  Returns false on read failure, leaving out
// untouched.  Thread-safe: any number of callers may share one reader.
// ---------------------------------------------------------------------------
bool tmReadSpikeFloat(SpkReader& spk, long fileIdx0, int nChan, int nSamp,
                      std::vector<int16_t>& rawScratch,
                      std::vector<float>& out);

// ---------------------------------------------------------------------------
// Shared xcorr helper used by both thread classes.
// Peak normalised cross-correlation over lags [-maxShift, +maxShift],
// linear (zero-padded) indexing.  Both vectors channel-major, length nPts.
// meanSubtract=true switches the per-lag normalisation from cosine similarity
// to Pearson correlation (overlap-window mean removed from each waveform).
// ---------------------------------------------------------------------------
float tmNormXcorr(const std::vector<float>& a,
                  const std::vector<float>& b,
                  int maxShift,
                  bool meanSubtract = false);

// Raw (non-normalised) peak cross-correlation: max over lags of |Σ a_i b_j|.
// NOT divided by the waveform norms, so it scales with waveform energy; the
// caller is responsible for mapping the (unbounded) result onto a display scale.
float tmRawXcorr(const std::vector<float>& a,
                 const std::vector<float>& b,
                 int maxShift);

// Noise-disattenuated cosine: like tmNormXcorr (cosine) but the two energy
// terms have the noise energy of each MEAN waveform subtracted before the
// square root, Σa² − noiseA over the overlap window (likewise b).  noiseA/noiseB
// carry the per-point variance of the sample MEAN (within-cluster sample
// variance / N), so the denominator approximates the signal-only energy and a
// same-neuron pair reads ~1 regardless of spike count / energy.  Each corrected
// norm is floored at 10% of its raw value (caps the boost for near-noise means)
// and the result is capped at 1.0 for the [0,1] colour map.
float tmDisattenXcorr(const std::vector<float>& a,
                      const std::vector<float>& b,
                      const std::vector<float>& noiseA,
                      const std::vector<float>& noiseB,
                      int maxShift);

// Fast-AP-windowed cosine: tmNormXcorr restricted, per channel, to the
// [peak-8, peak+8) sample window (the spike proper), dropping the energy-scaling
// post-peak after-potential that depresses high-energy same-neuron cosines.
// `a`/`b` are channel-major [nChan*nSamp]; peak is the 0-based detection sample.
float tmFastWinXcorr(const std::vector<float>& a,
                     const std::vector<float>& b,
                     int nChan, int nSamp, int peak,
                     int maxShift);

// Profile (inter-channel) similarity: the average of three template
// comparisons that deliberately IGNORE overall amplitude, for separating
// co-located units whose difference lives OFF the dominant channel (measured
// on the reference session's curated fast-spiking pool: dominant-channel-only
// discrimination is chance there, while these carry the signal):
//   1. amplitude-profile cosine — the per-channel peak-to-trough vectors,
//      unit-normalised: WHERE the energy sits across the shank;
//   2. polarity-profile correlation — per-channel log(above/below baseline
//      excursion) vectors, Pearson across channels: the source/sink geometry
//      (baseline = each channel's first-4-sample mean);
//   3. per-channel-normalised shape correlation — each channel mean-removed
//      and unit-normalised, correlations averaged over channels where both
//      templates carry signal, so every channel counts equally.
// Zero-lag by design: all three compare STRUCTURE, not alignment — the
// detection centring that the other metrics compensate for with a shift
// search is shared by both templates here, and the components are either
// lag-invariant (1, 2) or tolerant at template SNR (3).  Channel count is
// derived from the vectors (a.size()/nSamp), so a channel-masked matrix
// build compares whatever channels survived the mask.  Returns the mean of
// the components that were computable (~[-1,1]); with none, 0.
float tmProfileSim(const std::vector<float>& a,
                   const std::vector<float>& b,
                   int nSamp);

// ---------------------------------------------------------------------------
// Main background job: reads all cluster waveforms, computes means, and
// builds the pairwise mean-vs-mean xcorr matrix.
// Does NOT compute per-spike xcorr — that is deferred to PairXcorrThread.
// Posts TemplateMatrixEvent (User+601) when done.
//
// Formerly one QThread per recompute; now a QRunnable on the shared worker
// pool (KlustersJobPool — worker-pool conversion, step 4a).  Creating it
// launches the request, as the old constructor's start() did; the pool owns
// and deletes it after run().  Cancellation runs through the view's matrix
// token: the job polls the request generation where it used to poll its
// per-thread stop flag.  The results — which used to be read out of the
// thread object after the event arrived — now travel INSIDE the event: the
// matrix by owning pointer (freed by the event if nobody takes it), the
// templates and spike indices moved in by value.  The OpenMP passes inside
// run() are unchanged: OMP teams are plain threads without event loops, so
// they never allocate descriptor-costing wakeup pipes.
// ---------------------------------------------------------------------------
class TemplateMatrixThread : public QRunnable {
public:
    friend class TemplateMatrixView;

    ~TemplateMatrixThread() override {}

    class TemplateMatrixEvent : public QEvent {
        friend class TemplateMatrixThread;
    public:
        /**Deletes the matrix when no handler took it (stale generation, or
        * the event was removed unseen by removePostedEvents).*/
        ~TemplateMatrixEvent() { delete scoresResult; }

        int generation() const { return eventGeneration; }
        /**Hands the matrix (and its ownership) to the caller; nullptr when
        * the compute was cancelled or degenerate, or it was already taken.*/
        Array<double>* takeScores() { Array<double>* s = scoresResult; scoresResult = nullptr; return s; }
        QList<int> getClusterList() const { return clusterListResult; }
        /**Mutable on purpose: the accepting handler moves these out.*/
        std::vector<std::vector<float>>& getMeanWav()   { return meanWavResult; }
        std::vector<std::vector<int>>&   getAllFileIdx(){ return allFileIdxResult; }
        /// The channel selection the run was launched for (empty = all channels).
        /// The view files the result in the matching cache slot.
        QList<int> getSelection() const { return selectionResult; }

    private:
        /**Takes over the job's results: the matrix pointer moves into the
        * event, the heavy vectors are moved (the job posts as its final act
        * and never touches them again).*/
        explicit TemplateMatrixEvent(TemplateMatrixThread& job)
            : QEvent(QEvent::Type(QEvent::User + 601)),
              eventGeneration(job.jobGeneration),
              scoresResult(job.scores),
              clusterListResult(job.clusterList),
              meanWavResult(std::move(job.meanWav)),
              allFileIdxResult(std::move(job.allFileIdx)),
              selectionResult(job.selection) { job.scores = nullptr; }

        int                             eventGeneration;
        Array<double>*                  scoresResult;
        QList<int>                      clusterListResult;
        std::vector<std::vector<float>> meanWavResult;
        std::vector<std::vector<int>>   allFileIdxResult;
        QList<int>                      selectionResult;
    };

    /**Executed by a pool worker; builds the matrix, posts the completion
    * event and retires the job.*/
    void run() override;

private:
    /**
     * @param sel  Channel selection (group-local indices, empty = all channels).
     *             Only the MATRIX is restricted to it: the meanWav templates stay
     *             full-width because TemplateMatrixView hands them to
     *             PairXcorrThread together with Data::nbOfChannels(), which
     *             would mismatch a compacted template.
     *
     * Creating the job launches the request on the shared pool (as the old
     * thread constructor's start() did).  Runs on the GUI thread only.
     */
    TemplateMatrixThread(TemplateMatrixView& v, Data& d,
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

    TemplateMatrixView&          view;
    Data&                        data;
    /**Shared cancellation/completion state owned by the view (matrix stream).*/
    std::shared_ptr<KlustersJobToken> token;
    /**The view's request generation this job was enqueued under.*/
    int                          jobGeneration = 0;
    /**The membership epoch captured at creation (epoch-snapshot step 1): pins
    * the epoch's tables, and the cluster-list build and spike-position
    * prefetch below read through it — immutable, so no mutex.*/
    std::shared_ptr<const Data::ClusteringSnapshot> snapshot;

    Array<double>*               scores;
    QList<int>                   clusterList;
    std::vector<std::vector<float>> meanWav;    // [clusterIdx] → channel-major mean
    std::vector<std::vector<int>>   allFileIdx; // [clusterIdx] → 0-based .spk indices
    QList<int>                      selection;  // empty = all channels
    QList<int>                      activeClusters; // empty = all clusters.  Declared
                                                // after selection so member init
                                                // order matches the initialiser list.
};

#endif // TEMPLATEMATRIXTHREAD_H
