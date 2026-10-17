#include "spectralgpu.h"

// Default (no-CUDA) implementation of the GPU backend: there is no device, so
// available() is false and multitaperSpectrogram() always declines, causing the
// engine to use its CPU path. This translation unit is compiled when USE_CUDA
// is off; spectralgpu.cu replaces it when CUDA is enabled.

namespace neuroscope {
namespace spectral {
namespace gpu {

bool available() { return false; }

bool multitaperSpectrogram(const double*, int,
                           const DpssTapers&,
                           double, int, int,
                           TaperWeighting,
                           std::vector<std::vector<double>>& out)
{
    out.clear();
    return false; // request CPU fallback
}

} // namespace gpu
} // namespace spectral
} // namespace neuroscope
