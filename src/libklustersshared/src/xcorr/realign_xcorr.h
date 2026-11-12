/***************************************************************************
 * realign_xcorr.h
 *
 * Shared C interface for the spike-realignment cross-correlation kernel.
 * Implemented independently by each compute backend:
 *   realign_xcorr_cuda.cu     (NVIDIA CUDA)
 *   realign_xcorr_hip.hip     (AMD ROCm/HIP)
 *   realign_xcorr_sycl.cpp    (Intel oneAPI SYCL)
 *   realign_xcorr_metal.cpp   (Apple GPU, Metal; bit-identical to OpenMP)
 *   realign_xcorr_omp.cpp     (OpenMP CPU fallback, always compiled)
 *
 * Algorithm
 * ---------
 * For each spike in the cluster, compute the normalised cross-correlation
 * between the spike waveform and the cluster template at every candidate
 * lag τ ∈ [-maxShift, +maxShift].  The lag with the highest score is the
 * required shift.  Spikes whose best score is below minScore are left
 * unchanged (they may be noise or belong to an adjacent cluster).
 *
 * Undefined correlation: when the spike or the template has zero energy
 * (denominator <= 1e-12, e.g. a zero-filled spike whose read failed) every
 * lag scores 0 and no lag is preferred.  Every back end then returns
 * shift 0 and score 0 for that spike, whatever minScore is (including 0 or
 * negative), so it is never moved.  The score stays finite so callers that
 * average or print scores are unaffected.
 *
 * The template is the mean waveform over all spikes in the cluster,
 * computed by the caller before entering this function.
 *
 * Memory layout (all int16, interleaved samples)
 * -----------------------------------------------
 *   waveforms  [nSpikes × nChannels × nSamples]
 *              waveforms[(s*nChannels + ch)*nSamples + t]
 *   template   [nChannels × nSamples]
 *              tmpl[ch*nSamples + t]
 *   shifts_out [nSpikes]   — output: optimal lag per spike (signed int)
 *   scores_out [nSpikes]   — output: normalised xcorr score at optimal lag
 *                            (float, range [-1, 1])
 *
 * All arrays live on the HOST.  GPU backends copy them to device memory
 * internally and copy results back before returning.
 *
 * Returns 0 on success, non-zero if the backend is unavailable or fails.
 ***************************************************************************/

#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ── availability probes ─────────────────────────────────────────────── */
int xcorr_cuda_available();
int xcorr_hip_available();
int xcorr_sycl_available();
int xcorr_metal_available();

/* ── per-backend compute entry points ───────────────────────────────── */

/**
 * Compute optimal alignment shifts for a batch of spike waveforms.
 *
 * @param waveforms   [nSpikes × nChannels × nSamples] int16, interleaved
 * @param tmpl        [nChannels × nSamples] int16, the cluster mean waveform
 * @param nSpikes     number of spikes
 * @param nChannels   electrode channels per spike
 * @param nSamples    samples per channel per spike
 * @param maxShift    maximum search radius in samples (search ±maxShift)
 * @param minScore    minimum normalised xcorr to accept a shift (e.g. 0.7)
 * @param shifts_out  output: optimal lag per spike (0 = no shift needed)
 * @param scores_out  output: normalised xcorr at best lag
 * @return 0 on success, non-zero on failure
 */
int xcorr_cuda_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

int xcorr_hip_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

int xcorr_sycl_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

int xcorr_metal_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

int xcorr_omp_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

/**
 * WEIGHTED normalised cross-correlation (OpenMP only).  `weights` is a per-sample
 * kernel w[s] >= 0 of length nSamples in the TEMPLATE's frame (the same frame the
 * template's peak was rolled to), applied to every channel:
 *
 *   num(τ)    = Σ_ch Σ_s  w[s] · tmpl[ch,s] · spike[ch,(s+τ) % N]
 *   tmplE     = Σ_ch Σ_s  w[s] · tmpl[ch,s]²                (constant)
 *   spkE(τ)   = Σ_ch Σ_s  w[s] · spike[ch,(s+τ) % N]²       (per lag: the spike's
 *                                                           energy INSIDE the window)
 *   score(τ)  = num(τ) / sqrt(tmplE · spkE(τ))
 *
 * i.e. the correlation of the template with the part of the spike the window
 * admits, normalised by that part's energy -- a second spike (a collision) in
 * the tail or the lead-in of the window no longer feeds the score or its
 * normalisation, so it cannot pull the lag toward itself.  With w ≡ 1 every
 * score is bit-identical to xcorr_omp_compute (spkE(τ) is then the full
 * energy at every lag).  `weights == nullptr` is the unweighted kernel.  The
 * zero-energy rule is the unweighted one, per lag: a lag whose windowed energy
 * is <= 1e-12 scores 0.
 */
int xcorr_omp_compute_weighted(
    const int16_t* waveforms,
    const int16_t* tmpl,
    const float*   weights,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

#ifdef __cplusplus
}

// ---------------------------------------------------------------------------
// C++ dispatcher — selects the best available backend at runtime.
// Include this header and link realign_xcorr_dispatch.cpp to use it.
// ---------------------------------------------------------------------------
namespace XcorrDispatch {

/**
 * Run the cross-correlation alignment kernel on the best available backend
 * (CUDA → HIP → SYCL → Metal → OpenMP).  Same parameters as the per-backend
 * xcorr_*_compute() functions above.
 */
int compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

/**
 * The weighted kernel (xcorr_omp_compute_weighted).  With `weights == nullptr`
 * this is compute() on the best backend; with a kernel it runs on OpenMP, the
 * one backend that implements the weighting (the GPU kernels are unweighted),
 * whatever backend compute() selected.
 */
int computeWeighted(
    const int16_t* waveforms,
    const int16_t* tmpl,
    const float*   weights,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

/** Human-readable name of the backend selected at the first compute() call. */
const char* backendName();

} // namespace XcorrDispatch

#endif /* __cplusplus (second block) */
