#include "oblique_embed.h"

#include <cmath>
#include <algorithm>

namespace {

// Classic cyclic Jacobi eigensolver for a small symmetric n*n matrix A
// (row-major, overwritten).  Returns eigenvalues in w (unsorted) and
// eigenvectors as the COLUMNS of V (row-major).  n is tiny here (the number of
// selected clusters), so O(n^3) per sweep with a handful of sweeps is nothing.
void jacobiEigenSym(std::vector<double>& A, int n,
                    std::vector<double>& w, std::vector<double>& V)
{
    V.assign(static_cast<size_t>(n) * n, 0.0);
    for (int i = 0; i < n; ++i) V[static_cast<size_t>(i) * n + i] = 1.0;
    auto at = [&](std::vector<double>& M, int i, int j) -> double& {
        return M[static_cast<size_t>(i) * n + j];
    };
    for (int sweep = 0; sweep < 100; ++sweep) {
        double off = 0.0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) off += at(A, p, q) * at(A, p, q);
        if (off < 1e-30) break;
        for (int p = 0; p < n; ++p) {
            for (int q = p + 1; q < n; ++q) {
                const double apq = at(A, p, q);
                if (std::fabs(apq) < 1e-300) continue;
                const double app = at(A, p, p), aqq = at(A, q, q);
                const double phi = 0.5 * std::atan2(2.0 * apq, aqq - app);
                const double c = std::cos(phi), s = std::sin(phi);
                for (int k = 0; k < n; ++k) {
                    const double akp = at(A, k, p), akq = at(A, k, q);
                    at(A, k, p) = c * akp - s * akq;
                    at(A, k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k) {
                    const double apk = at(A, p, k), aqk = at(A, q, k);
                    at(A, p, k) = c * apk - s * aqk;
                    at(A, q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k) {
                    const double vkp = at(V, k, p), vkq = at(V, k, q);
                    at(V, k, p) = c * vkp - s * vkq;
                    at(V, k, q) = s * vkp + c * vkq;
                }
            }
        }
    }
    w.resize(n);
    for (int i = 0; i < n; ++i) w[i] = A[static_cast<size_t>(i) * n + i];
}

} // namespace

bool obliqueProject(const std::vector<double>& data, int N, int D,
                    const std::vector<int>& rowCluster,
                    const std::vector<int>& basisClusters,
                    const std::vector<double>& featMean,
                    const std::vector<double>& featInvStd,
                    std::vector<double>& outXY,
                    double& condNumber,
                    std::string* err)
{
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    const int K = static_cast<int>(basisClusters.size());
    if (N < 1 || D < 1)                     return fail("empty input");
    if (K < 2)                              return fail("need at least two clusters for an oblique basis");
    if (static_cast<size_t>(N) * D != data.size())   return fail("data size != N*D");
    if (static_cast<int>(rowCluster.size()) != N)     return fail("rowCluster size != N");
    if (static_cast<int>(featMean.size()) != D ||
        static_cast<int>(featInvStd.size()) != D)     return fail("featMean/featInvStd size != D");

    // Standardise with the caller's GLOBAL mean/invStd (see the header: centering
    // over only the basis clusters would collapse the Gram).  A zero invStd marks
    // a dead dimension and drops it from the geometry.
    auto Z = [&](int i, int d) -> double {
        return (data[static_cast<size_t>(i) * D + d] - featMean[d]) * featInvStd[d];
    };

    // ---- basis templates B: z-scored per-cluster means (D x K) --------------
    std::vector<double> B(static_cast<size_t>(D) * K, 0.0);  // column-major by k
    std::vector<long>   cnt(K, 0);
    for (int i = 0; i < N; ++i) {
        const int cid = rowCluster[i];
        for (int k = 0; k < K; ++k)
            if (basisClusters[k] == cid) {
                ++cnt[k];
                for (int d = 0; d < D; ++d)
                    B[static_cast<size_t>(k) * D + d] += Z(i, d);
                break;
            }
    }
    for (int k = 0; k < K; ++k) {
        if (cnt[k] == 0) return fail("a basis cluster has no points");
        for (int d = 0; d < D; ++d) B[static_cast<size_t>(k) * D + d] /= cnt[k];
    }

    // ---- Gram G = B^T B (K x K), its eigendecomposition, condition, inverse -
    std::vector<double> G(static_cast<size_t>(K) * K, 0.0);
    for (int a = 0; a < K; ++a)
        for (int b = a; b < K; ++b) {
            double s = 0.0;
            for (int d = 0; d < D; ++d)
                s += B[static_cast<size_t>(a) * D + d] * B[static_cast<size_t>(b) * D + d];
            G[static_cast<size_t>(a) * K + b] = G[static_cast<size_t>(b) * K + a] = s;
        }
    std::vector<double> w, V, Gwork = G;
    jacobiEigenSym(Gwork, K, w, V);
    double lmax = 0.0, lmin = 0.0;
    for (int k = 0; k < K; ++k) { lmax = std::max(lmax, w[k]); }
    lmin = lmax;
    for (int k = 0; k < K; ++k) lmin = std::min(lmin, w[k]);
    if (lmax <= 0.0) return fail("degenerate basis (zero templates)");
    condNumber = lmax / std::max(lmin, lmax * 1e-12);

    // Ginv = V diag(1/lambda) V^T, floor tiny eigenvalues to a ridge of the
    // spectrum so a near-singular (collinear) basis still yields a usable — if
    // noise-amplified — projection rather than exploding.
    const double floorEig = lmax * 1e-6;
    std::vector<double> Ginv(static_cast<size_t>(K) * K, 0.0);
    for (int a = 0; a < K; ++a)
        for (int b = 0; b < K; ++b) {
            double s = 0.0;
            for (int k = 0; k < K; ++k)
                s += V[static_cast<size_t>(a) * K + k] *
                     (1.0 / std::max(w[k], floorEig)) *
                     V[static_cast<size_t>(b) * K + k];
            Ginv[static_cast<size_t>(a) * K + b] = s;
        }

    // ---- per-point oblique coordinates  a = Ginv (B^T z)  (K-vector) --------
    std::vector<double> coords(static_cast<size_t>(N) * K);
    std::vector<double> btz(K);
    for (int i = 0; i < N; ++i) {
        for (int k = 0; k < K; ++k) {
            double s = 0.0;
            for (int d = 0; d < D; ++d)
                s += B[static_cast<size_t>(k) * D + d] * Z(i, d);
            btz[k] = s;
        }
        for (int a = 0; a < K; ++a) {
            double s = 0.0;
            for (int k = 0; k < K; ++k) s += Ginv[static_cast<size_t>(a) * K + k] * btz[k];
            coords[static_cast<size_t>(i) * K + a] = s;
        }
    }

    // ---- 2-D output --------------------------------------------------------
    outXY.assign(static_cast<size_t>(N) * 2, 0.0);
    if (K == 2) {
        for (int i = 0; i < N; ++i) {
            outXY[static_cast<size_t>(i) * 2]     = coords[static_cast<size_t>(i) * 2];
            outXY[static_cast<size_t>(i) * 2 + 1] = coords[static_cast<size_t>(i) * 2 + 1];
        }
    } else {
        // PCA of the K-dimensional coordinate cloud: center, then project onto
        // the two leading covariance eigenvectors — the max-variance 2-D view.
        std::vector<double> cmean(K, 0.0);
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < K; ++k) cmean[k] += coords[static_cast<size_t>(i) * K + k];
        for (int k = 0; k < K; ++k) cmean[k] /= N;
        std::vector<double> C(static_cast<size_t>(K) * K, 0.0);
        for (int i = 0; i < N; ++i)
            for (int a = 0; a < K; ++a) {
                const double va = coords[static_cast<size_t>(i) * K + a] - cmean[a];
                for (int b = a; b < K; ++b) {
                    const double vb = coords[static_cast<size_t>(i) * K + b] - cmean[b];
                    C[static_cast<size_t>(a) * K + b] += va * vb;
                }
            }
        for (int a = 0; a < K; ++a)
            for (int b = a; b < K; ++b)
                C[static_cast<size_t>(b) * K + a] = (C[static_cast<size_t>(a) * K + b] /= std::max(1, N - 1));
        std::vector<double> cw, cV;
        jacobiEigenSym(C, K, cw, cV);
        int i1 = 0, i2 = 1;                       // indices of the two largest
        for (int k = 1; k < K; ++k) if (cw[k] > cw[i1]) i1 = k;
        for (int k = 0; k < K; ++k) if (k != i1 && (i2 == i1 || cw[k] > cw[i2])) i2 = k;
        if (i2 == i1) i2 = (i1 + 1) % K;
        for (int i = 0; i < N; ++i) {
            double x = 0.0, y = 0.0;
            for (int k = 0; k < K; ++k) {
                const double c = coords[static_cast<size_t>(i) * K + k] - cmean[k];
                x += c * cV[static_cast<size_t>(k) * K + i1];
                y += c * cV[static_cast<size_t>(k) * K + i2];
            }
            outXY[static_cast<size_t>(i) * 2]     = x;
            outXY[static_cast<size_t>(i) * 2 + 1] = y;
        }
    }
    return true;
}
