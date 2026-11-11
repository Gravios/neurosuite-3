// supersample.hpp — super-sampling of spike windows (Klusters' super-sample mode).
//
// A super-sampled waveform space (custody token _U<factor>) holds each spike at
// <factor> ticks per acquisition sample: a window of nSamp raw samples becomes
// nSamp * factor ticks, tick k of the window standing at raw time
//
//      start + (k + phase) / factor
//
// where `start` is the window's first raw sample (timestamp - peakSample) and
// `phase` (0 .. factor-1, in ticks) a sub-sample offset a realignment in the
// super-sampled space may have given the spike.  The .res keeps the acquisition
// sample; the phase travels beside it (Klusters' .php sidecar), so a later re-cut
// of the same spike reproduces the same waveform.
//
// Two interpolators:
//   CubicSpline  natural cubic spline through the raw samples (the default): local,
//                no ringing, exact at the samples; a margin of raw samples on either
//                side of the window makes the edges as good as the middle.
//   Lanczos3     windowed sinc (a = 3): the band-limited answer, slightly sharper
//                peaks, mild ringing near clipped samples.
//
// Everything is header-only and allocation-light; the per-call scratch is the
// caller's (Scratch), so an extraction loop reuses it.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NEUROSUITE_SUPERSAMPLE_HPP
#define NEUROSUITE_SUPERSAMPLE_HPP

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace neurosuite::core {

enum class Interp { CubicSpline, Lanczos3 };

inline const char* interpName(Interp i)
{ return i == Interp::Lanczos3 ? "lanczos3" : "spline"; }

inline bool parseInterp(const std::string& s, Interp& out)
{
    if (s == "spline" || s == "cubic" || s == "cubic-spline") { out = Interp::CubicSpline; return true; }
    if (s == "lanczos" || s == "lanczos3" || s == "sinc")     { out = Interp::Lanczos3;    return true; }
    return false;
}

// Raw samples to hold on either side of the window for the edges to be exact:
// the spline wants two, Lanczos-3 three.  Three serves both.
inline constexpr int kSupersampleMargin = 3;

struct SupersampleScratch {
    std::vector<double> x, m, c, d, l, mu, z;
};

// Second-derivative coefficients of the natural cubic spline through x[0..n)
// at unit spacing (Burden & Faires); O(n).
inline void naturalSplineCoeffs(const double* x, int n, SupersampleScratch& s)
{
    s.m.assign(static_cast<std::size_t>(n), 0.0);
    if (n < 3) return;
    s.l.assign(static_cast<std::size_t>(n), 1.0);
    s.mu.assign(static_cast<std::size_t>(n), 0.0);
    s.z.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 1; i < n - 1; ++i) {
        const double alpha = 3.0 * (x[i + 1] - 2.0 * x[i] + x[i - 1]);
        s.l[i]  = 4.0 - s.mu[i - 1];
        s.mu[i] = 1.0 / s.l[i];
        s.z[i]  = (alpha - s.z[i - 1]) / s.l[i];
    }
    for (int j = n - 2; j >= 1; --j) s.m[j] = s.z[j] - s.mu[j] * s.m[j + 1];
}

// Spline value at fractional position t (0 <= t <= n-1); clamped outside.
inline double splineAt(const double* x, int n, const SupersampleScratch& s, double t)
{
    if (n == 1) return x[0];
    if (t <= 0.0) return x[0];
    if (t >= n - 1) return x[n - 1];
    int i = static_cast<int>(t);
    if (i >= n - 1) i = n - 2;
    const double u = t - i, v = 1.0 - u;
    const double mi = s.m[i], mj = s.m[i + 1];
    // cubic Hermite form of the natural spline with second derivatives m
    return v * x[i] + u * x[i + 1]
         + ((v * v * v - v) * mi + (u * u * u - u) * mj) / 6.0;
}

