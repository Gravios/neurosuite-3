// shiftprobe_cuda.cu — GPU kernel for post-split shift-probe refeaturization.
//
// For each spike in a batch, project its waveform through three pre-shifted
// PCA bases (δ = −1, 0, +1) in a single pass.  Because the eigenvectors taper
// to zero at window edges, circular-shift-by-δ of a spike is mathematically
// equivalent to index-shift-by-(−δ) of the basis row, with zero-padding at
// the tail.  We precompute the three shifted bases once on the host
// (InitShiftProbe, CPU code) and upload them here at init time.
//
// Kernel layout: one block per spike, threads cooperate across the
// (channel × component) product.  Each block reads its spike's waveform
// into shared memory once, then three independent accumulators run in
// registers over data2use samples.
//
// Not compiled when USE_CUDA is undefined.
#ifdef USE_CUDA

#include "KK.h"
#include "KK_cuda.h"
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#define SP_CUDA_CHECK(x)  do {                                                \
    cudaError_t _e = (x);                                                     \
    if (_e != cudaSuccess) {                                                  \
        fprintf(stderr, "[shiftprobe_cuda] %s:%d %s -> %s\n",                 \
                __FILE__, __LINE__, #x, cudaGetErrorString(_e));              \
        return false;                                                         \
    }                                                                         \
} while (0)

// ---------------------------------------------------------------------------
// GPU context — allocated by gpu_shift_probe_init
// ---------------------------------------------------------------------------
struct KK::ShiftProbeGpuCtx {
    // Basis: (2N+1) copies × nChan × data2use*nComp doubles
    double*  d_eig       = nullptr;   // [kCand * nChan * nComp * data2use]
    double*  d_mean      = nullptr;   // [kCand * nChan * data2use]
    // Per-dim normalisation (size = nChan*nComp = nPCA)
    float*   d_dimMin    = nullptr;
    float*   d_dimRange  = nullptr;
    // Waveform batch staged host->device per call
    int16_t* d_waveBatch = nullptr;   // [nMemMax * nChan * nSamplesPerSpike]
    int*     d_spikeIdx  = nullptr;   // [nMemMax]
    int*     d_cumShift  = nullptr;   // [nMemMax]
    float*   d_trialFeats = nullptr;  // [kCand * nMemMax * nPCA]
    float*   d_trialTime  = nullptr;  // [kCand * nMemMax]
    float*   d_timeCol    = nullptr;  // [nMemMax]  (rawTsNorm per spike)

    // Host-side pinned staging for .spk I/O (read here, DMA to GPU)
    int16_t* h_waveBatchPinned = nullptr;
    size_t   h_waveBatchCap    = 0;

    // Dims
    int nChan = 0, data2use = 0, nComp = 0, recShift = 0;
    int N = 0;                // basis fan half-width; total candidates = 2N+1
    bool isCentered = false;
    int nSamplesPerSpike = 0;
    int nPoints = 0;
    int nMemMax = 0;

    // .spk file handle, opened once and reused
    FILE* spkFp = nullptr;

    int nCand() const { return 2 * N + 1; }
};

// ---------------------------------------------------------------------------
// Compile-time upper bound on the basis-fan half-width.  Matches
// KK::kShiftProbeNmax.  Used to size thread-local accumulator arrays.
// ---------------------------------------------------------------------------
#define SP_N_MAX   5
#define SP_CAND_MAX (2 * SP_N_MAX + 1)

