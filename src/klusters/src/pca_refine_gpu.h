/***************************************************************************
 * pca_refine_gpu.h
 *
 * Common C interface implemented by each GPU backend (CUDA, HIP, SYCL) for
 * the PCA-projection-energy refine pass of realignSpikes
 * (--pca-refine / -p).  For each spike in the cluster the kernel evaluates
 * every candidate shift in [-maxShift, +maxShift] and reports the shift
 * that maximises the spike's projection energy on the kept PCA basis:
 *
 *   energy(s) = sum_ch sum_k <basis_ch_k, spike(s) - mean_ch>²
 *
 * I/O is the caller's job — the host must already have read one wide
 * window per spike covering all candidates (nSamp + 2*maxShift samples
 * per channel) into a flat channel-major buffer.  Reading once per spike
 * instead of M times saves ~M× the file syscalls and is independently
 * useful even when no GPU is available.
 *
 * Array layouts (all host pointers — backends do their own H↔D copies)
 * -------------------------------------------------------------------
 *   rawWindowsCM  [K × nChan × wideLen]  channel-major within each spike
 *                                         rawWindowsCM[k*nChan*wideLen
 *                                                      + ch*wideLen + t]
 *                                         where wideLen = nSamp + 2*maxShift
 *                                         and the candidate at shift
 *                                         s = c - maxShift starts at t = c.
 *   pcaEvec       [chForPca × kComp × d2u]
 *                  evec[ch*kComp*d2u + k*d2u + u]
 *   pcaMeans      [chForPca × d2u]
 *                  means[ch*d2u + u]  (consulted only if centered != 0)
 *   bestShifts    [K]   output, integer shifts in [-maxShift, +maxShift]
 *
 * Implementation note — the kernel applies stderiv per-candidate
 * internally when useStder != 0; it does not require the host to
 * pre-transform the wide window.
 ***************************************************************************/

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns 1 if the backend has a usable device, 0 otherwise. */
int cuda_pca_refine_available();
int hip_pca_refine_available();
int sycl_pca_refine_available();

/**
 * Refine candidate shifts on the specified backend.  Returns 0 on success,
 * non-zero on failure (caller should fall back to the next backend or the
 * CPU loop).
 *
 *   K, M             — number of spikes; number of candidate shifts (2*maxShift+1)
 *   wideLen          — per-channel length of the wide window (nSamp + 2*maxShift)
 *   nSamp, nChan     — extraction window length / channels in the group
 *   chForPca         — number of channels the PCA basis covers (≤ nChan)
 *   kComp, d2u       — PCA components per channel; samples used per component
 *   rShift           — sample offset within the candidate window where the
 *                      d2u-sample PCA slice starts
 *   maxShift         — half-width of the candidate-shift range
 *   centered         — 1 to subtract pcaMeans[ch][u] before projection
 *   useStder         — 1 to apply stderiv to each candidate window before
 *                      projection (matches the stderiv-pipeline behaviour)
 */
int cuda_pca_refine(
    int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
    int kComp, int d2u, int rShift, int maxShift,
    int centered, int useStder,
    const int16_t* rawWindowsCM,
    const float*   pcaEvec,
    const float*   pcaMeans,
    int*           bestShifts);

int hip_pca_refine(
    int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
    int kComp, int d2u, int rShift, int maxShift,
    int centered, int useStder,
    const int16_t* rawWindowsCM,
    const float*   pcaEvec,
    const float*   pcaMeans,
    int*           bestShifts);

int sycl_pca_refine(
    int K, int M, int wideLen, int nSamp, int nChan, int chForPca,
    int kComp, int d2u, int rShift, int maxShift,
    int centered, int useStder,
    const int16_t* rawWindowsCM,
    const float*   pcaEvec,
    const float*   pcaMeans,
    int*           bestShifts);

#ifdef __cplusplus
}
#endif
