/***************************************************************************
    process_medianfilter_metal.cpp
    Apple GPU (Metal) path of process_medianfilter (doc/design/metal-backend.md,
    Phase 2): filters one chunk laid out as process_medianfilter.cpp lays it
    out for the CPU (frame 0 at `data`, w frames of context either side),
    with the kernel in process_medianfilter.metal.  The output equals the CPU
    path's; test/test_medianfilter.py compares them byte for byte.
 ***************************************************************************/
#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

extern const char medianfilter_kernels[];   // process_medianfilter.metal

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

namespace {

constexpr long long kTile = 512;            // as in process_medianfilter.metal

// Shared buffers, grown as needed and kept for the program's life.
struct Buffers {
    MetalRef<MTL::Buffer> in, out;
    std::size_t inBytes = 0, outBytes = 0;
    bool reported = false;
};
Buffers buffers;

bool ensure(MetalContext* ctx, MetalRef<MTL::Buffer>& buf, std::size_t& have, std::size_t need)
{
    if (need <= have) return true;
    buf.reset(ctx->newSharedBuffer(need));
    have = buf ? need : 0;
    return static_cast<bool>(buf);
}

} // namespace

bool metalMedianFilterChunk(const short* data, short* filtered, long long nFrames,
                            int nChannels, int windowHalfLength, bool verbose)
{
    if (nFrames <= 0) return true;
    if (nFrames > INT_MAX) return false;
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return false;

    std::string err;
    MTL::ComputePipelineState* pipeline =
        ctx->pipeline("medianfilter", medianfilter_kernels, "medianFilter", &err);
    if (!pipeline) {
        std::fprintf(stderr, "process_medianfilter: Metal: %s\n", err.c_str());
        return false;
    }

    // Threads per threadgroup: up to 32 channels by as many tiles as the
    // threadgroup memory (one sorted window of 2w + 1 shorts per thread) and
    // the pipeline allow.
    const std::size_t wLen = 2 * static_cast<std::size_t>(windowHalfLength) + 1;
    const std::size_t memLimit = ctx->device()->maxThreadgroupMemoryLength();
    const std::size_t perThread = wLen * sizeof(short);
    const std::size_t maxThreads = pipeline->maxTotalThreadsPerThreadgroup();
    std::size_t tx = std::min<std::size_t>({32, static_cast<std::size_t>(nChannels),
                                            memLimit / perThread, maxThreads});
    if (tx == 0) {
        std::fprintf(stderr, "process_medianfilter: Metal: window half-length %d does not fit "
                             "in threadgroup memory\n", windowHalfLength);
        return false;
    }
    const std::size_t ty = std::max<std::size_t>(1, std::min(maxThreads / tx, memLimit / (tx * perThread)));
    const std::size_t tgBytes = (tx * ty * perThread + 15) / 16 * 16;   // a multiple of 16
    if (tgBytes > memLimit) return false;

    const std::size_t ctxSamples = static_cast<std::size_t>(windowHalfLength) * nChannels;
    const std::size_t outSamples = static_cast<std::size_t>(nFrames) * nChannels;
    const std::size_t inBytes = (outSamples + 2 * ctxSamples) * sizeof(short);
    const std::size_t outBytes = outSamples * sizeof(short);
    if (!ensure(ctx, buffers.in, buffers.inBytes, inBytes) ||
        !ensure(ctx, buffers.out, buffers.outBytes, outBytes)) {
        std::fprintf(stderr, "process_medianfilter: Metal: cannot allocate GPU buffers\n");
        return false;
    }
    if (verbose && !buffers.reported) {
        std::printf("[Metal medianfilter] %s: threadgroups %zux%zu, %zu B threadgroup memory\n",
                    ctx->deviceName().c_str(), tx, ty, tgBytes);
        buffers.reported = true;
    }

    std::memcpy(buffers.in->contents(), data - ctxSamples, inBytes);
    const int frames = static_cast<int>(nFrames);
    const int channels = nChannels;
    const int w = windowHalfLength;
    const std::size_t tiles = static_cast<std::size_t>((nFrames + kTile - 1) / kTile);
    {
        AutoreleaseScope pool;
        MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(pipeline);
        enc->setBuffer(buffers.in.get(), 0, 0);
        enc->setBuffer(buffers.out.get(), 0, 1);
        enc->setBytes(&frames, sizeof frames, 2);
        enc->setBytes(&channels, sizeof channels, 3);
        enc->setBytes(&w, sizeof w, 4);
        enc->setThreadgroupMemoryLength(tgBytes, 0);
        enc->dispatchThreads(MTL::Size(static_cast<NS::UInteger>(nChannels), tiles, 1),
                             MTL::Size(tx, ty, 1));
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
        if (!MetalContext::succeeded(cmd, &err)) {
            std::fprintf(stderr, "process_medianfilter: Metal: %s\n", err.c_str());
            return false;
        }
    }
    std::memcpy(filtered, buffers.out->contents(), outBytes);
    return true;
}
