/***************************************************************************
 * driftmatrixkernel.h — pure-STL kernels for the drift matrix.
 *
 * Qt-free and header-only so the maths can be unit-tested in isolation.
 * Shifts a cluster mean waveform along the probe depth axis by a micrometre
 * value (modelling probe/tissue drift) and builds the pairwise drift-shifted
 * cross-correlation matrix the DriftMatrixView paints.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#ifndef DRIFTMATRIXKERNEL_H
#define DRIFTMATRIXKERNEL_H

#include <vector>
#include <cmath>
#include <algorithm>

// Peak normalised (cosine) cross-correlation over temporal lags
// [-maxShift, maxShift].  Same maths as tmNormXcorr's cosine path; kept local
// so the kernel has no Qt / templatematrixthread dependency and stays testable.
inline float dmNormXcorr(const std::vector<float>& a,
                         const std::vector<float>& b, int maxShift)
{
    const int N = static_cast<int>(a.size());
    if (N == 0 || static_cast<int>(b.size()) != N) return 0.0f;
    float best = 0.0f;
    for (int lag = -maxShift; lag <= maxShift; ++lag) {
        double sab = 0.0, saa = 0.0, sbb = 0.0;
        for (int i = 0; i < N; ++i) {
            const int j = i + lag;
            if (j < 0 || j >= N) continue;
            const double ai = a[static_cast<size_t>(i)];
            const double bj = b[static_cast<size_t>(j)];
            sab += ai * bj; saa += ai * ai; sbb += bj * bj;
        }
        const double d = std::sqrt(saa * sbb);
        if (d < 1e-12) continue;
        best = std::max(best, static_cast<float>(std::fabs(sab) / d));
    }
    return best;
}

// Resample a channel-major mean waveform [ch*nSamp + sm] along the depth axis by
// deltaUm micrometres, modelling the neuron having drifted +deltaUm in depth.
// depths[ch] is the site depth (y, µm) of each channel — arbitrary order, need
// not be uniform.  shifted[ch] = waveform interpolated at depth
// (depths[ch] - deltaUm); zero past the shallowest / deepest site (drifted off
// the probe).  Linear interpolation between the two channels bracketing the
// target depth.  nChan is small (≈8–64) so the per-call depth sort is cheap.
inline void dmDriftShift(const std::vector<float>& in, int nChan, int nSamp,
                         const std::vector<float>& depths, float deltaUm,
                         std::vector<float>& out)
{
    out.assign(static_cast<size_t>(nChan) * nSamp, 0.0f);
    if (static_cast<int>(depths.size()) != nChan) return;

    std::vector<int> order(static_cast<size_t>(nChan));
    for (int c = 0; c < nChan; ++c) order[static_cast<size_t>(c)] = c;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return depths[static_cast<size_t>(a)]
                                       < depths[static_cast<size_t>(b)]; });

    const float dMin = depths[static_cast<size_t>(order.front())];
    const float dMax = depths[static_cast<size_t>(order.back())];

    for (int c = 0; c < nChan; ++c) {
        const float td = depths[static_cast<size_t>(c)] - deltaUm;   // target depth
        if (td < dMin || td > dMax) continue;                        // off probe -> 0
        int k = 0;
        while (k + 1 < nChan &&
               depths[static_cast<size_t>(order[static_cast<size_t>(k + 1)])] < td)
            ++k;
        const int   a  = order[static_cast<size_t>(k)];
        const int   b  = order[static_cast<size_t>(std::min(k + 1, nChan - 1))];
        const float da = depths[static_cast<size_t>(a)];
        const float db = depths[static_cast<size_t>(b)];
        const float f  = (db > da) ? (td - da) / (db - da) : 0.0f;
        for (int sm = 0; sm < nSamp; ++sm)
            out[static_cast<size_t>(c) * nSamp + sm] =
                (1.0f - f) * in[static_cast<size_t>(a) * nSamp + sm]
                +        f  * in[static_cast<size_t>(b) * nSamp + sm];
    }
}

// Fill an (nClusters x nClusters) drift-shifted xcorr matrix (1-based
// scores(i,j)) from cached cluster means + per-channel depths, at shift deltaUm.
// The ROW cluster is the one shifted: upper triangle (i<j) uses +deltaUm, lower
// (i>j) uses -deltaUm; the diagonal is 1.  Matrix must already be sized
// nClusters x nClusters and provide a 1-based operator()(int,int).
//
// Parallel over rows: iteration i writes only row i+1, so no two threads touch
// the same cell and the result is bit-identical to the serial order.  The view
// calls this on every drift-slider step, so this is what keeps the drag
// interactive on sessions with many clusters.
template <class Matrix>
void dmComputeDriftMatrix(const std::vector<std::vector<float>>& meanWav,
                          const std::vector<float>& depths,
                          int nChan, int nSamp, int maxShift, float deltaUm,
                          Matrix& scores)
{
    const int n = static_cast<int>(meanWav.size());
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
    for (int i = 0; i < n; ++i) {
        std::vector<float> shPlus, shMinus;   // per-thread scratch
        scores(i + 1, i + 1) = 1.0;
        dmDriftShift(meanWav[static_cast<size_t>(i)], nChan, nSamp, depths, +deltaUm, shPlus);
        dmDriftShift(meanWav[static_cast<size_t>(i)], nChan, nSamp, depths, -deltaUm, shMinus);
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            const std::vector<float>& shifted = (i < j) ? shPlus : shMinus; // upper:+  lower:-
            scores(i + 1, j + 1) =
                dmNormXcorr(shifted, meanWav[static_cast<size_t>(j)], maxShift);
        }
    }
}

#endif // DRIFTMATRIXKERNEL_H
