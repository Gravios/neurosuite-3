// =============================================================================
// adapt_model.h  —  per-cluster ISI-conditional rank-2 waveform model
//
// Model
// -----
// For each cluster c, decompose each spike i as
//     w_i = w₀_c + α_c · h(ISI_i; τ_c) · v_c + ε_i
// where:
//   w₀_c    : rested mean waveform (mode 0 = steady-state)
//   v_c     : adaptation shape (mode 1 = amplitude / shape change w/ ISI)
//   α_c     : adaptation magnitude (signed scalar)
//   τ_c     : recovery time constant (seconds)
//   h(x; τ) : adaptation kernel, single exponential exp(-x/τ)
//   ε_i     : residual (noise + biology not captured by the model)
//
// Biological interpretation: bursting spikes (short ISI_i) have reduced
// amplitude and broader half-width than rested ones, due to incomplete
// Na⁺ channel recovery and prolonged K⁺ tails.  The (v_c, α_c) pair
// captures the dominant mode of this morph; τ_c is the recovery time
// scale.
//
// Fit
// ---
// Alternating least squares:
//   1. Initialise:  w₀ = mean(w_i), v = first PC of (w_i - w₀), τ = grid value
//   2. For each τ in grid, alternate to convergence:
//      a. Given (w₀, v, τ), solve for α_i = h(ISI_i; τ) · α_c via LS
//         (closed-form per-spike): α_i_obs = <w_i - w₀, v> / <v, v>,
//         then α_c = mean(α_i_obs / h(ISI_i; τ)) over spikes with
//         finite ISI.
//      b. Given (α, τ), update w₀ = mean(w_i - α·h(ISI_i; τ)·v) and
//         v = first PC of residuals of (w_i - w₀)/(α·h(ISI_i; τ)).
//      c. Stop when ‖change‖ < convTol or maxIter reached.
//   3. Pick the τ that minimises total residual norm.  Parabolic interp
//      around the best grid point gives sub-grid precision.
//
// Robustness
// ----------
// Spikes with proxy_isi = +∞ (no causal predecessor) are USED for the w₀
// estimate only; they contribute nothing to v / α / τ.  Clusters with
// fewer than minValidIsi finite-ISI spikes get a degenerate model
// (disabled=true) — the caller should fall back to the no-adaptation
// model and use raw w_i in residual computations.
//
// Performance
// -----------
// Per cluster: O(nSpikes_c · nCh · nSamp · nTauGrid · maxIter).  For a
// typical chunk with 30 clusters × 1000 spikes × 8 ch × 32 samp × 4 τ ×
// 10 iter ≈ 300 M ops/cluster → seconds per chunk single-threaded.
// Outer loop over clusters is OMP-parallel.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

namespace adapt_model {

struct ClusterModel {
    int                clusterId       = 0;
    int                nSpikes         = 0;
    int                nValidIsi       = 0;
    std::vector<float> w0;              // rested mean waveform, [nCh·nSamp]
    std::vector<float> v;               // adaptation shape, [nCh·nSamp]
    float              alpha           = 0.0f;
    float              tau             = 0.015f;  // seconds
    float              fitResidVar     = 0.0f;    // post-fit residual variance
    bool               disabled        = false;   // true → use w0 only

    // Returns the predicted waveform under the model for a given proxy_isi.
    // dst must be size nCh·nSamp.  If `proxyIsiSec` is +∞ or model is
    // disabled, dst gets w0.
    void predict(float proxyIsiSec, int nCh, int nSamp, float* dst) const;

    // Residual = spike - predict(proxy_isi).  dst must be size nCh·nSamp.
    void residual(const float* spike, float proxyIsiSec,
                  int nCh, int nSamp, float* dst) const;
};

struct Config {
    // Grid of τ values to search, in seconds, increasing order.
    // Defaults reflect Na+ recovery time scales (5 ms) up to slower
    // K+/AHP-driven adaptation (150 ms).  Single exponential model so
    // these are the *characteristic* time constants per cluster.
    std::vector<float> tauGrid = {0.005f, 0.015f, 0.050f, 0.150f};

    // Minimum total spikes per cluster for fit to be attempted.
    int minSpikesForFit = 100;
    // Minimum spikes with finite proxy_isi for non-degenerate fit.
    int minValidIsiForFit = 50;
    // Alternating-LS iteration cap and convergence tolerance.
    int   maxIter = 15;
    float convTol = 1e-3f;
    // Verbose logging.
    bool Verbose = false;
};

struct Result {
    std::vector<ClusterModel> clusters;
    int nFit       = 0;  // non-disabled fits
    int nDisabled  = 0;  // total-spikes < min, or valid-ISI < min, or fit degenerate
};

// -----------------------------------------------------------------------------
// FitAll() — fits one ClusterModel per distinct cluster ID found in
// `clusterLabels`.
//
// Parameters:
//   spikes               [nSpikes × nCh × nSamp] flat, channel-major
//                        within each spike
//   nSpikes, nCh, nSamp  dimensions
//   clusterLabels        [nSpikes] per-spike cluster ID (any int).
//                        Spikes with label ≤ 0 are ignored.
//   proxyIsi             [nSpikes] per-spike proxy_isi in seconds.
//                        +∞ values are accepted (contribute to w₀ only).
//   cfg                  configuration
//
// Returns:
//   Result with one ClusterModel per cluster ID found.  disabled=true
//   models still have w0 populated (mean of all spikes); fields v, α, τ
//   are zero / sentinel.
// -----------------------------------------------------------------------------
Result FitAll(const float*           spikes,
              int                    nSpikes,
              int                    nCh,
              int                    nSamp,
              const int*             clusterLabels,
              const float*           proxyIsi,
              const Config&          cfg);

}  // namespace adapt_model
