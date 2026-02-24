/***************************************************************************
 * realign_xcorr.h
 *
 * Shared C interface for the spike-realignment cross-correlation kernel.
 * Implemented independently by each compute backend:
 *   realign_xcorr_cuda.cu     (NVIDIA CUDA)
 *   realign_xcorr_hip.hip     (AMD ROCm/HIP)
 *   realign_xcorr_sycl.cpp    (Intel oneAPI SYCL)
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
 * The template is the mean waveform over all spikes in the cluster,
 * computed by the caller before entering this function.
 *
 * Circular (periodic) shift
 * -------------------------
 * Spike waveforms come from high-pass filtered data which has no DC
 * component.  A circular shift is therefore more appropriate than
 * zero-padding: the spike samples wrap around at the buffer boundary
 * rather than being replaced by zeros.  This keeps the spike energy
 * constant at every candidate lag, so the normalised xcorr score is a
 * true comparison across all lags with no bias toward any shift magnitude.
 *
 * With zero-padding the denominator shrinks at large lags (fewer valid
 * samples), which artificially inflates scores at the extremes and biases
 * the argmax toward large shifts.  Circular shift removes this artefact.
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

int xcorr_omp_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out);

#ifdef __cplusplus
}
#endif
