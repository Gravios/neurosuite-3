#ifndef TEMPLATEMATRIXTHREAD_H
#define TEMPLATEMATRIXTHREAD_H

#include <QThread>
#include <QEvent>
#include <QList>
#include <atomic>
#include <vector>
#include <cstdio>
#include <cstdint>

#include "array.h"
#include "data.h"

class TemplateMatrixView;

// ---------------------------------------------------------------------------
// Shared .spk reader used by all template/xcorr consumers.
// Reads one spike's waveform from an already-open .spk handle into a
// channel-major float buffer (out, length nChan*nSamp), de-interleaving the
// on-disk sample-major layout [sm*nChan+ch] -> [ch*nSamp+sm].  Spike files are
// int16 throughout the toolchain (the extractor writes int16 regardless of
// acquisition nBits), so the sample width is fixed here — no 2-vs-4-byte branch.
// rawScratch is a caller-owned reusable int16 buffer (resized as needed) to
// avoid per-call allocation in hot loops.  Returns false on seek/read failure,
// leaving out untouched.
// ---------------------------------------------------------------------------
bool tmReadSpikeFloat(FILE* spk, long fileIdx0, int nChan, int nSamp,
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
