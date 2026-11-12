// xcorr_weighted_test.cpp — the weighted realign kernel (xcorr_omp_compute_weighted).
//
//   * weights == nullptr and weights == 1 everywhere reproduce xcorr_omp_compute
//     bit for bit (shift and score);
//   * a spike with a COLLISION in its tail (a second, larger spike 14 samples
//     after the template's peak) is pulled toward the collision by the
//     whole-window kernel and aligned by a Gaussian window on the peak;
//   * a zero spike scores 0 and is not moved under a window either.
// Self-contained (own main), run via ctest.
#include "realign_xcorr.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int fails = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok:  " : "FAIL:", what);
    if (!ok) ++fails;
}

int main()
{
    const int N = 32, C = 2, P = 12;                       // window, channels, 0-based peak
    // template: a trough at P with a small late lobe
    std::vector<int16_t> tmpl(static_cast<size_t>(C * N));
    for (int ch = 0; ch < C; ++ch)
        for (int s = 0; s < N; ++s) {
            const double d = s - P;
            const double v = -1000.0 * std::exp(-0.5 * d * d / 2.25) + 250.0 * std::exp(-0.5 * (d - 6) * (d - 6) / 6.0);
            tmpl[static_cast<size_t>(ch * N + s)] = static_cast<int16_t>(std::lround(v * (ch ? 0.6 : 1.0)));
        }
    auto spike = [&](int jitter, bool collide, std::vector<int16_t>& out) {
        out.assign(static_cast<size_t>(C * N), 0);
        for (int ch = 0; ch < C; ++ch)
            for (int s = 0; s < N; ++s) {
                const int src = ((s + jitter) % N + N) % N;     // spike[s] = tmpl[s + jitter]: the unit early by `jitter`, lag -jitter
                double v = tmpl[static_cast<size_t>(ch * N + src)];
                if (collide) {                                   // a bigger spike 14 samples after the peak
                    const double d = s - (P + 14);
                    v += -1600.0 * std::exp(-0.5 * d * d / 2.25) * (ch ? 0.9 : 1.0);
                }
                out[static_cast<size_t>(ch * N + s)] = static_cast<int16_t>(std::lround(v));
            }
    };
    std::vector<int16_t> waves;
    std::vector<int16_t> one;
    const int jit[4] = {0, 2, -3, 1};
    for (int k = 0; k < 4; ++k) { spike(jit[k], false, one); waves.insert(waves.end(), one.begin(), one.end()); }
    spike(2, true, one);  waves.insert(waves.end(), one.begin(), one.end());   // #4: jitter 2 + collision
    spike(-3, true, one); waves.insert(waves.end(), one.begin(), one.end());   // #5: jitter -3 + collision
    one.assign(static_cast<size_t>(C * N), 0); waves.insert(waves.end(), one.begin(), one.end());   // #6: zero
    const int nSp = 7, maxShift = 6;
    std::vector<int> sh0(nSp), sh1(nSp), shW(nSp);
    std::vector<float> sc0(nSp), sc1(nSp), scW(nSp);
    xcorr_omp_compute(waves.data(), tmpl.data(), nSp, C, N, maxShift, 0.5f, sh0.data(), sc0.data());
    xcorr_omp_compute_weighted(waves.data(), tmpl.data(), nullptr, nSp, C, N, maxShift, 0.5f, sh1.data(), sc1.data());
    check(sh0 == sh1 && sc0 == sc1, "weights == nullptr reproduces the unweighted kernel (shifts and scores)");
    std::vector<float> ones(static_cast<size_t>(N), 1.0f);
    xcorr_omp_compute_weighted(waves.data(), tmpl.data(), ones.data(), nSp, C, N, maxShift, 0.5f, sh1.data(), sc1.data());
    bool same = sh0 == sh1;
    for (int k = 0; k < nSp; ++k) same = same && std::fabs(sc0[static_cast<size_t>(k)] - sc1[static_cast<size_t>(k)]) < 1e-5f;
    check(same, "weights == 1 everywhere reproduces it too");
    // clean spikes: both kernels find the jitter
    bool clean = true;
    for (int k = 0; k < 4; ++k) clean = clean && sh0[static_cast<size_t>(k)] == -jit[k];
    check(clean, "unweighted: the clean spikes' lags are minus their jitters");
    std::vector<float> gauss(static_cast<size_t>(N));
    for (int s = 0; s < N; ++s) { const double d = (s - P) / 2.0; gauss[static_cast<size_t>(s)] = static_cast<float>(std::exp(-0.5 * d * d)); }
    xcorr_omp_compute_weighted(waves.data(), tmpl.data(), gauss.data(), nSp, C, N, maxShift, 0.5f, shW.data(), scW.data());
    clean = true;
    for (int k = 0; k < 4; ++k) clean = clean && shW[static_cast<size_t>(k)] == -jit[k];
    check(clean, "gauss +-4: the clean spikes' lags are minus their jitters");
    std::printf("    collided spikes: unweighted lags %d %d (expected -2 3), gauss lags %d %d; scores %.3f %.3f -> %.3f %.3f\n",
                sh0[4], sh0[5], shW[4], shW[5], sc0[4], sc0[5], scW[4], scW[5]);
    check(sh0[4] != -2 || sh0[5] != 3, "unweighted: a collision 14 samples after the peak pulls at least one lag off its jitter");
    check(shW[4] == -2 && shW[5] == 3, "gauss +-4: both collided spikes are aligned on their own peak");
    check(scW[4] > sc0[4] && scW[5] > sc0[5], "and score higher under the window than over the whole window");
    check(shW[6] == 0 && scW[6] == 0.0f, "a zero spike: shift 0, score 0 under a window");
    std::printf("xcorr_weighted_test: %s (%d failed)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
