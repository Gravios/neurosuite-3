// KiloKlustaKwik's Metal CEM steps (KK_metal.cpp) against references
// computed here in double, as KK.cpp's CPU path defines them:
//   M-step  Mean = class mean, Cov = upper triangle of the (n - 1) covariance;
//           only alive rows written, lower triangle zero; dead rows untouched
//   E-step  LogP[c][p] = 0.5 |L^-1 (x - mean)|^2 + log|L| - log w + D/2 log 2pi,
//           the noise row -log w0, and with !FullStep the DistThresh skip,
//           decided cluster by cluster in alive order as the kernels do
//   C-step  best and second best (first strictly smaller wins), exactly
//   score   sum of LogP at Class plus the penalty
//   deletion loss  per class, sum of LogP[Class2] - LogP[Class], exactly
// on Gaussian clusters with 1, 12 and 64 dimensions, a cluster larger than
// one M-step tile (4096 points), an empty alive cluster, a one-point one,
// two identical clusters (every point an exact C-step tie), and steps on
// fewer dimensions than were loaded (Mean rows nDims long, Cov/Chol blocks
// still nDims2 apart), as CEMTwoPhase's spatial phase runs them.
// It also checks that two M-steps give identical bytes (no atomics).
// SKIPPED (exit 0) without a Metal device.
#include "KK_metal.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, double detail = 0)
{
    std::printf("  [%s] %s", ok ? "PASS" : "FAIL", what);
    if (!ok) std::printf(" (%g)", detail);
    std::printf("\n");
    failures += !ok;
}

// KlustaKwik.cpp's Cholesky (upper triangle in, lower triangle out).
bool cholesky(const float *in, float *out, int D)
{
    std::fill(out, out + D * D, 0.0f);
    for (int i = 0; i < D; i++)
        for (int j = i; j < D; j++) {
            float sum = in[i * D + j];
            for (int k = i - 1; k >= 0; k--) sum -= out[i * D + k] * out[j * D + k];
            if (i == j) {
                if (sum <= 0.0f) return false;
                out[i * D + i] = std::sqrt(sum);
            } else {
                out[j * D + i] = sum / out[i * D + i];
            }
        }
    return true;
}

struct Case {
    const char *name;
    int nP, nD, maxC;
    int callDims;               // 0: nD; else the steps run on the first callDims
                                // dimensions, as CEMTwoPhase's spatial phase does
    std::vector<int> alive;     // AliveIndex, alive[0] == 0 (noise)
    std::vector<int> sizes;     // points per alive cluster, noise included (alive order)
    unsigned seed;
};

