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
 *   num(τ)    = Σ_ch Σ_s  tmpl[ch,s] · spike[ch, (s+τ+N) % N]
 *   spkNorm²  = Σ_ch Σ_s  spike[ch,s]²    (full energy, precomputed once)
 *   tmplNorm² = Σ_ch Σ_s  tmpl[ch,s]²     (constant, precomputed once)
 *   score(τ)  = num(τ) / sqrt(tmplNorm² · spkNorm²)
 *
 * Circular shift (wrap-around) is used instead of zero-padding because the
 * data is high-pass filtered, so edge discontinuities are negligible and
 * every lag has the same number of contributing samples — no normalization
 * bias.  An already-aligned spike always scores highest at lag 0.
 *
 * Sign convention
 * ---------------
 * A positive score peak at lag τ means the spike's content at position s+τ
 * best matches the template at position s — i.e. the spike peak is *late* by τ
 * samples relative to the template.  To realign, the waveform must be rolled
 * forward by τ: aligned[s] = original[(s + τ) % N].  Equivalently, the spike's
 * timestamp must advance by τ: newTimestamp = oldTimestamp + τ.
 *
 * shifts_out[sp] = +bestLag so the caller can apply both corrections by simply
 * adding shifts_out to the timestamp AND using (s + sh) % N for the waveform roll.
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
    int maxShift, float minScore, float zeroTieMargin,
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

        // Precompute full spike energy (all samples, all channels).
        // Using the full energy — rather than the windowed overlap at each lag
        // — removes the normalization bias that would otherwise inflate scores
        // at large lags (where fewer samples overlap) and cause drift on
        // repeated realignment calls.
        double spkEnergy = 0.0;
        for (int ch = 0; ch < nChannels; ++ch)
            for (int s = 0; s < nSamples; ++s) {
                double v = static_cast<double>(spk[ch * nSamples + s]);
                spkEnergy += v * v;
            }
        const double denom = sqrtTmplEnergy * std::sqrt(spkEnergy);

        float bestScore = -FLT_MAX;
        int   bestLag   = 0;
        float score0    = -FLT_MAX;  // patch61: score at lag=0, for tie-margin

        for (int lag = -maxShift; lag <= maxShift; ++lag) {
            double num = 0.0;

            // Circular shift: all samples always contribute.
            for (int ch = 0; ch < nChannels; ++ch) {
                const int16_t* tch = tmpl + ch * nSamples;
                const int16_t* sch = spk  + ch * nSamples;
                for (int s = 0; s < nSamples; ++s) {
                    int sLag = (s + lag + nSamples) % nSamples;
                    num += static_cast<double>(tch[s])
                         * static_cast<double>(sch[sLag]);
                }
            }

            float score = (denom > 1e-12)
                          ? static_cast<float>(num / denom)
                          : 0.0f;

            if (lag == 0) score0 = score;            // patch61
            if (score > bestScore) {
                bestScore = score;
                bestLag   = lag;
            }
        }

        // patch61 — STAY-AT-ZERO tie-margin.  On a freshly tightened
        // cluster, spike-noise asymmetry typically lets some non-zero
        // lag eke out a microscopic xcorr improvement over lag=0 even
        // though the alignment is statistically indistinguishable.
        // Repeated nudge then disperses the alignment.  Revert to lag=0
        // unless the winning lag exceeds it by zeroTieMargin (default
        // ~0.005 of the [-1, 1] xcorr range).  Real 1-sample
        // misalignments typically show differences > 0.05, so this
        // doesn't suppress legitimate corrections.
        if (bestLag != 0 && bestScore < score0 + zeroTieMargin) {
            bestLag   = 0;
            bestScore = score0;
        }

        // A peak at bestLag means the spike is shifted +bestLag samples
        // relative to the template.  Negate so the caller can simply add
        // shifts_out to the timestamp (moving the spike earlier).
        // +bestLag: spike peak is at peakSamp0+bestLag, so ts is bestLag too early;
        // caller does newTs = ts + shifts_out to correct.
        shifts_out[sp] = (bestScore >= minScore) ? bestLag : 0;
        scores_out[sp] = bestScore;
    }

    return 0;
}

/* availability always true — OMP is always compiled */
int xcorr_omp_available() { return 1; }

} // extern "C"
