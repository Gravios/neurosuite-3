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

struct KK::TimeShiftGpuCtx {
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
    int N = 0;                // basis fan half-width; total candidates = 2N+1
    bool isCentered = false;
    int nSamplesPerSpike = 0;
    int nPoints = 0;
    int nMemMax = 0;

    FILE* spkFp = nullptr;

    int nCand() const { return 2 * N + 1; }
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
    int N_basis,                    // kCand = 2*N_basis + 1
    int nChan, int nSamplesPerSpike,
    int data2use, int nComp, int recShift, int isCentered,
    float sessionSamples, int nMem,
    float* __restrict__ trialFeatsOut,
    float* __restrict__ trialTimeOut)
{
    // Compile-time cap matching KK::kTimeShiftNmax.
    constexpr int SP_N_MAX    = 5;
    constexpr int SP_CAND_MAX = 2 * SP_N_MAX + 1;

    const int nPCA  = nChan * nComp;
    const int kCand = 2 * N_basis + 1;
    const int mi    = blockIdx.x;
    const int tid   = threadIdx.x;
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
    const size_t candStrideM = static_cast<size_t>(nChan) * data2use;
    const size_t chStrideM   = static_cast<size_t>(data2use);

    // Per-thread stack array — compiler unrolls the kCand loop since
    // SP_CAND_MAX is a compile-time constant and the runtime kCand is
    // known loop-invariant.
    double accs[SP_CAND_MAX];
    for (int ci = 0; ci < kCand; ++ci) accs[ci] = 0.0;

    for (int j = 0; j < data2use; ++j) {
        const int s = recShift + j;
        const double rawV = static_cast<double>(smem_raw[s * nChan + ch]);
        for (int ci = 0; ci < kCand; ++ci) {
            const double* ev = eigAll  + ci * candStrideE + ch * chStrideE
                                       + k * data2use;
            const double  mu = (isCentered != 0)
                ? meanAll[ci * candStrideM + ch * chStrideM + j]
                : 0.0;
            accs[ci] += ev[j] * (rawV - mu);
        }
    }

    const float min_ = dimMin  [tid];
    const float rng_ = dimRange[tid];
    const float f0   = (static_cast<float>(accs[N_basis]) - min_) * rng_;

    for (int ci = 0; ci < kCand; ++ci) {
        const int   delta = ci - N_basis;
        const bool  okCi  = (std::abs(baseCum + delta) <= maxShiftAbs);
        const float fv    = (static_cast<float>(accs[ci]) - min_) * rng_;
        const size_t ofs  = (static_cast<size_t>(ci) * nMem + mi) * nPCA + tid;
        trialFeatsOut[ofs] = okCi ? fv : f0;
    }

    if (tid == 0) {
        const float rawTsNorm = timeColIn[mi];
        for (int ci = 0; ci < kCand; ++ci) {
            const int   delta = ci - N_basis;
            const bool  okCi  = (std::abs(baseCum + delta) <= maxShiftAbs);
            const float dd    = okCi
                ? static_cast<float>(delta) / sessionSamples
                : 0.0f;
            trialTimeOut[ci * nMem + mi] = rawTsNorm + dd;
        }
    }
}

void gpu_timeshift_free(KK::TimeShiftGpuCtx* ctx);

KK::TimeShiftGpuCtx* gpu_timeshift_init(
    KK_GPU* base, const KK::TimeShiftBasis& basis,
    int nChan, int nSamplesPerSpike, int nPoints, const char* spkPath)
{
    if (!base || !basis.valid() || nPoints <= 0 || !spkPath) return nullptr;

    const int data2use = basis.data2use;
    const int nComp    = basis.nComp;
    const int nChanB   = basis.nChan;
    const int N        = basis.N;
    const int kCand    = basis.nCand();
    const size_t evPerCand = static_cast<size_t>(nChanB) * nComp * data2use;
    const size_t muPerCand = static_cast<size_t>(nChanB) * data2use;
    const size_t nPCA      = static_cast<size_t>(nChanB) * nComp;

    auto* ctx = new KK::TimeShiftGpuCtx();
    ctx->nChan            = nChanB;
    ctx->data2use         = data2use;
    ctx->nComp            = nComp;
    ctx->recShift         = basis.recShift;
    ctx->isCentered       = basis.isCentered;
    ctx->N                = N;
    ctx->nSamplesPerSpike = nSamplesPerSpike;
    ctx->nPoints          = nPoints;
    ctx->nMemMax          = 4096;

    std::vector<double> eigFlat (static_cast<size_t>(kCand) * evPerCand, 0.0);
    std::vector<double> meanFlat(static_cast<size_t>(kCand) * muPerCand, 0.0);
    for (int cand = 0; cand < kCand; ++cand)
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
            static_cast<size_t>(kCand) * ctx->nMemMax * nPCA * sizeof(float)), "d_trialFeats") ||
        !_chk(hipMalloc(&ctx->d_trialTime,
            static_cast<size_t>(kCand) * ctx->nMemMax * sizeof(float)), "d_trialTime") ||
        !_chk(hipMalloc(&ctx->d_timeCol,  ctx->nMemMax * sizeof(float)), "d_timeCol"))
    {
        gpu_timeshift_free(ctx); return nullptr;
    }

    hipMemcpy(ctx->d_eig,  eigFlat.data(),  eigFlat.size()*sizeof(double),  hipMemcpyHostToDevice);
    hipMemcpy(ctx->d_mean, meanFlat.data(), meanFlat.size()*sizeof(double), hipMemcpyHostToDevice);

    ctx->h_waveBatchCap = static_cast<size_t>(ctx->nMemMax) * nChan * nSamplesPerSpike;
    if (hipHostMalloc((void**)&ctx->h_waveBatchPinned,
                      ctx->h_waveBatchCap * sizeof(int16_t)) != hipSuccess) {
        gpu_timeshift_free(ctx); return nullptr;
    }

    ctx->spkFp = fopen(spkPath, "rb");
    if (!ctx->spkFp) { gpu_timeshift_free(ctx); return nullptr; }
    return ctx;
}

void gpu_timeshift_free(KK::TimeShiftGpuCtx* ctx)
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

bool gpu_timeshift_project_batch(
    KK::TimeShiftGpuCtx* ctx,
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
    const int N                = ctx->N;
    const int kCand            = ctx->nCand();

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
            static_cast<size_t>(kCand) * newCap * nPCA * sizeof(float)));
        SP_HIP_CHECK(hipMalloc(&ctx->d_trialTime,
            static_cast<size_t>(kCand) * newCap * sizeof(float)));
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
        maxShiftAbs, N, nChan, nSamplesPerSpike,
        data2use, nComp, ctx->recShift, ctx->isCentered ? 1 : 0,
        sessionSamples, nMem,
        ctx->d_trialFeats, ctx->d_trialTime);

    SP_HIP_CHECK(hipGetLastError());

    SP_HIP_CHECK(hipMemcpy(trialFeatsOut, ctx->d_trialFeats,
        static_cast<size_t>(kCand) * nMem * nPCA * sizeof(float),
        hipMemcpyDeviceToHost));
    SP_HIP_CHECK(hipMemcpy(trialTimeOut, ctx->d_trialTime,
        static_cast<size_t>(kCand) * nMem * sizeof(float),
        hipMemcpyDeviceToHost));
    return true;
}

#endif // USE_HIP
