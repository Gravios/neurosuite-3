// KK_metal.cpp — KiloKlustaKwik's CEM steps on an Apple GPU (Metal).
//
// Host side of KK_metal.metal; entry points and data contracts as
// KK_cuda.cu's (see KK_metal.h for the differences).  Design:
// doc/design/metal-backend.md, Phase 4.
//
// E-step: per point and alive cluster, the CPU's formula (TriSolve with the
//   reciprocal diagonal, m * 0.5 + log|L| - log w + D/2 log 2pi, the
//   per-cluster constant computed on the host exactly as KK::EStep does),
//   with KK_cuda's DistThresh skip when !FullStep.  Clusters are split over
//   command buffers so none runs long enough to meet the GPU watchdog.
// Dimensions: each E- and M-step works on the nDims / nDims2 KK passes, which
//   can be fewer than were loaded (CEMTwoPhase's spatial phase).
// M-step: the host sorts the points by class (counting sort, O(nPoints)) and
//   cuts each alive cluster's run into tiles of kTile points; the GPU sums
//   each tile (means, then the upper triangle of the centred products); the
//   host adds the tiles in order, in double, and divides by n and n - 1.
//   Only alive clusters' rows of Mean and Cov are written (all of the row,
//   lower triangle zero), as the CPU path does.
// C-step: KK::CStep's loop, one thread per point.
// Score and deletion loss: per-threadgroup / per-point values summed on the
//   host in double.
#ifdef USE_METAL

#include "KK_metal.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern const char kk_cem_kernels[];   // KK_metal.metal, embedded by ns_metal_embed

using neurosuite::gpu::AutoreleaseScope;
using neurosuite::gpu::MetalContext;
using neurosuite::gpu::MetalRef;

namespace {

constexpr int           kMaxDims  = 64;     // KK_metal.metal's kMaxDims
constexpr std::uint32_t kCovChunk = 32;     // KK_metal.metal's kCovChunk
constexpr std::size_t   kMinCovWidth = 64;  // kMaxOwned in KK_metal.metal assumes >= 64 threads
constexpr std::uint32_t kTile     = 4096;   // points per M-step tile
constexpr double        kEStepOpsPerCommand = 4e9;   // multiply-adds per E-step command buffer

// Same layout as KkParams / KkTile in KK_metal.metal.
struct KkParams {
    std::uint32_t nPoints, nDims, nDims2, nTri;
    std::uint32_t ccBegin, ccEnd;
    std::uint32_t nClustersAlive;
    std::uint32_t fullStep;
    float distThresh;
    float hugeScore;
};
struct KkTile {
    std::uint32_t c, begin, end, pad;
};

[[noreturn]] void fail(const char *what, const std::string &err)
{
    std::fprintf(stderr, "Metal error in %s: %s\n", what, err.c_str());
    std::abort();
}

std::size_t floorPow2(std::size_t x)
{
    std::size_t p = 1;
    while (p * 2 <= x) p *= 2;
    return p;
}

// KK runs some steps on fewer dimensions than it loaded (CEMTwoPhase's
// spatial phase leaves out the time column), with Mean rows of nDims and
// Cov/Chol blocks still nDims2 apart.  The buffers are sized for the loaded
// dimensions, so any call may use that many or fewer.
void checkDims(const KK_GPU *gpu, int nDims, int nDims2, const char *where)
{
    if (nDims < 1 || nDims > gpu->nDims || nDims2 < nDims * nDims || nDims2 > gpu->nDims2) {
        std::fprintf(stderr, "%s: nDims %d / nDims2 %d outside the %d / %d allocated\n",
                     where, nDims, nDims2, gpu->nDims, gpu->nDims2);
        std::abort();
    }
}

std::size_t tgBytes(std::size_t floats)
{
    return (floats * sizeof(float) + 15) / 16 * 16;   // a multiple of 16, as Metal requires
}

} // namespace

struct KK_GPU::Impl {
    MetalContext *ctx = nullptr;
    MTL::ComputePipelineState *estep = nullptr, *meanTile = nullptr, *covTile = nullptr,
                              *cstep = nullptr, *score = nullptr, *delta = nullptr;
    std::size_t wEStep = 0, wMean = 0, wCov = 0, wCStep = 0, wScore = 0, wDelta = 0;
    std::size_t maxTiles = 0;

