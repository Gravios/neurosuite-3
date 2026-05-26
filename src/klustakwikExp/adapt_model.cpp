// =============================================================================
// adapt_model.cpp  —  alternating-LS fit of the rank-1 adaptation model
//
// Model:  w_i = w₀ + α · h(ISI_i; τ) · v + ε_i,   h(x; τ) = exp(-x/τ)
//
// Algorithm (per cluster, per τ):
//
//   Let r_i = w_i - w₀ (centered).  The model becomes
//       r_i ≈ h_i · (α · v)
//   This is a rank-1 LS factorisation with the per-spike scalar h_i
//   known.  Define u = α·v (a single vector).  The closed-form LS
//   solution is
//       u* = (Σ_i h_i · r_i) / (Σ_i h_i²)
//   so v = u / ‖u‖ and α = ‖u‖ (sign convention: positive α + v carrying
//   sign).
//
//   Alternation:
//     a. Given current w₀, compute u from r_i = w_i - w₀
//     b. Update w₀ = mean(w_i - h_i · u)
//     c. Repeat until ‖Δw₀‖ < convTol
//
// For each τ in the grid run the alternation; final τ-pick minimises the
// post-fit residual variance.  Parabolic interpolation through the three
// neighbouring grid points gives sub-grid τ precision (clamped to grid
// endpoints).
// =============================================================================
#include "adapt_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace adapt_model {

// -----------------------------------------------------------------------------
// ClusterModel::predict / residual
// -----------------------------------------------------------------------------
void ClusterModel::predict(float proxyIsiSec, int nCh, int nSamp, float* dst) const {
    const int N = nCh * nSamp;
    if (disabled || !std::isfinite(proxyIsiSec) || v.empty()) {
        for (int k = 0; k < N; ++k) {
            dst[k] = (k < static_cast<int>(w0.size())) ? w0[static_cast<size_t>(k)] : 0.0f;
        }
        return;
    }
    const float h = std::exp(-proxyIsiSec / tau);
    for (int k = 0; k < N; ++k) {
        dst[k] = w0[static_cast<size_t>(k)] + alpha * h * v[static_cast<size_t>(k)];
    }
}

void ClusterModel::residual(const float* spike, float proxyIsiSec,
                             int nCh, int nSamp, float* dst) const {
    const int N = nCh * nSamp;
    if (disabled || !std::isfinite(proxyIsiSec) || v.empty()) {
        for (int k = 0; k < N; ++k) {
            const float pred = (k < static_cast<int>(w0.size()))
                ? w0[static_cast<size_t>(k)] : 0.0f;
            dst[k] = spike[k] - pred;
        }
        return;
    }
    const float h = std::exp(-proxyIsiSec / tau);
    for (int k = 0; k < N; ++k) {
        const float pred = w0[static_cast<size_t>(k)] +
                           alpha * h * v[static_cast<size_t>(k)];
        dst[k] = spike[k] - pred;
    }
}

