// shiftprobe_hip.cpp — HIP kernel for post-split shift-probe refeaturization.
// Mirror of shiftprobe_cuda.cu; see that file for algorithm notes.
// Compiled by hipcc as an OBJECT library (see CMakeLists.txt); linked into
// KlustaKwikExp when USE_HIP is defined.
#ifdef USE_HIP

#include "KK.h"
#include "KK_hip.h"
#include <hip/hip_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#define SP_HIP_CHECK(x)  do {                                                \
    hipError_t _e = (x);                                                     \
    if (_e != hipSuccess) {                                                  \
        fprintf(stderr, "[shiftprobe_hip] %s:%d %s -> %s\n",                 \
                __FILE__, __LINE__, #x, hipGetErrorString(_e));              \
        return false;                                                        \
    }                                                                        \
} while (0)

struct KK::ShiftProbeGpuCtx {
    double*  d_eig       = nullptr;
    double*  d_mean      = nullptr;
    float*   d_dimMin    = nullptr;
    float*   d_dimRange  = nullptr;
    int16_t* d_waveBatch = nullptr;
    int*     d_spikeIdx  = nullptr;
    int*     d_cumShift  = nullptr;
    float*   d_trialFeats = nullptr;
    float*   d_trialTime  = nullptr;
    float*   d_timeCol    = nullptr;

    int16_t* h_waveBatchPinned = nullptr;
    size_t   h_waveBatchCap    = 0;

    int nChan = 0, data2use = 0, nComp = 0, recShift = 0;
    bool isCentered = false;
    int nSamplesPerSpike = 0;
    int nPoints = 0;
    int nMemMax = 0;

    FILE* spkFp = nullptr;
};

