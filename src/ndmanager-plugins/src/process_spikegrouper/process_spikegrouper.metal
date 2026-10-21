// process_spikegrouper — Apple GPU (Metal) kernels, embedded by ns_metal_embed
// and compiled at run time (process_spikegrouper_metal.cpp).
#include <metal_stdlib>
using namespace metal;

// Bins per channel: |x| as an int, 0 .. 32768 (as the CPU's abs((int)x)).
constant uint kBins = 32769;

// One thread per (sample, group channel): count |x| in that channel's
// histogram.  `data` is channel-major: channel k's samples at
// data[k * nSamples .. (k + 1) * nSamples).
kernel void absHistogram(device const short* data     [[buffer(0)]],
                         device atomic_uint* hist     [[buffer(1)]],
                         constant uint&      nSamples [[buffer(2)]],
                         uint2 gid [[thread_position_in_grid]])
{
    const uint s = gid.x, k = gid.y;
    if (s >= nSamples) return;
    const int v = data[ulong(k) * nSamples + s];
    atomic_fetch_add_explicit(&hist[k * kBins + uint(abs(v))], 1u, memory_order_relaxed);
}

// One threadgroup per channel pair (i, j), i != j: the number of events of
// channel i with at least one event of channel j within +-window samples.
// Events of channel c: events[offsets[c] .. offsets[c + 1]), sorted.
kernel void coincidenceCounts(device const long* events  [[buffer(0)]],
                              device const uint* offsets [[buffer(1)]],
                              constant int&      nv      [[buffer(2)]],
                              constant long&     window  [[buffer(3)]],
                              device uint*       counts  [[buffer(4)]],   // [i * nv + j]
                              threadgroup atomic_uint* total [[threadgroup(0)]],
                              uint2 tg   [[threadgroup_position_in_grid]],
                              uint  lid  [[thread_index_in_threadgroup]],
                              uint2 tgSize [[threads_per_threadgroup]])   // uint2, as tg must be
{
    const uint size = tgSize.x * tgSize.y;
    const uint i = tg.x, j = tg.y;
    const uint bi = offsets[i], ni = offsets[i + 1] - bi;
    const uint bj = offsets[j], nj = offsets[j + 1] - bj;
    if (i == j || ni == 0 || nj == 0) {                  // uniform across the group
        if (lid == 0) counts[i * uint(nv) + j] = 0;
        return;
    }
    if (lid == 0) atomic_store_explicit(total, 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    uint local = 0;
    for (uint k = lid; k < ni; k += size) {
        const long t = events[bi + k];
        const long lo = t - window, hi = t + window;
        uint a = 0, b = nj;                              // lower_bound(lo)
        while (a < b) {
            const uint mid = (a + b) / 2;
            if (events[bj + mid] < lo) a = mid + 1; else b = mid;
        }
        if (a < nj && events[bj + a] <= hi) ++local;
    }
    atomic_fetch_add_explicit(total, local, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (lid == 0) counts[i * uint(nv) + j] = atomic_load_explicit(total, memory_order_relaxed);
}