namespace {

// Fit one cluster at one τ.  Returns the post-fit total residual SSE and
// fills (w0, alpha, v) by reference.
//
// `spikePtrs[i]` points to a contiguous nCh·nSamp waveform.
// `isi[i]` is the proxy_isi in seconds (or +∞).
struct FitState {
    std::vector<float> w0;     // [N]
    std::vector<float> v;      // [N], unit norm (or zero if degenerate)
    float              alpha = 0.0f;
};

static double fitOneTau(const std::vector<const float*>& spikePtrs,
                         const std::vector<float>&        isi,
                         int                              N,           // nCh*nSamp
                         float                            tau,
                         int                              maxIter,
                         float                            convTol,
                         FitState&                        st) {
    const int M = static_cast<int>(spikePtrs.size());
    if (M < 2) return std::numeric_limits<double>::infinity();

    // h_i and validity flag
    std::vector<float> h(static_cast<size_t>(M), 0.0f);
    int nValid = 0;
    for (int i = 0; i < M; ++i) {
        if (std::isfinite(isi[static_cast<size_t>(i)])) {
            h[static_cast<size_t>(i)] = std::exp(-isi[static_cast<size_t>(i)] / tau);
            ++nValid;
        }
    }
    if (nValid < 2) return std::numeric_limits<double>::infinity();

    // Initialise w₀ = mean of all spikes
    st.w0.assign(static_cast<size_t>(N), 0.0f);
    for (int i = 0; i < M; ++i) {
        const float* s = spikePtrs[static_cast<size_t>(i)];
        for (int k = 0; k < N; ++k) st.w0[static_cast<size_t>(k)] += s[k];
    }
    const float invM = 1.0f / M;
    for (int k = 0; k < N; ++k) st.w0[static_cast<size_t>(k)] *= invM;

    st.v.assign(static_cast<size_t>(N), 0.0f);
    st.alpha = 0.0f;

    std::vector<double> u(static_cast<size_t>(N), 0.0);

    for (int it = 0; it < maxIter; ++it) {
        // 1. Compute u = (Σ_i h_i · (w_i - w₀)) / (Σ_i h_i²)
        std::fill(u.begin(), u.end(), 0.0);
        double sumH2 = 0.0;
        for (int i = 0; i < M; ++i) {
            const float hi = h[static_cast<size_t>(i)];
            if (hi == 0.0f) continue;
            sumH2 += static_cast<double>(hi) * hi;
            const float* s = spikePtrs[static_cast<size_t>(i)];
            for (int k = 0; k < N; ++k) {
                u[static_cast<size_t>(k)] +=
                    hi * (static_cast<double>(s[k]) -
                          static_cast<double>(st.w0[static_cast<size_t>(k)]));
            }
        }
        if (sumH2 <= 0.0) return std::numeric_limits<double>::infinity();
        const double invSum = 1.0 / sumH2;
        double normSq = 0.0;
        for (int k = 0; k < N; ++k) {
            u[static_cast<size_t>(k)] *= invSum;
            normSq += u[static_cast<size_t>(k)] * u[static_cast<size_t>(k)];
        }
        const double norm = std::sqrt(normSq);
        if (norm <= 1e-12) {
            // Degenerate (no adaptation signal) — fit is trivially zero-alpha.
            st.alpha = 0.0f;
            std::fill(st.v.begin(), st.v.end(), 0.0f);
            break;
        }
        const float newAlpha = static_cast<float>(norm);
        std::vector<float> newV(static_cast<size_t>(N), 0.0f);
        const double invNorm = 1.0 / norm;
        for (int k = 0; k < N; ++k) {
            newV[static_cast<size_t>(k)] =
                static_cast<float>(u[static_cast<size_t>(k)] * invNorm);
        }

        // 2. Update w₀ = mean(w_i - h_i · α · v)
        std::vector<float> newW0(static_cast<size_t>(N), 0.0f);
        for (int i = 0; i < M; ++i) {
            const float hi  = h[static_cast<size_t>(i)];
            const float* s  = spikePtrs[static_cast<size_t>(i)];
            for (int k = 0; k < N; ++k) {
                newW0[static_cast<size_t>(k)] += s[k] -
                    hi * newAlpha * newV[static_cast<size_t>(k)];
            }
        }
        for (int k = 0; k < N; ++k) newW0[static_cast<size_t>(k)] *= invM;

        // 3. Convergence check on w₀ delta
        double deltaSq = 0.0;
        for (int k = 0; k < N; ++k) {
            const double d = static_cast<double>(newW0[static_cast<size_t>(k)]) -
                             static_cast<double>(st.w0[static_cast<size_t>(k)]);
            deltaSq += d * d;
        }
        st.w0    = std::move(newW0);
        st.v     = std::move(newV);
        st.alpha = newAlpha;
        if (std::sqrt(deltaSq) < convTol) break;
    }

    // 4. Post-fit residual SSE summed over spikes and (ch, samp)
    double sse = 0.0;
    for (int i = 0; i < M; ++i) {
        const float  hi = h[static_cast<size_t>(i)];
        const float* s  = spikePtrs[static_cast<size_t>(i)];
        for (int k = 0; k < N; ++k) {
            const double pred = static_cast<double>(st.w0[static_cast<size_t>(k)]) +
                                static_cast<double>(st.alpha) * hi *
                                static_cast<double>(st.v[static_cast<size_t>(k)]);
            const double d = static_cast<double>(s[k]) - pred;
            sse += d * d;
        }
    }
    return sse;
}

}  // anonymous

