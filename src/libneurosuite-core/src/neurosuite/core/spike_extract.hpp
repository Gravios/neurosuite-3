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
// DEPENDENCY-FREE: std + <cstdio> (plus the platform's 64-bit seek), header-only.
#ifndef NEUROSUITE_CORE_SPIKE_EXTRACT_HPP
#define NEUROSUITE_CORE_SPIKE_EXTRACT_HPP

#include <cstdint>
#include <cstdio>
#include <vector>

#include "stderiv_transform.hpp"

#if !defined(_WIN32)
#include <sys/types.h>   // off_t for fseeko / ftello
#endif

namespace neurosuite {
namespace core {

namespace detail {
// 64-bit file positioning: fseek/ftell take a long, which is 32 bits on Windows,
// so a .fil past 2 GiB would be read as zeros there.
inline int seek64(std::FILE* f, long long off) {
#if defined(_WIN32)
    return _fseeki64(f, off, SEEK_SET);
#else
    return fseeko(f, static_cast<off_t>(off), SEEK_SET);
#endif
}
inline long long sizeOf(std::FILE* f) {
#if defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) != 0) return -1;
    return _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return -1;
    return static_cast<long long>(ftello(f));
#endif
}
}  // namespace detail

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
    const ChannelExprSet* expr = nullptr;  // order 6 (sdiffPairs expressions)
    // Seed each window's temporal difference with the spatial derivative of
    // the sample before it (what ndm_extractspikes writes since the
    // detect/extract split).  false = seed 0, the convention of .spk files
    // written by the older single-pass extractor; use detectStderivSeeding()
    // to tell which one an existing file follows.
    bool seedFromPrevious = true;
    // With applyTransform: false = the spatial derivative only (rounded and
    // saturated per sample, no temporal difference, no seed), which is what
    // process_extractspikes_sdiff writes.
    bool temporalDiff = true;

    // Read budget for the sliding buffer, in bytes.  Only affects speed.
    std::size_t bufferBytes = 8u << 20;
};

// Extract nSpikes windows.  `timestampAt(i)` returns the i-th spike time in samples
// and MUST be non-decreasing.  `emitRow(i, row)` receives the finished window as
// sample-major int16 [s * nChan + ch], nChan * nSamp entries.
//
// A window that would fall outside the file is emitted as zeros and counted in
// outOfRange, so there is always one row per timestamp and the rows stay aligned
// with the .res (and .clu).
//
// With applyTransform and seedFromPrevious, each window's temporal difference
// is seeded with the spatial derivative of the sample before it (stderivSeed),
// exactly as process_extractspikes_stderiv -R does; only a window starting at
// sample 0 is seeded with 0.  With seedFromPrevious false every window is seeded
// with 0.
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

    const long long fileBytes = detail::sizeOf(fil);
    if (fileBytes < 0) return -1;
    const std::int64_t totalSamples =
        static_cast<std::int64_t>(fileBytes)
        / (static_cast<std::int64_t>(sizeof(std::int16_t)) * spec.totalNbChannels);

    const std::size_t frameElems = static_cast<std::size_t>(spec.totalNbChannels);
    std::size_t bufSamples = spec.bufferBytes / (frameElems * sizeof(std::int16_t));
    if (bufSamples < static_cast<std::size_t>(spec.nSamp) + 1)
        bufSamples = static_cast<std::size_t>(spec.nSamp) + 1;   // window + seed sample

    std::vector<std::int16_t> buf(bufSamples * frameElems);
    std::int64_t bufStart = -1;                 // first sample held in buf
    std::int64_t bufCount = 0;                  // samples held

    std::vector<std::int16_t> wavCM(static_cast<std::size_t>(spec.nChan) * spec.nSamp);
    std::vector<std::int16_t> row(static_cast<std::size_t>(spec.nChan) * spec.nSamp);
    std::vector<std::int16_t> leadRow(static_cast<std::size_t>(spec.nChan));
    std::vector<std::int16_t> seed(static_cast<std::size_t>(spec.nChan));
    std::vector<double>       spatialRow(static_cast<std::size_t>(spec.nChan));

