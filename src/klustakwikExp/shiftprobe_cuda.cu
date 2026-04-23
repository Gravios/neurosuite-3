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
    // Basis: 3 copies × nChan × data2use*nComp doubles
    double*  d_eig       = nullptr;   // [3 * nChan * nComp * data2use]
    double*  d_mean      = nullptr;   // [3 * nChan * data2use]
    // Per-dim normalisation (size = nChan*nComp = nPCA)
    float*   d_dimMin    = nullptr;
    float*   d_dimRange  = nullptr;
    // Waveform batch staged host->device per call
    int16_t* d_waveBatch = nullptr;   // [nMemMax * nChan * nSamplesPerSpike]
    int*     d_spikeIdx  = nullptr;   // [nMemMax]
    int*     d_cumShift  = nullptr;   // [nMemMax]
    float*   d_trialFeats = nullptr;  // [3 * nMemMax * nPCA]
    float*   d_trialTime  = nullptr;  // [3 * nMemMax]
    float*   d_timeCol    = nullptr;  // [nMemMax]  (rawTsNorm per spike)

    // Host-side pinned staging for .spk I/O (read here, DMA to GPU)
    int16_t* h_waveBatchPinned = nullptr;
    size_t   h_waveBatchCap    = 0;

    // Dims
    int nChan = 0, data2use = 0, nComp = 0, recShift = 0;
    bool isCentered = false;
    int nSamplesPerSpike = 0;
    int nPoints = 0;
    int nMemMax = 0;

    // .spk file handle, opened once and reused
    FILE* spkFp = nullptr;
};

// ---------------------------------------------------------------------------
// Kernel: one block per spike, per-block shared-memory waveform, three
// accumulators per (channel, component).  Threads iterate over
// (channel * nComp) = nPCA in parallel.
//
// Grid  : (nMem,)
// Block : (nPCA,)   one thread per PCA feature
// Smem  : nChan * data2use doubles (spike window, centred if needed)
// ---------------------------------------------------------------------------
__global__ void shiftprobe_project_kernel(
    const int16_t* __restrict__ waveBatch,       // [nMem * nChan * nSamplesPerSpike]
    const double*  __restrict__ eigAll,          // [3 * nChan * nComp * data2use]
    const double*  __restrict__ meanAll,         // [3 * nChan * data2use]
    const float*   __restrict__ dimMin,          // [nPCA]
    const float*   __restrict__ dimRange,        // [nPCA]
    const int*     __restrict__ cumShift,        // [nMem]
    const float*   __restrict__ timeColIn,       // [nMem] rawTsNorm per spike
    int            maxShiftAbs,
    int            nChan,
    int            nSamplesPerSpike,
    int            data2use,
    int            nComp,
    int            recShift,
    int            isCentered,                   // 0/1
    float          sessionSamples,
    int            nMem,
    float*         __restrict__ trialFeatsOut,   // [3 * nMem * nPCA]
    float*         __restrict__ trialTimeOut)    // [3 * nMem]
{
    const int nPCA = nChan * nComp;
    const int mi   = blockIdx.x;       // spike index within batch
    const int tid  = threadIdx.x;      // PCA feature index

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

    // Three basis slabs per channel.  Layout of eigAll:
    //   [cand][ch][k*data2use + j]   stride: nChan*nComp*data2use per cand
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

    // Out-of-range mask: substitute f0 for out-of-range candidates so the
    // host-side variance criterion is unaffected by them.
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

    // Only one thread (tid==0) writes the time row for this spike.
    if (tid == 0) {
        const float rawTsNorm = timeColIn[mi];
        const float ddM = okM ? (-1.0f / sessionSamples) : 0.0f;
        const float ddP = okP ? (+1.0f / sessionSamples) : 0.0f;
        trialTimeOut[0 * nMem + mi] = rawTsNorm + ddM;
        trialTimeOut[1 * nMem + mi] = rawTsNorm;
        trialTimeOut[2 * nMem + mi] = rawTsNorm + ddP;
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
    const size_t evPerCand   = static_cast<size_t>(nChanB) * nComp * data2use;
    const size_t muPerCand   = static_cast<size_t>(nChanB) * data2use;
    const size_t nPCA        = static_cast<size_t>(nChanB) * nComp;

    auto* ctx = new KK::ShiftProbeGpuCtx();
    ctx->nChan            = nChanB;
    ctx->data2use         = data2use;
    ctx->nComp            = nComp;
    ctx->recShift         = basis.recShift;
    ctx->isCentered       = basis.isCentered;
    ctx->nSamplesPerSpike = nSamplesPerSpike;
    ctx->nPoints          = nPoints;
    // Initial capacity — grown on demand in project_batch when clusters
    // exceed it.  Start with 4096 spikes (covers most per-cluster cases).
    ctx->nMemMax = 4096;

    // --- Basis upload: flatten [cand][ch][k*data2use+j] into contiguous arrays ---
    std::vector<double> eigFlat (3 * evPerCand, 0.0);
    std::vector<double> meanFlat(3 * muPerCand, 0.0);
    for (int cand = 0; cand < 3; ++cand) {
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
            3 * ctx->nMemMax * nPCA * sizeof(float)), "d_trialFeats") ||
        !_check_alloc(cudaMalloc(&ctx->d_trialTime,
            3 * ctx->nMemMax * sizeof(float)), "d_trialTime") ||
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
            3 * static_cast<size_t>(newCap) * nPCA * sizeof(float)));
        SP_CUDA_CHECK(cudaMalloc(&ctx->d_trialTime,
            3 * static_cast<size_t>(newCap) * sizeof(float)));
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
        maxShiftAbs, nChan, nSamplesPerSpike,
        data2use, nComp, ctx->recShift, ctx->isCentered ? 1 : 0,
        sessionSamples, nMem,
        ctx->d_trialFeats, ctx->d_trialTime);

    SP_CUDA_CHECK(cudaGetLastError());

    // --- D2H transfer of trial features + times ---
    SP_CUDA_CHECK(cudaMemcpy(trialFeatsOut, ctx->d_trialFeats,
        3 * static_cast<size_t>(nMem) * nPCA * sizeof(float),
        cudaMemcpyDeviceToHost));
    SP_CUDA_CHECK(cudaMemcpy(trialTimeOut, ctx->d_trialTime,
        3 * static_cast<size_t>(nMem) * sizeof(float),
        cudaMemcpyDeviceToHost));

    return true;
}

#endif // USE_CUDA
