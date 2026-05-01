/***************************************************************************
                     process_extractemg.cpp
                     ----------------------
    EMG component identification, removal and event extraction for
    multichannel extracellular .dat recordings.

    copyright            : (C) 2026 neurosuite-3 contributors

 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/**
 * @file process_extractemg.cpp
 * @brief Identify, subtract and detect EMG events on a multichannel .dat.
 *
 * Pipeline summary
 * ----------------
 * Per spikeDetection group:
 *
 *   PASS 1  (small subsample, in-memory)
 *     - read up to noiseDurationSec seconds of dat starting at noiseStartSec
 *     - per-channel mean removal
 *     - 4-th order Butterworth high-pass at hpFreq
 *     - covariance over group channels
 *     - Jacobi eigendecomposition → leading eigenvector  ( As, unit-norm )
 *     - project subsample onto As to get EMG_au[t]
 *     - threshold  =  thresholdFactor * MAD(|EMG_au|)
 *
 *   PASS 2  (streaming, full file, chunked)
 *     - re-read original .dat in chunks of chunkSamples samples-per-channel
 *     - apply HP filter (group channels only) with state carried between
 *       chunks for biquad continuity
 *     - project the *un*-filtered (only de-meaned) signal onto As
 *           EMG_au[t]  =  Σc  ( raw[t,c] - μ[c] ) * As[c]
 *     - subtract from raw:
 *           clean[t,c] = raw[t,c] - As[c] * EMG_au[t]
 *     - write multichannel clean[] to <session>-emgclean.dat
 *     - write 1-channel int16( EMG_au * scale )  to <session>.emg.N
 *     - smooth |EMG_au| with a boxcar of envelopeWindowMs and detect peaks
 *       above threshold separated by at least refractorySamples
 *
 *   PASS 3  (waveform extraction, mmap-based)
 *     - mmap the original .dat
 *     - for each event time, extract a spikeLength-sample window of the
 *       group's channels into <session>.spk.emg.N
 *
 *   FINALLY
 *     - write timestamps to <session>.res.emg.N  (int64-LE, sample index)
 *     - append per-group entry to <session>.emg.meta.yaml
 *
 * Channels not in any group pass through unchanged into the cleaned dat.
 * Multiple groups overlap-share input but write to disjoint columns of the
 * cleaned dat, so the order of group processing does not matter — except
 * that we read the original (not the cleaned) dat for every group, so EMG
 * subtractions across groups are all relative to the raw signal.  This is
 * the same convention used by the MATLAB toolbox.
 *
 * File formats (binary, little-endian, no header except where noted)
 * ------------------------------------------------------------------
 *   <session>.dat                  N_samples × nChannels × int16 (raw)
 *   <session>-emgclean.dat         same shape, EMG-subtracted
 *   <session>.emg.N                N_samples × int16 (EMG_au scaled)
 *   <session>.res.emg.N            N_events × int64    (sample index)
 *   <session>.spk.emg.N            N_events × spikeLength × nChan(g) × int16
 *   <session>.emg.meta.yaml        text sidecar, one record per group
 */

#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64

#include "process_extractemg.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

static const char *programVersion = "process_extractemg 1.0 (2026)";

// =============================================================================
//  Butterworth 4-th order high-pass
// =============================================================================
//
// Cascade two 2-nd order RBJ-cookbook biquads with the analytic Q values
// of a 4-th order Butterworth (Q1 = 1/(2 sin(π/8)) ≈ 1.3066,
//                              Q2 = 1/(2 sin(3π/8)) ≈ 0.5412).
// Each section uses transposed direct form II for numerical stability,
// keeping its own (z1, z2) memory across step() calls.
// =============================================================================

void Butter4HP::reset()
{
    z1_[0] = z2_[0] = 0.0;
    z1_[1] = z2_[1] = 0.0;
}

void Butter4HP::design(double cutoffHz, double sampleRateHz)
{
    // Standard Butterworth 4-th order Q values for two cascaded biquads.
    static const double Qs[2] = {
        1.0 / (2.0 * std::sin( M_PI       / 8.0)),   // ≈ 1.3066
        1.0 / (2.0 * std::sin((3.0*M_PI)  / 8.0))    // ≈ 0.5412
    };

    const double w0    = 2.0 * M_PI * cutoffHz / sampleRateHz;
    const double cosw0 = std::cos(w0);

    for (int k = 0; k < 2; ++k) {
        const double Q     = Qs[k];
        const double alpha = std::sin(w0) / (2.0 * Q);

        // High-pass biquad coefficients (RBJ cookbook):
        const double b0 =  (1.0 + cosw0) * 0.5;
        const double b1 = -(1.0 + cosw0);
        const double b2 =  (1.0 + cosw0) * 0.5;
        const double a0 =   1.0 + alpha;
        const double a1 =  -2.0 * cosw0;
        const double a2 =   1.0 - alpha;

        const double inv_a0 = 1.0 / a0;
        b0_[k] = b0 * inv_a0;
        b1_[k] = b1 * inv_a0;
        b2_[k] = b2 * inv_a0;
        a1_[k] = a1 * inv_a0;
        a2_[k] = a2 * inv_a0;
    }
    reset();
}

double Butter4HP::step(double x)
{
    // Transposed direct form II, stage 0 → stage 1
    double y;
    for (int k = 0; k < 2; ++k) {
        y       = b0_[k] * x + z1_[k];
        z1_[k]  = b1_[k] * x - a1_[k] * y + z2_[k];
        z2_[k]  = b2_[k] * x - a2_[k] * y;
        x       = y;
    }
    return x;
}

// =============================================================================
//  Symmetric Jacobi eigendecomposition
// =============================================================================
//
// Classic two-sided Jacobi sweep: at each step pick the largest off-diagonal
// element and rotate it to zero.  Iterates until off-diagonal sum is below a
// tolerance or maxSweeps × n²/2 rotations have been performed.
//
// For n ≤ ~64 (our regime) this converges in single-digit sweeps and is
// quicker than calling out to LAPACK.  No external dependency.
// =============================================================================

bool jacobiEigenSym(vector<double> &A,
                    int n,
                    vector<double> &eigenvalues,
                    vector<double> &eigenvectors,
                    int maxSweeps)
{
    eigenvalues.assign(n, 0.0);
    eigenvectors.assign((size_t)n * n, 0.0);
    for (int i = 0; i < n; ++i) eigenvectors[(size_t)i * n + i] = 1.0;

    auto offDiagFrobSq = [&](void) -> double {
        double s = 0.0;
        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q) {
                double v = A[(size_t)p * n + q];
                s += 2.0 * v * v;
            }
        return s;
    };

    const double initialOff = offDiagFrobSq();
    if (initialOff == 0.0) {
        for (int i = 0; i < n; ++i)
            eigenvalues[i] = A[(size_t)i * n + i];
        return true;
    }

    const double tol     = 1e-14 * initialOff;
    const int    maxIter = maxSweeps * (n * (n - 1)) / 2;

    int iter = 0;
    while (iter < maxIter) {
        // Find largest off-diagonal magnitude
        int    p = 0, q = 1;
        double maxAbs = 0.0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                double v = std::fabs(A[(size_t)i * n + j]);
                if (v > maxAbs) { maxAbs = v; p = i; q = j; }
            }
        if (offDiagFrobSq() < tol) break;
        if (maxAbs == 0.0) break;

        const double app = A[(size_t)p * n + p];
        const double aqq = A[(size_t)q * n + q];
        const double apq = A[(size_t)p * n + q];

        const double phi = (aqq - app) / (2.0 * apq);
        double t;
        if (std::fabs(phi) > 1e15) {
            t = 1.0 / (2.0 * phi);
        } else {
            t = (phi >= 0.0)
              ?  1.0 / ( phi + std::sqrt(1.0 + phi * phi))
              :  1.0 / ( phi - std::sqrt(1.0 + phi * phi));
        }
        const double c = 1.0 / std::sqrt(1.0 + t * t);
        const double s = t * c;

        // Update A: rotation in (p,q) plane
        A[(size_t)p * n + p] = app - t * apq;
        A[(size_t)q * n + q] = aqq + t * apq;
        A[(size_t)p * n + q] = 0.0;
        A[(size_t)q * n + p] = 0.0;

        for (int i = 0; i < n; ++i) {
            if (i == p || i == q) continue;
            double aip = A[(size_t)i * n + p];
            double aiq = A[(size_t)i * n + q];
            A[(size_t)i * n + p] =  c * aip - s * aiq;
            A[(size_t)p * n + i] =  A[(size_t)i * n + p];
            A[(size_t)i * n + q] =  s * aip + c * aiq;
            A[(size_t)q * n + i] =  A[(size_t)i * n + q];
        }

        // Update eigenvector matrix: V = V * R
        for (int i = 0; i < n; ++i) {
            double vip = eigenvectors[(size_t)i * n + p];
            double viq = eigenvectors[(size_t)i * n + q];
            eigenvectors[(size_t)i * n + p] =  c * vip - s * viq;
            eigenvectors[(size_t)i * n + q] =  s * vip + c * viq;
        }
        ++iter;
    }

    for (int i = 0; i < n; ++i)
        eigenvalues[i] = A[(size_t)i * n + i];

    return iter < maxIter;
}