    std::int64_t emitted = 0, bad = 0;
    for (std::int64_t i = 0; i < nSpikes; ++i) {
        const std::int64_t startSample =
            static_cast<std::int64_t>(timestampAt(i)) - spec.peakSample;
        bool got = false;
        // With the transform, also hold the sample before the window (its seed).
        const std::int64_t lead =
            (spec.applyTransform && spec.temporalDiff && spec.seedFromPrevious
             && startSample >= 1) ? 1 : 0;
        const std::int64_t first = startSample - lead;

        if (startSample >= 0 && startSample + spec.nSamp <= totalSamples) {
            // Refill only when the span is not already buffered.  Timestamps are
            // non-decreasing, so the cursor only ever moves forward.
            if (bufStart < 0 || first < bufStart
                || startSample + spec.nSamp > bufStart + bufCount) {
                const std::int64_t want =
                    (totalSamples - first < static_cast<std::int64_t>(bufSamples))
                    ? totalSamples - first
                    : static_cast<std::int64_t>(bufSamples);
                const long long off =
                    static_cast<long long>(first) * static_cast<long long>(frameElems)
                    * static_cast<long long>(sizeof(std::int16_t));
                if (detail::seek64(fil, off) != 0) {
                    bufStart = -1; bufCount = 0;
                } else {
                    const std::size_t n = std::fread(buf.data(), sizeof(std::int16_t),
                                                     static_cast<std::size_t>(want) * frameElems,
                                                     fil);
                    bufCount = static_cast<std::int64_t>(n / frameElems);
                    bufStart = first;
                }
            }
            if (bufStart >= 0 && first >= bufStart
                && startSample + spec.nSamp <= bufStart + bufCount) {
                const std::int16_t* base =
                    buf.data() + static_cast<std::size_t>(startSample - bufStart) * frameElems;
                for (int s = 0; s < spec.nSamp; ++s)
                    for (int ci = 0; ci < spec.nChan; ++ci)
                        wavCM[static_cast<std::size_t>(ci) * spec.nSamp + s] =
                            base[static_cast<std::size_t>(s) * frameElems
                                 + spec.groupChannels[ci]];
                if (lead) {
                    const std::int16_t* prevFrame = base - frameElems;   // first >= bufStart
                    for (int ci = 0; ci < spec.nChan; ++ci)
                        leadRow[static_cast<std::size_t>(ci)] =
                            prevFrame[spec.groupChannels[ci]];
                }
                got = true;
            }
        }

        if (got) {
            if (spec.applyTransform && !spec.temporalDiff) {
                std::vector<double>& r = spatialRow;
                for (int s = 0; s < spec.nSamp; ++s) {
                    for (int ci = 0; ci < spec.nChan; ++ci)
                        r[static_cast<std::size_t>(ci)] =
                            wavCM[static_cast<std::size_t>(ci) * spec.nSamp + s];
                    for (int ci = 0; ci < spec.nChan; ++ci)
                        wavCM[static_cast<std::size_t>(ci) * spec.nSamp + s] =
                            clampToInt16(spatialDeriv(spec.order, r.data(), ci, spec.nChan,
                                                      spec.partner, spec.setOff, spec.setMem,
                                                      spec.expr));
                }
            } else if (spec.applyTransform) {
                const std::int16_t* seedPtr = nullptr;
                if (lead) {
                    stderivSeed(spec.order, leadRow.data(), spec.nChan, seed.data(),
                                spec.partner, spec.setOff, spec.setMem, spec.expr);
                    seedPtr = seed.data();
                }
                applyStderivTransform(spec.order, wavCM.data(), spec.nChan, spec.nSamp,
                                      wavCM.data(), spec.partner, seedPtr,
                                      spec.setOff, spec.setMem, spec.expr);
            }
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

// Which seed an existing stderiv .spk was written with.  The two conventions
// differ only in sample 0 of each window: Previous (sd[0] - sd[-1], what
// ndm_extractspikes writes since the detect/extract split) and Zero (sd[0], the
// older single-pass extractor).  A tool that rewrites single spikes of an
// existing file must follow the file's convention, or the rewritten spikes sit
// at a fixed offset from the rest in feature space.
enum class StderivSeeding { Previous, Zero, Unknown };

// Decide the convention from up to `maxCheck` spikes: recompute sample 0 both
// ways from the raw recording and compare with the stored value.
// `timestampAt(i)` is spike i's time; `storedSample0At(i, ci)` its stored sample
// 0 on group channel ci.  Only channels where the two conventions differ vote;
// a 90 % majority of at least 8 votes decides, otherwise Unknown (e.g. a file
// whose spikes were partly rewritten each way, or too few usable spikes).
template <class TimestampAt, class StoredSample0At>
inline StderivSeeding detectStderivSeeding(std::FILE* fil, std::int64_t nSpikes,
                                           const SpikeWindowSpec& spec,
                                           TimestampAt timestampAt,
                                           StoredSample0At storedSample0At,
                                           int maxCheck = 64)
{
    if (!fil || !spec.applyTransform || spec.nChan <= 0 || spec.totalNbChannels <= 0)
        return StderivSeeding::Unknown;
    const long long fileBytes = detail::sizeOf(fil);
    if (fileBytes < 0) return StderivSeeding::Unknown;
    const std::size_t frameElems = static_cast<std::size_t>(spec.totalNbChannels);
    const std::int64_t totalSamples = static_cast<std::int64_t>(fileBytes)
        / static_cast<std::int64_t>(frameElems * sizeof(std::int16_t));

    std::vector<std::int16_t> frames(2 * frameElems);
    std::vector<std::int16_t> prevRow(static_cast<std::size_t>(spec.nChan)),
                              curRow(static_cast<std::size_t>(spec.nChan)),
                              sdPrev(static_cast<std::size_t>(spec.nChan)),
                              sdCur(static_cast<std::size_t>(spec.nChan));
    long votesPrev = 0, votesZero = 0;
    int checked = 0;
    for (std::int64_t i = 0; i < nSpikes && checked < maxCheck; ++i) {
        const std::int64_t ws =
            static_cast<std::int64_t>(timestampAt(i)) - spec.peakSample;
        if (ws < 1 || ws >= totalSamples) continue;
        const long long off = static_cast<long long>(ws - 1)
            * static_cast<long long>(frameElems) * static_cast<long long>(sizeof(std::int16_t));
        if (detail::seek64(fil, off) != 0) continue;
        if (std::fread(frames.data(), sizeof(std::int16_t), frames.size(), fil)
            != frames.size()) continue;
        for (int ci = 0; ci < spec.nChan; ++ci) {
            prevRow[static_cast<std::size_t>(ci)] = frames[static_cast<std::size_t>(spec.groupChannels[ci])];
            curRow[static_cast<std::size_t>(ci)]  =
                frames[frameElems + static_cast<std::size_t>(spec.groupChannels[ci])];
        }
        stderivSeed(spec.order, prevRow.data(), spec.nChan, sdPrev.data(),
                    spec.partner, spec.setOff, spec.setMem, spec.expr);
        stderivSeed(spec.order, curRow.data(),  spec.nChan, sdCur.data(),
                    spec.partner, spec.setOff, spec.setMem, spec.expr);
        for (int ci = 0; ci < spec.nChan; ++ci) {
            const std::int16_t zero   = sdCur[static_cast<std::size_t>(ci)];
            const std::int16_t seeded = clampToInt16(
                static_cast<double>(sdCur[static_cast<std::size_t>(ci)])
              - static_cast<double>(sdPrev[static_cast<std::size_t>(ci)]));
            if (zero == seeded) continue;
            const std::int16_t stored = static_cast<std::int16_t>(storedSample0At(i, ci));
            if (stored == seeded)    ++votesPrev;
            else if (stored == zero) ++votesZero;
        }
        ++checked;
    }
    const long votes = votesPrev + votesZero;
    if (votes < 8) return StderivSeeding::Unknown;
    if (votesPrev * 10 >= votes * 9) return StderivSeeding::Previous;
    if (votesZero * 10 >= votes * 9) return StderivSeeding::Zero;
    return StderivSeeding::Unknown;
}

}  // namespace core
}  // namespace neurosuite

#endif  // NEUROSUITE_CORE_SPIKE_EXTRACT_HPP
