/***************************************************************************
 * realign_xcorr_omp.cpp
 *
 * OpenMP CPU implementation of normalised cross-correlation spike alignment.
 * This is the always-compiled fallback used when no GPU backend is available.
 *
 * Algorithm per spike
 * -------------------
 * For lag τ in [-maxShift, +maxShift]:
 *
 *   The waveform buffer for spike i is padded with zeros outside [0, nSamples).
 *   For each lag τ:
 *
 *   num(τ)      = Σ_ch Σ_s  tmpl[ch,s] · spike_padded[ch, s+τ]
 *   spkNorm²(τ) = Σ_ch Σ_s  spike_padded[ch, s+τ]²
 *   tmplNorm²   = Σ_ch Σ_s  tmpl[ch,s]²     (constant, precomputed once)
 *   score(τ)    = num(τ) / sqrt(tmplNorm² · spkNorm²(τ))
 *
 * Zero-padding (rather than "valid" mode) ensures the denominator is
 * consistent with the numerator at every lag and removes the bias toward
 * lag 0 that "valid" mode introduces.
 *
 * Sign convention
 * ---------------
 * A positive score peak at lag τ means the spike's content at position s+τ
 * best matches the template at position s — i.e. the spike is *late* by τ
 * samples relative to the template.  To realign, we must move the spike
 * *earlier* by τ samples, which means subtracting τ from its timestamp.
 * shifts_out[sp] is therefore returned as -bestLag so the caller applies
 *   newTimestamp = oldTimestamp + shifts_out[sp]
 * and the waveform roll in the iterative step shifts samples in the same
 * direction.
 *
 * Parallelism
 * -----------
 * Outer loop over spikes is parallelised with OpenMP.
 * Each thread works on independent output elements — no synchronisation.
 ***************************************************************************/

#include "realign_xcorr.h"

#ifdef _OPENMP
#  include <omp.h>
#endif

#include <cmath>
#include <cstring>
#include <cfloat>
#include <cstdint>
#include <cstddef>
#include <algorithm>

extern "C" {

int xcorr_omp_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore,
    int*   shifts_out,
    float* scores_out)
{
    // -----------------------------------------------------------------------
    // Precompute template energy (constant for all spikes and lags).
    // Sum over full waveform — matches the zero-padded numerator sum range.
    // -----------------------------------------------------------------------
    double tmplEnergy = 0.0;
    for (int ch = 0; ch < nChannels; ++ch)
        for (int s = 0; s < nSamples; ++s) {
            double v = static_cast<double>(tmpl[ch * nSamples + s]);
            tmplEnergy += v * v;
        }
    const double sqrtTmplEnergy = std::sqrt(tmplEnergy);

    // -----------------------------------------------------------------------
    // Per-spike cross-correlation search
    // -----------------------------------------------------------------------
#ifdef _OPENMP
    #pragma omp parallel for schedule(static)
#endif
    for (int sp = 0; sp < nSpikes; ++sp) {
        const int16_t* spk = waveforms + static_cast<ptrdiff_t>(sp)
                             * nChannels * nSamples;

        float bestScore = -FLT_MAX;
        int   bestLag   = 0;

        for (int lag = -maxShift; lag <= maxShift; ++lag) {
            double num       = 0.0;
            double spkEnergy = 0.0;

            // Zero-padded: template index s pairs with spike index s+lag.
            // Spike samples outside [0, nSamples) are treated as zero,
            // contributing 0 to both numerator and spkEnergy.
            for (int ch = 0; ch < nChannels; ++ch) {
                const int16_t* tch = tmpl + ch * nSamples;
                const int16_t* sch = spk  + ch * nSamples;
                for (int s = 0; s < nSamples; ++s) {
                    int sLag = s + lag;
                    if (sLag < 0 || sLag >= nSamples) continue; // zero pad
                    double tv = static_cast<double>(tch[s]);
                    double sv = static_cast<double>(sch[sLag]);
                    num       += tv * sv;
                    spkEnergy += sv * sv;
                }
            }

            double denom = sqrtTmplEnergy * std::sqrt(spkEnergy);
            float  score = (denom > 1e-12)
                           ? static_cast<float>(num / denom)
                           : 0.0f;

            if (score > bestScore) {
                bestScore = score;
                bestLag   = lag;
            }
        }

        // A peak at bestLag means the spike is shifted +bestLag samples
        // relative to the template.  Negate so the caller can simply add
        // shifts_out to the timestamp (moving the spike earlier).
        shifts_out[sp] = (bestScore >= minScore) ? -bestLag : 0;
        scores_out[sp] = bestScore;
    }

    return 0;
}

/* availability always true — OMP is always compiled */
int xcorr_omp_available() { return 1; }

} // extern "C"
