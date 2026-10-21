/***************************************************************************
    process_butterworth_metal.cpp  --  Apple GPU (Metal) path of
                                       process_butterworth

    The same entry points as the CUDA path (process_butterworth_cuda.cu), in
    single precision only (Apple GPUs have no double; a double request runs
    on the CPU).  One thread per (channel, block) unit runs the kernel in
    process_butterworth.metal on the chunk in a shared buffer.  See
    doc/design/metal-backend.md.

    copyright  (C) 2026 neurosuite-3 contributors
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/
#include <neurosuite/gpu/metalcontext.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "butterworth_core.h"

extern const char butterworth_kernels[];   // process_butterworth.metal

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

namespace {

// Same layout as BwParams in process_butterworth.metal.
struct BwMetalParams {
    long long row0, nRows, c0, c1, B, nUnits;
    int nCh, M;
};
static_assert(sizeof(BwMetalParams) == 56, "BwParams layout");

struct BwMetalHandle {
    MetalContext* ctx = nullptr;
    MTL::ComputePipelineState* pipeline = nullptr;
    BwCoefs<float> coefs{};
    int nCh = 0, M = 0;
    long long B = 0;
    MetalRef<MTL::Buffer> in, out, scratch;
};

} // namespace

// A Metal device and the kernel compiles.
bool bwMetalAvailable(bool verbose)
{
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) {
        if (verbose) std::printf("  no Metal device; using the CPU path\n");
        return false;
    }
    std::string err;
    if (!ctx->pipeline("butterworth", butterworth_kernels, "bwFilter", &err)) {
        std::fprintf(stderr, "process_butterworth: Metal: %s; using the CPU path\n", err.c_str());
        return false;
    }
    if (verbose) std::printf("  Metal device: %s\n", ctx->deviceName().c_str());
    return true;
}

// Buffers for chunks of up to maxChunkRows input rows and maxOutRows output
// rows; nullptr (message on stderr) if they cannot be allocated.
void* bwMetalInit(const BwCoefs<float>& coefs, int nCh, long long maxChunkRows,
                  long long maxOutRows, int M, long long B)
{
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return nullptr;
    auto* h = new BwMetalHandle();
    h->ctx = ctx;
    h->pipeline = ctx->pipeline("butterworth", butterworth_kernels, "bwFilter");
    h->coefs = coefs;
    h->nCh = nCh;
    h->M = M;
    h->B = B;
    const long long maxUnits = ((maxOutRows + B - 1) / B) * nCh;
    h->in.reset(ctx->newSharedBuffer(static_cast<std::size_t>(maxChunkRows) * nCh * sizeof(short)));
    h->out.reset(ctx->newSharedBuffer(static_cast<std::size_t>(maxOutRows) * nCh * sizeof(short)));
    h->scratch.reset(ctx->newSharedBuffer(static_cast<std::size_t>(B + 2LL * M) *
                                          static_cast<std::size_t>(maxUnits) * sizeof(float)));
    if (!h->pipeline || !h->in || !h->out || !h->scratch) {
        std::fprintf(stderr, "process_butterworth: Metal: cannot allocate GPU buffers "
                             "(try a smaller -b or -B); using the CPU path\n");
        delete h;
        return nullptr;
    }
    return h;
}

// Filters output rows [c0, c1) of a chunk holding input rows [row0, row0 + rows).
bool bwMetalChunk(void* handle, const short* chunk, long long row0, long long rows,
                  long long nRows, long long c0, long long c1, short* out)
{
    auto* h = static_cast<BwMetalHandle*>(handle);
    std::memcpy(h->in->contents(), chunk, static_cast<std::size_t>(rows) * h->nCh * sizeof(short));
    BwMetalParams p{row0, nRows, c0, c1, h->B, ((c1 - c0 + h->B - 1) / h->B) * h->nCh, h->nCh, h->M};
    std::string err;
    {
        AutoreleaseScope pool;
        MTL::CommandBuffer* cmd = h->ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(h->pipeline);
        enc->setBuffer(h->in.get(), 0, 0);
        enc->setBytes(&h->coefs, sizeof h->coefs, 1);
        enc->setBytes(&p, sizeof p, 2);
        enc->setBuffer(h->scratch.get(), 0, 3);
        enc->setBuffer(h->out.get(), 0, 4);
        enc->dispatchThreads(MTL::Size(static_cast<NS::UInteger>(p.nUnits), 1, 1),
                             MTL::Size(MetalContext::threadgroupWidth(h->pipeline, 128), 1, 1));
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
        if (!MetalContext::succeeded(cmd, &err)) {
            std::fprintf(stderr, "process_butterworth: Metal: %s\n", err.c_str());
            return false;
        }
    }
    std::memcpy(out, h->out->contents(), static_cast<std::size_t>(c1 - c0) * h->nCh * sizeof(short));
    return true;
}

void bwMetalFree(void* handle)
{
    delete static_cast<BwMetalHandle*>(handle);
}
