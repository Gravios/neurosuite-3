#include "spectralengine.h"
#include "spectralfft.h"
#include "spectralgpu.h"
#include "decimate.h"

#include <algorithm>
#include <cmath>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace neuroscope {
namespace spectral {

namespace {
// Route the spectrogram to the GPU when that backend is selected and usable;
// otherwise (and on any GPU decline) compute on the CPU.
void dispatchSpectrogram(const double* sig, int n, const DpssTapers& tapers,
                         double fs, int nfft, int step, TaperWeighting w,
                         SpectralBackend backend,
                         std::vector<std::vector<double>>& out)
{
    if (backend == SpectralBackend::Cuda && gpu::available()) {
        if (gpu::multitaperSpectrogram(sig, n, tapers, fs, nfft, step, w, out))
            return;
    }
    multitaperSpectrogram(sig, n, tapers, fs, nfft, step, w, out);
}
} // namespace

bool SpectralParams::sameAs(const SpectralParams& o) const
{
    return mode == o.mode
        && samplingRate == o.samplingRate
        && windowSamples == o.windowSamples
        && nfft == o.nfft
        && stepSamples == o.stepSamples
        && nw == o.nw
        && nTapers == o.nTapers
        && weighting == o.weighting
        && whiten == o.whiten
        && freqLow == o.freqLow
        && freqHigh == o.freqHigh
        && singleChannel == o.singleChannel
        && backend == o.backend
        && decimate == o.decimate;
}

const DpssTapers& SpectralEngine::tapersFor(int N, double nw, int K)
{
    if (!tapers_.valid() || N != cachedN_ || nw != cachedNW_ || K != cachedK_) {
        tapers_ = computeDpss(N, nw, K);
        cachedN_ = N; cachedNW_ = nw; cachedK_ = K;
    }
    return tapers_;
}

namespace {

// First/last one-sided bin indices covered by [fLow, fHigh] for a given nfft.
void bandBins(double fLow, double fHigh, int nfft, double fs, int& lo, int& hi)
{
    const int half = nfft / 2;
    if (fHigh <= 0.0 || fHigh > fs / 2.0) fHigh = fs / 2.0;
    if (fLow < 0.0) fLow = 0.0;
    lo = static_cast<int>(std::ceil(fLow  * nfft / fs));
    hi = static_cast<int>(std::floor(fHigh * nfft / fs));
    lo = std::max(0, std::min(lo, half));
    hi = std::max(lo, std::min(hi, half));
}

} // namespace

const SpectralImage& SpectralEngine::compute(const double* sampleMajor,
                                             int nSamples, int nChannels,
                                             const std::vector<int>& channels,
                                             const SpectralParams& params,
                                             std::uint64_t windowId)
{
    // Cache hit: same window and parameters -> return the previous image.
    if (hasCache_ && cacheWindowId_ == windowId && cacheParams_.sameAs(params)
        && cache_.valid()) {
        return cache_;
    }

    SpectralImage img;
    img.mode = params.mode;

    const int nch = static_cast<int>(channels.size());
    const int N = params.windowSamples;
    const double fs = params.samplingRate;
    if (nch < 1 || nSamples < N || N < 2 || fs <= 0.0 || params.stepSamples <= 0) {
        cache_ = img; cacheParams_ = params; cacheWindowId_ = windowId; hasCache_ = true;
        return cache_;
    }

    // Gather the requested channels into a contiguous channel-major buffer
    // (each channel's samples consecutive) for whitening and the estimator.
    std::vector<double> block(static_cast<std::size_t>(nch) * nSamples);
    for (int r = 0; r < nch; ++r) {
        const int c = channels[r];
        double* dst = block.data() + static_cast<std::size_t>(r) * nSamples;
        for (int s = 0; s < nSamples; ++s)
            dst[s] = sampleMajor[static_cast<std::size_t>(s) * nChannels + c];
    }

    if (params.whiten && nch > 1)
        commonWhiten(block.data(), nch, nSamples, 1e-6);

    // Optional anti-aliased decimation: when a high-frequency edge well below
    // Nyquist is selected, low-pass and downsample each channel by M so the
    // transform shrinks while the in-band resolution is preserved. The effective
    // sampling rate, window, step and sample count all scale by M, which keeps
    // the bin spacing (effFs/effNfft) and the real time axis unchanged.
    double effFs = fs;
    int    effN = N;
    int    effStep = params.stepSamples;
    int    effNSamples = nSamples;
    int    decM = 1;
    if (params.decimate) {
        decM = decimationFactor(fs, params.freqHigh, 1.25, 64, nSamples, 64);
        while (decM > 1 && (N / decM < 8 || params.stepSamples / decM < 1)) decM >>= 1;
    }
    if (decM > 1) {
        const int nDec = nSamples / decM;
        std::vector<double> decBlock(static_cast<std::size_t>(nch) * nDec);
        std::vector<double> chDec;
        for (int r = 0; r < nch; ++r) {
            decimate(block.data() + static_cast<std::size_t>(r) * nSamples,
                     nSamples, decM, chDec);
            const int cn = std::min<int>(nDec, static_cast<int>(chDec.size()));
            std::copy(chDec.begin(), chDec.begin() + cn,
                      decBlock.begin() + static_cast<std::size_t>(r) * nDec);
        }
        block.swap(decBlock);
        effFs = fs / decM;
        effN = std::max(2, N / decM);
        effStep = std::max(1, params.stepSamples / decM);
        effNSamples = nDec;
    }

    const DpssTapers& tapers = tapersFor(effN, params.nw, params.nTapers);
    const int nfftReq = std::max((decM > 1) ? params.nfft / decM : params.nfft, effN);
    RealFftPlan plan(nfftReq);                    // effective size from the plan
    const int effNfft = plan.nfft();
    const int nCols = 1 + (effNSamples - effN) / effStep;

    // Time axis: window-centre time (s); decimation preserves real time.
    img.colTimes.resize(nCols);
    for (int c = 0; c < nCols; ++c)
        img.colTimes[c] = (c * effStep + effN / 2.0) / effFs;

    double vmin = std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    if (params.mode == SpectralMode::TimeFrequencySingleChannel) {
        int ch = std::max(0, std::min(params.singleChannel, nch - 1));
        const double* sig = block.data() + static_cast<std::size_t>(ch) * effNSamples;

        std::vector<std::vector<double>> sg;
        dispatchSpectrogram(sig, effNSamples, tapers, effFs, effNfft,
                            effStep, params.weighting, params.backend, sg);

        int lo, hi; bandBins(params.freqLow, params.freqHigh, effNfft, effFs, lo, hi);
        const int rows = hi - lo + 1;
        const std::vector<double> allFreqs = psdFrequencies(effNfft, effFs);

        img.rows = rows; img.cols = static_cast<int>(sg.size());
        img.data.assign(static_cast<std::size_t>(rows) * img.cols, 0.0f);
        img.freqs.resize(rows);
        for (int r = 0; r < rows; ++r) img.freqs[r] = allFreqs[lo + r];
        for (int c = 0; c < img.cols; ++c)
            for (int r = 0; r < rows; ++r) {
                const double v = sg[c][lo + r];
                img.data[static_cast<std::size_t>(r) * img.cols + c] = static_cast<float>(v);
                vmin = std::min(vmin, v); vmax = std::max(vmax, v);
            }
    } else {
        // FrequencyAcrossChannels: band power per channel over time.
        int lo, hi; bandBins(params.freqLow, params.freqHigh, effNfft, effFs, lo, hi);
        const double df = effFs / effNfft;

        img.rows = nch; img.cols = nCols;
        img.data.assign(static_cast<std::size_t>(nch) * nCols, 0.0f);
        img.rowChannels = channels;

        // One row (channel) per task; windows handled inside the spectrogram.
        std::vector<double> rowMin(nch,  std::numeric_limits<double>::infinity());
        std::vector<double> rowMax(nch, -std::numeric_limits<double>::infinity());
        const bool useGpu = (params.backend == SpectralBackend::Cuda) && gpu::available();
#ifdef _OPENMP
        #pragma omp parallel for schedule(dynamic) if(!useGpu)
#endif
        for (int r = 0; r < nch; ++r) {
            const double* sig = block.data() + static_cast<std::size_t>(r) * effNSamples;
            std::vector<std::vector<double>> sg;
            dispatchSpectrogram(sig, effNSamples, tapers, effFs, effNfft,
                                effStep, params.weighting, params.backend, sg);
            for (int c = 0; c < static_cast<int>(sg.size()) && c < nCols; ++c) {
                double bp = 0.0;
                for (int b = lo; b <= hi; ++b) bp += sg[c][b] * df; // integrate band
                img.data[static_cast<std::size_t>(r) * nCols + c] = static_cast<float>(bp);
                rowMin[r] = std::min(rowMin[r], bp);
                rowMax[r] = std::max(rowMax[r], bp);
            }
        }
        for (int r = 0; r < nch; ++r) { vmin = std::min(vmin, rowMin[r]); vmax = std::max(vmax, rowMax[r]); }
    }

    if (!std::isfinite(vmin)) { vmin = 0.0; vmax = 0.0; }
    img.valueMin = vmin; img.valueMax = vmax;

    cache_ = std::move(img);
    cacheParams_ = params;
    cacheWindowId_ = windowId;
    hasCache_ = true;
    return cache_;
}

} // namespace spectral
} // namespace neuroscope
