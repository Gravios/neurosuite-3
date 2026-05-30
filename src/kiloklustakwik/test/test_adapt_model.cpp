// =============================================================================
// test_adapt_model.cpp  —  unit tests for ISI-conditional fit
// =============================================================================
#include "../adapt_model.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <vector>

namespace {

constexpr const char* GREEN = "\033[32m";
constexpr const char* RED   = "\033[31m";
constexpr const char* RESET = "\033[0m";

int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg) do {                                              \
    if (cond) {                                                            \
        std::printf("  %sPASS%s %s\n", GREEN, RESET, msg);                 \
        ++g_pass;                                                          \
    } else {                                                               \
        std::printf("  %sFAIL%s %s   (%s:%d)\n", RED, RESET, msg,          \
                    __FILE__, __LINE__);                                   \
        ++g_fail;                                                          \
    }                                                                      \
} while (0)

// Triphasic synthetic spike on a single channel.
std::vector<float> synth(int nSamp, int peakIdx, float amp) {
    std::vector<float> out(static_cast<size_t>(nSamp), 0.0f);
    for (int s = 0; s < nSamp; ++s) {
        const float ds = static_cast<float>(s - peakIdx);
        out[static_cast<size_t>(s)] =
            amp * (-std::exp(-ds * ds / 4.0f)
                   + 0.3f * std::exp(-(ds - 4.0f) * (ds - 4.0f) / 16.0f));
    }
    return out;
}

// Build channel-major waveform from per-channel scales.
std::vector<float> buildMC(int nCh, int nSamp, int peakIdx,
                            const std::vector<float>& chScales, float amp) {
    std::vector<float> out(static_cast<size_t>(nCh) * nSamp, 0.0f);
    auto t = synth(nSamp, peakIdx, amp);
    for (int ch = 0; ch < nCh; ++ch) {
        const float s = chScales[static_cast<size_t>(ch)];
        for (int k = 0; k < nSamp; ++k) {
            out[static_cast<size_t>(ch) * nSamp + k] = s * t[static_cast<size_t>(k)];
        }
    }
    return out;
}

}  // anonymous

// -----------------------------------------------------------------------------
// T1: cluster with too few spikes → disabled
// -----------------------------------------------------------------------------
static void test_too_few() {
    std::puts("T1: too few spikes → disabled");
    const int nCh = 4, nSamp = 32, N = nCh * nSamp;
    std::vector<float> spikes(static_cast<size_t>(50) * N, 0.0f);
    std::vector<int>   labels(50, 2);
    std::vector<float> isi(50, 0.05f);

    adapt_model::Config cfg;
    cfg.minSpikesForFit = 100;
    auto r = adapt_model::FitAll(spikes.data(), 50, nCh, nSamp,
                                   labels.data(), isi.data(), cfg);
    CHECK(r.clusters.size() == 1, "one cluster found");
    CHECK(r.clusters[0].disabled, "disabled due to too few spikes");
    CHECK(r.nDisabled == 1, "diagnostic counter");
}

// -----------------------------------------------------------------------------
// T2: synthetic data with known (w₀, v, α, τ) — verify recovery.
//     200 spikes from one cluster, alpha=0.5, tau=0.020s, v = rank-1
//     amplitude reduction.  Inject Gaussian noise.
// -----------------------------------------------------------------------------
static void test_recovery() {
    std::puts("T2: synthetic ground truth — α, τ recovered");

    const int nCh = 4, nSamp = 32, N = nCh * nSamp;
    const float trueAlpha = 0.5f;
    const float trueTau   = 0.020f;
    const std::vector<float> chScales = {1.0f, 0.5f, 0.7f, 0.3f};

    std::mt19937 rng(123);
    std::normal_distribution<float> nz(0.0f, 0.05f);  // noise σ
    std::uniform_real_distribution<float> isiDist(0.003f, 0.300f);

    const int M = 500;
    std::vector<float> spikes(static_cast<size_t>(M) * N, 0.0f);
    std::vector<int>   labels(M, 2);
    std::vector<float> isi(M, 0.0f);

    // Build w₀ (rested) and a rank-1 adaptation direction v.
    auto w0true = buildMC(nCh, nSamp, 16, chScales, 1.0f);
    // adaptation = reduce amplitude (negative scaling of the same shape)
    auto vTrue  = buildMC(nCh, nSamp, 16, chScales, -1.0f);
    // Normalise v
    double vNorm = 0.0;
    for (float x : vTrue) vNorm += static_cast<double>(x) * x;
    vNorm = std::sqrt(vNorm);
    for (auto& x : vTrue) x = static_cast<float>(static_cast<double>(x) / vNorm);

    for (int i = 0; i < M; ++i) {
        const float thisIsi = isiDist(rng);
        isi[static_cast<size_t>(i)] = thisIsi;
        const float h = std::exp(-thisIsi / trueTau);
        for (int k = 0; k < N; ++k) {
            spikes[static_cast<size_t>(i) * N + k] =
                w0true[static_cast<size_t>(k)] +
                trueAlpha * h * vTrue[static_cast<size_t>(k)] +
                nz(rng);
        }
    }

    adapt_model::Config cfg;
    cfg.tauGrid = {0.005f, 0.015f, 0.050f, 0.150f};  // 0.020 falls between 0.015 and 0.050
    cfg.minSpikesForFit = 100;
    cfg.minValidIsiForFit = 100;
    cfg.maxIter = 30;
    auto r = adapt_model::FitAll(spikes.data(), M, nCh, nSamp,
                                   labels.data(), isi.data(), cfg);
    CHECK(r.clusters.size() == 1, "one cluster fit");
    CHECK(!r.clusters[0].disabled, "fit not disabled");
    const auto& cm = r.clusters[0];

    // τ should be within parabolic-interpolated range of ground truth
    const float tauErr = std::fabs(cm.tau - trueTau) / trueTau;
    std::printf("    recovered τ=%.4f (truth=%.4f), α=%.3f (truth=%.3f), residVar=%.2e\n",
                cm.tau, trueTau, cm.alpha, trueAlpha, cm.fitResidVar);
    CHECK(tauErr < 0.5f, "τ within 50% of truth (grid-limited)");
    CHECK(std::fabs(cm.alpha - trueAlpha) < 0.15f, "α within 0.15 of truth");
    CHECK(cm.fitResidVar < 0.01f, "residual variance small");

    // Sign ambiguity: v might be flipped (with α sign flipped to compensate).
    // Check that α·v is close to trueAlpha·vTrue regardless of sign.
    double dotPos = 0.0, dotNeg = 0.0;
    for (int k = 0; k < N; ++k) {
        const double prod = static_cast<double>(cm.alpha) *
                            static_cast<double>(cm.v[static_cast<size_t>(k)]);
        const double targ = static_cast<double>(trueAlpha) *
                            static_cast<double>(vTrue[static_cast<size_t>(k)]);
        dotPos += (prod - targ) * (prod - targ);
        dotNeg += (prod + targ) * (prod + targ);
    }
    const double bestDiff = std::min(dotPos, dotNeg);
    CHECK(bestDiff / N < 0.02, "α·v matches truth (up to sign)");
}