// =============================================================================
//  FastICA — Hyvärinen's fixed-point algorithm
// =============================================================================
//
// Faithful port of fpica.m (deflation, pow3, no finetune, no stabilisation)
// preceded by an inline pcamat / whitenv equivalent.  Variable names mirror
// the MATLAB source so the correspondence is auditable line-for-line.
//
// MATLAB convention (column-major) — used in the labbox source:
//     X         (nch × N, mean-removed)        — input
//     C = X*X'/N                                — channel covariance
//     [E, D]                                    — pcamat: E columns are
//                                                 eigenvectors, D = diag(d)
//     wm  = D^(-1/2) E'                         — whitening matrix
//     dwm = E D^(1/2)                           — dewhitening matrix
//     Z   = wm * X                              — whitened data (nch × N)
//
//     for round = 1..K:
//         w  = randn(nch);  w -= B B' w;  w /= ||w||
//         while not converged:
//             w   -= B B' w;  w /= ||w||
//             wOld = w
//             w    = (Z * (Z' * w).^3) / N - 3 w     ← pow3 update (case 10)
//             w   /= ||w||
//             if ||w - wOld|| < eps OR ||w + wOld|| < eps: converged
//         B(:,round) = w
//         A(:,round) = dwm * w
//         W(round,:) = w' * wm
//
// C++ convention (row-major, sample-major) — what we use here:
//     X is laid out as N rows of nch contiguous channels, X[t*nch + c].
//     Spatial covariance:  C[i,j] = sum_t X[t*nch+i] * X[t*nch+j] / N.
//     Whitening: per sample t, Z[t,:] = WM * X[t,:]^T.
//     Pow3 update: y[t]      = sum_c Z[t,c] * w[c]   (contiguous in c)
//                  y[t]      = y[t]^3
//                  newW[c]   = sum_t Z[t,c] * y[t] / N - 3 w[c]   (one
//                              pass through Z, accumulating into newW
//                              while reading Z sample-by-sample so the
//                              inner loop is again contiguous in c).
//
// The two re-writes preserve the MATLAB algorithm exactly while being
// memory-bandwidth-friendly when N ≫ nch (the regime of interest:
// N ≈ 10⁵–10⁶, nch ≈ 4–32).
//
// =============================================================================

bool fastIcaDeflationPow3(const std::vector<double> &X,    // N × nch (row-major)
                          int                       nch,
                          int                       N,
                          int                       K,
                          std::vector<double>      &A,     // nch × K (column-major: A[c + nch*k])
                          std::vector<double>      &W,     // K × nch (row-major:    W[k*nch + c])
                          int                      &outIterTotal,
                          int                      &outAcceptedK,
                          double                    epsilon,
                          int                       maxIterations,
                          int                       failureLimit,
                          uint64_t                  seed)
{
    outIterTotal = 0;
    outAcceptedK = 0;
    A.assign((size_t)nch * K, 0.0);
    W.assign((size_t)K * nch, 0.0);
    if (nch <= 0 || N <= 0 || K <= 0 || K > nch) return false;

    // ----- spatial covariance C = X^T * X / N (nch × nch) -----
    // X is sample-major (N × nch row-major), so C[i,j] = sum_t X[t,i]*X[t,j]/N
    std::vector<double> C((size_t)nch * nch, 0.0);
    for (int t = 0; t < N; ++t) {
        const double *row = X.data() + (size_t)t * nch;
        for (int i = 0; i < nch; ++i) {
            const double xi = row[i];
            for (int j = i; j < nch; ++j)
                C[(size_t)i * nch + j] += xi * row[j];
        }
    }
    for (int i = 0; i < nch; ++i)
        for (int j = i; j < nch; ++j) {
            C[(size_t)i * nch + j] /= (double)N;
            C[(size_t)j * nch + i] = C[(size_t)i * nch + j];
        }

    // ----- eigendecompose C -----
    std::vector<double> eigval, eigvec;
    if (!jacobiEigenSym(C, nch, eigval, eigvec, 200)) return false;

    // pcamat sorts eigenvalues in MATLAB; FastICA does not require it
    // mathematically, but an explicit descending sort makes the output
    // reproducible regardless of Jacobi's traversal order.
    const double eigClamp = 1e-12;
    std::vector<int> order(nch);
    for (int i = 0; i < nch; ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return eigval[a] > eigval[b]; });

    // E (nch × nch, columns are eigenvectors in DESCENDING eigenvalue order)
    std::vector<double> E((size_t)nch * nch, 0.0);
    std::vector<double> dvec((size_t)nch, 0.0);
    for (int j = 0; j < nch; ++j) {
        const int src = order[j];
        dvec[j] = std::max(eigval[src], eigClamp);
        for (int i = 0; i < nch; ++i)
            E[(size_t)i * nch + j] = eigvec[(size_t)i * nch + src];
    }

    // wm  = D^(-1/2) E^T  (nch × nch)         row-major
    // dwm = E D^(1/2)     (nch × nch)         row-major
    std::vector<double> WM((size_t)nch * nch, 0.0);
    std::vector<double> DWM((size_t)nch * nch, 0.0);
    for (int i = 0; i < nch; ++i) {
        const double sqi = 1.0 / std::sqrt(dvec[i]);
        for (int j = 0; j < nch; ++j) {
            // wm[i,j]  = (1/sqrt(d_i)) * E[j,i]
            WM[(size_t)i * nch + j] = sqi * E[(size_t)j * nch + i];
            // dwm[i,j] = E[i,j] * sqrt(d_j)
            DWM[(size_t)i * nch + j] = E[(size_t)i * nch + j] * std::sqrt(dvec[j]);
        }
    }

    // ----- whiten data: Z = X * WM^T  (sample-major: Z[t,:] = WM * X[t,:]^T) -----
    // Z is N × nch, row-major; per-sample matrix-vector product.
    std::vector<double> Z((size_t)N * nch, 0.0);
    for (int t = 0; t < N; ++t) {
        const double *xrow = X.data() + (size_t)t * nch;
        double       *zrow = Z.data() + (size_t)t * nch;
        for (int i = 0; i < nch; ++i) {
            const double *WMi = WM.data() + (size_t)i * nch;
            double s = 0.0;
            for (int c = 0; c < nch; ++c) s += WMi[c] * xrow[c];
            zrow[i] = s;
        }
    }

    // ----- RNG -----
    std::mt19937_64 rng(seed ? seed : std::random_device{}());
    std::normal_distribution<double> ndist(0.0, 1.0);

    // B accumulates accepted unit vectors as columns (nch × K, column-major
    // for fast B^T w / B p inner products).  Layout: B[c + nch*k].
    std::vector<double> B((size_t)nch * K, 0.0);
    int    accepted    = 0;
    int    numFailures = 0;
    int    iterTotal   = 0;

    std::vector<double> w(nch), wOld(nch);
    std::vector<double> y((size_t)N), proj((size_t)K), newW((size_t)nch);

    auto orthoAgainstB = [&](double *vec) {
        // proj_k = sum_c B[c, k] * vec[c]
        for (int k = 0; k < accepted; ++k) {
            double s = 0.0;
            const double *Bk = B.data() + (size_t)nch * k;
            for (int c = 0; c < nch; ++c) s += Bk[c] * vec[c];
            proj[(size_t)k] = s;
        }
        // vec -= sum_k B[:, k] * proj_k
        for (int k = 0; k < accepted; ++k) {
            const double pk = proj[(size_t)k];
            const double *Bk = B.data() + (size_t)nch * k;
            for (int c = 0; c < nch; ++c) vec[c] -= Bk[c] * pk;
        }
    };

    auto normalize = [&](double *vec) -> double {
        double s2 = 0.0;
        for (int c = 0; c < nch; ++c) s2 += vec[c] * vec[c];
        const double n = std::sqrt(s2);
        if (n > 0.0) for (int c = 0; c < nch; ++c) vec[c] /= n;
        return n;
    };

    while (accepted < K) {

        // initial random vector
        for (int c = 0; c < nch; ++c) w[c] = ndist(rng);

        orthoAgainstB(w.data());
        if (normalize(w.data()) == 0.0) {
            ++numFailures;
            if (numFailures > failureLimit) {
                outIterTotal = iterTotal;
                outAcceptedK = accepted;
                return false;
            }
            continue;
        }

        for (int c = 0; c < nch; ++c) wOld[c] = 0.0;

        bool converged = false;
        int  iter      = 0;
        for (; iter < maxIterations; ++iter) {

            orthoAgainstB(w.data());
            normalize(w.data());

            // termination: ||w - wOld|| < eps  OR  ||w + wOld|| < eps
            {
                double dm = 0.0, dp = 0.0;
                for (int c = 0; c < nch; ++c) {
                    const double a = w[c] - wOld[c];
                    const double b = w[c] + wOld[c];
                    dm += a * a;
                    dp += b * b;
                }
                if (std::sqrt(dm) < epsilon || std::sqrt(dp) < epsilon) {
                    converged = true;
                    break;
                }
            }

            // wOld := w (saved for next iteration's convergence test)
            for (int c = 0; c < nch; ++c) wOld[c] = w[c];

            // ── Pow3 update (fpica.m case 10):
            //
            //   y[t]    = ( Z * w )[t]   = sum_c Z[t,c] * w[c]   (contig)
            //   y[t]    = y[t]^3
            //   newW[c] = ( Z^T * y )[c] / N   -   3 * w[c]
            //
            // We fuse the Z^T * y step into a single sample-major sweep:
            //
            //   for t in 0..N:
            //       row = Z[t, :]
            //       yt  = (sum_c row[c] * w[c]) ^ 3
            //       newW[c] += row[c] * yt
            //
            // → newW[c] = sum_t Z[t,c] * y[t]   (only one Z pass needed)
            //
            // Then divide by N and subtract 3*w.

            for (int c = 0; c < nch; ++c) newW[(size_t)c] = 0.0;

            for (int t = 0; t < N; ++t) {
                const double *zrow = Z.data() + (size_t)t * nch;
                double s = 0.0;
                for (int c = 0; c < nch; ++c) s += zrow[c] * w[c];
                const double yt = s * s * s;
                for (int c = 0; c < nch; ++c) newW[(size_t)c] += zrow[c] * yt;
            }
            const double invN = 1.0 / (double)N;
            for (int c = 0; c < nch; ++c)
                w[c] = newW[(size_t)c] * invN - 3.0 * w[c];

            normalize(w.data());
        }

        iterTotal += iter;

        if (!converged) {
            ++numFailures;
            if (numFailures > failureLimit) {
                outIterTotal = iterTotal;
                outAcceptedK = accepted;
                return false;
            }
            continue;   // retry this round
        }

        // Accept: B[:, accepted] = w   (column-major in B: B[c + nch*k])
        {
            double *Bk = B.data() + (size_t)nch * accepted;
            for (int c = 0; c < nch; ++c) Bk[c] = w[c];
        }

        // A[:, accepted] = dwm * w   (column-major in A: A[c + nch*k])
        for (int i = 0; i < nch; ++i) {
            double s = 0.0;
            const double *DWMi = DWM.data() + (size_t)i * nch;
            for (int c = 0; c < nch; ++c) s += DWMi[c] * w[c];
            A[(size_t)i + (size_t)nch * accepted] = s;
        }

        // W[accepted, :] = w^T * wm   (row-major in W)
        for (int j = 0; j < nch; ++j) {
            double s = 0.0;
            for (int i = 0; i < nch; ++i)
                s += w[i] * WM[(size_t)i * nch + j];
            W[(size_t)accepted * nch + j] = s;
        }

        ++accepted;
        numFailures = 0;
    }

    outIterTotal = iterTotal;
    outAcceptedK = accepted;
    return accepted == K;
}

