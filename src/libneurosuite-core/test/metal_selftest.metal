// Kernels for core_test_metal (metal_selftest.cpp), embedded with ns_metal_embed.
#include <metal_stdlib>
using namespace metal;

// out[i] = in[i] * 3 + 7 (exact integer arithmetic, compared bit for bit).
kernel void affineU32(device const uint* in  [[buffer(0)]],
                      device uint*       out [[buffer(1)]],
                      constant uint&     n   [[buffer(2)]],
                      uint i [[thread_position_in_grid]])
{
    if (i < n) out[i] = in[i] * 3u + 7u;
}

// Per-group sum through threadgroup memory and a barrier, plus one atomic
// add per group into a global total: the building blocks of the ports.
kernel void groupSum(device const uint* in    [[buffer(0)]],
                     device atomic_uint* total [[buffer(1)]],
                     constant uint&     n     [[buffer(2)]],
                     threadgroup uint*  part  [[threadgroup(0)]],
                     uint i    [[thread_position_in_grid]],
                     uint lid  [[thread_position_in_threadgroup]],
                     uint size [[threads_per_threadgroup]])
{
    part[lid] = i < n ? in[i] : 0u;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint stride = size / 2; stride > 0; stride /= 2) {
        if (lid < stride) part[lid] += part[lid + stride];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (lid == 0) atomic_fetch_add_explicit(total, part[0], memory_order_relaxed);
}
