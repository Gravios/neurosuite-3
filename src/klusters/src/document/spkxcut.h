#pragma once
// spkxcut.h — a row's window cut from its extended record (.spkx, formats/spkx.md)
// instead of from the recording.
//
// A row Add Residual Spikes made is a window LESS another unit's spike: the
// recording does not hold it, so a tool that moves the row (realign, nudge,
// re-extract on save) cuts it from the row's .spkx record, which is the .spk
// window and `margin` samples either side in the .spk's own space.

#include "neurosuite/core/neurofileio.h"

#include <cstdint>
#include <vector>

namespace spkxcut {

enum class Cut {
    None,       ///< the row has no extended record: cut it from the recording
    Done,       ///< the window is in the output
    Beyond      ///< the row has one, but the time is more than its margin away
};

/** Row @p row's window with its peak at @p t, channel-major (nChan x nSamp) into
 *  @p outCM, from @p x (null: no .spkx).  @p t in the .spk's samples. */
inline Cut cutChannelMajor(const neurofileio::SpkxFile* x, int64_t row, int64_t t, int16_t* outCM)
{
    if (!x || !x->ok) return Cut::None;
    const int64_t k = neurofileio::spkxFind(*x, row);
    if (k < 0) return Cut::None;
    std::vector<int16_t> sm(static_cast<std::size_t>(x->nSamples) * static_cast<std::size_t>(x->nChannels));
    if (!neurofileio::spkxCut(*x, k, t, sm.data())) return Cut::Beyond;
    for (int s = 0; s < x->nSamples; ++s)
        for (int c = 0; c < x->nChannels; ++c)
            outCM[static_cast<std::size_t>(c) * x->nSamples + s] = sm[static_cast<std::size_t>(s) * x->nChannels + c];
    return Cut::Done;
}

/** The same, sample-major (as the .spk holds it). */
inline Cut cutSampleMajor(const neurofileio::SpkxFile* x, int64_t row, int64_t t, int16_t* outSM)
{
    if (!x || !x->ok) return Cut::None;
    const int64_t k = neurofileio::spkxFind(*x, row);
    if (k < 0) return Cut::None;
    return neurofileio::spkxCut(*x, k, t, outSM) ? Cut::Done : Cut::Beyond;
}

} // namespace spkxcut
