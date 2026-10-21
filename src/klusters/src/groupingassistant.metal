// Grouping Assistant error matrix — Apple GPU (Metal) kernels, embedded by
// ns_metal_embed and compiled at run time (groupingassistant_metal.cpp).
// The FP32 error-matrix path of groupingassistant_cuda.cu: Mahalanobis
// log-posteriors, a max-stabilised softmax per spike, and the per-cluster
// mean of the posteriors.  Apple GPUs have no double, so this is the FP32
// path only; the FP64 option runs on the CPU.  The spikes are processed in
// chunks of positions (a cluster's spikes are contiguous positions), so the
// posteriors never need more than one chunk's buffer.
#include <metal_stdlib>
using namespace metal;

constant uint kMaxDim = 64;

// Same layout as GaParams in groupingassistant_metal.cpp.
struct GaParams {
    uint nbClusters, nbDim, chunkStart, chunkLen, cluster1Col;
};

// Grid (chunkLen, nbClusters), threadgroups (W, 1): the threads of one
// threadgroup share cluster j, whose Cholesky factor (column-major lower
// triangle) and mean are staged in threadgroup memory.  Writes the
// unnormalised log-posterior -0.5 * (mahal + logTerm), or -1e30 for an
// ignored cluster, at logPost[p * nbClusters + j] for position p of the chunk.
kernel void mahalLogPost(device const float* features  [[buffer(0)]],   // [feature row][nbDim]
                         device const int*   featRow   [[buffer(1)]],   // position -> feature row
                         device const float* chol      [[buffer(2)]],   // [nbClusters][nbDim * nbDim]
                         device const float* means     [[buffer(3)]],   // [nbClusters][nbDim]
                         device const float* logTerms  [[buffer(4)]],
                         device const int*   ignore    [[buffer(5)]],
                         constant GaParams&  p         [[buffer(6)]],
                         device float*       logPost   [[buffer(7)]],   // [chunkLen][nbClusters]
                         threadgroup float*  sh        [[threadgroup(0)]],
                         uint2 gid  [[thread_position_in_grid]],
                         uint  lid  [[thread_index_in_threadgroup]],
                         uint  size [[threads_per_threadgroup]])
{
    const uint j = gid.y;
    const uint dim = p.nbDim;
    const bool ig = ignore[j] != 0;
    threadgroup float* shL  = sh;
    threadgroup float* shMu = sh + dim * dim;
    if (!ig) {
        for (uint t = lid; t < dim * dim; t += size) shL[t] = chol[j * dim * dim + t];
        for (uint t = lid; t < dim; t += size) shMu[t] = means[j * dim + t];
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    const uint pos = gid.x;
    if (pos >= p.chunkLen) return;
    device float* out = logPost + ulong(pos) * p.nbClusters + j;
    if (ig) { *out = -1e30f; return; }

    const int row = featRow[p.chunkStart + pos];
    float b[kMaxDim], x[kMaxDim];
    for (uint d = 0; d < dim; ++d) b[d] = features[ulong(row) * dim + d] - shMu[d];
    float sq = 0.0f;
    for (uint i = 0; i < dim; ++i) {                    // forward substitution L x = b
        float s = b[i];
        for (uint k = 0; k < i; ++k) s -= shL[i + k * dim] * x[k];
        x[i] = s / shL[i + i * dim];
        sq += x[i] * x[i];
    }
    *out = -0.5f * (sq + logTerms[j]);
}

// One thread per position: softmax of its log-posteriors, stabilised by the
// row maximum.  If even the best is below the FP64 exp() underflow threshold
// the spike goes wholly to cluster 1 (noise), as the FP64 path does.
kernel void softmaxRows(device float*      logPost [[buffer(0)]],
                        constant GaParams& p       [[buffer(1)]],
                        uint pos [[thread_position_in_grid]])
{
    if (pos >= p.chunkLen) return;
    device float* row = logPost + ulong(pos) * p.nbClusters;
    float mx = -3.0e38f;
    for (uint c = 0; c < p.nbClusters; ++c) mx = max(mx, row[c]);
    if (mx < -745.0f) {
        for (uint c = 0; c < p.nbClusters; ++c) row[c] = 0.0f;
        row[p.cluster1Col] = 1.0f;
        return;
    }
    float sum = 0.0f;
    for (uint c = 0; c < p.nbClusters; ++c) { const float e = exp(row[c] - mx); row[c] = e; sum += e; }
    const float inv = 1.0f / sum;
    for (uint c = 0; c < p.nbClusters; ++c) row[c] *= inv;
}

// Grid (nbClusters, nbClusters): thread (i, j) adds cluster i's posteriors
// under cluster j, over cluster i's positions inside this chunk, to
// errSum[i * nbClusters + j].  One thread per cell and chunks run in turn,
// so no atomics are needed.
kernel void aggregateRows(device const float* logPost [[buffer(0)]],   // normalised posteriors
                          device const int*   first   [[buffer(1)]],
                          device const int*   nb      [[buffer(2)]],
                          constant GaParams&  p       [[buffer(3)]],
                          device float*       errSum  [[buffer(4)]],
                          uint2 gid [[thread_position_in_grid]])
{
    const uint i = gid.x, j = gid.y;
    if (i >= p.nbClusters || j >= p.nbClusters) return;
    const long lo = max(long(first[i]), long(p.chunkStart));
    const long hi = min(long(first[i]) + long(nb[i]), long(p.chunkStart) + long(p.chunkLen));
    if (lo >= hi) return;
    float s = 0.0f;
    for (long q = lo; q < hi; ++q)
        s += logPost[ulong(q - long(p.chunkStart)) * p.nbClusters + j];
    errSum[ulong(i) * p.nbClusters + j] += s;
}