__global__ void shiftprobe_project_kernel_hip(
    const int16_t* __restrict__ waveBatch,
    const double*  __restrict__ eigAll,
    const double*  __restrict__ meanAll,
    const float*   __restrict__ dimMin,
    const float*   __restrict__ dimRange,
    const int*     __restrict__ cumShift,
    const float*   __restrict__ timeColIn,
    int maxShiftAbs,
    int nChan, int nSamplesPerSpike,
    int data2use, int nComp, int recShift, int isCentered,
    float sessionSamples, int nMem,
    float* __restrict__ trialFeatsOut,
    float* __restrict__ trialTimeOut)
{
    const int nPCA = nChan * nComp;
    const int mi   = blockIdx.x;
    const int tid  = threadIdx.x;
    if (mi >= nMem) return;

    extern __shared__ int16_t smem_raw[];
    const int waveSamples = nChan * nSamplesPerSpike;
    const int16_t* src = waveBatch + static_cast<size_t>(mi) * waveSamples;
    for (int i = tid; i < waveSamples; i += blockDim.x)
        smem_raw[i] = src[i];
    __syncthreads();

    if (tid >= nPCA) return;
    const int ch = tid / nComp;
    const int k  = tid % nComp;
    const int baseCum = cumShift[mi];

    const size_t candStrideE = static_cast<size_t>(nChan) * nComp * data2use;
    const size_t chStrideE   = static_cast<size_t>(nComp) * data2use;
    const size_t chStrideM   = static_cast<size_t>(data2use);
    const size_t candStrideM = static_cast<size_t>(nChan) * data2use;

    const double* evM = eigAll  + 0 * candStrideE + ch * chStrideE + k * data2use;
    const double* ev0 = eigAll  + 1 * candStrideE + ch * chStrideE + k * data2use;
    const double* evP = eigAll  + 2 * candStrideE + ch * chStrideE + k * data2use;
    const double* muM = meanAll + 0 * candStrideM + ch * chStrideM;
    const double* mu0 = meanAll + 1 * candStrideM + ch * chStrideM;
    const double* muP = meanAll + 2 * candStrideM + ch * chStrideM;

    double accM = 0.0, acc0 = 0.0, accP = 0.0;
    for (int j = 0; j < data2use; ++j) {
        const int s = recShift + j;
        const double rawV = static_cast<double>(smem_raw[s * nChan + ch]);
        const double vM = (isCentered != 0) ? (rawV - muM[j]) : rawV;
        const double v0 = (isCentered != 0) ? (rawV - mu0[j]) : rawV;
        const double vP = (isCentered != 0) ? (rawV - muP[j]) : rawV;
        accM += evM[j] * vM;
        acc0 += ev0[j] * v0;
        accP += evP[j] * vP;
    }

    const float min_ = dimMin  [tid];
    const float rng_ = dimRange[tid];
    const float fM   = (static_cast<float>(accM) - min_) * rng_;
    const float f0   = (static_cast<float>(acc0) - min_) * rng_;
    const float fP   = (static_cast<float>(accP) - min_) * rng_;

    const int cumM = baseCum - 1;
    const int cumP = baseCum + 1;
    const bool okM = (cumM <= maxShiftAbs) && (-cumM <= maxShiftAbs);
    const bool okP = (cumP <= maxShiftAbs) && (-cumP <= maxShiftAbs);

    const size_t ofsM = (static_cast<size_t>(0) * nMem + mi) * nPCA + tid;
    const size_t ofs0 = (static_cast<size_t>(1) * nMem + mi) * nPCA + tid;
    const size_t ofsP = (static_cast<size_t>(2) * nMem + mi) * nPCA + tid;
    trialFeatsOut[ofsM] = okM ? fM : f0;
    trialFeatsOut[ofs0] = f0;
    trialFeatsOut[ofsP] = okP ? fP : f0;

    if (tid == 0) {
        const float rawTsNorm = timeColIn[mi];
        const float ddM = okM ? (-1.0f / sessionSamples) : 0.0f;
        const float ddP = okP ? (+1.0f / sessionSamples) : 0.0f;
        trialTimeOut[0 * nMem + mi] = rawTsNorm + ddM;
        trialTimeOut[1 * nMem + mi] = rawTsNorm;
        trialTimeOut[2 * nMem + mi] = rawTsNorm + ddP;
    }
}

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

    auto _chk = [](hipError_t e, const char* w){
        if (e != hipSuccess) {
            fprintf(stderr, "[shiftprobe_hip] alloc %s: %s\n", w, hipGetErrorString(e));
            return false;
        }
        return true;
    };
    if (!_chk(hipMalloc(&ctx->d_eig,  eigFlat.size()  * sizeof(double)), "d_eig")   ||
        !_chk(hipMalloc(&ctx->d_mean, meanFlat.size() * sizeof(double)), "d_mean")  ||
        !_chk(hipMalloc(&ctx->d_dimMin,   nPCA * sizeof(float)), "d_dimMin")        ||
        !_chk(hipMalloc(&ctx->d_dimRange, nPCA * sizeof(float)), "d_dimRange")      ||
        !_chk(hipMalloc(&ctx->d_waveBatch,
            static_cast<size_t>(ctx->nMemMax) * nChan * nSamplesPerSpike * sizeof(int16_t)), "d_waveBatch") ||
        !_chk(hipMalloc(&ctx->d_spikeIdx, ctx->nMemMax * sizeof(int)),   "d_spikeIdx")  ||
        !_chk(hipMalloc(&ctx->d_cumShift, ctx->nMemMax * sizeof(int)),   "d_cumShift")  ||
        !_chk(hipMalloc(&ctx->d_trialFeats,
            3 * ctx->nMemMax * nPCA * sizeof(float)), "d_trialFeats") ||
        !_chk(hipMalloc(&ctx->d_trialTime,
            3 * ctx->nMemMax * sizeof(float)), "d_trialTime") ||
        !_chk(hipMalloc(&ctx->d_timeCol,  ctx->nMemMax * sizeof(float)), "d_timeCol"))
    {
        gpu_shift_probe_free(ctx); return nullptr;
    }

    hipMemcpy(ctx->d_eig,  eigFlat.data(),  eigFlat.size()*sizeof(double),  hipMemcpyHostToDevice);
    hipMemcpy(ctx->d_mean, meanFlat.data(), meanFlat.size()*sizeof(double), hipMemcpyHostToDevice);

    ctx->h_waveBatchCap = static_cast<size_t>(ctx->nMemMax) * nChan * nSamplesPerSpike;
    if (hipHostMalloc((void**)&ctx->h_waveBatchPinned,
                      ctx->h_waveBatchCap * sizeof(int16_t)) != hipSuccess) {
        gpu_shift_probe_free(ctx); return nullptr;
    }

    ctx->spkFp = fopen(spkPath, "rb");
    if (!ctx->spkFp) { gpu_shift_probe_free(ctx); return nullptr; }
    return ctx;
}

