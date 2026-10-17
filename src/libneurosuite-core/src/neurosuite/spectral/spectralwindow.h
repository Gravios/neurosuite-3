#ifndef NEUROSCOPE_SPECTRAL_SPECTRALWINDOW_H
#define NEUROSCOPE_SPECTRAL_SPECTRALWINDOW_H

// Maps the waveform view's time window to the spectral view's window. The
// spectral view may use its own width (a wider span resolves frequency better)
// but is always centred on the same time point as the traces, so toggling or
// scrolling keeps both views looking at the same instant. Qt-free so the
// centring and clamping are unit-tested directly.

namespace neuroscope {
namespace spectral {

struct TimeWindow {
    long start; // ms
    long width; // ms
};

// traceStart/traceWidth : the waveform view's window (ms).
// lockToTrace           : if true, use the trace window unchanged.
// span                  : the spectral view's own width (ms) when unlocked;
//                         <= 0 means follow the trace width.
// recordingLength       : total length (ms); <= 0 means unknown (no end clamp).
inline TimeWindow spectralWindow(long traceStart, long traceWidth,
                                 bool lockToTrace, long span,
                                 long recordingLength)
{
    long width = (lockToTrace || span <= 0) ? traceWidth : span;
    if (width < 1) width = 1;

    const long center = traceStart + traceWidth / 2;
    long start = center - width / 2;
    if (start < 0) start = 0;

    if (recordingLength > 0) {
        if (width > recordingLength) width = recordingLength;
        if (start + width > recordingLength) start = recordingLength - width;
        if (start < 0) start = 0;
    }
    return {start, width};
}

} // namespace spectral
} // namespace neuroscope

#endif // NEUROSCOPE_SPECTRAL_SPECTRALWINDOW_H
