// realign_kernels_test.cpp — edge cases of the shared realignment kernels.
//
// Exercises the always-compiled OpenMP xcorr kernel (xcorr_omp_compute), the
// reference behaviour the CUDA/HIP/SYCL back ends must match, on inputs where
// the correlation is undefined:
//   - an all-zero spike (KiloKlustaKwik zero-fills a spike whose read failed
//     and commits every nonzero shift) must come back as shift 0 / score 0,
//     even with minScore 0 or negative;
//   - an all-zero template likewise gives shift 0 / score 0 for every spike;
//   - ordinary spikes in the same batch are still aligned;
//   - an exact two-way tie between lags resolves to the lower lag (the first
//     maximum) -- the rule the CUDA/HIP tree reduction must reproduce.
// Qt-free, self-contained (own main), run via ctest.

#include "realign_xcorr.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

// A spike with its trough at sample `peak` on every channel (channel-major).
static void putSpike(int16_t* w, int nCh, int nS, int peak) {
    for (int c = 0; c < nCh; ++c)
        for (int s = 0; s < nS; ++s)
            w[c * nS + s] = static_cast<int16_t>(
                s == peak ? -1000 : (s == peak - 1 || s == peak + 1) ? -400 : 0);
}

static void testXcorrUndefined() {
    const int nCh = 2, nS = 32, maxShift = 4;
    const int nSp = 3;   // spike 0 shifted +2, spike 1 all zero, spike 2 aligned
    std::vector<int16_t> tmpl(static_cast<size_t>(nCh) * nS);
    std::vector<int16_t> wave(static_cast<size_t>(nSp) * nCh * nS, 0);
    putSpike(tmpl.data(), nCh, nS, 16);
    putSpike(wave.data(), nCh, nS, 18);
    putSpike(wave.data() + 2 * nCh * nS, nCh, nS, 16);

    for (float minScore : {0.0f, -1.0f, 0.5f}) {
        std::vector<int> sh(nSp, -99);
        std::vector<float> sc(nSp, -99.0f);
        const int rc = xcorr_omp_compute(wave.data(), tmpl.data(), nSp, nCh, nS,
                                         maxShift, minScore, sh.data(), sc.data());
        const std::string tag = " (minScore " + std::to_string(minScore) + ")";
        check(rc == 0, "omp rc 0" + tag);
        check(sh[0] == 2, "shifted spike realigned by +2" + tag
              + " (got " + std::to_string(sh[0]) + ")");
        check(sh[1] == 0, "all-zero spike gets shift 0" + tag
              + " (got " + std::to_string(sh[1]) + ")");
        check(sc[1] == 0.0f && std::isfinite(sc[1]), "all-zero spike scores 0" + tag);
        check(sh[2] == 0 && sc[2] > 0.99f, "aligned spike stays at 0" + tag);
    }

    // Zero template: no spike is moved, every score is 0.
    {
        std::vector<int16_t> zero(tmpl.size(), 0);
        std::vector<int> sh(nSp, -99);
        std::vector<float> sc(nSp, -99.0f);
        xcorr_omp_compute(wave.data(), zero.data(), nSp, nCh, nS,
                          maxShift, 0.0f, sh.data(), sc.data());
        bool ok = true;
        for (int i = 0; i < nSp; ++i) ok &= sh[i] == 0 && sc[i] == 0.0f;
        check(ok, "zero template: every spike gets shift 0 / score 0");
    }
}

static void testXcorrTie() {
    // Spike = two copies of the template, 3 samples early and 3 samples late.
    // score(-3) and score(+3) are both R(0)+R(6) (exact integer sums), score(0)
    // is 2R(3) = 0: a two-way tie that must resolve to -3.
    const int nCh = 2, nS = 32, maxShift = 4;
    std::vector<int16_t> tmpl(static_cast<size_t>(nCh) * nS), wave(tmpl.size()),
                         a(tmpl.size()), b(tmpl.size());
    putSpike(tmpl.data(), nCh, nS, 16);
    putSpike(a.data(), nCh, nS, 13);
    putSpike(b.data(), nCh, nS, 19);
    for (size_t i = 0; i < wave.size(); ++i) wave[i] = static_cast<int16_t>(a[i] + b[i]);
    int sh = -99; float sc = 0.0f;
    xcorr_omp_compute(wave.data(), tmpl.data(), 1, nCh, nS, maxShift, 0.0f, &sh, &sc);
    check(sh == -3, "tie between lags -3 and +3 resolves to -3 (got "
          + std::to_string(sh) + ")");
}

int main() {
    testXcorrUndefined();
    testXcorrTie();
    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