// ---------------------------------------------------------------------------
// Kernel: one block per spike, per-block shared-memory waveform, (2N+1)
// accumulators per (channel, component).  Threads iterate over
// (channel * nComp) = nPCA in parallel.
//
// Grid  : (nMem,)
// Block : (nPCA,)   one thread per PCA feature
// Smem  : nChan * nSamplesPerSpike shorts (spike window, coop-loaded)
// ---------------------------------------------------------------------------
__global__ void shiftprobe_project_kernel(
    const int16_t* __restrict__ waveBatch,       // [nMem * nChan * nSamplesPerSpike]
    const double*  __restrict__ eigAll,          // [kCand * nChan * nComp * data2use]
    const double*  __restrict__ meanAll,         // [kCand * nChan * data2use]
    const float*   __restrict__ dimMin,          // [nPCA]
    const float*   __restrict__ dimRange,        // [nPCA]
    const int*     __restrict__ cumShift,        // [nMem]
    const float*   __restrict__ timeColIn,       // [nMem] rawTsNorm per spike
    int            maxShiftAbs,
    int            N_basis,                      // kCand = 2*N_basis + 1
    int            nChan,
    int            nSamplesPerSpike,
    int            data2use,
    int            nComp,
    int            recShift,
    int            isCentered,                   // 0/1
    float          sessionSamples,
    int            nMem,
    float*         __restrict__ trialFeatsOut,   // [kCand * nMem * nPCA]
    float*         __restrict__ trialTimeOut)    // [kCand * nMem]
{
    const int nPCA  = nChan * nComp;
    const int kCand = 2 * N_basis + 1;
    const int mi    = blockIdx.x;       // spike index within batch
    const int tid   = threadIdx.x;      // PCA feature index

    if (mi >= nMem) return;

    extern __shared__ int16_t smem_raw[];   // nChan * nSamplesPerSpike shorts

    // Cooperative load of this spike's waveform into shared memory.
    const int waveSamples = nChan * nSamplesPerSpike;
    const int16_t* src = waveBatch + static_cast<size_t>(mi) * waveSamples;
    for (int i = tid; i < waveSamples; i += blockDim.x)
        smem_raw[i] = src[i];
    __syncthreads();

    if (tid >= nPCA) return;

    const int ch = tid / nComp;
    const int k  = tid % nComp;

    const int baseCum = cumShift[mi];

    // Pre-compute per-candidate basis base pointers for this (ch, k).
    // Stride layout (must match InitShiftProbe's flatten order):
    //   eigAll : [cand][ch][k*data2use + j]
    //   meanAll: [cand][ch][j]
    const size_t candStrideE = static_cast<size_t>(nChan) * nComp * data2use;
    const size_t chStrideE   = static_cast<size_t>(nComp) * data2use;
    const size_t candStrideM = static_cast<size_t>(nChan) * data2use;
    const size_t chStrideM   = static_cast<size_t>(data2use);

    // Per-thread stack arrays sized for the worst case.  Blackwell has 64
    // regs/thread minimum; 11 doubles + 11 pointers easily fits.
    double accs[SP_CAND_MAX];
    for (int ci = 0; ci < kCand; ++ci) accs[ci] = 0.0;

    // Triple-layer inner loop → fanned over kCand candidates.  The compiler
    // unrolls the ci-loop because kCand is loop-invariant and bounded by the
    // compile-time constant SP_CAND_MAX.
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
    // The δ=0 candidate has index N_basis in the fan.
    const float f0   = (static_cast<float>(accs[N_basis]) - min_) * rng_;

    // Write trial features; out-of-range candidates substitute f0 so the
    // host-side variance criterion is unaffected by them.
    for (int ci = 0; ci < kCand; ++ci) {
        const int   delta  = ci - N_basis;
        const bool  okCi   = (std::abs(baseCum + delta) <= maxShiftAbs);
        const float fv     = (static_cast<float>(accs[ci]) - min_) * rng_;
        const size_t ofs   = (static_cast<size_t>(ci) * nMem + mi) * nPCA + tid;
        trialFeatsOut[ofs] = okCi ? fv : f0;
    }

    // Only one thread (tid==0) writes the time rows for this spike.
    if (tid == 0) {
        const float rawTsNorm = timeColIn[mi];
        for (int ci = 0; ci < kCand; ++ci) {
            const int  delta = ci - N_basis;
            const bool okCi  = (std::abs(baseCum + delta) <= maxShiftAbs);
            const float dd   = okCi
                ? static_cast<float>(delta) / sessionSamples
                : 0.0f;
            trialTimeOut[ci * nMem + mi] = rawTsNorm + dd;
        }
    }
}

// ---------------------------------------------------------------------------
// Forward declaration — gpu_shift_probe_init's cleanup paths call _free
// before its definition appears later in this TU.
// ---------------------------------------------------------------------------
void gpu_shift_probe_free(KK::ShiftProbeGpuCtx* ctx);

