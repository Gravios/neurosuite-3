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
// super-sampled space may have given the spike.  The shared .res keeps the
// acquisition sample; the phase travels beside it as the space's last feature
// column, and the space's own .res.<token>_U<factor>.N holds the sum,
// sample * factor + phase, in ticks (custody.hpp baseRateToken), so a later
// re-cut of the same spike reproduces the same waveform.
//
// Three interpolators:
//   SmoothSpline natural cubic spline through SMOOTHED samples (the default): the
//                samples are first passed through a Whittaker smoother (penalised
//                second differences, weight kSmoothLambda), which is a low-pass of
//                about 0.6 Nyquist -- the spike (1-5 kHz at 32 kHz) is untouched,
//                the sample-to-sample noise an exact interpolant would reproduce
//                between the samples is not.  Not exact at the samples (within the
//                noise); smooth derivative.
//   CubicSpline  natural cubic spline through the raw samples: local, no ringing,
//                exact at the samples; a margin of raw samples on either side of the
//                window makes the edges as good as the middle.
//   Lanczos3     windowed sinc (a = 3): the band-limited answer, slightly sharper
//                peaks, mild ringing near clipped samples.
// Each has a derivative (per raw sample): the stderiv of a super-sampled window
// takes its temporal part from the interpolant's derivative (spike_extract.hpp),
// not from the difference of two rounded ticks -- at x4 that difference is a
// quarter of the base-rate one with the same rounding error, which is what made
// the _U stderiv waveforms jagged.
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

enum class Interp { CubicSpline, Lanczos3, SmoothSpline };

inline const char* interpName(Interp i)
{ return i == Interp::Lanczos3 ? "lanczos3" : i == Interp::SmoothSpline ? "smooth" : "spline"; }

inline bool parseInterp(const std::string& s, Interp& out)
{
    if (s == "spline" || s == "cubic" || s == "cubic-spline")        { out = Interp::CubicSpline;  return true; }
    if (s == "lanczos" || s == "lanczos3" || s == "sinc")            { out = Interp::Lanczos3;     return true; }
    if (s == "smooth" || s == "smooth-spline" || s == "smoothing")   { out = Interp::SmoothSpline; return true; }
    return false;
}

// The Whittaker smoother's roughness weight (unit sample spacing): the response
// to a sinusoid of angular frequency w is 1 / (1 + lambda (2 - 2 cos w)^2), half
// power at about 0.6 Nyquist for 0.15 (9.6 kHz at 32 kHz; a 0.3 ms spike trough
// lives below 3 kHz and loses < 1 % of its depth).
inline constexpr double kSmoothLambda = 0.15;

// Raw samples to hold on either side of the window for the edges to be exact:
// the spline wants two, Lanczos-3 three.  Three serves both.
inline constexpr int kSupersampleMargin = 3;

struct SupersampleScratch {
    std::vector<double> x, m, c, d, l, mu, z;
    std::vector<double> f;                       // the smoothed samples (SmoothSpline)
    std::vector<double> a, b, e, g;              // the pentadiagonal solve's work
};