    MetalRef<MTL::Buffer> data, logP, mean, chol, invDiag, base, alive,
                          cls, oldCls, cls2, order, tiles, meanPart, covPart,
                          triIJ, scorePart, deltaBuf;

    template <class T>
    static T *at(const MetalRef<MTL::Buffer> &b) { return static_cast<T *>(b->contents()); }

    // Encodes with `encode`, runs and waits; aborts the run on failure.
    template <class F>
    void run(const char *what, F &&encode)
    {
        AutoreleaseScope pool;
        MTL::CommandBuffer *cmd = ctx->queue()->commandBuffer();
        MTL::ComputeCommandEncoder *enc = cmd->computeCommandEncoder();
        encode(enc);
        enc->endEncoding();
        cmd->commit();
        cmd->waitUntilCompleted();
        std::string err;
        if (!MetalContext::succeeded(cmd, &err)) fail(what, err);
    }
};

bool kk_metal_device_available()
{
    return MetalContext::instance() != nullptr;
}

bool KK_GPU::allocate(int nP, int nD, int nD2, int maxC)
{
    free_all();
    if (nP <= 0 || nD <= 0 || nD > kMaxDims || nD2 != nD * nD || maxC <= 0) {
        std::fprintf(stderr, "KiloKlustaKwik Metal: %d dimensions (at most %d) — using the CPU\n",
                     nD, kMaxDims);
        return false;
    }
    MetalContext *ctx = MetalContext::instance();
    if (!ctx) return false;

    auto im = std::make_unique<Impl>();
    im->ctx = ctx;
    std::string err;
    auto kernel = [&](const char *name) {
        MTL::ComputePipelineState *p = ctx->pipeline("kk_cem", kk_cem_kernels, name, &err);
        if (!p) std::fprintf(stderr, "KiloKlustaKwik Metal: %s — using the CPU\n", err.c_str());
        return p;
    };
    if (!(im->estep = kernel("kkEStep")) || !(im->meanTile = kernel("kkMeanTile"))
        || !(im->covTile = kernel("kkCovTile")) || !(im->cstep = kernel("kkCStep"))
        || !(im->score = kernel("kkScoreTile")) || !(im->delta = kernel("kkDeletionDelta")))
        return false;

    im->wEStep = MetalContext::threadgroupWidth(im->estep, 256);
    im->wMean  = MetalContext::threadgroupWidth(im->meanTile, 256);
    im->wCov   = MetalContext::threadgroupWidth(im->covTile, 256);
    im->wCStep = MetalContext::threadgroupWidth(im->cstep, 256);
    im->wScore = floorPow2(MetalContext::threadgroupWidth(im->score, 256));
    im->wDelta = MetalContext::threadgroupWidth(im->delta, 256);
    const std::size_t tgLimit = ctx->device()->maxThreadgroupMemoryLength();
    if (im->wMean < static_cast<std::size_t>(nD) || im->wCov < kMinCovWidth
        || tgBytes(static_cast<std::size_t>(nD2) + 2 * nD) > tgLimit
        || tgBytes(std::size_t(kCovChunk) * nD) > tgLimit) {
        std::fprintf(stderr, "KiloKlustaKwik Metal: device limits too small for %d dimensions"
                             " — using the CPU\n", nD);
        return false;
    }

    const std::size_t P = static_cast<std::size_t>(nP), D = static_cast<std::size_t>(nD),
                      D2 = static_cast<std::size_t>(nD2), C = static_cast<std::size_t>(maxC);
    const std::size_t nTri = D * (D + 1) / 2;
    im->maxTiles = P / kTile + C + 1;
    const std::size_t nScore = (P + im->wScore - 1) / im->wScore;
    struct Want { MetalRef<MTL::Buffer> *buf; std::size_t bytes; };
    const Want wants[] = {
        {&im->data, sizeof(float) * D * P},        {&im->logP, sizeof(float) * C * P},
        {&im->mean, sizeof(float) * C * D},        {&im->chol, sizeof(float) * C * D2},
        {&im->invDiag, sizeof(float) * C * D},     {&im->base, sizeof(float) * C},
        {&im->alive, sizeof(int) * C},             {&im->cls, sizeof(int) * P},
        {&im->oldCls, sizeof(int) * P},            {&im->cls2, sizeof(int) * P},
        {&im->order, sizeof(int) * P},             {&im->tiles, sizeof(KkTile) * im->maxTiles},
        {&im->meanPart, sizeof(float) * im->maxTiles * D},
        {&im->covPart, sizeof(float) * im->maxTiles * nTri},
        {&im->triIJ, sizeof(std::uint16_t) * 2 * nTri},
        {&im->scorePart, sizeof(float) * nScore},  {&im->deltaBuf, sizeof(float) * P},
    };
    std::size_t total = 0;
    for (const Want &w : wants) {
        total += w.bytes;
        w.buf->reset(ctx->newSharedBuffer(w.bytes));
        if (!*w.buf) {
            std::fprintf(stderr, "KiloKlustaKwik Metal: cannot allocate a %.0f MB buffer"
                                 " — using the CPU\n", static_cast<double>(w.bytes) / 1048576.0);
            return false;
        }
    }
    std::memset(im->logP->contents(), 0, sizeof(float) * C * P);

    impl = im.release();
    nPoints = nP; nDims = nD; nDims2 = nD2; MaxClusters = maxC;
    initialised = true;
    std::fprintf(stderr, "KiloKlustaKwik Metal: %s, %.0f MB of GPU buffers\n",
                 ctx->deviceName().c_str(), static_cast<double>(total) / 1048576.0);
    return true;
}