// ---------------------------------------------------------------------------
// gpu_shift_probe_init — allocate device buffers and upload pre-shifted bases
// ---------------------------------------------------------------------------
KK::ShiftProbeGpuCtx* gpu_shift_probe_init(
    KK_GPU* base, const KK::ShiftProbePcaBasis& basis,
    int nChan, int nSamplesPerSpike, int nPoints, const char* spkPath)
{
    if (!base || !basis.valid() || nPoints <= 0 || !spkPath) return nullptr;

    // Size of each basis flat buffer
    const int data2use = basis.data2use;
    const int nComp    = basis.nComp;
    const int nChanB   = basis.nChan;
    const int N        = basis.N;
    const int kCand    = basis.nCand();
    const size_t evPerCand   = static_cast<size_t>(nChanB) * nComp * data2use;
    const size_t muPerCand   = static_cast<size_t>(nChanB) * data2use;
    const size_t nPCA        = static_cast<size_t>(nChanB) * nComp;

    auto* ctx = new KK::ShiftProbeGpuCtx();
    ctx->nChan            = nChanB;
    ctx->data2use         = data2use;
    ctx->nComp            = nComp;
    ctx->recShift         = basis.recShift;
    ctx->isCentered       = basis.isCentered;
    ctx->N                = N;
    ctx->nSamplesPerSpike = nSamplesPerSpike;
    ctx->nPoints          = nPoints;
    // Initial capacity — grown on demand in project_batch when clusters
    // exceed it.  Start with 4096 spikes (covers most per-cluster cases).
    ctx->nMemMax = 4096;

    // --- Basis upload: flatten [cand][ch][k*data2use+j] into contiguous arrays ---
    std::vector<double> eigFlat (static_cast<size_t>(kCand) * evPerCand, 0.0);
    std::vector<double> meanFlat(static_cast<size_t>(kCand) * muPerCand, 0.0);
    for (int cand = 0; cand < kCand; ++cand) {
        for (int ch = 0; ch < nChanB; ++ch) {
            const auto& ev = basis.eigvecShifted[cand][ch];
            const auto& mu = basis.meanShifted  [cand][ch];
            std::memcpy(
                eigFlat.data()
                    + static_cast<size_t>(cand) * evPerCand
                    + static_cast<size_t>(ch)   * nComp * data2use,
                ev.data(), nComp * data2use * sizeof(double));
            std::memcpy(
                meanFlat.data()
                    + static_cast<size_t>(cand) * muPerCand
                    + static_cast<size_t>(ch)   * data2use,
                mu.data(), data2use * sizeof(double));
        }
    }

    auto _check_alloc = [&](cudaError_t e, const char* what) {
        if (e != cudaSuccess) {
            fprintf(stderr, "[shiftprobe_cuda] alloc %s failed: %s\n",
                    what, cudaGetErrorString(e));
            return false;
        }
        return true;
    };

    if (!_check_alloc(cudaMalloc(&ctx->d_eig,  eigFlat.size()  * sizeof(double)), "d_eig")   ||
        !_check_alloc(cudaMalloc(&ctx->d_mean, meanFlat.size() * sizeof(double)), "d_mean")  ||
        !_check_alloc(cudaMalloc(&ctx->d_dimMin,   nPCA * sizeof(float)), "d_dimMin")        ||
        !_check_alloc(cudaMalloc(&ctx->d_dimRange, nPCA * sizeof(float)), "d_dimRange")      ||
        !_check_alloc(cudaMalloc(&ctx->d_waveBatch,
            static_cast<size_t>(ctx->nMemMax) * nChan * nSamplesPerSpike * sizeof(int16_t)), "d_waveBatch") ||
        !_check_alloc(cudaMalloc(&ctx->d_spikeIdx, ctx->nMemMax * sizeof(int)),   "d_spikeIdx")  ||
        !_check_alloc(cudaMalloc(&ctx->d_cumShift, ctx->nMemMax * sizeof(int)),   "d_cumShift")  ||
        !_check_alloc(cudaMalloc(&ctx->d_trialFeats,
            static_cast<size_t>(kCand) * ctx->nMemMax * nPCA * sizeof(float)), "d_trialFeats") ||
        !_check_alloc(cudaMalloc(&ctx->d_trialTime,
            static_cast<size_t>(kCand) * ctx->nMemMax * sizeof(float)), "d_trialTime") ||
        !_check_alloc(cudaMalloc(&ctx->d_timeCol,  ctx->nMemMax * sizeof(float)), "d_timeCol"))
    {
        gpu_shift_probe_free(ctx);
        return nullptr;
    }

    cudaMemcpy(ctx->d_eig,  eigFlat.data(),  eigFlat.size()  * sizeof(double), cudaMemcpyHostToDevice);
    cudaMemcpy(ctx->d_mean, meanFlat.data(), meanFlat.size() * sizeof(double), cudaMemcpyHostToDevice);

    // Pinned host staging buffer for .spk reads (DMA-optimised).
    ctx->h_waveBatchCap = static_cast<size_t>(ctx->nMemMax)
                          * nChan * nSamplesPerSpike;
    if (cudaMallocHost((void**)&ctx->h_waveBatchPinned,
                       ctx->h_waveBatchCap * sizeof(int16_t)) != cudaSuccess) {
        fprintf(stderr, "[shiftprobe_cuda] cudaMallocHost failed — probe disabled\n");
        gpu_shift_probe_free(ctx); return nullptr;
    }

    ctx->spkFp = fopen(spkPath, "rb");
    if (!ctx->spkFp) {
        fprintf(stderr, "[shiftprobe_cuda] cannot open %s — probe disabled\n", spkPath);
        gpu_shift_probe_free(ctx); return nullptr;
    }

    return ctx;
}

