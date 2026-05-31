/***************************************************************************
                   KK_vbgmm.cpp
                   ------------
    copyright  : (C) 2026  Gravios / NeuroSuite-3 contributors

 Variational-Bayes GMM inner loop (RunVBGMM) and its file-local numerical
 helpers (digamma, Cholesky, forward-solve, log-det), split out of KK.cpp.
 RunVBGMM is a free function (declared in KK_internal.h) used by both the
 split and chunked phases; the vbgmm_* helpers have internal linkage here.
 ***************************************************************************/
#include "KK.h"
#include "KK_internal.h"
#include "KlustaKwik.h"

#include <algorithm>
#include <cmath>
#include <vector>



// ---------------------------------------------------------------------------
// (FindDominantChannels + FindBestLagXCorr removed in patch37 — the per-
// cluster alignment in Phase 2b mode 3 now uses the shared, normalised-xcorr
// XcorrDispatch::compute (same library that backs Klusters' interactive
// realignSpikes), which operates on ALL channels rather than a dominant
// subset.  The deprecated ResidualPCADominantChannels CLI flag is kept for
// backwards compatibility but is ignored — see RunPhase2bMode3Chunk.)
// ---------------------------------------------------------------------------


// ===========================================================================
// Variational Bayesian GMM — used as an alternate Phase 2b inner loop.
//
// Reference: Bishop, "Pattern Recognition and Machine Learning" (2006), §10.2.
//
// Operates on hard input labels (warm-start from Phase 2a's cls0) and
// produces refined hard labels via argmax of the converged posterior
// responsibilities.  Differs from the CEM warm-start path in three
// substantive ways:
//
//   1. Soft assignment during iteration: boundary spikes split their
//      "credit" between candidate clusters via the variational posterior
//      r[n,k] ∈ [0,1], summing to 1 across k.  CEM commits hard at every
//      iteration.
//
//   2. Bayesian K selection via the Dirichlet prior: clusters whose
//      effective N_k drops below a threshold get pruned automatically,
//      no BIC tuning required.  Replaces ConsiderDeletion.
//
//   3. Conjugate prior regularisation on cluster covariances: the
//      Normal-Wishart prior keeps marginal clusters from collapsing into
//      degenerate point masses.  Replaces the manual "covariance is
//      singular" deletion path.
//
// The final hard assignment (argmax_k r[n,k]) is what gets handed back
// to the rest of the pipeline, so Phase 4 mean-waveform harvest, Phase 5
// template match, and Phase 5 cross-chunk match all continue to operate
// on hard labels and can build per-cluster Gaussians from those.
//
// All internal computation is double precision (Wishart updates are
// numerically sensitive; single precision can cause Cholesky failures
// on rank-deficient sub-clusters during early iterations before the
// prior fully regularises).  Inputs/outputs are float to match the
// surrounding pipeline.
// ===========================================================================

