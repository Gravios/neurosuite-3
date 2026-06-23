#include "decimate.h"

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace neuroscope {
namespace spectral {

namespace {

// Windowed-sinc low-pass FIR, odd length L, normalised-frequency cutoff fc
// (cycles/sample, 0..0.5), Blackman window, unity DC gain.
std::vector<double> designLowpass(double fc, int L)
{
    if (L % 2 == 0) ++L;            // force odd for a linear-phase symmetric tap
    std::vector<double> h(static_cast<std::size_t>(L));
    const int M = (L - 1) / 2;
    double sum = 0.0;
    for (int i = 0; i < L; ++i) {
        const int k = i - M;
        const double sinc = (k == 0) ? 2.0 * fc
                                     : std::sin(2.0 * M_PI * fc * k) / (M_PI * k);
        const double w = 0.42
                       - 0.50 * std::cos(2.0 * M_PI * i / (L - 1))
                       + 0.08 * std::cos(4.0 * M_PI * i / (L - 1));
        h[i] = sinc * w;
        sum += h[i];
    }
    if (sum != 0.0)
        for (double& v : h) v /= sum;   // normalise DC gain to 1
    return h;
}

// One half-band /2 stage: low-pass just below the post-decimation Nyquist
// (0.25 cycles/sample), then keep every second sample. The centred convolution
// compensates the FIR group delay so the output stays time-aligned.
void halfbandDecimate(const std::vector<double>& in, std::vector<double>& out)
{
    static const std::vector<double> h = designLowpass(0.23, 63);
    const int L = static_cast<int>(h.size());
    const int half = (L - 1) / 2;
    const int n = static_cast<int>(in.size());
    const int m = n / 2;
    out.assign(static_cast<std::size_t>(m), 0.0);
    for (int j = 0; j < m; ++j) {
        const int center = 2 * j;
        double acc = 0.0;
        for (int t = 0; t < L; ++t) {
            const int idx = center + half - t;
            if (idx >= 0 && idx < n) acc += h[t] * in[idx];
        }
        out[j] = acc;
    }
}

int roundDownPow2(int v)
{
    if (v < 1) return 1;
    int p = 1;
    while ((p << 1) <= v) p <<= 1;
    return p;
}

} // namespace

int decimationFactor(double fs, double freqHigh, double marginFactor,
                     int maxFactor, int n, int minOutLen)
{
    if (fs <= 0.0 || freqHigh <= 0.0) return 1;       // full band: no decimation
    if (marginFactor < 1.0) marginFactor = 1.0;
    const double edge = freqHigh * marginFactor;
    if (edge <= 0.0) return 1;
    int M = static_cast<int>(std::floor(fs / (2.0 * edge)));
    M = roundDownPow2(M);
    if (maxFactor >= 1) M = std::min(M, roundDownPow2(maxFactor));
    if (minOutLen > 0)
        while (M > 1 && n / M < minOutLen) M >>= 1;
    return std::max(1, M);
}

void decimate(const double* in, int n, int M, std::vector<double>& out)
{
    M = roundDownPow2(M);
    if (M <= 1 || n <= 0) {
        out.assign(in, in + std::max(0, n));
        return;
    }
    std::vector<double> cur(in, in + n);
    std::vector<double> next;
    while (M > 1 && static_cast<int>(cur.size()) >= 2) {
        halfbandDecimate(cur, next);
        cur.swap(next);
        M >>= 1;
    }
    out.swap(cur);
}

} // namespace spectral
} // namespace neuroscope
