/***************************************************************************
                   KK_timeshift.cpp
                   ----------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Time-shift split/merge and waveform realignment phases for the KK class,
 split out of KK.cpp.  Carries the two file-local helpers used only here
 (wilsonHilfertyChi2, AutoReextractAfterFinalize).  Member definitions of the
 KK class declared in KK.h; no interface or behaviour change.
 ***************************************************************************/
#include "KK.h"
#include "KlustaKwik.h"
#include "realign_xcorr.h"
#include "realign_center.h"
#include "klusters_realign.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <omp.h>



// ===========================================================================
// Post-split shift-probe refeaturization  (kiloklustakwik)
//
// Purpose
// -------
// After a split is accepted, circularly shift each child cluster's spikes by
// δ ∈ {−1, 0, +1} samples, re-project through the cached PCA basis, and
// commit the shift that MAXIMISES the cluster's spatial-feature variance.
// Unlike standard realignment — which minimises variance by pulling spikes
// toward a template — maximising variance deliberately *spreads* any residual
// mixture structure along the PCA directions that separate subpopulations.
// The next TrySplits iteration then operates on features where residual
// mixtures are as exposed as possible.
//
// Cost
// ----
// Per call: O(|cluster| × 3 × data2use × nChan × nComp) PCA projections plus
// one .spk disk read per spike (no .fil, no .spk write).  At ±1 the wrap-
// around cost at the window boundary is negligible relative to the basis
// vectors' support: data2use is typically ≤ 0.9 × nSamplesPerSpike so the
// wrapped sample is usually outside the PCA's domain.
//
// Finalisation
// ------------
// m_cumShift[p] accumulates the chosen δ across all probe calls.  At program
// end, TimeShiftFinalize() invokes the existing RefeaturizeFromShifts +
// WritePhase15Checkpoint path, which re-extracts from .fil ONLY for spikes
// with m_cumShift[p] != 0 and rewrites .spk / .fet / (normalised .res via
// Data[timeDim]) accordingly.
// ===========================================================================

// ---------------------------------------------------------------------------
// InitTimeShift
// Load .pca[D].N once, build pre-shifted basis tensors for δ∈{-N,…,+N}, and
// open .spk (read-only) for the duration of the run.  Returns true on
// success; false makes the probe a no-op for this session.
// ---------------------------------------------------------------------------
bool KK::InitTimeShift(int nChan, int nSamplesPerSpike, int N_halfWidth)
{
    m_timeShiftReady = false;
    if (nChan <= 0 || nSamplesPerSpike <= 0) return false;
    if (nPoints <= 0 || nDims <= 1)          return false;
    if (N_halfWidth < 0) N_halfWidth = 0;
    if (N_halfWidth > kTimeShiftNmax) {
        Output("InitTimeShift: N_halfWidth=%d exceeds compile-time max %d — "
               "clamping\n", N_halfWidth, kTimeShiftNmax);
        N_halfWidth = kTimeShiftNmax;
    }
    if (N_halfWidth == 0) {
        Output("InitTimeShift: N_halfWidth=0 — probe disabled (no-op)\n");
        return false;
    }
    const int N     = N_halfWidth;
    const int kCand = 2 * N + 1;
    m_timeShiftMaxAbs = N;

    // --- Allocate cumulative-shift accumulator ---
    m_cumShift.assign(static_cast<size_t>(nPoints), 0);

    // --- Load raw PCA basis from .pca[D].N ---
    char pcaPath[STRLEN + 16];
    pickInputPath(pcaPath, sizeof(pcaPath), FileBase, "pca", ElecNo);
    FILE* pf = fopen(pcaPath, "rb");
    if (!pf) {
        Output("InitTimeShift: %s not found — post-split shift probe disabled\n",
               pcaPath);
        return false;
    }

    auto rd32 = [&](int32_t& v) { return fread(&v, 4, 1, pf) == 1; };
    int32_t nc, d2u, ncomp, ic, rs;
    if (!rd32(nc) || !rd32(d2u) || !rd32(ncomp) || !rd32(ic) || !rd32(rs)) {
        Output("InitTimeShift: truncated PCA header in %s — probe disabled\n",
               pcaPath);
        fclose(pf); return false;
    }
    // Channel-count check — accepts canonical .pca.N (nc == nChan) and
    // stderiv .pcaD.N variants with channel reduction.  For SDIFF_ALLPAIRS
    // (order 3) and SDIFF_FIRST (order 1), the last of nChan channels is
    // linearly dependent; process_pca_stderiv drops it at basis-build time,
    // so the .pcaD.N file has nc = nChan - 1 channels.  At probe time we
    // read from .spkD.N (which retains all nChan transformed channels, the
    // last being redundant) and iterate only the first nc = nChan - 1 in
    // the projection loop.
    const bool isStderiv = (nc == nChan - 1);
    if (nc != nChan && !isStderiv) {
        Output("InitTimeShift: PCA has %d channels, spike group has %d "
               "(expected %d for canonical .pca or %d for stderiv .pcaD) "
               "— probe disabled\n",
               nc, nChan, nChan, nChan - 1);
        fclose(pf); return false;
    }
    if (isStderiv) {
        Output("InitTimeShift: stderiv mode (.pcaD.%d basis, %d effective "
               "channels; last-channel-redundant convention)\n",
               ElecNo, nc);
    }
    m_timeShiftBasis.nChan       = nc;
    m_timeShiftBasis.data2use    = d2u;
    m_timeShiftBasis.nComp       = ncomp;
    m_timeShiftBasis.recShift    = rs;
    m_timeShiftBasis.isCentered  = (ic != 0);
    m_timeShiftBasis.N           = N;
    m_timeShiftBasis.isStderiv   = isStderiv;
    m_timeShiftBasis.rawChannels = nChan;   // .spkD stride uses raw count

    // Sanity check: N must be less than the PCA support, otherwise the
    // shifted bases are almost entirely zero and the probe is pointless.
    if (N >= d2u / 2) {
        Output("InitTimeShift: N=%d is >= data2use/2=%d; shifted bases would "
               "be nearly empty — probe disabled\n", N, d2u/2);
        fclose(pf); m_timeShiftBasis = TimeShiftBasis{}; return false;
    }

    // Stage the raw (unshifted) basis first — we discard it after building
    // the (2N+1) pre-shifted copies.
    std::vector<std::vector<double>> rawMean  (static_cast<size_t>(nc));
    std::vector<std::vector<double>> rawEigvec(static_cast<size_t>(nc));
    for (int ch = 0; ch < nc; ++ch) {
        rawMean[static_cast<size_t>(ch)]
            .resize(static_cast<size_t>(d2u));
        if (fread(rawMean[static_cast<size_t>(ch)].data(),
                  8, static_cast<size_t>(d2u), pf) != static_cast<size_t>(d2u)) {
            Output("InitTimeShift: truncated PCA means (ch %d) — probe disabled\n", ch);
            fclose(pf); m_timeShiftBasis = TimeShiftBasis{}; return false;
        }
    }
    const size_t evSz = static_cast<size_t>(d2u * ncomp);
    for (int ch = 0; ch < nc; ++ch) {
        rawEigvec[static_cast<size_t>(ch)].resize(evSz);
        if (fread(rawEigvec[static_cast<size_t>(ch)].data(),
                  8, evSz, pf) != evSz) {
            Output("InitTimeShift: truncated PCA eigenvectors (ch %d) — probe disabled\n", ch);
            fclose(pf); m_timeShiftBasis = TimeShiftBasis{}; return false;
        }
    }
    fclose(pf);

    const int nPCAFeatures = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCAFeatures > nDims - 1) {
        Output("InitTimeShift: PCA feature count (%d) exceeds nDims-1 (%d) — "
               "probe disabled\n", nPCAFeatures, nDims - 1);
        m_timeShiftBasis = TimeShiftBasis{};
        return false;
    }

    // --- Build pre-shifted bases for δ ∈ {-N, …, +N} -----------------------
    // Reindexing identity (derivation):
    //   y_δ[k] = Σ_j E[k,j] · (x[(rs+j+δ)*C+c] − μ[c,j])
    //   let j' = j+δ in the WAVEFORM indexing, so raw sample position is
    //   (rs+j')*C+c.  The basis row that multiplies raw sample (rs+j')*C+c
    //   is therefore E[k, j'−δ] with corresponding mean μ[c, j'−δ].
    //   When j'−δ is outside [0, data2use) we zero-pad (PC tails are near
    //   zero at the window edges — contribution is negligible).
    //
    // Storage layout: eigvecShifted[cand][ch] is a flat vector of length
    // d2u*ncomp indexed as k*d2u + j'.  cand=0..2N maps to δ=cand-N.
    m_timeShiftBasis.meanShifted.assign(static_cast<size_t>(kCand), {});
    m_timeShiftBasis.eigvecShifted.assign(static_cast<size_t>(kCand), {});
    for (int ci = 0; ci < kCand; ++ci) {
        const int delta = ci - N;
        m_timeShiftBasis.meanShifted  [static_cast<size_t>(ci)]
            .assign(static_cast<size_t>(nc), {});
        m_timeShiftBasis.eigvecShifted[static_cast<size_t>(ci)]
            .assign(static_cast<size_t>(nc), {});
        for (int ch = 0; ch < nc; ++ch) {
            auto& muOut = m_timeShiftBasis.meanShifted
                          [static_cast<size_t>(ci)]
                          [static_cast<size_t>(ch)];
            auto& evOut = m_timeShiftBasis.eigvecShifted
                          [static_cast<size_t>(ci)]
                          [static_cast<size_t>(ch)];
            muOut.assign(static_cast<size_t>(d2u), 0.0);
            evOut.assign(evSz, 0.0);
            const auto& muIn = rawMean  [static_cast<size_t>(ch)];
            const auto& evIn = rawEigvec[static_cast<size_t>(ch)];

            for (int jp = 0; jp < d2u; ++jp) {
                const int src = jp - delta;          // read index into raw basis
                if (src < 0 || src >= d2u) continue; // zero-pad outside domain
                muOut[static_cast<size_t>(jp)] =
                    muIn[static_cast<size_t>(src)];
                for (int k = 0; k < ncomp; ++k)
                    evOut[static_cast<size_t>(k * d2u + jp)] =
                        evIn[static_cast<size_t>(k * d2u + src)];
            }
        }
    }

    // --- Open .spk or .spkD ---
    // In stderiv mode the .spkD file holds transformed waveforms that match
    // the .pcaD basis.  Otherwise canonical .spk.
    //
    // Mapping strategy:
    //   Preferred: mmap(MAP_PRIVATE) — gives random-access shared-memory
    //   semantics via the kernel page cache, amortising cluster-scattered
    //   reads across the whole session without per-spike fseeko/fread.
    //   Fallback: fopen — used when mmap fails (e.g. exotic filesystems,
    //   NFS with wonky MAP_PRIVATE support).
    char spkPath[STRLEN + 16];
    if (isStderiv) {
        std::snprintf(spkPath, sizeof(spkPath), "%s.spkD.%d", FileBase, ElecNo);
    } else {
        pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo);
    }

    const int spkFd = open(spkPath, O_RDONLY);
    if (spkFd < 0) {
        Output("InitTimeShift: cannot open %s — probe disabled\n", spkPath);
        m_timeShiftBasis = TimeShiftBasis{};
        return false;
    }
    struct stat st;
    if (fstat(spkFd, &st) != 0 || st.st_size <= 0) {
        Output("InitTimeShift: cannot stat %s — probe disabled\n", spkPath);
        close(spkFd);
        m_timeShiftBasis = TimeShiftBasis{};
        return false;
    }
    const size_t spkBytes = static_cast<size_t>(st.st_size);
    void* spkMap = mmap(nullptr, spkBytes, PROT_READ, MAP_PRIVATE, spkFd, 0);
    close(spkFd);   // mmap holds its own reference

    if (spkMap == MAP_FAILED) {
        // Fallback to stdio.
        Output("InitTimeShift: mmap(%s) failed (%s) — falling back to stdio\n",
               spkPath, std::strerror(errno));
        m_timeShiftSpkFp = fopen(spkPath, "rb");
        if (!m_timeShiftSpkFp) {
            Output("InitTimeShift: cannot open %s — probe disabled\n", spkPath);
            m_timeShiftBasis = TimeShiftBasis{};
            return false;
        }
        m_timeShiftSpkMap = nullptr;
        m_timeShiftSpkLen = 0;
    } else {
        // Advise the kernel that random access is coming so it uses the
        // page cache aggressively without prefetching whole-file sequences.
        madvise(spkMap, spkBytes, MADV_RANDOM);
        m_timeShiftSpkMap = spkMap;
        m_timeShiftSpkLen = spkBytes;
        m_timeShiftSpkFp  = nullptr;
        Output("InitTimeShift: mmap(%s) %.2f MB — random-access page cache active\n",
               spkPath, spkBytes / (1024.0 * 1024.0));
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // Initialise GPU shift-probe context if a GPU is in play.
    // Implementation lives in shiftprobe_<backend>.{cu,hip,cpp}.
    // gpu_timeshift_init returns nullptr on failure; the probe then
    // falls through to the CPU path (still correct, just slower).
    if (gpu) {
        extern TimeShiftGpuCtx* gpu_timeshift_init(
            KK_GPU* base, const TimeShiftBasis& basis, int nChan,
            int nSamplesPerSpike, int nPoints, const char* spkPath);
        m_timeShiftGpuCtx = gpu_timeshift_init(gpu, m_timeShiftBasis,
                                             nChan, nSamplesPerSpike,
                                             nPoints, spkPath);
        if (m_timeShiftGpuCtx)
            Output("InitTimeShift: GPU kernel active (backend=%s)\n",
                   GPU_BACKEND_NAME);
    }
#endif

    m_timeShiftReady = true;
    m_timeShiftCallCount = 0;
    Output("InitTimeShift: ready (nChan=%d data2use=%d nComp=%d recShift=%d "
           "isCentered=%d, pre-shifted bases for δ∈{-%d,…,+%d} (%d candidates))\n",
           m_timeShiftBasis.nChan, m_timeShiftBasis.data2use,
           m_timeShiftBasis.nComp, m_timeShiftBasis.recShift,
           (int)m_timeShiftBasis.isCentered, N, N, kCand);
    return true;
}


// ---------------------------------------------------------------------------
// CloseTimeShift
// ---------------------------------------------------------------------------
void KK::CloseTimeShift()
{
    if (m_timeShiftSpkFp) { fclose(m_timeShiftSpkFp); m_timeShiftSpkFp = nullptr; }
    if (m_timeShiftSpkMap) {
        munmap(m_timeShiftSpkMap, m_timeShiftSpkLen);
        m_timeShiftSpkMap = nullptr;
        m_timeShiftSpkLen = 0;
    }
#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (m_timeShiftGpuCtx) {
        extern void gpu_timeshift_free(TimeShiftGpuCtx* ctx);
        gpu_timeshift_free(m_timeShiftGpuCtx);
        m_timeShiftGpuCtx = nullptr;
    }
#endif
    m_timeShiftReady = false;
}


// ---------------------------------------------------------------------------
// TimeShiftReadSpikeWave — read one spike's waveform from .spk / .spkD.
//
// Transparently branches on whichever backing store InitTimeShift chose:
//   • m_timeShiftSpkMap (preferred): const-cast pointer arithmetic + memcpy
//   • m_timeShiftSpkFp  (fallback):  fseeko + fread
//
// Both paths return `waveSamples` int16_t values starting at byte offset
// (p * waveSamples * sizeof(int16_t)).  Caller owns dst.  Returns false on
// bounds failure, I/O error, or when neither backing store is active.
// ---------------------------------------------------------------------------
bool KK::TimeShiftReadSpikeWave(int p, int waveSamples, int16_t* dst)
{
    if (p < 0 || waveSamples <= 0 || !dst) return false;
    const size_t byteOff = static_cast<size_t>(p) *
                           static_cast<size_t>(waveSamples) * sizeof(int16_t);
    const size_t byteLen = static_cast<size_t>(waveSamples) * sizeof(int16_t);

    if (m_timeShiftSpkMap) {
        if (byteOff + byteLen > m_timeShiftSpkLen) return false;
        std::memcpy(dst,
                    static_cast<const char*>(m_timeShiftSpkMap) + byteOff,
                    byteLen);
        return true;
    }
    if (m_timeShiftSpkFp) {
        if (fseeko(m_timeShiftSpkFp, static_cast<off_t>(byteOff), SEEK_SET) != 0)
            return false;
        return fread(dst, sizeof(int16_t),
                     static_cast<size_t>(waveSamples), m_timeShiftSpkFp)
               == static_cast<size_t>(waveSamples);
    }
    return false;
}