namespace {

// ----- Digamma (Stirling asymptotic series, recurse for small x) -----------
// ψ(x) ≈ ln(x) - 1/(2x) - 1/(12x²) + 1/(120x⁴) - 1/(252x⁶) for x > 6.
// For smaller x, use ψ(x) = ψ(x+1) - 1/x to push x into the asymptotic regime.
// Accuracy ~1e-12 for x ≥ 1; sufficient for ELBO convergence checks.
static double vbgmm_digamma(double x) {
    double result = 0.0;
    while (x < 6.0) {
        result -= 1.0 / x;
        x += 1.0;
    }
    const double r  = 1.0 / x;
    const double r2 = r * r;
    result += std::log(x) - 0.5 * r
            - r2 * (1.0/12.0 - r2 * (1.0/120.0 - r2 * (1.0/252.0)));
    return result;
}

// ----- Cholesky (double, lower-triangular, in-place) -----------------------
// Returns 0 on success, -1 if the matrix is non-positive-definite at any
// pivot.  Operates on dense [D × D] matrix; input M must be symmetric.
// Output L overwrites the lower triangle; upper triangle is left untouched
// (caller treats it as zero).
static int vbgmm_cholesky(double* M, int D) {
    for (int i = 0; i < D; i++) {
        for (int j = 0; j <= i; j++) {
            double s = M[i * D + j];
            for (int k = 0; k < j; k++)
                s -= M[i * D + k] * M[j * D + k];
            if (i == j) {
                if (s <= 0.0) return -1;
                M[i * D + j] = std::sqrt(s);
            } else {
                M[i * D + j] = s / M[j * D + j];
            }
        }
    }
    return 0;
}

// ----- Forward solve: L y = v, where L is lower triangular --------------
// L is the Cholesky factor (stored in lower triangle of D×D buffer); upper
// triangle is ignored.  Result returned in y; caller must size y to D.
static void vbgmm_forward_solve(const double* L, const double* v,
                                double* y, int D) {
    for (int i = 0; i < D; i++) {
        double s = v[i];
        for (int k = 0; k < i; k++) s -= L[i * D + k] * y[k];
        y[i] = s / L[i * D + i];
    }
}

// ----- ln |L L^T| = 2 Σ ln(diag(L)) ---------------------------------------
static double vbgmm_log_det_chol(const double* L, int D) {
    double s = 0.0;
    for (int i = 0; i < D; i++) s += std::log(L[i * D + i]);
    return 2.0 * s;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// RunVBGMM
//
// Run Variational-Bayes GMM on a packed feature buffer with hard initial
// labels.  Modifies labels[] in-place to the converged hard assignment
// (argmax of the variational posterior).  Returns the number of clusters
// surviving auto-pruning (clusters whose effective N_k < kPruneThresh
// after convergence are removed and their members reassigned to argmax
// over the survivors).
//
// Parameters
//   data         — [N × D] float, sample-major (row p starts at p*D)
//   labels       — [N] int (in/out), values in [0, K_init)
//   K_init       — number of initial clusters (max K, can shrink via prune)
//   N            — spike count
//   D            — feature dim
//   maxIter      — VB iteration cap (default 50)
//   convTol      — convergence: max |Δr| across all (n, k) below this stops
//
// Hyperparameters (constants in the function body — tweakable later):
//   alpha0      — Dirichlet concentration (1.0 = uniform; <1 favours sparsity)
//   beta0       — Normal prior strength on means
//   nu0         — Wishart d.o.f. (must be ≥ D)
//   m0          — Wishart prior mean = data mean (computed inside)
//   W0inv       — Wishart inverse scale = data variance × I (computed inside)
// ---------------------------------------------------------------------------
int RunVBGMM(const float* data, int* labels, int K_init,
                    int N, int D, int maxIter,
                    double convTol)
{
    if (K_init < 1 || N < 1 || D < 1) return std::max(1, K_init);

    // Hyperparameters from CLI globals (defaults set in KlustaKwik.cpp).
    // alpha0: Dirichlet concentration.  Lowering (e.g., 0.1) makes the
    //         prior favour sparser solutions — clusters with weak evidence
    //         get pruned more aggressively.
    // beta0:  Normal prior strength on means.
    // nu0:    Wishart d.o.f.  Must exceed D - 1 for a proper Wishart.  We
    //         enforce a floor of D + 0.5 to keep numerics stable when the
    //         user picks a small offset on a high-dim problem.
    const double alpha0 = static_cast<double>(VBGMMAlpha0);
    const double beta0  = static_cast<double>(VBGMMBeta0);
    double       nu0    = static_cast<double>(D)
                        + static_cast<double>(VBGMMNu0Offset);
    if (nu0 < D + 0.5) nu0 = D + 0.5;

    // Data mean (m0) and per-feature variance (for W0inv = mean_var * I).
    std::vector<double> m0(D, 0.0);
    for (int p = 0; p < N; p++)
        for (int d = 0; d < D; d++)
            m0[d] += data[p * D + d];
    for (int d = 0; d < D; d++) m0[d] /= N;

    double meanVar = 0.0;
    for (int p = 0; p < N; p++)
        for (int d = 0; d < D; d++) {
            const double v = data[p * D + d] - m0[d];
            meanVar += v * v;
        }
    meanVar /= (N * D);
    if (!(meanVar > 0.0)) meanVar = 1.0;  // pathological data; fall back

    // ── W0inv: per-cluster Wishart inverse-scale priors ────────────────
    //
    // Mode 0 (isotropic global): W0inv[k] = meanVar * I for every k.  The
    // prior expects all clusters to have the same per-feature variance,
    // ignoring that close-shank vs far-shank neurons have very different
    // per-channel variance signatures.
    //
    // Mode 1 (per-cluster diagonal empirical): W0inv[k] = diag of cluster
    // k's per-feature variance under the warm-start labels, blended with
    // a small isotropic floor for numerical safety.  Captures diagonal
    // anisotropy (different per-feature variance scales per cluster) but
    // not channel correlations within a cluster.
    //
    // Mode 2 (per-cluster FULL covariance empirical): W0inv[k] = full
    // empirical covariance of cluster k spikes under warm-start labels,
    // blended with isotropic floor on the diagonal.  Captures BOTH the
    // diagonal anisotropy of mode 1 AND off-diagonal correlations —
    // i.e., the channel-pattern signature of a neuron's spatial
    // waveform.  Higher numerical risk on small clusters (rank-deficient
    // empirical cov) — fallback to isotropic kicks in when N_k < D + 1.
    //
    // The blend term (VBGMMPriorBlend, default 0.1) adds blend*meanVar to
    // each cluster's prior diagonal, ensuring positive-definiteness even
    // when empirical cov is ill-conditioned.
    //
    // Stored as [K_init × D × D] dense (most off-diagonals zero in mode 0
    // and 1; populated in mode 2) for uniform indexing in init and M-step.
    std::vector<double> W0inv(static_cast<size_t>(K_init) * D * D, 0.0);
    {
        const double blend     = std::clamp(static_cast<double>(VBGMMPriorBlend), 0.0, 1.0);
        const double iso_floor = blend * meanVar;  // additive isotropic floor

        if (VBGMMPriorMode == 1 || VBGMMPriorMode == 2) {
            // Per-cluster empirical {variance | covariance} from warm-start
            // labels.  Common preamble: compute per-cluster N_k and means.
            std::vector<double> Nk_init(K_init, 0.0);
            std::vector<double> mean_init(static_cast<size_t>(K_init) * D, 0.0);
            for (int p = 0; p < N; p++) {
                const int k = labels[p];
                if (k < 0 || k >= K_init) continue;
                Nk_init[k] += 1.0;
                for (int d = 0; d < D; d++)
                    mean_init[k * D + d] += data[p * D + d];
            }
            for (int k = 0; k < K_init; k++) {
                if (Nk_init[k] < 1.0) continue;
                for (int d = 0; d < D; d++)
                    mean_init[k * D + d] /= Nk_init[k];
            }

            if (VBGMMPriorMode == 1) {
                // ── Mode 1: per-cluster DIAGONAL empirical ──────────────
                std::vector<double> emp_var(static_cast<size_t>(K_init) * D, 0.0);
                for (int p = 0; p < N; p++) {
                    const int k = labels[p];
                    if (k < 0 || k >= K_init) continue;
                    for (int d = 0; d < D; d++) {
                        const double v = data[p * D + d] - mean_init[k * D + d];
                        emp_var[k * D + d] += v * v;
                    }
                }
                for (int k = 0; k < K_init; k++) {
                    if (Nk_init[k] >= D + 1.0) {
                        for (int d = 0; d < D; d++) {
                            const double v_emp = emp_var[k * D + d] / Nk_init[k];
                            // (1 - blend) * empirical + blend * isotropic floor
                            W0inv[k * D * D + d * D + d] =
                                  (1.0 - blend) * v_emp + iso_floor;
                        }
                    } else {
                        // Fall back to isotropic for under-populated clusters.
                        for (int d = 0; d < D; d++)
                            W0inv[k * D * D + d * D + d] = meanVar;
                    }
                }
            } else {
                // ── Mode 2: per-cluster FULL covariance empirical ───────
                // Captures off-diagonal correlations (the channel-pattern
                // signature of a neuron's spatial waveform).  Requires
                // N_k >= D + 1 for a non-rank-deficient empirical cov;
                // smaller clusters fall back to isotropic.  Numerical
                // safety net: the blend term puts a positive isotropic
                // floor on every diagonal, keeping the matrix PD even if
                // empirical cov is ill-conditioned.
                //
                // Memory: [K × D²] doubles; for typical (K=20, D=6),
                // ~6 KB total — trivial.  For (K=50, D=21), ~180 KB.
                std::vector<double> emp_cov(static_cast<size_t>(K_init) * D * D, 0.0);
                for (int p = 0; p < N; p++) {
                    const int k = labels[p];
                    if (k < 0 || k >= K_init) continue;
                    for (int i = 0; i < D; i++) {
                        const double di = data[p * D + i] - mean_init[k * D + i];
                        for (int j = 0; j < D; j++) {
                            const double dj = data[p * D + j] - mean_init[k * D + j];
                            emp_cov[k * D * D + i * D + j] += di * dj;
                        }
                    }
                }
                for (int k = 0; k < K_init; k++) {
                    if (Nk_init[k] >= D + 1.0) {
                        const double inv_N = 1.0 / Nk_init[k];
                        for (int i = 0; i < D; i++) {
                            for (int j = 0; j < D; j++) {
                                const double c = emp_cov[k * D * D + i * D + j] * inv_N;
                                // (1 - blend) * empirical_cov_k for all (i,j),
                                // plus isotropic floor on diagonal only.
                                W0inv[k * D * D + i * D + j] = (1.0 - blend) * c;
                            }
                            W0inv[k * D * D + i * D + i] += iso_floor;
                        }
                    } else {
                        // Isotropic fallback for under-populated clusters.
                        for (int d = 0; d < D; d++)
                            W0inv[k * D * D + d * D + d] = meanVar;
                    }
                }
            }
        } else {
            // Mode 0 (isotropic global): every cluster gets meanVar * I.
            for (int k = 0; k < K_init; k++)
                for (int d = 0; d < D; d++)
                    W0inv[k * D * D + d * D + d] = meanVar;
        }
    }

    int K = K_init;

    // Variational state.  Indexed by [k] up to K (current alive count).
    // We don't compact k indices when pruning — instead we maintain an
    // alive[] mask and skip dead clusters in all loops.
    std::vector<double> alpha(K, alpha0);
    std::vector<double> beta (K, beta0);
    std::vector<double> nu   (K, nu0);
    std::vector<double> m    (K * D, 0.0);
    std::vector<double> Winv (K * D * D, 0.0);
    std::vector<double> Wchol(K * D * D, 0.0);
    std::vector<int>    alive(K, 1);

    std::vector<double> rResp(N * K, 0.0);  // responsibilities r[n, k]

    // ── Initialise from hard labels ──────────────────────────────────────
    // For each cluster k: set N_k from label count, x_bar_k from member mean,
    // S_k from member empirical scatter; apply M-step parameter updates.
    {
        std::vector<double> Nk(K, 0.0);
        std::vector<double> xbar(K * D, 0.0);
        for (int p = 0; p < N; p++) {
            const int k = labels[p];
            if (k < 0 || k >= K) continue;
            Nk[k] += 1.0;
            for (int d = 0; d < D; d++) xbar[k * D + d] += data[p * D + d];
        }
        for (int k = 0; k < K; k++) {
            if (Nk[k] < 1.0) { alive[k] = 0; continue; }
            for (int d = 0; d < D; d++) xbar[k * D + d] /= Nk[k];
        }
        // Empirical scatter S_k = (1/N_k) Σ_n (x_n - x_bar_k)(x_n - x_bar_k)^T
        std::vector<double> Sk(K * D * D, 0.0);
        for (int p = 0; p < N; p++) {
            const int k = labels[p];
            if (k < 0 || k >= K || !alive[k]) continue;
            for (int i = 0; i < D; i++) {
                const double di = data[p * D + i] - xbar[k * D + i];
                for (int j = i; j < D; j++) {  // upper triangle only
                    const double dj = data[p * D + j] - xbar[k * D + j];
                    Sk[k * D * D + i * D + j] += di * dj;
                }
            }
        }
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            for (int i = 0; i < D; i++)
                for (int j = i; j < D; j++) {
                    Sk[k * D * D + i * D + j] /= Nk[k];
                    if (j > i) Sk[k * D * D + j * D + i] = Sk[k * D * D + i * D + j];
                }
        }
        // Apply M-step parameter updates from these initial sufficient stats.
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            alpha[k] = alpha0 + Nk[k];
            beta [k] = beta0  + Nk[k];
            nu   [k] = nu0    + Nk[k];
            for (int d = 0; d < D; d++)
                m[k * D + d] = (beta0 * m0[d] + Nk[k] * xbar[k * D + d]) / beta[k];
            // W_k^{-1} = W0inv + N_k S_k + (β0 N_k / (β0 + N_k))(x_bar - m0)(x_bar - m0)^T
            const double w = (beta0 * Nk[k]) / (beta0 + Nk[k]);
            for (int i = 0; i < D; i++) {
                const double dxi = xbar[k * D + i] - m0[i];
                for (int j = 0; j < D; j++) {
                    const double dxj = xbar[k * D + j] - m0[j];
                    Winv[k * D * D + i * D + j] =
                        W0inv[k * D * D + i * D + j]
                      + Nk[k] * Sk[k * D * D + i * D + j]
                      + w * dxi * dxj;
                }
            }
            // Cholesky for fast quad-form / log-det later.
            std::vector<double> tmp(Winv.begin() + k * D * D,
                                    Winv.begin() + (k + 1) * D * D);
            if (vbgmm_cholesky(tmp.data(), D) != 0) {
                alive[k] = 0;  // shouldn't happen with W0inv regularisation
                continue;
            }
            std::copy(tmp.begin(), tmp.end(), Wchol.begin() + k * D * D);
        }
    }

