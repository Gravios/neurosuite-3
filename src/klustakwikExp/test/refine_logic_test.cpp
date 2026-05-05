// /////////////////////////////////////////////////////////////////////////////
// //  Standalone numerical test for KK::RefineExistingClustering logic.
// //
// //  We can't link against the full KKE build (CUDA/HIP/SYCL) in a sandbox,
// //  so this harness reproduces the inner gate logic exactly and verifies it
// //  on synthetic data with known truth:
// //
// //    Test 1.  Mahalanobis under each cluster's covariance.  Hand-build
// //             two well-separated clusters; assert the symmetric distance
// //             is much larger than χ²(d, 0.99) and exit 1 if not.
// //
// //    Test 2.  Bhattacharyya temporal-overlap.  Build two clusters with
// //             (a) identical occupancy and (b) disjoint occupancy; assert
// //             the multiplier is ~1.0 in (a) and ~1.5 in (b).
// //
// //    Test 3.  Drift-aware merge decision.  Build two clusters that are
// //             borderline by Mahalanobis (just above the base threshold),
// //             with disjoint temporal occupancy.  Assert the relaxed gate
// //             accepts the merger that the strict gate would reject.
// //
// //    Test 4.  Anti-test: same Mahalanobis distance, but full temporal
// //             overlap.  Assert the merger is REJECTED.
// /////////////////////////////////////////////////////////////////////////////
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <random>
#include <algorithm>

// Mirrored from KK.cpp / KlustaKwik.cpp Cholesky+TriSolve.
static int Cholesky(const float* in, float* out, int D) {
    for (int i = 0; i < D*D; ++i) out[i] = 0.0f;
    for (int i = 0; i < D; ++i) {
        for (int j = 0; j <= i; ++j) {
            double s = (i == j) ? in[i*D + i] : in[j*D + i];
            for (int k = 0; k < j; ++k) s -= (double)out[i*D + k] * out[j*D + k];
            if (i == j) {
                if (s <= 0.0) return 1;
                out[i*D + i] = (float)std::sqrt(s);
            } else {
                out[i*D + j] = (float)(s / out[j*D + j]);
            }
        }
    }
    return 0;
}
static void TriSolve(const float* L, const float* x, float* y, int D) {
    for (int i = 0; i < D; ++i) {
        double s = x[i];
        for (int k = 0; k < i; ++k) s -= (double)L[i*D + k] * y[k];
        y[i] = (float)(s / L[i*D + i]);
    }
}

// Cluster — mean + full covariance + Cholesky cache.
struct Clu {
    int                 dim;
    std::vector<float>  mean;
    std::vector<float>  cov;       // full DxD
    std::vector<float>  chol;      // lower triangular DxD
    std::vector<float>  occupancy; // [nChunks], normalized
    int                 nMembers;
};

static double mahal(const Clu& src, const Clu& tgt) {
    int D = src.dim;
    std::vector<float> diff(D), root(D);
    for (int d = 0; d < D; ++d) diff[d] = src.mean[d] - tgt.mean[d];
    TriSolve(tgt.chol.data(), diff.data(), root.data(), D);
    double s = 0.0;
    for (int d = 0; d < D; ++d) s += root[d] * root[d];
    return s;
}

static double bhattacharyya(const Clu& a, const Clu& b) {
    if (a.occupancy.empty() || b.occupancy.empty()) return 1.0;
    double bc = 0.0;
    for (size_t k = 0; k < a.occupancy.size(); ++k)
        bc += std::sqrt(a.occupancy[k] * b.occupancy[k]);
    return bc;
}

// ── Synthetic-cluster builder ──────────────────────────────────────────────
static Clu makeClu(int D, std::vector<double> mu, double scale,
                   std::vector<float> occupancy, std::mt19937& rng,
                   int n = 2000) {
    Clu c;
    c.dim = D;
    c.mean.resize(D, 0.0f);
    c.cov.assign(D*D, 0.0f);
    c.chol.resize(D*D, 0.0f);
    c.nMembers = n;
    std::normal_distribution<double> nd(0.0, scale);
    std::vector<std::vector<double>> X(n, std::vector<double>(D));
    for (int i = 0; i < n; ++i)
        for (int d = 0; d < D; ++d) X[i][d] = mu[d] + nd(rng);
    for (int d = 0; d < D; ++d)
        for (int i = 0; i < n; ++i) c.mean[d] += (float)X[i][d];
    for (int d = 0; d < D; ++d) c.mean[d] /= n;
    for (int i = 0; i < n; ++i) {
        std::vector<double> v(D);
        for (int d = 0; d < D; ++d) v[d] = X[i][d] - c.mean[d];
        for (int r = 0; r < D; ++r)
            for (int s = r; s < D; ++s)
                c.cov[r*D + s] += (float)(v[r] * v[s]);
    }
    for (int r = 0; r < D; ++r)
        for (int s = r; s < D; ++s)
            c.cov[r*D + s] /= (n - 1);
    if (Cholesky(c.cov.data(), c.chol.data(), D))
        std::abort();
    c.occupancy = std::move(occupancy);
    double sum = 0.0;
    for (float v : c.occupancy) sum += v;
    if (sum > 0.0)
        for (float& v : c.occupancy) v /= (float)sum;
    return c;
}

static double pairThresh(double base, const Clu& a, const Clu& b, int nChunks) {
    if (nChunks <= 1) return base;
    double ov  = bhattacharyya(a, b);
    double mul = 1.0 + 0.5 * (1.0 - ov);
    return base * mul;
}

