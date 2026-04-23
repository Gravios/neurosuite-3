// =============================================================================
// dipsplit.cpp — implementations for the bimodality-detection helpers.
// See dipsplit.h for algorithm overview.
// =============================================================================
#include "dipsplit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>

namespace dipsplit {

// -----------------------------------------------------------------------------
// valley_test
//
// 1. Standard-deviation & Silverman bandwidth:
//      h = 1.06 · σ · n^(-1/5)
//    (use inter-quartile range when σ is very small relative to range)
// 2. Grid of G=300 points spanning [min−3h, max+3h]
// 3. KDE with Gaussian kernel at each grid point
// 4. Scan for local maxima (peaks) and local minima (valleys)
// 5. For each valley between two adjacent peaks, depth = 1 − v / min(p_left, p_right)
// 6. Return the deepest valley.
// -----------------------------------------------------------------------------
ValleyResult valley_test(const double* samples, int n, double threshold)
{
    ValleyResult result;
    if (n < 10) return result;   // too few samples for meaningful KDE

    // -- 1. Statistics -------------------------------------------------------
    double mean = 0.0;
    for (int i = 0; i < n; ++i) mean += samples[i];
    mean /= n;
    double var = 0.0;
    for (int i = 0; i < n; ++i) {
        const double d = samples[i] - mean;
        var += d * d;
    }
    var /= std::max(1, n - 1);
    double sigma = std::sqrt(std::max(var, 0.0));
    if (!(sigma > 0.0)) return result;

    // Silverman's rule-of-thumb bandwidth (works well for roughly unimodal
    // distributions; slightly oversmooths for strongly bimodal inputs, which
    // is CONSERVATIVE for our purpose — tends to miss weak bimodality rather
    // than hallucinate it).
    const double h = 1.06 * sigma * std::pow(static_cast<double>(n), -0.2);
    if (!(h > 0.0)) return result;

    // -- 2. Grid extent ------------------------------------------------------
    double vmin = samples[0], vmax = samples[0];
    for (int i = 1; i < n; ++i) {
        if (samples[i] < vmin) vmin = samples[i];
        if (samples[i] > vmax) vmax = samples[i];
    }
    const double pad = 3.0 * h;
    const double x0  = vmin - pad;
    const double x1  = vmax + pad;
    constexpr int G = 301;
    std::vector<double> xs(G), dens(G, 0.0);
    for (int g = 0; g < G; ++g)
        xs[g] = x0 + (x1 - x0) * static_cast<double>(g) / (G - 1);

    // -- 3. KDE --------------------------------------------------------------
    // f(x) = (1 / nh) · Σ_i K((x − xᵢ) / h)    K(z) = (2π)^(-1/2) · exp(−z²/2)
    const double inv_nh = 1.0 / (static_cast<double>(n) * h);
    const double norm   = 1.0 / std::sqrt(2.0 * M_PI);
    for (int g = 0; g < G; ++g) {
        double sum = 0.0;
        for (int i = 0; i < n; ++i) {
            const double z = (xs[g] - samples[i]) / h;
            sum += norm * std::exp(-0.5 * z * z);
        }
        dens[g] = sum * inv_nh;
    }

    // -- 4. Find peaks and valleys (strict inequalities, 1-point window) -----
    //
    // We use a simple 3-point rule; for anything beyond weakly-noisy KDEs
    // this is plenty since the bandwidth already smooths out micro-jitter.
    // Exclude boundary points from peaks/valleys (they're artefacts of the
    // padding, not real structure).
    std::vector<int> peak_idx, valley_idx;
    for (int g = 1; g < G - 1; ++g) {
        if (dens[g] > dens[g-1] && dens[g] > dens[g+1])
            peak_idx.push_back(g);
        else if (dens[g] < dens[g-1] && dens[g] < dens[g+1])
            valley_idx.push_back(g);
    }

    if (peak_idx.size() < 2) {
        result.is_bimodal = false;
        return result;
    }

    // -- 5. Scan every adjacent peak pair for the deepest valley between ----
    double best_depth = 0.0;
    int    best_left  = -1;
    int    best_right = -1;
    int    best_valley_g = -1;
    for (size_t p = 0; p + 1 < peak_idx.size(); ++p) {
        const int L = peak_idx[p];
        const int R = peak_idx[p + 1];
        // Find deepest valley between L and R
        double v_min  = std::numeric_limits<double>::infinity();
        int    v_g    = -1;
        for (int g = L + 1; g < R; ++g) {
            if (dens[g] < v_min) { v_min = dens[g]; v_g = g; }
        }
        if (v_g < 0) continue;
        const double ph = std::min(dens[L], dens[R]);
        if (!(ph > 0.0)) continue;
        const double depth = 1.0 - v_min / ph;
        if (depth > best_depth) {
            best_depth    = depth;
            best_left     = L;
            best_right    = R;
            best_valley_g = v_g;
        }
    }

    result.depth      = best_depth;
    result.is_bimodal = (best_depth >= threshold);
    if (best_valley_g >= 0) {
        result.valley_loc = xs[best_valley_g];
        result.left_peak  = xs[best_left];
        result.right_peak = xs[best_right];
    }
    return result;
}

// -----------------------------------------------------------------------------
// top_pcs_power_iteration — power iteration on the d×d centred covariance.
// d is typically ≤ 32 for our spike-sort feature spaces, so O(d²) ops are
// trivially small.  Power iteration converges exponentially in the ratio
// of adjacent singular values, which for spike features is well-behaved.
// -----------------------------------------------------------------------------
void top_pcs_power_iteration(
    const float* X, int nPoints, int d, int k,
    double* pcs_out, int max_iters, double tol)
{
    if (d <= 0 || k <= 0 || nPoints < 2) return;

    // Centre → build covariance C = (Xᵀ X) / (n−1) ------------------------
    std::vector<double> mean(d, 0.0);
    for (int i = 0; i < nPoints; ++i)
        for (int j = 0; j < d; ++j)
            mean[j] += static_cast<double>(X[i * d + j]);
    for (int j = 0; j < d; ++j) mean[j] /= nPoints;

    std::vector<double> cov(static_cast<size_t>(d) * d, 0.0);
    for (int i = 0; i < nPoints; ++i) {
        const float* row = X + i * d;
        for (int a = 0; a < d; ++a) {
            const double ra = row[a] - mean[a];
            for (int b = a; b < d; ++b) {
                cov[a * d + b] += ra * (row[b] - mean[b]);
            }
        }
    }
    const double inv_dof = 1.0 / std::max(1, nPoints - 1);
    for (int a = 0; a < d; ++a)
        for (int b = a; b < d; ++b) {
            cov[a * d + b] *= inv_dof;
            if (b != a) cov[b * d + a] = cov[a * d + b];
        }

    std::mt19937 rng(0xA1C2E3);
    std::normal_distribution<double> gauss(0.0, 1.0);

    std::vector<double> Cv(d);
    std::vector<double> v (d);
    std::vector<double> prev(d);

    for (int pc = 0; pc < k; ++pc) {
        // Random init
        for (int j = 0; j < d; ++j) v[j] = gauss(rng);
        // Orthogonalise against previous PCs (Gram-Schmidt)
        for (int q = 0; q < pc; ++q) {
            const double* u = pcs_out + q * d;
            double dot = 0.0;
            for (int j = 0; j < d; ++j) dot += v[j] * u[j];
            for (int j = 0; j < d; ++j) v[j] -= dot * u[j];
        }
        // Normalise
        double nrm = 0.0;
        for (int j = 0; j < d; ++j) nrm += v[j] * v[j];
        nrm = std::sqrt(nrm);
        if (!(nrm > 0.0)) {
            // Fallback: initialise to unit axis pc
            std::fill(v.begin(), v.end(), 0.0);
            v[pc % d] = 1.0;
        } else {
            for (int j = 0; j < d; ++j) v[j] /= nrm;
        }

        // Iterate: v ← C v, orthogonalise, normalise
        for (int it = 0; it < max_iters; ++it) {
            std::copy(v.begin(), v.end(), prev.begin());
            // Cv = C · v
            for (int a = 0; a < d; ++a) {
                double s = 0.0;
                for (int b = 0; b < d; ++b) s += cov[a * d + b] * v[b];
                Cv[a] = s;
            }
            // Orthogonalise against previous PCs
            for (int q = 0; q < pc; ++q) {
                const double* u = pcs_out + q * d;
                double dot = 0.0;
                for (int j = 0; j < d; ++j) dot += Cv[j] * u[j];
                for (int j = 0; j < d; ++j) Cv[j] -= dot * u[j];
            }
            // Normalise
            double nrm2 = 0.0;
            for (int j = 0; j < d; ++j) nrm2 += Cv[j] * Cv[j];
            nrm2 = std::sqrt(nrm2);
            if (!(nrm2 > 0.0)) break;
            for (int j = 0; j < d; ++j) v[j] = Cv[j] / nrm2;

            // Convergence: | v · prev | ≈ 1
            double dotp = 0.0;
            for (int j = 0; j < d; ++j) dotp += v[j] * prev[j];
            if (std::abs(std::abs(dotp) - 1.0) < tol) break;
        }

        std::copy(v.begin(), v.end(), pcs_out + pc * d);
    }
}

// -----------------------------------------------------------------------------
// kmeans2_refine — Lloyd iterations with hard assignments, 2 clusters.
// -----------------------------------------------------------------------------
int kmeans2_refine(
    const float* X, int nPoints, int d,
    const double* c0_in, const double* c1_in,
    int* labels_out, int max_iters)
{
    std::vector<double> c0(c0_in, c0_in + d);
    std::vector<double> c1(c1_in, c1_in + d);

    int iter = 0;
    for (iter = 0; iter < max_iters; ++iter) {
        int changed = 0;
        // Assign
        for (int i = 0; i < nPoints; ++i) {
            const float* row = X + i * d;
            double d0 = 0.0, d1 = 0.0;
            for (int j = 0; j < d; ++j) {
                const double a0 = row[j] - c0[j];
                const double a1 = row[j] - c1[j];
                d0 += a0 * a0;
                d1 += a1 * a1;
            }
            const int new_lbl = (d1 < d0) ? 1 : 0;
            if (labels_out[i] != new_lbl) ++changed;
            labels_out[i] = new_lbl;
        }
        if (iter > 0 && changed == 0) break;

        // Recompute centroids
        std::vector<double> s0(d, 0.0), s1(d, 0.0);
        int n0 = 0, n1 = 0;
        for (int i = 0; i < nPoints; ++i) {
            const float* row = X + i * d;
            if (labels_out[i] == 0) {
                for (int j = 0; j < d; ++j) s0[j] += row[j];
                ++n0;
            } else {
                for (int j = 0; j < d; ++j) s1[j] += row[j];
                ++n1;
            }
        }
        if (n0 == 0 || n1 == 0) break;   // degenerate — one cluster empty
        for (int j = 0; j < d; ++j) {
            c0[j] = s0[j] / n0;
            c1[j] = s1[j] / n1;
        }
    }
    return iter;
}

// -----------------------------------------------------------------------------
// bic_two_vs_one — diagonal-covariance Gaussian BIC for k=1 and k=2.
//
// ln L(θ | X) = −(n/2) Σ_d [ln(2π) + ln(σ_d²) + 1]    for a single cluster
// BIC = −2 ln L + p · ln n,  p = d (mean) + d (variance)  per cluster (+ mix if k>1)
// -----------------------------------------------------------------------------
static double log_likelihood_diag(
    const float* X, int n, int d,
    const int* labels, int k)
{
    // Per-cluster mean and per-dim variance.
    std::vector<double> sum   (static_cast<size_t>(k) * d, 0.0);
    std::vector<double> sum_sq(static_cast<size_t>(k) * d, 0.0);
    std::vector<int>    cnt   (k, 0);

    for (int i = 0; i < n; ++i) {
        const int lbl = labels ? labels[i] : 0;
        const float* row = X + i * d;
        for (int j = 0; j < d; ++j) {
            sum   [lbl * d + j] += row[j];
            sum_sq[lbl * d + j] += static_cast<double>(row[j]) * row[j];
        }
        ++cnt[lbl];
    }

    // Per-cluster, per-dim variance, then log-likelihood contribution.
    double logL = 0.0;
    for (int c = 0; c < k; ++c) {
        const int nc = cnt[c];
        if (nc < 2) continue;
        for (int j = 0; j < d; ++j) {
            const double m = sum[c * d + j] / nc;
            double v = sum_sq[c * d + j] / nc - m * m;
            // Floor the variance to avoid log(0).  A 1e-10 floor is fine;
            // real spike features never have true zero variance.
            if (v < 1e-10) v = 1e-10;
            // ln L += −(nc/2) · (ln(2π) + ln v + 1)
            logL -= 0.5 * nc * (std::log(2.0 * M_PI) + std::log(v) + 1.0);
        }
    }
    return logL;
}

BicPair bic_two_vs_one(const float* X, int n, int d, const int* labels_k2)
{
    BicPair out;
    // k=1: treat all points as one cluster (zero labels)
    std::vector<int> zeros(static_cast<size_t>(n), 0);
    const double L1 = log_likelihood_diag(X, n, d, zeros.data(), 1);
    const double L2 = log_likelihood_diag(X, n, d, labels_k2, 2);

    // Free parameters: per-cluster mean (d) + per-cluster per-dim variance (d).
    // k=1: 2d free.  k=2: 4d + 1 (mixing weight).
    const double p1 = 2.0 * d;
    const double p2 = 4.0 * d + 1.0;
    const double lnN = std::log(static_cast<double>(std::max(1, n)));
    out.bic_k1 = -2.0 * L1 + p1 * lnN;
    out.bic_k2 = -2.0 * L2 + p2 * lnN;
    return out;
}

}  // namespace dipsplit