void KK_GPU::free_all()
{
    delete impl;
    impl = nullptr;
    initialised = false;
}

extern "C" {

void metal_upload_data(KK_GPU *gpu, const float *h_Data_pointMajor)
{
    const std::size_t nP = gpu->nPoints, nD = gpu->nDims;
    float *d = KK_GPU::Impl::at<float>(gpu->impl->data);
    for (std::size_t p = 0; p < nP; ++p)
        for (std::size_t k = 0; k < nD; ++k)
            d[k * nP + p] = h_Data_pointMajor[p * nD + k];
}

void metal_estep(KK_GPU *gpu, const float *h_Mean, const float *h_Weight, const float *h_Chol,
                 const int *h_AliveIndex, const int *h_Class, const int *h_OldClass,
                 float *h_LogP, int nClustersAlive, float DistThresh, int FullStep,
                 double PI, int /*MaxClusters*/, int nDims, int nDims2)
{
    KK_GPU::Impl &im = *gpu->impl;
    checkDims(gpu, nDims, nDims2, "metal_estep");
    const std::size_t nP = gpu->nPoints, nD = static_cast<std::size_t>(nDims),
                      nD2 = static_cast<std::size_t>(nDims2), maxC = gpu->MaxClusters;
    using I = KK_GPU::Impl;
    std::memcpy(im.mean->contents(), h_Mean, sizeof(float) * maxC * nD);
    std::memcpy(im.chol->contents(), h_Chol, sizeof(float) * maxC * nD2);
    std::memcpy(im.alive->contents(), h_AliveIndex, sizeof(int) * nClustersAlive);
    std::memcpy(im.cls->contents(), h_Class, sizeof(int) * nP);
    std::memcpy(im.oldCls->contents(), h_OldClass, sizeof(int) * nP);

    // Per-cluster constants, in float, exactly as KK::EStep's CPU path.
    const float log2piHalf = static_cast<float>(std::log(2.0 * PI) * static_cast<double>(nD) * 0.5);
    float *invDiag = I::at<float>(im.invDiag), *base = I::at<float>(im.base);
    for (int cc = 1; cc < nClustersAlive; ++cc) {
        const std::size_t c = static_cast<std::size_t>(h_AliveIndex[cc]);
        const float *chol = h_Chol + c * nD2;
        float logRootDet = 0.0f;
        for (std::size_t i = 0; i < nD; ++i) {
            logRootDet += std::log(chol[i * nD + i]);
            invDiag[c * nD + i] = 1.0f / chol[i * nD + i];
        }
        base[c] = logRootDet - std::log(h_Weight[c]) + log2piHalf;
    }
    // Cluster 0, the noise cluster: a constant row.
    std::fill_n(I::at<float>(im.logP), nP, -std::log(h_Weight[0]));

    if (nClustersAlive > 1) {
        const double opsPerCluster = static_cast<double>(nP) * static_cast<double>(nD * (nD + 3) / 2);
        const int perCommand = static_cast<int>(std::clamp(kEStepOpsPerCommand / opsPerCluster,
                                                           1.0, static_cast<double>(nClustersAlive)));
        const std::size_t groups = (nP + im.wEStep - 1) / im.wEStep;
        for (int cc0 = 1; cc0 < nClustersAlive; cc0 += perCommand) {
            KkParams q{};
            q.nPoints = static_cast<std::uint32_t>(nP);
            q.nDims = static_cast<std::uint32_t>(nD);
            q.nDims2 = static_cast<std::uint32_t>(nD2);
            q.ccBegin = static_cast<std::uint32_t>(cc0);
            q.ccEnd = static_cast<std::uint32_t>(std::min(cc0 + perCommand, nClustersAlive));
            q.nClustersAlive = static_cast<std::uint32_t>(nClustersAlive);
            q.fullStep = FullStep ? 1u : 0u;
            q.distThresh = DistThresh;
            im.run("the E-step", [&](MTL::ComputeCommandEncoder *enc) {
                enc->setComputePipelineState(im.estep);
                enc->setBuffer(im.data.get(), 0, 0);
                enc->setBuffer(im.mean.get(), 0, 1);
                enc->setBuffer(im.chol.get(), 0, 2);
                enc->setBuffer(im.invDiag.get(), 0, 3);
                enc->setBuffer(im.base.get(), 0, 4);
                enc->setBuffer(im.alive.get(), 0, 5);
                enc->setBuffer(im.cls.get(), 0, 6);
                enc->setBuffer(im.oldCls.get(), 0, 7);
                enc->setBuffer(im.logP.get(), 0, 8);
                enc->setBytes(&q, sizeof q, 9);
                enc->setThreadgroupMemoryLength(tgBytes(nD2 + 2 * nD), 0);
                enc->dispatchThreadgroups(MTL::Size(groups, 1, 1), MTL::Size(im.wEStep, 1, 1));
            });
        }
    }
    if (h_LogP) metal_download_logp(gpu, h_LogP, nClustersAlive, h_AliveIndex);
}

void metal_mstep(KK_GPU *gpu, const int *h_Class, const int *h_AliveIndex,
                 float *h_Mean, float *h_Cov, float * /*h_Weight*/,
                 int nClustersAlive, int /*MaxClusters*/, int /*nPoints*/, int nDims,
                 int nDims2)
{
    KK_GPU::Impl &im = *gpu->impl;
    using I = KK_GPU::Impl;
    checkDims(gpu, nDims, nDims2, "metal_mstep");
    const std::size_t nP = gpu->nPoints, nD = static_cast<std::size_t>(nDims),
                      nD2 = static_cast<std::size_t>(nDims2), maxC = gpu->MaxClusters;
    const std::size_t nTri = nD * (nD + 1) / 2;

    // The upper-triangle entries (i, j), j >= i, for this call's nDims.
    std::uint16_t *ij = I::at<std::uint16_t>(im.triIJ);
    for (std::size_t i = 0, e = 0; i < nD; ++i)
        for (std::size_t j = i; j < nD; ++j, ++e) {
            ij[2 * e] = static_cast<std::uint16_t>(i);
            ij[2 * e + 1] = static_cast<std::uint16_t>(j);
        }

    // Counting sort of the points by class: order[start[c] .. start[c] + n[c]).
    std::vector<std::uint32_t> count(maxC, 0), start(maxC + 1, 0);
    for (std::size_t p = 0; p < nP; ++p) ++count[static_cast<std::size_t>(h_Class[p])];
    for (std::size_t c = 0; c < maxC; ++c) start[c + 1] = start[c] + count[c];
    {
        std::vector<std::uint32_t> next(start.begin(), start.end() - 1);
        int *order = I::at<int>(im.order);
        for (std::size_t p = 0; p < nP; ++p)
            order[next[static_cast<std::size_t>(h_Class[p])]++] = static_cast<int>(p);
    }

    // Tiles of each alive cluster's run, and where each cluster's tiles are.
    KkTile *tiles = I::at<KkTile>(im.tiles);
    std::vector<std::size_t> firstTile(nClustersAlive + 1, 0);
    std::size_t nTiles = 0;
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const std::uint32_t c = static_cast<std::uint32_t>(h_AliveIndex[cc]);
        firstTile[cc] = nTiles;
        for (std::uint32_t b = start[c]; b < start[c + 1]; b += kTile)
            tiles[nTiles++] = KkTile{c, b, std::min(b + kTile, start[c + 1]), 0};
    }
    firstTile[nClustersAlive] = nTiles;

    KkParams q{};
    q.nPoints = static_cast<std::uint32_t>(nP);
    q.nDims = static_cast<std::uint32_t>(nD);
    q.nDims2 = static_cast<std::uint32_t>(nD2);
    q.nTri = static_cast<std::uint32_t>(nTri);
    const std::size_t lanes = im.wMean / nD;
    if (nTiles > 0)
        im.run("the M-step (means)", [&](MTL::ComputeCommandEncoder *enc) {
            enc->setComputePipelineState(im.meanTile);
            enc->setBuffer(im.data.get(), 0, 0);
            enc->setBuffer(im.order.get(), 0, 1);
            enc->setBuffer(im.tiles.get(), 0, 2);
            enc->setBuffer(im.meanPart.get(), 0, 3);
            enc->setBytes(&q, sizeof q, 4);
            enc->setThreadgroupMemoryLength(tgBytes(lanes * nD), 0);
            enc->dispatchThreadgroups(MTL::Size(nTiles, 1, 1), MTL::Size(im.wMean, 1, 1));
        });

    const float *meanPart = I::at<float>(im.meanPart);
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const std::size_t c = static_cast<std::size_t>(h_AliveIndex[cc]);
        const std::uint32_t n = count[c];
        for (std::size_t k = 0; k < nD; ++k) {
            double s = 0.0;
            for (std::size_t t = firstTile[cc]; t < firstTile[cc + 1]; ++t) s += meanPart[t * nD + k];
            h_Mean[c * nD + k] = n > 0 ? static_cast<float>(s / n) : 0.0f;
        }
    }
    std::memcpy(im.mean->contents(), h_Mean, sizeof(float) * maxC * nD);

    if (nTiles > 0)
        im.run("the M-step (covariances)", [&](MTL::ComputeCommandEncoder *enc) {
            enc->setComputePipelineState(im.covTile);
            enc->setBuffer(im.data.get(), 0, 0);
            enc->setBuffer(im.order.get(), 0, 1);
            enc->setBuffer(im.tiles.get(), 0, 2);
            enc->setBuffer(im.mean.get(), 0, 3);
            enc->setBuffer(im.triIJ.get(), 0, 4);
            enc->setBuffer(im.covPart.get(), 0, 5);
            enc->setBytes(&q, sizeof q, 6);
            enc->setThreadgroupMemoryLength(tgBytes(std::size_t(kCovChunk) * nD), 0);
            enc->dispatchThreadgroups(MTL::Size(nTiles, 1, 1), MTL::Size(im.wCov, 1, 1));
        });

    const float *covPart = I::at<float>(im.covPart);
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const std::size_t c = static_cast<std::size_t>(h_AliveIndex[cc]);
        const std::uint32_t n = count[c];
        float *cov = h_Cov + c * nD2;
        std::fill_n(cov, nD2, 0.0f);
        if (n <= 1) continue;
        for (std::size_t e = 0; e < nTri; ++e) {
            double s = 0.0;
            for (std::size_t t = firstTile[cc]; t < firstTile[cc + 1]; ++t) s += covPart[t * nTri + e];
            cov[ij[2 * e] * nD + ij[2 * e + 1]] = static_cast<float>(s / (n - 1));
        }
    }
}

