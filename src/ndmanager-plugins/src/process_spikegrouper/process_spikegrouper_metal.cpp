/***************************************************************************
    process_spikegrouper_metal.cpp
    Apple GPU (Metal) path of process_spikegrouper (doc/design/metal-backend.md,
    Phase 2): the noise medians and the coincidence counts, the two O(N) and
    O(n^2 E log E) parts; event detection (sequential, refractory) stays on
    the CPU.  Both results are integers the CPU then turns into sigma_n and
    the coincidence matrix exactly as its own path does, so the grouping is
    the same with or without the GPU.
 ***************************************************************************/
#include "process_spikegrouper_metal.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern const char spikegrouper_kernels[];   // process_spikegrouper.metal

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

namespace {

constexpr std::uint32_t kBins = 32769;      // as in process_spikegrouper.metal

MTL::ComputePipelineState* kernelNamed(MetalContext* ctx, const char* name)
{
    std::string err;
    MTL::ComputePipelineState* p = ctx->pipeline("spikegrouper", spikegrouper_kernels, name, &err);
    if (!p) std::fprintf(stderr, "process_spikegrouper: Metal: %s; using the CPU\n", err.c_str());
    return p;
}

bool finish(MTL::CommandBuffer* cmd)
{
    cmd->commit();
    cmd->waitUntilCompleted();
    std::string err;
    if (MetalContext::succeeded(cmd, &err)) return true;
    std::fprintf(stderr, "process_spikegrouper: Metal: %s; using the CPU\n", err.c_str());
    return false;
}

} // namespace

bool metalAbsMedians(const short* data, long nSamples, int nChannels,
                     const int* chs, int nv, std::vector<int>& medians, bool verbose)
{
    if (nSamples < 1 || nv < 1 || nSamples > 0xFFFFFFFFL) return false;
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return false;
    MTL::ComputePipelineState* histogram = kernelNamed(ctx, "absHistogram");
    if (!histogram) return false;

    const std::size_t n = static_cast<std::size_t>(nSamples);
    MetalRef<MTL::Buffer> compact(ctx->newSharedBuffer(n * nv * sizeof(short)));
    MetalRef<MTL::Buffer> hist(ctx->newSharedBuffer(static_cast<std::size_t>(nv) * kBins * sizeof(std::uint32_t)));
    if (!compact || !hist) {
        std::fprintf(stderr, "process_spikegrouper: Metal: cannot allocate GPU buffers; using the CPU\n");
        return false;
    }
    // Channel-major copy of the group's channels (coalesced reads on the GPU).
    auto* dst = static_cast<short*>(compact->contents());
    for (int k = 0; k < nv; ++k)
        for (std::size_t s = 0; s < n; ++s)
            dst[k * n + s] = data[s * nChannels + chs[k]];
    std::memset(hist->contents(), 0, static_cast<std::size_t>(nv) * kBins * sizeof(std::uint32_t));

    if (verbose) std::printf("  Metal noise medians on %s\n", ctx->deviceName().c_str());
    const std::uint32_t count = static_cast<std::uint32_t>(nSamples);
    {
        AutoreleaseScope pool;
        MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(histogram);
        enc->setBuffer(compact.get(), 0, 0);
        enc->setBuffer(hist.get(), 0, 1);
        enc->setBytes(&count, sizeof count, 2);
        enc->dispatchThreads(MTL::Size(count, static_cast<NS::UInteger>(nv), 1),
                             MTL::Size(MetalContext::threadgroupWidth(histogram), 1, 1));
        enc->endEncoding();
        if (!finish(cmd)) return false;
    }

    const auto* h = static_cast<const std::uint32_t*>(hist->contents());
    const long rank = nSamples / 2;
    medians.assign(static_cast<std::size_t>(nv), 0);
    for (int k = 0; k < nv; ++k) {
        long seen = 0;
        for (std::uint32_t b = 0; b < kBins; ++b) {
            seen += h[static_cast<std::size_t>(k) * kBins + b];
            if (seen > rank) { medians[k] = static_cast<int>(b); break; }
        }
    }
    return true;
}

bool metalCoincidenceCounts(const std::vector<std::vector<long>>& events, int window,
                            std::vector<long>& counts)
{
    const int nv = static_cast<int>(events.size());
    if (nv < 1) return false;
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return false;
    MTL::ComputePipelineState* coincidence = kernelNamed(ctx, "coincidenceCounts");
    if (!coincidence) return false;

    std::vector<std::uint32_t> offsets(static_cast<std::size_t>(nv) + 1, 0);
    for (int c = 0; c < nv; ++c) {
        const std::size_t next = offsets[c] + events[c].size();
        if (next > 0xFFFFFFFFu) return false;
        offsets[c + 1] = static_cast<std::uint32_t>(next);
    }
    const std::size_t nEvents = offsets[nv];
    MetalRef<MTL::Buffer> ev(ctx->newSharedBuffer(std::max<std::size_t>(1, nEvents) * sizeof(std::int64_t)));
    MetalRef<MTL::Buffer> off(ctx->newSharedBuffer(offsets.size() * sizeof(std::uint32_t)));
    MetalRef<MTL::Buffer> out(ctx->newSharedBuffer(static_cast<std::size_t>(nv) * nv * sizeof(std::uint32_t)));
    if (!ev || !off || !out) {
        std::fprintf(stderr, "process_spikegrouper: Metal: cannot allocate GPU buffers; using the CPU\n");
        return false;
    }
    auto* e = static_cast<std::int64_t*>(ev->contents());
    for (int c = 0; c < nv; ++c)
        for (std::size_t k = 0; k < events[c].size(); ++k) e[offsets[c] + k] = events[c][k];
    std::memcpy(off->contents(), offsets.data(), offsets.size() * sizeof(std::uint32_t));

    const std::int64_t win = window;
    {
        AutoreleaseScope pool;
        MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(coincidence);
        enc->setBuffer(ev.get(), 0, 0);
        enc->setBuffer(off.get(), 0, 1);
        enc->setBytes(&nv, sizeof nv, 2);
        enc->setBytes(&win, sizeof win, 3);
        enc->setBuffer(out.get(), 0, 4);
        enc->setThreadgroupMemoryLength(16, 0);
        enc->dispatchThreadgroups(MTL::Size(static_cast<NS::UInteger>(nv), static_cast<NS::UInteger>(nv), 1),
                                  MTL::Size(MetalContext::threadgroupWidth(coincidence), 1, 1));
        enc->endEncoding();
        if (!finish(cmd)) return false;
    }
    const auto* c = static_cast<const std::uint32_t*>(out->contents());
    counts.assign(c, c + static_cast<std::size_t>(nv) * nv);
    return true;
}