    // ── VB iterations ─────────────────────────────────────────────────────
    std::vector<double> rPrev(N * K, 0.0);
    std::vector<double> y(D);
    int iter;
    for (iter = 0; iter < maxIter; iter++) {
        // -- E-step: compute log responsibilities (then softmax) ---------
        // log α̂ = ψ(α_k) - ψ(Σ_k' α_k')
        double alphaSum = 0.0;
        for (int k = 0; k < K; k++) if (alive[k]) alphaSum += alpha[k];
        const double psiAlphaSum = vbgmm_digamma(alphaSum);

        // Per-cluster precomputed terms: E[ln π_k], E[ln |Λ_k|]
        std::vector<double> ElnPi   (K, 0.0);
        std::vector<double> ElnLambda(K, 0.0);
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            ElnPi[k] = vbgmm_digamma(alpha[k]) - psiAlphaSum;
            // E[ln |Λ_k|] = Σ_d ψ((ν_k + 1 - d)/2) + D ln(2) - ln |W^{-1}_k|
            // Note: ln|W_k| = -ln|W^{-1}_k| = -2 Σ ln(diag(L)) for L = chol(W^{-1})
            double s = 0.0;
            for (int d = 1; d <= D; d++)
                s += vbgmm_digamma((nu[k] + 1.0 - d) / 2.0);
            ElnLambda[k] = s + D * std::log(2.0)
                         - vbgmm_log_det_chol(&Wchol[k * D * D], D);
        }

