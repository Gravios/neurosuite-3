// dipsplit_test.cpp — core/dipsplit.hpp on point sets with a known answer.
//
// The bimodality gate, the principal components and their eigenvalues, the
// two-way k-means and the BIC comparison, each on data built so the right
// result is known: one Gaussian versus two well-separated ones, and an
// anisotropic cloud whose principal axes are the coordinate axes.
#include <neurosuite/core/dipsplit.hpp>

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace ds = neurosuite::dipsplit;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++fails; }
}

// n points in d dimensions, axis j scaled by sd[j]; with sep > 0, every
// other point is shifted by sep along axis 0.
static std::vector<float> cloud(int n, int d, const std::vector<double>& sd, double sep,
                                unsigned seed) {
    std::mt19937 g(seed);
    std::normal_distribution<double> N(0.0, 1.0);
    std::vector<float> X(static_cast<size_t>(n) * d);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < d; ++j)
            X[static_cast<size_t>(i) * d + j] =
                static_cast<float>(N(g) * sd[j] + ((j == 0 && (i % 2)) ? sep : 0.0));
    return X;
}

int main() {
    const int n = 4000, d = 4;
    const std::vector<double> sd = {1.0, 1.0, 1.0, 1.0};

    // ── valley_test ─────────────────────────────────────────────────────────
    {
        // One mode with no stray points.  (A
        // random Gaussian sample is not used here: an isolated tail point makes
        // its own small KDE peak, and valley_test scores the valley next to it
        // as deep -- see CORE_API.md §5, C22.)
        std::vector<double> a(n), b(n);
        for (int i = 0; i < n; ++i) {                  // Gaussian quantiles, 0.6% .. 99.4% (±2.5 sd)
            const double p = 0.006 + 0.988 * (i + 0.5) / n;
            double lo = -4.0, hi = 4.0;
            for (int it = 0; it < 60; ++it) {
                const double mid = 0.5 * (lo + hi);
                (0.5 * (1.0 + std::erf(mid / std::sqrt(2.0))) < p ? lo : hi) = mid;
            }
            a[i] = 0.5 * (lo + hi);
        }
        const auto two = cloud(n, d, sd, 8.0, 2);
        for (int i = 0; i < n; ++i) b[i] = two[static_cast<size_t>(i) * d];
        const auto u = ds::valley_test(a.data(), n, 0.3);
        const auto m = ds::valley_test(b.data(), n, 0.3);
        check(!u.is_bimodal && u.depth < 0.3, "one mode: no valley (depth " + std::to_string(u.depth) + ")");
        check(m.is_bimodal && m.depth > 0.9, "two Gaussians 8 sd apart: a deep valley (depth " + std::to_string(m.depth) + ")");
        check(m.valley_loc > 2.0 && m.valley_loc < 6.0, "the valley lies between the modes (" + std::to_string(m.valley_loc) + ")");
        check(m.left_peak < 1.5 && m.right_peak > 6.5, "the peaks are near 0 and 8");
        const auto none = ds::valley_test(a.data(), 2, 0.3);
        check(!none.is_bimodal, "two samples are never bimodal");
    }

    // ── top_pcs_with_eigenvalues / top_pcs_power_iteration ─────────────────
    {
        const auto X = cloud(n, d, {5.0, 3.0, 1.0, 0.5}, 0.0, 3);
        std::vector<double> pcs(3 * d), eig(3), pcs2(3 * d);
        ds::top_pcs_with_eigenvalues(X.data(), n, d, 3, pcs.data(), eig.data());
        ds::top_pcs_power_iteration(X.data(), n, d, 3, pcs2.data());
        for (int k = 0; k < 3; ++k) {
            check(std::fabs(std::fabs(pcs[k * d + k]) - 1.0) < 0.02,
                  "PC " + std::to_string(k) + " is coordinate axis " + std::to_string(k));
            double norm = 0;
            for (int j = 0; j < d; ++j) norm += pcs[k * d + j] * pcs[k * d + j];
            check(std::fabs(norm - 1.0) < 1e-9, "PC " + std::to_string(k) + " has unit length");
        }
        check(std::fabs(eig[0] - 25.0) < 2.0 && std::fabs(eig[1] - 9.0) < 1.0 && std::fabs(eig[2] - 1.0) < 0.15,
              "eigenvalues are the variances 25, 9, 1 (got " + std::to_string(eig[0]) + ", "
                  + std::to_string(eig[1]) + ", " + std::to_string(eig[2]) + ")");
        check(pcs == pcs2, "top_pcs_power_iteration gives the same PCs");
    }

    // ── kmeans2_refine and bic_two_vs_one ───────────────────────────────────
    {
        const auto two = cloud(n, d, sd, 8.0, 4);
        std::vector<double> c0(d, 0.0), c1(d, 0.0);
        c0[0] = 2.0; c1[0] = 5.0;                      // deliberately off-centre seeds
        std::vector<int> lab(n, -7);                   // garbage in: overwritten on pass 0
        const int it = ds::kmeans2_refine(two.data(), n, d, c0.data(), c1.data(), lab.data());
        int right = 0;
        for (int i = 0; i < n; ++i) right += (lab[i] == (i % 2));
        check(right > n - 10, "k-means separates the two Gaussians (" + std::to_string(right) + "/" + std::to_string(n) + ")");
        check(it >= 1 && it < 20, "k-means converges before max_iters (" + std::to_string(it) + ")");
        const auto bic = ds::bic_two_vs_one(two.data(), n, d, lab.data());
        check(bic.bic_k2 < bic.bic_k1, "BIC prefers two clusters for two Gaussians");

        const auto one = cloud(n, d, sd, 0.0, 5);
        std::vector<int> half(n);
        for (int i = 0; i < n; ++i) half[i] = i % 2;   // an arbitrary split of one Gaussian
        const auto bic1 = ds::bic_two_vs_one(one.data(), n, d, half.data());
        check(bic1.bic_k1 < bic1.bic_k2, "BIC prefers one cluster for one Gaussian split at random");
    }

    if (fails) { std::printf("%d check(s) failed\n", fails); return 1; }
    std::printf("dipsplit: all checks passed\n");
    return 0;
}