void metal_cstep(KK_GPU *gpu, int *h_Class, int *h_OldClass, int *h_Class2,
                 int nClustersAlive, int /*MaxClusters*/, float HugeScore)
{
    KK_GPU::Impl &im = *gpu->impl;
    const std::size_t nP = gpu->nPoints;
    KkParams q{};
    q.nPoints = static_cast<std::uint32_t>(nP);
    q.nClustersAlive = static_cast<std::uint32_t>(nClustersAlive);
    q.hugeScore = HugeScore;
    im.run("the C-step", [&](MTL::ComputeCommandEncoder *enc) {
        enc->setComputePipelineState(im.cstep);
        enc->setBuffer(im.logP.get(), 0, 0);
        enc->setBuffer(im.alive.get(), 0, 1);
        enc->setBuffer(im.cls.get(), 0, 2);
        enc->setBuffer(im.oldCls.get(), 0, 3);
        enc->setBuffer(im.cls2.get(), 0, 4);
        enc->setBytes(&q, sizeof q, 5);
        enc->dispatchThreadgroups(MTL::Size((nP + im.wCStep - 1) / im.wCStep, 1, 1),
                                  MTL::Size(im.wCStep, 1, 1));
    });
    std::memcpy(h_Class, im.cls->contents(), sizeof(int) * nP);
    std::memcpy(h_OldClass, im.oldCls->contents(), sizeof(int) * nP);
    std::memcpy(h_Class2, im.cls2->contents(), sizeof(int) * nP);
}

