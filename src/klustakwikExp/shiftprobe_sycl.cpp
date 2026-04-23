// shiftprobe_sycl.cpp — SYCL kernel for post-split shift-probe refeaturization.
// Mirror of shiftprobe_cuda.cu; see that file for algorithm notes.
#ifdef USE_SYCL

#include "KK.h"
#include "KK_sycl.h"
#include <sycl/sycl.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

struct KK::ShiftProbeGpuCtx {
    sycl::queue* q           = nullptr;   // borrowed pointer to gpu->q

    double*  d_eig           = nullptr;
    double*  d_mean          = nullptr;
    float*   d_dimMin        = nullptr;
    float*   d_dimRange      = nullptr;
    int16_t* d_waveBatch     = nullptr;
    int*     d_cumShift      = nullptr;
    float*   d_trialFeats    = nullptr;
    float*   d_trialTime     = nullptr;
    float*   d_timeCol       = nullptr;

    int16_t* h_waveBatchPinned = nullptr;   // shared USM for DMA-staged reads
    size_t   h_waveBatchCap    = 0;

    int nChan = 0, data2use = 0, nComp = 0, recShift = 0;
    bool isCentered = false;
    int nSamplesPerSpike = 0;
    int nPoints = 0;
    int nMemMax = 0;

    FILE* spkFp = nullptr;
};

void gpu_shift_probe_free(KK::ShiftProbeGpuCtx* ctx);

KK::ShiftProbeGpuCtx* gpu_shift_probe_init(
    KK_GPU* base, const KK::ShiftProbePcaBasis& basis,
    int nChan, int nSamplesPerSpike, int nPoints, const char* spkPath)
{
    if (!base || !basis.valid() || nPoints <= 0 || !spkPath) return nullptr;

    const int data2use = basis.data2use;
    const int nComp    = basis.nComp;
    const int nChanB   = basis.nChan;
    const size_t evPerCand = static_cast<size_t>(nChanB) * nComp * data2use;
    const size_t muPerCand = static_cast<size_t>(nChanB) * data2use;
    const size_t nPCA      = static_cast<size_t>(nChanB) * nComp;

    auto* ctx = new KK::ShiftProbeGpuCtx();
    ctx->q                = &base->q;
    ctx->nChan            = nChanB;
    ctx->data2use         = data2use;
    ctx->nComp            = nComp;
    ctx->recShift         = basis.recShift;
    ctx->isCentered       = basis.isCentered;
    ctx->nSamplesPerSpike = nSamplesPerSpike;
    ctx->nPoints          = nPoints;
    ctx->nMemMax          = 4096;

    std::vector<double> eigFlat (3 * evPerCand, 0.0);
    std::vector<double> meanFlat(3 * muPerCand, 0.0);
    for (int cand = 0; cand < 3; ++cand)
        for (int ch = 0; ch < nChanB; ++ch) {
            std::memcpy(
                eigFlat.data() + cand*evPerCand + ch*nComp*data2use,
                basis.eigvecShifted[cand][ch].data(),
                nComp*data2use*sizeof(double));
            std::memcpy(
                meanFlat.data() + cand*muPerCand + ch*data2use,
                basis.meanShifted[cand][ch].data(),
                data2use*sizeof(double));
        }

    auto& q = *ctx->q;
    ctx->d_eig        = sycl::malloc_device<double>(eigFlat.size(),  q);
    ctx->d_mean       = sycl::malloc_device<double>(meanFlat.size(), q);
    ctx->d_dimMin     = sycl::malloc_device<float> (nPCA,            q);
    ctx->d_dimRange   = sycl::malloc_device<float> (nPCA,            q);
    ctx->d_waveBatch  = sycl::malloc_device<int16_t>(
        static_cast<size_t>(ctx->nMemMax)*nChan*nSamplesPerSpike, q);
    ctx->d_cumShift   = sycl::malloc_device<int>  (ctx->nMemMax,     q);
    ctx->d_trialFeats = sycl::malloc_device<float>(3*ctx->nMemMax*nPCA, q);
    ctx->d_trialTime  = sycl::malloc_device<float>(3*ctx->nMemMax,     q);
    ctx->d_timeCol    = sycl::malloc_device<float>(ctx->nMemMax,       q);

    if (!ctx->d_eig || !ctx->d_mean || !ctx->d_dimMin || !ctx->d_dimRange ||
        !ctx->d_waveBatch || !ctx->d_cumShift ||
        !ctx->d_trialFeats || !ctx->d_trialTime || !ctx->d_timeCol) {
        gpu_shift_probe_free(ctx); return nullptr;
    }
    q.memcpy(ctx->d_eig,  eigFlat.data(),  eigFlat.size()*sizeof(double)).wait();
    q.memcpy(ctx->d_mean, meanFlat.data(), meanFlat.size()*sizeof(double)).wait();

    ctx->h_waveBatchCap = static_cast<size_t>(ctx->nMemMax)*nChan*nSamplesPerSpike;
    ctx->h_waveBatchPinned = sycl::malloc_host<int16_t>(ctx->h_waveBatchCap, q);
    if (!ctx->h_waveBatchPinned) { gpu_shift_probe_free(ctx); return nullptr; }

    ctx->spkFp = fopen(spkPath, "rb");
    if (!ctx->spkFp) { gpu_shift_probe_free(ctx); return nullptr; }
    return ctx;
}

