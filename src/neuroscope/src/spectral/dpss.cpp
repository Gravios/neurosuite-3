#include "dpss.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace neuroscope {
namespace spectral {

namespace {

// Number of eigenvalues of the symmetric tridiagonal matrix (diagonal d,
// off-diagonal e of length N-1) that are strictly less than x. Standard
// Sturm sequence with a small pivot guard.
int sturmCount(const std::vector<double>& d, const std::vector<double>& e, double x)
{
    const int N = static_cast<int>(d.size());
    int count = 0;
    double q = d[0] - x;
    if (q < 0.0) ++count;
    for (int i = 1; i < N; ++i) {
        const double eprev = e[i - 1];
        if (q == 0.0) q = std::numeric_limits<double>::min();
        q = (d[i] - x) - (eprev * eprev) / q;
        if (q < 0.0) ++count;
    }
    return count;
}

// The m-th smallest eigenvalue (0-based) of the tridiagonal, by bisection
// on the Sturm count, within [lo, hi].
double eigenvalueByIndex(const std::vector<double>& d, const std::vector<double>& e,
                         int m, double lo, double hi)
{
    for (int iter = 0; iter < 200; ++iter) {
        const double mid = 0.5 * (lo + hi);
        if (mid == lo || mid == hi) break;       // reached fp resolution
        if (sturmCount(d, e, mid) <= m) lo = mid; // mid below the (m+1)-th eigenvalue
        else hi = mid;
    }
    return 0.5 * (lo + hi);
}

// Solve (T - mu I) y = b for a symmetric tridiagonal T (diagonal d,
// off-diagonal e) via the Thomas algorithm, with mu slightly perturbed off
// the eigenvalue so the system is non-singular. Result written to y.
void tridiagonalSolveShifted(const std::vector<double>& d, const std::vector<double>& e,
                             double mu, const std::vector<double>& b, std::vector<double>& y)
{
    const int N = static_cast<int>(d.size());
    std::vector<double> cp(N), dp(N); // modified super-diagonal and rhs
    double denom = (d[0] - mu);
    if (std::abs(denom) < 1e-300) denom = 1e-300;
    cp[0] = (N > 1 ? e[0] : 0.0) / denom;
    dp[0] = b[0] / denom;
    for (int i = 1; i < N; ++i) {
        denom = (d[i] - mu) - e[i - 1] * cp[i - 1];
        if (std::abs(denom) < 1e-300) denom = 1e-300;
        cp[i] = (i < N - 1 ? e[i] : 0.0) / denom;
        dp[i] = (b[i] - e[i - 1] * dp[i - 1]) / denom;
    }
    y[N - 1] = dp[N - 1];
    for (int i = N - 2; i >= 0; --i) y[i] = dp[i] - cp[i] * y[i + 1];
}

void normaliseUnitEnergy(std::vector<double>& v)
{
    double s = 0.0;
    for (double x : v) s += x * x;
    s = std::sqrt(s);
    if (s > 0.0) for (double& x : v) x /= s;
}

// Concentration ratio lambda = v^T A v, A the sinc kernel
// A[m,n] = sin(2 pi W (m-n)) / (pi (m-n)), A[m,m] = 2W. Exploits symmetry
// and the Toeplitz structure (A depends only on |m-n|).
double concentration(const std::vector<double>& v, double W)
{
    const int N = static_cast<int>(v.size());
    // r[k] = A value for lag k.
    std::vector<double> r(N);
    r[0] = 2.0 * W;
    for (int k = 1; k < N; ++k)
        r[k] = std::sin(2.0 * M_PI * W * k) / (M_PI * k);

    double acc = 0.0;
    for (int m = 0; m < N; ++m) {
        double row = r[0] * v[m];
        for (int n = m + 1; n < N; ++n) row += r[n - m] * v[n]; // upper
        for (int n = 0; n < m; ++n)     row += r[m - n] * v[n]; // lower
        acc += v[m] * row;
    }
    return acc;
}

} // namespace

DpssTapers computeDpss(int N, double NW, int K)
{
    DpssTapers out;
    if (N < 2 || K < 1 || NW <= 0.0 || NW >= N / 2.0) return out;
    // Concentration falls off sharply past 2*NW tapers; clamp.
    const int kmax = std::max(1, static_cast<int>(std::floor(2.0 * NW)));
    K = std::min(K, std::min(N, kmax));

    const double W = NW / static_cast<double>(N);
    const double cw = std::cos(2.0 * M_PI * W);

    // Symmetric tridiagonal whose eigenvectors are the DPSS.
    std::vector<double> d(N), e(N > 1 ? N - 1 : 0);
    for (int i = 0; i < N; ++i) {
        const double t = 0.5 * (N - 1 - 2 * i);
        d[i] = t * t * cw;
    }
    for (int i = 0; i < N - 1; ++i)
        e[i] = 0.5 * (i + 1) * (N - 1 - i);

    // Gershgorin bounds for the eigenvalue search.
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < N; ++i) {
        const double rad = (i > 0 ? std::abs(e[i - 1]) : 0.0)
                         + (i < N - 1 ? std::abs(e[i]) : 0.0);
        lo = std::min(lo, d[i] - rad);
        hi = std::max(hi, d[i] + rad);
    }
    const double span = (hi - lo);
    lo -= 1e-6 * span + 1.0;
    hi += 1e-6 * span + 1.0;

    out.N = N; out.K = K; out.NW = NW;
    out.taper.assign(K, std::vector<double>(N));
    out.lambda.assign(K, 0.0);

    std::vector<double> b(N), y(N);
    for (int k = 0; k < K; ++k) {
        // Tapers are ordered by DECREASING tridiagonal eigenvalue, i.e. the
        // k-th taper is the (N-1-k)-th smallest eigenvalue.
        const int idx = N - 1 - k;
        const double mu = eigenvalueByIndex(d, e, idx, lo, hi);

        // Inverse iteration. Perturb the shift to avoid exact singularity.
        const double shift = mu + 1e-9 * (std::abs(mu) + 1.0);
        for (int i = 0; i < N; ++i) b[i] = 1.0;          // start vector
        for (int iter = 0; iter < 5; ++iter) {
            tridiagonalSolveShifted(d, e, shift, b, y);
            normaliseUnitEnergy(y);
            b = y;
        }

        // Polarity convention.
        double sum = 0.0, slope = 0.0;
        const double mid = 0.5 * (N - 1);
        for (int i = 0; i < N; ++i) { sum += y[i]; slope += (i - mid) * y[i]; }
        const bool flip = (k % 2 == 0) ? (sum < 0.0) : (slope < 0.0);
        if (flip) for (double& x : y) x = -x;

        out.taper[k] = y;
        out.lambda[k] = concentration(y, W);
    }
    return out;
}

} // namespace spectral
} // namespace neuroscope
