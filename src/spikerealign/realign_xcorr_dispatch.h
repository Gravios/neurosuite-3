#pragma once
#include <cstdint>

namespace XcorrDispatch {
    const char* backendName();
    int compute(const int16_t* waveforms, const int16_t* tmpl,
                int nSpikes, int nChannels, int nSamples,
                int maxShift, float minScore,
                int* shifts_out, float* scores_out);
}