// =============================================================================
//  CLI parsing
// =============================================================================

static void usage(const char *prog)
{
    fprintf(stderr,
        "\n%s — identify, remove and detect EMG events from a .dat\n"
        "\nusage: %s -n N -s SR -c GROUPS [options] basename\n"
        "\n"
        "Required:\n"
        "  -n  N            total number of channels in <basename>.dat\n"
        "  -s  SR           sampling rate (Hz)\n"
        "  -c  GROUPS       channel groups, colon-separated, comma-separated\n"
        "                   channel indices (0-based) within each group\n"
        "                       e.g.  0,1,2,3:8,9,10,11   for two 4-ch groups\n"
        "  basename         session basename (no extension)\n"
        "\n"
        "Event-detection / waveform options (defaults shown):\n"
        "  -w  W            samples per event waveform                  (32)\n"
        "  -p  P            peak sample index within waveform           (16)\n"
        "  -f  F            high-pass cutoff for component ID (Hz)     (100)\n"
        "  -t  T            threshold = T * MAD(|EMG_au|)               (5.0)\n"
        "  -e  MS           envelope smoothing window (ms)              (20)\n"
        "  -r  R            refractory period (samples)                  (0=auto)\n"
        "  -l  L            peak search length (samples)                 (0=auto)\n"
        "  -d  D            downsample factor for ID pass                (3)\n"
        "  -B  S            ID-pass start time (s)                       (0)\n"
        "  -Z  S            ID-pass duration (s)                        (60)\n"
        "  -k  N            chunk size for streaming (samples/channel) (1<<20)\n"
        "\n"
        "Output toggles:\n"
        "  --no-clean       skip writing  <basename>-emgclean.dat\n"
        "  --no-emg-trace   skip writing  <basename>.emg.N\n"
        "  --no-waveforms   skip writing  <basename>.spk.emg.N\n"
        "\n"
        "  -v               verbose\n"
        "  -V               print version and exit\n"
        "  -h               show this help\n",
        programVersion, prog);
}

static bool parseGroups(const char *spec,
                        vector<vector<int>> &out,
                        int nChannels)
{
    out.clear();
    string cur;
    auto flushGroup = [&](void) -> bool {
        if (cur.empty()) return true;
        vector<int> g;
        size_t start = 0;
        while (start <= cur.size()) {
            size_t comma = cur.find(',', start);
            string tok = (comma == string::npos)
                         ? cur.substr(start)
                         : cur.substr(start, comma - start);
            if (!tok.empty()) {
                int ch = std::atoi(tok.c_str());
                if (ch < 0 || ch >= nChannels) {
                    cerr << "process_extractemg: channel " << ch
                         << " out of range [0, " << nChannels << ")\n";
                    return false;
                }
                g.push_back(ch);
            }
            if (comma == string::npos) break;
            start = comma + 1;
        }
        if (!g.empty()) out.push_back(g);
        cur.clear();
        return true;
    };

    for (const char *p = spec; *p; ++p) {
        if (*p == ':') {
            if (!flushGroup()) return false;
        } else if (*p != ' ' && *p != '\t') {
            cur.push_back(*p);
        }
    }
    if (!flushGroup()) return false;
    return !out.empty();
}

