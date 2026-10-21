// process_spikegrouper's Metal helpers (process_spikegrouper_metal.cpp)
// against the CPU path's rules, exactly: the noise median is the element of
// rank n/2 of abs((int)x) (medianSigma), and a coincidence count is the
// number of events of channel i with an event of channel j within +-window
// (buildCoincidenceMatrix).  Interleaved data with more channels than the
// group, int16 extremes, empty event lists, several window sizes.  Prints
// SKIPPED (exit 0) with no Metal device; registered as
// ndm_test_spikegrouper_metal.
#include "process_spikegrouper_metal.h"

#include <neurosuite/gpu/metalcontext.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* name, long got = 0, long want = 0)
{
    if (ok) std::printf("  PASS  %s\n", name);
    else { std::printf("  FAIL  %s   (got %ld, want %ld)\n", name, got, want); ++failures; }
}
} // namespace

int main()
{
    if (!neurosuite::gpu::MetalContext::instance()) {
        std::printf("SKIPPED: no Metal device on this machine\n");
        return 0;
    }
    std::mt19937 rng(17);

    // Medians: 16-channel interleaved data, a 6-channel group out of order.
    for (int mode = 0; mode < 2; ++mode) {
        const long n = mode ? 30001 : 200000;
        const int nChannels = 16;
        const std::vector<int> chs{3, 0, 15, 7, 8, 1};
        std::vector<short> data(static_cast<std::size_t>(n) * nChannels);
        std::normal_distribution<double> g(0.0, 150.0);
        for (short& v : data)
            v = mode ? ((rng() % 3) ? short(-32768) : short(32767))
                     : static_cast<short>(std::clamp(g(rng), -32768.0, 32767.0));
        std::vector<int> gpu;
        const bool ran = metalAbsMedians(data.data(), n, nChannels, chs.data(),
                                         static_cast<int>(chs.size()), gpu, false);
        check(ran, mode ? "medians run (int16 extremes)" : "medians run (200000 samples)");
        if (!ran) continue;
        int bad = 0;
        long firstGot = 0, firstWant = 0;
        for (std::size_t k = 0; k < chs.size(); ++k) {
            std::vector<int> a(static_cast<std::size_t>(n));
            for (long s = 0; s < n; ++s) a[s] = std::abs(static_cast<int>(data[s * nChannels + chs[k]]));
            std::nth_element(a.begin(), a.begin() + n / 2, a.end());
            if (gpu[k] != a[n / 2] && bad++ == 0) { firstGot = gpu[k]; firstWant = a[n / 2]; }
        }
        check(bad == 0, "every channel's median equals the CPU's", firstGot, firstWant);
    }

    // Coincidence counts.
    for (int window : {1, 12, 400}) {
        const int nv = 9;
        std::vector<std::vector<long>> events(nv);
        for (int c = 0; c < nv; ++c) {
            if (c == 4) continue;                          // an empty list
            long t = 0;
            for (int k = 0; k < 500 + 300 * c; ++k) { t += 1 + rng() % 60; events[c].push_back(t); }
        }
        std::vector<long> gpu;
        const bool ran = metalCoincidenceCounts(events, window, gpu);
        char name[96];
        std::snprintf(name, sizeof name, "coincidence counts run (window %d)", window);
        check(ran, name);
        if (!ran) continue;
        int bad = 0;
        long firstGot = 0, firstWant = 0;
        for (int i = 0; i < nv; ++i)
            for (int j = 0; j < nv; ++j) {
                long want = 0;
                if (i != j && !events[i].empty() && !events[j].empty())
                    for (long t : events[i]) {
                        auto it = std::lower_bound(events[j].begin(), events[j].end(), t - window);
                        if (it != events[j].end() && *it <= t + window) ++want;
                    }
                const long got = gpu[static_cast<std::size_t>(i) * nv + j];
                if (got != want && bad++ == 0) { firstGot = got; firstWant = want; }
            }
        std::snprintf(name, sizeof name, "every count equals the CPU's (window %d)", window);
        check(bad == 0, name, firstGot, firstWant);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