void gpu_shift_probe_free(KK::ShiftProbeGpuCtx* ctx)
{
    if (!ctx) return;
    if (ctx->spkFp) fclose(ctx->spkFp);
    if (ctx->h_waveBatchPinned) hipHostFree(ctx->h_waveBatchPinned);
    if (ctx->d_eig)        hipFree(ctx->d_eig);
    if (ctx->d_mean)       hipFree(ctx->d_mean);
    if (ctx->d_dimMin)     hipFree(ctx->d_dimMin);
    if (ctx->d_dimRange)   hipFree(ctx->d_dimRange);
    if (ctx->d_waveBatch)  hipFree(ctx->d_waveBatch);
    if (ctx->d_spikeIdx)   hipFree(ctx->d_spikeIdx);
    if (ctx->d_cumShift)   hipFree(ctx->d_cumShift);
    if (ctx->d_trialFeats) hipFree(ctx->d_trialFeats);
    if (ctx->d_trialTime)  hipFree(ctx->d_trialTime);
    if (ctx->d_timeCol)    hipFree(ctx->d_timeCol);
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
    if (!ctx || !ctx->spkFp) return false;
    const int nMem = static_cast<int>(globalSpikeIndices.size());
    if (nMem < 1) return false;

    const int nChan            = ctx->nChan;
    const int nSamplesPerSpike = ctx->nSamplesPerSpike;
    const int data2use         = ctx->data2use;
    const int nComp            = ctx->nComp;
    const int nPCA             = nChan * nComp;
    const int waveSamples      = nChan * nSamplesPerSpike;

    if (nMem > ctx->nMemMax) {
        int newCap = ctx->nMemMax;
        while (newCap < nMem) newCap *= 2;
        hipFree(ctx->d_waveBatch);  ctx->d_waveBatch  = nullptr;
        hipFree(ctx->d_spikeIdx);   ctx->d_spikeIdx   = nullptr;
        hipFree(ctx->d_cumShift);   ctx->d_cumShift   = nullptr;
        hipFree(ctx->d_trialFeats); ctx->d_trialFeats = nullptr;
        hipFree(ctx->d_trialTime);  ctx->d_trialTime  = nullptr;
        hipFree(ctx->d_timeCol);    ctx->d_timeCol    = nullptr;
        hipHostFree(ctx->h_waveBatchPinned); ctx->h_waveBatchPinned = nullptr;

        SP_HIP_CHECK(hipMalloc(&ctx->d_waveBatch,
            static_cast<size_t>(newCap) * waveSamples * sizeof(int16_t)));
        SP_HIP_CHECK(hipMalloc(&ctx->d_spikeIdx, newCap * sizeof(int)));
        SP_HIP_CHECK(hipMalloc(&ctx->d_cumShift, newCap * sizeof(int)));
        SP_HIP_CHECK(hipMalloc(&ctx->d_trialFeats,
            3 * static_cast<size_t>(newCap) * nPCA * sizeof(float)));
        SP_HIP_CHECK(hipMalloc(&ctx->d_trialTime,
            3 * static_cast<size_t>(newCap) * sizeof(float)));
        SP_HIP_CHECK(hipMalloc(&ctx->d_timeCol, newCap * sizeof(float)));

        ctx->h_waveBatchCap = static_cast<size_t>(newCap) * waveSamples;
        if (hipHostMalloc((void**)&ctx->h_waveBatchPinned,
                          ctx->h_waveBatchCap * sizeof(int16_t)) != hipSuccess)
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
                   static_cast<off_t>(p) * waveSamples * sizeof(int16_t),
                   SEEK_SET) != 0) { ++nSkipped; continue; }
        int16_t* dst = ctx->h_waveBatchPinned + static_cast<size_t>(mi)*waveSamples;
        if (fread(dst, sizeof(int16_t), waveSamples, ctx->spkFp)
                != static_cast<size_t>(waveSamples)) { ++nSkipped; continue; }
        localCum[mi] = cumShift[p];
        localTs[mi]  = timeCol[p * nDims];
    }
    if (nSkipped == nMem) return false;

    SP_HIP_CHECK(hipMemcpy(ctx->d_waveBatch, ctx->h_waveBatchPinned,
        static_cast<size_t>(nMem) * waveSamples * sizeof(int16_t),
        hipMemcpyHostToDevice));
    SP_HIP_CHECK(hipMemcpy(ctx->d_cumShift, localCum.data(),
        nMem * sizeof(int), hipMemcpyHostToDevice));
    SP_HIP_CHECK(hipMemcpy(ctx->d_timeCol,  localTs.data(),
        nMem * sizeof(float), hipMemcpyHostToDevice));
    SP_HIP_CHECK(hipMemcpy(ctx->d_dimMin,   dimMin.data(),
        nPCA * sizeof(float), hipMemcpyHostToDevice));
    SP_HIP_CHECK(hipMemcpy(ctx->d_dimRange, dimRange.data(),
        nPCA * sizeof(float), hipMemcpyHostToDevice));

    const int nThreads = ((nPCA + 63) / 64) * 64;   // wavefront-aligned on AMD
    const size_t smemBytes = static_cast<size_t>(nChan)*nSamplesPerSpike*sizeof(int16_t);
    if (smemBytes > 64u * 1024u) {   // AMD has 64 KB LDS/block
        fprintf(stderr, "[shiftprobe_hip] smem=%zu too large\n", smemBytes);
        return false;
    }

    hipLaunchKernelGGL(shiftprobe_project_kernel_hip,
        dim3(nMem), dim3(nThreads), smemBytes, 0,
        ctx->d_waveBatch, ctx->d_eig, ctx->d_mean,
        ctx->d_dimMin, ctx->d_dimRange,
        ctx->d_cumShift, ctx->d_timeCol,
        maxShiftAbs, nChan, nSamplesPerSpike,
        data2use, nComp, ctx->recShift, ctx->isCentered ? 1 : 0,
        sessionSamples, nMem,
        ctx->d_trialFeats, ctx->d_trialTime);

    SP_HIP_CHECK(hipGetLastError());

    SP_HIP_CHECK(hipMemcpy(trialFeatsOut, ctx->d_trialFeats,
        3 * static_cast<size_t>(nMem) * nPCA * sizeof(float),
        hipMemcpyDeviceToHost));
    SP_HIP_CHECK(hipMemcpy(trialTimeOut, ctx->d_trialTime,
        3 * static_cast<size_t>(nMem) * sizeof(float),
        hipMemcpyDeviceToHost));
    return true;
}

#endif // USE_HIP