// ---------------------------------------------------------------------------
// ApplySdiffAllpairsTemporalDiff — in-place stderiv transform on a spike wave.
//
// Mirrors process_extractspikes_stderiv.cpp::fill_sdiff_buffer() exactly:
//   step 1 per time sample t:  sdiff[t, ch] = nChan * raw[t, ch] − Σ raw[t, :]
//                              (saturating to int16 range)
//   step 2 per time sample t:  wave[t, ch] = sdiff[t, ch] − sdiff[t-1, ch]
//                              (saturating to int16 range)
// Boundary: sdiff[-1, ch] = 0 per spike.  The streaming extraction uses a
// persisted chunk boundary; for per-spike re-extraction (what Phase 4 does)
// sdPrev=0 matches process_pca_stderiv's -d 4 pass-through convention.
//
// Called by RefeaturizeFromShifts (before basis projection) and
// WritePhase15Checkpoint (before writing .spkD.pending) when the basis
// is stderiv.  Static so it's a pure function of its arguments — no
// dependence on KK state beyond what's passed in.
// ---------------------------------------------------------------------------
void KK::ApplySdiffAllpairsTemporalDiff(int16_t* wave, int nChan,
                                        int nSamplesPerSpike)
{
    if (!wave || nChan <= 0 || nSamplesPerSpike <= 0) return;

    // Reusable scratch: per-channel sdiff for "current" and "previous" samples.
    // nChan is small (≤ 16 for typical probes), so heap alloc is negligible.
    std::vector<int32_t> sdiffCur(static_cast<size_t>(nChan), 0);
    std::vector<int32_t> sdiffPrev(static_cast<size_t>(nChan), 0);

    auto satI16 = [](int32_t v) -> int16_t {
        if (v >  32767) return  32767;
        if (v < -32768) return -32768;
        return static_cast<int16_t>(v);
    };

    for (int s = 0; s < nSamplesPerSpike; ++s) {
        // Spatial sum
        int32_t sum = 0;
        for (int c = 0; c < nChan; ++c)
            sum += static_cast<int32_t>(wave[s * nChan + c]);

        // SDIFF_ALLPAIRS + saturate
        for (int c = 0; c < nChan; ++c) {
            const int32_t raw = static_cast<int32_t>(wave[s * nChan + c]);
            int32_t iv = nChan * raw - sum;
            if (iv >  32767) iv =  32767;
            if (iv < -32768) iv = -32768;
            sdiffCur[static_cast<size_t>(c)] = iv;
        }

        // Temporal first-difference + saturate, write in place
        for (int c = 0; c < nChan; ++c) {
            const int32_t diff = sdiffCur[static_cast<size_t>(c)]
                               - sdiffPrev[static_cast<size_t>(c)];
            wave[s * nChan + c] = satI16(diff);
        }

        // Save for next iteration's sdPrev (cannot overwrite before the
        // temporal-diff loop reads sdiffPrev).
        std::swap(sdiffCur, sdiffPrev);
    }
}


// ---------------------------------------------------------------------------
// TimeShiftSplit
//
// Multi-candidate shift-probe applied at split acceptance.  Projects every
// spike under (2N+1) δ-shifted PCA bases; picks the cluster-wide δ that
// maximises the per-PC-dim variance sum across the spike set.
//
// For stderiv sessions: .spkD contains already-transformed waveforms and
// .pcaD was built on matching stderiv data, so the pre-shifted-basis trick
// is applied directly to .spkD content.  The zero-pad approximation at the
// edges of the shifted basis is small for the shift magnitudes used here
// (|δ| ≤ 5 samples within a 20–50-sample window).
//
// Arguments:
//   globalSpikeIndices — indices into Data[] identifying the spikes
//   nChan, nSamplesPerSpike — .spk/.spkD layout dimensions (sample-major)
//
// Returns the number of spikes whose committed shift changed this call.
//
// Core loop: the (2N+1) candidate deltas share every raw-sample load from
// .spk/.spkD.  (2N+1) accumulators per (channel, PC) are stepped through
// data2use samples in a single pass, reading from (2N+1) pre-shifted basis
// buffers.  No modulo, no branching in the hot loop — maps cleanly to SIMD
// and GPU.
//
// Selection: cluster-wide max sum-of-per-dim variance across candidates.
// Winner is committed as a single delta for ALL spikes in the index list.
// ---------------------------------------------------------------------------
int KK::TimeShiftSplit(const std::vector<int>& globalSpikeIndices,
                                    int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady)                                      return 0;
    if (!m_timeShiftSpkMap && !m_timeShiftSpkFp)                return 0;
    if (!m_timeShiftBasis.valid())                              return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0)                    return 0;
    const int nMem = static_cast<int>(globalSpikeIndices.size());
    if (nMem < 2) return 0;   // variance of a single point is 0; skip

    const int waveSamples = nChan * nSamplesPerSpike;
    const int timeDimIdx  = nDims - 1;
    const int nSpatial    = nDims - 1;
    const int nPCA        = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCA <= 0 || nPCA > nSpatial) return 0;
    const float sessionSamples = timeRawMax - timeRawMin;
    if (!(sessionSamples > 0.0f)) return 0;

    const int N     = m_timeShiftBasis.N;
    const int kCand = m_timeShiftBasis.nCand();
    // kCand <= 2*kTimeShiftNmax + 1 = 11 by construction
    constexpr int kCandMax = 2 * kTimeShiftNmax + 1;

    // Pre-size scratch.
    if (static_cast<int>(m_timeShiftWaveScratch.size()) < waveSamples)
        m_timeShiftWaveScratch.assign(static_cast<size_t>(waveSamples), 0);
    m_timeShiftTrialFeats.assign(static_cast<size_t>(kCand * nMem * nPCA), 0.0f);
    m_timeShiftTrialTime .assign(static_cast<size_t>(kCand * nMem),        0.0f);

    std::vector<double> sumPerDim  (static_cast<size_t>(kCand * nPCA), 0.0);
    std::vector<double> sumSqPerDim(static_cast<size_t>(kCand * nPCA), 0.0);

    const auto& pca = m_timeShiftBasis;
    const int   data2use = pca.data2use;
    const int   nComp    = pca.nComp;
    const int   nChanPca = pca.nChan;
    const int   rs       = pca.recShift;
    const bool  isCen    = pca.isCentered;

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // GPU fast path: dispatcher runs the (2N+1)-candidate projection on-device
    // and returns trial features in the same layout as the CPU path.  Falls
    // back to CPU when the context is null or the cluster is too small to
    // amortise launch overhead (threshold tuned for typical L2-resident
    // data2use * nComp * nChan; < 128 spikes almost always lose on GPU).
    const bool useGpu = (m_timeShiftGpuCtx != nullptr) && (nMem >= 128);
    if (useGpu) {
        extern bool gpu_timeshift_project_batch(
            TimeShiftGpuCtx* ctx,
            const std::vector<int>& globalSpikeIndices,
            const std::vector<int>& cumShift,
            int maxShiftAbs,
            const std::vector<float>& dimMin, const std::vector<float>& dimRange,
            float* trialFeatsOut, float* trialTimeOut,
            const float* timeCol, int nDims, float sessionSamples);
        const float* timeCol = Data.m_Data + timeDimIdx;   // strided; kernel uses nDims
        const bool ok = gpu_timeshift_project_batch(
            m_timeShiftGpuCtx,
            globalSpikeIndices, m_cumShift, m_timeShiftMaxAbs,
            dimMin_, dimRange_,
            m_timeShiftTrialFeats.data(), m_timeShiftTrialTime.data(),
            timeCol, nDims, sessionSamples);
        if (ok) {
            // Fold results into per-candidate moments.
            for (int ci = 0; ci < kCand; ++ci)
                for (int mi = 0; mi < nMem; ++mi) {
                    const size_t base = (static_cast<size_t>(ci) * nMem + mi) * nPCA;
                    for (int fi = 0; fi < nPCA; ++fi) {
                        const float fv = m_timeShiftTrialFeats[base + fi];
                        sumPerDim  [ci * nPCA + fi] += fv;
                        sumSqPerDim[ci * nPCA + fi] += static_cast<double>(fv) * fv;
                    }
                }
            goto pick_best_and_commit;
        }
        // ok==false → fall through to CPU path (e.g. transient alloc failure)
    }
#endif

    // CPU path: pre-shifted bases + (2N+1) accumulators in the inner loop.
    {
        int nSkippedRead = 0;
        // Stack-allocated accumulator arrays — size bounded at compile time
        // by kCandMax so the compiler can allocate them to registers.
        for (int mi = 0; mi < nMem; ++mi) {
            const int p = globalSpikeIndices[static_cast<size_t>(mi)];
            if (p < 0 || p >= nPoints) { ++nSkippedRead; continue; }

            if (!TimeShiftReadSpikeWave(p, waveSamples,
                                        m_timeShiftWaveScratch.data())) {
                ++nSkippedRead; continue;
            }

            const int baseCum = m_cumShift[static_cast<size_t>(p)];
            const int16_t* raw = m_timeShiftWaveScratch.data();

            // Per-candidate out-of-range mask (fall back to the δ=0 features,
            // i.e. cand = N, when committing the candidate shift would exceed
            // the global clamp).
            bool candOk[kCandMax];
            for (int ci = 0; ci < kCand; ++ci)
                candOk[ci] = (std::abs(baseCum + (ci - N))
                              <= m_timeShiftMaxAbs);

            // For each channel, accumulate projections for all (2N+1)
            // candidates in one pass over samples.
            for (int ch = 0; ch < nChanPca; ++ch) {
                // Cache basis pointers for this channel × candidate.
                const double* evs[kCandMax];
                const double* mus[kCandMax];
                for (int ci = 0; ci < kCand; ++ci) {
                    evs[ci] = pca.eigvecShifted[ci][static_cast<size_t>(ch)].data();
                    mus[ci] = pca.meanShifted  [ci][static_cast<size_t>(ch)].data();
                }

                for (int k = 0; k < nComp; ++k) {
                    double acc[kCandMax] = {0.0};
                    // Fanned inner loop — reads every raw sample exactly once,
                    // multiplies against (2N+1) shifted basis rows.
                    for (int j = 0; j < data2use; ++j) {
                        const int s = rs + j;
                        const double rawV = static_cast<double>(raw[s * nChan + ch]);
                        if (isCen) {
                            for (int ci = 0; ci < kCand; ++ci)
                                acc[ci] += evs[ci][k * data2use + j]
                                         * (rawV - mus[ci][j]);
                        } else {
                            for (int ci = 0; ci < kCand; ++ci)
                                acc[ci] += evs[ci][k * data2use + j] * rawV;
                        }
                    }

                    const int   fi   = ch * nComp + k;
                    const float min_ = dimMin_  [fi];
                    const float rng_ = dimRange_[fi];
                    const int   cand0 = N;   // the δ=0 candidate index
                    const float f0   = (static_cast<float>(acc[cand0]) - min_) * rng_;

                    for (int ci = 0; ci < kCand; ++ci) {
                        const float fv = (static_cast<float>(acc[ci]) - min_) * rng_;
                        const size_t ofs =
                            (static_cast<size_t>(ci) * nMem + mi) * nPCA + fi;
                        // If candidate δ is out of range for this spike we
                        // substitute the δ=0 features so the variance
                        // criterion is unaffected by out-of-range spikes.
                        m_timeShiftTrialFeats[ofs] = candOk[ci] ? fv : f0;
                        sumPerDim  [ci * nPCA + fi] += m_timeShiftTrialFeats[ofs];
                        sumSqPerDim[ci * nPCA + fi] +=
                            static_cast<double>(m_timeShiftTrialFeats[ofs]) *
                            m_timeShiftTrialFeats[ofs];
                    }
                }
            }

            // Per-candidate trial timestamps (normalised).  Out-of-range
            // candidates keep the original timestamp.
            const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];
            for (int ci = 0; ci < kCand; ++ci) {
                const float dd = candOk[ci]
                    ? static_cast<float>(ci - N) / sessionSamples
                    : 0.0f;
                m_timeShiftTrialTime[static_cast<size_t>(ci) * nMem + mi] =
                    rawTsNorm + dd;
            }
        }
        if (nSkippedRead == nMem) return 0;
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
pick_best_and_commit:
#endif

    // --- Pick the candidate with the largest total spatial-feature variance ---
    const double invN = 1.0 / static_cast<double>(nMem);
    int   bestCand = N;        // default to δ=0 candidate
    double bestVar = -1.0;
    for (int ci = 0; ci < kCand; ++ci) {
        double tot = 0.0;
        for (int fi = 0; fi < nPCA; ++fi) {
            const double s  = sumPerDim  [ci * nPCA + fi];
            const double ss = sumSqPerDim[ci * nPCA + fi];
            const double mean = s * invN;
            const double var  = std::max(0.0, ss * invN - mean * mean);
            tot += var;
        }
        if (tot > bestVar) { bestVar = tot; bestCand = ci; }
    }
    const int bestDelta = bestCand - N;

    // --- Commit winner into Data[] + m_cumShift ---
    int nChanged = 0;
    if (bestDelta != 0) {
        for (int mi = 0; mi < nMem; ++mi) {
            const int p = globalSpikeIndices[static_cast<size_t>(mi)];
            if (p < 0 || p >= nPoints) continue;
            const int wouldBe = m_cumShift[static_cast<size_t>(p)] + bestDelta;
            if (std::abs(wouldBe) > m_timeShiftMaxAbs) continue;

            const size_t featBase =
                (static_cast<size_t>(bestCand) * nMem + mi) * nPCA;
            float* dataRow = Data.m_Data + p * nDims;
            for (int fi = 0; fi < nPCA; ++fi)
                dataRow[fi] = m_timeShiftTrialFeats[featBase + fi];
            dataRow[timeDimIdx] =
                m_timeShiftTrialTime[static_cast<size_t>(bestCand) * nMem + mi];
            m_cumShift[static_cast<size_t>(p)] = wouldBe;
            ++nChanged;
        }
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    // Data[] changed on host.  Re-upload to device so the next MStep/EStep
    // reads the refreshed features.  Scatter-upload would be cheaper; for
    // now a full upload is simpler and still substantially cheaper than
    // the EM steps that follow.
    if (nChanged > 0 && gpu)
        gpu_upload_data(gpu, Data.m_Data);
#endif

    ++m_timeShiftCallCount;
    return nChanged;
}


