// KK_metal.metal — KiloKlustaKwik's CEM steps on an Apple GPU (Metal).
//
// Embedded by ns_metal_embed and compiled at run time (KK_metal.cpp).  The
// E- and C-steps are KK_cuda.cu's; the M-step, score and deletion loss are
// rebuilt without float atomics, so a run is deterministic, unlike CUDA:
//   kkEStep         one thread per point: Mahalanobis log-probability for
//                   each alive cluster in [ccBegin, ccEnd)  (as the CPU's)
//   kkMeanTile      one threadgroup per tile (a run of one cluster's points,
//                   from the host's class-sorted order): per-dimension sums
//   kkCovTile       one threadgroup per tile: upper-triangle sums of
//                   (x - mean)(x - mean)'
//   kkCStep         one thread per point: best and second-best cluster
//   kkScoreTile     one threadgroup per run of points: sum of LogP at Class
//   kkDeletionDelta one thread per point: LogP[Class2] - LogP[Class]
// The host adds the per-tile sums in a fixed order, in double, and finishes
// the means, covariances, score and deletion losses (KK_metal.cpp).
//
// Layouts (KK_cuda.h's):  data [d * nPoints + p]  (dimension-major)
//                         logP [c * nPoints + p]  (cluster-major)
//                         mean [c * nDims + d],   chol [c * nDims2 + i * nDims + j]
#include <metal_stdlib>
using namespace metal;

// Sizes, as enumerators so that they are constant expressions (array bounds).
enum : uint {
    kMaxDims  = 64,    // KK's kMaxStackDims
    kCovChunk = 32,    // points staged per kkCovTile pass
    kMaxOwned = 33,    // upper-triangle entries per thread: ceil(64*65/2 / 64), >= 64 threads
};

// Same layout as KkParams in KK_metal.cpp.
struct KkParams {
    uint  nPoints, nDims, nDims2, nTri;
    uint  ccBegin, ccEnd;          // kkEStep: alive positions to compute
    uint  nClustersAlive;
    uint  fullStep;
    float distThresh;
    float hugeScore;
};

// A tile: points order[begin, end) all belong to cluster c.
struct KkTile {
    uint c, begin, end, pad;
};

