// The Grouping Assistant's Metal error matrix (groupingassistant_metal.cpp,
// FP32) against an FP64 reference computed here the way the CPU path does:
// p = exp(-0.5 (mahal + logTerm)) per cluster, row-normalised, a row whose
// probabilities all underflow assigned to cluster 1, then the mean over each
// cluster's spikes.  Gaussian clusters in 3-11 dimensions, with an outlier
// (the cluster-1 rule), an empty cluster, an ignored cluster, and small
// chunks so a cluster's spikes span several.  FP64 requests must be refused
// (the CPU computes them).  SKIPPED (exit 0) without a Metal device.
#include "groupingassistant_gpu.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <random>
#include <string>
#include <vector>

extern "C" int metal_compute_error_matrix_chunked(
    const double*, const double*, const double*, const double*, const int*,
    const int*, const int*, const int*, double*, int, int, int, int, int, std::size_t);

namespace {
struct GaCase {
  int nS, nC, nD, c1 = 0;
  std::vector<double> feat, chol, mean, logT; std::vector<int> ign, row, first, nb;
};
inline GaCase makeCase(int nC, int nD, int perC, unsigned seed, bool withEmpty, bool withIgnore) {
  std::mt19937 rng(seed); std::normal_distribution<double> g(0, 1);
  GaCase c; c.nC = nC; c.nD = nD;
  std::vector<std::vector<double>> pts(nC);
  for (int k = 0; k < nC; ++k) {
    int n = (withEmpty && k == 2) ? 0 : perC + int(rng() % 50);
    std::vector<double> ctr(nD); for (auto& v : ctr) v = g(rng) * 6;
    for (int s = 0; s < n; ++s) for (int d = 0; d < nD; ++d) pts[k].push_back(ctr[d] + g(rng) * (1 + 0.3 * d) + (d ? 0.4 * pts[k][pts[k].size() - 1] : 0));
  }
  pts[1].insert(pts[1].end(), nD, 1e4);   // an outlier in cluster 1 -> noise rule
  c.chol.assign(nC * nD * nD, 0); c.mean.assign(nC * nD, 0); c.logT.assign(nC, 0); c.ign.assign(nC, 0);
  int pos = 0;
  for (int k = 0; k < nC; ++k) {
    int n = pts[k].size() / nD; c.first.push_back(pos); c.nb.push_back(n);
    for (int s = 0; s < n; ++s) { c.row.push_back(pos + s); for (int d = 0; d < nD; ++d) c.feat.push_back(pts[k][s * nD + d]); }
    pos += n;
    if (n < nD + 2 || (withIgnore && k == 3)) { c.ign[k] = 1; continue; }
    std::vector<double> mu(nD, 0), cov(nD * nD, 0);
    for (int s = 0; s < n; ++s) for (int d = 0; d < nD; ++d) mu[d] += pts[k][s * nD + d] / n;
    for (int s = 0; s < n; ++s) for (int a = 0; a < nD; ++a) for (int b = 0; b < nD; ++b) cov[a * nD + b] += (pts[k][s*nD+a]-mu[a])*(pts[k][s*nD+b]-mu[b])/(n-1);
    double* L = &c.chol[k * nD * nD]; double logdet = 0;
    for (int i = 0; i < nD; ++i) for (int j = 0; j <= i; ++j) { double s = cov[i*nD+j]; for (int q = 0; q < j; ++q) s -= L[i+q*nD]*L[j+q*nD]; if (i == j) { L[i+i*nD] = std::sqrt(s); logdet += std::log(L[i+i*nD]); } else L[i+j*nD] = s / L[j+j*nD]; }
    for (int d = 0; d < nD; ++d) c.mean[k*nD+d] = mu[d];
    c.logT[k] = logdet - std::log(double(n)) + std::log(2 * std::numbers::pi) * nD / 2.0;
  }
  c.nS = pos; c.c1 = 1;
  return c;
}
// FP64 reference as the CPU path: p = exp(-0.5(mahal + logT)), row-normalised, sum==0 -> cluster 1.
inline std::vector<double> refErr(const GaCase& c) {
  std::vector<double> err(c.nC * c.nC, 0), pr(c.nC);
  for (int i = 0; i < c.nC; ++i) for (int q = c.first[i]; q < c.first[i] + c.nb[i]; ++q) {
    const double* x = &c.feat[c.row[q] * c.nD]; double sum = 0;
    for (int j = 0; j < c.nC; ++j) {
      if (c.ign[j]) { pr[j] = 0; continue; }
      const double* L = &c.chol[j*c.nD*c.nD]; std::vector<double> y(c.nD); double sq = 0;
      for (int a = 0; a < c.nD; ++a) { double s = x[a] - c.mean[j*c.nD+a]; for (int b = 0; b < a; ++b) s -= L[a+b*c.nD]*y[b]; y[a] = s / L[a+a*c.nD]; sq += y[a]*y[a]; }
      pr[j] = std::exp(-0.5 * (sq + c.logT[j])); sum += pr[j];
    }
    if (sum == 0) { std::fill(pr.begin(), pr.end(), 0.0); pr[c.c1] = 1; sum = 1; }
    if (!c.ign[i]) for (int j = 0; j < c.nC; ++j) if (!c.ign[j]) err[i*c.nC+j] += pr[j] / sum / c.nb[i];
  }
  return err;
}

int failures = 0;
void check(bool ok, const std::string& what)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++failures;
}
} // namespace

int main()
{
    if (!neurosuite::gpu::MetalContext::instance()) {
        std::printf("SKIPPED: no Metal device on this machine\n");
        return 0;
    }
    for (int t = 0; t < 3; ++t) {
        const GaCase c = makeCase(6 + 3 * t, 3 + 4 * t, 300, 11 + t, t == 1, t == 2);
        const std::vector<double> want = refErr(c);
        for (std::size_t chunkBytes : {std::size_t(256) << 20, std::size_t(97) * c.nC * sizeof(float)}) {
            std::vector<double> got(static_cast<std::size_t>(c.nC) * c.nC, -1.0);
            const int rc = metal_compute_error_matrix_chunked(
                c.feat.data(), c.chol.data(), c.mean.data(), c.logT.data(), c.ign.data(),
                c.row.data(), c.first.data(), c.nb.data(), got.data(),
                c.nS, c.nC, c.nD, c.c1, 1, chunkBytes);
            double worst = rc == 0 ? 0.0 : 1.0;
            for (std::size_t k = 0; k < got.size(); ++k) worst = std::max(worst, std::fabs(got[k] - want[k]));
            char name[200];
            std::snprintf(name, sizeof name,
                          "%d clusters, %d dims, %d spikes, %s: max |Metal FP32 - FP64| = %.2e (<= 2e-3)",
                          c.nC, c.nD, c.nS, chunkBytes > (1u << 20) ? "one chunk" : "97-spike chunks", worst);
            check(rc == 0 && worst <= 2e-3, name);
        }
        std::vector<double> unused(static_cast<std::size_t>(c.nC) * c.nC);
        if (t == 0)
            check(metal_compute_error_matrix(c.feat.data(), c.chol.data(), c.mean.data(), c.logT.data(),
                                             c.ign.data(), c.row.data(), c.first.data(), c.nb.data(),
                                             unused.data(), c.nS, c.nC, c.nD, c.c1, 0) != 0,
                  "an FP64 request is refused (the CPU computes it)");
    }
    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
