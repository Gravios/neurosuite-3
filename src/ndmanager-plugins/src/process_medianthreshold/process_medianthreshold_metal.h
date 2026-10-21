// process_medianthreshold — Apple GPU (Metal) median per channel.
#ifndef PROCESS_MEDIANTHRESHOLD_METAL_H
#define PROCESS_MEDIANTHRESHOLD_METAL_H

#include <cstddef>
#include <vector>

// The median of every channel of interleaved data (frames x nChannels
// samples, already through the program's in-place |x|), with the CPU path's
// definition (medianOf in process_medianthreshold.cpp): the middle value for
// an odd frame count, the mean of the two middle values for an even one.
// The GPU builds an exact per-channel histogram of the values; the medians
// are read off it on the CPU, so the result is identical to the CPU path's.
//
// Returns false, leaving `medians` empty, when there is no Metal device or the
// GPU work fails (a message on stderr for a failure); the caller then uses
// the CPU path.  chunkBytes bounds the staging buffer the data are copied
// through (tests pass a small one to exercise chunking).
bool metalChannelMedians(const short* data, long long frames, int nChannels,
                         std::vector<long double>& medians, bool verbose,
                         std::size_t chunkBytes = std::size_t(64) << 20);

#endif
