// xcorr_dispatch_test.cpp — XcorrDispatch under concurrent calls.
//
// KiloKlustaKwik calls XcorrDispatch::compute from inside an OpenMP parallel
// loop, so back-end detection and GPU demotion must be safe when several
// threads arrive at once.  This test builds the dispatcher with USE_CUDA and
// supplies a fake CUDA back end (defined below) that counts its calls and can
// be told to fail.  It checks that:
//   - the back end is probed exactly once, however many threads start together;
//   - after one GPU failure every later call runs on OpenMP;
//   - a maxShift beyond the CUDA block limit runs on OpenMP without demoting;
//   - results always come back (from the fake GPU or the real OpenMP kernel).
// Build it with -fsanitize=thread to have the race itself reported.
// Self-contained (own main), run via ctest.

#include "realign_xcorr.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

static std::atomic<int>  g_probes{0}, g_gpuCalls{0};
static std::atomic<bool> g_gpuFails{false};

extern "C" {
int xcorr_cuda_available()
{
    ++g_probes;
    // widen the window in which an unsynchronised first call would race
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return 1;
}
int xcorr_cuda_compute(const int16_t*, const int16_t*, int nSpikes, int, int,
                       int, float, int* shifts, float* scores)
{
    ++g_gpuCalls;
    if (g_gpuFails.load()) return -1;
    for (int i = 0; i < nSpikes; ++i) { shifts[i] = 7; scores[i] = 1.0f; }   // fake marker
    return 0;
}
}

static int g_fail = 0, g_ran = 0;
static void check(bool ok, const std::string& what) {
    ++g_ran;
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++g_fail; }
}

int main() {
    const int nSp = 4, nCh = 2, nS = 32;
    std::vector<int16_t> wave(static_cast<size_t>(nSp) * nCh * nS), tmpl(static_cast<size_t>(nCh) * nS);
    for (int c = 0; c < nCh; ++c)
        for (int s = 0; s < nS; ++s) {
            const int16_t v = static_cast<int16_t>(s == 16 ? -1000 : (s == 15 || s == 17) ? -400 : 0);
            tmpl[static_cast<size_t>(c) * nS + s] = v;
            for (int i = 0; i < nSp; ++i)            // every spike shifted by +2
                wave[(static_cast<size_t>(i) * nCh + c) * nS + s] =
                    static_cast<int16_t>(s == 18 ? -1000 : (s == 17 || s == 19) ? -400 : 0);
        }

    auto run = [&](int maxShift, std::vector<int>& sh) {
        std::vector<float> sc(nSp);
        sh.assign(nSp, -99);
        return XcorrDispatch::compute(wave.data(), tmpl.data(), nSp, nCh, nS,
                                      maxShift, 0.0f, sh.data(), sc.data());
    };
    auto onGpu = [](const std::vector<int>& sh) { return sh[0] == 7; };

    // 1. Many threads make the first call together: one probe, all on the GPU.
    {
        const int nT = 8;
        std::vector<std::thread> ts;
        std::vector<int> ok(nT, 0);
        for (int t = 0; t < nT; ++t)
            ts.emplace_back([&, t] { std::vector<int> sh; ok[t] = run(4, sh) == 0 && onGpu(sh); });
        for (auto& th : ts) th.join();
        check(g_probes.load() == 1, "back end probed exactly once (got "
              + std::to_string(g_probes.load()) + ")");
        bool all = true; for (int v : ok) all &= v != 0;
        check(all, "concurrent first calls all ran on the GPU");
        check(std::string(XcorrDispatch::backendName()) == "CUDA", "backendName is CUDA");
    }

    // 2. A shift too wide for the CUDA block runs on OpenMP and does not demote.
    {
        const int before = g_gpuCalls.load();
        std::vector<int> sh;
        check(run(600, sh) == 0 && !onGpu(sh), "maxShift 600 ran on OpenMP");
        check(g_gpuCalls.load() == before, "maxShift 600 did not call the GPU");
        check(run(4, sh) == 0 && onGpu(sh), "the GPU is still used afterwards");
    }

    // 3. The GPU starts failing while threads are calling: every call still
    //    returns a result, and afterwards the dispatcher stays on OpenMP.
    {
        g_gpuFails = true;
        const int nT = 8;
        std::vector<std::thread> ts;
        std::vector<int> ok(nT, 0);
        for (int t = 0; t < nT; ++t)
            ts.emplace_back([&, t] {
                std::vector<int> sh; ok[t] = run(4, sh) == 0 && sh[0] == 2; });
        for (auto& th : ts) th.join();
        bool all = true; for (int v : ok) all &= v != 0;
        check(all, "calls during the failure fell back to OpenMP (shift +2 found)");
        check(std::string(XcorrDispatch::backendName()) == "OpenMP CPU", "demoted to OpenMP");
        g_gpuFails = false;
        const int before = g_gpuCalls.load();
        std::vector<int> sh;
        check(run(4, sh) == 0 && sh[0] == 2 && g_gpuCalls.load() == before,
              "after demotion the GPU is not called again");
    }

    std::printf("%d/%d checks passed\n", g_ran - g_fail, g_ran);
    return g_fail ? 1 : 0;
}
