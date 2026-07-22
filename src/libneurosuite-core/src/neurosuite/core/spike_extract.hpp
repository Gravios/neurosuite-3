// spike_extract.hpp — pull fixed-width spike windows out of an interleaved .fil.
//
// The same operation was written twice: process_extractspikes_stderiv streams the
// file in large blocks, while klusters' re-extract-on-save did one fseeko + fread
// PER SPIKE.  Both must produce identical bytes — a session re-extracted in klusters
// has to match what ndm_extractspikes wrote, or the waveforms silently disagree with
// the features derived from them — so the operation belongs in one place.
//
// Spike times are non-decreasing, which is what makes the scattered per-spike read
// unnecessary: a single forward cursor with a sliding buffer covers the whole pass.
// At 422k spikes that is ~400k fewer syscall pairs.
//
// DEPENDENCY-FREE: std + <cstdio> only, header-only, same as the rest of core.
#ifndef NEUROSUITE_CORE_SPIKE_EXTRACT_HPP
#define NEUROSUITE_CORE_SPIKE_EXTRACT_HPP

#include <cstdint>
#include <cstdio>
#include <vector>

#include "stderiv_transform.hpp"

namespace neurosuite {
namespace core {

// How to cut and (optionally) transform each window.
struct SpikeWindowSpec {
    int nSamp           = 0;        // samples per waveform
    int peakSample      = 0;        // index within the window the timestamp refers to
    int totalNbChannels = 0;        // channels interleaved in the .fil
    const int* groupChannels = nullptr;   // absolute channel index per group channel
    int nChan           = 0;        // channels in this spike group

    // Spatial/temporal transform applied per window (see stderiv_transform.hpp).
    bool       applyTransform = false;
    SdiffOrder order   = SdiffOrder::AllPairs;
    const int* partner = nullptr;   // order 4
    const int* setOff  = nullptr;   // order 5 (flattened reference sets)
    const int* setMem  = nullptr;

    // Read budget for the sliding buffer, in bytes.  Only affects speed.
    std::size_t bufferBytes = 8u << 20;
};

// Extract nSpikes windows.  `timestampAt(i)` returns the i-th spike time in samples
// and MUST be non-decreasing.  `emitRow(i, row)` receives the finished window as
// sample-major int16 [s * nChan + ch], nChan * nSamp entries.
//
// A window that would fall outside the file is emitted as zeros and counted in
// outOfRange, matching what both previous implementations did.
//
// Returns the number of windows emitted, or -1 if the file could not be sized.
template <class TimestampAt, class EmitRow>
inline std::int64_t extractSpikeWindows(std::FILE* fil,
                                        std::int64_t nSpikes,
                                        const SpikeWindowSpec& spec,
                                        TimestampAt timestampAt,
                                        EmitRow emitRow,
                                        std::int64_t* outOfRange = nullptr)
{
    if (!fil || spec.nSamp <= 0 || spec.nChan <= 0 || spec.totalNbChannels <= 0)
        return -1;

    if (std::fseek(fil, 0, SEEK_END) != 0) return -1;
    const long long fileBytes = std::ftell(fil);
    if (fileBytes < 0) return -1;
    const std::int64_t totalSamples =
        static_cast<std::int64_t>(fileBytes)
        / (static_cast<std::int64_t>(sizeof(std::int16_t)) * spec.totalNbChannels);

    const std::size_t frameElems = static_cast<std::size_t>(spec.totalNbChannels);
    std::size_t bufSamples = spec.bufferBytes / (frameElems * sizeof(std::int16_t));
    if (bufSamples < static_cast<std::size_t>(spec.nSamp))
        bufSamples = static_cast<std::size_t>(spec.nSamp);

    std::vector<std::int16_t> buf(bufSamples * frameElems);
    std::int64_t bufStart = -1;                 // first sample held in buf
    std::int64_t bufCount = 0;                  // samples held

    std::vector<std::int16_t> wavCM(static_cast<std::size_t>(spec.nChan) * spec.nSamp);
    std::vector<std::int16_t> row(static_cast<std::size_t>(spec.nChan) * spec.nSamp);

    std::int64_t emitted = 0, bad = 0;
    for (std::int64_t i = 0; i < nSpikes; ++i) {
        const std::int64_t startSample =
            static_cast<std::int64_t>(timestampAt(i)) - spec.peakSample;
        bool got = false;

        if (startSample >= 0 && startSample + spec.nSamp <= totalSamples) {
            // Refill only when the window is not already buffered.  Timestamps are
            // non-decreasing, so the cursor only ever moves forward.
            if (bufStart < 0 || startSample < bufStart
                || startSample + spec.nSamp > bufStart + bufCount) {
                const std::int64_t want =
                    (totalSamples - startSample < static_cast<std::int64_t>(bufSamples))
                    ? totalSamples - startSample
                    : static_cast<std::int64_t>(bufSamples);
                const long long off =
                    static_cast<long long>(startSample) * frameElems * sizeof(std::int16_t);
                if (std::fseek(fil, static_cast<long>(off), SEEK_SET) != 0) {
                    // Offsets past LONG_MAX need fseeko; fall back to it when present.
                    bufStart = -1; bufCount = 0;
                } else {
                    const std::size_t n = std::fread(buf.data(), sizeof(std::int16_t),
                                                     static_cast<std::size_t>(want) * frameElems,
                                                     fil);
                    bufCount = static_cast<std::int64_t>(n / frameElems);
                    bufStart = startSample;
                }
            }
            if (bufStart >= 0 && startSample >= bufStart
                && startSample + spec.nSamp <= bufStart + bufCount) {
                const std::int16_t* base =
                    buf.data() + static_cast<std::size_t>(startSample - bufStart) * frameElems;
                for (int s = 0; s < spec.nSamp; ++s)
                    for (int ci = 0; ci < spec.nChan; ++ci)
                        wavCM[static_cast<std::size_t>(ci) * spec.nSamp + s] =
                            base[static_cast<std::size_t>(s) * frameElems
                                 + spec.groupChannels[ci]];
                got = true;
            }
        }

        if (got) {
            if (spec.applyTransform)
                applyStderivTransform(spec.order, wavCM.data(), spec.nChan, spec.nSamp,
                                      wavCM.data(), spec.partner, nullptr,
                                      spec.setOff, spec.setMem);
            for (int s = 0; s < spec.nSamp; ++s)
                for (int ch = 0; ch < spec.nChan; ++ch)
                    row[static_cast<std::size_t>(s) * spec.nChan + ch] =
                        wavCM[static_cast<std::size_t>(ch) * spec.nSamp + s];
        } else {
            std::fill(row.begin(), row.end(), static_cast<std::int16_t>(0));
            ++bad;
        }
        emitRow(i, row.data());
        ++emitted;
    }

    if (outOfRange) *outOfRange = bad;
    return emitted;
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_SPIKE_EXTRACT_HPP