static bool parseArgs(int argc, char **argv, EmgArgs &args)
{
    string deferredGroupSpec;   // -c argument; parsed once nChannels is known
    bool   sawC = false;

    for (int i = 1; i < argc; ++i) {
        string a = argv[i];

        if (a == "-h" || a == "--help") { usage(argv[0]); std::exit(0); }
        if (a == "-V" || a == "--version") {
            printf("%s\n", programVersion); std::exit(0);
        }
        if (a == "-v") { args.verbose = true; continue; }
        if (a == "--no-clean")     { args.writeCleanDat = false;  continue; }
        if (a == "--no-emg-trace") { args.writeEmgTrace = false;  continue; }
        if (a == "--no-waveforms") { args.writeWaveforms = false; continue; }

        auto need = [&](void) -> const char* {
            if (i + 1 >= argc) {
                cerr << "process_extractemg: option " << a
                     << " requires an argument\n";
                return nullptr;
            }
            return argv[++i];
        };

        if (a == "-n") { const char *v = need(); if (!v) return false; args.nChannels    = std::atoi(v); }
        else if (a == "-s") { const char *v = need(); if (!v) return false; args.samplingRate = std::atof(v); }
        else if (a == "-c") {
            const char *v = need(); if (!v) return false;
            deferredGroupSpec = v;
            sawC = true;
        }
        else if (a == "-w") { const char *v = need(); if (!v) return false; args.spikeLength      = std::atoi(v); }
        else if (a == "-p") { const char *v = need(); if (!v) return false; args.peakSampleIndex  = std::atoi(v); }
        else if (a == "-f") { const char *v = need(); if (!v) return false; args.hpFreq           = std::atof(v); }
        else if (a == "-t") { const char *v = need(); if (!v) return false; args.thresholdFactor  = std::atof(v); }
        else if (a == "-e") { const char *v = need(); if (!v) return false; args.envelopeWindowMs = std::atof(v); }
        else if (a == "-r") { const char *v = need(); if (!v) return false; args.refractorySamples = std::atoi(v); }
        else if (a == "-l") { const char *v = need(); if (!v) return false; args.peakSearchLength  = std::atoi(v); }
        else if (a == "-d") { const char *v = need(); if (!v) return false; args.downsample        = std::atoi(v); }
        else if (a == "-B") { const char *v = need(); if (!v) return false; args.noiseStartSec     = std::atof(v); }
        else if (a == "-Z") { const char *v = need(); if (!v) return false; args.noiseDurationSec  = std::atof(v); }
        else if (a == "-k") { const char *v = need(); if (!v) return false; args.chunkSamples      = std::atoll(v); }
        else if (a.size() > 0 && a[0] == '-') {
            cerr << "process_extractemg: unknown option " << a << "\n";
            return false;
        }
        else {
            if (!args.basename.empty()) {
                cerr << "process_extractemg: too many positional arguments\n";
                return false;
            }
            args.basename = a;
        }
    }

    if (args.basename.empty() || args.nChannels <= 0
        || args.samplingRate <= 0.0 || !sawC) {
        cerr << "process_extractemg: missing required argument(s)\n";
        usage(argv[0]);
        return false;
    }

    // Now that -n has been seen, parse the deferred -c group specification.
    if (!parseGroups(deferredGroupSpec.c_str(), args.groupChannels, args.nChannels)) {
        cerr << "process_extractemg: invalid -c group specification: '"
             << deferredGroupSpec << "'\n";
        return false;
    }
    if (args.groupChannels.empty()) {
        cerr << "process_extractemg: -c yielded no groups\n";
        return false;
    }

    // Sanity-default the derived parameters
    if (args.refractorySamples <= 0)
        args.refractorySamples = (int)std::round(args.envelopeWindowMs * 1e-3 * args.samplingRate);
    if (args.refractorySamples < 1) args.refractorySamples = 1;
    if (args.peakSearchLength  <= 0) args.peakSearchLength = args.refractorySamples;
    if (args.downsample        <= 0) args.downsample = 1;
    if (args.spikeLength       <= 0) args.spikeLength = 32;
    if (args.peakSampleIndex   <  0 || args.peakSampleIndex >= args.spikeLength)
        args.peakSampleIndex = args.spikeLength / 2;
    if (args.chunkSamples      <  4096) args.chunkSamples = 4096;

    return true;
}

// =============================================================================
//  Helpers
// =============================================================================

static int64_t fileSizeBytes(const string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return (int64_t)st.st_size;
}

// median of |x| in-place destructive copy
static double medianAbs(const vector<double> &x)
{
    if (x.empty()) return 0.0;
    vector<double> tmp(x.size());
    for (size_t i = 0; i < x.size(); ++i) tmp[i] = std::fabs(x[i]);
    std::nth_element(tmp.begin(), tmp.begin() + tmp.size() / 2, tmp.end());
    return tmp[tmp.size() / 2];
}

// Boxcar smoothing of |signal|, in-place into dst (same length).
static void boxcarAbs(const vector<double> &signal,
                      int                   windowSamples,
                      vector<double>       &dst)
{
    const size_t n = signal.size();
    dst.assign(n, 0.0);
    if (n == 0 || windowSamples < 1) return;

    const int half = windowSamples / 2;
    double    acc  = 0.0;
    int       cnt  = 0;

    // Prime accumulator on [0, half)
    for (int i = 0; i < half && (size_t)i < n; ++i) { acc += std::fabs(signal[(size_t)i]); ++cnt; }

    for (size_t i = 0; i < n; ++i) {
        // Add right-edge sample
        const ptrdiff_t addIdx = (ptrdiff_t)i + half;
        if (addIdx >= 0 && (size_t)addIdx < n) { acc += std::fabs(signal[(size_t)addIdx]); ++cnt; }
        // Drop left-edge sample
        const ptrdiff_t dropIdx = (ptrdiff_t)i - half - 1;
        if (dropIdx >= 0 && (size_t)dropIdx < n) { acc -= std::fabs(signal[(size_t)dropIdx]); --cnt; }
        dst[i] = (cnt > 0) ? acc / (double)cnt : 0.0;
    }
}

// =============================================================================
//  Pass 1 — identify EMG component via FastICA on HP-filtered subsample
// =============================================================================
//
// 1.  Read up to noiseDurationSec seconds of the dat starting at
//     noiseStartSec.  Per-channel mean is removed.
// 2.  4-th order Butterworth high-pass at hpFreq is applied to each
//     group channel.  After a brief startup transient, every
//     `downsample`-th sample is kept.
// 3.  The kept samples form an N × nch sample-major matrix; FastICA
//     deflation pow3 is run on it (faithful port of fpica.m).
// 4.  The "EMG" component is the column k of A maximising the
//     uniformity score |sum( A[:, k] / ||A[:, k]|| )|, exactly as in
//     EMG_rm_long.m line 259.  Loading As = A[:, k]; unmixing
//     Ws = W[k, :].
// 5.  Threshold for event detection is computed on the same
//     subsample by projecting the un-filtered, de-meaned data onto
//     Ws and taking 1.4826 * thresholdFactor * MAD(|EMG_au|).
//
// Notes
// -----
// - This is the single-stage analog of EMG_rm_long's 'hw' mode.  The
//   second-stage refinement (FastICA on Wh-projected wide-band data) is
//   omitted; in practice the EMG component is dominant enough in the
//   high band that picking it from the single-stage result is reliable.
// - Returns false on hard failure (non-convergence of all components,
//   degenerate covariance, file errors).  On success populates
//   model.{loading, unmixing, uniformityScore, eigenvalueRatio,
//          threshold, medianEmgEnvelope, nIcaSweeps, converged}.
// =============================================================================

