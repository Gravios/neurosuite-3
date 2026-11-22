/* waveformquality_test.cpp -- standalone test of waveformquality.h.
 * No framework: exits 0 on success, non-zero on a failure.
 *
 *   g++ -std=c++20 -O2 -I src/klusters/src src/klusters/test/waveformquality_test.cpp -o wq && ./wq
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "waveformquality.h"

#include <cstdint>
#include <cstdio>
#include <random>

static int g_fail = 0;
#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL: %s  (%s:%d)\n", (msg), __FILE__, __LINE__);     \
            ++g_fail;                                                          \
        }                                                                      \
    } while (0)

namespace {
constexpr int kCh = 4, kSamp = 40, kPeak = 20;
using waveformquality::Grade;

/** One spike appended to @p wav (sample after sample, channel after channel): a trough of
 *  the given width at kPeak, per-channel gains, scaled by @p amp, plus white noise. */
void addSpike(std::vector<float>& wav, std::mt19937& rng, const double gain[kCh], double width, double amp, double noise)
{
    std::normal_distribution<double> g(0.0, noise);
    for (int s = 0; s < kSamp; ++s)
        for (int c = 0; c < kCh; ++c) {
            const double x = (s - kPeak) / width;
            wav.push_back(static_cast<float>(amp * gain[c] * -std::exp(-0.5 * x * x) + g(rng)));
        }
}

const double kGainA[kCh] = { 400.0, 900.0, 500.0, 150.0 };
const double kGainB[kCh] = { 900.0, 350.0, 150.0, 500.0 };

double value(const std::vector<float>& wav)
{
    return waveformquality::normalisedResidualVariance(wav.data(), static_cast<long>(wav.size() / (kSamp * kCh)), kSamp, kCh);
}
} // namespace

int main()
{
    std::mt19937 rng(11);

    // One unit, constant amplitude: what is left about the median is the background.
    std::vector<float> pure;
    for (int i = 0; i < 400; ++i) addSpike(pure, rng, kGainA, 2.0, 1.0, 40.0);
    const double vPure = value(pure);
    CHECK(std::isfinite(vPure) && std::fabs(vPure) < 0.02, "one unit at constant amplitude reads about 0");

    // The same unit with a 20 % spread of amplitude: the squared spread, about 0.04.
    std::vector<float> spread;
    std::normal_distribution<double> amp(1.0, 0.2);
    for (int i = 0; i < 2000; ++i) addSpike(spread, rng, kGainA, 2.0, amp(rng), 40.0);
    const double vSpread = value(spread);
    CHECK(vSpread > 0.03 && vSpread < 0.055, "a 20 % amplitude spread reads about 0.04");

    // A second unit mixed in: more of it reads higher, and far above the unit alone.
    double last = vPure;
    for (int share : { 5, 15, 30 }) {
        std::vector<float> mix;
        for (int i = 0; i < 1000; ++i) addSpike(mix, rng, (i % 100) < share ? kGainB : kGainA, 2.0, 1.0, 40.0);
        const double v = value(mix);
        CHECK(v > last + 0.02, "a larger share of a second unit reads higher");
        last = v;
    }
    CHECK(last > 0.15, "30 % of a second unit reads far above the unit alone");

    // The value does not depend on the gain of the recording, nor on the integer type.
    std::vector<float> scaled(spread.size());
    std::vector<std::int16_t> ints(spread.size());
    std::vector<float> rounded(spread.size());
    for (size_t i = 0; i < spread.size(); ++i) {
        scaled[i] = 3.0f * spread[i];
        ints[i] = static_cast<std::int16_t>(std::lrint(spread[i]));
        rounded[i] = static_cast<float>(ints[i]);
    }
    CHECK(std::fabs(value(scaled) - vSpread) < 1e-6, "scaling the recording leaves the value unchanged");
    const double vInt = waveformquality::normalisedResidualVariance(ints.data(), 2000, kSamp, kCh);
    CHECK(std::fabs(vInt - value(rounded)) < 1e-12, "int16 and float spikes of the same values agree");

    // An even number of spikes takes the mean of the two middle values (numpy's median):
    // two points, values 0..5 at the one that is not an edge sample.
    {
        const int nSamp = 5, nChan = 1; const long n = 30;       // edge = 2: samples 0, 1, 3, 4 are background
        std::vector<double> w(static_cast<size_t>(n * nSamp), 0.0);
        for (long k = 0; k < n; ++k) w[static_cast<size_t>(k * nSamp + 2)] = (k < 15) ? 10.0 : 20.0;
        // median 15, residual variance 25, no background, energy 225
        const double v = waveformquality::normalisedResidualVariance(w.data(), n, nSamp, nChan);
        CHECK(std::fabs(v - 25.0 / 225.0) < 1e-12, "the median of an even count is the mean of the two middle values");
    }

    // No value: too few spikes, a window too short, a flat median.
    std::vector<float> few(pure.begin(), pure.begin() + 29 * kSamp * kCh);
    CHECK(std::isnan(value(few)), "fewer than 30 spikes: no value");
    CHECK(std::isnan(waveformquality::normalisedResidualVariance(pure.data(), 400, 4, kCh)), "a window of 4 samples: no value");
    std::vector<float> flat(static_cast<size_t>(100 * kSamp * kCh), 0.0f);
    CHECK(std::isnan(value(flat)), "a flat median: no value");
    CHECK(std::isnan(waveformquality::normalisedResidualVariance<float>(nullptr, 400, kSamp, kCh)), "no spikes: no value");

    // The colours.
    CHECK(waveformquality::grade(0.05, 0.12, 0.27, 0.52) == Grade::Green,  "below the first threshold: green");
    CHECK(waveformquality::grade(0.12, 0.12, 0.27, 0.52) == Grade::Yellow, "at the first threshold: yellow");
    CHECK(waveformquality::grade(0.30, 0.12, 0.27, 0.52) == Grade::Orange, "between the second and third: orange");
    CHECK(waveformquality::grade(0.52, 0.12, 0.27, 0.52) == Grade::Red,    "at the third threshold: red");
    CHECK(waveformquality::grade(-0.4, 0.12, 0.27, 0.52) == Grade::Green,  "a negative value: green");
    CHECK(waveformquality::grade(std::nan(""), 0.12, 0.27, 0.52) == Grade::Unknown, "no value: no colour");

    if (g_fail) { std::printf("%d check(s) failed\n", g_fail); return 1; }
    std::printf("waveformquality: all checks passed\n");
    return 0;
}
