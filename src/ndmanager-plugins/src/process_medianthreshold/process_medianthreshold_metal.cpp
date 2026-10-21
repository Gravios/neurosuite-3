/***************************************************************************
    process_medianthreshold_metal.cpp

    Apple GPU (Metal) path of process_medianthreshold: per-channel medians of
    |x| for threshold = 4 * median(|x|) / 0.6745 (Quiroga et al. 2004).
    The first Metal port in neurosuite-3 (doc/design/metal-backend.md,
    Phase 1): it proves the toolchain end to end on a kernel whose result
    can be checked for exact equality with the CPU path.

    The GPU counts every sample into an exact histogram of its channel's
    values (absValueHistogram in process_medianthreshold.metal); the CPU
    then walks each histogram to the middle rank(s).  Unlike the CUDA path
    (process_medianthreshold_cuda.cu, upper middle value only), this gives
    the CPU path's median for even frame counts too, and treats -32768 as
    the CPU path does.

    The interleaved data are copied in frame-aligned chunks into a shared
    buffer (CPU and GPU share memory on Apple silicon; the copy is only
    there because the program's array is not page-aligned for a zero-copy
    buffer).
 ***************************************************************************/
#include "process_medianthreshold_metal.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern const char medianthreshold_kernels[];   // process_medianthreshold.metal

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

namespace {

constexpr std::uint32_t kBins = 32769;   // as in process_medianthreshold.metal

short binValue(std::uint32_t bin)
{
    return bin == 0 ? static_cast<short>(-32768) : static_cast<short>(bin - 1);
}

// The value of rank `rank` (0-based, in ascending order) in a histogram.
short valueAtRank(const std::uint32_t* hist, long long rank)
{
    long long seen = 0;
    for (std::uint32_t b = 0; b < kBins; ++b) {
        seen += hist[b];
        if (seen > rank) return binValue(b);
    }
    return binValue(kBins - 1);   // not reached when the counts add up
}

} // namespace

bool metalChannelMedians(const short* data, long long frames, int nChannels,
                         std::vector<long double>& medians, bool verbose,
                         std::size_t chunkBytes)
{
    medians.clear();
    if (frames < 1 || nChannels < 1) return false;
    // Per-bin counts are 32-bit: one channel may contribute at most 2^32 - 1.
    if (frames > 0xFFFFFFFFLL) return false;

    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return false;

    std::string err;
    MTL::ComputePipelineState* histogram =
        ctx->pipeline("medianthreshold", medianthreshold_kernels, "absValueHistogram", &err);
    if (!histogram) {
        std::fprintf(stderr, "process_medianthreshold: Metal: %s; using the CPU\n", err.c_str());
        return false;
    }

    const std::size_t frameBytes = static_cast<std::size_t>(nChannels) * sizeof(short);
    const long long chunkFrames =
        std::max<long long>(1, static_cast<long long>(chunkBytes / frameBytes));
    const std::size_t histBytes = static_cast<std::size_t>(nChannels) * kBins * sizeof(std::uint32_t);
    MetalRef<MTL::Buffer> hist(ctx->newSharedBuffer(histBytes));
    MetalRef<MTL::Buffer> staging(ctx->newSharedBuffer(static_cast<std::size_t>(
        std::min(chunkFrames, frames)) * frameBytes));
    if (!hist || !staging) {
        std::fprintf(stderr, "process_medianthreshold: Metal: cannot allocate GPU buffers; using the CPU\n");
        return false;
    }
    std::memset(hist->contents(), 0, histBytes);

    if (verbose)
        std::printf("[Metal medianthreshold] %s: %d channels x %lld samples\n",
                    ctx->deviceName().c_str(), nChannels, frames);

    const std::uint32_t channels = static_cast<std::uint32_t>(nChannels);
    const std::size_t width = MetalContext::threadgroupWidth(histogram);
    for (long long f0 = 0; f0 < frames; f0 += chunkFrames) {
        const long long nf = std::min(chunkFrames, frames - f0);
        const std::uint32_t count = static_cast<std::uint32_t>(nf * nChannels);
        std::memcpy(staging->contents(), data + f0 * nChannels,
                    static_cast<std::size_t>(count) * sizeof(short));

        AutoreleaseScope pool;
        MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(histogram);
        enc->setBuffer(staging.get(), 0, 0);
        enc->setBuffer(hist.get(), 0, 1);
        enc->setBytes(&channels, sizeof channels, 2);
        enc->setBytes(&count, sizeof count, 3);
        enc->dispatchThreads(MTL::Size(count, 1, 1), MTL::Size(width, 1, 1));
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();   // the staging buffer is refilled next
        if (!MetalContext::succeeded(cmd, &err)) {
            std::fprintf(stderr, "process_medianthreshold: Metal: %s; using the CPU\n", err.c_str());
            return false;
        }
    }

    // The CPU path's medianOf(): middle value (odd), or the mean of the two
    // middle values computed as (int + int) / 2.0L (even).
    const auto* counts = static_cast<const std::uint32_t*>(hist->contents());
    const long long mid = frames / 2;
    medians.resize(static_cast<std::size_t>(nChannels));
    for (int c = 0; c < nChannels; ++c) {
        const std::uint32_t* h = counts + static_cast<std::size_t>(c) * kBins;
        if (frames % 2 == 1)
            medians[c] = valueAtRank(h, mid);
        else
            medians[c] = (valueAtRank(h, mid - 1) + valueAtRank(h, mid)) / 2.0L;
    }
    return true;
}