static bool identifyComponent(const EmgArgs    &args,
                              FILE             *fdat,
                              int64_t           totalSamples,
                              const vector<int>&groupChans,
                              GroupModel       &model)
{
    const int nch = (int)groupChans.size();
    if (nch < 2) {
        cerr << "process_extractemg: group has < 2 channels; cannot run FastICA\n";
        return false;
    }

    int64_t startSamp = (int64_t)std::round(args.noiseStartSec * args.samplingRate);
    int64_t durSamp   = (int64_t)std::round(args.noiseDurationSec * args.samplingRate);
    if (startSamp < 0) startSamp = 0;
    if (startSamp + durSamp > totalSamples) durSamp = totalSamples - startSamp;
    if (durSamp <= 0) {
        cerr << "process_extractemg: ID window outside file\n";
        return false;
    }

    if (args.verbose) {
        cout << "  PASS 1 (identification): start=" << startSamp
             << " dur=" << durSamp
             << " (downsample=" << args.downsample
             << ", hpFreq=" << args.hpFreq << " Hz)\n";
    }

    // -----------------------------------------------------------------
    //  1a — per-channel mean over the ID window
    // -----------------------------------------------------------------
    vector<double> chanSum(nch, 0.0);
    {
        if (fseeko(fdat, startSamp * args.nChannels * (off_t)sizeof(int16_t),
                   SEEK_SET) != 0) {
            cerr << "process_extractemg: seek failed (1a): "
                 << std::strerror(errno) << "\n";
            return false;
        }
        const int64_t bufSamp = std::min<int64_t>(durSamp,
                                  (int64_t)std::round(10.0 * args.samplingRate));
        vector<int16_t> raw((size_t)bufSamp * args.nChannels);
        int64_t soFar = 0, nKept = 0;
        while (soFar < durSamp) {
            const int64_t want = std::min(bufSamp, durSamp - soFar);
            const size_t  got  = std::fread(raw.data(), sizeof(int16_t),
                                            (size_t)(want * args.nChannels), fdat);
            const int64_t gotN = (int64_t)(got / args.nChannels);
            if (gotN <= 0) break;
            for (int64_t t = 0; t < gotN; ++t) {
                const int16_t *row = raw.data() + (size_t)t * args.nChannels;
                for (int c = 0; c < nch; ++c)
                    chanSum[(size_t)c] += (double)row[groupChans[(size_t)c]];
            }
            nKept += gotN;
            soFar += gotN;
        }
        if (nKept == 0) {
            cerr << "process_extractemg: empty ID window\n";
            return false;
        }
        for (int c = 0; c < nch; ++c)
            chanSum[(size_t)c] /= (double)nKept;
    }

    if (args.verbose) {
        cout << "    per-channel mean :";
        for (int c = 0; c < nch; ++c) cout << ' ' << chanSum[(size_t)c];
        cout << '\n';
    }

    // -----------------------------------------------------------------
    //  1b — collect HP-filtered + de-meaned subsample for FastICA
    //
    //  Layout: hpData is sample-major, hpData[t * nch + c].
    //  We also stash the un-filtered de-meaned values (dmData, same
    //  layout) because the threshold is computed from |EMG_au| on
    //  the un-filtered raw projection (MATLAB EMG_rm_long line 262).
    // -----------------------------------------------------------------
    vector<Butter4HP> filt((size_t)nch);
    for (int c = 0; c < nch; ++c)
        filt[(size_t)c].design(args.hpFreq, args.samplingRate);

    // settling transient: a few biquad time constants of the high-pass
    const int64_t settleSamp = (int64_t)(4.0 / args.hpFreq * args.samplingRate);

    // Cap the kept-sample count to keep memory in line for very long
    // recordings.  At nch=32, 1M samples × 8 B × 2 buffers = 512 MB.
    const int64_t maxKeep = std::min<int64_t>(
        durSamp / std::max(args.downsample, 1) + 1,
        (int64_t)1'000'000);

    vector<double> hpData;  hpData.reserve((size_t)maxKeep * nch);
    vector<double> dmData;  dmData.reserve((size_t)maxKeep * nch);

    {
        if (fseeko(fdat, startSamp * args.nChannels * (off_t)sizeof(int16_t),
                   SEEK_SET) != 0) {
            cerr << "process_extractemg: seek failed (1b): "
                 << std::strerror(errno) << "\n";
            return false;
        }
        const int64_t bufSamp = std::min<int64_t>(durSamp,
                                  (int64_t)std::round(10.0 * args.samplingRate));
        vector<int16_t> raw((size_t)bufSamp * args.nChannels);
        int64_t soFar = 0, globalSamp = 0;

        while (soFar < durSamp) {
            const int64_t want = std::min(bufSamp, durSamp - soFar);
            const size_t  got  = std::fread(raw.data(), sizeof(int16_t),
                                            (size_t)(want * args.nChannels), fdat);
            const int64_t gotN = (int64_t)(got / args.nChannels);
            if (gotN <= 0) break;
            for (int64_t t = 0; t < gotN; ++t) {
                const int16_t *row = raw.data() + (size_t)t * args.nChannels;
                for (int c = 0; c < nch; ++c) {
                    const double dm = (double)row[groupChans[(size_t)c]]
                                    - chanSum[(size_t)c];
                    const double hp = filt[(size_t)c].step(dm);

                    if (globalSamp >= settleSamp
                        && (globalSamp % args.downsample) == 0
                        && (int64_t)hpData.size() / nch < maxKeep) {
                        hpData.push_back(hp);
                        dmData.push_back(dm);
                    }
                }
                ++globalSamp;
            }
            soFar += gotN;
        }
    }

    const int N = (int)(hpData.size() / nch);
    if (N < nch * 4) {
        cerr << "process_extractemg: insufficient subsample (" << N
             << " samples for nch=" << nch << ")\n";
        return false;
    }

    // -----------------------------------------------------------------
    //  1c — FastICA
    // -----------------------------------------------------------------
    // numOfIC default per labbox: max(round(0.8*nch), min(16, nch))
    int K = std::max((int)std::round(0.8 * nch), std::min(16, nch));
    if (K > nch) K = nch;
    if (K < 1)   K = 1;

    vector<double> Amat;   // nch × K, column-major
    vector<double> Wmat;   // K × nch, row-major
    int icaIter = 0, icaAcc = 0;

    // Use a deterministic seed derived from the group's first channel
    // index so reruns of the same session reproduce.
    const uint64_t seed = 0x5eed1234ull
                        ^ (uint64_t)groupChans.front()
                        ^ ((uint64_t)nch << 32);
    const bool icaOk = fastIcaDeflationPow3(hpData, nch, N, K,
                                            Amat, Wmat,
                                            icaIter, icaAcc,
                                            /*epsilon*/      1e-4,
                                            /*maxIter*/      1000,
                                            /*failureLimit*/ 5,
                                            seed);

    if (icaAcc == 0) {
        cerr << "process_extractemg: FastICA produced no components\n";
        return false;
    }
    if (!icaOk) {
        cerr << "process_extractemg: FastICA accepted only " << icaAcc
             << " of " << K << " components — proceeding with what we got\n";
    }

    model.nIcaSweeps = icaIter;
    model.converged  = icaOk;

    // -----------------------------------------------------------------
    //  1d — pick the EMG component (max |sum(A[:,k]/||A[:,k]||)|)
    // -----------------------------------------------------------------
    int    bestK    = 0;
    double bestUni  = -1.0;
    for (int k = 0; k < icaAcc; ++k) {
        double n2 = 0.0;
        for (int c = 0; c < nch; ++c) {
            const double v = Amat[(size_t)c + (size_t)nch * k];
            n2 += v * v;
        }
        const double n = std::sqrt(n2);
        if (n <= 0.0) continue;
        double s = 0.0;
        for (int c = 0; c < nch; ++c)
            s += Amat[(size_t)c + (size_t)nch * k] / n;
        const double uni = std::fabs(s);
        if (uni > bestUni) { bestUni = uni; bestK = k; }
    }
    model.uniformityScore = bestUni;

    model.loading.assign(nch, 0.0);
    model.unmixing.assign(nch, 0.0);
    {
        double n2 = 0.0;
        for (int c = 0; c < nch; ++c) {
            const double v = Amat[(size_t)c + (size_t)nch * bestK];
            model.loading[(size_t)c] = v;
            n2 += v * v;
        }
        // sign convention: bias toward sum(loading) > 0 so different
        // FastICA seeds give the same canonical EMG vector
        double sumLoad = 0.0;
        for (int c = 0; c < nch; ++c) sumLoad += model.loading[(size_t)c];
        if (sumLoad < 0.0) {
            for (int c = 0; c < nch; ++c) model.loading[(size_t)c] = -model.loading[(size_t)c];
            for (int c = 0; c < nch; ++c)
                model.unmixing[(size_t)c] = -Wmat[(size_t)bestK * nch + c];
        } else {
            for (int c = 0; c < nch; ++c)
                model.unmixing[(size_t)c] = Wmat[(size_t)bestK * nch + c];
        }
        (void)n2;
    }

    // Diagnostic: ratio of leading covariance eigenvalue to the sum.
    // Recompute spatial cov from hpData (this is cheap — same data
    // we just used for FastICA).
    {
        vector<double> Ccov((size_t)nch * nch, 0.0);
        for (int t = 0; t < N; ++t) {
            const double *row = hpData.data() + (size_t)t * nch;
            for (int i = 0; i < nch; ++i)
                for (int j = i; j < nch; ++j)
                    Ccov[(size_t)i * nch + j] += row[i] * row[j];
        }
        for (int i = 0; i < nch; ++i)
            for (int j = i; j < nch; ++j) {
                Ccov[(size_t)i * nch + j] /= (double)N;
                Ccov[(size_t)j * nch + i] = Ccov[(size_t)i * nch + j];
            }
        vector<double> ev, evV;
        if (jacobiEigenSym(Ccov, nch, ev, evV, 200)) {
            double leadAbs = 0.0, sumAbs = 0.0;
            for (double v : ev) {
                const double av = std::fabs(v);
                sumAbs += av;
                if (av > leadAbs) leadAbs = av;
            }
            model.eigenvalueRatio = (sumAbs > 0.0) ? leadAbs / sumAbs : 0.0;
        }
    }

    // -----------------------------------------------------------------
    //  1e — threshold from |EMG_au| on the un-filtered subsample
    //         EMG_au[t] = dot( dmData[t, :], unmixing )
    // -----------------------------------------------------------------
    vector<double> emgAu((size_t)N, 0.0);
    for (int t = 0; t < N; ++t) {
        const double *row = dmData.data() + (size_t)t * nch;
        double s = 0.0;
        for (int c = 0; c < nch; ++c) s += row[c] * model.unmixing[(size_t)c];
        emgAu[(size_t)t] = s;
    }
    const double mad   = medianAbs(emgAu);
    const double sigma = 1.4826 * mad;
    model.threshold         = args.thresholdFactor * sigma;
    model.medianEmgEnvelope = mad;

    if (args.verbose) {
        cout << "    K (numOfIC)               = " << K
             <<       " (accepted " << icaAcc << ", "
             << (icaOk ? "converged" : "partial") << ")\n"
             << "    FastICA total iterations  = " << icaIter << "\n"
             << "    EMG component index       = " << bestK
             <<       " (uniformity = " << bestUni << ")\n"
             << "    cov-eig leading / total   = " << model.eigenvalueRatio << "\n"
             << "    median |EMG_au|           = " << mad << "\n"
             << "    threshold (factor·σ_MAD)  = " << model.threshold << "\n"
             << "    loading As :";
        for (int c = 0; c < nch; ++c) cout << ' ' << model.loading[(size_t)c];
        cout << "\n    unmixing Ws :";
        for (int c = 0; c < nch; ++c) cout << ' ' << model.unmixing[(size_t)c];
        cout << "\n";
    }
    return true;
}