void metal_deletion_loss(KK_GPU *gpu, float *h_Loss, int MaxClusters)
{
    KK_GPU::Impl &im = *gpu->impl;
    using I = KK_GPU::Impl;
    const std::size_t nP = gpu->nPoints;
    KkParams q{};
    q.nPoints = static_cast<std::uint32_t>(nP);
    im.run("the deletion loss", [&](MTL::ComputeCommandEncoder *enc) {
        enc->setComputePipelineState(im.delta);
        enc->setBuffer(im.logP.get(), 0, 0);
        enc->setBuffer(im.cls.get(), 0, 1);
        enc->setBuffer(im.cls2.get(), 0, 2);
        enc->setBuffer(im.deltaBuf.get(), 0, 3);
        enc->setBytes(&q, sizeof q, 4);
        enc->dispatchThreadgroups(MTL::Size((nP + im.wDelta - 1) / im.wDelta, 1, 1),
                                  MTL::Size(im.wDelta, 1, 1));
    });
    // As KK_cuda: every entry is written (0 where no point counts); KK
    // restores HugeScore for dead clusters.
    std::vector<double> loss(static_cast<std::size_t>(MaxClusters), 0.0);
    const int *cls = I::at<int>(im.cls);
    const float *delta = I::at<float>(im.deltaBuf);
    for (std::size_t p = 0; p < nP; ++p) loss[static_cast<std::size_t>(cls[p])] += delta[p];
    for (int c = 0; c < MaxClusters; ++c) h_Loss[c] = static_cast<float>(loss[c]);
}