// ---------------------------------------------------------------------------
// ShiftWaveformRowInPlace
//
// Apply m_cumShift[p] to a freshly-read .spk waveform row, in-place.
// Used by Phase 4 mean-waveform harvest so meanWav/Left/Right reflect
// the shifted spike geometry that EStep/MStep already see post-probe.
//
// Algorithm: per-channel circular shift.  For shift sh (in samples):
//   new_row[s, ch] = old_row[(s - sh) mod nSamples, ch]
// Implementing the shift on the .spk-window directly (rather than
// re-extracting from .fil) is correct to within boundary samples; on
// high-pass-filtered data with flat edges those wrapped samples are
// baseline noise and the approximation is essentially exact for
// |sh| <= 1-2 samples (which is the only range the split probe uses
// when InitTimeShift is called with N_halfWidth=1).
//
// No-ops if the probe never ran (m_cumShift empty) or this spike has
// not been shifted (sh == 0).  Cost when sh != 0: one tmp allocation
// of nSamples*nChan int16 + a single copy pass.
// ---------------------------------------------------------------------------
void KK::ShiftWaveformRowInPlace(int16_t* row, int p,
                                 int nChan, int nSamples) const
{
    if (m_cumShift.empty()) return;
    if (p < 0 || p >= static_cast<int>(m_cumShift.size())) return;
    const int sh = m_cumShift[static_cast<size_t>(p)];
    if (sh == 0) return;
    if (nSamples <= 0 || nChan <= 0) return;

    const int total = nChan * nSamples;
    std::vector<int16_t> tmp(static_cast<size_t>(total));
    for (int s = 0; s < nSamples; s++) {
        // Wrap (s - sh) into [0, nSamples).  Negative-modulo behavior
        // is robust under either pre-C++11 or post-C++11 rules with
        // the (... + nSamples) % nSamples form.
        const int src = ((s - sh) % nSamples + nSamples) % nSamples;
        const int srcRow = src * nChan;
        const int dstRow = s   * nChan;
        for (int ch = 0; ch < nChan; ch++)
            tmp[static_cast<size_t>(dstRow + ch)] =
                row[static_cast<size_t>(srcRow + ch)];
    }
    std::copy(tmp.begin(), tmp.end(), row);
}


// ---------------------------------------------------------------------------
// TimeShiftMergeTighten
//
// Per-spike shift selection under a Mahalanobis-minimum criterion against a
// receiving cluster's Gaussian (mean + Cholesky factor of covariance).  Each
// spike picks INDEPENDENTLY the δ ∈ {-N, …, +N} that maximises its fit to
// the destination cluster.
//
// Rationale.  Split-probe uses max-variance because we want to EXPOSE
// mixture structure.  Merge-probe uses min-Mahalanobis because we want to
// TIGHTEN the fit of newly-transferred points to the cluster that is
// absorbing them.  Same infrastructure (pre-shifted bases, I/O, GPU) but a
// different selection criterion and per-spike rather than cluster-wide.
//
// Mahalanobis²(x_δ) = ||L^-1 · (x_δ - μ)||²  where L = destChol (lower tri).
// Forward-substitute to compute y = L^-1 · (x - μ); Mahal² = yᵀy.
// Constants (logRootDet, log2π, log(weight)) cancel across candidates for
// the same destination cluster, so we skip them.
//
// Parameters
//   globalSpikeIndices  — 0-based global indices into Data[] / .spk / m_cumShift
//   nChan, nSamplesPerSpike — .spk layout dimensions
//   destMean  — cluster mean vector (nDims floats, INCLUDING the time dim)
//   destChol  — cluster Cholesky factor (nDims² floats, lower triangular
//               stored row-major; diag positive, super-diag entries 0)
//
// Returns the number of spikes whose cumShift changed this call.
// ---------------------------------------------------------------------------
int KK::TimeShiftMergeTighten(
    const std::vector<int>& globalSpikeIndices,
    int nChan, int nSamplesPerSpike,
    const float* destMean, const float* destChol)
{
    if (!m_timeShiftReady || (!m_timeShiftSpkMap && !m_timeShiftSpkFp))  return 0;
    if (!m_timeShiftBasis.valid())             return 0;
    if (!destMean || !destChol)               return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0)  return 0;
    const int nMem = static_cast<int>(globalSpikeIndices.size());
    if (nMem < 1) return 0;

    const int waveSamples = nChan * nSamplesPerSpike;
    const int timeDimIdx  = nDims - 1;
    const int nPCA        = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCA <= 0 || nPCA > nDims - 1) return 0;
    const float sessionSamples = timeRawMax - timeRawMin;
    if (!(sessionSamples > 0.0f)) return 0;

    const int N     = m_timeShiftBasis.N;
    const int kCand = m_timeShiftBasis.nCand();
    constexpr int kCandMax = 2 * kTimeShiftNmax + 1;

    // We reuse the split-probe scratch buffers to avoid a second allocation.
    // Trial features are computed for all candidates; Mahal² is computed on
    // the fly per spike per candidate.
    if (static_cast<int>(m_timeShiftWaveScratch.size()) < waveSamples)
        m_timeShiftWaveScratch.assign(static_cast<size_t>(waveSamples), 0);

    const auto& pca   = m_timeShiftBasis;
    const int   data2use = pca.data2use;
    const int   nComp    = pca.nComp;
    const int   nChanPca = pca.nChan;
    const int   rs       = pca.recShift;
    const bool  isCen    = pca.isCentered;

    // Stack buffers sized for the worst case.  nDims is small (O(30)) so
    // these fit comfortably.
    std::vector<float> trialFeats(static_cast<size_t>(kCand * nPCA));
    std::vector<float> yVec      (static_cast<size_t>(nDims));  // forward-sub scratch
    std::vector<float> xMinusMu  (static_cast<size_t>(nDims));

    int nChanged = 0;
    int nSkippedRead = 0;

    for (int mi = 0; mi < nMem; ++mi) {
        const int p = globalSpikeIndices[static_cast<size_t>(mi)];
        if (p < 0 || p >= nPoints) { ++nSkippedRead; continue; }

        if (!TimeShiftReadSpikeWave(p, waveSamples,
                                    m_timeShiftWaveScratch.data())) {
            ++nSkippedRead; continue;
        }
        // CRITICAL: bring the freshly-read .spk waveform forward by the
        // cumulative shift already committed for this spike before we
        // project through any candidate basis.  Without this step the
        // probe interprets `bestDelta` as an ABSOLUTE shift from raw .spk
        // rather than an INCREMENT from the current state — yielding a
        // runaway when chained across multiple alignment passes
        // (TimeShiftAlignPhase calls this in a loop of TimeShiftAlignIter
        // passes, ConsiderDeletion's post-merge tightener is itself called
        // from the various TimeShiftAlignAfterPhase* hooks).  With the
        // shift applied here, ci=N (bestDelta=0) correctly means "stay at
        // the current cumulative position" and convergence is possible.
        // (The mean-waveform harvest path applies this same correction;
        // see Phase 4 callers at ~3565/3704/4683/4860/7132.)
        ShiftWaveformRowInPlace(m_timeShiftWaveScratch.data(), p,
                                nChan, nSamplesPerSpike);

        const int baseCum = m_cumShift[static_cast<size_t>(p)];
        const int16_t* raw = m_timeShiftWaveScratch.data();

        bool candOk[kCandMax];
        for (int ci = 0; ci < kCand; ++ci)
            candOk[ci] = (std::abs(baseCum + (ci - N))
                          <= m_timeShiftMaxAbs);

        // Project this spike under every candidate δ — same (2N+1)-fanned
        // inner loop as the split probe, but results are kept per-spike in
        // the local `trialFeats` scratch (not the cluster-wide buffer).
        for (int ch = 0; ch < nChanPca; ++ch) {
            const double* evs[kCandMax];
            const double* mus[kCandMax];
            for (int ci = 0; ci < kCand; ++ci) {
                evs[ci] = pca.eigvecShifted[ci][static_cast<size_t>(ch)].data();
                mus[ci] = pca.meanShifted  [ci][static_cast<size_t>(ch)].data();
            }

            for (int k = 0; k < nComp; ++k) {
                double acc[kCandMax] = {0.0};
                for (int j = 0; j < data2use; ++j) {
                    const int s = rs + j;
                    const double rawV = static_cast<double>(raw[s * nChan + ch]);
                    if (isCen) {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j]
                                     * (rawV - mus[ci][j]);
                    } else {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j] * rawV;
                    }
                }
                const int   fi   = ch * nComp + k;
                const float min_ = dimMin_  [fi];
                const float rng_ = dimRange_[fi];
                for (int ci = 0; ci < kCand; ++ci)
                    trialFeats[static_cast<size_t>(ci * nPCA + fi)] =
                        (static_cast<float>(acc[ci]) - min_) * rng_;
            }
        }

        // Trial timestamps for this spike (normalised).
        const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];

        // Evaluate Mahalanobis² under the destination cluster for each
        // in-range candidate and pick the minimum.  Also track the
        // δ=0 candidate's Mahal² separately so the post-loop gate
        // (-TimeShiftAlignScoreThresh) can require a minimum
        // improvement before committing a non-baseline shift.
        int   bestCand     = N;
        float bestMahal    = std::numeric_limits<float>::infinity();
        float baselineMahal = std::numeric_limits<float>::infinity();

        for (int ci = 0; ci < kCand; ++ci) {
            if (!candOk[ci]) continue;

            // Build x (nDims): first nPCA from trial, remaining spatial dims
            // from the current Data row (unchanged by the probe), final dim
            // = candidate-shifted normalised time.
            const float* trial = trialFeats.data() + ci * nPCA;
            for (int d = 0; d < nPCA; ++d)
                xMinusMu[d] = trial[d] - destMean[d];
            // Pass-through for any non-PCA spatial dims that the cluster
            // carries — leaves them at their current Data[] values.  The
            // probe does not perturb these.
            for (int d = nPCA; d < nDims - 1; ++d)
                xMinusMu[d] = Data.m_Data[p * nDims + d] - destMean[d];
            // candOk[ci] is guaranteed true at this point — the !candOk
            // candidates were already skipped by the `continue` above.
            const float dt = static_cast<float>(ci - N) / sessionSamples;
            xMinusMu[nDims - 1] = (rawTsNorm + dt) - destMean[nDims - 1];

            // Forward substitution: L · y = (x − μ)  →  y
            // chol is lower-triangular row-major: chol[i*nDims + j] for j<=i.
            // Any cluster with a zero diagonal entry → singular → skip.
            float mahal2 = 0.0f;
            bool  bad    = false;
            for (int i = 0; i < nDims; ++i) {
                float s = xMinusMu[i];
                for (int j = 0; j < i; ++j)
                    s -= destChol[i * nDims + j] * yVec[j];
                const float diag = destChol[i * nDims + i];
                if (!(diag > 0.0f)) { bad = true; break; }
                yVec[i] = s / diag;
                mahal2 += yVec[i] * yVec[i];
            }
            if (bad) continue;

            if (ci == N) baselineMahal = mahal2;

            if (mahal2 < bestMahal) {
                bestMahal = mahal2;
                bestCand  = ci;
            }
        }

        // Threshold gate (-TimeShiftAlignScoreThresh): when a non-baseline
        // candidate wins, require its Mahal² improvement over δ=0 to
        // exceed the user-set threshold; otherwise fall back to baseline.
        // With the default 0.0, this gate is a no-op since the argmin
        // guarantees bestMahal ≤ baselineMahal, so `baselineMahal -
        // bestMahal` is always ≥ 0 and never strictly less than 0.
        // Raise the threshold to suppress sub-noise-floor shifts whose
        // Mahal² improvement is dominated by numerical jitter and which,
        // compounded over TimeShiftAlignIter passes, can tighten a wrong
        // post-merge composite mean around spikes that don't really
        // belong (the Phase 6a "reinforce a bad Phase 6 merge" mode).
        if (bestCand != N
            && std::isfinite(baselineMahal)
            && (baselineMahal - bestMahal) < TimeShiftAlignScoreThresh) {
            bestCand  = N;
            bestMahal = baselineMahal;
        }

        const int bestDelta = bestCand - N;
        if (bestDelta == 0) continue;   // no change

        const int wouldBe = baseCum + bestDelta;
        if (std::abs(wouldBe) > m_timeShiftMaxAbs) continue;

        // Commit the per-spike shift.
        float* dataRow = Data.m_Data + p * nDims;
        const float* bestTrial = trialFeats.data() + bestCand * nPCA;
        for (int fi = 0; fi < nPCA; ++fi)
            dataRow[fi] = bestTrial[fi];
        dataRow[timeDimIdx] = rawTsNorm +
            static_cast<float>(bestDelta) / sessionSamples;
        m_cumShift[static_cast<size_t>(p)] = wouldBe;
        ++nChanged;
    }

    (void)nSkippedRead;  // not fatal; partial commits still valid

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (nChanged > 0 && gpu)
        gpu_upload_data(gpu, Data.m_Data);
#endif

    ++m_timeShiftCallCount;
    return nChanged;
}


// ---------------------------------------------------------------------------
// wilsonHilfertyChi2(df, z) — χ²(df, p) inverse CDF via Wilson-Hilferty.
// Accurate to < 1% for df ≥ 4.  Matches the inline formula used elsewhere
// in this file (e.g. KK.cpp:2811, :3521) for MergeThresh calibration
// warnings.  z is the standard normal quantile corresponding to p:
//   p=0.95   → z=1.6449
//   p=0.99   → z=2.326
//   p=0.9999 → z=3.719
// ---------------------------------------------------------------------------
static inline float wilsonHilfertyChi2(int df, float z)
{
    const float d = static_cast<float>(df);
    const float a = 1.0f - 2.0f / (9.0f * d);
    const float b = z * std::sqrt(2.0f / (9.0f * d));
    return d * std::pow(a + b, 3.0f);
}


