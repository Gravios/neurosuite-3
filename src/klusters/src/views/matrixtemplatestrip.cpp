/***************************************************************************
 * matrixtemplatestrip.cpp — see matrixtemplatestrip.h.
 *
 * Only computeShade lives here (it needs KlustersDoc/Data); everything else on
 * MatrixTemplateStrip is inline in the header.
 *
 * Copyright (C) 2026 neurosuite-3 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 ***************************************************************************/
#include "matrixtemplatestrip.h"

#include "klustersdoc.h"
#include "data.h"
#include <algorithm>

void MatrixTemplateStrip::computeShade(KlustersDoc& doc, const QList<int>& clusterList,
                                       const std::function<bool(int, int)>& hasData)
{
    const int M = static_cast<int>(cols_.size());
    const int N = clusterList.size();
    shade_.assign(static_cast<std::size_t>(M),
                  std::vector<unsigned char>(static_cast<std::size_t>(N), MatrixStripGrey));
    if (M == 0 || N == 0) return;

    // Per-cluster spike-time RANGE [lo,hi] in samples (two feature reads: the
    // cluster's spike block is time-ordered, so its ends bound it).  cHi < cLo
    // marks a cluster with no spikes at all.
    const int timeDim = doc.data().timeDimension();
    std::vector<double> cLo(static_cast<std::size_t>(N), 0.0),
                        cHi(static_cast<std::size_t>(N), -1.0);
    for (int j = 0; j < N; ++j) {
        const auto idx = doc.data().clusterSpkIndices(clusterList[j]);
        if (idx.isEmpty()) continue;
        const double t0 = static_cast<double>(doc.data().featureValue(idx.first() + 1, timeDim));
        const double t1 = static_cast<double>(doc.data().featureValue(idx.last()  + 1, timeDim));
        cLo[static_cast<std::size_t>(j)] = std::min(t0, t1);
        cHi[static_cast<std::size_t>(j)] = std::max(t0, t1);
    }
    const double sr = doc.getSamplingRate();

    for (int t = 0; t < M; ++t) {
        const double winLo = cols_[static_cast<std::size_t>(t)].a * sr;
        const double winHi = cols_[static_cast<std::size_t>(t)].b * sr;
        for (int j = 0; j < N; ++j) {
            const double lo = cLo[static_cast<std::size_t>(j)], hi = cHi[static_cast<std::size_t>(j)];
            const bool noSpk = (hi < lo) || (hi < winLo) || (lo > winHi);   // ranges disjoint
            // No comparable mean -> solid grey; data but no time overlap -> dim the
            // (still-shown) value; overlap -> full-alpha value.
            shade_[static_cast<std::size_t>(t)][static_cast<std::size_t>(j)] =
                !hasData(t, j) ? MatrixStripGrey : (noSpk ? MatrixStripDim : MatrixStripValue);
        }
    }
}
