/***************************************************************************
 * realign_xcorr_sycl.cpp
 *
 * Intel oneAPI SYCL implementation of normalised cross-correlation spike
 * alignment.  Compiled only when USE_SYCL is defined (icpx -fsycl).
 *
 * Work item layout
 * ----------------
 * nd_range<2> { {nSpikes, nLags}, {1, wg_lags} }
 *   - global[0] = spike index
 *   - global[1] = lag index (0 → lag = -maxShift + global[1])
 *
 * Each work-item computes the normalised xcorr score for one (spike, lag)
 * pair and writes to a score buffer.  A second kernel then reduces each
 * spike's scores across lags to find the best lag.
 *
 * Requires: SYCL 2020, Intel oneAPI Base Toolkit ≥ 2023.1
 ***************************************************************************/

#ifdef USE_SYCL

#include "realign_xcorr.h"

#include <sycl/sycl.hpp>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <optional>
#include <cfloat>
#include <vector>
#include <stdexcept>
#include <csetjmp>
#include <exception>

using namespace sycl;

// ---------------------------------------------------------------------------
// Kernel 1: compute per-(spike,lag) normalised xcorr score
// ---------------------------------------------------------------------------
// scores_buf  [nSpikes × nLags]   score(sp, lagIdx) at lagIdx = lag+maxShift
// ---------------------------------------------------------------------------
static void k_xcorr_scores(
    nd_item<2> it,
    const int16_t* waveforms,   // [nSpikes × nChan × nSamp]
    const int16_t* tmpl,        // [nChan × nSamp]
    float          sqrtTmplE,
    const float*   spkSqrtE,    // [nSpikes] — precomputed full spike sqrt(energy)
    int nChan, int nSamp, int maxShift,
    float* scores)              // [nSpikes × nLags]
{
    int sp      = static_cast<int>(it.get_global_id(0));
    int lagIdx  = static_cast<int>(it.get_global_id(1));
    int nLags   = 2 * maxShift + 1;
    int lag     = lagIdx - maxShift;

    const int16_t* spk = waveforms + static_cast<long long>(sp) * nChan * nSamp;

    // Denominator is constant across lags: use full spike energy so scores
    // at all lags are directly comparable (no normalization bias toward non-zero lags).
    float score = 0.0f;
    {
        // Use float — double is not supported on many Intel GPU targets.
        float num = 0.0f;
        // Circular shift — all samples contribute at every lag.
        for (int ch = 0; ch < nChan; ++ch) {
            const int16_t* tch = tmpl + ch * nSamp;
            const int16_t* sch = spk  + ch * nSamp;
            for (int s = 0; s < nSamp; ++s) {
                int sLag = (s + lag + nSamp) % nSamp;
                num += static_cast<float>(tch[s])
                     * static_cast<float>(sch[sLag]);
            }
        }
        float denom = sqrtTmplE * spkSqrtE[sp];
        score = (denom > 1e-12f) ? (num / denom) : 0.0f;
    }
    scores[sp * nLags + lagIdx] = score;
}

// ---------------------------------------------------------------------------
// Kernel 2: per-spike reduction — find argmax over lags
// ---------------------------------------------------------------------------
static void k_reduce(
    item<1> it,
    const float* scores,   // [nSpikes × nLags]
    int nLags, int maxShift, float minScore, float zeroTieMargin,
    int*   shifts_out,
    float* scores_out)
{
    int sp = static_cast<int>(it.get_id(0));
    float best = -FLT_MAX;
    int   blag = 0;
    // patch61 — track lag=0 score for stay-at-zero tie-margin.  In the
    // scores buffer, lag=0 sits at offset maxShift within each spike's
    // nLags-long row (lag = li - maxShift, so li == maxShift means lag = 0).
    float score0 = scores[sp * nLags + maxShift];
    for (int li = 0; li < nLags; ++li) {
        float s = scores[sp * nLags + li];
        if (s > best) { best = s; blag = li - maxShift; }
    }
    if (blag != 0 && best < score0 + zeroTieMargin) {
        blag = 0;
        best = score0;
    }
    // +bestLag: spike peak is at peakSamp0+bestLag, so ts is bestLag too early;
    // caller does newTs = ts + shifts_out to correct.
    shifts_out[sp] = (best >= minScore) ? blag : 0;
    scores_out[sp] = best;
}

