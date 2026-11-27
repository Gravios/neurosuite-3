// Klusters' GPU kernels against their CPU loops, on the backend the dispatcher picks.
//
// The core cut's ordering loop (CoreCutGpu::order, corecut_gpu.h) and the
// PCA-refine pass (PcaRefineGpu::refine, pca_refine_gpu.h) each have a CUDA,
// a HIP and a SYCL kernel.  This runs whichever the build has and the machine
// can use, on synthetic data, and compares it with the CPU:
//
//   - the core cut: the same order as corecut::detail::orderCpu, except where
//     two spikes were within rounding of each other (the header allows a
//     near-tie to go the other way); the steps within 1e-4 relative;
//   - the PCA refine: the same best shift as a direct evaluation of the
//     kernel's sum (per candidate the stderived or raw window, its projection
//     energy, the first largest), for both the raw and the stderiv path, with
//     a margin for float near-ties.
//
// With no GPU backend usable the test says so and passes: on a machine
// without a device there is nothing to compare.  Run it on each GPU machine:
//   ctest -R klusters_test_gpu_parity --output-on-failure

#include "corecut.h"
#include "corecut_dispatch.h"
#include "pca_refine_dispatch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (cond) std::printf("  ok:   %s\n", msg);                            \
        else { std::printf("  FAIL: %s\n", msg); ++g_fail; }                   \
    } while (0)

// ── the core cut ─────────────────────────────────────────────────────────────
static void coreCut()
{
    if (!CoreCutGpu::hasGpu()) { std::printf("core cut: no GPU backend usable here -- nothing to compare\n"); return; }
    const long n = std::max(6000L, CoreCutGpu::gpuThreshold() + 1000);
    const int  d = 48;
    std::mt19937 rng(11);
    std::normal_distribution<float> g(0.0f, 1.0f);
    std::vector<float> x(static_cast<std::size_t>(n) * d);
    for (long i = 0; i < n; ++i) {
        const float off = (i % 4 == 0) ? 3.0f : 0.0f;      // a quarter from a second unit
        for (int u = 0; u < d; ++u) x[static_cast<std::size_t>(i) * d + u] = g(rng) + (u < 8 ? off : 0.0f);
    }
    const long seed = 1;
    std::vector<long> oc(static_cast<std::size_t>(n)), og(static_cast<std::size_t>(n));
    std::vector<double> sc(static_cast<std::size_t>(n)), sg(static_cast<std::size_t>(n));
    corecut::detail::orderCpu(n, d, x.data(), seed, oc.data(), sc.data());
    const bool ran = CoreCutGpu::order(n, d, x.data(), seed, og.data(), sg.data());
    CHECK(ran, "the core cut's GPU ordering ran");
    if (!ran) return;
    // The first step where the two differ, and whether it was a near-tie there.
    long firstDiff = -1;
    for (long k = 0; k < n && firstDiff < 0; ++k) if (oc[static_cast<std::size_t>(k)] != og[static_cast<std::size_t>(k)]) firstDiff = k;
    double worst = 0.0;
    for (long k = 1; k < (firstDiff < 0 ? n : firstDiff); ++k) {
        const double a = sc[static_cast<std::size_t>(k)], b = sg[static_cast<std::size_t>(k)];
        worst = std::max(worst, std::fabs(a - b) / std::max(1e-9, std::fabs(a)));
    }
    const bool tie = firstDiff > 0
        && std::fabs(sc[static_cast<std::size_t>(firstDiff)] - sg[static_cast<std::size_t>(firstDiff)])
               <= 1e-4 * std::max(1e-9, sc[static_cast<std::size_t>(firstDiff)]);
    std::printf("core cut: %ld spikes x %d; orders agree for %ld steps%s; steps differ by %.2e relative at most\n",
                n, d, firstDiff < 0 ? n : firstDiff, firstDiff < 0 ? " (all)" : (tie ? " (then a near-tie)" : ""), worst);
    CHECK(firstDiff < 0 || tie, "the GPU takes the spikes in the CPU's order (up to a near-tie)");
    CHECK(worst < 1e-4, "... at the CPU's step sizes");
}

// ── the PCA refine ───────────────────────────────────────────────────────────
struct RefineCase {
    int K, nChan, nSamp, maxShift, chForPca, kComp, d2u, rShift;
    bool centered, stder;
};