void runCase(const Case &cs)
{
    std::printf("%s: %d points, %d dims\n", cs.name, cs.nP, cs.nD);
    // L dimensions loaded; the steps use nD <= L, with Mean rows nD long and
    // Cov/Chol blocks still L * L apart (KK's layout).
    const int nP = cs.nP, L = cs.nD, nD = cs.callDims ? cs.callDims : L, nD2 = L * L,
              maxC = cs.maxC;
    const int nAlive = static_cast<int>(cs.alive.size());
    std::mt19937 rng(cs.seed);
    std::normal_distribution<float> g(0.0f, 1.0f);

    // Points, class by class; each cluster a shifted, sheared Gaussian.
    std::vector<float> data(static_cast<size_t>(nP) * L);
    std::vector<int> cls(nP);
    {
        int p = 0;
        for (int cc = 0; cc < nAlive; ++cc) {
            std::vector<float> ctr(L);
            for (float &v : ctr) v = g(rng) * 0.3f + 0.5f;
            for (int s = 0; s < cs.sizes[cc]; ++s, ++p) {
                cls[p] = cs.alive[cc];
                float prev = 0.0f;
                for (int d = 0; d < L; ++d) {
                    prev = ctr[d] + 0.02f * (1 + d % 5) * g(rng) + 0.3f * prev;
                    data[static_cast<size_t>(p) * L + d] = prev;
                }
            }
        }
        // Shuffle so classes interleave in point order.
        std::vector<int> perm(nP);
        for (int i = 0; i < nP; ++i) perm[i] = i;
        std::shuffle(perm.begin(), perm.end(), rng);
        std::vector<float> d2(data.size());
        std::vector<int> c2(nP);
        for (int i = 0; i < nP; ++i) {
            c2[i] = cls[perm[i]];
            std::copy_n(&data[static_cast<size_t>(perm[i]) * L], L, &d2[static_cast<size_t>(i) * L]);
        }
        data.swap(d2);
        cls.swap(c2);
    }

    KK_GPU gpu;
    if (!gpu.allocate(nP, L, nD2, maxC)) {
        check(false, "allocate");
        return;
    }
    metal_upload_data(&gpu, data.data());

    // ---- M-step --------------------------------------------------------
    const float sentinel = -777.0f;
    std::vector<float> mean(static_cast<size_t>(maxC) * nD, sentinel), cov(static_cast<size_t>(maxC) * nD2, sentinel);
    std::vector<float> weight(maxC, 0.0f);
    metal_mstep(&gpu, cls.data(), cs.alive.data(), mean.data(), cov.data(), weight.data(),
                nAlive, maxC, nP, nD, nD2);
    {
        std::vector<float> mean2(mean.size(), sentinel), cov2(cov.size(), sentinel);
        metal_mstep(&gpu, cls.data(), cs.alive.data(), mean2.data(), cov2.data(), weight.data(),
                    nAlive, maxC, nP, nD, nD2);
        check(std::memcmp(mean.data(), mean2.data(), mean.size() * sizeof(float)) == 0
              && std::memcmp(cov.data(), cov2.data(), cov.size() * sizeof(float)) == 0,
              "M-step: two runs give identical bytes");
    }
    double worstMean = 0, worstCov = 0;
    bool layoutOk = true;
    std::vector<bool> isAlive(maxC, false);
    for (int c : cs.alive) isAlive[c] = true;
    for (int c = 0; c < maxC; ++c) {
        if (!isAlive[c]) {
            for (int k = 0; k < nD; ++k) layoutOk &= mean[c * nD + k] == sentinel;
            for (int k = 0; k < nD2; ++k) layoutOk &= cov[c * nD2 + k] == sentinel;
            continue;
        }
        std::vector<double> mu(nD, 0.0);
        int n = 0;
        for (int p = 0; p < nP; ++p)
            if (cls[p] == c) {
                ++n;
                for (int k = 0; k < nD; ++k) mu[k] += data[static_cast<size_t>(p) * L + k];
            }
        for (int k = 0; k < nD; ++k) {
            mu[k] = n > 0 ? mu[k] / n : 0.0;
            worstMean = std::max(worstMean, std::fabs(mean[c * nD + k] - mu[k]));
        }
        for (int i = 0; i < nD; ++i)
            for (int j = 0; j < nD; ++j) {
                const float got = cov[c * nD2 + i * nD + j];
                if (j < i || n <= 1) { layoutOk &= got == 0.0f; continue; }
                double s = 0.0;
                for (int p = 0; p < nP; ++p)
                    if (cls[p] == c)
                        s += (data[static_cast<size_t>(p) * L + i] - mu[i])
                           * (data[static_cast<size_t>(p) * L + j] - mu[j]);
                const double want = s / (n - 1);
                worstCov = std::max(worstCov, std::fabs(got - want) / (1e-6 + std::fabs(want)));
            }
    }
    check(worstMean < 1e-5, "M-step: means within 1e-5 of the double reference", worstMean);
    check(worstCov < 1e-3, "M-step: covariances within 0.1% of the double reference", worstCov);
    check(layoutOk, "M-step: alive rows written (lower triangle zero), dead rows untouched");

    // Clusters the CPU would keep: enough points and a positive-definite covariance.
    std::vector<int> alive{0};
    std::vector<float> chol(static_cast<size_t>(maxC) * nD2, 0.0f);
    for (int cc = 1; cc < nAlive; ++cc) {
        const int c = cs.alive[cc];
        if (cs.sizes[cc] > nD && cholesky(&cov[c * nD2], &chol[c * nD2], nD)) alive.push_back(c);
    }
    const int nA = static_cast<int>(alive.size());
    // Make the last alive cluster a twin of the first (same mean, factor and
    // weight), so every point has an exact tie for the C-step to break.
    if (nA >= 3) {
        std::copy_n(&mean[alive[1] * nD], nD, &mean[alive[nA - 1] * nD]);
        std::copy_n(&chol[alive[1] * nD2], nD2, &chol[alive[nA - 1] * nD2]);
    }
    for (int c = 0; c < maxC; ++c) weight[c] = 0.0f;
    for (int c : alive) weight[c] = 1.0f / nA;

    // ---- E-step (full) -------------------------------------------------
    const double log2piHalf = std::log(2.0 * std::numbers::pi) * nD * 0.5;
    auto refLogP = [&](int c, int p) {
        const float *f = &chol[c * nD2];
        std::vector<double> root(nD);
        double m = 0.0, logRootDet = 0.0;
        for (int i = 0; i < nD; ++i) {
            double s = data[static_cast<size_t>(p) * L + i] - mean[c * nD + i];
            for (int j = 0; j < i; ++j) s -= f[i * nD + j] * root[j];
            root[i] = s / f[i * nD + i];
            m += root[i] * root[i];
            logRootDet += std::log(static_cast<double>(f[i * nD + i]));
        }
        return 0.5 * m + logRootDet - std::log(static_cast<double>(weight[c])) + log2piHalf;
    };
    std::vector<int> oldCls(cls);
    std::vector<float> logP(static_cast<size_t>(maxC) * nP);
    metal_estep(&gpu, mean.data(), weight.data(), chol.data(), alive.data(), cls.data(),
                oldCls.data(), logP.data(), nA, 0.0f, 1, std::numbers::pi, maxC, nD, nD2);
    double worstE = 0;
    bool noiseOk = true;
    for (int p = 0; p < nP; ++p) noiseOk &= logP[p] == -std::log(weight[0]);
    for (int cc = 1; cc < nA; ++cc)
        for (int p = 0; p < nP; ++p) {
            const double want = refLogP(alive[cc], p);
            worstE = std::max(worstE, std::fabs(logP[static_cast<size_t>(alive[cc]) * nP + p] - want)
                                      / (1.0 + std::fabs(want)));
        }
    check(noiseOk, "E-step: the noise row is -log w0");
    check(worstE < 1e-4, "E-step: LogP within 1e-4 (relative) of the double reference", worstE);

    // ---- C-step ---------------------------------------------------------
    const float huge = 1e32f;
    std::vector<int> c1(nP), cOld(nP), c2(nP);
    metal_cstep(&gpu, c1.data(), cOld.data(), c2.data(), nA, maxC, huge);
    bool cOk = true;
    for (int p = 0; p < nP; ++p) {
        float best = huge, second = huge;
        int top = 0, next = 0;
        for (int cc = 0; cc < nA; ++cc) {
            const int c = alive[cc];
            const float s = logP[static_cast<size_t>(c) * nP + p];
            if (s < best) { next = top; second = best; top = c; best = s; }
            else if (s < second) { next = c; second = s; }
        }
        cOk &= c1[p] == top && c2[p] == next && cOld[p] == cls[p];
    }
    check(cOk, "C-step: Class, Class2, OldClass exactly as KK::CStep");

    // ---- score and deletion loss ---------------------------------------
    double want = 12.5;
    for (int p = 0; p < nP; ++p) want += logP[static_cast<size_t>(c1[p]) * nP + p];
    const float score = metal_compute_score(&gpu, 12.5f);
    check(std::fabs(score - want) <= 1e-5 * (1.0 + std::fabs(want)), "score: within 1e-5 (relative)",
          score - want);
    std::vector<float> loss(maxC, -1.0f);
    metal_deletion_loss(&gpu, loss.data(), maxC);
    std::vector<double> wantLoss(maxC, 0.0);
    for (int p = 0; p < nP; ++p)
        wantLoss[c1[p]] += logP[static_cast<size_t>(c2[p]) * nP + p] - logP[static_cast<size_t>(c1[p]) * nP + p];
    bool lossOk = true;
    for (int c = 0; c < maxC; ++c) lossOk &= loss[c] == static_cast<float>(wantLoss[c]);
    check(lossOk, "deletion loss: exactly the per-class double sums");

    // ---- E-step with the DistThresh skip -------------------------------
    // Move every mean a little; with !FullStep a point keeps its old LogP for
    // a cluster when its class did not change and that cluster was more than
    // DistThresh worse, judged in alive order against the current LogP.
    std::vector<float> prev(logP);
    for (float &m : mean) m += 0.01f;
    std::vector<int> nowCls(c1), was(c1);
    for (int p = 0; p < nP; p += 7) was[p] = 0;       // some points "changed class"
    const float thresh = 3.0f;
    metal_estep(&gpu, mean.data(), weight.data(), chol.data(), alive.data(), nowCls.data(),
                was.data(), logP.data(), nA, thresh, 0, std::numbers::pi, maxC, nD, nD2);
    std::vector<float> ref(prev);
    std::fill_n(ref.begin(), nP, -std::log(weight[0]));
    double worstSkip = 0;
    bool keptOk = true;
    int skipped = 0;
    for (int cc = 1; cc < nA; ++cc) {
        const int c = alive[cc];
        for (int p = 0; p < nP; ++p) {
            const size_t at = static_cast<size_t>(c) * nP + p;
            if (nowCls[p] == was[p]
                && ref[at] - ref[static_cast<size_t>(nowCls[p]) * nP + p] > thresh) {
                ++skipped;
                keptOk &= logP[at] == prev[at];
                continue;
            }
            const double w = refLogP(c, p);
            ref[at] = logP[at];   // continue from the device's value, as the kernel does
            worstSkip = std::max(worstSkip, std::fabs(logP[at] - w) / (1.0 + std::fabs(w)));
        }
    }
    std::printf("  ....  %d of %d (point, cluster) pairs skipped\n", skipped, nP * (nA - 1));
    check(keptOk && skipped > 0, "E-step: skipped pairs keep their LogP");
    check(worstSkip < 1e-4, "E-step: recomputed pairs within 1e-4 (relative)", worstSkip);

    gpu.free_all();
}

} // namespace

int main()
{
    if (!neurosuite::gpu::MetalContext::instance()) {
        std::printf("SKIPPED: no Metal device\n");
        return 0;
    }
    std::printf("Metal device: %s\n", neurosuite::gpu::MetalContext::instance()->deviceName().c_str());
    // alive[0] is the noise cluster; ids are sparse, as after deletions.
    runCase({"12 dims, a cluster over two tiles, an empty and a one-point cluster", 9001, 12, 40, 0,
             {0, 3, 7, 8, 15, 22, 39}, {300, 5000, 2400, 0, 1299, 1, 1}, 11});
    runCase({"1 dimension", 3000, 1, 10, 0, {0, 1, 2, 5}, {500, 1000, 700, 800}, 12});
    runCase({"64 dimensions", 2500, 64, 8, 0, {0, 2, 4, 6}, {200, 900, 700, 700}, 13});
    runCase({"13 loaded, steps on the first 12 (CEMTwoPhase's spatial phase)", 6000, 13, 20, 12,
             {0, 1, 4, 9, 12}, {400, 2000, 1600, 1000, 1000}, 14});
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
