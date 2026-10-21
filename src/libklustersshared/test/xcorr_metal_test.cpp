// xcorr_metal_test.cpp — the Metal realignment back end (realign_xcorr_metal.cpp)
// against the OpenMP one, bit for bit: shifts and scores.  The GPU computes
// exact integer numerators and the host scores them as the OpenMP path does,
// so any difference is a bug.  Covers several geometries, a zero-energy spike,
// a spike equal to the template, int16 extremes and a maxShift wider than the
// waveform, and checks that XcorrDispatch picks Metal.  Prints SKIPPED (exit
// 0) on a machine with no Metal device.
#include "realign_xcorr.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

static int g_fail = 0;
static void check(bool ok, const std::string& what)
{
    std::printf("  %s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (!ok) ++g_fail;
}

int main()
{
    if (!xcorr_metal_available()) {
        std::printf("SKIPPED: no Metal device on this machine\n");
        return 0;
    }
    std::mt19937 rng(23);
    struct Case { int nSp, nCh, nS, maxShift; double sd; };
    for (const Case& c : {Case{2000, 8, 52, 13, 300.0}, Case{500, 1, 32, 5, 50.0},
                          Case{300, 12, 40, 50, 300.0}, Case{400, 4, 48, 6, 14000.0}}) {
        std::vector<int16_t> w(static_cast<size_t>(c.nSp) * c.nCh * c.nS),
                             t(static_cast<size_t>(c.nCh) * c.nS);
        std::normal_distribution<double> g(0.0, c.sd);
        auto draw = [&] { return static_cast<int16_t>(std::clamp(g(rng), -32768.0, 32767.0)); };
        for (auto& v : t) v = draw();
        for (int sp = 0; sp < c.nSp; ++sp)
            for (int i = 0; i < c.nCh * c.nS; ++i)
                w[static_cast<size_t>(sp) * c.nCh * c.nS + i] =
                    sp % 97 == 3 ? int16_t(0) : sp % 97 == 5 ? t[i] : draw();
        std::vector<int> shG(c.nSp), shC(c.nSp);
        std::vector<float> scG(c.nSp), scC(c.nSp);
        const int rcG = xcorr_metal_compute(w.data(), t.data(), c.nSp, c.nCh, c.nS, c.maxShift,
                                            0.3f, shG.data(), scG.data());
        xcorr_omp_compute(w.data(), t.data(), c.nSp, c.nCh, c.nS, c.maxShift, 0.3f,
                          shC.data(), scC.data());
        int bad = 0;
        for (int sp = 0; sp < c.nSp; ++sp)
            bad += shG[sp] != shC[sp] || std::memcmp(&scG[sp], &scC[sp], sizeof(float)) != 0;
        char name[160];
        std::snprintf(name, sizeof name,
                      "%d spikes x %d ch x %d samples, maxShift %d: Metal == OpenMP (%d differ)",
                      c.nSp, c.nCh, c.nS, c.maxShift, bad);
        check(rcG == 0 && bad == 0, name);
    }
    check(std::string(XcorrDispatch::backendName()) == "Metal (Apple GPU)",
          std::string("XcorrDispatch picks Metal (") + XcorrDispatch::backendName() + ")");
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASS", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