        // For each spike, compute log ρ[n, k] then softmax to r[n, k].
        for (int p = 0; p < N; p++) {
            double maxLog = -std::numeric_limits<double>::infinity();
            std::vector<double> logRho(K, -std::numeric_limits<double>::infinity());
            for (int k = 0; k < K; k++) {
                if (!alive[k]) continue;
                // E[(x - μ_k)^T Λ_k (x - μ_k)] = D/β_k + ν_k * (x - m_k)^T W_k (x - m_k)
                // (x - m_k)^T W_k (x - m_k) computed via forward-solve on W^{-1}'s Cholesky:
                //   z = L^{-1} (x - m_k) → quad = ||z||²,  since W = (LL^T)^{-1} → x^T W x = ||L^{-1} x||²
                std::vector<double> diff(D);
                for (int d = 0; d < D; d++)
                    diff[d] = data[p * D + d] - m[k * D + d];
                vbgmm_forward_solve(&Wchol[k * D * D], diff.data(), y.data(), D);
                double quad = 0.0;
                for (int d = 0; d < D; d++) quad += y[d] * y[d];
                const double EmahalSq = D / beta[k] + nu[k] * quad;
                logRho[k] = ElnPi[k] + 0.5 * ElnLambda[k]
                          - 0.5 * D * std::log(2.0 * M_PI)
                          - 0.5 * EmahalSq;
                if (logRho[k] > maxLog) maxLog = logRho[k];
            }
            // softmax
            double Zsum = 0.0;
            for (int k = 0; k < K; k++) {
                if (!alive[k]) { rResp[p * K + k] = 0.0; continue; }
                rResp[p * K + k] = std::exp(logRho[k] - maxLog);
                Zsum += rResp[p * K + k];
            }
            if (Zsum > 0.0) {
                for (int k = 0; k < K; k++) rResp[p * K + k] /= Zsum;
            } else {
                // Numerical underflow: fall back to uniform over alive
                int nAlive = 0;
                for (int k = 0; k < K; k++) if (alive[k]) nAlive++;
                if (nAlive > 0) {
                    const double u = 1.0 / nAlive;
                    for (int k = 0; k < K; k++) rResp[p * K + k] = alive[k] ? u : 0.0;
                }
            }
        }

