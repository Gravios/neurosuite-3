// pca_fit.hpp — fit a per-channel PCAE basis (pca_projection.hpp) from waveforms.
//
// What process_pca does for a group, in a header: for every channel, the mean
// and the leading eigenvectors of the covariance of the window
// [recShift, recShift + data2use) over the spikes given, by cyclic Jacobi
// (data2use is at most a few hundred -- a super-sampled window -- so O(D^3) per
// sweep is nothing).  The basis projects with pcaProjectScores / Klusters'
// projectSpikeOntoPca exactly as one written by process_pca would, so a space
// built in Klusters (its super-sample mode) is an ordinary space to everything.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEUROSUITE_PCA_FIT_HPP
#define NEUROSUITE_PCA_FIT_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numeric>
#include <vector>

#include "pca_projection.hpp"

namespace neurosuite::core {

// Eigen-decomposition of a symmetric D x D matrix (row-major, overwritten) by
// cyclic Jacobi; evec column-major [k * D + u] = component k.  Eigenvalues come
// back sorted descending with their vectors.
inline void jacobiEigen(std::vector<double>& A, int D, std::vector<double>& eval,
                        std::vector<double>& evec, int maxSweeps = 60)
{
    evec.assign(static_cast<std::size_t>(D) * D, 0.0);
    for (int i = 0; i < D; ++i) evec[static_cast<std::size_t>(i) * D + i] = 1.0;   // V = I (row-major)
    auto a = [&](int i, int j) -> double& { return A[static_cast<std::size_t>(i) * D + j]; };
    auto v = [&](int i, int j) -> double& { return evec[static_cast<std::size_t>(i) * D + j]; };
    for (int sweep = 0; sweep < maxSweeps; ++sweep) {
        double off = 0.0;
        for (int i = 0; i < D; ++i)
            for (int j = i + 1; j < D; ++j) off += a(i, j) * a(i, j);
        if (off < 1e-22) break;
        for (int p = 0; p < D; ++p) {
            for (int q = p + 1; q < D; ++q) {
                const double apq = a(p, q);
                if (std::fabs(apq) < 1e-300) continue;
                const double theta = (a(q, q) - a(p, p)) / (2.0 * apq);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < D; ++k) {               // rotate rows/cols p, q
                    const double akp = a(k, p), akq = a(k, q);
                    a(k, p) = c * akp - s * akq;
                    a(k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < D; ++k) {
                    const double apk = a(p, k), aqk = a(q, k);
                    a(p, k) = c * apk - s * aqk;
                    a(q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < D; ++k) {               // V = V * J
                    const double vkp = v(k, p), vkq = v(k, q);
                    v(k, p) = c * vkp - s * vkq;
                    v(k, q) = s * vkp + c * vkq;
                }
            }
        }
    }
    eval.resize(static_cast<std::size_t>(D));
    for (int i = 0; i < D; ++i) eval[static_cast<std::size_t>(i)] = a(i, i);
    // sort descending; evec row-major V[u][k] -> column-major out [k * D + u]
    std::vector<int> order(static_cast<std::size_t>(D));
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int x, int y) { return eval[static_cast<std::size_t>(x)] > eval[static_cast<std::size_t>(y)]; });
    std::vector<double> ev2(static_cast<std::size_t>(D)), vec2(static_cast<std::size_t>(D) * D);
    for (int k = 0; k < D; ++k) {
        ev2[static_cast<std::size_t>(k)] = eval[static_cast<std::size_t>(order[static_cast<std::size_t>(k)])];
        for (int u = 0; u < D; ++u)
            vec2[static_cast<std::size_t>(k) * D + u] = v(u, order[static_cast<std::size_t>(k)]);
    }
    eval.swap(ev2);
    evec.swap(vec2);
}

// Fit a basis.  `sample(i, ch, s)` returns sample s (0 <= s < data2use, already
// offset by recShift) of channel ch of spike i, for i in [0, nSpikes).  PCAE
// readers take each eigenvector's sign as stored, so it is fixed here only for
// determinism (the largest-magnitude entry positive).
inline bool fitPca(int nSpikes, int nCh, int data2use, int recShift, int nComp,
                   const std::function<double(int, int, int)>& sample, PcaBasis& out,
                   Method method = Method::Standard, int nInputChannels = 0)
{
    if (nSpikes < 2 || nCh < 1 || data2use < 1 || nComp < 1) return false;
    const int D = data2use;
    nComp = std::min(nComp, D);
    out.nCh = nCh; out.data2use = D; out.nComp = nComp; out.recShift = recShift;
    out.centered = true; out.method = method; out.nInputChannels = nInputChannels;
    out.means.assign(static_cast<std::size_t>(nCh), std::vector<double>(static_cast<std::size_t>(D), 0.0));
    out.evec.assign(static_cast<std::size_t>(nCh), std::vector<double>(static_cast<std::size_t>(D) * nComp, 0.0));
    std::vector<double> x(static_cast<std::size_t>(D)), C(static_cast<std::size_t>(D) * D), eval, evec;
    for (int ch = 0; ch < nCh; ++ch) {
        std::vector<double>& mean = out.means[static_cast<std::size_t>(ch)];
        for (int i = 0; i < nSpikes; ++i)
            for (int s = 0; s < D; ++s) mean[static_cast<std::size_t>(s)] += sample(i, ch, s);
        for (double& m : mean) m /= nSpikes;
        std::fill(C.begin(), C.end(), 0.0);
        for (int i = 0; i < nSpikes; ++i) {
            for (int s = 0; s < D; ++s) x[static_cast<std::size_t>(s)] = sample(i, ch, s) - mean[static_cast<std::size_t>(s)];
            for (int u = 0; u < D; ++u) {
                const double xu = x[static_cast<std::size_t>(u)];
                double* row = C.data() + static_cast<std::size_t>(u) * D;
                for (int w = u; w < D; ++w) row[w] += xu * x[static_cast<std::size_t>(w)];
            }
        }
        for (int u = 0; u < D; ++u)
            for (int w = 0; w < u; ++w) C[static_cast<std::size_t>(u) * D + w] = C[static_cast<std::size_t>(w) * D + u];
        for (double& c : C) c /= (nSpikes - 1);
        jacobiEigen(C, D, eval, evec);
        std::vector<double>& ev = out.evec[static_cast<std::size_t>(ch)];
        for (int k = 0; k < nComp; ++k) {
            int big = 0;
            for (int u = 1; u < D; ++u)
                if (std::fabs(evec[static_cast<std::size_t>(k) * D + u]) > std::fabs(evec[static_cast<std::size_t>(k) * D + big])) big = u;
            const double sign = evec[static_cast<std::size_t>(k) * D + big] < 0 ? -1.0 : 1.0;
            for (int u = 0; u < D; ++u)
                ev[static_cast<std::size_t>(k) * D + u] = sign * evec[static_cast<std::size_t>(k) * D + u];
        }
    }
    return true;
}

}  // namespace neurosuite::core

#endif  // NEUROSUITE_PCA_FIT_HPP
