#ifndef NEUROSCOPE_SPECTRAL_SPECTRALENGINE_H
#define NEUROSCOPE_SPECTRAL_SPECTRALENGINE_H

// On-demand multitaper spectral compute engine.
//
// Bridges the windowed multichannel trace data to the DSP core and produces
// the two display images the spectral view renders:
//
//   FrequencyAcrossChannels : for a chosen frequency band, the band power of
//                             every channel over time (rows = channels,
//                             columns = time) — the spectral analogue of the
//                             stacked waveform layout.
//   TimeFrequencySingleChannel : the time x frequency spectrogram of one
//                             channel (rows = frequency bins in the display
//                             band, columns = time).
//
// Optional cross-channel ("common") whitening is applied to the whole block
// before estimation. DPSS tapers and the last result are cached so that
// nothing is recomputed while the window and parameters are unchanged
// (computed on demand). The CPU path parallelises with OpenMP; a CUDA backend
// can be slotted in behind the same compute() entry point.
//
// The engine is Qt-free and takes a plain sample-major buffer, so it is unit
// tested directly; the view adapts neuroscope's Array<dataType> to it.

#include "dpss.h"
#include "multitaper.h"

#include <cstdint>
#include <vector>

namespace neuroscope {
namespace spectral {

enum class SpectralMode {
    FrequencyAcrossChannels,    // rows = channels, columns = time
    TimeFrequencySingleChannel  // rows = frequency, columns = time
};

enum class SpectralBackend { Cpu, Cuda };

struct SpectralParams {
    SpectralMode mode      = SpectralMode::TimeFrequencySingleChannel;
    double samplingRate    = 1.0;   // Hz
    int    windowSamples   = 256;   // taper length N (per-FFT window)
    int    nfft            = 256;   // transform length (>= windowSamples)
    int    stepSamples     = 128;   // hop between windows
    double nw              = 3.0;   // time-half-bandwidth product
    int    nTapers         = 5;     // K
    TaperWeighting weighting = TaperWeighting::Eigenvalue;
    bool   whiten          = false; // common (cross-channel) whitening
    double freqLow         = 0.0;   // display / integration band low edge (Hz)
    double freqHigh        = 0.0;   // high edge; <= 0 means Nyquist
    int    singleChannel   = 0;     // row index (into the channel list) for mode B
    SpectralBackend backend = SpectralBackend::Cpu;
    bool   decimate        = false; // anti-alias + downsample to the band before FFT

    // Integration sub-band for mode B (FrequencyAcrossChannels): the per-channel
    // power is integrated over [bandLo, bandHi] within the displayed [freqLow,
    // freqHigh]. bandHi <= bandLo means "use the whole displayed range". This is
    // a pure re-integration of the retained cube, so it is deliberately excluded
    // from sameAs() below: changing it must not invalidate the computed cache.
    double bandLo          = 0.0;
    double bandHi          = 0.0;

    // Equality of everything that affects the result (used for caching).
    bool sameAs(const SpectralParams& o) const;
};

struct SpectralImage {
    SpectralMode mode = SpectralMode::TimeFrequencySingleChannel;
    int rows = 0;                       // channels (mode A) or freq bins (mode B)
    int cols = 0;                       // time columns
    std::vector<float> data;            // rows*cols, row-major; image[r*cols + c]
    std::vector<double> freqs;          // length rows for mode B (Hz)
    std::vector<int>    rowChannels;    // length rows for mode A (channel indices)
    std::vector<double> colTimes;       // length cols, window-centre time (s) from window start
    double valueMin = 0.0;              // min/max of data (linear power) for scaling
    double valueMax = 0.0;

    // Mode B only: the freq-resolved band-power density, retained so the
    // integration sub-band can be re-selected without recomputing. Layout is
    // [(r*cols + c)*nFreq + f]; cubeFreqs gives the Hz of each f, cubeDf the bin
    // width used to turn a bin sum into a band power. Empty in mode A.
    std::vector<float>  cube;
    std::vector<double> cubeFreqs;
    double cubeDf = 0.0;
    bool valid() const { return rows > 0 && cols > 0 && static_cast<int>(data.size()) == rows * cols; }
    float at(int r, int c) const { return data[static_cast<std::size_t>(r) * cols + c]; }
};

// Re-integrate a mode-B image's retained cube over a new sub-band [bandLo,
// bandHi] (Hz), writing img.data and refreshing img.valueMin/valueMax. A band
// with bandHi <= bandLo (or one that selects no bins) integrates the whole cube.
// No-op for mode A or an image without a cube.
void integrateBand(SpectralImage& img, double bandLo, double bandHi);

class SpectralEngine {
public:
    SpectralEngine() = default;

    // Compute the spectral image for a window.
    //
    // sampleMajor : nSamples*nChannels doubles, sample-major
    //               (sampleMajor[s*nChannels + c]); matches neuroscope's
    //               row-major Array layout once cast to double.
    // channels    : 0-based columns to include, in display order.
    // windowId    : opaque identity of this data window (e.g. start sample);
    //               with params it forms the cache key, so an unchanged
    //               window+params returns the cached image without recompute.
    const SpectralImage& compute(const double* sampleMajor, int nSamples, int nChannels,
                                 const std::vector<int>& channels,
                                 const SpectralParams& params,
                                 std::uint64_t windowId);

    void invalidate() { hasCache_ = false; }

private:
    // DPSS cache (recomputed only when N/NW/K change).
    DpssTapers tapers_;
    int    cachedN_ = 0, cachedK_ = 0;
    double cachedNW_ = 0.0;
    const DpssTapers& tapersFor(int N, double nw, int K);

    // Result cache.
    SpectralImage cache_;
    SpectralParams cacheParams_;
    std::uint64_t  cacheWindowId_ = 0;
    bool hasCache_ = false;
};

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_SPECTRALENGINE_H