Result FitAll(const float*           spikes,
              int                    nSpikes,
              int                    nCh,
              int                    nSamp,
              const int*             clusterLabels,
              const float*           proxyIsi,
              const Config&          cfg) {
    Result out;
    const int N = nCh * nSamp;
    if (nSpikes <= 0 || N <= 0) return out;

    // 1. Bucket spike indices by cluster.
    std::unordered_map<int, std::vector<int>> byClust;
    for (int i = 0; i < nSpikes; ++i) {
        const int c = clusterLabels[i];
        if (c <= 0) continue;
        byClust[c].push_back(i);
    }

    // 2. Convert to a vector for OMP-friendly iteration.
    std::vector<int> clusterIds;
    clusterIds.reserve(byClust.size());
    for (const auto& kv : byClust) clusterIds.push_back(kv.first);

    out.clusters.resize(clusterIds.size());

    #pragma omp parallel for schedule(dynamic)
    for (int cidx = 0; cidx < static_cast<int>(clusterIds.size()); ++cidx) {
        const int cid    = clusterIds[static_cast<size_t>(cidx)];
        const auto& idxs = byClust[cid];
        ClusterModel cm;
        cm.clusterId = cid;
        cm.nSpikes   = static_cast<int>(idxs.size());

        // Always populate w0 with the simple mean as a fallback.
        cm.w0.assign(static_cast<size_t>(N), 0.0f);
        for (int i : idxs) {
            const float* s = spikes + static_cast<size_t>(i) * N;
            for (int k = 0; k < N; ++k) cm.w0[static_cast<size_t>(k)] += s[k];
        }
        if (!idxs.empty()) {
            const float inv = 1.0f / static_cast<float>(idxs.size());
            for (int k = 0; k < N; ++k) cm.w0[static_cast<size_t>(k)] *= inv;
        }

        // Count valid ISI
        int nValid = 0;
        for (int i : idxs) {
            if (std::isfinite(proxyIsi[i])) ++nValid;
        }
        cm.nValidIsi = nValid;

        if (cm.nSpikes < cfg.minSpikesForFit || nValid < cfg.minValidIsiForFit) {
            cm.disabled = true;
            out.clusters[static_cast<size_t>(cidx)] = std::move(cm);
            continue;
        }

        // Build pointers + isi arrays
        std::vector<const float*> ptrs;
        ptrs.reserve(idxs.size());
        std::vector<float> isi;
        isi.reserve(idxs.size());
        for (int i : idxs) {
            ptrs.push_back(spikes + static_cast<size_t>(i) * N);
            isi.push_back(proxyIsi[i]);
        }

        // τ-grid search
        FitState bestSt;
        double   bestSse = std::numeric_limits<double>::infinity();
        float    bestTau = cfg.tauGrid.empty() ? 0.015f : cfg.tauGrid.front();
        std::vector<double> gridSse(cfg.tauGrid.size(),
                                     std::numeric_limits<double>::infinity());
        for (size_t gi = 0; gi < cfg.tauGrid.size(); ++gi) {
            FitState st;
            const double sse = fitOneTau(ptrs, isi, N, cfg.tauGrid[gi],
                                          cfg.maxIter, cfg.convTol, st);
            gridSse[gi] = sse;
            if (sse < bestSse) {
                bestSse = sse;
                bestTau = cfg.tauGrid[gi];
                bestSt  = std::move(st);
            }
        }

        if (!std::isfinite(bestSse) || bestSt.v.empty()) {
            cm.disabled = true;
            out.clusters[static_cast<size_t>(cidx)] = std::move(cm);
            continue;
        }

        // Parabolic τ-refinement (log space) if the best τ is interior to the grid
        for (size_t gi = 1; gi + 1 < cfg.tauGrid.size(); ++gi) {
            if (cfg.tauGrid[gi] == bestTau
                && std::isfinite(gridSse[gi - 1]) && std::isfinite(gridSse[gi + 1])) {
                const double yL = gridSse[gi - 1];
                const double yM = gridSse[gi];
                const double yR = gridSse[gi + 1];
                const double denom = 2.0 * (yL - 2.0 * yM + yR);
                if (denom > 1e-12) {
                    const double frac = (yL - yR) / denom;  // ∈ [-1, +1]
                    // Interpolate in log-τ
                    const double logL = std::log(cfg.tauGrid[gi - 1]);
                    const double logM = std::log(cfg.tauGrid[gi]);
                    const double logR = std::log(cfg.tauGrid[gi + 1]);
                    const double logTauRefined =
                        logM + 0.5 * frac * (logR - logL);
                    bestTau = static_cast<float>(std::exp(logTauRefined));
                }
                break;
            }
        }

        // Final fit at refined τ
        FitState finalSt;
        const double finalSse = fitOneTau(ptrs, isi, N, bestTau,
                                           cfg.maxIter, cfg.convTol, finalSt);
        if (std::isfinite(finalSse) && finalSse < bestSse * 1.05) {
            bestSt = std::move(finalSt);
            bestSse = finalSse;
        }

        cm.w0          = std::move(bestSt.w0);
        cm.v           = std::move(bestSt.v);
        cm.alpha       = bestSt.alpha;
        cm.tau         = bestTau;
        cm.fitResidVar = static_cast<float>(
            bestSse / (static_cast<double>(cm.nSpikes) * N));
        out.clusters[static_cast<size_t>(cidx)] = std::move(cm);
    }

    // 3. Diagnostic counters
    for (const auto& cm : out.clusters) {
        if (cm.disabled) ++out.nDisabled;
        else             ++out.nFit;
    }

    if (cfg.Verbose) {
        std::fprintf(stderr,
            "[adapt_model] fit=%d disabled=%d  (tauGrid: %zu values)\n",
            out.nFit, out.nDisabled, cfg.tauGrid.size());
    }
    return out;
}

}  // namespace adapt_model
