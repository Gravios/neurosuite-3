// process_butterworth — Apple GPU (Metal) kernel, embedded by ns_metal_embed
// and compiled at run time (process_butterworth_metal.cpp).  Single precision
// only: Apple GPUs have no double.  The same computation as bwFilterBlock in
// butterworth_core.h (the CPU and CUDA paths), written out here because the
// Metal shading language needs address spaces on its pointers:
//
// One thread per (channel, block) unit.  Unit u is channel u % nCh, block
// u / nCh: output rows [s, e) are filtered forward from a zero state M rows
// before s, then backward from a zero state M rows after e, with odd
// reflection at the file edges.
#include <metal_stdlib>
using namespace metal;

#define BW_MAX_SECTIONS 16

// Same layout as BwCoefs<float> in butterworth_core.h.
struct BwCoefs {
    int   nSec;
    float b0[BW_MAX_SECTIONS], b1[BW_MAX_SECTIONS], b2[BW_MAX_SECTIONS];
    float a1[BW_MAX_SECTIONS], a2[BW_MAX_SECTIONS];
};

// Same layout as BwMetalParams in process_butterworth_metal.cpp.
struct BwParams {
    long row0, nRows, c0, c1, B, nUnits;   // chunk holds rows [row0, row0 + ...)
    int  nCh, M;
};

// Row g of channel ch, odd-reflected at the file edges (bwFetch).
static float fetch(device const short* chunk, constant BwParams& p, int ch, long g)
{
    if (g < 0) {
        const float x0 = float(chunk[(0 - p.row0) * p.nCh + ch]);
        const float xr = float(chunk[(-g - p.row0) * p.nCh + ch]);
        return 2.0f * x0 - xr;
    }
    if (g >= p.nRows) {
        const float xn = float(chunk[(p.nRows - 1 - p.row0) * p.nCh + ch]);
        const float xr = float(chunk[(2 * (p.nRows - 1) - g - p.row0) * p.nCh + ch]);
        return 2.0f * xn - xr;
    }
    return float(chunk[(g - p.row0) * p.nCh + ch]);
}

// bwRoundSat: round half away from zero, saturate to int16.
static short roundSat(float y)
{
    float r = y >= 0.0f ? trunc(y + 0.5f) : trunc(y - 0.5f);
    r = clamp(r, -32768.0f, 32767.0f);
    return short(r);
}

kernel void bwFilter(device const short* chunk   [[buffer(0)]],
                     constant BwCoefs&   c       [[buffer(1)]],
                     constant BwParams&  p       [[buffer(2)]],
                     device float*       scratch [[buffer(3)]],   // element i of unit u at i * nUnits + u
                     device short*       out     [[buffer(4)]],   // row r at (r - c0) * nCh + ch
                     uint gid [[thread_position_in_grid]])
{
    const long u = long(gid);
    if (u >= p.nUnits) return;
    const int ch = int(u % p.nCh);
    const long s = p.c0 + (u / p.nCh) * p.B;
    const long e = (s + p.B < p.c1) ? s + p.B : p.c1;
    const long L = (e - s) + 2L * p.M;
    float z1[BW_MAX_SECTIONS], z2[BW_MAX_SECTIONS];

    for (int k = 0; k < c.nSec; ++k) { z1[k] = 0.0f; z2[k] = 0.0f; }
    for (long i = 0; i < L; ++i) {
        float v = fetch(chunk, p, ch, s - p.M + i);
        for (int k = 0; k < c.nSec; ++k) {
            const float y = c.b0[k] * v + z1[k];
            z1[k] = c.b1[k] * v - c.a1[k] * y + z2[k];
            z2[k] = c.b2[k] * v - c.a2[k] * y;
            v = y;
        }
        scratch[i * p.nUnits + u] = v;
    }
    for (int k = 0; k < c.nSec; ++k) { z1[k] = 0.0f; z2[k] = 0.0f; }
    for (long i = L - 1; i >= 0; --i) {
        float v = scratch[i * p.nUnits + u];
        for (int k = 0; k < c.nSec; ++k) {
            const float y = c.b0[k] * v + z1[k];
            z1[k] = c.b1[k] * v - c.a1[k] * y + z2[k];
            z2[k] = c.b2[k] * v - c.a2[k] * y;
            v = y;
        }
        if (i >= p.M && i < p.M + (e - s))
            out[(s + (i - p.M) - p.c0) * p.nCh + ch] = roundSat(v);
    }
}
