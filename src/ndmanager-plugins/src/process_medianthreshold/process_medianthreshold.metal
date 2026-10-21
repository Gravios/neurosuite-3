// process_medianthreshold — Apple GPU (Metal) kernel, embedded in the program
// by ns_metal_embed and compiled at run time (process_medianthreshold_metal.cpp).
#include <metal_stdlib>
using namespace metal;

// Bins per channel: one per value the program's |x| can take.  It takes
// |x| in place in a short, so x = -32768 stays -32768 (the smallest value)
// and every other x becomes 0..32767.  Bin 0 holds -32768 and bin v + 1
// holds v, so bin order is value order and the median can be read off the
// cumulative counts exactly as std::nth_element would find it.
constant uint kBins = 32769;

// One thread per sample of an interleaved chunk (frame-aligned, so sample i
// belongs to channel i % nChannels): count it in its channel's histogram.
kernel void absValueHistogram(device const short* data      [[buffer(0)]],
                              device atomic_uint* hist      [[buffer(1)]],
                              constant uint&      nChannels [[buffer(2)]],
                              constant uint&      count     [[buffer(3)]],
                              uint i [[thread_position_in_grid]])
{
    if (i >= count) return;
    const int v = data[i];
    const uint bin = v < 0 ? 0u : uint(v) + 1u;
    atomic_fetch_add_explicit(&hist[(i % nChannels) * kBins + bin], 1u, memory_order_relaxed);
}