/** The kernel's sum for one spike and candidate (pca_refine_cuda.cu). */
static double energyOf(const RefineCase& r, const std::vector<int16_t>& raw, int spike, int c,
                       const std::vector<float>& evec, const std::vector<float>& means)
{
    const int wideLen = r.nSamp + 2 * r.maxShift, need = r.rShift + r.d2u;
    std::vector<int> rows(static_cast<std::size_t>(r.chForPca) * need), prev(static_cast<std::size_t>(r.chForPca), 0);
    const int16_t* w = raw.data() + static_cast<std::size_t>(spike) * r.nChan * wideLen;
    for (int t = 0; t < need; ++t) {
        int total = 0;
        for (int ch = 0; ch < r.nChan; ++ch) total += w[static_cast<std::size_t>(ch) * wideLen + c + t];
        for (int ch = 0; ch < r.chForPca; ++ch) {
            const int v = w[static_cast<std::size_t>(ch) * wideLen + c + t];
            if (!r.stder) { rows[static_cast<std::size_t>(ch) * need + t] = v; continue; }
            const int sd = std::clamp(r.nChan * v - total, -32768, 32767);
            const int diff = std::clamp(sd - prev[static_cast<std::size_t>(ch)], -32768, 32767);
            prev[static_cast<std::size_t>(ch)] = sd;
            rows[static_cast<std::size_t>(ch) * need + t] = diff;
        }
    }
    double e = 0.0;
    for (int ch = 0; ch < r.chForPca; ++ch)
        for (int k = 0; k < r.kComp; ++k) {
            double s = 0.0;
            for (int u = 0; u < r.d2u; ++u) {
                double xv = rows[static_cast<std::size_t>(ch) * need + r.rShift + u];
                if (r.centered) xv -= means[static_cast<std::size_t>(ch) * r.d2u + u];
                s += evec[static_cast<std::size_t>(ch) * r.kComp * r.d2u + static_cast<std::size_t>(k) * r.d2u + u] * xv;
            }
            e += s * s;
        }
    return e;
}

static void refine(const RefineCase& r, const char* name)
{
    const int wideLen = r.nSamp + 2 * r.maxShift, M = 2 * r.maxShift + 1;
    std::mt19937 rng(5);
    std::normal_distribution<float> g(0.0f, 1.0f);
    // A unit's spike at a random shift in each window, plus noise.
    std::vector<int16_t> raw(static_cast<std::size_t>(r.K) * r.nChan * wideLen);
    std::uniform_int_distribution<int> at(-r.maxShift, r.maxShift);
    for (int k = 0; k < r.K; ++k) {
        const int s = at(rng);
        for (int ch = 0; ch < r.nChan; ++ch)
            for (int t = 0; t < wideLen; ++t) {
                const double tt = t - (r.maxShift + r.nSamp / 2 + s);
                const double spike = -400.0 * (1.0 - 0.1 * ch) * std::exp(-tt * tt / 8.0) + 120.0 * std::exp(-(tt - 6) * (tt - 6) / 18.0);
                raw[(static_cast<std::size_t>(k) * r.nChan + ch) * wideLen + t] = static_cast<int16_t>(std::lround(spike + 15.0 * g(rng)));
            }
    }
    std::vector<float> evec(static_cast<std::size_t>(r.chForPca) * r.kComp * r.d2u), means(static_cast<std::size_t>(r.chForPca) * r.d2u);
    for (float& v : evec) v = 0.1f * g(rng);
    for (float& v : means) v = 5.0f * g(rng);
    std::vector<int> best(static_cast<std::size_t>(r.K), 99);
    const int rc = PcaRefineGpu::refine(r.K, M, wideLen, r.nSamp, r.nChan, r.chForPca, r.kComp, r.d2u, r.rShift, r.maxShift,
                                        r.centered ? 1 : 0, r.stder ? 1 : 0, raw.data(), evec.data(), means.data(), best.data());
    CHECK(rc == 0, "the PCA refine ran on the GPU");
    if (rc != 0) { std::printf("    (%s: returned %d)\n", name, rc); return; }
    int same = 0, tie = 0, wrong = 0;
    for (int k = 0; k < r.K; ++k) {
        std::vector<double> e(static_cast<std::size_t>(M));
        for (int c = 0; c < M; ++c) e[static_cast<std::size_t>(c)] = energyOf(r, raw, k, c, evec, means);
        const int cpu = static_cast<int>(std::max_element(e.begin(), e.end()) - e.begin());
        const int gpu = best[static_cast<std::size_t>(k)] + r.maxShift;
        if (gpu == cpu) ++same;
        else if (gpu >= 0 && gpu < M && e[static_cast<std::size_t>(gpu)] >= e[static_cast<std::size_t>(cpu)] * (1.0 - 1e-5)) ++tie;
        else ++wrong;
    }
    std::printf("pca refine (%s): %d spikes, %d candidates: %d the same shift, %d a float near-tie, %d different\n",
                name, r.K, M, same, tie, wrong);
    CHECK(wrong == 0, "every spike gets the CPU's best shift (up to a float near-tie)");
}

int main()
{
    coreCut();
    if (!PcaRefineGpu::hasGpu()) {
        std::printf("pca refine: no GPU backend usable here -- nothing to compare\n");
    } else {
        const int K = std::max(PcaRefineGpu::gpuThreshold() * 2, 512);
        refine(RefineCase{K, 8, 32, 6, 8, 3, 24, 4, true, false}, "raw, centred");
        refine(RefineCase{K, 8, 32, 6, 8, 3, 24, 4, false, true}, "stderiv");
        refine(RefineCase{K, 5, 40, 8, 4, 2, 30, 6, true, true}, "odd channel count, stderiv");
    }
    std::printf("%s (%d failure(s))\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