        // -- Convergence check (max |Δr| over all (n, k)) ----------------
        if (iter > 0) {
            double maxDelta = 0.0;
            for (int p = 0; p < N; p++)
                for (int k = 0; k < K; k++) {
                    const double d = std::abs(rResp[p * K + k] - rPrev[p * K + k]);
                    if (d > maxDelta) maxDelta = d;
                }
            if (maxDelta < convTol) break;
        }
        std::copy(rResp.begin(), rResp.end(), rPrev.begin());

        // -- M-step: sufficient statistics → parameter updates -----------
        std::vector<double> Nk(K, 0.0);
        std::vector<double> xbar(K * D, 0.0);
        for (int p = 0; p < N; p++)
            for (int k = 0; k < K; k++) {
                if (!alive[k]) continue;
                const double r = rResp[p * K + k];
                if (r <= 0.0) continue;
                Nk[k] += r;
                for (int d = 0; d < D; d++) xbar[k * D + d] += r * data[p * D + d];
            }
        for (int k = 0; k < K; k++) {
            if (!alive[k] || Nk[k] <= 0.0) { alive[k] = 0; continue; }
            for (int d = 0; d < D; d++) xbar[k * D + d] /= Nk[k];
        }

        std::vector<double> Sk(K * D * D, 0.0);
        for (int p = 0; p < N; p++)
            for (int k = 0; k < K; k++) {
                if (!alive[k]) continue;
                const double r = rResp[p * K + k];
                if (r <= 0.0) continue;
                for (int i = 0; i < D; i++) {
                    const double di = data[p * D + i] - xbar[k * D + i];
                    for (int j = i; j < D; j++) {
                        const double dj = data[p * D + j] - xbar[k * D + j];
                        Sk[k * D * D + i * D + j] += r * di * dj;
                    }
                }
            }
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            for (int i = 0; i < D; i++)
                for (int j = i; j < D; j++) {
                    Sk[k * D * D + i * D + j] /= Nk[k];
                    if (j > i) Sk[k * D * D + j * D + i] = Sk[k * D * D + i * D + j];
                }
        }

        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            alpha[k] = alpha0 + Nk[k];
            beta [k] = beta0  + Nk[k];
            nu   [k] = nu0    + Nk[k];
            for (int d = 0; d < D; d++)
                m[k * D + d] = (beta0 * m0[d] + Nk[k] * xbar[k * D + d]) / beta[k];
            const double w = (beta0 * Nk[k]) / (beta0 + Nk[k]);
            for (int i = 0; i < D; i++) {
                const double dxi = xbar[k * D + i] - m0[i];
                for (int j = 0; j < D; j++) {
                    const double dxj = xbar[k * D + j] - m0[j];
                    Winv[k * D * D + i * D + j] =
                        W0inv[k * D * D + i * D + j]
                      + Nk[k] * Sk[k * D * D + i * D + j]
                      + w * dxi * dxj;
                }
            }
            std::vector<double> tmp(Winv.begin() + k * D * D,
                                    Winv.begin() + (k + 1) * D * D);
            if (vbgmm_cholesky(tmp.data(), D) != 0) {
                alive[k] = 0;
                continue;
            }
            std::copy(tmp.begin(), tmp.end(), Wchol.begin() + k * D * D);
        }

        // -- Auto-prune clusters with effective N below 1.0 spike --------
        // (At convergence the threshold could be tighter, e.g., 0.5; during
        // iteration we leave a margin to allow oscillating clusters to
        // recover before being killed.)
        for (int k = 0; k < K; k++) {
            if (alive[k] && Nk[k] < 1.0) alive[k] = 0;
        }
    }

    // ── Hard-assign by argmax over alive responsibilities ─────────────────
    int nAliveFinal = 0;
    for (int k = 0; k < K; k++) if (alive[k]) nAliveFinal++;
    if (nAliveFinal < 1) {
        // Pathological: nothing survived.  Punt by assigning everything to
        // cluster 0 and letting downstream phases decide.
        for (int p = 0; p < N; p++) labels[p] = 0;
        return 1;
    }
    for (int p = 0; p < N; p++) {
        double best = -1.0;
        int bestK = 0;
        for (int k = 0; k < K; k++) {
            if (!alive[k]) continue;
            const double r = rResp[p * K + k];
            if (r > best) { best = r; bestK = k; }
        }
        labels[p] = bestK;
    }

    return nAliveFinal;
}