// ---------------------------------------------------------------------------
// TimeShiftMergeEvaluate
//
// Build a TimeShiftMergePlan for the victim cluster.  Does NOT commit any
// shifts or reassignments — caller inspects `plan.lossReductionTotal` and
// decides whether the merge becomes acceptable.  Pairs with
// TimeShiftMergeCommit.
//
// Complexity: O(nVictimSpikes × kCand × nChan × data2use) projection work
// plus O(nVictimSpikes × kCand × nDims²) Cholesky forward-sub work.
// I/O: one read per victim spike from .spk (cached via m_timeShiftSpkFp).
// ---------------------------------------------------------------------------
bool KK::TimeShiftMergeEvaluate(
    int victim, int nChan, int nSamplesPerSpike,
    TimeShiftMergePlan& plan)
{
    plan = TimeShiftMergePlan{};
    if (!m_timeShiftReady || (!m_timeShiftSpkMap && !m_timeShiftSpkFp)) return false;
    if (!m_timeShiftBasis.valid())            return false;
    if (victim < 0 || victim >= MaxPossibleClusters) return false;
    if (!ClassAlive[victim])                 return false;

    // Collect victim's spike indices.
    std::vector<int> idxs;
    idxs.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == victim) idxs.push_back(p);
    const int nMem = static_cast<int>(idxs.size());
    if (nMem < 1) return false;

    const int waveSamples = nChan * nSamplesPerSpike;
    const int timeDimIdx  = nDims - 1;
    const int nPCA        = m_timeShiftBasis.nChan * m_timeShiftBasis.nComp;
    if (nPCA <= 0 || nPCA > nDims - 1) return false;
    const float sessionSamples = timeRawMax - timeRawMin;
    if (!(sessionSamples > 0.0f)) return false;

    const int N     = m_timeShiftBasis.N;
    const int kCand = m_timeShiftBasis.nCand();
    constexpr int kCandMax = 2 * kTimeShiftNmax + 1;

    if (static_cast<int>(m_timeShiftWaveScratch.size()) < waveSamples)
        m_timeShiftWaveScratch.assign(static_cast<size_t>(waveSamples), 0);

    // Per-spike trial features under every candidate.  Laid out
    // [mi][ci][fi] so a committed spike's features are contiguous.
    std::vector<float> trialFeats(
        static_cast<size_t>(nMem) * kCand * nPCA, 0.0f);
    std::vector<uint8_t> okMask(
        static_cast<size_t>(nMem) * kCand, 0);

    const auto& pca    = m_timeShiftBasis;
    const int   data2use = pca.data2use;
    const int   nComp    = pca.nComp;
    const int   nChanPca = pca.nChan;
    const int   rs       = pca.recShift;
    const bool  isCen    = pca.isCentered;

    // --- Project every victim spike under every candidate δ ----------------
    // Identical math to TimeShiftMergeTighten's projection loop,
    // but we save ALL candidates' features per spike (not just compute-and-
    // discard per-candidate Mahal²) so the caller can commit without a
    // second pass.
    int nRead = 0;
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = idxs[mi];
        if (!TimeShiftReadSpikeWave(p, waveSamples,
                                    m_timeShiftWaveScratch.data())) continue;
        // Bring the read waveform to the current cumulative frame — see
        // the matching commentary in TimeShiftMergeTighten.  Without this,
        // any prior cumShift on a victim spike from an earlier alignment
        // pass causes the candidate-shift selection to be biased by the
        // current cumulative shift's magnitude rather than the deviation
        // from current state.
        ShiftWaveformRowInPlace(m_timeShiftWaveScratch.data(), p,
                                nChan, nSamplesPerSpike);

        const int baseCum = m_cumShift[static_cast<size_t>(p)];
        const int16_t* raw = m_timeShiftWaveScratch.data();

        for (int ci = 0; ci < kCand; ++ci)
            okMask[static_cast<size_t>(mi) * kCand + ci] =
                (std::abs(baseCum + (ci - N)) <= m_timeShiftMaxAbs)
                    ? 1u : 0u;

        for (int ch = 0; ch < nChanPca; ++ch) {
            const double* evs[kCandMax];
            const double* mus[kCandMax];
            for (int ci = 0; ci < kCand; ++ci) {
                evs[ci] = pca.eigvecShifted[ci][ch].data();
                mus[ci] = pca.meanShifted  [ci][ch].data();
            }
            for (int k = 0; k < nComp; ++k) {
                double acc[kCandMax] = {0.0};
                for (int j = 0; j < data2use; ++j) {
                    const int s = rs + j;
                    const double rawV = static_cast<double>(raw[s * nChan + ch]);
                    if (isCen) {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j]
                                     * (rawV - mus[ci][j]);
                    } else {
                        for (int ci = 0; ci < kCand; ++ci)
                            acc[ci] += evs[ci][k * data2use + j] * rawV;
                    }
                }
                const int   fi   = ch * nComp + k;
                const float min_ = dimMin_  [fi];
                const float rng_ = dimRange_[fi];
                for (int ci = 0; ci < kCand; ++ci)
                    trialFeats[(static_cast<size_t>(mi) * kCand + ci) * nPCA + fi] =
                        (static_cast<float>(acc[ci]) - min_) * rng_;
            }
        }
        ++nRead;
    }
    if (nRead == 0) return false;

    // --- Group by Class2 destination & evaluate per-destination δ ----------
    // Per destination: for each candidate δ, aggregate Mahalanobis² over
    // the sub-batch.  Apply χ² threshold to accept non-zero δ.
    std::vector<std::vector<int>> byDest(
        static_cast<size_t>(MaxPossibleClusters));
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = idxs[mi];
        const int d = Class2[p];
        if (d > 0 && d < MaxPossibleClusters && ClassAlive[d])
            byDest[static_cast<size_t>(d)].push_back(mi);
    }

    // χ² threshold per spike (95% quantile of each spike's mahal² under
    // the null hypothesis that it came from this destination cluster).
    const float chi2_95 = wilsonHilfertyChi2(nDims, 1.6449f);

    // Output buffers for the plan.
    plan.globalIdx.assign(static_cast<size_t>(nMem), -1);
    plan.chosenCand.assign(static_cast<size_t>(nMem), N);  // default: no shift
    plan.chosenFeats.assign(static_cast<size_t>(nMem) * nPCA, 0.0f);
    plan.chosenTime.assign(static_cast<size_t>(nMem), 0.0f);
    plan.nPCA = nPCA;

    for (int mi = 0; mi < nMem; ++mi) plan.globalIdx[mi] = idxs[mi];

    // Scratch for forward-substitution
    std::vector<float> yVec(static_cast<size_t>(nDims));
    std::vector<float> xMinusMu(static_cast<size_t>(nDims));

    double totalLossReduction = 0.0;
    int    nDestsShifted      = 0;

    for (int dest = 1; dest < MaxPossibleClusters; ++dest) {
        const auto& subIdxs = byDest[dest];
        const int M = static_cast<int>(subIdxs.size());
        if (M < 1) continue;

        const float* destMean = Mean.m_Data      + dest * nDims;
        const float* destChol = cholFlat.data()  + dest * nDims2;

        // Aggregate Mahalanobis² for each candidate δ over this sub-batch.
        double agg[kCandMax];
        for (int ci = 0; ci < kCand; ++ci) agg[ci] = 0.0;
        std::vector<uint8_t> validSpike(M, 1);   // per-spike bad-Chol flag

        for (int im = 0; im < M; ++im) {
            const int mi = subIdxs[im];
            const int p  = idxs[mi];
            const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];
            for (int ci = 0; ci < kCand; ++ci) {
                if (!okMask[static_cast<size_t>(mi) * kCand + ci]) {
                    // Out-of-range candidate: substitute δ=0 Mahal² so this
                    // candidate cannot "win" by exploiting unreachable
                    // shifts.  Infinite is safer but pollutes aggregates;
                    // δ=0 substitution is the same convention the split and
                    // per-spike merge probes use.
                    // (If δ=0 itself is out-of-range — impossible because
                    // baseCum is already bounded — we'd mark invalid.)
                }
                // Build x = trial features ⊕ pass-through spatial dims ⊕ time_δ
                const float* trial = trialFeats.data()
                    + (static_cast<size_t>(mi) * kCand + ci) * nPCA;
                for (int d = 0; d < nPCA; ++d)
                    xMinusMu[d] = trial[d] - destMean[d];
                for (int d = nPCA; d < nDims - 1; ++d)
                    xMinusMu[d] = Data.m_Data[p * nDims + d] - destMean[d];
                const float dt = okMask[static_cast<size_t>(mi) * kCand + ci]
                    ? static_cast<float>(ci - N) / sessionSamples
                    : 0.0f;
                xMinusMu[nDims - 1] = (rawTsNorm + dt) - destMean[nDims - 1];

                // Forward substitution: L·y = (x−μ)
                bool bad = false;
                float mahal2 = 0.0f;
                for (int i = 0; i < nDims; ++i) {
                    float s = xMinusMu[i];
                    for (int j = 0; j < i; ++j)
                        s -= destChol[i * nDims + j] * yVec[j];
                    const float diag = destChol[i * nDims + i];
                    if (!(diag > 0.0f)) { bad = true; break; }
                    yVec[i] = s / diag;
                    mahal2 += yVec[i] * yVec[i];
                }
                if (bad) {
                    validSpike[im] = 0;
                    break;
                }
                agg[ci] += static_cast<double>(mahal2);
            }
        }

        // Skip destinations with any singular Chol slab — data hasn't
        // converged enough for the probe to be meaningful here.
        int validM = 0;
        for (int im = 0; im < M; ++im) validM += validSpike[im];
        if (validM == 0) continue;

        // χ² threshold: a non-zero δ must beat δ=0 by more than
        // chi2_95 × validM to be accepted.
        const int    baselineCand = N;
        const double baselineAgg  = agg[baselineCand];
        int    bestCand = baselineCand;
        double bestAgg  = baselineAgg;
        const double threshold =
            static_cast<double>(chi2_95) * static_cast<double>(validM);
        for (int ci = 0; ci < kCand; ++ci) {
            if (ci == baselineCand) continue;
            if (agg[ci] < bestAgg - threshold) {
                bestAgg  = agg[ci];
                bestCand = ci;
            }
        }

        // Record per-spike commit data for this sub-batch.
        for (int im = 0; im < M; ++im) {
            const int mi = subIdxs[im];
            if (!validSpike[im]) continue;
            plan.chosenCand[mi] = bestCand;
            const float* trial = trialFeats.data()
                + (static_cast<size_t>(mi) * kCand + bestCand) * nPCA;
            std::memcpy(
                plan.chosenFeats.data() + static_cast<size_t>(mi) * nPCA,
                trial, sizeof(float) * nPCA);
            const int p = idxs[mi];
            const float rawTsNorm = Data.m_Data[p * nDims + timeDimIdx];
            const float dt = okMask[static_cast<size_t>(mi) * kCand + bestCand]
                ? static_cast<float>(bestCand - N) / sessionSamples
                : 0.0f;
            plan.chosenTime[mi] = rawTsNorm + dt;
        }

        // Contribution to total loss reduction (in units of log-probability).
        // LogP uses the convention: higher = worse fit, with the formula
        // 0.5*mahal² + logRootDet - log(Weight) + const.  The δ-dependent
        // part is only 0.5*mahal², so the loss change is:
        //   ΔDeletionLoss(dest) = 0.5 × (bestAgg − baselineAgg)     (≤ 0)
        totalLossReduction += 0.5 * (bestAgg - baselineAgg);
        if (bestCand != baselineCand) ++nDestsShifted;
    }

    plan.valid              = true;
    plan.lossReductionTotal = static_cast<float>(totalLossReduction);

    if (nDestsShifted > 0)
        Output("  [tshift-merge-decision] victim=%d, %d destinations shifted, "
               "ΔDeletionLoss = %.3f\n",
               victim, nDestsShifted, plan.lossReductionTotal);
    return true;
}


// ---------------------------------------------------------------------------
// TimeShiftMergeCommit
// Apply the chosen shifts to Data[] and m_cumShift.  Caller is responsible
// for the subsequent reassignment (Class[p] = Class2[p]) and any follow-up
// post-merge tightening.
// ---------------------------------------------------------------------------
void KK::TimeShiftMergeCommit(const TimeShiftMergePlan& plan)
{
    if (!plan.valid || plan.globalIdx.empty()) return;
    if (plan.nPCA <= 0) return;

    const int timeDimIdx = nDims - 1;
    const int N          = m_timeShiftBasis.N;
    const int nPCA       = plan.nPCA;

    int nWritten = 0;
    const int nMem = static_cast<int>(plan.globalIdx.size());
    for (int mi = 0; mi < nMem; ++mi) {
        const int p = plan.globalIdx[mi];
        if (p < 0 || p >= nPoints) continue;
        const int cand = plan.chosenCand[mi];
        if (cand == N) continue;   // no-shift default; skip

        const int delta   = cand - N;
        const int wouldBe = m_cumShift[p] + delta;
        if (std::abs(wouldBe) > m_timeShiftMaxAbs) continue;

        float* dataRow = Data.m_Data + p * nDims;
        std::memcpy(dataRow,
                    plan.chosenFeats.data() + static_cast<size_t>(mi) * nPCA,
                    sizeof(float) * nPCA);
        dataRow[timeDimIdx] = plan.chosenTime[mi];
        m_cumShift[p] = wouldBe;
        ++nWritten;
    }

#if defined(USE_CUDA) || defined(USE_SYCL) || defined(USE_HIP)
    if (nWritten > 0 && gpu)
        gpu_upload_data(gpu, Data.m_Data);
#endif
}


// ---------------------------------------------------------------------------
// TimeShiftAlignCluster — in-cluster per-spike alignment
//
// Conceptually replaces canonical xcorr realignment (Phase 1a).  For each
// spike in the given cluster, picks the δ ∈ {-N,…,+N} that minimises its
// Mahalanobis² to the cluster's own Gaussian.  Equivalent to xcorr aligning
// each spike to its cluster mean, but weighted by the cluster's covariance
// structure instead of flat waveform overlap — which gives dimensions with
// high discriminative power (usually low-order PCs) more say.
//
// Implementation is a one-line wrapper over TimeShiftMergeTighten, pointed
// at the cluster's own stats instead of a destination's.  Per-spike scope
// is appropriate here: within-cluster alignment IS a per-spike operation.
// ---------------------------------------------------------------------------
int KK::TimeShiftAlignCluster(int clusterId, int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady)                        return 0;
    if (clusterId <= 0 || clusterId >= MaxPossibleClusters) return 0;
    if (!ClassAlive[clusterId])                    return 0;

    std::vector<int> members;
    members.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == clusterId) members.push_back(p);
    if (static_cast<int>(members.size()) < 2) return 0;

    const float* mean = Mean.m_Data     + clusterId * nDims;
    const float* chol = cholFlat.data() + clusterId * nDims2;

    return TimeShiftMergeTighten(members, nChan, nSamplesPerSpike, mean, chol);
}


// ---------------------------------------------------------------------------
// TimeShiftAlignPhase — Phase 1a driver
//
// Iterates alive clusters (skipping noise, and clusters with < 5 spikes
// that aren't worth the I/O) and calls TimeShiftAlignCluster on each.
// Called from the driver at the slot that canonical Phase 1a xcorr used
// to occupy.  Expects fresh MStep + EStep output (Mean + cholFlat current).
//
// Loops up to TimeShiftAlignIter times.  Each pass:
//   1. Iterate alive clusters, calling TimeShiftAlignCluster on each.
//   2. If any spikes shifted, run MStep so the next pass sees updated
//      cluster means (a spike that moved into cluster A now contributes
//      to A's mean before the next per-spike alignment decision).
//   3. Exit early when a pass produces zero shifts (converged).
//
// Between passes: MStep refreshes Mean/Cov, then a focused Cholesky-only
// refresh recomputes cholFlat from the new Cov.  Skipping the full EStep
// avoids the per-(point, cluster) LogP recompute, which alignment scoring
// doesn't need.  An EStep is still required before the NEXT phase that
// consumes LogP, but that's the caller's responsibility — the chunked-CEM
// driver runs MStep+EStep after Phase 1a in any case.
//
// Note: an earlier version of this function called only MStep() between
// passes and assumed cholFlat would be refreshed too.  It isn't — cholFlat
// lives inside EStep (KK::EStep, ~line 565).  Pass N+1 was therefore
// scoring with updated Mean against pre-shift Cholesky factors, an
// inconsistency that grew with each iteration.  The Cholesky-only refresh
// below mirrors the pattern at the provisional seeding site (~line 3199)
// and keeps Mean / Cov / cholFlat all in sync without paying for LogP.
//
// Returns the cumulative count of spikes whose shifts changed across
// all passes and clusters.
// ---------------------------------------------------------------------------
int KK::TimeShiftAlignPhase(int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady) return 0;
    if (TimeShiftAlignIter <= 0) return 0;

    int totalShifted = 0;

    for (int pass = 0; pass < TimeShiftAlignIter; ++pass) {
        int passShifted = 0;
        int nClustersProcessed = 0;
        for (int c = 1; c < MaxPossibleClusters; ++c) {
            if (!ClassAlive[c]) continue;
            const int shifted = TimeShiftAlignCluster(c, nChan, nSamplesPerSpike);
            if (shifted > 0) ++nClustersProcessed;
            passShifted += shifted;
        }
        totalShifted += passShifted;

        if (passShifted > 0) {
            Output("[Align] pass %d/%d: %d spikes shifted across "
                   "%d clusters\n",
                   pass + 1, TimeShiftAlignIter, passShifted, nClustersProcessed);
            // Refresh Mean/Cov so the next pass aligns against post-shift
            // centres, then refresh cholFlat from the new Cov.  Without
            // this Cholesky refresh, pass N+1 would score against stale
            // covariance factors.  Singular clusters are deleted in place
            // (matches EStep's behaviour at KK.cpp:565-572).
            MStep();
            for (int cc = 1; cc < nClustersAlive; ++cc) {
                const int c = AliveIndex[cc];
                if (Cholesky(Cov.m_Data + c * nDims2,
                             cholFlat.data() + c * nDims2, nDims)) {
                    if (Verbose >= 2)
                        Output("[Align] class %d deleted: covariance "
                               "matrix is singular after shifts\n", c);
                    ClassAlive[c] = 0;
                }
            }
            Reindex();
        } else {
            // Converged — no spikes changed shift; further passes can't
            // change anything either.
            if (pass > 0)
                Output("[Align] converged after %d pass(es)\n", pass + 1);
            break;
        }
    }

    if (totalShifted > 0)
        Output("[Align] Cluster alignment: %d total spike-shifts\n",
               totalShifted);
    return totalShifted;
}