// =============================================================================
//  Pass 2 — streaming EMG removal + event detection
// =============================================================================
//
// Per group we keep:
//   - an array of Butter4HP states (one per group channel) — only used
//     for diagnostic verification of the high-band signal during streaming,
//     but the ACTUAL subtraction is on the de-meaned UN-filtered signal,
//     to match the MATLAB toolbox semantics.
//   - the channel mean vector computed in PASS 1
//   - the loading vector computed in PASS 1
//   - the threshold computed in PASS 1
//   - a running EMG_au[] accumulator (full-session, in RAM as float to
//     keep memory bounded; for very long files we'd want chunked event
//     detection, but for typical sessions ≤ 1 GB float trace is fine)
//
// =============================================================================

struct GroupRuntime {
    vector<int>     channels;         // copy of groupChannels[g]
    vector<double>  chanMean;         // per-channel mean from PASS 1
    GroupModel      model;            // loading, threshold, etc.
    vector<float>   emgAu;            // full-session EMG component (sample rate)
};

static bool streamProcess(const EmgArgs        &args,
                          FILE                 *fdat,
                          int64_t               totalSamples,
                          vector<GroupRuntime> &runtime)
{
    if (args.verbose)
        cout << "  PASS 2 (streaming): " << totalSamples << " samples × "
             << args.nChannels << " channels\n";

    // -------- open the cleaned dat for write (if requested) -----------------
    FILE *fclean = nullptr;
    if (args.writeCleanDat) {
        const string p = args.basename + "-emgclean.dat";
        fclean = std::fopen(p.c_str(), "wb");
        if (!fclean) {
            cerr << "process_extractemg: cannot open " << p
                 << " for write: " << std::strerror(errno) << "\n";
            return false;
        }
    }

    // -------- open per-group .emg.N traces ---------------------------------
    vector<FILE*> femg(runtime.size(), nullptr);
    if (args.writeEmgTrace) {
        for (size_t g = 0; g < runtime.size(); ++g) {
            std::ostringstream oss;
            oss << args.basename << ".emg." << (g + 1);
            femg[g] = std::fopen(oss.str().c_str(), "wb");
            if (!femg[g]) {
                cerr << "process_extractemg: cannot open " << oss.str()
                     << " for write: " << std::strerror(errno) << "\n";
                if (fclean) std::fclose(fclean);
                for (size_t k = 0; k < g; ++k) if (femg[k]) std::fclose(femg[k]);
                return false;
            }
        }
    }

    // -------- pre-allocate the full-session EMG_au accumulator -------------
    for (auto &r : runtime) {
        r.emgAu.assign((size_t)totalSamples, 0.0f);
    }

    // -------- chunked stream loop ------------------------------------------
    if (fseeko(fdat, 0, SEEK_SET) != 0) {
        cerr << "process_extractemg: seek failed: " << std::strerror(errno) << "\n";
        return false;
    }

    const int64_t chunkSamp = args.chunkSamples;
    vector<int16_t> bufRaw  ((size_t)chunkSamp * args.nChannels);
    vector<int16_t> bufClean;
    if (args.writeCleanDat) bufClean.assign((size_t)chunkSamp * args.nChannels, 0);

    int64_t soFar = 0;
    while (soFar < totalSamples) {
        const int64_t want = std::min(chunkSamp, totalSamples - soFar);
        const size_t  got  = std::fread(bufRaw.data(), sizeof(int16_t),
                                        (size_t)(want * args.nChannels), fdat);
        const int64_t gotN = (int64_t)(got / args.nChannels);
        if (gotN <= 0) break;

        // Initialise cleaned buffer with raw (we only modify in-group channels)
        if (args.writeCleanDat)
            std::memcpy(bufClean.data(), bufRaw.data(),
                        (size_t)gotN * args.nChannels * sizeof(int16_t));

        // Per-group EMG projection + subtraction
        for (size_t g = 0; g < runtime.size(); ++g) {
            const auto &chans = runtime[g].channels;
            const auto &As    = runtime[g].model.loading;       // mixing
            const auto &Ws    = runtime[g].model.unmixing;      // unmixing
            const auto &mu    = runtime[g].chanMean;
            const int   nch   = (int)chans.size();
            float       *au   = runtime[g].emgAu.data() + soFar;

            for (int64_t t = 0; t < gotN; ++t) {
                const int16_t *row = bufRaw.data() + (size_t)t * args.nChannels;
                // EMG_au[t] = (x[t,:] - mu) · Ws    (MATLAB: x*Ws')
                double s = 0.0;
                for (int c = 0; c < nch; ++c) {
                    const double v = (double)row[chans[(size_t)c]] - mu[(size_t)c];
                    s += v * Ws[(size_t)c];
                }
                au[t] = (float)s;

                if (args.writeCleanDat) {
                    int16_t *outRow = bufClean.data() + (size_t)t * args.nChannels;
                    // clean[t,c] = raw[t,c] - As[c] * EMG_au[t]
                    // (preserve DC by leaving μ in raw[t,c]; MATLAB
                    // equivalently does x = x - EMG_au*As' on the
                    // de-meaned data and reconstructs DC after.)
                    for (int c = 0; c < nch; ++c) {
                        const double sub = s * As[(size_t)c];
                        const double v   = (double)row[chans[(size_t)c]] - sub;
                        long iv = std::lrint(v);
                        if (iv >  32767) iv =  32767;
                        if (iv < -32768) iv = -32768;
                        outRow[chans[(size_t)c]] = (int16_t)iv;
                    }
                }
            }
        }

        // Write cleaned chunk
        if (args.writeCleanDat) {
            const size_t toWrite = (size_t)gotN * args.nChannels;
            if (std::fwrite(bufClean.data(), sizeof(int16_t),
                            toWrite, fclean) != toWrite) {
                cerr << "process_extractemg: short write to clean dat\n";
                if (fclean) std::fclose(fclean);
                return false;
            }
        }

        // Write per-group .emg.N trace (int16, scaled)
        if (args.writeEmgTrace) {
            vector<int16_t> emgChunk((size_t)gotN);
            for (size_t g = 0; g < runtime.size(); ++g) {
                const float *au = runtime[g].emgAu.data() + soFar;
                for (int64_t t = 0; t < gotN; ++t) {
                    long iv = std::lrint((double)au[t]);
                    if (iv >  32767) iv =  32767;
                    if (iv < -32768) iv = -32768;
                    emgChunk[(size_t)t] = (int16_t)iv;
                }
                if (std::fwrite(emgChunk.data(), sizeof(int16_t),
                                (size_t)gotN, femg[g]) != (size_t)gotN) {
                    cerr << "process_extractemg: short write to .emg." << (g+1) << "\n";
                    if (fclean) std::fclose(fclean);
                    for (auto *fp : femg) if (fp) std::fclose(fp);
                    return false;
                }
            }
        }

        soFar += gotN;
        if (args.verbose && (soFar % (chunkSamp * 10) == 0))
            cout << "    " << soFar << " / " << totalSamples
                 << " samples processed\n";
    }

    if (fclean) std::fclose(fclean);
    for (auto *fp : femg) if (fp) std::fclose(fp);
    return true;
}

