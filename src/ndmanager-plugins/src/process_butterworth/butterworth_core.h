/***************************************************************************
    butterworth_core.h  --  shared CPU / CUDA core of process_butterworth

    Design
    ------
    Digital Butterworth high-pass (and optional low-pass), designed in double
    precision by the bilinear transform with frequency pre-warping and factored
    into second-order sections (SOS).  Each section is normalised to unit gain in
    its passband (z = -1 for the high-pass, z = +1 for the low-pass), which keeps
    the intermediate values bounded in single precision.  A band-pass is the
    cascade of the high-pass and the low-pass sections.

    Zero phase
    ----------
    The signal is filtered forward, then backward (filtfilt), so the result has
    no phase distortion and the magnitude response is squared: -6 dB at each
    corner, effective order 2N.

    Block decomposition
    -------------------
    An IIR filter's memory decays; after M samples the contribution of the
    initial state is below a chosen tolerance.  Each (channel, block) of output
    samples [s, e) is therefore computed independently from the input segment
    [s-M, e+M): forward from a zero state starting at s-M (valid from s on),
    backward from a zero state starting at e+M (valid down to e).  M is derived
    from the cascade's impulse response so that the start-up error is below
    1e-3 LSB for any int16 input.  Blocks are independent, which gives the
    CPU (OpenMP) and GPU (one thread per channel x block) parallelism.

    File edges are handled by odd reflection about the end samples
    (2*x[0] - x[-k], 2*x[N-1] - x[2N-2-k]), as scipy's filtfilt does by default.

    copyright  (C) 2026 neurosuite-3 contributors
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.
 ***************************************************************************/
#ifndef BUTTERWORTH_CORE_H
#define BUTTERWORTH_CORE_H

#ifdef __CUDACC__
#  define BW_HD __host__ __device__
#else
#  define BW_HD
#endif

#define BW_MAX_SECTIONS 16

// Second-order sections in precision T: y = b0 x + z1;  z1 = b1 x - a1 y + z2;
// z2 = b2 x - a2 y   (direct form II transposed).
template <typename T>
struct BwCoefs {
    int nSec;
    T b0[BW_MAX_SECTIONS], b1[BW_MAX_SECTIONS], b2[BW_MAX_SECTIONS];
    T a1[BW_MAX_SECTIONS], a2[BW_MAX_SECTIONS];
};

// Value of global row g of channel ch, odd-reflected at the file edges.
// chunk holds rows [row0, row0 + rows) of an nCh-interleaved int16 recording of
// nRows rows in total.
template <typename T>
BW_HD inline T bwFetch(const short* chunk, long long row0, long long nRows,
                       int nCh, int ch, long long g)
{
    if (g < 0) {
        const T x0 = (T)chunk[(0 - row0) * nCh + ch];
        const T xr = (T)chunk[(-g - row0) * nCh + ch];
        return (T)2 * x0 - xr;
    }
    if (g >= nRows) {
        const T xn = (T)chunk[(nRows - 1 - row0) * nCh + ch];
        const T xr = (T)chunk[(2 * (nRows - 1) - g - row0) * nCh + ch];
        return (T)2 * xn - xr;
    }
    return (T)chunk[(g - row0) * nCh + ch];
}

template <typename T>
BW_HD inline short bwRoundSat(T y)
{
    T r = y >= (T)0 ? (T)(long long)(y + (T)0.5) : (T)(long long)(y - (T)0.5);
    if (r > (T)32767)  r = (T)32767;
    if (r < (T)-32768) r = (T)-32768;
    return (short)r;
}

// Zero-phase filter of output rows [s, e) of channel ch.
//   scratch: L = (e - s) + 2*M elements of T, element i at scratch[i * stride]
//   out:     nCh-interleaved int16, row r stored at out[(r - outRow0) * nCh + ch]
template <typename T>
BW_HD void bwFilterBlock(const short* chunk, long long row0, long long nRows,
                         int nCh, int ch, long long s, long long e, int M,
                         const BwCoefs<T>& c, T* scratch, long long stride,
                         short* out, long long outRow0)
{
    const long long L = (e - s) + 2LL * M;
    T z1[BW_MAX_SECTIONS], z2[BW_MAX_SECTIONS];

    // forward pass from a zero state at s - M
    for (int k = 0; k < c.nSec; ++k) { z1[k] = (T)0; z2[k] = (T)0; }
    for (long long i = 0; i < L; ++i) {
        T v = bwFetch<T>(chunk, row0, nRows, nCh, ch, s - M + i);
        for (int k = 0; k < c.nSec; ++k) {
            const T y = c.b0[k] * v + z1[k];
            z1[k] = c.b1[k] * v - c.a1[k] * y + z2[k];
            z2[k] = c.b2[k] * v - c.a2[k] * y;
            v = y;
        }
        scratch[i * stride] = v;
    }
    // backward pass from a zero state at e + M
    for (int k = 0; k < c.nSec; ++k) { z1[k] = (T)0; z2[k] = (T)0; }
    for (long long i = L - 1; i >= 0; --i) {
        T v = scratch[i * stride];
        for (int k = 0; k < c.nSec; ++k) {
            const T y = c.b0[k] * v + z1[k];
            z1[k] = c.b1[k] * v - c.a1[k] * y + z2[k];
            z2[k] = c.b2[k] * v - c.a2[k] * y;
            v = y;
        }
        if (i >= M && i < M + (e - s))
            out[(s + (i - M) - outRow0) * nCh + ch] = bwRoundSat<T>(v);
    }
}

