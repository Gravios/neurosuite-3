/***************************************************************************
 * groupingassistant_metal.cpp
 *
 * Apple GPU (Metal) back end of the Grouping Assistant error matrix
 * (doc/design/metal-backend.md, Phase 3): the FP32 path of
 * groupingassistant_cuda.cu's cuda_compute_error_matrix, with kernels in
 * groupingassistant.metal.  Apple GPUs have no double, so the FP64 option,
 * and the full-posterior matrix, stay on the CPU (both return non-zero here
 * and the caller falls back).  Spikes are processed in chunks of positions,
 * so the float posteriors need one chunk's buffer, not nbSpikes x nbClusters.
 ***************************************************************************/
#ifdef USE_METAL

#include "groupingassistant_gpu.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern const char groupingassistant_kernels[];   // groupingassistant.metal

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

namespace {

constexpr int kMaxDim = 64;   // as in groupingassistant.metal

// Same layout as GaParams in groupingassistant.metal.
struct GaParams {
    std::uint32_t nbClusters, nbDim, chunkStart, chunkLen, cluster1Col;
};

MTL::ComputePipelineState* kernelNamed(MetalContext* ctx, const char* name)
{
    std::string err;
    MTL::ComputePipelineState* p =
        ctx->pipeline("groupingassistant", groupingassistant_kernels, name, &err);
    if (!p) std::fprintf(stderr, "[klusters] grouping Metal: %s\n", err.c_str());
    return p;
}

template <class T>
MTL::Buffer* bufferOf(MetalContext* ctx, const std::vector<T>& v)
{
    MTL::Buffer* b = ctx->newSharedBuffer(std::max<std::size_t>(1, v.size()) * sizeof(T));
    if (b && !v.empty()) std::memcpy(b->contents(), v.data(), v.size() * sizeof(T));
    return b;
}

} // namespace