// ---------------------------------------------------------------------------
// std::terminate interception guard
//
// The Intel oneAPI SYCL runtime occasionally calls std::terminate() directly
// (rather than throwing) when level-zero or the JIT compiler encounters an
// internal error.  This bypasses all try/catch blocks.
//
// We install a temporary terminate handler that longjmps back to a setjmp
// checkpoint, which lets us treat such failures as a normal error return
// instead of a crash.  The guard is active only for the duration of the
// SYCL call; the original handler is always restored.
// ---------------------------------------------------------------------------

static thread_local jmp_buf  s_sycl_jmpbuf;
static thread_local bool          s_sycl_guard_active = false;

static void sycl_terminate_handler()
{
    if (s_sycl_guard_active) {
        s_sycl_guard_active = false;
        longjmp(s_sycl_jmpbuf, 1);   // jump back with value 1 = failure
    }
    // Not our call — chain to whatever was installed before us.
    std::abort();
}



// ---------------------------------------------------------------------------
// Host entry points
// ---------------------------------------------------------------------------

// Helper: malloc_device with null check — throws std::runtime_error on failure
// so the caller's try/catch handles it uniformly.
// Must be outside extern "C" — templates require C++ linkage.
template<typename T>
static T* checked_malloc_device(size_t count, queue& q, const char* name)
{
    if (count == 0) return nullptr;
    T* ptr = malloc_device<T>(count, q);
    if (!ptr) {
        char msg[128];
        std::snprintf(msg, sizeof(msg),
            "SYCL malloc_device failed for '%s' (%zu elements)", name, count);
        throw std::runtime_error(msg);
    }
    return ptr;
}

