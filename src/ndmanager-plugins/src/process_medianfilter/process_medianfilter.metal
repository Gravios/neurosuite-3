// process_medianfilter — Apple GPU (Metal) kernel, embedded by ns_metal_embed
// and compiled at run time (process_medianfilter_metal.cpp).  The filter, as
// process_medianfilter.cpp defines it:
//
//   out[t] = x[t] - median(x[t - w .. t + w])   per channel, wrapped to int16.
//
// One thread per (channel, tile of kTile frames), as the CUDA kernel: it
// sorts the window centred on its first frame, then slides it one frame at
// a time.  Each thread keeps its sorted window in threadgroup memory.
#include <metal_stdlib>
using namespace metal;

constant int kTile = 512;

// `data` holds the chunk with w frames of context on either side: frame t
// (-w <= t < nFrames + w) of channel c is data[(t + w) * nChannels + c].
// `out` holds frames 0 .. nFrames - 1.
kernel void medianFilter(device const short* data       [[buffer(0)]],
                         device short*       out        [[buffer(1)]],
                         constant int&       nFrames    [[buffer(2)]],
                         constant int&       nChannels  [[buffer(3)]],
                         constant int&       w          [[buffer(4)]],
                         threadgroup short*  windows    [[threadgroup(0)]],
                         uint2 gid [[thread_position_in_grid]],
                         uint  lid [[thread_index_in_threadgroup]])
{
    const int ch = int(gid.x);
    const int t0 = int(gid.y) * kTile;
    if (ch >= nChannels || t0 >= nFrames) return;
    const int t1 = min(t0 + kTile, nFrames);
    const int wLen = 2 * w + 1;
    const long stride = nChannels;
    threadgroup short* win = windows + lid * uint(wLen);

    // Sorted window centred on t0: frames t0 - w .. t0 + w (insertion sort).
    for (int i = 0; i < wLen; ++i) {
        const short v = data[long(t0 + i) * stride + ch];   // frame t0 - w + i
        int j = i - 1;
        while (j >= 0 && win[j] > v) { win[j + 1] = win[j]; --j; }
        win[j + 1] = v;
    }

    for (int t = t0; t < t1; ++t) {
        if (t > t0) {
            // Slide: frame t - w - 1 leaves, frame t + w enters.
            const short oldV = data[long(t - 1) * stride + ch];
            const short newV = data[long(t + 2 * w) * stride + ch];
            if (newV != oldV) {
                int lo = 0, hi = wLen;                           // lower_bound(oldV)
                while (lo < hi) {
                    const int mid = (lo + hi) / 2;
                    if (win[mid] < oldV) lo = mid + 1; else hi = mid;
                }
                int idx = lo;
                if (newV > oldV) {
                    while (idx + 1 < wLen && win[idx + 1] < newV) { win[idx] = win[idx + 1]; ++idx; }
                } else {
                    while (idx > 0 && win[idx - 1] > newV) { win[idx] = win[idx - 1]; --idx; }
                }
                win[idx] = newV;
            }
        }
        const int x = data[long(t + w) * stride + ch];
        // Wrap into int16 as the CPU's short assignment does.
        out[long(t) * stride + ch] = as_type<short>(ushort(uint(x - int(win[w])) & 0xFFFFu));
    }
}