// =============================================================================
//  Event detection on the EMG envelope
// =============================================================================

static vector<int64_t> detectEvents(const EmgArgs    &args,
                                    const GroupModel &model,
                                    const vector<float> &emgAu)
{
    vector<int64_t> events;
    if (emgAu.empty() || model.threshold <= 0.0) return events;

    const int envSamp = std::max(1,
                                 (int)std::round(args.envelopeWindowMs * 1e-3
                                                 * args.samplingRate));

    // Smoothed |EMG_au| envelope
    vector<double> auD((size_t)emgAu.size());
    for (size_t i = 0; i < emgAu.size(); ++i) auD[i] = emgAu[i];
    vector<double> env;
    boxcarAbs(auD, envSamp, env);

    // Peaks above threshold separated by at least refractorySamples,
    // refined within a peakSearchLength window.
    const double thr   = model.threshold;
    const int64_t N     = (int64_t)env.size();
    const int64_t refr  = std::max(1, args.refractorySamples);
    const int64_t srch  = std::max(1, args.peakSearchLength);

    int64_t i = 0;
    int64_t lastPeak = -refr;
    while (i < N) {
        if (env[(size_t)i] >= thr && (i - lastPeak) >= refr) {
            // Find local max within [i, min(i+srch, N))
            int64_t end = std::min(i + srch, N);
            int64_t best = i;
            double  bestV = env[(size_t)i];
            for (int64_t j = i + 1; j < end; ++j) {
                if (env[(size_t)j] > bestV) { bestV = env[(size_t)j]; best = j; }
            }
            events.push_back(best);
            lastPeak = best;
            i = best + refr;
        } else {
            ++i;
        }
    }
    return events;
}

// =============================================================================
//  Pass 3 — waveform extraction (mmap-based, group-local channels)
// =============================================================================

static bool extractWaveforms(const EmgArgs         &args,
                             const string          &datPath,
                             int                    g,            // 1-based
                             const vector<int>     &chans,
                             const vector<int64_t> &events,
                             const string          &outSpkPath)
{
    if (events.empty()) {
        FILE *f = std::fopen(outSpkPath.c_str(), "wb");
        if (f) std::fclose(f);
        return true;
    }

    int fd = ::open(datPath.c_str(), O_RDONLY);
    if (fd < 0) {
        cerr << "process_extractemg: open(" << datPath << ") failed: "
             << std::strerror(errno) << "\n";
        return false;
    }
    const int64_t fsize = fileSizeBytes(datPath);
    if (fsize <= 0) { ::close(fd); return false; }

    void *map = ::mmap(nullptr, (size_t)fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        ::close(fd);
        cerr << "process_extractemg: mmap failed: " << std::strerror(errno) << "\n";
        return false;
    }
    const int16_t *raw = static_cast<const int16_t *>(map);
    const int64_t nSampTotal = fsize / (args.nChannels * (int64_t)sizeof(int16_t));

    FILE *fout = std::fopen(outSpkPath.c_str(), "wb");
    if (!fout) {
        ::munmap(map, (size_t)fsize);
        ::close(fd);
        cerr << "process_extractemg: cannot open " << outSpkPath
             << " for write: " << std::strerror(errno) << "\n";
        return false;
    }

    const int W   = args.spikeLength;
    const int P   = args.peakSampleIndex;
    const int nch = (int)chans.size();
    vector<int16_t> wav((size_t)W * nch, 0);

    int64_t nWritten = 0;
    for (int64_t t : events) {
        const int64_t startT = t - P;
        // Bounds check; pad with zeros if event near file edge
        for (int s = 0; s < W; ++s) {
            const int64_t ts = startT + s;
            int16_t *outRow = wav.data() + (size_t)s * nch;
            if (ts >= 0 && ts < nSampTotal) {
                const int16_t *inRow = raw + (size_t)ts * args.nChannels;
                for (int c = 0; c < nch; ++c)
                    outRow[c] = inRow[chans[(size_t)c]];
            } else {
                for (int c = 0; c < nch; ++c) outRow[c] = 0;
            }
        }
        if (std::fwrite(wav.data(), sizeof(int16_t),
                        (size_t)W * nch, fout) != (size_t)W * nch) {
            cerr << "process_extractemg: short write of waveforms (group "
                 << g << ")\n";
            std::fclose(fout);
            ::munmap(map, (size_t)fsize);
            ::close(fd);
            return false;
        }
        ++nWritten;
    }

    std::fclose(fout);
    ::munmap(map, (size_t)fsize);
    ::close(fd);

    if (args.verbose)
        cout << "    waveforms written: " << nWritten << " → " << outSpkPath << "\n";

    return true;
}

// =============================================================================
//  main
// =============================================================================