// ---------------------------------------------------------------------------
// gpu_shift_probe_free
// ---------------------------------------------------------------------------
void gpu_shift_probe_free(KK::ShiftProbeGpuCtx* ctx)
{
    if (!ctx) return;
    if (ctx->spkFp) fclose(ctx->spkFp);
    if (ctx->h_waveBatchPinned) cudaFreeHost(ctx->h_waveBatchPinned);
    if (ctx->d_eig)        cudaFree(ctx->d_eig);
    if (ctx->d_mean)       cudaFree(ctx->d_mean);
    if (ctx->d_dimMin)     cudaFree(ctx->d_dimMin);
    if (ctx->d_dimRange)   cudaFree(ctx->d_dimRange);
    if (ctx->d_waveBatch)  cudaFree(ctx->d_waveBatch);
    if (ctx->d_spikeIdx)   cudaFree(ctx->d_spikeIdx);
    if (ctx->d_cumShift)   cudaFree(ctx->d_cumShift);
    if (ctx->d_trialFeats) cudaFree(ctx->d_trialFeats);
    if (ctx->d_trialTime)  cudaFree(ctx->d_trialTime);
    if (ctx->d_timeCol)    cudaFree(ctx->d_timeCol);
    delete ctx;
}

// ---------------------------------------------------------------------------
// gpu_shift_probe_project_batch
// Reads spike waveforms from .spk into pinned host memory, DMAs to device,
// launches the projection kernel, downloads trial features + times.
// ---------------------------------------------------------------------------
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
    const int N                = ctx->N;
    const int kCand            = ctx->nCand();

    // Grow device and pinned-host batch buffers if this cluster exceeds the
    // current capacity.
    if (nMem > ctx->nMemMax) {
        int newCap = ctx->nMemMax;
        while (newCap < nMem) newCap *= 2;
        // Free + reallocate everything that scales with nMemMax.
        cudaFree(ctx->d_waveBatch);  ctx->d_waveBatch  = nullptr;
        cudaFree(ctx->d_spikeIdx);   ctx->d_spikeIdx   = nullptr;
        cudaFree(ctx->d_cumShift);   ctx->d_cumShift   = nullptr;
        cudaFree(ctx->d_trialFeats); ctx->d_trialFeats = nullptr;
        cudaFree(ctx->d_trialTime);  ctx->d_trialTime  = nullptr;
        cudaFree(ctx->d_timeCol);    ctx->d_timeCol    = nullptr;
        cudaFreeHost(ctx->h_waveBatchPinned); ctx->h_waveBatchPinned = nullptr;

        SP_CUDA_CHECK(cudaMalloc(&ctx->d_waveBatch,
            static_cast<size_t>(newCap) * waveSamples * sizeof(int16_t)));
        SP_CUDA_CHECK(cudaMalloc(&ctx->d_spikeIdx, newCap * sizeof(int)));
        SP_CUDA_CHECK(cudaMalloc(&ctx->d_cumShift, newCap * sizeof(int)));
        SP_CUDA_CHECK(cudaMalloc(&ctx->d_trialFeats,
            static_cast<size_t>(kCand) * newCap * nPCA * sizeof(float)));
        SP_CUDA_CHECK(cudaMalloc(&ctx->d_trialTime,
            static_cast<size_t>(kCand) * newCap * sizeof(float)));
        SP_CUDA_CHECK(cudaMalloc(&ctx->d_timeCol, newCap * sizeof(float)));

        ctx->h_waveBatchCap = static_cast<size_t>(newCap) * waveSamples;
        if (cudaMallocHost((void**)&ctx->h_waveBatchPinned,
                           ctx->h_waveBatchCap * sizeof(int16_t)) != cudaSuccess) {
            fprintf(stderr, "[shiftprobe_cuda] pinned realloc failed\n");
            return false;
        }
        ctx->nMemMax = newCap;
    }

    // --- Stage waveforms into pinned host buffer via seekable reads ---
    int nSkipped = 0;
    std::vector<int>   localCum(nMem);
    std::vector<float> localTs (nMem);
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = globalSpikeIndices[mi];
        if (p < 0 || p >= ctx->nPoints) { ++nSkipped; continue; }
        if (fseeko(ctx->spkFp,
                   static_cast<off_t>(p) * waveSamples * sizeof(int16_t),
                   SEEK_SET) != 0) { ++nSkipped; continue; }
        int16_t* dst = ctx->h_waveBatchPinned
                     + static_cast<size_t>(mi) * waveSamples;
        if (fread(dst, sizeof(int16_t), waveSamples, ctx->spkFp)
                != static_cast<size_t>(waveSamples)) { ++nSkipped; continue; }
        localCum[mi] = cumShift[p];
        localTs[mi]  = timeCol[p * nDims];
    }
    if (nSkipped == nMem) return false;

    // --- H2D transfers ---
    SP_CUDA_CHECK(cudaMemcpy(ctx->d_waveBatch, ctx->h_waveBatchPinned,
        static_cast<size_t>(nMem) * waveSamples * sizeof(int16_t),
        cudaMemcpyHostToDevice));
    SP_CUDA_CHECK(cudaMemcpy(ctx->d_cumShift, localCum.data(),
        nMem * sizeof(int), cudaMemcpyHostToDevice));
    SP_CUDA_CHECK(cudaMemcpy(ctx->d_timeCol,  localTs.data(),
        nMem * sizeof(float), cudaMemcpyHostToDevice));
    SP_CUDA_CHECK(cudaMemcpy(ctx->d_dimMin,   dimMin.data(),
        nPCA * sizeof(float), cudaMemcpyHostToDevice));
    SP_CUDA_CHECK(cudaMemcpy(ctx->d_dimRange, dimRange.data(),
        nPCA * sizeof(float), cudaMemcpyHostToDevice));

    // --- Launch kernel ---
    const int nThreads = ((nPCA + 31) / 32) * 32;   // round up to warp
    const size_t smemBytes = static_cast<size_t>(nChan)
                           * nSamplesPerSpike * sizeof(int16_t);
    if (smemBytes > 48u * 1024u) {
        // Fallback: probe groups with windows too large for 48 KB shared.
        // Running out of smem is rare (nChan*nSamp ≈ 8*32*2 = 512 B typical),
        // but fail safely so the CPU path can take over.
        fprintf(stderr, "[shiftprobe_cuda] smem=%zu exceeds 48 KB — CPU fallback\n", smemBytes);
        return false;
    }

    shiftprobe_project_kernel<<<nMem, nThreads, smemBytes>>>(
        ctx->d_waveBatch, ctx->d_eig, ctx->d_mean,
        ctx->d_dimMin, ctx->d_dimRange,
        ctx->d_cumShift, ctx->d_timeCol,
        maxShiftAbs, N, nChan, nSamplesPerSpike,
        data2use, nComp, ctx->recShift, ctx->isCentered ? 1 : 0,
        sessionSamples, nMem,
        ctx->d_trialFeats, ctx->d_trialTime);

    SP_CUDA_CHECK(cudaGetLastError());

    // --- D2H transfer of trial features + times ---
    SP_CUDA_CHECK(cudaMemcpy(trialFeatsOut, ctx->d_trialFeats,
        static_cast<size_t>(kCand) * nMem * nPCA * sizeof(float),
        cudaMemcpyDeviceToHost));
    SP_CUDA_CHECK(cudaMemcpy(trialTimeOut, ctx->d_trialTime,
        static_cast<size_t>(kCand) * nMem * sizeof(float),
        cudaMemcpyDeviceToHost));

    return true;
}

#endif // USE_CUDA