// ---------------------------------------------------------------------------
// EnergyCOMRealignPhase — per-cluster centre-of-energy realignment
//
// For each alive cluster, computes the MEAN waveform across all spikes
// assigned to that cluster, then the weighted-mean time of the per-sample
// channel-summed energy of that mean waveform:
//
//     mean_x(t, c) = ( 1/N ) · Σ_{p ∈ cluster}  x_p(t, c)
//     e(t)  = Σ_c |mean_x(t, c)|²       (EnergyCOMMetric = 1, default)
//           | Σ_c |mean_x(t, c)|        (EnergyCOMMetric = 0)
//     t_COM = ( Σ_t  t · e(t) ) / ( Σ_t e(t) )
//
// Then computes the integer shift δ_COM = round(t_COM − PeakSampleIndex)
// and adds it to m_cumShift[p] for every spike p in the cluster — the
// SAME delta to all members.  Computing the COM on the cluster mean rather
// than on individual spikes (a) averages out per-spike capture-noise
// (an individual spike's "energy COM" is a poor estimator with high
// variance), and (b) yields a single coherent direction per cluster, so
// the cluster's relative geometry in PCA space is preserved.  Per-spike
// COM realignment, by contrast, scatters cluster members in inconsistent
// directions and disrupts the cluster shape that downstream EM has just
// converged on.
//
// The per-cluster shift is ADDITIVE to whatever a preceding cluster-mean
// alignment (TimeShiftAlignPhase) wrote — the two are complementary:
// cluster-mean handles cluster-fit drift in PCA space, energy-COM handles
// waveform-anchor drift in time.  If applying delta to any cluster
// member would push its cumulative shift past ±m_timeShiftMaxAbs, the
// whole cluster is skipped rather than clamped per-spike (partial shifts
// would break the very uniformity this phase is meant to preserve).
//
// After updating m_cumShift, RefeaturizeFromShifts re-extracts the
// shifted spikes from .fil at the new sample offsets, projects through
// the saved PCA eigenvectors, re-normalises, and writes the new features
// into Data[].  This is the same machinery used by Phase 9
// (TimeShiftFinalize) for the final disk commit, just invoked mid-run.
//
// Reads the ORIGINAL .spk waveform per spike (TimeShiftReadSpikeWave
// does not account for m_cumShift), so the cluster mean is built from
// waveforms as originally captured — independent of any prior shift
// already in m_cumShift.  The additive update means the final shift is
// the SUM of all prior contributions plus the new COM correction, which
// is what Phase 9 / RefeaturizeFromShifts consume.
//
// Cost notes:
//  - O(nPoints · waveSamples) for the gather + accumulate step (one
//    pass over members per cluster; spike read from .spk dominates).
//  - The summed-waveform energy COM uses a double accumulator (sumWave)
//    so int16 sums across thousands of spikes do not overflow.
//  - Energy COM is computed on the SUMMED waveform — equivalent up to a
//    global scale factor (preserved by the t_COM ratio) to computing it
//    on the MEAN; saves the divide-by-N pass.
//
// Returns the total number of spikes whose m_cumShift was updated.
// ---------------------------------------------------------------------------
int KK::EnergyCOMRealignPhase(int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady) return 0;
    if (EnergyCOMRealign == 0) return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0) return 0;
    if (m_cumShift.empty()) return 0;

    const int waveSamples = nChan * nSamplesPerSpike;
    std::vector<int16_t> waveScratch(static_cast<size_t>(waveSamples), 0);
    std::vector<double>  sumWave   (static_cast<size_t>(waveSamples), 0.0);
    std::vector<int>     members;
    members.reserve(1024);

    const double peakRef = static_cast<double>(PeakSampleIndex);
    int nClustersShifted   = 0;
    int nSpikesShifted     = 0;
    int nClustersTooSmall  = 0;
    int nClustersBadRead   = 0;
    int nClustersSilent    = 0;
    int nClustersZeroShift = 0;
    int nClustersClamped   = 0;

    for (int c = 1; c < MaxPossibleClusters; ++c) {
        if (!ClassAlive[c]) continue;

        // Gather members of cluster c.
        members.clear();
        for (int p = 0; p < nPoints; ++p)
            if (Class[p] == c) members.push_back(p);
        if (members.size() < 5) { ++nClustersTooSmall; continue; }

        // Sum the .spk-captured waveforms across all members.  The COM is
        // scale-invariant so we don't need to divide by N — a double
        // accumulator avoids int16 overflow when N is large.
        std::fill(sumWave.begin(), sumWave.end(), 0.0);
        int nRead = 0;
        for (int p : members) {
            if (!TimeShiftReadSpikeWave(p, waveSamples, waveScratch.data())) continue;
            for (int i = 0; i < waveSamples; ++i)
                sumWave[i] += static_cast<double>(waveScratch[i]);
            ++nRead;
        }
        if (nRead == 0) { ++nClustersBadRead; continue; }

        // Energy COM on the summed-waveform — equivalent to MEAN-waveform
        // COM since the ratio cancels the 1/N factor.  This is the energy
        // profile of the cluster's typical waveform shape, with per-spike
        // capture noise averaged out.
        double totalE    = 0.0;
        double weightedT = 0.0;
        for (int t = 0; t < nSamplesPerSpike; ++t) {
            double e = 0.0;
            const double* row = sumWave.data() + t * nChan;
            if (EnergyCOMMetric == 0) {
                for (int ch = 0; ch < nChan; ++ch) e += std::fabs(row[ch]);
            } else {
                for (int ch = 0; ch < nChan; ++ch) {
                    const double x = row[ch];
                    e += x * x;
                }
            }
            totalE    += e;
            weightedT += static_cast<double>(t) * e;
        }
        if (!(totalE > 0.0)) { ++nClustersSilent; continue; }

        const double tCOM  = weightedT / totalE;
        const int    delta = static_cast<int>(std::lround(tCOM - peakRef));
        if (delta == 0) { ++nClustersZeroShift; continue; }

        // Cap check across all members: if applying delta would push ANY
        // member's cumulative shift past the cap, skip the whole cluster.
        // Clamping only the over-cap members would scatter the cluster
        // (some spikes shifted by delta, others by less), defeating the
        // per-cluster-uniform design.
        bool anyExceeds = false;
        for (int p : members) {
            const int wouldBe = m_cumShift[static_cast<size_t>(p)] + delta;
            if (std::abs(wouldBe) > m_timeShiftMaxAbs) { anyExceeds = true; break; }
        }
        if (anyExceeds) { ++nClustersClamped; continue; }

        // Apply the SAME delta to every spike in the cluster.
        for (int p : members)
            m_cumShift[static_cast<size_t>(p)] += delta;

        ++nClustersShifted;
        nSpikesShifted += static_cast<int>(members.size());
    }

    Output("[EnergyCOM] %d clusters realigned (%d spikes total; peak ref = "
           "sample %d, cap = ±%d); skipped: %d small, %d bad-read, %d silent, "
           "%d zero-shift, %d cap-clamped\n",
           nClustersShifted, nSpikesShifted, PeakSampleIndex,
           m_timeShiftMaxAbs, nClustersTooSmall, nClustersBadRead,
           nClustersSilent, nClustersZeroShift, nClustersClamped);

    if (nSpikesShifted > 0) {
        // Re-extract + re-featurize all currently-shifted spikes from .fil
        // at their NEW (cumulative) sample offsets, projecting through the
        // saved PCA eigenvectors and writing fresh features into Data[].
        RefeaturizeFromShifts(m_cumShift, nChan, nSamplesPerSpike);
    }

    return nSpikesShifted;
}


// ---------------------------------------------------------------------------
// KlustersStyleRealignAllClusters — Phase 7c
//
// Klusters-faithful per-spike realignment.  See KK.h for the algorithmic
// description.  This is the same algorithm that powers klusters' interactive
// "Realign top-ch" button (src/klusters/src/spikerealign.cpp); KKE reuses
// the same XcorrDispatch library and just supplies its own per-cluster
// spike lists and waveform reader.
//
// Per-cluster cost (N = cluster size, C = nChan, S = nSamples, M = maxShift):
//   Mean build      O(N·C·S)            CPU memcpy + accumulate
//   Pre-align       O(C·S)              CPU
//   xcorr           O(N·C·S·M)          GPU when available, OpenMP otherwise
//
// Whole-session cost on the user's sirotaA-jg-000005 data (189k spikes, 8
// channels, 32 samples, maxShift=8) is ~1-2 seconds end-to-end on CPU and
// well under 1 second on the RTX 5070 Ti.
//
// Sign convention (matches klusters spikerealign.cpp:669):
//   m_cumShift[p] += newShift  →  re-extraction reads from .fil starting
//   `newShift` samples LATER (or earlier when newShift is negative).
//
// Caller is responsible for:
//   • Running MStep + EStep beforehand if cluster means need to be fresh
//     (this function uses the .spk content, not Data[], so the cluster mean
//     is computed from the raw waveform — independent of stale Class[]
//     assignments).
//   • Calling RefeaturizeFromShifts (via TimeShiftFinalize) afterward to
//     turn the m_cumShift updates into actual .spk/.fet writes.
// ---------------------------------------------------------------------------
int KK::KlustersStyleRealignAllClusters(int nChan, int nSamplesPerSpike)
{
    if (nChan <= 0 || nSamplesPerSpike <= 0) return 0;
    if (!m_timeShiftReady) {
        // No .spk probe initialised — without it we can't read waveforms.
        // Skip silently (caller decides whether to warn).
        return 0;
    }
    if (PeakSampleIndex < 0 || PeakSampleIndex >= nSamplesPerSpike) {
        LockedStderr(
                "[Stage 3.6] PeakSampleIndex=%d out of [0,%d) — skipping "
                "klusters-style realignment.\n",
                PeakSampleIndex, nSamplesPerSpike);
        return 0;
    }

    const int waveSamples = nChan * nSamplesPerSpike;
    const int peakPos     = PeakSampleIndex;
    const int maxShift    = std::max(1, std::min(nSamplesPerSpike / 4,
                                                  KlustersRealignMaxShift));
    const int minSize     = std::max(2, KlustersRealignMinSize);

    // Group spike global indices by cluster ID.  Using nClustersAlive +
    // AliveIndex[] gives us live clusters only; the noise cluster (id 0)
    // is skipped explicitly.
    std::vector<std::vector<int>> clusterSpikes(MaxPossibleClusters);
    for (int p = 0; p < nPoints; ++p) {
        const int c = Class[p];
        if (c <= 0 || c >= MaxPossibleClusters) continue;  // noise / OOB
        clusterSpikes[static_cast<size_t>(c)].push_back(p);
    }

    KlustersRealign::RealignStats stats;
    long long sumAbsShift = 0;
    int       nSpikesChanged = 0;

    // Scratch buffer reused across clusters — sized to the largest cluster.
    std::vector<int16_t> waveBuf;       // flat: nSpikes × waveSamples
    std::vector<int>     shifts;
    std::vector<float>   scores;

    // ── Per-cluster minimum-variance pass selection (patch 0061) ─────────
    // Alignment is independent per cluster (each aligns to its OWN mean), so
    // each cluster reaches its residual-variance minimum at its OWN pass — a
    // high-SNR unit may bottom out at pass 2 while a faint one keeps
    // tightening.  Selection is therefore PER CLUSTER: track each cluster's
    // least residual variance and the m_cumShift of its spikes at that pass,
    // and restore each cluster independently before the disk write.  (A single
    // global best-pass would force every cluster onto whichever pass won the
    // SUM — strictly worse, and nothing couples the clusters to justify it.)
    const bool selMinVar =
        (KlustersRealignSelectMinVariance != 0 && KlustersRealignIters > 1);
    std::vector<double> bestSSEPerCluster;
    std::vector<int>    bestCumPerSpike;
    if (selMinVar) {
        bestSSEPerCluster.assign(static_cast<size_t>(MaxPossibleClusters),
                                 std::numeric_limits<double>::infinity());
        bestCumPerSpike = m_cumShift;     // spikes never realigned keep their shift
    }

    // Residual sum-of-squares of the N spikes currently in waveBuf about
    // their per-sample mean — the waveform variance about the cluster's
    // template, which is what the alignment minimises.
    auto clusterResidualSSE = [&](int N) -> double {
        std::vector<double> mean(static_cast<size_t>(waveSamples), 0.0);
        for (int i = 0; i < N; ++i)
            for (int e = 0; e < waveSamples; ++e)
                mean[static_cast<size_t>(e)] +=
                    waveBuf[static_cast<size_t>(i) * waveSamples + e];
        const double invN = 1.0 / N;
        for (int e = 0; e < waveSamples; ++e)
            mean[static_cast<size_t>(e)] *= invN;
        double sse = 0.0;
        for (int i = 0; i < N; ++i)
            for (int e = 0; e < waveSamples; ++e) {
                const double d =
                    static_cast<double>(
                        waveBuf[static_cast<size_t>(i) * waveSamples + e])
                    - mean[static_cast<size_t>(e)];
                sse += d * d;
            }
        return sse;
    };

    // ── Iterate to convergence (patch 0060) ──────────────────────────────
    // Each pass rebuilds the per-cluster mean from the CURRENTLY-aligned
    // waveforms — TimeShiftReadSpikeWave honours the m_cumShift committed by
    // earlier passes — so the template sharpens and a later pass picks up the
    // residual shifts a single pass leaves behind.  This is what klusters'
    // interactive "Realign" does when pressed repeatedly; a single pass stops
    // short of the fixed point because its template is built from jittered
    // spikes.  KlustersRealignIters defaults to 1 (original single-pass
    // behaviour); >1 iterates, breaking early once a pass moves no spike.
    const int maxRealignIters = std::max(1, KlustersRealignIters);
    int rItersUsed = 0;
    for (int rIter = 0; rIter < maxRealignIters; ++rIter) {
    int changedThisIter = 0;
    ++rItersUsed;
    for (int cc = 0; cc < nClustersAlive; ++cc) {
        const int c = AliveIndex[cc];
        if (c <= 0 || c >= MaxPossibleClusters) continue;  // skip noise
        const auto& pts = clusterSpikes[static_cast<size_t>(c)];
        const int N     = static_cast<int>(pts.size());

        if (N < minSize) { ++stats.nClustersSkipped; continue; }

        // ── Read all N spikes' waveforms from .spk (or .spkD) ─────────
        waveBuf.assign(static_cast<size_t>(N) * waveSamples, 0);
        int nReadOk = 0;
        for (int i = 0; i < N; ++i) {
            int16_t* dst = waveBuf.data() +
                           static_cast<ptrdiff_t>(i) * waveSamples;
            if (TimeShiftReadSpikeWave(pts[static_cast<size_t>(i)],
                                       waveSamples, dst)) {
                ++nReadOk;
            } else {
                // Zero-fill the failed slot; it will contribute zero to the
                // mean and the xcorr will return shift=0 for it.
                std::memset(dst, 0,
                            static_cast<size_t>(waveSamples) *
                            sizeof(int16_t));
                ++stats.nSpikesReadFailed;
            }
        }
        if (nReadOk < minSize) {
            ++stats.nClustersSkipped;
            continue;
        }

        // Per-cluster min-variance snapshot (patch 0061).  waveBuf reflects
        // this cluster's state at the START of this pass (its shift below is
        // not yet committed).  If that is this cluster's tightest seen so far,
        // remember its spikes' current shifts.
        if (selMinVar) {
            const double sse = clusterResidualSSE(N);
            if (sse < bestSSEPerCluster[static_cast<size_t>(c)]) {
                bestSSEPerCluster[static_cast<size_t>(c)] = sse;
                for (int i = 0; i < N; ++i)
                    bestCumPerSpike[static_cast<size_t>(pts[i])] =
                        m_cumShift[static_cast<size_t>(pts[i])];
            }
        }

        // ── Compute per-spike shifts via XcorrDispatch ────────────────
        const bool ok = KlustersRealign::ComputeClusterShiftsFlat(
            waveBuf.data(), N,
            nChan, nSamplesPerSpike, peakPos, maxShift,
            shifts, scores);
        if (!ok) { ++stats.nClustersSkipped; continue; }

        // ── Commit shifts to m_cumShift, clamping to maxAbs.
        //    Klusters writes back to .fil immediately; here we let
        //    TimeShiftFinalize handle that at the end of the run.
        for (int i = 0; i < N; ++i) {
            const int p  = pts[static_cast<size_t>(i)];
            const int sh = shifts[static_cast<size_t>(i)];
            if (sh == 0) continue;

            const int oldCum = m_cumShift[static_cast<size_t>(p)];
            int       newCum = oldCum + sh;
            if (newCum >  m_timeShiftMaxAbs) newCum =  m_timeShiftMaxAbs;
            if (newCum < -m_timeShiftMaxAbs) newCum = -m_timeShiftMaxAbs;
            if (newCum == oldCum) continue;  // clamped out

            m_cumShift[static_cast<size_t>(p)] = newCum;
            ++nSpikesChanged;
            ++changedThisIter;
            sumAbsShift += std::abs(sh);
            stats.maxAbsShift = std::max(stats.maxAbsShift, std::abs(sh));
        }

        stats.nSpikesEvaluated += N;
        stats.nSpikesRealigned += static_cast<int>(
            std::count_if(shifts.begin(), shifts.end(),
                          [](int s){ return s != 0; }));
        ++stats.nClustersProcessed;
    }
        if (Verbose >= 1 && maxRealignIters > 1)
            LockedStderr("[Stage 3.6] realign pass %d/%d: %d spikes shifted\n",
                         rIter + 1, maxRealignIters, changedThisIter);
        if (changedThisIter == 0) break;   // converged — no spike moved
    }  // end realign-iteration loop

    // ── Restore each cluster's own minimum-variance shifts (patch 0061) ──
    // The final state must compete too (the start-of-pass probe never saw the
    // last pass's commits unless it converged), so re-read each cluster once
    // more, snapshot if it is now tighter, then restore PER CLUSTER.  Spikes
    // in clusters never realigned keep their original shifts (bestCumPerSpike
    // was seeded from m_cumShift).
    if (selMinVar) {
        int nRolledBack = 0, nEligible = 0;
        for (int cc = 0; cc < nClustersAlive; ++cc) {
            const int c = AliveIndex[cc];
            if (c <= 0 || c >= MaxPossibleClusters) continue;
            const auto& pts = clusterSpikes[static_cast<size_t>(c)];
            const int N = static_cast<int>(pts.size());
            if (N < minSize) continue;
            waveBuf.assign(static_cast<size_t>(N) * waveSamples, 0);
            int nReadOk = 0;
            for (int i = 0; i < N; ++i) {
                int16_t* dst = waveBuf.data() +
                               static_cast<ptrdiff_t>(i) * waveSamples;
                if (TimeShiftReadSpikeWave(pts[static_cast<size_t>(i)],
                                           waveSamples, dst)) ++nReadOk;
                else std::memset(dst, 0,
                                 static_cast<size_t>(waveSamples) *
                                 sizeof(int16_t));
            }
            if (nReadOk < minSize) continue;
            ++nEligible;
            const double sse = clusterResidualSSE(N);
            if (sse < bestSSEPerCluster[static_cast<size_t>(c)]) {
                bestSSEPerCluster[static_cast<size_t>(c)] = sse;
                for (int i = 0; i < N; ++i)
                    bestCumPerSpike[static_cast<size_t>(pts[i])] =
                        m_cumShift[static_cast<size_t>(pts[i])];
            }
            // Did this cluster's best pass differ from its final state?
            bool rolled = false;
            for (int i = 0; i < N; ++i)
                if (bestCumPerSpike[static_cast<size_t>(pts[i])] !=
                    m_cumShift[static_cast<size_t>(pts[i])]) { rolled = true; break; }
            if (rolled) ++nRolledBack;
        }
        m_cumShift = bestCumPerSpike;
        LockedStderr("[Stage 3.6] min-variance select: %d/%d clusters rolled "
                     "back to an earlier (tighter) pass\n",
                     nRolledBack, nEligible);
    }

    // ── RMS circular group-recenter (centering phase) ────────────────────
    // Matches the Klusters interactive recenter (klustersdoc --recenter-rms):
    // after per-spike alignment, shift each cluster as a whole so its energy
    // envelope sits at peakPos.  The anchor is the circular weighted mean of
    // the per-sample RMS² profile (energy across the group, summed over
    // channels); the sample axis is periodic so a circular mean is the correct
    // centroid, and it is exactly invariant to the RMS noise floor (the Nth
    // roots of unity sum to zero).  The mean resultant length R guards the
    // degenerate multi-lobe case: below KlustersRealignRMin the cluster keeps
    // its per-spike alignment.
    //
    // Reads are the ORIGINAL .spk slots (TimeShiftReadSpikeWave ignores
    // m_cumShift), so each spike is rolled in-memory by its committed
    // m_cumShift to form the aligned group — the same waveform TimeShiftFinalize
    // will ultimately re-extract.  The per-cluster offset is then folded into
    // every member's m_cumShift (clamped at ±m_timeShiftMaxAbs).
    //
    // Gated by KlustersRealignCenterMode == 2 (RMS).  The default mode (1, PCA)
    // leaves centering to the PCA alignment phases; mode 0 disables it.
    if (KlustersRealignCenterMode == 2) {
        const float  rMin   = KlustersRealignRMin;
        std::vector<int16_t> row(static_cast<size_t>(waveSamples));
        std::vector<double>  energy(static_cast<size_t>(nSamplesPerSpike));
        int nRecentered = 0;

        for (int cc = 0; cc < nClustersAlive; ++cc) {
            const int c = AliveIndex[cc];
            if (c <= 0 || c >= MaxPossibleClusters) continue;
            const auto& pts = clusterSpikes[static_cast<size_t>(c)];
            const int N = static_cast<int>(pts.size());
            if (N < minSize) continue;

            std::fill(energy.begin(), energy.end(), 0.0);
            int nReadOk = 0;
            for (int i = 0; i < N; ++i) {
                const int p = pts[static_cast<size_t>(i)];
                if (!TimeShiftReadSpikeWave(p, waveSamples, row.data())) continue;
                ++nReadOk;
                const int sh = m_cumShift[static_cast<size_t>(p)];
                // Roll original window by committed shift -> aligned waveform.
                for (int s = 0; s < nSamplesPerSpike; ++s) {
                    const int src = (s + sh + nSamplesPerSpike) % nSamplesPerSpike;
                    double acc = 0.0;
                    for (int ch = 0; ch < nChan; ++ch) {
                        const double v = static_cast<double>(
                            row[static_cast<size_t>(src * nChan + ch)]);
                        acc += v * v;
                    }
                    energy[static_cast<size_t>(s)] += acc;
                }
            }
            if (nReadOk < minSize) continue;

            const realign_center::RecenterResult rc =
                realign_center::circularRecenterShift(
                    energy.data(), nSamplesPerSpike, peakPos, rMin);
            if (!rc.applied) continue;       // degenerate envelope — leave aligned
            const int dg = rc.shift;
            if (dg == 0) continue;

            for (int i = 0; i < N; ++i) {
                const int p   = pts[static_cast<size_t>(i)];
                int       nc  = m_cumShift[static_cast<size_t>(p)] + dg;
                if (nc >  m_timeShiftMaxAbs) nc =  m_timeShiftMaxAbs;
                if (nc < -m_timeShiftMaxAbs) nc = -m_timeShiftMaxAbs;
                m_cumShift[static_cast<size_t>(p)] = nc;
            }
            ++nRecentered;
        }
        LockedStderr("[Stage 3.6] RMS recenter: %d cluster(s) recentred "
                     "(rmin=%.2f).\n", nRecentered,
                     static_cast<double>(rMin));
    }

    stats.meanAbsShift = stats.nSpikesEvaluated > 0
        ? static_cast<double>(sumAbsShift) / stats.nSpikesEvaluated
        : 0.0;

    LockedStderr(
            "[Stage 3.6] KlustersStyle realignment: "
            "%d clusters processed (%d skipped), "
            "%d/%d spikes shifted, "
            "mean|Δ|=%.2f samples (max %d), "
            "%d read failures, %d pass(es).\n",
            stats.nClustersProcessed, stats.nClustersSkipped,
            stats.nSpikesRealigned, stats.nSpikesEvaluated,
            stats.meanAbsShift, stats.maxAbsShift,
            stats.nSpikesReadFailed, rItersUsed);

    return nSpikesChanged;
}