// Whittaker smoother at unit spacing: f minimising sum (y - f)^2 + lambda * sum (d2 f)^2,
// i.e. (I + lambda D'D) f = y with D the second-difference operator -- a symmetric
// pentadiagonal system, solved by a banded Cholesky in O(n).  n < 3: f = y.
inline void whittakerSmooth(const double* y, int n, double lambda, double* f, SupersampleScratch& s)
{
    if (n < 3 || lambda <= 0.0) { for (int i = 0; i < n; ++i) f[i] = y[i]; return; }
    // A = I + lambda D'D: diagonal a, first off-diagonal b, second off-diagonal e (constant lambda)
    s.a.assign(static_cast<std::size_t>(n), 0.0); s.b.assign(static_cast<std::size_t>(n), 0.0);
    s.e.assign(static_cast<std::size_t>(n), 0.0); s.g.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        // row i of D'D: the second-difference rows that touch i are those centred at i-1, i, i+1 (inside 1..n-2)
        double diag = 0.0, off1 = 0.0, off2 = 0.0;
        for (int c = i - 1; c <= i + 1; ++c) {                 // rows of D centred at c
            if (c < 1 || c > n - 2) continue;
            const double wi = (c == i) ? -2.0 : 1.0;           // D[c][i]
            diag += wi * wi;
            if (i + 1 <= n - 1) { const double wj = (c == i + 1) ? -2.0 : (std::abs(c - (i + 1)) == 1 ? 1.0 : 0.0); off1 += wi * wj; }
            if (i + 2 <= n - 1) { const double wj = (c == i + 2) ? -2.0 : (std::abs(c - (i + 2)) == 1 ? 1.0 : 0.0); off2 += wi * wj; }
        }
        s.a[static_cast<std::size_t>(i)] = 1.0 + lambda * diag;
        s.b[static_cast<std::size_t>(i)] = lambda * off1;      // A[i][i+1]
        s.e[static_cast<std::size_t>(i)] = lambda * off2;      // A[i][i+2]
    }
    // Banded Cholesky A = L D L' with L unit lower, bandwidth 2 (l1 on the first, l2 on the second sub-diagonal)
    std::vector<double>& d  = s.d;  d.assign(static_cast<std::size_t>(n), 0.0);
    std::vector<double>& l1 = s.l;  l1.assign(static_cast<std::size_t>(n), 0.0);
    std::vector<double>& l2 = s.mu; l2.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        double di = s.a[static_cast<std::size_t>(i)];
        if (i >= 1) di -= l1[static_cast<std::size_t>(i - 1)] * l1[static_cast<std::size_t>(i - 1)] * d[static_cast<std::size_t>(i - 1)];
        if (i >= 2) di -= l2[static_cast<std::size_t>(i - 2)] * l2[static_cast<std::size_t>(i - 2)] * d[static_cast<std::size_t>(i - 2)];
        d[static_cast<std::size_t>(i)] = di;
        if (i + 1 < n) {
            double v = s.b[static_cast<std::size_t>(i)];
            if (i >= 1) v -= l2[static_cast<std::size_t>(i - 1)] * l1[static_cast<std::size_t>(i - 1)] * d[static_cast<std::size_t>(i - 1)];
            l1[static_cast<std::size_t>(i)] = v / di;
        }
        if (i + 2 < n) l2[static_cast<std::size_t>(i)] = s.e[static_cast<std::size_t>(i)] / di;
    }
    // forward: L z = y ; diagonal: w = z / d ; back: L' f = w
    std::vector<double>& z = s.z; z.assign(static_cast<std::size_t>(n), 0.0);
    for (int i = 0; i < n; ++i) {
        double v = y[i];
        if (i >= 1) v -= l1[static_cast<std::size_t>(i - 1)] * z[static_cast<std::size_t>(i - 1)];
        if (i >= 2) v -= l2[static_cast<std::size_t>(i - 2)] * z[static_cast<std::size_t>(i - 2)];
        z[static_cast<std::size_t>(i)] = v;
    }
    for (int i = n - 1; i >= 0; --i) {
        double v = z[static_cast<std::size_t>(i)] / d[static_cast<std::size_t>(i)];
        if (i + 1 < n) v -= l1[static_cast<std::size_t>(i)] * f[i + 1];
        if (i + 2 < n) v -= l2[static_cast<std::size_t>(i)] * f[i + 2];
        f[i] = v;
    }
}

// Second derivatives M_j of the natural cubic spline through x[0..n) at unit
// spacing (Burden & Faires' tridiagonal solve gives c_j = M_j / 2; the Hermite
// form in splineAt wants M itself -- until 0965 the half was used, which made
// the curve between samples a cross between the spline and the chord: ~1 % off
// on a spike trough and visibly kinked); O(n).
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
    for (int j = 1; j < n - 1; ++j) s.m[j] *= 2.0;            // c_j -> M_j
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

// Derivative of the spline (per unit of t); clamped flat outside.
inline double splineDerivAt(const double* x, int n, const SupersampleScratch& s, double t)
{
    if (n < 2 || t <= 0.0 || t >= n - 1) return 0.0;
    int i = static_cast<int>(t);
    if (i >= n - 1) i = n - 2;
    const double u = t - i, v = 1.0 - u;
    const double mi = s.m[i], mj = s.m[i + 1];
    return x[i + 1] - x[i] + ((1.0 - 3.0 * v * v) * mi + (3.0 * u * u - 1.0) * mj) / 6.0;
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
// `deriv`, when given, receives the interpolant's derivative at each tick, per RAW
// sample (so a tick-to-tick slope is deriv / factor).
inline void supersampleChannel(const double* raw, int nRaw, int margin, int factor, int phaseTicks,
                               Interp interp, double* out, int nOut, int firstTick,
                               SupersampleScratch& s, double* deriv = nullptr)
{
    const double* x = raw;
    if (interp == Interp::SmoothSpline) {
        s.f.resize(static_cast<std::size_t>(nRaw));
        whittakerSmooth(raw, nRaw, kSmoothLambda, s.f.data(), s);
        x = s.f.data();
    }
    if (interp != Interp::Lanczos3) naturalSplineCoeffs(x, nRaw, s);
    const double inv = 1.0 / factor;
    for (int k = 0; k < nOut; ++k) {
        const double t = margin + (firstTick + k + phaseTicks) * inv;
        if (interp == Interp::Lanczos3) {
            out[k] = lanczos3At(x, nRaw, t);
            if (deriv) {                                     // central difference of the kernel
                const double h = 0.05;
                deriv[k] = (lanczos3At(x, nRaw, t + h) - lanczos3At(x, nRaw, t - h)) / (2.0 * h);
            }
        } else {
            out[k] = splineAt(x, nRaw, s, t);
            if (deriv) deriv[k] = splineDerivAt(x, nRaw, s, t);
        }
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