extern "C" {

int xcorr_sycl_available()
{
    // Install a temporary terminate handler so that if the Intel oneAPI
    // runtime calls std::terminate() internally (rather than throwing), we
    // recover via longjmp instead of aborting the process.
    auto prev_handler = std::set_terminate(sycl_terminate_handler);
    s_sycl_guard_active = true;

    int result = 0;
    if (setjmp(s_sycl_jmpbuf) == 0) {
        // Normal path — guarded code runs here.
        try {
            auto dev = device(gpu_selector_v);

            if (!dev.get_info<info::device::usm_device_allocations>()) {
                result = 0;
            } else {
                // Trial allocation: confirms the runtime can actually allocate
                // device memory without aborting.
                queue q(dev, property::queue::in_order{});
                void* probe = malloc_device(1, q);
                if (probe) { free(probe, q); q.wait(); result = 1; }
            }
        } catch (...) {
            result = 0;
        }
    }
    // setjmp returned non-zero: terminate was called, result stays 0.

    s_sycl_guard_active = false;
    std::set_terminate(prev_handler);
    return result;
}

int xcorr_sycl_compute(
    const int16_t* waveforms,
    const int16_t* tmpl,
    int nSpikes, int nChannels, int nSamples,
    int maxShift, float minScore, float zeroTieMargin,
    int*   shifts_out,
    float* scores_out)
{
    int16_t* d_wave   = nullptr;
    int16_t* d_tmpl   = nullptr;
    float*   d_scores = nullptr;
    int*     d_shifts = nullptr;
    float*   d_sout   = nullptr;

    // Use optional<queue> so we can construct inside try{} while still
    // having the pointer available to the cleanup lambda in catch{}.
    std::optional<queue> qopt;

    float* d_spkSqrtE = nullptr;  // allocated after queue is ready

    auto cleanup = [&]() {
        if (!qopt.has_value()) return;
        queue& q = *qopt;
        if (d_wave)     { free(d_wave,     q); d_wave     = nullptr; }
        if (d_tmpl)     { free(d_tmpl,     q); d_tmpl     = nullptr; }
        if (d_scores)   { free(d_scores,   q); d_scores   = nullptr; }
        if (d_shifts)   { free(d_shifts,   q); d_shifts   = nullptr; }
        if (d_sout)     { free(d_sout,     q); d_sout     = nullptr; }
        if (d_spkSqrtE) { free(d_spkSqrtE,q); d_spkSqrtE = nullptr; }
    };

    // Same terminate guard as xcorr_sycl_available() — protects against
    // Intel oneAPI runtime calling std::terminate on JIT/driver failures.
    auto prev_handler = std::set_terminate(sycl_terminate_handler);
    s_sycl_guard_active = true;

    if (setjmp(s_sycl_jmpbuf) != 0) {
        // terminate was called inside SYCL — clean up and signal failure.
        s_sycl_guard_active = false;
        std::set_terminate(prev_handler);
        cleanup();
        fprintf(stderr, "[xcorr_sycl] runtime called std::terminate — "
                        "falling back to OpenMP\n");
        return -1;
    }

    try {
        qopt.emplace(gpu_selector_v, property::queue::in_order{});
        queue& q = *qopt;

        const int nLags = 2 * maxShift + 1;

        // ── Precompute template energy on host ───────────────────────────────
        double tmplE = 0.0;
        for (int ch = 0; ch < nChannels; ++ch)
            for (int s = 0; s < nSamples; ++s) {
                double v = static_cast<double>(tmpl[ch * nSamples + s]);
                tmplE += v * v;
            }
        float sqrtTmplE = static_cast<float>(std::sqrt(tmplE));

        // ── Precompute per-spike sqrt(energy) on host, then upload ───────────
        std::vector<float> hostSpkSqrtE(static_cast<size_t>(nSpikes));
        for (int sp = 0; sp < nSpikes; ++sp) {
            const int16_t* spk = waveforms
                + static_cast<ptrdiff_t>(sp) * nChannels * nSamples;
            float e = 0.0f;
            for (int ch = 0; ch < nChannels; ++ch)
                for (int s = 0; s < nSamples; ++s) {
                    float v = static_cast<float>(spk[ch * nSamples + s]);
                    e += v * v;
                }
            hostSpkSqrtE[static_cast<size_t>(sp)] = std::sqrt(e);
        }

        // ── Device buffers ───────────────────────────────────────────────────
        size_t waveBytes  = static_cast<size_t>(nSpikes) * nChannels * nSamples;
        size_t tmplElems  = static_cast<size_t>(nChannels) * nSamples;
        size_t scoreElems = static_cast<size_t>(nSpikes) * nLags;

        d_wave     = checked_malloc_device<int16_t>(waveBytes,  q, "waveforms");
        d_tmpl     = checked_malloc_device<int16_t>(tmplElems,  q, "template");
        d_scores   = checked_malloc_device<float>  (scoreElems, q, "scores");
        d_shifts   = checked_malloc_device<int>    (nSpikes,    q, "shifts");
        d_sout     = checked_malloc_device<float>  (nSpikes,    q, "scores_out");
        d_spkSqrtE = checked_malloc_device<float>  (nSpikes,    q, "spkSqrtE");

        q.memcpy(d_wave,     waveforms,             waveBytes * sizeof(int16_t));
        q.memcpy(d_tmpl,     tmpl,                  tmplElems * sizeof(int16_t));
        q.memcpy(d_spkSqrtE, hostSpkSqrtE.data(),   nSpikes   * sizeof(float));
        q.wait();

        // ── Kernel 1: score each (spike, lag) ────────────────────────────────
        int wgLags = (nLags < 64) ? nLags : 64; // workgroup size along lag axis
        // Round global lag range up to multiple of wgLags
        int globalLags = ((nLags + wgLags - 1) / wgLags) * wgLags;

        q.submit([&](handler& h) {
            h.parallel_for(
                nd_range<2>({static_cast<size_t>(nSpikes),
                             static_cast<size_t>(globalLags)},
                            {1, static_cast<size_t>(wgLags)}),
                [=](nd_item<2> it) {
                    // Guard against padding threads beyond nLags
                    if (static_cast<int>(it.get_global_id(1)) < nLags)
                        k_xcorr_scores(it,
                                       d_wave, d_tmpl, sqrtTmplE,
                                       d_spkSqrtE,
                                       nChannels, nSamples, maxShift,
                                       d_scores);
                });
        });

        // ── Kernel 2: reduce each spike's scores to best lag ─────────────────
        q.submit([&](handler& h) {
            h.parallel_for(
                range<1>(static_cast<size_t>(nSpikes)),
                [=](item<1> it) {
                    k_reduce(it, d_scores, nLags, maxShift, minScore, zeroTieMargin,
                             d_shifts, d_sout);
                });
        });

        q.wait();

        q.memcpy(shifts_out, d_shifts, static_cast<size_t>(nSpikes) * sizeof(int));
        q.memcpy(scores_out, d_sout,   static_cast<size_t>(nSpikes) * sizeof(float));
        q.wait();

        cleanup();
        s_sycl_guard_active = false;
        std::set_terminate(prev_handler);
        return 0;

    } catch (const std::exception& e) {
        fprintf(stderr, "[xcorr_sycl] error: %s\n", e.what());
        cleanup();
        s_sycl_guard_active = false;
        std::set_terminate(prev_handler);
        return -1;
    } catch (...) {
        fprintf(stderr, "[xcorr_sycl] unknown error\n");
        cleanup();
        s_sycl_guard_active = false;
        std::set_terminate(prev_handler);
        return -1;
    }
}

} // extern "C"

#endif // USE_SYCL