// ---------------------------------------------------------------------------
// KK::KlustersStyleRealignOneCluster
//
// Per-cluster body of the realign algorithm — shared by the global
// AllClusters variant (above) and the per-chunk variant (below).  Reads
// each spike's waveform via TimeShiftReadSpikeWave (which honours
// m_cumShift), computes optimal integer shifts via
// KlustersRealign::ComputeClusterShiftsFlat, and commits to m_cumShift
// (clamped at ±m_timeShiftMaxAbs).
//
// Spikes whose committed shift actually changed are appended to
// outChangedSpikeIds so the caller can drive a focused refeaturize
// pass over just those spikes.
//
// Returns nSpikesChanged for this cluster.  stats accumulates across
// calls.
// ---------------------------------------------------------------------------
int KK::KlustersStyleRealignOneCluster(
        const std::vector<int>& spikeGlobalIds,
        int nChan, int nSamplesPerSpike,
        int peakPos, int maxShift, int minSize,
        std::vector<int>& outChangedSpikeIds,
        KlustersRealign::RealignStats& stats)
{
    const int waveSamples = nChan * nSamplesPerSpike;
    const int N           = static_cast<int>(spikeGlobalIds.size());
    if (N < minSize) { ++stats.nClustersSkipped; return 0; }

    // ── Read all N spikes' waveforms ──
    std::vector<int16_t> waveBuf(static_cast<size_t>(N) * waveSamples, 0);
    int nReadOk = 0;
    for (int i = 0; i < N; ++i) {
        int16_t* dst = waveBuf.data() +
                       static_cast<ptrdiff_t>(i) * waveSamples;
        if (TimeShiftReadSpikeWave(spikeGlobalIds[static_cast<size_t>(i)],
                                   waveSamples, dst)) {
            ++nReadOk;
        } else {
            std::memset(dst, 0,
                        static_cast<size_t>(waveSamples) * sizeof(int16_t));
            ++stats.nSpikesReadFailed;
        }
    }
    if (nReadOk < minSize) { ++stats.nClustersSkipped; return 0; }

    // ── Compute per-spike shifts ──
    std::vector<int>   shifts;
    std::vector<float> scores;
    const bool ok = KlustersRealign::ComputeClusterShiftsFlat(
        waveBuf.data(), N,
        nChan, nSamplesPerSpike, peakPos, maxShift,
        shifts, scores);
    if (!ok) { ++stats.nClustersSkipped; return 0; }

    // ── Snapshot entry shifts so change detection covers both the per-spike
    //    commit and the (uniform) recenter without double-counting a spike in
    //    outChangedSpikeIds. ──
    std::vector<int> entryCum(static_cast<size_t>(N));
    for (int i = 0; i < N; ++i)
        entryCum[static_cast<size_t>(i)] = m_cumShift[
            static_cast<size_t>(spikeGlobalIds[static_cast<size_t>(i)])];

    // ── Commit per-spike shifts (clamped) ──
    int maxAbs = 0;
    for (int i = 0; i < N; ++i) {
        const int p  = spikeGlobalIds[static_cast<size_t>(i)];
        const int sh = shifts[static_cast<size_t>(i)];
        if (sh == 0) continue;

        int newCum = entryCum[static_cast<size_t>(i)] + sh;
        if (newCum >  m_timeShiftMaxAbs) newCum =  m_timeShiftMaxAbs;
        if (newCum < -m_timeShiftMaxAbs) newCum = -m_timeShiftMaxAbs;
        m_cumShift[static_cast<size_t>(p)] = newCum;
        if (std::abs(sh) > maxAbs) maxAbs = std::abs(sh);
    }

    // ── RMS circular recenter (centering phase, gated by mode 2) ──
    // Mirrors KlustersStyleRealignAllClusters: form the aligned group (the
    // original .spk rolled by the committed m_cumShift) and shift the whole
    // cluster onto peakPos via the shared circular centroid.  Uniform offset.
    if (KlustersRealignCenterMode == 2) {
        std::vector<double> energy(static_cast<size_t>(nSamplesPerSpike), 0.0);
        for (int i = 0; i < N; ++i) {
            const int16_t* w = waveBuf.data()
                + static_cast<ptrdiff_t>(i) * waveSamples;
            const int cum = m_cumShift[static_cast<size_t>(
                spikeGlobalIds[static_cast<size_t>(i)])];
            for (int s = 0; s < nSamplesPerSpike; ++s) {
                const int src = (s + cum + nSamplesPerSpike) % nSamplesPerSpike;
                double acc = 0.0;
                for (int ch = 0; ch < nChan; ++ch) {
                    const double v = static_cast<double>(
                        w[static_cast<size_t>(src * nChan + ch)]);
                    acc += v * v;
                }
                energy[static_cast<size_t>(s)] += acc;
            }
        }
        const realign_center::RecenterResult rc =
            realign_center::circularRecenterShift(
                energy.data(), nSamplesPerSpike, peakPos, KlustersRealignRMin);
        if (rc.applied && rc.shift != 0) {
            for (int i = 0; i < N; ++i) {
                const int p = spikeGlobalIds[static_cast<size_t>(i)];
                int nc = m_cumShift[static_cast<size_t>(p)] + rc.shift;
                if (nc >  m_timeShiftMaxAbs) nc =  m_timeShiftMaxAbs;
                if (nc < -m_timeShiftMaxAbs) nc = -m_timeShiftMaxAbs;
                m_cumShift[static_cast<size_t>(p)] = nc;
            }
        }
    }

    // ── Report changed spikes (per-spike commit + recenter), no duplicates ──
    int nChanged = 0;
    for (int i = 0; i < N; ++i) {
        const int p = spikeGlobalIds[static_cast<size_t>(i)];
        if (m_cumShift[static_cast<size_t>(p)]
                != entryCum[static_cast<size_t>(i)]) {
            outChangedSpikeIds.push_back(p);
            ++nChanged;
        }
    }

    stats.nSpikesEvaluated += N;
    stats.nSpikesRealigned += static_cast<int>(
        std::count_if(shifts.begin(), shifts.end(),
                      [](int s){ return s != 0; }));
    stats.maxAbsShift = std::max(stats.maxAbsShift, maxAbs);
    ++stats.nClustersProcessed;
    return nChanged;
}