// -----------------------------------------------------------------------------
// T3: predict() and residual() — round-trip
// -----------------------------------------------------------------------------
static void test_predict_residual() {
    std::puts("T3: predict()/residual() round-trip");

    const int nCh = 2, nSamp = 16, N = nCh * nSamp;
    adapt_model::ClusterModel cm;
    cm.w0.assign(static_cast<size_t>(N), 0.5f);
    cm.v.assign(static_cast<size_t>(N), 0.1f);
    cm.alpha = 2.0f;
    cm.tau = 0.030f;
    cm.disabled = false;

    std::vector<float> pred(static_cast<size_t>(N), 0.0f);
    cm.predict(0.005f, nCh, nSamp, pred.data());
    // h = exp(-0.005/0.030) ≈ 0.846
    const float h = std::exp(-0.005f / 0.030f);
    const float expected = 0.5f + 2.0f * h * 0.1f;
    CHECK(std::fabs(pred[0] - expected) < 1e-4f, "predict matches model");

    // Residual of a spike == pred(isi) should be zero
    std::vector<float> spike = pred;
    std::vector<float> resid(static_cast<size_t>(N), 0.0f);
    cm.residual(spike.data(), 0.005f, nCh, nSamp, resid.data());
    float maxResid = 0.0f;
    for (float r : resid) maxResid = std::max(maxResid, std::fabs(r));
    CHECK(maxResid < 1e-4f, "residual of own prediction is 0");

    // Infinite ISI → predict = w0
    cm.predict(std::numeric_limits<float>::infinity(), nCh, nSamp, pred.data());
    CHECK(std::fabs(pred[0] - 0.5f) < 1e-4f, "predict at ∞ ISI = w0");
}

// -----------------------------------------------------------------------------
// T4: cluster with all-finite, all-equal ISI → fit should be degenerate
//     (no variation in h means we can't separate w0 from α·v).  Should
//     either succeed with α≈0 or be disabled.
// -----------------------------------------------------------------------------
static void test_constant_isi() {
    std::puts("T4: constant ISI → α ≈ 0 (cannot identify adaptation)");
    const int nCh = 4, nSamp = 32, N = nCh * nSamp;
    const int M = 200;

    std::mt19937 rng(7);
    std::normal_distribution<float> nz(0.0f, 0.05f);

    auto w0true = buildMC(nCh, nSamp, 16, {1.0f, 0.5f, 0.7f, 0.3f}, 1.0f);

    std::vector<float> spikes(static_cast<size_t>(M) * N, 0.0f);
    std::vector<int>   labels(M, 2);
    std::vector<float> isi(M, 0.050f);  // ALL THE SAME

    for (int i = 0; i < M; ++i) {
        for (int k = 0; k < N; ++k) {
            spikes[static_cast<size_t>(i) * N + k] =
                w0true[static_cast<size_t>(k)] + nz(rng);
        }
    }

    adapt_model::Config cfg;
    cfg.minSpikesForFit = 50;
    cfg.minValidIsiForFit = 50;
    auto r = adapt_model::FitAll(spikes.data(), M, nCh, nSamp,
                                   labels.data(), isi.data(), cfg);
    CHECK(r.clusters.size() == 1, "one cluster found");
    // Fit should succeed but with α ≈ 0
    const auto& cm = r.clusters[0];
    if (!cm.disabled) {
        // α absorbed essentially nothing because h is constant
        std::printf("    α=%.3f (expected ~0 with constant ISI)\n", cm.alpha);
        CHECK(std::fabs(cm.alpha) < 0.5f, "α small with constant ISI");
    } else {
        CHECK(true, "(disabled — acceptable behaviour)");
    }
}

int main() {
    std::puts("===============================================");
    std::puts(" adapt_model unit tests");
    std::puts("===============================================");
    test_too_few();
    test_recovery();
    test_predict_residual();
    test_constant_isi();
    std::puts("===============================================");
    std::printf(" Results: %d passed, %d failed\n", g_pass, g_fail);
    std::puts("===============================================");
    return g_fail == 0 ? 0 : 1;
}