void gpu_shift_probe_free(KK::ShiftProbeGpuCtx* ctx)
{
    if (!ctx) return;
    if (ctx->spkFp) fclose(ctx->spkFp);
    if (ctx->q) {
        auto& q = *ctx->q;
        if (ctx->h_waveBatchPinned) sycl::free(ctx->h_waveBatchPinned, q);
        if (ctx->d_eig)        sycl::free(ctx->d_eig,        q);
        if (ctx->d_mean)       sycl::free(ctx->d_mean,       q);
        if (ctx->d_dimMin)     sycl::free(ctx->d_dimMin,     q);
        if (ctx->d_dimRange)   sycl::free(ctx->d_dimRange,   q);
        if (ctx->d_waveBatch)  sycl::free(ctx->d_waveBatch,  q);
        if (ctx->d_cumShift)   sycl::free(ctx->d_cumShift,   q);
        if (ctx->d_trialFeats) sycl::free(ctx->d_trialFeats, q);
        if (ctx->d_trialTime)  sycl::free(ctx->d_trialTime,  q);
        if (ctx->d_timeCol)    sycl::free(ctx->d_timeCol,    q);
    }
    delete ctx;
}

bool gpu_shift_probe_project_batch(
    KK::ShiftProbeGpuCtx* ctx,
    const std::vector<int>& globalSpikeIndices,
    const std::vector<int>& cumShift,
    int maxShiftAbs,
    const std::vector<float>& dimMin,
    const std::vector<float>& dimRange,
    float* trialFeatsOut, float* trialTimeOut,
    const float* timeCol, int nDims, float sessionSamples)
{
    if (!ctx || !ctx->q || !ctx->spkFp) return false;
    const int nMem = static_cast<int>(globalSpikeIndices.size());
    if (nMem < 1) return false;

    auto& q = *ctx->q;
    const int nChan            = ctx->nChan;
    const int nSamplesPerSpike = ctx->nSamplesPerSpike;
    const int data2use         = ctx->data2use;
    const int nComp            = ctx->nComp;
    const int nPCA             = nChan * nComp;
    const int waveSamples      = nChan * nSamplesPerSpike;
    const int recShift         = ctx->recShift;
    const int isCenteredI      = ctx->isCentered ? 1 : 0;

    if (nMem > ctx->nMemMax) {
        int newCap = ctx->nMemMax;
        while (newCap < nMem) newCap *= 2;
        sycl::free(ctx->d_waveBatch,  q); ctx->d_waveBatch  = nullptr;
        sycl::free(ctx->d_cumShift,   q); ctx->d_cumShift   = nullptr;
        sycl::free(ctx->d_trialFeats, q); ctx->d_trialFeats = nullptr;
        sycl::free(ctx->d_trialTime,  q); ctx->d_trialTime  = nullptr;
        sycl::free(ctx->d_timeCol,    q); ctx->d_timeCol    = nullptr;
        sycl::free(ctx->h_waveBatchPinned, q); ctx->h_waveBatchPinned = nullptr;

        ctx->d_waveBatch  = sycl::malloc_device<int16_t>(
            static_cast<size_t>(newCap)*waveSamples, q);
        ctx->d_cumShift   = sycl::malloc_device<int>  (newCap, q);
        ctx->d_trialFeats = sycl::malloc_device<float>(3*static_cast<size_t>(newCap)*nPCA, q);
        ctx->d_trialTime  = sycl::malloc_device<float>(3*static_cast<size_t>(newCap), q);
        ctx->d_timeCol    = sycl::malloc_device<float>(newCap, q);
        ctx->h_waveBatchCap = static_cast<size_t>(newCap)*waveSamples;
        ctx->h_waveBatchPinned = sycl::malloc_host<int16_t>(ctx->h_waveBatchCap, q);
        if (!ctx->d_waveBatch || !ctx->d_cumShift || !ctx->d_trialFeats ||
            !ctx->d_trialTime || !ctx->d_timeCol || !ctx->h_waveBatchPinned)
            return false;
        ctx->nMemMax = newCap;
    }

    int nSkipped = 0;
    std::vector<int>   localCum(nMem);
    std::vector<float> localTs (nMem);
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = globalSpikeIndices[mi];
        if (p < 0 || p >= ctx->nPoints) { ++nSkipped; continue; }
        if (fseeko(ctx->spkFp,
                   static_cast<off_t>(p)*waveSamples*sizeof(int16_t),
                   SEEK_SET) != 0) { ++nSkipped; continue; }
        int16_t* dst = ctx->h_waveBatchPinned + static_cast<size_t>(mi)*waveSamples;
        if (fread(dst, sizeof(int16_t), waveSamples, ctx->spkFp)
                != static_cast<size_t>(waveSamples)) { ++nSkipped; continue; }
        localCum[mi] = cumShift[p];
        localTs[mi]  = timeCol[p*nDims];
    }
    if (nSkipped == nMem) return false;

    q.memcpy(ctx->d_waveBatch, ctx->h_waveBatchPinned,
             static_cast<size_t>(nMem)*waveSamples*sizeof(int16_t));
    q.memcpy(ctx->d_cumShift, localCum.data(), nMem*sizeof(int));
    q.memcpy(ctx->d_timeCol,  localTs.data(),  nMem*sizeof(float));
    q.memcpy(ctx->d_dimMin,   dimMin.data(),   nPCA*sizeof(float));
    q.memcpy(ctx->d_dimRange, dimRange.data(), nPCA*sizeof(float));

    // Round work-group to a multiple of 32 (safe on Intel Gen9/Xe).
    const int wgSize  = ((nPCA + 31) / 32) * 32;
    const size_t smemBytes =
        static_cast<size_t>(nChan) * nSamplesPerSpike * sizeof(int16_t);
    if (smemBytes > 32u*1024u) {
        fprintf(stderr, "[shiftprobe_sycl] smem=%zu too large for typical Xe SLM\n",
                smemBytes);
        return false;
    }

    // Capture plain pointers by value — lambdas passed to SYCL kernels must
    // not capture the ctx pointer (host-only object).
    int16_t* d_waveBatch  = ctx->d_waveBatch;
    double*  d_eig        = ctx->d_eig;
    double*  d_mean       = ctx->d_mean;
    float*   d_dimMin     = ctx->d_dimMin;
    float*   d_dimRange   = ctx->d_dimRange;
    int*     d_cumShift   = ctx->d_cumShift;
    float*   d_timeCol    = ctx->d_timeCol;
    float*   d_trialFeats = ctx->d_trialFeats;
    float*   d_trialTime  = ctx->d_trialTime;

    q.submit([&](sycl::handler& h) {
        sycl::local_accessor<int16_t, 1> smem(
            sycl::range<1>(static_cast<size_t>(nChan) * nSamplesPerSpike), h);
        sycl::nd_range<1> ndr(
            sycl::range<1>(static_cast<size_t>(nMem) * wgSize),
            sycl::range<1>(static_cast<size_t>(wgSize)));
        h.parallel_for(ndr, [=](sycl::nd_item<1> it) {
            const int mi  = static_cast<int>(it.get_group(0));
            const int tid = static_cast<int>(it.get_local_id(0));
            if (mi >= nMem) return;

            const int16_t* src = d_waveBatch + static_cast<size_t>(mi)*waveSamples;
            for (int i = tid; i < waveSamples; i += wgSize)
                smem[i] = src[i];
            it.barrier(sycl::access::fence_space::local_space);

            if (tid >= nPCA) return;
            const int ch = tid / nComp;
            const int k  = tid % nComp;
            const int baseCum = d_cumShift[mi];

            const size_t candStrideE = static_cast<size_t>(nChan)*nComp*data2use;
            const size_t chStrideE   = static_cast<size_t>(nComp)*data2use;
            const size_t candStrideM = static_cast<size_t>(nChan)*data2use;
            const size_t chStrideM   = static_cast<size_t>(data2use);

            const double* evM = d_eig  + 0*candStrideE + ch*chStrideE + k*data2use;
            const double* ev0 = d_eig  + 1*candStrideE + ch*chStrideE + k*data2use;
            const double* evP = d_eig  + 2*candStrideE + ch*chStrideE + k*data2use;
            const double* muM = d_mean + 0*candStrideM + ch*chStrideM;
            const double* mu0 = d_mean + 1*candStrideM + ch*chStrideM;
            const double* muP = d_mean + 2*candStrideM + ch*chStrideM;

            double accM = 0.0, acc0 = 0.0, accP = 0.0;
            for (int j = 0; j < data2use; ++j) {
                const int s = recShift + j;
                const double rawV = static_cast<double>(smem[s*nChan + ch]);
                const double vM = (isCenteredI != 0) ? (rawV - muM[j]) : rawV;
                const double v0 = (isCenteredI != 0) ? (rawV - mu0[j]) : rawV;
                const double vP = (isCenteredI != 0) ? (rawV - muP[j]) : rawV;
                accM += evM[j] * vM;
                acc0 += ev0[j] * v0;
                accP += evP[j] * vP;
            }

            const float min_ = d_dimMin  [tid];
            const float rng_ = d_dimRange[tid];
            const float fM = (static_cast<float>(accM) - min_)*rng_;
            const float f0 = (static_cast<float>(acc0) - min_)*rng_;
            const float fP = (static_cast<float>(accP) - min_)*rng_;

            const int cumM = baseCum - 1;
            const int cumP = baseCum + 1;
            const bool okM = (cumM <= maxShiftAbs) && (-cumM <= maxShiftAbs);
            const bool okP = (cumP <= maxShiftAbs) && (-cumP <= maxShiftAbs);

            const size_t ofsM = (static_cast<size_t>(0)*nMem + mi)*nPCA + tid;
            const size_t ofs0 = (static_cast<size_t>(1)*nMem + mi)*nPCA + tid;
            const size_t ofsP = (static_cast<size_t>(2)*nMem + mi)*nPCA + tid;
            d_trialFeats[ofsM] = okM ? fM : f0;
            d_trialFeats[ofs0] = f0;
            d_trialFeats[ofsP] = okP ? fP : f0;

            if (tid == 0) {
                const float rawTsNorm = d_timeCol[mi];
                const float ddM = okM ? (-1.0f / sessionSamples) : 0.0f;
                const float ddP = okP ? (+1.0f / sessionSamples) : 0.0f;
                d_trialTime[0*nMem + mi] = rawTsNorm + ddM;
                d_trialTime[1*nMem + mi] = rawTsNorm;
                d_trialTime[2*nMem + mi] = rawTsNorm + ddP;
            }
        });
    }).wait();

    q.memcpy(trialFeatsOut, ctx->d_trialFeats,
             3*static_cast<size_t>(nMem)*nPCA*sizeof(float)).wait();
    q.memcpy(trialTimeOut,  ctx->d_trialTime,
             3*static_cast<size_t>(nMem)*sizeof(float)).wait();
    return true;
}

#endif // USE_SYCL
