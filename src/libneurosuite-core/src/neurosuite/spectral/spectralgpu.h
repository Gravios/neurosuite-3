#ifndef NEUROSCOPE_SPECTRAL_SPECTRALGPU_H
#define NEUROSCOPE_SPECTRAL_SPECTRALGPU_H

// GPU backend seam for the multitaper spectrogram.
//
// The compute engine calls gpu::multitaperSpectrogram() when the GPU backend
// is selected. It returns true if it produced the result on the GPU, or false
// to tell the engine to fall back to the CPU path (not available, problem too
// small to be worth a transfer, or any runtime error). This keeps the GPU an
// optional accelerator behind one interface: the default build links a CPU
// stub that always declines, and a CUDA translation unit replaces it when
// USE_CUDA is enabled (cuFFT batched transforms).

#include "dpss.h"
#include "multitaper.h"

#include <vector>

namespace neuroscope {
namespace spectral {
namespace gpu {

// True if a usable GPU backend is compiled in and a device is present.
bool available();

// Compute the sliding multitaper spectrogram on the GPU. Same contract as
// multitaperSpectrogram(): out[col] is the one-sided PSD of window col.
// Returns false (leaving out untouched/cleared) to request CPU fallback.
bool multitaperSpectrogram(const double* signal, int n,
                           const DpssTapers& tapers,
                           double fs, int nfft, int step,
                           TaperWeighting weighting,
                           std::vector<std::vector<double>>& out);

} // namespace gpu
} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_SPECTRALGPU_H