// Threadgroup memory: chol (nDims2) | mean (nDims) | 1/diag (nDims).
// Every thread of the threadgroup passes both barriers of every cluster,
// whether or not it has a point, so the staging is never torn.
kernel void kkEStep(device const float* data     [[buffer(0)]],
                    device const float* mean     [[buffer(1)]],
                    device const float* chol     [[buffer(2)]],
                    device const float* invDiag  [[buffer(3)]],   // [c * nDims + i]
                    device const float* base     [[buffer(4)]],   // [c]: log|L| - log w + D/2 log 2pi
                    device const int*   alive    [[buffer(5)]],
                    device const int*   cls      [[buffer(6)]],
                    device const int*   oldCls   [[buffer(7)]],
                    device float*       logP     [[buffer(8)]],
                    constant KkParams&  q        [[buffer(9)]],
                    threadgroup float*  sh       [[threadgroup(0)]],
                    uint p    [[thread_position_in_grid]],
                    uint lid  [[thread_index_in_threadgroup]],
                    uint size [[threads_per_threadgroup]])
{
    const uint nD = q.nDims, nD2 = q.nDims2, nP = q.nPoints;
    threadgroup float* sChol = sh;
    threadgroup float* sMean = sh + nD2;
    threadgroup float* sInv  = sh + nD2 + nD;
    float v[kMaxDims], root[kMaxDims];

    for (uint cc = q.ccBegin; cc < q.ccEnd; ++cc) {
        const uint c = uint(alive[cc]);
        for (uint i = lid; i < nD2; i += size) sChol[i] = chol[c * nD2 + i];
        for (uint i = lid; i < nD; i += size) {
            sMean[i] = mean[c * nD + i];
            sInv[i]  = invDiag[c * nD + i];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);

        if (p < nP) {
            device float* out = logP + ulong(c) * nP + p;
            bool skip = false;
            if (q.fullStep == 0) {
                const int k = cls[p];
                skip = k == oldCls[p] && *out - logP[ulong(k) * nP + p] > q.distThresh;
            }
            if (!skip) {
                for (uint d = 0; d < nD; ++d) v[d] = data[ulong(d) * nP + p] - sMean[d];
                float m = 0.0f;
                for (uint i = 0; i < nD; ++i) {
                    float s = v[i];
                    for (uint j = 0; j < i; ++j) s -= sChol[i * nD + j] * root[j];
                    root[i] = s * sInv[i];
                }
                for (uint i = 0; i < nD; ++i) m += root[i] * root[i];
                *out = m * 0.5f + base[c];
            }
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
}

// Threadgroup (at least nDims threads): thread t sums dimension t % nDims
// over every lanes-th point of the tile, lane = t / nDims; lane 0 then adds
// the lanes in order.  Output: partial[tile * nDims + d].
kernel void kkMeanTile(device const float*  data    [[buffer(0)]],
                       device const int*    order   [[buffer(1)]],
                       device const KkTile* tiles   [[buffer(2)]],
                       device float*        partial [[buffer(3)]],
                       constant KkParams&   q       [[buffer(4)]],
                       threadgroup float*   sh      [[threadgroup(0)]],
                       uint tile [[threadgroup_position_in_grid]],
                       uint lid  [[thread_index_in_threadgroup]],
                       uint size [[threads_per_threadgroup]])
{
    const uint nD = q.nDims, nP = q.nPoints;
    const uint lanes = size / nD;
    const uint d = lid % nD, lane = lid / nD;
    const KkTile t = tiles[tile];
    if (lane < lanes) {
        float acc = 0.0f;
        for (uint k = t.begin + lane; k < t.end; k += lanes)
            acc += data[ulong(d) * nP + uint(order[k])];
        sh[lane * nD + d] = acc;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (lane == 0) {
        float s = 0.0f;
        for (uint l = 0; l < lanes; ++l) s += sh[l * nD + d];
        partial[ulong(tile) * nD + d] = s;
    }
}

// Threadgroup: kCovChunk centred points at a time are staged in threadgroup
// memory; thread t owns upper-triangle entries t, t + size, ... (row-major
// (i, j), j >= i, listed in triIJ) and sums their products over the tile.
// Output: partial[tile * nTri + entry].
kernel void kkCovTile(device const float*   data    [[buffer(0)]],
                      device const int*     order   [[buffer(1)]],
                      device const KkTile*  tiles   [[buffer(2)]],
                      device const float*   mean    [[buffer(3)]],
                      device const ushort2* triIJ   [[buffer(4)]],
                      device float*         partial [[buffer(5)]],
                      constant KkParams&    q       [[buffer(6)]],
                      threadgroup float*    sh      [[threadgroup(0)]],
                      uint tile [[threadgroup_position_in_grid]],
                      uint lid  [[thread_index_in_threadgroup]],
                      uint size [[threads_per_threadgroup]])
{
    const uint nD = q.nDims, nP = q.nPoints, nTri = q.nTri;
    const KkTile t = tiles[tile];
    float acc[kMaxOwned];
    for (uint r = 0; r < kMaxOwned; ++r) acc[r] = 0.0f;

    for (uint k0 = t.begin; k0 < t.end; k0 += kCovChunk) {
        const uint n = min(uint(kCovChunk), t.end - k0);
        for (uint x = lid; x < kCovChunk * nD; x += size) {
            const uint k = x / nD, d = x % nD;
            sh[x] = k < n ? data[ulong(d) * nP + uint(order[k0 + k])] - mean[t.c * nD + d] : 0.0f;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
        uint r = 0;
        for (uint e = lid; e < nTri; e += size, ++r) {
            const ushort2 ij = triIJ[e];
            float s = acc[r];
            for (uint k = 0; k < n; ++k) s += sh[k * nD + ij.x] * sh[k * nD + ij.y];
            acc[r] = s;
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    uint r = 0;
    for (uint e = lid; e < nTri; e += size, ++r) partial[ulong(tile) * nTri + e] = acc[r];
}

// As KK::CStep's CPU loop: first strictly smaller score wins.
kernel void kkCStep(device const float* logP   [[buffer(0)]],
                    device const int*   alive  [[buffer(1)]],
                    device int*         cls    [[buffer(2)]],
                    device int*         oldCls [[buffer(3)]],
                    device int*         cls2   [[buffer(4)]],
                    constant KkParams&  q      [[buffer(5)]],
                    uint p [[thread_position_in_grid]])
{
    const uint nP = q.nPoints;
    if (p >= nP) return;
    oldCls[p] = cls[p];
    float best = q.hugeScore, second = q.hugeScore;
    int top = 0, next = 0;
    for (uint cc = 0; cc < q.nClustersAlive; ++cc) {
        const int c = alive[cc];
        const float s = logP[ulong(c) * nP + p];
        if (s < best) {
            next = top;  second = best;
            top = c;     best = s;
        } else if (s < second) {
            next = c;    second = s;
        }
    }
    cls[p] = top;
    cls2[p] = next;
}

// Threadgroup width a power of two: a fixed-order tree sum of LogP[Class[p]]
// over the threadgroup's points.  Output: partial[threadgroup].
kernel void kkScoreTile(device const float* logP    [[buffer(0)]],
                        device const int*   cls     [[buffer(1)]],
                        device float*       partial [[buffer(2)]],
                        constant KkParams&  q       [[buffer(3)]],
                        threadgroup float*  sh      [[threadgroup(0)]],
                        uint p    [[thread_position_in_grid]],
                        uint tg   [[threadgroup_position_in_grid]],
                        uint lid  [[thread_index_in_threadgroup]],
                        uint size [[threads_per_threadgroup]])
{
    const uint nP = q.nPoints;
    sh[lid] = p < nP ? logP[ulong(cls[p]) * nP + p] : 0.0f;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint stride = size / 2; stride > 0; stride /= 2) {
        if (lid < stride) sh[lid] += sh[lid + stride];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (lid == 0) partial[tg] = sh[0];
}

kernel void kkDeletionDelta(device const float* logP  [[buffer(0)]],
                            device const int*   cls   [[buffer(1)]],
                            device const int*   cls2  [[buffer(2)]],
                            device float*       delta [[buffer(3)]],
                            constant KkParams&  q     [[buffer(4)]],
                            uint p [[thread_position_in_grid]])
{
    const uint nP = q.nPoints;
    if (p >= nP) return;
    delta[p] = logP[ulong(cls2[p]) * nP + p] - logP[ulong(cls[p]) * nP + p];
}