int main(int argc, char **argv)
{
    EmgArgs args;
    if (!parseArgs(argc, argv, args)) return EXIT_FAILURE;

    const string datPath = args.basename + ".dat";
    const int64_t fsize  = fileSizeBytes(datPath);
    if (fsize < 0) {
        cerr << "process_extractemg: cannot stat " << datPath
             << ": " << std::strerror(errno) << "\n";
        return EXIT_FAILURE;
    }
    const int64_t totalSamples = fsize / (args.nChannels * (int64_t)sizeof(int16_t));
    if (fsize % (args.nChannels * (int64_t)sizeof(int16_t)) != 0) {
        cerr << "process_extractemg: " << datPath << " size " << fsize
             << " not a multiple of nChannels(" << args.nChannels
             << ") × 2 bytes\n";
        return EXIT_FAILURE;
    }

    if (args.verbose) {
        cout << programVersion << "\n"
             << "  basename     : " << args.basename << "\n"
             << "  dat          : " << datPath << " (" << fsize << " B, "
             << totalSamples << " samples)\n"
             << "  nChannels    : " << args.nChannels << "\n"
             << "  samplingRate : " << args.samplingRate << " Hz\n"
             << "  hpFreq       : " << args.hpFreq << " Hz\n"
             << "  thresholdFct : " << args.thresholdFactor << "\n"
             << "  refractory   : " << args.refractorySamples << " samples\n"
             << "  groups       : " << args.groupChannels.size() << "\n";
        for (size_t g = 0; g < args.groupChannels.size(); ++g) {
            cout << "    group " << (g+1) << " (" << args.groupChannels[g].size() << " ch):";
            for (int c : args.groupChannels[g]) cout << ' ' << c;
            cout << "\n";
        }
    }

    FILE *fdat = std::fopen(datPath.c_str(), "rb");
    if (!fdat) {
        cerr << "process_extractemg: cannot open " << datPath
             << ": " << std::strerror(errno) << "\n";
        return EXIT_FAILURE;
    }

    // ------------------------------------------------------------------
    //  PASS 1 — identify EMG component for each group
    // ------------------------------------------------------------------
    vector<GroupRuntime> runtime(args.groupChannels.size());
    for (size_t g = 0; g < args.groupChannels.size(); ++g) {
        runtime[g].channels = args.groupChannels[g];
        if (args.verbose)
            cout << "Group " << (g+1) << ":\n";

        // We want the per-channel mean from the *full* file or at least the
        // ID window — identifyComponent computes it itself internally.
        if (!identifyComponent(args, fdat, totalSamples,
                               runtime[g].channels, runtime[g].model)) {
            std::fclose(fdat);
            return EXIT_FAILURE;
        }

        // Re-derive the per-channel mean from inside identifyComponent
        // (we pass it back via a parallel call here for streaming).
        // Simplest: recompute mean during PASS 2 setup using the same window.
        // For cheapness, use a streaming pass over the entire file mean here:
        runtime[g].chanMean.assign(runtime[g].channels.size(), 0.0);
        {
            // Recompute per-channel mean over the ID window for symmetry with
            // the projection used during identification.
            const int64_t startSamp = (int64_t)std::round(args.noiseStartSec * args.samplingRate);
            int64_t       durSamp   = (int64_t)std::round(args.noiseDurationSec * args.samplingRate);
            if (startSamp + durSamp > totalSamples) durSamp = totalSamples - startSamp;
            if (durSamp < 1) durSamp = 1;

            if (fseeko(fdat,
                       startSamp * args.nChannels * (off_t)sizeof(int16_t),
                       SEEK_SET) != 0) {
                cerr << "process_extractemg: seek failed (mean recompute)\n";
                std::fclose(fdat);
                return EXIT_FAILURE;
            }
            const int64_t bufSamp = std::min<int64_t>(durSamp, 1<<18);
            vector<int16_t> raw((size_t)bufSamp * args.nChannels);
            int64_t soFar = 0, nKept = 0;
            const int    nch = (int)runtime[g].channels.size();
            while (soFar < durSamp) {
                int64_t want = std::min(bufSamp, durSamp - soFar);
                size_t  got  = std::fread(raw.data(), sizeof(int16_t),
                                          (size_t)(want * args.nChannels), fdat);
                int64_t gotN = (int64_t)(got / args.nChannels);
                if (gotN <= 0) break;
                for (int64_t t = 0; t < gotN; ++t) {
                    const int16_t *row = raw.data() + (size_t)t * args.nChannels;
                    for (int c = 0; c < nch; ++c)
                        runtime[g].chanMean[(size_t)c] +=
                            (double)row[runtime[g].channels[(size_t)c]];
                }
                nKept += gotN;
                soFar += gotN;
            }
            if (nKept > 0)
                for (auto &m : runtime[g].chanMean) m /= (double)nKept;
        }
    }

    // ------------------------------------------------------------------
    //  PASS 2 — streaming subtraction + EMG_au accumulation
    // ------------------------------------------------------------------
    if (!streamProcess(args, fdat, totalSamples, runtime)) {
        std::fclose(fdat);
        return EXIT_FAILURE;
    }
    std::fclose(fdat);

    // ------------------------------------------------------------------
    //  Detect events for each group
    // ------------------------------------------------------------------
    vector<vector<int64_t>> allEvents(runtime.size());
    for (size_t g = 0; g < runtime.size(); ++g) {
        allEvents[g] = detectEvents(args, runtime[g].model, runtime[g].emgAu);
        if (args.verbose)
            cout << "Group " << (g+1) << ": " << allEvents[g].size()
                 << " EMG events detected\n";
    }

    // ------------------------------------------------------------------
    //  Write .res.emg.N (timestamps) + .spk.emg.N (waveforms)
    // ------------------------------------------------------------------
    for (size_t g = 0; g < runtime.size(); ++g) {
        // .res.emg.N
        std::ostringstream resOss;
        resOss << args.basename << ".res.emg." << (g + 1);
        FILE *fres = std::fopen(resOss.str().c_str(), "wb");
        if (!fres) {
            cerr << "process_extractemg: cannot open " << resOss.str()
                 << " for write: " << std::strerror(errno) << "\n";
            return EXIT_FAILURE;
        }
        if (!allEvents[g].empty()
            && std::fwrite(allEvents[g].data(), sizeof(int64_t),
                           allEvents[g].size(), fres) != allEvents[g].size()) {
            cerr << "process_extractemg: short write of .res.emg." << (g+1) << "\n";
            std::fclose(fres);
            return EXIT_FAILURE;
        }
        std::fclose(fres);

        // .spk.emg.N
        if (args.writeWaveforms) {
            std::ostringstream spkOss;
            spkOss << args.basename << ".spk.emg." << (g + 1);
            if (!extractWaveforms(args, datPath, (int)(g + 1),
                                  runtime[g].channels,
                                  allEvents[g], spkOss.str()))
                return EXIT_FAILURE;
        }
    }

    // ------------------------------------------------------------------
    //  Write metadata sidecar  <session>.emg.meta.yaml
    // ------------------------------------------------------------------
    {
        const string mp = args.basename + ".emg.meta.yaml";
        std::ofstream f(mp);
        if (!f) {
            cerr << "process_extractemg: cannot open " << mp << " for write\n";
            return EXIT_FAILURE;
        }
        f << "# Generated by " << programVersion << "\n"
          << "samplingRate: " << args.samplingRate << "\n"
          << "hpFreq: "       << args.hpFreq << "\n"
          << "thresholdFactor: " << args.thresholdFactor << "\n"
          << "envelopeWindowMs: " << args.envelopeWindowMs << "\n"
          << "refractorySamples: " << args.refractorySamples << "\n"
          << "spikeLength: " << args.spikeLength << "\n"
          << "peakSampleIndex: " << args.peakSampleIndex << "\n"
          << "groups:\n";
        for (size_t g = 0; g < runtime.size(); ++g) {
            const auto &r = runtime[g];
            f << "  - id: " << (g + 1) << "\n"
              << "    nEvents: " << allEvents[g].size() << "\n"
              << "    uniformityScore: " << r.model.uniformityScore << "\n"
              << "    eigenvalueRatio: " << r.model.eigenvalueRatio << "\n"
              << "    threshold: " << r.model.threshold << "\n"
              << "    medianEnvelope: " << r.model.medianEmgEnvelope << "\n"
              << "    fastica:\n"
              << "      iterations: " << r.model.nIcaSweeps << "\n"
              << "      converged: " << (r.model.converged ? "true" : "false") << "\n"
              << "    channels: [";
            for (size_t c = 0; c < r.channels.size(); ++c) {
                if (c) f << ", ";
                f << r.channels[c];
            }
            f << "]\n"
              << "    channelMean: [";
            for (size_t c = 0; c < r.chanMean.size(); ++c) {
                if (c) f << ", ";
                f << r.chanMean[c];
            }
            f << "]\n"
              << "    loading: [";       // As — mixing column for EMG IC
            for (size_t c = 0; c < r.model.loading.size(); ++c) {
                if (c) f << ", ";
                f << r.model.loading[c];
            }
            f << "]\n"
              << "    unmixing: [";      // Ws — applied to centred raw to get EMG_au
            for (size_t c = 0; c < r.model.unmixing.size(); ++c) {
                if (c) f << ", ";
                f << r.model.unmixing[c];
            }
            f << "]\n";
        }
        if (args.verbose)
            cout << "metadata written → " << mp << "\n";
    }

    return EXIT_SUCCESS;
}