// ---------------------------------------------------------------------------
// KK::RealignSpikesAgainstTemplate
//
// Incremental-realign primitive (patch 0054): align a subset of spikes
// (e.g. the spikes just absorbed into a canonical by a merge) against an
// EXTERNALLY-supplied template (the canonical's post-merge meanWav),
// committing the resulting shifts to m_cumShift.  Unlike
// KlustersStyleRealignOneCluster, the template is NOT built from the
// passed spikes — so aligning a handful of newly-merged spikes uses the
// canonical's actual shape, not a template derived from the few spikes.
//
// tmplSampleMajor: [nChan*nSamples] int16, sample-major (s*nChan+ch),
//                  i.e. the ChunkModel::meanWav / median layout.
// Returns nSpikesChanged; appends changed global IDs to outChangedSpikeIds.
// ---------------------------------------------------------------------------
int KK::RealignSpikesAgainstTemplate(
        const std::vector<int>& spikeGlobalIds,
        const int16_t* tmplSampleMajor,
        int nChan, int nSamplesPerSpike,
        int peakPos, int maxShift,
        std::vector<int>& outChangedSpikeIds,
        KlustersRealign::RealignStats& stats)
{
    (void)peakPos;
    const int waveSamples = nChan * nSamplesPerSpike;
    const int N           = static_cast<int>(spikeGlobalIds.size());
    if (N <= 0 || !tmplSampleMajor) return 0;

    std::vector<int16_t> waveBuf(static_cast<size_t>(N) * waveSamples, 0);
    int nReadOk = 0;
    for (int i = 0; i < N; ++i) {
        int16_t* dst = waveBuf.data() +
                       static_cast<ptrdiff_t>(i) * waveSamples;
        if (TimeShiftReadSpikeWave(spikeGlobalIds[static_cast<size_t>(i)],
                                   waveSamples, dst)) {
            ++nReadOk;
        } else {
            std::memset(dst, 0,
                        static_cast<size_t>(waveSamples) * sizeof(int16_t));
            ++stats.nSpikesReadFailed;
        }
    }
    if (nReadOk == 0) { ++stats.nClustersSkipped; return 0; }

    std::vector<int>   shifts;
    std::vector<float> scores;
    const bool ok = KlustersRealign::ComputeShiftsAgainstTemplateFlat(
        waveBuf.data(), N, tmplSampleMajor,
        nChan, nSamplesPerSpike, peakPos, maxShift, shifts, scores);
    if (!ok) { ++stats.nClustersSkipped; return 0; }

    int nChanged = 0, maxAbs = 0;
    for (int i = 0; i < N; ++i) {
        const int p  = spikeGlobalIds[static_cast<size_t>(i)];
        const int sh = shifts[static_cast<size_t>(i)];
        if (sh == 0) continue;
        const int oldCum = m_cumShift[static_cast<size_t>(p)];
        int       newCum = oldCum + sh;
        if (newCum >  m_timeShiftMaxAbs) newCum =  m_timeShiftMaxAbs;
        if (newCum < -m_timeShiftMaxAbs) newCum = -m_timeShiftMaxAbs;
        if (newCum == oldCum) continue;
        m_cumShift[static_cast<size_t>(p)] = newCum;
        outChangedSpikeIds.push_back(p);
        ++nChanged;
        if (std::abs(sh) > maxAbs) maxAbs = std::abs(sh);
    }
    stats.nSpikesEvaluated += N;
    stats.nSpikesRealigned += static_cast<int>(
        std::count_if(shifts.begin(), shifts.end(),
                      [](int s){ return s != 0; }));
    stats.maxAbsShift = std::max(stats.maxAbsShift, maxAbs);
    ++stats.nClustersProcessed;
    return nChanged;
}


// ---------------------------------------------------------------------------
// KK::KlustersStyleRealignPerChunkClusters
//
// Per-chunk variant.  Iterates over chunks; for each chunk, groups
// chunk-local spike indices by their per-chunk cluster label and calls
// KlustersStyleRealignOneCluster on each.  Shifts commit to m_cumShift
// just like the global variant; changed spike global IDs are appended
// to outChangedSpikeIds so the caller can refeaturize selectively.
//
// Designed to be called inside the Phase 4 loop, AFTER the merge step
// (so meanWav harvesting at the top of the next iter sees the freshly
// realigned spikes).  Caller is expected to follow up with
// RefeaturizeChangedSpikes to propagate the shifts into Data[].
//
// Returns total nSpikesChanged across all chunks.
// ---------------------------------------------------------------------------
int KK::KlustersStyleRealignPerChunkClusters(
        const std::vector<std::vector<int>>& chunkPoints,
        const std::vector<std::vector<int>>& perChunkClass,
        int nChan, int nSamplesPerSpike,
        std::vector<int>& outChangedSpikeIds)
{
    if (nChan <= 0 || nSamplesPerSpike <= 0) return 0;
    if (!m_timeShiftReady)                   return 0;
    if (PeakSampleIndex < 0 || PeakSampleIndex >= nSamplesPerSpike) {
        LockedStderr(
                "[Stage 2.11 realign] PeakSampleIndex=%d out of [0,%d) — skipping.\n",
                PeakSampleIndex, nSamplesPerSpike);
        return 0;
    }
    if (chunkPoints.size() != perChunkClass.size()) return 0;

    const int peakPos  = PeakSampleIndex;
    const int maxShift = std::max(1, std::min(nSamplesPerSpike / 4,
                                              KlustersRealignMaxShift));
    const int minSize  = std::max(2, KlustersRealignMinSize);

    KlustersRealign::RealignStats stats;
    int totalChanged = 0;

    const size_t nChunks = chunkPoints.size();
    for (size_t ck = 0; ck < nChunks; ++ck) {
        const auto& pts = chunkPoints[ck];
        const auto& cls = perChunkClass[ck];
        if (pts.size() != cls.size() || pts.empty()) continue;

        // Bucket spike global IDs by local cluster.
        std::unordered_map<int, std::vector<int>> bucket;
        bucket.reserve(64);
        for (size_t i = 0; i < pts.size(); ++i) {
            const int c = cls[i];
            if (c <= 0) continue;          // skip noise
            bucket[c].push_back(pts[i]);
        }

        for (auto& kv : bucket) {
            totalChanged += KlustersStyleRealignOneCluster(
                kv.second, nChan, nSamplesPerSpike,
                peakPos, maxShift, minSize,
                outChangedSpikeIds, stats);
        }
    }

    // Note: per-chunk variant doesn't accumulate a true sum-of-shifts
    // across clusters (the helper's local sumAbs is per-cluster).
    // Reporting maxAbsShift only is informative enough for the in-
    // pipeline use; the global variant's log line includes the proper
    // mean.  If the per-iter mean becomes important, add a sumAbsShift
    // out-param to KlustersStyleRealignOneCluster and accumulate here.

    LockedStderr(
            "[Stage 2.11 realign] Per-chunk klusters-style: "
            "%d clusters processed (%d skipped), "
            "%d/%d spikes shifted (max|Δ|=%d), "
            "%d read failures.\n",
            stats.nClustersProcessed, stats.nClustersSkipped,
            stats.nSpikesRealigned, stats.nSpikesEvaluated,
            stats.maxAbsShift,
            stats.nSpikesReadFailed);

    return totalChanged;
}


// ---------------------------------------------------------------------------
// FinalMeanSubtractionMerge — Phase 7b
//
// Pairwise merge of live global clusters based on the normalised L2
// residual between their mean spike waveforms:
//
//     D(i, j) = ||mean[i] - mean[j]||² / max(||mean[i]||², ||mean[j]||²)
//
// Mean waveforms are aggregated across all chunks (i.e. spike index
// 0..nPoints-1 walks the whole session's spikes), using either the
// time-shift probe's open .spk file when available (so the means
// reflect the post-alignment view) or a direct fopen on the canonical
// .spk / .spkD path when the probe never initialised.  Self-contained
// — does NOT require m_timeShiftReady, unlike EnergyCOMRealignPhase.
//
// Pairs below MeanSubtractionMergeThresh are merged via union-find,
// smallest D first.  Smaller cluster ID always wins (keeps the palette
// layout stable; chronological-order-by-ID gets preserved).  No
// iteration: union-find already handles transitive merges in one pass.
//
// Caller is responsible for the post-merge state refresh (MStep /
// Reindex / EStep / ComputeScore) — same convention as Phase 7a's
// TimeShiftAlignPhase.
// ---------------------------------------------------------------------------
int KK::FinalMeanSubtractionMerge(int nChan, int nSamplesPerSpike)
{
    if (MeanSubtractionMergeEnable == 0) return 0;
    if (nChan <= 0 || nSamplesPerSpike <= 0) return 0;
    if (!(MeanSubtractionMergeThresh > 0.0f)) return 0;

    const int wElems = nChan * nSamplesPerSpike;

    // ── Spike-file access ──────────────────────────────────────────────
    // Prefer the time-shift probe's open file (already mmap'd / fopen'd,
    // and applying any cumulative shifts via TimeShiftReadSpikeWave so
    // means reflect post-alignment geometry).  Fall back to a direct
    // open of .spk / .spkD when the probe isn't initialised.
    FILE* spkFallback = nullptr;
    if (!m_timeShiftReady) {
        char spkPath[STRLEN + 16];
        if (pickInputPath(spkPath, sizeof(spkPath), FileBase, "spk", ElecNo) < 0) {
            LockedStderr( "[Stage 3.4] No .spk/.spkD found for electrode %d; "
                            "skip mean-subtraction merge.\n", ElecNo);
            return 0;
        }
        spkFallback = fopen(spkPath, "rb");
        if (!spkFallback) {
            LockedStderr( "[Stage 3.4] Could not open %s; "
                            "skip mean-subtraction merge.\n", spkPath);
            return 0;
        }
    }

    // ── Step 1: per-cluster mean waveform aggregation ──────────────────
    // Layout matches EnergyCOMRealignPhase's sumWave: time-major,
    // channel-minor (index = t * nChan + ch).  Sample-major is the
    // natural .spk layout; for an L2 residual the layout doesn't
    // matter as long as both means use the same indexing.
    constexpr int   kMinSpikesForMean = 5;
    std::vector<int16_t> row(static_cast<size_t>(wElems));
    std::vector<int>     liveClusters;
    std::unordered_map<int, std::vector<double>> meanWav;
    std::unordered_map<int, int>                 meanN;
    int nSkippedSmall   = 0;
    int nSkippedBadRead = 0;

    for (int c = 2; c < MaxPossibleClusters; ++c) {
        if (!ClassAlive[c]) continue;

        // Gather members.  Could O(N) once into a multimap but each
        // cluster's loop is short and we want to skip too-small ones
        // before allocating their accumulator.
        std::vector<int> members;
        for (int p = 0; p < nPoints; ++p)
            if (Class[p] == c) members.push_back(p);
        if (static_cast<int>(members.size()) < kMinSpikesForMean) {
            ++nSkippedSmall;
            continue;
        }

        std::vector<double> acc(static_cast<size_t>(wElems), 0.0);
        int nRead = 0;
        for (int p : members) {
            bool ok = false;
            if (m_timeShiftReady) {
                ok = TimeShiftReadSpikeWave(p, wElems, row.data());
            } else {
                // Fallback path: raw .spk read, then apply any shifts
                // we know about.  m_cumShift may still be populated even
                // if m_timeShiftReady is false (probe deinitialised
                // post-Phase-7), so ShiftWaveformRowInPlace is correct.
                if (fseeko(spkFallback,
                           static_cast<off_t>(p) * wElems * sizeof(int16_t),
                           SEEK_SET) == 0 &&
                    fread(row.data(), sizeof(int16_t),
                          static_cast<size_t>(wElems), spkFallback)
                        == static_cast<size_t>(wElems))
                {
                    ShiftWaveformRowInPlace(row.data(), p,
                                            nChan, nSamplesPerSpike);
                    ok = true;
                }
            }
            if (!ok) continue;
            for (int i = 0; i < wElems; ++i)
                acc[static_cast<size_t>(i)] +=
                    static_cast<double>(row[static_cast<size_t>(i)]);
            ++nRead;
        }
        if (nRead == 0) { ++nSkippedBadRead; continue; }

        const double inv = 1.0 / nRead;
        for (int i = 0; i < wElems; ++i) acc[static_cast<size_t>(i)] *= inv;

        liveClusters.push_back(c);
        meanWav[c] = std::move(acc);
        meanN[c]   = nRead;
    }
    if (spkFallback) fclose(spkFallback);

    if (liveClusters.size() < 2) {
        LockedStderr( "[Stage 3.4] Mean-subtraction merge: only %zu eligible "
                        "clusters (skipped %d small, %d bad-read); nothing to do.\n",
                liveClusters.size(), nSkippedSmall, nSkippedBadRead);
        return 0;
    }

    // ── Step 2: per-cluster energy (denominator for normalisation) ─────
    std::unordered_map<int, double> energy;
    for (int c : liveClusters) {
        double e = 0.0;
        for (double v : meanWav[c]) e += v * v;
        energy[c] = e;
    }

    // ── Step 3: pairwise residual under cyclic time shifts ─────────────
    //
    // For each pair (i, j), the residual is computed at every cyclic
    // time-shift τ ∈ [−K, K] on mean[i] and the minimum is taken:
    //
    //     D(i, j) = min over τ of  ||shift_τ(m_i) - m_j||² / max(||m_i||², ||m_j||²)
    //
    // where shift_τ wraps the time axis (m[t * nChan + ch] →
    // m[((t + τ) mod nSamp) * nChan + ch]).  Two reasons cyclic is
    // safe here despite physical waveforms not being periodic: (1)
    // captured spike windows have near-zero amplitude at both edges
    // (baseline) since the peak sits at PeakSampleIndex, so wrap-around
    // moves baseline-on-baseline and contributes ≈0 to the residual;
    // (2) the search radius K is bounded by MeanSubtractionMergeMaxShift
    // (default 3) on a typical 32-42 sample window — ≤ 10% of the
    // window wraps, well outside the peak region.
    //
    // Single-shift (no sweep) comparison was misleading: cluster-mean
    // alignment converges to a "good enough" shift per cluster, but the
    // residual between two clusters' means can still have a 1-2 sample
    // misalignment that single-shift D would penalise as if the
    // waveforms differed in shape.  Min-over-shifts removes that.
    const int nSamp = nSamplesPerSpike;
    int maxShift = MeanSubtractionMergeMaxShift;
    if (maxShift < 0)              maxShift = 0;
    if (maxShift > nSamp / 2)      maxShift = nSamp / 2;  // hard cap

    struct Pair { int i; int j; double D; int bestShift; };
    std::vector<Pair> candidates;
    std::sort(liveClusters.begin(), liveClusters.end());

    for (size_t a = 0; a < liveClusters.size(); ++a) {
        const int    i  = liveClusters[a];
        const auto&  mi = meanWav[i];
        const double ei = energy[i];
        for (size_t b = a + 1; b < liveClusters.size(); ++b) {
            const int    j  = liveClusters[b];
            const auto&  mj = meanWav[j];
            const double ej = energy[j];
            const double denom = std::max(ei, ej);
            if (!(denom > 0.0)) continue;       // silent vs anything

            double bestD     = std::numeric_limits<double>::infinity();
            int    bestShift = 0;

            for (int tau = -maxShift; tau <= maxShift; ++tau) {
                double rss = 0.0;
                // Inner loops: for each (t, ch), pull shifted mi sample
                // and compare to mj at the same (t, ch).  Layout is
                // time-major channel-minor (idx = t * nChan + ch),
                // matching the aggregation step above.
                for (int t = 0; t < nSamp; ++t) {
                    // Cyclic source row: (t + tau) wrapped into [0, nSamp).
                    int tSrc = t + tau;
                    while (tSrc <    0)     tSrc += nSamp;
                    while (tSrc >= nSamp)   tSrc -= nSamp;
                    const int rowSrc = tSrc * nChan;
                    const int rowDst = t    * nChan;
                    for (int ch = 0; ch < nChan; ++ch) {
                        const double d =
                            mi[static_cast<size_t>(rowSrc + ch)] -
                            mj[static_cast<size_t>(rowDst + ch)];
                        rss += d * d;
                    }
                }
                const double D = rss / denom;
                if (D < bestD) { bestD = D; bestShift = tau; }
            }

            if (bestD < static_cast<double>(MeanSubtractionMergeThresh))
                candidates.push_back({i, j, bestD, bestShift});
        }
    }

    if (candidates.empty()) {
        LockedStderr( "[Stage 3.4] Mean-subtraction merge: 0 pairs below "
                        "D=%.3f (%zu eligible clusters compared with "
                        "cyclic-shift search ±%d); no merges.\n",
                static_cast<double>(MeanSubtractionMergeThresh),
                liveClusters.size(), maxShift);
        return 0;
    }

    // ── Step 4: union-find merge (smallest D first) ────────────────────
    std::sort(candidates.begin(), candidates.end(),
              [](const Pair& a, const Pair& b) { return a.D < b.D; });

    std::unordered_map<int, int> parent;
    for (int c : liveClusters) parent[c] = c;
    auto findRoot = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };

    int nMerged = 0;
    for (const auto& p : candidates) {
        const int ri = findRoot(p.i);
        const int rj = findRoot(p.j);
        if (ri == rj) continue;
        // Smaller cluster ID always wins — palette stability, and the
        // surviving root represents the chronologically-earlier cluster
        // (chunk-merge order assigns lower IDs to earlier-detected units).
        const int keep = std::min(ri, rj);
        const int drop = std::max(ri, rj);
        parent[drop] = keep;
        LockedStderr( "[Stage 3.4]   merge cluster %d <- %d  (D=%.4f at τ=%+d, "
                        "n=%d+%d=%d)\n",
                keep, drop, p.D, p.bestShift,
                meanN[keep], meanN[drop],
                meanN[keep] + meanN[drop]);
        ++nMerged;
    }

    // ── Step 5: apply resolved roots to Class[] + kill merged-away ─────
    for (int p = 0; p < nPoints; ++p) {
        const int c = Class[p];
        if (c < 2) continue;
        auto it = parent.find(c);
        if (it == parent.end()) continue;
        const int root = findRoot(c);
        if (root != c) Class[p] = root;
    }
    for (const auto& kv : parent) {
        if (kv.first != kv.second) ClassAlive[kv.first] = 0;
    }

    LockedStderr( "[Stage 3.4] Mean-subtraction merge: %d merges applied "
                    "(%zu candidates below D=%.3f under cyclic-shift ±%d, "
                    "%zu clusters evaluated; skipped %d small, %d bad-read).\n",
            nMerged, candidates.size(),
            static_cast<double>(MeanSubtractionMergeThresh),
            maxShift,
            liveClusters.size(), nSkippedSmall, nSkippedBadRead);

    return nMerged;
}