#ifndef __CUDACC__
// ---------------------------------------------------------------------------
// Host-only design helpers (double precision)
// ---------------------------------------------------------------------------
#include <cmath>
#include <complex>
#include <vector>

struct BwSection { double b0, b1, b2, a1, a2; };

// Butterworth sections of order N at corner fc (Hz), sampling rate fs.
// highpass = true: zeros at z = 1, unit gain at Nyquist; false: zeros at z = -1,
// unit gain at DC.
inline std::vector<BwSection> bwDesign(int N, double fc, double fs, bool highpass)
{
    typedef std::complex<double> C;
    const double pi = 3.14159265358979323846;
    const double wc = 2.0 * fs * std::tan(pi * fc / fs);       // pre-warped
    std::vector<BwSection> sos;
    auto bilinear = [&](C s) { return (2.0 * fs + s) / (2.0 * fs - s); };
    for (int k = 0; k < N / 2; ++k) {                           // conjugate pairs
        const C p = std::polar(1.0, pi * (2.0 * k + N + 1) / (2.0 * N));
        const C sp = highpass ? C(wc) / p : wc * p;
        const C zp = bilinear(sp);
        BwSection q;
        q.a1 = -2.0 * zp.real();
        q.a2 = std::norm(zp);
        q.b0 = 1.0; q.b1 = highpass ? -2.0 : 2.0; q.b2 = 1.0;
        sos.push_back(q);
    }
    if (N % 2) {                                                 // real pole
        const C sp = highpass ? C(wc) / C(-1.0) : C(-wc);
        const double r = bilinear(sp).real();
        BwSection q;
        q.a1 = -r; q.a2 = 0.0;
        q.b0 = 1.0; q.b1 = highpass ? -1.0 : 1.0; q.b2 = 0.0;
        sos.push_back(q);
    }
    for (auto& q : sos) {                                        // unit passband gain
        const double z = highpass ? -1.0 : 1.0;
        const double g = (q.b0 + q.b1 * z + q.b2 * z * z) / (1.0 + q.a1 * z + q.a2 * z * z);
        q.b0 /= g; q.b1 /= g; q.b2 /= g;
    }
    return sos;
}

// Smallest M with  sum_{n>=M} |h[n]| * 32768 * sum|h|  <  1e-3 :  the zero-state
// start-up error of either pass, for any int16 input, is then below 1e-3 LSB.
inline int bwMargin(const std::vector<BwSection>& sos, int maxM = 1 << 22)
{
    std::vector<double> h;
    std::vector<double> z1(sos.size(), 0.0), z2(sos.size(), 0.0);
    for (int n = 0; n < maxM; ++n) {
        double v = (n == 0) ? 1.0 : 0.0;
        for (size_t k = 0; k < sos.size(); ++k) {
            const BwSection& q = sos[k];
            const double y = q.b0 * v + z1[k];
            z1[k] = q.b1 * v - q.a1 * y + z2[k];
            z2[k] = q.b2 * v - q.a2 * y;
            v = y;
        }
        h.push_back(v);
        // stop once the state itself is negligible (far below the tolerance)
        double st = 0.0;
        for (size_t k = 0; k < sos.size(); ++k) st += std::fabs(z1[k]) + std::fabs(z2[k]);
        if (n > 8 && st < 1e-18) break;
    }
    double total = 0.0;
    for (double v : h) total += std::fabs(v);
    const double tol = 1e-3 / (32768.0 * (total > 1.0 ? total : 1.0));
    double tail = 0.0;
    int M = (int)h.size();
    for (int n = (int)h.size() - 1; n >= 0; --n) {
        tail += std::fabs(h[n]);
        if (tail >= tol) { M = n + 1; break; }
    }
    return M;
}

template <typename T>
inline BwCoefs<T> bwToCoefs(const std::vector<BwSection>& sos)
{
    BwCoefs<T> c;
    c.nSec = (int)sos.size();
    for (int k = 0; k < c.nSec; ++k) {
        c.b0[k] = (T)sos[k].b0; c.b1[k] = (T)sos[k].b1; c.b2[k] = (T)sos[k].b2;
        c.a1[k] = (T)sos[k].a1; c.a2[k] = (T)sos[k].a2;
    }
    return c;
}
#endif  // !__CUDACC__

#endif  // BUTTERWORTH_CORE_H
