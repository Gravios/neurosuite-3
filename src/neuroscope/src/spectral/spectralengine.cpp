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
// Target band for anti-aliased decimation. The decimated representation always
// spans 0..kDecimatedBandHz, independent of the inspector's display range, so a
// single decimated spectrogram can back any sub-band selection within it.
constexpr double kDecimatedBandHz = 300.0;

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

// --- Band-pass + RMS band power (FrequencyAcrossChannels) --------------------
// A 2nd-order Butterworth high-pass at the low edge cascaded with a low-pass at
// the high edge. Transposed Direct-Form II biquads; coefficients from the RBJ
// cookbook with Q = 1/sqrt(2). Edges at/near DC or Nyquist drop that section.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
inline Biquad lowpassBiquad(double f0, double fs, double Q) {
    const double w = 2.0 * M_PI * f0 / fs, c = std::cos(w), s = std::sin(w);
    const double al = s / (2.0 * Q), a0 = 1.0 + al;
    Biquad q;
    q.b0 = ((1 - c) / 2) / a0; q.b1 = (1 - c) / a0; q.b2 = ((1 - c) / 2) / a0;
    q.a1 = (-2 * c) / a0;      q.a2 = (1 - al) / a0;
    return q;
}
inline Biquad highpassBiquad(double f0, double fs, double Q) {
    const double w = 2.0 * M_PI * f0 / fs, c = std::cos(w), s = std::sin(w);
    const double al = s / (2.0 * Q), a0 = 1.0 + al;
    Biquad q;
    q.b0 = ((1 + c) / 2) / a0; q.b1 = (-(1 + c)) / a0; q.b2 = ((1 + c) / 2) / a0;
    q.a1 = (-2 * c) / a0;      q.a2 = (1 - al) / a0;
    return q;
}
// Per-channel band power (band-pass + windowed RMS^2), vectorised across
// channels. The IIR recurrence is serial over samples but independent across
// channels, so channels are processed in sample-major tiles: the inner loop
// over a tile's channels has no cross-lane dependency and vectorises (one
// AVX-512 vector covers 8 channels). blockCM is channel-major (each channel's
// samples contiguous); out is channel-major out[ch * nCols + col]. Build with
// -march=native (NS_ZEN_OPT) for the full width.
void bandPowerByChannel(const double* blockCM, int nch, int effNSamples,
                        int effN, int effStep, int nCols,
                        double effFs, double flo, double fhi, float* out)
{
    const double nyq = 0.5 * effFs, Q = 0.70710678118654752;
    if (flo < 0.0) flo = 0.0;
    if (fhi <= 0.0 || fhi > 0.999 * nyq) fhi = 0.999 * nyq;
    const bool useHP = flo > 0.5;
    const bool useLP = fhi < 0.999 * nyq;
    // Identity biquad (default Biquad{}) when an edge is skipped, so the fused
    // loop applies both stages unconditionally with no branch to vectorise around.
    const Biquad hp = useHP ? highpassBiquad(flo, effFs, Q) : Biquad{};
    const Biquad lp = useLP ? lowpassBiquad(fhi, effFs, Q) : Biquad{};
    const double hb0=hp.b0,hb1=hp.b1,hb2=hp.b2,ha1=hp.a1,ha2=hp.a2;
    const double lb0=lp.b0,lb1=lp.b1,lb2=lp.b2,la1=lp.a1,la2=lp.a2;
    const double invN = 1.0 / effN;
    constexpr int TILE = 8;                 // one AVX-512 double vector

#ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
#endif
    for (int c0 = 0; c0 < nch; c0 += TILE) {
        const int w = std::min(TILE, nch - c0);
        std::vector<double> sm(static_cast<std::size_t>(effNSamples) * w);
        for (int k = 0; k < w; ++k) {       // transpose tile -> sample-major
            const double* src = blockCM + static_cast<std::size_t>(c0 + k) * effNSamples;
            for (int i = 0; i < effNSamples; ++i) sm[static_cast<std::size_t>(i) * w + k] = src[i];
        }
        // Single fused pass: high-pass, low-pass and prefix-sum-of-squares per
        // channel, streaming the tile through memory once instead of three times.
        double h1[TILE]={0}, h2[TILE]={0}, l1[TILE]={0}, l2[TILE]={0}, acc[TILE]={0};
        for (int i = 0; i < effNSamples; ++i) {
            double* row = &sm[static_cast<std::size_t>(i) * w];
            #pragma omp simd
            for (int k = 0; k < w; ++k) {
                const double x = row[k];
                const double yh = hb0 * x + h1[k];
                h1[k] = hb1 * x - ha1 * yh + h2[k];
                h2[k] = hb2 * x - ha2 * yh;
                const double yl = lb0 * yh + l1[k];
                l1[k] = lb1 * yh - la1 * yl + l2[k];
                l2[k] = lb2 * yh - la2 * yl;
                acc[k] += yl * yl;
                row[k] = acc[k];            // prefix sum of squares
            }
        }
        for (int col = 0; col < nCols; ++col) {
            const int s0 = col * effStep, sEnd = s0 + effN;
            const double* hiR = &sm[static_cast<std::size_t>(sEnd - 1) * w];
            const double* loR = (s0 > 0) ? &sm[static_cast<std::size_t>(s0 - 1) * w] : nullptr;
            for (int k = 0; k < w; ++k) {
                const double sum = hiR[k] - (loR ? loR[k] : 0.0);
                out[static_cast<std::size_t>(c0 + k) * nCols + col] = static_cast<float>(sum * invN);
            }
        }
    }
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
        && decimate == o.decimate
        && car == o.car
        && refRegress == o.refRegress
        && refChannel == o.refChannel
        // bandLo/bandHi now define the FrequencyAcrossChannels filter pass-band,
        // so they affect the computed image and must invalidate the cache.
        && bandLo == o.bandLo
        && bandHi == o.bandHi;
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

void integrateBand(SpectralImage& img, double bandLo, double bandHi)
{
    if (img.mode != SpectralMode::FrequencyAcrossChannels || img.cube.empty())
        return;
    const int nF = static_cast<int>(img.cubeFreqs.size());
    const int rows = img.rows, cols = img.cols;
    if (nF <= 0 || rows <= 0 || cols <= 0) return;

    // Resolve the cube bin span for [bandLo, bandHi]; fall back to the whole
    // cube when the band is degenerate or selects no bins.
    int b0 = 0, b1 = nF - 1;
    if (bandHi > bandLo) {
        b0 = nF; b1 = -1;
        for (int f = 0; f < nF; ++f) {
            const double hz = img.cubeFreqs[f];
            if (hz >= bandLo && hz <= bandHi) { if (f < b0) b0 = f; if (f > b1) b1 = f; }
        }
        if (b1 < b0) { b0 = 0; b1 = nF - 1; }
    }

    double vmin = std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const float* cell = &img.cube[(static_cast<std::size_t>(r) * cols + c) * nF];
            double bp = 0.0;
            for (int f = b0; f <= b1; ++f) bp += static_cast<double>(cell[f]) * img.cubeDf;
            img.data[static_cast<std::size_t>(r) * cols + c] = static_cast<float>(bp);
            vmin = std::min(vmin, bp); vmax = std::max(vmax, bp);
        }
    if (!std::isfinite(vmin)) { vmin = 0.0; vmax = 0.0; }
    img.valueMin = vmin; img.valueMax = vmax;
}

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
    // Clamp the analysis window to the data: a window longer than the supplied
    // samples (e.g. a saved windowSamples bigger than the current view) yields a
    // single best-effort column rather than an empty image, which would otherwise
    // leave the spectral view stuck showing "computing...".
    const int N = std::min(params.windowSamples, nSamples);
    const double fs = params.samplingRate;
    if (nch < 1 || N < 2 || fs <= 0.0 || params.stepSamples <= 0) {
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

    // Re-referencing (channel-major, before any decimation), in increasing
    // specificity: common-average reference removes whole-array common mode;
    // reference regression removes one chosen channel from every channel; ZCA
    // whitening decorrelates what remains. Each is an independent toggle.
    if (params.car && nch > 1)
        commonAverageReference(block.data(), nch, nSamples);
    if (params.refRegress && nch > 1) {
        const int rr = std::max(0, std::min(params.refChannel, nch - 1));
        referenceRegress(block.data(), nch, nSamples, rr);
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
        decM = decimationFactor(fs, kDecimatedBandHz, 1.25, 64, nSamples, 64);
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

    const int nCols = 1 + (effNSamples - effN) / effStep;

    // Time axis: window-centre time (s); decimation preserves real time.
    img.colTimes.resize(nCols);
    for (int c = 0; c < nCols; ++c)
        img.colTimes[c] = (c * effStep + effN / 2.0) / effFs;

    double vmin = std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();

    if (params.mode == SpectralMode::TimeFrequencySingleChannel) {
        const DpssTapers& tapers = tapersFor(effN, params.nw, params.nTapers);
        const int nfftReq = std::max((decM > 1) ? params.nfft / decM : params.nfft, effN);
        RealFftPlan plan(nfftReq);                // effective size from the plan
        const int effNfft = plan.nfft();
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
        // FrequencyAcrossChannels: per-channel band power over time. Estimated
        // by band-pass filtering each channel to the selected band and taking
        // the windowed mean square (RMS^2) - far cheaper than a full multitaper
        // spectrogram per channel, and no freq cube is retained.
        img.rows = nch; img.cols = nCols;
        img.data.assign(static_cast<std::size_t>(nch) * nCols, 0.0f);
        img.rowChannels = channels;
        img.cube.clear(); img.cubeFreqs.clear(); img.cubeDf = 0.0;

        // Effective band: the slider sub-band when set, else the analysis range;
        // a high edge <= the low edge (e.g. 0) means up to Nyquist.
        double flo = params.bandLo, fhi = params.bandHi;
        if (!(fhi > flo)) { flo = params.freqLow; fhi = params.freqHigh; }
        if (fhi <= 0.0) fhi = 0.5 * effFs;
        if (flo < 0.0) flo = 0.0;
        fhi = std::min(fhi, 0.5 * effFs);

        bandPowerByChannel(block.data(), nch, effNSamples, effN, effStep, nCols,
                           effFs, flo, fhi, img.data.data());

        for (float v : img.data) { vmin = std::min(vmin, (double)v); vmax = std::max(vmax, (double)v); }
        img.valueMin = vmin; img.valueMax = vmax;
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
