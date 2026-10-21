/***************************************************************************
 * realign_xcorr_metal.cpp
 *
 * Apple GPU (Metal) back end of the spike-realignment cross-correlation
 * (doc/design/metal-backend.md, Phase 3).  The GPU computes the exact
 * integer numerators of every (spike, lag) and each spike's energy
 * (realign_xcorr.metal); the host then runs xcorr_omp_compute's own
 * double-precision scoring and lag choice on them.  Every int16 product and
 * sum here is exact, and the OpenMP path's double sums of the same integers
 * are exact too (below 2^53), so shifts and scores are bit-identical to the
 * OpenMP back end's.
 ***************************************************************************/
#ifdef USE_METAL

#include "realign_xcorr.h"

#include <neurosuite/gpu/metalcontext.h>

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern const char realign_xcorr_kernels[];   // realign_xcorr.metal

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

extern "C" {

int xcorr_metal_available()
{
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return 0;
    std::string err;
    if (!ctx->pipeline("realign_xcorr", realign_xcorr_kernels, "xcorrNumerators", &err)) {
        std::fprintf(stderr, "[xcorr_metal] %s\n", err.c_str());
        return 0;
    }
    return 1;
}

int xcorr_metal_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out)
{
    if (nSpikes <= 0) return 0;
    if (nChannels <= 0 || nSamples <= 0 || maxShift < 0) return -1;
    MetalContext* ctx = MetalContext::instance();
    if (!ctx) return -1;
    std::string err;
    MTL::ComputePipelineState* pipeline =
        ctx->pipeline("realign_xcorr", realign_xcorr_kernels, "xcorrNumerators", &err);
    if (!pipeline) {
        std::fprintf(stderr, "[xcorr_metal] %s\n", err.c_str());
        return -1;
    }

    const int nLags = 2 * maxShift + 1;
    const std::size_t per = static_cast<std::size_t>(nChannels) * nSamples;
    const std::size_t waveBytes = static_cast<std::size_t>(nSpikes) * per * sizeof(int16_t);
    const std::size_t outCount = static_cast<std::size_t>(nSpikes) * (nLags + 1);
    MetalRef<MTL::Buffer> wave(ctx->newSharedBuffer(waveBytes));
    MetalRef<MTL::Buffer> tbuf(ctx->newSharedBuffer(per * sizeof(int16_t)));
    MetalRef<MTL::Buffer> out(ctx->newSharedBuffer(outCount * sizeof(int64_t)));
    if (!wave || !tbuf || !out) return -1;
    std::memcpy(wave->contents(), waveforms, waveBytes);
    std::memcpy(tbuf->contents(), tmpl, per * sizeof(int16_t));

    {
        AutoreleaseScope pool;
        MTL::CommandBuffer* cmd = ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
        enc->setComputePipelineState(pipeline);
        enc->setBuffer(wave.get(), 0, 0);
        enc->setBuffer(tbuf.get(), 0, 1);
        enc->setBytes(&nChannels, sizeof nChannels, 2);
        enc->setBytes(&nSamples, sizeof nSamples, 3);
        enc->setBytes(&maxShift, sizeof maxShift, 4);
        enc->setBytes(&nSpikes, sizeof nSpikes, 5);
        enc->setBuffer(out.get(), 0, 6);
        const std::size_t w = MetalContext::threadgroupWidth(pipeline, 64);
        enc->dispatchThreads(MTL::Size(static_cast<NS::UInteger>(nLags + 1),
                                       static_cast<NS::UInteger>(nSpikes), 1),
                             MTL::Size(w, 1, 1));
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
        if (!MetalContext::succeeded(cmd, &err)) {
            std::fprintf(stderr, "[xcorr_metal] %s\n", err.c_str());
            return -1;
        }
    }

    // xcorr_omp_compute's scoring, on the exact sums.
    double tmplEnergy = 0.0;
    for (std::size_t i = 0; i < per; ++i) {
        const double v = static_cast<double>(tmpl[i]);
        tmplEnergy += v * v;
    }
    const double sqrtTmplEnergy = std::sqrt(tmplEnergy);
    const auto* sums = static_cast<const int64_t*>(out->contents());
    for (int sp = 0; sp < nSpikes; ++sp) {
        const int64_t* row = sums + static_cast<std::size_t>(sp) * (nLags + 1);
        const double denom = sqrtTmplEnergy * std::sqrt(static_cast<double>(row[nLags]));
        if (!(denom > 1e-12)) {
            shifts_out[sp] = 0;
            scores_out[sp] = 0.0f;
            continue;
        }
        float bestScore = -FLT_MAX;
        int bestLag = 0;
        for (int k = 0; k < nLags; ++k) {
            const float score = static_cast<float>(static_cast<double>(row[k]) / denom);
            if (score > bestScore) {
                bestScore = score;
                bestLag = k - maxShift;
            }
        }
        shifts_out[sp] = (bestScore >= minScore) ? bestLag : 0;
        scores_out[sp] = bestScore;
    }
    return 0;
}

} // extern "C"

#endif // USE_METAL