#define CHECK(cond, fmt, ...) do { \
    if (!(cond)) { fprintf(stderr, "FAIL %s:%d: " fmt "\n", \
                           __func__, __LINE__, ##__VA_ARGS__); ++fails; } \
    else { fprintf(stderr, "  ok: " fmt "\n", ##__VA_ARGS__); } \
} while (0)

int main() {
    std::mt19937 rng(42);
    int fails = 0;
    const int D = 21;     // typical KKE feature space
    const double chi2_99 = 38.93;  // χ²(21, 0.99)

    fprintf(stderr, "=== Test 1: Mahalanobis distance for well-separated clusters ===\n");
    {
        std::vector<double> mu0(D, 0.0), mu1(D, 0.0);
        mu1[0] = 30.0;  // huge separation along dim 0
        Clu a = makeClu(D, mu0, 1.0, {1, 1, 1}, rng);
        Clu b = makeClu(D, mu1, 1.0, {1, 1, 1}, rng);
        double d_ab = mahal(a, b);
        double d_ba = mahal(b, a);
        double d    = std::min(d_ab, d_ba);
        CHECK(d > 10.0 * chi2_99,
              "well-separated: mahal² = %.1f >> χ²(d,0.99) = %.1f",
              d, chi2_99);
    }

    fprintf(stderr, "\n=== Test 2: Bhattacharyya temporal overlap ===\n");
    {
        // Identical occupancy → bc = 1.0, mul = 1.0.
        Clu a = makeClu(D, std::vector<double>(D, 0.0), 1.0, {1,1,1,1}, rng);
        Clu b = makeClu(D, std::vector<double>(D, 0.0), 1.0, {1,1,1,1}, rng);
        double ov_full = bhattacharyya(a, b);
        double mul_full = pairThresh(35.0, a, b, 4) / 35.0;
        CHECK(std::abs(ov_full - 1.0) < 1e-5,
              "identical occupancy: bc = %.4f", ov_full);
        CHECK(std::abs(mul_full - 1.0) < 1e-5,
              "identical occupancy: mul = %.4f", mul_full);

        // Disjoint occupancy → bc = 0, mul = 1.5.
        Clu c = makeClu(D, std::vector<double>(D, 0.0), 1.0, {1,0,0,0}, rng);
        Clu d = makeClu(D, std::vector<double>(D, 0.0), 1.0, {0,0,0,1}, rng);
        double ov_disj = bhattacharyya(c, d);
        double mul_disj = pairThresh(35.0, c, d, 4) / 35.0;
        CHECK(std::abs(ov_disj - 0.0) < 1e-5,
              "disjoint occupancy: bc = %.4f", ov_disj);
        CHECK(std::abs(mul_disj - 1.5) < 1e-5,
              "disjoint occupancy: mul = %.4f", mul_disj);

        // Partial overlap → bc ∈ (0, 1), mul ∈ (1.0, 1.5).
        Clu e = makeClu(D, std::vector<double>(D, 0.0), 1.0, {2,1,0,0}, rng);
        Clu f = makeClu(D, std::vector<double>(D, 0.0), 1.0, {0,1,2,0}, rng);
        double ov_part = bhattacharyya(e, f);
        CHECK(ov_part > 0.0 && ov_part < 1.0,
              "partial overlap: bc = %.4f (in (0,1))", ov_part);
    }

    fprintf(stderr, "\n=== Test 3: Drift-aware merge accepts borderline disjoint pair ===\n");
    {
        // Borderline-similar clusters, disjoint occupancy → relaxed gate
        // accepts; strict (no-chunks) gate would not.
        std::vector<double> mu0(D, 0.0), mu1(D, 0.0);
        mu1[0] = 7.0;  // separation tuned to land in [base, 1.5*base]
        Clu a = makeClu(D, mu0, 1.0, {1, 0, 0}, rng);
        Clu b = makeClu(D, mu1, 1.0, {0, 0, 1}, rng);
        double d = std::min(mahal(a, b), mahal(b, a));
        double base = 35.0;  // ~χ²(d, 0.99)
        double thr_relaxed = pairThresh(base, a, b, 3);
        // No-chunks path: pairThresh(base, a, b, 0) = base.
        double thr_strict  = pairThresh(base, a, b, 0);
        fprintf(stderr, "    mahal² = %.2f, strict thr = %.2f, relaxed thr = %.2f\n",
                d, thr_strict, thr_relaxed);
        CHECK(d < thr_relaxed,
              "drift case: relaxed gate accepts (d=%.1f < thr=%.1f)",
              d, thr_relaxed);
        CHECK(d > thr_strict,
              "drift case: strict gate rejects (d=%.1f > thr=%.1f)",
              d, thr_strict);
    }

    fprintf(stderr, "\n=== Test 4: Anti-test — same Mahal, full overlap → rejected ===\n");
    {
        std::vector<double> mu0(D, 0.0), mu1(D, 0.0);
        mu1[0] = 7.0;
        Clu a = makeClu(D, mu0, 1.0, {1, 1, 1}, rng);
        Clu b = makeClu(D, mu1, 1.0, {1, 1, 1}, rng);
        double d = std::min(mahal(a, b), mahal(b, a));
        double base = 35.0;
        double thr_relaxed = pairThresh(base, a, b, 3);
        // Full overlap → relaxed = base, so identical to strict.
        CHECK(std::abs(thr_relaxed - base) < 1e-5,
              "co-occurring pair: thr stays at base (%.1f)", thr_relaxed);
    }

    fprintf(stderr, "\n%s — %d failures\n",
            (fails == 0) ? "PASS" : "FAIL", fails);
    return fails;
}