extern "C" {

int metal_device_available()
{
    MetalContext* ctx = MetalContext::instance();
    return ctx && kernelNamed(ctx, "mahalLogPost") ? 1 : 0;
}

// chunkBytes bounds the float posterior buffer of one chunk (tests pass a
// small value to exercise the chunking).
int metal_compute_error_matrix_chunked(
    const double* features, const double* choleskyAll, const double* means,
    const double* logTerms, const int* ignoreFlags,
    const int* featRow, const int* first, const int* nb,
    double* errOut,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col, int lowPrecision,
    std::size_t chunkBytes)
{
    if (!lowPrecision) return -1;                  // FP64: the CPU path
    if (nbSpikes <= 0 || nbClusters <= 0 || nbDim <= 0 || nbDim > kMaxDim) return -1;
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return -1;
    MTL::ComputePipelineState* mahal = kernelNamed(ctx, "mahalLogPost");
    MTL::ComputePipelineState* softmax = kernelNamed(ctx, "softmaxRows");
    MTL::ComputePipelineState* aggregate = kernelNamed(ctx, "aggregateRows");
    if (!mahal || !softmax || !aggregate) return -1;

    const std::size_t nS = static_cast<std::size_t>(nbSpikes);
    const std::size_t nC = static_cast<std::size_t>(nbClusters);
    const std::size_t nD = static_cast<std::size_t>(nbDim);
    std::vector<float> fFeat(features, features + nS * nD);
    std::vector<float> fChol(choleskyAll, choleskyAll + nC * nD * nD);
    std::vector<float> fMeans(means, means + nC * nD);
    std::vector<float> fLog(logTerms, logTerms + nC);
    std::vector<int> vIgnore(ignoreFlags, ignoreFlags + nC);
    std::vector<int> vRow(featRow, featRow + nS);
    std::vector<int> vFirst(first, first + nC);
    std::vector<int> vNb(nb, nb + nC);

    const std::size_t chunkLen = std::clamp<std::size_t>(chunkBytes / (nC * sizeof(float)), 1, nS);
    MetalRef<MTL::Buffer> bFeat(bufferOf(ctx, fFeat)), bChol(bufferOf(ctx, fChol)),
        bMeans(bufferOf(ctx, fMeans)), bLog(bufferOf(ctx, fLog)), bIgnore(bufferOf(ctx, vIgnore)),
        bRow(bufferOf(ctx, vRow)), bFirst(bufferOf(ctx, vFirst)), bNb(bufferOf(ctx, vNb));
    MetalRef<MTL::Buffer> bPost(ctx->newSharedBuffer(chunkLen * nC * sizeof(float)));
    MetalRef<MTL::Buffer> bSum(ctx->newSharedBuffer(nC * nC * sizeof(float)));
    if (!bFeat || !bChol || !bMeans || !bLog || !bIgnore || !bRow || !bFirst || !bNb
        || !bPost || !bSum) {
        std::fprintf(stderr, "[klusters] grouping Metal: cannot allocate GPU buffers\n");
        return -1;
    }
    std::memset(bSum->contents(), 0, nC * nC * sizeof(float));

    const std::size_t tgBytes = ((nD * nD + nD) * sizeof(float) + 15) / 16 * 16;
    const std::size_t wMahal = MetalContext::threadgroupWidth(mahal, 256);
    const std::size_t wSoft = MetalContext::threadgroupWidth(softmax, 256);
    const std::size_t wAgg = std::min<std::size_t>(16, MetalContext::threadgroupWidth(aggregate, 256));
    std::string err;
    for (std::size_t c0 = 0; c0 < nS; c0 += chunkLen) {
        const std::size_t len = std::min(chunkLen, nS - c0);
        const GaParams p{static_cast<std::uint32_t>(nC), static_cast<std::uint32_t>(nD),
                         static_cast<std::uint32_t>(c0), static_cast<std::uint32_t>(len),
                         static_cast<std::uint32_t>(cluster1Col)};
        AutoreleaseScope pool;
        MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();   // dispatches run in order

        enc->setComputePipelineState(mahal);
        enc->setBuffer(bFeat.get(), 0, 0);
        enc->setBuffer(bRow.get(), 0, 1);
        enc->setBuffer(bChol.get(), 0, 2);
        enc->setBuffer(bMeans.get(), 0, 3);
        enc->setBuffer(bLog.get(), 0, 4);
        enc->setBuffer(bIgnore.get(), 0, 5);
        enc->setBytes(&p, sizeof p, 6);
        enc->setBuffer(bPost.get(), 0, 7);
        enc->setThreadgroupMemoryLength(tgBytes, 0);
        enc->dispatchThreads(MTL::Size(len, nC, 1), MTL::Size(wMahal, 1, 1));

        enc->setComputePipelineState(softmax);
        enc->setBuffer(bPost.get(), 0, 0);
        enc->setBytes(&p, sizeof p, 1);
        enc->dispatchThreads(MTL::Size(len, 1, 1), MTL::Size(wSoft, 1, 1));

        enc->setComputePipelineState(aggregate);
        enc->setBuffer(bPost.get(), 0, 0);
        enc->setBuffer(bFirst.get(), 0, 1);
        enc->setBuffer(bNb.get(), 0, 2);
        enc->setBytes(&p, sizeof p, 3);
        enc->setBuffer(bSum.get(), 0, 4);
        enc->dispatchThreads(MTL::Size(nC, nC, 1), MTL::Size(wAgg, wAgg, 1));

        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
        if (!MetalContext::succeeded(cmd, &err)) {
            std::fprintf(stderr, "[klusters] grouping Metal: %s\n", err.c_str());
            return -1;
        }
    }

    // errOut[i][j] = mean of cluster i's posteriors under j; 0 when either
    // cluster is ignored or i has no spikes (as cuda_aggregate_kernel_t).
    const auto* sum = static_cast<const float*>(bSum->contents());
    for (std::size_t i = 0; i < nC; ++i)
        for (std::size_t j = 0; j < nC; ++j)
            errOut[i * nC + j] = (vIgnore[i] || vIgnore[j] || vNb[i] <= 0)
                ? 0.0
                : static_cast<double>(sum[i * nC + j] / static_cast<float>(vNb[i]));
    return 0;
}

int metal_compute_error_matrix(
    const double* features, const double* choleskyAll, const double* means,
    const double* logTerms, const int* ignoreFlags,
    const int* featRow, const int* first, const int* nb,
    double* errOut,
    int nbSpikes, int nbClusters, int nbDim, int cluster1Col, int lowPrecision)
{
    return metal_compute_error_matrix_chunked(features, choleskyAll, means, logTerms, ignoreFlags,
                                              featRow, first, nb, errOut, nbSpikes, nbClusters,
                                              nbDim, cluster1Col, lowPrecision,
                                              std::size_t(256) << 20);
}

} // extern "C"

#endif // USE_METAL
