// process_medianthreshold's Metal path (process_medianthreshold_metal.cpp)
// against the CPU path's definition of the median (medianOf: middle value,
// or the mean of the two middle values) on data put through the program's
// in-place |x|.  Exact equality is required.  Covers odd and even frame
// counts, -32768 and 32767, constant channels, a single frame, many
// channels, and data split over many staging chunks.  Prints SKIPPED (exit
// 0) with no Metal device; registered as ndm_test_medianthreshold_metal.
#include "process_medianthreshold_metal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include <neurosuite/gpu/metalcontext.h>

namespace {

int failures = 0;

void check(bool ok, const char* name, double got = 0, double want = 0)
{
    if (ok) std::printf("  PASS  %s\n", name);
    else { std::printf("  FAIL  %s   (got %.10Lg, want %.10Lg)\n", name,
                       static_cast<long double>(got), static_cast<long double>(want)); ++failures; }
}

// As process_medianthreshold.cpp: input[i] = abs(input[i]) on a short.
short programAbs(short v) { return static_cast<short>(std::abs(v)); }

// As process_medianthreshold.cpp's medianOf().
long double cpuMedian(std::vector<short> v)
{
    const long long n = static_cast<long long>(v.size());
    const long long mid = n / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    if (n % 2 == 1) return v[mid];
    const short upper = v[mid];
    std::nth_element(v.begin(), v.begin() + mid - 1, v.end());
    return (v[mid - 1] + upper) / 2.0L;
}

void runCase(const char* name, long long frames, int nChannels, unsigned seed,
             std::size_t chunkBytes, int mode)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, 120.0);
    std::vector<short> data(static_cast<std::size_t>(frames * nChannels));
    for (long long f = 0; f < frames; ++f)
        for (int c = 0; c < nChannels; ++c) {
            short v;
            if (mode == 1) v = static_cast<short>(c - 3);                 // constant channels
            else if (mode == 2) v = (rng() % 4 == 0) ? short(-32768)      // clipped extremes
                                  : (rng() % 3 == 0) ? short(32767)
                                  : static_cast<short>(noise(rng));
            else v = static_cast<short>(std::clamp(noise(rng) * (1 + c % 5), -32768.0, 32767.0));
            data[static_cast<std::size_t>(f * nChannels + c)] = programAbs(v);
        }

    std::vector<long double> gpu;
    const bool ran = metalChannelMedians(data.data(), frames, nChannels, gpu, false, chunkBytes);
    char label[160];
    std::snprintf(label, sizeof label, "%s: the Metal path runs", name);
    check(ran && gpu.size() == static_cast<std::size_t>(nChannels), label);
    if (!ran) return;

    int mismatches = 0;
    long double firstGot = 0, firstWant = 0;
    for (int c = 0; c < nChannels; ++c) {
        std::vector<short> ch(static_cast<std::size_t>(frames));
        for (long long f = 0; f < frames; ++f) ch[f] = data[static_cast<std::size_t>(f * nChannels + c)];
        const long double want = cpuMedian(ch);
        if (gpu[c] != want && mismatches++ == 0) { firstGot = gpu[c]; firstWant = want; }
    }
    std::snprintf(label, sizeof label, "%s: every channel's median equals the CPU's", name);
    check(mismatches == 0, label, static_cast<double>(firstGot), static_cast<double>(firstWant));
}

} // namespace

int main()
{
    if (!neurosuite::gpu::MetalContext::instance()) {
        std::printf("SKIPPED: no Metal device on this machine\n");
        return 0;
    }
    const std::size_t big = std::size_t(64) << 20;
    runCase("odd frames, 32 channels",           100001, 32, 1u, big, 0);
    runCase("even frames, 32 channels",          100000, 32, 2u, big, 0);
    runCase("-32768 and 32767, even frames",      50000,  7, 3u, big, 2);
    runCase("-32768 and 32767, odd frames",       50001,  7, 4u, big, 2);
    runCase("constant channels",                  10000,  6, 5u, big, 1);
    runCase("one frame",                              1,  5, 6u, big, 0);
    runCase("two frames",                             2,  5, 7u, big, 0);
    runCase("many channels (384)",                 4001, 384, 8u, big, 0);
    runCase("many small chunks (4 KiB staging)",  20011, 13, 9u, 4096, 0);
    runCase("chunk smaller than a frame",           999, 64, 10u, 16, 0);

    // Timing (not a check): a 64-channel, 10-minute 20 kHz recording.
    {
        const long long frames = 12000000;
        const int nch = 64;
        std::vector<short> data(static_cast<std::size_t>(frames * nch));
        std::mt19937 rng(11);
        for (short& v : data) v = programAbs(static_cast<short>(int(rng() % 2001) - 1000));
        std::vector<long double> med;
        const auto t0 = std::chrono::steady_clock::now();
        metalChannelMedians(data.data(), frames, nch, med, false);
        const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  ....  timing  Metal medians, %d ch x %lld samples: %.3f s\n", nch, frames, s);
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