// ---------------------------------------------------------------------------
// RunAlignmentBlock — per-phase-boundary alignment wrapper
//
// Invoked at each insertion site (after Phase 1, Phase 1b, Phase 2,
// Phase 2b, Phase 5, Phase 6) when the corresponding
// TimeShiftAlignAfterPhase* flag is set.  Runs cluster-mean alignment
// via TimeShiftAlignPhase, then (if -EnergyCOMRealign 1) the energy-COM
// pass via EnergyCOMRealignPhase.  Banners use the supplied label.
//
// Intentionally minimal — no MStep / EStep / ComputeScore here.  The
// existing convention (matching the original Phase 1a call site at the
// pre-patch line 3368) is that the next phase's own setup refreshes
// global state.  The dedicated Phase 6a site does its own
// ComputeScore + ReportClusterQuality because it's the LAST
// alignment of the run and the final score / quality dump consumes
// the refreshed numbers.
// ---------------------------------------------------------------------------
void KK::RunAlignmentBlock(int enableFlag, const char* phaseLabel)
{
    if (!enableFlag || !m_timeShiftReady) return;
    if (NbChannels <= 0 || NbSamplesPerSpike <= 0) return;

    LockedStderr( "[%s] Cluster-mean alignment\n", phaseLabel);
    (void)TimeShiftAlignPhase(NbChannels, NbSamplesPerSpike);

    if (EnergyCOMRealign != 0) {
        LockedStderr( "[%s] Energy-COM realignment\n", phaseLabel);
        (void)EnergyCOMRealignPhase(NbChannels, NbSamplesPerSpike);
    }
}


// ---------------------------------------------------------------------------
// TimeShiftSplitCluster — thin wrapper over the primitive
// ---------------------------------------------------------------------------
int KK::TimeShiftSplitCluster(int clusterId, int nChan, int nSamplesPerSpike)
{
    if (!m_timeShiftReady) return 0;
    std::vector<int> members;
    members.reserve(256);
    for (int p = 0; p < nPoints; ++p)
        if (Class[p] == clusterId) members.push_back(p);
    return TimeShiftSplit(members, nChan, nSamplesPerSpike);
}


// ---------------------------------------------------------------------------
// TimeShiftFinalize — Phase 9: shift commit
//
// Re-extract each shifted spike from .fil at (rawTs - cumShift - PeakSampleIndex),
// re-project through the PCA basis, and rewrite .spk / .fet (via
// WritePhase15Checkpoint's .*.pending mechanism).  This is the ONLY point at
// which disk is touched by the shift-probe — all earlier probe operations
// work purely in memory on Data[] and m_cumShift[].
//
// Using .fil rather than circular-shifting the .spk waveforms eliminates
// wrap-around corruption that would otherwise poison ~sample/waveform of
// the shifted window.  For spikes with cumShift == 0, the existing .spk
// content is preserved (no re-extract).
//
// Owns the phase label so log output reflects what's happening in real
// time.  Phase 1a is now cluster alignment (TimeShiftAlignPhase);
// Phase 4 is the final disk commit that closes the probe session.
// ---------------------------------------------------------------------------

// patch87 ----------------------------------------------------------------
// AutoReextractAfterFinalize: after TimeShiftFinalize has committed the
// .res / .spk / .fet rewrites via WritePhase15Checkpoint, invoke the
// appropriate extractspikes binary in -R mode to refresh the .spk(D)
// content from .fil for the CURRENT group at the now-corrected .res
// timestamps.
//
// Why this is needed even though WritePhase15Checkpoint also rewrites
// .spk(D): for stderiv pipelines, the per-spike re-extract in
// WritePhase15Checkpoint computes the temporal first-difference at
// sample s=0 with prev_sdiff = 0, because it has no cross-spike state.
// That introduces a small sample-0 error in every re-extracted spike.
// The full streaming re-extractor — but using mmap-and-jump in -R mode
// (patch86), which reads one extra sample at (ws - 1) and uses its
// spatial derivative as prev_sdiff — produces bit-correct .spkD content.
//
// For non-stderiv pipelines (vanilla .spk) the sample-0 issue doesn't
// exist, but running this is still harmless: the re-extracted content
// matches what WritePhase15Checkpoint wrote.  Caller can disable via
// KKEXP_AUTO_REEXTRACT=0.
//
// Pipeline detection:
//   m_timeShiftBasis.isStderiv == true   → process_extractspikes_stderiv
//   otherwise                            → process_extractspikes
//   override:  KKEXP_REEXTRACT_TOOL=...   (e.g. for sdiff pipeline)
//
// Strategy:
//   1. mkdtemp /tmp/kkexp-rxn-XXXXXX
//   2. symlink <tmp>/x.fil  → <FileBase>.fil
//   3. symlink <tmp>/x.res.1 → <FileBase>.res.<ElecNo>
//   4. popen("<tool> -n N -c chans -w W -p P [-d ord] -R <tmp>/x")
//   5. rename <tmp>/x.spk(D).1 → <FileBase>.spk(D).<ElecNo>
//   6. clean up symlinks + tmpdir
//
// Failures along the way are logged; the original files are preserved
// (the rename in step 5 is the only write to the canonical paths).
static void AutoReextractAfterFinalize(int nChan,
                                       int nSamplesPerSpike,
                                       bool isStderiv,
                                       int peakSampleIdx)
{
    (void)nChan;
    (void)nSamplesPerSpike;

    // ── 1. env gating ────────────────────────────────────────────────────
    const char *disable = getenv("KKEXP_AUTO_REEXTRACT");
    if (disable && strcmp(disable, "0") == 0) {
        Output("[Stage 3.5 reextract] auto-reextract disabled by KKEXP_AUTO_REEXTRACT=0\n");
        return;
    }

    // ── 2. pick the tool ─────────────────────────────────────────────────
    const char *tool = getenv("KKEXP_REEXTRACT_TOOL");
    const char *spkExt = isStderiv ? "spkD" : "spk";
    if (!tool || !*tool) {
        tool = isStderiv ? "process_extractspikes_stderiv"
                         : "process_extractspikes";
    }

    // ── 3. session-level inputs we need from the runtime ─────────────────
    if (NbTotalChannels <= 0 || GroupChannelIds.empty()) {
        Output("[Stage 3.5 reextract] auto-reextract: missing NbTotalChannels or "
               "GroupChannelIds — skipping\n");
        return;
    }
    if (nSamplesPerSpike <= 0 || peakSampleIdx <= 0) {
        Output("[Stage 3.5 reextract] auto-reextract: missing waveform geometry "
               "(nSamplesPerSpike=%d peakSampleIdx=%d) — skipping\n",
               nSamplesPerSpike, peakSampleIdx);
        return;
    }

    // ── 4. build the channel-list string (single group) ──────────────────
    std::string chans;
    for (size_t i = 0; i < GroupChannelIds.size(); i++) {
        if (i > 0) chans += ",";
        chans += std::to_string(GroupChannelIds[i]);
    }

    // ── 5. temp dir + symlinks ───────────────────────────────────────────
    char tmpDirTemplate[] = "/tmp/kkexp-rxn-XXXXXX";
    if (!mkdtemp(tmpDirTemplate)) {
        Output("[Stage 3.5 reextract] auto-reextract: mkdtemp failed (%s) — skipping\n",
               strerror(errno));
        return;
    }
    const std::string tmpDir   = tmpDirTemplate;
    const std::string tmpStem  = tmpDir + "/x";
    const std::string filLink  = tmpStem + ".fil";
    const std::string resLink  = tmpStem + ".res.1";

    char filReal[1024], resReal[1024], spkReal[1024];
    snprintf(filReal, sizeof(filReal), "%s.fil",            FileBase);
    snprintf(resReal, sizeof(resReal), "%s.res.%d",         FileBase, ElecNo);
    snprintf(spkReal, sizeof(spkReal), "%s.%s.%d",          FileBase, spkExt,
             ElecNo);

    // Resolve to absolute paths for the symlinks (in case FileBase is
    // relative — symlinks resolve relative to the symlink's *directory*,
    // not the cwd).
    //
    // Pass NULL as the destination so realpath() malloc's a buffer of
    // up to PATH_MAX bytes itself.  Using a fixed-size stack buffer
    // (e.g. char[1024]) triggers glibc's __realpath_chk FORTIFY abort
    // ("*** buffer overflow detected ***: terminated") even when the
    // actual resolved path would have fit, because the check refuses
    // any destination smaller than PATH_MAX at compile time.  Caller
    // must free() the returned pointer.
    char* filAbs = realpath(filReal, nullptr);
    if (!filAbs) {
        Output("[Stage 3.5 reextract] auto-reextract: cannot resolve %s (%s) — skipping\n",
               filReal, strerror(errno));
        rmdir(tmpDir.c_str());
        return;
    }
    char* resAbs = realpath(resReal, nullptr);
    if (!resAbs) {
        Output("[Stage 3.5 reextract] auto-reextract: cannot resolve %s (%s) — skipping\n",
               resReal, strerror(errno));
        free(filAbs);
        rmdir(tmpDir.c_str());
        return;
    }

    if (symlink(filAbs, filLink.c_str()) != 0) {
        Output("[Stage 3.5 reextract] auto-reextract: symlink %s → %s failed (%s) — "
               "skipping\n", filLink.c_str(), filAbs, strerror(errno));
        free(filAbs); free(resAbs);
        rmdir(tmpDir.c_str());
        return;
    }
    if (symlink(resAbs, resLink.c_str()) != 0) {
        Output("[Stage 3.5 reextract] auto-reextract: symlink %s → %s failed (%s) — "
               "skipping\n", resLink.c_str(), resAbs, strerror(errno));
        free(filAbs); free(resAbs);
        unlink(filLink.c_str());
        rmdir(tmpDir.c_str());
        return;
    }

    // ── 6. build command line ────────────────────────────────────────────
    std::ostringstream cmd;
    cmd << tool;
    cmd << " -n " << NbTotalChannels;
    cmd << " -c " << chans;
    cmd << " -w " << nSamplesPerSpike;
    // PeakSampleIndex in KKExp is 0-based (per KlustaKwik.h declaration);
    // the binary's -p flag expects 1-based and subtracts 1 internally.
    cmd << " -p " << (peakSampleIdx + 1);
    if (isStderiv) {
        // Default sdiff order if not overridden.  3 = ALLPAIRS matches the
        // wrapper script default.  Configurable via env for non-default
        // sessions (e.g. probes that require a specific spatial-derivative
        // order, or stderiv-on-raw with order 0).
        const char *sdiffOrder = getenv("KKEXP_REEXTRACT_SDIFF_ORDER");
        cmd << " -d " << (sdiffOrder && *sdiffOrder ? sdiffOrder : "3");
    }
    cmd << " -R";
    if (getenv("KKEXP_REEXTRACT_VERBOSE")) cmd << " -v";
    cmd << " " << tmpStem;
    cmd << " 2>&1";  // fold stderr into stdout for popen capture

    Output("[Stage 3.5 reextract] auto-reextract: %s\n", cmd.str().c_str());

    // ── 7. popen + stream output ─────────────────────────────────────────
    FILE *pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) {
        Output("[Stage 3.5 reextract] auto-reextract: popen failed (%s) — skipping\n",
               strerror(errno));
        free(filAbs); free(resAbs);
        unlink(filLink.c_str());
        unlink(resLink.c_str());
        rmdir(tmpDir.c_str());
        return;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe)) {
        Output("[Stage 3.5 reextract]   %s", buf);  // tool output already has '\n'
    }
    const int rc = pclose(pipe);

    // ── 8. rename output to canonical location, only on success ──────────
    const std::string spkOut = tmpStem + "." + spkExt + ".1";
    if (rc == 0) {
        if (rename(spkOut.c_str(), spkReal) == 0) {
            Output("[Stage 3.5 reextract] auto-reextract: refreshed %s from %s\n",
                   spkReal, filAbs);
        } else {
            Output("[Stage 3.5 reextract] auto-reextract: rename %s → %s failed (%s) — "
                   "ORIGINAL .%s PRESERVED, tmp output discarded\n",
                   spkOut.c_str(), spkReal, strerror(errno), spkExt);
            unlink(spkOut.c_str());
        }
    } else {
        Output("[Stage 3.5 reextract] auto-reextract: tool exited with status %d — "
               "ORIGINAL .%s PRESERVED, tmp output discarded\n",
               rc, spkExt);
        unlink(spkOut.c_str());  // tool may have created a partial file
    }

    // ── 9. cleanup symlinks + tmpdir ─────────────────────────────────────
    unlink(filLink.c_str());
    unlink(resLink.c_str());
    rmdir(tmpDir.c_str());
    free(filAbs);
    free(resAbs);
}


void KK::TimeShiftFinalize(int nChan, int nSamplesPerSpike)
{
    if (m_cumShift.empty()) { CloseTimeShift(); return; }

    const int nShifted = static_cast<int>(
        std::count_if(m_cumShift.begin(), m_cumShift.end(),
                      [](int s){ return s != 0; }));

    if (nShifted > 0) {
        LockedStderr(
                "[Stage 3.5 commit] Shift commit: re-extract %d spikes from .fil "
                "→ .spk/.fet\n", nShifted);
        Output("[Stage 3.5 commit] Shift commit: %d probe calls, %d spikes with "
               "non-zero cumulative shift\n",
               m_timeShiftCallCount, nShifted);
        // RefeaturizeFromShifts expects shift=0 to mean "skip" — which matches
        // our accumulator.  It re-extracts from .fil for non-zero entries,
        // projects, and re-normalises; this supersedes the in-memory features
        // with clean .fil-derived ones.  WritePhase15Checkpoint then writes
        // the corrected .spk / .fet / .res via the .*.pending mechanism.
        RefeaturizeFromShifts(m_cumShift, nChan, nSamplesPerSpike);
        WritePhase15Checkpoint(m_cumShift, nChan, nSamplesPerSpike);

        // patch87: after the .pending files are committed, invoke the
        // extractspikes binary in -R mode to refresh .spk(D) content from
        // .fil at the now-corrected .res timestamps.  Fixes the stderiv
        // sample-0 dispersion that WritePhase15Checkpoint can't fully
        // address per-spike (it has no cross-spike continuity for the
        // temporal first-difference).
        AutoReextractAfterFinalize(nChan, nSamplesPerSpike,
                                   m_timeShiftBasis.isStderiv,
                                   PeakSampleIndex);
    } else {
        Output("[Stage 3.5 commit] Shift commit: %d probe calls, 0 spikes shifted "
               "— nothing to write back\n", m_timeShiftCallCount);
    }
    CloseTimeShift();
}