inline double lanczos3At(const double* x, int n, double t)
{
    if (n == 1) return x[0];
    const int lo = static_cast<int>(std::floor(t)) - 2, hi = static_cast<int>(std::floor(t)) + 3;
    double acc = 0.0, wsum = 0.0;
    for (int j = lo; j <= hi; ++j) {
        const double dt = t - j;
        double w;
        if (dt == 0.0) w = 1.0;
        else if (std::fabs(dt) >= 3.0) w = 0.0;
        else {
            const double p = M_PI * dt;
            w = 3.0 * std::sin(p) * std::sin(p / 3.0) / (p * p);
        }
        const int jj = std::clamp(j, 0, n - 1);        // edge: clamp
        acc += w * x[jj];
        wsum += w;
    }
    return wsum != 0.0 ? acc / wsum : x[std::clamp(static_cast<int>(std::lround(t)), 0, n - 1)];
}

// One channel: raw[0..nRaw) are consecutive raw samples whose index `margin`
// is the window's first sample.  Writes nOut ticks, tick k at raw position
// margin + (k + phaseTicks) / factor, into out[0..nOut).  nOut is the caller's
// (nSamp * factor for a window; one more, from tick -1, when a transform wants
// the sample before the window as its seed -- then pass firstTick = -1).
inline void supersampleChannel(const double* raw, int nRaw, int margin, int factor, int phaseTicks,
                               Interp interp, double* out, int nOut, int firstTick,
                               SupersampleScratch& s)
{
    if (interp == Interp::CubicSpline) naturalSplineCoeffs(raw, nRaw, s);
    const double inv = 1.0 / factor;
    for (int k = 0; k < nOut; ++k) {
        const double t = margin + (firstTick + k + phaseTicks) * inv;
        out[k] = interp == Interp::CubicSpline ? splineAt(raw, nRaw, s, t) : lanczos3At(raw, nRaw, t);
    }
}

inline std::int16_t roundToInt16(double v)
{
    const double r = std::nearbyint(v);
    if (r > 32767.0) return 32767;
    if (r < -32768.0) return -32768;
    return static_cast<std::int16_t>(r);
}

// A whole window, channel-major in and out: in[ch * nRaw + j] holds the raw
// samples of channel ch (nRaw = nSamp + 2 * margin), out[ch * nOut + k] the
// ticks (nOut = nSamp * factor).  Rounded and saturated to int16.
inline void supersampleWindow(const std::int16_t* in, int nChan, int nRaw, int margin,
                              int factor, int phaseTicks, Interp interp,
                              std::int16_t* out, int nOut, SupersampleScratch& s)
{
    s.x.resize(static_cast<std::size_t>(nRaw));
    s.c.resize(static_cast<std::size_t>(nOut));
    for (int ch = 0; ch < nChan; ++ch) {
        const std::int16_t* src = in + static_cast<std::size_t>(ch) * nRaw;
        for (int j = 0; j < nRaw; ++j) s.x[static_cast<std::size_t>(j)] = src[j];
        supersampleChannel(s.x.data(), nRaw, margin, factor, phaseTicks, interp, s.c.data(), nOut, 0, s);
        std::int16_t* dst = out + static_cast<std::size_t>(ch) * nOut;
        for (int k = 0; k < nOut; ++k) dst[k] = roundToInt16(s.c[static_cast<std::size_t>(k)]);
    }
}

// From a STORED window only (no recording at hand): the same, with margin 0 --
// the last factor-1 ticks beyond the final sample are clamped to it.
inline void supersampleStoredWindow(const std::int16_t* in, int nChan, int nSamp, int factor,
                                    Interp interp, std::int16_t* out, SupersampleScratch& s)
{ supersampleWindow(in, nChan, nSamp, 0, factor, 0, interp, out, nSamp * factor, s); }

// The inverse direction for a timestamp: a shift of `ticks` in the super-sampled
// space is `ticks / factor` raw samples -- `whole` of them, with `phase` ticks
// left over (0 <= phase < factor), so the .res moves by `whole` and the phase
// sidecar takes `phase`.
inline void splitTicks(int ticks, int factor, int& whole, int& phase)
{
    whole = ticks / factor;
    phase = ticks - whole * factor;
    if (phase < 0) { phase += factor; --whole; }
}

}  // namespace neurosuite::core

#endif  // NEUROSUITE_SUPERSAMPLE_HPP