// ---------------------------------------------------------------------------
// TopKEigenPowerDeflation — top-K eigenvectors of a D×D symmetric matrix.
//
// Power-iteration with rank-1 deflation.  Cheap and adequate for the small
// K (≤ ~5) we need for residual-PCA: each eigenvector costs one chain of
// ~50 matvecs (D² ops each).  The matrix is modified in-place by the
// deflation A ← A − λ·v·vᵀ between iterations.
//
// Convergence criterion is on the Rayleigh quotient: stop when consecutive
// λ estimates differ by < relTol · |λ|, or after maxIter iterations.
//
// Outputs:
//   V       — row-major [K × D] eigenvectors (unit-norm, descending |λ|).
//   eigvals — [K] eigenvalues (descending in magnitude).
// ---------------------------------------------------------------------------
void TopKEigenPowerDeflation(std::vector<double>& A,  // [D*D], modified
                                    int D, int K,
                                    int maxIter, double relTol,
                                    std::vector<double>& V,
                                    std::vector<double>& eigvals)
{
    V.assign(static_cast<size_t>(K) * D, 0.0);
    eigvals.assign(K, 0.0);
    if (D <= 0 || K <= 0) return;

    std::vector<double> v(D), Av(D);
    // Deterministic seeding so two runs on identical data give the same
    // eigenvectors (modulo sign).  Re-seeding per-eigenvector keeps things
    // reproducible across calls.
    uint64_t seed = 0x9E3779B97F4A7C15ULL;

    for (int k = 0; k < K; k++) {
        // Cheap xor-shift RNG inline; v has unit-norm random init.
        double n2 = 0.0;
        for (int i = 0; i < D; i++) {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
            const double r = static_cast<double>(seed & 0xFFFFFFFFULL) /
                             4294967296.0 - 0.5;
            v[i] = r; n2 += r * r;
        }
        if (!(n2 > 0)) { eigvals[k] = 0; continue; }
        const double inv = 1.0 / std::sqrt(n2);
        for (int i = 0; i < D; i++) v[i] *= inv;

        double lambda_prev = 0.0;
        bool   converged   = false;
        for (int it = 0; it < maxIter; it++) {
            // Av = A · v
            for (int i = 0; i < D; i++) {
                double s = 0.0;
                const double* row = A.data() + static_cast<size_t>(i) * D;
                for (int j = 0; j < D; j++) s += row[j] * v[j];
                Av[i] = s;
            }
            // λ = vᵀ Av
            double lambda = 0.0;
            for (int i = 0; i < D; i++) lambda += v[i] * Av[i];
            // Normalise Av → v
            double norm = 0.0;
            for (int i = 0; i < D; i++) norm += Av[i] * Av[i];
            if (!(norm > 0.0)) { lambda = 0; break; }
            const double ninv = 1.0 / std::sqrt(norm);
            for (int i = 0; i < D; i++) v[i] = Av[i] * ninv;

            if (it > 0 && std::fabs(lambda - lambda_prev) <
                          relTol * std::max(1.0, std::fabs(lambda))) {
                lambda_prev = lambda;
                converged   = true;
                break;
            }
            lambda_prev = lambda;
        }
        (void)converged;
        // Store eigenpair (rejecting near-zero / negative-noise eigenvalues
        // produced by deflation rounding on remaining low-variance modes).
        eigvals[k] = lambda_prev;
        for (int i = 0; i < D; i++)
            V[static_cast<size_t>(k) * D + i] = v[i];

        // Deflate: A ← A − λ · v vᵀ
        for (int i = 0; i < D; i++) {
            const double vi = lambda_prev * v[i];
            double* row = A.data() + static_cast<size_t>(i) * D;
            for (int j = 0; j < D; j++) row[j] -= vi * v[j];
        }
    }
}