float metal_compute_score(KK_GPU *gpu, float penalty)
{
    KK_GPU::Impl &im = *gpu->impl;
    const std::size_t nP = gpu->nPoints;
    const std::size_t groups = (nP + im.wScore - 1) / im.wScore;
    KkParams q{};
    q.nPoints = static_cast<std::uint32_t>(nP);
    im.run("the score", [&](MTL::ComputeCommandEncoder *enc) {
        enc->setComputePipelineState(im.score);
        enc->setBuffer(im.logP.get(), 0, 0);
        enc->setBuffer(im.cls.get(), 0, 1);
        enc->setBuffer(im.scorePart.get(), 0, 2);
        enc->setBytes(&q, sizeof q, 3);
        enc->setThreadgroupMemoryLength(tgBytes(im.wScore), 0);
        enc->dispatchThreadgroups(MTL::Size(groups, 1, 1), MTL::Size(im.wScore, 1, 1));
    });
    const float *part = KK_GPU::Impl::at<float>(im.scorePart);
    double sum = penalty;
    for (std::size_t g = 0; g < groups; ++g) sum += part[g];
    return static_cast<float>(sum);
}

void metal_download_logp(KK_GPU *gpu, float *h_LogP, int /*nClustersAlive*/,
                         const int * /*h_AliveIndex*/)
{
    std::memcpy(h_LogP, gpu->impl->logP->contents(),
                sizeof(float) * static_cast<std::size_t>(gpu->MaxClusters) * gpu->nPoints);
}

} // extern "C"

#endif // USE_METAL
